/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI2_NIC_H_
#define GAUDI2_NIC_H_

#include "gaudi2P.h"
#include <linux/habanalabs/gaudi2.h>
#include "../include/gaudi2/asic_reg/gaudi2_regs.h"

/* SW-30244: writing to the device memory-mapped dram using the writel or
 * writeb commands (for example) is subject to the write-combined rules, meaning
 * that writes temporarily stored in a buffer and are released together later in
 * burst mode towards the device.
 * Due to the high latencies in the PLDM such writes take a lot of time which
 * may lead to system hangs. The burst issue gets more severe if ports are
 * opened in parallel as each port accesses this memory, therefore we limit the
 * amount of pending writes by inserting reads every several writes which causes
 * the pending writes to be flushed to the device.
 */
#define NIC_MAX_COMBINED_WRITES	0x2000

#define NIC_MAX_RC_MTU		SZ_8K

#define UDP_HDR_SIZE		8

/* This is the max frame length the H/W supports (Tx/Rx) */
#define NIC_MAX_RDMA_HDRS	128
#define NIC_MAX_TNL_HDR_SIZE	32 /* Bytes */
#define NIC_MAX_TNL_HDRS	(NIC_MAX_TNL_HDR_SIZE + UDP_HDR_SIZE)
#define NIC_MAX_FRM_LEN		(NIC_MAX_RC_MTU + NIC_MAX_RDMA_HDRS)
#define NIC_MAC_MAX_FRM_LEN	(NIC_MAX_FRM_LEN + HL_EN_MAX_HEADERS_SZ + NIC_MAX_TNL_HDRS)
#define NIC_RAW_MIN_MTU		(SZ_1K - HL_EN_MAX_HEADERS_SZ)
#define NIC_RAW_MAX_MTU		(NIC_MAX_RC_MTU - HL_EN_MAX_HEADERS_SZ)

/* This is the size of an element size in the RAW buffer - note that it is different than
 * NIC_MAX_FRM_LEN, because it has to be power of 2.
 */
#define NIC_RAW_ELEM_SIZE	(2 * NIC_MAX_RC_MTU)

/* verify power of 2 */
static_assert(IS_POWER_OF_2(NIC_RAW_ELEM_SIZE));

/* Time in jiffies before concluding the transmitter is hung */
#define NIC_TX_TIMEOUT		(5 * HZ)

#define NIC_RX_RING_PKT_NUM	(1 << 8)

static_assert(IS_POWER_OF_2(NIC_RX_RING_PKT_NUM));

#define NIC_MIN_CONN_ID		1
#define NIC_MAX_CONN_ID		((1 << 13) - 1) /* 8K QPs */

#define NIC_MAX_QP_NUM		(NIC_MAX_CONN_ID + 1)

/* Number of available QPs must not exceed NIC_HW_MAX_QP_NUM */
static_assert(NIC_MAX_QP_NUM <= NIC_HW_MAX_QP_NUM);

#define NIC_MAX_WQ_ARRAY_TYPE	HL_NIC_USER_WQ_RECV

/* Allocate an extra QP to be used as dummy QP. */
#define REQ_QPC_TOTAL_PORT_SIZE	((NIC_MAX_QP_NUM + 1) * sizeof(struct gaudi2_qpc_requester))
#define RES_QPC_TOTAL_PORT_SIZE	((NIC_MAX_QP_NUM + 1) * sizeof(struct gaudi2_qpc_responder))

#define TMR_ENT_SIZE		4
#define TMR_GRANULARITY		256
#define TMR_FSM_SIZE		ALIGN(NIC_MAX_QP_NUM, DEVICE_CACHE_LINE_SIZE)
/* each timer serves two NICs, hence multiply by 2 */
#define TMR_FIFO_SIZE		ALIGN((NIC_MAX_QP_NUM * 2 * TMR_ENT_SIZE) + \
					DEVICE_CACHE_LINE_SIZE * TMR_GRANULARITY, \
					DEVICE_CACHE_LINE_SIZE)
#define TMR_FREE_NUM_ENTRIES	(TMR_FIFO_SIZE / DEVICE_CACHE_LINE_SIZE)
#define TMR_FREE_SIZE		ALIGN(TMR_FREE_NUM_ENTRIES * TMR_ENT_SIZE, \
					DEVICE_CACHE_LINE_SIZE)
#define TMR_TOTAL_MACRO_SIZE	(TMR_FSM_SIZE * 2 + TMR_FREE_SIZE + TMR_FIFO_SIZE)

#define TMR_FSM0_OFFS		0
#define TMR_FREE_OFFS		(TMR_FSM0_OFFS + 2 * TMR_FSM_SIZE)
#define TMR_FIFO_OFFS		(TMR_FREE_OFFS + TMR_FREE_SIZE)

#define TXS_ENT_SIZE		4
#define TXS_GRANULARITY		256
#define TXS_FIFO_SIZE		ALIGN((NIC_MAX_QP_NUM * 2 * TXS_ENT_SIZE) + \
					DEVICE_CACHE_LINE_SIZE * TXS_GRANULARITY, \
					DEVICE_CACHE_LINE_SIZE)
#define TXS_FREE_NUM_ENTRIES	(TXS_FIFO_SIZE / DEVICE_CACHE_LINE_SIZE)
#define TXS_FREE_SIZE		ALIGN(TXS_FREE_NUM_ENTRIES * TXS_ENT_SIZE, \
					DEVICE_CACHE_LINE_SIZE)
#define TXS_TOTAL_PORT_SIZE	(TXS_FREE_SIZE + TXS_FIFO_SIZE)

#define TXS_FREE_OFFS		0
#define TXS_FIFO_OFFS		(TXS_FREE_OFFS + TXS_FREE_SIZE)

#define TXS_NUM_PORTS		NIC_MAC_LANES
#define TXS_SCHEDQ		TXS_GRANULARITY
#define TXS_NUM_SCHEDQS		TXS_SCHEDQ

#define TXS_PORT_NUM_SCHEDQS		(TXS_NUM_SCHEDQS / TXS_NUM_PORTS)
#define TXS_PORT_NUM_SCHED_GRANS	(TXS_PORT_NUM_SCHEDQS / HL_EN_PFC_PRIO_NUM)
#define TXS_PORT_RAW_SCHED_Q		(TXS_PORT_NUM_SCHED_GRANS - QPC_RAW_SCHED_Q)
#define TXS_PORT_RES_SCHED_Q		(TXS_PORT_NUM_SCHED_GRANS - QPC_RES_SCHED_Q)
#define TXS_PORT_REQ_SCHED_Q		(TXS_PORT_NUM_SCHED_GRANS - QPC_REQ_SCHED_Q)

#define RXB_NUM_BUFFS			2880
#define RXB_BUFF_SIZE			128 /* size in bytes */
#define RXB_NUM_MTU_BUFFS		((NIC_MAX_FRM_LEN / RXB_BUFF_SIZE) + 1)
#define RXB_DROP_SMALL_TH_DEPTH		3
#define RXB_DROP_TH_DEPTH		(1 * RXB_NUM_MTU_BUFFS)
#define RXB_XOFF_TH_DEPTH		(11 * RXB_NUM_MTU_BUFFS)
#define RXB_XON_TH_DEPTH		(1 * RXB_NUM_MTU_BUFFS)
#define RXB_NUM_STATIC_CREDITS		(RXB_NUM_BUFFS / 2)

#define SECTION_ALIGN_SIZE		0x100000ull
#define NIC_DRV_BASE_ADDR(nic_drv_addr)	ALIGN(nic_drv_addr, SECTION_ALIGN_SIZE)

#define NIC_DRV_END_ADDR(nic_drv_addr, nic_drv_size) \
					ALIGN(((nic_drv_addr) + (nic_drv_size)), \
						SECTION_ALIGN_SIZE)

#define REQ_QPC_BASE_ADDR		NIC_DRV_BASE_ADDR

#define RES_QPC_BASE_ADDR(nic_drv_addr)	(REQ_QPC_BASE_ADDR(nic_drv_addr) + \
						ALIGN(NIC_NUMBER_OF_ENGINES * \
							REQ_QPC_TOTAL_PORT_SIZE, \
							SECTION_ALIGN_SIZE))

#define TMR_BASE_ADDR(nic_drv_addr)	(RES_QPC_BASE_ADDR(nic_drv_addr) + \
						ALIGN(NIC_NUMBER_OF_ENGINES * \
							RES_QPC_TOTAL_PORT_SIZE, \
							SECTION_ALIGN_SIZE))

#define TXS_BASE_ADDR(nic_drv_addr)	(TMR_BASE_ADDR(nic_drv_addr) + \
						ALIGN(NIC_NUMBER_OF_MACROS * TMR_TOTAL_MACRO_SIZE, \
							SECTION_ALIGN_SIZE))

#define WQ_BASE_ADDR(nic_drv_addr)	(TXS_BASE_ADDR(nic_drv_addr) + \
						ALIGN(NIC_NUMBER_OF_ENGINES * TXS_TOTAL_PORT_SIZE, \
							SECTION_ALIGN_SIZE))

/* Unlike the other NIC related sizes, this size is shared between all the engines */
#define WQ_BASE_SIZE(nic_drv_addr, nic_drv_size) \
			(NIC_DRV_END_ADDR(nic_drv_addr, nic_drv_size) - WQ_BASE_ADDR(nic_drv_addr))

#define WQ_BUFFER_LOG_SIZE		8
#define WQ_BUFFER_SIZE			(1 << (WQ_BUFFER_LOG_SIZE))
#define CQE_SIZE			sizeof(struct gaudi2_cqe)
#define CQ_USER_MIN_ENTRIES		128
#define NIC_CQ_RAW_IDX			0
#define NIC_CQ_RDMA_IDX			1
#define QP_WQE_NUM_REC			128
#define QP_WORK_QUEUE_SIZE		(QP_WQE_NUM_REC * sizeof(struct gaudi2_sq_wqe))
#define TX_WQE_NUM_IN_CLINE		(DEVICE_CACHE_LINE_SIZE / NIC_SEND_WQE_SIZE_MULTI_STRIDE)
#define RX_WQE_NUM_IN_CLINE		(DEVICE_CACHE_LINE_SIZE / NIC_RECV_WQE_SIZE)
#define RAW_QPN				0

#define NIC_FIFO_DB_SIZE		64
#define NIC_RX_RING_SIZE		(NIC_RX_RING_PKT_NUM * NIC_RAW_ELEM_SIZE)
#define NIC_TX_BUF_SIZE			QP_WQE_NUM_REC
#define NIC_CQ_MAX_ENTRIES		BIT(13)
#define NIC_EQ_RING_NUM_REC		BIT(18)

#define NIC_TOTAL_CQ_MEM_SIZE		(NIC_CQ_MAX_ENTRIES * CQE_SIZE)

#define NIC_CQ_USER_MIN_ENTRIES		4
#define NIC_CQ_USER_MAX_ENTRIES		NIC_CQ_MAX_ENTRIES

#define NIC_MIN_CQ_ID			NIC_CQS_NUM
#define NIC_MAX_CQ_ID			(GAUDI2_NIC_MAX_CQS_NUM - 1)

#define NIC_SKB_PAD_SIZE		187

static_assert(IS_POWER_OF_2(NIC_CQ_MAX_ENTRIES));
static_assert(IS_POWER_OF_2(NIC_EQ_RING_NUM_REC));
static_assert(NIC_CQ_RDMA_IDX < GAUDI2_NIC_MAX_CQS_NUM);
static_assert(NIC_CQ_RAW_IDX < GAUDI2_NIC_MAX_CQS_NUM);

#define USER_WQES_MIN_NUM		16
#define USER_WQES_MAX_NUM		(1 << 15) /* 32K */

#define NIC_RXE_AXUSER_AXUSER_CQ_OFFSET \
		(mmNIC0_RXE0_AXUSER_AXUSER_CQ1_HB_ASID - \
			mmNIC0_RXE0_AXUSER_AXUSER_CQ0_HB_ASID)

/* Unsecure userspace doorbell fifo IDs as reported to the user, HW IDs are 0-29 */
#define GAUDI2_MIN_DB_FIFO_ID	1
#define GAUDI2_MAX_DB_FIFO_ID	30

#define GAUDI2_DB_FIFO_SECURE_HW_ID 30
#define GAUDI2_DB_FIFO_PRIVILEGE_HW_ID 31

/* The size of the DB FIFO in bytes is constant */
#define DB_FIFO_ENTRY_SIZE	8
#define DB_FIFO_NUM_OF_ENTRIES	64
#define DB_FIFO_SIZE		(DB_FIFO_NUM_OF_ENTRIES * DB_FIFO_ENTRY_SIZE)

/* User encapsulation IDs. There are 8 encaps and 4 decap resources available per macro.
 * So for now let's allow the max of 2 encaps per port.
 */
#define GAUDI2_MIN_ENCAP_ID	0
#define GAUDI2_MAX_ENCAP_ID	1

enum qpc_req_wq_type {
	QPC_REQ_WQ_TYPE_WRITE = 1,
	QPC_REQ_WQ_TYPE_READ = 2,
	QPC_REQ_WQ_TYPE_RDV = 3
};

#define QPC_GW_MASK_REG_NUM \
		(((mmNIC0_QPC0_GW_MASK_31 - mmNIC0_QPC0_GW_MASK_0) >> 2) + 1)


#define NIC_CFG_LO_SIZE		(mmNIC0_QPC1_REQ_STATIC_CONFIG - \
					mmNIC0_QPC0_REQ_STATIC_CONFIG)

#define NIC_CFG_HI_SIZE		(mmNIC0_RXE1_CONTROL - mmNIC0_RXE0_CONTROL)

#define NIC_CFG_BASE(port, reg)					\
		((u64) (NIC_MACRO_CFG_BASE(port) +		\
		((reg < mmNIC0_RXE0_CONTROL) ?			\
		(NIC_CFG_LO_SIZE * (u64) ((port) & 1)) :	\
		(NIC_CFG_HI_SIZE * (u64) ((port) & 1)))))

#define NIC_RREG32(reg) RREG32(NIC_CFG_BASE(port, (reg)) + (reg))
#define NIC_WREG32(reg, val) WREG32(NIC_CFG_BASE(port, (reg)) + (reg), (val))

#define NIC_RMWREG32(reg, val, mask)	\
		RMWREG32(NIC_CFG_BASE(port, reg) + (reg), (val), (mask))
#define NIC_RMWREG32_SHIFTED(reg, val, mask)	\
		RMWREG32_SHIFTED(NIC_CFG_BASE(port, reg) + (reg), (val), (mask))

#define MAC_CH_OFFSET(lane) \
	((mmNIC0_MAC_CH1_MAC_PCS_BASE - \
	mmNIC0_MAC_CH0_MAC_PCS_BASE) * (lane))

#define GAUDI2_HLS2_EXTERN_PORTS_MASK 0xC00100

enum gaudi2_setup_type {
	GAUDI2_SETUP_TYPE_HLS2,
	GAUDI2_SETUP_TYPE_HL225_S_EXT_LB,
	GAUDI2_SETUP_TYPE_HL325_S_EXT_LB
};

enum gaudi2_nic_mac_fec_stats_type {
	FEC_CW_CORRECTED_ACCUM,
	FEC_CW_UNCORRECTED_ACCUM,
	FEC_CW_CORRECTED,
	FEC_CW_UNCORRECTED,
	FEC_SYMBOL_ERR_CORRECTED_LANE_0,
	FEC_SYMBOL_ERR_CORRECTED_LANE_1,
	FEC_SYMBOL_ERR_CORRECTED_LANE_2,
	FEC_SYMBOL_ERR_CORRECTED_LANE_3,
	FEC_PRE_FEC_SER_INT,
	FEC_PRE_FEC_SER_EXP,
	FEC_POST_FEC_SER_INT,
	FEC_POST_FEC_SER_EXP,
	FEC_STAT_LAST
};

enum gaudi2_nic_perf_stats_type {
	PERF_BANDWIDTH_INT,
	PERF_BANDWIDTH_FRAC,
	PERF_LATENCY_INT,
	PERF_LATENCY_FRAC,
	PERF_STAT_LAST
};

int gaudi2_nic_eq_init(struct hl_device *hdev);
void gaudi2_nic_eq_fini(struct hl_device *hdev);
void gaudi2_nic_eq_handler_register(struct gaudi2_nic_port *gaudi2_nic,
				gaudi2_nic_eq_handler eq_handler);
void gaudi2_nic_eq_handler_unregister(struct gaudi2_nic_port *gaudi2_nic);
int gaudi2_nic_debugfs_qp_read(struct hl_device *hdev, char *buf, size_t bsize);
int gaudi2_nic_debugfs_wqe_read(struct hl_device *hdev, char *buf, size_t bsize);
void gaudi2_nic_debugfs_collect_fec_stats(struct hl_nic_port *nic_port, char *buf, size_t size);
int gaudi2_nic_eq_dispatcher_register_db(struct gaudi2_nic_port *gaudi2_nic, u32 asid, u32 dbn);
int gaudi2_nic_eq_request_irqs(struct hl_device *hdev);
void gaudi2_nic_eq_sync_irqs(struct hl_device *hdev);
void gaudi2_nic_eq_free_irqs(struct hl_device *hdev);
struct hl_nic_ev_dq *gaudi2_nic_eq_dispatcher_select_dq(struct hl_nic_port *nic_port,
					const struct hl_nic_eqe *eqe);
char *gaudi2_nic_qp_err_syndrom_to_str(u32 syndrome);
int gaudi2_nic_qpc_read(struct hl_nic_port *nic_port, void *qpc, u32 qpn, bool is_req);
int gaudi2_nic_wqe_read(struct hl_nic_port *nic_port, void *wqe, u32 qpn, u32 wqe_idx, bool is_tx);
void gaudi2_nic_hw_mac_loopback_cfg(struct gaudi2_nic_port *gaudi2_nic);
int gaudi2_nic_set_info(struct hl_device *hdev, bool get_from_fw);
bool gaudi2_nic_is_macro_enabled(struct hl_nic_macro *nic_macro);

#endif /* GAUDI2_NIC_H_ */
