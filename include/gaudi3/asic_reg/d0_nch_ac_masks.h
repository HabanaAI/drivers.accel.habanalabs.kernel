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

#ifndef ASIC_REG_D0_NCH_AC_MASKS_H_
#define ASIC_REG_D0_NCH_AC_MASKS_H_

/*
 *****************************************
 *   D0_NCH_AC
 *   (Prototype: AUTONOMOUS_CONTROL)
 *****************************************
 */

/* D0_NCH_AC_CTRL */
#define D0_NCH_AC_CTRL_EN_S 0
#define D0_NCH_AC_CTRL_EN_M 0x1
#define D0_NCH_AC_CTRL_NUM_CMD_S 4
#define D0_NCH_AC_CTRL_NUM_CMD_M 0xF0

/* D0_NCH_AC_IP_PROT */
#define D0_NCH_AC_IP_PROT_VAL_S 0
#define D0_NCH_AC_IP_PROT_VAL_M 0x7

/* D0_NCH_AC_LBW_PROT */
#define D0_NCH_AC_LBW_PROT_VAL_S 0
#define D0_NCH_AC_LBW_PROT_VAL_M 0x7

/* D0_NCH_AC_POLLING_MASK */
#define D0_NCH_AC_POLLING_MASK_VAL_S 0
#define D0_NCH_AC_POLLING_MASK_VAL_M 0xFFFFFFFF

/* D0_NCH_AC_STATUS */
#define D0_NCH_AC_STATUS_APB_ERR_S 0
#define D0_NCH_AC_STATUS_APB_ERR_M 0x1
#define D0_NCH_AC_STATUS_CMD_ERR_S 1
#define D0_NCH_AC_STATUS_CMD_ERR_M 0x2
#define D0_NCH_AC_STATUS_IDLE_S 2
#define D0_NCH_AC_STATUS_IDLE_M 0x4
#define D0_NCH_AC_STATUS_APB_ERR_INDEX_S 4
#define D0_NCH_AC_STATUS_APB_ERR_INDEX_M 0xF0
#define D0_NCH_AC_STATUS_CMD_ERR_INDEX_S 8
#define D0_NCH_AC_STATUS_CMD_ERR_INDEX_M 0xF00
#define D0_NCH_AC_STATUS_CURRENT_INDEX_S 16
#define D0_NCH_AC_STATUS_CURRENT_INDEX_M 0xF0000

/* D0_NCH_AC_CMD_ENTRY */
#define D0_NCH_AC_CMD_ENTRY_VAL_S 0
#define D0_NCH_AC_CMD_ENTRY_VAL_M 0xF

/* D0_NCH_AC_ADDR_ENTRY */
#define D0_NCH_AC_ADDR_ENTRY_VAL_S 0
#define D0_NCH_AC_ADDR_ENTRY_VAL_M 0xFFFFFFFF

/* D0_NCH_AC_DATA_ENTRY */
#define D0_NCH_AC_DATA_ENTRY_VAL_S 0
#define D0_NCH_AC_DATA_ENTRY_VAL_M 0xFFFFFFFF

#endif /* ASIC_REG_D0_NCH_AC_MASKS_H_ */
