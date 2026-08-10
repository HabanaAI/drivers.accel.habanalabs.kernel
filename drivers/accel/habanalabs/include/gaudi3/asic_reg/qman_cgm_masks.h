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

#ifndef ASIC_REG_QMAN_CGM_MASKS_H_
#define ASIC_REG_QMAN_CGM_MASKS_H_

/*
 *****************************************
 *   QMAN_CGM
 *   (Prototype: QMAN_CGM)
 *****************************************
 */

/* QMAN_CGM_CFG */
#define QMAN_CGM_CFG_IDLE_TH_S 0
#define QMAN_CGM_CFG_IDLE_TH_M 0xFFF
#define QMAN_CGM_CFG_G2F_TH_S 16
#define QMAN_CGM_CFG_G2F_TH_M 0xFF0000
#define QMAN_CGM_CFG_HBW_WR_IDLE_MASK_S 28
#define QMAN_CGM_CFG_HBW_WR_IDLE_MASK_M 0x10000000
#define QMAN_CGM_CFG_EN_S 31
#define QMAN_CGM_CFG_EN_M 0x80000000

/* QMAN_CGM_STS */
#define QMAN_CGM_STS_ST_S 0
#define QMAN_CGM_STS_ST_M 0x3
#define QMAN_CGM_STS_CG_S 4
#define QMAN_CGM_STS_CG_M 0x10
#define QMAN_CGM_STS_AGENT_IDLE_S 8
#define QMAN_CGM_STS_AGENT_IDLE_M 0x100
#define QMAN_CGM_STS_AXI_IDLE_S 9
#define QMAN_CGM_STS_AXI_IDLE_M 0x200
#define QMAN_CGM_STS_CP_IDLE_S 10
#define QMAN_CGM_STS_CP_IDLE_M 0x400
#define QMAN_CGM_STS_AXI_WR_IDLE_S 11
#define QMAN_CGM_STS_AXI_WR_IDLE_M 0x800

/* QMAN_CGM_CFG1 */
#define QMAN_CGM_CFG1_MASK_TH_S 0
#define QMAN_CGM_CFG1_MASK_TH_M 0xFF

#endif /* ASIC_REG_QMAN_CGM_MASKS_H_ */
