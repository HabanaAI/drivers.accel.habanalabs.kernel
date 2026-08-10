// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3P.h"
#include "../include/gaudi3/asic_reg/gaudi3_regs.h"

#define RR_LBW_SEC_SHORT_NUM_RANGES		14
#define RR_LBW_SEC_NUM_RANGES			4

#define RR_HBW_SEC_NUM_RANGES			8

struct gaudi3_atypical_pb_blocks {
	u32 mm_block_base_addr;
	u32 block_size;
	u32 glbl_sec_offset;
	u32 glbl_sec_length;
};

static const struct gaudi3_atypical_pb_blocks gaudi3_pb_hdcr0_sm_objs[2] = {
	{mmHD0_SYNC_MNGR_OBJS_BASE, SM_OBJS_BLOCK_SIZE, SM_OBJS_SEC_PROT_BITS_0_OFFS, SZ_512},
	{mmHD0_SYNC_MNGR_OBJS_BASE, SM_OBJS_BLOCK_SIZE, SM_OBJS_SEC_PROT_BITS_1_OFFS, SZ_512}
};

/*****************************
 * RR LBW configuration tables
 *****************************/

/* configuration table for LBW "secured short" range type */
static struct rr_range rr_lbw_sec_short_ranges[] = {
	{
		.min = mmD0_NIC0_MAC_PCS_PCS0_BASE & RR_LBW_SHORT_ADDR_MASK_SHORT,
		.max = (mmD0_NIC0_MAC_CH_MAC_CH0_BASE + HL_BLOCK_SIZE) &
				RR_LBW_SHORT_ADDR_MASK_SHORT,
		.rd = true,
		.wr = true
	},
	{
		.min = mmD0_NIC1_MAC_PCS_PCS0_BASE & RR_LBW_SHORT_ADDR_MASK_SHORT,
		.max = (mmD0_NIC1_MAC_CH_MAC_CH0_BASE + HL_BLOCK_SIZE) &
				RR_LBW_SHORT_ADDR_MASK_SHORT,
		.rd = true,
		.wr = true
	},
	{
		.min = mmD0_NIC2_MAC_PCS_PCS0_BASE & RR_LBW_SHORT_ADDR_MASK_SHORT,
		.max = (mmD0_NIC2_MAC_CH_MAC_CH0_BASE + HL_BLOCK_SIZE) &
				RR_LBW_SHORT_ADDR_MASK_SHORT,
		.rd = true,
		.wr = true
	},
	{
		.min = mmD0_NIC3_MAC_PCS_PCS0_BASE & RR_LBW_SHORT_ADDR_MASK_SHORT,
		.max = (mmD0_NIC3_MAC_CH_MAC_CH0_BASE + HL_BLOCK_SIZE) &
				RR_LBW_SHORT_ADDR_MASK_SHORT,
		.rd = true,
		.wr = true
	},
	{
		.min = mmD0_NIC4_MAC_PCS_PCS0_BASE & RR_LBW_SHORT_ADDR_MASK_SHORT,
		.max = (mmD0_NIC4_MAC_CH_MAC_CH0_BASE + HL_BLOCK_SIZE) &
				RR_LBW_SHORT_ADDR_MASK_SHORT,
		.rd = true,
		.wr = true
	},
	{
		.min = mmD0_NIC5_MAC_PCS_PCS0_BASE & RR_LBW_SHORT_ADDR_MASK_SHORT,
		.max = (mmD0_NIC5_MAC_CH_MAC_CH0_BASE + HL_BLOCK_SIZE) &
				RR_LBW_SHORT_ADDR_MASK_SHORT,
		.rd = true,
		.wr = true
	},
	{
		.min = mmD1_NIC0_MAC_PCS_PCS0_BASE & RR_LBW_SHORT_ADDR_MASK_SHORT,
		.max = (mmD1_NIC0_MAC_CH_MAC_CH0_BASE + HL_BLOCK_SIZE) &
				RR_LBW_SHORT_ADDR_MASK_SHORT,
		.rd = true,
		.wr = true
	},
	{
		.min = mmD1_NIC1_MAC_PCS_PCS0_BASE & RR_LBW_SHORT_ADDR_MASK_SHORT,
		.max = (mmD1_NIC1_MAC_CH_MAC_CH0_BASE + HL_BLOCK_SIZE) &
				RR_LBW_SHORT_ADDR_MASK_SHORT,
		.rd = true,
		.wr = true
	},
	{
		.min = mmD1_NIC2_MAC_PCS_PCS0_BASE & RR_LBW_SHORT_ADDR_MASK_SHORT,
		.max = (mmD1_NIC2_MAC_CH_MAC_CH0_BASE + HL_BLOCK_SIZE) &
				RR_LBW_SHORT_ADDR_MASK_SHORT,
		.rd = true,
		.wr = true
	},
	{
		.min = mmD1_NIC3_MAC_PCS_PCS0_BASE & RR_LBW_SHORT_ADDR_MASK_SHORT,
		.max = (mmD1_NIC3_MAC_CH_MAC_CH0_BASE + HL_BLOCK_SIZE) &
				RR_LBW_SHORT_ADDR_MASK_SHORT,
		.rd = true,
		.wr = true
	},
	{
		.min = mmD1_NIC4_MAC_PCS_PCS0_BASE & RR_LBW_SHORT_ADDR_MASK_SHORT,
		.max = (mmD1_NIC4_MAC_CH_MAC_CH0_BASE + HL_BLOCK_SIZE) &
				RR_LBW_SHORT_ADDR_MASK_SHORT,
		.rd = true,
		.wr = true
	},
	{
		.min = mmD1_NIC5_MAC_PCS_PCS0_BASE & RR_LBW_SHORT_ADDR_MASK_SHORT,
		.max = (mmD1_NIC5_MAC_CH_MAC_CH0_BASE + HL_BLOCK_SIZE) &
				RR_LBW_SHORT_ADDR_MASK_SHORT,
		.rd = true,
		.wr = true
	},
};

/* configuration table for LBW "secured" range type */
static struct rr_range rr_lbw_sec_ranges[] = {
	{
		.min = (mmD0_PCIE_DBI_SIG_BASE + mmPCIE_DBI_MSIX_DOORBELL_OFF) &
				RR_LBW_LONG_ADDR_MASK,
		.max = (mmD0_PCIE_DBI_SIG_BASE + mmPCIE_DBI_MSIX_DOORBELL_OFF +
				RR_LBW_LONG_ADDR_BYTE_GRANULARITY) & RR_LBW_LONG_ADDR_MASK,
		.rd = true,
		.wr = true
	},
};

/* verify no overflow in the LBW ranges */
static_assert(ARRAY_SIZE(rr_lbw_sec_short_ranges) <= RR_LBW_SEC_SHORT_NUM_RANGES);
static_assert(ARRAY_SIZE(rr_lbw_sec_ranges) <= RR_LBW_SEC_NUM_RANGES);

/* configuration table for LBW ranges */
static struct rr_type_config rr_lbw_config_array[RR_LBW_RANGE_TYPE_NUMBER] = {
	[RR_LBW_RANGE_TYPE_SEC_SHORT] = {
		.ranges = rr_lbw_sec_short_ranges,
		.num_ranges = ARRAY_SIZE(rr_lbw_sec_short_ranges),
		.lbw_prop = {
			.en_off = RTR_CTRL_RR_LBW_SEC_RANGE_EN_SHORT_OFFSET,
			.min_off = RTR_CTRL_RR_LBW_SEC_RANGE_MIN_SHORT_OFFSET,
			.max_off = RTR_CTRL_RR_LBW_SEC_RANGE_MAX_SHORT_OFFSET,
			.addr_mask = RR_LBW_SHORT_ADDR_MASK_SHORT,
		},
	},
	[RR_LBW_RANGE_TYPE_SEC] = {
		.ranges = rr_lbw_sec_ranges,
		.num_ranges = ARRAY_SIZE(rr_lbw_sec_ranges),
		.lbw_prop = {
			.en_off = RTR_CTRL_RR_LBW_SEC_RANGE_EN_OFFSET,
			.min_off = RTR_CTRL_RR_LBW_SEC_RANGE_MIN_OFFSET,
			.max_off = RTR_CTRL_RR_LBW_SEC_RANGE_MAX_OFFSET,
			.addr_mask = RR_LBW_LONG_ADDR_MASK,
		},
	},
	[RR_LBW_RANGE_TYPE_PRIV_SHORT] = {
		.num_ranges = 0,
	},
	[RR_LBW_RANGE_TYPE_PRIV_SHORT_13] = {
		.num_ranges = 0,
	},
	[RR_LBW_RANGE_TYPE_PRIV] = {
		.num_ranges = 0,
	},
};

/*****************************
 * RR HBW configuration tables
 *****************************/

/* configuration table for HBW "secured" range type */
static struct rr_range rr_hbw_sec_ranges[] = {
};

/* verify no overflow in HBW ranges */
static_assert(ARRAY_SIZE(rr_hbw_sec_ranges) <= RR_HBW_SEC_NUM_RANGES);

/* configuration table for HBW ranges */
static struct rr_type_config rr_hbw_config_array[RR_HBW_RANGE_TYPE_NUMBER] = {
	[RR_HBW_RANGE_TYPE_SEC] = {
		.ranges = rr_hbw_sec_ranges,
		.num_ranges = ARRAY_SIZE(rr_hbw_sec_ranges),
		.hbw_prop = {
			.en_off = RTR_CTRL_RR_HBW_SEC_RANGE_EN_OFFSET,
			.min_hi_off = RTR_CTRL_RR_HBW_SEC_RANGE_MIN_HI_OFFSET,
			.min_lo_off = RTR_CTRL_RR_HBW_SEC_RANGE_MIN_LO_OFFSET,
			.max_hi_off = RTR_CTRL_RR_HBW_SEC_RANGE_MAX_HI_OFFSET,
			.max_lo_off = RTR_CTRL_RR_HBW_SEC_RANGE_MAX_LO_OFFSET,
		},
	},
	[RR_HBW_RANGE_TYPE_PRIV] = {
		.num_ranges = 0,
	},
	[RR_HBW_RANGE_TYPE_PRIV_7] = {
		.num_ranges = 0,
	},
};

void gaudi3_rtr_ctrl_config_rr(struct hl_device *hdev, int block, int inst, u32 offset,
				struct iterate_module_ctx *ctx)
{
	struct rtr_ctrl_rr_config *rr_config = ctx->data;
	u8 instance_off, rd_access, wr_access;
	struct rr_type_config *rr_cfg;
	struct rr_range *range;
	int rr_type, range_idx;
	u32 val;

	/* configure all LBW range types */
	for (rr_type = 0, rr_cfg = rr_config->lbw_config_array;
			rr_type < RR_LBW_RANGE_TYPE_NUMBER; rr_type++, rr_cfg++) {
		for (range_idx = 0, range = rr_cfg->ranges;
						range_idx < rr_cfg->num_ranges;
						range_idx++, range++) {
			instance_off = range_idx * sizeof(u32);
			rd_access = !!range->rd;
			wr_access = !!range->wr;
			/*
			 * enable the range.
			 * the code use the mask of LBW_SEC_RANGE_EN_[WR/RD] yet the same mask
			 * applied to all other LBW regions as well.
			 */
			val = FIELD_PREP(RTR_CTRL_RR_LBW_SEC_RANGE_EN_WR_M, wr_access) |
				FIELD_PREP(RTR_CTRL_RR_LBW_SEC_RANGE_EN_RD_M, rd_access);
			WREG32(offset + rr_cfg->lbw_prop.en_off + instance_off, val);

			val = REG_OFF_TO_LBW_OFF(range->min) & rr_cfg->lbw_prop.addr_mask;
			WREG32(offset + rr_cfg->lbw_prop.min_off + instance_off, val);

			val = REG_OFF_TO_LBW_OFF(range->max) & rr_cfg->lbw_prop.addr_mask;
			WREG32(offset + rr_cfg->lbw_prop.max_off + instance_off, val);
		}
	}

	/* configure all HBW range types */
	for (rr_type = 0, rr_cfg = rr_config->hbw_config_array;
				rr_type < RR_HBW_RANGE_TYPE_NUMBER; rr_type++, rr_cfg++) {
		for (range_idx = 0, range = rr_cfg->ranges;
						range_idx < rr_cfg->num_ranges;
						range_idx++, range++) {
			instance_off = range_idx * sizeof(u32);
			rd_access = !!range->rd;
			wr_access = !!range->wr;
			/*
			 * enable the range.
			 * the code use the mask of HBW_SEC_RANGE_EN_[WR/RD] yet the same mask
			 * applied to all other LBW regions as well.
			 */
			val = FIELD_PREP(RTR_CTRL_RR_HBW_SEC_RANGE_EN_WR_M, wr_access) |
				FIELD_PREP(RTR_CTRL_RR_HBW_SEC_RANGE_EN_RD_M, rd_access);
			WREG32(offset + rr_cfg->hbw_prop.en_off + instance_off, val);

			WREG32(offset + rr_cfg->hbw_prop.min_hi_off + instance_off,
					upper_32_bits(range->min));
			WREG32(offset + rr_cfg->hbw_prop.min_lo_off + instance_off,
					lower_32_bits(range->min) & RR_HBW_LO_ADDR_MASK_SHORT);
			WREG32(offset + rr_cfg->hbw_prop.max_hi_off + instance_off,
					upper_32_bits(range->max));
			WREG32(offset + rr_cfg->hbw_prop.max_lo_off + instance_off,
					lower_32_bits(range->max) & RR_HBW_LO_ADDR_MASK_SHORT);
		}
	}
}

static void gaudi3_init_lbw_hbw_range_registers(struct hl_device *hdev)
{
	struct rtr_ctrl_rr_config rr_config = {
		.lbw_config_array = rr_lbw_config_array,
		.hbw_config_array = rr_hbw_config_array
	};
	struct iterate_module_ctx ctx = {
		.fn = gaudi3_rtr_ctrl_config_rr,
		.data = &rr_config,
	};

	gaudi3_iterate_rtr_ctrls(hdev, &ctx);
}

/**
 * gaudi3_init_range_registers -
 * Initialize range registers of all initiators
 *
 * @hdev: pointer to hl_device structure
 */
static inline void gaudi3_init_range_registers(struct hl_device *hdev)
{
	dev_dbg(hdev->dev, "Configure RRs\n");
	gaudi3_init_lbw_hbw_range_registers(hdev);
}

static const u32 gaudi3_pb_kdma[] = {
	mmD1_SPDMA1_CH5_A_PQM_CH_BASE,
	mmD1_SPDMA1_CH5_B_PQM_CH_BASE,
};

static const u32 gaudi3_pb_hdcr0_sm_glbl[] = {
	mmHD0_SYNC_MNGR_GLBL_BASE,
};

static const u32 gaudi3_pb_hdcr1_sm_glbl[] = {
	mmHD1_SYNC_MNGR_GLBL_BASE,
};

static const u32 gaudi3_pb_hdcr0_sm_mstr_if[] = {
	mmHD0_SYNC_MNGR_MSTR_IF_RR_HBW_BASE,
};

static const struct range gaudi3_pb_hdcr0_sm_glbl_unsecured_regs[] = {
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_BASE_ADDR_L_0 +
			GAUDI3_RESERVED_CQ_NUMBER * sizeof(u32),
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_BASE_ADDR_L_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_BASE_ADDR_H_0 +
			GAUDI3_RESERVED_CQ_NUMBER * sizeof(u32),
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_BASE_ADDR_H_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_SIZE_LOG2_0 +
			GAUDI3_RESERVED_CQ_NUMBER * sizeof(u32),
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_SIZE_LOG2_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_PI_0 +
			GAUDI3_RESERVED_CQ_NUMBER * sizeof(u32),
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_PI_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_ADDR_L_0 +
			GAUDI3_RESERVED_CQ_NUMBER * sizeof(u32),
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_ADDR_L_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_ADDR_H_0 +
			GAUDI3_RESERVED_CQ_NUMBER * sizeof(u32),
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_ADDR_H_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_DATA_0 +
			GAUDI3_RESERVED_CQ_NUMBER * sizeof(u32),
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_DATA_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_INC_MODE_0 +
			GAUDI3_RESERVED_CQ_NUMBER * sizeof(u32),
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_INC_MODE_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_DIR_LBW_EN_0 +
			GAUDI3_RESERVED_CQ_NUMBER * sizeof(u32),
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_DIR_LBW_EN_63},
};

static const struct range gaudi3_pb_hdcr_x_sm_glbl_unsecured_regs[] = {
	{mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_BASE_ADDR_L_0,
			mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_BASE_ADDR_L_63},
	{mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_BASE_ADDR_H_0,
			mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_BASE_ADDR_H_63},
	{mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_SIZE_LOG2_0,
			mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_SIZE_LOG2_63},
	{mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_PI_0,
			mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_PI_63},
	{mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_ADDR_L_0,
			mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_ADDR_L_63},
	{mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_ADDR_H_0,
			mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_ADDR_H_63},
	{mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_DATA_0,
			mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_DATA_63},
	{mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_INC_MODE_0,
			mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_INC_MODE_63},
	{mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_DIR_LBW_EN_0,
			mmHD1_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_DIR_LBW_EN_63},
};

static int gaudi3_init_pb_sm_objs(struct hl_device *hdev)
{
	int i, j, glbl_sec_array_len = gaudi3_pb_hdcr0_sm_objs[0].glbl_sec_length;
	u32 sec_entry, *sec_array, array_base, first_sob, first_mon, first_cq;
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	array_base = gaudi3_pb_hdcr0_sm_objs[0].mm_block_base_addr +
				gaudi3_pb_hdcr0_sm_objs[0].glbl_sec_offset;

	sec_array = kcalloc(glbl_sec_array_len, sizeof(u32), GFP_KERNEL);
	if (!sec_array)
		return -ENOMEM;

	first_sob = GAUDI3_RESERVED_SOB_NUMBER;
	first_mon = GAUDI3_RESERVED_MON_NUMBER;
	first_cq = GAUDI3_RESERVED_CQ_NUMBER;

	/* Handle 1st group of SM objects */

	/* First, un-secure 8192 SOB_OBJ (skip reserved SOBs) */
	for (j = i = first_sob ; i < HDCORE_NUM_OF_SOB_PER_GRP ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* Next, un-secure 1024 MON_PAY_ADDR_L (skip reserved MONs) */
	for (i = first_mon, j += i ; i < HDCORE_NUM_OF_MON_PER_GRP ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* Next, un-secure 1024 MON_PAY_ADDR_H (skip reserved MONs) */
	for (i = first_mon, j += i ; i < HDCORE_NUM_OF_MON_PER_GRP ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* Next, un-secure 1024 MON_PAY_DATA (skip reserved MONs) */
	for (i = first_mon, j += i ; i < HDCORE_NUM_OF_MON_PER_GRP ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* Next, un-secure 1024 MON_ARM (skip reserved MONs) */
	for (i = first_mon, j += i ; i < HDCORE_NUM_OF_MON_PER_GRP ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* Next, un-secure 1024 MON_CONFIG (skip reserved MONs) */
	for (i = first_mon, j += i ; i < HDCORE_NUM_OF_MON_PER_GRP ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* Next, un-secure 1024 MON_STATUS (skip reserved MONs) */
	for (i = first_mon, j += i ; i < HDCORE_NUM_OF_MON_PER_GRP ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* Last, un-secure 64 CQ_DIRECT (skip reserved CQs)
	 * Note that CQ_DIRECT exist ONLY in the 1st group of SYNC_MNGR_OBJS.
	 */
	for (i = first_cq, j += i ; i < HDCORE_NUM_OF_CQ ; i++, j++)
		UNSET_GLBL_SEC_BIT(sec_array, j);

	/* Un-secure all SYNC_MNGR_OBJS registers in HDCORE0 (except for the skipped ones above) */
	for (i = 0 ; i < glbl_sec_array_len ; i++) {
		sec_entry = array_base + i * sizeof(u32);
		WREG32(sec_entry, sec_array[i]);
	}

	/* Un-secure SYNC_MNGR_OBJS registers in the remaining HDCOREs */
	memset(sec_array, -1, glbl_sec_array_len * sizeof(u32));

	for (i = 1 ; i < prop->num_of_hdcores ; i++) {
		for (j = 0 ; j < glbl_sec_array_len ; j++) {
			sec_entry = HDCORE_OFFSET * i + array_base + j * sizeof(u32);
			WREG32(sec_entry, sec_array[j]);
		}
	}

	/* Handle 2nd group of SM objects */

	/* Un-secure all registers in 2nd group of SYNC_MNGR_OBJS */
	glbl_sec_array_len = gaudi3_pb_hdcr0_sm_objs[1].glbl_sec_length;
	array_base = gaudi3_pb_hdcr0_sm_objs[1].mm_block_base_addr +
				gaudi3_pb_hdcr0_sm_objs[1].glbl_sec_offset;

	for (i = 0 ; i < prop->num_of_hdcores ; i++) {
		for (j = 0 ; j < glbl_sec_array_len ; j++) {
			sec_entry = HDCORE_OFFSET * i + array_base + j * sizeof(u32);
			WREG32(sec_entry, sec_array[j]);
		}
	}

	kfree(sec_array);

	return 0;
}

/**
 * gaudi3_init_protection_bits -
 * Initialize protection bits of specific registers
 *
 * @hdev: pointer to hl_device structure
 *
 * All protection bits values are 1 by default, i.e., not secured.
 * Need to set to 0 each bit that corresponds to a register marked as secured.
 *
 */
static int gaudi3_init_protection_bits(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc = 0;

	dev_dbg(hdev->dev, "Configure protection bits\n");

	/* KDMA (D1_SPDMA1_CH5_A) */
	rc |= hl_init_pb(hdev, HL_PB_SHARED, HL_PB_NA,
			HL_PB_SINGLE_INSTANCE, HL_PB_NA,
			gaudi3_pb_kdma, ARRAY_SIZE(gaudi3_pb_kdma),
			NULL, HL_PB_NA);

	/* SYNC_MNGR_OBJS */
	rc |= gaudi3_init_pb_sm_objs(hdev);

	/* SYNC_MNGR_GLBL */

	/* Secure the first GAUDI3_RESERVED_CQ_NUMBER CQ registers in HDCORE0 */
	rc |= hl_init_pb_ranges(hdev, HL_PB_SHARED, HL_PB_NA,
			HL_PB_SINGLE_INSTANCE, HL_PB_NA,
			gaudi3_pb_hdcr0_sm_glbl,
			ARRAY_SIZE(gaudi3_pb_hdcr0_sm_glbl),
			gaudi3_pb_hdcr0_sm_glbl_unsecured_regs,
			ARRAY_SIZE(gaudi3_pb_hdcr0_sm_glbl_unsecured_regs));

	/* Secure all registers except for CQ related registers in HDCORE1..X */
	rc |= hl_init_pb_ranges(hdev, prop->num_of_hdcores - 1, HDCORE_OFFSET,
			HL_PB_SINGLE_INSTANCE, HL_PB_NA,
			gaudi3_pb_hdcr1_sm_glbl,
			ARRAY_SIZE(gaudi3_pb_hdcr1_sm_glbl),
			gaudi3_pb_hdcr_x_sm_glbl_unsecured_regs,
			ARRAY_SIZE(gaudi3_pb_hdcr_x_sm_glbl_unsecured_regs));

	/* SYNC_MNGR_MSTR_IF */
	rc |= hl_init_pb(hdev, prop->num_of_hdcores, HDCORE_OFFSET,
			HL_PB_SINGLE_INSTANCE, HL_PB_NA,
			gaudi3_pb_hdcr0_sm_mstr_if,
			ARRAY_SIZE(gaudi3_pb_hdcr0_sm_mstr_if),
			NULL, HL_PB_NA);

	return rc;
}

/**
 * gaudi3_init_security - Initialize security model
 *
 * @hdev: pointer to hl_device structure
 *
 * Initialize the security model of the device
 * That includes range registers and protection bit per register.
 */
int gaudi3_init_security(struct hl_device *hdev)
{
	int rc;

	/* SW-181592: By default, a secured FW enables ISEC security which
	 * prevents the driver from accessing and configuring PB registers,
	 * so there's no point in trying and configure them.
	 */
	if (!hdev->security_enable || hdev->asic_prop.fw_security_enabled)
		return 0;

	rc = gaudi3_init_protection_bits(hdev);
	if (rc)
		return rc;

	gaudi3_init_range_registers(hdev);

	rc = hl_init_pb_security(hdev, false);
	if (rc) {
		dev_err(hdev->dev, "Configuring Secured PBs failed!\n");
		return rc;
	}

	return 0;
}
