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

#ifndef ASIC_REG_CBC_REGS_H_
#define ASIC_REG_CBC_REGS_H_

/*
 *****************************************
 *   CBC
 *   (Prototype: CBC)
 *****************************************
 */

#define mmCBC_CBC_EN 0x0

#define mmCBC_CBC_CTRL 0x4

#define mmCBC_CBC_MODE 0x8

#define mmCBC_CBC_RANGE_ASID_CHK_EN_0 0x100

#define mmCBC_CBC_RANGE_ASID_CHK_EN_1 0x104

#define mmCBC_CBC_RANGE_ASID_CHK_EN_2 0x108

#define mmCBC_CBC_RANGE_ASID_CHK_EN_3 0x10C

#define mmCBC_CBC_RANGE_ASID_BASE_0 0x110

#define mmCBC_CBC_RANGE_ASID_BASE_1 0x114

#define mmCBC_CBC_RANGE_ASID_BASE_2 0x118

#define mmCBC_CBC_RANGE_ASID_BASE_3 0x11C

#define mmCBC_CBC_RANGE_ASID_MASK_0 0x120

#define mmCBC_CBC_RANGE_ASID_MASK_1 0x124

#define mmCBC_CBC_RANGE_ASID_MASK_2 0x128

#define mmCBC_CBC_RANGE_ASID_MASK_3 0x12C

#define mmCBC_CBC_ARB_WEIGHT 0x200

#define mmCBC_CBC_PLRU_MODE 0x210

#define mmCBC_SET_SCRAM_EN 0x220

#define mmCBC_SET_SCRAM_POLY_0 0x230

#define mmCBC_SET_SCRAM_POLY_1 0x234

#define mmCBC_SET_SCRAM_POLY_2 0x238

#define mmCBC_SET_SCRAM_POLY_3 0x23C

#define mmCBC_SET_SCRAM_POLY_4 0x240

#define mmCBC_SET_SCRAM_POLY_5 0x244

#define mmCBC_SET_SCRAM_POLY_6 0x248

#define mmCBC_SET_SCRAM_POLY_7 0x24C

#define mmCBC_SET_SCRAM_POLY_8 0x250

#define mmCBC_SET_SCRAM_POLY_9 0x254

#define mmCBC_SET_SCRAM_POLY_10 0x258

#define mmCBC_SET_SCRAM_POLY_11 0x25C

#define mmCBC_SET_SCRAM_POLY_12 0x260

#define mmCBC_SET_SCRAM_POLY_13 0x264

#define mmCBC_SET_SCRAM_POLY_14 0x268

#define mmCBC_SET_SCRAM_POLY_15 0x26C

#define mmCBC_SET_SCRAM_POLY_16 0x270

#define mmCBC_SET_SCRAM_POLY_17 0x274

#define mmCBC_SET_SCRAM_POLY_18 0x278

#define mmCBC_SET_SCRAM_POLY_19 0x27C

#define mmCBC_LBW_ID_0 0x300

#define mmCBC_LBW_ID_1 0x304

#define mmCBC_LBW_ID_2 0x308

#define mmCBC_LBW_ID_3 0x30C

#define mmCBC_LBW_ID_4 0x310

#define mmCBC_LBW_USER_0 0x320

#define mmCBC_LBW_USER_1 0x324

#define mmCBC_LBW_USER_2 0x328

#define mmCBC_LBW_USER_3 0x32C

#define mmCBC_LBW_USER_4 0x330

#define mmCBC_LBW_PROT0_0 0x340

#define mmCBC_LBW_PROT0_1 0x344

#define mmCBC_LBW_PROT0_2 0x348

#define mmCBC_LBW_PROT0_3 0x34C

#define mmCBC_LBW_PROT0_4 0x350

#define mmCBC_LBW_PROT1_0 0x360

#define mmCBC_LBW_PROT1_1 0x364

#define mmCBC_LBW_PROT1_2 0x368

#define mmCBC_LBW_PROT1_3 0x36C

#define mmCBC_LBW_PROT1_4 0x370

#define mmCBC_LBW_PROT2_0 0x380

#define mmCBC_LBW_PROT2_1 0x384

#define mmCBC_LBW_PROT2_2 0x388

#define mmCBC_LBW_PROT2_3 0x38C

#define mmCBC_LBW_PROT2_4 0x390

#define mmCBC_LBW_RSP_ERR_HAPPENED_CLR_0 0x400

#define mmCBC_LBW_RSP_ERR_HAPPENED_CLR_1 0x404

#define mmCBC_LBW_RSP_ERR_HAPPENED_CLR_2 0x408

#define mmCBC_LBW_RSP_ERR_HAPPENED_CLR_3 0x40C

#define mmCBC_LBW_RSP_ERR_HAPPENED_CLR_4 0x410

#define mmCBC_LBW_RSP_ERR_HAPPENED_ST_0 0x420

#define mmCBC_LBW_RSP_ERR_HAPPENED_ST_1 0x424

#define mmCBC_LBW_RSP_ERR_HAPPENED_ST_2 0x428

#define mmCBC_LBW_RSP_ERR_HAPPENED_ST_3 0x42C

#define mmCBC_LBW_RSP_ERR_HAPPENED_ST_4 0x430

#define mmCBC_LBW_RSP_ERR_MISC_0 0x440

#define mmCBC_LBW_RSP_ERR_MISC_1 0x444

#define mmCBC_LBW_RSP_ERR_MISC_2 0x448

#define mmCBC_LBW_RSP_ERR_MISC_3 0x44C

#define mmCBC_LBW_RSP_ERR_MISC_4 0x450

#define mmCBC_CBC_DBG_RRB 0x500

#define mmCBC_CBC_DBG_CSH_VLD 0x504

#define mmCBC_CBC_DBG_CNT_EN 0x510

#define mmCBC_CBC_DBG_TOT 0x520

#define mmCBC_CBC_DBG_RANGE_0 0x530

#define mmCBC_CBC_DBG_RANGE_1 0x534

#define mmCBC_CBC_DBG_RANGE_2 0x538

#define mmCBC_CBC_DBG_RANGE_3 0x53C

#define mmCBC_CBC_WRAP_DBG_TOT 0x540

#define mmCBC_CBC_WRAP_DBG_RANGE_0 0x560

#define mmCBC_CBC_WRAP_DBG_RANGE_1 0x564

#define mmCBC_CBC_WRAP_DBG_RANGE_2 0x568

#define mmCBC_CBC_WRAP_DBG_RANGE_3 0x56C

#define mmCBC_CBC_INV_CHK_BIT 0x600

#endif /* ASIC_REG_CBC_REGS_H_ */
