/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef ASIC_REG_GRECO_REGS_H_
#define ASIC_REG_GRECO_REGS_H_

#include "greco_blocks_linux_driver.h"
#include "cpu_if_regs.h"
#include "cpu_ca53_cfg_regs.h"
#include "psoc_arc0_cfg_regs.h"
#include "psoc_arc1_aux_regs.h"
#include "psoc_arc1_cfg_regs.h"
#include "psoc_mstr_if_rr_shrd_hbw_regs.h"
#include "psoc_mstr_if_rr_shrd_lbw_regs.h"
#include "psoc_mstr_if_axuser_regs.h"
#include "dcore0_ddma_core_regs.h"
#include "dcore0_ddma_core_ctx_regs.h"
#include "dcore0_ddma_core_ctx_axuser_regs.h"
#include "dcore0_ddma_qm_regs.h"
#include "dcore0_ddma_qm_axuser_nonsecured_regs.h"
#include "dcore0_ddma_mstr_if_axuser_regs.h"
#include "dcore0_kdma_core_regs.h"
#include "dcore0_kdma_core_ctx_axuser_regs.h"
#include "dcore0_kdma_mstr_if_axuser_regs.h"
#include "dcore0_kdma_core_ctx_regs.h"
#include "dcore0_kdma_mstr_if_rr_prvt_hbw_regs.h"
#include "dcore0_kdma_mstr_if_rr_prvt_lbw_regs.h"
#include "dcore0_pdma0_core_regs.h"
#include "dcore0_pdma0_core_ctx_axuser_regs.h"
#include "dcore0_pdma0_qm_regs.h"
#include "dcore0_pdma1_core_regs.h"
#include "dcore0_pdma1_core_ctx_axuser_regs.h"
#include "dcore0_pdma1_qm_regs.h"
#include "dcore0_pdma0_qm_axuser_nonsecured_regs.h"
#include "dcore0_pdma1_qm_axuser_nonsecured_regs.h"
#include "dcore0_pdma0_mstr_if_axuser_regs.h"
#include "dcore0_pdma1_mstr_if_axuser_regs.h"
#include "dcore0_pdma0_core_ctx_regs.h"
#include "dcore0_rot_qm_axuser_nonsecured_regs.h"
#include "dcore0_rot_qm_regs.h"
#include "dcore0_sync_mngr_mstr_if_axuser_regs.h"
#include "dcore0_sync_mngr_objs_regs.h"
#include "dcore0_sync_mngr_glbl_regs.h"
#include "dcore0_mme_qm_regs.h"
#include "dcore0_mme_ctrl_lo_regs.h"
#include "dcore0_mme_qm_axuser_nonsecured_regs.h"
#include "dcore0_mme_ctrl_lo_mme_axuser_regs.h"
#include "dcore0_mme_mstr_if_axuser_regs.h"
#include "dcore0_tpc0_cfg_regs.h"
#include "dcore0_tpc1_cfg_regs.h"
#include "dcore0_tpc2_cfg_regs.h"
#include "dcore0_tpc3_cfg_regs.h"
#include "dcore0_tpc4_cfg_regs.h"
#include "dcore1_tpc0_cfg_regs.h"
#include "dcore1_tpc1_cfg_regs.h"
#include "dcore1_tpc2_cfg_regs.h"
#include "dcore1_tpc3_cfg_regs.h"
#include "dcore1_tpc4_cfg_regs.h"
#include "dcore0_tpc0_cfg_axuser_regs.h"
#include "dcore0_tpc1_cfg_axuser_regs.h"
#include "dcore0_tpc2_cfg_axuser_regs.h"
#include "dcore0_tpc3_cfg_axuser_regs.h"
#include "dcore0_tpc4_cfg_axuser_regs.h"
#include "dcore0_tpc0_cfg_kernel_regs.h"
#include "dcore0_tpc0_cfg_kernel_tensor_0_regs.h"
#include "dcore0_tpc0_cfg_qm_regs.h"
#include "dcore0_tpc0_cfg_qm_sync_object_regs.h"
#include "dcore0_tpc0_cfg_qm_tensor_0_regs.h"
#include "dcore0_tpc0_qm_regs.h"
#include "dcore0_tpc1_qm_regs.h"
#include "dcore0_tpc2_qm_regs.h"
#include "dcore0_tpc3_qm_regs.h"
#include "dcore0_tpc4_qm_regs.h"
#include "dcore0_tpc0_qm_axuser_nonsecured_regs.h"
#include "dcore0_tpc1_qm_axuser_nonsecured_regs.h"
#include "dcore0_tpc2_qm_axuser_nonsecured_regs.h"
#include "dcore0_tpc3_qm_axuser_nonsecured_regs.h"
#include "dcore0_tpc4_qm_axuser_nonsecured_regs.h"
#include "dcore0_tpc0_mstr_if_axuser_regs.h"
#include "dcore0_tpc1_mstr_if_axuser_regs.h"
#include "dcore0_tpc2_mstr_if_axuser_regs.h"
#include "dcore0_tpc3_mstr_if_axuser_regs.h"
#include "dcore0_tpc4_mstr_if_axuser_regs.h"
#include "dcore0_tpc0_eml_tpc_qm_regs.h"
#include "dcore0_tpc0_eml_tpc_cfg_qm_regs.h"
#include "dcore0_tpc0_eml_tpc_cfg_kernel_tensor_0_regs.h"
#include "dcore0_tpc0_eml_tpc_cfg_qm_tensor_0_regs.h"
#include "dcore0_tpc0_eml_tpc_cfg_qm_sync_object_regs.h"
#include "dcore0_tpc0_eml_tpc_cfg_regs.h"
#include "dcore1_ddma_qm_regs.h"
#include "dcore1_mme_qm_regs.h"
#include "dcore1_mme_ctrl_lo_regs.h"
#include "dcore1_pdma0_qm_regs.h"
#include "dcore1_pdma1_qm_regs.h"
#include "dcore1_sync_mngr_objs_regs.h"
#include "dcore1_sync_mngr_glbl_regs.h"
#include "dcore1_tpc0_qm_regs.h"
#include "dcore1_tpc1_qm_regs.h"
#include "dcore1_tpc2_qm_regs.h"
#include "dcore1_tpc3_qm_regs.h"
#include "dcore1_tpc4_qm_regs.h"
#include "dcore1_tpc0_mstr_if_axuser_regs.h"
#include "dcore1_tpc1_mstr_if_axuser_regs.h"
#include "dcore1_tpc2_mstr_if_axuser_regs.h"
#include "dcore1_tpc3_mstr_if_axuser_regs.h"
#include "dcore1_tpc4_mstr_if_axuser_regs.h"
#include "dcore0_hmmu0_mmu_regs.h"
#include "dcore0_hmmu0_stlb_regs.h"
#include "gic_regs.h"
#include "pcie_aux_regs.h"
#include "pcie_wrap_regs.h"
#include "psoc_global_conf_regs.h"
#include "psoc_pci_pll_ctrl_regs.h"
#include "psoc_timestamp_regs.h"
#include "dcon0_hbw_rtr_if0_rtr_ctrl_regs.h"
#include "dcon0_hbw_rtr_if1_rtr_ctrl_regs.h"
#include "dcon1_hbw_rtr_if0_rtr_ctrl_regs.h"
#include "dcon1_hbw_rtr_if1_rtr_ctrl_regs.h"
#include "dcon2_hbw_rtr_if0_rtr_ctrl_regs.h"
#include "dcon2_hbw_rtr_if1_rtr_ctrl_regs.h"
#include "dcon3_hbw_rtr_if0_rtr_ctrl_regs.h"
#include "dcon3_hbw_rtr_if1_rtr_ctrl_regs.h"
#include "dcon0_hbw_rtr_if0_rtr_h3_regs.h"
#include "dcon0_hbw_rtr_if1_rtr_h3_regs.h"
#include "dcon1_hbw_rtr_if0_rtr_h3_regs.h"
#include "dcon1_hbw_rtr_if1_rtr_h3_regs.h"
#include "dcon2_hbw_rtr_if0_rtr_h3_regs.h"
#include "dcon2_hbw_rtr_if1_rtr_h3_regs.h"
#include "dcon3_hbw_rtr_if0_rtr_h3_regs.h"
#include "dcon3_hbw_rtr_if1_rtr_h3_regs.h"
#include "dcon0_lbw_rtr_if_rtr_ctrl_regs.h"
#include "dcon1_lbw_rtr_if_rtr_ctrl_regs.h"
#include "dcon2_lbw_rtr_if_rtr_ctrl_regs.h"
#include "dcon3_lbw_rtr_if_rtr_ctrl_regs.h"
#include "dcore0_mmeif_rtr0_regs.h"
#include "dcore0_mmeif_rtr1_regs.h"
#include "dcore0_mmeif_rtr2_regs.h"
#include "dcore0_mmeif_rtr3_regs.h"
#include "dcore0_mmeif_rtr0_h3_regs.h"
#include "dcore0_mmeif_rtr1_h3_regs.h"
#include "dcore0_mmeif_rtr2_h3_regs.h"
#include "dcore0_mmeif_rtr3_h3_regs.h"
#include "dcore0_mmeif_rtr0_ctrl_regs.h"
#include "dcore0_mmeif_rtr1_ctrl_regs.h"
#include "dcore0_mmeif_rtr2_ctrl_regs.h"
#include "dcore0_mmeif_rtr3_ctrl_regs.h"
#include "dcore0_tpcif_rtr0_regs.h"
#include "dcore0_tpcif_rtr1_regs.h"
#include "dcore0_tpcif_rtr2_regs.h"
#include "dcore0_tpcif_rtr3_regs.h"
#include "dcore0_tpcif_rtr0_h3_regs.h"
#include "dcore0_tpcif_rtr1_h3_regs.h"
#include "dcore0_tpcif_rtr2_h3_regs.h"
#include "dcore0_tpcif_rtr3_h3_regs.h"
#include "dcore0_tpcif_rtr0_ctrl_regs.h"
#include "dcore0_tpcif_rtr1_ctrl_regs.h"
#include "dcore0_tpcif_rtr2_ctrl_regs.h"
#include "dcore0_tpcif_rtr3_ctrl_regs.h"
#include "dcore1_mmeif_rtr0_regs.h"
#include "dcore1_mmeif_rtr1_regs.h"
#include "dcore1_mmeif_rtr2_regs.h"
#include "dcore1_mmeif_rtr3_regs.h"
#include "dcore1_mmeif_rtr0_h3_regs.h"
#include "dcore1_mmeif_rtr1_h3_regs.h"
#include "dcore1_mmeif_rtr2_h3_regs.h"
#include "dcore1_mmeif_rtr3_h3_regs.h"
#include "dcore1_mmeif_rtr0_ctrl_regs.h"
#include "dcore1_mmeif_rtr1_ctrl_regs.h"
#include "dcore1_mmeif_rtr2_ctrl_regs.h"
#include "dcore1_mmeif_rtr3_ctrl_regs.h"
#include "dcore1_tpcif_rtr0_regs.h"
#include "dcore1_tpcif_rtr1_regs.h"
#include "dcore1_tpcif_rtr2_regs.h"
#include "dcore1_tpcif_rtr3_regs.h"
#include "dcore1_tpcif_rtr0_h3_regs.h"
#include "dcore1_tpcif_rtr1_h3_regs.h"
#include "dcore1_tpcif_rtr2_h3_regs.h"
#include "dcore1_tpcif_rtr3_h3_regs.h"
#include "dcore1_tpcif_rtr0_ctrl_regs.h"
#include "dcore1_tpcif_rtr1_ctrl_regs.h"
#include "dcore1_tpcif_rtr2_ctrl_regs.h"
#include "dcore1_tpcif_rtr3_ctrl_regs.h"
#include "psoc_etr_regs.h"
#include "dcore0_ddma_qm_cgm_regs.h"
#include "dcore0_hif0_regs.h"
#include "dcore0_hif1_regs.h"
#include "dcore0_hmmu0_scramb_out_regs.h"
#include "dcore0_hmmu1_scramb_out_regs.h"
#include "dcore0_kdma_core_kdma_cgm_regs.h"
#include "dcore0_mme_qm_cgm_regs.h"
#include "dcore0_mme_sbtea_regs.h"
#include "dcore0_mme_sbteb_regs.h"
#include "dcore0_mme_sbtel_regs.h"
#include "dcore0_mme_acc_regs.h"
#include "dcore0_mme_sram_l0_regs.h"
#include "dcore0_pdma0_qm_cgm_regs.h"
#include "dcore0_pdma1_qm_cgm_regs.h"
#include "dcore0_pdma0_core_kdma_cgm_regs.h"
#include "dcore0_pdma1_core_kdma_cgm_regs.h"
#include "dcore0_ddma_core_kdma_cgm_regs.h"
#include "dcore0_rot_qm_cgm_regs.h"
#include "dcore0_sram_bank_0_regs.h"
#include "dcore0_sram_bank_1_regs.h"
#include "dcore0_sram_bank_2_regs.h"
#include "dcore0_sram_bank_3_regs.h"
#include "dcore0_sram_bank_4_regs.h"
#include "dcore0_sram_bank_5_regs.h"
#include "dcore0_sram_bank_6_regs.h"
#include "dcore0_sram_bank_7_regs.h"
#include "dcore0_sram_bank_8_regs.h"
#include "dcore0_sram_bank_9_regs.h"
#include "dcore0_sram_bank_10_regs.h"
#include "dcore0_sram_bank_11_regs.h"
#include "dcore0_sram_bank_12_regs.h"
#include "dcore0_sram_bank_13_regs.h"
#include "dcore0_sram_bank_14_regs.h"
#include "dcore0_sram_bank_15_regs.h"
#include "dcore0_tpc0_qm_cgm_regs.h"
#include "dcore0_tpc1_qm_cgm_regs.h"
#include "dcore0_tpc2_qm_cgm_regs.h"
#include "dcore0_tpc3_qm_cgm_regs.h"
#include "dcore0_tpc4_qm_cgm_regs.h"
#include "dcore0_vdec0_brdg_ctrl_regs.h"
#include "dcore0_vdec1_brdg_ctrl_regs.h"
#include "dcore0_vdec2_brdg_ctrl_regs.h"
#include "dcore0_vdec3_brdg_ctrl_regs.h"
#include "dcore0_vdec4_brdg_ctrl_regs.h"
#include "dcore0_vdec0_brdg_ctrl_axuser_arc_regs.h"
#include "dcore0_vdec0_brdg_ctrl_axuser_dec_regs.h"
#include "dcore0_vdec0_brdg_ctrl_axuser_msix_abnrm_regs.h"
#include "dcore0_vdec0_brdg_ctrl_axuser_msix_l2c_regs.h"
#include "dcore0_vdec0_brdg_ctrl_axuser_msix_nrm_regs.h"
#include "dcore0_vdec0_brdg_ctrl_axuser_msix_vcd_regs.h"
#include "dcore0_vdec1_brdg_ctrl_axuser_arc_regs.h"
#include "dcore0_vdec1_brdg_ctrl_axuser_dec_regs.h"
#include "dcore0_vdec1_brdg_ctrl_axuser_msix_abnrm_regs.h"
#include "dcore0_vdec1_brdg_ctrl_axuser_msix_l2c_regs.h"
#include "dcore0_vdec1_brdg_ctrl_axuser_msix_nrm_regs.h"
#include "dcore0_vdec1_brdg_ctrl_axuser_msix_vcd_regs.h"
#include "dcore0_vdec2_brdg_ctrl_axuser_arc_regs.h"
#include "dcore0_vdec2_brdg_ctrl_axuser_dec_regs.h"
#include "dcore0_vdec2_brdg_ctrl_axuser_msix_abnrm_regs.h"
#include "dcore0_vdec2_brdg_ctrl_axuser_msix_l2c_regs.h"
#include "dcore0_vdec2_brdg_ctrl_axuser_msix_nrm_regs.h"
#include "dcore0_vdec2_brdg_ctrl_axuser_msix_vcd_regs.h"
#include "dcore0_vdec3_brdg_ctrl_axuser_arc_regs.h"
#include "dcore0_vdec3_brdg_ctrl_axuser_dec_regs.h"
#include "dcore0_vdec3_brdg_ctrl_axuser_msix_abnrm_regs.h"
#include "dcore0_vdec3_brdg_ctrl_axuser_msix_l2c_regs.h"
#include "dcore0_vdec3_brdg_ctrl_axuser_msix_nrm_regs.h"
#include "dcore0_vdec3_brdg_ctrl_axuser_msix_vcd_regs.h"
#include "dcore0_vdec4_brdg_ctrl_axuser_arc_regs.h"
#include "dcore0_vdec4_brdg_ctrl_axuser_dec_regs.h"
#include "dcore0_vdec4_brdg_ctrl_axuser_msix_abnrm_regs.h"
#include "dcore0_vdec4_brdg_ctrl_axuser_msix_l2c_regs.h"
#include "dcore0_vdec4_brdg_ctrl_axuser_msix_nrm_regs.h"
#include "dcore0_vdec4_brdg_ctrl_axuser_msix_vcd_regs.h"
#include "dcore1_ddma_qm_cgm_regs.h"
#include "dcore1_hif0_regs.h"
#include "dcore1_hif1_regs.h"
#include "dcore1_hmmu0_scramb_out_regs.h"
#include "dcore1_hmmu1_scramb_out_regs.h"
#include "dcore1_kdma_core_kdma_cgm_regs.h"
#include "dcore1_mme_qm_cgm_regs.h"
#include "dcore1_mme_sbtea_regs.h"
#include "dcore1_mme_sbteb_regs.h"
#include "dcore1_mme_sbtel_regs.h"
#include "dcore1_mme_acc_regs.h"
#include "dcore1_mme_sram_l0_regs.h"
#include "dcore0_mme_ctrl_hi_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_act_pipe_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_agu_a_master_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_agu_a_slave_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_agu_b_master_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_agu_b_slave_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_agu_cin_master_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_agu_cin_slave_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_agu_cout_master_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_agu_cout_slave_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_agu_local_master_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_agu_local_slave_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_base_addr_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_non_tensor_end_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_non_tensor_start_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_tensor_a_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_tensor_b_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_tensor_cin_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_2_tensor_cout_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_act_pipe_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_agu_a_master_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_agu_a_slave_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_agu_b_master_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_agu_b_slave_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_agu_cin_master_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_agu_cin_slave_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_agu_cout_master_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_agu_cout_slave_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_agu_local_master_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_agu_local_slave_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_base_addr_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_non_tensor_end_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_non_tensor_start_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_tensor_a_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_tensor_b_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_tensor_cin_regs.h"
#include "dcore0_mme_ctrl_hi_shadow_3_tensor_cout_regs.h"
#include "dcore0_mme_ctrl_lo_arch_act_pipe_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_a_master_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_a_slave_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_b_master_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_b_slave_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_cin_master_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_cin_slave_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_cout_master_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_cout_slave_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_local_master_regs.h"
#include "dcore0_mme_ctrl_lo_arch_agu_local_slave_regs.h"
#include "dcore0_mme_ctrl_lo_arch_base_addr_regs.h"
#include "dcore0_mme_ctrl_lo_arch_non_tensor_end_regs.h"
#include "dcore0_mme_ctrl_lo_arch_non_tensor_start_regs.h"
#include "dcore0_mme_ctrl_lo_arch_tensor_a_regs.h"
#include "dcore0_mme_ctrl_lo_arch_tensor_b_regs.h"
#include "dcore0_mme_ctrl_lo_arch_tensor_cin_regs.h"
#include "dcore0_mme_ctrl_lo_arch_tensor_cout_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_act_pipe_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_agu_a_master_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_agu_a_slave_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_agu_b_master_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_agu_b_slave_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_agu_cin_master_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_agu_cin_slave_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_agu_cout_master_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_agu_cout_slave_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_agu_local_master_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_agu_local_slave_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_base_addr_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_non_tensor_end_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_non_tensor_start_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_tensor_a_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_tensor_b_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_tensor_cin_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_0_tensor_cout_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_act_pipe_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_agu_a_master_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_agu_a_slave_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_agu_b_master_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_agu_b_slave_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_agu_cin_master_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_agu_cin_slave_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_agu_cout_master_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_agu_cout_slave_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_agu_local_master_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_agu_local_slave_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_base_addr_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_non_tensor_end_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_non_tensor_start_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_tensor_a_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_tensor_b_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_tensor_cin_regs.h"
#include "dcore0_mme_ctrl_lo_shadow_1_tensor_cout_regs.h"
#include "dcore1_pdma0_qm_cgm_regs.h"
#include "dcore1_pdma1_qm_cgm_regs.h"
#include "dcore1_pdma0_core_kdma_cgm_regs.h"
#include "dcore1_pdma1_core_kdma_cgm_regs.h"
#include "dcore1_ddma_core_kdma_cgm_regs.h"
#include "dcore1_rot_qm_regs.h"
#include "dcore1_rot_qm_cgm_regs.h"
#include "dcore1_sram_bank_0_regs.h"
#include "dcore1_sram_bank_1_regs.h"
#include "dcore1_sram_bank_2_regs.h"
#include "dcore1_sram_bank_3_regs.h"
#include "dcore1_sram_bank_4_regs.h"
#include "dcore1_sram_bank_5_regs.h"
#include "dcore1_sram_bank_6_regs.h"
#include "dcore1_sram_bank_7_regs.h"
#include "dcore1_sram_bank_8_regs.h"
#include "dcore1_sram_bank_9_regs.h"
#include "dcore1_sram_bank_10_regs.h"
#include "dcore1_sram_bank_11_regs.h"
#include "dcore1_sram_bank_12_regs.h"
#include "dcore1_sram_bank_13_regs.h"
#include "dcore1_sram_bank_14_regs.h"
#include "dcore1_sram_bank_15_regs.h"
#include "dcore1_tpc0_qm_cgm_regs.h"
#include "dcore1_tpc1_qm_cgm_regs.h"
#include "dcore1_tpc2_qm_cgm_regs.h"
#include "dcore1_tpc3_qm_cgm_regs.h"
#include "dcore1_tpc4_qm_cgm_regs.h"
#include "dcore1_vdec0_brdg_ctrl_regs.h"
#include "dcore1_vdec1_brdg_ctrl_regs.h"
#include "dcore1_vdec2_brdg_ctrl_regs.h"
#include "dcore1_vdec3_brdg_ctrl_regs.h"
#include "dcore1_vdec4_brdg_ctrl_regs.h"
#include "pmmu_pif_regs.h"
#include "venc_vl2c_ctrl_regs.h"
#include "cpu_mstr_if_e2e_crdt_regs.h"
#include "dcore0_ddma_mstr_if_e2e_crdt_regs.h"
#include "dcore0_hmmu0_mstr_if_e2e_crdt_regs.h"
#include "dcore0_hmmu1_mstr_if_e2e_crdt_regs.h"
#include "dcore0_kdma_mstr_if_e2e_crdt_regs.h"
#include "dcore0_mme_sbtea_mstr_if_e2e_crdt_regs.h"
#include "dcore0_mme_sbteb_mstr_if_e2e_crdt_regs.h"
#include "dcore0_mme_sbtel_mstr_if_e2e_crdt_regs.h"
#include "dcore0_pdma0_mstr_if_e2e_crdt_regs.h"
#include "dcore0_pdma1_mstr_if_e2e_crdt_regs.h"
#include "dcore0_rot_mstr_if_e2e_crdt_regs.h"
#include "dcore0_sync_mngr_mstr_if_e2e_crdt_regs.h"
#include "dcore0_tpc0_mstr_if_e2e_crdt_regs.h"
#include "dcore0_tpc1_mstr_if_e2e_crdt_regs.h"
#include "dcore0_tpc2_mstr_if_e2e_crdt_regs.h"
#include "dcore0_tpc3_mstr_if_e2e_crdt_regs.h"
#include "dcore0_tpc4_mstr_if_e2e_crdt_regs.h"
#include "dcore0_vsi_wrap_mstr_if_e2e_crdt_regs.h"
#include "dcore1_ddma_mstr_if_e2e_crdt_regs.h"
#include "dcore1_hmmu0_mstr_if_e2e_crdt_regs.h"
#include "dcore1_hmmu1_mstr_if_e2e_crdt_regs.h"
#include "dcore1_kdma_mstr_if_e2e_crdt_regs.h"
#include "dcore1_mme_sbtea_mstr_if_e2e_crdt_regs.h"
#include "dcore1_mme_sbteb_mstr_if_e2e_crdt_regs.h"
#include "dcore1_mme_sbtel_mstr_if_e2e_crdt_regs.h"
#include "dcore1_pdma0_mstr_if_e2e_crdt_regs.h"
#include "dcore1_pdma1_mstr_if_e2e_crdt_regs.h"
#include "dcore1_rot_mstr_if_e2e_crdt_regs.h"
#include "dcore1_sync_mngr_mstr_if_e2e_crdt_regs.h"
#include "dcore1_tpc0_mstr_if_e2e_crdt_regs.h"
#include "dcore1_tpc1_mstr_if_e2e_crdt_regs.h"
#include "dcore1_tpc2_mstr_if_e2e_crdt_regs.h"
#include "dcore1_tpc3_mstr_if_e2e_crdt_regs.h"
#include "dcore1_tpc4_mstr_if_e2e_crdt_regs.h"
#include "dcore1_vsi_wrap_mstr_if_e2e_crdt_regs.h"
#include "i2c_s_mstr_if_e2e_crdt_regs.h"
#include "jt_mstr_if_e2e_crdt_regs.h"
#include "pcie_mstr_rr_mstr_if_e2e_crdt_regs.h"
#include "pmmu_hbw_mstr_if_e2e_crdt_regs.h"
#include "psoc_arc0_mstr_if_e2e_crdt_regs.h"
#include "psoc_arc1_mstr_if_e2e_crdt_regs.h"
#include "psoc_mstr_if_e2e_crdt_regs.h"
#include "smi_mstr_if_e2e_crdt_regs.h"
#include "psoc_mstr_if_core_hbw_regs.h"
#include "pmmu_hbw_mstr_if_core_hbw_regs.h"
#include "cpu_mstr_if_core_hbw_regs.h"
#include "dcore0_ddma_mstr_if_core_hbw_regs.h"
#include "dcore0_hmmu0_mstr_if_core_hbw_regs.h"
#include "dcore0_hmmu1_mstr_if_core_hbw_regs.h"
#include "dcore0_kdma_mstr_if_core_hbw_regs.h"
#include "dcore0_mme_sbtea_mstr_if_core_hbw_regs.h"
#include "dcore0_mme_sbteb_mstr_if_core_hbw_regs.h"
#include "dcore0_mme_sbtel_mstr_if_core_hbw_regs.h"
#include "dcore0_mme_sbtea_mstr_if_axuser_regs.h"
#include "dcore0_mme_sbteb_mstr_if_axuser_regs.h"
#include "dcore0_mme_sbtel_mstr_if_axuser_regs.h"
#include "dcore0_pdma0_mstr_if_core_hbw_regs.h"
#include "dcore0_pdma1_mstr_if_core_hbw_regs.h"
#include "dcore0_rot_mstr_if_core_hbw_regs.h"
#include "dcore0_sync_mngr_mstr_if_core_hbw_regs.h"
#include "dcore0_tpc0_mstr_if_core_hbw_regs.h"
#include "dcore0_tpc1_mstr_if_core_hbw_regs.h"
#include "dcore0_tpc2_mstr_if_core_hbw_regs.h"
#include "dcore0_tpc3_mstr_if_core_hbw_regs.h"
#include "dcore0_tpc4_mstr_if_core_hbw_regs.h"
#include "dcore0_vsi_wrap_mstr_if_core_hbw_regs.h"
#include "dcore1_ddma_mstr_if_core_hbw_regs.h"
#include "dcore1_hmmu0_mstr_if_core_hbw_regs.h"
#include "dcore1_hmmu1_mstr_if_core_hbw_regs.h"
#include "dcore1_kdma_mstr_if_core_hbw_regs.h"
#include "dcore1_mme_sbtea_mstr_if_core_hbw_regs.h"
#include "dcore1_mme_sbteb_mstr_if_core_hbw_regs.h"
#include "dcore1_mme_sbtel_mstr_if_core_hbw_regs.h"
#include "dcore1_pdma0_mstr_if_core_hbw_regs.h"
#include "dcore1_pdma1_mstr_if_core_hbw_regs.h"
#include "dcore1_rot_mstr_if_core_hbw_regs.h"
#include "dcore1_sync_mngr_mstr_if_core_hbw_regs.h"
#include "dcore1_tpc0_mstr_if_core_hbw_regs.h"
#include "dcore1_tpc1_mstr_if_core_hbw_regs.h"
#include "dcore1_tpc2_mstr_if_core_hbw_regs.h"
#include "dcore1_tpc3_mstr_if_core_hbw_regs.h"
#include "dcore1_tpc4_mstr_if_core_hbw_regs.h"
#include "dcore1_vsi_wrap_mstr_if_core_hbw_regs.h"
#include "pcie_mstr_rr_mstr_if_core_hbw_regs.h"
#include "dcon0_regs.h"
#include "dcon1_regs.h"
#include "dcon2_regs.h"
#include "dcon3_regs.h"
#include "dcore0_rot_regs.h"
#include "dcore1_rot_regs.h"
#include "dcore0_rot_desc_regs.h"
#include "dcore0_rot_mstr_if_axuser_regs.h"
#include "dcore0_vsi_wrap_regs.h"
#include "dcore1_vsi_wrap_regs.h"
#include "dcore0_vdec0_brdg_ctrl_special_regs.h"
#include "dcore0_vdec0_ctrl_special_regs.h"
#include "dcore0_dec0_cmd_regs.h"
#include "dcore0_ddr0_misc_regs.h"
#include "vsi_venc_regs.h"
#include "dcore0_tpc0_eml_tpc_cfg_kernel_regs.h"
#include "dcore1_kdma_core_regs.h"
#include "dcore1_ddma_core_regs.h"
#include "dcore1_pdma0_core_regs.h"
#include "dcore1_pdma1_core_regs.h"
#include "dcore0_mme_mstr_if_e2e_crdt_regs.h"
#include "dcore1_mme_mstr_if_e2e_crdt_regs.h"
#include "dcore0_mme_mstr_if_core_lbw_regs.h"
#include "dcore0_mme_mstr_if_core_hbw_regs.h"
#include "dcore1_mme_mstr_if_core_hbw_regs.h"
#include "dcore0_tpc0_mstr_if_core_lbw_regs.h"
#include "dcore0_tpc0_cfg_special_regs.h"

#include "dcore0_ddma_core_masks.h"
#include "dcore0_ddma_qm_masks.h"
#include "dcore0_hmmu0_mmu_masks.h"
#include "dcore0_hmmu0_stlb_masks.h"
#include "dcore0_hmmu0_scramb_out_masks.h"
#include "dcore0_kdma_core_masks.h"
#include "dcore0_kdma_mstr_if_axuser_masks.h"
#include "dcore0_kdma_core_ctx_masks.h"
#include "dcore0_mme_qm_masks.h"
#include "dcore0_pdma0_core_masks.h"
#include "dcore0_pdma0_qm_masks.h"
#include "dcore0_tpc0_qm_masks.h"
#include "pcie_wrap_masks.h"
#include "psoc_global_conf_masks.h"
#include "dcore0_tpcif_rtr3_masks.h"
#include "dcore1_tpcif_rtr3_masks.h"
#include "dcore0_mme_acc_masks.h"
#include "cpu_ca53_cfg_masks.h"
#include "dcon0_masks.h"
#include "dcore0_ddma_mstr_if_special_masks.h"
#include "dcore0_vdec0_brdg_ctrl_masks.h"
#include "dcore0_dec0_cmd_masks.h"
#include "dcore0_ddma_core_kdma_cgm_masks.h"
#include "dcore0_tpc0_cfg_masks.h"
#include "dcore0_dec0_vsi_masks.h"
#include "venc_vl2c_ctrl_masks.h"
#include "dcore0_mme_ctrl_lo_masks.h"
#include "dcore1_ddma_core_masks.h"
#include "dcore0_sync_mngr_objs_masks.h"
#include "dcore0_rot_masks.h"
#include "dcore0_sync_mngr_glbl_masks.h"
#include "dcore0_vdec0_ctrl_special_masks.h"

#define mmPCIE_DBI_DEVICE_ID_VENDOR_ID_REG                           0xC02000
#define mmPCIE_DBI_MSIX_DOORBELL_OFF                                 0xC02948

#define PLL_CTRL_CFG_OFFS		(mmPSOC_PCI_PLL_CTRL_CFG - \
						mmPSOC_PCI_PLL_CTRL_BASE)
#define PLL_CTRL_CFG_DIV_OFFS		(mmPSOC_PCI_PLL_CTRL_CFG_DIV - \
						mmPSOC_PCI_PLL_CTRL_BASE)
#define PLL_CTRL_DATA_CHNG_OFFS		(mmPSOC_PCI_PLL_CTRL_DATA_CHNG - \
						mmPSOC_PCI_PLL_CTRL_BASE)

#define PLL_CTRL_DIV_FACTOR_0_OFFS	\
	(mmPSOC_PCI_PLL_CTRL_DIV_FACTOR_0 - mmPSOC_PCI_PLL_CTRL_BASE)

#define PLL_CTRL_DIV_FACTOR_CMD_0_OFFS	\
	(mmPSOC_PCI_PLL_CTRL_DIV_FACTOR_CMD_0 - mmPSOC_PCI_PLL_CTRL_BASE)

#define PLL_CTRL_DIV_SEL_0_OFFS		(mmPSOC_PCI_PLL_CTRL_DIV_SEL_0 - \
						mmPSOC_PCI_PLL_CTRL_BASE)
#define PLL_CTRL_DIV_EN_0_OFFS		(mmPSOC_PCI_PLL_CTRL_DIV_EN_0 - \
						mmPSOC_PCI_PLL_CTRL_BASE)

#define QM_GLBL_CFG0_OFFSET		(mmDCORE0_PDMA0_QM_GLBL_CFG0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_GLBL_CFG1_OFFSET		(mmDCORE0_PDMA0_QM_GLBL_CFG1 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_GLBL_PROT_OFFSET		(mmDCORE0_PDMA0_QM_GLBL_PROT - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_GLBL_ERR_CFG_OFFSET		(mmDCORE0_PDMA0_QM_GLBL_ERR_CFG - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQ_BASE_LO_0_OFFSET		(mmDCORE0_PDMA0_QM_PQ_BASE_LO_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQ_BASE_HI_0_OFFSET		(mmDCORE0_PDMA0_QM_PQ_BASE_HI_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQ_SIZE_0_OFFSET		(mmDCORE0_PDMA0_QM_PQ_SIZE_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQ_PI_0_OFFSET		(mmDCORE0_PDMA0_QM_PQ_PI_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQ_CI_0_OFFSET		(mmDCORE0_PDMA0_QM_PQ_CI_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_CP_MSG_BASE0_ADDR_LO_0_OFFSET \
	(mmDCORE0_PDMA0_QM_CP_MSG_BASE0_ADDR_LO_0 - mmDCORE0_PDMA0_QM_BASE)

#define QM_CP_MSG_BASE0_ADDR_HI_0_OFFSET \
	(mmDCORE0_PDMA0_QM_CP_MSG_BASE0_ADDR_HI_0 - mmDCORE0_PDMA0_QM_BASE)

#define QM_CP_MSG_BASE1_ADDR_LO_0_OFFSET \
	(mmDCORE0_PDMA0_QM_CP_MSG_BASE1_ADDR_LO_0 - mmDCORE0_PDMA0_QM_BASE)

#define QM_CP_MSG_BASE1_ADDR_HI_0_OFFSET \
	(mmDCORE0_PDMA0_QM_CP_MSG_BASE1_ADDR_HI_0 - mmDCORE0_PDMA0_QM_BASE)

#define QM_CP_FENCE0_CNT_0_OFFSET	(mmDCORE0_PDMA0_QM_CP_FENCE0_CNT_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQC_HBW_BASE_LO_0_OFFSET	(mmDCORE0_PDMA0_QM_PQC_HBW_BASE_LO_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQC_HBW_BASE_HI_0_OFFSET	(mmDCORE0_PDMA0_QM_PQC_HBW_BASE_HI_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQC_SIZE_0_OFFSET		(mmDCORE0_PDMA0_QM_PQC_SIZE_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQC_PI_0_OFFSET		(mmDCORE0_PDMA0_QM_PQC_PI_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQC_LBW_WDATA_0_OFFSET	(mmDCORE0_PDMA0_QM_PQC_LBW_WDATA_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQC_LBW_BASE_LO_0_OFFSET	(mmDCORE0_PDMA0_QM_PQC_LBW_BASE_LO_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQC_LBW_BASE_HI_0_OFFSET	(mmDCORE0_PDMA0_QM_PQC_LBW_BASE_HI_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_PQC_CFG_OFFSET		(mmDCORE0_PDMA0_QM_PQC_CFG - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_ARB_CFG_0_OFFSET		(mmDCORE0_PDMA0_QM_ARB_CFG_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_ARB_SLV_CHOICE_WDT \
	(mmDCORE0_PDMA0_QM_ARB_SLV_CHOICE_WDT - mmDCORE0_PDMA0_QM_BASE)

#define QM_ARB_ERR_MSG_EN_OFFSET	(mmDCORE0_PDMA0_QM_ARB_ERR_MSG_EN - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_GLBL_ERR_ADDR_LO_OFFSET	(mmDCORE0_PDMA0_QM_GLBL_ERR_ADDR_LO - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_GLBL_ERR_ADDR_HI_OFFSET	(mmDCORE0_PDMA0_QM_GLBL_ERR_ADDR_HI - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_GLBL_ERR_WDATA_OFFSET	(mmDCORE0_PDMA0_QM_GLBL_ERR_WDATA - \
							mmDCORE0_PDMA0_QM_BASE)

#define QM_FENCE2_OFFSET		(mmDCORE0_PDMA0_QM_CP_FENCE2_RDATA_0 - \
							mmDCORE0_PDMA0_QM_BASE)

#define DMA_CORE_CFG_0_OFFSET		(mmDCORE0_PDMA0_CORE_CFG_0 - \
						mmDCORE0_PDMA0_CORE_BASE)

#define DMA_CORE_CFG_1_OFFSET		(mmDCORE0_PDMA0_CORE_CFG_1 - \
						mmDCORE0_PDMA0_CORE_BASE)

#define DMA_CORE_PROT_OFFSET		(mmDCORE0_PDMA0_CORE_PROT - \
						mmDCORE0_PDMA0_CORE_BASE)

#define DMA_CORE_WR_HBW_MAX_AWID_OFFSET	(mmDCORE0_PDMA0_CORE_WR_HBW_MAX_AWID - \
						mmDCORE0_PDMA0_CORE_BASE)

#define DMA_CORE_ERR_CFG_OFFSET		(mmDCORE0_PDMA0_CORE_ERR_CFG - \
						mmDCORE0_PDMA0_CORE_BASE)

#define DMA_CORE_ERRMSG_ADDR_LO_OFFSET	(mmDCORE0_PDMA0_CORE_ERRMSG_ADDR_LO - \
						mmDCORE0_PDMA0_CORE_BASE)

#define DMA_CORE_ERRMSG_ADDR_HI_OFFSET	(mmDCORE0_PDMA0_CORE_ERRMSG_ADDR_HI - \
						mmDCORE0_PDMA0_CORE_BASE)

#define DMA_CORE_ERRMSG_WDATA_OFFSET	(mmDCORE0_PDMA0_CORE_ERRMSG_WDATA - \
						mmDCORE0_PDMA0_CORE_BASE)

#define DMA_CORE_WR_COMP_MAX_OUTSTAND_OFFSET \
	(mmDCORE0_PDMA0_CORE_WR_COMP_MAX_OUTSTAND - mmDCORE0_PDMA0_CORE_BASE)

#define TPC_CFG_SM_BASE_ADDRESS_HIGH_OFFSET \
	(mmDCORE0_TPC0_CFG_SM_BASE_ADDRESS_HIGH - mmDCORE0_TPC0_CFG_BASE)

#define TPC_CFG_STALL_ON_ERR_OFFSET \
	(mmDCORE0_TPC0_CFG_STALL_ON_ERR - mmDCORE0_TPC0_CFG_BASE)

#define TPC_CFG_TPC_INTR_MASK_OFFSET \
	(mmDCORE0_TPC0_CFG_TPC_INTR_MASK - mmDCORE0_TPC0_CFG_BASE)

#define TPC_CFG_MSS_CONFIG_OFFSET \
	(mmDCORE0_TPC0_CFG_MSS_CONFIG - mmDCORE0_TPC0_CFG_BASE)

#define MME_ACC_INTR_MASK_OFFSET \
	(mmDCORE0_MME_ACC_INTR_MASK - mmDCORE0_MME_ACC_BASE)

#define MME_ACC_WBC_MAX_OFFSET \
	(mmDCORE0_MME_ACC_WBC_MAX - mmDCORE0_MME_ACC_BASE)

#define MME_CTRL_LO_EU_OFFSET \
	(mmDCORE0_MME_CTRL_LO_EU - mmDCORE0_MME_CTRL_LO_BASE)

#define MME_CTRL_LO_REDUN_OFFSET \
	(mmDCORE0_MME_CTRL_LO_REDUN - mmDCORE0_MME_CTRL_LO_BASE)

#define MME_CTRL_LO_FMA_FUNC_REDUN_CLK_EN32_OFFSET \
	(mmDCORE0_MME_CTRL_LO_FMA_FUNC_REDUN_CLK_EN32 - \
			mmDCORE0_MME_CTRL_LO_BASE)

#define MME_CTRL_LO_FMA_FUNC_REDUN_CLK_EN33_OFFSET \
	(mmDCORE0_MME_CTRL_LO_FMA_FUNC_REDUN_CLK_EN33 - \
			mmDCORE0_MME_CTRL_LO_BASE)

#define MME_CTRL_LO_IMA_FUNC_REDUN_CLK_EN32_OFFSET \
	(mmDCORE0_MME_CTRL_LO_IMA_FUNC_REDUN_CLK_EN32 - \
			mmDCORE0_MME_CTRL_LO_BASE)

#define MME_CTRL_LO_IMA_FUNC_REDUN_CLK_EN33_OFFSET \
	(mmDCORE0_MME_CTRL_LO_IMA_FUNC_REDUN_CLK_EN33 - \
			mmDCORE0_MME_CTRL_LO_BASE)

#define MME_CTRL_LO_EU_ISOLATION_DIS_OFFSET \
	(mmDCORE0_MME_CTRL_LO_EU_ISOLATION_DIS - mmDCORE0_MME_CTRL_LO_BASE)

#define MME_CTRL_LO_QM_SLV_CLK_EN_OFFSET \
	(mmDCORE0_MME_CTRL_LO_QM_SLV_CLK_EN - mmDCORE0_MME_CTRL_LO_BASE)

#define MME_CTRL_LO_FENCE_DUP_COLOR0_ADD_LO_OFFSET \
	(mmDCORE0_MME_CTRL_LO_FENCE_DUP_COLOR0_ADD_LO - \
			mmDCORE0_MME_CTRL_LO_BASE)

#define MME_CTRL_LO_FENCE_DUP_COLOR0_ADD_HI_OFFSET \
	(mmDCORE0_MME_CTRL_LO_FENCE_DUP_COLOR0_ADD_HI - \
			mmDCORE0_MME_CTRL_LO_BASE)

#define MME_CTRL_LO_MME_AXUSER_HB_OVRD_LO_OFFSET \
	(mmDCORE0_MME_CTRL_LO_MME_AXUSER_HB_OVRD_LO - mmDCORE0_MME_CTRL_LO_BASE)

#define MME_CTRL_LO_MME_AXUSER_HB_OVRD_HI_OFFSET \
	(mmDCORE0_MME_CTRL_LO_MME_AXUSER_HB_OVRD_HI - mmDCORE0_MME_CTRL_LO_BASE)

#define MME_CTRL_LO_TWO_MASTERS_MODE_OFFSET \
	(mmDCORE0_MME_CTRL_LO_TWO_MASTERS_MODE_NON_SEC - \
			mmDCORE0_MME_CTRL_LO_BASE)

#define ROT_ERR_CFG_OFFSET \
	(mmDCORE0_ROT_ERR_CFG - mmDCORE0_ROT_BASE)

#define ROT_RSB_CAM_MAX_SIZE_OFFSET \
	(mmDCORE0_ROT_RSB_CAM_MAX_SIZE - mmDCORE0_ROT_BASE)

#define MMU_OFFSET(reg)			((reg) - mmDCORE0_HMMU0_MMU_BASE)
#define STLB_OFFSET(reg)		((reg) - mmDCORE0_HMMU0_STLB_BASE)

#define MMU_ENABLE_OFFSET		MMU_OFFSET(mmDCORE0_HMMU0_MMU_MMU_ENABLE)
#define MMU_BYPASS_OFFSET		MMU_OFFSET(mmDCORE0_HMMU0_MMU_MMU_BYPASS)
#define MMU_SPI_SEI_MASK_OFFSET		MMU_OFFSET(mmDCORE0_HMMU0_MMU_SPI_SEI_MASK)
#define MMU_SPI_SEI_CAUSE_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_SPI_SEI_CAUSE)
#define MMU_PAGE_ERROR_CAPTURE_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_PAGE_ERROR_CAPTURE)
#define MMU_PAGE_ERROR_CAPTURE_VA_OFFSET \
					MMU_OFFSET(mmDCORE0_HMMU0_MMU_PAGE_ERROR_CAPTURE_VA)
#define MMU_ACCESS_ERROR_CAPTURE_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_ACCESS_ERROR_CAPTURE)
#define MMU_ACCESS_ERROR_CAPTURE_VA_OFFSET \
					MMU_OFFSET(mmDCORE0_HMMU0_MMU_ACCESS_ERROR_CAPTURE_VA)
#define MMU_INTERRUPT_CLR_OFFSET	MMU_OFFSET(mmDCORE0_HMMU0_MMU_INTERRUPT_CLR)
#define MMU_ADDR_SHIFT			MMU_OFFSET( \
						mmDCORE0_HMMU0_SCRAMB_OUT_DDR_ADDRESS_SHIFT_ALIGN)

#define STLB_BUSY_OFFSET		STLB_OFFSET(mmDCORE0_HMMU0_STLB_BUSY)
#define STLB_ASID_OFFSET		STLB_OFFSET(mmDCORE0_HMMU0_STLB_ASID)
#define STLB_HOP0_PA43_12_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_HOP0_PA43_12)
#define STLB_HOP0_PA49_44_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_HOP0_PA49_44)
#define STLB_HOP_CONFIGURATION_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_HOP_CONFIGURATION)
#define STLB_INV_ALL_START_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_INV_ALL_START)
#define STLB_SRAM_INIT_OFFSET		STLB_OFFSET(mmDCORE0_HMMU0_STLB_SRAM_INIT)
#define STLB_RANGE_CACHE_INVALIDATION_OFFSET \
					STLB_OFFSET(mmDCORE0_HMMU0_STLB_RANGE_CACHE_INVALIDATION)
#define STLB_RANGE_INV_START_LSB_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_RANGE_INV_START_RANGE_LSB)
#define STLB_RANGE_INV_START_MSB_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_RANGE_INV_START_RANGE_MSB)
#define STLB_RANGE_INV_END_LSB_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_RANGE_INV_END_RANGE_LSB)
#define STLB_RANGE_INV_END_MSB_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_RANGE_INV_END_RANGE_MSB)
#define STLB_RANGE_MEM_CACHE_INV_OFFSET	STLB_OFFSET(mmDCORE0_HMMU0_STLB_MEM_CACHE_INVALIDATION)
#define STLB_RANGE_MEM_CACHE_INV_STATUS_OFFSET \
					STLB_OFFSET(mmDCORE0_HMMU0_STLB_MEM_CACHE_INV_STATUS)

/* shorten masks names */
#define SHIFT_ALIGN_WR_SHIFT_EN_SHIFT \
	DCORE0_HMMU0_SCRAMB_OUT_DDR_ADDRESS_SHIFT_ALIGN_WR_SHIFT_EN_SHIFT
#define SHIFT_ALIGN_WR_SHIFT_OFFSET_SHIFT \
	DCORE0_HMMU0_SCRAMB_OUT_DDR_ADDRESS_SHIFT_ALIGN_WR_SHIFT_OFFSET_SHIFT
#define SHIFT_ALIGN_WR_SHIFT_WIDTH_SHIFT \
	DCORE0_HMMU0_SCRAMB_OUT_DDR_ADDRESS_SHIFT_ALIGN_WR_SHIFT_WIDTH_SHIFT
#define SHIFT_ALIGN_RD_SHIFT_EN_SHIFT \
	DCORE0_HMMU0_SCRAMB_OUT_DDR_ADDRESS_SHIFT_ALIGN_RD_SHIFT_EN_SHIFT
#define SHIFT_ALIGN_RD_SHIFT_OFFSET_SHIFT \
	DCORE0_HMMU0_SCRAMB_OUT_DDR_ADDRESS_SHIFT_ALIGN_RD_SHIFT_OFFSET_SHIFT
#define SHIFT_ALIGN_RD_SHIFT_WIDTH_SHIFT \
	DCORE0_HMMU0_SCRAMB_OUT_DDR_ADDRESS_SHIFT_ALIGN_RD_SHIFT_WIDTH_SHIFT

#define SM_OBJS_PROT_BITS_OFFS 0x4800

#define MMU_STATIC_MULTI_PAGE_SIZE_OFFSET	(mmDCORE0_HMMU0_MMU_STATIC_MULTI_PAGE_SIZE - \
							mmDCORE0_HMMU0_MMU_BASE)

#define LBW_DECODE_BASE_ADDR_OFFSET (mmDCON0_HBW_RTR_IF0_RTR_CTRL_LBW_DECODE_BASE_ADDR_0 - \
							mmDCON0_HBW_RTR_IF0_RTR_CTRL_BASE)

#define LBW_DECODE_CTRL_OFFSET (mmDCON0_HBW_RTR_IF0_RTR_CTRL_LBW_DECODE_CTRL_0 - \
							mmDCON0_HBW_RTR_IF0_RTR_CTRL_BASE)

#define DCON_OFFSET (mmDCON1_LBW_RTR_IF_RTR_CTRL_BASE - mmDCON0_LBW_RTR_IF_RTR_CTRL_BASE)

#define RTR_OFFSET (mmDCORE0_TPCIF_RTR1_CTRL_BASE - mmDCORE0_TPCIF_RTR0_CTRL_BASE)

#endif /* ASIC_REG_GRECO_REGS_H_ */
