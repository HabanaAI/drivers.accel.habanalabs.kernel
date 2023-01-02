// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi2_nic.h"
#include "../include/gaudi2/asic_reg/gaudi2_regs.h"
#include "../include/hw_ip/nic/nic_general.h"

#include <linux/pci.h>

#define GAUDI2_NIC_MAX_STRING_LEN	64

static const char
gaudi2_nic_eq_irq_name[NIC_NUMBER_OF_ENGINES][GAUDI2_NIC_MAX_STRING_LEN] = {
	"gaudi2 nic0 qpc0 EQ",
	"gaudi2 nic0 qpc1 EQ",
	"gaudi2 nic1 qpc0 EQ",
	"gaudi2 nic1 qpc1 EQ",
	"gaudi2 nic2 qpc0 EQ",
	"gaudi2 nic2 qpc1 EQ",
	"gaudi2 nic3 qpc0 EQ",
	"gaudi2 nic3 qpc1 EQ",
	"gaudi2 nic4 qpc0 EQ",
	"gaudi2 nic4 qpc1 EQ",
	"gaudi2 nic5 qpc0 EQ",
	"gaudi2 nic5 qpc1 EQ",
	"gaudi2 nic6 qpc0 EQ",
	"gaudi2 nic6 qpc1 EQ",
	"gaudi2 nic7 qpc0 EQ",
	"gaudi2 nic7 qpc1 EQ",
	"gaudi2 nic8 qpc0 EQ",
	"gaudi2 nic8 qpc1 EQ",
	"gaudi2 nic9 qpc0 EQ",
	"gaudi2 nic9 qpc1 EQ",
	"gaudi2 nic10 qpc0 EQ",
	"gaudi2 nic10 qpc1 EQ",
	"gaudi2 nic11 qpc0 EQ",
	"gaudi2 nic11 qpc1 EQ",
};

/*
 * Event queues for all the NICs are initialized ahead of NIC-specific
 * initialization regardless of NIC being enabled or not, we behave the same for
 * their IRQs.
 */
static irqreturn_t gaudi2_nic_eq_isr(int irq, void *arg);
static irqreturn_t gaudi2_nic_eq_threaded_isr(int irq, void *arg);

int gaudi2_nic_eq_request_irqs(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_en_core_info *core_info = &gaudi2->en_core_info;
	struct gaudi2_nic_port *gaudi2_nic;
	int i, rc, irq;

	/* IRQs should be allocated if polling activity is only temporal */
	if (hdev->nic_poll_enable && !core_info->temporal_polling)
		return 0;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		gaudi2_nic = &gaudi2->nic_ports[i];
		dev_dbg(hdev->dev, "Assigning MSI-X interrupt to port %d EQ\n", i);
		irq = pci_irq_vector(hdev->pdev, GAUDI2_IRQ_NUM_NIC_PORT_FIRST + i);
		rc = request_threaded_irq(irq,
					gaudi2_nic_eq_isr,
					gaudi2_nic_eq_threaded_isr,
					IRQF_ONESHOT,
					gaudi2_nic_eq_irq_name[i],
					gaudi2_nic);
		if (rc) {
			dev_err(hdev->dev, "Failed to request IRQ %d for port %d\n", irq, i);
			goto irq_fail;
		}
	}

	return 0;

irq_fail:
	for (i-- ; i >= 0 ; i--) {
		irq = pci_irq_vector(hdev->pdev, GAUDI2_IRQ_NUM_NIC_PORT_FIRST + i);
		free_irq(irq, gaudi2_nic);
	}
	return rc;
}

void gaudi2_nic_eq_sync_irqs(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_en_core_info *core_info = &gaudi2->en_core_info;
	int i, irq;

	/* Skip for simulator for which IRQs are not allocated */
	if (!hdev->pdev)
		return;

	/* IRQs are allocated if polling is temporal so return only if polling mode is constant */
	if (hdev->nic_poll_enable && !core_info->temporal_polling)
		return;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		dev_dbg(hdev->dev, "Going to sync MSI-X interrupt for port %d EQ\n", i);
		irq = pci_irq_vector(hdev->pdev, GAUDI2_IRQ_NUM_NIC_PORT_FIRST + i);
		synchronize_irq(irq);
	}
}

void gaudi2_nic_eq_free_irqs(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_en_core_info *core_info = &gaudi2->en_core_info;
	struct gaudi2_nic_port *gaudi2_nic;
	int i, irq;

	/* IRQs are allocated if polling is temporal so return only if polling mode is constant */
	if (hdev->nic_poll_enable && !core_info->temporal_polling)
		return;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		dev_dbg(hdev->dev, "Going to free MSI-X interrupt for port %d EQ\n", i);
		gaudi2_nic = &gaudi2->nic_ports[i];
		irq = pci_irq_vector(hdev->pdev, GAUDI2_IRQ_NUM_NIC_PORT_FIRST + i);
		free_irq(irq, gaudi2_nic);
	}
}

static void nic_eq_null_handler(struct gaudi2_nic_port *gaudi2_nic)
{

}

/* NIC HW per port link/lane status mask */
static u32 gaudi2_nic_get_link_status_mask(struct gaudi2_nic_port *gaudi2_nic)
{
	switch (gaudi2_nic->nic_port->speed) {
	case SPEED_50000:
		/* In 50GbE mode, HW supports upto 2 SERDES
		 * links per port.
		 * Note: SW uses fixed link 0 per port for
		 * transmission. Link 1 is unused.
		 */
		return 0x3;
	case SPEED_25000:
		fallthrough;
	case SPEED_100000:
		/* In 100GbE mode, HW supports only one
		 * SERDES link per port.
		 */
		return 0x1;
	default:
		dev_err(gaudi2_nic->hdev->dev, "Unsupported speed %d\n",
			gaudi2_nic->nic_port->speed);
	}

	return 0;
}

static void gaudi2_nic_link_event_handler(struct gaudi2_nic_port *gaudi2_nic)
{
	u32 curr_link_sts, link_sts_change, link_status_mask, port;
	struct hl_device *hdev = gaudi2_nic->hdev;
	struct gaudi2_nic_port *gaudi2_nic_curr;
	struct hl_nic_port *nic_port_curr;
	u8 l, nic_m, port_offset, port_shift;
	struct hl_nic_macro *nic_macro;
	struct gaudi2_device *gaudi2;
	struct hl_aux_dev *aux_dev;
	bool link_up, prev_link_up;

	gaudi2 = hdev->asic_specific;
	port = gaudi2_nic->nic_port->port;
	nic_macro = gaudi2_nic->nic_port->nic_macro;
	aux_dev = &hdev->nic.en_aux_dev;

	curr_link_sts = (NIC_MACRO_RREG32(mmPRT0_MAC_CORE_MAC_REC_STS0) &
			PRT0_MAC_CORE_MAC_REC_STS0_REC_LINK_STS_MASK) >>
			PRT0_MAC_CORE_MAC_REC_STS0_REC_LINK_STS_SHIFT;

	/* get the change on the serdes by XOR with previous val */
	link_sts_change = curr_link_sts ^ nic_macro->rec_link_sts;

	/* store current value as previous (for next round) */
	nic_macro->rec_link_sts = curr_link_sts;

	/* calc the NIC MACRO its link-change we need to handle */
	nic_m = port >> 1;

	/* Iterate all SERDES links and check which one was changed */
	for (l = 0 ; l < NIC_MAC_LANES ; l++) {
		if (!(link_sts_change & (1 << l)))
			continue;

		/* calc port offset from current link
		 * (2 ports per macro and 2 links per port)
		 */
		port_offset = l >> 1;
		port_shift = port_offset ? 2 : 0;

		/* get the port struct to handle its link according to the
		 * current SERDES link index
		 */
		gaudi2_nic_curr = &gaudi2->nic_ports[nic_m * 2 + port_offset];
		nic_port_curr = gaudi2_nic_curr->nic_port;

		mutex_lock(&nic_port_curr->control_lock);

		/* Skip in case the port is closed because the port_close method took care of
		 * disabling the carrier and stopping the queue.
		 */
		if (!hl_nic_is_port_open(nic_port_curr)) {
			mutex_unlock(&nic_port_curr->control_lock);
			continue;
		}

		link_status_mask = gaudi2_nic_get_link_status_mask(gaudi2_nic_curr);
		link_up = (curr_link_sts >> port_shift) & link_status_mask;
		prev_link_up = nic_port_curr->pcs_link;
		nic_port_curr->pcs_link = link_up;

		if (prev_link_up != link_up && !link_up) {
			mutex_lock(&gaudi2_nic_curr->qp_destroy_lock);

			if (gaudi2_nic_curr->qp_destroy_cnt && !nic_port_curr->mac_loopback) {
				nic_port_curr->mac_loopback = true;
				gaudi2_nic_hw_mac_loopback_cfg(gaudi2_nic_curr);
				gaudi2_nic_curr->qp_destroy_mac_lpbk = true;
			}

			mutex_unlock(&gaudi2_nic_curr->qp_destroy_lock);
		}

		if (!hdev->nic.phy_config_fw || nic_port_curr->phy_fw_tuned)
			hl_nic_phy_set_port_status(nic_port_curr, link_up);

		mutex_unlock(&nic_port_curr->control_lock);
	}
}

static void gaudi2_nic_eq_dispatcher_default_handler(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_nic_port *nic_port = gaudi2_nic->nic_port;
	struct hl_device *hdev = gaudi2_nic->hdev;
	u32 event_type, port, synd;
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
			synd = EQE_QP_EVENT_ERR_SYND(&eqe);
			dev_warn_ratelimited(hdev->dev, "Port-%d qp-err event: %d ,%s, for invalid QP:%d\n",
						port, synd, gaudi2_nic_qp_err_syndrom_to_str(synd),
						EQE_QP_EVENT_QPN(&eqe));
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
			gaudi2_nic->cong_q_err_cnt++;
			dev_dbg_ratelimited(hdev->dev, "Port-%d congestion error event\n", port);
			break;
		default:
			dev_warn_ratelimited(hdev->dev, "Port-%d unsupported event type: %d",
					port, event_type);
		}
	}

	mutex_unlock(&nic_port->control_lock);
}

static void nic_eq_handler(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_nic_port *nic_port = gaudi2_nic->nic_port;
	struct hl_device *hdev = gaudi2_nic->hdev;
	struct hl_nic_ring *eq_ring;
	struct hl_nic_eqe *eqe_p;
	u32 event_type, port;
	int rc;

	eq_ring = &gaudi2_nic->eq_ring;
	port = nic_port->port;

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
			gaudi2_nic_link_event_handler(gaudi2_nic);
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
			NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_CONSUMER_INDEX, eq_ring->ci_shadow);
		}
	}

	gaudi2_nic->eq_handler(gaudi2_nic);

	/* Handle unknown resources and events */
	gaudi2_nic_eq_dispatcher_default_handler(gaudi2_nic);
}

static inline void gaudi2_nic_eq_clr_interrupts(struct gaudi2_nic_port *gaudi2_nic)
{
	u32 port = gaudi2_nic->nic_port->port;
	struct hl_device *hdev = gaudi2_nic->hdev;

	/* Release the HW to allow more EQ interrupts.
	 * No need for interrupt masking. As long as the SW hasn't set the clear reg,
	 * new interrupts won't be raised
	 */
	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_CLR, 0x200);
}

static void gaudi2_nic_eq_work(struct work_struct *work)
{
	struct gaudi2_nic_port *gaudi2_nic =
					container_of(work, struct gaudi2_nic_port, eq_work.work);
	struct hl_device *hdev = gaudi2_nic->hdev;

	nic_eq_handler(gaudi2_nic);

	/* In simulator we use the eq work to also handle the simulated interrupts.
	 * In this case we need to ack/clear the interrupts here.
	 */
	if (hdev->nic_poll_enable)
		schedule_delayed_work(&gaudi2_nic->eq_work, msecs_to_jiffies(1));
	else
		gaudi2_nic_eq_clr_interrupts(gaudi2_nic);
}

/* Use the following two routines when working with real HW */
static irqreturn_t gaudi2_nic_eq_threaded_isr(int irq, void *arg)
{
	struct gaudi2_nic_port *gaudi2_nic = arg;

	gaudi2_nic_eq_clr_interrupts(gaudi2_nic);
	nic_eq_handler(gaudi2_nic);

	return IRQ_HANDLED;
}

static irqreturn_t gaudi2_nic_eq_isr(int irq, void *arg)
{
	return IRQ_WAKE_THREAD;
}

/* Use this routine when working in simulator */
irqreturn_t gaudi2_nic_eq_irq_handler(int irq, void *arg)
{
	struct gaudi2_nic_port *gaudi2_nic = arg;

	schedule_delayed_work(&gaudi2_nic->eq_work, 0);

	return IRQ_HANDLED;
}

static void gaudi2_nic_eq_hw_config(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_nic_ring *ring = &gaudi2_nic->eq_ring;
	struct hl_device *hdev = gaudi2_nic->hdev;
	u32 port = gaudi2_nic->nic_port->port;

	/* set base address for event queue */
	NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_PI_ADDR_63_32,
			upper_32_bits(RING_PI_DMA_ADDRESS(ring)));

	NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_PI_ADDR_31_7,
			lower_32_bits(RING_PI_DMA_ADDRESS(ring)) >> 7);

	NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_BASE_ADDR_63_32,
			upper_32_bits(RING_BUF_DMA_ADDRESS(ring)));

	NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_BASE_ADDR_31_7,
			lower_32_bits(RING_BUF_DMA_ADDRESS(ring)) >> 7);

	NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_LOG_SIZE, ilog2(ring->count));

	NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_WRITE_INDEX, 0);
	NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_PRODUCER_INDEX, 0);
	NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_CONSUMER_INDEX, 0);
	NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_CONSUMER_INDEX_CB, 0);

	NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_CFG,
			NIC0_QPC0_EVENT_QUE_CFG_INTERRUPT_PER_EQE_MASK |
			NIC0_QPC0_EVENT_QUE_CFG_WRITE_PI_EN_MASK |
			NIC0_QPC0_EVENT_QUE_CFG_ENABLE_MASK);

	NIC_WREG32(mmNIC0_QPC0_AXUSER_EV_QUE_LBW_INTR_HB_WR_OVRD_LO, 0xFFFFFBFF);
	NIC_WREG32(mmNIC0_QPC0_AXUSER_EV_QUE_LBW_INTR_HB_RD_OVRD_LO, 0xFFFFFBFF);

	/* reset SW indices */
	*((u32 *) RING_PI_ADDRESS(ring)) = 0;
	ring->pi_shadow = 0;
	ring->ci_shadow = 0;
	ring->rep_idx = 0;
}

static void gaudi2_nic_eq_interrupts_enable_conditionally(struct gaudi2_nic_port *gaudi2_nic,
							bool poll_enable)
{
	struct hl_nic_port *nic_port = gaudi2_nic->nic_port;
	struct hl_device *hdev = gaudi2_nic->hdev;
	u32 port = nic_port->port, sob_id;

	if (poll_enable) {
		/* Masking all QPC Interrupts except EQ wire int */
		NIC_WREG32(mmNIC0_QPC0_INTERRUPT_MASK, 0x3FF);
		NIC_WREG32(mmNIC0_QPC0_INTERRUPT_EN,
				NIC0_QPC0_INTERRUPT_EN_INTERRUPT10_WIRE_EN_MASK);
	} else {
		sob_id = GAUDI2_RESERVED_SOB_NIC_PORT_FIRST + port;
		NIC_WREG32(mmNIC0_QPC0_INTERRUPT_BASE_9,
				mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0 + sob_id * sizeof(u32));
		NIC_WREG32(mmNIC0_QPC0_INTERRUPT_DATA_9, GAUDI2_SOB_INCREMENT_BY_ONE);

		/* Masking all QPC Interrupts except EQ int and error event queue int */
		NIC_WREG32(mmNIC0_QPC0_INTERRUPT_MASK, 0x1FF);

		NIC_WREG32(mmNIC0_QPC0_INTERRUPT_EN, NIC0_QPC0_INTERRUPT_EN_INTERRUPT9_MSI_EN_MASK |
				NIC0_QPC0_INTERRUPT_EN_INTERRUPT10_WIRE_EN_MASK);
	}

	/* flush */
	NIC_RREG32(mmNIC0_QPC0_INTERRUPT_EN);
}

static void gaudi2_nic_eq_interrupts_disable(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_nic_port *nic_port = gaudi2_nic->nic_port;
	struct hl_device *hdev = gaudi2_nic->hdev;
	u32 port = nic_port->port;

	/* disabling and masking all QPC Interrupts */
	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_EN, 0);
	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_MASK, 0x7FF);

	/* flush */
	NIC_RREG32(mmNIC0_QPC0_INTERRUPT_EN);
}

void gaudi2_nic_eq_enter_temporal_polling_mode(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_nic_port *gaudi2_nic;
	int i;

	if (hdev->nic_poll_enable)
		return;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		dev_dbg(hdev->dev, "moving port %d EQ to polling mode\n", i);
		gaudi2_nic = &gaudi2->nic_ports[i];
		gaudi2_nic_eq_interrupts_enable_conditionally(gaudi2_nic, true);
	}

	hdev->nic_poll_enable = true;

	/* wait for ISRs to complete before scheduling the polling work */
	gaudi2_nic_eq_sync_irqs(hdev);

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		gaudi2_nic = &gaudi2->nic_ports[i];
		schedule_delayed_work(&gaudi2_nic->eq_work, msecs_to_jiffies(1));
	}
}

void gaudi2_nic_eq_exit_temporal_polling_mode(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_en_core_info *nic_info = &gaudi2->en_core_info;
	struct gaudi2_nic_port *gaudi2_nic;
	int i;

	if (!hdev->nic_poll_enable)
		return;

	if (!nic_info->temporal_polling)
		return;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		dev_dbg(hdev->dev, "moving port %d EQ to interrupt mode\n", i);
		gaudi2_nic = &gaudi2->nic_ports[i];
		cancel_delayed_work_sync(&gaudi2_nic->eq_work);
	}

	hdev->nic_poll_enable = false;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		gaudi2_nic = &gaudi2->nic_ports[i];
		gaudi2_nic_eq_interrupts_enable_conditionally(gaudi2_nic, false);
		/* Schedule the work as interrupts may be pending but not acked thus preventing
		 * interrupts from triggering.
		 * Double scheduling avoidance of the work (from the ISR and from here)
		 * is done by the WQ scheduler itself.
		 */
		schedule_delayed_work(&gaudi2_nic->eq_work, 0);
	}
}

static int gaudi2_nic_eq_port_init(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_device *hdev = gaudi2_nic->hdev;

	gaudi2_nic_eq_hw_config(gaudi2_nic);

	/* we use an empty valid routine instead of a NULL ptr here in order
	 * to prevent a crash if a race occurs between the work-queue
	 * calling the handler routine and the eth driver unregistering it
	 * (which in the standard case results putting a NULL here)
	 * this way we also avoid using locks
	 */
	gaudi2_nic->eq_handler = nic_eq_null_handler;

	INIT_DELAYED_WORK(&gaudi2_nic->eq_work, gaudi2_nic_eq_work);

	gaudi2_nic_eq_interrupts_enable_conditionally(gaudi2_nic, hdev->nic_poll_enable);

	if (hdev->nic_poll_enable) {
		dev_dbg(hdev->dev, "Going to use a workqueue for polling the NIC EQ\n");
		schedule_delayed_work(&gaudi2_nic->eq_work, msecs_to_jiffies(1));
	} else {
		dev_dbg(hdev->dev, "Going to enable MSI-X interrupt for NIC EQ\n");
	}

	return 0;
}

static void gaudi2_nic_eq_port_fini(struct gaudi2_nic_port *gaudi2_nic)
{
	gaudi2_nic_eq_interrupts_disable(gaudi2_nic);
	cancel_delayed_work_sync(&gaudi2_nic->eq_work);
	gaudi2_nic->eq_handler = nic_eq_null_handler;
}

int gaudi2_nic_eq_init(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_en_core_info *core_info = &gaudi2->en_core_info;
	struct gaudi2_nic_port *gaudi2_nic;
	int rc, i, nics_init = 0;
	u32 port;

	/* Need to reset the value of 'nic_poll_enable' for a case that we entered temporal polling
	 * mode but didn't exit it (e.g. during a failing soft-reset).
	 * The original value is actually the inverse of 'temporal_polling' which is set once in
	 * sw_init and is constant.
	 */
	hdev->nic_poll_enable = !core_info->temporal_polling;

	/* SW-42306: Due to H/W bug on gaudi2, link events for both even and odd ports arrive
	 * only on the odd port in the macro. Therefore, need to initialize all EQs of all
	 * ports regardless of their enablement.
	 */
	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++, nics_init++) {
		gaudi2_nic = &gaudi2->nic_ports[i];
		port = gaudi2_nic->nic_port->port;

		rc = gaudi2_nic_eq_port_init(gaudi2_nic);
		if (rc) {
			dev_err(hdev->dev, "Failed to init the hardware EQ, port: %d, %d\n",
				port, rc);
			goto err;
		}
	}

	return 0;

err:
	for (i = 0 ; i < nics_init ; i++)
		gaudi2_nic_eq_port_fini(&gaudi2->nic_ports[i]);

	return rc;
}

void gaudi2_nic_eq_fini(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	int i;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++)
		gaudi2_nic_eq_port_fini(&gaudi2->nic_ports[i]);
}

void gaudi2_nic_eq_handler_register(struct gaudi2_nic_port *gaudi2_nic,
					gaudi2_nic_eq_handler eq_handler)
{
	struct hl_device *hdev = gaudi2_nic->hdev;

	if (gaudi2_nic->eq_handler != nic_eq_null_handler) {
		dev_err(hdev->dev, "EQ is already registered\n");
		return;
	}

	gaudi2_nic->eq_handler = eq_handler;
#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "Port %d registered to the ethernet EQ handler\n", gaudi2_nic->port);
#endif
}

void gaudi2_nic_eq_handler_unregister(struct gaudi2_nic_port *gaudi2_nic)
{
	gaudi2_nic->eq_handler = nic_eq_null_handler;
#if HL_NIC_DEBUG
	dev_dbg(gaudi2_nic->hdev->dev, "Port %d unregistered from the ethernet EQ handler\n",
		gaudi2_nic->port);
#endif
}

/*
 * event dispatcher
 *
 * In gaudi2 each NIC has a single EQ. The HW writes all the NIC related events
 * to this EQ. Since multiple applications can use the NIC at the same time we
 * need to have a way to dispatch the app-related events to the correct
 * application, these events will be read later on by the app using a dedicated
 * IOCTL added for this purpose.
 */

struct hl_nic_ev_dq *gaudi2_nic_eq_dispatcher_select_dq(
					struct hl_nic_port *nic_port,
					const struct hl_nic_eqe *eqe)
{
	struct gaudi2_nic_port *gaudi2_nic = (struct gaudi2_nic_port *)nic_port->nic_specific;
	struct hl_nic_ev_dqs *ev_dqs = &nic_port->ev_dqs;
	struct hl_nic_ev_dq *dq = NULL;
#if HL_NIC_DEBUG
	struct hl_device *hdev = gaudi2_nic->hdev;
	u32 port = nic_port->port;
#endif
	u32 event_type = EQE_TYPE(eqe);
	u32 cqn, qpn, dbn, ccqn;

	switch (event_type) {
	case EQE_COMP:
		fallthrough;
	case EQE_COMP_ERR:
		cqn = EQE_CQ_EVENT_CQ_NUM(eqe);
		dq = hl_nic_cqn_to_dq(ev_dqs, cqn, gaudi2_nic->hdev);
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
		dq = hl_nic_dbn_to_dq(ev_dqs, dbn, gaudi2_nic->hdev);
		break;
	case EQE_CONG:
		ccqn = EQE_CQ_EVENT_CCQ_NUM(eqe);
		dq = hl_nic_ccqn_to_dq(ev_dqs, ccqn, gaudi2_nic->hdev);
		break;
	case EQE_CONG_ERR:
		fallthrough;
	case EQE_RESERVED:
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

int gaudi2_nic_eq_dispatcher_register_db(struct gaudi2_nic_port *gaudi2_nic,
					u32 asid, u32 dbn)
{

	if (dbn == GAUDI2_DB_FIFO_PRIVILEGE_HW_ID)
		return -EINVAL;

	if ((asid != HL_KERNEL_ASID_ID) && (dbn == GAUDI2_DB_FIFO_SECURE_HW_ID))
		return -EINVAL;

	return hl_nic_eq_dispatcher_register_db(gaudi2_nic->nic_port, asid, dbn);
}
