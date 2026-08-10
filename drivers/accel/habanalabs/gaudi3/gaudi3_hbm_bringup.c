// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3P.h"
#include "gaudi3_masks.h"

/* Timeouts */
#define PHY_INIT_TIMEOUT_USEC			(100000)			/* 100ms */
#define TRAINING_COMPLETE_TIMEOUT_USEC		(PHY_INIT_TIMEOUT_USEC)
#define PHY_TESTS_TIMEOUT			(PHY_INIT_TIMEOUT_USEC)

#define PHY_CHIPLET_MASTER_OFFSET	0x3F000
#define PHY_CHIPLET_INITENG_OFFSET	0x3F800

/* PLDM */
#define PLDM_TIMEOUT				(10000000)			/* 10s */

#define phy_poll_timeout(hdev, hbm_dev, addr, val, cond, sleep_us, timeout_us) \
({ \
	ktime_t __timeout; \
	__timeout = ktime_add_us(ktime_get(), timeout_us); \
	might_sleep_if(sleep_us); \
	for (;;) { \
		(val) = hbm_phy_read(hdev, hbm_dev, addr); \
		if ((cond) || unlikely(!hdev->pdev && hdev->disabled)) \
			break; \
		if (timeout_us && ktime_compare(ktime_get(), __timeout) > 0) { \
			(val) = hbm_phy_read(hdev, hbm_dev, addr); \
			break; \
		} \
		if (sleep_us) \
			usleep_range((sleep_us >> 2) + 1, sleep_us); \
	} \
	(cond) ? 0 : -ETIMEDOUT; \
})

enum phy_pstate {
	PSTATE_0 = 0x0,
	PSTATE_1 = 0x1,
	PSTATE_2 = 0x2,
	PSTATE_3 = 0x3,
	PSTATE_NUM = 0x4
};

enum hbm_init_status {
	status_pass,
	status_fail,
	status_skip
};

static void phy_reset(struct hl_device *hdev, u64 offset);
static void bcast_mc_axis_init(struct hl_device *hdev, u64 offset);
static void bcast_mc_scheduler_init(struct hl_device *hdev, u64 offset);
static void bcast_mc_sequencer_init(struct hl_device *hdev, u64 offset);
static void bcast_mc_dfi_master_init(struct hl_device *hdev, u64 offset);
static void open_traffic(struct hl_device *hdev, u32 hbm_dev);
static void phy_dfi_init(struct hl_device *hdev, u32 hbm_dev);
static void phy_config_p1500(struct hl_device *hdev, u32 hbm_dev);
static void open_traffic_2(struct hl_device *hdev, u64 offset, u64 hif_offset);

void gaudi3_iterate_mcs(struct hl_device *hdev, struct iterate_module_ctx *ctx)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u8 die, hbm_idx, hbm_die_idx, mc_hbm_idx, mc_idx;
	u32 offset;

	for (die = 0; die < prop->num_of_dies; die++) {
		for (hbm_die_idx = 0; hbm_die_idx < NUM_HBM_PER_DIE; hbm_die_idx++) {
			hbm_idx = (die * NUM_HBM_PER_DIE) +  hbm_die_idx;

			if (!(prop->dram_enabled_mask & BIT(hbm_idx)))
				continue;

			/* bcast MC config */
			offset = mmD0_HBM0_BCAST_MC_BASE + (die * DIE_OFFSET) +
							(hbm_die_idx * HBM_DEV_OFFSET);
			ctx->fn(hdev, die, hbm_idx, offset, ctx);

			/* HBM MC configs */
			for (mc_hbm_idx = 0; mc_hbm_idx < NUM_MCS_PER_HBM; mc_hbm_idx++) {
				mc_idx = (hbm_idx * NUM_MCS_PER_HBM) + mc_hbm_idx;
				offset = mmD0_HBM0_MC0_BASE + (die * DIE_OFFSET) +
								(hbm_die_idx * HBM_DEV_OFFSET) +
								(mc_hbm_idx * HBM_MC_OFFSET);
				ctx->fn(hdev, die, mc_idx, offset, ctx);
			}
		}
	}
}

static int poll_on_phy_init(struct hl_device *hdev, u32 dev)
{
	u64 hbm_offset = (dev / 4) * DIE_OFFSET + (dev % 4) * HBM_DEV_OFFSET;
	u64 timeout = (hdev->pldm) ? PLDM_TIMEOUT : PHY_INIT_TIMEOUT_USEC;
	u32 sleep = (hdev->pldm) ? 10000 : 100;
	u32 reg_val = 0;
	int rc = 0;

	rc = hl_poll_timeout(
		hdev,
		hbm_offset + mmD0_HBM0_MC0_BASE + mmMC_CH_DFI_PHY_INIT_COMPLETE,
		reg_val,
		((reg_val & MC_CH_DFI_PHY_INIT_COMPLETE_VAL_M) == 0x1),
		sleep,
		timeout);

	if (rc) {
		hl_err(hdev,
			"Timeout while polling on HBM%d dfi_init_complete assertion\n",
			dev);
		return status_fail;
	}

	return status_pass;
}

static void phy_set_pstate(struct hl_device *hdev,
				 u32 hbm_offset,
				 enum phy_pstate pstate)
{
	RMWREG32(mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_PHY_INDIRECT_ACCESS + hbm_offset,
			(u32) pstate, 0x70);
}

static inline u8 phy_get_chiplet_type_and_offset(
		u32 addr, struct gaudi3_hbm *cfg)
{
	u8 chiplet_type = 0;

	if (addr >= mmD0_HBM0_PHY_P0_MASTER_BASE
		&& addr < mmD0_HBM0_PHY_P0_INITENG_BASE) {
		chiplet_type = 2;
		cfg->phy_offset = PHY_CHIPLET_MASTER_OFFSET;
	} else if (addr >= mmD0_HBM0_PHY_P0_INITENG_BASE
		&& addr < mmD0_HBM1_MC0_BASE) {
		chiplet_type = 9;
		cfg->phy_offset = PHY_CHIPLET_INITENG_OFFSET;
	} else {
		chiplet_type = 0;
		cfg->phy_offset = 0;
	}

	return chiplet_type;
}

static inline void hbm_phy_write(struct hl_device *hdev, u32 dev, u32 addr, u32 val)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_hbm *hbm_cfg = &gaudi3->hbm_cfg;
	u8 chiplet_type;
	u32 phy_addr, hbm_offset;

	hbm_offset = (dev / 4) * DIE_OFFSET + (dev % 4) * HBM_DEV_OFFSET;
	chiplet_type = phy_get_chiplet_type_and_offset(addr, hbm_cfg);
	phy_addr = hbm_offset + addr - hbm_cfg->phy_offset;

	/* set chiplet type only if needed */
	if (chiplet_type != hbm_cfg->phy_chiplet) {
		RMWREG32(mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_PHY_INDIRECT_ACCESS + hbm_offset,
				chiplet_type, 0xf);
		hbm_cfg->phy_chiplet = chiplet_type;
	}

	WREG32(phy_addr, val);
}

static inline u32 hbm_phy_read(struct hl_device *hdev, u32 dev, u32 addr)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_hbm *hbm_cfg = &gaudi3->hbm_cfg;
	u8 chiplet_type;
	u32 phy_addr, hbm_offset;

	hbm_offset = (dev / 4) * DIE_OFFSET + (dev % 4) * HBM_DEV_OFFSET;
	chiplet_type = phy_get_chiplet_type_and_offset(addr, hbm_cfg);
	phy_addr = hbm_offset + addr - hbm_cfg->phy_offset;

	/* set chiplet type only if needed */
	if (chiplet_type != hbm_cfg->phy_chiplet) {
		RMWREG32(mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_PHY_INDIRECT_ACCESS + hbm_offset,
				chiplet_type, 0xf);
		hbm_cfg->phy_chiplet = chiplet_type;
	}

	return RREG32(phy_addr);
}

static int phy_poll_on_training(struct hl_device *hdev, u32 hbm_dev)
{
	u64 timeout = (hdev->pldm) ? PLDM_TIMEOUT : TRAINING_COMPLETE_TIMEOUT_USEC;
	int rc = status_pass;
	u32 reg_val = 0;

	rc = phy_poll_timeout(
		hdev,
		hbm_dev,
		mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_TRAINSTATUS,
		reg_val,
		(reg_val & HBM_PHY_P0_MASTER_TRAINSTATUS_TRAINDONE_M),
		100,
		timeout);

	if (rc) {
		hl_err(hdev,
			"HBM%d Timeout while polling for training FSM completion. TrainStatus: 0x%x\n",
			hbm_dev, reg_val);
		rc = status_fail;
	}

	return rc;
}

static void phy_init_config(struct hl_device *hdev, u32 hbm_dev)
{
	u64 hbm_offset = (hbm_dev / 4) * DIE_OFFSET + (hbm_dev % 4) * HBM_DEV_OFFSET;
	u64 hif_offset = (u64)hbm_dev * HDCORE_OFFSET;

	phy_reset(hdev, hbm_offset);
	bcast_mc_axis_init(hdev, hbm_offset);
	bcast_mc_scheduler_init(hdev, hbm_offset);
	bcast_mc_sequencer_init(hdev, hbm_offset);
	bcast_mc_dfi_master_init(hdev, hbm_offset);

	open_traffic(hdev, hbm_dev);
	phy_dfi_init(hdev, hbm_dev);
	phy_config_p1500(hdev, hbm_dev);
	open_traffic_2(hdev, hbm_offset, hif_offset);
}

static void enable_hbm_compression(struct hl_device *hdev, int hbm_dev)
{
	u64 hbm_offset, hif_offset;
	u32 val;

	if (!hdev->hbm_compression_enable)
		return;

	hbm_offset = (hbm_dev / 4) * DIE_OFFSET + (hbm_dev % 4) * HBM_DEV_OFFSET;
	hif_offset = (u64)hbm_dev * HDCORE_OFFSET;


	val = FIELD_PREP(MC_CMN_COMPRS_MOD_M, 0xFFFF);
	val |= FIELD_PREP(MC_CMN_COMPRS_EN_M, 0x1);
	WREG32(hbm_offset + mmD0_HBM0_BCAST_CMN_BASE + mmMC_CMN_COMPRS, val);

	WREG32(hif_offset + mmHD0_HIF_BASE + mmH9_HIF_MC_MAX_RD_INFLIGHT_0, 0x28);
	WREG32(hif_offset + mmHD0_HIF_BASE + mmH9_HIF_MC_MAX_RD_INFLIGHT_1, 0x28);
	WREG32(hif_offset + mmHD0_HIF_BASE + mmH9_HIF_MC_MAX_RD_INFLIGHT_2, 0x28);
	WREG32(hif_offset + mmHD0_HIF_BASE + mmH9_HIF_MC_MAX_RD_INFLIGHT_3, 0x28);
}

int gaudi3_init_hbm(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_hbm *hbm_cfg = &gaudi3->hbm_cfg;

	int dev;

	if (!hdev->dram_enable)
		return 0;

	if (gaudi3->hw_cap_initialized & HW_CAP_DRAM)
		return 0;

	if (!hdev->hbm_compression_enable)
		hl_dbg(hdev, "HBM compression is disabled\n");

	/* if preboot does HBM init then skip it */
	if ((hdev->fw_components & FW_TYPE_PREBOOT_CPU) &&
		(prop->fw_preboot_cpu_boot_dev_sts0 & CPU_BOOT_DEV_STS0_DRAM_INIT_EN))
		return 0;

	for (dev = 0 ; dev < (hdev->asic_prop.num_of_dies * NUM_HBM_PER_DIE); dev++) {
		if (!(prop->dram_enabled_mask & BIT_ULL(dev)))
			continue;

		hbm_cfg->phy_chiplet = 0xff;
		phy_init_config(hdev, dev);
		enable_hbm_compression(hdev, dev);
	}

	gaudi3_init_mc(hdev);
	gaudi3->hw_cap_initialized |= HW_CAP_DRAM;

	return 0;
}

/**************************************
 ** Below content is auto-generated  **
 **       DO NOT EDIT BELOW          **
 **************************************/

static void phy_reset(struct hl_device *hdev, u64 offset)
{
	WREG32(offset + mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_PHY_POWERGOOD, 0x0);
	WREG32(offset + mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_PHY_HBMPHY_RESET, 0x1);
	WREG32(offset + mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_PHY_PRESETN, 0x0);
	WREG32(offset + mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_PHY_DFI_RESET_N, 0x0);
	WREG32(offset + mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_PHY_POWERGOOD, 0x1);
	WREG32(offset + mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_PHY_HBMPHY_RESET, 0x0);
	WREG32(offset + mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_PHY_PRESETN, 0x1);
	WREG32(offset + mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_PHY_DFI_RESET_N, 0x1);
	/* Configure PHY_PPROT_PIN = 0 i.e. PHY access rights = secured */
	WREG32(offset + mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_PHY_PPROT_PIN, 0x0);
}

static void bcast_mc_axis_init(struct hl_device *hdev, u64 offset)
{
	WREG32(offset + mmD0_HBM0_BCAST_CMN_BASE + mmMC_CMN_COMPRS, 0xab70);
	WREG32(offset + mmD0_HBM0_CENTRAL_BASE + mmHBM_CENTRAL_P1500_THROT_MON, 0x14100);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_AXIS_CLIP_SIZE, 0x3c003c);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_ECC_CFG, 0x1);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_PREFETCH_NUM_1, 0x7);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_PREFETCH_NUM_11, 0x4);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_PFETCH_OPEN_SCORE, 0x96);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_PFETCH_CLOSED_SCORE, 0x96);
}

static void bcast_mc_scheduler_init(struct hl_device *hdev, u64 offset)
{
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_REF_ARB_TH_VAL, 0xffff);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SAT_MED_RPSB, 0x1);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SAT_HIGH_RPSB, 0x3);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_MIN_SATURATED, 0x14);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_WR_SAT_MED_RPSB, 0x1);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_WR_SAT_HIGH_RPSB, 0x3);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_WR_MIN_SATURATED, 0x14);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_0, 0x80);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_1, 0x168);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_2, 0x1b8);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_3, 0x1ff);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_QOS_ALMOST_FULL_TH_0, 0x1e);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_QOS_ALMOST_FULL_TH_1, 0x1e);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_QOS_ALMOST_FULL_TH_2, 0x1e);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_QOS_ALMOST_FULL_TH_3, 0x1e);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_WR_QOS_ALMOST_FULL_TH, 0x1e);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RDB_CREDIT_INIT, 0x40);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_QOS0_SCORE_1, 0x32);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_QOS1_SCORE_1, 0x32);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_QOS2_SCORE_1, 0x32);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_QOS3_SCORE_1, 0x32);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_QOS0_SCORE_2, 0x50);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_QOS1_SCORE_2, 0x50);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_QOS2_SCORE_2, 0x50);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_QOS3_SCORE_2, 0x50);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_QOS0_SCORE_3, 0x82);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_QOS1_SCORE_3, 0x82);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_QOS2_SCORE_3, 0x82);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_QOS3_SCORE_3, 0x82);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_LATENCY_LEVEL_SAT_SCORE, 0xc8);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_OPEN_PAGE_SCORE, 0x32);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_WR_OPEN_PAGE_SCORE, 0x32);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SID_SCORE, 0xf0);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_BG_SCORE, 0xd2);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_WR_BG_SCORE, 0xd2);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_WR_SWITCH_OCCUPANCY_LEVEL_QOS_0,
			0x32);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_WR_SWITCH_OCCUPANCY_LEVEL_QOS_1,
			0x32);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_WR_MIN_STREAM_LENGTH, 0x23);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_LATENCY_LEVEL_QOS0_0,
			0x168);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_LATENCY_LEVEL_QOS0_1,
			0x1b8);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_LATENCY_LEVEL_QOS1_0,
			0x168);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_LATENCY_LEVEL_QOS1_1,
			0x1b8);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_LATENCY_LEVEL_QOS2_0,
			0x168);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_LATENCY_LEVEL_QOS2_1,
			0x1b8);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_LATENCY_LEVEL_QOS3_0,
			0x168);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_LATENCY_LEVEL_QOS3_1,
			0x1b8);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_LATENCY_MAX, 0x1ff);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS0_0,
			0x14);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS0_1,
			0xf);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS1_0,
			0x14);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS1_1,
			0xf);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS2_0,
			0x14);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS2_1,
			0xf);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS3_0,
			0x14);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS3_1,
			0xf);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_RSCH_MAX_PAGE, 0x1f);
}

static void bcast_mc_sequencer_init(struct hl_device *hdev, u64 offset)
{
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TWTR_S, 0x9);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TWTR_L, 0xe);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TWTP, 0x17);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TRTW, 0x12);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TRAS, 0x1c);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TRTP, 0x3);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TRP, 0xf);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TRRD_L, 0x2);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TRRD_S, 0x2);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TFAW, 0xa);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TCCD_L, 0x1);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TCCD_R, 0x1);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TRCD_RD, 0x1e);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TRCD_WR, 0x12);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFI_0, 0x36d7);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFI_1, 0x1b6b);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFI_2, 0x6da);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFI_3, 0xdb5);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFI_4, 0xdb5);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFI_5, 0xdb5);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFI_6, 0x36d);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFI_7, 0xdb5);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFISB_ADJ_0, 0x1ad);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFISB_ADJ_1, 0xd6);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFISB_ADJ_2, 0x35);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFISB_ADJ_3, 0x6b);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFISB_ADJ_4, 0x6b);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFISB_ADJ_5, 0x6b);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFISB_ADJ_6, 0x1a);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFISB_ADJ_7, 0x6b);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TREFI_TH, 0x3e8);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TRFC, 0x194);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TRFCSB, 0xb3);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TWTRAP, 0x14);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TCKSRE, 0x8);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TCKSRX, 0x8);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TCKPDE, 0x6);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TXS, 0x19d);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TXP, 0x6);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TRREFD, 0x7);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_TMOD, 0xd);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_REFSB_CNT_URGENT_TH, 0x8);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_REFSB_BURST_NUM_URGENT, 0x1);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_REFSB_CNT_HYSTERIC_TH, 0x1c);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_REFSB_BURST_NUM_HYSTERIC, 0x14);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_CATTRIP_THROT_EN, 0x1);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_CBQ_FIFO_FULL_TH, 0xa);
}

static void bcast_mc_dfi_master_init(struct hl_device *hdev, u64 offset)
{
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_DFI_MC_CHANNEL_CFG, 0x22);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_DFI_PHY_CFG_CH, 0x3ffe9a4);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_DFI_PHY_CFG, 0x140f22af);
	// disable address xor in PLDM mode
	if (hdev->pldm)
		WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_DFI_PHY_CFG_2, 0x1f8fa1b7);
	else
		WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_DFI_PHY_CFG_2, 0x1f8fa3b7);

}

static void open_traffic(struct hl_device *hdev, u32 hbm_dev)
{
	u64 hbm_offset = (hbm_dev / 4) * DIE_OFFSET + (hbm_dev % 4) * HBM_DEV_OFFSET;

	phy_set_pstate(hdev, hbm_offset, PSTATE_0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH0_AW_BASE + mmHBM_PHY_CHANNELS_CH0_AW_CHANENABLE, 0x1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH1_AW_BASE + mmHBM_PHY_CHANNELS_CH1_AW_CHANENABLE, 0x1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH2_AW_BASE + mmHBM_PHY_CHANNELS_CH2_AW_CHANENABLE, 0x1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH3_AW_BASE + mmHBM_PHY_CHANNELS_CH3_AW_CHANENABLE, 0x1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH4_AW_BASE + mmHBM_PHY_CHANNELS_CH4_AW_CHANENABLE, 0x1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH5_AW_BASE + mmHBM_PHY_CHANNELS_CH5_AW_CHANENABLE, 0x1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH6_AW_BASE + mmHBM_PHY_CHANNELS_CH6_AW_CHANENABLE, 0x1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH7_AW_BASE + mmHBM_PHY_CHANNELS_CH7_AW_CHANENABLE, 0x1);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_DFIPHYUPD, 0x0);
	phy_set_pstate(hdev, hbm_offset, PSTATE_0);
	hbm_phy_write(hdev, hbm_dev, mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_MR0, 0x77);
	hbm_phy_write(hdev, hbm_dev, mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_MR1, 0x5f);
	hbm_phy_write(hdev, hbm_dev, mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_MR2, 0x32);
	hbm_phy_write(hdev, hbm_dev, mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_MR3, 0xf8);
	hbm_phy_write(hdev, hbm_dev, mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_MR4, 0x3f);
	phy_set_pstate(hdev, hbm_offset, PSTATE_0);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_DRAMPARAM0_P0,
		0x3219);
	phy_set_pstate(hdev, hbm_offset, PSTATE_1);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_DRAMPARAM0_P0,
		0x3219);
	phy_set_pstate(hdev, hbm_offset, PSTATE_2);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_DRAMPARAM0_P0,
		0x3219);
	phy_set_pstate(hdev, hbm_offset, PSTATE_3);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_DRAMPARAM0_P0,
		0x3219);
	phy_set_pstate(hdev, hbm_offset, PSTATE_0);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_DRAMPARAM1_P0, 0x1ff);
	phy_set_pstate(hdev, hbm_offset, PSTATE_1);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_DRAMPARAM1_P0, 0x1ff);
	phy_set_pstate(hdev, hbm_offset, PSTATE_2);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_DRAMPARAM1_P0, 0x1ff);
	phy_set_pstate(hdev, hbm_offset, PSTATE_3);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_DRAMPARAM1_P0, 0x1ff);
	phy_set_pstate(hdev, hbm_offset, PSTATE_0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_CALMISC, 0x8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_CALMISC2, 0x2);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_CALSCALECTRL, 0x40ef);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_CALINITVALS, 0x1428);
	phy_set_pstate(hdev, hbm_offset, PSTATE_0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH0_AW_BASE + mmHBM_PHY_CHANNELS_CH0_AW_RXPWRDN, 0x7070);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH1_AW_BASE + mmHBM_PHY_CHANNELS_CH1_AW_RXPWRDN, 0x7070);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH2_AW_BASE + mmHBM_PHY_CHANNELS_CH2_AW_RXPWRDN, 0x7070);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH3_AW_BASE + mmHBM_PHY_CHANNELS_CH3_AW_RXPWRDN, 0x7070);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH4_AW_BASE + mmHBM_PHY_CHANNELS_CH4_AW_RXPWRDN, 0x7070);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH5_AW_BASE + mmHBM_PHY_CHANNELS_CH5_AW_RXPWRDN, 0x7070);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH6_AW_BASE + mmHBM_PHY_CHANNELS_CH6_AW_RXPWRDN, 0x7070);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_CH7_AW_BASE + mmHBM_PHY_CHANNELS_CH7_AW_RXPWRDN, 0x7070);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_LCDLGAINCTL_P0, 0x62);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_LCDLLOCKPARAM_P0,
		0x1a0);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_LCDLTRAINPARAM, 0x2);
	phy_set_pstate(hdev, hbm_offset, PSTATE_0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_MASTERCTRL, 0x1804);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_DRAMTIMING0, 0x28);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_DRAMTIMING1, 0x50);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_DRAMTIMING2, 0x11cc);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_DRAMTIMING3, 0x679b);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_DRAMTIMING4, 0x3d2f);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_DRAMTIMING5, 0xe11e);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_DRAMTIMING8, 0x72);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_PLLCTRL4_P0, 0xd8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_PLLCTRL0, 0x86);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_PLLTESTMODE_P0, 0xa25);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_PLLCTRL2_P0, 0x18);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_PLLCTRL1_P0, 0x21);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_CALCMPSTARTUPTIME_P0,
			0x714);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_CALSAMPLETIME_P0, 0x16b);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_CALOFFSETSAMPLETIME_P0,
			0x110);
	phy_set_pstate(hdev, hbm_offset, PSTATE_0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDISABLEFLAG0, 0xfff0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDISABLEFLAG1, 0xffff);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDISABLEFLAG2, 0xfff0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDISABLEFLAG3, 0xffef);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDFIFREQXLAT0, 0x3210);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDFIFREQXLAT7, 0x4000);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDLY0_P0, 0x1190);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDLY1_P0, 0x1c1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDLY2_P0, 0xe0);
	phy_set_pstate(hdev, hbm_offset, PSTATE_1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDLY0_P0, 0xcea);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDLY1_P0, 0x14a);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDLY2_P0, 0xa5);
	phy_set_pstate(hdev, hbm_offset, PSTATE_2);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDLY0_P0, 0xa37);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDLY1_P0, 0x105);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDLY2_P0, 0x82);
	phy_set_pstate(hdev, hbm_offset, PSTATE_3);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDLY0_P0, 0x872);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDLY1_P0, 0xd8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQDLY2_P0, 0x6c);
	phy_set_pstate(hdev, hbm_offset, PSTATE_0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQPRE0S2, 0x400);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQPRE1S2, 0x400);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG0S0, 0x8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG0S1, 0x268);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG0S2, 0x8410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG1S0, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG1S1, 0x268);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG1S2, 0x8410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG2S0, 0x7f8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG2S1, 0x368);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG2S2, 0x8c10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG3S0, 0x430);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG3S1, 0x2d0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG3S2, 0x8c10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG4S0, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG4S1, 0x220);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG4S2, 0x8c10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG5S0, 0x10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG5S1, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG5S2, 0x1c00);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG6S0, 0x7ff);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG6S1, 0x368);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG6S2, 0x9c10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG7S0, 0x4b0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG7S1, 0x2d0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG7S2, 0x9c10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG8S0, 0x8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG8S1, 0x338);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG8S2, 0x9c10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG9S0, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG9S1, 0x220);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG9S2, 0x9c10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG10S0, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG10S1, 0x1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG10S2, 0x400);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG11S0, 0x7f8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG11S1, 0x220);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG11S2, 0x9c10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG12S0, 0x3);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG12S1, 0x6a);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG12S2, 0x1410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG13S0, 0x20);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG13S1, 0x788);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG13S2, 0x8448);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG14S0, 0x8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG14S1, 0x4);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG14S2, 0x400);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG15S0, 0x7);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG15S1, 0x338);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG15S2, 0x8410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG16S0, 0x437);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG16S1, 0x2d0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG16S2, 0x8c10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG17S0, 0x570);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG17S1, 0x2d0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG17S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG18S0, 0x8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG18S1, 0x4);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG18S2, 0x1400);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG19S0, 0x530);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG19S1, 0x2d0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG19S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG20S0, 0x10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG20S1, 0x4);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG20S2, 0x1400);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG21S0, 0x430);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG21S1, 0x2d0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG21S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG22S0, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG22S1, 0x4);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG22S2, 0x400);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG23S0, 0x10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG23S1, 0x4);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG23S2, 0xc00);
	// pllBypassMode in PLDM mode
	if (hdev->pldm)
		hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG24S0, 0x4b0);
	else
		hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG24S0, 0x410);

	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG24S1, 0x2d0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG24S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG25S0, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG25S1, 0x1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG25S2, 0x400);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG26S0, 0x38);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG26S1, 0x788);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG26S2, 0x9448);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG27S0, 0xb);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG27S1, 0x40);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG27S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG28S0, 0x8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG28S1, 0x208);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG28S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG29S0, 0x18);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG29S1, 0x40);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG29S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG30S0, 0x7);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG30S1, 0x208);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG30S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG31S0, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG31S1, 0x1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG31S2, 0x1400);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG32S0, 0x8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG32S1, 0x40);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG32S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG33S0, 0x3);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG33S1, 0x40);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG33S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG34S0, 0x6);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG34S1, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG34S2, 0x1400);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG35S0, 0x7);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG35S1, 0x368);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG35S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG36S0, 0x11);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG36S1, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG36S2, 0x1400);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG37S0, 0x8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG37S1, 0x360);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG37S2, 0x8410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG38S0, 0x5b0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG38S1, 0x70);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG38S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG39S0, 0xf);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG39S1, 0xf0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG39S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG40S0, 0x7);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG40S1, 0xf0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG40S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG41S0, 0x11);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG41S1, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG41S2, 0x1400);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG42S0, 0x10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG42S1, 0x40);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG42S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG43S0, 0x7f8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG43S1, 0x220);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG43S2, 0x9410);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG44S0, 0x10);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG44S1, 0x788);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG44S2, 0x9448);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG45S0, 0xff80);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG45S1, 0x6ff);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG45S2, 0x8448);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG46S0, 0xfff8);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG46S1, 0x6f7);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG46S2, 0x8448);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG47S0, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG47S1, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_INITENG_BASE +
			mmHBM2_PHY_P0_INITENG_INITSEQREG47S2, 0x0);
}

static void phy_dfi_init(struct hl_device *hdev, u32 hbm_dev)
{
	u64 offset = (hbm_dev / 4) * DIE_OFFSET + (hbm_dev % 4) * HBM_DEV_OFFSET;

	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_DFI_PHY_INIT_START, 0x1);
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_DFI_PHY_CFG_CH, 0x13ffe9a4);

	if (poll_on_phy_init(hdev, hbm_dev))
		return;

	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_DFI_PHY_INIT_START, 0x0);
}

static void phy_config_p1500(struct hl_device *hdev, u32 hbm_dev)
{
	u64 hbm_offset = (hbm_dev / 4) * DIE_OFFSET + (hbm_dev % 4) * HBM_DEV_OFFSET;

	phy_set_pstate(hdev, hbm_offset, PSTATE_0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_P1500CTRL, 0x5);
#ifdef HBM_TRAIN
	// HBM reset + MRS + training
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_TRAINCFG, 0x15);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_TRAINCTRL, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_TRAINCTRL, 0xff);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_TRAINCTRL, 0xfe);
#else
	// HBM reset + MRS
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_TRAINCFG, 0x14);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_TRAINCTRL, 0x0);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_TRAINCTRL, 0xc1);
	hbm_phy_write(hdev, hbm_dev,
			mmD0_HBM0_PHY_P0_MASTER_BASE + mmHBM_PHY_P0_MASTER_TRAINCTRL, 0xc0);
#endif
	phy_poll_on_training(hdev, hbm_dev);
	mdelay(2000);
	phy_set_pstate(hdev, hbm_offset, PSTATE_0);
	hbm_phy_write(hdev, hbm_dev,
		mmD0_HBM0_PHY_P0_CH_BCAST_AW_BASE + mmHBM2_PHY_P0_CH_BCAST_AW_DFIPHYUPD, 0xf);
	phy_set_pstate(hdev, hbm_offset, PSTATE_0);
}

static void open_traffic_2(struct hl_device *hdev, u64 offset, u64 hif_offset)
{
	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_DFI_ECC_SEC_THLD, 0x12);
	WREG32(offset + mmD0_HBM0_BCAST_CMN_INTR_BASE + mmMC_CMN_INTR_SEI0_MASK, 0x0);
	WREG32(offset + mmD0_HBM0_BCAST_CMN_INTR_BASE + mmMC_CMN_INTR_SEI1_MASK, 0x0);
	WREG32(offset + mmD0_HBM0_BCAST_CMN_INTR_BASE + mmMC_CMN_INTR_SPI_MASK, 0x0);
	// disable address xor in PLDM mode
	if (hdev->pldm)
		WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_DFI_PHY_CFG_2, 0x3f8fa1b7);
	else
		WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_DFI_PHY_CFG_2, 0x3f8fa3b7);

	WREG32(offset + mmD0_HBM0_BCAST_MC_BASE + mmMC_CH_SEQ_REF_MODE, 0x2);
	WREG32(hif_offset + mmHD0_HIF_BASE + mmH9_HIF_HBM_AXI_GLBL_TAP_EN, 0xf);
}
