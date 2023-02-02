// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include <linux/types.h>

#include "gaudi3P.h"
#include "../common/habanalabs.h"
#include "../include/gaudi3/asic_reg/gaudi3_regs.h"
#include "gaudi3_masks.h"

void gaudi3_axuser_hbw_mmu_bp_set(struct hl_device *hdev, u32 axuser_hbw_reg_base, bool bypass)
{
	u32 rw_mmu_bp = bypass ? HBW_RW_MMU_BYPASS_OVRD : 0;

	WREG32(axuser_hbw_reg_base + mmPDMA_CH_B_AXUSER_HBW_HB_MMU_BYPASS_OVRD,
			HBW_RW_MMU_BYPASS_OVRD);
	WREG32(axuser_hbw_reg_base + mmPDMA_CH_B_AXUSER_HBW_HB_MMU_BYPASS, rw_mmu_bp);
}

void gaudi3_axuser_hbw_mmu_bp_clear(struct hl_device *hdev, u32 axuser_hbw_reg_base)
{
	WREG32(axuser_hbw_reg_base + mmPDMA_CH_B_AXUSER_HBW_HB_MMU_BYPASS_OVRD, 0);
	WREG32(axuser_hbw_reg_base + mmPDMA_CH_B_AXUSER_HBW_HB_MMU_BYPASS, 0);
}

void gaudi3_axuser_hbw_asid_set(struct hl_device *hdev, u32 axuser_hbw_reg_base, u32 asid)
{
	u32 rw_asid = FIELD_PREP(PDMA_CH_B_AXUSER_HBW_HB_ASID_RD_M, asid) |
			FIELD_PREP(PDMA_CH_B_AXUSER_HBW_HB_ASID_WR_M, asid);

	WREG32(axuser_hbw_reg_base + mmPDMA_CH_B_AXUSER_HBW_HB_ASID_OVRD, HBW_RW_ASID_OVRD);
	WREG32(axuser_hbw_reg_base + mmPDMA_CH_B_AXUSER_HBW_HB_ASID, rw_asid);
}

void gaudi3_axuser_hbw_asid_clear(struct hl_device *hdev, u32 axuser_hbw_reg_base)
{
	WREG32(axuser_hbw_reg_base + mmPDMA_CH_B_AXUSER_HBW_HB_ASID_OVRD, 0);
	WREG32(axuser_hbw_reg_base + mmPDMA_CH_B_AXUSER_HBW_HB_ASID, 0);
}
