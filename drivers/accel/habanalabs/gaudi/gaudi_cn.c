// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2018-2024 HabanaLabs, Ltd.
 * Copyright (C) 2024-2025, Intel Corporation.
 * All Rights Reserved.
 */

#include "gaudi_cn.h"
#include "../include/hw_ip/nic/nic_general.h"

#define NUM_OF_XPCS91_REGS	2

void gaudi_cn_handle_qp_err(struct hl_device *hdev, u16 event_type)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_cn_aux_ops *gaudi_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi_aux_ops = &gaudi->cn_aux_ops;

	if (gaudi_aux_ops->handle_qp_err)
		gaudi_aux_ops->handle_qp_err(aux_dev, event_type - GAUDI_EVENT_NIC0_QP0);
}

int gaudi_cn_ctx_init(struct hl_ctx *ctx)
{
	struct gaudi_cn_aux_ops *gaudi_aux_ops;
	struct hl_device *hdev = ctx->hdev;
	struct hl_cn *cn = &hdev->cn;
	struct hl_cn_funcs *cn_funcs;
	struct gaudi_device *gaudi;
	struct hbl_aux_dev *aux_dev;
	int rc;

	aux_dev = &cn->cn_aux_dev;
	cn_funcs = hdev->asic_funcs->cn_funcs;
	gaudi = hdev->asic_specific;
	gaudi_aux_ops = &gaudi->cn_aux_ops;

	if (!cn_funcs->get_hw_cap(hdev))
		return 0;

	if (gaudi_aux_ops->ctx_init) {
		/* must be done before calling CN ctx_init as it might be used there */
		cn->ctx = ctx;

		rc = gaudi_aux_ops->ctx_init(aux_dev, ctx->asid);
		if (rc) {
			cn->ctx = NULL;
			return rc;
		}
	}

	return 0;
}

void gaudi_cn_ctx_fini(struct hl_ctx *ctx)
{
	struct gaudi_cn_aux_ops *gaudi_aux_ops;
	struct hl_device *hdev = ctx->hdev;
	struct hl_cn *cn = &hdev->cn;
	struct gaudi_device *gaudi;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi = hdev->asic_specific;
	gaudi_aux_ops = &gaudi->cn_aux_ops;

	/* Check the context pointer instead of the capability bit because the CN ctx_fini should
	 * be called even if the ports are stopped.
	 */
	if (!cn->ctx)
		return;

	/* No need to check for NULL pointer because here the context is not NULL so we can be sure
	 * that the aux_ops pointer is not NULL either.
	 */
	gaudi_aux_ops->ctx_fini(aux_dev, ctx->asid);

	/* must be done after calling CN ctx_fini as it might be used there */
	cn->ctx = NULL;
}

static bool gaudi_cn_get_hw_cap(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	return (gaudi->hw_cap_initialized & HW_CAP_NIC_DRV);
}

static void gaudi_cn_set_hw_cap(struct hl_device *hdev, bool enable)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	if (enable)
		gaudi->hw_cap_initialized |= HW_CAP_NIC_DRV;
	else
		gaudi->hw_cap_initialized &= ~HW_CAP_NIC_DRV;
}

static int gaudi_cn_pre_core_init(struct hl_device *hdev)
{
	struct hbl_cn_cpucp_info *cn_cpucp_info = &hdev->asic_prop.cn_props.cpucp_info;
	struct cpucp_info *cpucp_info = &hdev->asic_prop.cpucp_info;
	struct hbl_cn_cpucp_mac_addr *mac_arr = cn_cpucp_info->mac_addrs;
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct hl_cn *cn = &hdev->cn;
	u32 card_location;
	u8 mac[ETH_ALEN];
	int i, rc;

	if (TMR_FSM_SIZE + TMR_FREE_SIZE + TMR_FIFO_SIZE + TMR_FIFO_STATIC_SIZE >
			TMR_FSM_ENGINE_OFFS) {
		hl_err(hdev, "NIC TMR data shouldn't be bigger than %dMB\n",
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
			rc = hl_cn_cpucp_info_get(hdev);
			if (rc)
				return rc;
		}

		if (hdev->card_type == cpucp_card_type_pmc) {
			switch (cn_cpucp_info->serdes_type) {
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
			if (!(hdev->cn.ports_mask & BIT(i)))
				continue;

			mac_addr = mac_arr[i].mac_addr;
			if (strncmp(mac, mac_addr, 3)) {
				hl_err(hdev,
					"bad MAC OUI %02x:%02x:%02x:%02x:%02x:%02x, port %d\n",
					mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3],
					mac_addr[4], mac_addr[5], i);
				return -EFAULT;
			}
		}

		if (!hdev->ignore_fw_nic_info || !hdev->pdev) {
			hdev->cn.ports_mask &= cn_cpucp_info->link_mask[0];
			hdev->cn.ports_ext_mask &= cn_cpucp_info->link_ext_mask[0];
			hdev->cn.auto_neg_mask &= cn_cpucp_info->auto_neg_mask[0];
		}

		cn->card_location = le32_to_cpu(cpucp_info->card_location);
		cn->use_fw_serdes_info = true;
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

		cn->card_location = card_location;

		/* TODO: remove when Autoneg is supported towards the switch */
		if ((hdev->card_type == cpucp_card_type_pci) && (hdev->cn.auto_neg_mask)) {
			hl_info(hdev, "No Autoneg in PCI card\n");
			hdev->cn.auto_neg_mask = 0;
		}
	}

	/* no need to proceed if all ports are disabled */
	if (!hdev->cn.ports_mask)
		return 0;

	/* PCI card is usually connected directly to a switch so set all ports as external */
	if (hdev->card_type == cpucp_card_type_pci) {
		hdev->cn.ports_ext_mask = hdev->cn.ports_mask;
		hdev->cn.auto_neg_mask &= ~hdev->cn.ports_ext_mask;
	}

	return 0;
}

static int gaudi_cn_map_device_va(struct hbl_aux_dev *aux_dev, void *args, u64 *va)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return map_device_va(hdev->kernel_ctx, args, va);
}

static int gaudi_cn_unmap_device_va(struct hbl_aux_dev *aux_dev, void *args)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return unmap_device_va(hdev->kernel_ctx, args, false);
}

static int gaudi_cn_read_all_mac_cnts(struct hbl_aux_dev *aux_dev, u32 port, u64 *mac_cnts,
					u32 size)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct cpucp_packet pkt = {};
	dma_addr_t buf_dma_addr;
	void *buf_cpu_addr;
	int rc;

	buf_cpu_addr = hl_cpu_accessible_dma_pool_alloc(hdev, size, &buf_dma_addr);
	if (!buf_cpu_addr) {
		hl_err(hdev, "Failed to allocate DMA memory for NIC MAC cnts packet\n");
		return -ENOMEM;
	}

	memset(buf_cpu_addr, 0, size);

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_STAT_REGS_ALL_GET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.addr = cpu_to_le64(buf_dma_addr);
	pkt.data_max_size = cpu_to_le32(size);
	pkt.port_index = cpu_to_le32(port);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc)
		hl_err(hdev, "failed to send NIC MAC cnts CPUCP pkt, port %d\n", port);
	else
		memcpy(mac_cnts, buf_cpu_addr, size);

	hl_cpu_accessible_dma_pool_free(hdev, size, buf_cpu_addr);

	return rc;
}

static int gaudi_cn_read_xpcs91_regs(struct hbl_aux_dev *aux_dev, u32 port, u64 fw_tuning_mask,
					u32 *regs)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	bool fw_nic_stat_ext_en;
	int rc;

	fw_nic_stat_ext_en = hdev->asic_prop.fw_app_cpu_boot_dev_sts0 &
							CPU_BOOT_DEV_STS0_FW_NIC_STAT_EXT_EN;
	if (fw_nic_stat_ext_en) {
		struct cpucp_packet pkt = {};
		dma_addr_t buf_dma_addr;
		u32 size, *buf_cpu_addr;

		size = NUM_OF_XPCS91_REGS * sizeof(u32);

		buf_cpu_addr = hl_cpu_accessible_dma_pool_alloc(hdev, size, &buf_dma_addr);
		if (!buf_cpu_addr) {
			hl_err(hdev,
				"Failed to allocate DMA memory for NIC MAC cnts packet\n");
			return -ENOMEM;
		}

		memset(buf_cpu_addr, 0, size);

		pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_XPCS91_REGS_GET <<
				      CPUCP_PKT_CTL_OPCODE_SHIFT);
		pkt.addr = cpu_to_le64(buf_dma_addr);
		pkt.data_max_size = cpu_to_le32(size);
		pkt.port_index = cpu_to_le32(port);

		rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
		if (!rc) {
			regs[0] = buf_cpu_addr[0];
			regs[1] = buf_cpu_addr[1];
		} else {
			hl_err(hdev, "failed to send XPCS91 pkt, port %d\n", port);
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
			hl_err(hdev, "read XPCS91 registers pkt is too big\n");
			return -EIO;
		}

		pkt = kzalloc(total_pkt_size, GFP_KERNEL);
		if (!pkt)
			return -ENOMEM;

		pkt->length = cpu_to_le32(NUM_OF_XPCS91_REGS);

		pkt->cpucp_pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_XPCS91_REGS_GET <<
							CPUCP_PKT_CTL_OPCODE_SHIFT);
		pkt->cpucp_pkt.index = cpu_to_le32(port);
		/* send the mask rather than the lane index for possible future use */
		pkt->cpucp_pkt.value = cpu_to_le64(fw_tuning_mask);

		rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) pkt, total_pkt_size, 0, NULL);
		if (!rc) {
			regs[0] = le32_to_cpu(pkt->data[0]);
			regs[1] = le32_to_cpu(pkt->data[1]);
		} else {
			hl_err(hdev, "failed to send XPCS91 pkt, port %d\n", port);
		}

		kfree(pkt);
	}

	return rc;
}

static u32 gaudi_cn_get_fault_counters(struct hbl_aux_dev *aux_dev, u32 port, u64 fw_tuning_mask)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct cpucp_packet pkt;
	u64 result = 0;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_FAULT_GET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	/* we used this field as port_index didn't exist yet */
	pkt.index = cpu_to_le32(port);
	/* send the mask rather than the lane index for possible future use */
	pkt.value = cpu_to_le64(fw_tuning_mask);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, &result);
	if (rc) {
		hl_err(hdev, "Failed to get remote fault cnt for port %d, error %d\n", port,
			rc);
		return 0;
	}

	/* the counters value is 32-bit wide */
	return (u32) result;
}

static int gaudi_cn_config_port_mac_ch(struct hbl_aux_dev *aux_dev, u32 port, u32 speed)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct cpucp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_MAC_CFG << CPUCP_PKT_CTL_OPCODE_SHIFT);
	/* we used this field as port_index didn't exist yet */
	pkt.index = cpu_to_le32(port);
	pkt.value = cpu_to_le64(speed);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc) {
		hl_err(hdev, "Failed to init MAC via FW for port %d, %d\n", port, rc);
		return rc;
	}

	return 0;
}

static int gaudi_cn_set_pfc(struct hbl_aux_dev *aux_dev, u32 port, u64 fw_tuning_mask, bool enable)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct cpucp_packet pkt;
	u32 val;
	int rc;

	val = FIELD_PREP(CPUCP_PKT_VAL_PFC_IN1_MASK, enable);
	val |= FIELD_PREP(CPUCP_PKT_VAL_PFC_IN2_MASK, fw_tuning_mask);

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_PFC_SET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	/* we used this field as port_index didn't exist yet */
	pkt.index = cpu_to_le32(port);
	pkt.value = cpu_to_le64(val);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc) {
		hl_err(hdev, "Failed to %s PFC for port %d, %d\n",
			enable ? "enable" : "disable", port, rc);
		return rc;
	}

	return 0;
}

static int gaudi_cn_set_lpbk(struct hbl_aux_dev *aux_dev, u32 port, u64 fw_tuning_mask, bool enable)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct cpucp_packet pkt;
	u32 val;
	int rc;

	val = FIELD_PREP(CPUCP_PKT_VAL_LPBK_IN1_MASK, enable);
	val |= FIELD_PREP(CPUCP_PKT_VAL_LPBK_IN2_MASK, fw_tuning_mask);

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_LPBK_SET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	/* we used this field as port_index didn't exist yet */
	pkt.index = cpu_to_le32(port);
	pkt.value = cpu_to_le64(val);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc) {
		hl_err(hdev, "Failed to %s MAC loopback for port %d, %d\n",
			enable ? "enable" : "disable", port, rc);
		return rc;
	}

	return 0;
}

static int gaudi_cn_read_mac_cnt(struct hbl_aux_dev *aux_dev, u32 port, int offset, bool is_rx)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct cpucp_packet pkt;
	u64 result = 0;
	u32 val;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	val = FIELD_PREP(CPUCP_PKT_VAL_MAC_CNT_IN1_MASK, is_rx);
	val |= FIELD_PREP(CPUCP_PKT_VAL_MAC_CNT_IN2_MASK, offset);

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_STAT_REGS_GET << CPUCP_PKT_CTL_OPCODE_SHIFT);
	/* we used this field as port_index didn't exist yet */
	pkt.index = cpu_to_le32(port);
	pkt.value = cpu_to_le64(val);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, &result);
	if (rc) {
		hl_err(hdev, "Failed to get NIC STAT counters for port %d, error %d\n", port,
			rc);
		return 0;
	}

	return result;
}

static void gaudi_cn_reset_mac_stats(struct hbl_aux_dev *aux_dev, u32 port)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct cpucp_packet pkt;
	int rc;

	memset(&pkt, 0, sizeof(pkt));

	pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_STAT_REGS_CLR << CPUCP_PKT_CTL_OPCODE_SHIFT);
	/* we used this field as port_index didn't exist yet */
	pkt.index = cpu_to_le32(port);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc)
		hl_err(hdev, "Failed to clear NIC STAT registers, port %d, rc %d\n", port,
			rc);
}

static void *gaudi_cn_dma_alloc_coherent(struct hbl_aux_dev *aux_dev, size_t size,
					  dma_addr_t *dma_handle, gfp_t flag)
{
	return hl_cn_dma_alloc_coherent(aux_dev, size, dma_handle, flag);
}

static void gaudi_cn_dma_free_coherent(struct hbl_aux_dev *aux_dev, size_t size, void *cpu_addr,
					dma_addr_t dma_handle)
{
	hl_cn_dma_free_coherent(aux_dev, size, cpu_addr, dma_handle);
}

static void *gaudi_cn_dma_pool_zalloc(struct hbl_aux_dev *aux_dev, size_t size, gfp_t mem_flags,
				       dma_addr_t *dma_handle)
{
	return hl_cn_dma_pool_zalloc(aux_dev, size, mem_flags, dma_handle);
}

static void gaudi_cn_dma_pool_free(struct hbl_aux_dev *aux_dev, void *vaddr, dma_addr_t dma_addr)
{
	hl_cn_dma_pool_free(aux_dev, vaddr, dma_addr);
}

static int gaudi_cn_get_hw_block_handle(struct hbl_aux_dev *aux_dev, u64 address, u64 *handle)
{
	return hl_cn_get_hw_block_handle(aux_dev, address, handle);
}

static int gaudi_cn_dma_mmap(struct hbl_aux_dev *aux_dev, struct vm_area_struct *vma,
				void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hdev->asic_funcs->mmap(hdev, vma, cpu_addr, dma_addr, size);
}

static void gaudi_cn_device_reset(struct hbl_aux_dev *aux_dev)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	hl_device_cond_reset(hdev, HL_DRV_RESET_HARD, HL_NOTIFIER_EVENT_DEVICE_RESET);
}

static void gaudi_cn_set_cn_data(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_cn_aux_ops *gaudi_aux_ops;
	struct gaudi_cn_aux_data *gaudi_aux_data;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_cn_aux_ops *aux_ops;
	struct hbl_aux_dev *aux_dev;
	u32 secured_sts;

	aux_dev = &cn->cn_aux_dev;
	gaudi_aux_data = &gaudi->cn_aux_data;
	cn->asic_specific_dev_info = gaudi_aux_data;
	aux_ops = aux_dev->aux_ops;
	gaudi_aux_ops = &gaudi->cn_aux_ops;
	aux_ops->asic_ops = gaudi_aux_ops;
	secured_sts = prop->fw_app_cpu_boot_dev_sts0;

	gaudi_aux_data->pcie_bar = hdev->pcie_bar[HBM_BAR_ID];
	gaudi_aux_data->hbm_bar_cur_addr = &gaudi->hbm_bar_cur_addr;
	gaudi_aux_data->rx_msi_addr = RX_MSI_ADDRESS;
	gaudi_aux_data->card_type = hdev->card_type == cpucp_card_type_pci ? gaudi_card_type_pci :
										gaudi_card_type_pmc;
	gaudi_aux_data->mac_ch_secured = !!(secured_sts & CPU_BOOT_DEV_STS0_FW_NIC_MAC_EN);
	gaudi_aux_data->stat_xpcs91_secured =
					!!(secured_sts & CPU_BOOT_DEV_STS0_FW_NIC_STAT_XPCS91_EN);
	gaudi_aux_data->stat_ext_secured_read =
					!!(secured_sts & CPU_BOOT_DEV_STS0_FW_NIC_STAT_EXT_EN);
	gaudi_aux_data->mmap_type_flag = HL_MMAP_TYPE_CN_MEM;
	gaudi_aux_data->vendor_part_id = prop->pci_id;
	gaudi_aux_data->kernel_asid = HL_KERNEL_ASID_ID;
	gaudi_aux_data->card_location = hdev->ignore_fw_nic_info ? hdev->card_location_override :
								cn->card_location;
	gaudi_aux_data->minor = hdev->id;
	gaudi_aux_data->dev_mgmt_fw = !!(hdev->fw_components & FW_TYPE_BOOT_CPU);

	gaudi_aux_ops->map_device_va = gaudi_cn_map_device_va;
	gaudi_aux_ops->unmap_device_va = gaudi_cn_unmap_device_va;
	gaudi_aux_ops->read_all_mac_cnts = gaudi_cn_read_all_mac_cnts;
	gaudi_aux_ops->read_xpcs91_regs = gaudi_cn_read_xpcs91_regs;
	gaudi_aux_ops->get_fault_counters = gaudi_cn_get_fault_counters;
	gaudi_aux_ops->config_port_mac_ch = gaudi_cn_config_port_mac_ch;
	gaudi_aux_ops->set_pfc = gaudi_cn_set_pfc;
	gaudi_aux_ops->set_lpbk = gaudi_cn_set_lpbk;
	gaudi_aux_ops->read_mac_cnt = gaudi_cn_read_mac_cnt;
	gaudi_aux_ops->reset_mac_stats = gaudi_cn_reset_mac_stats;
#ifdef HL_DOWNSTREAM
	gaudi_aux_ops->sim_init_props = gaudi_sim_cn_early_init_props_ext;
#endif /* HL_DOWNSTREAM */
	gaudi_aux_ops->dma_alloc_coherent = gaudi_cn_dma_alloc_coherent;
	gaudi_aux_ops->dma_free_coherent = gaudi_cn_dma_free_coherent;
	gaudi_aux_ops->dma_pool_zalloc = gaudi_cn_dma_pool_zalloc;
	gaudi_aux_ops->dma_pool_free = gaudi_cn_dma_pool_free;
	gaudi_aux_ops->get_hw_block_handle = gaudi_cn_get_hw_block_handle;
	gaudi_aux_ops->dma_mmap = gaudi_cn_dma_mmap;
	gaudi_aux_ops->spmu_get_stats_names = hl_cn_spmu_get_stats_names;
	gaudi_aux_ops->spmu_get_stats_event_types = hl_cn_spmu_get_stats_event_types;
	gaudi_aux_ops->spmu_config = hl_cn_spmu_config;
	gaudi_aux_ops->spmu_sample = hl_cn_spmu_sample;
	gaudi_aux_ops->poll_reg = hl_cn_poll_reg;
	gaudi_aux_ops->send_cpu_message = hl_cn_send_cpu_message;
	gaudi_aux_ops->post_send_status = hl_cn_post_send_status;
	gaudi_aux_ops->device_reset = gaudi_cn_device_reset;
}

static int gaudi_cn_mmap(struct hl_device *hdev, u32 asid, struct vm_area_struct *vma)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_cn_aux_ops *gaudi_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi_aux_ops = &gaudi->cn_aux_ops;

	if (gaudi_aux_ops->mmap)
		return gaudi_aux_ops->mmap(aux_dev, asid, vma);

	return -ENODEV;
}

static int gaudi_reserve_irqs(struct hl_device *hdev, u32 num, int *base_irq)
{
	return 0;
}

static int gaudi_cn_cmd_control(struct hl_device *hdev, u32 op, void *input, void *output,
				u32 asid)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_cn_aux_ops *gaudi_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi_aux_ops = &gaudi->cn_aux_ops;

	if (gaudi_aux_ops->cmd_control)
		return gaudi_aux_ops->cmd_control(aux_dev, op, input, output, asid);

	return -ENODEV;
}

static void gaudi_cn_post_send_status(struct hl_device *hdev, u32 port)
{
	hl_fw_unmask_irq(hdev, GAUDI_EVENT_STATUS_NIC0_ENG0 + port);
}

static void gaudi_cn_ports_stop_prepare(struct hl_device *hdev, bool hard_reset, bool in_teardown)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_cn_aux_ops *gaudi_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi_aux_ops = &gaudi->cn_aux_ops;

	if (gaudi_aux_ops->ports_stop_prepare)
		gaudi_aux_ops->ports_stop_prepare(aux_dev, hard_reset, in_teardown);
}

static int gaudi_cn_send_port_cpucp_status(struct hl_device *hdev, u32 port, u8 cmd, u8 period)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_cn_aux_ops *gaudi_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi_aux_ops = &gaudi->cn_aux_ops;

	if (gaudi_aux_ops->send_port_cpucp_status)
		return gaudi_aux_ops->send_port_cpucp_status(aux_dev, port, cmd, period);

	return -ENODEV;
}

static int gaudi_cn_dump_port_statistics(struct hl_device *hdev, u32 port, u64 str_buf_ptr,
						u64 val_buf_ptr, u32 *num_of_stat)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct gaudi_cn_aux_ops *gaudi_aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	gaudi_aux_ops = &gaudi->cn_aux_ops;

	if (gaudi_aux_ops->dump_port_statistics)
		return gaudi_aux_ops->dump_port_statistics(aux_dev, port, str_buf_ptr, val_buf_ptr,
								num_of_stat);

	return -ENODEV;
}

static struct hl_cn_port_funcs gaudi_cn_port_funcs = {
	.spmu_get_stats_names = gaudi_cn_spmu_get_stats_names,
	.spmu_get_stats_event_types = gaudi_cn_spmu_get_stats_event_types,
	.spmu_config = gaudi_cn_spmu_config,
	.spmu_sample = gaudi_cn_spmu_sample,
	.post_send_status = gaudi_cn_post_send_status,
	.ports_stop_prepare = gaudi_cn_ports_stop_prepare,
	.send_port_cpucp_status = gaudi_cn_send_port_cpucp_status,
	.dump_port_statistics = gaudi_cn_dump_port_statistics,
};

struct hl_cn_funcs gaudi_cn_funcs = {
	.get_hw_cap = gaudi_cn_get_hw_cap,
	.set_hw_cap = gaudi_cn_set_hw_cap,
	.pre_core_init = gaudi_cn_pre_core_init,
	.set_cn_data = gaudi_cn_set_cn_data,
	.mmap = gaudi_cn_mmap,
	.reserve_irqs = gaudi_reserve_irqs,
	.cmd_control = gaudi_cn_cmd_control,
	.port_funcs = &gaudi_cn_port_funcs,
};
