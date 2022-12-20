// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2019-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "grecoP.h"
#include "greco_masks.h"
#include "../include/greco/greco_reg_map.h"
#include "../include/greco/greco_coresight.h"

#include <uapi/drm/habanalabs_accel.h>

#define GRECO_PLDM_CORESIGHT_TIMEOUT_USEC	(CORESIGHT_TIMEOUT_USEC * 100)

#define SPMU_SECTION_SIZE		DCORE0_DDMA_CS_SPMU_MAX_OFFSET
#define SPMU_EVENT_TYPES_OFFSET		0x400
#define SPMU_MAX_COUNTERS		6

static u64 debug_stm_regs[GRECO_STM_LAST + 1] = {
	[GRECO_STM_DCORE0_MME_SBTEA]	= mmDCORE0_MME_SBTEA_STM_BASE,
	[GRECO_STM_DCORE0_MME_SBTEB]	= mmDCORE0_MME_SBTEB_STM_BASE,
	[GRECO_STM_DCORE0_MME_SBTEL]	= mmDCORE0_MME_SBTEL_STM_BASE,
	[GRECO_STM_DCORE0_VSI_WRAP_CS]	= mmDCORE0_VSI_WRAP_CS_STM_BASE,
	[GRECO_STM_DCORE0_DDMA_CS]	= mmDCORE0_DDMA_CS_STM_BASE,
	[GRECO_STM_DCORE0_ROT_CS]	= mmDCORE0_ROT_CS_STM_BASE,
	[GRECO_STM_DCORE0_PDMA0_CS]	= mmDCORE0_PDMA0_CS_STM_BASE,
	[GRECO_STM_DCORE0_PDMA1_CS]	= mmDCORE0_PDMA1_CS_STM_BASE,
	[GRECO_STM_DCORE0_KDMA_CS]	= mmDCORE0_KDMA_CS_STM_BASE,
	[GRECO_STM_DCORE0_HMMU0_CS]	= mmDCORE0_HMMU0_CS_STM_BASE,
	[GRECO_STM_DCORE0_HMMU1_CS]	= mmDCORE0_HMMU1_CS_STM_BASE,
	[GRECO_STM_DCORE0_HIF0_CS]	= mmDCORE0_HIF0_CS_STM_BASE,
	[GRECO_STM_DCORE0_HIF1_CS]	= mmDCORE0_HIF1_CS_STM_BASE,
	[GRECO_STM_DCORE0_MME_CTRL]	= mmDCORE0_MME_CTRL_STM_BASE,
	[GRECO_STM_DCORE1_MME_SBTEA]	= mmDCORE1_MME_SBTEA_STM_BASE,
	[GRECO_STM_DCORE1_MME_SBTEB]	= mmDCORE1_MME_SBTEB_STM_BASE,
	[GRECO_STM_DCORE1_MME_SBTEL]	= mmDCORE1_MME_SBTEL_STM_BASE,
	[GRECO_STM_DCORE1_VSI_WRAP_CS]	= mmDCORE1_VSI_WRAP_CS_STM_BASE,
	[GRECO_STM_DCORE1_DDMA_CS]	= mmDCORE1_DDMA_CS_STM_BASE,
	[GRECO_STM_DCORE1_ROT_CS]	= mmDCORE1_ROT_CS_STM_BASE,
	[GRECO_STM_DCORE1_PDMA0_CS]	= mmDCORE1_PDMA0_CS_STM_BASE,
	[GRECO_STM_DCORE1_PDMA1_CS]	= mmDCORE1_PDMA1_CS_STM_BASE,
	[GRECO_STM_DCORE1_KDMA_CS]	= mmDCORE1_KDMA_CS_STM_BASE,
	[GRECO_STM_DCORE1_HMMU0_CS]	= mmDCORE1_HMMU0_CS_STM_BASE,
	[GRECO_STM_DCORE1_HMMU1_CS]	= mmDCORE1_HMMU1_CS_STM_BASE,
	[GRECO_STM_DCORE1_HIF0_CS]	= mmDCORE1_HIF0_CS_STM_BASE,
	[GRECO_STM_DCORE1_HIF1_CS]	= mmDCORE1_HIF1_CS_STM_BASE,
	[GRECO_STM_DCORE1_MME_CTRL]	= mmDCORE1_MME_CTRL_STM_BASE,
	[GRECO_STM_PCIE]		= mmPCIE_STM_BASE,
	[GRECO_STM_PSOC]		= mmPSOC_STM_BASE,
	[GRECO_STM_PSOC_ARC0_CS]	= mmPSOC_ARC0_CS_STM_BASE,
	[GRECO_STM_PSOC_ARC1_CS]	= mmPSOC_ARC1_CS_STM_BASE,
	[GRECO_STM_CPU]			= mmCPU_STM_BASE,
	[GRECO_STM_PMMU_CS]		= mmPMMU_CS_STM_BASE,
	[GRECO_STM_DCORE0_TPC0_EML]	= mmDCORE0_TPC0_EML_STM_BASE,
	[GRECO_STM_DCORE0_TPC1_EML]	= mmDCORE0_TPC1_EML_STM_BASE,
	[GRECO_STM_DCORE0_TPC2_EML]	= mmDCORE0_TPC2_EML_STM_BASE,
	[GRECO_STM_DCORE0_TPC3_EML]	= mmDCORE0_TPC3_EML_STM_BASE,
	[GRECO_STM_DCORE0_TPC4_EML]	= mmDCORE0_TPC4_EML_STM_BASE,
	[GRECO_STM_DCORE1_TPC0_EML]	= mmDCORE1_TPC0_EML_STM_BASE,
	[GRECO_STM_DCORE1_TPC1_EML]	= mmDCORE1_TPC1_EML_STM_BASE,
	[GRECO_STM_DCORE1_TPC2_EML]	= mmDCORE1_TPC2_EML_STM_BASE,
	[GRECO_STM_DCORE1_TPC3_EML]	= mmDCORE1_TPC3_EML_STM_BASE,
	[GRECO_STM_DCORE1_TPC4_EML]	= mmDCORE1_TPC4_EML_STM_BASE,
};

static u64 debug_etf_regs[GRECO_ETF_LAST + 1] = {
	[GRECO_ETF_DCORE0_MME_SBTEA]	= mmDCORE0_MME_SBTEA_ETF_BASE,
	[GRECO_ETF_DCORE0_MME_SBTEB]	= mmDCORE0_MME_SBTEB_ETF_BASE,
	[GRECO_ETF_DCORE0_MME_SBTEL]	= mmDCORE0_MME_SBTEL_ETF_BASE,
	[GRECO_ETF_DCORE0_VSI_WRAP_CS]	= mmDCORE0_VSI_WRAP_CS_ETF_BASE,
	[GRECO_ETF_DCORE0_DDMA_CS]	= mmDCORE0_DDMA_CS_ETF_BASE,
	[GRECO_ETF_DCORE0_ROT_CS]	= mmDCORE0_ROT_CS_ETF_BASE,
	[GRECO_ETF_DCORE0_PDMA0_CS]	= mmDCORE0_PDMA0_CS_ETF_BASE,
	[GRECO_ETF_DCORE0_PDMA1_CS]	= mmDCORE0_PDMA1_CS_ETF_BASE,
	[GRECO_ETF_DCORE0_KDMA_CS]	= mmDCORE0_KDMA_CS_ETF_BASE,
	[GRECO_ETF_DCORE0_HMMU0_CS]	= mmDCORE0_HMMU0_CS_ETF_BASE,
	[GRECO_ETF_DCORE0_HMMU1_CS]	= mmDCORE0_HMMU1_CS_ETF_BASE,
	[GRECO_ETF_DCORE0_HIF0_CS]	= mmDCORE0_HIF0_CS_ETF_BASE,
	[GRECO_ETF_DCORE0_HIF1_CS]	= mmDCORE0_HIF1_CS_ETF_BASE,
	[GRECO_ETF_DCORE0_MME_CTRL]	= mmDCORE0_MME_CTRL_ETF_BASE,
	[GRECO_ETF_DCORE1_MME_SBTEA]	= mmDCORE1_MME_SBTEA_ETF_BASE,
	[GRECO_ETF_DCORE1_MME_SBTEB]	= mmDCORE1_MME_SBTEB_ETF_BASE,
	[GRECO_ETF_DCORE1_MME_SBTEL]	= mmDCORE1_MME_SBTEL_ETF_BASE,
	[GRECO_ETF_DCORE1_VSI_WRAP_CS]	= mmDCORE1_VSI_WRAP_CS_ETF_BASE,
	[GRECO_ETF_DCORE1_DDMA_CS]	= mmDCORE1_DDMA_CS_ETF_BASE,
	[GRECO_ETF_DCORE1_ROT_CS]	= mmDCORE1_ROT_CS_ETF_BASE,
	[GRECO_ETF_DCORE1_PDMA0_CS]	= mmDCORE1_PDMA0_CS_ETF_BASE,
	[GRECO_ETF_DCORE1_PDMA1_CS]	= mmDCORE1_PDMA1_CS_ETF_BASE,
	[GRECO_ETF_DCORE1_KDMA_CS]	= mmDCORE1_KDMA_CS_ETF_BASE,
	[GRECO_ETF_DCORE1_HMMU0_CS]	= mmDCORE1_HMMU0_CS_ETF_BASE,
	[GRECO_ETF_DCORE1_HMMU1_CS]	= mmDCORE1_HMMU1_CS_ETF_BASE,
	[GRECO_ETF_DCORE1_HIF0_CS]	= mmDCORE1_HIF0_CS_ETF_BASE,
	[GRECO_ETF_DCORE1_HIF1_CS]	= mmDCORE1_HIF1_CS_ETF_BASE,
	[GRECO_ETF_DCORE1_MME_CTRL]	= mmDCORE1_MME_CTRL_ETF_BASE,
	[GRECO_ETF_PCIE]		= mmPCIE_ETF_BASE,
	[GRECO_ETF_PSOC]		= mmPSOC_ETF_BASE,
	[GRECO_ETF_PSOC_ARC0_CS]	= mmPSOC_ARC0_CS_ETF_BASE,
	[GRECO_ETF_PSOC_ARC1_CS]	= mmPSOC_ARC1_CS_ETF_BASE,
	[GRECO_ETF_CPU_0]		= mmCPU_ETF_0_BASE,
	[GRECO_ETF_CPU_1]		= mmCPU_ETF_1_BASE,
	[GRECO_ETF_CPU_TRACE]		= mmCPU_ETF_TRACE_BASE,
	[GRECO_ETF_PMMU_CS]		= mmPMMU_CS_ETF_BASE,
	[GRECO_ETF_DCORE0_TPC0_EML]	= mmDCORE0_TPC0_EML_ETF_BASE,
	[GRECO_ETF_DCORE0_TPC1_EML]	= mmDCORE0_TPC1_EML_ETF_BASE,
	[GRECO_ETF_DCORE0_TPC2_EML]	= mmDCORE0_TPC2_EML_ETF_BASE,
	[GRECO_ETF_DCORE0_TPC3_EML]	= mmDCORE0_TPC3_EML_ETF_BASE,
	[GRECO_ETF_DCORE0_TPC4_EML]	= mmDCORE0_TPC4_EML_ETF_BASE,
	[GRECO_ETF_DCORE1_TPC0_EML]	= mmDCORE1_TPC0_EML_ETF_BASE,
	[GRECO_ETF_DCORE1_TPC1_EML]	= mmDCORE1_TPC1_EML_ETF_BASE,
	[GRECO_ETF_DCORE1_TPC2_EML]	= mmDCORE1_TPC2_EML_ETF_BASE,
	[GRECO_ETF_DCORE1_TPC3_EML]	= mmDCORE1_TPC3_EML_ETF_BASE,
	[GRECO_ETF_DCORE1_TPC4_EML]	= mmDCORE1_TPC4_EML_ETF_BASE
};

static u64 debug_funnel_regs[GRECO_FUNNEL_LAST + 1] = {
	[GRECO_FUNNEL_DCORE0_TPC_CH_0]	= mmDCORE0_TPC_CH_FUNNEL0_BASE,
	[GRECO_FUNNEL_DCORE0_TPC_CH_1]	= mmDCORE0_TPC_CH_FUNNEL1_BASE,
	[GRECO_FUNNEL_DCORE0_TPC_CH_2]	= mmDCORE0_TPC_CH_FUNNEL2_BASE,
	[GRECO_FUNNEL_DCORE0_TPC_CH_3]	= mmDCORE0_TPC_CH_FUNNEL3_BASE,
	[GRECO_FUNNEL_DCORE0_XDMA]	= mmDCORE0_XDMA_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE0_HIF0]	= mmDCORE0_HIF0_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE0_HIF1]	= mmDCORE0_HIF1_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE1_TPC_CH_0]	= mmDCORE1_TPC_CH_FUNNEL0_BASE,
	[GRECO_FUNNEL_DCORE1_TPC_CH_1]	= mmDCORE1_TPC_CH_FUNNEL1_BASE,
	[GRECO_FUNNEL_DCORE1_TPC_CH_2]	= mmDCORE1_TPC_CH_FUNNEL2_BASE,
	[GRECO_FUNNEL_DCORE1_TPC_CH_3]	= mmDCORE1_TPC_CH_FUNNEL3_BASE,
	[GRECO_FUNNEL_DCORE1_XDMA]	= mmDCORE1_XDMA_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE1_HIF0]	= mmDCORE1_HIF0_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE1_HIF1]	= mmDCORE1_HIF1_FUNNEL_BASE,
	[GRECO_FUNNEL_PCIE]		= mmPCIE_FUNNEL_BASE,
	[GRECO_FUNNEL_PSOC]		= mmPSOC_FUNNEL_BASE,
	[GRECO_FUNNEL_PSOC_ARC0]	= mmPSOC_ARC0_FUNNEL_BASE,
	[GRECO_FUNNEL_PSOC_ARC1]	= mmPSOC_ARC1_FUNNEL_BASE,
	[GRECO_FUNNEL_DCON0_0]		= mmDCON0_FUNNEL_0_BASE,
	[GRECO_FUNNEL_DCON0_1]		= mmDCON0_FUNNEL_1_BASE,
	[GRECO_FUNNEL_DCON1_0]		= mmDCON1_FUNNEL_0_BASE,
	[GRECO_FUNNEL_DCON1_1]		= mmDCON1_FUNNEL_1_BASE,
	[GRECO_FUNNEL_DCON2_0]		= mmDCON2_FUNNEL_0_BASE,
	[GRECO_FUNNEL_DCON2_1]		= mmDCON2_FUNNEL_1_BASE,
	[GRECO_FUNNEL_DCON3_0]		= mmDCON3_FUNNEL_0_BASE,
	[GRECO_FUNNEL_DCON3_1]		= mmDCON3_FUNNEL_1_BASE,
	[GRECO_FUNNEL_CPU]		= mmCPU_FUNNEL_BASE,
	[GRECO_FUNNEL_PMMU]		= mmPMMU_FUNNEL_BASE,
	[GRECO_FUNNEL_XIF]		= mmXIF_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE0_TPCIF_0]	= mmDCORE0_TPCIF_FUNNEL0_BASE,
	[GRECO_FUNNEL_DCORE0_TPCIF_1]	= mmDCORE0_TPCIF_FUNNEL1_BASE,
	[GRECO_FUNNEL_DCORE0_TPCIF_2]	= mmDCORE0_TPCIF_FUNNEL2_BASE,
	[GRECO_FUNNEL_DCORE0_TPCIF_3]	= mmDCORE0_TPCIF_FUNNEL3_BASE,
	[GRECO_FUNNEL_DCORE0_MMEIF_0]	= mmDCORE0_MMEIF_FUNNEL0_BASE,
	[GRECO_FUNNEL_DCORE0_MMEIF_1]	= mmDCORE0_MMEIF_FUNNEL1_BASE,
	[GRECO_FUNNEL_DCORE0_MMEIF_2]	= mmDCORE0_MMEIF_FUNNEL2_BASE,
	[GRECO_FUNNEL_DCORE0_MMEIF_3]	= mmDCORE0_MMEIF_FUNNEL3_BASE,
	[GRECO_FUNNEL_DCORE1_TPCIF_0]	= mmDCORE1_TPCIF_FUNNEL0_BASE,
	[GRECO_FUNNEL_DCORE1_TPCIF_1]	= mmDCORE1_TPCIF_FUNNEL1_BASE,
	[GRECO_FUNNEL_DCORE1_TPCIF_2]	= mmDCORE1_TPCIF_FUNNEL2_BASE,
	[GRECO_FUNNEL_DCORE1_TPCIF_3]	= mmDCORE1_TPCIF_FUNNEL3_BASE,
	[GRECO_FUNNEL_DCORE1_MMEIF_0]	= mmDCORE1_MMEIF_FUNNEL0_BASE,
	[GRECO_FUNNEL_DCORE1_MMEIF_1]	= mmDCORE1_MMEIF_FUNNEL1_BASE,
	[GRECO_FUNNEL_DCORE1_MMEIF_2]	= mmDCORE1_MMEIF_FUNNEL2_BASE,
	[GRECO_FUNNEL_DCORE1_MMEIF_3]	= mmDCORE1_MMEIF_FUNNEL3_BASE,
	[GRECO_FUNNEL_DCORE0_TPC0_EML]	= mmDCORE0_TPC0_EML_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE0_TPC1_EML]	= mmDCORE0_TPC1_EML_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE0_TPC2_EML]	= mmDCORE0_TPC2_EML_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE0_TPC3_EML]	= mmDCORE0_TPC3_EML_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE0_TPC4_EML]	= mmDCORE0_TPC4_EML_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE1_TPC0_EML]	= mmDCORE1_TPC0_EML_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE1_TPC1_EML]	= mmDCORE1_TPC1_EML_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE1_TPC2_EML]	= mmDCORE1_TPC2_EML_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE1_TPC3_EML]	= mmDCORE1_TPC3_EML_FUNNEL_BASE,
	[GRECO_FUNNEL_DCORE1_TPC4_EML]	= mmDCORE1_TPC4_EML_FUNNEL_BASE
};

static u64 debug_bmon_regs[GRECO_BMON_LAST + 1] = {
	[GRECO_BMON_DCORE0_MME_SBTEA_0]	= mmDCORE0_MME_SBTEA_BMON0_BASE,
	[GRECO_BMON_DCORE0_MME_SBTEB_0]	= mmDCORE0_MME_SBTEB_BMON0_BASE,
	[GRECO_BMON_DCORE0_MME_SBTEL_0]	= mmDCORE0_MME_SBTEL_BMON0_BASE,
	[GRECO_BMON_DCORE0_VSI_WRAP_0]	= mmDCORE0_VSI_WRAP_BMON_0_BASE,
	[GRECO_BMON_DCORE0_VSI_WRAP_1]	= mmDCORE0_VSI_WRAP_BMON_1_BASE,
	[GRECO_BMON_DCORE0_VSI_WRAP_2]	= mmDCORE0_VSI_WRAP_BMON_2_BASE,
	[GRECO_BMON_DCORE0_DDMA_0]	= mmDCORE0_DDMA_BMON_0_BASE,
	[GRECO_BMON_DCORE0_DDMA_1]	= mmDCORE0_DDMA_BMON_1_BASE,
	[GRECO_BMON_DCORE0_ROT_0]	= mmDCORE0_ROT_BMON_0_BASE,
	[GRECO_BMON_DCORE0_ROT_1]	= mmDCORE0_ROT_BMON_1_BASE,
	[GRECO_BMON_DCORE0_ROT_2]	= mmDCORE0_ROT_BMON_2_BASE,
	[GRECO_BMON_DCORE0_PDMA0_0]	= mmDCORE0_PDMA0_BMON_0_BASE,
	[GRECO_BMON_DCORE0_PDMA0_1]	= mmDCORE0_PDMA0_BMON_1_BASE,
	[GRECO_BMON_DCORE0_PDMA1_0]	= mmDCORE0_PDMA1_BMON_0_BASE,
	[GRECO_BMON_DCORE0_PDMA1_1]	= mmDCORE0_PDMA1_BMON_1_BASE,
	[GRECO_BMON_DCORE0_KDMA_0]	= mmDCORE0_KDMA_BMON_0_BASE,
	[GRECO_BMON_DCORE0_KDMA_1]	= mmDCORE0_KDMA_BMON_1_BASE,
	[GRECO_BMON_DCORE0_KDMA_2]	= mmDCORE0_KDMA_BMON_2_BASE,
	[GRECO_BMON_DCORE0_KDMA_3]	= mmDCORE0_KDMA_BMON_3_BASE,
	[GRECO_BMON_DCORE0_KDMA_4]	= mmDCORE0_KDMA_BMON_4_BASE,
	[GRECO_BMON_DCORE0_KDMA_5]	= mmDCORE0_KDMA_BMON_5_BASE,
	[GRECO_BMON_DCORE0_HMMU0_0]	= mmDCORE0_HMMU0_BMON_0_BASE,
	[GRECO_BMON_DCORE0_HMMU0_1]	= mmDCORE0_HMMU0_BMON_1_BASE,
	[GRECO_BMON_DCORE0_HMMU0_3]	= mmDCORE0_HMMU0_BMON_3_BASE,
	[GRECO_BMON_DCORE0_HMMU0_2]	= mmDCORE0_HMMU0_BMON_2_BASE,
	[GRECO_BMON_DCORE0_HMMU0_4]	= mmDCORE0_HMMU0_BMON_4_BASE,
	[GRECO_BMON_DCORE0_HMMU1_0]	= mmDCORE0_HMMU1_BMON_0_BASE,
	[GRECO_BMON_DCORE0_HMMU1_1]	= mmDCORE0_HMMU1_BMON_1_BASE,
	[GRECO_BMON_DCORE0_HMMU1_3]	= mmDCORE0_HMMU1_BMON_3_BASE,
	[GRECO_BMON_DCORE0_HMMU1_2]	= mmDCORE0_HMMU1_BMON_2_BASE,
	[GRECO_BMON_DCORE0_HMMU1_4]	= mmDCORE0_HMMU1_BMON_4_BASE,
	[GRECO_BMON_DCORE0_HIF0_0]	= mmDCORE0_HIF0_BMON_0_BASE,
	[GRECO_BMON_DCORE0_HIF0_1]	= mmDCORE0_HIF0_BMON_1_BASE,
	[GRECO_BMON_DCORE0_HIF1_0]	= mmDCORE0_HIF1_BMON_0_BASE,
	[GRECO_BMON_DCORE0_HIF1_1]	= mmDCORE0_HIF1_BMON_1_BASE,
	[GRECO_BMON_DCORE0_MME_CTRL_0]	= mmDCORE0_MME_CTRL_BMON0_BASE,
	[GRECO_BMON_DCORE0_MME_CTRL_1]	= mmDCORE0_MME_CTRL_BMON1_BASE,
	[GRECO_BMON_DCORE0_MME_CTRL_2]	= mmDCORE0_MME_CTRL_BMON2_BASE,
	[GRECO_BMON_DCORE1_MME_SBTEA_0]	= mmDCORE1_MME_SBTEA_BMON0_BASE,
	[GRECO_BMON_DCORE1_MME_SBTEB_0]	= mmDCORE1_MME_SBTEB_BMON0_BASE,
	[GRECO_BMON_DCORE1_MME_SBTEL_0]	= mmDCORE1_MME_SBTEL_BMON0_BASE,
	[GRECO_BMON_DCORE1_VSI_WRAP_0]	= mmDCORE1_VSI_WRAP_BMON_0_BASE,
	[GRECO_BMON_DCORE1_VSI_WRAP_1]	= mmDCORE1_VSI_WRAP_BMON_1_BASE,
	[GRECO_BMON_DCORE1_VSI_WRAP_2]	= mmDCORE1_VSI_WRAP_BMON_2_BASE,
	[GRECO_BMON_DCORE1_DDMA_0]	= mmDCORE1_DDMA_BMON_0_BASE,
	[GRECO_BMON_DCORE1_DDMA_1]	= mmDCORE1_DDMA_BMON_1_BASE,
	[GRECO_BMON_DCORE1_ROT_0]	= mmDCORE1_ROT_BMON_0_BASE,
	[GRECO_BMON_DCORE1_ROT_1]	= mmDCORE1_ROT_BMON_1_BASE,
	[GRECO_BMON_DCORE1_ROT_2]	= mmDCORE1_ROT_BMON_2_BASE,
	[GRECO_BMON_DCORE1_PDMA0_0]	= mmDCORE1_PDMA0_BMON_0_BASE,
	[GRECO_BMON_DCORE1_PDMA0_1]	= mmDCORE1_PDMA0_BMON_1_BASE,
	[GRECO_BMON_DCORE1_PDMA1_0]	= mmDCORE1_PDMA1_BMON_0_BASE,
	[GRECO_BMON_DCORE1_PDMA1_1]	= mmDCORE1_PDMA1_BMON_1_BASE,
	[GRECO_BMON_DCORE1_KDMA_0]	= mmDCORE1_KDMA_BMON_0_BASE,
	[GRECO_BMON_DCORE1_KDMA_1]	= mmDCORE1_KDMA_BMON_1_BASE,
	[GRECO_BMON_DCORE1_KDMA_2]	= mmDCORE1_KDMA_BMON_2_BASE,
	[GRECO_BMON_DCORE1_KDMA_3]	= mmDCORE1_KDMA_BMON_3_BASE,
	[GRECO_BMON_DCORE1_KDMA_4]	= mmDCORE1_KDMA_BMON_4_BASE,
	[GRECO_BMON_DCORE1_KDMA_5]	= mmDCORE1_KDMA_BMON_5_BASE,
	[GRECO_BMON_DCORE1_HMMU0_0]	= mmDCORE1_HMMU0_BMON_0_BASE,
	[GRECO_BMON_DCORE1_HMMU0_1]	= mmDCORE1_HMMU0_BMON_1_BASE,
	[GRECO_BMON_DCORE1_HMMU0_3]	= mmDCORE1_HMMU0_BMON_3_BASE,
	[GRECO_BMON_DCORE1_HMMU0_2]	= mmDCORE1_HMMU0_BMON_2_BASE,
	[GRECO_BMON_DCORE1_HMMU0_4]	= mmDCORE1_HMMU0_BMON_4_BASE,
	[GRECO_BMON_DCORE1_HMMU1_0]	= mmDCORE1_HMMU1_BMON_0_BASE,
	[GRECO_BMON_DCORE1_HMMU1_1]	= mmDCORE1_HMMU1_BMON_1_BASE,
	[GRECO_BMON_DCORE1_HMMU1_3]	= mmDCORE1_HMMU1_BMON_3_BASE,
	[GRECO_BMON_DCORE1_HMMU1_2]	= mmDCORE1_HMMU1_BMON_2_BASE,
	[GRECO_BMON_DCORE1_HMMU1_4]	= mmDCORE1_HMMU1_BMON_4_BASE,
	[GRECO_BMON_DCORE1_HIF0_0]	= mmDCORE1_HIF0_BMON_0_BASE,
	[GRECO_BMON_DCORE1_HIF0_1]	= mmDCORE1_HIF0_BMON_1_BASE,
	[GRECO_BMON_DCORE1_HIF1_0]	= mmDCORE1_HIF1_BMON_0_BASE,
	[GRECO_BMON_DCORE1_HIF1_1]	= mmDCORE1_HIF1_BMON_1_BASE,
	[GRECO_BMON_DCORE1_MME_CTRL_0]	= mmDCORE1_MME_CTRL_BMON0_BASE,
	[GRECO_BMON_DCORE1_MME_CTRL_1]	= mmDCORE1_MME_CTRL_BMON1_BASE,
	[GRECO_BMON_DCORE1_MME_CTRL_2]	= mmDCORE1_MME_CTRL_BMON2_BASE,
	[GRECO_BMON_PCIE_MSTR_WR]	= mmPCIE_BMON_MSTR_WR_BASE,
	[GRECO_BMON_PCIE_MSTR_RD]	= mmPCIE_BMON_MSTR_RD_BASE,
	[GRECO_BMON_PCIE_SLV_WR]	= mmPCIE_BMON_SLV_WR_BASE,
	[GRECO_BMON_PCIE_SLV_RD]	= mmPCIE_BMON_SLV_RD_BASE,
	[GRECO_BMON_PSOC_ARC0_0]	= mmPSOC_ARC0_BMON_0_BASE,
	[GRECO_BMON_PSOC_ARC0_1]	= mmPSOC_ARC0_BMON_1_BASE,
	[GRECO_BMON_PSOC_ARC1_0]	= mmPSOC_ARC1_BMON_0_BASE,
	[GRECO_BMON_PSOC_ARC1_1]	= mmPSOC_ARC1_BMON_1_BASE,
	[GRECO_BMON_CPU_WR]		= mmCPU_WR_BMON_BASE,
	[GRECO_BMON_CPU_RD]		= mmCPU_RD_BMON_BASE,
	[GRECO_BMON_PMMU_0]		= mmPMMU_BMON_0_BASE,
	[GRECO_BMON_PMMU_1]		= mmPMMU_BMON_1_BASE,
	[GRECO_BMON_PMMU_2]		= mmPMMU_BMON_2_BASE,
	[GRECO_BMON_PMMU_3]		= mmPMMU_BMON_3_BASE,
	[GRECO_BMON_PMMU_4]		= mmPMMU_BMON_4_BASE,
	[GRECO_BMON_DCORE0_TPC0_EML_0]	= mmDCORE0_TPC0_EML_BUSMON_0_BASE,
	[GRECO_BMON_DCORE0_TPC0_EML_1]	= mmDCORE0_TPC0_EML_BUSMON_1_BASE,
	[GRECO_BMON_DCORE0_TPC0_EML_2]	= mmDCORE0_TPC0_EML_BUSMON_2_BASE,
	[GRECO_BMON_DCORE0_TPC0_EML_3]	= mmDCORE0_TPC0_EML_BUSMON_3_BASE,
	[GRECO_BMON_DCORE0_TPC1_EML_0]	= mmDCORE0_TPC1_EML_BUSMON_0_BASE,
	[GRECO_BMON_DCORE0_TPC1_EML_1]	= mmDCORE0_TPC1_EML_BUSMON_1_BASE,
	[GRECO_BMON_DCORE0_TPC1_EML_2]	= mmDCORE0_TPC1_EML_BUSMON_2_BASE,
	[GRECO_BMON_DCORE0_TPC1_EML_3]	= mmDCORE0_TPC1_EML_BUSMON_3_BASE,
	[GRECO_BMON_DCORE0_TPC2_EML_0]	= mmDCORE0_TPC2_EML_BUSMON_0_BASE,
	[GRECO_BMON_DCORE0_TPC2_EML_1]	= mmDCORE0_TPC2_EML_BUSMON_1_BASE,
	[GRECO_BMON_DCORE0_TPC2_EML_2]	= mmDCORE0_TPC2_EML_BUSMON_2_BASE,
	[GRECO_BMON_DCORE0_TPC2_EML_3]	= mmDCORE0_TPC2_EML_BUSMON_3_BASE,
	[GRECO_BMON_DCORE0_TPC3_EML_0]	= mmDCORE0_TPC3_EML_BUSMON_0_BASE,
	[GRECO_BMON_DCORE0_TPC3_EML_1]	= mmDCORE0_TPC3_EML_BUSMON_1_BASE,
	[GRECO_BMON_DCORE0_TPC3_EML_2]	= mmDCORE0_TPC3_EML_BUSMON_2_BASE,
	[GRECO_BMON_DCORE0_TPC3_EML_3]	= mmDCORE0_TPC3_EML_BUSMON_3_BASE,
	[GRECO_BMON_DCORE0_TPC4_EML_0]	= mmDCORE0_TPC4_EML_BUSMON_0_BASE,
	[GRECO_BMON_DCORE0_TPC4_EML_1]	= mmDCORE0_TPC4_EML_BUSMON_1_BASE,
	[GRECO_BMON_DCORE0_TPC4_EML_2]	= mmDCORE0_TPC4_EML_BUSMON_2_BASE,
	[GRECO_BMON_DCORE0_TPC4_EML_3]	= mmDCORE0_TPC4_EML_BUSMON_3_BASE,
	[GRECO_BMON_DCORE1_TPC0_EML_0]	= mmDCORE1_TPC0_EML_BUSMON_0_BASE,
	[GRECO_BMON_DCORE1_TPC0_EML_1]	= mmDCORE1_TPC0_EML_BUSMON_1_BASE,
	[GRECO_BMON_DCORE1_TPC0_EML_2]	= mmDCORE1_TPC0_EML_BUSMON_2_BASE,
	[GRECO_BMON_DCORE1_TPC0_EML_3]	= mmDCORE1_TPC0_EML_BUSMON_3_BASE,
	[GRECO_BMON_DCORE1_TPC1_EML_0]	= mmDCORE1_TPC1_EML_BUSMON_0_BASE,
	[GRECO_BMON_DCORE1_TPC1_EML_1]	= mmDCORE1_TPC1_EML_BUSMON_1_BASE,
	[GRECO_BMON_DCORE1_TPC1_EML_2]	= mmDCORE1_TPC1_EML_BUSMON_2_BASE,
	[GRECO_BMON_DCORE1_TPC1_EML_3]	= mmDCORE1_TPC1_EML_BUSMON_3_BASE,
	[GRECO_BMON_DCORE1_TPC2_EML_0]	= mmDCORE1_TPC2_EML_BUSMON_0_BASE,
	[GRECO_BMON_DCORE1_TPC2_EML_1]	= mmDCORE1_TPC2_EML_BUSMON_1_BASE,
	[GRECO_BMON_DCORE1_TPC2_EML_2]	= mmDCORE1_TPC2_EML_BUSMON_2_BASE,
	[GRECO_BMON_DCORE1_TPC2_EML_3]	= mmDCORE1_TPC2_EML_BUSMON_3_BASE,
	[GRECO_BMON_DCORE1_TPC3_EML_0]	= mmDCORE1_TPC3_EML_BUSMON_0_BASE,
	[GRECO_BMON_DCORE1_TPC3_EML_1]	= mmDCORE1_TPC3_EML_BUSMON_1_BASE,
	[GRECO_BMON_DCORE1_TPC3_EML_2]	= mmDCORE1_TPC3_EML_BUSMON_2_BASE,
	[GRECO_BMON_DCORE1_TPC3_EML_3]	= mmDCORE1_TPC3_EML_BUSMON_3_BASE,
	[GRECO_BMON_DCORE1_TPC4_EML_0]	= mmDCORE1_TPC4_EML_BUSMON_0_BASE,
	[GRECO_BMON_DCORE1_TPC4_EML_1]	= mmDCORE1_TPC4_EML_BUSMON_1_BASE,
	[GRECO_BMON_DCORE1_TPC4_EML_2]	= mmDCORE1_TPC4_EML_BUSMON_2_BASE,
	[GRECO_BMON_DCORE1_TPC4_EML_3]	= mmDCORE1_TPC4_EML_BUSMON_3_BASE
};

static u64 debug_spmu_regs[GRECO_SPMU_LAST + 1] = {
	[GRECO_SPMU_DCORE0_DDR0]	= mmDCORE0_DDR0_SPMU_BASE,
	[GRECO_SPMU_DCORE0_DDR1]	= mmDCORE0_DDR1_SPMU_BASE,
	[GRECO_SPMU_DCORE0_DDR2]	= mmDCORE0_DDR2_SPMU_BASE,
	[GRECO_SPMU_DCORE0_DDR3]	= mmDCORE0_DDR3_SPMU_BASE,
	[GRECO_SPMU_DCORE1_DDR0]	= mmDCORE1_DDR0_SPMU_BASE,
	[GRECO_SPMU_DCORE1_DDR1]	= mmDCORE1_DDR1_SPMU_BASE,
	[GRECO_SPMU_DCORE1_DDR2]	= mmDCORE1_DDR2_SPMU_BASE,
	[GRECO_SPMU_DCORE1_DDR3]	= mmDCORE1_DDR3_SPMU_BASE,
	[GRECO_SPMU_DCORE0_MME_SBTEA]	= mmDCORE0_MME_SBTEA_SPMU_BASE,
	[GRECO_SPMU_DCORE0_MME_SBTEB]	= mmDCORE0_MME_SBTEB_SPMU_BASE,
	[GRECO_SPMU_DCORE0_MME_SBTEL]	= mmDCORE0_MME_SBTEL_SPMU_BASE,
	[GRECO_SPMU_DCORE0_VSI_WRAP_CS]	= mmDCORE0_VSI_WRAP_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE0_DDMA_CS]	= mmDCORE0_DDMA_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE0_ROT_CS]	= mmDCORE0_ROT_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE0_PDMA0_CS]	= mmDCORE0_PDMA0_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE0_PDMA1_CS]	= mmDCORE0_PDMA1_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE0_KDMA_CS]	= mmDCORE0_KDMA_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE0_HMMU0_CS]	= mmDCORE0_HMMU0_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE0_HMMU1_CS]	= mmDCORE0_HMMU1_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE0_HIF0_CS]	= mmDCORE0_HIF0_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE0_HIF1_CS]	= mmDCORE0_HIF1_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE0_MME_CTRL]	= mmDCORE0_MME_CTRL_SPMU_BASE,
	[GRECO_SPMU_DCORE1_MME_SBTEA]	= mmDCORE1_MME_SBTEA_SPMU_BASE,
	[GRECO_SPMU_DCORE1_MME_SBTEB]	= mmDCORE1_MME_SBTEB_SPMU_BASE,
	[GRECO_SPMU_DCORE1_MME_SBTEL]	= mmDCORE1_MME_SBTEL_SPMU_BASE,
	[GRECO_SPMU_DCORE1_VSI_WRAP_CS]	= mmDCORE1_VSI_WRAP_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE1_DDMA_CS]	= mmDCORE1_DDMA_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE1_ROT_CS]	= mmDCORE1_ROT_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE1_PDMA0_CS]	= mmDCORE1_PDMA0_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE1_PDMA1_CS]	= mmDCORE1_PDMA1_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE1_KDMA_CS]	= mmDCORE1_KDMA_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE1_HMMU0_CS]	= mmDCORE1_HMMU0_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE1_HMMU1_CS]	= mmDCORE1_HMMU1_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE1_HIF0_CS]	= mmDCORE1_HIF0_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE1_HIF1_CS]	= mmDCORE1_HIF1_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE1_MME_CTRL]	= mmDCORE1_MME_CTRL_SPMU_BASE,
	[GRECO_SPMU_PCIE]		= mmPCIE_SPMU_BASE,
	[GRECO_SPMU_PSOC_ARC0_CS]	= mmPSOC_ARC0_CS_SPMU_BASE,
	[GRECO_SPMU_PSOC_ARC1_CS]	= mmPSOC_ARC1_CS_SPMU_BASE,
	[GRECO_SPMU_PMMU_CS]		= mmPMMU_CS_SPMU_BASE,
	[GRECO_SPMU_DCORE0_TPC0_EML]	= mmDCORE0_TPC0_EML_SPMU_BASE,
	[GRECO_SPMU_DCORE0_TPC1_EML]	= mmDCORE0_TPC1_EML_SPMU_BASE,
	[GRECO_SPMU_DCORE0_TPC2_EML]	= mmDCORE0_TPC2_EML_SPMU_BASE,
	[GRECO_SPMU_DCORE0_TPC3_EML]	= mmDCORE0_TPC3_EML_SPMU_BASE,
	[GRECO_SPMU_DCORE0_TPC4_EML]	= mmDCORE0_TPC4_EML_SPMU_BASE,
	[GRECO_SPMU_DCORE1_TPC0_EML]	= mmDCORE1_TPC0_EML_SPMU_BASE,
	[GRECO_SPMU_DCORE1_TPC1_EML]	= mmDCORE1_TPC1_EML_SPMU_BASE,
	[GRECO_SPMU_DCORE1_TPC2_EML]	= mmDCORE1_TPC2_EML_SPMU_BASE,
	[GRECO_SPMU_DCORE1_TPC3_EML]	= mmDCORE1_TPC3_EML_SPMU_BASE,
	[GRECO_SPMU_DCORE1_TPC4_EML]	= mmDCORE1_TPC4_EML_SPMU_BASE
};

static int greco_coresight_timeout(struct hl_device *hdev, u64 addr,
		int position, bool up)
{
	int rc;
	u32 val, timeout_usec;

	if (hdev->pldm)
		timeout_usec = GRECO_PLDM_CORESIGHT_TIMEOUT_USEC;
	else
		timeout_usec = CORESIGHT_TIMEOUT_USEC;

	rc = hl_poll_timeout(
		hdev,
		addr,
		val,
		up ? val & BIT(position) : !(val & BIT(position)),
		1000,
		timeout_usec);

	if (rc) {
		dev_err(hdev->dev,
			"Timeout while waiting for coresight, addr: 0x%llx, position: %d, up: %d\n",
				addr, position, up);
		return -EFAULT;
	}

	return 0;
}

static int greco_config_stm(struct hl_device *hdev,
		struct hl_debug_params *params)
{
	struct hl_debug_params_stm *input;
	u64 base_reg;
	u32 frequency;
	int rc;

	if (params->reg_idx >= ARRAY_SIZE(debug_stm_regs)) {
		dev_err(hdev->dev, "Invalid register index in STM\n");
		return -EINVAL;
	}

	base_reg = debug_stm_regs[params->reg_idx];

	WREG32(base_reg + 0xFB0, CORESIGHT_UNLOCK);

	if (params->enable) {
		input = params->input;

		if (!input)
			return -EINVAL;

		WREG32(base_reg + 0xE80, 0x80004);
		/* dummy read for pldm to flush outstanding writes */
		if (hdev->pldm)
			RREG32(base_reg + 0xE80);
		WREG32(base_reg + 0xD64, 7);
		WREG32(base_reg + 0xD60, 0);
		WREG32(base_reg + 0xD00, lower_32_bits(input->he_mask));
		WREG32(base_reg + 0xD60, 1);
		WREG32(base_reg + 0xD00, upper_32_bits(input->he_mask));
		WREG32(base_reg + 0xE70, 0x10);
		WREG32(base_reg + 0xE60, 0);
		WREG32(base_reg + 0xE00, lower_32_bits(input->sp_mask));
		WREG32(base_reg + 0xEF4, input->id);
		WREG32(base_reg + 0xDF4, 0x80);
		frequency = hdev->asic_prop.psoc_timestamp_frequency;
		if (frequency == 0)
			frequency = input->frequency;
		WREG32(base_reg + 0xE8C, frequency);
		WREG32(base_reg + 0xE90, 0x1F00);
		WREG32(base_reg + 0xE80, 0x23 | (input->id << 16));
	} else {
		WREG32(base_reg + 0xE80, 4);
		WREG32(base_reg + 0xD64, 0);
		WREG32(base_reg + 0xD60, 1);
		WREG32(base_reg + 0xD00, 0);
		WREG32(base_reg + 0xD20, 0);
		WREG32(base_reg + 0xD60, 0);
		WREG32(base_reg + 0xE20, 0);
		WREG32(base_reg + 0xE00, 0);
		WREG32(base_reg + 0xDF4, 0x80);
		WREG32(base_reg + 0xE70, 0);
		WREG32(base_reg + 0xE60, 0);
		WREG32(base_reg + 0xE64, 0);
		WREG32(base_reg + 0xE8C, 0);

		rc = greco_coresight_timeout(hdev, base_reg + 0xE80, 23, false);
		if (rc) {
			dev_err(hdev->dev,
				"Failed to disable STM on timeout, error %d\n",
				rc);
			return rc;
		}

		WREG32(base_reg + 0xE80, 4);
	}

	return 0;
}

static int greco_config_etf(struct hl_device *hdev,
		struct hl_debug_params *params)
{
	struct hl_debug_params_etf *input;
	u64 base_reg;
	u32 val;
	int rc;

	if (params->reg_idx >= ARRAY_SIZE(debug_etf_regs)) {
		dev_err(hdev->dev, "Invalid register index in ETF\n");
		return -EINVAL;
	}

	base_reg = debug_etf_regs[params->reg_idx];

	WREG32(base_reg + 0xFB0, CORESIGHT_UNLOCK);

	val = RREG32(base_reg + 0x304);
	val |= 0x1000;
	WREG32(base_reg + 0x304, val);
	val |= 0x40;
	WREG32(base_reg + 0x304, val);

	rc = greco_coresight_timeout(hdev, base_reg + 0x304, 6, false);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to %s ETF on timeout, error %d\n",
				params->enable ? "enable" : "disable", rc);
		return rc;
	}

	rc = greco_coresight_timeout(hdev, base_reg + 0xC, 2, true);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to %s ETF on timeout, error %d\n",
				params->enable ? "enable" : "disable", rc);
		return rc;
	}

	WREG32(base_reg + 0x20, 0);

	if (params->enable) {
		input = params->input;

		if (!input)
			return -EINVAL;

		WREG32(base_reg + 0x34, 0x3FFC);
		WREG32(base_reg + 0x28, input->sink_mode);
		WREG32(base_reg + 0x304, 0x4001);
		WREG32(base_reg + 0x308, 0xA);
		WREG32(base_reg + 0x20, 1);
	} else {
		WREG32(base_reg + 0x34, 0);
		WREG32(base_reg + 0x28, 0);
		WREG32(base_reg + 0x304, 0);
	}

	return 0;
}

static int greco_etr_validate_address(struct hl_device *hdev, u64 addr,
		u64 size)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct greco_device *greco = hdev->asic_specific;

	/* maximum address length is 50 bits */
	if (addr >> 50) {
		dev_err(hdev->dev,
			"ETR buffer address shouldn't exceed 50 bits\n");
		return false;
	}

	if (addr > (addr + size)) {
		dev_err(hdev->dev,
			"ETR buffer size %llu overflow\n", size);
		return false;
	}

	if (greco->hw_cap_initialized & HW_CAP_PMMU) {
		if (hl_mem_area_inside_range(addr, size,
					prop->pmmu.start_addr,
					prop->pmmu.end_addr))
			return true;

		if (hl_mem_area_inside_range(addr, size,
					prop->pmmu_huge.start_addr,
					prop->pmmu_huge.end_addr))
			return true;

		if (hl_mem_area_inside_range(addr, size,
					prop->dmmu.start_addr,
					prop->dmmu.end_addr))
			return true;
	} else {
		if (hl_mem_area_inside_range(addr, size,
					prop->dram_user_base_address,
					prop->dram_end_address))
			return true;
	}

	if (hl_mem_area_inside_range(addr, size,
			prop->sram_user_base_address,
			prop->sram_end_address))
		return true;

	if (!(greco->hw_cap_initialized & HW_CAP_PMMU))
		dev_err(hdev->dev, "ETR buffer should be in SRAM/DRAM\n");

	return false;
}

static int greco_config_etr(struct hl_device *hdev, struct hl_ctx *ctx,
				struct hl_debug_params *params)
{
	struct hl_debug_params_etr *input;
	u64 msb;
	u32 val;
	int rc;

	WREG32(mmPSOC_ETR_LAR, CORESIGHT_UNLOCK);

	val = RREG32(mmPSOC_ETR_FFCR);
	val |= 0x1000;
	WREG32(mmPSOC_ETR_FFCR, val);
	val |= 0x40;
	WREG32(mmPSOC_ETR_FFCR, val);

	rc = greco_coresight_timeout(hdev, mmPSOC_ETR_FFCR, 6, false);
	if (rc) {
		dev_err(hdev->dev, "Failed to %s ETR on timeout, error %d\n",
				params->enable ? "enable" : "disable", rc);
		return rc;
	}

	rc = greco_coresight_timeout(hdev, mmPSOC_ETR_STS, 2, true);
	if (rc) {
		dev_err(hdev->dev, "Failed to %s ETR on timeout, error %d\n",
				params->enable ? "enable" : "disable", rc);
		return rc;
	}

	WREG32(mmPSOC_ETR_CTL, 0);

	if (params->enable) {
		input = params->input;

		if (!input)
			return -EINVAL;

		if (input->buffer_size == 0) {
			dev_err(hdev->dev,
				"ETR buffer size should be bigger than 0\n");
			return -EINVAL;
		}

		if (!greco_etr_validate_address(hdev,
				input->buffer_address, input->buffer_size)) {
			dev_err(hdev->dev, "ETR buffer address is invalid\n");
			return -EINVAL;
		}

		RMWREG32(mmPSOC_GLOBAL_CONF_TRACE_AWUSER, ctx->asid, MMUBP_ASID_MASK);
		RMWREG32(mmPSOC_GLOBAL_CONF_TRACE_ARUSER, ctx->asid, MMUBP_ASID_MASK);

		msb = upper_32_bits(input->buffer_address) >> 8;
		msb &= PSOC_GLOBAL_CONF_TRACE_ADDR_MSB_MASK;
		WREG32(mmPSOC_GLOBAL_CONF_TRACE_ADDR, msb);

		WREG32(mmPSOC_ETR_BUFWM, 0x3FFC);
		WREG32(mmPSOC_ETR_RSZ, input->buffer_size);
		WREG32(mmPSOC_ETR_MODE, input->sink_mode);
		/* write the protection bits only if security is disable */
		if (!hdev->asic_prop.fw_security_enabled) {
			/* make ETR not privileged */
			val = FIELD_PREP(PSOC_ETR_AXICTL_PROTCTRLBIT0_MASK, 0);
			/* make ETR non-secured (inverted logic) */
			val |= FIELD_PREP(PSOC_ETR_AXICTL_PROTCTRLBIT1_MASK, 1);
			/* burst size 8 */
			val |= FIELD_PREP(PSOC_ETR_AXICTL_WRBURSTLEN_MASK, 7);
			WREG32(mmPSOC_ETR_AXICTL, val);
		}
		WREG32(mmPSOC_ETR_DBALO,
				lower_32_bits(input->buffer_address));
		WREG32(mmPSOC_ETR_DBAHI,
				upper_32_bits(input->buffer_address));
		WREG32(mmPSOC_ETR_FFCR, 3);
		WREG32(mmPSOC_ETR_PSCR, 0xA);
		WREG32(mmPSOC_ETR_CTL, 1);
	} else {
		WREG32(mmPSOC_ETR_BUFWM, 0);
		WREG32(mmPSOC_ETR_RSZ, 0x400);
		WREG32(mmPSOC_ETR_DBALO, 0);
		WREG32(mmPSOC_ETR_DBAHI, 0);
		WREG32(mmPSOC_ETR_PSCR, 0);
		WREG32(mmPSOC_ETR_MODE, 0);
		WREG32(mmPSOC_ETR_FFCR, 0);

		if (params->output_size >= sizeof(u64)) {
			u32 rwp, rwphi;

			/*
			 * The trace buffer address is 50 bits wide. The end of
			 * the buffer is set in the RWP register (lower 32
			 * bits), and in the RWPHI register (upper 8 bits).
			 * The 10 msb of the 50-bit address are stored in a
			 * global configuration register.
			 */
			rwp = RREG32(mmPSOC_ETR_RWP);
			rwphi = RREG32(mmPSOC_ETR_RWPHI) & 0xff;
			msb = RREG32(mmPSOC_GLOBAL_CONF_TRACE_ADDR) &
					PSOC_GLOBAL_CONF_TRACE_ADDR_MSB_MASK;
			*(u64 *) params->output = ((u64) msb << 40) |
						((u64) rwphi << 32) | rwp;
		}
	}

	return 0;
}

static int greco_config_funnel(struct hl_device *hdev,
		struct hl_debug_params *params)
{
	u64 base_reg;
	u32 val = params->enable ? 0x33F : 0;

	if (params->reg_idx >= ARRAY_SIZE(debug_funnel_regs)) {
		dev_err(hdev->dev, "Invalid register index in FUNNEL\n");
		return -EINVAL;
	}

	base_reg = debug_funnel_regs[params->reg_idx];

	/* Bug H5-1421: in ARC0 funnel, enable slave 1 only for STM trace */
	if (params->reg_idx == GRECO_FUNNEL_PSOC_ARC0)
		val &= (1<<2);
	/*
	 * Bug H5-1601: in PCIe funnel, enabling any slave other than slave 0
	 * may cause unexpected behavior
	 */
	if (params->reg_idx == GRECO_FUNNEL_PCIE)
		val &= 1;

	WREG32(base_reg + 0xFB0, CORESIGHT_UNLOCK);

	WREG32(base_reg, val);

	return 0;
}

static int greco_config_bmon(struct hl_device *hdev,
		struct hl_debug_params *params)
{
	struct hl_debug_params_bmon *input;
	u64 base_reg;

	if (params->reg_idx >= ARRAY_SIZE(debug_bmon_regs)) {
		dev_err(hdev->dev, "Invalid register index in BMON\n");
		return -EINVAL;
	}

	base_reg = debug_bmon_regs[params->reg_idx];

	WREG32(base_reg + 0x104, 1);
	/* dummy read for pldm to flush outstanding writes */
	if (hdev->pldm)
		RREG32(base_reg + 0x104);

	if (params->enable) {
		input = params->input;

		if (!input)
			return -EINVAL;

		/* ADDRL_S0	0x20	*/
		WREG32(base_reg + 0x20, lower_32_bits(input->start_addr0));
		/* ADDRH_S0	0x24	*/
		WREG32(base_reg + 0x24, upper_32_bits(input->start_addr0));
		/* ADDRL_E0	0x28	*/
		WREG32(base_reg + 0x28, lower_32_bits(input->addr_mask0));
		/* ADDRH_E0	0x2C	*/
		WREG32(base_reg + 0x2C, upper_32_bits(input->addr_mask0));
		/* ADDRL_S1	0x30	*/
		WREG32(base_reg + 0x30, lower_32_bits(input->start_addr1));
		/* ADDRH_S1	0x34	*/
		WREG32(base_reg + 0x34, upper_32_bits(input->start_addr1));
		/* ADDRL_E1	0x38	*/
		WREG32(base_reg + 0x38, lower_32_bits(input->addr_mask1));
		/* ADDRH_E1	0x3C	*/
		WREG32(base_reg + 0x3C, upper_32_bits(input->addr_mask1));
		/* ADDRL_S2	0x40	*/
		WREG32(base_reg + 0x40, lower_32_bits(input->start_addr2));
		/* ADDRH_S2	0x44	*/
		WREG32(base_reg + 0x44, upper_32_bits(input->start_addr2));
		/* ADDRL_E2	0x48	*/
		WREG32(base_reg + 0x48, lower_32_bits(input->end_addr2));
		/* ADDRH_E2	0x4C	*/
		WREG32(base_reg + 0x4C, upper_32_bits(input->end_addr2));
		/* ADDRL_S3	0x50	*/
		WREG32(base_reg + 0x50, lower_32_bits(input->start_addr3));
		/* ADDRH_S3	0x54	*/
		WREG32(base_reg + 0x54, upper_32_bits(input->start_addr3));
		/* ADDRL_E3	0x58	*/
		WREG32(base_reg + 0x58, lower_32_bits(input->end_addr3));
		/* ADDRH_E3	0x5C	*/
		WREG32(base_reg + 0x5C, upper_32_bits(input->end_addr3));
		/* ATTREN	0x104	*/
		WREG32(base_reg + 0x104, 0);
		/* BW_WIN	0x20C	*/
		WREG32(base_reg + 0x20C, input->bw_win);
		/* WIN_CAPT	0x208	*/
		WREG32(base_reg + 0x208, input->win_capture);
		/* REDUCTION	0x60	*/
		WREG32(base_reg + 0x60, 0x1 | (13 << 8));
		/* STM_TRC	0x420	*/
		WREG32(base_reg + 0x420, 0x7 | (input->id << 8));
		/* CR		0x0	*/
		WREG32(base_reg + 0x0, input->control);
	} else {
		/* ADDRL_S0	0x20	*/
		WREG32(base_reg + 0x20, 0);
		/* ADDRH_S0	0x24	*/
		WREG32(base_reg + 0x24, 0);
		/* ADDRL_E0	0x28	*/
		WREG32(base_reg + 0x28, 0);
		/* ADDRH_E0	0x2C	*/
		WREG32(base_reg + 0x2C, 0);
		/* ADDRL_S1	0x30	*/
		WREG32(base_reg + 0x30, 0);
		/* ADDRH_S1	0x34	*/
		WREG32(base_reg + 0x34, 0);
		/* ADDRL_E1	0x38	*/
		WREG32(base_reg + 0x38, 0);
		/* ADDRH_E1	0x3C	*/
		WREG32(base_reg + 0x3C, 0);
		/* REDUCTION	0x60	*/
		WREG32(base_reg + 0x60, 0);
		/* STM_TRC	0x420	*/
		WREG32(base_reg + 0x420, 0x7 | (0xA << 8));
		/* CR		0x0	*/
		WREG32(base_reg + 0x0, 0x77 | 0xf << 24);
	}

	return 0;
}

static int greco_config_spmu(struct hl_device *hdev,
		struct hl_debug_params *params)
{
	u64 base_reg;
	struct hl_debug_params_spmu *input = params->input;
	u64 *output;
	u32 output_arr_len;
	u32 events_num;
	u32 overflow_idx;
	u32 cycle_cnt_idx;
	u8 event_mask;
	int i;

	if (params->reg_idx >= ARRAY_SIZE(debug_spmu_regs)) {
		dev_err(hdev->dev, "Invalid register index in SPMU\n");
		return -EINVAL;
	}

	base_reg = debug_spmu_regs[params->reg_idx];

	if (params->enable) {
		input = params->input;

		if (!input)
			return -EINVAL;

		if (input->event_types_num > SPMU_MAX_COUNTERS) {
			dev_err(hdev->dev,
				"too many event types values for SPMU enable\n");
			return -EINVAL;
		}

		WREG32(base_reg + 0xE04, 0x41013046);
		WREG32(base_reg + 0xE04, 0x41013040);
		/* dummy read for pldm to flush outstanding writes */
		if (hdev->pldm)
			RREG32(base_reg);

		for (i = 0 ; i < input->event_types_num ; i++)
			WREG32(base_reg + SPMU_EVENT_TYPES_OFFSET + i * 4,
				input->event_types[i]);

		WREG32(base_reg + 0x200, input->pmtrc_val);

		WREG32(base_reg + 0x204, input->trc_ctrl_host_val);
		WREG32(base_reg + 0x20C, input->trc_en_host_val);

		WREG32(base_reg + 0xE04, 0x41013041);
		event_mask = 0;
		events_num = input->event_types_num;
		while (events_num--) {
			event_mask <<= 1;
			event_mask |= 1;
		}
		WREG32(base_reg + 0xC00, 0x80000000 | event_mask);
	} else {
		output = params->output;
		output_arr_len = params->output_size / 8;
		events_num = output_arr_len - 2;
		overflow_idx = output_arr_len - 2;
		cycle_cnt_idx = output_arr_len - 1;

		WREG32(base_reg + 0xE04, 0x41013040);
		if (output && output_arr_len > 2) {

			if (events_num > SPMU_MAX_COUNTERS) {
				dev_err(hdev->dev,
					"too many events values for SPMU disable\n");
				return -EINVAL;
			}

			for (i = 0 ; i < events_num ; i++)
				output[i] = RREG32(base_reg + i * 8);

			output[overflow_idx] = RREG32(base_reg + 0xCC0);

			output[cycle_cnt_idx] = RREG32(base_reg + 0xFC);
			output[cycle_cnt_idx] <<= 32;
			output[cycle_cnt_idx] |= RREG32(base_reg + 0xF8);
		}
		WREG32(base_reg + 0xCC0, 0);
		WREG32(base_reg + 0x200, 0x100400);
	}

	return 0;
}

int greco_debug_coresight(struct hl_device *hdev, struct hl_ctx *ctx, void *data)
{
	struct hl_debug_params *params = data;
	int rc = 0;

	switch (params->op) {
	case HL_DEBUG_OP_STM:
		rc = greco_config_stm(hdev, params);
		break;
	case HL_DEBUG_OP_ETF:
		rc = greco_config_etf(hdev, params);
		break;
	case HL_DEBUG_OP_ETR:
		rc = greco_config_etr(hdev, ctx, params);
		break;
	case HL_DEBUG_OP_FUNNEL:
		rc = greco_config_funnel(hdev, params);
		break;
	case HL_DEBUG_OP_BMON:
		rc = greco_config_bmon(hdev, params);
		break;
	case HL_DEBUG_OP_SPMU:
		rc = greco_config_spmu(hdev, params);
		break;
	case HL_DEBUG_OP_TIMESTAMP:
		/* Do nothing as this opcode is deprecated */
		break;

	default:
		dev_err(hdev->dev, "Unknown coresight id %d\n", params->op);
		return -EINVAL;
	}

	/* Perform read from the device to flush all configuration */
	RREG32(mmHW_STATE);

	return rc;
}

void greco_halt_coresight(struct hl_device *hdev, struct hl_ctx *ctx)
{
	struct hl_debug_params params = {};
	int i, rc;

	/* in pldm attempting to access stubbed etfs can cause problems */
	if (!hdev->pldm)
		for (i = GRECO_ETF_FIRST ; i <= GRECO_ETF_LAST ; i++) {
			params.reg_idx = i;
			rc = greco_config_etf(hdev, &params);
			if (rc)
				dev_err(hdev->dev, "halt ETF failed, %d/%d\n",
								rc, i);
		}

	rc = greco_config_etr(hdev, ctx, &params);
	if (rc)
		dev_err(hdev->dev, "halt ETR failed, %d\n", rc);
}
