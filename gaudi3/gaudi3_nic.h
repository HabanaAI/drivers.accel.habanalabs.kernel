/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI3_NIC_H_
#define GAUDI3_NIC_H_

#include "gaudi3P.h"
#include <linux/habanalabs/gaudi3.h>
#include "../include/gaudi3/asic_reg/gaudi3_regs.h"

#define DB_FIFO_SIZE	SZ_4K

#define DB_FIFO_MIN_GRANULARITY		8

#define DB_FIFO_ETH_ENTRY_SIZE		8
#define DB_FIFO_ETH_NUM_ENTRIES		8

#define DB_FIFO_RDMA_ENTRY_SIZE		8
#define DB_FIFO_RDMA_NUM_ENTRIES	16

#define DB_FIFO_CC_ENTRY_SIZE		16
#define DB_FIFO_CC_NUM_ENTRIES		8

#define DB_FIFO_ETH_SIZE			(DB_FIFO_ETH_ENTRY_SIZE * DB_FIFO_ETH_NUM_ENTRIES)
#define DB_FIFO_RDMA_SIZE			(DB_FIFO_RDMA_ENTRY_SIZE * \
							 DB_FIFO_RDMA_NUM_ENTRIES)
#define DB_FIFO_CC_SIZE				(DB_FIFO_CC_ENTRY_SIZE * DB_FIFO_CC_NUM_ENTRIES)
#define DB_FIFO_COLL_OPS_SHORT_SIZE		256
#define DB_FIFO_COLL_OPS_LONG_SIZE		512
#define DB_FIFO_COLL_OPS_DIR_SHORT_SIZE		256
#define DB_FIFO_COLL_OPS_DIR_LONG_SIZE		512
#define DB_FIFO_DIRECT_WQ_LINEAR_SIZE		256
#define DB_FIFO_DIRECT_WQ_MULTI_STRIDE_SIZE	256

#define DB_FIFO_CI_COUNTER_SIZE			BIT(11)

#define NIC_MAX_RC_MTU		SZ_8K

/* This is the max frame length the H/W supports (Tx/Rx) */
#define NIC_MAX_RDMA_HDRS	128
#define NIC_MAX_FRM_LEN		(NIC_MAX_RC_MTU + NIC_MAX_RDMA_HDRS)
#define NIC_RAW_MIN_MTU		(SZ_1K - HL_EN_MAX_HEADERS_SZ)
#define NIC_RAW_MAX_MTU		(NIC_MAX_RC_MTU - HL_EN_MAX_HEADERS_SZ)

/* This is the size of an element size in the RAW buffer - note that it is different than
 * NIC_MAX_FRM_LEN, because it has to be power of 2.
 */
#define NIC_RAW_ELEM_SIZE	(2 * NIC_MAX_RC_MTU)

/* verify power of 2 */
static_assert(IS_POWER_OF_2(NIC_RAW_ELEM_SIZE));

#define NIC_RX_RING_PKT_NUM	(1 << 8)

#define NIC_MAX_QP_NUM		(NIC_MAX_CONN_ID + 1)
#define NIC_MAX_GEN_QP_NUM	(NIC_MAX_QP_NUM / 2)
#define NIC_MAX_COLL_QP_NUM	(NIC_MAX_QP_NUM - NIC_MAX_GEN_QP_NUM)

#define NIC_MAX_TNL_HDR_SIZE	32 /* Bytes */

#define NIC_DEFAULT_COLL_LAG_SIZE	0x3

/* Number of available QPs must not exceed NIC_HW_MAX_QP_NUM */
static_assert(NIC_MAX_QP_NUM <= NIC_HW_MAX_QP_NUM);

#define NIC_MAX_WQ_ARRAY_TYPE	HL_NIC_USER_COLL_SCALE_OUT_WQ_RECV

/* The '*_SIZE' defines are per NIC port */
/* TODO: change to gaudi3 values */
#define REQ_QPC_BASE_SIZE	(NIC_MAX_QP_NUM * \
					(sizeof(struct gaudi3_qpc_requester) - \
					sizeof(struct gaudi3_qpc_swl_requester)))
#define REQ_QPC_SWL_BASE_SIZE	(NIC_MAX_QP_NUM * sizeof(struct gaudi3_qpc_swl_requester))
#define RES_QPC_BASE_SIZE	(NIC_MAX_QP_NUM * sizeof(struct gaudi3_qpc_responder))

#define TMR_ENTRY_SIZE		4
#define TMR_BASE_SIZE		(TMR_FSM_SIZE + TMR_FREE_SIZE + TMR_FIFO_SIZE)
#define TMR_FSM_SIZE		ALIGN(NIC_MAX_QP_NUM, DEVICE_CACHE_LINE_SIZE)
#define TMR_FREE_SIZE		ALIGN(TMR_FREE_NUM_ENTRIES * TMR_ENTRY_SIZE, DEVICE_CACHE_LINE_SIZE)
#define TMR_FIFO_SIZE		ALIGN((NIC_MAX_QP_NUM * TMR_ENTRY_SIZE), DEVICE_CACHE_LINE_SIZE)

#define TMR_FSM0_OFFS		0
#define TMR_FREE_OFFS		(TMR_FSM0_OFFS + TMR_FSM_SIZE)
#define TMR_FIFO_OFFS		(TMR_FREE_OFFS + TMR_FREE_SIZE)
#define TMR_FREE_NUM_ENTRIES	(TMR_FIFO_SIZE / NIC_CACHE_LINE_SIZE)
#define TMR_GRANULARITY		128

#define TXS_BASE_SIZE		(TXS_FREE_SIZE + TXS_FIFO_SIZE)
#define TXS_FREE_SIZE		ALIGN(TXS_FREE_NUM_ENTRIES * 4, \
					DEVICE_CACHE_LINE_SIZE)
#define TXS_FIFO_SIZE		ALIGN((NIC_MAX_QP_NUM * 4 * 2) + \
					DEVICE_CACHE_LINE_SIZE * \
					TXS_GRANULARITY, \
					DEVICE_CACHE_LINE_SIZE)

#define TXS_FREE_OFFS		0
#define TXS_FIFO_OFFS		(TXS_FREE_OFFS + TXS_FREE_SIZE)

#define TXS_FREE_NUM_ENTRIES	(TXS_FIFO_SIZE / NIC_CACHE_LINE_SIZE)
#define TXS_GRANULARITY		256
#define TXS_NUM_PORTS		NIC_MAC_LANES
#define TXS_SCHEDQ		256
#define TXS_NUM_EVICTED_SCHEDQS	16
/* for code clarity, squash the 15 reserved Qs into evicted-lpbk definition */
#define TXS_NUM_EVICTED_LPBK_SCHEDQS	16
#define TXS_NUM_SCHEDQS		\
		(TXS_SCHEDQ - TXS_NUM_EVICTED_SCHEDQS - TXS_NUM_EVICTED_LPBK_SCHEDQS)

#define TXS_PORT_NUM_SCHEDQS		(TXS_NUM_SCHEDQS / TXS_NUM_PORTS)
#define TXS_PORT_NUM_SCHED_GRANS	(TXS_PORT_NUM_SCHEDQS / HL_EN_PFC_PRIO_NUM)
#define TXS_PORT_NUM_REQ_SCHED_GRANS	(TXS_PORT_NUM_SCHED_GRANS - 2)
#define TXS_PORT_RAW_SCHED_Q		(TXS_PORT_NUM_SCHED_GRANS - QPC_RAW_SCHED_Q)
#define TXS_PORT_RES_SCHED_Q		(TXS_PORT_NUM_SCHED_GRANS - QPC_RES_SCHED_Q)
#define TXS_PORT_REQ_SCHED_Q		(TXS_PORT_NUM_SCHED_GRANS - QPC_REQ_SCHED_Q)

/* each RXB credit represents 512Bytes */
#define RXB_BUFF_SIZE		512
#define RXB_NUM_BUFFS		512
#define RXB_MTU_NUM_CREDS	(ALIGN(NIC_MAX_FRM_LEN, RXB_BUFF_SIZE) / RXB_BUFF_SIZE)
/* Currently we support up to 2 ports per-macro */
#define RXB_MAX_NUM_PORTS	(is_400g_mode(hdev) ? 1 : 2)
#define RXB_MAX_NUM_PRIOS	(HL_EN_PFC_PRIO_NUM - 1)
#define RXB_NUM_RES_MTU		3
#define RXB_NUM_PORT_PRIO_REGS	8
/* Only prio 0 gets static credits */
#define RXB_NUM_STATIC_CRED_CONFS	(RXB_MAX_NUM_PORTS * 1)

#define SECTION_ALIGN_SIZE		0x100000ull
#define NIC_DRV_BASE_ADDR(nic_drv_addr)	ALIGN(nic_drv_addr, SECTION_ALIGN_SIZE)

#define NIC_DRV_END_ADDR(nic_drv_addr, nic_drv_size) \
					ALIGN(((nic_drv_addr) + (nic_drv_size)), \
							SECTION_ALIGN_SIZE)

#define REQ_QPC_BASE_ADDR		NIC_DRV_BASE_ADDR

#define RES_QPC_BASE_ADDR(nic_drv_addr)	(REQ_QPC_BASE_ADDR(nic_drv_addr) + \
						ALIGN(NIC_NUMBER_OF_ENGINES * REQ_QPC_BASE_SIZE, \
							SECTION_ALIGN_SIZE))

#define REQ_QPC_SWL_BASE_ADDR(nic_drv_addr) \
					(RES_QPC_BASE_ADDR(nic_drv_addr) + \
						ALIGN(NIC_NUMBER_OF_ENGINES * RES_QPC_BASE_SIZE, \
							SECTION_ALIGN_SIZE))

#define TMR_BASE_ADDR(nic_drv_addr)	(REQ_QPC_SWL_BASE_ADDR(nic_drv_addr) + \
						ALIGN(NIC_NUMBER_OF_ENGINES * \
							REQ_QPC_SWL_BASE_SIZE, SECTION_ALIGN_SIZE))

#define TXS_BASE_ADDR(nic_drv_addr)	(TMR_BASE_ADDR(nic_drv_addr) + \
						ALIGN(NIC_NUMBER_OF_MACROS * TMR_BASE_SIZE, \
							SECTION_ALIGN_SIZE))

#define WQ_BASE_ADDR(nic_drv_addr)	(TXS_BASE_ADDR(nic_drv_addr) + \
						ALIGN(NIC_NUMBER_OF_ENGINES * TXS_BASE_SIZE, \
							SECTION_ALIGN_SIZE))

/* Unlike the other NIC related sizes, this size is shared between all the engines */
#define WQ_BASE_SIZE(nic_drv_addr, nic_drv_size) \
			(NIC_DRV_END_ADDR(nic_drv_addr, nic_drv_size) - WQ_BASE_ADDR(nic_drv_addr))

#define CQE_SIZE			sizeof(struct gaudi3_cqe)
#define CQ_USER_MIN_ENTRIES		128
#define QP_WQE_NUM_REC			128
#define QP_WORK_QUEUE_SIZE		(QP_WQE_NUM_REC * sizeof(struct gaudi3_sq_wqe))

/* Gaud3 ETH fifo size contains 8 entries. Each entry is 8 bytes.
 * We define it as 16 entries of 4 bytes to mach the HW granularity.
 */
#define NIC_FIFO_DB_SIZE		(DB_FIFO_ETH_SIZE / 4)
#define NIC_RX_RING_SIZE		(NIC_RX_RING_PKT_NUM * NIC_RAW_ELEM_SIZE)
#define NIC_TX_BUF_SIZE			QP_WQE_NUM_REC
#define NIC_CQ_MAX_ENTRIES		BIT(13)
#define NIC_EQ_RING_NUM_REC		BIT(18)

#define NIC_CQ_USER_MIN_ENTRIES		4
#define NIC_CQ_USER_MAX_ENTRIES		NIC_CQ_MAX_ENTRIES

#define RXE_CQ_AXI_USER_OFFSET		(mmD0_NIC0_RXE_CQ_AXI_USER_1 - mmD0_NIC0_RXE_CQ_AXI_USER_0)

#define USER_WQES_MIN_NUM		4
#define USER_WQES_MAX_NUM		(1 << 15) /* 32K */

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

/* 2 for ilog2(sizeof(u32)) */
#define QPC_GW_MASK_REG_NUM \
		(((mmD0_NIC0_QPC_GW_MASK_95 - mmD0_NIC0_QPC_GW_MASK_0) >> 2) + 1)

enum db_fifo_type_hw {
	DB_FIFO_TYPE_DOORBELL,
	DB_FIFO_TYPE_BBR,
	DB_FIFO_TYPE_SCHEDULER_DESC,
	DB_FIFO_TYPE_LINER_WQE,
	DB_FIFO_TYPE_MULTISTRIDE_WQE,
	DB_FIFO_TYPE_DIRECT_PATCHER,
};

/* read/write port specific registers */
#define NIC_PORT_DIE_OFFSET(port)	(((port) >= NIC_NUM_PORTS_PER_DIE) ? NIC_DIE_OFFSET : 0)
#define NIC_MACRO_OFFSET(macro)		((macro) * NIC_OFFSET)
#define NIC_PORT_TO_MACRO(port)		((port) / NIC_PORTS_PER_MACRO)
#define NIC_PORT_TO_MACRO_OFFSET(port)	\
				NIC_MACRO_OFFSET(NIC_PORT_TO_MACRO(port) % NIC_NUM_MACROS_PER_DIE)
#define NIC_CFG_BASE(port)		(NIC_PORT_DIE_OFFSET(port) + NIC_PORT_TO_MACRO_OFFSET(port))

#define NIC_REG(reg)			(NIC_CFG_BASE(port) + (reg))
#define NIC_RREG32(reg)			RREG32(NIC_REG(reg))
#define NIC_WREG32(reg, val)		WREG32(NIC_REG(reg), (val))
#define NIC_RMWREG32(reg, val, mask)	RMWREG32(NIC_REG(reg), (val), (mask))
#define NIC_RMWREG32_SHIFTED(reg, val, mask)	RMWREG32_SHIFTED(NIC_REG(reg), (val), (mask))

#define NIC_OFFSET_RREG32_AUX(reg, stride)	NIC_RREG32((reg) + (offset) * (stride))
#define NIC_OFFSET_WREG32_AUX(reg, val, stride)	NIC_WREG32((reg) + (offset) * (stride), (val))
#define NIC_OFFSET_RMWREG32_AUX(reg, val, mask, stride) \
					NIC_RMWREG32((reg) + (offset) * (stride), (val), (mask))

#define NIC_OFFSET_RREG32(reg)		NIC_OFFSET_RREG32_AUX((reg), 4)
#define NIC_OFFSET_WREG32(reg, val)	NIC_OFFSET_WREG32_AUX((reg), (val), 4)
#define NIC_OFFSET_RMWREG32(reg, val, mask)	NIC_OFFSET_RMWREG32_AUX((reg), (val), (mask), 4)
#define NIC_MAC_RREG32(reg_off, lane)	\
		NIC_RREG32(mmD0_NIC0_MAC_CH_MAC_CH0_BASE + (reg_off) + MAC_CH_OFFSET(lane))
#define NIC_MAC_WREG32(reg_off, lane, val)	\
		NIC_WREG32(mmD0_NIC0_MAC_CH_MAC_CH0_BASE + (reg_off) + MAC_CH_OFFSET(lane), (val))
#define NIC_MAC_RMWREG32(reg_off, lane, val, mask)	\
		NIC_RMWREG32(mmD0_NIC0_MAC_CH_MAC_CH0_BASE + (reg_off) + MAC_CH_OFFSET(lane), \
				(val), (mask))

#define MAC_CH_OFFSET(lane) \
		((mmD0_NIC0_MAC_CH_MAC_CH1_BASE - mmD0_NIC0_MAC_CH_MAC_CH0_BASE) * (lane))

#define ELEMENT_NUM(diff, size)	(((diff) >> ilog2(size)) + 1)
/* div by 4 (register size) and add one to include the first one */
#define REG_NUM(diff)	ELEMENT_NUM(diff, 4)
#define QPC_EQ_NUM	REG_NUM(mmD0_NIC0_QPC_EVENT_QUE_PI_ADDR_63_32_3 - \
				mmD0_NIC0_QPC_EVENT_QUE_PI_ADDR_63_32_0)

#define ELEMENT_OFFSET(port, count)	get_resource_offset(hdev, (port), (count))
#define ELEMENT_COUNT(count)		get_resource_count(hdev, (count))

#define GAUDI3_NIC_EQ_INTERRUPT_S(port)	ELEMENT_OFFSET((port), QPC_EQ_NUM)
#define GAUDI3_NIC_EQ_INTERRUPT_M(port)	BIT(GAUDI3_NIC_EQ_INTERRUPT_S(port))
/* The meaning of the bits in the interrupt mask register is:
 *    [3..0] - EQ event interrupt (1 bit per EQ)
 *    [4..7] - EQ error interrupt (1 bit per EQ)
 * i.e. EQ error shift = EQ event shift + 4
 */
#define GAUDI3_NIC_EQ_ERR_INTERRUPT_M(port)	BIT(GAUDI3_NIC_EQ_INTERRUPT_S(port) + 4)

#define QPC_DB_FIFO_NUM	REG_NUM(mmD0_NIC0_QPC_DB_FIFO_UPD_ADDR_MSB_23 - \
				mmD0_NIC0_QPC_DB_FIFO_UPD_ADDR_MSB_0)
#define GAUDI3_RAW_OFFSET		0
#define GAUDI3_DB_FIFO_SECURE_HW_ID(port)	\
				(GAUDI3_RAW_OFFSET + ELEMENT_OFFSET((port), QPC_DB_FIFO_NUM))

#define RDMA_OFFSET		1

#define TXE_SQ_NUM	REG_NUM(mmD0_NIC0_TXE_SQ_BASE_ADDRESS_31_0_15 - \
				mmD0_NIC0_TXE_SQ_BASE_ADDRESS_31_0_0)
#define RXE_WQ_NUM	REG_NUM(mmD0_NIC0_RXE_WIN_WQ_BASE_HI_15 - mmD0_NIC0_RXE_WIN_WQ_BASE_HI_0)

#define GAUDI3_TXE_WQ_RDMA_IDX(port)		(RDMA_OFFSET + ELEMENT_OFFSET((port), TXE_SQ_NUM))
#define GAUDI3_RXE_WQ_RDMA_IDX(port)		(RDMA_OFFSET + ELEMENT_OFFSET((port), RXE_WQ_NUM))
#define GAUDI3_TXE_COLL_WQ_RDMA_IDX(port)		(GAUDI3_TXE_WQ_RDMA_IDX(port) + 1)
#define GAUDI3_RXE_COLL_WQ_RDMA_IDX(port)		(GAUDI3_RXE_WQ_RDMA_IDX(port) + 1)
#define GAUDI3_TXE_COLL_SCALE_OUT_WQ_RDMA_IDX(port)	(GAUDI3_TXE_COLL_WQ_RDMA_IDX(port) + 1)
#define GAUDI3_RXE_COLL_SCALE_OUT_WQ_RDMA_IDX(port)	(GAUDI3_RXE_COLL_WQ_RDMA_IDX(port) + 1)

#define MAC_GLOB_STAT_TX_NUM	REG_NUM((mmD0_NIC0_MAC_GLOB_STAT_TX3_BASE + \
					mmNIC_MAC_GLOB_STAT_TX3_ETHERSTATSPKTS_7) - \
					(mmD0_NIC0_MAC_GLOB_STAT_TX0_BASE + \
					mmNIC_MAC_GLOB_STAT_TX0_ETHERSTATSOCTETS_4))

#define MAC_GLOB_STAT_RX_NUM	REG_NUM((mmD0_NIC0_MAC_GLOB_STAT_RX3_BASE + \
					mmNIC_MAC_GLOB_STAT_RX3_AMACCONTROLFRAMESRECEIVED_3) - \
					(mmD0_NIC0_MAC_GLOB_STAT_RX0_BASE + \
					mmNIC_MAC_GLOB_STAT_RX0_ETHERSTATSOCTETS))

#define VALID_WQE_OPCODES \
	(BIT(WQE_SEND) | BIT(WQE_LINEAR) | BIT(WQE_STRIDE) | BIT(WQE_MULTI_STRIDE) | \
	BIT(WQE_RENDEZVOUS_WR) | BIT(WQE_RENDEZVOUS_RD) | BIT(WQE_ATOMIC_FETCH_ADD) | \
	BIT(WQE_MULTI_STRIDE_DUAL))

#define WQ_WR_VALID_WQE_OPCODES \
	(BIT(WQE_SEND) | BIT(WQE_LINEAR) | BIT(WQE_STRIDE) | BIT(WQE_MULTI_STRIDE) | \
	BIT(WQE_RENDEZVOUS_WR) | BIT(WQE_RENDEZVOUS_RD) | BIT(WQE_ATOMIC_FETCH_ADD) | \
	BIT(WQE_MULTI_STRIDE_DUAL))

#define WQ_RD_VALID_WQE_OPCODES \
	(BIT(WQE_LINEAR) | BIT(WQE_STRIDE) | BIT(WQE_MULTI_STRIDE) | BIT(WQE_MULTI_STRIDE_DUAL))

#define WQ_RDV_VALID_WQE_OPCODES \
	(BIT(WQE_LINEAR) | BIT(WQE_MULTI_STRIDE_DUAL))

#define UPSCALE_VALID_WQE_OPCODES \
	(BIT(WQE_SEND) | BIT(WQE_LINEAR) | BIT(WQE_STRIDE) | BIT(WQE_MULTI_STRIDE) | \
	BIT(WQE_RENDEZVOUS_WR) | BIT(WQE_RENDEZVOUS_RD) | BIT(WQE_MULTI_STRIDE_DUAL))

#define PLAIN_RDMA_VALID_WQE_OPCODES \
	(BIT(WQE_SEND) | BIT(WQE_LINEAR))

#define GAUDI3_PORTS_MASK_200G 0xFFFFFF
#define GAUDI3_PORTS_MASK_400G 0xFFF
#define GAUDI3_HLS3_EXTERN_PORTS_MASK_200G_16TB 0x3003C0
#define GAUDI3_HLS3_EXTERN_PORTS_MASK_200G_48TB GAUDI3_PORTS_MASK_200G
#define GAUDI3_HLS3_EXTERN_PORTS_MASK_400G_48TB GAUDI3_PORTS_MASK_400G

enum gaudi3_setup_type {
	GAUDI3_SETUP_TYPE_HLS3,
};

u64 gaudi3_nic_get_macro_ports_mask(struct hl_nic_macro *nic_macro);
int get_lane_offset(struct gaudi3_nic_port *gaudi3_nic);
int get_resource_offset(struct hl_device *hdev, u32 port, int num_of_resources);
int get_resource_count(struct hl_device *hdev, int num_of_resources);
u32 get_port_from_lane(struct hl_nic_macro *nic_macro, u8 lane);
int gaudi3_nic_eq_init(struct hl_device *hdev);
void gaudi3_nic_eq_fini(struct hl_device *hdev);
int gaudi3_nic_eq_request_irqs(struct hl_device *hdev);
void gaudi3_nic_eq_sync_irqs(struct hl_device *hdev);
void gaudi3_nic_eq_free_irqs(struct hl_device *hdev);
void gaudi3_nic_eq_handler_register(struct gaudi3_nic_port *gaudi3_nic,
				gaudi3_nic_eq_handler eq_handler);
void gaudi3_nic_eq_handler_unregister(struct gaudi3_nic_port *gaudi3_nic);
int gaudi3_nic_eq_dispatcher_register_db(struct gaudi3_nic_port *gaudi3_nic, u32 asid, u32 dbn);
struct hl_nic_ev_dq *gaudi3_nic_eq_dispatcher_select_dq(struct hl_nic_port *nic_port,
					const struct hl_nic_eqe *eqe);
int gaudi3_nic_qpc_read(struct hl_nic_port *nic_port, void *qpc, u32 qpn, bool is_req);
void gaudi3_nic_eq_reset_ring(struct gaudi3_nic_port *gaudi3_nic);
bool is_coll_qp_in_reset(struct hl_nic_port *nic_port, u32 qpn);
void gaudi3_handle_nic_port_reset_locked(struct hl_nic_port *nic_port);
void gaudi3_handle_nic_spi_event(struct hl_device *hdev, u32 macro_index,
					struct hl_eq_intr_cause *nic_intr_cause);
void gaudi3_handle_nic_sei_error_event(struct hl_device *hdev, u32 macro_index,
						struct hl_eq_intr_cause *nic_intr_cause);
void gaudi3_nic_handle_bmon_spmu_event(struct hl_device *hdev, u32 macro_index,
					struct hl_eq_intr_cause *nic_intr_cause, u64 *event_mask);
char *gaudi3_nic_qp_err_src_to_str(u32 syndrome);
char *gaudi3_nic_qp_err_syndrom_to_str(u32 syndrome);
/**
 * enum db_fifo_source - Describes db fifo's source.
 * @DB_FIFO_SRC_UMR: source of the db fifo is from the UMR registers.
 * @DB_FIFO_SRC_DUP: source of the db fifo is from the DUP registers.
 */

enum db_fifo_source {
	DB_FIFO_SRC_UMR,
	DB_FIFO_SRC_DUP,
};

#endif /* GAUDI3_NIC_H_ */
