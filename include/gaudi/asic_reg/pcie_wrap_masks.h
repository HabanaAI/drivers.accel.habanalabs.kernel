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

#ifndef ASIC_REG_PCIE_WRAP_MASKS_H_
#define ASIC_REG_PCIE_WRAP_MASKS_H_

/*
 *****************************************
 *   PCIE_WRAP (Prototype: PCIE_WRAP)
 *****************************************
 */

/* PCIE_WRAP_RR_ELBI_WR_PRV_REG_CTRL */
#define PCIE_WRAP_RR_ELBI_WR_PRV_REG_CTRL_HIT_BLOCK_SHIFT            0
#define PCIE_WRAP_RR_ELBI_WR_PRV_REG_CTRL_HIT_BLOCK_MASK             0xFFFFFFFF

/* PCIE_WRAP_RR_ELBI_WR_SEC_REG_CTRL */
#define PCIE_WRAP_RR_ELBI_WR_SEC_REG_CTRL_HIT_BLOCK_SHIFT            0
#define PCIE_WRAP_RR_ELBI_WR_SEC_REG_CTRL_HIT_BLOCK_MASK             0xFFFFFFFF

/* PCIE_WRAP_RR_ELBI_RD_PRV_REG_CTRL */
#define PCIE_WRAP_RR_ELBI_RD_PRV_REG_CTRL_HIT_BLOCK_SHIFT            0
#define PCIE_WRAP_RR_ELBI_RD_PRV_REG_CTRL_HIT_BLOCK_MASK             0xFFFFFFFF

/* PCIE_WRAP_RR_ELBI_RD_SEC_REG_CTRL */
#define PCIE_WRAP_RR_ELBI_RD_SEC_REG_CTRL_HIT_BLOCK_SHIFT            0
#define PCIE_WRAP_RR_ELBI_RD_SEC_REG_CTRL_HIT_BLOCK_MASK             0xFFFFFFFF

/* PCIE_WRAP_PHY_FW_FSM_SIZE */
#define PCIE_WRAP_PHY_FW_FSM_SIZE_SIZE_SHIFT                         0
#define PCIE_WRAP_PHY_FW_FSM_SIZE_SIZE_MASK                          0xFFFFFFFF

/* PCIE_WRAP_LBW_GW_ADDR */
#define PCIE_WRAP_LBW_GW_ADDR_ADDR_SHIFT                             0
#define PCIE_WRAP_LBW_GW_ADDR_ADDR_MASK                              0xFFFFFFFF

/* PCIE_WRAP_LBW_GW_DATA */
#define PCIE_WRAP_LBW_GW_DATA_DATA_SHIFT                             0
#define PCIE_WRAP_LBW_GW_DATA_DATA_MASK                              0xFFFFFFFF

/* PCIE_WRAP_LBW_GW_GO */
#define PCIE_WRAP_LBW_GW_GO_GO_SHIFT                                 0
#define PCIE_WRAP_LBW_GW_GO_GO_MASK                                  0x1

/* PCIE_WRAP_LBW_GW_STATUS */
#define PCIE_WRAP_LBW_GW_STATUS_STATUS_SHIFT                         0
#define PCIE_WRAP_LBW_GW_STATUS_STATUS_MASK                          0x1

/* PCIE_WRAP_OUTSTAND_TRANS */
#define PCIE_WRAP_OUTSTAND_TRANS_LBW_RD_SHIFT                        0
#define PCIE_WRAP_OUTSTAND_TRANS_LBW_RD_MASK                         0x1
#define PCIE_WRAP_OUTSTAND_TRANS_LBW_WR_SHIFT                        1
#define PCIE_WRAP_OUTSTAND_TRANS_LBW_WR_MASK                         0x2
#define PCIE_WRAP_OUTSTAND_TRANS_HBW_RD_SHIFT                        4
#define PCIE_WRAP_OUTSTAND_TRANS_HBW_RD_MASK                         0x10
#define PCIE_WRAP_OUTSTAND_TRANS_HBW_WR_SHIFT                        5
#define PCIE_WRAP_OUTSTAND_TRANS_HBW_WR_MASK                         0x20

/* PCIE_WRAP_MASK_REQ */
#define PCIE_WRAP_MASK_REQ_LBW_SHIFT                                 0
#define PCIE_WRAP_MASK_REQ_LBW_MASK                                  0x1
#define PCIE_WRAP_MASK_REQ_HBW_SHIFT                                 4
#define PCIE_WRAP_MASK_REQ_HBW_MASK                                  0x10

/* PCIE_WRAP_IND_AWADDR_L */
#define PCIE_WRAP_IND_AWADDR_L_VAL_SHIFT                             0
#define PCIE_WRAP_IND_AWADDR_L_VAL_MASK                              0xFFFFFFFF

/* PCIE_WRAP_IND_AWADDR_H */
#define PCIE_WRAP_IND_AWADDR_H_VAL_SHIFT                             0
#define PCIE_WRAP_IND_AWADDR_H_VAL_MASK                              0xFFFFFFFF

/* PCIE_WRAP_IND_AWLEN */
#define PCIE_WRAP_IND_AWLEN_VAL_SHIFT                                0
#define PCIE_WRAP_IND_AWLEN_VAL_MASK                                 0xFF

/* PCIE_WRAP_IND_AWSIZE */
#define PCIE_WRAP_IND_AWSIZE_VAL_SHIFT                               0
#define PCIE_WRAP_IND_AWSIZE_VAL_MASK                                0x7

/* PCIE_WRAP_IND_AWBURST */
#define PCIE_WRAP_IND_AWBURST_VAL_SHIFT                              0
#define PCIE_WRAP_IND_AWBURST_VAL_MASK                               0x3

/* PCIE_WRAP_IND_AWLOCK */
#define PCIE_WRAP_IND_AWLOCK_VAL_SHIFT                               0
#define PCIE_WRAP_IND_AWLOCK_VAL_MASK                                0x1

/* PCIE_WRAP_IND_AWCACHE */
#define PCIE_WRAP_IND_AWCACHE_VAL_SHIFT                              0
#define PCIE_WRAP_IND_AWCACHE_VAL_MASK                               0xF

/* PCIE_WRAP_IND_AWPROT */
#define PCIE_WRAP_IND_AWPROT_VAL_SHIFT                               0
#define PCIE_WRAP_IND_AWPROT_VAL_MASK                                0x7

/* PCIE_WRAP_IND_AWVALID */
#define PCIE_WRAP_IND_AWVALID_IND_SHIFT                              0
#define PCIE_WRAP_IND_AWVALID_IND_MASK                               0x1

/* PCIE_WRAP_IND_WDATA */
#define PCIE_WRAP_IND_WDATA_VAL_SHIFT                                0
#define PCIE_WRAP_IND_WDATA_VAL_MASK                                 0xFFFFFFFF

/* PCIE_WRAP_IND_WSTRB */
#define PCIE_WRAP_IND_WSTRB_VAL_SHIFT                                0
#define PCIE_WRAP_IND_WSTRB_VAL_MASK                                 0xFFFF

/* PCIE_WRAP_IND_WLAST */
#define PCIE_WRAP_IND_WLAST_VAL_SHIFT                                0
#define PCIE_WRAP_IND_WLAST_VAL_MASK                                 0x1

/* PCIE_WRAP_IND_WVALID */
#define PCIE_WRAP_IND_WVALID_IND_SHIFT                               0
#define PCIE_WRAP_IND_WVALID_IND_MASK                                0x1

/* PCIE_WRAP_IND_BRESP */
#define PCIE_WRAP_IND_BRESP_VAL_SHIFT                                0
#define PCIE_WRAP_IND_BRESP_VAL_MASK                                 0x3

/* PCIE_WRAP_IND_BVALID */
#define PCIE_WRAP_IND_BVALID_IND_SHIFT                               0
#define PCIE_WRAP_IND_BVALID_IND_MASK                                0x1

/* PCIE_WRAP_IND_ARADDR */
#define PCIE_WRAP_IND_ARADDR_VAL_SHIFT                               0
#define PCIE_WRAP_IND_ARADDR_VAL_MASK                                0xFFFFFFFF

/* PCIE_WRAP_IND_ARLEN */
#define PCIE_WRAP_IND_ARLEN_VAL_SHIFT                                0
#define PCIE_WRAP_IND_ARLEN_VAL_MASK                                 0xFF

/* PCIE_WRAP_IND_ARSIZE */
#define PCIE_WRAP_IND_ARSIZE_VAL_SHIFT                               0
#define PCIE_WRAP_IND_ARSIZE_VAL_MASK                                0x7

/* PCIE_WRAP_IND_ARBURST */
#define PCIE_WRAP_IND_ARBURST_VAL_SHIFT                              0
#define PCIE_WRAP_IND_ARBURST_VAL_MASK                               0x3

/* PCIE_WRAP_IND_ARLOCK */
#define PCIE_WRAP_IND_ARLOCK_VAL_SHIFT                               0
#define PCIE_WRAP_IND_ARLOCK_VAL_MASK                                0x1

/* PCIE_WRAP_IND_ARCACHE */
#define PCIE_WRAP_IND_ARCACHE_VAL_SHIFT                              0
#define PCIE_WRAP_IND_ARCACHE_VAL_MASK                               0xF

/* PCIE_WRAP_IND_ARPROT */
#define PCIE_WRAP_IND_ARPROT_VAL_SHIFT                               0
#define PCIE_WRAP_IND_ARPROT_VAL_MASK                                0x7

/* PCIE_WRAP_IND_ARVALID */
#define PCIE_WRAP_IND_ARVALID_IND_SHIFT                              0
#define PCIE_WRAP_IND_ARVALID_IND_MASK                               0x1

/* PCIE_WRAP_IND_RDATA */
#define PCIE_WRAP_IND_RDATA_VAL_SHIFT                                0
#define PCIE_WRAP_IND_RDATA_VAL_MASK                                 0xFFFFFFFF

/* PCIE_WRAP_IND_RLAST */
#define PCIE_WRAP_IND_RLAST_VAL_SHIFT                                0
#define PCIE_WRAP_IND_RLAST_VAL_MASK                                 0x1

/* PCIE_WRAP_IND_RRESP */
#define PCIE_WRAP_IND_RRESP_VAL_SHIFT                                0
#define PCIE_WRAP_IND_RRESP_VAL_MASK                                 0x3

/* PCIE_WRAP_IND_RVALID */
#define PCIE_WRAP_IND_RVALID_IND_SHIFT                               0
#define PCIE_WRAP_IND_RVALID_IND_MASK                                0x1

/* PCIE_WRAP_IND_AWMISC_INFO */
#define PCIE_WRAP_IND_AWMISC_INFO_VAL_SHIFT                          0
#define PCIE_WRAP_IND_AWMISC_INFO_VAL_MASK                           0x3FFFFF

/* PCIE_WRAP_IND_AWMISC_INFO_HDR_34DW */
#define PCIE_WRAP_IND_AWMISC_INFO_HDR_34DW_VAL_SHIFT                 0
#define PCIE_WRAP_IND_AWMISC_INFO_HDR_34DW_VAL_MASK                  0xFFFFFFFF

/* PCIE_WRAP_IND_AWMISC_INFO_P_TAG */
#define PCIE_WRAP_IND_AWMISC_INFO_P_TAG_VAL_SHIFT                    0
#define PCIE_WRAP_IND_AWMISC_INFO_P_TAG_VAL_MASK                     0x3FF

/* PCIE_WRAP_IND_AWMISC_INFO_ATU_BYPAS */
#define PCIE_WRAP_IND_AWMISC_INFO_ATU_BYPAS_VAL_SHIFT                0
#define PCIE_WRAP_IND_AWMISC_INFO_ATU_BYPAS_VAL_MASK                 0x1

/* PCIE_WRAP_IND_AWMISC_INFO_FUNC_NUM */
#define PCIE_WRAP_IND_AWMISC_INFO_FUNC_NUM_VAL_SHIFT                 0
#define PCIE_WRAP_IND_AWMISC_INFO_FUNC_NUM_VAL_MASK                  0x1

/* PCIE_WRAP_IND_AWMISC_INFO_VFUNC_ACT */
#define PCIE_WRAP_IND_AWMISC_INFO_VFUNC_ACT_VAL_SHIFT                0
#define PCIE_WRAP_IND_AWMISC_INFO_VFUNC_ACT_VAL_MASK                 0x1

/* PCIE_WRAP_IND_AWMISC_INFO_VFUNC_NUM */
#define PCIE_WRAP_IND_AWMISC_INFO_VFUNC_NUM_VAL_SHIFT                0
#define PCIE_WRAP_IND_AWMISC_INFO_VFUNC_NUM_VAL_MASK                 0x7F

/* PCIE_WRAP_IND_AWMISC_INFO_TLPPRFX */
#define PCIE_WRAP_IND_AWMISC_INFO_TLPPRFX_VAL_SHIFT                  0
#define PCIE_WRAP_IND_AWMISC_INFO_TLPPRFX_VAL_MASK                   0xFFFFFFFF

/* PCIE_WRAP_IND_ARMISC_INFO */
#define PCIE_WRAP_IND_ARMISC_INFO_VAL_SHIFT                          0
#define PCIE_WRAP_IND_ARMISC_INFO_VAL_MASK                           0x3FFFFF

/* PCIE_WRAP_IND_ARMISC_INFO_TLPPRFX */
#define PCIE_WRAP_IND_ARMISC_INFO_TLPPRFX_VAL_SHIFT                  0
#define PCIE_WRAP_IND_ARMISC_INFO_TLPPRFX_VAL_MASK                   0xFFFFFFFF

/* PCIE_WRAP_IND_ARMISC_INFO_ATU_BYP */
#define PCIE_WRAP_IND_ARMISC_INFO_ATU_BYP_VAL_SHIFT                  0
#define PCIE_WRAP_IND_ARMISC_INFO_ATU_BYP_VAL_MASK                   0x1

/* PCIE_WRAP_IND_ARMISC_INFO_FUNC_NUM */
#define PCIE_WRAP_IND_ARMISC_INFO_FUNC_NUM_VAL_SHIFT                 0
#define PCIE_WRAP_IND_ARMISC_INFO_FUNC_NUM_VAL_MASK                  0x1

/* PCIE_WRAP_IND_ARMISC_INFO_VFUNC_ACT */
#define PCIE_WRAP_IND_ARMISC_INFO_VFUNC_ACT_VAL_SHIFT                0
#define PCIE_WRAP_IND_ARMISC_INFO_VFUNC_ACT_VAL_MASK                 0x1

/* PCIE_WRAP_IND_ARMISC_INFO_VFUNC_NUM */
#define PCIE_WRAP_IND_ARMISC_INFO_VFUNC_NUM_VAL_SHIFT                0
#define PCIE_WRAP_IND_ARMISC_INFO_VFUNC_NUM_VAL_MASK                 0x7F

/* PCIE_WRAP_RR_WR_PRV_RANGE_REG_CTRL */
#define PCIE_WRAP_RR_WR_PRV_RANGE_REG_CTRL_HIT_BLOCK_SHIFT           0
#define PCIE_WRAP_RR_WR_PRV_RANGE_REG_CTRL_HIT_BLOCK_MASK            0xFFFFFFFF

/* PCIE_WRAP_RR_WR_PRV_RANGE_REG_MIN */
#define PCIE_WRAP_RR_WR_PRV_RANGE_REG_MIN_MIN_SHIFT                  0
#define PCIE_WRAP_RR_WR_PRV_RANGE_REG_MIN_MIN_MASK                   0xFFFFFFFF

/* PCIE_WRAP_RR_WR_PRV_RANGE_REG_MAX */
#define PCIE_WRAP_RR_WR_PRV_RANGE_REG_MAX_MAX_SHIFT                  0
#define PCIE_WRAP_RR_WR_PRV_RANGE_REG_MAX_MAX_MASK                   0xFFFFFFFF

/* PCIE_WRAP_RR_WR_SEC_RANGE_REG_CTRL */
#define PCIE_WRAP_RR_WR_SEC_RANGE_REG_CTRL_HIT_BLOCK_SHIFT           0
#define PCIE_WRAP_RR_WR_SEC_RANGE_REG_CTRL_HIT_BLOCK_MASK            0xFFFFFFFF

/* PCIE_WRAP_RR_WR_SEC_RANGE_REG_MIN */
#define PCIE_WRAP_RR_WR_SEC_RANGE_REG_MIN_MIN_SHIFT                  0
#define PCIE_WRAP_RR_WR_SEC_RANGE_REG_MIN_MIN_MASK                   0xFFFFFFFF

/* PCIE_WRAP_RR_WR_SEC_RANGE_REG_MAX */
#define PCIE_WRAP_RR_WR_SEC_RANGE_REG_MAX_MAX_SHIFT                  0
#define PCIE_WRAP_RR_WR_SEC_RANGE_REG_MAX_MAX_MASK                   0xFFFFFFFF

/* PCIE_WRAP_SLV_AWMISC_INFO */
#define PCIE_WRAP_SLV_AWMISC_INFO_VAL_SHIFT                          0
#define PCIE_WRAP_SLV_AWMISC_INFO_VAL_MASK                           0x3FFFFF

/* PCIE_WRAP_SLV_AWMISC_INFO_HDR_34DW */
#define PCIE_WRAP_SLV_AWMISC_INFO_HDR_34DW_VAL_SHIFT                 0
#define PCIE_WRAP_SLV_AWMISC_INFO_HDR_34DW_VAL_MASK                  0xFFFFFFFF

/* PCIE_WRAP_SLV_AWMISC_INFO_P_TAG */
#define PCIE_WRAP_SLV_AWMISC_INFO_P_TAG_VAL_SHIFT                    0
#define PCIE_WRAP_SLV_AWMISC_INFO_P_TAG_VAL_MASK                     0x3FF

/* PCIE_WRAP_SLV_AWMISC_INFO_ATU_BYPAS */
#define PCIE_WRAP_SLV_AWMISC_INFO_ATU_BYPAS_VAL_SHIFT                0
#define PCIE_WRAP_SLV_AWMISC_INFO_ATU_BYPAS_VAL_MASK                 0x1

/* PCIE_WRAP_SLV_AWMISC_INFO_FUNC_NUM */
#define PCIE_WRAP_SLV_AWMISC_INFO_FUNC_NUM_VAL_SHIFT                 0
#define PCIE_WRAP_SLV_AWMISC_INFO_FUNC_NUM_VAL_MASK                  0x1

/* PCIE_WRAP_SLV_AWMISC_INFO_VFUNC_ACT */
#define PCIE_WRAP_SLV_AWMISC_INFO_VFUNC_ACT_VAL_SHIFT                0
#define PCIE_WRAP_SLV_AWMISC_INFO_VFUNC_ACT_VAL_MASK                 0x1

/* PCIE_WRAP_SLV_AWMISC_INFO_VFUNC_NUM */
#define PCIE_WRAP_SLV_AWMISC_INFO_VFUNC_NUM_VAL_SHIFT                0
#define PCIE_WRAP_SLV_AWMISC_INFO_VFUNC_NUM_VAL_MASK                 0x7F

/* PCIE_WRAP_SLV_AWMISC_INFO_TLPPRFX */
#define PCIE_WRAP_SLV_AWMISC_INFO_TLPPRFX_VAL_SHIFT                  0
#define PCIE_WRAP_SLV_AWMISC_INFO_TLPPRFX_VAL_MASK                   0xFFFFFFFF

/* PCIE_WRAP_SLV_ARMISC_INFO */
#define PCIE_WRAP_SLV_ARMISC_INFO_VAL_SHIFT                          0
#define PCIE_WRAP_SLV_ARMISC_INFO_VAL_MASK                           0x3FFFFF

/* PCIE_WRAP_SLV_ARMISC_INFO_TLPPRFX */
#define PCIE_WRAP_SLV_ARMISC_INFO_TLPPRFX_VAL_SHIFT                  0
#define PCIE_WRAP_SLV_ARMISC_INFO_TLPPRFX_VAL_MASK                   0xFFFFFFFF

/* PCIE_WRAP_SLV_ARMISC_INFO_ATU_BYP */
#define PCIE_WRAP_SLV_ARMISC_INFO_ATU_BYP_VAL_SHIFT                  0
#define PCIE_WRAP_SLV_ARMISC_INFO_ATU_BYP_VAL_MASK                   0x1

/* PCIE_WRAP_SLV_ARMISC_INFO_FUNC_NUM */
#define PCIE_WRAP_SLV_ARMISC_INFO_FUNC_NUM_VAL_SHIFT                 0
#define PCIE_WRAP_SLV_ARMISC_INFO_FUNC_NUM_VAL_MASK                  0x1

/* PCIE_WRAP_SLV_ARMISC_INFO_VFUNC_ACT */
#define PCIE_WRAP_SLV_ARMISC_INFO_VFUNC_ACT_IND_SHIFT                0
#define PCIE_WRAP_SLV_ARMISC_INFO_VFUNC_ACT_IND_MASK                 0x1

/* PCIE_WRAP_SLV_ARMISC_INFO_VFUNC_NUM */
#define PCIE_WRAP_SLV_ARMISC_INFO_VFUNC_NUM_VAL_SHIFT                0
#define PCIE_WRAP_SLV_ARMISC_INFO_VFUNC_NUM_VAL_MASK                 0x7F

/* PCIE_WRAP_RR_RD_PRV_RANGE_REG_CTRL */
#define PCIE_WRAP_RR_RD_PRV_RANGE_REG_CTRL_HIT_BLOCK_SHIFT           0
#define PCIE_WRAP_RR_RD_PRV_RANGE_REG_CTRL_HIT_BLOCK_MASK            0xFFFFFFFF

/* PCIE_WRAP_RR_RD_PRV_RANGE_REG_MIN */
#define PCIE_WRAP_RR_RD_PRV_RANGE_REG_MIN_MIN_SHIFT                  0
#define PCIE_WRAP_RR_RD_PRV_RANGE_REG_MIN_MIN_MASK                   0xFFFFFFFF

/* PCIE_WRAP_RR_RD_PRV_RANGE_REG_MAX */
#define PCIE_WRAP_RR_RD_PRV_RANGE_REG_MAX_MAX_SHIFT                  0
#define PCIE_WRAP_RR_RD_PRV_RANGE_REG_MAX_MAX_MASK                   0xFFFFFFFF

/* PCIE_WRAP_RR_RD_SEC_RANGE_REG_CTRL */
#define PCIE_WRAP_RR_RD_SEC_RANGE_REG_CTRL_HIT_BLOCK_SHIFT           0
#define PCIE_WRAP_RR_RD_SEC_RANGE_REG_CTRL_HIT_BLOCK_MASK            0xFFFFFFFF

/* PCIE_WRAP_RR_RD_SEC_RANGE_REG_MIN */
#define PCIE_WRAP_RR_RD_SEC_RANGE_REG_MIN_MIN_SHIFT                  0
#define PCIE_WRAP_RR_RD_SEC_RANGE_REG_MIN_MIN_MASK                   0xFFFFFFFF

/* PCIE_WRAP_RR_RD_SEC_RANGE_REG_MAX */
#define PCIE_WRAP_RR_RD_SEC_RANGE_REG_MAX_MAX_SHIFT                  0
#define PCIE_WRAP_RR_RD_SEC_RANGE_REG_MAX_MAX_MASK                   0xFFFFFFFF

/* PCIE_WRAP_MMU_BYPASS_DMA */
#define PCIE_WRAP_MMU_BYPASS_DMA_IND_SHIFT                           0
#define PCIE_WRAP_MMU_BYPASS_DMA_IND_MASK                            0x1

/* PCIE_WRAP_MMU_BYPASS_NON_DMA */
#define PCIE_WRAP_MMU_BYPASS_NON_DMA_IND_SHIFT                       0
#define PCIE_WRAP_MMU_BYPASS_NON_DMA_IND_MASK                        0x1

/* PCIE_WRAP_ASID_NON_DMA */
#define PCIE_WRAP_ASID_NON_DMA_VAL_SHIFT                             0
#define PCIE_WRAP_ASID_NON_DMA_VAL_MASK                              0x3FF

/* PCIE_WRAP_ASID_DMA */
#define PCIE_WRAP_ASID_DMA_VAL_SHIFT                                 0
#define PCIE_WRAP_ASID_DMA_VAL_MASK                                  0x3FF

/* PCIE_WRAP_CPU_HOT_RST */
#define PCIE_WRAP_CPU_HOT_RST_CTRL_SHIFT                             0
#define PCIE_WRAP_CPU_HOT_RST_CTRL_MASK                              0x1

/* PCIE_WRAP_AXI_PROT_OVR */
#define PCIE_WRAP_AXI_PROT_OVR_IND_SHIFT                             0
#define PCIE_WRAP_AXI_PROT_OVR_IND_MASK                              0x1

/* PCIE_WRAP_CACHE_OVR */
#define PCIE_WRAP_CACHE_OVR_READ_SHIFT                               0
#define PCIE_WRAP_CACHE_OVR_READ_MASK                                0xF
#define PCIE_WRAP_CACHE_OVR_WRITE_SHIFT                              4
#define PCIE_WRAP_CACHE_OVR_WRITE_MASK                               0xF0
#define PCIE_WRAP_CACHE_OVR_RD_EN_SHIFT                              8
#define PCIE_WRAP_CACHE_OVR_RD_EN_MASK                               0xF00
#define PCIE_WRAP_CACHE_OVR_WR_EN_SHIFT                              12
#define PCIE_WRAP_CACHE_OVR_WR_EN_MASK                               0xF000

/* PCIE_WRAP_LOCK_OVR */
#define PCIE_WRAP_LOCK_OVR_READ_SHIFT                                0
#define PCIE_WRAP_LOCK_OVR_READ_MASK                                 0x1
#define PCIE_WRAP_LOCK_OVR_WRITE_SHIFT                               4
#define PCIE_WRAP_LOCK_OVR_WRITE_MASK                                0x10
#define PCIE_WRAP_LOCK_OVR_RD_EN_SHIFT                               8
#define PCIE_WRAP_LOCK_OVR_RD_EN_MASK                                0x100
#define PCIE_WRAP_LOCK_OVR_WR_EN_SHIFT                               12
#define PCIE_WRAP_LOCK_OVR_WR_EN_MASK                                0x1000

/* PCIE_WRAP_PROT_OVR */
#define PCIE_WRAP_PROT_OVR_READ_SHIFT                                0
#define PCIE_WRAP_PROT_OVR_READ_MASK                                 0x7
#define PCIE_WRAP_PROT_OVR_WRITE_SHIFT                               4
#define PCIE_WRAP_PROT_OVR_WRITE_MASK                                0x70
#define PCIE_WRAP_PROT_OVR_RD_EN_SHIFT                               8
#define PCIE_WRAP_PROT_OVR_RD_EN_MASK                                0x700
#define PCIE_WRAP_PROT_OVR_WR_EN_SHIFT                               12
#define PCIE_WRAP_PROT_OVR_WR_EN_MASK                                0x7000

/* PCIE_WRAP_ARUSER_OVR */
#define PCIE_WRAP_ARUSER_OVR_VAL_SHIFT                               0
#define PCIE_WRAP_ARUSER_OVR_VAL_MASK                                0xFFFFFFFF

/* PCIE_WRAP_AWUSER_OVR */
#define PCIE_WRAP_AWUSER_OVR_VAL_SHIFT                               0
#define PCIE_WRAP_AWUSER_OVR_VAL_MASK                                0xFFFFFFFF

/* PCIE_WRAP_ARUSER_OVR_EN */
#define PCIE_WRAP_ARUSER_OVR_EN_VAL_SHIFT                            0
#define PCIE_WRAP_ARUSER_OVR_EN_VAL_MASK                             0xFFFFFFFF

/* PCIE_WRAP_AWUSER_OVR_EN */
#define PCIE_WRAP_AWUSER_OVR_EN_VAL_SHIFT                            0
#define PCIE_WRAP_AWUSER_OVR_EN_VAL_MASK                             0xFFFFFFFF

/* PCIE_WRAP_MAX_OUTSTAND */
#define PCIE_WRAP_MAX_OUTSTAND_READ_SHIFT                            0
#define PCIE_WRAP_MAX_OUTSTAND_READ_MASK                             0xFF
#define PCIE_WRAP_MAX_OUTSTAND_WRITE_SHIFT                           8
#define PCIE_WRAP_MAX_OUTSTAND_WRITE_MASK                            0xFF00

/* PCIE_WRAP_MST_IN */
#define PCIE_WRAP_MST_IN_AR_SHIFT                                    0
#define PCIE_WRAP_MST_IN_AR_MASK                                     0x3
#define PCIE_WRAP_MST_IN_AW_SHIFT                                    4
#define PCIE_WRAP_MST_IN_AW_MASK                                     0x30

/* PCIE_WRAP_RSP_OK */
#define PCIE_WRAP_RSP_OK_IND_SHIFT                                   0
#define PCIE_WRAP_RSP_OK_IND_MASK                                    0x1

/* PCIE_WRAP_LBW_CACHE_OVR */
#define PCIE_WRAP_LBW_CACHE_OVR_READ_SHIFT                           0
#define PCIE_WRAP_LBW_CACHE_OVR_READ_MASK                            0xF
#define PCIE_WRAP_LBW_CACHE_OVR_WRITE_SHIFT                          4
#define PCIE_WRAP_LBW_CACHE_OVR_WRITE_MASK                           0xF0
#define PCIE_WRAP_LBW_CACHE_OVR_RD_EN_SHIFT                          8
#define PCIE_WRAP_LBW_CACHE_OVR_RD_EN_MASK                           0xF00
#define PCIE_WRAP_LBW_CACHE_OVR_WR_EN_SHIFT                          12
#define PCIE_WRAP_LBW_CACHE_OVR_WR_EN_MASK                           0xF000

/* PCIE_WRAP_LBW_LOCK_OVR */
#define PCIE_WRAP_LBW_LOCK_OVR_READ_SHIFT                            0
#define PCIE_WRAP_LBW_LOCK_OVR_READ_MASK                             0x1
#define PCIE_WRAP_LBW_LOCK_OVR_WRITE_SHIFT                           4
#define PCIE_WRAP_LBW_LOCK_OVR_WRITE_MASK                            0x10
#define PCIE_WRAP_LBW_LOCK_OVR_RD_EN_SHIFT                           8
#define PCIE_WRAP_LBW_LOCK_OVR_RD_EN_MASK                            0x100
#define PCIE_WRAP_LBW_LOCK_OVR_WR_EN_SHIFT                           12
#define PCIE_WRAP_LBW_LOCK_OVR_WR_EN_MASK                            0x1000

/* PCIE_WRAP_LBW_PROT_OVR */
#define PCIE_WRAP_LBW_PROT_OVR_READ_SHIFT                            0
#define PCIE_WRAP_LBW_PROT_OVR_READ_MASK                             0x7
#define PCIE_WRAP_LBW_PROT_OVR_WRITE_SHIFT                           4
#define PCIE_WRAP_LBW_PROT_OVR_WRITE_MASK                            0x70
#define PCIE_WRAP_LBW_PROT_OVR_RD_EN_SHIFT                           8
#define PCIE_WRAP_LBW_PROT_OVR_RD_EN_MASK                            0x700
#define PCIE_WRAP_LBW_PROT_OVR_WR_EN_SHIFT                           12
#define PCIE_WRAP_LBW_PROT_OVR_WR_EN_MASK                            0x7000

/* PCIE_WRAP_LBW_ARUSER_OVR */
#define PCIE_WRAP_LBW_ARUSER_OVR_VAL_SHIFT                           0
#define PCIE_WRAP_LBW_ARUSER_OVR_VAL_MASK                            0xFFFFFFFF

/* PCIE_WRAP_LBW_AWUSER_OVR */
#define PCIE_WRAP_LBW_AWUSER_OVR_VAL_SHIFT                           0
#define PCIE_WRAP_LBW_AWUSER_OVR_VAL_MASK                            0xFFFFFFFF

/* PCIE_WRAP_LBW_ARUSER_OVR_EN */
#define PCIE_WRAP_LBW_ARUSER_OVR_EN_VAL_SHIFT                        0
#define PCIE_WRAP_LBW_ARUSER_OVR_EN_VAL_MASK                         0xFFFFFFFF

/* PCIE_WRAP_LBW_AWUSER_OVR_EN */
#define PCIE_WRAP_LBW_AWUSER_OVR_EN_VAL_SHIFT                        0
#define PCIE_WRAP_LBW_AWUSER_OVR_EN_VAL_MASK                         0xFFFFFFFF

/* PCIE_WRAP_LBW_MAX_OUTSTAND */
#define PCIE_WRAP_LBW_MAX_OUTSTAND_READ_SHIFT                        0
#define PCIE_WRAP_LBW_MAX_OUTSTAND_READ_MASK                         0xFF
#define PCIE_WRAP_LBW_MAX_OUTSTAND_WRITE_SHIFT                       8
#define PCIE_WRAP_LBW_MAX_OUTSTAND_WRITE_MASK                        0xFF00

/* PCIE_WRAP_LBW_MST_IN */
#define PCIE_WRAP_LBW_MST_IN_AR_SHIFT                                0
#define PCIE_WRAP_LBW_MST_IN_AR_MASK                                 0x3
#define PCIE_WRAP_LBW_MST_IN_AW_SHIFT                                4
#define PCIE_WRAP_LBW_MST_IN_AW_MASK                                 0x30

/* PCIE_WRAP_LBW_RSP_OK */
#define PCIE_WRAP_LBW_RSP_OK_IND_SHIFT                               0
#define PCIE_WRAP_LBW_RSP_OK_IND_MASK                                0x1

/* PCIE_WRAP_AXI_SPLIT_INTR */
#define PCIE_WRAP_AXI_SPLIT_INTR_IND_SHIFT                           0
#define PCIE_WRAP_AXI_SPLIT_INTR_IND_MASK                            0x1
#define PCIE_WRAP_AXI_SPLIT_INTR_CLR_SHIFT                           1
#define PCIE_WRAP_AXI_SPLIT_INTR_CLR_MASK                            0x2

/* PCIE_WRAP_PCIE_AWUSER */
#define PCIE_WRAP_PCIE_AWUSER_VAL_SHIFT                              0
#define PCIE_WRAP_PCIE_AWUSER_VAL_MASK                               0xFFFFFFFF

/* PCIE_WRAP_PCIE_ARUSER */
#define PCIE_WRAP_PCIE_ARUSER_VAL_SHIFT                              0
#define PCIE_WRAP_PCIE_ARUSER_VAL_MASK                               0xFFFFFFFF

/* PCIE_WRAP_REDUCTION_AWUSER */
#define PCIE_WRAP_REDUCTION_AWUSER_VAL_SHIFT                         0
#define PCIE_WRAP_REDUCTION_AWUSER_VAL_MASK                          0xFFFFFFFF

/* PCIE_WRAP_REDUCTION_ARUSER */
#define PCIE_WRAP_REDUCTION_ARUSER_VAL_SHIFT                         0
#define PCIE_WRAP_REDUCTION_ARUSER_VAL_MASK                          0xFFFFFFFF

/* PCIE_WRAP_PSOC2PCI_AWUSER */
#define PCIE_WRAP_PSOC2PCI_AWUSER_VAL_SHIFT                          0
#define PCIE_WRAP_PSOC2PCI_AWUSER_VAL_MASK                           0xFFFFFFFF

/* PCIE_WRAP_PSOC2PCI_ARUSER */
#define PCIE_WRAP_PSOC2PCI_ARUSER_VAL_SHIFT                          0
#define PCIE_WRAP_PSOC2PCI_ARUSER_VAL_MASK                           0xFFFFFFFF

/* PCIE_WRAP_HBW_DRAIN_TIMEOUT */
#define PCIE_WRAP_HBW_DRAIN_TIMEOUT_VAL_SHIFT                        0
#define PCIE_WRAP_HBW_DRAIN_TIMEOUT_VAL_MASK                         0xFFFFFFFF

/* PCIE_WRAP_HBW_DRAIN_CFG */
#define PCIE_WRAP_HBW_DRAIN_CFG_EN_SHIFT                             0
#define PCIE_WRAP_HBW_DRAIN_CFG_EN_MASK                              0x1
#define PCIE_WRAP_HBW_DRAIN_CFG_RSP_SHIFT                            4
#define PCIE_WRAP_HBW_DRAIN_CFG_RSP_MASK                             0x30

/* PCIE_WRAP_LBW_DRAIN_TIMEOUT */
#define PCIE_WRAP_LBW_DRAIN_TIMEOUT_VAL_SHIFT                        0
#define PCIE_WRAP_LBW_DRAIN_TIMEOUT_VAL_MASK                         0xFFFFFFFF

/* PCIE_WRAP_LBW_DRAIN_CFG */
#define PCIE_WRAP_LBW_DRAIN_CFG_EN_SHIFT                             0
#define PCIE_WRAP_LBW_DRAIN_CFG_EN_MASK                              0x1
#define PCIE_WRAP_LBW_DRAIN_CFG_RSP_SHIFT                            4
#define PCIE_WRAP_LBW_DRAIN_CFG_RSP_MASK                             0x30

/* PCIE_WRAP_ONE_IN_FLIGHT */
#define PCIE_WRAP_ONE_IN_FLIGHT_LBW_BLOCK_HBW_EN_SHIFT               0
#define PCIE_WRAP_ONE_IN_FLIGHT_LBW_BLOCK_HBW_EN_MASK                0x1
#define PCIE_WRAP_ONE_IN_FLIGHT_HBW_BLOCK_LBW_EN_SHIFT               1
#define PCIE_WRAP_ONE_IN_FLIGHT_HBW_BLOCK_LBW_EN_MASK                0x2

/* PCIE_WRAP_PHY_FW_FSM */
#define PCIE_WRAP_PHY_FW_FSM_EN_SHIFT                                0
#define PCIE_WRAP_PHY_FW_FSM_EN_MASK                                 0x1
#define PCIE_WRAP_PHY_FW_FSM_FW_READY_SHIFT                          1
#define PCIE_WRAP_PHY_FW_FSM_FW_READY_MASK                           0x2
#define PCIE_WRAP_PHY_FW_FSM_BYPASS_POLLING_SHIFT                    2
#define PCIE_WRAP_PHY_FW_FSM_BYPASS_POLLING_MASK                     0x4
#define PCIE_WRAP_PHY_FW_FSM_DIRECT_WRITE_EN_SHIFT                   3
#define PCIE_WRAP_PHY_FW_FSM_DIRECT_WRITE_EN_MASK                    0x8
#define PCIE_WRAP_PHY_FW_FSM_RD_CNTR_SHIFT                           4
#define PCIE_WRAP_PHY_FW_FSM_RD_CNTR_MASK                            0xFFFF0

/* PCIE_WRAP_PCIE_PHY_BASE_ADDR_L */
#define PCIE_WRAP_PCIE_PHY_BASE_ADDR_L_VAL_SHIFT                     0
#define PCIE_WRAP_PCIE_PHY_BASE_ADDR_L_VAL_MASK                      0xFFFFFFFF

/* PCIE_WRAP_PCIE_PHY_BASE_ADDR_H */
#define PCIE_WRAP_PCIE_PHY_BASE_ADDR_H_VAL_SHIFT                     0
#define PCIE_WRAP_PCIE_PHY_BASE_ADDR_H_VAL_MASK                      0xFFFFFFFF

/* PCIE_WRAP_PCIE_CORE_BASE_ADDR_L */
#define PCIE_WRAP_PCIE_CORE_BASE_ADDR_L_VAL_SHIFT                    0
#define PCIE_WRAP_PCIE_CORE_BASE_ADDR_L_VAL_MASK                     0xFFFFFFFF

/* PCIE_WRAP_PCIE_CORE_BASE_ADDR_H */
#define PCIE_WRAP_PCIE_CORE_BASE_ADDR_H_VAL_SHIFT                    0
#define PCIE_WRAP_PCIE_CORE_BASE_ADDR_H_VAL_MASK                     0xFFFFFFFF

/* PCIE_WRAP_SPMU_INTR */
#define PCIE_WRAP_SPMU_INTR_IND_SHIFT                                0
#define PCIE_WRAP_SPMU_INTR_IND_MASK                                 0x1

/* PCIE_WRAP_AXI_INTR */
#define PCIE_WRAP_AXI_INTR_IND_SHIFT                                 0
#define PCIE_WRAP_AXI_INTR_IND_MASK                                  0x1

/* PCIE_WRAP_PCIE_IC_SEI_INTR_IND */
#define PCIE_WRAP_PCIE_IC_SEI_INTR_IND_AXI_ERR_INTR_SHIFT            0
#define PCIE_WRAP_PCIE_IC_SEI_INTR_IND_AXI_ERR_INTR_MASK             0x1
#define PCIE_WRAP_PCIE_IC_SEI_INTR_IND_BAD_ACCESS_INTR_SHIFT         1
#define PCIE_WRAP_PCIE_IC_SEI_INTR_IND_BAD_ACCESS_INTR_MASK          0x2
#define PCIE_WRAP_PCIE_IC_SEI_INTR_IND_AXI_ERR_INTR_MASK_SHIFT       2
#define PCIE_WRAP_PCIE_IC_SEI_INTR_IND_AXI_ERR_INTR_MASK_MASK        0x4
#define PCIE_WRAP_PCIE_IC_SEI_INTR_IND_BAD_ACCESS_INTR_MASK_SHIFT    3
#define PCIE_WRAP_PCIE_IC_SEI_INTR_IND_BAD_ACCESS_INTR_MASK_MASK     0x8

#endif /* ASIC_REG_PCIE_WRAP_MASKS_H_ */
