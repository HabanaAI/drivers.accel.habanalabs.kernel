/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

/************************************
 ** This is an auto-generated file **
 **       DO NOT EDIT BELOW        **
 ************************************/

#ifndef ASIC_REG_CPU_IF_MASKS_H_
#define ASIC_REG_CPU_IF_MASKS_H_

/*
 *****************************************
 *   CPU_IF (Prototype: CPU_IF)
 *****************************************
 */

/* CPU_IF_ARUSER_OVR */
#define CPU_IF_ARUSER_OVR_VAL_SHIFT                                  0
#define CPU_IF_ARUSER_OVR_VAL_MASK                                   0xFFFFFFFF

/* CPU_IF_ARUSER_OVR_EN */
#define CPU_IF_ARUSER_OVR_EN_VAL_SHIFT                               0
#define CPU_IF_ARUSER_OVR_EN_VAL_MASK                                0xFFFFFFFF

/* CPU_IF_AWUSER_OVR */
#define CPU_IF_AWUSER_OVR_VAL_SHIFT                                  0
#define CPU_IF_AWUSER_OVR_VAL_MASK                                   0xFFFFFFFF

/* CPU_IF_AWUSER_OVR_EN */
#define CPU_IF_AWUSER_OVR_EN_VAL_SHIFT                               0
#define CPU_IF_AWUSER_OVR_EN_VAL_MASK                                0xFFFFFFFF

/* CPU_IF_AXCACHE_OVR */
#define CPU_IF_AXCACHE_OVR_READ_SHIFT                                0
#define CPU_IF_AXCACHE_OVR_READ_MASK                                 0xF
#define CPU_IF_AXCACHE_OVR_WRITE_SHIFT                               4
#define CPU_IF_AXCACHE_OVR_WRITE_MASK                                0xF0
#define CPU_IF_AXCACHE_OVR_RD_EN_SHIFT                               8
#define CPU_IF_AXCACHE_OVR_RD_EN_MASK                                0xF00
#define CPU_IF_AXCACHE_OVR_WR_EN_SHIFT                               12
#define CPU_IF_AXCACHE_OVR_WR_EN_MASK                                0xF000

/* CPU_IF_LOCK_OVR */
#define CPU_IF_LOCK_OVR_READ_SHIFT                                   0
#define CPU_IF_LOCK_OVR_READ_MASK                                    0x3
#define CPU_IF_LOCK_OVR_WRITE_SHIFT                                  4
#define CPU_IF_LOCK_OVR_WRITE_MASK                                   0x30
#define CPU_IF_LOCK_OVR_RD_EN_SHIFT                                  8
#define CPU_IF_LOCK_OVR_RD_EN_MASK                                   0x300
#define CPU_IF_LOCK_OVR_WR_EN_SHIFT                                  12
#define CPU_IF_LOCK_OVR_WR_EN_MASK                                   0x3000

/* CPU_IF_PROT_OVR */
#define CPU_IF_PROT_OVR_READ_SHIFT                                   0
#define CPU_IF_PROT_OVR_READ_MASK                                    0x7
#define CPU_IF_PROT_OVR_WRITE_SHIFT                                  4
#define CPU_IF_PROT_OVR_WRITE_MASK                                   0x70
#define CPU_IF_PROT_OVR_RD_EN_SHIFT                                  8
#define CPU_IF_PROT_OVR_RD_EN_MASK                                   0x700
#define CPU_IF_PROT_OVR_WR_EN_SHIFT                                  12
#define CPU_IF_PROT_OVR_WR_EN_MASK                                   0x7000

/* CPU_IF_MAX_OUTSTANDING */
#define CPU_IF_MAX_OUTSTANDING_READ_SHIFT                            0
#define CPU_IF_MAX_OUTSTANDING_READ_MASK                             0xFF
#define CPU_IF_MAX_OUTSTANDING_WRITE_SHIFT                           8
#define CPU_IF_MAX_OUTSTANDING_WRITE_MASK                            0xFF00

/* CPU_IF_EARLY_BRESP_EN */
#define CPU_IF_EARLY_BRESP_EN_VAL_SHIFT                              0
#define CPU_IF_EARLY_BRESP_EN_VAL_MASK                               0x1

/* CPU_IF_FORCE_RSP_OK */
#define CPU_IF_FORCE_RSP_OK_VAL_SHIFT                                0
#define CPU_IF_FORCE_RSP_OK_VAL_MASK                                 0x1

/* CPU_IF_CPU_MSB_ADDR */
#define CPU_IF_CPU_MSB_ADDR_VAL_SHIFT                                0
#define CPU_IF_CPU_MSB_ADDR_VAL_MASK                                 0x7FF

/* CPU_IF_AXI_SPLIT_INTR */
#define CPU_IF_AXI_SPLIT_INTR_IND_SHIFT                              0
#define CPU_IF_AXI_SPLIT_INTR_IND_MASK                               0x1
#define CPU_IF_AXI_SPLIT_INTR_CLR_SHIFT                              1
#define CPU_IF_AXI_SPLIT_INTR_CLR_MASK                               0x2

/* CPU_IF_TOTAL_WR_CNT */
#define CPU_IF_TOTAL_WR_CNT_VAL_SHIFT                                0
#define CPU_IF_TOTAL_WR_CNT_VAL_MASK                                 0xFFFFFFFF

/* CPU_IF_INFLIGHT_WR_CNT */
#define CPU_IF_INFLIGHT_WR_CNT_VAL_SHIFT                             0
#define CPU_IF_INFLIGHT_WR_CNT_VAL_MASK                              0xFFFFFFFF

/* CPU_IF_TOTAL_RD_CNT */
#define CPU_IF_TOTAL_RD_CNT_VAL_SHIFT                                0
#define CPU_IF_TOTAL_RD_CNT_VAL_MASK                                 0xFFFFFFFF

/* CPU_IF_INFLIGHT_RD_CNT */
#define CPU_IF_INFLIGHT_RD_CNT_VAL_SHIFT                             0
#define CPU_IF_INFLIGHT_RD_CNT_VAL_MASK                              0xFFFFFFFF

/* CPU_IF_PF_PQ_PI */
#define CPU_IF_PF_PQ_PI_VAL_SHIFT                                    0
#define CPU_IF_PF_PQ_PI_VAL_MASK                                     0xFFFFFFFF

/* CPU_IF_PQ_BASE_ADDR_LOW */
#define CPU_IF_PQ_BASE_ADDR_LOW_VAL_SHIFT                            0
#define CPU_IF_PQ_BASE_ADDR_LOW_VAL_MASK                             0xFFFFFFFF

/* CPU_IF_PQ_BASE_ADDR_HIGH */
#define CPU_IF_PQ_BASE_ADDR_HIGH_VAL_SHIFT                           0
#define CPU_IF_PQ_BASE_ADDR_HIGH_VAL_MASK                            0xFFFFFFFF

/* CPU_IF_PQ_LENGTH */
#define CPU_IF_PQ_LENGTH_VAL_SHIFT                                   0
#define CPU_IF_PQ_LENGTH_VAL_MASK                                    0xFFFFFFFF

/* CPU_IF_CQ_BASE_ADDR_LOW */
#define CPU_IF_CQ_BASE_ADDR_LOW_VAL_SHIFT                            0
#define CPU_IF_CQ_BASE_ADDR_LOW_VAL_MASK                             0xFFFFFFFF

/* CPU_IF_CQ_BASE_ADDR_HIGH */
#define CPU_IF_CQ_BASE_ADDR_HIGH_VAL_SHIFT                           0
#define CPU_IF_CQ_BASE_ADDR_HIGH_VAL_MASK                            0xFFFFFFFF

/* CPU_IF_CQ_LENGTH */
#define CPU_IF_CQ_LENGTH_VAL_SHIFT                                   0
#define CPU_IF_CQ_LENGTH_VAL_MASK                                    0xFFFFFFFF

/* CPU_IF_EQ_BASE_ADDR_LOW */
#define CPU_IF_EQ_BASE_ADDR_LOW_VAL_SHIFT                            0
#define CPU_IF_EQ_BASE_ADDR_LOW_VAL_MASK                             0xFFFFFFFF

/* CPU_IF_EQ_BASE_ADDR_HIGH */
#define CPU_IF_EQ_BASE_ADDR_HIGH_VAL_SHIFT                           0
#define CPU_IF_EQ_BASE_ADDR_HIGH_VAL_MASK                            0xFFFFFFFF

/* CPU_IF_EQ_LENGTH */
#define CPU_IF_EQ_LENGTH_VAL_SHIFT                                   0
#define CPU_IF_EQ_LENGTH_VAL_MASK                                    0xFFFFFFFF

/* CPU_IF_EQ_RD_OFFS */
#define CPU_IF_EQ_RD_OFFS_VAL_SHIFT                                  0
#define CPU_IF_EQ_RD_OFFS_VAL_MASK                                   0xFFFFFFFF

/* CPU_IF_QUEUE_INIT */
#define CPU_IF_QUEUE_INIT_VAL_SHIFT                                  0
#define CPU_IF_QUEUE_INIT_VAL_MASK                                   0xFFFFFFFF

/* CPU_IF_TPC_SERR_INTR_STS */
#define CPU_IF_TPC_SERR_INTR_STS_VAL_SHIFT                           0
#define CPU_IF_TPC_SERR_INTR_STS_VAL_MASK                            0xFF

/* CPU_IF_TPC_SERR_INTR_CLR */
#define CPU_IF_TPC_SERR_INTR_CLR_VAL_SHIFT                           0
#define CPU_IF_TPC_SERR_INTR_CLR_VAL_MASK                            0xFF

/* CPU_IF_TPC_SERR_INTR_MASK */
#define CPU_IF_TPC_SERR_INTR_MASK_VAL_SHIFT                          0
#define CPU_IF_TPC_SERR_INTR_MASK_VAL_MASK                           0xFF

/* CPU_IF_TPC_DERR_INTR_STS */
#define CPU_IF_TPC_DERR_INTR_STS_VAL_SHIFT                           0
#define CPU_IF_TPC_DERR_INTR_STS_VAL_MASK                            0xFF

/* CPU_IF_TPC_DERR_INTR_CLR */
#define CPU_IF_TPC_DERR_INTR_CLR_VAL_SHIFT                           0
#define CPU_IF_TPC_DERR_INTR_CLR_VAL_MASK                            0xFF

/* CPU_IF_TPC_DERR_INTR_MASK */
#define CPU_IF_TPC_DERR_INTR_MASK_VAL_SHIFT                          0
#define CPU_IF_TPC_DERR_INTR_MASK_VAL_MASK                           0xFF

/* CPU_IF_DMA_SERR_INTR_STS */
#define CPU_IF_DMA_SERR_INTR_STS_VAL_SHIFT                           0
#define CPU_IF_DMA_SERR_INTR_STS_VAL_MASK                            0xFF

/* CPU_IF_DMA_SERR_INTR_CLR */
#define CPU_IF_DMA_SERR_INTR_CLR_VAL_SHIFT                           0
#define CPU_IF_DMA_SERR_INTR_CLR_VAL_MASK                            0xFF

/* CPU_IF_DMA_SERR_INTR_MASK */
#define CPU_IF_DMA_SERR_INTR_MASK_VAL_SHIFT                          0
#define CPU_IF_DMA_SERR_INTR_MASK_VAL_MASK                           0xFF

/* CPU_IF_DMA_DERR_INTR_STS */
#define CPU_IF_DMA_DERR_INTR_STS_VAL_SHIFT                           0
#define CPU_IF_DMA_DERR_INTR_STS_VAL_MASK                            0xFF

/* CPU_IF_DMA_DERR_INTR_CLR */
#define CPU_IF_DMA_DERR_INTR_CLR_VAL_SHIFT                           0
#define CPU_IF_DMA_DERR_INTR_CLR_VAL_MASK                            0xFF

/* CPU_IF_DMA_DERR_INTR_MASK */
#define CPU_IF_DMA_DERR_INTR_MASK_VAL_SHIFT                          0
#define CPU_IF_DMA_DERR_INTR_MASK_VAL_MASK                           0xFF

/* CPU_IF_SRAM_SERR_INTR_STS */
#define CPU_IF_SRAM_SERR_INTR_STS_VAL_SHIFT                          0
#define CPU_IF_SRAM_SERR_INTR_STS_VAL_MASK                           0xFFFFFFFF

/* CPU_IF_SRAM_SERR_INTR_CLR */
#define CPU_IF_SRAM_SERR_INTR_CLR_VAL_SHIFT                          0
#define CPU_IF_SRAM_SERR_INTR_CLR_VAL_MASK                           0xFFFFFFFF

/* CPU_IF_SRAM_SERR_INTR_MASK */
#define CPU_IF_SRAM_SERR_INTR_MASK_VAL_SHIFT                         0
#define CPU_IF_SRAM_SERR_INTR_MASK_VAL_MASK                          0xFFFFFFFF

/* CPU_IF_SRAM_DERR_INTR_STS */
#define CPU_IF_SRAM_DERR_INTR_STS_VAL_SHIFT                          0
#define CPU_IF_SRAM_DERR_INTR_STS_VAL_MASK                           0xFFFFFFFF

/* CPU_IF_SRAM_DERR_INTR_CLR */
#define CPU_IF_SRAM_DERR_INTR_CLR_VAL_SHIFT                          0
#define CPU_IF_SRAM_DERR_INTR_CLR_VAL_MASK                           0xFFFFFFFF

/* CPU_IF_SRAM_DERR_INTR_MASK */
#define CPU_IF_SRAM_DERR_INTR_MASK_VAL_SHIFT                         0
#define CPU_IF_SRAM_DERR_INTR_MASK_VAL_MASK                          0xFFFFFFFF

/* CPU_IF_NIC_SERR_INTR_STS */
#define CPU_IF_NIC_SERR_INTR_STS_VAL_SHIFT                           0
#define CPU_IF_NIC_SERR_INTR_STS_VAL_MASK                            0x3FF

/* CPU_IF_NIC_SERR_INTR_CLR */
#define CPU_IF_NIC_SERR_INTR_CLR_VAL_SHIFT                           0
#define CPU_IF_NIC_SERR_INTR_CLR_VAL_MASK                            0x3FF

/* CPU_IF_NIC_SERR_INTR_MASK */
#define CPU_IF_NIC_SERR_INTR_MASK_VAL_SHIFT                          0
#define CPU_IF_NIC_SERR_INTR_MASK_VAL_MASK                           0x3FF

/* CPU_IF_NIC_DERR_INTR_STS */
#define CPU_IF_NIC_DERR_INTR_STS_VAL_SHIFT                           0
#define CPU_IF_NIC_DERR_INTR_STS_VAL_MASK                            0x3FF

/* CPU_IF_NIC_DERR_INTR_CLR */
#define CPU_IF_NIC_DERR_INTR_CLR_VAL_SHIFT                           0
#define CPU_IF_NIC_DERR_INTR_CLR_VAL_MASK                            0x3FF

/* CPU_IF_NIC_DERR_INTR_MASK */
#define CPU_IF_NIC_DERR_INTR_MASK_VAL_SHIFT                          0
#define CPU_IF_NIC_DERR_INTR_MASK_VAL_MASK                           0x3FF

/* CPU_IF_DMA_IF_SERR_INTR_STS */
#define CPU_IF_DMA_IF_SERR_INTR_STS_VAL_SHIFT                        0
#define CPU_IF_DMA_IF_SERR_INTR_STS_VAL_MASK                         0xF

/* CPU_IF_DMA_IF_SERR_INTR_CLR */
#define CPU_IF_DMA_IF_SERR_INTR_CLR_VAL_SHIFT                        0
#define CPU_IF_DMA_IF_SERR_INTR_CLR_VAL_MASK                         0xF

/* CPU_IF_DMA_IF_SERR_INTR_MASK */
#define CPU_IF_DMA_IF_SERR_INTR_MASK_VAL_SHIFT                       0
#define CPU_IF_DMA_IF_SERR_INTR_MASK_VAL_MASK                        0xF

/* CPU_IF_DMA_IF_DERR_INTR_STS */
#define CPU_IF_DMA_IF_DERR_INTR_STS_VAL_SHIFT                        0
#define CPU_IF_DMA_IF_DERR_INTR_STS_VAL_MASK                         0xF

/* CPU_IF_DMA_IF_DERR_INTR_CLR */
#define CPU_IF_DMA_IF_DERR_INTR_CLR_VAL_SHIFT                        0
#define CPU_IF_DMA_IF_DERR_INTR_CLR_VAL_MASK                         0xF

/* CPU_IF_DMA_IF_DERR_INTR_MASK */
#define CPU_IF_DMA_IF_DERR_INTR_MASK_VAL_SHIFT                       0
#define CPU_IF_DMA_IF_DERR_INTR_MASK_VAL_MASK                        0xF

/* CPU_IF_HBM_SERR_INTR_STS */
#define CPU_IF_HBM_SERR_INTR_STS_VAL_SHIFT                           0
#define CPU_IF_HBM_SERR_INTR_STS_VAL_MASK                            0xF

/* CPU_IF_HBM_SERR_INTR_CLR */
#define CPU_IF_HBM_SERR_INTR_CLR_VAL_SHIFT                           0
#define CPU_IF_HBM_SERR_INTR_CLR_VAL_MASK                            0xF

/* CPU_IF_HBM_SERR_INTR_MASK */
#define CPU_IF_HBM_SERR_INTR_MASK_VAL_SHIFT                          0
#define CPU_IF_HBM_SERR_INTR_MASK_VAL_MASK                           0xF

/* CPU_IF_HBM_DERR_INTR_STS */
#define CPU_IF_HBM_DERR_INTR_STS_VAL_SHIFT                           0
#define CPU_IF_HBM_DERR_INTR_STS_VAL_MASK                            0xF

/* CPU_IF_HBM_DERR_INTR_CLR */
#define CPU_IF_HBM_DERR_INTR_CLR_VAL_SHIFT                           0
#define CPU_IF_HBM_DERR_INTR_CLR_VAL_MASK                            0xF

/* CPU_IF_HBM_DERR_INTR_MASK */
#define CPU_IF_HBM_DERR_INTR_MASK_VAL_SHIFT                          0
#define CPU_IF_HBM_DERR_INTR_MASK_VAL_MASK                           0xF

/* CPU_IF_PLL_SEI_INTR_STS */
#define CPU_IF_PLL_SEI_INTR_STS_VAL_SHIFT                            0
#define CPU_IF_PLL_SEI_INTR_STS_VAL_MASK                             0x3FFFF

/* CPU_IF_PLL_SEI_INTR_CLR */
#define CPU_IF_PLL_SEI_INTR_CLR_VAL_SHIFT                            0
#define CPU_IF_PLL_SEI_INTR_CLR_VAL_MASK                             0x3FFFF

/* CPU_IF_PLL_SEI_INTR_MASK */
#define CPU_IF_PLL_SEI_INTR_MASK_VAL_SHIFT                           0
#define CPU_IF_PLL_SEI_INTR_MASK_VAL_MASK                            0x3FFFF

/* CPU_IF_NIC_SEI_INTR_STS */
#define CPU_IF_NIC_SEI_INTR_STS_VAL_SHIFT                            0
#define CPU_IF_NIC_SEI_INTR_STS_VAL_MASK                             0x1F

/* CPU_IF_NIC_SEI_INTR_CLR */
#define CPU_IF_NIC_SEI_INTR_CLR_VAL_SHIFT                            0
#define CPU_IF_NIC_SEI_INTR_CLR_VAL_MASK                             0x1F

/* CPU_IF_NIC_SEI_INTR_MASK */
#define CPU_IF_NIC_SEI_INTR_MASK_VAL_SHIFT                           0
#define CPU_IF_NIC_SEI_INTR_MASK_VAL_MASK                            0x1F

/* CPU_IF_DMA_SEI_INTR_STS */
#define CPU_IF_DMA_SEI_INTR_STS_VAL_SHIFT                            0
#define CPU_IF_DMA_SEI_INTR_STS_VAL_MASK                             0xFF

/* CPU_IF_DMA_SEI_INTR_CLR */
#define CPU_IF_DMA_SEI_INTR_CLR_VAL_SHIFT                            0
#define CPU_IF_DMA_SEI_INTR_CLR_VAL_MASK                             0xFF

/* CPU_IF_DMA_SEI_INTR_MASK */
#define CPU_IF_DMA_SEI_INTR_MASK_VAL_SHIFT                           0
#define CPU_IF_DMA_SEI_INTR_MASK_VAL_MASK                            0xFF

/* CPU_IF_DMA_IF_SEI_INTR_STS */
#define CPU_IF_DMA_IF_SEI_INTR_STS_VAL_SHIFT                         0
#define CPU_IF_DMA_IF_SEI_INTR_STS_VAL_MASK                          0xF

/* CPU_IF_DMA_IF_SEI_INTR_CLR */
#define CPU_IF_DMA_IF_SEI_INTR_CLR_VAL_SHIFT                         0
#define CPU_IF_DMA_IF_SEI_INTR_CLR_VAL_MASK                          0xF

/* CPU_IF_DMA_IF_SEI_INTR_MASK */
#define CPU_IF_DMA_IF_SEI_INTR_MASK_VAL_SHIFT                        0
#define CPU_IF_DMA_IF_SEI_INTR_MASK_VAL_MASK                         0xF

#endif /* ASIC_REG_CPU_IF_MASKS_H_ */
