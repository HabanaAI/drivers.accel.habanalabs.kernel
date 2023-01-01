// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

/************************************
 ** This is an auto-generated file **
 **       DO NOT EDIT BELOW        **
 ************************************/

#include "gaudi3P.h"
#include "../include/gaudi3/asic_reg/gaudi3_regs.h"

#define N2R_CREDITS_VERSION		"0.5"
#define R2C_CREDITS_VERSION		"0.5"
#define SCRAMBLING_VERSION		"0.10"
#define REGULATORS_VERSION		"0.9"
#define QOS_VERSION		"0.1"
#define CACHE_VERSION		"0.2"
#define MC_VERSION		"0.2"
#define SOL_CONFIG_VERSION		"1.6"

static void gaudi3_init_n2r_credits_d2d(struct hl_device *hdev)
{
	WREG32(0xC000044, 0x15);
	WREG32(0xC00D010, 0x0808);
	WREG32(0xC00D014, 0x0808);
	WREG32(0xC00D030, 0x1010);
	WREG32(0xC00D034, 0x1018);
	WREG32(0xC00D038, 0x1010);
	WREG32(0xC00D03C, 0x1018);
	WREG32(0xC00D040, 0x3236);
	WREG32(0xC00D044, 0x3236);
	WREG32(0xC00D048, 0x3236);
	WREG32(0xC00D04C, 0x3236);
	WREG32(0xC00D110, 0x2008);
	WREG32(0xC00D114, 0x0808);
	WREG32(0xC00D130, 0x0C08);
	WREG32(0xC00D134, 0x080C);
	WREG32(0xC00D138, 0x0808);
	WREG32(0xC00D13C, 0x080C);
	WREG32(0xC00D140, 0x1018);
	WREG32(0xC00D144, 0x1018);
	WREG32(0xC00D148, 0x1018);
	WREG32(0xC00D14C, 0x1018);
	WREG32(0xC00D210, 0x2C10);
	WREG32(0xC00D214, 0x0808);
	WREG32(0xC00D230, 0x1410);
	WREG32(0xC00D234, 0x1410);
	WREG32(0xC00D238, 0x1410);
	WREG32(0xC00D23C, 0x1410);
	WREG32(0xC00D240, 0x1410);
	WREG32(0xC00D244, 0x1410);
	WREG32(0xC00D248, 0x1410);
	WREG32(0xC00D24C, 0x1410);
	WREG32(0xC00D310, 0x4010);
	WREG32(0xC00D314, 0x0808);
	WREG32(0xC00D330, 0x0C08);
	WREG32(0xC00D334, 0x0C08);
	WREG32(0xC00D338, 0x0C0C);
	WREG32(0xC00D33C, 0x0C0C);
	WREG32(0xC00D340, 0x1418);
	WREG32(0xC00D344, 0x1418);
	WREG32(0xC00D348, 0x1418);
	WREG32(0xC00D34C, 0x1418);
	WREG32(0xC00D410, 0x1010);
	WREG32(0xC00D414, 0x0808);
	WREG32(0xC00D510, 0x0E0E);
	WREG32(0xC00D514, 0x0E0E);
	WREG32(0xC00D518, 0x0E0E);
	WREG32(0xC00D51C, 0x0E0E);
	WREG32(0xC00D520, 0x0E0E);
	WREG32(0xC00D524, 0x0E0E);
	WREG32(0xC00D528, 0x0E0E);
	WREG32(0xC00D52C, 0x0E0E);
	WREG32(0xC020044, 0x15);
	WREG32(0xC027010, 0x0808);
	WREG32(0xC027014, 0x0808);
	WREG32(0xC027030, 0x1010);
	WREG32(0xC027034, 0x1018);
	WREG32(0xC027038, 0x1010);
	WREG32(0xC02703C, 0x1018);
	WREG32(0xC027040, 0x3236);
	WREG32(0xC027044, 0x3236);
	WREG32(0xC027048, 0x3236);
	WREG32(0xC02704C, 0x3236);
	WREG32(0xC027110, 0x0808);
	WREG32(0xC027114, 0x0808);
	WREG32(0xC027130, 0x1010);
	WREG32(0xC027134, 0x1018);
	WREG32(0xC027138, 0x1010);
	WREG32(0xC02713C, 0x1018);
	WREG32(0xC027140, 0x3236);
	WREG32(0xC027144, 0x3236);
	WREG32(0xC027148, 0x3236);
	WREG32(0xC02714C, 0x3236);
	WREG32(0xC027210, 0x2C10);
	WREG32(0xC027214, 0x1010);
	WREG32(0xC027230, 0x1410);
	WREG32(0xC027234, 0x1410);
	WREG32(0xC027238, 0x1410);
	WREG32(0xC02723C, 0x1410);
	WREG32(0xC027240, 0x1410);
	WREG32(0xC027244, 0x1410);
	WREG32(0xC027248, 0x1410);
	WREG32(0xC02724C, 0x1410);
	WREG32(0xC027410, 0x1010);
	WREG32(0xC027414, 0x0808);
	WREG32(0xC027510, 0x0E0E);
	WREG32(0xC027514, 0x0E0E);
	WREG32(0xC027518, 0x0E0E);
	WREG32(0xC02751C, 0x0E0E);
	WREG32(0xC027520, 0x0E0E);
	WREG32(0xC027524, 0x0E0E);
	WREG32(0xC027528, 0x0E0E);
	WREG32(0xC02752C, 0x0E0E);
	WREG32(0xC800044, 0x15);
	WREG32(0xC80D010, 0x0808);
	WREG32(0xC80D014, 0x0808);
	WREG32(0xC80D030, 0x3236);
	WREG32(0xC80D034, 0x3236);
	WREG32(0xC80D038, 0x3236);
	WREG32(0xC80D03C, 0x3236);
	WREG32(0xC80D040, 0x1018);
	WREG32(0xC80D044, 0x1010);
	WREG32(0xC80D048, 0x1018);
	WREG32(0xC80D04C, 0x1010);
	WREG32(0xC80D110, 0x0808);
	WREG32(0xC80D114, 0x0808);
	WREG32(0xC80D130, 0x0404);
	WREG32(0xC80D134, 0x0404);
	WREG32(0xC80D138, 0x0404);
	WREG32(0xC80D13C, 0x0404);
	WREG32(0xC80D140, 0x0404);
	WREG32(0xC80D144, 0x0404);
	WREG32(0xC80D148, 0x0404);
	WREG32(0xC80D14C, 0x0404);
	WREG32(0xC80D210, 0x2C18);
	WREG32(0xC80D214, 0x0808);
	WREG32(0xC80D230, 0x1410);
	WREG32(0xC80D234, 0x1410);
	WREG32(0xC80D238, 0x1410);
	WREG32(0xC80D23C, 0x1410);
	WREG32(0xC80D240, 0x1410);
	WREG32(0xC80D244, 0x1410);
	WREG32(0xC80D248, 0x1410);
	WREG32(0xC80D24C, 0x1410);
	WREG32(0xC80D310, 0x0878);
	WREG32(0xC80D314, 0x0808);
	WREG32(0xC80D330, 0x1418);
	WREG32(0xC80D334, 0x1418);
	WREG32(0xC80D338, 0x1418);
	WREG32(0xC80D33C, 0x1418);
	WREG32(0xC80D340, 0x0C08);
	WREG32(0xC80D344, 0x0C08);
	WREG32(0xC80D348, 0x0C0C);
	WREG32(0xC80D34C, 0x0C0C);
	WREG32(0xC80D410, 0x4010);
	WREG32(0xC80D414, 0x0808);
	WREG32(0xC80D510, 0x0E0E);
	WREG32(0xC80D514, 0x0E0E);
	WREG32(0xC80D518, 0x0E0E);
	WREG32(0xC80D51C, 0x0E0E);
	WREG32(0xC80D520, 0x0E0E);
	WREG32(0xC80D524, 0x0E0E);
	WREG32(0xC80D528, 0x0E0E);
	WREG32(0xC80D52C, 0x0E0E);
	WREG32(0xC820044, 0x15);
	WREG32(0xC827010, 0x0808);
	WREG32(0xC827014, 0x0808);
	WREG32(0xC827030, 0x3236);
	WREG32(0xC827034, 0x3236);
	WREG32(0xC827038, 0x3236);
	WREG32(0xC82703C, 0x3236);
	WREG32(0xC827040, 0x1018);
	WREG32(0xC827044, 0x1010);
	WREG32(0xC827048, 0x1018);
	WREG32(0xC82704C, 0x1010);
	WREG32(0xC827110, 0x0808);
	WREG32(0xC827114, 0x0808);
	WREG32(0xC827130, 0x3236);
	WREG32(0xC827134, 0x3236);
	WREG32(0xC827138, 0x3236);
	WREG32(0xC82713C, 0x3236);
	WREG32(0xC827140, 0x1018);
	WREG32(0xC827144, 0x1010);
	WREG32(0xC827148, 0x1018);
	WREG32(0xC82714C, 0x1010);
	WREG32(0xC827210, 0x2C18);
	WREG32(0xC827214, 0x0808);
	WREG32(0xC827230, 0x1410);
	WREG32(0xC827234, 0x1410);
	WREG32(0xC827238, 0x1410);
	WREG32(0xC82723C, 0x1410);
	WREG32(0xC827240, 0x1410);
	WREG32(0xC827244, 0x1410);
	WREG32(0xC827248, 0x1410);
	WREG32(0xC82724C, 0x1410);
	WREG32(0xC827410, 0x4010);
	WREG32(0xC827414, 0x0808);
	WREG32(0xC827510, 0x0E0E);
	WREG32(0xC827514, 0x0E0E);
	WREG32(0xC827518, 0x0E0E);
	WREG32(0xC82751C, 0x0E0E);
	WREG32(0xC827520, 0x0E0E);
	WREG32(0xC827524, 0x0E0E);
	WREG32(0xC827528, 0x0E0E);
	WREG32(0xC82752C, 0x0E0E);
	WREG32(0xC040030, 0x15);
	WREG32(0xC045210, 0x2C10);
	WREG32(0xC045214, 0x0808);
	WREG32(0xC045230, 0x1410);
	WREG32(0xC045234, 0x1410);
	WREG32(0xC045238, 0x1410);
	WREG32(0xC04523C, 0x1410);
	WREG32(0xC045240, 0x1410);
	WREG32(0xC045244, 0x1410);
	WREG32(0xC045248, 0x1410);
	WREG32(0xC04524C, 0x1410);
	WREG32(0xC045410, 0x4010);
	WREG32(0xC045414, 0x0808);
	WREG32(0xC045510, 0x0E0E);
	WREG32(0xC045514, 0x0E0E);
	WREG32(0xC045518, 0x0E0E);
	WREG32(0xC04551C, 0x0E0E);
	WREG32(0xC045520, 0x0E0E);
	WREG32(0xC045524, 0x0E0E);
	WREG32(0xC045528, 0x0E0E);
	WREG32(0xC04552C, 0x0E0E);
	WREG32(0xC050030, 0x15);
	WREG32(0xC055210, 0x2C10);
	WREG32(0xC055214, 0x0808);
	WREG32(0xC055230, 0x1410);
	WREG32(0xC055234, 0x1410);
	WREG32(0xC055238, 0x1410);
	WREG32(0xC05523C, 0x1410);
	WREG32(0xC055240, 0x1410);
	WREG32(0xC055244, 0x1410);
	WREG32(0xC055248, 0x1410);
	WREG32(0xC05524C, 0x1410);
	WREG32(0xC055410, 0x4010);
	WREG32(0xC055414, 0x0808);
	WREG32(0xC055510, 0x0E0E);
	WREG32(0xC055514, 0x0E0E);
	WREG32(0xC055518, 0x0E0E);
	WREG32(0xC05551C, 0x0E0E);
	WREG32(0xC055520, 0x0E0E);
	WREG32(0xC055524, 0x0E0E);
	WREG32(0xC055528, 0x0E0E);
	WREG32(0xC05552C, 0x0E0E);
	WREG32(0xC840030, 0x15);
	WREG32(0xC845210, 0x2C18);
	WREG32(0xC845214, 0x0808);
	WREG32(0xC845230, 0x1410);
	WREG32(0xC845234, 0x1410);
	WREG32(0xC845238, 0x1410);
	WREG32(0xC84523C, 0x1410);
	WREG32(0xC845240, 0x1410);
	WREG32(0xC845244, 0x1410);
	WREG32(0xC845248, 0x1410);
	WREG32(0xC84524C, 0x1410);
	WREG32(0xC845410, 0x5010);
	WREG32(0xC845414, 0x0808);
	WREG32(0xC845510, 0x0E0E);
	WREG32(0xC845514, 0x0E0E);
	WREG32(0xC845518, 0x0E0E);
	WREG32(0xC84551C, 0x0E0E);
	WREG32(0xC845520, 0x0E0E);
	WREG32(0xC845524, 0x0E0E);
	WREG32(0xC845528, 0x0E0E);
	WREG32(0xC84552C, 0x0E0E);
	WREG32(0xC850030, 0x15);
	WREG32(0xC855210, 0x2C18);
	WREG32(0xC855214, 0x0808);
	WREG32(0xC855230, 0x1410);
	WREG32(0xC855234, 0x1410);
	WREG32(0xC855238, 0x1410);
	WREG32(0xC85523C, 0x1410);
	WREG32(0xC855240, 0x1410);
	WREG32(0xC855244, 0x1410);
	WREG32(0xC855248, 0x1410);
	WREG32(0xC85524C, 0x1410);
	WREG32(0xC855410, 0x5010);
	WREG32(0xC855414, 0x0808);
	WREG32(0xC855510, 0x0E0E);
	WREG32(0xC855514, 0x0E0E);
	WREG32(0xC855518, 0x0E0E);
	WREG32(0xC85551C, 0x0E0E);
	WREG32(0xC855520, 0x0E0E);
	WREG32(0xC855524, 0x0E0E);
	WREG32(0xC855528, 0x0E0E);
	WREG32(0xC85552C, 0x0E0E);
}

static void gaudi3_init_n2r_credits_single_die(struct hl_device *hdev)
{
	WREG32(0xC000044, 0x15);
	WREG32(0xC00D010, 0x0808);
	WREG32(0xC00D014, 0x0808);
	WREG32(0xC00D030, 0x1010);
	WREG32(0xC00D034, 0x2020);
	WREG32(0xC00D038, 0x1010);
	WREG32(0xC00D03C, 0x2020);
	WREG32(0xC00D110, 0x0808);
	WREG32(0xC00D130, 0x0C08);
	WREG32(0xC00D134, 0x2020);
	WREG32(0xC00D138, 0x1010);
	WREG32(0xC00D13C, 0x2020);
	WREG32(0xC00D210, 0x1010);
	WREG32(0xC00D230, 0x0202);
	WREG32(0xC00D310, 0x4040);
	WREG32(0xC00D330, 0x1010);
	WREG32(0xC00D334, 0x1010);
	WREG32(0xC00D338, 0x2020);
	WREG32(0xC00D33C, 0x2020);
	WREG32(0xC00D410, 0x1020);
	WREG32(0xC00D510, 0x0E0E);
	WREG32(0xC00D514, 0x0E0E);
	WREG32(0xC00D518, 0x0E0E);
	WREG32(0xC00D51C, 0x0E0E);
	WREG32(0xC00D520, 0x0E0E);
	WREG32(0xC00D524, 0x0E0E);
	WREG32(0xC00D528, 0x0E0E);
	WREG32(0xC00D52C, 0x0E0E);
	WREG32(0xC020044, 0x15);
	WREG32(0xC027010, 0x0808);
	WREG32(0xC027030, 0x1010);
	WREG32(0xC027034, 0x2020);
	WREG32(0xC027038, 0x1010);
	WREG32(0xC02703C, 0x2020);
	WREG32(0xC027110, 0x0808);
	WREG32(0xC027130, 0x1010);
	WREG32(0xC027134, 0x2020);
	WREG32(0xC027138, 0x1010);
	WREG32(0xC02713C, 0x2020);
	WREG32(0xC027210, 0x0808);
	WREG32(0xC027230, 0x0202);
	WREG32(0xC027234, 0x0202);
	WREG32(0xC027238, 0x0202);
	WREG32(0xC02723C, 0x0202);
	WREG32(0xC027410, 0x1020);
	WREG32(0xC027510, 0x0A0A);
	WREG32(0xC027514, 0x0E0E);
	WREG32(0xC027518, 0x0E0E);
	WREG32(0xC02751C, 0x0E0E);
	WREG32(0xC027520, 0x0E0E);
	WREG32(0xC027524, 0x0E0E);
	WREG32(0xC027528, 0x0E0E);
	WREG32(0xC02752C, 0x0E0E);
	WREG32(0xC040030, 0x15);
	WREG32(0xC045210, 0x1010);
	WREG32(0xC045230, 0x0202);
	WREG32(0xC045234, 0x0202);
	WREG32(0xC045238, 0x0202);
	WREG32(0xC04523C, 0x0202);
	WREG32(0xC045410, 0x1010);
	WREG32(0xC045510, 0x0E0E);
	WREG32(0xC045514, 0x0E0E);
	WREG32(0xC045518, 0x0E0E);
	WREG32(0xC04551C, 0x0E0E);
	WREG32(0xC045520, 0x0E0E);
	WREG32(0xC045524, 0x0E0E);
	WREG32(0xC045528, 0x0E0E);
	WREG32(0xC04552C, 0x0E0E);
	WREG32(0xC050030, 0x15);
	WREG32(0xC055210, 0x1010);
	WREG32(0xC055230, 0x0202);
	WREG32(0xC055234, 0x0202);
	WREG32(0xC055238, 0x0202);
	WREG32(0xC05523C, 0x0202);
	WREG32(0xC055410, 0x1010);
	WREG32(0xC055510, 0x0E0E);
	WREG32(0xC055514, 0x0E0E);
	WREG32(0xC055518, 0x0E0E);
	WREG32(0xC05551C, 0x0E0E);
	WREG32(0xC055520, 0x0E0E);
	WREG32(0xC055524, 0x0E0E);
	WREG32(0xC055528, 0x0E0E);
	WREG32(0xC05552C, 0x0E0E);
}

static void gaudi3_init_r2c_credits_single_die_rif_edma_hd1_hd4_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x2802);
	WREG32(offset + 0x420, 0x0608);
	WREG32(offset + 0x424, 0x080A);
	WREG32(offset + 0x428, 0x080C);
	WREG32(offset + 0x42C, 0x0A0E);
	WREG32(offset + 0x430, 0x0A0E);
	WREG32(offset + 0x434, 0x0A0E);
	WREG32(offset + 0x438, 0x0A0E);
	WREG32(offset + 0x43C, 0x0A0E);
}

static void gaudi3_init_r2c_credits_d2d_rif_edma_hd1_hd4_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x2802);
	WREG32(offset + 0x420, 0x0608);
	WREG32(offset + 0x424, 0x080A);
	WREG32(offset + 0x428, 0x080C);
	WREG32(offset + 0x42C, 0x0A0E);
	WREG32(offset + 0x430, 0x0A0E);
	WREG32(offset + 0x434, 0x0A0E);
	WREG32(offset + 0x438, 0x0A0E);
	WREG32(offset + 0x43C, 0x0A0E);
}

static void gaudi3_init_r2c_credits_single_die_rif_edma_hd3_hd5_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x2802);
	WREG32(offset + 0x420, 0x0A0E);
	WREG32(offset + 0x424, 0x0A0E);
	WREG32(offset + 0x428, 0x0A0E);
	WREG32(offset + 0x42C, 0x0A0E);
	WREG32(offset + 0x430, 0x0A0E);
	WREG32(offset + 0x434, 0x080C);
	WREG32(offset + 0x438, 0x080A);
	WREG32(offset + 0x43C, 0x0608);
}

static void gaudi3_init_r2c_credits_d2d_rif_edma_hd3_hd5_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x2802);
	WREG32(offset + 0x420, 0x0A0E);
	WREG32(offset + 0x424, 0x0A0E);
	WREG32(offset + 0x428, 0x0A0E);
	WREG32(offset + 0x42C, 0x0A0E);
	WREG32(offset + 0x430, 0x0A0E);
	WREG32(offset + 0x434, 0x080C);
	WREG32(offset + 0x438, 0x080A);
	WREG32(offset + 0x43C, 0x0608);
}

static void gaudi3_init_r2c_credits_single_die_rif_rot_hd1_hd4_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0202);
	WREG32(offset + 0x420, 0x0404);
	WREG32(offset + 0x424, 0x0404);
	WREG32(offset + 0x428, 0x0404);
	WREG32(offset + 0x42C, 0x0404);
	WREG32(offset + 0x430, 0x0404);
	WREG32(offset + 0x434, 0x0404);
	WREG32(offset + 0x438, 0x0404);
	WREG32(offset + 0x43C, 0x0404);
}

static void gaudi3_init_r2c_credits_d2d_rif_rot_hd1_hd4_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0202);
	WREG32(offset + 0x420, 0x0404);
	WREG32(offset + 0x424, 0x0404);
	WREG32(offset + 0x428, 0x0404);
	WREG32(offset + 0x42C, 0x0404);
	WREG32(offset + 0x430, 0x0404);
	WREG32(offset + 0x434, 0x0404);
	WREG32(offset + 0x438, 0x0404);
	WREG32(offset + 0x43C, 0x0404);
}

static void gaudi3_init_r2c_credits_single_die_rif_rot_hd3_hd5_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0202);
	WREG32(offset + 0x420, 0x0404);
	WREG32(offset + 0x424, 0x0404);
	WREG32(offset + 0x428, 0x0404);
	WREG32(offset + 0x42C, 0x0404);
	WREG32(offset + 0x430, 0x0404);
	WREG32(offset + 0x434, 0x0404);
	WREG32(offset + 0x438, 0x0404);
	WREG32(offset + 0x43C, 0x0404);
}

static void gaudi3_init_r2c_credits_d2d_rif_rot_hd3_hd5_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0202);
	WREG32(offset + 0x420, 0x0404);
	WREG32(offset + 0x424, 0x0404);
	WREG32(offset + 0x428, 0x0404);
	WREG32(offset + 0x42C, 0x0404);
	WREG32(offset + 0x430, 0x0404);
	WREG32(offset + 0x434, 0x0404);
	WREG32(offset + 0x438, 0x0404);
	WREG32(offset + 0x43C, 0x0404);
}

static void gaudi3_init_r2c_credits_single_die_rif_tpc_spare_hd01_hd45_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0606);
	WREG32(offset + 0x424, 0x0806);
	WREG32(offset + 0x428, 0x0806);
	WREG32(offset + 0x42C, 0x0806);
	WREG32(offset + 0x430, 0x0A08);
	WREG32(offset + 0x434, 0x0A08);
	WREG32(offset + 0x438, 0x0A08);
	WREG32(offset + 0x43C, 0x0A08);
}

static void gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd01_hd45_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0606);
	WREG32(offset + 0x424, 0x0806);
	WREG32(offset + 0x428, 0x0806);
	WREG32(offset + 0x42C, 0x0806);
	WREG32(offset + 0x430, 0x0A08);
	WREG32(offset + 0x434, 0x0A08);
	WREG32(offset + 0x438, 0x0A08);
	WREG32(offset + 0x43C, 0x0A08);
}

static void gaudi3_init_r2c_credits_single_die_rif_tpc_spare_hd23_hd67_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0A08);
	WREG32(offset + 0x424, 0x0A08);
	WREG32(offset + 0x428, 0x0A08);
	WREG32(offset + 0x42C, 0x0A08);
	WREG32(offset + 0x430, 0x0806);
	WREG32(offset + 0x434, 0x0806);
	WREG32(offset + 0x438, 0x0806);
	WREG32(offset + 0x43C, 0x0606);
}

static void gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd23_hd67_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0A08);
	WREG32(offset + 0x424, 0x0A08);
	WREG32(offset + 0x428, 0x0A08);
	WREG32(offset + 0x42C, 0x0A08);
	WREG32(offset + 0x430, 0x0806);
	WREG32(offset + 0x434, 0x0806);
	WREG32(offset + 0x438, 0x0806);
	WREG32(offset + 0x43C, 0x0606);
}

static void gaudi3_init_r2c_credits_single_die_rif_stlb_hd0145_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0400);
	WREG32(offset + 0x420, 0x0200);
	WREG32(offset + 0x424, 0x0200);
	WREG32(offset + 0x428, 0x0200);
	WREG32(offset + 0x42C, 0x0200);
	WREG32(offset + 0x430, 0x0200);
	WREG32(offset + 0x434, 0x0200);
	WREG32(offset + 0x438, 0x0200);
	WREG32(offset + 0x43C, 0x0200);
}

static void gaudi3_init_r2c_credits_d2d_rif_stlb_hd0145_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0400);
	WREG32(offset + 0x420, 0x0200);
	WREG32(offset + 0x424, 0x0200);
	WREG32(offset + 0x428, 0x0200);
	WREG32(offset + 0x42C, 0x0200);
	WREG32(offset + 0x430, 0x0200);
	WREG32(offset + 0x434, 0x0200);
	WREG32(offset + 0x438, 0x0200);
	WREG32(offset + 0x43C, 0x0200);
}

static void gaudi3_init_r2c_credits_single_die_rif_stlb_hd2367_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0400);
	WREG32(offset + 0x420, 0x0200);
	WREG32(offset + 0x424, 0x0200);
	WREG32(offset + 0x428, 0x0200);
	WREG32(offset + 0x42C, 0x0200);
	WREG32(offset + 0x430, 0x0200);
	WREG32(offset + 0x434, 0x0200);
	WREG32(offset + 0x438, 0x0200);
	WREG32(offset + 0x43C, 0x0200);
}

static void gaudi3_init_r2c_credits_d2d_rif_stlb_hd2367_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0400);
	WREG32(offset + 0x420, 0x0200);
	WREG32(offset + 0x424, 0x0200);
	WREG32(offset + 0x428, 0x0200);
	WREG32(offset + 0x42C, 0x0200);
	WREG32(offset + 0x430, 0x0200);
	WREG32(offset + 0x434, 0x0200);
	WREG32(offset + 0x438, 0x0200);
	WREG32(offset + 0x43C, 0x0200);
}

static void gaudi3_init_r2c_credits_single_die_mme_b0_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0200);
	WREG32(offset + 0x420, 0x0500);
	WREG32(offset + 0x424, 0x0500);
	WREG32(offset + 0x428, 0x0600);
	WREG32(offset + 0x42C, 0x0700);
	WREG32(offset + 0x430, 0x0700);
	WREG32(offset + 0x434, 0x0700);
	WREG32(offset + 0x438, 0x0700);
	WREG32(offset + 0x43C, 0x0700);
}

static void gaudi3_init_r2c_credits_d2d_mme_b0_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0200);
	WREG32(offset + 0x420, 0x0500);
	WREG32(offset + 0x424, 0x0500);
	WREG32(offset + 0x428, 0x0600);
	WREG32(offset + 0x42C, 0x0700);
	WREG32(offset + 0x430, 0x0700);
	WREG32(offset + 0x434, 0x0700);
	WREG32(offset + 0x438, 0x0700);
	WREG32(offset + 0x43C, 0x0700);
}

static void gaudi3_init_r2c_credits_single_die_mme_b1_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0200);
	WREG32(offset + 0x420, 0x0500);
	WREG32(offset + 0x424, 0x0500);
	WREG32(offset + 0x428, 0x0600);
	WREG32(offset + 0x42C, 0x0700);
	WREG32(offset + 0x430, 0x0700);
	WREG32(offset + 0x434, 0x0700);
	WREG32(offset + 0x438, 0x0700);
	WREG32(offset + 0x43C, 0x0700);
}

static void gaudi3_init_r2c_credits_d2d_mme_b1_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0200);
	WREG32(offset + 0x420, 0x0500);
	WREG32(offset + 0x424, 0x0500);
	WREG32(offset + 0x428, 0x0600);
	WREG32(offset + 0x42C, 0x0700);
	WREG32(offset + 0x430, 0x0700);
	WREG32(offset + 0x434, 0x0700);
	WREG32(offset + 0x438, 0x0700);
	WREG32(offset + 0x43C, 0x0700);
}

static void gaudi3_init_r2c_credits_single_die_tpc0_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0504);
	WREG32(offset + 0x424, 0x0504);
	WREG32(offset + 0x428, 0x0504);
	WREG32(offset + 0x42C, 0x0604);
	WREG32(offset + 0x430, 0x0606);
	WREG32(offset + 0x434, 0x0606);
	WREG32(offset + 0x438, 0x0706);
	WREG32(offset + 0x43C, 0x0706);
}

static void gaudi3_init_r2c_credits_d2d_tpc0_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0504);
	WREG32(offset + 0x424, 0x0504);
	WREG32(offset + 0x428, 0x0504);
	WREG32(offset + 0x42C, 0x0604);
	WREG32(offset + 0x430, 0x0606);
	WREG32(offset + 0x434, 0x0606);
	WREG32(offset + 0x438, 0x0706);
	WREG32(offset + 0x43C, 0x0706);
}

static void gaudi3_init_r2c_credits_single_die_tpc1_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0504);
	WREG32(offset + 0x424, 0x0504);
	WREG32(offset + 0x428, 0x0504);
	WREG32(offset + 0x42C, 0x0604);
	WREG32(offset + 0x430, 0x0606);
	WREG32(offset + 0x434, 0x0606);
	WREG32(offset + 0x438, 0x0706);
	WREG32(offset + 0x43C, 0x0706);
}

static void gaudi3_init_r2c_credits_d2d_tpc1_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0504);
	WREG32(offset + 0x424, 0x0504);
	WREG32(offset + 0x428, 0x0504);
	WREG32(offset + 0x42C, 0x0604);
	WREG32(offset + 0x430, 0x0606);
	WREG32(offset + 0x434, 0x0606);
	WREG32(offset + 0x438, 0x0706);
	WREG32(offset + 0x43C, 0x0706);
}

static void gaudi3_init_r2c_credits_single_die_tpc2_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0604);
	WREG32(offset + 0x424, 0x0504);
	WREG32(offset + 0x428, 0x0504);
	WREG32(offset + 0x42C, 0x0504);
	WREG32(offset + 0x430, 0x0604);
	WREG32(offset + 0x434, 0x0606);
	WREG32(offset + 0x438, 0x0606);
	WREG32(offset + 0x43C, 0x0706);
}

static void gaudi3_init_r2c_credits_d2d_tpc2_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0604);
	WREG32(offset + 0x424, 0x0504);
	WREG32(offset + 0x428, 0x0504);
	WREG32(offset + 0x42C, 0x0504);
	WREG32(offset + 0x430, 0x0604);
	WREG32(offset + 0x434, 0x0606);
	WREG32(offset + 0x438, 0x0606);
	WREG32(offset + 0x43C, 0x0706);
}

static void gaudi3_init_r2c_credits_single_die_tpc3_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0604);
	WREG32(offset + 0x424, 0x0504);
	WREG32(offset + 0x428, 0x0504);
	WREG32(offset + 0x42C, 0x0504);
	WREG32(offset + 0x430, 0x0604);
	WREG32(offset + 0x434, 0x0606);
	WREG32(offset + 0x438, 0x0606);
	WREG32(offset + 0x43C, 0x0706);
}

static void gaudi3_init_r2c_credits_d2d_tpc3_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0604);
	WREG32(offset + 0x424, 0x0504);
	WREG32(offset + 0x428, 0x0504);
	WREG32(offset + 0x42C, 0x0504);
	WREG32(offset + 0x430, 0x0604);
	WREG32(offset + 0x434, 0x0606);
	WREG32(offset + 0x438, 0x0606);
	WREG32(offset + 0x43C, 0x0706);
}

static void gaudi3_init_r2c_credits_single_die_mme_a0_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0200);
	WREG32(offset + 0x420, 0x0600);
	WREG32(offset + 0x424, 0x0600);
	WREG32(offset + 0x428, 0x0500);
	WREG32(offset + 0x42C, 0x0500);
	WREG32(offset + 0x430, 0x0500);
	WREG32(offset + 0x434, 0x0600);
	WREG32(offset + 0x438, 0x0600);
	WREG32(offset + 0x43C, 0x0600);
}

static void gaudi3_init_r2c_credits_d2d_mme_a0_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0200);
	WREG32(offset + 0x420, 0x0600);
	WREG32(offset + 0x424, 0x0600);
	WREG32(offset + 0x428, 0x0500);
	WREG32(offset + 0x42C, 0x0500);
	WREG32(offset + 0x430, 0x0500);
	WREG32(offset + 0x434, 0x0600);
	WREG32(offset + 0x438, 0x0600);
	WREG32(offset + 0x43C, 0x0600);
}

static void gaudi3_init_r2c_credits_single_die_mme_a1_c0_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x060A);
	WREG32(offset + 0x424, 0x060A);
	WREG32(offset + 0x428, 0x0508);
	WREG32(offset + 0x42C, 0x0508);
	WREG32(offset + 0x430, 0x0508);
	WREG32(offset + 0x434, 0x060A);
	WREG32(offset + 0x438, 0x060A);
	WREG32(offset + 0x43C, 0x060A);
}

static void gaudi3_init_r2c_credits_d2d_mme_a1_c0_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x060A);
	WREG32(offset + 0x424, 0x060A);
	WREG32(offset + 0x428, 0x0508);
	WREG32(offset + 0x42C, 0x0508);
	WREG32(offset + 0x430, 0x0508);
	WREG32(offset + 0x434, 0x060A);
	WREG32(offset + 0x438, 0x060A);
	WREG32(offset + 0x43C, 0x060A);
}

static void gaudi3_init_r2c_credits_single_die_mme_a2_c1_qman_wr_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0202);
	WREG32(offset + 0x420, 0x060A);
	WREG32(offset + 0x424, 0x060A);
	WREG32(offset + 0x428, 0x060A);
	WREG32(offset + 0x42C, 0x0508);
	WREG32(offset + 0x430, 0x0508);
	WREG32(offset + 0x434, 0x0508);
	WREG32(offset + 0x438, 0x050A);
	WREG32(offset + 0x43C, 0x060A);
}

static void gaudi3_init_r2c_credits_d2d_mme_a2_c1_qman_wr_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0202);
	WREG32(offset + 0x420, 0x060A);
	WREG32(offset + 0x424, 0x060A);
	WREG32(offset + 0x428, 0x060A);
	WREG32(offset + 0x42C, 0x0508);
	WREG32(offset + 0x430, 0x0508);
	WREG32(offset + 0x434, 0x0508);
	WREG32(offset + 0x438, 0x050A);
	WREG32(offset + 0x43C, 0x060A);
}

static void gaudi3_init_r2c_credits_single_die_mme_a3_qman_rd_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0200);
	WREG32(offset + 0x420, 0x0600);
	WREG32(offset + 0x424, 0x0600);
	WREG32(offset + 0x428, 0x0600);
	WREG32(offset + 0x42C, 0x0500);
	WREG32(offset + 0x430, 0x0500);
	WREG32(offset + 0x434, 0x0500);
	WREG32(offset + 0x438, 0x0500);
	WREG32(offset + 0x43C, 0x0600);
}

static void gaudi3_init_r2c_credits_d2d_mme_a3_qman_rd_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0200);
	WREG32(offset + 0x420, 0x0600);
	WREG32(offset + 0x424, 0x0600);
	WREG32(offset + 0x428, 0x0600);
	WREG32(offset + 0x42C, 0x0500);
	WREG32(offset + 0x430, 0x0500);
	WREG32(offset + 0x434, 0x0500);
	WREG32(offset + 0x438, 0x0500);
	WREG32(offset + 0x43C, 0x0600);
}

static void gaudi3_init_r2c_credits_single_die_tpc4_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0706);
	WREG32(offset + 0x424, 0x0606);
	WREG32(offset + 0x428, 0x0606);
	WREG32(offset + 0x42C, 0x0604);
	WREG32(offset + 0x430, 0x0504);
	WREG32(offset + 0x434, 0x0504);
	WREG32(offset + 0x438, 0x0504);
	WREG32(offset + 0x43C, 0x0504);
}

static void gaudi3_init_r2c_credits_d2d_tpc4_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0706);
	WREG32(offset + 0x424, 0x0606);
	WREG32(offset + 0x428, 0x0606);
	WREG32(offset + 0x42C, 0x0604);
	WREG32(offset + 0x430, 0x0504);
	WREG32(offset + 0x434, 0x0504);
	WREG32(offset + 0x438, 0x0504);
	WREG32(offset + 0x43C, 0x0504);
}

static void gaudi3_init_r2c_credits_single_die_tpc5_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0706);
	WREG32(offset + 0x424, 0x0606);
	WREG32(offset + 0x428, 0x0606);
	WREG32(offset + 0x42C, 0x0604);
	WREG32(offset + 0x430, 0x0504);
	WREG32(offset + 0x434, 0x0504);
	WREG32(offset + 0x438, 0x0504);
	WREG32(offset + 0x43C, 0x0504);
}

static void gaudi3_init_r2c_credits_d2d_tpc5_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x0802);
	WREG32(offset + 0x420, 0x0706);
	WREG32(offset + 0x424, 0x0606);
	WREG32(offset + 0x428, 0x0606);
	WREG32(offset + 0x42C, 0x0604);
	WREG32(offset + 0x430, 0x0504);
	WREG32(offset + 0x434, 0x0504);
	WREG32(offset + 0x438, 0x0504);
	WREG32(offset + 0x43C, 0x0504);
}

static void gaudi3_init_r2c_credits_single_die_tpc6_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x2802);
	WREG32(offset + 0x420, 0x0706);
	WREG32(offset + 0x424, 0x0706);
	WREG32(offset + 0x428, 0x0706);
	WREG32(offset + 0x42C, 0x0606);
	WREG32(offset + 0x430, 0x0604);
	WREG32(offset + 0x434, 0x0504);
	WREG32(offset + 0x438, 0x0504);
	WREG32(offset + 0x43C, 0x0504);
}

static void gaudi3_init_r2c_credits_d2d_tpc6_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x2802);
	WREG32(offset + 0x420, 0x0706);
	WREG32(offset + 0x424, 0x0706);
	WREG32(offset + 0x428, 0x0706);
	WREG32(offset + 0x42C, 0x0606);
	WREG32(offset + 0x430, 0x0604);
	WREG32(offset + 0x434, 0x0504);
	WREG32(offset + 0x438, 0x0504);
	WREG32(offset + 0x43C, 0x0504);
}

static void gaudi3_init_r2c_credits_single_die_tpc7_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x2802);
	WREG32(offset + 0x420, 0x0706);
	WREG32(offset + 0x424, 0x0706);
	WREG32(offset + 0x428, 0x0706);
	WREG32(offset + 0x42C, 0x0606);
	WREG32(offset + 0x430, 0x0604);
	WREG32(offset + 0x434, 0x0504);
	WREG32(offset + 0x438, 0x0504);
	WREG32(offset + 0x43C, 0x0504);
}

static void gaudi3_init_r2c_credits_d2d_tpc7_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x2802);
	WREG32(offset + 0x420, 0x0706);
	WREG32(offset + 0x424, 0x0706);
	WREG32(offset + 0x428, 0x0706);
	WREG32(offset + 0x42C, 0x0606);
	WREG32(offset + 0x430, 0x0604);
	WREG32(offset + 0x434, 0x0504);
	WREG32(offset + 0x438, 0x0504);
	WREG32(offset + 0x43C, 0x0504);
}

static void gaudi3_init_r2c_credits_single_die_mme_b2_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x2802);
	WREG32(offset + 0x420, 0x0800);
	WREG32(offset + 0x424, 0x0800);
	WREG32(offset + 0x428, 0x0800);
	WREG32(offset + 0x42C, 0x0700);
	WREG32(offset + 0x430, 0x0600);
	WREG32(offset + 0x434, 0x0600);
	WREG32(offset + 0x438, 0x0500);
	WREG32(offset + 0x43C, 0x0500);
}

static void gaudi3_init_r2c_credits_d2d_mme_b2_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x2802);
	WREG32(offset + 0x420, 0x0800);
	WREG32(offset + 0x424, 0x0800);
	WREG32(offset + 0x428, 0x0800);
	WREG32(offset + 0x42C, 0x0700);
	WREG32(offset + 0x430, 0x0600);
	WREG32(offset + 0x434, 0x0600);
	WREG32(offset + 0x438, 0x0500);
	WREG32(offset + 0x43C, 0x0500);
}

static void gaudi3_init_r2c_credits_single_die_mme_b3_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x2802);
	WREG32(offset + 0x420, 0x0800);
	WREG32(offset + 0x424, 0x0800);
	WREG32(offset + 0x428, 0x0800);
	WREG32(offset + 0x42C, 0x0700);
	WREG32(offset + 0x430, 0x0600);
	WREG32(offset + 0x434, 0x0600);
	WREG32(offset + 0x438, 0x0500);
	WREG32(offset + 0x43C, 0x0500);
}

static void gaudi3_init_r2c_credits_d2d_mme_b3_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0x410, 0x2802);
	WREG32(offset + 0x420, 0x0800);
	WREG32(offset + 0x424, 0x0800);
	WREG32(offset + 0x428, 0x0800);
	WREG32(offset + 0x42C, 0x0700);
	WREG32(offset + 0x430, 0x0600);
	WREG32(offset + 0x434, 0x0600);
	WREG32(offset + 0x438, 0x0500);
	WREG32(offset + 0x43C, 0x0500);
}

static void gaudi3_init_r2c_credits_single_die_rif_edma_hd1_hd4(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_rif_edma_hd1_hd4_block(hdev, 0xE4A1000);
}

static void gaudi3_init_r2c_credits_d2d_rif_edma_hd1_hd4(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_rif_edma_hd1_hd4_block(hdev, 0xE4A1000);
	gaudi3_init_r2c_credits_d2d_rif_edma_hd1_hd4_block(hdev, 0xF0A1000);
}

static void gaudi3_init_r2c_credits_single_die_rif_edma_hd3_hd5(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_rif_edma_hd3_hd5_block(hdev, 0xECA1000);
}

static void gaudi3_init_r2c_credits_d2d_rif_edma_hd3_hd5(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_rif_edma_hd3_hd5_block(hdev, 0xECA1000);
	gaudi3_init_r2c_credits_d2d_rif_edma_hd3_hd5_block(hdev, 0xF8A1000);
}

static void gaudi3_init_r2c_credits_single_die_rif_rot_hd1_hd4(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_rif_rot_hd1_hd4_block(hdev, 0xE4A2000);
}

static void gaudi3_init_r2c_credits_d2d_rif_rot_hd1_hd4(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_rif_rot_hd1_hd4_block(hdev, 0xE4A2000);
	gaudi3_init_r2c_credits_d2d_rif_rot_hd1_hd4_block(hdev, 0xF0A2000);
}

static void gaudi3_init_r2c_credits_single_die_rif_rot_hd3_hd5(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_rif_rot_hd3_hd5_block(hdev, 0xECA2000);
}

static void gaudi3_init_r2c_credits_d2d_rif_rot_hd3_hd5(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_rif_rot_hd3_hd5_block(hdev, 0xECA2000);
	gaudi3_init_r2c_credits_d2d_rif_rot_hd3_hd5_block(hdev, 0xF8A2000);
}

static void gaudi3_init_r2c_credits_single_die_rif_tpc_spare_hd01_hd45(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_rif_tpc_spare_hd01_hd45_block(hdev, 0xE0A3000);
	gaudi3_init_r2c_credits_single_die_rif_tpc_spare_hd01_hd45_block(hdev, 0xE4A3000);
}

static void gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd01_hd45(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd01_hd45_block(hdev, 0xE0A3000);
	gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd01_hd45_block(hdev, 0xE4A3000);
	gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd01_hd45_block(hdev, 0xF0A3000);
	gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd01_hd45_block(hdev, 0xF4A3000);
}

static void gaudi3_init_r2c_credits_single_die_rif_tpc_spare_hd23_hd67(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_rif_tpc_spare_hd23_hd67_block(hdev, 0xE8A3000);
	gaudi3_init_r2c_credits_single_die_rif_tpc_spare_hd23_hd67_block(hdev, 0xECA3000);
}

static void gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd23_hd67(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd23_hd67_block(hdev, 0xE8A3000);
	gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd23_hd67_block(hdev, 0xECA3000);
	gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd23_hd67_block(hdev, 0xF8A3000);
	gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd23_hd67_block(hdev, 0xFCA3000);
}

static void gaudi3_init_r2c_credits_single_die_rif_stlb_hd0145(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_rif_stlb_hd0145_block(hdev, 0xE0A4000);
	gaudi3_init_r2c_credits_single_die_rif_stlb_hd0145_block(hdev, 0xE4A4000);
}

static void gaudi3_init_r2c_credits_d2d_rif_stlb_hd0145(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_rif_stlb_hd0145_block(hdev, 0xE0A4000);
	gaudi3_init_r2c_credits_d2d_rif_stlb_hd0145_block(hdev, 0xE4A4000);
	gaudi3_init_r2c_credits_d2d_rif_stlb_hd0145_block(hdev, 0xF0A4000);
	gaudi3_init_r2c_credits_d2d_rif_stlb_hd0145_block(hdev, 0xF4A4000);
}

static void gaudi3_init_r2c_credits_single_die_rif_stlb_hd2367(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_rif_stlb_hd2367_block(hdev, 0xE8A4000);
	gaudi3_init_r2c_credits_single_die_rif_stlb_hd2367_block(hdev, 0xECA4000);
}

static void gaudi3_init_r2c_credits_d2d_rif_stlb_hd2367(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_rif_stlb_hd2367_block(hdev, 0xE8A4000);
	gaudi3_init_r2c_credits_d2d_rif_stlb_hd2367_block(hdev, 0xECA4000);
	gaudi3_init_r2c_credits_d2d_rif_stlb_hd2367_block(hdev, 0xF8A4000);
	gaudi3_init_r2c_credits_d2d_rif_stlb_hd2367_block(hdev, 0xFCA4000);
}

static void gaudi3_init_r2c_credits_single_die_mme_b0(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_mme_b0_block(hdev, 0xE181000);
	gaudi3_init_r2c_credits_single_die_mme_b0_block(hdev, 0xE581000);
	gaudi3_init_r2c_credits_single_die_mme_b0_block(hdev, 0xE9F1000);
	gaudi3_init_r2c_credits_single_die_mme_b0_block(hdev, 0xEDF1000);
}

static void gaudi3_init_r2c_credits_d2d_mme_b0(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_mme_b0_block(hdev, 0xE181000);
	gaudi3_init_r2c_credits_d2d_mme_b0_block(hdev, 0xE581000);
	gaudi3_init_r2c_credits_d2d_mme_b0_block(hdev, 0xE9F1000);
	gaudi3_init_r2c_credits_d2d_mme_b0_block(hdev, 0xEDF1000);
	gaudi3_init_r2c_credits_d2d_mme_b0_block(hdev, 0xF181000);
	gaudi3_init_r2c_credits_d2d_mme_b0_block(hdev, 0xF581000);
	gaudi3_init_r2c_credits_d2d_mme_b0_block(hdev, 0xF9F1000);
	gaudi3_init_r2c_credits_d2d_mme_b0_block(hdev, 0xFDF1000);
}

static void gaudi3_init_r2c_credits_single_die_mme_b1(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_mme_b1_block(hdev, 0xE182000);
	gaudi3_init_r2c_credits_single_die_mme_b1_block(hdev, 0xE582000);
	gaudi3_init_r2c_credits_single_die_mme_b1_block(hdev, 0xE9F2000);
	gaudi3_init_r2c_credits_single_die_mme_b1_block(hdev, 0xEDF2000);
}

static void gaudi3_init_r2c_credits_d2d_mme_b1(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_mme_b1_block(hdev, 0xE182000);
	gaudi3_init_r2c_credits_d2d_mme_b1_block(hdev, 0xE582000);
	gaudi3_init_r2c_credits_d2d_mme_b1_block(hdev, 0xE9F2000);
	gaudi3_init_r2c_credits_d2d_mme_b1_block(hdev, 0xEDF2000);
	gaudi3_init_r2c_credits_d2d_mme_b1_block(hdev, 0xF182000);
	gaudi3_init_r2c_credits_d2d_mme_b1_block(hdev, 0xF582000);
	gaudi3_init_r2c_credits_d2d_mme_b1_block(hdev, 0xF9F2000);
	gaudi3_init_r2c_credits_d2d_mme_b1_block(hdev, 0xFDF2000);
}

static void gaudi3_init_r2c_credits_single_die_tpc0(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_tpc0_block(hdev, 0xE191000);
	gaudi3_init_r2c_credits_single_die_tpc0_block(hdev, 0xE591000);
	gaudi3_init_r2c_credits_single_die_tpc0_block(hdev, 0xE9E1000);
	gaudi3_init_r2c_credits_single_die_tpc0_block(hdev, 0xEDE1000);
}

static void gaudi3_init_r2c_credits_d2d_tpc0(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_tpc0_block(hdev, 0xE191000);
	gaudi3_init_r2c_credits_d2d_tpc0_block(hdev, 0xE591000);
	gaudi3_init_r2c_credits_d2d_tpc0_block(hdev, 0xE9E1000);
	gaudi3_init_r2c_credits_d2d_tpc0_block(hdev, 0xEDE1000);
	gaudi3_init_r2c_credits_d2d_tpc0_block(hdev, 0xF191000);
	gaudi3_init_r2c_credits_d2d_tpc0_block(hdev, 0xF591000);
	gaudi3_init_r2c_credits_d2d_tpc0_block(hdev, 0xF9E1000);
	gaudi3_init_r2c_credits_d2d_tpc0_block(hdev, 0xFDE1000);
}

static void gaudi3_init_r2c_credits_single_die_tpc1(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_tpc1_block(hdev, 0xE192000);
	gaudi3_init_r2c_credits_single_die_tpc1_block(hdev, 0xE592000);
	gaudi3_init_r2c_credits_single_die_tpc1_block(hdev, 0xE9E2000);
	gaudi3_init_r2c_credits_single_die_tpc1_block(hdev, 0xEDE2000);
}

static void gaudi3_init_r2c_credits_d2d_tpc1(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_tpc1_block(hdev, 0xE192000);
	gaudi3_init_r2c_credits_d2d_tpc1_block(hdev, 0xE592000);
	gaudi3_init_r2c_credits_d2d_tpc1_block(hdev, 0xE9E2000);
	gaudi3_init_r2c_credits_d2d_tpc1_block(hdev, 0xEDE2000);
	gaudi3_init_r2c_credits_d2d_tpc1_block(hdev, 0xF192000);
	gaudi3_init_r2c_credits_d2d_tpc1_block(hdev, 0xF592000);
	gaudi3_init_r2c_credits_d2d_tpc1_block(hdev, 0xF9E2000);
	gaudi3_init_r2c_credits_d2d_tpc1_block(hdev, 0xFDE2000);
}

static void gaudi3_init_r2c_credits_single_die_tpc2(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_tpc2_block(hdev, 0xE1A1000);
	gaudi3_init_r2c_credits_single_die_tpc2_block(hdev, 0xE5A1000);
	gaudi3_init_r2c_credits_single_die_tpc2_block(hdev, 0xE9D1000);
	gaudi3_init_r2c_credits_single_die_tpc2_block(hdev, 0xEDD1000);
}

static void gaudi3_init_r2c_credits_d2d_tpc2(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_tpc2_block(hdev, 0xE1A1000);
	gaudi3_init_r2c_credits_d2d_tpc2_block(hdev, 0xE5A1000);
	gaudi3_init_r2c_credits_d2d_tpc2_block(hdev, 0xE9D1000);
	gaudi3_init_r2c_credits_d2d_tpc2_block(hdev, 0xEDD1000);
	gaudi3_init_r2c_credits_d2d_tpc2_block(hdev, 0xF1A1000);
	gaudi3_init_r2c_credits_d2d_tpc2_block(hdev, 0xF5A1000);
	gaudi3_init_r2c_credits_d2d_tpc2_block(hdev, 0xF9D1000);
	gaudi3_init_r2c_credits_d2d_tpc2_block(hdev, 0xFDD1000);
}

static void gaudi3_init_r2c_credits_single_die_tpc3(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_tpc3_block(hdev, 0xE1A2000);
	gaudi3_init_r2c_credits_single_die_tpc3_block(hdev, 0xE5A2000);
	gaudi3_init_r2c_credits_single_die_tpc3_block(hdev, 0xE9D2000);
	gaudi3_init_r2c_credits_single_die_tpc3_block(hdev, 0xEDD2000);
}

static void gaudi3_init_r2c_credits_d2d_tpc3(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_tpc3_block(hdev, 0xE1A2000);
	gaudi3_init_r2c_credits_d2d_tpc3_block(hdev, 0xE5A2000);
	gaudi3_init_r2c_credits_d2d_tpc3_block(hdev, 0xE9D2000);
	gaudi3_init_r2c_credits_d2d_tpc3_block(hdev, 0xEDD2000);
	gaudi3_init_r2c_credits_d2d_tpc3_block(hdev, 0xF1A2000);
	gaudi3_init_r2c_credits_d2d_tpc3_block(hdev, 0xF5A2000);
	gaudi3_init_r2c_credits_d2d_tpc3_block(hdev, 0xF9D2000);
	gaudi3_init_r2c_credits_d2d_tpc3_block(hdev, 0xFDD2000);
}

static void gaudi3_init_r2c_credits_single_die_mme_a0(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_mme_a0_block(hdev, 0xE1B1000);
	gaudi3_init_r2c_credits_single_die_mme_a0_block(hdev, 0xE5B1000);
	gaudi3_init_r2c_credits_single_die_mme_a0_block(hdev, 0xE9C1000);
	gaudi3_init_r2c_credits_single_die_mme_a0_block(hdev, 0xEDC1000);
}

static void gaudi3_init_r2c_credits_d2d_mme_a0(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_mme_a0_block(hdev, 0xE1B1000);
	gaudi3_init_r2c_credits_d2d_mme_a0_block(hdev, 0xE5B1000);
	gaudi3_init_r2c_credits_d2d_mme_a0_block(hdev, 0xE9C1000);
	gaudi3_init_r2c_credits_d2d_mme_a0_block(hdev, 0xEDC1000);
	gaudi3_init_r2c_credits_d2d_mme_a0_block(hdev, 0xF1B1000);
	gaudi3_init_r2c_credits_d2d_mme_a0_block(hdev, 0xF5B1000);
	gaudi3_init_r2c_credits_d2d_mme_a0_block(hdev, 0xF9C1000);
	gaudi3_init_r2c_credits_d2d_mme_a0_block(hdev, 0xFDC1000);
}

static void gaudi3_init_r2c_credits_single_die_mme_a1_c0(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_mme_a1_c0_block(hdev, 0xE1B2000);
	gaudi3_init_r2c_credits_single_die_mme_a1_c0_block(hdev, 0xE5B2000);
	gaudi3_init_r2c_credits_single_die_mme_a1_c0_block(hdev, 0xE9C2000);
	gaudi3_init_r2c_credits_single_die_mme_a1_c0_block(hdev, 0xEDC2000);
}

static void gaudi3_init_r2c_credits_d2d_mme_a1_c0(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_mme_a1_c0_block(hdev, 0xE1B2000);
	gaudi3_init_r2c_credits_d2d_mme_a1_c0_block(hdev, 0xE5B2000);
	gaudi3_init_r2c_credits_d2d_mme_a1_c0_block(hdev, 0xE9C2000);
	gaudi3_init_r2c_credits_d2d_mme_a1_c0_block(hdev, 0xEDC2000);
	gaudi3_init_r2c_credits_d2d_mme_a1_c0_block(hdev, 0xF1B2000);
	gaudi3_init_r2c_credits_d2d_mme_a1_c0_block(hdev, 0xF5B2000);
	gaudi3_init_r2c_credits_d2d_mme_a1_c0_block(hdev, 0xF9C2000);
	gaudi3_init_r2c_credits_d2d_mme_a1_c0_block(hdev, 0xFDC2000);
}

static void gaudi3_init_r2c_credits_single_die_mme_a2_c1_qman_wr(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_mme_a2_c1_qman_wr_block(hdev, 0xE1C1000);
	gaudi3_init_r2c_credits_single_die_mme_a2_c1_qman_wr_block(hdev, 0xE5C1000);
	gaudi3_init_r2c_credits_single_die_mme_a2_c1_qman_wr_block(hdev, 0xE9B1000);
	gaudi3_init_r2c_credits_single_die_mme_a2_c1_qman_wr_block(hdev, 0xEDB1000);
}

static void gaudi3_init_r2c_credits_d2d_mme_a2_c1_qman_wr(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_mme_a2_c1_qman_wr_block(hdev, 0xE1C1000);
	gaudi3_init_r2c_credits_d2d_mme_a2_c1_qman_wr_block(hdev, 0xE5C1000);
	gaudi3_init_r2c_credits_d2d_mme_a2_c1_qman_wr_block(hdev, 0xE9B1000);
	gaudi3_init_r2c_credits_d2d_mme_a2_c1_qman_wr_block(hdev, 0xEDB1000);
	gaudi3_init_r2c_credits_d2d_mme_a2_c1_qman_wr_block(hdev, 0xF1C1000);
	gaudi3_init_r2c_credits_d2d_mme_a2_c1_qman_wr_block(hdev, 0xF5C1000);
	gaudi3_init_r2c_credits_d2d_mme_a2_c1_qman_wr_block(hdev, 0xF9B1000);
	gaudi3_init_r2c_credits_d2d_mme_a2_c1_qman_wr_block(hdev, 0xFDB1000);
}

static void gaudi3_init_r2c_credits_single_die_mme_a3_qman_rd(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_mme_a3_qman_rd_block(hdev, 0xE1C2000);
	gaudi3_init_r2c_credits_single_die_mme_a3_qman_rd_block(hdev, 0xE5C2000);
	gaudi3_init_r2c_credits_single_die_mme_a3_qman_rd_block(hdev, 0xE9B2000);
	gaudi3_init_r2c_credits_single_die_mme_a3_qman_rd_block(hdev, 0xEDB2000);
}

static void gaudi3_init_r2c_credits_d2d_mme_a3_qman_rd(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_mme_a3_qman_rd_block(hdev, 0xE1C2000);
	gaudi3_init_r2c_credits_d2d_mme_a3_qman_rd_block(hdev, 0xE5C2000);
	gaudi3_init_r2c_credits_d2d_mme_a3_qman_rd_block(hdev, 0xE9B2000);
	gaudi3_init_r2c_credits_d2d_mme_a3_qman_rd_block(hdev, 0xEDB2000);
	gaudi3_init_r2c_credits_d2d_mme_a3_qman_rd_block(hdev, 0xF1C2000);
	gaudi3_init_r2c_credits_d2d_mme_a3_qman_rd_block(hdev, 0xF5C2000);
	gaudi3_init_r2c_credits_d2d_mme_a3_qman_rd_block(hdev, 0xF9B2000);
	gaudi3_init_r2c_credits_d2d_mme_a3_qman_rd_block(hdev, 0xFDB2000);
}

static void gaudi3_init_r2c_credits_single_die_tpc4(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_tpc4_block(hdev, 0xE1D1000);
	gaudi3_init_r2c_credits_single_die_tpc4_block(hdev, 0xE5D1000);
	gaudi3_init_r2c_credits_single_die_tpc4_block(hdev, 0xE9A1000);
	gaudi3_init_r2c_credits_single_die_tpc4_block(hdev, 0xEDA1000);
}

static void gaudi3_init_r2c_credits_d2d_tpc4(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_tpc4_block(hdev, 0xE1D1000);
	gaudi3_init_r2c_credits_d2d_tpc4_block(hdev, 0xE5D1000);
	gaudi3_init_r2c_credits_d2d_tpc4_block(hdev, 0xE9A1000);
	gaudi3_init_r2c_credits_d2d_tpc4_block(hdev, 0xEDA1000);
	gaudi3_init_r2c_credits_d2d_tpc4_block(hdev, 0xF1D1000);
	gaudi3_init_r2c_credits_d2d_tpc4_block(hdev, 0xF5D1000);
	gaudi3_init_r2c_credits_d2d_tpc4_block(hdev, 0xF9A1000);
	gaudi3_init_r2c_credits_d2d_tpc4_block(hdev, 0xFDA1000);
}

static void gaudi3_init_r2c_credits_single_die_tpc5(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_tpc5_block(hdev, 0xE1D2000);
	gaudi3_init_r2c_credits_single_die_tpc5_block(hdev, 0xE5D2000);
	gaudi3_init_r2c_credits_single_die_tpc5_block(hdev, 0xE9A2000);
	gaudi3_init_r2c_credits_single_die_tpc5_block(hdev, 0xEDA2000);
}

static void gaudi3_init_r2c_credits_d2d_tpc5(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_tpc5_block(hdev, 0xE1D2000);
	gaudi3_init_r2c_credits_d2d_tpc5_block(hdev, 0xE5D2000);
	gaudi3_init_r2c_credits_d2d_tpc5_block(hdev, 0xE9A2000);
	gaudi3_init_r2c_credits_d2d_tpc5_block(hdev, 0xEDA2000);
	gaudi3_init_r2c_credits_d2d_tpc5_block(hdev, 0xF1D2000);
	gaudi3_init_r2c_credits_d2d_tpc5_block(hdev, 0xF5D2000);
	gaudi3_init_r2c_credits_d2d_tpc5_block(hdev, 0xF9A2000);
	gaudi3_init_r2c_credits_d2d_tpc5_block(hdev, 0xFDA2000);
}

static void gaudi3_init_r2c_credits_single_die_tpc6(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_tpc6_block(hdev, 0xE1E1000);
	gaudi3_init_r2c_credits_single_die_tpc6_block(hdev, 0xE5E1000);
	gaudi3_init_r2c_credits_single_die_tpc6_block(hdev, 0xE991000);
	gaudi3_init_r2c_credits_single_die_tpc6_block(hdev, 0xED91000);
}

static void gaudi3_init_r2c_credits_d2d_tpc6(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_tpc6_block(hdev, 0xE1E1000);
	gaudi3_init_r2c_credits_d2d_tpc6_block(hdev, 0xE5E1000);
	gaudi3_init_r2c_credits_d2d_tpc6_block(hdev, 0xE991000);
	gaudi3_init_r2c_credits_d2d_tpc6_block(hdev, 0xED91000);
	gaudi3_init_r2c_credits_d2d_tpc6_block(hdev, 0xF1E1000);
	gaudi3_init_r2c_credits_d2d_tpc6_block(hdev, 0xF5E1000);
	gaudi3_init_r2c_credits_d2d_tpc6_block(hdev, 0xF991000);
	gaudi3_init_r2c_credits_d2d_tpc6_block(hdev, 0xFD91000);
}

static void gaudi3_init_r2c_credits_single_die_tpc7(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_tpc7_block(hdev, 0xE1E2000);
	gaudi3_init_r2c_credits_single_die_tpc7_block(hdev, 0xE5E2000);
	gaudi3_init_r2c_credits_single_die_tpc7_block(hdev, 0xE992000);
	gaudi3_init_r2c_credits_single_die_tpc7_block(hdev, 0xED92000);
}

static void gaudi3_init_r2c_credits_d2d_tpc7(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_tpc7_block(hdev, 0xE1E2000);
	gaudi3_init_r2c_credits_d2d_tpc7_block(hdev, 0xE5E2000);
	gaudi3_init_r2c_credits_d2d_tpc7_block(hdev, 0xE992000);
	gaudi3_init_r2c_credits_d2d_tpc7_block(hdev, 0xED92000);
	gaudi3_init_r2c_credits_d2d_tpc7_block(hdev, 0xF1E2000);
	gaudi3_init_r2c_credits_d2d_tpc7_block(hdev, 0xF5E2000);
	gaudi3_init_r2c_credits_d2d_tpc7_block(hdev, 0xF992000);
	gaudi3_init_r2c_credits_d2d_tpc7_block(hdev, 0xFD92000);
}

static void gaudi3_init_r2c_credits_single_die_mme_b2(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_mme_b2_block(hdev, 0xE1F1000);
	gaudi3_init_r2c_credits_single_die_mme_b2_block(hdev, 0xE5F1000);
	gaudi3_init_r2c_credits_single_die_mme_b2_block(hdev, 0xE981000);
	gaudi3_init_r2c_credits_single_die_mme_b2_block(hdev, 0xED81000);
}

static void gaudi3_init_r2c_credits_d2d_mme_b2(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_mme_b2_block(hdev, 0xE1F1000);
	gaudi3_init_r2c_credits_d2d_mme_b2_block(hdev, 0xE5F1000);
	gaudi3_init_r2c_credits_d2d_mme_b2_block(hdev, 0xE981000);
	gaudi3_init_r2c_credits_d2d_mme_b2_block(hdev, 0xED81000);
	gaudi3_init_r2c_credits_d2d_mme_b2_block(hdev, 0xF1F1000);
	gaudi3_init_r2c_credits_d2d_mme_b2_block(hdev, 0xF5F1000);
	gaudi3_init_r2c_credits_d2d_mme_b2_block(hdev, 0xF981000);
	gaudi3_init_r2c_credits_d2d_mme_b2_block(hdev, 0xFD81000);
}

static void gaudi3_init_r2c_credits_single_die_mme_b3(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_mme_b3_block(hdev, 0xE1F2000);
	gaudi3_init_r2c_credits_single_die_mme_b3_block(hdev, 0xE5F2000);
	gaudi3_init_r2c_credits_single_die_mme_b3_block(hdev, 0xE982000);
	gaudi3_init_r2c_credits_single_die_mme_b3_block(hdev, 0xED82000);
}

static void gaudi3_init_r2c_credits_d2d_mme_b3(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_mme_b3_block(hdev, 0xE1F2000);
	gaudi3_init_r2c_credits_d2d_mme_b3_block(hdev, 0xE5F2000);
	gaudi3_init_r2c_credits_d2d_mme_b3_block(hdev, 0xE982000);
	gaudi3_init_r2c_credits_d2d_mme_b3_block(hdev, 0xED82000);
	gaudi3_init_r2c_credits_d2d_mme_b3_block(hdev, 0xF1F2000);
	gaudi3_init_r2c_credits_d2d_mme_b3_block(hdev, 0xF5F2000);
	gaudi3_init_r2c_credits_d2d_mme_b3_block(hdev, 0xF982000);
	gaudi3_init_r2c_credits_d2d_mme_b3_block(hdev, 0xFD82000);
}

static void gaudi3_init_r2c_credits_single_die(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_single_die_rif_edma_hd1_hd4(hdev);
	gaudi3_init_r2c_credits_single_die_rif_edma_hd3_hd5(hdev);
	gaudi3_init_r2c_credits_single_die_rif_rot_hd1_hd4(hdev);
	gaudi3_init_r2c_credits_single_die_rif_rot_hd3_hd5(hdev);
	gaudi3_init_r2c_credits_single_die_rif_tpc_spare_hd01_hd45(hdev);
	gaudi3_init_r2c_credits_single_die_rif_tpc_spare_hd23_hd67(hdev);
	gaudi3_init_r2c_credits_single_die_rif_stlb_hd0145(hdev);
	gaudi3_init_r2c_credits_single_die_rif_stlb_hd2367(hdev);
	gaudi3_init_r2c_credits_single_die_mme_b0(hdev);
	gaudi3_init_r2c_credits_single_die_mme_b1(hdev);
	gaudi3_init_r2c_credits_single_die_tpc0(hdev);
	gaudi3_init_r2c_credits_single_die_tpc1(hdev);
	gaudi3_init_r2c_credits_single_die_tpc2(hdev);
	gaudi3_init_r2c_credits_single_die_tpc3(hdev);
	gaudi3_init_r2c_credits_single_die_mme_a0(hdev);
	gaudi3_init_r2c_credits_single_die_mme_a1_c0(hdev);
	gaudi3_init_r2c_credits_single_die_mme_a2_c1_qman_wr(hdev);
	gaudi3_init_r2c_credits_single_die_mme_a3_qman_rd(hdev);
	gaudi3_init_r2c_credits_single_die_tpc4(hdev);
	gaudi3_init_r2c_credits_single_die_tpc5(hdev);
	gaudi3_init_r2c_credits_single_die_tpc6(hdev);
	gaudi3_init_r2c_credits_single_die_tpc7(hdev);
	gaudi3_init_r2c_credits_single_die_mme_b2(hdev);
	gaudi3_init_r2c_credits_single_die_mme_b3(hdev);
}

static void gaudi3_init_r2c_credits_d2d(struct hl_device *hdev)
{
	gaudi3_init_r2c_credits_d2d_rif_edma_hd1_hd4(hdev);
	gaudi3_init_r2c_credits_d2d_rif_edma_hd3_hd5(hdev);
	gaudi3_init_r2c_credits_d2d_rif_rot_hd1_hd4(hdev);
	gaudi3_init_r2c_credits_d2d_rif_rot_hd3_hd5(hdev);
	gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd01_hd45(hdev);
	gaudi3_init_r2c_credits_d2d_rif_tpc_spare_hd23_hd67(hdev);
	gaudi3_init_r2c_credits_d2d_rif_stlb_hd0145(hdev);
	gaudi3_init_r2c_credits_d2d_rif_stlb_hd2367(hdev);
	gaudi3_init_r2c_credits_d2d_mme_b0(hdev);
	gaudi3_init_r2c_credits_d2d_mme_b1(hdev);
	gaudi3_init_r2c_credits_d2d_tpc0(hdev);
	gaudi3_init_r2c_credits_d2d_tpc1(hdev);
	gaudi3_init_r2c_credits_d2d_tpc2(hdev);
	gaudi3_init_r2c_credits_d2d_tpc3(hdev);
	gaudi3_init_r2c_credits_d2d_mme_a0(hdev);
	gaudi3_init_r2c_credits_d2d_mme_a1_c0(hdev);
	gaudi3_init_r2c_credits_d2d_mme_a2_c1_qman_wr(hdev);
	gaudi3_init_r2c_credits_d2d_mme_a3_qman_rd(hdev);
	gaudi3_init_r2c_credits_d2d_tpc4(hdev);
	gaudi3_init_r2c_credits_d2d_tpc5(hdev);
	gaudi3_init_r2c_credits_d2d_tpc6(hdev);
	gaudi3_init_r2c_credits_d2d_tpc7(hdev);
	gaudi3_init_r2c_credits_d2d_mme_b2(hdev);
	gaudi3_init_r2c_credits_d2d_mme_b3(hdev);
}

static void gaudi3_init_scrambling_single_die_rtr_cntrl_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x000, 0x4);
	WREG32(offset + 0x010, 0x1);
	WREG32(offset + 0x014, 0x11E);
	WREG32(offset + 0x018, 0xF91);
	WREG32(offset + 0x01C, 0x34F);
	WREG32(offset + 0x020, 0x576);
	WREG32(offset + 0x024, 0xBBD);
	WREG32(offset + 0x028, 0x148);
	WREG32(offset + 0x02C, 0xD35);
	WREG32(offset + 0x030, 0x97A);
	WREG32(offset + 0x034, 0x987);
	WREG32(offset + 0x038, 0x5F5);
	WREG32(offset + 0x03C, 0x685);
	WREG32(offset + 0x040, 0xC25);
	WREG32(offset + 0x050, 0x1);
	WREG32(offset + 0x054, 0x1CC);
	WREG32(offset + 0x058, 0x892);
	WREG32(offset + 0x05C, 0x939);
	WREG32(offset + 0x060, 0x433);
	WREG32(offset + 0x064, 0x29F);
	WREG32(offset + 0x068, 0x383);
	WREG32(offset + 0x06C, 0x7E3);
	WREG32(offset + 0x070, 0x786);
	WREG32(offset + 0x074, 0x4);
	WREG32(offset + 0x078, 0x650);
	WREG32(offset + 0x07C, 0x389);
	WREG32(offset + 0x080, 0x868);
	WREG32(offset + 0x0D0, 0x643201);
	WREG32(offset + 0x0D4, 0xBA9587);
	WREG32(offset + 0x0D8, 0x51);
	WREG32(offset + 0xF60, 0x0000001F);
}

static void gaudi3_init_scrambling_d2d_8_hbm_rtr_cntrl_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x000, 0x8);
	WREG32(offset + 0x010, 0x1);
	WREG32(offset + 0x014, 0x11E);
	WREG32(offset + 0x018, 0xF91);
	WREG32(offset + 0x01C, 0x34F);
	WREG32(offset + 0x020, 0x576);
	WREG32(offset + 0x024, 0xBBD);
	WREG32(offset + 0x028, 0x148);
	WREG32(offset + 0x02C, 0xD35);
	WREG32(offset + 0x030, 0x97A);
	WREG32(offset + 0x034, 0x987);
	WREG32(offset + 0x038, 0x5F5);
	WREG32(offset + 0x03C, 0x685);
	WREG32(offset + 0x040, 0xC25);
	WREG32(offset + 0x050, 0x1);
	WREG32(offset + 0x054, 0x1CC);
	WREG32(offset + 0x058, 0x892);
	WREG32(offset + 0x05C, 0x939);
	WREG32(offset + 0x060, 0x433);
	WREG32(offset + 0x064, 0x29F);
	WREG32(offset + 0x068, 0x383);
	WREG32(offset + 0x06C, 0x7E3);
	WREG32(offset + 0x070, 0x786);
	WREG32(offset + 0x074, 0x4);
	WREG32(offset + 0x078, 0x650);
	WREG32(offset + 0x07C, 0x389);
	WREG32(offset + 0x080, 0x868);
	WREG32(offset + 0x090, 0x1);
	WREG32(offset + 0x094, 0xA40);
	WREG32(offset + 0x098, 0xCD4);
	WREG32(offset + 0x09C, 0x981);
	WREG32(offset + 0x0A0, 0x864);
	WREG32(offset + 0x0A4, 0x616);
	WREG32(offset + 0x0A8, 0x6BA);
	WREG32(offset + 0x0AC, 0x3F3);
	WREG32(offset + 0x0B0, 0x798);
	WREG32(offset + 0x0B4, 0xE7B);
	WREG32(offset + 0x0B8, 0x2EB);
	WREG32(offset + 0x0BC, 0xD71);
	WREG32(offset + 0x0C0, 0xF51);
	WREG32(offset + 0x0D0, 0x643201);
	WREG32(offset + 0x0D4, 0xBA9587);
	WREG32(offset + 0x0D8, 0x51);
	WREG32(offset + 0xF60, 0x0000001F);
}

static void gaudi3_init_scrambling_d2d_7_hbm_rtr_cntrl_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x000, 0x7);
	WREG32(offset + 0x010, 0x1);
	WREG32(offset + 0x014, 0x11E);
	WREG32(offset + 0x018, 0xF91);
	WREG32(offset + 0x01C, 0x34F);
	WREG32(offset + 0x020, 0x576);
	WREG32(offset + 0x024, 0xBBD);
	WREG32(offset + 0x028, 0x148);
	WREG32(offset + 0x02C, 0xD35);
	WREG32(offset + 0x030, 0x97A);
	WREG32(offset + 0x034, 0x987);
	WREG32(offset + 0x038, 0x5F5);
	WREG32(offset + 0x03C, 0x685);
	WREG32(offset + 0x040, 0xC25);
	WREG32(offset + 0x050, 0x1);
	WREG32(offset + 0x054, 0x5FD);
	WREG32(offset + 0x058, 0xA25);
	WREG32(offset + 0x05C, 0xB56);
	WREG32(offset + 0x060, 0xFE5);
	WREG32(offset + 0x064, 0xD8D);
	WREG32(offset + 0x068, 0xDC4);
	WREG32(offset + 0x06C, 0xFF8);
	WREG32(offset + 0x070, 0xE3D);
	WREG32(offset + 0x074, 0x7E8);
	WREG32(offset + 0x078, 0x6E9);
	WREG32(offset + 0x07C, 0x9E0);
	WREG32(offset + 0x080, 0x15F);
	WREG32(offset + 0x090, 0x1);
	WREG32(offset + 0x094, 0x080);
	WREG32(offset + 0x098, 0x0F7);
	WREG32(offset + 0x09C, 0x3FA);
	WREG32(offset + 0x0A0, 0x5DF);
	WREG32(offset + 0x0A4, 0x643);
	WREG32(offset + 0x0A8, 0x9D1);
	WREG32(offset + 0x0AC, 0x751);
	WREG32(offset + 0x0B0, 0xCB0);
	WREG32(offset + 0x0B4, 0x0D4);
	WREG32(offset + 0x0B8, 0x17B);
	WREG32(offset + 0x0BC, 0x813);
	WREG32(offset + 0x0C0, 0x51B);
	WREG32(offset + 0x0D0, 0x543201);
	WREG32(offset + 0x0D4, 0xBA9687);
	WREG32(offset + 0x0D8, 0x61);
	WREG32(offset + 0xF60, 0x0000001F);
}

static void gaudi3_init_scrambling_single_die_dtlb_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x07C, 0x1);
	WREG32(offset + 0x080, 0xF60);
	WREG32(offset + 0x084, 0x5CD);
	WREG32(offset + 0x088, 0x2CC);
	WREG32(offset + 0x08C, 0x9A0);
	WREG32(offset + 0x090, 0x124);
	WREG32(offset + 0x094, 0x8F0);
	WREG32(offset + 0x098, 0x716);
	WREG32(offset + 0x09C, 0x754);
	WREG32(offset + 0x0A0, 0x407);
	WREG32(offset + 0x0A4, 0xC26);
	WREG32(offset + 0x0A8, 0x2FC);
	WREG32(offset + 0x0AC, 0x149);
	WREG32(offset + 0xF64, 0xFFFFFFFF);
}

static void gaudi3_init_scrambling_d2d_8_hbm_dtlb_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x07C, 0x1);
	WREG32(offset + 0x080, 0xF60);
	WREG32(offset + 0x084, 0x5CD);
	WREG32(offset + 0x088, 0x2CC);
	WREG32(offset + 0x08C, 0x9A0);
	WREG32(offset + 0x090, 0x124);
	WREG32(offset + 0x094, 0x8F0);
	WREG32(offset + 0x098, 0x716);
	WREG32(offset + 0x09C, 0x754);
	WREG32(offset + 0x0A0, 0x407);
	WREG32(offset + 0x0A4, 0xC26);
	WREG32(offset + 0x0A8, 0x2FC);
	WREG32(offset + 0x0AC, 0x149);
	WREG32(offset + 0xF64, 0xFFFFFFFF);
}

static void gaudi3_init_scrambling_d2d_7_hbm_dtlb_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x07C, 0x1);
	WREG32(offset + 0x080, 0x235);
	WREG32(offset + 0x084, 0x382);
	WREG32(offset + 0x088, 0x1ED);
	WREG32(offset + 0x08C, 0xA3B);
	WREG32(offset + 0x090, 0x529);
	WREG32(offset + 0x094, 0xB5E);
	WREG32(offset + 0x098, 0x345);
	WREG32(offset + 0x09C, 0xAFE);
	WREG32(offset + 0x0A0, 0xD12);
	WREG32(offset + 0x0A4, 0xDAB);
	WREG32(offset + 0x0A8, 0x7F2);
	WREG32(offset + 0x0AC, 0xEC6);
	WREG32(offset + 0xF64, 0xFFFFFFDD);
}

static void gaudi3_init_scrambling_single_die_cache_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x100, 0x1);
	WREG32(offset + 0x110, 0x800);
	WREG32(offset + 0x114, 0xF73);
	WREG32(offset + 0x118, 0xFA5);
	WREG32(offset + 0x11C, 0xDF6);
	WREG32(offset + 0x120, 0x439);
	WREG32(offset + 0x124, 0xD17);
	WREG32(offset + 0x128, 0x51C);
	WREG32(offset + 0x12C, 0xB00);
	WREG32(offset + 0x130, 0xD41);
	WREG32(offset + 0x134, 0x7B8);
	WREG32(offset + 0x138, 0x135);
	WREG32(offset + 0x13C, 0x1B0);
	WREG32(offset + 0x400, 0x21);
	WREG32(offset + 0x404, 0x8);
	WREG32(offset + 0x408, 0x36);
	WREG32(offset + 0x40C, 0x16);
	WREG32(offset + 0x410, 0x2E);
	WREG32(offset + 0x414, 0x3C);
}

static void gaudi3_init_scrambling_d2d_8_hbm_cache_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x100, 0x1);
	WREG32(offset + 0x110, 0x800);
	WREG32(offset + 0x114, 0xF73);
	WREG32(offset + 0x118, 0xFA5);
	WREG32(offset + 0x11C, 0xDF6);
	WREG32(offset + 0x120, 0x439);
	WREG32(offset + 0x124, 0xD17);
	WREG32(offset + 0x128, 0x51C);
	WREG32(offset + 0x12C, 0xB00);
	WREG32(offset + 0x130, 0xD41);
	WREG32(offset + 0x134, 0x7B8);
	WREG32(offset + 0x138, 0x135);
	WREG32(offset + 0x13C, 0x1B0);
	WREG32(offset + 0x400, 0x21);
	WREG32(offset + 0x404, 0x8);
	WREG32(offset + 0x408, 0x36);
	WREG32(offset + 0x40C, 0x16);
	WREG32(offset + 0x410, 0x2E);
	WREG32(offset + 0x414, 0x3C);
}

static void gaudi3_init_scrambling_d2d_7_hbm_cache_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x100, 0x1);
	WREG32(offset + 0x110, 0x800);
	WREG32(offset + 0x114, 0xF73);
	WREG32(offset + 0x118, 0xFA5);
	WREG32(offset + 0x11C, 0xDF6);
	WREG32(offset + 0x120, 0x439);
	WREG32(offset + 0x124, 0xD17);
	WREG32(offset + 0x128, 0x51C);
	WREG32(offset + 0x12C, 0xB00);
	WREG32(offset + 0x130, 0xD41);
	WREG32(offset + 0x134, 0x7B8);
	WREG32(offset + 0x138, 0x135);
	WREG32(offset + 0x13C, 0x1B0);
	WREG32(offset + 0x400, 0x21);
	WREG32(offset + 0x404, 0x8);
	WREG32(offset + 0x408, 0x36);
	WREG32(offset + 0x40C, 0x16);
	WREG32(offset + 0x410, 0x2E);
	WREG32(offset + 0x414, 0x3C);
}

static void gaudi3_init_scrambling_single_die_mc_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x000, 0x8);
	WREG32(offset + 0x004, 0xA);
	WREG32(offset + 0x008, 0xC);
	WREG32(offset + 0x00C, 0xF);
	WREG32(offset + 0x01C, 0x9);
	WREG32(offset + 0x020, 0xB);
	WREG32(offset + 0x024, 0xD);
	WREG32(offset + 0x028, 0x10);
	WREG32(offset + 0x02C, 0x11);
	WREG32(offset + 0x030, 0x12);
	WREG32(offset + 0x034, 0x13);
	WREG32(offset + 0x038, 0x14);
	WREG32(offset + 0x03C, 0x15);
	WREG32(offset + 0x040, 0x16);
	WREG32(offset + 0x044, 0x17);
	WREG32(offset + 0x048, 0x18);
	WREG32(offset + 0x04C, 0x19);
	WREG32(offset + 0x050, 0x1A);
	WREG32(offset + 0x054, 0x1B);
	WREG32(offset + 0x058, 0x1C);
	WREG32(offset + 0x05C, 0x1D);
	WREG32(offset + 0x060, 0xE);
}

static void gaudi3_init_scrambling_d2d_8_hbm_mc_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x000, 0x8);
	WREG32(offset + 0x004, 0xA);
	WREG32(offset + 0x008, 0xC);
	WREG32(offset + 0x00C, 0xF);
	WREG32(offset + 0x01C, 0x9);
	WREG32(offset + 0x020, 0xB);
	WREG32(offset + 0x024, 0xD);
	WREG32(offset + 0x028, 0x10);
	WREG32(offset + 0x02C, 0x11);
	WREG32(offset + 0x030, 0x12);
	WREG32(offset + 0x034, 0x13);
	WREG32(offset + 0x038, 0x14);
	WREG32(offset + 0x03C, 0x15);
	WREG32(offset + 0x040, 0x16);
	WREG32(offset + 0x044, 0x17);
	WREG32(offset + 0x048, 0x18);
	WREG32(offset + 0x04C, 0x19);
	WREG32(offset + 0x050, 0x1A);
	WREG32(offset + 0x054, 0x1B);
	WREG32(offset + 0x058, 0x1C);
	WREG32(offset + 0x05C, 0x1D);
	WREG32(offset + 0x060, 0xE);
}

static void gaudi3_init_scrambling_d2d_7_hbm_mc_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x000, 0xA);
	WREG32(offset + 0x004, 0xB);
	WREG32(offset + 0x008, 0xC);
	WREG32(offset + 0x00C, 0xF);
	WREG32(offset + 0x01C, 0x8);
	WREG32(offset + 0x020, 0x9);
	WREG32(offset + 0x024, 0xD);
	WREG32(offset + 0x028, 0x10);
	WREG32(offset + 0x02C, 0x11);
	WREG32(offset + 0x030, 0x12);
	WREG32(offset + 0x034, 0x13);
	WREG32(offset + 0x038, 0x14);
	WREG32(offset + 0x03C, 0x15);
	WREG32(offset + 0x040, 0x16);
	WREG32(offset + 0x044, 0x17);
	WREG32(offset + 0x048, 0x18);
	WREG32(offset + 0x04C, 0x19);
	WREG32(offset + 0x050, 0x1A);
	WREG32(offset + 0x054, 0x1B);
	WREG32(offset + 0x058, 0x1C);
	WREG32(offset + 0x05C, 0x1D);
	WREG32(offset + 0x060, 0xE);
}

static void gaudi3_init_scrambling_single_die_rtr_cntrl(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_scrambling_single_die_rtr_cntrl_block;
	gaudi3_iterate_rtr_ctrls(hdev, &ctx);
}

static void gaudi3_init_scrambling_d2d_8_hbm_rtr_cntrl(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_scrambling_d2d_8_hbm_rtr_cntrl_block;
	gaudi3_iterate_rtr_ctrls(hdev, &ctx);
}

static void gaudi3_init_scrambling_d2d_7_hbm_rtr_cntrl(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_scrambling_d2d_7_hbm_rtr_cntrl_block;
	gaudi3_iterate_rtr_ctrls(hdev, &ctx);
}

static void gaudi3_init_scrambling_single_die_dtlb(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_scrambling_single_die_dtlb_block;
	gaudi3_iterate_dtlbs(hdev, &ctx);
}

static void gaudi3_init_scrambling_d2d_8_hbm_dtlb(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_scrambling_d2d_8_hbm_dtlb_block;
	gaudi3_iterate_dtlbs(hdev, &ctx);
}

static void gaudi3_init_scrambling_d2d_7_hbm_dtlb(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_scrambling_d2d_7_hbm_dtlb_block;
	gaudi3_iterate_dtlbs(hdev, &ctx);
}

static void gaudi3_init_scrambling_single_die_cache(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_scrambling_single_die_cache_block;
	gaudi3_iterate_cache_slices(hdev, &ctx);
}

static void gaudi3_init_scrambling_d2d_8_hbm_cache(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_scrambling_d2d_8_hbm_cache_block;
	gaudi3_iterate_cache_slices(hdev, &ctx);
}

static void gaudi3_init_scrambling_d2d_7_hbm_cache(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_scrambling_d2d_7_hbm_cache_block;
	gaudi3_iterate_cache_slices(hdev, &ctx);
}

static void gaudi3_init_scrambling_single_die_mc(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_scrambling_single_die_mc_block;
	gaudi3_iterate_mcs(hdev, &ctx);
}

static void gaudi3_init_scrambling_d2d_8_hbm_mc(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_scrambling_d2d_8_hbm_mc_block;
	gaudi3_iterate_mcs(hdev, &ctx);
}

static void gaudi3_init_scrambling_d2d_7_hbm_mc(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_scrambling_d2d_7_hbm_mc_block;
	gaudi3_iterate_mcs(hdev, &ctx);
}

static void gaudi3_init_scrambling_single_die(struct hl_device *hdev)
{
	gaudi3_init_scrambling_single_die_rtr_cntrl(hdev);
	gaudi3_init_scrambling_single_die_dtlb(hdev);
	gaudi3_init_scrambling_single_die_cache(hdev);
	gaudi3_init_scrambling_single_die_mc(hdev);
}

static void gaudi3_init_scrambling_d2d_8_hbm(struct hl_device *hdev)
{
	gaudi3_init_scrambling_d2d_8_hbm_rtr_cntrl(hdev);
	gaudi3_init_scrambling_d2d_8_hbm_dtlb(hdev);
	gaudi3_init_scrambling_d2d_8_hbm_cache(hdev);
	gaudi3_init_scrambling_d2d_8_hbm_mc(hdev);
}

static void gaudi3_init_scrambling_d2d_7_hbm(struct hl_device *hdev)
{
	gaudi3_init_scrambling_d2d_7_hbm_rtr_cntrl(hdev);
	gaudi3_init_scrambling_d2d_7_hbm_dtlb(hdev);
	gaudi3_init_scrambling_d2d_7_hbm_cache(hdev);
	gaudi3_init_scrambling_d2d_7_hbm_mc(hdev);
}

static void gaudi3_init_regulators_single_die_tpc_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE00D000;
	WREG32(offset + 0xA00, 0x000000C8);
	WREG32(offset + 0xA04, 0x00000190);
	WREG32(offset + 0xA20, 0x01040508);
	WREG32(offset + 0xA24, 0x01040508);
	WREG32(offset + 0xB40, 0x11);
	WREG32(offset + 0xB88, 0x00070208);
	WREG32(offset + 0xB8C, 0x00070308);
	WREG32(offset + 0xB90, 0x00060308);
	WREG32(offset + 0xB94, 0x00060308);
	WREG32(offset + 0xB98, 0x00050308);
	WREG32(offset + 0xBA0, 0x00100208);
	WREG32(offset + 0xBA4, 0x000A0308);
	WREG32(offset + 0xBA8, 0x00090308);
	WREG32(offset + 0xBAC, 0x00060308);
	WREG32(offset + 0xBB0, 0x00050308);
	WREG32(offset + 0xBB8, 0x00070208);
	WREG32(offset + 0xBBC, 0x00070308);
	WREG32(offset + 0xBC0, 0x00050308);
	WREG32(offset + 0xBC4, 0x00050408);
	WREG32(offset + 0xBC8, 0x00090908);
	WREG32(offset + 0xBD0, 0x00070108);
	WREG32(offset + 0xBD4, 0x00060208);
	WREG32(offset + 0xBD8, 0x00080308);
	WREG32(offset + 0xBDC, 0x00040308);
	WREG32(offset + 0xBE0, 0x00040408);
	WREG32(offset + 0x840, 0x000F0208);
	WREG32(offset + 0x844, 0x00060208);
	WREG32(offset + 0x848, 0x00060308);
	WREG32(offset + 0x84C, 0x00090308);
	WREG32(offset + 0x850, 0x00090208);
	WREG32(offset + 0x854, 0x00170208);
	WREG32(offset + 0x858, 0x000F0208);
	WREG32(offset + 0x85C, 0x00060208);
	WREG32(offset + 0x860, 0x00060308);
	WREG32(offset + 0x864, 0x000A0108);
	WREG32(offset + 0x868, 0x00070208);
	WREG32(offset + 0x86C, 0x00080308);
	WREG32(offset + 0xC00, 0x00011010);
}

static void gaudi3_init_regulators_d2d_8_hbm_tpc_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE00D000;
	WREG32(offset + 0xA00, 0x000000C8);
	WREG32(offset + 0xA04, 0x00000190);
	WREG32(offset + 0xA20, 0x01040508);
	WREG32(offset + 0xA24, 0x01040508);
	WREG32(offset + 0xB40, 0x11);
	WREG32(offset + 0xB88, 0x00070208);
	WREG32(offset + 0xB8C, 0x00070308);
	WREG32(offset + 0xB90, 0x00060308);
	WREG32(offset + 0xB94, 0x00060308);
	WREG32(offset + 0xB98, 0x00050308);
	WREG32(offset + 0xBA0, 0x00100208);
	WREG32(offset + 0xBA4, 0x000A0308);
	WREG32(offset + 0xBA8, 0x00090308);
	WREG32(offset + 0xBAC, 0x00060308);
	WREG32(offset + 0xBB0, 0x00050308);
	WREG32(offset + 0xBB8, 0x00070208);
	WREG32(offset + 0xBBC, 0x00070308);
	WREG32(offset + 0xBC0, 0x00050308);
	WREG32(offset + 0xBC4, 0x00050408);
	WREG32(offset + 0xBC8, 0x00090908);
	WREG32(offset + 0xBD0, 0x00070108);
	WREG32(offset + 0xBD4, 0x00060208);
	WREG32(offset + 0xBD8, 0x00060308);
	WREG32(offset + 0xBDC, 0x00040308);
	WREG32(offset + 0xBE0, 0x00040408);
	WREG32(offset + 0x840, 0x000F0208);
	WREG32(offset + 0x844, 0x00060208);
	WREG32(offset + 0x848, 0x00060308);
	WREG32(offset + 0x84C, 0x00090308);
	WREG32(offset + 0x850, 0x00090208);
	WREG32(offset + 0x854, 0x00170208);
	WREG32(offset + 0x858, 0x000F0208);
	WREG32(offset + 0x85C, 0x00060208);
	WREG32(offset + 0x860, 0x00060308);
	WREG32(offset + 0x864, 0x000A0108);
	WREG32(offset + 0x868, 0x00070208);
	WREG32(offset + 0x86C, 0x00080308);
	WREG32(offset + 0xC00, 0x00011010);
}

static void gaudi3_init_regulators_d2d_7_hbm_tpc_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE00D000;
	WREG32(offset + 0xA00, 0x000000C8);
	WREG32(offset + 0xA04, 0x00000190);
	WREG32(offset + 0xA20, 0x01040508);
	WREG32(offset + 0xA24, 0x01040508);
	WREG32(offset + 0xB40, 0x11);
}

static void gaudi3_init_regulators_single_die_mme_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE0D6000;
	WREG32(offset + 0xA00, 0x00000190);
	WREG32(offset + 0xA04, 0x00000320);
	WREG32(offset + 0xA20, 0x01040508);
	WREG32(offset + 0xA24, 0x01040508);
	WREG32(offset + 0xB40, 0x11);
	WREG32(offset + 0xB88, 0x00040508);
	WREG32(offset + 0xB8C, 0x00040508);
	WREG32(offset + 0xB90, 0x00040508);
	WREG32(offset + 0xB94, 0x00040508);
	WREG32(offset + 0xB98, 0x00040508);
	WREG32(offset + 0xBA0, 0x00050308);
	WREG32(offset + 0xBA4, 0x00050308);
	WREG32(offset + 0xBA8, 0x00030308);
	WREG32(offset + 0xBAC, 0x00040408);
	WREG32(offset + 0xBB0, 0x00040508);
	WREG32(offset + 0xBB8, 0x00050308);
	WREG32(offset + 0xBBC, 0x00040308);
	WREG32(offset + 0xBC0, 0x00030308);
	WREG32(offset + 0xBC4, 0x00040508);
	WREG32(offset + 0xBC8, 0x00040508);
	WREG32(offset + 0xBD0, 0x00070208);
	WREG32(offset + 0xBD4, 0x00060308);
	WREG32(offset + 0xBD8, 0x00050308);
	WREG32(offset + 0xBDC, 0x00040408);
	WREG32(offset + 0xBE0, 0x00040508);
	WREG32(offset + 0xC00, 0x00011010);
	WREG32(offset + 0xF60, 0x84E95E95);
	WREG32(offset + 0xF64, 0x011E011D);
	WREG32(offset + 0xF68, 0x95FA6FA6);
}

static void gaudi3_init_regulators_d2d_8_hbm_mme_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE0D6000;
	WREG32(offset + 0xA00, 0x00000190);
	WREG32(offset + 0xA04, 0x00000320);
	WREG32(offset + 0xA20, 0x01040508);
	WREG32(offset + 0xA24, 0x01040508);
	WREG32(offset + 0xB40, 0x11);
	WREG32(offset + 0xB88, 0x00040508);
	WREG32(offset + 0xB8C, 0x00040508);
	WREG32(offset + 0xB90, 0x00040508);
	WREG32(offset + 0xB94, 0x00040508);
	WREG32(offset + 0xB98, 0x00040508);
	WREG32(offset + 0xBA0, 0x00050308);
	WREG32(offset + 0xBA4, 0x00050308);
	WREG32(offset + 0xBA8, 0x00030308);
	WREG32(offset + 0xBAC, 0x00040408);
	WREG32(offset + 0xBB0, 0x00040508);
	WREG32(offset + 0xBB8, 0x00050308);
	WREG32(offset + 0xBBC, 0x00040308);
	WREG32(offset + 0xBC0, 0x00030308);
	WREG32(offset + 0xBC4, 0x00040508);
	WREG32(offset + 0xBC8, 0x00040508);
	WREG32(offset + 0xBD0, 0x00070208);
	WREG32(offset + 0xBD4, 0x00060308);
	WREG32(offset + 0xBD8, 0x00050308);
	WREG32(offset + 0xBDC, 0x00040508);
	WREG32(offset + 0xBE0, 0x00040508);
	WREG32(offset + 0xC00, 0x00011010);
	WREG32(offset + 0xF60, 0x84E95E95);
	WREG32(offset + 0xF64, 0x011E011D);
	WREG32(offset + 0xF68, 0x95FA6FA6);
}

static void gaudi3_init_regulators_d2d_7_hbm_mme_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE0D6000;
	WREG32(offset + 0xA00, 0x00000190);
	WREG32(offset + 0xA04, 0x00000320);
	WREG32(offset + 0xA20, 0x01040508);
	WREG32(offset + 0xA24, 0x01040508);
}

static void gaudi3_init_regulators_single_die_nic_cntrl_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xD038000;
	WREG32(offset + 0xA20, 0x01070208);
	WREG32(offset + 0xA24, 0x01070208);
}

static void gaudi3_init_regulators_d2d_8_hbm_nic_cntrl_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xD038000;
	WREG32(offset + 0xA20, 0x01070208);
	WREG32(offset + 0xA24, 0x01070208);
}

static void gaudi3_init_regulators_d2d_7_hbm_nic_cntrl_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xD038000;
	WREG32(offset + 0xA20, 0x01070208);
	WREG32(offset + 0xA24, 0x01070208);
}

static void gaudi3_init_regulators_single_die_nic_data_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xD039000;
	WREG32(offset + 0xA20, 0x01131308);
	WREG32(offset + 0xA24, 0x01131308);
}

static void gaudi3_init_regulators_d2d_8_hbm_nic_data_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xD039000;
	WREG32(offset + 0xA20, 0x01131308);
	WREG32(offset + 0xA24, 0x01131308);
}

static void gaudi3_init_regulators_d2d_7_hbm_nic_data_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xD039000;
	WREG32(offset + 0xA20, 0x01131308);
	WREG32(offset + 0xA24, 0x01131308);
}

static void gaudi3_init_regulators_single_die_pdma_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xC54D000;
	WREG32(offset + 0xA20, 0x01050208);
	WREG32(offset + 0xA24, 0x01050208);
}

static void gaudi3_init_regulators_d2d_8_hbm_pdma_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xC54D000;
	WREG32(offset + 0xA20, 0x01050208);
	WREG32(offset + 0xA24, 0x01050208);
}

static void gaudi3_init_regulators_d2d_7_hbm_pdma_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xC54D000;
	WREG32(offset + 0xA20, 0x01050208);
	WREG32(offset + 0xA24, 0x01050208);
}

static void gaudi3_init_regulators_single_die_sedma_master_if_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE50E000;
	WREG32(offset + 0xA00, 0x00000320);
	WREG32(offset + 0xA04, 0x000004B0);
	WREG32(offset + 0xA20, 0x01040508);
	WREG32(offset + 0xA24, 0x01040508);
	WREG32(offset + 0xC00, 0x00011010);
}

static void gaudi3_init_regulators_d2d_8_hbm_sedma_master_if_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE50E000;
	WREG32(offset + 0xA00, 0x00000320);
	WREG32(offset + 0xA04, 0x000004B0);
	WREG32(offset + 0xA20, 0x01040508);
	WREG32(offset + 0xA24, 0x01040508);
	WREG32(offset + 0xC00, 0x00011010);
}

static void gaudi3_init_regulators_d2d_7_hbm_sedma_master_if_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE50E000;
	WREG32(offset + 0xA00, 0x00000320);
	WREG32(offset + 0xA04, 0x000004B0);
	WREG32(offset + 0xA20, 0x01040508);
	WREG32(offset + 0xA24, 0x01040508);
}

static void gaudi3_init_regulators_single_die_sedma_cmn_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE50D000;
	WREG32(offset + 0xE00, 0x01040508);
	WREG32(offset + 0xE04, 0x01040508);
}

static void gaudi3_init_regulators_d2d_8_hbm_sedma_cmn_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE50D000;
	WREG32(offset + 0xE00, 0x01040508);
	WREG32(offset + 0xE04, 0x01040508);
}

static void gaudi3_init_regulators_d2d_7_hbm_sedma_cmn_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE50D000;
	WREG32(offset + 0xE00, 0x01040508);
	WREG32(offset + 0xE04, 0x01040508);
}

static void gaudi3_init_regulators_single_die_vdec_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE125000;
	WREG32(offset + 0xA20, 0x010A0108);
	WREG32(offset + 0xA24, 0x010A0108);
}

static void gaudi3_init_regulators_d2d_8_hbm_vdec_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE125000;
	WREG32(offset + 0xA20, 0x010A0108);
	WREG32(offset + 0xA24, 0x010A0108);
}

static void gaudi3_init_regulators_d2d_7_hbm_vdec_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE125000;
	WREG32(offset + 0xA20, 0x010A0108);
	WREG32(offset + 0xA24, 0x010A0108);
}

static void gaudi3_init_regulators_single_die_rot_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE551000;
	WREG32(offset + 0xA20, 0x01040508);
	WREG32(offset + 0xA24, 0x01040508);
	WREG32(offset + 0xC00, 0x00011010);
}

static void gaudi3_init_regulators_d2d_8_hbm_rot_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE551000;
	WREG32(offset + 0xA20, 0x01040508);
	WREG32(offset + 0xA24, 0x01040508);
	WREG32(offset + 0xC00, 0x00011010);
}

static void gaudi3_init_regulators_d2d_7_hbm_rot_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	offset += 0xE551000;
	WREG32(offset + 0xA20, 0x01040508);
	WREG32(offset + 0xA24, 0x01040508);
}

static void gaudi3_init_regulators_single_die_arc_farm_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0xA20, 0x01070108);
	WREG32(offset + 0xA24, 0x01200108);
}

static void gaudi3_init_regulators_d2d_8_hbm_arc_farm_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0xA20, 0x01070108);
	WREG32(offset + 0xA24, 0x01200108);
}

static void gaudi3_init_regulators_d2d_7_hbm_arc_farm_block(
			struct hl_device *hdev, u64 offset)
{
	WREG32(offset + 0xA20, 0x01070108);
	WREG32(offset + 0xA24, 0x01200108);
}

static void gaudi3_init_regulators_single_die_tpc(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_single_die_tpc_block;
	gaudi3_iterate_tpcs(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_8_hbm_tpc(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_8_hbm_tpc_block;
	gaudi3_iterate_tpcs(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_7_hbm_tpc(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_7_hbm_tpc_block;
	gaudi3_iterate_tpcs(hdev, &ctx);
}

static void gaudi3_init_regulators_single_die_mme(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_single_die_mme_block;
	gaudi3_iterate_mmes(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_8_hbm_mme(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_8_hbm_mme_block;
	gaudi3_iterate_mmes(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_7_hbm_mme(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_7_hbm_mme_block;
	gaudi3_iterate_mmes(hdev, &ctx);
}

static void gaudi3_init_regulators_single_die_nic_cntrl(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_single_die_nic_cntrl_block;
	gaudi3_iterate_nics(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_8_hbm_nic_cntrl(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_8_hbm_nic_cntrl_block;
	gaudi3_iterate_nics(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_7_hbm_nic_cntrl(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_7_hbm_nic_cntrl_block;
	gaudi3_iterate_nics(hdev, &ctx);
}

static void gaudi3_init_regulators_single_die_nic_data(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_single_die_nic_data_block;
	gaudi3_iterate_nics(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_8_hbm_nic_data(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_8_hbm_nic_data_block;
	gaudi3_iterate_nics(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_7_hbm_nic_data(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_7_hbm_nic_data_block;
	gaudi3_iterate_nics(hdev, &ctx);
}

static void gaudi3_init_regulators_single_die_pdma(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_single_die_pdma_block;
	gaudi3_iterate_pdma_grps(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_8_hbm_pdma(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_8_hbm_pdma_block;
	gaudi3_iterate_pdma_grps(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_7_hbm_pdma(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_7_hbm_pdma_block;
	gaudi3_iterate_pdma_grps(hdev, &ctx);
}

static void gaudi3_init_regulators_single_die_sedma_master_if(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_single_die_sedma_master_if_block;
	gaudi3_iterate_edmas(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_8_hbm_sedma_master_if(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_8_hbm_sedma_master_if_block;
	gaudi3_iterate_edmas(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_7_hbm_sedma_master_if(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_7_hbm_sedma_master_if_block;
	gaudi3_iterate_edmas(hdev, &ctx);
}

static void gaudi3_init_regulators_single_die_sedma_cmn(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_single_die_sedma_cmn_block;
	gaudi3_iterate_edmas(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_8_hbm_sedma_cmn(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_8_hbm_sedma_cmn_block;
	gaudi3_iterate_edmas(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_7_hbm_sedma_cmn(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_7_hbm_sedma_cmn_block;
	gaudi3_iterate_edmas(hdev, &ctx);
}

static void gaudi3_init_regulators_single_die_vdec(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_single_die_vdec_block;
	gaudi3_iterate_decoders(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_8_hbm_vdec(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_8_hbm_vdec_block;
	gaudi3_iterate_decoders(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_7_hbm_vdec(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_7_hbm_vdec_block;
	gaudi3_iterate_decoders(hdev, &ctx);
}

static void gaudi3_init_regulators_single_die_rot(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_single_die_rot_block;
	gaudi3_iterate_rotators(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_8_hbm_rot(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_8_hbm_rot_block;
	gaudi3_iterate_rotators(hdev, &ctx);
}

static void gaudi3_init_regulators_d2d_7_hbm_rot(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_regulators_d2d_7_hbm_rot_block;
	gaudi3_iterate_rotators(hdev, &ctx);
}

static void gaudi3_init_regulators_single_die_arc_farm(struct hl_device *hdev)
{
	gaudi3_init_regulators_single_die_arc_farm_block(hdev, 0xE30C000);
	gaudi3_init_regulators_single_die_arc_farm_block(hdev, 0xE70C000);
	gaudi3_init_regulators_single_die_arc_farm_block(hdev, 0xEB0C000);
	gaudi3_init_regulators_single_die_arc_farm_block(hdev, 0xEF0C000);
}

static void gaudi3_init_regulators_d2d_8_hbm_arc_farm(struct hl_device *hdev)
{
	gaudi3_init_regulators_d2d_8_hbm_arc_farm_block(hdev, 0xE30C000);
	gaudi3_init_regulators_d2d_8_hbm_arc_farm_block(hdev, 0xE70C000);
	gaudi3_init_regulators_d2d_8_hbm_arc_farm_block(hdev, 0xEB0C000);
	gaudi3_init_regulators_d2d_8_hbm_arc_farm_block(hdev, 0xEF0C000);
	gaudi3_init_regulators_d2d_8_hbm_arc_farm_block(hdev, 0xF30C000);
	gaudi3_init_regulators_d2d_8_hbm_arc_farm_block(hdev, 0xF70C000);
	gaudi3_init_regulators_d2d_8_hbm_arc_farm_block(hdev, 0xFB0C000);
	gaudi3_init_regulators_d2d_8_hbm_arc_farm_block(hdev, 0xFF0C000);
}

static void gaudi3_init_regulators_d2d_7_hbm_arc_farm(struct hl_device *hdev)
{
	gaudi3_init_regulators_d2d_7_hbm_arc_farm_block(hdev, 0xE30C000);
	gaudi3_init_regulators_d2d_7_hbm_arc_farm_block(hdev, 0xE70C000);
	gaudi3_init_regulators_d2d_7_hbm_arc_farm_block(hdev, 0xEB0C000);
	gaudi3_init_regulators_d2d_7_hbm_arc_farm_block(hdev, 0xEF0C000);
	gaudi3_init_regulators_d2d_7_hbm_arc_farm_block(hdev, 0xF30C000);
	gaudi3_init_regulators_d2d_7_hbm_arc_farm_block(hdev, 0xF70C000);
	gaudi3_init_regulators_d2d_7_hbm_arc_farm_block(hdev, 0xFB0C000);
	gaudi3_init_regulators_d2d_7_hbm_arc_farm_block(hdev, 0xFF0C000);
}

static void gaudi3_init_regulators_single_die(struct hl_device *hdev)
{
	gaudi3_init_regulators_single_die_tpc(hdev);
	gaudi3_init_regulators_single_die_mme(hdev);
	gaudi3_init_regulators_single_die_nic_cntrl(hdev);
	gaudi3_init_regulators_single_die_nic_data(hdev);
	gaudi3_init_regulators_single_die_pdma(hdev);
	gaudi3_init_regulators_single_die_sedma_master_if(hdev);
	gaudi3_init_regulators_single_die_sedma_cmn(hdev);
	gaudi3_init_regulators_single_die_vdec(hdev);
	gaudi3_init_regulators_single_die_rot(hdev);
	gaudi3_init_regulators_single_die_arc_farm(hdev);
}

static void gaudi3_init_regulators_d2d_8_hbm(struct hl_device *hdev)
{
	gaudi3_init_regulators_d2d_8_hbm_tpc(hdev);
	gaudi3_init_regulators_d2d_8_hbm_mme(hdev);
	gaudi3_init_regulators_d2d_8_hbm_nic_cntrl(hdev);
	gaudi3_init_regulators_d2d_8_hbm_nic_data(hdev);
	gaudi3_init_regulators_d2d_8_hbm_pdma(hdev);
	gaudi3_init_regulators_d2d_8_hbm_sedma_master_if(hdev);
	gaudi3_init_regulators_d2d_8_hbm_sedma_cmn(hdev);
	gaudi3_init_regulators_d2d_8_hbm_vdec(hdev);
	gaudi3_init_regulators_d2d_8_hbm_rot(hdev);
	gaudi3_init_regulators_d2d_8_hbm_arc_farm(hdev);
}

static void gaudi3_init_regulators_d2d_7_hbm(struct hl_device *hdev)
{
	gaudi3_init_regulators_d2d_7_hbm_tpc(hdev);
	gaudi3_init_regulators_d2d_7_hbm_mme(hdev);
	gaudi3_init_regulators_d2d_7_hbm_nic_cntrl(hdev);
	gaudi3_init_regulators_d2d_7_hbm_nic_data(hdev);
	gaudi3_init_regulators_d2d_7_hbm_pdma(hdev);
	gaudi3_init_regulators_d2d_7_hbm_sedma_master_if(hdev);
	gaudi3_init_regulators_d2d_7_hbm_sedma_cmn(hdev);
	gaudi3_init_regulators_d2d_7_hbm_vdec(hdev);
	gaudi3_init_regulators_d2d_7_hbm_rot(hdev);
	gaudi3_init_regulators_d2d_7_hbm_arc_farm(hdev);
}

static void gaudi3_init_qos_single_die_rrtr_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x030, 0x1);
	WREG32(offset + 0x034, 0x2);
	WREG32(offset + 0x038, 0x4);
	WREG32(offset + 0x03C, 0x8);
	WREG32(offset + 0x070, 0x1);
	WREG32(offset + 0x074, 0x2);
	WREG32(offset + 0x078, 0x4);
	WREG32(offset + 0x07C, 0x8);
	WREG32(offset + 0x0A0, 0x1);
	WREG32(offset + 0x0B0, 0x4010);
	WREG32(offset + 0x0B4, 0x4010);
	WREG32(offset + 0x0B8, 0x4010);
	WREG32(offset + 0x0BC, 0x4010);
	WREG32(offset + 0x0C0, 0x10020);
	WREG32(offset + 0x0C4, 0x10020);
	WREG32(offset + 0x0C8, 0x10020);
	WREG32(offset + 0x0CC, 0x10020);
}

static void gaudi3_init_qos_d2d_8_hbm_rrtr_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x030, 0x1);
	WREG32(offset + 0x034, 0x2);
	WREG32(offset + 0x038, 0x4);
	WREG32(offset + 0x03C, 0x8);
	WREG32(offset + 0x070, 0x1);
	WREG32(offset + 0x074, 0x2);
	WREG32(offset + 0x078, 0x4);
	WREG32(offset + 0x07C, 0x8);
	WREG32(offset + 0x0A0, 0x1);
	WREG32(offset + 0x0B0, 0x4010);
	WREG32(offset + 0x0B4, 0x4010);
	WREG32(offset + 0x0B8, 0x4010);
	WREG32(offset + 0x0BC, 0x4010);
	WREG32(offset + 0x0C0, 0x10020);
	WREG32(offset + 0x0C4, 0x10020);
	WREG32(offset + 0x0C8, 0x10020);
	WREG32(offset + 0x0CC, 0x10020);
}

static void gaudi3_init_qos_d2d_7_hbm_rrtr_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x030, 0x1);
	WREG32(offset + 0x034, 0x2);
	WREG32(offset + 0x038, 0x4);
	WREG32(offset + 0x03C, 0x8);
	WREG32(offset + 0x070, 0x1);
	WREG32(offset + 0x074, 0x2);
	WREG32(offset + 0x078, 0x4);
	WREG32(offset + 0x07C, 0x8);
	WREG32(offset + 0x0A0, 0x1);
	WREG32(offset + 0x0B0, 0x4010);
	WREG32(offset + 0x0B4, 0x4010);
	WREG32(offset + 0x0B8, 0x4010);
	WREG32(offset + 0x0BC, 0x4010);
	WREG32(offset + 0x0C0, 0x10020);
	WREG32(offset + 0x0C4, 0x10020);
	WREG32(offset + 0x0C8, 0x10020);
	WREG32(offset + 0x0CC, 0x10020);
}

static void gaudi3_init_qos_single_die_rrtr(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_qos_single_die_rrtr_block;
	gaudi3_iterate_rrtrs(hdev, &ctx);
}

static void gaudi3_init_qos_d2d_8_hbm_rrtr(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_qos_d2d_8_hbm_rrtr_block;
	gaudi3_iterate_rrtrs(hdev, &ctx);
}

static void gaudi3_init_qos_d2d_7_hbm_rrtr(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_qos_d2d_7_hbm_rrtr_block;
	gaudi3_iterate_rrtrs(hdev, &ctx);
}

static void gaudi3_init_qos_single_die(struct hl_device *hdev)
{
	gaudi3_init_qos_single_die_rrtr(hdev);
}

static void gaudi3_init_qos_d2d_8_hbm(struct hl_device *hdev)
{
	gaudi3_init_qos_d2d_8_hbm_rrtr(hdev);
}

static void gaudi3_init_qos_d2d_7_hbm(struct hl_device *hdev)
{
	gaudi3_init_qos_d2d_7_hbm_rrtr(hdev);
}

static void gaudi3_init_cache_single_die_cache_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x010, 0x00001000);
	WREG32(offset + 0x014, 0x00001403);
	WREG32(offset + 0x018, 0x00001807);
	WREG32(offset + 0x01C, 0x00001F0E);
	WREG32(offset + 0x024, 0x00000020);
	WREG32(offset + 0x234, 0x120C0801);
	WREG32(offset + 0x238, 0x12030801);
	WREG32(offset + 0x240, 0x00280010);
	WREG32(offset + 0x620, 0x00030420);
	WREG32(offset + 0x72C, 0x000000F6);
	WREG32(offset + 0x80C, 0x18181011);
	WREG32(offset + 0x860, 0x00000401);
}

static void gaudi3_init_cache_d2d_8_hbm_cache_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x010, 0x00001000);
	WREG32(offset + 0x014, 0x00001403);
	WREG32(offset + 0x018, 0x00001807);
	WREG32(offset + 0x01C, 0x00001F0E);
	WREG32(offset + 0x024, 0x00000020);
	WREG32(offset + 0x234, 0x120C0801);
	WREG32(offset + 0x238, 0x12030801);
	WREG32(offset + 0x240, 0x00280010);
	WREG32(offset + 0x620, 0x00030420);
	WREG32(offset + 0x704, 0x93FF);
	WREG32(offset + 0x72C, 0x000000F6);
	WREG32(offset + 0x80C, 0x18181011);
	WREG32(offset + 0x860, 0x00000401);
}

static void gaudi3_init_cache_d2d_7_hbm_cache_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x010, 0x00001000);
	WREG32(offset + 0x014, 0x00001403);
	WREG32(offset + 0x018, 0x00001807);
	WREG32(offset + 0x01C, 0x00001F0E);
	WREG32(offset + 0x024, 0x00000020);
	WREG32(offset + 0x234, 0x120C0801);
	WREG32(offset + 0x238, 0x12030801);
	WREG32(offset + 0x240, 0x00280010);
	WREG32(offset + 0x620, 0x00030420);
	WREG32(offset + 0x72C, 0x000000F6);
	WREG32(offset + 0x80C, 0x18181011);
	WREG32(offset + 0x860, 0x00000401);
}

static void gaudi3_init_cache_single_die_cache(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_cache_single_die_cache_block;
	gaudi3_iterate_cache_slices(hdev, &ctx);
}

static void gaudi3_init_cache_d2d_8_hbm_cache(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_cache_d2d_8_hbm_cache_block;
	gaudi3_iterate_cache_slices(hdev, &ctx);
}

static void gaudi3_init_cache_d2d_7_hbm_cache(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_cache_d2d_7_hbm_cache_block;
	gaudi3_iterate_cache_slices(hdev, &ctx);
}

static void gaudi3_init_cache_single_die(struct hl_device *hdev)
{
	gaudi3_init_cache_single_die_cache(hdev);
}

static void gaudi3_init_cache_d2d_8_hbm(struct hl_device *hdev)
{
	gaudi3_init_cache_d2d_8_hbm_cache(hdev);
}

static void gaudi3_init_cache_d2d_7_hbm(struct hl_device *hdev)
{
	gaudi3_init_cache_d2d_7_hbm_cache(hdev);
}

static void gaudi3_init_mc_single_die_mc_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x0B0, 0x20);
	WREG32(offset + 0x2D8, 0x1FF);
	WREG32(offset + 0x364, 0x30);
}

static void gaudi3_init_mc_d2d_8_hbm_mc_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x0B0, 0x20);
	WREG32(offset + 0x2D8, 0x1FF);
	WREG32(offset + 0x364, 0x30);
}

static void gaudi3_init_mc_d2d_7_hbm_mc_block(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + 0x0B0, 0x20);
	WREG32(offset + 0x2D8, 0x1FF);
	WREG32(offset + 0x364, 0x30);
}

static void gaudi3_init_mc_single_die_mc(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_mc_single_die_mc_block;
	gaudi3_iterate_mcs(hdev, &ctx);
}

static void gaudi3_init_mc_d2d_8_hbm_mc(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_mc_d2d_8_hbm_mc_block;
	gaudi3_iterate_mcs(hdev, &ctx);
}

static void gaudi3_init_mc_d2d_7_hbm_mc(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_mc_d2d_7_hbm_mc_block;
	gaudi3_iterate_mcs(hdev, &ctx);
}

static void gaudi3_init_mc_single_die(struct hl_device *hdev)
{
	gaudi3_init_mc_single_die_mc(hdev);
}

static void gaudi3_init_mc_d2d_8_hbm(struct hl_device *hdev)
{
	gaudi3_init_mc_d2d_8_hbm_mc(hdev);
}

static void gaudi3_init_mc_d2d_7_hbm(struct hl_device *hdev)
{
	gaudi3_init_mc_d2d_7_hbm_mc(hdev);
}

void gaudi3_init_n2r_credits(struct hl_device *hdev)
{
	dev_dbg(hdev->dev, "N2R_CREDITS version: %s\n", N2R_CREDITS_VERSION);

	if (hdev->asic_prop.num_of_dies == MAX_NUM_OF_DIES)
		gaudi3_init_n2r_credits_d2d(hdev);
	else
		gaudi3_init_n2r_credits_single_die(hdev);
}

void gaudi3_init_r2c_credits(struct hl_device *hdev)
{
	dev_dbg(hdev->dev, "R2C_CREDITS version: %s\n", R2C_CREDITS_VERSION);

	if (hdev->asic_prop.num_of_dies == MAX_NUM_OF_DIES)
		gaudi3_init_r2c_credits_d2d(hdev);
	else
		gaudi3_init_r2c_credits_single_die(hdev);
}

void gaudi3_init_scrambler(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;

	if (!hdev->dram_scrambler_enable || !hdev->dram_enable)
		return;

	if (!hdev->sram_scrambler_enable)
		dev_info(hdev->dev, "DRAM scrambler enable also the SRAM scrambler\n");

	if ((gaudi3->hw_cap_initialized & HW_CAP_SCRAMBLER_MASK) == HW_CAP_SCRAMBLER_MASK)
		return;

	dev_dbg(hdev->dev, "SCRAMBLING version: %s\n", SCRAMBLING_VERSION);

	if (hdev->asic_prop.num_functional_hbms == 8)
		gaudi3_init_scrambling_d2d_8_hbm(hdev);
	else if (hdev->asic_prop.num_functional_hbms == 7)
		gaudi3_init_scrambling_d2d_7_hbm(hdev);
	else
		gaudi3_init_scrambling_single_die(hdev);

	gaudi3->hw_cap_initialized |= HW_CAP_SCRAMBLER_MASK;
}

void gaudi3_init_regulators(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;

	if ((gaudi3->hw_cap_initialized & HW_CAP_REGULATOR_MASK) == HW_CAP_REGULATOR_MASK)
		return;

	dev_dbg(hdev->dev, "REGULATORS version: %s\n", REGULATORS_VERSION);

	if (hdev->asic_prop.num_functional_hbms == 8)
		gaudi3_init_regulators_d2d_8_hbm(hdev);
	else if (hdev->asic_prop.num_functional_hbms == 7)
		gaudi3_init_regulators_d2d_7_hbm(hdev);
	else
		gaudi3_init_regulators_single_die(hdev);

	gaudi3->hw_cap_initialized |= HW_CAP_REGULATOR_MASK;
}

void gaudi3_init_qos(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;

	if ((gaudi3->hw_cap_initialized & HW_CAP_QOS_MASK) == HW_CAP_QOS_MASK)
		return;

	dev_dbg(hdev->dev, "QOS version: %s\n", QOS_VERSION);

	if (hdev->asic_prop.num_functional_hbms == 8)
		gaudi3_init_qos_d2d_8_hbm(hdev);
	else if (hdev->asic_prop.num_functional_hbms == 7)
		gaudi3_init_qos_d2d_7_hbm(hdev);
	else
		gaudi3_init_qos_single_die(hdev);

	gaudi3->hw_cap_initialized |= HW_CAP_QOS_MASK;
}

void gaudi3_init_cache(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;

	if ((gaudi3->hw_cap_initialized & HW_CAP_CACHE_MASK) == HW_CAP_CACHE_MASK)
		return;

	dev_dbg(hdev->dev, "CACHE version: %s\n", CACHE_VERSION);

	if (hdev->asic_prop.num_functional_hbms == 8)
		gaudi3_init_cache_d2d_8_hbm(hdev);
	else if (hdev->asic_prop.num_functional_hbms == 7)
		gaudi3_init_cache_d2d_7_hbm(hdev);
	else
		gaudi3_init_cache_single_die(hdev);

	gaudi3->hw_cap_initialized |= HW_CAP_CACHE_MASK;
}

void gaudi3_init_mc(struct hl_device *hdev)
{
	dev_dbg(hdev->dev, "MC version: %s\n", MC_VERSION);

	if (hdev->asic_prop.num_functional_hbms == 8)
		gaudi3_init_mc_d2d_8_hbm(hdev);
	else if (hdev->asic_prop.num_functional_hbms == 7)
		gaudi3_init_mc_d2d_7_hbm(hdev);
	else
		gaudi3_init_mc_single_die(hdev);
}

void gaudi3_print_sol_config_version(struct hl_device *hdev)
{
	dev_dbg(hdev->dev, "SOL_CONFIG version: %s\n", SOL_CONFIG_VERSION);
}
