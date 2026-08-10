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

#ifndef ASIC_REG_NIC0_TXE0_MASKS_H_
#define ASIC_REG_NIC0_TXE0_MASKS_H_

/*
 *****************************************
 *   NIC0_TXE0 (Prototype: NIC_TXE)
 *****************************************
 */

/* NIC0_TXE0_WQE_FETCH_REQ_MASK_31_0 */
#define NIC0_TXE0_WQE_FETCH_REQ_MASK_31_0_R_SHIFT                    0
#define NIC0_TXE0_WQE_FETCH_REQ_MASK_31_0_R_MASK                     0xFFFFFFFF

/* NIC0_TXE0_WQE_FETCH_REQ_MASK_47_32 */
#define NIC0_TXE0_WQE_FETCH_REQ_MASK_47_32_R_SHIFT                   0
#define NIC0_TXE0_WQE_FETCH_REQ_MASK_47_32_R_MASK                    0xFFFF

/* NIC0_TXE0_LOCAL_WQ_BUFFER_SIZE */
#define NIC0_TXE0_LOCAL_WQ_BUFFER_SIZE_R_SHIFT                       0
#define NIC0_TXE0_LOCAL_WQ_BUFFER_SIZE_R_MASK                        0xF

/* NIC0_TXE0_LOCAL_WQ_LINE_SIZE */
#define NIC0_TXE0_LOCAL_WQ_LINE_SIZE_R_SHIFT                         0
#define NIC0_TXE0_LOCAL_WQ_LINE_SIZE_R_MASK                          0xF

/* NIC0_TXE0_LOG_MAX_WQ_SIZE */
#define NIC0_TXE0_LOG_MAX_WQ_SIZE_R_SHIFT                            0
#define NIC0_TXE0_LOG_MAX_WQ_SIZE_R_MASK                             0x1F

/* NIC0_TXE0_SQ_BASE_ADDRESS_49_32 */
#define NIC0_TXE0_SQ_BASE_ADDRESS_49_32_R_SHIFT                      0
#define NIC0_TXE0_SQ_BASE_ADDRESS_49_32_R_MASK                       0x3FFFF

/* NIC0_TXE0_SQ_BASE_ADDRESS_31_0 */
#define NIC0_TXE0_SQ_BASE_ADDRESS_31_0_R_SHIFT                       0
#define NIC0_TXE0_SQ_BASE_ADDRESS_31_0_R_MASK                        0xFFFFFFFF

/* NIC0_TXE0_SQ_BASE_ADDRESS_SEL */
#define NIC0_TXE0_SQ_BASE_ADDRESS_SEL_R_SHIFT                        0
#define NIC0_TXE0_SQ_BASE_ADDRESS_SEL_R_MASK                         0x1F

/* NIC0_TXE0_ALLOC_CREDIT */
#define NIC0_TXE0_ALLOC_CREDIT_R_SHIFT                               0
#define NIC0_TXE0_ALLOC_CREDIT_R_MASK                                0xFF

/* NIC0_TXE0_ALLOC_CREDIT_FORCE_FULL */
#define NIC0_TXE0_ALLOC_CREDIT_FORCE_FULL_R_SHIFT                    0
#define NIC0_TXE0_ALLOC_CREDIT_FORCE_FULL_R_MASK                     0x1

/* NIC0_TXE0_READ_CREDIT */
#define NIC0_TXE0_READ_CREDIT_R_SHIFT                                0
#define NIC0_TXE0_READ_CREDIT_R_MASK                                 0x7FF

/* NIC0_TXE0_READ_CREDIT_FORCE_FULL */
#define NIC0_TXE0_READ_CREDIT_FORCE_FULL_R_SHIFT                     0
#define NIC0_TXE0_READ_CREDIT_FORCE_FULL_R_MASK                      0x1

/* NIC0_TXE0_BURST_ENABLE */
#define NIC0_TXE0_BURST_ENABLE_R_SHIFT                               0
#define NIC0_TXE0_BURST_ENABLE_R_MASK                                0x1

/* NIC0_TXE0_WR_INIT_BUSY */
#define NIC0_TXE0_WR_INIT_BUSY_R_SHIFT                               0
#define NIC0_TXE0_WR_INIT_BUSY_R_MASK                                0x3

/* NIC0_TXE0_READ_RES_WT_INIT_BUSY */
#define NIC0_TXE0_READ_RES_WT_INIT_BUSY_R_SHIFT                      0
#define NIC0_TXE0_READ_RES_WT_INIT_BUSY_R_MASK                       0x1

/* NIC0_TXE0_BTH_TVER */
#define NIC0_TXE0_BTH_TVER_R_SHIFT                                   0
#define NIC0_TXE0_BTH_TVER_R_MASK                                    0xF

/* NIC0_TXE0_IPV4_IDENTIFICATION */
#define NIC0_TXE0_IPV4_IDENTIFICATION_R_SHIFT                        0
#define NIC0_TXE0_IPV4_IDENTIFICATION_R_MASK                         0xFFFF

/* NIC0_TXE0_IPV4_FLAGS */
#define NIC0_TXE0_IPV4_FLAGS_R_SHIFT                                 0
#define NIC0_TXE0_IPV4_FLAGS_R_MASK                                  0x7

/* NIC0_TXE0_PAD */
#define NIC0_TXE0_PAD_ENABLE_SHIFT                                   0
#define NIC0_TXE0_PAD_ENABLE_MASK                                    0x1
#define NIC0_TXE0_PAD_RAW_QP_ENABLE_SHIFT                            1
#define NIC0_TXE0_PAD_RAW_QP_ENABLE_MASK                             0x2

/* NIC0_TXE0_ADD_PAD_TO_IPV4_LEN */
#define NIC0_TXE0_ADD_PAD_TO_IPV4_LEN_R_SHIFT                        0
#define NIC0_TXE0_ADD_PAD_TO_IPV4_LEN_R_MASK                         0x1

/* NIC0_TXE0_ADD_PAD_TO_UDP_LEN */
#define NIC0_TXE0_ADD_PAD_TO_UDP_LEN_R_SHIFT                         0
#define NIC0_TXE0_ADD_PAD_TO_UDP_LEN_R_MASK                          0x1

/* NIC0_TXE0_ICRC_EN */
#define NIC0_TXE0_ICRC_EN_R_SHIFT                                    0
#define NIC0_TXE0_ICRC_EN_R_MASK                                     0x1

/* NIC0_TXE0_UDP_MASK_S_PORT */
#define NIC0_TXE0_UDP_MASK_S_PORT_R_SHIFT                            0
#define NIC0_TXE0_UDP_MASK_S_PORT_R_MASK                             0xFFFF

/* NIC0_TXE0_UDP_CHECKSUM */
#define NIC0_TXE0_UDP_CHECKSUM_R_SHIFT                               0
#define NIC0_TXE0_UDP_CHECKSUM_R_MASK                                0xFFFF

/* NIC0_TXE0_UDP_DEST_PORT */
#define NIC0_TXE0_UDP_DEST_PORT_R_SHIFT                              0
#define NIC0_TXE0_UDP_DEST_PORT_R_MASK                               0xFFFF

/* NIC0_TXE0_PORT0_MAC_CFG_47_32 */
#define NIC0_TXE0_PORT0_MAC_CFG_47_32_R_SHIFT                        0
#define NIC0_TXE0_PORT0_MAC_CFG_47_32_R_MASK                         0xFFFF

/* NIC0_TXE0_PORT0_MAC_CFG_31_0 */
#define NIC0_TXE0_PORT0_MAC_CFG_31_0_R_SHIFT                         0
#define NIC0_TXE0_PORT0_MAC_CFG_31_0_R_MASK                          0xFFFFFFFF

/* NIC0_TXE0_PORT1_MAC_CFG_47_32 */
#define NIC0_TXE0_PORT1_MAC_CFG_47_32_R_SHIFT                        0
#define NIC0_TXE0_PORT1_MAC_CFG_47_32_R_MASK                         0xFFFF

/* NIC0_TXE0_PORT1_MAC_CFG_31_0 */
#define NIC0_TXE0_PORT1_MAC_CFG_31_0_R_SHIFT                         0
#define NIC0_TXE0_PORT1_MAC_CFG_31_0_R_MASK                          0xFFFFFFFF

/* NIC0_TXE0_PRIO_TO_DSCP */
#define NIC0_TXE0_PRIO_TO_DSCP_PRIO0_SHIFT                           0
#define NIC0_TXE0_PRIO_TO_DSCP_PRIO0_MASK                            0x3F
#define NIC0_TXE0_PRIO_TO_DSCP_PRIO1_SHIFT                           8
#define NIC0_TXE0_PRIO_TO_DSCP_PRIO1_MASK                            0x3F00
#define NIC0_TXE0_PRIO_TO_DSCP_PRIO2_SHIFT                           16
#define NIC0_TXE0_PRIO_TO_DSCP_PRIO2_MASK                            0x3F0000
#define NIC0_TXE0_PRIO_TO_DSCP_PRIO3_SHIFT                           24
#define NIC0_TXE0_PRIO_TO_DSCP_PRIO3_MASK                            0x3F000000

/* NIC0_TXE0_PRIO_TO_PCP */
#define NIC0_TXE0_PRIO_TO_PCP_PORT0_PRIO0_SHIFT                      0
#define NIC0_TXE0_PRIO_TO_PCP_PORT0_PRIO0_MASK                       0x7
#define NIC0_TXE0_PRIO_TO_PCP_PORT0_PRIO1_SHIFT                      3
#define NIC0_TXE0_PRIO_TO_PCP_PORT0_PRIO1_MASK                       0x38
#define NIC0_TXE0_PRIO_TO_PCP_PORT0_PRIO2_SHIFT                      6
#define NIC0_TXE0_PRIO_TO_PCP_PORT0_PRIO2_MASK                       0x1C0
#define NIC0_TXE0_PRIO_TO_PCP_PORT0_PRIO3_SHIFT                      9
#define NIC0_TXE0_PRIO_TO_PCP_PORT0_PRIO3_MASK                       0xE00
#define NIC0_TXE0_PRIO_TO_PCP_PORT1_PRIO0_SHIFT                      12
#define NIC0_TXE0_PRIO_TO_PCP_PORT1_PRIO0_MASK                       0x7000
#define NIC0_TXE0_PRIO_TO_PCP_PORT1_PRIO1_SHIFT                      15
#define NIC0_TXE0_PRIO_TO_PCP_PORT1_PRIO1_MASK                       0x38000
#define NIC0_TXE0_PRIO_TO_PCP_PORT1_PRIO2_SHIFT                      18
#define NIC0_TXE0_PRIO_TO_PCP_PORT1_PRIO2_MASK                       0x1C0000
#define NIC0_TXE0_PRIO_TO_PCP_PORT1_PRIO3_SHIFT                      21
#define NIC0_TXE0_PRIO_TO_PCP_PORT1_PRIO3_MASK                       0xE00000

/* NIC0_TXE0_MAC_ETHER_TYPE */
#define NIC0_TXE0_MAC_ETHER_TYPE_R_SHIFT                             0
#define NIC0_TXE0_MAC_ETHER_TYPE_R_MASK                              0xFFFF

/* NIC0_TXE0_MAC_ETHER_TYPE_VLAN */
#define NIC0_TXE0_MAC_ETHER_TYPE_VLAN_R_SHIFT                        0
#define NIC0_TXE0_MAC_ETHER_TYPE_VLAN_R_MASK                         0xFFFF

/* NIC0_TXE0_ECN_0 */
#define NIC0_TXE0_ECN_0_R_SHIFT                                      0
#define NIC0_TXE0_ECN_0_R_MASK                                       0x3

/* NIC0_TXE0_ECN_1 */
#define NIC0_TXE0_ECN_1_R_SHIFT                                      0
#define NIC0_TXE0_ECN_1_R_MASK                                       0x3

/* NIC0_TXE0_IPV4_TIME_TO_LIVE_0 */
#define NIC0_TXE0_IPV4_TIME_TO_LIVE_0_R_SHIFT                        0
#define NIC0_TXE0_IPV4_TIME_TO_LIVE_0_R_MASK                         0xFF

/* NIC0_TXE0_IPV4_TIME_TO_LIVE_1 */
#define NIC0_TXE0_IPV4_TIME_TO_LIVE_1_R_SHIFT                        0
#define NIC0_TXE0_IPV4_TIME_TO_LIVE_1_R_MASK                         0xFF

/* NIC0_TXE0_PRIO_PORT_CREDIT_FORCE */
#define NIC0_TXE0_PRIO_PORT_CREDIT_FORCE_FORCE_FULL_SHIFT            0
#define NIC0_TXE0_PRIO_PORT_CREDIT_FORCE_FORCE_FULL_MASK             0xFF

/* NIC0_TXE0_PRIO_PORT_CRDIT */
#define NIC0_TXE0_PRIO_PORT_CRDIT_R_SHIFT                            0
#define NIC0_TXE0_PRIO_PORT_CRDIT_R_MASK                             0xFF

/* NIC0_TXE0_WQE_FETCH_TOKEN_EN */
#define NIC0_TXE0_WQE_FETCH_TOKEN_EN_R_SHIFT                         0
#define NIC0_TXE0_WQE_FETCH_TOKEN_EN_R_MASK                          0x1

/* NIC0_TXE0_NACK_SYNDROME */
#define NIC0_TXE0_NACK_SYNDROME_SYNDROME_0_SHIFT                     0
#define NIC0_TXE0_NACK_SYNDROME_SYNDROME_0_MASK                      0xFF
#define NIC0_TXE0_NACK_SYNDROME_SYNDROME_1_SHIFT                     8
#define NIC0_TXE0_NACK_SYNDROME_SYNDROME_1_MASK                      0xFF00
#define NIC0_TXE0_NACK_SYNDROME_SYNDROME_2_SHIFT                     16
#define NIC0_TXE0_NACK_SYNDROME_SYNDROME_2_MASK                      0xFF0000
#define NIC0_TXE0_NACK_SYNDROME_SYNDROME_3_SHIFT                     24
#define NIC0_TXE0_NACK_SYNDROME_SYNDROME_3_MASK                      0xFF000000

/* NIC0_TXE0_WQE_FETCH_AXI_USER */
#define NIC0_TXE0_WQE_FETCH_AXI_USER_R_SHIFT                         0
#define NIC0_TXE0_WQE_FETCH_AXI_USER_R_MASK                          0xFFFFFFFF

/* NIC0_TXE0_WQE_FETCH_AXI_PROT */
#define NIC0_TXE0_WQE_FETCH_AXI_PROT_PRIVILEGE_SHIFT                 0
#define NIC0_TXE0_WQE_FETCH_AXI_PROT_PRIVILEGE_MASK                  0x7
#define NIC0_TXE0_WQE_FETCH_AXI_PROT_SECURED_SHIFT                   3
#define NIC0_TXE0_WQE_FETCH_AXI_PROT_SECURED_MASK                    0x38
#define NIC0_TXE0_WQE_FETCH_AXI_PROT_UNSECURED_SHIFT                 6
#define NIC0_TXE0_WQE_FETCH_AXI_PROT_UNSECURED_MASK                  0x1C0

/* NIC0_TXE0_DATA_FETCH_AXI_USER */
#define NIC0_TXE0_DATA_FETCH_AXI_USER_R_SHIFT                        0
#define NIC0_TXE0_DATA_FETCH_AXI_USER_R_MASK                         0xFFFFFFFF

/* NIC0_TXE0_DATA_FETCH_AXI_PROT */
#define NIC0_TXE0_DATA_FETCH_AXI_PROT_PRIVILEGE_SHIFT                0
#define NIC0_TXE0_DATA_FETCH_AXI_PROT_PRIVILEGE_MASK                 0x7
#define NIC0_TXE0_DATA_FETCH_AXI_PROT_SECURED_SHIFT                  3
#define NIC0_TXE0_DATA_FETCH_AXI_PROT_SECURED_MASK                   0x38
#define NIC0_TXE0_DATA_FETCH_AXI_PROT_UNSECURED_SHIFT                6
#define NIC0_TXE0_DATA_FETCH_AXI_PROT_UNSECURED_MASK                 0x1C0

/* NIC0_TXE0_FETCH_OUT_OF_TOKEN */
#define NIC0_TXE0_FETCH_OUT_OF_TOKEN_R_SHIFT                         0
#define NIC0_TXE0_FETCH_OUT_OF_TOKEN_R_MASK                          0x7

/* NIC0_TXE0_ECN_COUNT_EN */
#define NIC0_TXE0_ECN_COUNT_EN_R_SHIFT                               0
#define NIC0_TXE0_ECN_COUNT_EN_R_MASK                                0x1

/* NIC0_TXE0_INERRUPT_CAUSE */
#define NIC0_TXE0_INERRUPT_CAUSE_R_SHIFT                             0
#define NIC0_TXE0_INERRUPT_CAUSE_R_MASK                              0x7F

/* NIC0_TXE0_INTERRUPT_MASK */
#define NIC0_TXE0_INTERRUPT_MASK_R_SHIFT                             0
#define NIC0_TXE0_INTERRUPT_MASK_R_MASK                              0x7F

/* NIC0_TXE0_INTERRUPT_CLR */

/* NIC0_TXE0_VLAN_TAG_QPN_OFFSET */
#define NIC0_TXE0_VLAN_TAG_QPN_OFFSET_R_SHIFT                        0
#define NIC0_TXE0_VLAN_TAG_QPN_OFFSET_R_MASK                         0x1F

/* NIC0_TXE0_VALN_TAG_CFG */
#define NIC0_TXE0_VALN_TAG_CFG_TPD_SHIFT                             0
#define NIC0_TXE0_VALN_TAG_CFG_TPD_MASK                              0xFFFF
#define NIC0_TXE0_VALN_TAG_CFG_VLAN_ID_SHIFT                         16
#define NIC0_TXE0_VALN_TAG_CFG_VLAN_ID_MASK                          0xFFF0000
#define NIC0_TXE0_VALN_TAG_CFG_DEI_SHIFT                             28
#define NIC0_TXE0_VALN_TAG_CFG_DEI_MASK                              0x10000000
#define NIC0_TXE0_VALN_TAG_CFG_ENABLE_SHIFT                          31
#define NIC0_TXE0_VALN_TAG_CFG_ENABLE_MASK                           0x80000000

/* NIC0_TXE0_DBG_TRIG */
#define NIC0_TXE0_DBG_TRIG_R_SHIFT                                   0
#define NIC0_TXE0_DBG_TRIG_R_MASK                                    0xF

/* NIC0_TXE0_WQE_PREFETCH_CFG */
#define NIC0_TXE0_WQE_PREFETCH_CFG_ENABLE_SHIFT                      0
#define NIC0_TXE0_WQE_PREFETCH_CFG_ENABLE_MASK                       0x1
#define NIC0_TXE0_WQE_PREFETCH_CFG_ALWAYS_ENABLE_SHIFT               1
#define NIC0_TXE0_WQE_PREFETCH_CFG_ALWAYS_ENABLE_MASK                0x2

/* NIC0_TXE0_WQE_PREFETCH_INVALIDATE */

/* NIC0_TXE0_SWAP_MEMORY_ENDIANNESS */
#define NIC0_TXE0_SWAP_MEMORY_ENDIANNESS_RAW_SHIFT                   0
#define NIC0_TXE0_SWAP_MEMORY_ENDIANNESS_RAW_MASK                    0x1
#define NIC0_TXE0_SWAP_MEMORY_ENDIANNESS_RDMA_SHIFT                  1
#define NIC0_TXE0_SWAP_MEMORY_ENDIANNESS_RDMA_MASK                   0x2

/* NIC0_TXE0_WQE_FETCH_SLICE_47_32 */
#define NIC0_TXE0_WQE_FETCH_SLICE_47_32_R_SHIFT                      0
#define NIC0_TXE0_WQE_FETCH_SLICE_47_32_R_MASK                       0xFFFF

/* NIC0_TXE0_WQE_FETCH_SLICE_31_0 */
#define NIC0_TXE0_WQE_FETCH_SLICE_31_0_R_SHIFT                       0
#define NIC0_TXE0_WQE_FETCH_SLICE_31_0_R_MASK                        0xFFFFFFFF

/* NIC0_TXE0_WQE_EXE_SLICE_47_32 */
#define NIC0_TXE0_WQE_EXE_SLICE_47_32_R_SHIFT                        0
#define NIC0_TXE0_WQE_EXE_SLICE_47_32_R_MASK                         0xFFFF

/* NIC0_TXE0_WQE_EXE_SLICE_31_0 */
#define NIC0_TXE0_WQE_EXE_SLICE_31_0_R_SHIFT                         0
#define NIC0_TXE0_WQE_EXE_SLICE_31_0_R_MASK                          0xFFFFFFFF

/* NIC0_TXE0_DBG_COUNT_SELECT */
#define NIC0_TXE0_DBG_COUNT_SELECT_R_SHIFT                           0
#define NIC0_TXE0_DBG_COUNT_SELECT_R_MASK                            0x1F

/* NIC0_TXE0_BTH_MKEY */
#define NIC0_TXE0_BTH_MKEY_R_SHIFT                                   0
#define NIC0_TXE0_BTH_MKEY_R_MASK                                    0xFFFF

/* NIC0_TXE0_WQE_BUFF_FLUSH_SLICE_47_3 */
#define NIC0_TXE0_WQE_BUFF_FLUSH_SLICE_47_3_R_SHIFT                  0
#define NIC0_TXE0_WQE_BUFF_FLUSH_SLICE_47_3_R_MASK                   0xFFFF

/* NIC0_TXE0_WQE_BUFF_FLUSH_SLICE_31_0 */
#define NIC0_TXE0_WQE_BUFF_FLUSH_SLICE_31_0_R_SHIFT                  0
#define NIC0_TXE0_WQE_BUFF_FLUSH_SLICE_31_0_R_MASK                   0xFFFFFFFF

/* NIC0_TXE0_INTERRUPT_INDEX_MASK_RING */
#define NIC0_TXE0_INTERRUPT_INDEX_MASK_RING_R_SHIFT                  0
#define NIC0_TXE0_INTERRUPT_INDEX_MASK_RING_R_MASK                   0x3FFFFF

/* NIC0_TXE0_QPN_RING */
#define NIC0_TXE0_QPN_RING_R_SHIFT                                   0
#define NIC0_TXE0_QPN_RING_R_MASK                                    0xFFFFFF

/* NIC0_TXE0_INTERRUPT_EACH_PACKET */
#define NIC0_TXE0_INTERRUPT_EACH_PACKET_R_SHIFT                      0
#define NIC0_TXE0_INTERRUPT_EACH_PACKET_R_MASK                       0x1F

/* NIC0_TXE0_EXECUTIN_INDEX_RING */
#define NIC0_TXE0_EXECUTIN_INDEX_RING_R_SHIFT                        0
#define NIC0_TXE0_EXECUTIN_INDEX_RING_R_MASK                         0x3FFFFF

#endif /* ASIC_REG_NIC0_TXE0_MASKS_H_ */
