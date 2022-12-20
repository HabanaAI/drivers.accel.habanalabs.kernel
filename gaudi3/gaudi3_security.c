// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3P.h"
#include "gaudi3_nic.h"
#include "../include/gaudi3/asic_reg/gaudi3_regs.h"

/*
 * Naming conventions for the defines/enum etc. try to follow the SocOnline
 * conventions.
 * Therefore, we have here (for example) both PRIV_SHORT and PRIV_SHORT_13
 * as both are regs (or regs groups) form SocOnline.
 */
#define RR_LBW_SEC_SHORT_NUM_RANGES	14
#define RR_LBW_SEC_NUM_RANGES		4
#define RR_LBW_PRIV_SHORT_NUM_RANGES	13
#define RR_LBW_PRIV_SHORT_13_NUM_RANGES	1
#define RR_LBW_PRIV_NUM_RANGES		4

#define RR_HBW_SEC_NUM_RANGES		8
#define RR_HBW_PRIV_NUM_RANGES		7
#define RR_HBW_PRIV_7_NUM_RANGES	1

#define RR_LBW_LONG_ADDR_BYTE_GRANULARITY	8

#define RR_LBW_SHORT_ADDR_MASK_SHORT	GENMASK_ULL(28, 12)
#define RR_LBW_LONG_ADDR_MASK		GENMASK_ULL(28, 2)
#define RR_HBW_LO_ADDR_MASK_SHORT	GENMASK_ULL(31, 14)

/* enum for RR LBW ranges types */
enum rr_lbw_range_type {
	RR_LBW_RANGE_TYPE_SEC_SHORT,
	RR_LBW_RANGE_TYPE_SEC,
	RR_LBW_RANGE_TYPE_PRIV_SHORT,
	RR_LBW_RANGE_TYPE_PRIV_SHORT_13,
	RR_LBW_RANGE_TYPE_PRIV,
	RR_LBW_RANGE_TYPE_NUMBER,
};

/* enum for RR HBW ranges types */
enum rr_hbw_range_type {
	RR_HBW_RANGE_TYPE_SEC,
	RR_HBW_RANGE_TYPE_PRIV,
	RR_HBW_RANGE_TYPE_PRIV_7,
	RR_HBW_RANGE_TYPE_NUMBER,
};

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

/**
 * struct rr_range - single RR range configuration
 * @min: min address
 * @max: max address
 * @rd: if true the range config applies to read access
 * @wr: if true the range config applies to write access
 */
struct rr_range {
	u64 min;
	u64 max;
	bool rd;
	bool wr;
};

/**
 * struct rr_lbw_type_prop - properties of RR LBW range type
 * @en_off: offset of first copy of EN reg
 * @min_off: offset of first copy of MIN reg
 * @max_off: offset of first copy of MAX reg
 * @addr_mask: mask for the addresses configured to MIN and MAX
 */
struct rr_lbw_type_prop {
	u32 en_off;
	u32 min_off;
	u32 max_off;
	u32 addr_mask;
};

/**
 * struct rr_hbw_type_prop - properties of RR range type
 * @en_off: offset of first copy of EN reg
 * @min_hi_off: offset of first copy of MIN HI reg
 * @min_lo_off: offset of first copy of MIN LO reg
 * @max_hi_off: offset of first copy of MAX HI reg
 * @max_lo_off: offset of first copy of MAX LO reg
 */
struct rr_hbw_type_prop {
	u32 en_off;
	u32 min_hi_off;
	u32 min_lo_off;
	u32 max_hi_off;
	u32 max_lo_off;
};

/**
 * struct rr_type_config - configuration data for specific RR LBW or HBW type
 * @lbw_prop: properties of LBW range type
 * @hbw_prop: properties of HBW range type
 * @ranges: array of configuration element
 * @num_ranges: number of configuration elements in ranges
 */
struct rr_type_config {
	union {
		struct rr_lbw_type_prop lbw_prop;
		struct rr_hbw_type_prop hbw_prop;
	};
	struct rr_range *ranges;
	u8 num_ranges;
};

/**
 * struct dtlb_rr_cfg_data - configuration data for DTLB RR
 * @rr_glbl_pa_end0: bits 20-39 of last DRAM phys addr
 */
struct dtlb_rr_cfg_data {
	u32 rr_glbl_pa_end0;
};

/*****************************
 * RR LBW configuration tables
 *****************************/

/* configuration table for LBW "secured short" range type */
static struct rr_range rr_lbw_sec_short_ranges[] = {
	/* TODO SW-93357: Config NIC RR */
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

/* configuration table for LBW "privileged short" range type */
static struct rr_range rr_lbw_priv_short_ranges[] = {
	{
		.min = mmD0_CPU_TIMESTAMP_BASE,
		.max = mmD0_CPU_TIMESTAMP_BASE + HL_BLOCK_SIZE,
		.rd = true,
		.wr = true
	},
};

/* configuration table for LBW "privileged short #13" range type */
static struct rr_range rr_lbw_priv_short_13_ranges[] = {
};

/* configuration table for LBW "privileged" range type */
static struct rr_range rr_lbw_priv_ranges[] = {
	{
		.min = mmD0_GIC_BASE,
		.max = (mmD0_GIC_BASE + mmGIC_DISTRIBUTOR_5_GICD_SETSPI_NSR +
				RR_LBW_LONG_ADDR_BYTE_GRANULARITY) & RR_LBW_LONG_ADDR_MASK,
		.rd = true,
		.wr = true
	},
};

/* verify no overflow in the LBW ranges */
static_assert(ARRAY_SIZE(rr_lbw_sec_short_ranges) <= RR_LBW_SEC_SHORT_NUM_RANGES);
static_assert(ARRAY_SIZE(rr_lbw_sec_ranges) <= RR_LBW_SEC_NUM_RANGES);
static_assert(ARRAY_SIZE(rr_lbw_priv_short_ranges) <= RR_LBW_PRIV_SHORT_NUM_RANGES);
static_assert(ARRAY_SIZE(rr_lbw_priv_short_13_ranges) <= RR_LBW_PRIV_SHORT_13_NUM_RANGES);
static_assert(ARRAY_SIZE(rr_lbw_priv_ranges) <= RR_LBW_PRIV_NUM_RANGES);

/* complete configuration table for all LBW ranges */
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
		.ranges = rr_lbw_priv_short_ranges,
		.num_ranges = ARRAY_SIZE(rr_lbw_priv_short_ranges),
		.lbw_prop = {
			.en_off = RTR_CTRL_RR_LBW_PRIV_RANGE_EN_SHORT_OFFSET,
			.min_off = RTR_CTRL_RR_LBW_PRIV_RANGE_MIN_SHORT_OFFSET,
			.max_off = RTR_CTRL_RR_LBW_PRIV_RANGE_MAX_SHORT_OFFSET,
			.addr_mask = RR_LBW_SHORT_ADDR_MASK_SHORT,
		},
	},
	[RR_LBW_RANGE_TYPE_PRIV_SHORT_13] = {
		.ranges = rr_lbw_priv_short_13_ranges,
		.num_ranges = ARRAY_SIZE(rr_lbw_priv_short_13_ranges),
		.lbw_prop = {
			.en_off = RTR_CTRL_RR_LBW_PRIV_RANGE_EN_SHORT_13_OFFSET,
			.min_off = RTR_CTRL_RR_LBW_PRIV_RANGE_MIN_SHORT_13_OFFSET,
			.max_off = RTR_CTRL_RR_LBW_PRIV_RANGE_MAX_SHORT_13_OFFSET,
			.addr_mask = RR_LBW_SHORT_ADDR_MASK_SHORT,
		},
	},
	[RR_LBW_RANGE_TYPE_PRIV] = {
		.ranges = rr_lbw_priv_ranges,
		.num_ranges = ARRAY_SIZE(rr_lbw_priv_ranges),
		.lbw_prop = {
			.en_off = RTR_CTRL_RR_LBW_PRIV_RANGE_EN_OFFSET,
			.min_off = RTR_CTRL_RR_LBW_PRIV_RANGE_MIN_OFFSET,
			.max_off = RTR_CTRL_RR_LBW_PRIV_RANGE_MAX_OFFSET,
			.addr_mask = RR_LBW_LONG_ADDR_MASK,
		},
	},
};

/*****************************
 * RR HBW configuration tables
 *****************************/

/* configuration table for HBW "secured" range type */
static struct rr_range rr_hbw_sec_ranges[] = {
};

/* configuration table for HBW "privileged" range type */
static struct rr_range rr_hbw_priv_ranges[] = {
};

/* configuration table for HBW "privileged #7" range type */
static struct rr_range rr_hbw_priv_7_ranges[] = {
};

/* verify no overflow in HBW ranges */
static_assert(ARRAY_SIZE(rr_hbw_sec_ranges) <= RR_HBW_SEC_NUM_RANGES);
static_assert(ARRAY_SIZE(rr_hbw_priv_ranges) <= RR_HBW_PRIV_NUM_RANGES);
static_assert(ARRAY_SIZE(rr_hbw_priv_7_ranges) <= RR_HBW_PRIV_7_NUM_RANGES);

/* complete configuration table for all HBW ranges */
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
		.ranges = rr_hbw_priv_ranges,
		.num_ranges = ARRAY_SIZE(rr_hbw_priv_ranges),
		.hbw_prop = {
			.en_off = RTR_CTRL_RR_HBW_PRIV_RANGE_EN_OFFSET,
			.min_hi_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MIN_HI_OFFSET,
			.min_lo_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MIN_LO_OFFSET,
			.max_hi_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MAX_HI_OFFSET,
			.max_lo_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MAX_LO_OFFSET,
		},
	},
	[RR_HBW_RANGE_TYPE_PRIV_7] = {
		.ranges = rr_hbw_priv_7_ranges,
		.num_ranges = ARRAY_SIZE(rr_hbw_priv_7_ranges),
		.hbw_prop = {
			.en_off = RTR_CTRL_RR_HBW_PRIV_RANGE_EN_7_OFFSET,
			.min_hi_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MIN_HI_7_OFFSET,
			.min_lo_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MIN_LO_7_OFFSET,
			.max_hi_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MAX_HI_7_OFFSET,
			.max_lo_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MAX_LO_7_OFFSET,
		},
	},
};

static void gaudi3_rtr_ctrl_config_rr(struct hl_device *hdev, int block, int inst, u32 offset,
					struct iterate_module_ctx *ctx)
{
	struct rr_type_config *rr_cfg;
	struct rr_range *range;
	int rr_type, range_idx;
	u8 instance_off, rd_access, wr_access;
	u32 val;

	/* configure all LBW range types */
	for (rr_type = 0, rr_cfg = rr_lbw_config_array;
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
			val = FIELD_PREP(HD0_RRTR0_RTR_CTRL_RR_LBW_SEC_RANGE_EN_WR_M, wr_access) |
				FIELD_PREP(HD0_RRTR0_RTR_CTRL_RR_LBW_SEC_RANGE_EN_RD_M, rd_access);
			WREG32(offset + rr_cfg->lbw_prop.en_off + instance_off, val);

			val = REG_OFF_TO_LBW_OFF(range->min) & rr_cfg->lbw_prop.addr_mask;
			WREG32(offset + rr_cfg->lbw_prop.min_off + instance_off, val);

			val = REG_OFF_TO_LBW_OFF(range->max) & rr_cfg->lbw_prop.addr_mask;
			WREG32(offset + rr_cfg->lbw_prop.max_off + instance_off, val);
		}
	}

	/* configure all HBW range types */
	for (rr_type = 0, rr_cfg = rr_hbw_config_array;
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
			val = FIELD_PREP(HD0_RRTR0_RTR_CTRL_RR_HBW_SEC_RANGE_EN_WR_M, wr_access) |
				FIELD_PREP(HD0_RRTR0_RTR_CTRL_RR_HBW_SEC_RANGE_EN_RD_M, rd_access);
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
	struct iterate_module_ctx ctx = {
		.fn = gaudi3_rtr_ctrl_config_rr,
	};

	gaudi3_iterate_rtr_ctrls(hdev, &ctx);
}

static void gaudi3_init_dtlb_pa_range_registers(struct hl_device *hdev, int block, int inst,
							u32 offset, struct iterate_module_ctx *ctx)
{
	struct dtlb_rr_cfg_data *cfg_data = ctx->data;

	/* set bits 20-39 of DRAM phys end address */
	WREG32(offset + DTLB_RR_GLBL_PA_END0_OFFSET, cfg_data->rr_glbl_pa_end0);
}

static void gaudi3_init_pa_range_registers(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct dtlb_rr_cfg_data cfg_data;
	struct iterate_module_ctx ctx;
	u64 dram_last_addr;

	dram_last_addr = prop->dram_end_address - 1;

	/* extract bits 20-39 (inclusive) from DRAM last address */
	cfg_data.rr_glbl_pa_end0 = lower_32_bits((dram_last_addr & GENMASK_ULL(39, 20)) >> 20);

	ctx.fn = gaudi3_init_dtlb_pa_range_registers;
	ctx.data = &cfg_data;
	gaudi3_iterate_dtlbs(hdev, &ctx);
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
	gaudi3_init_pa_range_registers(hdev);
}

static const u32 gaudi3_pb_kdma[] = {
	mmD0_SPDMA0_CH0_A_PQM_CH_BASE,
	mmD0_SPDMA0_CH0_B_PQM_CH_BASE,
};

static const u32 gaudi3_pb_hdcr0_sm_glbl[] = {
	mmHD0_SYNC_MNGR_GLBL_BASE,
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
};

static const struct range gaudi3_pb_hdcr_x_sm_glbl_unsecured_regs[] = {
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_BASE_ADDR_L_0,
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_BASE_ADDR_L_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_BASE_ADDR_H_0,
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_BASE_ADDR_H_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_SIZE_LOG2_0,
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_SIZE_LOG2_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_PI_0,
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_PI_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_ADDR_L_0,
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_ADDR_L_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_ADDR_H_0,
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_ADDR_H_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_DATA_0,
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_LBW_DATA_63},
	{mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_INC_MODE_0,
			mmHD0_SYNC_MNGR_GLBL_BASE + mmSOB_GLBL_CQ_INC_MODE_63},
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

	/* KDMA (D0_SPDMA0_CH0_A) */
	rc |= hl_init_pb(hdev, HL_PB_SHARED, HL_PB_NA,
			HL_PB_SINGLE_INSTANCE, HL_PB_NA,
			gaudi3_pb_kdma, ARRAY_SIZE(gaudi3_pb_kdma),
			NULL, HL_PB_NA);

	/* SYNC_MNGR_OBJS */
	rc |= gaudi3_init_pb_sm_objs(hdev);

	/* SYNC_MNGR_GLBL */

	/* Secure all registers except for CQ related registers */
	rc |= hl_init_pb_ranges(hdev, prop->num_of_hdcores, HDCORE_OFFSET,
			HL_PB_SINGLE_INSTANCE, HL_PB_NA,
			gaudi3_pb_hdcr0_sm_glbl,
			ARRAY_SIZE(gaudi3_pb_hdcr0_sm_glbl),
			gaudi3_pb_hdcr_x_sm_glbl_unsecured_regs,
			ARRAY_SIZE(gaudi3_pb_hdcr_x_sm_glbl_unsecured_regs));

	/* Secure the first GAUDI3_RESERVED_CQ_NUMBER CQ registers in HDCORE0 */
	rc |= hl_init_pb_ranges(hdev, HL_PB_SHARED, HL_PB_NA,
			HL_PB_SINGLE_INSTANCE, HL_PB_NA,
			gaudi3_pb_hdcr0_sm_glbl,
			ARRAY_SIZE(gaudi3_pb_hdcr0_sm_glbl),
			gaudi3_pb_hdcr0_sm_glbl_unsecured_regs,
			ARRAY_SIZE(gaudi3_pb_hdcr0_sm_glbl_unsecured_regs));

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

	if (!hdev->security_enable)
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
