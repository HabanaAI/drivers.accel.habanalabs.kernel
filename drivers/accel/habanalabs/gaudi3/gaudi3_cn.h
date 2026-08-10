/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI3_CN_H_
#define GAUDI3_CN_H_

#include "gaudi3P.h"
#include "../include/gaudi3/gaudi3_regs.h"

/* Parameters for bring-up (not to be upstreamed) - START */
#define NIC_MAX_RC_MTU		SZ_8K
/* This is the max frame length the H/W supports (Tx/Rx) */
#define NIC_MAX_RDMA_HDRS	234
#define NIC_MAX_FRM_LEN		(NIC_MAX_RC_MTU + NIC_MAX_RDMA_HDRS)

/* This is the size of an element size in the RAW buffer - note that it is different than
 * NIC_MAX_FRM_LEN, because it has to be power of 2.
 */
#define NIC_RAW_ELEM_SIZE	(2 * NIC_MAX_RC_MTU)

/* verify power of 2 */
static_assert(IS_POWER_OF_2(NIC_RAW_ELEM_SIZE));
/* Parameters for bring-up (not to be upstreamed) - END */

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

/* Parameters for bring-up (not to be upstreamed) - START */

enum gaudi3_wqe_opcode {
	WQE_NOP = 0,
	WQE_SEND = 1,
	WQE_LINEAR = 2,
	WQE_STRIDE = 3,
	WQE_MULTI_STRIDE = 4,
	WQE_RENDEZVOUS_WR = 5,
	WQE_RENDEZVOUS_RD = 6,
	WQE_ATOMIC_FETCH_ADD = 7,
	WQE_MULTI_STRIDE_DUAL = 8,
	WQE_ATOMIC_FETCH_AND_ADD_WRITE = 9,
	WQE_ATOMIC_FETCH_AND_ADD_READ = 0xa,
	WQE_FIFO_ALLOCATION = 0xb,
	WQE_FIFO_PUSH = 0xc
};

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
/* Parameters for bring-up (not to be upstreamed) - END */

/* Parameters for simulator (not to be upstreamed) - START*/
#define GAUDI3_PORTS_MASK_200G 0xFFFFFF
#define GAUDI3_PORTS_MASK_400G 0xFFF
#define GAUDI3_HLS3_EXTERN_PORTS_MASK_200G 0xC00100
#define GAUDI3_HLS3_EXTERN_PORTS_MASK_200G_16TB 0x3003C0
#define GAUDI3_HLS3_EXTERN_PORTS_MASK_200G_48TB GAUDI3_PORTS_MASK_200G
#define GAUDI3_HLS3_EXTERN_PORTS_MASK_400G_48TB GAUDI3_PORTS_MASK_400G
/* Parameters for simulator (not to be upstreamed) - END*/

u64 gaudi3_cn_get_macro_ports_mask(struct hl_device *hdev, int macro_idx);
u32 gaudi3_cn_handle_bmon_spmu_event(struct hl_device *hdev);
int gaudi3_cn_set_info(struct hl_device *hdev, bool get_from_fw);
bool is_400g_mode(struct hl_device *hdev);

#endif /* GAUDI3_CN_H_ */
