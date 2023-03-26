// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2018-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi_nic.h"
#include "../include/gaudi/asic_reg/gaudi_regs.h"
#include "../include/hw_ip/nic/nic_general.h"
#include <linux/pci.h>

#define NUM_OF_XPCS91_REGS	2

/* MAC configuration */
#define MAC_CFG(addr, data, type)	gaudi_nic_mac_write(nic_port, i, (type), (addr), (data))
#define MAC_CFG_MAC(addr, data)		MAC_CFG((addr), (data), "mac")
#define MAC_CFG_MAC_CORE(addr, data)	MAC_CFG((addr), (data), "mac_core")
#define MAC_CFG_XPCS(addr, data)	MAC_CFG((addr), (data), "xpcs")
#define MAC_CFG_XPCS91(addr, data)	MAC_CFG((addr), (data), "xpcs91")

#define NIC_MAC_STAT_BLOCK_SIZE		(mmNIC1_STAT_BASE - mmNIC0_STAT_BASE)
#define NIC_MAC_STAT_HI_PART		mmNIC0_STAT_DATA_HI_REG
#define NIC_MAC_RX_PORT0_OFFSET		mmNIC0_STAT_ETHERSTATSOCTETS
#define NIC_MAC_RX_PORT1_OFFSET		mmNIC0_STAT_ETHERSTATSOCTETS_2
#define NIC_MAC_TX_PORT0_OFFSET		mmNIC0_STAT_ETHERSTATSOCTETS_4
#define NIC_MAC_TX_PORT1_OFFSET		mmNIC0_STAT_ETHERSTATSOCTETS_6
#define NIC_MAC_FEC_PORT0_OFFSET	0
#define NIC_MAC_FEC_PORT1_OFFSET	16

#define NIC_MAC_STAT_BASE(port) \
			((u64)(NIC_MAC_STAT_BLOCK_SIZE * (u64)((port) >> 1)))

#define NIC_MAC_STAT_RREG32(port, reg) \
			RREG32(NIC_MAC_STAT_BASE(port) + (reg))

static u32 gaudi_nic_mac_addr_convert(int mac, char *cfg_type, u32 addr)
{
	if (!strcmp(cfg_type, "xpcs")) {
		if (addr >= 200 && addr <= 219)
			addr = addr - 200 + 54;
		else if (addr >= 400 && addr <= 419)
			addr = addr - 400 + 74;
		else if (addr >= (1 << 15))
			addr = addr - (1 << 15) + 95;

		addr = addr * 4 + mac * (1 << 12);
	} else if (!strcmp(cfg_type, "mac")) {
		addr = addr + mac * (1 << 12) + (1 << 10);
	} else if (!strcmp(cfg_type, "mac_core")) {
		addr = addr + (1 << 15);
	} else if (!strcmp(cfg_type, "xpcs91")) {
		addr = addr * 4 + (1 << 11) * 10;
	}

	return addr + 0xCC0000;
}

static void gaudi_nic_mac_write(struct hl_nic_port *nic_port, int mac, char *cfg_type, u32 addr,
				u32 data)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	addr = gaudi_nic_mac_addr_convert(mac, cfg_type, addr);

	NIC_MACRO_WREG32(addr, data);
}

u32 gaudi_nic_mac_read(struct hl_nic_port *nic_port, int mac, char *cfg_type, u32 addr)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	addr = gaudi_nic_mac_addr_convert(mac, cfg_type, addr);

	return NIC_MACRO_RREG32(addr);
}

/* must be called under mutex_lock(&nic_port->qpc_lock) */
static int gaudi_nic_qpc_op(struct hl_nic_port *nic_port, u32 qpn, bool is_req,
				bool wait_for_completion)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	u32 status, port = nic_port->port;
	int rc = 0;

	NIC_WREG32(mmNIC0_QPC0_GW_CTRL,
			(qpn << NIC0_QPC0_GW_CTRL_QPN_SHIFT) |
			(is_req << NIC0_QPC0_GW_CTRL_REQUESTER_SHIFT));

	NIC_WREG32(mmNIC0_QPC0_GW_BUSY, 1);

	/* do not poll on registers when reset was initiated by FW */
	if (wait_for_completion && !hdev->reset_info.fw_reset)
		rc = hl_poll_timeout(
			hdev,
			mmNIC0_QPC0_GW_BUSY + NIC_CFG_BASE(port),
			status,
			!status,
			1000,
			nic_props->nic_qpc_cache_inv_timeout);

	return rc;
}

static int gaudi_nic_qpc_write_masked(struct hl_nic_port *nic_port,
					const void *qpc_data,
					const struct qpc_mask *qpc_mask,
					u32 qpn, bool is_req)
{
	struct hl_device *hdev;
	const u32 *data, *mask;
	u32 port, size;
	int rc, i;

	hdev = nic_port->hdev;
	port = nic_port->port;
	data = qpc_data;
	mask = (const u32 *) qpc_mask;
	size = is_req ? sizeof(struct gaudi_qpc_requester) : sizeof(struct gaudi_qpc_responder);

	/* Don't write to the Gw if its busy with prev operation */
	if (NIC_RREG32(mmNIC0_QPC0_GW_BUSY)) {
		if (hl_device_operational(hdev, NULL))
			dev_err(hdev->dev, "Cannot write to port %d QP %d %s QPC, GW is busy\n",
				port, qpn, is_req ? "requester" : "responder");

		/* Since the device doesn't function once simulator terminated, we would like to
		 * avoid any device failure prints that followed.
		 */
		return (hdev->pdev || !hdev->device_fini_pending) ? -EBUSY : 0;
	}

	/* Set the appropriate mask regs and write the data*/
	for (i = 0 ; i < sizeof(struct qpc_mask) / sizeof(u32) ; i++)
		NIC_WREG32(mmNIC0_QPC0_GW_MASK_0 + i * sizeof(u32), mask[i]);

	for (i = 0 ; i < size / sizeof(u32) ; i++)
		NIC_WREG32(mmNIC0_QPC0_GW_DATA_0 + i * sizeof(u32), data[i]);

	rc = gaudi_nic_qpc_op(nic_port, qpn, is_req, true);
	if (rc && hl_device_operational(hdev, NULL))
		/* Device might not respond during reset if the reset was due to error */
		dev_err(hdev->dev, "%s QPC GW timeout, port: %d\n",
			is_req ? "requester" : "responder", port);

	return rc;
}

static int gaudi_nic_qpc_write(struct hl_nic_port *nic_port, void *qpc, struct qpc_mask *qpc_mask,
					u32 qpn, bool is_req)
{
	struct qpc_mask mask = {};
	u32 data_size = is_req ? sizeof(struct gaudi_qpc_requester) :
								sizeof(struct gaudi_qpc_responder);

	if (!qpc_mask) {
		/* NULL mask flags full QPC write */
		memset(&mask, 0xFF, data_size);
		qpc_mask = &mask;
	}

	return gaudi_nic_qpc_write_masked(nic_port, qpc, qpc_mask, qpn, is_req);
}

static int gaudi_nic_qpc_invalidate(struct hl_nic_port *nic_port, struct hl_qp *qp, bool is_req)
{
	struct qpc_mask mask = {};
	struct gaudi_qpc_requester req_qpc = {};
	struct gaudi_qpc_responder res_qpc = {};
	void *qpc;

	if (is_req) {
		REQ_QPC_SET_CONG_EN(mask, 3);
		REQ_QPC_SET_CONG_WINDOW(mask, GENMASK(21, 0));
		REQ_QPC_SET_CONG_EN(req_qpc, 1);
		REQ_QPC_SET_CONG_WINDOW(req_qpc, 0);
		qpc = &req_qpc;
	} else {
		RES_QPC_SET_VALID(mask, 1);
		RES_QPC_SET_VALID(res_qpc, 0);
		qpc = &res_qpc;
	}

	return gaudi_nic_qpc_write_masked(nic_port, qpc, &mask, qp->qp_id, is_req);
}

static int gaudi_nic_qpc_clear(struct hl_nic_port *nic_port, struct hl_qp *qp, bool is_req)
{
	struct qpc_mask mask;
	struct gaudi_qpc_requester req_qpc = {};
	struct gaudi_qpc_responder res_qpc = {};
	void *qpc = is_req ? (void *) &req_qpc : (void *) &res_qpc;

	memset(&mask, 0xFF, sizeof(mask));

	return gaudi_nic_qpc_write_masked(nic_port, qpc, &mask, qp->qp_id, is_req);
}

static int gaudi_nic_qpc_read(struct hl_nic_port *nic_port, void *qpc, u32 qpn, bool is_req)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 *data, port, size;
	int i, rc;

	port = nic_port->port;
	data = qpc;
	size = is_req ? sizeof(struct gaudi_qpc_requester) : sizeof(struct gaudi_qpc_responder);

	/* Don't write to the Gw if its busy with prev operation */
	if (NIC_RREG32(mmNIC0_QPC0_GW_BUSY)) {
		if (hl_device_operational(hdev, NULL))
			dev_err(hdev->dev, "Cannot write to port %d QP %d %s QPC, GW is busy\n",
				port, qpn, is_req ? "requester" : "responder");

		/* Since the device doesn't function once simulator terminated, we would like to
		 * avoid any device failure prints that followed.
		 */
		return (hdev->pdev || !hdev->device_fini_pending) ? -EBUSY : 0;
	}

	/* Clear the mask gateway regs which will cause the operation to be a read */
	for (i = 0 ; i < GW_MASK_REG_NUM ; i++)
		NIC_WREG32(mmNIC0_QPC0_GW_MASK_0 + i * sizeof(u32), 0);

	/* In read we must wait for op to complete */
	rc = gaudi_nic_qpc_op(nic_port, qpn, is_req, true);
	if (rc)
		return rc;

	for (i = 0 ; i < size / sizeof(u32) ; i++)
		data[i] = NIC_RREG32(mmNIC0_QPC0_GW_DATA_0 + i * sizeof(u32));

	return 0;
}

static int gaudi_nic_qpc_query(struct hl_nic_port *nic_port, u32 qpn, bool is_req,
				struct hl_nic_qpc_attr *attr)
{
	struct hl_device *hdev = nic_port->hdev;
	struct gaudi_qpc_requester req_qpc;
	struct gaudi_qpc_responder res_qpc;
	u32 port = nic_port->port;
	int rc;

	if (is_req) {
		rc = gaudi_nic_qpc_read(nic_port, (void *) &req_qpc, qpn, is_req);
		if (rc)
			goto out_err;

		attr->valid = REQ_QPC_GET_VALID(req_qpc);
		attr->in_work = REQ_QPC_GET_IN_WORK(req_qpc);
		attr->error = REQ_QPC_GET_ERROR(req_qpc);
	} else {
		rc = gaudi_nic_qpc_read(nic_port, (void *) &res_qpc, qpn, is_req);
		if (rc)
			goto out_err;

		attr->valid = RES_QPC_GET_VALID(res_qpc);
		attr->in_work = RES_QPC_GET_IN_WORK(res_qpc);
		attr->conn_state = RES_QPC_GET_CONN_STATE(res_qpc);
	}

	return 0;

out_err:
	dev_err(hdev->dev, "%s QPC GW read timeout, port: %d, qpn: %u\n",
		is_req ? "requester" : "responder", port, qpn);
	return rc;
}

static void gaudi_nic_port_sw_fini(struct hl_nic_port *nic_port)
{
	struct gaudi_nic_port *gaudi_nic = nic_port->nic_specific;
	struct hl_device *hdev = gaudi_nic->hdev;

	mutex_destroy(&gaudi_nic->cfg_lock);

	hl_asic_dma_free_coherent(hdev, gaudi_nic->qp_err_mem.size, gaudi_nic->qp_err_mem.addr,
					gaudi_nic->qp_err_mem.dma_addr);

	hl_asic_dma_free_coherent(hdev, gaudi_nic->rx_mem.size, gaudi_nic->rx_mem.addr,
					gaudi_nic->rx_mem.dma_addr);
}

static int gaudi_nic_port_sw_init(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct gaudi_nic_port *gaudi_nic;
	struct gaudi_device *gaudi;
	u32 port = nic_port->port;
	int rc;

	gaudi = hdev->asic_specific;
	gaudi_nic = &gaudi->nic_ports[port];
	gaudi_nic->hdev = hdev;
	gaudi_nic->nic_port = nic_port;
	nic_port->nic_specific = gaudi_nic;

	nic_port->nic_macro = &hdev->nic.nic_macros[port >> 1];

	gaudi_nic->rx_mem.size = NIC_RX_SIZE * NIC_RAW_ELEM_SIZE;

	gaudi_nic->rx_mem.addr = hl_asic_dma_alloc_coherent(hdev, gaudi_nic->rx_mem.size,
								&gaudi_nic->rx_mem.dma_addr,
								GFP_KERNEL);
	if (!gaudi_nic->rx_mem.addr) {
		dev_err(hdev->dev, "Failed to allocate Rx memory, port: %d\n", port);
		return -ENOMEM;
	}

	gaudi_nic->qp_err_mem.size = QP_ERR_BUF_SIZE;

	gaudi_nic->qp_err_mem.addr = hl_asic_dma_alloc_coherent(hdev, gaudi_nic->qp_err_mem.size,
								&gaudi_nic->qp_err_mem.dma_addr,
								GFP_KERNEL);
	if (!gaudi_nic->qp_err_mem.addr) {
		dev_err(hdev->dev, "Failed to allocate QP error memory, port: %d\n", port);
		rc = -ENOMEM;
		goto free_rx_mem;
	}

	/* Gaudi is considered to work in advanced mode by default */
	gaudi_nic->advanced = true;

	mutex_init(&gaudi_nic->cfg_lock);

	return 0;

free_rx_mem:
	hl_asic_dma_free_coherent(hdev, gaudi_nic->rx_mem.size, gaudi_nic->rx_mem.addr,
					gaudi_nic->rx_mem.dma_addr);

	return rc;
}

static void gaudi_nic_macro_sw_fini(struct hl_nic_macro *nic_macro)
{
	struct gaudi_nic_macro *gaudi_macro = nic_macro->asic_priv;

	mutex_destroy(&gaudi_macro->macro_cfg_lock);
}

static int gaudi_nic_macro_sw_init(struct hl_nic_macro *nic_macro)
{
	struct gaudi_device *gaudi = nic_macro->hdev->asic_specific;
	struct gaudi_nic_macro *gaudi_macro;

	gaudi_macro = &gaudi->nic_macros[nic_macro->idx];

	nic_macro->asic_priv = gaudi_macro;

	mutex_init(&gaudi_macro->macro_cfg_lock);

	return 0;
}

static void gaudi_get_cq_id_range(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id)
{
	/* only a single CQ is supported per port */
	*min_id = 0;
	*max_id = 0;
}

static int gaudi_user_cq_set(struct hl_nic_user_cq *user_cq,
				struct hl_nic_user_cq_set_in_params *in,
				struct hl_nic_user_cq_set_out_params *out)
{
	struct hl_nic_port *nic_port = user_cq->nic_port;
	struct gaudi_nic_port *gaudi_nic = nic_port->nic_specific;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_mem_in args;
	u64 user_cq_va;
	u32 port = nic_port->port;
	int rc;

	memset(&args, 0, sizeof(args));

	args.flags = HL_MEM_USERPTR;
	args.map_host.host_virt_addr = in->addr;
	/* add cache line size for later alignment */
	args.map_host.mem_size = in->num_of_cqes * sizeof(struct cqe) + DEVICE_CACHE_LINE_SIZE;

	/* Pin user memory, map it to PMMU and get device VA. */
	rc = map_device_va(hdev->kernel_ctx, &args, &gaudi_nic->user_cq_va);
	if (rc) {
		dev_dbg(hdev->dev, "Failed to map NIC user CQ buffer, port %d\n", port);
		return rc;
	}

	/* user CQ addr must be aligned to cache line_size */
	user_cq_va = ALIGN(gaudi_nic->user_cq_va, DEVICE_CACHE_LINE_SIZE);

	NIC_WREG32(mmNIC0_RXE0_CQ_BASE_ADDR_31_7, user_cq_va & NIC0_RXE0_CQ_BASE_ADDR_31_7_R_MASK);
	NIC_WREG32(mmNIC0_RXE0_CA_BASE_ADDR_49_32, user_cq_va >> 32);
	NIC_WREG32(mmNIC0_RXE0_CQ_MASK, in->num_of_cqes - 1);
	NIC_WREG32(mmNIC0_RXE0_CQ_PRODUCER_INDEX, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_CONSUMER_INDEX, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_WRITE_INDEX, 0);

	return rc;
}

static int gaudi_user_cq_unset(struct hl_nic_user_cq *user_cq)
{
	return 0;
}

static void gaudi_user_cq_destroy(struct hl_nic_user_cq *user_cq)
{
	struct hl_nic_port *nic_port = user_cq->nic_port;
	struct gaudi_nic_port *gaudi_nic = nic_port->nic_specific;
	struct hl_device *hdev = gaudi_nic->hdev;
	u32 port = nic_port->port;
	struct hl_mem_in args;
	int rc;

	NIC_WREG32(mmNIC0_RXE0_CQ_BASE_ADDR_31_7, 0);
	NIC_WREG32(mmNIC0_RXE0_CA_BASE_ADDR_49_32, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_MASK, 0);

	memset(&args, 0, sizeof(args));
	args.unmap.device_virt_addr = gaudi_nic->user_cq_va;
	rc = unmap_device_va(hdev->kernel_ctx, &args, false);
	if (rc)
		dev_dbg(hdev->dev, "Failed to unmap NIC user CQ buffer, port %d\n", port);
}

static void gaudi_user_cq_update_ci(struct hl_nic_port *nic_port, u32 ci)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	NIC_WREG32(mmNIC0_RXE0_CQ_CONSUMER_INDEX, ci);
}

static void cq_overrun_handler(struct gaudi_nic_port *gaudi_nic)
{
	struct hl_nic_port *nic_port = gaudi_nic->nic_port;
	struct hl_device *hdev = gaudi_nic->hdev;
	u32 port = nic_port->port;

	if (unlikely(!hl_nic_is_port_open(nic_port)))
		return;

	if (NIC_RREG32(mmNIC0_RXE0_MSI_CAUSE) & 2) {
		if (hl_device_operational(hdev, NULL))
			dev_crit(hdev->dev, "NIC CQ overrun, port %d\n", nic_port->port);
		NIC_WREG32(mmNIC0_RXE0_MSI_CAUSE, 0);
		NIC_WREG32(mmNIC0_RXE0_CQ_MSI_CAUSE_CLR, 0xFFFF);
		/* flush the cause clear */
		NIC_RREG32(mmNIC0_RXE0_CQ_MSI_CAUSE_CLR);
	}
}

static void cq_overrun_work(struct work_struct *work)
{
	struct gaudi_nic_port *gaudi_nic = container_of(work, struct gaudi_nic_port,
							cq_overrun_work.work);

	struct hl_nic_port *nic_port = gaudi_nic->nic_port;

	cq_overrun_handler(gaudi_nic);

	queue_delayed_work(nic_port->cq_wq, &gaudi_nic->cq_overrun_work, msecs_to_jiffies(500));
}

irqreturn_t gaudi_nic_cq_irq_handler(int irq, void *arg)
{
	struct hl_device *hdev = arg;
	struct gaudi_device *gaudi;
	int i;

	gaudi = hdev->asic_specific;

	/* one IRQ for all ports, need to iterate and read the cause */
	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		cq_overrun_handler(&gaudi->nic_ports[i]);
	}

	return IRQ_HANDLED;
}

static int __gaudi_nic_cq_init(struct gaudi_nic_port *gaudi_nic)
{
	struct hl_nic_port *nic_port = gaudi_nic->nic_port;
	struct hl_device *hdev = gaudi_nic->hdev;
	u32 port = nic_port->port;
	char cq_wq_name[32] = {0};
	int rc;

	snprintf(cq_wq_name, sizeof(cq_wq_name) - 1, "hl%u-nic%d-cq-wq", hdev->cdev_idx, port);

	/*
	 * Use only one thread because cq_irq_work() should not be executed
	 * concurrently for the same port.
	 */
	nic_port->cq_wq = create_singlethread_workqueue(cq_wq_name);
	if (!nic_port->cq_wq) {
		dev_err(hdev->dev, "Failed to create NIC CQ WQ, port: %d\n",
			port);
		rc = -ENOMEM;
		goto cq_unmap;
	}

	NIC_WREG32(mmNIC0_RXE0_CQ_WRITE_INDEX, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_PRODUCER_INDEX, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_CONSUMER_INDEX, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_CFG0,
			(1 << NIC0_RXE0_CQ_CFG0_ENABLE_SHIFT) |
			(1 << NIC0_RXE0_CQ_CFG0_INTERRUPT_MASK_SHIFT) |
			(8 << NIC0_RXE0_CQ_CFG0_CREDIT_SHIFT) |
			(1 << NIC0_RXE0_CQ_CFG0_WRAPAROUND_EN_SHIFT) |
			(1 << NIC0_RXE0_CQ_CFG0_SOB_CQ_MUTEX_SHIFT) |
			(24 << NIC0_RXE0_CQ_CFG0_CQ_SELECT_SHIFT));

	if (hdev->nic_poll_enable) {
		/* in polling mode we need to check ourselves for CQ overrun */
		INIT_DELAYED_WORK(&gaudi_nic->cq_overrun_work, cq_overrun_work);
		queue_delayed_work(nic_port->cq_wq, &gaudi_nic->cq_overrun_work,
					msecs_to_jiffies(500));
	}

	NIC_WREG32(mmNIC0_RXE0_MSI_CASUE_MASK, 2);
	NIC_WREG32(mmNIC0_RXE0_MSI_CAUSE, 0);

	/* Perform read from the device to flush all configurations */
	NIC_RREG32(mmNIC0_RXE0_MSI_CAUSE);

	return 0;

cq_unmap:

	return rc;
}

static void __gaudi_nic_cq_fini(struct gaudi_nic_port *gaudi_nic)
{
	struct hl_nic_port *nic_port = gaudi_nic->nic_port;
	struct hl_device *hdev = gaudi_nic->hdev;

	if (hdev->nic_poll_enable)
		cancel_delayed_work_sync(&gaudi_nic->cq_overrun_work);

	destroy_workqueue(nic_port->cq_wq);
}

static void gaudi_nic_cq_fini(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int i, cq_irq;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++)
		__gaudi_nic_cq_fini(&gaudi->nic_ports[i]);

	if (gaudi->nic_cq_irq_enable) {
		cq_irq = pci_irq_vector(hdev->pdev, CQ_MSI_IDX);
		synchronize_irq(cq_irq);
		free_irq(cq_irq, hdev);
		gaudi->nic_cq_irq_enable = false;
	}
}

static int gaudi_nic_cq_init(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int rc, i;

	/* SW-37077: in Gaudi, even ports push their responder CQEs to the odd port CQ. Hence we
	 * need to initialize all CQs of all ports regardless of their enablement, or else we'll get
	 * a RAZWI.
	 */
	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		rc = __gaudi_nic_cq_init(&gaudi->nic_ports[i]);
		if (rc) {
			dev_err(hdev->dev,
				"NIC CQ init failed, port: %d, rc: %d\n", i,
				rc);
			goto err;
		}
	}

	return 0;

err:
	gaudi_nic_cq_fini(hdev);

	return rc;
}

static void gaudi_nic_mac_ch_init(struct hl_nic_port *nic_port)
{
	u32 port = nic_port->port;

	if (nic_port->auto_neg_enable && nic_port->speed == SPEED_100000) {
		nic_port->power_up_mask = (port & 1) ? 0xC : 0x3;
		nic_port->fw_tuning_mask = (port & 1) ? 0xC : 0x3;
		nic_port->auto_neg_mask = (port & 1) ? 0x4 : 0x1;
	} else {
		nic_port->power_up_mask = (port & 1) ? 0xC : 0x3;
		nic_port->fw_tuning_mask = nic_port->power_up_mask;
	}
}

static int gaudi_nic_config_port_mac_ch(struct gaudi_nic_port *gaudi_nic)
{
	struct hl_nic_port *nic_port = gaudi_nic->nic_port;
	u32 port = nic_port->port, speed = nic_port->speed;
	struct hl_device *hdev = gaudi_nic->hdev;
	struct asic_fixed_properties *prop;
	struct cpucp_packet pkt;
	int rc, i;

	memset(&pkt, 0, sizeof(pkt));
	prop = &hdev->asic_prop;

	if (prop->fw_app_cpu_boot_dev_sts0 & CPU_BOOT_DEV_STS0_FW_NIC_MAC_EN) {
		memset(&pkt, 0, sizeof(pkt));

		pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_MAC_CFG <<
					CPUCP_PKT_CTL_OPCODE_SHIFT);
		/* we used this field as port_index didn't exist yet */
		pkt.index = cpu_to_le32(port);
		pkt.value = cpu_to_le64(speed);

		rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt,
							sizeof(pkt), 0, NULL);

		if (rc) {
			dev_err(hdev->dev,
				"Failed to init MAC via FW for port %d, %d\n",
				port, rc);
			return rc;
		}

		return 0;
	}

	for (i = 0 ; i < NIC_MAC_LANES ; i++) {
		/* H/W WA for error length */
		MAC_CFG_MAC(0x14, 8192);
		MAC_CFG_MAC(0x20, 4);
		MAC_CFG_MAC(0x1C, 4);

		MAC_CFG_MAC(0x54, 0xFFFFFFFF);
		MAC_CFG_MAC(0x58, 0xFFFFFFFF);
		MAC_CFG_MAC(0x5C, 0xFFFFFFFF);
		MAC_CFG_MAC(0x60, 0xFFFFFFFF);

		MAC_CFG_MAC(0x84, 0xFFFFFFFF);
		MAC_CFG_MAC(0x88, 0xFFFFFFFF);
		MAC_CFG_MAC(0x8C, 0xFFFFFFFF);
		MAC_CFG_MAC(0x90, 0xFFFFFFFF);

		MAC_CFG_MAC(0x64, 0x7FFF7FFF);
		MAC_CFG_MAC(0x68, 0x7FFF7FFF);
		MAC_CFG_MAC(0x6C, 0x7FFF7FFF);
		MAC_CFG_MAC(0x70, 0x7FFF7FFF);

		MAC_CFG_MAC(0x94, 0x7FFF7FFF);
		MAC_CFG_MAC(0x98, 0x7FFF7FFF);
		MAC_CFG_MAC(0x9C, 0x7FFF7FFF);
		MAC_CFG_MAC(0xa0, 0x7FFF7FFF);

		switch (speed) {
		case SPEED_10000:
			MAC_CFG_XPCS(0x8010, 3);
			break;
		case SPEED_25000:
			MAC_CFG_XPCS(0x8002, 0x4FFF);
			MAC_CFG_XPCS(0x8010, 5);
			MAC_CFG_XPCS(0x8008, 0x68C1);
			MAC_CFG_XPCS(0x8009, 0x21);
			MAC_CFG_XPCS(0x800A, 0xC4F0);
			MAC_CFG_XPCS(0x800B, 0xE6);
			MAC_CFG_XPCS(0x800C, 0x65C5);
			MAC_CFG_XPCS(0x800D, 0x9B);
			MAC_CFG_XPCS(0x800E, 0x79A2);
			MAC_CFG_XPCS(0x800F, 0x3D);
			break;
		case SPEED_50000:
			MAC_CFG_XPCS(0x8002, 0x4FFF);
			MAC_CFG_XPCS(0x8010, 0);
			MAC_CFG_XPCS(0x8008, 0x7690);
			MAC_CFG_XPCS(0x8009, 0x47);
			MAC_CFG_XPCS(0x800A, 0xC4F0);
			MAC_CFG_XPCS(0x800B, 0xE6);
			MAC_CFG_XPCS(0x800C, 0x65C5);
			MAC_CFG_XPCS(0x800D, 0x9B);
			MAC_CFG_XPCS(0x800E, 0x79A2);
			MAC_CFG_XPCS(0x800F, 0x3D);
			break;
		case SPEED_100000:
			MAC_CFG_XPCS(0x8002, 0x3FFF);
			MAC_CFG_XPCS(0x8010, 0);
			MAC_CFG_XPCS(0x8008, 0x68C1);
			MAC_CFG_XPCS(0x8009, 0x21);
			MAC_CFG_XPCS(0x800A, 0x719D);
			MAC_CFG_XPCS(0x800B, 0x8E);
			MAC_CFG_XPCS(0x800C, 0x4B59);
			MAC_CFG_XPCS(0x800D, 0xE8);
			MAC_CFG_XPCS(0x800E, 0x954D);
			MAC_CFG_XPCS(0x800F, 0x7B);
			MAC_CFG_XPCS(0x8048, 0x07F5);
			MAC_CFG_XPCS(0x8049, 0x09);
			MAC_CFG_XPCS(0x804A, 0x14DD);
			MAC_CFG_XPCS(0x804B, 0xC2);
			MAC_CFG_XPCS(0x804C, 0x4A9A);
			MAC_CFG_XPCS(0x804D, 0x26);
			MAC_CFG_XPCS(0x804E, 0x457B);
			MAC_CFG_XPCS(0x804F, 0x66);
			MAC_CFG_XPCS(0x8050, 0x24A0);
			MAC_CFG_XPCS(0x8051, 0x76);
			MAC_CFG_XPCS(0x8052, 0xC968);
			MAC_CFG_XPCS(0x8053, 0xFB);
			MAC_CFG_XPCS(0x8054, 0x6CFD);
			MAC_CFG_XPCS(0x8055, 0x99);
			MAC_CFG_XPCS(0x8056, 0x91B9);
			MAC_CFG_XPCS(0x8057, 0x55);
			MAC_CFG_XPCS(0x8058, 0xB95C);
			MAC_CFG_XPCS(0x8059, 0xB2);
			MAC_CFG_XPCS(0x805A, 0xF81A);
			MAC_CFG_XPCS(0x805B, 0xBD);
			MAC_CFG_XPCS(0x805C, 0xC783);
			MAC_CFG_XPCS(0x805D, 0xCA);
			MAC_CFG_XPCS(0x805E, 0x3635);
			MAC_CFG_XPCS(0x805F, 0xCD);
			MAC_CFG_XPCS(0x8060, 0x31C4);
			MAC_CFG_XPCS(0x8061, 0x4C);
			MAC_CFG_XPCS(0x8062, 0xD6AD);
			MAC_CFG_XPCS(0x8063, 0xB7);
			MAC_CFG_XPCS(0x8064, 0x665F);
			MAC_CFG_XPCS(0x8065, 0x2A);
			MAC_CFG_XPCS(0x8066, 0xF0C0);
			MAC_CFG_XPCS(0x8067, 0xE5);
			break;
		default:
			dev_err(hdev->dev,
				"invalid port %d speed %dMb/s, can't configure MAC XPCS\n",
				port, speed);
			return -EINVAL;
		}
	}

	return 0;
}

static int gaudi_nic_hw_mac_config(struct gaudi_nic_port *gaudi_nic)
{
	struct hl_nic_port *nic_port = gaudi_nic->nic_port;
	u32 port = nic_port->port, speed = nic_port->speed;
	struct hl_device *hdev = gaudi_nic->hdev;
	struct asic_fixed_properties *prop;
	int i, rc;

	prop = &hdev->asic_prop;

	rc = gaudi_nic_config_port_mac_ch(gaudi_nic);
	if (rc) {
		dev_err(hdev->dev, "Failed to configure MAC CHs port %d, %d\n",
			port, rc);
		return rc;
	}

	for (i = 0 ; i < NIC_MAC_LANES ; i++)
		/* Disable FC FEC */
		MAC_CFG_MAC_CORE(0x10, 0);

	switch (speed) {
	case SPEED_10000:
		MAC_CFG_MAC_CORE(0, 0xF0FF00);
		MAC_CFG_MAC_CORE(0x1C, 0);
		MAC_CFG_MAC_CORE(0x10, 0);
		break;
	case SPEED_25000:
		MAC_CFG_MAC_CORE(0, 0xF0FF00);
		MAC_CFG_MAC_CORE(0x18, 0x60F);
		MAC_CFG_MAC_CORE(0x1C, 0);
		MAC_CFG_MAC_CORE(0x10, 0);
		break;
	case SPEED_50000:
		MAC_CFG_MAC_CORE(0x18, 0xFF);
		MAC_CFG_MAC_CORE(0, 0xF0FFF0);
		MAC_CFG_MAC_CORE(0x1C, 0);

		if (prop->fw_app_cpu_boot_dev_sts0 &
				CPU_BOOT_DEV_STS0_FW_NIC_STAT_XPCS91_EN) {
			dev_err(hdev->dev,
				"XPCS91 is blocked, can't configure it directly, port %d\n",
				port);
			return -EINVAL;
		}

		MAC_CFG_XPCS91(0, 0x400);
		MAC_CFG_XPCS91(0x8, 0x400);
		MAC_CFG_XPCS91(0x10, 0x400);
		MAC_CFG_XPCS91(0x18, 0x400);
		break;
	case SPEED_100000:
		MAC_CFG_MAC_CORE(0x18, 0xFF);
		break;
	default:
		dev_err(hdev->dev,
			"invalid port %d speed %dMb/s, can't configure MAC CORE\n",
			port, speed);
		return -EINVAL;
	}

	return 0;
}

static int gaudi_nic_set_pfc(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	bool pfc_enable = nic_port->pfc_enable;
	struct asic_fixed_properties *prop;
	u32 port = nic_port->port, val;
	struct cpucp_packet pkt;
	bool fw_nic_mac_en;
	int rc, i;

	memset(&pkt, 0, sizeof(pkt));
	prop = &hdev->asic_prop;

	fw_nic_mac_en = !!(prop->fw_app_cpu_boot_dev_sts0 &
					CPU_BOOT_DEV_STS0_FW_NIC_MAC_EN);

	if (!fw_nic_mac_en) {
		for (i = 0 ; i < NIC_MAC_LANES ; i++) {
			if (!(nic_port->fw_tuning_mask & BIT(i)))
				continue;

			MAC_CFG_MAC(0x8, pfc_enable ? 0x80813 : 0x2913);
		}

		return 0;
	}

	val = FIELD_PREP(CPUCP_PKT_VAL_PFC_IN1_MASK, pfc_enable);
	val |= FIELD_PREP(CPUCP_PKT_VAL_PFC_IN2_MASK,
				nic_port->fw_tuning_mask);

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_PFC_SET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	/* we used this field as port_index didn't exist yet */
	pkt.index = cpu_to_le32(port);
	pkt.value = cpu_to_le64(val);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, NULL);
	if (rc) {
		dev_err(hdev->dev, "Failed to %s PFC for port %d, %d\n",
			pfc_enable ? "enable" : "disable", port, rc);
		return rc;
	}

	return 0;
}

static int gaudi_nic_set_loopback(struct gaudi_nic_port *gaudi_nic)
{
	struct hl_nic_port *nic_port = gaudi_nic->nic_port;
	bool fw_nic_mac_en, is_lpbk = nic_port->mac_loopback;
	struct hl_device *hdev = gaudi_nic->hdev;
	struct asic_fixed_properties *prop;
	u32 port = nic_port->port, val;
	struct cpucp_packet pkt;
	int rc, i;

	memset(&pkt, 0, sizeof(pkt));
	prop = &hdev->asic_prop;

	fw_nic_mac_en = !!(prop->fw_app_cpu_boot_dev_sts0 &
					CPU_BOOT_DEV_STS0_FW_NIC_MAC_EN);

	if (!fw_nic_mac_en) {
		for (i = 0 ; i < NIC_MAC_LANES ; i++) {
			if (!(nic_port->fw_tuning_mask & BIT(i)))
				continue;

			MAC_CFG_XPCS(0, is_lpbk ? 0xC000 : 0x8000);
		}

		return 0;
	}

	val = FIELD_PREP(CPUCP_PKT_VAL_LPBK_IN1_MASK, is_lpbk);
	val |= FIELD_PREP(CPUCP_PKT_VAL_LPBK_IN2_MASK,
				nic_port->fw_tuning_mask);

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_LPBK_SET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	/* we used this field as port_index didn't exist yet */
	pkt.index = cpu_to_le32(port);
	pkt.value = cpu_to_le64(val);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, NULL);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to %s MAC loopback for port %d, %d\n",
			is_lpbk ? "enable" : "disable", port, rc);
		return rc;
	}

	return 0;
}

static int gaudi_nic_hw_port_config(struct gaudi_nic_port *gaudi_nic)
{
	u64 req_qpc_base_addr, res_qpc_base_addr, txs_addr, swq_base_addr,
		tx_swq_base, mac_addr = 0, rx_msi_addr;
	u32 rx_mem_addr_lo, rx_mem_addr_hi, txs_fence_idx, txs_pi, txs_ci,
		txs_tail, txs_head, txs_timeout_31_0, timeout_47_32, prio,
		txs_port, rl_en_log_time, txs_schedq, port;
	struct gaudi_device *gaudi = gaudi_nic->hdev->asic_specific;
	struct hl_nic_port *nic_port = gaudi_nic->nic_port;
	struct hl_device *hdev = gaudi_nic->hdev;
	struct hl_nic_properties *nic_prop;
	int i, rc;

	nic_prop = &hdev->asic_prop.nic_props;
	port = nic_port->port;

	swq_base_addr = nic_prop->swq_base_addr + port * nic_prop->swq_base_size;
	req_qpc_base_addr = nic_prop->req_qpc_base_addr + port * nic_prop->req_qpc_base_size;
	res_qpc_base_addr = nic_prop->res_qpc_base_addr + port * nic_prop->res_qpc_base_size;

	for (i = 0 ; i < ETH_ALEN ; i++) {
		mac_addr <<= 8;
		mac_addr |= hdev->asic_prop.cpucp_nic_info.mac_addrs[port].mac_addr[i];
	}

	/* ASIC supports only single msi, simulator supports only multi msi */
	rx_msi_addr = hdev->pdev ? mmPCIE_CORE_MSI_REQ : (RX_MSI_ADDRESS + port * 4);

	/* TXS Configuration */
	txs_addr = nic_prop->txs_base_addr + port * nic_prop->txs_base_size;

	/* Timer free list */
	for (i = 0 ; i < TXS_FREE_NUM_ENTRIES ; i++)
		writel(TXS_GRANULARITY + i, hdev->pcie_bar[HBM_BAR_ID] +
			((txs_addr + TXS_FREE_OFFS + i * 4) -
				gaudi->hbm_bar_cur_addr));

	/* Perform read to flush the writes */
	readq(hdev->pcie_bar[HBM_BAR_ID] + nic_prop->nic_drv_base_addr -
		gaudi->hbm_bar_cur_addr);

	NIC_WREG32(mmNIC0_TXS0_BASE_ADDRESS_49_18,
		   (txs_addr + TXS_FIFO_OFFS) >> 18);
	NIC_WREG32(mmNIC0_TXS0_BASE_ADDRESS_17_7,
		   ((txs_addr + TXS_FIFO_OFFS) >> 7) & 0x7FF);
	NIC_WREG32(mmNIC0_TXS0_FREE_LIST_PUSH_MASK_EN, 1);

	txs_fence_idx = 0;
	txs_pi = 0;
	txs_ci = 0;
	txs_tail = 0;
	txs_head = 0;
	txs_timeout_31_0 = 0;
	timeout_47_32 = 0;
	prio = 0;
	txs_port = 0;
	rl_en_log_time = 0;

	for (i = 0 ; i < TXS_SCHEDQ ; i++) {
		prio = i % HL_EN_PFC_PRIO_NUM;
		txs_schedq = (timeout_47_32 & 0xFFFF) | ((prio & 0x3) << 16) |
				((txs_port & 1) << 18) |
				((rl_en_log_time & 0x3F) << 19);
		txs_tail = i;
		txs_head = i;
		NIC_WREG32(mmNIC0_TXS0_SCHEDQ_UPDATE_DESC_31_0, txs_fence_idx);
		NIC_WREG32(mmNIC0_TXS0_SCHEDQ_UPDATE_DESC_63_32, txs_pi);
		NIC_WREG32(mmNIC0_TXS0_SCHEDQ_UPDATE_DESC_95_64, txs_ci);
		NIC_WREG32(mmNIC0_TXS0_SCHEDQ_UPDATE_DESC_127_96, txs_tail);
		NIC_WREG32(mmNIC0_TXS0_SCHEDQ_UPDATE_DESC_159_128, txs_head);
		NIC_WREG32(mmNIC0_TXS0_SCHEDQ_UPDATE_DESC_191_160, txs_timeout_31_0);
		NIC_WREG32(mmNIC0_TXS0_SCHEDQ_UPDATE_DESC_217_192, txs_schedq);
		NIC_WREG32(mmNIC0_TXS0_SCHEDQ_UPDATE_FIFO, i);
		NIC_WREG32(mmNIC0_TXS0_SCHEDQ_UPDATE_EN, 1);
	}

	NIC_WREG32(mmNIC0_TXS0_TICK_WRAP, 100);
	NIC_WREG32(mmNIC0_TXS0_FIRST_SCHEDQ_ID,
		   0 << NIC0_TXS0_FIRST_SCHEDQ_ID_R0_SHIFT |
		   64 << NIC0_TXS0_FIRST_SCHEDQ_ID_R1_SHIFT |
		   128 << NIC0_TXS0_FIRST_SCHEDQ_ID_R2_SHIFT |
		   192 << NIC0_TXS0_FIRST_SCHEDQ_ID_R3_SHIFT);
	NIC_WREG32(mmNIC0_TXS0_LAST_SCHEDQ_ID,
		   63 << NIC0_TXS0_FIRST_SCHEDQ_ID_R0_SHIFT |
		   127 << NIC0_TXS0_FIRST_SCHEDQ_ID_R1_SHIFT |
		   191 << NIC0_TXS0_FIRST_SCHEDQ_ID_R2_SHIFT |
		   155 << NIC0_TXS0_FIRST_SCHEDQ_ID_R3_SHIFT);
	NIC_WREG32(mmNIC0_TXS0_SCAN_TIME_COMPARE_0, 4);
	NIC_WREG32(mmNIC0_TXS0_SCAN_TIME_COMPARE_1, 0);
	NIC_WREG32(mmNIC0_TXS0_TMR_SCAN_EN, 1);

	NIC_WREG32(mmNIC0_TXS0_BASE_ADDRESS_FREE_LIST_49_32,
		   (txs_addr + TXS_FREE_OFFS) >> 32);
	NIC_WREG32(mmNIC0_TXS0_BASE_ADDRESS_FREE_LIST_31_0,
		   (txs_addr + TXS_FREE_OFFS) & 0xFFFFFFFF);

	NIC_WREG32(mmNIC0_TXS0_LIST_MASK,
		   ~(0xFFFFFFFF << (ilog2(TXS_FREE_NUM_ENTRIES) - 5)));
	NIC_WREG32(mmNIC0_TXS0_PRODUCER_UPDATE, TXS_FREE_NUM_ENTRIES);
	NIC_WREG32(mmNIC0_TXS0_PRODUCER_UPDATE_EN, 1);
	NIC_WREG32(mmNIC0_TXS0_PRODUCER_UPDATE_EN, 0);
	NIC_WREG32(mmNIC0_TXS0_LIST_MEM_READ_MASK, 0);
	NIC_WREG32(mmNIC0_TXS0_PUSH_LOCK_EN, 1);

	/* enable burst size optimization */
	NIC_WREG32(mmNIC0_TXS0_IGNORE_BURST_EN, 1);

	NIC_WREG32(mmNIC0_TXS0_FORCE_HIT_EN, 0);

	/* TXE Configuration */

	/* We want to separate the driver WQ from the user WQs.
	 * Since the NIC supports 4 different WQ base addresses, base address 0
	 * will be used by the user and base address 1 by the driver.
	 * The WQ base address index is inferred by two bits that are taken from
	 * QPC.WQ_BASE_ADDR and are configurable by SQ_BASE_ADDRESS_SEL.
	 * Since we support up to NIC_HW_MAX_QP_NUM user QPs and the single
	 * driver QP is located after them, we configure the driver
	 * QPC.WQ_BASE_ADDR to the value NIC_HW_MAX_QP_NUM, and
	 * SQ_BASE_ADDRESS_SEL to have the right shift value so the driver will
	 * indeed use base address 1.
	 */

	/* Need to subtract the size of the user WQs because the driver uses WQ
	 * base address 1.
	 */
	tx_swq_base = swq_base_addr -
			(1 << (WQ_BUFFER_LOG_SIZE - 2)) * NIC_HW_MAX_QP_NUM *
				DEVICE_CACHE_LINE_SIZE;

	NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_49_32_1, (tx_swq_base >> 32) & 0x3FFFFF);
	NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_31_0_1, tx_swq_base & 0xFFFFFFFF);

	NIC_WREG32(mmNIC0_TXE0_PRIO_TO_DSCP_0, 0x18100800);

	/* This register should contain the value of the shift that the H/W will
	 * apply on QPC.WQ_BASE_ADDR in order to get the WQ base address index.
	 * The driver uses WQ base address 1 so we need to trim the leading
	 * zero bits.
	 */
	NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_SEL, ffs(NIC_HW_MAX_QP_NUM) - 1);

	NIC_WREG32(mmNIC0_TXE0_LOG_MAX_WQ_SIZE_1, WQ_BUFFER_LOG_SIZE - 2);
	NIC_WREG32(mmNIC0_TXE0_PORT0_MAC_CFG_47_32, (mac_addr >> 32) & 0xFFFF);
	NIC_WREG32(mmNIC0_TXE0_PORT0_MAC_CFG_31_0, mac_addr & 0xFFFFFFFF);
	NIC_WREG32(mmNIC0_TXE0_PORT1_MAC_CFG_47_32, (mac_addr >> 32) & 0xFFFF);
	NIC_WREG32(mmNIC0_TXE0_PORT1_MAC_CFG_31_0, mac_addr & 0xFFFFFFFF);

	/* Since the user WQs are mapped via MMU by the user, its AXI_USER
	 * registers are set without MMU bypass and with the user ASID.
	 * Because these configuration registers are shared between the user WQs
	 * and the ETH Tx WQ, the latter can't be mapped via MMU as we need to
	 * configure the LKD ASID for that.
	 * In addition, the ETH Tx WQ is secured so the user shouldn't be able
	 * to access it. Hence we place the ETH Tx WQ on HBM in the LKD reserved
	 * section.
	 */
	NIC_WREG32(mmNIC0_TXE0_WQE_FETCH_AXI_USER, 1);

	/* The Tx data is placed on HBM. Hence configure it without MMU bypass
	 * and with the user ASID to avoid any successful access to the host
	 */
	NIC_WREG32(mmNIC0_TXE0_DATA_FETCH_AXI_USER, 1);
	NIC_WREG32(mmNIC0_TXE0_INTERRUPT_MASK, 3);

	/* Make sure data fetch can never be privileged */
	NIC_WREG32(mmNIC0_TXE0_DATA_FETCH_AXI_PROT, 0x80);
	/* Make sure WQE fetch can never be privileged */
	NIC_WREG32(mmNIC0_TXE0_WQE_FETCH_AXI_PROT, 0x80);

	NIC_WREG32(mmNIC0_TXE0_BTH_MKEY, 0xffff);

	/* QPC Configuration */
	NIC_WREG32(mmNIC0_QPC0_REQ_BASE_ADDRESS_49_18,
		   (req_qpc_base_addr >> 18) & 0xFFFFFFFF);
	NIC_WREG32(mmNIC0_QPC0_REQ_BASE_ADDRESS_17_7,
		   (req_qpc_base_addr >> 7) & 0x7FF);
	NIC_WREG32(mmNIC0_QPC0_RES_BASE_ADDRESS_49_18,
		   (res_qpc_base_addr >> 18) & 0xFFFFFFFF);
	NIC_WREG32(mmNIC0_QPC0_RES_BASE_ADDRESS_17_7,
		   (res_qpc_base_addr >> 7) & 0x7FF);
	NIC_WREG32(mmNIC0_QPC0_RES_QPC_CACHE_INVALIDATE, 1);
	NIC_WREG32(mmNIC0_QPC0_REQ_QPC_CACHE_INVALIDATE, 1);
	NIC_WREG32(mmNIC0_QPC0_RES_QPC_CACHE_INVALIDATE, 0);
	NIC_WREG32(mmNIC0_QPC0_REQ_QPC_CACHE_INVALIDATE, 0);
	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_BASE_4, rx_msi_addr);
	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_DATA_4, 1);
	NIC_WREG32(mmNIC0_QPC0_RES_RING0_CFG, RAW_QPN);
	/* Interrupt each packet */
	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_CFG, 0x1FF);
	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_CAUSE, 0);

	/* Enable the QP error interrupt. The Rx interrupt will be enabled later by the Ethernet
	 * driver if needed. Other interrupts are unused.
	 */
	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_MASK, 0x100);

	/* Generate wire interrupt in case of a QP error. CPU-CP converts it to event. */
	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_EN, 1 << NIC0_QPC0_INTERRUPT_EN_INTERRUPT8_WIRE_EN_SHIFT);

	/* Due to H/W bug, odd ports cannot generate Ethernet MSI interrupts.
	 * Hence they generate wire interrupts and the CPU-CP converts them to MSI interrupts.
	 */
	if (nic_port->eth_enable) {
		if (port & 1)
			NIC_RMWREG32(mmNIC0_QPC0_INTERRUPT_EN, 1,
				     NIC0_QPC0_INTERRUPT_EN_INTERRUPT4_WIRE_EN_MASK);
		else
			NIC_RMWREG32(mmNIC0_QPC0_INTERRUPT_EN, 1,
				     NIC0_QPC0_INTERRUPT_EN_INTERRUPT4_MSI_EN_MASK);
	}

	NIC_WREG32(mmNIC0_QPC0_AXI_PROT, 0); /* secured */

	NIC_WREG32(mmNIC0_QPC0_ERR_FIFO_BASE_ADDR_49_18,
		   (gaudi_nic->qp_err_mem.dma_addr >> 18) & 0xFFFFFFFF);
	NIC_WREG32(mmNIC0_QPC0_ERR_FIFO_BASE_ADDR_17_7,
		   gaudi_nic->qp_err_mem.dma_addr & 0x3FF80);
	NIC_WREG32(mmNIC0_QPC0_ERR_FIFO_PRODUCER_INDEX, 0);
	NIC_WREG32(mmNIC0_QPC0_ERR_FIFO_CONSUMER_INDEX, 0);
	NIC_WREG32(mmNIC0_QPC0_ERR_FIFO_WRITE_INDEX, 0);
	NIC_WREG32(mmNIC0_QPC0_ERR_FIFO_MASK, QP_ERR_BUF_LEN - 1);
	NIC_RMWREG32(mmNIC0_QPC0_ERR_FIFO_CFG, 1,
		     NIC0_QPC0_ERR_FIFO_CFG_WRAPAROUND_EN_MASK);
	/* The error FIFO is unmapped, hence the bypass */
	NIC_WREG32(mmNIC0_QPC0_AXI_USER, 0x400);
	NIC_WREG32(mmNIC0_QPC0_RETRY_COUNT_MAX, 0xFFFF);

	NIC_RMWREG32(mmNIC0_QPC0_REQ_STATIC_CONFIG, 1,
		     NIC0_QPC0_REQ_STATIC_CONFIG_INVALIDATE_WRITEBACK_MASK);
	NIC_RMWREG32(mmNIC0_QPC0_RES_STATIC_CONFIG, 1,
		     NIC0_QPC0_RES_STATIC_CONFIG_INVALIDATE_WRITEBACK_MASK);

	/* RXE Configuration */
	rx_mem_addr_lo = lower_32_bits(gaudi_nic->rx_mem.dma_addr);
	/* discard packets above the max size */
	rx_mem_addr_hi = (upper_32_bits(gaudi_nic->rx_mem.dma_addr) <<
			NIC0_RXE0_RAW_BASE_HI_P1_RAW_BASE_ADDR_HI_P1_SHIFT) |
			(ilog2(NIC_RAW_ELEM_SIZE) <<
			NIC0_RXE0_RAW_BASE_HI_P1_LOG_RAW_ENTRY_SIZE_P1_SHIFT);

	NIC_WREG32(mmNIC0_RXE0_ARUSER_HBW_10_0, 1);
	NIC_WREG32(mmNIC0_RXE0_ARUSER_HBW_31_11, 0);

	/* Make sure LBW write access (for SM) can never be privileged */
	NIC_WREG32(mmNIC0_RXE0_AWPROT_LBW, 0x2);

	/* Make sure HBW read access (for WQE) is always unsecured */
	NIC_WREG32(mmNIC0_RXE0_ARPROT_HBW, 0x222);

	NIC_WREG32(mmNIC0_RXE0_RAW_QPN_P0_0, RAW_QPN);
	NIC_WREG32(mmNIC0_RXE0_RAW_QPN_P2_0, RAW_QPN);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_LO_P0_0, rx_mem_addr_lo);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_HI_P0_0, rx_mem_addr_hi);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_LO_P2_0, rx_mem_addr_lo);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_HI_P2_0, rx_mem_addr_hi);

	/* See the comment for mmNIC0_TXE0_SQ_BASE_ADDRESS_SEL. The same applies
	 * for the Rx.
	 */
	NIC_WREG32(mmNIC0_RXE0_WQ_BASE_WINDOW_SEL, ffs(NIC_HW_MAX_QP_NUM) - 1);

	NIC_WREG32(mmNIC0_RXE0_PKT_DROP,
		   (0 << NIC0_RXE0_PKT_DROP_ERR_QP_INVALID_SHIFT) |
		   (1 << NIC0_RXE0_PKT_DROP_ERR_TS_MISMATCH_SHIFT) |
		   (0 << NIC0_RXE0_PKT_DROP_ERR_CS_INVALID_SHIFT) |
		   (0 << NIC0_RXE0_PKT_DROP_ERR_REQ_PSN_INVALID_SHIFT) |
		   (1 << NIC0_RXE0_PKT_DROP_ERR_RES_RKEY_INVALID_SHIFT) |
		   (0 << NIC0_RXE0_PKT_DROP_ERR_RES_RESYNC_INVALID_SHIFT) |
		   /* H/W WA for check priority order */
		   (0 << NIC0_RXE0_PKT_DROP_ERR_INV_OPCODE_SHIFT) |
		   (0 << NIC0_RXE0_PKT_DROP_ERR_INV_SYNDROME_SHIFT) |
		   (0 << NIC0_RXE0_PKT_DROP_ERR_INV_RAW_SIZE_SHIFT));

	/* MAC filtering */
	if (nic_port->eth_enable) {
		if (port & 1) {
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_31_0_2,
					 mac_addr & 0xFFFFFFFF);
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_31_0_3,
					 mac_addr & 0xFFFFFFFF);

			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_47_32_2,
					 (mac_addr >> 32) & 0xFFFF);
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_47_32_3,
					 (mac_addr >> 32) & 0xFFFF);

			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_31_0_MASK_2, 0);
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_31_0_MASK_3, 0);

			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_47_32_MASK_2, 0);
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_47_32_MASK_3, 0);
		} else {
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_31_0_0,
					 mac_addr & 0xFFFFFFFF);
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_31_0_1,
					 mac_addr & 0xFFFFFFFF);

			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_47_32_0,
					 (mac_addr >> 32) & 0xFFFF);
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_47_32_1,
					 (mac_addr >> 32) & 0xFFFF);

			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_31_0_MASK_0, 0);
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_31_0_MASK_1, 0);

			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_47_32_MASK_0, 0);
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_47_32_MASK_1, 0);
		}
	} else {
		if (port & 1) {
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_31_0_MASK_2, 0xFFFFFFFF);
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_31_0_MASK_3, 0xFFFFFFFF);

			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_47_32_MASK_2, 0xFFFF);
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_47_32_MASK_3, 0xFFFF);
		} else {
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_31_0_MASK_0, 0xFFFFFFFF);
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_31_0_MASK_1, 0xFFFFFFFF);

			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_47_32_MASK_0, 0xFFFF);
			NIC_MACRO_WREG32(mmNIC0_RXB_TS_RC_MAC_47_32_MASK_1, 0xFFFF);
		}
	}

	rc = gaudi_nic_set_loopback(gaudi_nic);
	if (rc)
		return rc;

	rc = gaudi_nic_set_pfc(nic_port);
	if (rc)
		return rc;

	return 0;
}

static void gaudi_nic_hw_macro_config(struct gaudi_nic_port *gaudi_nic)
{
	struct gaudi_device *gaudi = gaudi_nic->hdev->asic_specific;
	int i, drop = 0xAB4, xoff = drop - 0x400, xon = xoff - 0x10,
		credits = 0x41;
	struct hl_nic_port *nic_port = gaudi_nic->nic_port;
	struct hl_device *hdev = gaudi_nic->hdev;
	struct hl_nic_properties *nic_prop;
	u32 port = nic_port->port;
	u64 tmr_addr;

	nic_prop = &hdev->asic_prop.nic_props;

	/* TXB Configuration */
	/* Set iCRC calculation & generation with reversed bytes */
	NIC_MACRO_WREG32(mmNIC0_TXB_ICRC_CFG, 0x2);

	/* RXB Configuration */
	NIC_MACRO_WREG32(mmNIC0_RXB_LBW_OFFSET_0, CFG_BASE & 0xFFFFFFFF);
	NIC_MACRO_WREG32(mmNIC0_RXB_LBW_OFFSET_1, (CFG_BASE >> 32) & 0x3FFFF);
	/* Set iCRC calculation & verification with reversed bytes */
	NIC_MACRO_WREG32(mmNIC0_RXB_ICRC_CFG, 0x2);
	/* Don't filter any incoming raw packets in HW, they will be filtered in SW */
	NIC_MACRO_WREG32(mmNIC0_RXB_TS_RAW0_MAC_31_0_MASK_0, 0xFFFFFFFF);
	NIC_MACRO_WREG32(mmNIC0_RXB_TS_RAW0_MAC_47_32_MASK_0, 0xFFFF);
	NIC_MACRO_WREG32(mmNIC0_RXB_TS_RAW0_MAC_31_0_MASK_2, 0xFFFFFFFF);
	NIC_MACRO_WREG32(mmNIC0_RXB_TS_RAW0_MAC_47_32_MASK_2, 0xFFFF);

	/* H/W WA for credit leakage */
	for (i = 0 ; i < 16 ; i++)
		NIC_MACRO_WREG32(mmNIC0_RXB_DROP_THRESHOLD_0 + i * 4,
				 drop | (drop << 13));

	NIC_MACRO_WREG32(mmNIC0_RXB_AXI_AXUSER_10_0_UNTRUST, 1);
	NIC_MACRO_WREG32(mmNIC0_RXB_AXI_AXUSER_10_0_TRUST, 0x400);
	NIC_MACRO_WREG32(mmNIC0_RXB_AXI_AXUSER_10_0_PRIV, 0x400);
	NIC_MACRO_WREG32(mmNIC0_RXB_AXI_AXPROT_PRIV, 0);
	NIC_MACRO_WREG32(mmNIC0_RXB_AXI_AXPROT_TRUST, 0);
	NIC_MACRO_WREG32(mmNIC0_RXB_AXI_AXPROT_UNTRUST, 2);

	/* Credits allocation - all dynamic */
	/* H/W WA for credit leakage */
	NIC_MACRO_WREG32(mmNIC0_RXB_MAX_DYNAMIC, 0xB36);

	for (i = 0 ; i < 16 ; i++)
		NIC_MACRO_WREG32(mmNIC0_RXB_MAX_STATIC_CREDITS_0 + i * 4,
				 credits | (credits << 13));

	NIC_MACRO_WREG32(mmNIC0_RXB_PORT_TRUST_LEVEL, 0);

	for (i = 0 ; i < 16 ; i++)
		NIC_MACRO_WREG32(mmNIC0_RXB_XOFF_THRESHOLD_0 + i * 4,
				 xoff | (xoff << 13));

	for (i = 0 ; i < 16 ; i++)
		NIC_MACRO_WREG32(mmNIC0_RXB_XON_THRESHOLD_0 + i * 4,
				 xon | (xon << 13));

	/* TMR Configuration */
	tmr_addr = nic_prop->tmr_base_addr + nic_port->nic_macro->idx * nic_prop->tmr_base_size;

	/* Clear timer FSM0 */
	for (i = 0 ; i < NIC_HW_MAX_QP_NUM ; i++)
		writeb(0, hdev->pcie_bar[HBM_BAR_ID] +
			((tmr_addr + TMR_FSM0_OFFS + i) -
				gaudi->hbm_bar_cur_addr));

	/* Clear timer FSM1 */
	for (i = 0 ; i < NIC_HW_MAX_QP_NUM ; i++)
		writeb(0, hdev->pcie_bar[HBM_BAR_ID] +
			((tmr_addr + TMR_FSM1_OFFS + i) -
				gaudi->hbm_bar_cur_addr));

	/* Timer free list */
	for (i = 0 ; i < TMR_FREE_NUM_ENTRIES ; i++)
		writel(TMR_GRANULARITY + i, hdev->pcie_bar[HBM_BAR_ID] +
			((tmr_addr + TMR_FREE_OFFS + i * 4) -
				gaudi->hbm_bar_cur_addr));

	/* Perform read to flush the writes */
	readq(hdev->pcie_bar[HBM_BAR_ID] + nic_prop->nic_drv_base_addr -
		gaudi->hbm_bar_cur_addr);

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_BASE_ADDRESS_49_18,
			 (tmr_addr + TMR_FIFO_OFFS) >> 18);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_BASE_ADDRESS_17_7,
			 ((tmr_addr + TMR_FIFO_OFFS) >> 7) & 0x7FF);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_BASE_ADDRESS_FREE_LIST_49_32,
			 (tmr_addr + TMR_FREE_OFFS) >> 32);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_BASE_ADDRESS_FREE_LIST_31_0,
			 (tmr_addr + TMR_FREE_OFFS) & 0xFFFFFFFF);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_CACHE_BASE_ADDR_49_32,
			 (tmr_addr + TMR_FSM0_OFFS) >> 32);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_CACHE_BASE_ADDR_31_7,
			 ((tmr_addr + TMR_FSM0_OFFS) >> 7) & 0xFFFFFF);

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_31_0, 0);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_63_32, 0);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_95_64, 0);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_191_160, 1000000);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_216_192, 0);

	for (i = 0 ; i < TMR_GRANULARITY ; i++) {
		NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_127_96, i);
		NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_159_128, i);
		NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_FIFO, i);
		NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_EN, 1);
	}

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCAN_TIMER_COMP_31_0, 10);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_TICK_WRAP, 500);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_LIST_MASK,
			 ~(0xFFFFFFFF << (ilog2(TMR_FREE_NUM_ENTRIES) - 5)));
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_PRODUCER_UPDATE, TMR_FREE_NUM_ENTRIES);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_PRODUCER_UPDATE_EN, 1);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_PRODUCER_UPDATE_EN, 0);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_LIST_MEM_READ_MASK, 0);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_PUSH_LOCK_EN, 1);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_TIMER_EN, 1);
	NIC_MACRO_WREG32(mmNIC0_TMR_FREE_LIST_PUSH_MASK_EN, 0);

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_FORCE_HIT_EN, 0);
}

static int gaudi_nic_hw_config(struct gaudi_nic_port *gaudi_nic)
{
	struct gaudi_nic_macro *gaudi_macro = gaudi_nic->nic_port->nic_macro->asic_priv;
	struct hl_device *hdev = gaudi_nic->hdev;
	u32 port = gaudi_nic->nic_port->port;
	int rc;

	rc = gaudi_nic_hw_port_config(gaudi_nic);
	if (rc) {
		dev_err(hdev->dev, "Failed to configure port %d HW, %d\n", port,
			rc);
		return rc;
	}

	/* only one port should configure the NIC MACRO */
	mutex_lock(&gaudi_macro->macro_cfg_lock);

	if (!gaudi_macro->active_ports) {
		rc = gaudi_nic_hw_mac_config(gaudi_nic);
		if (rc) {
			dev_err(hdev->dev,
				"Failed to configure port %d MAC, %d\n", port,
				rc);
			mutex_unlock(&gaudi_macro->macro_cfg_lock);
			return rc;
		}

		gaudi_nic_hw_macro_config(gaudi_nic);
	}

	gaudi_macro->active_ports++;

	/* Perform read from the device to flush all configurations */
	NIC_MACRO_RREG32(mmNIC0_TMR_TMR_TIMER_EN);

	mutex_unlock(&gaudi_macro->macro_cfg_lock);

	return 0;
}

static int gaudi_nic_port_hw_init(struct hl_nic_port *nic_port)
{
	struct gaudi_nic_port *gaudi_nic = nic_port->nic_specific;
	struct hl_device *hdev = gaudi_nic->hdev;
	u32 port = nic_port->port;
	int rc;

	gaudi_nic_mac_ch_init(nic_port);

	rc = gaudi_nic_hw_config(gaudi_nic);
	if (rc) {
		dev_err(hdev->dev, "Failed to configure the HW, port: %d, %d", port, rc);
		return rc;
	}

	return 0;
}

static void gaudi_nic_port_hw_fini(struct hl_nic_port *nic_port)
{
	struct gaudi_nic_macro *gaudi_macro = nic_port->nic_macro->asic_priv;
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	/* decrement the active_ports counter of this NIC macro */
	mutex_lock(&gaudi_macro->macro_cfg_lock);
	gaudi_macro->active_ports--;
	mutex_unlock(&gaudi_macro->macro_cfg_lock);

	/* Due to H/W bug, odd ports cannot generate Ethernet MSI interrupts.
	 * Hence they generate wire interrupts and the CPU-CP converts them to MSI interrupts. In
	 * order to avoid CPU-CP from generating MSI interrupts after the odd port went down, clear
	 * here the interrupt enable bit.
	 */
	if (nic_port->eth_enable && (port & 1))
		NIC_RMWREG32(mmNIC0_QPC0_INTERRUPT_EN, 0,
				NIC0_QPC0_INTERRUPT_EN_INTERRUPT4_WIRE_EN_MASK);
}

static int gaudi_user_wq_arr_set(struct hl_device *hdev,
				struct hl_nic_user_wq_arr_set_in *in,
				struct hl_nic_user_wq_arr_set_out *out,
				struct hl_ctx *ctx)
{
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_nic_port *nic_port;
	u64 wq_base_addr, num_of_wq_entries, num_of_wq_entries_log, wq_arr_size, num_of_wqs;
	u32 wqe_size, port, type, alignment_size;

	port = in->port;
	type = in->type;
	nic_port = &hdev->nic.nic_ports[port];

	if (in->addr) {
		dev_dbg(hdev->dev, "WQ array address shouldn't be set: 0x%llx\n", in->addr);
		return -EINVAL;
	}

	wq_arr_props = &nic_port->wq_arr_props[type];
	num_of_wqs = in->num_of_wqs;

	/* Reserve device virtual address block for the WQ array - align the size of each
	 * WQ to be a multiple of page size, to prevent a situation that two different WQs
	 * share the same MMU entry.
	 */
	wqe_size = wq_arr_props->is_send ? NIC_SEND_WQE_SIZE : NIC_RECV_WQE_SIZE;

	if (wq_arr_props->is_send)
		nic_port->swqe_size = wqe_size;

	alignment_size = PAGE_SIZE / min(NIC_SEND_WQE_SIZE, NIC_RECV_WQE_SIZE);
	num_of_wq_entries = ALIGN(in->num_of_wq_entries, alignment_size);
	wq_arr_size = num_of_wqs * num_of_wq_entries * wqe_size;

	wq_base_addr = hl_nic_reserve_wq_dva(hdev, ctx, nic_port, wq_arr_size, type);
	if (!wq_base_addr)
		return -ENOMEM;

	num_of_wq_entries_log = ilog2(num_of_wq_entries);

	/* num_of_wq_entries_log subtracted by 2/4 for tx/rx respectively for cache line unit
	 * conversion, e.g.: ilog2(NIC_SEND_WQE_SIZE / DEVICE_CACHE_LINE_SIZE) = -2
	 */
	if (wq_arr_props->is_send) {
		NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_49_32_0, (wq_base_addr >> 32) & 0x3FFFFF);
		NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_31_0_0, wq_base_addr & 0xFFFFFFFF);
		NIC_WREG32(mmNIC0_TXE0_LOG_MAX_WQ_SIZE_0, num_of_wq_entries_log - 2);
		wq_arr_props->wq_size = num_of_wq_entries * NIC_SEND_WQE_SIZE;
	} else {
		NIC_WREG32(mmNIC0_RXE0_WIN0_WQ_BASE_LO, wq_base_addr & 0xFFFFFFFF);
		NIC_WREG32(mmNIC0_RXE0_WIN0_WQ_BASE_HI,
			((wq_base_addr >> 32) & 0xFFFFFFFF) | ((num_of_wq_entries_log - 4) << 24));
		wq_arr_props->wq_size = num_of_wq_entries * NIC_RECV_WQE_SIZE;
	}

	return 0;
}

static int gaudi_user_wq_arr_unset(struct hl_nic_port *nic_port, u32 type, struct hl_ctx *ctx)
{
	struct hl_wq_array_properties *wq_arr_props = &nic_port->wq_arr_props[type];
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	int rc = 0;

	if (wq_arr_props->is_send) {
		NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_49_32_0, 0);
		NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_31_0_0, 0);
		NIC_WREG32(mmNIC0_TXE0_LOG_MAX_WQ_SIZE_0, 0);
	} else {
		NIC_WREG32(mmNIC0_RXE0_WIN0_WQ_BASE_LO, 0);
		NIC_WREG32(mmNIC0_RXE0_WIN0_WQ_BASE_HI, 0);
	}

	if (wq_arr_props->dva_base)
		rc = hl_nic_unreserve_wq_dva(hdev, ctx, nic_port, type);

	return rc;
}

static int gaudi_user_set_app_params(struct hl_device *hdev,
					struct hl_nic_set_user_app_params_in *in,
					bool *modify_wqe_checkers, struct hl_ctx *ctx)
{
	struct gaudi_nic_port *gaudi_nic;
	struct hl_nic_port *nic_port;
	u32 port = in->port;

	nic_port = &hdev->nic.nic_ports[port];

	/* set_app_params can be either true or SET_APP_PARAM_MASK. We want to allow
	 * a user on Gaudi1, call explicitly for user_set_app_params. While it is not mandatory
	 * for Gaudi1. Hence we added a special value for it.
	 */
	if (nic_port->set_app_params && (nic_port->set_app_params != SET_APP_PARAM_MASK)) {
		dev_dbg(hdev->dev, "App params were already set, port %d\n", port);
		return -EPERM;
	}

	gaudi_nic = nic_port->nic_specific;

	gaudi_nic->advanced = in->advanced;

	*modify_wqe_checkers = false;

	return 0;
}

static void gaudi_user_get_app_params(struct hl_device *hdev,
					struct hl_nic_get_user_app_params_in *in,
					struct hl_nic_get_user_app_params_out *out)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct hl_nic_port *nic_port;
	struct gaudi_nic_port *gaudi_nic;
	u32 port;

	port = in->port;
	gaudi_nic = &gaudi->nic_ports[port];
	nic_port = gaudi_nic->nic_port;

	out->max_num_of_qps = NIC_MAX_QP_NUM;
	/* indicates how many QPs are already taken */
	out->num_allocated_qps = RDMA_OFFSET + atomic_read(&nic_port->num_of_allocated_qps);
	out->max_allocated_qp_idx = hl_nic_get_max_qp_id(nic_port);
	out->max_cq_size = sizeof(struct cqe) * USER_CQ_MAX_ENTRIES;
	out->max_num_of_cqs = 1;
}

static int get_src_ip(struct hl_nic_port *nic_port, u32 *src_ip)
{
	struct hl_aux_dev *aux_dev = &nic_port->hdev->nic.en_aux_dev;
	struct hl_en_aux_ops *aux_ops = aux_dev->aux_ops;
	u32 port = nic_port->port;

	if (!nic_port->eth_enable) {
		*src_ip = 0;
		return 0;
	}

	return aux_ops->get_src_ip(aux_dev, port, src_ip);
}

static int gaudi_set_req_qp_ctx(struct hl_device *hdev,
				struct hl_nic_req_conn_ctx_in *in,
				struct hl_qp *qp)
{
	struct hl_nic_port *nic_port;
	struct gaudi_nic_port *gaudi_nic;
	struct gaudi_qpc_requester req_qpc = {};
	u8 mac[ETH_ALEN];
	u32 src_ip;
	int rc;

	nic_port = &hdev->nic.nic_ports[in->port];
	gaudi_nic = nic_port->nic_specific;

	rc = get_src_ip(nic_port, &src_ip);
	if (rc) {
		dev_dbg(hdev->dev, "Failed to get src IP for port %d qpn %d\n", in->port,
			in->conn_id);
		return rc;
	}

	if (nic_port->eth_enable)
		memcpy(mac, in->dst_mac_addr, ETH_ALEN);
	else
		/* in this case the MAC is irrelevant so use broadcast */
		eth_broadcast_addr(mac);

	REQ_QPC_SET_DST_QP(req_qpc, in->dst_conn_id);
	REQ_QPC_SET_PORT(req_qpc, 0);
	REQ_QPC_SET_PRIORITY(req_qpc, in->priority);
	REQ_QPC_SET_RKEY(req_qpc, qp->remote_key);
	REQ_QPC_SET_DST_IP(req_qpc, in->dst_ip_addr);
	REQ_QPC_SET_SRC_IP(req_qpc, src_ip);
	REQ_QPC_SET_DST_MAC_31_0(req_qpc, *(u32 *) mac);
	REQ_QPC_SET_DST_MAC_47_32(req_qpc, *(u16 *) (mac + 4));
	REQ_QPC_SET_SQ_NUM(req_qpc, QPC_REQ_SCHED_Q);
	REQ_QPC_SET_TM_GRANULARITY(req_qpc, in->timer_granularity);
	REQ_QPC_SET_SOB_EN(req_qpc, (gaudi_nic->advanced ? 1 : 0));
	REQ_QPC_SET_TRANSPORT_SERVICE(req_qpc, TS_RC);
	REQ_QPC_SET_BURST_SIZE(req_qpc, QPC_REQ_BURST_SIZE);
	REQ_QPC_SET_LAST_IDX(req_qpc, in->last_index);
	REQ_QPC_SET_WQ_BASE_ADDR(req_qpc, in->conn_id);
	REQ_QPC_SET_SWQ_GRANULARITY(req_qpc, in->swq_granularity);
	REQ_QPC_SET_VALID(req_qpc, 1);

	return gaudi_nic_qpc_write(nic_port, &req_qpc.data, NULL, in->conn_id, true);
}

static int gaudi_set_res_qp_ctx(struct hl_device *hdev,
				struct hl_nic_res_conn_ctx_in *in,
				struct hl_qp *qp)
{
	struct hl_nic_port *nic_port;
	struct gaudi_nic_port *gaudi_nic;
	struct gaudi_qpc_responder res_qpc = {};
	u8 mac[ETH_ALEN];
	u32 src_ip;
	int rc;

	nic_port = &hdev->nic.nic_ports[in->port];
	gaudi_nic = nic_port->nic_specific;

	rc = get_src_ip(nic_port, &src_ip);
	if (rc) {
		dev_dbg(hdev->dev, "Failed to get src IP for port %d qpn %d\n", in->port,
			in->conn_id);
		return rc;
	}

	if (nic_port->eth_enable)
		memcpy(mac, in->dst_mac_addr, ETH_ALEN);
	else
		/* in this case the MAC is irrelevant so use broadcast */
		eth_broadcast_addr(mac);

	RES_QPC_SET_DST_QP(res_qpc, in->dst_conn_id);
	RES_QPC_SET_PORT(res_qpc, 0);
	RES_QPC_SET_PRIORITY(res_qpc, in->priority);
	RES_QPC_SET_SQ_NUM(res_qpc, QPC_RES_SCHED_Q);
	RES_QPC_SET_LKEY(res_qpc, qp->local_key);
	RES_QPC_SET_DST_IP(res_qpc, in->dst_ip_addr);
	RES_QPC_SET_SRC_IP(res_qpc, src_ip);
	RES_QPC_SET_DST_MAC_31_0(res_qpc, *(u32 *) mac);
	RES_QPC_SET_DST_MAC_47_32(res_qpc, *(u16 *) (mac + 4));
	RES_QPC_SET_TRANSPORT_SERVICE(res_qpc, TS_RC);
	RES_QPC_SET_LOG_BUF_SIZE_MASK(res_qpc, 0);
	RES_QPC_SET_SOB_EN(res_qpc, (gaudi_nic->advanced ? 1 : 0));
	RES_QPC_SET_VALID(res_qpc, 1);

	return gaudi_nic_qpc_write(nic_port, &res_qpc.data, NULL, in->conn_id, false);
}

static int gaudi_nic_update_qp_mtu(struct hl_nic_port *nic_port, struct hl_qp *qp, u32 mtu)
{
	return 0;
}

static void get_syndrome_text(char *str, u32 len, u32 syndrome)
{
	switch (syndrome) {
	case 0x05:
		strscpy(str, "Rx got invalid QP", len);
		break;
	case 0x06:
		strscpy(str, "Rx transport service mismatch", len);
		break;
	case 0x09:
		strscpy(str, "Rx Rkey check failed", len);
		break;
	case 0x40:
		strscpy(str, "timer retry exceeded", len);
		break;
	case 0x41:
		strscpy(str, "NACK retry exceeded", len);
		break;
	case 0x42:
		strscpy(str, "doorbell on invalid QP", len);
		break;
	case 0x43:
		strscpy(str, "doorbell security check failed", len);
		break;
	case 0x44:
		strscpy(str, "Tx got invalid QP", len);
		break;
	case 0x45:
		strscpy(str, "responder try to send ACK/NACK on invalid QP",
			len);
		break;
	case 0x46:
		strscpy(str, "responder got ACK/NACK on invalid QP",
			len);
		break;
	default:
		snprintf(str, len, "unknown syndrome 0x%x", syndrome);
		break;
	}
}

void gaudi_nic_handle_qp_err(struct hl_device *hdev, u16 event_type)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_nic_port *gaudi_nic;
	struct hl_nic_port *nic_port;
	struct qp_err *qp_err_arr;
	struct hl_nic_cqe cqe_sw;
	char syndrome_str[64];
	u32 pi, ci, port;

	gaudi_nic = &gaudi->nic_ports[event_type - GAUDI_EVENT_NIC0_QP0];
	qp_err_arr = gaudi_nic->qp_err_mem.addr;
	nic_port = gaudi_nic->nic_port;
	port = nic_port->port;

	memset(&cqe_sw, 0, sizeof(cqe_sw));

	pi = NIC_RREG32(mmNIC0_QPC0_ERR_FIFO_PRODUCER_INDEX) & (QP_ERR_BUF_LEN - 1);
	ci = gaudi_nic->qp_err_ci;

	if (pi == ci)
		return;

	cqe_sw.is_err = true;
	cqe_sw.port = port;

	while (ci != pi) {
		cqe_sw.type = QP_ERR_IS_REQ(qp_err_arr[ci]) ? HL_NIC_CQE_TYPE_REQ :
										HL_NIC_CQE_TYPE_RES;
		cqe_sw.qp_number = QP_ERR_QP_NUM(qp_err_arr[ci]);
		cqe_sw.qp_err.syndrome = QP_ERR_ERR_NUM(qp_err_arr[ci]);

		ci = (ci + 1) & (QP_ERR_BUF_LEN - 1);

		memset(syndrome_str, 0, sizeof(syndrome_str));
		get_syndrome_text(syndrome_str, sizeof(syndrome_str), cqe_sw.qp_err.syndrome);

		dev_err_ratelimited(hdev->dev,
					"NIC QP error port: %d, type: %d, qpn: %d, syndrome: %s\n",
					cqe_sw.port, cqe_sw.type, cqe_sw.qp_number, syndrome_str);

	}

	gaudi_nic->qp_err_ci = ci;
	NIC_WREG32(mmNIC0_QPC0_ERR_FIFO_CONSUMER_INDEX, ci);
}

static bool gaudi_nic_get_hw_cap(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	return (gaudi->hw_cap_initialized & HW_CAP_NIC_DRV);
}

static void gaudi_nic_set_hw_cap(struct hl_device *hdev, bool enable)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (enable)
		gaudi->hw_cap_initialized |= HW_CAP_NIC_DRV;
	else
		gaudi->hw_cap_initialized &= ~HW_CAP_NIC_DRV;
}

static int gaudi_nic_kernel_ctx_init(struct hl_ctx *ctx)
{
	if (!ctx->hdev->mmu_enable) {
		ctx->hdev->nic_ports_mask = 0;
		return 0;
	}

	return 0;
}

static void gaudi_nic_kernel_ctx_fini(struct hl_ctx *ctx)
{
}

static int gaudi_nic_ctx_init(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_nic_port *nic_port;
	int port;

	for (port = 0 ; port < hdev->asic_prop.nic_props.max_num_of_ports ; port++) {
		nic_port = &hdev->nic.nic_ports[port];

		/* set_app_params operation support was added for Gaudi1, but
		 * as Gaudi considered legacy, communication library, which relies on
		 * that set_app_params was called. We should keep it that way but meanwhile
		 * allow explicit call to set_app_params ioctl. Hence, use a special value for it.
		 */
		nic_port->set_app_params = SET_APP_PARAM_MASK;
	}

	return 0;
}

static void gaudi_nic_ctx_fini(struct hl_ctx *ctx)
{

}

static int gaudi_nic_pre_core_init(struct hl_device *hdev)
{
	struct cpucp_nic_info *nic_info = &hdev->asic_prop.cpucp_nic_info;
	struct hl_nic_properties *nic_prop = &hdev->asic_prop.nic_props;
	struct cpucp_info *cpucp_info = &hdev->asic_prop.cpucp_info;
	struct cpucp_mac_addr *mac_arr = nic_info->mac_addrs;
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct hl_nic *nic = &hdev->nic;
	u64 nic_dram_alloc_size;
	u32 card_location;
	u8 mac[ETH_ALEN];
	int i, rc;

	nic_dram_alloc_size = nic_prop->nic_drv_end_addr - nic_prop->nic_drv_base_addr;
	if (nic_dram_alloc_size > nic_prop->nic_drv_size) {
		dev_err(hdev->dev, "DRAM allocation for NIC (%lluMB) shouldn't exceed %lluMB\n",
			div_u64(nic_dram_alloc_size, SZ_1M),
			div_u64(nic_prop->nic_drv_size, SZ_1M));
		return -ENOMEM;
	}

	if (TMR_FSM_SIZE + TMR_FREE_SIZE + TMR_FIFO_SIZE + TMR_FIFO_STATIC_SIZE >
			TMR_FSM_ENGINE_OFFS) {
		dev_err(hdev->dev, "NIC TMR data shouldn't be bigger than %dMB\n",
			TMR_FSM_ENGINE_OFFS / SZ_1M);
		return -ENOMEM;
	}

	/* copy the MAC OUI in reverse */
	for (i = 0 ; i < 3 ; i++)
		mac[i] = HABANALABS_MAC_OUI_1 >> (8 * (2 - i));

	/* On simulator F/W is disabled so provide nic info by the driver. */
	if ((gaudi->hw_cap_initialized & HW_CAP_CPU_Q) || !hdev->pdev) {
		u8 *mac_addr;

		if (hdev->pdev) {
			rc = hl_fw_cpucp_nic_info_get(hdev);
			if (rc)
				return rc;
		}

		if (hdev->card_type == cpucp_card_type_pmc) {
			switch (le16_to_cpu(nic_info->serdes_type)) {
			case TYPE_1_SERDES_TYPE:
				hdev->asic_prop.server_type = HL_SERVER_GAUDI_TYPE1;
				break;
			case TYPE_2_SERDES_TYPE:
				hdev->asic_prop.server_type = HL_SERVER_GAUDI_TYPE2;
				break;
			case HLS1_SERDES_TYPE:
				hdev->asic_prop.server_type = HL_SERVER_GAUDI_HLS1;
				break;
			case HLS1H_SERDES_TYPE:
				hdev->asic_prop.server_type = HL_SERVER_GAUDI_HLS1H;
				break;
			default:
				hdev->asic_prop.server_type = HL_SERVER_TYPE_UNKNOWN;
				break;
			}
		}

		for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
			if (!(hdev->nic_ports_mask & BIT(i)))
				continue;

			mac_addr = mac_arr[i].mac_addr;
			if (strncmp(mac, mac_addr, 3)) {
				dev_err(hdev->dev,
					"bad MAC OUI %02x:%02x:%02x:%02x:%02x:%02x, port %d\n",
					mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
					mac_addr[4], mac_addr[5], i);
				return -EFAULT;
			}
		}

		if (!hdev->ignore_fw_nic_info || !hdev->pdev) {
			hdev->nic_ports_mask &= le64_to_cpu(nic_info->link_mask[0]);
			hdev->nic_ports_ext_mask &= le64_to_cpu(nic_info->link_ext_mask[0]);
			hdev->nic_auto_neg_mask &= le64_to_cpu(nic_info->auto_neg_mask[0]);
		}

		nic->card_location = le32_to_cpu(cpucp_info->card_location);
		nic->use_fw_serdes_info = true;
	} else {
		/*
		 * No CPU, hence set the MAC addresses manually.
		 * Each device will have its own unique MAC random.
		 */
		get_random_bytes(&mac[3], 2);

		for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
			mac[ETH_ALEN - 1] = i;
			memcpy(mac_arr[i].mac_addr, mac, ETH_ALEN);
		}

		card_location = RREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS);

		cpucp_info->card_location = cpu_to_le32((card_location >> 22) & 0x7);

		nic->card_location = card_location;

		/* TODO: remove when Autoneg is supported towards the switch */
		if ((hdev->card_type == cpucp_card_type_pci) && (hdev->nic_auto_neg_mask)) {
			dev_info(hdev->dev, "No Autoneg in PCI card\n");
			hdev->nic_auto_neg_mask = 0;
		}
	}

	/* no need to proceed if all ports are disabled */
	if (!hdev->nic_ports_mask)
		return 0;

	/* PCI card is usually connected directly to a switch so set all ports as external */
	if (hdev->card_type == cpucp_card_type_pci) {
		hdev->nic_ports_ext_mask = hdev->nic_ports_mask;
		hdev->nic_auto_neg_mask &= ~hdev->nic_ports_ext_mask;
	}

	/*
	 * The default is to turn on autoneg on all the ports which support it.
	 * The user can disable autoneg on the ports which supports it but cannot enable autoneg on
	 * the rest of the ports.
	 */
	nic->auto_neg_mask = hdev->nic_auto_neg_mask;

	gaudi_nic_phy_init(hdev);

	return 0;
}

static int gaudi_nic_core_init(struct hl_device *hdev)
{
	return gaudi_nic_cq_init(hdev);
}

static void gaudi_nic_core_fini(struct hl_device *hdev)
{
	gaudi_nic_cq_fini(hdev);
}

static void gaudi_nic_get_cnts_names(struct hl_nic_port *nic_port, u8 *data, bool ext)
{
	struct hl_en_stat *spmu_stats;
	u32 n_spmu_stats;
	int i;

	gaudi_nic_spmu_get_stats_info(nic_port, &spmu_stats, &n_spmu_stats);

	for (i = 0 ; i < n_spmu_stats ; i++)
		memcpy(data + i * ETH_GSTRING_LEN, spmu_stats[i].str, ETH_GSTRING_LEN);
	data += i * ETH_GSTRING_LEN;

	for (i = 0 ; i < hl_nic_mac_stats_rx_len ; i++)
		memcpy(data + i * ETH_GSTRING_LEN, hl_nic_mac_stats_rx[i].str, ETH_GSTRING_LEN);
	data += i * ETH_GSTRING_LEN;

	for (i = 0 ; i < hl_nic_mac_fec_stats_len ; i++)
		memcpy(data + i * ETH_GSTRING_LEN, hl_nic_mac_fec_stats[i].str, ETH_GSTRING_LEN);
	data += i * ETH_GSTRING_LEN;

	for (i = 0 ; i < hl_nic_mac_stats_tx_len ; i++)
		memcpy(data + i * ETH_GSTRING_LEN, hl_nic_mac_stats_tx[i].str, ETH_GSTRING_LEN);
}

static int gaudi_nic_get_cnts_num(struct hl_nic_port *nic_port)
{
	int n_spmu_stats, mac_counters;
	struct hl_en_stat *ignore;

	mac_counters = hl_nic_mac_stats_rx_len + hl_nic_mac_stats_tx_len;
	gaudi_nic_spmu_get_stats_info(nic_port, &ignore, &n_spmu_stats);

	return n_spmu_stats + mac_counters + hl_nic_mac_fec_stats_len;
}

static u64 gaudi_nic_read_mac_cnt(struct hl_nic_port *nic_port, int offset, bool is_rx)
{
	struct hl_device *hdev = nic_port->hdev;
	u64 lo_part, hi_part, start_reg, result;
	u32 port = nic_port->port, val;
	struct cpucp_packet pkt;
	int rc;

	if (!hdev->supports_coresight)
		return 0;

	if (!(hdev->asic_prop.fw_app_cpu_boot_dev_sts0 &
			CPU_BOOT_DEV_STS0_FW_NIC_STAT_XPCS91_EN)) {
		if (is_rx)
			if (port & 1)
				start_reg = NIC_MAC_RX_PORT1_OFFSET;
			else
				start_reg = NIC_MAC_RX_PORT0_OFFSET;
		else
			if (port & 1)
				start_reg = NIC_MAC_TX_PORT1_OFFSET;
			else
				start_reg = NIC_MAC_TX_PORT0_OFFSET;

		lo_part = NIC_MAC_STAT_RREG32(port, start_reg + offset);
		/* Volatile read: MUST read high part after low */
		hi_part = NIC_MAC_STAT_RREG32(port, NIC_MAC_STAT_HI_PART);

		return lo_part | (hi_part << 32);
	}

	val = FIELD_PREP(CPUCP_PKT_VAL_MAC_CNT_IN1_MASK, is_rx);
	val |= FIELD_PREP(CPUCP_PKT_VAL_MAC_CNT_IN2_MASK, offset);

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_STAT_REGS_GET <<
				CPUCP_PKT_CTL_OPCODE_SHIFT);
	/* we used this field as port_index didn't exist yet */
	pkt.index = cpu_to_le32(port);
	pkt.value = cpu_to_le64(val);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt),
						0, &result);

	if (rc) {
		dev_err(hdev->dev,
			"Failed to get NIC STAT counters for port %d, error %d\n",
			port, rc);
		return 0;
	}

	return result;
}

static int gaudi_nic_read_all_mac_cnts(struct hl_nic_port *nic_port, u64 *mac_cnts, u32 size)
{
	struct hl_device *hdev = nic_port->hdev;
	struct cpucp_packet pkt = {};
	u32 port = nic_port->port;
	dma_addr_t buf_dma_addr;
	void *buf_cpu_addr;
	int rc;

	buf_cpu_addr = hl_cpu_accessible_dma_pool_alloc(hdev, size, &buf_dma_addr);
	if (!buf_cpu_addr) {
		dev_err(hdev->dev, "Failed to allocate DMA memory for NIC MAC cnts packet\n");
		return -ENOMEM;
	}

	memset(buf_cpu_addr, 0, size);

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_STAT_REGS_ALL_GET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.addr = cpu_to_le64(buf_dma_addr);
	pkt.data_max_size = cpu_to_le32(size);
	pkt.port_index = cpu_to_le32(port);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc)
		dev_err(hdev->dev, "failed to send NIC MAC cnts CPUCP pkt, port %d\n", port);
	else
		memcpy(mac_cnts, buf_cpu_addr, size);

	hl_cpu_accessible_dma_pool_free(hdev, size, buf_cpu_addr);

	return rc;
}

static u32 __gaudi_nic_read_xpcs91_reg(struct hl_nic_port *nic_port, u32 lo_offset, u32 hi_offset)
{
	u32 lo_part, hi_part, base_offset, port = nic_port->port;

	base_offset = (port & 1) ? NIC_MAC_FEC_PORT1_OFFSET : NIC_MAC_FEC_PORT0_OFFSET;

	/* the mac is irrelevant for xpcs91 */
	lo_part = gaudi_nic_mac_read(nic_port, 0, "xpcs91", base_offset + lo_offset);
	hi_part = gaudi_nic_mac_read(nic_port, 0, "xpcs91", base_offset + hi_offset);

	return (hi_part << 16) | lo_part;
}

static void __gaudi_nic_read_xpcs91_regs(struct hl_nic_port *nic_port, u64 *out_data)
{
	u32 lo_offset, hi_offset;

	/* this function can be called from ethtool for external ports, from get_statistics ioctl
	 * for internal ports and from nic_status thread for both port types.
	 */
	mutex_lock(&nic_port->cnt_lock);

	lo_offset = hl_nic_mac_fec_stats[0].lo_offset;
	hi_offset = hl_nic_mac_fec_stats[0].hi_offset;

	nic_port->correctable_errors_cnt +=
					__gaudi_nic_read_xpcs91_reg(nic_port, lo_offset, hi_offset);
	out_data[0] = nic_port->correctable_errors_cnt;

	lo_offset = hl_nic_mac_fec_stats[1].lo_offset;
	hi_offset = hl_nic_mac_fec_stats[1].hi_offset;

	nic_port->uncorrectable_errors_cnt +=
					__gaudi_nic_read_xpcs91_reg(nic_port, lo_offset, hi_offset);
	out_data[1] = nic_port->uncorrectable_errors_cnt;

	mutex_unlock(&nic_port->cnt_lock);
}

static void gaudi_nic_read_xpcs91_regs(struct hl_nic_port *nic_port, u64 *out_data)
{
	bool fw_nic_stat_xpcs91_en, fw_nic_stat_ext_en;
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	int rc;

	fw_nic_stat_xpcs91_en = hdev->asic_prop.fw_app_cpu_boot_dev_sts0 &
							CPU_BOOT_DEV_STS0_FW_NIC_STAT_XPCS91_EN;

	fw_nic_stat_ext_en = hdev->asic_prop.fw_app_cpu_boot_dev_sts0 &
							CPU_BOOT_DEV_STS0_FW_NIC_STAT_EXT_EN;

	if (!fw_nic_stat_xpcs91_en && !fw_nic_stat_ext_en) {
		__gaudi_nic_read_xpcs91_regs(nic_port, out_data);
	} else if (fw_nic_stat_ext_en) {
		struct cpucp_packet pkt = {};
		dma_addr_t buf_dma_addr;
		u32 size, *buf_cpu_addr;

		size = NUM_OF_XPCS91_REGS * sizeof(u32);

		buf_cpu_addr = hl_cpu_accessible_dma_pool_alloc(hdev, size, &buf_dma_addr);
		if (!buf_cpu_addr) {
			dev_err(hdev->dev,
				"Failed to allocate DMA memory for NIC MAC cnts packet\n");
			return;
		}

		memset(buf_cpu_addr, 0, size);

		pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_XPCS91_REGS_GET <<
				      CPUCP_PKT_CTL_OPCODE_SHIFT);
		pkt.addr = cpu_to_le64(buf_dma_addr);
		pkt.data_max_size = cpu_to_le32(size);
		pkt.port_index = cpu_to_le32(port);

		rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
		if (rc) {
			dev_err(hdev->dev, "failed to send XPCS91 pkt, port %d\n", port);
		} else {
			nic_port->correctable_errors_cnt += buf_cpu_addr[0];
			out_data[0] = nic_port->correctable_errors_cnt;

			nic_port->uncorrectable_errors_cnt += buf_cpu_addr[1];
			out_data[1] = nic_port->uncorrectable_errors_cnt;
		}

		hl_cpu_accessible_dma_pool_free(hdev, size, buf_cpu_addr);
	} else {
		struct cpucp_array_data_packet *pkt;
		size_t total_pkt_size, data_size;

		data_size = NUM_OF_XPCS91_REGS * sizeof(u32);
		total_pkt_size = sizeof(struct cpucp_array_data_packet) + data_size;

		/* data should be aligned to 8 bytes in order to CPU-CP to copy it */
		total_pkt_size = (total_pkt_size + 0x7) & ~0x7;

		/* total_pkt_size is casted to u16 later on */
		if (total_pkt_size > USHRT_MAX) {
			dev_err(hdev->dev, "read XPCS91 registers pkt is too big\n");
			return;
		}

		pkt = kzalloc(total_pkt_size, GFP_KERNEL);
		if (!pkt)
			return;

		pkt->length = cpu_to_le32(NUM_OF_XPCS91_REGS);

		pkt->cpucp_pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_XPCS91_REGS_GET <<
							CPUCP_PKT_CTL_OPCODE_SHIFT);
		pkt->cpucp_pkt.index = cpu_to_le32(port);
		/* send the mask rather than the lane index for possible future use */
		pkt->cpucp_pkt.value = cpu_to_le64(nic_port->fw_tuning_mask);

		rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *)pkt, total_pkt_size, 0, NULL);
		if (rc) {
			dev_err(hdev->dev, "failed to send XPCS91 pkt, port %d\n", port);
		} else {
			nic_port->correctable_errors_cnt += le32_to_cpu(pkt->data[0]);
			out_data[0] = nic_port->correctable_errors_cnt;

			nic_port->uncorrectable_errors_cnt += le32_to_cpu(pkt->data[1]);
			out_data[1] = nic_port->uncorrectable_errors_cnt;
		}

		kfree(pkt);
	}
}

static void gaudi_nic_get_cnts_values(struct hl_nic_port *nic_port, u64 *data)
{
	u32 port = nic_port->port, num_spmus, size;
	struct asic_fixed_properties *prop;
	bool fw_nic_stat_ext_en;
	struct hl_device *hdev;
	int i, rc, offset;
	u64 *mac_cnts;

	hdev = nic_port->hdev;
	prop = &hdev->asic_prop;

	fw_nic_stat_ext_en = prop->fw_cpu_boot_dev_sts0_valid &&
					(prop->fw_app_cpu_boot_dev_sts0 &
					CPU_BOOT_DEV_STS0_FW_NIC_STAT_EXT_EN);
	if (fw_nic_stat_ext_en) {
		size = (hl_nic_mac_stats_rx_len + hl_nic_mac_stats_tx_len) * sizeof(u64);
		mac_cnts = kzalloc(size, GFP_KERNEL);
		if (mac_cnts) {
			rc = gaudi_nic_read_all_mac_cnts(nic_port, mac_cnts, size);
			if (rc) {
				dev_err(hdev->dev,
					"failed to fetch all MAC counters in one packet from FW, port %d\n",
					port);
				kfree(mac_cnts);
				fw_nic_stat_ext_en = false;
			}
		} else {
			fw_nic_stat_ext_en = false;
		}
	}

	rc = hl_nic_read_spmu_counters(nic_port, data, &num_spmus);
	if (rc)
		dev_err(hdev->dev, "Failed to get SPMU counters, port %d\n", port);

	data += num_spmus;

	if (fw_nic_stat_ext_en) {
		for (i = 0 ; i < hl_nic_mac_stats_rx_len ; i++)
			data[i] = mac_cnts[i];
	} else {
		for (i = 0 ; i < hl_nic_mac_stats_rx_len ; i++) {
			offset = hl_nic_mac_stats_rx[i].lo_offset;
			data[i] = gaudi_nic_read_mac_cnt(nic_port, offset, true);
		}
	}
	data += i;

	gaudi_nic_read_xpcs91_regs(nic_port, data);
	data += hl_nic_mac_fec_stats_len;

	if (fw_nic_stat_ext_en) {
		for (i = 0 ; i < hl_nic_mac_stats_tx_len ; i++)
			data[i] = mac_cnts[i + hl_nic_mac_stats_rx_len];
	} else {
		for (i = 0 ; i < hl_nic_mac_stats_tx_len ; i++) {
			offset = hl_nic_mac_stats_tx[i].lo_offset;
			data[i] = gaudi_nic_read_mac_cnt(nic_port, offset, false);
		}
	}

	if (fw_nic_stat_ext_en)
		kfree(mac_cnts);
}

static void gaudi_nic_reset_mac_stats(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	struct cpucp_packet pkt;
	int rc;

	if (!(hdev->asic_prop.fw_app_cpu_boot_dev_sts0 & CPU_BOOT_DEV_STS0_FW_NIC_STAT_XPCS91_EN)) {
		NIC_MACRO_WREG32(mmNIC0_STAT_STATN_CONFIG, 0x80000000);
		return;
	}

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_STAT_REGS_CLR << CPUCP_PKT_CTL_OPCODE_SHIFT);
	/* we used this field as port_index didn't exist yet */
	pkt.index = cpu_to_le32(port);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc)
		dev_err(hdev->dev, "Failed to clear NIC STAT registers, port %d, rc %d\n", port,
			rc);
}

static u32 gaudi_nic_read_tx_ci(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	return NIC_RREG32(mmNIC0_QPC0_REQ_RING0_CI);
}

static u32 gaudi_nic_read_rx_pi(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	return NIC_RREG32(mmNIC0_QPC0_RES_RING0_PI) & (NIC_RX_SIZE - 1);
}

static void gaudi_nic_ring_tx_doorbell(struct hl_aux_dev *aux_dev, u32 port, u32 pi)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	NIC_WREG32(mmNIC0_QPC0_SECURED_DOORBELL_PI, pi);
	NIC_WREG32(mmNIC0_QPC0_SECURED_DOORBELL_QPN, 0x80000000 | RAW_QPN);
}

static void gaudi_nic_configure_rx_irq(struct hl_aux_dev *aux_dev, u32 port, bool enable)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	u32 val;

	val = NIC_RREG32(mmNIC0_QPC0_INTERRUPT_MASK);

	if (enable)
		val |= 0x10;
	else
		val &= ~0x10;

	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_MASK, val);
}

static void gaudi_nic_reenable_rx_irq(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_CLR, 0xFFFF);
}

static u64 gaudi_nic_read_mac_stat_cnt(struct hl_aux_dev *aux_dev, u32 port, int idx, bool is_rx)
{
	struct hl_en_stat *stat = is_rx ? &hl_nic_mac_stats_rx[idx] : &hl_nic_mac_stats_tx[idx];
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];

	return gaudi_nic_read_mac_cnt(nic_port, stat->lo_offset, is_rx);
}

static int gaudi_nic_sw_init(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_en_aux_ops *aux_ops = &gaudi->en_aux_ops;
	struct gaudi_en_core_info *core_info = &gaudi->en_core_info;
	struct hl_nic *nic = &hdev->nic;
	void **rx_mem_addr;

	rx_mem_addr = kcalloc(NIC_NUMBER_OF_PORTS, sizeof(*rx_mem_addr), GFP_KERNEL);
	if (!rx_mem_addr)
		return -ENOMEM;

	core_info->pcie_bar = hdev->pcie_bar[HBM_BAR_ID];
	core_info->hbm_bar_cur_addr = &gaudi->hbm_bar_cur_addr;
	core_info->tx_ring_len = WQ_BUFFER_SIZE;
	/* See comment regarding the NIC_HW_MAX_QP_NUM value in the section of TXE configuration in
	 * gaudi_nic_hw_port_config().
	 */
	core_info->tx_wq_base_addr = NIC_HW_MAX_QP_NUM;
	core_info->rx_ring_len = NIC_RX_SIZE;
	core_info->base_rx_irq = RX_MSI_IDX;
	core_info->rx_mem_addr = rx_mem_addr;

	aux_ops->read_mac_stat = gaudi_nic_read_mac_stat_cnt;
	aux_ops->read_tx_ci = gaudi_nic_read_tx_ci;
	aux_ops->read_rx_pi = gaudi_nic_read_rx_pi;
	aux_ops->ring_tx_doorbell = gaudi_nic_ring_tx_doorbell;
	aux_ops->configure_rx_irq = gaudi_nic_configure_rx_irq;
	aux_ops->reenable_rx_irq = gaudi_nic_reenable_rx_irq;

	nic->skip_wq_arrays_pool = true;

	nic->ctrl_op_mask = BIT(HL_NIC_OP_ALLOC_CONN) |
			BIT(HL_NIC_OP_SET_REQ_CONN_CTX) |
			BIT(HL_NIC_OP_SET_RES_CONN_CTX) |
			BIT(HL_NIC_OP_DESTROY_CONN) |
			BIT(HL_NIC_OP_USER_WQ_SET) |
			BIT(HL_NIC_OP_USER_WQ_UNSET) |
			BIT(HL_NIC_OP_SET_USER_APP_PARAMS) |
			BIT(HL_NIC_OP_GET_USER_APP_PARAMS) |
			BIT(HL_NIC_OP_USER_CQ_SET) |
			BIT(HL_NIC_OP_USER_CQ_UNSET) |
			BIT(HL_NIC_OP_USER_CQ_UPDATE_CI) |
			BIT(HL_NIC_OP_DUMP_QP);

	return 0;
}

static void gaudi_nic_sw_fini(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_en_core_info *core_info = &gaudi->en_core_info;

	kfree(core_info->rx_mem_addr);
}

static void gaudi_nic_en_set_core_data(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_en_core_info *asic_core_info;
	struct hl_en_core_info *core_info;
	struct gaudi_nic_port *gaudi_port;
	struct hl_nic *nic = &hdev->nic;
	struct hl_en_aux_ops *aux_ops;
	struct hl_nic_port *nic_port;
	struct hl_aux_dev *aux_dev;
	int i;

	aux_dev = &nic->en_aux_dev;
	core_info = aux_dev->core_info;
	asic_core_info = &gaudi->en_core_info;
	core_info->asic_specific = asic_core_info;
	aux_ops = aux_dev->aux_ops;
	aux_ops->asic_ops = &gaudi->en_aux_ops;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];
		gaudi_port = nic_port->nic_specific;
		if (nic_port->eth_enable)
			asic_core_info->rx_mem_addr[i] = gaudi_port->rx_mem.addr;
	}
}

static int gaudi_register_qp(struct hl_nic_port *nic_port, u32 qp_id, u32 asid)
{
	return 0;
}

static void gaudi_unregister_qp(struct hl_nic_port *nic_port, u32 qp_id)
{
}

static void gaudi_get_qp_id_range(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id)
{
	*min_id = NIC_MIN_CONN_ID;
	*max_id = NIC_MAX_CONN_ID;
}

static u32 gaudi_nic_get_default_port_speed(struct hl_device *hdev)
{
	return hdev->pldm ? SPEED_50000 : SPEED_100000;
}

static void gaudi_nic_fill_spmu_data(struct hl_nic_port *nic_port,
					struct cpucp_nic_status *nic_status)
{
	struct hl_device *hdev = nic_port->hdev;
	u64 spmu_data[NIC_SPMU_STATS_LEN_MAX];
	u32 port = nic_port->port, ignore;
	int rc;

	memset(spmu_data, 0, sizeof(spmu_data));

	rc = hl_nic_read_spmu_counters(nic_port, spmu_data, &ignore);
	if (rc) {
		dev_err(hdev->dev, "Failed to get SPMU counters, port %d, %d\n", port, rc);
		return;
	}

	nic_status->bad_format_cnt = cpu_to_le32(spmu_data[0]);
	nic_status->responder_out_of_sequence_psn_cnt = cpu_to_le32(spmu_data[3]);
}

static void gaudi_nic_fill_fec_stats(struct hl_nic_port *nic_port,
					struct cpucp_nic_status *nic_status)
{
	u64 fec_data[2];

	memset(fec_data, 0, sizeof(fec_data));

	gaudi_nic_read_xpcs91_regs(nic_port, fec_data);

	nic_status->correctable_err_cnt = cpu_to_le32(nic_port->correctable_errors_cnt);
	nic_status->uncorrectable_err_cnt = cpu_to_le32(nic_port->uncorrectable_errors_cnt);
}

static void gaudi_nic_fill_nic_status(struct hl_nic_port *nic_port,
					struct cpucp_nic_status *nic_status)
{
	gaudi_nic_fill_spmu_data(nic_port, nic_status);
	gaudi_nic_fill_fec_stats(nic_port, nic_status);

	nic_status->timeout_retransmission_cnt = 0;
	nic_status->high_ber_cnt = 0;
}

static void gaudi_nic_cfg_lock(struct hl_nic_port *nic_port)
	__acquires(&gaudi_nic->cfg_lock)
{
	struct gaudi_nic_port *gaudi_nic = nic_port->nic_specific;

	mutex_lock(&gaudi_nic->cfg_lock);
}

static void gaudi_nic_cfg_unlock(struct hl_nic_port *nic_port)
	__releases(&gaudi_nic->cfg_lock)
{
	struct gaudi_nic_port *gaudi_nic = nic_port->nic_specific;

	mutex_unlock(&gaudi_nic->cfg_lock);
}

static bool gaudi_nic_cfg_is_locked(struct hl_nic_port *nic_port)
{
	struct gaudi_nic_port *gaudi_nic = nic_port->nic_specific;

	return mutex_is_locked(&gaudi_nic->cfg_lock);
}

static void gaudi_nic_override_phy_readiness(struct hl_nic_port *nic_port, bool set_ready)
{

}

static void gaudi_nic_qp_pre_destroy(struct hl_qp *qp)
{

}

static void gaudi_nic_qp_post_destroy(struct hl_qp *qp)
{

}

static bool gaudi_nic_is_coll_conn_id(struct hl_device *hdev, u32 conn_id)
{
	return false;
}

static u32 gaudi_nic_get_max_msg_sz(struct hl_device *hdev)
{
	return SZ_4K;
}

static void gaudi_nic_app_params_clear(struct hl_device *hdev)
{

}

static void gaudi_nic_set_port_status(struct hl_nic_port *nic_port, bool up)
{
	/* NoOps */
}

static int gaudi_nic_inject_rx_err(struct hl_device *hdev, u8 drop_percent)
{
	/* NoOps */
	return 0;
}

void gaudi_fw_nic_status(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;

	hl_fw_unmask_irq(hdev, nic_props->base_status_event_idx + nic_port->port);
}

static struct hl_nic_port_funcs gaudi_nic_port_funcs = {
	.port_hw_init = gaudi_nic_port_hw_init,
	.port_hw_fini = gaudi_nic_port_hw_fini,
	.phy_port_init = gaudi_nic_phy_port_init,
	.phy_port_start_stop = gaudi_nic_phy_port_start_stop,
	.phy_port_power_up = gaudi_nic_phy_port_power_up,
	.phy_port_reconfig = gaudi_nic_phy_port_reconfig,
	.phy_port_fini = gaudi_nic_phy_port_fini,
	.phy_link_status_work = gaudi_nic_phy_link_status_work,
	.update_qp_mtu = gaudi_nic_update_qp_mtu,
	.user_wq_arr_unset = gaudi_user_wq_arr_unset,
	.get_cq_id_range = gaudi_get_cq_id_range,
	.user_cq_set = gaudi_user_cq_set,
	.user_cq_unset = gaudi_user_cq_unset,
	.user_cq_destroy = gaudi_user_cq_destroy,
	.user_cq_update_ci = gaudi_user_cq_update_ci,
	.get_cnts_num = gaudi_nic_get_cnts_num,
	.get_cnts_names = gaudi_nic_get_cnts_names,
	.get_cnts_values = gaudi_nic_get_cnts_values,
	.port_sw_init = gaudi_nic_port_sw_init,
	.port_sw_fini = gaudi_nic_port_sw_fini,
	.spmu_get_stats_info = gaudi_nic_spmu_get_stats_info,
	.spmu_config = gaudi_nic_spmu_config,
	.spmu_sample = gaudi_nic_spmu_sample,
	.register_qp = gaudi_register_qp,
	.unregister_qp = gaudi_unregister_qp,
	.get_qp_id_range = gaudi_get_qp_id_range,
	.set_pfc = gaudi_nic_set_pfc,
	.qpc_write = gaudi_nic_qpc_write,
	.qpc_invalidate = gaudi_nic_qpc_invalidate,
	.qpc_query = gaudi_nic_qpc_query,
	.qpc_clear = gaudi_nic_qpc_clear,
	.reset_mac_stats = gaudi_nic_reset_mac_stats,
	.collect_fec_stats = gaudi_nic_debugfs_collect_fec_stats,
	.fill_nic_status = gaudi_nic_fill_nic_status,
	.cfg_lock = gaudi_nic_cfg_lock,
	.cfg_unlock = gaudi_nic_cfg_unlock,
	.cfg_is_locked = gaudi_nic_cfg_is_locked,
	.override_phy_readiness = gaudi_nic_override_phy_readiness,
	.qp_pre_destroy = gaudi_nic_qp_pre_destroy,
	.qp_post_destroy = gaudi_nic_qp_post_destroy,
	.set_port_status = gaudi_nic_set_port_status,
	.fw_nic_status = gaudi_fw_nic_status,
};

struct hl_nic_funcs gaudi_nic_funcs = {
	.pre_core_init = gaudi_nic_pre_core_init,
	.core_init = gaudi_nic_core_init,
	.core_fini = gaudi_nic_core_fini,
	.get_hw_cap = gaudi_nic_get_hw_cap,
	.set_hw_cap = gaudi_nic_set_hw_cap,
	.set_req_qp_ctx = gaudi_set_req_qp_ctx,
	.set_res_qp_ctx = gaudi_set_res_qp_ctx,
	.mac_addr_convert = gaudi_nic_mac_addr_convert,
	.user_wq_arr_set = gaudi_user_wq_arr_set,
	.user_set_app_params = gaudi_user_set_app_params,
	.user_get_app_params = gaudi_user_get_app_params,
	.get_default_port_speed = gaudi_nic_get_default_port_speed,
	.phy_reset_macro = gaudi_nic_phy_reset_macro,
	.get_phy_fw_name = gaudi_nic_phy_get_fw_name,
	.phy_fw_load_all = gaudi_nic_phy_fw_load_all,
	.phy_get_crc = gaudi_nic_phy_get_crc,
	.sw_init = gaudi_nic_sw_init,
	.sw_fini = gaudi_nic_sw_fini,
	.macro_sw_init = gaudi_nic_macro_sw_init,
	.macro_sw_fini = gaudi_nic_macro_sw_fini,
	.kernel_ctx_init = gaudi_nic_kernel_ctx_init,
	.kernel_ctx_fini = gaudi_nic_kernel_ctx_fini,
	.ctx_init = gaudi_nic_ctx_init,
	.ctx_fini = gaudi_nic_ctx_fini,
	.qp_read = gaudi_nic_debugfs_qp_read,
	.wqe_read = gaudi_nic_debugfs_wqe_read,
	.set_en_core_data = gaudi_nic_en_set_core_data,
	.is_coll_conn_id = gaudi_nic_is_coll_conn_id,
	.get_max_msg_sz = gaudi_nic_get_max_msg_sz,
	.app_params_clear = gaudi_nic_app_params_clear,
	.inject_rx_err = gaudi_nic_inject_rx_err,
	.port_funcs = &gaudi_nic_port_funcs,
};
