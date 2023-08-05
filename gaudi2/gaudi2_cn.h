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
