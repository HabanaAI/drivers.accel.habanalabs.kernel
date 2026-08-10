// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi2P.h"
#include "gaudi2_masks.h"
#include "gaudi2_hbm_mbist.h"

/* Timeouts */
#define PHY_INIT_TIMEOUT_USEC			(100000)			/* 100ms */
#define TRAINING_COMPLETE_TIMEOUT_USEC		(PHY_INIT_TIMEOUT_USEC)
#define PHY_TESTS_TIMEOUT			(PHY_INIT_TIMEOUT_USEC)
#define MCBIST_TIMEOUT_USEC			(PHY_INIT_TIMEOUT_USEC * 5)	/* 500ms */

/* Gaudi2 properties */
#define HBM_DEVICES_NUM				(6)
#define MC_CHANNELS_NUM				(4)
#define HBM_MC_NUM				(2)
#define HBM_PC_NUM				(16)
#define PHY_CFG_BASE				(0x5040000)
#define PHY_CHIPLET_SHIFT			(18)
#define PHY_CHIPLET_MASK			(0x3c0000)
#define PHY_ADDR_MASK				(0x3FFFF)
#define HBM_DEV_OFFSET				(0x80000)
#define MC_OFFSET				(0x20000)
#define MCBIST_OFFSET				(0x1000)
#define PC_OFFSET				(0x4)
#define BYTE_OFFSET				(0x4)
#define DW_OFFSET				(0x400)
#define PHY_CH_OFFSET				(0x4000)
#define HDR_TO_SDR				(2)
#define TRAINING_EYE_TH				(50)
#define TRAINSTATUS_MASK			(0x1FFF)
#define MC_P1500_WDR_SIZE_MAX			(128)
#define P1500_WDR_TEMP_VALID_BIT_MASK		(0x80)
#define PHY_VREF_OPTIONS			(127)				/* 1 - 127 */
#define PHY_VREF_RATIO_TH_PRCNT			(30)
#define PHY_VREF_DEFAULT			(0x46)
#define RDQS_RISING_FALLING_EDGES_FACTOR	(2)

#define MCBIST_FAIL_REPAIRABLE_SHIFT		(0)
#define MCBIST_FAIL_UNREPAIRABLE_SHIFT		(16)
#define MCBIST_ERR				(U32_MAX)

#define HBM_PHY_CHANNELS_NUM			(8)
#define HBM_CHANNEL_DWORDS_NUM			(4)

/* PLDM */
#define PLDM_TIMEOUT				(10000000)			/* 10s */
#define PLDM_SKIP_MCBIST			(1)
#define PLDM_SKIP_SCRUB				(1)

/* Debug */
#undef	DEBUG
#define AUTO_TEMP_EN				(0)
#define BU_THROT_EN				(0)
#define SLR_ENABLE				(0)
#define HBM_DUMP_BUS				(0)
#define PRINT_LCDL_1UI				(0)
#define RUN_PHY_MISR				(0)
#define ROW_REPAIR_ENABLE			(0)

#define SPACES40                                "                                        "
#define SPACES10                                "          "
#define SPACES5					"     "
#define BITS_PER_REG32				(32)

#define PHY_RREG(addr) hbm_phy_read(hdev, dev, addr)
#define PHY_WREG(addr, val) hbm_phy_write(hdev, dev, addr, val)
#define CEIL(numerator, denominator) ((numerator + denominator - 1) / (denominator))

#ifndef MIN
#define MIN(a, b) ((a < b) ? a : b)
#endif

#ifndef MAX
#define MAX(a, b) ((a > b) ? a : b)
#endif


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

enum hbm_training_type {
	train_type_hw_only,
	train_type_semi_auto,
	train_type_sw,
	train_type_pldm
};

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

enum hbm_drive_strength {
	drive_strength_6ma,
	drive_strength_9ma,
	drive_strength_12ma,
	drive_strength_15ma,
	drive_strength_18ma
};

enum hbm_vref {
	p50vddq,
	p46vddq,
	p42vddq,
	p38vddq,
	p54vddq,
	p58vddq,
	p62vddq,
	p66vddq,
	hbm_vref_steps
};

enum pubmode {
	pubmode_exit,
	pubmode_enter
};

enum lanerepair_err {
	repairable,
	unrepairable
};

enum dump_component {
	HCON0,
	HCON1,
	MC0,
	MC1,
	PHY
};

enum mcbist_data_mode {
	MCBIST_PRBS = 0,
	MCBIST_CMD_CNT = 1,
	MCBIST_ALL_1 = 2,
	MCBIST_ALL_0 = 3,
	MCBIST_SLOW_00_FF = 4,
	MCBIST_SLOW_55_AA = 5,
	MCBIST_FAST_00_FF = 6,
	MCBIST_FAST_55_AA = 7,
	MCBIST_BEAT_CNT = 8,
	MCBIST_MEDIUM_00_FF = 9,
	MCBIST_MEDIUM_55_AA = 10
};

enum p1500_op {
	wir_only = 0x0,
	wir_write = 0x1,
	wir_read = 0x2,
	write_only = 0x3,
	read_only = 0x4
};

enum p1500_instruction {
	ieee1500_bypass = 0x00,
	ieee1500_extest_rx = 0x01,
	ieee1500_extest_tx = 0x02,
	ieee1500_intest_rx = 0x03,
	ieee1500_intest_tx = 0x04,
	ieee1500_hbm_reset = 0x05,
	ieee1500_mbist = 0x06,		/* Not used */
	ieee1500_soft_repair = 0x07,
	ieee1500_hard_repair = 0x08,
	ieee1500_dword_misr = 0x09,
	ieee1500_aword_misr = 0x0a,
	ieee1500_channel_id = 0x0b,
	ieee1500_misr_mask = 0x0c,
	ieee1500_aword_misr_config = 0x0d,
	ieee1500_device_id = 0x0e,
	ieee1500_temperature = 0x0f,
	ieee1500_mode_register_dump_set = 0x10,
	ieee1500_read_lfsr_compare_sticky = 0x11,
	ieee1500_soft_lane_repair = 0x12,
	ieee1500_hard_lane_repair = 0x13,
	ieee1500_mbist_a6 = 0xa6,
	ieee1500_mbist_a7 = 0xa7
};

enum hbm_mcbist_mode {
	single_pc = 0x0,
	parallel_all_pc = 0x1
};

struct lcdl {
	u16 min;
	u16 max;
	u16 final;
	u16 ratio_1ui;
};

struct rx_training {
	struct lcdl rdqs_rise;
	struct lcdl rdqs_fall;
	u16 read_latency;
};

struct tx_training {
	struct lcdl dq;
	/* WDQS transmit delay (normally not trained) */
	struct lcdl wdqs;
};

struct dw_training {
	struct rx_training rx;
	struct tx_training tx;
};

struct vref_training {
	u16 min;
	u16 max;
};

struct pc_err_bitmap {
	u64 dq;
	u8 dm;
};

struct mrs_cmd {
	u8 num;
	u8 val;
};

struct mcbist_cfg {
	enum mcbist_data_mode data_mode;
	u32 block_size;		/* maximal block size is 0x10000, the granularity is 32B */
	u32 start_addr;		/* 30 bits (1GB per PC) */
	u32 dbi_mode;
	u16 poly;
	u16 seed;
	u16 sram_bist_sel;	/* bitmap for which PCs run SRAM BIST, may indicate multiple */
	u16 rep_num;		/* maximal rep_num is 0xFFFF */
	u16 loop_num;		/* maximal loop_num is 0xFFFF */
	u8 stop_on_err;
	u8 pc;			/* if mcbist_mode == single_pc, pc should have a valid(0-15) val */
	enum hbm_mcbist_mode mcbist_mode;
};

struct point {
	u32 x_coord;
	u32 y_coord;
};

/**
 * struct hbm_ch_lane_remap_info - specifies remapped lanes information per HBM channel
 * @aw: specifies which row and column lanes are remapped
 * @dw: each array entry specifies which DWORD byte data bus lanes are remapped
 */
struct hbm_ch_lane_remap_info {
	u8 aw;
	u16 dw[HBM_CHANNEL_DWORDS_NUM];
};

/**
 * struct hbm_hw_training - specifies remapped lanes information per HBM channel
 * @training_type: selector for PHY training algorithm
 * @train_ca: enable flag for C/A training
 * @train_rl: enable flag for read latnecy training
 * @train_rdeye: enable flag for read-eye training
 * @train_wreye: enable flag for write-eye training
 * @train_vref: enable flag for VREF training
 * @mrs: enable flag for HBM mode registers set command
 * @hbm_reset: enable flag for HBM reset and initialization sequence
 * @cmd_repeat: number of times an HBM command has to be executed
 * @data_pattern: 0 for LFSR preset mode, 1 for LFSR mode
 */
struct hbm_hw_training {
	enum hbm_training_type training_type;
	u32 train_ca;
	u32 train_rl;
	u32 train_rdeye;
	u32 train_wreye;
	u32 train_vref;
	u32 mrs;
	u32 hbm_reset;
	u8 cmd_repeat;
	u8 data_pattern;
};

/*
 * struct hbm_ac_params - hold various timing parameters for HBM device, MC and PHY
 * @mc_tfaw: Four bank activate window
 * mc_trrd_l: ACT/SBREF bank A to ACT/SBREF bank B (same BG)
 * mc_trrd_s: ACT/SBREF bank A to ACT/SBREF bank B (different BG)
 * mc_tras: ACTIVATE to PRECHARGE
 * mc_trp: PRECHARGE command period
 * mc_trc: ACTIVATE to ACTIVATE
 * mc_trcd_rd: ACTIVATE to READ
 * mc_trcd_wr: ACTIVATE to WRITE
 * mc_trtp:READ to PRECHARGE
 * mc_trfc: REFRESH command period
 * mc_trfcsb: SBREF command period
 * mc_trrefd: SBREF to ACTIVATE (different bank)
 * mc_trefi[8]: average periodic refresh interval
 * mc_trefisb_adj: TODO
 * mc_tmod: MRS command delay
 * mc_tcksre: TODO
 * mc_tcksrx TODO
 * mc_tckpde: depends on ck_dis_pd (SEQ_PWR)
 * mc_txs: self-refresh exit delay
 * mc_txp: power-down exit delay
 * mc_refsb_gap: TODO
 * hbm_twtr_s: TODO
 * hbm_twtr_l: TODO
 * trd_data_en: TODO
 * tphy_wr_lat: TODO
 * ctrlupd_max_t: TODO
 * twr: write recovery (nCK)
 * wr_lat: TODO
 * rd_lat: TODO
 * parity_lat: TODO
 * extend_phased_time: TODO
 * t_pwreset: TODO
 * t_init5: TODO
 */
struct hbm_ac_params {
	u32 mc_tfaw;
	u32 mc_trrd_l;
	u32 mc_trrd_s;
	u32 mc_tras;
	u32 mc_trp;
	u32 mc_trc;
	u32 mc_trcd_rd;
	u32 mc_trcd_wr;
	u32 mc_trtp;
	u32 mc_trfc;
	u32 mc_trfcsb;
	u32 mc_trrefd;
	u32 mc_tccd_l;
	u32 mc_tccd_s;
	u32 mc_tccd_r;
	u32 mc_twtr_s;
	u32 mc_twtr_l;
	u32 mc_twtrap;
	u32 mc_trtw;
	u32 mc_twtp;
	u32 mc_trefi[8];
	u32 mc_trefisb_adj;
	u32 mc_tmod;
	u32 mc_tcksre;
	u32 mc_tcksrx;
	u32 mc_tckpde;		/* depends on ck_dis_pd (SEQ_PWR) */
	u32 mc_txs;
	u32 mc_txp;
	u32 mc_refsb_gap;
	u32 hbm_twtr_s;
	u32 hbm_twtr_l;
	/* DFI_CFG */
	u32 trd_data_en;	/* RL - 2 */
	u32 tphy_wr_lat;	/* WL - 2 */
	/* DFI_CFG_2 */
	u32 ctrlupd_max_t;	/* 54 + extend_phased_time */
	/* MR1 */
	u32 twr;
	/* MR2*/
	u32 wr_lat;
	u32 rd_lat;
	/* MR4 */
	u32 parity_lat;
	u32 extend_phased_time;
	u32 t_pwreset;		/* [nCK]. Flashbolt: 1000ns  */
	u32 hbm_tinit5;		/* [nCK]. Flashbolt: 200ns */
	u32 hbm_tmod;
};

/*
 * struct gaudi2_hbm - holds HBM subsystem settings
 * @ck_freq: HBM device CK_t/c frequency, drived by H6 ASIC
 * @ecc_enable: enable flag for ECC memory
 * @dbi_enable: enable flag for DBI
 * @dfi_phy_update: enable flag PHY-initiated LCDL VT compensation
 * @rmw_fwd_dis: disable flag for RMW forwarding
 * @fast_zq_cal: enable flag for faster impedance calibration settings
 * @phy_chiplet: tracking of current HBM PHY chiplet type (CHANNEL/MASTER/INITENG)
 * @hbm_io_drive: drive strength of HBM IOs
 * @ac_params: HBM/MC/PHY timing parameters
 * @train_cfg: HW training settings
 * @slr_info - an array which holds remapped lanes information per channel (granularity = 1Byte)
 */

struct gaudi2_hbm {
	/* TODO - unite "ck_freq" with struct gaudi2_device member "hbm_pll_freq" */
	enum gaudi2_hbm_freqs ck_freq;
	u8 ecc_enable;
	u8 dbi_enable;
	u8 dfi_phy_update;
	u8 rmw_fwd_dis;
	u8 fast_zq_cal;
	u8 phy_chiplet;
	u8 hbm_io_drive;
	struct hbm_ac_params ac_params;
	struct hbm_hw_training train_cfg;
	struct hbm_ch_lane_remap_info slr_info[HBM_PHY_CHANNELS_NUM];
};

/* Globals */
static const u32 rand_addr = 0xa0460119;
static const u32 rand_array[16][8] = {
{0x00000001, 0x945d65c8, 0xb3952b27, 0x2a3b6fb4, 0x212769f4, 0x4d73a0f5, 0x123b7e92, 0x0812526c},
{0xc2a81dc4, 0x65fa33c4, 0x31d2fd68, 0xe17cf0cb, 0x7d2fbdd7, 0xd59cafad, 0x6e1d7a65, 0x682f1c9c},
{0x26948b0c, 0x24859f18, 0xc239b5c4, 0xd037cd86, 0xed63a14e, 0xb5187472, 0xde1ce526, 0xbd77123f},
{0x24f39176, 0x0eafa33e, 0xbcb92430, 0x3106c365, 0x05e7d47d, 0x6fae10e9, 0x71a7c081, 0x5f776ada},
{0x049283d7, 0xfa52187f, 0x6ad292d8, 0xf3ee0971, 0xe134166e, 0x9fad04bb, 0xaa8bf327, 0x24e6be12},
{0xc389d7ca, 0x49e636bf, 0x456dbf75, 0x3f9f1999, 0x037d32f8, 0xce8f11db, 0xe5b3200f, 0x76148050},
{0xede90479, 0x8e0b2484, 0x80d44930, 0x4f095446, 0x4be2fb00, 0x2547f353, 0xaac3b5c8, 0xbcfbb33b},
{0x206c2288, 0xf412c237, 0x7968fe84, 0x3f004093, 0x9f2e5840, 0xfa92133c, 0x2f5cc7d7, 0x01d7a1b3},
{0xb5901436, 0xa5c98799, 0x39a5689b, 0xb6117d74, 0xdaa7c63d, 0x6b364db5, 0xa67cca32, 0xcf4b03b3},
{0x2146c9bb, 0xb7319acc, 0xd6d3c6b1, 0x80ec2c71, 0x9ff73203, 0x8a6fa50e, 0x753f90dd, 0x24202d01},
{0xf48e1a40, 0xd81b1a83, 0x1c196640, 0xc5f7e5ed, 0xc4f322cb, 0x86ee91a1, 0x86b21962, 0x09a880b5},
{0xd094c483, 0x7fbb1183, 0x6c794d12, 0xd361cf67, 0x4590aa96, 0x0f91cda9, 0x13ac84e8, 0xeec11f1e},
{0xfc07617d, 0xdea09c87, 0x33602535, 0x35572ffe, 0x6ac329f9, 0xd722f93e, 0x4ac150c5, 0xee3c78ab},
{0x7392918c, 0x90c1a27b, 0xa153df01, 0xbb31b8f7, 0xb02f8f0e, 0x1bbe9971, 0x933a9866, 0xcb158c96},
{0x5a9b73be, 0x9f51faae, 0xcccb9765, 0x1373c93a, 0x7d9ad762, 0x3d425cd1, 0xf3419aac, 0xbcfaf53a},
{0x0fd7dca8, 0xa51bcb00, 0xbad7656b, 0xe8445cf8, 0x4f73306d, 0xcff5fef2, 0x443f1452, 0x65892c96}
};

static const u32 ieee_wdr_len_t[] = {
	[ieee1500_bypass] = 1,				/* BYPASS */
	[ieee1500_extest_rx] = 215,			/* EXTEST_RX */
	[ieee1500_extest_tx] = 215,			/* EXTEST_TX */
	[ieee1500_intest_rx] = 0,			/* INTEST_RX */
	[ieee1500_intest_tx] = 0,			/* INTEST_TX */
	[ieee1500_hbm_reset] = 1,			/* HBM_RESET */
	[ieee1500_mbist] = 375,				/* MBIST */
	[ieee1500_soft_repair] = 21,			/* SOFT_REPAIR */
	[ieee1500_hard_repair] = 21,			/* HARD_REPAIR */
	[ieee1500_dword_misr] = 320,			/* DWORD_MISR */
	[ieee1500_aword_misr] = 30,			/* AWORD_MISR */
	[ieee1500_channel_id] = 1,			/* CHANNEL_ID */
	[ieee1500_misr_mask] = 72,			/* MISR_MASK */
	[ieee1500_aword_misr_config] = 8,		/* AWORD_MISR_CONFIG */
	[ieee1500_device_id] = 82,			/* DEVICE_ID */
	[ieee1500_temperature] = 8,			/* TEMPERATURE */
	[ieee1500_mode_register_dump_set] = 128,	/* MODE_REGISTER_DUMP_SET */
	[ieee1500_read_lfsr_compare_sticky] = 175,	/* READ_LFSR_COMPARE_STICKY */
	[ieee1500_soft_lane_repair] = 72,		/* SOFT_LANE_REPAIR */
	[ieee1500_hard_lane_repair] = 72,		/* HARD_LANE_REPAIR */
	[ieee1500_mbist_a6] = 2077,			/* MBIST_A6 */
	[ieee1500_mbist_a7] = 301			/* MBIST_A7 */
};

static inline int enum_to_freq(enum gaudi2_hbm_freqs freq_e)
{
	const int freqs[] = {800, 1200, 1600, 1800};

	return freqs[freq_e];
}

static void phy_set_pstate(struct hl_device *hdev,
				 u32 mc_offset,
				 enum phy_pstate pstate)
{
	RMWREG32(mmHBM0_MC0_PHY_INDIRECT_ACCESS + mc_offset, (u32) pstate, 0x70);
}

static inline void hbm_phy_write(struct hl_device *hdev, u32 dev, u32 addr, u32 val)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	u32 phy_addr, mc1_offset, chiplet_type;
#ifdef DUMP_DYNAMIC
	/* for Synopsis format set shift to 2 */
	u32 shift = 0;
#endif
	mc1_offset = dev * HBM_DEV_OFFSET + MC_OFFSET;
	chiplet_type = (addr & PHY_CHIPLET_MASK) >> PHY_CHIPLET_SHIFT;

	phy_addr = PHY_CFG_BASE + dev * HBM_DEV_OFFSET + (addr & PHY_ADDR_MASK);

	/* set chiplet type only if needed */
	if (chiplet_type != hbm_cfg->phy_chiplet) {
		RMWREG32(mmHBM0_MC0_PHY_INDIRECT_ACCESS + mc1_offset, chiplet_type, 0xf);
		hbm_cfg->phy_chiplet = chiplet_type;
	}
#ifdef DUMP_DYNAMIC
	if (dev == 0)
		hl_err(hdev, "DUMP: phy addr: 0x%05x val: 0x%04x\n",
			(chiplet_type << 16) | ((phy_addr & 0x3ffff) >> shift), val);
#endif
	WREG32(phy_addr, val);
}

static inline u32 hbm_phy_read(struct hl_device *hdev, u32 dev, u32 addr)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	u32 phy_addr, mc1_offset, chiplet_type;

	mc1_offset = dev * HBM_DEV_OFFSET + MC_OFFSET;
	chiplet_type = (addr & PHY_CHIPLET_MASK) >> PHY_CHIPLET_SHIFT;

	phy_addr = PHY_CFG_BASE + dev * HBM_DEV_OFFSET + (addr & PHY_ADDR_MASK);

	/* set chiplet type only if needed*/
	if (chiplet_type != hbm_cfg->phy_chiplet) {
		RMWREG32(mmHBM0_MC0_PHY_INDIRECT_ACCESS + mc1_offset, chiplet_type, 0xf);
		hbm_cfg->phy_chiplet = chiplet_type;
	}

	return RREG32(phy_addr);
}

static inline void hbm_phy_rmw(struct hl_device *hdev, u32 dev, u32 addr, u32 val, u32 mask)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	u32 phy_addr, mc1_offset, chiplet_type;

	mc1_offset = dev * HBM_DEV_OFFSET + MC_OFFSET;
	chiplet_type = (addr & PHY_CHIPLET_MASK) >> PHY_CHIPLET_SHIFT;

	phy_addr = PHY_CFG_BASE + dev * HBM_DEV_OFFSET + (addr & PHY_ADDR_MASK);

	/* set chiplet type only if needed*/
	if (chiplet_type != hbm_cfg->phy_chiplet) {
		RMWREG32(mmHBM0_MC0_PHY_INDIRECT_ACCESS + mc1_offset, chiplet_type, 0xf);
		hbm_cfg->phy_chiplet = chiplet_type;
	}
#ifdef DUMP_DYNAMIC
	if (dev == 0)
		hl_err(hdev, "DUMP: phy addr: 0x%05x val: 0x04%x mask: 0x%x\n",
			(chiplet_type << 16) | ((phy_addr & 0x3ffff) >> 2), val, mask);
#endif
	RMWREG32(phy_addr, val, mask);
}

static void phy_write_all_ch(struct hl_device *hdev, u32 dev, u32 ch0_reg, u32 val)
{
	int ch;

	/* TODO - use broadcast */
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++)
		hbm_phy_write(hdev, dev, ch0_reg + ch * PHY_CH_OFFSET, val);
}

static void debug_print_pub_deviceid(struct hl_device *hdev, u32 dev)
{
	int i;

#ifndef DEBUG
	return;
#endif
	for (i = 0; i < 6; i++) {
		hl_dbg(hdev, "HBM%d DramDeviceId%d: 0x%x\n", dev, i,
			hbm_phy_read(hdev, dev, mmHBM_PHY_MASTER_DRAMDEVICEID0 + i * 4));
	}
}

static void debug_print_mrs(struct hl_device *hdev, u32 dev)
{
	int i;

#ifndef DEBUG
	return;
#endif
	for (i = 0; i < 8; i++) {
		hl_dbg(hdev, "HBM%d MR%d: 0x%x\n", dev, i,
			hbm_phy_read(hdev, dev, mmHBM_PHY_MASTER_MR0 + i * 4));
	}
}

static void debug_print_pub_dramtiming(struct hl_device *hdev, u32 dev)
{
	int i;

#ifndef DEBUG
	return;
#endif
	for (i = 0; i < 9; i++) {
		hl_dbg(hdev, "HBM%d DramTiming%d: 0x%x\n", dev, i,
			hbm_phy_read(hdev, dev, mmHBM_PHY_MASTER_DRAMTIMING0 + i * 4));
	}
}

static void debug_print_training_status(struct hl_device *hdev, u32 dev)
{
	u32 trainStatus, trainCtrl;
	char *str, s[100];
	int size;

#ifndef DEBUG
	return;
#endif
	trainStatus = hbm_phy_read(hdev, dev, mmHBM_PHY_MASTER_TRAINSTATUS) & TRAINSTATUS_MASK;
	trainCtrl = hbm_phy_read(hdev, dev, mmHBM_PHY_MASTER_TRAINCTRL);

	size = 0;
	size += snprintf(s + size, 100 - size, "HBM%d CA training ", dev);
	if (trainCtrl & HBM_PHY_MASTER_TRAINCTRL_TRAINCAEN_MASK) {
		if ((trainStatus & HBM_PHY_MASTER_TRAINSTATUS_TRAINCADONE_MASK) &&
			!(trainStatus & HBM_PHY_MASTER_TRAINSTATUS_TRAINCAERR_MASK))
			str = "PASSED";
		else
			str = "FAILED";
		size += snprintf(s + size, 100 - size, "%s ", str);
		hl_dbg(hdev, "%s\n", s);
	}

	size = 0;
	size += snprintf(s + size, 100 - size, "HBM%d read latency training ", dev);
	if (trainCtrl & HBM_PHY_MASTER_TRAINCTRL_TRAINRLEN_MASK) {
		if ((trainStatus & HBM_PHY_MASTER_TRAINSTATUS_TRAINRLDONE_MASK) &&
			!(trainStatus & HBM_PHY_MASTER_TRAINSTATUS_TRAINRLERR_MASK))
			str = "PASSED";
		else
			str = "FAILED";
		size += snprintf(s + size, 100 - size, "%s ", str);
		hl_dbg(hdev, "%s\n", s);
	}

	size = 0;
	size += snprintf(s + size, 100 - size, "HBM%d read eye training ", dev);
	if (trainCtrl & HBM_PHY_MASTER_TRAINCTRL_TRAINREYEEN_MASK) {
		if ((trainStatus & HBM_PHY_MASTER_TRAINSTATUS_TRAINREYEDONE_MASK) &&
			!(trainStatus & HBM_PHY_MASTER_TRAINSTATUS_TRAINREYEERR_MASK))
			str = "PASSED";
		else
			str = "FAILED";
		size += snprintf(s + size, 100 - size, "%s ", str);
		hl_dbg(hdev, "%s\n", s);
	}

	size = 0;
	size += snprintf(s + size, 100 - size, "HBM%d VREF training ", dev);
	if (trainCtrl & HBM_PHY_MASTER_TRAINCTRL_TRAINVREFEN_MASK) {
		if ((trainStatus & HBM_PHY_MASTER_TRAINSTATUS_TRAINVREFDONE_MASK) &&
			!(trainStatus & HBM_PHY_MASTER_TRAINSTATUS_TRAINVREFERR_MASK))
			str = "PASSED";
		else
			str = "FAILED";
		size += snprintf(s + size, 100 - size, "%s ", str);
		hl_dbg(hdev, "%s\n", s);
	}

	size = 0;
	size += snprintf(s + size, 100 - size, "HBM%d write eye training ", dev);
	if (trainCtrl & HBM_PHY_MASTER_TRAINCTRL_TRAINWEYEEN_MASK) {
		if ((trainStatus & HBM_PHY_MASTER_TRAINSTATUS_TRAINWEYEDONE_MASK) &&
			!(trainStatus & HBM_PHY_MASTER_TRAINSTATUS_TRAINWEYEERR_MASK))
			str = "PASSED";
		else
			str = "FAILED";
		size += snprintf(s + size, 100 - size, "%s ", str);
		hl_dbg(hdev, "%s\n", s);
	}
}

static void debug_print_lcdls_1ui(struct hl_device *hdev, int dev)
{
	u32 ch_offset, offset;
	int ch, dw, size = 0;
	char s[500] = {0};

	if (hdev->pldm || !PRINT_LCDL_1UI)
		return;

	hl_dbg(hdev, "\n%s<0> *** HBM%d LCDLs 1UI ***\n", SPACES40, dev);
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		ch_offset = ch * PHY_CH_OFFSET;
		hl_dbg(hdev, "   CH%d Address/Command: 0x%03x  CK: 0x%03x\n", ch,
			PHY_RREG(mmHBM_PHY_CHAN_CHAN0_AWORD_TXACLCDL1UI + ch_offset),
			PHY_RREG(mmHBM_PHY_CHAN_CHAN0_AWORD_TXCKLCDL1UI + ch_offset));
	}
	hl_dbg(hdev, "\n%s    CH%s%s%sDW-0%s%s%sDW-1%s%s%sDW-2%s%s%sDW-3\n",
		SPACES40, SPACES10, SPACES10, SPACES5, SPACES10, SPACES10, SPACES10,
		SPACES10, SPACES10, SPACES10, SPACES10, SPACES10, SPACES10);

	hl_dbg(hdev,
	"  ----- : ------------------------------------|-------------------------------|-------------------------------|-------------------------------|\n");
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		ch_offset = ch * PHY_CH_OFFSET;
		size = 0;
		size += snprintf(s + size, 500 - size, "      ");
		for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
			offset = ch_offset + dw * DW_OFFSET;
			size += snprintf(s + size, 500 - size,
			 "RDQS_t/c 0x%02x/0x%02x  DQ TX 0x%02x |",
				hbm_phy_read(hdev, dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKLCDL1UI
				+ offset),
				hbm_phy_read(hdev, dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKNLCDL1UI
				+ offset),
				hbm_phy_read(hdev, dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_TXDQLCDL1UI
				+ offset));
		}
		hl_dbg(hdev, "   CH-%d :%s", ch, s);
	}
}

static void phy_clear_errors(struct hl_device *hdev, u32 hbm_dev)
{
	phy_write_all_ch(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_AWORD_DATACTRL, 0x1000);
	phy_write_all_ch(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_AWORD_DATACTRL, 0x0);

	hbm_phy_rmw(hdev, hbm_dev, mmHBM_PHY_MASTER_LANEREPAIRCTRL, 0x1,
		HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRCLRSTATUS_MASK);
	hbm_phy_rmw(hdev, hbm_dev, mmHBM_PHY_MASTER_LANEREPAIRCTRL, 0x0,
		HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRCLRSTATUS_MASK);
}

static int phy_error_aword(struct hl_device *hdev, int dev, int ch)
{
	u32 reg_val, ch_offset = ch * PHY_CH_OFFSET;
	int rc = 0;

	reg_val = PHY_RREG(mmHBM_PHY_CHAN_CHAN0_AWORD_AWDATABITERR0 + ch_offset);
	if (reg_val) {
		hl_dbg(hdev, "CH%d AwDataBitErr0: 0x%x\n", ch, reg_val);
		rc++;
	}

	reg_val = PHY_RREG(mmHBM_PHY_CHAN_CHAN0_AWORD_AWDATABITERR1 + ch_offset);
	/* Skip ARFU[1] & ARFU[3] */
	if ((reg_val != 0x28) && (reg_val != 0)) {
		hl_dbg(hdev, "CH%d AwDataBitErr1: 0x%x\n", ch, reg_val);
		rc++;
	}

	return rc;
}

static int phy_error_dword(struct hl_device *hdev, int dev, int ch, int dw)
{
	u32 ch_offset = ch * PHY_CH_OFFSET, dw_offset = ch_offset + dw * DW_OFFSET, reg_val;
	int byte, rc = 0;

	reg_val = PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_DWDATASTATUS0 + dw_offset);
	if (reg_val & HBM_PHY_CHAN_CHAN0_DWORD0_DWDATASTATUS0_DWDATAERR_MASK) {
		hl_dbg(hdev, "Errors found on CH%d DW%d. DwDataStatus0: 0x%x\n",
			ch, dw, reg_val);
		for (byte = 0; byte < 4; byte++) {
			reg_val = PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_DWDB0DATABITERR +
					   dw_offset + byte * BYTE_OFFSET);
			if (reg_val == 0x0)
				continue;
			hl_dbg(hdev, "\t\tCH%d DW%d byte%d DataBitErr: 0x%x\n",
				ch, dw, byte, reg_val);
		}
		rc = 1;
	}
	return rc;
}

/* return 0 if no errors found */
static int phy_error(struct hl_device *hdev, u32 dev)
{
	int ch, dw, rc = 0;
	u32 reg_val;
	char str[100] = {0};
	int size = 0;

	reg_val =  PHY_RREG(mmHBM_PHY_MASTER_CHNSTATUS);
	if (reg_val) {
		size += snprintf(str + size, 100 - size, "HBM%d PHY errors found on channels: ",
				 dev);
		for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
			if (reg_val & BIT(ch))
				size += snprintf(str + size, 100 - size, "%d ", ch);
		}
		hl_dbg(hdev, "%s", str);
	}

	/* Although we read ChanStatus, we iterate over all channels and look for errors */
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		rc |= phy_error_aword(hdev, dev, ch);

		for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++)
			rc |= phy_error_dword(hdev, dev, ch, dw);

		/* TEMP[2:0] and CATTRRIP */
		reg_val = PHY_RREG(mmHBM_PHY_MASTER_WSOREADSTATUS);
		if ((reg_val & 0xf00) != 0x0 && !hdev->pldm) {
			hl_dbg(hdev, "Errors found on MISDTACK signals:\n");
			hl_dbg(hdev, "HBM%d CH%d WsoReadStatus: 0x%x\n", dev, ch, reg_val);
			rc |= 1;
		}
	}
	phy_clear_errors(hdev, dev);

	return rc;
}

static void phy_pubmode(struct hl_device *hdev, u32 hbm_dev, enum pubmode state)
{
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_PUBMODE, state);
}

#if SLR_ENABLE
/* returns 1 in case there was lane repair in the system */
static int phy_print_remap_info(struct hl_device *hdev, u32 dev,
				enum lanerepair_err type)
{
	u32 reg_val, ch_offset, dw_offset;
	int ch, dw, rc = 0;

	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		ch_offset = ch * PHY_CH_OFFSET;
		/* AWORD */
		reg_val = hbm_phy_read(hdev, dev, mmHBM_PHY_CHAN_CHAN0_AWORD_AWREMAP + ch_offset);
		if (reg_val != 0xff) {
			hl_dbg(hdev, "CH%d AWORD remapping: 0x%x\n", ch, reg_val);
			rc = 1;
		}
		/* DWORD */
		for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
			dw_offset = ch_offset + dw * DW_OFFSET;
			reg_val = hbm_phy_read(hdev, dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_DWREMAP +
					       dw_offset);
			if (reg_val != 0xffff) {
				hl_dbg(hdev, "CH%d dw%d DwRemap: 0x%x\n", ch, dw, reg_val);
				rc = 1;
			}
		}
	}

	return rc;
}
#endif

static int phy_test_loopback(struct hl_device *hdev, u32 dev)
{
	u32 timeout = (hdev->pldm) ? PLDM_TIMEOUT : PHY_TESTS_TIMEOUT;
	u32 val, bist_status_valid;
	int rc;

	/* TODO - loopback is unstable and produce false failures */
	return status_pass;

	/* Fix density on PUB DeviceId CSR - 16Gb per channel - TODO remove? */
	if (hdev->pldm)
		hbm_phy_write(hdev, dev, mmHBM_PHY_MASTER_DRAMDEVICEID4, 0xA100);

	bist_status_valid = (hdev->pldm) ? 0x13 : 0x7f;

	/* loads the EXTEST_RX command through the P1500 in order to tristate the DRAM outputs */
	hbm_phy_rmw(hdev, dev, mmHBM_PHY_MASTER_MASTERCTRL, 0x1,
		HBM_PHY_MASTER_MASTERCTRL_TRISTATEDRAMEN_MASK);
	msleep(100);
	hbm_phy_rmw(hdev, dev, mmHBM_PHY_MASTER_MASTERCTRL, 0x0,
		HBM_PHY_MASTER_MASTERCTRL_TRISTATEDRAMEN_MASK);

	/* BistTrainNumWords = 12; BistMode = concurrent AW/DW */
	val = 0x3006;
	if (!hdev->pldm) {
		/* AWORD and DWORD read latency and eye training */
		val |= FIELD_PREP(HBM_PHY_MASTER_BISTCTRL0_BISTAWTRAINRDLATEN_MASK, 0x1);
		val |= FIELD_PREP(HBM_PHY_MASTER_BISTCTRL0_BISTAWTRAINEYEEN_MASK, 0x1);
		val |= FIELD_PREP(HBM_PHY_MASTER_BISTCTRL0_BISTDWTRAINEYEEN_MASK, 0x1);
		val |= FIELD_PREP(HBM_PHY_MASTER_BISTCTRL0_BISTDWTRAINRDLATEN_MASK, 0x1);
	}
	/* Trigger BIST run */
	val |= HBM_PHY_MASTER_BISTCTRL0_BISTINSTR_MASK;
	hbm_phy_write(hdev, dev, mmHBM_PHY_MASTER_BISTCTRL0, val);

	/* Reset BIST trigger */
	val ^= HBM_PHY_MASTER_BISTCTRL0_BISTINSTR_MASK;
	hbm_phy_write(hdev, dev, mmHBM_PHY_MASTER_BISTCTRL0, val);

	rc = phy_poll_timeout(
		hdev,
		dev,
		mmHBM_PHY_MASTER_BISTSTATUS,
		val,
		(val & HBM_PHY_MASTER_BISTSTATUS_BISTDONE_MASK),
		100,
		timeout);
	if (rc) {
		hl_err(hdev, "Timeout while waiting for PHY loopback BIST to complete\n");
		return status_fail;
	}

	if (val != bist_status_valid) {
		hl_err(hdev, "HBM%d PHY loopback BIST failed. BistStatus: 0x%x\n",
			dev, val);
		phy_error(hdev, dev);
		return status_fail;
	}

	/* TODO - consider un-comment this code after 3.2 is stable */
//	if (phy_error(hdev, dev)) {
//		hl_err(hdev, "Loopback passed but mismatches have been FOUND\n");
//		return status_fail;
//	}

	phy_clear_errors(hdev, dev);
	hl_dbg(hdev, "HBM%d PHY internal loopback PASSED. BistStatus: 0x%x\n", dev, val);
	return status_pass;
}

static int phy_trigger_lanerepair_fsm(struct hl_device *hdev, u32 dev, u32 ctrl,
					u32 expected_status)
{
	u32 timeout = (hdev->pldm) ? PLDM_TIMEOUT : PHY_TESTS_TIMEOUT, reg_val;
	int rc;

	hbm_phy_write(hdev, dev, mmHBM_PHY_MASTER_LANEREPAIRCTRL, ctrl);
	ctrl ^= HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRRUN_MASK;
	hbm_phy_write(hdev, dev, mmHBM_PHY_MASTER_LANEREPAIRCTRL, ctrl);

	rc = phy_poll_timeout(
		hdev,
		dev,
		mmHBM_PHY_MASTER_LANEREPAIRSTATUS,
		reg_val,
		(reg_val & HBM_PHY_MASTER_LANEREPAIRSTATUS_LANEREPAIRDONE_MASK),
		100,
		timeout);
	if (rc) {
		hl_err(hdev, "Timeout while waiting for PHY IO-DC to complete\n");
		return status_fail;
	}

	if (reg_val != expected_status) {
		hl_err(hdev, "HBM%d LANEREPAIR FSM failed. LaneRepairCtrl = 0x%x LaneRepairStatus = 0x%x\n",
			dev, expected_status, reg_val);
		phy_error(hdev, dev);
		return status_fail;
	}

	if (phy_error(hdev, dev)) {
		hl_err(hdev, "LANEREPAIR FSM operation passed but mismatches have been FOUND. LaneRepairStatus = 0x%x\n",
			reg_val);
		return status_fail;
	}

	return status_pass;
}

static int phy_test_io_dc(struct hl_device *hdev, u32 dev)
{
	u32 lanerepair_ctrl, lanerepair_status;
	int rc;

	/* read HARD repair info from the HBM */
	lanerepair_ctrl =  HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRRUN_MASK	|
		HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRREADEN_MASK		|
		HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRREMAPEN_MASK		|
		HBM_PHY_MASTER_LANEREPAIRCTRL_DRAMPOWERUPRSTEN_MASK;
	lanerepair_status = HBM_PHY_MASTER_LANEREPAIRSTATUS_LANEREPAIRDONE_MASK	|
		HBM_PHY_MASTER_LANEREPAIRSTATUS_LANEREPAIRREADDONE_MASK		|
		HBM_PHY_MASTER_LANEREPAIRSTATUS_LANEREPAIRREMAPDONE_MASK;
	rc = phy_trigger_lanerepair_fsm(hdev, dev, lanerepair_ctrl, lanerepair_status);
	if (rc) {
		hl_err(hdev, "HBM%d PHY IO-DC test FAILED\n", dev);
		return rc;
	}

	/* EXTEST */
	lanerepair_ctrl = HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRRUN_MASK	|
		HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIREXTESTEN_MASK		|
		HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRREMAPEN_MASK;

	lanerepair_status = HBM_PHY_MASTER_LANEREPAIRSTATUS_LANEREPAIRDONE_MASK |
		HBM_PHY_MASTER_LANEREPAIRSTATUS_LANEREPAIREXTESTDONE_MASK	|
		HBM_PHY_MASTER_LANEREPAIRSTATUS_LANEREPAIRREMAPDONE_MASK;

	/* PLDM HBM model doesn't support Tx on Rx-only IOs (nor Rx on Tx-only) */
	if (!hdev->pldm)
		lanerepair_ctrl |= HBM_PHY_MASTER_LANEREPAIRCTRL_EXTESTRXDIFFOUTEN_MASK	|
			HBM_PHY_MASTER_LANEREPAIRCTRL_EXTESTTXDIFFINEN_MASK		|
			HBM_PHY_MASTER_LANEREPAIRCTRL_EXTESTRXOUTEN_MASK		|
			HBM_PHY_MASTER_LANEREPAIRCTRL_EXTESTTXINEN_MASK;

	if (SLR_ENABLE) {
		lanerepair_ctrl |= HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRSOFTEN_MASK;
		lanerepair_status |= HBM_PHY_MASTER_LANEREPAIRSTATUS_LANEREPAIRSOFTDONE_MASK;
	}

	rc = phy_trigger_lanerepair_fsm(hdev, dev, lanerepair_ctrl, lanerepair_status);
	if (rc) {
		hl_err(hdev, "HBM%d PHY IO-DC test FAILED\n", dev);
		return rc;
	}

	hl_dbg(hdev, "HBM%d PHY IO-DC test PASSED\n", dev);
	return rc;
}

static void phy_test_io_ac(struct hl_device *hdev, u32 dev)
{
	u32 timeout = PHY_TESTS_TIMEOUT, reg_val, reg_offset;
	int rc, ch, dw;

	/* PLDM HBM model doesn't support 34bit MISR */
	if (hdev->pldm) {
		hl_dbg(hdev,
			"Skipping PHY IO-AC test - PLDM HBM model doesn't support 34bit MISR\n");
		return;
	}

	/* Twick A/C RL and DFI-MASTER RL (TODO) */
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		reg_offset = ch * PHY_CH_OFFSET;
		hbm_phy_write(hdev, dev, mmHBM_PHY_CHAN_CHAN0_AWORD_ACRDLAT + reg_offset,
		0x2);
		for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
			reg_offset =  ch * PHY_CH_OFFSET + dw * DW_OFFSET;
			hbm_phy_write(hdev, dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_DFIMRL_P0 +
				      reg_offset, 0xd);
		}
	}

	/* Disable soft repair */
	hbm_phy_write(hdev, dev, mmHBM_PHY_MASTER_LANEREPAIRCTRL, 0x1110);
	hbm_phy_write(hdev, dev, mmHBM_PHY_MASTER_LANEREPAIRCTRL, 0x111);
	hbm_phy_write(hdev, dev, mmHBM_PHY_MASTER_LANEREPAIRCTRL, 0x110);

	rc = phy_poll_timeout(
		hdev,
		dev,
		mmHBM_PHY_MASTER_LANEREPAIRSTATUS,
		reg_val,
		(reg_val & HBM_PHY_MASTER_LANEREPAIRSTATUS_LANEREPAIRDONE_MASK),
		100,
		timeout);
	if (rc) {
		hl_err(hdev,
			"Timeout while waiting for PHY MISR to complete. LaneRepairStatus: 0x%x\n",
			reg_val);
		return;
	}

	if (reg_val & HBM_PHY_MASTER_LANEREPAIRSTATUS_LANEREPAIRMISRTESTERR_MASK) {
		hl_dbg(hdev, "HBM%d PHY IO-AC test FAILED. LaneRepairStatus: 0x%x\n",
			dev, reg_val);
		phy_error(hdev, dev);
		return;
	}

	if (phy_error(hdev, dev)) {
		hl_err(hdev, "HBM%d PHY IO-AC test passed but mismatches have been FOUND. LaneRepairStatus = 0x%x\n",
			dev, reg_val);
		return;
	}

	hl_dbg(hdev, "HBM%d PHY IO-AC test PASSED. LaneRepairStatus = 0x%x\n", dev, reg_val);
}

static void phy_config_zq_cal(struct hl_device *hdev, u32 hbm_dev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;

	if (hbm_cfg->fast_zq_cal) {
		/* Set LinearSearchStep and CalNumVotes*/
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_CALMISC, 0x8);
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_CALMISC2, 0x2);

		/* PHY drive strength  = 12mA */
		hbm_phy_rmw(hdev, hbm_dev, mmHBM_PHY_MASTER_CALSCALECTRL, 0xf,
			HBM_PHY_MASTER_CALSCALECTRL_CALSCALENUM_MASK);
		hbm_phy_rmw(hdev, hbm_dev, mmHBM_PHY_MASTER_CALSCALECTRL, 0x7,
			HBM_PHY_MASTER_CALSCALECTRL_CALSCALEDEN_MASK);
		hbm_phy_rmw(hdev, hbm_dev, mmHBM_PHY_MASTER_CALSCALECTRL, 0x10,
			HBM_PHY_MASTER_CALSCALECTRL_CALSCALEBASE_MASK);
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_CALINITVALS, 0x1428);
	}

	/* CalCmpStartupTime = 1us
	 * CalSampleTime = 200ns
	 * CalOffsetSampleTime = 150ns
	 */
	switch (hbm_cfg->ck_freq) {
	case HBM_PLL_1200:
		/* Default */
		break;

	case HBM_PLL_1600:
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_CALCMPSTARTUPTIME_P0, 1603);
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_CALSAMPLETIME_P0, 321);
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_CALOFFSETSAMPLETIME_P0, 241);
		break;

	case HBM_PLL_1800:
	default:
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_CALCMPSTARTUPTIME_P0, 1812);
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_CALSAMPLETIME_P0, 363);
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_CALOFFSETSAMPLETIME_P0, 272);
		break;
	}
}

static void phy_config_lcdl_cal(struct hl_device *hdev, u32 hbm_dev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	struct hbm_ac_params *ac_params = &hbm_cfg->ac_params;
	u32 reg_val;

	phy_write_all_ch(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_AWORD_LCDLGAINCTL_P0, 0x62);

	switch (hbm_cfg->ck_freq) {
	case HBM_PLL_1200:
		reg_val = 0x22c;
		break;
	case HBM_PLL_1600:
		reg_val = 0x1a0;
		break;
	case HBM_PLL_1800:
	default:
		reg_val = 0x172;
		break;
	}

	phy_write_all_ch(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_AWORD_LCDLLOCKPARAM_P0, reg_val);

	phy_write_all_ch(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_AWORD_LCDLTRAINPARAM,
			ac_params->extend_phased_time);
}

static void phy_config_mode_registers(struct hl_device *hdev, u32 hbm_dev)
{
	const u64 mr3_tras_mask = 0x3f, mr4_parity_lat_mask = 0xc, mr4_ext_wl_mask = 0x10,
		mr4_ext_rl_mask = 0x20, mr4_ecc_mask = 0x3;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	struct hbm_ac_params *ac_params = &hbm_cfg->ac_params;
	bool ext_wl = ac_params->wr_lat > 8, ext_rl = ac_params->rd_lat > 33;
	u32 reg_val, mc1_offset = hbm_dev * HBM_DEV_OFFSET + MC_OFFSET;
	bool ext_rl_enable, ext_wl_enable;
	int pstate;

	/* PHY mode registers */
	/* wr_dbi, rd_dbi, TCSR, dq_rd_par, dq_wr_par, ca_par - ALL ENABLED */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_MR0, 0x77);

	/* Drive strength & write recovery */
	reg_val = 0x0;
	reg_val |= FIELD_PREP(0xe0, hbm_cfg->hbm_io_drive);
	reg_val |= FIELD_PREP(0x1f, ac_params->twr);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_MR1, reg_val);

	/* WL & RL */
	reg_val = 0x0;
	reg_val |= FIELD_PREP(0x7, ext_wl ? ac_params->wr_lat - 9 : ac_params->wr_lat - 1);
	reg_val |= FIELD_PREP(0xf8, ext_rl ? ac_params->rd_lat - 34 : ac_params->rd_lat - 2);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_MR2, reg_val);

	/* tRAS, BG enable, BL 4 */
	reg_val = 0xc0;
	reg_val |= FIELD_PREP(mr3_tras_mask, ac_params->mc_tras * HDR_TO_SDR);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_MR3, reg_val);

	/* ECC cfg, extended RL/WL bits, parity latency */
	reg_val = 0x0;
	reg_val |= FIELD_PREP(mr4_ecc_mask, hbm_cfg->ecc_enable ? 0x3 : 0x0);
	reg_val |= FIELD_PREP(mr4_ext_wl_mask, ext_wl ? 1 : 0);
	reg_val |= FIELD_PREP(mr4_ext_rl_mask, ext_rl ? 1 : 0);
	reg_val |= FIELD_PREP(mr4_parity_lat_mask, ac_params->parity_lat);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_MR4, reg_val);

	/* DramParam0/1 */
	for (pstate = PSTATE_0; pstate <= PSTATE_3; pstate++) {
		phy_set_pstate(hdev, mc1_offset, pstate);
		reg_val = 0x1;		/* BL = 4 */
		reg_val |= FIELD_PREP(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM0_P0_READLATENCY_MASK,
				      ext_rl ? ac_params->rd_lat - 34 : ac_params->rd_lat - 2);
		reg_val |= FIELD_PREP(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM0_P0_WRITELATENCY_MASK,
				      ext_wl ? ac_params->wr_lat - 9 : ac_params->wr_lat - 1);
		reg_val |= FIELD_PREP(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM0_P0_PARITYLATENCY_MASK,
				  ac_params->parity_lat);
		phy_write_all_ch(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM0_P0, reg_val);

		reg_val = 0x01c;	/* Enable rd/wr/cmd parity */
		ext_rl_enable = !!ext_rl;
		reg_val |= FIELD_PREP(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_EXTREADLATENCY_MASK,
					  ext_rl_enable);
		ext_wl_enable = !!ext_wl;
		reg_val |= FIELD_PREP(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_EXTWRITELATENCY_MASK,
					  ext_wl_enable);
		reg_val |= FIELD_PREP(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_READDBI_MASK,
				  hbm_cfg->dbi_enable);
		reg_val |= FIELD_PREP(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_WRITEDBI_MASK,
						  hbm_cfg->dbi_enable);
		reg_val |= FIELD_PREP(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_ECC_MASK,
						  hbm_cfg->ecc_enable);
		reg_val |= FIELD_PREP(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_WRITEDATAMASK_MASK,
						  0x1);
		phy_write_all_ch(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0, reg_val);
	}
	phy_set_pstate(hdev, mc1_offset, PSTATE_0);
}

static void phy_config_internal_pll(struct hl_device *hdev, u32 hbm_dev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	u32 reg_val;

	/* TODO - add option for pll at 800MHZ, for M_BIST */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_PLLCTRL4_P0, 0xd8);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_PLLCTRL0, 0x86);

	reg_val = (hbm_cfg->ck_freq == HBM_PLL_1200) ? 0x225 : 0xa25;
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_PLLTESTMODE_P0, reg_val);

	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_PLLCTRL2_P0, 0x18);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_PLLCTRL1_P0, 0x21);
}

static void phy_config_dram_timing(struct hl_device *hdev, u32 hbm_dev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	struct hbm_ac_params *ac_params = &hbm_cfg->ac_params;
	u32 reg_val;

	/* For CK freq <= 1800MHz */
	reg_val = (hdev->pldm) ? 0x28 : 0x57e4;
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_DRAMTIMING0, reg_val);
	reg_val = (hdev->pldm) ? 0x50 : 0xdbba;
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_DRAMTIMING1, reg_val);

	reg_val = 0x1000;	/* Flashbolt tMRD = 8nCK for all CK values*/
	reg_val |= FIELD_PREP(HBM_PHY_MASTER_DRAMTIMING2_TINIT5_MASK, ac_params->hbm_tinit5);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_DRAMTIMING2, reg_val);

	reg_val = 0x0;
	reg_val |= FIELD_PREP(HBM_PHY_MASTER_DRAMTIMING3_TMOD_MASK, ac_params->hbm_tmod);
	reg_val |= FIELD_PREP(HBM_PHY_MASTER_DRAMTIMING3_TXS_MASK,
			      (ac_params->mc_txs + 1) * HDR_TO_SDR);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_DRAMTIMING3, reg_val);

	reg_val = 0x0;
	reg_val |= FIELD_PREP(HBM_PHY_MASTER_DRAMTIMING4_TWTR_MASK,
			      ac_params->hbm_twtr_l);
	reg_val |= FIELD_PREP(HBM_PHY_MASTER_DRAMTIMING4_TCKSRE_MASK,
				(ac_params->mc_tcksre + 1) * HDR_TO_SDR);
	/* tRCD is the only MC timing parameter given in SDR. No need to convert */
	reg_val |= FIELD_PREP(HBM_PHY_MASTER_DRAMTIMING4_TRCD_MASK, ac_params->mc_trcd_rd);


	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_DRAMTIMING4, reg_val);

	reg_val = 0x0;
	reg_val |= FIELD_PREP(0x1f, ac_params->mc_trp * HDR_TO_SDR);
	reg_val |= FIELD_PREP(0xffe0, ac_params->t_pwreset);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_DRAMTIMING5, reg_val);

	/* tCCDS, tCCDL, tCCDR - from Flashbolt spec. Frequency independent */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_DRAMTIMING8, 0x72);
}

static void phy_preload_initeng(struct hl_device *hdev, u32 hbm_dev)
{

	u32 mc1_offset = hbm_dev * HBM_DEV_OFFSET + MC_OFFSET;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	u32 instr_dly[PSTATE_NUM][3];
	int pstate;

	/* Program Disable flags for initeng groups */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQDISABLEFLAG0, 0xfff0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQDISABLEFLAG1, 0xffff);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQDISABLEFLAG2, 0xfff0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQDISABLEFLAG3, 0xffef);

	/* Program DFI Frequency mappings */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQDFIFREQXLAT0, 0x3210);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQDFIFREQXLAT7, 0x4000); //0x3210

	/* Program INITENG instructions delays */
	switch (hbm_cfg->ck_freq) {
	case HBM_PLL_1200:
		instr_dly[PSTATE_0][0] = 0xbb5;
		instr_dly[PSTATE_0][1] = 0x12b;
		instr_dly[PSTATE_0][2] = 0x95;
		instr_dly[PSTATE_1][0] = 0x971;
		instr_dly[PSTATE_1][1] = 0xf1;
		instr_dly[PSTATE_1][2] = 0x78;
		instr_dly[PSTATE_2][0] = 0x7e9;
		instr_dly[PSTATE_2][1] = 0xca;
		instr_dly[PSTATE_2][2] = 0x65;
		instr_dly[PSTATE_3][0] = 0x6cf;
		instr_dly[PSTATE_3][1] = 0xae;
		instr_dly[PSTATE_3][2] = 0x57;
		break;
	case HBM_PLL_1600:
		instr_dly[PSTATE_0][0] = 0xf99;
		instr_dly[PSTATE_0][1] = 0x18f;
		instr_dly[PSTATE_0][2] = 0xc7;
		instr_dly[PSTATE_1][0] = 0x7d0;
		instr_dly[PSTATE_1][1] = 0xc8;
		instr_dly[PSTATE_1][2] = 0x64;
		instr_dly[PSTATE_2][0] = 0x4e2;
		instr_dly[PSTATE_2][1] = 0x7d;
		instr_dly[PSTATE_2][2] = 0x3e;
		instr_dly[PSTATE_3][0] = 0x271;
		instr_dly[PSTATE_3][1] = 0x3e;
		instr_dly[PSTATE_3][2] = 0x1f;
		break;
	case HBM_PLL_1800:
	default:
		instr_dly[PSTATE_0][0] = 0x1190;
		instr_dly[PSTATE_0][1] = 0x1c1;
		instr_dly[PSTATE_0][2] = 0xe0;
		instr_dly[PSTATE_1][0] = 0xcea;
		instr_dly[PSTATE_1][1] = 0x14a;
		instr_dly[PSTATE_1][2] = 0xa5;
		instr_dly[PSTATE_2][0] = 0xa37;
		instr_dly[PSTATE_2][1] = 0x105;
		instr_dly[PSTATE_2][2] = 0x82;
		instr_dly[PSTATE_3][0] = 0x872;
		instr_dly[PSTATE_3][1] = 0xd8;
		instr_dly[PSTATE_3][2] = 0x6c;
		break;
	}

	for (pstate = PSTATE_0; pstate <= PSTATE_3; pstate++) {
		phy_set_pstate(hdev, mc1_offset, pstate);
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQDLY0_P0,
		instr_dly[pstate][0]);
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQDLY1_P0,
		instr_dly[pstate][1]);
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQDLY2_P0,
		instr_dly[pstate][2]);
	}
	phy_set_pstate(hdev, mc1_offset, PSTATE_0);

	/* Program Pre sequencer instructions */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQPRE0S2, 0x400);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQPRE1S2, 0x400);
}

static void phy_load_initeng(struct hl_device *hdev, u32 hbm_dev)
{
	u32 mc1_offset = hbm_dev * HBM_DEV_OFFSET + MC_OFFSET;

	/* Part 1 - LP2 Entry Sequence */

	/* Set     [0]=>CalRun   = 1'b1, Start the calibration engine */
	/* Writing to Address CalRun */
	/* INSTR = OP=WRITE, GRP=0, FLAG=CONT, ADDR=CalRun,               DATA=1,      DELAY_S=0 */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG0S0, 0x0008);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG0S1, 0x0268);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG0S2, 0x8410);

	//Set     [0]=>CalRun   = 1'b0, Clear CalRun
	//Writing to Address CalRun
	//INSTR = OP=WRITE, GRP=0, FLAG=CONT, ADDR=CalRun,               DATA=0,      DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG1S0, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG1S1, 0x0268);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG1S2, 0x8410);

	//Set   [7:0]=>TxRxPtrInit = 8'bff, stopping the fifo pointers
	//Writing to Address TxRdPtrInit
	//INSTR = OP=WRITE, GRP=1, FLAG=CONT, ADDR=TxRdPtrInit,          DATA=0xff,   DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG2S0, 0x07f8);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG2S1, 0x0368);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG2S2, 0x8c10);

	//Set     [3]=>PllOutBypEn = 1'b1, Put pll in bypass
	//Writing to Address PllCtrl0
	//INSTR = OP=WRITE, GRP=1, FLAG=CONT, ADDR=PllCtrl0,             DATA=0x0086, DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG3S0, 0x0430);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG3S1, 0x02d0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG3S2, 0x8c10);

	//Set   [7:0]=>DfiInitComplete = 80x0 for all channels
	//Writing to Address DfiInitComplete
	//INSTR = OP=WRITE, GRP=1, FLAG=CONT, ADDR=DfiInitComplete,      DATA=0,      DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG4S0, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG4S1, 0x0220);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG4S2, 0x8c10);

	//Part 2 - LP3(S3)  Entry Sequence

	//Waiting for 64 cycles before LP3 entry
	//INSTR = OP=NOP,   GRP=3, FLAG=CONT,                                         DELAY_L=16
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG5S0, 0x0010);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG5S1, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG5S2, 0x1c00);

	//Set   [7:0]=>TxRxPtrInit       = 8'bff, stopping the fifo pointers
	//Writing to Address TxRdPtrInit
	//INSTR = OP=WRITE, GRP=3, FLAG=CONT, ADDR=TxRdPtrInit,          DATA=0xff,   DELAY_S=7
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG6S0, 0x07ff);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG6S1, 0x0368);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG6S2, 0x9c10);

	//Set   [5]=>PllBypClkSel  = 1'b1   [3]=>PllOutBypEn       = 1'b1,  Put pll in bypass
	//Writing to Address PllCtrl0
	//INSTR = OP=WRITE, GRP=3, FLAG=CONT, ADDR=PllCtrl0,             DATA=0x0096, DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG7S0, 0x04b0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG7S1, 0x02d0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG7S2, 0x9c10);

	//Reset   [0]=>PllPwrDn          = 1'b1,  Pll in Power Down
	//Writing to Address PllPwrDn
	//INSTR = OP=WRITE, GRP=3, FLAG=CONT, ADDR=PllPwrDn,             DATA=1,      DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG8S0, 0x0008);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG8S1, 0x0338);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG8S2, 0x9c10);

	//Set   [7:0]=>DfiInitComplete   = 80x0,  for all channels
	//Writing to Address DfiInitComplete
	//INSTR = OP=WRITE, GRP=3, FLAG=CONT, ADDR=DfiInitComplete,      DATA=0,      DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG9S0, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG9S1, 0x0220);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG9S2, 0x9c10);

	//Part 3 - Wait instruction for Detecting Start De-assertion of LP2 and LP3 exit

	//Adding Wait instruction which will wait here for the Start to be de-asserted
	//INSTR = OP=WAIT,  GRP=0, FLAG=CONT,                                         DELAY_L=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG10S0, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG10S1, 0x0001);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG10S2, 0x0400);

	//Part 4 - LP3(S3) Exit  Sequence

	//Setting [7:0]=>DfiInitComplete = 80xff for all channels
	//Writing to Address DfiInitComplete
	//INSTR = OP=WRITE, GRP=3, FLAG=CONT, ADDR=DfiInitComplete,      DATA=0xff,   DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG11S0, 0x07f8);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG11S1, 0x0220);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG11S2, 0x9c10);

	//Part 5 - LP2 Exit Sequence + Dfi Initialization

	//Ensuring Pstate bits get written with the dfi_frequency value
	//Writing to Address PState
	//INSTR = OP=WRVAL, GRP=2, FLAG=CONT, ADDR=PState,               REGSEL=0,      DELAY_S=3
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG12S0, 0x0003);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG12S1, 0x006a);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG12S2, 0x1410);

	//Set   [2:0]=>InitSeqWaitCondSel = 3'b4,  Configure to wait for Impedance Calibration Done
	//Writing to Address InitSeqWaitCondSel
	//INSTR = OP=WRITE, GRP=0, FLAG=CONT, ADDR=InitSeqWaitCondSel,   DATA=4,        DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG13S0, 0x0020);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG13S1, 0x0788);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG13S2, 0x8448);

	//Waiting for 1us before de-asserting PwrDn or Standby
	//INSTR = OP=NOP_R, GRP=0, FLAG=CONT,                            REGSEL=1
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG14S0, 0x0008);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG14S1, 0x0004);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG14S2, 0x0400);

	//Reset   [0]=>PllPwrDn    = 1'b0, Pll out of power down
	//Writing to Address PllPwrDn
	//INSTR = OP=WRITE, GRP=0, FLAG=CONT, ADDR=PllPwrDn,             DATA=0,        DELAY_S=7
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG15S0, 0x0007);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG15S1, 0x0338);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG15S2, 0x8410);

	//Reset   [0]=>PllStandby  = 1'b0,  Pll out of standby
	//Writing to Address PllCtrl0
	//INSTR = OP=WRITE, GRP=1, FLAG=CONT, ADDR=PllCtrl0,             DATA=0x0086,   DELAY_S=7
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG16S0, 0x0437);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG16S1, 0x02d0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG16S2, 0x8c10);

	//Set     [5]=>GearShit = 1'b1 and [3]=>Preset = 1'b1
	//Writing to Address PllCtrl0
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=PllCtrl0,             DATA=0x00AE,   DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG17S0, 0x0570);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG17S1, 0x02d0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG17S2, 0x9410);

	//Waiting for 1us before de-asserting Preset
	//INSTR = OP=NOP_R, GRP=2, FLAG=CONT,                            REGSEL=1
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG18S0, 0x0008);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG18S1, 0x0004);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG18S2, 0x1400);

	if (hdev->pldm)
		RREG32(mmHBM0_MC0_PHY_INDIRECT_ACCESS + mc1_offset);

	//Reset   [3]=>Preset = 1'b0
	//Writing to Address PllCtrl0
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=PllCtrl0,             DATA=0x00A6,   DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG19S0, 0x0530);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG19S1, 0x02d0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG19S2, 0x9410);

	//Waiting for 0.5us before de-asserting Gearshift
	//INSTR = OP=NOP_R, GRP=2, FLAG=CONT,                            REGSEL=2
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG20S0, 0x0010);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG20S1, 0x0004);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG20S2, 0x1400);

	//Reset  [5]=>Gearshift = 1'b0
	//Writing to Address PllCtrl0
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=PllCtrl0,             DATA=0x0086,   DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG21S0, 0x0430);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG21S1, 0x02d0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG21S2, 0x9410);

	//Waiting for 10us for the PLL to lock
	//INSTR = OP=NOP_R, GRP=0, FLAG=CONT,                            REGSEL=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG22S0, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG22S1, 0x0004);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG22S2, 0x0400);

	//Waiting for 0.5us for the Pll to lock
	//INSTR = OP=NOP_R, GRP=1, FLAG=CONT,                            REGSEL=2
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG23S0, 0x0010);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG23S1, 0x0004);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG23S2, 0x0c00);

	//Reset   [2]=>PllBypMode = 1'b1, Pull pll out of bypass
	//Writing to Address PllCtrl0
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=PllCtrl0, BYPDAT=0x0096, DATA=0x0082, DELAY_S=0
	if (hdev->pldm)
		/* PLL bypass */
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG24S0, 0x04b0);
	else
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG24S0, 0x0410);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG24S1, 0x02d0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG24S2, 0x9410);

	//Adding Wait instruction which will wait here for the ZQ Calibration to be done
	//INSTR = OP=WAIT,  GRP=0, FLAG=CONT,                                           DELAY_L=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG25S0, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG25S1, 0x0001);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG25S2, 0x0400);

	//Set   [2:0]=>InitSeqWaitCondSel = 3'b7,  Configure to wait for LCDL Calibration Done
	//Writing to Address InitSeqWaitCondSel
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=InitSeqWaitCondSel,   DATA=7,        DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG26S0, 0x0038);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG26S1, 0x0788);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG26S2, 0x9448);

	//Set     [0]=>PClkEnAsyncCtrl = 1'b1
	//Writing to Address ClockingCtrl
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=ClockingCtrl,         DATA=1,        DELAY_S=3
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG27S0, 0x000b);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG27S1, 0x0040);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG27S2, 0x9410);

	//Set     Set [0]=>LcdlResetRelock = 1'b1
	//Writing to Address LcdlControl
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=LcdlControl,          DATA=1,        DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG28S0, 0x0008);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG28S1, 0x0208);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG28S2, 0x9410);

	//Set     [1]=>LcdlTrackEnCtrl = 1'b1
	//Writing to Address ClockingCtrl
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=ClockingCtrl,         DATA=3,        DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG29S0, 0x0018);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG29S1, 0x0040);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG29S2, 0x9410);

	//Reset   Set [0]=>LcdlResetRelock = 1'b0
	//Writing to Address LcdlControl
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=LcdlControl,          DATA=0,        DELAY_S=7
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG30S0, 0x0007);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG30S1, 0x0208);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG30S2, 0x9410);

	if (hdev->pldm)
		RREG32(mmHBM0_MC0_PHY_INDIRECT_ACCESS + mc1_offset);

	//Adding Wait instruction which will wait here for the LCDL Calibration to be done
	//INSTR = OP=WAIT,  GRP=2, FLAG=CONT,                                           DELAY_L=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG31S0, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG31S1, 0x0001);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG31S2, 0x1400);

	//Reset   [1]=>LcdlTrackEnCtrl = 1'b0
	//Writing to Address ClockingCtrl
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=ClockingCtrl,         DATA=1,        DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG32S0, 0x0008);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG32S1, 0x0040);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG32S2, 0x9410);

	//Reset   [0]=>PclkEnAsyncCtrl = 1'b0
	//Writing to Address ClockingCtrl
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=ClockingCtrl,         DATA=0,        DELAY_S=3
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG33S0, 0x0003);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG33S1, 0x0040);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG33S2, 0x9410);

	//Waiting for 24 cycles to allow Calibration to complete final cycle
	//INSTR = OP=NOP,   GRP=2, FLAG=CONT,                                           DELAY_L=6
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG34S0, 0x0006);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG34S1, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG34S2, 0x1400);

	//Set   [7:0]=>TxRxPtrInit = 8'b0 , starting the fifo pointers
	//Writing to Address TxRdPtrInit
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=TxRdPtrInit,          DATA=0,        DELAY_S=7
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG35S0, 0x0007);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG35S1, 0x0368);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG35S2, 0x9410);

	//Waiting for 68 cycles (for Rx I/O power-up) before triggering reset rxdat fifos
	//INSTR = OP=NOP,   GRP=2, FLAG=CONT,                                           DELAY_L=17
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG36S0, 0x0011);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG36S1, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG36S2, 0x1400);

	//Set [0]=>PLLockDone = 1'b1
	//Writing to Address PorControl
	//INSTR = OP=WRITE, GRP=0, FLAG=CONT, ADDR=PorControl,           DATA=0x0001,   DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG37S0, 0x0008);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG37S1, 0x0360);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG37S2, 0x8410);

	//Write CmdSeqReg0 Instruction cmd=FIFO_RST, addr=DWORD DATA RX
	//Writing to Address CmdSeqReg0
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=CmdSeqReg0,           DATA=0x00B6,   DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG38S0, 0x05b0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG38S1, 0x0070);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG38S2, 0x9410);

	if (hdev->pldm)
		RREG32(mmHBM0_MC0_PHY_INDIRECT_ACCESS + mc1_offset);

	//Set CmdSeqRun=1, CmdSeqStartAddr=0, CmdSeqStopAddr=0
	//Writing to Address CmdSeqCtrl0
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=CmdSeqCtrl0,          DATA=0x0001,   DELAY_S=7
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG39S0, 0x000f);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG39S1, 0x00f0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG39S2, 0x9410);

	//Set CmdSeqRun=0, CmdSeqStartAddr=0, CmdSeqStopAddr=0
	//Writing to Address CmdSeqCtrl0
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=CmdSeqCtrl0,          DATA=0x0000,   DELAY_S=7
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG40S0, 0x0007);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG40S1, 0x00f0);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG40S2, 0x9410);

	//Waiting for 68 cycles to allow CmdSeq time to complete
	//INSTR = OP=NOP,   GRP=2, FLAG=CONT,                                           DELAY_L=17
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG41S0, 0x0011);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG41S1, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG41S2, 0x1400);

	//Set     [1]=>LcdlTrackEnCtrl = 1'b1
	//Writing to Address ClockingCtrl
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=ClockingCtrl,         DATA=2,        DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG42S0, 0x0010);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG42S1, 0x0040);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG42S2, 0x9410);

	if (hdev->pldm)
		usleep_range(100, 1000);

	//Set   [7:0]=>DfiInitComplete = 80xff for all channels
	//Writing to Address DfiInitComplete
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=DfiInitComplete,      DATA=0xff,     DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG43S0, 0x07f8);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG43S1, 0x0220);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG43S2, 0x9410);

	RREG32(mmHBM0_MC0_PHY_INDIRECT_ACCESS + mc1_offset);

	//Part 6 - Enabling LP2 sequence

	//Set   [2:0]=>InitSeqWaitCondSel = 3'b2, Configure to wait for Init Start = 0
	//Writing to Address InitSeqWaitCondSel
	//INSTR = OP=WRITE, GRP=2, FLAG=CONT, ADDR=InitSeqWaitCondSel,   DATA=2,        DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG44S0, 0x0010);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG44S1, 0x0788);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG44S2, 0x9448);

	//Set  [15:0]=>InitSeqDisableFlag = 160xfff0
	//Writing to Address InitSeqDisableFlag1
	//INSTR = OP=WRITE, GRP=0, FLAG=CONT, ADDR=InitSeqDisableFlag1,  DATA=0xfff0,   DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG45S0, 0xff80);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG45S1, 0x06ff);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG45S2, 0x8448);

	//Set  [15:0]=>InitSeqDisableFlag = 160xffff
	//Writing to Address InitSeqDisableFlag0
	//INSTR = OP=WRITE, GRP=0, FLAG=CONT, ADDR=InitSeqDisableFlag0,  DATA=0xffff,   DELAY_S=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG46S0, 0xfff8);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG46S1, 0x06f7);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG46S2, 0x8448);

	//Terminating by setting the Terminate Flag to 0
	//INSTR = OP=NOP,   GRP=0, FLAG=TERM,                                           DELAY_L=0
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG47S0, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG47S1, 0x0000);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_INITENG_INITSEQREG47S2, 0x0000);
}

static void phy_init_config(struct hl_device *hdev, u32 hbm_dev)
{
	u32 mc1_offset = hbm_dev * HBM_DEV_OFFSET + MC_OFFSET;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;

	/* Set initial chiplet and pstate */
	hbm_cfg->phy_chiplet = 0xf;
	phy_set_pstate(hdev, mc1_offset, PSTATE_0);

	phy_config_mode_registers(hdev, hbm_dev);

	phy_config_dram_timing(hdev, hbm_dev);

	phy_config_zq_cal(hdev, hbm_dev);

	/* HDR mode + AWORD_MISR_CONFIG + DeviceIdReadOnRst */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_MASTERCTRL, 0x1804);

	if (hdev->pldm)
		RREG32(mmHBM0_MC0_PHY_INDIRECT_ACCESS + mc1_offset);

	phy_config_internal_pll(hdev, hbm_dev);

	/* Disable PHY updates */
	phy_write_all_ch(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_AWORD_DFIPHYUPD, 0x0);

	phy_config_lcdl_cal(hdev, hbm_dev);

	phy_preload_initeng(hdev, hbm_dev);

	phy_load_initeng(hdev, hbm_dev);

	debug_print_mrs(hdev, hbm_dev);
	debug_print_pub_dramtiming(hdev, hbm_dev);
}

static int poll_on_phy_init(struct hl_device *hdev, u32 dev, u32 mc_idx)
{
	u64 timeout = (hdev->pldm) ? PLDM_TIMEOUT : PHY_INIT_TIMEOUT_USEC;
	u32 mc_offset = dev * HBM_DEV_OFFSET + mc_idx * MC_OFFSET;
	u32 sleep = (hdev->pldm) ? 10000 : 100;
	u32 reg_val = 0;
	int rc = 0;

	rc = hl_poll_timeout(
		hdev,
		mc_offset + mmHBM0_MC0_DFI_PHY_STS_0,
		reg_val,
		((reg_val & HBM0_MC0_DFI_PHY_STS_INIT_COMPLETE_MASK) == 0x1),
		sleep,
		timeout);

	rc |= hl_poll_timeout(
		hdev,
		mc_offset + mmHBM0_MC0_DFI_PHY_STS_1,
		reg_val,
		((reg_val & HBM0_MC0_DFI_PHY_STS_INIT_COMPLETE_MASK) == 0x1),
		sleep,
		timeout);

	rc |= hl_poll_timeout(
		hdev,
		mc_offset + mmHBM0_MC0_DFI_PHY_STS_2,
		reg_val,
		((reg_val & HBM0_MC0_DFI_PHY_STS_INIT_COMPLETE_MASK) == 0x1),
		sleep,
		timeout);

	rc |= hl_poll_timeout(
		hdev,
		mc_offset + mmHBM0_MC0_DFI_PHY_STS_3,
		reg_val,
		((reg_val & HBM0_MC0_DFI_PHY_STS_INIT_COMPLETE_MASK) == 0x1),
		sleep,
		timeout);

	if (rc) {
		hl_err(hdev,
			"Timeout while polling on HBM%d MC%d dfi_init_complete assertion\n",
			dev, mc_idx);
		return status_fail;
	}

	return status_pass;
}

/* assert/deassert dfi_init_start towards 4 MC channels
 * PHY will not complete init unless all 8 channels asserted dfi_init_start
 * Therefore, both MCs must assert dfi_init_start at the same time
 */
static void phy_init(struct hl_device *hdev, u32 dev, u32 mc_idx, bool assert)
{
	u32 reg_val, mc_offset = dev * HBM_DEV_OFFSET + mc_idx * MC_OFFSET;

	reg_val = (assert) ? 0x1 : 0x0;
	RMWREG32(mc_offset + mmHBM0_MC0_DFI_PHY_CFG, reg_val,
		HBM0_MC0_DFI_PHY_CFG_DFI_CR2DFI_INIT_START_MASK);
}

static void print_training_vref(struct hl_device *hdev, int hbm_dev,
				struct vref_training *vref_res)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	struct hbm_hw_training *training_cfg = &hbm_cfg->train_cfg;

	/* Ignore PHY VREF training results for other training types */
	if (training_cfg->training_type != train_type_hw_only)
		return;

	hl_dbg(hdev, "<5> VREF training\n");
	hl_dbg(hdev, "VREF-Min = 0x%04x \t VREF-Max = 0x%04x\n",
		vref_res->min, vref_res->max);
}

/* returns the percentage of the eye relative to 1UI */
static inline int calc_eye_ratio_ui(u16 min_reg, u16 max_reg)
{
	if ((!min_reg) && (!max_reg))
		return 0;

	if (min_reg & BIT(6))
		return ((32 - (min_reg & 0x1f) + max_reg + 1) * 100) / 32;
	/* else */
	return ((max_reg - min_reg + 1) * 100) / 32;
}

static void print_training_dw(struct hl_device *hdev, int hbm_dev,
				struct dw_training *dw_res)

{
	u32 ratio = 0, dw_res_offset, ratio_sum = 0, ratio_min = 100;
	int ch, pc, dw, size = 0;
	char s[500] = {0};

#ifndef DEBUG
	return;
#endif

	hl_dbg(hdev, "\n%s<2> *** HBM%d DWORDs DFIM read latency ***\n", SPACES40, hbm_dev);
	hl_dbg(hdev, "   CH%sDW-0  DW-1  DW-2  DW-3\n", SPACES10);
	hl_dbg(hdev, "  ----- : -------------------------------------\n");
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		size = 0;
		size += snprintf(s + size, 500 - size, "      ");
		for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
			size += snprintf(s + size, 500 - size, "0x%02x  ",
					 dw_res[ch * HBM_CHANNEL_DWORDS_NUM + dw].rx.read_latency);
		}
		hl_dbg(hdev, "   CH-%d :%s", ch, s);
	}

	hl_dbg(hdev, "\n%s<3> *** HBM%d DWORDs RDQS rising edge delay ***\n",
		SPACES40, hbm_dev);
	hl_dbg(hdev, "  %s%sDW-0%s%s%sDW-1%s%s%sDW-2%s%s%sDW-3\n",
		SPACES10, SPACES10, SPACES10, SPACES10, SPACES10, SPACES10, SPACES10, SPACES10,
		SPACES10, SPACES10, SPACES10);
	hl_dbg(hdev,
		" ----- : --------------------------------------------------------------------------------------------------------------------------------\n");
	hl_dbg(hdev,
		"  CH   :  result (ratio)  Min  -  Max%s result (ratio)  Min  -  Max%s result (ratio)  Min  -  Max%s result (ratio)  Min  -  Max\n",
		SPACES5, SPACES5, SPACES5);
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		size = 0;
		size += snprintf(s + size, 500 - size, "  CH-%d :", ch);
		for (pc = 0;  pc < 2; pc++) {
			dw_res_offset = ch * HBM_CHANNEL_DWORDS_NUM + pc * 2;
			/* 1st DWORD */
			ratio = dw_res[dw_res_offset].rx.rdqs_rise.ratio_1ui;
			ratio_sum += ratio;
			ratio_min = ratio < ratio_min ? ratio : ratio_min;
			size += snprintf(s + size, 500 - size,
					 "   0x%02x   (%d%%)   0x%02x - 0x%02x    ",
					 dw_res[dw_res_offset].rx.rdqs_rise.final,
					 ratio,
					 dw_res[dw_res_offset].rx.rdqs_rise.min,
					 dw_res[dw_res_offset].rx.rdqs_rise.max);
			/* 2nd DWORD */
			dw_res_offset++;
			ratio = dw_res[dw_res_offset].rx.rdqs_rise.ratio_1ui;
			ratio_sum += ratio;
			ratio_min = ratio < ratio_min ? ratio : ratio_min;
			size += snprintf(s + size, 500 - size,
					 "   0x%02x   (%d%%)   0x%02x - 0x%02x    ",
					 dw_res[dw_res_offset].rx.rdqs_rise.final,
					 ratio,
					 dw_res[dw_res_offset].rx.rdqs_rise.min,
					 dw_res[dw_res_offset].rx.rdqs_rise.max);
		}
		hl_dbg(hdev, "%s", s);
	}

	hl_dbg(hdev, "\n%s<3> *** HBM%d DWORDs RDQS falling edge delay ***\n",
		SPACES40, hbm_dev);
	hl_dbg(hdev, "  %s%sDW-0%s%s%sDW-1%s%s%sDW-2%s%s%sDW-3\n",
		SPACES10, SPACES10, SPACES10, SPACES10, SPACES10, SPACES10, SPACES10, SPACES10,
		SPACES10, SPACES10, SPACES10);
	hl_dbg(hdev,
		 " ----- : --------------------------------------------------------------------------------------------------------------------------------\n");
	hl_dbg(hdev,
		"  CH   :  result (ratio)  Min  -  Max%s result (ratio)  Min  -  Max%s result (ratio)  Min  -  Max%s result (ratio)  Min  -  Max\n",
		SPACES5, SPACES5, SPACES5);
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		size = 0;
		size += snprintf(s + size, 500 - size, "  CH-%d :", ch);
		for (pc = 0;  pc < 2; pc++) {
			dw_res_offset = ch * HBM_CHANNEL_DWORDS_NUM + pc * 2;
			/* 1st DWORD */
			ratio = dw_res[dw_res_offset].rx.rdqs_fall.ratio_1ui;
			ratio_sum += ratio;
			ratio_min = ratio < ratio_min ? ratio : ratio_min;
			size += snprintf(s + size, 500 - size,
					 "   0x%02x   (%d%%)   0x%02x - 0x%02x    ",
					 dw_res[dw_res_offset].rx.rdqs_fall.final,
					 ratio,
					 dw_res[dw_res_offset].rx.rdqs_fall.min,
					 dw_res[dw_res_offset].rx.rdqs_fall.max);
			/* 2nd DWORD */
			dw_res_offset++;
			ratio = dw_res[dw_res_offset].rx.rdqs_fall.ratio_1ui;
			ratio_sum += ratio;
			ratio_min = ratio < ratio_min ? ratio : ratio_min;
			size += snprintf(s + size, 500 - size,
					 "   0x%02x   (%d%%)   0x%02x - 0x%02x    ",
					 dw_res[dw_res_offset].rx.rdqs_fall.final,
					 ratio,
					 dw_res[dw_res_offset].rx.rdqs_fall.min,
					 dw_res[dw_res_offset].rx.rdqs_fall.max);
		}
		hl_dbg(hdev, "%s", s);
	}

	hl_dbg(hdev,
		 "\n%s%s%s%sFor all DWORDs - RD eye avg ratio: %d%% RD eye min ratio: %d%%\n\n",
		SPACES10, SPACES10, SPACES10, SPACES5, ratio_sum /
		(RDQS_RISING_FALLING_EDGES_FACTOR * 32), ratio_min);

	ratio_sum = 0;
	ratio_min = 100;

	hl_dbg(hdev, "\n%s<4> *** HBM%d DWORDs DQ transmit delay ***\n",
	SPACES40, hbm_dev);
	hl_dbg(hdev, "  %s%sDW-0%s%s%sDW-1%s%s%sDW-2%s%s%sDW-3\n",
	SPACES10, SPACES10, SPACES10, SPACES10, SPACES10, SPACES10, SPACES10, SPACES10, SPACES10,
	SPACES10, SPACES10);
	hl_dbg(hdev,
	 " ----- : --------------------------------------------------------------------------------------------------------------------------------\n");
	hl_dbg(hdev, "  CH   :  result (ratio)  Min  -  Max%s result (ratio)  Min  -  Max%s result (ratio)  Min  -  Max%s result (ratio)  Min  -  Max\n",
	SPACES5, SPACES5, SPACES5);
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		size = 0;
		size += snprintf(s + size, 500 - size, "  CH-%d :", ch);
		for (pc = 0;  pc < 2; pc++) {
			dw_res_offset = ch * HBM_CHANNEL_DWORDS_NUM + pc * 2;
			/* 1st DWORD */
			ratio = dw_res[dw_res_offset].tx.dq.ratio_1ui;
			ratio_sum += ratio;
			ratio_min = ratio < ratio_min ? ratio : ratio_min;
			size += snprintf(s + size, 500 - size,
					 "   0x%02x   (%d%%)   0x%02x - 0x%02x    ",
					 dw_res[dw_res_offset].tx.dq.final,
					 ratio,
					 dw_res[dw_res_offset].tx.dq.min,
					 dw_res[dw_res_offset].tx.dq.max);
			/* 2nd DWORD */
			dw_res_offset++;
			ratio = dw_res[dw_res_offset].tx.dq.ratio_1ui;
			ratio_sum += ratio;
			ratio_min = ratio < ratio_min ? ratio : ratio_min;
			size += snprintf(s + size, 500 - size,
					 "   0x%02x   (%d%%)   0x%02x - 0x%02x    ",
					 dw_res[dw_res_offset].tx.dq.final,
					 ratio,
					 dw_res[dw_res_offset].tx.dq.min,
					 dw_res[dw_res_offset].tx.dq.max);
		}
		hl_dbg(hdev, "%s", s);
	}

	hl_dbg(hdev,
	 "\n%s%s%s%sFor all DWORDs - WR eye avg ratio: %d%% WR eye min ratio: %d%%\n\n",
	SPACES10, SPACES10, SPACES10, SPACES5, ratio_sum / 32, ratio_min);
}

static void print_training_ca(struct hl_device *hdev, int dev,
				  struct lcdl *ca_res,
				  u32 *ck_res)
{
	int ch, ratio = 0, ratio_sum = 0, ratio_min = 100;

#ifndef DEBUG
	return;
#endif
	hl_dbg(hdev, "\n%s<1> *** HBM%d Command-Address transmit delay ***\n", SPACES40, dev);
	hl_dbg(hdev, "  CH   :   result (ratio)%sMin  -  Max\n", SPACES5);
	hl_dbg(hdev, " ----- : -------------------------------------\n");
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		ratio = ca_res[ch].ratio_1ui;
		ratio_sum += ratio;
		ratio_min = ratio < ratio_min ? ratio : ratio_min;
		hl_dbg(hdev, "  CH-%d :    0x%02x   (%02d%%)     0x%02x - 0x%02x\n",
			ch,
			ca_res[ch].final,
			ratio,
			ca_res[ch].min,
			ca_res[ch].max);
	}
	hl_dbg(hdev,
	"\n%s%s%s%sFor all Channels - CA eye avg ratio: %d%% CA eye min ratio: %d%%\n\n",
	SPACES10, SPACES10, SPACES10, SPACES5, ratio_sum / 8, ratio_min);
}

static int phy_get_training_results(struct hl_device *hdev, int dev)
{
	u32 *ck_res = NULL, ch_offset, dw_offset, dw_res_offset;
	int ch, dw, rc = status_pass, fail_cntr = 0;
	struct vref_training *vref_res = NULL;
	struct dw_training *dw_res = NULL;
	struct lcdl *ca_res = NULL;

	if (hdev->pldm)
		return status_pass;

	ca_res = kmalloc_array(HBM_PHY_CHANNELS_NUM, sizeof(struct lcdl),
			       GFP_KERNEL);
	ck_res = kmalloc_array(HBM_PHY_CHANNELS_NUM, sizeof(u32),
			       GFP_KERNEL);
	dw_res = kmalloc_array(HBM_PHY_CHANNELS_NUM * HBM_CHANNEL_DWORDS_NUM,
			       sizeof(struct dw_training), GFP_KERNEL);
	vref_res = kmalloc_array(HBM_PHY_CHANNELS_NUM, sizeof(u32),
			       GFP_KERNEL);
	if (dw_res == NULL || ca_res == NULL  || ck_res == NULL || vref_res == NULL) {
		hl_err(hdev, "Failed to allocate kernel memory for HW training results\n");
		rc = status_fail;
		goto free_mem;
	}

	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		ch_offset = ch * PHY_CH_OFFSET;

		/* Command Address */
		ca_res[ch].min = PHY_RREG(mmHBM_PHY_CHAN_CHAN0_AWORD_TXACMINDLY + ch_offset);
		ca_res[ch].max = PHY_RREG(mmHBM_PHY_CHAN_CHAN0_AWORD_TXACMAXDLY + ch_offset);
		ca_res[ch].final = PHY_RREG(mmHBM_PHY_CHAN_CHAN0_AWORD_TXACDLY_P0 + ch_offset);

		ca_res[ch].ratio_1ui = calc_eye_ratio_ui(ca_res[ch].min, ca_res[ch].max);
		if (ca_res[ch].ratio_1ui < TRAINING_EYE_TH)
			fail_cntr++;

		/* CK */
		ck_res[ch] = PHY_RREG(mmHBM_PHY_CHAN_CHAN0_AWORD_TXCKDLY_P0 +  ch_offset);

		for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
			dw_offset = ch_offset + dw * DW_OFFSET;
			dw_res_offset = ch * HBM_CHANNEL_DWORDS_NUM + dw;

			/* Read latency */
			dw_res[dw_res_offset].rx.read_latency =
				PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_DFIMRL_P0 + dw_offset);

			/* Write eye */
			dw_res[dw_res_offset].tx.dq.min =
				PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_TXDQMINDLY + dw_offset);
			dw_res[dw_res_offset].tx.dq.max =
				PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_TXDQMAXDLY + dw_offset);
			dw_res[dw_res_offset].tx.dq.final =
				PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_TXDQDLY_P0 + dw_offset);
			dw_res[dw_res_offset].tx.wdqs.final =
				PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_TXDQSDLY_P0 + dw_offset);
			dw_res[dw_res_offset].tx.dq.ratio_1ui = calc_eye_ratio_ui(
							dw_res[dw_res_offset].tx.dq.min,
							dw_res[dw_res_offset].tx.dq.max);
			if (dw_res[dw_res_offset].tx.dq.ratio_1ui < TRAINING_EYE_TH)
				fail_cntr++;

			/* Read eye */
			dw_res[dw_res_offset].rx.rdqs_rise.min =
				PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKMINDLY + dw_offset);
			dw_res[dw_res_offset].rx.rdqs_rise.max =
				PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKMAXDLY + dw_offset);
			dw_res[dw_res_offset].rx.rdqs_rise.final =
				PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKDLY_P0 + dw_offset);
			dw_res[dw_res_offset].rx.rdqs_fall.min =
				PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKNMINDLY + dw_offset);
			dw_res[dw_res_offset].rx.rdqs_fall.max =
				PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKNMAXDLY + dw_offset);
			dw_res[dw_res_offset].rx.rdqs_fall.final =
				PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKNDLY_P0 + dw_offset);

			dw_res[dw_res_offset].rx.rdqs_rise.ratio_1ui = calc_eye_ratio_ui(
							dw_res[dw_res_offset].rx.rdqs_rise.min,
							dw_res[dw_res_offset].rx.rdqs_rise.max);
			if (dw_res[dw_res_offset].rx.rdqs_rise.ratio_1ui < TRAINING_EYE_TH)
				fail_cntr++;

			dw_res[dw_res_offset].rx.rdqs_fall.ratio_1ui = calc_eye_ratio_ui(
							dw_res[dw_res_offset].rx.rdqs_fall.min,
							dw_res[dw_res_offset].rx.rdqs_fall.max);

			if (dw_res[dw_res_offset].rx.rdqs_fall.ratio_1ui < TRAINING_EYE_TH)
				fail_cntr++;
		}
	}

	if (fail_cntr)
		hl_err(hdev, "HBM%d - %d violations of eye width (<%d%%)\n",
			dev, fail_cntr, TRAINING_EYE_TH);

	/* VREF */
	vref_res->min = hbm_phy_read(hdev, dev,
			mmHBM_PHY_MASTER_VREFINDACSTATUS) & 0x7f;
	vref_res->max = (hbm_phy_read(hdev, dev,
			mmHBM_PHY_MASTER_VREFINDACSTATUS) & 0x3f80) >>
			HBM_PHY_MASTER_VREFINDACSTATUS_GLOBALVREFINDACMAX_SHIFT;

	hl_dbg(hdev, "MiscStatus: 0x%x\n", hbm_phy_read(hdev, dev,
			mmHBM_PHY_MASTER_MISCSTATUS));
	hl_dbg(hdev, "ChnStatus: 0x%x\n", hbm_phy_read(hdev, dev,
			mmHBM_PHY_MASTER_CHNSTATUS));

	debug_print_lcdls_1ui(hdev, dev);
	print_training_ca(hdev, dev, ca_res, ck_res);
	print_training_dw(hdev, dev, dw_res);
	print_training_vref(hdev, dev, vref_res);

free_mem:
	kfree(ca_res);
	kfree(ck_res);
	kfree(dw_res);
	kfree(vref_res);

	return rc;
}

static int phy_poll_on_training(struct hl_device *hdev, u32 hbm_dev)
{
	u64 timeout = (hdev->pldm) ? PLDM_TIMEOUT : TRAINING_COMPLETE_TIMEOUT_USEC;
	int rc = status_pass;
	u32 reg_val = 0;

	rc = phy_poll_timeout(
		hdev,
		hbm_dev,
		mmHBM_PHY_MASTER_TRAINSTATUS,
		reg_val,
		(reg_val & HBM_PHY_MASTER_TRAINSTATUS_TRAINDONE_MASK),
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

static int hbm_mrs(struct hl_device *hdev, int hbm_dev)
{
	u32 reg_val, train_fsm_status_valid;

	reg_val = HBM_PHY_MASTER_TRAINCTRL_TRAINRUN_MASK |
		HBM_PHY_MASTER_TRAINCTRL_DRAMINITMRS_MASK;
	train_fsm_status_valid = HBM_PHY_MASTER_TRAINSTATUS_TRAINDONE_MASK |
		HBM_PHY_MASTER_TRAINSTATUS_DRAMINITMRSDONE_MASK;

	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_TRAINCTRL, reg_val);

	/* de-assert TrainRun for future training and/or DRAM init */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_TRAINCTRL, reg_val & ~(1ULL));

	if (phy_poll_on_training(hdev, hbm_dev))
		return status_fail;

	reg_val = hbm_phy_read(hdev, hbm_dev, mmHBM_PHY_MASTER_TRAINSTATUS) & TRAINSTATUS_MASK;
	if (reg_val != train_fsm_status_valid) {
		hl_err(hdev, "HBM%d - FAILED to set mode registers. TrainStatus: 0x%x\n",
			hbm_dev, reg_val);
		return status_fail;
	}

	return status_pass;
}

static int hbm_reset(struct hl_device *hdev, int hbm_dev)
{
	u32 reg_val, train_fsm_status_valid;

	reg_val = HBM_PHY_MASTER_TRAINCTRL_TRAINRUN_MASK |
		HBM_PHY_MASTER_TRAINCTRL_DRAMINITRESET_MASK;
	train_fsm_status_valid = HBM_PHY_MASTER_TRAINSTATUS_TRAINDONE_MASK |
		HBM_PHY_MASTER_TRAINSTATUS_DRAMINITRSTDONE_MASK;

	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_TRAINCTRL, reg_val);

	/* de-assert TrainRun for future training and/or DRAM init */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_TRAINCTRL, reg_val & ~(1ULL));

	if (phy_poll_on_training(hdev, hbm_dev))
		return status_fail;

	reg_val = hbm_phy_read(hdev, hbm_dev, mmHBM_PHY_MASTER_TRAINSTATUS) & TRAINSTATUS_MASK;
	if (reg_val != train_fsm_status_valid) {
		hl_err(hdev, "HBM%d - FAILED to reset HBM device. TrainStatus: 0x%x\n",
			hbm_dev, reg_val);
		return status_fail;
	}

	return status_pass;
}

static int phy_hw_training(struct hl_device *hdev, int hbm_dev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	struct hbm_hw_training *training_cfg = &hbm_cfg->train_cfg;
	u32 reg_val = 0, train_status_valid;
	int rc;

	train_status_valid = 1 << HBM_PHY_MASTER_TRAINSTATUS_TRAINDONE_SHIFT |
		(training_cfg->train_ca << HBM_PHY_MASTER_TRAINSTATUS_TRAINCADONE_SHIFT) |
		(training_cfg->train_rl << HBM_PHY_MASTER_TRAINSTATUS_TRAINRLDONE_SHIFT) |
		(training_cfg->train_rdeye << HBM_PHY_MASTER_TRAINSTATUS_TRAINREYEDONE_SHIFT) |
		(training_cfg->train_vref << HBM_PHY_MASTER_TRAINSTATUS_TRAINVREFDONE_SHIFT) |
		(training_cfg->train_wreye << HBM_PHY_MASTER_TRAINSTATUS_TRAINWEYEDONE_SHIFT) |
		(training_cfg->hbm_reset << HBM_PHY_MASTER_TRAINSTATUS_DRAMINITRSTDONE_SHIFT) |
		(training_cfg->mrs << HBM_PHY_MASTER_TRAINSTATUS_DRAMINITMRSDONE_SHIFT);

	reg_val = training_cfg->training_type == train_type_semi_auto ? 1 : 0;
	hbm_phy_rmw(hdev, hbm_dev, mmHBM_PHY_MASTER_TRAINCFG, reg_val,
		    HBM_PHY_MASTER_TRAINCFG_RDLATTRAINMODE_MASK);

	reg_val = 1 << HBM_PHY_MASTER_TRAINCTRL_TRAINRUN_SHIFT |
		(training_cfg->train_ca << HBM_PHY_MASTER_TRAINCTRL_TRAINCAEN_SHIFT) |
		(training_cfg->train_rl << HBM_PHY_MASTER_TRAINCTRL_TRAINRLEN_SHIFT) |
		(training_cfg->train_rdeye << HBM_PHY_MASTER_TRAINCTRL_TRAINREYEEN_SHIFT) |
		(training_cfg->train_vref << HBM_PHY_MASTER_TRAINCTRL_TRAINVREFEN_SHIFT) |
		(training_cfg->train_wreye << HBM_PHY_MASTER_TRAINCTRL_TRAINWEYEEN_SHIFT);

	/* trigger H/W training */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_TRAINCTRL, reg_val);

	/* Reduce debug prints */
	if (training_cfg->training_type == train_type_hw_only) {
		hl_dbg(hdev, "TrainCtrl: 0x%x | TrainCfg: 0x%x\n",
			hbm_phy_read(hdev, hbm_dev, mmHBM_PHY_MASTER_TRAINCTRL),
			hbm_phy_read(hdev, hbm_dev, mmHBM_PHY_MASTER_TRAINCFG));
	}

	/* de-assert TrainRun for future training and/or DRAM init */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_TRAINCTRL, reg_val & (~1));

	rc = phy_poll_on_training(hdev, hbm_dev);
	if (rc)
		goto exit;

	reg_val = hbm_phy_read(hdev, hbm_dev, mmHBM_PHY_MASTER_TRAINSTATUS) & TRAINSTATUS_MASK;
	if (reg_val != train_status_valid) {
		rc = status_fail;
		if (training_cfg->training_type == train_type_hw_only)
			hl_err(hdev, "HBM%d Training DID NOT COMPLETE AS EXPECTED\n",
				hbm_dev);
		if (rc)
			goto exit;
	}

	/* Reduce debug prints */
	if (training_cfg->training_type == train_type_hw_only) {
		debug_print_lcdls_1ui(hdev, hbm_dev);
		debug_print_training_status(hdev, hbm_dev);
		rc = phy_get_training_results(hdev, hbm_dev);
	};
exit:
	return rc;
}

/* Config WRCK (Max freq =  50MHz) */
static void phy_config_p1500(struct hl_device *hdev,
			     u32 hbm_dev,
			     enum gaudi2_hbm_freqs ck_freq)
{

	if (ck_freq == HBM_PLL_1800)
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_P1500CTRL, 0x6);
	else
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_P1500CTRL, 0x5);
}

static void phy_reset(struct hl_device *hdev, u32 dev)
{
	/* In each HBM cluster only MC1 is connected to the Midstack interfaces
	 * of the PHY (IEEE1500, CATTRIP, RESETs, DFT)
	 */
	u32 mc1_offset = dev * HBM_DEV_OFFSET + MC_OFFSET;

	/* Cold reset */
	WREG32(mmHBM0_MC0_PHY_POWERGOOD + mc1_offset, 0x0);
	/* Warm reset */
	WREG32(mmHBM0_MC0_PHY_HBMPHY_RESET + mc1_offset, 0x1);
	/* APB */
	WREG32(mmHBM0_MC0_PHY_PRESETN + mc1_offset, 0x0);
	/* DFI */
	WREG32(mmHBM0_MC0_PHY_DFI_RESET_N + mc1_offset, 0x0);

	WREG32(mmHBM0_MC0_PHY_POWERGOOD + mc1_offset, 0x1);
	/* 64 DfiClks TODO - optimize */
	usleep_range(100, 1000);

	WREG32(mmHBM0_MC0_PHY_HBMPHY_RESET + mc1_offset, 0x0);
	WREG32(mmHBM0_MC0_PHY_PRESETN + mc1_offset, 0x1);
	WREG32(mmHBM0_MC0_PHY_DFI_RESET_N + mc1_offset, 0x3);

	/* PHY access rights = secured */
	WREG32(mmHBM0_MC0_PHY_PPROT_PIN + mc1_offset, 0x0);

	if (hdev->pldm)
		RREG32(mmHBM0_MC0_PHY_PPROT_PIN + mc1_offset);
}

static void mc_axis_init(struct hl_device *hdev, u32 dev, u32 is_master)
{
	u32 mc_offset = dev * HBM_DEV_OFFSET + is_master * MC_OFFSET;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	int mc_ch;

	WREG32(mmHBM0_MC0_AXI_GLBL_CFG + mc_offset, hbm_cfg->rmw_fwd_dis);

	for (mc_ch = 0; mc_ch < MC_CHANNELS_NUM; mc_ch++) {
		WREG32(mmHBM0_MC0_AXIS_RMW_CRED_0 + mc_offset + mc_ch * 4,
		      0x9090);
	}
	WREG32(mmHBM0_MC0_ECC_CFG + mc_offset, hbm_cfg->ecc_enable);
}

static void mc_scheduler_init(struct hl_device *hdev, u32 dev, u32 is_master)
{
	u32 mc_offset = dev * HBM_DEV_OFFSET + is_master * MC_OFFSET;
	int mc_ch;

	WREG32(mmHBM0_MC0_RSCH_REF_ARB_TH_VAL + mc_offset, 0xffff);
	WREG32(mmHBM0_MC0_RSCH_RD_SAT_MED_RPSB + mc_offset, 0x1);
	WREG32(mmHBM0_MC0_RSCH_RD_SAT_HIGH_RPSB + mc_offset, 0x3);
	WREG32(mmHBM0_MC0_RSCH_RD_MIN_SATURATED + mc_offset, 0xa);
	WREG32(mmHBM0_MC0_RSCH_WR_SAT_MED_RPSB + mc_offset, 0x1);
	WREG32(mmHBM0_MC0_RSCH_WR_SAT_HIGH_RPSB + mc_offset, 0x3);
	WREG32(mmHBM0_MC0_RSCH_WR_MIN_SATURATED + mc_offset, 0x14);

	WREG32(mmHBM0_MC0_RSCH_RD_LATENCY_LEVEL_0 + mc_offset, 0x40);
	WREG32(mmHBM0_MC0_RSCH_RD_LATENCY_LEVEL_1 + mc_offset, 0x80);
	WREG32(mmHBM0_MC0_RSCH_RD_LATENCY_LEVEL_2 + mc_offset, 0xfa);
	WREG32(mmHBM0_MC0_RSCH_RD_LATENCY_LEVEL_3 + mc_offset, 0x190);

	for (mc_ch = 0; mc_ch < MC_CHANNELS_NUM; mc_ch++) {
		WREG32(mmHBM0_MC0_RSCH_RD_QOS_ALMOST_FULL_TH_0 + mc_offset +
		       mc_ch * 4, 0x1e);
	}
	WREG32(mmHBM0_MC0_RSCH_WR_QOS_ALMOST_FULL_TH + mc_offset, 0x1e);
	WREG32(mmHBM0_MC0_RSCH_RDB_CREDIT_INIT + mc_offset, 0x20);

	WREG32(mmHBM0_MC0_RSCH_RD_SAT_MED_SCORE + mc_offset, 0x0);
	WREG32(mmHBM0_MC0_RSCH_RD_SAT_HIGH_SCORE + mc_offset, 0x0);
	WREG32(mmHBM0_MC0_RSCH_WR_SAT_MED_SCORE + mc_offset, 0x0);
	WREG32(mmHBM0_MC0_RSCH_WR_SAT_HIGH_SCORE + mc_offset, 0x0);

	WREG32(mmHBM0_MC0_RSCH_RD_OPEN_PAGE_SCORE + mc_offset, 0x32);
	WREG32(mmHBM0_MC0_RSCH_WR_OPEN_PAGE_SCORE + mc_offset, 0x32);

	if (hdev->pldm)
		RREG32(mmHBM0_MC0_PHY_PPROT_PIN + mc_offset);

	WREG32(mmHBM0_MC0_RSCH_RD_SID_SCORE + mc_offset, 0xd2);
	WREG32(mmHBM0_MC0_RSCH_RD_BG_SCORE + mc_offset, 0xd2);
	WREG32(mmHBM0_MC0_RSCH_WR_BG_SCORE + mc_offset, 0xd2);

	/* write-read switching */
	WREG32(mmHBM0_MC0_RSCH_EMPTY_FROM_ARB_VLD + mc_offset, 0x1);
	WREG32(mmHBM0_MC0_RSCH_RD_MIN_STREAM_LENGTH + mc_offset, 0x3c);
	WREG32(mmHBM0_MC0_RSCH_WR_SWITCH_OCCUPANCY_LEVEL_QOS_0 + mc_offset, 0x1a);
	WREG32(mmHBM0_MC0_RSCH_WR_SWITCH_OCCUPANCY_LEVEL_QOS_1 + mc_offset, 0x1a);
	WREG32(mmHBM0_MC0_RSCH_WR_MIN_STREAM_LENGTH + mc_offset, 0x23);
	WREG32(mmHBM0_MC0_RSCH_WR_ALMOST_EMPTY_LEVEL + mc_offset, 0x2);
	WREG32(mmHBM0_MC0_RSCH_RD_SWITCH_LATENCY_MAX + mc_offset, 0x1ff);
	WREG32(mmHBM0_MC0_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS0_0 + mc_offset, 0x1a);
	WREG32(mmHBM0_MC0_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS0_1 + mc_offset, 0x1a);
	WREG32(mmHBM0_MC0_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS1_0 + mc_offset, 0x1a);
	WREG32(mmHBM0_MC0_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS1_1 + mc_offset, 0x1a);
	WREG32(mmHBM0_MC0_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS2_0 + mc_offset, 0x1a);
	WREG32(mmHBM0_MC0_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS2_1 + mc_offset, 0x1a);
	WREG32(mmHBM0_MC0_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS3_0 + mc_offset, 0x1a);
	WREG32(mmHBM0_MC0_RSCH_RD_SWITCH_OCCUPANCY_LEVEL_QOS3_1 + mc_offset, 0x1a);
	WREG32(mmHBM0_MC0_RSCH_MAX_PAGE + mc_offset, 0x1f);
}

static int set_timing_params(struct hl_device *hdev,
			     struct gaudi2_hbm *hbm_cfg)
{
	enum gaudi2_hbm_freqs freq = hbm_cfg->ck_freq;
	struct hbm_ac_params *ac_params = &hbm_cfg->ac_params;

	switch (freq) {
	case HBM_PLL_1200:
		ac_params->rd_lat = 26;
		ac_params->wr_lat = 10;
		ac_params->parity_lat = 2;
		ac_params->mc_tfaw = 0x7;
		ac_params->mc_trrd_l = 0x1;
		ac_params->mc_trrd_s = 0x1;
		ac_params->mc_tras = 0x13;
		ac_params->mc_trp = 0xa;
		ac_params->mc_trc = 0x1c;
		ac_params->mc_trcd_rd = 0x14;
		ac_params->mc_trcd_wr = 0xc;
		ac_params->mc_trtp = 0x3;
		ac_params->mc_trfc = 0x10d;
		ac_params->mc_trfcsb = 0x77;
		ac_params->mc_trrefd = 0x4;
		ac_params->mc_tccd_l = 0x1;
		ac_params->mc_tccd_s = 0x0;
		ac_params->mc_tccd_r = 0x1;
		ac_params->mc_twtr_s = 0x8;
		ac_params->mc_twtr_l = 0xb;
		ac_params->mc_twtrap = 0xf;
		ac_params->mc_trtw = 0x12;
		ac_params->mc_twtp = 0x11;
		ac_params->mc_trefi[0] = 0x923;
		ac_params->mc_trefi[1] = 0x923;
		ac_params->mc_trefi[2] = 0x923;
		ac_params->mc_trefi[3] = 0x923;
		ac_params->mc_trefi[4] = 0x923;
		ac_params->mc_trefi[5] = 0x923;
		ac_params->mc_trefi[6] = 0x923;
		ac_params->mc_trefi[7] = 0x923;
		ac_params->mc_trefisb_adj = 0x47;
		ac_params->mc_tmod = 0x8;
		ac_params->mc_tcksre = 0x5;
		ac_params->mc_tcksrx = 0x5;
		ac_params->mc_tckpde = 0x4;
		ac_params->mc_txs = 0x113;
		ac_params->mc_txp = 0x4;
		ac_params->mc_refsb_gap = 0xa;
		ac_params->twr = 0x16;
		ac_params->extend_phased_time = 0;
		ac_params->t_pwreset = 1200;
		ac_params->hbm_twtr_l = 11;
		ac_params->hbm_tinit5 = 0x154;
		ac_params->hbm_tmod = 13;
		break;

	case HBM_PLL_1600:
		ac_params->rd_lat = 35;
		ac_params->wr_lat = 10;
		ac_params->parity_lat = 2;
		ac_params->mc_tfaw = 0x9;
		ac_params->mc_trrd_l = 0x2;
		ac_params->mc_trrd_s = 0x2;
		ac_params->mc_tras = 0x19;
		ac_params->mc_trp = 0xe;
		ac_params->mc_trc = 0x25;
		ac_params->mc_trcd_rd = 0x1b;
		ac_params->mc_trcd_wr = 0x10;
		ac_params->mc_trtp = 0x3;
		ac_params->mc_trfc = 0x167;
		ac_params->mc_trfcsb = 0x9f;
		ac_params->mc_trrefd = 0x6;
		ac_params->mc_tccd_l = 0x1;
		ac_params->mc_tccd_s = 0x0;
		ac_params->mc_tccd_r = 0x1;
		ac_params->mc_twtr_s = 0x8;
		ac_params->mc_twtr_l = 0xc;
		ac_params->mc_twtrap = 0x12;
		ac_params->mc_trtw = 0x10;
		ac_params->mc_twtp = 0x15;
		ac_params->mc_trefi[0] = 0xc2f;
		ac_params->mc_trefi[1] = 0xc2f;
		ac_params->mc_trefi[2] = 0xc2f;
		ac_params->mc_trefi[3] = 0xc2f;
		ac_params->mc_trefi[4] = 0xc2f;
		ac_params->mc_trefi[5] = 0xc2f;
		ac_params->mc_trefi[6] = 0xc2f;
		ac_params->mc_trefi[7] = 0xc2f;
		ac_params->mc_trefisb_adj = 0x5f;
		ac_params->mc_tmod = 0xb;
		ac_params->mc_tcksre = 0x7;
		ac_params->mc_tcksrx = 0x7;
		ac_params->mc_tckpde = 0x5;
		ac_params->mc_txs = 0x16f;
		ac_params->mc_txp = 0x5;
		ac_params->mc_refsb_gap = 0xa;
		ac_params->twr = 0x1d;
		ac_params->extend_phased_time = 0;
		ac_params->t_pwreset = 1600;
		ac_params->hbm_twtr_l = 13;
		ac_params->hbm_tinit5 = 320;
		ac_params->hbm_tmod = 24;
		break;

	case HBM_PLL_1800:
	default:
		ac_params->rd_lat = 40;
		ac_params->wr_lat = 11;
		ac_params->parity_lat = 2;
		ac_params->mc_tfaw = 0xa;
		ac_params->mc_trrd_l = 0x2;
		ac_params->mc_trrd_s = 0x2;
		ac_params->mc_tras = 0x1c;
		ac_params->mc_trp = 0xf;
		ac_params->mc_trc = 0x2a;
		ac_params->mc_trcd_rd = 0x1e;
		ac_params->mc_trcd_wr = 0x12;
		ac_params->mc_trtp = 0x3;
		ac_params->mc_trfc = 0x194;
		ac_params->mc_trfcsb = 0xb3;
		ac_params->mc_trrefd = 0x7;
		ac_params->mc_tccd_l = 0x1;
		ac_params->mc_tccd_s = 0x0;
		ac_params->mc_tccd_r = 0x1;
		ac_params->mc_twtr_s = 0x9;
		ac_params->mc_twtr_l = 0xd;
		ac_params->mc_twtrap = 0x14;
		ac_params->mc_trtw = 0x13;
		ac_params->mc_twtp = 0x16;
		ac_params->mc_trefi[0] = 0xdb5;
		ac_params->mc_trefi[1] = 0xdb5;
		ac_params->mc_trefi[2] = 0xdb5;
		ac_params->mc_trefi[3] = 0xdb5;
		ac_params->mc_trefi[4] = 0xdb5;
		ac_params->mc_trefi[5] = 0xdb5;
		ac_params->mc_trefi[6] = 0xdb5;
		ac_params->mc_trefi[7] = 0xdb5;
		ac_params->mc_trefisb_adj = 0x6b;
		ac_params->mc_tmod = 0xd;
		ac_params->mc_tcksre = 0x8;
		ac_params->mc_tcksrx = 0x8;
		ac_params->mc_tckpde = 0x6;
		ac_params->mc_txs = 0x19d;
		ac_params->mc_txp = 0x6;
		ac_params->mc_refsb_gap = 0xa;
		ac_params->twr = 0x1f;
		ac_params->extend_phased_time = 0x1;
		ac_params->t_pwreset = 1800;
		ac_params->hbm_twtr_l = 15;
		ac_params->hbm_tinit5 = 360;
		ac_params->hbm_tmod = 27;
		break;
	}

	/* DFI timing calc */
	ac_params->tphy_wr_lat = ac_params->wr_lat - 2;
	ac_params->trd_data_en = (ac_params->rd_lat - 2) - 17;
	ac_params->ctrlupd_max_t = 54 + ac_params->extend_phased_time;

	hl_dbg(hdev, "Setting AC params for system frequency %dMHz\n", enum_to_freq(freq));
	return 0;
}

static void mc_dfi_master_init(struct hl_device *hdev, u32 dev, u32 is_master)
{
	u32 mc_offset = dev * HBM_DEV_OFFSET + is_master * MC_OFFSET;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	struct hbm_ac_params *ac_params = &hbm_cfg->ac_params;
	u32 reg_val;
	int mc_ch;


	WREG32(mmHBM0_MC0_DFI_MC_CHANNEL_CFG + mc_offset, 0x22);
	for (mc_ch = 0; mc_ch < MC_CHANNELS_NUM; mc_ch++)
		WREG32(mmHBM0_MC0_DFI_PHY_CFG_CH_0 + mc_offset + mc_ch * 4, 0x1b6c924);

	reg_val = 0x140f0204;
	reg_val |= FIELD_PREP(HBM0_MC0_DFI_PHY_CFG_DFI_ECC_ENABLE_MASK, hbm_cfg->ecc_enable);
	reg_val |= FIELD_PREP(HBM0_MC0_DFI_PHY_CFG_T_RD_DATA_EN_MASK, ac_params->trd_data_en);
	reg_val |= FIELD_PREP(HBM0_MC0_DFI_PHY_CFG_T_PHY_WR_LAT_MASK, ac_params->tphy_wr_lat);
	WREG32(mmHBM0_MC0_DFI_PHY_CFG + mc_offset, reg_val);

	reg_val = 0x1f8fa380;
	reg_val |= FIELD_PREP(HBM0_MC0_DFI_PHY_CFG_2_CTRLUPD_MAX_T_MASK,
			       ac_params->ctrlupd_max_t);
	WREG32(mmHBM0_MC0_DFI_PHY_CFG_2 + mc_offset, reg_val);

	if (hdev->pldm)
		RREG32(mmHBM0_MC0_PHY_PPROT_PIN + mc_offset);
}

static void mc_sequencer_init(struct hl_device *hdev, u32 dev, u32 is_master)
{
	u32 mc_offset = dev * HBM_DEV_OFFSET + is_master * MC_OFFSET;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	struct hbm_ac_params *ac_params = &hbm_cfg->ac_params;
	int i;

	WREG32(mmHBM0_MC0_SEQ_TWTR_S + mc_offset, ac_params->mc_twtr_s);
	WREG32(mmHBM0_MC0_SEQ_TWTR_L + mc_offset, ac_params->mc_twtr_l);
	WREG32(mmHBM0_MC0_SEQ_TWTP + mc_offset, ac_params->mc_twtp);
	WREG32(mmHBM0_MC0_SEQ_TRTW + mc_offset, ac_params->mc_trtw);
	WREG32(mmHBM0_MC0_SEQ_TRAS + mc_offset, ac_params->mc_tras);
	WREG32(mmHBM0_MC0_SEQ_TRTP + mc_offset, ac_params->mc_trtp);
	WREG32(mmHBM0_MC0_SEQ_TRP + mc_offset, ac_params->mc_trp);
	WREG32(mmHBM0_MC0_SEQ_TRRD_L + mc_offset, ac_params->mc_trrd_l);
	WREG32(mmHBM0_MC0_SEQ_TRRD_S + mc_offset, ac_params->mc_trrd_s);
	WREG32(mmHBM0_MC0_SEQ_TFAW + mc_offset, ac_params->mc_tfaw);
	WREG32(mmHBM0_MC0_SEQ_TCCD_L + mc_offset, ac_params->mc_tccd_l);
	WREG32(mmHBM0_MC0_SEQ_TCCD_R + mc_offset, ac_params->mc_tccd_r);
	WREG32(mmHBM0_MC0_SEQ_TRCD_RD + mc_offset, ac_params->mc_trcd_rd);
	WREG32(mmHBM0_MC0_SEQ_TRCD_WR + mc_offset, ac_params->mc_trcd_wr);
	if (hdev->pldm)
		RREG32(mmHBM0_MC0_PHY_PPROT_PIN + mc_offset);

	for (i = 0; i < 8; i++)
		WREG32(mmHBM0_MC0_SEQ_TREFI_0 + mc_offset + i * 4,
		       ac_params->mc_trefi[3]);

	for (i = 0; i < 8; i++)
		WREG32(mmHBM0_MC0_SEQ_TREFISB_ADJ_0 + mc_offset + i * 4,
		       ac_params->mc_trefisb_adj);
	WREG32(mmHBM0_MC0_SEQ_TREFI_TH + mc_offset, 0x3e8);

	WREG32(mmHBM0_MC0_SEQ_TRFC + mc_offset, ac_params->mc_trfc);
	WREG32(mmHBM0_MC0_SEQ_TRFCSB + mc_offset, ac_params->mc_trfcsb);
	WREG32(mmHBM0_MC0_SEQ_TWTRAP + mc_offset, ac_params->mc_twtrap);
	WREG32(mmHBM0_MC0_SEQ_TCKSRE + mc_offset, ac_params->mc_tcksre);
	WREG32(mmHBM0_MC0_SEQ_TCKSRX + mc_offset, ac_params->mc_tcksrx);
	WREG32(mmHBM0_MC0_SEQ_TCKPDE + mc_offset, ac_params->mc_tckpde);
	WREG32(mmHBM0_MC0_SEQ_TXS + mc_offset, ac_params->mc_txs);
	WREG32(mmHBM0_MC0_SEQ_TXP + mc_offset, ac_params->mc_txp);
	WREG32(mmHBM0_MC0_SEQ_TRREFD + mc_offset, ac_params->mc_trrefd);
	WREG32(mmHBM0_MC0_SEQ_TMOD + mc_offset, ac_params->mc_tmod);

	if (hdev->pldm)
		RREG32(mmHBM0_MC0_PHY_PPROT_PIN + mc_offset);

	WREG32(mmHBM0_MC0_SEQ_REFSB_CNT_URGENT_TH + mc_offset, 0x8);
	WREG32(mmHBM0_MC0_SEQ_REFSB_BURST_NUM_URGENT + mc_offset, 0x1);
	WREG32(mmHBM0_MC0_SEQ_REFSB_CNT_HYSTERIC_TH + mc_offset, 0x1e);
	WREG32(mmHBM0_MC0_SEQ_REFSB_BURST_NUM_HYSTERIC + mc_offset, 0x1b);
	WREG32(mmHBM0_MC0_SEQ_CATTRIP_THROT_EN + mc_offset, 0x1);

	/* C bus queue */
	WREG32(mmHBM0_MC0_SEQ_CBQ_FIFO_FULL_TH + mc_offset, 0x5);
}

static void mc_enable_interrupts(struct hl_device *hdev, u32 mc_offset)
{
	/* clear interrupts */
	(void)RREG32(mmHBM0_MC0_SEI_STATUS_INTR_0 + mc_offset);
	(void)RREG32(mmHBM0_MC0_SEI_STATUS_INTR_1 + mc_offset);
	(void)RREG32(mmHBM0_MC0_SPI_STATUS_INTR + mc_offset);

	/* severe errors are mapped to SEI0 pin
	 * AC_PAR, CATTRIP, DERR, WR_PAR, RD_PAR
	 */
	WREG32(mmHBM0_MC0_SEI0_MASK_INTR_0 + mc_offset, 0xff00ef00);
	WREG32(mmHBM0_MC0_SEI0_MASK_INTR_1 + mc_offset, 0xffff0000);

	/* non-severe errors are mapped to SEI1 pin
	 * DFI_ERR, TEMP_RDOUT, MEM_BIST_FAIL, SERR, BIST_FAIL
	 */
	WREG32(mmHBM0_MC0_SEI1_MASK_INTR_0 + mc_offset, 0xff90ff);
	WREG32(mmHBM0_MC0_SEI1_MASK_INTR_1 + mc_offset, 0xff00ffff);

	WREG32(mmHBM0_MC0_SPI_MASK_INTR + mc_offset, 0x0);
}

static void mc_enable_traffic(struct hl_device *hdev, u32 mc_offset)
{
	int mc_ch;

	for (mc_ch = 0; mc_ch < MC_CHANNELS_NUM; mc_ch++)
		WREG32(mmHBM0_MC0_AXIS_EN_0 + mc_offset + 4 * mc_ch, 0xf);

	WREG32(mmHBM0_MC0_AXI_GLBL_TAP_EN + mc_offset, 0xff);
}

static void mc_phy_config(struct hl_device *hdev, u32 dev)
{
	int mc_idx;
#ifdef DUMP_DYNAMIC
	if (dev == 0)
		hl_err(hdev, "DUMP: %s +\n", __func__);
#endif
	phy_reset(hdev, dev);

	for (mc_idx = 0; mc_idx < HBM_MC_NUM; mc_idx++) {
		mc_axis_init(hdev, dev, mc_idx);
		mc_scheduler_init(hdev, dev, mc_idx);
		mc_sequencer_init(hdev, dev, mc_idx);
		mc_dfi_master_init(hdev, dev, mc_idx);
	}

	phy_init_config(hdev, dev);
#ifdef DUMP_DYNAMIC
	if (dev == 0)
		hl_err(hdev, "DUMP: %s -\n", __func__);
#endif
}

static int phy_pre_training_tests(struct hl_device *hdev, u32 dev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	int rc = status_pass;

	phy_config_p1500(hdev, dev, hbm_cfg->ck_freq);
	phy_pubmode(hdev, dev, pubmode_enter);

	rc = phy_test_loopback(hdev, dev);
	if (rc)
		goto exit;

	rc = phy_test_io_dc(hdev, dev);
	if (rc)
		goto exit;

	/* PHY trigges HBM reset after EXTESTs, need to re-set mode-registers */
	rc = hbm_mrs(hdev, dev);
	if (rc)
		goto exit;

	if (RUN_PHY_MISR)
		phy_test_io_ac(hdev, dev);

exit:
	phy_pubmode(hdev, dev, pubmode_exit);
	return rc;
}

static void open_traffic(struct hl_device *hdev, u32 dev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	u32 mc_offset;
	int mc_idx;

	for (mc_idx = 0; mc_idx < HBM_MC_NUM; mc_idx++) {
		mc_offset = dev * HBM_DEV_OFFSET + mc_idx * MC_OFFSET;

		if (hdev->pldm)
			RMWREG32(mmHBM0_MC0_DFI_PHY_CFG_2 + mc_offset, 0x0,
				HBM0_MC0_DFI_PHY_CFG_2_DFI_ADDRESS_XOR_ENABLE_MASK);

		if (hbm_cfg->dfi_phy_update)
			phy_write_all_ch(hdev, dev, mmHBM_PHY_CHAN_CHAN0_AWORD_DFIPHYUPD, 0xf);

		/* the following 2 reg writes are executed from both MC0 and MC1.
		 * MC0 execution is not relevant
		 */
		RMWREG32(mmHBM0_MC0_DFI_PHY_CFG_2 + mc_offset, 0x1,
			HBM0_MC0_DFI_PHY_CFG_2_DFI_CATTRIP_ENABLE_MASK);

		/* refresh start - single bank */
		WREG32(mmHBM0_MC0_SEQ_REF_MODE + mc_offset, 0x2);

		mc_enable_interrupts(hdev, mc_offset);
		mc_enable_traffic(hdev, mc_offset);
	}
}

/* 8 signals will be directed to GPIOs 20:27 per configuration
 * PHY dumps supports options 0-3. HCON and MC support options 0-2
 * see uArch/FC/Dump Bus.xls for more details
 */
static void hbm_dump_bus(struct hl_device *hdev, u32 dev,
			enum dump_component component, u32 option)
{
	const u32 dump_hbm_offset = 18;
	const u32 psoc_gpio_func0 = 0;
	u32 gpio_idx, reg_val = 0;

	if (hdev->pldm)
		return;

	RMWREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS_L, 0x0,
		PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_L_DUMP_DIS_MASK);

	reg_val = ((dev + dump_hbm_offset) << 8) | component << 2 | option;
	RMWREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS_L, reg_val,
		PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_L_DUMP_SEL_MASK);

	/* configure GPIO_20:27  to func0. signals should be drived out from the pins */
	for (gpio_idx = 0; gpio_idx < 8; gpio_idx++)
		WREG32(mmPSOC_GLOBAL_CONF_PAD_SEL_20 + gpio_idx * 4, psoc_gpio_func0);
}

static void enable_auto_temp(struct hl_device *hdev, u32 dev)
{
	u32 reg_val, mc1_offset = dev * HBM_DEV_OFFSET + MC_OFFSET;
	static const u32 auto_temp_interval = 0x1;

	reg_val = RREG32(mmHBM0_MC0_P1500_AUTO_TMP + mc1_offset);
	reg_val |= FIELD_PREP(HBM0_MC0_P1500_AUTO_TMP_ENABLE_MASK, 0x1);
	reg_val |= FIELD_PREP(HBM0_MC0_P1500_AUTO_TMP_INTERVAL_MASK, auto_temp_interval);
	WREG32(mmHBM0_MC0_P1500_AUTO_TMP + mc1_offset, reg_val);

	hl_dbg(hdev, "Auto Temperature in HBM%d is enabled\n", dev);
}

static void enable_traffic_throttling(struct hl_device *hdev, u32 dev)
{
	u32 mc_offset, mc_idx, reg_val = 0;
	static const u32 en_threshold = 85, dis_threshold = 65;

	for (mc_idx = 0; mc_idx < HBM_MC_NUM; mc_idx++) {
		mc_offset = dev * HBM_DEV_OFFSET + mc_idx * MC_OFFSET;
		/* limitig traffic to 2/3 capacity */
		WREG32(mmHBM0_MC0_SEQ_MONI_THROT_ON + mc_offset, 60);
		WREG32(mmHBM0_MC0_SEQ_MONI_THROT_OFF + mc_offset, 30);
		/* MC0 slave, MC1 master */
		if (mc_idx) {
			reg_val = FIELD_PREP(HBM0_MC0_P1500_THROT_MON_THROT_MODE_MASK, 0x1);
			/* setting threshold 85C for limiting traffic and 65C to unlimit back */
			reg_val |= FIELD_PREP(HBM0_MC0_P1500_THROT_MON_THROT_TEMP_DIS_MASK,
			dis_threshold);
			reg_val |= FIELD_PREP(HBM0_MC0_P1500_THROT_MON_THROT_TEMP_EN_MASK,
			en_threshold);
		} else
			reg_val = FIELD_PREP(HBM0_MC0_P1500_THROT_MON_THROT_MODE_MASK, 0x2);
		WREG32(mmHBM0_MC0_P1500_THROT_MON + mc_offset, reg_val);
	}
	hl_dbg(hdev, "Traffic Throttling in HBM%d is enabled\n", dev);
}

static int phy_ch_mapping(int ch)
{
	int phy_map[] = {0, 1, 4, 5, 2, 3, 6, 7};
	return phy_map[ch];
}

static int mc_mrs_cmd(struct hl_device *hdev, u32 hbm_dev, int mc_idx, int ch_idx, u8 mr_num,
		      u8 mr_val)
{
	int timeout;
	int trig_delay = hdev->pldm ? PLDM_TIMEOUT : 5;
	u32 poll1, poll2, poll3, val;
	u32 auto_pwr_mode;
	u32 dev_offset = hbm_dev * HBM_DEV_OFFSET;
	u32 mc_offset  = dev_offset + mc_idx * MC_OFFSET;
	u32 ch_offset  = mc_offset + ch_idx * PC_OFFSET;

	/* Temporarily disable AutoPwr modes */
	auto_pwr_mode = RREG32(mmHBM0_MC0_AUTO_PWR_EN_0 + ch_offset);
	WREG32(mmHBM0_MC0_AUTO_PWR_EN_0 + ch_offset, 0x0);

	/* Setup the MC MRS register */
	val = ((mr_num << HBM0_MC0_SEQ_MRS_CMD_NUM_SHIFT) & HBM0_MC0_SEQ_MRS_CMD_NUM_MASK) |
	      ((mr_val << HBM0_MC0_SEQ_MRS_CMD_VAL_SHIFT) & HBM0_MC0_SEQ_MRS_CMD_VAL_MASK);
	WREG32(mmHBM0_MC0_SEQ_MRS_0 + ch_offset, val);

	/* Verify no background operation by MC */
	timeout = hl_poll_timeout(
			hdev, mmHBM0_MC0_SEQ_MRS_COMMAND_TRIG_0 + ch_offset,
			poll1,
			((poll1 & HBM0_MC0_SEQ_MRS_COMMAND_TRIG_VAL_MASK) == 0),
			1, trig_delay);
	timeout |= hl_poll_timeout(
			hdev, mmHBM0_MC0_SEQ_PWR_COMMAND_TRIG_0 + ch_offset,
			poll2,
			((poll2 & HBM0_MC0_SEQ_PWR_COMMAND_TRIG_VAL_MASK) == 0),
			1, trig_delay);
	timeout |= hl_poll_timeout(
			hdev, mmHBM0_MC0_SEQ_PWR_0 + ch_offset,
			poll3,
			((poll3 & HBM0_MC0_SEQ_PWR_STATE_MASK) == 0),
			1, trig_delay);
	if (timeout) {
		hl_err(hdev,
			"MC MRS command FAILED (HBM%d MC%d CH%d) - Timeout while polling for idle state: MRS_COMMAND_TRIG=0x%08x, PWR_COMMAND_TRIG=0x%08x, PWR_STATE=0x%08x\n",
			hbm_dev, mc_idx, ch_idx, poll1, poll2, poll3);
		return status_fail;
	}

	/* Trigger MRS command */
	WREG32(mmHBM0_MC0_SEQ_MRS_COMMAND_TRIG_0 + ch_offset, 0x1);

	/* Poll for MRS completion */
	timeout = hl_poll_timeout(
			hdev, mmHBM0_MC0_SEQ_MRS_COMMAND_TRIG_0 + ch_offset,
			poll1,
			((poll1 & HBM0_MC0_SEQ_MRS_COMMAND_TRIG_VAL_MASK) == 0),
			1, trig_delay);
	if (timeout) {
		hl_err(hdev,
			"MC MRS command FAILED (HBM%d MC%d CH%d) - Timeout while polling for MRS completion: MRS_COMMAND_TRIG=0x%08x\n",
			hbm_dev, mc_idx, ch_idx, poll1);
		return status_fail;
	}

	/* Restore AutoPwr mode */
	if (auto_pwr_mode)
		WREG32(mmHBM0_MC0_AUTO_PWR_EN_0 + ch_offset, auto_pwr_mode);

	return 0;
}

static int change_dbi(struct hl_device *hdev, u32 hbm_dev, int mc_idx, u32 dbi_mode,
	int ch_begin_idx, int ch_end_idx)
{
	int ch_idx;
	u32 dev_offset = hbm_dev * HBM_DEV_OFFSET;
	u32 mc_offset  = dev_offset + mc_idx * MC_OFFSET;
	u32 bcast_offset  = 0xF * PHY_CH_OFFSET;
	u8  pl = dbi_mode ? 2 : 3;
	struct mrs_cmd mr0 = {.num = 0, .val = 0}, mr4 = {.num = 4, .val = 0};

	u32 reg_val = hbm_phy_read(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0);

	u32 rd_par = FIELD_GET(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_READDQPARITY_MASK, reg_val);
	u32 wr_par = FIELD_GET(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_WRITEDQPARITY_MASK, reg_val);
	u32 ca_par = FIELD_GET(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_ADDRESSPARITY_MASK, reg_val);
	u32 ecc = FIELD_GET(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_ECC_MASK, reg_val);
	u32 dm = FIELD_GET(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_WRITEDATAMASK_MASK, reg_val);
	u32 ext_rl = FIELD_GET(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_EXTREADLATENCY_MASK, reg_val);
	u32 ext_wl = FIELD_GET(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_EXTWRITELATENCY_MASK,
			       reg_val);

	mr0.val |= FIELD_PREP(0x1, dbi_mode);
	mr0.val |= FIELD_PREP(0x2, dbi_mode);
	mr0.val |= FIELD_PREP(0x4, 0x1);
	mr0.val |= FIELD_PREP(0x10, rd_par);
	mr0.val |= FIELD_PREP(0x20, wr_par);
	mr0.val |= FIELD_PREP(0x40, ca_par);
	mr4.val |= FIELD_PREP(0x1, ecc);
	mr4.val |= FIELD_PREP(0x2, dm);
	mr4.val |= FIELD_PREP(0xc, pl);
	mr4.val |= FIELD_PREP(0x10, ext_wl);
	mr4.val |= FIELD_PREP(0x20, ext_rl);

	for (ch_idx = ch_begin_idx; ch_idx < ch_end_idx; ch_idx++) {
		if (mc_mrs_cmd(hdev, hbm_dev, mc_idx, ch_idx, 0, mr0.val)) {
			hl_err(hdev,
				"MC %s FAILED (HBM%d MC%d CH%d) - MRS (MR0) command failure\n",
				__func__, hbm_dev, mc_idx, ch_idx);
			return status_fail;
		}

		if (mc_mrs_cmd(hdev, hbm_dev, mc_idx, ch_idx, 4, mr4.val)) {
			hl_err(hdev,
				"MC %s FAILED (HBM%d MC%d CH%d) - MRS (MR4) command failure\n",
				__func__, hbm_dev, mc_idx, ch_idx);
			return status_fail;
		}
	}

	/* Set DBI Mode and Parity Latency in PHY (broadcast to all channels) */
	reg_val &= ~(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_READDBI_MASK |
		     HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_WRITEDBI_MASK);
	reg_val |= FIELD_PREP(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_READDBI_MASK, dbi_mode);
	reg_val |= FIELD_PREP(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0_WRITEDBI_MASK, dbi_mode);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM1_P0 + bcast_offset,
		      reg_val);

	reg_val = hbm_phy_read(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM0_P0);
	reg_val &= ~HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM0_P0_PARITYLATENCY_MASK;
	reg_val |= FIELD_PREP(HBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM0_P0_PARITYLATENCY_MASK, pl);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_AWORD_DRAMPARAM0_P0 + bcast_offset,
		      reg_val);

	/* Set DBI Mode and Parity Latency in DFI Master (single cfg common for all channels) */
	reg_val = RREG32(mmHBM0_MC0_DFI_PHY_CFG + mc_offset);
	reg_val &= ~(HBM0_MC0_DFI_PHY_CFG_WR_DBI_ENABLE_MASK |
		     HBM0_MC0_DFI_PHY_CFG_RD_DBI_ENABLE_MASK |
		     HBM0_MC0_DFI_PHY_CFG_PARITY_RL_MASK);
	reg_val |= FIELD_PREP(HBM0_MC0_DFI_PHY_CFG_WR_DBI_ENABLE_MASK, dbi_mode);
	reg_val |= FIELD_PREP(HBM0_MC0_DFI_PHY_CFG_RD_DBI_ENABLE_MASK, dbi_mode);
	reg_val |= FIELD_PREP(HBM0_MC0_DFI_PHY_CFG_PARITY_RL_MASK, pl);
	WREG32(mmHBM0_MC0_DFI_PHY_CFG + mc_offset, reg_val);

	hl_dbg(hdev, "DBI mode changed to %u (HBM%d MC%d)\n", dbi_mode, hbm_dev, mc_idx);
	return 0;
}

static struct pc_err_bitmap syndrome_to_bitmap(struct hl_device *hdev, u8 syndrome)
{
	struct pc_err_bitmap map;

	map.dq = 0;
	map.dm = 0;

	switch (syndrome) {
	case 0xCE:
		map.dq = 0x0000000000000001; break;
	case 0xCB:
		map.dq = 0x0000000000000002; break;
	case 0xD3:
		map.dq = 0x0000000000000004; break;
	case 0xD5:
		map.dq = 0x0000000000000008; break;
	case 0xD6:
		map.dq = 0x0000000000000010; break;
	case 0xD9:
		map.dq = 0x0000000000000020; break;
	case 0xDA:
		map.dq = 0x0000000000000040; break;
	case 0xDC:
		map.dq = 0x0000000000000080; break;

	case 0x23:
		map.dq = 0x0000000000000100; break;
	case 0x25:
		map.dq = 0x0000000000000200; break;
	case 0x26:
		map.dq = 0x0000000000000400; break;
	case 0x29:
		map.dq = 0x0000000000000800; break;
	case 0x2A:
		map.dq = 0x0000000000001000; break;
	case 0x2C:
		map.dq = 0x0000000000002000; break;
	case 0x31:
		map.dq = 0x0000000000004000; break;
	case 0x34:
		map.dq = 0x0000000000008000; break;

	case 0x0E:
		map.dq = 0x0000000000010000; break;
	case 0x0B:
		map.dq = 0x0000000000020000; break;
	case 0x13:
		map.dq = 0x0000000000040000; break;
	case 0x15:
		map.dq = 0x0000000000080000; break;
	case 0x16:
		map.dq = 0x0000000000100000; break;
	case 0x19:
		map.dq = 0x0000000000200000; break;
	case 0x1A:
		map.dq = 0x0000000000400000; break;
	case 0x1C:
		map.dq = 0x0000000000800000; break;

	case 0xE3:
		map.dq = 0x0000000001000000; break;
	case 0xE5:
		map.dq = 0x0000000002000000; break;
	case 0xE6:
		map.dq = 0x0000000004000000; break;
	case 0xE9:
		map.dq = 0x0000000008000000; break;
	case 0xEA:
		map.dq = 0x0000000010000000; break;
	case 0xEC:
		map.dq = 0x0000000020000000; break;
	case 0xF1:
		map.dq = 0x0000000040000000; break;
	case 0xF4:
		map.dq = 0x0000000080000000; break;

	case 0x4F:
		map.dq = 0x0000000100000000; break;
	case 0x4A:
		map.dq = 0x0000000200000000; break;
	case 0x52:
		map.dq = 0x0000000400000000; break;
	case 0x54:
		map.dq = 0x0000000800000000; break;
	case 0x57:
		map.dq = 0x0000001000000000; break;
	case 0x58:
		map.dq = 0x0000002000000000; break;
	case 0x5B:
		map.dq = 0x0000004000000000; break;
	case 0x5D:
		map.dq = 0x0000008000000000; break;

	case 0xA2:
		map.dq = 0x0000010000000000; break;
	case 0xA4:
		map.dq = 0x0000020000000000; break;
	case 0xA7:
		map.dq = 0x0000040000000000; break;
	case 0xA8:
		map.dq = 0x0000080000000000; break;
	case 0xAB:
		map.dq = 0x0000100000000000; break;
	case 0xAD:
		map.dq = 0x0000200000000000; break;
	case 0xB0:
		map.dq = 0x0000400000000000; break;
	case 0xB5:
		map.dq = 0x0000800000000000; break;

	case 0x8F:
		map.dq = 0x0001000000000000; break;
	case 0x8A:
		map.dq = 0x0002000000000000; break;
	case 0x92:
		map.dq = 0x0004000000000000; break;
	case 0x94:
		map.dq = 0x0008000000000000; break;
	case 0x97:
		map.dq = 0x0010000000000000; break;
	case 0x98:
		map.dq = 0x0020000000000000; break;
	case 0x9B:
		map.dq = 0x0040000000000000; break;
	case 0x9D:
		map.dq = 0x0080000000000000; break;

	case 0x62:
		map.dq = 0x0100000000000000; break;
	case 0x64:
		map.dq = 0x0200000000000000; break;
	case 0x67:
		map.dq = 0x0400000000000000; break;
	case 0x68:
		map.dq = 0x0800000000000000; break;
	case 0x6B:
		map.dq = 0x1000000000000000; break;
	case 0x6D:
		map.dq = 0x2000000000000000; break;
	case 0x70:
		map.dq = 0x4000000000000000; break;
	case 0x75:
		map.dq = 0x8000000000000000; break;

	case 0x01:
		map.dm = 0x01; break;
	case 0x02:
		map.dm = 0x02; break;
	case 0x04:
		map.dm = 0x04; break;
	case 0x08:
		map.dm = 0x08; break;
	case 0x10:
		map.dm = 0x10; break;
	case 0x20:
		map.dm = 0x20; break;
	case 0x40:
		map.dm = 0x40; break;
	case 0x80:
		map.dm = 0x80; break;

	default:
		hl_err(hdev, "Received illegal ECC syndrome! 0x%02x\n", syndrome);
	}

	return map;
}

static u8 encode_remap_from_bitmap(u32 byte_pair)
{
	u8 code;

	switch (byte_pair & 0x3FFFF) {
	/* DQ */
	case 0x00000:
			code = 0xFF; break;
	case 0x00001:
			code = 0xE1; break;
	case 0x00002:
			code = 0xE2; break;
	case 0x00004:
			code = 0xE3; break;
	case 0x00008:
			code = 0xE4; break;
	case 0x00010:
			code = 0xE5; break;
	case 0x00020:
			code = 0xE6; break;
	case 0x00040:
			code = 0xE7; break;
	case 0x00080:
			code = 0xE8; break;
	case 0x00100:
			code = 0x1E; break;
	case 0x00200:
			code = 0x2E; break;
	case 0x00400:
			code = 0x3E; break;
	case 0x00800:
			code = 0x4E; break;
	case 0x01000:
			code = 0x5E; break;
	case 0x02000:
			code = 0x6E; break;
	case 0x04000:
			code = 0x7E; break;
	case 0x08000:
			code = 0x8E; break;
	/* DM */
	case 0x10000:
			code = 0xE0; break;
	case 0x20000:
			code = 0x0E; break;
	/* any other value means more than one bit error (invalid for remap) -
	 * shouldn't have reached this function
	 */
	default:
			code = 0xAA;
	}

	return code;
}

static bool is_valid_repair(u32 current_remap, u32 proposed_remap)
{
/* Checks that:
 *  1. The proposed remap does not contain invalid code (0xAA) which means more than one lane
 *     to repair within a byte pair
 *  2. The proposed remap is consistent with the current for every byte pair, unless either of the
 *     remaps is a "NOP" (code 0xFF)
 */
	u8 cpair, ppair;
	int p;

	for (p = 0; p < 4; p++) {
		cpair = (current_remap >> (p*8)) & 0xFF;
		ppair = (proposed_remap >> (p*8)) & 0xFF;
		if ((ppair == 0xAA) ||
		   ((ppair != cpair) && (ppair != 0xFF) && (cpair != 0xFF)))
			return false;
	}
	return true;
}

static void clear_ecc_status(struct hl_device *hdev, u32 hbm_dev)
{
	u32 mc_offset, pc_offset;
	int pc, mc;

	for (mc = 0; mc < HBM_MC_NUM; mc++) {
		mc_offset = hbm_dev * HBM_DEV_OFFSET + mc * MC_OFFSET;
		for (pc = 0; pc < 8; pc++) {
			pc_offset = mc_offset + pc * PC_OFFSET;
			WREG32(mmHBM0_MC0_DFI_ERR_CTL_0 + pc_offset, 0xff);
			WREG32(mmHBM0_MC0_DFI_ERR_CTL_0 + pc_offset, 0x0);
		}
	}
}

static void clear_interrupts(struct hl_device *hdev, u32 hbm_dev)
{
	u32 mc_offset;
	int mc;

	for (mc = 0; mc < HBM_MC_NUM; mc++) {
		mc_offset = hbm_dev * HBM_DEV_OFFSET + mc * MC_OFFSET;
		RREG32(mmHBM0_MC0_SEI_STATUS_INTR_0 + mc_offset);
		RREG32(mmHBM0_MC0_SEI_STATUS_INTR_1 + mc_offset);
	}
}

static void hbm_mcbist_load_sram(struct hl_device *hdev, u32 mcbist_mem_offset,
				 int row, int col, u32 info)
{
	u32 gw_req;

	gw_req = (row << 4) + col;
	gw_req |= HBM0_MC0BIST0_SPECIAL_MEM_GW_REQ_WNR_MASK;

	ndelay(10);
	WREG32(mmHBM0_MC0BIST0_SPECIAL_MEM_GW_DATA + mcbist_mem_offset, info);
	ndelay(10);
	WREG32(mmHBM0_MC0BIST0_SPECIAL_MEM_GW_REQ + mcbist_mem_offset, gw_req);
	ndelay(10);
	gw_req |= HBM0_MC0BIST0_SPECIAL_MEM_GW_REQ_VLD_MASK;
	WREG32(mmHBM0_MC0BIST0_SPECIAL_MEM_GW_REQ + mcbist_mem_offset, gw_req);
	if (col == 8) {
		ndelay(10);
		gw_req &= ~HBM0_MC0BIST0_SPECIAL_MEM_GW_REQ_VLD_MASK;
		WREG32(mmHBM0_MC0BIST0_SPECIAL_MEM_GW_REQ + mcbist_mem_offset, gw_req);
	}
	ndelay(20);
}

/* Triggers MCBIST FSM and poll for completion */
static int hbm_mcbist_run(struct hl_device *hdev, u32 hbm_dev, u16 sram_bist_sel, int pc_begin_idx,
	 int pc_end_idx)
{
	u32 mc_offset, mcbist_pc_offset, reg_val;
	u32 timeout = hdev->pldm ? PLDM_TIMEOUT : MCBIST_TIMEOUT_USEC;
	u32 dev_offset = hbm_dev * HBM_DEV_OFFSET;
	int pc, rc = 0;

	/* Clear ECC error reporting registers + Clear interrupts */
	clear_ecc_status(hdev, hbm_dev);
	clear_interrupts(hdev, hbm_dev);

	/* Trigger all PCs closely together - for parallelism */
	for (pc = pc_begin_idx; pc < pc_end_idx; pc++) {
		int mc_idx = pc / (MC_CHANNELS_NUM*2);
		int pc_idx = pc % (MC_CHANNELS_NUM*2);

		mc_offset        = dev_offset + mc_idx * MC_OFFSET;
		mcbist_pc_offset = (sram_bist_sel & BIT(pc)) ?
			mc_offset + (MC_CHANNELS_NUM*2) * MCBIST_OFFSET :
			mc_offset + pc_idx * MCBIST_OFFSET;

		WREG32(mmHBM0_MC0BIST0_BIST_START + mcbist_pc_offset, 1);
	}

	if (hdev->pldm)
		mdelay(1000);

	/* Poll for MCBIST end */
	for (pc = pc_begin_idx; pc < pc_end_idx; pc++) {
		int mc_idx = pc / (MC_CHANNELS_NUM*2);
		int pc_idx = pc % (MC_CHANNELS_NUM*2);

		mc_offset = dev_offset + mc_idx * MC_OFFSET;
		mcbist_pc_offset = (sram_bist_sel & BIT(pc)) ?
			mc_offset + (MC_CHANNELS_NUM*2) * MCBIST_OFFSET :
			mc_offset + pc_idx * MCBIST_OFFSET;

		rc = hl_poll_timeout(
			hdev,
			mmHBM0_MC0BIST0_BIST_DONE + mcbist_pc_offset,
			reg_val,
			(reg_val == 0x1),
			10,
			timeout);
		if (rc) {
			hl_err(hdev,
				"HBM MCBIST FAILED - Timeout while polling on MCBIST end - HBM%d MC%d PC%02d\n",
				hbm_dev, pc, mc_idx);
			hl_err(hdev, "SEI0: 0x%x, SEI1: 0x%x\n",
				RREG32(mmHBM0_MC0_SEI_STATUS_INTR_0 + mc_offset),
				RREG32(mmHBM0_MC0_SEI_STATUS_INTR_1 + mc_offset));
			return status_fail;
		}
	}

	return status_pass;
}

/* Read HBM MC0/1 status registers and look for errors
 * Return code:
 * 16b LSB - bitmap of PCs that had an error (of any kind)
 * 16b MSB - bitmap of PCs that had a CA Parity error
 */
static u32 mcbist_error_parser(struct hl_device *hdev, u32 hbm_dev,
			    u16 sram_bist_sel,
			    struct pc_err_bitmap *err_bitmaps_out, int pc_begin_idx, int pc_end_idx)
{
	u32 mc_offset, pc_offset, mcbist_pc_offset, rc = 0, addr, serr_beats, syndromes;
	u32 dev_offset = hbm_dev * HBM_DEV_OFFSET;
	u16 serr, capar, rdpar, wrpar, bisterr;
	struct pc_err_bitmap map;
	int pc, i;
	u64 exp, rcv;

	/* Check for various IO errors (note interrupt registers are Clear-on-read) */
	u32 mc0_sei_stat_0 = RREG32(mmHBM0_MC0_SEI_STATUS_INTR_0 + dev_offset);
	u32 mc1_sei_stat_0 = RREG32(mmHBM0_MC0_SEI_STATUS_INTR_0 + dev_offset + MC_OFFSET);
	u32 mc0_sei_stat_1 = RREG32(mmHBM0_MC0_SEI_STATUS_INTR_1 + dev_offset);
	u32 mc1_sei_stat_1 = RREG32(mmHBM0_MC0_SEI_STATUS_INTR_1 + dev_offset + MC_OFFSET);

	serr = ((mc0_sei_stat_0 & 0xFF000000) >> 24) |  ((mc1_sei_stat_0 & 0xFF000000) >> 16);
	capar = ((mc0_sei_stat_0 & 0x000000FF)) | ((mc1_sei_stat_0 & 0x000000FF) << 8);
	rdpar = ((mc0_sei_stat_1 & 0x000000FF)) | ((mc1_sei_stat_1 & 0x000000FF) << 8);
	wrpar = ((mc0_sei_stat_1 & 0x0000FF00) >> 8) | ((mc1_sei_stat_1 & 0x0000FF00));
	bisterr = ((mc0_sei_stat_1 & 0x00FF0000) >> 16) | ((mc1_sei_stat_1 & 0x00FF0000) >> 8);

	/* Check results */
	for (pc = pc_begin_idx; pc < pc_end_idx; pc++) {
		int mc_idx = pc / (MC_CHANNELS_NUM*2);
		int pc_idx = pc % (MC_CHANNELS_NUM*2);

		mc_offset = dev_offset + mc_idx * MC_OFFSET;
		pc_offset = mc_offset + pc_idx * PC_OFFSET;
		mcbist_pc_offset = (sram_bist_sel & BIT(pc)) ?
				   mc_offset + (MC_CHANNELS_NUM*2) * MCBIST_OFFSET :
				   mc_offset + pc_idx * MCBIST_OFFSET;

		/*** Parity errors ***/
		if (capar & BIT(pc)) {
			rc |= BIT(pc);
			/* high 16b of rc denote CA error */
			rc |= (BIT(pc) << 16);
			hl_err(hdev,
				"HBM MCBIST FAILED - CA parity error(s) - HBM%d PC%d (MC%d)\n",
				hbm_dev, pc, mc_idx);
		}

		if (rdpar & BIT(pc)) {
			rc |= BIT(pc);
			hl_err(hdev,
				"HBM MCBIST FAILED - RD parity error(s) - HBM%d PC%d (MC%d)\n",
				hbm_dev, pc, mc_idx);
		}

		if (wrpar & BIT(pc)) {
			rc |= BIT(pc);
			hl_err(hdev,
				"HBM MCBIST FAILED - WR parity error(s) - HBM%d PC%d (MC%d)\n",
				hbm_dev, pc, mc_idx);
		}

		/*** ECC errors ***/
		if (serr & BIT(pc)) {
			rc |= BIT(pc);
			hl_err(hdev,
				"HBM MCBIST FAILED - %d ECC error(s) - HBM%d PC%d (MC%d)\n",
				RREG32(mmHBM0_MC0_DFI_ECC_SERR_CNT_0  + pc_offset),
				hbm_dev, pc, mc_idx);

			addr = RREG32(mmHBM0_MC0_DFI_RD_ERR_REP_ADDR_0 + pc_offset);
			hl_dbg(hdev,
				"\tECC 1st error: HBM address SID=%01x BG=%01x BA=%01x ROW=0x%04x COL=0x%02x\n",
				(addr & HBM0_MC0_DFI_RD_ERR_REP_ADDR_SID_MASK) >>
				    HBM0_MC0_DFI_RD_ERR_REP_ADDR_SID_SHIFT,
				(addr & HBM0_MC0_DFI_RD_ERR_REP_ADDR_BG_MASK)  >>
				    HBM0_MC0_DFI_RD_ERR_REP_ADDR_BG_SHIFT,
				(addr & HBM0_MC0_DFI_RD_ERR_REP_ADDR_BA_MASK)  >>
				    HBM0_MC0_DFI_RD_ERR_REP_ADDR_BA_SHIFT,
				(addr & HBM0_MC0_DFI_RD_ERR_REP_ADDR_ROW_MASK) >>
				    HBM0_MC0_DFI_RD_ERR_REP_ADDR_ROW_SHIFT,
				(addr & HBM0_MC0_DFI_RD_ERR_REP_ADDR_COL_MASK) >>
				    HBM0_MC0_DFI_RD_ERR_REP_ADDR_COL_SHIFT);

			serr_beats = (RREG32(mmHBM0_MC0_DFI_RD_ERR_REP_ERR_0 + pc_offset) &
				      HBM0_MC0_DFI_RD_ERR_REP_ERR_SERR_MASK) >>
				      HBM0_MC0_DFI_RD_ERR_REP_ERR_SERR_SHIFT;
			syndromes = RREG32(mmHBM0_MC0_DFI_RD_ERR_REP_SYNDROME_0 + pc_offset);
			/* Looping on information from 4 data beats of 64b each */
			for (i = 0 ; i < 4 ; i++, serr_beats >>= 1, syndromes >>= 8) {
				if (serr_beats & 0x1) {
					map = syndrome_to_bitmap(hdev, syndromes & 0xFF);
					err_bitmaps_out[pc].dq |= map.dq;
					err_bitmaps_out[pc].dm |= map.dm;
					hl_dbg(hdev,
						"\tECC 1st error: error bitmap for beat %d: DQ = 0x%016llx, DM = 0x%02x\n",
						i, map.dq, map.dm);
				}
			}
		}

		/*** Data Mismatch errors ***/
		if (bisterr & BIT(pc)) {
			rc |= BIT(pc);
			hl_err(hdev,
				"HBM MCBIST FAILED - %d data mismatch errors - HBM%d PC%02d (MC%d)\n",
				RREG32(mmHBM0_MC0BIST0_MISMATCH_CNT + mcbist_pc_offset),
				hbm_dev, pc, mc_idx);
#ifdef DEBUG
			/******* Removing this section due to bug - JIRA H6-3233  *******
			 *u32 addr = RREG32(mmHBM0_MC0BIST0_MISMATCH_FIRST_ADDR + mcbist_pc_offset);
			 *hl_err(hdev,
			 *	"--> 1st mismatch HBM address: SID=%01x BG=%01x BA=%01x"
			 *      "ROW=0x%04x COL=0x%02x\n",
			 *	/-* addr mapping according to cfg at beginning of this function *-/
			 *	/-* SID = axi[16] *-/
			 *	(addr & 0x00010000) >> 16,
			 *	/-* BG = axi[8:7] *-/
			 *	(addr & 0x00000180)  >> 7,
			 *	/-* BA = axi[10:9] *-/
			 *	(addr & 0x00000600)  >> 9,
			 *	/-* ROW[14:0] = {axi[26:17], axi[15:11]} *-/
			 *	((addr & 0x07FE0000) >> 12) | ((addr & 0x0000F800) >> 11),
			 *	/-* COL[5:0] = {axi[29:27], axi[6:5], 1'b0} *-/
			 *	((addr & 0x38000000) >> 24) | ((addr & 0x00000060) >> 4) );
			 *************************************************************************/
#endif
			/* Accumulating info from 4 latched 64b data beats */
			for (i = 0, exp = 0, rcv = 0 ; i < 4 ; i++) {
				WREG32(mmHBM0_MC0BIST0_EXPECTED_MISMATCH_SEL + mcbist_pc_offset,
				       BIT(i*2+1));
				WREG32(mmHBM0_MC0BIST0_RECEIVED_MISMATCH_SEL + mcbist_pc_offset,
					BIT(i*2+1));
				exp = RREG32(mmHBM0_MC0BIST0_FIRST_MISMATCH_EXPECTED +
					     mcbist_pc_offset);
				rcv = RREG32(mmHBM0_MC0BIST0_FIRST_MISMATCH_RECEIVED +
					     mcbist_pc_offset);
				exp <<= 32;
				rcv <<= 32;
				WREG32(mmHBM0_MC0BIST0_EXPECTED_MISMATCH_SEL +
					mcbist_pc_offset, BIT(i*2));
				WREG32(mmHBM0_MC0BIST0_RECEIVED_MISMATCH_SEL +
					mcbist_pc_offset, BIT(i*2));
				exp |= RREG32(mmHBM0_MC0BIST0_FIRST_MISMATCH_EXPECTED +
					      mcbist_pc_offset);
				rcv |= RREG32(mmHBM0_MC0BIST0_FIRST_MISMATCH_RECEIVED +
					      mcbist_pc_offset);

				hl_dbg(hdev,
					"\t1st mismatch beat %d: expected = 0x%016llx , received = 0x%016llx\n",
					i, exp, rcv);
				hl_dbg(hdev,
					"\terror bitmap beat %d: DQ = 0x%016llx\n",
					i, exp ^ rcv);

				err_bitmaps_out[pc].dq |= (exp ^ rcv);
			}
		}
	}

	return rc;
}

/* Return code:
 * 16b LSB - bitmap of PCs that had an error (of any kind)
 * 16b MSB - bitmap of PCs that had a CA Parity error
 */
static u32 hbm_mcbist(struct hl_device *hdev, u32 hbm_dev,
		/* err_bitmaps returns the failing pins per each PC (array) */
		struct pc_err_bitmap *err_bitmaps,
		struct mcbist_cfg *cfg)
{
	u32 mc_offset, pc_offset, mcbist_pc_offset, bist_addr, rc = 0,
		dev_offset = hbm_dev * HBM_DEV_OFFSET;
	u16 sram_bist_1hot_sel_mc0, sram_bist_1hot_sel_mc1, pc_range = BIT(MC_CHANNELS_NUM*2) - 1,
		poly = 0xD008, seed = 0x1234;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	struct hbm_ac_params *ac_params = &hbm_cfg->ac_params;
	int mc, pc, row, col, mc_ch, mc_begin_idx = 0, mc_end_idx = HBM_MC_NUM, mc_ch_begin_idx = 0,
		mc_ch_end_idx = MC_CHANNELS_NUM, pc_begin_idx = 0, pc_end_idx = HBM_PC_NUM;

	/* Store original configurations (assuming identical in both MCs) */
	u32 sid0 = RREG32(mmHBM0_MC0_AXI_ADMAP_SID_0  + dev_offset);
	u32 sid1 = RREG32(mmHBM0_MC0_AXI_ADMAP_SID_1  + dev_offset);
	u32 bg0 = RREG32(mmHBM0_MC0_AXI_ADMAP_BG_0   + dev_offset);
	u32 bg1 = RREG32(mmHBM0_MC0_AXI_ADMAP_BG_1   + dev_offset);
	u32 ba0 = RREG32(mmHBM0_MC0_AXI_ADMAP_BA_0   + dev_offset);
	u32 ba1 = RREG32(mmHBM0_MC0_AXI_ADMAP_BA_1   + dev_offset);
	u32 row0 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_0  + dev_offset);
	u32 row1 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_1  + dev_offset);
	u32 row2 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_2  + dev_offset);
	u32 row3 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_3  + dev_offset);
	u32 row4 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_4  + dev_offset);
	u32 row5 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_5  + dev_offset);
	u32 row6 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_6  + dev_offset);
	u32 row7 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_7  + dev_offset);
	u32 row8 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_8  + dev_offset);
	u32 row9 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_9  + dev_offset);
	u32 row10 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_10 + dev_offset);
	u32 row11 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_11 + dev_offset);
	u32 row12 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_12 + dev_offset);
	u32 row13 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_13 + dev_offset);
	u32 row14 = RREG32(mmHBM0_MC0_AXI_ADMAP_ROW_14 + dev_offset);
	u32 col0 = RREG32(mmHBM0_MC0_AXI_ADMAP_COL_0  + dev_offset);
	u32 col1 = RREG32(mmHBM0_MC0_AXI_ADMAP_COL_1  + dev_offset);
	u32 col2 = RREG32(mmHBM0_MC0_AXI_ADMAP_COL_2  + dev_offset);
	u32 col3 = RREG32(mmHBM0_MC0_AXI_ADMAP_COL_3  + dev_offset);
	u32 col4 = RREG32(mmHBM0_MC0_AXI_ADMAP_COL_4  + dev_offset);
	u32 ref_mode = RREG32(mmHBM0_MC0_SEQ_REF_MODE + dev_offset);
	u32 axi_glbl_cfg = RREG32(mmHBM0_MC0_AXI_GLBL_CFG + dev_offset);
	u32 sei0_intr_mask0 = RREG32(mmHBM0_MC0_SEI0_MASK_INTR_0 + dev_offset);
	u32 sei0_intr_mask1 = RREG32(mmHBM0_MC0_SEI0_MASK_INTR_1 + dev_offset);
	u32 sei1_intr_mask0 = RREG32(mmHBM0_MC0_SEI1_MASK_INTR_0 + dev_offset);
	u32 sei1_intr_mask1 = RREG32(mmHBM0_MC0_SEI1_MASK_INTR_1 + dev_offset);
	u32 trfc = RREG32(mmHBM0_MC0_SEQ_TRFC + dev_offset);
	u32 trefi_th = RREG32(mmHBM0_MC0_SEQ_TREFI_TH + dev_offset);
	u32 rd_bg_score = RREG32(mmHBM0_MC0_RSCH_RD_BG_SCORE + dev_offset);
	u32 rd_sid_score = RREG32(mmHBM0_MC0_RSCH_RD_SID_SCORE + dev_offset);
	u32 rd_open_score = RREG32(mmHBM0_MC0_RSCH_RD_OPEN_PAGE_SCORE + dev_offset);
	u32 wr_bg_score = RREG32(mmHBM0_MC0_RSCH_WR_BG_SCORE + dev_offset);
	u32 wr_sid_score = RREG32(mmHBM0_MC0_RSCH_WR_SID_SCORE + dev_offset);
	u32 wr_open_score = RREG32(mmHBM0_MC0_RSCH_WR_OPEN_PAGE_SCORE + dev_offset);
	bool orig_dbi_mode = (RREG32(mmHBM0_MC0_DFI_PHY_CFG + dev_offset) &
				HBM0_MC0_DFI_PHY_CFG_RD_DBI_ENABLE_MASK) >>
				HBM0_MC0_DFI_PHY_CFG_RD_DBI_ENABLE_SHIFT;

	if (cfg->mcbist_mode == single_pc) {
		mc_begin_idx = (cfg->pc < 8) ? 0 : 1;
		mc_end_idx = mc_begin_idx + 1;
		mc_ch_begin_idx = (cfg->pc < 8) ? cfg->pc / 2 : (cfg->pc % 8) / 2;
		mc_ch_end_idx = mc_ch_begin_idx + 1;
		pc_begin_idx = cfg->pc;
		pc_end_idx = pc_begin_idx + 1;
	}
	/* Configurations which are per MC (common to all PCs/CHs) */
	for (mc = mc_begin_idx; mc < mc_end_idx; mc++) {
		mc_offset = dev_offset + mc * MC_OFFSET;

		/* enable AXI wr/rd traffic per channel */
		for (mc_ch = mc_ch_begin_idx; mc_ch < mc_ch_end_idx; mc_ch++)
			WREG32(mmHBM0_MC0_AXIS_EN_0 + mc_offset + 4 * mc_ch, 0xf);

		/* Use All-bank Refresh to avoid reordering due to REFSB */
		WREG32(mmHBM0_MC0_SEQ_REF_MODE + mc_offset, 0x0);
		udelay(1);
		WREG32(mmHBM0_MC0_SEQ_REF_MODE + mc_offset, 0x1);
		WREG32(mmHBM0_MC0_SEQ_TRFC + mc_offset, ac_params->mc_trfc + 1);
		WREG32(mmHBM0_MC0_SEQ_TREFI_TH + mc_offset, 2000);

		/* Clear all Scheduling weights to avoid reordering */
		WREG32(mmHBM0_MC0_RSCH_RD_BG_SCORE + mc_offset, 0x0);
		WREG32(mmHBM0_MC0_RSCH_RD_SID_SCORE + mc_offset, 0x0);
		WREG32(mmHBM0_MC0_RSCH_RD_OPEN_PAGE_SCORE + mc_offset, 0x0);
		WREG32(mmHBM0_MC0_RSCH_WR_BG_SCORE + mc_offset, 0x0);
		WREG32(mmHBM0_MC0_RSCH_WR_SID_SCORE + mc_offset, 0x0);
		WREG32(mmHBM0_MC0_RSCH_WR_OPEN_PAGE_SCORE + mc_offset, 0x0);

		/* Reconfig address mapping to break linear addresses into BGs and BAs
		 * (optimize bus utilization)
		 */
		WREG32(mmHBM0_MC0_AXI_ADMAP_SID_0 + mc_offset, 16);
		WREG32(mmHBM0_MC0_AXI_ADMAP_SID_1 + mc_offset, 0);
		WREG32(mmHBM0_MC0_AXI_ADMAP_BG_0 + mc_offset, 7);
		WREG32(mmHBM0_MC0_AXI_ADMAP_BG_1 + mc_offset, 8);
		WREG32(mmHBM0_MC0_AXI_ADMAP_BA_0 + mc_offset, 9);
		WREG32(mmHBM0_MC0_AXI_ADMAP_BA_1 + mc_offset, 10);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_0 + mc_offset, 11);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_1 + mc_offset, 12);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_2 + mc_offset, 13);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_3 + mc_offset, 14);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_4 + mc_offset, 15);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_5 + mc_offset, 17);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_6 + mc_offset, 18);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_7 + mc_offset, 19);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_8 + mc_offset, 20);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_9 + mc_offset, 21);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_10 + mc_offset, 22);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_11 + mc_offset, 23);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_12 + mc_offset, 24);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_13 + mc_offset, 25);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_14 + mc_offset, 26);
		WREG32(mmHBM0_MC0_AXI_ADMAP_COL_0 + mc_offset, 5);
		WREG32(mmHBM0_MC0_AXI_ADMAP_COL_1 + mc_offset, 6);
		WREG32(mmHBM0_MC0_AXI_ADMAP_COL_2 + mc_offset, 27);
		WREG32(mmHBM0_MC0_AXI_ADMAP_COL_3 + mc_offset, 28);
		WREG32(mmHBM0_MC0_AXI_ADMAP_COL_4 + mc_offset, 29);

		/* enable AXIS rdy detour -
		 * disable buffering within AXIS to avoid reordering at the AXIS arbiter
		 */
		/* set bit SCH_RDY_TO_AR, SCH_RDY_TO_AW */
		WREG32(mmHBM0_MC0_AXI_GLBL_CFG + mc_offset, axi_glbl_cfg | BIT(28) | BIT(29));

		/* Disable interrupts */
		WREG32(mmHBM0_MC0_SEI0_MASK_INTR_0 + mc_offset, 0xFFFFFFFF);
		WREG32(mmHBM0_MC0_SEI0_MASK_INTR_1 + mc_offset, 0xFFFFFFFF);
		WREG32(mmHBM0_MC0_SEI1_MASK_INTR_0 + mc_offset, 0xFFFFFFFF);
		WREG32(mmHBM0_MC0_SEI1_MASK_INTR_1 + mc_offset, 0xFFFFFFFF);

		/* Configure required DBI mode */
		if (cfg->dbi_mode != orig_dbi_mode) {
			if (change_dbi(hdev, hbm_dev, mc, cfg->dbi_mode, mc_ch_begin_idx,
				mc_ch_end_idx)) {
				hl_err(hdev,
					"HBM%d MC%d MCBIST FAILED - cannot set DBI mode to %d\n",
					hbm_dev, mc, cfg->dbi_mode);
				return MCBIST_ERR;
			}
		}

		/* Configure SRAM MCBIST (if selected to be used) */
		if (cfg->sram_bist_sel) {
			u32 mcbist_mem_offset = mc_offset + (MC_CHANNELS_NUM*2) * MCBIST_OFFSET;
			/* configure wrap twice on the SRAM (depth is 128) */
			u32 mem_block_size = (cfg->block_size <= 128 && cfg->block_size > 0) ?
					      cfg->block_size : 128;
			u32 mem_rep_num = cfg->rep_num * (cfg->block_size / mem_block_size);
			u32 addr_stride = mem_block_size * 32;

			WREG32(mmHBM0_MC0BIST0_LOOP_NUM + mcbist_mem_offset, cfg->loop_num);
			WREG32(mmHBM0_MC0BIST0_WR_BLOCK_SIZE + mcbist_mem_offset, mem_block_size);
			WREG32(mmHBM0_MC0BIST0_WR_REP_NUM + mcbist_mem_offset, mem_rep_num);
			WREG32(mmHBM0_MC0BIST0_RD_BLOCK_SIZE + mcbist_mem_offset, mem_block_size);
			WREG32(mmHBM0_MC0BIST0_RD_REP_NUM + mcbist_mem_offset, mem_rep_num);
			WREG32(mmHBM0_MC0BIST0_STOP_ON_ERR + mcbist_mem_offset, cfg->stop_on_err);
#ifdef DEBUG
			/* due to JIRA H6-2933: ADDR_MODE config has no effect (always from mem) */
			/*WREG32(mmHBM0_MC0BIST0_ADDR_MODE + mcbist_mem_offset, 2);*/
#endif
			WREG32(mmHBM0_MC0BIST0_ADDR_INCR_INIT_1 + mcbist_mem_offset, 0);
			WREG32(mmHBM0_MC0BIST0_ADDR_MEM_STRIDE + mcbist_mem_offset, addr_stride);
			WREG32(mmHBM0_MC0BIST0_ADDR_INCR_STRIDE + mcbist_mem_offset, addr_stride);
			WREG32(mmHBM0_MC0BIST0_WR_RD + mcbist_mem_offset, 0xd);
			WREG32(mmHBM0_MC0BIST0_RD_DELAY + mcbist_mem_offset, 0x20);
			/* following values are mandatory */
			WREG32(mmHBM0_MC0BIST0_WDATA_MEM_START + mcbist_mem_offset, 0);
			WREG32(mmHBM0_MC0BIST0_WDATA_MEM_END + mcbist_mem_offset, 127);
			WREG32(mmHBM0_MC0BIST0_WDATA_MODE + mcbist_mem_offset, 7);
			WREG32(mmHBM0_MC0BIST0_AXLEN + mcbist_mem_offset, 3);
			WREG32(mmHBM0_MC0BIST0_AXSIZE + mcbist_mem_offset, 5);

			/* Load MCBIST SRAM through gateway */
			bist_addr = rand_addr;
			for (row = 0; row < 128; row++, bist_addr += 32) {
				/* BIST data */
				for (col = 0; col < 8; col++)
					hbm_mcbist_load_sram(hdev, mcbist_mem_offset,
							     row, col, rand_array[row % 16][col]);
				/* BIST address */
				hbm_mcbist_load_sram(hdev, mcbist_mem_offset, row, 8, bist_addr);
			}
			hl_dbg(hdev, "HBM%d MC%d MCBIST SRAM was loaded with address and data\n",
				hbm_dev, mc);
		}
	}

	/* Configurations which are per PC */
	for (pc = pc_begin_idx; pc < pc_end_idx; pc++) {
		int mc_idx = pc / (MC_CHANNELS_NUM * 2);
		int pc_idx = pc % (MC_CHANNELS_NUM * 2);

		mc_offset = dev_offset + mc_idx * MC_OFFSET;
		pc_offset = mc_offset + pc_idx * PC_OFFSET;
		mcbist_pc_offset = mc_offset + pc_idx * MCBIST_OFFSET;

		/* Configure MCBIST */
		WREG32(mmHBM0_MC0_BIST_DATA_MODE_0 + pc_offset, cfg->data_mode);
		WREG32(mmHBM0_MC0_BIST_WDATA_PSEUDO_POLY_0 + pc_offset, poly);
		WREG32(mmHBM0_MC0_BIST_RDATA_PSEUDO_POLY_0 + pc_offset, poly);
		WREG32(mmHBM0_MC0_BIST_WDATA_PSEUDO_SEED_0 + pc_offset, seed);
		WREG32(mmHBM0_MC0_BIST_RDATA_PSEUDO_SEED_0 + pc_offset, seed);
		WREG32(mmHBM0_MC0BIST0_LOOP_NUM + mcbist_pc_offset, cfg->loop_num);
		WREG32(mmHBM0_MC0BIST0_WR_BLOCK_SIZE + mcbist_pc_offset, cfg->block_size);
		WREG32(mmHBM0_MC0BIST0_WR_REP_NUM + mcbist_pc_offset, cfg->rep_num);
		WREG32(mmHBM0_MC0BIST0_RD_BLOCK_SIZE + mcbist_pc_offset, cfg->block_size);
		WREG32(mmHBM0_MC0BIST0_RD_REP_NUM + mcbist_pc_offset, cfg->rep_num);
		WREG32(mmHBM0_MC0BIST0_STOP_ON_ERR + mcbist_pc_offset, cfg->stop_on_err);
		/* following values are mandatory */
		WREG32(mmHBM0_MC0BIST0_WDATA_MODE + mcbist_pc_offset, 7);
		WREG32(mmHBM0_MC0BIST0_ADDR_MEM_STRIDE + mcbist_pc_offset, 0);
		WREG32(mmHBM0_MC0BIST0_WRBLOCK_FENCE_EN + mcbist_pc_offset, 1);
		WREG32(mmHBM0_MC0BIST0_WR_DELAY + mcbist_pc_offset, 0x300);
		WREG32(mmHBM0_MC0BIST0_AXLEN + mcbist_pc_offset, 3);
		WREG32(mmHBM0_MC0BIST0_AXSIZE + mcbist_pc_offset, 5);

		/* Initialize error bitmap */
		err_bitmaps[pc].dq = 0;
		err_bitmaps[pc].dm = 0;
	}

	hl_dbg(hdev, "HBM%d MCBIST settings: DATA_MODE=%d, POLY=0x%04x, SEED=0x%04x\n",
		hbm_dev, cfg->data_mode, poly, seed);

	/* Iterate on all PCs selected for SRAM BIST +
	 * possibly one iteration without any PC using SRAM BIST
	 */
	for (sram_bist_1hot_sel_mc0 = 0x1, sram_bist_1hot_sel_mc1 = 0x1;
		sram_bist_1hot_sel_mc0 || sram_bist_1hot_sel_mc1;
		sram_bist_1hot_sel_mc0 = (sram_bist_1hot_sel_mc0 << 1) & pc_range,
		sram_bist_1hot_sel_mc1 = (sram_bist_1hot_sel_mc1 << 1) & pc_range) {

		u32 cfg_bist_en_mc0, cfg_bist_en_mc1;
		u16 sram_bist_sel;

		/* Find next set bit in each half of sram_bist_sel */
		while (!(cfg->sram_bist_sel & sram_bist_1hot_sel_mc0) && sram_bist_1hot_sel_mc0)
			sram_bist_1hot_sel_mc0 = (sram_bist_1hot_sel_mc0 << 1) & pc_range;
		while (!(cfg->sram_bist_sel & (sram_bist_1hot_sel_mc1<<(MC_CHANNELS_NUM*2))) &&
			sram_bist_1hot_sel_mc1)
			sram_bist_1hot_sel_mc1 = (sram_bist_1hot_sel_mc1 << 1) & pc_range;

		/* Enable MCBIST mode for all PCs + select SRAM BIST */
		cfg_bist_en_mc0 = (HBM0_MC0_BIST_EN_VAL_MASK << HBM0_MC0_BIST_EN_VAL_SHIFT) |
				  (sram_bist_1hot_sel_mc0    << HBM0_MC0_BIST_EN_WITH_MEM_SHIFT);
		cfg_bist_en_mc1 = (HBM0_MC0_BIST_EN_VAL_MASK << HBM0_MC0_BIST_EN_VAL_SHIFT) |
				  (sram_bist_1hot_sel_mc1    << HBM0_MC0_BIST_EN_WITH_MEM_SHIFT);
		WREG32(mmHBM0_MC0_BIST_EN + dev_offset, cfg_bist_en_mc0);
		WREG32(mmHBM0_MC0_BIST_EN + dev_offset + MC_OFFSET, cfg_bist_en_mc1);
		hl_dbg(hdev,
			"HBM%d MCBIST settings: cfg_bist_en_mc0 = 0x%08x , cfg_bist_en_mc1 = 0x%08x\n",
			hbm_dev, cfg_bist_en_mc0, cfg_bist_en_mc1);

		/* Following loop is a workaround for JIRA H6-3266 */
		for (pc = pc_begin_idx; pc < pc_end_idx; pc++) {
			int mc_idx = pc / (MC_CHANNELS_NUM*2);
			int pc_idx = pc % (MC_CHANNELS_NUM*2);

			mc_offset = dev_offset + mc_idx * MC_OFFSET;
			pc_offset = mc_offset + pc_idx * PC_OFFSET;
			WREG32(mmHBM0_MC0_BIST_ADDRESS_WR_0 + pc_offset, cfg->start_addr);
			WREG32(mmHBM0_MC0_BIST_ADDRESS_RD_0 + pc_offset, cfg->start_addr);
		}

		sram_bist_sel = (sram_bist_1hot_sel_mc1 << (MC_CHANNELS_NUM * 2)) |
				sram_bist_1hot_sel_mc0;

		if (hbm_mcbist_run(hdev, hbm_dev, sram_bist_sel, pc_begin_idx, pc_end_idx))
			return MCBIST_ERR;

		rc |= mcbist_error_parser(hdev, hbm_dev, sram_bist_sel, err_bitmaps, pc_begin_idx,
			pc_end_idx);
	}

	/* Restore MC configurations for functional mode */
	for (mc = mc_begin_idx; mc < mc_end_idx; mc++) {
		mc_offset = hbm_dev * HBM_DEV_OFFSET + mc * MC_OFFSET;
		WREG32(mmHBM0_MC0_AXI_ADMAP_SID_0 + mc_offset, sid0);
		WREG32(mmHBM0_MC0_AXI_ADMAP_SID_1 + mc_offset, sid1);
		WREG32(mmHBM0_MC0_AXI_ADMAP_BG_0 + mc_offset, bg0);
		WREG32(mmHBM0_MC0_AXI_ADMAP_BG_1 + mc_offset, bg1);
		WREG32(mmHBM0_MC0_AXI_ADMAP_BA_0 + mc_offset, ba0);
		WREG32(mmHBM0_MC0_AXI_ADMAP_BA_1 + mc_offset, ba1);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_0 + mc_offset, row0);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_1 + mc_offset, row1);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_2 + mc_offset, row2);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_3 + mc_offset, row3);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_4 + mc_offset, row4);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_5 + mc_offset, row5);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_6 + mc_offset, row6);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_7 + mc_offset, row7);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_8 + mc_offset, row8);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_9 + mc_offset, row9);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_10 + mc_offset, row10);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_11 + mc_offset, row11);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_12 + mc_offset, row12);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_13 + mc_offset, row13);
		WREG32(mmHBM0_MC0_AXI_ADMAP_ROW_14 + mc_offset, row14);
		WREG32(mmHBM0_MC0_AXI_ADMAP_COL_0 + mc_offset, col0);
		WREG32(mmHBM0_MC0_AXI_ADMAP_COL_1 + mc_offset, col1);
		WREG32(mmHBM0_MC0_AXI_ADMAP_COL_2 + mc_offset, col2);
		WREG32(mmHBM0_MC0_AXI_ADMAP_COL_3 + mc_offset, col3);
		WREG32(mmHBM0_MC0_AXI_ADMAP_COL_4 + mc_offset, col4);
		WREG32(mmHBM0_MC0_AXI_GLBL_CFG + mc_offset, axi_glbl_cfg);
		WREG32(mmHBM0_MC0_SEI0_MASK_INTR_0 + mc_offset, sei0_intr_mask0);
		WREG32(mmHBM0_MC0_SEI0_MASK_INTR_1 + mc_offset, sei0_intr_mask1);
		WREG32(mmHBM0_MC0_SEI1_MASK_INTR_0 + mc_offset, sei1_intr_mask0);
		WREG32(mmHBM0_MC0_SEI1_MASK_INTR_1 + mc_offset, sei1_intr_mask1);
		WREG32(mmHBM0_MC0_BIST_EN + mc_offset, 0x0);
		WREG32(mmHBM0_MC0_SEQ_TRFC + mc_offset, trfc);
		WREG32(mmHBM0_MC0_SEQ_TREFI_TH + mc_offset, trefi_th);
		WREG32(mmHBM0_MC0_RSCH_RD_BG_SCORE + mc_offset, rd_bg_score);
		WREG32(mmHBM0_MC0_RSCH_RD_SID_SCORE + mc_offset, rd_sid_score);
		WREG32(mmHBM0_MC0_RSCH_RD_OPEN_PAGE_SCORE + mc_offset, rd_open_score);
		WREG32(mmHBM0_MC0_RSCH_WR_BG_SCORE + mc_offset, wr_bg_score);
		WREG32(mmHBM0_MC0_RSCH_WR_SID_SCORE + mc_offset, wr_sid_score);
		WREG32(mmHBM0_MC0_RSCH_WR_OPEN_PAGE_SCORE + mc_offset, wr_open_score);
		WREG32(mmHBM0_MC0_SEQ_REF_MODE + mc_offset, 0x0);
		/* delay to ensure at least one REF cmd + tRFC */
		udelay(5);
		WREG32(mmHBM0_MC0_SEQ_REF_MODE + mc_offset, ref_mode);

		if (cfg->dbi_mode != orig_dbi_mode)
			if (change_dbi(hdev, hbm_dev, mc, orig_dbi_mode, mc_ch_begin_idx,
				mc_ch_end_idx)) {
				hl_err(hdev,
					"HBM MCBIST FAILED - cannot restore DBI mode to %d - HBM%d MC%d\n",
					orig_dbi_mode, hbm_dev, mc);
				return MCBIST_ERR;
			}
	}

	return rc;
}

static int soft_lane_repair(struct hl_device *hdev, u32 hbm_dev, u16 repair_map)
{
	u32 reg_val, phy_ch_offset, mc_offset, ref_mode[HBM_MC_NUM],  polling_rc;
	u32 timeout = hdev->pldm ? PLDM_TIMEOUT : MCBIST_TIMEOUT_USEC;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	int mc, pc, phy_ch, dw_idx;

	if (!SLR_ENABLE) {
		hl_err(hdev, "SOFT lane repair is disabled\n");
		return status_fail;
	}

	/* Shut down Refresh */
	for (mc = 0; mc < HBM_MC_NUM; mc++) {
		mc_offset = hbm_dev * HBM_DEV_OFFSET + mc * MC_OFFSET;
		ref_mode[mc] = RREG32(mmHBM0_MC0_SEQ_REF_MODE + mc_offset);
		if (ref_mode[mc])
			WREG32(mmHBM0_MC0_SEQ_REF_MODE + mc_offset, 0);
	}

	/* Configure remapping in PHY */
	for (pc = 0; pc < HBM_PC_NUM; pc++) {
		if (!(repair_map & BIT(pc)))
			continue;

		hl_dbg(hdev, "Applying SOFT lane repair - HBM%d PC%d\n",
			hbm_dev, pc);

		phy_ch = phy_ch_mapping(pc / 2);
		phy_ch_offset   = phy_ch * PHY_CH_OFFSET;
		dw_idx = (pc % 2 == 0) ? 0 : 2;

		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_DWREMAP +
			      phy_ch_offset + dw_idx * DW_OFFSET,
			      hbm_cfg->slr_info[phy_ch].dw[dw_idx]);
		hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_DWREMAP +
			      phy_ch_offset + (dw_idx + 1) * DW_OFFSET,
			      hbm_cfg->slr_info[phy_ch].dw[dw_idx + 1]);
	}

	phy_pubmode(hdev, hbm_dev, pubmode_enter);

	/* Engage Lane Repair in PUB */
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_LANEREPAIRCTRL,
			HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRSOFTEN_MASK |
			HBM_PHY_MASTER_LANEREPAIRCTRL_DWREMAPMODE_MASK |
			HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRCLRSTATUS_MASK);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_LANEREPAIRCTRL,
			HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRRUN_MASK |
			HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRSOFTEN_MASK |
			HBM_PHY_MASTER_LANEREPAIRCTRL_DWREMAPMODE_MASK);
	hbm_phy_write(hdev, hbm_dev, mmHBM_PHY_MASTER_LANEREPAIRCTRL,
			HBM_PHY_MASTER_LANEREPAIRCTRL_LANEREPAIRSOFTEN_MASK |
			HBM_PHY_MASTER_LANEREPAIRCTRL_DWREMAPMODE_MASK);

	/* Poll for completion */
	polling_rc = phy_poll_timeout(
			hdev,
			hbm_dev,
			mmHBM_PHY_MASTER_LANEREPAIRSTATUS,
			reg_val,
			(reg_val & HBM_PHY_MASTER_LANEREPAIRSTATUS_LANEREPAIRDONE_MASK),
			10,
			timeout);

	if (polling_rc) {
		hl_err(hdev,
			"HBM%d SOFT LANE REPAIR FAILED - Timeout while polling on LaneRepairDone\n",
			hbm_dev);
		return status_fail;
	}

	phy_pubmode(hdev, hbm_dev, pubmode_exit);

	/* Restore Refresh */
	/* If original mode was REFSB (2), need to issue a REF command in order to reset the
	 * REFSB cycle in the HBM, Therefore, the procedure is:
	 *  1. go first to Refresh-All mode
	 *  2. spend ~8us in this mode to ensure a REF command has been issued
	 *  3. disable Refresh again to reset the Reset FSM
	 *  4. go to REFSB mode
	 */
	for (mc = 0; mc < HBM_MC_NUM; mc++) {
		mc_offset = hbm_dev * HBM_DEV_OFFSET + mc * MC_OFFSET;
		if (ref_mode[mc] > 0)
			WREG32(mmHBM0_MC0_SEQ_REF_MODE + mc_offset, 1);
		if (ref_mode[mc] > 1) {
			udelay(8);
			WREG32(mmHBM0_MC0_SEQ_REF_MODE + mc_offset, 0);
			WREG32(mmHBM0_MC0_SEQ_REF_MODE + mc_offset, ref_mode[mc]);
		}
	}

	return status_pass;
}

/* Return code:
 * 16b LSB - bitmap of PCs that had an repairable error
 * 16b MSB - bitmap of PCs that had an un-repairable error
 */
static u32 mcbist_analyzer(struct hl_device *hdev, u32 hbm_dev, u32 mcbist_rc,
			   struct pc_err_bitmap *err_bitmaps, struct mcbist_cfg *cfg)
{
	u32 phy_ch_offset, proposed_remap = 0x0, pc_current_remap = 0x0, combined_remap = 0x0,
	byte_pair, analyzer_rc = 0;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	int pc, mc, phy_ch, dw_idx, i, pc_begin_idx = 0, pc_end_idx = HBM_PC_NUM;
	u64 dq;
	u8  dm;

	if (mcbist_rc == 0x0)
		return status_pass;

	if (mcbist_rc == U32_MAX)
		return U32_MAX;

	if (cfg->mcbist_mode == single_pc) {
		pc_begin_idx = cfg->pc;
		pc_end_idx = pc_begin_idx + 1;
	}

	/* Analyze failing PCs */
	for (pc = pc_begin_idx; pc < pc_end_idx; pc++) {
		mc = (pc < 8) ? 0 : 1;
		phy_ch = phy_ch_mapping(pc / 2);
		dw_idx = (pc % 2 == 0) ? 0 : 2;

		/* No error in current PC */
		if (!(mcbist_rc & BIT(pc))) {
			/* proceed to next PC */
			continue;
		}

		/* CA parity error is un-repairable */
		if (mcbist_rc & (BIT(pc) << 16)) {
			analyzer_rc |= BIT(pc + MCBIST_FAIL_UNREPAIRABLE_SHIFT);
			hl_err(hdev,
				"HBM MCBIST IO AC test - Unrepairable FAIL on CA parity - HBM%d PC%02d (MC%d)\n",
				hbm_dev, pc, mc);
			return analyzer_rc;
		}

		/* If reached here, we suspect 1 or more defect DWORD lanes */
		hl_err(hdev, "HBM%d PC%d defect DQ lanes bitmap = 0x%llx\n",
			hbm_dev, pc, err_bitmaps[pc].dq);
		hl_err(hdev, "HBM%d PC%d defect DM lanes bitmap = 0x%x\n",
			hbm_dev, pc, err_bitmaps[pc].dm);

		/* Analyze whether lanes are repairable */
		/* Walk over error bitmap from LSB to MSB in 16bits (byte_pair) sections */
		for (i = 0, dq = err_bitmaps[pc].dq, dm = err_bitmaps[pc].dm;
		     i < 4; i++, dq >>= 16, dm >>= 2) {
			byte_pair = (dq & 0xFFFF) | ((dm & 0x3) << 16);

			if (hweight32(byte_pair) > 1) {
				analyzer_rc |= BIT(pc + MCBIST_FAIL_UNREPAIRABLE_SHIFT);
				hl_err(hdev,
					"HBM%d MCBIST IO AC test - Unrepairable FAIL: multiple lane errors in byte-pair %d - PC%d (MC%d)\n",
					hbm_dev, i, pc, mc);
				goto end;
			} else
				proposed_remap |= encode_remap_from_bitmap(byte_pair) << (i * 8);
		}

		/* Read existing remap info (per PC) */
		phy_ch_offset    = phy_ch * PHY_CH_OFFSET;
		pc_current_remap =
		   (hbm_phy_read(hdev, hbm_dev,
				 mmHBM_PHY_CHAN_CHAN0_DWORD0_DWREMAP +
				 phy_ch_offset + (dw_idx + 1) * DW_OFFSET) << 16) |
		    hbm_phy_read(hdev, hbm_dev,
				 mmHBM_PHY_CHAN_CHAN0_DWORD0_DWREMAP +
				 phy_ch_offset + dw_idx * DW_OFFSET);

		/* Check whether repair is consistent with existing remap */
		combined_remap = pc_current_remap & proposed_remap;
		if (is_valid_repair(pc_current_remap, proposed_remap)) {
			analyzer_rc |= BIT(pc + MCBIST_FAIL_REPAIRABLE_SHIFT);
			hl_err(hdev,
				"HBM%d MCBIST IO AC test - Repairable FAIL: remap code = 0x%08x - PC%d (MC%d)\n",
				hbm_dev, combined_remap, pc, mc);

			/* Save the required remap in global struct */
			hbm_cfg->slr_info[phy_ch].dw[dw_idx] = combined_remap & 0xFFFF;
			hbm_cfg->slr_info[phy_ch].dw[dw_idx + 1] = (combined_remap >> 16) & 0xFFFF;
		} else {
			analyzer_rc |= BIT(pc + MCBIST_FAIL_UNREPAIRABLE_SHIFT);
			hl_err(hdev,
				"HBM%d MCBIST IO AC test - Unrepairable FAIL: inconsistency with existing remap (0x%08x vs 0x%08x) - PC%d (MC %d)\n",
				hbm_dev, proposed_remap, pc_current_remap, pc, mc);
			return analyzer_rc;
		}
	}
end:
	return analyzer_rc;
}

static int hbm_mcbist_io_ac(struct hl_device *hdev, u32 hbm_dev, struct mcbist_cfg *cfg, int iter)
{
	static const u32 rc_unrepairable_mask = 0xffff0000;
	static const u32 rc_repairable_mask = 0x0000ffff;
	struct pc_err_bitmap err_bitmaps[HBM_PC_NUM];
	u32 io_ac_rc, mcbist_rc;

	mcbist_rc = hbm_mcbist(hdev, hbm_dev, err_bitmaps, cfg);
	io_ac_rc = mcbist_analyzer(hdev, hbm_dev, mcbist_rc, err_bitmaps, cfg);

	if (io_ac_rc == U32_MAX)
		return status_fail;

	if (io_ac_rc & rc_unrepairable_mask) {
		hl_err(hdev, "HBM%d MCBIST IO AC test (iter%d) FAILED Unrepairable\n",
			hbm_dev, iter);
		return status_fail;
	} else if (io_ac_rc & rc_repairable_mask) {
		hl_err(hdev,
			"HBM%d MCBIST IO AC test (iter%d) FAILED Repairable\n",
			hbm_dev, iter);
		io_ac_rc = soft_lane_repair(hdev, hbm_dev, io_ac_rc & rc_repairable_mask);
		if (io_ac_rc)
			return status_fail;
		/* Re-run MCBIST after SLR */
		mcbist_rc = hbm_mcbist(hdev, hbm_dev, err_bitmaps, cfg);
		io_ac_rc = mcbist_analyzer(hdev, hbm_dev, mcbist_rc, err_bitmaps, cfg);
		if (io_ac_rc) {
			hl_err(hdev, "HBM%d IO AC (iter%d) re-run after SOFT lane repair FAILED\n",
				hbm_dev, iter);
			return status_fail;
		}
		hl_dbg(hdev, "HBM%d IO AC test (iter%d) RE-RUN PASSED after SOFT lane repair\n",
					hbm_dev, iter);
		return status_pass;
	} else if (io_ac_rc != 0x0) {
		hl_err(hdev, "HBM%d IO AC test (iter%d) return code is invalid\n",
			hbm_dev, iter);
		return status_fail;
	}

	hl_dbg(hdev, "HBM%d IO AC test (iter%d) PASSED\n", hbm_dev, iter);
	return status_pass;
}

static u16 hbm_mcbist_full_scrub(struct hl_device *hdev, u32 hbm_dev)
{
	struct pc_err_bitmap err_bitmaps[HBM_PC_NUM];
	/*** DO NOT CHANGE ANY OF FOLLOWING PARAMS OTHERWISE SCRUBBING WILL NOT BE FULL! ***/
	struct mcbist_cfg mcbist_cfg = {
		.data_mode = MCBIST_BEAT_CNT,
		.poly = 0xd008,
		.seed = 0x1234,
		.loop_num = 1,
		/* block_size * rep_num = 1GB = PC capacity */
		.rep_num = 512,
		.block_size = 0x10000,
		.stop_on_err = 1,
		.sram_bist_sel = 0x0,
		.dbi_mode = 1,
		.mcbist_mode = parallel_all_pc,
		.start_addr = 0x0
	};
	u32 mcbist_rc;
	int pc;

	mcbist_rc = hbm_mcbist(hdev, hbm_dev, err_bitmaps, &mcbist_cfg);
	if (mcbist_rc == 0) {
		hl_dbg(hdev, "HBM MCBIST Full Scrubbing PASSED - HBM%d\n", hbm_dev);
		return status_pass;
	}

	for (pc = 0; pc < HBM_PC_NUM; pc++) {
		if (mcbist_rc & BIT(pc)) {
			hl_err(hdev,
				"HBM MCBIST Full Scrubbing failed for HBM%d PC%02d (rc = 0x%08x)\n",
				hbm_dev, pc, mcbist_rc);
		}
	}
	return status_fail;
}

static void mc_p1500_init(struct hl_device *hdev, int dev)
{
	u32 reg_val = 0, mc1_offset = dev * HBM_DEV_OFFSET + MC_OFFSET;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;

	RMWREG32(mmHBM0_MC0_P1500_TMN_CFG_1 + mc1_offset, 0xc,
		HBM0_MC0_P1500_TMN_CFG_1_TOVWSO_MASK);

	reg_val |= FIELD_PREP(HBM0_MC0_P1500_TMN_CFG2_TSF_MASK, 0x5);
	reg_val |= FIELD_PREP(HBM0_MC0_P1500_TMN_CFG2_THF_MASK, 0x5);
	reg_val |= FIELD_PREP(HBM0_MC0_P1500_TMN_CFG2_TSR_MASK, 0x5);
	reg_val |= FIELD_PREP(HBM0_MC0_P1500_TMN_CFG2_THR_MASK, 0x5);
	WREG32(mmHBM0_MC0_P1500_TMN_CFG2 + mc1_offset, reg_val);

	/* Config and enable WRCK from the MC */
	reg_val = hbm_cfg->ck_freq == HBM_PLL_1800 ? 0x1 : 0;
	RMWREG32(mmHBM0_MC0_P1500_TMN_CFG_1 + mc1_offset, reg_val,
		HBM0_MC0_P1500_TMN_CFG_1_WRCK_DIV_MASK);
	RMWREG32(mmHBM0_MC0_P1500_TMN_CFG_1 + mc1_offset, 0x1,
		HBM0_MC0_P1500_TMN_CFG_1_WRCK_EN_MASK);

	/* Assert WRSTn */
	RMWREG32(mmHBM0_MC0_P1500_TMN_CFG_1 + mc1_offset, 0x1,
		 HBM0_MC0_P1500_TMN_CFG_1_WRST_N_MASK);
}

static int mc_p1500_inst(struct hl_device *hdev, int dev, int ch,
			enum p1500_instruction inst, enum p1500_op op, u32 *wdr_ptr, int mbist_mode)
{
	/*
	 * Supports only wir-read and wir-write p1500 op
	 * Write instructions - WDR registers should be written prior to calling this function
	 * Read instructions - WDR registers should be read after calling this function
	 */
	u64 timeout = (hdev->pldm) ? (PLDM_TIMEOUT) : PHY_INIT_TIMEOUT_USEC;
	u32 mc1_offset = dev * HBM_DEV_OFFSET + MC_OFFSET, reg_val, wdr_ptr_idx = 0, reg_poll,
		sleep = (hdev->pldm) ? 10000 : 100, trig_bit, wdr_regs_count, wdr_regs_total;
	int rc, remaining_data, wdr_idx, wso_ch_offset, ch_idx;

	remaining_data = ieee_wdr_len_t[inst] - 1;

	/* In case read MBIST result Data size changes according to MBIST MODE */
	if (mbist_mode != mbist_off && op == wir_read)
		remaining_data = (mbist_mode == mbist_pbt_mode) ? MBIST_PBT_RESULT_SIZE :
			MBIST_LEGACY_RESULT_SIZE;

	wdr_regs_total = CEIL(remaining_data, BITS_PER_REG32);
	trig_bit = HBM0_MC0_P1500_INST_CMD_START_MASK;
	if (RREG32(mmHBM0_MC0_P1500_AUTO_TMP + mc1_offset) & HBM0_MC0_P1500_AUTO_TMP_ENABLE_MASK) {
		hl_err(hdev, "HBM%d IEEE15000 instruction FAILED - auto temperature is enabled\n",
			dev);
		return status_fail;
	}

	reg_val = 0;
	/* Configure WDR length */
	reg_val |= FIELD_PREP(HBM0_MC0_P1500_INST_CMD_WDR_LEN_MASK,
		MIN(ieee_wdr_len_t[inst], MC_P1500_WDR_SIZE_MAX) - 1);
	/* Configure WIR {ch[3:0], instruction[7:0]} */
	reg_val |= FIELD_PREP(HBM0_MC0_P1500_INST_CMD_WIR_MASK, inst | (ch << 8));
	/* Send WIR and write/read WDR */
	reg_val |= FIELD_PREP(HBM0_MC0_P1500_INST_CMD_OP_MASK, op);
	while (remaining_data > 0) {
		wdr_regs_count = CEIL(MIN(remaining_data, MC_P1500_WDR_SIZE_MAX), BITS_PER_REG32);
		if (op == wir_write) {
			for (wdr_idx = 0; wdr_idx < wdr_regs_count; wdr_idx++) {
				WREG32(mmHBM0_MC0_P1500_WSI_DATA_0 + mc1_offset +
				       wdr_idx * BYTE_OFFSET, *(wdr_ptr + wdr_ptr_idx));
				wdr_ptr_idx++;
			}
		}

		/* trigger IEEE1500 transaction */
		reg_val |= trig_bit;
		if (remaining_data <= MC_P1500_WDR_SIZE_MAX) {
			reg_val |= FIELD_PREP(HBM0_MC0_P1500_INST_CMD_LAST_DATA_MASK, 0x1);
			reg_val &= ~HBM0_MC0_P1500_INST_CMD_WDR_LEN_MASK;
			reg_val |= FIELD_PREP(HBM0_MC0_P1500_INST_CMD_WDR_LEN_MASK,
				ieee_wdr_len_t[inst] - 1);
		}
		WREG32(mmHBM0_MC0_P1500_INST_CMD + mc1_offset, reg_val);

		/* polling on start/pause bit in case less/more then 128 bit remained */
		rc = hl_poll_timeout(
			hdev,
			mc1_offset + mmHBM0_MC0_P1500_INST_CMD,
			reg_poll,
			((remaining_data <= MC_P1500_WDR_SIZE_MAX) ?
			((reg_poll & HBM0_MC0_P1500_INST_CMD_START_MASK) == 0x0) :
			((FIELD_GET(HBM0_MC0_P1500_INST_CMD_PAUSE_MASK, reg_poll)) == 0x1)),
			sleep,
			timeout);

		if (rc) {
			hl_err(hdev, "HBM%d IEEE15000 instruction code 0x%x failed\n",
				dev, inst);
			return status_fail;
		}

		if (rc) {
			hl_err(hdev, "HBM%d IEEE15000 instruction paused WITHOUT SPI interrupt\n",
				dev);
			return status_fail;
		}
		/* IEEE1500 WSO output ,128bit in 4 regs, are per channel
		 * 0/1/2/3-ch0, 4/5/6/7-ch1.... 28/29/30/31-ch7
		 * In order to arrange the data per channel in sequence the readout is in
		 * wdr_regs_count steps
		 */
		/* read command is up to wdr size output u32 regs */
		if (op == wir_read) {
			/* specific channel */
			if (ch != 0xf) {
				wso_ch_offset = ch * 4 * BYTE_OFFSET;
				for (wdr_idx = 0; wdr_idx < wdr_regs_count; wdr_idx++) {
					*(wdr_ptr + wdr_ptr_idx + wdr_idx) =
					RREG32(mmHBM0_MC0_P1500_WSO_DATA_0 + mc1_offset +
					wso_ch_offset + wdr_idx * BYTE_OFFSET);
				}
			}
			/* all channels */
			else if (ch == 0xf) {
				/* read values in order by ch: 0,1...7  */
				for (ch_idx = 0; ch_idx < HBM_PHY_CHANNELS_NUM; ch_idx++) {
					wso_ch_offset = ch_idx * 4 * BYTE_OFFSET;
					for (wdr_idx = 0; wdr_idx < wdr_regs_count; wdr_idx++) {
						*(wdr_ptr + wdr_ptr_idx + wdr_idx +
						ch_idx * wdr_regs_total) =
						RREG32(mmHBM0_MC0_P1500_WSO_DATA_0 +
							mc1_offset + wso_ch_offset +
							wdr_idx * BYTE_OFFSET);
					}
				}
			}
			wdr_ptr_idx += wdr_regs_count;
		}

		trig_bit = HBM0_MC0_P1500_INST_CMD_RESUME_MASK;
		remaining_data -= MC_P1500_WDR_SIZE_MAX;
	}
	return status_pass;
}

#ifdef HBM_TEMP
static void print_auto_temp(struct hl_device *hdev, u32 dev)
{
	u32 auto_temp_data, curr_temp, max_temp, min_temp,
	mc1_offset = dev * HBM_DEV_OFFSET + MC_OFFSET;

	/* MC0 is slave and MC1 is Master which holds the correct temp data */
	auto_temp_data = RREG32(mmHBM0_MC0_P1500_TEMP_REP + mc1_offset);

	curr_temp = (auto_temp_data & HBM0_MC0_P1500_TEMP_REP_AUTO_TEMP_READ_MASK) >>
	HBM0_MC0_P1500_TEMP_REP_AUTO_TEMP_READ_SHIFT;

	max_temp = (auto_temp_data & HBM0_MC0_P1500_TEMP_REP_AUTO_TEMP_MAX_MASK) >>
	HBM0_MC0_P1500_TEMP_REP_AUTO_TEMP_MAX_SHIFT;

	min_temp = (auto_temp_data  & HBM0_MC0_P1500_TEMP_REP_AUTO_TEMP_MIN_MASK) >>
	HBM0_MC0_P1500_TEMP_REP_AUTO_TEMP_MIN_SHIFT;

	hl_dbg(hdev, "Auto-Temp: current: %d, max:%d, min:%d\n",
	curr_temp, max_temp, min_temp);
}

static void clear_auto_temp_max_min(struct hl_device *hdev, u32 dev)
{
	u32 reg_val, mc1_offset = dev * HBM_DEV_OFFSET + MC_OFFSET;
	/* default auto temp max and min values by design*/
	const u32 def_min_val = 0x7F, def_max_val = 0;

	/* MC0 is slave and MC1 is Master which holds the correct temp data */
	reg_val = RREG32(mmHBM0_MC0_P1500_TEMP_REP & 0xf);
	reg_val |= FIELD_PREP(HBM0_MC0_P1500_TEMP_REP_AUTO_TEMP_MAX_MASK, def_max_val);
	reg_val |= FIELD_PREP(HBM0_MC0_P1500_TEMP_REP_AUTO_TEMP_MIN_MASK, def_min_val);
	WREG32(mmHBM0_MC0_P1500_TEMP_REP + mc1_offset, reg_val);

	hl_dbg(hdev, "Auto Temperature values in HBM%d have been restored to default\n", dev);
}

static int hbm_temperature(struct hl_device *hdev, int dev)
{
	int rc = status_pass;
	u32 reg_val;
	u8 temp_celsius;

	rc = mc_p1500_inst(hdev, dev, 0xf, ieee1500_temperature, wir_read, &reg_val, mbist_off);
	if (rc) {
		hl_err(hdev, "HBM%d temperature reading - IEEE1500 use FAILED\n", dev);
		return status_fail;
	}

	if (reg_val & P1500_WDR_TEMP_VALID_BIT_MASK) {
		hl_err(hdev, "HBM%d temperature reading is INVALID\n", dev);
		return status_fail;
	}

	temp_celsius = reg_val & 0x7F;
	if (temp_celsius == 0)
		hl_dbg(hdev, "HBM%d temperature is 0C or less\n", dev);
	else
		hl_dbg(hdev, "HBM%d temperature: %dC\n", dev, temp_celsius);

	return status_pass;
}
#endif

static bool hbm_mbist_is_repairable(struct hl_device *hdev, u32 dev, u32 fail_ra_cnt)
{
	/* check if mbist is repariable or not (0x11 means not repairable) */
	if (fail_ra_cnt == 0x11) {
		hl_err(hdev, "MBIST is unrepairable, more then 16 rows failed");
		return row_is_unrepairable;
	}
	hl_err(hdev, "MBIST is repairable");
	return row_is_repairable;
}
static int hbm_mbist_pbt_mode_parser(struct hl_device *hdev, u32 dev, u32 *result_raw,
	struct mbist_repair_vector *repair_vectors_arr, u32 pattern_idx, u32 *vec_cnt,
	bool *mbist_is_repairable)
{
	int rc = status_pass, ch_idx;
	u32 status_data, repair_vec, sid, pbt_run_fail, fail_ra_cnt, mbist_status, ra_master_bit,
		ra, pbt_finish, dq_position, err_count, ch_regs_count, vec_idx;

	ch_regs_count = CEIL(MBIST_PBT_RESULT_SIZE, BITS_PER_REG32);
	for (ch_idx = 0; ch_idx < HBM_PHY_CHANNELS_NUM; ch_idx++) {
		err_count = 0;

		status_data = FIELD_GET(MBIST_PBT_STATUS_BITS_MASK, *result_raw);
		mbist_status = FIELD_GET(MBIST_STATUS_BIT_MASK, status_data);
		fail_ra_cnt = FIELD_GET(MBIST_FAIL_RA_CNT_MASK, status_data);
		pbt_run_fail = FIELD_GET(MBIST_PBT_RUN_FAIL_MASK, status_data);
		sid = FIELD_GET(MBIST_SID_MASK, status_data);

		/*  Start parsing repair vectors */
		if (mbist_status || fail_ra_cnt || pbt_run_fail) {
			/* Maximum rows with repair vector defined */
			err_count = MIN(fail_ra_cnt, MBIST_MAX_REPAIR_VECTORS_NUM);
			hl_err(hdev, "\n %s%sHBM%d CH%d MBIST STATUS: 0x%x\n",
				SPACES5, SPACES10, dev, ch_idx, status_data);
			hl_err(hdev, "STATUS: 0x%x, FAIL RA CNT: 0x%x, PBT Run Fail: 0x%x, SID: 0x%x\n\n",
			mbist_status, fail_ra_cnt, pbt_run_fail, sid);

			*mbist_is_repairable = hbm_mbist_is_repairable(hdev, dev, fail_ra_cnt);
			rc |= status_fail;
		}

		if (err_count) {
			hl_dbg(hdev, "  repair vector : RA Master Bit%s   RA%sPBT Finish%sDQ Position%s\n",
				SPACES5, SPACES10, SPACES5, SPACES5);
			hl_dbg(hdev, "  ------------- : ------------------------------------------------------------\n");
		}

		for (vec_idx = 0; vec_idx < err_count; vec_idx++) {
			/* Each repair_vec data is stored in 2 consecutive regs 22/10 bit per reg */
			repair_vec = (u32)((*(result_raw + vec_idx)) >> MBIST_PBT_STATUS_BITS_SHIFT)
				| (u32)(*(result_raw + vec_idx + 1) << MBIST_PBT_DATA_BITS_SHIFT);
			ra_master_bit = FIELD_GET(MBIST_DATA_RA_MASTER_BIT_MASK, repair_vec);
			ra = FIELD_GET(MBIST_PBT_DATA_RA_MASK, repair_vec);
			pbt_finish = FIELD_GET(MBIST_DATA_PBT_FINISH_MASK, repair_vec);
			dq_position = FIELD_GET(MBIST_PBT_DATA_DQ_POSITION_MASK, repair_vec);

			hl_dbg(hdev, "%svec-%2d%s: %s0x%x%s 0x%03x%s 0x%x%s 0x%x\n",
				SPACES5, vec_idx, SPACES5, SPACES5, ra_master_bit, SPACES10,
				ra, SPACES10, pbt_finish, SPACES10, dq_position);

			/* update repair vecor array for row repair usage */
			repair_vectors_arr[*vec_cnt].ch = ch_idx;
			/* RA[13:0] - RA [13:1] = from vec, RA [0] = RA[1] */
			repair_vectors_arr[*vec_cnt].ra = ((ra << 1) | (ra & 0x1));
			/* RA[14]  = 0 / 1 if pattern is r14b / r14 */
			if (pattern_idx == 2 || pattern_idx == 3 || pattern_idx == 6 ||
			pattern_idx == 7) {
				repair_vectors_arr[*vec_cnt].ra |= 0x4000;
				}
			repair_vectors_arr[*vec_cnt].enable = 1;
			repair_vectors_arr[*vec_cnt].sid = sid;

			/* if[25] == 1 : DQ_pos=0, BA[2]=0, BA[3]= [28], BA[1:0]=[27:26]
			 *  if[29] == 1 : DQ_pos=0, BA[2]=1, BA[3]= [32], BA[1:0]=[31:30]
			 *  if[33] == 1 : DQ_pos=1, BA[2]=0, BA[3]= [36], BA[1:0]=[35:34]
			 *  if[37] == 1 : DQ_pos=1, BA[2]=1, BA[3]= [40], BA[1:0]=[39:38]
			 *  RB[4] = DQ_pos, RB[3:0] = BA[3:0]
			 */
			if (ra_master_bit) {
				ra = repair_vectors_arr[*vec_cnt].ra;
				if (dq_position & BIT(0)) {
					repair_vectors_arr[*vec_cnt].dq_position = 0;
					repair_vectors_arr[*vec_cnt].rb =
						(dq_position & GENMASK(2, 1)) |
						(FIELD_GET(BIT(3), dq_position) << 3);
					repair_vectors_arr[*vec_cnt].ra = ra;
					repair_vectors_arr[*vec_cnt].ch = ch_idx;
					repair_vectors_arr[*vec_cnt].enable = 1;
					repair_vectors_arr[*vec_cnt].sid = sid;
					(*vec_cnt)++;
				}
				if (dq_position & BIT(4)) {
					repair_vectors_arr[*vec_cnt].dq_position = 0;
					repair_vectors_arr[*vec_cnt].rb =
						(dq_position & GENMASK(6, 5)) |
						(FIELD_GET(BIT(7), dq_position) << 3) | BIT(2);
					repair_vectors_arr[*vec_cnt].ra = ra;
					repair_vectors_arr[*vec_cnt].ch = ch_idx;
					repair_vectors_arr[*vec_cnt].enable = 1;
					repair_vectors_arr[*vec_cnt].sid = sid;
					(*vec_cnt)++;
				}
				if (dq_position & BIT(8)) {
					repair_vectors_arr[*vec_cnt].dq_position = 1;
					repair_vectors_arr[*vec_cnt].rb =
						(dq_position & GENMASK(10, 9)) |
						(FIELD_GET(BIT(11), dq_position) << 3);
					repair_vectors_arr[*vec_cnt].ra = ra;
					repair_vectors_arr[*vec_cnt].ch = ch_idx;
					repair_vectors_arr[*vec_cnt].enable = 1;
					repair_vectors_arr[*vec_cnt].sid = sid;
					(*vec_cnt)++;
				}
				if (dq_position & BIT(12)) {
					repair_vectors_arr[*vec_cnt].dq_position = 1;
					repair_vectors_arr[*vec_cnt].rb =
						(dq_position & GENMASK(14, 13)) |
						(FIELD_GET(BIT(15), dq_position) << 3) | BIT(2);
					repair_vectors_arr[*vec_cnt].ra = ra;
					repair_vectors_arr[*vec_cnt].ch = ch_idx;
					repair_vectors_arr[*vec_cnt].enable = 1;
					repair_vectors_arr[*vec_cnt].sid = sid;
					(*vec_cnt)++;
				}
			}
			(*vec_cnt)++;
		}
		result_raw += ch_regs_count;
	}
	return rc;
}

static int hbm_mbist_legacy_mode_parser(struct hl_device *hdev, u32 dev, u32 *result_raw,
	struct mbist_repair_vector *repair_vectors_arr, u32 pattern_idx, u32 *vec_cnt,
	bool *mbist_is_repairable)
{
	int rc = status_pass, ch_idx, reg_idx, bit_idx;
	u32 status_data, repair_vec, sid, rdqs_fail, fail_ra_cnt, mbist_status, rb, ra, dq_position,
		err_count, ch_regs_count, vec_idx, error_cnt, ignore[3], ignore_idx, bit_pos;
	u8 result_bit_arr[BITS_PER_REG32 *
				CEIL(MBIST_LEGACY_RESULT_SIZE, BITS_PER_REG32)] = {0};

	ch_regs_count = CEIL(MBIST_LEGACY_RESULT_SIZE, BITS_PER_REG32);
	for (ch_idx = 0; ch_idx < HBM_PHY_CHANNELS_NUM; ch_idx++) {
		err_count = 0;

		status_data = FIELD_GET(MBIST_LEGACY_STATUS_BITS_MASK, *result_raw);
		mbist_status = FIELD_GET(MBIST_STATUS_BIT_MASK, status_data);
		fail_ra_cnt = FIELD_GET(MBIST_FAIL_RA_CNT_MASK, status_data);
		rdqs_fail = FIELD_GET(MBIST_LEGACY_RDQS_FAIL_MASK, status_data);

		/*  Start parsing repair vectors */
		if (mbist_status || fail_ra_cnt || rdqs_fail) {
			/* Maximum rows with repair vector defined */
			err_count = MIN(fail_ra_cnt, MBIST_MAX_REPAIR_VECTORS_NUM);
			hl_err(hdev, "\n %s%sHBM%d CH%d MBIST STATUS: 0x%x\n",
				SPACES5, SPACES10, dev, ch_idx, status_data);
			hl_err(hdev, "STATUS: 0x%x, FAIL RA CNT: 0x%x, RDQS Fail: 0x%x\n\n",
			mbist_status, fail_ra_cnt, rdqs_fail);

			*mbist_is_repairable = hbm_mbist_is_repairable(hdev, dev, fail_ra_cnt);
			rc |= status_fail;
		}

		if (err_count) {
			hl_dbg(hdev, "  repair vector : %sSID%s  RB%s  RA%s  DQ Position%s\n",
				SPACES5, SPACES10, SPACES10, SPACES5, SPACES5);
			hl_dbg(hdev, "  ------------- : ------------------------------------------------------------\n");
		}

		/* convert data for parsing from u32 regs to u32 bits array */
		for (reg_idx = 0; reg_idx < ch_regs_count; reg_idx++) {
			for (bit_idx = 0; bit_idx < BITS_PER_REG32;
				bit_idx++) {
				result_bit_arr[bit_idx + reg_idx *
				BITS_PER_REG32] = ((*(result_raw + reg_idx))
				& BIT(bit_idx)) >> (bit_idx);
			}
		}

		for (vec_idx = 0; vec_idx < err_count; vec_idx++) {
			repair_vec = 0;
			/* Each repair_vec data is stored in consecutive regs each vec is 23 bits */
			for (bit_idx = 0; bit_idx < MBIST_LEGACY_REPAIR_VECTOR_SIZE; bit_idx++)
				repair_vec |= (u32)(result_bit_arr[bit_idx +
				MBIST_LEGACY_STATUS_FIRST_BIT + vec_idx *
				MBIST_LEGACY_REPAIR_VECTOR_SIZE] << bit_idx);

			sid = FIELD_GET(MBIST_SID_MASK, repair_vec);
			rb = FIELD_GET(MBIST_LEGACY_DATA_RB_MASK, repair_vec);
			ra = FIELD_GET(MBIST_LEGACY_DATA_RA_MASK, repair_vec);
			dq_position = FIELD_GET(MBIST_LEGACY_DATA_DQ_POSITION_MASK, repair_vec);

			hl_dbg(hdev, "%svec-%2d%s: %s0x%x%s 0x%x%s 0x%03x%s    0x%x\n",
				SPACES5, vec_idx, SPACES5, SPACES5, sid, SPACES10,
				rb, SPACES10, ra, SPACES5, dq_position);

			/* update repair vecor array for row repair usage */
			repair_vectors_arr[*vec_cnt].ch = ch_idx;
			repair_vectors_arr[*vec_cnt].rb = rb;
			repair_vectors_arr[*vec_cnt].ra = ra;
			repair_vectors_arr[*vec_cnt].enable = 1;
			repair_vectors_arr[*vec_cnt].sid = sid;

			/* dq=0x1 -> dq_pos = 0 |  dq=0x10 -> dq_pos = 1 | dq=0x11 -> both 2 vecs */
			if (dq_position == 1)
				repair_vectors_arr[*vec_cnt].dq_position = 0;
			else if (dq_position == 2)
				repair_vectors_arr[*vec_cnt].dq_position = 1;
			else if (dq_position == 3) {
				repair_vectors_arr[*vec_cnt].dq_position = 0;
				(*vec_cnt)++;
				repair_vectors_arr[*vec_cnt].ch = ch_idx;
				repair_vectors_arr[*vec_cnt].rb = rb;
				repair_vectors_arr[*vec_cnt].ra = ra;
				repair_vectors_arr[*vec_cnt].enable = 1;
				repair_vectors_arr[*vec_cnt].sid = sid;
				repair_vectors_arr[*vec_cnt].dq_position = 1;
			}
			(*vec_cnt)++;
		}

		error_cnt = 0;
		/* error cnt data bit are 375:394 */
		for (bit_idx = 0; bit_idx < MBIST_LEGACY_ERROR_CNT_SIZE; bit_idx++) {
			error_cnt |= (result_bit_arr[bit_idx + MBIST_LEGACY_ERROR_CNT_FIRST_BIT] <<
			bit_idx);
		}

		ignore_idx = 0;
		ignore[0] = 0;
		ignore[1] = 0;
		ignore[2] = 0;
		/* error cnt data bit are 395:474 */
		for (bit_idx = 0; bit_idx < MBIST_LEGACY_IGNORE_SIZE; bit_idx++) {
			ignore_idx = bit_idx / 32;
			bit_pos = bit_idx % 32;
			ignore[ignore_idx] |= (result_bit_arr[bit_idx +	MBIST_LEGACY_IGNORE_FIRST_BIT] << bit_pos);
		}
		if (mbist_status || fail_ra_cnt || rdqs_fail) {
			hl_err(hdev, "HBM%d CH%d - ERROR_CNT: 0x%x, IGNORE: 0x%08x%08x%08x\n\n",
				dev, ch_idx, error_cnt, ignore[2], ignore[1], ignore[0]);
		}

		result_raw += ch_regs_count;
	}
	return rc;
}

static int hbm_mbist(struct hl_device *hdev, u32 dev,
	struct mbist_repair_vector *repair_vectors_arr, u32 *vec_cnt, bool *mbist_is_repairable)
{
	int rc = status_pass, mbist_mode, patterns_num;
	u32 pattern_idx, *wdr;
	u32 mbist_result[CEIL(MAX(MBIST_PBT_RESULT_SIZE, MBIST_LEGACY_RESULT_SIZE), BITS_PER_REG32)
		* HBM_PHY_CHANNELS_NUM] = {0};
	/* DO NOT CHANGE THE ORDER OF THE PATTERNS */
	u32 *pbt_patterns_arr[PBT_PATTERNS_NUM] = {s0_r14b_pbt_march, s0_r14b_pbt_scan,
		s0_r14_pbt_march, s0_r14_pbt_scan, s1_r14b_pbt_march, s1_r14b_pbt_scan,
		s1_r14_pbt_march, s1_r14_pbt_scan, s0_tsv, s1_tsv};
	u32 *legacy_patterns_arr[LEGACY_PATTERNS_NUM] = {s0_legacy_march, s0_legacy_scan,
		s1_legacy_march, s1_legacy_scan};

	mbist_mode = mbist_pbt_mode; //set mbist mode to PBT or LEGACY
	if (mbist_mode == mbist_pbt_mode)
		patterns_num = PBT_PATTERNS_NUM;
	else
		patterns_num = LEGACY_PATTERNS_NUM;

	/* Disable device id read after hbm reset */
	hbm_phy_rmw(hdev, dev, mmHBM_PHY_MASTER_MASTERCTRL, 0x0,
		HBM_PHY_MASTER_MASTERCTRL_DEVICEIDREADONRST_MASK);

	/* Run HBM MBIST for 8 patterns at PBT mode Scan & March per SID & RA */
	for (pattern_idx = 0; pattern_idx < patterns_num; pattern_idx++) {
		rc = hbm_reset(hdev, dev);
		mdelay(1000);

		/* TSV patterns (number 8 and 9) on PBT mode are Legacy mode */
		if (pattern_idx >= 8 && mbist_mode == mbist_pbt_mode)
			mbist_mode = mbist_legacy_mode;

		/* Run A7 IEEE1500 write instruction for MBIST configuration */
		wdr = hbm_mbist_config_pattern;
		rc = mc_p1500_inst(hdev, dev, 0xf, ieee1500_mbist_a7, wir_write, wdr, mbist_mode);
		if (rc) {
			hl_err(hdev, "HBM%d MBIST write A7 read instruction - IEEE1500 use FAILED\n",
				dev);
			return status_fail;
		}

		if (mbist_mode == mbist_pbt_mode || pattern_idx >= 8)
			wdr = pbt_patterns_arr[pattern_idx];
		else
			wdr = legacy_patterns_arr[pattern_idx];
		rc = mc_p1500_inst(hdev, dev, 0xf, ieee1500_mbist_a6, wir_write, wdr, mbist_mode);
		if (rc) {
			hl_err(hdev, "HBM%d MBIST A6 write instruction - IEEE1500 use FAILED\n",
				dev);
			return status_fail;
		}

		/* wait for MBIST to finish 2.5S for maximal len(pattern) */
		mdelay((mbist_mode == mbist_pbt_mode) ? 2500 : 2500 * 6);

		wdr = mbist_result;
		rc = mc_p1500_inst(hdev, dev, 0xf, ieee1500_mbist_a6, wir_read, wdr, mbist_mode);
		if (rc) {
			hl_err(hdev, "HBM%d MBIST A6 read instruction - IEEE1500 use FAILED\n",
				dev);
			return status_fail;
		}

		if (mbist_mode == mbist_pbt_mode)
			rc = hbm_mbist_pbt_mode_parser(hdev, dev, mbist_result, repair_vectors_arr,
				pattern_idx, vec_cnt, mbist_is_repairable);
		else
			rc = hbm_mbist_legacy_mode_parser(hdev, dev, mbist_result,
				repair_vectors_arr, pattern_idx, vec_cnt, mbist_is_repairable);

		/* check MBIST status */
		if (rc) {
			hl_err(hdev, "HBM%d MBIST pattern%d - %s FAILED\n",
			dev, pattern_idx, (mbist_mode == mbist_pbt_mode || pattern_idx >= 8) ?
			pbt_pattern_names[pattern_idx] : legacy_pattern_names[pattern_idx]);

			return status_fail;
		}
		hl_dbg(hdev, "HBM%d MBIST pattern%d - %s PASSED\n", dev, pattern_idx,
			(mbist_mode == mbist_pbt_mode || pattern_idx >= 8) ?
			pbt_pattern_names[pattern_idx] : legacy_pattern_names[pattern_idx]);
	}

	hl_dbg(hdev, "HBM%d MBIST PASSED\n", dev);
	return status_pass;
}

static int hbm_row_repair(struct hl_device *hdev, u32 dev,
	struct mbist_repair_vector *repair_vectors_arr, u32 *vec_cnt, bool *mbist_is_repairable)
{
	int rc = status_pass, row_idx;
	u32 repair_vector_reg, mc1_offset = dev * HBM_DEV_OFFSET + MC_OFFSET;

	if (!ROW_REPAIR_ENABLE) {
		hl_err(hdev, "row repair is disabled\n");
		return status_fail;
	}

	if (*mbist_is_repairable) {
		hl_err(hdev, "row repair can not be done -  unrepairable\n");
		return status_fail;
	}

	hl_err(hdev, "HBM%d MBIST FAILED but trying to do row repair\n", dev);

	for (row_idx = 0; row_idx < *vec_cnt; row_idx++) {
		repair_vector_reg = 0;
		repair_vector_reg |= repair_vectors_arr[row_idx].ra;
		repair_vector_reg |= (repair_vectors_arr[row_idx].rb << ROW_REPAIR_RB_SHIFT);
		repair_vector_reg |= (repair_vectors_arr[row_idx].dq_position <<
			ROW_REPAIR_DQ_POSITION_SHIFT);
		/* sid output from MBIST is 2 bits, taking only LSB */
		repair_vector_reg |= ((repair_vectors_arr[row_idx].sid & 0x1)
			<< ROW_REPAIR_SID_SHIFT);
		repair_vector_reg |= (repair_vectors_arr[row_idx].enable <<
			ROW_REPAIR_ENABLE_SHIFT);

		rc = mc_p1500_inst(hdev, dev, repair_vectors_arr[row_idx].ch, ieee1500_soft_repair,
			wir_write, &repair_vector_reg, mbist_off);
		if (rc) {
			hl_err(hdev, "HBM%d CH%d vector%d: 0x%x IEEE1500 soft repair FAILED\n",
				dev, repair_vectors_arr[row_idx].ch, row_idx, repair_vector_reg);
			return status_fail;
		}

		/* Each repair command followed by P1500 reset */
		RMWREG32(mmHBM0_MC0_P1500_TMN_CFG_1 + mc1_offset, 0x0,
			HBM0_MC0_P1500_TMN_CFG_1_WRST_N_MASK);
		ndelay(100);
		RMWREG32(mmHBM0_MC0_P1500_TMN_CFG_1 + mc1_offset, 0x1,
			HBM0_MC0_P1500_TMN_CFG_1_WRST_N_MASK);
	}

	return status_pass;
}

static void sw_training_print_eye(struct hl_device *hdev, u8 *mat_2d, int rows, int cols)
{
	int i, j, size = 0;
	char s[900];
	char symbol;

	hl_dbg(hdev, "Extracted eye:\n");

	for (i = 0; i < rows; i++) {
		size += snprintf(s + size, 900 - size, "|");
		for (j = 0; j < cols; j++) {
			if (mat_2d[i * rows + j])
				symbol = 'P';
			else
				symbol = ' ';
			size += snprintf(s + size, 900 - size, "%c", symbol);
		}
		size += snprintf(s + size, 900 - size, "|\n");
	}
	hl_dbg(hdev, "%s\n", s);
}

static void sw_training_set_hbm_vref(struct hl_device *hdev, u32 dev, enum hbm_vref vref_val)
{
	const int vref_mr = 15;
	int mc, ch;

	for (mc = 0; mc < HBM_MC_NUM; mc++) {
		for (ch = 0; ch < MC_CHANNELS_NUM; ch++)
			mc_mrs_cmd(hdev, dev, mc, ch, vref_mr, vref_val);
	}
}

static void sw_training_set_host_vref(struct hl_device *hdev, u32 dev, u8 vref_val)
{
	hbm_phy_rmw(hdev, dev, mmHBM_PHY_MASTER_VREFINGLOBAL_P0, vref_val,
		    HBM_PHY_MASTER_VREFINGLOBAL_P0_GLOBALVREFINDAC_MASK);
	/* VREF settling time is up to 2.5us */
	udelay(3);
}

static void sw_training_update_lcdls(struct hl_device *hdev, int dev, int ch, bool write_training)
{
	u32 reg_val;
	int dw;

	reg_val = (write_training) ? 0x3 : 0xc;

	for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
		/* 0->1 transition will load the relevant delay CSR into the relevant LCDL */
		hbm_phy_write(hdev, dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_DWLCDLCTRL +
			ch * PHY_CH_OFFSET +
			dw * DW_OFFSET, 0x0);
		hbm_phy_write(hdev, dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_DWLCDLCTRL +
			ch * PHY_CH_OFFSET +
			dw * DW_OFFSET, reg_val);
		hbm_phy_write(hdev, dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_DWLCDLCTRL +
			ch * PHY_CH_OFFSET +
			dw * DW_OFFSET, 0x0);
	}
}

static void sw_training_flush_lcdls(struct hl_device *hdev, int dev, bool write_training)
{
	int ch, dw;

	for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
		if (write_training) {
			phy_write_all_ch(hdev, dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_TXDQDLY_P0 +
							dw * DW_OFFSET, 0x0);
			/* TODO - WDQS should not be modified during training
			 *  check if can be removed
			 */
			phy_write_all_ch(hdev, dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_TXDQSDLY_P0 +
							dw * DW_OFFSET, 0x0);
		} else {
			phy_write_all_ch(hdev, dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKDLY_P0 +
							dw * DW_OFFSET, 0x0);
			phy_write_all_ch(hdev, dev, mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKNDLY_P0 +
							dw * DW_OFFSET, 0x0);
		}
	}
	for (ch = 0; ch <  HBM_PHY_CHANNELS_NUM; ch++)
		sw_training_update_lcdls(hdev, dev, ch, write_training);
}

/* @return (X,Y) point which represents the center of mass of the input 2D matrix
 * Theoreticaly, the COM point coordinates might not corresponds to a PASS value (numerical 1)
 * We will need to address this issue later. For now, assume PASS region is continuous
 */
static int sw_training_find_com(struct hl_device *hdev, u8 *mat_2d, int rows, int cols,
			struct point *center_of_mass)
{
	int i, j, rc = status_pass;
	u32 tmp = 0, total = 0;

	for (i = 0; i < rows; i++)
		for (j = 0; j < cols; j++)
			total += mat_2d[i * rows + j];
	if (total == 0) {
		hl_err(hdev, "sum of all matrix entries equal to 0. Cannot calculate COM point\n");
		return status_fail;
	}

	for (i = 0; i < rows; i++)
		for (j = 0; j < cols; j++)
			tmp += i * mat_2d[i * rows + j];
	center_of_mass->x_coord = tmp / total;

	tmp = 0;
	for (i = 0; i < rows; i++)
		for (j = 0; j < cols; j++)
			tmp += j * mat_2d[i * rows + j];
	center_of_mass->y_coord = tmp / total;

	if (mat_2d[center_of_mass->x_coord * rows + center_of_mass->y_coord])
		hl_err(hdev, "COM point produce MCBIST failure\n");

	return rc;
}

/* @return status_skip / status_fail / status_pass */
static int software_training(struct hl_device *hdev, int dev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	struct hbm_hw_training *training_cfg = &hbm_cfg->train_cfg;
	u32 lcdl_fine, lcdl_coarse, lcdl_step, lcdl_val, mcbist_rc = 0;
	u8 *mcbist_res_2d_tx[HBM_PHY_CHANNELS_NUM], *mcbist_res_2d_rx[HBM_PHY_CHANNELS_NUM],
		host_vref;
	struct pc_err_bitmap err_bitmaps[HBM_PC_NUM];
	int hbm_vref, dw, ch, rc = status_pass;
	struct point com_point = {0, 0};
	/* TODO - optimize MCBIST settings for SW training */
	struct mcbist_cfg mcbist_cfg = {
		.data_mode = MCBIST_FAST_55_AA,
		.poly = 0xd008,
		.seed = 0x1234,
		.loop_num = 1,
		.rep_num = 1,
		.block_size = 0x10000,
		.stop_on_err = 1,
		.sram_bist_sel = 0x0,
		.dbi_mode = 1,
		.mcbist_mode = parallel_all_pc,
		.start_addr = 1 << 26
	};
	const int hbm_vref_opt = 8,	/* Range: 0 - 7 */
		host_vref_opt = 127,	/* Range: 0x1 - 0x7f */
		host_vref_stride = 5,
		tx_dq_lcdl_opt = 127,	/* TxDqDly[7:6] units =  1UI, TxDqDly[4:0] units = UI/32 */
		rdqs_lcdl_opt = 32;	/* Range: 0x0 - 0x1f */

	/* Set all pointers to NULL in case we exit pre-maturely, kfree is called when only
	 * some of the pointers are valid
	 */
	memset(mcbist_res_2d_tx, 0, sizeof(u8 *) * HBM_PHY_CHANNELS_NUM);
	memset(mcbist_res_2d_rx, 0, sizeof(u8 *) * HBM_PHY_CHANNELS_NUM);

	/* Allocate array per channel. each array represent 2D matrix
	 * in which each row stores HBM VREF values and each column stores DQ TX LCDL values.
	 */
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		mcbist_res_2d_tx[ch] = kmalloc_array(tx_dq_lcdl_opt * hbm_vref_opt, sizeof(u8),
						     GFP_KERNEL);
		if (mcbist_res_2d_tx[ch] == NULL) {
			rc = status_fail;
			goto exit;
		}
		memset(mcbist_res_2d_tx[ch], 0x0, tx_dq_lcdl_opt * hbm_vref_opt);
	}

	/* flush write-data eye training results */
	sw_training_flush_lcdls(hdev, dev, true);

	/* Apply partial HW training for C/A, RL and read-data eye.
	 * TODO - consider removing HW training and rely on previous HW training
	 * (HW training is not skipped in case SW_TRAINING flag is set)
	 */
	training_cfg->train_ca = 1;
	training_cfg->train_rl = 1;
	training_cfg->train_rdeye = 1;
	training_cfg->train_wreye = 0;
	training_cfg->train_vref = 0;

	hl_dbg(hdev, "Applying partial HW training for read-data and SW training for write-data\n");
	rc = phy_hw_training(hdev, dev);
	if (rc) {
		hl_err(hdev, "Partial HW training failed (C/A, RL and read-data)\n");
		goto exit;
	}

	/* For each channel, apply 2D training for HBM-VREF and DQ transmit delay */
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		hl_dbg(hdev, "* 2D training for HBM-VREF and DQ transmit delay - HBM%d CH%d *",
			dev, ch);
		for (hbm_vref = 0; hbm_vref < hbm_vref_opt; hbm_vref++) {
			/* Note - HBM VREF is common for all channels*/
			sw_training_set_hbm_vref(hdev, dev, hbm_vref);

			/* set incremental LCDL values and run MCBIST as link tester*/
			for (lcdl_step = 0; lcdl_step < tx_dq_lcdl_opt; lcdl_step++) {
				lcdl_fine = lcdl_step % 32;
				lcdl_coarse = (lcdl_step - lcdl_fine) / 32;
				lcdl_val = lcdl_fine + (lcdl_coarse << 6);

				for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
					hbm_phy_write(hdev, dev,
						      mmHBM_PHY_CHAN_CHAN0_DWORD0_TXDQDLY_P0 +
						      ch * PHY_CH_OFFSET +
						      dw * DW_OFFSET, lcdl_val);
				}
				sw_training_update_lcdls(hdev, dev, ch, true);

				/* run MCBIST and focus ONLY on current channel results */
				/* TODO - edit hbm_mcbist to skip common cfg */
				mcbist_rc = hbm_mcbist(hdev, dev, err_bitmaps, &mcbist_cfg);
				if (mcbist_rc && GENMASK(ch * 2 + 1, ch * 2))
					mcbist_res_2d_tx[ch][hbm_vref * hbm_vref_opt +
							     lcdl_step] = 1;
				else
					mcbist_res_2d_tx[ch][hbm_vref * hbm_vref_opt +
							     lcdl_step] = 0;
			}
		}
		sw_training_print_eye(hdev, mcbist_res_2d_tx[ch], hbm_vref_opt, tx_dq_lcdl_opt);

		rc = sw_training_find_com(hdev, mcbist_res_2d_tx[ch], hbm_vref_opt, tx_dq_lcdl_opt,
					&com_point);
		if (rc)
			goto exit;

		/*  set optimal HBM-VREF and DQ TX LCDL */;
		hl_dbg(hdev, "HBM%d CH%d COM: (HBM-VREF = 0x%x, DQ TX LCDL = 0x%x\n",
			dev, ch, com_point.x_coord, com_point.y_coord);
		for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
			hbm_phy_write(hdev, dev,
				      mmHBM_PHY_CHAN_CHAN0_DWORD0_TXDQDLY_P0 +
				      ch * PHY_CH_OFFSET +
				      dw * DW_OFFSET, com_point.x_coord);

		}
		sw_training_update_lcdls(hdev, dev, ch, true);
		sw_training_set_hbm_vref(hdev, dev, com_point.y_coord);

		com_point.x_coord = 0;
		com_point.y_coord = 0;
	}

	/* At this point, we have per-channel observability on the write-data eye
	 * and the optimized values (COM) for the HBM-VREF and DQ TX LCDLs are set.
	 */

	/* Allocate 2D matrix per channel for read-data eye training */
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		mcbist_res_2d_rx[ch] = kmalloc_array(rdqs_lcdl_opt * host_vref_opt, sizeof(u8),
						     GFP_KERNEL);
		if (mcbist_res_2d_rx[ch] == NULL) {
			rc = status_fail;
			goto exit;
		}
		memset(mcbist_res_2d_rx[ch], 0x0, rdqs_lcdl_opt * host_vref_opt);
	}

	/* Flush read-data eye training results */
	sw_training_flush_lcdls(hdev, dev, false);

	/* For each channel, apply 2D training for Host-VREF and RDQS_t/c receive delay
	 * Note: RDQS_t & RDQS_c delays will be kept identical along the read-data eye training
	 */
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		hl_dbg(hdev, "* 2D training for Host-VREF and RDQS_t/c receive delay - HBM%d CH%d *",
			dev, ch);
		for (host_vref = 0; host_vref < host_vref_opt; host_vref += host_vref_stride) {
			/* Note - Host VREF is common for all channels*/
			sw_training_set_host_vref(hdev, dev, host_vref);

			/* set incremental LCDL values and run MCBIST as link tester*/
			for (lcdl_step = 0; lcdl_step < rdqs_lcdl_opt; lcdl_step++) {
				lcdl_val = lcdl_step;

				for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
					hbm_phy_write(hdev, dev,
						      mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKDLY_P0+
						      ch * PHY_CH_OFFSET +
						      dw * DW_OFFSET, lcdl_val);
					hbm_phy_write(hdev, dev,
						      mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKNDLY_P0+
						      ch * PHY_CH_OFFSET +
						      dw * DW_OFFSET, lcdl_val);
				}
				sw_training_update_lcdls(hdev, dev, ch, false);

				/* run MCBIST and focus ONLY on current channel results */
				mcbist_rc = hbm_mcbist(hdev, dev, err_bitmaps, &mcbist_cfg);
				if (mcbist_rc && GENMASK(ch * 2 + 1, ch * 2))
					mcbist_res_2d_rx[ch][host_vref * host_vref_opt +
							     lcdl_step] = 1;
				else
					mcbist_res_2d_rx[ch][host_vref * host_vref_opt +
							     lcdl_step] = 0;
			}
		}
		sw_training_print_eye(hdev, mcbist_res_2d_rx[ch], host_vref_opt, rdqs_lcdl_opt);

		rc = sw_training_find_com(hdev, mcbist_res_2d_rx[ch], host_vref_opt, rdqs_lcdl_opt,
					&com_point);
		if (rc)
			goto exit;

		/*  set optimal Host-VREF and RDQS_t/c LCDLs */;
		hl_dbg(hdev, "HBM%d CH%d COM: (HBM-VREF = 0x%x, DQ TX LCDL = 0x%x\n",
			dev, ch, com_point.x_coord, com_point.y_coord);
		for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
			hbm_phy_write(hdev, dev,
				      mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKDLY_P0+
				      ch * PHY_CH_OFFSET +
				      dw * DW_OFFSET, com_point.x_coord);
			hbm_phy_write(hdev, dev,
				      mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKNDLY_P0+
				      ch * PHY_CH_OFFSET +
				      dw * DW_OFFSET, com_point.x_coord);

		}
		sw_training_update_lcdls(hdev, dev, ch, false);
		sw_training_set_host_vref(hdev, dev, com_point.y_coord);

		com_point.x_coord = 0;
		com_point.y_coord = 0;
	}

exit:
	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		kfree(mcbist_res_2d_tx[ch]);
		kfree(mcbist_res_2d_rx[ch]);
	}

	return rc;
}

static void phy_set_read_latency(struct hl_device *hdev, int dev, u16 rd_lat)
{
	u32 reg_offset;
	int ch, dw;

	for (ch = 0; ch < HBM_PHY_CHANNELS_NUM; ch++) {
		for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
			reg_offset =  ch * PHY_CH_OFFSET + dw * DW_OFFSET;
			PHY_WREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_DFIMRL_P0 + reg_offset,
				rd_lat & 0x1f);
		}
	}
}

static int semi_auto_training(struct hl_device *hdev, int dev)
{
	u32 phy_vref, phy_ch, dw, offset, dev_avg_eye_ratio, sum_valid_vref_eyes = 0,
		weighted_sum_valid_vref_eyes = 0, sum_dev_eyes_ratios = 0, passing_regions_cnt = 0,
		opt_phy_vref, valid_vref_start = U32_MAX, valid_vref_end = U32_MAX,
		passing_region_size = 0, passing_region_max = 0, final_opt_vref = PHY_VREF_DEFAULT,
		hbm_vref, opt_hbm_vref = 0, max_hbm_vref_eye = 0, phy_vref_valid_ratio = 0;
	bool training_fail;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	struct hbm_hw_training *training_cfg = &hbm_cfg->train_cfg;
	int rc;

	/* Set initial DFIMRL */
	phy_set_read_latency(hdev, dev, 0xd);

	/* Enable CA training only */
	training_cfg->hbm_reset = 0;
	training_cfg->mrs = 0;
	training_cfg->train_ca = 1;
	training_cfg->train_rl = 0;
	training_cfg->train_rdeye = 0;
	training_cfg->train_vref = 0;
	training_cfg->train_wreye = 0;
	rc = phy_hw_training(hdev, dev);
	debug_print_training_status(hdev, dev);
	if (rc) {
		hl_err(hdev, "HBM%d CA training FAILED\n", dev);
		return rc;
	}

	/* For each PHY VREF, train read-eye only */
	training_cfg->train_ca = 0;
	training_cfg->train_rdeye = 1;
	for (phy_vref = 1; phy_vref <= PHY_VREF_OPTIONS; phy_vref++) {
		sw_training_set_host_vref(hdev, dev, phy_vref);

		training_fail = phy_hw_training(hdev, dev);
		training_fail |= hbm_phy_read(hdev, dev, mmHBM_PHY_MASTER_CHNSTATUS);
		if (training_fail || phy_vref == PHY_VREF_OPTIONS) {
			if (valid_vref_start != U32_MAX) {
				/* Passing region had ended. Calculate relevant parameters */
				valid_vref_end = phy_vref - 1;
				passing_region_size = valid_vref_end - valid_vref_start + 1;
				opt_phy_vref = weighted_sum_valid_vref_eyes / sum_valid_vref_eyes;
				passing_regions_cnt++;
				hl_dbg(hdev, "Passing region #%d: Range: (%d - %d), Optimal PHY VREF: (%d)\n",
					passing_regions_cnt, valid_vref_start,
					valid_vref_end, opt_phy_vref);
				if (passing_region_size > passing_region_max) {
					passing_region_max = passing_region_size;
					final_opt_vref = opt_phy_vref;
				}
				/* Initialize to reset values for next possible passing region */
				valid_vref_start = U32_MAX;
				weighted_sum_valid_vref_eyes = 0;
				sum_valid_vref_eyes = 0;
			}
			continue;
		}
		/* If we got here, read-eye training passed with current PHY VREF
		 * we sum RDQS eyes ratios for all DWORDS in the HBM device
		 */
		for (phy_ch = 0; phy_ch < HBM_PHY_CHANNELS_NUM; phy_ch++) {
			for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
				offset = phy_ch * PHY_CH_OFFSET + dw * DW_OFFSET;
				sum_dev_eyes_ratios += calc_eye_ratio_ui(
					PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKMINDLY +
						 offset),
					PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKMAXDLY +
						 offset))
					+ calc_eye_ratio_ui(
						PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKNMINDLY +
							 offset),
						PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_RXCLKNMAXDLY +
							 offset));
			}
		}

		/* Summation of 0 should be considered as training failure */
		if (sum_dev_eyes_ratios == 0)
			continue;

		dev_avg_eye_ratio = sum_dev_eyes_ratios / (HBM_PHY_CHANNELS_NUM *
			HBM_CHANNEL_DWORDS_NUM * RDQS_RISING_FALLING_EDGES_FACTOR);

		/* Set passing region start */
		if (valid_vref_start == U32_MAX)
			valid_vref_start = phy_vref;

		sum_valid_vref_eyes += dev_avg_eye_ratio;
		weighted_sum_valid_vref_eyes += phy_vref * dev_avg_eye_ratio;

		sum_dev_eyes_ratios = 0;
	}

	phy_vref_valid_ratio = (passing_region_max * 100 + PHY_VREF_OPTIONS/2) / PHY_VREF_OPTIONS;
	if (phy_vref_valid_ratio < PHY_VREF_RATIO_TH_PRCNT) {
		hl_err(hdev, "HBM%d PHY VREF passing regsion of %d%% doesn NOT meet TH\n",
			dev, phy_vref_valid_ratio);
		return status_fail;
	}

	sw_training_set_host_vref(hdev, dev, final_opt_vref);

	/* Train read eye again with optimal VREF */
	rc = phy_hw_training(hdev, dev);
	debug_print_training_status(hdev, dev);
	if (rc)
		return rc;

	/* Train read latency optimal PHY_VREF */
	training_cfg->train_rl = 1;
	training_cfg->train_rdeye = 0;
	rc = phy_hw_training(hdev, dev);
	debug_print_training_status(hdev, dev);
	if (rc)
		return rc;

	sum_valid_vref_eyes = 0;
	weighted_sum_valid_vref_eyes = 0;

	/* For each HBM VREF, train write-eye only */
	training_cfg->train_rl = 0;
	training_cfg->train_wreye = 1;
	for (hbm_vref = 0; hbm_vref < hbm_vref_steps; hbm_vref++) {
		sw_training_set_hbm_vref(hdev, dev, hbm_vref);

		training_fail = phy_hw_training(hdev, dev);
		training_fail |= hbm_phy_read(hdev, dev, mmHBM_PHY_MASTER_CHNSTATUS);
		if (training_fail)
			continue;

		/* If we got here, write-eye training passed with current HBM VREF
		 * we sum TX-DQ eyes ratio for all DWORDS in the HBM device
		 */
		for (phy_ch = 0; phy_ch < HBM_PHY_CHANNELS_NUM; phy_ch++) {
			for (dw = 0; dw < HBM_CHANNEL_DWORDS_NUM; dw++) {
				offset = phy_ch * PHY_CH_OFFSET + dw * DW_OFFSET;
				sum_dev_eyes_ratios += calc_eye_ratio_ui(
					PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_TXDQMINDLY +
						 offset),
					PHY_RREG(mmHBM_PHY_CHAN_CHAN0_DWORD0_TXDQMAXDLY +
						 offset));
			}
		}
		dev_avg_eye_ratio = sum_dev_eyes_ratios / (HBM_PHY_CHANNELS_NUM *
			HBM_CHANNEL_DWORDS_NUM);

		if (dev_avg_eye_ratio > max_hbm_vref_eye) {
			max_hbm_vref_eye = dev_avg_eye_ratio;
			opt_hbm_vref = hbm_vref;
		}

		sum_dev_eyes_ratios = 0;
	}

	if (max_hbm_vref_eye == 0) {
		hl_err(hdev, "HBM%d - FAILED to find a valid HBM VREF\n", dev);
		return status_fail;
	}
	sw_training_set_hbm_vref(hdev, dev, opt_hbm_vref);
	/* Train write eye again with optimal HBM VREF */
	rc = phy_hw_training(hdev, dev);
	debug_print_training_status(hdev, dev);
	if (rc)
		return rc;

	rc = phy_get_training_results(hdev, dev);
	hl_dbg(hdev, "Final PHY VREF: %d. Passing region size: %u\n",
		final_opt_vref, passing_region_max);
	hl_dbg(hdev, "Final HBM VREF: %d\n",
		opt_hbm_vref);

	return rc;
}

static int phy_training(struct hl_device *hdev, int dev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg = gaudi2->hbm_cfg;
	struct hbm_hw_training *training_cfg = &hbm_cfg->train_cfg;
	int rc;

	/* Common */
	hbm_cfg->train_cfg.cmd_repeat = 63;
	hbm_cfg->train_cfg.data_pattern = 0;
	hbm_phy_write(hdev, dev, mmHBM_PHY_MASTER_CMDREPEAT, training_cfg->cmd_repeat);
	hbm_phy_rmw(hdev, dev, mmHBM_PHY_MASTER_TRAINCFG, training_cfg->data_pattern,
		HBM_PHY_MASTER_TRAINCFG_TRAINDATAPATTERN_MASK);

	switch (training_cfg->training_type) {
	case train_type_hw_only:
		hbm_cfg->train_cfg.hbm_reset = 0;
		hbm_cfg->train_cfg.mrs = 0;
		hbm_cfg->train_cfg.train_ca = 1;
		hbm_cfg->train_cfg.train_rl = 1;
		hbm_cfg->train_cfg.train_rdeye = 1;
		hbm_cfg->train_cfg.train_vref = 1;
		hbm_cfg->train_cfg.train_wreye = 1;
		/* Increase vref settling time delay */
		hbm_phy_write(hdev, dev, mmHBM_PHY_MASTER_VREFSETTLINGTIME_P0, 0x1194);
		rc = phy_hw_training(hdev, dev);
		if (rc)
			hl_err(hdev, "HBM%d HW training FAILED\n", dev);
		break;

	case train_type_semi_auto:
		rc = semi_auto_training(hdev, dev);
		if (rc)
			hl_err(hdev, "HBM%d semi-auto training FAILED\n", dev);
		break;

	case train_type_sw:
		rc = software_training(hdev, dev);
		if (rc)
			hl_err(hdev, "HBM%d SW training FAILED\n", dev);
		break;

	case train_type_pldm:
		hl_info(hdev, "HBM%d - Skipping PHY training which is not supported by PLDM HBM model\n",
			 dev);
		rc = status_pass;
		break;

	default:
		hl_err(hdev, "Un-supported training type!\n");
		rc = status_fail;
	}

	return rc;
}

static void mc_assert_cke(struct hl_device *hdev, int dev)
{
	int mc_idx;
	u32 mc_offset, reg_val;

	for (mc_idx = 0; mc_idx < HBM_MC_NUM; mc_idx++) {
		mc_offset = dev * HBM_DEV_OFFSET + mc_idx * MC_OFFSET;

		reg_val = RREG32(mc_offset + mmHBM0_MC0_DFI_PHY_CFG_CH_0);
		reg_val |= HBM0_MC0_DFI_PHY_CFG_CH_DFI_TRIGGER_CKE_FLIP_MASK;
		WREG32(mc_offset + mmHBM0_MC0_DFI_PHY_CFG_CH_0, reg_val);
		WREG32(mc_offset + mmHBM0_MC0_DFI_PHY_CFG_CH_1, reg_val);
		WREG32(mc_offset + mmHBM0_MC0_DFI_PHY_CFG_CH_2, reg_val);
		WREG32(mc_offset + mmHBM0_MC0_DFI_PHY_CFG_CH_3, reg_val);
	}
}

#ifdef DEBUG
static void mc_reset(struct hl_device *hdev, int dev)
{
	int rst_idx;

	/* clear all PSOC RESET configuration space */
	for (rst_idx = 0; rst_idx <= 32; rst_idx++)
		WREG32(mmPSOC_RESET_CONF_PSOC_UNIT_RST + rst_idx * BYTE_OFFSET, 0x0);

	/* reset HBM */
	WREG32(mmPSOC_RESET_CONF_HBM_MC_UNIT_RST, BIT(dev));
	WREG32(mmPSOC_RESET_CONF_UNIT_RST_N, 0x1);
	udelay(1000);

	/* clear reset */
	WREG32(mmPSOC_RESET_CONF_UNIT_RST_N, 0x0);
	WREG32(mmPSOC_RESET_CONF_HBM_MC_UNIT_RST, 0x0);
}
#endif

int gaudi2_init_hbm(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct gaudi2_hbm *hbm_cfg;
	int dev, mc_idx, rc = 0;

	/* HBM initialization is done by u-boot */
	if ((hdev->fw_components & FW_TYPE_BOOT_CPU) && !hdev->fw_cfg_skip)
		return 0;

	if (!hdev->dram_enable)
		return 0;

	if (prop->num_functional_hbms == GAUDI2_HBM_NUM)
		gaudi2_config_fc_no_dram_binning(hdev);

	if (gaudi2->hw_cap_initialized & HW_CAP_DRAM)
		return 0;

	hl_dbg(hdev, "**** HBM subsystem init start ****\n");
	hl_dbg(hdev, "dram_enabled_mask = 0x%llx\n",
			prop->dram_enabled_mask);

	gaudi2->hbm_cfg = kmalloc(sizeof(struct gaudi2_hbm), GFP_KERNEL);
	if (!gaudi2->hbm_cfg)
		return -ENOMEM;

	hbm_cfg = gaudi2->hbm_cfg;

	/* Hard-coded HBM subsystem settings (on FW, some are inputs from EEPROM) */
	hbm_cfg->ck_freq = gaudi2->hbm_pll_freq;
	hbm_cfg->ecc_enable = 1;
	hbm_cfg->dbi_enable = 1;
	hbm_cfg->dfi_phy_update = 1;
	hbm_cfg->rmw_fwd_dis = 0;
	hbm_cfg->fast_zq_cal = 1;
	hbm_cfg->hbm_io_drive = drive_strength_12ma;

	hbm_cfg->train_cfg.training_type = (hdev->pldm) ? train_type_pldm : train_type_semi_auto;

	memset(hbm_cfg->slr_info, 0, HBM_PHY_CHANNELS_NUM * sizeof(struct hbm_ch_lane_remap_info));
	set_timing_params(hdev, hbm_cfg);

	for (dev = 0 ; dev < GAUDI2_HBM_NUM ; dev++) {
		/* Skip binned device/s */
		if (!test_bit(dev, (unsigned long *)&hdev->asic_prop.dram_enabled_mask)) {
			hl_dbg(hdev, "HBM%d is binned out\n", dev);
			continue;
		}

		mc_phy_config(hdev, dev);

		/* Assert dfi_init_start */
		for (mc_idx = 0; mc_idx < HBM_MC_NUM; mc_idx++)
			phy_init(hdev, dev, mc_idx, true);

		for (mc_idx = 1; mc_idx >= 0; mc_idx--) {
			if (poll_on_phy_init(hdev, dev, mc_idx))
				return status_fail;
			phy_init(hdev, dev, mc_idx, false);
			mc_assert_cke(hdev, dev);
		}

		rc = hbm_reset(hdev, dev) || hbm_mrs(hdev, dev);
		if (rc)
			return rc;

		if (hbm_cfg->ck_freq == HBM_PLL_800) {
			struct mbist_repair_vector *repair_vectors_arr = NULL;
			u32 vec_cnt = 0;
			bool mbist_is_repairable = 0;
			/* mult by 4  due to DQ and BA possible combinations */
			repair_vectors_arr = kmalloc_array(MBIST_MAX_REPAIR_VECTORS_NUM *
				4 * HBM_PHY_CHANNELS_NUM, sizeof(struct mbist_repair_vector),
				GFP_KERNEL);

			mc_p1500_init(hdev, dev);
			rc = hbm_mbist(hdev, dev, repair_vectors_arr, &vec_cnt,
				&mbist_is_repairable);
			if (rc) {
				hl_err(hdev, "HBM%d MBIST FAILED\n", dev);
				rc = hbm_row_repair(hdev, dev, repair_vectors_arr, &vec_cnt,
					&mbist_is_repairable);
				if (rc) {
					hl_err(hdev, "HBM%d IEEE1500 soft repair FAILED\n",
						dev);
					kfree(repair_vectors_arr);
					return rc;
				}
			}
			kfree(repair_vectors_arr);
			hl_info(hdev, "HBM MBIST is done - Must reset system to gain memory access");
			goto exit;
		}

		rc = phy_pre_training_tests(hdev, dev);
		if (rc) {
			hl_err(hdev, "HBM%d failure detected during pre-training tests\n",
				dev);
			return rc;
		}

		rc = phy_training(hdev, dev);
		if (rc) {
			hl_err(hdev, "HBM%d training FAILED\n", dev);
			return rc;
		}

		debug_print_pub_deviceid(hdev, dev);

		/* Memory controller BIST */
		if (!(hdev->pldm && PLDM_SKIP_MCBIST)) {
			int io_ac_iteration = 0;
			struct mcbist_cfg mcbist_cfg = {
				.data_mode = MCBIST_FAST_55_AA,
				.poly = 0xd008,
				.seed = 0x1234,
				.loop_num = 1,
				.rep_num = 1,
				.block_size = 0x800,	/* 64KB (8K per beat) */
				.stop_on_err = 1,
				.sram_bist_sel = 0xffff,
				.dbi_mode = 0,
				.mcbist_mode = parallel_all_pc,
				/* set bit RA[14] (mapped to addr bit 26) to guarantee
				 * toggling of pin R[6]
				 */
				.start_addr = 1 << 26
			};

			/* 1st run - Pattern 55/AA, no DBI (for maximal toggle rate of DQ),
			 * SRAM BIST for toggling DM[7:4]
			 */
			if (hbm_mcbist_io_ac(hdev, dev, &mcbist_cfg, ++io_ac_iteration))
				return status_fail;

			/* 2nd run - Pattern FF/00 (SSO), no DBI (for maximal toggle rate of DQ)
			 * SRAM BIST is OFF
			 */
			mcbist_cfg.sram_bist_sel = 0x0;
			mcbist_cfg.data_mode = MCBIST_FAST_00_FF;
			if (hbm_mcbist_io_ac(hdev, dev, &mcbist_cfg, ++io_ac_iteration))
				return status_fail;

			/* 3rd run - Pattern FF/00, with DBI (for coverage of DBI pins)
			 * SRAM BIST is OFF
			 */
			mcbist_cfg.dbi_mode = 1;
			if (hbm_mcbist_io_ac(hdev, dev, &mcbist_cfg, ++io_ac_iteration))
				return status_fail;
		}

		/* Full memory test + scrubbing */
		if (!(hdev->pldm && PLDM_SKIP_SCRUB)) {
			if (hbm_mcbist_full_scrub(hdev, dev))
				return status_fail;
		}

		mc_p1500_init(hdev, dev);

		if (AUTO_TEMP_EN)
			enable_auto_temp(hdev, dev);

		if (BU_THROT_EN)
			enable_traffic_throttling(hdev, dev);

exit:
		open_traffic(hdev, dev);
	}

	if (HBM_DUMP_BUS)
		hbm_dump_bus(hdev, 0, HCON0, 0);

	gaudi2->hw_cap_initialized |= HW_CAP_DRAM;

	return 0;
}
