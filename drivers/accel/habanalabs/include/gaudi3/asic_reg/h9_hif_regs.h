/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

/************************************
 ** This is an auto-generated file **
 **       DO NOT EDIT BELOW        **
 ************************************/

#ifndef ASIC_REG_H9_HIF_REGS_H_
#define ASIC_REG_H9_HIF_REGS_H_

/*
 *****************************************
 *   H9_HIF
 *   (Prototype: H9_HIF)
 *****************************************
 */

#define mmH9_HIF_PC_SEL 0x0

#define mmH9_HIF_MC_MAX_RD_CRED_0 0x10

#define mmH9_HIF_MC_MAX_RD_CRED_1 0x14

#define mmH9_HIF_MC_MAX_RD_CRED_2 0x18

#define mmH9_HIF_MC_MAX_RD_CRED_3 0x1C

#define mmH9_HIF_MC_MAX_WR_CRED_0 0x20

#define mmH9_HIF_MC_MAX_WR_CRED_1 0x24

#define mmH9_HIF_MC_MAX_WR_CRED_2 0x28

#define mmH9_HIF_MC_MAX_WR_CRED_3 0x2C

#define mmH9_HIF_MC_MAX_RD_INFLIGHT_0 0x30

#define mmH9_HIF_MC_MAX_RD_INFLIGHT_1 0x34

#define mmH9_HIF_MC_MAX_RD_INFLIGHT_2 0x38

#define mmH9_HIF_MC_MAX_RD_INFLIGHT_3 0x3C

#define mmH9_HIF_HBM_AXI_GLBL_TAP_EN 0x40

#define mmH9_HIF_HBM_AXI_IDLE_STS 0x44

#define mmH9_HIF_FL_RD_LVL_0 0x100

#define mmH9_HIF_FL_RD_LVL_1 0x104

#define mmH9_HIF_FL_RD_LVL_2 0x108

#define mmH9_HIF_FL_RD_LVL_3 0x10C

#define mmH9_HIF_FL_WR_LVL_0 0x110

#define mmH9_HIF_FL_WR_LVL_1 0x114

#define mmH9_HIF_FL_WR_LVL_2 0x118

#define mmH9_HIF_FL_WR_LVL_3 0x11C

#define mmH9_HIF_LL_RD_LVL_0 0x120

#define mmH9_HIF_LL_RD_LVL_1 0x124

#define mmH9_HIF_LL_RD_LVL_2 0x128

#define mmH9_HIF_LL_RD_LVL_3 0x12C

#define mmH9_HIF_LL_WR_LVL_0 0x130

#define mmH9_HIF_LL_WR_LVL_1 0x134

#define mmH9_HIF_LL_WR_LVL_2 0x138

#define mmH9_HIF_LL_WR_LVL_3 0x13C

#define mmH9_HIF_PC_CONSMD_RD_CRED_0 0x140

#define mmH9_HIF_PC_CONSMD_RD_CRED_1 0x144

#define mmH9_HIF_PC_CONSMD_RD_CRED_2 0x148

#define mmH9_HIF_PC_CONSMD_RD_CRED_3 0x14C

#define mmH9_HIF_PC_CONSMD_RD_CRED_4 0x150

#define mmH9_HIF_PC_CONSMD_RD_CRED_5 0x154

#define mmH9_HIF_PC_CONSMD_RD_CRED_6 0x158

#define mmH9_HIF_PC_CONSMD_RD_CRED_7 0x15C

#define mmH9_HIF_PC_CONSMD_RD_CRED_8 0x160

#define mmH9_HIF_PC_CONSMD_RD_CRED_9 0x164

#define mmH9_HIF_PC_CONSMD_RD_CRED_10 0x168

#define mmH9_HIF_PC_CONSMD_RD_CRED_11 0x16C

#define mmH9_HIF_PC_CONSMD_RD_CRED_12 0x170

#define mmH9_HIF_PC_CONSMD_RD_CRED_13 0x174

#define mmH9_HIF_PC_CONSMD_RD_CRED_14 0x178

#define mmH9_HIF_PC_CONSMD_RD_CRED_15 0x17C

#define mmH9_HIF_PC_CONSMD_WR_CRED_0 0x180

#define mmH9_HIF_PC_CONSMD_WR_CRED_1 0x184

#define mmH9_HIF_PC_CONSMD_WR_CRED_2 0x188

#define mmH9_HIF_PC_CONSMD_WR_CRED_3 0x18C

#define mmH9_HIF_PC_CONSMD_WR_CRED_4 0x190

#define mmH9_HIF_PC_CONSMD_WR_CRED_5 0x194

#define mmH9_HIF_PC_CONSMD_WR_CRED_6 0x198

#define mmH9_HIF_PC_CONSMD_WR_CRED_7 0x19C

#define mmH9_HIF_PC_CONSMD_WR_CRED_8 0x1A0

#define mmH9_HIF_PC_CONSMD_WR_CRED_9 0x1A4

#define mmH9_HIF_PC_CONSMD_WR_CRED_10 0x1A8

#define mmH9_HIF_PC_CONSMD_WR_CRED_11 0x1AC

#define mmH9_HIF_PC_CONSMD_WR_CRED_12 0x1B0

#define mmH9_HIF_PC_CONSMD_WR_CRED_13 0x1B4

#define mmH9_HIF_PC_CONSMD_WR_CRED_14 0x1B8

#define mmH9_HIF_PC_CONSMD_WR_CRED_15 0x1BC

#define mmH9_HIF_PC_RD_INFLIGHT_STS_0 0x1C0

#define mmH9_HIF_PC_RD_INFLIGHT_STS_1 0x1C4

#define mmH9_HIF_PC_RD_INFLIGHT_STS_2 0x1C8

#define mmH9_HIF_PC_RD_INFLIGHT_STS_3 0x1CC

#define mmH9_HIF_PC_RD_INFLIGHT_STS_4 0x1D0

#define mmH9_HIF_PC_RD_INFLIGHT_STS_5 0x1D4

#define mmH9_HIF_PC_RD_INFLIGHT_STS_6 0x1D8

#define mmH9_HIF_PC_RD_INFLIGHT_STS_7 0x1DC

#define mmH9_HIF_PC_RD_INFLIGHT_STS_8 0x1E0

#define mmH9_HIF_PC_RD_INFLIGHT_STS_9 0x1E4

#define mmH9_HIF_PC_RD_INFLIGHT_STS_10 0x1E8

#define mmH9_HIF_PC_RD_INFLIGHT_STS_11 0x1EC

#define mmH9_HIF_PC_RD_INFLIGHT_STS_12 0x1F0

#define mmH9_HIF_PC_RD_INFLIGHT_STS_13 0x1F4

#define mmH9_HIF_PC_RD_INFLIGHT_STS_14 0x1F8

#define mmH9_HIF_PC_RD_INFLIGHT_STS_15 0x1FC

#endif /* ASIC_REG_H9_HIF_REGS_H_ */
