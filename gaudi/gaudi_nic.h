/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2018-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI_NIC_H_
#define GAUDI_NIC_H_

#include "gaudiP.h"
#include <linux/habanalabs/gaudi.h>
#include "../include/gaudi/gaudi_fw_if.h"
#include "../include/gaudi/asic_reg/gaudi_regs.h"

#define NIC_RX_SIZE		(1 << 9)

/* verify power of 2 */
static_assert(IS_POWER_OF_2(NIC_RX_SIZE));

#define NIC_MAX_RC_MTU		(SZ_8K - DEVICE_CACHE_LINE_SIZE)
#define NIC_MAX_FRM_LEN		(NIC_MAX_RC_MTU + DEVICE_CACHE_LINE_SIZE)
#define NIC_RAW_MIN_MTU		(SZ_1K - HL_EN_MAX_HEADERS_SZ)
#define NIC_RAW_MAX_MTU		(NIC_MAX_FRM_LEN - HL_EN_MAX_HEADERS_SZ)
#define NIC_RAW_ELEM_SIZE	(NIC_RAW_MAX_MTU + HL_EN_MAX_HEADERS_SZ)

/* verify power of 2 */
static_assert(IS_POWER_OF_2(NIC_RAW_ELEM_SIZE));

#define NIC_MAX_QP_NUM		(NIC_MAX_CONN_ID + 1)

/* Number of available QPs must not exceed NIC_HW_MAX_QP_NUM */
static_assert(NIC_MAX_QP_NUM <= NIC_HW_MAX_QP_NUM);

#define NIC_MAX_WQ_ARRAY_TYPE	HL_NIC_USER_WQ_RECV

/* The '*_SIZE' defines are per NIC port */
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

#define TMR_FSM0_OFFS		0
#define TMR_FREE_OFFS		(TMR_FSM0_OFFS + TMR_FSM_SIZE)
#define TMR_FIFO_OFFS		(TMR_FREE_OFFS + TMR_FREE_SIZE)
#define TMR_FSM1_OFFS		(TMR_FSM0_OFFS + TMR_FSM_ENGINE_OFFS)

#define TMR_FREE_NUM_ENTRIES	(TMR_FIFO_SIZE / DEVICE_CACHE_LINE_SIZE)
#define TMR_GRANULARITY		128

#define TXS_BASE_SIZE		(TXS_FREE_SIZE + TXS_FIFO_SIZE + TXS_FIFO_STATIC_SIZE)


#define TXS_FREE_SIZE		ALIGN(TXS_FREE_NUM_ENTRIES * 4, DEVICE_CACHE_LINE_SIZE)
/* TXS serves requester and responder QPs, hence multiply by 2 */
#define TXS_FIFO_SIZE		ALIGN((NIC_MAX_QP_NUM * 2 * 4), DEVICE_CACHE_LINE_SIZE)
#define TXS_FIFO_STATIC_SIZE	(DEVICE_CACHE_LINE_SIZE * TXS_GRANULARITY)

#define TXS_FREE_OFFS		0
#define TXS_FIFO_OFFS		(TXS_FREE_OFFS + TXS_FREE_SIZE)

#define TXS_FREE_NUM_ENTRIES	(TXS_FIFO_SIZE / DEVICE_CACHE_LINE_SIZE)
#define TXS_GRANULARITY		256
#define TXS_SCHEDQ		256

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
#define CQ_USER_MIN_ENTRIES		128
#define QP_ERR_BUF_SIZE			(QP_ERR_SIZE * QP_ERR_BUF_LEN)
#define QP_ERR_SIZE			sizeof(struct qp_err)
#define QP_ERR_BUF_LEN			1024
#define RAW_QPN				0
#define RX_MSI_IDX			(GAUDI_EVENT_QUEUE_MSI_IDX + 1)
#define RX_MSI_ADDRESS			(mmPCIE_MSI_INTR_0 + RX_MSI_IDX * 4)
#define CQ_MSI_IDX			(NUMBER_OF_CMPLT_QUEUES + NUMBER_OF_CPU_HW_QUEUES + \
						NIC_NUMBER_OF_ENGINES)

#define USER_CQ_MIN_ENTRIES		(1 << 10)
#define USER_CQ_MAX_ENTRIES		(1 << 27)

#define WQE_MAX_SIZE			max(NIC_SEND_WQE_SIZE, NIC_RECV_WQE_SIZE)
#define USER_WQES_MIN_NUM		(1 << 4)
#define USER_WQES_MAX_NUM		(1 << 21) /* 2MB */
#define USER_WQ_ARR_MAX_SIZE		ALIGN((1ull * NIC_HW_MAX_QP_NUM * USER_WQES_MAX_NUM * \
						WQE_MAX_SIZE), PAGE_SIZE_2MB)

#define GW_MASK_REG_NUM		(((mmNIC0_QPC0_GW_MASK_15 - mmNIC0_QPC0_GW_MASK_0) >> 2) + 1)

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

struct cqe {
	__le64	data;
};

#define CQE_IS_VALID(cqe)	((le64_to_cpu((cqe)->data) >> 63) & 1)
#define CQE_TYPE(cqe)		((le64_to_cpu((cqe)->data) >> 23) & 1)
#define CQE_RES_NIC(cqe)	((le64_to_cpu((cqe)->data) >> 10) & 1)
#define CQE_RES_IMDT_21_0(cqe)	((le64_to_cpu((cqe)->data) >> 32) & 0x3FFFFF)
#define CQE_RES_IMDT_31_22(cqe)	(le64_to_cpu((cqe)->data) & 0x3FF)
#define CQE_REQ_WQE_IDX(cqe)	((le64_to_cpu((cqe)->data) >> 32) & 0x3FFFFF)
#define CQE_REQ_QPN(cqe)	(le64_to_cpu((cqe)->data) & 0x7FFFFF)
#define CQE_SET_INVALID(cqe)	((cqe)->data &= cpu_to_le64(~(1ull << 63)))

#define CQE_SIZE			sizeof(struct cqe)

struct qp_err {
	__le32	data;
};

#define QP_ERR_QP_NUM(qp_err)	(le32_to_cpu((qp_err).data) & 0xFFFFFF)
#define QP_ERR_ERR_NUM(qp_err)	((le32_to_cpu((qp_err).data) >> 24) & 0x7F)
#define QP_ERR_IS_REQ(qp_err)	((le32_to_cpu((qp_err).data) >> 31) & 1)

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

const char *gaudi_nic_phy_get_fw_name(void);
void gaudi_nic_phy_init(struct hl_device *hdev);
int gaudi_nic_phy_fw_load_all(struct hl_device *hdev);
u16 gaudi_nic_phy_get_crc(struct hl_device *hdev);
int gaudi_nic_phy_port_init(struct hl_nic_port *nic_port);
void gaudi_nic_phy_port_start_stop(struct hl_nic_port *nic_port, bool is_start);
int gaudi_nic_phy_port_power_up(struct hl_nic_port *nic_port);
void gaudi_nic_phy_port_reconfig(struct hl_nic_port *nic_port);
void gaudi_nic_phy_port_fini(struct hl_nic_port *nic_port);
int gaudi_nic_phy_reset_macro(struct hl_nic_macro *nic_macro);
void gaudi_nic_phy_link_status_work(struct work_struct *work);
u32 gaudi_nic_mac_read(struct hl_nic_port *nic_port, int mac, char *cfg_type, u32 addr);
int gaudi_nic_debugfs_qp_read(struct hl_device *hdev, char *buf, size_t bsize);
int gaudi_nic_debugfs_wqe_read(struct hl_device *hdev, char *buf, size_t bsize);
void gaudi_nic_debugfs_print_fec_stats(struct hl_nic_port *nic_port);

#endif /* GAUDI_NIC_H_ */
