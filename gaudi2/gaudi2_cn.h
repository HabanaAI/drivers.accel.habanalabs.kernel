/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI2_CN_H_
#define GAUDI2_CN_H_

#include "gaudi2P.h"
#include "../include/gaudi2/asic_reg/gaudi2_regs.h"

/* Parameters for bring-up (not to be upstreamed) */
#define NIC_MAX_RC_MTU		SZ_8K
/* This is the max frame length the H/W supports (Tx/Rx) */
#define NIC_MAX_RDMA_HDRS	128
#define NIC_MAX_FRM_LEN		(NIC_MAX_RC_MTU + NIC_MAX_RDMA_HDRS)

/* TODO: SW-153130 - remove once SW-153128 is done - START*/
#define NIC_MAX_CONN_ID		((1 << 13) - 1) /* 8K QPs */
#define NIC_MAX_QP_NUM		(NIC_MAX_CONN_ID + 1)
/* Number of available QPs must not exceed NIC_HW_MAX_QP_NUM */
static_assert(NIC_MAX_QP_NUM <= NIC_HW_MAX_QP_NUM);
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

#define TXS_ENT_SIZE		4
#define TXS_GRANULARITY		256
#define TXS_FIFO_SIZE		ALIGN((NIC_MAX_QP_NUM * 2 * TXS_ENT_SIZE) + \
					DEVICE_CACHE_LINE_SIZE * TXS_GRANULARITY, \
					DEVICE_CACHE_LINE_SIZE)
#define TXS_FREE_NUM_ENTRIES	(TXS_FIFO_SIZE / DEVICE_CACHE_LINE_SIZE)
#define TXS_FREE_SIZE		ALIGN(TXS_FREE_NUM_ENTRIES * TXS_ENT_SIZE, \
					DEVICE_CACHE_LINE_SIZE)
#define TXS_TOTAL_PORT_SIZE	(TXS_FREE_SIZE + TXS_FIFO_SIZE)

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
/* TODO: SW-153130 - remove once SW-153128 is done - END*/

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

/* Parameters for simulator (not to be upstreamed) */
#define GAUDI2_HLS2_EXTERN_PORTS_MASK 0xC00100

int gaudi2_cn_set_info(struct hl_device *hdev, bool get_from_fw);
int gaudi2_cn_handle_sw_error_event(struct hl_device *hdev, u16 event_type, u8 macro_index,
					struct hl_eq_nic_intr_cause *nic_intr_cause);
int gaudi2_cn_handle_axi_error_response_event(struct hl_device *hdev, u16 event_type,
				u8 macro_index, struct hl_eq_nic_intr_cause *nic_intr_cause);

#endif /* GAUDI2_CN_H_ */
