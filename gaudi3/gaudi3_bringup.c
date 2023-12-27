// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3P.h"
#include "gaudi3_cn.h"
#include "gaudi3_masks.h"
#include "gaudi3_interrupt_map_bringup.h"

#include <linux/bitrev.h>
#include <linux/ethtool.h>

#define GAUDI3_PLL_TIMEOUT_USEC		10000 /* 10ms */

#define GAUDI3_D2D_DPHY_CTRL_POLL_INTERVAL_USEC	2000000		/* 2sec */
#define GAUDI3_D2D_DPHY_CTRL_POLL_TIMEOUT_USEC	6000000ULL	/* 6sec */
#define GAUDI3_PLDM_D2D_DPHY_CTRL_POLL_TIMEOUT_USEC	600000000ULL	/* 600sec */

#define GAUDI3_EU0_REDUN_CLK_EN		0xffff
#define GAUDI3_EU1_REDUN_CLK_EN		0xffff

#define MME_CTRL_LO_QM_STOP_ON_ERR_MASK	(MME_CTRL_LO_QM_STOP_ON_SB_ERR_M | \
					MME_CTRL_LO_QM_STOP_ON_WAP_AXI_ERR_M | \
					MME_CTRL_LO_QM_STOP_ON_ARC_AXI_ERR_M | \
					MME_CTRL_LO_QM_STOP_ON_SIG_AXI_ERR_M | \
					MME_CTRL_LO_QM_STOP_ON_ARC_SERR_M | \
					MME_CTRL_LO_QM_STOP_ON_ARC_DERR_M | \
					MME_CTRL_LO_QM_STOP_ON_ACC_SERR_M | \
					MME_CTRL_LO_QM_STOP_ON_ACC_DERR_M | \
					MME_CTRL_LO_QM_STOP_ON_SB_SERR_M | \
					MME_CTRL_LO_QM_STOP_ON_SB_DERR_M | \
					MME_CTRL_LO_QM_STOP_ON_NUMERIC_ERR_M)

#define ROT_MSS_SEI_MASK_QM_CP_SW_STOP_M	ROTATOR_MSS_SEI_CAUSE_I2_M

#define NIC_SEI_INTR_MASK			0x3F0
#define NIC_SPI_INTR_MASK_0			0xFFE00000
#define NIC_SPI_INTR_MASK_1			0x1

/* There is no dedicated header just for the special registers, so add defines using the special
 * registers defines of some arbitrary block.
 */
#define mmSPECIAL_MEM_NUMOF		mmVDEC_CTRL_SPECIAL_MEM_NUMOF
#define mmSPECIAL_MEM_ECC_SEL		mmVDEC_CTRL_SPECIAL_MEM_ECC_SEL
#define mmSPECIAL_MEM_ECC_CTL		mmVDEC_CTRL_SPECIAL_MEM_ECC_CTL
#define mmSPECIAL_MEM_ECC_ERR_STS	mmVDEC_CTRL_SPECIAL_MEM_ECC_ERR_STS
#define mmSPECIAL_MEM_ECC_ERR_ADDR	mmVDEC_CTRL_SPECIAL_MEM_ECC_ERR_ADDR

#define SPECIAL_MEM_ECC_CTL_DERR_CLR_S	VDEC_CTRL_SPECIAL_MEM_ECC_CTL_DERR_CLR_S
#define SPECIAL_MEM_ECC_CTL_DERR_CLR_M	VDEC_CTRL_SPECIAL_MEM_ECC_CTL_DERR_CLR_M

#define SPECIAL_MEM_ECC_ERR_STS_SYND_S	VDEC_CTRL_SPECIAL_MEM_ECC_ERR_STS_SYND_S
#define SPECIAL_MEM_ECC_ERR_STS_SYND_M	VDEC_CTRL_SPECIAL_MEM_ECC_ERR_STS_SYND_M
#define SPECIAL_MEM_ECC_ERR_STS_DERR_S	VDEC_CTRL_SPECIAL_MEM_ECC_ERR_STS_DERR_S
#define SPECIAL_MEM_ECC_ERR_STS_DERR_M	VDEC_CTRL_SPECIAL_MEM_ECC_ERR_STS_DERR_M

/* Common MSTR_IF_AXPROT definitions (HBW/LBW/ARPPROT/AWPROT defines are identical) */
#define MSTR_IF_AXPROT_0_OVRD_OVRD_EN_M	MSTR_IF_AXPROT_LBW_ARPROT_0_OVRD_OVRD_EN_M
#define MSTR_IF_AXPROT_0_OVRD_VAL_M	MSTR_IF_AXPROT_LBW_ARPROT_0_OVRD_VAL_M
#define MSTR_IF_AXPROT_1_OVRD_OVRD_EN_M	MSTR_IF_AXPROT_LBW_ARPROT_1_OVRD_OVRD_EN_M
#define MSTR_IF_AXPROT_1_OVRD_VAL_M	MSTR_IF_AXPROT_LBW_ARPROT_1_OVRD_VAL_M

#define VDEC_BRDG_CTRL_CAUSE_INTR_SPI_M	(VDEC_BRDG_CTRL_CAUSE_INTR_VCD_SPI_M | \
					VDEC_BRDG_CTRL_CAUSE_INTR_L2C_SPI_M | \
					VDEC_BRDG_CTRL_CAUSE_INTR_NRM_SPI_M | \
					VDEC_BRDG_CTRL_CAUSE_INTR_ABNRM_SPI_M)

/* we have up to 8 bmons */
#define NUM_OF_BMONS 8

/* A DUMMY block isn't a regular block, but in fact a block with a manually
 * configured block response, and used by PCIE 'Fabric Serialization' feature.
 * Although listed in SOL, it has no 'specs' record associated to it.
 * It's configured for DIE0 only, since DIE1 PCIE/features are disabled.
 */
#define mmD0_PIF_DUMMY_LBW_BLK_BASE		0xC41C000ull

/*
 * The interrupt map table for the CPU interrupts aggregators has the following order:
 * D0_CPU_INT_AGG_HDCORE0
 * ...
 * D0_CPU_INT_AGG_HDCORE3
 * D1_CPU_INT_AGG_HDCORE0
 * ...
 * D1_CPU_INT_AGG_HDCORE3
 * D0_CPU_INT_AGG_SHARED
 * D1_CPU_INT_AGG_SHARED
 *
 * Each CPU HDCORE aggregator aggregates 320 interrupts: 64 DERR, 64 SERR, 64 SEI and 128 SPI_ECO.
 * Each CPU SHARED aggregator aggregates 256 interrupts: 32 DERR, 32 SERR, 96 SEI and 96 SPI_ECO.
 *
 * The table entries number is therefore: [2 dies x (4 hdcore x 320 + 1 shared x 256)] = 3072.
 */
#define CPU_HDCORE_AGGR_DERR_GRP_INTR_OFFSET	0
#define CPU_HDCORE_AGGR_DERR_GRP_INTR_NUM	64
#define CPU_HDCORE_AGGR_SERR_GRP_INTR_OFFSET	(CPU_HDCORE_AGGR_DERR_GRP_INTR_OFFSET + \
						CPU_HDCORE_AGGR_DERR_GRP_INTR_NUM)
#define CPU_HDCORE_AGGR_SERR_GRP_INTR_NUM	64
#define CPU_HDCORE_AGGR_SEI_GRP_INTR_OFFSET	(CPU_HDCORE_AGGR_SERR_GRP_INTR_OFFSET + \
						CPU_HDCORE_AGGR_SERR_GRP_INTR_NUM)
#define CPU_HDCORE_AGGR_SEI_GRP_INTR_NUM	64
#define CPU_HDCORE_AGGR_SPI_GRP_INTR_OFFSET	(CPU_HDCORE_AGGR_SEI_GRP_INTR_OFFSET + \
						CPU_HDCORE_AGGR_SEI_GRP_INTR_NUM)
#define CPU_HDCORE_AGGR_SPI_GRP_INTR_NUM	128
#define CPU_HDCORE_AGGR_INTR_NUM		(CPU_HDCORE_AGGR_DERR_GRP_INTR_NUM + \
						CPU_HDCORE_AGGR_SERR_GRP_INTR_NUM + \
						CPU_HDCORE_AGGR_SEI_GRP_INTR_NUM + \
						CPU_HDCORE_AGGR_SPI_GRP_INTR_NUM)
#define CPU_HDCORE_AGGRS_INTR_NUM_PER_DIE	(CPU_INTR_AGGR_NUM_OF_HDCORE_AGGR * \
						CPU_HDCORE_AGGR_INTR_NUM)
#define CPU_SHARED_AGGR_DERR_GRP_INTR_OFFSET	0
#define CPU_SHARED_AGGR_DERR_GRP_INTR_NUM	32
#define CPU_SHARED_AGGR_SERR_GRP_INTR_OFFSET	(CPU_SHARED_AGGR_DERR_GRP_INTR_OFFSET + \
						CPU_SHARED_AGGR_DERR_GRP_INTR_NUM)
#define CPU_SHARED_AGGR_SERR_GRP_INTR_NUM	32
#define CPU_SHARED_AGGR_SEI_GRP_INTR_OFFSET	(CPU_SHARED_AGGR_SERR_GRP_INTR_OFFSET + \
						CPU_SHARED_AGGR_SERR_GRP_INTR_NUM)
#define CPU_SHARED_AGGR_SEI_GRP_INTR_NUM	96
#define CPU_SHARED_AGGR_SPI_GRP_INTR_OFFSET	(CPU_SHARED_AGGR_SEI_GRP_INTR_OFFSET + \
						CPU_SHARED_AGGR_SEI_GRP_INTR_NUM)
#define CPU_SHARED_AGGR_SPI_GRP_INTR_NUM	96
#define CPU_SHARED_AGGR_INTR_GRP_NUM		(CPU_SHARED_AGGR_DERR_GRP_INTR_NUM + \
						CPU_SHARED_AGGR_SERR_GRP_INTR_NUM + \
						CPU_SHARED_AGGR_SEI_GRP_INTR_NUM + \
						CPU_SHARED_AGGR_SPI_GRP_INTR_NUM)

#define GAUDI3_GLBL_ERR_CFG	(FIELD_PREP(QMAN_GLBL_ERR_CFG_CQF_ERR_MSG_EN_M, 0x1) |	\
				FIELD_PREP(QMAN_GLBL_ERR_CFG_CP_ERR_MSG_EN_M, 0x1) |	\
				FIELD_PREP(QMAN_GLBL_ERR_CFG_ARC_CQF_ERR_MSG_EN_M, 0x1))

#define RR_LBW_PRIV_SHORT_NUM_RANGES	13
#define RR_LBW_PRIV_SHORT_13_NUM_RANGES	1
#define RR_LBW_PRIV_NUM_RANGES		4

#define RR_HBW_PRIV_NUM_RANGES		7
#define RR_HBW_PRIV_7_NUM_RANGES	1

/*
 * The reset value of RR_LBW_PRIV_RANGE_MAX_SHORT_13, RR_HBW_PRIV_RANGE_MAX_HI_7 and
 * RR_HBW_PRIV_RANGE_MAX_LO_7 are 0x1FFFF000, 0xFFFFFFFF and 0xFFFFC000, respectively.
 * The values are configured in both MAX and MIN registers in order to disable these RRs.
 *
 * "CFG_BAR_BASE - LBW_BASE" is subtracted from the LBW value, to compensate the addition of the
 * same value by REG_OFF_TO_LBW_OFF() which is called in gaudi3_rtr_ctrl_config_rr().
 */
#define RR_LBW_PRIV_RANGE_SHORT_13_DISABLED_VAL		(0x1FFFF000 - (CFG_BAR_BASE - LBW_BASE))
#define RR_HBW_PRIV_RANGE_7_DISABLED_VAL		0xFFFFFFFFFFFFC000ULL

#define INT_AGG_SHARED_SEI_INT_MSG_STS_0_PARC_0_S	25
#define INT_AGG_SHARED_SEI_INT_MSG_STS_0_PARC_0_M	0x2000000

struct qm_sw_event_info {
	enum hl_agg_component_type comp;
	u32 instance;
	u32 hd;
	u32 die;
	u32 base;
};

/*
 * based on gaudi3_qm_irq_map_table in embedded/specs/gaudi3/gaudi3_interrupt.h.
 */
static const struct qm_sw_event_info gaudi3_qm_irq_map_table[] = {
	/* hd0 */
	{.comp = INT_COMP_TYPE_MME, .instance = 0, .hd = 0, .die = 0, .base = mmHD0_MME_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 0, .hd = 0, .die = 0, .base = mmHD0_TPC0_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 1, .hd = 0, .die = 0, .base = mmHD0_TPC1_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 2, .hd = 0, .die = 0, .base = mmHD0_TPC2_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 3, .hd = 0, .die = 0, .base = mmHD0_TPC3_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 4, .hd = 0, .die = 0, .base = mmHD0_TPC4_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 5, .hd = 0, .die = 0, .base = mmHD0_TPC5_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 6, .hd = 0, .die = 0, .base = mmHD0_TPC6_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 7, .hd = 0, .die = 0, .base = mmHD0_TPC7_QM_BASE},

	/* hd1 */
	{.comp = INT_COMP_TYPE_MME, .instance = 0, .hd = 1, .die = 0, .base = mmHD1_MME_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 0, .hd = 1, .die = 0, .base = mmHD1_TPC0_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 1, .hd = 1, .die = 0, .base = mmHD1_TPC1_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 2, .hd = 1, .die = 0, .base = mmHD1_TPC2_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 3, .hd = 1, .die = 0, .base = mmHD1_TPC3_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 4, .hd = 1, .die = 0, .base = mmHD1_TPC4_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 5, .hd = 1, .die = 0, .base = mmHD1_TPC5_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 6, .hd = 1, .die = 0, .base = mmHD1_TPC6_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 7, .hd = 1, .die = 0, .base = mmHD1_TPC7_QM_BASE},

	{.comp = INT_COMP_TYPE_ROT, .instance = 0, .hd = 1, .die = 0, .base = mmHD1_ROT0_QM_BASE},
	{.comp = INT_COMP_TYPE_ROT, .instance = 1, .hd = 1, .die = 0, .base = mmHD1_ROT1_QM_BASE},

	{.comp = INT_COMP_TYPE_EDMA, .instance = 0, .hd = 1, .die = 0,
					.base = mmHD1_SEDMA0_QM_BASE},
	{.comp = INT_COMP_TYPE_EDMA, .instance = 1, .hd = 1, .die = 0,
					.base = mmHD1_SEDMA1_QM_BASE},

	/* hd2 */
	{.comp = INT_COMP_TYPE_MME, .instance = 0, .hd = 2, .die = 0, .base = mmHD2_MME_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 0, .hd = 2, .die = 0, .base = mmHD2_TPC0_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 1, .hd = 2, .die = 0, .base = mmHD2_TPC1_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 2, .hd = 2, .die = 0, .base = mmHD2_TPC2_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 3, .hd = 2, .die = 0, .base = mmHD2_TPC3_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 4, .hd = 2, .die = 0, .base = mmHD2_TPC4_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 5, .hd = 2, .die = 0, .base = mmHD2_TPC5_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 6, .hd = 2, .die = 0, .base = mmHD2_TPC6_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 7, .hd = 2, .die = 0, .base = mmHD2_TPC7_QM_BASE},

	/* hd3 */
	{.comp = INT_COMP_TYPE_MME, .instance = 0, .hd = 3, .die = 0, .base = mmHD3_MME_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 0, .hd = 3, .die = 0, .base = mmHD3_TPC0_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 1, .hd = 3, .die = 0, .base = mmHD3_TPC1_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 2, .hd = 3, .die = 0, .base = mmHD3_TPC2_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 3, .hd = 3, .die = 0, .base = mmHD3_TPC3_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 4, .hd = 3, .die = 0, .base = mmHD3_TPC4_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 5, .hd = 3, .die = 0, .base = mmHD3_TPC5_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 6, .hd = 3, .die = 0, .base = mmHD3_TPC6_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 7, .hd = 3, .die = 0, .base = mmHD3_TPC7_QM_BASE},

	{.comp = INT_COMP_TYPE_ROT, .instance = 0, .hd = 3, .die = 0, .base = mmHD3_ROT0_QM_BASE},
	{.comp = INT_COMP_TYPE_ROT, .instance = 1, .hd = 3, .die = 0, .base = mmHD3_ROT1_QM_BASE},
	{.comp = INT_COMP_TYPE_EDMA, .instance = 0, .hd = 3, .die = 0,
					.base = mmHD3_SEDMA0_QM_BASE},
	{.comp = INT_COMP_TYPE_EDMA, .instance = 1, .hd = 3, .die = 0,
					.base = mmHD3_SEDMA1_QM_BASE},

	/* hd7 */
	{.comp = INT_COMP_TYPE_MME, .instance = 0, .hd = 7, .die = 1, .base = mmHD7_MME_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 0, .hd = 7, .die = 1, .base = mmHD7_TPC0_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 1, .hd = 7, .die = 1, .base = mmHD7_TPC1_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 2, .hd = 7, .die = 1, .base = mmHD7_TPC2_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 3, .hd = 7, .die = 1, .base = mmHD7_TPC3_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 4, .hd = 7, .die = 1, .base = mmHD7_TPC4_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 5, .hd = 7, .die = 1, .base = mmHD7_TPC5_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 6, .hd = 7, .die = 1, .base = mmHD7_TPC6_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 7, .hd = 7, .die = 1, .base = mmHD7_TPC7_QM_BASE},

	/* hd6 */
	{.comp = INT_COMP_TYPE_MME, .instance = 0, .hd = 6, .die = 1, .base = mmHD6_MME_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 0, .hd = 6, .die = 1, .base = mmHD6_TPC0_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 1, .hd = 6, .die = 1, .base = mmHD6_TPC1_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 2, .hd = 6, .die = 1, .base = mmHD6_TPC2_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 3, .hd = 6, .die = 1, .base = mmHD6_TPC3_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 4, .hd = 6, .die = 1, .base = mmHD6_TPC4_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 5, .hd = 6, .die = 1, .base = mmHD6_TPC5_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 6, .hd = 6, .die = 1, .base = mmHD6_TPC6_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 7, .hd = 6, .die = 1, .base = mmHD6_TPC7_QM_BASE},

	{.comp = INT_COMP_TYPE_ROT, .instance = 0, .hd = 6, .die = 1, .base = mmHD6_ROT0_QM_BASE},
	{.comp = INT_COMP_TYPE_ROT, .instance = 1, .hd = 6, .die = 1, .base = mmHD6_ROT1_QM_BASE},
	{.comp = INT_COMP_TYPE_EDMA, .instance = 0, .hd = 6, .die = 1,
					.base = mmHD6_SEDMA0_QM_BASE},
	{.comp = INT_COMP_TYPE_EDMA, .instance = 1, .hd = 6, .die = 1,
					.base = mmHD6_SEDMA1_QM_BASE},

	/* hd5 */
	{.comp = INT_COMP_TYPE_MME, .instance = 0, .hd = 5, .die = 1, .base = mmHD5_MME_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 0, .hd = 5, .die = 1, .base = mmHD5_TPC0_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 1, .hd = 5, .die = 1, .base = mmHD5_TPC1_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 2, .hd = 5, .die = 1, .base = mmHD5_TPC2_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 3, .hd = 5, .die = 1, .base = mmHD5_TPC3_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 4, .hd = 5, .die = 1, .base = mmHD5_TPC4_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 5, .hd = 5, .die = 1, .base = mmHD5_TPC5_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 6, .hd = 5, .die = 1, .base = mmHD5_TPC6_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 7, .hd = 5, .die = 1, .base = mmHD5_TPC7_QM_BASE},

	/* hd4 */
	{.comp = INT_COMP_TYPE_MME, .instance = 0, .hd = 4, .die = 1, .base = mmHD4_MME_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 0, .hd = 4, .die = 1, .base = mmHD4_TPC0_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 1, .hd = 4, .die = 1, .base = mmHD4_TPC1_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 2, .hd = 4, .die = 1, .base = mmHD4_TPC2_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 3, .hd = 4, .die = 1, .base = mmHD4_TPC3_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 4, .hd = 4, .die = 1, .base = mmHD4_TPC4_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 5, .hd = 4, .die = 1, .base = mmHD4_TPC5_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 6, .hd = 4, .die = 1, .base = mmHD4_TPC6_QM_BASE},
	{.comp = INT_COMP_TYPE_TPC, .instance = 7, .hd = 4, .die = 1, .base = mmHD4_TPC7_QM_BASE},

	{.comp = INT_COMP_TYPE_ROT, .instance = 0, .hd = 4, .die = 1, .base = mmHD4_ROT0_QM_BASE},
	{.comp = INT_COMP_TYPE_ROT, .instance = 1, .hd = 4, .die = 1, .base = mmHD4_ROT1_QM_BASE},
	{.comp = INT_COMP_TYPE_EDMA, .instance = 0, .hd = 4, .die = 1,
					.base = mmHD4_SEDMA0_QM_BASE},
	{.comp = INT_COMP_TYPE_EDMA, .instance = 1, .hd = 4, .die = 1,
					.base = mmHD4_SEDMA1_QM_BASE},
};

/* The meaning of the bits in the interrupt mask register is:
 *    [3..0] - EQ event interrupt (1 bit per EQ)
 *    [4..7] - EQ error interrupt (1 bit per EQ)
 * i.e. EQ error shift = EQ event shift + 4
 */
#define GAUDI3_NIC_EQ_INTERRUPT_S(port) ((is_400g_mode(hdev) || !(port & 1)) ? 0 : 2)
#define GAUDI3_NIC_EQ_ERR_INTERRUPT_M(port) BIT(GAUDI3_NIC_EQ_INTERRUPT_S(port) + 4)

struct gaudi3_cpu_aggr_intr_map {
	u32 grp_type;
	u32 comp_type;
	u32 comp_inst;
	u32 hdcore_type;
	u32 die_id;
};

struct tlb_init_data {
	u32 cntrl_page_size;
};

enum err_grp {
	ERR_GRP_DERR,
	ERR_GRP_SERR,
	ERR_GRP_SEI,
	ERR_GRP_SPI_ECO,
};

struct eq_agg_header_params {
	enum hl_agg_component_type component_type;
	enum err_grp grp_type;
	u32 die;
	u32 hdcore;
	u32 instance;
};

enum shared_handler_type {
	SHARED_PCIE_EVENT,
	SHARED_NIC_EVENT,
	SHARED_NCH_EVENT,
	SHARED_PMMU_EVENT,
	SHARED_TS_EVENT,
	SHARED_PDMA_EVENT,
	SHARED_D2D_EVENT,
	SHARED_CPU_EVENT,
	SHARED_VM_EVENT,
	SHARED_PLL_EVENT,
	SHARED_PSOC_EVENT,
	SHARED_PARC_EVENT,
	SHARED_GLINK_EVENT,
	SHARED_RTR_EVENT,
};

enum hdcore_handler_type {
	HDCORE_TPC_EVENT,
	HDCORE_MME_EVENT,
	HDCORE_ROT_EVENT,
	HDCORE_CS_EVENT,
	HDCORE_STLB_EVENT,
	HDCORE_HBM_EVENT,
	HDCORE_SOB_EVENT,
	HDCORE_ARCFARM_EVENT,
	HDCORE_DEC_EVENT,
	HDCORE_DUP_EVENT,
	HDCORE_EDMA_EVENT,
	HDCORE_RTR_EVENT,
	HDCORE_HIF_EVENT,
};

enum sei_intr_idx {
	SEI_INTR_MME0_SBTE0,
	SEI_INTR_MME0_SBTE1,
	SEI_INTR_MME0_SBTE2,
	SEI_INTR_MME0_SBTE3,
	SEI_INTR_MME1_SBTE0,
	SEI_INTR_MME1_SBTE1,
	SEI_INTR_MME1_SBTE2,
	SEI_INTR_MME1_SBTE3,
	SEI_INTR_MME0_ACC,
	SEI_INTR_MME1_ACC,
	SEI_INTR_MME_CTRL0_LO0,
	SEI_INTR_MME_CTRL0_LO1,
};

enum spi_intr_idx {
	SPI_INTR_MME0_SBTE0_1_CS_DBG,
	SPI_INTR_MME0_SBTE0,
	SPI_INTR_MME0_SBTE1,
	SPI_INTR_MME0_SBTE2_3_CS_DBG,
	SPI_INTR_MME0_SBTE2,
	SPI_INTR_MME0_SBTE3,
	SPI_INTR_MME1_SBTE0_1_CS_DBG,
	SPI_INTR_MME1_SBTE0,
	SPI_INTR_MME1_SBTE1,
	SPI_INTR_MME1_SBTE2_3_CS_DBG,
	SPI_INTR_MME1_SBTE2,
	SPI_INTR_MME1_SBTE3,
	SPI_INTR_MME_QM_CS,
	SPI_INTR_MME0_ACC,
	SPI_INTR_MME1_ACC,
	SPI_INTR_TPC0_BMON_SPMU,
	SPI_INTR_TPC0_KERNEL_ERR,
	SPI_INTR_TPC1_BMON_SPMU,
	SPI_INTR_TPC1_KERNEL_ERR,
	SPI_INTR_TPC2_BMON_SPMU,
	SPI_INTR_TPC2_KERNEL_ERR,
	SPI_INTR_TPC3_BMON_SPMU,
	SPI_INTR_TPC3_KERNEL_ERR,
	SPI_INTR_TPC4_BMON_SPMU,
	SPI_INTR_TPC4_KERNEL_ERR,
	SPI_INTR_TPC5_BMON_SPMU,
	SPI_INTR_TPC5_KERNEL_ERR,
	SPI_INTR_TPC6_BMON_SPMU,
	SPI_INTR_TPC6_KERNEL_ERR,
	SPI_INTR_TPC7_BMON_SPMU,
	SPI_INTR_TPC7_KERNEL_ERR,
	SPI_INTR_TPC8_BMON_SPMU,
	SPI_INTR_TPC8_KERNEL_ERR,
	SPI_INTR_ROT0_ERR,
	SPI_INTR_ROT0_RSVD,
	SPI_INTR_ROT1_ERR,
	SPI_INTR_ROT1_RSVD,
	SPI_INTR_CS0_TRACE_N_DBG,
	SPI_INTR_CS0_RSVD,
	SPI_INTR_CS1_TRACE_N_DBG,
	SPI_INTR_CS1_RSVD,
	SPI_INTR_CS2_TRACE_N_DBG,
	SPI_INTR_CS2_RSVD,
	SPI_INTR_CS3_TRACE_N_DBG,
	SPI_INTR_CS3_RSVD,
	SPI_INTR_CS4_TRACE_N_DBG,
	SPI_INTR_CS4_RSVD,
	SPI_INTR_CS5_TRACE_N_DBG,
	SPI_INTR_CS5_RSVD,
	SPI_INTR_CS6_TRACE_N_DBG,
	SPI_INTR_CS6_RSVD,
	SPI_INTR_CS7_TRACE_N_DBG,
	SPI_INTR_CS7_RSVD,
	SPI_INTR_STLB_FAULT,
	SPI_INTR_STLB_MAINT_QUEUE_FULL,
	SPI_INTR_STLB_SW_PREFETCH_FAIL,
	SPI_INTR_STLB_RSVD,
	SPI_INTR_EDMA0_TRACE_N_DBG,
	SPI_INTR_EDMA1_TRACE_N_DBG,
	SPI_INTR_MC0,
	SPI_INTR_MC1,
	SPI_INTR_MC2,
	SPI_INTR_MC3,
	SPI_INTR_SOB0_TRACE_N_DBG,
	SPI_INTR_ARC_FARM0_RSVD,
	SPI_INTR_ARC_FARM1_RSVD,
	SPI_INTR_DEC0,
	SPI_INTR_DEC0_TRACE_N_DBG,
	SPI_INTR_DEC1,
	SPI_INTR_DEC1_TRACE_N_DBG,
};

enum eco_bfe {
	NIC_ENABLE_H9_RX_DROP_ECO = 0,
	NIC_ENABLE_H9_QP_DOORBELLS_ECO,
	NIC_ENABLE_H9_CC_MSG_DROPS_ECO,
	NIC_ENABLE_H9_REMOTE_PI_UPDATE_ECO,
	NIC_ENABLE_H9_RXB_MEM_DEADLOCK_ECO,
	NIC_ENABLE_H9_SINGLE_QP_PERF_FIX_ECO,
	NIC_ENABLE_H9_SAL_OVERRIDE_ECO,
	NIC_ENABLE_H9_SACK_DEADLOCK_ECO,
	NIC_ENABLE_H9_TXE_BUFF_ALLOC_ECO,
	NIC_ENABLE_H9_PHY_MAC_HANG_ECO,
};

#define SPI_INTR_STLB_BASE SPI_INTR_STLB_FAULT

typedef void (*shared_aggr_handle_and_clear)(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);

typedef void (*hdcore_aggr_handle_and_clear)(struct hl_device *hdev, u32 die, u32 hdcore,
					u32 instance, enum err_grp type, u32 sts, u32 sts_idx,
					u32 idx, u32 aggr_mask_reg, u32 event_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);

static void handle_and_clear_pcie_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_psoc_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_pmmu_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_pdma_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_tpc_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_mme_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_rotator_events(struct hl_device *hdev, u32 die, u32 hdcore,
					u32 instance, enum err_grp type, u32 sts, u32 sts_idx,
					u32 idx, u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_stlb_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_arc_farm_events(struct hl_device *hdev, u32 die, u32 hdcore,
					u32 instance, enum err_grp type, u32 sts, u32 sts_idx,
					u32 idx, u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_decoder_events(struct hl_device *hdev, u32 die, u32 hdcore,
					u32 instance, enum err_grp type, u32 sts, u32 sts_idx,
					u32 idx, u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_edma_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_nic_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_cs_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_hbm_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);
static void handle_and_clear_sob_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry);

static const shared_aggr_handle_and_clear shared_handle_and_clear[] = {
	handle_and_clear_pcie_events, /* SHARED_PCIE_EVENT */
	handle_and_clear_nic_events, /* SHARED_NIC_EVENT */
	NULL, /* SHARED_NCH_EVENT */
	handle_and_clear_pmmu_events, /* SHARED_PMMU_EVENT */
	NULL, /* SHARED_TS_EVENT */
	handle_and_clear_pdma_events, /* SHARED_PDMA_EVENT */
	NULL, /* SHARED_D2D_EVENT */
	NULL, /* SHARED_CPU_EVENT */
	NULL, /* SHARED_VM_EVENT */
	NULL, /* SHARED_PLL_EVENT */
	handle_and_clear_psoc_events, /* SHARED_PSOC_EVENT */
	NULL, /* SHARED_PARC_EVENT */
	NULL, /* SHARED_GLINK_EVENT */
	NULL, /* SHARED_RTR_EVENT */
};

static const hdcore_aggr_handle_and_clear hdcore_handle_and_clear[] = {
	handle_and_clear_tpc_events, /* HDCORE_TPC_EVENT */
	handle_and_clear_mme_events, /* HDCORE_MME_EVENT */
	handle_and_clear_rotator_events, /* HDCORE_ROT_EVENT */
	handle_and_clear_cs_events, /* HDCORE_CS_EVENT */
	handle_and_clear_stlb_events, /* HDCORE_STLB_EVENT */
	handle_and_clear_hbm_events, /* HDCORE_HBM_EVENT */
	handle_and_clear_sob_events, /* HDCORE_SOB_EVENT */
	handle_and_clear_arc_farm_events, /* HDCORE_ARCFARM_EVENT */
	handle_and_clear_decoder_events, /* HDCORE_DEC_EVENT */
	NULL, /* HDCORE_DUP_EVENT */
	handle_and_clear_edma_events, /* HDCORE_EDMA_EVENT */
	NULL, /* HDCORE_RTR_EVENT */
	NULL, /* HDCORE_HIF_EVENT */
};

#define PCIE_AUX_DBI_CS2_0		0x0
#define PCIE_AUX_DBI_CS2_1		0x300000

/* Interrupt coalescing window of ~1 usec */
#define GAUDI3_MSIX_COALESCING_CNT	0x400

enum gaudi3_pll_index {
	GAUDI3_D0_G0_D2D_PLL = 0,
	GAUDI3_D0_G6_D2D_PLL = 1,
	GAUDI3_D0_G0_MME_PLL = 2,
	GAUDI3_D0_G3_MME_PLL = 3,
	GAUDI3_D0_G0_TPC_PLL = 4,
	GAUDI3_D0_G2_TPC_PLL = 5,
	GAUDI3_D0_G4_TPC_PLL = 6,
	GAUDI3_D0_G6_TPC_PLL = 7,
	GAUDI3_D0_G0_HBM_PLL = 8,
	GAUDI3_D0_G2_HBM_PLL = 9,
	GAUDI3_D0_G4_HBM_PLL = 10,
	GAUDI3_D0_G6_HBM_PLL = 11,
	GAUDI3_D0_G0_MESH_PLL = 12,
	GAUDI3_D0_G6_MESH_PLL = 13,
	GAUDI3_D0_G1_CS_PLL  = 14,
	GAUDI3_D0_G5_CS_PLL  = 15,
	GAUDI3_D0_G1_DMA_PLL = 16,
	GAUDI3_D0_G1_C2C_PLL = 17,
	GAUDI3_D0_G5_C2C_PLL = 18,
	GAUDI3_D0_G2_MEDIA_PLL = 19,
	GAUDI3_D0_G6_MEDIA_PLL = 20,
	GAUDI3_D0_G4_NCH_PLL = 21,
	GAUDI3_D0_G4_PCI_PLL = 22,
	GAUDI3_D0_G5_C2M_PLL = 23,
	GAUDI3_D0_G2_NIC_PLL = 24,
	GAUDI3_PLL_MAX
};

static const u32 gaudi3_pll_block_bases[] = {
	[GAUDI3_D0_G0_D2D_PLL] = mmD0_G0_D2D_PLL_CTRL_BASE,
	[GAUDI3_D0_G6_D2D_PLL] = mmD0_G6_D2D_PLL_CTRL_BASE,
	[GAUDI3_D0_G0_MME_PLL] = mmD0_G0_MME_PLL_CTRL_BASE,
	[GAUDI3_D0_G3_MME_PLL] = mmD0_G3_MME_PLL_CTRL_BASE,
	[GAUDI3_D0_G0_TPC_PLL] = mmD0_G0_TPC_PLL_CTRL_BASE,
	[GAUDI3_D0_G2_TPC_PLL] = mmD0_G2_TPC_PLL_CTRL_BASE,
	[GAUDI3_D0_G4_TPC_PLL] = mmD0_G4_TPC_PLL_CTRL_BASE,
	[GAUDI3_D0_G6_TPC_PLL] = mmD0_G6_TPC_PLL_CTRL_BASE,
	[GAUDI3_D0_G0_HBM_PLL] = mmD0_G0_HBM_PLL_CTRL_BASE,
	[GAUDI3_D0_G2_HBM_PLL] = mmD0_G2_HBM_PLL_CTRL_BASE,
	[GAUDI3_D0_G4_HBM_PLL] = mmD0_G4_HBM_PLL_CTRL_BASE,
	[GAUDI3_D0_G6_HBM_PLL] = mmD0_G6_HBM_PLL_CTRL_BASE,
	[GAUDI3_D0_G0_MESH_PLL] = mmD0_G0_MSH_PLL_CTRL_BASE,
	[GAUDI3_D0_G6_MESH_PLL] = mmD0_G6_MSH_PLL_CTRL_BASE,
	[GAUDI3_D0_G1_CS_PLL] = mmD0_G1_CS_PLL_CTRL_BASE,
	[GAUDI3_D0_G5_CS_PLL] = mmD0_G5_CS_PLL_CTRL_BASE,
	[GAUDI3_D0_G1_DMA_PLL] = mmD0_G1_DMA_PLL_CTRL_BASE,
	[GAUDI3_D0_G1_C2C_PLL] = mmD0_G1_C2C_PLL_CTRL_BASE,
	[GAUDI3_D0_G5_C2C_PLL] = mmD0_G5_C2C_PLL_CTRL_BASE,
	[GAUDI3_D0_G2_MEDIA_PLL] = mmD0_G2_MEDIA_PLL_CTRL_BASE,
	[GAUDI3_D0_G6_MEDIA_PLL] = mmD0_G6_MEDIA_PLL_CTRL_BASE,
	[GAUDI3_D0_G4_NCH_PLL] = mmD0_G4_NCH_PLL_CTRL_BASE,
	[GAUDI3_D0_G4_PCI_PLL] = mmD0_G4_PCI_PLL_CTRL_BASE,
	[GAUDI3_D0_G5_C2M_PLL] = mmD0_G5_C2M_PLL_CTRL_BASE,
	[GAUDI3_D0_G2_NIC_PLL] = mmD0_G2_NIC_PLL_CTRL_BASE,
};

struct gaudi3_etr_ac_config gaudi3_etr_ac_config[GAUDI3_NUM_ETR] = {
	[GAUDI3_D0_PSOC_ETR] = {
		mmD0_PSOC_ETR_AC_BASE - mmD0_NCH_AC_BASE,
		mmD0_PSOC_ETR_BASE - mmD0_NCH_ETR_BASE
		},
	[GAUDI3_D0_NCH_ETR] = {
		mmD0_NCH_AC_BASE - mmD0_NCH_AC_BASE,
		mmD0_NCH_ETR_BASE - mmD0_NCH_ETR_BASE
		},
	[GAUDI3_D1_PSOC_ETR] = {
		mmD1_PSOC_ETR_AC_BASE - mmD0_NCH_AC_BASE,
		mmD1_PSOC_ETR_BASE - mmD0_NCH_ETR_BASE
		},
	[GAUDI3_D1_NCH_ETR] = {
		mmD1_NCH_AC_BASE - mmD0_NCH_AC_BASE,
		mmD1_NCH_ETR_BASE - mmD0_NCH_ETR_BASE
		},
};

#define GAUDI3_INIT_PLL_COEFFICIENT(pll, refdiv, fbdiv, pdiv1, pdiv2) \
do { \
	pll.div_cfg = FIELD_PREP(PLL_DIV_CFG_REFDIV_MASK, refdiv); \
	pll.div_cfg |= FIELD_PREP(PLL_DIV_CFG_FBDIV_MASK, fbdiv); \
	pll.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV1_MASK, pdiv1); \
	pll.div_cfg |= FIELD_PREP(PLL_DIV_CFG_POSTDIV2_MASK, pdiv2); \
} while (0)

#define GAUDI3_INIT_PLL_DIV_SELECTOR(pll, s0, s1, s2, s3) \
do { \
	pll.div_sel[0] = s0; \
	pll.div_sel[1] = s1; \
	pll.div_sel[2] = s2; \
	pll.div_sel[3] = s3; \
} while (0)

#define GAUDI3_INIT_PLL_DIVIDER_VAL(pll, d0, d1, d2, d3) \
do { \
	pll.div_fact[0] = d0; \
	pll.div_fact[1] = d1; \
	pll.div_fact[2] = d2; \
	pll.div_fact[3] = d3; \
} while (0)

/* bit 28 cleared signals mesh to send TX to SPI gateway */
#define D2D_SPI_GW_MASK	(~BIT(28))

static const u32 gaudi3_sched_arc_af_blocks_bases[CPU_ID_SCHED_MAX] = {
	[CPU_ID_SCHED_ARC0] = mmHD0_ARC_FARM_ARC0_AF_BASE,
	[CPU_ID_SCHED_ARC1] = mmHD0_ARC_FARM_ARC1_AF_BASE,
	[CPU_ID_SCHED_ARC2] = mmHD1_ARC_FARM_ARC0_AF_BASE,
	[CPU_ID_SCHED_ARC3] = mmHD1_ARC_FARM_ARC1_AF_BASE,
	[CPU_ID_SCHED_ARC4] = mmHD2_ARC_FARM_ARC0_AF_BASE,
	[CPU_ID_SCHED_ARC5] = mmHD2_ARC_FARM_ARC1_AF_BASE,
	[CPU_ID_SCHED_ARC6] = mmHD3_ARC_FARM_ARC0_AF_BASE,
	[CPU_ID_SCHED_ARC7] = mmHD3_ARC_FARM_ARC1_AF_BASE,
	[CPU_ID_SCHED_ARC8] = mmHD4_ARC_FARM_ARC0_AF_BASE,
	[CPU_ID_SCHED_ARC9] = mmHD4_ARC_FARM_ARC1_AF_BASE,
	[CPU_ID_SCHED_ARC10] = mmHD5_ARC_FARM_ARC0_AF_BASE,
	[CPU_ID_SCHED_ARC11] = mmHD5_ARC_FARM_ARC1_AF_BASE,
	[CPU_ID_SCHED_ARC12] = mmHD6_ARC_FARM_ARC0_AF_BASE,
	[CPU_ID_SCHED_ARC13] = mmHD6_ARC_FARM_ARC1_AF_BASE,
	[CPU_ID_SCHED_ARC14] = mmHD7_ARC_FARM_ARC0_AF_BASE,
	[CPU_ID_SCHED_ARC15] = mmHD7_ARC_FARM_ARC1_AF_BASE
};

enum gaudi3_rotator_err_id {
	RSB_RR_ERROR,
	RSB_NUM_ERROR,
	RSB_SLV_ERROR,
	MRSB_RR_ERROR,
	MRSB_NUM_ERROR,
	MRSB_SLV_ERROR,
	GRSB_RR_ERROR,
	GRSB_NUM_ERROR,
	GRSB_SLV_ERROR,
	WCH_CH0_RR_ERROR,
	WCH_CH0_PINF_ERROR,
	WCH_CH0_NINF_ERROR,
	WCH_CH0_NAN_ERROR,
	WCH_CH0_SLV_ERROR,
	WCH_CH1_RR_ERROR,
	WCH_CH1_PINF_ERROR,
	WCH_CH1_NINF_ERROR,
	WCH_CH1_NAN_ERROR,
	WCH_CH1_SLV_ERROR,
	RINTERP_PINF_ERROR,
	RINTERP_NINF_ERROR,
	RINTERP_NAN_ERROR,
	MINTERP_PINF_ERROR,
	MINTERP_NINF_ERROR,
	MINTERP_NAN_ERROR,
	COORD_PINF_ERROR,
	COORD_NINF_ERROR,
	COORD_NAN_ERROR
};

static struct rotator_err_ctx_id_reg {
	u32 offset;
	u32 shift;
	u32 mask;
} gaudi3_rotator_err_ctx_id_regs[] = {
	[RSB_RR_ERROR] = {mmROTATOR_RSB_ERR_CONTEXT_ID,
				ROTATOR_RSB_ERR_CONTEXT_ID_RSB_S,
				ROTATOR_RSB_ERR_CONTEXT_ID_RSB_M},
	[RSB_NUM_ERROR] = {mmROTATOR_RSB_ERR_CONTEXT_ID,
				ROTATOR_RSB_ERR_CONTEXT_ID_RSB_S,
				ROTATOR_RSB_ERR_CONTEXT_ID_RSB_M},
	[RSB_SLV_ERROR] = {mmROTATOR_RSB_ERR_CONTEXT_ID,
				ROTATOR_RSB_ERR_CONTEXT_ID_RSB_S,
				ROTATOR_RSB_ERR_CONTEXT_ID_RSB_M},
	[MRSB_RR_ERROR] = {mmROTATOR_GRSB_MRSB_ERR_CONTEXT_ID,
				ROTATOR_GRSB_MRSB_ERR_CONTEXT_ID_MRSB_S,
				ROTATOR_GRSB_MRSB_ERR_CONTEXT_ID_MRSB_M},
	[MRSB_NUM_ERROR] = {mmROTATOR_GRSB_MRSB_ERR_CONTEXT_ID,
				ROTATOR_GRSB_MRSB_ERR_CONTEXT_ID_MRSB_S,
				ROTATOR_GRSB_MRSB_ERR_CONTEXT_ID_MRSB_M},
	[MRSB_SLV_ERROR] = {mmROTATOR_GRSB_MRSB_ERR_CONTEXT_ID,
				ROTATOR_GRSB_MRSB_ERR_CONTEXT_ID_MRSB_S,
				ROTATOR_GRSB_MRSB_ERR_CONTEXT_ID_MRSB_M},
	[GRSB_RR_ERROR] = {mmROTATOR_GRSB_MRSB_ERR_CONTEXT_ID,
				ROTATOR_GRSB_MRSB_ERR_CONTEXT_ID_GRSB_S,
				ROTATOR_GRSB_MRSB_ERR_CONTEXT_ID_GRSB_M},
	[GRSB_NUM_ERROR] = {mmROTATOR_GRSB_MRSB_ERR_CONTEXT_ID,
				ROTATOR_GRSB_MRSB_ERR_CONTEXT_ID_GRSB_S,
				ROTATOR_GRSB_MRSB_ERR_CONTEXT_ID_GRSB_M},
	[GRSB_SLV_ERROR] = {mmROTATOR_GRSB_MRSB_ERR_CONTEXT_ID,
				ROTATOR_GRSB_MRSB_ERR_CONTEXT_ID_GRSB_S,
				ROTATOR_GRSB_MRSB_ERR_CONTEXT_ID_GRSB_M},
	[WCH_CH0_RR_ERROR] = {mmROTATOR_WCH_ERR_CONTEXT_ID,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL0_S,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL0_M},
	[WCH_CH0_PINF_ERROR] = {mmROTATOR_WCH_ERR_CONTEXT_ID,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL0_S,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL0_M},
	[WCH_CH0_NINF_ERROR] = {mmROTATOR_WCH_ERR_CONTEXT_ID,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL0_S,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL0_M},
	[WCH_CH0_NAN_ERROR] = {mmROTATOR_WCH_ERR_CONTEXT_ID,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL0_S,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL0_M},
	[WCH_CH0_SLV_ERROR] = {mmROTATOR_WCH_ERR_CONTEXT_ID,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL0_S,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL0_M},
	[WCH_CH1_RR_ERROR] = {mmROTATOR_WCH_ERR_CONTEXT_ID,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL1_S,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL1_M},
	[WCH_CH1_PINF_ERROR] = {mmROTATOR_WCH_ERR_CONTEXT_ID,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL1_S,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL1_M},
	[WCH_CH1_NINF_ERROR] = {mmROTATOR_WCH_ERR_CONTEXT_ID,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL1_S,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL1_M},
	[WCH_CH1_NAN_ERROR] = {mmROTATOR_WCH_ERR_CONTEXT_ID,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL1_S,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL1_M},
	[WCH_CH1_SLV_ERROR] = {mmROTATOR_WCH_ERR_CONTEXT_ID,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL1_S,
				ROTATOR_WCH_ERR_CONTEXT_ID_CHANNEL1_M},
	[RINTERP_PINF_ERROR] = {mmROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID,
				ROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID_RINTERP_S,
				ROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID_RINTERP_M},
	[RINTERP_NINF_ERROR] = {mmROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID,
				ROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID_RINTERP_S,
				ROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID_RINTERP_M},
	[RINTERP_NAN_ERROR] = {mmROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID,
				ROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID_RINTERP_S,
				ROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID_RINTERP_M},
	[MINTERP_PINF_ERROR] = {mmROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID,
				ROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID_MINTERP_S,
				ROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID_MINTERP_M},
	[MINTERP_NINF_ERROR] = {mmROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID,
				ROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID_MINTERP_S,
				ROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID_MINTERP_M},
	[MINTERP_NAN_ERROR] = {mmROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID,
				ROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID_MINTERP_S,
				ROTATOR_RINTERP_MINTERP_NUM_ERR_CONTEXT_ID_MINTERP_M},
	[COORD_PINF_ERROR] = {mmROTATOR_COORD_NUM_ERR_CONTEXT_ID,
				ROTATOR_COORD_NUM_ERR_CONTEXT_ID_COORD_S,
				ROTATOR_COORD_NUM_ERR_CONTEXT_ID_COORD_M},
	[COORD_NINF_ERROR] = {mmROTATOR_COORD_NUM_ERR_CONTEXT_ID,
				ROTATOR_COORD_NUM_ERR_CONTEXT_ID_COORD_S,
				ROTATOR_COORD_NUM_ERR_CONTEXT_ID_COORD_M},
	[COORD_NAN_ERROR] = {mmROTATOR_COORD_NUM_ERR_CONTEXT_ID,
				ROTATOR_COORD_NUM_ERR_CONTEXT_ID_COORD_S,
				ROTATOR_COORD_NUM_ERR_CONTEXT_ID_COORD_M}
};

bool gaudi3_get_bfe_status(struct hl_aux_dev *aux_dev, u8 bfe)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	switch (bfe) {
	case NIC_ENABLE_H9_RX_DROP_ECO:
		return hdev->nic_enable_h9_rx_drop_eco;
	case NIC_ENABLE_H9_QP_DOORBELLS_ECO:
		return hdev->nic_enable_h9_qp_doorbells_eco;
	case NIC_ENABLE_H9_CC_MSG_DROPS_ECO:
		return hdev->nic_enable_h9_cc_msg_drops_eco;
	case NIC_ENABLE_H9_REMOTE_PI_UPDATE_ECO:
		return hdev->nic_enable_h9_remote_pi_update_eco;
	case NIC_ENABLE_H9_RXB_MEM_DEADLOCK_ECO:
		return hdev->nic_enable_h9_rxb_mem_deadlock_eco;
	case NIC_ENABLE_H9_SINGLE_QP_PERF_FIX_ECO:
		return hdev->nic_enable_h9_single_qp_perf_fix_eco;
	case NIC_ENABLE_H9_SAL_OVERRIDE_ECO:
		return hdev->nic_enable_h9_sal_override_eco;
	case NIC_ENABLE_H9_SACK_DEADLOCK_ECO:
		return hdev->nic_enable_h9_sack_deadlock_eco;
	case NIC_ENABLE_H9_TXE_BUFF_ALLOC_ECO:
		return hdev->nic_enable_h9_txe_buff_alloc_eco;
	case NIC_ENABLE_H9_PHY_MAC_HANG_ECO:
		return hdev->nic_enable_h9_phy_mac_hang_eco;
	}

	return false;
}

/**
 * gaudi3_get_spi_gw_addr - convert address to SPI GW address
 *
 * @die0_addr: config offset of DIE0.
 * @die: the die index
 */
static u64 gaudi3_get_spi_gw_addr(u64 die0_addr, u8 die)
{
	die0_addr += CFG_BAR_BASE;

	if (die == 0)
		return die0_addr;

	return ((die0_addr | DIE_OFFSET) & D2D_SPI_GW_MASK);
}

static int hl_pci_elbi_spi_gw_read(struct hl_device *hdev, u64 d0_reg, u32 *data, u8 die)
{
	u64 addr = gaudi3_get_spi_gw_addr(d0_reg, die);

	return hl_pci_elbi_read(hdev, addr, data);
}

static int hl_pci_elbi_spi_gw_write(struct hl_device *hdev, u64 d0_reg, u32 data, u8 die)
{
	u64 addr = gaudi3_get_spi_gw_addr(d0_reg, die);

	return hl_pci_elbi_write(hdev, addr, data);
}

static int gaudi3_init_pll(struct hl_device *hdev,
				enum gaudi3_pll_index pll_index,
				struct gaudi3_pll_params *pll_params)
{
	u32 ctrl_cfg, reg_base, vco_ctrl;
	int rc, i;
	u8 die;

	/**********************************
	 * Configure DIE0 PLLs from PCI BAR
	 **********************************/
	reg_base = gaudi3_pll_block_bases[pll_index];

	/* Configure CFG_DIV with the desired pll_out clk frequency */
	WREG32(reg_base + mmPLL_CTRL_CFG_DIV, pll_params->div_cfg);

	/* Configure VCO_CTRL_SEL: 6GHz to 12GHz */
	WREG32(reg_base + mmPLL_CTRL_VCO_CTRL, 0x3);

	usleep_range(1, 2);

	/* Enable VCO output clock */
	vco_ctrl = RREG32(reg_base + mmPLL_CTRL_VCO_CTRL);
	vco_ctrl |= FIELD_PREP(BIT(4), 1);
	WREG32(reg_base + mmPLL_CTRL_VCO_CTRL, vco_ctrl);

	/* Enable PLL */
	WREG32(reg_base + mmPLL_CTRL_CFG, 0x101);
	ctrl_cfg =  RREG32(reg_base + mmPLL_CTRL_CFG);

	if (!hdev->pldm) {
		/* Wait for PLL lock */
		rc = hl_poll_timeout(
				hdev,
				reg_base + mmPLL_CTRL_CFG,
				ctrl_cfg,
				((ctrl_cfg & 0x00010101) == 0x00010101),
				100,
				GAUDI3_PLL_TIMEOUT_USEC);

		if (rc) {
			dev_err(hdev->dev, "Failed to get PLL %d lock, die 0 ()\n", pll_index);
			return -EIO;
		}
	}

	for (i = 0 ; i < 4 ; i++) {
		WREG32(reg_base + mmPLL_CTRL_DIV_FACTOR_0 + i * 4, pll_params->div_fact[i]);
		WREG32(reg_base + mmPLL_CTRL_DIV_FACTOR_CMD_0 + i * 4, 0x1);
		WREG32(reg_base + mmPLL_CTRL_DIV_EN_0 + i * 4, 0x1);
		WREG32(reg_base + mmPLL_CTRL_DIV_SEL_0 + i * 4, pll_params->div_sel[i]);
	}

	if (hdev->asic_prop.num_of_dies != MAX_NUM_OF_DIES)
		return 0;

	/********************************************
	 * Configure DIE1 PLLs with ELBI using SPI GW
	 ********************************************/
	reg_base = gaudi3_pll_block_bases[pll_index] + DIE_OFFSET;
	die = 1;

	/* Configure CFG_DIV with the desired pll_out clk frequency */
	rc = hl_pci_elbi_spi_gw_write(hdev, reg_base + mmPLL_CTRL_CFG_DIV, pll_params->div_cfg,
					die);
	if (rc)
		return rc;

	/* Configure VCO_CTRL_SEL: 6GHz to 12GHz */
	rc = hl_pci_elbi_spi_gw_write(hdev, reg_base + mmPLL_CTRL_VCO_CTRL, 0x3, die);
	if (rc)
		return rc;

	usleep_range(1, 2);

	/* Enable VCO output clock */
	rc = hl_pci_elbi_spi_gw_read(hdev, reg_base + mmPLL_CTRL_VCO_CTRL, &vco_ctrl, die);
	if (rc)
		return rc;
	vco_ctrl |= FIELD_PREP(BIT(4), 1);
	rc = hl_pci_elbi_spi_gw_write(hdev, reg_base + mmPLL_CTRL_VCO_CTRL, vco_ctrl, die);
	if (rc)
		return rc;

	/* Enable PLL */
	rc = hl_pci_elbi_spi_gw_write(hdev, reg_base + mmPLL_CTRL_CFG, 0x101, die);
	if (rc)
		return rc;
	rc = hl_pci_elbi_spi_gw_read(hdev, reg_base + mmPLL_CTRL_CFG, &ctrl_cfg, die);
	if (rc)
		return rc;

	if (!hdev->pldm) {
		u64 poll_addr = gaudi3_get_spi_gw_addr(reg_base + mmPLL_CTRL_CFG,
									die);

		/* Wait for PLL lock */
		rc = hl_poll_timeout_elbi(
				hdev,
				poll_addr,
				ctrl_cfg,
				((ctrl_cfg & 0x00010101) == 0x00010101),
				100,
				GAUDI3_PLL_TIMEOUT_USEC);

		if (rc) {
			dev_err(hdev->dev, "Failed to get PLL %d lock, die 1 (%d)\n",
						pll_index, rc);
			return -EIO;
		}
	}

	for (i = 0 ; i < 4 ; i++) {
		rc = hl_pci_elbi_spi_gw_write(hdev,
					reg_base + mmPLL_CTRL_DIV_FACTOR_0 + i * 4,
					pll_params->div_fact[i], die);
		if (rc)
			return rc;
		rc = hl_pci_elbi_spi_gw_write(hdev,
					reg_base + mmPLL_CTRL_DIV_FACTOR_CMD_0 + i * 4,
					0x1, die);
		if (rc)
			return rc;
		rc = hl_pci_elbi_spi_gw_write(hdev,
					reg_base + mmPLL_CTRL_DIV_EN_0 + i * 4,
					0x1, die);
		if (rc)
			return rc;
		rc = hl_pci_elbi_spi_gw_write(hdev,
					reg_base + mmPLL_CTRL_DIV_SEL_0 + i * 4,
					pll_params->div_sel[i], die);
		if (rc)
			return rc;
	}

	return 0;
}

static int gaudi3_nominal_hbm_1800(struct hl_device *hdev, struct gaudi3_device *gaudi3)
{
	struct gaudi3_pll_params pll;
	int rc;

	/* PCIe PSOC ARM PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 240, 3, 2);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 0, 1, 9, 0);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_REF_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G4_PCI_PLL, &pll);
	if (rc)
		return rc;

	/* MESH (R2C, vRTR, gRTR) PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 224, 6, 0);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 0, 1, 1, 7);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G0_MESH_PLL, &pll);
	if (rc)
		return rc;

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G6_MESH_PLL, &pll);
	if (rc)
		return rc;

	/* HBM PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 216, 2, 1);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 0, 1, 3, 5);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G0_HBM_PLL, &pll);
	if (rc)
		return rc;

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G2_HBM_PLL, &pll);
	if (rc)
		return rc;

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G4_HBM_PLL, &pll);
	if (rc)
		return rc;

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G6_HBM_PLL, &pll);
	if (rc)
		return rc;

	/* NC (nRTR, NCH) PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 224, 6, 0);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 0, 1, 1, 7);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G4_NCH_PLL, &pll);
	if (rc)
		return rc;

	/* C2M PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 240, 4, 1);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 0, 1, 3, 3);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G5_C2M_PLL, &pll);

	/* D2D VFT, GLink PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 240, 2, 1);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 0, 2, 3, 15);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G0_D2D_PLL, &pll);
	if (rc)
		return rc;

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G6_D2D_PLL, &pll);
	if (rc)
		return rc;

	/* MME PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 224, 6, 0);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 0, 1, 3, 7);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G0_MME_PLL, &pll);
	if (rc)
		return rc;

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G3_MME_PLL, &pll);
	if (rc)
		return rc;

	/* TPC PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 224, 6, 0);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 0, 1, 3, 7);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G0_TPC_PLL, &pll);
	if (rc)
		return rc;

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G2_TPC_PLL, &pll);
	if (rc)
		return rc;

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G4_TPC_PLL, &pll);
	if (rc)
		return rc;

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G6_TPC_PLL, &pll);
	if (rc)
		return rc;

	/* Media PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 210, 4, 0);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 1, 2, 5, 9);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G2_MEDIA_PLL, &pll);
	if (rc)
		return rc;

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G6_MEDIA_PLL, &pll);
	if (rc)
		return rc;

	/* CS PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 240, 3, 2);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 0, 1, 2, 7);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G1_CS_PLL, &pll);
	if (rc)
		return rc;

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G5_CS_PLL, &pll);
	if (rc)
		return rc;

	/* NIC PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 224, 6, 0);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 1, 2, 7, 15);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G2_NIC_PLL, &pll);
	if (rc)
		return rc;

	/* DMA PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 224, 6, 0);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 0, 1, 3, 7);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G1_DMA_PLL, &pll);
	if (rc)
		return rc;

	/* C2C (cRTR, HFT, dRTR, MAC) PLL */
	GAUDI3_INIT_PLL_COEFFICIENT(pll, 1, 224, 6, 0);
	GAUDI3_INIT_PLL_DIVIDER_VAL(pll, 0, 1, 3, 7);
	GAUDI3_INIT_PLL_DIV_SELECTOR(pll,
					PLL_CTRL_DIV_SEL_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK,
					PLL_CTRL_DIV_SEL_DIVIDED_PLL_CLK);

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G1_C2C_PLL, &pll);
	if (rc)
		return rc;

	rc = gaudi3_init_pll(hdev, GAUDI3_D0_G5_C2C_PLL, &pll);
	if (rc)
		return rc;

	return rc;
}

int gaudi3_init_plls(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	int rc;

	if (!hdev->config_pll)
		return 0;

	if (gaudi3->hw_cap_initialized & HW_CAP_PLL)
		return 0;

	if (hdev->fw_components & FW_TYPE_PREBOOT_CPU)
		return 0;

	dev_dbg(hdev->dev, "PLL init\n");

	rc = gaudi3_nominal_hbm_1800(hdev, gaudi3);
	if (rc)
		return rc;

	gaudi3->hw_cap_initialized |= HW_CAP_PLL;

	return 0;
}

static void gaudi3_set_cache_mode_rtr_cntrl_clr_addr_dec(struct hl_device *hdev,
								int block, int inst,
								u32 offset,
								struct iterate_module_ctx *ctx)
{
	WREG32(offset + RTR_CTRL_ADEC_HBW_ADEC_REGION_EN_1_OFFSET, 0);
}

static void gaudi3_set_cache_mode_rtr_cntrl_clr_sram_mode(struct hl_device *hdev, int block,
								int inst, u32 offset,
								struct iterate_module_ctx *ctx)
{
	WREG32(offset + mmRTR_CTRL_HBW_SCRAM_SRAM_MODE, 0);
}

static void gaudi3_set_cache_mode_dtlb(struct hl_device *hdev, int block, int inst,
						u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + mmDTLB_RR_GLBL_PA_END1, 0);
}

static void gaudi3_set_cache_mode_cslice(struct hl_device *hdev, int hdcore, int inst,
						u32 offset, struct iterate_module_ctx *ctx)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	bool dcore_has_binned_hbm = false;
	/* init to illegal large value */
	u8 binned_hbm = 0xFF;
	u32 val, mask;
	int rc;

	if (prop->dram_binning_mask) {
		binned_hbm = __ffs((unsigned long)prop->dram_binning_mask);
		/*
		 * HBM is associated with HDCORE, check if *DCORE* (note: DCORE, not HDCORE)
		 * is hosting binned-out HBM
		 */
		dcore_has_binned_hbm = ((hdcore >> 1) == (binned_hbm >> 1));
	}

	val = FIELD_PREP(CACHE_MAIN_CNTRL_MAIN_SRAM_MODE_EN_M, 0) |
		FIELD_PREP(CACHE_MAIN_CNTRL_MAIN_DATA_FORWARD_EN_M, 1) |
		FIELD_PREP(CACHE_MAIN_CNTRL_MAIN_DATA_FORWARD_WR_EN_M, 1);
	mask = CACHE_MAIN_CNTRL_MAIN_SRAM_MODE_EN_M |
			CACHE_MAIN_CNTRL_MAIN_DATA_FORWARD_EN_M |
			CACHE_MAIN_CNTRL_MAIN_DATA_FORWARD_WR_EN_M;

	if (hdev->enable_h9_cache_eta_eco) {
		/* this field shall be applied only to the ETA ECO (SW-117604) */
		val |= FIELD_PREP(CACHE_MAIN_CNTRL_MAIN_DNGRD_D2H_RQ_SPCL_FSM_DIS_M, 1);
		mask |= CACHE_MAIN_CNTRL_MAIN_DNGRD_D2H_RQ_SPCL_FSM_DIS_M;
	}

	/*
	 * the field ALWAYS_ALLOW_NTQ_DNGRD (note- the field name is meaningless)
	 * shall be set to 1 in the below cases:
	 * 1. for CS in DCORE that has binned-out HBM with even index {0, 2, 4, 6}
	 * 2. for CS in DCORE that has 2 functional HBMs and is placed in odd HDCORE
	 *    (i.e. HD[1,3,5,7]_CS[0-7]).
	 */
	if ((dcore_has_binned_hbm && (!(binned_hbm & 0x1))) ||
			(!dcore_has_binned_hbm && (hdcore & 0x1))) {
		val |= FIELD_PREP(CACHE_MAIN_CNTRL_MAIN_ALWAYS_ALLOW_NTQ_DNGRD_M, 1);
		mask |= CACHE_MAIN_CNTRL_MAIN_ALWAYS_ALLOW_NTQ_DNGRD_M;
	}

	RMWREG32_SHIFTED(offset + mmCACHE_MAIN_CNTRL_MAIN, val, mask);

	WREG32(offset + CSLICE_MISC_OFFSET + mmCACHE_MISC_LTA_INIT_TRIG, 0x1);

	/* Wait till LTA not busy */
	rc = hl_poll_timeout(
			hdev,
			offset + CSLICE_MISC_OFFSET + mmCACHE_MISC_LTA_INIT_BUSY,
			val,
			(val == 0x0),
			100,
			GAUDI3_PLL_TIMEOUT_USEC);

	if (rc) {
		dev_err(hdev->dev, "LTA (%#llx) still busy\n",
				offset + CSLICE_MISC_OFFSET + mmCACHE_MISC_LTA_INIT_BUSY);
		ctx->rc = -EIO;
		return;
	}

	val = FIELD_PREP(CACHE_CRDT_TO_HBM_SWAP_PC_ADDR_WITH_9_M, 1);
	mask = CACHE_CRDT_TO_HBM_SWAP_PC_ADDR_WITH_9_M;

	/*
	 * if we have "binned-out" HBM in DIE0 we need to set
	 * BINOUT_INV_ADDR16_2HBM/BINOUT_SWAP_PC_0_WITH_1 in the CSs that
	 * are on the same DCORE (e.g. HBM3 is binned out- apply on CSs in
	 * HDCOREs 2, 3
	 */
	if ((binned_hbm < NUM_HBM_PER_DIE) && ((binned_hbm & 0x2) == (hdcore & 0x2))) {
		val |= FIELD_PREP(CACHE_CRDT_TO_HBM_BINOUT_INV_ADDR16_2HBM_M, 1) |
			FIELD_PREP(CACHE_CRDT_TO_HBM_BINOUT_SWAP_PC_0_WITH_1_M, 1);
		mask |= CACHE_CRDT_TO_HBM_BINOUT_INV_ADDR16_2HBM_M |
			CACHE_CRDT_TO_HBM_BINOUT_SWAP_PC_0_WITH_1_M;
	}

	RMWREG32_SHIFTED(offset + CSLICE_CRDT_OFFSET + mmCACHE_CRDT_TO_HBM, val, mask);
}

int gaudi3_set_cache_mode(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct iterate_module_ctx ctx = {};

	if ((gaudi3->hw_cap_initialized & HW_CAP_SET_CACHE_MODE_MASK) == HW_CAP_SET_CACHE_MODE_MASK)
		return 0;

	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return 0;

	ctx.fn = gaudi3_set_cache_mode_cslice;
	gaudi3_iterate_cache_slices(hdev, &ctx);
	if (ctx.rc)
		return ctx.rc;

	ctx.fn = gaudi3_set_cache_mode_rtr_cntrl_clr_addr_dec;
	gaudi3_iterate_rtr_ctrls(hdev, &ctx);

	ctx.fn = gaudi3_set_cache_mode_dtlb;
	gaudi3_iterate_dtlbs(hdev, &ctx);

	ctx.fn = gaudi3_set_cache_mode_rtr_cntrl_clr_sram_mode;
	gaudi3_iterate_rtr_ctrls(hdev, &ctx);

	/* Perform read from the device to flush all configurations */
	RREG32(mmHD0_RIF_RTR_CTRL_ADEC_HBW_BASE + mmRTR_CTRL_ADEC_HBW_ADEC_REGION_EN_1);

	gaudi3->hw_cap_initialized |= HW_CAP_SET_CACHE_MODE_MASK;

	return 0;
}

static void gaudi3_set_cslice_sei_mask(struct hl_device *hdev, int block, int inst, u32 offset,
					struct iterate_module_ctx *ctx)
{
	u32 mask = CACHE_MAIN_SEI_MASK_REG_FAR_HOST_REDUC_NUM_ERR_M |
			CACHE_MAIN_SEI_MASK_REG_CLOSE_HOST_REDUC_NUM_ERR_M |
			CACHE_MAIN_SEI_MASK_REG_AAB_REDUC_NUM_ERR_M |
			CACHE_MAIN_SEI_MASK_REG_DN_CONV_NUM_ERR_M;

	WREG32(offset + mmCACHE_MAIN_SEI_MASK_REG, mask);
}

static void gaudi3_enable_sram_mode(struct hl_device *hdev, int block, int inst, u32 offset,
					struct iterate_module_ctx *ctx)
{
	u32 val = RREG32(offset + mmCACHE_MAIN_CNTRL_MAIN);

	/* enable SRAM mode */
	val |= CACHE_MAIN_CNTRL_MAIN_SRAM_MODE_EN_M;

	/* disable data FW (bug H9-5279) */
	val &= (~CACHE_MAIN_CNTRL_MAIN_DATA_FORWARD_EN_M);

	WREG32(offset + mmCACHE_MAIN_CNTRL_MAIN, val);
}

static void gaudi3_init_cslice(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct iterate_module_ctx ctx = {};

	if (gaudi3->hw_cap_initialized & HW_CAP_CSLICE)
		return;

	ctx.fn  = hdev->cache_enable ? gaudi3_set_cslice_sei_mask : gaudi3_enable_sram_mode;
	gaudi3_iterate_cache_slices(hdev, &ctx);

	gaudi3->hw_cap_initialized |= HW_CAP_CSLICE;
}

static void gaudi3_reset_config(struct hl_device *hdev)
{
	u64 offset;
	u32 die;

	for (die = 0; die < hdev->asic_prop.num_of_dies; die++) {
		offset = die * DIE_OFFSET;

		/* Reset PMMU during hard-reset */
		WREG32(mmD0_PSOC_RESET_CONF_BASE + offset +
				mmPSOC_RESET_CONF_PMMU_SW_RST_CFG,
				PSOC_RESET_CONF_PMMU_SW_RST_CFG_EN_M);

		/* PPW termination of all transactions after reset should be cleared by F/W via a
		 * non-PCIe access to PCIE_AUX.BOOT_TERM_CLR.
		 * This cleanup cannot be done when working w/o F/W, so this feature is disabled.
		 * Need to be skipped for DIE1
		 */
		if (die == 0)
			WREG32(mmD0_PCIE_AUX_BASE + mmPCIE_AUX_BOOT_TERM_EN, 0x0);
	}
}

static void gaudi3_set_pcie_security_level(struct hl_device *hdev)
{
	/* Set the access through PCI bars as secured */
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_PCIE_PROT_OVR,
			(PCIE_WRAP_PCIE_PROT_OVR_RD_EN_M | PCIE_WRAP_PCIE_PROT_OVR_WR_EN_M));

	/* Perform read to flush the waiting writes to ensure configuration was set in the device */
	RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_PCIE_PROT_OVR);
}

static int gaudi3_init_axi_drain(struct hl_device *hdev)
{
	int rc;

	/* we don't access DIE1 PCIE_WRAP */
	if (hdev->axi_drain == AXI_DRAIN_ENABLED) {
		rc = hl_pci_elbi_write(hdev,
				CFG_BAR_BASE + mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_DRAIN_TIMEOUT,
				0x1000);
		if (rc)
			return rc;

		rc = hl_pci_elbi_write(hdev,
				CFG_BAR_BASE + mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_DRAIN_CFG,
				FIELD_PREP(PCIE_WRAP_HBW_DRAIN_CFG_EN_M, 0x1));
		if (rc)
			return rc;

		rc = hl_pci_elbi_write(hdev,
				CFG_BAR_BASE + mmD0_PCIE_WRAP_BASE +
					mmPCIE_WRAP_HBW_RSP_ERR_DRAIN_STAMP,
				FIELD_PREP(PCIE_WRAP_HBW_RSP_ERR_DRAIN_STAMP_EN_M, 0x1));
		if (rc)
			return rc;

		rc = hl_pci_elbi_write(hdev,
				CFG_BAR_BASE + mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_DRAIN_TIMEOUT,
				0xF000);
		if (rc)
			return rc;

		rc = hl_pci_elbi_write(hdev,
				CFG_BAR_BASE + mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_DRAIN_CFG,
				FIELD_PREP(PCIE_WRAP_LBW_DRAIN_CFG_EN_M, 0x1));
		if (rc)
			return rc;

		rc = hl_pci_elbi_write(hdev,
				CFG_BAR_BASE + mmD0_PCIE_WRAP_BASE +
					mmPCIE_WRAP_LBW_RSP_ERR_DRAIN_STAMP,
				FIELD_PREP(PCIE_WRAP_LBW_RSP_ERR_DRAIN_STAMP_EN_M, 0x1));
		if (rc)
			return rc;

		/* TDOD: add PSOC axi drain configuration */

	} else if (hdev->axi_drain == AXI_DRAIN_DISABLED) {
		rc = hl_pci_elbi_write(hdev,
				CFG_BAR_BASE + mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_DRAIN_CFG, 0);
		if (rc)
			return rc;

		rc = hl_pci_elbi_write(hdev,
				CFG_BAR_BASE + mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_DRAIN_CFG, 0);
		if (rc)
			return rc;

		/* TDOD: add PSOC axi drain configuration */
	}

	return 0;
}

static u64 gaudi3_get_drain_address(struct hl_device *hdev, u64 addr_base)
{
	u64 addr_high;
	u32 addr_low;

	addr_low = RREG32(addr_base);
	addr_high = RREG32(addr_base + 0x4);

	return ((addr_high << 32) + addr_low);
}

/*
 * AXI drain address registers layout (with offsets form drain_addr_base):
 * [0]: DRAIN_WR_ADDR_0
 * [4]: DRAIN_WR_ADDR_1
 * [8]: DRAIN_RD_ADDR_0
 * [C]: DRAIN_RD_ADDR_1
 */
static void gaudi3_print_axi_drain_address(struct hl_device *hdev, u64 drain_addr_base,
						const char *type)
{
	u64 wr_addr, rd_addr;

	wr_addr = gaudi3_get_drain_address(hdev, drain_addr_base);
	rd_addr = gaudi3_get_drain_address(hdev, drain_addr_base + 0x8);

	if ((wr_addr == 0) && (rd_addr == 0))
		return;

	dev_err(hdev->dev, "AXI drain %s event: read address %#llx, write address %#llx\n",
						type, rd_addr, wr_addr);
}

u32 gaudi3_handle_axi_drain(struct hl_device *hdev, bool *pci_link_error)
{
	u32 err_cnt = 0, drain_indication;

	drain_indication = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_AXI_DRAIN_IND);
	if (drain_indication == 0xFFFFFFFF) {
		dev_err(hdev->dev, "PCI link error\n");
		*pci_link_error = true;
		return 1;
	}

	drain_indication &= (PCIE_WRAP_AXI_DRAIN_IND_LBW_AXI_DRAIN_IND_M |
				PCIE_WRAP_AXI_DRAIN_IND_HBW_AXI_DRAIN_IND_M);
	if (!drain_indication)
		return 0;

	if (drain_indication & PCIE_WRAP_AXI_DRAIN_IND_LBW_AXI_DRAIN_IND_M) {
		gaudi3_print_axi_drain_address(hdev,
					mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_DRAIN_WR_ADDR_0,
					"LBW TIMEOUT");
		gaudi3_print_axi_drain_address(hdev,
					mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_WR_ERR_ADDR_0,
					"LBW ERR");
		err_cnt++;
	}
	if (drain_indication & PCIE_WRAP_AXI_DRAIN_IND_HBW_AXI_DRAIN_IND_M) {
		gaudi3_print_axi_drain_address(hdev,
					mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_DRAIN_WR_ADDR_0,
					"HBW TIMEOUT");
		gaudi3_print_axi_drain_address(hdev,
					mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_WR_ERR_ADDR_0,
					"HBW ERR");
		err_cnt++;
	}

	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_RSP_ERR_DRAIN_STAMP, 0x1);
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_RSP_ERR_DRAIN_STAMP, 0x1);

	/* Ack the event */
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_AXI_DRAIN_IND, 0);

	return err_cnt;
}

static void gaudi3_init_dbi_gateway(struct hl_device *hdev)
{
	/* we don't access DIE1 PCIE_WRAP */

	/* PCIE_DBI_SNPS (M0): access is allowed only to privileged agents (PRIV = 0x2).
	 * However, when working without F/W, the driver needs to configure the iATU, so the
	 * access should be allowed also to secure agents (SECURE = 0x1).
	 */
	WREG32(mmD0_PCIE_WRAP_DBI_GW_M0_BASE + mmPCIE_WRAP_DBI_GW_M0_PROT_CTRL,
			FIELD_PREP(PCIE_WRAP_DBI_GW_M0_PROT_CTRL_PRIO_WR_M, 0x1) |
			FIELD_PREP(PCIE_WRAP_DBI_GW_M0_PROT_CTRL_PRIO_RD_M, 0x1) |
			FIELD_PREP(PCIE_WRAP_DBI_GW_M0_PROT_CTRL_PRIO_EN_M, 0x1));

	/* DBI gateway should configure CS2=1 in PCIE_AUX.DBI when M0 is accessed */
	WREG32(mmD0_PCIE_WRAP_DBI_ACCESS_BASE + mmPCIE_WRAP_DBI_ACCESS_DBI_GW_M0_ADDR_OVERRIDE_DATA,
			PCIE_AUX_DBI_CS2_1);

	/* Enable the M0 address window */
	WREG32(mmD0_PCIE_WRAP_DBI_GW_M0_BASE + mmPCIE_WRAP_DBI_GW_M0_ADDR_CTRL,
			FIELD_PREP(PCIE_WRAP_DBI_GW_M0_ADDR_CTRL_WIN_EN_M, 0x1));

	/* PCIE_DBI_SIG (M1): access is allowed to all agents (USER = 0x0) */
	WREG32(mmD0_PCIE_WRAP_DBI_GW_M1_BASE + mmPCIE_WRAP_DBI_GW_M1_PROT_CTRL,
			FIELD_PREP(PCIE_WRAP_DBI_GW_M1_PROT_CTRL_PRIO_WR_M, 0x0) |
			FIELD_PREP(PCIE_WRAP_DBI_GW_M1_PROT_CTRL_PRIO_RD_M, 0x0) |
			FIELD_PREP(PCIE_WRAP_DBI_GW_M1_PROT_CTRL_PRIO_EN_M, 0x1));

	/* DBI gateway should configure CS2=0 in PCIE_AUX.DBI when M1 is accessed */
	WREG32(mmD0_PCIE_WRAP_DBI_ACCESS_BASE + mmPCIE_WRAP_DBI_ACCESS_DBI_GW_M1_ADDR_OVERRIDE_DATA,
			PCIE_AUX_DBI_CS2_0);

	/* Enable the M1 address window */
	WREG32(mmD0_PCIE_WRAP_DBI_GW_M1_BASE + mmPCIE_WRAP_DBI_GW_M1_ADDR_CTRL,
			FIELD_PREP(PCIE_WRAP_DBI_GW_M1_ADDR_CTRL_WIN_EN_M, 0x1));

	/* Disable the DBI gateway bypass */
	WREG32(mmD0_PCIE_WRAP_DBI_ACCESS_BASE + mmPCIE_WRAP_DBI_ACCESS_DBI_GW_BYPASS,
			FIELD_PREP(PCIE_WRAP_DBI_ACCESS_DBI_GW_BYPASS_VAL_M, 0x0));
}

static int gaudi3_die_phy_ctrl_write_broadcast(struct hl_device *hdev, u8 die, u32 reg_offset,
						u32 val)
{
	u8 phy_ctrl;
	u64 addr;
	int rc;

	for (phy_ctrl = 0; phy_ctrl < NUM_OF_DPHY_CTRL_PER_DIE; phy_ctrl++) {
		addr = mmD0_DPHY0_CTRL_BASE + reg_offset +
				(phy_ctrl * DPHY_CTRL_DIE_INSTANCE_OFFSET);
		rc = hl_pci_elbi_spi_gw_write(hdev, addr, val, die);
		if (rc)
			return rc;
	}

	return 0;
}

static int gaudi3_d2d_phy_ctrl_write_broadcast(struct hl_device *hdev, u32 reg_offset, u32 val)
{
	u8 die;
	int rc;

	for (die = 0; die < MAX_NUM_OF_DIES; die++) {
		rc = gaudi3_die_phy_ctrl_write_broadcast(hdev, die, reg_offset, val);
		if (rc)
			return rc;
	}

	return 0;
}

static void gaudi3_set_d2d_dphy_pll_lock_props(struct hl_device *hdev)
{
	if (!hdev->pldm)
		return;

	gaudi3_d2d_phy_ctrl_write_broadcast(hdev, mmDPHY_CTRL_INIT_PLL_CTRL0, 0x4);
	gaudi3_d2d_phy_ctrl_write_broadcast(hdev, mmDPHY_CTRL_INIT_PLL_CTRL1, 0x4);
	gaudi3_d2d_phy_ctrl_write_broadcast(hdev, mmDPHY_CTRL_INIT_PLL_CTRL2, 0x0);
	gaudi3_d2d_phy_ctrl_write_broadcast(hdev, mmDPHY_CTRL_INIT_PLL_CTRL3, 0x4);
}

static int gaudi3_d2d_psoc_dphy_fsm_init(struct hl_device *hdev)
{
	u64 timeout;
	int rc;
	u8 die;

	/* Start PSOC DPHY FSM, on both dies */
	for (die = 0; die < MAX_NUM_OF_DIES; die++) {
		rc = hl_pci_elbi_spi_gw_write(hdev, mmD0_PSOC_GLOBAL_CONF_BASE +
				mmGLOBAL_CONF_DPHY_FSM_CTRL, 0x1, die);
		if (rc)
			return rc;
	}

	/*
	 * Poll for done indication, on both dies.
	 * not using the poll reg array as condition is more complicated
	 * than plain value comparison.
	 */
	for (die = 0; die < MAX_NUM_OF_DIES; die++) {
		u64 poll_addr;
		u32 reg_val;

		poll_addr = gaudi3_get_spi_gw_addr(mmD0_PSOC_GLOBAL_CONF_BASE +
				mmGLOBAL_CONF_DPHY_FSM_STS, die);
		if (hdev->pldm)
			timeout = GAUDI3_PLDM_D2D_DPHY_CTRL_POLL_TIMEOUT_USEC;
		else
			timeout = GAUDI3_D2D_DPHY_CTRL_POLL_TIMEOUT_USEC;

		rc = hl_poll_timeout_elbi(
				hdev,
				poll_addr,
				reg_val,
				(reg_val & 0x1),
				GAUDI3_D2D_DPHY_CTRL_POLL_INTERVAL_USEC,
				timeout);

		if (rc) {
			dev_err(hdev->dev, "Failed to get D2D FSM indication DONE on DIE%u (%d)\n",
						die, rc);
			return -EIO;
		}
	}

	return 0;
}

/*
 * performing sanity test by initializing registers in DIE1 (that will be overwritten
 * by later code) to some magic value and than verify we can read that value
 */
static bool gaudi3_d2d_sanity_test(struct hl_device *hdev)
{
	u32 hdcore, rtr, orig_id, cmp_id;
	u64 addr;

	for (hdcore = NUM_OF_HDCORES_PER_DIE;
		hdcore < (hdev->asic_prop.num_of_dies * NUM_OF_HDCORES_PER_DIE); hdcore++) {
		for (rtr = 0 ; rtr < NUM_OF_RRTR_PER_HDCORE ; rtr++) {
			orig_id = 9 - rtr;
			addr = mmHD0_RRTR0_DTLB_BASE + (hdcore * HDCORE_OFFSET) +
						(rtr * RRTR_OFFSET);
			WREG32(addr, orig_id);
			cmp_id = RREG32(addr);
			if (cmp_id != orig_id) {
				dev_err(hdev->dev,
					"D2D sanity failed: %x, rtr: %x, orig: %x, cmp: %x\n",
						hdcore, rtr, orig_id, cmp_id);
				return false;
			}
		}
	}

	return true;
}

static int gaudi3_d2d_init(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	int rc = 0;

	if (hdev->asic_prop.num_of_dies != MAX_NUM_OF_DIES)
		return 0;

	/* simulator does not support the D2D PHY init code */
	if (!hdev->pdev)
		return 0;

	if (hdev->fw_components & FW_TYPE_PREBOOT_CPU)
		return 0;

	if (gaudi3->hw_cap_initialized & HW_CAP_D2D)
		return 0;

	dev_dbg(hdev->dev, "D2D init\n");

	/*
	 * remove protection to allow master d2d_spi to configure
	 * all things necessary (on DIE 0 only)
	 */
	rc = hl_pci_elbi_spi_gw_write(hdev, mmD0_D2D_SPI_BASE + mmD2D_SPI_MST_ACCESS_PROT, 0x0, 0);

	gaudi3_set_d2d_dphy_pll_lock_props(hdev);

	rc = gaudi3_d2d_psoc_dphy_fsm_init(hdev);
	if (rc)
		return rc;

	if (!gaudi3_d2d_sanity_test(hdev))
		return -EFAULT;

	gaudi3->hw_cap_initialized |= HW_CAP_D2D;

	return 0;
}

int gaudi3_pre_hw_init(struct hl_device *hdev)
{
	int rc;

	if (hdev->fw_components & FW_TYPE_PREBOOT_CPU)
		return 0;

	gaudi3_set_pcie_security_level(hdev);
	rc = gaudi3_init_axi_drain(hdev);
	if (rc)
		return rc;

	gaudi3_init_dbi_gateway(hdev);
	return gaudi3_d2d_init(hdev);
}

void gaudi3_disable_nic_interrupts_cpu_if(struct hl_device *hdev)
{
	int die;

	/* Disable the NICs global interrupts regardless of NICs' existence */
	for (die = 0 ; die < NIC_NUM_OF_DIES ; die++) {
		WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + die * DIE_OFFSET,
				NIC_SEI_INTR_MASK);
		WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + die * DIE_OFFSET,
				NIC_SPI_INTR_MASK_0);
		WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + die * DIE_OFFSET,
				NIC_SPI_INTR_MASK_1);
	}

	/* flush */
	RREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE + mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0);
}

static void gaudi3_set_edma_isolation(struct hl_device *hdev, bool isolate)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 edma_iso;
	int die;

	if (isolate) {
		for (die = 0 ; die < prop->num_of_dies ; die++)
			WREG32(mmD0_PSOC_BOOT_CONF_BASE + die * DIE_OFFSET +
					mmPSOC_BOOT_CONF_EDMA_ISO,
					PSOC_BOOT_CONF_EDMA_ISO_ISO_EN_M);
		return;
	}

	/* DIE0 EDMA_ISO:
	 * Bit[0] - HD1_EDMA{0,1}
	 * Bit[1] - HD3_EDMA{0,1}
	 */
	edma_iso = 0;
	if (!(prop->edma_enabled_mask & 0x3))
		edma_iso |= BIT(0);
	if (!(prop->edma_enabled_mask & 0xC))
		edma_iso |= BIT(1);
	WREG32(mmD0_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_EDMA_ISO, edma_iso);

	if (prop->num_of_dies == 1)
		return;

	/* DIE1 EDMA_ISO:
	 * Bit[0] - HD6_EDMA{0,1}
	 * Bit[1] - HD4_EDMA{0,1}
	 */
	edma_iso = 0;
	if (!(prop->edma_enabled_mask & 0xC0))
		edma_iso |= BIT(0);
	if (!(prop->edma_enabled_mask & 0x30))
		edma_iso |= BIT(1);
	WREG32(mmD1_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_EDMA_ISO, edma_iso);
}

static void gaudi3_set_tpc_isolation(struct hl_device *hdev, bool isolate)
{
	u32 die0_tpc_disabled_mask, die1_tpc_disabled_mask, tpc_iso_l;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 tpc_disabled_mask;
	int die;

	if (isolate) {
		for (die = 0 ; die < prop->num_of_dies ; die++) {
			WREG32(mmD0_PSOC_BOOT_CONF_BASE + die * DIE_OFFSET +
					mmPSOC_BOOT_CONF_TPC_ISO_L,
					PSOC_BOOT_CONF_TPC_ISO_L_ISO_EN_M);
			WREG32(mmD0_PSOC_BOOT_CONF_BASE + die * DIE_OFFSET +
					mmPSOC_BOOT_CONF_TPC_ISO_H,
					PSOC_BOOT_CONF_TPC_ISO_H_ISO_EN_M);
		}

		return;
	}

	/* DIE0 TPC_ISO_L:
	 *  Bit[0]  - HD0_TPC0 ... Bit[7]  - HD0_TPC7
	 *  Bit[8]  - HD1_TPC0 ... Bit[15] - HD1_TPC7
	 *  Bit[16] - HD2_TPC0 ... Bit[23] - HD2_TPC7
	 *  Bit[24] - HD3_TPC0 ... Bit[31] - HD3_TPC7
	 */
	tpc_disabled_mask = ~prop->tpc_enabled_mask;
	die0_tpc_disabled_mask = lower_32_bits(tpc_disabled_mask);

	/* for DIE0 the isolation value is the same as the DIE disabled mask */
	WREG32(mmD0_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_TPC_ISO_L, die0_tpc_disabled_mask);

	/* DIE0 TPC_ISO_H:
	 *  Bit[0] - HD0_TPC8
	 *  Bit[1] - HD2_TPC8
	 */
	WREG32(mmD0_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_TPC_ISO_H, 0x3);

	if (prop->num_of_dies == 1)
		return;

	/* DIE1 TPC_ISO_L:
	 *  Bit[0]  - HD7_TPC7 ... Bit[7]  - HD7_TPC0
	 *  Bit[8]  - HD6_TPC7 ... Bit[15] - HD6_TPC0
	 *  Bit[16] - HD5_TPC7 ... Bit[23] - HD5_TPC0
	 *  Bit[24] - HD4_TPC7 ... Bit[31] - HD4_TPC0
	 */
	die1_tpc_disabled_mask = upper_32_bits(tpc_disabled_mask);

	tpc_iso_l =
		FIELD_PREP(0xFF, bitrev8(FIELD_GET(0xFF000000, die1_tpc_disabled_mask))) |
		FIELD_PREP(0xFF00, bitrev8(FIELD_GET(0xFF0000, die1_tpc_disabled_mask))) |
		FIELD_PREP(0xFF0000, bitrev8(FIELD_GET(0xFF00, die1_tpc_disabled_mask))) |
		FIELD_PREP(0xFF000000, bitrev8(FIELD_GET(0xFF, die1_tpc_disabled_mask)));
	WREG32(mmD1_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_TPC_ISO_L, tpc_iso_l);

	/* DIE1 TPC_ISO_H:
	 *  Bit[0] - HD7_TPC8
	 *  Bit[1] - HD5_TPC8
	 */
	WREG32(mmD1_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_TPC_ISO_H, 0x3);
}

static void gaudi3_set_mme_isolation(struct hl_device *hdev, bool isolate)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 mme_iso;
	int die;

	if (isolate) {
		for (die = 0 ; die < prop->num_of_dies ; die++)
			WREG32(mmD0_PSOC_BOOT_CONF_BASE + die * DIE_OFFSET +
					mmPSOC_BOOT_CONF_MME_ISO,
					PSOC_BOOT_CONF_MME_ISO_ISO_EN_M);
		return;
	}

	/* DIE0 MME_ISO:
	 * Bit[0] - HD0_MME ... Bit[3] - HD3_MME
	 */
	mme_iso = ~hdev->mme_mask & 0xF;
	WREG32(mmD0_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_MME_ISO, mme_iso);

	if (prop->num_of_dies == 1)
		return;

	/* DIE1 MME_ISO:
	 * Bit[0] - HD7_MME ... Bit[3] - HD4_MME
	 */
	mme_iso = bitrev8(~hdev->mme_mask & 0xF0);
	WREG32(mmD1_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_MME_ISO, mme_iso);
}

static void gaudi3_set_rotator_isolation(struct hl_device *hdev, bool isolate)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 rot_disabled_mask, rot_iso;
	int die;

	if (isolate) {
		for (die = 0 ; die < prop->num_of_dies ; die++)
			WREG32(mmD0_PSOC_BOOT_CONF_BASE + die * DIE_OFFSET +
					mmPSOC_BOOT_CONF_ROT_ISO,
					PSOC_BOOT_CONF_ROT_ISO_ISO_EN_M);
		return;
	}

	rot_disabled_mask = ~prop->rotator_enabled_mask & 0xFF;
	/* DIE0 ROT_ISO:
	 * Bit[0] - HD1_ROT0
	 * Bit[1] - HD1_ROT1
	 * Bit[2] - HD3_ROT0
	 * Bit[3] - HD3_ROT1
	 */
	rot_iso = rot_disabled_mask & 0xF;
	WREG32(mmD0_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_ROT_ISO, rot_iso);

	if (prop->num_of_dies == 1)
		return;

	/* get only rotator bits relevant for DIE1 */
	rot_disabled_mask = FIELD_GET(0xF0, rot_disabled_mask);

	/* DIE1 ROT_ISO:
	 * Bit[0] - HD6_ROT0
	 * Bit[1] - HD6_ROT1
	 * Bit[2] - HD4_ROT0
	 * Bit[3] - HD4_ROT1
	 */
	rot_iso = FIELD_PREP(0x3, FIELD_GET(0xC, rot_disabled_mask)) |
			FIELD_PREP(0xC, FIELD_GET(0x3, rot_disabled_mask));
	WREG32(mmD1_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_ROT_ISO, rot_iso);
}

static void gaudi3_set_decoder_isolation(struct hl_device *hdev, bool isolate)
{
	u8 d0_decoder_id_to_iso_bit[] = {0, 2, 4, 6, 1, 3, 5, 7};
	u8 d1_decoder_id_to_iso_bit[] = {5, 7, 1, 3, 4, 6, 0, 2};
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 num_of_decoder_per_die, vdec_iso;
	int i, die;

	if (isolate) {
		for (die = 0 ; die < prop->num_of_dies ; die++)
			WREG32(mmD0_PSOC_BOOT_CONF_BASE + die * DIE_OFFSET +
					mmPSOC_BOOT_CONF_VDEC_ISO,
					PSOC_BOOT_CONF_VDEC_ISO_ISO_EN_M);
		return;
	};

	num_of_decoder_per_die = NUM_OF_HDCORES_PER_DIE * NUM_OF_DECODER_PER_HDCORE;

	/* DIE0 VDEC_ISO:
	 * Bit[0] - HD0_VDEC0
	 * Bit[1] - HD2_VDEC0
	 * Bit[2] - HD0_VDEC1
	 * Bit[3] - HD2_VDEC1
	 * Bit[4] - HD1_VDEC0
	 * Bit[5] - HD3_VDEC0
	 * Bit[6] - HD1_VDEC1
	 * Bit[7] - HD3_VDEC1
	 */
	vdec_iso = 0;
	for (i = 0 ; i < num_of_decoder_per_die ; i++) {
		if (!(prop->decoder_enabled_mask & BIT(i)))
			vdec_iso |= BIT(d0_decoder_id_to_iso_bit[i]);
	}
	WREG32(mmD0_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_VDEC_ISO, vdec_iso);

	if (prop->num_of_dies == 1)
		return;

	/* DIE1 VDEC_ISO:
	 * Bit[0] - HD7_VDEC0
	 * Bit[1] - HD5_VDEC0
	 * Bit[2] - HD7_VDEC1
	 * Bit[3] - HD5_VDEC1
	 * Bit[4] - HD6_VDEC0
	 * Bit[5] - HD4_VDEC0
	 * Bit[6] - HD6_VDEC1
	 * Bit[7] - HD4_VDEC1
	 */
	vdec_iso = 0;
	for (i = 0 ; i < num_of_decoder_per_die ; i++) {
		if (!(prop->decoder_enabled_mask & BIT(num_of_decoder_per_die + i)))
			vdec_iso |= BIT(d1_decoder_id_to_iso_bit[i]);
	}
	WREG32(mmD1_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_VDEC_ISO, vdec_iso);
}

static void gaudi3_set_nic_isolation(struct hl_device *hdev, bool isolate)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 nic_iso;
	int i, die;

	if (isolate) {
		for (die = 0 ; die < prop->num_of_dies ; die++)
			WREG32(mmD0_PSOC_BOOT_CONF_BASE + die * DIE_OFFSET +
					mmPSOC_BOOT_CONF_NIC_ISO,
					PSOC_BOOT_CONF_NIC_ISO_ISO_EN_M);
		return;
	};

	/* DIE0 NIC_ISO:
	 * Bit[0] - D0_NIC0 ... Bit[5] - D0_NIC5
	 */
	nic_iso = 0;
	for (i = 0 ; i < NIC_NUM_MACROS_PER_DIE ; i++) {
		if (!(hdev->cn.ports_mask & gaudi3_cn_get_macro_ports_mask(hdev, i)))
			nic_iso |= BIT(i);
	}
	WREG32(mmD0_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_NIC_ISO, nic_iso);

	if (prop->num_of_dies == 1)
		return;

	/* DIE1 NIC_ISO:
	 * Bit[0] - D1_NIC0 ... Bit[5] - D1_NIC5
	 */
	nic_iso = 0;
	for (i = 0 ; i < NIC_NUM_MACROS_PER_DIE ; i++) {
		if (!(hdev->cn.ports_mask & gaudi3_cn_get_macro_ports_mask(hdev,
								NIC_NUM_MACROS_PER_DIE + i)))
			nic_iso |= BIT(i);
	}
	WREG32(mmD1_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_NIC_ISO, nic_iso);
}

static void gaudi3_set_hbm_isolation(struct hl_device *hdev, bool isolate)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 hbm_iso;
	int die;

	if (isolate || !hdev->dram_enable) {
		for (die = 0; die < prop->num_of_dies; die++)
			WREG32(mmD0_PSOC_BOOT_CONF_BASE + die * DIE_OFFSET +
					mmPSOC_BOOT_CONF_HBM_ISO,
					PSOC_BOOT_CONF_HBM_ISO_ISO_EN_M);
		return;
	}

	/* DIE0 HBM_ISO:
	 * Bit[0] - HBM0 ... Bit[3] - HBM3
	 */
	hbm_iso = ~lower_32_bits(prop->dram_enabled_mask) & 0xF;
	WREG32(mmD0_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_HBM_ISO, hbm_iso);

	if (prop->num_of_dies == 1)
		return;

	/* DIE1 HBM_ISO:
	 * Bit[0] - HBM7 ... Bit[3] - HBM4
	 */
	hbm_iso = bitrev8(~lower_32_bits(prop->dram_enabled_mask) & 0xF0);
	WREG32(mmD1_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_HBM_ISO, hbm_iso);
}

/*
 * gaudi3_set_isolation() - configure isolation for engines and HBM.
 * @hdev: pointer to habanalabs device structure.
 * @isolate_engines: isolate or deisolate non-NIC engines.
 * @isolate_nic_and_hbm: isolate or deioslate NIC and HBM.
 *
 * Configure isolation for engines and HBM according to the provided 'isolate' flags.
 * A 'true' value in the flags means isolate all, while a 'false' value means remove isolation based
 * on the 'enabled' properties. I.e. if a block is not enabled, it will be isolated even if a
 * 'false' isolate flag was provided.
 * NIC/HBM and non-NIC engines have separate flags, to allow the isolation of only non-NIC engines
 * before compute reset.
 */
void gaudi3_set_isolation(struct hl_device *hdev, bool isolate_engines, bool isolate_nic_and_hbm)
{
	/* Isolation is configured by preboot */
	if (hdev->fw_components & FW_TYPE_PREBOOT_CPU)
		return;

	/* Perform read from the device to flush all previous accesses */
	RREG32(mmD0_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_HBM_ISO);

	gaudi3_set_edma_isolation(hdev, isolate_engines);
	gaudi3_set_tpc_isolation(hdev, isolate_engines);
	gaudi3_set_mme_isolation(hdev, isolate_engines);
	gaudi3_set_rotator_isolation(hdev, isolate_engines);
	gaudi3_set_decoder_isolation(hdev, isolate_engines);

	gaudi3_set_nic_isolation(hdev, isolate_nic_and_hbm);
	gaudi3_set_hbm_isolation(hdev, isolate_nic_and_hbm);

	/* Perform read from the device to flush the isolation configuration */
	RREG32(mmD0_PSOC_BOOT_CONF_BASE + mmPSOC_BOOT_CONF_HBM_ISO);
}

void gaudi3_init_arc(struct hl_device *hdev, u32 cpu_id)
{
	u32 reg_base, reg_val;

	/* skip arc init if already done by FW */
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	hdev->asic_funcs->set_priv_assertions(hdev, false);

	reg_base = gaudi3_arc_blocks_bases[cpu_id];

	/* Configure reset vector to fetch FW from Region 4 (ARC region HBM0) */
	reg_val = FIELD_PREP(QMAN_ARC_AUX_RST_VEC_ADDR_REGION_M, ARC_REGION4_HBM0_FW);
	WREG32(reg_base + mmQMAN_ARC_AUX_RST_VEC_ADDR, reg_val >> 10);

	/* Bring ARC out of RESET */
	reg_val = FIELD_PREP(QMAN_ARC_AUX_ARC_RST_PRESETDBGN_M, 1);
	WREG32(reg_base + mmQMAN_ARC_AUX_ARC_RST, reg_val);

	/* Disable DCCM termination */
	WREG32(reg_base + mmQMAN_ARC_AUX_CFG_DCCM_TERMINATE_EN, 0);

	/* Enhance ARCs performance */
	reg_val = FIELD_PREP(QMAN_ARC_AUX_ARC_CBU_AWCACHE_OVR_AXI_WRITE_M, 0x1);
	reg_val |= FIELD_PREP(QMAN_ARC_AUX_ARC_CBU_AWCACHE_OVR_AXI_WRITE_EN_M, 0x3);
	WREG32(reg_base + mmQMAN_ARC_AUX_ARC_CBU_AWCACHE_OVR, reg_val);
	WREG32(reg_base + mmQMAN_ARC_AUX_ARC_LBU_AWCACHE_OVR, reg_val);
	WREG32(reg_base + mmQMAN_ARC_AUX_CBU_EARLY_BRESP_EN, 0x0);

	/* Initialize AxCACHE bits for scheduler ARCs */
	if (cpu_id < CPU_ID_SCHED_MAX) {
		reg_val = FIELD_PREP(QMAN_ARC_AUX_CBU_AXCACHE_OVR_CBU_READ_M,
					AXCACHE_DO_NOT_SKIP_CACHE) |
				FIELD_PREP(QMAN_ARC_AUX_CBU_AXCACHE_OVR_CBU_WRITE_M,
						AXCACHE_DO_NOT_SKIP_CACHE) |
				FIELD_PREP(QMAN_ARC_AUX_CBU_AXCACHE_OVR_CBU_RD_EN_M, 0xF) |
				FIELD_PREP(QMAN_ARC_AUX_CBU_AXCACHE_OVR_CBU_WR_EN_M, 0xF);
		WREG32(reg_base + mmQMAN_ARC_AUX_CBU_AXCACHE_OVR, reg_val);

		reg_val = FIELD_PREP(ARC_AF_ENG_SB_ARCACHE_ARCACHE_M,
					AXCACHE_DO_NOT_SKIP_CACHE);
		reg_base = gaudi3_sched_arc_af_blocks_bases[cpu_id];
		WREG32(reg_base + mmARC_AF_ENG_SB_ARCACHE, reg_val);
	}

	hdev->asic_funcs->set_priv_assertions(hdev, true);
}

void gaudi3_reset_arc(struct hl_device *hdev, u32 cpu_id)
{
	u32 reg_base, reg_val;

	/* skip arc init if already done by FW */
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	reg_base = gaudi3_arc_blocks_bases[cpu_id];

	/* Enable DCCM termination */
	WREG32(reg_base + mmQMAN_ARC_AUX_CFG_DCCM_TERMINATE_EN, 1);

	/* Put ARC in RESET */
	reg_val = FIELD_PREP(QMAN_ARC_AUX_ARC_RST_CORE_M, 1);
	WREG32(reg_base + mmQMAN_ARC_AUX_ARC_RST, reg_val);
}

static void gaudi3_init_cbc_fw_config(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	int regs_set;

	if (gaudi3->hw_cap_initialized & HW_CAP_CBC) {
		/* CBC LBW response errors of a previous user are not cleared in compute reset */
		for (regs_set = 0 ; regs_set < NUM_OF_CBC_INVALIDATION_REGS_SETS ; regs_set++)
			WREG32(mmD0_PMMU_CBC_BASE + mmCBC_LBW_RSP_ERR_HAPPENED_CLR_0 +
					regs_set * sizeof(u32), 0x1);
		return;
	}

	dev_dbg(hdev->dev, "Initializing CBC [F/W configuration]\n");

	WREG32(mmD0_PMMU_CBC_BASE + mmCBC_SET_SCRAM_EN, 0x1);
}

static void gaudi3_init_vdec_mstr_if_axcache_hbw(struct hl_device *hdev, int hdcore, int inst,
							u32 offset, struct iterate_module_ctx *ctx)
{
	u32 axcache = (uintptr_t) ctx->data, reg_val;

	reg_val = FIELD_PREP(MSTR_IF_AXCACHE_HBW_AXCACHE_OVRD_RD_OVRD_EN_M, 0xF) |
			FIELD_PREP(MSTR_IF_AXCACHE_HBW_AXCACHE_OVRD_RD_VAL_M, axcache) |
			FIELD_PREP(MSTR_IF_AXCACHE_HBW_AXCACHE_OVRD_WR_OVRD_EN_M, 0xF) |
			FIELD_PREP(MSTR_IF_AXCACHE_HBW_AXCACHE_OVRD_WR_VAL_M, axcache);

	WREG32(mmHD0_VDEC0_MSTR_IF_AXCACHE_HBW_BASE + offset + mmMSTR_IF_AXCACHE_HBW_AXCACHE_OVRD,
			reg_val);
}

static void gaudi3_init_vdec_mstr_if(struct hl_device *hdev)
{
	struct iterate_module_ctx iter_ctx = {
		.fn = gaudi3_init_vdec_mstr_if_axcache_hbw,
		.data = (void *) (uintptr_t) AXCACHE_DO_NOT_SKIP_CACHE
	};

	gaudi3_iterate_decoders(hdev, &iter_ctx);
}

static void gaudi3_init_mstr_if_fw_config(struct hl_device *hdev)
{
	gaudi3_init_vdec_mstr_if(hdev);
}

static void gaudi3_init_pdma_fw_config(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	u32 ch_reg_base, reg_base, ch_id;
	int i, j;

	if ((gaudi3->hw_cap_pdma_initialized & HW_CAP_PDMA_MASK) == HW_CAP_PDMA_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing PDMA [F/W configuration]\n");

	for (i = 0 ; i < prop->pdma_grp_max ; i++) {
		reg_base = gaudi3_pdma_grp_blocks_bases[i];

		/* Skip configuring group 'i', if all of its channels are off */
		if (!(hdev->pdma_ch_mask & (0x3FULL << (i * prop->pdma_grp_ch_max))))
			continue;

		/* Disable engine-collective STOP_ON_ERR (for all ENGINE channels) */
		WREG32(reg_base + PDMA_CMN_B_OFFSET + mmPDMA_CMN_B_CFG1,
				FIELD_PREP(PDMA_CMN_B_CFG1_STOP_ON_ERR_M, 0x0));

		for (j = 0 ; j < prop->pdma_grp_ch_max ; j++) {
			ch_id = i * prop->pdma_grp_ch_max + j;
			if (!(hdev->pdma_ch_mask & BIT(ch_id)))
				continue;

			ch_reg_base = reg_base + j * PDMA_CH_OFFSET;

			/* Enable PDMA channel STOP_ON_ERR */
			WREG32(ch_reg_base + PDMA_CH_B_OFFSET + mmPDMA_CH_B_CFG1,
				FIELD_PREP(PDMA_CH_B_CFG1_STOP_ON_ERR_M, 0x1));

			/* Halt PDMA channel upon any PDMA err. 'LBW message upon err' is off */
			WREG32(ch_reg_base + PDMA_CH_B_OFFSET + mmPDMA_CH_B_ERR_ENABLE,
					PDMA_CH_B_ERR_ENABLE_MASK);

			/* Enable PQM channel STOP_ON_ERR. 'LBW message upon err' is off */
			WREG32(ch_reg_base + PDMA_CH_B_OFFSET + mmPDMA_CH_B_PQM_CH_CFG1,
				FIELD_PREP(PDMA_CH_B_PQM_CH_CFG1_STOP_ON_ERR_M, 0x1) |
				FIELD_PREP(PDMA_CH_B_PQM_CH_CFG1_EN_ERR_MSG_M, 0x0));

			/* Halt PQM channel upon any PQM err */
			WREG32(ch_reg_base + PDMA_CH_B_OFFSET +
					mmPDMA_CH_B_PQM_CH_ERR_STOP_ENABLE,
					PDMA_CH_B_PQM_CH_ERR_STOP_ENABLE_MASK);
		}
	}
}

static void gaudi3_init_qman_common_fw_config(struct hl_device *hdev, u32 reg_base)
{
	u32 glbl_err_cfg1;

	glbl_err_cfg1 = FIELD_PREP(QMAN_GLBL_ERR_CFG1_CQF_STOP_ON_ERR_M, 0x1) |
			FIELD_PREP(QMAN_GLBL_ERR_CFG1_CP_STOP_ON_ERR_M, 0x1) |
			FIELD_PREP(QMAN_GLBL_ERR_CFG1_ARC_CQF_STOP_ON_ERR_M, 0x1) |
			FIELD_PREP(QMAN_GLBL_ERR_CFG1_ARC_STOP_ON_ERR_M, 0x1);
	WREG32(reg_base + mmQMAN_GLBL_ERR_CFG1, glbl_err_cfg1);
}

static void gaudi3_init_qman_fw_config(struct hl_device *hdev, u32 reg_base)
{
	gaudi3_init_qman_common_fw_config(hdev, reg_base);
}

static void gaudi3_init_edma_common(struct hl_device *hdev, u32 reg_base)
{
	WREG32(reg_base + mmEDMA_CMN_CFG1, FIELD_PREP(EDMA_CMN_CFG1_STOP_ON_ERR_M, 0x1));
}

static void gaudi3_init_edma_eng_fw_config(struct hl_device *hdev, int hdcore, int inst, u32 offset,
						struct iterate_module_ctx *ctx)
{
	u32 edma_cmn_reg_base, qm_reg_base;

	edma_cmn_reg_base = mmHD1_SEDMA0_CMN_BASE + offset;
	gaudi3_init_edma_common(hdev, edma_cmn_reg_base);

	qm_reg_base = mmHD1_SEDMA0_QM_BASE + offset;
	gaudi3_init_qman_fw_config(hdev, qm_reg_base);
}

static void gaudi3_init_edma_fw_config(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct iterate_module_ctx iter_ctx = {
		.fn = gaudi3_init_edma_eng_fw_config
	};

	if (!hdev->asic_prop.edma_enabled_mask)
		return;

	if ((gaudi3->hw_cap_initialized & HW_CAP_EDMA_MASK) == HW_CAP_EDMA_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing EDMAs [F/W configuration]\n");

	gaudi3_iterate_edmas(hdev, &iter_ctx);
}

static void gaudi3_init_tpc_cfg_fw_config(struct hl_device *hdev, u32 reg_base)
{
	u32 sob_base_hi, clk_enable;

	sob_base_hi = upper_32_bits(CFG_BAR_BASE + mmHD0_SYNC_MNGR_OBJS_BASE +
					mmSOB_OBJS_SOB_OBJ_0_0);
	WREG32(reg_base + mmTPC_SM_BASE_ADDRESS_HIGH, sob_base_hi);

	/* Mask stall for QM interrupt.
	 * Stall is configured for all unmasked interrupts, so use the same mask as for interrupts.
	 */
	WREG32(reg_base + mmTPC_STALL_ON_ERR_MASK_0, TPC_INTR_MASK_0_MASK);

	WREG32(reg_base + mmTPC_STALL_ON_ERR, FIELD_PREP(TPC_STALL_ON_ERR_STALL_ENABLE_M, 0x1));

	WREG32(reg_base + mmTPC_TENSOR_SMT_PRIV, 0);

	/* Set Debug clock enable, required for trace block programming.
	 * BMON HBW programming requires DBG_CLK_OFF bit to be set as 0x0
	 *
	 * TODO: check why DBG_CFG_DIS should be also set to 0x0 (SW-167875).
	 */
	clk_enable = FIELD_PREP(TPC_CLK_EN_LBW_CFG_DIS_M, 0x0);
	clk_enable |= FIELD_PREP(TPC_CLK_EN_DBG_CFG_DIS_M, 0x0);
	clk_enable |= FIELD_PREP(TPC_CLK_EN_DBG_CLK_OFF_M, 0x0);

	WREG32(reg_base + mmTPC_CLK_EN, clk_enable);
}

static void gaudi3_init_tpc_smt_fw_config(struct hl_device *hdev, u32 reg_base)
{
	u32 thread, offset;

	/* Mask stall for arithmetic interrupts.
	 * Stall is configured for all unmasked interrupts, so use the same mask as for interrupts.
	 */
	for (thread = 0 ; thread < NUM_OF_TPC_THREADS ; thread++) {
		offset = thread * TPC_THREAD_OFFSET;
		WREG32(reg_base + mmTPC_SMT_TPC_TH0_STALL_ON_ERR_MASK0 + offset,
				TPC_TH_INTR_MASK_0_MASK);
	}
}

static void gaudi3_init_tpc_eng_fw_config(struct hl_device *hdev, int hdcore, int inst, u32 offset,
						struct iterate_module_ctx *ctx)
{
	u32 tpc_cfg_reg_base, tpc_smt_reg_base, qm_reg_base;

	tpc_cfg_reg_base = mmHD0_TPC0_CFG_BASE + offset;
	gaudi3_init_tpc_cfg_fw_config(hdev, tpc_cfg_reg_base);

	tpc_smt_reg_base = mmHD0_TPC0_SMT_TPC_TH0_BASE + offset;
	gaudi3_init_tpc_smt_fw_config(hdev, tpc_smt_reg_base);

	qm_reg_base = mmHD0_TPC0_QM_BASE + offset;
	gaudi3_init_qman_fw_config(hdev, qm_reg_base);
}

static void gaudi3_init_tpc_fw_config(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct iterate_module_ctx iter_ctx = {
		.fn = gaudi3_init_tpc_eng_fw_config
	};

	if (!hdev->asic_prop.tpc_enabled_mask)
		return;

	if ((gaudi3->hw_cap_tpc_initialized & HW_CAP_TPC_MASK) == HW_CAP_TPC_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing TPCs [F/W configuration]\n");

	gaudi3_iterate_tpcs(hdev, &iter_ctx);
}

static void gaudi3_init_mme_ctrl_lo_row_enable(struct hl_device *hdev, u32 reg_base)
{
	if (hdev->fw_components & FW_TYPE_PREBOOT_CPU)
		return;

	WREG32(reg_base + mmMME_CTRL_LO_REDUN_PSOC_SEL_SEC, 0x0);
	WREG32(reg_base + mmMME_CTRL_LO_EU0_REDUN_CLK_EN, GAUDI3_EU0_REDUN_CLK_EN);
	WREG32(reg_base + mmMME_CTRL_LO_EU1_REDUN_CLK_EN, GAUDI3_EU1_REDUN_CLK_EN);
	WREG32(reg_base + mmMME_CTRL_LO_REDUN,
			FIELD_PREP(MME_CTRL_LO_REDUN_EU0_M, 0x10) |
			FIELD_PREP(MME_CTRL_LO_REDUN_EU1_M, 0x10));

	WREG32(reg_base + mmMME_CTRL_LO_EU_ISOLATION_DIS,
			FIELD_PREP(MME_CTRL_LO_EU_ISOLATION_DIS_EU0_M, 0x1) |
			FIELD_PREP(MME_CTRL_LO_EU_ISOLATION_DIS_EU1_M, 0x1));
}

static void gaudi3_init_mme_ctrl_lo_fw_config(struct hl_device *hdev, u32 reg_base)
{
	u32 reg_val;

	gaudi3_init_mme_ctrl_lo_row_enable(hdev, reg_base);

	reg_val = FIELD_PREP(MME_CTRL_LO_QM_STOP_ON_SB_ERR_M, 1) |
			FIELD_PREP(MME_CTRL_LO_QM_STOP_ON_WAP_AXI_ERR_M, 1) |
			FIELD_PREP(MME_CTRL_LO_QM_STOP_ON_ARC_AXI_ERR_M, 1) |
			FIELD_PREP(MME_CTRL_LO_QM_STOP_ON_SIG_AXI_ERR_M, 1) |
			FIELD_PREP(MME_CTRL_LO_QM_STOP_ON_ARC_SERR_M, 0) |
			FIELD_PREP(MME_CTRL_LO_QM_STOP_ON_ARC_DERR_M, 1) |
			FIELD_PREP(MME_CTRL_LO_QM_STOP_ON_ACC_SERR_M, 0) |
			FIELD_PREP(MME_CTRL_LO_QM_STOP_ON_ACC_DERR_M, 1) |
			FIELD_PREP(MME_CTRL_LO_QM_STOP_ON_SB_SERR_M, 0) |
			FIELD_PREP(MME_CTRL_LO_QM_STOP_ON_SB_DERR_M, 1) |
			FIELD_PREP(MME_CTRL_LO_QM_STOP_ON_NUMERIC_ERR_M, 0);

	RMWREG32_SHIFTED(reg_base + mmMME_CTRL_LO_QM, reg_val, MME_CTRL_LO_QM_STOP_ON_ERR_MASK);
}

static void gaudi3_init_mme_eng_fw_config(struct hl_device *hdev, int hdcore, int inst, u32 offset,
						struct iterate_module_ctx *ctx)
{
	u32 ctrl_lo_reg_base, qm_reg_base;

	ctrl_lo_reg_base = mmHD0_MME_CTRL_LO_BASE + offset;
	gaudi3_init_mme_ctrl_lo_fw_config(hdev, ctrl_lo_reg_base);

	qm_reg_base = mmHD0_MME_QM_BASE + offset;
	gaudi3_init_qman_fw_config(hdev, qm_reg_base);
}

static void gaudi3_init_mme_fw_config(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct iterate_module_ctx iter_ctx = {
		.fn = gaudi3_init_mme_eng_fw_config
	};

	if (!hdev->mme_mask)
		return;

	if ((gaudi3->hw_cap_initialized & HW_CAP_MME_MASK) == HW_CAP_MME_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing MMEs [F/W configuration]\n");

	gaudi3_iterate_mmes(hdev, &iter_ctx);
}

static void gaudi3_init_rotator_cfg(struct hl_device *hdev, u32 rot_reg_base)
{
	WREG32(rot_reg_base + mmROTATOR_ERR_CFG, FIELD_PREP(ROTATOR_ERR_CFG_STOP_ON_ERR_M, 0x1));

	WREG32(rot_reg_base + mmROTATOR_MSS_SEI_MASK,
			FIELD_PREP(ROT_MSS_SEI_MASK_QM_CP_SW_STOP_M, 0x1));
}

static void gaudi3_init_rotator_engine_fw_config(struct hl_device *hdev, int hdcore, int inst,
							u32 offset,
							struct iterate_module_ctx *ctx)
{
	u32 rot_reg_base, qm_reg_base;

	rot_reg_base = mmHD1_ROT0_BASE + offset;
	gaudi3_init_rotator_cfg(hdev, rot_reg_base);

	qm_reg_base = mmHD1_ROT0_QM_BASE + offset;
	gaudi3_init_qman_fw_config(hdev, qm_reg_base);
}

static void gaudi3_init_rotator_fw_config(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct iterate_module_ctx iter_ctx = {
		.fn = gaudi3_init_rotator_engine_fw_config
	};

	if (!hdev->asic_prop.rotator_enabled_mask)
		return;

	if ((gaudi3->hw_cap_initialized & HW_CAP_ROT_MASK) == HW_CAP_ROT_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing ROTs [F/W configuration]\n");

	gaudi3_iterate_rotators(hdev, &iter_ctx);
}

static void gaudi3_init_vdec_brdg_ctrl_fw_config(struct hl_device *hdev, u32 reg_base)
{
	u32 intr_mask;

	/* Unmask idle signals from IP */
	WREG32(reg_base + mmVDEC_BRDG_CTRL_IDLE_MASK, 0x0);

	/* VCD/L2C interrupts are not required. Normal interrupts are handled through MSI-X. */
	intr_mask = FIELD_PREP(VDEC_BRDG_CTRL_CAUSE_INTR_VCD_SPI_M, 0x1) |
			FIELD_PREP(VDEC_BRDG_CTRL_CAUSE_INTR_L2C_SPI_M, 0x1) |
			FIELD_PREP(VDEC_BRDG_CTRL_CAUSE_INTR_NRM_SPI_M, 0x1);
	WREG32(reg_base + mmVDEC_BRDG_CTRL_CAUSE_INTR_MASK, intr_mask);

	/* Configure the decoder access to HBW/LBW to be non-secure */
	WREG32(reg_base + mmVDEC_BRDG_CTRL_DEC_HBW_AWPROT_1, 0x1);
	WREG32(reg_base + mmVDEC_BRDG_CTRL_DEC_HBW_ARPROT_1, 0x1);
	WREG32(reg_base + mmVDEC_BRDG_CTRL_DEC_LBW_AWPROT_1, 0x1);
	WREG32(reg_base + mmVDEC_BRDG_CTRL_DEC_LBW_ARPROT_1, 0x1);

	/* Enable VCMD normal interrupts */
	WREG32(reg_base + mmVDEC_BRDG_CTRL_NRM_INTR_MASK, 0);
	WREG32(reg_base + mmVDEC_BRDG_CTRL_NRM_MSIX_LBW_AWADDR,
			mmD0_PCIE_DBI_SIG_BASE + mmPCIE_DBI_MSIX_DOORBELL_OFF +
			CFG_BAR_BASE - LBW_BASE);
}

static void gaudi3_mask_vdec_ecc_interrupts(struct hl_device *hdev, u32 reg_base)
{
	u8 vcd_mem_id[] = {19, 23, 24, 56, 85, 88, 90, 95, 96, 100, 102, 103, 111, 114, 115};
	u32 ecc_err_mask;
	int i;

	ecc_err_mask = FIELD_PREP(VDEC_CTRL_SPECIAL_MEM_ECC_GLBL_ERR_MASK_SERR_M, 0x1) |
			FIELD_PREP(VDEC_CTRL_SPECIAL_MEM_ECC_GLBL_ERR_MASK_DERR_M, 0x1);

	/* Mask ECC interrupts for memories that might be read before being written */
	for (i = 0 ; i < ARRAY_SIZE(vcd_mem_id) ; i++) {
		WREG32(reg_base + mmVDEC_CTRL_SPECIAL_MEM_ECC_SEL, vcd_mem_id[i]);
		WREG32(reg_base + mmVDEC_CTRL_SPECIAL_MEM_ECC_ERR_MASK, ecc_err_mask);
	}
}

static void gaudi3_init_decoder_engine_fw_config(struct hl_device *hdev, int hdcore, int inst,
							u32 offset, struct iterate_module_ctx *ctx)
{
	u32 vdec_brdg_ctrl_reg_base, vdec_ctrl_reg_base;

	vdec_brdg_ctrl_reg_base = mmHD0_VDEC0_BRDG_CTRL_BASE + offset;
	gaudi3_init_vdec_brdg_ctrl_fw_config(hdev, vdec_brdg_ctrl_reg_base);

	vdec_ctrl_reg_base = mmHD0_VDEC0_CTRL_SPECIAL_BASE + offset;
	gaudi3_mask_vdec_ecc_interrupts(hdev, vdec_ctrl_reg_base);
}

static void gaudi3_init_decoder_fw_config(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct iterate_module_ctx iter_ctx = {
		.fn = gaudi3_init_decoder_engine_fw_config
	};

	if (!hdev->asic_prop.decoder_enabled_mask)
		return;

	if ((gaudi3->hw_cap_dec_initialized & HW_CAP_DEC_MASK) == HW_CAP_DEC_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing DECs [F/W configuration]\n");

	gaudi3_iterate_decoders(hdev, &iter_ctx);
}

static void gaudi3_init_odp(struct hl_device *hdev)
{
	/* Enable odp */
	if (hdev->odp_enabled)
		RMWREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_FEATURE_CONTROL, 1,
				MMU_FEATURE_CONTROL_ODP_EN_M);
}

static void gaudi3_enable_ptw_bypass(struct hl_device *hdev)
{
	u64 reg_addr;
	int i;

	if (hdev->ptw_bypass_enable)
		for (i = 0 ; i < hdev->asic_prop.num_of_dies ; ++i) {
			/* Enable ptw bypass on all dies */
			reg_addr = DIE_OFFSET * i + mmD0_PMMU_HBW_MMU_BASE + mmMMU_FEATURE_CONTROL;
			RMWREG32(reg_addr, 1, MMU_FEATURE_CONTROL_PTW_RD_VIA_PIF_M);
		}
}

static void gaudi3_init_sm_axprot_overrides(struct hl_device *hdev)
{
	struct asic_fixed_properties *props = &hdev->asic_prop;
	u32 reg_base, axprot_0_ovrd, axprot_1_ovrd;
	int i;

	/* non-privileged: AXPROT[0] = 0 */
	axprot_0_ovrd = FIELD_PREP(MSTR_IF_AXPROT_0_OVRD_OVRD_EN_M, 0x1) |
			FIELD_PREP(MSTR_IF_AXPROT_0_OVRD_VAL_M, 0x0);

	/* non-secure: AXPROT[1] = 1 */
	axprot_1_ovrd = FIELD_PREP(MSTR_IF_AXPROT_1_OVRD_OVRD_EN_M, 0x1) |
			FIELD_PREP(MSTR_IF_AXPROT_1_OVRD_VAL_M, 0x1);

	/* Override is for hdcores 1 to last, i.e. excluding 0 */
	for (i = 1 ; i < props->num_of_hdcores ; ++i) {
		reg_base = mmHD0_SYNC_MNGR_MSTR_IF_AXPROT_HBW_BASE + i * HDCORE_OFFSET;
		WREG32(reg_base + mmMSTR_IF_AXPROT_HBW_ARPROT_0_OVRD, axprot_0_ovrd);
		WREG32(reg_base + mmMSTR_IF_AXPROT_HBW_ARPROT_1_OVRD, axprot_1_ovrd);
		WREG32(reg_base + mmMSTR_IF_AXPROT_HBW_AWPROT_0_OVRD, axprot_0_ovrd);
		WREG32(reg_base + mmMSTR_IF_AXPROT_HBW_AWPROT_1_OVRD, axprot_1_ovrd);

		reg_base = mmHD0_SYNC_MNGR_MSTR_IF_AXPROT_LBW_BASE + i * HDCORE_OFFSET;
		WREG32(reg_base + mmMSTR_IF_AXPROT_LBW_ARPROT_0_OVRD, axprot_0_ovrd);
		WREG32(reg_base + mmMSTR_IF_AXPROT_LBW_ARPROT_1_OVRD, axprot_1_ovrd);
		WREG32(reg_base + mmMSTR_IF_AXPROT_LBW_AWPROT_0_OVRD, axprot_0_ovrd);
		WREG32(reg_base + mmMSTR_IF_AXPROT_LBW_AWPROT_1_OVRD, axprot_1_ovrd);
	}
}

static void gaudi3_set_decoder_clock_gating(struct hl_device *hdev, int hdcore, int inst,
						u32 offset, struct iterate_module_ctx *ctx)
{
	u32 vdec_brdg_ctrl_reg_base = mmHD0_VDEC0_BRDG_CTRL_BASE + offset;
	bool enable = (uintptr_t) ctx->data;

	WREG32(vdec_brdg_ctrl_reg_base + mmVDEC_BRDG_CTRL_CGM_DISABLE, enable ? 0x0 : 0x1);
}

static void gaudi3_set_decoders_clock_gating(struct hl_device *hdev, bool enable)
{
	struct iterate_module_ctx iter_ctx = {
		.fn = gaudi3_set_decoder_clock_gating,
		.data = (void *) (uintptr_t) enable
	};

	gaudi3_iterate_decoders(hdev, &iter_ctx);
}

static void gaudi3_enable_clock_gating(struct hl_device *hdev)
{
	gaudi3_set_decoders_clock_gating(hdev, true);
}

static void gaudi3_init_interrupt_coalescing(struct hl_device *hdev)
{
	/* Interrupt coalescing is currently disabled, and the counter value is configured to its
	 * reset value (0x400 ≈ 1 usec).
	 * The code is here mainly as a reference if in future the feature is enabled. In such a
	 * case, if the configured counter value is large enough, need to consider the PLDM factor
	 * which is ≈ 10000.
	 */
	WREG32(mmD0_PCIE_WRAP_DBI_ACCESS_BASE + mmPCIE_WRAP_DBI_ACCESS_MSIX_GLBL_CNT,
			GAUDI3_MSIX_COALESCING_CNT);
	WREG32(mmD0_PCIE_WRAP_DBI_ACCESS_BASE + mmPCIE_WRAP_DBI_ACCESS_MSIX_MASK_CTRL,
			FIELD_PREP(PCIE_WRAP_DBI_ACCESS_MSIX_MASK_CTRL_EN_M, 0x0) |
			FIELD_PREP(PCIE_WRAP_DBI_ACCESS_MSIX_MASK_CTRL_RESP_M, 0x1));
}

/*
 * looking at [S/D]TLB specs we can see that code -> order (size) mapping is:
 * code		order	(size)
 * 0x0		20	(1M)
 * 0x1		21	(2M)
 * ...
 * 0xB		31	(2G)
 *
 * Note that 0x7 (i.e. 128MB page) is reserved but is already checked
 * in the flow so allowing it here to be more efficient.
 */
static inline u32 gaudi3_tlb_page_map_order_to_code(u8 order)
{
	return order - PAGE_SHIFT_1MB;
}

/**
 * tlb_get_next_page_ctrl_cfg - get the next page size code
 *
 * @supported_pages_mask: mask of pages sizes we need to support in TLBs
 *
 * The function receives pages bitmask, finds the next page for TLB support and
 * return the page code to the caller.
 *
 * Notes:
 * 1. caller must guarantee mask is valid and not 0
 * 2. caller must guarantee valid number of set bits
 * 3. the function will modify the input mask by clearing the next set bit
 *
 */
static u32 tlb_get_next_page_ctrl_cfg(u64 *supported_pages_mask)
{
	u8 page_order;
	u32 val;

	page_order = __ffs(*supported_pages_mask);
	val = gaudi3_tlb_page_map_order_to_code(page_order);

	/* clear the bit */
	*supported_pages_mask &= (~BIT_ULL(page_order));

	return val;
}

/*
 * we want to use the same function to generate the TLB CNTRL_PAGE_SIZE mask.
 * in order to use common function we have to make sure the fields are exactly the same
 * between STLB and DTLB
 */
static_assert(DTLB_CNTRL_PAGE_SIZE_TYPE1_PAGE_SIZE_M == STLB_CNTRL_PAGE_SIZE_TYPE1_PAGE_SIZE_M);
static_assert(DTLB_CNTRL_PAGE_SIZE_TYPE2_PAGE_SIZE_M == STLB_CNTRL_PAGE_SIZE_TYPE2_PAGE_SIZE_M);
static_assert(DTLB_CNTRL_PAGE_SIZE_TYPE3_PAGE_SIZE_M == STLB_CNTRL_PAGE_SIZE_TYPE3_PAGE_SIZE_M);

/**
 * build_tlb_ctrl_page_size - create the correct config value for CNTRL_PAGE_SIZE
 *
 * @hdev: pointer to the habanalabs device structure
 *
 * The function generates the correct configuration value for CNTRL_PAGE _SIZE
 *
 * Notes:
 * 1. caller must guarantee mask is valid and not 0
 * 2. caller must guarantee valid number of set bits
 * 3. the function will modify the input mask by clearing the next set bit
 * 4. used for both STLD and DTLB
 */
static u32 build_tlb_ctrl_page_size(struct hl_device *hdev)
{
	u64 hmmu_small_pages_mask, hmmu_large_pages_mask;
	u32 reg_val = 0, val;
	u8 page_type;

	hmmu_small_pages_mask =
		hdev->asic_prop.dmmu.supported_pages_mask & GAUDI3_HMMU_VALID_SMALL_PAGES_MASK;
	hmmu_large_pages_mask =
		hdev->asic_prop.dmmu.supported_pages_mask & GAUDI3_HMMU_VALID_LARGE_PAGES_MASK;

	/* search for all supported small pages */
	for (page_type = 0; page_type < GAUDI3_HMMU_MAX_SMALL_PAGES_NUM; page_type++) {
		if (!hmmu_small_pages_mask)
			break;

		val = tlb_get_next_page_ctrl_cfg(&hmmu_small_pages_mask);

		switch (page_type) {
		case 0:
			reg_val |= FIELD_PREP(DTLB_CNTRL_PAGE_SIZE_TYPE1_PAGE_SIZE_M, val);
			break;
		case 1:
			reg_val |= FIELD_PREP(DTLB_CNTRL_PAGE_SIZE_TYPE2_PAGE_SIZE_M, val);
			break;
		}
	}

	if (!hmmu_large_pages_mask)
		return reg_val;

	val = tlb_get_next_page_ctrl_cfg(&hmmu_large_pages_mask);
	reg_val |= FIELD_PREP(DTLB_CNTRL_PAGE_SIZE_TYPE3_PAGE_SIZE_M, val);

	return reg_val;
}

static void gaudi3_dtlb_init(struct hl_device *hdev, int block, int inst, u32 offset,
				struct iterate_module_ctx *ctx)
{
	struct tlb_init_data *init_data = ctx->data;

	/* the iterator already supplies the DTLB base in offset so it will be used as dtlb base */

	/* set DTLB unit ID */
	WREG32(offset + mmDTLB_UNIT_ID, inst);

	/* set supported page sizes */
	WREG32(offset + mmDTLB_CNTRL_PAGE_SIZE, init_data->cntrl_page_size);

	/*
	 * set HBM params: single HBM memory size and number of HBMs
	 * This RMW is to avoid overriding the value in DCORE0_HAS_*HBM field
	 * written by FW in the binning phase.
	 */
	RMWREG32_SHIFTED(offset + mmDTLB_HBM_CONF,
				FIELD_PREP(DTLB_HBM_CONF_INDX_M, 0x4) |
				FIELD_PREP(DTLB_HBM_CONF_NUM_HBM_M,
						hdev->asic_prop.num_functional_hbms),
				DTLB_HBM_CONF_INDX_M | DTLB_HBM_CONF_NUM_HBM_M);

	/*
	 *  we want pre-boot to run on SRAM that is in one-die (48MB SRAM)
	 *  and at same time be able to access an HBM space that covers both dies
	 *  to support this we use a bit in the spare register of DTLB so that
	 *  we can increase NUM_HBM to 8 and stay on same SRAM space.
	 */
	if (hdev->asic_prop.num_of_dies == MAX_NUM_OF_DIES)
		RMWREG32(offset + DTLB_SPECIAL_GLBL_SPARE_0_OFFSET, 0x1, 0x1);
}

static void gaudi3_hdcore_stlb_init_fw_config(struct hl_device *hdev)
{
	u32 val;
	int i;

	/* set page size types */
	val = build_tlb_ctrl_page_size(hdev);
	gaudi3_lbw_dup_group_push(hdev,	GAUDI3_DUP_GRP_STLB_BASE, mmSTLB_CNTRL_PAGE_SIZE, val);

	/* unmask STLB interrupts */
	for (i = 0; i < hdev->asic_prop.num_of_hdcores ; ++i) {
		WREG32(mmHD0_STLB_BASE + mmSTLB_INTR_SPI_MASK + (i * HDCORE_OFFSET),
			FIELD_PREP(STLB_INTR_SPI_MASK_FAULT_UNMAPPED_MSK_M, 1) |
			FIELD_PREP(STLB_INTR_SPI_MASK_FAULT_PERMISSION_MSK_M, 1) |
			FIELD_PREP(STLB_INTR_SPI_MASK_FAULT_PTW_DATA_MSK_M, 1) |
			FIELD_PREP(STLB_INTR_SPI_MASK_MAINT_QUEUE_FULL_MSK_M, 1) |
			FIELD_PREP(STLB_INTR_SPI_MASK_MAINT_PREFETCH_FAIL_MSK_M, 1));
		WREG32(mmHD0_STLB_BASE + mmSTLB_INTR_SEI_MASK + (i * HDCORE_OFFSET),
			FIELD_PREP(STLB_INTR_SEI_MASK_LBW_RSP_ERR_MSK_M, 1) |
			FIELD_PREP(STLB_INTR_SEI_MASK_PTW_RSP_ERR_MSK_M, 1) |
			FIELD_PREP(STLB_INTR_SEI_MASK_TRANS_REQ_ERR_MSK_M, 1) |
			FIELD_PREP(STLB_INTR_SEI_MASK_MAINT_REQ_ERR_MSK_M, 1));
	}

}

static void gaudi3_init_hbm_mmu_fw_config(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct tlb_init_data dtlb_init_data;
	struct iterate_module_ctx ctx = {
		.fn = gaudi3_dtlb_init,
		.data = &dtlb_init_data,
	};

	if (!hdev->dram_enable)
		return;

	if (gaudi3->hw_cap_initialized & HW_CAP_HMMU_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing HBM MMU (FW init)\n");

	dtlb_init_data.cntrl_page_size = build_tlb_ctrl_page_size(hdev);
	gaudi3_iterate_dtlbs(hdev, &ctx);

	gaudi3_hdcore_stlb_init_fw_config(hdev);
}

static void gaudi3_init_pci_mmu_fw_config(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	if (gaudi3->hw_cap_initialized & HW_CAP_PMMU)
		return;

	dev_dbg(hdev->dev, "Initializing PCI MMU (FW init)\n");

	/* init PMMU prefetch-cache to store only hop5 */
	RMWREG32(mmD0_PMMU_HBW_STLB_BASE + mmPSTLB_MEM_CACHE_CONFIG, 0x20,
			PSTLB_MEM_CACHE_CONFIG_CACHE_HOP_PREFETCH_EN_M);
}

static void gaudi3_init_mmu_fw_config(struct hl_device *hdev)
{
	gaudi3_init_pci_mmu_fw_config(hdev);
	gaudi3_init_hbm_mmu_fw_config(hdev);
}

enum gaudi3_ac_instruction {
	GAUDI3_AC_INSTRUCTION_WREG,
	GAUDI3_AC_INSTRUCTION_POL,
	GAUDI3_AC_INSTRUCTION_RREG,
	GAUDI3_AC_INSTRUCTION_DLY,
};

static void gaudi3_ac_add_instruction(struct hl_device *hdev, u32 etr_idx,
				      u8 pos, enum gaudi3_ac_instruction opcode,
				      u32 addr, u32 data)
{
	u64 ac_off = gaudi3_etr_ac_config[etr_idx].ac_off;

	WREG32(mmD0_NCH_AC_BASE + mmAUTONOMOUS_CONTROL_CMD_ENTRY_0 + ac_off + pos * sizeof(u32),
			opcode);
	WREG32(mmD0_NCH_AC_BASE + mmAUTONOMOUS_CONTROL_ADDR_ENTRY_0 + ac_off + pos * sizeof(u32),
			addr);
	WREG32(mmD0_NCH_AC_BASE + mmAUTONOMOUS_CONTROL_DATA_ENTRY_0 + ac_off + pos * sizeof(u32),
			data);
}

static void gaudi3_ac_program(struct hl_device *hdev, u32 etr_idx)
{
	u32 doorbell, poll_mask, rwp, rwp_msb_mask;
	u64 ac_off, etr_off;
	u8 i = 0;

	ac_off = gaudi3_etr_ac_config[etr_idx].ac_off;
	etr_off = gaudi3_etr_ac_config[etr_idx].etr_off;

	doorbell = CFG_BAR_BASE - LBW_BASE + mmD0_PCIE_DBI_SIG_BASE + mmPCIE_DBI_MSIX_DOORBELL_OFF;
	poll_mask = CFG_BAR_BASE - LBW_BASE + mmD0_NCH_AC_BASE +
			mmAUTONOMOUS_CONTROL_POLLING_MASK + ac_off;
	rwp = CFG_BAR_BASE - LBW_BASE + mmD0_NCH_ETR_BASE + mmETR_RWP + etr_off;

	/* This assumes etr_buf_dram_size is power of 2 */
	rwp_msb_mask = hdev->asic_prop.etr_buf_dram_size >> 1;

	/*
	 * Observing the buffer write pointer, we are waiting for an MSB flip.
	 * For power of 2 buffer size, this means buffer is halfway full.
	 * We are not looking for an exact value, rather for an MSB value. This
	 * is because the watermark may never reach the exact value of half
	 * buffer, but jump over it. So MSB flip indicates that the half has
	 * passed, at least, but not necessarily exactly, which is what is
	 * important.
	 */
	gaudi3_ac_add_instruction(hdev, etr_idx, i++, GAUDI3_AC_INSTRUCTION_WREG,
				  poll_mask, rwp_msb_mask);

	/* Wait for ETR RWP to reach half buffer size, i.e MSB = 1*/
	gaudi3_ac_add_instruction(hdev, etr_idx, i++, GAUDI3_AC_INSTRUCTION_POL,
				  rwp, rwp_msb_mask);
	/* Issue an interrupt */
	gaudi3_ac_add_instruction(hdev, etr_idx, i++, GAUDI3_AC_INSTRUCTION_WREG,
				  doorbell, GAUDI3_IRQ_NUM_ETR_FIRST + etr_idx);

	/* Wait for the full wraparound, i.e MSB = 0 */
	gaudi3_ac_add_instruction(hdev, etr_idx, i++, GAUDI3_AC_INSTRUCTION_POL,
				  rwp, 0);
	/* Issue an interrupt, again */
	gaudi3_ac_add_instruction(hdev, etr_idx, i++, GAUDI3_AC_INSTRUCTION_WREG,
				  doorbell, GAUDI3_IRQ_NUM_ETR_FIRST + etr_idx);

	/* At this point, AC will go back to the first instruction */

	/* Modify the number of commands, without the activation bit */
	RMWREG32(mmD0_NCH_AC_BASE + mmAUTONOMOUS_CONTROL_CTRL + ac_off, i - 1,
				AUTONOMOUS_CONTROL_CTRL_NUM_CMD_M);
}

static void gaudi3_ac_program_all(struct hl_device *hdev)
{
	u32 etr_idx;

	for (etr_idx = 0; etr_idx < hdev->asic_prop.etr_buf_number; ++etr_idx)
		gaudi3_ac_program(hdev, etr_idx);
}

void gaudi3_ac_start_no_fw(struct hl_device *hdev, u32 etr_idx)
{
	u64 base = gaudi3_etr_ac_config[etr_idx].ac_off;

	RMWREG32(mmD0_NCH_AC_BASE + mmAUTONOMOUS_CONTROL_CTRL + base, 1,
			AUTONOMOUS_CONTROL_CTRL_EN_M);
}

void gaudi3_ac_stop_no_fw(struct hl_device *hdev, u32 etr_idx)
{
	u64 base = gaudi3_etr_ac_config[etr_idx].ac_off;

	RMWREG32(mmD0_NCH_AC_BASE + mmAUTONOMOUS_CONTROL_CTRL + base, 0,
			AUTONOMOUS_CONTROL_CTRL_EN_M);
}

static void gaudi3_init_credits(struct hl_device *hdev)
{
	gaudi3_init_n2r_credits(hdev);
	gaudi3_init_r2c_credits(hdev);
}

/* Since PCIE blocks don't reset following a reset, every stage we'll go through
 * will be with 'Fabric Initialization' enabled, which was proven to fail the HW
 * init flow. Hence, we must manually reset this feature prior to any reset.
 */
void gaudi3_fabric_serialization_fini_fw_config(struct hl_device *hdev)
{
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	/* Disable Early Write (B)Response (Enable immediate fake mesh response) */
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_PCIE_WR_BUF_0, 0x0);

	/* Disable Write Cache Override */
	WREG32(mmD0_PCIE_DBI_SIG_BASE + mmPCIE_DBI_COHERENCY_CONTROL_3_OFF, 0x0);

	/* 1. Disable LBW Write/s 'Fabric Serialization'.
	 * 2. Disable the 1st Serialization Region (8 possible LBW regions).
	 */
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_ONE_IN_FLIGHT, 0x0);

	/* Reset the 1st Serialization Region (8 possible LBW regions) */
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_SERIALIZATION_REGION_START_0, 0x0);
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_SERIALIZATION_REGION_END_0, 0x0);

	/* Reset the response value for dummy LBW transactions */
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_DUMMY_LBW_REGION_RESP, 0xF);
}

/* Naturally, all FW configurations should go under gaudi3_hw_init_fw_config().
 * 'Fabric Serialization Enhancement', however, is an exception as it might
 * alter PCIE reordering rules. Hence, we should make sure it's called long
 * after the HW has already been initialized.
 */
void gaudi3_fabric_serialization_init_fw_config(struct hl_device *hdev)
{
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	/* Enable Early Write (B)Response (Enable immediate fake mesh response) */
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_PCIE_WR_BUF_0,
			FIELD_PREP(PCIE_WRAP_PCIE_WR_BUF_VAL_M, 0x1));

	/* Enable Write Cache Override */
	WREG32(mmD0_PCIE_DBI_SIG_BASE + mmPCIE_DBI_COHERENCY_CONTROL_3_OFF,
			FIELD_PREP(PCIE_DBI_COHERENCY_CONTROL_3_OFF_CFG_MSTR_AWCACHE_MODE_M, 0xF) |
			FIELD_PREP(PCIE_DBI_COHERENCY_CONTROL_3_OFF_CFG_MSTR_AWCACHE_VALUE_M, 0x1));

	/* 1. Enable LBW Write/s 'Fabric Serialization'.
	 * 2. Enable the 1st Serialization Region (8 possible LBW regions).
	 */
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_ONE_IN_FLIGHT,
			FIELD_PREP(PCIE_WRAP_ONE_IN_FLIGHT_HBW_BLOCK_LBW_EN_M, 0x1) |
			FIELD_PREP(PCIE_WRAP_ONE_IN_FLIGHT_LBW_SERIAL_REGION0_EN_M, 0x1));

	/* Configure the 1st Serialization Region (8 possible LBW regions) */
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_SERIALIZATION_REGION_START_0,
			FIELD_PREP(PCIE_WRAP_LBW_SERIALIZATION_REGION_START_ADDR_M,
					mmD0_PIF_DUMMY_LBW_BLK_BASE));
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_SERIALIZATION_REGION_END_0,
			FIELD_PREP(PCIE_WRAP_LBW_SERIALIZATION_REGION_END_ADDR_M,
					mmD0_PIF_DUMMY_LBW_BLK_BASE + sizeof(u32)));

	/* Configure the response value for dummy LBW transactions */
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_DUMMY_LBW_REGION_RESP, 0x0);
}

void gaudi3_hw_init_fw_config(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct gaudi3_cn_aux_ops *aux_ops = &gaudi3->cn_aux_ops;
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;

	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	hdev->asic_funcs->set_binning_masks(hdev);

	gaudi3_print_sol_config_version(hdev);
	gaudi3_reset_config(hdev);
	gaudi3_init_cslice(hdev);
	gaudi3_init_credits(hdev);
	gaudi3_set_isolation(hdev, false, false);
	gaudi3_init_cbc_fw_config(hdev);
	gaudi3_init_mstr_if_fw_config(hdev);
	gaudi3_init_pdma_fw_config(hdev);
	gaudi3_init_edma_fw_config(hdev);
	gaudi3_init_tpc_fw_config(hdev);

	/*
	 * Note that inside this mme fw config we skip part of them if preboot exist
	 * since part of those configs are done in preboot and the other part in FW management app.
	 */
	gaudi3_init_mme_fw_config(hdev);
	gaudi3_init_rotator_fw_config(hdev);
	gaudi3_init_decoder_fw_config(hdev);
	gaudi3_init_sm_axprot_overrides(hdev);
	gaudi3_enable_clock_gating(hdev);
	gaudi3_init_odp(hdev);
	gaudi3_init_regulators(hdev);
	gaudi3_init_interrupt_coalescing(hdev);
	gaudi3_init_mmu_fw_config(hdev);
	gaudi3_ac_program_all(hdev);
	gaudi3_enable_ptw_bypass(hdev);
	gaudi3_init_qos(hdev);
	if (aux_ops->sei_err_event_handler)
		aux_ops->restore_dynamic_cfg_soft_reset_fw(aux_dev);

	if (hdev->cache_enable)
		gaudi3_init_cache(hdev);
}

static int gaudi3_wait_outbound_outsatnding_complete(struct hl_device *hdev)
{
	u32 val;
	int rc;


	rc = hl_poll_timeout(
			hdev,
			mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_OUTBOUND_OUTSTANDING,
			val,
			(val == 0x0),
			1000,
			200000);
	if (rc) {
		dev_err(hdev->dev, "wait for PCIE OB outstanding timed out\n");
		return rc;
	}

	return 0;
}

static void gaudi3_execute_hard_reset_no_fw(struct hl_device *hdev)
{
	/* Set the device to handle FLR by H/W as we put the device CPU to halt mode */

	WREG32(mmD0_PCIE_AUX_BASE + mmPCIE_AUX_FLR_CTRL,
			(PCIE_AUX_FLR_CTRL_HW_CTRL_M | PCIE_AUX_FLR_CTRL_INT_MASK_M));

	gaudi3_send_hard_reset_cmd(hdev);

	/* Restart BTL/BLR upon hard-reset */
	WREG32(mmD0_PSOC_GLOBAL_CONF_BASE + mmGLOBAL_CONF_BOOT_SEQ_RE_START, 0x770);
}

void gaudi3_execute_reset_no_fw(struct hl_device *hdev, bool hard_reset)
{
	u32 d0_reset_reg;

	gaudi3_wait_outbound_outsatnding_complete(hdev);

	if (hard_reset) {
		gaudi3_execute_hard_reset_no_fw(hdev);
		d0_reset_reg = mmD0_PSOC_RESET_CONF_BASE + mmPSOC_RESET_CONF_SW_ALL_RST;
	} else {
		d0_reset_reg = mmD0_PSOC_RESET_CONF_BASE + mmPSOC_RESET_CONF_SOFT_RST;
	}

	if (hdev->asic_prop.num_of_dies == MAX_NUM_OF_DIES) {
		/* generate reset in DIE1 */
		WREG32(d0_reset_reg + DIE_OFFSET, 0x1);

		/* sleep to make sure that DIE1 started its reset before resetting DIE0 */
		usleep_range(1000, 1500);
	}

	WREG32(d0_reset_reg, 0x1);

	dev_dbg(hdev->dev, "Driver issued %s reset command\n", hard_reset ? "HARD" : "SOFT");
}

static u32 gaudi3_get_qm_sw_map_idx(u32 base)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(gaudi3_qm_irq_map_table); i++) {
		if (gaudi3_qm_irq_map_table[i].base == base)
			return i;
	}

	return ARRAY_SIZE(gaudi3_qm_irq_map_table);
}

static void gaudi3_cfg_qm_sw_irq(struct hl_device *hdev, int block, int inst,
						u32 offset, struct iterate_module_ctx *ctx)
{
	u32 first_qm_reg_base = *(u32 *) ctx->data, qm_reg_base = first_qm_reg_base + offset;
	u64 msix_addr = CFG_BAR_BASE + mmD0_PCIE_MSIX_BASE;
	u32 irq, msix_addr_lo, msix_addr_hi;

	msix_addr_hi = upper_32_bits(msix_addr);
	msix_addr_lo = lower_32_bits(msix_addr);

	irq = gaudi3_get_qm_sw_map_idx(qm_reg_base);
	if (irq == ARRAY_SIZE(gaudi3_qm_irq_map_table)) {
		dev_err(hdev->dev, "invalid qm sw base: 0x%x, first base: 0x%x\n", qm_reg_base,
						first_qm_reg_base);
		return;
	}
	irq += GAUDI3_PLDM_QM_SW_IRQ_FIRST;

	WREG32(qm_reg_base + mmQMAN_GLBL_ERR_ADDR_HI, msix_addr_hi);
	WREG32(qm_reg_base + mmQMAN_GLBL_ERR_ADDR_LO, msix_addr_lo);
	WREG32(qm_reg_base + mmQMAN_GLBL_ERR_WDATA, irq);
	WREG32(qm_reg_base + mmQMAN_GLBL_ERR_CFG, GAUDI3_GLBL_ERR_CFG);
}

static void gaudi3_enable_qm_sw_interrupt_msgs(struct hl_device *hdev)
{
	u32 first_qm_reg_base;
	struct iterate_module_ctx iter_ctx = {
		.fn = gaudi3_cfg_qm_sw_irq,
		.data = &first_qm_reg_base
	};

	static_assert(ARRAY_SIZE(gaudi3_qm_irq_map_table) == GAUDI3_NUM_OF_SW_QM_INTR);

	first_qm_reg_base = mmHD1_SEDMA0_QM_BASE;
	gaudi3_iterate_edmas(hdev, &iter_ctx);

	first_qm_reg_base = mmHD0_TPC0_QM_BASE;
	gaudi3_iterate_tpcs(hdev, &iter_ctx);

	first_qm_reg_base = mmHD0_MME_QM_BASE;
	gaudi3_iterate_mmes(hdev, &iter_ctx);

	first_qm_reg_base = mmHD1_ROT0_QM_BASE;
	gaudi3_iterate_rotators(hdev, &iter_ctx);
}

static void gaudi3_enable_interrupt_aggr_msgs(struct hl_device *hdev)
{
	u32 offset, die, intr_agg, irq, i, msix_addr, sts0, sts1, sts2;
	struct asic_fixed_properties *props = &hdev->asic_prop;

	if (!hdev->enable_intr_aggr)
		return;

	/* Enable interrupt messages for all aggregators in CPU and PSOC blocks */
	for (die = 0 ; die < props->num_of_dies ; ++die) {
		/* Both die0 and die1 aggregators should write to die0 PCIE_MSIX.
		 * Due to a H/W bug (H9-5161), there is a bit flip in bit[23] of the configured
		 * address for die1 CPU aggregators.
		 * As this bit is the die select, the W/A for these aggregators is writing the other
		 * die address, i.e. in this case: die1 PCIE_MSIX instead of die0 PCIE_MSIX.
		 */
		irq = GAUDI3_PLDM_AGGR_IRQ_FIRST + (die * INTR_AGGR_NUM_OF_MSIX_VECTORS_PER_DIE);
		msix_addr = CFG_BAR_BASE - LBW_BASE + mmD0_PCIE_MSIX_BASE + die * DIE_OFFSET;

		/* CPU HDCORE aggregators */
		for (intr_agg = 0 ; intr_agg < CPU_INTR_AGGR_NUM_OF_HDCORE_AGGR ; intr_agg++) {
			offset = die * DIE_OFFSET + intr_agg * INTR_AGG_BLOCK_OFFSET;

			/* REI DERR */
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
					mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MSG_CFG + offset, 0x1);
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
					mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MSG_ADDR + offset,
						msix_addr);
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
					mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MSG_DATA + offset, irq++);

			/* REI SERR */
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_SERR_INT_MSG_BASE +
					mmINT_AGG_HDCORE_REI_SERR_INT_MSG_MSG_CFG + offset, 0x1);
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_SERR_INT_MSG_BASE +
					mmINT_AGG_HDCORE_REI_SERR_INT_MSG_MSG_ADDR + offset,
						msix_addr);
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_SERR_INT_MSG_BASE +
					mmINT_AGG_HDCORE_REI_SERR_INT_MSG_MSG_DATA + offset, irq++);

			/* SEI */
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
					mmINT_AGG_HDCORE_SEI_INT_MSG_MSG_CFG + offset, 0x1);
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
					mmINT_AGG_HDCORE_SEI_INT_MSG_MSG_ADDR + offset, msix_addr);
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
					mmINT_AGG_HDCORE_SEI_INT_MSG_MSG_DATA + offset, irq++);

			/* SPI ECO */
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
					mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MSG_CFG + offset, 0x1);
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
					mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MSG_ADDR + offset,
						msix_addr);
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
					mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MSG_DATA + offset, irq++);
		}

		/* CPU SHARED aggregator */
		offset = die * DIE_OFFSET;

		/* REI DERR */
		WREG32(mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MSG_CFG + offset, 0x1);
		WREG32(mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MSG_ADDR + offset, msix_addr);
		WREG32(mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MSG_DATA + offset, irq++);

		/* REI SERR */
		WREG32(mmD0_CPU_INT_AGG_SHARED_REI_SERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_SERR_INT_MSG_MSG_CFG + offset, 0x1);
		WREG32(mmD0_CPU_INT_AGG_SHARED_REI_SERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_SERR_INT_MSG_MSG_ADDR + offset, msix_addr);
		WREG32(mmD0_CPU_INT_AGG_SHARED_REI_SERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_SERR_INT_MSG_MSG_DATA + offset, irq++);

		/* SEI */
		WREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MSG_CFG + offset, 0x1);
		WREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MSG_ADDR + offset, msix_addr);
		WREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MSG_DATA + offset, irq++);

		/* SPI ECO */
		WREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MSG_CFG + offset, 0x1);
		WREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MSG_ADDR + offset, msix_addr);
		WREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MSG_DATA + offset, irq++);

		/* Several events are set always at bringup time and need to mask them.
		 * At the moment driver doesn't have the information of how to clear them
		 * so as a WA will mask them.
		 */
		sts0 = RREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_STS_0 + offset);
		sts1 = RREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_STS_1 + offset);
		sts2 = RREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_STS_2 + offset);

		WREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + offset, sts0);
		WREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset, sts1);
		WREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_2 + offset, sts2);

		WREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MSG_PENDING + offset, 1);

		sts0 = RREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_STS_0 + offset);
		WREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset, sts0);
		WREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MSG_PENDING + offset, 1);

		/*
		 * PSOC aggregators.
		 * Skip when running with preboot because it needs to handle the PSOC aggregators
		 * interrupts, and they should be routed to PSOC ARC.
		 */
		if (!(hdev->fw_components & FW_TYPE_PREBOOT_CPU)) {
			msix_addr = CFG_BAR_BASE - LBW_BASE + mmD0_PCIE_MSIX_BASE;

			for (i = 0 ; i < PSOC_INTR_AGGR_NUM_OF_AGGR_BLOCKS ; i++) {
				offset = die * DIE_OFFSET + i * PARC_INTR_BLOCK_OFFSET;
				WREG32(mmD0_PARC_INT_AGGR_UART_COMB_BASE +
					mmINT_AGG_PSOC_UART_COMB_MSG_CFG + offset, 0x1);
				WREG32(mmD0_PARC_INT_AGGR_UART_COMB_BASE +
					mmINT_AGG_PSOC_UART_COMB_MSG_ADDR + offset, msix_addr);
				WREG32(mmD0_PARC_INT_AGGR_UART_COMB_BASE +
					mmINT_AGG_PSOC_UART_COMB_MSG_DATA + offset, irq++);
			}

			/* Unmask MSG2WIRE PSOC0 and SH_HD
			 * All msgs that comes from shared/HDECORE aggregator will pass through
			 * this MSG2WIRE_SH_HD mask, and events of the PSOC will pass
			 * through MSG2WIRE_PSOC_0/1 block.
			 * Note that no need to unmask ARCs masks since we're not
			 * targeting the messages to ARCs but to PCIe MSIx
			 */
			offset = die * DIE_OFFSET;
			WREG32(mmD0_PARC_MSG2WIRE_PSOC_0_BASE + mmMSG2WIRE_PSOC_0_MASK_0 +
											offset, 0);
			WREG32(mmD0_PARC_MSG2WIRE_PSOC_0_BASE + mmMSG2WIRE_PSOC_0_MASK_1 +
											offset, 0);
			WREG32(mmD0_PARC_MSG2WIRE_SH_HD_BASE + mmMSG2WIRE_SH_HD_MASK_0 + offset, 0);
			WREG32(mmD0_PARC_MSG2WIRE_SH_HD_BASE + mmMSG2WIRE_SH_HD_MASK_1 + offset, 0);
		}
	}
}

static void gaudi3_disable_interrupt_aggr_msgs(struct hl_device *hdev)
{
	struct asic_fixed_properties *props = &hdev->asic_prop;
	u32 offset, die, intr_agg;

	if (!hdev->enable_intr_aggr)
		return;

	/* Disable interrupt messages for all aggregators in CPU and PSOC blocks */
	for (die = 0 ; die < props->num_of_dies ; ++die) {
		/* CPU HDCORE aggregators */
		for (intr_agg = 0 ; intr_agg < CPU_INTR_AGGR_NUM_OF_HDCORE_AGGR ; ++intr_agg) {
			offset = die * DIE_OFFSET + intr_agg * INTR_AGG_BLOCK_OFFSET;

			/* REI DERR */
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
					mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MSG_CFG + offset, 0x0);
			/* REI SERR */
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_SERR_INT_MSG_BASE +
					mmINT_AGG_HDCORE_REI_SERR_INT_MSG_MSG_CFG + offset, 0x0);
			/* SEI */
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
					mmINT_AGG_HDCORE_SEI_INT_MSG_MSG_CFG + offset, 0x0);
			/* SPI ECO */
			WREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
					mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MSG_CFG + offset, 0x0);
		}

		/* CPU SHARED aggregator */
		offset = die * DIE_OFFSET;

		/* REI DERR */
		WREG32(mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MSG_CFG + offset, 0x0);
		/* REI SERR */
		WREG32(mmD0_CPU_INT_AGG_SHARED_REI_SERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_SERR_INT_MSG_MSG_CFG + offset, 0x0);
		/* SEI */
		WREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MSG_CFG + offset, 0x0);
		/* SPI ECO */
		WREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MSG_CFG + offset, 0x0);

		/* PSOC aggregators */
		if (hdev->fw_components & FW_TYPE_PREBOOT_CPU)
			continue;

		for (intr_agg = 0 ; intr_agg < PSOC_INTR_AGGR_NUM_OF_AGGR_BLOCKS ; ++intr_agg) {
			offset = die * DIE_OFFSET + intr_agg * PARC_INTR_BLOCK_OFFSET;
			WREG32(mmD0_PARC_INT_AGGR_UART_COMB_BASE +
					mmINT_AGG_PSOC_UART_COMB_MSG_CFG + offset, 0x0);
		}
	}
}

/* PARC_SEI[0] is generated due to a boot FSM access to a bad register address (H9-5613) */
static void gaudi3_clear_parc_sei_interrupt(struct hl_device *hdev)
{
	u32 die, offset, reg_base, sei_cause;

	for (die = 0 ; die < hdev->asic_prop.num_of_dies ; ++die) {
		offset = die * DIE_OFFSET;

		reg_base = mmD0_PARC_GLOBAL_CONF_BASE + offset;
		sei_cause = RREG32(reg_base + mmPARC_GLOBAL_CONF_SEI_INTR_CTRL_CAUSE);
		WREG32(reg_base + mmPARC_GLOBAL_CONF_SEI_INTR_CTRL_CLEAR, sei_cause);

		reg_base = mmD0_NRTR0_2CH_CTRL0_LBW_CH_RAZWI_LBW_CH1_BASE + offset;
		WREG32(reg_base + ADDR_DECODER_AW_OFFSET, 0x1);

		reg_base = mmD0_PARC_MSTR_IF_XRESP_LBW_BASE + offset;
		WREG32(reg_base + mmMSTR_IF_XRESP_LBW_INTR_CTRL_CLEAR,
				FIELD_PREP(MSTR_IF_XRESP_LBW_INTR_CTRL_CLEAR_BRESP_ERR_M, 0x1));

		RREG32(hdev->asic_prop.pcie_flush_reg_addr);

		reg_base = mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE + offset;
		WREG32(reg_base + mmINT_AGG_SHARED_SEI_INT_MSG_STS_0,
				FIELD_PREP(INT_AGG_SHARED_SEI_INT_MSG_STS_0_PARC_0_M, 0x1));
		WREG32(reg_base + mmINT_AGG_SHARED_SEI_INT_MSG_MSG_PENDING, 0x1);
	}
}

static void gaudi3_clear_boot_time_interrupts(struct hl_device *hdev)
{
	gaudi3_clear_parc_sei_interrupt(hdev);
}

void gaudi3_pldm_enable_interrupts(struct hl_device *hdev)
{
	if (!hdev->pldm || (hdev->fw_components & FW_TYPE_BOOT_CPU))
		return;

	gaudi3_clear_boot_time_interrupts(hdev);
	gaudi3_enable_interrupt_aggr_msgs(hdev);
	gaudi3_enable_qm_sw_interrupt_msgs(hdev);
}

void gaudi3_pldm_disable_interrupts(struct hl_device *hdev)
{
	if (!hdev->pldm || (hdev->fw_components & FW_TYPE_BOOT_CPU))
		return;

	gaudi3_disable_interrupt_aggr_msgs(hdev);
}

static void gaudi3_handle_psoc_aggr(struct hl_device *hdev, u32 intr_aggr_irq, u32 die,
				    struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 parc_block_idx, offset, sts0, idx, sts0_prstn_mask = GENMASK(1, 0),
			sts0_vm_alarm_mask = GENMASK(3, 0);

	parc_block_idx = intr_aggr_irq - die * INTR_AGGR_NUM_OF_MSIX_VECTORS_PER_DIE -
				CPU_INTR_AGGR_NUM_OF_MSIX_VECTORS;
	offset = die * DIE_OFFSET + parc_block_idx * PARC_INTR_BLOCK_OFFSET;

	sts0 = RREG32(mmD0_PARC_INT_AGGR_UART_COMB_BASE +
			mmINT_AGG_PSOC_UART_COMB_STS + offset);
	/* Clear indication, since we don't know how to clear the actual event yet
	 * driver will log the event once then it'll mask it in aggregator
	 * to avoid getting endless messages.
	 */
	WREG32_OR(mmD0_PARC_INT_AGGR_UART_COMB_BASE +
			mmINT_AGG_PSOC_UART_COMB_MASK + offset, sts0);

	/* Handle PRSTN Aggr */
	if (parc_block_idx == 10) {
		idx = ffs(sts0 & sts0_prstn_mask) - 1;
		dev_err(hdev->dev, "Received PRSTN_SPI[%u] in D%u_PARC_INT_AGGR\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PSOC_EVENT])
			shared_handle_and_clear[SHARED_PSOC_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts0, 0, idx,
				mmD0_PARC_INT_AGGR_UART_COMB_BASE +
				mmINT_AGG_PSOC_UART_COMB_MASK + offset,
				~(sts0 & sts0_prstn_mask), eq_dynamic_entry);

	}

	/* Handle VM_ALARMA_COMB Aggr */
	if (parc_block_idx == 18) {
		idx = ffs(sts0 & sts0_vm_alarm_mask) - 1;
		dev_err(hdev->dev, "Received VM_ALARMA_COMB_SPI[%u] in D%u_PARC_INT_AGGR\n",
			idx, die);
	}

	/* Handle COMBINED Aggr */
	if (parc_block_idx == 24) {
		idx = 0;
		dev_err(hdev->dev, "Received COMBINED_SPI[%u] in D%u_PARC_INT_AGGR\n",
			idx, die);

		if (sts0 & BIT(6))
			dev_err(hdev->dev, "PSOC AXI drain event\n");
	}

	WREG32(mmD0_PARC_INT_AGGR_UART_COMB_BASE +
			mmINT_AGG_PSOC_UART_COMB_STS + offset, sts0);
	WREG32(mmD0_PARC_INT_AGGR_UART_COMB_BASE +
			mmINT_AGG_PSOC_UART_COMB_MSG_PENDING + offset, 1);
}

static void __handle_and_clear_derr_events(struct hl_device *hdev, u32 special_regs_base,
					struct hl_eq_ecc_data *ecc_data, bool *ecc_err_found)
{
	u32 mem, num_of_mem, ecc_err_sts, ecc_address, ecc_syndrom;

	num_of_mem = RREG32(special_regs_base + mmSPECIAL_MEM_NUMOF);

	for (mem = 0 ; mem < num_of_mem ; ++mem) {
		WREG32(special_regs_base + mmSPECIAL_MEM_ECC_SEL, mem);

		ecc_err_sts = RREG32(special_regs_base + mmSPECIAL_MEM_ECC_ERR_STS);
		if (!FIELD_GET(SPECIAL_MEM_ECC_ERR_STS_DERR_M, ecc_err_sts))
			continue;

		/* ECC data is sent only for the first ECC error that is found */
		if (!*ecc_err_found) {
			ecc_address = RREG32(special_regs_base + mmSPECIAL_MEM_ECC_ERR_ADDR);
			ecc_data->ecc_address = cpu_to_le64(ecc_address);
			ecc_syndrom = FIELD_GET(SPECIAL_MEM_ECC_ERR_STS_SYND_M, ecc_err_sts);
			ecc_data->ecc_syndrom = cpu_to_le64(ecc_syndrom);
			ecc_data->memory_wrapper_idx = (u8) mem;
			ecc_data->is_critical = 0;
			ecc_data->block_id = FIELD_GET(ECC_BLOCK_LBW_BASE_ADDR_M,
							special_regs_base);

			*ecc_err_found = true;
		}

		WREG32(special_regs_base + mmVDEC_CTRL_SPECIAL_MEM_ECC_CTL,
				FIELD_PREP(SPECIAL_MEM_ECC_CTL_DERR_CLR_M, 0x1));

		WREG32(special_regs_base + mmSPECIAL_MEM_ECC_ERR_STS,
				FIELD_PREP(SPECIAL_MEM_ECC_ERR_STS_DERR_M, 0x1));
	}
}

static void handle_and_clear_derr_events(struct hl_device *hdev, u32 *special_regs_base_arr,
					u32 arr_size, u32 offset, struct hl_eq_ecc_data *ecc_data)
{
	u32 arr_idx, special_regs_base;
	bool ecc_err_found = false;

	for (arr_idx = 0 ; arr_idx < arr_size ; ++arr_idx) {
		special_regs_base = special_regs_base_arr[arr_idx] + offset;
		__handle_and_clear_derr_events(hdev, special_regs_base, ecc_data, &ecc_err_found);
	}
}

static enum hl_agg_grp_type to_agg_grp_type(enum err_grp type)
{
	switch (type) {
	case ERR_GRP_DERR:
		return INT_GRP_TYPE_DERR;
	case ERR_GRP_SERR:
		return INT_GRP_TYPE_SERR;
	case ERR_GRP_SEI:
		return INT_GRP_TYPE_SEI;
	case ERR_GRP_SPI_ECO:
		return INT_GRP_TYPE_SPI;
	}

	return INT_GRP_TYPE_MAX;
}

static enum hl_agg_hdcore_type to_agg_hdcore_type(u32 hdcore)
{
	if (hdcore >= INT_HDCORE_MAX)
		return INT_HDCORE_MAX;

	return hdcore;
}

static void prepare_eq_dynamic_entry_agg_header(struct hl_eq_dynamic_entry *eq_dynamic_entry,
						struct eq_agg_header_params *params)
{
	eq_dynamic_entry->hdr.ctl = cpu_to_le32(FIELD_PREP(EQ_CTL_EVENT_MODE_MASK, 1));

	eq_dynamic_entry->agg_hdr.int_grp_type = to_agg_grp_type(params->grp_type);
	eq_dynamic_entry->agg_hdr.int_comp_type = params->component_type;
	eq_dynamic_entry->agg_hdr.die_id = (u8) params->die;
	eq_dynamic_entry->agg_hdr.hdcore_type = to_agg_hdcore_type(params->hdcore);
	eq_dynamic_entry->agg_hdr.comp_instance = (u8) params->instance;
}

static u32 cs_special_regs_base[] = {
	mmHD0_CS0_SPECIAL_BASE
};

static void handle_and_clear_cs_sei_events(struct hl_device *hdev, u32 offset,
					   struct hl_eq_cs_sei_data *sei_data)
{
	u32 base = mmHD0_CS0_MAIN_BASE + offset;
	u32 cause, mask, lsb, msb, val;

	cause = RREG32(base + mmCACHE_MAIN_SEI_CAUSE_REG);
	sei_data->cause.intr_cause_data = cpu_to_le64((u64)cause);

	/* process the interrupt */
	if (cause & CACHE_MAIN_SEI_CAUSE_REG_C2M_R_SLV_ERR_ORIG_WRITE_M) {
		lsb = FIELD_GET(CACHE_ERR_SLV_ERR_ADDR_LSB_VALUE_M,
				RREG32(base + mmCACHE_ERR_SLV_ERR_ADDR_LSB));
		msb = FIELD_GET(CACHE_ERR_SLV_ERR_ADDR_MSB_VALUE_M,
				RREG32(base + mmCACHE_ERR_SLV_ERR_ADDR_MSB));
		sei_data->err_data.slv_err_addr = cpu_to_le64(((u64)msb << 32) | lsb);
	}

	mask = (CACHE_MAIN_SEI_CAUSE_REG_POISON_LOCAL_AR_M |
		CACHE_MAIN_SEI_CAUSE_REG_POISON_LOCAL_AWW_M |
		CACHE_MAIN_SEI_CAUSE_REG_POISON_LOCAL_M_WR_M |
		CACHE_MAIN_SEI_CAUSE_REG_POISON_REMOTE_AR_M |
		CACHE_MAIN_SEI_CAUSE_REG_POISON_REMOTE_AWW_M);
	if (cause & mask) {
		val = RREG32(base + mmCACHE_ERR_POISON_INFO_A);

		msb = FIELD_GET(CACHE_ERR_POISON_INFO_A_ID_MSB_M, val);
		lsb = FIELD_GET(CACHE_ERR_POISON_INFO_B_ID_LSB_M,
				RREG32(base + mmCACHE_ERR_POISON_INFO_B));
		sei_data->err_data.poison_data.id = cpu_to_le64(((u64)msb << 32) | lsb);

		msb = FIELD_GET(CACHE_ERR_POISON_INFO_A_ADDR_MSB_M, val);
		lsb = FIELD_GET(CACHE_ERR_POISON_INFO_C_ADDR_LSB_M,
				RREG32(base + mmCACHE_ERR_POISON_INFO_C));
		sei_data->err_data.poison_data.addr = cpu_to_le64(((u64)msb << 32) | lsb);

		WREG32((base + mmCACHE_ERR_POISON_INFO_CLEAR), (cause & mask));
	}

	if (cause & CACHE_MAIN_SEI_CAUSE_REG_FAR_HOST_REDUC_NUM_ERR_M) {
		val = RREG32(offset + mmCACHE_ERR_NUM_ERR_FAR_HOST_A);

		sei_data->err_data.far_data.num_err =
				FIELD_GET(CACHE_ERR_NUM_ERR_FAR_HOST_A_NUM_ERR_M, val);

		msb = FIELD_GET(CACHE_ERR_NUM_ERR_FAR_HOST_B_USER_LSB_M, val);
		lsb = FIELD_GET(CACHE_ERR_NUM_ERR_FAR_HOST_B_USER_LSB_M,
				RREG32(base + mmCACHE_ERR_NUM_ERR_FAR_HOST_B));
		sei_data->err_data.far_data.info = cpu_to_le64(((u64) msb << 32) | lsb);

		msb = FIELD_GET(CACHE_ERR_NUM_ERR_FAR_HOST_A_ADDR_MSB_M, val);
		lsb = FIELD_GET(CACHE_ERR_NUM_ERR_FAR_HOST_C_ADDR_LSB_M,
				RREG32(base + mmCACHE_ERR_NUM_ERR_FAR_HOST_C));
		sei_data->err_data.far_data.addr = cpu_to_le64(((u64)msb << 32) | lsb);

		WREG32((base + mmCACHE_ERR_NUM_ERR_FAR_HOST_CLEAR),
				CACHE_MAIN_SEI_CAUSE_REG_FAR_HOST_REDUC_NUM_ERR_M);
	}

	if (cause & CACHE_MAIN_SEI_CAUSE_REG_CLOSE_HOST_REDUC_NUM_ERR_M) {
		val = RREG32(base + mmCACHE_ERR_NUM_ERR_CLOSE_HOST_A);

		sei_data->err_data.close_data.num_err =
				FIELD_GET(CACHE_ERR_NUM_ERR_CLOSE_HOST_A_NUM_ERR_M, val);
		msb = FIELD_GET(CACHE_ERR_NUM_ERR_CLOSE_HOST_A_USER_MSB_M, val);
		lsb = FIELD_GET(CACHE_ERR_NUM_ERR_CLOSE_HOST_B_USER_LSB_M,
				RREG32(base + mmCACHE_ERR_NUM_ERR_CLOSE_HOST_B));
		sei_data->err_data.close_data.info = cpu_to_le64(((u64) msb << 32) | lsb);

		msb = FIELD_GET(CACHE_ERR_NUM_ERR_CLOSE_HOST_A_ADDR_MSB_M, val);
		lsb = FIELD_GET(CACHE_ERR_NUM_ERR_CLOSE_HOST_C_ADDR_LSB_M,
				RREG32(base + mmCACHE_ERR_NUM_ERR_CLOSE_HOST_C));
		sei_data->err_data.close_data.addr = cpu_to_le64(((u64) msb << 32) | lsb);

		WREG32((base + mmCACHE_ERR_NUM_ERR_CLOSE_HOST_CLEAR),
				CACHE_MAIN_SEI_CAUSE_REG_CLOSE_HOST_REDUC_NUM_ERR_M);
	}

	if (cause & CACHE_MAIN_SEI_CAUSE_REG_AAB_REDUC_NUM_ERR_M) {
		sei_data->err_data.aab_num_err = FIELD_GET(CACHE_ERR_NUM_ERR_AAB_A_NUM_ERR_M,
							   RREG32(mmCACHE_ERR_NUM_ERR_AAB_A));
		WREG32((base + mmCACHE_ERR_NUM_ERR_AAB_CLEAR),
				CACHE_MAIN_SEI_CAUSE_REG_AAB_REDUC_NUM_ERR_M);
	}

	if (cause & CACHE_MAIN_SEI_CAUSE_REG_DN_CONV_NUM_ERR_M) {
		lsb = FIELD_GET(CACHE_ERR_DN_CONV_INFO_A_ID_LSB_M,
				RREG32(base + mmCACHE_ERR_DN_CONV_INFO_A));
		msb = FIELD_GET(CACHE_ERR_DN_CONV_INFO_B_ID_MSB_M,
				RREG32(base + mmCACHE_ERR_DN_CONV_INFO_B));
		sei_data->err_data.dn_conv_id = cpu_to_le64(((u64) msb << 32) | lsb);

		WREG32((base + mmCACHE_ERR_DN_CONV_INFO_CLEAR),
				CACHE_MAIN_SEI_CAUSE_REG_DN_CONV_NUM_ERR_M);
	}

	/* clear the cause reg */
	WREG32(base + mmCACHE_MAIN_SEI_CAUSE_REG, cause);
}

/* HDCORE_CS_EVENT */
static void handle_and_clear_cs_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	bool unmask_event_in_aggr = false, need_clear = false;
	u32 offset, intr_cause_data, intr_cause_reg;
	struct eq_agg_header_params params = {};
	int rc;

	offset = (die * NUM_OF_HDCORES_PER_DIE + hdcore) * HDCORE_OFFSET + instance * CSLICE_OFFSET;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		handle_and_clear_derr_events(hdev, cs_special_regs_base,
						ARRAY_SIZE(cs_special_regs_base), offset,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SEI:
		handle_and_clear_cs_sei_events(hdev, offset, &eq_dynamic_entry->cs_sei_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_cs_sei_data));
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SPI_ECO:
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_intr_cause));
		intr_cause_reg = mmHD0_CS0_MAIN_BASE + offset + mmCACHE_MAIN_SPI_CAUSE_REG;
		intr_cause_data = RREG32(intr_cause_reg);
		eq_dynamic_entry->intr_cause.intr_cause_data = cpu_to_le64((u64)intr_cause_data);
		need_clear = unmask_event_in_aggr = true;
		break;
	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_CS;
	params.grp_type = type;
	params.die = die;
	params.hdcore = hdcore;
	params.instance = instance;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	if (need_clear)
		WREG32(intr_cause_reg, intr_cause_data);

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static u32 mc_special_regs_base[] = {
	mmD0_HBM0_MC0_SPECIAL_BASE,
	mmD0_HBM0_MC1_SPECIAL_BASE,
	mmD0_HBM0_CMN01_SPECIAL_BASE
};

/* HDCORE_HBM_EVENT */
static void handle_and_clear_hbm_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	struct eq_agg_header_params params = {};
	bool unmask_event_in_aggr = false;
	u32 offset;
	int rc;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		/* Per HBM, 4 DERR interrupts for MC_CH0/1, MC_CH2/3, MC_CH4/5 and MC_CH6/7 */
		offset = die * DIE_OFFSET + hdcore * HBM_DEV_OFFSET +
				(idx % 4) * (2 * HBM_MC_OFFSET);
		handle_and_clear_derr_events(hdev, mc_special_regs_base,
						ARRAY_SIZE(mc_special_regs_base), offset,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SEI:
		break;
	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_MC;
	params.grp_type = type;
	params.die = die;
	params.hdcore = hdcore;
	params.instance = 0;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static u32 sob_special_regs_base[] = {
	mmHD0_SYNC_MNGR_GLBL_SPECIAL_BASE,
};

/* HDCORE_SOB_EVENT */
static void handle_and_clear_sob_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	struct hl_eq_sob_sei_data *sob_sei_data = &eq_dynamic_entry->sob_sei_data;
	struct eq_agg_header_params params = {};
	bool unmask_event_in_aggr = false;
	u32 offset, base, sei_cause, cq_intr_val;
	int rc;

	offset = (die * NUM_OF_HDCORES_PER_DIE + hdcore) * HDCORE_OFFSET;
	switch (type) {
	case ERR_GRP_DERR:
		handle_and_clear_derr_events(hdev, sob_special_regs_base,
						ARRAY_SIZE(sob_special_regs_base), offset,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SEI:
		base = mmHD0_SYNC_MNGR_GLBL_BASE + offset;
		sei_cause = RREG32(base + mmSOB_GLBL_SM_SEI_CAUSE);

		sob_sei_data->intr_cause.intr_cause_data = cpu_to_le64(sei_cause);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_sob_sei_data));

		if (sei_cause == 0) {
			sob_sei_data->cq_data.cq_intr = 1;
			cq_intr_val = RREG32(base + mmSOB_GLBL_CQ_INTR);
			sob_sei_data->cq_data.cq_intr_queue_idx =
				FIELD_GET(SOB_GLBL_CQ_INTR_CQ_INTR_QUEUE_INDEX_M, cq_intr_val);
			WREG32(base + mmSOB_GLBL_CQ_INTR, 0x0);
		}

		/* Clear interrupt (W0C) */
		WREG32(base + mmSOB_GLBL_SM_SEI_CAUSE, 0x0);
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SPI_ECO:
		/* SPI interrupt is only for trace/debug so don't pass it to EQ handler */
		return;
	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_SOB;
	params.grp_type = type;
	params.die = die;
	params.hdcore = hdcore;
	params.instance = instance;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static void gaudi3_print_pcie_err_resp_data(struct hl_device *hdev, bool hbw, bool read)
{
	u64 addr_hi, addr_lo;

	if (hbw) {
		if (read) {
			addr_lo = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_RD_ERR_ADDR_0);
			addr_hi = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_RD_ERR_ADDR_1);
		} else {
			addr_lo = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_WR_ERR_ADDR_0);
			addr_hi = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_WR_ERR_ADDR_1);
		}
	} else {
		if (read) {
			addr_lo = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_RD_ERR_ADDR_0);
			addr_hi = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_RD_ERR_ADDR_1);
		} else {
			addr_lo = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_WR_ERR_ADDR_0);
			addr_hi = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_WR_ERR_ADDR_1);
		}
	}

	dev_dbg(hdev->dev, "PCIE error response 0x%llX\n", (addr_hi << 32) + addr_lo);
}

static void gaudi3_clear_xresp_block(struct hl_device *hdev, u64 base, bool is_read)
{
	u32 val;

	val = RREG32(base + mmMSTR_IF_XRESP_LBW_INTR_CTRL_CLEAR);
	WREG32(base + mmMSTR_IF_XRESP_LBW_INTR_CTRL_CLEAR, val);
}

static void gaudi3_clear_pcie_sei_cause_events(struct hl_device *hdev, u32 err_msk)
{
	u32 idx = 0;

	while (err_msk) {
		if (err_msk & 1)
			switch (idx) {
			case PCIE_WRAP_PCIE_SEI_INTR_STATUS_ALL_AXI_SPLIT_S:
				/* Clear on read */
				RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_AXI_SPLIT_STATUS);
				WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_AXI_SPLIT, 0);
				break;

			case PCIE_WRAP_PCIE_SEI_INTR_STATUS_LBW_MSTRIF_LBW_WR_RSP_ERR_S:
				gaudi3_print_pcie_err_resp_data(hdev, false, false);
				gaudi3_clear_xresp_block(hdev,
							mmD0_PCIE_LBW_RR_MSTR_IF_XRESP_LBW_BASE,
							false);
				break;
			case PCIE_WRAP_PCIE_SEI_INTR_STATUS_MST_MSTRIF_LBW_RD_RSP_ERR_S:
				gaudi3_print_pcie_err_resp_data(hdev, false, true);
				gaudi3_clear_xresp_block(hdev,
							mmD0_PCIE_MSTR_RR_MSTR_IF_XRESP_LBW_BASE,
							true);
				break;

			case PCIE_WRAP_PCIE_SEI_INTR_STATUS_MST_MSTRIF_LBW_WR_RSP_ERR_S:
				gaudi3_print_pcie_err_resp_data(hdev, false, false);
				gaudi3_clear_xresp_block(hdev,
							mmD0_PCIE_MSTR_RR_MSTR_IF_XRESP_LBW_BASE,
							false);
				break;

			case PCIE_WRAP_PCIE_SEI_INTR_STATUS_MST_MSTRIF_HBW_RD_RSP_ERR_S:
				gaudi3_print_pcie_err_resp_data(hdev, true, true);
				gaudi3_clear_xresp_block(hdev,
							mmD0_PCIE_MSTR_RR_MSTR_IF_XRESP_HBW_BASE,
							true);
				break;

			case PCIE_WRAP_PCIE_SEI_INTR_STATUS_MST_MSTRIF_HBW_WR_RSP_ERR_S:
				gaudi3_print_pcie_err_resp_data(hdev, true, false);
				gaudi3_clear_xresp_block(hdev,
							mmD0_PCIE_MSTR_RR_MSTR_IF_XRESP_HBW_BASE,
							false);
				break;

			case PCIE_WRAP_PCIE_SEI_INTR_STATUS_AXI_LBW_ERR_S:
			case PCIE_WRAP_PCIE_SEI_INTR_STATUS_AXI_ERR_S:
				/* Nothing to clear */
				break;

			default:
				dev_err(hdev->dev, "SEI event %u does not have event clear flow\n",
						idx);
				break;
			}
		idx++;
		err_msk >>= 1;
	}

	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_AXI_INTR, 1);
}

static void handle_and_clear_pcie_drain_event(struct hl_device *hdev,
						struct hl_eq_pcie_drain_ind_data *drain_cause)
{
	u64 addr_lo, addr_hi;
	u32 axi_drain_ind;

	axi_drain_ind = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_AXI_DRAIN_IND);
	drain_cause->intr_cause.intr_cause_data = cpu_to_le64(axi_drain_ind);

	if (axi_drain_ind & (PCIE_WRAP_AXI_DRAIN_IND_LBW_AXI_DRAIN_IND_M |
				PCIE_WRAP_AXI_DRAIN_IND_LBW_AXI_DRAIN_BP_IND_M)) {
		addr_lo = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_DRAIN_WR_ADDR_0);
		addr_hi = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_DRAIN_WR_ADDR_1);
		drain_cause->drain_wr_addr_lbw = cpu_to_le64((addr_hi << 32) | addr_lo);

		addr_lo = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_DRAIN_RD_ADDR_0);
		addr_hi = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_LBW_DRAIN_RD_ADDR_1);
		drain_cause->drain_rd_addr_lbw = cpu_to_le64((addr_hi << 32) | addr_lo);
	}

	if (axi_drain_ind & (PCIE_WRAP_AXI_DRAIN_IND_HBW_AXI_DRAIN_IND_M |
				PCIE_WRAP_AXI_DRAIN_IND_HBW_AXI_DRAIN_BP_IND_M)) {
		addr_lo = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_DRAIN_WR_ADDR_0);
		addr_hi = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_DRAIN_WR_ADDR_1);
		drain_cause->drain_wr_addr_hbw = cpu_to_le64((addr_hi << 32) | addr_lo);

		addr_lo = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_DRAIN_RD_ADDR_0);
		addr_hi = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_HBW_DRAIN_RD_ADDR_1);
		drain_cause->drain_rd_addr_hbw = cpu_to_le64((addr_hi << 32) | addr_lo);
	}

	/* Clear interrupt */
	WREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_AXI_DRAIN_IND, 0x0);
}

static u32 pcie_special_regs_base[] = {
	mmD0_PCIE_CORE_SPECIAL_BASE,
	mmD0_PCIE_WRAP_SPECIAL_BASE,
	mmD0_PCIE_PHY_SPECIAL_BASE
};

/* SHARED_PCIE_EVENT */
static void handle_and_clear_pcie_events(struct hl_device *hdev, u32 die,
						enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
						u32 aggr_mask_reg, u32 events_mask,
						struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 intr_cause = 0x0, err_idx = 0, err_msk;
	struct eq_agg_header_params params = {};
	bool unmask_event_in_aggr = false;
	int rc;

	if (die == 1) {
		dev_err(hdev->dev, "PCIE events from DIE1 are not supported\n");
		return;
	}

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		handle_and_clear_derr_events(hdev, &pcie_special_regs_base[idx], 1, 0x0,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SEI:
		eq_dynamic_entry->pcie_sei_data.sei_type = PCIE_SEI_AXI_RESP_ERR;

		/* Clear on read */
		intr_cause = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_PCIE_SEI_INTR_STATUS);
		eq_dynamic_entry->pcie_sei_data.intr_cause.intr_cause_data =
								cpu_to_le64(intr_cause);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_pcie_sei_data));
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SPI_ECO:
		if (idx == 0)
			eq_dynamic_entry->pcie_spi_data.spi_type = PCIE_SPI_FLR;
		else if (idx == 1)
			eq_dynamic_entry->pcie_spi_data.spi_type = PCIE_SPI_APB_ACCESS_TIMEOUT;
		else if (2 >= idx && idx <= 5)
			eq_dynamic_entry->pcie_spi_data.spi_type = PCIE_SPI_BMON_SPMU;
		else if (idx == 6)
			eq_dynamic_entry->pcie_spi_data.spi_type = PCIE_SPI_FATAL_ERR;
		else if (idx == 7)
			eq_dynamic_entry->pcie_spi_data.spi_type = PCIE_SPI_P2P_OR_MSIX_GW_INTR;
		else /* 8 */
			eq_dynamic_entry->pcie_spi_data.spi_type = PCIE_SPI_DRAIN;

		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_pcie_spi_data));

		if (eq_dynamic_entry->pcie_spi_data.spi_type == PCIE_SPI_DRAIN) {
			handle_and_clear_pcie_drain_event(hdev,
						&eq_dynamic_entry->pcie_spi_data.drain_cause);
			unmask_event_in_aggr = true;
		}
		break;
	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_PCIE;
	params.grp_type = type;
	params.die = die;
	params.hdcore = INT_SHARED;
	params.instance = 0;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	if (type == ERR_GRP_SEI &&
			eq_dynamic_entry->pcie_sei_data.sei_type == PCIE_SEI_AXI_RESP_ERR) {
		/* PCIE SEI cause register may not be fully cleared. It is possible that we need to
		 * clear other registers and clear cause register once again.
		 * Note!! there might be a corner case which is not handled:
		 * Lets say reg value before eqe is 0x4001. After eqe we clear other registers which
		 * relate for those error, and we read the cause reg again to clear it, but when we
		 * read it another cause is added and now reg value is 0x4004001. This will clear
		 * the additional event without handling it.
		 */
		gaudi3_clear_pcie_sei_cause_events(hdev, intr_cause);

		/* Clearing SEI cause register once again */
		err_msk = RREG32(mmD0_PCIE_WRAP_BASE + mmPCIE_WRAP_PCIE_SEI_INTR_STATUS);

		while (err_msk) {
			/* In case new event raised */
			if ((err_msk & 1) && !(intr_cause & 1))
				dev_err(hdev->dev,
					"PCIE SEI event %u raised after PCIE SEI handling\n",
					err_idx);
			err_idx++;
			err_msk >>= 1;
			intr_cause >>= 1;
		}
	}

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

/* SHARED_PSOC_EVENT */
static void handle_and_clear_psoc_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	struct eq_agg_header_params params = {};
	struct hl_eq_psoc_sei_data *data;
	u32 base = mmD0_PSOC_GLOBAL_CONF2_BASE + die * DIE_OFFSET, intr_cause;
	bool unmask_event_in_aggr = false;
	int rc;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_SEI:
		intr_cause = RREG32(base + mmPSOC_GLOBAL_CONF2_SEI_INTR_CTRL_CAUSE);
		WREG32(base + mmPSOC_GLOBAL_CONF2_SEI_INTR_CTRL_CLEAR, intr_cause);

		data = &eq_dynamic_entry->psoc_sei_data;
		data->intr_cause.intr_cause_data = cpu_to_le64((u64)intr_cause);
		unmask_event_in_aggr = true;
		break;
	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_PSOC;
	params.grp_type = type;
	params.die = die;
	params.hdcore = INT_PSOC;
	params.instance = 0;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static u32 pmmu_special_regs_base[] = {
	mmD0_PMMU_HBW_MMU_SPECIAL_BASE,
	mmD0_PMMU_HBW_STLB_SPECIAL_BASE
};

static void handle_pmmu_spi_events(struct hl_device *hdev, u32 die,
		struct hl_eq_pmmu_spi_data *spi_data)
{
	u32 valid, err_type, mmu_spi_status, offset = die * DIE_OFFSET,
		acc_err_m = MMU_ACCESS_PAGE_ERROR_VALID_ACCESS_ERR_VALID_ENTRY_M,
		acc_err_spi_sts_m = MMU_SPI_STATUS_I2_M,
		page_fault_err_m = MMU_ACCESS_PAGE_ERROR_VALID_PAGE_ERR_VALID_ENTRY_M,
		page_fault_spi_sts_m = MMU_SPI_STATUS_I0_M | MMU_SPI_STATUS_I1_M;
	u64 axi_id1, axi_id2, lsb_va, msb_va;

	mmu_spi_status = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_SPI_STATUS + offset);
	mmu_spi_status &= (acc_err_spi_sts_m | page_fault_spi_sts_m);
	if (!mmu_spi_status)
		return;
	spi_data->intr_cause.intr_cause_data = cpu_to_le64(mmu_spi_status);

	err_type = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_ACCESS_PAGE_ERROR_VALID + offset);
	err_type &= (acc_err_m | page_fault_err_m);

	if ((mmu_spi_status & page_fault_spi_sts_m) || (err_type & page_fault_err_m)) {
		lsb_va = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_PAGE_ERROR_CAPTURE_VA + offset);
		msb_va = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_PAGE_ERROR_CAPTURE + offset);
		spi_data->err_data[PMMU_ERR_TYPE_PAGE_ERR].va =
			cpu_to_le64((msb_va << 32) | lsb_va);

		axi_id1 = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_PAGE_FAULT_ID_LSB + offset);
		axi_id2 = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_PAGE_FAULT_ID_MSB + offset);
		spi_data->err_data[PMMU_ERR_TYPE_PAGE_ERR].axid =
			cpu_to_le64((axi_id2 << 32) | axi_id1);
	}
	if ((mmu_spi_status & acc_err_spi_sts_m) || (err_type & acc_err_m)) {
		lsb_va = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_ACCESS_ERROR_CAPTURE_VA + offset);
		msb_va = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_ACCESS_ERROR_CAPTURE + offset);
		spi_data->err_data[PMMU_ERR_TYPE_ACCESS_ERR].va =
			cpu_to_le64((msb_va << 32) | lsb_va);

		axi_id1 = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_PAGE_ACCESS_ID_LSB + offset);
		axi_id2 = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_PAGE_ACCESS_ID_MSB + offset);
		spi_data->err_data[PMMU_ERR_TYPE_ACCESS_ERR].axid =
			cpu_to_le64((axi_id2 << 32) | axi_id1);
	}

	/* we have a loop since we might have several errors and we want to clear all of them */
	while (mmu_spi_status || err_type) {
		if ((mmu_spi_status & page_fault_spi_sts_m) || (err_type & page_fault_err_m)) {
			valid = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_ACCESS_PAGE_ERROR_VALID +
					offset);
			valid = valid & ~MMU_ACCESS_PAGE_ERROR_VALID_PAGE_ERR_VALID_ENTRY_M;
			/* Clear VALID_BIT; */
			WREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_ACCESS_PAGE_ERROR_VALID + offset,
					valid);

			WREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_SPI_STATUS + offset,
					page_fault_spi_sts_m);
		}
		if ((mmu_spi_status & acc_err_spi_sts_m) || (err_type & acc_err_m)) {
			valid = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_ACCESS_PAGE_ERROR_VALID +
					offset);
			valid = valid & ~MMU_ACCESS_PAGE_ERROR_VALID_ACCESS_ERR_VALID_ENTRY_M;
			/* Clear VALID_BIT; */
			WREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_ACCESS_PAGE_ERROR_VALID + offset,
					valid);

			WREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_SPI_STATUS + offset,
					acc_err_spi_sts_m);
		}
		err_type = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_ACCESS_PAGE_ERROR_VALID + offset);
		err_type &= (acc_err_m | page_fault_err_m);
		mmu_spi_status = RREG32(mmD0_PMMU_HBW_MMU_BASE + mmMMU_SPI_STATUS + offset);
		mmu_spi_status &= (acc_err_spi_sts_m | page_fault_spi_sts_m);
	}
}

/* SHARED_PMMU_EVENT */
static void handle_and_clear_pmmu_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	struct hl_eq_pmmu_spi_data *spi_data = &eq_dynamic_entry->pmmu_spi_data;
	struct eq_agg_header_params params = {};
	bool unmask_event_in_aggr = false;
	u32 offset;
	int rc;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		offset = die * DIE_OFFSET;
		handle_and_clear_derr_events(hdev, pmmu_special_regs_base,
						ARRAY_SIZE(pmmu_special_regs_base), offset,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SEI:
		break;
	case ERR_GRP_SPI_ECO:
		handle_pmmu_spi_events(hdev, die, spi_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_pmmu_spi_data));
		unmask_event_in_aggr = true;
		break;
	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_PMMU;
	params.grp_type = type;
	params.die = die;
	params.hdcore = INT_SHARED;
	params.instance = 0;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static void handle_and_clear_qman_interrupts(struct hl_device *hdev, u32 qm_reg_base,
				u32 qm_arc_aux_reg_base, struct hl_eq_qm_sei_data *qm_sei_data)
{
	u32 sei_status, glbl_err_sts, arc_sei_intr_sts;

	sei_status = RREG32(qm_reg_base + mmQMAN_SEI_STATUS);

	if (sei_status & QMAN_SEI_STATUS_QM_INT_M) {
		glbl_err_sts = RREG32(qm_reg_base + mmQMAN_GLBL_ERR_STS) & QMAN_GLBL_ERR_STS_MASK;
		qm_sei_data->qm_cause.intr_cause_data = cpu_to_le64(glbl_err_sts);
	}

	if (sei_status & QMAN_SEI_STATUS_ARC_INT_M) {
		arc_sei_intr_sts = RREG32(qm_arc_aux_reg_base + mmQMAN_ARC_AUX_ARC_SEI_INTR_STS);
		qm_sei_data->arc_qm_cause.intr_cause_data = cpu_to_le64(arc_sei_intr_sts);
	}

	WREG32(qm_reg_base + mmQMAN_SEI_STATUS, sei_status);
}

static u32 tpc_special_regs_base[] = {
	mmHD0_TPC0_QM_ARC_AUX_SPECIAL_BASE,
	mmHD0_TPC0_CFG_SPECIAL_BASE
};

/* HDCORE_TPC_EVENT */
static void handle_and_clear_tpc_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 offset, th_offset, cfg_base, smt_base, mask, smt_mask;
	struct eq_agg_header_params params = {};
	struct hl_eq_tpc_sei_data *sei_data;
	bool unmask_event_in_aggr = false;
	struct hl_eq_tpc_data *tpc_data;
	u64 intr_cause;
	int th, rc;

	offset = (die * NUM_OF_HDCORES_PER_DIE + hdcore) * HDCORE_OFFSET +
			instance * HDCORE_TPC_OFFSET;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		handle_and_clear_derr_events(hdev, tpc_special_regs_base,
						ARRAY_SIZE(tpc_special_regs_base), offset,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;

	case ERR_GRP_SEI:
	case ERR_GRP_SPI_ECO:
		if (type == ERR_GRP_SEI) {
			mask = TPC_SEI_INTR_MASK;
			smt_mask = TPC_SMT_TH_SEI_INTR_MASK;
			tpc_data = &eq_dynamic_entry->tpc_sei_data.data;
			sei_data = &eq_dynamic_entry->tpc_sei_data;
			eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_tpc_sei_data));
		} else {
			mask = TPC_SPI_INTR_MASK;
			smt_mask = TPC_SMT_TH_SPI_INTR_MASK;
			tpc_data = &eq_dynamic_entry->tpc_spi_data.data;
			eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_tpc_spi_data));
		}

		cfg_base = mmHD0_TPC0_CFG_BASE + offset;
		intr_cause = RREG32(cfg_base + mmTPC_TPC_INTR_CAUSE_0);
		tpc_data->intr_cause.intr_cause_data = cpu_to_le64(mask & intr_cause);
		tpc_data->kernel_id = cpu_to_le16(RREG32(cfg_base + mmTPC_KERNEL_KERNEL_ID));

		/* clear the interrupt by writing 0 */
		WREG32(cfg_base + mmTPC_TPC_INTR_CAUSE_0, 0x0);

		smt_base = mmHD0_TPC0_SMT_TPC_TH0_BASE + offset;
		for (th = 0 ; th < NUM_OF_TPC_THREADS ; th++) {
			th_offset = th * TPC_THREAD_OFFSET;
			*(&tpc_data->smt_th0_cause + th) =
				cpu_to_le32(smt_mask & RREG32(smt_base +
					th_offset + mmTPC_SMT_TPC_TH0_INTR_CAUSE_0));

			/* in order to clear the interrupt we write 0
			 * (as opposed to most cases where we write 1 to clear)
			 */
			WREG32(smt_base + th_offset + mmTPC_SMT_TPC_TH0_INTR_CAUSE_0, 0x0);
		}

		if (type == ERR_GRP_SEI)
			handle_and_clear_qman_interrupts(hdev,
							mmHD0_TPC0_QM_BASE + offset,
							mmHD0_TPC0_QM_ARC_AUX_BASE + offset,
							&sei_data->qm_data);

		unmask_event_in_aggr = true;
		break;

	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_TPC;
	params.grp_type = type;
	params.die = die;
	params.hdcore = hdcore;
	params.instance = instance;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static u32 mme_special_regs_base[] = {
	mmHD0_MME0_SBTE0_SPECIAL_BASE,
	mmHD0_MME0_SBTE1_SPECIAL_BASE,
	mmHD0_MME0_SBTE2_SPECIAL_BASE,
	mmHD0_MME0_SBTE3_SPECIAL_BASE,
	mmHD0_MME1_SBTE0_SPECIAL_BASE,
	mmHD0_MME1_SBTE1_SPECIAL_BASE,
	mmHD0_MME1_SBTE2_SPECIAL_BASE,
	mmHD0_MME1_SBTE3_SPECIAL_BASE,
	mmHD0_MME_QM_ARC_AUX_SPECIAL_BASE,
	mmHD0_MME0_ACC_SPECIAL_BASE,
	mmHD0_MME1_ACC_SPECIAL_BASE
};

static void gaudi3_set_mme_acc_ctx_id_interrupt(struct hl_device *hdev,
		struct hl_eq_mme_acc_data *data, u32 cause, u32 acc_base)
{
	if (cause & (MME_ACC_INTR_WBC_BUSER_SLV_ERR_SET0_CH0_MASK |
			MME_ACC_INTR_WBC_BUSER_NUMERICAL_INF_ERR_SET0_CH0_MASK |
			MME_ACC_INTR_WBC_BUSER_NUMERICAL_NINF_ERR_SET0_CH0_MASK |
			MME_ACC_INTR_WBC_BUSER_NUMERICAL_NAN_ERR_SET0_CH0_MASK |
			MME_ACC_INTR_WBC_BUSER_RR_DBG_ERR_SET0_CH0_MASK)) {
		data->ctx_id[MME_ACC_CTX_ID_CH0_SET0] = cpu_to_le16(RREG32(acc_base +
					mmACC_INTR_WBC_BUSER_CTX_ID_CH0) &
					ACC_INTR_WBC_BUSER_CTX_ID_CH0_SET0_M);
	}

	if (cause & (MME_ACC_INTR_WBC_BUSER_SLV_ERR_SET0_CH1_MASK |
			MME_ACC_INTR_WBC_BUSER_NUMERICAL_INF_ERR_SET0_CH1_MASK |
			MME_ACC_INTR_WBC_BUSER_NUMERICAL_NINF_ERR_SET0_CH1_MASK |
			MME_ACC_INTR_WBC_BUSER_NUMERICAL_NAN_ERR_SET0_CH1_MASK |
			MME_ACC_INTR_WBC_BUSER_RR_DBG_ERR_SET0_CH1_MASK)) {
		data->ctx_id[MME_ACC_CTX_ID_CH1_SET0] = cpu_to_le16(RREG32(acc_base +
					mmACC_INTR_WBC_BUSER_CTX_ID_CH1) &
					ACC_INTR_WBC_BUSER_CTX_ID_CH1_SET0_M);
	}

	if (cause & (MME_ACC_INTR_WBC_BUSER_SLV_ERR_SET1_CH0_MASK |
			MME_ACC_INTR_WBC_BUSER_NUMERICAL_INF_ERR_SET1_CH0_MASK |
			MME_ACC_INTR_WBC_BUSER_NUMERICAL_NINF_ERR_SET1_CH0_MASK |
			MME_ACC_INTR_WBC_BUSER_NUMERICAL_NAN_ERR_SET1_CH0_MASK |
			MME_ACC_INTR_WBC_BUSER_RR_DBG_ERR_SET1_CH0_MASK)) {
		data->ctx_id[MME_ACC_CTX_ID_CH0_SET1] = cpu_to_le16((RREG32(acc_base +
					mmACC_INTR_WBC_BUSER_CTX_ID_CH0) &
					ACC_INTR_WBC_BUSER_CTX_ID_CH0_SET1_M) >>
					ACC_INTR_WBC_BUSER_CTX_ID_CH0_SET1_S);
	}

	if (cause & (MME_ACC_INTR_WBC_BUSER_SLV_ERR_SET1_CH1_MASK |
			MME_ACC_INTR_WBC_BUSER_NUMERICAL_INF_ERR_SET1_CH1_MASK |
			MME_ACC_INTR_WBC_BUSER_NUMERICAL_NINF_ERR_SET1_CH1_MASK |
			MME_ACC_INTR_WBC_BUSER_NUMERICAL_NAN_ERR_SET1_CH1_MASK |
			MME_ACC_INTR_WBC_BUSER_RR_DBG_ERR_SET1_CH1_MASK)) {
		data->ctx_id[MME_ACC_CTX_ID_CH1_SET1] = cpu_to_le16((RREG32(acc_base +
					mmACC_INTR_WBC_BUSER_CTX_ID_CH1) &
					ACC_INTR_WBC_BUSER_CTX_ID_CH1_SET1_M) >>
					ACC_INTR_WBC_BUSER_CTX_ID_CH1_SET1_S);
	}
}

static void handle_and_clear_mme_sei_events(struct hl_device *hdev, u32 idx, u32 offset,
					struct hl_eq_mme_sei_data *sei_data)
{
	u32 mme_id, sbte_id, ctrl_lo_base, acc_base, sbte_base, cause, mask;

	switch (idx) {
	case SEI_INTR_MME0_SBTE0:
	case SEI_INTR_MME0_SBTE1:
	case SEI_INTR_MME0_SBTE2:
	case SEI_INTR_MME0_SBTE3:
	case SEI_INTR_MME1_SBTE0:
	case SEI_INTR_MME1_SBTE1:
	case SEI_INTR_MME1_SBTE2:
	case SEI_INTR_MME1_SBTE3:
		mme_id = idx / 4;
		sbte_id = idx % 4;
		sbte_base = mmHD0_MME0_SBTE0_BASE +
			mme_id * HDCORE_MME_SBTE_GRP_OFFSET +
			sbte_id * HDCORE_MME_SBTE_OFFSET + offset;

		cause = RREG32(sbte_base + mmSB_INTR_CAUSE);

		sei_data->sbte_data.cause.intr_cause_data =
			cpu_to_le64(cause & MME_SBTE_SEI_INTR_MASK);
		sei_data->sbte_data.mme_eu_id = mme_id;
		sei_data->sbte_data.sbte_id = sbte_id;
		sei_data->sbte_data.ctx_id =
			cpu_to_le16((cause & SB_INTR_CAUSE_CONTEXT_ID_M) >>
						SB_INTR_CAUSE_CONTEXT_ID_S);
		sei_data->type = MME_DATA_TYPE_SBTE;

		WREG32(sbte_base + mmSB_INTR_CLEAR, cause & MME_SBTE_SEI_INTR_MASK);
		break;
	case SEI_INTR_MME0_ACC:
	case SEI_INTR_MME1_ACC:
		mme_id = idx - SEI_INTR_MME0_ACC;
		acc_base = mmHD0_MME0_ACC_BASE + HDCORE_MME_EU_OFFSET * mme_id + offset;

		cause = RREG32(acc_base + mmACC_INTR_CAUSE) & MME_ACC_SEI_INTR_MASK;

		gaudi3_set_mme_acc_ctx_id_interrupt(hdev, &sei_data->acc_data, cause,
							acc_base);
		sei_data->acc_data.intr_cause.intr_cause_data = cpu_to_le64(cause);
		sei_data->acc_data.id = mme_id;
		sei_data->type = MME_DATA_TYPE_ACC;
		WREG32(acc_base + mmACC_INTR_CLEAR, cause);
		break;
	case SEI_INTR_MME_CTRL0_LO0:
	case SEI_INTR_MME_CTRL0_LO1:
		if (idx == SEI_INTR_MME_CTRL0_LO0)
			mask = MME_CTRL_SEI0_INTR_MASK;
		else
			mask = MME_CTRL_SEI1_INTR_MASK;

		ctrl_lo_base = mmHD0_MME_CTRL_LO_BASE;

		cause = RREG32(ctrl_lo_base + mmMME_CTRL_LO_INTR_CAUSE + offset);
		sei_data->control_data.cause.intr_cause_data = cpu_to_le64(cause & mask);
		sei_data->type = MME_DATA_TYPE_CTRL;

		if ((cause & MME_CTRL_SEI0_QM_INTR_MASK) ||
		    (cause & MME_CTRL_SEI1_QM_INTR_MASK))
			handle_and_clear_qman_interrupts(hdev,
						mmHD0_MME_QM_BASE + offset,
						mmHD0_MME_QM_ARC_AUX_BASE + offset,
						&sei_data->control_data.qm_data);

		WREG32(ctrl_lo_base + mmMME_CTRL_LO_INTR_CLEAR + offset, (cause & mask));
		break;
	default:
		break;
	}
}

static void process_spmu_bmon_spi_interrupt(struct hl_device *hdev,
						struct hl_eq_spmu_bmon *spmu_bmon_data,
					u32 spmu_base, u32 bmon_base, u32 num_bmon)
{
	u32 cause;
	int i;

	if (spmu_base) {
		cause = RREG32(spmu_base + mmCS_DBG_W_SPMU_4_BMON_SPMU_PMINTENSET_EL1);
		spmu_bmon_data->cause[CS_DBG_SPMU] = cpu_to_le32(cause);
		if (cause)
			WREG32(spmu_base + mmCS_DBG_W_SPMU_4_BMON_SPMU_PMINTENCLR_EL1, cause);
	}

	for (i = CS_DBG_BMON0; i < CS_DBG_BMON0 + num_bmon; i++) {
		cause = RREG32(bmon_base + mmCS_DBG_W_SPMU_4_BMON_BMON0_TRIG_TH);
		spmu_bmon_data->cause[i] = cpu_to_le32(cause);
		if (cause)
			WREG32(bmon_base + mmCS_DBG_W_SPMU_4_BMON_BMON0_INT_CLR, cause);
		bmon_base += BMON_BASE_OFFSET;
	}
}

/* HDCORE_MME_EVENT */
static void handle_and_clear_mme_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 offset, cause, mme_id, acc_base, spmu_base, bmon_base;
	struct eq_agg_header_params params = {};
	struct hl_eq_mme_spi_data *spi_data;
	bool unmask_event_in_aggr = false;
	int rc;

	offset = (die * NUM_OF_HDCORES_PER_DIE + hdcore) * HDCORE_OFFSET;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		handle_and_clear_derr_events(hdev, &mme_special_regs_base[idx], 1, offset,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SEI:
		handle_and_clear_mme_sei_events(hdev, idx, offset, &eq_dynamic_entry->mme_sei_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_mme_sei_data));
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SPI_ECO:
		spi_data = &eq_dynamic_entry->mme_spi_data;
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_mme_spi_data));
		/* the driver only cares about interrupts 12-14, which are mapped to 0-2 in 'idx' */
		switch (idx + 12) {
		case SPI_INTR_MME_QM_CS:
			spmu_base = mmHD0_MME_QMAN_CS_DBG_SPMU_BASE + offset;
			bmon_base = mmHD0_MME_QMAN_CS_DBG_BMON0_BASE + offset;
			spi_data->spmu_bmon_data.comp_sub_type = CS_DBG_MME_QM;
			spi_data->type = MME_DATA_TYPE_CS_DBG;
			process_spmu_bmon_spi_interrupt(hdev, &spi_data->spmu_bmon_data.data,
							spmu_base, bmon_base, NUM_OF_BMONS);
			break;
		case SPI_INTR_MME0_ACC:
		case SPI_INTR_MME1_ACC:
			if (idx + 12 == SPI_INTR_MME0_ACC)
				mme_id = 0;
			else
				mme_id = 1;

			acc_base = mmHD0_MME0_ACC_BASE + HDCORE_MME_EU_OFFSET * mme_id + offset;
			cause = RREG32(acc_base + mmACC_INTR_CAUSE) & MME_ACC_SPI_INTR_MASK;

			spi_data->type = MME_DATA_TYPE_ACC;
			gaudi3_set_mme_acc_ctx_id_interrupt(hdev, &spi_data->acc_data, cause,
								acc_base);
			spi_data->acc_data.intr_cause.intr_cause_data = cpu_to_le64(cause);
			spi_data->acc_data.id = mme_id;

			WREG32(acc_base + mmACC_INTR_CLEAR, cause);
			break;
		default:
			return;
		}
		unmask_event_in_aggr = true;
		break;
	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_MME;
	params.grp_type = type;
	params.die = die;
	params.hdcore = hdcore;
	params.instance = 0;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static u32 rotator_special_regs_base[] = {
	mmHD1_ROT0_BASE
};

static void __get_rotator_err_ctx_id(struct hl_device *hdev, u32 rotator_offset,
					struct hl_eq_rot_spi_data *rot_spi_data,
					enum gaudi3_rotator_err_id err_id)
{
	u32 reg_offset, reg_val, shift, mask, ctx_id;

	WREG32(mmHD1_ROT0_BASE + rotator_offset + mmROTATOR_DBG_CONTEXT_ID_CONTROL, err_id);

	reg_offset = gaudi3_rotator_err_ctx_id_regs[err_id].offset;
	reg_val = RREG32(mmHD1_ROT0_BASE + rotator_offset + reg_offset);

	shift = gaudi3_rotator_err_ctx_id_regs[err_id].shift;
	mask = gaudi3_rotator_err_ctx_id_regs[err_id].mask;
	ctx_id = (reg_val & mask) >> shift;
	rot_spi_data->data.ctx_id[err_id] = cpu_to_le16(ctx_id);
}

static void get_rotator_err_ctx_id(struct hl_device *hdev, u32 rotator_offset,
					struct hl_eq_rot_spi_data *rot_spi_data,
					u32 err_status, u32 err_id_offset)
{
	u32 idx = 0;

	while (err_status) {
		if (err_status & 0x1)
			__get_rotator_err_ctx_id(hdev, rotator_offset, rot_spi_data,
							idx + err_id_offset);
		++idx;
		err_status >>= 1;
	}
}

/* HDCORE_ROT_EVENT */
static void handle_and_clear_rotator_events(struct hl_device *hdev, u32 die, u32 hdcore,
					u32 instance, enum err_grp type, u32 sts, u32 sts_idx,
					u32 idx, u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	struct hl_eq_rot_sei_data *rot_sei_data;
	struct hl_eq_rot_spi_data *rot_spi_data;
	struct eq_agg_header_params params = {};
	bool unmask_event_in_aggr = false;
	u32 offset, cause, err_status;
	int rc;

	/* There are rotator blocks only in HD 1/3/4/6 */
	if ((die == 0 && (hdcore == 0 || hdcore == 2)) ||
			(die == 1 && (hdcore == 1 || hdcore == 3)))  {
		dev_err(hdev->dev, "No rotator interrupts are expected for DIE%u_HD%u!\n",
			die, hdcore);
		return;
	}

	/* Subtract 1 from hdcore because the offset is relative to the first rotator in HD1 */
	offset = (die * NUM_OF_HDCORES_PER_DIE + hdcore - 1) * HDCORE_OFFSET +
				HDCORE_ROT_OFFSET * instance;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		handle_and_clear_derr_events(hdev, rotator_special_regs_base,
						ARRAY_SIZE(rotator_special_regs_base), offset,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;

	case ERR_GRP_SEI:
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_rot_sei_data));
		rot_sei_data = &eq_dynamic_entry->rot_sei_data;
		cause = RREG32(mmHD1_ROT0_BASE + offset + mmROTATOR_MSS_SEI_CAUSE) &
				ROTATOR_MSS_SEI_CAUSE_MASK;
		rot_sei_data->cause.intr_cause_data = cpu_to_le64(cause);

		/* Clear interrupt (W1C) */
		WREG32(mmHD1_ROT0_BASE + offset + mmROTATOR_MSS_SEI_CLEAR, cause);

		handle_and_clear_qman_interrupts(hdev, mmHD1_ROT0_QM_BASE + offset,
							mmHD1_ROT0_QM_ARC_AUX_BASE + offset,
							&rot_sei_data->qm_data);

		unmask_event_in_aggr = true;
		break;

	case ERR_GRP_SPI_ECO:
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_rot_spi_data));
		rot_spi_data = &eq_dynamic_entry->rot_spi_data;
		cause = RREG32(mmHD1_ROT0_BASE + offset + mmROTATOR_MSS_SPI_CAUSE) &
				ROTATOR_MSS_SPI_CAUSE_MASK;
		rot_spi_data->data.intr_cause.intr_cause_data = cpu_to_le64(cause);

		if (cause & ROTATOR_MSS_SPI_CAUSE_IP_NUM_MASK) {
			err_status =
				RREG32(mmHD1_ROT0_BASE + offset + mmROTATOR_IP_NUM_ERR_STATUS) &
				ROTATOR_IP_NUM_ERR_STATUS_MASK;
			rot_spi_data->data.ip_num_cause = cpu_to_le32(err_status);
			get_rotator_err_ctx_id(hdev, offset, rot_spi_data, err_status,
						RINTERP_PINF_ERROR);

			/* Clear interrupt (W1C) */
			WREG32(mmHD1_ROT0_BASE + offset + mmROTATOR_IP_NUM_ERR_STATUS, err_status);
		}

		if (cause & ROTATOR_MSS_SPI_CAUSE_RSB_MASK) {
			err_status = RREG32(mmHD1_ROT0_BASE + offset + mmROTATOR_RSB_ERR_STATUS) &
					ROTATOR_RSB_ERR_STATUS_MASK;
			rot_spi_data->data.rsb_err_cause = cpu_to_le32(err_status);
			get_rotator_err_ctx_id(hdev, offset, rot_spi_data, err_status,
						RSB_RR_ERROR);

			/* Clear interrupt (W1C) */
			WREG32(mmHD1_ROT0_BASE + offset + mmROTATOR_RSB_ERR_STATUS, err_status);
		}

		if (cause & ROTATOR_MSS_SPI_CAUSE_WCH_MASK) {
			err_status = RREG32(mmHD1_ROT0_BASE + offset + mmROTATOR_WCH_ERR_STATUS) &
					ROTATOR_WCH_ERR_STATUS_MASK;
			rot_spi_data->data.wch_err_cause = cpu_to_le32(err_status);
			get_rotator_err_ctx_id(hdev, offset, rot_spi_data, err_status,
						WCH_CH0_RR_ERROR);

			/* Clear interrupt (W1C) */
			WREG32(mmHD1_ROT0_BASE + offset + mmROTATOR_WCH_ERR_STATUS, err_status);
		}

		/* Clear interrupt (W1C) */
		WREG32(mmHD1_ROT0_BASE + offset + mmROTATOR_MSS_SPI_CLEAR, cause);
		unmask_event_in_aggr = true;
		break;

	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_ROT;
	params.grp_type = type;
	params.die = die;
	params.hdcore = hdcore;
	params.instance = instance;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static u32 stlb_special_regs_base[] = {
	mmHD0_STLB_SPECIAL_BASE
};

#define NUM_OF_DTLB_PER_NRTR (DTLB_NRTR1 - DTLB_NRTR0 + 1)

static void dtlb_read_fault_data(struct hl_device *hdev,
				 u32 base, struct hl_eq_dtlb_fault_data *data)
{
	u32 syndrom_l, syndrom_h;

	data->fault_type = cpu_to_le32(RREG32(base + mmDTLB_FLT_SYNDROM1));
	if (data->fault_type) {
		data->addr_47_20 = cpu_to_le32(RREG32(base + mmDTLB_FLT_SYNDROM2));
		syndrom_l = RREG32(base + mmDTLB_FLT_SYNDROM3);
		syndrom_h = RREG32(base + mmDTLB_FLT_SYNDROM4);
		data->id = cpu_to_le64(((u64) syndrom_h << 32) | syndrom_l);

		/* Clear interrupt (W1C) */
		WREG32((base + mmDTLB_FLT_SYNDROM_CLR), DTLB_FLT_SYNDROM_CLR_FLT_CLR_M);
	}
}

static void dtlbs_read_fault_data(struct hl_device *hdev, u32 die, u32 hdcore,
				  struct hl_eq_dtlb_fault_data *dtlb_data)
{
	struct hl_eq_dtlb_fault_data *dtlbd;
	u8 dtlb;
	u32 offset, nrtr_base;
	int rrtr;

	/* convert to absolute hdcore number */
	hdcore = die * NUM_OF_HDCORES_PER_DIE + hdcore;

	for (rrtr = DTLB_RRTR0 ; rrtr <= DTLB_RRTR7 ; rrtr++) {
		/* each RRTR has single DTLB so no need to iterate over DTLBs */
		offset = mmHD0_RRTR0_DTLB_BASE +
			 hdcore * HDCORE_OFFSET + rrtr * RRTR_OFFSET;
		dtlbd = &dtlb_data[rrtr];
		dtlb_read_fault_data(hdev, offset, dtlbd);
	}

	if (!(hdcore == 0 || hdcore == 2 || hdcore == 5 || hdcore == 7))
		return;

	/* for STLBs {0, 2, 5, 7} we have 2 more dtlbs connected which sits on the NRTR
	 * STLB0 and STLB7 are on NRTR0 , STLB2 and STLB5 are on NRTR1
	 */
	nrtr_base = hdcore == 0 || hdcore == 7 ?
			mmD0_NRTR0_DTLB_NW0_BASE : mmD0_NRTR1_DTLB_NW1_BASE;

	for (dtlb = 0 ; dtlb < NUM_OF_DTLB_PER_NRTR ; dtlb++) {
		offset = nrtr_base + (die * DIE_OFFSET) + (dtlb * NRTR_DTLB_OFFSET);
		dtlbd = &dtlb_data[dtlb + DTLB_NRTR0];
		dtlb_read_fault_data(hdev, offset, dtlbd);
	}
}

static void handle_and_clear_stlb_spi_events(struct hl_device *hdev, u32 die, u32 hdcore,
					     u32 offset, u32 idx,
					     struct hl_eq_stlb_spi_data *spi_data)
{
	u32 base, cause, syndrom_l, syndrom_h, mask;

	base = mmHD0_STLB_BASE + offset;

	switch (idx + SPI_INTR_STLB_BASE) {
	case SPI_INTR_STLB_FAULT:
		mask = STLB_INTR_SPI_CAUSE_FAULT_UNMAPPED_M |
			STLB_INTR_SPI_CAUSE_FAULT_PERMISSION_M |
			STLB_INTR_SPI_CAUSE_FAULT_PTW_DATA_M;
		syndrom_l = RREG32(base + mmSTLB_FAULT_SYNDROME1);
		syndrom_h = RREG32(base + mmSTLB_FAULT_SYNDROME2);
		spi_data->fault_data.syndrom_dti =
				cpu_to_le64(((u64) syndrom_h << 32) | syndrom_l);
		syndrom_l = RREG32(base + mmSTLB_FAULT_SYNDROME3);
		syndrom_h = RREG32(base + mmSTLB_FAULT_SYNDROME4);
		spi_data->fault_data.syndrom_pte =
				cpu_to_le64(((u64) syndrom_h << 32) | syndrom_l);

		dtlbs_read_fault_data(hdev, die, hdcore, spi_data->dtlb_data);

		break;
	case SPI_INTR_STLB_MAINT_QUEUE_FULL:
		mask = STLB_INTR_SPI_CAUSE_MAINT_QUEUE_FULL_M;
		break;
	case SPI_INTR_STLB_SW_PREFETCH_FAIL:
		mask = STLB_INTR_SPI_CAUSE_MAINT_PREFETCH_FAULT_M;
		break;
	default:
		return;
	}

	cause = RREG32(base + mmSTLB_INTR_SPI_CAUSE) & mask;
	spi_data->cause.intr_cause_data = cpu_to_le64(cause);

	/* Write 0 to clear interrupt */
	RMWREG32((base + mmSTLB_INTR_SPI_CAUSE), 0, cause);
	if (idx == SPI_INTR_STLB_FAULT)
		WREG32(base + mmSTLB_FAULT_SYNDORM_CNTRL,
			FIELD_PREP(STLB_FAULT_SYNDORM_CNTRL_REL_M, 0x1));
}

static void handle_and_clear_stlb_sei_events(struct hl_device *hdev, u32 offset, u32 idx,
					     struct hl_eq_stlb_sei_data *sei_data)
{
	u32 base, syndrom_l, syndrom_h, cause;

	/* single interrupt SEI_INTR_STLB_FAULT so index should be 0 */
	if (idx)
		return;

	base = mmHD0_STLB_BASE + offset;

	cause = RREG32(base + mmSTLB_INTR_SEI_CAUSE);
	sei_data->cause.intr_cause_data = cpu_to_le64((u64) cause);

	if (cause & STLB_INTR_SEI_CAUSE_LBW_RSP_ERR_M) {
		sei_data->lbw_data.addr = cpu_to_le32(RREG32(base + mmSTLB_FAULT_LBW_ADDR));
		sei_data->lbw_data.data = cpu_to_le32(RREG32(base + mmSTLB_FAULT_LBW_DATA));
	}

	if (cause & STLB_INTR_SEI_CAUSE_PTW_RSP_ERR_M) {
		syndrom_l = RREG32(base + mmSTLB_FAULT_SYNDROME1);
		syndrom_h = RREG32(base + mmSTLB_FAULT_SYNDROME2);
		sei_data->fault_data.syndrom_dti =
				cpu_to_le64(((u64) syndrom_h << 32) | syndrom_l);
	}

	/* Write 0 to clear interrupt */
	RMWREG32((base + mmSTLB_INTR_SEI_CAUSE), 0, cause);
}

/* HDCORE_STLB_EVENT */
static void handle_and_clear_stlb_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	struct eq_agg_header_params params = {};
	u32 offset;
	int rc;

	offset = (die * NUM_OF_HDCORES_PER_DIE + hdcore) * HDCORE_OFFSET;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		handle_and_clear_derr_events(hdev, stlb_special_regs_base,
						ARRAY_SIZE(stlb_special_regs_base), offset,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		break;
	case ERR_GRP_SEI:
		handle_and_clear_stlb_sei_events(hdev, offset, idx,
						 &eq_dynamic_entry->stlb_sei_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_stlb_sei_data));
		break;
	case ERR_GRP_SPI_ECO:
		handle_and_clear_stlb_spi_events(hdev, die, hdcore, offset, idx,
						 &eq_dynamic_entry->stlb_spi_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_stlb_spi_data));
		break;
	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_STLB;
	params.grp_type = type;
	params.die = die;
	params.hdcore = hdcore;
	params.instance = 0;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	WREG32_AND(aggr_mask_reg, events_mask);
}

static u32 pdma_special_regs_base[] = {
	mmD0_SPDMA0_CMN_B_SPECIAL_BASE,
	mmD0_SPDMA1_CMN_B_SPECIAL_BASE
};

static void pdma_eng_mask_err_int(struct hl_device *hdev, u32 err_cause_reg, u32 err_int_mask_reg,
								u32 *err_cause_data)
{
	u32 err_int_mask_val, err_ignore_msk;

	err_ignore_msk = PDMA_CH_B_ERR_STATUS_VALID_M;
	*err_cause_data = RREG32(err_cause_reg) & (~err_ignore_msk);

	if (*err_cause_data) {
		err_int_mask_val = RREG32(err_int_mask_reg);
		WREG32(err_int_mask_reg, err_int_mask_val | *err_cause_data);
	}
}

static void handle_and_clear_pdma_sei_events(struct hl_device *hdev, u8 die,
						struct hl_eq_pdma_sei_data *data)
{
	u32 ch_b_base, cmn_b_base, die_offset, spdma_offset, ch_offset, err_msk, cmn_err_msk,
				ch_err_cause_addr, ch_err_int_mask_addr;
	u8 i, j;

	die_offset = (DIE_OFFSET * die);
	ch_b_base = mmD0_SPDMA0_CH0_B_BASE + die_offset;
	cmn_b_base = mmD0_SPDMA0_CMN_B_PQM_CMN_B_BASE + die_offset;

	for (i = 0; i < SPDMA_ID_MAX; i++) {
		spdma_offset = (PDMA_GRP_OFFSET * i);
		ch_b_base += spdma_offset;
		cmn_b_base += spdma_offset;

		for (j = 0; j < SPDMA_CHANNEL_MAX; j++) {
			ch_offset = ch_b_base + (D0_SPDMA0_CH0_B_MAX_OFFSET * j);
			ch_err_cause_addr = ch_offset + mmPDMA_CH_B_ERR_STATUS;
			ch_err_int_mask_addr = ch_offset + mmPDMA_CH_B_ERR_INT_MASK;

			pdma_eng_mask_err_int(hdev, ch_err_cause_addr, ch_err_int_mask_addr,
									&err_msk);
			data->spdma_data[i].ch_b_data[j].err_sts.intr_cause_data =
					cpu_to_le64(err_msk);
		}

		cmn_err_msk = RREG32(cmn_b_base + mmPDMA_CMN_B_PQM_CMN_B_SEI_STATUS);

		for (j = 0; j < SPDMA_CHANNEL_MAX; j++) {
			ch_offset = ch_b_base + (D0_SPDMA0_CH0_B_MAX_OFFSET * j);

			err_msk = RREG32(ch_offset + mmPDMA_CH_B_PQM_CH_ERR_STATUS);
			/* Clear error status of PQM CH of channel B */
			WREG32((ch_offset + mmPDMA_CH_B_PQM_CH_ERR_STATUS), err_msk);

			data->spdma_data[i].ch_b_data[j].pqm_chn_err_sts.intr_cause_data =
					cpu_to_le64(err_msk);
		}

		/* Clear PDMA SEI interrupt */
		WREG32((cmn_b_base + mmPDMA_CMN_B_PQM_CMN_B_SEI_STATUS), cmn_err_msk);

		data->spdma_data[i].cmn_b_cause.intr_cause_data = cpu_to_le64(cmn_err_msk);
	}
}

/* SHARED_PDMA_EVENT */
static void handle_and_clear_pdma_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	struct eq_agg_header_params params = {};
	bool unmask_event_in_aggr = false;
	u32 offset;
	int rc;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		offset = die * DIE_OFFSET;
		handle_and_clear_derr_events(hdev, pdma_special_regs_base,
						ARRAY_SIZE(pdma_special_regs_base), offset,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SEI:
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_pdma_sei_data));
		handle_and_clear_pdma_sei_events(hdev, die, &eq_dynamic_entry->pdma_sei_data);
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SPI_ECO:
		break;
	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_PDMA;
	params.grp_type = type;
	params.die = die;
	params.hdcore = INT_SHARED;
	params.instance = 0;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static u32 arc_farm_special_regs_base[] = {
	mmHD0_ARC_FARM_ARC0_AUX_SPECIAL_BASE,
	mmHD0_ARC_FARM_ARC1_AUX_SPECIAL_BASE
};

/* HDCORE_ARCFARM_EVENT */
static void handle_and_clear_arc_farm_events(struct hl_device *hdev, u32 die, u32 hdcore,
					u32 instance, enum err_grp type, u32 sts, u32 sts_idx,
					u32 idx, u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	struct hl_eq_arcfarm_sei_data *arcfarm_sei_data;
	struct eq_agg_header_params params = {};
	bool unmask_event_in_aggr = false;
	u32 offset, err_msk;
	int rc;

	offset = (die * NUM_OF_HDCORES_PER_DIE + hdcore) * HDCORE_OFFSET;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		handle_and_clear_derr_events(hdev, arc_farm_special_regs_base,
						ARRAY_SIZE(arc_farm_special_regs_base), offset,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SEI:
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_arcfarm_sei_data));
		arcfarm_sei_data = &eq_dynamic_entry->arcfarm_sei_data;
		arcfarm_sei_data->arc0_wrapper_cause.intr_cause_data =
				cpu_to_le64(RREG32(mmHD0_ARC_FARM_ARC0_AUX_BASE +
						offset + mmQMAN_ARC_AUX_ARC_SEI_INTR_STS));
		arcfarm_sei_data->arc1_wrapper_cause.intr_cause_data =
				cpu_to_le64(RREG32(mmHD0_ARC_FARM_ARC1_AUX_BASE +
						offset + mmQMAN_ARC_AUX_ARC_SEI_INTR_STS));
		arcfarm_sei_data->internal_cause.intr_cause_data =
				cpu_to_le64(RREG32(mmHD0_ARC_FARM_FARM_BASE +
						offset + mmFARM_FARM_SEI_INTR_STS));
		unmask_event_in_aggr = true;
		break;
	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_ARC_FARM;
	params.grp_type = type;
	params.die = die;
	params.hdcore = hdcore;
	params.instance = 0;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	/* Clear event */
	if (type == ERR_GRP_SEI) {
		err_msk = RREG32(mmHD0_ARC_FARM_ARC0_AUX_BASE + offset +
					mmQMAN_ARC_AUX_ARC_SEI_INTR_STS);
		WREG32(mmHD0_ARC_FARM_ARC0_AUX_BASE + offset + mmQMAN_ARC_AUX_ARC_SEI_INTR_CLR,
				err_msk);

		err_msk = RREG32(mmHD0_ARC_FARM_ARC1_AUX_BASE + offset +
					mmQMAN_ARC_AUX_ARC_SEI_INTR_STS);
		WREG32(mmHD0_ARC_FARM_ARC1_AUX_BASE + offset + mmQMAN_ARC_AUX_ARC_SEI_INTR_CLR,
				err_msk);

		err_msk = RREG32(mmHD0_ARC_FARM_FARM_BASE + offset + mmFARM_FARM_SEI_INTR_STS);
		WREG32(mmHD0_ARC_FARM_FARM_BASE + offset + mmFARM_FARM_SEI_INTR_CLR, err_msk);
	}

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static u32 decoder_special_regs_base[] = {
	mmHD0_VDEC0_CTRL_SPECIAL_BASE,
	mmHD0_VDEC0_BRDG_CTRL_SPECIAL_BASE
};

/* HDCORE_DEC_EVENT */
static void handle_and_clear_decoder_events(struct hl_device *hdev, u32 die, u32 hdcore,
					u32 instance, enum err_grp type, u32 sts, u32 sts_idx,
					u32 idx, u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 offset, irq_status_addr, irq_status, cause_intr_addr, cause_intr;
	struct eq_agg_header_params params = {};
	bool unmask_event_in_aggr = false;
	int rc;

	offset = (die * NUM_OF_HDCORES_PER_DIE + hdcore) * HDCORE_OFFSET +
			instance * HDCORE_DECODER_OFFSET;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		handle_and_clear_derr_events(hdev, &decoder_special_regs_base[idx / 2], 1,
						offset, &eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;

	case ERR_GRP_SEI:
		cause_intr_addr = mmHD0_VDEC0_BRDG_CTRL_BASE + offset + mmVDEC_BRDG_CTRL_CAUSE_INTR;
		cause_intr = RREG32(cause_intr_addr) & VDEC_BRDG_CTRL_CAUSE_INTR_SEI_M;
		eq_dynamic_entry->razwi_with_intr_cause.intr_cause.intr_cause_data =
				cpu_to_le64(cause_intr);
		eq_dynamic_entry->hdr.size =
				cpu_to_le16(sizeof(struct hl_eq_razwi_with_intr_cause_data));

		/* Clear interrupt (W1C) */
		WREG32(cause_intr_addr, cause_intr);
		unmask_event_in_aggr = true;
		break;

	case ERR_GRP_SPI_ECO:
		irq_status_addr = mmHD0_VDEC0_CMD_BASE + offset + mmVSI_CMD_SWREG17;
		irq_status = RREG32(irq_status_addr);
		eq_dynamic_entry->spi_data.cause.intr_cause_data = cpu_to_le64(irq_status);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_generic_spi_data));

		/* Clear interrupt (W1C) */
		WREG32(irq_status_addr, irq_status);
		cause_intr_addr = mmHD0_VDEC0_BRDG_CTRL_BASE + offset + mmVDEC_BRDG_CTRL_CAUSE_INTR;
		WREG32_AND(cause_intr_addr, VDEC_BRDG_CTRL_CAUSE_INTR_SPI_M);
		unmask_event_in_aggr = true;
		break;

	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_DEC;
	params.grp_type = type;
	params.die = die;
	params.hdcore = hdcore;
	params.instance = instance;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static u32 edma_special_regs_base[] = {
	mmHD1_SEDMA0_QM_ARC_AUX_SPECIAL_BASE,
	mmHD1_SEDMA0_CMN_SPECIAL_BASE,
	mmHD1_SEDMA1_QM_ARC_AUX_SPECIAL_BASE,
	mmHD1_SEDMA1_CMN_SPECIAL_BASE
};

/* HDCORE_EDMA_EVENT */
static void handle_and_clear_edma_events(struct hl_device *hdev, u32 die, u32 hdcore, u32 instance,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 offset, edma_id, edma_offset, channel, channel_offset, chn_data_idx, chn_reg_base;
	struct hl_eq_edma_sei_data *edma_sei_data;
	struct hl_eq_edma_chn_data *edma_chn_data;
	struct eq_agg_header_params params = {};
	bool unmask_event_in_aggr = false;
	int rc;

	/* There are EDMA blocks only in HD 1/3/4/6 */
	if ((die == 0 && (hdcore == 0 || hdcore == 2)) ||
			(die == 1 && (hdcore == 1 || hdcore == 3)))  {
		dev_err(hdev->dev, "No EDMA interrupts are expected for DIE%u_HD%u!\n",
			die, hdcore);
		return;
	}

	/* Subtract 1 from hdcore because the offset is relative to the first EDMA in HD1 */
	offset = (die * NUM_OF_HDCORES_PER_DIE + hdcore - 1) * HDCORE_OFFSET;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		handle_and_clear_derr_events(hdev, edma_special_regs_base,
						ARRAY_SIZE(edma_special_regs_base), offset,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;

	case ERR_GRP_SEI:
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_edma_sei_data));
		for (edma_id = SEDMA_ID0 ; edma_id < SEDMA_ID_MAX ; ++edma_id) {
			edma_sei_data = &eq_dynamic_entry->edma_sei_data;
			edma_offset = offset + edma_id * HDCORE_EDMA_OFFSET;

			for (channel = SEDMA_CHANNEL0 ; channel < SEDMA_CHANNEL_MAX ; ++channel) {
				chn_data_idx = edma_id * SEDMA_CHANNEL_MAX + channel;
				edma_chn_data = &edma_sei_data->chn_data[chn_data_idx];
				channel_offset = channel * EDMA_CHANNEL_OFFSET;
				chn_reg_base = mmHD1_SEDMA0_CH0_BASE + edma_offset + channel_offset;
				edma_chn_data->err_sts =
					cpu_to_le32(RREG32(chn_reg_base + mmEDMA_CHN_ERR_STATUS) &
							EDMA_CHN_ERR_STATUS_ENG_MASK);
				edma_chn_data->ctx_id =
					cpu_to_le16(RREG32(chn_reg_base + mmEDMA_CHN_ERR_CTX_ID));

				/* Clear interrupt (W1C) */
				WREG32(chn_reg_base + mmEDMA_CHN_ERR_STATUS,
						le32_to_cpu(edma_chn_data->err_sts));
			}

			handle_and_clear_qman_interrupts(hdev,
							mmHD1_SEDMA0_QM_BASE + edma_offset,
							mmHD1_SEDMA0_QM_ARC_AUX_BASE + edma_offset,
							&edma_sei_data->qm_data[edma_id]);
		}

		unmask_event_in_aggr = true;
		break;

	case ERR_GRP_SPI_ECO:
		/* SPI interrupt is only for trace/debug so there's nothing to pass to EQ handler */
		break;

	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_EDMA;
	params.grp_type = type;
	params.die = die;
	params.hdcore = hdcore;
	params.instance = 0;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	if (rc)
		return;

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static u32 nic_special_regs_base[] = {
	mmD0_NIC0_RXB_CORE_SPECIAL_BASE,
	mmD0_NIC0_RXE_SPECIAL_BASE,
	mmD0_NIC0_TXS_SPECIAL_BASE,
	mmD0_NIC0_TXE_SPECIAL_BASE,
	mmD0_NIC0_TMR_SPECIAL_BASE,
	mmD0_NIC0_QPC_SPECIAL_BASE,
	mmD0_NIC0_TXB_SPECIAL_BASE,
	mmD0_NIC0_MSTR_IF_CTRL_SPECIAL_BASE,
	mmD0_NIC0_MSTR_IF_DATA_SPECIAL_BASE
};

static void gaudi3_cn_get_spi_event_data(struct hl_device *hdev,
				  struct hl_eq_nic_spi_data *spi_data,
				  u32 macro_index)
{
	u32 rxe_spi_intr_cause_0, rxe_spi_intr_cause_1, rxb_core_spi_intr_cause,
		rxe_spi_intr_mask_0, rxe_spi_intr_mask_1, rxb_core_spi_intr_mask,
		qpc_intr_cause, port = macro_index * NIC_PORTS_PER_MACRO;

	qpc_intr_cause = NIC_RREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_INTERRUPT_CAUSE);
	rxe_spi_intr_cause_0 = NIC_RREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SPI_INTR_CAUSE_0);
	rxe_spi_intr_mask_0 = NIC_RREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SPI_INTR_MASK_0);
	rxe_spi_intr_cause_0 = rxe_spi_intr_cause_0 & ~rxe_spi_intr_mask_0;

	rxe_spi_intr_cause_1 = NIC_RREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SPI_INTR_CAUSE_1);
	rxe_spi_intr_mask_1 = NIC_RREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SPI_INTR_MASK_1);
	rxe_spi_intr_cause_1 = rxe_spi_intr_cause_1 & ~rxe_spi_intr_mask_1;

	rxb_core_spi_intr_cause = NIC_RREG32(mmD0_NIC0_RXB_CORE_BASE +
					     mmNIC_RXB_CORE_SPI_INTR_CAUSE);
	rxb_core_spi_intr_mask = NIC_RREG32(mmD0_NIC0_RXB_CORE_BASE + mmNIC_RXB_CORE_SPI_INTR_MASK);
	rxb_core_spi_intr_cause = rxb_core_spi_intr_cause & ~rxb_core_spi_intr_mask;

	spi_data->qpc_cause.intr_cause_data = cpu_to_le64(qpc_intr_cause);
	spi_data->rxb_core_cause.intr_cause_data = cpu_to_le64(rxb_core_spi_intr_cause);
	spi_data->rxe_cause_0.intr_cause_data = cpu_to_le64(rxe_spi_intr_cause_0);
	spi_data->rxe_cause_1.intr_cause_data = cpu_to_le64(rxe_spi_intr_cause_1);
}

static void gaudi3_cn_clear_spi_event(struct hl_device *hdev,
			       struct hl_eq_nic_spi_data *spi_data,
			       u32 macro_index)
{
	u32 rxe_spi_intr_cause_0, rxe_spi_intr_cause_1, rxb_core_spi_intr_cause,
		qpc_intr_cause, port, first_port, last_port;

	first_port = macro_index * NIC_PORTS_PER_MACRO;
	last_port = (macro_index + 1) * NIC_PORTS_PER_MACRO - 1;

	qpc_intr_cause = lower_32_bits(le64_to_cpu(spi_data->qpc_cause.intr_cause_data));

	for (port = first_port; port <= last_port; port++) {
		/* check that port is indeed enabled in the macro */
		if (!(hdev->cn.ports_mask & BIT(port)))
			continue;

		/* eqe interrupts are mapped to MSI except interrupt on error event queue
		 * which is handled here, in such case port reset is required.
		 */
		if ((qpc_intr_cause & GAUDI3_NIC_EQ_ERR_INTERRUPT_M(port)))
			NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_INTERRUPT_CLR,
				   GAUDI3_NIC_EQ_ERR_INTERRUPT_M(port));
	}

	port = first_port;
	rxb_core_spi_intr_cause =
			lower_32_bits(le64_to_cpu(spi_data->rxb_core_cause.intr_cause_data));
	rxe_spi_intr_cause_0 =
			lower_32_bits(le64_to_cpu(spi_data->rxe_cause_0.intr_cause_data));
	rxe_spi_intr_cause_1 =
			lower_32_bits(le64_to_cpu(spi_data->rxe_cause_1.intr_cause_data));

	/* RXE SPI interrupts are packet caused interrupts and are not severe,
	 * no need to perform port reset on them, they should be print for debug purpose.
	 */
	if (rxe_spi_intr_cause_0) {
		/* After writing to the SPI_INTR_CLEAR register we need to set it back to zero as
		 * it's a sticky register (the read between is done in order to flush the first
		 * write).
		 */
		NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SPI_INTR_CLEAR_0, rxe_spi_intr_cause_0);
		NIC_RREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SPI_INTR_CLEAR_0);
		NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SPI_INTR_CLEAR_0, 0);
	}

	if (rxe_spi_intr_cause_1) {
		/* After writing to the SPI_INTR_CLEAR register we need to set it back to zero as
		 * it's a sticky register (the read between is done in order to flush the first
		 * write).
		 */
		NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SPI_INTR_CLEAR_1, rxe_spi_intr_cause_1);
		NIC_RREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SPI_INTR_CLEAR_1);
		NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SPI_INTR_CLEAR_1, 0);
	}

	if (rxb_core_spi_intr_cause) {
		/* After writing to the SPI_INTR_CLEAR register we need to set it back to zero
		 * as it's a sticky register (the read between is done in order to flush the first
		 * write).
		 */
		NIC_WREG32(mmD0_NIC0_RXB_CORE_BASE + mmNIC_RXB_CORE_SPI_INTR_CLEAR,
			   rxb_core_spi_intr_cause);
		NIC_RREG32(mmD0_NIC0_RXB_CORE_BASE + mmNIC_RXB_CORE_SPI_INTR_CLEAR);
		NIC_WREG32(mmD0_NIC0_RXB_CORE_BASE + mmNIC_RXB_CORE_SPI_INTR_CLEAR, 0);
	}
}

static void gaudi3_cn_get_sei_error_event_data(struct hl_device *hdev,
					 struct hl_eq_nic_sei_data *sei_data,
					 u32 macro_index)
{
	u32 rxe_sei_intr_cause, rxb_core_sei_intr_cause, tmr_intr_cause, rxe_sei_intr_mask,
		rxb_core_sei_intr_mask, tmr_intr_mask, port, qpc_intr_resp_err_cause,
		txs_intr_cause, txe_intr_cause, qpc_intr_resp_err_mask, txs_intr_mask,
		txe_intr_mask;

	port = macro_index * NIC_PORTS_PER_MACRO;
	rxe_sei_intr_cause = NIC_RREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SEI_INTR_CAUSE);
	rxe_sei_intr_mask = NIC_RREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SEI_INTR_MASK);
	rxe_sei_intr_cause = rxe_sei_intr_cause & ~rxe_sei_intr_mask;

	rxb_core_sei_intr_cause = NIC_RREG32(mmD0_NIC0_RXB_CORE_BASE +
			mmNIC_RXB_CORE_SEI_INTR_CAUSE);
	rxb_core_sei_intr_mask = NIC_RREG32(mmD0_NIC0_RXB_CORE_BASE +
			mmNIC_RXB_CORE_SEI_INTR_MASK);
	rxb_core_sei_intr_cause = rxb_core_sei_intr_cause & ~rxb_core_sei_intr_mask;

	tmr_intr_cause = NIC_RREG32(mmD0_NIC0_TMR_BASE + mmNIC_TMR_INTERRUPT_CAUSE);
	tmr_intr_mask = NIC_RREG32(mmD0_NIC0_TMR_BASE + mmNIC_TMR_INTERRUPT_MASK);
	tmr_intr_cause = tmr_intr_cause & ~tmr_intr_mask;

	qpc_intr_resp_err_cause = NIC_RREG32(mmD0_NIC0_QPC_BASE +
			mmNIC_QPC_INTERRUPT_RESP_ERR_CAUSE);
	qpc_intr_resp_err_mask = NIC_RREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_INTERRUPT_RESP_ERR_MASK);
	qpc_intr_resp_err_cause = qpc_intr_resp_err_cause & ~qpc_intr_resp_err_mask;

	txs_intr_cause = NIC_RREG32(mmD0_NIC0_TXS_BASE + mmNIC_TXS_INTERRUPT_CAUSE);
	txs_intr_mask = NIC_RREG32(mmD0_NIC0_TXS_BASE + mmNIC_TXS_INTERRUPT_MASK);
	txs_intr_cause = txs_intr_cause & ~txs_intr_mask;

	txe_intr_cause = NIC_RREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_INTERRUPT_CAUSE);
	txe_intr_mask = NIC_RREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_INTERRUPT_MASK);
	txe_intr_cause = txe_intr_cause & ~txe_intr_mask;

	sei_data->rxe_cause.intr_cause_data = cpu_to_le64(rxe_sei_intr_cause);
	sei_data->rxb_core_cause.intr_cause_data = cpu_to_le64(rxb_core_sei_intr_cause);
	sei_data->tmr_cause.intr_cause_data = cpu_to_le64(tmr_intr_cause);
	sei_data->qpc_cause.intr_cause_data = cpu_to_le64(qpc_intr_resp_err_cause);
	sei_data->txs_cause.intr_cause_data = cpu_to_le64(txs_intr_cause);
}

static void gaudi3_cn_clear_sei_error_event(struct hl_device *hdev,
				      const struct hl_eq_nic_sei_data *sei_data,
				      u32 macro_index)
{
	u32 rxe_sei_intr_cause, rxb_core_sei_intr_cause, tmr_intr_cause, port,
		qpc_intr_resp_err_cause, txs_intr_cause, txe_intr_cause;

	rxe_sei_intr_cause = lower_32_bits(le64_to_cpu(sei_data->rxe_cause.intr_cause_data));
	rxb_core_sei_intr_cause =
			lower_32_bits(le64_to_cpu(sei_data->rxb_core_cause.intr_cause_data));
	tmr_intr_cause = lower_32_bits(le64_to_cpu(sei_data->tmr_cause.intr_cause_data));
	qpc_intr_resp_err_cause = lower_32_bits(le64_to_cpu(sei_data->qpc_cause.intr_cause_data));
	txs_intr_cause = lower_32_bits(le64_to_cpu(sei_data->txs_cause.intr_cause_data));
	txe_intr_cause = lower_32_bits(le64_to_cpu(sei_data->txe_cause.intr_cause_data));

	port = macro_index * NIC_PORTS_PER_MACRO;

	if (rxe_sei_intr_cause) {
		/* After writing to the SEI_INTR_CLEAR register we need to set it back to zero as
		 * it's a sticky register (the read between is done in order to flush the first
		 * write).
		 */
		NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SEI_INTR_CLEAR, rxe_sei_intr_cause);
		NIC_RREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SEI_INTR_CLEAR);
		NIC_WREG32(mmD0_NIC0_RXE_BASE + mmNIC_RXE_SEI_INTR_CLEAR, 0);
	}

	if (rxb_core_sei_intr_cause) {
		/* After writing to the SEI_INTR_CLEAR register we need to set it back to zero
		 * as it's a sticky register (the read between is done in order to flush the first
		 * write).
		 */
		NIC_WREG32(mmD0_NIC0_RXB_CORE_BASE + mmNIC_RXB_CORE_SEI_INTR_CLEAR,
				rxb_core_sei_intr_cause);
		NIC_RREG32(mmD0_NIC0_RXB_CORE_BASE + mmNIC_RXB_CORE_SEI_INTR_CLEAR);
		NIC_WREG32(mmD0_NIC0_RXB_CORE_BASE + mmNIC_RXB_CORE_SEI_INTR_CLEAR, 0);
	}

	if (tmr_intr_cause)
		NIC_WREG32(mmD0_NIC0_TMR_BASE + mmNIC_TMR_INTERRUPT_CLR, tmr_intr_cause);

	if (qpc_intr_resp_err_cause)
		NIC_WREG32(mmD0_NIC0_QPC_BASE + mmNIC_QPC_INTERRUPR_RESP_ERR_CLR,
				qpc_intr_resp_err_cause);

	if (txs_intr_cause)
		NIC_WREG32(mmD0_NIC0_TXS_BASE + mmNIC_TXS_INTERRUPT_CLR, txs_intr_cause);

	if (txe_intr_cause)
		NIC_WREG32(mmD0_NIC0_TXE_BASE + mmNIC_TXE_INTERRUPT_CLR, txe_intr_cause);
}
/* SHARED_NIC_EVENT */
static void handle_and_clear_nic_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	struct eq_agg_header_params params = {};
	bool unmask_event_in_aggr = false;
	u32 instance, offset, macro_index;
	int rc;

	memset(eq_dynamic_entry, 0, sizeof(*eq_dynamic_entry));

	switch (type) {
	case ERR_GRP_DERR:
		instance = idx;
		offset = die * NIC_DIE_OFFSET + instance * NIC_OFFSET;
		handle_and_clear_derr_events(hdev, nic_special_regs_base,
						ARRAY_SIZE(nic_special_regs_base), offset,
						&eq_dynamic_entry->ecc_data);
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_ecc_data));
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_SEI:
		instance = idx;
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_nic_sei_data));
		unmask_event_in_aggr = true;
		macro_index = die * NIC_NUM_MACROS_PER_DIE + instance;
		gaudi3_cn_get_sei_error_event_data(hdev, &eq_dynamic_entry->nic_sei_data,
						   macro_index);
		gaudi3_cn_clear_sei_error_event(hdev, &eq_dynamic_entry->nic_sei_data,
						macro_index);
		break;
	case ERR_GRP_SPI_ECO:
		instance = idx / 2;
		eq_dynamic_entry->nic_spi_data.spi_type = idx & 0x1;
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_nic_spi_data));
		unmask_event_in_aggr = true;
		/* TODO: SW-163409 get cause & clear interrupt(s) for NIC_SPI_BMON_SPMU */
		if (eq_dynamic_entry->nic_spi_data.spi_type != NIC_SPI_BMON_SPMU) {
			macro_index = die * NIC_NUM_MACROS_PER_DIE + instance;
			gaudi3_cn_get_spi_event_data(hdev, &eq_dynamic_entry->nic_spi_data,
						     macro_index);
			gaudi3_cn_clear_spi_event(hdev, &eq_dynamic_entry->nic_spi_data,
						  macro_index);
		}
		break;
	default:
		return;
	}

	params.component_type = INT_COMP_TYPE_NIC;
	params.grp_type = type;
	params.die = die;
	params.hdcore = INT_SHARED;
	params.instance = instance;
	prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

	rc = gaudi3_handle_eqe(hdev, eq_dynamic_entry);

	if (rc)
		return;

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static void get_cpu_aggr_intr_map(u32 intr_map_idx, struct gaudi3_cpu_aggr_intr_map *intr_map)
{
	struct gaudi3_async_events_ids_map async_events_ids_map = { .intr_data = U32_MAX };

	if (intr_map_idx < ARRAY_SIZE(gaudi3_irq_map_table))
		async_events_ids_map.intr_data = gaudi3_irq_map_table[intr_map_idx].intr_data;

	intr_map->grp_type = async_events_ids_map.grp_type;
	intr_map->comp_type = async_events_ids_map.comp_type;
	intr_map->comp_inst = async_events_ids_map.comp_inst;
	intr_map->hdcore_type = async_events_ids_map.hdcore_type;
	intr_map->die_id = async_events_ids_map.die_id;
}

static void hdcore_aggr_intr_to_hdcore_and_instance(u32 die, u32 aggr_hdcore, enum err_grp grp_type,
							u32 aggr_idx, u32 *hdcore, u32 *instance)
{
	struct gaudi3_cpu_aggr_intr_map intr_map = {};
	u32 intr_map_idx, grp_offset = 0;

	switch (grp_type) {
	case ERR_GRP_DERR:
		grp_offset = CPU_HDCORE_AGGR_DERR_GRP_INTR_OFFSET;
		break;
	case ERR_GRP_SERR:
		grp_offset = CPU_HDCORE_AGGR_SERR_GRP_INTR_OFFSET;
		break;
	case ERR_GRP_SEI:
		grp_offset = CPU_HDCORE_AGGR_SEI_GRP_INTR_OFFSET;
		break;
	case ERR_GRP_SPI_ECO:
		grp_offset = CPU_HDCORE_AGGR_SPI_GRP_INTR_OFFSET;
		break;
	default:
		*hdcore = *instance = U32_MAX;
		return;
	}

	intr_map_idx = die * CPU_HDCORE_AGGRS_INTR_NUM_PER_DIE +
			aggr_hdcore * CPU_HDCORE_AGGR_INTR_NUM + grp_offset + aggr_idx;

	get_cpu_aggr_intr_map(intr_map_idx, &intr_map);

	*hdcore = intr_map.hdcore_type;
	*instance = intr_map.comp_inst;
}

static void gaudi3_shared_spi_event_info(struct hl_device *hdev, u32 die,
					 struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 offset, sts0, sts1, sts2, idx,
			sts0_pcie_mask0 = BIT(3),
			sts0_pcie_mask1 = GENMASK(11, 6),
			sts0_pcie_mask2 = GENMASK(16, 15),
			sts0_nic_mask = GENMASK(31, 21),
			sts1_nic_mask = BIT(0),
			sts1_nch_mask = GENMASK(2, 1),
			sts1_pmmu_mask = GENMASK(5, 4),
			sts1_ts_mask = GENMASK(15, 8),
			sts1_pdma_mask = BIT(16),
			sts1_d2d_mask = GENMASK(18, 17);

	offset = die * DIE_OFFSET;
	sts0 = RREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_STS_0 + offset);
	sts1 = RREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_STS_1 + offset);
	sts2 = RREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_STS_2 + offset);

	/* Mask events first, if there is a handler then it'll be unmasked back */
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + offset, sts0);
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset, sts1);
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_2 + offset, sts2);

	/* Handle PCIE */
	if (sts0 & sts0_pcie_mask0) {
		idx = 1;
		dev_err(hdev->dev, "Received PCIE_SPI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PCIE_EVENT])
			shared_handle_and_clear[SHARED_PCIE_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts0, 0, 0,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pcie_mask0), eq_dynamic_entry);
	}

	if (sts0 & sts0_pcie_mask1) {
		idx = ffs(sts0 & sts0_pcie_mask1) - 3;
		dev_err(hdev->dev, "Received PCIE_SPI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PCIE_EVENT])
			shared_handle_and_clear[SHARED_PCIE_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts0, 0, idx - 3,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pcie_mask1), eq_dynamic_entry);
	}

	if (sts0 & sts0_pcie_mask2) {
		idx = ffs(sts0 & sts0_pcie_mask2) - 3;
		dev_err(hdev->dev, "Received PCIE_SPI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PCIE_EVENT])
			shared_handle_and_clear[SHARED_PCIE_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts0, 0, idx - 6,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pcie_mask2), eq_dynamic_entry);
	}

	/* Handle NIC */
	if (sts0 & sts0_nic_mask) {
		idx = ffs(sts0 & sts0_nic_mask) - 22;
		dev_err(hdev->dev, "Received NIC_SPI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_NIC_EVENT])
			shared_handle_and_clear[SHARED_NIC_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_nic_mask), eq_dynamic_entry);
	}

	if (sts1 & sts1_nic_mask) {
		idx = 11;
		dev_err(hdev->dev, "Received NIC_SPI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_NIC_EVENT])
			shared_handle_and_clear[SHARED_NIC_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_nic_mask), eq_dynamic_entry);
	}

	/* Handle NCH */
	if (sts1 & sts1_nch_mask) {
		idx = ffs(sts1 & sts1_nch_mask) - 2;
		dev_err(hdev->dev, "Received NCH_SPI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_NCH_EVENT])
			shared_handle_and_clear[SHARED_NCH_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_nch_mask), eq_dynamic_entry);
	}

	/* Handle PMMU */
	if (sts1 & sts1_pmmu_mask) {
		idx = ffs(sts1 & sts1_pmmu_mask) - 4;
		dev_err(hdev->dev, "Received PMMU_SPI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PMMU_EVENT])
			shared_handle_and_clear[SHARED_PMMU_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts1, 1, idx - 1,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_pmmu_mask), eq_dynamic_entry);
	}

	/* Handle TS */
	if (sts1 & sts1_ts_mask) {
		idx = ffs(sts1 & sts1_ts_mask) - 9;
		dev_err(hdev->dev, "Received TS_SPI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_TS_EVENT])
			shared_handle_and_clear[SHARED_TS_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_ts_mask), eq_dynamic_entry);
	}

	/* Handle PDMA */
	if (sts1 & sts1_pdma_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received PDMA_SPI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PDMA_EVENT])
			shared_handle_and_clear[SHARED_PDMA_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_pdma_mask), eq_dynamic_entry);
	}

	/* Handle D2D */
	if (sts1 & sts1_d2d_mask) {
		idx = ffs(sts1 & sts1_d2d_mask) - 18;
		dev_err(hdev->dev, "Received D2D_SPI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_D2D_EVENT])
			shared_handle_and_clear[SHARED_D2D_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_d2d_mask), eq_dynamic_entry);
	}

	/* Clear interrupt - W1C */
	WREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_STS_0 + offset, sts0);
	WREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_STS_1 + offset, sts1);
	WREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_STS_2 + offset, sts2);
	WREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MSG_PENDING + offset, 1);
}

static void gaudi3_hdcore_spi_event_info(struct hl_device *hdev, u32 offset, u32 die,
					 u32 aggr_hdcore,
					 struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 hdcore, instance, idx, sts0, sts1, sts2, sts3,
			sts0_mme_mask = GENMASK(14, 12),
			sts0_tpc_mask = GENMASK(31, 15),
			sts1_tpc_mask = BIT(0),
			sts1_rot_mask = GENMASK(3, 1),
			sts1_cs_mask = GENMASK(20, 5),
			sts1_stlb_mask = GENMASK(23, 21),
			sts1_edma_mask = BIT(25),
			sts1_sob_mask = BIT(31),
			sts2_dec_mask = GENMASK(5, 2);

	sts0 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_STS_0 + offset);
	sts1 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_STS_1 + offset);
	sts2 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_STS_2 + offset);
	sts3 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_STS_3 + offset);

	/* Mask events first, if there is a handler then it'll be unmasked back */
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_0 + offset, sts0);
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset, sts1);
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_2 + offset, sts2);
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_3 + offset, sts3);

	/* Handle MME */
	if (sts0 & sts0_mme_mask) {
		idx = ffs(sts0 & sts0_mme_mask) - 1;
		dev_err(hdev->dev, "Received MME_SPI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_MME_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SPI_ECO,
								__ffs(sts0 & sts0_mme_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_MME_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SPI_ECO, sts0, 0, idx - 12,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_mme_mask), eq_dynamic_entry);
		}
	}

	/* Handle TPC */
	if (sts0 & sts0_tpc_mask) {
		idx = ffs(sts0 & sts0_tpc_mask) - 16;
		dev_err(hdev->dev, "Received TPC_SPI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_TPC_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SPI_ECO,
								__ffs(sts0 & sts0_tpc_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_TPC_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SPI_ECO, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_tpc_mask), eq_dynamic_entry);
		}
	}

	if (sts1 & sts1_tpc_mask) {
		idx = 17;
		dev_err(hdev->dev, "Received TPC_SPI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_TPC_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SPI_ECO,
								32 + __ffs(sts1 & sts1_tpc_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_TPC_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_tpc_mask), eq_dynamic_entry);
		}
	}

	/* Handle ROT */
	if (sts1 & sts1_rot_mask) {
		idx = ffs(sts1 & sts1_rot_mask) - 2;
		dev_err(hdev->dev, "Received ROT_SPI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_ROT_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SPI_ECO,
								32 + __ffs(sts1 & sts1_rot_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_ROT_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_rot_mask), eq_dynamic_entry);
		}
	}

	/* Handle Cache */
	if (sts1 & sts1_cs_mask) {
		idx = ffs(sts1 & sts1_cs_mask) - 6;
		dev_err(hdev->dev, "Received CS_SPI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_CS_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SPI_ECO,
								32 + __ffs(sts1 & sts1_cs_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_CS_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_cs_mask), eq_dynamic_entry);
		}
	}

	/* Handle STLB */
	if (sts1 & sts1_stlb_mask) {
		idx = ffs(sts1 & sts1_stlb_mask) - 22;
		dev_err(hdev->dev, "Received STLB_SPI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_STLB_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SPI_ECO,
								32 + __ffs(sts1 & sts1_stlb_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_STLB_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_stlb_mask), eq_dynamic_entry);
		}
	}

	/* Handle EDMA */
	if (sts1 & sts1_edma_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received EDMA_SPI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_EDMA_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SPI_ECO,
								32 + __ffs(sts1 & sts1_edma_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_EDMA_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_edma_mask), eq_dynamic_entry);
		}
	}

	/* Handle SOB */
	if (sts1 & sts1_sob_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received SOB_SPI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_SOB_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SPI_ECO,
								32 + __ffs(sts1 & sts1_sob_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_SOB_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_sob_mask), eq_dynamic_entry);
		}
	}

	/* Handle Decoder */
	if (sts2 & sts2_dec_mask) {
		idx = ffs(sts2 & sts2_dec_mask) - 3;
		dev_err(hdev->dev, "Received DEC_SPI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_DEC_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SPI_ECO,
								64 + __ffs(sts2 & sts2_dec_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_DEC_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SPI_ECO, sts2, 2, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_2 + offset,
				~(sts2 & sts2_dec_mask), eq_dynamic_entry);
		}
	}

	/* Clear interrupt - W1C */
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_STS_0 + offset, sts0);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_STS_1 + offset, sts1);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_STS_2 + offset, sts2);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_STS_3 + offset, sts3);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MSG_PENDING + offset, 1);
}

static void gaudi3_hdcore_sei_event_info(struct hl_device *hdev, u32 offset, u32 die,
					 u32 aggr_hdcore,
					 struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 hdcore, instance, idx, sts0, sts1,
			sts0_mme_mask = GENMASK(11, 0),
			sts0_tpc_mask = GENMASK(20, 12),
			sts0_rot_mask = GENMASK(22, 21),
			sts0_cs_mask = GENMASK(30, 23),
			sts0_stlb_mask = BIT(31),
			sts1_edma_mask = BIT(0),
			sts1_hbm_mask = GENMASK(4, 1),
			sts1_sob_mask = BIT(9),
			sts1_arcfarm_mask = BIT(10),
			sts1_dup_mask = BIT(11),
			sts1_dec_mask = GENMASK(13, 12);

	sts0 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_STS_0 + offset);
	sts1 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_STS_1 + offset);

	/* Mask events first, if there is a handler then it'll be unmasked back */
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_0 + offset, sts0);
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset, sts1);

	/* Handle MME */
	if (sts0 & sts0_mme_mask) {
		idx = ffs(sts0 & sts0_mme_mask) - 1;
		dev_err(hdev->dev, "Received MME_SEI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_MME_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SEI,
								__ffs(sts0 & sts0_mme_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_MME_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_mme_mask), eq_dynamic_entry);
		}
	}

	/* Handle TPC */
	if (sts0 & sts0_tpc_mask) {
		idx = ffs(sts0 & sts0_tpc_mask) - 13;
		dev_err(hdev->dev, "Received TPC_SEI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_TPC_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SEI,
								__ffs(sts0 & sts0_tpc_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_TPC_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_tpc_mask), eq_dynamic_entry);
		}
	}

	/* Handle ROT */
	if (sts0 & sts0_rot_mask) {
		idx = ffs(sts0 & sts0_rot_mask) - 22;
		dev_err(hdev->dev, "Received ROT_SEI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_ROT_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SEI,
								__ffs(sts0 & sts0_rot_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_ROT_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_rot_mask), eq_dynamic_entry);
		}
	}

	/* Handle Cache */
	if (sts0 & sts0_cs_mask) {
		idx = ffs(sts0 & sts0_cs_mask) - 24;
		dev_err(hdev->dev, "Received CS_SEI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_CS_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SEI,
								__ffs(sts0 & sts0_cs_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_CS_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_cs_mask), eq_dynamic_entry);
		}
	}

	/* Handle STLB */
	if (sts0 & sts0_stlb_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received STLB_SEI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_STLB_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SEI,
								__ffs(sts0 & sts0_stlb_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_STLB_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_stlb_mask), eq_dynamic_entry);
		}
	}

	/* Handle EDMA */
	if (sts1 & sts1_edma_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received EDMA_SEI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_EDMA_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SEI,
								32 + __ffs(sts1 & sts1_edma_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_EDMA_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_edma_mask), eq_dynamic_entry);
		}
	}

	/* Handle HBM */
	if (sts1 & sts1_hbm_mask) {
		idx = ffs(sts1 & sts1_hbm_mask) - 2;
		dev_err(hdev->dev, "Received HBM_SEI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_HBM_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SEI,
								32 + __ffs(sts1 & sts1_hbm_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_HBM_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_hbm_mask), eq_dynamic_entry);
		}
	}

	/* Handle SOB */
	if (sts1 & sts1_sob_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received SOB_SEI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_SOB_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SEI,
								32 + __ffs(sts1 & sts1_sob_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_SOB_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_sob_mask), eq_dynamic_entry);
		}
	}

	/* Handle ARCFARM */
	if (sts1 & sts1_arcfarm_mask) {
		idx = 0;
		dev_err(hdev->dev,
			"Received ARC_FARM_SEI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_ARCFARM_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SEI,
							32 + __ffs(sts1 & sts1_arcfarm_mask),
							&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_ARCFARM_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_arcfarm_mask), eq_dynamic_entry);
		}
	}

	/* Handle DUP */
	if (sts1 & sts1_dup_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received DUP_SEI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_DUP_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SEI,
								32 + __ffs(sts1 & sts1_dup_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_DUP_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_dup_mask), eq_dynamic_entry);
		}
	}

	/* Handle DEC */
	if (sts1 & sts1_dec_mask) {
		idx = ffs(sts1 & sts1_dec_mask) - 13;
		dev_err(hdev->dev, "Received DEC_SEI[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_DEC_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_SEI,
								32 + __ffs(sts1 & sts1_dec_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_DEC_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_dec_mask), eq_dynamic_entry);
		}
	}

	/* Clear interrupt - W1C */
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_STS_0 + offset, sts0);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_STS_1 + offset, sts1);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_MSG_PENDING + offset, 1);
}

static void gaudi3_shared_sei_event_info(struct hl_device *hdev, u32 die,
					 struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 offset, sts0, sts1, sts2, idx,
		sts0_cpu_mask = GENMASK(2, 0),
		sts0_pcie_mask = BIT(3),
		sts0_nic_mask = GENMASK(10, 5),
		sts0_nch_mask = GENMASK(12, 11),
		sts0_pmmu_mask = BIT(14),
		sts0_vm_mask = GENMASK(22, 15),
		sts0_pdma_mask = BIT(23),
		sts0_psoc_mask = BIT(24),
		sts0_parc_mask = BIT(25),
		sts0_d2d_mask = GENMASK(27, 26),
		sts0_glink_mask = GENMASK(31, 28),
		sts1_glink_mask = GENMASK(1, 0),
		sts1_pll_mask = GENMASK(30, 6);

	offset = die * DIE_OFFSET;
	sts0 = RREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_STS_0 + offset);
	sts1 = RREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_STS_1 + offset);
	sts2 = RREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_STS_2 + offset);

	/* Mask events first, if there is a handler then it'll be unmasked back */
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset, sts0);
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_1 + offset, sts1);
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_2 + offset, sts2);

	/* Handle CPU */
	if (sts0 & sts0_cpu_mask) {
		idx = ffs(sts0 & sts0_cpu_mask) - 1;
		dev_err(hdev->dev, "Received CPU_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_CPU_EVENT])
			shared_handle_and_clear[SHARED_CPU_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_cpu_mask), eq_dynamic_entry);
	}

	/* Handle PCIE */
	if (sts0 & sts0_pcie_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received PCIE_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PCIE_EVENT])
			shared_handle_and_clear[SHARED_PCIE_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pcie_mask), eq_dynamic_entry);
	}

	/* Handle NIC */
	if (sts0 & sts0_nic_mask) {
		idx = ffs(sts0 & sts0_nic_mask) - 6;
		dev_err(hdev->dev, "Received NIC_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_NIC_EVENT])
			shared_handle_and_clear[SHARED_NIC_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_nic_mask), eq_dynamic_entry);
	}

	/* Handle NCH */
	if (sts0 & sts0_nch_mask) {
		idx = ffs(sts0 & sts0_nch_mask) - 12;
		dev_err(hdev->dev, "Received NCH_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_NCH_EVENT])
			shared_handle_and_clear[SHARED_NCH_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_nch_mask), eq_dynamic_entry);
	}

	/* Handle PMMU */
	if (sts0 & sts0_pmmu_mask) {
		idx = 1;
		dev_err(hdev->dev, "Received PMMU_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PMMU_EVENT])
			shared_handle_and_clear[SHARED_PMMU_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, 0,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pmmu_mask), eq_dynamic_entry);
	}

	/* Handle VM */
	if (sts0 & sts0_vm_mask) {
		idx = ffs(sts0 & sts0_vm_mask) - 16;
		dev_err(hdev->dev, "Received VM_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_VM_EVENT])
			shared_handle_and_clear[SHARED_VM_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_vm_mask), eq_dynamic_entry);
	}

	/* Handle PDMA */
	if (sts0 & sts0_pdma_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received PDMA_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PDMA_EVENT])
			shared_handle_and_clear[SHARED_PDMA_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pdma_mask), eq_dynamic_entry);
	}

	/* Handle PSOC */
	if (sts0 & sts0_psoc_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received PSOC_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PSOC_EVENT])
			shared_handle_and_clear[SHARED_PSOC_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_psoc_mask), eq_dynamic_entry);
	}

	/* Handle PARC */
	if (sts0 & sts0_parc_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received PARC_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PARC_EVENT])
			shared_handle_and_clear[SHARED_PARC_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_parc_mask), eq_dynamic_entry);
	}

	/* Handle D2D */
	if (sts0 & sts0_d2d_mask) {
		idx = ffs(sts0 & sts0_d2d_mask) - 27;
		dev_err(hdev->dev, "Received D2D_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_D2D_EVENT])
			shared_handle_and_clear[SHARED_D2D_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_d2d_mask), eq_dynamic_entry);
	}

	/* Handle GLINK */
	if (sts0 & sts0_glink_mask) {
		idx = ffs(sts0 & sts0_glink_mask) - 29;
		dev_err(hdev->dev, "Received GLINK_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_GLINK_EVENT])
			shared_handle_and_clear[SHARED_GLINK_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_glink_mask), eq_dynamic_entry);
	}

	if (sts1 & sts1_glink_mask) {
		idx = (ffs(sts0 & sts1_glink_mask)) + 3; /* +3 to distinguish name from sts0 */
		dev_err(hdev->dev, "Received GLINK_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_GLINK_EVENT])
			shared_handle_and_clear[SHARED_GLINK_EVENT](hdev, die,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_glink_mask), eq_dynamic_entry);
	}

	/* Handle PLL */
	if (sts1 & sts1_pll_mask) {
		idx = ffs(sts0 & sts1_pll_mask) - 7;
		dev_err(hdev->dev, "Received PLL_SEI[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PLL_EVENT])
			shared_handle_and_clear[SHARED_PLL_EVENT](hdev, die,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_pll_mask), eq_dynamic_entry);
	}

	/* Clear interrupt - W1C */
	WREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_STS_0 + offset, sts0);
	WREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_STS_1 + offset, sts1);
	WREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_STS_2 + offset, sts2);
	WREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MSG_PENDING + offset, 1);
}

static void gaudi3_hdcore_serr_event_info(struct hl_device *hdev, u32 offset, u32 die,
						u32 aggr_hdcore)
{
	u32 sts0, sts1;

	/* No Hdcore serr event should reach host, but for our pldm purposes need
	 * to mask such events in case occurred
	 */
	sts0 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_SERR_INT_MSG_STS_0 + offset);
	sts1 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_SERR_INT_MSG_STS_1 + offset);

	/* Clear interrupt - W1C */
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_SERR_INT_MSG_MASK_0 + offset, sts0);
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_SERR_INT_MSG_MASK_1 + offset, sts1);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_SERR_INT_MSG_STS_0 + offset, sts0);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_SERR_INT_MSG_STS_1 + offset, sts1);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_SERR_INT_MSG_MSG_PENDING + offset, 1);
}

static void gaudi3_shared_serr_event_info(struct hl_device *hdev, u32 die)
{
	u32 offset, sts0;

	offset = die * DIE_OFFSET;
	sts0 = RREG32(mmD0_CPU_INT_AGG_SHARED_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_SERR_INT_MSG_STS + offset);

	/* Mask events first, if there is a handler then it'll be unmasked back */
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_SERR_INT_MSG_MASK + offset, sts0);

	/* No SERR event should notify lkd for now */

	/* Clear interrupt - W1C */
	WREG32(mmD0_CPU_INT_AGG_SHARED_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_SERR_INT_MSG_STS + offset, sts0);
	WREG32(mmD0_CPU_INT_AGG_SHARED_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_SERR_INT_MSG_MSG_PENDING + offset, 1);
}

static void gaudi3_hdcore_derr_event_info(struct hl_device *hdev, u32 offset, u32 die,
					  u32 aggr_hdcore,
					  struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 hdcore, instance, idx, sts0, sts1,
			sts0_mme_mask = GENMASK(10, 0),
			sts0_tpc_mask = GENMASK(19, 11),
			sts0_rot_mask = GENMASK(21, 20),
			sts0_cs_mask = GENMASK(29, 22),
			sts0_stlb_mask = BIT(30),
			sts0_rtr_mask = BIT(31),
			sts1_rtr_mask = GENMASK(6, 0),
			sts1_edma_mask = BIT(7),
			sts1_hbm_mask = GENMASK(11, 8),
			sts1_sob_mask = BIT(12),
			sts1_arcfarm_mask = BIT(13),
			sts1_dec_mask = GENMASK(17, 14),
			sts1_hif_mask = BIT(18);

	sts0 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_STS_0 + offset);
	sts1 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_STS_1 + offset);

	/* Mask events first, if there is a handler then it'll be unmasked back */
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset, sts0);
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset, sts1);

	/* Handle MME */
	if (sts0 & sts0_mme_mask) {
		idx = ffs(sts0 & sts0_mme_mask) - 1;
		dev_err(hdev->dev, "Received MME_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_MME_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
								__ffs(sts0 & sts0_mme_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_MME_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_mme_mask), eq_dynamic_entry);
		}
	}

	/* Handle TPC */
	if (sts0 & sts0_tpc_mask) {
		idx = ffs(sts0 & sts0_tpc_mask) - 12;
		dev_err(hdev->dev, "Received TPC_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_TPC_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
								__ffs(sts0 & sts0_tpc_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_TPC_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_tpc_mask), eq_dynamic_entry);
		}
	}

	/* Handle ROT */
	if (sts0 & sts0_rot_mask) {
		idx = ffs(sts0 & sts0_rot_mask) - 21;
		dev_err(hdev->dev, "Received ROT_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_ROT_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
								__ffs(sts0 & sts0_rot_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_ROT_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_rot_mask), eq_dynamic_entry);
		}
	}

	/* Handle CS */
	if (sts0 & sts0_cs_mask) {
		idx = ffs(sts0 & sts0_cs_mask) - 23;
		dev_err(hdev->dev, "Received CS_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_CS_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
								__ffs(sts0 & sts0_cs_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_CS_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_cs_mask), eq_dynamic_entry);
		}
	}

	/* Handle STLB */
	if (sts0 & sts0_stlb_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received STLB_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_STLB_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
								__ffs(sts0 & sts0_stlb_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_STLB_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_stlb_mask), eq_dynamic_entry);
		}
	}

	/* Handle RTR */
	if (sts0 & sts0_rtr_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received RTR_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_RTR_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
								__ffs(sts0 & sts0_rtr_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_RTR_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_rtr_mask), eq_dynamic_entry);
		}
	}

	if (sts1 & sts1_rtr_mask) {
		idx = ffs(sts1 & sts1_rtr_mask);
		dev_err(hdev->dev, "Received RTR_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_RTR_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
								32 + __ffs(sts1 & sts1_rtr_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_RTR_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_rtr_mask), eq_dynamic_entry);
		}
	}

	/* Handle EDMA */
	if (sts1 & sts1_edma_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received EDMA_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_EDMA_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
								32 + __ffs(sts1 & sts1_edma_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_EDMA_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_edma_mask), eq_dynamic_entry);
		}
	}

	/* Handle HBM */
	if (sts1 & sts1_hbm_mask) {
		idx = ffs(sts0 & sts1_hbm_mask) - 9;
		dev_err(hdev->dev, "Received HBM_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_HBM_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
								32 + __ffs(sts1 & sts1_hbm_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_HBM_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_hbm_mask), eq_dynamic_entry);
		}
	}

	/* Handle SOB */
	if (sts1 & sts1_sob_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received SOB_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_SOB_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
								32 + __ffs(sts1 & sts1_sob_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_SOB_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_sob_mask), eq_dynamic_entry);
		}
	}

	/* Handle ARCFARM */
	if (sts1 & sts1_arcfarm_mask) {
		idx = 0;
		dev_err(hdev->dev,
			"Received ARC_FARM_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_ARCFARM_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
							32 + __ffs(sts1 & sts1_arcfarm_mask),
							&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_ARCFARM_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_arcfarm_mask), eq_dynamic_entry);
		}
	}

	/* Handle DEC */
	if (sts1 & sts1_dec_mask) {
		idx = ffs(sts0 & sts1_dec_mask) - 15;
		dev_err(hdev->dev, "Received DEC_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_DEC_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
								32 + __ffs(sts1 & sts1_dec_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_DEC_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_dec_mask), eq_dynamic_entry);
		}
	}

	/* Handle HIF */
	if (sts1 & sts1_hif_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received HIF_DERR[%u] interrupt in D%u_CPU_INT_AGG_HDCORE%u\n",
			idx, die, aggr_hdcore);

		if (hdcore_handle_and_clear[HDCORE_HIF_EVENT]) {
			hdcore_aggr_intr_to_hdcore_and_instance(die, aggr_hdcore, ERR_GRP_DERR,
								32 + __ffs(sts1 & sts1_hif_mask),
								&hdcore, &instance);
			hdcore_handle_and_clear[HDCORE_HIF_EVENT](hdev, die, hdcore, instance,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_hif_mask), eq_dynamic_entry);
		}
	}

	/* Clear interrupt - W1C */
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_STS_0 + offset, sts0);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_STS_1 + offset, sts1);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MSG_PENDING + offset, 1);
}

static void gaudi3_shared_derr_event_info(struct hl_device *hdev, u32 die,
					  struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 offset, sts0, idx,
			sts0_cpu_mask = BIT(0),
			sts0_pcie_mask = GENMASK(3, 1),
			sts0_nic_mask = GENMASK(9, 4),
			sts0_nch_mask = GENMASK(11, 10),
			sts0_pmmu_mask = BIT(12),
			sts0_pdma_mask = BIT(13),
			sts0_parc_mask = BIT(15),
			sts0_d2d_mask = GENMASK(17, 16),
			sts0_rtr_mask = GENMASK(21, 18);

	offset = die * DIE_OFFSET;
	sts0 = RREG32(mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_DERR_INT_MSG_STS + offset);

	/* Mask events first, if there is a handler then it'll be unmasked back */
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset, sts0);

	/* Handle CPU */
	if (sts0 & sts0_cpu_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received CPU_DERR[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_CPU_EVENT])
			shared_handle_and_clear[SHARED_CPU_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_cpu_mask), eq_dynamic_entry);
	}

	/* Handle PCIE */
	if (sts0 & sts0_pcie_mask) {
		idx = ffs(sts0 & sts0_pcie_mask) - 2;
		dev_err(hdev->dev, "Received PCIE_DERR[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PCIE_EVENT])
			shared_handle_and_clear[SHARED_PCIE_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_pcie_mask), eq_dynamic_entry);
	}

	/* Handle NIC */
	if (sts0 & sts0_nic_mask) {
		idx = ffs(sts0 & sts0_nic_mask) - 5;
		dev_err(hdev->dev, "Received NIC_DERR[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_NIC_EVENT])
			shared_handle_and_clear[SHARED_NIC_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_nic_mask), eq_dynamic_entry);
	}

	/* Handle NCH */
	if (sts0 & sts0_nch_mask) {
		idx = ffs(sts0 & sts0_nch_mask) - 11;
		dev_err(hdev->dev, "Received NCH_DERR[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_NCH_EVENT])
			shared_handle_and_clear[SHARED_NCH_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_nch_mask), eq_dynamic_entry);
	}

	/* Handle PMMU */
	if (sts0 & sts0_pmmu_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received PMMU_DERR[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PMMU_EVENT])
			shared_handle_and_clear[SHARED_PMMU_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_pmmu_mask), eq_dynamic_entry);
	}

	/* Handle PDMA */
	if (sts0 & sts0_pdma_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received PDMA_DERR[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PDMA_EVENT])
			shared_handle_and_clear[SHARED_PDMA_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_pdma_mask), eq_dynamic_entry);
	}

	/* Handle PARC */
	if (sts0 & sts0_parc_mask) {
		idx = 0;
		dev_err(hdev->dev, "Received PARC_DERR[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_PARC_EVENT])
			shared_handle_and_clear[SHARED_PARC_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_parc_mask), eq_dynamic_entry);
	}

	/* Handle D2D */
	if (sts0 & sts0_d2d_mask) {
		idx = ffs(sts0 & sts0_d2d_mask) - 17;
		dev_err(hdev->dev, "Received D2D_DERR[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_D2D_EVENT])
			shared_handle_and_clear[SHARED_D2D_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_d2d_mask), eq_dynamic_entry);
	}

	/* Handle RTR */
	if (sts0 & sts0_rtr_mask) {
		idx = ffs(sts0 & sts0_rtr_mask) - 19;
		dev_err(hdev->dev, "Received RTR_DERR[%u] interrupt in D%u_CPU_INT_AGG_SHARED\n",
			idx, die);

		if (shared_handle_and_clear[SHARED_RTR_EVENT])
			shared_handle_and_clear[SHARED_RTR_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_rtr_mask), eq_dynamic_entry);
	}

	/* Clear interrupt - W1C */
	WREG32(mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_DERR_INT_MSG_STS + offset, sts0);
	WREG32(mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_DERR_INT_MSG_MSG_PENDING + offset, 1);
}

static void gaudi3_handle_cpu_aggr(struct hl_device *hdev, u32 intr_aggr_irq, u32 die,
				   struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 offset, event_type,	intr_block_idx;
	bool is_shared;

	intr_block_idx = (intr_aggr_irq - die * INTR_AGGR_NUM_OF_MSIX_VECTORS_PER_DIE) /
				CPU_INTR_AGGR_NUM_OF_EVENTS_GROUPS;
	is_shared = (intr_block_idx == CPU_INTR_AGGR_NUM_OF_EVENTS_GROUPS);
	event_type = (intr_aggr_irq - die * INTR_AGGR_NUM_OF_MSIX_VECTORS_PER_DIE) %
			CPU_INTR_AGGR_NUM_OF_EVENTS_GROUPS;
	offset = die * DIE_OFFSET + intr_block_idx * INTR_AGG_BLOCK_OFFSET;

	/* Note:
	 * since we don't know how to clear the actual event yet
	 * driver will log the event once then it'll mask it in aggregator
	 * to avoid getting endless messages.
	 * Shared aggregator might have multiple events raised at the same time,
	 * since driver will get only on interrupt so we need to check all possible
	 * errors type.
	 */
	if (is_shared) {
		gaudi3_shared_derr_event_info(hdev, die, eq_dynamic_entry);
		gaudi3_shared_serr_event_info(hdev, die);
		gaudi3_shared_sei_event_info(hdev, die, eq_dynamic_entry);
		gaudi3_shared_spi_event_info(hdev, die, eq_dynamic_entry);
		return;
	}

	switch (event_type) {
	case 0:
		gaudi3_hdcore_derr_event_info(hdev, offset, die, intr_block_idx, eq_dynamic_entry);
		break;
	case 1:
		gaudi3_hdcore_serr_event_info(hdev, offset, die, intr_block_idx);
		break;
	case 2:
		gaudi3_hdcore_sei_event_info(hdev, offset, die, intr_block_idx, eq_dynamic_entry);
		break;
	case 3:
		gaudi3_hdcore_spi_event_info(hdev, offset, die, intr_block_idx, eq_dynamic_entry);
		break;
	};
}

static void gaudi3_handle_qm_sw_undef_cmd_err(struct hl_device *hdev, u32 qm_reg_base,
						struct hl_eq_qm_undef_cmd_data *undef_op_data)
{
	bool is_arc_cq;
	u32 cp_sts, lo;
	u64 hi;

	cp_sts = RREG32(qm_reg_base + mmQMAN_CP_STS);
	is_arc_cq = !!FIELD_GET(QMAN_CP_STS_CUR_CQ_M, cp_sts); /* 0 - legacy CQ, 1 - ARC_CQ */

	if (is_arc_cq) {
		undef_op_data->cq_type = CQ_TYPE_ARC;
		undef_op_data->cq_tsize =
				cpu_to_le32(RREG32(qm_reg_base + mmQMAN_ARC_CQ_TSIZE_STS));
		lo = RREG32(qm_reg_base + mmQMAN_ARC_CQ_PTR_LO_STS);
		hi = RREG32(qm_reg_base + mmQMAN_ARC_CQ_PTR_HI_STS);
		undef_op_data->cq_ptr = cpu_to_le64((hi << 32) | lo);
	} else {
		undef_op_data->cq_type = CQ_TYPE_LEGACY;
		undef_op_data->cq_tsize = cpu_to_le32(RREG32(qm_reg_base + mmQMAN_CQ_TSIZE_STS));
		lo = RREG32(qm_reg_base + mmQMAN_CQ_PTR_LO_STS);
		hi = RREG32(qm_reg_base + mmQMAN_CQ_PTR_HI_STS);
		undef_op_data->cq_ptr = cpu_to_le64((hi << 32) | lo);
	}

	lo = RREG32(qm_reg_base + mmQMAN_CP_CURRENT_INST_LO);
	hi = RREG32(qm_reg_base + mmQMAN_CP_CURRENT_INST_HI);
	undef_op_data->cp_curr_inst = cpu_to_le64((hi << 32) | lo);
}

static void gaudi3_handle_qm_sw_event(struct hl_device *hdev,
					const struct qm_sw_event_info *qm_info,
					struct hl_eq_dynamic_entry *eq_dynamic_entry)
{
	u32 glbl_err_sts, hd_in_die = qm_info->hd % NUM_OF_HDCORES_PER_DIE;
	struct hl_eq_qm_sei_data *qm_data;

	switch (qm_info->comp) {
	case INT_COMP_TYPE_MME:
		dev_err(hdev->dev, "Received QM SW event for D%u_HD%u_MME\n",
			qm_info->die, hd_in_die);
		qm_data = &eq_dynamic_entry->mme_sei_data.control_data.qm_data;
		eq_dynamic_entry->mme_sei_data.type = MME_DATA_TYPE_CTRL;
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_mme_sei_data));
		break;
	case INT_COMP_TYPE_TPC:
		dev_err(hdev->dev, "Received QM SW event for D%u_HD%u_TPC%u\n",
			qm_info->die, hd_in_die, qm_info->instance);
		qm_data = &eq_dynamic_entry->tpc_sei_data.qm_data;
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_tpc_sei_data));
		break;
	case INT_COMP_TYPE_ROT:
		dev_err(hdev->dev, "Received QM SW event for D%u_HD%u_ROT%u\n",
			qm_info->die, hd_in_die, qm_info->instance);
		qm_data = &eq_dynamic_entry->rot_sei_data.qm_data;
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_rot_sei_data));
		break;
	case INT_COMP_TYPE_EDMA:
		dev_err(hdev->dev, "Received QM SW event for D%u_HD%u_EDMA%u\n",
			qm_info->die, hd_in_die, qm_info->instance);
		qm_data = &eq_dynamic_entry->edma_sei_data.qm_data[qm_info->instance];
		eq_dynamic_entry->hdr.size = cpu_to_le16(sizeof(struct hl_eq_edma_sei_data));
		break;
	default:
		dev_err(hdev->dev, "Received QM SW event with invalid component %u\n",
			qm_info->comp);
		return;
	}

	glbl_err_sts = RREG32(qm_info->base + mmQMAN_GLBL_ERR_STS) & QMAN_GLBL_ERR_STS_MASK;
	qm_data->qm_cause.intr_cause_data = cpu_to_le64(glbl_err_sts);

	if (glbl_err_sts & QMAN_GLBL_ERR_STS_CP_UNDEF_CMD_ERR_M)
		gaudi3_handle_qm_sw_undef_cmd_err(hdev, qm_info->base, &qm_data->undef_op_data);
}

irqreturn_t hl_pldm_irq_handler(int irq, void *arg)
{
	struct gaudi3_pldm_msix_info *msix_info = arg;
	struct hl_device *hdev = msix_info->hdev;
	struct hl_eq_dynamic_entry *eq_dynamic_entry = &msix_info->eq_dyn_entry;
	struct eq_agg_header_params params = {};
	const struct qm_sw_event_info *qm_info;
	u32 intr_aggr_irq, die;
	bool is_psoc;

	/*
	 * Aggregator      Relative IRQ
	 * ==========      ============
	 * D0 CPU HDCORE0: IRQs 0..3
	 * ...
	 * D0 CPU HDCORE3: IRQs 12..15
	 * D0 CPU SHARED:  IRQs 16..19
	 * D0 PSOC:        IRQs 20..53
	 * ---
	 * D1 CPU HDCORE0: IRQs 54..57
	 * ...
	 * D1 CPU HDCORE3: IRQs 66..69
	 * D1 CPU SHARED:  IRQs 70..73
	 * D1 PSOC:        IRQs 74..107
	 */
	intr_aggr_irq = irq - hl_irq_vector(hdev, GAUDI3_PLDM_AGGR_IRQ_FIRST);
	if (intr_aggr_irq < INTR_AGGR_NUM_OF_MSIX_VECTORS) {
		die = (intr_aggr_irq >= INTR_AGGR_NUM_OF_MSIX_VECTORS_PER_DIE) ? 1 : 0;
		is_psoc = (intr_aggr_irq - die * INTR_AGGR_NUM_OF_MSIX_VECTORS_PER_DIE) >=
				CPU_INTR_AGGR_NUM_OF_MSIX_VECTORS;

		if (is_psoc)
			gaudi3_handle_psoc_aggr(hdev, intr_aggr_irq, die, eq_dynamic_entry);
		else
			gaudi3_handle_cpu_aggr(hdev, intr_aggr_irq, die, eq_dynamic_entry);
	/* the case of QM SW interrupt */
	} else {
		intr_aggr_irq -= INTR_AGGR_NUM_OF_MSIX_VECTORS;
		qm_info = &gaudi3_qm_irq_map_table[intr_aggr_irq];
		gaudi3_handle_qm_sw_event(hdev, qm_info, eq_dynamic_entry);

		params.component_type = qm_info->comp;
		params.grp_type = ERR_GRP_SEI;
		params.die = qm_info->die;
		params.hdcore = qm_info->hd % NUM_OF_HDCORES_PER_DIE;
		params.instance = qm_info->comp == INT_COMP_TYPE_EDMA ? 0 : qm_info->instance;
		prepare_eq_dynamic_entry_agg_header(eq_dynamic_entry, &params);

		gaudi3_handle_eqe(hdev, eq_dynamic_entry);
	}

	return IRQ_HANDLED;
}

static int gaudi3_map_pll_index_to_pll_offset_index(int pll_index)
{
	switch (pll_index) {
	case HL_GAUDI3_CPU_PLL: return GAUDI3_D0_G4_PCI_PLL;
	case HL_GAUDI3_PCI_PLL: return GAUDI3_D0_G4_PCI_PLL;
	case HL_GAUDI3_NIC_PLL: return GAUDI3_D0_G2_NIC_PLL;
	case HL_GAUDI3_DMA_PLL: return GAUDI3_D0_G1_DMA_PLL;
	case HL_GAUDI3_MESH_PLL: return GAUDI3_D0_G0_MESH_PLL;
	case HL_GAUDI3_MME_PLL: return GAUDI3_D0_G0_MME_PLL;
	case HL_GAUDI3_TPC_PLL: return GAUDI3_D0_G0_TPC_PLL;
	case HL_GAUDI3_HBM_PLL: return GAUDI3_D0_G0_HBM_PLL;
	case HL_GAUDI3_VID_PLL: return GAUDI3_D0_G2_MEDIA_PLL;
	case HL_GAUDI3_D2D_PLL: return GAUDI3_D0_G0_D2D_PLL;
	case HL_GAUDI3_CS_PLL: return GAUDI3_D0_G1_CS_PLL;
	case HL_GAUDI3_C2C_PLL: return GAUDI3_D0_G1_C2C_PLL;
	case HL_GAUDI3_NCH_PLL: return GAUDI3_D0_G4_NCH_PLL;
	case HL_GAUDI3_C2M_PLL: return GAUDI3_D0_G5_C2M_PLL;
	default: return -EINVAL;
	}
}

static int calculate_pll_freq(struct hl_device *hdev, u64 pll_base_address, u16 *pll_freq_arr)
{
	u32 refdiv, fbdiv, postdiv1, postdiv2, frequency, cfg_div, i;

	/*
	 * Reset pll output values to 0x0;
	 */
	memset(pll_freq_arr, 0x0, sizeof(u16) * HL_PLL_NUM_OUTPUTS);

	/*
	 * Read pll CFG_DIV register
	 */
	cfg_div = RREG32(pll_base_address + mmPLL_CTRL_CFG_DIV);

	/*
	 * Extract cfg_div values
	 */
	refdiv = FIELD_GET(PLL_DIV_CFG_REFDIV_MASK, cfg_div);
	fbdiv = FIELD_GET(PLL_DIV_CFG_FBDIV_MASK, cfg_div);
	postdiv1 = FIELD_GET(PLL_DIV_CFG_POSTDIV1_MASK, cfg_div);
	postdiv2 = FIELD_GET(PLL_DIV_CFG_POSTDIV2_MASK, cfg_div);

	/*
	 * ref div must be set to 0x1, cannot be 0x0
	 */
	if (!refdiv) {
		dev_err(hdev->dev, "refdiv = 0x0 for pll base_address: 0x%llx\n", pll_base_address);
		return -EINVAL;
	}

	/*
	 * PLL Frequency formula:
	 * Fout = Fin*FBDIV/(POSTDIV1+1)/(POSTDIV2+1)/ REFDIV
	 */
	frequency = PLL_REF_CLK*fbdiv/(postdiv1 + 1)/(postdiv2 + 1)/refdiv;

	/*
	 * Calculate divisor values, we have HL_PLL_NUM_OUTPUTS output divisors.
	 */
	for (i = 0 ; i < HL_PLL_NUM_OUTPUTS ; i++) {
		u32 div_en = RREG32(pll_base_address + mmPLL_CTRL_DIV_EN_0 + i * 4);

		if (div_en) {
			u32 div_sel, div_factor;

			div_sel = RREG32(pll_base_address + mmPLL_CTRL_DIV_SEL_0 + i * 4);

			switch (div_sel) {
			case DIV_SEL_REF_CLK:
				pll_freq_arr[i] = PLL_REF_CLK;
				break;
			case DIV_SEL_PLL_CLK:
				pll_freq_arr[i] = frequency;
				break;
			case DIV_SEL_DIVIDED_REF:
				div_factor = RREG32(pll_base_address +
					mmPLL_CTRL_DIV_FACTOR_0 + i * 4);
				pll_freq_arr[i] = PLL_REF_CLK / (div_factor + 1);
				break;
			case DIV_SEL_DIVIDED_PLL:
				div_factor = RREG32(pll_base_address +
					mmPLL_CTRL_DIV_FACTOR_0 + i * 4);
				pll_freq_arr[i] = frequency / (div_factor + 1);
				break;
			default:
				break;
			};
		}
	}

	return 0;
}

int gaudi3_pll_info_get(struct hl_device *hdev, u32 pll_index, u16 *pll_freq_arr)
{
	int index;

	if (pll_index >= HL_GAUDI3_PLL_MAX)
		return -EINVAL;

	index = gaudi3_map_pll_index_to_pll_offset_index(pll_index);
	if (index < 0 || index >= GAUDI3_PLL_MAX)
		return -EINVAL;

	return calculate_pll_freq(hdev, gaudi3_pll_block_bases[index], pll_freq_arr);
}

static void gaudi3_disable_pqm_clock_gating(struct hl_device *hdev, int die, int inst, u32 offset,
					struct iterate_module_ctx *ctx)
{
	u32 reg_base = mmD0_SPDMA0_CMN_B_BASE + offset;

	RMWREG32(reg_base + mmPDMA_CMN_B_PQM_CMN_B_CGM_CFG, 0x0, PDMA_CMN_B_PQM_CMN_B_CGM_CFG_EN_M);
}

static void gaudi3_disable_pqms_clock_gating(struct hl_device *hdev)
{
	struct iterate_module_ctx iter_ctx = {
		.fn = gaudi3_disable_pqm_clock_gating
	};

	gaudi3_iterate_pdma_grps(hdev, &iter_ctx);
}

static void gaudi3_disable_qman_clock_gating(struct hl_device *hdev, int block, int inst,
						u32 offset, struct iterate_module_ctx *ctx)
{
	u32 first_qm_reg_base = *(u32 *) ctx->data, qm_reg_base = first_qm_reg_base + offset;

	RMWREG32(qm_reg_base + QM_CGM_BASE_OFFSET + mmQMAN_CGM_CFG, 0x0, QMAN_CGM_CFG_EN_M);
}

static void gaudi3_disable_qmans_clock_gating(struct hl_device *hdev)
{
	u32 first_qm_reg_base;
	struct iterate_module_ctx iter_ctx = {
		.fn = gaudi3_disable_qman_clock_gating,
		.data = &first_qm_reg_base
	};

	first_qm_reg_base = mmHD1_SEDMA0_QM_BASE;
	gaudi3_iterate_edmas(hdev, &iter_ctx);

	first_qm_reg_base = mmHD0_TPC0_QM_BASE;
	gaudi3_iterate_tpcs(hdev, &iter_ctx);

	first_qm_reg_base = mmHD0_MME_QM_BASE;
	gaudi3_iterate_mmes(hdev, &iter_ctx);

	first_qm_reg_base = mmHD1_ROT0_QM_BASE;
	gaudi3_iterate_rotators(hdev, &iter_ctx);
}

static void gaudi3_disable_clock_gating(struct hl_device *hdev)
{
	gaudi3_disable_pqms_clock_gating(hdev);
	gaudi3_disable_qmans_clock_gating(hdev);
	gaudi3_set_decoders_clock_gating(hdev, false);
}

static void gaudi3_stop_decoder_engine_fw_config(struct hl_device *hdev, int hdcore, int inst,
							u32 offset, struct iterate_module_ctx *ctx)
{
	u32 vdec_brdg_ctrl_reg_base = mmHD0_VDEC0_BRDG_CTRL_BASE + offset;

	/* Mask idle signals from IP */
	WREG32(vdec_brdg_ctrl_reg_base + mmVDEC_BRDG_CTRL_IDLE_MASK,
			FIELD_PREP(VDEC_BRDG_CTRL_IDLE_MASK_VAL_M, 0x7));

	/* Disable VCMD normal interrupts */
	WREG32(vdec_brdg_ctrl_reg_base + mmVDEC_BRDG_CTRL_NRM_INTR_MASK,
			FIELD_PREP(VDEC_BRDG_CTRL_NRM_INTR_MASK_VAL_M, 0x1));
}

static void gaudi3_stop_decoder_fw_config(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct iterate_module_ctx iter_ctx = {
		.fn = gaudi3_stop_decoder_engine_fw_config
	};

	if (!(gaudi3->hw_cap_dec_initialized & HW_CAP_DEC_MASK))
		return;

	gaudi3_iterate_decoders(hdev, &iter_ctx);
}

void gaudi3_halt_engines_fw_config(struct hl_device *hdev)
{
	if (hdev->fw_components & FW_TYPE_BOOT_CPU)
		return;

	gaudi3_disable_clock_gating(hdev);
	gaudi3_stop_decoder_fw_config(hdev);
}

static void gaudi3_init_dtlb_nrtr_eco_fixup(struct hl_device *hdev,
			int block, int inst, u32 offset, struct iterate_module_ctx *ctx)
{
	u32 val;

	if (block == 0)
		val = 0x32103210;
	else
		val = 0x76547654;

	WREG32(offset + mmDTLB_HBM_PHY_MAP, val);
}

void gaudi3_dtlb_nrtr_eco_fixup(struct hl_device *hdev)
{
	struct iterate_module_ctx ctx = {};

	ctx.fn = gaudi3_init_dtlb_nrtr_eco_fixup;
	gaudi3_iterate_nrtr_dtlbs(hdev, &ctx);
}

/****************************************
 * Privileged RR LBW configuration tables
 ****************************************/

/* configuration table for LBW "privileged short" range type */
static struct rr_range rr_lbw_priv_short_ranges[] = {
	{
		.min = mmD0_CPU_TIMESTAMP_BASE,
		.max = mmD0_CPU_TIMESTAMP_BASE + HL_BLOCK_SIZE,
		.rd = true,
		.wr = true
	},
};

/* configuration table for LBW "privileged short #13" range type */
static struct rr_range rr_lbw_priv_short_13_ranges[] = {
	{
		.min = RR_LBW_PRIV_RANGE_SHORT_13_DISABLED_VAL,
		.max = RR_LBW_PRIV_RANGE_SHORT_13_DISABLED_VAL,
		.rd = false,
		.wr = false
	},
};

/* configuration table for LBW "privileged" range type */
static struct rr_range rr_lbw_priv_ranges[] = {
	{
		.min = mmD0_GIC_BASE,
		.max = (mmD0_GIC_BASE + mmGIC_DISTRIBUTOR_5_GICD_SETSPI_NSR +
				RR_LBW_LONG_ADDR_BYTE_GRANULARITY) & RR_LBW_LONG_ADDR_MASK,
		.rd = true,
		.wr = true
	},
};

/* verify no overflow in the privileged LBW ranges */
static_assert(ARRAY_SIZE(rr_lbw_priv_short_ranges) <= RR_LBW_PRIV_SHORT_NUM_RANGES);
static_assert(ARRAY_SIZE(rr_lbw_priv_short_13_ranges) <= RR_LBW_PRIV_SHORT_13_NUM_RANGES);
static_assert(ARRAY_SIZE(rr_lbw_priv_ranges) <= RR_LBW_PRIV_NUM_RANGES);

/* configuration table for privileged LBW ranges */
static struct rr_type_config rr_lbw_priv_config_array[RR_LBW_RANGE_TYPE_NUMBER] = {
	[RR_LBW_RANGE_TYPE_SEC_SHORT] = {
		.num_ranges = 0,
	},
	[RR_LBW_RANGE_TYPE_SEC] = {
		.num_ranges = 0,
	},
	[RR_LBW_RANGE_TYPE_PRIV_SHORT] = {
		.ranges = rr_lbw_priv_short_ranges,
		.num_ranges = ARRAY_SIZE(rr_lbw_priv_short_ranges),
		.lbw_prop = {
			.en_off = RTR_CTRL_RR_LBW_PRIV_RANGE_EN_SHORT_OFFSET,
			.min_off = RTR_CTRL_RR_LBW_PRIV_RANGE_MIN_SHORT_OFFSET,
			.max_off = RTR_CTRL_RR_LBW_PRIV_RANGE_MAX_SHORT_OFFSET,
			.addr_mask = RR_LBW_SHORT_ADDR_MASK_SHORT,
		},
	},
	[RR_LBW_RANGE_TYPE_PRIV_SHORT_13] = {
		.ranges = rr_lbw_priv_short_13_ranges,
		.num_ranges = ARRAY_SIZE(rr_lbw_priv_short_13_ranges),
		.lbw_prop = {
			.en_off = RTR_CTRL_RR_LBW_PRIV_RANGE_EN_SHORT_13_OFFSET,
			.min_off = RTR_CTRL_RR_LBW_PRIV_RANGE_MIN_SHORT_13_OFFSET,
			.max_off = RTR_CTRL_RR_LBW_PRIV_RANGE_MAX_SHORT_13_OFFSET,
			.addr_mask = RR_LBW_SHORT_ADDR_MASK_SHORT,
		},
	},
	[RR_LBW_RANGE_TYPE_PRIV] = {
		.ranges = rr_lbw_priv_ranges,
		.num_ranges = ARRAY_SIZE(rr_lbw_priv_ranges),
		.lbw_prop = {
			.en_off = RTR_CTRL_RR_LBW_PRIV_RANGE_EN_OFFSET,
			.min_off = RTR_CTRL_RR_LBW_PRIV_RANGE_MIN_OFFSET,
			.max_off = RTR_CTRL_RR_LBW_PRIV_RANGE_MAX_OFFSET,
			.addr_mask = RR_LBW_LONG_ADDR_MASK,
		},
	},
};

/****************************************
 * Privileged RR HBW configuration tables
 ****************************************/

/* configuration table for HBW "privileged" range type */
static struct rr_range rr_hbw_priv_ranges[] = {
};

/* configuration table for HBW "privileged #7" range type */
static struct rr_range rr_hbw_priv_7_ranges[] = {
	{
		.min = RR_HBW_PRIV_RANGE_7_DISABLED_VAL,
		.max = RR_HBW_PRIV_RANGE_7_DISABLED_VAL,
		.rd = false,
		.wr = false
	},
};

/* verify no overflow in privileged HBW ranges */
static_assert(ARRAY_SIZE(rr_hbw_priv_ranges) <= RR_HBW_PRIV_NUM_RANGES);
static_assert(ARRAY_SIZE(rr_hbw_priv_7_ranges) <= RR_HBW_PRIV_7_NUM_RANGES);

/* configuration table for privileged HBW ranges */
static struct rr_type_config rr_hbw_priv_config_array[RR_HBW_RANGE_TYPE_NUMBER] = {
	[RR_HBW_RANGE_TYPE_SEC] = {
		.num_ranges = 0,
	},
	[RR_HBW_RANGE_TYPE_PRIV] = {
		.ranges = rr_hbw_priv_ranges,
		.num_ranges = ARRAY_SIZE(rr_hbw_priv_ranges),
		.hbw_prop = {
			.en_off = RTR_CTRL_RR_HBW_PRIV_RANGE_EN_OFFSET,
			.min_hi_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MIN_HI_OFFSET,
			.min_lo_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MIN_LO_OFFSET,
			.max_hi_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MAX_HI_OFFSET,
			.max_lo_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MAX_LO_OFFSET,
		},
	},
	[RR_HBW_RANGE_TYPE_PRIV_7] = {
		.ranges = rr_hbw_priv_7_ranges,
		.num_ranges = ARRAY_SIZE(rr_hbw_priv_7_ranges),
		.hbw_prop = {
			.en_off = RTR_CTRL_RR_HBW_PRIV_RANGE_EN_7_OFFSET,
			.min_hi_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MIN_HI_7_OFFSET,
			.min_lo_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MIN_LO_7_OFFSET,
			.max_hi_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MAX_HI_7_OFFSET,
			.max_lo_off = RTR_CTRL_RR_HBW_PRIV_RANGE_MAX_LO_7_OFFSET,
		},
	},
};

static void gaudi3_init_lbw_hbw_range_registers_privileged(struct hl_device *hdev)
{
	struct rtr_ctrl_rr_config rr_config = {
		.lbw_config_array = rr_lbw_priv_config_array,
		.hbw_config_array = rr_hbw_priv_config_array
	};
	struct iterate_module_ctx ctx = {
		.fn = gaudi3_rtr_ctrl_config_rr,
		.data = &rr_config,
	};

	dev_dbg(hdev->dev, "Configure privileged LBW/HBW RRs\n");

	gaudi3_iterate_rtr_ctrls(hdev, &ctx);
}

int gaudi3_init_security_privileged(struct hl_device *hdev)
{
	int rc;

	/*
	 * TODO:
	 * Unify the 2 if statements and place them here once hl_iterate_special_blocks() supports
	 * single die (SW-169903).
	 */
	if (hdev->asic_prop.fw_security_enabled)
		return 0;

	gaudi3_init_lbw_hbw_range_registers_privileged(hdev);

	if (!hdev->priv_security_enable)
		return 0;

	rc = hl_init_pb_security(hdev, true);
	if (rc) {
		dev_err(hdev->dev, "Configuring privileged PBs failed!\n");
		return rc;
	}

	return 0;
}
