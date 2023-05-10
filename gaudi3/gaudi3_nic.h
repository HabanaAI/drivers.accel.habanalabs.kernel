/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI3_NIC_H_
#define GAUDI3_NIC_H_

#include "gaudi3P.h"
#include "../include/gaudi3/asic_reg/gaudi3_regs.h"

#define NIC_MAX_RC_MTU		SZ_8K

#define NIC_MAX_TNL_HDR_SIZE	32 /* Bytes */

/* This is the max frame length the H/W supports (Tx/Rx) */
#define NIC_MAX_RDMA_HDRS	234
#define NIC_MAX_FRM_LEN		(NIC_MAX_RC_MTU + NIC_MAX_RDMA_HDRS)

#define NIC_RAW_MIN_MTU		(SZ_1K - HL_EN_MAX_HEADERS_SZ)
#define NIC_RAW_MAX_MTU		(NIC_MAX_RC_MTU - HL_EN_MAX_HEADERS_SZ)

/* This is the size of an element size in the RAW buffer - note that it is different than
 * NIC_MAX_FRM_LEN, because it has to be power of 2.
 */
#define NIC_RAW_ELEM_SIZE	(2 * NIC_MAX_RC_MTU)

/* verify power of 2 */
static_assert(IS_POWER_OF_2(NIC_RAW_ELEM_SIZE));

#define NIC_MIN_CONN_ID		1
#define NIC_MAX_CONN_ID		((1 << 15) - 1) /* 32K QPs */
#define NIC_MAX_CONN_ID_NO_DRAM	((1 << 14) - 1) /* 16K QPs */

#define NIC_MAX_QP_NUM		((hdev->dram_enable ? NIC_MAX_CONN_ID : NIC_MAX_CONN_ID_NO_DRAM) + \
							1)
#define NIC_MAX_GEN_QP_NUM	(NIC_MAX_QP_NUM / 2)

/* Number of available QPs must not exceed NIC_HW_MAX_QP_NUM */
static_assert((NIC_MAX_CONN_ID + 1) <= NIC_HW_MAX_QP_NUM);

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

#define TMR_FREE_NUM_ENTRIES	(TMR_FIFO_SIZE / NIC_CACHE_LINE_SIZE)

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

#define NIC_CQ_MAX_ENTRIES		BIT(13)

#define NIC_CQ_USER_MIN_ENTRIES		4
#define NIC_CQ_USER_MAX_ENTRIES		NIC_CQ_MAX_ENTRIES

#define USER_WQES_MIN_NUM		4
#define USER_WQES_MAX_NUM		(1 << 15) /* 32K */

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

#define ELEMENT_COUNT(count)		get_resource_count(hdev, (count))

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

u64 gaudi3_nic_get_macro_ports_mask(struct hl_device *hdev, int macro_idx);
int get_resource_offset(struct hl_device *hdev, u32 port, int num_of_resources);
int get_resource_count(struct hl_device *hdev, int num_of_resources);
u32 gaudi3_nic_handle_bmon_spmu_event(struct hl_device *hdev, u32 macro_index);
int gaudi3_nic_set_info(struct hl_device *hdev, bool get_from_fw);

#endif /* GAUDI3_NIC_H_ */
