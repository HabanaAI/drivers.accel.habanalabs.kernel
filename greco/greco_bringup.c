// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2019-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "grecoP.h"
#include "greco_masks.h"

#define GRECO_UBOOT_FW_FILE    "habanalabs/greco/greco-u-boot.bin"

#define DDR_MACRO_NUM			8
#define DDR_CH_PER_MACRO_NUM		2
#define mmDCORE0_DDR0_MC0_SWSTAT	0x602C84
#define mmDCORE0_DDR0_MC0_DFISTAT	0x602514
#define mmDCORE0_DDR0_MC0_STAT		0x602014
#define DDR_CFG_BASE			mmDCORE0_DDR0_MC0_BASE

#define DDR_CFG_OFFSET			\
			(mmDCORE0_DDR1_MC0_BASE - mmDCORE0_DDR0_MC0_BASE)

#define DCORE0_DDR0_PHY				0xE00000
#define DCORE0_DDR1_PHY				0xE40000
#define DDR_CFG_PHY_OFFSET		(DCORE0_DDR1_PHY - DCORE0_DDR0_PHY)

#define GRECO_LPDDR_PHY_TIMEOUT_USEC		10000 /* 10ms */

#define MSTR_IF_HBW_LO_OVRD_DEFAULT		0xA3FFFFFF
#define MSTR_IF_HBW_LO_OVRD_REDUCTION_MASK	0x003FE000
#define MSTR_IF_HBW_LO_ASID_MMUBP_MASK		0x000007FF

#define MSTR_IF_LBW_OVRD_XY_MASK		0xFF000000
#define MSTR_IF_LBW_OVRD_SHARED_MASK		0x00800000
#define MSTR_IF_LBW_OVRD_LOCK_MASK		0x00000400

#define MSTR_IF_MME_LBW_MAX_OUTSTANDING_WR_TOTAL 15

#define MSTR_IF_LBW_OVRD_DEFAULT		MSTR_IF_LBW_OVRD_XY_MASK

#define GRECO_PLL_TIMEOUT_USEC			10000		/* 10ms */

#define RTR_CTRL_LBW_DECODE_BASE_ADDR_VAL_MASK \
					DCON0_LBW_RTR_IF_RTR_CTRL_LBW_DECODE_BASE_ADDR_VAL_MASK
#define RTR_CTRL_LBW_DECODE_CTRL_EN_MASK \
					DCON0_LBW_RTR_IF_RTR_CTRL_LBW_DECODE_CTRL_EN_MASK
#define RTR_CTRL_LBW_DECODE_CTRL_START_BIT_MASK \
					DCON0_LBW_RTR_IF_RTR_CTRL_LBW_DECODE_CTRL_START_BIT_MASK
#define RTR_CTRL_LBW_DECODE_CTRL_END_BIT_MASK \
					DCON0_LBW_RTR_IF_RTR_CTRL_LBW_DECODE_CTRL_END_BIT_MASK
#define RTR_CTRL_LBW_DECODE_CTRL_YX_MASK \
					DCON0_LBW_RTR_IF_RTR_CTRL_LBW_DECODE_CTRL_YX_MASK

#define NUM_OF_DCONS			4
#define NUM_OF_MME_TPC_RTR_PER_DCORE	4

#define RTR_LBW_DECODE_ENTRY_0_TPC	0
#define RTR_LBW_DECODE_ENTRY_1_DEC	1
#define RTR_LBW_DECODE_ENTRY_2_DEC	2
#define RTR_LBW_DECODE_ENTRY_3_DEC	3

/* The init is based on ddr_init_cfg_8g */

static int ecc = 1;

static const u32 greco_pll_block_bases[] = {
	[HL_GRECO_PCI_PLL]		= mmPSOC_PCI_PLL_CTRL_BASE,
	[HL_GRECO_SIF_PLL]		= mmPSOC_SIF_PLL_CTRL_BASE,
	[HL_GRECO_MESH_PLL]		= mmPSOC_MESH_PLL_CTRL_BASE,
	[HL_GRECO_DDR0_PLL]		= mmPMMU_DDR_PLL_CTRL_BASE,
	[HL_GRECO_DDR1_PLL]		= mmPSOC_DDR_PLL_CTRL_BASE,
	[HL_GRECO_MME_PLL]		= mmPSOC_MME_PLL_CTRL_BASE,
	[HL_GRECO_TPC_PLL]		= mmPSOC_TPC_PLL_CTRL_BASE,
	[HL_GRECO_VID_PLL]		= mmPSOC_VIDEO_PLL_CTRL_BASE,
	[HL_GRECO_SRAM_PLL]		= mmPSOC_BANK_PLL_CTRL_BASE,
	[HL_GRECO_MMU_PLL]		= mmPSOC_MMU_PLL_CTRL_BASE
};

static void greco_poll_swstat_sw_done_ack(struct hl_device *hdev, u64 offset)
{
	u32 reg_val;
	int rc;

	if (hdev->pldm)
		return;

	rc = hl_poll_timeout(
		hdev,
		offset + mmDCORE0_DDR0_MC0_SWSTAT,
		reg_val,
		((reg_val & 0x01) == 0),
		10,
		GRECO_LPDDR_PHY_TIMEOUT_USEC);
	if (rc)
		dev_err(hdev->dev, "Timed out waiting for DDR sw_done_ack\n");
}

static void greco_poll_dfi_init_complete(struct hl_device *hdev, u64 offset)
{
	u32 reg_val;
	int rc;

	if (hdev->pldm)
		return;

	rc = hl_poll_timeout(
		hdev,
		offset + mmDCORE0_DDR0_MC0_DFISTAT,
		reg_val,
		((reg_val & 0x01) == 0),
		10,
		GRECO_LPDDR_PHY_TIMEOUT_USEC);
	if (rc)
		dev_err(hdev->dev,
			"Timed out waiting for DDR dfi_init_complete\n");
}

static void greco_poll_normal_operation_mode(struct hl_device *hdev, u64 offset)
{
	u32 reg_val;
	int rc;

	if (hdev->pldm)
		return;

	rc = hl_poll_timeout(
		hdev,
		offset + mmDCORE0_DDR0_MC0_STAT,
		reg_val,
		((reg_val & 0x07) != 1),
		10,
		GRECO_LPDDR_PHY_TIMEOUT_USEC);
	if (rc)
		dev_err(hdev->dev,
			"Timed out waiting for DDR to become operational\n");
}

static noinline void greco_init_lpddr_macro(struct hl_device *hdev,
				   u64 ddr_macro_offset, u64 ddr_phy_offset)
{
	u64 offset = DDR_CFG_BASE + ddr_macro_offset;

	WREG32(offset + 0x0001f204, 0x1);
	WREG32(offset + 0x0001f204, 0x3);
	WREG32(offset + 0x0001f204, 0x2);
	WREG32(offset + 0x00002000, 0x3080008);
	WREG32(offset + 0x00012000, 0x3080008);
	WREG32(offset + 0x00002008, 0x0);
	WREG32(offset + 0x00012008, 0x0);
	WREG32(offset + 0x00002010, 0x1);
	WREG32(offset + 0x00012010, 0x1);
	WREG32(offset + 0x00002104, 0x1);
	WREG32(offset + 0x00012104, 0x1);
	WREG32(offset + 0x00002108, 0x1);
	WREG32(offset + 0x00012108, 0x1);
	WREG32(offset + 0x00002118, 0x0);
	WREG32(offset + 0x00012118, 0x0);
	WREG32(offset + 0x00002184, 0x0);
	WREG32(offset + 0x00012184, 0x0);
	WREG32(offset + 0x00002200, 0x100);
	WREG32(offset + 0x00012200, 0x100);
	WREG32(offset + 0x00002280, 0x80000000);
	WREG32(offset + 0x00012280, 0x80000000);
	WREG32(offset + 0x00002288, 0x1);
	WREG32(offset + 0x00012288, 0x1);
	WREG32(offset + 0x00002300, 0x400040);
	WREG32(offset + 0x00012300, 0x400040);
	WREG32(offset + 0x00002308, 0x0);
	WREG32(offset + 0x00012308, 0x0);
	WREG32(offset + 0x00002380, 0x3f1d);
	WREG32(offset + 0x00012380, 0x3f1d);
	WREG32(offset + 0x00002384, 0x2000);
	WREG32(offset + 0x00012384, 0x2000);
	WREG32(offset + 0x0000238c, 0x404021f);
	WREG32(offset + 0x0001238c, 0x404021f);
	WREG32(offset + 0x00002390, 0x8400810);
	WREG32(offset + 0x00012390, 0x8400810);
	WREG32(offset + 0x00002394, 0x2000010f);
	WREG32(offset + 0x00012394, 0x2000010f);
	WREG32(offset + 0x00002500, 0x100100);
	WREG32(offset + 0x00012500, 0x100100);
	WREG32(offset + 0x00002508, 0xe0000000);
	WREG32(offset + 0x00012508, 0xe0000000);
	WREG32(offset + 0x00002510, 0x5);
	WREG32(offset + 0x00012510, 0x5);
	WREG32(offset + 0x00002518, 0x0);
	WREG32(offset + 0x00012518, 0x0);
	WREG32(offset + 0x00002600, 0x13f7f10);
	WREG32(offset + 0x00012600, 0x13f7f10);
	WREG32(offset + 0x00002604, 0xfb0);
	WREG32(offset + 0x00012604, 0xfb0);
	WREG32(offset + 0x0000260c, 0x7041f);
	WREG32(offset + 0x0001260c, 0x7041f);
	WREG32(offset + 0x00002650, 0x0);
	WREG32(offset + 0x00012650, 0x0);

	if (ecc) {
		WREG32(offset + 0x00002980, 0x3);
		WREG32(offset + 0x00012980, 0x3);
		WREG32(offset + 0x00002984, 0x11);
		WREG32(offset + 0x00012984, 0x11);
		WREG32(offset + 0x00002c94, 0x3);
		WREG32(offset + 0x00012c94, 0x3);
		WREG32(offset + 0x00002c94, 0x3);
		WREG32(offset + 0x00012c94, 0x3);
	} else {
		WREG32(offset + 0x00002980, 0x0);
		WREG32(offset + 0x00012980, 0x0);
		WREG32(offset + 0x00002c94, 0x7);
		WREG32(offset + 0x00012c94, 0x7);
		WREG32(offset + 0x00002c94, 0x7);
		WREG32(offset + 0x00012c94, 0x7);
	}

	WREG32(offset + 0x00002b80, 0x0);
	WREG32(offset + 0x00012b80, 0x0);
	WREG32(offset + 0x00002c90, 0xf00f);
	WREG32(offset + 0x00012c90, 0xf00f);
	WREG32(offset + 0x00002ca0, 0x0);
	WREG32(offset + 0x00012ca0, 0x0);
	WREG32(offset + 0x00002d00, 0x40020002);
	WREG32(offset + 0x00012d00, 0x40020002);
	WREG32(offset + 0x00002d04, 0x30000);
	WREG32(offset + 0x00012d04, 0x30000);
	WREG32(offset + 0x00004004, 0x501f);
	WREG32(offset + 0x00014004, 0x501f);
	WREG32(offset + 0x00004008, 0x501f);
	WREG32(offset + 0x00014008, 0x501f);
	WREG32(offset + 0x00004094, 0x0);
	WREG32(offset + 0x00014094, 0x0);
	WREG32(offset + 0x00004098, 0x0);
	WREG32(offset + 0x00014098, 0x0);
	WREG32(offset + 0x0000409c, 0xe00);
	WREG32(offset + 0x0001409c, 0xe00);
	WREG32(offset + 0x000040a0, 0x0);
	WREG32(offset + 0x000140a0, 0x0);

	if (ecc) {
		WREG32(offset + 0x00000000, 0x2b100322);
		WREG32(offset + 0x00010000, 0x2b100322);
		WREG32(offset + 0x00000004, 0x60630);
		WREG32(offset + 0x00010004, 0x60630);
		WREG32(offset + 0x00000008, 0x913141a);
		WREG32(offset + 0x00010008, 0x913141a);
		WREG32(offset + 0x0000000c, 0xc2332);
		WREG32(offset + 0x0001000c, 0xc2332);
		WREG32(offset + 0x00000580, 0x647021f);
		WREG32(offset + 0x00010580, 0x647021f);
		WREG32(offset + 0x00000588, 0x471f);
		WREG32(offset + 0x00010588, 0x471f);
		WREG32(offset + 0x00000590, 0x200c0406);
		WREG32(offset + 0x00010590, 0x200c0406);
		WREG32(offset + 0x00000060, 0x12180e);
		WREG32(offset + 0x00010060, 0x12180e);
		WREG32(offset + 0x00000010, 0xf040412);
		WREG32(offset + 0x00010010, 0xf040412);
		WREG32(offset + 0x00000014, 0x2040c01);
		WREG32(offset + 0x00010014, 0x2040c01);
		WREG32(offset + 0x00000018, 0x8);
		WREG32(offset + 0x00010018, 0x8);
		WREG32(offset + 0x0000001c, 0x3);
		WREG32(offset + 0x0001001c, 0x3);
		WREG32(offset + 0x00000020, 0x4400);
		WREG32(offset + 0x00010020, 0x4400);
		WREG32(offset + 0x00000024, 0x20416);
		WREG32(offset + 0x00010024, 0x20416);
		WREG32(offset + 0x00000030, 0x30000);
		WREG32(offset + 0x00010030, 0x30000);
		WREG32(offset + 0x00000034, 0xc100002);
		WREG32(offset + 0x00010034, 0xc100002);
		WREG32(offset + 0x00000038, 0x96);
		WREG32(offset + 0x00010038, 0x96);
		WREG32(offset + 0x0000005c, 0x9d0fc1);
		WREG32(offset + 0x0001005c, 0x9d0fc1);
		WREG32(offset + 0x00000064, 0x2b06);
		WREG32(offset + 0x00010064, 0x2b06);
		WREG32(offset + 0x00000078, 0x1b141a);
		WREG32(offset + 0x00010078, 0x1b141a);
	} else {
		WREG32(offset + 0x00000000, 0x28100322);
		WREG32(offset + 0x00010000, 0x28100322);
		WREG32(offset + 0x00000004, 0x60630);
		WREG32(offset + 0x00010004, 0x60630);
		WREG32(offset + 0x00000008, 0x9121217);
		WREG32(offset + 0x00010008, 0x9121217);
		WREG32(offset + 0x0000000c, 0xc222f);
		WREG32(offset + 0x0001000c, 0xc222f);
		WREG32(offset + 0x00000580, 0x643021f);
		WREG32(offset + 0x00010580, 0x643021f);
		WREG32(offset + 0x00000588, 0x431f);
		WREG32(offset + 0x00010588, 0x431f);
		WREG32(offset + 0x00000590, 0x1c0c0406);
		WREG32(offset + 0x00010590, 0x1c0c0406);
		WREG32(offset + 0x00000060, 0x10160e);
		WREG32(offset + 0x00010060, 0x10160e);
		WREG32(offset + 0x00000010, 0xf040412);
		WREG32(offset + 0x00010010, 0xf040412);
		WREG32(offset + 0x00000014, 0x2040c01);
		WREG32(offset + 0x00010014, 0x2040c01);
		WREG32(offset + 0x00000018, 0x8);
		WREG32(offset + 0x00010018, 0x8);
		WREG32(offset + 0x0000001c, 0x3);
		WREG32(offset + 0x0001001c, 0x3);
		WREG32(offset + 0x00000020, 0x4400);
		WREG32(offset + 0x00010020, 0x4400);
		WREG32(offset + 0x00000024, 0x20412);
		WREG32(offset + 0x00010024, 0x20412);
		WREG32(offset + 0x00000030, 0x30000);
		WREG32(offset + 0x00010030, 0x30000);
		WREG32(offset + 0x00000034, 0xc100002);
		WREG32(offset + 0x00010034, 0xc100002);
		WREG32(offset + 0x00000038, 0x96);
		WREG32(offset + 0x00010038, 0x96);
		WREG32(offset + 0x0000005c, 0x9d0fc1);
		WREG32(offset + 0x0001005c, 0x9d0fc1);
		WREG32(offset + 0x00000064, 0x2806);
		WREG32(offset + 0x00010064, 0x2806);
		WREG32(offset + 0x00000078, 0x191218);
		WREG32(offset + 0x00010078, 0x191218);
	}

	WREG32(offset + 0x00000584, 0x140606);
	WREG32(offset + 0x00010584, 0x140606);
	WREG32(offset + 0x00000594, 0x4100006);
	WREG32(offset + 0x00010594, 0x4100006);
	WREG32(offset + 0x000005a0, 0x20202);
	WREG32(offset + 0x000105a0, 0x20202);
	WREG32(offset + 0x000005a4, 0x201);
	WREG32(offset + 0x000105a4, 0x201);
	WREG32(offset + 0x000005ac, 0x2100b1);
	WREG32(offset + 0x000105ac, 0x2100b1);
	WREG32(offset + 0x00000600, 0x82000186);
	WREG32(offset + 0x00010600, 0x82000186);
	WREG32(offset + 0x00000604, 0x70);
	WREG32(offset + 0x00010604, 0x70);
	WREG32(offset + 0x00000608, 0x6480000);
	WREG32(offset + 0x00010608, 0x6480000);
	WREG32(offset + 0x00000800, 0x1804ba);
	WREG32(offset + 0x00010800, 0x1804ba);
	WREG32(offset + 0x00000804, 0x2800070);
	WREG32(offset + 0x00010804, 0x2800070);
	WREG32(offset + 0x00000a80, 0x14f4);
	WREG32(offset + 0x00010a80, 0x14f4);
	WREG32(offset + 0x00000b00, 0x420a00f);
	WREG32(offset + 0x00010b00, 0x420a00f);
	WREG32(offset + 0x00000b04, 0x1024100a);
	WREG32(offset + 0x00010b04, 0x1024100a);
	WREG32(offset + 0x00000b08, 0x33);
	WREG32(offset + 0x00010b08, 0x33);
	WREG32(offset + 0x00000b80, 0xfc0000);
	WREG32(offset + 0x00010b80, 0xfc0000);
	WREG32(offset + 0x00000c80, 0xf000001);
	WREG32(offset + 0x00010c80, 0xf000001);
	WREG32(offset + 0x00000c84, 0x400001ff);
	WREG32(offset + 0x00010c84, 0x400001ff);
	WREG32(offset + 0x00000c88, 0x200003ff);
	WREG32(offset + 0x00010c88, 0x200003ff);
	WREG32(offset + 0x00000d00, 0x1);
	WREG32(offset + 0x00010d00, 0x1);
	WREG32(offset + 0x00000d04, 0xb07);
	WREG32(offset + 0x00010d04, 0xb07);
	WREG32(offset + 0x00000d08, 0x1202);
	WREG32(offset + 0x00010d08, 0x1202);
	WREG32(offset + 0x00000d0c, 0x400004);
	WREG32(offset + 0x00010d0c, 0x400004);
	WREG32(offset + 0x00006004, 0x13);
	WREG32(offset + 0x00016004, 0x13);
	WREG32(offset + 0x0000600c, 0x3f0404);
	WREG32(offset + 0x0001600c, 0x3f0404);
	WREG32(offset + 0x00006010, 0x606);
	WREG32(offset + 0x00016010, 0x606);
	WREG32(offset + 0x00006014, 0x1f131313);
	WREG32(offset + 0x00016014, 0x1f131313);
	WREG32(offset + 0x00006018, 0x0);
	WREG32(offset + 0x00016018, 0x0);
	WREG32(offset + 0x0000601c, 0x1f1f0909);
	WREG32(offset + 0x0001601c, 0x1f1f0909);
	WREG32(offset + 0x00006020, 0x5050505);
	WREG32(offset + 0x00016020, 0x5050505);
	WREG32(offset + 0x00006024, 0x5050505);
	WREG32(offset + 0x00016024, 0x5050505);
	WREG32(offset + 0x00006028, 0x5050505);
	WREG32(offset + 0x00016028, 0x5050505);
	WREG32(offset + 0x0000602c, 0x505);
	WREG32(offset + 0x0001602c, 0x505);
	WREG32(offset + 0x00006030, 0x3);
	WREG32(offset + 0x00016030, 0x3);
	WREG32(offset + 0x00002b84, 0x0);
	WREG32(offset + 0x00012b84, 0x0);
	WREG32(offset + 0x00004090, 0x1);
	WREG32(offset + 0x00014090, 0x1);
	WREG32(offset + 0x00002208, 0x1);
	WREG32(offset + 0x00012208, 0x1);
	WREG32(offset + 0x00002180, 0x20001);
	WREG32(offset + 0x00012180, 0x20001);
	WREG32(offset + 0x00002180, 0x20000);
	WREG32(offset + 0x00012180, 0x20000);
	WREG32(offset + 0x00002100, 0x8);
	WREG32(offset + 0x00012100, 0x8);
	WREG32(offset + 0x0001f204, 0x12);
	WREG32(offset + 0x00012c80, 0x0);
	WREG32(offset + 0x00002c80, 0x0);
	WREG32(offset + 0x00012510, 0x4);
	WREG32(offset + 0x00002510, 0x4);
	WREG32(offset + 0x00012c80, 0x1);
	WREG32(offset + 0x00002c80, 0x1);

	greco_poll_swstat_sw_done_ack(hdev, ddr_macro_offset); /* step 7 */

	WREG32(offset + 0x00012208, 0x0);
	WREG32(offset + 0x00002208, 0x0);
}

static noinline void greco_init_lpddr_macro1(struct hl_device *hdev,
				u64 ddr_macro_offset, u64 ddr_phy_offset)
{
	u64 offset = DDR_CFG_BASE + ddr_macro_offset;
	/* DDR_CFG_PHY_BASE is part of the hard-coded values */
	u64 phy_offset = ddr_phy_offset;

	if (hdev->pldm)
		return;

	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e00240, 0x1);
	WREG32(phy_offset + 0x00e00144, 0x3);
	WREG32(phy_offset + 0x00e00294, 0x1);
	WREG32(phy_offset + 0x00e00c00, 0x808);
	WREG32(phy_offset + 0x00e00c0c, 0x9);
	WREG32(phy_offset + 0x00e00c08, 0x26);
	WREG32(phy_offset + 0x00e00ca0, 0x0);
	WREG32(phy_offset + 0x00e00c04, 0x3);
	WREG32(phy_offset + 0x00e00c2c, 0x0);
	WREG32(offset + 0x0001f100, 0xc0000);
	WREG32(phy_offset + 0x00e3c29c, 0x0);
	WREG32(phy_offset + 0x00e3c2b4, 0x80);
	WREG32(phy_offset + 0x00e3c2b8, 0x80);
	WREG32(phy_offset + 0x00e3c2b0, 0x80);
	WREG32(offset + 0x0001f100, 0x300000);
	WREG32(phy_offset + 0x00e00200, 0x3);
	WREG32(phy_offset + 0x00e00218, 0x1);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e0046c, 0x0);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c28c, 0xb33);
	WREG32(phy_offset + 0x00e3c28c, 0xb3f);
	WREG32(offset + 0x0001f100, 0x300000);
	WREG32(phy_offset + 0x00e003c4, 0x6000);
	WREG32(phy_offset + 0x00e003cc, 0x8000);
	WREG32(phy_offset + 0x00e003d0, 0x5);
	WREG32(phy_offset + 0x00e003d4, 0x4000);
	WREG32(phy_offset + 0x00e003d8, 0x2);
	WREG32(phy_offset + 0x00e003dc, 0xf000);
	WREG32(phy_offset + 0x00e003e4, 0x6000);
	WREG32(phy_offset + 0x00e003ec, 0x8000);
	WREG32(phy_offset + 0x00e003f4, 0x4000);
	WREG32(phy_offset + 0x00e003fc, 0xf000);
	WREG32(offset + 0x0001f100, 0x240000);
	WREG32(phy_offset + 0x00e02008, 0x1);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e00130, 0x2e8f);
	WREG32(phy_offset + 0x00e00134, 0x1);
	WREG32(offset + 0x0001f100, 0x240000);
	WREG32(phy_offset + 0x00e023c0, 0x64);
	WREG32(phy_offset + 0x00e023c4, 0xc8);
	WREG32(phy_offset + 0x00e023c8, 0x7d0);
	WREG32(phy_offset + 0x00e023cc, 0x58);
	WREG32(phy_offset + 0x00e023d0, 0x14);
	WREG32(phy_offset + 0x00e023d4, 0x0);
	WREG32(phy_offset + 0x00e023d8, 0xb0);
	WREG32(phy_offset + 0x00e023dc, 0x12c);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e00008, 0x2);
	WREG32(phy_offset + 0x00e00000, 0x2);
	WREG32(phy_offset + 0x00e00058, 0x2);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c014, 0x0);
	WREG32(phy_offset + 0x00e3c02c, 0x0);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e0001c, 0x0);
	WREG32(phy_offset + 0x00e0004c, 0x2c);
	WREG32(offset + 0x0001f100, 0xc0000);
	WREG32(phy_offset + 0x00e3c0e0, 0x3);
	WREG32(phy_offset + 0x00e3c0e4, 0x3);
	WREG32(phy_offset + 0x00e3c0e8, 0x3);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c0e0, 0x3);
	WREG32(phy_offset + 0x00e3c0e8, 0x3);
	WREG32(phy_offset + 0x00e3c0ec, 0x3);
	WREG32(phy_offset + 0x00e3c010, 0x0);
	WREG32(phy_offset + 0x00e3c00c, 0x0);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e00010, 0x320);
	WREG32(offset + 0x0001f100, 0xc0000);
	WREG32(phy_offset + 0x00e3c140, 0x35);
	WREG32(phy_offset + 0x00e3c144, 0x35);
	WREG32(phy_offset + 0x00e3c148, 0x35);
	WREG32(phy_offset + 0x00e3c14c, 0x35);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c138, 0x35);
	WREG32(phy_offset + 0x00e3c13c, 0x35);
	WREG32(phy_offset + 0x00e3c140, 0x35);
	WREG32(phy_offset + 0x00e3c144, 0x35);
	WREG32(phy_offset + 0x00e3c538, 0x35);
	WREG32(phy_offset + 0x00e3c53c, 0x35);
	WREG32(phy_offset + 0x00e3c540, 0x35);
	WREG32(phy_offset + 0x00e3c544, 0x35);
	WREG32(phy_offset + 0x00e3c938, 0x35);
	WREG32(phy_offset + 0x00e3c93c, 0x35);
	WREG32(phy_offset + 0x00e3c940, 0x35);
	WREG32(phy_offset + 0x00e3c944, 0x35);
	WREG32(phy_offset + 0x00e3cd38, 0x35);
	WREG32(phy_offset + 0x00e3cd3c, 0x35);
	WREG32(phy_offset + 0x00e3cd40, 0x35);
	WREG32(phy_offset + 0x00e3cd44, 0x35);
	WREG32(phy_offset + 0x00e3d138, 0x35);
	WREG32(phy_offset + 0x00e3d13c, 0x35);
	WREG32(phy_offset + 0x00e3d140, 0x35);
	WREG32(phy_offset + 0x00e3d144, 0x35);
	WREG32(phy_offset + 0x00e3d538, 0x35);
	WREG32(phy_offset + 0x00e3d53c, 0x35);
	WREG32(phy_offset + 0x00e3d540, 0x35);
	WREG32(phy_offset + 0x00e3d544, 0x35);
	WREG32(phy_offset + 0x00e3d938, 0x35);
	WREG32(phy_offset + 0x00e3d93c, 0x35);
	WREG32(phy_offset + 0x00e3d940, 0x35);
	WREG32(phy_offset + 0x00e3d944, 0x35);
	WREG32(phy_offset + 0x00e3dd38, 0x35);
	WREG32(phy_offset + 0x00e3dd3c, 0x35);
	WREG32(phy_offset + 0x00e3dd40, 0x35);
	WREG32(phy_offset + 0x00e3dd44, 0x35);
	WREG32(offset + 0x0001f100, 0xc0000);
	WREG32(phy_offset + 0x00e3c0c0, 0x0);
	WREG32(phy_offset + 0x00e3c0c4, 0x0);
	WREG32(phy_offset + 0x00e3c0d4, 0x0);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c0c0, 0x0);
	WREG32(phy_offset + 0x00e3c0d4, 0x0);
	WREG32(phy_offset + 0x00e3c0d8, 0x0);
	WREG32(offset + 0x0001f100, 0xc0000);
	WREG32(phy_offset + 0x00e3c0f0, 0x0);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c0f0, 0x0);
	WREG32(phy_offset + 0x00e3c0f4, 0x5);
	WREG32(phy_offset + 0x00e3c0f8, 0x0);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e0000c, 0x1);
	WREG32(phy_offset + 0x00e00004, 0x1122);
	WREG32(phy_offset + 0x00e00024, 0x0);
	WREG32(phy_offset + 0x00e00020, 0x0);
	WREG32(phy_offset + 0x00e00050, 0x1300);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c100, 0xf0f);
	WREG32(phy_offset + 0x00e3c108, 0xf0f);
	WREG32(phy_offset + 0x00e3c10c, 0xf0f);
	WREG32(phy_offset + 0x00e3c110, 0xf0f);
	WREG32(phy_offset + 0x00e3c114, 0xf0f);
	WREG32(offset + 0x0001f100, 0xc0000);
	WREG32(phy_offset + 0x00e3c100, 0xf0f);
	WREG32(phy_offset + 0x00e3c104, 0xf0f);
	WREG32(phy_offset + 0x00e3c108, 0xf0f);
	WREG32(phy_offset + 0x00e3c10c, 0xf0f);
	WREG32(phy_offset + 0x00e3ccc0, 0x33);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e00cc4, 0x33);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c120, 0xc00);
	WREG32(phy_offset + 0x00e3c128, 0xf00);
	WREG32(phy_offset + 0x00e3c12c, 0xf00);
	WREG32(phy_offset + 0x00e3c130, 0xf00);
	WREG32(phy_offset + 0x00e3c134, 0xf00);
	WREG32(offset + 0x0001f100, 0xc0000);
	WREG32(phy_offset + 0x00e3c120, 0xc00);
	WREG32(phy_offset + 0x00e3c124, 0xc00);
	WREG32(phy_offset + 0x00e3c128, 0xf00);
	WREG32(phy_offset + 0x00e3c12c, 0xf00);
	WREG32(phy_offset + 0x00e3c0cc, 0x0);
	WREG32(phy_offset + 0x00e3c0d0, 0x0);
	WREG32(phy_offset + 0x00e3c0b8, 0x0);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c0cc, 0x0);
	WREG32(phy_offset + 0x00e3c0b8, 0x0);
	WREG32(phy_offset + 0x00e3c0bc, 0x0);
	WREG32(phy_offset + 0x00e3c3a0, 0x0);
	WREG32(phy_offset + 0x00e3c3a4, 0x200);

	if (ecc)
		WREG32(phy_offset + 0x00e3c004, 0x1);
	else
		WREG32(phy_offset + 0x00e3c004, 0x0);

	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e00048, 0xa1a1);
	WREG32(phy_offset + 0x00e0005c, 0x40);
	WREG32(phy_offset + 0x00e00028, 0x40);
	WREG32(phy_offset + 0x00e005d4, 0xa1);
	WREG32(phy_offset + 0x00e005dc, 0x40);
	WREG32(phy_offset + 0x00e00040, 0xda);
	WREG32(phy_offset + 0x00e00044, 0xf);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c294, 0x1);
	WREG32(phy_offset + 0x00e3c050, 0x910);
	WREG32(phy_offset + 0x00e3c03c, 0x6);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e000d4, 0x100c);
	WREG32(phy_offset + 0x00e000d8, 0x100c);
	WREG32(phy_offset + 0x00e000dc, 0x41c);
	WREG32(phy_offset + 0x00e000e0, 0x1920);
	WREG32(phy_offset + 0x00e000e4, 0x1018);
	WREG32(phy_offset + 0x00e000e8, 0x1018);
	WREG32(phy_offset + 0x00e000ec, 0x428);
	WREG32(phy_offset + 0x00e000f0, 0x2d2c);
	WREG32(phy_offset + 0x00e000f4, 0x1004);
	WREG32(phy_offset + 0x00e000f8, 0x1004);
	WREG32(phy_offset + 0x00e000fc, 0x414);
	WREG32(phy_offset + 0x00e00100, 0x1118);
	WREG32(phy_offset + 0x00e000b0, 0x83f);
	WREG32(phy_offset + 0x00e000b4, 0x83f);
	WREG32(phy_offset + 0x00e000c0, 0x83f);
	WREG32(phy_offset + 0x00e000b8, 0x81f);
	WREG32(phy_offset + 0x00e000bc, 0x81f);
	WREG32(phy_offset + 0x00e00030, 0x0);
	WREG32(phy_offset + 0x00e00054, 0x0);
	WREG32(phy_offset + 0x00e00124, 0x3f0);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c364, 0x9c);
	WREG32(phy_offset + 0x00e3c158, 0x2);
	WREG32(offset + 0x0001f100, 0xc0000);
	WREG32(phy_offset + 0x00e3c008, 0x40);
	WREG32(phy_offset + 0x00e3c004, 0x40);
	WREG32(phy_offset + 0x00e3c404, 0x40);
	WREG32(phy_offset + 0x00e3c804, 0x40);
	WREG32(phy_offset + 0x00e3cc04, 0x40);
	WREG32(phy_offset + 0x00e3d004, 0x40);
	WREG32(phy_offset + 0x00e3d404, 0x40);
	WREG32(phy_offset + 0x00e3d804, 0x40);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c000, 0x5);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e00034, 0x5);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c0a8, 0x280);
	WREG32(phy_offset + 0x00e3c0ac, 0x280);
	WREG32(phy_offset + 0x00e3c0a0, 0xed);
	WREG32(phy_offset + 0x00e3c0a4, 0xed);
	WREG32(phy_offset + 0x00e3c098, 0xed);
	WREG32(phy_offset + 0x00e3c09c, 0xed);
	WREG32(phy_offset + 0x00e3c498, 0xed);
	WREG32(phy_offset + 0x00e3c49c, 0xed);
	WREG32(phy_offset + 0x00e3c898, 0xed);
	WREG32(phy_offset + 0x00e3c89c, 0xed);
	WREG32(phy_offset + 0x00e3cc98, 0xed);
	WREG32(phy_offset + 0x00e3cc9c, 0xed);
	WREG32(phy_offset + 0x00e3d098, 0xed);
	WREG32(phy_offset + 0x00e3d09c, 0xed);
	WREG32(phy_offset + 0x00e3d498, 0xed);
	WREG32(phy_offset + 0x00e3d49c, 0xed);
	WREG32(phy_offset + 0x00e3d898, 0xed);
	WREG32(phy_offset + 0x00e3d89c, 0xed);
	WREG32(phy_offset + 0x00e3dc98, 0xed);
	WREG32(phy_offset + 0x00e3dc9c, 0xed);
	WREG32(phy_offset + 0x00e3e098, 0xed);
	WREG32(phy_offset + 0x00e3e09c, 0xed);
	WREG32(phy_offset + 0x00e3c080, 0x3d9);
	WREG32(phy_offset + 0x00e3c084, 0x3d9);
	WREG32(phy_offset + 0x00e3c040, 0x121);
	WREG32(phy_offset + 0x00e3c044, 0x121);
	WREG32(phy_offset + 0x00e3c048, 0x121);
	WREG32(phy_offset + 0x00e3c04c, 0x121);
	WREG32(phy_offset + 0x00e3c440, 0x121);
	WREG32(phy_offset + 0x00e3c444, 0x121);
	WREG32(phy_offset + 0x00e3c448, 0x121);
	WREG32(phy_offset + 0x00e3c44c, 0x121);
	WREG32(phy_offset + 0x00e3c840, 0x121);
	WREG32(phy_offset + 0x00e3c844, 0x121);
	WREG32(phy_offset + 0x00e3c848, 0x121);
	WREG32(phy_offset + 0x00e3c84c, 0x121);
	WREG32(phy_offset + 0x00e3cc40, 0x121);
	WREG32(phy_offset + 0x00e3cc44, 0x121);
	WREG32(phy_offset + 0x00e3cc48, 0x121);
	WREG32(phy_offset + 0x00e3cc4c, 0x121);
	WREG32(phy_offset + 0x00e3d040, 0x121);
	WREG32(phy_offset + 0x00e3d044, 0x121);
	WREG32(phy_offset + 0x00e3d048, 0x121);
	WREG32(phy_offset + 0x00e3d04c, 0x121);
	WREG32(phy_offset + 0x00e3d440, 0x121);
	WREG32(phy_offset + 0x00e3d444, 0x121);
	WREG32(phy_offset + 0x00e3d448, 0x121);
	WREG32(phy_offset + 0x00e3d44c, 0x121);
	WREG32(phy_offset + 0x00e3d840, 0x121);
	WREG32(phy_offset + 0x00e3d844, 0x121);
	WREG32(phy_offset + 0x00e3d848, 0x121);
	WREG32(phy_offset + 0x00e3d84c, 0x121);
	WREG32(phy_offset + 0x00e3dc40, 0x121);
	WREG32(phy_offset + 0x00e3dc44, 0x121);
	WREG32(phy_offset + 0x00e3dc48, 0x121);
	WREG32(phy_offset + 0x00e3dc4c, 0x121);
	WREG32(phy_offset + 0x00e3e040, 0x121);
	WREG32(phy_offset + 0x00e3e044, 0x121);
	WREG32(phy_offset + 0x00e3e048, 0x121);
	WREG32(phy_offset + 0x00e3e04c, 0x121);
	WREG32(phy_offset + 0x00e3c090, 0x3f9);
	WREG32(phy_offset + 0x00e3c094, 0x3f9);
	WREG32(phy_offset + 0x00e3c490, 0x3f9);
	WREG32(phy_offset + 0x00e3c494, 0x3f9);
	WREG32(phy_offset + 0x00e3c890, 0x3f9);
	WREG32(phy_offset + 0x00e3c894, 0x3f9);
	WREG32(phy_offset + 0x00e3cc90, 0x3f9);
	WREG32(phy_offset + 0x00e3cc94, 0x3f9);
	WREG32(phy_offset + 0x00e3d090, 0x3f9);
	WREG32(phy_offset + 0x00e3d094, 0x3f9);
	WREG32(phy_offset + 0x00e3d490, 0x3f9);
	WREG32(phy_offset + 0x00e3d494, 0x3f9);
	WREG32(phy_offset + 0x00e3d890, 0x3f9);
	WREG32(phy_offset + 0x00e3d894, 0x3f9);
	WREG32(phy_offset + 0x00e3dc90, 0x3f9);
	WREG32(phy_offset + 0x00e3dc94, 0x3f9);
	WREG32(phy_offset + 0x00e3e090, 0x3f9);
	WREG32(phy_offset + 0x00e3e094, 0x3f9);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e001c8, 0x3);
	WREG32(phy_offset + 0x00e001cc, 0x3);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c030, 0xcd);
	WREG32(phy_offset + 0x00e3c034, 0xcd);
	WREG32(phy_offset + 0x00e3c054, 0x19a);
	WREG32(phy_offset + 0x00e3c058, 0x19a);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e001dc, 0x0);
	WREG32(phy_offset + 0x00e00048, 0x7353);
	WREG32(phy_offset + 0x00e00028, 0xc3);
	WREG32(phy_offset + 0x00e0005c, 0x30);
	WREG32(phy_offset + 0x00e00138, 0x10);
	WREG32(phy_offset + 0x00e005dc, 0x30);
	WREG32(phy_offset + 0x00e005d4, 0x73);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c340, 0x0);
	WREG32(phy_offset + 0x00e3c344, 0x0);
	WREG32(phy_offset + 0x00e3c348, 0x0);
	WREG32(phy_offset + 0x00e3c34c, 0x30);
	WREG32(phy_offset + 0x00e3c350, 0x61);
	WREG32(phy_offset + 0x00e3c2b4, 0x3);
	WREG32(phy_offset + 0x00e3c2bc, 0x40);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e00054, 0x0);
	WREG32(phy_offset + 0x00e00088, 0x2);
	WREG32(phy_offset + 0x00e0008c, 0x7d);
	WREG32(phy_offset + 0x00e00104, 0x0);
	WREG32(offset + 0x0001f100, 0x240000);
	WREG32(phy_offset + 0x00e02008, 0x1);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e00114, 0x2);
	WREG32(offset + 0x0001f100, 0x40000);
	WREG32(phy_offset + 0x00e3c03c, 0x5);
	WREG32(offset + 0x0001f100, 0x340000);
	WREG32(phy_offset + 0x00e00000, 0x0);
	WREG32(offset + 0x0001f100, 0x240000);
}

static noinline void greco_init_lpddr_macro2(struct hl_device *hdev,
				u64 ddr_macro_offset, u64 ddr_phy_offset)
{
	u64 offset = DDR_CFG_BASE + ddr_macro_offset;
	/* DDR_CFG_PHY_BASE is part of the hard-coded values */
	u64 phy_offset = ddr_phy_offset;

	if (hdev->pldm)
		return;

	WREG32(phy_offset + 0x00e00000, 0x10);
	WREG32(phy_offset + 0x00e00004, 0x400);
	WREG32(phy_offset + 0x00e00008, 0x40e);
	WREG32(phy_offset + 0x00e0000c, 0x0);
	WREG32(phy_offset + 0x00e00010, 0x0);
	WREG32(phy_offset + 0x00e00014, 0x8);
	WREG32(phy_offset + 0x00e000b8, 0xb);
	WREG32(phy_offset + 0x00e000bc, 0x480);
	WREG32(phy_offset + 0x00e000c0, 0x409);
	WREG32(phy_offset + 0x00e000c4, 0x9);
	WREG32(phy_offset + 0x00e000c8, 0x308);
	WREG32(phy_offset + 0x00e000cc, 0x609);
	WREG32(phy_offset + 0x00e000d0, 0x28);
	WREG32(phy_offset + 0x00e000d4, 0x8160);
	WREG32(phy_offset + 0x00e000d8, 0x47c);
	WREG32(phy_offset + 0x00e000dc, 0x98);
	WREG32(phy_offset + 0x00e000e0, 0x820);
	WREG32(phy_offset + 0x00e000e4, 0x67e);
	WREG32(phy_offset + 0x00e000e8, 0x2);
	WREG32(phy_offset + 0x00e000ec, 0x1);
	WREG32(phy_offset + 0x00e000f0, 0x78);
	WREG32(phy_offset + 0x00e000f4, 0xa);
	WREG32(phy_offset + 0x00e000f8, 0x370);
	WREG32(phy_offset + 0x00e000fc, 0x439);
	WREG32(phy_offset + 0x00e00100, 0x18);
	WREG32(phy_offset + 0x00e00104, 0x8160);
	WREG32(phy_offset + 0x00e00108, 0x43c);
	WREG32(phy_offset + 0x00e0010c, 0x199);
	WREG32(phy_offset + 0x00e00110, 0x3d0);
	WREG32(phy_offset + 0x00e00114, 0x639);
	WREG32(phy_offset + 0x00e00118, 0x9);
	WREG32(phy_offset + 0x00e0011c, 0x150);
	WREG32(phy_offset + 0x00e00120, 0x439);
	WREG32(phy_offset + 0x00e00124, 0x609);
	WREG32(phy_offset + 0x00e00128, 0x100);
	WREG32(phy_offset + 0x00e0012c, 0x439);
	WREG32(phy_offset + 0x00e00130, 0x2);
	WREG32(phy_offset + 0x00e00134, 0x1);
	WREG32(phy_offset + 0x00e00138, 0x38);
	WREG32(phy_offset + 0x00e0013c, 0x0);
	WREG32(phy_offset + 0x00e00140, 0x100);
	WREG32(phy_offset + 0x00e00144, 0x439);
	WREG32(phy_offset + 0x00e00148, 0x11b);
	WREG32(phy_offset + 0x00e0014c, 0x100);
	WREG32(phy_offset + 0x00e00150, 0x439);
	WREG32(phy_offset + 0x00e00154, 0x20);
	WREG32(phy_offset + 0x00e00158, 0x4);
	WREG32(phy_offset + 0x00e0015c, 0x38);
	WREG32(phy_offset + 0x00e00160, 0x20);
	WREG32(phy_offset + 0x00e00164, 0x4);
	WREG32(phy_offset + 0x00e00168, 0x38);
	WREG32(phy_offset + 0x00e0016c, 0x0);
	WREG32(phy_offset + 0x00e00170, 0x100);
	WREG32(phy_offset + 0x00e00174, 0x439);
	WREG32(phy_offset + 0x00e00178, 0x199);
	WREG32(phy_offset + 0x00e0017c, 0x3d0);
	WREG32(phy_offset + 0x00e00180, 0x639);
	WREG32(phy_offset + 0x00e00184, 0x1ab);
	WREG32(phy_offset + 0x00e00188, 0x100);
	WREG32(phy_offset + 0x00e0018c, 0x639);
	WREG32(phy_offset + 0x00e00190, 0x2);
	WREG32(phy_offset + 0x00e00194, 0x150);
	WREG32(phy_offset + 0x00e00198, 0x439);
	WREG32(phy_offset + 0x00e0019c, 0x0);
	WREG32(phy_offset + 0x00e001a0, 0x2a0);
	WREG32(phy_offset + 0x00e001a4, 0x409);
	WREG32(phy_offset + 0x00e001a8, 0x8);
	WREG32(phy_offset + 0x00e001ac, 0x1880);
	WREG32(phy_offset + 0x00e001b0, 0x439);
	WREG32(phy_offset + 0x00e001b4, 0xa);
	WREG32(phy_offset + 0x00e001b8, 0x510);
	WREG32(phy_offset + 0x00e001bc, 0x439);
	WREG32(phy_offset + 0x00e001c0, 0x0);
	WREG32(phy_offset + 0x00e001c4, 0x82b0);
	WREG32(phy_offset + 0x00e001c8, 0x438);
	WREG32(phy_offset + 0x00e001cc, 0xf);
	WREG32(phy_offset + 0x00e001d0, 0x7c0);
	WREG32(phy_offset + 0x00e001d4, 0x439);
	WREG32(phy_offset + 0x00e001d8, 0xa);
	WREG32(phy_offset + 0x00e001dc, 0x700);
	WREG32(phy_offset + 0x00e001e0, 0x449);
	WREG32(phy_offset + 0x00e001e4, 0xa);
	WREG32(phy_offset + 0x00e001e8, 0x700);
	WREG32(phy_offset + 0x00e001ec, 0x459);
	WREG32(phy_offset + 0x00e001f0, 0xa);
	WREG32(phy_offset + 0x00e001f4, 0x708);
	WREG32(phy_offset + 0x00e001f8, 0x459);
	WREG32(phy_offset + 0x00e001fc, 0x2);
	WREG32(phy_offset + 0x00e00200, 0x708);
	WREG32(phy_offset + 0x00e00204, 0x449);
	WREG32(phy_offset + 0x00e00208, 0x0);
	WREG32(phy_offset + 0x00e0020c, 0x84b8);
	WREG32(phy_offset + 0x00e00210, 0x438);
	WREG32(phy_offset + 0x00e00214, 0xff8);
	WREG32(phy_offset + 0x00e00218, 0x8410);
	WREG32(phy_offset + 0x00e0021c, 0x439);
	WREG32(phy_offset + 0x00e00220, 0x7ff8);
	WREG32(phy_offset + 0x00e00224, 0x8498);
	WREG32(phy_offset + 0x00e00228, 0x438);
	WREG32(phy_offset + 0x00e0022c, 0x10);
	WREG32(phy_offset + 0x00e00230, 0x8160);
	WREG32(phy_offset + 0x00e00234, 0x43c);
	WREG32(phy_offset + 0x00e00238, 0x0);
	WREG32(phy_offset + 0x00e0023c, 0x7c8);
	WREG32(phy_offset + 0x00e00240, 0x409);
	WREG32(phy_offset + 0x00e00244, 0x0);
	WREG32(phy_offset + 0x00e00248, 0x1);
	WREG32(phy_offset + 0x00e0024c, 0x8);
	WREG32(phy_offset + 0x00e00250, 0x0);
	WREG32(phy_offset + 0x00e00254, 0x45a);
	WREG32(phy_offset + 0x00e00258, 0x89);
	WREG32(phy_offset + 0x00e0025c, 0x29);
	WREG32(phy_offset + 0x00e00260, 0x8160);
	WREG32(phy_offset + 0x00e00264, 0x47c);
	WREG32(phy_offset + 0x00e00268, 0xb8);
	WREG32(phy_offset + 0x00e0026c, 0x820);
	WREG32(phy_offset + 0x00e00270, 0x67e);
	WREG32(phy_offset + 0x00e00274, 0x1);
	WREG32(phy_offset + 0x00e00278, 0x0);
	WREG32(phy_offset + 0x00e0027c, 0x78);
	WREG32(phy_offset + 0x00e00280, 0x0);
	WREG32(phy_offset + 0x00e00284, 0x1);
	WREG32(phy_offset + 0x00e00288, 0x78);
	WREG32(phy_offset + 0x00e0028c, 0x3ff8);
	WREG32(phy_offset + 0x00e00290, 0x84b8);
	WREG32(phy_offset + 0x00e00294, 0x408);
	WREG32(phy_offset + 0x00e00298, 0x0);
	WREG32(phy_offset + 0x00e0029c, 0x700);
	WREG32(phy_offset + 0x00e002a0, 0x449);
	WREG32(phy_offset + 0x00e002a4, 0x1);
	WREG32(phy_offset + 0x00e002a8, 0x618);
	WREG32(phy_offset + 0x00e002ac, 0x409);
	WREG32(phy_offset + 0x00e002b0, 0x18);
	WREG32(phy_offset + 0x00e002b4, 0x710);
	WREG32(phy_offset + 0x00e002b8, 0x449);
	WREG32(phy_offset + 0x00e002bc, 0xd8);
	WREG32(phy_offset + 0x00e002c0, 0x820);
	WREG32(phy_offset + 0x00e002c4, 0x67e);
	WREG32(phy_offset + 0x00e002c8, 0x8);
	WREG32(phy_offset + 0x00e002cc, 0x4);
	WREG32(phy_offset + 0x00e002d0, 0x48);
	WREG32(phy_offset + 0x00e002d4, 0x0);
	WREG32(phy_offset + 0x00e002d8, 0x1);
	WREG32(phy_offset + 0x00e002dc, 0x78);
	WREG32(phy_offset + 0x00e002e0, 0x10);
	WREG32(phy_offset + 0x00e002e4, 0x710);
	WREG32(phy_offset + 0x00e002e8, 0x449);
	WREG32(phy_offset + 0x00e002ec, 0x0);
	WREG32(phy_offset + 0x00e002f0, 0x1880);
	WREG32(phy_offset + 0x00e002f4, 0x409);
	WREG32(phy_offset + 0x00e002f8, 0xf8);
	WREG32(phy_offset + 0x00e002fc, 0x820);
	WREG32(phy_offset + 0x00e00300, 0x67e);
	WREG32(phy_offset + 0x00e00304, 0x0);
	WREG32(phy_offset + 0x00e00308, 0x4);
	WREG32(phy_offset + 0x00e0030c, 0x48);
	WREG32(phy_offset + 0x00e00310, 0x0);
	WREG32(phy_offset + 0x00e00314, 0x1);
	WREG32(phy_offset + 0x00e00318, 0x78);
	WREG32(phy_offset + 0x00e0031c, 0x0);
	WREG32(phy_offset + 0x00e00320, 0x710);
	WREG32(phy_offset + 0x00e00324, 0x449);
	WREG32(phy_offset + 0x00e00328, 0x0);
	WREG32(phy_offset + 0x00e0032c, 0x4);
	WREG32(phy_offset + 0x00e00330, 0x48);
	WREG32(phy_offset + 0x00e00334, 0x10);
	WREG32(phy_offset + 0x00e00338, 0x4);
	WREG32(phy_offset + 0x00e0033c, 0x18);
	WREG32(phy_offset + 0x00e00340, 0x0);
	WREG32(phy_offset + 0x00e00344, 0x8410);
	WREG32(phy_offset + 0x00e00348, 0x409);
	WREG32(phy_offset + 0x00e0034c, 0x0);
	WREG32(phy_offset + 0x00e00350, 0x8498);
	WREG32(phy_offset + 0x00e00354, 0x408);
	WREG32(phy_offset + 0x00e00358, 0x5);
	WREG32(phy_offset + 0x00e0035c, 0x510);
	WREG32(phy_offset + 0x00e00360, 0x409);
	WREG32(phy_offset + 0x00e00364, 0x198);
	WREG32(phy_offset + 0x00e00368, 0x1000);
	WREG32(phy_offset + 0x00e0036c, 0x409);
	WREG32(phy_offset + 0x00e00370, 0x9);
	WREG32(phy_offset + 0x00e00374, 0x3c0);
	WREG32(phy_offset + 0x00e00378, 0x409);
	WREG32(phy_offset + 0x00e0037c, 0x10);
	WREG32(phy_offset + 0x00e00380, 0x510);
	WREG32(phy_offset + 0x00e00384, 0x409);
	WREG32(phy_offset + 0x00e00388, 0x0);
	WREG32(phy_offset + 0x00e0038c, 0x3c0);
	WREG32(phy_offset + 0x00e00390, 0x409);
	WREG32(phy_offset + 0x00e00394, 0x18);
	WREG32(phy_offset + 0x00e00398, 0x4);
	WREG32(phy_offset + 0x00e0039c, 0x8);
	WREG32(phy_offset + 0x00e003a0, 0x2);
	WREG32(phy_offset + 0x00e003a4, 0x510);
	WREG32(phy_offset + 0x00e003a8, 0x409);
	WREG32(phy_offset + 0x00e003ac, 0x1);
	WREG32(phy_offset + 0x00e003b0, 0x1000);
	WREG32(phy_offset + 0x00e003b4, 0x409);
	WREG32(phy_offset + 0x00e003b8, 0x0);
	WREG32(phy_offset + 0x00e003bc, 0x2a0);
	WREG32(phy_offset + 0x00e003c0, 0x409);
	WREG32(phy_offset + 0x00e003c4, 0x2);
	WREG32(phy_offset + 0x00e003c8, 0x7c8);
	WREG32(phy_offset + 0x00e003cc, 0x409);
	WREG32(phy_offset + 0x00e003d0, 0x10);
	WREG32(phy_offset + 0x00e003d4, 0x8160);
	WREG32(phy_offset + 0x00e003d8, 0x40c);
	WREG32(phy_offset + 0x00e003dc, 0x0);
	WREG32(phy_offset + 0x00e003e0, 0x1);
	WREG32(phy_offset + 0x00e003e4, 0x8);
	WREG32(phy_offset + 0x00e003e8, 0x0);
	WREG32(phy_offset + 0x00e003ec, 0x2a0);
	WREG32(phy_offset + 0x00e003f0, 0x409);
	WREG32(phy_offset + 0x00e003f4, 0xb);
	WREG32(phy_offset + 0x00e003f8, 0x370);
	WREG32(phy_offset + 0x00e003fc, 0x409);
	WREG32(phy_offset + 0x00e00400, 0xd);
	WREG32(phy_offset + 0x00e00404, 0x7c0);
	WREG32(phy_offset + 0x00e00408, 0x409);
	WREG32(phy_offset + 0x00e0040c, 0x10);
	WREG32(phy_offset + 0x00e00410, 0x82b0);
	WREG32(phy_offset + 0x00e00414, 0x418);
	WREG32(phy_offset + 0x00e00418, 0x10);
	WREG32(phy_offset + 0x00e0041c, 0x82b0);
	WREG32(phy_offset + 0x00e00420, 0x468);
	WREG32(phy_offset + 0x00e00424, 0x5);
	WREG32(phy_offset + 0x00e00428, 0x7c0);
	WREG32(phy_offset + 0x00e0042c, 0x409);
	WREG32(phy_offset + 0x00e00430, 0x10);
	WREG32(phy_offset + 0x00e00434, 0x510);
	WREG32(phy_offset + 0x00e00438, 0x409);
	WREG32(phy_offset + 0x00e0043c, 0xa);
	WREG32(phy_offset + 0x00e00440, 0x1880);
	WREG32(phy_offset + 0x00e00444, 0x669);
	WREG32(phy_offset + 0x00e00448, 0x8);
	WREG32(phy_offset + 0x00e0044c, 0x1900);
	WREG32(phy_offset + 0x00e00450, 0x609);
	WREG32(phy_offset + 0x00e00454, 0x2);
	WREG32(phy_offset + 0x00e00458, 0x7c8);
	WREG32(phy_offset + 0x00e0045c, 0x409);
	WREG32(phy_offset + 0x00e00460, 0xa);
	WREG32(phy_offset + 0x00e00464, 0x3c8);
	WREG32(phy_offset + 0x00e00468, 0x629);
	WREG32(phy_offset + 0x00e0046c, 0x19a);
	WREG32(phy_offset + 0x00e00470, 0x3d0);
	WREG32(phy_offset + 0x00e00474, 0x619);
	WREG32(phy_offset + 0x00e00478, 0x199);
	WREG32(phy_offset + 0x00e0047c, 0x3d0);
	WREG32(phy_offset + 0x00e00480, 0x609);
	WREG32(phy_offset + 0x00e00484, 0x9);
	WREG32(phy_offset + 0x00e00488, 0x150);
	WREG32(phy_offset + 0x00e0048c, 0x409);
	WREG32(phy_offset + 0x00e00490, 0x12b);
	WREG32(phy_offset + 0x00e00494, 0x100);
	WREG32(phy_offset + 0x00e00498, 0x639);
	WREG32(phy_offset + 0x00e0049c, 0x20);
	WREG32(phy_offset + 0x00e004a0, 0x4);
	WREG32(phy_offset + 0x00e004a4, 0x38);
	WREG32(phy_offset + 0x00e004a8, 0x13f);
	WREG32(phy_offset + 0x00e004ac, 0x100);
	WREG32(phy_offset + 0x00e004b0, 0x619);
	WREG32(phy_offset + 0x00e004b4, 0x38);
	WREG32(phy_offset + 0x00e004b8, 0x4);
	WREG32(phy_offset + 0x00e004bc, 0x18);
	WREG32(phy_offset + 0x00e004c0, 0x14f);
	WREG32(phy_offset + 0x00e004c4, 0x100);
	WREG32(phy_offset + 0x00e004c8, 0x619);
	WREG32(phy_offset + 0x00e004cc, 0x18);
	WREG32(phy_offset + 0x00e004d0, 0x8510);
	WREG32(phy_offset + 0x00e004d4, 0x468);
	WREG32(phy_offset + 0x00e004d8, 0x18);
	WREG32(phy_offset + 0x00e004dc, 0x2c8);
	WREG32(phy_offset + 0x00e004e0, 0x469);
	WREG32(phy_offset + 0x00e004e4, 0x10);
	WREG32(phy_offset + 0x00e004e8, 0x8520);
	WREG32(phy_offset + 0x00e004ec, 0x468);
	WREG32(phy_offset + 0x00e004f0, 0x0);
	WREG32(phy_offset + 0x00e004f4, 0x8050);
	WREG32(phy_offset + 0x00e004f8, 0x468);
	WREG32(phy_offset + 0x00e004fc, 0x18);
	WREG32(phy_offset + 0x00e00500, 0x8160);
	WREG32(phy_offset + 0x00e00504, 0x46c);
	WREG32(phy_offset + 0x00e00508, 0x1ff8);
	WREG32(phy_offset + 0x00e0050c, 0x85d8);
	WREG32(phy_offset + 0x00e00510, 0x668);
	WREG32(phy_offset + 0x00e00514, 0x8);
	WREG32(phy_offset + 0x00e00518, 0x85f0);
	WREG32(phy_offset + 0x00e0051c, 0x468);
	WREG32(phy_offset + 0x00e00520, 0x8);
	WREG32(phy_offset + 0x00e00524, 0xa5f0);
	WREG32(phy_offset + 0x00e00528, 0x468);
	WREG32(phy_offset + 0x00e0052c, 0x168);
	WREG32(phy_offset + 0x00e00530, 0x100);
	WREG32(phy_offset + 0x00e00534, 0x469);
	WREG32(phy_offset + 0x00e00538, 0x2);
	WREG32(phy_offset + 0x00e0053c, 0x1);
	WREG32(phy_offset + 0x00e00540, 0x68);
	WREG32(phy_offset + 0x00e00544, 0x0);
	WREG32(phy_offset + 0x00e00548, 0x100);
	WREG32(phy_offset + 0x00e0054c, 0x469);
	WREG32(phy_offset + 0x00e00550, 0x0);
	WREG32(phy_offset + 0x00e00554, 0x85f0);
	WREG32(phy_offset + 0x00e00558, 0x468);
	WREG32(phy_offset + 0x00e0055c, 0x0);
	WREG32(phy_offset + 0x00e00560, 0xa5f0);
	WREG32(phy_offset + 0x00e00564, 0x468);
	WREG32(phy_offset + 0x00e00568, 0x1ff8);
	WREG32(phy_offset + 0x00e0056c, 0x85d8);
	WREG32(phy_offset + 0x00e00570, 0x668);
	WREG32(phy_offset + 0x00e00574, 0x8);
	WREG32(phy_offset + 0x00e00578, 0x8df0);
	WREG32(phy_offset + 0x00e0057c, 0x468);
	WREG32(phy_offset + 0x00e00580, 0x8);
	WREG32(phy_offset + 0x00e00584, 0xadf0);
	WREG32(phy_offset + 0x00e00588, 0x468);
	WREG32(phy_offset + 0x00e0058c, 0x178);
	WREG32(phy_offset + 0x00e00590, 0x100);
	WREG32(phy_offset + 0x00e00594, 0x469);
	WREG32(phy_offset + 0x00e00598, 0x2);
	WREG32(phy_offset + 0x00e0059c, 0x1);
	WREG32(phy_offset + 0x00e005a0, 0x68);
	WREG32(phy_offset + 0x00e005a4, 0x0);
	WREG32(phy_offset + 0x00e005a8, 0x100);
	WREG32(phy_offset + 0x00e005ac, 0x469);
	WREG32(phy_offset + 0x00e005b0, 0x0);
	WREG32(phy_offset + 0x00e005b4, 0x8df0);
	WREG32(phy_offset + 0x00e005b8, 0x468);
	WREG32(phy_offset + 0x00e005bc, 0x0);
	WREG32(phy_offset + 0x00e005c0, 0xadf0);
	WREG32(phy_offset + 0x00e005c4, 0x468);
	WREG32(phy_offset + 0x00e005c8, 0x10);
	WREG32(phy_offset + 0x00e005cc, 0x2c8);
	WREG32(phy_offset + 0x00e005d0, 0x469);
	WREG32(phy_offset + 0x00e005d4, 0x8);
	WREG32(phy_offset + 0x00e005d8, 0x8050);
	WREG32(phy_offset + 0x00e005dc, 0x468);
	WREG32(phy_offset + 0x00e005e0, 0x28);
	WREG32(phy_offset + 0x00e005e4, 0x4);
	WREG32(phy_offset + 0x00e005e8, 0x68);
	WREG32(phy_offset + 0x00e005ec, 0x1c);
	WREG32(phy_offset + 0x00e005f0, 0x82b0);
	WREG32(phy_offset + 0x00e005f4, 0x468);
	WREG32(phy_offset + 0x00e005f8, 0x18);
	WREG32(phy_offset + 0x00e005fc, 0x82b0);
	WREG32(phy_offset + 0x00e00600, 0x418);
	WREG32(phy_offset + 0x00e00604, 0xa);
	WREG32(phy_offset + 0x00e00608, 0x500);
	WREG32(phy_offset + 0x00e0060c, 0x669);
	WREG32(phy_offset + 0x00e00610, 0x0);
	WREG32(phy_offset + 0x00e00614, 0x8520);
	WREG32(phy_offset + 0x00e00618, 0x468);
	WREG32(phy_offset + 0x00e0061c, 0x8);
	WREG32(phy_offset + 0x00e00620, 0x8530);
	WREG32(phy_offset + 0x00e00624, 0x408);
	WREG32(phy_offset + 0x00e00628, 0x18a);
	WREG32(phy_offset + 0x00e0062c, 0x100);
	WREG32(phy_offset + 0x00e00630, 0x469);
	WREG32(phy_offset + 0x00e00634, 0x2);
	WREG32(phy_offset + 0x00e00638, 0x1);
	WREG32(phy_offset + 0x00e0063c, 0x68);
	WREG32(phy_offset + 0x00e00640, 0x2);
	WREG32(phy_offset + 0x00e00644, 0x100);
	WREG32(phy_offset + 0x00e00648, 0x469);
	WREG32(phy_offset + 0x00e0064c, 0x9);
	WREG32(phy_offset + 0x00e00650, 0x500);
	WREG32(phy_offset + 0x00e00654, 0x629);
	WREG32(phy_offset + 0x00e00658, 0x0);
	WREG32(phy_offset + 0x00e0065c, 0x8530);
	WREG32(phy_offset + 0x00e00660, 0x408);
	WREG32(phy_offset + 0x00e00664, 0x10);
	WREG32(phy_offset + 0x00e00668, 0x8510);
	WREG32(phy_offset + 0x00e0066c, 0x468);
	WREG32(phy_offset + 0x00e00670, 0x0);
	WREG32(phy_offset + 0x00e00674, 0x2c8);
	WREG32(phy_offset + 0x00e00678, 0x469);
	WREG32(phy_offset + 0x00e0067c, 0x30);
	WREG32(phy_offset + 0x00e00680, 0x4);
	WREG32(phy_offset + 0x00e00684, 0x68);
	WREG32(phy_offset + 0x00e00688, 0x8);
	WREG32(phy_offset + 0x00e0068c, 0x8520);
	WREG32(phy_offset + 0x00e00690, 0x468);
	WREG32(phy_offset + 0x00e00694, 0x19a);
	WREG32(phy_offset + 0x00e00698, 0x100);
	WREG32(phy_offset + 0x00e0069c, 0x469);
	WREG32(phy_offset + 0x00e006a0, 0x2);
	WREG32(phy_offset + 0x00e006a4, 0x1);
	WREG32(phy_offset + 0x00e006a8, 0x68);
	WREG32(phy_offset + 0x00e006ac, 0x4);
	WREG32(phy_offset + 0x00e006b0, 0x100);
	WREG32(phy_offset + 0x00e006b4, 0x469);
	WREG32(phy_offset + 0x00e006b8, 0x0);
	WREG32(phy_offset + 0x00e006bc, 0x8520);
	WREG32(phy_offset + 0x00e006c0, 0x468);
	WREG32(phy_offset + 0x00e006c4, 0x8);
	WREG32(phy_offset + 0x00e006c8, 0x3c8);
	WREG32(phy_offset + 0x00e006cc, 0x629);
	WREG32(phy_offset + 0x00e006d0, 0x0);
	WREG32(phy_offset + 0x00e006d4, 0x150);
	WREG32(phy_offset + 0x00e006d8, 0x409);
	WREG32(phy_offset + 0x00e006dc, 0x199);
	WREG32(phy_offset + 0x00e006e0, 0x3d0);
	WREG32(phy_offset + 0x00e006e4, 0x769);
	WREG32(phy_offset + 0x00e006e8, 0x2);
	WREG32(phy_offset + 0x00e006ec, 0x370);
	WREG32(phy_offset + 0x00e006f0, 0x409);
	WREG32(phy_offset + 0x00e006f4, 0x0);
	WREG32(phy_offset + 0x00e006f8, 0x400);
	WREG32(phy_offset + 0x00e006fc, 0x40e);
	WREG32(phy_offset + 0x00e00700, 0x30);
	WREG32(phy_offset + 0x00e00704, 0x82b0);
	WREG32(phy_offset + 0x00e00708, 0x408);
	WREG32(phy_offset + 0x00e0070c, 0x8);
	WREG32(phy_offset + 0x00e00710, 0x2a0);
	WREG32(phy_offset + 0x00e00714, 0x409);
	WREG32(phy_offset + 0x00e00718, 0x0);
	WREG32(phy_offset + 0x00e0071c, 0x8168);
	WREG32(phy_offset + 0x00e00720, 0x40c);
	WREG32(phy_offset + 0x00e00724, 0x8);
	WREG32(phy_offset + 0x00e00728, 0x300);
	WREG32(phy_offset + 0x00e0072c, 0x419);
	WREG32(phy_offset + 0x00e00730, 0x10);
	WREG32(phy_offset + 0x00e00734, 0x8160);
	WREG32(phy_offset + 0x00e00738, 0x40c);
	WREG32(phy_offset + 0x00e0073c, 0x8);
	WREG32(phy_offset + 0x00e00740, 0x7c8);
	WREG32(phy_offset + 0x00e00744, 0x401);
	WREG32(phy_offset + 0x00e00748, 0x8);
	WREG32(phy_offset + 0x00e0074c, 0x0);
	WREG32(phy_offset + 0x00e00750, 0x8);
	WREG32(phy_offset + 0x00e00754, 0x8);
	WREG32(phy_offset + 0x00e00758, 0x1880);
	WREG32(phy_offset + 0x00e0075c, 0x409);
	WREG32(phy_offset + 0x00e00760, 0xf);
	WREG32(phy_offset + 0x00e00764, 0x7c0);
	WREG32(phy_offset + 0x00e00768, 0x409);
	WREG32(phy_offset + 0x00e0076c, 0x0);
	WREG32(phy_offset + 0x00e00770, 0x2a0);
	WREG32(phy_offset + 0x00e00774, 0x409);
	WREG32(phy_offset + 0x00e00778, 0x8);
	WREG32(phy_offset + 0x00e0077c, 0x618);
	WREG32(phy_offset + 0x00e00780, 0x409);
	WREG32(phy_offset + 0x00e00784, 0x0);
	WREG32(phy_offset + 0x00e00788, 0x84b8);
	WREG32(phy_offset + 0x00e0078c, 0x408);
	WREG32(phy_offset + 0x00e00790, 0xff8);
	WREG32(phy_offset + 0x00e00794, 0x8410);
	WREG32(phy_offset + 0x00e00798, 0x409);
	WREG32(phy_offset + 0x00e0079c, 0x7ff8);
	WREG32(phy_offset + 0x00e007a0, 0x8498);
	WREG32(phy_offset + 0x00e007a4, 0x408);
	WREG32(phy_offset + 0x00e007a8, 0x0);
	WREG32(phy_offset + 0x00e007ac, 0x7c8);
	WREG32(phy_offset + 0x00e007b0, 0x409);
	WREG32(phy_offset + 0x00e007b4, 0x8);
	WREG32(phy_offset + 0x00e007b8, 0x8168);
	WREG32(phy_offset + 0x00e007bc, 0x40c);
	WREG32(phy_offset + 0x00e007c0, 0x0);
	WREG32(phy_offset + 0x00e007c4, 0x1);
	WREG32(phy_offset + 0x00e007c8, 0x8);
	WREG32(phy_offset + 0x00e007cc, 0x8);
	WREG32(phy_offset + 0x00e007d0, 0x4);
	WREG32(phy_offset + 0x00e007d4, 0x8);
	WREG32(phy_offset + 0x00e007d8, 0x18);
	WREG32(phy_offset + 0x00e007dc, 0x300);
	WREG32(phy_offset + 0x00e007e0, 0x409);
	WREG32(phy_offset + 0x00e007e4, 0x0);
	WREG32(phy_offset + 0x00e007e8, 0x480);
	WREG32(phy_offset + 0x00e007ec, 0x409);
	WREG32(phy_offset + 0x00e007f0, 0x8);
	WREG32(phy_offset + 0x00e007f4, 0x510);
	WREG32(phy_offset + 0x00e007f8, 0x409);
	WREG32(phy_offset + 0x00e007fc, 0x8);
	WREG32(phy_offset + 0x00e00800, 0x7c8);
	WREG32(phy_offset + 0x00e00804, 0x401);
	WREG32(phy_offset + 0x00e00018, 0x0);
	WREG32(phy_offset + 0x00e0001c, 0x0);
	WREG32(phy_offset + 0x00e00020, 0x8);
	WREG32(phy_offset + 0x00e00024, 0x0);
	WREG32(phy_offset + 0x00e00028, 0x0);
	WREG32(phy_offset + 0x00e0002c, 0x0);
	WREG32(offset + 0x0001f100, 0x340000);
	WREG32(phy_offset + 0x00e0039c, 0x400);
	WREG32(offset + 0x0001f100, 0x240000);
	WREG32(phy_offset + 0x00e00070, 0x0);
	WREG32(phy_offset + 0x00e00080, 0x40);
	WREG32(phy_offset + 0x00e00090, 0x44);
	WREG32(phy_offset + 0x00e000ac, 0x8c);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e005e8, 0x0);
	WREG32(phy_offset + 0x00e00490, 0x8080);
	WREG32(phy_offset + 0x00e00494, 0x7e7e);
	WREG32(phy_offset + 0x00e00498, 0x8080);
	WREG32(phy_offset + 0x00e0049c, 0x7e7e);
	WREG32(phy_offset + 0x00e004a0, 0x7d7d);
	WREG32(phy_offset + 0x00e004a4, 0x7f7f);
	WREG32(phy_offset + 0x00e004a8, 0x7d7d);
	WREG32(phy_offset + 0x00e004ac, 0x7f7f);
	WREG32(phy_offset + 0x00e004b0, 0x7e7e);
	WREG32(phy_offset + 0x00e004b4, 0x8080);
	WREG32(phy_offset + 0x00e004b8, 0x7e7e);
	WREG32(phy_offset + 0x00e004bc, 0x8080);
	WREG32(phy_offset + 0x00e004c0, 0x8181);
	WREG32(phy_offset + 0x00e004c4, 0x8181);
	WREG32(phy_offset + 0x00e004c8, 0x8181);
	WREG32(phy_offset + 0x00e004cc, 0x8181);
	WREG32(phy_offset + 0x00e004d0, 0x8282);
	WREG32(phy_offset + 0x00e004d4, 0x8282);
	WREG32(phy_offset + 0x00e004d8, 0x8282);
	WREG32(phy_offset + 0x00e004dc, 0x8282);
	WREG32(phy_offset + 0x00e004e0, 0x8283);
	WREG32(phy_offset + 0x00e004e4, 0x8283);
	WREG32(phy_offset + 0x00e004e8, 0x8283);
	WREG32(phy_offset + 0x00e004ec, 0x8283);
	WREG32(phy_offset + 0x00e004f0, 0x9883);
	WREG32(phy_offset + 0x00e004f4, 0x9883);
	WREG32(phy_offset + 0x00e004f8, 0xd8c4);
	WREG32(phy_offset + 0x00e004fc, 0xd8c4);
	WREG32(phy_offset + 0x00e00500, 0x9f99);
	WREG32(phy_offset + 0x00e00504, 0x9f99);
	WREG32(phy_offset + 0x00e00508, 0xded9);
	WREG32(phy_offset + 0x00e0050c, 0xded9);
	WREG32(phy_offset + 0x00e00510, 0xb3a0);
	WREG32(phy_offset + 0x00e00514, 0xb3a0);
	WREG32(phy_offset + 0x00e00518, 0xeddf);
	WREG32(phy_offset + 0x00e0051c, 0xeddf);
	WREG32(phy_offset + 0x00e00520, 0xc2b4);
	WREG32(phy_offset + 0x00e00524, 0xc2b4);
	WREG32(phy_offset + 0x00e00528, 0xfcee);
	WREG32(phy_offset + 0x00e0052c, 0xfcee);
	WREG32(phy_offset + 0x00e00530, 0xc3c3);
	WREG32(phy_offset + 0x00e00534, 0xc3c3);
	WREG32(phy_offset + 0x00e00538, 0xfdfd);
	WREG32(phy_offset + 0x00e0053c, 0xfdfd);
	WREG32(offset + 0x0001f100, 0x300000);
	WREG32(phy_offset + 0x00e00004, 0x5061);
	WREG32(phy_offset + 0x00e00000, 0xbedc);
	WREG32(offset + 0x0001f100, 0x240000);
	WREG32(phy_offset + 0x00e00030, 0x0);
	WREG32(phy_offset + 0x00e00034, 0x177);
	WREG32(phy_offset + 0x00e00038, 0x60);
	WREG32(phy_offset + 0x00e0003c, 0x7910);
	WREG32(phy_offset + 0x00e00040, 0x3156);
	WREG32(phy_offset + 0x00e00044, 0xcfbd);
	WREG32(phy_offset + 0x00e00048, 0xffff);
	WREG32(phy_offset + 0x00e0004c, 0xffff);
	WREG32(phy_offset + 0x00e00050, 0x110);
	WREG32(offset + 0x0001f100, 0x80000);
	WREG32(phy_offset + 0x00e003c0, 0x3f7);
	WREG32(phy_offset + 0x00e001f8, 0x33);
	WREG32(phy_offset + 0x00e0009c, 0xd);
	WREG32(phy_offset + 0x00e003bc, 0xffff);
	WREG32(phy_offset + 0x00e00c40, 0x1);
	WREG32(phy_offset + 0x00e00c44, 0x1);
	WREG32(offset + 0x0001f100, 0x300000);
	WREG32(phy_offset + 0x00e00200, 0x2);
	WREG32(offset + 0x0001f100, 0x340000);
	WREG32(phy_offset + 0x00e00000, 0x1);
}

static noinline void greco_init_lpddr_macro3(struct hl_device *hdev,
				   u64 ddr_macro_offset, u64 ddr_phy_offset)
{
	u64 offset = DDR_CFG_BASE + ddr_macro_offset;

	WREG32(offset + 0x00002c80, 0x0);
	WREG32(offset + 0x00012c80, 0x0);
	WREG32(offset + 0x00002510, 0x24);
	WREG32(offset + 0x00012510, 0x24);
	WREG32(offset + 0x00002c80, 0x1);
	WREG32(offset + 0x00012c80, 0x1);

	greco_poll_swstat_sw_done_ack(hdev, ddr_macro_offset); /* step 17 */
	greco_poll_dfi_init_complete(hdev, ddr_macro_offset); /* step 18 */

	WREG32(offset + 0x00002c80, 0x0);
	WREG32(offset + 0x00002510, 0x25);
	WREG32(offset + 0x00012c80, 0x0);
	WREG32(offset + 0x00002c80, 0x1);
	WREG32(offset + 0x00012510, 0x25);
	WREG32(offset + 0x00012c80, 0x1);

	greco_poll_swstat_sw_done_ack(hdev, ddr_macro_offset);

	WREG32(offset + 0x00002c80, 0x0);
	WREG32(offset + 0x00002510, 0x5);
	WREG32(offset + 0x00012c80, 0x0);
	WREG32(offset + 0x00002c80, 0x1);
	WREG32(offset + 0x00012510, 0x5);
	WREG32(offset + 0x00012c80, 0x1);

	greco_poll_swstat_sw_done_ack(hdev, ddr_macro_offset);

	WREG32(offset + 0x00002180, 0x20000);
	WREG32(offset + 0x00012180, 0x20000);

	greco_poll_normal_operation_mode(hdev, ddr_macro_offset); /* step 25 */

	WREG32(offset + 0x00000a80, 0x1330);
	WREG32(offset + 0x00002080, 0xf0);
	WREG32(offset + 0x00002084, 0x1b0);
	WREG32(offset + 0x00002080, 0x800000f0);
	WREG32(offset + 0x00002080, 0xf0);
	WREG32(offset + 0x00002084, 0x2bb);
	WREG32(offset + 0x00002080, 0x800000f0);
	WREG32(offset + 0x00002080, 0xf0);

	if (ecc)
		WREG32(offset + 0x00002084, 0x381);
	else
		WREG32(offset + 0x00002084, 0x3c1);

	WREG32(offset + 0x00002080, 0x800000f0);
	WREG32(offset + 0x00002080, 0xf0);
	WREG32(offset + 0x00002084, 0xa58);
	WREG32(offset + 0x00002080, 0x800000f0);
	WREG32(offset + 0x00002080, 0xf0);
	WREG32(offset + 0x00002084, 0xb20);
	WREG32(offset + 0x00002080, 0x800000f0);
	WREG32(offset + 0x00002080, 0xf0);
	WREG32(offset + 0x00002084, 0xc38);
	WREG32(offset + 0x00002080, 0x800000f0);
	WREG32(offset + 0x00002080, 0xf0);
	WREG32(offset + 0x00002084, 0xd81);
	WREG32(offset + 0x00002080, 0x800000f0);
	WREG32(offset + 0x00002080, 0xf0);
	WREG32(offset + 0x00002084, 0x1000);
	WREG32(offset + 0x00002080, 0x800000f0);
	WREG32(offset + 0x00002080, 0xf0);
	WREG32(offset + 0x00002084, 0x1213);
	WREG32(offset + 0x00002080, 0x800000f0);
	WREG32(offset + 0x00002080, 0xf0);
	WREG32(offset + 0x00002084, 0x1402);
	WREG32(offset + 0x00002080, 0x800000f0);
	WREG32(offset + 0x00002080, 0xf0);
	WREG32(offset + 0x00002084, 0x1500);
	WREG32(offset + 0x00002080, 0x800000f0);

	if (ecc) {
		WREG32(offset + 0x00002080, 0xf0);
		WREG32(offset + 0x00002084, 0x1650);
		WREG32(offset + 0x00002080, 0x800000f0);
	} else {
		WREG32(offset + 0x00002080, 0xf0);
		WREG32(offset + 0x00002084, 0x1600);
		WREG32(offset + 0x00002080, 0x800000f0);
	}

	WREG32(offset + 0x00002080, 0x800000f0);
	WREG32(offset + 0x00002080, 0xf0);
	WREG32(offset + 0x00002084, 0x1c00);
	WREG32(offset + 0x00002080, 0x800000f0);
	WREG32(offset + 0x00002208, 0x0);
	WREG32(offset + 0x00002180, 0x20000);
	WREG32(offset + 0x00002100, 0x8);
	WREG32(offset + 0x00000a80, 0x2a65);
	WREG32(offset + 0x000040e0, 0x10030810);
	WREG32(offset + 0x00002180, 0x20000);
	WREG32(offset + 0x00002100, 0x8);
	WREG32(offset + 0x00002208, 0x1);
	WREG32(offset + 0x00002208, 0x0);
	WREG32(offset + 0x00010a80, 0x1330);
	WREG32(offset + 0x00002180, 0x20000);
	WREG32(offset + 0x00000a80, 0x3220);
	WREG32(offset + 0x00012080, 0xf0);
	WREG32(offset + 0x00012084, 0x1b0);
	WREG32(offset + 0x00012080, 0x800000f0);
	WREG32(offset + 0x00012080, 0xf0);
	WREG32(offset + 0x00012084, 0x2bb);
	WREG32(offset + 0x00012080, 0x800000f0);
	WREG32(offset + 0x00012080, 0xf0);

	if (ecc)
		WREG32(offset + 0x00012084, 0x381);
	else
		WREG32(offset + 0x00012084, 0x3c1);

	WREG32(offset + 0x00012080, 0x800000f0);
	WREG32(offset + 0x00012080, 0xf0);
	WREG32(offset + 0x00012084, 0xa58);
	WREG32(offset + 0x00012080, 0x800000f0);
	WREG32(offset + 0x00012080, 0xf0);
	WREG32(offset + 0x00012084, 0xb20);
	WREG32(offset + 0x00012080, 0x800000f0);
	WREG32(offset + 0x00012080, 0xf0);
	WREG32(offset + 0x00012084, 0xc38);
	WREG32(offset + 0x00012080, 0x800000f0);
	WREG32(offset + 0x00012080, 0xf0);
	WREG32(offset + 0x00012084, 0xd81);
	WREG32(offset + 0x00012080, 0x800000f0);
	WREG32(offset + 0x00012080, 0xf0);
	WREG32(offset + 0x00012084, 0x1000);
	WREG32(offset + 0x00012080, 0x800000f0);
	WREG32(offset + 0x00012080, 0xf0);
	WREG32(offset + 0x00012084, 0x1213);
	WREG32(offset + 0x00012080, 0x800000f0);
	WREG32(offset + 0x00012080, 0xf0);
	WREG32(offset + 0x00012084, 0x1402);
	WREG32(offset + 0x00012080, 0x800000f0);
	WREG32(offset + 0x00012080, 0xf0);
	WREG32(offset + 0x00012084, 0x1500);
	WREG32(offset + 0x00012080, 0x800000f0);

	if (ecc) {
		WREG32(offset + 0x00012080, 0xf0);
		WREG32(offset + 0x00012084, 0x1650);
		WREG32(offset + 0x00012080, 0x800000f0);
	} else {
		WREG32(offset + 0x00012080, 0xf0);
		WREG32(offset + 0x00012084, 0x1600);
		WREG32(offset + 0x00012080, 0x800000f0);
	}

	WREG32(offset + 0x00012080, 0xf0);
	WREG32(offset + 0x00012084, 0x1c00);
	WREG32(offset + 0x00012080, 0x800000f0);
	WREG32(offset + 0x00012208, 0x0);
	WREG32(offset + 0x00012180, 0x20000);
	WREG32(offset + 0x00012100, 0x8);
	WREG32(offset + 0x00010a80, 0x2a65);
	WREG32(offset + 0x000140e0, 0x10030810);
	WREG32(offset + 0x00012180, 0x20000);
	WREG32(offset + 0x00012100, 0x8);
	WREG32(offset + 0x00012208, 0x1);
	WREG32(offset + 0x00012208, 0x0);
	WREG32(offset + 0x00012180, 0x20000);
	WREG32(offset + 0x00010a80, 0x3220);
	WREG32(offset + 0x0001f200, 0x1);
}

int greco_init_dram(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	int i;

	if (!hdev->dram_enable || (hdev->fw_components & FW_TYPE_BOOT_CPU))
		return 0;

	if (greco->hw_cap_initialized & HW_CAP_DRAM)
		return 0;

	for (i = 0 ; i < DDR_MACRO_NUM * DDR_CH_PER_MACRO_NUM ; i += 2) {
		dev_dbg(hdev->dev, "Initializing DDR Macro %d\n", (i / 2));

		/*
		 * Break the configuration to multiple functions since Smatch
		 * doesn't allow a too long function.
		 */
		greco_init_lpddr_macro(hdev,
					DDR_CFG_OFFSET * (i / 2),
					DDR_CFG_PHY_OFFSET * (i / 2));
		greco_init_lpddr_macro1(hdev,
					DDR_CFG_OFFSET * (i / 2),
					DDR_CFG_PHY_OFFSET * (i / 2));
		greco_init_lpddr_macro2(hdev,
					DDR_CFG_OFFSET * (i / 2),
					DDR_CFG_PHY_OFFSET * (i / 2));
		greco_init_lpddr_macro3(hdev,
					DDR_CFG_OFFSET * (i / 2),
					DDR_CFG_PHY_OFFSET * (i / 2));
		usleep_range(100, 400);
	}

	greco->hw_cap_initialized |= HW_CAP_DRAM;

	return 0;
}

void greco_dram_shift_cfg(struct hl_device *hdev)
{
	u32 offset, mmu_base;
	int dcore_id, dmmu_id;

	dev_dbg(hdev->dev, "Configure DRAM addresses shift\n");

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++)
		for (dmmu_id = 0 ; dmmu_id < NUM_OF_DMMU_PER_DCORE; dmmu_id++) {
			offset = (u32) (dcore_id * DCORE_OFFSET +
					dmmu_id * DMMU_OFFSET);
			mmu_base = mmDCORE0_HMMU0_MMU_BASE + offset;

			/* Configure shift to support all DRAM sizes */
			WREG32(mmu_base + MMU_ADDR_SHIFT,
				1 << SHIFT_ALIGN_WR_SHIFT_EN_SHIFT |
				8 << SHIFT_ALIGN_WR_SHIFT_OFFSET_SHIFT |
				4 << SHIFT_ALIGN_WR_SHIFT_WIDTH_SHIFT |
				1 << SHIFT_ALIGN_RD_SHIFT_EN_SHIFT |
				8 << SHIFT_ALIGN_RD_SHIFT_OFFSET_SHIFT |
				4 << SHIFT_ALIGN_RD_SHIFT_WIDTH_SHIFT);
		}

	dev_dbg(hdev->dev, "Finished configuring DRAM addresses shift\n");
}

/*
 * greco_cfg_tpc_binning:
 *
 * The Dcore1 devices are a mirroring of Dcore0 defining the relations
 * between user flags, device spec (SocOnline) and HW layout as follows:
 * +-----------+------------------+------------+
 * | user mask |   device spec    |  HW layout |
 * +-----------+------------------+------------+
 * |   0x001   |   DCORE0_TPC0  0 | CORE0 TOP0 |
 * |   0x010   |   DCORE0_TPC4  4 | CORE0 TOP4 |
 * |   0x020   |   DCORE1_TPC0  5 | CORE1 TOP4 |
 * |   0x040   |   DCORE1_TPC1  6 | CORE1 TOP3 |
 * |   0x080   |   DCORE1_TPC2  7 | CORE1 TOP2 |
 * |   0x100   |   DCORE1_TPC3  8 | CORE1 TOP1 |
 * |   0x200   |   DCORE1_TPC4  9 | CORE1 TOP0 |
 * +-----------+------------------+------------+
 *
 * Configure Dcore0 tpc_ch/mme_ch by DCORE0_TPCIF_RTR2/DCORE0_MMEIF_RTR2
 * Configure Dcore1 tpc_ch/mme_ch by DCORE1_TPCIF_RTR1/DCORE1_MMEIF_RTR1
 * ===================================================================
 * Configurations Modes ( Clustering / Binning )
 * tpc_bin_dcore0:
 *    rtr_ctrl_mode[0] - Dcore 0 Tpc 0 binning: 0 - no bin, 1 - bin
 *    rtr_ctrl_mode[1] - Dcore 0 Tpc 1 binning: 0 - no bin, 1 - bin
 *    rtr_ctrl_mode[2] - Dcore 0 Tpc 2 binning: 0 - no bin, 1 - bin
 *    rtr_ctrl_mode[3] - Dcore 0 Tpc 3 binning: 0 - no bin, 1 - bin
 *    rtr_ctrl_mode[4] - Dcore 0 Tpc 4 binning: 0 - no bin, 1 - bin
 * tpc_bin_dcore1:
 *    rtr_ctrl_mode[5] - Dcore 1 Tpc 0 binning: 0 - no bin, 1 - bin 9
 *    rtr_ctrl_mode[6] - Dcore 1 Tpc 1 binning: 0 - no bin, 1 - bin 8
 *    rtr_ctrl_mode[7] - Dcore 1 Tpc 2 binning: 0 - no bin, 1 - bin
 *    rtr_ctrl_mode[8] - Dcore 1 Tpc 3 binning: 0 - no bin, 1 - bin
 *    rtr_ctrl_mode[9] - Dcore 1 Tpc 4 binning: 0 - no bin, 1 - bin
 * ch_mx_dcore0:
 *    rtr_ctrl_mode[10] - Dcore 0 TPC 1 mux sel: 0 - RTR0, 1 - RTR1
 *    rtr_ctrl_mode[11] - Dcore 0 TPC 2 mux sel: 0 - RTR1, 1 - RTR2
 *    rtr_ctrl_mode[12] - Dcore 0 TPC 3 mux sel: 0 - RTR2, 1 - RTR3
 * ch_mx_dcore1:
 *    rtr_ctrl_mode[13] - Dcore 1 TPC 1 mux sel: 0 - RTR0, 1 - RTR1
 *    rtr_ctrl_mode[14] - Dcore 1 TPC 2 mux sel: 0 - RTR1, 1 - RTR2
 *    rtr_ctrl_mode[15] - Dcore 1 TPC 3 mux sel: 0 - RTR2, 1 - RTR3
 *    rtr_ctrl_mode[21:16] - ddr size (in GB)
 * rtr_ctrl_mode[23:22] - ddr_mode: 0-1xDDR, 1-2xDDR, 2-4xDDR
 * rtr_ctrl_mode[25:24] - sram optimization: 0 - disable, 1 - 64MB, 2 - 32MB
 * rtr_ctrl_mode[28] - dec0-4 binning:
 *			0 - no bin, 1 - at least 1 of DEC 0-4 decoders is bin
 * rtr_ctrl_mode[29] - dec5-9 binning:
 *			0 - no bin, 1 - at least 1 of DEC 5-9 decoders is bin
 * rtr_ctrl_mode[31] - Virtualization: Full = 0, Half = 1
 */
static void greco_cfg_tpc_mux(struct hl_device *hdev, u32 binned_tpcs)
{
	u32 dcore0_tpc_mux = 0, dcore1_tpc_mux = 0, dcore_tpc_mux, dcon_tpc_mux;

	if (!binned_tpcs)
		return;

	/* RTR to TPC SELECT (DCORE0) */
	switch (binned_tpcs & DCORE0_TPCIF_RTR3_CTRL_MODE_TPC_BIN_D0_MASK) {
	case 0x0:
	case 0x1:
		dcore0_tpc_mux = 0;
		break;
	case 0x2:
		dcore0_tpc_mux = 0x1;
		break;
	case 0x4:
		dcore0_tpc_mux = 0x3;
		break;
	case 0x8:
	case 0x10:
		dcore0_tpc_mux = 0x7;
		break;
	default:
		break;
	}

	/* RTR to TPC SELECT (DCORE1) */
	switch (binned_tpcs & DCORE1_TPCIF_RTR3_CTRL_MODE_TPC_BIN_D1_MASK) {
	case 0x0:
	case 0x20:
		dcore1_tpc_mux = 0x0;
		break;
	case 0x40:
		dcore1_tpc_mux = 0x1;
		break;
	case 0x80:
		dcore1_tpc_mux = 0x3;
		break;
	case 0x100:
	case 0x200:
		dcore1_tpc_mux = 0x7;
		break;
	default:
		break;
	}

	dcore_tpc_mux = (dcore0_tpc_mux <<
				DCORE0_TPCIF_RTR3_CTRL_MODE_CH_MX_D0_0_SHIFT) |
			(dcore1_tpc_mux <<
				DCORE1_TPCIF_RTR3_CTRL_MODE_CH_MX_D1_0_SHIFT) |
			binned_tpcs;

	WREG32_OR(mmDCORE0_TPCIF_RTR2_CTRL_MODE, dcore_tpc_mux);
	WREG32_OR(mmDCORE0_MMEIF_RTR2_CTRL_MODE, dcore_tpc_mux);
	WREG32_OR(mmDCORE1_TPCIF_RTR1_CTRL_MODE, dcore_tpc_mux);
	WREG32_OR(mmDCORE1_MMEIF_RTR1_CTRL_MODE, dcore_tpc_mux);

	dcon_tpc_mux = (dcore0_tpc_mux <<
				DCON0_E2E_MST_WR_MID_CRD_CH_MUX_D0_0_SHIFT) |
			(dcore1_tpc_mux <<
				DCON0_E2E_MST_WR_MID_CRD_CH_MUX_D1_0_SHIFT);

	WREG32_OR(mmDCON0_E2E_MST_WR_MID_CRD, dcon_tpc_mux);
	WREG32_OR(mmDCON0_E2E_MST_RD_MID_CRD, dcon_tpc_mux);
	WREG32_OR(mmDCON1_E2E_MST_WR_MID_CRD, dcon_tpc_mux);
	WREG32_OR(mmDCON1_E2E_MST_RD_MID_CRD, dcon_tpc_mux);
	WREG32_OR(mmDCON2_E2E_MST_WR_MID_CRD, dcon_tpc_mux);
	WREG32_OR(mmDCON2_E2E_MST_RD_MID_CRD, dcon_tpc_mux);
	WREG32_OR(mmDCON3_E2E_MST_WR_MID_CRD, dcon_tpc_mux);
	WREG32_OR(mmDCON3_E2E_MST_RD_MID_CRD, dcon_tpc_mux);
}

static void greco_route_block1_to_block2(struct hl_device *hdev, u64 block1_addr, u32 block2_xy,
						u32 match_start_bit, u32 rtr_lbw_decode_entry)
{
	u32 match_end_bit, entry_offset, lbw_decode_addr, lbw_decode_ctrl, i, base;

	match_end_bit = 25; /* LBW address space is 32MB */
	entry_offset = rtr_lbw_decode_entry * 0x4;

	lbw_decode_addr = lower_32_bits(block1_addr) & RTR_CTRL_LBW_DECODE_BASE_ADDR_VAL_MASK;
	lbw_decode_ctrl = FIELD_PREP(RTR_CTRL_LBW_DECODE_CTRL_EN_MASK, 0x1) |
			FIELD_PREP(RTR_CTRL_LBW_DECODE_CTRL_START_BIT_MASK, match_start_bit) |
			FIELD_PREP(RTR_CTRL_LBW_DECODE_CTRL_END_BIT_MASK, match_end_bit) |
			FIELD_PREP(RTR_CTRL_LBW_DECODE_CTRL_YX_MASK, block2_xy);

	/* 4 DCONs */
	for (i = 0 ; i < NUM_OF_DCONS ; ++i) {
		base = mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + i * DCON_OFFSET;
		WREG32(base + LBW_DECODE_BASE_ADDR_OFFSET + entry_offset, lbw_decode_addr);
		WREG32(base + LBW_DECODE_CTRL_OFFSET + entry_offset, lbw_decode_ctrl);
	}

	/* 8 MME RTRs */
	for (i = 0 ; i < NUM_OF_MME_TPC_RTR_PER_DCORE ; ++i) {
		base = mmDCORE0_MMEIF_RTR0_CTRL_BASE + i * RTR_OFFSET;
		WREG32(base + LBW_DECODE_BASE_ADDR_OFFSET + entry_offset, lbw_decode_addr);
		WREG32(base + LBW_DECODE_CTRL_OFFSET + entry_offset, lbw_decode_ctrl);

		base = mmDCORE1_MMEIF_RTR0_CTRL_BASE + i * RTR_OFFSET;
		WREG32(base + LBW_DECODE_BASE_ADDR_OFFSET + entry_offset, lbw_decode_addr);
		WREG32(base + LBW_DECODE_CTRL_OFFSET + entry_offset, lbw_decode_ctrl);
	}

	/* 8 TPC RTRs */
	for (i = 0 ; i < NUM_OF_MME_TPC_RTR_PER_DCORE ; ++i) {
		base = mmDCORE0_TPCIF_RTR0_CTRL_BASE + i * RTR_OFFSET;
		WREG32(base + LBW_DECODE_BASE_ADDR_OFFSET + entry_offset, lbw_decode_addr);
		WREG32(base + LBW_DECODE_CTRL_OFFSET + entry_offset, lbw_decode_ctrl);

		base = mmDCORE1_TPCIF_RTR0_CTRL_BASE + i * RTR_OFFSET;
		WREG32(base + LBW_DECODE_BASE_ADDR_OFFSET + entry_offset, lbw_decode_addr);
		WREG32(base + LBW_DECODE_CTRL_OFFSET + entry_offset, lbw_decode_ctrl);
	}
}

static void greco_route_tpc4_to_tpc9(struct hl_device *hdev)
{
	u32 tpc9_xy, match_start_bit;
	u64 tpc4_addr;

	tpc4_addr = CFG_BASE + mmDCORE0_TPC4_QM_BASE;
	tpc9_xy = 0x19; /* X=9, Y=1 */
	match_start_bit = 16; /* TPC CFG address is 64KB */

	greco_route_block1_to_block2(hdev, tpc4_addr, tpc9_xy, match_start_bit,
					RTR_LBW_DECODE_ENTRY_0_TPC);
}

static void greco_route_dec4_to_dec9(struct hl_device *hdev)
{
	u32 dec9_xy, match_start_bit;
	u64 dec4_addr;

	dec9_xy = 0x2A; /* X=10, Y=2 */
	match_start_bit = 13; /* DEC CFG address is 24KB so need 3 x 8KB */

	dec4_addr = CFG_BASE + mmDCORE0_VDEC4_BRDG_CTRL_BASE;
	greco_route_block1_to_block2(hdev, dec4_addr, dec9_xy, match_start_bit,
					RTR_LBW_DECODE_ENTRY_1_DEC);

	dec4_addr = CFG_BASE + mmDCORE0_DEC4_L2C_BASE;
	greco_route_block1_to_block2(hdev, dec4_addr, dec9_xy, match_start_bit,
					RTR_LBW_DECODE_ENTRY_2_DEC);

	dec4_addr = CFG_BASE + mmDCORE0_DEC4_CMD_BASE;
	greco_route_block1_to_block2(hdev, dec4_addr, dec9_xy, match_start_bit,
					RTR_LBW_DECODE_ENTRY_3_DEC);
}

static void greco_init_binning_tpc_dec(struct hl_device *hdev)
{
	u32 tpc_binning, dec_binning, dcore1_dec_binning, binned_decoder;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int i;

	/*
	 * reverse the tpc binning mask if it is in Dcore 1
	 * in order to keep the mirroring between the Dcores
	 */
	switch (prop->tpc_binning_mask) {
	case 0x20:
		prop->tpc_binning_mask = 0x200;
		break;
	case 0x40:
		prop->tpc_binning_mask = 0x100;
		break;
	case 0x100:
		prop->tpc_binning_mask = 0x40;
		break;
	case 0x200:
		prop->tpc_binning_mask = 0x20;
		break;
	default:
		break;
	}

	tpc_binning = TPC_BINNING & ~prop->tpc_binning_mask;
	dec_binning = DEC_BINNING;

	if (prop->decoder_enabled_mask && (prop->decoder_binning_mask & 0x1F))
		dec_binning &= ~DCORE0_TPCIF_RTR3_CTRL_MODE_DEC04_BIN_MASK;

	if (prop->decoder_enabled_mask && (prop->decoder_binning_mask & 0x3E0))
		dec_binning &= ~DCORE0_TPCIF_RTR3_CTRL_MODE_DEC59_BIN_MASK;

	WREG32_AND(mmDCON0_HBW_RTR_IF0_RTR_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCON0_HBW_RTR_IF1_RTR_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCON0_LBW_RTR_IF_RTR_CTRL_RTR_CTRL_MODE,
						~(dec_binning | tpc_binning));
	WREG32_AND(mmDCON1_HBW_RTR_IF0_RTR_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCON1_HBW_RTR_IF1_RTR_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCON1_LBW_RTR_IF_RTR_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCON2_HBW_RTR_IF0_RTR_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCON2_HBW_RTR_IF1_RTR_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCON2_LBW_RTR_IF_RTR_CTRL_RTR_CTRL_MODE,
						~(dec_binning | tpc_binning));
	WREG32_AND(mmDCON3_HBW_RTR_IF0_RTR_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCON3_HBW_RTR_IF1_RTR_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCON3_LBW_RTR_IF_RTR_CTRL_RTR_CTRL_MODE, ~tpc_binning);

	WREG32_AND(mmDCORE0_MMEIF_RTR0_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_MMEIF_RTR1_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_MMEIF_RTR2_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_MMEIF_RTR3_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_MMEIF_RTR0_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_MMEIF_RTR1_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_MMEIF_RTR2_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_MMEIF_RTR3_CTRL_RTR_CTRL_MODE, ~tpc_binning);

	WREG32_AND(mmDCORE1_MMEIF_RTR0_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_MMEIF_RTR1_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_MMEIF_RTR2_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_MMEIF_RTR3_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_MMEIF_RTR0_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_MMEIF_RTR1_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_MMEIF_RTR2_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_MMEIF_RTR3_CTRL_RTR_CTRL_MODE, ~tpc_binning);

	WREG32_AND(mmDCORE0_TPCIF_RTR0_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_TPCIF_RTR1_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_TPCIF_RTR2_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_TPCIF_RTR3_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_TPCIF_RTR0_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_TPCIF_RTR1_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_TPCIF_RTR2_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE0_TPCIF_RTR3_CTRL_RTR_CTRL_MODE, ~tpc_binning);

	WREG32_AND(mmDCORE1_TPCIF_RTR0_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_TPCIF_RTR1_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_TPCIF_RTR2_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_TPCIF_RTR3_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_TPCIF_RTR0_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_TPCIF_RTR1_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_TPCIF_RTR2_CTRL_RTR_CTRL_MODE, ~tpc_binning);
	WREG32_AND(mmDCORE1_TPCIF_RTR3_CTRL_RTR_CTRL_MODE, ~tpc_binning);

	greco_cfg_tpc_mux(hdev, prop->tpc_binning_mask);

	/* If TPC binning is done in DCORE0, route TPC4 to TPC9 to have 9 consecutive TPCs */
	if (prop->tpc_binning_mask & 0x1F)
		greco_route_tpc4_to_tpc9(hdev);

	/* Configure decoders and their binning */
	if (prop->decoder_binning_mask & 0x1F) {
		WREG32(mmDCORE0_VSI_WRAP_DEC_BIN_MASK,
			prop->decoder_binning_mask & 0x1F);

		/* Find the binned decoder and number it as the last one */
		binned_decoder = ffs(prop->decoder_binning_mask & 0x1F) - 1;
		WREG32(mmDCORE0_VSI_WRAP_DEC_ID_0 + 4 * binned_decoder,
			NUM_OF_DEC_PER_DCORE - 1);

		/* Renumber the succeeding decoders */
		for (i = binned_decoder + 1 ; i < NUM_OF_DEC_PER_DCORE ; i++)
			WREG32(mmDCORE0_VSI_WRAP_DEC_ID_0 + 4 * i, i - 1);
	} else if (prop->decoder_enabled_mask & 0x1F) {
		WREG32(mmDCORE0_VSI_WRAP_DEC_BIN_MASK, 0);
	}

	if (prop->decoder_binning_mask & 0x3E0) {
		dcore1_dec_binning = (prop->decoder_binning_mask & 0x3E0) >> 5;
		WREG32(mmDCORE1_VSI_WRAP_DEC_BIN_MASK, dcore1_dec_binning);

		/* Find the binned decoder and number it as the last one */
		binned_decoder = ffs(dcore1_dec_binning) - 1;
		WREG32(mmDCORE1_VSI_WRAP_DEC_ID_0 + 4 * binned_decoder,
			NUM_OF_DEC_PER_DCORE - 1);

		/* Renumber the succeeding decoders */
		for (i = binned_decoder + 1 ; i < NUM_OF_DEC_PER_DCORE ; i++)
			WREG32(mmDCORE1_VSI_WRAP_DEC_ID_0 + 4 * i, i - 1);
	} else if (prop->decoder_enabled_mask & 0x3E0) {
		WREG32(mmDCORE1_VSI_WRAP_DEC_BIN_MASK, 0);
	}

	/* If DEC binning is done in DCORE0, route DEC4 to DEC9 to have 9 consecutive decoders */
	if (prop->decoder_binning_mask & 0x1F)
		greco_route_dec4_to_dec9(hdev);
}

static void greco_init_binning_sram(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 sram_binning_ctrl;

	/*
	 * [0] - Binning mode.
	 *    0 - 128MB mode, all SRAM used
	 *    1 - 64MB mode, only half of SRAM used, according to Bank_Sel field
	 *
	 * [1] - Bank Sel
	 *    0 - Use only lower half of SRAM
	 *    1 - Use only high half of SRAM
	 */
	switch (prop->sram_binning) {
	case 1:
		sram_binning_ctrl = 3;
		break;
	case 2:
		sram_binning_ctrl = 1;
		break;
	default:
		sram_binning_ctrl = 0;
		break;
	}

	WREG32(mmDCORE0_SRAM_BANK_0_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE0_SRAM_BANK_1_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE0_SRAM_BANK_2_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE0_SRAM_BANK_3_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE0_SRAM_BANK_4_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE0_SRAM_BANK_5_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE0_SRAM_BANK_6_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE0_SRAM_BANK_7_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE1_SRAM_BANK_0_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE1_SRAM_BANK_1_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE1_SRAM_BANK_2_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE1_SRAM_BANK_3_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE1_SRAM_BANK_4_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE1_SRAM_BANK_5_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE1_SRAM_BANK_6_BINNING_CTRL, sram_binning_ctrl);
	WREG32(mmDCORE1_SRAM_BANK_7_BINNING_CTRL, sram_binning_ctrl);
}

static void set_binning_mme_blocks(struct hl_device *hdev,
		u32 dcore_base_addr, u8 fma_binned_idx, u8 ima_binned_idx)
{
	u32 val;

	/*
	 * Bits 0-5 represent IMA block idx [0-32].
	 * Bits 6-11 represent FMA block idx [0-32].
	 */
	WREG32(dcore_base_addr + MME_CTRL_LO_REDUN_OFFSET,
			ima_binned_idx | (fma_binned_idx << 6));

	/*
	 * For every MME block (FMA/IMA), out of 33 binnable columns, only 1
	 * column is to be binned, and the other 32 are to be kept enabled:
	 * If binned idx is between 0-31, clear its relevant bit on _CLK_EN32
	 * and set all the others. As well set LSB (1st bit) ONLY on _CLK_EN33.
	 * If binned idx is 32 then set all 32 bits on _CLK_EN32. As well, clear
	 * LSB bit on _CLK_EN33.
	 */
	val = fma_binned_idx & 0x20 ? 0xFFFFFFFF :
			(u32)~(BIT(fma_binned_idx & 0x1F));
	WREG32(dcore_base_addr +
			MME_CTRL_LO_FMA_FUNC_REDUN_CLK_EN32_OFFSET, val);

	WREG32(dcore_base_addr + MME_CTRL_LO_FMA_FUNC_REDUN_CLK_EN33_OFFSET,
			!(fma_binned_idx & 0x20));


	val = ima_binned_idx & 0x20 ? 0xFFFFFFFF :
			(u32)~(BIT(ima_binned_idx & 0x1F));
	WREG32(dcore_base_addr +
			MME_CTRL_LO_IMA_FUNC_REDUN_CLK_EN32_OFFSET, val);

	WREG32(dcore_base_addr + MME_CTRL_LO_IMA_FUNC_REDUN_CLK_EN33_OFFSET,
			!(ima_binned_idx & 0x20));
}

static void greco_init_binning_mme(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 mme_binning = (u32)prop->mme_binning_mask;
	u8 dcore0_mme_fma_binned_idx, dcore0_mme_ima_binned_idx,
	dcore1_mme_fma_binned_idx, dcore1_mme_ima_binned_idx;

	/*
	 * MME binning mask:
	 * bits [0:6]   <==> dcore0 mme fma
	 * bits [7:13]  <==> dcore1 mme fma
	 * bits [14:20] <==> dcore0 mme ima
	 * bits [21:27] <==> dcore1 mme ima
	 * For each group, if the 6th bit is set, then first 5 bits represent
	 * the col's idx [0-31], otherwise these bits are ignored,
	 * and col idx 32 is the binned col. The 7th bit is don't care.
	 */
	dcore0_mme_fma_binned_idx =
			mme_binning & 0x20 ? mme_binning & 0x1F : 0x20;

	mme_binning >>= 7;
	dcore1_mme_fma_binned_idx =
			mme_binning & 0x20 ? mme_binning & 0x1F : 0x20;

	mme_binning >>= 7;
	dcore0_mme_ima_binned_idx =
			mme_binning & 0x20 ? mme_binning & 0x1F : 0x20;

	mme_binning >>= 7;
	dcore1_mme_ima_binned_idx =
			mme_binning & 0x20 ? mme_binning & 0x1F : 0x20;


	set_binning_mme_blocks(hdev, mmDCORE0_MME_CTRL_LO_BASE,
			dcore0_mme_fma_binned_idx, dcore0_mme_ima_binned_idx);

	set_binning_mme_blocks(hdev, mmDCORE1_MME_CTRL_LO_BASE,
			dcore1_mme_fma_binned_idx, dcore1_mme_ima_binned_idx);
}

static void greco_report_driver_performed_binning(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	struct cpucp_packet ack_pkt;
	u64 result;
	int rc;

	if (!(greco->hw_cap_initialized & HW_CAP_CPU_Q))
		return;

	memset(&ack_pkt, 0, sizeof(ack_pkt));
	ack_pkt.ctl = cpu_to_le32(CPUCP_PACKET_BINNING_DONE <<
			CPUCP_PKT_CTL_OPCODE_SHIFT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) &ack_pkt,
			sizeof(ack_pkt), 0, &result);
	if (rc) {
		dev_err(hdev->dev, "Failed to handle CPU-CP info pkt id %d, error %d\n",
				CPUCP_PACKET_BINNING_DONE, rc);
	}
}

static void greco_init_binning(struct hl_device *hdev)
{
	hdev->asic_funcs->set_binning_masks(hdev);
	greco_init_binning_sram(hdev);
	greco_init_binning_tpc_dec(hdev);
	greco_init_binning_mme(hdev);

	/* TODO - remove once we're not required to support older FW */
	if (hdev->fw_inner_major_ver > 35)
		greco_report_driver_performed_binning(hdev);
}

void greco_kdma_e2e_init(struct hl_device *hdev)
{
	WREG32(mmDCORE0_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 8);
	WREG32(mmDCORE0_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 8);
	WREG32(mmDCORE0_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 8);
	WREG32(mmDCORE0_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 8);
	WREG32(mmDCORE0_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 8);
	WREG32(mmDCORE0_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 8);
	WREG32(mmDCORE0_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 8);
	WREG32(mmDCORE0_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 8);

	WREG32(mmDCORE1_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 8);
	WREG32(mmDCORE1_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 8);
	WREG32(mmDCORE1_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 8);
	WREG32(mmDCORE1_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 8);
	WREG32(mmDCORE1_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 8);
	WREG32(mmDCORE1_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 8);
	WREG32(mmDCORE1_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 8);
	WREG32(mmDCORE1_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 8);

	WREG32(mmDCORE0_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	WREG32(mmDCORE1_KDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
}

static void greco_init_e2e(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 value, orig_addr, orig_ctrl;

	/* EMEM shared update values */
	WREG32(mmCPU_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 9);
	WREG32(mmCPU_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 9);
	WREG32(mmCPU_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 9);
	WREG32(mmCPU_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 9);
	WREG32(mmCPU_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 9);
	WREG32(mmCPU_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 9);
	WREG32(mmCPU_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 9);
	WREG32(mmCPU_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 9);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 13);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 13);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 13);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 13);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 13);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 13);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 13);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 13);
	WREG32(mmPMMU_HBW_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 128);
	WREG32(mmPMMU_HBW_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 128);
	WREG32(mmPMMU_HBW_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 128);
	WREG32(mmPMMU_HBW_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 128);
	WREG32(mmPMMU_HBW_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 0);
	WREG32(mmPMMU_HBW_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 0);
	WREG32(mmPMMU_HBW_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 0);
	WREG32(mmPMMU_HBW_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 0);
	WREG32(mmPSOC_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 9);
	WREG32(mmPSOC_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 9);
	WREG32(mmPSOC_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 9);
	WREG32(mmPSOC_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 9);
	WREG32(mmPSOC_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 7);
	WREG32(mmPSOC_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 7);
	WREG32(mmPSOC_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 7);
	WREG32(mmPSOC_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 7);

	/* EMEM DCORE0 update values */
	WREG32(mmDCORE0_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 34);
	WREG32(mmDCORE0_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 34);
	WREG32(mmDCORE0_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 34);
	WREG32(mmDCORE0_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 34);
	WREG32(mmDCORE0_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 34);
	WREG32(mmDCORE0_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 34);
	WREG32(mmDCORE0_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 34);
	WREG32(mmDCORE0_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 34);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 34);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 34);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 34);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 34);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 34);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 34);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 34);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 34);
	WREG32(mmDCORE0_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 83);
	WREG32(mmDCORE0_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 83);
	WREG32(mmDCORE0_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 83);
	WREG32(mmDCORE0_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 83);
	WREG32(mmDCORE0_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 83);
	WREG32(mmDCORE0_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 83);
	WREG32(mmDCORE0_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 83);
	WREG32(mmDCORE0_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 83);
	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 1);
	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 1);
	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 1);
	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 1);
	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 1);
	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 1);
	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 1);
	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 1);

	if (hdev->mmu_enable) {
		WREG32(mmDCORE0_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 32);
		WREG32(mmDCORE0_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 32);
		WREG32(mmDCORE0_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 32);
		WREG32(mmDCORE0_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 32);
		WREG32(mmDCORE0_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 0);
		WREG32(mmDCORE0_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 0);
		WREG32(mmDCORE0_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 0);
		WREG32(mmDCORE0_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 0);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 32);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 32);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 32);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 32);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 0);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 0);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 0);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 0);
	}

	if (hdev->mme_mask & 0x1) {
		WREG32(mmDCORE0_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 42);
		WREG32(mmDCORE0_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 42);
		WREG32(mmDCORE0_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 42);
		WREG32(mmDCORE0_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 42);
		WREG32(mmDCORE0_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 168);
		WREG32(mmDCORE0_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 168);
		WREG32(mmDCORE0_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 168);
		WREG32(mmDCORE0_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 168);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0,
			42);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1,
			42);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2,
			42);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3,
			42);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0,
			0);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1,
			0);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2,
			0);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3,
			0);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0,
			42);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1,
			42);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2,
			42);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3,
			42);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0,
			0);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1,
			0);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2,
			0);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3,
			0);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0,
			42);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1,
			42);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2,
			42);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3,
			42);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0,
			0);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1,
			0);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2,
			0);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3,
			0);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x1) {
		WREG32(mmDCORE0_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 33);
		WREG32(mmDCORE0_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 33);
		WREG32(mmDCORE0_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 33);
		WREG32(mmDCORE0_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 33);
		WREG32(mmDCORE0_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 30);
		WREG32(mmDCORE0_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 30);
		WREG32(mmDCORE0_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 30);
		WREG32(mmDCORE0_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 30);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x2) {
		WREG32(mmDCORE0_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 33);
		WREG32(mmDCORE0_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 33);
		WREG32(mmDCORE0_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 33);
		WREG32(mmDCORE0_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 33);
		WREG32(mmDCORE0_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 30);
		WREG32(mmDCORE0_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 30);
		WREG32(mmDCORE0_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 30);
		WREG32(mmDCORE0_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 30);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x4) {
		WREG32(mmDCORE0_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 33);
		WREG32(mmDCORE0_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 33);
		WREG32(mmDCORE0_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 33);
		WREG32(mmDCORE0_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 33);
		WREG32(mmDCORE0_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 30);
		WREG32(mmDCORE0_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 30);
		WREG32(mmDCORE0_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 30);
		WREG32(mmDCORE0_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 30);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x8) {
		WREG32(mmDCORE0_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 33);
		WREG32(mmDCORE0_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 33);
		WREG32(mmDCORE0_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 33);
		WREG32(mmDCORE0_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 33);
		WREG32(mmDCORE0_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 30);
		WREG32(mmDCORE0_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 30);
		WREG32(mmDCORE0_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 30);
		WREG32(mmDCORE0_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 30);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x10) {
		WREG32(mmDCORE0_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 33);
		WREG32(mmDCORE0_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 33);
		WREG32(mmDCORE0_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 33);
		WREG32(mmDCORE0_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 33);
		WREG32(mmDCORE0_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 30);
		WREG32(mmDCORE0_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 30);
		WREG32(mmDCORE0_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 30);
		WREG32(mmDCORE0_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 30);
	}

	if (hdev->asic_prop.decoder_enabled_mask & 0x1F) {
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0,
			25);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1,
			25);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2,
			25);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3,
			25);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0,
			20);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1,
			20);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2,
			20);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3,
			20);
	}

	if (hdev->rotator_mask & 0x1) {
		WREG32(mmDCORE0_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 5);
		WREG32(mmDCORE0_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 5);
		WREG32(mmDCORE0_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 5);
		WREG32(mmDCORE0_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 5);
		WREG32(mmDCORE0_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 4);
		WREG32(mmDCORE0_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 4);
		WREG32(mmDCORE0_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 4);
		WREG32(mmDCORE0_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 4);
	}

	/* EMEM DCORE1 update values */
	WREG32(mmDCORE1_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 34);
	WREG32(mmDCORE1_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 34);
	WREG32(mmDCORE1_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 34);
	WREG32(mmDCORE1_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 34);
	WREG32(mmDCORE1_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 34);
	WREG32(mmDCORE1_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 34);
	WREG32(mmDCORE1_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 34);
	WREG32(mmDCORE1_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 34);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 34);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 34);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 34);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 34);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 34);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 34);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 34);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 34);
	WREG32(mmDCORE1_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 83);
	WREG32(mmDCORE1_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 83);
	WREG32(mmDCORE1_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 83);
	WREG32(mmDCORE1_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 83);
	WREG32(mmDCORE1_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 83);
	WREG32(mmDCORE1_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 83);
	WREG32(mmDCORE1_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 83);
	WREG32(mmDCORE1_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 83);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 1);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 1);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 1);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 1);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 1);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 1);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 1);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 1);

	if (hdev->mmu_enable) {
		WREG32(mmDCORE1_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 32);
		WREG32(mmDCORE1_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 32);
		WREG32(mmDCORE1_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 32);
		WREG32(mmDCORE1_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 32);
		WREG32(mmDCORE1_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 0);
		WREG32(mmDCORE1_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 0);
		WREG32(mmDCORE1_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 0);
		WREG32(mmDCORE1_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 0);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 32);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 32);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 32);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 32);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 0);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 0);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 0);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 0);
	}

	if (hdev->mme_mask & 0x2) {
		WREG32(mmDCORE1_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 42);
		WREG32(mmDCORE1_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 42);
		WREG32(mmDCORE1_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 42);
		WREG32(mmDCORE1_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 42);
		WREG32(mmDCORE1_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 168);
		WREG32(mmDCORE1_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 168);
		WREG32(mmDCORE1_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 168);
		WREG32(mmDCORE1_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 168);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0,
			42);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1,
			42);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2,
			42);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3,
			42);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0,
			0);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1,
			0);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2,
			0);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3,
			0);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0,
			42);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1,
			42);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2,
			42);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3,
			42);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0,
			0);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1,
			0);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2,
			0);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3,
			0);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0,
			42);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1,
			42);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2,
			42);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3,
			42);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0,
			0);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1,
			0);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2,
			0);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3,
			0);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x20) {
		WREG32(mmDCORE1_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 33);
		WREG32(mmDCORE1_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 33);
		WREG32(mmDCORE1_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 33);
		WREG32(mmDCORE1_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 33);
		WREG32(mmDCORE1_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 30);
		WREG32(mmDCORE1_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 30);
		WREG32(mmDCORE1_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 30);
		WREG32(mmDCORE1_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 30);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x40) {
		WREG32(mmDCORE1_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 33);
		WREG32(mmDCORE1_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 33);
		WREG32(mmDCORE1_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 33);
		WREG32(mmDCORE1_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 33);
		WREG32(mmDCORE1_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 30);
		WREG32(mmDCORE1_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 30);
		WREG32(mmDCORE1_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 30);
		WREG32(mmDCORE1_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 30);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x80) {
		WREG32(mmDCORE1_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 33);
		WREG32(mmDCORE1_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 33);
		WREG32(mmDCORE1_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 33);
		WREG32(mmDCORE1_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 33);
		WREG32(mmDCORE1_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 30);
		WREG32(mmDCORE1_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 30);
		WREG32(mmDCORE1_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 30);
		WREG32(mmDCORE1_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 30);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x100) {
		WREG32(mmDCORE1_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 33);
		WREG32(mmDCORE1_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 33);
		WREG32(mmDCORE1_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 33);
		WREG32(mmDCORE1_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 33);
		WREG32(mmDCORE1_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 30);
		WREG32(mmDCORE1_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 30);
		WREG32(mmDCORE1_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 30);
		WREG32(mmDCORE1_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 30);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x200) {
		WREG32(mmDCORE1_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 33);
		WREG32(mmDCORE1_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 33);
		WREG32(mmDCORE1_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 33);
		WREG32(mmDCORE1_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 33);
		WREG32(mmDCORE1_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 30);
		WREG32(mmDCORE1_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 30);
		WREG32(mmDCORE1_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 30);
		WREG32(mmDCORE1_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 30);
	}

	if (hdev->asic_prop.decoder_enabled_mask & 0x3E0) {
		/* Due to H/W issue we cannot access DCORE1 VSI registers from PCI, hence
		 * we need to temporarily re-route transaction through DCORE0
		 */
		orig_addr = RREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET);
		orig_ctrl = RREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET, 0xBE000);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET, 0x2A190C1);

		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 32);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 32);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 32);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 32);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 27);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 27);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 27);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 27);

		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET, orig_addr);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET, orig_ctrl);
	}

	if (hdev->rotator_mask & 0x2) {
		WREG32(mmDCORE1_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_0, 5);
		WREG32(mmDCORE1_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_1, 5);
		WREG32(mmDCORE1_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_2, 5);
		WREG32(mmDCORE1_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_RD_SIZE_3, 5);
		WREG32(mmDCORE1_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_0, 4);
		WREG32(mmDCORE1_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_1, 4);
		WREG32(mmDCORE1_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_2, 4);
		WREG32(mmDCORE1_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_WR_SIZE_3, 4);
	}

	/* EMEM shared enable */
	WREG32(mmCPU_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	WREG32(mmPMMU_HBW_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	WREG32(mmPSOC_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);

	/* EMEM DCORE0 enable */
	WREG32(mmDCORE0_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	WREG32(mmDCORE0_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);

	if (hdev->mmu_enable) {
		WREG32(mmDCORE0_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	}

	if (hdev->mme_mask & 0x1) {
		WREG32(mmDCORE0_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x1)
		WREG32(mmDCORE0_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x2)
		WREG32(mmDCORE0_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x4)
		WREG32(mmDCORE0_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x8)
		WREG32(mmDCORE0_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x10)
		WREG32(mmDCORE0_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);

	if (hdev->asic_prop.decoder_enabled_mask & 0x1F)
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);

	if (hdev->rotator_mask & 0x1)
		WREG32(mmDCORE0_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);

	/* EMEM DCORE1 enable */
	WREG32(mmDCORE1_PDMA0_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	WREG32(mmDCORE1_DDMA_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);

	if (hdev->mmu_enable) {
		WREG32(mmDCORE1_HMMU0_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	}

	if (hdev->mme_mask & 0x2) {
		WREG32(mmDCORE1_MME_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x20)
		WREG32(mmDCORE1_TPC0_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x40)
		WREG32(mmDCORE1_TPC1_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x80)
		WREG32(mmDCORE1_TPC2_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x100)
		WREG32(mmDCORE1_TPC3_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x200)
		WREG32(mmDCORE1_TPC4_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);

	if (hdev->asic_prop.decoder_enabled_mask & 0x3E0) {
		/* Due to H/W issue we cannot access DCORE1 VSI registers from PCI, hence
		 * we need to temporarily re-route transaction through DCORE0
		 */
		orig_addr = RREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET);
		orig_ctrl = RREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET, 0xBE000);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET, 0x2A190C1);

		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);

		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET, orig_addr);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET, orig_ctrl);
	}

	if (hdev->rotator_mask & 0x2)
		WREG32(mmDCORE1_ROT_MSTR_IF_E2E_CRDT_E2E_EMEM_EN, 0x1);

	/* PCIE shared update values */
	value = prop->pmmu.host_resident ? 8 : 26;
	WREG32(mmCPU_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
	WREG32(mmCPU_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 0);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 0);
	value = prop->pmmu.host_resident ? 128 : 16;
	WREG32(mmPMMU_HBW_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
	WREG32(mmPMMU_HBW_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 0);
	value = prop->pmmu.host_resident ? 8 : 13;
	WREG32(mmPSOC_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
	WREG32(mmPSOC_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);

	/* PCIE DCORE0 update values */
	WREG32(mmDCORE0_KDMA_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 3);
	WREG32(mmDCORE0_KDMA_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 3);
	value = prop->pmmu.host_resident ? 16 : 34;
	WREG32(mmDCORE0_PDMA0_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
	value = prop->pmmu.host_resident ? 56 : 121;
	WREG32(mmDCORE0_PDMA0_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	WREG32(mmDCORE0_DDMA_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 1);
	WREG32(mmDCORE0_DDMA_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 1);
	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 2);
	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 2);

	if (hdev->mmu_enable) {
		WREG32(mmDCORE0_HMMU0_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 0);
		WREG32(mmDCORE0_HMMU0_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 0);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 0);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 0);
	}

	if (hdev->mme_mask & 0x1) {
		WREG32(mmDCORE0_MME_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 1);
		WREG32(mmDCORE0_MME_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 4);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 1);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 0);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 1);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 0);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 1);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 0);
	}

	value = prop->pmmu.host_resident ? 5 : 7;

	if (hdev->asic_prop.tpc_enabled_mask & 0x1) {
		WREG32(mmDCORE0_TPC0_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
		WREG32(mmDCORE0_TPC0_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x2) {
		WREG32(mmDCORE0_TPC1_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
		WREG32(mmDCORE0_TPC1_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x4) {
		WREG32(mmDCORE0_TPC2_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
		WREG32(mmDCORE0_TPC2_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x8) {
		WREG32(mmDCORE0_TPC3_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
		WREG32(mmDCORE0_TPC3_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x10) {
		WREG32(mmDCORE0_TPC4_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
		WREG32(mmDCORE0_TPC4_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	}

	if (hdev->asic_prop.decoder_enabled_mask & 0x1F) {
		value = prop->pmmu.host_resident ? 15 : 30;
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE,
				value);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE,
				value);
	}

	if (hdev->rotator_mask & 0x1) {
		WREG32(mmDCORE0_ROT_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 1);
		WREG32(mmDCORE0_ROT_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 1);
	}

	/* PCIE DCORE1 update values */
	WREG32(mmDCORE1_KDMA_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 3);
	WREG32(mmDCORE1_KDMA_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 3);
	value = prop->pmmu.host_resident ? 16 : 34;
	WREG32(mmDCORE1_PDMA0_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
	value = prop->pmmu.host_resident ? 56 : 121;
	WREG32(mmDCORE1_PDMA0_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	WREG32(mmDCORE1_DDMA_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 1);
	WREG32(mmDCORE1_DDMA_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 1);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 2);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 2);

	if (hdev->mmu_enable) {
		WREG32(mmDCORE1_HMMU0_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 0);
		WREG32(mmDCORE1_HMMU0_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 0);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 0);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 0);
	}

	if (hdev->mme_mask & 0x2) {
		WREG32(mmDCORE1_MME_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 1);
		WREG32(mmDCORE1_MME_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 4);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 1);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 0);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 1);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 0);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 1);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 0);
	}

	value = prop->pmmu.host_resident ? 5 : 7;

	if (hdev->asic_prop.tpc_enabled_mask & 0x20) {
		WREG32(mmDCORE1_TPC0_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
		WREG32(mmDCORE1_TPC0_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x40) {
		WREG32(mmDCORE1_TPC1_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
		WREG32(mmDCORE1_TPC1_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x80) {
		WREG32(mmDCORE1_TPC2_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
		WREG32(mmDCORE1_TPC2_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x100) {
		WREG32(mmDCORE1_TPC3_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
		WREG32(mmDCORE1_TPC3_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x200) {
		WREG32(mmDCORE1_TPC4_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
		WREG32(mmDCORE1_TPC4_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);
	}

	if (hdev->asic_prop.decoder_enabled_mask & 0x3E0) {
		/* Due to H/W issue we cannot access DCORE1 VSI registers from PCI, hence
		 * we need to temporarily re-route transaction through DCORE0
		 */
		orig_addr = RREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET);
		orig_ctrl = RREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET, 0xBE000);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET, 0x2A190C1);

		value = prop->pmmu.host_resident ? 23 : 43;
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, value);
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, value);

		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET, orig_addr);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET, orig_ctrl);
	}

	if (hdev->rotator_mask & 0x2) {
		WREG32(mmDCORE1_ROT_MSTR_IF_E2E_CRDT_E2E_PCI_RD_SIZE, 1);
		WREG32(mmDCORE1_ROT_MSTR_IF_E2E_CRDT_E2E_PCI_WR_SIZE, 1);
	}

	/* PCIE shared enable */
	WREG32(mmCPU_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	WREG32(mmPMMU_HBW_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	WREG32(mmPSOC_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);

	/* PCIE DCORE0 enable */
	WREG32(mmDCORE0_KDMA_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	WREG32(mmDCORE0_PDMA0_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	WREG32(mmDCORE0_DDMA_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);

	if (hdev->mmu_enable) {
		WREG32(mmDCORE0_HMMU0_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	}

	if (hdev->mme_mask & 0x1) {
		WREG32(mmDCORE0_MME_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x1)
		WREG32(mmDCORE0_TPC0_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x2)
		WREG32(mmDCORE0_TPC1_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x4)
		WREG32(mmDCORE0_TPC2_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x8)
		WREG32(mmDCORE0_TPC3_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x10)
		WREG32(mmDCORE0_TPC4_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);

	if (hdev->asic_prop.decoder_enabled_mask & 0x1F)
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);

	if (hdev->rotator_mask & 0x1)
		WREG32(mmDCORE0_ROT_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);

	/* PCIE DCORE1 enable */
	WREG32(mmDCORE1_KDMA_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	WREG32(mmDCORE1_PDMA0_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	WREG32(mmDCORE1_DDMA_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);

	if (hdev->mmu_enable) {
		WREG32(mmDCORE1_HMMU0_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	}

	if (hdev->mme_mask & 0x2) {
		WREG32(mmDCORE1_MME_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x20)
		WREG32(mmDCORE1_TPC0_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x40)
		WREG32(mmDCORE1_TPC1_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x80)
		WREG32(mmDCORE1_TPC2_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x100)
		WREG32(mmDCORE1_TPC3_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x200)
		WREG32(mmDCORE1_TPC4_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);

	if (hdev->asic_prop.decoder_enabled_mask & 0x3E0) {
		/* Due to H/W issue we cannot access DCORE1 VSI registers from PCI, hence
		 * we need to temporarily re-route transaction through DCORE0
		 */
		orig_addr = RREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET);
		orig_ctrl = RREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET, 0xBE000);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET, 0x2A190C1);

		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);

		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET, orig_addr);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET, orig_ctrl);
	}

	if (hdev->rotator_mask & 0x2)
		WREG32(mmDCORE1_ROT_MSTR_IF_E2E_CRDT_E2E_PCI_EN, 0x1);

	/*
	 * On palladium, the MMU PGT clearing will take a lot of time with the
	 * KDMA E2E credits. Therefore we configure them at the end of
	 * greco_late_init() after all KDMA initialization jobs are done.
	 */
	if (!hdev->pldm)
		greco_kdma_e2e_init(hdev);
}

static void greco_init_shared_mstr_if(struct hl_device *hdev)
{
	WREG32(mmPSOC_MSTR_IF_AXUSER_HB_OVRD_LO, MSTR_IF_HBW_LO_OVRD_DEFAULT);
	WREG32(mmPSOC_MSTR_IF_AXUSER_LB_OVRD, MSTR_IF_LBW_OVRD_DEFAULT);
}

/*
 * Override AXI User Bits
 * For HBW fabric, AWUSER and ARUSER defined as follows:
 *   [40:33]  X,Y coordinates
 *   [32]     Shared initiator indication (CPU/PSOC/PCIe/Encoder).
 *   [31]     REGULATOR
 *   [30]     Reserved, used for RAZWI indication by the Master IF.
 *   [29]     DDR close page mode
 *   [28]     Core index (0/1)
 *   [27:26]  Reserved
 *   [25:22]  QoS
 *   [21:20]  Reduction rounding mode
 *   [19:18]  Reduction operation
 *   [17:14]  Reduction data type
 *   [13]     Reduction indication
 *   [12]     No snoop
 *   [11]     Relaxed ordered
 *   [10]     MMU bypass
 *   [9:0]    ASID/Router coordinates
 *
 * For LBW fabric, both AWUSER and ARUSER:
 *   [31:24]  Y,X coordinates
 *   [23]     Shared initiator indication (CPU/PSOC/PCIe/Encoder).
 *   [10]     Lock indication (load lock, store unlock).
 *   [9:0]    Router coordinates for accessing RAZWI.
 */
static void greco_init_dcore_mstr_if(struct hl_device *hdev, int dcore_id)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 dcore_offset, tpc_mask_shift, tpc_offset;
	int i;

	dcore_offset = dcore_id * DCORE_OFFSET;
	tpc_mask_shift = dcore_id * NUM_OF_TPC_PER_DCORE;

	WREG32(mmDCORE0_KDMA_MSTR_IF_AXUSER_HB_OVRD_LO + dcore_offset,
			MSTR_IF_HBW_LO_OVRD_DEFAULT);

	WREG32(mmDCORE0_DDMA_MSTR_IF_AXUSER_HB_OVRD_LO + dcore_offset,
			MSTR_IF_HBW_LO_OVRD_DEFAULT);

	WREG32(mmDCORE0_PDMA0_MSTR_IF_AXUSER_HB_OVRD_LO + dcore_offset,
			MSTR_IF_HBW_LO_OVRD_DEFAULT);

	WREG32(mmDCORE0_PDMA1_MSTR_IF_AXUSER_HB_OVRD_LO + dcore_offset,
			MSTR_IF_HBW_LO_OVRD_DEFAULT);

	for (i = 0; i < NUM_OF_TPC_PER_DCORE; i++) {
		if (!(prop->tpc_enabled_mask & BIT(0 + tpc_mask_shift + i)))
			continue;

		tpc_offset = dcore_offset + i * DCORE_TPC_OFFSET;

		WREG32(mmDCORE0_TPC0_MSTR_IF_AXUSER_LB_OVRD + tpc_offset,
			MSTR_IF_LBW_OVRD_DEFAULT | MSTR_IF_LBW_OVRD_LOCK_MASK);
		WREG32(mmDCORE0_TPC0_MSTR_IF_AXUSER_HB_OVRD_LO + tpc_offset,
			MSTR_IF_HBW_LO_OVRD_DEFAULT);
		WREG32(mmDCORE0_TPC0_MSTR_IF_CORE_LBW_MSG_AWID + tpc_offset,
			0x800);
	}

	if (hdev->mme_mask) {
		WREG32(mmDCORE0_MME_MSTR_IF_AXUSER_HB_OVRD_LO + dcore_offset,
				MSTR_IF_HBW_LO_OVRD_DEFAULT);

		WREG32(mmDCORE0_MME_CTRL_LO_MME_AXUSER_HB_OVRD_LO +
								dcore_offset,
				MSTR_IF_HBW_LO_OVRD_REDUCTION_MASK);

		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_AXUSER_HB_OVRD_LO +
								dcore_offset,
				MSTR_IF_HBW_LO_ASID_MMUBP_MASK);

		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_AXUSER_HB_OVRD_LO +
								dcore_offset,
				MSTR_IF_HBW_LO_ASID_MMUBP_MASK);

		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_AXUSER_HB_OVRD_LO +
								dcore_offset,
				MSTR_IF_HBW_LO_ASID_MMUBP_MASK);
	}

	if (hdev->rotator_mask & BIT(dcore_id))
		WREG32(mmDCORE0_ROT_MSTR_IF_AXUSER_HB_OVRD_LO + dcore_offset,
				MSTR_IF_HBW_LO_OVRD_DEFAULT);
}

static void greco_init_mstr_if(struct hl_device *hdev)
{
	u32 dcore_id;

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++)
		greco_init_dcore_mstr_if(hdev, dcore_id);

	greco_init_shared_mstr_if(hdev);
}

static void greco_init_rtr_h3(struct hl_device *hdev)
{
	if (hdev->fw_components & FW_TYPE_LINUX)
		return;

	WREG32(mmDCORE0_TPCIF_RTR0_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE0_TPCIF_RTR0_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
	WREG32(mmDCORE0_TPCIF_RTR1_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE0_TPCIF_RTR1_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
	WREG32(mmDCORE0_TPCIF_RTR2_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE0_TPCIF_RTR2_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
	WREG32(mmDCORE0_TPCIF_RTR3_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE0_TPCIF_RTR3_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);

	WREG32(mmDCORE0_MMEIF_RTR0_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE0_MMEIF_RTR0_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
	WREG32(mmDCORE0_MMEIF_RTR1_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE0_MMEIF_RTR1_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
	WREG32(mmDCORE0_MMEIF_RTR2_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE0_MMEIF_RTR2_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
	WREG32(mmDCORE0_MMEIF_RTR3_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE0_MMEIF_RTR3_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);

	WREG32(mmDCORE1_TPCIF_RTR0_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE1_TPCIF_RTR0_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
	WREG32(mmDCORE1_TPCIF_RTR1_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE1_TPCIF_RTR1_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
	WREG32(mmDCORE1_TPCIF_RTR2_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE1_TPCIF_RTR2_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
	WREG32(mmDCORE1_TPCIF_RTR3_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE1_TPCIF_RTR3_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);

	WREG32(mmDCORE1_MMEIF_RTR0_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE1_MMEIF_RTR0_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
	WREG32(mmDCORE1_MMEIF_RTR1_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE1_MMEIF_RTR1_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
	WREG32(mmDCORE1_MMEIF_RTR2_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE1_MMEIF_RTR2_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
	WREG32(mmDCORE1_MMEIF_RTR3_H3_RTR_CH_THR_SRAM_RD_0, 0x80000);
	WREG32(mmDCORE1_MMEIF_RTR3_H3_RTR_CH_THR_MEM_RD_0, 0x4000000);
}

static int greco_load_boot_bin_to_device(struct hl_device *hdev)
{
	void __iomem *dst;

	/* For PLDM, need to initialize SRAM scrambler before pushing u-boot
	 * to SRAM
	 */
	greco_init_scrambler_sram(hdev);

	dst = hdev->pcie_bar[SRAM_CFG_BAR_ID] + UBOOT_FW_OFFSET;

	return hl_fw_load_fw_to_device(hdev, GRECO_UBOOT_FW_FILE, dst, 0, 0);
}

int greco_pldm_init_cpu(struct hl_device *hdev)
{
	int rc;

	/* Put ARM cores into reset */
	WREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL, CPU_RESET_ASSERT);
	RREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL);

	/* Reset the CA53 MACRO */
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N_M, 0);
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N_H, 0);
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N_L,
			1 << PSOC_GLOBAL_CONF_UNIT_RST_N_L_CPU_SHIFT);
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N,
			1 << PSOC_GLOBAL_CONF_UNIT_RST_N_IND_SHIFT);
	RREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N);

	usleep_range(50, 100);

	/* Take CA53 MACRO out of reset */
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N_L, 0);
	WREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N,
			1 << PSOC_GLOBAL_CONF_UNIT_RST_N_IND_SHIFT);
	RREG32(mmPSOC_GLOBAL_CONF_UNIT_RST_N);

	if (hdev->fw_components & FW_TYPE_BOOT_CPU) {
		rc = greco_load_boot_bin_to_device(hdev);
		if (rc)
			return rc;
	}

	if (hdev->fw_components & FW_TYPE_LINUX) {
		rc = greco_load_firmware_to_device(hdev);
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
	WREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL,
					CPU_RESET_CORE0_DEASSERT);
	RREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL);

	return 0;
}

void greco_init_scrambler_sram(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	u32 status;
	int rc;

	if (!hdev->sram_scrambler_enable)
		return;

	if (greco->hw_cap_initialized & HW_CAP_SRAM_SCRAMBLER)
		return;

	if (hdev->asic_prop.fw_security_enabled)
		return;

	if (hdev->asic_prop.fw_app_cpu_boot_dev_sts0 &
			CPU_BOOT_DEV_STS0_SRAM_SCR_EN)
		return;

	/* In case we don't load F/W app, we must wait for uboot to finish
	 * before we enable scrambling. Otherwise, we risk interrupting it in
	 * the middle of initialization, which can cause the device to get stuck
	 */
	if ((hdev->fw_components & (FW_TYPE_BOOT_CPU | FW_TYPE_LINUX))
							== FW_TYPE_BOOT_CPU) {
		dev_info(hdev->dev,
			"Waiting for u-boot to finish before enabling SRAM scrambler\n");

		rc = hl_poll_timeout(
			hdev,
			mmPSOC_GLOBAL_CONF_CPU_BOOT_STATUS,
			status,
			(status == CPU_BOOT_STATUS_READY_TO_BOOT) ||
			(status == CPU_BOOT_STATUS_SRAM_AVAIL),
			10000,
			GRECO_BOOT_FIT_REQ_TIMEOUT_USEC);

		if (rc)
			dev_warn(hdev->dev,
				"Failed to detect u-boot has finished loading F/W (status = %d). Maybe running old F/W?\n",
				status);

		if (status != CPU_BOOT_STATUS_SRAM_AVAIL)
			ssleep(1);

		/*
		 * In the case of Gaudi, if LinuxFW isn't loaded & bootfit isn't
		 * relocatable, we used to send WFE (Halt ARM) before SRAM
		 * scrambling, and thus to disable the SRAM-residing u-boot.
		 * This is NOT the case here, as we know that in production mode
		 * LinuxFW will always be on. i.e. any other scenario is for dbg
		 * so we allow EMB side to use u-boot console for debug purposes
		 */
	}

	dev_dbg(hdev->dev, "Enable SRAM scrambler\n");

	WREG32(mmDCORE0_MMEIF_RTR0_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE0_MMEIF_RTR1_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE0_MMEIF_RTR2_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE0_MMEIF_RTR3_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE0_TPCIF_RTR0_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE0_TPCIF_RTR1_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE0_TPCIF_RTR2_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE0_TPCIF_RTR3_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE1_MMEIF_RTR0_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE1_MMEIF_RTR1_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE1_MMEIF_RTR2_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE1_MMEIF_RTR3_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE1_TPCIF_RTR0_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE1_TPCIF_RTR1_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE1_TPCIF_RTR2_H3_SCRAM_SRAM_EN, 0x1);
	WREG32(mmDCORE1_TPCIF_RTR3_H3_SCRAM_SRAM_EN, 0x1);

	if (!(hdev->fw_components & FW_TYPE_PREBOOT_CPU)) {
		WREG32(mmDCON0_HBW_RTR_IF0_RTR_H3_SCRAM_SRAM_EN, 0x1);
		WREG32(mmDCON0_HBW_RTR_IF1_RTR_H3_SCRAM_SRAM_EN, 0x1);
		WREG32(mmDCON1_HBW_RTR_IF0_RTR_H3_SCRAM_SRAM_EN, 0x1);
		WREG32(mmDCON1_HBW_RTR_IF1_RTR_H3_SCRAM_SRAM_EN, 0x1);
		WREG32(mmDCON2_HBW_RTR_IF0_RTR_H3_SCRAM_SRAM_EN, 0x1);
		WREG32(mmDCON2_HBW_RTR_IF1_RTR_H3_SCRAM_SRAM_EN, 0x1);
		WREG32(mmDCON3_HBW_RTR_IF0_RTR_H3_SCRAM_SRAM_EN, 0x1);
		WREG32(mmDCON3_HBW_RTR_IF1_RTR_H3_SCRAM_SRAM_EN, 0x1);
	}

	greco->hw_cap_initialized |= HW_CAP_SRAM_SCRAMBLER;
}

void greco_cpu_init_scrambler_dram(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!hdev->dram_scrambler_enable)
		return;

	if (hdev->asic_prop.fw_security_enabled)
		return;

	if (hdev->asic_prop.fw_bootfit_cpu_boot_dev_sts0 &
			CPU_BOOT_DEV_STS0_DRAM_SCR_EN)
		return;

	if ((greco->hw_cap_initialized & HW_CAP_DRAM_SCRAMBLER_MASK) ==
			HW_CAP_DRAM_SCRAMBLER_MASK)
		return;

	dev_dbg(hdev->dev, "Enable CPU related DRAM scrambler\n");

	/*
	 * F/W is written to DRAM via PCIE and used by CPU, so need to enable
	 * scrambling in their MSTR_IF and their corresponding RTR blocks.
	 */
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	WREG32(mmDCON0_HBW_RTR_IF1_RTR_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmCPU_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	WREG32(mmDCON2_HBW_RTR_IF1_RTR_H3_SCRAM_MEM_EN, 0x1);
}

void greco_init_scrambler_dram(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	u32 orig_addr, orig_ctrl;

	if (!hdev->dram_scrambler_enable)
		return;

	if (hdev->asic_prop.fw_security_enabled)
		return;

	if (hdev->asic_prop.fw_bootfit_cpu_boot_dev_sts0 &
			CPU_BOOT_DEV_STS0_DRAM_SCR_EN)
		return;

	if ((greco->hw_cap_initialized & HW_CAP_DRAM_SCRAMBLER_MASK) ==
			HW_CAP_DRAM_SCRAMBLER_MASK)
		return;

	dev_dbg(hdev->dev, "Enable DRAM scrambler\n");

	WREG32(mmCPU_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	WREG32(mmPCIE_MSTR_RR_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	WREG32(mmPMMU_HBW_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	WREG32(mmPSOC_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);

	WREG32(mmDCORE0_KDMA_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	WREG32(mmDCORE1_KDMA_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);

	WREG32(mmDCORE0_PDMA0_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	WREG32(mmDCORE0_PDMA1_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	WREG32(mmDCORE1_PDMA0_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	WREG32(mmDCORE1_PDMA1_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);

	WREG32(mmDCORE0_DDMA_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	WREG32(mmDCORE1_DDMA_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);

	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	WREG32(mmDCORE1_SYNC_MNGR_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);

	if (hdev->mmu_enable) {
		WREG32(mmDCORE0_HMMU0_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
		WREG32(mmDCORE0_HMMU1_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
		WREG32(mmDCORE1_HMMU0_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
		WREG32(mmDCORE1_HMMU1_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	}

	if (hdev->mme_mask & 0x1) {
		WREG32(mmDCORE0_MME_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
		WREG32(mmDCORE0_MME_SBTEA_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN,
									0x1);
		WREG32(mmDCORE0_MME_SBTEB_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN,
									0x1);
		WREG32(mmDCORE0_MME_SBTEL_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN,
									0x1);
	}

	if (hdev->mme_mask & 0x2) {
		WREG32(mmDCORE1_MME_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
		WREG32(mmDCORE1_MME_SBTEA_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN,
									0x1);
		WREG32(mmDCORE1_MME_SBTEB_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN,
									0x1);
		WREG32(mmDCORE1_MME_SBTEL_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN,
									0x1);
	}

	if (hdev->asic_prop.tpc_enabled_mask & 0x1)
		WREG32(mmDCORE0_TPC0_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x2)
		WREG32(mmDCORE0_TPC1_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x4)
		WREG32(mmDCORE0_TPC2_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x8)
		WREG32(mmDCORE0_TPC3_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x10)
		WREG32(mmDCORE0_TPC4_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x20)
		WREG32(mmDCORE1_TPC0_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x40)
		WREG32(mmDCORE1_TPC1_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x80)
		WREG32(mmDCORE1_TPC2_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x100)
		WREG32(mmDCORE1_TPC3_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	if (hdev->asic_prop.tpc_enabled_mask & 0x200)
		WREG32(mmDCORE1_TPC4_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);

	if (hdev->asic_prop.decoder_enabled_mask & 0x1F)
		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);

	if (hdev->asic_prop.decoder_enabled_mask & 0x3E0) {
		/* Due to H/W issue we cannot access DCORE1 VSI registers from PCI, hence
		 * we need to temporarily re-route transaction through DCORE0
		 */
		orig_addr = RREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET);
		orig_ctrl = RREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET, 0xBE000);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET, 0x2A190C1);

		WREG32(mmDCORE0_VSI_WRAP_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);

		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_BASE_ADDR_OFFSET, orig_addr);
		WREG32(mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE + LBW_DECODE_CTRL_OFFSET, orig_ctrl);
	}

	if (hdev->rotator_mask & 0x1)
		WREG32(mmDCORE0_ROT_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);
	if (hdev->rotator_mask & 0x2)
		WREG32(mmDCORE1_ROT_MSTR_IF_CORE_HBW_SCRAM_EXTMEM_EN, 0x1);

	if (greco->hw_cap_initialized & HW_CAP_DRAM_SCRAMBLER_HW_RESET)
		goto out;

	WREG32(mmDCORE0_TPCIF_RTR0_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE0_TPCIF_RTR1_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE0_TPCIF_RTR2_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE0_TPCIF_RTR3_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE1_TPCIF_RTR0_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE1_TPCIF_RTR1_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE1_TPCIF_RTR2_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE1_TPCIF_RTR3_H3_SCRAM_MEM_EN, 0x1);

	WREG32(mmDCORE0_MMEIF_RTR0_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE0_MMEIF_RTR1_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE0_MMEIF_RTR2_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE0_MMEIF_RTR3_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE1_MMEIF_RTR0_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE1_MMEIF_RTR1_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE1_MMEIF_RTR2_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCORE1_MMEIF_RTR3_H3_SCRAM_MEM_EN, 0x1);

	WREG32(mmDCON0_HBW_RTR_IF0_RTR_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCON0_HBW_RTR_IF1_RTR_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCON1_HBW_RTR_IF0_RTR_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCON1_HBW_RTR_IF1_RTR_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCON2_HBW_RTR_IF0_RTR_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCON2_HBW_RTR_IF1_RTR_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCON3_HBW_RTR_IF0_RTR_H3_SCRAM_MEM_EN, 0x1);
	WREG32(mmDCON3_HBW_RTR_IF1_RTR_H3_SCRAM_MEM_EN, 0x1);

	greco->hw_cap_initialized |= HW_CAP_DRAM_SCRAMBLER_HW_RESET;
out:
	greco->hw_cap_initialized |= HW_CAP_DRAM_SCRAMBLER_SW_RESET;
}

void greco_pre_hw_init(struct hl_device *hdev)
{
	/* Set the access through PCI bars (Linux driver only) as secured */
	WREG32(mmPCIE_WRAP_PCIE_PROT_OVR, (PCIE_WRAP_PCIE_PROT_OVR_RD_EN_MASK |
					PCIE_WRAP_PCIE_PROT_OVR_WR_EN_MASK));

	/* Set PCI as a shared component */
	WREG32_OR(mmPCIE_WRAP_PCIE_ARUSER_OVR_1, 1);
	WREG32_OR(mmPCIE_WRAP_PCIE_ARUSER_OVR_EN_1, 1);
	WREG32_OR(mmPCIE_WRAP_PCIE_AWUSER_OVR_1, 1);
	WREG32_OR(mmPCIE_WRAP_PCIE_AWUSER_OVR_EN_1, 1);

	/*
	 * Perform read to flush the waiting writes to ensure configuration
	 * was set in the device
	 */
	RREG32(mmPCIE_WRAP_PCIE_PROT_OVR);

	if (hdev->axi_drain == AXI_DRAIN_ENABLED) {
		hl_pci_elbi_write(hdev,
				CFG_BASE + mmPCIE_WRAP_HBW_DRAIN_TIMEOUT,
				0x1000);
		hl_pci_elbi_write(hdev, CFG_BASE + mmPCIE_WRAP_HBW_DRAIN_CFG,
				1 << PCIE_WRAP_HBW_DRAIN_CFG_EN_SHIFT);

		hl_pci_elbi_write(hdev,
				CFG_BASE + mmPCIE_WRAP_LBW_DRAIN_TIMEOUT,
				0x5000);
		hl_pci_elbi_write(hdev, CFG_BASE + mmPCIE_WRAP_LBW_DRAIN_CFG,
				1 << PCIE_WRAP_LBW_DRAIN_CFG_EN_SHIFT);

		WREG32(mmPSOC_GLOBAL_CONF_AXI_DRAIN_TIMEOUT, 0x4000);
		RMWREG32(mmPSOC_GLOBAL_CONF_AXI_DRAIN_CTRL, 1,
				PSOC_GLOBAL_CONF_AXI_DRAIN_CTRL_EN_MASK);
	} else if (hdev->axi_drain == AXI_DRAIN_DISABLED) {
		hl_pci_elbi_write(hdev, CFG_BASE + mmPCIE_WRAP_HBW_DRAIN_CFG,
				0);
		hl_pci_elbi_write(hdev, CFG_BASE + mmPCIE_WRAP_LBW_DRAIN_CFG,
				0);

		RMWREG32(mmPSOC_GLOBAL_CONF_AXI_DRAIN_CTRL, 0,
				PSOC_GLOBAL_CONF_AXI_DRAIN_CTRL_EN_MASK);
	}

	/* Avoid resetting KDMA as part of soft reset */
	if (!(hdev->fw_components & FW_TYPE_LINUX) || hdev->pldm)
		RMWREG32(mmPSOC_GLOBAL_CONF_SOFT_RST_CFG_M, 0,
				PSOC_GLOBAL_CONF_SOFT_RST_CFG_M_KDMA_MASK);
}

static int __greco_init_pll(struct hl_device *hdev,
				u32 pll_index,
				struct greco_pll_params *pll_params)
{
	u32 ctrl_cfg, reg_base;
	int rc, i;

	reg_base = greco_pll_block_bases[pll_index];

	WREG32(reg_base + PLL_CTRL_CFG_DIV_OFFS, pll_params->div_cfg);
	WREG32(reg_base + PLL_CTRL_CFG_OFFS, 0x101);
	WREG32(reg_base + PLL_CTRL_DATA_CHNG_OFFS, 0x11);
	ctrl_cfg =  RREG32(reg_base + PLL_CTRL_CFG_OFFS);

	if (!hdev->pldm) {
		/* Wait for PLL lock */
		rc = hl_poll_timeout(
			hdev,
			reg_base + PLL_CTRL_CFG_OFFS,
			ctrl_cfg,
			((ctrl_cfg & 0x00010101) != 0x00010101),
			100,
			GRECO_PLL_TIMEOUT_USEC);

		if (rc) {
			dev_err(hdev->dev, "Failed to get PLL %d lock\n",
				pll_index);
			return -EIO;
		}
	}

	for (i = 0 ; i < 4 ; i++) {
		WREG32(reg_base + PLL_CTRL_DIV_FACTOR_0_OFFS + i * 4,
						pll_params->div_fact[i]);
		WREG32(reg_base + PLL_CTRL_DIV_FACTOR_CMD_0_OFFS + i * 4, 0x1);
		WREG32(reg_base + PLL_CTRL_DIV_EN_0_OFFS + i * 4, 0x1);
		WREG32(reg_base + PLL_CTRL_DIV_SEL_0_OFFS + i * 4,
						pll_params->div_sel[i]);
	}

	return 0;
}

int greco_init_pll(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	struct greco_pll_params pll_params;
	int i, rc;

	if (!hdev->config_pll)
		return 0;

	if (greco->hw_cap_initialized & HW_CAP_PLL)
		return 0;

	if (hdev->fw_components & FW_TYPE_BOOT_CPU) {
		dev_dbg(hdev->dev,
			"Waiting 5s for u-boot before configuring PLLs\n");
		ssleep(5);
	}

	dev_dbg(hdev->dev, "Configure PCI PLL\n");

	/* 500Mhz FBDIV 60 POSTDIV1 3 POSTDIV2 2 REFDIV 1 */
	pll_params.div_cfg = FIELD_PREP(PLL_DIV_CFG_FBDIV_MASK, 60);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV1_MASK, 3);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV2_MASK, 2);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_REFDIV_MASK, 1);
	pll_params.div_fact[0] = 0;
	pll_params.div_sel[0] = PLL_CTRL_DIV_SEL_PLL_CLK;
	pll_params.div_fact[1] = 0;
	pll_params.div_sel[1] = PLL_CTRL_DIV_SEL_PLL_CLK;
	pll_params.div_fact[2] = 4;
	pll_params.div_sel[2] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[3] = 0;
	pll_params.div_sel[3] = PLL_CTRL_DIV_SEL_REF_CLK;

	rc = __greco_init_pll(hdev, HL_GRECO_PCI_PLL, &pll_params);
	if (rc)
		return rc;

	dev_dbg(hdev->dev, "Configure MESH PLL\n");

	/* 1500Mhz FBDIV 60 POSTDIV1 2 POSTDIV2 1 REFDIV 1 */
	pll_params.div_cfg = FIELD_PREP(PLL_DIV_CFG_FBDIV_MASK, 60);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV1_MASK, 2);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV2_MASK, 1);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_REFDIV_MASK, 1);
	pll_params.div_fact[0] = 0;
	pll_params.div_sel[0] = PLL_CTRL_DIV_SEL_PLL_CLK;
	pll_params.div_fact[1] = 1;
	pll_params.div_sel[1] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[2] = 1;
	pll_params.div_sel[2] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[3] = 11;
	pll_params.div_sel[3] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;

	rc = __greco_init_pll(hdev, HL_GRECO_MESH_PLL, &pll_params);
	if (rc)
		return rc;

	dev_dbg(hdev->dev, "Configure BANK PLL\n");

	/* 350Mhz FBDIV 63 POSTDIV1 3 POSTDIV2 3 REFDIV 1 */
	pll_params.div_cfg = FIELD_PREP(PLL_DIV_CFG_FBDIV_MASK, 63);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV1_MASK, 3);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV2_MASK, 3);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_REFDIV_MASK, 1);
	pll_params.div_fact[0] = 0;
	pll_params.div_sel[0] = PLL_CTRL_DIV_SEL_PLL_CLK;
	pll_params.div_fact[1] = 0;
	pll_params.div_sel[1] = PLL_CTRL_DIV_SEL_REF_CLK;
	pll_params.div_fact[2] = 0;
	pll_params.div_sel[2] = PLL_CTRL_DIV_SEL_REF_CLK;
	pll_params.div_fact[3] = 0;
	pll_params.div_sel[3] = PLL_CTRL_DIV_SEL_REF_CLK;

	rc = __greco_init_pll(hdev, HL_GRECO_SRAM_PLL, &pll_params);
	if (rc)
		return rc;

	dev_dbg(hdev->dev, "Configure SIF PLL\n");

	/* 1350Mhz FBDIV 54 POSTDIV1 2 POSTDIV2 1 REFDIV 1 */
	pll_params.div_cfg = FIELD_PREP(PLL_DIV_CFG_FBDIV_MASK, 54);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV1_MASK, 2);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV2_MASK, 1);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_REFDIV_MASK, 1);
	pll_params.div_fact[0] = 0;
	pll_params.div_sel[0] = PLL_CTRL_DIV_SEL_PLL_CLK;
	pll_params.div_fact[1] = 1;
	pll_params.div_sel[1] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[2] = 1;
	pll_params.div_sel[2] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[3] = 10;
	pll_params.div_sel[3] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;

	rc = __greco_init_pll(hdev, HL_GRECO_SIF_PLL, &pll_params);
	if (rc)
		return rc;

	dev_dbg(hdev->dev, "Configure DDR0/1 PLL\n");

	/* 800Mhz FBDIV 64 POSTDIV1 2 POSTDIV2 2 REFDIV 1 */
	pll_params.div_cfg = FIELD_PREP(PLL_DIV_CFG_FBDIV_MASK, 64);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV1_MASK, 2);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV2_MASK, 2);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_REFDIV_MASK, 1);
	pll_params.div_fact[0] = 0;
	pll_params.div_sel[0] = PLL_CTRL_DIV_SEL_PLL_CLK;
	pll_params.div_fact[1] = 5;
	pll_params.div_sel[1] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[2] = 0;
	pll_params.div_sel[2] = PLL_CTRL_DIV_SEL_REF_CLK;
	pll_params.div_fact[3] = 0;
	pll_params.div_sel[3] = PLL_CTRL_DIV_SEL_REF_CLK;

	for (i = HL_GRECO_DDR0_PLL ; i <= HL_GRECO_DDR1_PLL; i++) {
		rc = __greco_init_pll(hdev, i, &pll_params);
		if (rc)
			return rc;
	}

	dev_dbg(hdev->dev, "Configure MME PLL\n");

	/* 1500Mhz FBDIV 60 POSTDIV1 2 POSTDIV2 1 REFDIV 1 */
	pll_params.div_cfg = FIELD_PREP(PLL_DIV_CFG_FBDIV_MASK, 60);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV1_MASK, 2);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV2_MASK, 1);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_REFDIV_MASK, 1);
	pll_params.div_fact[0] = 0;
	pll_params.div_sel[0] = PLL_CTRL_DIV_SEL_PLL_CLK;
	pll_params.div_fact[1] = 1;
	pll_params.div_sel[1] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[2] = 2;
	pll_params.div_sel[2] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[3] = 12;
	pll_params.div_sel[3] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;

	rc = __greco_init_pll(hdev, HL_GRECO_MME_PLL, &pll_params);
	if (rc)
		return rc;

	dev_dbg(hdev->dev, "Configure TPC PLL\n");

	/* 1450Mhz FBDIV 58 POSTDIV1 2 POSTDIV2 1 REFDIV 1 */
	pll_params.div_cfg = FIELD_PREP(PLL_DIV_CFG_FBDIV_MASK, 58);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV1_MASK, 2);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV2_MASK, 1);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_REFDIV_MASK, 1);
	pll_params.div_fact[0] = 0;
	pll_params.div_sel[0] = PLL_CTRL_DIV_SEL_PLL_CLK;
	pll_params.div_fact[1] = 1;
	pll_params.div_sel[1] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[2] = 2;
	pll_params.div_sel[2] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[3] = 11;
	pll_params.div_sel[3] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;

	rc = __greco_init_pll(hdev, HL_GRECO_TPC_PLL, &pll_params);
	if (rc)
		return rc;

	dev_dbg(hdev->dev, "Configure VIDEO PLL\n");

	/* 500Mhz FBDIV 60 POSTDIV1 3 POSTDIV2 2 REFDIV 1 */
	pll_params.div_cfg = FIELD_PREP(PLL_DIV_CFG_FBDIV_MASK, 60);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV1_MASK, 3);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV2_MASK, 2);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_REFDIV_MASK, 1);
	pll_params.div_fact[0] = 0;
	pll_params.div_sel[0] = PLL_CTRL_DIV_SEL_PLL_CLK;
	pll_params.div_fact[1] = 2;
	pll_params.div_sel[1] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[2] = 2;
	pll_params.div_sel[2] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[3] = 0;
	pll_params.div_sel[3] = PLL_CTRL_DIV_SEL_REF_CLK;

	rc = __greco_init_pll(hdev, HL_GRECO_VID_PLL, &pll_params);
	if (rc)
		return rc;

	dev_dbg(hdev->dev, "Configure MMU PLL\n");

	/* 800Mhz FBDIV 64 POSTDIV1 2 POSTDIV2 2 REFDIV 1 */
	pll_params.div_cfg = FIELD_PREP(PLL_DIV_CFG_FBDIV_MASK, 64);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV1_MASK, 2);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV2_MASK, 2);
	pll_params.div_cfg |= FIELD_PREP(PLL_DIV_CFG_REFDIV_MASK, 1);
	pll_params.div_fact[0] = 0;
	pll_params.div_sel[0] = PLL_CTRL_DIV_SEL_PLL_CLK;
	pll_params.div_fact[1] = 1;
	pll_params.div_sel[1] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[2] = 1;
	pll_params.div_sel[2] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;
	pll_params.div_fact[3] = 5;
	pll_params.div_sel[3] = PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK;

	rc = __greco_init_pll(hdev, HL_GRECO_MMU_PLL, &pll_params);
	if (rc)
		return rc;

	greco->hw_cap_initialized |= HW_CAP_PLL;

	return 0;
}

static void greco_init_sync_mngr_glbl(struct hl_device *hdev)
{
	/*
	 * Configure Sync manager L2H address boundary:
	 * LBW range 0x7FFC000000 - 0x7FFCE00000, split to High and Low address
	 * registers and address masks. Lsb address is in units of 1MB.
	 */
	WREG32(mmDCORE0_SYNC_MNGR_GLBL_L2H_CPMR_1, CFG_BASE >> 32);
	WREG32(mmDCORE0_SYNC_MNGR_GLBL_L2H_CPMR_0, (CFG_BASE & U32_MAX) >> 20);
	WREG32(mmDCORE0_SYNC_MNGR_GLBL_L2H_MASK_1, U32_MAX);
	WREG32(mmDCORE0_SYNC_MNGR_GLBL_L2H_MASK_0, 0xFF0);

	WREG32(mmDCORE1_SYNC_MNGR_GLBL_L2H_CPMR_1, CFG_BASE >> 32);
	WREG32(mmDCORE1_SYNC_MNGR_GLBL_L2H_CPMR_0, (CFG_BASE & U32_MAX) >> 20);
	WREG32(mmDCORE1_SYNC_MNGR_GLBL_L2H_MASK_1, U32_MAX);
	WREG32(mmDCORE1_SYNC_MNGR_GLBL_L2H_MASK_0, 0xFF0);
}

void greco_init_golden_registers(struct hl_device *hdev)
{
	greco_init_binning(hdev);
	greco_init_e2e(hdev);
	greco_init_sync_mngr_glbl(hdev);
	greco_init_mstr_if(hdev);
	greco_init_rtr_h3(hdev);

	/* Bug H5-1691: in PCIe funnel, ROM did not configure TPC_CH_FUNNEL3 */
	if (!(hdev->fw_components & FW_TYPE_LINUX)) {
		WREG32(mmDCON2_DBG_DECODE_CTRL_0, 0x151F001);
		WREG32(mmDCON2_DBG_DECODE_BASE_ADDR_0, 0xFE001000);
	}

	/* BUG SIV-49: PMMU miss blocking when rd+wr credits > 128 */
	WREG32(mmPMMU_PIF_WR_CORE_CREDITS_THRESHOLD, 0x40 << 18);
	WREG32(mmPMMU_PIF_RD_CORE_CREDITS_THRESHOLD, 0x40 << 18);
	WREG32(mmPMMU_PIF_CORE_CREDITS_THRESHOLD, 0x80 << 18);

	/* Bug H5-1959: LBW write corrupted when reaching max inflights */
	WREG32(mmDCORE0_MME_MSTR_IF_CORE_LBW_MAX_OUTSTANDING_WR_TOTAL,
				MSTR_IF_MME_LBW_MAX_OUTSTANDING_WR_TOTAL);
	WREG32(mmDCORE0_MME_MSTR_IF_CORE_LBW_MAX_OUTSTANDING_WR_TOTAL +
								DCORE_OFFSET,
				MSTR_IF_MME_LBW_MAX_OUTSTANDING_WR_TOTAL);
}

void greco_enable_clock_gating(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	struct asic_fixed_properties *asic_prop = &hdev->asic_prop;
	bool encoder_enabled = !!(hdev->asic_prop.decoder_enabled_mask & 0x3E0);

	/* The TPC's internal SB CG must be always disabled due to H5-1902 */
	if (asic_prop->tpc_enabled_mask & 0x1)
		WREG32(mmDCORE0_TPC0_CFG_TSB_CFG, 0x0);
	if (asic_prop->tpc_enabled_mask & 0x2)
		WREG32(mmDCORE0_TPC1_CFG_TSB_CFG, 0x0);
	if (asic_prop->tpc_enabled_mask & 0x4)
		WREG32(mmDCORE0_TPC2_CFG_TSB_CFG, 0x0);
	if (asic_prop->tpc_enabled_mask & 0x8)
		WREG32(mmDCORE0_TPC3_CFG_TSB_CFG, 0x0);
	if (asic_prop->tpc_enabled_mask & 0x10)
		WREG32(mmDCORE0_TPC4_CFG_TSB_CFG, 0x0);
	if (asic_prop->tpc_enabled_mask & 0x20)
		WREG32(mmDCORE1_TPC0_CFG_TSB_CFG, 0x0);
	if (asic_prop->tpc_enabled_mask & 0x40)
		WREG32(mmDCORE1_TPC1_CFG_TSB_CFG, 0x0);
	if (asic_prop->tpc_enabled_mask & 0x80)
		WREG32(mmDCORE1_TPC2_CFG_TSB_CFG, 0x0);
	if (asic_prop->tpc_enabled_mask & 0x100)
		WREG32(mmDCORE1_TPC3_CFG_TSB_CFG, 0x0);
	if (asic_prop->tpc_enabled_mask & 0x200)
		WREG32(mmDCORE1_TPC4_CFG_TSB_CFG, 0x0);

	if (!hdev->clock_gating_enabled)
		return;

	if (greco->hw_cap_initialized & HW_CAP_CLK_GATE)
		return;

	dev_dbg_ratelimited(hdev->dev, "Enable Clock Gating\n");

	if (asic_prop->tpc_enabled_mask & 0x1) {
		WREG32(mmDCORE0_TPC0_QM_CGM_CFG, 0x80100080);
		WREG32(mmDCORE0_TPC0_CFG_CGU_CNT, 0x0);
		WREG32(mmDCORE0_TPC0_CFG_CGU_CPE_0, 0x0);
		WREG32(mmDCORE0_TPC0_CFG_CGU_CPE_1, 0x0);
		WREG32(mmDCORE0_TPC0_CFG_CGU_CPE_2, 0x0);
		WREG32(mmDCORE0_TPC0_CFG_CGU_CPE_3, 0x0);
		WREG32(mmDCORE0_TPC0_CFG_CGU_CPE_4, 0x0);
		WREG32(mmDCORE0_TPC0_CFG_CGU_CPE_5, 0x0);
		WREG32(mmDCORE0_TPC0_CFG_CGU_CPE_6, 0x0);
		WREG32(mmDCORE0_TPC0_CFG_CGU_CPE_7, 0x0);
	}
	if (asic_prop->tpc_enabled_mask & 0x2) {
		WREG32(mmDCORE0_TPC1_QM_CGM_CFG, 0x80100080);
		WREG32(mmDCORE0_TPC1_CFG_CGU_CNT, 0x0);
		WREG32(mmDCORE0_TPC1_CFG_CGU_CPE_0, 0x0);
		WREG32(mmDCORE0_TPC1_CFG_CGU_CPE_1, 0x0);
		WREG32(mmDCORE0_TPC1_CFG_CGU_CPE_2, 0x0);
		WREG32(mmDCORE0_TPC1_CFG_CGU_CPE_3, 0x0);
		WREG32(mmDCORE0_TPC1_CFG_CGU_CPE_4, 0x0);
		WREG32(mmDCORE0_TPC1_CFG_CGU_CPE_5, 0x0);
		WREG32(mmDCORE0_TPC1_CFG_CGU_CPE_6, 0x0);
		WREG32(mmDCORE0_TPC1_CFG_CGU_CPE_7, 0x0);
	}
	if (asic_prop->tpc_enabled_mask & 0x4) {
		WREG32(mmDCORE0_TPC2_QM_CGM_CFG, 0x80100080);
		WREG32(mmDCORE0_TPC2_CFG_CGU_CNT, 0x0);
		WREG32(mmDCORE0_TPC2_CFG_CGU_CPE_0, 0x0);
		WREG32(mmDCORE0_TPC2_CFG_CGU_CPE_1, 0x0);
		WREG32(mmDCORE0_TPC2_CFG_CGU_CPE_2, 0x0);
		WREG32(mmDCORE0_TPC2_CFG_CGU_CPE_3, 0x0);
		WREG32(mmDCORE0_TPC2_CFG_CGU_CPE_4, 0x0);
		WREG32(mmDCORE0_TPC2_CFG_CGU_CPE_5, 0x0);
		WREG32(mmDCORE0_TPC2_CFG_CGU_CPE_6, 0x0);
		WREG32(mmDCORE0_TPC2_CFG_CGU_CPE_7, 0x0);
	}
	if (asic_prop->tpc_enabled_mask & 0x8) {
		WREG32(mmDCORE0_TPC3_QM_CGM_CFG, 0x80100080);
		WREG32(mmDCORE0_TPC3_CFG_CGU_CNT, 0x0);
		WREG32(mmDCORE0_TPC3_CFG_CGU_CPE_0, 0x0);
		WREG32(mmDCORE0_TPC3_CFG_CGU_CPE_1, 0x0);
		WREG32(mmDCORE0_TPC3_CFG_CGU_CPE_2, 0x0);
		WREG32(mmDCORE0_TPC3_CFG_CGU_CPE_3, 0x0);
		WREG32(mmDCORE0_TPC3_CFG_CGU_CPE_4, 0x0);
		WREG32(mmDCORE0_TPC3_CFG_CGU_CPE_5, 0x0);
		WREG32(mmDCORE0_TPC3_CFG_CGU_CPE_6, 0x0);
		WREG32(mmDCORE0_TPC3_CFG_CGU_CPE_7, 0x0);
	}
	if (asic_prop->tpc_enabled_mask & 0x10) {
		WREG32(mmDCORE0_TPC4_QM_CGM_CFG, 0x80100080);
		WREG32(mmDCORE0_TPC4_CFG_CGU_CNT, 0x0);
		WREG32(mmDCORE0_TPC4_CFG_CGU_CPE_0, 0x0);
		WREG32(mmDCORE0_TPC4_CFG_CGU_CPE_1, 0x0);
		WREG32(mmDCORE0_TPC4_CFG_CGU_CPE_2, 0x0);
		WREG32(mmDCORE0_TPC4_CFG_CGU_CPE_3, 0x0);
		WREG32(mmDCORE0_TPC4_CFG_CGU_CPE_4, 0x0);
		WREG32(mmDCORE0_TPC4_CFG_CGU_CPE_5, 0x0);
		WREG32(mmDCORE0_TPC4_CFG_CGU_CPE_6, 0x0);
		WREG32(mmDCORE0_TPC4_CFG_CGU_CPE_7, 0x0);
	}
	if (asic_prop->tpc_enabled_mask & 0x20) {
		WREG32(mmDCORE1_TPC0_QM_CGM_CFG, 0x80100080);
		WREG32(mmDCORE1_TPC0_CFG_CGU_CNT, 0x0);
		WREG32(mmDCORE1_TPC0_CFG_CGU_CPE_0, 0x0);
		WREG32(mmDCORE1_TPC0_CFG_CGU_CPE_1, 0x0);
		WREG32(mmDCORE1_TPC0_CFG_CGU_CPE_2, 0x0);
		WREG32(mmDCORE1_TPC0_CFG_CGU_CPE_3, 0x0);
		WREG32(mmDCORE1_TPC0_CFG_CGU_CPE_4, 0x0);
		WREG32(mmDCORE1_TPC0_CFG_CGU_CPE_5, 0x0);
		WREG32(mmDCORE1_TPC0_CFG_CGU_CPE_6, 0x0);
		WREG32(mmDCORE1_TPC0_CFG_CGU_CPE_7, 0x0);
	}
	if (asic_prop->tpc_enabled_mask & 0x40) {
		WREG32(mmDCORE1_TPC1_QM_CGM_CFG, 0x80100080);
		WREG32(mmDCORE1_TPC1_CFG_CGU_CNT, 0x0);
		WREG32(mmDCORE1_TPC1_CFG_CGU_CPE_0, 0x0);
		WREG32(mmDCORE1_TPC1_CFG_CGU_CPE_1, 0x0);
		WREG32(mmDCORE1_TPC1_CFG_CGU_CPE_2, 0x0);
		WREG32(mmDCORE1_TPC1_CFG_CGU_CPE_3, 0x0);
		WREG32(mmDCORE1_TPC1_CFG_CGU_CPE_4, 0x0);
		WREG32(mmDCORE1_TPC1_CFG_CGU_CPE_5, 0x0);
		WREG32(mmDCORE1_TPC1_CFG_CGU_CPE_6, 0x0);
		WREG32(mmDCORE1_TPC1_CFG_CGU_CPE_7, 0x0);
	}
	if (asic_prop->tpc_enabled_mask & 0x80) {
		WREG32(mmDCORE1_TPC2_QM_CGM_CFG, 0x80100080);
		WREG32(mmDCORE1_TPC2_CFG_CGU_CNT, 0x0);
		WREG32(mmDCORE1_TPC2_CFG_CGU_CPE_0, 0x0);
		WREG32(mmDCORE1_TPC2_CFG_CGU_CPE_1, 0x0);
		WREG32(mmDCORE1_TPC2_CFG_CGU_CPE_2, 0x0);
		WREG32(mmDCORE1_TPC2_CFG_CGU_CPE_3, 0x0);
		WREG32(mmDCORE1_TPC2_CFG_CGU_CPE_4, 0x0);
		WREG32(mmDCORE1_TPC2_CFG_CGU_CPE_5, 0x0);
		WREG32(mmDCORE1_TPC2_CFG_CGU_CPE_6, 0x0);
		WREG32(mmDCORE1_TPC2_CFG_CGU_CPE_7, 0x0);
	}
	if (asic_prop->tpc_enabled_mask & 0x100) {
		WREG32(mmDCORE1_TPC3_QM_CGM_CFG, 0x80100080);
		WREG32(mmDCORE1_TPC3_CFG_CGU_CNT, 0x0);
		WREG32(mmDCORE1_TPC3_CFG_CGU_CPE_0, 0x0);
		WREG32(mmDCORE1_TPC3_CFG_CGU_CPE_1, 0x0);
		WREG32(mmDCORE1_TPC3_CFG_CGU_CPE_2, 0x0);
		WREG32(mmDCORE1_TPC3_CFG_CGU_CPE_3, 0x0);
		WREG32(mmDCORE1_TPC3_CFG_CGU_CPE_4, 0x0);
		WREG32(mmDCORE1_TPC3_CFG_CGU_CPE_5, 0x0);
		WREG32(mmDCORE1_TPC3_CFG_CGU_CPE_6, 0x0);
		WREG32(mmDCORE1_TPC3_CFG_CGU_CPE_7, 0x0);
	}
	if (asic_prop->tpc_enabled_mask & 0x200) {
		WREG32(mmDCORE1_TPC4_QM_CGM_CFG, 0x80100080);
		WREG32(mmDCORE1_TPC4_CFG_CGU_CNT, 0x0);
		WREG32(mmDCORE1_TPC4_CFG_CGU_CPE_0, 0x0);
		WREG32(mmDCORE1_TPC4_CFG_CGU_CPE_1, 0x0);
		WREG32(mmDCORE1_TPC4_CFG_CGU_CPE_2, 0x0);
		WREG32(mmDCORE1_TPC4_CFG_CGU_CPE_3, 0x0);
		WREG32(mmDCORE1_TPC4_CFG_CGU_CPE_4, 0x0);
		WREG32(mmDCORE1_TPC4_CFG_CGU_CPE_5, 0x0);
		WREG32(mmDCORE1_TPC4_CFG_CGU_CPE_6, 0x0);
		WREG32(mmDCORE1_TPC4_CFG_CGU_CPE_7, 0x0);
	}

	if (hdev->mme_mask) {
		WREG32(mmDCORE0_MME_QM_CGM_CFG, 0x80100080);
		/* DCORE1_MME is in slave mode, so we do not init QM_CGM_CFG */
		WREG32(mmDCORE0_MME_CTRL_LO_EU, 0xFFFF1E);
		WREG32(mmDCORE1_MME_CTRL_LO_EU, 0xFFFF1E);
		WREG32(mmDCORE0_MME_ACC_CLK_GATE_EN, 0);
		WREG32(mmDCORE1_MME_ACC_CLK_GATE_EN, 0);
		WREG32(mmDCORE0_MME_SBTEL_ENABLE_CGATE, 0x111);
		WREG32(mmDCORE1_MME_SBTEL_ENABLE_CGATE, 0x111);
		WREG32(mmDCORE0_MME_SBTEA_ENABLE_CGATE, 0x111);
		WREG32(mmDCORE1_MME_SBTEA_ENABLE_CGATE, 0x111);
		WREG32(mmDCORE0_MME_SRAM_L0_DISABLE_CG, 0);
		WREG32(mmDCORE1_MME_SRAM_L0_DISABLE_CG, 0);
		WREG32(mmDCORE0_MME_SBTEB_ENABLE_CGATE, 0x111);
		WREG32(mmDCORE1_MME_SBTEB_ENABLE_CGATE, 0x111);
	}

	if (hdev->rotator_mask & BIT(0))
		WREG32(mmDCORE0_ROT_QM_CGM_CFG, 0x80100080);
	if (hdev->rotator_mask & BIT(1))
		WREG32(mmDCORE1_ROT_QM_CGM_CFG, 0x80100080);

	WREG32(mmDCORE0_SRAM_BANK_0_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_1_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_2_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_3_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_4_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_5_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_6_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_7_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_8_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_9_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_10_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_11_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_12_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_13_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_14_CG_CTRL, 0x700FF);
	WREG32(mmDCORE0_SRAM_BANK_15_CG_CTRL, 0x700FF);

	WREG32(mmDCORE1_SRAM_BANK_0_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_1_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_2_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_3_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_4_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_5_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_6_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_7_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_8_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_9_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_10_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_11_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_12_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_13_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_14_CG_CTRL, 0x700FF);
	WREG32(mmDCORE1_SRAM_BANK_15_CG_CTRL, 0x700FF);

	WREG32(mmDCORE0_KDMA_CORE_CKG, 0x7);
	WREG32(mmDCORE1_KDMA_CORE_CKG, 0x7);
	WREG32(mmDCORE0_DDMA_CORE_CKG, 0x7);
	WREG32(mmDCORE1_DDMA_CORE_CKG, 0x7);
	WREG32(mmDCORE0_PDMA0_CORE_CKG, 0x7);
	WREG32(mmDCORE0_PDMA1_CORE_CKG, 0x7);
	WREG32(mmDCORE1_PDMA0_CORE_CKG, 0x7);
	WREG32(mmDCORE1_PDMA1_CORE_CKG, 0x7);

	WREG32(mmDCORE0_KDMA_CORE_KDMA_CGM_CFG, 0x80100080);
	WREG32(mmDCORE1_KDMA_CORE_KDMA_CGM_CFG, 0x80100080);
	WREG32(mmDCORE0_DDMA_CORE_KDMA_CGM_CFG, 0x80100080);
	WREG32(mmDCORE1_DDMA_CORE_KDMA_CGM_CFG, 0x80100080);
	WREG32(mmDCORE0_PDMA0_CORE_KDMA_CGM_CFG, 0x80100080);
	WREG32(mmDCORE0_PDMA1_CORE_KDMA_CGM_CFG, 0x80100080);
	WREG32(mmDCORE1_PDMA0_CORE_KDMA_CGM_CFG, 0x80100080);
	WREG32(mmDCORE1_PDMA1_CORE_KDMA_CGM_CFG, 0x80100080);

	WREG32(mmDCORE0_DDMA_QM_CGM_CFG, 0x80100080);
	WREG32(mmDCORE1_DDMA_QM_CGM_CFG, 0x80100080);
	WREG32(mmDCORE0_PDMA0_QM_CGM_CFG, 0x80100080);
	WREG32(mmDCORE0_PDMA1_QM_CGM_CFG, 0x80100080);
	WREG32(mmDCORE1_PDMA0_QM_CGM_CFG, 0x80100080);
	WREG32(mmDCORE1_PDMA1_QM_CGM_CFG, 0x80100080);

	WREG32(mmDCORE0_HIF0_CLOCK_GATE_CONFIG, 0x5);
	WREG32(mmDCORE0_HIF1_CLOCK_GATE_CONFIG, 0x5);
	WREG32(mmDCORE1_HIF0_CLOCK_GATE_CONFIG, 0x5);
	WREG32(mmDCORE1_HIF1_CLOCK_GATE_CONFIG, 0x5);

	/* MMU_CLOCK_GATE_EN must be disabled (H6-3297, which is relevant also for H5) */
	WREG32(mmPMMU_PIF_CLOCK_GATE_CONFIG, 0x19);
	WREG32(mmDCORE0_HMMU0_SCRAMB_OUT_CLOCK_GATE_CONFIG, 0x32);
	WREG32(mmDCORE0_HMMU1_SCRAMB_OUT_CLOCK_GATE_CONFIG, 0x32);
	WREG32(mmDCORE1_HMMU0_SCRAMB_OUT_CLOCK_GATE_CONFIG, 0x32);
	WREG32(mmDCORE1_HMMU1_SCRAMB_OUT_CLOCK_GATE_CONFIG, 0x32);

	if (asic_prop->decoder_enabled_mask & 0x1)
		WREG32(mmDCORE0_VDEC0_BRDG_CTRL_CGM_DISABLE, 0);
	if (asic_prop->decoder_enabled_mask & 0x2)
		WREG32(mmDCORE0_VDEC1_BRDG_CTRL_CGM_DISABLE, 0);
	if (asic_prop->decoder_enabled_mask & 0x4)
		WREG32(mmDCORE0_VDEC2_BRDG_CTRL_CGM_DISABLE, 0);
	if (asic_prop->decoder_enabled_mask & 0x8)
		WREG32(mmDCORE0_VDEC3_BRDG_CTRL_CGM_DISABLE, 0);
	if (asic_prop->decoder_enabled_mask & 0x10)
		WREG32(mmDCORE0_VDEC4_BRDG_CTRL_CGM_DISABLE, 0);
	if (asic_prop->decoder_enabled_mask & 0x20)
		WREG32(mmDCORE1_VDEC0_BRDG_CTRL_CGM_DISABLE, 0);
	if (asic_prop->decoder_enabled_mask & 0x40)
		WREG32(mmDCORE1_VDEC1_BRDG_CTRL_CGM_DISABLE, 0);
	if (asic_prop->decoder_enabled_mask & 0x80)
		WREG32(mmDCORE1_VDEC2_BRDG_CTRL_CGM_DISABLE, 0);
	if (asic_prop->decoder_enabled_mask & 0x100)
		WREG32(mmDCORE1_VDEC3_BRDG_CTRL_CGM_DISABLE, 0);
	if (asic_prop->decoder_enabled_mask & 0x200)
		WREG32(mmDCORE1_VDEC4_BRDG_CTRL_CGM_DISABLE, 0);

	if (encoder_enabled)
		WREG32(mmVENC_VL2C_CTRL_CGM_DISABLE, 0);

	WREG32(mmDCON0_RTR_IF_CG_CTRL, 0x3);
	WREG32(mmDCON1_RTR_IF_CG_CTRL, 0x3);
	WREG32(mmDCON2_RTR_IF_CG_CTRL, 0x3);
	WREG32(mmDCON3_RTR_IF_CG_CTRL, 0x3);

	WREG32(mmDCORE0_TPCIF_RTR0_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE0_TPCIF_RTR0_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE0_TPCIF_RTR1_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE0_TPCIF_RTR1_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE0_TPCIF_RTR2_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE0_TPCIF_RTR2_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE0_TPCIF_RTR3_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE0_TPCIF_RTR3_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE1_TPCIF_RTR0_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE1_TPCIF_RTR0_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE1_TPCIF_RTR1_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE1_TPCIF_RTR1_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE1_TPCIF_RTR2_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE1_TPCIF_RTR2_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE1_TPCIF_RTR3_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE1_TPCIF_RTR3_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);

	WREG32(mmDCORE0_MMEIF_RTR0_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE0_MMEIF_RTR0_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE0_MMEIF_RTR1_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE0_MMEIF_RTR1_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE0_MMEIF_RTR2_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE0_MMEIF_RTR2_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE0_MMEIF_RTR3_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE0_MMEIF_RTR3_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE1_MMEIF_RTR0_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE1_MMEIF_RTR0_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE1_MMEIF_RTR1_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE1_MMEIF_RTR1_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE1_MMEIF_RTR2_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE1_MMEIF_RTR2_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);
	WREG32(mmDCORE1_MMEIF_RTR3_PWR_CGATE_ENABLE_WR, 0xFFFFFFFF);
	WREG32(mmDCORE1_MMEIF_RTR3_PWR_CGATE_ENABLE_RD, 0xFFFFFFFF);

	greco->hw_cap_initialized |= HW_CAP_CLK_GATE;
}
