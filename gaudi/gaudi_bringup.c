// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudiP.h"
#include "include/gaudi/gaudi_masks.h"
#include "include/gaudi/gaudi_fw_if.h"

#define GAUDI_UBOOT_FW_FILE	"habanalabs/gaudi/gaudi-u-boot.bin"

#define GAUDI_HBM_CENTER_CHANNEL	9
#define GAUDI_MAX_TEMP_THRESHOLD	85

#define STATIC_DLL_ACC			0x84
#define STATIC_DLL_AWORD		0x4B
#define STATIC_DLL_DWORD_RD_RISE	0x49
#define STATIC_DLL_DWORD_RD_FALL	0x49
#define STATIC_DLL_DWORD_WR		0x4E

#define GAUDI_HBM_CFG_TIMEOUT		1000000 /* 1s */
#define GAUDI_HBM_ECC_CFG_TIMEOUT	(GAUDI_HBM_CFG_TIMEOUT * 2)
#define GAUDI_PLDM_HBM_CFG_TIMEOUT	10000000 /* 10s */
#define GAUDI_PLDM_HBM_ECC_CFG_TIMEOUT	(GAUDI_PLDM_HBM_CFG_TIMEOUT * 2)

#define ECC_SCRUBBING_TIMEOUT_US	1000000 /* 1s */

#define EXTEST_WDR_PTRNS_NUM		10
#define EXTEST_WDR_REG_NUM		7
#define EXTEST_WRITE_INST		1
#define EXTEST_READ_INST		0

enum hl_phy_state {
	phy_state_bypass,
	phy_state_dword,
	phy_state_initial,
	phy_state_aword
};

enum dram_state {
	dram_state_training,
	dram_state_bypass
};

enum cke_state {
	cke_state_low,
	cke_state_high
};

enum mc_state {
	mc_state_bist,
	mc_state_normal
};

enum loopback_state {
	loopback_state_initial,
	loopback_state_check
};

enum bist_trigger {
	bist_trigger_falling,
	bist_trigger_rising,
	bist_trigger_edge
};

enum mc_bist_mode {
	mc_bist_mode_once,
	mc_bist_mode_overn
};

enum hbm_mode {
	hbm_mode_lfsr,
	hbm_mode_register
};

enum ieee1500_instruction {
	ieee1500_bypass  = 0x00,
	ieee1500_extest_rx = 0x01,
	ieee1500_extest_tx = 0x02,
	ieee1500_intest_rx = 0x03,
	ieee1500_intest_tx = 0x04,
	ieee1500_hbm_reset = 0x05,
	ieee1500_mbist = 0x06,
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
	ieee1500_hard_lane_repair = 0x13
};

static u32 hbm_ch_mapping[8] = {
	0,
	1,
	4,
	5,
	2,
	3,
	6,
	7
};

static u32 hbm_golden_registers[16 * GAUDI_HBM_CHANNELS] = {
	/* ch 0 */
	0xA78E9D39,
	0x504FACAA,
	0x504FACAA,
	0xAC80D125,
	0x70545889,
	0x64A97E07,
	0xF7437341,
	0xA28EBFF5,
	0xE3307AD3,
	0x1F097DE3,
	0x9F0EF987,
	0x4A3C56E7,
	0xA2CD139C,
	0x48BB6953,
	0xD9F398D5,
	0x3B445A37,
	/* ch 1 */
	0x578E9d39,
	0x504FACAA,
	0x2F6B85DC,
	0xAC80D125,
	0x70545889,
	0x64A97E07,
	0xF7437341,
	0xA28EBFF5,
	0xE3307AD3,
	0x1F097DE3,
	0x9F0EF987,
	0x4A3C56E7,
	0xA2CD139C,
	0x48BB6953,
	0xD9F398D5,
	0x34445A37,
	/* ch 2 */
	0xAC80D125,
	0x70545889,
	0x64A97E07,
	0xF7437341,
	0xA28EBFF5,
	0xE3307AD3,
	0x1F097DE3,
	0x9F0EF987,
	0x4A3C56E7,
	0xA2CD139C,
	0x48BB6953,
	0xD9F398D5,
	0x3B445A37,
	0x578E9d39,
	0x504FACAA,
	0x2F6B85DC,
	/* ch 3 */
	0x5C80D125,
	0x70545889,
	0x64A97E07,
	0xF7437341,
	0xA28EBFF5,
	0xE3307AD3,
	0x1F097DE3,
	0x9F0EF987,
	0x4A3C56E7,
	0xA2CD139C,
	0x48BB6953,
	0xD9F398D5,
	0x3B445A37,
	0x578E9d39,
	0x504FACAA,
	0x2F6B85DC,
	/* ch 4 */
	0xF7437341,
	0xA28EBFF5,
	0xE3307AD3,
	0x1F097DE3,
	0x9F0EF987,
	0x4A3C56E7,
	0xA2CD139C,
	0x48BB6953,
	0xD9F398D5,
	0x3B445A37,
	0x578E9d39,
	0x504FACAA,
	0x2F6B85DC,
	0xAC80D125,
	0x70545889,
	0x64A97E07,
	/* ch 5 */
	0x17437341,
	0xA28EBFF5,
	0xE3307AD3,
	0x1F097DE3,
	0x9F0EF987,
	0x4A3C56E7,
	0xA2CD139C,
	0x48BB6953,
	0xD9F398D5,
	0x3B445A37,
	0x578E9d39,
	0x504FACAA,
	0x2F6B85DC,
	0xAC80D125,
	0x70545889,
	0x64A97E07,
	/* ch 6 */
	0xFF097DE3,
	0x9F0EF987,
	0x4A3C56E7,
	0xA2CD139C,
	0x48BB6953,
	0xD9F398D5,
	0x3B445A37,
	0x578E9d39,
	0x504FACAA,
	0x2F6B85DC,
	0xAC80D125,
	0x70545889,
	0x64A97E07,
	0xF7437341,
	0xA28EBFF5,
	0xE3307AD3,
	/* ch 7 */
	0x1F097DE3,
	0x9F0EF987,
	0x4A3C56E7,
	0xA2CD139C,
	0x48BB6953,
	0xD9F398D5,
	0x3B445A37,
	0x578E9d39,
	0x504FACAA,
	0x2F6B85DC,
	0xAC80D125,
	0x70545889,
	0x64A97E07,
	0xF7437341,
	0xA28EBFF5,
	0xE3307AD3
};

static u32 ieee_wdr_len_t[20] = {
	1, /* BYPASS */
	215, /* EXTEST_RX */
	215, /* EXTEST_TX */
	0, /* INTEST_RX */
	0, /* INTEST_TX */
	1, /* HBM_RESET */
	375, /* MBIST */
	21, /* SOFT_REPAIR */
	21, /* HARD_REPAIR */
	320, /* DWORD_MISR */
	30, /* AWORD_MISR */
	1, /* CHANNEL_ID */
	72, /* MISR_MASK */
	8, /* AWORD_MISR_CONFIG */
	82, /* DEVICE_ID */
	8, /* TEMPERATURE */
	128, /* MODE_REGISTER_DUMP_SET */
	175, /* READ_LFSR_COMPARE_STICKY */
	72, /* SOFT_LANE_REPAIR */
	72 /* HARD_LANE_REPAIR */
};

static u32 acc[GAUDI_HBM_CHANNELS][5];
static u32 aword[GAUDI_HBM_CHANNELS][4];
static u32 dword_wr[GAUDI_HBM_CHANNELS][4][4];
static u32 dword_rd[GAUDI_HBM_CHANNELS][4][4];

static u32 extest_wdr_pattern[EXTEST_WDR_PTRNS_NUM * EXTEST_WDR_REG_NUM] = {
	/* pattern 0 */
	0x55555555,
	0x55555555,
	0x55555555,
	0x55555555,
	0x55555555,
	0x55555555,
	0x00555555,
	/* pattern 1 */
	0x26626666,
	0x66666666,
	0x66662662,
	0x66666666,
	0x66626626,
	0x66266666,
	0x00666662,
	/* pattern 2 */
	0x387C7878,
	0x78787878,
	0x7878387C,
	0x78787878,
	0x787C7838,
	0x78387878,
	0x0078787C,
	/* pattern 3 */
	0x3F847F80,
	0x7F807F80,
	0x7F803F84,
	0x7F807F80,
	0x7F847FC0,
	0x7FC07F80,
	0x00007F84,
	/* pattern 4 */
	0x3FFB8000,
	0x7FFF8000,
	0x7FFFC004,
	0x7FFF8000,
	0x7FFB8040,
	0x7FBF8000,
	0x007F8004,
	/* pattern 5 */
	0xC0040000,
	0x7FFFFFFF,
	0x80004004,
	0x7FFF7FFF,
	0x80040040,
	0x7FBFFFFF,
	0x00000004,
	/* pattern 6 */
	0x40040000,
	0x80000000,
	0xFFFFBFFB,
	0x7FFF7FFF,
	0x00040040,
	0x80400000,
	0x007FFFFB,
	/* pattern 7 */
	0x40040000,
	0x00000000,
	0x00004004,
	0x80008000,
	0xFFFBFFBF,
	0xFFBFFFFF,
	0x007FFFFB,
	/* pattern 8 */
	0x40040000,
	0x00000000,
	0x00004004,
	0x00008000,
	0x00040040,
	0x00400000,
	0x00000004,
	/* pattern 9 */
	0xBFFBFFFF,
	0xFFFFFFFF,
	0xFFFFBFFB,
	0xFFFF7FFF,
	0xFFFBFFBF,
	0xFFBFFFFF,
	0x007FFFFB
};

static u32 extest_wdr_rx_mask[EXTEST_WDR_REG_NUM] = {
	0x9FFFFFFF,
	0xFFFFFDFF,
	0xFDFF9FFF,
	0xFFF7FFFF,
	0xDFF9FFFF,
	0xFFFFFFFF,
	0x000FDFF9
};

static u32 extest_wdr_tx_mask[EXTEST_WDR_REG_NUM] = {
	0xFFF9FFFF,
	0xFFFFFFFF,
	0xFFFFFFF9,
	0xFFF80000,
	0xFFFFFF9F,
	0xFF9FFFFF,
	0x000FFFFF
};

static void ieee1500_inst(struct hl_device *hdev, int device, u32 ch,
				enum ieee1500_instruction instruction,
				bool write)
{
	/*
	 * Write instructions - WDR registers (0x9100 - 0x9188) should be
	 * written prior to calling this function.
	 * Read instructions - WDR registers (0x9800 - 0x9F88) should be read
	 * after calling this function.
	 */

	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	u32 wir, op, status, err, timeout;

	if (hdev->pldm)
		timeout = GAUDI_PLDM_HBM_CFG_TIMEOUT;
	else
		timeout = GAUDI_HBM_CFG_TIMEOUT;

	/* Configure WDR length */
	WREG32(base + 0x9020, ieee_wdr_len_t[instruction] - 1);

	/*
	 * Configure WIR
	 * {ch, instruction} - ch may be 0xF to indicate ALL channels
	 */
	wir = (instruction & 0xFF) | ((ch & 0xF) << 8);
	WREG32(base + 0x9024, wir);

	/* Send WIR and write/read WDR */
	op = write ? 0x6 : 0x5;
	WREG32(base + 0x9028, op);

	/* Poll for MLB interrupt */
	err = hl_poll_timeout(
		hdev,
		base + 0x9034,
		status,
		status == op,
		1000,
		timeout);

	if (err)
		hl_err(hdev,
		       "Poll for MLB interrupt failed for HBM device: %d, ch %d, status: %d\n",
		       device, ch, status);

	/* Clear MLB interrupts */
	WREG32(base + 0x9034, 0);
}

static int extest_rx(struct hl_device *hdev, int device)
{
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	u32 received, expected, mask, addr, idx;
	int pat, ch, i, err = 0;

	hl_dbg(hdev, "%s\n", __func__);

	/* Enable all outputs, except for HBM TX-only signals */
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		WREG32(base + ch * 0x1000 + 0x920, 0x60000000);
		WREG32(base + ch * 0x1000 + 0x924, 0x00000200);
		WREG32(base + ch * 0x1000 + 0x928, 0x02006000);
		WREG32(base + ch * 0x1000 + 0x92c, 0x00080000);
		WREG32(base + ch * 0x1000 + 0x930, 0x20060000);
		WREG32(base + ch * 0x1000 + 0x934, 0x00000000);
		WREG32(base + ch * 0x1000 + 0x938, 0x00002006);
	}

	/* Patterns loop */
	for (pat = 0 ; pat < 16; pat++) {
		/* Configure output values */
		if (pat <= 7) {
			/* Configure all channels to same pattern */
			for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
				for (i = 0; i < EXTEST_WDR_REG_NUM; i++) {
					addr = base + ch * 0x1000 + 0x940 +
									(i * 4);
					idx = pat * EXTEST_WDR_REG_NUM + i;
					WREG32(addr, extest_wdr_pattern[idx]);
				}
		} else {
			/* Configure all channels to Pattern 8 */
			for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
				for (i = 0; i < EXTEST_WDR_REG_NUM; i++) {
					addr = base + ch * 0x1000 + 0x940 +
									(i * 4);
					idx = 8 * EXTEST_WDR_REG_NUM + i;
					WREG32(addr, extest_wdr_pattern[idx]);
				}
			/* Reconfigure a selected channel to Pattern 9 */
			for (i = 0; i < EXTEST_WDR_REG_NUM; i++) {
				addr = base + (pat - 8) * 0x1000 + 0x940 +
									(i * 4);
				idx = 9 * EXTEST_WDR_REG_NUM + i;
				WREG32(addr, extest_wdr_pattern[idx]);
			}
		}

		for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
			/* Perform EXTEST_RX command */
			ieee1500_inst(hdev, device, hbm_ch_mapping[ch],
					ieee1500_extest_rx, EXTEST_READ_INST);

			/* Compare results */
			for (i = 0; i < EXTEST_WDR_REG_NUM; i++) {
				if (pat <= 7)
					idx = pat * EXTEST_WDR_REG_NUM + i;
				else if (ch == (pat - 8))
					idx = 9 * EXTEST_WDR_REG_NUM + i;
				else
					idx = 8 * EXTEST_WDR_REG_NUM + i;

				expected = extest_wdr_pattern[idx];

				addr = base +
					(GAUDI_HBM_CENTER_CHANNEL * 0x1000) +
					0x800 + (hbm_ch_mapping[ch] * 0x100) +
					(i * 4);
				received = RREG32(addr);

				mask = extest_wdr_rx_mask[i];

				if ((received & mask) != (expected & mask)) {
					err++;
					hl_dbg(hdev,
						"mismatch: pattern %d, ch %d, WDR[%02d:%02d], expected 0x%x, received 0x%x, mask 0x%x\n",
						pat, ch, i * 32 + 31, i * 32,
						expected, received, mask);
				}
			}
		}
	}

	return err;
}

static int extest_tx(struct hl_device *hdev, int device)
{
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	u32 received, expected, mask, addr, idx;
	int pat, ch, i, err = 0;

	hl_dbg(hdev, "%s\n", __func__);

	/* Disable all outputs */
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
		for (i = 0 ; i < EXTEST_WDR_REG_NUM ; i++)
			WREG32(base + ch * 0x1000 + 0x920 + (i * 4),
					0xFFFFFFFF);

	/*
	 * Patterns 0 - 7
	 */

	/* Patterns loop */
	for (pat = 0 ; pat < 8; pat++) {
		/* Configure WDR write values */
		for (i = 0; i < EXTEST_WDR_REG_NUM; i++) {
			addr = base + (GAUDI_HBM_CENTER_CHANNEL * 0x1000) +
					0x100 + (i*4);
			idx = pat * EXTEST_WDR_REG_NUM + i;
			WREG32(addr, extest_wdr_pattern[idx]);
		}

		/* Perform EXTEST_TX command to all channels */
		ieee1500_inst(hdev, device, 0xF, ieee1500_extest_tx,
				EXTEST_WRITE_INST);

		/* Compare results */
		for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
			for (i = 0; i < EXTEST_WDR_REG_NUM; i++) {
				if (pat <= 7)
					idx = pat * EXTEST_WDR_REG_NUM + i;
				else if (ch == (pat - 8))
					idx = 9 * EXTEST_WDR_REG_NUM + i;
				else
					idx = 8 * EXTEST_WDR_REG_NUM + i;

				expected = extest_wdr_pattern[idx];

				received = RREG32(base + (ch * 0x1000) + 0x960
								+ (i * 4));

				mask = extest_wdr_tx_mask[i];

				if ((received & mask) != (expected & mask)) {
					err++;
					hl_err(hdev,
					       "mismatch: pattern %d, ch %d, WDR[%02d:%02d], expected 0x%x, received 0x%x, mask 0x%x\n",
					       pat, ch, i * 32 + 31, i * 32,
					       expected, received, mask);
				}
			}

			/* Special case - pins TEMP[2:0] + CATTRIP */
			if (ch == 0) {
				received = RREG32(base + 0x8000 + 0x04c);
				if (((received >> 2) & 1) !=
						((expected  >>  20) & 1)) {
					err++;
					hl_dbg(hdev,
						"mismatch: pattern %d, ch %d, WDR[212] (TEMP[2]), expected 0x%x, received 0x%x\n",
						pat, ch, ((expected >> 20) & 1),
						((received >> 2) & 1));
				}

				if (((received >> 1) & 1) !=
						((expected >> 21) & 1)) {
					err++;
					hl_dbg(hdev,
						"mismatch: pattern %d, ch %d, WDR[213] (TEMP[1]), expected 0x%x, received 0x%x\n",
						pat, ch, ((expected >> 21) & 1),
						((received >> 1) & 1));
				}

				if (((received >> 0) & 1) !=
						((expected  >>  22) & 1)) {
					err++;
					hl_dbg(hdev,
						"mismatch: pattern %d, ch %d, WDR[214] (TEMP[0]), expected 0x%x, received 0x%x\n",
						pat, ch, ((expected >> 22) & 1),
						((received >> 0) & 1));
				}
			}

			if (ch == 1) {
				received = RREG32(base + 0x8000 + 0x04c);
				if (((received >> 3) & 1) !=
						((expected >> 20) & 1)) {
					err++;
					hl_dbg(hdev,
						"mismatch: pattern %d, ch %d, WDR[212] (CATTRIP), expected 0x%x, received 0x%x\n",
						pat, ch, ((expected >> 20) & 1),
						((received >> 3) & 1));
				}
			}
		}
	}

	/*
	 * Patterns 8 - 15
	 */

	/* Patterns loop */
	for (pat = 8 ; pat < 15; pat++) {
		for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
			/* Configure WDR write values */
			for (i = 0; i < EXTEST_WDR_REG_NUM; i++) {
				addr = base +
					(GAUDI_HBM_CENTER_CHANNEL * 0x1000) +
					0x100 + (i*4);
				if (ch == (pat-8))
					idx = 9 * EXTEST_WDR_REG_NUM + i;
				else
					idx = 8 * EXTEST_WDR_REG_NUM + i;

				WREG32(addr, extest_wdr_pattern[idx]);
			}

			/* Perform EXTEST_TX command to the selected channel */
			ieee1500_inst(hdev, device, hbm_ch_mapping[ch],
					ieee1500_extest_tx, EXTEST_WRITE_INST);

			/* Compare results */
			for (i = 0; i < EXTEST_WDR_REG_NUM; i++) {
				if (ch == (pat - 8))
					idx = 9 * EXTEST_WDR_REG_NUM + i;
				else
					idx = 8 * EXTEST_WDR_REG_NUM + i;

				expected = extest_wdr_pattern[idx];

				received = RREG32(base + (ch * 0x1000) + 0x960
								+ (i * 4));

				mask = extest_wdr_tx_mask[i];

				if ((received & mask) != (expected & mask)) {
					err++;
					hl_err(hdev,
						"mismatch: pattern %d, ch %d, WDR[%02d:%02d], expected 0x%x, received 0x%x, mask 0x%x\n",
						pat, ch, i * 32 + 31, i * 32,
						expected, received, mask);
				}
			}

			/* Special case - pins TEMP[2:0] + CATTRIP */
			if (ch == 0) {
				received = RREG32(base + 0x8000 + 0x04c);
				if (((received >> 2) & 1) !=
						((expected  >>  20) & 1)) {
					err++;
					hl_dbg(hdev,
						"mismatch: pattern %d, ch %d, WDR[212] (TEMP[2]), expected 0x%x, received 0x%x\n",
						pat, ch, ((expected >> 20) & 1),
						((received >> 2) & 1));
				}

				if (((received >> 1) & 1) !=
						((expected >> 21) & 1)) {
					err++;
					hl_dbg(hdev,
						"mismatch: pattern %d, ch %d, WDR[213] (TEMP[1]), expected 0x%x, received 0x%x\n",
						pat, ch, ((expected >> 21) & 1),
						((received >> 1) & 1));
				}

				if (((received >> 0) & 1) !=
						((expected  >>  22) & 1)) {
					err++;
					hl_dbg(hdev,
						"mismatch: pattern %d, ch %d, WDR[214] (TEMP[0]), expected 0x%x, received 0x%x\n",
						pat, ch, ((expected >> 22) & 1),
						((received >> 0) & 1));
				}
			}

			if (ch == 1) {
				received = RREG32(base + 0x8000 + 0x04c);
				if (((received >> 3) & 1) !=
						((expected >> 20) & 1)) {
					err++;
					hl_dbg(hdev,
						"mismatch: pattern %d, ch %d, WDR[212] (CATTRIP), expected 0x%x, received 0x%x\n",
						pat, ch, ((expected >> 20) & 1),
						((received >> 3) & 1));
				}
			}
		}
	}

	return err;
}

static void change_pll(struct hl_device *hdev, u32 val)
{
	WREG32(mmNIC0_PLL_DIV_SEL_2, 0);
	WREG32(mmNIC0_PLL_DIV_FACTOR_2, val);
	WREG32(mmNIC0_PLL_DIV_FACTOR_CMD_2, 1);
	WREG32(mmNIC0_PLL_DIV_SEL_2, 3);

	WREG32(mmNIC1_PLL_DIV_SEL_2, 0);
	WREG32(mmNIC1_PLL_DIV_FACTOR_2, val);
	WREG32(mmNIC1_PLL_DIV_FACTOR_CMD_2, 1);
	WREG32(mmNIC1_PLL_DIV_SEL_2, 3);
}

static void reduce_pll(struct hl_device *hdev)
{
	change_pll(hdev, 15);
}

static void restore_pll(struct hl_device *hdev)
{
	change_pll(hdev, 8);
}

static void set_dll_val(struct hl_device *hdev, u32 base, int ch, u32 reg,
			u32 val)
{
	u32 add16, add4, add1, address;
	int i;

	add16 = val >> 4;
	add4 = (val % 16) >> 2;
	add1 = (val % 16) % 4;

	address = base + ch * 0x1000 + reg;

	RMWREG32(address, 4, 0xFF);

	for (i = 0 ; i < add16 ; i++)
		RMWREG32(address, 3, 0xFF);

	for (i = 0 ; i < add4 ; i++)
		RMWREG32(address, 2, 0xFF);

	for (i = 0 ; i < add1 ; i++)
		RMWREG32(address, 1, 0xFF);
}

static void set_acc_dll(struct hl_device *hdev, u32 base, int ch, int dw,
			u32 val)
{
	set_dll_val(hdev, base, ch, (dw + 4) * 0x100 + 0x14, val);
}

static void set_dword_read_dll(struct hl_device *hdev, u32 base,
						int ch, int dw, u32 val)
{
	set_dll_val(hdev, base, ch, (dw + 5) * 0x100 + 0x18, val);
	set_dll_val(hdev, base, ch, (dw + 5) * 0x100 + 0x1C, val);
}

static void set_dword_write_dll(struct hl_device *hdev, u32 base,
						int ch, int dw, u32 val)
{
	set_dll_val(hdev, base, ch, (dw + 5) * 0x100 + 0x28, val);
}

static u32 aword_acc_window(struct hl_device *hdev, u32 base, int ch)
{
	return RREG32_MASK(base + ch * 0x1000 + 0x414, 0x1FF0000);
}

static u32 dword_acc_window(struct hl_device *hdev, u32 base, int ch, int dw)
{
	return RREG32_MASK(base + ch * 0x1000 + 0x100 * (dw + 5) + 0x14,
				0x1FF0000);
}

static void copy_acc(struct hl_device *hdev, u32 base)
{
	u32 val, add16, add4, add1;
	int i, ch;

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch += 2) {
		val = aword_acc_window(hdev, base, ch);

		add16 = val >> 4;
		add4 = (val % 16) >> 2;
		add1 = (val % 16) % 4;

		RMWREG32(base + (ch + 1) * 0x1000 + 0x414, 4, 0xFF);

		for (i = 0 ; i < add16 ; i++)
			RMWREG32(base + (ch + 1) * 0x1000 + 0x414, 3, 0xFF);

		for (i = 0 ; i < add4 ; i++)
			RMWREG32(base + (ch + 1) * 0x1000 + 0x414, 2, 0xFF);

		for (i = 0 ; i < add1 ; i++)
			RMWREG32(base + (ch + 1) * 0x1000 + 0x414, 1, 0xFF);
	}
}

static void config_mlb_state(struct hl_device *hdev, u32 base, int ch,
				enum hl_phy_state next_state)
{
	enum hl_phy_state curr_state =
				RREG32_MASK(base + ch * 0x1000 + 0x200, 0xFF00);
	s32 offset = next_state - curr_state;
	int i;

	if (!offset)
		return;

	/* msleep(20); */

	for (i = 0 ; i < abs(offset) ; i++)
		RMWREG32(base + ch * 0x1000 + 0x200,
				(offset > 0) ? 1 : 2, 0xFF);

	/* msleep(20); */
}

static void set_mode_reg_by_mlb(struct hl_device *hdev, u32 base,
						int ch, u32 num, u32 op)
{
	u32 count = 0, i, one = 1, parity;
	u32 value = (num << 8) | op;

	hl_dbg(hdev, "set mode by mlb, ch: %d\n", ch);

	for (i = 0; i < 12; i++)
		if (value & (one << i))
			count++;

	parity = count & 1;

	WREG32(base + ch * 0x1000 + 0x210, (parity << 16) | value);
	WREG32(base + ch * 0x1000 + 0x214, 4);

	ndelay(200);
}

static void config_dram_state(struct hl_device *hdev, u32 base,
					int ch, enum dram_state state)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	config_mlb_state(hdev, base, ch, phy_state_initial);

	if (state == dram_state_training) {
		/* CA Par=en, Wr Par=en, Rd Par=en, WDBI=en, RDBI=en */
		set_mode_reg_by_mlb(hdev, base, ch, 0, 0x73);
		/* drive strength=15mA, WR=16 */
		set_mode_reg_by_mlb(hdev, base, ch, 1, 0x70);
		/* RL=22, WL=7 */
		set_mode_reg_by_mlb(hdev, base, ch, 2, 0xA6);
		/* BL=4, BG=enable, tRAS=33 */
		set_mode_reg_by_mlb(hdev, base, ch, 3, 0xE1);
		/* PL=0, DM=disable, ECC=enable */
		set_mode_reg_by_mlb(hdev, base, ch, 4, 0x03);
		/* Vref=50% */
		set_mode_reg_by_mlb(hdev, base, ch, 15, 0x00);

		/* Adjust MLB for PL=0 */
		RMWREG32(base + ch*0x1000 + 0x204, 0x0, 0x300);
	} else if (state == dram_state_bypass) {
		/* CA Par=en, Wr Par=en, Rd Par=en, WDBI=en, RDBI=en */
		set_mode_reg_by_mlb(hdev, base, ch, 0, 0x70 |
						(gaudi->hbm_dbi_enable << 1) |
						(gaudi->hbm_dbi_enable << 0));
		/* drive strength=15mA, WR=16 */
		set_mode_reg_by_mlb(hdev, base, ch, 1, 0x70);
		/* RL=22, WL=7 */
		set_mode_reg_by_mlb(hdev, base, ch, 2, 0xA6);
		/* BL=4, BG=enable, tRAS=33 */
		set_mode_reg_by_mlb(hdev, base, ch, 3, 0xE1);
		/* PL=2, DM=disable, ECC=enable */
		set_mode_reg_by_mlb(hdev, base, ch, 4, 0x8 |
						(gaudi->hbm_ecc_enable << 1) |
						(gaudi->hbm_ecc_enable << 0));
		/* Vref=50% */
		set_mode_reg_by_mlb(hdev, base, ch, 15, 0x00);

		/* Adjust MLB for PL=2 */
		RMWREG32(base + ch*0x1000 + 0x204, 0x2, 0x300);
	} else {
		hl_err(hdev, "unknown dram state %d, ch: %d\n", state, ch);
	}
}

static void set_timing_params(struct hl_device *hdev, int device, int ch,
				int freq)
{
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;

	if (freq == 950) {
		/* trfc_sb_fix=152, r_r2w = 18 to support PL=2 */
		WREG32(base + (ch * 0x1000) + 0x04C, 0xA0030812);
		/* r_trfc_sb=304, r_trrefd_norm=109, trrefd_min=8 */
		WREG32(base + (ch * 0x1000) + 0x050, 0x206D0130);
		/* tRTP_L=5, tWTP_L=25, tRP=14 */
		WREG32(base + (ch * 0x1000) + 0x058, 0xE1905);
		/* r_tcksre=r_tcksrx=14, trefi=115, txp=10, trfc=333 */
		WREG32(base + (ch * 0x1000) + 0x05C, 0xEE73A14D);
	} else if (freq == 1000) {
		/* trfc_sb_fix=160, r_r2w = 18 to support PL=2 */
		WREG32(base + (ch * 0x1000) + 0x04C, 0xA0030812);
		/* r_trfc_sb=320, r_trrefd_norm=115, trrefd_min=8 */
		WREG32(base + (ch * 0x1000) + 0x050, 0x20730140);
		/* tRTP_L=5, tWTP_L=25, tRP=14 */
		WREG32(base + (ch * 0x1000) + 0x058, 0xE1905);
		/* r_tcksre=r_tcksrx=14, trefi=121, txp=10, trfc=350 */
		WREG32(base + (ch * 0x1000) + 0x05C, 0xEE79A15E);
	} else if (freq == 400) {
		/* trfc_sb_fix=64, r_r2w = 18 to support PL=2 */
		WREG32(base + (ch * 0x1000) + 0x04C, 0x40030812);
		/* r_trfc_sb=128, r_trrefd_norm=46, trrefd_min=8 */
		WREG32(base + (ch * 0x1000) + 0x050, 0x202E0080);
		/* tRTP_L=5, tWTP_L=16, tRP=6 */
		WREG32(base + (ch * 0x1000) + 0x058, 0x61005);
		/* r_tcksre=r_tcksrx=14, trefi=48, txp=4, trfc=140 */
		WREG32(base + (ch * 0x1000) + 0x05C, 0xEE30408C);
	} else if (freq == 666) {
		/* trfc_sb_fix=107, r_r2w = 18 to support PL=2 */
		WREG32(base + (ch * 0x1000) + 0x04C, 0x6B030812);
		/* r_trfc_sb=214, r_trrefd_norm=76, trrefd_min=8 */
		WREG32(base + (ch * 0x1000) + 0x050, 0x204C00D6);
		/* tRTP_L=5, tWTP_L=20, tRP=10 */
		WREG32(base + (ch * 0x1000) + 0x058, 0xA1405);
		/* r_tcksre=r_tcksrx=14, trefi=81, txp=5, trfc=234 */
		WREG32(base + (ch * 0x1000) + 0x05C, 0xEE5150EA);
	} else if (freq == 800) {
		/* trfc_sb_fix=128, r_r2w = 18 to support PL=2 */
		WREG32(base + (ch * 0x1000) + 0x04C, 0x80030812);
		/* r_trfc_sb=256, r_trrefd_norm=92, trrefd_min=8 */
		WREG32(base + (ch * 0x1000) + 0x050, 0x205C0100);
		/* tRTP_L=5, tWTP_L=22, tRP=12 */
		WREG32(base + (ch * 0x1000) + 0x058, 0xC1605);
		/* r_tcksre=r_tcksrx=14, trefi=97, txp=6, trfc=280 */
		WREG32(base + (ch * 0x1000) + 0x05C, 0xEE616118);
	} else if (freq == 900) {
		/* trfc_sb_fix=144, r_r2w = 18 to support PL=2 */
		WREG32(base + (ch * 0x1000) + 0x04C, 0x90030812);
		/* r_trfc_sb=288, r_trrefd_norm=103, trrefd_min=8 */
		WREG32(base + (ch * 0x1000) + 0x050, 0x20670120);
		/* tRTP_L=5, tWTP_L=24, tRP=13 */
		WREG32(base + (ch * 0x1000) + 0x058, 0xD1805);
		/* r_tcksre=r_tcksrx=14, trefi=109, txp=8, trfc=315 */
		WREG32(base + (ch * 0x1000) + 0x05C, 0xEE6D813B);
	} else {
		hl_err(hdev,
		       "missing timing parameters for frequency %d\n", freq);
		return;
	}

	/*
	 * r_t_rddata_en=0x15, r_t_phy_wrlat=0x6, r_t_phy_wrdata=0x1,
	 * r_rrq_switch_lvl_low=0x0
	 */
	WREG32(base + (ch * 0x1000) + 0x054, 0x10615);


}

static void config_mc_state(struct hl_device *hdev, int device, int ch,
				enum mc_state state)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;

	if (state == mc_state_bist) {
		RMWREG32(base + ch * 0x1000 + 0x004, 1, 0x1);
		/* Adjust the refresh rate */
		WREG32(base + (ch * 0x1000) + 0x050, 0x20360098);
	} else if (state == mc_state_normal) {
		/* Allow time for issuing REFab */
		udelay(100);
		RMWREG32(base + ch * 0x1000 + 0x004, 0, 0x1);
		set_timing_params(hdev, device, ch, gaudi->hbm_freq);
	} else
		hl_err(hdev, "unknown mc state %d, device: %d, ch: %d\n",
		       state, device, ch);
}

static int read_interrupts(struct hl_device *hdev, int unused)
{
	int device, err = 0;

	for (device = 0 ; device < GAUDI_HBM_DEVICES ; device++)
		if (gaudi_hbm_read_interrupts(hdev, device, NULL))
			err = 1;

	return err;
}

static void dbg_set_min_training_values(struct hl_device *hdev, int device)
{
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	int ch, dw;
	int margin = 8;

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		/* AWORD slave4 */
		set_dll_val(hdev, base, ch, 0x428, aword[ch][1]+margin);

		for (dw = 0 ; dw < 4 ; dw++) {
			/* DWORD slave0 (read rise) */
			set_dll_val(hdev, base, ch, dw * 0x100 + 0x518,
					dword_rd[ch][dw][1]+margin);
			/* DWORD slave1 (read fall) */
			set_dll_val(hdev, base, ch, dw * 0x100 + 0x51C,
					dword_rd[ch][dw][1]+margin);
			/* DWORD slave4 (write) */
			set_dll_val(hdev, base, ch, dw * 0x100 + 0x528,
					dword_wr[ch][dw][1]+margin);
		}
	}
}


static void dbg_set_max_training_values(struct hl_device *hdev, int device)
{
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	int ch, dw;
	int margin = 8;

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		/* AWORD slave4 */
		set_dll_val(hdev, base, ch, 0x428, aword[ch][2]-margin);

		for (dw = 0 ; dw < 4 ; dw++) {
			/* DWORD slave0 (read rise) */
			set_dll_val(hdev, base, ch, dw * 0x100 + 0x518,
					dword_rd[ch][dw][2]-margin);
			/* DWORD slave1 (read fall) */
			set_dll_val(hdev, base, ch, dw * 0x100 + 0x51C,
					dword_rd[ch][dw][2]-margin);
			/* DWORD slave4 (write) */
			set_dll_val(hdev, base, ch, dw * 0x100 + 0x528,
					dword_wr[ch][dw][2]-margin);
		}
	}
}

static int poll_mbist_completion(struct hl_device *hdev, int sid, int device)
{
	int err, ch, rc = 0;
	u32 base, status, timeout;

	hl_dbg(hdev, "%s\n", __func__);

	if (hdev->pldm)
		timeout = GAUDI_PLDM_HBM_ECC_CFG_TIMEOUT;
	else
		timeout = GAUDI_HBM_ECC_CFG_TIMEOUT;

	base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		u32 i, r1, r2, r3, r4;

		err = hl_poll_timeout(
			hdev,
			base + ch * 0x1000 + 0x11C,
			status,
			(status == 0x03000000),
			1000,
			timeout);

		if (!err)
			continue;

		rc = -EIO;

		/* if error on PC0 */
		if (((status >> 0  & 0x1) == 0x1) ||
				((status >> 24 & 0x1) == 0x0)) {

			hl_err(hdev,
			       "MCBIST Error - failed for HBM device %d, pc %d, sid %d, status 0x%x\n",
			       device, 2 * ch, sid, status);

			for (i = 2 ; i <= 6 ; i++) {
				u64 reg_addr = base + ch * 0x1000 + 0x100 + i * 0x10;

				r1 = RREG32(reg_addr);
				r2 = RREG32(reg_addr + 0x4);
				r3 = RREG32(reg_addr + 0x8);
				r4 = RREG32(reg_addr + 0xC);

				hl_err(hdev,
					"0x%x:\t0x%08x\t0x%08x\t0x%08x\t0x%08x\n",
					0x100 + i * 0x10, r1, r2, r3, r4);
			}
		}

		/* if error on PC1 */
		if (((status >> 1  & 0x1) == 0x1) ||
				((status >> 25 & 0x1) == 0x0)) {

			/* Select PC1 for debug output */
			RMWREG32(base + ch * 0x1000 + 0x100, 0x1, 0x20);

			hl_err(hdev,
				"MCBIST Error - failed for HBM device %d, pc %d, sid %d, status 0x%x\n",
				device, 2*ch+1, sid, status);

			for (i = 2 ; i <= 6 ; i++) {
				u64 reg_addr = base + ch * 0x1000 + 0x100 + i * 0x10;

				r1 = RREG32(reg_addr);
				r2 = RREG32(reg_addr + 0x4);
				r3 = RREG32(reg_addr + 0x8);
				r4 = RREG32(reg_addr + 0xC);

				hl_err(hdev,
					"0x%x:\t0x%08x\t0x%08x\t0x%08x\t0x%08x\n",
					0x100 + i * 0x10, r1, r2, r3, r4);
			}

			/* Select PC0 for debug output */
			RMWREG32(base + ch * 0x1000 + 0x100, 0x0, 0x20);
		}
	}

	return rc;
}

static int dbg_write_read_full(struct hl_device *hdev, int unused)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int device, ch, sid, rc = 0;
	u32 base, pseudo_rand = 0x1, interleave = 0x0;
	enum mc_bist_mode bist_mode = mc_bist_mode_once;

	hl_dbg(hdev, "%s\n", __func__);

	/* Clean transition from Functional mode to MCBIST mode */
	for (device = 0 ; device < GAUDI_HBM_DEVICES ; device++) {
		base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
		for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
			config_mlb_state(hdev, base, ch, phy_state_initial);
			config_mc_state(hdev, device, ch, mc_state_bist);
			config_mlb_state(hdev, base, ch, phy_state_bypass);
		}
	}

	for (sid = 0 ; sid < 2 ; sid++) {
		/* Configure MCBIST for all channels */
		for (device = 0 ; device < GAUDI_HBM_DEVICES ; device++) {
			base = GAUDI_HBM_CFG_BASE +
					device * GAUDI_HBM_CFG_OFFSET;

			for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
				WREG32(base + ch * 0x1000 + 0x100, pseudo_rand |
						(interleave << 1) |
						(bist_mode << 2) |
						(gaudi->hbm_ecc_enable  << 3));
				WREG32(base + ch * 0x1000 + 0x104,
						(0x0 | sid << 28 | sid << 29));
				WREG32(base + ch * 0x1000 + 0x108, 0x800000);
				WREG32(base + ch * 0x1000 + 0x10c, 0x12345678);
				WREG32(base + ch * 0x1000 + 0x110, 0x9ABCDEF0);
				WREG32(base + ch * 0x1000 + 0x114, 0x5A5A5A5A);
				WREG32(base + ch * 0x1000 + 0x118, 0xA5A5A5A5);
			}
		}

		/* Start MCBIST */
		for (device = 0 ; device < GAUDI_HBM_DEVICES ; device++) {
			base = GAUDI_HBM_CFG_BASE +
					device * GAUDI_HBM_CFG_OFFSET;

			for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
				WREG32(base + ch * 0x1000 + 0x11C, 0x3);
		}

		/* Poll for MCBIST completion */
		for (device = 0 ; device < GAUDI_HBM_DEVICES ; device++)
			rc |= poll_mbist_completion(hdev, sid, device);
	}

	/* Clean transition from MCBIST mode to Functional mode */
	for (device = 0 ; device < GAUDI_HBM_DEVICES ; device++) {
		base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
		for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
			config_mlb_state(hdev, base, ch, phy_state_initial);
			config_mc_state(hdev, device, ch, mc_state_normal);
			config_mlb_state(hdev, base, ch, phy_state_bypass);
		}
	}

	return rc;
}

static int do_loopback(struct hl_device *hdev, u32 base, int ch,
			enum loopback_state state, int continuous)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int i, j = ch * 16, cnt = 0;
	u32 val;

	if (state == loopback_state_initial) {
		/* Internal Loopback mode */
		RMWREG32(base + ch * 0x1000 + 0x370, 3, 0xFF);
		for (i = 0 ; i < 16 ; i++)
			WREG32(base + ch * 0x1000 + 0x380 + i * 4,
					hbm_golden_registers[i + j]);
		return 0;
	}

	if (state != loopback_state_check) {
		hl_err(hdev, "unknown loopback state %d, ch: %d\n", state,
				ch);
		return -EINVAL;
	}

	WREG32(base + ch * 0x1000 + 0x294, 1);
	WREG32(base + ch * 0x1000 + 0x294, 0);

	RMWREG32(base + ch * 0x1000 + 0x378, 0xF, 0xFF);
	if (continuous) {
		WREG32(base + ch * 0x1000 + 0x374, 0xF0);
		udelay(gaudi->hbm_internal_lb_length_usec);
		WREG32(base + ch * 0x1000 + 0x374, 0);
	} else {
		RMWREG32(base + ch * 0x1000 + 0x374, 0xF, 0xFF);
	}

	if (gaudi->hbm_debug) {
		for (i = 0 ; i < 15 ; i++) {
			val = RREG32(base + ch * 0x1000 + 0x3C0 + i * 4);
			if (val == hbm_golden_registers[i + j])
				cnt++;
		}

		return cnt == 15 ? 0 : -1;
	}

	val = RREG32(base + ch * 0x1000 + 0x37C);
	if (val) {
		hl_err(hdev, "hbm loopback wrong val: 0x%x, ch: %d\n",
				val, ch);
		return -EIO;
	}

	return 0;
}

static int internal_loopback_test(struct hl_device *hdev, int device)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	u32 dword_dr, pass_cnt = 0, max_pass_cnt = 0, pass_left_t = 0,
		max_pass_left_t = 0, window = 0, right_t = 0, middle_t = 0;
	bool fail = false;
	int ch, dw, continuous = gaudi->hbm_continuous;

	hl_dbg(hdev, "%s\n", __func__);

	/* Add delay from previous SRX */
	ndelay(500);

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		/* SRE */
		WREG32(base + ch * 0x1000 + 0x214, 2);
		/* ILB ctrl_rddv_sel */
		WREG32(base + ch * 0x1000 + 0x298, 5);
		/* Reset read FIFO */
		WREG32(base + ch * 0x1000 + 0x294, 1);
		WREG32(base + ch * 0x1000 + 0x294, 0);

		do_loopback(hdev, base, ch, loopback_state_initial, continuous);

		for (dw = 0 ; dw < 4 ; dw++)
			set_dword_read_dll(hdev, base, ch, dw, 0);

		if (gaudi->hbm_debug) {
			for (dword_dr = 0 ; dword_dr < 0xB0 ; dword_dr += 4) {
				for (dw = 0 ; dw < 4 ; dw++)
					set_dword_write_dll(hdev,
								base, ch, dw,
								dword_dr);

				if (!do_loopback(hdev, base, ch,
							loopback_state_check,
							continuous)) {
					if (pass_cnt == 0)
						pass_left_t = dword_dr;
					pass_cnt++;
				} else {
					fail = true;
					if (pass_cnt > max_pass_cnt) {
						max_pass_cnt = pass_cnt;
						max_pass_left_t = pass_left_t;
					}
					pass_cnt = 0;
				}
			}

			if (pass_cnt > max_pass_cnt) {
				max_pass_cnt = pass_cnt;
				max_pass_left_t = pass_left_t;
			}

			window = max_pass_cnt * 4;
			if (window)
				right_t = max_pass_left_t + window - 1;
			middle_t = max_pass_left_t + (window >> 1);

			if (fail) {
				hl_err(hdev,
					"HBM internal loopback failed, ch: %d\n",
					ch);
				hl_err(hdev, "interval: 0x%x ~ 0x%x\n",
					max_pass_left_t, right_t);
				hl_err(hdev, "window: %d\n", window);
				hl_err(hdev, "result: %d\n", middle_t);

				return -EIO;
			}
		} else {
			for (dw = 0 ; dw < 4 ; dw++)
				set_dword_write_dll(hdev, base, ch, dw, 0x50);
			if (do_loopback(hdev, base, ch, loopback_state_check,
								continuous)) {
				hl_err(hdev,
					"HBM internal loopback failed, ch: %d\n",
					ch);
				return -EIO;
			}
		}

		WREG32(base + ch * 0x1000 + 0x378, 0);
		WREG32(base + ch * 0x1000 + 0x370, 0);
		/* functional ctrl_rddv_sel */
		if (hdev->pldm)
			WREG32(base + ch * 0x1000 + 0x298, 6);
		else
			WREG32(base + ch * 0x1000 + 0x298, 4);
		/* SRX */
		WREG32(base + ch * 0x1000 + 0x214, 1);
	}

	return 0;
}

static void acc_training(struct hl_device *hdev, int device)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	u32 dll_inc, status, timeout;
	int ch, dw, err;

	if (hdev->pldm)
		timeout = GAUDI_PLDM_HBM_CFG_TIMEOUT;
	else
		timeout = GAUDI_HBM_CFG_TIMEOUT;

	if (gaudi->hbm_inc == 16)
		dll_inc = 3;
	else if (gaudi->hbm_inc == 4)
		dll_inc = 2;
	else
		dll_inc = 1;

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		for (dw = 0 ; dw < 5 ; dw++)
			set_acc_dll(hdev, base, ch, dw,
						gaudi->hbm_acc_initial);
		WREG32(base + ch * 0x1000 + 0x444,
				(gaudi->hbm_training_delay << 8) | dll_inc);
	}

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch += 2) {
		WREG32(base + ch * 0x1000 + 0x440, 0x1F);
		WREG32(base + (ch + 1) * 0x1000 + 0x440, 0xF);
	}

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		err = hl_poll_timeout(
			hdev,
			base + ch * 0x1000 + 0x440,
			status,
			status == 0,
			1000,
			timeout);

		if (err) {
			hl_err(hdev,
				"ACC done polling failed for HBM device %d, ch %d\n",
				device, ch);
			return;
		}
	}

	copy_acc(hdev, base);

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		if (!aword_acc_window(hdev, base, ch))
			hl_err(hdev,
				"aword acc_window for device %d, ch %d can't be 0\n",
				device, ch);
		for (dw = 0; dw < 4; dw++)
			if (!dword_acc_window(hdev, base, ch, dw))
				hl_err(hdev,
					"dword acc_window for device %d, ch %d, dw %d can't be 0\n",
					device, ch, dw);
	}
}

static void print_training_result(struct hl_device *hdev, int device)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int ch, dw, aw_ratio, wr_ratio, rd_ratio;
	int min_window_size = 50; /* percentage of UI */

	hl_dbg(hdev,
		"\n-----------------------------------------------------------------------\n");
	hl_dbg(hdev,
		"The detail test item is shown as below.\n");
	hl_dbg(hdev,
		"  <1> ACC training : Calculate one data (half ddr clock) period range.\n");
	hl_dbg(hdev,
		"  <2> AWORD write training : Ensure Clock in the middle of Command/Address bus.\n");
	hl_dbg(hdev,
		"  <3> DWORD write training : Ensure WDQS in the middle of DQ.\n");
	hl_dbg(hdev,
		"  <4> DWORD read training : Ensure RDQS to align with DQ edge.\n");
	hl_dbg(hdev,
		"-----------------------------------------------------------------------\n");

	hl_dbg(hdev,
		"\n\t\t<1> ACC training\n");
	hl_dbg(hdev,
		"    CH : start-end   start-end     start-end     start-end     start-end\n");
	hl_dbg(hdev,
		"  ---- : ---AW---   ----DW0----    ---DW1----    ---DW2----    ---DW3----\n");

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
		hl_dbg(hdev,
			"  CH-%d : 0x00-0x%x       0x00-0x%x       0x00-0x%x       0x00-0x%x       0x00-0x%x\n",
			ch, acc[ch][0], acc[ch][1], acc[ch][2], acc[ch][3],
			acc[ch][4]);

	hl_dbg(hdev, "\n\t\t<2> AWORD write training\n");
	hl_dbg(hdev, "    CH : result   ( ratio )  \tstart-end\n");
	hl_dbg(hdev, "  ---- : ---------------------------------\n");

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		if (acc[ch][0]) {
			aw_ratio = (aword[ch][3] * 100) / acc[ch][0];
			hl_dbg(hdev,
					"  CH-%d : 0x%x       (%d%%)  \t0x%x-0x%x\n",
					ch, aword[ch][0], aw_ratio,
					aword[ch][1], aword[ch][2]);
		} else {
			hl_dbg(hdev,
					"  CH-%d : 0x%x       (None)  \t0x%x\n",
					ch, aword[ch][0], aword[ch][1]);
		}
	}

	hl_dbg(hdev, "\n<3> DWORD write training\n");
	hl_dbg(hdev,
		"    CH : result   ( ratio )  \tstart-end\tresult   ( ratio )  \tstart-end\tresult   ( ratio )  \tstart-end\tresult   ( ratio )  \tstart-end\n");
	hl_dbg(hdev,
		"  ---- : --------DW0---------\t\t       --------DW1---------\t\t       --------DW2---------\t\t       --------DW3---------\t\n");

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		char s[256] = {0};
		int size = 0;

		size += snprintf(s + size, 256 - size, "CH-%d : ", ch);

		for (dw = 0 ; dw < 4 ; dw++) {
			if (acc[ch][dw+1]) {
				wr_ratio = (dword_wr[ch][dw][3] * 100) /
							acc[ch][dw + 1];
				size += snprintf(s + size, 256 - size,
						"0x%x   (%d%%)  \t0x%x-0x%x\t",
						dword_wr[ch][dw][0], wr_ratio,
						dword_wr[ch][dw][1],
						dword_wr[ch][dw][2]);
			} else {
				size += snprintf(s + size, 256 - size,
						"0x%x   (None)  \t0x%x-0x%x\t",
						dword_wr[ch][dw][0],
						dword_wr[ch][dw][1],
						dword_wr[ch][dw][2]);
			}
		}
		hl_dbg(hdev, "%s", s);
	}

	hl_dbg(hdev, "\n<4> DWORD read training\n");
	hl_dbg(hdev,
		"    CH : result   ( ratio )  \tstart-end\tresult   ( ratio )  \tstart-end\tresult   ( ratio )  \tstart-end\tresult   ( ratio )  \tstart-end\n");
	hl_dbg(hdev,
		"  ---- : --------DW0---------\t\t       --------DW1---------\t\t       --------DW2---------\t\t       --------DW3---------\t\n");

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		char s[256] = {0};
		int size = 0;

		size += snprintf(s + size, 256 - size, "CH-%d : ", ch);

		for (dw = 0 ; dw < 4 ; dw++) {
			if (acc[ch][dw+1]) {
				rd_ratio = (dword_rd[ch][dw][3] * 100) /
							acc[ch][dw + 1];
				size += snprintf(s + size, 256 - size,
						"0x%x   (%d%%)  \t0x%x-0x%x\t",
						dword_rd[ch][dw][0], rd_ratio,
						dword_rd[ch][dw][1],
						dword_rd[ch][dw][2]);
			} else {
				size += snprintf(s + size, 256 - size,
						"0x%x   (None)  \t0x%x-0x%x\t",
						dword_rd[ch][dw][0],
						dword_rd[ch][dw][1],
						dword_rd[ch][dw][2]);
			}
		}
		hl_dbg(hdev, "%s", s);
	}

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		for (dw = 0 ; dw < 5 ; dw++)
			if (acc[ch][dw] <= gaudi->hbm_acc_initial)
				hl_err(hdev,
					"system error - failed to obtain the UI size (ACC) for channel %d\n",
					ch);

		if (aword[ch][1] == 0 || aword[ch][2] == 0 ||
			aword[ch][3] < ((acc[ch][0] * min_window_size) / 100))
			hl_err(hdev,
				"system error - failed to find a valid AWORD window for channel %d\n",
				ch);

		for (dw = 0 ; dw < 4 ; dw++) {
			if (dword_wr[ch][dw][2] == 0 ||
				dword_wr[ch][dw][3] <
				((acc[ch][dw + 1] * min_window_size) / 100))
				hl_err(hdev,
					"system error - failed to find a valid DWORD Write window for channel %d dword %d\n",
					ch, dw);

			if (dword_rd[ch][dw][2] == 0 ||
				dword_rd[ch][dw][3] <
					((acc[ch][dw + 1] *
							min_window_size) / 100))
				hl_err(hdev,
					"system error - failed to find a valid DWORD Read window for channel %d dword %d\n",
					ch, dw);
		}
	}
}

static void phy_init(struct hl_device *hdev, int device)
{
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	u32 ctrl_rddv_sel, rdatvld_dly;
	int ch;

	hl_dbg(hdev, "%s\n", __func__);

	if (hdev->pldm)
		ctrl_rddv_sel = 6;
	else
		ctrl_rddv_sel = 4;

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		if (hdev->asic_type == ASIC_GAUDI ||
				hdev->asic_type == ASIC_GAUDI_SEC) {
			if (ch == 0 || ch == 4) {
				/* PLL settings */
				WREG32(base + ch * 0x1000 + 0x2B8, 0x0CD80303);
				WREG32(base + ch * 0x1000 + 0x2BC, 0x0000001E);

				/* PLL and DLL power up */
				RMWREG32(base + ch * 0x1000 + 0x288, 0x1, 0x1);
				mdelay(1);
				RMWREG32(base + ch * 0x1000 + 0x284, 0x0, 0x1);
				mdelay(1);
				RMWREG32(base + ch * 0x1000 + 0x28C, 0x0, 0x1);
				mdelay(1);

				/* Drive strength */
				WREG32(base + ch * 0x1000 + 0x2A0, 0x681F0);
				WREG32(base + ch * 0x1000 + 0x2A4, 0x6A1F0);
			}
		} else {
			if (ch == 0 || ch == 4) {
				/* Release PHY RSTN (set bit 1) */
				RMWREG32(base + ch * 0x1000 + 0x284, 1, 0x2);
				mdelay(1);

				/* non-deskew PLL settings 2Gbps */
				WREG32(base + ch * 0x1000 + 0x2B8, 0x19180F3F);
				WREG32(base + ch * 0x1000 + 0x2BC, 0x0000001B);

				/* Release PLL power-down (clear bit 0) */
				RMWREG32(base + ch * 0x1000 + 0x284, 0, 0x1);
				mdelay(1);

				/* Release PLL clock divider rstn */
				WREG32(base + ch * 0x1000 + 0x288, 0x0000001);
				mdelay(1);

				/* Release PLL stable_n (clear bit 7) */
				RMWREG32(base + ch * 0x1000 + 0x284, 0, 0x80);
				mdelay(1);
			}

			/* Release MLB & PHASE FIFO RSTN (set bit 7) */
			RMWREG32(base + ch * 0x1000 + 0x174, 1, 0x80);

			/* PHASE FIFO start (set bit 15) */
			RMWREG32(base + ch * 0x1000 + 0x174, 1, 0x8000);

			if (ch == 0 || ch == 4) {
				/* Release DLL power-down */
				WREG32(base + ch * 0x1000 + 0x28C, 0x0000000);
				mdelay(1);

				/* Drive strength */
				WREG32(base + ch * 0x1000 + 0x2A0, 0x681F0);
				WREG32(base + ch * 0x1000 + 0x2A4, 0x6A1F0);
			}
		}

		/* Reset read FIFO */
		RMWREG32(base + ch * 0x1000 + 0x294, 0x1, 0x1);
		RMWREG32(base + ch * 0x1000 + 0x294, 0x0, 0x1);

		/* DFI setting - PHY */
		WREG32(base + ch * 0x1000 + 0x208, 0x140106);

		/* DFI setting - MC:
		 * r_t_rddata_en=0x15, r_t_phy_wrlat=0x6, r_t_phy_wrdata=0x1,
		 * r_rrq_switch_lvl_low=0x0. TODO: remove later
		 */
		WREG32(base + (ch * 0x1000) + 0x054, 0x10615);

		/* trfc_sb_fix=152, r_r2w = 18 to support PL=2
		 * TODO: remove later
		 */
		WREG32(base + (ch * 0x1000) + 0x04C, 0xA0030812);

		/* ctrl_rddv_sel */
		WREG32(base + ch * 0x1000 + 0x298, ctrl_rddv_sel);

		/* Enable HBM CK, set PL=0, set rdatvld_dly */
		if (hdev->asic_type == ASIC_GAUDI ||
				hdev->asic_type == ASIC_GAUDI_SEC)
			rdatvld_dly = ctrl_rddv_sel + 8;
		else
			rdatvld_dly = ctrl_rddv_sel + 10;

		if (ch == 0 || ch == 1 || ch == 4 || ch == 5)
			WREG32(base + ch * 0x1000 + 0x204,
					0x001 | ((rdatvld_dly) << 16));
		else if (ch == 2 || ch == 3 || ch == 6 || ch == 7)
			WREG32(base + ch * 0x1000 + 0x204,
					0x001 | ((rdatvld_dly + 1) << 16));

		/* Enable ECC. TODO: remove later */
		WREG32(base + (ch * 0x1000) + 0x000, 0x413000);
	}
}

static void dram_rstn(struct hl_device *hdev, int device, enum cke_state cke)
{
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	int ch;

	hl_dbg(hdev, "%s\n", __func__);

	/* SRE (CKE low) */
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
		WREG32(base + ch * 0x1000 + 0x214, 0x2);

	/* WRCK disable */
	RMWREG32(base + GAUDI_HBM_CENTER_CHANNEL * 0x1000 + 0x18, 0x0, 0x1);

	/* WRSTn low */
	RMWREG32(base + GAUDI_HBM_CENTER_CHANNEL * 0x1000 + 0x14, 0x0, 0x1);

	/* hbm_RESETn low */
	RMWREG32(base + GAUDI_HBM_CENTER_CHANNEL * 0x1000 + 0x10, 0x0, 0x1);

	/* tINIT1=200us for initial power-up or tPW_RESET=1us with stable
	 * power
	 */
	mdelay(1);

	/* hbm_RESETn high */
	RMWREG32(base + GAUDI_HBM_CENTER_CHANNEL * 0x1000 + 0x10, 0x1, 0x1);

	/* tINIT3: 500us from RESETn deassertion to WRSTn/CKE high */
	mdelay(1);

	/* WRCK divider */
	RMWREG32(base + GAUDI_HBM_CENTER_CHANNEL * 0x1000 + 0x00, 0x1, 0x3);

	/* WRSTn high */
	RMWREG32(base + GAUDI_HBM_CENTER_CHANNEL * 0x1000 + 0x14, 0x1, 0x1);

	/* WRCK enable */
	RMWREG32(base + GAUDI_HBM_CENTER_CHANNEL * 0x1000 + 0x18, 0x1, 0x1);

	/* IEEE1500 IO strength
	 * RMWREG32(base + GAUDI_HBM_CENTER_CHANNEL * 0x1000 + 0x40, 0xF, 0xF);
	 */

	/* tINIT4 / tWINIT1 */
	udelay(1);

	if (cke == cke_state_high) {

		/* SRX (CKE high) */
		for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
			WREG32(base + ch * 0x1000 + 0x214, 0x1);

		/* tINIT5 */
		udelay(1);
	}
}

static void init_mc(struct hl_device *hdev, int device)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	int ch;

	hl_dbg(hdev, "initializing MC for device %d\n", device);

	/* Cleanup: Reset HBM + MRS setup */
	dram_rstn(hdev, device, cke_state_high);
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
		config_dram_state(hdev, base, ch, dram_state_bypass);

	/* Set dfi_init_complete */
	WREG32(base + 0x9004, 1);

	/* Enable DBI (MCCTL) */
	if (gaudi->hbm_dbi_enable) {
		WREG32(base + 0x8004, 0xFFFFFFFF);
		WREG32(base + 0x8008, 0xFFFFFFFF);
		WREG32(base + 0x800C, 0xFFFFFFFF);
		WREG32(base + 0x8010, 0xFFFFFFFF);
		WREG32(base + 0x8014, 0xFFFFFFFF);
		WREG32(base + 0x8018, 0xFFFFFFFF);
		WREG32(base + 0x801C, 0xFFFFFFFF);
		WREG32(base + 0x8020, 0xFFFFFFFF);
	}

	/* Enable CATTRIP interrupt */
	WREG32(base + 0x8000 + 0x044, 0x8);

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		/* Reset PHY read FIFO */
		WREG32(base + (ch * 0x1000) + 0x294, 1);
		WREG32(base + (ch * 0x1000) + 0x294, 0);

		/* Pass control to MC */
		config_mlb_state(hdev, base, ch, phy_state_bypass);
		config_mc_state(hdev, device, ch, mc_state_normal);

		/* Basic configuration */
		WREG32(base + (ch * 0x1000) + 0x000,
				0x21012010 | (gaudi->hbm_ecc_enable << 22));

		/* Enable interrupts. Clear them from leftovers from init. */
		RMWREG32(base + (ch * 0x1000) + 0x060, 0x8, 0xF);
		RMWREG32(base + (ch * 0x1000) + 0x070, 0x8, 0xF);
		WREG32(base + (ch * 0x1000) + 0x06C, 0x1F1F);
		WREG32(base + (ch * 0x1000) + 0x07C, 0x1F1F);
		WREG32(base + (ch * 0x1000) + 0x068, 0x817);
		WREG32(base + (ch * 0x1000) + 0x078, 0x817);
		RMWREG32(base + (ch * 0x1000) + 0x060, 0x0, 0xF);
		RMWREG32(base + (ch * 0x1000) + 0x070, 0x0, 0xF);
	}
}

static void enable_traffic(struct hl_device *hdev, int device)
{
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	int ch;

	hl_dbg(hdev, "enabling traffic for HBM device %d\n", device);

	/* Enable traffic */
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		RMWREG32(base + (ch * 0x1000) + 0x000, 0x1, 0x8);
		WREG32(base + (ch * 0x1000) + 0x004, 0xC0);
	}
}

static void read_hardware_training_result(struct hl_device *hdev, u32 base)
{
	int ch;

	hl_dbg(hdev, "%s\n", __func__);

	for (ch = 0; ch < GAUDI_HBM_CHANNELS; ch++) {
		acc[ch][0] = RREG32_MASK(base + (ch * 0x1000) + 0x414,
					0x1FF0000);
		acc[ch][1] = RREG32_MASK(base + (ch * 0x1000) + 0x514,
					0x1FF0000);
		acc[ch][2] = RREG32_MASK(base + (ch * 0x1000) + 0x614,
					0x1FF0000);
		acc[ch][3] = RREG32_MASK(base + (ch * 0x1000) + 0x714,
					0x1FF0000);
		acc[ch][4] = RREG32_MASK(base + (ch * 0x1000) + 0x814,
					0x1FF0000);

		aword[ch][0] = RREG32_MASK(base + (ch * 0x1000) + 0x428,
					0x1FF0000);
		aword[ch][1] = RREG32_MASK(base + (ch * 0x1000) + 0x458, 0x1FF);
		aword[ch][2] = RREG32_MASK(base + (ch * 0x1000) + 0x458,
					0x1FF0000);
		aword[ch][3] = aword[ch][2] - aword[ch][1] + 1;

		dword_wr[ch][0][0] = RREG32_MASK(base + (ch * 0x1000) + 0x528,
						0x1FF0000);
		dword_wr[ch][0][1] = RREG32_MASK(base + (ch * 0x1000) + 0x470,
						0x1FF);
		dword_wr[ch][0][2] = RREG32_MASK(base + (ch * 0x1000) + 0x470,
						0x1FF0000);
		dword_wr[ch][0][3] = dword_wr[ch][0][2] -
					dword_wr[ch][0][1] + 1;
		dword_wr[ch][1][0] = RREG32_MASK(base + (ch * 0x1000) + 0x628,
						0x1FF0000);
		dword_wr[ch][1][1] = RREG32_MASK(base + (ch * 0x1000) + 0x474,
						0x1FF);
		dword_wr[ch][1][2] = RREG32_MASK(base + (ch * 0x1000) + 0x474,
						0x1FF0000);
		dword_wr[ch][1][3] = dword_wr[ch][1][2] -
					dword_wr[ch][1][1] + 1;
		dword_wr[ch][2][0] = RREG32_MASK(base + (ch * 0x1000) + 0x728,
						0x1FF0000);
		dword_wr[ch][2][1] = RREG32_MASK(base + (ch * 0x1000) + 0x478,
						0x1FF);
		dword_wr[ch][2][2] = RREG32_MASK(base + (ch * 0x1000) + 0x478,
						0x1FF0000);
		dword_wr[ch][2][3] = dword_wr[ch][2][2] -
					dword_wr[ch][2][1] + 1;
		dword_wr[ch][3][0] = RREG32_MASK(base + (ch * 0x1000) + 0x828,
						0x1FF0000);
		dword_wr[ch][3][1] = RREG32_MASK(base + (ch * 0x1000) + 0x47C,
						0x1FF);
		dword_wr[ch][3][2] = RREG32_MASK(base + (ch * 0x1000) + 0x47C,
						0x1FF0000);
		dword_wr[ch][3][3] = dword_wr[ch][3][2] -
					dword_wr[ch][3][1] + 1;

		dword_rd[ch][0][0] = RREG32_MASK(base + (ch * 0x1000) + 0x518,
						0x1FF0000);
		dword_rd[ch][0][1] = RREG32_MASK(base + (ch * 0x1000) + 0x490,
						0x1FF);
		dword_rd[ch][0][2] = RREG32_MASK(base + (ch * 0x1000) + 0x490,
						0x1FF0000);
		dword_rd[ch][0][3] = dword_rd[ch][0][2] -
					dword_rd[ch][0][1] + 1;
		dword_rd[ch][1][0] = RREG32_MASK(base + (ch * 0x1000) + 0x618,
						0x1FF0000);
		dword_rd[ch][1][1] = RREG32_MASK(base + (ch * 0x1000) + 0x494,
						0x1FF);
		dword_rd[ch][1][2] = RREG32_MASK(base + (ch * 0x1000) + 0x494,
						0x1FF0000);
		dword_rd[ch][1][3] = dword_rd[ch][1][2] -
					dword_rd[ch][1][1] + 1;
		dword_rd[ch][2][0] = RREG32_MASK(base + (ch * 0x1000) + 0x718,
						0x1FF0000);
		dword_rd[ch][2][1] = RREG32_MASK(base + (ch * 0x1000) + 0x498,
						0x1FF);
		dword_rd[ch][2][2] = RREG32_MASK(base + (ch * 0x1000) + 0x498,
						0x1FF0000);
		dword_rd[ch][2][3] = dword_rd[ch][2][2] -
					dword_rd[ch][2][1] + 1;
		dword_rd[ch][3][0] = RREG32_MASK(base + (ch * 0x1000) + 0x818,
						0x1FF0000);
		dword_rd[ch][3][1] = RREG32_MASK(base + (ch * 0x1000) + 0x49C,
						0x1FF);
		dword_rd[ch][3][2] = RREG32_MASK(base + (ch * 0x1000) + 0x49C,
						0x1FF0000);
		dword_rd[ch][3][3] = dword_rd[ch][3][2] -
					dword_rd[ch][3][1] + 1;
	}
}

static void hardware_training(struct hl_device *hdev, int device)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	u32 dll_inc, status, timeout;
	int ch, err;

	hl_dbg(hdev, "%s\n", __func__);

	if (hdev->pldm)
		timeout = GAUDI_PLDM_HBM_CFG_TIMEOUT;
	else
		timeout = GAUDI_HBM_CFG_TIMEOUT;

	reduce_pll(hdev);

	if (gaudi->hbm_inc == 16)
		dll_inc = 3;
	else if (gaudi->hbm_inc == 4)
		dll_inc = 2;
	else
		dll_inc = 1;

	/* Reset read FIFO */
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		WREG32(base + ch * 0x1000 + 0x294, 1);
		WREG32(base + ch * 0x1000 + 0x294, 0);
	}

	acc_training(hdev, device);

	/* AWORD training */
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
		WREG32(base + ch * 0x1000 + 0x454,
			((gaudi->hbm_hardware_training_repeat - 1) << 16) |
				(gaudi->hbm_training_delay << 8) | dll_inc);

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		WREG32(base + ch * 0x1000 + 0x450, 1);

		err = hl_poll_timeout(
			hdev,
			base + ch * 0x1000 + 0x450,
			status,
			status == 0,
			1000,
			timeout);

		if (err) {
			hl_err(hdev,
				"AWORD write done test failed for HBM device %d, ch %d\n",
				device, ch);
			goto out;
		}
	}

	/* Cleanup: Reset HBM + MRS setup */
	dram_rstn(hdev, device, cke_state_high);
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
		config_dram_state(hdev, base, ch, dram_state_training);

	/* DWORD read training */
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
		WREG32(base + ch * 0x1000 + 0x484,
			((gaudi->hbm_hardware_training_repeat - 1) << 16) |
			(gaudi->hbm_training_delay << 8) | dll_inc);

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		WREG32(base + ch * 0x1000 + 0x480, 1);

		err = hl_poll_timeout(
			hdev,
			base + ch * 0x1000 + 0x480,
			status,
			status == 0,
			1000,
			timeout);

		if (err) {
			hl_err(hdev,
				"DWORD read done test failed for HBM device %d, ch %d\n",
				device, ch);
			goto out;
		}
	}

	/* Cleanup: Reset HBM + MRS setup */
	dram_rstn(hdev, device, cke_state_high);
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
		config_dram_state(hdev, base, ch, dram_state_training);

	/* DWORD write training */
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
		WREG32(base + ch * 0x1000 + 0x464,
			((gaudi->hbm_hardware_training_repeat - 1) << 16) |
			(gaudi->hbm_training_delay << 8) | dll_inc);

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		WREG32(base + ch * 0x1000 + 0x460, 1);

		err = hl_poll_timeout(
			hdev,
			base + ch * 0x1000 + 0x460,
			status,
			status == 0,
			1000,
			timeout);

		if (err) {
			hl_err(hdev,
				"DWORD write done test failed for HBM device %d, ch %d\n",
				device, ch);
			goto out;
		}
	}

out:
	read_hardware_training_result(hdev, base);

	restore_pll(hdev);
}

static int init_ecc(struct hl_device *hdev, int unused)
{
	u32 base;
	u32 status, timeout;
	int device, err, ch, sid;

	hl_info(hdev, "Initializing ECC\n");

	if (hdev->pldm)
		timeout = GAUDI_PLDM_HBM_ECC_CFG_TIMEOUT;
	else
		timeout = GAUDI_HBM_ECC_CFG_TIMEOUT;

	/* Clean transition from Functional mode to MCBIST mode */
	for (device = 0 ; device < GAUDI_HBM_DEVICES ; device++) {
		base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
		for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
			config_mlb_state(hdev, base, ch, phy_state_initial);
			config_mc_state(hdev, device, ch, mc_state_bist);
			config_mlb_state(hdev, base, ch, phy_state_bypass);
		}
	}

	/* Configure MCBIST engine for ECC init in all channels */
	for (device = 0 ; device < GAUDI_HBM_DEVICES ; device++) {
		base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
		for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
			WREG32(base + ch * 0x1000 + 0x100, 0x100);
			WREG32(base + ch * 0x1000 + 0x108, 0x800000);
			WREG32(base + ch * 0x1000 + 0x10c, 0x0);
			WREG32(base + ch * 0x1000 + 0x110, 0x0);
			WREG32(base + ch * 0x1000 + 0x114, 0xFFFFFFFF);
			WREG32(base + ch * 0x1000 + 0x118, 0xFFFFFFFF);
		}
	}

	for (sid = 0 ; sid < 2 ; sid++) {
		/* Start MCBIST engine */
		for (device = 0 ; device < GAUDI_HBM_DEVICES ; device++) {
			base = GAUDI_HBM_CFG_BASE +
						device * GAUDI_HBM_CFG_OFFSET;
			for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
				WREG32(base + ch * 0x1000 + 0x104,
						(0x0 | sid << 28 | sid << 29));
				WREG32(base + ch * 0x1000 + 0x11C, 0x3);
			}
		}

		/* Poll for MCBIST completion */
		for (device = 0 ; device < GAUDI_HBM_DEVICES ; device++) {
			base = GAUDI_HBM_CFG_BASE +
						device * GAUDI_HBM_CFG_OFFSET;
			for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
				err = hl_poll_timeout(
					hdev,
					base + ch * 0x1000 + 0x11C,
					status,
					(status == 0x03000000),
					1000,
					timeout);

				if (err) {
					hl_err(hdev,
						"ECC init failed for HBM device %d, ch %d, sid %d, status 0x%x\n",
						device, ch, sid, status);
					config_mc_state(hdev, device, ch,
							mc_state_normal);
					return -EIO;
				}
			}
		}
	}

	/* Clean transition from MCBIST mode to Functional mode */
	for (device = 0 ; device < GAUDI_HBM_DEVICES ; device++) {
		base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
		for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
			config_mlb_state(hdev, base, ch, phy_state_initial);
			config_mc_state(hdev, device, ch, mc_state_normal);
			config_mlb_state(hdev, base, ch, phy_state_bypass);
		}
	}

	return 0;
}

static int lane_detection(struct hl_device *hdev, int device)
{
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	int ch, rc;

	hl_dbg(hdev, "%s\n", __func__);

	/* Enter IODC mode */
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
		WREG32(base + ch * 0x1000 + 0x900, 1);

	rc = extest_rx(hdev, device);
	if (rc) {
		hl_err(hdev, "extest_rx failed, device: %d\n", device);
		/* TODO: debug failed EXTEST  return rc; */
	}

	rc = extest_tx(hdev, device);
	if (rc) {
		hl_err(hdev, "extest_tx failed, device: %d\n", device);
		/* TODO: debug failed EXTEST  return rc; */
	}

	/* Exit IODC mode */
	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++)
		WREG32(base + ch * 0x1000 + 0x900, 0);


	return 0;
}

static void lane_remap(struct hl_device *hdev, int device)
{
	u32 base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	u32 reg_800, reg_804, reg_808;
	u32 remap_info_dw01, remap_info_dw23, remap_info_aw;
	int ch;

	hl_dbg(hdev, "%s\n", __func__);

	for (ch = 0 ; ch < GAUDI_HBM_CHANNELS ; ch++) {
		/* Read HBM lane repair eFuse information */
		ieee1500_inst(hdev, device, ch, ieee1500_hard_lane_repair,
				EXTEST_READ_INST);
		reg_800 = RREG32(base + (GAUDI_HBM_CENTER_CHANNEL * 0x1000) +
							0x800 + (ch * 0x100));
		reg_804 = RREG32(base + (GAUDI_HBM_CENTER_CHANNEL * 0x1000) +
							0x804 + (ch * 0x100));
		reg_808 = RREG32(base + (GAUDI_HBM_CENTER_CHANNEL * 0x1000) +
							0x808 + (ch * 0x100));

		/* Configure MLB according to remapped lanes */
		remap_info_dw01 = reg_800;
		remap_info_aw   = reg_804 & 0xFF;
		remap_info_dw23 = ((reg_804 & 0xFFFFFF00) >> 8) |
						((reg_808 & 0xFF) << 24);
		WREG32(base + (ch*0x1000) + 0x2F0, remap_info_dw01);
		WREG32(base + (ch*0x1000) + 0x2F4, remap_info_dw23);
		WREG32(base + (ch*0x1000) + 0x2F8, remap_info_aw);

		if ((remap_info_dw01 != 0xFFFFFFFF) ||
			(remap_info_dw23 != 0xFFFFFFFF) ||
				(remap_info_aw != 0xFF))
			hl_err(hdev,
				"Lane remapping info FOUND in HBM %d ch %d - updating PHY remapping\n",
				device, ch);
	}

}

static void temperature_read(struct hl_device *hdev, int device)
{

	int temp;
	u32 base, rdata, ignore = 0;
	bool invalid;

	base = GAUDI_HBM_CFG_BASE + device * GAUDI_HBM_CFG_OFFSET;
	ieee1500_inst(hdev, device, ignore, ieee1500_temperature,
			EXTEST_READ_INST);
	rdata = RREG32(base + GAUDI_HBM_CENTER_CHANNEL * 0x1000 + 0x800);
	invalid = (rdata >> 7) & 1;
	temp = rdata & 0x7F;

	if (invalid) {
		hl_err(hdev, "Temperature read from HBM %d is invalid\n",
				device);
	} else {
		/* Display temperatures - celsius degrees */
		hl_info(hdev,
			"Temperature read from HBM %d: %d deg celsius\n",
			device, temp);
		if (temp > GAUDI_MAX_TEMP_THRESHOLD) {
			/* system power down? */
			hl_warn(hdev,
				"Temperature read from HBM %d is high!\n",
				device);
		}
	}
}

static void gaudi_pldm_init_hbm_bank(struct hl_device *hdev, int bank)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 rcv_data, base_addr, timeout;
	int ch, rc;

	if (hdev->pldm)
		timeout = GAUDI_PLDM_HBM_CFG_TIMEOUT;
	else
		timeout = GAUDI_HBM_CFG_TIMEOUT;

	hl_dbg(hdev, "Starting to initialize HBM Bank %d\n", bank);

	base_addr = bank * GAUDI_HBM_CFG_OFFSET;

	rcv_data = RREG32(base_addr + 0x6001fc);
	if (rcv_data & 0x01000000) {
		hl_dbg(hdev, "HBM Bank %d already initialized\n", bank);
		gaudi->hw_cap_initialized |= HW_CAP_HBM;
		return;
	}

	for (ch = 0 ; ch < 8 ; ch++) {
		WREG32(base_addr + 0x60009c + ch * 0x1000, 0x0000001C);
		WREG32(base_addr + 0x600094 + ch * 0x1000, 0x003F1716);
		WREG32(base_addr + 0x6000bc + ch * 0x1000, 0x00060C1C);
		WREG32(base_addr + 0x6000b4 + ch * 0x1000, 0x003F1716);
		WREG32(base_addr + 0x600000 + ch * 0x1000, 0x20010010);
		WREG32(base_addr + 0x600010 + ch * 0x1000, 0x14250003);
		WREG32(base_addr + 0x600014 + ch * 0x1000, 0x0A001E01);
		WREG32(base_addr + 0x600048 + ch * 0x1000, 0x20020A0E);
		WREG32(base_addr + 0x600050 + ch * 0x1000, 0x207800A0);
	}

	for (ch = 0 ; ch < 8 ; ch++) {
		WREG32(base_addr + 0x600048 + ch * 0x1000, 0x20020A0E);
		WREG32(base_addr + 0x600058 + ch * 0x1000, 0x000E1805);
	}

	/* PLL settings */
	if (hdev->asic_type == ASIC_GAUDI ||
			hdev->asic_type == ASIC_GAUDI_SEC) {
		WREG32(base_addr + 0x6002B8, 0xCC00303);
		WREG32(base_addr + 0x6042B8, 0xCC00303);
		WREG32(base_addr + 0x6002BC, 0x000001A);
		WREG32(base_addr + 0x6042BC, 0x000001A);
		WREG32(base_addr + 0x600288, 0x0000001);
		WREG32(base_addr + 0x604288, 0x0000001);
		WREG32(base_addr + 0x600284, 0x0000000);
		WREG32(base_addr + 0x604284, 0x0000000);
	} else {
		/* PLL + PHASE FIFO init sequence modifications */

		/* Release PHY RSTN (set bit 1) */
		RMWREG32(base_addr + 0x600284, 1, 0x2);
		RMWREG32(base_addr + 0x604284, 1, 0x2);
		/* non-deskew PLL settings 2Gbps */
		WREG32(base_addr + 0x6002B8, 0x19180F3F);
		WREG32(base_addr + 0x6042B8, 0x19180F3F);
		WREG32(base_addr + 0x6002BC, 0x0000001B);
		WREG32(base_addr + 0x6042BC, 0x0000001B);
		/* Release PLL power-down (clear bit 0) */
		RMWREG32(base_addr + 0x600284, 0, 0x1);
		RMWREG32(base_addr + 0x604284, 0, 0x1);
		/* Release PLL clock divider rstn */
		WREG32(base_addr + 0x600288, 0x0000001);
		WREG32(base_addr + 0x604288, 0x0000001);
		/* Release PLL stable_n (clear bit 7) */
		RMWREG32(base_addr + 0x600284, 0, 0x80);
		RMWREG32(base_addr + 0x604284, 0, 0x80);

		/* Release MLB & PHASE FIFO RSTN (set bit 7) */
		for (ch = 0 ; ch < 8 ; ch++)
			RMWREG32(base_addr + 0x600174 + ch * 0x1000, 1, 0x80);

		/* PHASE FIFO start (set bit 15) */
		for (ch = 0 ; ch < 8 ; ch++)
			RMWREG32(base_addr + 0x600174 + ch * 0x1000, 1, 0x8000);
	}

	/* Release DLL power-down */
	WREG32(base_addr + 0x60028C, 0x0000000);
	WREG32(base_addr + 0x60428C, 0x0000000);

	for (ch = 0 ; ch < 8 ; ch++) {
		WREG32(base_addr + 0x600054 + ch * 0x1000, 0x010614);
		WREG32(base_addr + 0x600208 + ch * 0x1000, 0x140106);
		WREG32(base_addr + 0x600298 + ch * 0x1000, 0x000006);
		WREG32(base_addr + 0x600204 + ch * 0x1000, 0x000001);
	}

	WREG32(base_addr + 0x609010, 0x00000001);
	WREG32(base_addr + 0x609000, 0x00000000);
	WREG32(base_addr + 0x609018, 0x00000001);
	WREG32(base_addr + 0x609014, 0x00000001);

	for (ch = 0 ; ch < 8 ; ch++)
		WREG32(base_addr + 0x600214 + ch * 0x1000, 0x00000001);

	for (ch = 0 ; ch < 8 ; ch++)
		WREG32(base_addr + 0x600294 + ch * 0x1000, 0x00000001);

	for (ch = 0 ; ch < 8 ; ch++)
		WREG32(base_addr + 0x600294 + ch * 0x1000, 0x00000000);

	WREG32(base_addr + 0x609004, 0x00000001);

	for (ch = 0 ; ch < 8 ; ch++)
		WREG32(base_addr + 0x600200 + ch * 0x1000, 0x000000002);

	for (ch = 0 ; ch < 8 ; ch++)
		WREG32(base_addr + 0x600200 + ch * 0x1000, 0x000000002);

	for (ch = 0 ; ch < 8 ; ch++) {
		u32 addr = base_addr + 0x60000C + 0x1000 * ch;

		WREG32(addr, 0x000F7702);

		rc = hl_poll_timeout(
			hdev,
			addr,
			rcv_data,
			((rcv_data & 0x2) == 0),
			1000,
			timeout);

		if (rc) {
			hl_err(hdev,
				"Timeout while waiting for HBM %d MR0, ch %d\n",
				bank, ch);
			return;
		}
	}

	for (ch = 0 ; ch < 8 ; ch++) {
		u32 addr = base_addr + 0x60000C + 0x1000 * ch;

		WREG32(addr, 0x000F1012);

		rc = hl_poll_timeout(
			hdev,
			addr,
			rcv_data,
			((rcv_data & 0x2) == 0),
			1000,
			timeout);

		if (rc) {
			hl_err(hdev,
				"Timeout while waiting for HBM %d MR1, ch %d\n",
				bank, ch);
			return;
		}
	}

	for (ch = 0 ; ch < 8 ; ch++) {
		u32 addr = base_addr + 0x60000C + 0x1000 * ch;

		WREG32(addr, 0x000FA622);

		rc = hl_poll_timeout(
			hdev,
			addr,
			rcv_data,
			((rcv_data & 0x2) == 0),
			1000,
			timeout);

		if (rc) {
			hl_err(hdev,
				"Timeout while waiting for HBM %d MR2, ch %d\n",
				bank, ch);
			return;
		}
	}

	for (ch = 0 ; ch < 8 ; ch++) {
		u32 addr = base_addr + 0x60000C + 0x1000 * ch;

		WREG32(addr, 0x000FE032);

		rc = hl_poll_timeout(
			hdev,
			addr,
			rcv_data,
			((rcv_data & 0x2) == 0),
			1000,
			timeout);

		if (rc) {
			hl_err(hdev,
				"Timeout while waiting for HBM %d MR3, ch %d\n",
				bank, ch);
			return;
		}
	}

	for (ch = 0 ; ch < 8 ; ch++) {
		u32 addr = base_addr + 0x60000C + 0x1000 * ch;

		WREG32(addr, 0x000F0342);

		rc = hl_poll_timeout(
			hdev,
			addr,
			rcv_data,
			((rcv_data & 0x2) == 0),
			1000,
			timeout);

		if (rc) {
			hl_err(hdev,
				"Timeout while waiting for HBM %d MR4, ch %d\n",
				bank, ch);
			return;
		}
	}

	for (ch = 0 ; ch < 8 ; ch++)
		WREG32(base_addr + 0x608004 + ch * 4, 0xFFFFFFFF);

	for (ch = 0 ; ch < 8 ; ch++) {
		WREG32(base_addr + 0x60005C + 0x1000 * ch, 0xEE7A815E);
		WREG32(base_addr + 0x600044 + 0x1000 * ch, 0x14260000);
		WREG32(base_addr + 0x600058 + 0x1000 * ch, 0x000E1905);
		WREG32(base_addr + 0x600010 + 0x1000 * ch, 0x14250003);
		WREG32(base_addr + 0x600048 + 0x1000 * ch, 0x20020A0E);
		WREG32(base_addr + 0x600050 + 0x1000 * ch, 0x20730140);
		WREG32(base_addr + 0x60004C + 0x1000 * ch, 0xA0030810);
		WREG32(base_addr + 0x600000 + 0x1000 * ch, 0x21412018);
	}

	for (ch = 0 ; ch < 8 ; ch++)
		WREG32(base_addr + 0x600004 + (0x1000 * ch), 0x000000C0);

	WREG32_OR(base_addr + 0x6001FC, 0x01000000);

	hl_dbg(hdev, "Finished HBM Bank %d initialization\n", bank);
}

/*    *** Replaced with init_ecc ***
static int gaudi_hbm_ecc_scrubbing(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 total_size, cur_addr = prop->dram_base_address;
	u32 val;
	int rc, dma_id;

	hl_info(hdev, "ECC enabled so doing ECC scrubbing using DMA\n");

	total_size = prop->dram_end_address - cur_addr;
	if ((total_size != SZ_16G) && (total_size != SZ_32G)) {
		hl_err(hdev, "total size of HBM is invalid 0x%llx\n",
			total_size);
		return -EINVAL;
	}

	while (cur_addr < prop->dram_end_address) {
		for (dma_id = 0 ; dma_id < DMA_NUMBER_OF_CHANNELS ; dma_id++) {
			u32 dma_offset = dma_id * DMA_CORE_OFFSET;

			hl_dbg(hdev,
				"Doing ECC scrubbing for 0x%09llx - 0x%09llx\n",
				cur_addr, cur_addr + SZ_2G);

			WREG32(mmDMA0_CORE_SRC_BASE_LO + dma_offset, 0);
			WREG32(mmDMA0_CORE_SRC_BASE_HI + dma_offset, 0);
			WREG32(mmDMA0_CORE_DST_BASE_LO + dma_offset,
						lower_32_bits(cur_addr));
			WREG32(mmDMA0_CORE_DST_BASE_HI + dma_offset,
						upper_32_bits(cur_addr));
			WREG32(mmDMA0_CORE_DST_TSIZE_0 + dma_offset, SZ_2G);
			WREG32(mmDMA0_CORE_COMMIT + dma_offset,
					((1 << DMA0_CORE_COMMIT_LIN_SHIFT) |
					(1 << DMA0_CORE_COMMIT_MEM_SET_SHIFT)));

			cur_addr += SZ_2G;
		}

		for (dma_id = 0 ; dma_id < DMA_NUMBER_OF_CHANNELS ; dma_id++) {
			u32 dma_offset = dma_id * DMA_CORE_OFFSET;

			rc = hl_poll_timeout(
				hdev,
				mmDMA0_CORE_STS0 + dma_offset,
				val,
				((val & DMA0_CORE_STS0_BUSY_MASK) == 0),
				1000,
				ECC_SCRUBBING_TIMEOUT_US);

			if (rc) {
				hl_err(hdev,
					"DMA Timeout during ECC scrubbing of DMA #%d\n",
					dma_id);
				return -EIO;
			}
		}
	}

	return 0;
}
*/

/**
 * gaudi_init_hbm - Initialize HBM controller of the chip
 *
 * @hdev: pointer to hl_device structure
 *
 * Return: 0 for success, negative value for failure
 *
 */
int gaudi_init_hbm(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	int dev;

	if (!hdev->dram_enable || (hdev->fw_components & FW_TYPE_BOOT_CPU))
		return 0;

	if (gaudi->hw_cap_initialized & HW_CAP_HBM)
		return 0;

	if (hdev->pldm) {
		gaudi_pldm_init_hbm_bank(hdev, 0);
		gaudi_pldm_init_hbm_bank(hdev, 1);
		gaudi_pldm_init_hbm_bank(hdev, 2);
		gaudi_pldm_init_hbm_bank(hdev, 3);
		goto out;
	}

	gaudi->hbm_sdii_cnt = 0x80000; /* max 0x800000 */
	gaudi->hbm_continuous = 0;
	gaudi->hbm_ecc_enable = hdev->hbm_ecc_enable;
	gaudi->hbm_dbi_enable = 1;
	gaudi->hbm_acc_initial = 0x40;
	gaudi->hbm_inc = 1;
	gaudi->hbm_loop = 1;
	gaudi->hbm_interleave = 0;
	gaudi->hbm_internal_lb_length_usec = 1;
	gaudi->hbm_set_mode_reg_by_mlb_num = 0;
	gaudi->hbm_set_mode_reg_by_mlb_op = 0;
	/* For cfg_clk = 50MHz -> 0x3, 100MHz -> 0x7, 200MHz -> 0xF
	 * range 0x0-0x3F
	 */
	gaudi->hbm_training_delay = 7;
	gaudi->hbm_hardware_training_repeat = 1; /* range 1-4 */
	gaudi->hbm_debug = 0;
	gaudi->hbm_mode = hbm_mode_lfsr;
	gaudi->hbm_static_dll_aword = STATIC_DLL_AWORD;
	gaudi->hbm_static_dll_dword_wr = STATIC_DLL_DWORD_WR;
	gaudi->hbm_static_dll_dword_rd = STATIC_DLL_DWORD_RD_RISE;
	gaudi->hbm_freq = 950;

	for (dev = 0 ; dev < GAUDI_HBM_DEVICES ; dev++) {
		/* PHY requires 100ns delay from mc_rstn */
		udelay(1);

		phy_init(hdev, dev);
		dram_rstn(hdev, dev, cke_state_low);

		/* Internal Loopback (bring-up only) */
		if (internal_loopback_test(hdev, dev)) {
			hl_err(hdev,
				"HBM internal loopback failed, device: %d\n",
				dev);
			return -EIO;
		}

		hl_dbg(hdev,
			"HBM finished internal_loopback_test, device: %d\n",
			dev);

		/* IO connectivity test (bring-up only) */
		if (lane_detection(hdev, dev)) {
			hl_err(hdev,
				"HBM lane detection failed, device: %d\n",
				dev);
			return -EIO;
		}
		dram_rstn(hdev, dev, cke_state_low);

		/* Lane remapping */
		lane_remap(hdev, dev);

		/* Training */
		hardware_training(hdev, dev);
		hl_dbg(hdev,
				"hardware training finished, device: %d\n",
				dev);
		print_training_result(hdev, dev);
		hl_dbg(hdev, "printed training results, device: %d\n",
				dev);

		if (gaudi->hbm_debug) {
			dbg_set_min_training_values(hdev, dev);
			dbg_set_max_training_values(hdev, dev);
		}

		/* Initialize MC for functional operation */
		init_mc(hdev, dev);

		hl_dbg(hdev, "HBM device %d init finished\n", dev);
	}

	/* MC BIST (validate Training result) */
	hl_info(hdev, "Verifying HBM access (MCBIST)");
	if (dbg_write_read_full(hdev, 0)) {
		if (read_interrupts(hdev, 0))
			hl_err(hdev, "Got INTERRUPTS after MCBIST\n");
		return -EIO;
	} else {
		if (read_interrupts(hdev, 0))
			hl_err(hdev, "Got INTERRUPTS after MCBIST\n");
	}

	/* If ECC enabled, perform ECC scrubbing */
	if (gaudi->hbm_ecc_enable) {
		if (init_ecc(hdev, 0))
			return -EIO;
		if (read_interrupts(hdev, 0))
			hl_err(hdev, "Got INTERRUPTS after ECC init\n");
	}

	/* Temperature readout */
	for (dev = 0 ; dev < GAUDI_HBM_DEVICES ; dev++)
		temperature_read(hdev, dev);

	/* Enable traffic to HBM */
	for (dev = 0 ; dev < GAUDI_HBM_DEVICES ; dev++)
		enable_traffic(hdev, dev);

out:
	gaudi->hw_cap_initialized |= HW_CAP_HBM;

	return 0;
}

void gaudi_init_pll(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u16 hbw_nr, hbw_nf, hbw_od, hbw_nb;
	u16 dma_mesh_nr, dma_mesh_nf, dma_mesh_od, dma_mesh_nb;
	u16 hbm_nr, hbm_nf, hbm_od, hbm_nb;
	u16 pci_nr, pci_nf, pci_od, pci_nb;
	u16 nic_nr, nic_nf, nic_od, nic_nb;
	u16 sram_nr, sram_nf, sram_od, sram_nb;
	u32 val;

	if (!hdev->config_pll)
		return;

	if (gaudi->hw_cap_initialized & HW_CAP_PLL)
		return;

	if (hdev->fw_components & FW_TYPE_BOOT_CPU) {
		hl_dbg(hdev,
			"Waiting 5s for u-boot before configuring PLLs\n");
		ssleep(5);
	}

	/*
	 * 100MHz - 0 31 15 15
	 * 600MHz - 0 47 3 23
	 * 1.2GHz - 0 47 1 23
	 * 1.4GHz - 0 55 1 27
	 */

	if (hdev->pldm) {
		hbw_nr = 0, hbw_nf = 63, hbw_od = 1, hbw_nb = 31;
		dma_mesh_nr = 0, dma_mesh_nf = 59;
		dma_mesh_od = 1, dma_mesh_nb = 29;
		/* SRAM - 900MHz*/
		sram_nr = 0, sram_nf = 35, sram_od = 1, sram_nb = 17;
		/* HBM - 950MHz */
		hbm_nr = 0, hbm_nf = 37, hbm_od = 1, hbm_nb = 18;
		/* PCI - 950MHz */
		/*pci_nr = 0, pci_nf = 37, pci_od = 1, pci_nb = 18;*/
		/* PCI - 830MHz */
		pci_nr = 4, pci_nf = 331, pci_od = 3, pci_nb = 165;
		nic_nr = 24, nic_nf = 1665, nic_od = 3, nic_nb = 832;
	} else {
		hbw_nr = 0, hbw_nf = 63, hbw_od = 1, hbw_nb = 31;
		dma_mesh_nr = 0, dma_mesh_nf = 59;
		dma_mesh_od = 1, dma_mesh_nb = 29;
		/* SRAM - 900MHz*/
		sram_nr = 0, sram_nf = 35, sram_od = 1, sram_nb = 17;
		/* HBM - 950MHz */
		hbm_nr = 0, hbm_nf = 37, hbm_od = 1, hbm_nb = 18;
		/* PCI - 950MHz */
		/*pci_nr = 0, pci_nf = 37, pci_od = 1, pci_nb = 18;*/
		/* PCI - 830MHz */
		pci_nr = 4, pci_nf = 331, pci_od = 3, pci_nb = 165;
		nic_nr = 24, nic_nf = 1665, nic_od = 3, nic_nb = 832;
	}

	val = RREG32(mmPSOC_GLOBAL_CONF_COLD_RST_FLOPS_0);
	if (val) {
		hl_dbg(hdev, "Configuring only clock switching\n");
		WREG32(mmPSOC_PCI_PLL_DIV_SEL_0, 0x1);
		WREG32(mmPSOC_PCI_PLL_DIV_SEL_1, 0x1);
		WREG32(mmPSOC_PCI_PLL_DIV_SEL_2, 0x3);
		WREG32(mmPSOC_PCI_PLL_DIV_SEL_3, 0x3);
		WREG32(mmSRAM_E_PLL_DIV_SEL_0, 0x1);
		WREG32(mmSRAM_W_PLL_DIV_SEL_0, 0x1);

		if (!(hdev->fw_components & FW_TYPE_BOOT_CPU)) {
			WREG32(mmPSOC_HBM_PLL_DIV_SEL_0, 0x1);
			WREG32(mmPSOC_HBM_PLL_DIV_SEL_1, 0x3);
			WREG32(mmNIC2_HBM_PLL_DIV_SEL_0, 0x1);
			WREG32(mmNIC2_HBM_PLL_DIV_SEL_1, 0x3);
		}

		if (!hdev->pldm ||
			(hdev->cn.ports_mask & GAUDI_NIC_MASK_NIC0)) {
			WREG32(mmNIC0_PLL_DIV_SEL_0, 0x1);
			WREG32(mmNIC0_PLL_DIV_SEL_1, 0x3);
			WREG32(mmNIC0_PLL_DIV_SEL_2, 0x3);
		}

		WREG32(mmNIC1_PLL_DIV_SEL_0, 0x1);
		WREG32(mmNIC1_PLL_DIV_SEL_1, 0x3);
		WREG32(mmNIC1_PLL_DIV_SEL_2, 0x3);
		WREG32(mmDMA_E_PLL_DIV_SEL_0, 0x1);
		WREG32(mmDMA_E_PLL_DIV_SEL_1, 0x3);
		WREG32(mmDMA_W_PLL_DIV_SEL_0, 0x1);
		WREG32(mmDMA_W_PLL_DIV_SEL_1, 0x3);
		WREG32(mmMESH_E_PLL_DIV_SEL_0, 0x1);
		WREG32(mmMESH_E_PLL_DIV_SEL_1, 0x3);
		WREG32(mmMESH_E_PLL_DIV_SEL_2, 0x3);
		WREG32(mmMESH_E_PLL_DIV_SEL_3, 0x3);
		WREG32(mmMESH_W_PLL_DIV_SEL_0, 0x1);
		WREG32(mmMESH_W_PLL_DIV_SEL_1, 0x3);
		WREG32(mmMESH_W_PLL_DIV_SEL_2, 0x3);
		WREG32(mmMESH_W_PLL_DIV_SEL_3, 0x3);
		WREG32(mmPSOC_MME_PLL_DIV_SEL_0, 0x1);
		WREG32(mmPSOC_MME_PLL_DIV_SEL_1, 0x3);
		WREG32(mmPSOC_MME_PLL_DIV_SEL_2, 0x3);
		WREG32(mmPSOC_MME_PLL_DIV_SEL_3, 0x3);
		WREG32(mmNIC2_MME_PLL_DIV_SEL_0, 0x1);
		WREG32(mmNIC2_MME_PLL_DIV_SEL_1, 0x3);
		WREG32(mmNIC2_MME_PLL_DIV_SEL_2, 0x3);
		WREG32(mmNIC2_MME_PLL_DIV_SEL_3, 0x3);
		WREG32(mmPSOC_TPC_PLL_DIV_SEL_0, 0x1);
		WREG32(mmPSOC_TPC_PLL_DIV_SEL_1, 0x3);
		WREG32(mmPSOC_TPC_PLL_DIV_SEL_2, 0x3);
		WREG32(mmPSOC_TPC_PLL_DIV_SEL_3, 0x3);
		WREG32(mmNIC2_TPC_PLL_DIV_SEL_0, 0x1);
		WREG32(mmNIC2_TPC_PLL_DIV_SEL_1, 0x3);
		WREG32(mmNIC2_TPC_PLL_DIV_SEL_2, 0x3);
		WREG32(mmNIC2_TPC_PLL_DIV_SEL_3, 0x3);
		WREG32(mmIF_E_PLL_DIV_SEL_0, 0x1);
		WREG32(mmIF_E_PLL_DIV_SEL_1, 0x3);
		WREG32(mmIF_E_PLL_DIV_SEL_2, 0x3);
		WREG32(mmIF_E_PLL_DIV_SEL_3, 0x3);
		WREG32(mmIF_W_PLL_DIV_SEL_0, 0x1);
		WREG32(mmIF_W_PLL_DIV_SEL_1, 0x3);
		WREG32(mmIF_W_PLL_DIV_SEL_2, 0x3);
		WREG32(mmIF_W_PLL_DIV_SEL_3, 0x3);

		gaudi->hw_cap_initialized |= HW_CAP_PLL;
		return;
	}

	hl_dbg(hdev, "Configure PCI PLL\n");

	WREG32(mmPSOC_PCI_PLL_RST, 1);
	WREG32(mmPSOC_PCI_PLL_NR, pci_nr);
	WREG32(mmPSOC_PCI_PLL_NF, pci_nf);
	WREG32(mmPSOC_PCI_PLL_OD, pci_od);
	WREG32(mmPSOC_PCI_PLL_NB, pci_nb);
	WREG32(mmPSOC_PCI_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmPSOC_PCI_PLL_RST, 0);
	udelay(1000);

	WREG32(mmPSOC_PCI_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmPSOC_PCI_PLL_DIV_SEL_0, 0x1);

	WREG32(mmPSOC_PCI_PLL_DIV_EN_1, 0x1);
	if ((hdev->compatibility_mode) || (gaudi->compat_mode))
		WREG32(mmPSOC_PCI_PLL_DIV_FACTOR_1, 0x1);
	else
		WREG32(mmPSOC_PCI_PLL_DIV_FACTOR_1, 0);
	WREG32(mmPSOC_PCI_PLL_DIV_FACTOR_CMD_1, 0x1);
	udelay(1000);
	if ((hdev->compatibility_mode) || (gaudi->compat_mode))
		WREG32(mmPSOC_PCI_PLL_DIV_SEL_1, 0x3);
	else
		WREG32(mmPSOC_PCI_PLL_DIV_SEL_1, 0x1);

	WREG32(mmPSOC_PCI_PLL_DIV_EN_2, 0x1);
	WREG32(mmPSOC_PCI_PLL_DIV_FACTOR_2, 4);
	WREG32(mmPSOC_PCI_PLL_DIV_FACTOR_CMD_2, 0x1);
	udelay(1000);
	WREG32(mmPSOC_PCI_PLL_DIV_SEL_2, 0x3);

	WREG32(mmPSOC_PCI_PLL_DIV_EN_3, 0x1);
	WREG32(mmPSOC_PCI_PLL_DIV_FACTOR_3, 9);
	WREG32(mmPSOC_PCI_PLL_DIV_FACTOR_CMD_3, 0x1);
	udelay(1000);
	WREG32(mmPSOC_PCI_PLL_DIV_SEL_3, 0x3);

	hl_dbg(hdev, "Configure SRAM PLL\n");

	WREG32(mmSRAM_E_PLL_RST, 1);
	WREG32(mmSRAM_E_PLL_NR, sram_nr);
	WREG32(mmSRAM_E_PLL_NF, sram_nf);
	WREG32(mmSRAM_E_PLL_OD, sram_od);
	WREG32(mmSRAM_E_PLL_NB, sram_nb);
	WREG32(mmSRAM_E_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmSRAM_E_PLL_RST, 0);
	udelay(1000);

	WREG32(mmSRAM_E_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmSRAM_E_PLL_DIV_SEL_0, 0x1);

	WREG32(mmSRAM_W_PLL_RST, 1);
	WREG32(mmSRAM_W_PLL_NR, sram_nr);
	WREG32(mmSRAM_W_PLL_NF, sram_nf);
	WREG32(mmSRAM_W_PLL_OD, sram_od);
	WREG32(mmSRAM_W_PLL_NB, sram_nb);
	WREG32(mmSRAM_W_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmSRAM_W_PLL_RST, 0);
	udelay(1000);

	WREG32(mmSRAM_W_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmSRAM_W_PLL_DIV_SEL_0, 0x1);

	if (!(hdev->fw_components & FW_TYPE_BOOT_CPU)) {
		hl_dbg(hdev, "Configure HBM PLL\n");

		WREG32(mmPSOC_HBM_PLL_RST, 1);

		WREG32(mmPSOC_HBM_PLL_NR, hbm_nr);
		WREG32(mmPSOC_HBM_PLL_NF, hbm_nf);
		WREG32(mmPSOC_HBM_PLL_OD, hbm_od);
		WREG32(mmPSOC_HBM_PLL_NB, hbm_nb);
		WREG32(mmPSOC_HBM_PLL_DATA_CHNG, 0x11);
		udelay(1000);
		WREG32(mmPSOC_HBM_PLL_RST, 0);
		udelay(1000);

		WREG32(mmPSOC_HBM_PLL_DIV_EN_0, 0x1);
		udelay(1000);
		WREG32(mmPSOC_HBM_PLL_DIV_SEL_0, 0x1);

		WREG32(mmPSOC_HBM_PLL_DIV_EN_1, 0x1);
		WREG32(mmPSOC_HBM_PLL_DIV_FACTOR_1, 1);
		WREG32(mmPSOC_HBM_PLL_DIV_FACTOR_CMD_1, 0x1);
		udelay(1000);
		WREG32(mmPSOC_HBM_PLL_DIV_SEL_1, 0x3);

		WREG32(mmNIC2_HBM_PLL_RST, 1);
		WREG32(mmNIC2_HBM_PLL_NR, hbm_nr);
		WREG32(mmNIC2_HBM_PLL_NF, hbm_nf);
		WREG32(mmNIC2_HBM_PLL_OD, hbm_od);
		WREG32(mmNIC2_HBM_PLL_NB, hbm_nb);
		WREG32(mmNIC2_HBM_PLL_DATA_CHNG, 0x11);
		udelay(1000);
		WREG32(mmNIC2_HBM_PLL_RST, 0);
		udelay(1000);

		WREG32(mmNIC2_HBM_PLL_DIV_EN_0, 0x1);
		udelay(1000);
		WREG32(mmNIC2_HBM_PLL_DIV_SEL_0, 0x1);

		WREG32(mmNIC2_HBM_PLL_DIV_EN_1, 0x1);
		WREG32(mmNIC2_HBM_PLL_DIV_FACTOR_1, 1);
		WREG32(mmNIC2_HBM_PLL_DIV_FACTOR_CMD_1, 0x1);
		udelay(1000);
		WREG32(mmNIC2_HBM_PLL_DIV_SEL_1, 0x3);
	}

	if (!hdev->pldm || (hdev->cn.ports_mask & GAUDI_NIC_MASK_NIC0)) {
		hl_dbg(hdev, "Configure NIC0 PLL\n");

		WREG32(mmNIC0_PLL_RST, 1);
		WREG32(mmNIC0_PLL_NR, nic_nr);
		WREG32(mmNIC0_PLL_NF, nic_nf);
		WREG32(mmNIC0_PLL_OD, nic_od);
		WREG32(mmNIC0_PLL_NB, nic_nb);
		WREG32(mmNIC0_PLL_DATA_CHNG, 0x11);
		udelay(1000);
		WREG32(mmNIC0_PLL_RST, 0);
		udelay(1000);

		WREG32(mmNIC0_PLL_DIV_EN_0, 0x1);
		udelay(1000);
		WREG32(mmNIC0_PLL_DIV_SEL_0, 0x1);

		WREG32(mmNIC0_PLL_DIV_EN_1, 0x1);
		WREG32(mmNIC0_PLL_DIV_FACTOR_1, 4);
		WREG32(mmNIC0_PLL_DIV_FACTOR_CMD_1, 0x1);
		udelay(1000);
		WREG32(mmNIC0_PLL_DIV_SEL_1, 0x3);

		WREG32(mmNIC0_PLL_DIV_EN_2, 0x1);
		WREG32(mmNIC0_PLL_DIV_FACTOR_2, 8);
		WREG32(mmNIC0_PLL_DIV_FACTOR_CMD_2, 0x1);
		udelay(1000);
		WREG32(mmNIC0_PLL_DIV_SEL_2, 0x3);
	}

	hl_dbg(hdev, "Configure NIC1 PLL\n");

	WREG32(mmNIC1_PLL_RST, 1);
	WREG32(mmNIC1_PLL_NR, nic_nr);
	WREG32(mmNIC1_PLL_NF, nic_nf);
	WREG32(mmNIC1_PLL_OD, nic_od);
	WREG32(mmNIC1_PLL_NB, nic_nb);
	WREG32(mmNIC1_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmNIC1_PLL_RST, 0);
	udelay(1000);

	WREG32(mmNIC1_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmNIC1_PLL_DIV_SEL_0, 0x1);

	WREG32(mmNIC1_PLL_DIV_EN_1, 0x1);
	WREG32(mmNIC1_PLL_DIV_FACTOR_1, 4);
	WREG32(mmNIC1_PLL_DIV_FACTOR_CMD_1, 0x1);
	udelay(1000);
	WREG32(mmNIC1_PLL_DIV_SEL_1, 0x3);

	WREG32(mmNIC1_PLL_DIV_EN_2, 0x1);
	WREG32(mmNIC1_PLL_DIV_FACTOR_2, 8);
	WREG32(mmNIC1_PLL_DIV_FACTOR_CMD_2, 0x1);
	udelay(1000);
	WREG32(mmNIC1_PLL_DIV_SEL_2, 0x3);

	hl_dbg(hdev, "Configure DMA PLL\n");

	WREG32(mmDMA_E_PLL_RST, 1);
	WREG32(mmDMA_E_PLL_NR, dma_mesh_nr);
	WREG32(mmDMA_E_PLL_NF, dma_mesh_nf);
	WREG32(mmDMA_E_PLL_OD, dma_mesh_od);
	WREG32(mmDMA_E_PLL_NB, dma_mesh_nb);
	WREG32(mmDMA_E_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmDMA_E_PLL_RST, 0);
	udelay(1000);

	WREG32(mmDMA_E_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmDMA_E_PLL_DIV_SEL_0, 0x1);

	WREG32(mmDMA_E_PLL_DIV_EN_1, 0x1);
	WREG32(mmDMA_E_PLL_DIV_FACTOR_1, 1);
	WREG32(mmDMA_E_PLL_DIV_FACTOR_CMD_1, 0x1);
	udelay(1000);
	WREG32(mmDMA_E_PLL_DIV_SEL_1, 0x3);

	WREG32(mmDMA_W_PLL_RST, 1);
	WREG32(mmDMA_W_PLL_NR, dma_mesh_nr);
	WREG32(mmDMA_W_PLL_NF, dma_mesh_nf);
	WREG32(mmDMA_W_PLL_OD, dma_mesh_od);
	WREG32(mmDMA_W_PLL_NB, dma_mesh_nb);
	WREG32(mmDMA_W_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmDMA_W_PLL_RST, 0);
	udelay(1000);

	WREG32(mmDMA_W_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmDMA_W_PLL_DIV_SEL_0, 0x1);

	WREG32(mmDMA_W_PLL_DIV_EN_1, 0x1);
	WREG32(mmDMA_W_PLL_DIV_FACTOR_1, 1);
	WREG32(mmDMA_W_PLL_DIV_FACTOR_CMD_1, 0x1);
	udelay(1000);
	WREG32(mmDMA_W_PLL_DIV_SEL_1, 0x3);

	hl_dbg(hdev, "Configure MESH PLL\n");

	WREG32(mmMESH_E_PLL_RST, 1);
	WREG32(mmMESH_E_PLL_NR, dma_mesh_nr);
	WREG32(mmMESH_E_PLL_NF, dma_mesh_nf);
	WREG32(mmMESH_E_PLL_OD, dma_mesh_od);
	WREG32(mmMESH_E_PLL_NB, dma_mesh_nb);
	WREG32(mmMESH_E_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmMESH_E_PLL_RST, 0);
	udelay(1000);

	WREG32(mmMESH_E_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmMESH_E_PLL_DIV_SEL_0, 0x1);

	WREG32(mmMESH_E_PLL_DIV_EN_1, 0x1);
	WREG32(mmMESH_E_PLL_DIV_FACTOR_1, 1);
	WREG32(mmMESH_E_PLL_DIV_FACTOR_CMD_1, 0x1);
	udelay(1000);
	WREG32(mmMESH_E_PLL_DIV_SEL_1, 0x3);

	WREG32(mmMESH_E_PLL_DIV_EN_2, 0x1);
	WREG32(mmMESH_E_PLL_DIV_FACTOR_2, 2);
	WREG32(mmMESH_E_PLL_DIV_FACTOR_CMD_2, 0x1);
	udelay(1000);
	WREG32(mmMESH_E_PLL_DIV_SEL_2, 0x3);

	WREG32(mmMESH_E_PLL_DIV_EN_3, 0x1);
	WREG32(mmMESH_E_PLL_DIV_FACTOR_3, 7);
	WREG32(mmMESH_E_PLL_DIV_FACTOR_CMD_3, 0x1);
	udelay(1000);
	WREG32(mmMESH_E_PLL_DIV_SEL_3, 0x3);

	WREG32(mmMESH_W_PLL_RST, 1);
	WREG32(mmMESH_W_PLL_NR, dma_mesh_nr);
	WREG32(mmMESH_W_PLL_NF, dma_mesh_nf);
	WREG32(mmMESH_W_PLL_OD, dma_mesh_od);
	WREG32(mmMESH_W_PLL_NB, dma_mesh_nb);
	WREG32(mmMESH_W_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmMESH_W_PLL_RST, 0);
	udelay(1000);

	WREG32(mmMESH_W_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmMESH_W_PLL_DIV_SEL_0, 0x1);

	WREG32(mmMESH_W_PLL_DIV_EN_1, 0x1);
	WREG32(mmMESH_W_PLL_DIV_FACTOR_1, 1);
	WREG32(mmMESH_W_PLL_DIV_FACTOR_CMD_1, 0x1);
	udelay(1000);
	WREG32(mmMESH_W_PLL_DIV_SEL_1, 0x3);

	WREG32(mmMESH_W_PLL_DIV_EN_2, 0x1);
	WREG32(mmMESH_W_PLL_DIV_FACTOR_2, 2);
	WREG32(mmMESH_W_PLL_DIV_FACTOR_CMD_2, 0x1);
	udelay(1000);
	WREG32(mmMESH_W_PLL_DIV_SEL_2, 0x3);

	WREG32(mmMESH_W_PLL_DIV_EN_3, 0x1);
	WREG32(mmMESH_W_PLL_DIV_FACTOR_3, 7);
	WREG32(mmMESH_W_PLL_DIV_FACTOR_CMD_3, 0x1);
	udelay(1000);
	WREG32(mmMESH_W_PLL_DIV_SEL_3, 0x3);

	hl_dbg(hdev, "Configure MME PLL\n");

	WREG32(mmPSOC_MME_PLL_RST, 1);
	WREG32(mmPSOC_MME_PLL_NR, hbw_nr);
	WREG32(mmPSOC_MME_PLL_NF, hbw_nf);
	WREG32(mmPSOC_MME_PLL_OD, hbw_od);
	WREG32(mmPSOC_MME_PLL_NB, hbw_nb);
	WREG32(mmPSOC_MME_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmPSOC_MME_PLL_RST, 0);
	udelay(1000);

	WREG32(mmPSOC_MME_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmPSOC_MME_PLL_DIV_SEL_0, 0x1);

	WREG32(mmPSOC_MME_PLL_DIV_EN_1, 0x1);
	WREG32(mmPSOC_MME_PLL_DIV_FACTOR_1, 1);
	WREG32(mmPSOC_MME_PLL_DIV_FACTOR_CMD_1, 0x1);
	udelay(1000);
	WREG32(mmPSOC_MME_PLL_DIV_SEL_1, 0x3);

	WREG32(mmPSOC_MME_PLL_DIV_EN_2, 0x1);
	WREG32(mmPSOC_MME_PLL_DIV_FACTOR_2, 2);
	WREG32(mmPSOC_MME_PLL_DIV_FACTOR_CMD_2, 0x1);
	udelay(1000);
	WREG32(mmPSOC_MME_PLL_DIV_SEL_2, 0x3);

	WREG32(mmPSOC_MME_PLL_DIV_EN_3, 0x1);
	WREG32(mmPSOC_MME_PLL_DIV_FACTOR_3, 7);
	WREG32(mmPSOC_MME_PLL_DIV_FACTOR_CMD_3, 0x1);
	udelay(1000);
	WREG32(mmPSOC_MME_PLL_DIV_SEL_3, 0x3);

	WREG32(mmNIC2_MME_PLL_RST, 1);
	WREG32(mmNIC2_MME_PLL_NR, hbw_nr);
	WREG32(mmNIC2_MME_PLL_NF, hbw_nf);
	WREG32(mmNIC2_MME_PLL_OD, hbw_od);
	WREG32(mmNIC2_MME_PLL_NB, hbw_nb);
	WREG32(mmNIC2_MME_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmNIC2_MME_PLL_RST, 0);
	udelay(1000);

	WREG32(mmNIC2_MME_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmNIC2_MME_PLL_DIV_SEL_0, 0x1);

	WREG32(mmNIC2_MME_PLL_DIV_EN_1, 0x1);
	WREG32(mmNIC2_MME_PLL_DIV_FACTOR_1, 1);
	WREG32(mmNIC2_MME_PLL_DIV_FACTOR_CMD_1, 0x1);
	udelay(1000);
	WREG32(mmNIC2_MME_PLL_DIV_SEL_1, 0x3);

	WREG32(mmNIC2_MME_PLL_DIV_EN_2, 0x1);
	WREG32(mmNIC2_MME_PLL_DIV_FACTOR_2, 2);
	WREG32(mmNIC2_MME_PLL_DIV_FACTOR_CMD_2, 0x1);
	udelay(1000);
	WREG32(mmNIC2_MME_PLL_DIV_SEL_2, 0x3);

	WREG32(mmNIC2_MME_PLL_DIV_EN_3, 0x1);
	WREG32(mmNIC2_MME_PLL_DIV_FACTOR_3, 7);
	WREG32(mmNIC2_MME_PLL_DIV_FACTOR_CMD_3, 0x1);
	udelay(1000);
	WREG32(mmNIC2_MME_PLL_DIV_SEL_3, 0x3);

	hl_dbg(hdev, "Configure TPC PLL\n");

	WREG32(mmPSOC_TPC_PLL_RST, 1);
	WREG32(mmPSOC_TPC_PLL_NR, hbw_nr);
	WREG32(mmPSOC_TPC_PLL_NF, hbw_nf);
	WREG32(mmPSOC_TPC_PLL_OD, hbw_od);
	WREG32(mmPSOC_TPC_PLL_NB, hbw_nb);
	WREG32(mmPSOC_TPC_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmPSOC_TPC_PLL_RST, 0);
	udelay(1000);

	WREG32(mmPSOC_TPC_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmPSOC_TPC_PLL_DIV_SEL_0, 0x1);

	WREG32(mmPSOC_TPC_PLL_DIV_EN_1, 0x1);
	WREG32(mmPSOC_TPC_PLL_DIV_FACTOR_1, 1);
	WREG32(mmPSOC_TPC_PLL_DIV_FACTOR_CMD_1, 0x1);
	udelay(1000);
	WREG32(mmPSOC_TPC_PLL_DIV_SEL_1, 0x3);

	WREG32(mmPSOC_TPC_PLL_DIV_EN_2, 0x1);
	WREG32(mmPSOC_TPC_PLL_DIV_FACTOR_2, 2);
	WREG32(mmPSOC_TPC_PLL_DIV_FACTOR_CMD_2, 0x1);
	udelay(1000);
	WREG32(mmPSOC_TPC_PLL_DIV_SEL_2, 0x3);

	WREG32(mmPSOC_TPC_PLL_DIV_EN_3, 0x1);
	WREG32(mmPSOC_TPC_PLL_DIV_FACTOR_3, 7);
	WREG32(mmPSOC_TPC_PLL_DIV_FACTOR_CMD_3, 0x1);
	udelay(1000);
	WREG32(mmPSOC_TPC_PLL_DIV_SEL_3, 0x3);

	WREG32(mmNIC2_TPC_PLL_RST, 1);
	WREG32(mmNIC2_TPC_PLL_NR, hbw_nr);
	WREG32(mmNIC2_TPC_PLL_NF, hbw_nf);
	WREG32(mmNIC2_TPC_PLL_OD, hbw_od);
	WREG32(mmNIC2_TPC_PLL_NB, hbw_nb);
	WREG32(mmNIC2_TPC_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmNIC2_TPC_PLL_RST, 0);
	udelay(1000);

	WREG32(mmNIC2_TPC_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmNIC2_TPC_PLL_DIV_SEL_0, 0x1);

	WREG32(mmNIC2_TPC_PLL_DIV_EN_1, 0x1);
	WREG32(mmNIC2_TPC_PLL_DIV_FACTOR_1, 1);
	WREG32(mmNIC2_TPC_PLL_DIV_FACTOR_CMD_1, 0x1);
	udelay(1000);
	WREG32(mmNIC2_TPC_PLL_DIV_SEL_1, 0x3);

	WREG32(mmNIC2_TPC_PLL_DIV_EN_2, 0x1);
	WREG32(mmNIC2_TPC_PLL_DIV_FACTOR_2, 2);
	WREG32(mmNIC2_TPC_PLL_DIV_FACTOR_CMD_2, 0x1);
	udelay(1000);
	WREG32(mmNIC2_TPC_PLL_DIV_SEL_2, 0x3);

	WREG32(mmNIC2_TPC_PLL_DIV_EN_3, 0x1);
	WREG32(mmNIC2_TPC_PLL_DIV_FACTOR_3, 7);
	WREG32(mmNIC2_TPC_PLL_DIV_FACTOR_CMD_3, 0x1);
	udelay(1000);
	WREG32(mmNIC2_TPC_PLL_DIV_SEL_3, 0x3);

	hl_dbg(hdev, "Configure IF PLL\n");

	WREG32(mmIF_E_PLL_RST, 1);
	WREG32(mmIF_E_PLL_NR, hbw_nr);
	WREG32(mmIF_E_PLL_NF, hbw_nf);
	WREG32(mmIF_E_PLL_OD, hbw_od);
	WREG32(mmIF_E_PLL_NB, hbw_nb);
	WREG32(mmIF_E_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmIF_E_PLL_RST, 0);
	udelay(1000);

	WREG32(mmIF_E_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmIF_E_PLL_DIV_SEL_0, 0x1);

	WREG32(mmIF_E_PLL_DIV_EN_1, 0x1);
	WREG32(mmIF_E_PLL_DIV_FACTOR_1, 1);
	WREG32(mmIF_E_PLL_DIV_FACTOR_CMD_1, 0x1);
	udelay(1000);
	WREG32(mmIF_E_PLL_DIV_SEL_1, 0x3);

	WREG32(mmIF_E_PLL_DIV_EN_2, 0x1);
	WREG32(mmIF_E_PLL_DIV_FACTOR_2, 2);
	WREG32(mmIF_E_PLL_DIV_FACTOR_CMD_2, 0x1);
	udelay(1000);
	WREG32(mmIF_E_PLL_DIV_SEL_2, 0x3);

	WREG32(mmIF_E_PLL_DIV_EN_3, 0x1);
	WREG32(mmIF_E_PLL_DIV_FACTOR_3, 7);
	WREG32(mmIF_E_PLL_DIV_FACTOR_CMD_3, 0x1);
	udelay(1000);
	WREG32(mmIF_E_PLL_DIV_SEL_3, 0x3);

	WREG32(mmIF_W_PLL_RST, 1);
	WREG32(mmIF_W_PLL_NR, hbw_nr);
	WREG32(mmIF_W_PLL_NF, hbw_nf);
	WREG32(mmIF_W_PLL_OD, hbw_od);
	WREG32(mmIF_W_PLL_NB, hbw_nb);
	WREG32(mmIF_W_PLL_DATA_CHNG, 0x11);
	udelay(1000);
	WREG32(mmIF_W_PLL_RST, 0);
	udelay(1000);

	WREG32(mmIF_W_PLL_DIV_EN_0, 0x1);
	udelay(1000);
	WREG32(mmIF_W_PLL_DIV_SEL_0, 0x1);

	WREG32(mmIF_W_PLL_DIV_EN_1, 0x1);
	WREG32(mmIF_W_PLL_DIV_FACTOR_1, 1);
	WREG32(mmIF_W_PLL_DIV_FACTOR_CMD_1, 0x1);
	udelay(1000);
	WREG32(mmIF_W_PLL_DIV_SEL_1, 0x3);

	WREG32(mmIF_W_PLL_DIV_EN_2, 0x1);
	WREG32(mmIF_W_PLL_DIV_FACTOR_2, 2);
	WREG32(mmIF_W_PLL_DIV_FACTOR_CMD_2, 0x1);
	udelay(1000);
	WREG32(mmIF_W_PLL_DIV_SEL_2, 0x3);

	WREG32(mmIF_W_PLL_DIV_EN_3, 0x1);
	WREG32(mmIF_W_PLL_DIV_FACTOR_3, 7);
	WREG32(mmIF_W_PLL_DIV_FACTOR_CMD_3, 0x1);
	udelay(1000);
	WREG32(mmIF_W_PLL_DIV_SEL_3, 0x3);

	WREG32(mmPSOC_GLOBAL_CONF_COLD_RST_FLOPS_0, 1);

	gaudi->hw_cap_initialized |= HW_CAP_PLL;
}

static int gaudi_load_boot_bin_to_device(struct hl_device *hdev)
{
	void __iomem *dst;

#ifdef HL_DOWNSTREAM
	/* For PLDM, need to initialize SRAM scrambler before pushing u-boot
	 * to SRAM
	 */
	gaudi_init_scrambler_sram(hdev);
#endif /* HL_DOWNSTREAM */

	dst = hdev->pcie_bar[SRAM_BAR_ID] + UBOOT_FW_OFFSET;

	return hl_fw_load_fw_to_device(hdev, GAUDI_UBOOT_FW_FILE, dst, 0, 0);
}

int gaudi_pldm_init_cpu(struct hl_device *hdev)
{
	int rc;

	/* Put ARM cores into reset */
	WREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL, CPU_RESET_ASSERT);
	RREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL);

	/* Reset the CA53 MACRO */
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N_L, 0);
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N_H, 1 << UNIT_RST_H_CPU_SHIFT);
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N,
			1 << PSOC_GLOBAL_CONF_UNIT_RST_N_IND_SHIFT);
	RREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N);

	usleep_range(50, 100);

	/* Take CA53 MACRO out of reset */
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N_H, 0);
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N,
			1 << PSOC_GLOBAL_CONF_UNIT_RST_N_IND_SHIFT);
	RREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N);

	if (hdev->fw_components & FW_TYPE_BOOT_CPU) {
		rc = gaudi_load_boot_bin_to_device(hdev);
		if (rc)
			return rc;
	}

	if (hdev->fw_components & FW_TYPE_LINUX) {
		rc = gaudi_load_firmware_to_device(hdev);
		if (rc)
			return rc;
		WREG32(mmPSOC_GLOBAL_CONF_KMD_MSG_TO_CPU, KMD_MSG_FIT_RDY);
	}

	WREG32(mmPSOC_GLOBAL_CONF_CPU_BOOT_STATUS, CPU_BOOT_STATUS_NA);

	WREG32(mmCPU_CA53_CFG_RST_ADDR_LSB_0,
			lower_32_bits(SRAM_BASE_ADDR + UBOOT_FW_OFFSET));
	WREG32(mmCPU_CA53_CFG_RST_ADDR_MSB_0,
			upper_32_bits(SRAM_BASE_ADDR + UBOOT_FW_OFFSET));

	/* Release ARM core 0 from reset */
	WREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL, CPU_RESET_CORE0_DEASSERT);
	RREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL);

	return 0;
}
