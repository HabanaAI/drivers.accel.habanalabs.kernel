// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3P.h"
#include "gaudi3_nic.h"
#include "gaudi3_masks.h"
#include "../include/gaudi3/gaudi3_async_events.h"

#include <linux/bitrev.h>

#define GAUDI3_PLL_TIMEOUT_USEC		10000 /* 10ms */

/* TODO: the below time work but not tuned. should be fixed with SW-91407 */
#define GAUDI3_D2D_DPHY_CTRL_POLL_INTERVAL_USEC	2000000		/* 2sec */
#define GAUDI3_D2D_DPHY_CTRL_POLL_TIMEOUT_USEC	600000000ULL	/* 600sec */

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

/* @TODO: remove this CAP bit (SW-115621)
 * Temporary FW capability bit used for D2D preboot integration
 */
#define CPU_BOOT_DEV_STS0_D2D_INIT_EN		30

/* A DUMMY block isn't a regular block, but in fact a block with a manually
 * configured block response, and used by PCIE 'Fabric Serialization' feature.
 * Although listed in SOL, it has no 'specs' record associated to it.
 * It's configured for DIE0 only, since DIE1 PCIE/features are disabled.
 */
#define mmD0_PIF_DUMMY_LBW_BLK_BASE		0xC41C000ull

struct hl_pldm_eqe_work {
	struct work_struct	eq_work;
	struct hl_device	*hdev;
	u32			sw_irq;
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

typedef void (*shared_aggr_handle_and_clear)(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask);

typedef void (*hdcore_aggr_handle_and_clear)(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 event_mask);

static void handle_and_clear_pcie_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask);
static void handle_and_clear_psoc_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask);
static void handle_and_clear_pmmu_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask);
static void handle_and_clear_pdma_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask);
static void handle_and_clear_tpc_events(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask);
static void handle_and_clear_mme_events(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask);
static void handle_and_clear_stlb_events(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask);
static void handle_and_clear_arc_farm_events(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask);
static void handle_and_clear_nic_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask);
static void handle_and_clear_cs_events(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask);
static void handle_and_clear_hbm_events(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask);

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
	NULL, /* HDCORE_ROT_EVENT */
	handle_and_clear_cs_events, /* HDCORE_CS_EVENT */
	handle_and_clear_stlb_events, /* HDCORE_STLB_EVENT */
	handle_and_clear_hbm_events, /* HDCORE_HBM_EVENT */
	NULL, /* HDCORE_SOB_EVENT */
	handle_and_clear_arc_farm_events, /* HDCORE_ARCFARM_EVENT */
	NULL, /* HDCORE_DEC_EVENT */
	NULL, /* HDCORE_DUP_EVENT */
	NULL, /* HDCORE_EDMA_EVENT */
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
	WREG32(offset + RTR_CTRL_HBW_SCRAM_SRAM_MODE_OFFSET, 0);
}

static void gaudi3_set_cache_mode_dtlb(struct hl_device *hdev, int block, int inst,
						u32 offset, struct iterate_module_ctx *ctx)
{
	WREG32(offset + DTLB_RR_GLBL_PA_END1_OFFSET, 0);
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

	/* If preboot simulator exist- he will perform those configs- skip */
	if ((hdev->fw_components & FW_TYPE_BOOT_CPU) && (hdev->asic_type == ASIC_GAUDI3_SIM_ARC))
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

static void gaudi3_sram_init_cslice(struct hl_device *hdev, int block, int inst,
						u32 offset, struct iterate_module_ctx *ctx)
{
	u32 val = RREG32(offset + mmCACHE_MAIN_CNTRL_MAIN);

	/* enable SRAM mode */
	val |= CACHE_MAIN_CNTRL_MAIN_SRAM_MODE_EN_M;

	/* disable data FW (bug H9-5279) */
	val &= (~CACHE_MAIN_CNTRL_MAIN_DATA_FORWARD_EN_M);

	WREG32(offset + mmCACHE_MAIN_CNTRL_MAIN, val);
}

static void gaudi3_sram_init(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct iterate_module_ctx ctx = {
		.fn = gaudi3_sram_init_cslice,
	};

	if (hdev->cache_enable)
		return;

	if (gaudi3->hw_cap_initialized & HW_CAP_SRAM)
		return;

	gaudi3_iterate_cache_slices(hdev, &ctx);

	gaudi3->hw_cap_initialized |= HW_CAP_SRAM;
}

static void gaudi3_reset_config(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	u64 offset;
	u32 die;

	gaudi3->psoc_reset = (hdev->fw_loader.fw_comp_loaded & FW_TYPE_PREBOOT_CPU) ?
							true : false;
	for (die = 0; die < hdev->asic_prop.num_of_dies; die++) {
		offset = die * DIE_OFFSET;

		if (gaudi3->psoc_reset) {
			/* Reset PSOC and PSOC ARCs during hard-reset */
			WREG32(mmD0_PSOC_RESET_CONF_BASE + offset +
					mmPSOC_RESET_CONF_PSOC_SW_RST_CFG,
					PSOC_RESET_CONF_PSOC_SW_RST_CFG_EN_M);
			WREG32(mmD0_PSOC_RESET_CONF_BASE + offset +
					mmPSOC_RESET_CONF_ARC_SW_RST_CFG,
					PSOC_RESET_CONF_ARC_SW_RST_CFG_EN_M);
		}

		/* Reset PMMU during hard-reset */
		WREG32(mmD0_PSOC_RESET_CONF_BASE + offset +
				mmPSOC_RESET_CONF_PMMU_SW_RST_CFG,
				PSOC_RESET_CONF_PMMU_SW_RST_CFG_EN_M);

		/* Reset NIC_QMAN during soft-reset */
		WREG32(mmD0_PSOC_RESET_CONF_BASE + offset +
				mmPSOC_RESET_CONF_NIC_QMAN_SOFT_RST_CFG,
				PSOC_RESET_CONF_NIC_QMAN_SOFT_RST_CFG_EN_M);

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

		rc = hl_poll_timeout_elbi(
				hdev,
				poll_addr,
				reg_val,
				(reg_val & 0x1),
				GAUDI3_D2D_DPHY_CTRL_POLL_INTERVAL_USEC,
				GAUDI3_D2D_DPHY_CTRL_POLL_TIMEOUT_USEC);

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
			addr = mmHD0_RRTR0_DTLB_UNIT_ID + (hdcore * HDCORE_OFFSET) +
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
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	int rc = 0;

	if (hdev->asic_prop.num_of_dies != MAX_NUM_OF_DIES)
		return 0;

	/* simulator does not support the D2D PHY init code */
	if (!hdev->pdev)
		return 0;

	if ((hdev->fw_components & FW_TYPE_PREBOOT_CPU) &&
			(prop->fw_preboot_cpu_boot_dev_sts0 & CPU_BOOT_DEV_STS0_D2D_INIT_EN)) {
		dev_info(hdev->dev, "D2D configs skipped!\n");
		return 0;
	}

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
	struct hl_nic_macro *nic_macro;
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
		nic_macro = &hdev->nic.nic_macros[i];
		if (!(hdev->nic_ports_mask & gaudi3_nic_get_macro_ports_mask(nic_macro)))
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
		nic_macro = &hdev->nic.nic_macros[NIC_NUM_MACROS_PER_DIE + i];
		if (!(hdev->nic_ports_mask & gaudi3_nic_get_macro_ports_mask(nic_macro)))
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
	if ((hdev->fw_components & FW_TYPE_BOOT_CPU) && !hdev->fw_cfg_skip)
		return;

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
	WREG32(reg_base + mmQMAN_ARC_AUX_CBU_EARLY_BRESP_EN, 0x1);
	WREG32(reg_base + mmQMAN_ARC_AUX_LBU_EARLY_BRESP_EN, 0x1);

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

}

void gaudi3_reset_arc(struct hl_device *hdev, u32 cpu_id)
{
	u32 reg_base, reg_val;

	/* skip arc init if already done by FW */
	if ((hdev->fw_components & FW_TYPE_BOOT_CPU) && !hdev->fw_cfg_skip)
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

			/* TODO - all registers below are SECURE. Consider moving them elsewhere */

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
	WREG32(reg_base + mmEDMA_CMN_CFG1,
			FIELD_PREP(HD1_SEDMA0_CMN_CFG1_STOP_ON_ERR_M, 0x1));
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
	u32 sob_base_hi;

	sob_base_hi = upper_32_bits(CFG_BAR_BASE + mmHD0_SYNC_MNGR_OBJS_BASE +
					mmSOB_OBJS_SOB_OBJ_0_0);
	WREG32(reg_base + mmTPC_SM_BASE_ADDRESS_HIGH, sob_base_hi);

	/* Mask stall for QM interrupt.
	 * Stall is configured for all unmasked interrupts, so use the same mask as for interrupts.
	 */
	WREG32(reg_base + mmTPC_STALL_ON_ERR_MASK_0, TPC_INTR_MASK_0_MASK);

	WREG32(reg_base + mmTPC_STALL_ON_ERR, FIELD_PREP(TPC_STALL_ON_ERR_STALL_ENABLE_M, 0x1));

	WREG32(reg_base + mmTPC_TENSOR_SMT_PRIV, 0);
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

static void gaudi3_init_mme_ctrl_lo_fw_config(struct hl_device *hdev, u32 reg_base)
{
	u32 reg_val;

	WREG32(reg_base + mmMME_CTRL_LO_REDUN_PSOC_SEL_SEC, 0x0);
	WREG32(reg_base + mmMME_CTRL_LO_EU0_REDUN_CLK_EN, GAUDI3_EU0_REDUN_CLK_EN);
	WREG32(reg_base + mmMME_CTRL_LO_EU1_REDUN_CLK_EN, GAUDI3_EU1_REDUN_CLK_EN);
	WREG32(reg_base + mmMME_CTRL_LO_REDUN,
			FIELD_PREP(MME_CTRL_LO_REDUN_EU0_M, 0x10) |
			FIELD_PREP(MME_CTRL_LO_REDUN_EU1_M, 0x10));

	WREG32(reg_base + mmMME_CTRL_LO_EU_ISOLATION_DIS,
			FIELD_PREP(MME_CTRL_LO_EU_ISOLATION_DIS_EU0_M, 0x1) |
			FIELD_PREP(MME_CTRL_LO_EU_ISOLATION_DIS_EU1_M, 0x1));

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
	/* Unmask idle signals from IP */
	WREG32(reg_base + mmVDEC_BRDG_CTRL_IDLE_MASK, 0x0);

	/* Mask SPI normal interrupt because it is received and handled through MSI-X */
	WREG32(reg_base + mmVDEC_BRDG_CTRL_CAUSE_INTR_MASK,
			FIELD_PREP(VDEC_BRDG_CTRL_CAUSE_INTR_NRM_SPI_M, 0x1));

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

static void gaudi3_init_nic_qman_fw_config(struct hl_device *hdev, int die, int inst, u32 offset,
						struct iterate_module_ctx *ctx)
{
	u32 qm_reg_base;

	qm_reg_base = mmD0_NIC0_QM_BASE + offset;
	gaudi3_init_qman_fw_config(hdev, qm_reg_base);
}

static void gaudi3_init_nic_qmans_fw_config(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct iterate_module_ctx iter_ctx = {
		.fn = gaudi3_init_nic_qman_fw_config
	};

	if (!hdev->nic_ports_mask)
		return;

	if ((gaudi3->hw_cap_nic_initialized & HW_CAP_NIC_MASK) == HW_CAP_NIC_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing NICs QM [F/W configuration]\n");

	gaudi3_iterate_nics(hdev, &iter_ctx);
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
	u32 reg_base, reg_val;
	int i;

	/* Override is for hdcores 1 to last, i.e. excluding 0 */
	for (i = 1 ; i < props->num_of_hdcores ; ++i) {
		reg_base = mmHD0_SYNC_MNGR_MSTR_IF_AXPROT_LBW_BASE + i * HDCORE_OFFSET;

		/* Val = 0 => non privileged */
		reg_val = FIELD_PREP(MSTR_IF_AXPROT_LBW_ARPROT_0_OVRD_OVRD_EN_M, 0x1) |
				FIELD_PREP(MSTR_IF_AXPROT_LBW_ARPROT_0_OVRD_VAL_M, 0x0);
		WREG32(reg_base + mmMSTR_IF_AXPROT_LBW_ARPROT_0_OVRD, reg_val);

		reg_val = FIELD_PREP(MSTR_IF_AXPROT_LBW_AWPROT_0_OVRD_OVRD_EN_M, 0x1) |
				FIELD_PREP(MSTR_IF_AXPROT_LBW_AWPROT_0_OVRD_VAL_M, 0x0);
		WREG32(reg_base + mmMSTR_IF_AXPROT_LBW_AWPROT_0_OVRD, reg_val);

		/* Val = 1 => non secure */
		reg_val = FIELD_PREP(MSTR_IF_AXPROT_LBW_ARPROT_1_OVRD_OVRD_EN_M, 0x1) |
				FIELD_PREP(MSTR_IF_AXPROT_LBW_ARPROT_1_OVRD_VAL_M, 0x1);
		WREG32(reg_base + mmMSTR_IF_AXPROT_LBW_ARPROT_1_OVRD, reg_val);

		reg_val = FIELD_PREP(MSTR_IF_AXPROT_LBW_AWPROT_1_OVRD_OVRD_EN_M, 0x1) |
				FIELD_PREP(MSTR_IF_AXPROT_LBW_AWPROT_1_OVRD_VAL_M, 0x1);
		WREG32(reg_base + mmMSTR_IF_AXPROT_LBW_AWPROT_1_OVRD, reg_val);
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
	u64 supported_pages_mask = hdev->asic_prop.dmmu.supported_pages_mask;
	u32 reg_val = 0, val;
	u8 page_type;

	for (page_type = 0; page_type < HMMU_TLB_MAX_SUPPORTED_PAGE_TYPES; page_type++) {
		if (!supported_pages_mask)
			return reg_val;

		val = tlb_get_next_page_ctrl_cfg(&supported_pages_mask);

		switch (page_type) {
		case 0:
			reg_val |=
				FIELD_PREP(DTLB_CNTRL_PAGE_SIZE_TYPE1_PAGE_SIZE_M, val);
			break;
		case 1:
			reg_val |=
				FIELD_PREP(DTLB_CNTRL_PAGE_SIZE_TYPE2_PAGE_SIZE_M, val);
			break;
		case 2:
			reg_val |=
				FIELD_PREP(DTLB_CNTRL_PAGE_SIZE_TYPE3_PAGE_SIZE_M, val);
			break;
		}
	}

	return reg_val;
}

static void gaudi3_dtlb_init(struct hl_device *hdev, int block, int inst, u32 offset,
				struct iterate_module_ctx *ctx)
{
	struct tlb_init_data *init_data = ctx->data;

	/* the iterator already supplies the DTLB base in offset so it will be used as dtlb base */

	/* set DTLB unit ID */
	WREG32(offset + DTLB_UNIT_ID_OFFSET, inst);

	/* set supported page sizes */
	WREG32(offset + DTLB_CNTRL_PAGE_SIZE_OFFSET, init_data->cntrl_page_size);

	/*
	 * set HBM params: single HBM memory size and number of HBMs
	 * TODO: this should be modified if we have HBM binning. In addition,
	 * this is RMW to avoid overriding the value in DCORE0_HAS_*HBM field
	 * written by FW in the binning phase.
	 */
	RMWREG32_SHIFTED(offset + DTLB_HBM_CONF_OFFSET,
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
	gaudi3_lbw_dup_group_push(hdev,	GAUDI3_DUP_GRP_STLB_BASE, STLB_CNTRL_PAGE_SIZE_OFFSET, val);

	/* unmask STLB interrupts */
	for (i = 0; i < hdev->asic_prop.num_of_hdcores ; ++i) {
		WREG32(mmHD0_STLB_BASE + STLB_INTR_SPI_MASK_OFFSET + (i * HDCORE_OFFSET),
			FIELD_PREP(STLB_INTR_SPI_MASK_FAULT_UNMAPPED_MSK_M, 1) |
			FIELD_PREP(STLB_INTR_SPI_MASK_FAULT_PERMISSION_MSK_M, 1) |
			FIELD_PREP(STLB_INTR_SPI_MASK_FAULT_PTW_DATA_MSK_M, 1) |
			FIELD_PREP(STLB_INTR_SPI_MASK_MAINT_QUEUE_FULL_MSK_M, 1) |
			FIELD_PREP(STLB_INTR_SPI_MASK_MAINT_PREFETCH_FAIL_MSK_M, 1));
		WREG32(mmHD0_STLB_BASE + STLB_INTR_SEI_MASK_OFFSET + (i * HDCORE_OFFSET),
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

	if (!hdev->dram_enable || (hdev->mmu_enable != MMU_EN_ALL))
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
	RMWREG32(mmD0_PMMU_HBW_STLB_BASE, 0x20,
			PSTLB_MEM_CACHE_CONFIG_CACHE_HOP_PREFETCH_EN_M);
}

static void gaudi3_init_mmu_fw_config(struct hl_device *hdev)
{
	/* contains only HMMU config but exist as a placeholder for PMMU */
	if (!hdev->mmu_enable)
		return;

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

void gaudi3_ac_start(struct hl_device *hdev, u32 etr_idx)
{
	struct hl_etr_buf_store *store = &hdev->etr_buf_store;
	u64 base = gaudi3_etr_ac_config[etr_idx].ac_off;

	store->etr_tracer[etr_idx].ac_started = 1;

	/* TODO: this has to be done by FW, add mailbox for that */
	RMWREG32(mmD0_NCH_AC_BASE + mmAUTONOMOUS_CONTROL_CTRL + base, 1,
			AUTONOMOUS_CONTROL_CTRL_EN_M);
}

void gaudi3_ac_stop(struct hl_device *hdev, u32 etr_idx)
{
	struct hl_etr_buf_store *store = &hdev->etr_buf_store;
	u64 base = gaudi3_etr_ac_config[etr_idx].ac_off;

	store->etr_tracer[etr_idx].ac_started = 0;

	/* TODO: this has to be done by FW, add mailbox for that */
	RMWREG32(mmD0_NCH_AC_BASE + mmAUTONOMOUS_CONTROL_CTRL + base, 0,
			AUTONOMOUS_CONTROL_CTRL_EN_M);
}

int gaudi3_is_ac_started(struct hl_device *hdev, u32 etr_idx)
{
	struct hl_etr_buf_store *store = &hdev->etr_buf_store;

	return store->etr_tracer[etr_idx].ac_started;
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
	/* TODO (SW-108260): Temporary allow those configs for SIM_GAUDI3_ARC */
	if ((hdev->fw_components & FW_TYPE_BOOT_CPU) && (hdev->asic_type != ASIC_GAUDI3_SIM_ARC))
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
	/* TODO (SW-108260): Temporary allow those configs for SIM_GAUDI3_ARC */
	if ((hdev->fw_components & FW_TYPE_BOOT_CPU) && (hdev->asic_type != ASIC_GAUDI3_SIM_ARC))
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
	/* TODO (SW-108260): Temporary allow those configs for SIM_GAUDI3_ARC */
	if ((hdev->fw_components & FW_TYPE_BOOT_CPU) && (hdev->asic_type != ASIC_GAUDI3_SIM_ARC))
		return;

	hdev->asic_funcs->set_binning_masks(hdev);

	gaudi3_print_sol_config_version(hdev);
	gaudi3_reset_config(hdev);
	gaudi3_sram_init(hdev);
	gaudi3_init_credits(hdev);
	gaudi3_set_isolation(hdev, false, false);
	gaudi3_init_cbc_fw_config(hdev);
	gaudi3_init_mstr_if_fw_config(hdev);
	gaudi3_init_pdma_fw_config(hdev);
	gaudi3_init_edma_fw_config(hdev);
	gaudi3_init_tpc_fw_config(hdev);
	gaudi3_init_mme_fw_config(hdev);
	gaudi3_init_rotator_fw_config(hdev);
	gaudi3_init_decoder_fw_config(hdev);
	gaudi3_init_nic_qmans_fw_config(hdev);
	gaudi3_init_sm_axprot_overrides(hdev);
	gaudi3_enable_clock_gating(hdev);
	gaudi3_init_odp(hdev);
	gaudi3_init_regulators(hdev);
	gaudi3_init_interrupt_coalescing(hdev);
	gaudi3_init_mmu_fw_config(hdev);
	gaudi3_ac_program_all(hdev);
	gaudi3_enable_ptw_bypass(hdev);
	gaudi3_init_qos(hdev);

	gaudi3_nic_macros_fw_config(hdev);

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
	/* Set device to handle FLR by H/W as we will put the device
	 * CPU to halt mode
	 */
	/* TODO: handle DIE1 code */
	WREG32(mmD0_PCIE_AUX_BASE + mmPCIE_AUX_FLR_CTRL,
			(PCIE_AUX_FLR_CTRL_HW_CTRL_M | PCIE_AUX_FLR_CTRL_INT_MASK_M));

	gaudi3_send_hard_reset_cmd(hdev);

	/* Restart BTL/BLR upon hard-reset */
	WREG32(mmD0_PSOC_GLOBAL_CONF_BASE + mmGLOBAL_CONF_BOOT_SEQ_RE_START, 0x770);
}

void gaudi3_execute_reset_no_fw(struct hl_device *hdev, u32 reset_sleep_ms, bool hard_reset)
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

	dev_dbg(hdev->dev, "Driver issued %s reset command, sleeping %ums\n",
					hard_reset ? "HARD" : "SOFT", reset_sleep_ms);
}

void gaudi3_enable_interrupt_aggr_msgs(struct hl_device *hdev)
{
	u32 offset, die, intr_agg, irq, i, msix_addr, sts0, sts1, sts2;
	struct asic_fixed_properties *props = &hdev->asic_prop;

	if (!hdev->pldm || !hdev->enable_intr_aggr)
		return;

	irq = GAUDI3_PLDM_IRQ_FIRST;

	/*
	 * Enable interrupt aggregators messages for all aggregators in CPU
	 * and PSOC blocks.
	 */
	for (die = 0 ; die < props->num_of_dies ; ++die) {
		/* Both die0 and die1 aggregators should write to die0 PCIE_MSIX.
		 * Due to a H/W bug (H9-5161), there is a bit flip in bit[23] of the configured
		 * address for die1 CPU aggregators.
		 * As this bit is the die select, the W/A for these aggregators is writing the other
		 * die address, i.e. in this case: die1 PCIE_MSIX instead of die0 PCIE_MSIX.
		 */
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

		/* PSOC aggregators */
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
		WREG32(mmD0_PARC_MSG2WIRE_PSOC_0_BASE + mmMSG2WIRE_PSOC_0_MASK_0 + offset, 0);
		WREG32(mmD0_PARC_MSG2WIRE_PSOC_0_BASE + mmMSG2WIRE_PSOC_0_MASK_1 + offset, 0);
		WREG32(mmD0_PARC_MSG2WIRE_SH_HD_BASE + mmMSG2WIRE_SH_HD_MASK_0 + offset, 0);
		WREG32(mmD0_PARC_MSG2WIRE_SH_HD_BASE + mmMSG2WIRE_SH_HD_MASK_1 + offset, 0);
	}
}

static void gaudi3_handle_psoc_aggr(struct hl_device *hdev, u32 intr_aggr_irq, u32 die)
{
	u32 parc_block_idx, offset, sts0, idx, sts0_prstn_mask = GENMASK(1, 0),
			sts0_vm_alarm_mask = GENMASK(3, 0);
	char str[512];

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
		snprintf(str, 512, "PSOC%u_DIE%u_HDPSOC_PRSTN%u_SPI", die, die, idx);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PSOC_EVENT])
			shared_handle_and_clear[SHARED_PSOC_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts0, 0, idx,
				mmD0_PARC_INT_AGGR_UART_COMB_BASE +
				mmINT_AGG_PSOC_UART_COMB_MASK + offset,
				~(sts0 & sts0_prstn_mask));

	}

	/* Handle VM_ALARMA_COMB Aggr */
	if (parc_block_idx == 18) {
		idx = ffs(sts0 & sts0_vm_alarm_mask) - 1;
		snprintf(str, 512, "PSOC%u_DIE%u_HDPSOC_VM_ALARMA%u_SPI", die, die, idx);
		dev_err(hdev->dev, "Received %s event\n", str);
	}

	/* Handle VM_ALARMA_COMB Aggr */
	if (parc_block_idx == 24) {
		idx = 0;
		snprintf(str, 512, "PSOC%u_DIE%u_HDPSOC_VM_ALARMA%u_SPI", die, die, idx);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (sts0 & BIT(6))
			dev_err(hdev->dev, "PSOC AXI drain event\n");
	}

	WREG32(mmD0_PARC_INT_AGGR_UART_COMB_BASE +
			mmINT_AGG_PSOC_UART_COMB_STS + offset, sts0);
	WREG32(mmD0_PARC_INT_AGGR_UART_COMB_BASE +
			mmINT_AGG_PSOC_UART_COMB_MSG_PENDING + offset, 1);
}

static const enum gaudi3_async_event_id
cs_derr_events_id_map[MAX_NUM_OF_DIES][8 * NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_CS0_DIE0_HD0_DERR,
		GAUDI3_EVENT_CS1_DIE0_HD0_DERR,
		GAUDI3_EVENT_CS2_DIE0_HD0_DERR,
		GAUDI3_EVENT_CS3_DIE0_HD0_DERR,
		GAUDI3_EVENT_CS4_DIE0_HD0_DERR,
		GAUDI3_EVENT_CS5_DIE0_HD0_DERR,
		GAUDI3_EVENT_CS6_DIE0_HD0_DERR,
		GAUDI3_EVENT_CS7_DIE0_HD0_DERR,
		GAUDI3_EVENT_CS0_DIE0_HD1_DERR,
		GAUDI3_EVENT_CS1_DIE0_HD1_DERR,
		GAUDI3_EVENT_CS2_DIE0_HD1_DERR,
		GAUDI3_EVENT_CS3_DIE0_HD1_DERR,
		GAUDI3_EVENT_CS4_DIE0_HD1_DERR,
		GAUDI3_EVENT_CS5_DIE0_HD1_DERR,
		GAUDI3_EVENT_CS6_DIE0_HD1_DERR,
		GAUDI3_EVENT_CS7_DIE0_HD1_DERR,
		GAUDI3_EVENT_CS0_DIE0_HD2_DERR,
		GAUDI3_EVENT_CS1_DIE0_HD2_DERR,
		GAUDI3_EVENT_CS2_DIE0_HD2_DERR,
		GAUDI3_EVENT_CS3_DIE0_HD2_DERR,
		GAUDI3_EVENT_CS4_DIE0_HD2_DERR,
		GAUDI3_EVENT_CS5_DIE0_HD2_DERR,
		GAUDI3_EVENT_CS6_DIE0_HD2_DERR,
		GAUDI3_EVENT_CS7_DIE0_HD2_DERR,
		GAUDI3_EVENT_CS0_DIE0_HD3_DERR,
		GAUDI3_EVENT_CS1_DIE0_HD3_DERR,
		GAUDI3_EVENT_CS2_DIE0_HD3_DERR,
		GAUDI3_EVENT_CS3_DIE0_HD3_DERR,
		GAUDI3_EVENT_CS4_DIE0_HD3_DERR,
		GAUDI3_EVENT_CS5_DIE0_HD3_DERR,
		GAUDI3_EVENT_CS6_DIE0_HD3_DERR,
		GAUDI3_EVENT_CS7_DIE0_HD3_DERR},
		{GAUDI3_EVENT_CS0_DIE1_HD0_DERR,
		GAUDI3_EVENT_CS1_DIE1_HD0_DERR,
		GAUDI3_EVENT_CS2_DIE1_HD0_DERR,
		GAUDI3_EVENT_CS3_DIE1_HD0_DERR,
		GAUDI3_EVENT_CS4_DIE1_HD0_DERR,
		GAUDI3_EVENT_CS5_DIE1_HD0_DERR,
		GAUDI3_EVENT_CS6_DIE1_HD0_DERR,
		GAUDI3_EVENT_CS7_DIE1_HD0_DERR,
		GAUDI3_EVENT_CS0_DIE1_HD1_DERR,
		GAUDI3_EVENT_CS1_DIE1_HD1_DERR,
		GAUDI3_EVENT_CS2_DIE1_HD1_DERR,
		GAUDI3_EVENT_CS3_DIE1_HD1_DERR,
		GAUDI3_EVENT_CS4_DIE1_HD1_DERR,
		GAUDI3_EVENT_CS5_DIE1_HD1_DERR,
		GAUDI3_EVENT_CS6_DIE1_HD1_DERR,
		GAUDI3_EVENT_CS7_DIE1_HD1_DERR,
		GAUDI3_EVENT_CS0_DIE1_HD2_DERR,
		GAUDI3_EVENT_CS1_DIE1_HD2_DERR,
		GAUDI3_EVENT_CS2_DIE1_HD2_DERR,
		GAUDI3_EVENT_CS3_DIE1_HD2_DERR,
		GAUDI3_EVENT_CS4_DIE1_HD2_DERR,
		GAUDI3_EVENT_CS5_DIE1_HD2_DERR,
		GAUDI3_EVENT_CS6_DIE1_HD2_DERR,
		GAUDI3_EVENT_CS7_DIE1_HD2_DERR,
		GAUDI3_EVENT_CS0_DIE1_HD3_DERR,
		GAUDI3_EVENT_CS1_DIE1_HD3_DERR,
		GAUDI3_EVENT_CS2_DIE1_HD3_DERR,
		GAUDI3_EVENT_CS3_DIE1_HD3_DERR,
		GAUDI3_EVENT_CS4_DIE1_HD3_DERR,
		GAUDI3_EVENT_CS5_DIE1_HD3_DERR,
		GAUDI3_EVENT_CS6_DIE1_HD3_DERR,
		GAUDI3_EVENT_CS7_DIE1_HD3_DERR}
};

static const enum gaudi3_async_event_id
cs_sei_events_id_map[MAX_NUM_OF_DIES][8 * NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_CS0_DIE0_HD0_SEI,
		GAUDI3_EVENT_CS1_DIE0_HD0_SEI,
		GAUDI3_EVENT_CS2_DIE0_HD0_SEI,
		GAUDI3_EVENT_CS3_DIE0_HD0_SEI,
		GAUDI3_EVENT_CS4_DIE0_HD0_SEI,
		GAUDI3_EVENT_CS5_DIE0_HD0_SEI,
		GAUDI3_EVENT_CS6_DIE0_HD0_SEI,
		GAUDI3_EVENT_CS7_DIE0_HD0_SEI,
		GAUDI3_EVENT_CS0_DIE0_HD1_SEI,
		GAUDI3_EVENT_CS1_DIE0_HD1_SEI,
		GAUDI3_EVENT_CS2_DIE0_HD1_SEI,
		GAUDI3_EVENT_CS3_DIE0_HD1_SEI,
		GAUDI3_EVENT_CS4_DIE0_HD1_SEI,
		GAUDI3_EVENT_CS5_DIE0_HD1_SEI,
		GAUDI3_EVENT_CS6_DIE0_HD1_SEI,
		GAUDI3_EVENT_CS7_DIE0_HD1_SEI,
		GAUDI3_EVENT_CS0_DIE0_HD2_SEI,
		GAUDI3_EVENT_CS1_DIE0_HD2_SEI,
		GAUDI3_EVENT_CS2_DIE0_HD2_SEI,
		GAUDI3_EVENT_CS3_DIE0_HD2_SEI,
		GAUDI3_EVENT_CS4_DIE0_HD2_SEI,
		GAUDI3_EVENT_CS5_DIE0_HD2_SEI,
		GAUDI3_EVENT_CS6_DIE0_HD2_SEI,
		GAUDI3_EVENT_CS7_DIE0_HD2_SEI,
		GAUDI3_EVENT_CS0_DIE0_HD3_SEI,
		GAUDI3_EVENT_CS1_DIE0_HD3_SEI,
		GAUDI3_EVENT_CS2_DIE0_HD3_SEI,
		GAUDI3_EVENT_CS3_DIE0_HD3_SEI,
		GAUDI3_EVENT_CS4_DIE0_HD3_SEI,
		GAUDI3_EVENT_CS5_DIE0_HD3_SEI,
		GAUDI3_EVENT_CS6_DIE0_HD3_SEI,
		GAUDI3_EVENT_CS7_DIE0_HD3_SEI},
		{GAUDI3_EVENT_CS0_DIE1_HD0_SEI,
		GAUDI3_EVENT_CS1_DIE1_HD0_SEI,
		GAUDI3_EVENT_CS2_DIE1_HD0_SEI,
		GAUDI3_EVENT_CS3_DIE1_HD0_SEI,
		GAUDI3_EVENT_CS4_DIE1_HD0_SEI,
		GAUDI3_EVENT_CS5_DIE1_HD0_SEI,
		GAUDI3_EVENT_CS6_DIE1_HD0_SEI,
		GAUDI3_EVENT_CS7_DIE1_HD0_SEI,
		GAUDI3_EVENT_CS0_DIE1_HD1_SEI,
		GAUDI3_EVENT_CS1_DIE1_HD1_SEI,
		GAUDI3_EVENT_CS2_DIE1_HD1_SEI,
		GAUDI3_EVENT_CS3_DIE1_HD1_SEI,
		GAUDI3_EVENT_CS4_DIE1_HD1_SEI,
		GAUDI3_EVENT_CS5_DIE1_HD1_SEI,
		GAUDI3_EVENT_CS6_DIE1_HD1_SEI,
		GAUDI3_EVENT_CS7_DIE1_HD1_SEI,
		GAUDI3_EVENT_CS0_DIE1_HD2_SEI,
		GAUDI3_EVENT_CS1_DIE1_HD2_SEI,
		GAUDI3_EVENT_CS2_DIE1_HD2_SEI,
		GAUDI3_EVENT_CS3_DIE1_HD2_SEI,
		GAUDI3_EVENT_CS4_DIE1_HD2_SEI,
		GAUDI3_EVENT_CS5_DIE1_HD2_SEI,
		GAUDI3_EVENT_CS6_DIE1_HD2_SEI,
		GAUDI3_EVENT_CS7_DIE1_HD2_SEI,
		GAUDI3_EVENT_CS0_DIE1_HD3_SEI,
		GAUDI3_EVENT_CS1_DIE1_HD3_SEI,
		GAUDI3_EVENT_CS2_DIE1_HD3_SEI,
		GAUDI3_EVENT_CS3_DIE1_HD3_SEI,
		GAUDI3_EVENT_CS4_DIE1_HD3_SEI,
		GAUDI3_EVENT_CS5_DIE1_HD3_SEI,
		GAUDI3_EVENT_CS6_DIE1_HD3_SEI,
		GAUDI3_EVENT_CS7_DIE1_HD3_SEI}
};

static const enum gaudi3_async_event_id
cs_spi_events_id_map[MAX_NUM_OF_DIES][8 * NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_CS0_DIE0_HD0_SPI_0,
		GAUDI3_EVENT_CS1_DIE0_HD0_SPI_0,
		GAUDI3_EVENT_CS2_DIE0_HD0_SPI_0,
		GAUDI3_EVENT_CS3_DIE0_HD0_SPI_0,
		GAUDI3_EVENT_CS4_DIE0_HD0_SPI_0,
		GAUDI3_EVENT_CS5_DIE0_HD0_SPI_0,
		GAUDI3_EVENT_CS6_DIE0_HD0_SPI_0,
		GAUDI3_EVENT_CS7_DIE0_HD0_SPI_0,
		GAUDI3_EVENT_CS0_DIE0_HD1_SPI_0,
		GAUDI3_EVENT_CS1_DIE0_HD1_SPI_0,
		GAUDI3_EVENT_CS2_DIE0_HD1_SPI_0,
		GAUDI3_EVENT_CS3_DIE0_HD1_SPI_0,
		GAUDI3_EVENT_CS4_DIE0_HD1_SPI_0,
		GAUDI3_EVENT_CS5_DIE0_HD1_SPI_0,
		GAUDI3_EVENT_CS6_DIE0_HD1_SPI_0,
		GAUDI3_EVENT_CS7_DIE0_HD1_SPI_0,
		GAUDI3_EVENT_CS0_DIE0_HD2_SPI_0,
		GAUDI3_EVENT_CS1_DIE0_HD2_SPI_0,
		GAUDI3_EVENT_CS2_DIE0_HD2_SPI_0,
		GAUDI3_EVENT_CS3_DIE0_HD2_SPI_0,
		GAUDI3_EVENT_CS4_DIE0_HD2_SPI_0,
		GAUDI3_EVENT_CS5_DIE0_HD2_SPI_0,
		GAUDI3_EVENT_CS6_DIE0_HD2_SPI_0,
		GAUDI3_EVENT_CS7_DIE0_HD2_SPI_0,
		GAUDI3_EVENT_CS0_DIE0_HD3_SPI_0,
		GAUDI3_EVENT_CS1_DIE0_HD3_SPI_0,
		GAUDI3_EVENT_CS2_DIE0_HD3_SPI_0,
		GAUDI3_EVENT_CS3_DIE0_HD3_SPI_0,
		GAUDI3_EVENT_CS4_DIE0_HD3_SPI_0,
		GAUDI3_EVENT_CS5_DIE0_HD3_SPI_0,
		GAUDI3_EVENT_CS6_DIE0_HD3_SPI_0,
		GAUDI3_EVENT_CS7_DIE0_HD3_SPI_0},
		{GAUDI3_EVENT_CS0_DIE1_HD0_SPI_0,
		GAUDI3_EVENT_CS1_DIE1_HD0_SPI_0,
		GAUDI3_EVENT_CS2_DIE1_HD0_SPI_0,
		GAUDI3_EVENT_CS3_DIE1_HD0_SPI_0,
		GAUDI3_EVENT_CS4_DIE1_HD0_SPI_0,
		GAUDI3_EVENT_CS5_DIE1_HD0_SPI_0,
		GAUDI3_EVENT_CS6_DIE1_HD0_SPI_0,
		GAUDI3_EVENT_CS7_DIE1_HD0_SPI_0,
		GAUDI3_EVENT_CS0_DIE1_HD1_SPI_0,
		GAUDI3_EVENT_CS1_DIE1_HD1_SPI_0,
		GAUDI3_EVENT_CS2_DIE1_HD1_SPI_0,
		GAUDI3_EVENT_CS3_DIE1_HD1_SPI_0,
		GAUDI3_EVENT_CS4_DIE1_HD1_SPI_0,
		GAUDI3_EVENT_CS5_DIE1_HD1_SPI_0,
		GAUDI3_EVENT_CS6_DIE1_HD1_SPI_0,
		GAUDI3_EVENT_CS7_DIE1_HD1_SPI_0,
		GAUDI3_EVENT_CS0_DIE1_HD2_SPI_0,
		GAUDI3_EVENT_CS1_DIE1_HD2_SPI_0,
		GAUDI3_EVENT_CS2_DIE1_HD2_SPI_0,
		GAUDI3_EVENT_CS3_DIE1_HD2_SPI_0,
		GAUDI3_EVENT_CS4_DIE1_HD2_SPI_0,
		GAUDI3_EVENT_CS5_DIE1_HD2_SPI_0,
		GAUDI3_EVENT_CS6_DIE1_HD2_SPI_0,
		GAUDI3_EVENT_CS7_DIE1_HD2_SPI_0,
		GAUDI3_EVENT_CS0_DIE1_HD3_SPI_0,
		GAUDI3_EVENT_CS1_DIE1_HD3_SPI_0,
		GAUDI3_EVENT_CS2_DIE1_HD3_SPI_0,
		GAUDI3_EVENT_CS3_DIE1_HD3_SPI_0,
		GAUDI3_EVENT_CS4_DIE1_HD3_SPI_0,
		GAUDI3_EVENT_CS5_DIE1_HD3_SPI_0,
		GAUDI3_EVENT_CS6_DIE1_HD3_SPI_0,
		GAUDI3_EVENT_CS7_DIE1_HD3_SPI_0}
};

/* HDCORE_CS_EVENT */
static void handle_and_clear_cs_events(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask)
{
	enum gaudi3_async_event_id event_id;
	bool unmask_event_in_aggr = false;
	struct hl_eq_entry eq_entry;
	u32 index;

	memset(&eq_entry, 0, sizeof(struct hl_eq_entry));

	switch (type) {
	case ERR_GRP_SPI_ECO:
		index = (idx / 2) * (8 * hdcore);
		event_id = cs_spi_events_id_map[die][index];
		break;
	case ERR_GRP_DERR:
		index = idx + (8 * hdcore);
		event_id = cs_derr_events_id_map[die][index];
		break;
	case ERR_GRP_SEI:
		index = idx + (8 * hdcore);
		event_id = cs_sei_events_id_map[die][index];
		break;
	default:
		return;
	}

	eq_entry.hdr.ctl = cpu_to_le32(event_id << EQ_CTL_EVENT_TYPE_SHIFT);
	gaudi3_handle_eqe(hdev, &eq_entry);

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static const enum gaudi3_async_event_id
mc_derr_events_id_map[MAX_NUM_OF_DIES][4 * NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_MC0_DIE0_HD0_DERR,
		GAUDI3_EVENT_MC1_DIE0_HD0_DERR,
		GAUDI3_EVENT_MC2_DIE0_HD0_DERR,
		GAUDI3_EVENT_MC3_DIE0_HD0_DERR,
		GAUDI3_EVENT_MC0_DIE0_HD1_DERR,
		GAUDI3_EVENT_MC1_DIE0_HD1_DERR,
		GAUDI3_EVENT_MC2_DIE0_HD1_DERR,
		GAUDI3_EVENT_MC3_DIE0_HD1_DERR,
		GAUDI3_EVENT_MC0_DIE0_HD2_DERR,
		GAUDI3_EVENT_MC1_DIE0_HD2_DERR,
		GAUDI3_EVENT_MC2_DIE0_HD2_DERR,
		GAUDI3_EVENT_MC3_DIE0_HD2_DERR,
		GAUDI3_EVENT_MC0_DIE0_HD3_DERR,
		GAUDI3_EVENT_MC1_DIE0_HD3_DERR,
		GAUDI3_EVENT_MC2_DIE0_HD3_DERR,
		GAUDI3_EVENT_MC3_DIE0_HD3_DERR},
		{GAUDI3_EVENT_MC0_DIE1_HD0_DERR,
		GAUDI3_EVENT_MC1_DIE1_HD0_DERR,
		GAUDI3_EVENT_MC2_DIE1_HD0_DERR,
		GAUDI3_EVENT_MC3_DIE1_HD0_DERR,
		GAUDI3_EVENT_MC0_DIE1_HD1_DERR,
		GAUDI3_EVENT_MC1_DIE1_HD1_DERR,
		GAUDI3_EVENT_MC2_DIE1_HD1_DERR,
		GAUDI3_EVENT_MC3_DIE1_HD1_DERR,
		GAUDI3_EVENT_MC0_DIE1_HD2_DERR,
		GAUDI3_EVENT_MC1_DIE1_HD2_DERR,
		GAUDI3_EVENT_MC2_DIE1_HD2_DERR,
		GAUDI3_EVENT_MC3_DIE1_HD2_DERR,
		GAUDI3_EVENT_MC0_DIE1_HD3_DERR,
		GAUDI3_EVENT_MC1_DIE1_HD3_DERR,
		GAUDI3_EVENT_MC2_DIE1_HD3_DERR,
		GAUDI3_EVENT_MC3_DIE1_HD3_DERR}
};

static const enum gaudi3_async_event_id
mc_sei_events_id_map[MAX_NUM_OF_DIES][4 * NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_MC0_DIE0_HD0_SEI,
		GAUDI3_EVENT_MC1_DIE0_HD0_SEI,
		GAUDI3_EVENT_MC2_DIE0_HD0_SEI,
		GAUDI3_EVENT_MC3_DIE0_HD0_SEI,
		GAUDI3_EVENT_MC0_DIE0_HD1_SEI,
		GAUDI3_EVENT_MC1_DIE0_HD1_SEI,
		GAUDI3_EVENT_MC2_DIE0_HD1_SEI,
		GAUDI3_EVENT_MC3_DIE0_HD1_SEI,
		GAUDI3_EVENT_MC0_DIE0_HD2_SEI,
		GAUDI3_EVENT_MC1_DIE0_HD2_SEI,
		GAUDI3_EVENT_MC2_DIE0_HD2_SEI,
		GAUDI3_EVENT_MC3_DIE0_HD2_SEI,
		GAUDI3_EVENT_MC0_DIE0_HD3_SEI,
		GAUDI3_EVENT_MC1_DIE0_HD3_SEI,
		GAUDI3_EVENT_MC2_DIE0_HD3_SEI,
		GAUDI3_EVENT_MC3_DIE0_HD3_SEI},
		{GAUDI3_EVENT_MC0_DIE1_HD0_SEI,
		GAUDI3_EVENT_MC1_DIE1_HD0_SEI,
		GAUDI3_EVENT_MC2_DIE1_HD0_SEI,
		GAUDI3_EVENT_MC3_DIE1_HD0_SEI,
		GAUDI3_EVENT_MC0_DIE1_HD1_SEI,
		GAUDI3_EVENT_MC1_DIE1_HD1_SEI,
		GAUDI3_EVENT_MC2_DIE1_HD1_SEI,
		GAUDI3_EVENT_MC3_DIE1_HD1_SEI,
		GAUDI3_EVENT_MC0_DIE1_HD2_SEI,
		GAUDI3_EVENT_MC1_DIE1_HD2_SEI,
		GAUDI3_EVENT_MC2_DIE1_HD2_SEI,
		GAUDI3_EVENT_MC3_DIE1_HD2_SEI,
		GAUDI3_EVENT_MC0_DIE1_HD3_SEI,
		GAUDI3_EVENT_MC1_DIE1_HD3_SEI,
		GAUDI3_EVENT_MC2_DIE1_HD3_SEI,
		GAUDI3_EVENT_MC3_DIE1_HD3_SEI}
};

/* HDCORE_HBM_EVENT */
static void handle_and_clear_hbm_events(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask)
{
	enum gaudi3_async_event_id event_id;
	bool unmask_event_in_aggr = false;
	struct hl_eq_entry eq_entry;
	u32 index;

	memset(&eq_entry, 0, sizeof(struct hl_eq_entry));

	switch (type) {
	case ERR_GRP_DERR:
		index = idx + (4 * hdcore);
		event_id = mc_derr_events_id_map[die][index];
		break;
	case ERR_GRP_SEI:
		index = idx + (4 * hdcore);
		event_id = mc_sei_events_id_map[die][index];
		break;
	default:
		return;
	}

	eq_entry.hdr.ctl = cpu_to_le32(event_id << EQ_CTL_EVENT_TYPE_SHIFT);
	gaudi3_handle_eqe(hdev, &eq_entry);

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static const enum gaudi3_async_event_id pcie_spi_events_id_map[MAX_NUM_OF_DIES][6] = {
		{GAUDI3_EVENT_PCIE1_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_PCIE4_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_PCIE5_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_PCIE9_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_PCIE13_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_PCIE14_DIE0_HDSHARED_SPI},
		{GAUDI3_EVENT_PCIE1_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_PCIE4_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_PCIE5_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_PCIE9_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_PCIE13_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_PCIE14_DIE1_HDSHARED_SPI}
};

static const enum gaudi3_async_event_id pcie_derr_events_id_map[MAX_NUM_OF_DIES][3] = {
		{GAUDI3_EVENT_PCIE0_DIE0_HDSHARED_DERR,
		GAUDI3_EVENT_PCIE1_DIE0_HDSHARED_DERR,
		GAUDI3_EVENT_PCIE2_DIE0_HDSHARED_DERR},
		{GAUDI3_EVENT_PCIE0_DIE1_HDSHARED_DERR,
		GAUDI3_EVENT_PCIE1_DIE1_HDSHARED_DERR,
		GAUDI3_EVENT_PCIE2_DIE1_HDSHARED_DERR},
};

static const enum gaudi3_async_event_id pcie_sei_events_id_map[MAX_NUM_OF_DIES][2] = {
		{GAUDI3_EVENT_PCIE0_DIE0_HDSHARED_SEI,
		GAUDI3_EVENT_PCIE1_DIE0_HDSHARED_SEI},
		{GAUDI3_EVENT_PCIE0_DIE1_HDSHARED_SEI,
		GAUDI3_EVENT_PCIE1_DIE1_HDSHARED_SEI},
};

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

static void gaudi3_clear_pcie_sei_cause_events(struct hl_device *hdev, u32 die, u32 err_msk)
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

/* SHARED_PCIE_EVENT */
static void handle_and_clear_pcie_events(struct hl_device *hdev, u32 die,
						enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
						u32 aggr_mask_reg, u32 events_mask)
{
	enum gaudi3_async_event_id event_id;
	bool unmask_event_in_aggr = false;
	struct hl_eq_entry eq_entry;
	u32 err_idx = 0, err_msk;
	u64 intr_cause_data;

	if (die == 1)
		dev_err(hdev->dev, "PCIE events from DIE1 are not supported\n");

	/* If event is GAUDI3_EVENT_PCIE1_DIE0_HDSHARED_SEI, there is nothing to do in LKD.
	 * Only need to clear it in FW.
	 */
	if (idx == 1)
		WREG32(mmD0_PCIE_AUX_BASE + mmPCIE_AUX_BUS_MSTR_EN_CLR_INTR, 0x1);

	memset(&eq_entry, 0, sizeof(struct hl_eq_entry));

	switch (type) {
	case ERR_GRP_SPI_ECO:
		event_id = pcie_spi_events_id_map[die][idx];
		if (event_id == GAUDI3_EVENT_PCIE14_DIE0_HDSHARED_SPI ||
			event_id == GAUDI3_EVENT_PCIE14_DIE1_HDSHARED_SPI)
			unmask_event_in_aggr = true;
		break;
	case ERR_GRP_DERR:
		event_id = pcie_derr_events_id_map[die][idx];
		break;
	case ERR_GRP_SEI:
		event_id = pcie_sei_events_id_map[die][idx];
		/* Clear on read */
		intr_cause_data = RREG32((die * DIE_OFFSET) + mmD0_PCIE_WRAP_BASE +
						mmPCIE_WRAP_PCIE_SEI_INTR_STATUS);
		eq_entry.intr_cause.intr_cause_data = cpu_to_le64(intr_cause_data);
		unmask_event_in_aggr = true;
		break;
	default:
		return;
	}

	eq_entry.hdr.ctl = cpu_to_le32(event_id << EQ_CTL_EVENT_TYPE_SHIFT);
	gaudi3_handle_eqe(hdev, &eq_entry);

	if (type == ERR_GRP_SEI) {
		/* PCIE SEI cause register may not be fully cleared. It is possible that we need to
		 * clear other registers and clear cause register once again.
		 * Note!! there might be a corner case which is not handled:
		 * Lets say reg value before eqe is 0x4001. After eqe we clear other registers which
		 * relates for those error, and we read the cause reg again to clear it, but when we
		 * read it another cause added and now reg value is 0x4004001. This will clear the
		 * additional event without handling it.
		 */
		gaudi3_clear_pcie_sei_cause_events(hdev, die, intr_cause_data);
		/* Clearing SEI cause register once again */
		err_msk = RREG32((die * DIE_OFFSET) + mmD0_PCIE_WRAP_BASE +
					mmPCIE_WRAP_PCIE_SEI_INTR_STATUS);

		while (err_msk) {
			/* In case new event raised */
			if ((err_msk & 1) && !(intr_cause_data & 1))
				dev_err(hdev->dev,
					"PCIE SEI event %u raised after PCIE SEI handling\n",
					err_idx);
			err_idx++;
			err_msk >>= 1;
			intr_cause_data >>= 1;
		}
	}

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static const enum gaudi3_async_event_id psoc_spi_events_id_map[1][2] = {
		{GAUDI3_EVENT_PSOC0_DIE0_HDPSOC_PRSTN_SPI,
		GAUDI3_EVENT_PSOC0_DIE0_HDPSOC_PRSTN1_SPI}
};

static const enum gaudi3_async_event_id psoc_sei_events_id_map[MAX_NUM_OF_DIES][1] = {
		{GAUDI3_EVENT_PSOC0_DIE0_HDSHARED_SEI},
		{GAUDI3_EVENT_PSOC0_DIE1_HDSHARED_SEI},
};

/* SHARED_PSOC_EVENT */
static void handle_and_clear_psoc_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask)
{
	enum gaudi3_async_event_id event_id;
	bool unmask_event_in_aggr = false;
	struct hl_eq_entry eq_entry;

	memset(&eq_entry, 0, sizeof(struct hl_eq_entry));

	switch (type) {
	case ERR_GRP_SPI_ECO:
		event_id = psoc_spi_events_id_map[0][idx];
		break;
	case ERR_GRP_SEI:
		event_id = psoc_sei_events_id_map[die][idx];
		break;
	default:
		return;
	}

	eq_entry.hdr.ctl = cpu_to_le32(event_id << EQ_CTL_EVENT_TYPE_SHIFT);
	gaudi3_handle_eqe(hdev, &eq_entry);

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static const enum gaudi3_async_event_id pmmu_spi_events_id_map[MAX_NUM_OF_DIES][2] = {
		{GAUDI3_EVENT_PMMU1_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_PMMU2_DIE0_HDSHARED_SPI},
		{GAUDI3_EVENT_PMMU1_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_PMMU2_DIE1_HDSHARED_SPI},
};

static const enum gaudi3_async_event_id pmmu_derr_events_id_map[MAX_NUM_OF_DIES][1] = {
		{GAUDI3_EVENT_PMMU0_DIE0_HDSHARED_DERR},
		{GAUDI3_EVENT_PMMU0_DIE1_HDSHARED_DERR},
};

static const enum gaudi3_async_event_id pmmu_sei_events_id_map[MAX_NUM_OF_DIES][1] = {
		{GAUDI3_EVENT_PMMU1_DIE0_HDSHARED_SEI},
		{GAUDI3_EVENT_PMMU1_DIE1_HDSHARED_SEI},
};

/* SHARED_PMMU_EVENT */
static void handle_and_clear_pmmu_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask)
{
	enum gaudi3_async_event_id event_id;
	struct hl_eq_entry eq_entry;
	bool unmask_event_in_aggr = false;

	memset(&eq_entry, 0, sizeof(struct hl_eq_entry));

	switch (type) {
	case ERR_GRP_SPI_ECO:
		event_id = pmmu_spi_events_id_map[die][idx];
		unmask_event_in_aggr = true;
		break;
	case ERR_GRP_DERR:
		event_id = pmmu_derr_events_id_map[die][idx];
		break;
	case ERR_GRP_SEI:
		event_id = pmmu_sei_events_id_map[die][idx];
		break;
	default:
		return;
	}

	eq_entry.hdr.ctl = cpu_to_le32(event_id << EQ_CTL_EVENT_TYPE_SHIFT);
	gaudi3_handle_eqe(hdev, &eq_entry);

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static const enum gaudi3_async_event_id
tpc_spi_events_id_map[MAX_NUM_OF_DIES][18 * NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_TPC0_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC1_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC2_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC3_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC4_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC5_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC6_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC7_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC8_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC9_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC10_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC11_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC12_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC13_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC14_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC15_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC16_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC17_DIE0_HD0_SPI,
		GAUDI3_EVENT_TPC0_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC1_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC2_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC3_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC4_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC5_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC6_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC7_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC8_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC9_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC10_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC11_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC12_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC13_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC14_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC15_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC16_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC17_DIE0_HD1_SPI,
		GAUDI3_EVENT_TPC0_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC1_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC2_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC3_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC4_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC5_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC6_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC7_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC8_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC9_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC10_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC11_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC12_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC13_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC14_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC15_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC16_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC17_DIE0_HD2_SPI,
		GAUDI3_EVENT_TPC0_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC1_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC2_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC3_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC4_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC5_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC6_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC7_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC8_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC9_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC10_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC11_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC12_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC13_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC14_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC15_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC16_DIE0_HD3_SPI,
		GAUDI3_EVENT_TPC17_DIE0_HD3_SPI},
		{GAUDI3_EVENT_TPC0_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC1_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC2_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC3_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC4_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC5_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC6_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC7_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC8_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC9_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC10_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC11_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC12_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC13_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC14_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC15_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC16_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC17_DIE1_HD0_SPI,
		GAUDI3_EVENT_TPC0_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC1_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC2_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC3_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC4_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC5_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC6_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC7_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC8_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC9_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC10_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC11_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC12_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC13_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC14_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC15_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC16_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC17_DIE1_HD1_SPI,
		GAUDI3_EVENT_TPC0_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC1_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC2_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC3_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC4_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC5_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC6_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC7_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC8_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC9_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC10_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC11_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC12_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC13_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC14_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC15_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC16_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC17_DIE1_HD2_SPI,
		GAUDI3_EVENT_TPC0_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC1_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC2_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC3_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC4_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC5_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC6_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC7_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC8_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC9_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC10_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC11_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC12_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC13_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC14_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC15_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC16_DIE1_HD3_SPI,
		GAUDI3_EVENT_TPC17_DIE1_HD3_SPI}
};

static const enum gaudi3_async_event_id
tpc_derr_events_id_map[MAX_NUM_OF_DIES][9 * NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_TPC0_DIE0_HD0_DERR,
		GAUDI3_EVENT_TPC1_DIE0_HD0_DERR,
		GAUDI3_EVENT_TPC2_DIE0_HD0_DERR,
		GAUDI3_EVENT_TPC3_DIE0_HD0_DERR,
		GAUDI3_EVENT_TPC4_DIE0_HD0_DERR,
		GAUDI3_EVENT_TPC5_DIE0_HD0_DERR,
		GAUDI3_EVENT_TPC6_DIE0_HD0_DERR,
		GAUDI3_EVENT_TPC7_DIE0_HD0_DERR,
		GAUDI3_EVENT_TPC8_DIE0_HD0_DERR,
		GAUDI3_EVENT_TPC0_DIE0_HD1_DERR,
		GAUDI3_EVENT_TPC1_DIE0_HD1_DERR,
		GAUDI3_EVENT_TPC2_DIE0_HD1_DERR,
		GAUDI3_EVENT_TPC3_DIE0_HD1_DERR,
		GAUDI3_EVENT_TPC4_DIE0_HD1_DERR,
		GAUDI3_EVENT_TPC5_DIE0_HD1_DERR,
		GAUDI3_EVENT_TPC6_DIE0_HD1_DERR,
		GAUDI3_EVENT_TPC7_DIE0_HD1_DERR,
		GAUDI3_EVENT_TPC8_DIE0_HD1_DERR,
		GAUDI3_EVENT_TPC0_DIE0_HD2_DERR,
		GAUDI3_EVENT_TPC1_DIE0_HD2_DERR,
		GAUDI3_EVENT_TPC2_DIE0_HD2_DERR,
		GAUDI3_EVENT_TPC3_DIE0_HD2_DERR,
		GAUDI3_EVENT_TPC4_DIE0_HD2_DERR,
		GAUDI3_EVENT_TPC5_DIE0_HD2_DERR,
		GAUDI3_EVENT_TPC6_DIE0_HD2_DERR,
		GAUDI3_EVENT_TPC7_DIE0_HD2_DERR,
		GAUDI3_EVENT_TPC8_DIE0_HD2_DERR,
		GAUDI3_EVENT_TPC0_DIE0_HD3_DERR,
		GAUDI3_EVENT_TPC1_DIE0_HD3_DERR,
		GAUDI3_EVENT_TPC2_DIE0_HD3_DERR,
		GAUDI3_EVENT_TPC3_DIE0_HD3_DERR,
		GAUDI3_EVENT_TPC4_DIE0_HD3_DERR,
		GAUDI3_EVENT_TPC5_DIE0_HD3_DERR,
		GAUDI3_EVENT_TPC6_DIE0_HD3_DERR,
		GAUDI3_EVENT_TPC7_DIE0_HD3_DERR,
		GAUDI3_EVENT_TPC8_DIE0_HD3_DERR},
		{GAUDI3_EVENT_TPC0_DIE1_HD0_DERR,
		GAUDI3_EVENT_TPC1_DIE1_HD0_DERR,
		GAUDI3_EVENT_TPC2_DIE1_HD0_DERR,
		GAUDI3_EVENT_TPC3_DIE1_HD0_DERR,
		GAUDI3_EVENT_TPC4_DIE1_HD0_DERR,
		GAUDI3_EVENT_TPC5_DIE1_HD0_DERR,
		GAUDI3_EVENT_TPC6_DIE1_HD0_DERR,
		GAUDI3_EVENT_TPC7_DIE1_HD0_DERR,
		GAUDI3_EVENT_TPC8_DIE1_HD0_DERR,
		GAUDI3_EVENT_TPC0_DIE1_HD1_DERR,
		GAUDI3_EVENT_TPC1_DIE1_HD1_DERR,
		GAUDI3_EVENT_TPC2_DIE1_HD1_DERR,
		GAUDI3_EVENT_TPC3_DIE1_HD1_DERR,
		GAUDI3_EVENT_TPC4_DIE1_HD1_DERR,
		GAUDI3_EVENT_TPC5_DIE1_HD1_DERR,
		GAUDI3_EVENT_TPC6_DIE1_HD1_DERR,
		GAUDI3_EVENT_TPC7_DIE1_HD1_DERR,
		GAUDI3_EVENT_TPC8_DIE1_HD1_DERR,
		GAUDI3_EVENT_TPC0_DIE1_HD2_DERR,
		GAUDI3_EVENT_TPC1_DIE1_HD2_DERR,
		GAUDI3_EVENT_TPC2_DIE1_HD2_DERR,
		GAUDI3_EVENT_TPC3_DIE1_HD2_DERR,
		GAUDI3_EVENT_TPC4_DIE1_HD2_DERR,
		GAUDI3_EVENT_TPC5_DIE1_HD2_DERR,
		GAUDI3_EVENT_TPC6_DIE1_HD2_DERR,
		GAUDI3_EVENT_TPC7_DIE1_HD2_DERR,
		GAUDI3_EVENT_TPC8_DIE1_HD2_DERR,
		GAUDI3_EVENT_TPC0_DIE1_HD3_DERR,
		GAUDI3_EVENT_TPC1_DIE1_HD3_DERR,
		GAUDI3_EVENT_TPC2_DIE1_HD3_DERR,
		GAUDI3_EVENT_TPC3_DIE1_HD3_DERR,
		GAUDI3_EVENT_TPC4_DIE1_HD3_DERR,
		GAUDI3_EVENT_TPC5_DIE1_HD3_DERR,
		GAUDI3_EVENT_TPC6_DIE1_HD3_DERR,
		GAUDI3_EVENT_TPC7_DIE1_HD3_DERR,
		GAUDI3_EVENT_TPC8_DIE1_HD3_DERR}
};

static const enum gaudi3_async_event_id
tpc_sei_events_id_map[MAX_NUM_OF_DIES][9 * NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_TPC0_DIE0_HD0_SEI,
		GAUDI3_EVENT_TPC1_DIE0_HD0_SEI,
		GAUDI3_EVENT_TPC2_DIE0_HD0_SEI,
		GAUDI3_EVENT_TPC3_DIE0_HD0_SEI,
		GAUDI3_EVENT_TPC4_DIE0_HD0_SEI,
		GAUDI3_EVENT_TPC5_DIE0_HD0_SEI,
		GAUDI3_EVENT_TPC6_DIE0_HD0_SEI,
		GAUDI3_EVENT_TPC7_DIE0_HD0_SEI,
		GAUDI3_EVENT_TPC8_DIE0_HD0_SEI,
		GAUDI3_EVENT_TPC0_DIE0_HD1_SEI,
		GAUDI3_EVENT_TPC1_DIE0_HD1_SEI,
		GAUDI3_EVENT_TPC2_DIE0_HD1_SEI,
		GAUDI3_EVENT_TPC3_DIE0_HD1_SEI,
		GAUDI3_EVENT_TPC4_DIE0_HD1_SEI,
		GAUDI3_EVENT_TPC5_DIE0_HD1_SEI,
		GAUDI3_EVENT_TPC6_DIE0_HD1_SEI,
		GAUDI3_EVENT_TPC7_DIE0_HD1_SEI,
		GAUDI3_EVENT_TPC8_DIE0_HD1_SEI,
		GAUDI3_EVENT_TPC0_DIE0_HD2_SEI,
		GAUDI3_EVENT_TPC1_DIE0_HD2_SEI,
		GAUDI3_EVENT_TPC2_DIE0_HD2_SEI,
		GAUDI3_EVENT_TPC3_DIE0_HD2_SEI,
		GAUDI3_EVENT_TPC4_DIE0_HD2_SEI,
		GAUDI3_EVENT_TPC5_DIE0_HD2_SEI,
		GAUDI3_EVENT_TPC6_DIE0_HD2_SEI,
		GAUDI3_EVENT_TPC7_DIE0_HD2_SEI,
		GAUDI3_EVENT_TPC8_DIE0_HD2_SEI,
		GAUDI3_EVENT_TPC0_DIE0_HD3_SEI,
		GAUDI3_EVENT_TPC1_DIE0_HD3_SEI,
		GAUDI3_EVENT_TPC2_DIE0_HD3_SEI,
		GAUDI3_EVENT_TPC3_DIE0_HD3_SEI,
		GAUDI3_EVENT_TPC4_DIE0_HD3_SEI,
		GAUDI3_EVENT_TPC5_DIE0_HD3_SEI,
		GAUDI3_EVENT_TPC6_DIE0_HD3_SEI,
		GAUDI3_EVENT_TPC7_DIE0_HD3_SEI,
		GAUDI3_EVENT_TPC8_DIE0_HD3_SEI},
		{GAUDI3_EVENT_TPC0_DIE1_HD0_SEI,
		GAUDI3_EVENT_TPC1_DIE1_HD0_SEI,
		GAUDI3_EVENT_TPC2_DIE1_HD0_SEI,
		GAUDI3_EVENT_TPC3_DIE1_HD0_SEI,
		GAUDI3_EVENT_TPC4_DIE1_HD0_SEI,
		GAUDI3_EVENT_TPC5_DIE1_HD0_SEI,
		GAUDI3_EVENT_TPC6_DIE1_HD0_SEI,
		GAUDI3_EVENT_TPC7_DIE1_HD0_SEI,
		GAUDI3_EVENT_TPC8_DIE1_HD0_SEI,
		GAUDI3_EVENT_TPC0_DIE1_HD1_SEI,
		GAUDI3_EVENT_TPC1_DIE1_HD1_SEI,
		GAUDI3_EVENT_TPC2_DIE1_HD1_SEI,
		GAUDI3_EVENT_TPC3_DIE1_HD1_SEI,
		GAUDI3_EVENT_TPC4_DIE1_HD1_SEI,
		GAUDI3_EVENT_TPC5_DIE1_HD1_SEI,
		GAUDI3_EVENT_TPC6_DIE1_HD1_SEI,
		GAUDI3_EVENT_TPC7_DIE1_HD1_SEI,
		GAUDI3_EVENT_TPC8_DIE1_HD1_SEI,
		GAUDI3_EVENT_TPC0_DIE1_HD2_SEI,
		GAUDI3_EVENT_TPC1_DIE1_HD2_SEI,
		GAUDI3_EVENT_TPC2_DIE1_HD2_SEI,
		GAUDI3_EVENT_TPC3_DIE1_HD2_SEI,
		GAUDI3_EVENT_TPC4_DIE1_HD2_SEI,
		GAUDI3_EVENT_TPC5_DIE1_HD2_SEI,
		GAUDI3_EVENT_TPC6_DIE1_HD2_SEI,
		GAUDI3_EVENT_TPC7_DIE1_HD2_SEI,
		GAUDI3_EVENT_TPC8_DIE1_HD2_SEI,
		GAUDI3_EVENT_TPC0_DIE1_HD3_SEI,
		GAUDI3_EVENT_TPC1_DIE1_HD3_SEI,
		GAUDI3_EVENT_TPC2_DIE1_HD3_SEI,
		GAUDI3_EVENT_TPC3_DIE1_HD3_SEI,
		GAUDI3_EVENT_TPC4_DIE1_HD3_SEI,
		GAUDI3_EVENT_TPC5_DIE1_HD3_SEI,
		GAUDI3_EVENT_TPC6_DIE1_HD3_SEI,
		GAUDI3_EVENT_TPC7_DIE1_HD3_SEI,
		GAUDI3_EVENT_TPC8_DIE1_HD3_SEI}
};

/* HDCORE_TPC_EVENT */
static void handle_and_clear_tpc_events(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask)
{
	enum gaudi3_async_event_id event_id;
	bool unmask_event_in_aggr = false;
	struct hl_eq_entry eq_entry;
	u32 index;

	memset(&eq_entry, 0, sizeof(struct hl_eq_entry));

	switch (type) {
	case ERR_GRP_SPI_ECO:
		index = idx + (18 * hdcore);
		event_id = tpc_spi_events_id_map[die][index];
		break;
	case ERR_GRP_DERR:
		index = idx + (9 * hdcore);
		event_id = tpc_derr_events_id_map[die][index];
		break;
	case ERR_GRP_SEI:
		index = idx + (9 * hdcore);
		event_id = tpc_sei_events_id_map[die][index];
		break;
	default:
		return;
	}

	eq_entry.hdr.ctl = cpu_to_le32(event_id << EQ_CTL_EVENT_TYPE_SHIFT);
	gaudi3_handle_eqe(hdev, &eq_entry);

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static const enum gaudi3_async_event_id
mme_derr_events_id_map[MAX_NUM_OF_DIES][11 * NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_MME0_DIE0_HD0_DERR,
		GAUDI3_EVENT_MME1_DIE0_HD0_DERR,
		GAUDI3_EVENT_MME2_DIE0_HD0_DERR,
		GAUDI3_EVENT_MME3_DIE0_HD0_DERR,
		GAUDI3_EVENT_MME4_DIE0_HD0_DERR,
		GAUDI3_EVENT_MME5_DIE0_HD0_DERR,
		GAUDI3_EVENT_MME6_DIE0_HD0_DERR,
		GAUDI3_EVENT_MME7_DIE0_HD0_DERR,
		GAUDI3_EVENT_MME8_DIE0_HD0_DERR,
		GAUDI3_EVENT_MME9_DIE0_HD0_DERR,
		GAUDI3_EVENT_MME10_DIE0_HD0_DERR,
		GAUDI3_EVENT_MME0_DIE0_HD1_DERR,
		GAUDI3_EVENT_MME1_DIE0_HD1_DERR,
		GAUDI3_EVENT_MME2_DIE0_HD1_DERR,
		GAUDI3_EVENT_MME3_DIE0_HD1_DERR,
		GAUDI3_EVENT_MME4_DIE0_HD1_DERR,
		GAUDI3_EVENT_MME5_DIE0_HD1_DERR,
		GAUDI3_EVENT_MME6_DIE0_HD1_DERR,
		GAUDI3_EVENT_MME7_DIE0_HD1_DERR,
		GAUDI3_EVENT_MME8_DIE0_HD1_DERR,
		GAUDI3_EVENT_MME9_DIE0_HD1_DERR,
		GAUDI3_EVENT_MME10_DIE0_HD1_DERR,
		GAUDI3_EVENT_MME0_DIE0_HD2_DERR,
		GAUDI3_EVENT_MME1_DIE0_HD2_DERR,
		GAUDI3_EVENT_MME2_DIE0_HD2_DERR,
		GAUDI3_EVENT_MME3_DIE0_HD2_DERR,
		GAUDI3_EVENT_MME4_DIE0_HD2_DERR,
		GAUDI3_EVENT_MME5_DIE0_HD2_DERR,
		GAUDI3_EVENT_MME6_DIE0_HD2_DERR,
		GAUDI3_EVENT_MME7_DIE0_HD2_DERR,
		GAUDI3_EVENT_MME8_DIE0_HD2_DERR,
		GAUDI3_EVENT_MME9_DIE0_HD2_DERR,
		GAUDI3_EVENT_MME10_DIE0_HD2_DERR,
		GAUDI3_EVENT_MME0_DIE0_HD3_DERR,
		GAUDI3_EVENT_MME1_DIE0_HD3_DERR,
		GAUDI3_EVENT_MME2_DIE0_HD3_DERR,
		GAUDI3_EVENT_MME3_DIE0_HD3_DERR,
		GAUDI3_EVENT_MME4_DIE0_HD3_DERR,
		GAUDI3_EVENT_MME5_DIE0_HD3_DERR,
		GAUDI3_EVENT_MME6_DIE0_HD3_DERR,
		GAUDI3_EVENT_MME7_DIE0_HD3_DERR,
		GAUDI3_EVENT_MME8_DIE0_HD3_DERR,
		GAUDI3_EVENT_MME9_DIE0_HD3_DERR,
		GAUDI3_EVENT_MME10_DIE0_HD3_DERR},
		{GAUDI3_EVENT_MME0_DIE1_HD0_DERR,
		GAUDI3_EVENT_MME1_DIE1_HD0_DERR,
		GAUDI3_EVENT_MME2_DIE1_HD0_DERR,
		GAUDI3_EVENT_MME3_DIE1_HD0_DERR,
		GAUDI3_EVENT_MME4_DIE1_HD0_DERR,
		GAUDI3_EVENT_MME5_DIE1_HD0_DERR,
		GAUDI3_EVENT_MME6_DIE1_HD0_DERR,
		GAUDI3_EVENT_MME7_DIE1_HD0_DERR,
		GAUDI3_EVENT_MME8_DIE1_HD0_DERR,
		GAUDI3_EVENT_MME9_DIE1_HD0_DERR,
		GAUDI3_EVENT_MME10_DIE1_HD0_DERR,
		GAUDI3_EVENT_MME0_DIE1_HD1_DERR,
		GAUDI3_EVENT_MME1_DIE1_HD1_DERR,
		GAUDI3_EVENT_MME2_DIE1_HD1_DERR,
		GAUDI3_EVENT_MME3_DIE1_HD1_DERR,
		GAUDI3_EVENT_MME4_DIE1_HD1_DERR,
		GAUDI3_EVENT_MME5_DIE1_HD1_DERR,
		GAUDI3_EVENT_MME6_DIE1_HD1_DERR,
		GAUDI3_EVENT_MME7_DIE1_HD1_DERR,
		GAUDI3_EVENT_MME8_DIE1_HD1_DERR,
		GAUDI3_EVENT_MME9_DIE1_HD1_DERR,
		GAUDI3_EVENT_MME10_DIE1_HD1_DERR,
		GAUDI3_EVENT_MME0_DIE1_HD2_DERR,
		GAUDI3_EVENT_MME1_DIE1_HD2_DERR,
		GAUDI3_EVENT_MME2_DIE1_HD2_DERR,
		GAUDI3_EVENT_MME3_DIE1_HD2_DERR,
		GAUDI3_EVENT_MME4_DIE1_HD2_DERR,
		GAUDI3_EVENT_MME5_DIE1_HD2_DERR,
		GAUDI3_EVENT_MME6_DIE1_HD2_DERR,
		GAUDI3_EVENT_MME7_DIE1_HD2_DERR,
		GAUDI3_EVENT_MME8_DIE1_HD2_DERR,
		GAUDI3_EVENT_MME9_DIE1_HD2_DERR,
		GAUDI3_EVENT_MME10_DIE1_HD2_DERR,
		GAUDI3_EVENT_MME0_DIE1_HD3_DERR,
		GAUDI3_EVENT_MME1_DIE1_HD3_DERR,
		GAUDI3_EVENT_MME2_DIE1_HD3_DERR,
		GAUDI3_EVENT_MME3_DIE1_HD3_DERR,
		GAUDI3_EVENT_MME4_DIE1_HD3_DERR,
		GAUDI3_EVENT_MME5_DIE1_HD3_DERR,
		GAUDI3_EVENT_MME6_DIE1_HD3_DERR,
		GAUDI3_EVENT_MME7_DIE1_HD3_DERR,
		GAUDI3_EVENT_MME8_DIE1_HD3_DERR,
		GAUDI3_EVENT_MME9_DIE1_HD3_DERR,
		GAUDI3_EVENT_MME10_DIE1_HD3_DERR}
};

static const enum gaudi3_async_event_id
mme_sei_events_id_map[MAX_NUM_OF_DIES][12 * NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_MME0_DIE0_HD0_SEI,
		GAUDI3_EVENT_MME1_DIE0_HD0_SEI,
		GAUDI3_EVENT_MME2_DIE0_HD0_SEI,
		GAUDI3_EVENT_MME3_DIE0_HD0_SEI,
		GAUDI3_EVENT_MME4_DIE0_HD0_SEI,
		GAUDI3_EVENT_MME5_DIE0_HD0_SEI,
		GAUDI3_EVENT_MME6_DIE0_HD0_SEI,
		GAUDI3_EVENT_MME7_DIE0_HD0_SEI,
		GAUDI3_EVENT_MME8_DIE0_HD0_SEI,
		GAUDI3_EVENT_MME9_DIE0_HD0_SEI,
		GAUDI3_EVENT_MME10_DIE0_HD0_SEI,
		GAUDI3_EVENT_MME11_DIE0_HD0_SEI,
		GAUDI3_EVENT_MME0_DIE0_HD1_SEI,
		GAUDI3_EVENT_MME1_DIE0_HD1_SEI,
		GAUDI3_EVENT_MME2_DIE0_HD1_SEI,
		GAUDI3_EVENT_MME3_DIE0_HD1_SEI,
		GAUDI3_EVENT_MME4_DIE0_HD1_SEI,
		GAUDI3_EVENT_MME5_DIE0_HD1_SEI,
		GAUDI3_EVENT_MME6_DIE0_HD1_SEI,
		GAUDI3_EVENT_MME7_DIE0_HD1_SEI,
		GAUDI3_EVENT_MME8_DIE0_HD1_SEI,
		GAUDI3_EVENT_MME9_DIE0_HD1_SEI,
		GAUDI3_EVENT_MME10_DIE0_HD1_SEI,
		GAUDI3_EVENT_MME11_DIE0_HD1_SEI,
		GAUDI3_EVENT_MME0_DIE0_HD2_SEI,
		GAUDI3_EVENT_MME1_DIE0_HD2_SEI,
		GAUDI3_EVENT_MME2_DIE0_HD2_SEI,
		GAUDI3_EVENT_MME3_DIE0_HD2_SEI,
		GAUDI3_EVENT_MME4_DIE0_HD2_SEI,
		GAUDI3_EVENT_MME5_DIE0_HD2_SEI,
		GAUDI3_EVENT_MME6_DIE0_HD2_SEI,
		GAUDI3_EVENT_MME7_DIE0_HD2_SEI,
		GAUDI3_EVENT_MME8_DIE0_HD2_SEI,
		GAUDI3_EVENT_MME9_DIE0_HD2_SEI,
		GAUDI3_EVENT_MME10_DIE0_HD2_SEI,
		GAUDI3_EVENT_MME11_DIE0_HD2_SEI,
		GAUDI3_EVENT_MME0_DIE0_HD3_SEI,
		GAUDI3_EVENT_MME1_DIE0_HD3_SEI,
		GAUDI3_EVENT_MME2_DIE0_HD3_SEI,
		GAUDI3_EVENT_MME3_DIE0_HD3_SEI,
		GAUDI3_EVENT_MME4_DIE0_HD3_SEI,
		GAUDI3_EVENT_MME5_DIE0_HD3_SEI,
		GAUDI3_EVENT_MME6_DIE0_HD3_SEI,
		GAUDI3_EVENT_MME7_DIE0_HD3_SEI,
		GAUDI3_EVENT_MME8_DIE0_HD3_SEI,
		GAUDI3_EVENT_MME9_DIE0_HD3_SEI,
		GAUDI3_EVENT_MME10_DIE0_HD3_SEI,
		GAUDI3_EVENT_MME11_DIE0_HD3_SEI},
		{GAUDI3_EVENT_MME0_DIE1_HD0_SEI,
		GAUDI3_EVENT_MME1_DIE1_HD0_SEI,
		GAUDI3_EVENT_MME2_DIE1_HD0_SEI,
		GAUDI3_EVENT_MME3_DIE1_HD0_SEI,
		GAUDI3_EVENT_MME4_DIE1_HD0_SEI,
		GAUDI3_EVENT_MME5_DIE1_HD0_SEI,
		GAUDI3_EVENT_MME6_DIE1_HD0_SEI,
		GAUDI3_EVENT_MME7_DIE1_HD0_SEI,
		GAUDI3_EVENT_MME8_DIE1_HD0_SEI,
		GAUDI3_EVENT_MME9_DIE1_HD0_SEI,
		GAUDI3_EVENT_MME10_DIE1_HD0_SEI,
		GAUDI3_EVENT_MME11_DIE1_HD0_SEI,
		GAUDI3_EVENT_MME0_DIE1_HD1_SEI,
		GAUDI3_EVENT_MME1_DIE1_HD1_SEI,
		GAUDI3_EVENT_MME2_DIE1_HD1_SEI,
		GAUDI3_EVENT_MME3_DIE1_HD1_SEI,
		GAUDI3_EVENT_MME4_DIE1_HD1_SEI,
		GAUDI3_EVENT_MME5_DIE1_HD1_SEI,
		GAUDI3_EVENT_MME6_DIE1_HD1_SEI,
		GAUDI3_EVENT_MME7_DIE1_HD1_SEI,
		GAUDI3_EVENT_MME8_DIE1_HD1_SEI,
		GAUDI3_EVENT_MME9_DIE1_HD1_SEI,
		GAUDI3_EVENT_MME10_DIE1_HD1_SEI,
		GAUDI3_EVENT_MME11_DIE1_HD1_SEI,
		GAUDI3_EVENT_MME0_DIE1_HD2_SEI,
		GAUDI3_EVENT_MME1_DIE1_HD2_SEI,
		GAUDI3_EVENT_MME2_DIE1_HD2_SEI,
		GAUDI3_EVENT_MME3_DIE1_HD2_SEI,
		GAUDI3_EVENT_MME4_DIE1_HD2_SEI,
		GAUDI3_EVENT_MME5_DIE1_HD2_SEI,
		GAUDI3_EVENT_MME6_DIE1_HD2_SEI,
		GAUDI3_EVENT_MME7_DIE1_HD2_SEI,
		GAUDI3_EVENT_MME8_DIE1_HD2_SEI,
		GAUDI3_EVENT_MME9_DIE1_HD2_SEI,
		GAUDI3_EVENT_MME10_DIE1_HD2_SEI,
		GAUDI3_EVENT_MME11_DIE1_HD2_SEI,
		GAUDI3_EVENT_MME0_DIE1_HD3_SEI,
		GAUDI3_EVENT_MME1_DIE1_HD3_SEI,
		GAUDI3_EVENT_MME2_DIE1_HD3_SEI,
		GAUDI3_EVENT_MME3_DIE1_HD3_SEI,
		GAUDI3_EVENT_MME4_DIE1_HD3_SEI,
		GAUDI3_EVENT_MME5_DIE1_HD3_SEI,
		GAUDI3_EVENT_MME6_DIE1_HD3_SEI,
		GAUDI3_EVENT_MME7_DIE1_HD3_SEI,
		GAUDI3_EVENT_MME8_DIE1_HD3_SEI,
		GAUDI3_EVENT_MME9_DIE1_HD3_SEI,
		GAUDI3_EVENT_MME10_DIE1_HD3_SEI,
		GAUDI3_EVENT_MME11_DIE1_HD3_SEI}
};

static const enum gaudi3_async_event_id
mme_spi_events_id_map[MAX_NUM_OF_DIES][3 * NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_MME12_DIE0_HD0_SPI,
		GAUDI3_EVENT_MME13_DIE0_HD0_SPI,
		GAUDI3_EVENT_MME14_DIE0_HD0_SPI,
		GAUDI3_EVENT_MME12_DIE0_HD1_SPI,
		GAUDI3_EVENT_MME13_DIE0_HD1_SPI,
		GAUDI3_EVENT_MME14_DIE0_HD1_SPI,
		GAUDI3_EVENT_MME12_DIE0_HD2_SPI,
		GAUDI3_EVENT_MME13_DIE0_HD2_SPI,
		GAUDI3_EVENT_MME14_DIE0_HD2_SPI,
		GAUDI3_EVENT_MME12_DIE0_HD3_SPI,
		GAUDI3_EVENT_MME13_DIE0_HD3_SPI,
		GAUDI3_EVENT_MME14_DIE0_HD3_SPI},
		{GAUDI3_EVENT_MME12_DIE1_HD0_SPI,
		GAUDI3_EVENT_MME13_DIE1_HD0_SPI,
		GAUDI3_EVENT_MME14_DIE1_HD0_SPI,
		GAUDI3_EVENT_MME12_DIE1_HD1_SPI,
		GAUDI3_EVENT_MME13_DIE1_HD1_SPI,
		GAUDI3_EVENT_MME14_DIE1_HD1_SPI,
		GAUDI3_EVENT_MME12_DIE1_HD2_SPI,
		GAUDI3_EVENT_MME13_DIE1_HD2_SPI,
		GAUDI3_EVENT_MME14_DIE1_HD2_SPI,
		GAUDI3_EVENT_MME12_DIE1_HD3_SPI,
		GAUDI3_EVENT_MME13_DIE1_HD3_SPI,
		GAUDI3_EVENT_MME14_DIE1_HD3_SPI}
};

/* HDCORE_MME_EVENT */
static void handle_and_clear_mme_events(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask)
{
	enum gaudi3_async_event_id event_id;
	bool unmask_event_in_aggr = false;
	struct hl_eq_entry eq_entry;
	u32 index;

	memset(&eq_entry, 0, sizeof(struct hl_eq_entry));

	switch (type) {
	case ERR_GRP_SPI_ECO:
		index = idx + (3 * hdcore);
		event_id = mme_spi_events_id_map[die][index];
		break;
	case ERR_GRP_DERR:
		index = idx + (11 * hdcore);
		event_id = mme_derr_events_id_map[die][index];
		break;
	case ERR_GRP_SEI:
		index = idx + (12 * hdcore);
		event_id = mme_sei_events_id_map[die][index];
		break;
	default:
		return;
	}

	eq_entry.hdr.ctl = cpu_to_le32(event_id << EQ_CTL_EVENT_TYPE_SHIFT);
	gaudi3_handle_eqe(hdev, &eq_entry);

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static const enum gaudi3_async_event_id
stlb_derr_events_id_map[MAX_NUM_OF_DIES][NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_STLB0_DIE0_HD0_DERR,
		GAUDI3_EVENT_STLB0_DIE0_HD1_DERR,
		GAUDI3_EVENT_STLB0_DIE0_HD2_DERR,
		GAUDI3_EVENT_STLB0_DIE0_HD3_DERR},
		{GAUDI3_EVENT_STLB0_DIE1_HD0_DERR,
		GAUDI3_EVENT_STLB0_DIE1_HD1_DERR,
		GAUDI3_EVENT_STLB0_DIE1_HD2_DERR,
		GAUDI3_EVENT_STLB0_DIE1_HD3_DERR}
};

static const enum gaudi3_async_event_id
stlb_spi_events_id_map[MAX_NUM_OF_DIES][3 * NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_STLB0_DIE0_HD0_SPI,
		GAUDI3_EVENT_STLB1_DIE0_HD0_SPI,
		GAUDI3_EVENT_STLB2_DIE0_HD0_SPI,

		GAUDI3_EVENT_STLB0_DIE0_HD1_SPI,
		GAUDI3_EVENT_STLB1_DIE0_HD1_SPI,
		GAUDI3_EVENT_STLB2_DIE0_HD1_SPI,

		GAUDI3_EVENT_STLB0_DIE0_HD2_SPI,
		GAUDI3_EVENT_STLB1_DIE0_HD2_SPI,
		GAUDI3_EVENT_STLB2_DIE0_HD2_SPI,

		GAUDI3_EVENT_STLB0_DIE0_HD3_SPI,
		GAUDI3_EVENT_STLB1_DIE0_HD3_SPI,
		GAUDI3_EVENT_STLB2_DIE0_HD3_SPI},

		{GAUDI3_EVENT_STLB0_DIE1_HD0_SPI,
		GAUDI3_EVENT_STLB1_DIE1_HD0_SPI,
		GAUDI3_EVENT_STLB2_DIE1_HD0_SPI,

		GAUDI3_EVENT_STLB0_DIE1_HD1_SPI,
		GAUDI3_EVENT_STLB1_DIE1_HD1_SPI,
		GAUDI3_EVENT_STLB2_DIE1_HD1_SPI,

		GAUDI3_EVENT_STLB0_DIE1_HD2_SPI,
		GAUDI3_EVENT_STLB1_DIE1_HD2_SPI,
		GAUDI3_EVENT_STLB2_DIE1_HD2_SPI,

		GAUDI3_EVENT_STLB0_DIE1_HD3_SPI,
		GAUDI3_EVENT_STLB1_DIE1_HD3_SPI,
		GAUDI3_EVENT_STLB2_DIE1_HD3_SPI},
};

/* HDCORE_STLB_EVENT */
static void handle_and_clear_stlb_events(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask)
{
	enum gaudi3_async_event_id event_id;
	bool unmask_event_in_aggr = false;
	struct hl_eq_entry eq_entry;

	memset(&eq_entry, 0, sizeof(struct hl_eq_entry));

	switch (type) {
	case ERR_GRP_DERR:
		event_id = stlb_derr_events_id_map[die][idx + hdcore];
		break;
	case ERR_GRP_SPI_ECO:
		event_id = stlb_spi_events_id_map[die][idx + 3 * hdcore];
		unmask_event_in_aggr = true;
		break;
	default:
		dev_err(hdev->dev, "Unexpected error group(%u)\n", type);
		return;
	}

	eq_entry.hdr.ctl = cpu_to_le32(event_id << EQ_CTL_EVENT_TYPE_SHIFT);
	gaudi3_handle_eqe(hdev, &eq_entry);

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static const enum gaudi3_async_event_id
pdma_derr_events_id_map[MAX_NUM_OF_DIES][1] = {
		{GAUDI3_EVENT_PDMA0_DIE0_HDSHARED_DERR},
		{GAUDI3_EVENT_PDMA0_DIE1_HDSHARED_DERR}
};

static const enum gaudi3_async_event_id
pdma_sei_events_id_map[MAX_NUM_OF_DIES][1] = {
		{GAUDI3_EVENT_PDMA0_DIE0_HDSHARED_SEI},
		{GAUDI3_EVENT_PDMA0_DIE1_HDSHARED_SEI}
};

static const enum gaudi3_async_event_id
pdma_spi_events_id_map[MAX_NUM_OF_DIES][1] = {
		{GAUDI3_EVENT_PDMA0_DIE0_HDSHARED_SPI},
		{GAUDI3_EVENT_PDMA0_DIE1_HDSHARED_SPI}
};

/* SHARED_PDMA_EVENT */
static void handle_and_clear_pdma_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask)
{
	enum gaudi3_async_event_id event_id;
	bool unmask_event_in_aggr = false;
	struct hl_eq_entry eq_entry;

	memset(&eq_entry, 0, sizeof(struct hl_eq_entry));

	switch (type) {
	case ERR_GRP_SPI_ECO:
		event_id = pdma_spi_events_id_map[die][idx];
		break;
	case ERR_GRP_DERR:
		event_id = pdma_derr_events_id_map[die][idx];
		break;
	case ERR_GRP_SEI:
		event_id = pdma_sei_events_id_map[die][idx];
		unmask_event_in_aggr = true;
		break;
	default:
		return;
	}

	eq_entry.hdr.ctl = cpu_to_le32(event_id << EQ_CTL_EVENT_TYPE_SHIFT);
	gaudi3_handle_eqe(hdev, &eq_entry);

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static enum gaudi3_async_event_id
arc_sei_events_id_map[MAX_NUM_OF_DIES][NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_ARC_FARM0_DIE0_HD0_SEI,
		GAUDI3_EVENT_ARC_FARM0_DIE0_HD1_SEI,
		GAUDI3_EVENT_ARC_FARM0_DIE0_HD2_SEI,
		GAUDI3_EVENT_ARC_FARM0_DIE0_HD3_SEI},

		{GAUDI3_EVENT_ARC_FARM0_DIE1_HD0_SEI,
		GAUDI3_EVENT_ARC_FARM0_DIE1_HD1_SEI,
		GAUDI3_EVENT_ARC_FARM0_DIE1_HD2_SEI,
		GAUDI3_EVENT_ARC_FARM0_DIE1_HD3_SEI}
};

static enum gaudi3_async_event_id
arc_derr_events_id_map[MAX_NUM_OF_DIES][NUM_OF_HDCORES_PER_DIE] = {
		{GAUDI3_EVENT_ARC_FARM0_DIE0_HD0_DERR,
		GAUDI3_EVENT_ARC_FARM0_DIE0_HD1_DERR,
		GAUDI3_EVENT_ARC_FARM0_DIE0_HD2_DERR,
		GAUDI3_EVENT_ARC_FARM0_DIE0_HD3_DERR},

		{GAUDI3_EVENT_ARC_FARM0_DIE1_HD0_DERR,
		GAUDI3_EVENT_ARC_FARM0_DIE1_HD1_DERR,
		GAUDI3_EVENT_ARC_FARM0_DIE1_HD2_DERR,
		GAUDI3_EVENT_ARC_FARM0_DIE1_HD3_DERR}
};

/* HDCORE_ARCFARM_EVENT */
static void handle_and_clear_arc_farm_events(struct hl_device *hdev, u32 die, u32 hdcore,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask)
{
	enum gaudi3_async_event_id event_id;
	bool unmask_event_in_aggr = false;
	struct hl_eq_entry eq_entry;
	u32 err_msk;

	memset(&eq_entry, 0, sizeof(struct hl_eq_entry));

	switch (type) {
	case ERR_GRP_DERR:
		event_id = arc_derr_events_id_map[die][hdcore];
		break;
	case ERR_GRP_SEI:
		event_id = arc_sei_events_id_map[die][hdcore];
		eq_entry.intr_cause.intr_cause_data =
				cpu_to_le64(RREG32(hdcore * HDCORE_OFFSET +
						mmHD0_ARC_FARM_ARC0_AUX_BASE +
						mmQMAN_ARC_AUX_ARC_SEI_INTR_STS));
		unmask_event_in_aggr = true;
		break;
	default:
		dev_err(hdev->dev, "Unexpected error group(%u)\n", type);
		return;
	}

	eq_entry.hdr.ctl = cpu_to_le32(event_id << EQ_CTL_EVENT_TYPE_SHIFT);
	gaudi3_handle_eqe(hdev, &eq_entry);

	/* Clear event */
	if (type == ERR_GRP_SEI) {
		err_msk = lower_32_bits(le64_to_cpu(eq_entry.intr_cause.intr_cause_data));
		WREG32(hdcore * HDCORE_OFFSET + mmHD0_ARC_FARM_ARC0_AUX_BASE +
				mmQMAN_ARC_AUX_ARC_SEI_INTR_CLR, err_msk);
	}

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static enum gaudi3_async_event_id nic_spi_events_id_map[MAX_NUM_OF_DIES][12] = {
		{GAUDI3_EVENT_NIC0_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_NIC1_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_NIC2_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_NIC3_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_NIC4_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_NIC5_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_NIC6_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_NIC7_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_NIC8_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_NIC9_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_NIC10_DIE0_HDSHARED_SPI,
		GAUDI3_EVENT_NIC11_DIE0_HDSHARED_SPI},

		{GAUDI3_EVENT_NIC0_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_NIC1_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_NIC2_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_NIC3_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_NIC4_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_NIC5_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_NIC6_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_NIC7_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_NIC8_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_NIC9_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_NIC10_DIE1_HDSHARED_SPI,
		GAUDI3_EVENT_NIC11_DIE1_HDSHARED_SPI}
};

static enum gaudi3_async_event_id nic_sei_events_id_map[MAX_NUM_OF_DIES][6] = {
		{GAUDI3_EVENT_NIC0_DIE0_HDSHARED_SEI,
		GAUDI3_EVENT_NIC1_DIE0_HDSHARED_SEI,
		GAUDI3_EVENT_NIC2_DIE0_HDSHARED_SEI,
		GAUDI3_EVENT_NIC3_DIE0_HDSHARED_SEI,
		GAUDI3_EVENT_NIC4_DIE0_HDSHARED_SEI,
		GAUDI3_EVENT_NIC5_DIE0_HDSHARED_SEI},

		{GAUDI3_EVENT_NIC0_DIE1_HDSHARED_SEI,
		GAUDI3_EVENT_NIC1_DIE1_HDSHARED_SEI,
		GAUDI3_EVENT_NIC2_DIE1_HDSHARED_SEI,
		GAUDI3_EVENT_NIC3_DIE1_HDSHARED_SEI,
		GAUDI3_EVENT_NIC4_DIE1_HDSHARED_SEI,
		GAUDI3_EVENT_NIC5_DIE1_HDSHARED_SEI}
};

static enum gaudi3_async_event_id nic_derr_events_id_map[MAX_NUM_OF_DIES][6] = {
		{GAUDI3_EVENT_NIC0_DIE0_HDSHARED_DERR,
		GAUDI3_EVENT_NIC1_DIE0_HDSHARED_DERR,
		GAUDI3_EVENT_NIC2_DIE0_HDSHARED_DERR,
		GAUDI3_EVENT_NIC3_DIE0_HDSHARED_DERR,
		GAUDI3_EVENT_NIC4_DIE0_HDSHARED_DERR,
		GAUDI3_EVENT_NIC5_DIE0_HDSHARED_DERR},

		{GAUDI3_EVENT_NIC0_DIE1_HDSHARED_DERR,
		GAUDI3_EVENT_NIC1_DIE1_HDSHARED_DERR,
		GAUDI3_EVENT_NIC2_DIE1_HDSHARED_DERR,
		GAUDI3_EVENT_NIC3_DIE1_HDSHARED_DERR,
		GAUDI3_EVENT_NIC4_DIE1_HDSHARED_DERR,
		GAUDI3_EVENT_NIC5_DIE1_HDSHARED_DERR}
};

/* SHARED_NIC_EVENT */
static void handle_and_clear_nic_events(struct hl_device *hdev, u32 die,
					enum err_grp type, u32 sts, u32 sts_idx, u32 idx,
					u32 aggr_mask_reg, u32 events_mask)
{
	enum gaudi3_async_event_id event_id;
	bool unmask_event_in_aggr = false;
	struct hl_eq_entry eq_entry;

	memset(&eq_entry, 0, sizeof(struct hl_eq_entry));

	switch (type) {
	case ERR_GRP_SPI_ECO:
		event_id = nic_spi_events_id_map[die][idx];
		break;
	case ERR_GRP_SEI:
		event_id = nic_sei_events_id_map[die][idx];
		break;
	case ERR_GRP_DERR:
		event_id = nic_derr_events_id_map[die][idx];
		break;
	default:
		dev_err(hdev->dev, "Unexpected error group(%u)\n", type);
		return;
	}

	eq_entry.hdr.ctl = cpu_to_le32(event_id << EQ_CTL_EVENT_TYPE_SHIFT);
	gaudi3_handle_eqe(hdev, &eq_entry);

	if (unmask_event_in_aggr)
		WREG32_AND(aggr_mask_reg, events_mask);
}

static void gaudi3_shared_spi_event_info(struct hl_device *hdev, u32 die)
{
	u32 offset, sts0, sts1, sts2, idx,
			sts0_pcie_mask0 = BIT(3),
			sts0_pcie_mask1 = GENMASK(7, 6),
			sts0_pcie_mask2 = BIT(11),
			sts0_pcie_mask3 = GENMASK(16, 15),
			sts0_nic_mask = GENMASK(31, 21),
			sts1_nic_mask = BIT(0),
			sts1_nch_mask = GENMASK(2, 1),
			sts1_pmmu_mask = GENMASK(5, 4),
			sts1_ts_mask = GENMASK(15, 8),
			sts1_pdma_mask = BIT(16),
			sts1_d2d_mask = GENMASK(18, 17);
	char str[512];

	offset = die * DIE_OFFSET;
	sts0 = RREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_STS_0 + offset);
	sts1 = RREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_STS_1 + offset);
	sts2 = RREG32(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_STS_2 + offset);

	/* Mask events first, if there is a handler then it'll be unmaked back */
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + offset, sts0);
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset, sts1);
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_2 + offset, sts2);

	/* Handle PCIE */
	if (sts0 & sts0_pcie_mask0) {
		idx = 1;
		snprintf(str, 512, "PCIE%u_DIE%u_HDSHARED_SPI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PCIE_EVENT])
			shared_handle_and_clear[SHARED_PCIE_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts0, 0, 0,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pcie_mask0));
	}

	if (sts0 & sts0_pcie_mask1) {
		idx = ffs(sts0 & sts0_pcie_mask1) - 3;
		snprintf(str, 512, "PCIE%u_DIE%u_HDSHARED_SPI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PCIE_EVENT])
			shared_handle_and_clear[SHARED_PCIE_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts0, 0, idx - 3,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pcie_mask1));
	}

	if (sts0 & sts0_pcie_mask2) {
		idx = 9;
		snprintf(str, 512, "PCIE%u_DIE%u_HDSHARED_SPI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PCIE_EVENT])
			shared_handle_and_clear[SHARED_PCIE_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts0, 0, 3,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pcie_mask2));
	}

	if (sts0 & sts0_pcie_mask3) {
		idx = ffs(sts0 & sts0_pcie_mask3) - 3;
		snprintf(str, 512, "PCIE%u_DIE%u_HDSHARED_SPI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PCIE_EVENT])
			shared_handle_and_clear[SHARED_PCIE_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts0, 0, idx - 9,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pcie_mask3));
	}

	/* Handle NIC */
	if (sts0 & sts0_nic_mask) {
		idx = ffs(sts0 & sts0_nic_mask) - 22;
		snprintf(str, 512, "NIC%u_DIE%u_HDSHARED_SPI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_NIC_EVENT])
			shared_handle_and_clear[SHARED_NIC_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_nic_mask));
	}

	if (sts1 & sts1_nic_mask) {
		idx = 11;
		snprintf(str, 512, "NIC%u_DIE%u_HDSHARED_SPI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_NIC_EVENT])
			shared_handle_and_clear[SHARED_NIC_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_nic_mask));
	}

	/* Handle NCH */
	if (sts1 & sts1_nch_mask) {
		idx = ffs(sts1 & sts1_nch_mask) - 2;
		snprintf(str, 512, "NCH%u_DIE%u_HDSHARED_SPI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_NCH_EVENT])
			shared_handle_and_clear[SHARED_NCH_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_nch_mask));
	}

	/* Handle PMMU */
	if (sts1 & sts1_pmmu_mask) {
		idx = ffs(sts1 & sts1_pmmu_mask) - 4;
		snprintf(str, 512, "PMMU%u_DIE%u_HDSHARED_SPI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PMMU_EVENT])
			shared_handle_and_clear[SHARED_PMMU_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts1, 1, idx - 1,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_pmmu_mask));
	}

	/* Handle TS */
	if (sts1 & sts1_ts_mask) {
		idx = ffs(sts1 & sts1_ts_mask) - 9;
		snprintf(str, 512, "TS%u_DIE%u_HDSHARED_SPI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_TS_EVENT])
			shared_handle_and_clear[SHARED_TS_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_ts_mask));
	}

	/* Handle PDMA */
	if (sts1 & sts1_pdma_mask) {
		idx = 0;
		snprintf(str, 512, "PDMA%u_DIE%u_HDSHARED_SPI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PDMA_EVENT])
			shared_handle_and_clear[SHARED_PDMA_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_pdma_mask));
	}

	/* Handle D2D */
	if (sts1 & sts1_d2d_mask) {
		idx = ffs(sts1 & sts1_d2d_mask) - 18;
		snprintf(str, 512, "D2D%u_DIE%u_HDSHARED_SPI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_D2D_EVENT])
			shared_handle_and_clear[SHARED_D2D_EVENT](hdev, die,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_SHARED_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_d2d_mask));
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

static void gaudi3_hdcore_spi_event_info(struct hl_device *hdev, u32 offset, u32 die, u32 hdcore)
{
	u32 idx, sts0, sts1, sts2, sts3,
			sts0_mme_mask = GENMASK(14, 12),
			sts0_tpc_mask = GENMASK(31, 15),
			sts1_tpc_mask = BIT(0),
			sts1_rot_mask = GENMASK(3, 1),
			sts1_cs_mask = GENMASK(20, 5),
			sts1_stlb_mask = GENMASK(23, 21),
			sts1_edma_mask = BIT(25),
			sts1_sob_mask = BIT(31),
			sts2_dec_mask = GENMASK(5, 2);
	char str[512];

	sts0 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_STS_0 + offset);
	sts1 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_STS_1 + offset);
	sts2 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_STS_2 + offset);
	sts3 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_STS_3 + offset);

	/* Mask events first, if there is a handler then it'll be unmaked back */
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
		snprintf(str, 512, "MME%u_DIE%u_HD%u_SPI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_MME_EVENT])
			hdcore_handle_and_clear[HDCORE_MME_EVENT](hdev, die, hdcore,
				ERR_GRP_SPI_ECO, sts0, 0, idx - 12,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_mme_mask));
	}

	/* Handle TPC */
	if (sts0 & sts0_tpc_mask) {
		idx = ffs(sts0 & sts0_tpc_mask) - 16;
		snprintf(str, 512, "TPC%u_DIE%u_HD%u_SPI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_TPC_EVENT])
			hdcore_handle_and_clear[HDCORE_TPC_EVENT](hdev, die, hdcore,
				ERR_GRP_SPI_ECO, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_tpc_mask));
	}

	if (sts1 & sts1_tpc_mask) {
		idx = 17;
		snprintf(str, 512, "TPC%u_DIE%u_HD%u_SPI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_TPC_EVENT])
			hdcore_handle_and_clear[HDCORE_TPC_EVENT](hdev, die, hdcore,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_tpc_mask));
	}

	/* Handle ROT */
	if (sts1 & sts1_rot_mask) {
		idx = ffs(sts1 & sts1_rot_mask) - 2;
		snprintf(str, 512, "ROT%u_DIE%u_HD%u_SPI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_ROT_EVENT])
			hdcore_handle_and_clear[HDCORE_ROT_EVENT](hdev, die, hdcore,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_rot_mask));
	}

	/* Handle Cache */
	if (sts1 & sts1_cs_mask) {
		idx = ffs(sts1 & sts1_cs_mask) - 6;
		snprintf(str, 512, "CS%u_DIE%u_HD%u_SPI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_CS_EVENT])
			hdcore_handle_and_clear[HDCORE_CS_EVENT](hdev, die, hdcore,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_cs_mask));
	}

	/* Handle STLB */
	if (sts1 & sts1_stlb_mask) {
		idx = ffs(sts1 & sts1_stlb_mask) - 22;
		snprintf(str, 512, "STLB%u_DIE%u_HD%u_SPI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);
		if (hdcore_handle_and_clear[HDCORE_STLB_EVENT])
			hdcore_handle_and_clear[HDCORE_STLB_EVENT](hdev, die, hdcore,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_stlb_mask));
	}

	/* Handle EDMA */
	if (sts1 & sts1_edma_mask) {
		idx = 0;
		snprintf(str, 512, "EDMA%u_DIE%u_HD%u_SPI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_EDMA_EVENT])
			hdcore_handle_and_clear[HDCORE_EDMA_EVENT](hdev, die, hdcore,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_edma_mask));
	}

	/* Handle SOB */
	if (sts1 & sts1_sob_mask) {
		idx = 0;
		snprintf(str, 512, "SOB%u_DIE%u_HD%u_SPI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_SOB_EVENT])
			hdcore_handle_and_clear[HDCORE_SOB_EVENT](hdev, die, hdcore,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_sob_mask));
	}

	/* Handle Decoder */
	if (sts2 & sts2_dec_mask) {
		idx = ffs(sts2 & sts2_dec_mask) - 3;
		snprintf(str, 512, "DEC%u_DIE%u_HD%u_SPI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_DEC_EVENT])
			hdcore_handle_and_clear[HDCORE_DEC_EVENT](hdev, die, hdcore,
				ERR_GRP_SPI_ECO, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SPI_ECO_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SPI_ECO_INT_MSG_MASK_2 + offset,
				~(sts2 & sts2_dec_mask));
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

static void gaudi3_hdcore_sei_event_info(struct hl_device *hdev, u32 offset, u32 die, u32 hdcore)
{
	u32 idx, sts0, sts1,
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
	char str[512];

	sts0 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_STS_0 + offset);
	sts1 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_STS_1 + offset);

	/* Mask events first, if there is a handler then it'll be unmaked back */
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_0 + offset, sts0);
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset, sts1);

	/* Handle MME */
	if (sts0 & sts0_mme_mask) {
		idx = ffs(sts0 & sts0_mme_mask) - 1;
		snprintf(str, 512, "MME%u_DIE%u_HD%u_SEI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_MME_EVENT])
			hdcore_handle_and_clear[HDCORE_MME_EVENT](hdev, die, hdcore,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_mme_mask));
	}

	/* Handle TPC */
	if (sts0 & sts0_tpc_mask) {
		idx = ffs(sts0 & sts0_tpc_mask) - 13;
		snprintf(str, 512, "TPC%u_DIE%u_HD%u_SEI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_TPC_EVENT])
			hdcore_handle_and_clear[HDCORE_TPC_EVENT](hdev, die, hdcore,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_tpc_mask));
	}

	/* Handle ROT */
	if (sts0 & sts0_rot_mask) {
		idx = ffs(sts0 & sts0_rot_mask) - 22;
		snprintf(str, 512, "ROT%u_DIE%u_HD%u_SEI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_ROT_EVENT])
			hdcore_handle_and_clear[HDCORE_ROT_EVENT](hdev, die, hdcore,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_rot_mask));
	}

	/* Handle Cache */
	if (sts0 & sts0_cs_mask) {
		idx = ffs(sts0 & sts0_cs_mask) - 24;
		snprintf(str, 512, "CS%u_DIE%u_HD%u_SEI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_CS_EVENT])
			hdcore_handle_and_clear[HDCORE_CS_EVENT](hdev, die, hdcore,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_cs_mask));
	}

	/* Handle STLB */
	if (sts0 & sts0_stlb_mask) {
		idx = 0;
		snprintf(str, 512, "STLB%u_DIE%u_HD%u_SEI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_STLB_EVENT])
			hdcore_handle_and_clear[HDCORE_STLB_EVENT](hdev, die, hdcore,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_stlb_mask));
	}

	/* Handle EDMA */
	if (sts1 & sts1_edma_mask) {
		idx = 0;
		snprintf(str, 512, "EDMA%u_DIE%u_HD%u_SEI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_EDMA_EVENT])
			hdcore_handle_and_clear[HDCORE_EDMA_EVENT](hdev, die, hdcore,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_edma_mask));
	}

	/* Handle HBM */
	if (sts1 & sts1_hbm_mask) {
		idx = ffs(sts1 & sts1_hbm_mask) - 2;
		snprintf(str, 512, "HBM%u_DIE%u_HD%u_SEI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_HBM_EVENT])
			hdcore_handle_and_clear[HDCORE_HBM_EVENT](hdev, die, hdcore,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_hbm_mask));
	}

	/* Handle SOB */
	if (sts1 & sts1_sob_mask) {
		idx = 0;
		snprintf(str, 512, "SOB%u_DIE%u_HD%u_SEI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_SOB_EVENT])
			hdcore_handle_and_clear[HDCORE_SOB_EVENT](hdev, die, hdcore,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_sob_mask));
	}

	/* Handle ARCFARM */
	if (sts1 & sts1_arcfarm_mask) {
		idx = 0;
		snprintf(str, 512, "ARC_FARM%u_DIE%u_HD%u_SEI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_ARCFARM_EVENT])
			hdcore_handle_and_clear[HDCORE_ARCFARM_EVENT](hdev, die, hdcore,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_arcfarm_mask));
	}

	/* Handle DUP */
	if (sts1 & sts1_dup_mask) {
		idx = 0;
		snprintf(str, 512, "DUP%u_DIE%u_HD%u_SEI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_DUP_EVENT])
			hdcore_handle_and_clear[HDCORE_DUP_EVENT](hdev, die, hdcore,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_dup_mask));
	}

	/* Handle DEC */
	if (sts1 & sts1_dec_mask) {
		idx = ffs(sts1 & sts1_dec_mask) - 13;
		snprintf(str, 512, "DEC%u_DIE%u_HD%u_SEI", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_DEC_EVENT])
			hdcore_handle_and_clear[HDCORE_DEC_EVENT](hdev, die, hdcore,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
				mmINT_AGG_HDCORE_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_dec_mask));
	}

	/* Clear interrupt - W1C */
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_STS_0 + offset, sts0);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_STS_1 + offset, sts1);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_SEI_INT_MSG_BASE +
			mmINT_AGG_HDCORE_SEI_INT_MSG_MSG_PENDING + offset, 1);
}

static void gaudi3_shared_sei_event_info(struct hl_device *hdev, u32 die)
{
	u32 offset, sts0, sts1, sts2, idx,
		sts0_cpu_mask = GENMASK(2, 0),
		sts0_pcie_mask = GENMASK(4, 3),
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

	char str[512];

	offset = die * DIE_OFFSET;
	sts0 = RREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_STS_0 + offset);
	sts1 = RREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_STS_1 + offset);
	sts2 = RREG32(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_STS_2 + offset);

	/* Mask events first, if there is a handler then it'll be unmaked back */
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset, sts0);
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_1 + offset, sts1);
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_2 + offset, sts2);

	/* Handle CPU */
	if (sts0 & sts0_cpu_mask) {
		idx = ffs(sts0 & sts0_cpu_mask) - 1;
		snprintf(str, 512, "CPU%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_CPU_EVENT])
			shared_handle_and_clear[SHARED_CPU_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_cpu_mask));
	}

	/* Handle PCIE */
	if (sts0 & sts0_pcie_mask) {
		idx = ffs(sts0 & sts0_pcie_mask) - 4;
		snprintf(str, 512, "PCIE%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PCIE_EVENT])
			shared_handle_and_clear[SHARED_PCIE_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pcie_mask));
	}

	/* Handle NIC */
	if (sts0 & sts0_nic_mask) {
		idx = ffs(sts0 & sts0_nic_mask) - 6;
		snprintf(str, 512, "NIC%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_NIC_EVENT])
			shared_handle_and_clear[SHARED_NIC_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_nic_mask));
	}

	/* Handle NCH */
	if (sts0 & sts0_nch_mask) {
		idx = ffs(sts0 & sts0_nch_mask) - 12;
		snprintf(str, 512, "NCH%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_NCH_EVENT])
			shared_handle_and_clear[SHARED_NCH_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_nch_mask));
	}

	/* Handle PMMU */
	if (sts0 & sts0_pmmu_mask) {
		idx = 1;
		snprintf(str, 512, "PMMU%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PMMU_EVENT])
			shared_handle_and_clear[SHARED_PMMU_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, 0,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pmmu_mask));
	}

	/* Handle VM */
	if (sts0 & sts0_vm_mask) {
		idx = ffs(sts0 & sts0_vm_mask) - 16;
		snprintf(str, 512, "VM%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_VM_EVENT])
			shared_handle_and_clear[SHARED_VM_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_vm_mask));
	}

	/* Handle PDMA */
	if (sts0 & sts0_pdma_mask) {
		idx = 0;
		snprintf(str, 512, "PDMA%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PDMA_EVENT])
			shared_handle_and_clear[SHARED_PDMA_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_pdma_mask));
	}

	/* Handle PSOC */
	if (sts0 & sts0_psoc_mask) {
		idx = 0;
		snprintf(str, 512, "PSOC%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PSOC_EVENT])
			shared_handle_and_clear[SHARED_PSOC_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_psoc_mask));
	}

	/* Handle PARC */
	if (sts0 & sts0_parc_mask) {
		idx = 0;
		snprintf(str, 512, "PARC%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PARC_EVENT])
			shared_handle_and_clear[SHARED_PARC_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_parc_mask));
	}

	/* Handle D2D */
	if (sts0 & sts0_d2d_mask) {
		idx = ffs(sts0 & sts0_d2d_mask) - 27;
		snprintf(str, 512, "D2D%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_D2D_EVENT])
			shared_handle_and_clear[SHARED_D2D_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_d2d_mask));
	}

	/* Handle GLINK */
	if (sts0 & sts0_glink_mask) {
		idx = ffs(sts0 & sts0_glink_mask) - 29;
		snprintf(str, 512, "GLINK%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_GLINK_EVENT])
			shared_handle_and_clear[SHARED_GLINK_EVENT](hdev, die,
				ERR_GRP_SEI, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_glink_mask));
	}

	if (sts1 & sts1_glink_mask) {
		idx = (ffs(sts0 & sts1_glink_mask)) + 3; /* +3 to distiguish name from sts0 */
		snprintf(str, 512, "GLINK%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_GLINK_EVENT])
			shared_handle_and_clear[SHARED_GLINK_EVENT](hdev, die,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_glink_mask));
	}

	/* Handle PLL */
	if (sts1 & sts1_pll_mask) {
		idx = ffs(sts0 & sts1_pll_mask) - 7;
		snprintf(str, 512, "PLL%u_DIE%u_HDSHARED_SEI", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PLL_EVENT])
			shared_handle_and_clear[SHARED_PLL_EVENT](hdev, die,
				ERR_GRP_SEI, sts1, 1, idx,
				mmD0_CPU_INT_AGG_SHARED_SEI_INT_MSG_BASE +
				mmINT_AGG_SHARED_SEI_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_pll_mask));
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

static void gaudi3_hdcore_serr_event_info(struct hl_device *hdev, u32 offset, u32 die, u32 hdcore)
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

	/* Mask events first, if there is a handler then it'll be unmaked back */
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_SERR_INT_MSG_MASK + offset, sts0);

	/* No SERR event should notify lkd for now */

	/* Clear interrupt - W1C */
	WREG32(mmD0_CPU_INT_AGG_SHARED_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_SERR_INT_MSG_STS + offset, sts0);
	WREG32(mmD0_CPU_INT_AGG_SHARED_REI_SERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_SERR_INT_MSG_MSG_PENDING + offset, 1);
}

static void gaudi3_hdcore_derr_event_info(struct hl_device *hdev, u32 offset, u32 die, u32 hdcore)
{
	u32 idx, sts0, sts1,
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
	char str[512];

	sts0 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_STS_0 + offset);
	sts1 = RREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_STS_1 + offset);

	/* Mask events first, if there is a handler then it'll be unmaked back */
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset, sts0);
	WREG32_OR(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset, sts1);

	/* Handle MME */
	if (sts0 & sts0_mme_mask) {
		idx = ffs(sts0 & sts0_mme_mask) - 1;
		snprintf(str, 512, "MME%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_MME_EVENT])
			hdcore_handle_and_clear[HDCORE_MME_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_mme_mask));
	}

	/* Handle TPC */
	if (sts0 & sts0_tpc_mask) {
		idx = ffs(sts0 & sts0_tpc_mask) - 12;
		snprintf(str, 512, "TPC%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_TPC_EVENT])
			hdcore_handle_and_clear[HDCORE_TPC_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_tpc_mask));
	}

	/* Handle ROT */
	if (sts0 & sts0_rot_mask) {
		idx = ffs(sts0 & sts0_rot_mask) - 21;
		snprintf(str, 512, "ROT%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_ROT_EVENT])
			hdcore_handle_and_clear[HDCORE_ROT_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_rot_mask));
	}

	/* Handle CS */
	if (sts0 & sts0_cs_mask) {
		idx = ffs(sts0 & sts0_cs_mask) - 23;
		snprintf(str, 512, "CS%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_CS_EVENT])
			hdcore_handle_and_clear[HDCORE_CS_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_cs_mask));
	}

	/* Handle STLB */
	if (sts0 & sts0_stlb_mask) {
		idx = 0;
		snprintf(str, 512, "STLB%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_STLB_EVENT])
			hdcore_handle_and_clear[HDCORE_STLB_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_stlb_mask));
	}

	/* Handle RTR */
	if (sts0 & sts0_rtr_mask) {
		idx = 0;
		snprintf(str, 512, "RTR%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_RTR_EVENT])
			hdcore_handle_and_clear[HDCORE_RTR_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_0 + offset,
				~(sts0 & sts0_rtr_mask));
	}

	if (sts1 & sts1_rtr_mask) {
		idx = ffs(sts1 & sts1_rtr_mask);
		snprintf(str, 512, "RTR%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_RTR_EVENT])
			hdcore_handle_and_clear[HDCORE_RTR_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_rtr_mask));
	}

	/* Handle EDMA */
	if (sts1 & sts1_edma_mask) {
		idx = 0;
		snprintf(str, 512, "EDMA%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_EDMA_EVENT])
			hdcore_handle_and_clear[HDCORE_EDMA_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_edma_mask));
	}

	/* Handle HBM */
	if (sts1 & sts1_hbm_mask) {
		idx = ffs(sts0 & sts1_hbm_mask) - 9;
		snprintf(str, 512, "HBM%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_HBM_EVENT])
			hdcore_handle_and_clear[HDCORE_HBM_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_hbm_mask));
	}

	/* Handle SOB */
	if (sts1 & sts1_sob_mask) {
		idx = 0;
		snprintf(str, 512, "SOB%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_SOB_EVENT])
			hdcore_handle_and_clear[HDCORE_SOB_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_sob_mask));
	}

	/* Handle ARCFARM */
	if (sts1 & sts1_arcfarm_mask) {
		idx = 0;
		snprintf(str, 512, "ARC_FARM%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_ARCFARM_EVENT])
			hdcore_handle_and_clear[HDCORE_ARCFARM_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_arcfarm_mask));
	}

	/* Handle DEC */
	if (sts1 & sts1_dec_mask) {
		idx = ffs(sts0 & sts1_dec_mask) - 15;
		snprintf(str, 512, "DEC%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_DEC_EVENT])
			hdcore_handle_and_clear[HDCORE_DEC_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_dec_mask));
	}

	/* Handle HIF */
	if (sts1 & sts1_hif_mask) {
		idx = 0;
		snprintf(str, 512, "HIF%u_DIE%u_HD%u_DERR", idx, die, hdcore);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (hdcore_handle_and_clear[HDCORE_HIF_EVENT])
			hdcore_handle_and_clear[HDCORE_HIF_EVENT](hdev, die, hdcore,
				ERR_GRP_DERR, sts1, 1, idx,
				mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MASK_1 + offset,
				~(sts1 & sts1_hif_mask));
	}

	/* Clear interrupt - W1C */
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_STS_0 + offset, sts0);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_STS_1 + offset, sts1);
	WREG32(mmD0_CPU_INT_AGG_HDCORE0_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_HDCORE_REI_DERR_INT_MSG_MSG_PENDING + offset, 1);
}

static void gaudi3_shared_derr_event_info(struct hl_device *hdev, u32 die)
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
	char str[512];

	offset = die * DIE_OFFSET;
	sts0 = RREG32(mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_DERR_INT_MSG_STS + offset);

	/* Mask events first, if there is a handler then it'll be unmaked back */
	WREG32_OR(mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset, sts0);

	/* Handle CPU */
	if (sts0 & sts0_cpu_mask) {
		idx = 0;
		snprintf(str, 512, "CPU%u_DIE%u_HDSHARED_DERR", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_CPU_EVENT])
			shared_handle_and_clear[SHARED_CPU_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_cpu_mask));
	}

	/* Handle PCIE */
	if (sts0 & sts0_pcie_mask) {
		idx = ffs(sts0 & sts0_pcie_mask) - 2;
		snprintf(str, 512, "PCIE%u_DIE%u_HDSHARED_DERR", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PCIE_EVENT])
			shared_handle_and_clear[SHARED_PCIE_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_pcie_mask));
	}

	/* Handle NIC */
	if (sts0 & sts0_nic_mask) {
		idx = ffs(sts0 & sts0_nic_mask) - 5;
		snprintf(str, 512, "NIC%u_DIE%u_HDSHARED_DERR", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_NIC_EVENT])
			shared_handle_and_clear[SHARED_NIC_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_nic_mask));
	}

	/* Handle NCH */
	if (sts0 & sts0_nch_mask) {
		idx = ffs(sts0 & sts0_nch_mask) - 11;
		snprintf(str, 512, "NCH%u_DIE%u_HDSHARED_DERR", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_NCH_EVENT])
			shared_handle_and_clear[SHARED_NCH_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_nch_mask));
	}

	/* Handle PMMU */
	if (sts0 & sts0_pmmu_mask) {
		idx = 0;
		snprintf(str, 512, "PMMU%u_DIE%u_HDSHARED_DERR", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PMMU_EVENT])
			shared_handle_and_clear[SHARED_PMMU_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_pmmu_mask));
	}

	/* Handle PDMA */
	if (sts0 & sts0_pdma_mask) {
		idx = 0;
		snprintf(str, 512, "PDMA%u_DIE%u_HDSHARED_DERR", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PDMA_EVENT])
			shared_handle_and_clear[SHARED_PDMA_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_pdma_mask));
	}

	/* Handle PARC */
	if (sts0 & sts0_parc_mask) {
		idx = 0;
		snprintf(str, 512, "PARC%u_DIE%u_HDSHARED_DERR", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_PARC_EVENT])
			shared_handle_and_clear[SHARED_PARC_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_parc_mask));
	}

	/* Handle D2D */
	if (sts0 & sts0_d2d_mask) {
		idx = ffs(sts0 & sts0_d2d_mask) - 17;
		snprintf(str, 512, "D2D%u_DIE%u_HDSHARED_DERR", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_D2D_EVENT])
			shared_handle_and_clear[SHARED_D2D_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_d2d_mask));
	}

	/* Handle RTR */
	if (sts0 & sts0_rtr_mask) {
		idx = ffs(sts0 & sts0_rtr_mask) - 19;
		snprintf(str, 512, "RTR%u_DIE%u_HDSHARED_DERR", idx, die);
		dev_err(hdev->dev, "Received %s event\n", str);

		if (shared_handle_and_clear[SHARED_RTR_EVENT])
			shared_handle_and_clear[SHARED_RTR_EVENT](hdev, die,
				ERR_GRP_DERR, sts0, 0, idx,
				mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
				mmINT_AGG_SHARED_REI_DERR_INT_MSG_MASK + offset,
				~(sts0 & sts0_rtr_mask));
	}

	/* Clear interrupt - W1C */
	WREG32(mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_DERR_INT_MSG_STS + offset, sts0);
	WREG32(mmD0_CPU_INT_AGG_SHARED_REI_DERR_INT_MSG_BASE +
			mmINT_AGG_SHARED_REI_DERR_INT_MSG_MSG_PENDING + offset, 1);
}

static void gaudi3_handle_cpu_aggr(struct hl_device *hdev, u32 intr_aggr_irq, u32 die)
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
		gaudi3_shared_derr_event_info(hdev, die);
		gaudi3_shared_serr_event_info(hdev, die);
		gaudi3_shared_sei_event_info(hdev, die);
		gaudi3_shared_spi_event_info(hdev, die);
		return;
	}

	switch (event_type) {
	case 0:
		gaudi3_hdcore_derr_event_info(hdev, offset, die, intr_block_idx);
		break;
	case 1:
		gaudi3_hdcore_serr_event_info(hdev, offset, die, intr_block_idx);
		break;
	case 2:
		gaudi3_hdcore_sei_event_info(hdev, offset, die, intr_block_idx);
		break;
	case 3:
		gaudi3_hdcore_spi_event_info(hdev, offset, die, intr_block_idx);
		break;
	};
}

static void _hl_pldm_irq_handler(struct work_struct *work)
{
	struct hl_pldm_eqe_work *eqe_work = container_of(work, struct hl_pldm_eqe_work, eq_work);
	struct hl_device *hdev = eqe_work->hdev;
	u32 intr_aggr_irq, die;
	bool is_psoc;

	/* D0 CPU HDCORE0: IRQs 0..3
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
	intr_aggr_irq = eqe_work->sw_irq - GAUDI3_PLDM_IRQ_FIRST;
	die = (intr_aggr_irq >= INTR_AGGR_NUM_OF_MSIX_VECTORS_PER_DIE) ? 1 : 0;
	is_psoc = (intr_aggr_irq - die * INTR_AGGR_NUM_OF_MSIX_VECTORS_PER_DIE) >=
			CPU_INTR_AGGR_NUM_OF_MSIX_VECTORS;

	if (is_psoc)
		gaudi3_handle_psoc_aggr(hdev, intr_aggr_irq, die);
	else
		gaudi3_handle_cpu_aggr(hdev, intr_aggr_irq, die);

	kfree(eqe_work);
}

irqreturn_t hl_pldm_irq_handler(int irq, void *arg)
{
	struct hl_device *hdev = arg;
	u32 sw_irq = irq - (hdev->pdev->irq + 1);
	struct hl_pldm_eqe_work *handle_eqe_work;

	handle_eqe_work = kmalloc(sizeof(*handle_eqe_work), GFP_ATOMIC);
	if (handle_eqe_work) {
		INIT_WORK(&handle_eqe_work->eq_work, _hl_pldm_irq_handler);
		handle_eqe_work->hdev = hdev;
		handle_eqe_work->sw_irq = sw_irq;
		queue_work(hdev->eq_wq, &handle_eqe_work->eq_work);
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

	first_qm_reg_base = mmD0_NIC0_QM_BASE;
	gaudi3_iterate_nics(hdev, &iter_ctx);
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
