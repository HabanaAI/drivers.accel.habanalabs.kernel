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

#ifndef ASIC_REG_STLB_REGS_H_
#define ASIC_REG_STLB_REGS_H_

/*
 *****************************************
 *   STLB
 *   (Prototype: STLB)
 *****************************************
 */

#define mmSTLB_CNTRL_MAIN 0x0

#define mmSTLB_CNTRL_CACHE 0x4

#define mmSTLB_CNTRL_PF 0x8

#define mmSTLB_CNTRL_PAGE_SIZE 0xC

#define mmSTLB_STATUS 0x10

#define mmSTLB_STATUS_TLB_CURR 0x14

#define mmSTLB_STATUS_TLB_MAX 0x18

#define mmSTLB_MAINT_TRIGGER 0x50

#define mmSTLB_MAINT_STATUS 0x54

#define mmSTLB_MAINT_BASE_ADDR 0x58

#define mmSTLB_MAINT_DATA 0x5C

#define mmSTLB_INV_VA_START 0x60

#define mmSTLB_INV_VA_END 0x64

#define mmSTLB_PF_VA_START 0x80

#define mmSTLB_PF_VA_END 0x84

#define mmSTLB_FAULT_CNTRL 0x100

#define mmSTLB_FAULT_LBW_ADDR 0x104

#define mmSTLB_FAULT_LBW_DATA 0x108

#define mmSTLB_FAULT_SYNDORM_CNTRL 0x110

#define mmSTLB_FAULT_SYNDROM1 0x114

#define mmSTLB_FAULT_SYNDROM2 0x118

#define mmSTLB_FAULT_SYNDROM3 0x11C

#define mmSTLB_FAULT_SYNDROM4 0x120

#define mmSTLB_INTR_SPI_CAUSE 0x300

#define mmSTLB_INTR_SPI_MASK 0x308

#define mmSTLB_MSIX_SPI_MASK 0x30C

#define mmSTLB_INTR_SEI_CAUSE 0x310

#define mmSTLB_INTR_SEI_MASK 0x318

#define mmSTLB_MSIX_SEI_MASK 0x31C

#define mmSTLB_ASID_TBL_LSB 0x420

#define mmSTLB_ASID_TBL_MSB 0x424

#define mmSTLB_ASID_TBL_WR 0x42C

#define mmSTLB_ASID_TBL_RD 0x430

#define mmSTLB_ASID_TBL_ADDR 0x434

#define mmSTLB_FSM_DBG 0x500

#endif /* ASIC_REG_STLB_REGS_H_ */
