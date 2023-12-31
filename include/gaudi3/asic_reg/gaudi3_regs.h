/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef ASIC_REG_GAUDI3_REGS_H_
#define ASIC_REG_GAUDI3_REGS_H_

#include "gaudi3_blocks_linux_driver.h"

#include "cpu_if_regs.h"
#include "nch_regs.h"
#include "cache_maint_regs.h"
#include "pdma_cmn_b_regs.h"
#include "pdma_cmn_b_pqm_cmn_b_regs.h"
#include "pdup_eng_regs.h"
#include "qman_regs.h"
#include "qman_cgm_regs.h"
#include "edma_cmn_regs.h"
#include "tpc_regs.h"
#include "tpc_eml_cfg_regs.h"
#include "tpc_qm_regs.h"
#include "tpc_kernel_regs.h"
#include "tpc_qm_tensor_0_shared_regs.h"
#include "tpc_smt_tpc_th0_regs.h"
#include "mme_ctrl_lo_regs.h"
#include "mme_ctrl_lo_arch_dma_n_ten_regs.h"
#include "sb_regs.h"
#include "vdec_brdg_ctrl_regs.h"
#include "vdec_ctrl_special_regs.h"
#include "vsi_cmd_regs.h"
#include "vsi_dec_regs.h"
#include "arc_regs.h"
#include "arc_dup_eng_regs.h"
#include "qman_arc_aux_regs.h"
#include "pcie_wrap_regs.h"
#include "pcie_wrap_dbi_access_regs.h"
#include "pcie_wrap_dbi_gw_m0_regs.h"
#include "pcie_wrap_dbi_gw_m1_regs.h"
#include "rotator_regs.h"
#include "rotator_desc_regs.h"
#include "mstr_if_axprot_hbw_regs.h"
#include "mstr_if_axprot_lbw_regs.h"
#include "mstr_if_axcache_hbw_regs.h"
#include "mstr_if_xresp_lbw_regs.h"
#include "mstr_if_dbg_hbw_regs.h"
#include "mstr_if_dbg_lbw_regs.h"
#include "cbc_regs.h"
#include "cbc_user_regs.h"
#include "pcie_wrap_special_regs.h"
#include "global_conf_regs.h"
#include "pll_ctrl_regs.h"
#include "bmu_regs.h"
#include "pcie_dbi_regs.h"
#include "mmu_regs.h"
#include "pstlb_regs.h"
#include "pcie_aux_regs.h"
#include "pdma_ch_a_ctx_regs.h"
#include "pdma_ch_b_regs.h"
#include "pdma_ch_b_pqm_ch_regs.h"
#include "pdma_ch_b_axuser_hbw_regs.h"
#include "pdma_ch_b_pqm_axuser_hbw_regs.h"
#include "sob_objs_regs.h"
#include "sob_glbl_regs.h"
#include "cache_main_regs.h"
#include "cache_crdt_regs.h"
#include "cache_misc_regs.h"
#include "acc_regs.h"
#include "hbm_central_regs.h"
#include "mc_ch_regs.h"
#include "mc_cmn_regs.h"
#include "mc_cmn_intr_regs.h"
#include "hbm_phy_channels_ch0_aw_regs.h"
#include "hbm_phy_channels_ch1_aw_regs.h"
#include "hbm_phy_channels_ch2_aw_regs.h"
#include "hbm_phy_channels_ch3_aw_regs.h"
#include "hbm_phy_channels_ch4_aw_regs.h"
#include "hbm_phy_channels_ch5_aw_regs.h"
#include "hbm_phy_channels_ch6_aw_regs.h"
#include "hbm_phy_channels_ch7_aw_regs.h"
#include "hbm_phy_p0_master_regs.h"
#include "hbm2_phy_p0_ch_bcast_aw_regs.h"
#include "hbm2_phy_p0_initeng_regs.h"
#include "h9_hif_regs.h"
#include "int_agg_shared_sei_int_msg_regs.h"
#include "int_agg_shared_spi_eco_int_msg_regs.h"
#include "edma_chn_regs.h"
#include "arc_af_eng_sec_user_adapt_regs.h"
#include "rtr_ctrl_adec_hbw_regs.h"
#include "stm_regs.h"
#include "cs_dbg_tpc_eml_eml_etf_regs.h"
#include "cs_dbg_w_spmu_4_bmon_spmu_regs.h"
#include "cs_dbg_w_spmu_4_bmon_bmon0_regs.h"
#include "etf_1kb_regs.h"
#include "cs_dbg_tpc_eml_eml_spmu_regs.h"
#include "cs_dbg_tpc_eml_bmon0_regs.h"
#include "etr_regs.h"
#include "autonomous_control_regs.h"
#include "cs_trace_regs.h"
#include "psoc_reset_conf_regs.h"
#include "psoc_boot_conf_regs.h"
#include "psoc_security_regs.h"
#include "rtr_ctrl_rr_lbw_regs.h"
#include "rtr_ctrl_rr_hbw_regs.h"
#include "gic_regs.h"
#include "dphy_ctrl_regs.h"
#include "d2d_spi_regs.h"
#include "int_agg_hdcore_rei_derr_int_msg_regs.h"
#include "int_agg_hdcore_rei_serr_int_msg_regs.h"
#include "int_agg_hdcore_sei_int_msg_regs.h"
#include "int_agg_hdcore_spi_eco_int_msg_regs.h"
#include "int_agg_shared_rei_derr_int_msg_regs.h"
#include "int_agg_shared_rei_serr_int_msg_regs.h"
#include "int_agg_psoc_uart_comb_regs.h"
#include "msg2wire_sh_hd_regs.h"
#include "msg2wire_psoc_0_regs.h"
#include "arc_af_eng_regs.h"
#include "intr_gen_regs.h"
#include "timestamp_regs.h"
#include "dtlb_regs.h"
#include "dtlb_special_regs.h"
#include "rtr_ctrl_hbw_scram_regs.h"
#include "stlb_regs.h"
#include "nic_txs_regs.h"
#include "nic_txe_regs.h"
#include "nic_tmr_regs.h"
#include "nic_qpc_regs.h"
#include "nic_rxe_regs.h"
#include "nic_rxb_core_regs.h"
#include "rtr_ctrl_ch_razwi_hbw_regs.h"
#include "farm_regs.h"
#include "cache_err_regs.h"
#include "psoc_global_conf2_regs.h"
#include "parc_global_conf_regs.h"

/* TODO: used for debug, so can be removed once H9-5315 is resolved */
#include "nrtr_crdt_rrtr_ob_crdt_regs.h"

#include "pdma_cmn_b_masks.h"
#include "pdma_cmn_b_pqm_cmn_b_masks.h"
#include "pdup_eng_masks.h"
#include "qman_masks.h"
#include "qman_cgm_masks.h"
#include "edma_cmn_masks.h"
#include "edma_chn_masks.h"
#include "tpc_masks.h"
#include "tpc_qm_masks.h"
#include "tpc_kernel_masks.h"
#include "tpc_qm_tensor_0_shared_masks.h"
#include "mme_ctrl_lo_masks.h"
#include "mme_ctrl_lo_arch_dma_n_ten_masks.h"
#include "vdec_brdg_ctrl_masks.h"
#include "vdec_ctrl_special_masks.h"
#include "vsi_cmd_masks.h"
#include "vsi_dec_masks.h"
#include "arc_dup_eng_masks.h"
#include "qman_arc_aux_masks.h"
#include "pcie_wrap_masks.h"
#include "pcie_dbi_masks.h"
#include "pcie_wrap_dbi_access_masks.h"
#include "pcie_wrap_dbi_gw_m0_masks.h"
#include "pcie_wrap_dbi_gw_m1_masks.h"
#include "rotator_masks.h"
#include "rotator_desc_masks.h"
#include "mstr_if_axprot_lbw_masks.h"
#include "mstr_if_axcache_hbw_masks.h"
#include "mstr_if_xresp_lbw_masks.h"
#include "cbc_masks.h"
#include "cbc_user_masks.h"
#include "psoc_reset_conf_masks.h"
#include "psoc_boot_conf_masks.h"
#include "nic_txs_masks.h"
#include "nic_txe_masks.h"
#include "stlb_masks.h"
#include "mmu_masks.h"
#include "pstlb_masks.h"
#include "bmu_masks.h"
#include "pdma_ch_a_ctx_masks.h"
#include "pdma_ch_b_masks.h"
#include "pdma_ch_b_pqm_ch_masks.h"
#include "pdma_ch_b_axuser_hbw_masks.h"
#include "pdma_ch_b_pqm_axuser_hbw_masks.h"
#include "sob_objs_masks.h"
#include "sob_glbl_masks.h"
#include "cache_main_masks.h"
#include "cache_crdt_masks.h"
#include "dtlb_masks.h"
#include "pcie_aux_masks.h"
#include "nic_qpc_masks.h"
#include "nic_rxe_masks.h"
#include "mc_ch_masks.h"
#include "mc_cmn_intr_masks.h"
#include "hbm_phy_p0_master_masks.h"
#include "arc_af_eng_masks.h"
#include "arc_af_eng_sec_user_adapt_masks.h"
#include "autonomous_control_masks.h"
#include "etr_masks.h"
#include "mc_cmn_masks.h"
#include "rtr_ctrl_rr_lbw_masks.h"
#include "rtr_ctrl_rr_hbw_masks.h"
#include "cs_dbg_w_spmu_0_bmon_spmu_masks.h"
#include "cs_dbg_tpc_eml_bmon0_masks.h"
#include "sb_masks.h"
#include "acc_masks.h"
#include "cache_err_masks.h"
#include "parc_global_conf_masks.h"
#include "psoc_global_conf2_masks.h"

#define BMON_BASE_OFFSET		(mmHD0_TPC0_CS_DBG_BMON1_BASE - \
						mmHD0_TPC0_CS_DBG_BMON0_BASE)
/* CSLICE */
#define CS_MAINT_BASE_OFFSET		(mmHD0_CS0_MAINT_BASE - mmHD0_CS0_MAIN_BASE)

/* QM */
#define QM_AXUSER_HBW_BASE_OFFSET	(mmHD0_TPC0_QM_AXUSER_HBW_BASE - mmHD0_TPC0_QM_BASE)
#define QM_CGM_BASE_OFFSET		(mmHD0_TPC0_QM_CGM_BASE - mmHD0_TPC0_QM_BASE)

/* TPC */
#define HDCORE_TPC_OFFSET		(mmHD0_TPC1_CFG_BASE - mmHD0_TPC0_CFG_BASE)
#define TPC_THREAD_OFFSET		(mmHD0_TPC0_SMT_TPC_TENOSOR0_BASE_TH1_BASE - \
						mmHD0_TPC0_SMT_TPC_TENOSOR0_BASE_TH0_BASE)
#define HDCORE_TPC_TENSOR_OFFSET	(mmHD0_TPC0_CFG_QM_TENSOR_1_SHARED_BASE - \
						mmHD0_TPC0_CFG_QM_TENSOR_0_SHARED_BASE)
#define TPC_CFG_QM_TENSOR_0_OFFSET	(mmHD0_TPC0_CFG_QM_TENSOR_0_SHARED_BASE - \
						mmHD0_TPC0_CFG_BASE)
#define TPC_CFG_QM_OFFSET		(mmHD0_TPC0_CFG_QM_BASE - mmHD0_TPC0_CFG_BASE)

/* MME */
#define HDCORE_MME_EU_OFFSET		(mmHD0_MME1_ACC_BASE - mmHD0_MME0_ACC_BASE)
#define HDCORE_MME_SBTE_OFFSET		(mmHD0_MME0_SBTE1_BASE - mmHD0_MME0_SBTE0_BASE)
#define HDCORE_MME_SBTE_GRP_OFFSET	(mmHD0_MME1_SBTE0_BASE - mmHD0_MME0_SBTE0_BASE)
#define MME_CTRL_LO_ARCH_DMA_N_TEN_OFFSET \
			(mmHD0_MME_CTRL_LO_ARCH_DMA_N_TEN_BASE - mmHD0_MME_CTRL_LO_BASE)

/* ROT */
#define HDCORE_ROT_OFFSET		(mmHD1_ROT1_BASE - mmHD1_ROT0_BASE)

/* Decoder */
#define HDCORE_DECODER_OFFSET		(mmHD0_VDEC1_CMD_BASE - mmHD0_VDEC0_CMD_BASE)

#define mmD0_PCIE_DBI_SNPS_BASE	0xC403000ull

#define DIE_OFFSET	(mmD1_PMMU_HBW_STLB_BASE - mmD0_PMMU_HBW_STLB_BASE)
#define HDCORE_OFFSET	(mmHD1_ARC_FARM_ARC0_ACP_ENG_BASE - mmHD0_ARC_FARM_ARC0_ACP_ENG_BASE)

/* SYNC_MNGR */
#define SM_OBJS_SEC_PROT_BITS_0_OFFS	0xF000
#define SM_OBJS_SEC_PROT_BITS_1_OFFS	0x1F000

/* ARC_ACP */
#define ARC_ACP_ENG_OFFSET \
	(mmHD0_ARC_FARM_ARC1_ACP_ENG_BASE - mmHD0_ARC_FARM_ARC0_ACP_ENG_BASE)

/* ARC_AF_SEC */
#define ARC_AF_SEC_USER_ADAPT_OFFSET \
		(mmHD0_ARC_FARM_ARC1_AF_SEC_USER_ADAPT_BASE - \
			mmHD0_ARC_FARM_ARC0_AF_SEC_USER_ADAPT_BASE)

/* ARC_DUP */
#define ARC_DUP_SCHED_OFFSET \
		(mmHD0_ARC_FARM_ARC1_DUP_ENG_BASE - \
			mmHD0_ARC_FARM_ARC0_DUP_ENG_BASE)

/* PMMU_HBW_STLB */
#define PSTLB_OFFSET(REG)		\
		((u64)REG - mmD0_PMMU_HBW_STLB_BASE)

#define PSTLB_LL_LOOKUP_MASK_63_32_OFFSET		\
		PSTLB_OFFSET(mmD0_PMMU_HBW_STLB_LINK_LIST_LOOKUP_MASK_63_32)

#define PSTLB_SRAM_INIT_OFFSET			\
		PSTLB_OFFSET(mmD0_PMMU_HBW_STLB_SRAM_INIT)

/* PDMA */
#define PDMA_GRP_OFFSET \
	(mmD0_SPDMA1_CH0_A_BASE - mmD0_SPDMA0_CH0_A_BASE)

#define PDMA_CH_OFFSET \
	(mmD0_SPDMA0_CH1_A_PQM_CH_BASE - mmD0_SPDMA0_CH0_A_PQM_CH_BASE)

#define PDMA_ENGINE_OFFSET \
	(mmD0_SPDMA1_CH0_A_PQM_CH_BASE - mmD0_SPDMA0_CH0_A_PQM_CH_BASE)

#define PDMA_CH_B_AXUSER_HBW_OFFSET \
	(mmD0_SPDMA0_CH0_B_AXUSER_HBW_BASE - mmD0_SPDMA0_CH0_A_PQM_CH_BASE)

#define PDMA_CH_B_PQM_AXUSER_HBW_OFFSET \
	(mmD0_SPDMA0_CH0_B_PQM_AXUSER_HBW_BASE - mmD0_SPDMA0_CH0_A_PQM_CH_BASE)

#define PDMA_CH_B_ECMPLTN_Q_AXUSER_HBW_OFFSET \
	(mmD0_SPDMA0_CH0_B_ECMPLTN_Q_AXUSER_HBW_BASE - mmD0_SPDMA0_CH0_A_PQM_CH_BASE)

#define PDMA_CH_B_OFFSET \
	(mmD0_SPDMA0_CH0_B_BASE - mmD0_SPDMA0_CH0_A_PQM_CH_BASE)

#define PDMA_CMN_B_OFFSET \
	(mmD0_SPDMA0_CMN_B_BASE - mmD0_SPDMA0_CH0_A_PQM_CH_BASE)

#define PDMA_CH_A_CTX_OFFSET \
	(mmD0_SPDMA0_CH0_A_CTX_BASE - mmD0_SPDMA0_CH0_A_PQM_CH_BASE)

/* DUP */
#define DUP_CONTROL_HALT_OFFSET \
	(mmD0_SPDMA0_DUP_ENG_BASE + mmPDUP_ENG_DUP_CONTROL_HALT - mmD0_SPDMA0_CH0_A_PQM_CH_BASE)

/* NIC */
#define NIC_UMR_OFFSET			(mmD0_NIC0_UMR_1_BASE - mmD0_NIC0_UMR_0_BASE)
#define NIC_CQ_UMR_OFFSET		(mmD0_NIC0_CQ_UMR_1_BASE - mmD0_NIC0_CQ_UMR_0_BASE)
#define NIC_OFFSET			(mmD0_NIC1_QM_DCCM_BASE - mmD0_NIC0_QM_DCCM_BASE)
#define NIC_DIE_OFFSET			(mmD1_NIC0_QPC_BASE - mmD0_NIC0_QPC_BASE)
#define NIC_QM_SIZE			(mmD0_NIC0_QM_BASE - mmD0_NIC0_QM_DCCM_BASE)
#define NIC_AUX_SIZE			(mmD0_NIC0_ARC_AUX_BASE - mmD0_NIC0_QM_DCCM_BASE)

/* DTLB */
#define DTLB_SPECIAL_GLBL_SPARE_0_OFFSET ((mmHD0_RRTR0_DTLB_SPECIAL_BASE - mmHD0_RRTR0_DTLB_BASE) \
						+ mmDTLB_SPECIAL_GLBL_SPARE_0)

/* RTR */
#define RTR_CTRL_REG_OFF(HD0_RRTR0_CTRL_REG)	\
				(HD0_RRTR0_CTRL_REG - mmHD0_RRTR0_RTR_CTRL_HBW_SCRAM_BASE)
#define RRTR_OFFSET		(mmHD0_RRTR1_BASE - mmHD0_RRTR0_BASE)
#define RRTR_DTLB_OFFSET	(mmHD0_RRTR0_DTLB_BASE - mmHD0_RRTR0_BASE)
#define NRTR_DTLB_OFFSET	(mmD0_NRTR0_DTLB_PDMA_BASE - mmD0_NRTR0_DTLB_NW0_BASE)
#define GRTR_OFFSET		(mmD0_GRTR1_BASE - mmD0_GRTR0_BASE)

#define RTR_CTRL_ADEC_HBW_ADEC_REGION_EN_1_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_ADEC_HBW_BASE + \
						mmRTR_CTRL_ADEC_HBW_ADEC_REGION_EN_1))

#define RTR_CTRL_RR_LBW_SEC_RANGE_EN_SHORT_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_SEC_RANGE_EN_SHORT_0))
#define RTR_CTRL_RR_LBW_SEC_RANGE_MIN_SHORT_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_SEC_RANGE_MIN_SHORT_0))
#define RTR_CTRL_RR_LBW_SEC_RANGE_MAX_SHORT_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_SEC_RANGE_MAX_SHORT_0))
#define RTR_CTRL_RR_LBW_PRIV_RANGE_EN_SHORT_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_PRIV_RANGE_EN_SHORT_0))
#define RTR_CTRL_RR_LBW_PRIV_RANGE_MIN_SHORT_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_PRIV_RANGE_MIN_SHORT_0))
#define RTR_CTRL_RR_LBW_PRIV_RANGE_MAX_SHORT_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_PRIV_RANGE_MAX_SHORT_0))
#define RTR_CTRL_RR_LBW_PRIV_RANGE_EN_SHORT_13_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_PRIV_RANGE_EN_SHORT_13))
#define RTR_CTRL_RR_LBW_PRIV_RANGE_MIN_SHORT_13_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_PRIV_RANGE_MIN_SHORT_13))
#define RTR_CTRL_RR_LBW_PRIV_RANGE_MAX_SHORT_13_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_PRIV_RANGE_MAX_SHORT_13))
#define RTR_CTRL_RR_LBW_SEC_RANGE_EN_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_SEC_RANGE_EN_0))
#define RTR_CTRL_RR_LBW_SEC_RANGE_MIN_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_SEC_RANGE_MIN_0))
#define RTR_CTRL_RR_LBW_SEC_RANGE_MAX_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_SEC_RANGE_MAX_0))
#define RTR_CTRL_RR_LBW_PRIV_RANGE_EN_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_PRIV_RANGE_EN_0))
#define RTR_CTRL_RR_LBW_PRIV_RANGE_MIN_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_PRIV_RANGE_MIN_0))
#define RTR_CTRL_RR_LBW_PRIV_RANGE_MAX_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_LBW_BASE + \
					mmRTR_CTRL_RR_LBW_PRIV_RANGE_MAX_0))

#define RTR_CTRL_RR_HBW_SEC_RANGE_EN_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_SEC_RANGE_EN_0))
#define RTR_CTRL_RR_HBW_SEC_RANGE_MIN_HI_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_SEC_RANGE_MIN_HI_0))
#define RTR_CTRL_RR_HBW_SEC_RANGE_MIN_LO_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_SEC_RANGE_MIN_LO_0))
#define RTR_CTRL_RR_HBW_SEC_RANGE_MAX_HI_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_SEC_RANGE_MAX_HI_0))
#define RTR_CTRL_RR_HBW_SEC_RANGE_MAX_LO_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_SEC_RANGE_MAX_LO_0))
#define RTR_CTRL_RR_HBW_PRIV_RANGE_EN_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_PRIV_RANGE_EN_0))
#define RTR_CTRL_RR_HBW_PRIV_RANGE_MIN_HI_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_PRIV_RANGE_MIN_HI_0))
#define RTR_CTRL_RR_HBW_PRIV_RANGE_MIN_LO_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_PRIV_RANGE_MIN_LO_0))
#define RTR_CTRL_RR_HBW_PRIV_RANGE_MAX_HI_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_PRIV_RANGE_MAX_HI_0))
#define RTR_CTRL_RR_HBW_PRIV_RANGE_MAX_LO_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_PRIV_RANGE_MAX_LO_0))
#define RTR_CTRL_RR_HBW_PRIV_RANGE_EN_7_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_PRIV_RANGE_EN_7))
#define RTR_CTRL_RR_HBW_PRIV_RANGE_MIN_HI_7_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_PRIV_RANGE_MIN_HI_7))
#define RTR_CTRL_RR_HBW_PRIV_RANGE_MIN_LO_7_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_PRIV_RANGE_MIN_LO_7))
#define RTR_CTRL_RR_HBW_PRIV_RANGE_MAX_HI_7_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_PRIV_RANGE_MAX_HI_7))
#define RTR_CTRL_RR_HBW_PRIV_RANGE_MAX_LO_7_OFFSET	\
			RTR_CTRL_REG_OFF((mmHD0_RRTR0_RTR_CTRL_RR_HBW_BASE + \
					mmRTR_CTRL_RR_HBW_PRIV_RANGE_MAX_LO_7))

/* EDMA */
#define HDCORE_EDMA_OFFSET	(mmHD1_SEDMA1_QM_BASE - mmHD1_SEDMA0_QM_BASE)
#define EDMA_CHANNEL_OFFSET	(mmHD1_SEDMA0_CH1_BASE - mmHD1_SEDMA0_CH0_BASE)

/* CSLICE */
#define CSLICE_REG_OFF(HD0_CS0_REG)	(HD0_CS0_REG - mmHD0_CS0_MAIN_BASE)
#define CSLICE_CRDT_OFFSET		(mmHD0_CS0_CRDT_BASE - mmHD0_CS0_MAIN_BASE)
#define CSLICE_MISC_OFFSET		(mmHD0_CS0_MISC_BASE - mmHD0_CS0_MAIN_BASE)
#define CSLICE_OFFSET			(mmHD0_CS1_MAIN_BASE - mmHD0_CS0_MAIN_BASE)

/* DPHY_CTRL */
#define DPHY_CTRL_DIE_INSTANCE_OFFSET		(mmD0_DPHY1_CTRL_BASE - mmD0_DPHY0_CTRL_BASE)


#define DPHY_CTRL_TRAIN_FSM_CTRL0_OFFSET	DPHY_CTRL_REG_OFF(mmD0_DPHY0_CTRL_TRAIN_FSM_CTRL0)
#define DPHY_CTRL_TRAIN_FSM_CTRL1_OFFSET	DPHY_CTRL_REG_OFF(mmD0_DPHY0_CTRL_TRAIN_FSM_CTRL1)
#define DPHY_CTRL_TRAIN_FSM_CTRL2_OFFSET	DPHY_CTRL_REG_OFF(mmD0_DPHY0_CTRL_TRAIN_FSM_CTRL2)
#define DPHY_CTRL_TRAIN_FSM_CTRL3_OFFSET	DPHY_CTRL_REG_OFF(mmD0_DPHY0_CTRL_TRAIN_FSM_CTRL3)
#define DPHY_CTRL_TRAIN_FSM_CTRL5_OFFSET	DPHY_CTRL_REG_OFF(mmD0_DPHY0_CTRL_TRAIN_FSM_CTRL5)
#define DPHY_CTRL_TRAIN_FSM_CTRL6_OFFSET	DPHY_CTRL_REG_OFF(mmD0_DPHY0_CTRL_TRAIN_FSM_CTRL6)

/* D2D_MAC */
#define D2D_MAC_DIE_INSTANCE_OFFSET	(mmD0_D2D_MAC1_SHARED_BASE - mmD0_D2D_MAC0_SHARED_BASE)
#define D2D_MAC_LINK_OVRD_OFFSET	(mmD0_D2D_MAC0_SHARED_LINK_OVRD - mmD0_D2D_MAC0_SHARED_BASE)
#define D2D_MAC_MAC_CTRL0_OFFSET	(mmD0_D2D_MAC0_SHARED_MAC_CTRL0 - mmD0_D2D_MAC0_SHARED_BASE)

/* HBM */
#define HBM_DEV_OFFSET		(mmD0_HBM1_MC0_BASE - mmD0_HBM0_MC0_BASE)
#define HBM_MC_OFFSET		(mmD0_HBM0_MC1_BASE - mmD0_HBM0_MC0_BASE)

/* Interrupts Aggr */
#define INTR_AGG_BLOCK_OFFSET	(mmD0_CPU_INT_AGG_HDCORE1_REI_DERR_INT_MSG_BASE - \
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE)

#define PARC_INTR_BLOCK_OFFSET	(mmD0_PARC_INT_AGGR_GPIO_COMB_BASE - \
					mmD0_PARC_INT_AGGR_UART_COMB_BASE)

/* RAZWI */
#define RR_AW_OFFSET (mmRTR_CTRL_CH_RAZWI_HBW_RR_RAZWI_AW_HAPPENED_CLR)
#define RR_AR_OFFSET (mmRTR_CTRL_CH_RAZWI_HBW_RR_RAZWI_AR_HAPPENED_CLR)
#define ADDR_DECODER_AW_OFFSET (mmRTR_CTRL_CH_RAZWI_HBW_ADEC_RAZWI_AW_CLR)
#define ADDR_DECODER_AR_OFFSET (mmRTR_CTRL_CH_RAZWI_HBW_ADEC_RAZWI_AR_CLR)

#endif /* ASIC_REG_GAUDI3_REGS_H_ */
