// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3_nic.h"
#include "../include/gaudi3/asic_reg/gaudi3_regs.h"
#include "../include/hw_ip/nic/nic_general.h"
#include "uapi/misc/habanalabs.h"
#include <uapi/linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>

#define GAUDI3_PFC_PRIO_DRIVER			0
#define GAUDI3_PFC_PRIO_USER_BASE		1

#define GAUDI3_NIC_MTU_DEFAULT		(8 * (1 << 10)) /* 8KB */

#define GAUDI3_MIN_DB_FIFO_ID(port)	(RDMA_OFFSET + ELEMENT_OFFSET((port), QPC_DB_FIFO_NUM))
#define GAUDI3_MAX_DB_FIFO_ID(port)	\
		(GAUDI3_MIN_DB_FIFO_ID(port) + ELEMENT_COUNT(QPC_DB_FIFO_NUM) - RDMA_OFFSET - 1)
#define GAUDI3_DB_FIFO_NUM(port)	\
				(GAUDI3_MAX_DB_FIFO_ID(port) - GAUDI3_MIN_DB_FIFO_ID(port) + 1)

#define DB_FIFO_PORT_OFFSET(port)	\
		(((port) & 1) ? (DB_FIFO_ETH_SIZE + GAUDI3_DB_FIFO_NUM(port) *	\
							DB_FIFO_RDMA_SIZE) : 0)

/* User doorbell fifo 32 bit register offset. */
#define db_fifo_offset(id) ((id) * 4)

/* User encapsulation IDs */
#define GAUDI3_DEFAULT_ENCAP_ID(port)	ELEMENT_OFFSET((port), TXE_ENCAP_NUM)
#define GAUDI3_MIN_ENCAP_ID(port)	(GAUDI3_DEFAULT_ENCAP_ID(port) + 1)
#define GAUDI3_MAX_ENCAP_ID(port)	\
				(GAUDI3_DEFAULT_ENCAP_ID(port) + ELEMENT_COUNT(TXE_ENCAP_NUM) - 1)

#define GAUDI3_CQ_RAW_IDX(port)	(GAUDI3_RAW_OFFSET + ELEMENT_OFFSET((port), CQ_UMR_NUM))

#define GAUDI3_MIN_CQ_RDMA_IDX(port) (RDMA_OFFSET + ELEMENT_OFFSET((port), CQ_UMR_NUM))
#define GAUDI3_MAX_CQ_RDMA_IDX(port) \
	(GAUDI3_MIN_CQ_RDMA_IDX(port) + ELEMENT_COUNT(CQ_UMR_NUM) - 1 - RDMA_OFFSET)

#define GAUDI3_AFA_IDX(port)	(ELEMENT_OFFSET((port), AFA_REG_NUM))
#define GAUDI3_AFA_ARUSER_IDX(port)	(ELEMENT_OFFSET((port), AFA_ARUSER_REG_NUM))
#define GAUDI3_AFA_MASK_IDX(port)	(ELEMENT_OFFSET((port), AFA_MASK_REG_NUM))

#define GAUDI3_EQ_RAW_IDX(port)	(GAUDI3_RAW_OFFSET + ELEMENT_OFFSET((port), QPC_EQ_NUM))
#define GAUDI3_EQ_RDMA_IDX(port)       GAUDI3_EQ_RAW_IDX(port)
#define GAUDI3_TXE_WQ_RAW_IDX(port)	(GAUDI3_RAW_OFFSET + ELEMENT_OFFSET((port), TXE_SQ_NUM))
#define GAUDI3_DB_FIFO_RAW_IDX(port)	\
		(GAUDI3_RAW_OFFSET + ELEMENT_OFFSET((port), QPC_DB_FIFO_NUM))

#define RAW_QPN(port)	(GAUDI3_RAW_OFFSET + ELEMENT_OFFSET((port), NIC_MAX_GEN_QP_NUM))
#define GAUDI3_MIN_QP_ID(port)	(RDMA_OFFSET + ELEMENT_OFFSET((port), NIC_MAX_GEN_QP_NUM))
#define GAUDI3_MAX_QP_ID(port)	\
		(GAUDI3_MIN_QP_ID(port) + ELEMENT_COUNT(NIC_MAX_GEN_QP_NUM) - RDMA_OFFSET - 1)

#define GAUDI3_MIN_COLL_QP_ID		NIC_MAX_GEN_QP_NUM
#define GAUDI3_MAX_COLL_QP_ID		\
		(GAUDI3_MIN_COLL_QP_ID + ELEMENT_COUNT(NIC_MAX_COLL_QP_NUM) - 1)

#define GAUDI3_NIC_WTD_BP_UPPER_TH_DIFF	4
#define GAUDI3_NIC_WTD_BP_LOWER_TH_DIFF	8

#define GAUDI3_SYNDROME_TYPE(syndrome)	((syndrome >> 7) & 0x7)
#define GAUDI3_SYNDROM_IS_RX(src)	((src == 4) || (src == 6))
#define GAUDI3_SYNDROM_IS_TX(src)	(src == 5)
#define GAUDI3_MAX_SYNDROM_STRING_LEN	256
#define GAUDI3_MAX_SYNDROMS		0x400
#define GAUDI3_MAX_SYNDROME_TYPE	3
#define GAUDI3_ERR_CAUSE_QPC_SHIFT	64

#define GAUDI3_SYNDROME_CAUSE(syndrome)	(syndrome & 0x7f)
#define GAUDI3_SYNDROME_CAUSE_IS_QPC(cause)	(cause >= 64)

#define GAUDI3_MAX_LAG_SIZE		63

#define GAUDI3_CC_MIN_WINDOW_SIZE	1
#define GAUDI3_CC_MAX_WINDOW_SIZE	1024

#define AFA_REG_NUM	REG_NUM(mmD0_NIC0_RXE_AFA_LBW_ADDR_0_3 - mmD0_NIC0_RXE_AFA_LBW_ADDR_0_0)
#define AFA_ARUSER_REG_NUM	REG_NUM(mmD0_NIC0_RXE_AFA_ARUSER_ATTR_3 - \
						mmD0_NIC0_RXE_AFA_ARUSER_ATTR_0)

#define AFA_MASK_REG_NUM	REG_NUM(mmD0_NIC0_RXE_AFA_MASK_SIZE_3 - \
					mmD0_NIC0_RXE_AFA_MASK_SIZE_0)
#define TXE_DSCP_NUM	REG_NUM(mmD0_NIC0_TXE_PRIO_TO_DSCP_3 - mmD0_NIC0_TXE_PRIO_TO_DSCP_0)
#define TXE_SRC_IP_NUM	REG_NUM(mmD0_NIC0_TXE_SOURCE_IP_PORT0_7 - mmD0_NIC0_TXE_SOURCE_IP_PORT0_0)
#define TXE_ENCAP_NUM	REG_NUM(mmD0_NIC0_TXE_ENCAP_DATA_31_0_7 - mmD0_NIC0_TXE_ENCAP_DATA_31_0_0)

#define RXE_CQ_NUM	REG_NUM(mmD0_NIC0_RXE_CQ_BASE_ADDR_HI_15 - mmD0_NIC0_RXE_CQ_BASE_ADDR_HI_0)
#define RXB_RAW_NUM	REG_NUM(mmD0_NIC0_RXB_CORE_PRT_TS_RAW0_MAC_31_0_MASK_3 - \
				mmD0_NIC0_RXB_CORE_PRT_TS_RAW0_MAC_31_0_MASK_0)
#define RXE_RAW_QPN_NUM	REG_NUM(mmD0_NIC0_RXE_RAW_QPN_P3_1 - mmD0_NIC0_RXE_RAW_QPN_P0_0)
/* The RXE registers area contains holes. Hence we can't just calculate the offset between the last
 * and the first registers, but to use this hack in order to get the correct number of registers
 * from the REG_NUM macro.
 */
#define RXE_RAW_BASE_NUM	REG_NUM((mmD0_NIC0_RXE_RAW_BASE_LO_P2_0 - \
				mmD0_NIC0_RXE_RAW_BASE_LO_P0_0) * 2 - 4)
#define CQ_UMR_NUM	ELEMENT_NUM((mmD0_NIC0_CQ_UMR_15_BASE - mmD0_NIC0_CQ_UMR_0_BASE), \
				NIC_CQ_UMR_OFFSET)
#define PHY_RX_NUM	REG_NUM(mmD0_NIC0_PHY_PHY_RX_CFG_3 - mmD0_NIC0_PHY_PHY_RX_CFG_0)
#define QPC_CONG_QUE_NUM	REG_NUM(mmD0_NIC0_QPC_CONG_QUE_CFG_3 - mmD0_NIC0_QPC_CONG_QUE_CFG_0)
#define BP_OFFS_MSG_EN_NUM	REG_NUM(mmD0_NIC0_QPC_WQ_BP_MSG_EN_3 - mmD0_NIC0_QPC_WQ_BP_MSG_EN_0)

#define QPC_COLL_LAG_SZ_DEFAULT		0x3
#define QPC_RX_WQE_CT_MASK_DEFAULT	0x2
#define NIC_TMR_TIMEOUT_US		1000 /* 1 msec */

#define IPv4_PROTOCOL_UDP	17
#define IPv4_PROTOCOL_DUMMY	200

#define PERF_BW_DIV		1000000

/* As part of the ECO support for H9-5384, lower part of DECAP mask must be 0*/
#define RX_DROP_ECO_DCAP_UNSET_MASK	0xFFFF

static int gaudi3_encap_set(struct hl_nic_port *nic_port, u32 encap_id,
				struct hl_nic_encap_idr_pdata *idr_pdata);

static u8 db_fifo_sw_to_hw_map[] = {
	[HL_NIC_DB_FIFO_TYPE_DB] = DB_FIFO_TYPE_DOORBELL,
	[HL_NIC_DB_FIFO_TYPE_CC] = DB_FIFO_TYPE_BBR,
	[HL_NIC_DB_FIFO_TYPE_COLL_OPS_SHORT] = DB_FIFO_TYPE_SCHEDULER_DESC,
	[HL_NIC_DB_FIFO_TYPE_COLL_OPS_LONG] = DB_FIFO_TYPE_SCHEDULER_DESC,
	[HL_NIC_DB_FIFO_TYPE_DWQ_LIN] = DB_FIFO_TYPE_LINER_WQE,
	[HL_NIC_DB_FIFO_TYPE_DWQ_MS] = DB_FIFO_TYPE_MULTISTRIDE_WQE,
	[HL_NIC_DB_FIFO_TYPE_COLL_DIR_OPS_SHORT] = DB_FIFO_TYPE_DIRECT_PATCHER,
	[HL_NIC_DB_FIFO_TYPE_COLL_DIR_OPS_LONG] = DB_FIFO_TYPE_DIRECT_PATCHER,
};

union rr_qpc {
	struct gaudi3_qpc_requester req;
	struct gaudi3_qpc_responder res;
};

enum qp_err_synd_src {
	GAUDI3_SYNDROME_ERR_SRC_RXE = 0,
	GAUDI3_SYNDROME_ERR_SRC_QPC,
	GAUDI3_SYNDROME_ERR_SRC_TXE
};

static const char qp_err_eng_src_strs[][GAUDI3_MAX_SYNDROM_STRING_LEN] = {
		"RXE", "QPC", "TXE" };

static const char qp_err_qpc_strs[][8][GAUDI3_MAX_SYNDROM_STRING_LEN] = {
	{
		"[qpc] [req DB] QP not valid",
		"[qpc] [req DB] ASID not valid",
		"[qpc] [req DB] security check",
		"[qpc] [req DB] (PI - CI) > last-index",
		"[qpc] [req DB] wq-type is READ",
	},
	{
		"[qpc] [patcher DB] QP not valid",
		"[qpc] [patcher DB] ASID not valid",
		"[qpc] [patcher DB] security check",
	},
	{
		"[qpc] [CC DB] QP not valid",
		"[qpc] [CC DB] ASID not valid",
		"[qpc] [CC DB] security check",
	},
	{
		"[qpc] [res TX] QP not valid",
	},
	{
		"[qpc] [res RX] QP not valid",
	},
	{
		"[qpc] [req TX] QP not valid",
		"[qpc] [req TX] RDV WQE but wq-type is not WRITE",
	},
	{
		"[qpc] [req RX] QP not valid",
		"[qpc] [req RX] max-retry-cnt exceeded",
		"[qpc] [TMR] max-retry-cnt exceeded",
	},
	{
		"[qpc] [req RDV] QP not valid",
		"[qpc] [req RDV] wrong wq-type",
	},
};

static const char qp_err_rxe_strs[][GAUDI3_MAX_SYNDROM_STRING_LEN] = {
	"[RX] pkt err, pkt bad format",
	"[RX] pkt err, parser FSM invalid",
	"[RX] pkt err, HDR size invalid",
	"[RX] pkt err, IPv4-len invalid",
	"[RX] pkt err, IPv6-len invalid",
	"[RX] pkt err, pkt tunnel invalid",
	"[RX] pkt err, parser hint invalid",
	"[RX] pkt err, BTH opcode invalid",
	"[RX] pkt err, syndrome invalid",
	"[RX] pkt err, RC max size invalid",
	"[RX] pkt err, RC min size invalid",
	"[RX] pkt err, Raw pkt invalid",
	"[RX] pkt err, Raw max size invalid",
	"[RX] pkt err, Raw min size invalid",
	"[RX] pkt err, Raw max size invalid",
	"[RX] QPC err, QP invalid",
	"[RX] QPC err, QPC Transport Service mismatch",
	"[RX] QPC err, QPC Requester connection state invalid",
	"[RX] QPC err, QPC Responder Connection state invalid",
	"[RX] QPC err, QPC Responder resync invalid",
	"[RX] QPC err, QPC Requester PSN invalid",
	"[RX] QPC err, QPC Requester PSN unset",
	"[RX] QPC err, Requester SAL NTS invalid",
	"[RX] QPC err, QPC Responder RKEY invalid",
	"[RX] QPC err, Requester SAL PSN invalid",
	"[RX] WQE err, WQE-index miss-match",
	"[RX] WQE err, WQE write opcode invalid",
	"[RX] WQE err, WQE Rendezvous opcode invalid",
	"[RX] WQE err, WQE Read opcode invalid",
	"[RX] WQE err, WQE Write Zero",
	"[RX] WQE err, WQE multi zero",
	"[RX] WQE err, WQE Write send big",
	"[RX] WQE err, WQE multi big",
};

struct gaudi3_nic_stat {
	char str[ETH_GSTRING_LEN];
};

static struct gaudi3_nic_stat gaudi3_nic_err_stats[] = {
	{"Congestion Q err"},
	{"Eth DB fifo overrun"},
};

#define FEC_MAX_SYMBOL_ERR (FEC_CW_CORRECTED_15_SYMBOL_ERR - FEC_CW_CORRECTED_1_SYMBOL_ERR + 1)

static struct hl_en_stat gaudi3_nic_mac_fec_stats[] = {
	{"cw_received",      0x0},
	{"cw_correct",       0x4},
	{"cw_uncorrectable", 0x8},
	{"cw_corrected",     0xc},
	{"cw_corrected_1_symbol_err", 0x10},
	{"cw_corrected_2_symbol_err", 0x14},
	{"cw_corrected_3_symbol_err", 0x18},
	{"cw_corrected_4_symbol_err", 0x1c},
	{"cw_corrected_5_symbol_err", 0x20},
	{"cw_corrected_6_symbol_err", 0x24},
	{"cw_corrected_7_symbol_err", 0x28},
	{"cw_corrected_8_symbol_err", 0x2c},
	{"cw_corrected_9_symbol_err", 0x30},
	{"cw_corrected_10_symbol_err", 0x34},
	{"cw_corrected_11_symbol_err", 0x38},
	{"cw_corrected_12_symbol_err", 0x3c},
	{"cw_corrected_13_symbol_err", 0x40},
	{"cw_corrected_14_symbol_err", 0x44},
	{"cw_corrected_15_symbol_err", 0x48},
	{"symbol_err_corrected_lane_0", 0x4c},
	{"symbol_err_corrected_lane_1", 0x50},
	{"symbol_err_corrected_lane_2", 0x54},
	{"symbol_err_corrected_lane_3", 0x58},
	{"post_FEC_SER"},
	{"pre_FEC_SER"},
};

/* Gaudi3 performance Stats */
enum gaudi3_nic_perf_stats_type {
	PERF_BANDWIDTH_INT,
	PERF_BANDWIDTH_FRAC,
	PERF_LATENCY_INT,
	PERF_LATENCY_FRAC,
	PERF_STAT_LAST
};

static struct gaudi3_nic_stat gaudi3_nic_perf_stats[] = {
	{"bandwidth_gbps_int"},
	{"bandwidth_gbps_frac"},
	{"last_data_latency_usec_int"},
	{"last_data_latency_usec_frac"},
};

static size_t gaudi3_nic_err_stats_len = ARRAY_SIZE(gaudi3_nic_err_stats);
static size_t gaudi3_nic_mac_fec_stats_len = ARRAY_SIZE(gaudi3_nic_mac_fec_stats);
static size_t gaudi3_nic_perf_stats_len = ARRAY_SIZE(gaudi3_nic_perf_stats);

static const char qp_err_txe_strs[][128] = {
	"[TX] QPC.wq_type is write does not support WQE.opcode",
	"[TX] QPC.wq_type is rendezvous does not support WQE.opcode",
	"[TX] QPC.wq_type is read does not support WQE.opcode",
	"[TX] WQE is inline but does not support WQE.opcode",
	"[TX] WQE.opcode is write but WQE.size is 0",
	"[TX] WQE.opcode is multi-stride|local-stride|multi-dual but WQE.size is 0",
	"[TX] WQE.opcode is send but WQE.size is 0",
	"[TX] WQE.opcode is rendezvous-write|rendezvous-read but WQE.size is 0",
	"[TX] WQE.opcode is write but size > configured max-write-send-size",
	"[TX] WQE.opcode is multi-stride|local-stride|multi-dual but size > configured max-stride-size",
	"[TX] WQE.opcode is rendezvous-write|rendezvous-read but QPC.remote_wq_log_size <= configured min-remote-log-size",
	"[TX] WQE.opcode is rendezvous-write but WQE.size != configured rdv-wqe-size (per granularity)",
	"[TX] WQE.opcode is rendezvous-read but WQE.size != configured rdv-wqe-size (per granularity)",
	"[TX] WQE.inline is set but WQE.size != configured inline-wqe-size (per granularity)",
	"[TX] WQE.opcode is multi-stride|local-stride|multi-dual but QPC.swq_granularity is 0",
	"[TX] QP-RAW and not compression nor down/up-convert",
	"[TX] WQE.opcode is multi-stride|local-stride|multi-dual but WQE.size < stride-size",
	"[TX] Upscale with unaligned remote address",
	"[TX] WQE.reduction_opcode is upscale but does not support WQE.opcode",
	"[TX] RAW packet but WQE.size not supported",
	"[TX] QP-SACK with WQE NOP",
	"[TX] QP-RAW with non-linear WQE opcode",
	"[TX] Wrong opcode for QP-plain-RDMA",
	"[TX] QP-plain RDMA with up/down-convert",
	"[TX] Down/up-convert with non 4B align size/address/stride",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] N/A",
	"[TX] WQE's sent bytes exceeds WQE's size",
	"[TX] QPC WQ-log-size below cfg",
	"[TX] QPC WQ-log-size above cfg",
	"[TX] QP-RAW with WQE size above cfg",
	"[TX] QP-RAW with WQE size below cfg",
	"[TX] WQE.opcode is RD-RDV but WQE.inline is set",
	"[TX] WQE fetch&add WR size not 0",
	"[TX] WQE fetch&add WR addr not mod4",
	"[TX] WQE.opcode above 15",
	"[TX] WQE bad opcode",
	"[TX] WQE bad size",
	"[TX] Tunnel 0-size",
	"[TX] Tunnel max size",
};

static const char * const
gaudi3_nic_rxe_spi_interrupts_cause_1[] = {
	"[RX] WQE err, WQE multi big",
};

static const char * const
gaudi3_nic_rxb_core_spi_interrupts_cause[] = {
	"Packet dropped due to no available buffers",
	"Control pointers count illegal, port 0",
	"Control pointers count illegal, port 1",
	"Control pointers count illegal, port 2",
	"Control pointers count illegal, port 3",
	"Scatter pointers count illegal",
	"Size mismatch between comrpession and pkt",
	"NIC-DBG CS interrupt (SPMU or BMON)"
};

static const char * const
gaudi3_nic_rxe_sei_interrupts_cause[] = {
	"HBW RRESP error WQE",
	"HBW RRESP error FNA",
	"LBW BRESP error",
	"HBW BRESP error"
};

static const char * const
gaudi3_nic_rxb_core_sei_interrupts_cause[] = {
	"HBW RRESP error",
	"LBW RRESP error"
};

static const char * const
gaudi3_nic_tmr_interrupts_cause[] = {
	"Memory read resp Slave Error",
	"Memory read resp Slave Error Reserved",
	"Memory write resp Slave Error",
	"Memory write resp Slave Error Reserved",
};

static const char * const
gaudi3_nic_qpc_interrupts_resp_err_cause[] = {
	"HBW memory read resp Slave Error",
	"HBW memory read resp Slave Error Reserved",
	"HBW memory write resp Slave Error",
	"HBW memory write resp Slave Error Reserved",
	"LBW memory write resp Slave Error",
	"LBW memory write resp Slave Error Reserved",
	"ARC SEI interrupt (from NIC's QMAN)",
};

static const char * const
gaudi3_nic_txs_interrupts_cause[] = {
	"Memory read resp Slave Error",
	"Memory read resp Slave Error Reserved",
	"Memory write resp Slave Error",
	"Memory write resp Slave Error Reserved",
};

static const char * const
gaudi3_nic_txe_interrupts_cause[] = {
	"Control Bus Memory read resp Slave Error",
	"Control Bus Memory read resp Slave Error Reserved",
	"Data Bus Memory read resp Slave Error",
	"Data Bus Memory read resp Slave Error Reserved",
};

char *gaudi3_nic_qp_err_src_to_str(u32 syndrome)
{
	u8 err_cause, err_qpc_src, err_src  = GAUDI3_SYNDROME_ERR_SRC_QPC;

	err_cause = GAUDI3_SYNDROME_CAUSE(syndrome);
	err_qpc_src = GAUDI3_SYNDROME_TYPE(syndrome);

	if (GAUDI3_SYNDROM_IS_RX(err_qpc_src) && !GAUDI3_SYNDROME_CAUSE_IS_QPC(err_cause))
		err_src = GAUDI3_SYNDROME_ERR_SRC_RXE;
	else if (GAUDI3_SYNDROM_IS_TX(err_qpc_src) && !GAUDI3_SYNDROME_CAUSE_IS_QPC(err_cause))
		err_src = GAUDI3_SYNDROME_ERR_SRC_TXE;

	return (char *) qp_err_eng_src_strs[err_src];
}

char *gaudi3_nic_qp_err_syndrom_to_str(u32 syndrome)
{
	uint8_t err_src, err_qpc_src, err_cause;
	char *synd_str;

	err_cause = GAUDI3_SYNDROME_CAUSE(syndrome);
	err_qpc_src = GAUDI3_SYNDROME_TYPE(syndrome);
	/* The error source by default is QPC */
	err_src = GAUDI3_SYNDROME_ERR_SRC_QPC;
	if (GAUDI3_SYNDROM_IS_RX(err_qpc_src)) {
		/* The error source is RX. now check if it generated by RXE or QPC */
		if (!GAUDI3_SYNDROME_CAUSE_IS_QPC(err_cause))
			err_src = GAUDI3_SYNDROME_ERR_SRC_RXE;
		else
			err_cause -= GAUDI3_ERR_CAUSE_QPC_SHIFT;

	} else if (GAUDI3_SYNDROM_IS_TX(err_qpc_src)) {
		/* The error source is TX. now check if it generated by TXE or QPC */
		if (!GAUDI3_SYNDROME_CAUSE_IS_QPC(err_cause))
			err_src = GAUDI3_SYNDROME_ERR_SRC_TXE;
		else
			err_cause -= GAUDI3_ERR_CAUSE_QPC_SHIFT;

	}

	if (err_src == GAUDI3_SYNDROME_ERR_SRC_RXE)
		synd_str = (char *) qp_err_rxe_strs[err_cause];
	else if (err_src == GAUDI3_SYNDROME_ERR_SRC_QPC)
		synd_str = (char *) qp_err_qpc_strs[err_qpc_src][err_cause];
	else
		synd_str = (char *) qp_err_txe_strs[err_cause];

	return synd_str;
}

static void gaudi3_nic_reset_rings(struct gaudi3_nic_port *gaudi3_nic);

static bool is_400g_mode(struct hl_device *hdev)
{
	return hdev->nic_lanes_per_port == PORT_LANES_4;
}

static bool is_200g_mode(struct hl_device *hdev)
{
	return !is_400g_mode(hdev);
}

static u8 get_drv_cqn(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;

	return GAUDI3_MIN_CQ_RDMA_IDX(nic_port->port);
}

int get_resource_offset(struct hl_device *hdev, u32 port, int num_of_resources)
{
	/*
	 * In 400G mode we have a single port per macro so all resources belong to that port and
	 * hence offset 0 should be used.
	 * In 200G mode we have two port per macro so the even port should use the lower half of the
	 * resources (offset 0) and the odd port should use the upper half.
	 */
	return (is_400g_mode(hdev) || !(port & 1)) ? 0 : (num_of_resources / 2);
}

int get_resource_count(struct hl_device *hdev, int num_of_resources)
{
	return is_400g_mode(hdev) ? num_of_resources : (num_of_resources / 2);
}

/* get the index of the first port in the macro */
u32 gaudi3_nic_get_first_port(struct hl_nic_macro *nic_macro)
{
	u32 port = nic_macro->idx;

	if (is_200g_mode(nic_macro->hdev))
		port *= 2;

	return port;
}

u64 gaudi3_nic_get_macro_ports_mask(struct hl_nic_macro *nic_macro)
{
	u64 macro_ports_mask = is_400g_mode(nic_macro->hdev) ? 0x1 : 0x3;

	return macro_ports_mask << gaudi3_nic_get_first_port(nic_macro);
}

static int __get_lane_offset(struct hl_device *hdev, u32 port)
{
	return get_resource_offset(hdev, port, NIC_MAC_NUM_OF_LANES) + NIC_MAC_LANES_START;
}

int get_lane_offset(struct gaudi3_nic_port *gaudi3_nic)
{
	return __get_lane_offset(gaudi3_nic->hdev, gaudi3_nic->port);
}

u32 get_port_from_lane(struct hl_nic_macro *nic_macro, u8 lane)
{
	u32 port = nic_macro->idx;

	if (is_400g_mode(nic_macro->hdev))
		return port;

	port *= 2;

	if (lane >= (NIC_MAC_NUM_OF_LANES >> 1))
		port++;

	return port;
}

bool gaudi3_nic_is_macro_enabled(struct hl_device *hdev, struct hl_nic_macro *nic_macro)
{
	u32 port1, port2;

	/* In 400Gbps mode we have a single port in each macro.
	 * In 200Gbps mode we need to check also the second port in the macro. If any of the two
	 * ports are enabled, then the corresponding macro is enabled
	 */
	if (is_400g_mode(hdev)) {
		port1 = nic_macro->idx;

		return (hdev->nic_ports_mask & BIT(port1));
	}

	/* 200G mode */
	port1 = nic_macro->idx * 2;
	port2 = port1 + 1;

	return ((hdev->nic_ports_mask & BIT(port1)) ||
			(hdev->nic_ports_mask & BIT(port2)));
}

static int gaudi3_nic_set_pfc(struct hl_nic_port *nic_port)
{
	struct gaudi3_nic_port *gaudi3_nic = nic_port->nic_specific;
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, val;
	int lane;

	lane = get_lane_offset(gaudi3_nic);

	val = NIC_MAC_RREG32(mmNIC_MAC_CH_MAC_CH0_COMMAND_CONFIG, lane);

	val |= D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_TX_ENA_M |
		D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_RX_ENA_M |
		D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_PROMIS_EN_M |
		D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_TX_PAD_EN_M;

	if (nic_port->pfc_enable) {
		val |= D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_PFC_MODE_M;
		val &= ~(D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_PAUSE_IGNORE_M |
			D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_CMD_FRAME_ENA_M);
	} else {
		val |= D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_PAUSE_IGNORE_M |
			D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_CMD_FRAME_ENA_M;
		val &= ~D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_PFC_MODE_M;
	}

	NIC_MAC_WREG32(mmNIC_MAC_CH_MAC_CH0_COMMAND_CONFIG, lane, val);

	return 0;
}

static int gaudi3_db_fifo_allocate(struct hl_nic_port *nic_port,
				struct hl_nic_db_fifo_idr_pdata *idr_pdata)
{
	struct gaudi3_nic_macro *gaudi3_macro = nic_port->nic_macro->asic_priv;
	struct hl_device *hdev = nic_port->hdev;
	u64 gen_pool_offset;
	u32 fifo_size;

	switch (idr_pdata->fifo_mode) {
	case HL_NIC_DB_FIFO_TYPE_DB:
		fifo_size = DB_FIFO_RDMA_SIZE;
		break;

	case HL_NIC_DB_FIFO_TYPE_CC:
		fifo_size = DB_FIFO_CC_SIZE;
		break;

	case HL_NIC_DB_FIFO_TYPE_COLL_OPS_SHORT:
		fifo_size = DB_FIFO_COLL_OPS_SHORT_SIZE;
		break;

	case HL_NIC_DB_FIFO_TYPE_COLL_OPS_LONG:
		fifo_size = DB_FIFO_COLL_OPS_LONG_SIZE;
		break;

	case HL_NIC_DB_FIFO_TYPE_COLL_DIR_OPS_SHORT:
		fifo_size = DB_FIFO_COLL_OPS_DIR_SHORT_SIZE;
		break;

	case HL_NIC_DB_FIFO_TYPE_COLL_DIR_OPS_LONG:
		fifo_size = DB_FIFO_COLL_OPS_DIR_LONG_SIZE;
		break;

	case HL_NIC_DB_FIFO_TYPE_DWQ_LIN:
		fifo_size = DB_FIFO_DIRECT_WQ_LINEAR_SIZE;
		break;

	case HL_NIC_DB_FIFO_TYPE_DWQ_MS:
		fifo_size = DB_FIFO_DIRECT_WQ_MULTI_STRIDE_SIZE;
		break;

	default:
		dev_dbg(hdev->dev, "Invalid DB fifo mode: %d. Allocation failed\n",
									idr_pdata->fifo_mode);
		return -EINVAL;
	}

	gen_pool_offset = gen_pool_alloc(gaudi3_macro->db_fifo_pool, fifo_size);
	if (!gen_pool_offset)
		return -ENOMEM;

	idr_pdata->db_pool_addr = gen_pool_offset;
	idr_pdata->fifo_offset = gen_pool_offset - gaudi3_macro->db_fifo_start_addr;
	idr_pdata->fifo_size = fifo_size;

	return 0;
}

static void gaudi3_db_fifo_free(struct hl_nic_port *nic_port, u32 db_pool_offset, u32 fifo_size)
{
	struct gaudi3_nic_macro *gaudi3_macro = nic_port->nic_macro->asic_priv;

	/* Return the fifo memory to the pool. */
	gen_pool_free(gaudi3_macro->db_fifo_pool, db_pool_offset, fifo_size);
}

static void gaudi3_nic_config_hw_txs(struct hl_nic_macro *nic_macro)
{
	u32 txs_schedq, txs_fence_idx, txs_pi, txs_ci, txs_tail, txs_head,
		txs_timeout_31_0, timeout_47_32, prio, txs_port, rl_en_log_time, port, reg;
	struct hl_device *hdev = nic_macro->hdev;
	struct hl_nic_properties *nic_prop;
	struct gaudi3_nic_port *gaudi3_nic;
	struct gaudi3_device *gaudi3;
	void *cpu_addr;
	u64 txs_addr;
	int i;

	gaudi3 = hdev->asic_specific;
	port = gaudi3_nic_get_first_port(nic_macro);
	gaudi3_nic = &gaudi3->nic_ports[port];
	nic_prop = &hdev->asic_prop.nic_props;

	if (hdev->dram_enable) {
		txs_addr = nic_prop->txs_base_addr + nic_macro->idx * nic_prop->txs_base_size;

		/* TX sched-Qs list */
		for (i = 0 ; i < TXS_FREE_NUM_ENTRIES ; i++) {
			hl_nic_dram_writel(hdev,
					TXS_GRANULARITY + i, txs_addr + TXS_FREE_OFFS + i * 4);
		}

		/* Perform read to flush the writes */
		hl_nic_dram_readq(hdev, txs_addr);
	} else {
		cpu_addr = gaudi3_nic->txs_mem.addr;
		txs_addr = gaudi3_nic->txs_mem.dma_addr;

		/* TX sched-Qs list */
		for (i = 0 ; i < TXS_FREE_NUM_ENTRIES ; i++)
			writel(TXS_GRANULARITY + i,
				(void __iomem *)cpu_addr + TXS_FREE_OFFS + i * 4);
	}

	/* set TX sched queues address */
	NIC_WREG32(mmD0_NIC0_TXS_BASE_ADDRESS_63_32,
			upper_32_bits(txs_addr + TXS_FIFO_OFFS));
	NIC_WREG32(mmD0_NIC0_TXS_BASE_ADDRESS_31_7,
			lower_32_bits(txs_addr + TXS_FIFO_OFFS) >> 7);

	/* Set access to bypass the MMU */
	gaudi3_axuser_hbw_mmu_bp_set(hdev, NIC_REG(mmD0_NIC0_TXS_AXUSER_BASE), true);
	gaudi3_axuser_hbw_asid_set(hdev, NIC_REG(mmD0_NIC0_TXS_AXUSER_BASE),
					hdev->kernel_ctx->asid);

	NIC_WREG32(mmD0_NIC0_TXS_FREE_LIST_PUSH_MASK_EN, 1);

	txs_fence_idx = 0;
	txs_pi = 0;
	txs_ci = 0;
	txs_timeout_31_0 = 0;
	timeout_47_32 = 0;
	rl_en_log_time = 0;

	/* Gaudi3 TXS implements 256 schedule-Qs.
	 * These queues are hard-divided to 4x64 priority groups of Qs.
	 *    (The first and last Q group-relative numbers of each group (0-63) can be configured
	 *     via mmD0_NIC0_TXS_FIRST_SCHEDQ_ID and mmD0_NIC0_TXS_LAST_SCHEDQ_ID, We will use its
	 *     default values of 0 and 63 respectively).
	 * From the above pools we need to allocate and configure:
	 * - 16  Qs as evicted Qs (4ports x 4 prio) and
	 * - 1 Q for evicted loopback Q.
	 * configuring the above is done in mmD0_NIC0_TXS_EVICTED_QPS with default value of:
	 * - 0xF Queues for evicted-QPs (we divide the 256 Qs to groups of 16 and we use the
	 *       most significant group (group 0xF) for Evicted-QPs).
	 * - 1 queue (0xEF) as the evicted LPBK Q, (which is just before the evicted-QPs group).
	 *   we will reserve 32 Qs for all the above, this will help us to evenly distribute
	 *       the remaining Qs between the 4 possible ports and 4 possible priorities.
	 *   224 Qs (0-223) are evenly divided between the 4 possible ports so each port is
	 *       assigned with 56 Qs.
	 *   The 56 Qs are divided between the 4 possible priorities generating 14
	 *   priority-granularity groups of which:
	 *   - The Last group is dedicated for Ethernet (RAW_SCHED_Q).
	 *   - The last-1 group is dedicated for the RDMA responder (RES_SCHED_Q)
	 *   - The Last-2 group is dedicated for the RDMA Req (REQ_SCHED_Q)
	 *   - The remaining Qs will be used by the BBR when supported.
	 */
	for (i = 0 ; i < TXS_SCHEDQ ; i++) {
		if (i < TXS_NUM_SCHEDQS)
			/* main sched Qs */
			txs_port = i / TXS_PORT_NUM_SCHEDQS;
		else if (i < TXS_NUM_SCHEDQS + TXS_NUM_EVICTED_LPBK_SCHEDQS)
			/* followed by LPBK Qs */
			txs_port = 3;
		else
			/* Upper 16 Qs are the evicted-Qs formatted as 4x4 (ports x prio-groups) */
			txs_port = (i - TXS_NUM_SCHEDQS - TXS_NUM_EVICTED_LPBK_SCHEDQS) /
					TXS_NUM_PORTS;

		/* TODO: SW-91783 use only 3 priorities */
		prio = i % HL_EN_PFC_PRIO_NUM;
		txs_schedq = (timeout_47_32 & 0xFFFF) | ((prio & 0x3) << 16) |
				((txs_port & 0x3) << 18) |
				((rl_en_log_time & 0x3F) << 20);
		txs_tail = i;
		txs_head = i;
		NIC_WREG32(mmD0_NIC0_TXS_SCHEDQ_UPDATE_DESC_0, txs_fence_idx);
		NIC_WREG32(mmD0_NIC0_TXS_SCHEDQ_UPDATE_DESC_1, txs_pi);
		NIC_WREG32(mmD0_NIC0_TXS_SCHEDQ_UPDATE_DESC_2, txs_ci);
		NIC_WREG32(mmD0_NIC0_TXS_SCHEDQ_UPDATE_DESC_3, txs_tail);
		NIC_WREG32(mmD0_NIC0_TXS_SCHEDQ_UPDATE_DESC_4, txs_head);
		NIC_WREG32(mmD0_NIC0_TXS_SCHEDQ_UPDATE_DESC_5, txs_timeout_31_0);
		NIC_WREG32(mmD0_NIC0_TXS_SCHEDQ_UPDATE_DESC_6, txs_schedq);
		NIC_WREG32(mmD0_NIC0_TXS_SCHEDQ_UPDATE_FIFO, i);
		NIC_WREG32(mmD0_NIC0_TXS_SCHEDQ_UPDATE_EN, 1);
	}

	reg = NIC_RREG32(mmD0_NIC0_TXS_EVICTED_QPS) &
			~(NIC_TXS_EVICTED_QPS_IMMEDIATE_SQ_BASE_ADDR_M |
				NIC_TXS_EVICTED_QPS_EVICTED_LB_SCHEDQ_M);
	reg |= (0xF << NIC_TXS_EVICTED_QPS_IMMEDIATE_SQ_BASE_ADDR_S |
		0xEF << NIC_TXS_EVICTED_QPS_EVICTED_LB_SCHEDQ_S);
	NIC_WREG32(mmD0_NIC0_TXS_EVICTED_QPS, reg);

	NIC_WREG32(mmD0_NIC0_TXS_TICK_WRAP, 100);
	NIC_WREG32(mmD0_NIC0_TXS_SCAN_TIME_COMPARE_0, 4);
	NIC_WREG32(mmD0_NIC0_TXS_SCAN_TIME_COMPARE_1, 0);
	NIC_WREG32(mmD0_NIC0_TXS_TMR_SCAN_EN, !!txs_timeout_31_0);

	NIC_WREG32(mmD0_NIC0_TXS_BASE_ADDRESS_FREE_LIST_63_32,
			upper_32_bits(txs_addr + TXS_FREE_OFFS));

	NIC_WREG32(mmD0_NIC0_TXS_BASE_ADDRESS_FREE_LIST_31_0,
			lower_32_bits(txs_addr + TXS_FREE_OFFS));

	/* mask should never be 0 */
	NIC_WREG32(mmD0_NIC0_TXS_LIST_MASK,
			max(~(0xFFFFFFFF << (ilog2(TXS_FREE_NUM_ENTRIES) - 5)), (u32)1));
	NIC_WREG32(mmD0_NIC0_TXS_PRODUCER_UPDATE, TXS_FREE_NUM_ENTRIES);
	NIC_WREG32(mmD0_NIC0_TXS_PRODUCER_UPDATE_EN, 1);
	NIC_WREG32(mmD0_NIC0_TXS_PRODUCER_UPDATE_EN, 0);
	NIC_WREG32(mmD0_NIC0_TXS_LIST_MEM_READ_MASK, 0);
	NIC_WREG32(mmD0_NIC0_TXS_PUSH_LOCK_EN, 1);

	/* disable burst size optimization */
	NIC_WREG32(mmD0_NIC0_TXS_IGNORE_BURST_EN, 0);

	NIC_WREG32(mmD0_NIC0_TXS_FORCE_HIT_EN, 0);

	/* TXS AXI cache should be set to read mode but not alloc = 0x3 */
	NIC_WREG32(mmD0_NIC0_TXS_AXI_CACHE, (0x3 << NIC_TXS_AXI_CACHE_AR_CACHE_S));
	NIC_WREG32(mmD0_NIC0_TXS_AXI_CACHE, (0x3 << NIC_TXS_AXI_CACHE_AW_CACHE_S));
}

static void gaudi3_nic_config_hw_txe(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	struct hl_nic_properties *nic_prop;
	u32 port;

	nic_prop = &hdev->asic_prop.nic_props;

	port = gaudi3_nic_get_first_port(nic_macro);

	/* Privilege registers configured under nic bringup file */

	NIC_WREG32(mmD0_NIC0_TXE_BTH_MKEY, 0xffff);

	/* TXE WQE fetch should be set to read mode but not alloc = 0x3 */
	NIC_WREG32(mmD0_NIC0_TXE_WQE_FETCH_AXI_CACHE,
						(0x3 << NIC_TXE_WQE_FETCH_AXI_CACHE_VAL_S));

	/* TXE DATA fetch should not skip cache with writeback, allocate controlled by user bit */
	NIC_WREG32(mmD0_NIC0_TXE_DATA_FETCH_AXI_CACHE,
						(0x1 << NIC_TXE_DATA_FETCH_AXI_CACHE_BIT0_S) |
						(0x1 << NIC_TXE_DATA_FETCH_AXI_CACHE_BIT1_S));

	NIC_WREG32(mmD0_NIC0_TXE_STATS_CFG0, nic_prop->clk * USEC_PER_MSEC); /* 1ms window size */
	NIC_RMWREG32(mmD0_NIC0_TXE_STATS_CFG1, 1, NIC_TXE_STATS_CFG1_LATENCY_ENABLE_M);
	NIC_RMWREG32(mmD0_NIC0_TXE_STATS_CFG1, 0, NIC_TXE_STATS_CFG1_WIN_TYPE_M);
	NIC_RMWREG32(mmD0_NIC0_TXE_STATS_CFG1, 0, NIC_TXE_STATS_CFG1_WIN_SAMP_LATENCY_M);
	NIC_RMWREG32(mmD0_NIC0_TXE_STATS_CFG1, 3, NIC_TXE_STATS_CFG1_TOT_TYPE_M);
	NIC_RMWREG32(mmD0_NIC0_TXE_STATS_CFG1, 1, NIC_TXE_STATS_CFG1_ENABLE_M);
	NIC_RMWREG32(mmD0_NIC0_TXE_STATS_CFG2, 0, NIC_TXE_STATS_CFG2_LATENCY_WRAP_EN_M);
	NIC_RMWREG32(mmD0_NIC0_TXE_STATS_CFG2, 2 * nic_prop->clk,
			NIC_TXE_STATS_CFG2_LATENCY_MAX_VAL_M);
}

static void gaudi3_nic_config_port_hw_txe(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port = gaudi3_nic->nic_port->port, offset, wq_size_cline_log, wq_size;
	dma_addr_t wq_addr;

	wq_size = ALIGN(QP_WQE_NUM_REC * NIC_SEND_WQE_SIZE, DEVICE_CACHE_LINE_SIZE);
	/* Config number of WQEs per WQ in SWQ in units of cache line */
	wq_size_cline_log = ilog2(wq_size / NIC_CACHE_LINE_SIZE);

	/* The WQ size we allocated is for a single QP. If the raw QP is 0 then it can work as is.
	 * But if the QP is not 0 then the HW will calculate the offset in order to get the base
	 * address of the relevant WQ. In order for this to work, we subtract the offset that the
	 * HW will add.
	 */
	wq_addr = RING_BUF_DMA_ADDRESS(&gaudi3_nic->wq_ring) - RAW_QPN(port) * wq_size;

	/* Set the base address of the raw WQ. Ethernet uses the first WQ */
	offset = GAUDI3_TXE_WQ_RAW_IDX(port);
	NIC_OFFSET_WREG32(mmD0_NIC0_TXE_SQ_BASE_ADDRESS_63_32_0, upper_32_bits(wq_addr));
	NIC_OFFSET_WREG32(mmD0_NIC0_TXE_SQ_BASE_ADDRESS_31_0_0, lower_32_bits(wq_addr));
	NIC_OFFSET_WREG32(mmD0_NIC0_TXE_LOG_MAX_WQ_SIZE_0, wq_size_cline_log);
	/* set MMU bypass for kernel WQ */
	NIC_OFFSET_WREG32(mmD0_NIC0_TXE_WQE_FETCH_AXI_USER_0,
			(1 << NIC_TXE_WQE_FETCH_AXI_USER_MMU_BYPASS_S) |
			(hdev->kernel_ctx->asid << NIC_TXE_WQE_FETCH_AXI_USER_0_ASID_S));

	offset = GAUDI3_RAW_OFFSET + ELEMENT_OFFSET(port, TXE_DSCP_NUM);
	NIC_OFFSET_WREG32(mmD0_NIC0_TXE_PRIO_TO_DSCP_0, 0x18100800);
}

static void gaudi3_nic_config_hw_qpc(struct hl_nic_macro *nic_macro)
{
	u64 req_qpc_base_addr, res_qpc_base_addr, req_qpc_swl_base_addr;
	struct hl_device *hdev = nic_macro->hdev;
	struct gaudi3_nic_port *gaudi3_nic;
	struct hl_nic_properties *nic_prop;
	struct gaudi3_device *gaudi3;
	u32 port, direct_qpn_offset, patcher_cfg_mask;

	port = gaudi3_nic_get_first_port(nic_macro);
	gaudi3 = hdev->asic_specific;
	gaudi3_nic = &gaudi3->nic_ports[port];
	nic_prop = &hdev->asic_prop.nic_props;

	if (hdev->dram_enable) {
		req_qpc_base_addr = nic_prop->req_qpc_base_addr +
					nic_macro->idx * nic_prop->req_qpc_base_size;
		res_qpc_base_addr = nic_prop->res_qpc_base_addr +
					nic_macro->idx * nic_prop->res_qpc_base_size;
		req_qpc_swl_base_addr = nic_prop->req_qpc_swl_base_addr +
					nic_macro->idx * nic_prop->req_qpc_swl_base_size;
	} else {
		req_qpc_base_addr = gaudi3_nic->req_qpc_mem.dma_addr;
		res_qpc_base_addr = gaudi3_nic->res_qpc_mem.dma_addr;
		req_qpc_swl_base_addr = gaudi3_nic->req_qpc_swl_mem.dma_addr;
	}

	/* Privilege registers configured under nic bringup file */

	NIC_WREG32(mmD0_NIC0_QPC_REQ_BASE_ADDRESS_63_32, upper_32_bits(req_qpc_base_addr));
	NIC_WREG32(mmD0_NIC0_QPC_REQ_BASE_ADDRESS_31_0, lower_32_bits(req_qpc_base_addr));

	NIC_WREG32(mmD0_NIC0_QPC_RES_BASE_ADDRESS_63_32, upper_32_bits(res_qpc_base_addr));
	NIC_WREG32(mmD0_NIC0_QPC_RES_BASE_ADDRESS_31_0, lower_32_bits(res_qpc_base_addr));

	NIC_WREG32(mmD0_NIC0_QPC_SWL_BASE_ADDRESS_63_32, upper_32_bits(req_qpc_swl_base_addr));
	NIC_WREG32(mmD0_NIC0_QPC_SWL_BASE_ADDRESS_31_0, lower_32_bits(req_qpc_swl_base_addr));

	NIC_WREG32(mmD0_NIC0_QPC_MAX_QPN, NIC_MAX_CONN_ID);

	NIC_WREG32(mmD0_NIC0_QPC_RES_QPC_CACHE_INVALIDATE, 1);
	NIC_WREG32(mmD0_NIC0_QPC_REQ_QPC_CACHE_INVALIDATE, 1);
	NIC_WREG32(mmD0_NIC0_QPC_RES_QPC_CACHE_INVALIDATE, 0);
	NIC_WREG32(mmD0_NIC0_QPC_REQ_QPC_CACHE_INVALIDATE, 0);

	NIC_WREG32(mmD0_NIC0_QPC_INTERRUPT_CAUSE, 0);

	/* Configure MMU-BP override for DB-FIFOs */
	gaudi3_axuser_hbw_mmu_bp_set(hdev, NIC_REG(mmD0_NIC0_QPC_AXUSER_HBW_DB_FIFO_BASE), true);

	NIC_WREG32(mmD0_NIC0_QPC_QPC_CLOCK_GATE_DIS, 0x1);
	NIC_WREG32(mmD0_NIC0_QPC_RETRY_COUNT_MAX, 0xFEFE);

	/* Configure MMU-BP override for QPCs */
	gaudi3_axuser_hbw_mmu_bp_set(hdev, NIC_REG(mmD0_NIC0_QPC_AXUSER_HBW_QPC_REQ_BASE), true);
	gaudi3_axuser_hbw_mmu_bp_set(hdev, NIC_REG(mmD0_NIC0_QPC_AXUSER_HBW_QPC_RESP_BASE), true);

	/* Configure MMU-BP override for Congestion-Queue */
	gaudi3_axuser_hbw_mmu_bp_set(hdev, NIC_REG(mmD0_NIC0_QPC_AXUSER_HBW_CONG_QUE_BASE), true);

	/* Configure MMU-BP override for EQs */
	gaudi3_axuser_hbw_mmu_bp_set(hdev, NIC_REG(mmD0_NIC0_QPC_AXUSER_HBW_EV_QUE_BASE), true);

	/* SW-90895: due to HW limitation, RESEND_WQE_ON_ROLLBACK should be disabled */
	NIC_RMWREG32(mmD0_NIC0_QPC_REQ_STATIC_CONFIG, 0,
			D0_NIC0_QPC_REQ_STATIC_CONFIG_RESEND_WQE_ON_ROLLBACK_M);


	/* TODO SW-82722 - enable WQ Back Pressure */
	NIC_WREG32(mmD0_NIC0_QPC_WQ_BP_MSG_EN_0, 0);
	NIC_WREG32(mmD0_NIC0_QPC_WQ_BP_MSG_EN_1, 0);
	NIC_WREG32(mmD0_NIC0_QPC_WQ_BP_MSG_EN_2, 0);
	NIC_WREG32(mmD0_NIC0_QPC_WQ_BP_MSG_EN_3, 0);

	/* The back-pressure thresholds values describe the vacant space left in the
	 * QP at which the back-pressure value will be increased/decreased.
	 * Thresholds are configured by some defined numbers.
	 * (currently 4/8 for the upper/lower thresholds respectively).
	 */
	NIC_WREG32(mmD0_NIC0_QPC_WQ_INC_THRESHOLD, GAUDI3_NIC_WTD_BP_UPPER_TH_DIFF);
	NIC_WREG32(mmD0_NIC0_QPC_WQ_DEC_THRESHOLD, GAUDI3_NIC_WTD_BP_LOWER_TH_DIFF);

	/* HW accelerated congestion control configuration */
	/* H9-5456 - HW bug when congestion control is used with SACK. Fix was done for RTT
	 * measure method = 1 only.
	 */
	NIC_WREG32(mmD0_NIC0_QPC_SWIFT_CFG,
			1 << D0_NIC0_QPC_SWIFT_CFG_SWIFT_EN_S |
			1 << D0_NIC0_QPC_SWIFT_CFG_RTT_MEASURE_METHOD_S |
			4 << D0_NIC0_QPC_SWIFT_CFG_COALESCE_INIT_VAL_S |
			1 << D0_NIC0_QPC_SWIFT_CFG_ENABLE_COALESCE_S);
	NIC_WREG32(mmD0_NIC0_QPC_CC_ROLLBACK,
			1 << D0_NIC0_QPC_CC_ROLLBACK_HW_EN_S |
			1 << D0_NIC0_QPC_CC_ROLLBACK_SW_EN_S |
			1 << D0_NIC0_QPC_CC_ROLLBACK_TRIGGER_HW_EN_S);
	NIC_WREG32(mmD0_NIC0_QPC_CC_MIN_WINDOW_SIZE, GAUDI3_CC_MIN_WINDOW_SIZE);
	NIC_WREG32(mmD0_NIC0_QPC_CC_MAX_WINDOW_SIZE, GAUDI3_CC_MAX_WINDOW_SIZE);

	NIC_WREG32(mmD0_NIC0_QPC_NIC_ID, nic_macro->idx);

	patcher_cfg_mask = D0_NIC0_QPC_PATCHER_CFG_LAG_SIZE_M |
					D0_NIC0_QPC_PATCHER_CFG_RX_WQE_CT_MASK_M;

	NIC_RMWREG32_SHIFTED(mmD0_NIC0_QPC_PATCHER_CFG,
		((gaudi3->coll_lag_size << D0_NIC0_QPC_PATCHER_CFG_LAG_SIZE_S) |
		(QPC_RX_WQE_CT_MASK_DEFAULT << D0_NIC0_QPC_PATCHER_CFG_RX_WQE_CT_MASK_S)),
		patcher_cfg_mask);

	direct_qpn_offset = is_200g_mode(hdev) ? (NIC_MAX_COLL_QP_NUM / 2) : 0;
	NIC_RMWREG32(mmD0_NIC0_QPC_PATCHER_CFG, direct_qpn_offset,
			D0_NIC0_QPC_PATCHER_CFG_DIRECT_QPN_OFFSET_M);

	/* AXCACHE register configuration */
	/* QPC requester cache configuration - should be set to read mode but not alloc = 0x3 */
	NIC_WREG32(mmD0_NIC0_QPC_QPC_REQUESTER_AR_ATTR,
				(0x3 << D0_NIC0_QPC_QPC_REQUESTER_AR_ATTR_CACHE_S));
	NIC_WREG32(mmD0_NIC0_QPC_QPC_REQUESTER_WORK_AW_ATTR,
				(0x3 << D0_NIC0_QPC_QPC_REQUESTER_WORK_AW_ATTR_CACHE_S));
	NIC_WREG32(mmD0_NIC0_QPC_QPC_REQUESTER_NO_WORK_AW_ATTR,
				(0x3 << D0_NIC0_QPC_QPC_REQUESTER_NO_WORK_AW_ATTR_CACHE_S));
	/* QPC responder cache configuration - should be set to read mode but not alloc = 0x3 */
	NIC_WREG32(mmD0_NIC0_QPC_QPC_RESPONDER_AR_ATTR,
				(0x3 << D0_NIC0_QPC_QPC_RESPONDER_AR_ATTR_CACHE_S));
	NIC_WREG32(mmD0_NIC0_QPC_QPC_RESPONDER_AW_ATTR,
				(0x3 << D0_NIC0_QPC_QPC_RESPONDER_AW_ATTR_CACHE_S));
	/* WTD configuration - should be set to read mode but not alloc = 0x3 */
	NIC_WREG32(mmD0_NIC0_QPC_WTD_AWCACHE, (0x3 << D0_NIC0_QPC_WTD_AWCACHE_CACHE_S));
	/* DB FIFO configuration - should be set to read mode but not alloc = 0x3 */
	NIC_WREG32(mmD0_NIC0_QPC_DB_FIFO_AWCACHE, (0x3 << D0_NIC0_QPC_DB_FIFO_AWCACHE_CACHE_S));
	/* EQ configuration - should be set to read mode but not alloc = 0x3 */
	NIC_WREG32(mmD0_NIC0_QPC_EQ_AWCACHE, (0x3 << D0_NIC0_QPC_EQ_AWCACHE_CACHE_S));
	/* CongQ configuration - should be set to read mode but not alloc = 0x3 */
	NIC_WREG32(mmD0_NIC0_QPC_CONGQ_AWCACHE, (0x3 << D0_NIC0_QPC_CONGQ_AWCACHE_CACHE_S));

	/* H9-5457 - causing the TXE to stop sending packets at 1023 OTF rather then 1024, i.e.
	 * avoid a case where the windows is full and the HW bug may occur.
	 */
	NIC_WREG32(mmD0_NIC0_QPC_MAX_OTF_PSN_SACK, 1023);

#ifdef SW_68008
	/* TODO: [SW-68008] */
	NIC_WREG32(mmNIC0_QPC0_AXUSER_EV_QUE_LBW_INTR_HB_WR_OVRD_LO, 0xFFFFFBFF);
	NIC_WREG32(mmNIC0_QPC0_AXUSER_EV_QUE_LBW_INTR_HB_RD_OVRD_LO, 0xFFFFFBFF);
#endif
}

static void gaudi3_nic_config_port_hw_qpc(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_port *nic_port = gaudi3_nic->nic_port;
	u32 size, offset, fifo_offset;
	u32 port = nic_port->port;
	struct hl_device *hdev;

	hdev = gaudi3_nic->hdev;

	/* Configure doorbell */
	/* TODO: SW-67924 */
	offset = GAUDI3_DB_FIFO_RAW_IDX(port);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_UPD_ADDR_MSB_0,
			upper_32_bits(RING_BUF_DMA_ADDRESS(&gaudi3_nic->fifo_ring)));
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_UPD_ADDR_LSB_0,
			lower_32_bits(RING_BUF_DMA_ADDRESS(&gaudi3_nic->fifo_ring)));

	gaudi3_nic_eq_dispatcher_register_db(gaudi3_nic, HL_KERNEL_ASID_ID,
						GAUDI3_DB_FIFO_SECURE_HW_ID(port));

	/* Set the protection level as following:
	 * Qman (bits 0-1): 0x0
	 * Unsecured (bits 2-3): 0x0
	 * Secured (bits 4-5): 0x1
	 * Privilege (bits 6-7): 0x2
	 *
	 * Note that the encoding here is NOT the AXI-prot encoding, but a
	 * sequential number that is compared to the QPC trust level.
	 */
	/* TODO: SW-67924 */
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_SECURITY_0,
			1 << D0_NIC0_QPC_DB_FIFO_SECURITY_SECURITY_LEVEL_S);

	/* TODO: SW-67924: fix also the EQ ID */
	/* TODO: SW-68378: verify DB_TYPE cfg */
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_CFG_0,
			HL_KERNEL_ASID_ID << D0_NIC0_QPC_DB_FIFO_CFG_0_ASID_S |
			1 << D0_NIC0_QPC_DB_FIFO_CFG_MMU_BP_S |
			0 << D0_NIC0_QPC_DB_FIFO_CFG_DB_TYPE_S |
			0 << D0_NIC0_QPC_DB_FIFO_CFG_EQ_ID_S |
			0 << D0_NIC0_QPC_DB_FIFO_CFG_DB_SOURCE_S |
			1 << D0_NIC0_QPC_DB_FIFO_CFG_UPDATE_MSG_TYPE_S);

	/* TODO: SW-68379: check if to add 1 << D0_NIC0_QPC_DB_FIFO_CFG2_CLR_S */

	fifo_offset = gaudi3_nic->raw_fifo_offset / sizeof(u32);
	size = ilog2(DB_FIFO_ETH_SIZE / sizeof(u32));
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_CFG2_0,
			fifo_offset << D0_NIC0_QPC_DB_FIFO_CFG2_FIFO_OFFSET_S |
			size << D0_NIC0_QPC_DB_FIFO_CFG2_FIFO_L2_SIZE_S);
}

static void gaudi3_nic_config_hw_rxe(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	struct asic_fixed_properties *props;
	struct hl_nic_properties *nic_prop;
	u32 port;
	int i;

	port = gaudi3_nic_get_first_port(nic_macro);
	props = &hdev->asic_prop;
	nic_prop = &props->nic_props;

	/* Privilege registers configured under nic bringup file */

	/* TODO: HB_OVRD_LO zero 11 LSBs */
	NIC_WREG32(mmD0_NIC0_RXE_LBW_BASE_LO, lower_32_bits(CFG_BAR_BASE));
	NIC_WREG32(mmD0_NIC0_RXE_LBW_BASE_HI, upper_32_bits(CFG_BAR_BASE));

	/* Make sure LBW write access (for SM) can never be privileged */
	NIC_WREG32(mmD0_NIC0_RXE_AWPROT_LBW_PRIV, 0);

	/* Initialize MMU-BP for all CQs */
	for (i = 0 ; i < nic_prop->max_cqs ; i++)
		NIC_WREG32(mmD0_NIC0_RXE_CQ_AXI_USER_0 + (i * RXE_CQ_AXI_USER_OFFSET),
				D0_NIC0_RXE_CQ_AXI_USER_MMU_BP_M);

	for (i = 0 ; i < props->num_of_hdcores; i++) {
		NIC_WREG32(mmD0_NIC0_RXE_SOB_SM_BASE_ADDR_0 + i * sizeof(u32),
				(CFG_BAR_BASE - LBW_BASE + mmHD0_SYNC_MNGR_OBJS_BASE +
						mmSOB_OBJS_SOB_OBJ_0_0) + i * HDCORE_OFFSET);
	}

	NIC_WREG32(mmD0_NIC0_RXE_SOB_SUB_SM_OFFSET,
		(mmSOB_OBJS_SOB_OBJ_1_0 - mmSOB_OBJS_SOB_OBJ_0_0));

	/* RXE WQE cache should be set to read mode but not alloc = 0x3 */
	NIC_WREG32(mmD0_NIC0_RXE_WQE_ARCACHE, (0x3 << D0_NIC0_RXE_WQE_ARCACHE_VAL_S));

	/* RXE AF&A cache should not skip cache with writeback, allocate controlled by user bit */
	NIC_WREG32(mmD0_NIC0_RXE_AFA_CACHE_ATTR,
					(0x1 << D0_NIC0_RXE_AFA_CACHE_ATTR_ARCACHE_BIT0_S) |
					(0x1 << D0_NIC0_RXE_AFA_CACHE_ATTR_ARCACHE_BIT1_S));

	/* RXE CQ cache should be set to read mode but not alloc = 0x3 */
	NIC_WREG32(mmD0_NIC0_RXE_CQ_AWCACHE, (0x3 << D0_NIC0_RXE_CQ_AWCACHE_VAL_S));

	/*
	 * TODO SW-84547: new reg (mmD0_NIC0_RXE_WQE_ARCACHE )for WQ cache attributes
	 * how to configure it?
	 */
}

static void gaudi3_nic_config_port_hw_rxe(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_ring *cq_ring = &gaudi3_nic->cq_rings[GAUDI3_RAW_OFFSET];
	struct hl_nic_ring *rx_ring = &gaudi3_nic->rx_ring;
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port, rx_mem_addr_lo, rx_mem_addr_hi, offset;

	port = gaudi3_nic->port;
	rx_mem_addr_lo = lower_32_bits(RING_BUF_DMA_ADDRESS(rx_ring));
	rx_mem_addr_hi = upper_32_bits(RING_BUF_DMA_ADDRESS(rx_ring));

	offset = GAUDI3_RAW_OFFSET + ELEMENT_OFFSET(port, RXE_RAW_QPN_NUM);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RAW_QPN_P0_0, RAW_QPN(port));
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RAW_QPN_P0_1, RAW_QPN(port));

	offset = GAUDI3_RAW_OFFSET + ELEMENT_OFFSET(port, RXE_RAW_BASE_NUM);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RAW_BASE_LO_P0_0, rx_mem_addr_lo);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RAW_BASE_LO_P0_1, rx_mem_addr_lo);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RAW_BASE_HI_P0_0, rx_mem_addr_hi);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RAW_BASE_HI_P0_1, rx_mem_addr_hi);

	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RAW_MISC_P0_0,
			(ilog2(rx_ring->elem_size) &
				D0_NIC0_RXE_RAW_MISC_P2_LOG_RAW_ENTRY_SIZE_P2_M) |
			((ilog2(rx_ring->count) & 0x1F) << 15));

	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RAW_MISC_P0_1,
			(ilog2(rx_ring->elem_size) &
				D0_NIC0_RXE_RAW_MISC_P2_LOG_RAW_ENTRY_SIZE_P2_M) |
			((ilog2(rx_ring->count) & 0x1F) << 15));

	/* TODO: SW-67924 */
	offset = GAUDI3_RAW_OFFSET + ELEMENT_OFFSET(port, RXE_CQ_NUM);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_BASE_ADDR_HI_0,
			upper_32_bits(RING_BUF_DMA_ADDRESS(cq_ring)));
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_BASE_ADDR_LO_0,
			lower_32_bits(RING_BUF_DMA_ADDRESS(cq_ring)));

	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_PI_ADDR_HI_0,
			upper_32_bits(RING_PI_DMA_ADDRESS(cq_ring)));
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_PI_ADDR_LO_0,
			lower_32_bits(RING_PI_DMA_ADDRESS(cq_ring)));

	/* Set the actual single CQ size log2(number of entries in cq)*/
	/* TODO: SW-67924 */
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_LOG_SIZE_0, ilog2(cq_ring->count));

	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_WRITE_INDEX_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_PRODUCER_INDEX_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_CONSUMER_INDEX_0, 0);

	/* enable, pi-update and completion-events */
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_CFG_0,
			1 << D0_NIC0_RXE_CQ_CFG_WRITE_PI_EN_S |
			1 << D0_NIC0_RXE_CQ_CFG_ENABLE_S |
			GAUDI3_EQ_RAW_IDX(port) << D0_NIC0_RXE_CQ_CFG_EQ_ID_S);

	/* set MMU bypass for kernel WQ */
	offset = GAUDI3_RAW_OFFSET + ELEMENT_OFFSET(port, RXE_WQ_NUM);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_WQE_ARUSER_ATTR_0,
			HL_KERNEL_ASID_ID << D0_NIC0_RXE_WQE_ARUSER_ATTR_0_ASID_S |
			1 << D0_NIC0_RXE_WQE_ARUSER_ATTR_MMU_BP_S);
}

static void gaudi3_nic_config_port_hw_mac_filter(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_port *nic_port = gaudi3_nic->nic_port;
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port = nic_port->port, offset;
	u64 mac_addr = 0;
	int i;

	for (i = 0 ; i < ETH_ALEN ; i++) {
		mac_addr <<= BITS_PER_BYTE;
		mac_addr |= hdev->asic_prop.cpucp_nic_info.mac_addrs[port].mac_addr[i];
	}

	offset = GAUDI3_RAW_OFFSET + ELEMENT_OFFSET(port, RXB_RAW_NUM);

	/* TODO: SW-67924 check that we do not use 4x100G mode */
	/* RAW packet MAC filter */
	NIC_OFFSET_WREG32(mmD0_NIC0_RXB_CORE_PRT_TS_RAW0_MAC_31_0_MASK_0, 0xFFFFFFFF);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXB_CORE_PRT_TS_RAW0_MAC_47_32_MASK_0, 0xFFFF);

	/*
	 * HW MAC filtering PASS logic:
	 * rcv_pkt_mac & ~rxb_core_mac_mask == rxb_core_mac & ~rxb_core_mac_mask
	 */
	if (nic_port->eth_enable) {
		/* RoCE packet MAC filter. */
		NIC_OFFSET_WREG32(mmD0_NIC0_RXB_CORE_PRT_TS_RC_MAC_31_0_0,
					mac_addr & 0xFFFFFFFF);
		NIC_OFFSET_WREG32(mmD0_NIC0_RXB_CORE_PRT_TS_RC_MAC_47_32_0,
					(mac_addr >> 32) & 0xFFFF);
		NIC_OFFSET_WREG32(mmD0_NIC0_RXB_CORE_PRT_TS_RC_MAC_31_0_MASK_0, 0);
		NIC_OFFSET_WREG32(mmD0_NIC0_RXB_CORE_PRT_TS_RC_MAC_47_32_MASK_0, 0);
	} else {
		/* Internal port MAC filter.
		 * - RAW is not supported. Hence, skip RAW MAC filter config.
		 * - Accept all RoCE packets since internal ports are connected back-to-back.
		 */
		NIC_OFFSET_WREG32(mmD0_NIC0_RXB_CORE_PRT_TS_RC_MAC_31_0_MASK_0, 0xFFFFFFFF);
		NIC_OFFSET_WREG32(mmD0_NIC0_RXB_CORE_PRT_TS_RC_MAC_47_32_MASK_0, 0xFFFF);
	}

	NIC_OFFSET_WREG32(mmD0_NIC0_TXE_PORT_MAC_CFG_31_0_0, mac_addr & 0xFFFFFFFF);
	NIC_OFFSET_WREG32(mmD0_NIC0_TXE_PORT_MAC_CFG_47_32_0, (mac_addr >> 32) & 0xFFFF);
}

static void gaudi3_nic_hw_mac_ch_reset(struct gaudi3_nic_port *gaudi3_nic, int lane)
{
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port = gaudi3_nic->nic_port->port;
	ktime_t timeout;
	u32 read_reg;

	if (hdev->nic.skip_mac_reset)
		return;

	timeout = ktime_add_ms(ktime_get(), HL_PENDING_RESET_LONG_SEC * 1000);

	do {
		NIC_MAC_WREG32(mmNIC_MAC_CH_MAC_CH0_COMMAND_CONFIG, lane,
				BIT(D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_SW_RESET_S));
		usleep_range(50, 200);

		read_reg = NIC_MAC_RREG32(mmNIC_MAC_CH_MAC_CH0_COMMAND_CONFIG, lane);
	} while ((read_reg & D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_SW_RESET_M) &&
		ktime_compare(ktime_get(), timeout) < 0);

	if (read_reg & D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_SW_RESET_M)
		dev_err(hdev->dev, "Timeout while MAC channel %d reset\n", lane);
}

static void gaudi3_nic_config_port_hw_mac(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_port *nic_port = gaudi3_nic->nic_port;
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	int lane, lane_offset;

	lane = get_lane_offset(gaudi3_nic);

	gaudi3_nic_hw_mac_ch_reset(gaudi3_nic, lane);

	/* TODO: SW-70999 */
	NIC_MAC_WREG32(mmNIC_MAC_CH_MAC_CH0_FRM_LENGTH, lane, 8400);
	NIC_MAC_WREG32(mmNIC_MAC_CH_MAC_CH0_COMMAND_CONFIG, lane,
			D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_TX_ENA_M |
			D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_RX_ENA_M |
			D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_PROMIS_EN_M |
			D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_PAUSE_IGNORE_M |
			D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_TX_PAD_EN_M |
			D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_CMD_FRAME_ENA_M);
	NIC_MAC_WREG32(mmNIC_MAC_CH_MAC_CH0_RX_FIFO_SECTIONS, lane, 0x3);
	NIC_MAC_WREG32(mmNIC_MAC_CH_MAC_CH0_TX_IPG_LENGTH, lane, 0xA00);

	for (lane_offset = 0 ; lane_offset < hdev->nic_lanes_per_port ; lane_offset++) {
		NIC_RMWREG32(mmD0_NIC0_MAC_CORE_BASE + mmPRT_MAC_CORE_MAC_EQ_REGISTRATION,
			GAUDI3_EQ_RAW_IDX(port),
			D0_NIC0_MAC_CORE_MAC_EQ_REGISTRATION_LANE0_TO_EQ_ID_M <<
			((lane + lane_offset) * 2));
	}
}

static void gaudi3_nic_config_port_hw(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_port *nic_port = gaudi3_nic->nic_port;

	/* TXE Configuration */
	gaudi3_nic_config_port_hw_txe(gaudi3_nic);

	/* QPC Configuration */
	gaudi3_nic_config_port_hw_qpc(gaudi3_nic);

	/* RXE Configuration */
	gaudi3_nic_config_port_hw_rxe(gaudi3_nic);

	/* MAC filtering */
	gaudi3_nic_config_port_hw_mac_filter(gaudi3_nic);

	/* Lanes Configuration */
	gaudi3_nic_config_port_hw_mac(gaudi3_nic);

	/* PFC Configuration */
	gaudi3_nic_set_pfc(nic_port);
}

static void gaudi3_nic_hw_mac_loopback_cfg(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_device *hdev;
	struct hl_nic_port *nic_port;
	u32 port, val;
	int lane;

	hdev = gaudi3_nic->hdev;
	nic_port = gaudi3_nic->nic_port;
	port = nic_port->port;
	val = !!nic_port->mac_loopback;

	lane = get_lane_offset(gaudi3_nic);

	if (hdev->pdev) {
		if (is_400g_mode(hdev))
			NIC_RMWREG32(mmD0_NIC0_MAC_PCS_PCS400_BASE + mmMAC_PCS_PCS400_CONTROL1,
					val, D0_NIC0_MAC_PCS_PCS400_CONTROL1_LOOPBACK_M);
		else {
			if (lane == 0)
				/* The first 200g port (has lane 0) is configured through PCS400 */
				NIC_RMWREG32(mmD0_NIC0_MAC_PCS_PCS400_BASE +
							mmMAC_PCS_PCS400_CONTROL1,
						val, D0_NIC0_MAC_PCS_PCS400_CONTROL1_LOOPBACK_M);
			else
				/* The second port is configured via PCS200 */
				NIC_RMWREG32(mmD0_NIC0_MAC_PCS_PCS200_BASE +
						mmMAC_PCS_PCS200_CONTROL1,
						val, D0_NIC0_MAC_PCS_PCS200_CONTROL1_LOOPBACK_M);
		}
	} else {
		NIC_MAC_RMWREG32(mmNIC_MAC_CH_MAC_CH0_COMMAND_CONFIG, lane, val,
					D0_NIC0_MAC_CH_MAC_CH0_COMMAND_CONFIG_LOOPBACK_EN_M);
	}
}

static void gaudi3_nic_hw_config(struct gaudi3_nic_port *gaudi3_nic)
{
	gaudi3_nic_config_port_hw(gaudi3_nic);
	gaudi3_nic_hw_mac_loopback_cfg(gaudi3_nic);
}

/* TODO: unify to hl_nic_eq_handler_func: SW-82998 */
static void gaudi3_nic_eq_handler_func(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_port *nic_port = gaudi3_nic->nic_port;
	struct gaudi3_device *gaudi3;
	struct hl_aux_dev *aux_dev;
	struct hl_device *hdev;
	struct hl_nic_eqe eqe;
	u32 port;


	hdev = gaudi3_nic->hdev;
	aux_dev = &hdev->nic.en_aux_dev;
	gaudi3 = hdev->asic_specific;
	port = nic_port->port;

	mutex_lock(&nic_port->control_lock);

	if (!hl_nic_is_port_open(nic_port)) {
		dev_dbg(hdev->dev, "ignoring events while port %d closed", port);
		goto out;
	}

	while (!hl_nic_eq_dispatcher_dequeue(nic_port, HL_KERNEL_ASID_ID, &eqe, false)) {
		if (!EQE_IS_VALID(&eqe)) {
			dev_warn_ratelimited(hdev->dev, "Port-%d got invalid EQE on EQ!\n", port);
			continue;
		}

		gaudi3->en_aux_ops.handle_eqe(aux_dev, port, &eqe);
	}

out:
	mutex_unlock(&nic_port->control_lock);
}

static int gaudi3_nic_port_hw_init(struct hl_nic_port *nic_port)
{
	struct gaudi3_nic_macro *gaudi3_macro = nic_port->nic_macro->asic_priv;
	struct gaudi3_nic_port *gaudi3_nic = nic_port->nic_specific;
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port = nic_port->port;
	u64 gen_pool_offset;
	int rc;

	gaudi3_nic_reset_rings(gaudi3_nic);

	/* TODO: SW-65623 register the Eth CQ and eq_handler with the event dispatcher */
	/* register the Eth CQ with the event dispatcher */
	rc = hl_nic_eq_dispatcher_register_cq(nic_port,
						gaudi3_nic->cq_rings[GAUDI3_RAW_OFFSET].asid,
						GAUDI3_CQ_RAW_IDX(port));
	if (rc) {
		dev_err(hdev->dev, "failed to register port %d CQ %d with nic_eq_sw\n", port,
			GAUDI3_CQ_RAW_IDX(port));
		goto cq_register_fail;
	}

	gaudi3_nic_hw_config(gaudi3_nic);

	gaudi3_nic_eq_handler_register(gaudi3_nic, gaudi3_nic_eq_handler_func);

	/* Allocate DB FIFO for the Eth QP */
	gen_pool_offset = gen_pool_alloc(gaudi3_macro->db_fifo_pool, DB_FIFO_ETH_SIZE);
	if (!gen_pool_offset) {
		dev_err(hdev->dev, "Failed to allocate Raw DB FIFO, port: %d\n", port);
		rc = -ENOMEM;
		goto cq_init_fail;
	}

	gaudi3_nic->raw_db_pool_offset = gen_pool_offset;
	gaudi3_nic->raw_fifo_offset = gen_pool_offset - gaudi3_macro->db_fifo_start_addr;
	nic_port->qp_idx_offset = ELEMENT_OFFSET(port, NIC_MAX_GEN_QP_NUM);
	nic_port->coll_qp_idx_offset =
			GAUDI3_MIN_COLL_QP_ID + ELEMENT_OFFSET(port, NIC_MAX_COLL_QP_NUM);
	nic_port->scale_out_coll_qp_idx_offset = nic_port->coll_qp_idx_offset +
							NIC_MAX_NON_SCALE_OUT_COLL_CONNS;

	return 0;

cq_init_fail:
	gaudi3_nic_eq_handler_unregister(gaudi3_nic);
	hl_nic_eq_dispatcher_unregister_cq(nic_port, GAUDI3_CQ_RAW_IDX(port));
cq_register_fail:
	return rc;
}

static void gaudi3_nic_port_hw_fini(struct hl_nic_port *nic_port)
{
	struct gaudi3_nic_port *gaudi3_nic = nic_port->nic_specific;
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 port = nic_port->port;

	/* Release the fifo memory held by Ethernet */
	gaudi3_db_fifo_free(nic_port, gaudi3_nic->raw_db_pool_offset, DB_FIFO_ETH_SIZE);
	gaudi3_nic->raw_db_pool_offset = 0;
	gaudi3_nic->raw_fifo_offset = 0;

	/* TODO: SW-65623 unregister the Eth CQ and eq_handler with the event dispatcher */
	gaudi3_nic_eq_handler_unregister(gaudi3_nic);

	hl_nic_eq_dispatcher_unregister_cq(nic_port, GAUDI3_CQ_RAW_IDX(port));
	hl_nic_eq_dispatcher_unregister_db(nic_port, GAUDI3_DB_FIFO_SECURE_HW_ID(port));

	hl_nic_eq_dispatcher_reset(nic_port);

#if HL_NIC_DEBUG
	dev_dbg(hdev->dev, "link down, port %d\n", port);
#endif
}

/* must be called under mutex_lock(&gaudi3_macro->cfg_lock) */
static int gaudi3_nic_qpc_op(struct hl_nic_port *nic_port, u64 ctrl, bool wait_for_completion)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_properties *nic_props = &hdev->asic_prop.nic_props;
	u32 status, port = nic_port->port;
	int rc = 0;

	NIC_WREG32(mmD0_NIC0_QPC_GW_CTRL, ctrl);

	NIC_WREG32(mmD0_NIC0_QPC_GW_BUSY, 1);

	if (wait_for_completion)
		rc = hl_poll_timeout(
			hdev,
			mmD0_NIC0_QPC_GW_BUSY + NIC_CFG_BASE(port),
			status,
			!status,
			1000,
			nic_props->nic_qpc_cache_inv_timeout);

	return rc;
}

static int gaudi3_nic_qpc_write_masked(struct hl_nic_port *nic_port, const void *qpc_data,
						const struct qpc_mask *qpc_mask, u32 qpn,
						bool is_req, bool force_doorbell)
{
	struct hl_device *hdev;
	const u32 *mask, *data;
	u32 port, data_size;
	int i, rc = 0;
	u64 ctrl;

	hdev = nic_port->hdev;
	port = nic_port->port;
	data_size = is_req ? sizeof(struct gaudi3_qpc_requester) :
				sizeof(struct gaudi3_qpc_responder);

	mask = (const u32 *) qpc_mask;
	data = qpc_data;

	/* Don't write to the Gw if its busy with prev operation */
	if (NIC_RREG32(mmD0_NIC0_QPC_GW_BUSY)) {
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
		NIC_WREG32(mmD0_NIC0_QPC_GW_MASK_0 + i * sizeof(u32), mask[i]);

	for (i = 0 ; i < (data_size / sizeof(u32)) ; i++)
		NIC_WREG32(mmD0_NIC0_QPC_GW_DATA_0 + i * sizeof(u32), data[i]);

	ctrl = (is_req << D0_NIC0_QPC_GW_CTRL_REQUESTER_S) | qpn |
			(!!force_doorbell << D0_NIC0_QPC_GW_CTRL_DOORBELL_FORCE_S);

	rc = gaudi3_nic_qpc_op(nic_port, ctrl, true);
	if (rc && hl_device_operational(hdev, NULL))
		/* Device might not respond during reset if the reset was due to error */
		dev_err(hdev->dev, "%s QPC GW write timeout, port: %d, qpn: %u\n",
				is_req ? "requester" : "responder", port, qpn);

	return rc;
}

static int gaudi3_nic_qpc_write(struct hl_nic_port *nic_port, void *qpc, struct qpc_mask *qpc_mask,
					u32 qpn, bool is_req)
{
	struct qpc_mask mask = {};
	u32 data_size = is_req ? sizeof(struct gaudi3_qpc_requester) :
					sizeof(struct gaudi3_qpc_responder);

	if (!qpc_mask) {
		/* NULL mask flags full QPC write */
		memset(&mask, 0xFF, data_size);
		qpc_mask = &mask;
	}

	return gaudi3_nic_qpc_write_masked(nic_port, qpc, qpc_mask, qpn, is_req, false);
}

static int gaudi3_nic_qpc_invalidate(struct hl_nic_port *nic_port, u32 qpn, bool is_req)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	struct qpc_mask mask = {};
	union rr_qpc _qpc = {};
	int rc = 0;
	void *qpc;

	if (is_req) {
		/* use Congestion window mode with RTT state disabled &
		 * window size 0 to force REQ Tx stop, while Rx remains
		 * active.
		 */
		REQ_QPC_SET_CONGESTION_MODE(mask, 3);
		REQ_QPC_SET_RTT_STATE(mask, 3);
		REQ_QPC_SET_CONGESTION_WIN(mask, GENMASK(23, 0));

		REQ_QPC_SET_CONGESTION_MODE(_qpc.req, 2);
		REQ_QPC_SET_RTT_STATE(_qpc.req, 0);
		REQ_QPC_SET_CONGESTION_WIN(_qpc.req, 0);
		qpc = &_qpc.req;
	} else {
		RES_QPC_SET_VALID(mask, 1);
		RES_QPC_SET_VALID(_qpc.res, 0);
		qpc = &_qpc.res;
	}

	rc = gaudi3_nic_qpc_write_masked(nic_port, qpc, &mask, qpn, is_req, false);

	if (is_req) {
		/* Invalidate RXE WQE cache */
		NIC_RMWREG32(mmD0_NIC0_RXE_CACHE_CFG, 1, D0_NIC0_RXE_CACHE_CFG_INVALIDATION_M);
		NIC_RREG32(mmD0_NIC0_RXE_CACHE_CFG);

		NIC_RMWREG32(mmD0_NIC0_RXE_CACHE_CFG, 0, D0_NIC0_RXE_CACHE_CFG_INVALIDATION_M);
		NIC_RREG32(mmD0_NIC0_RXE_CACHE_CFG);
	}

	return rc;
}

static int gaudi3_nic_qpc_clear(struct hl_nic_port *nic_port, u32 qpn, bool is_req)
{
	struct qpc_mask mask = {};
	union rr_qpc _qpc = {};
	void *qpc = is_req ? (void *) &_qpc.req : (void *) &_qpc.res;
	u32 data_size = is_req ? sizeof(_qpc.req) : sizeof(_qpc.res);

	memset(&mask, 0xFF, data_size);
	return gaudi3_nic_qpc_write_masked(nic_port, qpc, &mask, qpn, is_req, false);
}

int gaudi3_nic_qpc_read(struct hl_nic_port *nic_port, void *qpc, u32 qpn, bool is_req)
{
	struct hl_device *hdev = nic_port->hdev;
	bool force_doorbell = false;
	u32 *data, port, data_size;
	int i, rc;
	u64 ctrl;

	port = nic_port->port;
	data = qpc;
	data_size = is_req ? sizeof(struct gaudi3_qpc_requester) :
				sizeof(struct gaudi3_qpc_responder);

	/* Don't write to the Gw if its busy with prev operation */
	if (NIC_RREG32(mmD0_NIC0_QPC_GW_BUSY)) {
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
		NIC_WREG32(mmD0_NIC0_QPC_GW_MASK_0 + i * sizeof(u32), 0);

	ctrl = (is_req << D0_NIC0_QPC_GW_CTRL_REQUESTER_S) | qpn |
			(!!force_doorbell << D0_NIC0_QPC_GW_CTRL_DOORBELL_FORCE_S);
	rc = gaudi3_nic_qpc_op(nic_port, ctrl, true);
	if (rc)
		return rc;

	for (i = 0 ; i < data_size / sizeof(u32) ; i++)
		data[i] = NIC_RREG32(mmD0_NIC0_QPC_GW_DATA_0 + i * sizeof(u32));

	return 0;
}

static int gaudi3_nic_qpc_query(struct hl_nic_port *nic_port, u32 qpn, bool is_req,
				struct hl_nic_qpc_attr *attr)
{
	struct hl_device *hdev = nic_port->hdev;
	struct gaudi3_qpc_requester req_qpc;
	struct gaudi3_qpc_responder res_qpc;
	u32 port = nic_port->port;
	int rc;

	if (is_req) {
		rc = gaudi3_nic_qpc_read(nic_port, (void *) &req_qpc, qpn, is_req);
		if (rc)
			goto out_err;

		attr->valid = REQ_QPC_GET_VALID(req_qpc);
		attr->in_work = REQ_QPC_GET_IN_WORK(req_qpc);
		attr->error = REQ_QPC_GET_ERROR(req_qpc);
	} else {
		rc = gaudi3_nic_qpc_read(nic_port, (void *) &res_qpc, qpn, is_req);
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

static int gaudi3_nic_pre_core_init(struct hl_device *hdev)
{
	struct cpucp_nic_info *nic_info = &hdev->asic_prop.cpucp_nic_info;
	struct hl_nic_properties *nic_prop = &hdev->asic_prop.nic_props;
	struct cpucp_mac_addr *mac_arr = nic_info->mac_addrs;
	struct cpucp_info *cpucp_info = &hdev->asic_prop.cpucp_info;
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct hl_nic *nic = &hdev->nic;
	u8 mac[ETH_ALEN], *mac_addr;
	u64 nic_dram_alloc_size;
	u32 card_location, serdes_type = MAX_NUM_SERDES_TYPE;
	int i;

	nic_dram_alloc_size = nic_prop->nic_drv_end_addr -
				nic_prop->nic_drv_base_addr;

	if (nic_dram_alloc_size > nic_prop->nic_drv_size) {
		dev_err(hdev->dev, "DRAM allocation for NIC (%lluMB) shouldn't exceed %lluMB\n",
			div_u64(nic_dram_alloc_size, SZ_1M),
			div_u64(nic_prop->nic_drv_size, SZ_1M));
		return -ENOMEM;
	}

	/* copy the MAC OUI in reverse */
	for (i = 0 ; i < 3 ; i++)
		mac[i] = HABANALABS_MAC_OUI_1 >> (8 * (2 - i));

	if ((gaudi3->hw_cap_initialized & HW_CAP_CPU_Q) && !hdev->ignore_fw_nic_info) {
		serdes_type = le16_to_cpu(nic_info->serdes_type);

		for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
			if (!(hdev->nic_ports_mask & BIT(i)))
				continue;

			mac_addr = mac_arr[i].mac_addr;
			if (strncmp(mac, mac_addr, 3)) {
				dev_err(hdev->dev, "bad MAC OUI %pM, port %d\n", mac_addr, i);
				return -EFAULT;
			}
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

		if (!hdev->asic_prop.fw_security_enabled) {
			card_location = RREG32(mmD0_PSOC_BOOT_CONF_BASE +
						mmPSOC_BOOT_CONF_BOOT_STRAP_PINS_H);
			serdes_type = card_location;
			card_location &= PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_MODULE_ID_M;
			card_location >>= PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_MODULE_ID_S;
			cpucp_info->card_location = cpu_to_le32(card_location);
			nic->card_location = card_location;
			serdes_type &= ~(PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_MODULE_ID_M |
						PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_SRIS_M |
						PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_SRNS_M |
						PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_ADD_SSC_M |
						PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_DIE_ID_M);
			serdes_type >>= (PSOC_BOOT_CONF_BOOT_STRAP_PINS_H_DIE_ID_S + 1);
		} else {
			dev_warn(hdev->dev, "can't read card location as FW security is enabled\n");
		}
	}

	switch (serdes_type) {
	case HLS3_SERDES_TYPE:
		hdev->asic_prop.server_type = HL_SERVER_GAUDI3_HLS3;
		break;
	default:
		hdev->asic_prop.server_type = HL_SERVER_TYPE_UNKNOWN;
		break;
	}

	return 0;
}

static bool gaudi3_nic_get_hw_cap(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;

	return (gaudi3->hw_cap_initialized & HW_CAP_NIC_DRV);
}

static void gaudi3_nic_set_hw_cap(struct hl_device *hdev, bool enable)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;

	if (enable)
		gaudi3->hw_cap_initialized |= HW_CAP_NIC_DRV;
	else
		gaudi3->hw_cap_initialized &= ~HW_CAP_NIC_DRV;
}

static void gaudi3_nic_stop_traffic_macro(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	u32 port = gaudi3_nic_get_first_port(nic_macro);

	/* Skip masked NIC macros. */
	if (!(hdev->nic_ports_mask & gaudi3_nic_get_macro_ports_mask(nic_macro)))
		return;

	/* 1. Set RXB stop bit but allow interrupts to be generated. */
	NIC_WREG32(mmD0_NIC0_RXB_CORE_NIC_STOP_BIT,
			D0_NIC0_RXB_CORE_NIC_STOP_BIT_STOP_BIT_MESH_ONLY_M);

	/* Flush RX config and wait. */
	NIC_RREG32(mmD0_NIC0_RXB_CORE_NIC_STOP_BIT);
	usleep_range(1000, 2000);

	/* 2. Stop Timer HW block. */
	NIC_RMWREG32(mmD0_NIC0_TMR_TMR_CACHES_CFG, 1,
			D0_NIC0_TMR_TMR_CACHES_CFG_LIST_CACHE_STOP_M);
	NIC_RMWREG32(mmD0_NIC0_TMR_TMR_CACHES_CFG, 1,
			D0_NIC0_TMR_TMR_CACHES_CFG_FREE_LIST_CACHE_STOP_M);
	NIC_RMWREG32(mmD0_NIC0_TMR_TMR_CACHES_CFG, 1,
			D0_NIC0_TMR_TMR_CACHES_CFG_STATE_CACHE_STOP_M);

	/* Flush and wait for timers to drain. */
	NIC_RREG32(mmD0_NIC0_TMR_TMR_CACHES_CFG);
	usleep_range(1000, 2000);

	/* 3. Set RXB general stop bit. */
	NIC_WREG32(mmD0_NIC0_RXB_CORE_NIC_STOP_BIT, D0_NIC0_RXB_CORE_NIC_STOP_BIT_STOP_BIT_M);

	/* 4. Stop QPC HW block. */
	NIC_RMWREG32(mmD0_NIC0_QPC_REQ_STATIC_CONFIG, 1,
			D0_NIC0_QPC_REQ_STATIC_CONFIG_CACHE_STOP_M);
	NIC_RMWREG32(mmD0_NIC0_QPC_RES_STATIC_CONFIG, 1,
			D0_NIC0_QPC_RES_STATIC_CONFIG_CACHE_STOP_M);
	NIC_RREG32(mmD0_NIC0_QPC_REQ_STATIC_CONFIG); /* flush */

	/* 5. Stop TX scheduler HW block. */
	NIC_RMWREG32(mmD0_NIC0_TXS_CACHE_CFG, 1,
			NIC_TXS_CACHE_CFG_LIST_CACHE_STOP_M);
	NIC_RMWREG32(mmD0_NIC0_TXS_CACHE_CFG, 1,
			NIC_TXS_CACHE_CFG_FREE_LIST_CACHE_STOP_M);
	NIC_RREG32(mmD0_NIC0_TXS_CACHE_CFG); /* flush */
}

static void gaudi3_nic_stop_traffic(struct hl_device *hdev)
{
	int i;

	/* Force stop all available HW macros */
	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++)
		gaudi3_nic_stop_traffic_macro(&hdev->nic.nic_macros[i]);
}

static void gaudi3_nic_config_hw_mac(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	u32 port;

	port = gaudi3_nic_get_first_port(nic_macro);

	/* Privilege registers configured under nic bringup file */

	if (is_400g_mode(hdev)) {
		NIC_WREG32(mmD0_NIC0_MAC_CORE_BASE + mmPRT_MAC_CORE_PCS_MODE_SETTING,
				D0_NIC0_MAC_CORE_PCS_MODE_SETTING_PCS400_ENA_IN_M);
		NIC_WREG32(mmD0_NIC0_MAC_PCS_PCS400_BASE + mmMAC_PCS_PCS400_CONTROL1,
							D0_NIC0_MAC_PCS_PCS400_CONTROL1_F_RESET_M);
	} else {
		NIC_WREG32(mmD0_NIC0_MAC_PCS_RSFEC400_VENDOR_VL0_0, 0xc0b3);
		NIC_WREG32(mmD0_NIC0_MAC_PCS_RSFEC400_VENDOR_VL0_1, 0x8c);

		NIC_WREG32(mmD0_NIC0_MAC_PCS_PCS400_BASE +
				mmMAC_PCS_PCS400_VENDOR_VL_INTVL, 0x1000);
		NIC_WREG32(mmD0_NIC0_MAC_PCS_PCS200_BASE +
				mmMAC_PCS_PCS200_VENDOR_VL_INTVL, 0x1000);

		NIC_WREG32(mmD0_NIC0_MAC_CORE_BASE + mmPRT_MAC_CORE_PCS_MODE_SETTING,
				D0_NIC0_MAC_CORE_PCS_MODE_SETTING_PCS200_ENA_IN_M);

		NIC_WREG32(mmD0_NIC0_MAC_PCS_PCS400_BASE + mmMAC_PCS_PCS400_CONTROL1,
				(1 << D0_NIC0_MAC_PCS_PCS400_CONTROL1_SPEED_SELECTION_S) |
				(1 << D0_NIC0_MAC_PCS_PCS400_CONTROL1_F_RESET_S));
		NIC_WREG32(mmD0_NIC0_MAC_PCS_PCS200_BASE + mmMAC_PCS_PCS200_CONTROL1,
				(1 << D0_NIC0_MAC_PCS_PCS200_CONTROL1_SPEED_SELECTION_S) |
				(1 << D0_NIC0_MAC_PCS_PCS200_CONTROL1_F_RESET_S));
	}

	/* enable link status change EQ notify */
	/*
	 * TODO: enable MAC rx FIFO drop indication to EQ ?
	 * D0_NIC0_MAC_CORE_MAC_EQ_EN_RX_FIFO_VIOLATION_EN_S
	 */
	NIC_WREG32(mmD0_NIC0_MAC_CORE_BASE + mmPRT_MAC_CORE_MAC_EQ_EN,
			(1 << D0_NIC0_MAC_CORE_MAC_EQ_EN_LINK_STATUS_EN_S));
}

static void gaudi3_nic_config_hw_rxb(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	u32 i, val, ofst, port = gaudi3_nic_get_first_port(nic_macro);
	u32 total_credits = RXB_NUM_BUFFS;
	u32 stat_cred = RXB_NUM_RES_MTU * RXB_MTU_NUM_CREDS;
	u32 dyn_cred = total_credits - (RXB_NUM_STATIC_CRED_CONFS * stat_cred),
		/* no need for drop threshold so use ridiculously high value */
		drop = total_credits + 1,
		xoff = dyn_cred + 1,
		xon = dyn_cred - 2 * RXB_MTU_NUM_CREDS;

	/* Set iCRC calculation & verification with reversed bytes */
	NIC_WREG32(mmD0_NIC0_RXB_CORE_PRT_ICRC_CFG, 0x2);
	NIC_WREG32(mmD0_NIC0_RXB_CORE_PRT_BTH_TVER, 1);

	/* Support for QP-LB.
	 * These following lines disable the MAC filter of lane 3 which is the QP-LB lane.
	 * This cfg can't be used in 100Gbps mode.
	 */
	NIC_WREG32(mmD0_NIC0_RXB_CORE_PRT_TS_RC_MAC_31_0_MASK_3, 0xFFFFFFFF);
	NIC_WREG32(mmD0_NIC0_RXB_CORE_PRT_TS_RC_MAC_47_32_MASK_3, 0xFFFF);

	NIC_WREG32(mmD0_NIC0_RXB_CORE_AXI_AWPROT_HBW_PRIV, 0);
	NIC_WREG32(mmD0_NIC0_RXB_CORE_AXI_AWPROT_HBW_SEC, 0);
	NIC_WREG32(mmD0_NIC0_RXB_CORE_AXI_AWPROT_HBW_UNSEC, 2);
	NIC_WREG32(mmD0_NIC0_RXB_CORE_AXI_AWPROT_HBW_LPBK_PRIV, 0);
	NIC_WREG32(mmD0_NIC0_RXB_CORE_AXI_AWPROT_HBW_LPBK_SEC, 0);
	NIC_WREG32(mmD0_NIC0_RXB_CORE_AXI_AWPROT_HBW_LPBK_UNSEC, 2);

	NIC_WREG32(mmD0_NIC0_RXB_CORE_MAX_DYNAMIC, dyn_cred);

	/* static credits are only allocated to priority 0 of each port, so:
	 * first clear all the preallocated static credits and then
	 * assign static credits to priority 0 of the existing NIC ports
	 */
	for (i = 0 ; i < RXB_NUM_PORT_PRIO_REGS ; i++)
		NIC_WREG32(mmD0_NIC0_RXB_CORE_MAX_STATIC_CREDITS_0 + i * sizeof(u32), 0);

	for (i = 0 ; i < RXB_MAX_NUM_PORTS ; i++) {
		ofst = get_resource_offset(hdev, i, RXB_NUM_PORT_PRIO_REGS);
		NIC_WREG32(mmD0_NIC0_RXB_CORE_MAX_STATIC_CREDITS_0 + ofst * sizeof(u32), stat_cred);
	}

	/* Drop threshold (per port/prio) */
	val = drop | (drop << D0_NIC0_RXB_CORE_DROP_THRESHOLD_DROP_THRESHOLD_FLOW1_S);
	for (i = 0; i < RXB_NUM_PORT_PRIO_REGS; i++)
		NIC_WREG32(mmD0_NIC0_RXB_CORE_DROP_THRESHOLD_0 + i * sizeof(u32), val);

	/* XONN threshold (per port/prio) */
	val = xon | (xon << D0_NIC0_RXB_CORE_XON_THRESHOLD_XON_THRESHOLD_FLOW1_S);
	for (i = 0; i < RXB_NUM_PORT_PRIO_REGS; i++)
		NIC_WREG32(mmD0_NIC0_RXB_CORE_XON_THRESHOLD_0 + i * sizeof(u32), val);

	/* XOFF threshold (per port/prio) */
	val = xoff | (xoff << D0_NIC0_RXB_CORE_XOFF_THRESHOLD_XOFF_THRESHOLD_FLOW1_S);
	for (i = 0; i < RXB_NUM_PORT_PRIO_REGS; i++)
		NIC_WREG32(mmD0_NIC0_RXB_CORE_XOFF_THRESHOLD_0 + i * sizeof(u32), val);

	/* RXB CORE cache not skipping cache with writeback, allocate controlled by user bit */
	NIC_WREG32(mmD0_NIC0_RXB_CORE_AXI_AWCACHE,
						(0x1 << D0_NIC0_RXB_CORE_AXI_AWCACHE_BIT0_S) |
						(0x1 << D0_NIC0_RXB_CORE_AXI_AWCACHE_BIT1_S));

	/* RXB CORE cache lpbk not skipping cache with writeback, allocate controlled by user bit */
	NIC_WREG32(mmD0_NIC0_RXB_CORE_AXI_AWCACHE_LPBK,
						(0x1 << D0_NIC0_RXB_CORE_AXI_AWCACHE_LPBK_BIT0_S) |
						(0x1 << D0_NIC0_RXB_CORE_AXI_AWCACHE_LPBK_BIT1_S));
}

static void gaudi3_nic_config_hw_txb(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	u32 port;

	port = gaudi3_nic_get_first_port(nic_macro);

	/* Set iCRC calculation&generation with reversed bytes */
	NIC_WREG32(mmD0_NIC0_TXB_BASE + mmNIC_TXB_ICRC_CFG, 0x2);

	NIC_WREG32(mmD0_NIC0_TXB_BASE + mmNIC_TXB_LB_CREDIT, 18);

	if (is_400g_mode(hdev))
		NIC_WREG32(mmD0_NIC0_TXB_BASE + mmNIC_TXB_NUM_OF_PORTS, 1);
	else
		NIC_WREG32(mmD0_NIC0_TXB_BASE + mmNIC_TXB_NUM_OF_PORTS, 2);
}

static void gaudi3_nic_config_hw_tmr(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev = nic_macro->hdev;
	struct gaudi3_nic_macro *gaudi3_macro;
	struct hl_nic_properties *nic_prop;
	void *cpu_addr;
	u64 tmr_addr;
	u32 port;
	int i;

	port = gaudi3_nic_get_first_port(nic_macro);
	nic_prop = &hdev->asic_prop.nic_props;
	gaudi3_macro = nic_macro->asic_priv;

	if (hdev->dram_enable) {
		tmr_addr = nic_prop->tmr_base_addr + nic_macro->idx * nic_prop->tmr_base_size;

		/* Clear timer FSM */
		for (i = 0 ; i < NIC_MAX_QP_NUM ; i++) {
			hl_nic_dram_writeb(hdev, 0, tmr_addr + TMR_FSM0_OFFS + i);

			if ((i % NIC_MAX_COMBINED_WRITES) == 0)
				hl_nic_dram_readb(hdev, tmr_addr + TMR_FSM0_OFFS + i);
		}

		/* Timer free list */
		for (i = 0 ; i < TMR_FREE_NUM_ENTRIES ; i++) {
			hl_nic_dram_writel(hdev,
					TMR_GRANULARITY + i, tmr_addr + TMR_FREE_OFFS + i * 4);
			if ((i % NIC_MAX_COMBINED_WRITES) == 0)
				hl_nic_dram_readl(hdev, tmr_addr + TMR_FREE_OFFS + i * 4);
		}

		/* Perform read to flush the writes */
		hl_nic_dram_readq(hdev, tmr_addr);
	} else {
		cpu_addr = gaudi3_macro->tmr_mem.addr;
		tmr_addr = gaudi3_macro->tmr_mem.dma_addr;

		/* Clear timer FSM */
		for (i = 0 ; i < NIC_MAX_QP_NUM ; i++)
			writeb(0, (void __iomem *)cpu_addr + TMR_FSM0_OFFS + i);

		/* Timer free list */
		for (i = 0 ; i < TMR_FREE_NUM_ENTRIES ; i++)
			writel(TMR_GRANULARITY + i,
				(void __iomem *)cpu_addr + TMR_FREE_OFFS + i * 4);
	}

	NIC_WREG32(mmD0_NIC0_TMR_TMR_BASE_ADDRESS_63_32,
			 upper_32_bits(tmr_addr + TMR_FIFO_OFFS));
	NIC_WREG32(mmD0_NIC0_TMR_TMR_BASE_ADDRESS_31_7,
			 lower_32_bits(tmr_addr + TMR_FIFO_OFFS) >> 7);

	NIC_WREG32(mmD0_NIC0_TMR_TMR_BASE_ADDRESS_FREE_LIST_63_32,
			 upper_32_bits(tmr_addr + TMR_FREE_OFFS));
	NIC_WREG32(mmD0_NIC0_TMR_TMR_BASE_ADDRESS_FREE_LIST_31_0,
			 lower_32_bits(tmr_addr + TMR_FREE_OFFS));

	NIC_WREG32(mmD0_NIC0_TMR_TMR_CACHE_BASE_ADDR_63_32,
			 upper_32_bits(tmr_addr + TMR_FSM0_OFFS));
	NIC_WREG32(mmD0_NIC0_TMR_TMR_CACHE_BASE_ADDR_31_7,
			 lower_32_bits(tmr_addr + TMR_FSM0_OFFS) >> 7);

	/* configure MMU-BP for TIMERS */
	gaudi3_axuser_hbw_mmu_bp_set(hdev, NIC_REG(mmD0_NIC0_TMR_AXUSER_AXUSER_BASE), true);

	/* Perform read to flush the writes */
	NIC_RREG32(mmD0_NIC0_TMR_AXUSER_AXUSER_HB_MMU_BYPASS);

	NIC_WREG32(mmD0_NIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_0, 0);
	NIC_WREG32(mmD0_NIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_1, 0);
	NIC_WREG32(mmD0_NIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_2, 0);

	/* Set the amount of ticks for timeout. */
	/* SW-108577: TODO revisit timeout value for external ports.*/
	NIC_WREG32(mmD0_NIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_5, NIC_TMR_TIMEOUT_US);
	NIC_WREG32(mmD0_NIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_6, 0);

	for (i = 0 ; i < TMR_GRANULARITY ; i++) {
		NIC_WREG32(mmD0_NIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_3, i);
		NIC_WREG32(mmD0_NIC0_TMR_TMR_SCHEDQ_UPDATE_DESC_4, i);
		NIC_WREG32(mmD0_NIC0_TMR_TMR_SCHEDQ_UPDATE_FIFO, i);
		NIC_WREG32(mmD0_NIC0_TMR_TMR_SCHEDQ_UPDATE_EN, 1);
	}

	NIC_WREG32(mmD0_NIC0_TMR_TMR_SCAN_TIMER_COMP_31_0, 10);

	/* Set the number of clock's cycles for a single tick in order to have 1 usec per tick.
	 * i.e.: 1/frequency_in_MHz * num_of_clk_cycles = 1 usec
	 */
	NIC_WREG32(mmD0_NIC0_TMR_TMR_TICK_WRAP, nic_prop->clk);
	/* mask should never be zero */
	NIC_WREG32(mmD0_NIC0_TMR_TMR_LIST_MASK,
			max(~(0xFFFFFFFF << (ilog2(TMR_FREE_NUM_ENTRIES) - 5)), (u32)1));

	NIC_WREG32(mmD0_NIC0_TMR_TMR_PRODUCER_UPDATE, TMR_FREE_NUM_ENTRIES);
	/* Latch the TMR value */
	NIC_WREG32(mmD0_NIC0_TMR_TMR_PRODUCER_UPDATE_EN, 1);
	NIC_WREG32(mmD0_NIC0_TMR_TMR_PRODUCER_UPDATE_EN, 0);

	NIC_WREG32(mmD0_NIC0_TMR_TMR_LIST_MEM_READ_MASK, 0);
	NIC_WREG32(mmD0_NIC0_TMR_TMR_PUSH_LOCK_EN, 1);
	NIC_WREG32(mmD0_NIC0_TMR_TMR_TIMER_EN, 1);
	NIC_WREG32(mmD0_NIC0_TMR_FREE_LIST_PUSH_MASK_EN, 0);

	NIC_WREG32(mmD0_NIC0_TMR_TMR_FORCE_HIT_EN, 0);

	/* TMR cache should be set to read mode but not alloc = 0x3 */
	NIC_WREG32(mmD0_NIC0_TMR_TMR_AXI_CACHE, (0x3 << D0_NIC0_TMR_TMR_AXI_CACHE_AR_S));
	NIC_WREG32(mmD0_NIC0_TMR_TMR_AXI_CACHE, (0x3 << D0_NIC0_TMR_TMR_AXI_CACHE_AW_S));
	NIC_WREG32(mmD0_NIC0_TMR_TMR_AXI_CACHE, (0x3 << D0_NIC0_TMR_TMR_AXI_CACHE_CACHE_AR_S));
	NIC_WREG32(mmD0_NIC0_TMR_TMR_AXI_CACHE, (0x3 << D0_NIC0_TMR_TMR_AXI_CACHE_CACHE_AW_S));

	/* Perform read from the device to flush all configurations
	 * TODO: SW-111594: check why w/o this read we are getting hangs in the device.
	 */
	NIC_RREG32(mmD0_NIC0_TMR_TMR_TIMER_EN);
}

static void gaudi3_nic_hw_macro_config(struct hl_nic_macro *nic_macro)
{
	/* MAC Configuration */
	gaudi3_nic_config_hw_mac(nic_macro);

	/* RXB Configuration */
	gaudi3_nic_config_hw_rxb(nic_macro);

	/* TXB Configuration */
	gaudi3_nic_config_hw_txb(nic_macro);

	/* TMR Configuration */
	gaudi3_nic_config_hw_tmr(nic_macro);

	/* TXS Configuration */
	gaudi3_nic_config_hw_txs(nic_macro);

	gaudi3_nic_config_hw_txe(nic_macro);

	gaudi3_nic_config_hw_qpc(nic_macro);

	gaudi3_nic_config_hw_rxe(nic_macro);
}

static void gaudi3_nic_config_coll_lag_size(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;

	/* If the value is non-zero then that means, the user supplied the value via debugfs entry
	 * and that should be retained across hard and soft reset. If not just program the default
	 * lag size of '3'.
	 */
	if (!gaudi3->coll_lag_size)
		gaudi3->coll_lag_size = NIC_DEFAULT_COLL_LAG_SIZE;
}

static void gaudi3_nic_macros_hw_config(struct hl_device *hdev)
{
	struct hl_nic_macro *nic_macro;
	u32 port;
	int i;

	gaudi3_nic_config_coll_lag_size(hdev);

	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		nic_macro = &hdev->nic.nic_macros[i];

		/* It's not allowed to configure a macro that its port or ports are disabled.
		 * In 400Gbps mode we have a single port in each macro.
		 * In 200Gbps mode we need to check also the second port in the macro. Only if both
		 * of the ports are disabled, we should skip this macro.
		 */
		if (!gaudi3_nic_is_macro_enabled(hdev, nic_macro))
			continue;

		port = gaudi3_nic_get_first_port(nic_macro);

		/* H9-5194: Setting timeout for PRT configurations. */
		NIC_WREG32(mmD0_NIC0_PHY_SPECIAL_GLBL_SPARE_0, 0x1001000);
		NIC_WREG32(mmD0_NIC0_MAC_AUX_SPECIAL_GLBL_SPARE_0, 0x1001000);

		gaudi3_nic_hw_macro_config(nic_macro);
	}
}

static int gaudi3_nic_core_init(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_prop = &hdev->asic_prop.nic_props;
	struct hl_nic *nic = &hdev->nic;

	/* PCI card is a testing card so set all ports as external */
	if (hdev->card_type == cpucp_card_type_pci) {
		hdev->nic_ports_ext_mask = hdev->nic_ports_mask;
		hdev->nic_auto_neg_mask &= ~hdev->nic_ports_ext_mask;
	}

	nic->auto_neg_mask &= hdev->nic_auto_neg_mask;

	gaudi3_nic_macros_hw_config(hdev);

	/* TODO: SW-69799: Get the clock rate dynamically via get_clk_rate. Optimize the amount of
	 * pkts to wait for and the timeout it requires.
	 * For now request an interrupt after 1 pkt with 10 usec timeout.
	 */
	if (!nic->skip_cq_arm_timeout)
		nic->cq_arm_timeout = lower_32_bits((nic_prop->clk * CQ_ARM_TIMEOUT_USEC) / SZ_1K);

	return gaudi3_nic_eq_init(hdev);
}

static void gaudi3_nic_core_fini(struct hl_device *hdev)
{
	gaudi3_nic_stop_traffic(hdev);
	gaudi3_nic_eq_fini(hdev);
}

static int normalize_priority(struct hl_device *hdev, u32 priority,
				enum hl_ts_type type, bool is_req, u32 *norm_priority)
{
	/* Ethernet and Responder get the highest priority */
	if (!is_req || (type == TS_RAW)) {
		*norm_priority = GAUDI3_PFC_PRIO_DRIVER;
		return 0;
	}

	/* Req priority can vary from 1 to 3 */
	if ((priority < GAUDI3_PFC_PRIO_USER_BASE) || (priority >= HL_EN_PFC_PRIO_NUM))
		return -EINVAL;

	*norm_priority = priority;
	return 0;
}

static u32 __gaudi3_nic_txs_get_schedq_num(u32 port_first_lane,
					u32 priority, enum hl_ts_type ts, bool is_req)
{
	u32 prio_q_group;

	/* prio-group numbering start from 1 - normalize it to Zero */
	if (ts == TS_RAW)
		prio_q_group = TXS_PORT_RAW_SCHED_Q;
	else
		prio_q_group = (is_req ? TXS_PORT_REQ_SCHED_Q : TXS_PORT_RES_SCHED_Q);

	/* TODO: SW-91783 use only 3 priorities */
	return port_first_lane * TXS_PORT_NUM_SCHEDQS +
			prio_q_group * HL_EN_PFC_PRIO_NUM + priority;
}

static u32 gaudi3_nic_txs_get_schedq_num(struct hl_aux_dev *aux_dev,
						u32 port_first_lane, bool is_req)
{
	return __gaudi3_nic_txs_get_schedq_num(port_first_lane,
						GAUDI3_PFC_PRIO_DRIVER, TS_RAW, is_req);
}

static bool is_valid_mtu(u16 mtu)
{
	return (mtu == SZ_1K) || (mtu == SZ_2K) || (mtu == SZ_4K) || (mtu == SZ_8K);
}

static void gaudi3_get_default_encap_id(struct hl_nic_port *nic_port, u32 *id)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	*id = GAUDI3_DEFAULT_ENCAP_ID(port);
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

static void gaudi3_set_ip_addr_encap(struct hl_nic_port *nic_port, u32 *encap_id, u32 src_ip)
{
	struct hl_nic_encap_idr_pdata encap_data;
	uint8_t dummy_hdr[NIC_MAX_TNL_HDR_SIZE] = {};

	gaudi3_get_default_encap_id(nic_port, encap_id);

	if (!src_ip && get_src_ip(nic_port, &src_ip)) {
		dev_dbg(nic_port->hdev->dev, "failed to get interface IP, using 0\n");
		src_ip = 0;
	}

	encap_data.port = nic_port->port;
	encap_data.id = *encap_id;
	encap_data.src_ip = src_ip;

	if (nic_port->hdev->nic_enable_h9_rx_drop_eco) {
		memset(dummy_hdr, 0xa5, sizeof(dummy_hdr));
		encap_data.encap_type = HL_NIC_ENCAP_OVER_IPV4;
		encap_data.encap_type_data = IPv4_PROTOCOL_DUMMY;
		encap_data.encap_header = dummy_hdr;
		encap_data.encap_header_size = sizeof(dummy_hdr);

	} else {
		encap_data.encap_type = HL_NIC_ENCAP_NONE;
		encap_data.encap_type_data = IPv4_PROTOCOL_UDP;
		encap_data.encap_header = NULL;
		encap_data.encap_header_size = 0;
	}

	gaudi3_encap_set(nic_port, *encap_id, &encap_data);
}

static int gaudi3_nic_set_req_qp_ctx(struct hl_device *hdev,
				struct hl_nic_req_conn_ctx_in *in,
				struct hl_qp *qp)
{
	u32 congestion_wnd, log_wq_size, port, priority, encap_id, wq_base_addr, wq_idx_offset,
		encap_en;
	struct gaudi3_qpc_requester req_qpc;
	struct gaudi3_nic_port *gaudi3_nic;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_user_cq *user_cq;
	struct hl_en_aux_ops *aux_ops;
	struct gaudi3_device *gaudi3;
	struct hl_nic_port *nic_port;
	struct hl_aux_dev *aux_dev;
	bool loopback, plain_rdma;
	u8 mac[ETH_ALEN], cqn;
	int logical_port, rc;

	port = in->port;
	aux_dev = &nic->en_aux_dev;
	aux_ops = aux_dev->aux_ops;
	nic_port = &nic->nic_ports[port];
	gaudi3_nic = nic_port->nic_specific;
	gaudi3 = hdev->asic_specific;

	if (in->mtu && !is_valid_mtu(in->mtu)) {
		dev_dbg(hdev->dev, "MTU of %u is not supported\n", in->mtu);
		return -EINVAL;
	}

	if (!IS_POWER_OF_2(in->wq_size)) {
		dev_dbg(hdev->dev,
			"WQ size, denoted by wq_size (%u), is not power of two\n",
			in->wq_size);
		return -EINVAL;
	}

	log_wq_size = ilog2(in->wq_size);

	if (in->coll_lag_idx >= gaudi3->coll_lag_size) {
		dev_dbg(hdev->dev,
			"Port %u: LAG idx is invalid. Idx: %d, size %d\n", port,
			in->coll_lag_idx, gaudi3->coll_lag_size);
		return -EINVAL;
	}

	if (normalize_priority(hdev, in->priority, TS_RC, true, &priority)) {
		dev_dbg(hdev->dev, "Unsupported priority value %u, port %d\n", in->priority, port);
		return -EINVAL;
	}

	/* H9-5384: Below configuration isn't valid due to H/W bug, i.e.: using encap_id for src IP
	 * settings w/o encapsulation isn't allowed.
	 */
	if (hdev->nic_enable_h9_rx_drop_eco && !in->encap_en && in->encap_id) {
		dev_dbg(hdev->dev,
			"Encapsulation ID %d can't be set when encapsulation disable, port %d\n",
			in->encap_id, port);
		return -EINVAL;
	}

	if (in->cq_number) {
		/* User CQ. */
		cqn = in->cq_number;

		user_cq = hl_nic_user_cq_get(nic_port, cqn);
		if (IS_ERR_OR_NULL(user_cq)) {
			dev_dbg(hdev->dev, "CQ %d is invalid, port %d\n", cqn, port);
			return -EINVAL;
		}

		qp->req_user_cq = user_cq;
	} else {
		/* No CQ. */
		cqn = get_drv_cqn(nic_port);
	}

	memset(&req_qpc, 0, sizeof(req_qpc));

	logical_port = __get_lane_offset(hdev, port);

	if (nic_port->eth_enable)
		memcpy(mac, in->dst_mac_addr, ETH_ALEN);
	else
		/* in this case the MAC is irrelevant so use broadcast */
		eth_broadcast_addr(mac);

	REQ_QPC_SET_DST_QP(req_qpc, in->dst_conn_id);
	REQ_QPC_SET_PORT(req_qpc, logical_port); /* TODO: SW-67924 */
	REQ_QPC_SET_PRIORITY(req_qpc, priority);
	REQ_QPC_SET_RKEY(req_qpc, qp->remote_key);
	REQ_QPC_SET_DST_IP(req_qpc, in->dst_ip_addr);
	REQ_QPC_SET_DST_MAC_LSB(req_qpc, *(u32 *) mac);
	REQ_QPC_SET_DST_MAC_MSB(req_qpc, *(u16 *) (mac + 4));

	REQ_QPC_SET_SCHD_Q_NUM(req_qpc,
			__gaudi3_nic_txs_get_schedq_num(logical_port, priority, TS_RC, true));
	REQ_QPC_SET_TM_GRANULARITY(req_qpc, in->timer_granularity);

	REQ_QPC_SET_TRANSPORT_SERVICE(req_qpc, TS_RC);
	REQ_QPC_SET_BURST_SIZE(req_qpc, QPC_REQ_BURST_SIZE);
	REQ_QPC_SET_LOCAL_WQ_LOG_SZ(req_qpc, log_wq_size);

	if (qp->is_coll) {
		if (hl_nic_is_scale_out_coll_type(qp->coll_conn_type)) {
			wq_base_addr = GAUDI3_TXE_COLL_SCALE_OUT_WQ_RDMA_IDX(port);
			wq_idx_offset = nic_port->scale_out_coll_qp_idx_offset;
		} else {
			wq_base_addr = GAUDI3_TXE_COLL_WQ_RDMA_IDX(port);
			wq_idx_offset = nic_port->coll_qp_idx_offset;
		}
	} else {
		wq_base_addr = GAUDI3_TXE_WQ_RDMA_IDX(port);
		wq_idx_offset = nic_port->qp_idx_offset;
	}

	REQ_QPC_SET_WQ_BASE_ADDR(req_qpc, wq_base_addr);
	REQ_QPC_SET_WQ_IDX(req_qpc, qp->qp_id - wq_idx_offset);

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
		qp->mtu = GAUDI3_NIC_MTU_DEFAULT;
		qp->mtu_type = MTU_DEFAULT;
	}

	REQ_QPC_SET_MTU(req_qpc, ilog2(roundup_pow_of_two(qp->mtu)) - 10);

	REQ_QPC_SET_SWQ_GRANULARITY(req_qpc, in->swq_granularity);
	REQ_QPC_SET_CQ_NUM(req_qpc, cqn);
	REQ_QPC_SET_EQ_NUM(req_qpc, GAUDI3_EQ_RAW_IDX(port));	/* TODO: SW-67924 */

	/* Protect the HW from zero value */
	REQ_QPC_SET_REMOTE_WQ_LOG_SZ(req_qpc, in->wq_remote_log_size ? in->wq_remote_log_size : 2);

	/* config MMU-BP */
	if (hdev->mmu_enable)
		REQ_QPC_SET_DATA_MMU_BYPASS(req_qpc, 0);
	else
		REQ_QPC_SET_DATA_MMU_BYPASS(req_qpc, 1);

	/* ASID is also used as protection-domain, so always configure it */
	REQ_QPC_SET_ASID(req_qpc, qp->asid);

	REQ_QPC_SET_ACKREQ_FREQ(req_qpc, 8);
	REQ_QPC_SET_WQ_TYPE(req_qpc, in->wq_type);
	REQ_QPC_SET_SACK_ENABLE(req_qpc, in->sack_en);

	/* user QP - unsecured trust level */
	REQ_QPC_SET_TRUST_LEVEL(req_qpc, UNSECURED);

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
	 *
	 */
	REQ_QPC_SET_CONGESTION_MODE(req_qpc, (in->congestion_en) ? 1 : 2);

	REQ_QPC_SET_RTT_STATE(req_qpc, in->congestion_en);

	if (in->congestion_wnd)
		congestion_wnd = in->congestion_wnd;
	else if (in->congestion_en)
		congestion_wnd = GAUDI3_CC_MAX_WINDOW_SIZE;
	else
		congestion_wnd = 1 << 23;
	REQ_QPC_SET_CONGESTION_WIN(req_qpc, congestion_wnd);

	/* HW accelerated congestion control default values. */
	REQ_QPC_SET_AI(req_qpc, 1);
	REQ_QPC_SET_BETA_NOMINATOR(req_qpc, 3);
	REQ_QPC_SET_BETA_DE_NOMINATOR(req_qpc, 4);
	REQ_QPC_SET_MAX_MDF(req_qpc, 2);
	REQ_QPC_SET_TARGET_DELAY(req_qpc, 1000);

	if (in->encap_en || (!hdev->nic_enable_h9_rx_drop_eco && in->encap_id))
		encap_id = in->encap_id;
	else
		gaudi3_get_default_encap_id(nic_port, &encap_id);

	if (hdev->nic_enable_h9_rx_drop_eco)
		encap_en = 1;
	else
		encap_en = in->encap_en;

	REQ_QPC_SET_ENCAP_ENABLE(req_qpc, encap_en);
	REQ_QPC_SET_ENCAP_TYPE(req_qpc, encap_id);

	loopback = !!in->loopback;
	REQ_QPC_SET_LOOPBACK(req_qpc, loopback);

	/* Plain RDMA should be set only if advanced field is not set */
	plain_rdma = !gaudi3_nic->advanced;
	REQ_QPC_SET_PLAIN_RDMA(req_qpc, plain_rdma);

	REQ_QPC_SET_LAST_NIC_IN_LAG(req_qpc, in->coll_last_in_lag);
	REQ_QPC_SET_LAG_IDX(req_qpc, in->coll_lag_idx);

	REQ_QPC_SET_COMPRESSION_ENABLE(req_qpc, in->compression_en);

	REQ_QPC_SET_VALID(req_qpc, 1);

	rc = gaudi3_nic_qpc_write(nic_port, &req_qpc, NULL, qp->qp_id, true);
	if (rc)
		goto qpc_write_fail;

	return 0;
qpc_write_fail:
	if (qp->req_user_cq) {
		hl_nic_user_cq_put(qp->req_user_cq);
		qp->req_user_cq = NULL;
	}

	return rc;
}

static int gaudi3_nic_set_res_qp_ctx(struct hl_device *hdev,
				struct hl_nic_res_conn_ctx_in *in,
				struct hl_qp *qp)
{
	struct hl_nic_port *nic_port;
	struct hl_nic *nic = &hdev->nic;
	struct gaudi3_qpc_responder res_qpc;
	struct gaudi3_nic_port *gaudi3_nic;
	struct hl_nic_user_cq *user_cq;
	bool loopback, plain_rdma, atomic_fna;
	u32 port = in->port, encap_id, priority, encap_en;
	u8 mac[ETH_ALEN], cqn;
	int logical_port, rc;

	nic_port = &nic->nic_ports[port];
	gaudi3_nic = nic_port->nic_specific;
	logical_port = __get_lane_offset(hdev, port);

	if (normalize_priority(hdev, in->priority, TS_RC, false, &priority)) {
		dev_dbg(hdev->dev, "Unsupported priority value %u, port %d\n", in->priority, port);
		return -EINVAL;
	}

	if (in->rdv) {
		u32 num_of_wq_entries;

		if (!in->wq_peer_size) {
			dev_dbg(hdev->dev, "wq_peer_size cannot be 0 for an rdv QP\n");
			return -EINVAL;
		}

		/* TODO: SW-84552: num_of_wq_entries is changed from user_wq_arr_(un)set and hence
		 * need to add proper locking
		 */
		if (qp->is_coll) {
			num_of_wq_entries =
				nic->coll_props[qp->coll_conn_type].num_of_coll_wq_entries;

			if (in->wq_peer_size > num_of_wq_entries) {
				dev_dbg(hdev->dev,
					"wq_peer_size (0x%x) cannot be bigger than collective WQ entries number (0x%x)\n",
					in->wq_peer_size, num_of_wq_entries);
				return -EINVAL;
			}
		} else {
			num_of_wq_entries = nic_port->num_of_wq_entries;

			if (in->wq_peer_size > num_of_wq_entries) {
				dev_dbg(hdev->dev,
					"wq_peer_size (0x%x) cannot be bigger than WQ entries number (0x%x)\n",
					in->wq_peer_size, num_of_wq_entries);
				return -EINVAL;
			}
		}
	}

	/* H9-5384: Below configuration isn't valid due to H/W bug, i.e.: using encap_id for src IP
	 * settings w/o encapsulation isn't allowed.
	 */
	if (hdev->nic_enable_h9_rx_drop_eco && !in->encap_en && in->encap_id) {
		dev_dbg(hdev->dev,
			"Encapsulation ID %d can't be set when encapsulation disable, port %d\n",
			in->encap_id, port);
		return -EINVAL;
	}

	if (in->cq_number) {
		/* User CQ. */
		cqn = in->cq_number;

		user_cq = hl_nic_user_cq_get(nic_port, cqn);
		if (IS_ERR_OR_NULL(user_cq)) {
			dev_dbg(hdev->dev, "CQ %d is invalid, port %d\n", cqn, port);
			return -EINVAL;
		}

		qp->res_user_cq = user_cq;
	} else {
		/* No CQ. */
		cqn = get_drv_cqn(nic_port);
	}

	if (nic_port->eth_enable)
		memcpy(mac, in->dst_mac_addr, ETH_ALEN);
	else
		/* in this case the MAC is irrelevant so use broadcast */
		eth_broadcast_addr(mac);

	memset(&res_qpc, 0, sizeof(res_qpc));

	RES_QPC_SET_DST_QP(res_qpc, in->dst_conn_id);
	RES_QPC_SET_PORT(res_qpc, logical_port); /* TODO: SW-67924 */
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
	if (hdev->mmu_enable && !(in->rdv && nic->wq_mmu_bypass))
		RES_QPC_SET_DATA_MMU_BYPASS(res_qpc, 0);
	else
		RES_QPC_SET_DATA_MMU_BYPASS(res_qpc, 1);

	/* ASID is also used as protection-domain, so always configure it */
	RES_QPC_SET_ASID(res_qpc, qp->asid);

	RES_QPC_SET_SACK_EN(res_qpc, in->sack_en);

	RES_QPC_SET_SCHD_Q_NUM(res_qpc,
			__gaudi3_nic_txs_get_schedq_num(logical_port, priority, TS_RC, false));

	/* for rdv QPs RXE responder takes its security-level from QPC */
	if (in->rdv)
		RES_QPC_SET_TRUST_LEVEL(res_qpc, SECURED);
	else
		RES_QPC_SET_TRUST_LEVEL(res_qpc, UNSECURED);

	RES_QPC_SET_CQ_NUM(res_qpc, cqn);

	RES_QPC_SET_EQ_NUM(res_qpc, GAUDI3_EQ_RAW_IDX(port)); /* TODO: SW-67924 */

	if (in->conn_peer) {
		struct hl_qp *peer_qp;
		u32 peer_wq_base_addr, peer_wq_idx_offset;

		peer_qp = qp->is_coll ?
				hl_nic_get_qp_from_coll_conn_id(nic_port, in->conn_peer) :
				idr_find(&nic_port->qp_ids, in->conn_peer);

		if (IS_ERR_OR_NULL(peer_qp)) {
			dev_dbg(hdev->dev, "conn_peer %d is invalid, port %d\n",
				in->conn_peer, port);
			return -EINVAL;
		}

		if (qp->is_coll) {
			if (hl_nic_is_scale_out_coll_type(qp->coll_conn_type)) {
				peer_wq_base_addr = GAUDI3_TXE_COLL_SCALE_OUT_WQ_RDMA_IDX(port);
				peer_wq_idx_offset = nic_port->scale_out_coll_qp_idx_offset;
			} else {
				peer_wq_base_addr = GAUDI3_TXE_COLL_WQ_RDMA_IDX(port);
				peer_wq_idx_offset = nic_port->coll_qp_idx_offset;
			}
		} else {
			peer_wq_base_addr = GAUDI3_TXE_WQ_RDMA_IDX(port);
			peer_wq_idx_offset = nic_port->qp_idx_offset;
		}

		RES_QPC_SET_PEER_WQ_BASE_ADDR(res_qpc, peer_wq_base_addr);
		RES_QPC_SET_PEER_WQ_IDX(res_qpc, peer_qp->qp_id - peer_wq_idx_offset);
		RES_QPC_SET_PEER_QP(res_qpc, peer_qp->qp_id);
		RES_QPC_SET_PEER_WQ_GRAN(res_qpc, in->wq_peer_granularity);
		RES_QPC_SET_PEER_WQ_LOG_SIZE(res_qpc, ilog2(in->wq_peer_size));
	}

	loopback = !!in->loopback;
	RES_QPC_SET_LOOPBACK(res_qpc, loopback);

	/* Plain RDMA should be set only if advanced field is not set */
	plain_rdma = !gaudi3_nic->advanced;
	RES_QPC_SET_PLAIN_RDMA(res_qpc, plain_rdma);

	/* Atomic F&A should be set only if advanced field is set */
	atomic_fna = !!gaudi3_nic->advanced;
	RES_QPC_SET_ATOMIC_FA_EN(res_qpc, atomic_fna);


	if (in->encap_en || (!hdev->nic_enable_h9_rx_drop_eco && in->encap_id))
		encap_id = in->encap_id;
	else
		gaudi3_get_default_encap_id(nic_port, &encap_id);

	if (hdev->nic_enable_h9_rx_drop_eco)
		encap_en = 1;
	else
		encap_en = in->encap_en;

	RES_QPC_SET_ENCAP_ENABLE(res_qpc, encap_en);
	RES_QPC_SET_ENCAP_TYPE(res_qpc, encap_id);

	RES_QPC_SET_VALID(res_qpc, 1);

	rc = gaudi3_nic_qpc_write(nic_port, &res_qpc, NULL, qp->qp_id, false);
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

static int gaudi3_nic_update_qp_mtu(struct hl_nic_port *nic_port, struct hl_qp *qp, u32 mtu)
{
	struct gaudi3_qpc_requester req_qpc = {};
	struct qpc_mask mask = {};

	/* MTU field is 2 bits wide */
	REQ_QPC_SET_MTU(mask, 0x3);
	REQ_QPC_SET_MTU(req_qpc, ilog2(roundup_pow_of_two(mtu)) - 10);

	return gaudi3_nic_qpc_write_masked(nic_port, &req_qpc, &mask, qp->qp_id, true, false);
}

static int gaudi3_user_wq_arr_set(struct hl_device *hdev,
				struct hl_nic_user_wq_arr_set_in *in,
				struct hl_nic_user_wq_arr_set_out *out,
				struct hl_ctx *ctx)
{
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_nic_mem_data mem_data = {};
	struct gaudi3_nic_port *gaudi3_nic;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic;
	u64 wq_base_addr, wq_size_cline_log, wq_size, wq_arr_size, num_of_wqs,
		num_of_wq_entries;
	u32 wqe_size, type, port, wqe_asid, offset, alignment_size;
	bool is_mmu_bp = true;
	int rc;

	type = in->type;
	port = in->port;
	nic = &hdev->nic;
	nic_port = &hdev->nic.nic_ports[port];
	gaudi3_nic = nic_port->nic_specific;

	if (in->addr) {
		dev_dbg(hdev->dev, "User WQ array address shouldn't be set: 0x%llx\n", in->addr);
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
		mem_data.mem_id = HL_NIC_DRV_MEM_HOST_DMA_COHERENT;
	} else {
		num_of_wq_entries = in->num_of_wq_entries;
		wq_size = ALIGN(num_of_wq_entries * wqe_size, DEVICE_CACHE_LINE_SIZE);
		mem_data.mem_id = HL_NIC_DRV_MEM_DEVICE;
		mem_data.in.device_mem_data.port = port;
		mem_data.in.device_mem_data.type = type;
		wqe_asid = HL_KERNEL_ASID_ID;
	}

	wq_arr_size = num_of_wqs * wq_size;

	/* Config number of WQEs per WQ in SWQ in units of cache line */
	wq_size_cline_log = ilog2(wq_size / NIC_CACHE_LINE_SIZE);

	/* We use the MMU whenever the WQ allocation is more than the 4MB DMA coherent memory
	 * constraint. We need not allocate memory if we are using MMU. We reserve the VA in the
	 * PMMU and allocate the actual memory inside set_req_qp_ctx and map to this virtual address
	 * space.
	 */
	if ((wq_arr_size > DMA_COHERENT_MAX_SIZE) && (in->mem_id == HL_NIC_MEM_HOST)) {
		if (!hdev->mmu_enable) {
			dev_err(hdev->dev,
				"MMU not enabled. For allocations greater than %llx, MMU needs to be enabled, wq_arr_size : 0x%llx, port: %d\n",
				(u64) DMA_COHERENT_MAX_SIZE, wq_arr_size, port);
			return -EINVAL;
		}
		is_mmu_bp = false;

		wq_base_addr = hl_nic_reserve_wq_dva(hdev, ctx, nic_port, wq_arr_size, type);
		if (!wq_base_addr)
			return -ENOMEM;
	} else {
		mem_data.size = wq_arr_size;

		rc = hl_nic_mem_alloc(ctx, &mem_data);
		if (rc) {
			dev_err(hdev->dev, "Failed to allocate WQ: %d\n", rc);
			return rc;
		}

		wq_base_addr = mem_data.addr;
		wq_arr_props->handle = mem_data.handle;
	}

	wq_arr_props->on_device_mem = in->mem_id == HL_NIC_MEM_DEVICE;

	dev_dbg(hdev->dev,
		"port %d: WQ-> type:%u addr=0x%llx log_size:%llu wqe_asid:%u mmu_bp:%u\n",
		port, type, wq_base_addr, wq_size_cline_log, wqe_asid, is_mmu_bp);

	offset = wq_arr_props->idx;

	if (wq_arr_props->is_send) {
		NIC_OFFSET_WREG32(mmD0_NIC0_TXE_SQ_BASE_ADDRESS_63_32_0,
					upper_32_bits(wq_base_addr));
		NIC_OFFSET_WREG32(mmD0_NIC0_TXE_SQ_BASE_ADDRESS_31_0_0,
					lower_32_bits(wq_base_addr));

		NIC_OFFSET_WREG32(mmD0_NIC0_TXE_LOG_MAX_WQ_SIZE_0, wq_size_cline_log);

		/* configure WQ MMU, currently user app has the index of 1 */
		NIC_OFFSET_WREG32(mmD0_NIC0_TXE_WQE_FETCH_AXI_USER_0,
			((is_mmu_bp ? 1 : 0) << NIC_TXE_WQE_FETCH_AXI_USER_MMU_BYPASS_S) |
			(ctx->asid << NIC_TXE_WQE_FETCH_AXI_USER_0_ASID_S));

		/* configure the QPC with the Tx WQ parameters */
		NIC_OFFSET_WREG32(mmD0_NIC0_QPC_TX_WQ_BASE_ADDR_63_32_0,
				upper_32_bits(wq_base_addr));
		NIC_OFFSET_WREG32(mmD0_NIC0_QPC_TX_WQ_BASE_ADDR_31_0_0,
				lower_32_bits(wq_base_addr));
		NIC_OFFSET_WREG32(mmD0_NIC0_QPC_LOG_MAX_TX_WQ_SIZE_0,
				wq_size_cline_log);
		NIC_OFFSET_WREG32(mmD0_NIC0_QPC_AWUSER_ATTR_TX_WQE_0,
				(wqe_asid << D0_NIC0_QPC_AWUSER_ATTR_TX_WQE_0_ASID_S) |
				((is_mmu_bp ? 0x1 : 0)
				<< D0_NIC0_QPC_AWUSER_ATTR_TX_WQE_MMU_BP_S));

		if (gaudi3_nic->advanced) {
			/* rendezvous configuration for send work queue */
			NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RDV_TX_WQ_BASE_ADDR_HI_0,
				upper_32_bits(wq_base_addr));
			NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RDV_TX_WQ_BASE_ADDR_LO_0,
				lower_32_bits(wq_base_addr));
			NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RDV_TX_WQ_LOG_MAX_SIZE_0,
					wq_size_cline_log);
		}
	} else {
		NIC_OFFSET_WREG32(mmD0_NIC0_RXE_WIN_WQ_BASE_HI_0, upper_32_bits(wq_base_addr));
		NIC_OFFSET_WREG32(mmD0_NIC0_RXE_WIN_WQ_BASE_LO_0, lower_32_bits(wq_base_addr));

		NIC_OFFSET_WREG32(mmD0_NIC0_RXE_WIN_WQ_MISC_0, wq_size_cline_log);

		/* configure WQ MMU for RXE */
		NIC_OFFSET_WREG32(mmD0_NIC0_RXE_WQE_ARUSER_ATTR_0,
				wqe_asid << D0_NIC0_RXE_WQE_ARUSER_ATTR_0_ASID_S |
				(is_mmu_bp ? 1 : 0) << D0_NIC0_RXE_WQE_ARUSER_ATTR_MMU_BP_S);

		/* configure the QPC with the Rx WQ parameters */
		NIC_OFFSET_WREG32(mmD0_NIC0_QPC_RX_WQ_BASE_ADDR_63_32_0,
				upper_32_bits(wq_base_addr));
		NIC_OFFSET_WREG32(mmD0_NIC0_QPC_RX_WQ_BASE_ADDR_31_0_0,
				lower_32_bits(wq_base_addr));
		NIC_OFFSET_WREG32(mmD0_NIC0_QPC_LOG_MAX_RX_WQ_SIZE_0,
				wq_size_cline_log);
		NIC_OFFSET_WREG32(mmD0_NIC0_QPC_AWUSER_ATTR_RX_WQE_0,
				wqe_asid << D0_NIC0_QPC_AWUSER_ATTR_RX_WQE_0_ASID_S |
				(is_mmu_bp ? 0x1 : 0)
				<< D0_NIC0_QPC_AWUSER_ATTR_RX_WQE_MMU_BP_S);
	}

	/* DOORBELL_SECURITY is configured per doorbell in gaudi3_db_fifo_set */

	/* We are using a separate flag for wq mmu bypass as the nic->mmu_bypass is being used by
	 * other NIC data structures.
	 */
	nic->wq_mmu_bypass = is_mmu_bp;

	return 0;
}

static int gaudi3_user_wq_arr_unset(struct hl_nic_port *nic_port, u32 type, struct hl_ctx *ctx)
{
	struct hl_wq_array_properties *wq_arr_props = &nic_port->wq_arr_props[type];
	struct gaudi3_nic_port *gaudi3_nic = nic_port->nic_specific;
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, offset;
	int rc = 0;

	offset = wq_arr_props->idx;

	if (wq_arr_props->is_send) {
		if (gaudi3_nic->advanced) {
			/* rendezvous configuration for send work queue */
			NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RDV_TX_WQ_BASE_ADDR_HI_0, 0);
			NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RDV_TX_WQ_BASE_ADDR_LO_0, 0);
			NIC_OFFSET_WREG32(mmD0_NIC0_RXE_RDV_TX_WQ_LOG_MAX_SIZE_0, 0);

			NIC_OFFSET_WREG32(mmD0_NIC0_QPC_TX_WQ_BASE_ADDR_63_32_0, 0);
			NIC_OFFSET_WREG32(mmD0_NIC0_QPC_TX_WQ_BASE_ADDR_31_0_0, 0);
			NIC_OFFSET_WREG32(mmD0_NIC0_QPC_LOG_MAX_TX_WQ_SIZE_0, 0);
			NIC_OFFSET_WREG32(mmD0_NIC0_QPC_AWUSER_ATTR_TX_WQE_0, 0);
		}

		// TODO: find replacement NIC_RMWREG32(mmD0_NIC0_TXE_WQE_USER_CFG, 0, BIT(offset));
		NIC_OFFSET_WREG32(mmD0_NIC0_TXE_WQE_FETCH_AXI_USER_0, 0);
		NIC_OFFSET_WREG32(mmD0_NIC0_TXE_LOG_MAX_WQ_SIZE_0, 0);
		NIC_OFFSET_WREG32(mmD0_NIC0_TXE_SQ_BASE_ADDRESS_63_32_0, 0);
		NIC_OFFSET_WREG32(mmD0_NIC0_TXE_SQ_BASE_ADDRESS_31_0_0, 0);
	} else {
		if (gaudi3_nic->advanced) {
			/*  unconfigure rendezvous for the receive work queue */
			NIC_OFFSET_WREG32(mmD0_NIC0_QPC_RX_WQ_BASE_ADDR_63_32_0, 0);
			NIC_OFFSET_WREG32(mmD0_NIC0_QPC_RX_WQ_BASE_ADDR_31_0_0, 0);
			NIC_OFFSET_WREG32(mmD0_NIC0_QPC_LOG_MAX_RX_WQ_SIZE_0, 0);
			NIC_OFFSET_WREG32(mmD0_NIC0_QPC_AWUSER_ATTR_RX_WQE_0, 0);
		}

		NIC_OFFSET_WREG32(mmD0_NIC0_RXE_WQE_ARUSER_ATTR_0, 0);
		NIC_OFFSET_WREG32(mmD0_NIC0_RXE_WIN_WQ_MISC_0, 0);
		NIC_OFFSET_WREG32(mmD0_NIC0_RXE_WIN_WQ_BASE_HI_0, 0);
		NIC_OFFSET_WREG32(mmD0_NIC0_RXE_WIN_WQ_BASE_LO_0, 0);
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

static void gaudi3_get_cq_id_range(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	*min_id = GAUDI3_MIN_CQ_RDMA_IDX(port);
	*max_id = GAUDI3_MAX_CQ_RDMA_IDX(port);
}

/* Get user CQ UMR block physical base address. */
static u64 get_user_cq_umr_addr(struct hl_nic_port *nic_port, u32 id)
{
	struct asic_fixed_properties *prop = &nic_port->hdev->asic_prop;
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	return prop->cfg_base_address +
		NIC_REG(mmD0_NIC0_CQ_UMR_0_BASE + (id * NIC_CQ_UMR_OFFSET));
}

static int gaudi3_user_cq_set(struct hl_nic_user_cq *user_cq,
				struct hl_nic_user_cq_set_in_params *in,
				struct hl_nic_user_cq_set_out_params *out)
{
	struct hl_nic_port *nic_port = user_cq->nic_port;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_mem_data mem_data = {};
	struct hl_ctx *ctx = user_cq->ctx;
	u64 mem_handle, mem_device_addr, pi_handle, pi_device_addr, umr_block_addr, regs_handle;
	u32 port = nic_port->port, id = user_cq->id, offset = id;
	int rc;

	if (!nic->mmu_bypass) {
		dev_dbg_ratelimited(hdev->dev,
			"PMMU mapping not supported, user CQ %d, port %d\n", id, port);
		return -EOPNOTSUPP;
	}

	/* Allocate all required resources before configuring HW. */

	/* 1. Allocate DMA coherent host memory CQ buffer and get mmap handle. */
	mem_data.mem_id = HL_NIC_DRV_MEM_HOST_DMA_COHERENT;
	mem_data.size = in->num_of_cqes * CQE_SIZE;
	rc = hl_nic_mem_alloc(ctx, &mem_data);
	if (rc) {
		dev_err_ratelimited(hdev->dev,
			"User CQ %d buffer allocation failed, rc %d, port %d\n", id, rc, port);
		return rc;
	}

	mem_handle = mem_data.handle;
	mem_device_addr = mem_data.addr;

	/* 2. Allocate DMA coherent host PI memory and get mmap handle. */
	memset(&mem_data, 0, sizeof(mem_data));
	mem_data.mem_id = HL_NIC_DRV_MEM_HOST_DMA_COHERENT;
	mem_data.size = PAGE_SIZE;
	rc = hl_nic_mem_alloc(ctx, &mem_data);
	if (rc) {
		dev_err_ratelimited(hdev->dev,
			"User CQ %d PI allocation failed, rc %d, port %d\n", id, rc, port);
		goto pi_alloc_fail;
	}

	pi_handle = mem_data.handle;
	pi_device_addr = mem_data.addr;

	/* 3. Get CI UMR mmap handle. */
	umr_block_addr = get_user_cq_umr_addr(nic_port, id);
	rc = hl_get_hw_block_handle(hdev, umr_block_addr, &regs_handle, NULL);
	if (rc) {
		dev_err_ratelimited(hdev->dev,
			"Failed to get user CQ %d UMR handle, rc %d, port %d\n", id, rc, port);
		goto umr_get_fail;
	}

	/* Register CQ to get user EQ events. */
	rc = hl_nic_eq_dispatcher_register_cq(nic_port, ctx->asid, id);
	if (rc) {
		dev_err_ratelimited(hdev->dev,
			"Failed to register CQ %d, rc %d, port %d\n", id, rc, port);
		goto eq_register_fail;
	}

	/* Config HW */

	/* Config user CQ host memory buffer address and size */
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_BASE_ADDR_HI_0, upper_32_bits(mem_device_addr));
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_BASE_ADDR_LO_0, lower_32_bits(mem_device_addr));
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_LOG_SIZE_0, ilog2(in->num_of_cqes));

	/* Config user CQ PI host memory address.
	 * Note: PI memory size is fixed 8 bytes. However, since it's mmaped
	 * to userspace we allocate 1 page.
	 */
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_PI_ADDR_HI_0, upper_32_bits(pi_device_addr));
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_PI_ADDR_LO_0, lower_32_bits(pi_device_addr));

	/* Enable user CQ. */
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_CFG_0,
		D0_NIC0_RXE_CQ_CFG_WRITE_PI_EN_M | D0_NIC0_RXE_CQ_CFG_ENABLE_M |
		(GAUDI3_EQ_RDMA_IDX(port) << D0_NIC0_RXE_CQ_CFG_EQ_ID_S));

	/* Reset HW before user application starts accessing it.
	 * Note: All allocated memory buffers are default zeroed by
	 * NIC memory APIs via gfp flag __GFP_ZERO.
	 */
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_PRODUCER_INDEX_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_CONSUMER_INDEX_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_WRITE_INDEX_0, 0);

	/* Set out params and populate user CQ idr data. */
	out->mem_handle = mem_handle;
	out->regs_handle = regs_handle;
	out->regs_offset = 0; /* CQ CI register in UMR block has offset 0. */
	out->pi_handle = pi_handle;

	user_cq->mem_handle = mem_handle;
	user_cq->pi_handle = pi_handle;

	return 0;

eq_register_fail:
	/* NoOps. Destroy CQ CI UMR mmap handle. */
umr_get_fail:
	hl_nic_mem_destroy(ctx, pi_handle);
pi_alloc_fail:
	hl_nic_mem_destroy(ctx, mem_handle);

	return rc;
}

/* User is done with CQ. Disable user CQ HW. Don't reclaim associated resources yet.
 * We free the resources and clean-up HW once all references to user CQ is released.
 */
static int gaudi3_user_cq_unset(struct hl_nic_user_cq *user_cq)
{
	struct hl_nic_port *nic_port = user_cq->nic_port;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_ctx *ctx = user_cq->ctx;
	u32 port = nic_port->port, id = user_cq->id, offset = id;

	/* User application no more seeks user CQ EQEs. Hence, we unregister user CQ EQEs from user
	 * context.
	 */
	hl_nic_eq_dispatcher_unregister_cq(nic_port, id);

	/* Invalidate user CQ PI. */
	NIC_OFFSET_RMWREG32(mmD0_NIC0_RXE_CQ_CFG_0, 0, D0_NIC0_RXE_CQ_CFG_WRITE_PI_EN_M);
	NIC_OFFSET_RREG32(mmD0_NIC0_RXE_CQ_CFG_0); /* flush */

	hl_nic_mem_destroy(ctx, user_cq->pi_handle);

	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_PI_ADDR_HI_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_PI_ADDR_LO_0, 0);

	return 0;
}

/* All references to user CQ has been released. Free all resources
 * and clean-up the HW.
 */
static void gaudi3_user_cq_destroy(struct hl_nic_user_cq *user_cq)
{
	struct hl_nic_port *nic_port = user_cq->nic_port;
	struct hl_device *hdev = nic_port->hdev;
	struct hl_ctx *ctx = user_cq->ctx;
	u32 port = nic_port->port, id = user_cq->id, offset = id;

	/* Disable user CQ HW. */
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_CFG_0, 0);

	/* Invalidate user CQ memory buffer address. */
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_BASE_ADDR_HI_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_BASE_ADDR_LO_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_RXE_CQ_LOG_SIZE_0, 0);

	/* Destroy memory resources. */
	hl_nic_mem_destroy(ctx, user_cq->mem_handle);
}

static void gaudi3_set_advanced_op_mask(struct hl_device *hdev, bool advanced)
{
	u64 advanced_op_mask = BIT(HL_NIC_OP_USER_CCQ_SET) | BIT(HL_NIC_OP_USER_CCQ_UNSET);

	if (advanced)
		hdev->nic.ctrl_op_mask |= advanced_op_mask;
	else
		hdev->nic.ctrl_op_mask &= ~advanced_op_mask;
}

static int gaudi3_user_set_app_params(struct hl_device *hdev,
					struct hl_nic_set_user_app_params_in *in,
					bool *modify_wqe_checkers, struct hl_ctx *ctx)
{
	u32 port, offset, encap_id, bp_off_num = 0, bp_base_index;
	struct gaudi3_nic_macro *gaudi3_macro;
	struct gaudi3_nic_port *gaudi3_nic;
	struct hl_nic_port *nic_port;
	int i;

	port = in->port;
	nic_port = &hdev->nic.nic_ports[port];
	gaudi3_nic = nic_port->nic_specific;
	gaudi3_nic->advanced = in->advanced;
	gaudi3_macro = nic_port->nic_macro->asic_priv;

	/* Enable\disable advanced operations */
	gaudi3_set_advanced_op_mask(hdev, (bool) gaudi3_nic->advanced);

	*modify_wqe_checkers = false;
	for (i = 0 ; i < HL_NIC_USER_BP_OFFS_MAX ; i++)
		if (in->bp_offs[i])
			bp_off_num++;
		else
			break;

	*modify_wqe_checkers = bp_off_num > 0;

	if (!gaudi3_nic->advanced) {
		if (*modify_wqe_checkers || in->fna_mask_size) {
			dev_dbg(hdev->dev,
				"Port %u: advanced features are set but advanced flag is disabled\n",
				port);
			return -EINVAL;
		}

		dev_info(hdev->dev, "Port %u: Working in Plain RDMA mode\n", port);
		return 0;
	}

	if (in->fna_mask_size) {
		offset = GAUDI3_AFA_IDX(port);
		if (in->fna_fifo_offs[0]) {
			NIC_OFFSET_WREG32(mmD0_NIC0_RXE_AFA_LBW_ADDR_0_0,
				in->fna_fifo_offs[0] & D0_NIC0_RXE_AFA_LBW_ADDR_0_VAL_M);
		}

		if (in->fna_fifo_offs[1]) {
			NIC_OFFSET_WREG32(mmD0_NIC0_RXE_AFA_LBW_ADDR_1_0,
				in->fna_fifo_offs[1] & D0_NIC0_RXE_AFA_LBW_ADDR_1_VAL_M);
		}

		offset = GAUDI3_AFA_MASK_IDX(port);
		NIC_OFFSET_WREG32(mmD0_NIC0_RXE_AFA_MASK_SIZE_0, in->fna_mask_size);

		offset = GAUDI3_AFA_ARUSER_IDX(port);
		NIC_OFFSET_WREG32(mmD0_NIC0_RXE_AFA_ARUSER_ATTR_0,
				ctx->asid << D0_NIC0_RXE_AFA_ARUSER_ATTR_0_ASID_S);
	}

	if (*modify_wqe_checkers) {
		NIC_RMWREG32(mmD0_NIC0_QPC_WTD_CONFIG, 1,
				 D0_NIC0_QPC_WTD_CONFIG_WQ_BP_DB_ACCOUNTED_M);

		bp_base_index = gaudi3_macro->bp_off_num;
		if ((bp_base_index + bp_off_num) > HL_NIC_USER_BP_OFFS_MAX) {
			dev_dbg(hdev->dev,
			"Port %u: too many bp offsets requested. Requested - %d, available %d\n",
			port, bp_off_num, HL_NIC_USER_BP_OFFS_MAX - bp_base_index);
			return -EINVAL;
		}

		for (i = 0 ; i < bp_off_num ; i++) {
			if ((in->bp_offs[i] & ~D0_NIC0_QPC_WQ_BP_ADDR_R_M)) {
				dev_dbg(hdev->dev, "Port %u: bp %u invalid BP offset 0x%x\n",
					port, i, in->bp_offs[i]);
				return -EINVAL;
			}

			NIC_WREG32(mmD0_NIC0_QPC_WQ_BP_ADDR_0 + (i + bp_base_index) * sizeof(u32),
					in->bp_offs[i]);
		}

		gaudi3_macro->bp_off_num += bp_off_num;

		offset = ELEMENT_OFFSET(port, BP_OFFS_MSG_EN_NUM);
		NIC_OFFSET_WREG32(mmD0_NIC0_QPC_WQ_BP_MSG_EN_0,
					(BIT(bp_off_num) - 1) << bp_base_index);
	}

	/* SW-99892 - configure port's encapsulation for source ip-address automatically */
	gaudi3_set_ip_addr_encap(nic_port, &encap_id, 0);

	return 0;
}

static void gaudi3_user_get_app_params(struct hl_device *hdev,
					struct hl_nic_get_user_app_params_in *in,
					struct hl_nic_get_user_app_params_out *out)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct hl_nic_port *nic_port;
	struct gaudi3_nic_port *gaudi3_nic;
	u32 port;

	port = in->port;
	gaudi3_nic = &gaudi3->nic_ports[port];
	nic_port = gaudi3_nic->nic_port;

	out->max_num_of_qps = ELEMENT_COUNT(NIC_MAX_GEN_QP_NUM);
	/* indicates how many QPs are already taken */
	out->num_allocated_qps = RDMA_OFFSET + atomic_read(&nic_port->num_of_allocated_qps);
	out->max_allocated_qp_idx = hl_nic_get_max_qp_id(nic_port);
	out->max_cq_size = sizeof(struct gaudi3_cqe) * NIC_CQ_MAX_ENTRIES;
	out->max_num_of_cqs = NIC_CQS_NUM;
	out->max_num_of_db_fifos = GAUDI3_DB_FIFO_NUM(port);
	out->max_num_of_encaps = GAUDI3_MAX_ENCAP_ID(port) - GAUDI3_MIN_ENCAP_ID(port) + 1;
	out->advanced = gaudi3_nic->advanced;
	out->speed = nic_port->speed;
	out->nic_macro_idx = nic_port->nic_macro->idx;
	out->nic_phys_port_idx = get_lane_offset(gaudi3_nic);
	out->base_coll_qp_idx = GAUDI3_MIN_COLL_QP_ID;
	out->base_scale_out_coll_qp_idx = GAUDI3_MIN_COLL_QP_ID + NIC_MAX_NON_SCALE_OUT_COLL_CONNS;
	out->max_num_of_coll_qps = NIC_MAX_NON_SCALE_OUT_COLL_CONNS;
	out->max_num_of_scale_out_coll_qps = ELEMENT_COUNT(NIC_MAX_COLL_QP_NUM) -
						NIC_MAX_NON_SCALE_OUT_COLL_CONNS;
	out->coll_qps_offset = ELEMENT_OFFSET(port, NIC_MAX_COLL_QP_NUM);
}

static u32 gaudi3_nic_get_default_port_speed(struct hl_device *hdev)
{
	return is_400g_mode(hdev) ? SPEED_400000 : SPEED_200000;
}

static int gaudi3_nic_alloc_ring(struct gaudi3_nic_port *gaudi3_nic,
			struct hl_nic_ring *ring, int elem_size, int count)
{
	struct hl_device *hdev = gaudi3_nic->hdev;
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

static void gaudi3_nic_free_ring(struct gaudi3_nic_port *gaudi3_nic, struct hl_nic_ring *ring)
{
	struct hl_device *hdev = gaudi3_nic->hdev;

	hl_asic_dma_pool_free(hdev, RING_PI_ADDRESS(ring), RING_PI_DMA_ADDRESS(ring));

	hl_asic_dma_free_coherent(hdev, RING_BUF_SIZE(ring), RING_BUF_ADDRESS(ring),
					RING_BUF_DMA_ADDRESS(ring));
}

static int gaudi3_nic_alloc_cq_rings(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_device *hdev = gaudi3_nic->hdev;
	struct hl_nic_ring *ring;
	u32 elem_size, queue_size, total_queues_size, count;
	void *cpu_addr;
	dma_addr_t dma_addr;
	int rc, i;

	elem_size = sizeof(struct gaudi3_cqe);
	count = NIC_CQ_MAX_ENTRIES;
	queue_size = elem_size * count;
	total_queues_size = queue_size * NIC_CQS_NUM;

	/*
	 * The HW expects that all CQs will be located in a physically consecutive memory one after
	 * the other. Hence we allocate all of them in one chunk.
	 */
	cpu_addr = hl_asic_dma_alloc_coherent(hdev, total_queues_size, &dma_addr, GFP_KERNEL);
	if (!cpu_addr)
		return -ENOMEM;

	for (i = 0 ; i < NIC_CQS_NUM ; i++) {
		ring = &gaudi3_nic->cq_rings[i];
		RING_BUF_ADDRESS(ring) = cpu_addr + i * queue_size;
		RING_BUF_DMA_ADDRESS(ring) = dma_addr + i * queue_size;
		/* prevent freeing memory fragments by individual Qs */
		RING_BUF_SIZE(ring) = i ? 0 : total_queues_size;
		ring->count = count;
		ring->elem_size = elem_size;
		ring->asid = HL_KERNEL_ASID_ID;
	}

	for (i = 0 ; i < NIC_CQS_NUM ; i++) {
		ring = &gaudi3_nic->cq_rings[i];
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
		ring = &gaudi3_nic->cq_rings[i];
		hl_asic_dma_pool_free(hdev, RING_PI_ADDRESS(ring), RING_PI_DMA_ADDRESS(ring));
	}

	/* free rings memory */
	hl_asic_dma_free_coherent(hdev, total_queues_size, cpu_addr, dma_addr);

	return rc;
}

static void gaudi3_nic_free_cq_rings(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_device *hdev = gaudi3_nic->hdev;
	struct hl_nic_ring *ring;
	int i;

	for (i = 0 ; i < NIC_CQS_NUM ; i++) {
		ring = &gaudi3_nic->cq_rings[i];
		hl_asic_dma_pool_free(hdev, RING_PI_ADDRESS(ring), RING_PI_DMA_ADDRESS(ring));
	}

	/* the entire CQs memory is allocated as one chunk and stored at index 0 */
	ring = &gaudi3_nic->cq_rings[0];
	hl_asic_dma_free_coherent(hdev, RING_BUF_SIZE(ring), RING_BUF_ADDRESS(ring),
					RING_BUF_DMA_ADDRESS(ring));
}

static int gaudi3_nic_alloc_rings_resources(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_device *hdev = gaudi3_nic->hdev;
	int rc;

	rc = gaudi3_nic_alloc_ring(gaudi3_nic, &gaudi3_nic->fifo_ring,
					ALIGN(sizeof(u32), NIC_CACHE_LINE_SIZE), 1);
	if (rc) {
		dev_err(hdev->dev, "Failed to allocate fifo ring\n");
		return rc;
	}

	rc = gaudi3_nic_alloc_ring(gaudi3_nic, &gaudi3_nic->rx_ring, NIC_RAW_ELEM_SIZE,
					NIC_RX_RING_PKT_NUM);
	if (rc) {
		dev_err(hdev->dev, "Failed to allocate RX ring\n");
		goto err_rx_ring;
	}

	rc = gaudi3_nic_alloc_ring(gaudi3_nic, &gaudi3_nic->wq_ring, sizeof(struct gaudi3_sq_wqe),
					QP_WQE_NUM_REC);
	if (rc) {
		dev_err(hdev->dev, "Failed to allocate WQ ring\n");
		goto err_wq_ring;
	}

	rc = gaudi3_nic_alloc_ring(gaudi3_nic, &gaudi3_nic->eq_ring,
					sizeof(struct hl_nic_eqe),
					NIC_EQ_RING_NUM_REC);
	if (rc) {
		dev_err(hdev->dev, "Failed to allocate EQ ring\n");
		goto err_eq_ring;
	}

	rc = gaudi3_nic_alloc_cq_rings(gaudi3_nic);
	if (rc) {
		dev_err(hdev->dev, "Failed to allocate CQ rings\n");
		goto err_cq_rings;
	}

	return 0;

err_cq_rings:
	gaudi3_nic_free_ring(gaudi3_nic, &gaudi3_nic->eq_ring);
err_eq_ring:
	gaudi3_nic_free_ring(gaudi3_nic, &gaudi3_nic->wq_ring);
err_wq_ring:
	gaudi3_nic_free_ring(gaudi3_nic, &gaudi3_nic->rx_ring);
err_rx_ring:
	gaudi3_nic_free_ring(gaudi3_nic, &gaudi3_nic->fifo_ring);

	return rc;
}

static void gaudi3_nic_free_rings_resources(struct gaudi3_nic_port *gaudi3_nic)
{
	gaudi3_nic_free_cq_rings(gaudi3_nic);
	gaudi3_nic_free_ring(gaudi3_nic, &gaudi3_nic->eq_ring);
	gaudi3_nic_free_ring(gaudi3_nic, &gaudi3_nic->wq_ring);
	gaudi3_nic_free_ring(gaudi3_nic, &gaudi3_nic->rx_ring);
	gaudi3_nic_free_ring(gaudi3_nic, &gaudi3_nic->fifo_ring);
}

static void gaudi3_nic_reset_rings(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_nic_ring *cq_ring;

	/* Reset CQ ring HW PI and shadow PI/CI */
	cq_ring = &gaudi3_nic->cq_rings[RDMA_OFFSET];
	*((u32 *) RING_PI_ADDRESS(cq_ring)) = 0;
	cq_ring->pi_shadow = 0;
	cq_ring->ci_shadow = 0;

	gaudi3_nic_eq_reset_ring(gaudi3_nic);
}

static void free_no_dram_port_resources(struct gaudi3_nic_port *gaudi3_nic)
{
	struct hl_device *hdev = gaudi3_nic->hdev;

	if (gaudi3_nic->txs_mem.addr) {
		hl_asic_dma_free_coherent(hdev, gaudi3_nic->txs_mem.size,
						gaudi3_nic->txs_mem.addr,
						gaudi3_nic->txs_mem.dma_addr);
		memset(&gaudi3_nic->txs_mem, 0, sizeof(gaudi3_nic->txs_mem));
	}

	if (gaudi3_nic->req_qpc_mem.addr) {
		hl_asic_dma_free_coherent(hdev, gaudi3_nic->req_qpc_mem.size,
						gaudi3_nic->req_qpc_mem.addr,
						gaudi3_nic->req_qpc_mem.dma_addr);
		memset(&gaudi3_nic->req_qpc_mem, 0, sizeof(gaudi3_nic->req_qpc_mem));
	}

	if (gaudi3_nic->res_qpc_mem.addr) {
		hl_asic_dma_free_coherent(hdev, gaudi3_nic->res_qpc_mem.size,
						gaudi3_nic->res_qpc_mem.addr,
						gaudi3_nic->res_qpc_mem.dma_addr);
		memset(&gaudi3_nic->res_qpc_mem, 0, sizeof(gaudi3_nic->res_qpc_mem));
	}

	if (gaudi3_nic->req_qpc_swl_mem.addr) {
		hl_asic_dma_free_coherent(hdev, gaudi3_nic->req_qpc_swl_mem.size,
						gaudi3_nic->req_qpc_swl_mem.addr,
						gaudi3_nic->req_qpc_swl_mem.dma_addr);
		memset(&gaudi3_nic->req_qpc_swl_mem, 0, sizeof(gaudi3_nic->req_qpc_swl_mem));
	}
}

static int alloc_no_dram_port_resources(struct gaudi3_nic_port *gaudi3_nic)
{
	void *cpu_addr;
	dma_addr_t dma_handle;
	struct hl_nic_properties *nic_prop;
	struct hl_device *hdev;

	hdev = gaudi3_nic->hdev;
	nic_prop = &hdev->asic_prop.nic_props;

	/* 1. Allocate host memory for TX scheduler. */

	cpu_addr = hl_asic_dma_alloc_coherent(hdev, nic_prop->txs_base_size, &dma_handle,
						GFP_KERNEL | __GFP_ZERO);
	if (!cpu_addr)
		goto err;

	gaudi3_nic->txs_mem.addr = cpu_addr;
	gaudi3_nic->txs_mem.dma_addr = dma_handle;
	gaudi3_nic->txs_mem.size = nic_prop->txs_base_size;

	/* 2. Allocate host memory for requester QPC. */

	cpu_addr = hl_asic_dma_alloc_coherent(hdev, nic_prop->req_qpc_base_size, &dma_handle,
						GFP_KERNEL | __GFP_ZERO);
	if (!cpu_addr)
		goto err;

	gaudi3_nic->req_qpc_mem.addr = cpu_addr;
	gaudi3_nic->req_qpc_mem.dma_addr = dma_handle;
	gaudi3_nic->req_qpc_mem.size = nic_prop->req_qpc_base_size;

	/* 3. Allocate host memory for responder QPC. */

	cpu_addr = hl_asic_dma_alloc_coherent(hdev, nic_prop->res_qpc_base_size, &dma_handle,
						GFP_KERNEL | __GFP_ZERO);
	if (!cpu_addr)
		goto err;

	gaudi3_nic->res_qpc_mem.addr = cpu_addr;
	gaudi3_nic->res_qpc_mem.dma_addr = dma_handle;
	gaudi3_nic->res_qpc_mem.size = nic_prop->res_qpc_base_size;

	/* 4. Allocate host memory for requester QPC SWL. */

	cpu_addr = hl_asic_dma_alloc_coherent(hdev, nic_prop->req_qpc_swl_base_size, &dma_handle,
						GFP_KERNEL | __GFP_ZERO);
	if (!cpu_addr)
		goto err;

	gaudi3_nic->req_qpc_swl_mem.addr = cpu_addr;
	gaudi3_nic->req_qpc_swl_mem.dma_addr = dma_handle;
	gaudi3_nic->req_qpc_swl_mem.size = nic_prop->req_qpc_swl_base_size;

	return 0;
err:
	free_no_dram_port_resources(gaudi3_nic);
	return -ENOMEM;
}

static void gaudi3_nic_port_sw_fini(struct hl_nic_port *nic_port)
{
	struct gaudi3_nic_port *gaudi3_nic = nic_port->nic_specific;

	hl_nic_eq_dispatcher_fini(nic_port);
	gaudi3_nic_free_rings_resources(gaudi3_nic);

	if (!nic_port->hdev->dram_enable)
		free_no_dram_port_resources(gaudi3_nic);
}

static int gaudi3_nic_port_sw_init(struct hl_nic_port *nic_port)
{
	struct hl_wq_array_properties *wq_arr_props;
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, macro_idx;
	struct gaudi3_nic_port *gaudi3_nic;
	struct gaudi3_device *gaudi3;
	int rc;

	gaudi3 = hdev->asic_specific;
	gaudi3_nic = &gaudi3->nic_ports[port];
	gaudi3_nic->hdev = hdev;
	gaudi3_nic->port = port;
	gaudi3_nic->nic_port = nic_port;
	nic_port->nic_specific = gaudi3_nic;
	wq_arr_props = nic_port->wq_arr_props;

	macro_idx = (hdev->nic_lanes_per_port == PORT_LANES_4) ? port : (port >> 1);
	nic_port->nic_macro = &hdev->nic.nic_macros[macro_idx];

	/* Set the index for each WQ array type */
	wq_arr_props[HL_NIC_USER_WQ_SEND].idx = GAUDI3_TXE_WQ_RDMA_IDX(port);
	wq_arr_props[HL_NIC_USER_WQ_RECV].idx = GAUDI3_RXE_WQ_RDMA_IDX(port);
	wq_arr_props[HL_NIC_USER_COLL_WQ_SEND].idx = GAUDI3_TXE_COLL_WQ_RDMA_IDX(port);
	wq_arr_props[HL_NIC_USER_COLL_WQ_RECV].idx = GAUDI3_RXE_COLL_WQ_RDMA_IDX(port);
	wq_arr_props[HL_NIC_USER_COLL_SCALE_OUT_WQ_SEND].idx =
						GAUDI3_TXE_COLL_SCALE_OUT_WQ_RDMA_IDX(port);
	wq_arr_props[HL_NIC_USER_COLL_SCALE_OUT_WQ_RECV].idx =
						GAUDI3_RXE_COLL_SCALE_OUT_WQ_RDMA_IDX(port);

	rc = gaudi3_nic_alloc_rings_resources(gaudi3_nic);
	if (rc) {
		dev_err(hdev->dev, "Failed to alloc rings, port: %d, %d\n", port, rc);
		return rc;
	}

	if (!hdev->dram_enable) {
		rc = alloc_no_dram_port_resources(gaudi3_nic);
		if (rc) {
			dev_err(hdev->dev,
			"Failed to allocate port %d host memory to emulate DRAM\n", port);
			goto free_rings;
		}
	}

	hl_nic_eq_dispatcher_init(gaudi3_nic->nic_port);

	return 0;

free_rings:
	gaudi3_nic_free_rings_resources(gaudi3_nic);

	return rc;
}

static void free_no_dram_macro_resources(struct gaudi3_nic_macro *gaudi3_macro)
{
	struct hl_device *hdev = gaudi3_macro->hdev;

	if (gaudi3_macro->tmr_mem.addr) {
		hl_asic_dma_free_coherent(hdev, gaudi3_macro->tmr_mem.size,
						gaudi3_macro->tmr_mem.addr,
						gaudi3_macro->tmr_mem.dma_addr);
		memset(&gaudi3_macro->tmr_mem, 0, sizeof(gaudi3_macro->tmr_mem));
	}
}

static int alloc_no_dram_macro_resources(struct gaudi3_nic_macro *gaudi3_macro)
{
	void *cpu_addr;
	dma_addr_t dma_handle;
	struct hl_nic_properties *nic_prop;
	struct hl_device *hdev;

	hdev = gaudi3_macro->hdev;
	nic_prop = &hdev->asic_prop.nic_props;

	/* Allocate host memory for Timer. */

	cpu_addr = hl_asic_dma_alloc_coherent(hdev, nic_prop->tmr_base_size, &dma_handle,
						GFP_KERNEL | __GFP_ZERO);
	if (!cpu_addr)
		return -ENOMEM;

	gaudi3_macro->tmr_mem.addr = cpu_addr;
	gaudi3_macro->tmr_mem.dma_addr = dma_handle;
	gaudi3_macro->tmr_mem.size = nic_prop->tmr_base_size;

	return 0;
}

static int gaudi3_nic_macro_sw_init(struct hl_nic_macro *nic_macro)
{
	struct hl_device *hdev;
	struct gaudi3_device *gaudi3;
	struct gaudi3_nic_macro *gaudi3_macro;
	int rc;

	hdev = nic_macro->hdev;
	gaudi3 = hdev->asic_specific;
	gaudi3_macro = &gaudi3->nic_macros[nic_macro->idx];

	gaudi3_macro->hdev = hdev;
	gaudi3_macro->nic_macro = nic_macro;

	nic_macro->asic_priv = gaudi3_macro;

	if (!hdev->dram_enable) {
		rc = alloc_no_dram_macro_resources(gaudi3_macro);
		if (rc) {
			dev_err(hdev->dev,
			"Failed to allocate macro %d host memory to emulate DRAM\n",
			nic_macro->idx);
			return -ENOMEM;
		}
	}

	mutex_init(&gaudi3_macro->cfg_lock);

	/* In Gaudi3, the DB FIFO is a generic FIFO which is used for various other purposes
	 * like Direct WQ, Collective OPS and so on. Each use case can have different FIFO size
	 * requirements. We have a total size of 4KB FIFO and a total of 24 FIFOs, so this has to
	 * be managed by the driver. To do that, we are using the gen pool subsystem of linux as
	 * follows:
	 * --> Create a gen pool with basic granularity of allocation.
	 * --> Set the algorithm of allocation to best fit as we need to manage a stringent 4KB
	 * --> Add a memory to the gen pool with some random address. Here we choose 1000. Genpool
	 *     APIs manages a memory chunk with a given base address and size.
	 *     It does not try to access that particular address at any point. Hence it is safe
	 *     to give any random address.
	 * --> Whenever there is an allocation request, call gen_pool_alloc to get the free memory
	 * --> Find out the fifo offset that we need to provide to the user by doing
	 *     (gen pool offset - 1000).
	 */
	gaudi3_macro->db_fifo_pool = gen_pool_create(ilog2(DB_FIFO_MIN_GRANULARITY), -1);
	if (!gaudi3_macro->db_fifo_pool) {
		dev_err(hdev->dev, "Failed to create gen_pool to manage db fifo\n");
		rc = -ENOMEM;
		goto gen_pool_create_fail;
	}

	gen_pool_set_algo(gaudi3_macro->db_fifo_pool, gen_pool_best_fit, NULL);

	/* Here we could have even passed 0 as address. But the gen_pool_alloc API returns
	 * '0' if it cannot find a free memory. So we cannot tell the difference between a valid
	 * address '0' or a failure to find free memory.
	 */
	gaudi3_macro->db_fifo_start_addr = 1000;
	rc = gen_pool_add(gaudi3_macro->db_fifo_pool, gaudi3_macro->db_fifo_start_addr,
				DB_FIFO_SIZE, -1);
	if (rc) {
		dev_err(hdev->dev, "Failed adding memory to db fifo gen pool\n");
		goto gen_pool_add_fail;
	}

	gaudi3_macro->bp_off_num = 0;

	return 0;

gen_pool_add_fail:
	gen_pool_destroy(gaudi3_macro->db_fifo_pool);
gen_pool_create_fail:
	mutex_destroy(&gaudi3_macro->cfg_lock);
	if (!hdev->dram_enable)
		free_no_dram_macro_resources(gaudi3_macro);

	return rc;
}

static void gaudi3_nic_macro_sw_fini(struct hl_nic_macro *nic_macro)
{
	struct gaudi3_nic_macro *gaudi3_macro = nic_macro->asic_priv;

	mutex_destroy(&gaudi3_macro->cfg_lock);

	if (!nic_macro->hdev->dram_enable)
		free_no_dram_macro_resources(gaudi3_macro);

	/* We can safely assume that when we reach SW fini, gen pool is all free. It's a bug if
	 * allocations are not removed yet
	 */
	gen_pool_destroy(gaudi3_macro->db_fifo_pool);
}

static int gaudi3_nic_ctx_dispatcher_init(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_nic_port *gaudi3_nic;
	struct hl_nic_port *nic_port;
	u32 port;
	int i, j, rc = 0;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		gaudi3_nic = &(gaudi3->nic_ports[i]);
		nic_port = gaudi3_nic->nic_port;
		port = nic_port->port;

		rc = hl_nic_eq_dispatcher_associate_dq(nic_port, ctx->asid);
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
		gaudi3_nic = &(gaudi3->nic_ports[j]);
		hl_nic_eq_dispatcher_dissociate_dq(gaudi3_nic->nic_port, ctx->asid);
	}

	return rc;
}

static void gaudi3_nic_ctx_dispatcher_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_nic_port *gaudi3_nic;
	int i;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++)
		if (hdev->nic_ports_mask & BIT(i)) {
			gaudi3_nic = &(gaudi3->nic_ports[i]);
			hl_nic_eq_dispatcher_dissociate_dq(gaudi3_nic->nic_port, ctx->asid);
		}
}

static void gaudi3_nic_reset_macro_params(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct gaudi3_nic_macro *gaudi3_macro;
	struct hl_nic_macro *nic_macro;
	int i;

	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		nic_macro = &hdev->nic.nic_macros[i];

		if (hdev->nic_ports_mask & gaudi3_nic_get_macro_ports_mask(nic_macro)) {
			gaudi3_macro = nic_macro->asic_priv;
			gaudi3_macro->bp_off_num = 0;
		}
	}
}

static int gaudi3_nic_ctx_init(struct hl_ctx *ctx)
{
	return gaudi3_nic_ctx_dispatcher_init(ctx);
}

static void gaudi3_nic_ctx_fini(struct hl_ctx *ctx)
{
	gaudi3_nic_ctx_dispatcher_fini(ctx);
	hl_nic_user_db_fifo_ctx_destroy(ctx);

	gaudi3_nic_reset_macro_params(ctx);
}

static int gaudi3_nic_kernel_ctx_init(struct hl_ctx *ctx)
{
	return gaudi3_nic_ctx_init(ctx);
}

static void gaudi3_nic_kernel_ctx_fini(struct hl_ctx *ctx)
{
	gaudi3_nic_ctx_fini(ctx);
}

static void gaudi3_nic_configure_cq(struct hl_aux_dev *aux_dev, u32 port,
				    u32 coalesce_usec, bool enable)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct hl_nic_properties *nic_prop = &hdev->asic_prop.nic_props;
	u32 cqn, arm_timeout;

	/* Calc timeout in ticks.
	 * result/value of 0 is interpreted as ASAP but since a value of Zero is an invalid value
	 * we modify it to 1.
	 */
	arm_timeout = lower_32_bits((nic_prop->clk * coalesce_usec) / SZ_1K);
	nic->cq_arm_timeout = arm_timeout ? arm_timeout : 1;
	/* TODO: SW-67924 */
	cqn = GAUDI3_CQ_RAW_IDX(port);

	/* disable the current timer before configuring the new time */
	NIC_RMWREG32(mmD0_NIC0_RXE_CQ_ARM_TIMEOUT_EN, 0, BIT(cqn));
	NIC_RREG32(mmD0_NIC0_RXE_CQ_ARM_TIMEOUT_EN);

	/* if enable - configure the new timer and enable it */
	if (enable) {
		NIC_WREG32(mmD0_NIC0_RXE_CQ_ARM_TIMEOUT, hdev->nic.cq_arm_timeout);
		NIC_RMWREG32(mmD0_NIC0_RXE_CQ_ARM_TIMEOUT_EN, 1, BIT(cqn));
	}
}

static void gaudi3_nic_arm_cq(struct hl_aux_dev *aux_dev, u32 port, u32 index)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	u32 offset = GAUDI3_CQ_RAW_IDX(port);

	NIC_OFFSET_WREG32_AUX(mmD0_NIC0_CQ_UMR_0_ARM_INDEX, index, NIC_CQ_UMR_OFFSET);
}

static void gaudi3_nic_write_rx_ci(struct hl_aux_dev *aux_dev, u32 port, u32 ci)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	u32 offset = GAUDI3_CQ_RAW_IDX(port);

	/* TODO: SW-67924 */
	NIC_OFFSET_WREG32_AUX(mmD0_NIC0_CQ_UMR_0_CONSUMER_INDEX, ci, NIC_CQ_UMR_OFFSET);
}

static void gaudi3_nic_get_pfc_cnts(struct hl_aux_dev *aux_dev, u32 port, int pfc_prio,
					u64 *indications, u64 *requests)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	u64 reg_addr, lo_part, hi_part;
	u32 offset;

	offset = ELEMENT_OFFSET(port, MAC_GLOB_STAT_RX_NUM);
	reg_addr = mmD0_NIC0_MAC_GLOB_STAT_RX0_BASE +
			mmNIC_MAC_GLOB_STAT_RX0_ACBFCPAUSEFRAMESRECEIVED0;

	reg_addr += (4 * pfc_prio);

	lo_part = NIC_OFFSET_RREG32(reg_addr);
	hi_part = NIC_RREG32(mmD0_NIC0_MAC_GLOB_STAT_CONTROL_REG_DATA_HI);
	*indications = lo_part | (hi_part << 32);

	offset = ELEMENT_OFFSET(port, MAC_GLOB_STAT_TX_NUM);
	reg_addr = mmD0_NIC0_MAC_GLOB_STAT_TX0_BASE +
			mmNIC_MAC_GLOB_STAT_TX0_ACBFCPAUSEFRAMESTRANSMITTED0;

	reg_addr += (4 * pfc_prio);

	lo_part = NIC_OFFSET_RREG32(reg_addr);
	hi_part = NIC_RREG32(mmD0_NIC0_MAC_GLOB_STAT_CONTROL_REG_DATA_HI);
	*requests = lo_part | (hi_part << 32);
}

static int hl_nic_ring_tx_doorbell(struct hl_aux_dev *aux_dev, u32 port, u32 pi)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);
	struct gaudi3_nic_port *gaudi3_nic;
	struct hl_nic_port *nic_port;
	u32 db_fifo_ci = 0, db_fifo_pi = 0, space_left_in_db_fifo = 0;
	u32 offset = GAUDI3_DB_FIFO_RAW_IDX(port);

	nic_port = &nic->nic_ports[port];
	gaudi3_nic = nic_port->nic_specific;

	db_fifo_ci = *((u32 *) RING_CI_ADDRESS(&gaudi3_nic->fifo_ring));
	db_fifo_pi = gaudi3_nic->db_fifo_pi;

	/* TODO: SW-82278 Utilize fifo space calculation to use all entries */
	space_left_in_db_fifo = ((db_fifo_pi >= db_fifo_ci) ?
				(NIC_FIFO_DB_SIZE - (db_fifo_pi - db_fifo_ci)) :
						(db_fifo_ci - db_fifo_pi)) - 1;

	/* PSB explanation for this condition */
	if (space_left_in_db_fifo < 2) {
		dev_dbg_ratelimited(hdev->dev, "port %d DB fifo full. PI %d, CI %d\n",
				    port, db_fifo_pi, db_fifo_ci);
		return -EBUSY;
	}

	/* TODO: SW-67924 */
	NIC_OFFSET_WREG32_AUX(mmD0_NIC0_UMR_0_REG_DW_0, pi, NIC_UMR_OFFSET);
	NIC_OFFSET_WREG32_AUX(mmD0_NIC0_UMR_0_REG_DW_1, RAW_QPN(port), NIC_UMR_OFFSET);

	/*
	 * Due to HW change in Gaudi3, in which, the HW works in 4 bytes granularity,
	 * and each DB packet is 8 bytes, we increment the local PI in steps of 2 to match
	 * with CI. Moreover, in Gaudi3 the CI is a free run counter, hence PI should wrap on
	 * the same counter size.
	 */
	gaudi3_nic->db_fifo_pi = (gaudi3_nic->db_fifo_pi + 2) & (DB_FIFO_CI_COUNTER_SIZE - 1);

	return 0;
}

static dma_addr_t gaudi3_nic_dma_map_tx_pkt(struct hl_aux_dev *aux_dev, void *addr, int len)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	return hdev->asic_funcs->asic_dma_map_single(hdev, addr, len, DMA_TO_DEVICE);
}

static void gaudi3_nic_dma_unmap_tx_pkt(struct hl_aux_dev *aux_dev, dma_addr_t dma_addr, int len)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_device *hdev = container_of(nic, struct hl_device, nic);

	hdev->asic_funcs->asic_dma_unmap_single(hdev, dma_addr, len, DMA_TO_DEVICE);
}

static int db_fifo_reset(struct hl_device *hdev, u32 port, u32 offset)
{
	u32 cfg2_val, temp;

	/* no need to reset the DB FIFO, as the hard will reset itself. */
	if (hdev->reset_info.hard_reset_pending)
		return 0;

	cfg2_val = NIC_OFFSET_RREG32(mmD0_NIC0_QPC_DB_FIFO_CFG2_0);

	/* Set CI clear/reset bit. */
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_CFG2_0, cfg2_val | D0_NIC0_QPC_DB_FIFO_CFG2_CLR_M);

	/* Poll for CI reset to complete. */
	return hl_poll_timeout(hdev, NIC_REG(mmD0_NIC0_QPC_DB_FIFO_CFG2_0 + db_fifo_offset(offset)),
				temp, temp == cfg2_val, 1000, HL_DEVICE_TIMEOUT_USEC);
}

static void gaudi3_nic_en_db_fifo_reset(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_nic *nic = container_of(aux_dev, struct hl_nic, en_aux_dev);
	struct hl_nic_port *nic_port = &nic->nic_ports[port];
	struct gaudi3_nic_port *gaudi3_nic = nic_port->nic_specific;
	struct hl_device *hdev = gaudi3_nic->hdev;
	u32 offset;
	int rc;

	offset = GAUDI3_DB_FIFO_RAW_IDX(port);

	rc = db_fifo_reset(hdev, port, offset);
	if (rc) {
		dev_err(hdev->dev, "Port %d user doorbell %d fifo reset timed out, %d\n",
					port, offset, rc);
		return;
	}

	/* If register reset succeed we should flush the RAW PI and CI counters we have on host
	 * otherwise on next DB packet we will read old CI value.
	 */
	gaudi3_nic->db_fifo_pi = 0;
	*((u32 *) RING_CI_ADDRESS(&gaudi3_nic->fifo_ring)) = 0;
}

static void gaudi3_nic_db_fifo_reset(struct hl_nic_port *nic_port, struct hl_ctx *ctx,
				u32 id, u64 ci_mmap_handle)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_mmap_mem_buf *buf;
	struct hl_nic_mem *mem;
	u32 port = nic_port->port, offset = id;
	int rc;

	buf = hl_mmap_mem_buf_get(&ctx->hpriv->mem_mgr, ci_mmap_handle);
	if (!buf) {
		dev_err(hdev->dev, "Failed to retrieve port %d db fifo CI memory\n", port);
		return;
	}

	mem = buf->private;

	rc = db_fifo_reset(hdev, port, offset);
	if (rc)
		dev_err(hdev->dev, "Port %d user doorbell %d fifo CI %d reset timed out, %d\n",
			port, id, *((u32 *) mem->kernel_address), rc);

	hl_mmap_mem_buf_put(buf);
}

static int gaudi3_nic_sw_init(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_en_aux_ops *aux_ops = &gaudi3->en_aux_ops;
	struct gaudi3_en_core_info *core_info = &gaudi3->en_core_info;
	struct hl_nic_ring **rx_rings, **cq_rings, **wq_rings;
	struct gaudi3_en_port_info *port_info;
	struct hl_nic *nic = &hdev->nic;
	int rc, i;

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

	port_info = kcalloc(NIC_NUMBER_OF_PORTS, sizeof(*port_info), GFP_KERNEL);
	if (!port_info) {
		rc = -ENOMEM;
		goto port_info_fail;
	}

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		port_info[i].qpn = RAW_QPN(i);
		port_info[i].cqn = GAUDI3_CQ_RAW_IDX(i);
		port_info[i].eqn = GAUDI3_EQ_RAW_IDX(i);
		port_info[i].wqn = GAUDI3_TXE_WQ_RAW_IDX(i);
		port_info[i].lane = __get_lane_offset(hdev, i);
	}

	core_info->rx_rings = rx_rings;
	core_info->cq_rings = cq_rings;
	core_info->wq_rings = wq_rings;
	core_info->port_info = port_info;
	core_info->kernel_asid = HL_KERNEL_ASID_ID;
	core_info->tx_ring_len = NIC_TX_BUF_SIZE;

	aux_ops->configure_cq = gaudi3_nic_configure_cq;
	aux_ops->arm_cq = gaudi3_nic_arm_cq;
	aux_ops->write_rx_ci = gaudi3_nic_write_rx_ci;
	aux_ops->get_pfc_cnts = gaudi3_nic_get_pfc_cnts;
	aux_ops->ring_tx_doorbell = hl_nic_ring_tx_doorbell;
	aux_ops->dma_map_tx_pkt = gaudi3_nic_dma_map_tx_pkt;
	aux_ops->dma_unmap_tx_pkt = gaudi3_nic_dma_unmap_tx_pkt;
	aux_ops->qp_err_syndrom_to_str = gaudi3_nic_qp_err_syndrom_to_str;
	aux_ops->qp_err_src_to_str = gaudi3_nic_qp_err_src_to_str;
	aux_ops->db_fifo_reset = gaudi3_nic_en_db_fifo_reset;
	aux_ops->txs_get_schedq_num = gaudi3_nic_txs_get_schedq_num;

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
			BIT(HL_NIC_OP_ALLOC_COLL_CONN);

	/* Since in Gaudi3 the port level lock is actually a macro level lock, we must not lock
	 * both ports of a macro when acquiring the cfg lock for all the ports, otherwise we'll
	 * get a deadlock.
	 */
	nic->skip_odd_ports_cfg_lock = is_200g_mode(hdev);
	nic->ib_support = true;

	return 0;

port_info_fail:
	kfree(wq_rings);
qp_rings_fail:
	kfree(cq_rings);
cq_rings_fail:
	kfree(rx_rings);

	return rc;
}

static void gaudi3_nic_sw_fini(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_en_core_info *core_info = &gaudi3->en_core_info;

	kfree(core_info->port_info);
	kfree(core_info->wq_rings);
	kfree(core_info->cq_rings);
	kfree(core_info->rx_rings);
}

static void gaudi3_nic_en_set_core_data(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_en_core_info *asic_core_info;
	struct gaudi3_nic_port *gaudi3_port;
	struct hl_en_core_info *core_info;
	struct hl_nic *nic = &hdev->nic;
	struct hl_en_aux_ops *aux_ops;
	struct hl_nic_port *nic_port;
	struct hl_aux_dev *aux_dev;
	int i;

	aux_dev = &nic->en_aux_dev;
	core_info = aux_dev->core_info;
	asic_core_info = &gaudi3->en_core_info;
	core_info->asic_specific = asic_core_info;
	aux_ops = aux_dev->aux_ops;
	aux_ops->asic_ops = &gaudi3->en_aux_ops;

	for (i = 0 ; i < NIC_NUMBER_OF_PORTS ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];
		gaudi3_port = nic_port->nic_specific;

		if (nic_port->eth_enable) {
			asic_core_info->rx_rings[i] = &gaudi3_port->rx_ring;
			asic_core_info->cq_rings[i] = &gaudi3_port->cq_rings[GAUDI3_RAW_OFFSET];
			asic_core_info->wq_rings[i] = &gaudi3_port->wq_ring;
		}
	}
}

static int gaudi3_register_qp(struct hl_nic_port *nic_port, u32 qp_id, u32 asid)
{
	return hl_nic_eq_dispatcher_register_qp(nic_port, asid, qp_id);
}

static void gaudi3_unregister_qp(struct hl_nic_port *nic_port, u32 qp_id)
{
	hl_nic_eq_dispatcher_unregister_qp(nic_port, qp_id);
}

static void gaudi3_get_qp_id_range(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	/* TODO: SW-67924: Manage all QPs via resource manager */
	*min_id = GAUDI3_MIN_QP_ID(port);
	*max_id = GAUDI3_MAX_QP_ID(port);
}

static u8 gaudi3_qp_event_is_req_event(struct hl_nic_eqe *eqe)
{
	char synd_str_to_lower[GAUDI3_MAX_SYNDROM_STRING_LEN] = {};
	u32 syn = EQE_QP_EVENT_ERR_SYND(eqe);
	char *synd_str;

	synd_str = gaudi3_nic_qp_err_syndrom_to_str(syn);

	if (strlen(synd_str)) {
		strncpy(synd_str_to_lower, synd_str, GAUDI3_MAX_SYNDROM_STRING_LEN - 1);
		hl_nic_strtolower(synd_str_to_lower);

		if (strnstr(synd_str_to_lower, "req", strlen(synd_str_to_lower)))
			return 1;
	}

	return 0;
}

static int gaudi3_eq_poll(struct hl_nic_port *nic_port, u32 asid, struct hl_nic_eq_poll_out *event)
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
		dev_info_ratelimited(hdev->dev,
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
		event->is_req = gaudi3_qp_event_is_req_event(&eqe);
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
	case EQE_WTD_SECURITY_ERR:
		event->ev_type = HL_NIC_EQ_EVENT_TYPE_WTD_SECURITY_ERR;
		event->idx = EQE_RAW_TX_EVENT_QPN(&eqe);
		break;
	case EQE_NUMERICAL_ERR:
		event->ev_type = HL_NIC_EQ_EVENT_TYPE_NUMERICAL_ERR;
		event->idx = EQE_RAW_TX_EVENT_QPN(&eqe);
		break;
	default:
		/* if the event should not be reported to the user then return
		 * as if no event was found
		 */
		dev_info_ratelimited(hdev->dev,
				"dropping Port-%d event %d report to user\n",
				port, ev_type);
		return -ENODATA;
	}

	/* fill the event-specific data */
	event->ev_data = eqe.data[2];

	return 0;
}

static u32 gaudi3_db_fifo_update_freq(struct hl_nic_db_fifo_idr_pdata *idr_pdata,
					enum hl_nic_db_fifo_type fifo_type)
{
	u32 fifo_update_freq;
	u32 fifo_update_freq_max = D0_NIC0_QPC_DB_FIFO_CFG_UPDATE_MSG_FREQ_M >>
					D0_NIC0_QPC_DB_FIFO_CFG_UPDATE_MSG_FREQ_S;

	switch (fifo_type) {
	case HL_NIC_DB_FIFO_TYPE_DB:
		fifo_update_freq = DB_FIFO_RDMA_ENTRY_SIZE;
		break;

	case HL_NIC_DB_FIFO_TYPE_CC:
		fifo_update_freq = DB_FIFO_CC_ENTRY_SIZE;
		break;

	/* On the cases below we update the DB CI every half a fifo */
	case HL_NIC_DB_FIFO_TYPE_COLL_OPS_SHORT:
	case HL_NIC_DB_FIFO_TYPE_COLL_OPS_LONG:
	case HL_NIC_DB_FIFO_TYPE_COLL_DIR_OPS_SHORT:
	case HL_NIC_DB_FIFO_TYPE_COLL_DIR_OPS_LONG:
		fifo_update_freq = idr_pdata->fifo_size / 2;
		break;

	default:
		/* for variable sized entries - return the minimal granularity */
		fifo_update_freq = DB_FIFO_MIN_GRANULARITY;
		break;
	}

	/* update freq is in u32 sizes and is 5Bits wide */
	fifo_update_freq = fifo_update_freq / sizeof(u32);
	fifo_update_freq = min(fifo_update_freq, fifo_update_freq_max);

	return fifo_update_freq;
}

static void gaudi3_get_db_fifo_id_range(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	/* TODO: SW-67924: Manage all 24 DB fifos via resource manager */
	*min_id = GAUDI3_MIN_DB_FIFO_ID(port);
	*max_id = GAUDI3_MAX_DB_FIFO_ID(port);
}

static void gaudi3_get_db_fifo_modes_mask(struct hl_nic_port *nic_port, u32 *mode_mask)
{
	*mode_mask = BIT(HL_NIC_DB_FIFO_TYPE_DB) | BIT(HL_NIC_DB_FIFO_TYPE_CC) |
			BIT(HL_NIC_DB_FIFO_TYPE_COLL_OPS_SHORT) |
			BIT(HL_NIC_DB_FIFO_TYPE_COLL_OPS_LONG) |
			BIT(HL_NIC_DB_FIFO_TYPE_COLL_DIR_OPS_SHORT) |
			BIT(HL_NIC_DB_FIFO_TYPE_COLL_DIR_OPS_LONG) |
			BIT(HL_NIC_DB_FIFO_TYPE_DWQ_LIN) |
			BIT(HL_NIC_DB_FIFO_TYPE_DWQ_MS);
}

static int validate_dir_dup_mask(struct hl_device *hdev, int port, u8 *direct_port_en_msk)
{
	struct hl_nic *nic = &hdev->nic;
	struct hl_nic_port *nic_port;
	u32 port_to_check = 0;

	if (is_400g_mode(hdev) && (*direct_port_en_msk & ~0x1)) {
		dev_dbg(hdev->dev, "Invalid dir_dup_ports_mask: 0x%x for 400G mode, port: %d\n",
				*direct_port_en_msk, port);
		return -EINVAL;
	}

	if (is_200g_mode(hdev)) {
		if (*direct_port_en_msk & ~0x3) {
			dev_dbg(hdev->dev, "Invalid dir_dup_ports_mask: 0x%x for 200G mode, port: %d\n",
				*direct_port_en_msk, port);
			return -EINVAL;
		}

		/* If the mask is passed with the other port of the macro, we need to
		 * check if that port is enabled and opened.
		 */
		if (((*direct_port_en_msk & 0x1) && (port % 2)) ||
			((*direct_port_en_msk & 0x2) && !(port % 2))) {
			port_to_check = port % 2 ? port - 1 : port + 1;

			nic_port = &nic->nic_ports[port_to_check];
			if (!hl_nic_is_port_open(nic_port)) {
				dev_dbg(hdev->dev, "Invalid dir_dup_port_mask: 0x%x, Other Port: %d of the macro is not enabled, port: %d\n",
					*direct_port_en_msk, port_to_check, port);
				return -EINVAL;
			}
		}
	}

	/* If the user did not supply dup ports mask, then just program the corresponding
	 * port enable bits.
	 */
	if (!*direct_port_en_msk) {
		if (is_200g_mode(hdev))
			*direct_port_en_msk = (port % 2) ? 0x2 : 0x1;
		else
			*direct_port_en_msk = 0x1;
	}

	return 0;
}

static int gaudi3_db_fifo_set(struct hl_nic_port *nic_port, struct hl_ctx *ctx, u32 id,
			u64 ci_device_handle, struct hl_nic_db_fifo_idr_pdata *idr_pdata)
{
	struct gaudi3_nic_port *gaudi3_nic = nic_port->nic_specific;
	struct hl_device *hdev = nic_port->hdev;
	struct asic_fixed_properties *props = &hdev->asic_prop;
	u32 port = nic_port->port;
	u32 mmu_bypass, size, fifo_offset, db_type, msg_freq;
	u32 val = 0, __val = 0, offset = id;
	int rc;
	bool is_coll_op;

	mmu_bypass = !!(!hdev->mmu_enable || hdev->nic.mmu_bypass);
	db_type = db_fifo_sw_to_hw_map[idr_pdata->fifo_mode];
	fifo_offset = idr_pdata->fifo_offset / sizeof(u32);
	size = ilog2((idr_pdata->fifo_size) / sizeof(u32));
	is_coll_op = (db_type == DB_FIFO_TYPE_DIRECT_PATCHER) ||
			(db_type == DB_FIFO_TYPE_SCHEDULER_DESC);

	/* If num_sobs is not zero, then ci_device_handle lower 32 bits is the SOB offset
	 * that the user provided. Hence, we need to verify its value because the HW will
	 * access the address that is based on this offset.
	 */
	if (idr_pdata->num_sobs) {
		bool found = false;
		u64 base, ci_device_handle_offset = lower_32_bits(ci_device_handle);
		int i;

		for (i = 0 ; i < props->num_of_hdcores ; i++) {
			base = CFG_BAR_BASE - LBW_BASE + mmHD0_SYNC_MNGR_OBJS_BASE +
					i * HDCORE_OFFSET;
			if ((ci_device_handle_offset >=
				(base + mmSOB_OBJS_SOB_OBJ_0_0) &&
				ci_device_handle_offset <=
				(base + mmSOB_OBJS_SOB_OBJ_0_8191)) ||
				(ci_device_handle_offset >=
				(base + mmSOB_OBJS_SOB_OBJ_1_0) &&
				ci_device_handle_offset <=
				(base + mmSOB_OBJS_SOB_OBJ_1_8191))) {
				found = true;
				break;
			}
		}

		if (!found) {
			dev_dbg(hdev->dev,
				"Failed to set DB FIFO, SOB offset 0x%llx is out of range, port %d\n",
				ci_device_handle_offset, port);
			return -EINVAL;
		}
	}

	/* validate the dir_dup_ports_mask */
	if (db_type == DB_FIFO_TYPE_DIRECT_PATCHER) {
		rc = validate_dir_dup_mask(hdev, port, &idr_pdata->dir_dup_ports_mask);
		if (rc)
			return rc;
	}

	rc = gaudi3_nic_eq_dispatcher_register_db(gaudi3_nic, ctx->asid, id);
	if (rc)
		return rc;

	/*
	 * Name DB fifo is misnomer. It's indeed a generic fifo.
	 * Configure the fifo accordingly.
	 *
	 * SW-67924: Manage all EQs.
	 */
	msg_freq = gaudi3_db_fifo_update_freq(idr_pdata, idr_pdata->fifo_mode);

	val = ctx->asid;
	val |= mmu_bypass << D0_NIC0_QPC_DB_FIFO_CFG_MMU_BP_S;
	val |= GAUDI3_EQ_RDMA_IDX(port) << D0_NIC0_QPC_DB_FIFO_CFG_EQ_ID_S;
	val |= db_type << D0_NIC0_QPC_DB_FIFO_CFG_DB_TYPE_S;
	val |= (msg_freq << D0_NIC0_QPC_DB_FIFO_CFG_UPDATE_MSG_FREQ_S) &
		D0_NIC0_QPC_DB_FIFO_CFG_UPDATE_MSG_FREQ_M;

	/* Per SW policy, DB fifo in collective mode should update CI via SOB
	 * and source data from DUP interface. Hence, piggyback on SOB for DUP
	 * interface enablement.
	 */
	if (is_coll_op && idr_pdata->num_sobs) {
		NIC_RMWREG32(mmD0_NIC0_QPC_DB_FIFO_DUP_EN, 1, 1 << id);
		val |= DB_FIFO_SRC_DUP << D0_NIC0_QPC_DB_FIFO_CFG_DB_SOURCE_S;
	}

	/* Select CI update type.
	 * 0: Update sync objects.
	 * 1: Update memory.
	 */
	if (!idr_pdata->num_sobs) {
		val |= 1 << D0_NIC0_QPC_DB_FIFO_CFG_UPDATE_MSG_TYPE_S;
	} else {
		__val = ilog2(idr_pdata->num_sobs) &
			(D0_NIC0_QPC_DB_FIFO_CFG_L2_UPDATE_NUM_OF_SOB_M >>
			D0_NIC0_QPC_DB_FIFO_CFG_L2_UPDATE_NUM_OF_SOB_S);

		if (ilog2(idr_pdata->num_sobs) > __val)
			dev_warn_ratelimited(hdev->dev, "Truncating number of SOBs %d -> %d",
						idr_pdata->num_sobs, 1 << __val);

		val |= __val << D0_NIC0_QPC_DB_FIFO_CFG_L2_UPDATE_NUM_OF_SOB_S;
	}

	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_CFG_0, val);

	/*
	 * Config CI handle. Note, CI handle might not always be memory buffer.
	 * e.g. Collective operations track CI via sync objects.
	 */
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_UPD_ADDR_LSB_0, lower_32_bits(ci_device_handle));
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_UPD_ADDR_MSB_0, upper_32_bits(ci_device_handle));

	/*
	 * DB fifo memory is HW internal and is shared across all DB fifos
	 * of a port.
	 * Note: Offset and size is in unit of dword i.e. 4 bytes.
	 *
	 */
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_CFG2_0,
			(fifo_offset << D0_NIC0_QPC_DB_FIFO_CFG2_FIFO_OFFSET_S) |
			(size << D0_NIC0_QPC_DB_FIFO_CFG2_FIFO_L2_SIZE_S) |
			(idr_pdata->dir_dup_ports_mask <<
					 D0_NIC0_QPC_DB_FIFO_CFG2_DIRECT_PORT_EN_S));

	/* TODO SW-71143: Set security per DB type. */
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_SECURITY_0,
			0 << D0_NIC0_QPC_DB_FIFO_SECURITY_SECURITY_LEVEL_S);

	return 0;
}

static void gaudi3_db_fifo_unset(struct hl_nic_port *nic_port, struct hl_ctx *ctx, u32 id,
					struct hl_nic_db_fifo_idr_pdata *idr_pdata)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, offset = id;
	int rc;

	/* Reset DB fifo consumer index.
	 * Note, CI could be sync objects or memory buffer.
	 */
	if (idr_pdata->num_sobs) {
		rc = db_fifo_reset(hdev, port, offset);
		if (rc)
			dev_dbg_ratelimited(hdev->dev,
				"Port %d user DB fifo %d SOB reset timed out, %d\n", port, id, rc);
	} else {
		gaudi3_nic_db_fifo_reset(nic_port, ctx, id, idr_pdata->ci_mmap_handle);
	}

	/* Zero out HW CI buffer address register for added safety */
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_UPD_ADDR_LSB_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_UPD_ADDR_MSB_0, 0);

	/* Clear the Read and write indices status */
	NIC_OFFSET_RMWREG32(mmD0_NIC0_QPC_DB_FIFO_STATUS_0, 0,
			D0_NIC0_QPC_DB_FIFO_STATUS_READ_INDEX_M);
	NIC_OFFSET_RMWREG32(mmD0_NIC0_QPC_DB_FIFO_STATUS_0, 0,
			D0_NIC0_QPC_DB_FIFO_STATUS_WRITE_INDEX_M);

	/* Clear configuration registers. */
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_CFG_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_DB_FIFO_CFG2_0, 0);

	hl_nic_eq_dispatcher_unregister_db(nic_port, id);
}

static void gaudi3_nic_get_db_fifo_umr(struct hl_nic_port *nic_port,
					u32 id, u64 *umr_block_addr,
					u32 *umr_db_offset)
{
	struct asic_fixed_properties *prop = &nic_port->hdev->asic_prop;
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	*umr_block_addr = prop->cfg_base_address +
				NIC_REG(mmD0_NIC0_UMR_0_BASE + (id * NIC_UMR_OFFSET));

	/* Each UMR hosts only one DB fifo at offset 0. */
	*umr_db_offset = 0;
}

static void gaudi3_get_encap_id_range(struct hl_nic_port *nic_port, u32 *min_id, u32 *max_id)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	*min_id = GAUDI3_MIN_ENCAP_ID(port);
	*max_id = GAUDI3_MAX_ENCAP_ID(port);
}

static int gaudi3_encap_set(struct hl_nic_port *nic_port, u32 encap_id,
				struct hl_nic_encap_idr_pdata *idr_pdata)
{
	struct gaudi3_nic_port *gaudi3_nic = nic_port->nic_specific;
	u32 port = nic_port->port, *encap_header = idr_pdata->encap_header, offset = encap_id,
		encap_cfg = 0, decap_cfg = 0, port_offset;
	struct hl_device *hdev = nic_port->hdev;
	u32 encap_hdr_offset = mmD0_NIC0_TXE_ENCAP_DATA_63_32_0 -
				mmD0_NIC0_TXE_ENCAP_DATA_31_0_0;
	u32 hdr_size;
	int i;

	port_offset = get_lane_offset(gaudi3_nic) * (mmD0_NIC0_TXE_SOURCE_IP_PORT1_0 -
							mmD0_NIC0_TXE_SOURCE_IP_PORT0_0);

	NIC_OFFSET_WREG32(mmD0_NIC0_TXE_SOURCE_IP_PORT0_0 + port_offset, idr_pdata->src_ip);

	encap_cfg |= (idr_pdata->encap_type_data <<
				NIC_TXE_ENCAP_CFG_IPV4_PROTOCOL_UDP_DEST_S) &
				NIC_TXE_ENCAP_CFG_IPV4_PROTOCOL_UDP_DEST_M;

	if (idr_pdata->encap_type == HL_NIC_ENCAP_NONE) {
		NIC_OFFSET_WREG32(mmD0_NIC0_TXE_ENCAP_CFG_0, encap_cfg);
		return 0;
	}

	if (!IS_ALIGNED(idr_pdata->encap_header_size, sizeof(u32))) {
		dev_err(hdev->dev, "Encap header size(%d) must be a multiple of %ld\n",
			idr_pdata->encap_header_size, sizeof(u32));
		return -EINVAL;
	}

	hdr_size = idr_pdata->encap_header_size / sizeof(u32);
	encap_cfg |= (hdr_size << NIC_TXE_ENCAP_CFG_ENCAP_SIZE_S) &
			NIC_TXE_ENCAP_CFG_ENCAP_SIZE_M;

	if (idr_pdata->encap_type == HL_NIC_ENCAP_OVER_UDP) {
		encap_cfg |= BIT(NIC_TXE_ENCAP_CFG_HDR_FORMAT_S);

		if (!hdev->nic.is_decap_disabled)
			decap_cfg |= BIT(D0_NIC0_RXB_CORE_PRT_TNL_DECAP_ENTRY_VALID_UDP_S);

	} else if (idr_pdata->encap_type == HL_NIC_ENCAP_OVER_IPV4) {
		if (!hdev->nic.is_decap_disabled)
			decap_cfg |= BIT(D0_NIC0_RXB_CORE_PRT_TNL_DECAP_ENTRY_VALID_IPV4_S);
	}

	if (!hdev->nic.is_decap_disabled) {
		decap_cfg |= (idr_pdata->encap_type_data <<
					D0_NIC0_RXB_CORE_PRT_TNL_DECAP_ENTRY_NEXT_HDR_TUNNEL_S) &
					D0_NIC0_RXB_CORE_PRT_TNL_DECAP_ENTRY_NEXT_HDR_TUNNEL_M;

		decap_cfg |= (hdr_size << D0_NIC0_RXB_CORE_PRT_TNL_DECAP_ENTRY_TNL_SIZE_S) &
				D0_NIC0_RXB_CORE_PRT_TNL_DECAP_ENTRY_TNL_SIZE_M;
	}

	/*
	 * Encapsulation header is already aligned to 32 bits. Hence, it's
	 * safe to access it in chunks of 4 bytes.
	 */
	for (i = 0 ; i * sizeof(u32) < idr_pdata->encap_header_size ; i++)
		NIC_OFFSET_WREG32(mmD0_NIC0_TXE_ENCAP_DATA_31_0_0 + encap_hdr_offset * i,
					encap_header[i]);

	NIC_OFFSET_WREG32(mmD0_NIC0_TXE_ENCAP_CFG_0, encap_cfg);

	if (!hdev->nic.is_decap_disabled) {
		NIC_OFFSET_WREG32(mmD0_NIC0_RXB_CORE_PRT_TNL_DECAP_ENTRY_0, decap_cfg);

		/* As part of the ECO support for H9-5384, lower part of DECAP mask must be 0
		 * Even though at the time of writing these lines, there are no writes to this
		 * register, make sure the lower bits are always set to 0.
		 */
		if (hdev->nic_enable_h9_rx_drop_eco) {
			uint32_t tnl_decap_mask;

			tnl_decap_mask = NIC_OFFSET_RREG32(mmD0_NIC0_RXB_CORE_PRT_TNL_DECAP_MASK_0);
			if (tnl_decap_mask & RX_DROP_ECO_DCAP_UNSET_MASK) {
				dev_dbg(hdev->dev, "Decap mask 0x%x is not valid\n",
					tnl_decap_mask);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static void gaudi3_encap_unset(struct hl_nic_port *nic_port, u32 encap_id,
				struct hl_nic_encap_idr_pdata *idr_pdata)
{
	struct gaudi3_nic_port *gaudi3_nic = nic_port->nic_specific;
	u32 port = nic_port->port, offset = encap_id, port_offset;
	struct hl_device *hdev = nic_port->hdev;
	u32 encap_hdr_offset = mmD0_NIC0_TXE_ENCAP_DATA_63_32_0 -
				mmD0_NIC0_TXE_ENCAP_DATA_31_0_0;
	int i;

	port_offset = get_lane_offset(gaudi3_nic) * (mmD0_NIC0_TXE_SOURCE_IP_PORT1_0 -
							mmD0_NIC0_TXE_SOURCE_IP_PORT0_0);

	NIC_OFFSET_WREG32(mmD0_NIC0_TXE_SOURCE_IP_PORT0_0 + port_offset, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_TXE_ENCAP_CFG_0, 0);

	NIC_OFFSET_WREG32(mmD0_NIC0_RXB_CORE_PRT_TNL_DECAP_ENTRY_0, 0);

	for (i = 0 ; i * sizeof(u32) < idr_pdata->encap_header_size ; i++)
		NIC_OFFSET_WREG32(mmD0_NIC0_TXE_ENCAP_DATA_31_0_0 + encap_hdr_offset * i, 0);
}

static int gaudi3_nic_get_cnts_num(struct hl_nic_port *nic_port)
{
	int n_spmu_stats, mac_counters;
	struct hl_en_stat *ignore;

	gaudi3_nic_spmu_get_stats_info(nic_port, &ignore, &n_spmu_stats);
	/* Skip MAC counters in simulator */
	mac_counters =  !nic_port->hdev->nic.skip_mac_cnts ? hl_nic_mac_stats_rx_len +
				hl_nic_mac_stats_tx_len + gaudi3_nic_mac_fec_stats_len : 0;

	return n_spmu_stats + mac_counters + gaudi3_nic_err_stats_len + gaudi3_nic_perf_stats_len;
}

static void gaudi3_nic_get_cnts_names(struct hl_nic_port *nic_port, u8 *data)
{
	struct hl_en_stat *spmu_stats;
	u32 n_spmu_stats = 0;
	int i;

	gaudi3_nic_spmu_get_stats_info(nic_port, &spmu_stats, &n_spmu_stats);

	for (i = 0 ; i < n_spmu_stats ; i++)
		memcpy(data + i * ETH_GSTRING_LEN, spmu_stats[i].str, ETH_GSTRING_LEN);
	data += i * ETH_GSTRING_LEN;

	/* Skip MAC counters in simulator */
	if (!nic_port->hdev->nic.skip_mac_cnts) {
		for (i = 0 ; i < hl_nic_mac_stats_rx_len ; i++)
			memcpy(data + i * ETH_GSTRING_LEN, hl_nic_mac_stats_rx[i].str,
				ETH_GSTRING_LEN);
		data += i * ETH_GSTRING_LEN;

		for (i = 0 ; i < gaudi3_nic_mac_fec_stats_len ; i++)
			memcpy(data + i * ETH_GSTRING_LEN, gaudi3_nic_mac_fec_stats[i].str,
				ETH_GSTRING_LEN);
		data += i * ETH_GSTRING_LEN;

		for (i = 0 ; i < hl_nic_mac_stats_tx_len ; i++)
			memcpy(data + i * ETH_GSTRING_LEN, hl_nic_mac_stats_tx[i].str,
				ETH_GSTRING_LEN);
		data += i * ETH_GSTRING_LEN;
	}

	for (i = 0 ; i < gaudi3_nic_err_stats_len ; i++)
		memcpy(data + i * ETH_GSTRING_LEN, gaudi3_nic_err_stats[i].str, ETH_GSTRING_LEN);
	data += i * ETH_GSTRING_LEN;

	for (i = 0 ; i < gaudi3_nic_perf_stats_len ; i++)
		memcpy(data + i * ETH_GSTRING_LEN, gaudi3_nic_perf_stats[i].str, ETH_GSTRING_LEN);
}

static int gaudi3_nic_get_mac_tx_stats(struct hl_nic_port *nic_port, u64 *data)
{
	u32 port = nic_port->port, offset, stats_len;
	struct hl_device *hdev = nic_port->hdev;
	u64 addr, lo_part, hi_part;
	int i;

	/* The MAC stats block has 4 sets of the TX registers.
	 * In 400G mode, take stats from TX0 regs.
	 * In 200G mode, for even ports take stats from TX0 regs and odd ports from TX2 regs
	 */
	offset = ELEMENT_OFFSET(port, MAC_GLOB_STAT_TX_NUM);
	addr = mmD0_NIC0_MAC_GLOB_STAT_TX0_BASE + mmNIC_MAC_GLOB_STAT_TX0_ETHERSTATSOCTETS_4;

	stats_len = hl_nic_mac_stats_tx_len;

	for (i = 0 ; i < stats_len ; i++) {
		lo_part = NIC_OFFSET_RREG32(addr + hl_nic_mac_stats_tx[i].lo_offset);

		/* Upper part must be read after lower part, since the upper part register
		 * gets its value only after the lower part was read.
		 */
		hi_part = NIC_RREG32(mmD0_NIC0_MAC_GLOB_STAT_CONTROL_REG_DATA_HI);
		data[i] = lo_part | (hi_part << 32);
	}

	return i;
}

static int gaudi3_nic_get_mac_rx_stats(struct hl_nic_port *nic_port, u64 *data)
{
	u32 port = nic_port->port, offset, stats_len;
	struct hl_device *hdev = nic_port->hdev;
	u64 addr, lo_part, hi_part;
	int i;

	/* The MAC stats block has 4 sets of the RX registers.
	 * In 400G mode, take stats from RX0 regs.
	 * In 200G mode, for even ports take stats from RX0 regs and odd ports from RX2 regs
	 */
	offset = ELEMENT_OFFSET(port, MAC_GLOB_STAT_RX_NUM);
	addr = mmD0_NIC0_MAC_GLOB_STAT_RX0_BASE + mmNIC_MAC_GLOB_STAT_RX0_ETHERSTATSOCTETS;

	stats_len = hl_nic_mac_stats_rx_len;

	for (i = 0 ; i < stats_len ; i++) {
		lo_part = NIC_OFFSET_RREG32(addr + hl_nic_mac_stats_rx[i].lo_offset);

		/* Upper part must be read after lower part, since the upper part register
		 * gets its value only after the lower part was read.
		 */
		hi_part = NIC_RREG32(mmD0_NIC0_MAC_GLOB_STAT_CONTROL_REG_DATA_HI);
		data[i] = lo_part | (hi_part << 32);
	}

	return i;
}

static int gaudi3_nic_get_mac_fec_stats(struct hl_nic_port *nic_port, u64 *data)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;
	u64 start_reg, total_symb_err, divisor;
	int i, j;

	/* There are 4 sets of FEC Stats registers.
	 * In 400G mode, take the stats from _1 regs.
	 * In 200G mode, for even ports take stats from _1 regs and for odd ports take from _3
	 * regs.
	 */
	start_reg = (is_400g_mode(hdev) ? (mmD0_NIC0_MAC_GLOB_STAT_RSFEC_STATS_BASE +
			mmNIC_MAC_GLOB_STAT_RSFEC_STATS_TOTAL_CW_1) :
			((port & 1) ? (mmD0_NIC0_MAC_GLOB_STAT_RSFEC_STATS_BASE +
					mmNIC_MAC_GLOB_STAT_RSFEC_STATS_TOTAL_CW_3) :
					(mmD0_NIC0_MAC_GLOB_STAT_RSFEC_STATS_BASE +
					mmNIC_MAC_GLOB_STAT_RSFEC_STATS_TOTAL_CW_1)));

	for (i = 0 ; i < (gaudi3_nic_mac_fec_stats_len - 2) ; i++)
		data[i] = NIC_RREG32(start_reg + gaudi3_nic_mac_fec_stats[i].lo_offset);

	/* Formula to calculate post_FEC_SER:
	 * post_FEC_SER =  STATS_TOTAL_CW / TOTAL_UNCORRECTED_CW * 16
	 */
	divisor = data[FEC_CW_UNCORRECTABLE] << 4;

	if (divisor)
		data[i++] = div64_u64(data[FEC_CW_RECEIVED], divisor);
	else
		data[i++] = ~0ULL;

	/* Formula to calculate pre_FEC_SER:
	 * pre_FEC_SER = STATS_TOTAL_CW / (CW_CORRECTED_1_SYMB_ERR +
	 *                          CW_CORRECTED_2_SYMB_ERR * 2 +
	 *                          CW_CORRECTED_3_SYMB_ERR * 3 +
	 *                          CW_CORRECTED_4_SYMB_ERR * 4 +
	 *                          CW_CORRECTED_5_SYMB_ERR * 5 +
	 *                          CW_CORRECTED_6_SYMB_ERR * 6 +
	 *                          CW_CORRECTED_7_SYMB_ERR * 7 +
	 *                          CW_CORRECTED_8_SYMB_ERR * 8 +
	 *                          CW_CORRECTED_9_SYMB_ERR * 9 +
	 *                          CW_CORRECTED_10_SYMB_ERR * 10 +
	 *                          CW_CORRECTED_11_SYMB_ERR * 11 +
	 *                          CW_CORRECTED_12_SYMB_ERR * 12 +
	 *                          CW_CORRECTED_13_SYMB_ERR * 13 +
	 *                          CW_CORRECTED_14_SYMB_ERR * 14 +
	 *                          CW_CORRECTED_15_SYMB_ERR * 15 +
	 *                          TOTAL_UNCORRECTED_CW * 16)
	 */
	for (j = 0, total_symb_err = 0 ; j < FEC_MAX_SYMBOL_ERR ; j++)
		total_symb_err += (data[FEC_CW_CORRECTED_1_SYMBOL_ERR + j] * (j + 1));

	divisor = total_symb_err + (data[FEC_CW_UNCORRECTABLE] << 4);

	if (divisor)
		data[i++] = div64_u64(data[FEC_CW_RECEIVED], divisor);
	else
		data[i++] = ~0ULL;

	nic_port->correctable_errors_cnt = data[FEC_CW_CORRECT];
	nic_port->uncorrectable_errors_cnt = data[FEC_CW_UNCORRECTABLE];

	return i;
}

static int gaudi3_nic_get_mac_stats(struct hl_nic_port *nic_port, u64 *data)
{
	int cnt = 0;

	/* Skip MAC counters in simulator */
	if (nic_port->hdev->nic.skip_mac_cnts)
		return 0;

	cnt += gaudi3_nic_get_mac_rx_stats(nic_port, &data[cnt]);
	cnt += gaudi3_nic_get_mac_fec_stats(nic_port, &data[cnt]);
	cnt += gaudi3_nic_get_mac_tx_stats(nic_port, &data[cnt]);

	return cnt;
}

static int gaudi3_nic_get_nic_err_stats(struct hl_nic_port *nic_port, u64 *data)
{
	struct gaudi3_nic_port *gaudi3_nic = nic_port->nic_specific;
	struct gaudi3_device *gaudi3 = nic_port->hdev->asic_specific;
	struct hl_aux_dev *aux_dev = &gaudi3_nic->hdev->nic.en_aux_dev;
	int i = 0;

	data[i++] = gaudi3_nic->cong_q_err_cnt;
	data[i++] = nic_port->eth_enable ?
			gaudi3->en_aux_ops.get_overrun_cnt(aux_dev, nic_port->port) : 0;

	return i;
}

static int gaudi3_nic_get_perf_stats(struct hl_nic_port *nic_port, u64 *data)
{
	struct hl_device *hdev = nic_port->hdev;
	struct hl_nic_properties *nic_prop;
	u64 bw_dividend, bw_int, bw_frac, lat_dividend, lat_divisor, lat_int, lat_frac;
	u32 port = nic_port->port;

	nic_prop = &hdev->asic_prop.nic_props;

	/* Bandwidth calculation */
	bw_dividend = (((u64) NIC_RREG32(mmD0_NIC0_TXE_STATS_MEAS_WIN_BYTES_MSB)) << 32) |
				NIC_RREG32(mmD0_NIC0_TXE_STATS_MEAS_WIN_BYTES_LSB);

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
		lat_dividend = (((u64) NIC_RREG32(mmD0_NIC0_TXE_STATS_TOT_BYTES_MSB)) << 32) |
					NIC_RREG32(mmD0_NIC0_TXE_STATS_TOT_BYTES_LSB);
		lat_divisor = nic_prop->clk;

		lat_int = div_u64(lat_dividend, lat_divisor);
		lat_frac = ((lat_dividend - lat_divisor * lat_int) * 10) / (lat_divisor);
	}

	data[PERF_BANDWIDTH_INT] = bw_int;
	data[PERF_BANDWIDTH_FRAC] = bw_frac;
	data[PERF_LATENCY_INT] = lat_int;
	data[PERF_LATENCY_FRAC] = lat_frac;

	return (int) gaudi3_nic_perf_stats_len;
}

static void gaudi3_nic_get_cnts_values(struct hl_nic_port *nic_port, u64 *data)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 cnt = 0;
	int rc;

	rc = hl_nic_read_spmu_counters(nic_port, &data[cnt], &cnt);
	if (rc)
		dev_err(hdev->dev, "Failed to get SPMU counters, port %d\n", nic_port->port);

	cnt += gaudi3_nic_get_mac_stats(nic_port, &data[cnt]);
	cnt += gaudi3_nic_get_nic_err_stats(nic_port, &data[cnt]);
	cnt += gaudi3_nic_get_perf_stats(nic_port, &data[cnt]);
}

static void gaudi3_nic_reset_mac_stats(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	NIC_WREG32(mmD0_NIC0_MAC_GLOB_STAT_CONTROL_REG_STATN_CONFIG,
			   BIT(NIC_MAC_GLOB_STAT_CONTROL_REG_STATN_CONFIG_F_RESET_S));
}

void gaudi3_nic_read_mac_fec_stats(struct hl_nic_port *nic_port, u64 *data)
{
	gaudi3_nic_get_mac_fec_stats(nic_port, data);
}

static void gaudi3_nic_user_ccq_set(struct hl_nic_port *nic_port, u64 ccq_device_addr,
		u64 pi_device_addr, u32 num_of_entries, u32 *ccqn)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, offset;

	offset = ELEMENT_OFFSET(port, QPC_CONG_QUE_NUM);

	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_BASE_ADDR_63_32_0, upper_32_bits(ccq_device_addr));
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_BASE_ADDR_31_7_0,
			((ccq_device_addr >> 7) & 0x1FFFFFF));

	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_PI_ADDR_63_32_0, upper_32_bits(pi_device_addr));
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_PI_ADDR_31_7_0,
			((pi_device_addr >> 7) & 0x1FFFFFF));

	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_WRITE_INDEX_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_PRODUCER_INDEX_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_CONSUMER_INDEX_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_CONSUMER_INDEX_CB_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_LOG_SIZE_0, ilog2(num_of_entries));

	/* set enable + update-pi
	 * set overrun-en to allow overrun of ci since a HW bug exist
	 */
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_CFG_0,
		D0_NIC0_QPC_CONG_QUE_CFG_ENABLE_M |
		D0_NIC0_QPC_CONG_QUE_CFG_OVERRUN_EN_M |
		D0_NIC0_QPC_CONG_QUE_CFG_WRITE_PI_EN_M);

	*ccqn = ELEMENT_OFFSET(nic_port->port, QPC_CONG_QUE_NUM);
}

static void gaudi3_nic_user_ccq_unset(struct hl_nic_port *nic_port, u32 *ccqn)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port, offset;

	offset = ELEMENT_OFFSET(port, QPC_CONG_QUE_NUM);

	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_CFG_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_PI_ADDR_63_32_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_PI_ADDR_31_7_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_BASE_ADDR_63_32_0, 0);
	NIC_OFFSET_WREG32(mmD0_NIC0_QPC_CONG_QUE_BASE_ADDR_31_7_0, 0);

	*ccqn = ELEMENT_OFFSET(nic_port->port, QPC_CONG_QUE_NUM);
}

/**
 * gaudi3_nic_disable_nics_interrupts() - Disable interrupts of all NICs.
 * Gaudi3 NIC interrupts are enabled by default, need to disable them ASAP
 * before ports init and after hard reset.
 *
 * @hdev: habanalabs device structure.
 */
static void gaudi3_nic_disable_nics_interrupts(struct hl_device *hdev)
{
	struct hl_nic_macro *nic_macro;
	u32 port;
	int i;

	/* The CPU_IF_NIC registers are handled only by the privileged embedded code and in any
	 * case, this is relevant only on PLDM where the PHY link is always ON
	 */
	if (hdev->pldm)
		gaudi3_disable_nic_interrupts_cpu_if(hdev);

	/* Disable interrupts of all NICs */
	for (i = 0 ; i < NIC_NUMBER_OF_MACROS ; i++) {
		nic_macro = &hdev->nic.nic_macros[i];

		/* skip non-present macros in pldm as we may run on partial-nics image */
		if (hdev->pldm && !(hdev->nic_ports_mask &
					gaudi3_nic_get_macro_ports_mask(nic_macro)))
			continue;

		port = gaudi3_nic_get_first_port(nic_macro);

		NIC_WREG32(mmD0_NIC0_TXE_INTERRUPT_MASK, NIC_TXE_INTERRUPT_MASK_R_M);
		NIC_WREG32(mmD0_NIC0_TXS_INTERRUPT_MASK, NIC_TXS_INTERRUPT_MASK_R_M);

		/* interrupt MSI and WIRE regs determine if the interrupt
		 * being generated is directed to the MSI or Wire Path
		 * (can also be both or neither). By default interrupts are
		 * directed to both locations.
		 */
		NIC_WREG32(mmD0_NIC0_QPC_INTERRUPT_MSI, 0);
		NIC_WREG32(mmD0_NIC0_QPC_INTERRUPT_WIRE, 0);

		/* Interrupt mask reg disables/enables each interrupt (1 is disabled)
		 * The meaning of the bits in the interrupt mask register is:
		 *    [3..0] - EQ event interrupt (1 bit per EQ)
		 *    [4..7] - EQ error interrupt (1 bit per EQ)
		 * interrupts are disabled by default
		 */
		NIC_WREG32(mmD0_NIC0_QPC_INTERRUPT_MASK, D0_NIC0_QPC_INTERRUPT_MASK_R_M);

		NIC_WREG32(mmD0_NIC0_QPC_INTERRUPT_RESP_ERR_MASK,
					D0_NIC0_QPC_INTERRUPT_RESP_ERR_MASK_R_M);

		NIC_WREG32(mmD0_NIC0_RXE_SPI_INTR_MASK_0, D0_NIC0_RXE_SPI_INTR_MASK_VAL_M);
		NIC_WREG32(mmD0_NIC0_RXE_SEI_INTR_MASK, 0xFFFFFFFF);

		NIC_WREG32(mmD0_NIC0_QPC_EVENT_QUE_CFG_0, 0);
		NIC_WREG32(mmD0_NIC0_QPC_EVENT_QUE_CFG_1, 0);
		NIC_WREG32(mmD0_NIC0_QPC_EVENT_QUE_CFG_2, 0);
		NIC_WREG32(mmD0_NIC0_QPC_EVENT_QUE_CFG_3, 0);

		/* flush */
		NIC_RREG32(mmD0_NIC0_RXE_SEI_INTR_MASK);
	}
}

/**
 * gaudi3_nic_quiescence() - make sure that NIC does not generate events nor
 *                           receives traffic.
 * Gaudi3 default values at power-up and after hard-reset are interrupts enabled
 * and Rx enabled, we need to disable them until driver configuration is
 * complete.
 *
 * @hdev: habanalabs device structure.
 */
void gaudi3_nic_quiescence(struct hl_device *hdev)
{
	/* Do not quiescence the ports during device release
	 * reset aka soft reset flow.
	 */
	if (gaudi3_nic_get_hw_cap(hdev))
		return;

	dev_dbg(hdev->dev, "Quiescence the NICs\n");

	gaudi3_nic_disable_nics_interrupts(hdev);
}

bool is_coll_qp_in_reset(struct hl_nic_port *nic_port, u32 qpn)
{
	struct hl_nic_funcs *nic_funcs;
	struct hl_coll_qp *coll_qp;
	struct hl_device *hdev;
	struct hl_nic *nic;
	struct hl_qp *qp;
	u32 port, real_port, coll_qp_id, coll_conn_type;
	bool rc = true;

	hdev = nic_port->hdev;
	nic_funcs = hdev->asic_funcs->nic_funcs;
	nic = &hdev->nic;
	port = nic_port->port;

	/* This WA is only for 200G mode */
	if (is_400g_mode(hdev))
		return false;

	if (!nic_funcs->is_coll_conn_id(hdev, qpn))
		return false;

	hl_nic_cfg_lock_all(hdev);

	if ((qpn - GAUDI3_MIN_COLL_QP_ID) >= (NIC_MAX_COLL_QP_NUM / 2)) {
		real_port = port & 0x1 ? port : port + 1;
		coll_qp_id = qpn - NIC_MAX_COLL_QP_NUM / 2;
	} else {
		real_port = port & 0x1 ? port - 1 : port;
		coll_qp_id = qpn;
	}

	coll_conn_type =
		(coll_qp_id >= (GAUDI3_MIN_COLL_QP_ID + NIC_MAX_NON_SCALE_OUT_COLL_CONNS)) ?
		HL_NIC_COLL_CONN_TYPE_SCALE_OUT : HL_NIC_COLL_CONN_TYPE_NON_SCALE_OUT;

	coll_qp = idr_find(&nic->coll_props[coll_conn_type].coll_qp_ids, coll_qp_id);
	if (IS_ERR_OR_NULL(coll_qp)) {
		dev_dbg(hdev->dev,
			"Failed to find matching collective QP %u, port %u\n",
			coll_qp_id, real_port);
		rc = false;
		goto out;
	}

	qp = coll_qp->qps_array[real_port];

	if (IS_ERR_OR_NULL(qp)) {
		dev_dbg(hdev->dev,
			"No QP for port %u under collective QP %u\n", real_port, coll_qp_id);
		rc = true;
		goto out;
	}

	if (qp->curr_state != NIC_QP_STATE_RESET) {
		rc = false;
		goto out;
	}

	dev_dbg(hdev->dev, "Got event on unused Collective QP %u\n", qp->qp_id);

out:
	hl_nic_cfg_unlock_all(hdev);
	return rc;
}

static void gaudi3_nic_fill_nic_status(struct hl_nic_port *nic_port,
					struct cpucp_nic_status *nic_status)
{
	/* TODO: SW-68468: Implement NIC status packet */
}

static void gaudi3_nic_cfg_lock(struct hl_nic_port *nic_port)
	__acquires(&gaudi3_macro->cfg_lock)
{
	struct gaudi3_nic_macro *gaudi3_macro = nic_port->nic_macro->asic_priv;

	mutex_lock(&gaudi3_macro->cfg_lock);
}

static void gaudi3_nic_cfg_unlock(struct hl_nic_port *nic_port)
	__releases(&gaudi3_macro->cfg_lock)
{
	struct gaudi3_nic_macro *gaudi3_macro = nic_port->nic_macro->asic_priv;

	mutex_unlock(&gaudi3_macro->cfg_lock);
}

static bool gaudi3_nic_cfg_is_locked(struct hl_nic_port *nic_port)
{
	struct gaudi3_nic_macro *gaudi3_macro = nic_port->nic_macro->asic_priv;

	return mutex_is_locked(&gaudi3_macro->cfg_lock);
}

static void gaudi3_nic_qp_pre_destroy(struct hl_qp *qp)
{

}

static void gaudi3_nic_qp_post_destroy(struct hl_qp *qp)
{

}

static u32 gaudi3_nic_get_coll_qps_offset(struct hl_nic_port *nic_port)
{
	struct hl_device *hdev = nic_port->hdev;
	u32 port = nic_port->port;

	return ELEMENT_OFFSET(port, NIC_MAX_COLL_QP_NUM);
}

static void gaudi3_nic_get_coll_qp_id_range(struct hl_device *hdev, bool is_scale_out_conn,
						u32 *min_id, u32 *max_id)
{
	if (is_scale_out_conn) {
		*min_id = GAUDI3_MIN_COLL_QP_ID + NIC_MAX_NON_SCALE_OUT_COLL_CONNS;
		*max_id = GAUDI3_MAX_COLL_QP_ID;
	} else {
		*min_id = GAUDI3_MIN_COLL_QP_ID;
		*max_id = GAUDI3_MIN_COLL_QP_ID + NIC_MAX_NON_SCALE_OUT_COLL_CONNS - 1;
	}
}

static bool gaudi3_nic_is_coll_conn_id(struct hl_device *hdev, u32 conn_id)
{
	return conn_id >= GAUDI3_MIN_COLL_QP_ID;
}

static u32 gaudi3_nic_get_max_msg_sz(struct hl_device *hdev)
{
	return SZ_1G;
}

void gaudi3_handle_nic_port_reset_locked(struct hl_nic_port *nic_port)
{
	struct gaudi3_device *gaudi3 = nic_port->hdev->asic_specific;
	struct hl_device *hdev = nic_port->hdev;

	if (hdev->nic_ports_ext_mask & BIT(nic_port->port)) {
		dev_err_ratelimited(hdev->dev, "NIC port %d, going to reset\n", nic_port->port);
		gaudi3->en_aux_ops.port_reset_locked(&hdev->nic.en_aux_dev, nic_port->port);
	} else {
		hl_nic_internal_port_fini_locked(nic_port);
		hl_nic_internal_port_init_locked(nic_port);
	}
}

void gaudi3_handle_nic_spi_event(struct hl_device *hdev, u32 macro_index,
						struct hl_eq_intr_cause *nic_intr_cause)
{
	u32 rxe_spi_intr_cause_0, rxe_spi_intr_cause_1, rxb_core_spi_intr_cause,
		rxe_spi_intr_mask_0, rxe_spi_intr_mask_1, rxb_core_spi_intr_mask,
		qpc_intr_cause, port, first_port, last_port;
	struct hl_nic_port *nic_port;
	int i;

	first_port = macro_index * NIC_PORTS_PER_MACRO;
	last_port = (macro_index + 1) * NIC_PORTS_PER_MACRO - 1;

	for (port = first_port ; port <= last_port ; port++) {
		/* check that port is indeed enabled in the macro */
		if (!(hdev->nic_ports_mask & BIT(port)))
			continue;

		qpc_intr_cause = NIC_RREG32(mmD0_NIC0_QPC_INTERRUPT_CAUSE);

		/* eqe interrupts are mapped to MSI except interrupt on error event queue
		 * which is handled here, in such case port reset is required.
		 */
		if (!(qpc_intr_cause & GAUDI3_NIC_EQ_ERR_INTERRUPT_M(port)))
			continue;

		dev_err_ratelimited(hdev->dev, "QPC EQ error on NIC port %d\n", port);
		NIC_WREG32(mmD0_NIC0_QPC_INTERRUPT_CLR, GAUDI3_NIC_EQ_ERR_INTERRUPT_M(port));

		nic_port = &hdev->nic.nic_ports[port];
		mutex_lock(&nic_port->control_lock);
		gaudi3_handle_nic_port_reset_locked(nic_port);
		mutex_unlock(&nic_port->control_lock);
	}

	port = first_port;
	rxe_spi_intr_cause_0 = NIC_RREG32(mmD0_NIC0_RXE_SPI_INTR_CAUSE_0);
	rxe_spi_intr_mask_0 = NIC_RREG32(mmD0_NIC0_RXE_SPI_INTR_MASK_0);
	rxe_spi_intr_cause_0 = rxe_spi_intr_cause_0 & ~rxe_spi_intr_mask_0;

	rxe_spi_intr_cause_1 = NIC_RREG32(mmD0_NIC0_RXE_SPI_INTR_CAUSE_1);
	rxe_spi_intr_mask_1 = NIC_RREG32(mmD0_NIC0_RXE_SPI_INTR_MASK_1);
	rxe_spi_intr_cause_1 = rxe_spi_intr_cause_1 & ~rxe_spi_intr_mask_1;

	rxb_core_spi_intr_cause = NIC_RREG32(mmD0_NIC0_RXB_CORE_SPI_INTR_CAUSE);
	rxb_core_spi_intr_mask = NIC_RREG32(mmD0_NIC0_RXB_CORE_SPI_INTR_MASK);
	rxb_core_spi_intr_cause = rxb_core_spi_intr_cause & ~rxb_core_spi_intr_mask;

	/* RXE SPI interrupts are packet caused interrupts and are not severe,
	 * no need to perform port reset on them, they should be print for debug purpose.
	 */
	/* Used "ARRAY_SIZE(qp_err_rxe_strs) - 1" as qp_err_rxe_strs size is 33 but
	 * mmD0_NIC0_RXE_SPI_INTR_CAUSE_0 is only 32 bit register
	 */
	if (rxe_spi_intr_cause_0) {
		for (i = 0 ; i < ARRAY_SIZE(qp_err_rxe_strs) - 1 ; i++) {
			if (!(rxe_spi_intr_cause_0 & BIT(i)))
				continue;

			dev_dbg_ratelimited(hdev->dev,
				"RXE SPI error on NIC macro %d cause: %s. cause bit %d\n",
				macro_index, qp_err_rxe_strs[i], i);
		}

		/* After writing to the SPI_INTR_CLEAR register we need to set it back to zero as
		 * it's a sticky register (the read between is done in order to flush the first
		 * write).
		 */
		NIC_WREG32(mmD0_NIC0_RXE_SPI_INTR_CLEAR_0, rxe_spi_intr_cause_0);
		NIC_RREG32(mmD0_NIC0_RXE_SPI_INTR_CLEAR_0);
		NIC_WREG32(mmD0_NIC0_RXE_SPI_INTR_CLEAR_0, 0);
	}

	if (rxe_spi_intr_cause_1) {
		for (i = 0 ; i < ARRAY_SIZE(gaudi3_nic_rxe_spi_interrupts_cause_1) ; i++) {
			if (!(rxe_spi_intr_cause_1 & BIT(i)))
				continue;

			dev_dbg_ratelimited(hdev->dev,
				"RXE SPI error on NIC macro %d cause: %s. cause bit %d\n",
				macro_index, gaudi3_nic_rxe_spi_interrupts_cause_1[i], i);
		}

		/* After writing to the SPI_INTR_CLEAR register we need to set it back to zero as
		 * it's a sticky register (the read between is done in order to flush the first
		 * write).
		 */
		NIC_WREG32(mmD0_NIC0_RXE_SPI_INTR_CLEAR_1, rxe_spi_intr_cause_1);
		NIC_RREG32(mmD0_NIC0_RXE_SPI_INTR_CLEAR_1);
		NIC_WREG32(mmD0_NIC0_RXE_SPI_INTR_CLEAR_1, 0);
	}

	if (rxb_core_spi_intr_cause) {
		for (i = 0 ; i < ARRAY_SIZE(gaudi3_nic_rxb_core_spi_interrupts_cause) ; i++) {
			if (!(rxb_core_spi_intr_cause & BIT(i)))
				continue;

			dev_dbg_ratelimited(hdev->dev,
				"RXB CORE SPI error on NIC macro %d cause: %s. cause bit %d\n",
				macro_index, gaudi3_nic_rxb_core_spi_interrupts_cause[i], i);
		}

		/* After writing to the SPI_INTR_CLEAR register we need to set it back to zero
		 * as it's a sticky register (the read between is done in order to flush the first
		 * write).
		 */
		NIC_WREG32(mmD0_NIC0_RXB_CORE_SPI_INTR_CLEAR, rxb_core_spi_intr_cause);
		NIC_RREG32(mmD0_NIC0_RXB_CORE_SPI_INTR_CLEAR);
		NIC_WREG32(mmD0_NIC0_RXB_CORE_SPI_INTR_CLEAR, 0);
	}
}

void gaudi3_handle_nic_sei_error_event(struct hl_device *hdev, u32 macro_index,
						struct hl_eq_intr_cause *nic_intr_cause)
{
	u32 rxe_sei_intr_cause, rxb_core_sei_intr_cause, tmr_intr_cause,
		rxe_sei_intr_mask, rxb_core_sei_intr_mask, tmr_intr_mask, port,
		qpc_intr_resp_err_cause, txs_intr_cause, txe_intr_cause,
		qpc_intr_resp_err_mask, txs_intr_mask, txe_intr_mask;
	int i;

	port = macro_index * NIC_PORTS_PER_MACRO;
	rxe_sei_intr_cause = NIC_RREG32(mmD0_NIC0_RXE_SEI_INTR_CAUSE);
	rxe_sei_intr_mask = NIC_RREG32(mmD0_NIC0_RXE_SEI_INTR_MASK);
	rxe_sei_intr_cause = rxe_sei_intr_cause & ~rxe_sei_intr_mask;

	rxb_core_sei_intr_cause = NIC_RREG32(mmD0_NIC0_RXB_CORE_SEI_INTR_CAUSE);
	rxb_core_sei_intr_mask = NIC_RREG32(mmD0_NIC0_RXB_CORE_SEI_INTR_MASK);
	rxb_core_sei_intr_cause = rxb_core_sei_intr_cause & ~rxb_core_sei_intr_mask;

	tmr_intr_cause = NIC_RREG32(mmD0_NIC0_TMR_INTERRUPT_CAUSE);
	tmr_intr_mask = NIC_RREG32(mmD0_NIC0_TMR_INTERRUPT_MASK);
	tmr_intr_cause = tmr_intr_cause & ~tmr_intr_mask;

	qpc_intr_resp_err_cause = NIC_RREG32(mmD0_NIC0_QPC_INTERRUPT_RESP_ERR_CAUSE);
	qpc_intr_resp_err_mask = NIC_RREG32(mmD0_NIC0_QPC_INTERRUPT_RESP_ERR_MASK);
	qpc_intr_resp_err_cause = qpc_intr_resp_err_cause & ~qpc_intr_resp_err_mask;

	txs_intr_cause = NIC_RREG32(mmD0_NIC0_TXS_INTERRUPT_CAUSE);
	txs_intr_mask = NIC_RREG32(mmD0_NIC0_TXS_INTERRUPT_MASK);
	txs_intr_cause = txs_intr_cause & ~txs_intr_cause;

	txe_intr_cause = NIC_RREG32(mmD0_NIC0_TXE_INTERRUPT_CAUSE);
	txe_intr_mask = NIC_RREG32(mmD0_NIC0_TXE_INTERRUPT_MASK);
	txe_intr_cause = txe_intr_cause & ~txe_intr_mask;

	if (rxe_sei_intr_cause) {
		for (i = 0 ; i < ARRAY_SIZE(gaudi3_nic_rxe_sei_interrupts_cause) ; i++) {
			if (!(rxe_sei_intr_cause & BIT(i)))
				continue;

			dev_warn_ratelimited(hdev->dev,
				"RXE SEI error on NIC macro %d cause: %s. cause bit %d\n",
				macro_index, gaudi3_nic_rxe_sei_interrupts_cause[i], i);
		}

		/* After writing to the SEI_INTR_CLEAR register we need to set it back to zero as
		 * it's a sticky register (the read between is done in order to flush the first
		 * write).
		 */
		NIC_WREG32(mmD0_NIC0_RXE_SEI_INTR_CLEAR, rxe_sei_intr_cause);
		NIC_RREG32(mmD0_NIC0_RXE_SEI_INTR_CLEAR);
		NIC_WREG32(mmD0_NIC0_RXE_SEI_INTR_CLEAR, 0);
	}

	if (rxb_core_sei_intr_cause) {
		for (i = 0 ; i < ARRAY_SIZE(gaudi3_nic_rxb_core_sei_interrupts_cause) ; i++) {
			if (!(rxb_core_sei_intr_cause & BIT(i)))
				continue;

			dev_warn_ratelimited(hdev->dev,
				"RXB CORE SEI error on NIC macro %d cause: %s. cause bit %d\n",
				macro_index, gaudi3_nic_rxb_core_sei_interrupts_cause[i], i);
		}

		/* After writing to the SEI_INTR_CLEAR register we need to set it back to zero
		 * as it's a sticky register (the read between is done in order to flush the first
		 * write).
		 */
		NIC_WREG32(mmD0_NIC0_RXB_CORE_SEI_INTR_CLEAR, rxb_core_sei_intr_cause);
		NIC_RREG32(mmD0_NIC0_RXB_CORE_SEI_INTR_CLEAR);
		NIC_WREG32(mmD0_NIC0_RXB_CORE_SEI_INTR_CLEAR, 0);
	}

	if (tmr_intr_cause) {
		for (i = 0 ; i < ARRAY_SIZE(gaudi3_nic_tmr_interrupts_cause) ; i++) {
			if (!(tmr_intr_cause & BIT(i)))
				continue;

			dev_warn_ratelimited(hdev->dev,
				"TMR error on NIC macro %d cause %s. cause bit %d\n",
				macro_index, gaudi3_nic_tmr_interrupts_cause[i], i);
		}

		NIC_WREG32(mmD0_NIC0_TMR_INTERRUPT_CLR, tmr_intr_cause);
	}

	if (qpc_intr_resp_err_cause) {
		for (i = 0 ; i < ARRAY_SIZE(gaudi3_nic_qpc_interrupts_resp_err_cause) ; i++) {
			if (!(qpc_intr_resp_err_cause & BIT(i)))
				continue;

			dev_warn_ratelimited(hdev->dev,
				"QPC response error on NIC macro %d cause %s. cause bit %d\n",
				macro_index, gaudi3_nic_qpc_interrupts_resp_err_cause[i], i);
		}

		NIC_WREG32(mmD0_NIC0_QPC_INTERRUPR_RESP_ERR_CLR, qpc_intr_resp_err_cause);
	}

	if (txs_intr_cause) {
		for (i = 0 ; i < ARRAY_SIZE(gaudi3_nic_txs_interrupts_cause) ; i++) {
			if (!(txs_intr_cause & BIT(i)))
				continue;

			dev_warn_ratelimited(hdev->dev,
				"TXS error on NIC macro %d cause %s. cause bit %d\n",
				macro_index, gaudi3_nic_txs_interrupts_cause[i], i);
		}

		NIC_WREG32(mmD0_NIC0_TXS_INTERRUPT_CLR, txs_intr_cause);
	}

	if (txe_intr_cause) {
		for (i = 0 ; i < ARRAY_SIZE(gaudi3_nic_txe_interrupts_cause) ; i++) {
			if (!(txe_intr_cause & BIT(i)))
				continue;

			dev_warn_ratelimited(hdev->dev,
				"TXE error on NIC macro %d cause %s. cause bit %d\n",
				macro_index, gaudi3_nic_txe_interrupts_cause[i], i);
		}

		NIC_WREG32(mmD0_NIC0_TXE_INTERRUPT_CLR, txe_intr_cause);
	}
}

void gaudi3_nic_handle_bmon_spmu_event(struct hl_device *hdev, u32 macro_index,
					struct hl_eq_intr_cause *nic_intr_cause, u64 *event_mask)
{
	int rc;

	/* For this point a profiler is not configuring the BMON and SPMU block and
	 * therefore we can't deduce which port and entity triggered the interrupt.
	 * So for now we only clear all interrupt registers to prevent interrupt flood
	 */
	rc = gaudi3_nic_ack_spmu_bmon_interrupt(hdev, macro_index);
	if (rc)
		dev_err_ratelimited(hdev->dev,
				"failed to ack nic-macro %d SPMU/BMON interrupt(rc %d)\n",
				macro_index, rc);
}

static struct hl_nic_port_funcs gaudi3_nic_port_funcs = {
	.port_hw_init = gaudi3_nic_port_hw_init,
	.port_hw_fini = gaudi3_nic_port_hw_fini,
	.phy_port_init = gaudi3_nic_phy_port_init,
	.phy_port_start_stop = gaudi3_nic_phy_port_start_stop,
	.phy_port_power_up = gaudi3_nic_phy_port_power_up,
	.phy_port_reconfig = gaudi3_nic_phy_port_reconfig,
	.phy_port_fini = gaudi3_nic_phy_port_fini,
	.phy_link_status_work = gaudi3_nic_phy_link_status_work,
	.update_qp_mtu = gaudi3_nic_update_qp_mtu,
	.user_wq_arr_unset = gaudi3_user_wq_arr_unset,
	.get_cq_id_range = gaudi3_get_cq_id_range,
	.user_cq_set = gaudi3_user_cq_set,
	.user_cq_unset = gaudi3_user_cq_unset,
	.user_cq_destroy = gaudi3_user_cq_destroy,
	.get_cnts_num = gaudi3_nic_get_cnts_num,
	.get_cnts_names = gaudi3_nic_get_cnts_names,
	.get_cnts_values = gaudi3_nic_get_cnts_values,
	.port_sw_init = gaudi3_nic_port_sw_init,
	.port_sw_fini = gaudi3_nic_port_sw_fini,
	.spmu_get_stats_info = gaudi3_nic_spmu_get_stats_info,
	.spmu_config = gaudi3_nic_spmu_config,
	.spmu_sample = gaudi3_nic_spmu_sample,
	.register_qp = gaudi3_register_qp,
	.unregister_qp = gaudi3_unregister_qp,
	.get_qp_id_range = gaudi3_get_qp_id_range,
	.eq_poll = gaudi3_eq_poll,
	.eq_dispatcher_select_dq = gaudi3_nic_eq_dispatcher_select_dq,
	.get_db_fifo_id_range = gaudi3_get_db_fifo_id_range,
	.db_fifo_set = gaudi3_db_fifo_set,
	.db_fifo_unset = gaudi3_db_fifo_unset,
	.get_db_fifo_umr = gaudi3_nic_get_db_fifo_umr,
	.get_db_fifo_modes_mask = gaudi3_get_db_fifo_modes_mask,
	.db_fifo_allocate = gaudi3_db_fifo_allocate,
	.db_fifo_free = gaudi3_db_fifo_free,
	.set_pfc = gaudi3_nic_set_pfc,
	.get_encap_id_range = gaudi3_get_encap_id_range,
	.encap_set = gaudi3_encap_set,
	.encap_unset = gaudi3_encap_unset,
	.set_ip_addr_encap = gaudi3_set_ip_addr_encap,
	.qpc_write = gaudi3_nic_qpc_write,
	.qpc_invalidate = gaudi3_nic_qpc_invalidate,
	.qpc_query = gaudi3_nic_qpc_query,
	.qpc_clear = gaudi3_nic_qpc_clear,
	.user_ccq_set = gaudi3_nic_user_ccq_set,
	.user_ccq_unset = gaudi3_nic_user_ccq_unset,
	.reset_mac_stats = gaudi3_nic_reset_mac_stats,
	.print_fec_stats = gaudi3_nic_debugfs_print_fec_stats,
	.disable_wqe_index_checker = gaudi3_nic_disable_wqe_index_checker_no_fw,
	.fill_nic_status = gaudi3_nic_fill_nic_status,
	.cfg_lock = gaudi3_nic_cfg_lock,
	.cfg_unlock = gaudi3_nic_cfg_unlock,
	.cfg_is_locked = gaudi3_nic_cfg_is_locked,
	.override_phy_readiness = gaudi3_nic_override_phy_readiness,
	.qp_pre_destroy = gaudi3_nic_qp_pre_destroy,
	.qp_post_destroy = gaudi3_nic_qp_post_destroy,
	.get_coll_qps_offset = gaudi3_nic_get_coll_qps_offset,
};

struct hl_nic_funcs gaudi3_nic_funcs = {
	.pre_core_init = gaudi3_nic_pre_core_init,
	.core_init = gaudi3_nic_core_init,
	.core_fini = gaudi3_nic_core_fini,
	.get_hw_cap = gaudi3_nic_get_hw_cap,
	.set_hw_cap = gaudi3_nic_set_hw_cap,
	.set_req_qp_ctx = gaudi3_nic_set_req_qp_ctx,
	.set_res_qp_ctx = gaudi3_nic_set_res_qp_ctx,
	.user_wq_arr_set = gaudi3_user_wq_arr_set,
	.user_set_app_params = gaudi3_user_set_app_params,
	.user_get_app_params = gaudi3_user_get_app_params,
	.phy_reset_macro = gaudi3_nic_phy_reset_macro,
	.phy_get_crc = gaudi3_nic_phy_get_crc,
	.get_phy_fw_name = gaudi3_nic_phy_get_fw_name,
	.phy_fw_load_all = gaudi3_nic_phy_fw_load_all,
	.get_default_port_speed = gaudi3_nic_get_default_port_speed,
	.sw_init = gaudi3_nic_sw_init,
	.sw_fini = gaudi3_nic_sw_fini,
	.macro_sw_init = gaudi3_nic_macro_sw_init,
	.macro_sw_fini = gaudi3_nic_macro_sw_fini,
	.kernel_ctx_init = gaudi3_nic_kernel_ctx_init,
	.kernel_ctx_fini = gaudi3_nic_kernel_ctx_fini,
	.ctx_init = gaudi3_nic_ctx_init,
	.ctx_fini = gaudi3_nic_ctx_fini,
	.qp_read = gaudi3_nic_debugfs_qp_read,
	.wqe_read = gaudi3_nic_debugfs_wqe_read,
	.set_en_core_data = gaudi3_nic_en_set_core_data,
	.request_irqs = gaudi3_nic_eq_request_irqs,
	.synchronize_irqs = gaudi3_nic_eq_sync_irqs,
	.free_irqs = gaudi3_nic_eq_free_irqs,
	.write_coll_lag_size = gaudi3_nic_debugfs_write_coll_lag_size,
	.read_coll_lag_size = gaudi3_nic_debugfs_read_coll_lag_size,
	.get_coll_qp_id_range = gaudi3_nic_get_coll_qp_id_range,
	.is_coll_conn_id = gaudi3_nic_is_coll_conn_id,
	.get_max_msg_sz = gaudi3_nic_get_max_msg_sz,
	.qp_syndrome_to_str = gaudi3_nic_qp_err_syndrom_to_str,
	.port_funcs = &gaudi3_nic_port_funcs,
};
