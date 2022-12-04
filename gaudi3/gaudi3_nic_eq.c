// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3_nic.h"
#include "../include/gaudi3/asic_reg/gaudi3_regs.h"
#include "../include/hw_ip/nic/nic_general.h"

#include <linux/pci.h>

#define GAUDI3_NIC_MAX_STRING_LEN	64

static const char gaudi3_nic_eq_irq_name[NIC_MAX_NUMBER_OF_PORTS][GAUDI3_NIC_MAX_STRING_LEN] = {
	"gaudi3 nic0 qpc EQ",
	"gaudi3 nic1 qpc EQ",
	"gaudi3 nic2 qpc EQ",
	"gaudi3 nic3 qpc EQ",
	"gaudi3 nic4 qpc EQ",
	"gaudi3 nic5 qpc EQ",
	"gaudi3 nic6 qpc EQ",
	"gaudi3 nic7 qpc EQ",
	"gaudi3 nic8 qpc EQ",
	"gaudi3 nic9 qpc EQ",
	"gaudi3 nic10 qpc EQ",
	"gaudi3 nic11 qpc EQ",
	"gaudi3 nic12 qpc EQ",
	"gaudi3 nic13 qpc EQ",
	"gaudi3 nic14 qpc EQ",
	"gaudi3 nic15 qpc EQ",
	"gaudi3 nic16 qpc EQ",
	"gaudi3 nic17 qpc EQ",
	"gaudi3 nic18 qpc EQ",
	"gaudi3 nic19 qpc EQ",
	"gaudi3 nic20 qpc EQ",
	"gaudi3 nic21 qpc EQ",
	"gaudi3 nic22 qpc EQ",
	"gaudi3 nic23 qpc EQ",
};

struct dq_qp_info {
	struct hlist_node	node;
	struct hl_nic_ev_dq	*dq;
	u32			qpn;
};

/*
 * Event queues for all the NICs are initialized ahead of NIC-specific
 * initialization regardless of NIC being enabled or not, we behave the same for
 * their IRQs.
 */
static irqreturn_t gaudi3_nic_eq_isr(int irq, void *arg);
static irqreturn_t gaudi3_nic_eq_threaded_isr(int irq, void *arg);

int gaudi3_nic_eq_request_irqs(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_nic_port *gaudi3_nic;
	int i, rc, irq;

	if (hdev->nic_poll_enable)
		return 0;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		gaudi3_nic = &gaudi3->nic_ports[i];
		irq = hl_irq_vector(hdev, GAUDI3_IRQ_NUM_NIC_PORT_FIRST + i);
		rc = request_threaded_irq(irq,
					gaudi3_nic_eq_isr,
					gaudi3_nic_eq_threaded_isr,
					IRQF_ONESHOT,
					gaudi3_nic_eq_irq_name[i],
					gaudi3_nic);
		if (rc) {
			dev_err(hdev->dev, "Failed to request IRQ %d for port %d\n", irq, i);
			goto irq_fail;
		}
	}

	return 0;

irq_fail:
	for (i-- ; i >= 0 ; i--) {
		irq = pci_irq_vector(hdev->pdev, GAUDI3_IRQ_NUM_NIC_PORT_FIRST + i);
		free_irq(irq, gaudi3_nic);
	}
	return rc;
}

void gaudi3_nic_eq_sync_irqs(struct hl_device *hdev)
{
	int i, irq;

	if (hdev->nic_poll_enable)
		return;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		irq = hl_irq_vector(hdev, GAUDI3_IRQ_NUM_NIC_PORT_FIRST + i);
		synchronize_irq(irq);
	}
}

void gaudi3_nic_eq_free_irqs(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_nic_port *gaudi3_nic;
	int i, irq;

	if (hdev->nic_poll_enable)
		return;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		gaudi3_nic = &gaudi3->nic_ports[i];
		irq = hl_irq_vector(hdev, GAUDI3_IRQ_NUM_NIC_PORT_FIRST + i);
		free_irq(irq, gaudi3_nic);
	}
}

static void nic_eq_null_handler(struct gaudi3_nic_port *gaudi3_nic)
{

}

/* NIC HW per port link/lane status mask */
static u32 gaudi3_nic_get_link_status_mask(struct gaudi3_nic_port *gaudi3_nic)
{
	switch (gaudi3_nic->nic_port->speed) {
	case SPEED_200000:
		fallthrough;
	case SPEED_400000:
		/* Note, driver uses link status mask as:
		 * (link_status >> port_shift) & mask
		 */
		return 0x1;
	case SPEED_100000:
		/* 100GbE mode is not supported in SW. */
		fallthrough;
	default:
		dev_err(gaudi3_nic->hdev->dev,
			"Unsupported speed %d\n", gaudi3_nic->nic_port->speed);
	}

	return 0;
}

static void gaudi3_nic_link_event_handler(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_device *hdev = gaudi3_nic->hdev;
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct hl_nic_macro *nic_macro;
	struct hl_nic_port *nic_port_curr;
	struct gaudi3_nic_port *gaudi3_nic_curr;
	u32 curr_link_sts, link_sts_change, link_status_mask, port, port_curr;
	int port_shift, first_lane;
	bool link_up;
	u8 l;

	port = gaudi3_nic->nic_port->port;
	nic_macro = gaudi3_nic->nic_port->nic_macro;

	curr_link_sts = (NIC_RREG32(mmD0_NIC0_MAC_CORE_BASE + mmPRT_MAC_CORE_PCS_REC_STS) &
					PRT_MAC_CORE_PCS_REC_STS_LINK_STATUS_M) >>
						PRT_MAC_CORE_PCS_REC_STS_LINK_STATUS_S;

	/* get the change on the serdes by XOR with previous val */
	link_sts_change = curr_link_sts ^ nic_macro->rec_link_sts;

	/* store current value as previous (for next round) */
	nic_macro->rec_link_sts = curr_link_sts;

	/* Iterate all SERDES links and check which one was changed */
	for (l = 0 ; l < NIC_MAC_LANES ; l++) {
		if (!(link_sts_change & (1 << l)))
			continue;

		/* TODO: SW-71221 - support multiple ports */
		/* get the port struct to handle its link according to the
		 * current SERDES link index
		 */
		port_curr = get_port_from_lane(nic_macro, l);
		gaudi3_nic_curr = &gaudi3->nic_ports[port_curr];

		nic_port_curr = gaudi3_nic_curr->nic_port;
		first_lane = get_lane_offset(gaudi3_nic_curr);
		port_shift = first_lane;

		mutex_lock(&nic_port_curr->control_lock);

		/* Skip in case the port is closed or in mac_loopbcak mode, since the port_close
		 * method took care of disabling the carrier and stopping the queue and in
		 * mac_loopback mode we bring up the port anyway and shouldn't depend on the
		 * link event.
		 */
		if (!hl_nic_is_port_open(nic_port_curr) || nic_port_curr->mac_loopback) {
			mutex_unlock(&nic_port_curr->control_lock);
			continue;
		}

		link_status_mask = gaudi3_nic_get_link_status_mask(gaudi3_nic_curr);
		link_up = (curr_link_sts >> port_shift) & link_status_mask;
		nic_port_curr->pcs_link = link_up;

		if (!hdev->nic.phy_config_fw)
			hl_nic_phy_set_port_status(nic_port_curr, link_up);

		mutex_unlock(&nic_port_curr->control_lock);
	}
}

static void gaudi3_nic_eq_dispatcher_default_handler(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_port *nic_port = gaudi3_nic->nic_port;
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 syndrome, event_type, port, qpn;
	struct hl_nic_eqe eqe;

	port = nic_port->port;

	mutex_lock(&nic_port->control_lock);

	while (!hl_nic_eq_dispatcher_dequeue(nic_port, HL_KERNEL_ASID_ID, &eqe, true)) {
		if (!EQE_IS_VALID(&eqe)) {
			dev_warn_ratelimited(
					hdev->dev,
					"Port-%d got invalid EQE on default queue!\n", port);
			continue;
		}

		event_type = EQE_TYPE(&eqe);
		syndrome = EQE_QP_EVENT_ERR_SYND(&eqe);

		switch (event_type) {
		case EQE_COMP:
			dev_warn_ratelimited(hdev->dev, "Port-%d comp event for invalid CQ:%d\n",
						port, EQE_CQ_EVENT_CQ_NUM(&eqe));
			break;
		case EQE_RAW_TX_COMP:
			dev_warn_ratelimited(hdev->dev, "Port-%d raw-tx-comp event for invalid QP:%d\n",
						port, EQE_RAW_TX_EVENT_QPN(&eqe));
			break;
		case EQE_QP_ERR:
			qpn = EQE_QP_EVENT_QPN(&eqe);

			if (!is_coll_qp_in_reset(nic_port, qpn))
				dev_warn_ratelimited(hdev->dev,
				"port:%d Got QP-error event: QP:%d, err (%d): %s: %s\n",
				port, EQE_QP_EVENT_QPN(&eqe), syndrome,
				gaudi3_nic_qp_err_src_to_str(syndrome),
				gaudi3_nic_qp_err_syndrom_to_str(syndrome));
			break;
		case EQE_COMP_ERR:
			dev_warn_ratelimited(hdev->dev, "Port-%d cq-err event for invalid CQ:%d\n",
						port, EQE_CQ_EVENT_CQ_NUM(&eqe));
			break;
		case EQE_DB_FIFO_OVERRUN:
			dev_warn_ratelimited(hdev->dev,
						"Port-%d db-fifo overrun event for invalid DB:%d\n",
						port, EQE_DB_EVENT_DB_NUM(&eqe));
			break;
		case EQE_CONG:
			dev_warn_ratelimited(hdev->dev,
						"Port-%d congestion event for invalid CCQ:%d\n",
						port, EQE_CQ_EVENT_CCQ_NUM(&eqe));
			break;
		case EQE_CONG_ERR:
			/* congestion error due to cc cq hw bug is known */
			gaudi3_nic->cong_q_err_cnt++;
			dev_dbg_ratelimited(hdev->dev, "Port-%d congestion error event\n", port);
			break;
		case EQE_WTD_SECURITY_ERR:
			dev_warn_ratelimited(hdev->dev,
						"Port-%d WTD security error for invalid QP:%d\n",
						port, EQE_QP_EVENT_QPN(&eqe));
			break;
		case EQE_NUMERICAL_ERR:
			dev_warn_ratelimited(hdev->dev,
						"Port-%d numerical error for invalid QP:%d\n",
						port, EQE_QP_EVENT_QPN(&eqe));
			break;
		default:
			dev_warn_ratelimited(hdev->dev, "Port-%d unsupported event type: %d",
					port, event_type);
		}
	}

	mutex_unlock(&nic_port->control_lock);
}

void gaudi3_nic_eq_reset_ring(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_ring *ring = &gaudi3_nic->eq_ring;
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port = gaudi3_nic->nic_port->port, offset;

	/* Set base address for event queue */
	offset = ELEMENT_OFFSET(port, QPC_EQ_NUM);

	/* Disable EQ first */
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_EVENT_QUE_CFG_0, 0);

	/* Perform a read to make sure the EQ is disabled */
	NIC_OFFSET_RREG32(mmD0_NIC0_QPC_EVENT_QUE_CFG_0);

	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_EVENT_QUE_WRITE_INDEX_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_EVENT_QUE_PRODUCER_INDEX_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_EVENT_QUE_CONSUMER_INDEX_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_EVENT_QUE_CONSUMER_INDEX_CB_0, 0);

	/* Reset SW indices */
	*((u32 *) RING_PI_ADDRESS(ring)) = 0;
	ring->pi_shadow = 0;
	ring->ci_shadow = 0;
	ring->rep_idx = 0;

	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_EVENT_QUE_CFG_0,
			offset << NIC_QPC_EVENT_QUE_CFG_EQ_ID_S |
			NIC_QPC_EVENT_QUE_CFG_INTERRUPT_PER_EQE_M |
			NIC_QPC_EVENT_QUE_CFG_OVERRUN_EN_M |
			NIC_QPC_EVENT_QUE_CFG_WRITE_PI_EN_M |
			NIC_QPC_EVENT_QUE_CFG_ENABLE_M);
}

static void nic_eq_handler(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_port *nic_port = gaudi3_nic->nic_port;
	struct hl_device *hdev = gaudi3_nic->hdev;
	struct hl_nic_ring *eq_ring;
	struct hl_nic_eqe *eqe_p;
	u32 event_type, port, offset;
	int rc;

	eq_ring = &gaudi3_nic->eq_ring;
	port = nic_port->port;
	offset = ELEMENT_OFFSET(port, QPC_EQ_NUM);

	/* read the producer index from HW once. New event, received
	 * after the "read once", will be handled in the next callback.
	 */
	eq_ring->pi_shadow = *((u32 *) RING_PI_ADDRESS(eq_ring));

	while (eq_ring->ci_shadow != eq_ring->pi_shadow) {
		eqe_p = (struct hl_nic_eqe *) RING_BUF_ADDRESS(eq_ring) +
			(eq_ring->ci_shadow & (eq_ring->count - 1));
		if (!EQE_IS_VALID(eqe_p)) {
			dev_warn_ratelimited(hdev->dev,
				"Port-%d got invalid EQE on EQ (eq.data[0] 0x%x, ci 0x%x, pi 0x%x)\n",
				port, eqe_p->data[0],
				eq_ring->ci_shadow, eq_ring->pi_shadow);
			break;
		}

		event_type = EQE_TYPE(eqe_p);

		/* In case this is link event, we handle it now and the dispatcher won't be
		 * involved.
		 */
		if (event_type == EQE_LINK_STATUS) {
			gaudi3_nic_link_event_handler(gaudi3_nic);
		} else {
			rc = hl_nic_eq_dispatcher_enqueue(nic_port, eqe_p);
			if (rc)
				dev_warn_ratelimited(hdev->dev,
						"failed to dispatch event %d, err %d\n",
						event_type, rc);
		}

		eq_ring->rep_idx++;
		eq_ring->ci_shadow = (eq_ring->ci_shadow + 1) & EQ_IDX_MASK;
		/* Mark the EQ-entry is not valid */
		EQE_SET_INVALID(eqe_p);

		/* Update the HW consumer index, every quarter ring, with an
		 * absolute value (ci_shadow is a wrap-around value).
		 * Use the read producer index value for that.
		 */
		if (eq_ring->rep_idx > (eq_ring->count / 4) - 1) {
			eq_ring->rep_idx = 0;
			NIC_OFFSET_WREG32(mmD0_NIC0_QPC_EVENT_QUE_CONSUMER_INDEX_0,
						eq_ring->ci_shadow);
		}
	}

	gaudi3_nic->eq_handler(gaudi3_nic);

	/* Handle unknown resources and events */
	gaudi3_nic_eq_dispatcher_default_handler(gaudi3_nic);
}

static inline void gaudi3_nic_eq_clr_interrupts(struct gaudi3_nic_port *gaudi3_nic)
{
	u32 port = gaudi3_nic->nic_port->port;
	struct hl_device *hdev = gaudi3_nic->hdev;

	/* Release the HW to allow more EQ interrupts.
	 * No need for interrupt masking. As long as the SW hasn't set the clear reg,
	 * new interrupts won't be raised
	 */
	NIC_WREG32(mmD0_NIC0_QPC_INTERRUPT_CLR, GAUDI3_NIC_EQ_INTERRUPT_M(port));
}

static void gaudi3_nic_eq_work(struct work_struct *work)
{
	struct gaudi3_nic_port *gaudi3_nic =
					container_of(work, struct gaudi3_nic_port, eq_work.work);
	struct hl_device *hdev = gaudi3_nic->hdev;

	nic_eq_handler(gaudi3_nic);

	/* In simulator we use the eq work to also handle the simulated interrupts.
	 * In this case we need to ack/clear the interrupts here.
	 */
	if (hdev->nic_poll_enable)
		schedule_delayed_work(&gaudi3_nic->eq_work, msecs_to_jiffies(1));
	else
		gaudi3_nic_eq_clr_interrupts(gaudi3_nic);
}

/* Use the following two routines when working with real HW */
static irqreturn_t gaudi3_nic_eq_threaded_isr(int irq, void *arg)
{
	struct gaudi3_nic_port *gaudi3_nic = arg;

	gaudi3_nic_eq_clr_interrupts(gaudi3_nic);
	nic_eq_handler(gaudi3_nic);

	return IRQ_HANDLED;
}

static irqreturn_t gaudi3_nic_eq_isr(int irq, void *arg)
{
	return IRQ_WAKE_THREAD;
}

static void gaudi3_nic_eq_hw_config(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_ring *ring = &gaudi3_nic->eq_ring;
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port = gaudi3_nic->nic_port->port, offset;

	/* set base address for event queue */
	offset = ELEMENT_OFFSET(port, QPC_EQ_NUM);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_EVENT_QUE_PI_ADDR_63_32_0,
			upper_32_bits(RING_PI_DMA_ADDRESS(ring)));

	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_EVENT_QUE_PI_ADDR_31_7_0,
			lower_32_bits(RING_PI_DMA_ADDRESS(ring)) >> 7);

	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_EVENT_QUE_BASE_ADDR_63_32_0,
			upper_32_bits(RING_BUF_DMA_ADDRESS(ring)));

	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_EVENT_QUE_BASE_ADDR_31_7_0,
			lower_32_bits(RING_BUF_DMA_ADDRESS(ring)) >> 7);

	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_EVENT_QUE_LOG_SIZE_0, ilog2(ring->count));

	gaudi3_nic_eq_reset_ring(gaudi3_nic);
}

static void gaudi3_nic_eq_hw_unconfig(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port = gaudi3_nic->nic_port->port, offset;

	offset = ELEMENT_OFFSET(port, QPC_EQ_NUM);
	NIC_OFFSET_RMWREG32(mmD0_NIC0_QPC_EVENT_QUE_CFG_0, 0, NIC_QPC_EVENT_QUE_CFG_ENABLE_M);
	NIC_OFFSET_RREG32(mmD0_NIC0_QPC_EVENT_QUE_CFG_0);
}

static void gaudi3_nic_eq_interrupts_enable_conditionally(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_port *nic_port = gaudi3_nic->nic_port;
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port = nic_port->port, offset;

	/* Disabling wire interrupts for EQ events, enable wire interrupts for EQ_ERR */
	NIC_RMWREG32(mmD0_NIC0_QPC_INTERRUPT_WIRE, 0, GAUDI3_NIC_EQ_INTERRUPT_M(port));
	NIC_RMWREG32(mmD0_NIC0_QPC_INTERRUPT_WIRE, 1, GAUDI3_NIC_EQ_ERR_INTERRUPT_M(port));

	/* No need here to synchronize the access to the shared registers as this code is called
	 * sequentially for each port and not simultaneously.
	 */

	if (hdev->nic_poll_enable) {
		/* Masking all QPC Interrupts leaving EQ int indication */
		NIC_RMWREG32(mmD0_NIC0_QPC_INTERRUPT_MSI, 0, GAUDI3_NIC_EQ_INTERRUPT_M(port));
		NIC_RMWREG32(mmD0_NIC0_QPC_INTERRUPT_MASK, 1, GAUDI3_NIC_EQ_INTERRUPT_M(port));
		NIC_RMWREG32(mmD0_NIC0_QPC_INTERRUPT_MASK, 1, GAUDI3_NIC_EQ_ERR_INTERRUPT_M(port));
	} else {
		offset = GAUDI3_NIC_EQ_INTERRUPT_S(port);

		NIC_OFFSET_WREG32(mmD0_NIC0_QPC_INTERRUPT_BASE_0,
					CFG_BAR_BASE - LBW_BASE + mmD0_PCIE_MSIX_BASE);

		NIC_OFFSET_WREG32(mmD0_NIC0_QPC_INTERRUPT_DATA_0,
					GAUDI3_IRQ_NUM_NIC_PORT_FIRST + port);

		/* Masking all QPC Interrupts except EQ int */
		NIC_RMWREG32(mmD0_NIC0_QPC_INTERRUPT_MASK, 0, GAUDI3_NIC_EQ_INTERRUPT_M(port));
		NIC_RMWREG32(mmD0_NIC0_QPC_INTERRUPT_MASK, 0, GAUDI3_NIC_EQ_ERR_INTERRUPT_M(port));
		NIC_RMWREG32(mmD0_NIC0_QPC_INTERRUPT_MSI, 1, GAUDI3_NIC_EQ_INTERRUPT_M(port));
	}

	/* flush */
	NIC_RREG32(mmD0_NIC0_QPC_INTERRUPT_MASK);
}

static void gaudi3_nic_eq_interrupts_disable(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_port *nic_port = gaudi3_nic->nic_port;
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port = nic_port->port;

	/* disabling and masking all QPC Interrupts */
	NIC_WREG32(mmD0_NIC0_QPC_INTERRUPT_WIRE,
		NIC_RREG32(mmD0_NIC0_QPC_INTERRUPT_WIRE) &
			~(GAUDI3_NIC_EQ_INTERRUPT_M(port) | GAUDI3_NIC_EQ_ERR_INTERRUPT_M(port)));
	NIC_WREG32(mmD0_NIC0_QPC_INTERRUPT_MSI,
		NIC_RREG32(mmD0_NIC0_QPC_INTERRUPT_MSI) & ~GAUDI3_NIC_EQ_INTERRUPT_M(port));

	NIC_WREG32(mmD0_NIC0_QPC_INTERRUPT_MASK,
		NIC_RREG32(mmD0_NIC0_QPC_INTERRUPT_MASK) | GAUDI3_NIC_EQ_INTERRUPT_M(port));

	/* flush */
	NIC_RREG32(mmD0_NIC0_QPC_INTERRUPT_MSI);
}

static int gaudi3_nic_eq_port_init(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_device *hdev = gaudi3_nic->hdev;

	gaudi3_nic_eq_hw_config(gaudi3_nic);

	/* we use an empty valid routine instead of a NULL ptr here in order
	 * to prevent a crash if a race occurs between the work-queue
	 * calling the handler routine and the eth driver unregistering it
	 * (which in the standard case results putting a NULL here)
	 * this way we also avoid using locks
	 */
	gaudi3_nic->eq_handler = nic_eq_null_handler;

	INIT_DELAYED_WORK(&gaudi3_nic->eq_work, gaudi3_nic_eq_work);

	gaudi3_nic_eq_interrupts_enable_conditionally(gaudi3_nic);

	if (hdev->nic_poll_enable)
		schedule_delayed_work(&gaudi3_nic->eq_work, msecs_to_jiffies(1));

	return 0;
}

static void gaudi3_nic_eq_port_fini(struct gaudi3_nic_port *gaudi3_nic)
{
	gaudi3_nic_eq_interrupts_disable(gaudi3_nic);
	cancel_delayed_work_sync(&gaudi3_nic->eq_work);
	gaudi3_nic->eq_handler = nic_eq_null_handler;
	gaudi3_nic_eq_hw_unconfig(gaudi3_nic);
}

int gaudi3_nic_eq_init(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_nic_port *gaudi3_nic;
	int rc, i, nics_init = 0;
	u32 port;

	/* SW-68430: Due to H/W bug on gaudi3, link events for both even and odd ports arrive
	 * only on the odd port in the macro. Therefore, need to initialize all EQs of all
	 * ports regardless of their enablement.
	 */

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++, nics_init++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		gaudi3_nic = &gaudi3->nic_ports[i];
		port = gaudi3_nic->nic_port->port;

		rc = gaudi3_nic_eq_port_init(gaudi3_nic);
		if (rc) {
			dev_err(hdev->dev, "Failed to init the hardware EQ, port: %d, %d\n",
				port, rc);
			goto err;
		}
	}

	return 0;

err:
	for (i = 0 ; i < nics_init ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		gaudi3_nic_eq_port_fini(&gaudi3->nic_ports[i]);
	}

	return rc;
}

void gaudi3_nic_eq_fini(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	int i;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		gaudi3_nic_eq_port_fini(&gaudi3->nic_ports[i]);
	}
}

void gaudi3_nic_eq_handler_register(struct gaudi3_nic_port *gaudi3_nic,
					gaudi3_nic_eq_handler eq_handler)
{
	struct hl_device *hdev = gaudi3_nic->hdev;

	if (gaudi3_nic->eq_handler != nic_eq_null_handler) {
		dev_err(hdev->dev, "EQ is already registered\n");
		return;
	}

	gaudi3_nic->eq_handler = eq_handler;

#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "Port %d registered to the ethernet EQ handler\n", gaudi3_nic->port);
#endif
}

void gaudi3_nic_eq_handler_unregister(struct gaudi3_nic_port *gaudi3_nic)
{
	gaudi3_nic->eq_handler = nic_eq_null_handler;

#if HL_NIC_DEBUG
	dev_dbg(gaudi3_nic->hdev->dev, "Port %d unregistered from the ethernet EQ handler\n",
		gaudi3_nic->port);
#endif
}

/*
 * event dispatcher
 *
 * In gaudi3 each NIC has a single EQ. The HW writes all the NIC related events
 * to this EQ. Since multiple applications can use the NIC at the same time we
 * need to have a way to dispatch the app-related events to the correct
 * application, these events will be read later on by the app using a dedicated
 * IOCTL added for this purpose.
 */

struct hl_nic_ev_dq *gaudi3_nic_eq_dispatcher_select_dq(
					struct hl_nic_port *nic_port,
					const struct hl_nic_eqe *eqe)
{
	struct gaudi3_nic_port *gaudi3_nic = (struct gaudi3_nic_port *)nic_port->nic_specific;
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
	struct hl_nic_ev_dq *dq = NULL;
#if HL_NIC_DEBUG
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port = nic_port->port;
#endif
	u32 event_type = EQE_TYPE(eqe);
	u32 cqn, qpn, dbn, ccqn;

	switch (event_type) {
	case EQE_COMP:
		fallthrough;
	case EQE_COMP_ERR:
		cqn = EQE_CQ_EVENT_CQ_NUM(eqe);
		dq = hl_nic_cqn_to_dq(ev_dqs, cqn, gaudi3_nic->hdev);
		break;
	case EQE_QP_ERR:
		qpn = EQE_QP_EVENT_QPN(eqe);
		dq = hl_nic_qpn_to_dq(ev_dqs, qpn);
		break;
	case EQE_RAW_TX_COMP:
		qpn = EQE_RAW_TX_EVENT_QPN(eqe);
		dq = hl_nic_qpn_to_dq(ev_dqs, qpn);
		break;
	case EQE_DB_FIFO_OVERRUN:
		dbn = EQE_DB_EVENT_DB_NUM(eqe);
		dq = hl_nic_dbn_to_dq(ev_dqs, dbn, gaudi3_nic->hdev);
		break;
	case EQE_CONG:
		ccqn = EQE_CQ_EVENT_CCQ_NUM(eqe);
		dq = hl_nic_ccqn_to_dq(ev_dqs, ccqn, gaudi3_nic->hdev);
		break;
	case EQE_WTD_SECURITY_ERR:
		qpn = EQE_QP_EVENT_QPN(eqe);
		dq = hl_nic_qpn_to_dq(ev_dqs, qpn);
		break;
	case EQE_NUMERICAL_ERR:
		qpn = EQE_QP_EVENT_QPN(eqe);
		dq = hl_nic_qpn_to_dq(ev_dqs, qpn);
		break;
	case EQE_CONG_ERR:
		fallthrough;
	case EQE_RESERVED_1:
		fallthrough;
	case EQE_RESERVED_2:
		fallthrough;
	default:
		dq = &ev_dqs->default_edq;
	}

#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "%s port %d\n", __func__, port);
#endif

	/* Unknown resources and events should be handled by default events
	 * dispatch queue.
	 */
	return IS_ERR_OR_NULL(dq) ? &ev_dqs->default_edq : dq;
}

int gaudi3_nic_eq_dispatcher_register_db(struct gaudi3_nic_port *gaudi3_nic,
					u32 asid, u32 dbn)
{
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port = gaudi3_nic->nic_port->port;

	/*TODO: add sanity check for privileged DB value. */
	if ((asid != HL_KERNEL_ASID_ID) && (dbn == GAUDI3_DB_FIFO_SECURE_HW_ID(port)))
		return -EINVAL;

	return hl_nic_eq_dispatcher_register_db(gaudi3_nic->nic_port, asid, dbn);
}
