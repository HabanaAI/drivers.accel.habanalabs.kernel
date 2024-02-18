/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2018-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI_CN_H_
#define GAUDI_CN_H_

#include "gaudiP.h"
#include "../include/gaudi/asic_reg/gaudi_regs.h"

#define NIC_MAX_RC_MTU		(SZ_8K - DEVICE_CACHE_LINE_SIZE)
#define NIC_MAX_FRM_LEN		(NIC_MAX_RC_MTU + DEVICE_CACHE_LINE_SIZE)
#define NIC_RAW_MIN_MTU		(SZ_1K - HBL_EN_MAX_HEADERS_SZ)
#define NIC_RAW_MAX_MTU		(NIC_MAX_FRM_LEN - HBL_EN_MAX_HEADERS_SZ)
#define NIC_RAW_ELEM_SIZE	(NIC_RAW_MAX_MTU + HBL_EN_MAX_HEADERS_SZ)

/* verify power of 2 */
static_assert(IS_POWER_OF_2(NIC_RAW_ELEM_SIZE));

#define NIC_MIN_CONN_ID		1
#define NIC_MAX_CONN_ID		((1 << 10) - 1) /* 1K QPs */

#define NIC_MAX_QP_NUM		(NIC_MAX_CONN_ID + 1)

/* Number of available QPs must not exceed NIC_HW_MAX_QP_NUM */
static_assert(NIC_MAX_QP_NUM <= NIC_HW_MAX_QP_NUM);

/* The '*_SIZE' defines are per NIC port */

struct gaudi_qpc_requester {
	__le64	data[8];
};

struct gaudi_qpc_responder {
	__le64	data[4];
};

struct gaudi_sq_wqe {
	__le64	data[4];
};

#define REQ_QPC_BASE_SIZE	(NIC_MAX_QP_NUM * sizeof(struct gaudi_qpc_requester))
#define RES_QPC_BASE_SIZE	(NIC_MAX_QP_NUM * sizeof(struct gaudi_qpc_responder))
#define SWQ_BASE_SIZE		(WQ_BUFFER_SIZE * sizeof(struct gaudi_sq_wqe))
#define SB_BASE_SIZE		(WQ_BUFFER_SIZE * NIC_RAW_ELEM_SIZE)

#define TMR_BASE_SIZE		(TMR_FSM_ENGINE_OFFS + TMR_FSM_SIZE)

#define TMR_FSM_ENGINE_OFFS	(1 << 22) /* H/W constraint */

#define TMR_FSM_SIZE		ALIGN(NIC_HW_MAX_QP_NUM, DEVICE_CACHE_LINE_SIZE)
#define TMR_FREE_SIZE		ALIGN(TMR_FREE_NUM_ENTRIES * 4, DEVICE_CACHE_LINE_SIZE)
/* each timer serves two NICs, hence multiply by 2 */
#define TMR_FIFO_SIZE		ALIGN((NIC_MAX_QP_NUM * 2 * 4), DEVICE_CACHE_LINE_SIZE)
#define TMR_FIFO_STATIC_SIZE	(DEVICE_CACHE_LINE_SIZE * TMR_GRANULARITY)

#define TMR_FREE_NUM_ENTRIES	(TMR_FIFO_SIZE / DEVICE_CACHE_LINE_SIZE)
#define TMR_GRANULARITY		128

#define TXS_BASE_SIZE		(TXS_FREE_SIZE + TXS_FIFO_SIZE + TXS_FIFO_STATIC_SIZE)


#define TXS_FREE_SIZE		ALIGN(TXS_FREE_NUM_ENTRIES * 4, DEVICE_CACHE_LINE_SIZE)
/* TXS serves requester and responder QPs, hence multiply by 2 */
#define TXS_FIFO_SIZE		ALIGN((NIC_MAX_QP_NUM * 2 * 4), DEVICE_CACHE_LINE_SIZE)
#define TXS_FIFO_STATIC_SIZE	(DEVICE_CACHE_LINE_SIZE * TXS_GRANULARITY)

#define TXS_FREE_NUM_ENTRIES	(TXS_FIFO_SIZE / DEVICE_CACHE_LINE_SIZE)
#define TXS_GRANULARITY		256

#define SECTION_ALIGN_SIZE	0x100000ull
#define NIC_DRV_BASE_ADDR	ALIGN(NIC_DRV_ADDR, SECTION_ALIGN_SIZE)

#define REQ_QPC_BASE_ADDR	NIC_DRV_BASE_ADDR

#define RES_QPC_BASE_ADDR	ALIGN(REQ_QPC_BASE_ADDR + NIC_NUMBER_OF_ENGINES * \
					REQ_QPC_BASE_SIZE, SECTION_ALIGN_SIZE)

#define TMR_BASE_ADDR		ALIGN(RES_QPC_BASE_ADDR + NIC_NUMBER_OF_ENGINES * \
					RES_QPC_BASE_SIZE, SECTION_ALIGN_SIZE)

#define TXS_BASE_ADDR		ALIGN(TMR_BASE_ADDR + NIC_NUMBER_OF_MACROS * TMR_BASE_SIZE, \
					SECTION_ALIGN_SIZE)

#define SWQ_BASE_ADDR		ALIGN(TXS_BASE_ADDR + NIC_NUMBER_OF_ENGINES * TXS_BASE_SIZE, \
					SECTION_ALIGN_SIZE)

#define SB_BASE_ADDR		ALIGN(SWQ_BASE_ADDR + NIC_NUMBER_OF_PORTS * SWQ_BASE_SIZE, \
					SECTION_ALIGN_SIZE)

#define NIC_DRV_END_ADDR	ALIGN(SB_BASE_ADDR + NIC_NUMBER_OF_PORTS * SB_BASE_SIZE, \
					SECTION_ALIGN_SIZE)

#define WQ_BUFFER_LOG_SIZE		8
#define WQ_BUFFER_SIZE			(1 << WQ_BUFFER_LOG_SIZE)
#define RX_MSI_IDX			(GAUDI_EVENT_QUEUE_MSI_IDX + 1)
#define RX_MSI_ADDRESS			(mmPCIE_MSI_INTR_0 + RX_MSI_IDX * 4)

/* On Gaudi1, the F/W does extra validation that the received nic_status packet size is equal to
 * the size of the cpcpu_nic_status structure.
 * On later ASICs, this structure will be extended with additional fields, therefore we will be
 * exposed to a compatibility issue - New driver will send a new packet size but old F/W will
 * expect an old packet size and will fail the packet.
 * In order to overcome this issue, the F/W will remove the extra validation for later ASICs and
 * for Gaudi1, the driver will always send the size of the "old" packet (40 bytes).
 *
 * Therefore, this value must not changed !!!
 */
#define NIC_STATUS_PACKET_SIZE		40

/*
 * Some registers are specific for each NIC port, and some are shared for all
 * the NIC macro (a pair of even and odd port).
 * Therefore we need different methods to handle these registers.
 */

/* read/write port specific registers */
#define NIC_CFG_SIZE		(mmNIC0_QPC1_REQ_STATIC_CONFIG - mmNIC0_QPC0_REQ_STATIC_CONFIG)
#define NIC_CFG_BASE(port)	(NIC_MACRO_CFG_SIZE * ((port) >> 1) + NIC_CFG_SIZE * ((port) & 1))

#define NIC_RREG32(reg)		RREG32(NIC_CFG_BASE(port) + (reg))
#define NIC_WREG32(reg, val)	WREG32(NIC_CFG_BASE(port) + (reg), (val))
#define NIC_RMWREG32(reg, val, mask)	RMWREG32(NIC_CFG_BASE(port) + (reg), (val), (mask))

#endif /* GAUDI_CN_H_ */
