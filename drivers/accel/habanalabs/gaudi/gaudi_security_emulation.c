// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2022 HabanaLabs, Ltd.
 * All rights reserved.
 *
 */

#include "gaudiP.h"
#include "../include/gaudi/asic_reg/gaudi_regs.h"

#define PROT_REGS_MAX			(HL_PROT_BITS_REGS_NUM - 1) /* Secure config -> LKD */
#define BITS_IN_REG			32
#define SNGL_REG_PROT_SIZE		(BITS_IN_REG * sizeof(u32))
#define PROT_SIZE_MAX			(PROT_REGS_MAX * SNGL_REG_PROT_SIZE)

/*
 * Protection register calculation:
 *
 * prot_reg_base = (reg & 0xFFF000) + (secure_or_priv ? 0xE80 : 0xF80)
 * prot_reg_offset = (reg & 0xF80) / 32
 * prot_offset = prot_reg_base + prot_reg_offset
 *
 * Protection value (bit) calculation:
 *
 * val = ~(1 << ((reg & 0x7F) / 4))
 */

/* Protection bits helpers */
#define PAGE_BASE(x)			((x) & 0x3FFF000)
#define PROT_REG_BASE(x, off)		((PAGE_BASE(x)) + (off))
#define PROT_REG_OFF(x)			(((x) & HL_GLBL_SEC_REG_OFFSET) / HL_PROT_BITS_REGS_NUM)
#define PRIV_PROT_REG(x)		(PROT_REG_BASE((x), \
						HL_GLBL_PRIV_REG_OFFSET) + PROT_REG_OFF(x))
#define SEC_PROT_REG(x)			(PROT_REG_BASE((x), \
						HL_GLBL_SEC_REG_OFFSET) + PROT_REG_OFF(x))
#define PROT_BIT(x)			(~(1 << (((x) & 0x7F) / 4)))

#define PROT_OVR_SECURE			0
#define PROT_OVR_PRIV			BIT(0)
#define PROT_OVR_USER			BIT(1)

/*
 * The CFG high gap starts at 0x7FFD000000, but there is additional one between
 * the TPC7 QM and the CFG high gap which needs to be included as well.
 * Include the TPC7 gap inside the CFG high gap
 */
#define CFG_HIGH_GAP_BASE		0x7FFCFC9000ull
#define DBG_BASE			mmMME_S_ROM_TABLE_BASE

#define mmSTM_BASE			0x7FF4000000ULL
#define mmRAZWI_BASE			0x7FFBFFF000ULL

#define PCIE_WRAP_RRS_NUM		32

#define HL_SP_SRAM_BASE			0x7FFBFE0000
#define HL_SP_SRAM_SIZE			0x10000

#define FIT_IMG_SPSRAM_MAX_SIZE		SZ_8K
#define FIT_IMG_SPSRAM_END		(HL_SP_SRAM_BASE + HL_SP_SRAM_SIZE)
#define FIT_IMG_SPSRAM_BASE		(FIT_IMG_SPSRAM_END - FIT_IMG_SPSRAM_MAX_SIZE)

#define HL_SPI_BASE			0x7FF8000000ull
#define HL_SPI_SIZE			SZ_32M
#define HL_SPI_END			(HL_SPI_BASE + HL_SPI_SIZE)

#define HBM_RR_BASE			0
#define HBM_RR_MASK			0x3FFFFF0000000 /* 256MB reserved for Linux FW */

#define HBW_RR_SRAM_BGN_BASE		0x007FF0000000
#define HBW_RR_SRAM_BGN_MASK		0x3FFFFF000000
#define HBW_RR_SRAM_END_BASE		0x007FF1000000
#define HBW_RR_SRAM_END_MASK		0x3FFFFFC00000

#define GAUDI_NICS_NUM                  5
#define GAUDI_NICS_DIST                 0x40000
#define GAUDI_NIC_ENGS_DIST             0x1000

#define NIC_QM_DIST			0x2000

/* registers 0xF60-0xF80 are allowed access */
#define PHY_RX_PROTECT_HIGH		0xF60

#define SM_DIST				0x20000
#define SM_OBJS_MON_OFF			0x4000
#define SM_OBJS_MON_SZ			0xC000
#define SM_ENGINES			4

#define IF_RTRS_DIST			0x10000
#define IF_RTR_TO_RTR_CTRL_DIST		0x6000
#define IF_RTRS_NUM			16

#define MMES_NUM			(MME_NUMBER_OF_MASTER_ENGINES + \
						MME_NUMBER_OF_SLAVE_ENGINES)
#define MMES_DIST			0x80000

#define TPCS_DIST			0x40000

#define DBG_TPC_DIST			0x200000
#define INT_CFG_REGS_OFF		0xF00

#define SRAM_BANKS_NUM			32
#define SRAM_BANKS_DIST			0x8000

#define DMA_IF_NUM			4
#define DMA_DIST			0x20000

enum gaudi_sec_if_hbw_rrs {
	HBW_RR_HBM,
	HBW_RR_STM,
	HBW_RR_SPI_FLASH,
	HBW_RR_SP_SRAM,
	HBW_RR_PCIE_FW,
	HBW_RR_SRAM_R_BGN,
	HBW_RR_SRAM_R_1,
	HBW_RR_SRAM_R_END,
	HBW_RR_SRAM_BGN,
	HBW_RR_SRAM_END,
};

enum gaudi_sec_pcie_dyn_rrs {
	PCIE_DYN_RR_STM_SF = 16,
	PCIE_DYN_RR_SRAM_BGN = 17,
	PCIE_DYN_RR_SRAM_R1 = 18,
	PCIE_DYN_RR_SRAM_R2 = 19,
	PCIE_DYN_RR_SRAM_R3 = 20,
	PCIE_DYN_RR_SRAM_END = 21,
	PCIE_DYN_RR_SPSRAM_IMG = 23,
	PCIE_DYN_RR_CFG_LOW = 30,
	PCIE_DYN_RR_CFG_HIGH = 31
};

enum fw_sec_lbw_rr_type {
	IF_RTR,
	DMA_IF_SOB,
	DMA_IF_CH0,
	DMA_IF_CH1,
};

enum gaudi_sec_lbw_rrs {
	LBW_RR_MME1_QM,
	LBW_RR_MME3_QM_SRAM_CFG,
	LBW_RR_CPU_TSTAMP,
	LBW_RR_HBM_GIC,
	LBW_RR_PCIE_DBI_COREBGN,
	LBW_RR_PCIE_COREEND_AUX_PHY,
	LBW_RR_PCIE_MSI_PSOC0,
	LBW_RR_PSOC1_NIC0_MAC_CH,
	LBW_RR_NIC0_TS_NIC1_MAC_CH,
	LBW_RR_NIC1_TS_NIC2_MAC_CH,
	LBW_RR_NIC3_MAC_CH,
	LBW_RR_NIC3_TS_NIC4_MAC_CH,
	LBW_RR_CFG_HIGH_GAP,
	LBW_RR_DBG_ETR,
};

struct fw_sec_lbw_rrs_cfg {
	int instances_num;
	u32 instances_dist;
	u32 min_wr_breg;
	u32 max_wr_breg;
	u32 min_rd_breg;
	u32 max_rd_breg;
	u32 hit_wr_breg;
	u32 hit_rd_breg;
};

static struct fw_sec_lbw_rrs_cfg fw_sec_lbw_rrs_cfg[] = {
	[IF_RTR] = {
		.instances_num = IF_RTRS_NUM,
		.instances_dist = IF_RTRS_DIST,
		.min_wr_breg = mmSIF_RTR_0_LBW_RANGE_PRIV_MIN_AW_0,
		.max_wr_breg = mmSIF_RTR_0_LBW_RANGE_PRIV_MAX_AW_0,
		.min_rd_breg = mmSIF_RTR_0_LBW_RANGE_PRIV_MIN_AR_0,
		.max_rd_breg = mmSIF_RTR_0_LBW_RANGE_PRIV_MAX_AR_0,
		.hit_wr_breg = mmSIF_RTR_0_LBW_RANGE_PRIV_HIT_AW,
		.hit_rd_breg = mmSIF_RTR_0_LBW_RANGE_PRIV_HIT_AR,
	},
	[DMA_IF_SOB] = {
		.instances_num = DMA_IF_NUM,
		.instances_dist = DMA_DIST,
		.min_wr_breg = mmDMA_IF_W_S_SOB_MIN_WPRIV_0,
		.max_wr_breg = mmDMA_IF_W_S_SOB_MAX_WPRIV_0,
		.min_rd_breg = mmDMA_IF_W_S_SOB_MIN_RPRIV_0,
		.max_rd_breg = mmDMA_IF_W_S_SOB_MAX_RPRIV_0,
		.hit_wr_breg = mmDMA_IF_W_S_SOB_HIT_WPRIV,
		.hit_rd_breg = mmDMA_IF_W_S_SOB_HIT_RPRIV,
	},
	[DMA_IF_CH0] = {
		.instances_num = DMA_IF_NUM,
		.instances_dist = DMA_DIST,
		.min_wr_breg = mmDMA_IF_W_S_DMA0_MIN_WPRIV_0,
		.max_wr_breg = mmDMA_IF_W_S_DMA0_MAX_WPRIV_0,
		.min_rd_breg = mmDMA_IF_W_S_DMA0_MIN_RPRIV_0,
		.max_rd_breg = mmDMA_IF_W_S_DMA0_MAX_RPRIV_0,
		.hit_wr_breg = mmDMA_IF_W_S_DMA0_HIT_WPRIV,
		.hit_rd_breg = mmDMA_IF_W_S_DMA0_HIT_RPRIV,
	},
	[DMA_IF_CH1] = {
		.instances_num = DMA_IF_NUM,
		.instances_dist = DMA_DIST,
		.min_wr_breg = mmDMA_IF_W_S_DMA1_MIN_WPRIV_0,
		.max_wr_breg = mmDMA_IF_W_S_DMA1_MAX_WPRIV_0,
		.min_rd_breg = mmDMA_IF_W_S_DMA1_MIN_RPRIV_0,
		.max_rd_breg = mmDMA_IF_W_S_DMA1_MAX_RPRIV_0,
		.hit_wr_breg = mmDMA_IF_W_S_DMA1_HIT_WPRIV,
		.hit_rd_breg = mmDMA_IF_W_S_DMA1_HIT_RPRIV,
	},
};

static void set_prot_bits_range_all(struct hl_device *hdev, u32 base, int size, bool priv)
{
	int i, count = size / SNGL_REG_PROT_SIZE;
	u32 prot_base, glbl_priv_reg_29_offset = HL_GLBL_PRIV_REG_OFFSET + 29 * sizeof(u32);

	for (i = 0 ; i < count ; i++) {
		prot_base = base + i * SNGL_REG_PROT_SIZE;

		/* skip setting the 29th GLBL_PRIV reg (i.e., keep GLBL_PRIV
		 * regs protection as NON-PRIVILEGED) to avoid driver being
		 * blocked when configuring GLBL_PRIV regs after a hard-reset.
		 */
		if ((PRIV_PROT_REG(prot_base) & glbl_priv_reg_29_offset) ==
				glbl_priv_reg_29_offset)
			continue;

		/* each write protects 32 registers starting from prot_base */
		if (priv)
			WREG32(PRIV_PROT_REG(prot_base), HL_REGS_ALL_PROT_MASK);
		else
			WREG32(PRIV_PROT_REG(prot_base), HL_REGS_ALL_NON_PROT_MASK);
	}
}

static void set_prot_bits_pages_range_all(struct hl_device *hdev, u32 base,
		u32 dist, int count, bool priv)
{
	int i;
	u32 prot_base;

	for (i = 0 ; i < count ; i++) {
		prot_base = base + dist * i;
		set_prot_bits_range_all(hdev, prot_base, PROT_SIZE_MAX, priv);
	}
}

static void set_prot_bits_priv_int_conf(struct hl_device *hdev, u64 page_base)
{
	/* Calculate the private protection bits base register */
	u32 base = PRIV_PROT_REG(page_base & ~CFG_BASE);

	/* Protect the private and internal registers */
	set_prot_bits_range_all(hdev, base, SNGL_REG_PROT_SIZE * 2, true);
}

static void fw_secure_dma_if_down_regs(struct hl_device *hdev, u32 base)
{
	u32 mask, prot_base = base;

	/* offsets: 0 - 0x580 */
	set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE * 11, true);

	/* offsets: 0x580 - 0x600 */
	prot_base += SNGL_REG_PROT_SIZE * 11;
	mask = ~(PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AW_0) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AW_1) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AW_2) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AW_3) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AW_4) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AW_5) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AW_6));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/*
	 * offsets: 0x600 - 0x680 allowed access
	 * offsets: 0x680 - 0x700 as following:
	 */
	prot_base += SNGL_REG_PROT_SIZE * 2;
	mask = (PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_PRIV_BASE_LOW_AW_0) &
		PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_PRIV_BASE_LOW_AW_1) &
		PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_PRIV_BASE_LOW_AW_2) &
		PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_PRIV_BASE_LOW_AW_3) &
		PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_PRIV_BASE_LOW_AW_4) &
		PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_PRIV_BASE_LOW_AW_5) &
		PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_PRIV_BASE_LOW_AW_6));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets: 0x700 - 0x800 */
	prot_base += SNGL_REG_PROT_SIZE;
	set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE * 2, true);

	/* offsets: 0x800 - 0x880 as following */
	prot_base += SNGL_REG_PROT_SIZE * 2;
	mask = ~(PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_0) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_1) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_2) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_3) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_4) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_5) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_6) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_7) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_8) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_9) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_10) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_11) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_12) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_13) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_14) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_LOW_AR_15) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_HIGH_AR_0) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_HIGH_AR_1) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_HIGH_AR_2) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_HIGH_AR_3) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_HIGH_AR_4) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_HIGH_AR_5) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_BASE_HIGH_AR_6));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets: 0x880 - 0x900 allowed access */

	/* offsets: 0x900 - 0x980 as following: */
	prot_base += SNGL_REG_PROT_SIZE * 2;
	mask = ~(PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_MASK_HIGH_AR_7) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_MASK_HIGH_AR_8) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_MASK_HIGH_AR_9) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_MASK_HIGH_AR_10) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_MASK_HIGH_AR_11) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_MASK_HIGH_AR_12) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_MASK_HIGH_AR_13) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_MASK_HIGH_AR_14) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_MASK_HIGH_AR_15));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets: 0x980 - 0xA00 */
	prot_base += SNGL_REG_PROT_SIZE;
	set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE, true);

	/* offsets: 0xA00 - 0xA80 */
	prot_base += SNGL_REG_PROT_SIZE;
	mask = ~(PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_HIT_AR) &
		 PROT_BIT(mmDMA_IF_W_S_DOWN_CH0_RANGE_SEC_HIT_AW));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets: 0xA80 - 0xF80 */
	prot_base += SNGL_REG_PROT_SIZE;
	set_prot_bits_range_all(hdev, prot_base, PROT_SIZE_MAX - (prot_base - base), true);
}

static void fw_secure_dma_if_regs(struct hl_device *hdev, u32 base)
{
	u32 mask, prot_base = base;

	/* 0 - 0x300 */
	set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE * 6, true);

	/*
	 * 0x300 - 0x400 allowed
	 * 0x400 - 0x500 not allowed access
	 */
	prot_base += SNGL_REG_PROT_SIZE * 8;
	set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE * 2, true);

	/* 0x500 - 0x600 allowed */
	/* 0x600 - 0x700 not allowed */
	prot_base += SNGL_REG_PROT_SIZE * 4;
	set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE * 2, true);

	/* 0x700 - 0x780 as following: */
	prot_base += SNGL_REG_PROT_SIZE * 2;
	mask = ~(PROT_BIT(mmDMA_IF_W_S_DMA0_HIT_RPROT) &
		 PROT_BIT(mmDMA_IF_W_S_DMA0_HIT_WPROT) &
		 PROT_BIT(mmDMA_IF_W_S_DMA1_HIT_RPROT) &
		 PROT_BIT(mmDMA_IF_W_S_DMA1_HIT_WPROT));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* 0x780 - 0xF80 as following: */
	prot_base += SNGL_REG_PROT_SIZE;
	set_prot_bits_range_all(hdev, prot_base, PROT_SIZE_MAX - (prot_base - base), true);
}

static void fw_secure_qm(struct hl_device *hdev, u32 base)
{
	u32 mask, prot_base = base;

	/*
	 * offsets: 0 - 0xC00 - allowed access
	 * offset: 0xC00 - 0xC80 as following:
	 */
	prot_base = base + SNGL_REG_PROT_SIZE * 24;
	mask = (PROT_BIT(mmTPC0_QM_CGM_CFG) &
		PROT_BIT(mmTPC0_QM_CGM_CFG1));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/*
	 * offset: 0xC80 - 0xD00 as following:
	 */
	prot_base += SNGL_REG_PROT_SIZE;
	mask = (PROT_BIT(mmTPC0_QM_IND_GW_APB_CFG) &
		PROT_BIT(mmTPC0_QM_IND_GW_APB_WDATA) &
		PROT_BIT(mmTPC0_QM_IND_GW_APB_RDATA) &
		PROT_BIT(mmTPC0_QM_IND_GW_APB_STATUS));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	prot_base += SNGL_REG_PROT_SIZE * 4;
	set_prot_bits_range_all(hdev, prot_base, PROT_SIZE_MAX - (prot_base - base), true);
}

static void fw_secure_dma(struct hl_device *hdev)
{
	u32 i, base, prot_base, mask;

	for (i = 0 ; i < DMA_IF_NUM ; i++) {
		base = i * DMA_DIST;
		prot_base = base + (mmDMA_IF_W_S_BASE & ~CFG_BASE);
		fw_secure_dma_if_regs(hdev, prot_base);
		prot_base = base + (mmDMA_IF_W_S_DOWN_BASE & ~CFG_BASE);
		set_prot_bits_range_all(hdev, prot_base, PROT_SIZE_MAX, true);
		prot_base = base + (mmDMA_IF_W_S_DOWN_CH0_BASE & ~CFG_BASE);
		fw_secure_dma_if_down_regs(hdev, prot_base);
		prot_base = base + (mmDMA_IF_W_S_DOWN_CH1_BASE & ~CFG_BASE);
		fw_secure_dma_if_down_regs(hdev, prot_base);
	}

	/* Protect open pages of DMA CORE */
	for (i = 0 ; i < DMA_NUMBER_OF_CHANNELS ; i++) {
		base = (mmDMA0_CORE_BASE & ~CFG_BASE) + i * DMA_DIST;

		/* Offsets 0x0 - 0x200 not protected */

		/* Offsets 0x200 - 0x280 */
		prot_base = base + SNGL_REG_PROT_SIZE * 4;
		mask = (PROT_BIT(mmDMA0_CORE_RD_DBGMEM_ADD) &
			PROT_BIT(mmDMA0_CORE_RD_DBGMEM_DATA_WR) &
			PROT_BIT(mmDMA0_CORE_RD_DBGMEM_DATA_RD) &
			PROT_BIT(mmDMA0_CORE_RD_DBGMEM_CTRL) &
			PROT_BIT(mmDMA0_CORE_RD_DBGMEM_RC) &
			PROT_BIT(mmDMA0_CORE_DBG_HBW_AXI_AR_CNT) &
			PROT_BIT(mmDMA0_CORE_DBG_HBW_AXI_AW_CNT) &
			PROT_BIT(mmDMA0_CORE_DBG_LBW_AXI_AW_CNT) &
			PROT_BIT(mmDMA0_CORE_DBG_DESC_CNT) &
			PROT_BIT(mmDMA0_CORE_DBG_STS) &
			PROT_BIT(mmDMA0_CORE_DBG_RD_DESC_ID) &
			PROT_BIT(mmDMA0_CORE_DBG_WR_DESC_ID));
		WREG32(PRIV_PROT_REG(prot_base), mask);

		/* Offsets 0x280 - 0xF80 */
		prot_base += SNGL_REG_PROT_SIZE;
		set_prot_bits_range_all(hdev, prot_base,
					PROT_SIZE_MAX - (prot_base - base), true);

		/* Protect the QM */
		prot_base = (mmDMA0_QM_BASE & ~CFG_BASE) + i * DMA_DIST;
		fw_secure_qm(hdev, prot_base);
	}
}

static void fw_secure_sm(struct hl_device *hdev)
{
	u32 base;
	int i;

	for (i = 0 ; i < SM_ENGINES ; i++) {
		base = (mmSYNC_MNGR_GLBL_W_S_BASE & ~CFG_BASE) + i * SM_DIST;
		set_prot_bits_range_all(hdev, base, PROT_SIZE_MAX, true);
	}
}

static void fw_secure_cpu(struct hl_device *hdev)
{
	u32 prot_base, base = mmCPU_CA53_CFG_BASE & ~CFG_BASE;
	u32 mask;

	/* Protect CPU_CA53_CFG - 4K */
	set_prot_bits_range_all(hdev, base, PROT_SIZE_MAX, true);

	prot_base = base + SNGL_REG_PROT_SIZE * 4; /* offsets: 0x200 - 0x280 */
	mask = ~(PROT_BIT(mmCPU_IF_PF_PQ_PI) &
		PROT_BIT(mmCPU_IF_PQ_BASE_ADDR_LOW) &
		PROT_BIT(mmCPU_IF_PQ_BASE_ADDR_HIGH) &
		PROT_BIT(mmCPU_IF_PQ_LENGTH) &
		PROT_BIT(mmCPU_IF_CQ_BASE_ADDR_LOW) &
		PROT_BIT(mmCPU_IF_CQ_BASE_ADDR_HIGH) &
		PROT_BIT(mmCPU_IF_CQ_LENGTH) &
		PROT_BIT(mmCPU_IF_EQ_BASE_ADDR_LOW) &
		PROT_BIT(mmCPU_IF_EQ_BASE_ADDR_HIGH) &
		PROT_BIT(mmCPU_IF_EQ_LENGTH) &
		PROT_BIT(mmCPU_IF_EQ_RD_OFFS) &
		PROT_BIT(mmCPU_IF_QUEUE_INIT));
	/* allow only secured access */
	WREG32(PRIV_PROT_REG(prot_base), mask);

	prot_base += SNGL_REG_PROT_SIZE;	/* offsets: 0x280 - 0xF80 */
	set_prot_bits_range_all(hdev, prot_base, PROT_SIZE_MAX - (prot_base - base), true);
}

static void fw_secure_pll(struct hl_device *hdev, bool priv)
{
	u32 base;

	/* DMA_W, IF_E, MESH_W, MESH_E */
	base = mmDMA_W_PLL_BASE & ~CFG_BASE;
	set_prot_bits_pages_range_all(hdev, base, 0x20000, 4, priv);

	/* IF_W, DMA_E, SRAM_W, SRAM_E */
	base = mmIF_W_PLL_BASE & ~CFG_BASE;
	set_prot_bits_pages_range_all(hdev, base, 0x20000, 4, priv);

	/* PSOC_CPU, PSOC_MME, PSOC_PCI, PSOC_TPC, PSOC_HBM */
	base = mmPSOC_CPU_PLL_BASE & ~CFG_BASE;
	set_prot_bits_pages_range_all(hdev, base, 0x1000, 5, priv);

	/* NIC0, NIC1 */
	base = mmNIC0_PLL_BASE & ~CFG_BASE;
	set_prot_bits_pages_range_all(hdev, base, 0x40000, 2, priv);

	/* NIC2_HBM, NIC2_MME, NIC2_TPC */
	base = mmNIC2_HBM_PLL_BASE & ~CFG_BASE;
	set_prot_bits_pages_range_all(hdev, base, 0x1000, 3, priv);
}

static void fw_secure_set_prot_bits_if_rtr(struct hl_device *hdev, u32 base)
{
	u32 mask, prot_base = base, reg;

	/* offsets: 0 - 0x400 */
	set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE * 8, true);

	/* offsets: 0x400 - 0x480 - close 3 regs gap between open registers */
	prot_base += SNGL_REG_PROT_SIZE * 8;
	reg = mmSIF_RTR_0_LBW_RANGE_PROT_HIT_AW + sizeof(u32);
	mask = PROT_BIT(reg) & PROT_BIT(reg + sizeof(u32)) &
		PROT_BIT(reg + 2 * sizeof(u32));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets: 0x480 - 0x500 - close 3 regs gap between open registers */
	prot_base += SNGL_REG_PROT_SIZE;
	reg = mmSIF_RTR_0_LBW_RANGE_PROT_HIT_AR + sizeof(u32);
	mask = PROT_BIT(reg) & PROT_BIT(reg + sizeof(u32)) &
		PROT_BIT(reg + 2 * sizeof(u32));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets: 0x500 - 0x580 */
	prot_base += SNGL_REG_PROT_SIZE;
	mask = ~(PROT_BIT(mmSIF_RTR_0_LBW_RANGE_PROT_MAX_AR_8) &
		 PROT_BIT(mmSIF_RTR_0_LBW_RANGE_PROT_MAX_AR_9) &
		 PROT_BIT(mmSIF_RTR_0_LBW_RANGE_PROT_MAX_AR_10) &
		 PROT_BIT(mmSIF_RTR_0_LBW_RANGE_PROT_MAX_AR_11) &
		 PROT_BIT(mmSIF_RTR_0_LBW_RANGE_PROT_MAX_AR_12) &
		 PROT_BIT(mmSIF_RTR_0_LBW_RANGE_PROT_MAX_AR_13) &
		 PROT_BIT(mmSIF_RTR_0_LBW_RANGE_PROT_MAX_AR_14) &
		 PROT_BIT(mmSIF_RTR_0_LBW_RANGE_PROT_MAX_AR_15));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets: 0x580 - 0xF80 */
	prot_base += SNGL_REG_PROT_SIZE;
	set_prot_bits_range_all(hdev, prot_base, PROT_SIZE_MAX - (prot_base - base), true);
}

static void fw_secure_set_prot_bits_if_rtr_ctrl(struct hl_device *hdev, u32 base)
{
	u32 mask, prot_base = base;

	/* offsets: 0 - 0x580 */
	set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE * 11, true);

	/* offsets: 0x580 - 0x600 */
	prot_base += SNGL_REG_PROT_SIZE * 11;
	mask = ~(PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AW_0) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AW_1) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AW_2) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AW_3) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AW_4) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AW_5) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AW_6));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets: 0x600 - 0x680 allowed access */
	prot_base += SNGL_REG_PROT_SIZE;
	WREG32(PRIV_PROT_REG(prot_base), ~0);

	/* offsets: 0x680 - 0x700 as following: */
	prot_base += SNGL_REG_PROT_SIZE;
	mask = (PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_PRIV_BASE_LOW_AW_0) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_PRIV_BASE_LOW_AW_1) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_PRIV_BASE_LOW_AW_2) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_PRIV_BASE_LOW_AW_3) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_PRIV_BASE_LOW_AW_4) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_PRIV_BASE_LOW_AW_5) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_PRIV_BASE_LOW_AW_6));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets: 0x700 - 0x800 not allowed access */
	prot_base += SNGL_REG_PROT_SIZE;
	set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE * 2, true);

	/* offsets: 0x800 - 0x880 as following: */
	prot_base += SNGL_REG_PROT_SIZE * 2;
	mask = ~(PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_0) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_1) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_2) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_3) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_4) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_5) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_6) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_7) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_8) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_9) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_10) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_11) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_12) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_13) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_14) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_LOW_AR_15) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_HIGH_AR_0) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_HIGH_AR_1) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_HIGH_AR_2) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_HIGH_AR_3) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_HIGH_AR_4) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_HIGH_AR_5) &
		 PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_BASE_HIGH_AR_6));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets: 0x880 - 0x900 allowed access */
	prot_base += SNGL_REG_PROT_SIZE;
	WREG32(PRIV_PROT_REG(prot_base), ~0);

	/* offsets: 0x900 - 0x980 as following: */
	prot_base += SNGL_REG_PROT_SIZE;
	mask = ~(PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_MASK_HIGH_AR_7) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_MASK_HIGH_AR_8) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_MASK_HIGH_AR_9) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_MASK_HIGH_AR_10) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_MASK_HIGH_AR_11) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_MASK_HIGH_AR_12) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_MASK_HIGH_AR_13) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_MASK_HIGH_AR_14) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_MASK_HIGH_AR_15));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets: 0x980 - 0xA00 not allowed access */
	prot_base += SNGL_REG_PROT_SIZE;
	set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE, true);

	/* offsets: 0xA00 - 0xA80 as following: */
	prot_base += SNGL_REG_PROT_SIZE;
	mask = ~(PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_HIT_AW) &
		PROT_BIT(mmSIF_RTR_CTRL_0_RANGE_SEC_HIT_AR));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets: 0xA80 - 0xF80 */
	prot_base += SNGL_REG_PROT_SIZE;
	set_prot_bits_range_all(hdev, prot_base, PROT_SIZE_MAX - (prot_base - base), true);
}

static void fw_secure_if(struct hl_device *hdev)
{
	int i;
	u32 prot_base, base = mmSIF_RTR_0_BASE & ~CFG_BASE;

	for (i = 0 ; i < IF_RTRS_NUM ; i++) {
		prot_base = base + i * IF_RTRS_DIST;
		fw_secure_set_prot_bits_if_rtr(hdev, prot_base);

		prot_base += IF_RTR_TO_RTR_CTRL_DIST;
		fw_secure_set_prot_bits_if_rtr_ctrl(hdev, prot_base);
	}
}

static void fw_secure_psoc_global_conf(struct hl_device *hdev)
{
	u32 mask, prot_base, base = mmPSOC_GLOBAL_CONF_BASE & ~CFG_BASE;

	prot_base = base;	/* offsets: 0 - 0x80 */
	mask = ~(PROT_BIT(mmPSOC_GLOBAL_CONF_NON_RST_FLOPS_0) &
		PROT_BIT(mmPSOC_GLOBAL_CONF_BTM_FSM));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	prot_base += SNGL_REG_PROT_SIZE;	/* offsets: 0x80 - 0x100 */
	set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE, true);

	/* Unprotect SCRATCHPADs: offsets: 0x100 - 0x180 */
	prot_base += SNGL_REG_PROT_SIZE;
	WREG32(PRIV_PROT_REG(prot_base), ~0);

	/* offsets: 0x180 - 0x300 */
	prot_base += SNGL_REG_PROT_SIZE;
	set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE * 3, true);

	/* offsets: 0x300 - 0x380 */
	prot_base += SNGL_REG_PROT_SIZE * 3;
	mask = ~(PROT_BIT(mmPSOC_GLOBAL_CONF_CPU_BOOT_STATUS) &
		PROT_BIT(mmPSOC_GLOBAL_CONF_KMD_MSG_TO_CPU) &
		PROT_BIT(mmPSOC_GLOBAL_CONF_TRACE_ADDR) &
		PROT_BIT(mmPSOC_GLOBAL_CONF_TRACE_AWUSER) &
		PROT_BIT(mmPSOC_GLOBAL_CONF_TRACE_ARUSER));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	prot_base += SNGL_REG_PROT_SIZE;	/* offsets: 0x380 - 0xF80 */
	set_prot_bits_range_all(hdev, prot_base, PROT_SIZE_MAX - (prot_base - base), true);
}

static void fw_secure_psoc(struct hl_device *hdev)
{
	u32 mask, base;

	/* Set PSOC level to privilege */
	mask = BIT(PSOC_GLOBAL_CONF_PROT_AR_SHIFT) |
		BIT(PSOC_GLOBAL_CONF_PROT_AW_SHIFT);
	WREG32(mmPSOC_GLOBAL_CONF_PROT, mask);

	/* Note: PSOC_TIMESTAMP should not be protected */

	/* Protect EFUSE */
	base = mmPSOC_EFUSE_BASE & ~CFG_BASE;
	set_prot_bits_range_all(hdev, base, PROT_SIZE_MAX, true);

	/* Protect GLOBAL_CONF - 4K */
	fw_secure_psoc_global_conf(hdev);

	/* Set ETR level to user */
	mask = BIT(PSOC_ETR_AXICTL_PROTCTRLBIT1_SHIFT);
	WREG32(mmPSOC_ETR_AXICTL, mask);
}

static void fw_secure_mmu_up(struct hl_device *hdev)
{
	u32 mask, prot_base, base = mmMMU_UP_BASE & ~CFG_BASE;

	/* blocking regs within offsets: 0 - 0x80 */
	prot_base = base;
	mask = ~(PROT_BIT(mmMMU_UP_MMU_ENABLE) &
		 PROT_BIT(mmMMU_UP_PAGE_ERROR_CAPTURE) &
		 PROT_BIT(mmMMU_UP_PAGE_ERROR_CAPTURE_VA) &
		 PROT_BIT(mmMMU_UP_ACCESS_ERROR_CAPTURE) &
		 PROT_BIT(mmMMU_UP_ACCESS_ERROR_CAPTURE_VA) &
		 PROT_BIT(mmMMU_UP_RAZWI_WRITE_VLD) &
		 PROT_BIT(mmMMU_UP_RAZWI_WRITE_ID) &
		 PROT_BIT(mmMMU_UP_RAZWI_READ_VLD) &
		 PROT_BIT(mmMMU_UP_RAZWI_READ_ID));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	prot_base += SNGL_REG_PROT_SIZE;	/* offsets: 0x80 - 0xF80 */
	set_prot_bits_range_all(hdev, prot_base, PROT_SIZE_MAX - (prot_base - base), true);
}

static void fw_secure_stlb(struct hl_device *hdev)
{
	u32 mask, prot_base, base = mmSTLB_BASE & ~CFG_BASE;

	/* keep only regs used by LKD: offsets: 0 - 0x80 */
	prot_base = base;
	mask = ~(PROT_BIT(mmSTLB_CACHE_INV) &
		 PROT_BIT(mmSTLB_INV_PS) &
		 PROT_BIT(mmSTLB_INV_SET) &
		 PROT_BIT(mmSTLB_MEM_CACHE_INVALIDATION));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	prot_base += SNGL_REG_PROT_SIZE;	/* offsets: 0x80 - 0xF80 */
	set_prot_bits_range_all(hdev, prot_base, PROT_SIZE_MAX - (prot_base - base), true);
}

static void fw_secure_pci(struct hl_device *hdev)
{
	/* protect registers in MMU_UP */
	fw_secure_mmu_up(hdev);

	/* protect registers in STLB */
	fw_secure_stlb(hdev);

	/* The rest is protected by RRs */
}

static void fw_secure_mme_acc(struct hl_device *hdev)
{
	u32 reg, mask, prot_base, base;
	int i;

	for (i = 0 ; i < MMES_NUM ; i++) {
		base = (mmMME0_ACC_BASE & ~CFG_BASE) + MMES_DIST * i;
		/* Set ACC to user */
		reg = mmMME0_ACC_PROT + MMES_DIST * i;
		WREG32(reg, PROT_OVR_USER);

		/* blocking regs within offsets: 0 - 0x80 */
		prot_base = base;
		mask = ~(PROT_BIT(mmMME0_ACC_ACC_STALL) &
			 PROT_BIT(mmMME0_ACC_WBC));
		WREG32(PRIV_PROT_REG(prot_base), mask);

		prot_base += SNGL_REG_PROT_SIZE; /* offsets: 0x80 - 0xF80 */
		set_prot_bits_range_all(hdev, prot_base,
					PROT_SIZE_MAX - (prot_base - base), true);
	}
}

static void fw_secure_mme_sbab(struct hl_device *hdev)
{
	u32 reg, mask, prot_base, base;
	int i;

	for (i = 0 ; i < MMES_NUM ; i++) {
		base = (mmMME0_SBAB_BASE & ~CFG_BASE) + MMES_DIST * i;
		/* Set SBAB to user */
		reg = mmMME0_SBAB_PROT + MMES_DIST * i;
		WREG32(reg, PROT_OVR_USER);

		/* blocking regs within offsets: 0 - 0x80 */
		prot_base = base;
		mask = ~(PROT_BIT(mmMME0_SBAB_SB_STALL) &
			 PROT_BIT(mmMME0_SBAB_ARUSER0) &
			 PROT_BIT(mmMME0_SBAB_ARUSER1));
		WREG32(PRIV_PROT_REG(prot_base), mask);

		prot_base += SNGL_REG_PROT_SIZE; /* offsets: 0x80 - 0xF80 */
		set_prot_bits_range_all(hdev, prot_base,
					PROT_SIZE_MAX - (prot_base - base), true);
	}
}

static void fw_secure_mme(struct hl_device *hdev)
{
	u32 reg;
	int i;

	/* Protect the ACC */
	fw_secure_mme_acc(hdev);
	/* Protect the SBAB */
	fw_secure_mme_sbab(hdev);

	for (i = 0 ; i < MMES_NUM ; i++) {
		/* Set CTRL to user */
		reg = mmMME0_CTRL_PROT + MMES_DIST * i;
		WREG32(reg, PROT_OVR_USER);
		/* Protect the CTRL configuration */
		WREG32(PRIV_PROT_REG(reg), PROT_BIT(mmMME0_CTRL_PROT));
		set_prot_bits_priv_int_conf(hdev, mmMME0_CTRL_BASE + MMES_DIST * i);

		/*
		 * For MME1 and MME3 QMANs configuration the LBW clock
		 * is disabled by default, so we have to enable the LBW clock
		 * before accessing, otherwise we stuck.
		 */
		if (i & BIT(0)) {
			reg = mmMME0_CTRL_QM_SLV_LBW_CLK_EN + MMES_DIST * i;
			WREG32(reg, 1);
		}

		/* Protect the QM */
		reg = (MMES_DIST * i) + (mmMME0_QM_BASE & ~CFG_BASE);
		fw_secure_qm(hdev, reg);

		/* close the clock back */
		if (i & BIT(0)) {
			reg = mmMME0_CTRL_QM_SLV_LBW_CLK_EN + MMES_DIST * i;
			WREG32(reg, 0);
		}
	}
}

static void fw_secure_tpc_single(struct hl_device *hdev, u32 base)
{
	u32 mask, prot_base;

	/* offsets 0x0 - 0x900: unprotected */

	/* offsets 0x900 - 0x980 */
	prot_base = base + SNGL_REG_PROT_SIZE * 18;
	mask = PROT_BIT(mmTPC0_CFG_PROT);
	if (base == (mmTPC3_CFG_BASE & ~CFG_BASE)) {
		/*
		 * TPC3 pre-fetcher does not work for PCIe address space range.
		 * Block the register from host changes.
		 */
		mask &= PROT_BIT(mmTPC0_CFG_ICACHE_BASE_ADDERESS_HIGH);
	}
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets 0x980 - 0xA00 */
	prot_base += SNGL_REG_PROT_SIZE;
	mask = (PROT_BIT(mmTPC0_CFG_DBGMEM_ADD) &
		PROT_BIT(mmTPC0_CFG_DBGMEM_DATA_WR) &
		PROT_BIT(mmTPC0_CFG_DBGMEM_DATA_RD) &
		PROT_BIT(mmTPC0_CFG_DBGMEM_CTRL) &
		PROT_BIT(mmTPC0_CFG_DBGMEM_RC));
	WREG32(PRIV_PROT_REG(prot_base), mask);

	/* offsets 0xA00 - 0xE80: unprotected */

	/* offsets 0xE80 - 0xF80 */
	prot_base += SNGL_REG_PROT_SIZE * 10;
	set_prot_bits_range_all(hdev, prot_base, PROT_SIZE_MAX - (prot_base - base), true);
}

static void fw_secure_tpc(struct hl_device *hdev)
{
	u32 mask, reg, base;
	int i;

	/* Set TPC engines level to user */
	mask = PROT_OVR_USER << TPC0_CFG_PROT_ARPROT_SHIFT |
		PROT_OVR_USER << TPC0_CFG_PROT_AWPROT_SHIFT;
	for (i = 0 ; i < NUM_OF_TPC_ENGINES ; i++) {
		/* Set protection level to user */
		reg = mmTPC0_CFG_PROT + TPCS_DIST * i;
		WREG32(reg, mask);

		base = (mmTPC0_CFG_BASE & ~CFG_BASE) + TPCS_DIST * i;
		fw_secure_tpc_single(hdev, base);

		/* Protect the E2E Creds */
		reg = mmTPC0_E2E_CRED_BASE & ~CFG_BASE;
		set_prot_bits_range_all(hdev, reg + TPCS_DIST * i, PROT_SIZE_MAX, true);

		/* Protect the QM */
		reg = (mmTPC0_QM_BASE & ~CFG_BASE) + TPCS_DIST * i;
		fw_secure_qm(hdev, reg);
	}
}

static void fw_secure_dbg(struct hl_device *hdev)
{
	int i;
	u32 prot_base, base;

	for (i = 0 ; i < NUM_OF_TPC_ENGINES ; i++) {
		base = DBG_TPC_DIST * i;

		/* Protect DBG TPC_EML_CFG */
		prot_base = (mmTPC0_EML_CFG_BASE & ~CFG_BASE) + base;
		set_prot_bits_range_all(hdev, prot_base, SNGL_REG_PROT_SIZE * 29, true);
		WREG32(PRIV_PROT_REG(prot_base + INT_CFG_REGS_OFF), HL_REGS_ALL_PROT_MASK);
		/*
		 * The Protection Bits register protecting the privilege bits
		 * must be accessed the last due to DBG protocol not having the
		 * protection bits, therefore the privilege level here does not
		 * play any role on access. So further accesses (no matter the
		 * initiator) will result in exception.
		 */
		WREG32(PRIV_PROT_REG(prot_base + HL_GLBL_PRIV_REG_OFFSET), HL_REGS_ALL_PROT_MASK);
	}
}

static void fw_secure_set_nic_eng_role(struct hl_device *hdev, u32 prot_reg,
					u32 mask, int eng_cnt)
{
	int i, j;
	u32 reg;

	for (i = 0 ; i < GAUDI_NICS_NUM ; i++) {
		for (j = 0 ; j < eng_cnt ; j++) {
			/* Set the engine role (security level) */
			reg = prot_reg + GAUDI_NICS_DIST * i +
							GAUDI_NIC_ENGS_DIST * j;
			WREG32(reg, mask);
		}
	}
}

/*
 * fw_secure_prot_nics_engs_regs	- protects several registers (up to 32)
 *					  that are protected by the same
 *					  protection register
 *
 * @base_reg	- one of the registers to protect - used for calculating the
 *		  protection register offset.
 * @mask	- the mask to write into the protection register
 * @eng_cnt	- amount of engines to work on (usually 1, or 2)
 */
static void fw_secure_prot_nics_engs_regs(struct hl_device *hdev, u32 base_reg,
						u32 mask, int eng_cnt)
{
	int i, j;
	u32 reg;

	for (i = 0 ; i < GAUDI_NICS_NUM ; i++) {
		for (j = 0 ; j < eng_cnt ; j++) {
			reg = base_reg + GAUDI_NICS_DIST * i +
							GAUDI_NIC_ENGS_DIST * j;
			/* Protect the engine configuration */
			WREG32(PRIV_PROT_REG(reg), mask);
			/* Protect the private prot bits and config space */
			set_prot_bits_priv_int_conf(hdev, PAGE_BASE(reg));
		}
	}
}

/* This function only suitable for single PROT OVR register protection */
static void fw_secure_nic_role_single(struct hl_device *hdev, u32 reg, u32 mask, int eng_cnt)
{
	u32 prot_mask = PROT_BIT(reg);

	fw_secure_set_nic_eng_role(hdev, reg, mask, eng_cnt);
	fw_secure_prot_nics_engs_regs(hdev, reg, prot_mask, eng_cnt);
}

static void fw_secure_nic_open_pages(struct hl_device *hdev)
{
	int i;
	u32 nic_off;
	u64 base;

	for (i = 0 ; i < GAUDI_NICS_NUM ; i++) {
		nic_off = GAUDI_NICS_DIST * i;

		/* STAT */
		base = nic_off + mmNIC0_STAT_BASE;
		set_prot_bits_priv_int_conf(hdev, base);

		/* MAC CORE */
		base = nic_off + mmNIC0_MAC_CORE_BASE;
		set_prot_bits_priv_int_conf(hdev, base);

		/* MAC AUX */
		base = nic_off + mmNIC0_MAC_AUX_BASE;
		set_prot_bits_priv_int_conf(hdev, base);

		/* TXB */
		base = nic_off + mmNIC0_TXB_BASE;
		set_prot_bits_priv_int_conf(hdev, base);
	}
}

static void fw_secure_nic_qm_pages(struct hl_device *hdev, u32 base)
{
	u32 port;

	for (port = 0 ; port < GAUDI_NICS_NUM ; port++) {
		fw_secure_qm(hdev, base);
		base += GAUDI_NICS_DIST;
	}
}

static void fw_secure_nic_qm(struct hl_device *hdev)
{
	u32 base = mmNIC0_QM0_BASE & ~CFG_BASE;

	fw_secure_nic_qm_pages(hdev, base);
	fw_secure_nic_qm_pages(hdev, base + NIC_QM_DIST);
}

static void fw_secure_nic_phy(struct hl_device *hdev)
{
	u32 mask, port, prot_base, base = mmNIC0_PHY_BASE & ~CFG_BASE;

	for (port = 0 ; port < GAUDI_NICS_NUM ; port++) {
		/* Protect PHY_RX_CFG registers in each NIC block */
		mask = (PROT_BIT(mmNIC0_PHY_PHY_RX_CFG_0) &
			PROT_BIT(mmNIC0_PHY_PHY_RX_CFG_1) &
			PROT_BIT(mmNIC0_PHY_PHY_RX_CFG_2) &
			PROT_BIT(mmNIC0_PHY_PHY_RX_CFG_3));
		WREG32(PRIV_PROT_REG(base), mask);

		/* offsets: 0x80 - 0xF60 */
		prot_base = base + SNGL_REG_PROT_SIZE;
		set_prot_bits_range_all(hdev, prot_base,
					PHY_RX_PROTECT_HIGH - SNGL_REG_PROT_SIZE, true);
		base += GAUDI_NICS_DIST;
	}
}

static void fw_secure_nic(struct hl_device *hdev)
{
	u32 reg, mask;

	/* STAT, MAC CORE/AUX, TXB */
	fw_secure_nic_open_pages(hdev);

	/* QPC */
	reg = mmNIC0_QPC0_AXI_PROT;
	mask = PROT_OVR_SECURE << NIC0_QPC0_AXI_PROT_R_SHIFT;
	fw_secure_nic_role_single(hdev, reg, mask, 2);

	/* TODO - CI has a check-patch exception over a typo in nic0_rxb_masks.h.
	 * Until fixed, shift mask is temporarily defined below.
	 */
	#define NIC0_RXB_AXI_AXPROT_PRIV_AXPROT_SHIFT 0

	/* RXB */
	reg = mmNIC0_RXB_AXI_AXPROT_PRIV;
	mask = PROT_OVR_SECURE << NIC0_RXB_AXI_AXPROT_PRIV_AXPROT_SHIFT;
	fw_secure_nic_role_single(hdev, reg, mask, 1);

	/* TXS */
	reg = mmNIC0_TXS0_AXI_PROT;
	mask = PROT_OVR_SECURE << NIC0_TXS0_AXI_PROT_R_SHIFT;
	fw_secure_nic_role_single(hdev, reg, mask, 2);

	/* TXE DATA, TXE WQE */
	reg = mmNIC0_TXE0_DATA_FETCH_AXI_PROT;
	mask = PROT_OVR_USER << NIC0_TXE0_DATA_FETCH_AXI_PROT_UNSECURED_SHIFT;
	fw_secure_nic_role_single(hdev, reg, mask, 2);
	reg = mmNIC0_TXE0_WQE_FETCH_AXI_PROT;
	mask = PROT_OVR_USER << NIC0_TXE0_WQE_FETCH_AXI_PROT_UNSECURED_SHIFT;
	fw_secure_nic_role_single(hdev, reg, mask, 2);

	/* RXE AW LBW, RXE AR HBW */
	reg = mmNIC0_RXE0_AWPROT_LBW;
	mask = PROT_OVR_USER << NIC0_RXE0_AWPROT_LBW_AWPROT_LBW_UNSECURED_SHIFT;
	fw_secure_set_nic_eng_role(hdev, reg, mask, 2);
	reg = mmNIC0_RXE0_ARPROT_HBW;
	mask = PROT_OVR_USER << NIC0_RXE0_ARPROT_HBW_ARPROT_HBW_UNSECURED_SHIFT;
	mask |= PROT_OVR_USER << NIC0_RXE0_ARPROT_HBW_ARPROT_HBW_SECURED_SHIFT;
	mask |= PROT_OVR_USER <<
		NIC0_RXE0_ARPROT_HBW_ARPROT_HBW_PRIVILEGED_SHIFT;
	fw_secure_set_nic_eng_role(hdev, reg, mask, 2);
	mask = PROT_BIT(mmNIC0_RXE0_AWPROT_LBW) & PROT_BIT(reg);
	fw_secure_prot_nics_engs_regs(hdev, reg, mask, 2);

	/* TMR LIST, TMR STATE */
	reg = mmNIC0_TMR_TMR_LIST_AXI_PROT;
	mask = PROT_OVR_SECURE << NIC0_TMR_TMR_LIST_AXI_PROT_R_SHIFT;
	fw_secure_set_nic_eng_role(hdev, reg, mask, 1);
	reg = mmNIC0_TMR_TMR_STATE_AXI_PROT;
	mask = PROT_OVR_SECURE << NIC0_TMR_TMR_STATE_AXI_PROT_R_SHIFT;
	fw_secure_set_nic_eng_role(hdev, reg, mask, 1);
	mask = PROT_BIT(mmNIC0_TMR_TMR_LIST_AXI_PROT) & PROT_BIT(reg);
	fw_secure_prot_nics_engs_regs(hdev, reg, mask, 1);

	fw_secure_nic_phy(hdev);
	fw_secure_nic_qm(hdev);
}

static void fw_secure_sram_cfg(struct hl_device *hdev)
{
	u32 base;

	base = mmSRAM_Y0_X0_BANK_BASE & ~CFG_BASE;
	set_prot_bits_pages_range_all(hdev, base, SRAM_BANKS_DIST, SRAM_BANKS_NUM, true);

	/*
	 * There is an integration version mismatch bug in Gaudi
	 * resulting in absence of the Privilege protection bits registers.
	 * Therefore we cannot use the protection bits to protect the
	 * SRAM_Y_X_RTRs.
	 * LBW RRs are used instead.
	 */
}

static void fw_secure_pcie_rr_wr(struct hl_device *hdev, int rr, u64 min64, u64 max64)
{
	u32 min = (u32)min64;
	u32 max = (u32)max64;
	u32 rr_base = rr * sizeof(u32), rr_dest;

	/* Set WP RR */
	rr_dest = rr_base + mmPCIE_WRAP_RR_WR_PRV_RANGE_REG_MIN_0;
	WREG32(rr_dest, min);
	rr_dest = rr_base + mmPCIE_WRAP_RR_WR_PRV_RANGE_REG_MAX_0;
	WREG32(rr_dest, max);
}

static void fw_secure_pcie_rr_rd(struct hl_device *hdev, int rr, u64 min64, u64 max64)
{
	u32 min = (u32)min64;
	u32 max = (u32)max64;
	u32 rr_base = rr * sizeof(u32), rr_dest;

	/* Set RP RR */
	rr_dest = rr_base + mmPCIE_WRAP_RR_RD_PRV_RANGE_REG_MIN_0;
	WREG32(rr_dest, min);
	rr_dest = rr_base + mmPCIE_WRAP_RR_RD_PRV_RANGE_REG_MAX_0;
	WREG32(rr_dest, max);
}

static void fw_secure_pcie_rr(struct hl_device *hdev, int rr, u64 min64, u64 max64)
{
	/* We skip the PCIE RRs that, by default, are configured by BTL */

	if ((rr != LBW_RR_MME1_QM) && (rr != LBW_RR_MME3_QM_SRAM_CFG) &&
			(rr != LBW_RR_CPU_TSTAMP)) {
		fw_secure_pcie_rr_wr(hdev, rr, min64, max64);
		fw_secure_pcie_rr_rd(hdev, rr, min64, max64);
	}
}

static void fw_secure_lbw_rr_gen(struct hl_device *hdev, int rr, u64 min64, u64 max64,
				      enum fw_sec_lbw_rr_type type)
{
	int i;
	u32 min = min64 & ~CFG_BASE;
	u32 max = max64 & ~CFG_BASE;
	u32 rr_base, rr_dest, hit;
	struct fw_sec_lbw_rrs_cfg *cfg = &fw_sec_lbw_rrs_cfg[type];

	for (i = 0 ; i < cfg->instances_num ; i++) {
		/* TODO - configuring the 8th PRIV RTR LBW RR causes undefined ASIC behavior.
		 * It's therefore being skipped, until SW-103309 is resolved and/or we
		 * find another way to handle it.
		 */
		if (i == 7)
			continue;

		rr_base = rr * sizeof(u32) + cfg->instances_dist * i;
		rr_dest = rr_base + cfg->min_wr_breg;
		WREG32(rr_dest, min);
		rr_dest = rr_base + cfg->max_wr_breg;
		WREG32(rr_dest, max);
		rr_dest = rr_base + cfg->min_rd_breg;
		WREG32(rr_dest, min);
		rr_dest = rr_base + cfg->max_rd_breg;
		WREG32(rr_dest, max);

		/* Enable (hit) RR */
		rr_base = cfg->instances_dist * i;
		rr_dest = rr_base + cfg->hit_wr_breg;
		hit = RREG32(rr_dest);
		WREG32(rr_dest, hit | BIT(rr));

		rr_dest = rr_base + cfg->hit_rd_breg;
		hit = RREG32(rr_dest);
		WREG32(rr_dest, hit | BIT(rr));
	}
}

static void fw_secure_lbw_rr_if(struct hl_device *hdev, int rr, u64 min, u64 max)
{
	fw_secure_lbw_rr_gen(hdev, rr, min, max, IF_RTR);
	fw_secure_lbw_rr_gen(hdev, rr, min, max, DMA_IF_SOB);
	fw_secure_lbw_rr_gen(hdev, rr, min, max, DMA_IF_CH0);
	fw_secure_lbw_rr_gen(hdev, rr, min, max, DMA_IF_CH1);
}

static void fw_secure_lbw_rr(struct hl_device *hdev, int rr, u64 min, u64 max)
{
	fw_secure_lbw_rr_if(hdev, rr, min, max);
	fw_secure_pcie_rr(hdev, rr, min, max);
}

static void fw_secure_all_lbw_rr(struct hl_device *hdev)
{
	int i;
	u64 min, max, base;

	/*
	 * NOTE: The LBW and PCIe RRs HIT is: min < hit < max
	 * meaning - not inclusive, so we need to provide it the min address
	 * below the forbidden one and max address above the forbidden one.
	 */

	/* MME1_QM: RR(LBW_RR_MME1_QM) */
	min = mmMME1_QM_BASE - 1;
	max = mmMME2_ACC_BASE;
	fw_secure_lbw_rr(hdev, LBW_RR_MME1_QM, min, max);

	/* MME3_QM, SRAM_CFG up to SIF: RR(LBW_RR_MME3_QM_SRAM_CFG) */
	min = mmMME3_QM_BASE - 1;
	max = mmSIF_RTR_0_BASE;
	fw_secure_lbw_rr(hdev, LBW_RR_MME3_QM_SRAM_CFG, min, max);

	/* CPU_TIMESTAMP: RR(LBW_RR_CPU_TSTAMP) */
	min = mmCPU_TIMESTAMP_BASE - 1;
	max = mmDMA_IF_W_S_BASE;
	fw_secure_lbw_rr(hdev, LBW_RR_CPU_TSTAMP, min, max);

	/* HBM_CFG up to PCIE_WRAP: RR(LBW_RR_HBM_GIC) */
	min = mmHBM0_BASE - 1;
	max = mmPCIE_WRAP_BASE;
	fw_secure_lbw_rr(hdev, LBW_RR_HBM_GIC, min, max);

	/* PCIE_DBI up to PCIE_CORE_L: RR(LBW_RR_PCIE_DBI_COREBGN) */
	min = mmPCIE_DBI_BASE - 1;
	max = CFG_BASE + mmPCIE_CORE_MSI_REQ;
	fw_secure_lbw_rr(hdev, LBW_RR_PCIE_DBI_COREBGN, min, max);

	/* PCIE_CORE_H up to MMU_UP: RR(LBW_RR_PCIE_COREEND_AUX_PHY) */
	min = max + sizeof(u32) - 1;
	max = mmMMU_UP_BASE;
	fw_secure_lbw_rr(hdev, LBW_RR_PCIE_COREEND_AUX_PHY, min, max);

	/* PCIE_MSI up to PSOC_TIMESTAMP: RR(LBW_RR_PCIE_MSI_PSOC0) */
	min = mmPCIE_MSI_BASE - 1;
	max = mmPSOC_TIMESTAMP_BASE;
	fw_secure_lbw_rr(hdev, LBW_RR_PCIE_MSI_PSOC0, min, max);

	/* PSOC_GPIO0 up to NIC0_MAC_CORE_BASE: RR(LBW_RR_PSOC1_NIC0_MAC_CH) */
	min = mmPSOC_GPIO0_BASE - 1;
	max = mmNIC0_MAC_CORE_BASE;
	fw_secure_lbw_rr(hdev, LBW_RR_PSOC1_NIC0_MAC_CH, min, max);

	/* NIC0_TS up to NIC1_MAC_CORE_BASE: RR(LBW_RR_NIC0_TS_NIC1_MAC_CH) */
	min = mmNIC0_TS_BASE - 1;
	max = mmNIC1_MAC_CORE_BASE;
	fw_secure_lbw_rr(hdev, LBW_RR_NIC0_TS_NIC1_MAC_CH, min, max);

	/* NIC1_TS up to NIC2_MAC_CORE_BASE: RR(LBW_RR_NIC1_TS_NIC2_MAC_CH) */
	min = mmNIC1_TS_BASE - 1;
	max = mmNIC2_MAC_CORE_BASE;
	fw_secure_lbw_rr(hdev, LBW_RR_NIC1_TS_NIC2_MAC_CH, min, max);

	/* NIC3_MAC_CH0 up to NIC3_MAC_CORE_BASE: RR(LBW_RR_NIC3_MAC_CH) */
	min = mmNIC3_MAC_CH0_BASE - 1;
	max = mmNIC3_MAC_CORE_BASE;
	fw_secure_lbw_rr(hdev, LBW_RR_NIC3_MAC_CH, min, max);

	/* NIC3_TS up to NIC4_MAC_CORE_BASE: RR(LBW_RR_NIC3_TS_NIC4_MAC_CH) */
	min = mmNIC3_TS_BASE - 1;
	max = mmNIC4_MAC_CORE_BASE;
	fw_secure_lbw_rr(hdev, LBW_RR_NIC3_TS_NIC4_MAC_CH, min, max);

	/* CFG HIGH GAP: 0x7FFD000000 - 0x7FFE000000 */
	min = CFG_HIGH_GAP_BASE - 1;
	max = DBG_BASE;
	fw_secure_lbw_rr(hdev, LBW_RR_CFG_HIGH_GAP, min, max);

	/* mmPSOC_ETR_AXICTL (single register): RR(LBW_RR_ETR) */
	min = CFG_BASE + mmPSOC_ETR_AXICTL - 1;
	max = CFG_BASE + mmPSOC_ETR_AXICTL + sizeof(u32);
	fw_secure_pcie_rr(hdev, LBW_RR_DBG_ETR, min, max);

	/*
	 * H3_errata:
	 * Due to Router LBW address decoder bug
	 * RR is defined for DBG block such that
	 * DBG access via the address decoder are
	 * blocked. Only PCIe/CA53/PSOC are allowed
	 * to access as they do not contain address
	 * decoding.
	 */
	/* Debug address space : 0x7FFE000000 - 0x7FFFFFFFFF */
	min = mmMME_S_ROM_TABLE_BASE - 1;
	/*
	 * Spl case: As per LBW RR config 'min<hit<max' which means
	 * address ranges of min-1 and max+1 should be configured, as
	 * hit is not inclusive. But for DBG address max address is 0x7FFFFFFFFF
	 * and max+1(0x80_0000_0000) is not a valid location.
	 * Hence max+1 logic should not be applied for this case alone
	 * and the max address is configured as 0x7FFFFFFFFF and not
	 * 0x80_0000_0000.
	 */
	max = mmTPC7_EML_CS_BASE + TPC7_EML_CS_MAX_OFFSET - 1;
	fw_secure_lbw_rr_if(hdev, LBW_RR_DBG_ETR, min, max);

	/* SYNC_MNGR: PCIe only! PCIe RR_26 up to PCIe RR_29 */
	for (i = 0 ; i < 4 ; i++) {
		base = mmSYNC_MNGR_GLBL_W_S_BASE + SM_OBJS_MON_OFF;
		base += SM_DIST * i;
		min = base - 1;
		max = base + SM_OBJS_MON_SZ;
		fw_secure_pcie_rr(hdev, 26 + i, min, max);
	}
}

static void fw_hbw_rr_set_rtr_wr(struct hl_device *hdev, int rr, int rtr, u64 base, u64 mask,
				      bool is_lock)
{
	u32 base_l = (u32)base, base_h = (u32)(base >> BITS_IN_REG);
	u32 mask_l = (u32)mask, mask_h = (u32)(mask >> BITS_IN_REG);
	u32 rr_base, rr_dest, hit;

	rr_base = rr * sizeof(u32) + IF_RTRS_DIST * rtr;
	rr_dest = rr_base + mmSIF_RTR_CTRL_0_RANGE_PRIV_BASE_LOW_AW_0;
	WREG32(rr_dest, base_l);
	rr_dest = rr_base + mmSIF_RTR_CTRL_0_RANGE_PRIV_BASE_HIGH_AW_0;
	WREG32(rr_dest, base_h);
	rr_dest = rr_base + mmSIF_RTR_CTRL_0_RANGE_PRIV_MASK_LOW_AW_0;
	WREG32(rr_dest, mask_l);
	rr_dest = rr_base + mmSIF_RTR_CTRL_0_RANGE_PRIV_MASK_HIGH_AW_0;
	WREG32(rr_dest, mask_h);

	/* Enable (hit) RR */
	rr_base = IF_RTRS_DIST * rtr;
	rr_dest = rr_base + mmSIF_RTR_CTRL_0_RANGE_PRIV_HIT_AW;
	hit = RREG32(rr_dest);
	if (is_lock)
		WREG32(rr_dest, hit | BIT(rr));
	else
		WREG32(rr_dest, hit & ~BIT(rr));
}

static void fw_hbw_rr_set_rtr_ar(struct hl_device *hdev, int rr, int rtr, u64 base, u64 mask,
				      bool is_lock)
{
	u32 base_l = (u32)base, base_h = (u32)(base >> BITS_IN_REG);
	u32 mask_l = (u32)mask, mask_h = (u32)(mask >> BITS_IN_REG);
	u32 rr_base, rr_dest, hit;

	rr_base = rr * sizeof(u32) + IF_RTRS_DIST * rtr;
	rr_dest = rr_base + mmSIF_RTR_CTRL_0_RANGE_PRIV_BASE_LOW_AR_0;
	WREG32(rr_dest, base_l);
	rr_dest = rr_base + mmSIF_RTR_CTRL_0_RANGE_PRIV_BASE_HIGH_AR_0;
	WREG32(rr_dest, base_h);
	rr_dest = rr_base + mmSIF_RTR_CTRL_0_RANGE_PRIV_MASK_LOW_AR_0;
	WREG32(rr_dest, mask_l);
	rr_dest = rr_base + mmSIF_RTR_CTRL_0_RANGE_PRIV_MASK_HIGH_AR_0;
	WREG32(rr_dest, mask_h);

	/* Enable (hit) RR */
	rr_base = IF_RTRS_DIST * rtr;
	rr_dest = rr_base + mmSIF_RTR_CTRL_0_RANGE_PRIV_HIT_AR;
	hit = RREG32(rr_dest);
	if (is_lock)
		WREG32(rr_dest, hit | BIT(rr));
	else
		WREG32(rr_dest, hit & ~BIT(rr));
}

static void fw_hbw_rr_set_rtr(struct hl_device *hdev, int rr, int rtr, u64 base, u64 mask,
				   bool is_lock)
{
	fw_hbw_rr_set_rtr_wr(hdev, rr, rtr, base, mask, is_lock);
	fw_hbw_rr_set_rtr_ar(hdev, rr, rtr, base, mask, is_lock);
}

static void fw_secure_hbw_rr(struct hl_device *hdev, int rr, u64 base, u64 mask)
{
	int i;

	for (i = 0 ; i < IF_RTRS_NUM ; i++) {
		/* The 8th PRIV RTR HBW RR is already configured by BTL */
		if (i == 7)
			continue;
		fw_hbw_rr_set_rtr(hdev, rr, i, base, mask, true);
	}
}

static void fw_secure_all_hbw_rr(struct hl_device *hdev)
{
	/* STM RR 0x7F_F400_0000 - 0x7F_F800_0000 (64MB) */
	fw_secure_hbw_rr(hdev, HBW_RR_STM, 0x7FF4000000, 0x3FFFFFC000000);

	/* SPI Flash RR 0x7F_F800_0000 - 0x7F_FA00_0000 (32MB) */
	fw_secure_hbw_rr(hdev, HBW_RR_SPI_FLASH, HL_SPI_BASE, 0x3FFFFFE000000);

	/* Scratchpad SRAM RR 0x7F_FBFE_0000 - 0x7F_FBFF_0000 (64KB) */
	fw_secure_hbw_rr(hdev, HBW_RR_SP_SRAM, 0x7FFBFE0000, 0x3FFFFFFFF0000);

	/* PCIe FW SRAM RR 0x7F_FBFF_0000 - 0x7F_FBFF_8000 (32KB) */
	fw_secure_hbw_rr(hdev, HBW_RR_PCIE_FW, 0x7FFBFF0000, 0x3FFFFFFFF8000);

	/* HBM RR 0x00_0000_0000 - 0x00_1000_0000 (256MB) */
	/* Keep RR_0 last, as it replaces the BTL settings */
	fw_secure_hbw_rr(hdev, HBW_RR_HBM, HBM_RR_BASE, HBM_RR_MASK);

	/*
	 * PCIe IF HBW RRs are for direct access from PCIe IF to PSOC domain:
	 * STM, SPI Flash, Scratchpad SRAM, PCIe FW SRAM.
	 * SPSRAM space for fit is used in dynamic RR.
	 * Assign the PCIe IF RR_16, RR_22, RR_24 (0-15 and 26-31 are allocated
	 * for LBW) for this job.
	 * NOTE: The PCIe RRs use min/max concept and HIT is: min < hit < max
	 */
	fw_secure_pcie_rr(hdev, PCIE_DYN_RR_STM_SF, mmSTM_BASE - 1, HL_SPI_END);
	fw_secure_pcie_rr(hdev, 22, HL_SPI_END - 1, FIT_IMG_SPSRAM_BASE);
	fw_secure_pcie_rr(hdev, 24, FIT_IMG_SPSRAM_END - 1, mmRAZWI_BASE);
}

/* PLL blocks remain intact after a hard reset. Therefore, we must first
 * remove their protection before reconfiguring them following a hard reset.
 */
void gaudi_fw_security_emulation_fini(struct hl_device *hdev, bool asic_dirty)
{
	if (hdev->asic_prop.fw_security_enabled)
		return;

	if (hdev->priv_security_enable || asic_dirty) {
		fw_secure_pll(hdev, false);
		hl_info(hdev, "Privileged security disabled\n");
	}
}

void gaudi_fw_security_emulation_init(struct hl_device *hdev)
{
	if (hdev->asic_prop.fw_security_enabled)
		return;

	/* Init sequence (do NOT change this flow unless you're familiar with the dependencies):
	 * 1. Configure all priv PBs, excluding PBs that protect priv RRs (located under PCIE,
	 *    RTR & DMA) as this will block the driver from configuring the priv RRs later on.
	 * 2. Configure all priv RRs (note that some ranges include, as well, priv PB registers-
	 *    yet it doesn't matter, as those PBs were already configured in par #1).
	 * 3. Configure the remaining priv PBs from par #1.
	 */

	if (hdev->priv_security_enable) {
		fw_secure_cpu(hdev);
		fw_secure_psoc(hdev);
		fw_secure_mme(hdev);
		fw_secure_tpc(hdev);
		fw_secure_pll(hdev, true);

		/* TODO - enable for simulator once SW-93916 & SW-94762 are resolved */
		if (hdev->pdev)
			fw_secure_nic(hdev);

		fw_secure_sram_cfg(hdev);
		fw_secure_sm(hdev);
		fw_secure_dbg(hdev);

		/* priv RRs configurations */
		fw_secure_all_hbw_rr(hdev);
		fw_secure_all_lbw_rr(hdev);

		fw_secure_pci(hdev);

		/* TODO - enable for simulator once SW-93916 & SW-94762 are resolved */
		if (hdev->pdev)
			fw_secure_if(hdev);

		fw_secure_dma(hdev);

		hl_info(hdev, "Privileged security enabled\n");
	}
}

