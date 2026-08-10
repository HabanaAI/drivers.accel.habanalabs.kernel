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

#ifndef ASIC_REG_TPC_QM_REGS_H_
#define ASIC_REG_TPC_QM_REGS_H_

/*
 *****************************************
 *   TPC_QM
 *   (Prototype: TPC_NON_TENSOR_DESCRIPTOR)
 *****************************************
 */

#define mmTPC_QM_KERNEL_BASE_ADDRESS_LOW 0x0

#define mmTPC_QM_KERNEL_BASE_ADDRESS_HIGH 0x4

#define mmTPC_QM_KERNEL_CONFIG 0x8

#define mmTPC_QM_POWER_LOOP 0xC

#define mmTPC_QM_SMT_EN 0x10

#define mmTPC_QM_QOS 0x14

#define mmTPC_QM_MCID_FAST_CFG 0x18

#define mmTPC_QM_CLASS_FAST_CFG 0x1C

#define mmTPC_QM_KERNEL_ID 0x20

#define mmTPC_QM_KERNEL_ID_INC 0x24

#define mmTPC_QM_ACTIVE_THRD 0x28

#define mmTPC_QM_SRF_0 0x2C

#define mmTPC_QM_SRF_1 0x30

#define mmTPC_QM_SRF_2 0x34

#define mmTPC_QM_SRF_3 0x38

#define mmTPC_QM_SRF_4 0x3C

#define mmTPC_QM_SRF_5 0x40

#define mmTPC_QM_SRF_6 0x44

#define mmTPC_QM_SRF_7 0x48

#define mmTPC_QM_SRF_8 0x4C

#define mmTPC_QM_SRF_9 0x50

#define mmTPC_QM_SRF_10 0x54

#define mmTPC_QM_SRF_11 0x58

#define mmTPC_QM_SRF_12 0x5C

#define mmTPC_QM_SRF_13 0x60

#define mmTPC_QM_SRF_14 0x64

#define mmTPC_QM_SRF_15 0x68

#define mmTPC_QM_SRF_16 0x6C

#define mmTPC_QM_SRF_17 0x70

#define mmTPC_QM_SRF_18 0x74

#define mmTPC_QM_SRF_19 0x78

#define mmTPC_QM_SRF_20 0x7C

#define mmTPC_QM_SRF_21 0x80

#define mmTPC_QM_SRF_22 0x84

#define mmTPC_QM_SRF_23 0x88

#define mmTPC_QM_SRF_24 0x8C

#define mmTPC_QM_SRF_25 0x90

#define mmTPC_QM_SRF_26 0x94

#define mmTPC_QM_SRF_27 0x98

#define mmTPC_QM_SRF_28 0x9C

#define mmTPC_QM_SRF_29 0xA0

#define mmTPC_QM_SRF_30 0xA4

#define mmTPC_QM_SRF_31 0xA8

#define mmTPC_QM_SRF_32 0xAC

#define mmTPC_QM_SRF_33 0xB0

#define mmTPC_QM_SRF_34 0xB4

#define mmTPC_QM_SRF_35 0xB8

#define mmTPC_QM_SRF_36 0xBC

#define mmTPC_QM_SRF_37 0xC0

#define mmTPC_QM_SRF_38 0xC4

#define mmTPC_QM_SRF_39 0xC8

#define mmTPC_QM_SRF_40 0xCC

#define mmTPC_QM_SRF_41 0xD0

#define mmTPC_QM_SRF_42 0xD4

#define mmTPC_QM_SRF_43 0xD8

#define mmTPC_QM_SRF_44 0xDC

#define mmTPC_QM_SRF_45 0xE0

#define mmTPC_QM_SRF_46 0xE4

#define mmTPC_QM_SRF_47 0xE8

#define mmTPC_QM_SRF_48 0xEC

#define mmTPC_QM_SRF_49 0xF0

#define mmTPC_QM_SRF_50 0xF4

#define mmTPC_QM_SRF_51 0xF8

#define mmTPC_QM_SRF_52 0xFC

#define mmTPC_QM_SRF_53 0x100

#define mmTPC_QM_SRF_54 0x104

#define mmTPC_QM_SRF_55 0x108

#define mmTPC_QM_SRF_56 0x10C

#define mmTPC_QM_SRF_57 0x110

#define mmTPC_QM_SRF_58 0x114

#define mmTPC_QM_SRF_59 0x118

#define mmTPC_QM_ICACHE_AXI_CFG 0x11C

#define mmTPC_QM_LKUP_AXI_CFG 0x120

#define mmTPC_QM_DCACHE_AXI_CFG 0x124

#define mmTPC_QM_CLASS_L2_PREF 0x128

#define mmTPC_QM_CLASS_CTRL_L1_PREF_LD 0x12C

#define mmTPC_QM_HALT_ZERO_SQZ 0x130

#define mmTPC_QM_RMW_CLIP_FP 0x134

#define mmTPC_QM_SYNC_MSG_MODE_SMT4 0x138

#define mmTPC_QM_LD_DEFAULT_HBW_AXI_CFG 0x13C

#define mmTPC_QM_ST_DEFAULT_HBW_AXI_CFG 0x140

#define mmTPC_QM_DCACHE_PREF_WINDOW_INIT 0x144

#define mmTPC_QM_DCACHE_PREF_DYNAMIC_WINDOW 0x148

#define mmTPC_QM_DCACHE_PREF_L1_WINDOW_LIMIT 0x14C

#define mmTPC_QM_DCACHE_PREF_L2_WINDOW_LIMIT 0x150

#define mmTPC_QM_DCACHE_STALL_LENGTH_THR 0x154

#define mmTPC_QM_IRF44_SAT 0x158

#define mmTPC_QM_IRF44_SAT_LOW 0x15C

#define mmTPC_QM_IRF44_SAT_HIGH 0x160

#define mmTPC_QM_TSB_ST_DIRECT_MODE 0x164

#endif /* ASIC_REG_TPC_QM_REGS_H_ */
