// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi2_nic.h"
#include "../include/gaudi2/asic_reg/gaudi2_regs.h"
#include "../include/hw_ip/nic/nic_general.h"
#include <linux/pci.h>
#include <linux/circ_buf.h>

#define GAUDI2_NIC_WTD_BP_UPPER_TH_DIFF		4
#define GAUDI2_NIC_WTD_BP_LOWER_TH_DIFF		8
#define GAUDI2_NIC_MTU_DEFAULT			SZ_8K /* 8KB */
#define QPC_SANITY_CHECK_INTERVAL_MS		1 /* 1 msec */
#define ACCUMULATE_FEC_STATS_DURATION_MS	100
#define NIC_TMR_TIMEOUT_US			10000 /* 10 msec */
#define NIC_TMR_TIMEOUT_PLDM_US			1000 /* 1 msec */
#define PERF_BW_DIV				1000000

#define IPv4_PROTOCOL_UDP			17
#define DUMMY_UDP_PORT				8224
#define GAUDI2_USER_ENCAP_ID			0

#define GAUDI2_PFC_PRIO_DRIVER			0
#define GAUDI2_PFC_PRIO_USER_BASE		1

#define GAUDI2_NIC_MAX_CONG_WND			(1 << 23)
#define GAUDI2_NIC_MAX_SPEED			SPEED_100000

#define RETRY_COUNT_QPC_SANITY			10

/* This static assert guarantees that we can't overflow the PSN window within a timer period,
 * meaning that it needs to ensure that the time it takes to transmit the total amount of bits of
 * all packets is greater than the timeout value. note that we take the "worst case" values, i.e.
 * min MTU and max speed.
 *
 * MAX_CONG_WND * MIN_MTU [bits]
 * ----------------------------- > TIMEOUT [s]
 *      SPEED [bits/s]
 *
 * ==>
 *
 * GAUDI2_NIC_MAX_CONG_WND * (NIC_RAW_MIN_MTU [bytes] * BITS_PER_BYTE)   NIC_TMR_TIMEOUT_US [usec]
 * ------------------------------------------------------------------- > -------------------------
 *                 GAUDI2_NIC_MAX_SPEED [Mbits/sec] * 1M                            1M
 *
 * ==>
 *
 * GAUDI2_NIC_MAX_CONG_WND * (NIC_RAW_MIN_MTU * BITS_PER_BYTE) >
 *    NIC_TMR_TIMEOUT_US * GAUDI2_NIC_MAX_SPEED
 */
static_assert((((u64) GAUDI2_NIC_MAX_CONG_WND) * ((u64) (NIC_RAW_MIN_MTU * BITS_PER_BYTE))) >
		(((u64) NIC_TMR_TIMEOUT_US) * ((u64) GAUDI2_NIC_MAX_SPEED)));

/* Actual mask used by HW is smaller than the one declared in
 * NIC0_QPC0_WQ_BP_2ARC_ADDR_VAL_MASK and NIC0_QPC0_WQ_BP_2QMAN_ADDR_VAL_MASK
 */
#define WQ_BP_ADDR_VAL_MASK			0x7FFFFFF

/* User encapsulation 32 bit register offset. */
#define encap_offset(id) (id * 4)

/* We have fixed mapping between SW and HW IDs. */
#define db_fifo_hw_id(id) ((id) - 1)

/* User doorbell fifo 32 bit register offset. */
#define db_fifo_offset(id) (db_fifo_hw_id(id) * 4)

/* User doorbell fifo 64 bit register offset. */
#define db_fifo_offset64(id) (db_fifo_hw_id(id) * 8)

static int gaudi2_nic_ctx_init(struct hl_ctx *ctx);
static void gaudi2_nic_ctx_fini(struct hl_ctx *ctx);
static bool gaudi2_nic_get_hw_cap(struct hl_device *hdev);
static void gaudi2_qp_sanity_fini(struct gaudi2_nic_port *gaudi2_nic);
static int gaudi2_qp_sanity_init(struct gaudi2_nic_port *gaudi2_nic);
static void gaudi2_get_default_encap_id(struct hl_nic_port *nic_port, u32 *id);
static int gaudi2_encap_set(struct hl_nic_port *nic_port, u32 encap_id,
				struct hl_nic_encap_idr_pdata *idr_pdata);

enum gaudi2_nic_user_bp_offs {
	HL_NIC_USER_BP_OFFS_FW,
	HL_NIC_USER_BP_OFFS_QMAN
};

struct gaudi2_nic_stat {
	char str[ETH_GSTRING_LEN];
};

static struct gaudi2_nic_stat gaudi2_nic_err_stats[] = {
	{"Congestion Q err"},
	{"Eth DB fifo overrun"}
};

/* Gaudi2 FEC (Fwd Error Correction) Stats */
static struct gaudi2_nic_stat gaudi2_nic_mac_fec_stats[] = {
	{"cw_corrected_accum"},
	{"cw_uncorrect_accum"},
	{"cw_corrected"},
	{"cw_uncorrect"},
	{"symbol_err_corrected_lane_0"},
	{"symbol_err_corrected_lane_1"},
	{"symbol_err_corrected_lane_2"},
	{"symbol_err_corrected_lane_3"},
	{"pre_FEC_SER_int"},
	{"pre_FEC_SER_exp (negative)"},
	{"post_FEC_SER_int"},
	{"post_FEC_SER_exp (negative)"},
};

/* Gaudi2 performance Stats */
static struct gaudi2_nic_stat gaudi2_nic_perf_stats[] = {
	{"bandwidth_gbps_int"},
	{"bandwidth_gbps_frac"},
	{"last_data_latency_usec_int"},
	{"last_data_latency_usec_frac"},
};

static size_t gaudi2_nic_err_stats_len = ARRAY_SIZE(gaudi2_nic_err_stats);
static size_t gaudi2_nic_mac_fec_stats_len = ARRAY_SIZE(gaudi2_nic_mac_fec_stats);
static size_t gaudi2_nic_perf_stats_len = ARRAY_SIZE(gaudi2_nic_perf_stats);

#define GAUDI2_SYNDROME_TYPE(syndrome)	(((syndrome) >> 6) & 0x3)
#define GAUDI2_MAX_SYNDROM_STRING_LEN	256
#define GAUDI2_MAX_SYNDROME_TYPE	3

static char qp_syndroms[NIC_MAX_QP_ERR_SYNDROMS][GAUDI2_MAX_SYNDROM_STRING_LEN] = {
	/* Rx packet errors*/
	[0x1]  = "[RX] pkt err, pkt bad format",
	[0x2]  = "[RX] pkt err, pkt tunnel invalid",
	[0x3]  = "[RX] pkt err, BTH opcode invalid",
	[0x4]  = "[RX] pkt err, syndrome invalid",
	[0x5]  = "[RX] pkt err, Reliable QP max size invalid",
	[0x6]  = "[RX] pkt err, Reliable QP min size invalid",
	[0x7]  = "[RX] pkt err, Raw min size invalid",
	[0x8]  = "[RX] pkt err, Raw max size invalid",
	[0x9]  = "[RX] pkt err, QP invalid",
	[0xa]  = "[RX] pkt err, Transport Service mismatch",
	[0xb]  = "[RX] pkt err, QPC Requester QP state invalid",
	[0xc]  = "[RX] pkt err, QPC Responder QP state invalid",
	[0xd]  = "[RX] pkt err, QPC Responder resync invalid",
	[0xe]  = "[RX] pkt err, QPC Requester PSN invalid",
	[0xf]  = "[RX] pkt err, QPC Requester PSN unset",
	[0x10] = "[RX] pkt err, QPC Responder RKEY invalid",
	[0x11] = "[RX] pkt err, WQE index mismatch",
	[0x12] = "[RX] pkt err, WQE write opcode invalid",
	[0x13] = "[RX] pkt err, WQE Rendezvous opcode invalid",
	[0x14] = "[RX] pkt err, WQE Read  opcode invalid",
	[0x15] = "[RX] pkt err, WQE Write Zero",
	[0x16] = "[RX] pkt err, WQE multi zero",
	[0x17] = "[RX] pkt err, WQE Write send big",
	[0x18] = "[RX] pkt err, WQE multi big",

	/* QPC errors */
	[0x40] = "[qpc] [TMR] max-retry-cnt exceeded",
	[0x41] = "[qpc] [req DB] QP not valid",
	[0x42] = "[qpc] [req DB] security check",
	[0x43] = "[qpc] [req DB] PI > last-index",
	[0x44] = "[qpc] [req DB] wq-type is READ",
	[0x45] = "[qpc] [req TX] QP not valid",
	[0x46] = "[qpc] [req TX] Rendezvous WQE but wq-type is not WRITE",
	[0x47] = "[qpc] [req RX] QP not valid",
	[0x48] = "[qpc] [req RX] max-retry-cnt exceeded",
	[0x49] = "[qpc] [req RDV] QP not valid",
	[0x4a] = "[qpc] [req RDV] wrong wq-type",
	[0x4b] = "[qpc] [req RDV] PI > last-index",
	[0x4c] = "[qpc] [res TX] QP not valid",
	[0x4d] = "[qpc] [res RX] QP not valid",

	/* tx packet error */
	[0x80] = "[TX] pkt error, QPC.wq_type is write does not support WQE.opcode",
	[0x81] = "[TX] pkt error, QPC.wq_type is rendezvous does not support WQE.opcode",
	[0x82] = "[TX] pkt error, QPC.wq_type is read does not support WQE.opcode",
	[0x83] = "[TX] pkt error, QPC.gaudi1 is set does not support WQE.opcode",
	[0x84] = "[TX] pkt error, WQE.opcode is write but WQE.size is 0",
	[0x85] =
		"[TX] pkt error, WQE.opcode is multi-stride|local-stride|multi-dual but WQE.size is 0",
	[0x86] = "[TX] pkt error, WQE.opcode is send but WQE.size is 0",
	[0x87] = "[TX] pkt error, WQE.opcode is rendezvous-write|rendezvous-read but WQE.size is 0",
	[0x88] = "[TX] pkt error, WQE.opcode is write but size > configured max-write-send-size",
	[0x89] =
		"[TX] pkt error, WQE.opcode is multi-stride|local-stride|multi-dual but size > configured max-stride-size",
	[0x8a] =
		"[TX] pkt error, WQE.opcode is rendezvous-write|rendezvous-read but QPC.remote_wq_log_size <= configured min-remote-log-size",
	[0x8b] =
		"[TX] pkt error, WQE.opcode is rendezvous-write but WQE.size != configured rdv-wqe-size (per granularity)",
	[0x8c] =
		"[TX] pkt error, WQE.opcode is rendezvous-read but WQE.size != configured rdv-wqe-size (per granularity)",
	[0x8d] =
		"[TX] pkt error, WQE.inline is set but WQE.size != configured inline-wqe-size (per granularity)",
	[0x8e] = "[TX] pkt error, QPC.gaudi1 is set but WQE.inline is set",
	[0x8f] =
		"[TX] pkt error, WQE.opcode is multi-stride|local-stride|multi-dual but QPC.swq_granularity is 0",
	[0x90] = "[TX] pkt error, WQE.opcode != NOP but WQE.reserved0 != 0",
	[0x91] = "[TX] pkt error, WQE.opcode != NOP but WQE.wqe_index != execution-index [7.0]",
	[0x92] =
		"[TX] pkt error, WQE.opcode is multi-stride|local-stride|multi-dual but WQE.size < stride-size",
	[0x93] =
		"[TX] pkt error, WQE.reduction_opcode is upscale but WQE.remote_address LSB is not 0",
	[0x94] = "[TX] pkt error, WQE.reduction_opcode is upscale but does not support WQE.opcode",
	[0x95] = "[TX] pkt error, RAW packet but WQE.size not supported",
	[0xB0] = "WQE.opcode is QoS but WQE.inline is set",
	[0xB1] = "WQE.opcode above 15",
	[0xB2] = "RAW above MIN",
	[0xB3] = "RAW below MAX",
	[0xB4] = "WQE.reduction is disable but reduction-opcode is not 0",
	[0xB5] = "WQE.opcode is READ-RDV but WQE.inline is set",
	[0xB6] = "WQE fetch WR size not 4",
	[0xB7] = "WQE fetch WR addr not mod4",
	[0xB8] = "RDV last-index",
	[0xB9] = "Gaudi1 multi-dual",
	[0xBA] = "WQE bad opcode",
	[0xBB] = "WQE bad size",
	[0xBC] = "WQE SE not RAW",
	[0xBD] = "Gaudi1 tunnal",
	[0xBE] = "Tunnel 0-size",
	[0xBF] = "Tunnel max size",
};

char *gaudi2_nic_qp_err_syndrom_to_str(u32 syndrome)
{
	int syndrome_type;
	char *str;

	/* syndrome comprised from 8 bits
	 * [2:type, 6:syndrome]
	 * 6 bits for syndrome
	 * 2 bits for type
	 *   0 - rx packet error
	 *   1 - qp error
	 *   2 - tx packet error
	 */

	if (syndrome >= NIC_MAX_QP_ERR_SYNDROMS)
		return "syndrome unknown";

	syndrome_type = GAUDI2_SYNDROME_TYPE(syndrome);

	str = qp_syndroms[syndrome];
	if (strlen(str))
		return str;

	switch (syndrome_type) {
	case 0:
		str = "RX packet syndrome unknown";
		break;
	case 1:
		str = "QPC syndrome unknown";
		break;
	case 2:
		str = "TX packet syndrome unknown";
		break;
	default:
		str = "syndrome unknown";
		break;
	}

	return str;
}

bool gaudi2_nic_is_macro_enabled(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	u32 port1, port2;

	port1 = nic_macro->idx << 1; /* the index of the first port in the macro */
	port2 = port1 + 1;

	return (hdev->nic_ports_mask & BIT(port1)) || (hdev->nic_ports_mask & BIT(port2));
}

static void db_fifo_toggle_err_evt(struct hl_nic_port *nic_port, bool enable)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	u32 mask = NIC0_QPC0_REQ_STATIC_CONFIG_QM_PUSH_TO_ERR_FIFO_NON_V_MASK |
		NIC0_QPC0_REQ_STATIC_CONFIG_QM_PUSH_ERR_PI_EX_LAST_MASK;
	u32 val = enable ? (mask >> __ffs(mask)) : 0;

	NIC_RMWREG32_SHIFTED(mmNIC0_QPC0_REQ_STATIC_CONFIG, val, mask);
}

static void __gaudi2_nic_get_db_fifo_umr(struct hl_nic_port *nic_port, u32 block_id, u32 offset_id,
						u64 *umr_block_addr, u32 *umr_db_offset)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, odd_db_offset;

	odd_db_offset = mmNIC0_UMR0_0_UNSECURE_DOORBELL1_UNSECURE_DB_FIRST32 -
			mmNIC0_UMR0_0_UNSECURE_DOORBELL0_UNSECURE_DB_FIRST32;

	/* UMR base address we map to userspace */
	*umr_block_addr = CFG_BASE + NIC_CFG_BASE(port, mmNIC0_UMR0_0_UNSECURE_DOORBELL0_BASE) +
				mmNIC0_UMR0_0_UNSECURE_DOORBELL0_BASE + (block_id * NIC_UMR_OFFSET);

	/* Each UMR block hosts 2 doorbell fifos. Get byte offset. */
	*umr_db_offset = (offset_id & 1) ? odd_db_offset : 0;
}

static void gaudi2_nic_get_db_fifo_umr(struct hl_nic_port *nic_port, u32 id, u64 *umr_block_addr,
					u32 *umr_db_offset)
{
	__gaudi2_nic_get_db_fifo_umr(nic_port, db_fifo_hw_id(id) / 2, db_fifo_hw_id(id),
					umr_block_addr, umr_db_offset);
}

static void db_fifo_push_dummy(struct hl_nic_port *nic_port, u32 id, int n_dummy, bool is_eth)
{
	u32 port, umr_db_offset, offset_0_31, offset_32_64, db_dummy[2];
	struct hl_device *hdev = nic_port->hdev;
	struct gaudi2_device *gaudi2;
	u64 umr_block_addr;
	int i;

	gaudi2 = hdev->asic_specific;
	port = nic_port->port;

	/* 8 bytes dummy doorbell packet with unused QP ID. */
	db_dummy[0] = 0;
	db_dummy[1] = NIC_MAX_QP_NUM;

	/* Get DB fifo offset in register configuration space. */
	gaudi2_nic_get_db_fifo_umr(nic_port, id, &umr_block_addr, &umr_db_offset);
	offset_0_31 = umr_block_addr - CFG_BASE + umr_db_offset;
	offset_32_64 = offset_0_31 +
			(mmNIC0_UMR0_0_UNSECURE_DOORBELL0_UNSECURE_DB_SECOND32 -
			mmNIC0_UMR0_0_UNSECURE_DOORBELL0_UNSECURE_DB_FIRST32);

	/* Split user doorbell fifo packets to fit 32 bit registers. */
	for (i = 0 ; i < n_dummy ; i++) {
		if (is_eth) {
			NIC_WREG32(mmNIC0_QPC0_SECURED_DB_FIRST32, db_dummy[0]);
			NIC_WREG32(mmNIC0_QPC0_SECURED_DB_SECOND32, db_dummy[1]);
		} else {
			WREG32(offset_0_31, db_dummy[0]);
			WREG32(offset_32_64, db_dummy[1]);
		}
	}

	/* read is needed by simulator to flush small writes */
	if (gaudi2->flush_db_fifo) {
		if (is_eth)
			NIC_RREG32(mmNIC0_QPC0_SECURED_DB_FIRST32);
		else
			RREG32(offset_0_31);
	}
}

/*
 * Doorbell fifo H/W bug. There is no provision for S/W to reset H/W CI.
 * Hence, we implement workaround. Push dummy doorbells to db fifos till
 * CI wraps.
 */
static void __db_fifo_reset(struct hl_nic_port *nic_port, u32 *ci_cpu_addr, u32 id, bool is_eth)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 ci = *ci_cpu_addr, port;
	int rc;

	/* no need to push dummy doorbells, as the hard will reset itself. However, reset the memory
	 * where the last CI is stored at.
	 */
	if (nic_port->hdev->reset_info.hard_reset_pending) {
		*ci_cpu_addr = 0;
		return;
	}

	port = nic_port->port;

	/*
	 * Stop HW from throwing below error events.
	 * 1. Requester DB, invalid QP
	 * 2. Requester DB, PI > last WQ index
	 *
	 * Dummy doorbell piggybacks on the idea that HW updates CI
	 * even for invalid doorbells. However, EQ error events are
	 * generated and fifo is pushed to error state.
	 */
	db_fifo_toggle_err_evt(nic_port, false);

	/*
	 * 1. Another User doorbell fifo HW bug. CI updated by HW is one
	 * less than number of doorbells pushed.
	 * 2. We cannot assert if user has pushed any doorbells. i.e
	 * CI buffer is default 0 or HW updated zero.
	 *
	 * To handle above scenario we push 2 dummy doorbells if CI is zero.
	 * This forces HW to update CI buffer. Hence, ensuring we are not
	 * dealing with default zero memory.
	 * Note, driver ensures CI buffer is zeroed out before passing it on to HW.
	 */
	if (!ci) {
		db_fifo_push_dummy(nic_port, id, 2, is_eth);

		rc = hl_poll_timeout_memory(hdev, ci_cpu_addr, ci,
			ci, 10, HL_DEVICE_TIMEOUT_USEC, true);
		if (rc && !ci)
			dev_err(hdev->dev, "Doorbell fifo reset timed out\n");
	}

	/* Push dummy doorbells such that HW CI points to fifo base. */
	db_fifo_push_dummy(nic_port, id, NIC_FIFO_DB_SIZE - ci - 1, is_eth);

	/* Wait for HW to absorb dummy doorbells and update CI. */
	rc = hl_poll_timeout_memory(hdev, ci_cpu_addr, ci,
		ci == (NIC_FIFO_DB_SIZE - 1), 10, HL_DEVICE_TIMEOUT_USEC, true);
	if (rc && (ci != (NIC_FIFO_DB_SIZE - 1)))
		dev_err(hdev->dev, "Doorbell fifo reset timed out, ci: %d\n", ci);

	db_fifo_toggle_err_evt(nic_port, true);

	/* Zero out HW CI buffer address register for added safety. */
	if (is_eth) {
		NIC_WREG32(mmNIC0_QPC0_DBFIFOSECUR_CI_UPD_ADDR_DBFIFO_CI_UPD_ADDR_31_7, 0);
		NIC_WREG32(mmNIC0_QPC0_DBFIFOSECUR_CI_UPD_ADDR_DBFIFO_CI_UPD_ADDR_63_32, 0);
	} else {
		NIC_WREG32(mmNIC0_QPC0_DBFIFO0_CI_UPD_ADDR_DBFIFO_CI_UPD_ADDR_31_7 +
				db_fifo_offset64(id), 0);
		NIC_WREG32(mmNIC0_QPC0_DBFIFO0_CI_UPD_ADDR_DBFIFO_CI_UPD_ADDR_63_32 +
				db_fifo_offset64(id), 0);
	}
}

static void gaudi2_nic_db_fifo_reset(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];
	struct gaudi2_nic_port *gaudi2_nic;
	u32 *ci_cpu_addr;

	gaudi2_nic = nic_port->nic_specific;
	ci_cpu_addr = (u32 *) RING_BUF_ADDRESS(&gaudi2_nic->fifo_ring);

	gaudi2_nic->db_fifo_pi = 0;

	__db_fifo_reset(nic_port, ci_cpu_addr, 0, true);
}

static int gaudi2_nic_alloc_ring(struct gaudi2_nic_port *gaudi2_nic, struct hl_nic_ring *ring,
					int elem_size, int count)
{
	struct hl_device *hdev = gaudi2_nic->hdev;
	int rc;

	ring->count = count;
	ring->elem_size = elem_size;
	ring->asid = HL_KERNEL_ASID_ID;

	RING_BUF_SIZE(ring) = elem_size * count;
	RING_BUF_ADDRESS(ring) = hl_asic_dma_alloc_coherent(hdev, RING_BUF_SIZE(ring),
								&RING_BUF_DMA_ADDRESS(ring),
								GFP_KERNEL);
	if (!RING_BUF_ADDRESS(ring))
		return -ENOMEM;

	/* ring's idx_ptr shall point on pi/ci address */
	RING_PI_SIZE(ring) = sizeof(u64);
	RING_PI_ADDRESS(ring) = hl_asic_dma_pool_zalloc(hdev, RING_PI_SIZE(ring),
								GFP_KERNEL | __GFP_ZERO,
								&RING_PI_DMA_ADDRESS(ring));
	if (!RING_PI_ADDRESS(ring)) {
		hl_asic_dma_free_coherent(hdev, RING_BUF_SIZE(ring), RING_BUF_ADDRESS(ring),
						RING_BUF_DMA_ADDRESS(ring));
		rc = -ENOMEM;
		goto pi_alloc_fail;
	}

	return 0;

pi_alloc_fail:
	hl_asic_dma_free_coherent(hdev, RING_BUF_SIZE(ring), RING_BUF_ADDRESS(ring),
					RING_BUF_DMA_ADDRESS(ring));

	return rc;
}

static void gaudi2_nic_free_ring(struct gaudi2_nic_port *gaudi2_nic, struct hl_nic_ring *ring)
{
	struct hl_device *hdev = gaudi2_nic->hdev;

	hl_asic_dma_pool_free(hdev, RING_PI_ADDRESS(ring), RING_PI_DMA_ADDRESS(ring));

	hl_asic_dma_free_coherent(hdev, RING_BUF_SIZE(ring), RING_BUF_ADDRESS(ring),
					RING_BUF_DMA_ADDRESS(ring));
}

static int gaudi2_nic_alloc_cq_rings(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_device *hdev = gaudi2_nic->hdev;
	struct hl_nic_ring *ring;
	u32 elem_size, queue_size, total_queues_size, count;
	void *cpu_addr;
	dma_addr_t dma_addr;
	int rc, i;

	elem_size = sizeof(struct gaudi2_cqe);
	count = NIC_CQ_MAX_ENTRIES;
	queue_size = elem_size * count;
	total_queues_size = queue_size * GAUDI2_NIC_MAX_CQS_NUM;

	/*
	 * The HW expects that all CQs will be located in a physically consecutive memory one after
	 * the other. Hence we allocate all of them in one chunk.
	 */
	cpu_addr = hl_asic_dma_alloc_coherent(hdev, total_queues_size, &dma_addr, GFP_KERNEL);
	if (!cpu_addr)
		return -ENOMEM;

	for (i = 0 ; i < NIC_CQS_NUM ; i++) {
		ring = &gaudi2_nic->cq_rings[i];
		RING_BUF_ADDRESS(ring) = cpu_addr + i * queue_size;
		RING_BUF_DMA_ADDRESS(ring) = dma_addr + i * queue_size;
		/* prevent freeing memory fragments by individual Qs */
		RING_BUF_SIZE(ring) = i ? 0 : total_queues_size;
		ring->count = count;
		ring->elem_size = elem_size;
		ring->asid = HL_KERNEL_ASID_ID;
	}

	for (i = 0 ; i < NIC_CQS_NUM ; i++) {
		ring = &gaudi2_nic->cq_rings[i];
		RING_PI_SIZE(ring) = sizeof(u64);
		RING_PI_ADDRESS(ring) = hl_asic_dma_pool_zalloc(hdev, RING_PI_SIZE(ring),
									GFP_KERNEL | __GFP_ZERO,
									&RING_PI_DMA_ADDRESS(ring));
		if (!RING_PI_ADDRESS(ring)) {
			rc = -ENOMEM;
			goto err;
		}
	}

	return 0;

err:
	/* free the allocated rings indices */
	for (--i ; i >= 0 ; i--) {
		ring = &gaudi2_nic->cq_rings[i];
		hl_asic_dma_pool_free(hdev, RING_PI_ADDRESS(ring), RING_PI_DMA_ADDRESS(ring));
	}

	/* free rings memory */
	hl_asic_dma_free_coherent(hdev, total_queues_size, cpu_addr, dma_addr);

	return rc;
}

static void gaudi2_nic_free_cq_rings(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_device *hdev = gaudi2_nic->hdev;
	struct hl_nic_ring *ring;
	int i;

	for (i = 0 ; i < NIC_CQS_NUM ; i++) {
		ring = &gaudi2_nic->cq_rings[i];
		hl_asic_dma_pool_free(hdev, RING_PI_ADDRESS(ring), RING_PI_DMA_ADDRESS(ring));
	}

	/* the entire CQs memory is allocated as one chunk and stored at index 0 */
	ring = &gaudi2_nic->cq_rings[0];
	hl_asic_dma_free_coherent(hdev, RING_BUF_SIZE(ring), RING_BUF_ADDRESS(ring),
					RING_BUF_DMA_ADDRESS(ring));
}

static int gaudi2_nic_alloc_rings_resources(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_device *hdev = gaudi2_nic->hdev;
	int rc;

	rc = gaudi2_nic_alloc_ring(gaudi2_nic, &gaudi2_nic->fifo_ring,
					ALIGN(sizeof(u32), DEVICE_CACHE_LINE_SIZE), 1);
	if (rc) {
		dev_err(hdev->dev, "Failed to allocate fifo ring\n");
		return rc;
	}

	rc = gaudi2_nic_alloc_ring(gaudi2_nic, &gaudi2_nic->rx_ring, NIC_RAW_ELEM_SIZE,
					NIC_RX_RING_PKT_NUM);
	if (rc) {
		dev_err(hdev->dev, "Failed to allocate RX ring\n");
		goto err_rx_ring;
	}

	rc = gaudi2_nic_alloc_ring(gaudi2_nic, &gaudi2_nic->wq_ring, sizeof(struct gaudi2_sq_wqe),
					QP_WQE_NUM_REC);
	if (rc) {
		dev_err(hdev->dev, "Failed to allocate WQ ring\n");
		goto err_wq_ring;
	}

	rc = gaudi2_nic_alloc_ring(gaudi2_nic, &gaudi2_nic->eq_ring,
					sizeof(struct hl_nic_eqe),
					NIC_EQ_RING_NUM_REC);
	if (rc) {
		dev_err(hdev->dev, "Failed to allocate EQ ring\n");
		goto err_eq_ring;
	}

	rc = gaudi2_nic_alloc_cq_rings(gaudi2_nic);
	if (rc) {
		dev_err(hdev->dev, "Failed to allocate CQ rings\n");
		goto err_cq_rings;
	}

	return 0;

err_cq_rings:
	gaudi2_nic_free_ring(gaudi2_nic, &gaudi2_nic->eq_ring);
err_eq_ring:
	gaudi2_nic_free_ring(gaudi2_nic, &gaudi2_nic->wq_ring);
err_wq_ring:
	gaudi2_nic_free_ring(gaudi2_nic, &gaudi2_nic->rx_ring);
err_rx_ring:
	gaudi2_nic_free_ring(gaudi2_nic, &gaudi2_nic->fifo_ring);

	return rc;
}

static void gaudi2_nic_free_rings_resources(struct gaudi2_nic_port *gaudi2_nic)
{
	gaudi2_nic_free_cq_rings(gaudi2_nic);
	gaudi2_nic_free_ring(gaudi2_nic, &gaudi2_nic->eq_ring);
	gaudi2_nic_free_ring(gaudi2_nic, &gaudi2_nic->wq_ring);
	gaudi2_nic_free_ring(gaudi2_nic, &gaudi2_nic->rx_ring);
	gaudi2_nic_free_ring(gaudi2_nic, &gaudi2_nic->fifo_ring);
}

static void gaudi2_nic_reset_rings(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_nic_ring *cq_ring;

	/* Reset CQ ring HW PI and shadow PI/CI */
	cq_ring = &gaudi2_nic->cq_rings[NIC_CQ_RDMA_IDX];
	*((u32 *) RING_PI_ADDRESS(cq_ring)) = 0;
	cq_ring->pi_shadow = 0;
	cq_ring->ci_shadow = 0;
}

static void gaudi2_nic_port_sw_fini(struct hl_nic_port *nic_port)
{
	struct gaudi2_nic_port *gaudi2_nic = nic_port->nic_specific;

	mutex_destroy(&gaudi2_nic->qp_destroy_lock);
	mutex_destroy(&gaudi2_nic->cfg_lock);

	hl_nic_eq_dispatcher_fini(nic_port);
	gaudi2_nic_free_rings_resources(gaudi2_nic);
}

static void link_eqe_init(struct hl_nic_port *nic_port)
{
	/* Init only header. Data field(i.e. link status) would be updated
	 * when event is ready to be sent to user.
	 */
	nic_port->link_eqe.data[0] = EQE_HEADER(true, EQE_LINK_STATUS);
}

static int gaudi2_nic_port_sw_init(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct gaudi2_nic_port *gaudi2_nic;
	struct gaudi2_device *gaudi2;
	u32 port = nic_port->port;
	int rc;

	gaudi2 = hdev->asic_specific;
	gaudi2_nic = &gaudi2->nic_ports[port];
	gaudi2_nic->hdev = hdev;
	gaudi2_nic->nic_port = nic_port;
	nic_port->nic_specific = gaudi2_nic;

	nic_port->nic_macro = &hdev->nic.nic_macros[port >> 1];

	rc = gaudi2_nic_alloc_rings_resources(gaudi2_nic);
	if (rc) {
		dev_err(hdev->dev, "Failed to alloc rings, port: %d, %d\n", port, rc);
		return rc;
	}

	hl_nic_eq_dispatcher_init(gaudi2_nic->nic_port);

	mutex_init(&gaudi2_nic->cfg_lock);
	mutex_init(&gaudi2_nic->qp_destroy_lock);

	/* Userspace might not be notified immediately of link event from HW.
	 * e.g. if serdes is not yet configured or link is not stable, SW might
	 * defer sending link event to userspace.
	 * Hence we cache HW link EQE and updated with real link status just
	 * before sending to userspace.
	 */
	link_eqe_init(nic_port);

	return 0;
}

static int gaudi2_nic_macro_sw_init(struct hl_nic_macro *nic_macro)
{
	struct gaudi2_nic_macro *gaudi2_macro;
	struct gaudi2_device *gaudi2;
	struct hl_device *hdev;

	hdev = nic_macro->hdev;
	gaudi2 = hdev->asic_specific;
	gaudi2_macro = &gaudi2->nic_macros[nic_macro->idx];

	gaudi2_macro->hdev = hdev;
	gaudi2_macro->nic_macro = nic_macro;

	nic_macro->asic_priv = gaudi2_macro;

	return 0;
}

static void gaudi2_nic_macro_sw_fini(struct hl_nic_macro *nic_macro)
{
}

/**
 * gaudi2_nic_disable_nics_interrupts() - Disable interrupts of all NICs.
 * Gaudi2 NIC interrupts are enabled by default, need to disable them ASAP
 * before ports init and after hard reset.
 *
 * @hdev: habanalabs device structure.
  */
void gaudi2_nic_disable_nics_interrupts(struct hl_device *hdev)
{
	u32 port;

	/* The CPU_IF_NIC registers are handled only by the privileged embedded code and in any
	 * case, this is relevant only on PLDM where the PHY link is always ON
	 */
	if (hdev->pldm)
		gaudi2_disable_nic_interrupts_cpu_if(hdev);

	/* Disable interrupts of all NICs */
	if (hdev->nic_ports_mask) {
		/* we only need the port number for NIC_WREG32 */
		for (port = 0 ; port < NIC_NUMBER_OF_PORTS ; port++) {
			NIC_WREG32(mmNIC0_QPC0_EVENT_QUE_CFG, 0);
			NIC_WREG32(mmNIC0_QPC0_INTERRUPT_EN, 0);
			NIC_WREG32(mmNIC0_QPC0_INTERRUPT_MASK, 0xFFFFFFFF);

			/* This registers needs to be configured only in case of PLDM */
			if (hdev->pldm) {
				NIC_WREG32(mmNIC0_QPC0_INTERRUPT_RESP_ERR_MASK, 0xFFFFFFFF);
				NIC_WREG32(mmNIC0_TXE0_INTERRUPT_MASK, 0xFFFFFFFF);
				NIC_WREG32(mmNIC0_RXE0_SPI_INTR_MASK, 0xFFFFFFFF);
				NIC_WREG32(mmNIC0_RXE0_SEI_INTR_MASK, 0xFFFFFFFF);
				NIC_WREG32(mmNIC0_TXS0_INTERRUPT_MASK, 0xFFFFFFFF);
			}

			/* WA for H/W bug H6-3339 - mask the link UP interrupt */
			NIC_MACRO_WREG32(mmNIC0_PHY_PHY_LINK_STS_INTR, 0x1);
		}

		/* flush */
		port = 0;
		NIC_RREG32(mmNIC0_QPC0_EVENT_QUE_CFG);
	}
}

/**
 * gaudi2_nic_quiescence() - make sure that NIC does not generate events nor
 *                           receives traffic.
 * Gaudi2 default values at power-up and after hard-reset are interrupts enabled
 * and Rx enabled, we need to disable them until driver configuration is
 * complete.
 *
 * @hdev: habanalabs device structure.
 */
void gaudi2_nic_quiescence(struct hl_device *hdev)
{
	/*
	 * Do not quiescence the ports during device release
	 * reset aka soft reset flow.
	 */
	if (gaudi2_nic_get_hw_cap(hdev))
		return;

	dev_dbg(hdev->dev, "Quiescence the NICs\n");

	gaudi2_nic_disable_nics_interrupts(hdev);

	/* quiescence phy before configuring to prevent any packet entering nic */
	gaudi2_nic_quiescence_phy_no_fw(hdev);
}

static int gaudi2_nic_set_pfc(struct hl_nic_port *nic_port)
{
	struct gaudi2_nic_port *gaudi2_nic = nic_port->nic_specific;
	struct hl_device *hdev = gaudi2_nic->hdev;
	u32 port = nic_port->port, val = 0;
	int i, start_lane;

	val |= NIC0_MAC_CH0_MAC_128_COMMAND_CONFIG_TX_ENA_MASK |
		NIC0_MAC_CH0_MAC_128_COMMAND_CONFIG_RX_ENA_MASK |
		NIC0_MAC_CH0_MAC_128_COMMAND_CONFIG_PROMIS_EN_MASK |
		NIC0_MAC_CH0_MAC_128_COMMAND_CONFIG_TX_PAD_EN_MASK;

	if (nic_port->pfc_enable) {
		val |= NIC0_MAC_CH0_MAC_128_COMMAND_CONFIG_PFC_MODE_MASK;
	} else {
		val |= NIC0_MAC_CH0_MAC_128_COMMAND_CONFIG_PAUSE_IGNORE_MASK |
			NIC0_MAC_CH0_MAC_128_COMMAND_CONFIG_CNTL_FRAME_ENA_MASK;
	}

	/* Write the value for each lane under this port */
	start_lane = (port & 1) ? (NIC_MAC_NUM_OF_LANES / 2) : NIC_MAC_LANES_START;

	for (i = start_lane ; i < start_lane + (NIC_MAC_NUM_OF_LANES / 2) ; i++)
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_COMMAND_CONFIG + MAC_CH_OFFSET(i), val);

	return 0;
}

static void gaudi2_nic_config_port_hw_txs(struct gaudi2_nic_port *gaudi2_nic)
{
	u32 txs_schedq, txs_fence_idx, txs_pi, txs_ci, txs_tail, txs_head,
		txs_timeout_31_0, timeout_47_32, prio, txs_port, rl_en_log_time;
	struct hl_nic_port *nic_port = gaudi2_nic->nic_port;
	struct hl_nic_properties *nic_prop;
	struct gaudi2_device *gaudi2;
	u32 port = nic_port->port;
	struct hl_device *hdev;
	u64 txs_addr;
	int i;

	hdev = gaudi2_nic->hdev;
	gaudi2 = hdev->asic_specific;
	nic_prop = &hdev->asic_prop.nic_props;

	txs_addr = nic_prop->txs_base_addr + port * nic_prop->txs_base_size;

	/* TX sched-Qs list */
	for (i = 0 ; i < TXS_FREE_NUM_ENTRIES ; i++)
		writel(TXS_GRANULARITY + i, hdev->pcie_bar[DRAM_BAR_ID] +
			((txs_addr + TXS_FREE_OFFS + i * TXS_ENT_SIZE) -
				gaudi2->dram_bar_cur_addr));

	/* Perform read to flush the writes */
	readq(hdev->pcie_bar[DRAM_BAR_ID] + txs_addr - gaudi2->dram_bar_cur_addr);

	/* set TX sched queues address */
	NIC_WREG32(mmNIC0_TXS0_BASE_ADDRESS_63_32,
		   upper_32_bits(txs_addr + TXS_FIFO_OFFS));
	NIC_WREG32(mmNIC0_TXS0_BASE_ADDRESS_31_7,
		   lower_32_bits(txs_addr + TXS_FIFO_OFFS) >> 7);

	/* Set access to bypass the MMU (old style configuration) */
	NIC_WREG32(mmNIC0_TXS0_AXI_USER_LO, 0x400);

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

	/* Gaudi2 TXS implements 256 schedule-Qs.
	 * These queues are hard-divided to 4x64 priority groups of Qs.
	 *    (The first and last Q group-relative numbers of each group (0-63) can be configured
	 *     via mmNIC0_TXS0_FIRST_SCHEDQ_ID and mmNIC0_TXS0_LAST_SCHEDQ_ID, We will use its
	 *     default values of 0 and 63 respectively).
	 * From the above pools we need to allocate and configure:
	 *   256 Qs (0-255) are evenly divided between the 4 possible ports so each port is
	 *       assigned with 64 Qs.
	 *   The 64 Qs are divided between the 4 possible priorities generating 16
	 *   priority-granularity groups of which:
	 *   - The Last group is dedicated for Ethernet (RAW_SCHED_Q).
	 *   - The last-1 group is dedicated for the RDMA responder (RES_SCHED_Q)
	 *   - The Last-2 group is dedicated for the RDMA Req (REQ_SCHED_Q)
	 *   - The remaining Qs will be used by the BBR when supported.
	 */
	for (i = 0 ; i < TXS_SCHEDQ ; i++) {
		/* main sched Qs */
		txs_port = i / TXS_PORT_NUM_SCHEDQS;

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

	NIC_WREG32(mmNIC0_TXS0_SCAN_TIME_COMPARE_0, 4);
	NIC_WREG32(mmNIC0_TXS0_SCAN_TIME_COMPARE_1, 0);
	NIC_WREG32(mmNIC0_TXS0_TMR_SCAN_EN, 1);

	NIC_WREG32(mmNIC0_TXS0_BASE_ADDRESS_FREE_LIST_63_32,
		   upper_32_bits(txs_addr + TXS_FREE_OFFS));

	NIC_WREG32(mmNIC0_TXS0_BASE_ADDRESS_FREE_LIST_31_0,
		   lower_32_bits(txs_addr + TXS_FREE_OFFS));

	NIC_WREG32(mmNIC0_TXS0_LIST_MASK,
		   ~(0xFFFFFFFF << (ilog2(TXS_FREE_NUM_ENTRIES) - 5)));
	NIC_WREG32(mmNIC0_TXS0_PRODUCER_UPDATE, TXS_FREE_NUM_ENTRIES);
	NIC_WREG32(mmNIC0_TXS0_PRODUCER_UPDATE_EN, 1);
	NIC_WREG32(mmNIC0_TXS0_PRODUCER_UPDATE_EN, 0);
	NIC_WREG32(mmNIC0_TXS0_LIST_MEM_READ_MASK, 0);
	NIC_WREG32(mmNIC0_TXS0_PUSH_LOCK_EN, 1);

	/* disable burst size optimization */
	NIC_WREG32(mmNIC0_TXS0_IGNORE_BURST_EN, 0);
}

static void gaudi2_nic_config_port_hw_txe(struct gaudi2_nic_port *gaudi2_nic, u64 mac_addr)
{
	struct hl_device *hdev = gaudi2_nic->hdev;
	struct hl_nic_properties *nic_prop;
	u32 port;

	nic_prop = &hdev->asic_prop.nic_props;
	port = gaudi2_nic->nic_port->port;

	/* set the base address of the raw wq */
	NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_63_32_0,
		   upper_32_bits(RING_BUF_DMA_ADDRESS(&gaudi2_nic->wq_ring)));

	NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_31_0_0,
		   lower_32_bits(RING_BUF_DMA_ADDRESS(&gaudi2_nic->wq_ring)));

	NIC_WREG32(mmNIC0_TXE0_LOG_MAX_WQ_SIZE_0, WQ_BUFFER_LOG_SIZE - 2);

	/* Send all priorities on DSCP '0' */
	NIC_WREG32(mmNIC0_TXE0_PRIO_TO_DSCP_0, 0);

	/* TODO: ECN */
	NIC_WREG32(mmNIC0_TXE0_PORT0_MAC_CFG_47_32, (mac_addr >> 32) & 0xFFFF);
	NIC_WREG32(mmNIC0_TXE0_PORT0_MAC_CFG_31_0, mac_addr & 0xFFFFFFFF);
	NIC_WREG32(mmNIC0_TXE0_PORT1_MAC_CFG_47_32, (mac_addr >> 32) & 0xFFFF);
	NIC_WREG32(mmNIC0_TXE0_PORT1_MAC_CFG_31_0, mac_addr & 0xFFFFFFFF);

	/* set MMU bypass for kernel WQ */
	NIC_WREG32(mmNIC0_TXE0_WQE_USER_CFG, 0x1);

	NIC_WREG32(mmNIC0_TXE0_WQE_PREFETCH_CFG, 0x3);

	NIC_WREG32(mmNIC0_TXE0_BTH_MKEY, 0xffff);

	NIC_WREG32(mmNIC0_TXE0_STATS_CFG0, nic_prop->clk * USEC_PER_MSEC); /* 1 msec window size */
	NIC_RMWREG32(mmNIC0_TXE0_STATS_CFG1, 1, NIC0_TXE0_STATS_CFG1_LATENCY_ENABLE_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_STATS_CFG1, 0, NIC0_TXE0_STATS_CFG1_WIN_TYPE_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_STATS_CFG1, 0, NIC0_TXE0_STATS_CFG1_WIN_SAMP_LATENCY_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_STATS_CFG1, 3, NIC0_TXE0_STATS_CFG1_TOT_TYPE_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_STATS_CFG1, 1, NIC0_TXE0_STATS_CFG1_ENABLE_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_STATS_CFG2, 0, NIC0_TXE0_STATS_CFG2_LATENCY_WRAP_EN_MASK);
	NIC_RMWREG32(mmNIC0_TXE0_STATS_CFG2, 2 * nic_prop->clk,
			NIC0_TXE0_STATS_CFG2_LATENCY_MAX_VAL_MASK);
}

static void gaudi2_nic_config_port_hw_qpc(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_nic_port *nic_port = gaudi2_nic->nic_port;
	u64 req_qpc_base_addr, res_qpc_base_addr;
	struct hl_nic_properties *nic_prop;
	u32 port = nic_port->port;
	struct hl_device *hdev;

	hdev = gaudi2_nic->hdev;
	nic_prop = &hdev->asic_prop.nic_props;

	req_qpc_base_addr = nic_prop->req_qpc_base_addr + port * nic_prop->req_qpc_base_size;
	res_qpc_base_addr = nic_prop->res_qpc_base_addr + port * nic_prop->res_qpc_base_size;

	NIC_WREG32(mmNIC0_QPC0_REQ_BASE_ADDRESS_63_32, upper_32_bits(req_qpc_base_addr));
	NIC_WREG32(mmNIC0_QPC0_REQ_BASE_ADDRESS_31_7, lower_32_bits(req_qpc_base_addr) >> 7);

	NIC_WREG32(mmNIC0_QPC0_RES_BASE_ADDRESS_63_32, upper_32_bits(res_qpc_base_addr));
	NIC_WREG32(mmNIC0_QPC0_RES_BASE_ADDRESS_31_7, lower_32_bits(res_qpc_base_addr) >> 7);

	NIC_WREG32(mmNIC0_QPC0_RES_QPC_CACHE_INVALIDATE, 1);
	NIC_WREG32(mmNIC0_QPC0_REQ_QPC_CACHE_INVALIDATE, 1);
	NIC_WREG32(mmNIC0_QPC0_RES_QPC_CACHE_INVALIDATE, 0);
	NIC_WREG32(mmNIC0_QPC0_REQ_QPC_CACHE_INVALIDATE, 0);

	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_CAUSE, 0);

	/* Configure MMU-BP override for DB-FIFOs */
	NIC_WREG32(mmNIC0_QPC0_AXUSER_DB_FIFO_HB_WR_OVRD_LO, 0xFFFFFBFF);
	NIC_WREG32(mmNIC0_QPC0_AXUSER_DB_FIFO_HB_RD_OVRD_LO, 0xFFFFFBFF);

	/* Configure doorbell */
	NIC_WREG32(mmNIC0_QPC0_DBFIFOSECUR_CI_UPD_ADDR_DBFIFO_CI_UPD_ADDR_63_32,
			upper_32_bits(RING_BUF_DMA_ADDRESS(&gaudi2_nic->fifo_ring)));
	NIC_WREG32(mmNIC0_QPC0_DBFIFOSECUR_CI_UPD_ADDR_DBFIFO_CI_UPD_ADDR_31_7,
			lower_32_bits(RING_BUF_DMA_ADDRESS(&gaudi2_nic->fifo_ring)) >> 7);

	gaudi2_nic_eq_dispatcher_register_db(gaudi2_nic, HL_KERNEL_ASID_ID,
						GAUDI2_DB_FIFO_SECURE_HW_ID);

	/* Configure MMU-BP override for error-FIFO */
	NIC_WREG32(mmNIC0_QPC0_AXUSER_ERR_FIFO_HB_WR_OVRD_LO, 0xFFFFFBFF);
	NIC_WREG32(mmNIC0_QPC0_AXUSER_ERR_FIFO_HB_RD_OVRD_LO, 0xFFFFFBFF);

	NIC_WREG32(mmNIC0_QPC0_RETRY_COUNT_MAX,
		(0xFE << NIC0_QPC0_RETRY_COUNT_MAX_TIMEOUT_SHIFT) |
		(0xFE << NIC0_QPC0_RETRY_COUNT_MAX_SEQUENCE_ERROR_SHIFT));

	/* Configure MMU-BP override for QPCs */
	NIC_WREG32(mmNIC0_QPC0_AXUSER_QPC_REQ_HB_WR_OVRD_LO, 0xFFFFFBFF);
	NIC_WREG32(mmNIC0_QPC0_AXUSER_QPC_REQ_HB_RD_OVRD_LO, 0xFFFFFBFF);
	NIC_WREG32(mmNIC0_QPC0_AXUSER_QPC_RESP_HB_WR_OVRD_LO, 0xFFFFFBFF);
	NIC_WREG32(mmNIC0_QPC0_AXUSER_QPC_RESP_HB_RD_OVRD_LO, 0xFFFFFBFF);

	/* Configure MMU-BP override for Congestion-Queue */
	NIC_WREG32(mmNIC0_QPC0_AXUSER_CONG_QUE_HB_WR_OVRD_LO, 0xFFFFFBFF);
	NIC_WREG32(mmNIC0_QPC0_AXUSER_CONG_QUE_HB_RD_OVRD_LO, 0xFFFFFBFF);

	NIC_RMWREG32(mmNIC0_QPC0_REQ_STATIC_CONFIG, 1,
			NIC0_QPC0_REQ_STATIC_CONFIG_QM_MOVEQP2ERR_SECUR_ERR_MASK);
	NIC_RMWREG32(mmNIC0_QPC0_REQ_STATIC_CONFIG, 0,
			NIC0_QPC0_REQ_STATIC_CONFIG_QM_PUSH_TO_ERROR_ASID_MASK);
	NIC_RMWREG32(mmNIC0_QPC0_REQ_STATIC_CONFIG, 1,
			NIC0_QPC0_REQ_STATIC_CONFIG_QM_UPD_IGNORE_ASID_ERR_MASK);
	NIC_RMWREG32(mmNIC0_QPC0_REQ_STATIC_CONFIG, 0,
			NIC0_QPC0_REQ_STATIC_CONFIG_QM_MOVEQP2ERR_ASID_ERR_MASK);
	NIC_RMWREG32(mmNIC0_QPC0_REQ_STATIC_CONFIG, 1,
			NIC0_QPC0_REQ_STATIC_CONFIG_QM_PUSH_TO_ERROR_SECURITY_MASK);

	/* Disable the WTD back-pressure mechanism to ARC and QMAN - it will be
	 * enabled later on by the user.
	 */
	NIC_RMWREG32_SHIFTED(mmNIC0_QPC0_WTD_CONFIG, 0,
		     NIC0_QPC0_WTD_CONFIG_WQ_BP_2ARC_EN_MASK |
		     NIC0_QPC0_WTD_CONFIG_WQ_BP_2QMAN_EN_MASK);
}

static void gaudi2_nic_config_port_hw_rxe(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_nic_properties *nic_prop = &gaudi2_nic->hdev->asic_prop.nic_props;
	struct hl_nic_ring *rx_ring = &gaudi2_nic->rx_ring;
	struct hl_nic_ring *cq_ring = &gaudi2_nic->cq_rings[NIC_CQ_RAW_IDX];
	struct hl_device *hdev = gaudi2_nic->hdev;
	u32 port = gaudi2_nic->nic_port->port;
	u32 rx_mem_addr_lo, rx_mem_addr_hi;
	int i;

	rx_mem_addr_lo = lower_32_bits(RING_BUF_DMA_ADDRESS(rx_ring));
	rx_mem_addr_hi = upper_32_bits(RING_BUF_DMA_ADDRESS(rx_ring));

	NIC_WREG32(mmNIC0_RXE0_RAW_QPN_P0_0, RAW_QPN);
	NIC_WREG32(mmNIC0_RXE0_RAW_QPN_P0_1, RAW_QPN);
	NIC_WREG32(mmNIC0_RXE0_RAW_QPN_P1_0, RAW_QPN);
	NIC_WREG32(mmNIC0_RXE0_RAW_QPN_P1_1, RAW_QPN);
	NIC_WREG32(mmNIC0_RXE0_RAW_QPN_P2_0, RAW_QPN);
	NIC_WREG32(mmNIC0_RXE0_RAW_QPN_P2_1, RAW_QPN);
	NIC_WREG32(mmNIC0_RXE0_RAW_QPN_P3_0, RAW_QPN);
	NIC_WREG32(mmNIC0_RXE0_RAW_QPN_P3_1, RAW_QPN);

	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_LO_P0_0, rx_mem_addr_lo);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_LO_P0_1, rx_mem_addr_lo);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_HI_P0_0, rx_mem_addr_hi);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_HI_P0_1, rx_mem_addr_hi);

	/* #define NIC5_RXE1_RAW_MISC_P2_LOG_RAW_ENTRY_SIZE_P2_SHIFT 0   */
	/* #define NIC5_RXE1_RAW_MISC_P2_LOG_RAW_ENTRY_SIZE_P2_MASK  0xF */

	/* #define NIC5_RXE1_RAW_MISC_P2_LOG_BUFFER_SIZE_MASK_P2_SHIFT 15 */
	/* #define NIC5_RXE1_RAW_MISC_P2_LOG_BUFFER_SIZE_MASK_P2_MASK     */
	/* 0xF8000                                                        */

	NIC_WREG32(mmNIC0_RXE0_RAW_MISC_P0_0,
			(ilog2(rx_ring->elem_size) &
			NIC0_RXE0_RAW_MISC_P2_LOG_RAW_ENTRY_SIZE_P2_MASK) |
			((ilog2(rx_ring->count) & 0x1F) << 15));

	NIC_WREG32(mmNIC0_RXE0_RAW_MISC_P0_1,
			(ilog2(rx_ring->elem_size) &
			NIC0_RXE0_RAW_MISC_P2_LOG_RAW_ENTRY_SIZE_P2_MASK) |
			((ilog2(rx_ring->count) & 0x1F) << 15));

	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_LO_P1_0, rx_mem_addr_lo);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_LO_P1_1, rx_mem_addr_lo);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_HI_P1_0, rx_mem_addr_hi);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_HI_P1_1, rx_mem_addr_hi);

	NIC_WREG32(mmNIC0_RXE0_RAW_MISC_P1_0,
			(ilog2(rx_ring->elem_size) &
			NIC0_RXE0_RAW_MISC_P2_LOG_RAW_ENTRY_SIZE_P2_MASK) |
			((ilog2(rx_ring->count) & 0x1F) << 15));

	NIC_WREG32(mmNIC0_RXE0_RAW_MISC_P1_1,
			(ilog2(rx_ring->elem_size) &
			NIC0_RXE0_RAW_MISC_P2_LOG_RAW_ENTRY_SIZE_P2_MASK) |
			((ilog2(rx_ring->count) & 0x1F) << 15));

	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_LO_P2_0, rx_mem_addr_lo);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_LO_P2_1, rx_mem_addr_lo);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_HI_P2_0, rx_mem_addr_hi);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_HI_P2_1, rx_mem_addr_hi);

	NIC_WREG32(mmNIC0_RXE0_RAW_MISC_P2_0,
			(ilog2(rx_ring->elem_size) &
			NIC0_RXE0_RAW_MISC_P2_LOG_RAW_ENTRY_SIZE_P2_MASK) |
			((ilog2(rx_ring->count) & 0x1F) << 15));

	NIC_WREG32(mmNIC0_RXE0_RAW_MISC_P2_1,
			(ilog2(rx_ring->elem_size) &
			NIC0_RXE0_RAW_MISC_P2_LOG_RAW_ENTRY_SIZE_P2_MASK) |
			((ilog2(rx_ring->count) & 0x1F) << 15));

	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_LO_P3_0, rx_mem_addr_lo);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_LO_P3_1, rx_mem_addr_lo);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_HI_P3_0, rx_mem_addr_hi);
	NIC_WREG32(mmNIC0_RXE0_RAW_BASE_HI_P3_1, rx_mem_addr_hi);

	NIC_WREG32(mmNIC0_RXE0_RAW_MISC_P3_0,
			(ilog2(rx_ring->elem_size) &
			NIC0_RXE0_RAW_MISC_P2_LOG_RAW_ENTRY_SIZE_P2_MASK) |
			((ilog2(rx_ring->count) & 0x1F) << 15));

	NIC_WREG32(mmNIC0_RXE0_RAW_MISC_P3_1,
			(ilog2(rx_ring->elem_size) &
			NIC0_RXE0_RAW_MISC_P2_LOG_RAW_ENTRY_SIZE_P2_MASK) |
			((ilog2(rx_ring->count) & 0x1F) << 15));

	NIC_WREG32(mmNIC0_RXE0_CQ_BASE_ADDR_63_32,
		   upper_32_bits(RING_BUF_DMA_ADDRESS(cq_ring)));
	NIC_WREG32(mmNIC0_RXE0_CQ_BASE_ADDR_31_7,
		   lower_32_bits(RING_BUF_DMA_ADDRESS(cq_ring)) & 0xFFFFFF80);

	NIC_WREG32(mmNIC0_RXE0_CQ_PI_ADDR_HI_0,
		   upper_32_bits(RING_PI_DMA_ADDRESS(cq_ring)));
	NIC_WREG32(mmNIC0_RXE0_CQ_PI_ADDR_LO_0,
		   lower_32_bits(RING_PI_DMA_ADDRESS(cq_ring)) & 0xFFFFFF80);

	/* Set the max CQ size */
	NIC_WREG32(mmNIC0_RXE0_CQ_LOG_MAX_SIZE, ilog2(NIC_CQ_MAX_ENTRIES));

	/* Set the actual single CQ size log2(number of entries in cq)*/
	NIC_WREG32(mmNIC0_RXE0_CQ_LOG_SIZE_0, ilog2(cq_ring->count));

	/* Initialize MMU-BP for all CQs */
	for (i = 0 ; i < nic_prop->max_cqs ; i++)
		NIC_WREG32(mmNIC0_RXE0_AXUSER_AXUSER_CQ0_HB_WR_OVRD_LO +
			(i * NIC_RXE_AXUSER_AXUSER_CQ_OFFSET), 0xFFFFFBFF);

	NIC_WREG32(mmNIC0_RXE0_CQ_WRITE_INDEX_0, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_PRODUCER_INDEX_0, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_CONSUMER_INDEX_0, 0);

	/* enable, pi-update and completion-events */
	NIC_WREG32(mmNIC0_RXE0_CQ_CFG_0, 1 << NIC0_RXE0_CQ_CFG_WRITE_PI_EN_SHIFT |
					1 << NIC0_RXE0_CQ_CFG_ENABLE_SHIFT);

	/* disable all RDMA CQs */
	for (i = 1 ; i < nic_prop->max_cqs ; i++)
		NIC_WREG32(mmNIC0_RXE0_CQ_CFG_0 + i * 4, 0);

	/* set MMU bypass for kernel WQ */
	NIC_WREG32(mmNIC0_RXE0_ARUSER_MMU_BP, 0x1);

	/* set SPMU RXE counters of Group 1 */
	NIC_WREG32(mmNIC0_RXE0_DBG_SPMU_SELECT, 0x1);
}

static void gaudi2_nic_config_hw_mac_filter(struct gaudi2_nic_port *gaudi2_nic, u64 mac_addr)
{
	struct hl_nic_port *nic_port = gaudi2_nic->nic_port;
	struct hl_device *hdev = gaudi2_nic->hdev;
	u32 port = nic_port->port;

	if (nic_port->eth_enable) {
		if (port & 1) {
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RC_MAC_31_0_2,
					 mac_addr & 0xFFFFFFFF);
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RC_MAC_47_32_2,
					 (mac_addr >> 32) & 0xFFFF);

			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RC_MAC_31_0_MASK_2, 0);
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RC_MAC_47_32_MASK_2, 0);

			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RAW0_MAC_31_0_2,
					 mac_addr & 0xFFFFFFFF);
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RAW0_MAC_47_32_2,
					 (mac_addr >> 32) & 0xFFFF);

			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RAW0_MAC_31_0_MASK_2, 0);
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RAW0_MAC_47_32_MASK_2, 0);
		} else {
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RC_MAC_31_0_0,
					 mac_addr & 0xFFFFFFFF);
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RC_MAC_47_32_0,
					 (mac_addr >> 32) & 0xFFFF);

			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RC_MAC_31_0_MASK_0, 0);
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RC_MAC_47_32_MASK_0, 0);

			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RAW0_MAC_31_0_0,
					 mac_addr & 0xFFFFFFFF);
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RAW0_MAC_47_32_0,
					 (mac_addr >> 32) & 0xFFFF);

			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RAW0_MAC_31_0_MASK_0, 0);
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RAW0_MAC_47_32_MASK_0, 0);
		}
	} else {
		if (port & 1) {
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RC_MAC_31_0_MASK_2,
					 0xFFFFFFFF);

			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RC_MAC_47_32_MASK_2, 0xFFFF);
		} else {
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RC_MAC_31_0_MASK_0,
					 0xFFFFFFFF);

			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TS_RC_MAC_47_32_MASK_0, 0xFFFF);
		}
	}
}

static int gaudi2_nic_hw_mac_ch_reset(struct gaudi2_nic_port *gaudi2_nic, int lane)
{
	struct hl_device *hdev = gaudi2_nic->hdev;
	u32 port = gaudi2_nic->nic_port->port;
	ktime_t timeout;
	u32 read_reg;

	if (hdev->nic.skip_mac_reset)
		return 0;

	timeout = ktime_add_ms(ktime_get(), HL_PENDING_RESET_LONG_SEC * 1000);

	do {
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_CONTROL1 +
				MAC_CH_OFFSET(lane),
				BIT(NIC0_MAC_CH0_MAC_PCS_CONTROL1_FLD_RESET_SHIFT));
		usleep_range(50, 200);

		read_reg = NIC_MACRO_RREG32(mmNIC0_MAC_CH0_MAC_PCS_CONTROL1 +
				MAC_CH_OFFSET(lane));
	} while ((read_reg & NIC0_MAC_CH0_MAC_PCS_CONTROL1_FLD_RESET_MASK) &&
		ktime_compare(ktime_get(), timeout) < 0);

	if (read_reg & NIC0_MAC_CH0_MAC_PCS_CONTROL1_FLD_RESET_MASK) {
		dev_err(hdev->dev, "Timeout while MAC channel %d reset\n", lane);
		return -EBUSY;
	}

	return 0;
}

static void gaudi2_nic_hw_mac_port_config(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_nic_port *nic_port = gaudi2_nic->nic_port;
	struct hl_device *hdev = gaudi2_nic->hdev;
	u32 port = nic_port->port;
	int i, start_lane;

	start_lane = (port & 1) ? (NIC_MAC_NUM_OF_LANES / 2) : NIC_MAC_LANES_START;

	for (i = start_lane ; i < start_lane + (NIC_MAC_NUM_OF_LANES / 2) ; i++) {
		gaudi2_nic_hw_mac_ch_reset(gaudi2_nic, i);

		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_FRM_LENGTH +
				MAC_CH_OFFSET(i), NIC_MAC_MAX_FRM_LEN);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_COMMAND_CONFIG +
				MAC_CH_OFFSET(i), 0x2913);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_TX_FIFO_SECTIONS +
				MAC_CH_OFFSET(i), 0x4);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_RX_FIFO_SECTIONS +
				MAC_CH_OFFSET(i), 0x4);

		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL01_PAUSE_QUANTA +
				MAC_CH_OFFSET(i), 0xFFFFFFFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL01_QUANTA_THRESH +
				MAC_CH_OFFSET(i), 0x7FFF7FFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL23_PAUSE_QUANTA +
				MAC_CH_OFFSET(i), 0xFFFFFFFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL23_QUANTA_THRESH +
				MAC_CH_OFFSET(i), 0x7FFF7FFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL45_PAUSE_QUANTA +
				MAC_CH_OFFSET(i), 0xFFFFFFFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL45_QUANTA_THRESH +
				MAC_CH_OFFSET(i), 0x7FFF7FFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL67_PAUSE_QUANTA +
				MAC_CH_OFFSET(i), 0xFFFFFFFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL67_QUANTA_THRESH +
				MAC_CH_OFFSET(i), 0x7FFF7FFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL89_PAUSE_QUANTA +
				MAC_CH_OFFSET(i), 0xFFFFFFFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL89_QUANTA_THRESH +
				MAC_CH_OFFSET(i), 0x7FFF7FFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL1011_PAUSE_QUANTA +
				MAC_CH_OFFSET(i), 0xFFFFFFFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL1011_QUANTA_THRESH +
				MAC_CH_OFFSET(i), 0x7FFF7FFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL1213_PAUSE_QUANTA +
				MAC_CH_OFFSET(i), 0xFFFFFFFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL1213_QUANTA_THRESH +
				MAC_CH_OFFSET(i), 0x7FFF7FFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL1415_PAUSE_QUANTA +
				MAC_CH_OFFSET(i), 0xFFFFFFFF);
		NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_128_CL1415_QUANTA_THRESH +
				MAC_CH_OFFSET(i), 0x7FFF7FFF);

		switch (nic_port->speed) {
		case SPEED_25000:
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL_INTVL +
					MAC_CH_OFFSET(i),
					REGMASK(0x4FFF,
						NIC0_MAC_CH0_MAC_PCS_VENDOR_VL_INTVL_MARKER_COUNTER)
					);
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_PCS_MODE +
					MAC_CH_OFFSET(i),
					REGMASK(1,
						NIC0_MAC_CH0_MAC_PCS_VENDOR_PCS_MODE_ENA_CLAUSE49)
					| REGMASK(1,
						NIC0_MAC_CH0_MAC_PCS_VENDOR_PCS_MODE_HI_BER25));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xC1, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_0_M0)
					| REGMASK(0x68, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_0_M1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x21, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_1_M2));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xF0, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_0_M0)
					| REGMASK(0xC4, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_0_M1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0xE6, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_1_M2));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xC5, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_0_M0)
					| REGMASK(0x65, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_0_M1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x9B, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_1_M2));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xA2, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_0_M0)
					| REGMASK(0x79, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_0_M1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x3D,  NIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_1_M2));
			break;
		case SPEED_50000:
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL_INTVL +
					MAC_CH_OFFSET(i),
					REGMASK(0x4FFF,
						NIC0_MAC_CH0_MAC_PCS_VENDOR_VL_INTVL_MARKER_COUNTER)
					);
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_PCS_MODE +
					MAC_CH_OFFSET(i), 0x0);
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x90, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_0_M0)
					| REGMASK(0x76, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_0_M1));

			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x47, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_1_M2));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xF0, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_0_M0)
					| REGMASK(0xC4, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_0_M1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0xE6, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_1_M2));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xC5, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_0_M0)
					| REGMASK(0x65, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_0_M1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x9B, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_1_M2));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xA2, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_0_M0)
					| REGMASK(0x79, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_0_M1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x3D, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_1_M2));
			break;
		case SPEED_100000:
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL_INTVL +
					MAC_CH_OFFSET(i),
					REGMASK(0x3FFF,
						NIC0_MAC_CH0_MAC_PCS_VENDOR_VL_INTVL_MARKER_COUNTER)
					);
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_PCS_MODE +
					MAC_CH_OFFSET(i), 0x0);
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xC1, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_0_M0)
					| REGMASK(0x68, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_0_M1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x21, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL0_1_M2));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x9D, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_0_M0)
					| REGMASK(0x71, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_0_M1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x8E, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL1_1_M2));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x59, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_0_M0)
					| REGMASK(0x4B, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_0_M1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0xE8, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL2_1_M2));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x4D, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_0_M0)
					| REGMASK(0x95, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_0_M1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x7B, NIC0_MAC_CH0_MAC_PCS_VENDOR_VL3_1_M2));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL4_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x7F5, NIC0_MAC_CH0_MAC_PCS_VL4_0_VL4_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL4_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x9, NIC0_MAC_CH0_MAC_PCS_VL4_1_VL4_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL5_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x14DD, NIC0_MAC_CH0_MAC_PCS_VL5_0_VL5_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL5_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0xC2, NIC0_MAC_CH0_MAC_PCS_VL5_1_VL5_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL6_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x4A9A, NIC0_MAC_CH0_MAC_PCS_VL6_0_VL6_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL6_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x26, NIC0_MAC_CH0_MAC_PCS_VL6_1_VL6_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL7_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x457B, NIC0_MAC_CH0_MAC_PCS_VL7_0_VL7_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL7_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x66, NIC0_MAC_CH0_MAC_PCS_VL7_1_VL7_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL8_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x24A0, NIC0_MAC_CH0_MAC_PCS_VL8_0_VL8_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL8_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x76, NIC0_MAC_CH0_MAC_PCS_VL8_1_VL8_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL9_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xC968, NIC0_MAC_CH0_MAC_PCS_VL9_0_VL9_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL9_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0xFB, NIC0_MAC_CH0_MAC_PCS_VL9_1_VL9_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL10_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x6CFD, NIC0_MAC_CH0_MAC_PCS_VL10_0_VL10_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL10_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x99, NIC0_MAC_CH0_MAC_PCS_VL10_1_VL10_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL11_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x91B9, NIC0_MAC_CH0_MAC_PCS_VL11_0_VL11_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL11_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x55, NIC0_MAC_CH0_MAC_PCS_VL11_1_VL11_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL12_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xB95C, NIC0_MAC_CH0_MAC_PCS_VL12_0_VL12_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL12_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0xB2, NIC0_MAC_CH0_MAC_PCS_VL12_1_VL12_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL13_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xF81A, NIC0_MAC_CH0_MAC_PCS_VL13_0_VL13_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL13_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0xBD, NIC0_MAC_CH0_MAC_PCS_VL13_1_VL13_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL14_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xC783, NIC0_MAC_CH0_MAC_PCS_VL14_0_VL14_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL14_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0xCA, NIC0_MAC_CH0_MAC_PCS_VL14_1_VL14_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL15_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x3635, NIC0_MAC_CH0_MAC_PCS_VL15_0_VL15_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL15_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0xCD, NIC0_MAC_CH0_MAC_PCS_VL15_1_VL15_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL16_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x31C4, NIC0_MAC_CH0_MAC_PCS_VL16_0_VL16_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL16_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x4C, NIC0_MAC_CH0_MAC_PCS_VL16_1_VL16_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL17_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xD6AD, NIC0_MAC_CH0_MAC_PCS_VL17_0_VL17_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL17_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0xB7, NIC0_MAC_CH0_MAC_PCS_VL17_1_VL17_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL18_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0x665F, NIC0_MAC_CH0_MAC_PCS_VL18_0_VL18_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL18_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0x2A, NIC0_MAC_CH0_MAC_PCS_VL18_1_VL18_1));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL19_0 +
					MAC_CH_OFFSET(i),
					REGMASK(0xF0C0, NIC0_MAC_CH0_MAC_PCS_VL19_0_VL19_0));
			NIC_MACRO_WREG32(mmNIC0_MAC_CH0_MAC_PCS_VL19_1 +
					MAC_CH_OFFSET(i),
					REGMASK(0xE5, NIC0_MAC_CH0_MAC_PCS_VL19_1_VL19_1));
			break;
		default:
			dev_err(hdev->dev,
				"unknown NIC port %d speed %dMb/s, cannot set MAC XPCS\n",
				nic_port->port, nic_port->speed);
			return;
		}
	}
}

static void gaudi2_nic_enable_port_interrupts(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	/* enable RXE block interrupts. RXE SPI interrupts should stay masked out,
	 * they generate a lot of events which are not fatal errors
	 */
	NIC_WREG32(mmNIC0_RXE0_SEI_INTR_MASK, 0x0);

	/* enable TXS block interrupts */
	NIC_WREG32(mmNIC0_TXS0_INTERRUPT_MASK, 0x0);

	/* enable TXE block interrupts */
	NIC_WREG32(mmNIC0_TXE0_INTERRUPT_MASK, 0x0);

	/* enable QPC response error interrupts */
	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_RESP_ERR_MASK, 0x0);
}

static void gaudi2_nic_disable_port_interrupts(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	/* disable RXE block interrupts */
	NIC_WREG32(mmNIC0_RXE0_SEI_INTR_MASK, 0xF);

	/* disable TXS block interrupts */
	NIC_WREG32(mmNIC0_TXS0_INTERRUPT_MASK, 0xF);

	/* disable TXE block interrupts */
	NIC_WREG32(mmNIC0_TXE0_INTERRUPT_MASK, 0x7F);

	/* disable QPC response error interrupts */
	NIC_WREG32(mmNIC0_QPC0_INTERRUPT_RESP_ERR_MASK, 0x7F);
}

static void gaudi2_nic_hw_port_config(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_nic_port *nic_port = gaudi2_nic->nic_port;
	struct hl_device *hdev = gaudi2_nic->hdev;
	u64 mac_addr = 0;
	u32 port = nic_port->port;
	int i;

	for (i = 0 ; i < ETH_ALEN ; i++) {
		mac_addr <<= 8;
		mac_addr |= hdev->asic_prop.cpucp_nic_info.mac_addrs[port].mac_addr[i];
	}

	/* TXS Configuration */
	gaudi2_nic_config_port_hw_txs(gaudi2_nic);

	/* TXE Configuration */
	gaudi2_nic_config_port_hw_txe(gaudi2_nic, mac_addr);

	/* QPC Configuration */
	gaudi2_nic_config_port_hw_qpc(gaudi2_nic);

	/* RXE Configuration */
	gaudi2_nic_config_port_hw_rxe(gaudi2_nic);

	/* MAC filtering */
	gaudi2_nic_config_hw_mac_filter(gaudi2_nic, mac_addr);

	/* Lanes Configuration */
	gaudi2_nic_hw_mac_port_config(gaudi2_nic);

	/* PFC Configuration */
	gaudi2_nic_set_pfc(nic_port);

	/* Enable port GIC interrupts - required only if running on PLDM */
	if (hdev->pldm)
		gaudi2_nic_enable_port_interrupts(nic_port);
}

void gaudi2_nic_hw_mac_loopback_cfg(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_nic_port *nic_port;
	struct hl_device *hdev;
	u32 port, val;

	nic_port = gaudi2_nic->nic_port;
	hdev = nic_port->hdev;
	port = nic_port->port;
	val = !!nic_port->mac_loopback;

	if (port & 1) {
		/* odd ports use lanes 2,3 */
		NIC_MACRO_RMWREG32(mmNIC0_MAC_CH2_MAC_PCS_CONTROL1, val,
				   NIC0_MAC_CH0_MAC_PCS_CONTROL1_LOOPBACK_MASK);
		NIC_MACRO_RMWREG32(mmNIC0_MAC_CH3_MAC_PCS_CONTROL1, val,
				   NIC0_MAC_CH0_MAC_PCS_CONTROL1_LOOPBACK_MASK);
	} else {
		/* even ports use lanes 0,1 */
		NIC_MACRO_RMWREG32(mmNIC0_MAC_CH0_MAC_PCS_CONTROL1, val,
				   NIC0_MAC_CH0_MAC_PCS_CONTROL1_LOOPBACK_MASK);
		NIC_MACRO_RMWREG32(mmNIC0_MAC_CH1_MAC_PCS_CONTROL1, val,
				   NIC0_MAC_CH0_MAC_PCS_CONTROL1_LOOPBACK_MASK);
	}

	/* flush cfg */
	NIC_MACRO_RREG32(mmNIC0_MAC_CH0_MAC_PCS_CONTROL1);
}

static void gaudi2_nic_qp_pre_destroy(struct hl_qp *qp)
{
	struct hl_nic_port *nic_port = qp->nic_port;
	struct gaudi2_nic_port *gaudi2_nic = nic_port->nic_specific;

	mutex_lock(&gaudi2_nic->qp_destroy_lock);

	/* only the first QP should enable MAC loopback */
	if (++gaudi2_nic->qp_destroy_cnt == 1 && !nic_port->mac_loopback && !nic_port->pcs_link) {
		nic_port->mac_loopback = true;
		gaudi2_nic_hw_mac_loopback_cfg(gaudi2_nic);
		gaudi2_nic->qp_destroy_mac_lpbk = true;
	}

	mutex_unlock(&gaudi2_nic->qp_destroy_lock);
}

static void gaudi2_nic_qp_post_destroy(struct hl_qp *qp)
{
	struct hl_nic_port *nic_port = qp->nic_port;
	struct gaudi2_nic_port *gaudi2_nic = nic_port->nic_specific;

	mutex_lock(&gaudi2_nic->qp_destroy_lock);

	/* only the last QP should disable MAC loopback */
	if (!--gaudi2_nic->qp_destroy_cnt && gaudi2_nic->qp_destroy_mac_lpbk) {
		nic_port->mac_loopback = false;
		gaudi2_nic_hw_mac_loopback_cfg(gaudi2_nic);
		gaudi2_nic->qp_destroy_mac_lpbk = false;
	}

	mutex_unlock(&gaudi2_nic->qp_destroy_lock);
}

static void gaudi2_nic_hw_config(struct gaudi2_nic_port *gaudi2_nic)
{
	gaudi2_nic_hw_port_config(gaudi2_nic);
	gaudi2_nic_hw_mac_loopback_cfg(gaudi2_nic);
}

/* TODO: unify to hl_nic_eq_handler_func: SW-82998 */
static void gaudi2_nic_eq_handler_func(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_nic_port *nic_port = gaudi2_nic->nic_port;
	struct gaudi2_device *gaudi2;
	struct hl_aux_dev *aux_dev;
	struct hl_device *hdev;
	struct hl_nic_eqe eqe;
	u32 port;

	hdev = gaudi2_nic->hdev;
	aux_dev = &hdev->nic.en_aux_dev;
	gaudi2 = hdev->asic_specific;
	port = nic_port->port;

	mutex_lock(&nic_port->control_lock);

	while (!hl_nic_eq_dispatcher_dequeue(nic_port, HL_KERNEL_ASID_ID, &eqe, false)) {
		if (!EQE_IS_VALID(&eqe)) {
			dev_warn_ratelimited(hdev->dev, "Port-%d got invalid EQE on EQ!\n", port);
			continue;
		}

		gaudi2->en_aux_ops.handle_eqe(aux_dev, port, &eqe);
	}

	mutex_unlock(&nic_port->control_lock);
}

static int gaudi2_nic_port_hw_init(struct hl_nic_port *nic_port)
{
	struct gaudi2_nic_port *gaudi2_nic = nic_port->nic_specific;
	struct hl_device *hdev = gaudi2_nic->hdev;
	u32 port = nic_port->port;
	int rc;

	gaudi2_nic_reset_rings(gaudi2_nic);

	/* register the Eth CQ with the event dispatcher */
	rc = hl_nic_eq_dispatcher_register_cq(nic_port, gaudi2_nic->cq_rings[NIC_CQ_RAW_IDX].asid,
						NIC_CQ_RAW_IDX);
	if (rc) {
		dev_err(hdev->dev, "failed to register port %d CQ %d with nic_eq_sw\n", port,
			NIC_CQ_RAW_IDX);
		goto cq_register_fail;
	}

	gaudi2_nic_hw_config(gaudi2_nic);

	gaudi2_nic_eq_handler_register(gaudi2_nic, gaudi2_nic_eq_handler_func);

	rc = gaudi2_qp_sanity_init(gaudi2_nic);
	if (rc) {
		dev_err(hdev->dev, "Failed to init NIC QP sanity, port: %d, %d\n", port, rc);
		goto cq_init_fail;
	}

	return 0;

cq_init_fail:
	gaudi2_nic_eq_handler_unregister(gaudi2_nic);
	hl_nic_eq_dispatcher_unregister_cq(nic_port, NIC_CQ_RAW_IDX);
cq_register_fail:

	return rc;
}

static void gaudi2_nic_port_hw_fini(struct hl_nic_port *nic_port)
{
	struct gaudi2_nic_port *gaudi2_nic = nic_port->nic_specific;
	struct hl_device *hdev = gaudi2_nic->hdev;

	/* Disable port GIC interrupts - required only if running on PLDM */
	if (hdev->pldm)
		gaudi2_nic_disable_port_interrupts(nic_port);

	gaudi2_qp_sanity_fini(gaudi2_nic);

	gaudi2_nic_eq_handler_unregister(gaudi2_nic);

	hl_nic_eq_dispatcher_unregister_cq(nic_port, NIC_CQ_RAW_IDX);
	hl_nic_eq_dispatcher_unregister_db(nic_port, GAUDI2_DB_FIFO_SECURE_HW_ID);

	hl_nic_eq_dispatcher_reset(nic_port);
#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "link down, port %d\n", nic_port->port);
#endif
}

/* must be called under mutex_lock(&nic_port->qpc_lock) */
static int gaudi2_nic_qpc_op(struct hl_nic_port *nic_port, u64 ctrl, bool wait_for_completion)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	u32 status, port = nic_port->port;
	int rc = 0;

	NIC_WREG32(mmNIC0_QPC0_GW_CTRL, ctrl);

	NIC_WREG32(mmNIC0_QPC0_GW_BUSY, 1);

	/* do not poll on registers when reset was initiated by FW */
	if (wait_for_completion && !hdev->reset_info.fw_reset)
		rc = hl_poll_timeout(
			hdev,
			mmNIC0_QPC0_GW_BUSY + NIC_CFG_BASE(port, mmNIC0_QPC0_GW_BUSY),
			status,
			!status,
			1000,
			nic_props->nic_qpc_cache_inv_timeout);

	return rc;
}

static int gaudi2_nic_qpc_write_masked(struct hl_nic_port *nic_port, const void *qpc_data,
					const struct qpc_mask *qpc_mask, u32 qpn, bool is_req,
					bool force_doorbell)
{
	struct hl_device *hdev;
	const u32 *mask, *data;
	u32 port, data_size;
	int i, rc = 0;
	u64 ctrl;

	hdev = nic_port->hdev;
	port = nic_port->port;
	data_size = is_req ? sizeof(struct gaudi2_qpc_requester) :
								sizeof(struct gaudi2_qpc_responder);
	mask = (const u32 *) qpc_mask;
	data = qpc_data;

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

	/* Copy the mask and data to the gateway regs.
	 * Only the data bits with their corresponding mask-bits set will be written
	 * to the HW.
	 */
	for (i = 0 ; i < (sizeof(struct qpc_mask) / sizeof(u32)) ; i++)
		NIC_WREG32(mmNIC0_QPC0_GW_MASK_0 + i * sizeof(u32), mask[i]);

	for (i = 0 ; i < (data_size / sizeof(u32)) ; i++)
		NIC_WREG32(mmNIC0_QPC0_GW_DATA_0 + i * sizeof(u32), data[i]);

	ctrl = (is_req << NIC0_QPC0_GW_CTRL_REQUESTER_SHIFT) | qpn |
			(!!force_doorbell << NIC0_QPC0_GW_CTRL_DOORBELL_FORCE_SHIFT);

	rc = gaudi2_nic_qpc_op(nic_port, ctrl, true);
	if (rc && hl_device_operational(hdev, NULL))
		/* Device might not respond during reset if the reset was due to error */
		dev_err(hdev->dev, "%s QPC GW write timeout, port: %d, qpn: %u\n",
				is_req ? "requester" : "responder", port, qpn);

	return rc;
}

static int gaudi2_nic_qpc_write(struct hl_nic_port *nic_port, void *qpc, struct qpc_mask *qpc_mask,
					u32 qpn, bool is_req)
{
	struct qpc_mask mask = {};
	u32 data_size = is_req ? sizeof(struct gaudi2_qpc_requester) :
								sizeof(struct gaudi2_qpc_responder);

	if (!qpc_mask) {
		/* NULL mask flags full QPC write */
		memset(&mask, 0xFF, data_size);
		qpc_mask = &mask;
	}

	return gaudi2_nic_qpc_write_masked(nic_port, qpc, qpc_mask, qpn, is_req, false);
}

static int gaudi2_nic_qpc_invalidate(struct hl_nic_port *nic_port, u32 qpn, bool is_req)
{
	struct hl_device *hdev = nic_port->hdev;
	struct qpc_mask mask = {};
	struct gaudi2_qpc_requester req_qpc = {};
	struct gaudi2_qpc_responder res_qpc = {};
	u32 port = nic_port->port;
	void *qpc;
	int rc;

	if (is_req) {
		/* use Congestion window mode with RTT state disabled &
		 * window size 0 to force REQ Tx stop, while Rx remains
		 * active.
		 */
		REQ_QPC_SET_CONGESTION_MODE(mask, 3);
		REQ_QPC_SET_RTT_STATE(mask, 3);
		REQ_QPC_SET_CONGESTION_WIN(mask, GENMASK(23, 0));

		REQ_QPC_SET_CONGESTION_MODE(req_qpc, 2);
		REQ_QPC_SET_RTT_STATE(req_qpc, 0);
		REQ_QPC_SET_CONGESTION_WIN(req_qpc, 0);
		qpc = &req_qpc;
	} else {
		RES_QPC_SET_VALID(mask, 1);
		RES_QPC_SET_VALID(res_qpc, 0);
		qpc = &res_qpc;
	}

	rc = gaudi2_nic_qpc_write_masked(nic_port, qpc, &mask, qpn, is_req, false);

	if (is_req) {
		/* Invalidate RXE WQE cache */
		NIC_RMWREG32(mmNIC0_RXE0_CACHE_CFG, 1, NIC0_RXE0_CACHE_CFG_INVALIDATION_MASK);
		NIC_RREG32(mmNIC0_RXE0_CACHE_CFG);

		NIC_RMWREG32(mmNIC0_RXE0_CACHE_CFG, 0, NIC0_RXE0_CACHE_CFG_INVALIDATION_MASK);
		NIC_RREG32(mmNIC0_RXE0_CACHE_CFG);

		/* H/W bug H6-3379: the TXE WQ cache is disabled, thus no need to invalidate it */
	}

	return rc;
}

static int gaudi2_nic_qpc_clear(struct hl_nic_port *nic_port, u32 qpn, bool is_req)
{
	struct qpc_mask mask;
	struct gaudi2_qpc_requester req_qpc = {};
	struct gaudi2_qpc_responder res_qpc = {};
	void *qpc = is_req ? (void *) &req_qpc : (void *) &res_qpc;

	memset(&mask, 0xFF, sizeof(mask));
	return gaudi2_nic_qpc_write_masked(nic_port, qpc, &mask, qpn, is_req, false);
}

int gaudi2_nic_qpc_read(struct hl_nic_port *nic_port, void *qpc, u32 qpn, bool is_req)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 *data, port, size;
	bool force_doorbell = false;
	int i, rc;
	u64 ctrl;

	port = nic_port->port;
	data = qpc;
	size = is_req ? sizeof(struct gaudi2_qpc_requester) : sizeof(struct gaudi2_qpc_responder);

	/* Don't write to the Gw if its busy with prev operation */
	if (NIC_RREG32(mmNIC0_QPC0_GW_BUSY)) {
		if (hl_device_operational(hdev, NULL))
			dev_err(hdev->dev, "Cannot read from port %d QP %d %s QPC, GW is busy\n",
				port, qpn, is_req ? "requester" : "responder");

		/* Since the device doesn't function once simulator terminated, we would like to
		 * avoid any device failure prints that followed.
		 */
		return (hdev->pdev || !hdev->device_fini_pending) ? -EBUSY : 0;
	}

	/* Clear the mask gateway regs which will cause the operation to be a read */
	for (i = 0 ; i < QPC_GW_MASK_REG_NUM ; i++)
		NIC_WREG32(mmNIC0_QPC0_GW_MASK_0 + i * sizeof(u32), 0);

	ctrl = (is_req << NIC0_QPC0_GW_CTRL_REQUESTER_SHIFT) | qpn |
			(!!force_doorbell << NIC0_QPC0_GW_CTRL_DOORBELL_FORCE_SHIFT);
	rc = gaudi2_nic_qpc_op(nic_port, ctrl, true);
	if (rc)
		return rc;

	for (i = 0 ; i < size / sizeof(u32) ; i++)
		data[i] = NIC_RREG32(mmNIC0_QPC0_GW_DATA_0 + i * sizeof(u32));

	return 0;
}

static int gaudi2_nic_qpc_query(struct hl_nic_port *nic_port, u32 qpn, bool is_req,
				struct hl_nic_qpc_attr *attr)
{
	struct hl_device *hdev = nic_port->hdev;
	struct gaudi2_qpc_requester req_qpc;
	struct gaudi2_qpc_responder res_qpc;
	u32 port = nic_port->port;
	int rc;

	if (is_req) {
		rc = gaudi2_nic_qpc_read(nic_port, (void *) &req_qpc, qpn, is_req);
		if (rc)
			goto out_err;

		attr->valid = REQ_QPC_GET_VALID(req_qpc);
		attr->in_work = REQ_QPC_GET_IN_WORK(req_qpc);
		attr->error = REQ_QPC_GET_ERROR(req_qpc);
	} else {
		rc = gaudi2_nic_qpc_read(nic_port, (void *) &res_qpc, qpn, is_req);
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

int gaudi2_nic_wqe_read(struct hl_nic_port *nic_port, void *wqe, u32 qpn,
				u32 wqe_idx, bool is_tx)
{
	u64 wq_base_addr_upper, wq_base_addr_lower, wq_size_cline_log, ctrl,
	req_qpc_base_addr_upper, req_qpc_base_addr_lower, wqe_offset;
	u32 *data, port, wqe_cline_idx, num_of_wqe_in_cline;
	struct hl_device *hdev = nic_port->hdev;
	int i, rc;

	port = nic_port->port;
	data = wqe;

	if (is_tx) {
		wq_base_addr_upper = NIC_RREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_63_32_1);
		wq_base_addr_lower = NIC_RREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_31_0_1);
		wq_size_cline_log = NIC_RREG32(mmNIC0_TXE0_LOG_MAX_WQ_SIZE_1);
		num_of_wqe_in_cline = TX_WQE_NUM_IN_CLINE;
	} else {
		wq_base_addr_upper = NIC_RREG32(mmNIC0_RXE0_WIN1_WQ_BASE_HI);
		wq_base_addr_lower = NIC_RREG32(mmNIC0_RXE0_WIN1_WQ_BASE_LO);
		wq_size_cline_log = NIC_RREG32(mmNIC0_RXE0_WIN1_WQ_MISC);
		num_of_wqe_in_cline = RX_WQE_NUM_IN_CLINE;
	}
	wqe_cline_idx = wqe_idx / num_of_wqe_in_cline;

	req_qpc_base_addr_upper = NIC_RREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_63_32_1);
	req_qpc_base_addr_lower = NIC_RREG32(mmNIC0_QPC0_REQ_BASE_ADDRESS_31_7);

	/* Don't write to the Gw if its busy with prev operation */
	if (NIC_RREG32(mmNIC0_QPC0_GW_BUSY)) {
		if (hl_device_operational(hdev, NULL))
			dev_err(hdev->dev,
				"Cannot read wqe from port %d QP %d, GW is busy\n", port, qpn);

		return -EBUSY;
	}

	/* Hacking the QPC base address to read WQ */
	NIC_WREG32(mmNIC0_QPC0_REQ_BASE_ADDRESS_63_32, wq_base_addr_upper);
	NIC_WREG32(mmNIC0_QPC0_REQ_BASE_ADDRESS_31_7, wq_base_addr_lower >> 7);

	/* Clear the mask gateway regs which will cause the operation to be a read */
	for (i = 0 ; i < QPC_GW_MASK_REG_NUM ; i++)
		NIC_WREG32(mmNIC0_QPC0_GW_MASK_0 + i * sizeof(u32), 0);

	/* Calculate the WQE offset in cache line units */
	wqe_offset = (1ULL << wq_size_cline_log) * qpn + wqe_cline_idx;
	ctrl = NIC0_QPC0_GW_CTRL_REQUESTER_MASK + wqe_offset;
	rc = gaudi2_nic_qpc_op(nic_port, ctrl, true);
	if (rc)
		goto exit;

	/* H/W reads in cache line size */
	for (i = 0 ; i < 32 ; i++)
		data[i] = NIC_RREG32(mmNIC0_QPC0_GW_DATA_0 + i * sizeof(u32));

exit:
	/* Restore the configuration */
	NIC_WREG32(mmNIC0_QPC0_REQ_BASE_ADDRESS_63_32, req_qpc_base_addr_upper);
	NIC_WREG32(mmNIC0_QPC0_REQ_BASE_ADDRESS_31_7, req_qpc_base_addr_lower);

	return rc;
}

static bool is_valid_mtu(u16 mtu)
{
	return (mtu == SZ_1K) || (mtu == SZ_2K) || (mtu == SZ_4K) || (mtu == SZ_8K);
}

static int normalize_priority(struct hl_device *hdev, u32 priority,
				enum hl_ts_type type, bool is_req, u32 *norm_priority)
{
	/* Ethernet and Responder get the highest priority */
	if (!is_req || (type == TS_RAW)) {
		*norm_priority = GAUDI2_PFC_PRIO_DRIVER;
		return 0;
	}

	/* Req priority can vary from 1 to 3 */
	if ((priority < GAUDI2_PFC_PRIO_USER_BASE) || (priority >= HL_EN_PFC_PRIO_NUM))
		return -EINVAL;

	*norm_priority = priority;
	return 0;
}

static u32 gaudi2_nic_txs_get_schedq_num(u32 priority, bool is_req)
{
	u32 prio_q_group;

	/* prio-group numbering start from 1 - normalize it to Zero */
	prio_q_group = (is_req ? TXS_PORT_REQ_SCHED_Q : TXS_PORT_RES_SCHED_Q);

	return prio_q_group * HL_EN_PFC_PRIO_NUM + priority;
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

static void gaudi2_default_encap_set(struct hl_nic_port *nic_port, u32 *encap_id, u32 src_ip)
{
	struct hl_nic_encap_idr_pdata encap_data;
	uint8_t dummy_hdr[NIC_MAX_TNL_HDR_SIZE] = {};

	gaudi2_get_default_encap_id(nic_port, encap_id);

	if (!src_ip && get_src_ip(nic_port, &src_ip)) {
		dev_dbg(nic_port->hdev->dev, "failed to get interface IP, using 0\n");
		src_ip = 0;
	}

	memset(dummy_hdr, 0xa5, sizeof(dummy_hdr));

	encap_data.port = nic_port->port;
	encap_data.id = *encap_id;
	encap_data.src_ip = src_ip;
	encap_data.encap_type = HL_NIC_ENCAP_OVER_UDP;
	encap_data.encap_type_data = DUMMY_UDP_PORT;
	encap_data.encap_header = dummy_hdr;
	encap_data.encap_header_size = sizeof(dummy_hdr);

	gaudi2_encap_set(nic_port, *encap_id, &encap_data);
}

static int gaudi2_set_req_qp_ctx(struct hl_device *hdev,
				struct hl_nic_req_conn_ctx_in *in,
				struct hl_qp *qp)
{
	struct gaudi2_qpc_requester req_qpc;
	struct gaudi2_nic_port *gaudi2_nic;
	u8 mac[ETH_ALEN], cqn, encap_en;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_user_cq *user_cq;
	struct hl_en_aux_ops *aux_ops;
	struct hl_nic_port *nic_port;
	struct hl_aux_dev *aux_dev;
	u32 port, priority, encap_id;
	int rc;

	port = in->port;
	nic_port = &nic->nic_ports[port];
	gaudi2_nic = nic_port->nic_specific;
	aux_dev = &nic->en_aux_dev;
	aux_ops = aux_dev->aux_ops;

	/* In case user didn't set encap, unset for internal ports. */
	encap_id = 0;
	encap_en = 0;

	/* SW-61290: Enforce sender (remote) WQ to be at least 4 times bigger than receiver WQ to
	 * avoid H/W bug - RDV roll-back may stuck the QP.
	 */
	if (in->wq_type == QPC_REQ_WQ_TYPE_WRITE && in->wq_remote_log_size &&
		(in->wq_size * 4 > BIT(in->wq_remote_log_size))) {
		dev_dbg(hdev->dev, "Invalid RDV WQ size. local %d, remote %lu, port %d\n",
			in->wq_size, BIT(in->wq_remote_log_size), port);
		return -EINVAL;
	}

	if (in->mtu && !is_valid_mtu(in->mtu)) {
		dev_dbg(hdev->dev, "MTU of %u is not supported, port %d\n", in->mtu, port);
		return -EINVAL;
	}

	if (normalize_priority(hdev, in->priority, TS_RC, true, &priority)) {
		dev_dbg(hdev->dev, "Unsupported priority value %u, port %d\n", in->priority, port);
		return -EINVAL;
	}

	/* H6-3399: Below configuration isn't valid due to H/W bug, i.e.: using encap_id for src IP
	 * settings w/o encapsulation isn't allowed.
	 */
	if (!in->encap_en && in->encap_id) {
		dev_dbg(hdev->dev,
			"Encapsulation ID %d can't be set when encapsulation disable, port %d\n",
			in->encap_id, port);
		return -EINVAL;
	}

	/* Due to H/W bug H6-3280, it was decided to allow congestion control for external ports
	 * only - the user shouldn't enable it for internal ports.
	 */
	if (!nic_port->eth_enable && in->congestion_en) {
		dev_err(hdev->dev, "congestion control should be disabled for internal ports, port %d mode %u\n",
			port, in->congestion_en);
		return -EINVAL;
	}

	if (in->cq_number) {
		/* User CQ. */
		cqn = in->cq_number;

		user_cq = hl_nic_user_cq_get(nic_port, cqn);
		if (!user_cq) {
			dev_dbg(hdev->dev, "CQ %d is invalid, port %d\n", cqn, port);
			return -EINVAL;
		}

		qp->req_user_cq = user_cq;
	} else {
		/* No CQ. */
		cqn = NIC_CQ_RDMA_IDX;
	}

	if (nic_port->eth_enable)
		memcpy(mac, in->dst_mac_addr, ETH_ALEN);
	else
		/* in this case the MAC is irrelevant so use broadcast */
		eth_broadcast_addr(mac);

	memset(&req_qpc, 0, sizeof(req_qpc));

	REQ_QPC_SET_DST_QP(req_qpc, in->dst_conn_id);
	REQ_QPC_SET_PORT(req_qpc, 0); /* Always select lane 0 */
	REQ_QPC_SET_PRIORITY(req_qpc, priority);
	REQ_QPC_SET_RKEY(req_qpc, qp->remote_key);
	REQ_QPC_SET_DST_IP(req_qpc, in->dst_ip_addr);
	REQ_QPC_SET_DST_MAC_LSB(req_qpc, *(u32 *) mac);
	REQ_QPC_SET_DST_MAC_MSB(req_qpc, *(u16 *) (mac + 4));

	REQ_QPC_SET_SCHD_Q_NUM(req_qpc,
			gaudi2_nic_txs_get_schedq_num(priority, true));
	REQ_QPC_SET_TM_GRANULARITY(req_qpc, in->timer_granularity);

	REQ_QPC_SET_TRANSPORT_SERVICE(req_qpc, TS_RC);
	REQ_QPC_SET_BURST_SIZE(req_qpc, QPC_REQ_BURST_SIZE);
	REQ_QPC_SET_LAST_IDX(req_qpc, in->wq_size - 1);

	REQ_QPC_SET_WQ_BASE_ADDR(req_qpc, 1);

	/* In case the user didn't specify MTU, set the one from netdev.
	 * If there is no netdev, use the default value.
	 */
	if (in->mtu) {
		qp->mtu = in->mtu;
		qp->mtu_type = MTU_FROM_USER;
	} else if (nic_port->eth_enable) {
		qp->mtu = aux_ops->get_mtu(aux_dev, port) + HL_EN_MAX_HEADERS_SZ;
		qp->mtu_type = MTU_FROM_NETDEV;
	} else {
		qp->mtu = GAUDI2_NIC_MTU_DEFAULT;
		qp->mtu_type = MTU_DEFAULT;
	}

	REQ_QPC_SET_MTU(req_qpc, ilog2(roundup_pow_of_two(qp->mtu)) - 10);

	/* SW-83137: mode GAUDI1 is not used and hence set to 0 */
	REQ_QPC_SET_MOD_GAUDI1(req_qpc, 0);
	REQ_QPC_SET_SWQ_GRANULARITY(req_qpc, in->swq_granularity);
	REQ_QPC_SET_CQ_NUM(req_qpc, cqn);

	/* Protect the HW from zero value */
	REQ_QPC_SET_REMOTE_WQ_LOG_SZ(req_qpc, in->wq_remote_log_size ? in->wq_remote_log_size : 2);

	/* config MMU-BP */
	REQ_QPC_SET_DATA_MMU_BYPASS(req_qpc, 0);

	/* ASID is also used as protection-domain, so always configure it */
	REQ_QPC_SET_ASID(req_qpc, qp->asid);

	REQ_QPC_SET_ACKREQ_FREQ(req_qpc, 8);
	REQ_QPC_SET_WQ_TYPE(req_qpc, in->wq_type);

	/* user QP - unsecured trust level */
	REQ_QPC_SET_TRUST_LEVEL(req_qpc, UNSECURED);

	/* Congestion control configurations are done for external ports only - for internal ports
	 * congestion control will be disabled.
	 */
	if (nic_port->eth_enable) {
		u32 congestion_wnd;

		if (in->congestion_wnd > GAUDI2_NIC_MAX_CONG_WND) {
			dev_dbg(hdev->dev,
				"Congestion window size(%u) can't be > max allowed size(%u), port %d\n",
				in->congestion_wnd, GAUDI2_NIC_MAX_CONG_WND, port);
		return -EINVAL;
		}

		/*
		 * congestion_mode:
		 * 0: no congestion
		 * 1: congestion control (BBR/SWIFT)
		 * 2: congestion window
		 *
		 * REQ_QPC_SET_CONGESTION_MODE set those modes.
		 * REQ_QPC_SET_RTT_STATE enable the CC-CQ mechanism (relevant for BBR/SWIFT only).
		 * when user does not set congestion_en we set congestion to mode 2
		 * so we still have cc via the CONGESTION_WIN.
		 */
		REQ_QPC_SET_CONGESTION_MODE(req_qpc, (in->congestion_en) ? 1 : 2);

		REQ_QPC_SET_RTT_STATE(req_qpc, in->congestion_en);

		congestion_wnd = in->congestion_wnd ? in->congestion_wnd : GAUDI2_NIC_MAX_CONG_WND;
		REQ_QPC_SET_CONGESTION_WIN(req_qpc, congestion_wnd);
	}

	if (in->encap_en) {
		encap_id = in->encap_id;
		encap_en = in->encap_en;
	} else if (nic_port->eth_enable) {
		gaudi2_get_default_encap_id(nic_port, &encap_id);
		encap_en = 1;
	}

	REQ_QPC_SET_ENCAP_ENABLE(req_qpc, encap_en);
	REQ_QPC_SET_ENCAP_TYPE(req_qpc, encap_id);

	REQ_QPC_SET_VALID(req_qpc, 1);

	rc = gaudi2_nic_qpc_write(nic_port, &req_qpc, NULL, in->conn_id, true);
	if (rc)
		goto qpc_write_fail;

	if (gaudi2_nic->advanced && (in->wq_size < gaudi2_nic->min_qp_size)) {
		gaudi2_nic->min_qp_size = in->wq_size;

		/*
		 * The back-pressure thresholds values describe the occupancy of the QP,
		 * thus should be configured to be the size of the smallest QP minus some
		 * defined numbers (currently 4/8 for the upper/lower thresholds
		 * respectively).
		 */
		NIC_WREG32(mmNIC0_QPC0_WQ_UPPER_THRESHOLD,
			gaudi2_nic->min_qp_size - GAUDI2_NIC_WTD_BP_UPPER_TH_DIFF);
		NIC_WREG32(mmNIC0_QPC0_WQ_LOWER_THRESHOLD,
			gaudi2_nic->min_qp_size - GAUDI2_NIC_WTD_BP_LOWER_TH_DIFF);
	}

	return 0;

qpc_write_fail:
	if (qp->req_user_cq) {
		hl_nic_user_cq_put(qp->req_user_cq);
		qp->req_user_cq = NULL;
	}

	return rc;
}

static int gaudi2_set_res_qp_ctx(struct hl_device *hdev,
				struct hl_nic_res_conn_ctx_in *in,
				struct hl_qp *qp)
{

	u32 port = in->port, priority, encap_id;
	struct gaudi2_qpc_responder res_qpc;
	u8 mac[ETH_ALEN], cqn, encap_en, wq_mmu_bypass;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_user_cq *user_cq;
	struct hl_nic_port *nic_port;
	int rc;

	nic_port = &nic->nic_ports[port];

	/* In case user didn't set encap, unset for internal ports. */
	encap_id = 0;
	encap_en = 0;

	/* H6-3399: Below configuration isn't valid due to H/W bug, i.e.: using encap_id for src IP
	 * settings w/o encapsulation isn't allowed.
	 */
	if (!in->encap_en && in->encap_id) {
		dev_dbg(hdev->dev,
			"Encapsulation ID %d can't be set when encapsulation disable, port %d\n",
			in->encap_id, port);
		return -EINVAL;
	}

	if (nic_port->eth_enable)
		memcpy(mac, in->dst_mac_addr, ETH_ALEN);
	else
		/* in this case the MAC is irrelevant so use broadcast */
		eth_broadcast_addr(mac);

	if (normalize_priority(hdev, in->priority, TS_RC, false, &priority)) {
		dev_dbg(hdev->dev, "Unsupported priority value %u, port %d\n", in->priority, port);
		return -EINVAL;
	}

	if (in->cq_number) {
		/* User CQ. */
		cqn = in->cq_number;

		user_cq = hl_nic_user_cq_get(nic_port, cqn);
		if (!user_cq) {
			dev_dbg(hdev->dev, "CQ %d is invalid, port %d\n", cqn, port);
			return -EINVAL;
		}

		qp->res_user_cq = user_cq;
	} else {
		/* No CQ. */
		cqn = NIC_CQ_RDMA_IDX;
	}

	memset(&res_qpc, 0, sizeof(res_qpc));

	RES_QPC_SET_DST_QP(res_qpc, in->dst_conn_id);
	RES_QPC_SET_PORT(res_qpc, 0); /* Always select lane 0 */
	RES_QPC_SET_PRIORITY(res_qpc, priority);
	RES_QPC_SET_LKEY(res_qpc, qp->local_key);
	RES_QPC_SET_DST_IP(res_qpc, in->dst_ip_addr);
	RES_QPC_SET_DST_MAC_LSB(res_qpc, *(u32 *) mac);
	RES_QPC_SET_DST_MAC_MSB(res_qpc, *(u16 *) (mac + 4));

	RES_QPC_SET_TRANSPORT_SERVICE(res_qpc, TS_RC);

	/* config MMU-BP
	 * In RDV QPs, the responded side is not used for 'real' user data but
	 * rather to pass WQEs as data, therefore the QPC MMU-BP attribute shall
	 * be taken according to the configuration of the WQ array.
	 */
	wq_mmu_bypass = nic_port->wq_arr_props[HL_NIC_USER_WQ_SEND].wq_mmu_bypass;

	if (!(in->rdv && wq_mmu_bypass))
		RES_QPC_SET_DATA_MMU_BYPASS(res_qpc, 0);
	else
		RES_QPC_SET_DATA_MMU_BYPASS(res_qpc, 1);

	/* ASID is also used as protection-domain, so always configure it */
	RES_QPC_SET_ASID(res_qpc, qp->asid);

	RES_QPC_SET_PEER_QP(res_qpc, in->conn_peer);
	RES_QPC_SET_SCHD_Q_NUM(res_qpc,
			gaudi2_nic_txs_get_schedq_num(priority, false));

	/* for rdv QPs RXE responder takes its security-level from QPC */
	if (in->rdv)
		RES_QPC_SET_TRUST_LEVEL(res_qpc, SECURED);
	else
		RES_QPC_SET_TRUST_LEVEL(res_qpc, UNSECURED);

	/* SW-83137: mode GAUDI1 is not used and hence set to 0 */
	RES_QPC_SET_MOD_GAUDI1(res_qpc, 0);

	RES_QPC_SET_CQ_NUM(res_qpc, cqn);
	RES_QPC_SET_PEER_WQ_GRAN(res_qpc, in->wq_peer_granularity);

	if (in->encap_en) {
		encap_id = in->encap_id;
		encap_en = in->encap_en;
	} else if (nic_port->eth_enable) {
		gaudi2_get_default_encap_id(nic_port, &encap_id);
		encap_en = 1;
	}

	RES_QPC_SET_ENCAP_ENABLE(res_qpc, encap_en);
	RES_QPC_SET_ENCAP_TYPE(res_qpc, encap_id);

	RES_QPC_SET_VALID(res_qpc, 1);

	rc = gaudi2_nic_qpc_write(nic_port, &res_qpc, NULL, in->conn_id, false);
	if (rc)
		goto qpc_write_fail;

	return 0;

qpc_write_fail:
	if (qp->res_user_cq) {
		hl_nic_user_cq_put(qp->res_user_cq);
		qp->res_user_cq = NULL;
	}

	return rc;
}

static int gaudi2_nic_update_qp_mtu(struct hl_nic_port *nic_port, struct hl_qp *qp, u32 mtu)
{
	struct gaudi2_qpc_requester req_qpc = {};
	struct qpc_mask mask = {};

	/* MTU field is 2 bits wide */
	REQ_QPC_SET_MTU(mask, 0x3);
	REQ_QPC_SET_MTU(req_qpc, ilog2(roundup_pow_of_two(mtu)) - 10);

	return gaudi2_nic_qpc_write_masked(nic_port, &req_qpc, &mask, qp->qp_id, true, false);
}

static int gaudi2_user_wq_arr_set(struct hl_device *hdev,
				struct hl_nic_user_wq_arr_set_in *in,
				struct hl_nic_user_wq_arr_set_out *out,
				struct hl_ctx *ctx)
{
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_nic_mem_data mem_data = {};
	struct gaudi2_nic_port *gaudi2_nic;
	struct gaudi2_device *gaudi2;
	struct hl_nic_port *nic_port;
	u64 wq_base_addr, wq_size_cline_log, wq_size, wq_arr_size, num_of_wqs,
		num_of_wq_entries;
	u32 wqe_size, rw_asid, type, port, wqe_asid, alignment_size;
	bool phys_addr = true;
	int rc;

	type = in->type;
	port = in->port;
	nic_port = &hdev->nic.nic_ports[port];
	gaudi2_nic = nic_port->nic_specific;
	gaudi2 = hdev->asic_specific;

	if (in->addr) {
		dev_dbg(hdev->dev, "WQ array address shouldn't be set: 0x%llx\n", in->addr);
		return -EINVAL;
	}

	wq_arr_props = &nic_port->wq_arr_props[type];
	num_of_wqs = in->num_of_wqs;

	if (wq_arr_props->is_send) {
		wqe_size = (in->swq_granularity == HL_NIC_SWQE_GRAN_64B) ?
				NIC_SEND_WQE_SIZE_MULTI_STRIDE : NIC_SEND_WQE_SIZE;
		nic_port->swqe_size = wqe_size;
	} else
		wqe_size = NIC_RECV_WQE_SIZE;

	if (in->mem_id == HL_NIC_MEM_HOST) {
		alignment_size = PAGE_SIZE / min(NIC_SEND_WQE_SIZE, NIC_RECV_WQE_SIZE);
		num_of_wq_entries = ALIGN(in->num_of_wq_entries, alignment_size);
		wq_size = num_of_wq_entries * wqe_size;
		wqe_asid = ctx->asid;
		wq_arr_props->wq_size = wq_size;
		wq_size_cline_log = ilog2(wq_size / DEVICE_CACHE_LINE_SIZE);
		mem_data.mem_id = HL_NIC_DRV_MEM_HOST_DMA_COHERENT;
	} else {
		num_of_wq_entries = in->num_of_wq_entries;
		wq_size = ALIGN(num_of_wq_entries * wqe_size, DEVICE_CACHE_LINE_SIZE);
		mem_data.mem_id = HL_NIC_DRV_MEM_DEVICE;
		mem_data.in.device_mem_data.port = port;
		mem_data.in.device_mem_data.type = type;
		wqe_asid = HL_KERNEL_ASID_ID;
		/* device wants the size in units of cache-line */
		wq_size_cline_log = ilog2((num_of_wq_entries * wqe_size) /
				DEVICE_CACHE_LINE_SIZE);
	}

	wq_arr_size = num_of_wqs * wq_size;

	/* We use the MMU whenever the WQ allocation is more than the 4MB DMA coherent memory
	 * constraint. We need not allocate memory if we are using MMU. We reserve the VA in the
	 * PMMU and allocate the actual memory inside set_req_qp_ctx and map to this virtual address
	 * space.
	 */
	if ((wq_arr_size > DMA_COHERENT_MAX_SIZE) && (in->mem_id == HL_NIC_MEM_HOST)) {
		if (!hdev->mmu_enable) {
			dev_dbg(hdev->dev,
				"MMU not enabled. For allocations greater than %llx, MMU needs to be enabled, wq_arr_size : 0x%llx, port: %d\n",
				(u64) DMA_COHERENT_MAX_SIZE, wq_arr_size, port);
			return -EINVAL;
		}
		phys_addr = false;

		wq_base_addr = hl_nic_reserve_wq_dva(hdev, ctx, nic_port, wq_arr_size, type);
		if (!wq_base_addr)
			return -ENOMEM;
	} else {
		mem_data.size = wq_arr_size;

		rc = hl_nic_mem_alloc(ctx, &mem_data);
		if (rc) {
			dev_dbg(hdev->dev, "Failed to allocate WQ: %d\n", rc);
			return rc;
		}

		wq_base_addr = mem_data.addr;
		wq_arr_props->handle = mem_data.handle;
	}

	wq_arr_props->on_device_mem = in->mem_id == HL_NIC_MEM_DEVICE;

	dev_dbg(hdev->dev,
		"port %d: WQ-> type:%u addr=0x%llx log_size:%llu wqe_asid:%u mmu_bp:%u\n",
		port, type, wq_base_addr, wq_size_cline_log, wqe_asid, phys_addr ? 1 : 0);

	if (wq_arr_props->is_send) {
		NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_63_32_1, upper_32_bits(wq_base_addr));
		NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_31_0_1, lower_32_bits(wq_base_addr));

		/* TODO: Support multi-app */
		NIC_WREG32(mmNIC0_TXE0_LOG_MAX_WQ_SIZE_1, wq_size_cline_log);

		/* configure WQ MMU
		 * currently user app has the index of 1
		 */
		if (phys_addr)
			NIC_WREG32(mmNIC0_TXE0_WQE_USER_CFG,
					NIC_RREG32(mmNIC0_TXE0_WQE_USER_CFG) | (1 << 1));
		else
			NIC_WREG32(mmNIC0_TXE0_WQE_USER_CFG,
					NIC_RREG32(mmNIC0_TXE0_WQE_USER_CFG) & ~(1 << 1));

		/* Set secured ASID config. The security is enabled for WQs on HBM such that it can
		 * be accessed only with process whose ASID is wqe_asid. Here we program ASID '0' so
		 * that only the NIC HW can access the WQs on HBM.
		 * For FW below 1.8, there is no option to unset the ASID. Hence we set ASID for
		 * both wq on host as well as HBM. This way, we override the previous settings. But
		 * from FW 1.8 onwards, there is a provision to unset the previous ASID settings.
		 * So, we set the ASID only in case of WQ on HBM and unset it in the wq_arr_unset
		 */
		if (gaudi2_is_fw_ver_below_1_8(hdev)) {
			rc = gaudi2_nic_config_wqe_asid(nic_port, wqe_asid, true);
			if (rc)
				goto set_asid_fail;
		} else {
			if (wqe_asid != ctx->asid) {
				rc = gaudi2_nic_config_wqe_asid(nic_port, wqe_asid, true);
				if (rc)
					goto set_asid_fail;
			}
		}

		rw_asid = (ctx->asid <<	ARC_FARM_KDMA_CTX_AXUSER_HB_ASID_RD_SHIFT) |
				(ctx->asid << ARC_FARM_KDMA_CTX_AXUSER_HB_ASID_WR_SHIFT);

		NIC_WREG32(mmNIC0_QM0_AXUSER_NONSECURED_HB_ASID, rw_asid);
		NIC_WREG32(mmNIC0_QM0_AXUSER_NONSECURED_HB_MMU_BP, phys_addr ? 0x1 : 0);

		if (gaudi2_nic->advanced) {
			/* enable override of asid */
			NIC_WREG32(mmNIC0_QPC0_AXUSER_TXWQE_LBW_QMAN_BP_HB_WR_OVRD_LO,
					0xfffff800);
			NIC_WREG32(mmNIC0_QPC0_AXUSER_TXWQE_LBW_QMAN_BP_HB_RD_OVRD_LO,
					0xfffff800);
			NIC_WREG32(mmNIC0_QPC0_AXUSER_TXWQE_LBW_QMAN_BP_HB_ASID, wqe_asid);
			/* Configure MMU-BP override for TX-WQs */
			NIC_WREG32(mmNIC0_QPC0_AXUSER_TXWQE_LBW_QMAN_BP_HB_MMU_BP,
					phys_addr ? 0x1 : 0);

			/* configure the QPC with the Tx WQ parameters */
			NIC_WREG32(mmNIC0_QPC0_TX_WQ_BASE_ADDR_63_32_1,
					upper_32_bits(wq_base_addr));
			NIC_WREG32(mmNIC0_QPC0_TX_WQ_BASE_ADDR_31_0_1,
					lower_32_bits(wq_base_addr));
			NIC_WREG32(mmNIC0_QPC0_LOG_MAX_TX_WQ_SIZE_1, wq_size_cline_log);
			NIC_WREG32(mmNIC0_QPC0_MMU_BYPASS_TX_WQ_1, phys_addr ? 0x1 : 0);

			/* rendezvous configuration for send work queue */
			NIC_WREG32(mmNIC0_RXE0_RDV_SEND_WQ_BASE_ADDR_HI,
				(upper_32_bits(wq_base_addr)));
			NIC_WREG32(mmNIC0_RXE0_RDV_SEND_WQ_BASE_ADDR_LO,
				lower_32_bits(wq_base_addr));
			NIC_WREG32(mmNIC0_RXE0_RDV_LOG_MAX_WQ_SIZE, wq_size_cline_log);

			if (num_of_wq_entries < gaudi2_nic->min_qp_size) {
				gaudi2_nic->min_qp_size = (u32) num_of_wq_entries;

				/*
				 * The back-pressure thresholds values describe the occupancy of
				 * the QP, thus should be configured to be the size of the smallest
				 * QP minus some defined numbers (currently 4/8 for the
				 * upper/lower thresholds respectively).
				 */
				NIC_WREG32(mmNIC0_QPC0_WQ_UPPER_THRESHOLD,
					gaudi2_nic->min_qp_size - GAUDI2_NIC_WTD_BP_UPPER_TH_DIFF);
				NIC_WREG32(mmNIC0_QPC0_WQ_LOWER_THRESHOLD,
					gaudi2_nic->min_qp_size - GAUDI2_NIC_WTD_BP_LOWER_TH_DIFF);
			}
		}
	} else {
		NIC_WREG32(mmNIC0_RXE0_WIN1_WQ_BASE_HI, upper_32_bits(wq_base_addr));
		NIC_WREG32(mmNIC0_RXE0_WIN1_WQ_BASE_LO, lower_32_bits(wq_base_addr));

		NIC_WREG32(mmNIC0_RXE0_WIN1_WQ_MISC, wq_size_cline_log);

		/* configure WQ MMU for RXE */
		if (phys_addr)
			NIC_WREG32(mmNIC0_RXE0_ARUSER_MMU_BP,
					NIC_RREG32(mmNIC0_RXE0_ARUSER_MMU_BP) | (1 << 1));
		else
			NIC_WREG32(mmNIC0_RXE0_ARUSER_MMU_BP,
					NIC_RREG32(mmNIC0_RXE0_ARUSER_MMU_BP) & ~(1 << 1));

		if (wqe_asid != ctx->asid) {
			/* enable override of asid bit before changing asid */
			NIC_WREG32(mmNIC0_RXE0_WQE_ARUSER_HB_RD_OVRD_LO, 0xFFFFFc00);
			/* change asid to secured asid */
			NIC_WREG32(mmNIC0_RXE0_WQE_ARUSER_HB_ASID, wqe_asid);
		} else
			NIC_WREG32(mmNIC0_RXE0_WQE_ARUSER_HB_RD_OVRD_LO, 0xFFFFFFFF);

		if (gaudi2_nic->advanced) {
			/* enable override of asid */
			NIC_WREG32(mmNIC0_QPC0_AXUSER_RXWQE_HB_WR_OVRD_LO, 0xFFFFF800);
			NIC_WREG32(mmNIC0_QPC0_AXUSER_RXWQE_HB_RD_OVRD_LO, 0xFFFFF800);
			NIC_WREG32(mmNIC0_QPC0_AXUSER_RXWQE_HB_ASID, wqe_asid);
			/* Configure MMU-BP override for RX-WQs */
			NIC_WREG32(mmNIC0_QPC0_AXUSER_RXWQE_HB_MMU_BP, phys_addr ? 0x1 : 0);

			/* configure the QPC with the Rx WQ parameters */
			NIC_WREG32(mmNIC0_QPC0_RX_WQ_BASE_ADDR_63_32_1,
					upper_32_bits(wq_base_addr));
			NIC_WREG32(mmNIC0_QPC0_RX_WQ_BASE_ADDR_31_0_1,
					lower_32_bits(wq_base_addr));
			NIC_WREG32(mmNIC0_QPC0_LOG_MAX_RX_WQ_SIZE_1, wq_size_cline_log);
			NIC_WREG32(mmNIC0_QPC0_MMU_BYPASS_RX_WQ_1, phys_addr ? 0x1 : 0);

			/* rendezvous configuration for receive work queue */
			NIC_WREG32(mmNIC0_RXE0_WIN0_WQ_BASE_HI, (upper_32_bits(wq_base_addr)));
			NIC_WREG32(mmNIC0_RXE0_WIN0_WQ_BASE_LO, lower_32_bits(wq_base_addr));
			NIC_WREG32(mmNIC0_RXE0_WIN0_WQ_MISC, wq_size_cline_log);
		}
	}

	/* We are using a separate flag for wq mmu bypass as the nic->mmu_bypass is being used by
	 * other NIC data structures.
	 */
	wq_arr_props->wq_mmu_bypass = phys_addr;

	return 0;

set_asid_fail:
	if (phys_addr)
		hl_nic_mem_destroy(ctx, mem_data.handle);
	else
		hl_unreserve_va_block(hdev, ctx, wq_arr_props->dva_base, wq_arr_props->dva_size);

	return rc;
}

static int gaudi2_user_wq_arr_unset(struct hl_nic_port *nic_port, u32 type, struct hl_ctx *ctx)
{
	struct hl_wq_array_properties *wq_arr_props = &nic_port->wq_arr_props[type];
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	int rc = 0;

	if (wq_arr_props->is_send) {
		NIC_WREG32(mmNIC0_QPC0_TX_WQ_BASE_ADDR_63_32_1, 0);
		NIC_WREG32(mmNIC0_QPC0_TX_WQ_BASE_ADDR_31_0_1, 0);
		NIC_WREG32(mmNIC0_QPC0_LOG_MAX_TX_WQ_SIZE_1, 0);

		NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_63_32_1, 0);
		NIC_WREG32(mmNIC0_TXE0_SQ_BASE_ADDRESS_31_0_1, 0);
		NIC_WREG32(mmNIC0_TXE0_LOG_MAX_WQ_SIZE_1, 0);

		if (wq_arr_props->on_device_mem && !gaudi2_is_fw_ver_below_1_8(hdev))
			gaudi2_nic_config_wqe_asid(nic_port, 0, false);
	} else {
		NIC_WREG32(mmNIC0_QPC0_RX_WQ_BASE_ADDR_63_32_1, 0);
		NIC_WREG32(mmNIC0_QPC0_RX_WQ_BASE_ADDR_31_0_1, 0);
		NIC_WREG32(mmNIC0_QPC0_LOG_MAX_RX_WQ_SIZE_1, 0);

		NIC_WREG32(mmNIC0_RXE0_WIN1_WQ_BASE_LO, 0);
		NIC_WREG32(mmNIC0_RXE0_WIN1_WQ_BASE_HI, 0);
		NIC_WREG32(mmNIC0_RXE0_WIN1_WQ_MISC, 0);
	}

	if (wq_arr_props->dva_base) {
		rc = hl_nic_unreserve_wq_dva(hdev, ctx, nic_port, type);
	} else {
		rc = hl_nic_mem_destroy(ctx, wq_arr_props->handle);
		if (!rc)
			wq_arr_props->handle = 0;
	}

	return rc;
}

static void gaudi2_get_cq_id_range(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id)
{
	*min_id = NIC_MIN_CQ_ID;
	*max_id = NIC_MAX_CQ_ID;
}

static int gaudi2_user_cq_set(struct hl_nic_user_cq *user_cq,
				struct hl_nic_user_cq_set_in_params *in,
				struct hl_nic_user_cq_set_out_params *out)
{
	struct hl_nic_port *nic_port = user_cq->nic_port;
	struct hl_device *hdev = nic_port->hdev;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct hl_nic *nic = &hdev->nic;
	struct gaudi2_nic_port *gaudi2_nic;
	struct hl_nic_mem_data mem_data;
	struct hl_ctx *ctx = user_cq->ctx;
	u64 mem_handle, pi_handle, regs_handle, pi_device_addr, umr_block_addr;
	u32 port = nic_port->port, id = user_cq->id, offset = id * 4, regs_offset;
	int rc;

	if (!nic->mmu_bypass) {
		dev_dbg(hdev->dev,
			"Allocation of non physical CQ %d dma-mem is not supported, port %d\n", id,
			port);
		return -EOPNOTSUPP;
	}

	gaudi2_nic = &gaudi2->nic_ports[port];

	memset(&mem_data, 0, sizeof(mem_data));
	mem_data.mem_id = HL_NIC_DRV_MEM_HOST_MAP_ONLY;
	mem_data.in.host_map_data.bus_address = RING_BUF_DMA_ADDRESS(&gaudi2_nic->cq_rings[0]) +
						(id * NIC_TOTAL_CQ_MEM_SIZE);
	mem_data.in.host_map_data.kernel_address = RING_BUF_ADDRESS(&gaudi2_nic->cq_rings[0]) +
							(id * NIC_TOTAL_CQ_MEM_SIZE);
	mem_data.size = in->num_of_cqes * CQE_SIZE;
	rc = hl_nic_mem_alloc(ctx, &mem_data);
	if (rc) {
		dev_dbg(hdev->dev, "user CQ %d buffer allocation failed, rc %d, port %d\n", id, rc,
			port);
		return rc;
	}

	mem_handle = mem_data.handle;

	/*
	 * Allocate a producer-index (PI) buffer in host kernel.
	 * HW updates PI when it pushes an entry to a CQ.
	 * User mmaps PI buffer and may poll to read current PI.
	 *
	 * Allocate page size, else we risk exposing kernel data to userspace inadvertently.
	 */
	memset(&mem_data, 0, sizeof(mem_data));
	mem_data.mem_id = HL_NIC_DRV_MEM_HOST_DMA_COHERENT;
	mem_data.size = PAGE_SIZE;
	rc = hl_nic_mem_alloc(ctx, &mem_data);
	if (rc) {
		dev_dbg(hdev->dev, "user CQ %d PI buffer allocation failed, rc %d, port %d\n", id,
			rc, port);
		goto pi_alloc_fail;
	}

	pi_handle = mem_data.handle;
	pi_device_addr = mem_data.addr;

	NIC_WREG32(mmNIC0_RXE0_CQ_LOG_SIZE_0 + offset, ilog2(in->num_of_cqes));
	NIC_WREG32(mmNIC0_RXE0_CQ_PI_ADDR_HI_0 + offset, upper_32_bits(pi_device_addr));
	NIC_WREG32(mmNIC0_RXE0_CQ_PI_ADDR_LO_0 + offset, lower_32_bits(pi_device_addr));

	/* reset the PI+CI for this CQ */
	NIC_WREG32(mmNIC0_RXE0_CQ_WRITE_INDEX_0 + offset, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_PRODUCER_INDEX_0 + offset, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_CONSUMER_INDEX_0 + offset, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_CFG_0 + offset, NIC0_RXE0_CQ_CFG_WRITE_PI_EN_MASK |
					NIC0_RXE0_CQ_CFG_ENABLE_MASK);

	/* CQs 0 and 1 are secured and hence reserved so skip them for block addr calculation */
	__gaudi2_nic_get_db_fifo_umr(nic_port, (id >> 1) - 1, id, &umr_block_addr, &regs_offset);

	/* add CQ offset */
	regs_offset += mmNIC0_UMR0_0_COMPLETION_QUEUE_CI_1_CQ_NUMBER -
			mmNIC0_UMR0_0_UNSECURE_DOORBELL1_UNSECURE_DB_FIRST32;

	/* get mmap handle for UMR block */
	rc = hl_get_hw_block_handle(hdev, umr_block_addr, &regs_handle, NULL);
	if (rc) {
		dev_dbg(hdev->dev, "Failed to get user CQ %d UMR block, rc %d, port %d\n", id, rc,
			port);
		goto umr_get_fail;
	}

	rc = hl_nic_eq_dispatcher_register_cq(nic_port, ctx->asid, id);
	if (rc) {
		dev_err(hdev->dev, "failed to register CQ %d, rc %d, port %d\n", id, rc, port);
		goto eq_register_fail;
	}

	out->mem_handle = mem_handle;
	out->regs_handle = regs_handle;
	out->regs_offset = regs_offset;
	out->pi_handle = pi_handle;

	user_cq->mem_handle = mem_handle;
	user_cq->pi_handle = pi_handle;

	return 0;

eq_register_fail:
umr_get_fail:
	hl_nic_mem_destroy(ctx, pi_handle);
pi_alloc_fail:
	hl_nic_mem_destroy(ctx, mem_handle);

	return rc;
}

static int gaudi2_user_cq_unset(struct hl_nic_user_cq *user_cq)
{
	struct hl_nic_port *nic_port = user_cq->nic_port;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_ctx *ctx = user_cq->ctx;
	u32 port = nic_port->port, id = user_cq->id, offset = id * 4;

	hl_nic_eq_dispatcher_unregister_cq(nic_port, id);

	NIC_RMWREG32(mmNIC0_RXE0_CQ_CFG_0 + offset, 0, NIC0_RXE0_CQ_CFG_WRITE_PI_EN_MASK);
	/* flush the new cfg */
	NIC_RREG32(mmNIC0_RXE0_CQ_CFG_0 + offset);

	NIC_WREG32(mmNIC0_RXE0_CQ_PI_ADDR_HI_0 + offset, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_PI_ADDR_LO_0 + offset, 0);

	/* only unmaps as the HW migh still access this memory */
	hl_nic_mem_destroy(ctx, user_cq->mem_handle);
	/* unmaps and frees as we disabled the PI flag and the HW won't access this memory */
	hl_nic_mem_destroy(user_cq->ctx, user_cq->pi_handle);

	return 0;
}

static void gaudi2_user_cq_destroy(struct hl_nic_user_cq *user_cq)
{
	struct hl_nic_port *nic_port = user_cq->nic_port;
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, offset = user_cq->id * 4;

	NIC_WREG32(mmNIC0_RXE0_CQ_CFG_0 + offset, 0);
	NIC_WREG32(mmNIC0_RXE0_CQ_LOG_SIZE_0 + offset, 0);
}

static void gaudi2_set_advanced_op_mask(struct hl_device *hdev, bool advanced)
{
	u64 advanced_op_mask = BIT(HL_NIC_OP_USER_CCQ_SET) | BIT(HL_NIC_OP_USER_CCQ_UNSET);

	if (advanced)
		hdev->nic.ctrl_op_mask |= advanced_op_mask;
	else
		hdev->nic.ctrl_op_mask &= ~advanced_op_mask;
}

static int gaudi2_user_set_app_params(struct hl_device *hdev,
					struct hl_nic_set_user_app_params_in *in,
					bool *modify_wqe_checkers, struct hl_ctx *ctx)
{
	struct gaudi2_nic_port *gaudi2_nic;
	struct hl_nic_port *nic_port;
	u32 port = in->port, bp_offs_fw, bp_offs_qman, encap_id, wtd_config;

	nic_port = &hdev->nic.nic_ports[port];

	if (nic_port->set_app_params) {
		dev_dbg(hdev->dev, "App params were already set, port %d\n", port);
		return -EPERM;
	}

	gaudi2_nic = nic_port->nic_specific;
	gaudi2_nic->advanced = in->advanced;

	/* Enable\disable advanced operations */
	gaudi2_set_advanced_op_mask(hdev, (bool) gaudi2_nic->advanced);

	bp_offs_fw = in->bp_offs[HL_NIC_USER_BP_OFFS_FW];
	bp_offs_qman = in->bp_offs[HL_NIC_USER_BP_OFFS_QMAN];

	/* Validate the parameters before performing any register changes */
	if ((bp_offs_fw) || (bp_offs_qman)) {
		if (!gaudi2_nic->advanced) {
			dev_dbg(hdev->dev,
				"Port %u: advanced flag is disabled - can't set back-pressue\n",
				port);
			return -EINVAL;
		}

		if ((bp_offs_fw) && (bp_offs_fw & ~WQ_BP_ADDR_VAL_MASK)) {
			dev_dbg(hdev->dev, "Port %u: invalid ARC BP offset 0x%x\n",
				port, bp_offs_fw);
			return -EINVAL;
		}

		if ((bp_offs_qman) && (bp_offs_qman & ~WQ_BP_ADDR_VAL_MASK)) {
			dev_dbg(hdev->dev, "Port %u: invalid QMAN BP offset 0x%x\n",
				port, bp_offs_qman);
			return -EINVAL;
		}

		gaudi2_nic->min_qp_size = U32_MAX;

		/* Enable BP for DB */
		wtd_config = NIC_RREG32(mmNIC0_QPC0_WTD_CONFIG) |
				NIC0_QPC0_WTD_CONFIG_WQ_BP_DB_ACCOUNTED_MASK;

		if (bp_offs_fw) {
			/* Enable WTD BP to ARC */
			wtd_config |= NIC0_QPC0_WTD_CONFIG_WQ_BP_2ARC_EN_MASK;

			/* Set the offset in the ARC memory to signal the BP*/
			NIC_WREG32(mmNIC0_QPC0_WQ_BP_2ARC_ADDR, bp_offs_fw);
		}

		if (bp_offs_qman) {
			/* Enable WTD BP to QMAN */
			wtd_config |= NIC0_QPC0_WTD_CONFIG_WQ_BP_2QMAN_EN_MASK;

			/* Set the offset in the QMAN memory to signal the BP*/
			NIC_WREG32(mmNIC0_QPC0_WQ_BP_2QMAN_ADDR, bp_offs_qman);
		}

		NIC_WREG32(mmNIC0_QPC0_WTD_CONFIG, wtd_config);

		*modify_wqe_checkers = true;
	} else {
		*modify_wqe_checkers = false;
	}

	/* SW-99892 - configure port's encapsulation for source ip-address automatically */
	if (nic_port->eth_enable)
		gaudi2_default_encap_set(nic_port, &encap_id, 0);

	return 0;
}

static void gaudi2_user_get_app_params(struct hl_device *hdev,
					struct hl_nic_get_user_app_params_in *in,
					struct hl_nic_get_user_app_params_out *out)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct hl_nic_port *nic_port;

	nic_port = gaudi2->nic_ports[in->port].nic_port;
	out->max_num_of_qps = NIC_MAX_QP_NUM;
	/* always include the Ethernet QP */
	out->num_allocated_qps = 1 + atomic_read(&nic_port->num_of_allocated_qps);
	out->max_allocated_qp_idx = hl_nic_get_max_qp_id(nic_port);
	out->max_cq_size = CQE_SIZE * NIC_CQ_MAX_ENTRIES;

	/* Two CQs are reserved - one for the Ethernet and one for the driver CQ. We could use the
	 * driver CQ as a user CQ but in each UMR there are 2 CQs and since CQ idx 0 is reserved for
	 * Ethernet, also CQ idx 1 is unavailable.
	 */
	out->max_num_of_cqs = GAUDI2_NIC_MAX_CQS_NUM - NIC_CQS_NUM;
	out->max_num_of_db_fifos = GAUDI2_MAX_DB_FIFO_ID - GAUDI2_MIN_DB_FIFO_ID + 1;
	out->max_num_of_encaps = GAUDI2_MAX_ENCAP_ID - GAUDI2_MIN_ENCAP_ID + 1;
}

static bool gaudi2_nic_get_hw_cap(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;

	return (gaudi2->hw_cap_initialized & HW_CAP_NIC_DRV);
}

static void gaudi2_nic_set_hw_cap(struct hl_device *hdev, bool enable)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;

	if (enable)
		gaudi2->hw_cap_initialized |= HW_CAP_NIC_DRV;
	else
		gaudi2->hw_cap_initialized &= ~HW_CAP_NIC_DRV;
}

static void gaudi2_nic_stop_traffic_macro(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	u32 port = nic_macro->idx << 1; /* the index of the first port in the macro */
	int i;

	for (i = 0 ; i < 8 ; i++)
		NIC_MACRO_WREG32(mmNIC0_RXB_CORE_DROP_THRESHOLD_0 + i * 4, 0);

	usleep_range(1000, 2000);

	NIC_MACRO_RMWREG32(mmNIC0_TMR_TMR_CACHES_CFG, 1,
				NIC0_TMR_TMR_CACHES_CFG_LIST_CACHE_STOP_MASK);
	NIC_MACRO_RMWREG32(mmNIC0_TMR_TMR_CACHES_CFG, 1,
				NIC0_TMR_TMR_CACHES_CFG_FREE_LIST_CACHE_STOP_MASK);
	NIC_MACRO_RMWREG32(mmNIC0_TMR_TMR_CACHES_CFG, 1,
				NIC0_TMR_TMR_CACHES_CFG_STATE_CACHE_STOP_MASK);

	usleep_range(1000, 2000);

	/* Flush all the writes */
	NIC_MACRO_RREG32(mmNIC0_TMR_TMR_CACHES_CFG);
}

static void gaudi2_nic_stop_traffic_port(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	NIC_RMWREG32(mmNIC0_QPC0_REQ_STATIC_CONFIG, 1,
			NIC0_QPC0_REQ_STATIC_CONFIG_CACHE_STOP_MASK);
	NIC_RMWREG32(mmNIC0_QPC0_RES_STATIC_CONFIG, 1,
			NIC0_QPC0_RES_STATIC_CONFIG_CACHE_STOP_MASK);
	NIC_RMWREG32(mmNIC0_TXS0_CACHE_CFG, 1,
			NIC0_TXS0_CACHE_CFG_LIST_CACHE_STOP_MASK);
	NIC_RMWREG32(mmNIC0_TXS0_CACHE_CFG, 1,
			NIC0_TXS0_CACHE_CFG_FREE_LIST_CACHE_STOP_MASK);

	/* Flush all the writes */
	NIC_MACRO_RREG32(mmNIC0_TXS0_CACHE_CFG);
}

/* FW must be aligned with any changes done to this function */
static void gaudi2_nic_stop_traffic(struct hl_device *hdev)
{
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_macro *nic_macro;
	int i;

	for (i = 0 ; i < hdev->asic_prop.nic_props.num_of_macros ; i++) {
		nic_macro = &hdev->nic.nic_macros[i];

		if (!gaudi2_nic_is_macro_enabled(nic_macro))
			continue;

		gaudi2_nic_stop_traffic_macro(nic_macro);
	}

	for (i = 0 ; i < hdev->asic_prop.nic_props.max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		gaudi2_nic_stop_traffic_port(&nic->nic_ports[i]);
	}
}

static int gaudi2_nic_kernel_ctx_init(struct hl_ctx *ctx)
{
	return gaudi2_nic_ctx_init(ctx);
}

static void gaudi2_nic_kernel_ctx_fini(struct hl_ctx *ctx)
{
	gaudi2_nic_ctx_fini(ctx);
}

static int gaudi2_nic_pre_core_init(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;

	/* This case is handled as part of gaudi2_cpucp_info_get() */
	if ((gaudi2->hw_cap_initialized & HW_CAP_CPU_Q) && !hdev->ignore_fw_nic_info)
		return 0;

	return gaudi2_nic_set_info(hdev, false);
}

static void gaudi2_nic_set_speed(struct hl_device *hdev)
{
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_port *nic_port;
	int i;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];
		nic_port->speed = nic->phy_set_nrz ? SPEED_25000 : SPEED_100000;
	}
}

static void gaudi2_nic_config_hw_mac(struct hl_nic_macro *nic_macro)
{
	struct hl_nic_properties *nic_prop = &nic_macro->hdev->asic_prop.nic_props;
	struct hl_device *hdev = nic_macro->hdev;
	u32 port = nic_macro->idx << 1; /* the index of the first port in the macro */
	u32 speed = hdev->nic.nic_ports[port].speed;

	switch (speed) {
	case SPEED_25000:
		/* AC_SD_CFG: sd_n2 = 0, sd_8x = 0 */
		NIC_MACRO_WREG32(mmPRT0_MAC_CORE_MAC_SD_CFG, 0xF0FF00);

		/* KP_MODE = 0, FEC91_EN = 1, FEC91_1LANE = 1 */
		NIC_MACRO_WREG32(mmPRT0_MAC_CORE_MAC_FEC91_CFG, 0x60F);
		NIC_MACRO_WREG32(mmPRT0_MAC_CORE_MAC_PCS_CFG, 0x0);
		NIC_MACRO_WREG32(mmPRT0_MAC_CORE_MAC_FC_FEC_CFG, 0x0);
		break;
	case SPEED_50000:
		NIC_MACRO_WREG32(mmPRT0_MAC_CORE_MAC_SD_CFG, 0xF0FFF0);
		NIC_MACRO_WREG32(mmPRT0_MAC_CORE_MAC_FEC91_CFG, 0xFF);
		NIC_MACRO_WREG32(mmPRT0_MAC_CORE_MAC_PCS_CFG, 0x0);

		if (nic_prop->advanced_hw_support) {
			NIC_MACRO_WREG32(mmNIC0_MAC_RS_FEC_RSFEC_CONTROL, 0x400);
			NIC_MACRO_WREG32(mmNIC0_MAC_RS_FEC_RSFEC1_CONTROL, 0x400);
			NIC_MACRO_WREG32(mmNIC0_MAC_RS_FEC_RSFEC2_CONTROL, 0x400);
			NIC_MACRO_WREG32(mmNIC0_MAC_RS_FEC_RSFEC3_CONTROL, 0x400);
		}
		break;
	case SPEED_100000:
		NIC_MACRO_WREG32(mmPRT0_MAC_CORE_MAC_FEC91_CFG, 0xFF);
		break;
	default:
		dev_err(hdev->dev, "unknown NIC speed %dMb/s, cannot set MAC\n", speed);
	}
}

static void gaudi2_nic_config_hw_rxb(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	u32 dynamic_credits, static_credits, drop_th, small_pkt_drop_th, xoff_th, xon_th, val;
	u32 port = nic_macro->idx << 1; /* the index of the first port in the macro */
	int i;

	/* Set iCRC calculation & verification with reversed bytes */
	NIC_MACRO_WREG32(mmNIC0_RXB_CORE_ICRC_CFG, 0x2);

	/* Assuming 1 effective priority per port divided between 2 physical ports. */
	static_credits = RXB_NUM_STATIC_CREDITS;
	dynamic_credits = 0;
	drop_th = static_credits - RXB_DROP_TH_DEPTH;
	xoff_th = static_credits - RXB_XOFF_TH_DEPTH;
	xon_th = xoff_th - RXB_XON_TH_DEPTH;
	small_pkt_drop_th = static_credits - RXB_DROP_SMALL_TH_DEPTH;

	/* Dynamic credits (global) */
	NIC_MACRO_WREG32(mmNIC0_RXB_CORE_MAX_DYNAMIC, dynamic_credits);

	val = static_credits | (static_credits << 13);
	for (i = 0 ; i < 8 ; i++)
		NIC_MACRO_WREG32(mmNIC0_RXB_CORE_MAX_STATIC_CREDITS_0 + i * 4, val);

	/* Drop threshold (per port/prio) */
	val = drop_th | (drop_th << 13);
	for (i = 0 ; i < 8 ; i++)
		NIC_MACRO_WREG32(mmNIC0_RXB_CORE_DROP_THRESHOLD_0 + i * 4, val);

	/* Drop threshold for small packets (per port/prio) */
	val = small_pkt_drop_th | (small_pkt_drop_th << 13);
	for (i = 0 ; i < 8 ; i++)
		NIC_MACRO_WREG32(mmNIC0_RXB_CORE_DROP_SMALL_THRESHOLD_0 + i * 4, val);

	/* XOFF threshold (per port/prio) */
	val = xoff_th | (xoff_th << 13);
	for (i = 0 ; i < 8 ; i++)
		NIC_MACRO_WREG32(mmNIC0_RXB_CORE_XOFF_THRESHOLD_0 + i * 4, val);

	/* XON threshold (per port/prio) */
	val = xon_th | (xon_th << 13);
	for (i = 0 ; i < 8 ; i++)
		NIC_MACRO_WREG32(mmNIC0_RXB_CORE_XON_THRESHOLD_0 + i * 4, val);

	/* All DSCP values should be mapped to PRIO 0 */
	for (i = 0 ; i < 8 ; i++)
		NIC_MACRO_WREG32(mmNIC0_RXB_CORE_DSCP2PRIO_0 + i * 4, 0);
}

static void gaudi2_nic_config_hw_txb(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	u32 port = nic_macro->idx << 1; /* the index of the first port in the macro */
	u32 speed = hdev->nic.nic_ports[port].speed;

	/* Set iCRC calculation & generation with reversed bytes */
	NIC_MACRO_WREG32(mmNIC0_TXB_ICRC_CFG, 0x2);

	/* All priorities will use one global PAUSE */
	NIC_MACRO_WREG32(mmNIC0_TXB_GLOBAL_PAUSE, 0xf);

	switch (speed) {
	case SPEED_25000:
		fallthrough;
	case SPEED_50000:
		NIC_MACRO_WREG32(mmNIC0_TXB_TDM_PORT_ARB_MASK, 0x7BDE);
		break;
	case SPEED_100000:
		NIC_MACRO_WREG32(mmNIC0_TXB_TDM_PORT_ARB_MASK, 0xBBEE);
		break;
	default:
		dev_err(hdev->dev,
			"unknown NIC port %d speed %dMb/s, cannot set TDM mask\n",
			port, speed);
	}
}

static void gaudi2_nic_config_hw_tmr(struct hl_nic_macro *nic_macro)
{
	struct hl_nic_properties *nic_prop;
	struct gaudi2_device *gaudi2;
	struct hl_device *hdev;
	u64 tmr_addr;
	u32 port;
	int i;

	port = nic_macro->idx << 1; /* the index of the first port in the macro */
	hdev = nic_macro->hdev;
	gaudi2 = hdev->asic_specific;
	nic_prop = &hdev->asic_prop.nic_props;

	tmr_addr = nic_prop->tmr_base_addr + nic_macro->idx * nic_prop->tmr_base_size;

	/* Clear timer FSM0 + FSM1 */
	for (i = 0 ; i < NIC_MAX_QP_NUM * 2 ; i++) {
		writeb(0, hdev->pcie_bar[DRAM_BAR_ID] +
			((tmr_addr + TMR_FSM0_OFFS + i) - gaudi2->dram_bar_cur_addr));

		if ((i % NIC_MAX_COMBINED_WRITES) == 0)
			readb(hdev->pcie_bar[DRAM_BAR_ID] +
				((tmr_addr + TMR_FSM0_OFFS + i) - gaudi2->dram_bar_cur_addr));
	}

	/* Timer free list */
	for (i = 0 ; i < TMR_FREE_NUM_ENTRIES ; i++) {
		writel(TMR_GRANULARITY + i, hdev->pcie_bar[DRAM_BAR_ID] +
			((tmr_addr + TMR_FREE_OFFS + i * TMR_ENT_SIZE) -
				gaudi2->dram_bar_cur_addr));
		if ((i % NIC_MAX_COMBINED_WRITES) == 0)
			readl(hdev->pcie_bar[DRAM_BAR_ID] +
				((tmr_addr + TMR_FREE_OFFS + i * TMR_ENT_SIZE) -
						gaudi2->dram_bar_cur_addr));
	}

	/* Perform read to flush the writes */
	readq(hdev->pcie_bar[DRAM_BAR_ID] + tmr_addr - gaudi2->dram_bar_cur_addr);

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_BASE_ADDRESS_63_32,
			 upper_32_bits(tmr_addr + TMR_FIFO_OFFS));
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_BASE_ADDRESS_31_7,
			 lower_32_bits(tmr_addr + TMR_FIFO_OFFS) >> 7);

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_BASE_ADDRESS_FREE_LIST_63_32,
			 upper_32_bits(tmr_addr + TMR_FREE_OFFS));
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_BASE_ADDRESS_FREE_LIST_31_0,
			 lower_32_bits(tmr_addr + TMR_FREE_OFFS));

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_CACHE_BASE_ADDR_63_32,
			 upper_32_bits(tmr_addr + TMR_FSM0_OFFS));
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_CACHE_BASE_ADDR_31_7,
			 lower_32_bits(tmr_addr + TMR_FSM0_OFFS) >> 7);

	/* configure MMU-BP for TIMERS */
	NIC_MACRO_WREG32(mmNIC0_TMR_AXUSER_TMR_FREE_LIST_HB_WR_OVRD_LO, 0xFFFFFBFF);
	NIC_MACRO_WREG32(mmNIC0_TMR_AXUSER_TMR_FREE_LIST_HB_RD_OVRD_LO, 0xFFFFFBFF);

	NIC_MACRO_WREG32(mmNIC0_TMR_AXUSER_TMR_FIFO_HB_WR_OVRD_LO, 0xFFFFFBFF);
	NIC_MACRO_WREG32(mmNIC0_TMR_AXUSER_TMR_FIFO_HB_RD_OVRD_LO, 0xFFFFFBFF);

	NIC_MACRO_WREG32(mmNIC0_TMR_AXUSER_TMR_FSM_HB_WR_OVRD_LO, 0xFFFFFBFF);
	NIC_MACRO_WREG32(mmNIC0_TMR_AXUSER_TMR_FSM_HB_RD_OVRD_LO, 0xFFFFFBFF);
	/* Perform read to flush the writes */
	NIC_MACRO_RREG32(mmNIC0_TMR_AXUSER_TMR_FSM_HB_RD_OVRD_LO);

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_31_0, 0);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_63_32, 0);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_95_64, 0);

	/* Set the amount of ticks for timeout. */
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_191_160,
			hdev->pldm ? NIC_TMR_TIMEOUT_PLDM_US : NIC_TMR_TIMEOUT_US);

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_216_192, 0);

	for (i = 0 ; i < TMR_GRANULARITY ; i++) {
		NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_127_96, i);
		NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_159_128, i);
		NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_FIFO, i);
		NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCHEDQ_UPDATE_EN, 1);
	}

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_SCAN_TIMER_COMP_31_0, 10);

	/* Set the number of clock's cycles for a single tick in order to have 1 usec per tick.
	 * i.e.: 1/frequency_in_MHz * num_of_clk_cycles = 1 usec
	 */
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_TICK_WRAP, nic_prop->clk);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_LIST_MASK,
			 ~(0xFFFFFFFF << (ilog2(TMR_FREE_NUM_ENTRIES) - 5)));

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_PRODUCER_UPDATE, TMR_FREE_NUM_ENTRIES);
	/* Latch the TMR value */
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_PRODUCER_UPDATE_EN, 1);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_PRODUCER_UPDATE_EN, 0);

	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_LIST_MEM_READ_MASK, 0);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_PUSH_LOCK_EN, 1);
	NIC_MACRO_WREG32(mmNIC0_TMR_TMR_TIMER_EN, 1);
	NIC_MACRO_WREG32(mmNIC0_TMR_FREE_LIST_PUSH_MASK_EN, 0);

	/* Perform read from the device to flush all configurations */
	NIC_MACRO_RREG32(mmNIC0_TMR_TMR_TIMER_EN);
}

static void gaudi2_nic_enable_gic_macro_interrupts(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev;
	u32 port;

	port = nic_macro->idx << 1; /* the index of the first port in the macro */
	hdev = nic_macro->hdev;

	/* enable TMR block interrupts */
	NIC_MACRO_WREG32(mmNIC0_TMR_INTERRUPT_MASK, 0x0);

	/* enable RXB_CORE block interrupts */
	NIC_MACRO_WREG32(mmNIC0_RXB_CORE_SEI_INTR_MASK, 0x0);
	NIC_MACRO_WREG32(mmNIC0_RXB_CORE_SPI_INTR_MASK, 0x0);
}

static void gaudi2_nic_disable_gic_macro_interrupts(struct hl_device *hdev)
{
	struct hl_nic_macro *nic_macro;
	u32 port;
	int i;

	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		nic_macro = &hdev->nic.nic_macros[i];
		port = nic_macro->idx << 1; /* the index of the first port in the macro */

		/* It's not allowed to configure a macro that both of its ports are not enabled */
		if (!gaudi2_nic_is_macro_enabled(nic_macro))
			continue;

		/* disable TMR block interrupts */
		NIC_MACRO_WREG32(mmNIC0_TMR_INTERRUPT_MASK, 0xF);

		/* disable RXB_CORE block interrupts */
		NIC_MACRO_WREG32(mmNIC0_RXB_CORE_SEI_INTR_MASK, 0x3);
		NIC_MACRO_WREG32(mmNIC0_RXB_CORE_SPI_INTR_MASK, 0x3F);
	}
}

static void gaudi2_nic_hw_macro_config(struct hl_nic_macro *nic_macro)
{
	/* the following registers are shared between each pair of ports */

	/* MAC Configuration */
	gaudi2_nic_config_hw_mac(nic_macro);

	/* RXB Configuration */
	gaudi2_nic_config_hw_rxb(nic_macro);

	/* TXB Configuration */
	gaudi2_nic_config_hw_txb(nic_macro);

	/* TMR Configuration */
	gaudi2_nic_config_hw_tmr(nic_macro);

	/* Enable GIC macro interrupts - required only if running on PLDM */
	if (nic_macro->hdev->pldm)
		gaudi2_nic_enable_gic_macro_interrupts(nic_macro);
}

static void gaudi2_nic_macros_hw_config(struct hl_device *hdev)
{
	struct hl_nic_macro *nic_macro;
	int i;

	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		nic_macro = &hdev->nic.nic_macros[i];

		if (!gaudi2_nic_is_macro_enabled(nic_macro))
			continue;

		gaudi2_nic_hw_macro_config(nic_macro);
	}
}

int gaudi2_nic_set_info(struct hl_device *hdev, bool get_from_fw)
{
	struct cpucp_nic_info *nic_info = &hdev->asic_prop.cpucp_nic_info;
	struct cpucp_info *cpucp_info = &hdev->asic_prop.cpucp_info;
	struct cpucp_mac_addr *mac_arr = nic_info->mac_addrs;
	struct hl_nic *nic = &hdev->nic;
	u32 card_location, serdes_type = MAX_NUM_SERDES_TYPE;
	u8 mac[ETH_ALEN], *mac_addr;
	int rc, i;

	/* copy the MAC OUI in reverse */
	for (i = 0 ; i < 3 ; i++)
		mac[i] = HABANALABS_MAC_OUI_1 >> (8 * (2 - i));

	if (get_from_fw) {
		rc = hl_fw_cpucp_nic_info_get(hdev);
		if (rc)
			return rc;

		/* Allow debugging of gaudi2B disabled ports by overriding the pci revision id and
		 * by that identifying as gaudi2.
		 */
		if ((hdev->asic_type == ASIC_GAUDI2) && hdev->pci_rev_id_override) {
			dev_dbg(hdev->dev,
				"skipping NIC FW ports info on gaudi2B device with an overridden pci revision id\n");
		} else {
			hdev->nic_ports_mask &= le64_to_cpu(nic_info->link_mask[0]);
			hdev->nic_ports_ext_mask &= le64_to_cpu(nic_info->link_ext_mask[0]);
			hdev->nic_auto_neg_mask &= le64_to_cpu(nic_info->auto_neg_mask[0]);
		}

		serdes_type = le16_to_cpu(nic_info->serdes_type);

		/* In case of invalid MAC from F/W, and if the user asked to ignore eeprom related
		 * errors, the MAC addresses will be set manually according to the bus address and
		 * the port id.
		 * here we prepare the 3rd (bus id) and the 4th (device id) octates for such a case.
		 */
		if (hdev->ignore_eeprom_errors) {
			mac[3] = hdev->pdev->bus->number;
			mac[4] = PCI_SLOT(hdev->pdev->devfn);
		}

		/* check for invalid MAC addresses from F/W (bad OUI) */
		for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
			if (!(hdev->nic_ports_mask & BIT(i)))
				continue;

			mac_addr = mac_arr[i].mac_addr;
			if (strncmp(mac, mac_addr, 3)) {
				if (hdev->ignore_eeprom_errors) {
					dev_dbg(hdev->dev,
						"bad MAC OUI %pM, port %d - setting a valid MAC\n",
						mac_addr, i);
					mac[ETH_ALEN - 1] = i;
					memcpy(mac_addr, mac, ETH_ALEN);
				} else {
					dev_err(hdev->dev,
						"bad MAC OUI %pM, port %d - failing the initialization\n",
						mac_addr, i);
					return -EFAULT;
				}
			}
		}

		nic->card_location = le32_to_cpu(cpucp_info->card_location);

		/* LKD will use the FW serdes info only in case of HLS2 setup */
		if (hdev->gaudi2_setup_type == GAUDI2_SETUP_TYPE_HLS2) {
			gaudi2_nic_get_fw_lane_mapping(nic_info, nic);
			nic->use_fw_serdes_info = true;
		}
	} else {
		/* No F/W, hence need to set the MACs manually (randomize) */
		get_random_bytes(&mac[3], 2);

		for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
			if (!(hdev->nic_ports_mask & BIT(i)))
				continue;

			mac[ETH_ALEN - 1] = i;
			memcpy(mac_arr[i].mac_addr, mac, ETH_ALEN);
		}

		if (!(hdev->fw_components & FW_TYPE_BOOT_CPU)) {
			/* This section reads privilege register, hence we should disable
			 * assertion on simulator to allow this read.
			 * Assertion is turned on right after the register is read.
			 */
			hdev->asic_funcs->set_priv_assertions(hdev, false);
			card_location = RREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS_H);
			hdev->asic_funcs->set_priv_assertions(hdev, true);

			serdes_type = card_location;
			card_location &= PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_H_I2C_SLV_ADDR_MASK;
			card_location >>= PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_H_I2C_SLV_ADDR_SHIFT;
			cpucp_info->card_location = cpu_to_le32(card_location);
			nic->card_location = card_location;
			serdes_type &= PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_H_RERERVED_STRAP_MASK;
			serdes_type >>= PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_H_RERERVED_STRAP_SHIFT;
		} else {
			dev_warn(hdev->dev, "can't read card location as FW security is enabled\n");
		}
	}

	switch (serdes_type) {
	case HLS2_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI2_HLS2;
		break;
	case HLS2_TYPE_1_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI2_TYPE1;
		break;
	default:
		hdev->asic_prop.server_type = HL_SERVER_TYPE_UNKNOWN;
		break;
	}

	/* If we are running on non HLS2 setup or a PCI card, all the ports should be set as
	 * external (the only exception is when the asic type is GADUI2B).
	 */
	if (hdev->card_type == cpucp_card_type_pci ||
			hdev->gaudi2_setup_type != GAUDI2_SETUP_TYPE_HLS2) {
		if (hdev->asic_type != ASIC_GAUDI2B)
			hdev->nic_ports_ext_mask = hdev->nic_ports_mask;

		hdev->nic_auto_neg_mask &= ~hdev->nic_ports_ext_mask;
	}

	/* Disable ANLT on NIC 0 ports (due to lane swapping) */
	hdev->nic_auto_neg_mask &= ~0x3;

	return 0;
}

static int gaudi2_nic_core_init(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_prop = &hdev->asic_prop.nic_props;
	struct hl_nic *nic = &hdev->nic;
	u64 nic_dram_alloc_size;
	int rc;

	nic_dram_alloc_size = nic_prop->nic_drv_end_addr - nic_prop->nic_drv_base_addr;
	if (nic_dram_alloc_size > nic_prop->nic_drv_size) {
		dev_err(hdev->dev, "DRAM allocation for NIC (%lluMB) shouldn't exceed %lluMB\n",
			div_u64(nic_dram_alloc_size, SZ_1M),
			div_u64(nic_prop->nic_drv_size, SZ_1M));
		return -ENOMEM;
	}

	rc = gaudi2_nic_phy_init(hdev);
	if (rc)
		return rc;

	/* This function must be called before configuring the NIC macros */
	gaudi2_nic_set_speed(hdev);

	gaudi2_nic_macros_hw_config(hdev);

	/* TODO: SW-69798: Optimize the amount of pkts to wait for and the timeout it requires.
	 * For now request an interrupt after 1 pkt with 10 usec timeout.
	 */
	if (!nic->skip_cq_arm_timeout)
		nic->cq_arm_timeout = lower_32_bits((nic_prop->clk * CQ_ARM_TIMEOUT_USEC) / SZ_1K);

	return gaudi2_nic_eq_init(hdev);
}

static void gaudi2_nic_core_fini(struct hl_device *hdev)
{
	gaudi2_nic_eq_fini(hdev);

	/* Disable GIC macro interrupts - required only if running on PLDM */
	if (hdev->pldm)
		gaudi2_nic_disable_gic_macro_interrupts(hdev);

	gaudi2_nic_stop_traffic(hdev);
}

static int gaudi2_nic_ctx_dispatcher_init(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_nic_port *gaudi2_nic;
	struct hl_nic_port *nic_port;
	u32 port;
	int i, j, rc = 0;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		gaudi2_nic = &(gaudi2->nic_ports[i]);
		nic_port = gaudi2_nic->nic_port;
		port = nic_port->port;

		rc = hl_nic_eq_dispatcher_associate_dq(gaudi2_nic->nic_port, ctx->asid);
		if (rc) {
			dev_err(hdev->dev,
				"failed to associate ASID %d with port %d event dispatcher (err %d)\n",
				ctx->asid, port, rc);
			goto associate_error;
		}
	}

	return 0;

associate_error:
	/* dissociate the associated dqs */
	for (j = 0 ; j < i ; j++) {
		gaudi2_nic = &(gaudi2->nic_ports[j]);
		hl_nic_eq_dispatcher_dissociate_dq(gaudi2_nic->nic_port, ctx->asid);
	}

	return rc;
}

static void gaudi2_nic_ctx_dispatcher_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_nic_port *gaudi2_nic;
	int i;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++)
		if (hdev->nic_ports_mask & BIT(i)) {
			gaudi2_nic = &(gaudi2->nic_ports[i]);
			hl_nic_eq_dispatcher_dissociate_dq(gaudi2_nic->nic_port, ctx->asid);
		}
}

static int gaudi2_nic_ctx_init(struct hl_ctx *ctx)
{
	return gaudi2_nic_ctx_dispatcher_init(ctx);
}

static void gaudi2_nic_ctx_fini(struct hl_ctx *ctx)
{
	gaudi2_nic_ctx_dispatcher_fini(ctx);
	hl_nic_user_db_fifo_ctx_destroy(ctx);
}

static void gaudi2_nic_configure_cq(struct hl_aux_dev *aux_dev, u32 port,
				    u32 coalesce_usec, bool enable)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_nic_properties *nic_prop = &hdev->asic_prop.nic_props;
	u32 arm_timeout;

	/* Calc timeout in ticks.
	 * result/value of 0 is interpreted as ASAP but since a value of Zero is an invalid value
	 * we modify it to 1.
	 */
	arm_timeout = lower_32_bits((nic_prop->clk * coalesce_usec) / SZ_1K);
	nic->cq_arm_timeout = arm_timeout ? arm_timeout : 1;

	/* disable the current timer before configuring the new time */
	NIC_RMWREG32(mmNIC0_RXE0_CQ_ARM_TIMEOUT_EN, 0, BIT(NIC_CQ_RAW_IDX));
	NIC_RREG32(mmNIC0_RXE0_CQ_ARM_TIMEOUT_EN);

	/* if enable - configure the new timer and enable it */
	if (enable) {
		NIC_WREG32(mmNIC0_RXE0_CQ_ARM_TIMEOUT, nic->cq_arm_timeout);
		NIC_RMWREG32(mmNIC0_RXE0_CQ_ARM_TIMEOUT_EN, 1, BIT(NIC_CQ_RAW_IDX));
	}
}

static void gaudi2_nic_arm_cq(struct hl_aux_dev *aux_dev, u32 port, u32 index)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	NIC_WREG32(mmNIC0_QPC0_ARM_CQ_NUM, NIC_CQ_RAW_IDX);
	NIC_WREG32(mmNIC0_QPC0_ARM_CQ_INDEX, index);
}

static void gaudi2_nic_write_rx_ci(struct hl_aux_dev *aux_dev, u32 port, u32 ci)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	NIC_WREG32(mmNIC0_QPC0_SECURED_CQ_NUMBER, NIC_CQ_RAW_IDX);
	NIC_WREG32(mmNIC0_QPC0_SECURED_CQ_CONSUMER_INDEX, ci);
}

static void gaudi2_nic_get_pfc_cnts(struct hl_aux_dev *aux_dev, u32 port, int pfc_prio,
					u64 *indications, u64 *requests)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	u64 reg_addr, lo_part, hi_part;

	reg_addr = (port & 1) ? mmNIC0_MAC_GLOB_STAT_RX2_ACBFCPAUSEFRAMESRECEIVED0_2 :
				mmNIC0_MAC_GLOB_STAT_RX0_ACBFCPAUSEFRAMESRECEIVED0;

	reg_addr += (4 * pfc_prio);

	lo_part = NIC_MACRO_RREG32(reg_addr);
	hi_part = NIC_MACRO_RREG32(mmNIC0_MAC_GLOB_STAT_CONTROL_REG_DATA_HI);
	*indications = lo_part | (hi_part << 32);

	reg_addr = (port & 1) ? mmNIC0_MAC_GLOB_STAT_TX2_ACBFCPAUSEFRAMESTRANSMITTED0_2 :
				mmNIC0_MAC_GLOB_STAT_TX0_ACBFCPAUSEFRAMESTRANSMITTED0;

	reg_addr += (4 * pfc_prio);

	lo_part = NIC_MACRO_RREG32(reg_addr);
	hi_part = NIC_MACRO_RREG32(mmNIC0_MAC_GLOB_STAT_CONTROL_REG_DATA_HI);
	*requests = lo_part | (hi_part << 32);
}

static int gaudi2_nic_ring_tx_doorbell(struct hl_aux_dev *aux_dev, u32 port, u32 pi)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct gaudi2_nic_port *gaudi2_nic;
	struct hl_nic_port *nic_port;
	u32 db_fifo_ci = 0, db_fifo_pi = 0, space_left_in_db_fifo = 0;

	nic_port = &nic->nic_ports[port];
	gaudi2_nic = nic_port->nic_specific;

	db_fifo_ci = *((u32 *) RING_CI_ADDRESS(&gaudi2_nic->fifo_ring));
	db_fifo_pi = gaudi2_nic->db_fifo_pi;

	space_left_in_db_fifo = CIRC_SPACE(db_fifo_pi, db_fifo_ci, NIC_FIFO_DB_SIZE);

	if (!space_left_in_db_fifo) {
		dev_dbg_ratelimited(hdev->dev, "port %d DB fifo full. PI %d, CI %d\n",
				    port, db_fifo_pi, db_fifo_ci);
		return -EBUSY;
	}

	NIC_WREG32(mmNIC0_QPC0_SECURED_DB_FIRST32, pi);
	NIC_WREG32(mmNIC0_QPC0_SECURED_DB_SECOND32, RAW_QPN);

	/* Incrementing local PI and wrap around at the size of NIC_FIFO_DB_SIZE */
	gaudi2_nic->db_fifo_pi = (gaudi2_nic->db_fifo_pi + 1) & (NIC_FIFO_DB_SIZE - 1);

	return 0;
}

static dma_addr_t gaudi2_nic_dma_map_tx_pkt(struct hl_aux_dev *aux_dev, void *addr, int len)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	return hdev->asic_funcs->asic_dma_map_single(hdev, addr, len, DMA_TO_DEVICE);
}

static void gaudi2_nic_dma_unmap_tx_pkt(struct hl_aux_dev *aux_dev, dma_addr_t dma_addr, int len)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	hdev->asic_funcs->asic_dma_unmap_single(hdev, dma_addr, len, DMA_TO_DEVICE);
}

static int gaudi2_nic_sw_init(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_en_aux_ops *aux_ops = &gaudi2->en_aux_ops;
	struct gaudi2_en_core_info *core_info = &gaudi2->en_core_info;
	struct hl_nic_ring **rx_rings, **cq_rings, **wq_rings;
	struct hl_nic *nic = &hdev->nic;
	int rc;

	rx_rings = kcalloc(NIC_NUMBER_OF_PORTS, sizeof(*rx_rings), GFP_KERNEL);
	if (!rx_rings)
		return -ENOMEM;

	cq_rings = kcalloc(NIC_NUMBER_OF_PORTS, sizeof(*cq_rings), GFP_KERNEL);
	if (!cq_rings) {
		rc = -ENOMEM;
		goto cq_rings_fail;
	}

	wq_rings = kcalloc(NIC_NUMBER_OF_PORTS, sizeof(*wq_rings), GFP_KERNEL);
	if (!wq_rings) {
		rc = -ENOMEM;
		goto qp_rings_fail;
	}

	core_info->rx_rings = rx_rings;
	core_info->cq_rings = cq_rings;
	core_info->wq_rings = wq_rings;
	core_info->kernel_asid = HL_KERNEL_ASID_ID;
	core_info->raw_qpn = RAW_QPN;
	core_info->tx_ring_len = NIC_TX_BUF_SIZE;
	core_info->schedq_num = TXS_PORT_RAW_SCHED_Q * HL_EN_PFC_PRIO_NUM + GAUDI2_PFC_PRIO_DRIVER;
	core_info->temporal_polling = !hdev->nic_poll_enable;

	/* As a W/A for H/W bug H6-3399, we increase our Tx packets by padding them with bigger
	 * value than the default. This should keep the MAC in the other side busier on each packet
	 * processing, hence decrease the rate that it pushes the packet towards the Rx.
	 */
	core_info->pad_size = NIC_SKB_PAD_SIZE;

	aux_ops->configure_cq = gaudi2_nic_configure_cq;
	aux_ops->arm_cq = gaudi2_nic_arm_cq;
	aux_ops->write_rx_ci = gaudi2_nic_write_rx_ci;
	aux_ops->get_pfc_cnts = gaudi2_nic_get_pfc_cnts;
	aux_ops->ring_tx_doorbell = gaudi2_nic_ring_tx_doorbell;
	aux_ops->dma_map_tx_pkt = gaudi2_nic_dma_map_tx_pkt;
	aux_ops->dma_unmap_tx_pkt = gaudi2_nic_dma_unmap_tx_pkt;
	aux_ops->qp_err_syndrom_to_str = gaudi2_nic_qp_err_syndrom_to_str;
	aux_ops->db_fifo_reset = gaudi2_nic_db_fifo_reset;

	nic->ctrl_op_mask = BIT(HL_NIC_OP_ALLOC_CONN) |
			BIT(HL_NIC_OP_SET_REQ_CONN_CTX) |
			BIT(HL_NIC_OP_SET_RES_CONN_CTX) |
			BIT(HL_NIC_OP_DESTROY_CONN) |
			BIT(HL_NIC_OP_USER_WQ_SET) |
			BIT(HL_NIC_OP_USER_WQ_UNSET) |
			BIT(HL_NIC_OP_SET_USER_APP_PARAMS) |
			BIT(HL_NIC_OP_GET_USER_APP_PARAMS) |
			BIT(HL_NIC_OP_ALLOC_USER_DB_FIFO) |
			BIT(HL_NIC_OP_USER_DB_FIFO_SET) |
			BIT(HL_NIC_OP_USER_DB_FIFO_UNSET) |
			BIT(HL_NIC_OP_EQ_POLL) |
			BIT(HL_NIC_OP_USER_ENCAP_ALLOC) |
			BIT(HL_NIC_OP_USER_ENCAP_SET) |
			BIT(HL_NIC_OP_USER_ENCAP_UNSET) |
			BIT(HL_NIC_OP_ALLOC_USER_CQ_ID) |
			BIT(HL_NIC_OP_USER_CQ_ID_SET) |
			BIT(HL_NIC_OP_USER_CQ_ID_UNSET) |
			BIT(HL_NIC_OP_DUMP_QP);

#ifdef HL_LOAD_IB
	nic->ib_support = true;
#endif

	return 0;

qp_rings_fail:
	kfree(cq_rings);
cq_rings_fail:
	kfree(rx_rings);

	return rc;
}

static void gaudi2_nic_sw_fini(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_en_core_info *core_info = &gaudi2->en_core_info;

	kfree(core_info->wq_rings);
	kfree(core_info->cq_rings);
	kfree(core_info->rx_rings);
}

static void gaudi2_nic_en_set_core_data(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_en_core_info *asic_core_info;
	struct gaudi2_nic_port *gaudi2_port;
	struct hl_en_core_info *core_info;
	struct hl_nic *nic = &hdev->nic;
	struct hl_en_aux_ops *aux_ops;
	struct hl_nic_port *nic_port;
	struct hl_aux_dev *aux_dev;
	int i;

	aux_dev = &nic->en_aux_dev;
	core_info = aux_dev->core_info;
	asic_core_info = &gaudi2->en_core_info;
	core_info->asic_specific = asic_core_info;
	aux_ops = aux_dev->aux_ops;
	aux_ops->asic_ops = &gaudi2->en_aux_ops;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];
		gaudi2_port = nic_port->nic_specific;

		if (nic_port->eth_enable) {
			asic_core_info->rx_rings[i] = &gaudi2_port->rx_ring;
			asic_core_info->cq_rings[i] = &gaudi2_port->cq_rings[NIC_CQ_RAW_IDX];
			asic_core_info->wq_rings[i] = &gaudi2_port->wq_ring;
		}
	}
}

static int gaudi2_register_qp(struct hl_nic_port *nic_port, u32 qp_id, u32 asid)
{
	return hl_nic_eq_dispatcher_register_qp(nic_port, asid, qp_id);
}

static void gaudi2_unregister_qp(struct hl_nic_port *nic_port, u32 qp_id)
{
	hl_nic_eq_dispatcher_unregister_qp(nic_port, qp_id);
}

static void gaudi2_get_qp_id_range(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id)
{
	*min_id = NIC_MIN_CONN_ID;
	*max_id = NIC_MAX_CONN_ID;
}

static u8 gaudi2_qp_event_is_req_event(struct hl_nic_eqe *eqe)
{
	char synd_str_to_lower[GAUDI2_MAX_SYNDROM_STRING_LEN] = {};
	u32 synd = EQE_QP_EVENT_ERR_SYND(eqe);
	char *synd_str;

	synd_str = gaudi2_nic_qp_err_syndrom_to_str(synd);

	if (strlen(synd_str)) {
		strncpy(synd_str_to_lower, synd_str, GAUDI2_MAX_SYNDROM_STRING_LEN - 1);
		hl_nic_strtolower(synd_str_to_lower);

		if (strnstr(synd_str_to_lower, "req", strlen(synd_str_to_lower)))
			return 1;
	}

	return 0;
}

static int gaudi2_eq_poll(struct hl_nic_port *nic_port, u32 asid, struct hl_nic_eq_poll_out *event)
{
	u32 ev_type, ev_valid, port = nic_port->port;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_eqe eqe;
	int rc;

	rc = hl_nic_eq_dispatcher_dequeue(nic_port, asid, &eqe, false);
	if (rc)
		return rc;

	ev_valid = EQE_IS_VALID(&eqe);
	if (!ev_valid) {
		dev_dbg_ratelimited(hdev->dev,
			"got EQE invalid entry while expecting a valid one\n");
			return -ENODATA;
	}

	ev_type = EQE_TYPE(&eqe);
	switch (ev_type) {
	case EQE_COMP_ERR:
		event->ev_type = HL_NIC_EQ_EVENT_TYPE_CQ_ERR;
		event->idx = EQE_CQ_EVENT_CQ_NUM(&eqe);
		break;
	case EQE_QP_ERR:
		event->ev_type = HL_NIC_EQ_EVENT_TYPE_QP_ERR;
		event->idx = EQE_RAW_TX_EVENT_QPN(&eqe);
		event->rest_occurred = EQE_QP_EVENT_RESET(&eqe);
		event->is_req = gaudi2_qp_event_is_req_event(&eqe);
		break;
	case EQE_DB_FIFO_OVERRUN:
		event->ev_type = HL_NIC_EQ_EVENT_TYPE_DB_FIFO_ERR;
		event->idx = EQE_DB_EVENT_DB_NUM(&eqe);
		break;
	case EQE_CONG:
		/* completion ready in cc comp queue */
		event->ev_type = HL_NIC_EQ_EVENT_TYPE_CCQ;
		event->idx = EQE_CQ_EVENT_CQ_NUM(&eqe);
		break;
	case EQE_LINK_STATUS:
		event->ev_type = HL_NIC_EQ_EVENT_TYPE_LINK_STATUS;
		break;
	case EQE_QP_ALIGN_COUNTERS:
		event->ev_type = HL_NIC_EQ_EVENT_TYPE_QP_ALIGN_COUNTERS;
		event->idx = EQE_SW_EVENT_QPN(&eqe);
		break;
	default:
		/* if the event should not be reported to the user then return
		 * as if no event was found
		 */
		dev_dbg_ratelimited(hdev->dev,
				"dropping Port-%d event %d report to user\n",
				port, ev_type);
		return -ENODATA;
	}

	/* fill the vevent-specific data */
	event->ev_data = eqe.data[2];

	return 0;
}

static void gaudi2_get_db_fifo_id_range(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id)
{
	*min_id = GAUDI2_MIN_DB_FIFO_ID;
	*max_id = GAUDI2_MAX_DB_FIFO_ID;
}

static void gaudi2_get_db_fifo_modes_mask(struct hl_nic_port *nic_port, u32 *mode_mask)
{
	*mode_mask = BIT(HL_NIC_DB_FIFO_TYPE_DB) | BIT(HL_NIC_DB_FIFO_TYPE_CC);
}

static int gaudi2_db_fifo_allocate(struct hl_nic_port *nic_port,
				struct hl_nic_db_fifo_idr_pdata *idr_pdata)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 fifo_size;

	switch (idr_pdata->fifo_mode) {
	case HL_NIC_DB_FIFO_TYPE_DB:
	case HL_NIC_DB_FIFO_TYPE_CC:
		fifo_size = DB_FIFO_SIZE;
		break;
	default:
		dev_dbg(hdev->dev, "Port %d, invalid DB fifo mode: %d. Allocation failed\n",
							nic_port->port, idr_pdata->fifo_mode);
		return -EINVAL;
	}

	idr_pdata->fifo_size = fifo_size;

	return 0;
}

static void gaudi2_db_fifo_free(struct hl_nic_port *nic_port, u32 db_pool_offset, u32 fifo_size)
{

}

static int gaudi2_db_fifo_set(struct hl_nic_port *nic_port, struct hl_ctx *ctx, u32 id,
			u64 ci_device_handle, struct hl_nic_db_fifo_idr_pdata *idr_pdata)
{
	struct gaudi2_nic_port *gaudi2_nic = nic_port->nic_specific;
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	u32 mmu_bypass;
	u32 val;
	int rc;
	u8 is_cc;

	rc = gaudi2_nic_eq_dispatcher_register_db(gaudi2_nic, ctx->asid, db_fifo_hw_id(id));
	if (rc)
		return rc;
	/*
	 * Config HW to use memory buffer for updating
	 * consumer-index(CI) when it pops fifo.
	 */
	NIC_WREG32(mmNIC0_QPC0_DBFIFO0_CI_UPD_ADDR_DBFIFO_CI_UPD_ADDR_31_7 +
			db_fifo_offset64(id),
			lower_32_bits(ci_device_handle) >> 7);
	NIC_WREG32(mmNIC0_QPC0_DBFIFO0_CI_UPD_ADDR_DBFIFO_CI_UPD_ADDR_63_32 +
			db_fifo_offset64(id),
			upper_32_bits(ci_device_handle));

	is_cc = (idr_pdata->fifo_mode == HL_NIC_DB_FIFO_TYPE_CC);
	/*
	 * We use generic H/W FIFOs. Configured as a userspace doorbell or congestion control FIFO.
	 */
	NIC_WREG32(mmNIC0_QPC0_DB_FIFO_CFG_0 + db_fifo_offset(id), is_cc);

	mmu_bypass = !!(hdev->nic.mmu_bypass);
	val = ctx->asid;
	val |= mmu_bypass << NIC0_QPC0_DB_FIFO_USER_OVRD_MMU_BYPASS_SHIFT;
	NIC_WREG32(mmNIC0_QPC0_DB_FIFO_USER_OVRD_0 + db_fifo_offset(id), val);

	return 0;
}

static void db_fifo_reset(struct hl_nic_port *nic_port, struct hl_ctx *ctx, u32 id, u64 mmap_handle)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_mem_buf *buf;
	u32 *ci_cpu_addr;

	buf = hl_nic_mem_buf_get(ctx, mmap_handle);
	if (!buf) {
		dev_err(hdev->dev,
			"Failed to retrieve port %d db fifo CI memory\n", nic_port->port);
		return;
	}

	/* Read latest HW updated CI */
	ci_cpu_addr = (u32 *) buf->kernel_address;

	__db_fifo_reset(nic_port, ci_cpu_addr, id, false);

	hl_nic_mem_buf_put(buf);
}

static void gaudi2_db_fifo_unset(struct hl_nic_port *nic_port, struct hl_ctx *ctx, u32 id,
					struct hl_nic_db_fifo_idr_pdata *idr_pdata)
{
	db_fifo_reset(nic_port, ctx, id, idr_pdata->ci_mmap_handle);

	hl_nic_eq_dispatcher_unregister_db(nic_port, db_fifo_hw_id(id));
}

static void gaudi2_get_encap_id_range(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id)
{
	if (nic_port->port & 0x1) {
		*min_id = GAUDI2_USER_ENCAP_ID + 2;
		*max_id = GAUDI2_USER_ENCAP_ID + 2;
	} else {
		*min_id = GAUDI2_USER_ENCAP_ID;
		*max_id = GAUDI2_USER_ENCAP_ID;
	}
}

static void gaudi2_get_default_encap_id(struct hl_nic_port *nic_port, u32 *id)
{
	u32 min, max;

	gaudi2_get_encap_id_range(nic_port, &min, &max);
	*id = max + 1;
}

static int gaudi2_encap_set(struct hl_nic_port *nic_port, u32 encap_id,
				struct hl_nic_encap_idr_pdata *idr_pdata)
{
	u32 encap_cfg = 0, decap_cfg = 0;
	u32 port = nic_port->port;
	struct hl_device *hdev = nic_port->hdev;
	u32 encap_hdr_offset = mmNIC0_TXE0_ENCAP_DATA_63_32_0 -
				mmNIC0_TXE0_ENCAP_DATA_31_0_0;
	int i;
	u32 *encap_header = idr_pdata->encap_header;
	u32 hdr_size;

	NIC_WREG32(mmNIC0_TXE0_SOURCE_IP_PORT0_0 + encap_offset(encap_id), idr_pdata->src_ip);

	encap_cfg |= idr_pdata->encap_type_data &
			NIC0_TXE0_ENCAP_CFG_IPV4_PROTOCOL_UDP_DEST_MASK;

	if (idr_pdata->encap_type == HL_NIC_ENCAP_NONE) {
		NIC_WREG32(mmNIC0_TXE0_ENCAP_CFG_0 + encap_offset(encap_id), encap_cfg);
		return 0;
	}

	if (!IS_ALIGNED(idr_pdata->encap_header_size, sizeof(u32))) {
		dev_err(hdev->dev, "Encap header size(%d) must be a multiple of %ld\n",
			idr_pdata->encap_header_size, sizeof(u32));
		return -EINVAL;
	}

	hdr_size = idr_pdata->encap_header_size / sizeof(u32);
	encap_cfg |= (hdr_size << NIC0_TXE0_ENCAP_CFG_ENCAP_SIZE_SHIFT) &
			NIC0_TXE0_ENCAP_CFG_ENCAP_SIZE_MASK;

	if (idr_pdata->encap_type == HL_NIC_ENCAP_OVER_UDP) {
		encap_cfg |= BIT(NIC0_TXE0_ENCAP_CFG_HDR_FORMAT_SHIFT);
		if (!hdev->nic.is_decap_disabled) {
			decap_cfg |= NIC0_RXB_CORE_TNL_DECAP_UDP_VALID_MASK;
			decap_cfg |= (idr_pdata->encap_type_data <<
						NIC0_RXB_CORE_TNL_DECAP_UDP_UDP_DEST_PORT_SHIFT) &
						NIC0_RXB_CORE_TNL_DECAP_UDP_UDP_DEST_PORT_MASK;
			decap_cfg |= (hdr_size << NIC0_RXB_CORE_TNL_DECAP_UDP_TNL_SIZE_SHIFT) &
					NIC0_RXB_CORE_TNL_DECAP_UDP_TNL_SIZE_MASK;
			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TNL_DECAP_UDP_0 + encap_offset(encap_id),
						decap_cfg);
		}
	} else if (idr_pdata->encap_type == HL_NIC_ENCAP_OVER_IPV4) {
		if (!hdev->nic.is_decap_disabled) {
			decap_cfg |= NIC0_RXB_CORE_TNL_DECAP_IPV4_VALID_MASK;
			decap_cfg |= (idr_pdata->encap_type_data <<
						NIC0_RXB_CORE_TNL_DECAP_IPV4_IPV4_PROTOCOL_SHIFT) &
						NIC0_RXB_CORE_TNL_DECAP_IPV4_IPV4_PROTOCOL_MASK;
			decap_cfg |= (hdr_size << NIC0_RXB_CORE_TNL_DECAP_IPV4_TNL_SIZE_SHIFT) &
					NIC0_RXB_CORE_TNL_DECAP_IPV4_TNL_SIZE_MASK;

			NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TNL_DECAP_IPV4_0 + encap_offset(encap_id),
						decap_cfg);
		}
	}

	NIC_WREG32(mmNIC0_TXE0_ENCAP_CFG_0 + encap_offset(encap_id), encap_cfg);

	/*
	 * Encapsulation header is already aligned to 32 bits. Hence, it's
	 * safe to access it in chunks of 4 bytes.
	 */
	for (i = 0 ; i * sizeof(u32) < idr_pdata->encap_header_size ; i++)
		NIC_WREG32(mmNIC0_TXE0_ENCAP_DATA_31_0_0 + encap_hdr_offset * i +
				encap_offset(encap_id), encap_header[i]);

	return 0;
}

static void gaudi2_encap_unset(struct hl_nic_port *nic_port, u32 encap_id,
				struct hl_nic_encap_idr_pdata *idr_pdata)
{
	u32 port = nic_port->port;
	struct hl_device *hdev = nic_port->hdev;
	u32 encap_hdr_offset = mmNIC0_TXE0_ENCAP_DATA_63_32_0 -
				mmNIC0_TXE0_ENCAP_DATA_31_0_0;
	int i;

	NIC_WREG32(mmNIC0_TXE0_SOURCE_IP_PORT0_0 + encap_offset(encap_id), 0);
	NIC_WREG32(mmNIC0_TXE0_ENCAP_CFG_0 + encap_offset(encap_id), 0);

	if (idr_pdata->encap_type == HL_NIC_ENCAP_OVER_UDP)
		NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TNL_DECAP_UDP_0 + encap_offset(encap_id), 0);
	else if (idr_pdata->encap_type == HL_NIC_ENCAP_OVER_IPV4)
		NIC_MACRO_WREG32(mmNIC0_RXB_CORE_TNL_DECAP_IPV4_0 + encap_offset(encap_id), 0);

	for (i = 0 ; i * sizeof(u32) < idr_pdata->encap_header_size ; i++)
		NIC_WREG32(mmNIC0_TXE0_ENCAP_DATA_31_0_0 + encap_hdr_offset * i +
				encap_offset(encap_id), 0);
}

static u32 gaudi2_nic_get_default_port_speed(struct hl_device *hdev)
{
	return SPEED_100000;
}

static void gaudi2_nic_get_cnts_names(struct hl_nic_port *nic_port, u8 *data, bool ext)
{
	char str[HL_IB_CNT_NAME_LEN], *rx_fmt, *tx_fmt;
	struct hl_en_stat *spmu_stats;
	u32 n_spmu_stats;
	int i, len;

	if (ext) {
		len = HL_IB_CNT_NAME_LEN;
		rx_fmt = "rx_%s";
		tx_fmt = "tx_%s";
	} else {
		len = ETH_GSTRING_LEN;
		rx_fmt = "%s";
		tx_fmt = "%s";
	}

	gaudi2_nic_spmu_get_stats_info(nic_port, &spmu_stats, &n_spmu_stats);

	for (i = 0 ; i < n_spmu_stats ; i++)
		memcpy(data + i * len, spmu_stats[i].str, ETH_GSTRING_LEN);
	data += i * len;

	/* Skip MAC counters in simulator */
	if (!nic_port->hdev->nic.skip_mac_cnts) {
		for (i = 0 ; i < hl_nic_mac_stats_rx_len ; i++) {
			memset(str, 0, len);
			snprintf(str, len, rx_fmt, hl_nic_mac_stats_rx[i].str);
			memcpy(data + i * len, str, len);
		}
		data += i * len;

		for (i = 0 ; i < gaudi2_nic_mac_fec_stats_len ; i++)
			memcpy(data + i * len, gaudi2_nic_mac_fec_stats[i].str, ETH_GSTRING_LEN);
		data += i * len;

		for (i = 0 ; i < hl_nic_mac_stats_tx_len ; i++) {
			memset(str, 0, len);
			snprintf(str, len, tx_fmt, hl_nic_mac_stats_tx[i].str);
			memcpy(data + i * len, str, len);
		}
		data += i * len;
	}

	for (i = 0 ; i < gaudi2_nic_err_stats_len ; i++)
		memcpy(data + i * len, gaudi2_nic_err_stats[i].str, ETH_GSTRING_LEN);
	data += i * len;

	for (i = 0 ; i < gaudi2_nic_perf_stats_len ; i++)
		memcpy(data + i * len, gaudi2_nic_perf_stats[i].str, ETH_GSTRING_LEN);
}

static int gaudi2_nic_get_cnts_num(struct hl_nic_port *nic_port)
{
	int n_spmu_stats, mac_counters;
	struct hl_en_stat *ignore;

	gaudi2_nic_spmu_get_stats_info(nic_port, &ignore, &n_spmu_stats);
	/* Skip MAC counters in simulator */
	mac_counters = !nic_port->hdev->nic.skip_mac_cnts ? hl_nic_mac_stats_rx_len +
				hl_nic_mac_stats_tx_len + gaudi2_nic_mac_fec_stats_len : 0;

	return n_spmu_stats + mac_counters + gaudi2_nic_err_stats_len + gaudi2_nic_perf_stats_len;
}

static int gaudi2_nic_get_mac_tx_stats(struct hl_nic_port *nic_port, u64 *data)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	u64 start_reg, lo_part, hi_part;
	int i;

	start_reg = (port & 1) ? mmNIC0_MAC_GLOB_STAT_TX2_ETHERSTATSOCTETS_6 :
				mmNIC0_MAC_GLOB_STAT_TX0_ETHERSTATSOCTETS_4;

	for (i = 0 ; i < hl_nic_mac_stats_tx_len ; i++) {
		lo_part = NIC_MACRO_RREG32(start_reg + hl_nic_mac_stats_tx[i].lo_offset);
		/* Upper part must be read after lower part, since the upper part register
		 * gets its value only after the lower part was read.
		 */
		hi_part = NIC_MACRO_RREG32(mmNIC0_MAC_GLOB_STAT_CONTROL_REG_DATA_HI);

		data[i] = lo_part | (hi_part << 32);
	}

	return i;
}

static int gaudi2_nic_get_mac_rx_stats(struct hl_nic_port *nic_port, u64 *data)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	u64 start_reg, lo_part, hi_part;
	int i;

	start_reg = (port & 1) ? mmNIC0_MAC_GLOB_STAT_RX2_ETHERSTATSOCTETS_2 :
				mmNIC0_MAC_GLOB_STAT_RX0_ETHERSTATSOCTETS;

	for (i = 0 ; i < hl_nic_mac_stats_rx_len ; i++) {
		lo_part = NIC_MACRO_RREG32(start_reg + hl_nic_mac_stats_rx[i].lo_offset);
		/* Upper part must be read after lower part, since the upper part register
		 * gets its value only after the lower part was read.
		 */
		hi_part = NIC_MACRO_RREG32(mmNIC0_MAC_GLOB_STAT_CONTROL_REG_DATA_HI);

		data[i] = lo_part | (hi_part << 32);
	}

	return i;
}

static int __gaudi2_nic_get_mac_fec_stats(struct hl_nic_port *nic_port, u64 *data)
{
	u64 start_jiffies, diff_ms, numerator, denominator, integer, exp;
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	int i;

	if (!data)
		return 0;

	/* Read the relevant registers in order to clear them */
	if (port & 1) {
		nic_port->correctable_errors_cnt +=
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC2_CCW_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC2_CCW_HI) << 16);
		nic_port->uncorrectable_errors_cnt +=
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC2_NCCW_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC2_NCCW_HI) << 16);

		for (i = 0 ; i < 8 ; i++)
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR4_LO + 4 * i);
	} else {
		nic_port->correctable_errors_cnt +=
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_CCW_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_CCW_HI) << 16);
		nic_port->uncorrectable_errors_cnt +=
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_NCCW_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_NCCW_HI) << 16);

		for (i = 0 ; i < 8 ; i++)
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR0_LO + 4 * i);
	}

	start_jiffies = jiffies;

	/* sleep some time to accumulate stats */
	msleep(ACCUMULATE_FEC_STATS_DURATION_MS);

	diff_ms = jiffies_to_msecs(jiffies - start_jiffies);

	if (port & 1) {
		data[FEC_CW_CORRECTED] =
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC2_CCW_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC2_CCW_HI) << 16);
		data[FEC_CW_UNCORRECTED] =
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC2_NCCW_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC2_NCCW_HI) << 16);
		data[FEC_SYMBOL_ERR_CORRECTED_LANE_0] =
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR4_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR4_HI) << 16);
		data[FEC_SYMBOL_ERR_CORRECTED_LANE_1] =
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR5_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR5_HI) << 16);
		data[FEC_SYMBOL_ERR_CORRECTED_LANE_2] =
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR6_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR6_HI) << 16);
		data[FEC_SYMBOL_ERR_CORRECTED_LANE_3] =
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR7_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR7_HI) << 16);
	} else {
		data[FEC_CW_CORRECTED] =
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_CCW_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_CCW_HI) << 16);
		data[FEC_CW_UNCORRECTED] =
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_NCCW_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_NCCW_HI) << 16);
		data[FEC_SYMBOL_ERR_CORRECTED_LANE_0] =
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR0_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR0_HI) << 16);
		data[FEC_SYMBOL_ERR_CORRECTED_LANE_1] =
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR1_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR1_HI) << 16);
		data[FEC_SYMBOL_ERR_CORRECTED_LANE_2] =
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR2_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR2_HI) << 16);
		data[FEC_SYMBOL_ERR_CORRECTED_LANE_3] =
			NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR3_LO) |
				(NIC_MACRO_RREG32(mmNIC0_MAC_RS_FEC_RSFEC_SYMBLERR3_HI) << 16);
	}

	nic_port->correctable_errors_cnt += data[FEC_CW_CORRECTED];
	nic_port->uncorrectable_errors_cnt += data[FEC_CW_UNCORRECTED];

	data[FEC_CW_CORRECTED_ACCUM] = nic_port->correctable_errors_cnt;
	data[FEC_CW_UNCORRECTED_ACCUM] = nic_port->uncorrectable_errors_cnt;

	/* The denominator is the total number of symbols in the measured time T ms
	 * (100G bits/sec = 10G sym/sec = 10G * T/1000 sym = 1G * T / 100 sym)
	 */
	denominator = div_u64((u64) (1 << 30) * diff_ms, 100);

	/* Pre FEC: the numerator is the sum of uncorrected symbols (~= uncorrected_cw * 16) and
	 * corrected symbols.
	 */
	numerator = data[FEC_CW_UNCORRECTED] << 4;
	for (i = 0 ; i < 4 ; i++)
		numerator += data[FEC_SYMBOL_ERR_CORRECTED_LANE_0 + i];

	hl_nic_get_frac_info(numerator, denominator, &integer, &exp);

	data[FEC_PRE_FEC_SER_INT] = integer;
	data[FEC_PRE_FEC_SER_EXP] = exp;

	/* Post FEC: the numerator is the uncorrected symbols (~= uncorrected_cw * 16) */
	numerator = data[FEC_CW_UNCORRECTED] << 4;

	hl_nic_get_frac_info(numerator, denominator, &integer, &exp);

	data[FEC_POST_FEC_SER_INT] = integer;
	data[FEC_POST_FEC_SER_EXP] = exp;

	return (int) gaudi2_nic_mac_fec_stats_len;
}

static int gaudi2_nic_get_mac_stats(struct hl_nic_port *nic_port, u64 *data)
{
	int cnt = 0;

	/* Skip MAC counters in simulator */
	if (nic_port->hdev->nic.skip_mac_cnts)
		return 0;

	cnt += gaudi2_nic_get_mac_rx_stats(nic_port, &data[cnt]);
	cnt += __gaudi2_nic_get_mac_fec_stats(nic_port, &data[cnt]);
	cnt += gaudi2_nic_get_mac_tx_stats(nic_port, &data[cnt]);

	return cnt;
}

static int gaudi2_nic_get_nic_err_stats(struct hl_nic_port *nic_port, u64 *data)
{
	struct gaudi2_nic_port *gaudi2_nic = nic_port->nic_specific;
	struct gaudi2_device *gaudi2 = nic_port->hdev->asic_specific;
	struct hl_aux_dev *aux_dev = &gaudi2_nic->hdev->nic.en_aux_dev;
	int i = 0;

	data[i++] = gaudi2_nic->cong_q_err_cnt;
	data[i++] = nic_port->eth_enable ?
			gaudi2->en_aux_ops.get_overrun_cnt(aux_dev, nic_port->port) : 0;

	return i;
}

static int gaudi2_nic_get_perf_stats(struct hl_nic_port *nic_port, u64 *data)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_properties *nic_prop;
	u64 bw_dividend, bw_int, bw_frac;
	u64 lat_dividend, lat_divisor, lat_int, lat_frac;
	u32 port = nic_port->port;

	nic_prop = &hdev->asic_prop.nic_props;

	/* Bandwidth calculation */
	bw_dividend = (((u64) NIC_RREG32(mmNIC0_TXE0_STATS_MEAS_WIN_BYTES_MSB)) << 32) |
				NIC_RREG32(mmNIC0_TXE0_STATS_MEAS_WIN_BYTES_LSB);

	/* bytes to bits */
	bw_dividend *= BITS_PER_BYTE;

	bw_int = div_u64(bw_dividend, PERF_BW_DIV);
	bw_frac = ((bw_dividend - PERF_BW_DIV * bw_int) * 10) / PERF_BW_DIV;

	/* In case there is no traffic (BW=0), the latency will show the last measured value (when
	 * there was traffic). Therefore, we need to clear it.
	 */
	if (bw_int == 0 && bw_frac == 0) {
		lat_int = 0;
		lat_frac = 0;
	} else {
		/* Latency calculation */
		lat_dividend = (((u64) NIC_RREG32(mmNIC0_TXE0_STATS_TOT_BYTES_MSB)) << 32) |
					NIC_RREG32(mmNIC0_TXE0_STATS_TOT_BYTES_LSB);
		lat_divisor = nic_prop->clk;

		lat_int = div_u64(lat_dividend, lat_divisor);
		lat_frac = ((lat_dividend - lat_divisor * lat_int) * 10) / (lat_divisor);
	}

	data[PERF_BANDWIDTH_INT] = bw_int;
	data[PERF_BANDWIDTH_FRAC] = bw_frac;
	data[PERF_LATENCY_INT] = lat_int;
	data[PERF_LATENCY_FRAC] = lat_frac;

	return (int) gaudi2_nic_perf_stats_len;
}

static void gaudi2_nic_get_cnts_values(struct hl_nic_port *nic_port, u64 *data)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 cnt = 0;
	int rc;

	rc = hl_nic_read_spmu_counters(nic_port, &data[cnt], &cnt);
	if (rc)
		dev_err(hdev->dev, "Failed to get SPMU counters, port %d\n", nic_port->port);

	cnt += gaudi2_nic_get_mac_stats(nic_port, &data[cnt]);
	cnt += gaudi2_nic_get_nic_err_stats(nic_port, &data[cnt]);
	cnt += gaudi2_nic_get_perf_stats(nic_port, &data[cnt]);
}

static void gaudi2_nic_reset_mac_stats(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	NIC_MACRO_WREG32(mmNIC0_MAC_GLOB_STAT_CONTROL_REG_STATN_CONFIG,
			BIT(NIC0_MAC_GLOB_STAT_CONTROL_REG_STATN_CONFIG_F_RESET_SHIFT));
}

void gaudi2_nic_get_mac_fec_stats(struct hl_nic_port *nic_port, u64 *data)
{
	__gaudi2_nic_get_mac_fec_stats(nic_port, data);
}

/*
 * SW-61283 HW bug: QP stuck in limited state in case there is a race between
 * timeout and receive ACK.
 * Description: When timeout occurs QPC resets NTS to ONA. If an ACK arives to
 * the RX, the RX reads the QPC and timeout occurs after the read response to
 * the RX and before the RX update indication, the NTS will rollback to the ONA.
 * After the RX handles the ACK, it will do an update, and may advance the ONA
 * ahead of the NTS.
 * In this case the QP will go into limited state forever:
 * NTS - ONA > congestion window
 */
static void __qpc_sanity_check(struct gaudi2_nic_port *gaudi2_nic, u32 qpn)
{
	struct hl_nic_port *nic_port;
	struct hl_device *hdev;
	struct gaudi2_qpc_requester req_qpc = {};
	struct qpc_mask qpc_mask = {};
	u32 ona_psn, nts_psn, in_work, bcs_psn, bcc_psn, rem_pi, ona_rem_pi,
	    consumer_idx, execution_idx, is_valid, port, wq_type;
	int retry_cnt_in_work = 0, retry_cnt_qpc_timeout = 0;
	int rc;

	nic_port = gaudi2_nic->nic_port;
	hdev = nic_port->hdev;
	port = nic_port->port;

retry:
	rc = gaudi2_nic_qpc_read(nic_port, (void *) &req_qpc, qpn, true);
	if (rc) {
		dev_err_ratelimited(hdev->dev,
			"Requester port %d QPC %d read failed\n", port, qpn);
		return;
	}

	is_valid = REQ_QPC_GET_VALID(req_qpc);
	if (!is_valid)
		return;

	/* When the timeout retry counter is non zero, an ack could potentially arrive and increase
	 * ONA, after QPC was read.
	 */
	if (REQ_QPC_GET_TIMEOUT_RETRY_COUNT(req_qpc)) {
		if (retry_cnt_qpc_timeout < RETRY_COUNT_QPC_SANITY) {
			dev_dbg(hdev->dev, "QPC timeout retry count > 0, trying again #%d\n",
				retry_cnt_qpc_timeout);
			usleep_range(1000, 1500);
			retry_cnt_qpc_timeout++;
			goto retry;
		} else {
			dev_dbg(hdev->dev, "Can't apply fix. QPC timeout retry count > 0, after %d QPC reads",
						retry_cnt_qpc_timeout);
			return;
		}
	}

	in_work = REQ_QPC_GET_IN_WORK(req_qpc);

	ona_psn = REQ_QPC_GET_ONA_PSN(req_qpc);
	nts_psn = REQ_QPC_GET_NTS_PSN(req_qpc);

	bcs_psn = REQ_QPC_GET_BCS_PSN(req_qpc);
	bcc_psn = REQ_QPC_GET_BCC_PSN(req_qpc);

	consumer_idx = REQ_QPC_GET_CONSUMER_IDX(req_qpc);
	execution_idx = REQ_QPC_GET_EXECUTION_IDX(req_qpc);

	rem_pi = REQ_QPC_GET_REMOTE_PRODUCER_IDX(req_qpc);
	ona_rem_pi = REQ_QPC_GET_OLDEST_UNACKED_REMOTE_PRODUCER_IDX(req_qpc);

	wq_type = REQ_QPC_GET_WQ_TYPE(req_qpc);

	/*
	 * We hit the HW bug. Unacknowledged PSN can never be greater than next
	 * PSN to be sent out.
	 */
	if (NIC_IS_PSN_CYCLIC_BIG(ona_psn, nts_psn)) {
		struct hl_nic_eqe eqe;

		dev_dbg(hdev->dev, "ona_psn(%d) nts_psn(%d), bcc_psn(%d) bcs_psn(%d), consumer_idx(%d) execution_idx(%d). Retry_cnt %d\n",
			ona_psn, nts_psn, bcc_psn, bcs_psn, consumer_idx, execution_idx,
			retry_cnt_in_work);

		/* Wait till HW stops working on QPC. */
		if (in_work && retry_cnt_in_work < RETRY_COUNT_QPC_SANITY) {
			usleep_range(1000, 1500);
			retry_cnt_in_work++;
			goto retry;
		}

		dev_info(hdev->dev, "Port %d QP %d in limited state. Applying fix.\n", port, qpn);

		/* Force update QPC fields. */

		REQ_QPC_SET_NTS_PSN(qpc_mask, 0xffffff);
		REQ_QPC_SET_BCS_PSN(qpc_mask, 0xffffff);
		REQ_QPC_SET_EXECUTION_IDX(qpc_mask, 0x3fffff);
		if (wq_type == QPC_REQ_WQ_TYPE_WRITE)
			REQ_QPC_SET_REMOTE_PRODUCER_IDX(qpc_mask, 0x3fffff);

		REQ_QPC_SET_NTS_PSN(req_qpc, ona_psn);
		REQ_QPC_SET_BCS_PSN(req_qpc, bcc_psn);
		REQ_QPC_SET_EXECUTION_IDX(req_qpc, consumer_idx);
		if (wq_type == QPC_REQ_WQ_TYPE_WRITE)
			REQ_QPC_SET_REMOTE_PRODUCER_IDX(req_qpc, ona_rem_pi);

		rc = gaudi2_nic_qpc_write_masked(nic_port, (void *) &req_qpc, &qpc_mask,
								qpn, true, true);
		if (rc)
			dev_err(hdev->dev, "Requester port %d QPC %d write failed\n", port, qpn);

		eqe.data[0] = EQE_HEADER(true, EQE_QP_ALIGN_COUNTERS);
		eqe.data[1] = qpn;

		rc = hl_nic_eq_dispatcher_enqueue(nic_port, &eqe);
		if (rc)
			dev_err(hdev->dev, "port %d QPC %d failed dispatching EQ event %d\n",
						port, qpn, EQE_QP_ALIGN_COUNTERS);

	}
}

/*
 * We sanitize one QP at a time since it's high latency operation,
 * too heavy to do it in one shot. We mitigate this via interleaving
 * with thread scheduling.
 */
static void gaudi2_qp_sanity_work(struct work_struct *work)
{
	struct gaudi2_nic_port *gaudi2_nic =
		container_of(work, struct gaudi2_nic_port, qp_sanity_work.work);
	struct hl_nic_port *nic_port = gaudi2_nic->nic_port;
	struct hl_device *hdev = nic_port->hdev;
	u32 timeout_cnt, port;
	struct hl_qp *qp;
	int qp_id;

	port = nic_port->port;
	timeout_cnt = NIC_RREG32(mmNIC0_QPC0_NUM_TIMEOUTS);

	if (gaudi2_nic->qp_timeout_cnt == timeout_cnt)
		goto done;

	gaudi2_nic->qp_timeout_cnt = timeout_cnt;

	mutex_lock(&gaudi2_nic->cfg_lock);
	idr_for_each_entry(&nic_port->qp_ids, qp, qp_id)
		if (qp && qp->is_req)
			__qpc_sanity_check(gaudi2_nic, qp_id);
	mutex_unlock(&gaudi2_nic->cfg_lock);

done:
	queue_delayed_work(gaudi2_nic->qp_sanity_wq, &gaudi2_nic->qp_sanity_work,
				msecs_to_jiffies(QPC_SANITY_CHECK_INTERVAL_MS));
}

static int gaudi2_qp_sanity_init(struct gaudi2_nic_port *gaudi2_nic)
{
	struct hl_nic_port *nic_port = gaudi2_nic->nic_port;
	struct hl_device *hdev = gaudi2_nic->hdev;
	char wq_name[30] = {0};
	u32 port = nic_port->port;

	/* The qp sanity work is relevant only for external ports */
	if (!nic_port->eth_enable)
		return 0;

	snprintf(wq_name, sizeof(wq_name) - 1, "hl%u-nic%d-qp-sanity", hdev->cdev_idx, port);

	gaudi2_nic->qp_sanity_wq = create_singlethread_workqueue(wq_name);
	if (!gaudi2_nic->qp_sanity_wq) {
		dev_err(hdev->dev, "Failed to create QP sanity WQ, port: %d\n", port);
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&gaudi2_nic->qp_sanity_work, gaudi2_qp_sanity_work);
	queue_delayed_work(gaudi2_nic->qp_sanity_wq, &gaudi2_nic->qp_sanity_work,
				msecs_to_jiffies(QPC_SANITY_CHECK_INTERVAL_MS));

	return 0;
}

static void gaudi2_qp_sanity_fini(struct gaudi2_nic_port *gaudi2_nic)
{
	if (!gaudi2_nic->qp_sanity_wq)
		return;

	cancel_delayed_work_sync(&gaudi2_nic->qp_sanity_work);
	destroy_workqueue(gaudi2_nic->qp_sanity_wq);
}

static void gaudi2_nic_user_ccq_set(struct hl_nic_port *nic_port, u64 ccq_device_addr,
						u64 pi_device_addr, u32 num_of_entries, u32 *ccqn)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_BASE_ADDR_63_32, upper_32_bits(ccq_device_addr));
	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_BASE_ADDR_31_7, ((ccq_device_addr >> 7) & 0x1FFFFFF));

	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_PI_ADDR_63_32, upper_32_bits(pi_device_addr));
	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_PI_ADDR_31_7, ((pi_device_addr >> 7) & 0x1FFFFFF));

	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_WRITE_INDEX, 0);
	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_PRODUCER_INDEX, 0);
	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_CONSUMER_INDEX, 0);
	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_CONSUMER_INDEX_CB, 0);
	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_LOG_SIZE, ilog2(num_of_entries));

	/*
	 * set enable + update-pi
	 * set overrun-en to allow overrun of ci since a HW bug exist
	 * in Gaudi2 which prevents updating ci. it will be solved in Gaudi3
	 */
	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_CFG,
		NIC0_QPC0_CONG_QUE_CFG_ENABLE_MASK |
		NIC0_QPC0_CONG_QUE_CFG_OVERRUN_EN_MASK |
		NIC0_QPC0_CONG_QUE_CFG_WRITE_PI_EN_MASK);

	/* gaudi2 has only 1 CCQ. Therefore, set 0 as ccqn. */
	*ccqn = 0;
}

static void  gaudi2_nic_user_ccq_unset(struct hl_nic_port *nic_port, u32 *ccqn)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_CFG, 0);
	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_PI_ADDR_63_32, 0);
	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_PI_ADDR_31_7, 0);
	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_BASE_ADDR_63_32, 0);
	NIC_WREG32(mmNIC0_QPC0_CONG_QUE_BASE_ADDR_31_7, 0);

	/* gaudi2 has only 1 CCQ. Therefore, set 0 as ccqn. */
	*ccqn = 0;
}

static void gaudi2_nic_fill_spmu_data(struct hl_nic_port *nic_port,
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

	nic_status->bad_format_cnt = 0;
	nic_status->responder_out_of_sequence_psn_cnt = cpu_to_le32(spmu_data[3]);
}

static void gaudi2_nic_fill_fec_stats(struct hl_nic_port *nic_port,
					struct cpucp_nic_status *nic_status)
{
	u64 fec_data[FEC_STAT_LAST];

	memset(fec_data, 0, sizeof(fec_data));

	gaudi2_nic_get_mac_fec_stats(nic_port, fec_data);

	nic_status->correctable_err_cnt = cpu_to_le32(fec_data[FEC_CW_CORRECTED_ACCUM]);
	nic_status->uncorrectable_err_cnt = cpu_to_le32(fec_data[FEC_CW_UNCORRECTED_ACCUM]);
	nic_status->pre_fec_ser.integer = cpu_to_le16(fec_data[FEC_PRE_FEC_SER_INT]);
	nic_status->pre_fec_ser.exp = cpu_to_le16(fec_data[FEC_PRE_FEC_SER_EXP]);
	nic_status->post_fec_ser.integer = cpu_to_le16(fec_data[FEC_POST_FEC_SER_INT]);
	nic_status->post_fec_ser.exp = cpu_to_le16(fec_data[FEC_POST_FEC_SER_EXP]);
}

static void gaudi2_nic_fill_perf_stats(struct hl_nic_port *nic_port,
					struct cpucp_nic_status *nic_status)
{
	u64 perf_data[PERF_STAT_LAST];

	memset(perf_data, 0, sizeof(perf_data));

	gaudi2_nic_get_perf_stats(nic_port, perf_data);

	nic_status->bandwidth.integer = cpu_to_le16(perf_data[PERF_BANDWIDTH_INT]);
	nic_status->bandwidth.frac = cpu_to_le16(perf_data[PERF_BANDWIDTH_FRAC]);
	nic_status->lat.integer = cpu_to_le16(perf_data[PERF_LATENCY_INT]);
	nic_status->lat.frac = cpu_to_le16(perf_data[PERF_LATENCY_FRAC]);
}

static u32 gaudi2_nic_get_timeout_retransmission_cnt(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	return NIC_RREG32(mmNIC0_QPC0_NUM_TIMEOUTS);
}

static u32 gaudi2_nic_get_high_ber_cnt(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	if (port & 1)
		return NIC_RREG32(mmNIC0_MAC_CH2_MAC_PCS_BER_HIGH_ORDER_CNT);
	else
		return NIC_RREG32(mmNIC0_MAC_CH0_MAC_PCS_BER_HIGH_ORDER_CNT);
}

static void gaudi2_nic_fill_nic_status(struct hl_nic_port *nic_port,
					struct cpucp_nic_status *nic_status)
{
	u32 timeout_retransmission_cnt, high_ber_cnt;

	gaudi2_nic_fill_spmu_data(nic_port, nic_status);
	gaudi2_nic_fill_fec_stats(nic_port, nic_status);
	gaudi2_nic_fill_perf_stats(nic_port, nic_status);

	timeout_retransmission_cnt = gaudi2_nic_get_timeout_retransmission_cnt(nic_port);
	high_ber_cnt = gaudi2_nic_get_high_ber_cnt(nic_port);

	nic_status->timeout_retransmission_cnt = cpu_to_le32(timeout_retransmission_cnt);
	nic_status->high_ber_cnt = cpu_to_le32(high_ber_cnt);
}

static void gaudi2_nic_cfg_lock(struct hl_nic_port *nic_port)
	__acquires(&gaudi2_nic->cfg_lock)
{
	struct gaudi2_nic_port *gaudi2_nic = nic_port->nic_specific;

	mutex_lock(&gaudi2_nic->cfg_lock);
}

static void gaudi2_nic_cfg_unlock(struct hl_nic_port *nic_port)
	__releases(&gaudi2_nic->cfg_lock)
{
	struct gaudi2_nic_port *gaudi2_nic = nic_port->nic_specific;

	mutex_unlock(&gaudi2_nic->cfg_lock);
}

static bool gaudi2_nic_cfg_is_locked(struct hl_nic_port *nic_port)
{
	struct gaudi2_nic_port *gaudi2_nic = nic_port->nic_specific;

	return mutex_is_locked(&gaudi2_nic->cfg_lock);
}

void gaudi2_nic_compute_reset_prepare(struct hl_device *hdev)
{
	gaudi2_nic_eq_enter_temporal_polling_mode(hdev);
}

void gaudi2_nic_compute_reset_late_init(struct hl_device *hdev)
{
	gaudi2_nic_eq_exit_temporal_polling_mode(hdev);
}

static bool gaudi2_nic_is_coll_conn_id(struct hl_device *hdev, u32 conn_id)
{
	return false;
}

static u32 gaudi2_nic_get_max_msg_sz(struct hl_device *hdev)
{
	return SZ_1G;
}

static void gaudi2_nic_app_params_clear(struct hl_device *hdev)
{

}

static int gaudi2_nic_send_cpucp_packet(struct hl_nic_port *nic_port,
					enum cpucp_packet_id packet_id, int val)
{
	struct hl_device *hdev = nic_port->hdev;
	struct cpucp_packet pkt;
	int rc = 0;

	memset(&pkt, 0, sizeof(pkt));
	pkt.ctl = cpu_to_le32(packet_id << CPUCP_PKT_CTL_OPCODE_SHIFT);
	pkt.value = cpu_to_le64(val);
	pkt.macro_index = cpu_to_le32(nic_port->port);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &pkt, sizeof(pkt), 0, NULL);
	if (rc)
		dev_err(hdev->dev,
			"Failed to send cpucp packet, port %d packet id %d, val %d, error %d\n",
			nic_port->port, packet_id, val, rc);

	return rc;
}

static void gaudi2_nic_set_port_status(struct hl_nic_port *nic_port, bool up)
{
	nic_port->link_eqe.data[2] = !!up;
}

static int gaudi2_nic_inject_rx_err(struct hl_device *hdev, u8 drop_percent)
{
	/* NoOps */
	return 0;
}

static bool gaudi2_nic_is_encap_supported(struct hl_device *hdev,
						struct hl_nic_user_encap_set_in *in)
{
	if (in->encap_type != HL_NIC_ENCAP_OVER_UDP) {
		dev_dbg(hdev->dev, "Encap type %u is not supported\n", in->encap_type);
		return false;
	}

	if (in->tnl_hdr_size != NIC_MAX_TNL_HDR_SIZE) {
		dev_dbg(hdev->dev, "Encap hdr-size must be %d\n", NIC_MAX_TNL_HDR_SIZE);
		return false;
	}

	return true;
}

void gaudi2_fw_nic_status(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;

	hl_fw_unmask_irq(hdev, nic_props->base_status_event_idx + nic_port->port);
}

static struct hl_nic_port_funcs gaudi2_nic_port_funcs = {
	.port_hw_init = gaudi2_nic_port_hw_init,
	.port_hw_fini = gaudi2_nic_port_hw_fini,
	.phy_port_init = gaudi2_nic_phy_port_init,
	.phy_port_start_stop = gaudi2_nic_phy_port_start_stop,
	.phy_port_power_up = gaudi2_nic_phy_port_power_up,
	.phy_port_reconfig = gaudi2_nic_phy_port_reconfig,
	.phy_port_fini = gaudi2_nic_phy_port_fini,
	.phy_link_status_work = gaudi2_nic_phy_link_status_work,
	.update_qp_mtu = gaudi2_nic_update_qp_mtu,
	.user_wq_arr_unset = gaudi2_user_wq_arr_unset,
	.get_cq_id_range = gaudi2_get_cq_id_range,
	.user_cq_set = gaudi2_user_cq_set,
	.user_cq_unset = gaudi2_user_cq_unset,
	.user_cq_destroy = gaudi2_user_cq_destroy,
	.get_cnts_num = gaudi2_nic_get_cnts_num,
	.get_cnts_names = gaudi2_nic_get_cnts_names,
	.get_cnts_values = gaudi2_nic_get_cnts_values,
	.port_sw_init = gaudi2_nic_port_sw_init,
	.port_sw_fini = gaudi2_nic_port_sw_fini,
	.spmu_get_stats_info = gaudi2_nic_spmu_get_stats_info,
	.spmu_config = gaudi2_nic_spmu_config,
	.spmu_sample = gaudi2_nic_spmu_sample,
	.register_qp = gaudi2_register_qp,
	.unregister_qp = gaudi2_unregister_qp,
	.get_qp_id_range = gaudi2_get_qp_id_range,
	.eq_poll = gaudi2_eq_poll,
	.eq_dispatcher_select_dq = gaudi2_nic_eq_dispatcher_select_dq,
	.get_db_fifo_id_range = gaudi2_get_db_fifo_id_range,
	.db_fifo_set = gaudi2_db_fifo_set,
	.db_fifo_unset = gaudi2_db_fifo_unset,
	.get_db_fifo_umr = gaudi2_nic_get_db_fifo_umr,
	.get_db_fifo_modes_mask = gaudi2_get_db_fifo_modes_mask,
	.db_fifo_allocate = gaudi2_db_fifo_allocate,
	.db_fifo_free = gaudi2_db_fifo_free,
	.set_pfc = gaudi2_nic_set_pfc,
	.get_encap_id_range = gaudi2_get_encap_id_range,
	.encap_set = gaudi2_encap_set,
	.encap_unset = gaudi2_encap_unset,
	.set_ip_addr_encap = gaudi2_default_encap_set,
	.qpc_write = gaudi2_nic_qpc_write,
	.qpc_invalidate = gaudi2_nic_qpc_invalidate,
	.qpc_query = gaudi2_nic_qpc_query,
	.qpc_clear = gaudi2_nic_qpc_clear,
	.user_ccq_set = gaudi2_nic_user_ccq_set,
	.user_ccq_unset = gaudi2_nic_user_ccq_unset,
	.reset_mac_stats = gaudi2_nic_reset_mac_stats,
	.collect_fec_stats = gaudi2_nic_debugfs_collect_fec_stats,
	.disable_wqe_index_checker = gaudi2_nic_disable_wqe_index_checker_fw,
	.fill_nic_status = gaudi2_nic_fill_nic_status,
	.cfg_lock = gaudi2_nic_cfg_lock,
	.cfg_unlock = gaudi2_nic_cfg_unlock,
	.cfg_is_locked = gaudi2_nic_cfg_is_locked,
	.override_phy_readiness = gaudi2_nic_override_phy_readiness,
	.qp_pre_destroy = gaudi2_nic_qp_pre_destroy,
	.qp_post_destroy = gaudi2_nic_qp_post_destroy,
	.send_cpucp_packet = gaudi2_nic_send_cpucp_packet,
	.set_port_status = gaudi2_nic_set_port_status,
	.fw_nic_status = gaudi2_fw_nic_status,
};

struct hl_nic_funcs gaudi2_nic_funcs = {
	.pre_core_init = gaudi2_nic_pre_core_init,
	.core_init = gaudi2_nic_core_init,
	.core_fini = gaudi2_nic_core_fini,
	.get_hw_cap = gaudi2_nic_get_hw_cap,
	.set_hw_cap = gaudi2_nic_set_hw_cap,
	.set_req_qp_ctx = gaudi2_set_req_qp_ctx,
	.set_res_qp_ctx = gaudi2_set_res_qp_ctx,
	.user_wq_arr_set = gaudi2_user_wq_arr_set,
	.user_set_app_params = gaudi2_user_set_app_params,
	.user_get_app_params = gaudi2_user_get_app_params,
	.phy_reset_macro = gaudi2_nic_phy_reset_macro,
	.phy_get_crc = gaudi2_nic_phy_get_crc,
	.get_phy_fw_name = gaudi2_nic_phy_get_fw_name,
	.phy_fw_load_all = gaudi2_nic_phy_fw_load_all,
	.get_default_port_speed = gaudi2_nic_get_default_port_speed,
	.sw_init = gaudi2_nic_sw_init,
	.sw_fini = gaudi2_nic_sw_fini,
	.macro_sw_init = gaudi2_nic_macro_sw_init,
	.macro_sw_fini = gaudi2_nic_macro_sw_fini,
	.kernel_ctx_init = gaudi2_nic_kernel_ctx_init,
	.kernel_ctx_fini = gaudi2_nic_kernel_ctx_fini,
	.ctx_init = gaudi2_nic_ctx_init,
	.ctx_fini = gaudi2_nic_ctx_fini,
	.qp_read = gaudi2_nic_debugfs_qp_read,
	.wqe_read = gaudi2_nic_debugfs_wqe_read,
	.set_en_core_data = gaudi2_nic_en_set_core_data,
	.request_irqs = gaudi2_nic_eq_request_irqs,
	.synchronize_irqs = gaudi2_nic_eq_sync_irqs,
	.free_irqs = gaudi2_nic_eq_free_irqs,
	.is_coll_conn_id = gaudi2_nic_is_coll_conn_id,
	.phy_dump_serdes_params = gaudi2_nic_phy_dump_serdes_params,
	.get_max_msg_sz = gaudi2_nic_get_max_msg_sz,
	.qp_syndrome_to_str = gaudi2_nic_qp_err_syndrom_to_str,
	.app_params_clear = gaudi2_nic_app_params_clear,
	.inject_rx_err = gaudi2_nic_inject_rx_err,
	.is_encap_supported = gaudi2_nic_is_encap_supported,
	.port_funcs = &gaudi2_nic_port_funcs,
};
