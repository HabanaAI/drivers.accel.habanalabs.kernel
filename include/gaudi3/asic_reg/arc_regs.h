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

#ifndef ASIC_REG_ARC_REGS_H_
#define ASIC_REG_ARC_REGS_H_

/*
 *****************************************
 *   ARC
 *   (Prototype: ARC)
 *****************************************
 */

#define mmARC_RUN_HALT_REQ 0x100

#define mmARC_RUN_HALT_ACK 0x104

#define mmARC_RST_VEC_ADDR 0x108

#define mmARC_DBG_MODE 0x10C

#define mmARC_CLUSTER_NUM 0x110

#define mmARC_ARC_NUM 0x114

#define mmARC_WAKE_UP_EVENT 0x118

#define mmARC_TRACE_START_STOP 0x11C

#define mmARC_CTI_AP_STS 0x120

#define mmARC_CTI_RTT_FILTERS 0x124

#define mmARC_CTI_CFG_MUX_SEL 0x128

#define mmARC_ARC_RST 0x12C

#define mmARC_ARC_REGION_CFG 0x130

#define mmARC_SRAM_LSB_ADDR 0x138

#define mmARC_SRAM_MSB_ADDR 0x13C

#define mmARC_FW_MEM_LSB_ADDR 0x140

#define mmARC_FW_MEM_MSB_ADDR 0x144

#define mmARC_VIR_MEM0_LSB_ADDR 0x148

#define mmARC_VIR_MEM0_MSB_ADDR 0x14C

#define mmARC_VIR_MEM1_LSB_ADDR 0x150

#define mmARC_VIR_MEM1_MSB_ADDR 0x154

#define mmARC_VIR_MEM2_LSB_ADDR 0x158

#define mmARC_VIR_MEM2_MSB_ADDR 0x15C

#define mmARC_VIR_MEM3_LSB_ADDR 0x160

#define mmARC_VIR_MEM3_MSB_ADDR 0x164

#define mmARC_VIR_MEM0_OFFSET 0x168

#define mmARC_VIR_MEM1_OFFSET 0x16C

#define mmARC_VIR_MEM2_OFFSET 0x170

#define mmARC_VIR_MEM3_OFFSET 0x174

#define mmARC_PCIE_LOWER_LSB_ADDR 0x178

#define mmARC_PCIE_LOWER_MSB_ADDR 0x17C

#define mmARC_PCIE_UPPER_LSB_ADDR 0x180

#define mmARC_PCIE_UPPER_MSB_ADDR 0x184

#define mmARC_D2D_HBW_LSB_ADDR 0x188

#define mmARC_D2D_HBW_MSB_ADDR 0x18C

#define mmARC_GENERAL_PURPOSE_LSB_ADDR_0 0x190

#define mmARC_GENERAL_PURPOSE_LSB_ADDR_1 0x194

#define mmARC_GENERAL_PURPOSE_LSB_ADDR_2 0x198

#define mmARC_GENERAL_PURPOSE_LSB_ADDR_3 0x19C

#define mmARC_GENERAL_PURPOSE_LSB_ADDR_4 0x1A0

#define mmARC_GENERAL_PURPOSE_MSB_ADDR_0 0x1A4

#define mmARC_GENERAL_PURPOSE_MSB_ADDR_1 0x1A8

#define mmARC_GENERAL_PURPOSE_MSB_ADDR_2 0x1AC

#define mmARC_GENERAL_PURPOSE_MSB_ADDR_3 0x1B0

#define mmARC_GENERAL_PURPOSE_MSB_ADDR_4 0x1B4

#define mmARC_AUX2APB_PPROT_0 0x1F4

#define mmARC_AUX2APB_PPROT_1 0x1F8

#define mmARC_AUX2APB_PPROT_2 0x1FC

#define mmARC_KMD_HW_DIRTY_STATUS 0x200

#define mmARC_PF_PQ_PI 0x204

#define mmARC_PQ_BASE_ADDR_LOW 0x208

#define mmARC_PQ_BASE_ADDR_HIGH 0x20C

#define mmARC_PQ_LENGTH 0x210

#define mmARC_CQ_BASE_ADDR_LOW 0x214

#define mmARC_CQ_BASE_ADDR_HIGH 0x218

#define mmARC_CQ_LENGTH 0x21C

#define mmARC_EQ_BASE_ADDR_LOW 0x220

#define mmARC_EQ_BASE_ADDR_HIGH 0x224

#define mmARC_EQ_LENGTH 0x228

#define mmARC_EQ_RD_OFFS 0x22C

#define mmARC_QUEUE_INIT 0x230

#define mmARC_IRQ_INTR_MASK_0 0x280

#define mmARC_IRQ_INTR_MASK_1 0x284

#define mmARC_IRQ_INTR_MASK_2 0x288

#define mmARC_IRQ_INTR_MASK_3 0x28C

#define mmARC_IRQ_INTR_MASK_4 0x290

#define mmARC_IRQ_INTR_MASK_5 0x294

#define mmARC_IRQ_INTR_MASK_6 0x298

#define mmARC_IRQ_INTR_MASK_7 0x29C

#define mmARC_APB_ARB_TO 0x2B0

#define mmARC_SEMAPHORE_0 0x300

#define mmARC_SEMAPHORE_1 0x304

#define mmARC_SEMAPHORE_2 0x308

#define mmARC_SEMAPHORE_3 0x30C

#define mmARC_SEMAPHORE_4 0x310

#define mmARC_SEMAPHORE_5 0x314

#define mmARC_SEMAPHORE_6 0x318

#define mmARC_SEMAPHORE_7 0x31C

#define mmARC_SEMAPHORE_8 0x320

#define mmARC_SEMAPHORE_9 0x324

#define mmARC_SEMAPHORE_10 0x328

#define mmARC_SEMAPHORE_11 0x32C

#define mmARC_SEMAPHORE_12 0x330

#define mmARC_SEMAPHORE_13 0x334

#define mmARC_SEMAPHORE_14 0x338

#define mmARC_SEMAPHORE_15 0x33C

#define mmARC_SEMAPHORE_16 0x340

#define mmARC_SEMAPHORE_17 0x344

#define mmARC_SEMAPHORE_18 0x348

#define mmARC_SEMAPHORE_19 0x34C

#define mmARC_SEMAPHORE_20 0x350

#define mmARC_SEMAPHORE_21 0x354

#define mmARC_SEMAPHORE_22 0x358

#define mmARC_SEMAPHORE_23 0x35C

#define mmARC_SEMAPHORE_24 0x360

#define mmARC_SEMAPHORE_25 0x364

#define mmARC_SEMAPHORE_26 0x368

#define mmARC_SEMAPHORE_27 0x36C

#define mmARC_SEMAPHORE_28 0x370

#define mmARC_SEMAPHORE_29 0x374

#define mmARC_SEMAPHORE_30 0x378

#define mmARC_SEMAPHORE_31 0x37C

#endif /* ASIC_REG_ARC_REGS_H_ */
