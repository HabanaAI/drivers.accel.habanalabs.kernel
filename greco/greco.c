// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2019-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "grecoP.h"
#include "greco_masks.h"
#include "../include/greco/greco_special_blocks.h"
#include "../include/greco/greco_fw_if.h"
#include "../include/hw_ip/mmu/mmu_general.h"
#include "../include/hw_ip/mmu/mmu_v1_2.h"
#include "../include/greco/greco_packets.h"
#include "../include/greco/greco_reg_map.h"
#include "../include/greco/greco_async_ids_map_extended.h"

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/hwmon.h>
#include <linux/iommu.h>
#include <linux/seq_file.h>

/*
 * GRECO security scheme:
 * TBD
 */

#define GRECO_BOOT_FIT_FILE	"habanalabs/greco/greco-boot-fit.itb"
#define GRECO_LINUX_FW_FILE	"habanalabs/greco/greco-fit.itb"

MODULE_FIRMWARE(GRECO_BOOT_FIT_FILE);
MODULE_FIRMWARE(GRECO_LINUX_FW_FILE);

#define GRECO_DMA_POOL_BLK_SIZE		0x100		/* 256 bytes */

#define GRECO_RESET_TIMEOUT_MSEC	500		/* 500ms */
#define GRECO_PLDM_HRESET_TIMEOUT_MSEC	20000		/* 20s */
#define GRECO_PLDM_SRESET_TIMEOUT_MSEC	20000		/* 10s */
#define GRECO_RESET_WAIT_MSEC		1		/* 1ms */
#define GRECO_CPU_RESET_WAIT_MSEC	100		/* 100ms */
#define GRECO_PLDM_RESET_WAIT_MSEC	1000		/* 1s */
#define GRECO_PLDM_MMU_TIMEOUT_USEC	(MMU_CONFIG_TIMEOUT_USEC * 100)
#define GRECO_MMU_TIMEOUT_USEC		300000		/* 300ms */
#define GRECO_MSG_TO_CPU_TIMEOUT_USEC	4000000		/* 4s */
#define GRECO_WAIT_FOR_BL_TIMEOUT_USEC	20000000	/* 20s */

#define GRECO_CB_POOL_CB_CNT		512
#define GRECO_CB_POOL_CB_SIZE		0x20000		/* 128KB */

#define GRECO_TEST_QUEUE_WAIT_USEC	100000		/* 100ms */
#define GRECO_PLDM_TEST_QUEUE_WAIT_USEC	1000000		/* 1s */

#define GRECO_ARB_WDT_TIMEOUT		0xEE6b27FF

#define KDMA_TIMEOUT_USEC		USEC_PER_SEC

#define GRECO_VDEC_TIMEOUT_USEC		10000		/* 10ms */
#define GRECO_PLDM_VDEC_TIMEOUT_USEC	(GRECO_VDEC_TIMEOUT_USEC * 100)

#define GRECO_ALLOC_CPU_MEM_RETRY_CNT	6

#define MAX_FAULTY_TPCS			1
#define MAX_FAULTY_DECODERS		1

#define GRECO_MAX_STRING_LEN		32

#define GRECO_NUM_OF_TPC_INTR_CAUSE	25

#define GRECO_NUM_OF_QM_ERR_CAUSE	18

#define GRECO_NUM_OF_QM_ARB_ERR_CAUSE	3

#define GRECO_NUM_OF_DDR_SEI_ERR_CAUSE	16

#define GRECO_NUM_OF_DDR_SPI_ERR_CAUSE	23

#define GRECO_NUM_OF_VDEC_INTR_CAUSE	24

#define GRECO_NUM_OF_DMA_CORE_INTR_CAUSE 4

#define GRECO_NUM_OF_PCIE_ADDR_DEC_ERR_CAUSE	2

#define GRECO_NUM_OF_MMU_SPI_SEI_ERR_CAUSE	18

#define GRECO_NUM_OF_SM_SEI_ERR_CAUSE	3

#define GRECO_VDEC_MSIX_ENTRIES		(GRECO_IRQ_NUM_DCORE1_DEC4_ABNRM - \
					GRECO_IRQ_NUM_DCORE0_DEC0_NRM + 1)

#define MMU_RANGE_INV_VA_LSB_SHIFT	12
#define MMU_RANGE_INV_VA_MSB_SHIFT	44
#define MMU_RANGE_INV_EN_SHIFT		0
#define MMU_RANGE_INV_ASID_EN_SHIFT	1
#define MMU_RANGE_INV_ASID_SHIFT	2

#define GRECO_MMU_SPI_SEI_ENABLE_MASK	GENMASK(GRECO_NUM_OF_MMU_SPI_SEI_ERR_CAUSE - 1, 0)

#define GRECO_NUM_OF_VCD_MEMORIES	92

/* RAZWI initiator coordinates */

#define RAZWI_GET_AXUSER_XY(x) \
	((x & 0xF8000FF0) >> 4)

#define RAZWI_INITIATOR_AXUER_L_X_SHIFT		0
#define RAZWI_INITIATOR_AXUER_L_X_MASK		0xF
#define RAZWI_INITIATOR_AXUER_L_Y_SHIFT		4
#define RAZWI_INITIATOR_AXUER_L_Y_MASK		0xF

#define RAZWI_INITIATOR_AXUER_H_X_SHIFT		23
#define RAZWI_INITIATOR_AXUER_H_X_MASK		0xF
#define RAZWI_INITIATOR_AXUER_H_Y_SHIFT		27
#define RAZWI_INITIATOR_AXUER_H_Y_MASK		0x1

#define RAZWI_INITIATOR_ID_X_Y_LOW(x, y) \
	((((y) & RAZWI_INITIATOR_AXUER_L_Y_MASK) << RAZWI_INITIATOR_AXUER_L_Y_SHIFT) | \
		(((x) & RAZWI_INITIATOR_AXUER_L_X_MASK) << RAZWI_INITIATOR_AXUER_L_X_SHIFT))

#define RAZWI_INITIATOR_ID_X_Y_HIGH(x, y) \
	((((y) & RAZWI_INITIATOR_AXUER_H_Y_MASK) << RAZWI_INITIATOR_AXUER_H_Y_SHIFT) | \
		(((x) & RAZWI_INITIATOR_AXUER_H_X_MASK) << RAZWI_INITIATOR_AXUER_H_X_SHIFT))

#define RAZWI_INITIATOR_ID_X_Y(xl, yl, xh, yh) \
	(RAZWI_INITIATOR_ID_X_Y_LOW(xl, yl) | RAZWI_INITIATOR_ID_X_Y_HIGH(xh, yh))

#define DEC_RAZWI_HBW_AW_HI_ADDR (mmDCORE0_TPCIF_RTR0_CTRL_DEC_RAZWI_HBW_AW_HI_ADDR - \
					mmDCORE0_TPCIF_RTR0_CTRL_BASE)
#define DEC_RAZWI_HBW_AW_LO_ADDR (mmDCORE0_TPCIF_RTR0_CTRL_DEC_RAZWI_HBW_AW_LO_ADDR - \
					mmDCORE0_TPCIF_RTR0_CTRL_BASE)
#define DEC_RAZWI_HBW_AW_SET (mmDCORE0_TPCIF_RTR0_CTRL_DEC_RAZWI_HBW_AW_SET - \
					mmDCORE0_TPCIF_RTR0_CTRL_BASE)
#define DEC_RAZWI_HBW_AR_HI_ADDR (mmDCORE0_TPCIF_RTR0_CTRL_DEC_RAZWI_HBW_AR_HI_ADDR - \
					mmDCORE0_TPCIF_RTR0_CTRL_BASE)
#define DEC_RAZWI_HBW_AR_LO_ADDR (mmDCORE0_TPCIF_RTR0_CTRL_DEC_RAZWI_HBW_AR_LO_ADDR - \
					mmDCORE0_TPCIF_RTR0_CTRL_BASE)
#define DEC_RAZWI_HBW_AR_SET (mmDCORE0_TPCIF_RTR0_CTRL_DEC_RAZWI_HBW_AR_SET - \
					mmDCORE0_TPCIF_RTR0_CTRL_BASE)
#define DEC_RAZWI_LBW_AW_ADDR (mmDCORE0_TPCIF_RTR0_CTRL_DEC_RAZWI_LBW_AW_ADDR - \
					mmDCORE0_TPCIF_RTR0_CTRL_BASE)
#define DEC_RAZWI_LBW_AW_SET (mmDCORE0_TPCIF_RTR0_CTRL_DEC_RAZWI_LBW_AW_SET - \
					mmDCORE0_TPCIF_RTR0_CTRL_BASE)
#define DEC_RAZWI_LBW_AR_ADDR (mmDCORE0_TPCIF_RTR0_CTRL_DEC_RAZWI_LBW_AR_ADDR - \
					mmDCORE0_TPCIF_RTR0_CTRL_BASE)
#define DEC_RAZWI_LBW_AR_SET (mmDCORE0_TPCIF_RTR0_CTRL_DEC_RAZWI_LBW_AR_SET - \
					mmDCORE0_TPCIF_RTR0_CTRL_BASE)

struct greco_razwi_info {
	u32 axuser_xy;
	u32 rtr_ctrl;
	u16 eng_id;
	char *eng_name;
};

static struct greco_razwi_info razwi_info[] = {
		{RAZWI_INITIATOR_ID_X_Y(1, 2, 3, 0), mmDCON0_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_DEC_0, "D0_DEC0"},
		{RAZWI_INITIATOR_ID_X_Y(10, 2, 8, 0), mmDCON1_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_DEC_0, "D1_DEC0"},
		{RAZWI_INITIATOR_ID_X_Y(2, 2, 2, 0), mmDCORE0_TPCIF_RTR0_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_TPC_0, "D0_TPC0"},
		{RAZWI_INITIATOR_ID_X_Y(2, 2, 2, 1), mmDCORE0_TPCIF_RTR0_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_TPC_1, "D0_TPC1"},
		{RAZWI_INITIATOR_ID_X_Y(3, 2, 3, 1), mmDCORE0_TPCIF_RTR1_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_TPC_2, "D0_TPC2"},
		{RAZWI_INITIATOR_ID_X_Y(4, 2, 4, 1), mmDCORE0_TPCIF_RTR2_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_TPC_3, "D0_TPC3"},
		{RAZWI_INITIATOR_ID_X_Y(5, 2, 5, 1), mmDCORE0_TPCIF_RTR3_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_TPC_4, "D0_TPC4"},
		{RAZWI_INITIATOR_ID_X_Y(6, 2, 6, 1), mmDCORE1_TPCIF_RTR0_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_TPC_4, "D1_TPC4"},
		{RAZWI_INITIATOR_ID_X_Y(7, 2, 7, 1), mmDCORE1_TPCIF_RTR1_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_TPC_3, "D1_TPC3"},
		{RAZWI_INITIATOR_ID_X_Y(8, 2, 8, 1), mmDCORE1_TPCIF_RTR2_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_TPC_2, "D1_TPC2"},
		{RAZWI_INITIATOR_ID_X_Y(9, 2, 9, 1), mmDCORE1_TPCIF_RTR3_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_TPC_1, "D1_TPC1"},
		{RAZWI_INITIATOR_ID_X_Y(9, 2, 9, 0), mmDCORE1_TPCIF_RTR3_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_TPC_0, "D1_TPC0"},
		{RAZWI_INITIATOR_ID_X_Y(2, 7, 0, 0), mmDCORE0_MMEIF_RTR0_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_MME, "D0_MME"},
		{RAZWI_INITIATOR_ID_X_Y(3, 7, 0, 0), mmDCORE0_MMEIF_RTR1_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_MME, "D0_MME"},
		{RAZWI_INITIATOR_ID_X_Y(4, 7, 0, 0), mmDCORE0_MMEIF_RTR2_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_MME, "D0_MME"},
		{RAZWI_INITIATOR_ID_X_Y(5, 7, 0, 0), mmDCORE0_MMEIF_RTR3_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_MME, "D0_MME"},
		{RAZWI_INITIATOR_ID_X_Y(6, 7, 0, 0), mmDCORE1_MMEIF_RTR0_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_MME, "D1_MME"},
		{RAZWI_INITIATOR_ID_X_Y(7, 7, 0, 0), mmDCORE1_MMEIF_RTR1_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_MME, "D1_MME"},
		{RAZWI_INITIATOR_ID_X_Y(8, 7, 0, 0), mmDCORE1_MMEIF_RTR2_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_MME, "D1_MME"},
		{RAZWI_INITIATOR_ID_X_Y(9, 7, 0, 0), mmDCORE1_MMEIF_RTR3_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_MME, "D1_MME"},
		{RAZWI_INITIATOR_ID_X_Y(1, 7, 0, 1), mmDCON1_HBW_RTR_IF0_RTR_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_PDMA_0, "D0_PDMA0"},
		{RAZWI_INITIATOR_ID_X_Y(1, 7, 0, 0), mmDCON1_HBW_RTR_IF0_RTR_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_PDMA_1, "D0_PDMA1"},
		{RAZWI_INITIATOR_ID_X_Y(10, 7, 11, 1), mmDCON3_HBW_RTR_IF0_RTR_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_PDMA_0, "D1_PDMA0"},
		{RAZWI_INITIATOR_ID_X_Y(10, 7, 11, 0), mmDCON3_HBW_RTR_IF0_RTR_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_PDMA_1, "D1_PDMA1"},
		{RAZWI_INITIATOR_ID_X_Y(1, 7, 1, 0), mmDCON1_HBW_RTR_IF0_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "KDMA0"}, /* KDMA0 */
		{RAZWI_INITIATOR_ID_X_Y(10, 7, 10, 0), mmDCON3_HBW_RTR_IF0_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "KDMA1"}, /* KDMA1 */
		{RAZWI_INITIATOR_ID_X_Y(1, 2, 0, 1), mmDCON0_HBW_RTR_IF0_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "DDMA0"}, /* DDMA0 */
		{RAZWI_INITIATOR_ID_X_Y(10, 2, 11, 11), mmDCON3_HBW_RTR_IF0_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "DDMA1"}, /* DDMA1 */
		{RAZWI_INITIATOR_ID_X_Y(1, 2, 1, 0), mmDCON0_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "PMMU"}, /* PMMU */
		{RAZWI_INITIATOR_ID_X_Y(1, 2, 0, 0), mmDCON0_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "PCIE"}, /* PCIE_IF */
		{RAZWI_INITIATOR_ID_X_Y(1, 7, 1, 1), mmDCON1_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "SM0"}, /* SM0 */
		{RAZWI_INITIATOR_ID_X_Y(11, 7, 11, 1), mmDCON3_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "SM1"}, /* SM1 */
		{RAZWI_INITIATOR_ID_X_Y(1, 2, 1, 1), mmDCON0_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "HMMU0"}, /* HMMU0 */
		{RAZWI_INITIATOR_ID_X_Y(10, 2, 10, 1), mmDCON2_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "HMMU1"}, /* HMMU1 */
		{RAZWI_INITIATOR_ID_X_Y(1, 7, 1, 1), mmDCON1_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "HMMU2"}, /* HMMU2 */
		{RAZWI_INITIATOR_ID_X_Y(10, 7, 10, 1), mmDCON3_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "HMMU3"}, /* HMMU3 */
		{RAZWI_INITIATOR_ID_X_Y(1, 7, 0, 1), mmDCON1_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_DCORE0_ENGINE_ID_ROT, "D0_ROT"},
		{RAZWI_INITIATOR_ID_X_Y(10, 7, 11, 1), mmDCON3_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_DCORE1_ENGINE_ID_ROT, "D1_ROT"},
		{RAZWI_INITIATOR_ID_X_Y(2, 2, 10, 1), mmDCON2_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "CPU"}, /* CPU */
		{RAZWI_INITIATOR_ID_X_Y(10, 2, 11, 0), mmDCON2_HBW_RTR_IF1_RTR_CTRL_BASE,
				GRECO_ENGINE_ID_SIZE, "PSOC"} /* PSOC */
};

#define IS_QM_IDLE(qm_glbl_sts0, qm_cgm_sts) \
	((((qm_glbl_sts0) & (QM_IDLE_MASK)) == (QM_IDLE_MASK)) && \
			(((qm_cgm_sts) & (CGM_IDLE_MASK)) == (CGM_IDLE_MASK)))

#define IS_DMA_QM_IDLE(qm_glbl_sts0) \
	(((qm_glbl_sts0) & (QM_IDLE_MASK)) == (QM_IDLE_MASK))

#define IS_MME_QM_IDLE(qm_glbl_sts0) \
	(((qm_glbl_sts0) & (QM_IDLE_MASK)) == (QM_IDLE_MASK))

#define IS_DMA_IDLE(dma_core_idle_ind_mask)	\
	(!((dma_core_idle_ind_mask) &		\
		((DCORE0_DDMA_CORE_IDLE_IND_MASK_DESC_CNT_STS_MASK) | \
		(DCORE0_DDMA_CORE_IDLE_IND_MASK_COMP_MASK))))

#define IS_TPC_IDLE(tpc_cfg_sts, tsb_occupancy, tsb_inflight_cntr) \
	((((tpc_cfg_sts) & (TPC_IDLE_MASK)) == (TPC_IDLE_MASK)) && \
			((tsb_occupancy) == 0) && ((tsb_inflight_cntr) == 0))

#define IS_MME_IDLE(mme_arch_sts) \
	(((mme_arch_sts) & MME_ARCH_IDLE_MASK) == MME_ARCH_IDLE_MASK)

#define DEC_WORK_STATE_IDLE		0
#define DEC_WORK_STATE_PEND		3
#define IS_DEC_IDLE(dec_swreg15) \
	(((dec_swreg15) & DCORE0_DEC0_CMD_SWREG15_SW_WORK_STATE_MASK) == \
							DEC_WORK_STATE_IDLE || \
	((dec_swreg15) & DCORE0_DEC0_CMD_SWREG15_SW_WORK_STATE_MASK) ==  \
							DEC_WORK_STATE_PEND)

#define IS_ENC_IDLE(enc_swreg1) \
		(((enc_swreg1) & (DCORE0_DEC0_VSI_SWREG1_SW_DEC_E_MASK)) == 0)

static struct hl_special_block_info greco_special_blocks[] = GRECO_SPECIAL_BLOCKS;

/* Special blocks ranges that should be skipped.
 * The first 2 entries are placeholders for DCORE{0,1}_VDEC4, and they will be included depending on
 * the decoder binning info.
 * The next entry is a placeholder for DCORE1_MME_QM, and it will be included according to whether
 * or not DCORE1_MME works in slave mode.
 * The following entries are for DCORE1_TPC4, and it is assumed that it always binned-out.
 */
#define GRECO_SPECIAL_BLOCKS_SKIP_RANGES_D0_DEC4_IDX	0
#define GRECO_SPECIAL_BLOCKS_SKIP_RANGES_D1_DEC4_IDX	1
#define GRECO_SPECIAL_BLOCKS_SKIP_RANGES_D1_MME_QM_IDX	2

static struct range greco_special_blocks_skip_ranges[] = {
	{mmDCORE0_VDEC4_BRDG_CTRL_BASE, mmDCORE0_DEC4_CMD_BASE},
	{mmDCORE1_VDEC4_BRDG_CTRL_BASE, mmDCORE1_DEC4_CMD_BASE},
	{mmDCORE1_MME_QM_BASE, mmDCORE1_MME_QM_BASE},
	{mmDCORE1_TPC4_QM_BASE, mmDCORE1_TPC4_QM_BASE},
	{mmDCORE1_TPC4_CFG_BASE, mmDCORE1_TPC4_PRTN_BASE},
	{mmDCORE1_TPC4_ROM_TABLE_BASE, mmDCORE1_TPC4_EML_BUSMON_3_BASE},
	{mmDCORE1_TPC4_EML_CFG_BASE, mmDCORE1_TPC4_EML_TPC_QM_BASE},
	{mmDCORE1_TPC4_EML_CS_BASE, mmDCORE1_TPC4_EML_CS_BASE}
};

static const int greco_qman_async_event_id[] = {
	[GRECO_QUEUE_ID_DCORE0_PDMA_0_0] = GRECO_EVENT_PDMA0_QM,
	[GRECO_QUEUE_ID_DCORE0_PDMA_0_1] = GRECO_EVENT_PDMA0_QM,
	[GRECO_QUEUE_ID_DCORE0_PDMA_0_2] = GRECO_EVENT_PDMA0_QM,
	[GRECO_QUEUE_ID_DCORE0_PDMA_0_3] = GRECO_EVENT_PDMA0_QM,
	[GRECO_QUEUE_ID_DCORE0_PDMA_1_0] = GRECO_EVENT_PDMA1_QM,
	[GRECO_QUEUE_ID_DCORE0_PDMA_1_1] = GRECO_EVENT_PDMA1_QM,
	[GRECO_QUEUE_ID_DCORE0_PDMA_1_2] = GRECO_EVENT_PDMA1_QM,
	[GRECO_QUEUE_ID_DCORE0_PDMA_1_3] = GRECO_EVENT_PDMA1_QM,
	[GRECO_QUEUE_ID_DCORE0_DDMA_0_0] = GRECO_EVENT_DDMA0_QM,
	[GRECO_QUEUE_ID_DCORE0_DDMA_0_1] = GRECO_EVENT_DDMA0_QM,
	[GRECO_QUEUE_ID_DCORE0_DDMA_0_2] = GRECO_EVENT_DDMA0_QM,
	[GRECO_QUEUE_ID_DCORE0_DDMA_0_3] = GRECO_EVENT_DDMA0_QM,
	[GRECO_QUEUE_ID_DCORE0_MME_0_0] = GRECO_EVENT_MME0_QM,
	[GRECO_QUEUE_ID_DCORE0_MME_0_1] = GRECO_EVENT_MME0_QM,
	[GRECO_QUEUE_ID_DCORE0_MME_0_2] = GRECO_EVENT_MME0_QM,
	[GRECO_QUEUE_ID_DCORE0_MME_0_3] = GRECO_EVENT_MME0_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_0_0] = GRECO_EVENT_TPC0_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_0_1] = GRECO_EVENT_TPC0_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_0_2] = GRECO_EVENT_TPC0_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_0_3] = GRECO_EVENT_TPC0_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_1_0] = GRECO_EVENT_TPC1_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_1_1] = GRECO_EVENT_TPC1_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_1_2] = GRECO_EVENT_TPC1_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_1_3] = GRECO_EVENT_TPC1_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_2_0] = GRECO_EVENT_TPC2_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_2_1] = GRECO_EVENT_TPC2_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_2_2] = GRECO_EVENT_TPC2_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_2_3] = GRECO_EVENT_TPC2_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_3_0] = GRECO_EVENT_TPC3_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_3_1] = GRECO_EVENT_TPC3_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_3_2] = GRECO_EVENT_TPC3_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_3_3] = GRECO_EVENT_TPC3_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_4_0] = GRECO_EVENT_TPC4_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_4_1] = GRECO_EVENT_TPC4_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_4_2] = GRECO_EVENT_TPC4_QM,
	[GRECO_QUEUE_ID_DCORE0_TPC_4_3] = GRECO_EVENT_TPC4_QM,
	[GRECO_QUEUE_ID_DCORE0_ROT_0_0] = GRECO_EVENT_ROTATOR0_ROT0_QM,
	[GRECO_QUEUE_ID_DCORE0_ROT_0_1] = GRECO_EVENT_ROTATOR0_ROT0_QM,
	[GRECO_QUEUE_ID_DCORE0_ROT_0_2] = GRECO_EVENT_ROTATOR0_ROT0_QM,
	[GRECO_QUEUE_ID_DCORE0_ROT_0_3] = GRECO_EVENT_ROTATOR0_ROT0_QM,
	[GRECO_QUEUE_ID_DCORE1_PDMA_0_0] = GRECO_EVENT_PDMA2_QM,
	[GRECO_QUEUE_ID_DCORE1_PDMA_0_1] = GRECO_EVENT_PDMA2_QM,
	[GRECO_QUEUE_ID_DCORE1_PDMA_0_2] = GRECO_EVENT_PDMA2_QM,
	[GRECO_QUEUE_ID_DCORE1_PDMA_0_3] = GRECO_EVENT_PDMA2_QM,
	[GRECO_QUEUE_ID_DCORE1_PDMA_1_0] = GRECO_EVENT_PDMA3_QM,
	[GRECO_QUEUE_ID_DCORE1_PDMA_1_1] = GRECO_EVENT_PDMA3_QM,
	[GRECO_QUEUE_ID_DCORE1_PDMA_1_2] = GRECO_EVENT_PDMA3_QM,
	[GRECO_QUEUE_ID_DCORE1_PDMA_1_3] = GRECO_EVENT_PDMA3_QM,
	[GRECO_QUEUE_ID_DCORE1_DDMA_0_0] = GRECO_EVENT_DDMA1_QM,
	[GRECO_QUEUE_ID_DCORE1_DDMA_0_1] = GRECO_EVENT_DDMA1_QM,
	[GRECO_QUEUE_ID_DCORE1_DDMA_0_2] = GRECO_EVENT_DDMA1_QM,
	[GRECO_QUEUE_ID_DCORE1_DDMA_0_3] = GRECO_EVENT_DDMA1_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_0_0] = GRECO_EVENT_TPC5_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_0_1] = GRECO_EVENT_TPC5_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_0_2] = GRECO_EVENT_TPC5_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_0_3] = GRECO_EVENT_TPC5_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_1_0] = GRECO_EVENT_TPC6_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_1_1] = GRECO_EVENT_TPC6_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_1_2] = GRECO_EVENT_TPC6_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_1_3] = GRECO_EVENT_TPC6_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_2_0] = GRECO_EVENT_TPC7_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_2_1] = GRECO_EVENT_TPC7_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_2_2] = GRECO_EVENT_TPC7_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_2_3] = GRECO_EVENT_TPC7_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_3_0] = GRECO_EVENT_TPC8_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_3_1] = GRECO_EVENT_TPC8_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_3_2] = GRECO_EVENT_TPC8_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_3_3] = GRECO_EVENT_TPC8_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_4_0] = GRECO_EVENT_TPC9_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_4_1] = GRECO_EVENT_TPC9_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_4_2] = GRECO_EVENT_TPC9_QM,
	[GRECO_QUEUE_ID_DCORE1_TPC_4_3] = GRECO_EVENT_TPC9_QM,
	[GRECO_QUEUE_ID_DCORE1_ROT_0_0] = GRECO_EVENT_ROTATOR1_ROT1_QM,
	[GRECO_QUEUE_ID_DCORE1_ROT_0_1] = GRECO_EVENT_ROTATOR1_ROT1_QM,
	[GRECO_QUEUE_ID_DCORE1_ROT_0_2] = GRECO_EVENT_ROTATOR1_ROT1_QM,
	[GRECO_QUEUE_ID_DCORE1_ROT_0_3] = GRECO_EVENT_ROTATOR1_ROT1_QM,
};

static const int greco_dma_core_async_event_id[] = {
	[DMA_CORE_ID_DCORE0_PDMA0] = GRECO_EVENT_PDMA0_CORE,
	[DMA_CORE_ID_DCORE0_PDMA1] = GRECO_EVENT_PDMA1_CORE,
	[DMA_CORE_ID_DCORE0_DDMA] = GRECO_EVENT_DDMA0_CORE,
	[DMA_CORE_ID_DCORE0_KDMA] = GRECO_EVENT_KDMA0_CORE,
	[DMA_CORE_ID_DCORE1_PDMA0] = GRECO_EVENT_PDMA2_CORE,
	[DMA_CORE_ID_DCORE1_PDMA1] = GRECO_EVENT_PDMA3_CORE,
	[DMA_CORE_ID_DCORE1_DDMA] = GRECO_EVENT_DDMA1_CORE,
	[DMA_CORE_ID_DCORE1_KDMA] = GRECO_EVENT_KDMA1_CORE,
};

static u32 greco_stream_master[GRECO_STREAM_MASTER_ARR_SIZE] = {
	GRECO_QUEUE_ID_DCORE0_PDMA_0_0,
	GRECO_QUEUE_ID_DCORE0_PDMA_0_3,
	GRECO_QUEUE_ID_DCORE1_PDMA_0_0,
	GRECO_QUEUE_ID_DCORE0_TPC_0_0,
};

static const char * const
greco_dma_core_interrupts_cause[GRECO_NUM_OF_DMA_CORE_INTR_CAUSE] = {
		"HBW Read returned with error BRESP",
		"HBW write returned with error BRESP",
		"LBW write returned with error BRESP",
		"descriptor_fifo_overflow",
};

static const char * const
greco_tpc_interrupts_cause[GRECO_NUM_OF_TPC_INTR_CAUSE] = {
	"tpc_address_exceed_slm",
	"tpc_div_by_0",
	"tpc_spu_mac_overflow",
	"tpc_spu_addsub_overflow",
	"tpc_spu_abs_overflow",
	"tpc_spu_fp_dst_nan_inf",
	"tpc_spu_fp_dst_denorm",
	"tpc_vpu_mac_overflow",
	"tpc_vpu_addsub_overflow",
	"tpc_vpu_abs_overflow",
	"tpc_vpu_fp_dst_nan_inf",
	"tpc_vpu_fp_dst_denorm",
	"tpc_assertions",
	"tpc_illegal_instruction",
	"tpc_pc_wrap_around",
	"tpc_qm_sw_err",
	"tpc_hbw_rresp_err",
	"tpc_hbw_bresp_err",
	"tpc_lbw_rresp_err",
	"tpc_lbw_bresp_err",
	"tpc_prtn_interrupt",
	"st_unlock_already_locked",
	"invalid_lock_access",
	"LD_L protection violation",
	"ST_L protection violation"
};

static const char * const
greco_qman_error_cause[GRECO_NUM_OF_QM_ERR_CAUSE] = {
	"PQ AXI HBW error",
	"CQ AXI HBW error",
	"CP AXI HBW error",
	"CP error due to undefined OPCODE",
	"CP encountered STOP OPCODE",
	"CP AXI LBW error",
	"CP WRREG32 or WRBULK returned error",
	"N/A",
	"FENCE 0 inc over max value and clipped",
	"FENCE 1 inc over max value and clipped",
	"FENCE 2 inc over max value and clipped",
	"FENCE 3 inc over max value and clipped",
	"FENCE 0 dec under min value and clipped",
	"FENCE 1 dec under min value and clipped",
	"FENCE 2 dec under min value and clipped",
	"FENCE 3 dec under min value and clipped",
	"CPDMA-UP one in the air violated",
	"PQC LBW MSG address is HBW"
};

static const char * const
greco_qman_arb_error_cause[GRECO_NUM_OF_QM_ARB_ERR_CAUSE] = {
	"Choice push while full error",
	"Choice Q watchdog error",
	"MSG AXI LBW returned with error"
};

struct greco_mmu_spi_sei_info {
	char cause[32];
	int clear_bit;
};

static const struct greco_mmu_spi_sei_info
greco_mmu_spi_sei[GRECO_NUM_OF_MMU_SPI_SEI_ERR_CAUSE] = {
	{"page fault", 1},		/* INTERRUPT_CLR[1] */
	{"page access", 1},		/* INTERRUPT_CLR[1] */
	{"bypass ddr", 2},		/* INTERRUPT_CLR[2] */
	{"multi hit", 2},		/* INTERRUPT_CLR[2] */
	{"mmu rei0", -1},		/* no clear register bit */
	{"mmu rei1", -1},		/* no clear register bit */
	{"stlb rei0", -1},		/* no clear register bit */
	{"stlb rei1", -1},		/* no clear register bit */
	{"rr privileged write hit", 2},	/* INTERRUPT_CLR[2] */
	{"rr privileged read hit", 2},	/* INTERRUPT_CLR[2] */
	{"rr secure write hit", 2},	/* INTERRUPT_CLR[2] */
	{"rr secure read hit", 2},	/* INTERRUPT_CLR[2] */
	{"bist_fail no use", 2},	/* INTERRUPT_CLR[2] */
	{"bist_fail no use", 2},	/* INTERRUPT_CLR[2] */
	{"bist_fail no use", 2},	/* INTERRUPT_CLR[2] */
	{"bist_fail no use", 2},	/* INTERRUPT_CLR[2] */
	{"slave error", 16},		/* INTERRUPT_CLR[16] */
	{"dec error", 17},		/* INTERRUPT_CLR[17] */
};

struct greco_sm_sei_cause_data {
	const char *cause_name;
	const char *log_name;
	u32 log_mask;
};

static const struct greco_sm_sei_cause_data
greco_sm_sei_cause[GRECO_NUM_OF_SM_SEI_ERR_CAUSE] = {
		{"calculated SO value overflow/underflow", "SOB group ID", 0x7FF},
		{"payload address of monitor is not aligned to 4B", "monitor addr", 0xFFFF},
		{"armed monitor write got BRESP (SLVERR or DECERR)", "AXI id", 0xFFFF},
};

static const char * const
greco_pcie_addr_dec_error_cause[GRECO_NUM_OF_PCIE_ADDR_DEC_ERR_CAUSE] = {
	"AXI Error interrupt",
	"Bad Access interrupt",
};

static const char * const
greco_ddr_sei_error_cause[GRECO_NUM_OF_DDR_SEI_ERR_CAUSE] = {
	"ddr0 spi ecc uncorrected err",
	"ddr0 ecc ap err",
	"ddr0 ecc ap err",
	"ddr0 sei alert err",
	"ddr0 ecc ap err fault",
	"ddr0 sei ecc corrected err fault",
	"ddr0 rd linkecc corrected err fault",
	"ddr0 rd linkecc uncorrected err fault",
	"ddr1 spi ecc uncorrected err",
	"ddr1 ecc ap err",
	"ddr1 sei alert err",
	"ddr1 ecc ap err fault",
	"ddr1 sei ecc corrected err fault",
	"ddr1 sei ecc uncorrected err fault",
	"ddr1 rd linkecc corrected err fault",
	"ddr1 rd linkecc uncorrected err fault",
};

static const char * const
greco_ddr_spi_error_cause[GRECO_NUM_OF_DDR_SPI_ERR_CAUSE] = {
	"ddr0_intr_derate_temp_limit",
	"ddr0_intr_spi_sbr_done",
	"ddr0_intr_spi_ecc_uncorrected_err",
	"ddr0_intr_spi_ecc_corrected_err",
	"ddr0_intr_spi_arpoison",
	"ddr0_intr_spi_awpoison",
	"ddr0_intr_derate_temp_limit_fault_int",
	"ddr0_dfi_error_intr",
	"ddr0_intr_rd_linkecc_uncorr_err",
	"ddr0_intr_rd_linkecc_corr_err",
	"ddr1_intr_derate_temp_limit",
	"ddr1_intr_spi_sbr_done",
	"ddr1_intr_spi_ecc_uncorrected_err",
	"ddr1_intr_spi_ecc_corrected_err",
	"ddr1_intr_spi_arpoison",
	"ddr1_intr_spi_awpoison",
	"ddr1_intr_derate_temp_limit_fault_int",
	"ddr1_dfi_error_intr",
	"ddr1_intr_rd_linkecc_uncorr_err",
	"ddr1_intr_rd_linkecc_corr_err",
	"phy_interrupt_20",
	"phy_interrupt_21",
	"spmu_interrupt_out",
};

static const char * const
greco_vdec_interrupts_cause[GRECO_NUM_OF_VDEC_INTR_CAUSE] = {
		"l2c_rei_serr",
		"l2c_rei_derr",
		"vcd_rei_serr",
		"vcd_rei_derr",
		"msix_vcd_hbw_sei",
		"msix_l2c_hbw_sei",
		"msix_nrm_hbw_sei",
		"msix_abnrm_hbw_sei",
		"msix_vcd_lbw_sei",
		"msix_l2c_lbw_sei",
		"msix_nrm_lbw_sei",
		"msix_abnrm_lbw_sei",
		"apb_vcd_lbw_sei",
		"apb_l2c_lbw_sei",
		"apb_nrm_lbw_sei",
		"apb_abnrm_lbw_sei",
		"dec_sei",
		"dec_apb_sei",
		"trc_apb_sei",
		"arc_lbw_sei",
		"vcd_spi",
		"l2c_spi",
		"nrm_spi",
		"abnrm_spi",
};

static const u32
greco_tpc_initiator_coordinates[DCORE_NUM_OF_TPCS * NUM_OF_DCORES] = {
	RAZWI_INITIATOR_ID_X_Y_LOW(2, 0),
	RAZWI_INITIATOR_ID_X_Y_LOW(2, 1),
	RAZWI_INITIATOR_ID_X_Y_LOW(3, 1),
	RAZWI_INITIATOR_ID_X_Y_LOW(4, 1),
	RAZWI_INITIATOR_ID_X_Y_LOW(5, 1),
	RAZWI_INITIATOR_ID_X_Y_LOW(6, 1),
	RAZWI_INITIATOR_ID_X_Y_LOW(7, 1),
	RAZWI_INITIATOR_ID_X_Y_LOW(8, 1),
	RAZWI_INITIATOR_ID_X_Y_LOW(9, 1),
	RAZWI_INITIATOR_ID_X_Y_LOW(9, 0),
};

static const u32
greco_rr_hbw_tpc_mstr_if[NUM_OF_DCORES][DCORE_NUM_OF_TPCS - 1] = {
	{
		mmDCORE0_TPCIF_RTR0_MSTR_IF_RR_PRVT_HBW_BASE,
		mmDCORE0_TPCIF_RTR1_MSTR_IF_RR_PRVT_HBW_BASE,
		mmDCORE0_TPCIF_RTR2_MSTR_IF_RR_PRVT_HBW_BASE,
		mmDCORE0_TPCIF_RTR3_MSTR_IF_RR_PRVT_HBW_BASE,
	},
	{
		mmDCORE1_TPCIF_RTR0_MSTR_IF_RR_PRVT_HBW_BASE,
		mmDCORE1_TPCIF_RTR1_MSTR_IF_RR_PRVT_HBW_BASE,
		mmDCORE1_TPCIF_RTR2_MSTR_IF_RR_PRVT_HBW_BASE,
		mmDCORE1_TPCIF_RTR3_MSTR_IF_RR_PRVT_HBW_BASE,
	}
};

static const u32
greco_rr_lbw_tpc_mstr_if[NUM_OF_DCORES][DCORE_NUM_OF_TPCS - 1] = {
	{
		mmDCORE0_TPCIF_RTR0_MSTR_IF_RR_PRVT_LBW_BASE,
		mmDCORE0_TPCIF_RTR1_MSTR_IF_RR_PRVT_LBW_BASE,
		mmDCORE0_TPCIF_RTR2_MSTR_IF_RR_PRVT_LBW_BASE,
		mmDCORE0_TPCIF_RTR3_MSTR_IF_RR_PRVT_LBW_BASE,
	},
	{
		mmDCORE1_TPCIF_RTR0_MSTR_IF_RR_PRVT_LBW_BASE,
		mmDCORE1_TPCIF_RTR1_MSTR_IF_RR_PRVT_LBW_BASE,
		mmDCORE1_TPCIF_RTR2_MSTR_IF_RR_PRVT_LBW_BASE,
		mmDCORE1_TPCIF_RTR3_MSTR_IF_RR_PRVT_LBW_BASE,
	}
};

static const u16 greco_packet_sizes[MAX_PACKET_ID] = {
	[PACKET_WREG_32]	= sizeof(struct packet_wreg32),
	[PACKET_WREG_BULK]	= sizeof(struct packet_wreg_bulk),
	[PACKET_MSG_LONG]	= sizeof(struct packet_msg_long),
	[PACKET_MSG_SHORT]	= sizeof(struct packet_msg_short),
	[PACKET_CP_DMA]		= sizeof(struct packet_cp_dma),
	[PACKET_REPEAT]		= sizeof(struct packet_repeat),
	[PACKET_MSG_PROT]	= sizeof(struct packet_msg_prot),
	[PACKET_FENCE]		= sizeof(struct packet_fence),
	[PACKET_LIN_DMA]	= sizeof(struct packet_lin_dma),
	[PACKET_NOP]		= sizeof(struct packet_nop),
	[PACKET_STOP]		= sizeof(struct packet_stop),
	[PACKET_ARB_POINT]	= sizeof(struct packet_arb_point),
	[PACKET_WAIT]		= sizeof(struct packet_wait),
	[PACKET_CB_LIST]	= sizeof(struct packet_cb_list),
	[PACKET_LOAD_AND_EXE]	= sizeof(struct packet_load_and_exe)
};

static inline bool validate_packet_id(enum packet_id id)
{
	switch (id) {
	case PACKET_WREG_32:
	case PACKET_WREG_BULK:
	case PACKET_MSG_LONG:
	case PACKET_MSG_SHORT:
	case PACKET_CP_DMA:
	case PACKET_REPEAT:
	case PACKET_MSG_PROT:
	case PACKET_FENCE:
	case PACKET_LIN_DMA:
	case PACKET_NOP:
	case PACKET_STOP:
	case PACKET_ARB_POINT:
	case PACKET_WAIT:
	case PACKET_CB_LIST:
	case PACKET_LOAD_AND_EXE:
		return true;
	default:
		return false;
	}
}

static const u32 greco_qm_blocks_bases[GRECO_QUEUE_ID_SIZE] = {
	[GRECO_QUEUE_ID_DCORE0_PDMA_0_0] = mmDCORE0_PDMA0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_PDMA_0_1] = mmDCORE0_PDMA0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_PDMA_0_2] = mmDCORE0_PDMA0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_PDMA_0_3] = mmDCORE0_PDMA0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_PDMA_1_0] = mmDCORE0_PDMA1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_PDMA_1_1] = mmDCORE0_PDMA1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_PDMA_1_2] = mmDCORE0_PDMA1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_PDMA_1_3] = mmDCORE0_PDMA1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_DDMA_0_0] = mmDCORE0_DDMA_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_DDMA_0_1] = mmDCORE0_DDMA_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_DDMA_0_2] = mmDCORE0_DDMA_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_DDMA_0_3] = mmDCORE0_DDMA_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_MME_0_0]  = mmDCORE0_MME_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_MME_0_1]  = mmDCORE0_MME_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_MME_0_2]  = mmDCORE0_MME_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_MME_0_3]  = mmDCORE0_MME_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_0_0]  = mmDCORE0_TPC0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_0_1]  = mmDCORE0_TPC0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_0_2]  = mmDCORE0_TPC0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_0_3]  = mmDCORE0_TPC0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_1_0]  = mmDCORE0_TPC1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_1_1]  = mmDCORE0_TPC1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_1_2]  = mmDCORE0_TPC1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_1_3]  = mmDCORE0_TPC1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_2_0]  = mmDCORE0_TPC2_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_2_1]  = mmDCORE0_TPC2_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_2_2]  = mmDCORE0_TPC2_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_2_3]  = mmDCORE0_TPC2_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_3_0]  = mmDCORE0_TPC3_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_3_1]  = mmDCORE0_TPC3_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_3_2]  = mmDCORE0_TPC3_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_3_3]  = mmDCORE0_TPC3_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_4_0]  = mmDCORE0_TPC4_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_4_1]  = mmDCORE0_TPC4_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_4_2]  = mmDCORE0_TPC4_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_TPC_4_3]  = mmDCORE0_TPC4_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_ROT_0_0]  = mmDCORE0_ROT_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_ROT_0_1]  = mmDCORE0_ROT_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_ROT_0_2]  = mmDCORE0_ROT_QM_BASE,
	[GRECO_QUEUE_ID_DCORE0_ROT_0_3]  = mmDCORE0_ROT_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_PDMA_0_0] = mmDCORE1_PDMA0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_PDMA_0_1] = mmDCORE1_PDMA0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_PDMA_0_2] = mmDCORE1_PDMA0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_PDMA_0_3] = mmDCORE1_PDMA0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_PDMA_1_0] = mmDCORE1_PDMA1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_PDMA_1_1] = mmDCORE1_PDMA1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_PDMA_1_2] = mmDCORE1_PDMA1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_PDMA_1_3] = mmDCORE1_PDMA1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_DDMA_0_0] = mmDCORE1_DDMA_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_DDMA_0_1] = mmDCORE1_DDMA_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_DDMA_0_2] = mmDCORE1_DDMA_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_DDMA_0_3] = mmDCORE1_DDMA_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_MME_0_0]  = mmDCORE1_MME_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_MME_0_1]  = mmDCORE1_MME_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_MME_0_2]  = mmDCORE1_MME_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_MME_0_3]  = mmDCORE1_MME_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_0_0]  = mmDCORE1_TPC0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_0_1]  = mmDCORE1_TPC0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_0_2]  = mmDCORE1_TPC0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_0_3]  = mmDCORE1_TPC0_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_1_0]  = mmDCORE1_TPC1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_1_1]  = mmDCORE1_TPC1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_1_2]  = mmDCORE1_TPC1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_1_3]  = mmDCORE1_TPC1_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_2_0]  = mmDCORE1_TPC2_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_2_1]  = mmDCORE1_TPC2_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_2_2]  = mmDCORE1_TPC2_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_2_3]  = mmDCORE1_TPC2_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_3_0]  = mmDCORE1_TPC3_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_3_1]  = mmDCORE1_TPC3_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_3_2]  = mmDCORE1_TPC3_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_3_3]  = mmDCORE1_TPC3_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_4_0]  = mmDCORE1_TPC4_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_4_1]  = mmDCORE1_TPC4_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_4_2]  = mmDCORE1_TPC4_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_TPC_4_3]  = mmDCORE1_TPC4_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_ROT_0_0]  = mmDCORE1_ROT_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_ROT_0_1]  = mmDCORE1_ROT_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_ROT_0_2]  = mmDCORE1_ROT_QM_BASE,
	[GRECO_QUEUE_ID_DCORE1_ROT_0_3]  = mmDCORE1_ROT_QM_BASE
};

static const u32 greco_dma_core_blocks_bases[DMA_CORE_ID_SIZE] = {
	[DMA_CORE_ID_DCORE0_PDMA0] = mmDCORE0_PDMA0_CORE_BASE,
	[DMA_CORE_ID_DCORE0_PDMA1] = mmDCORE0_PDMA1_CORE_BASE,
	[DMA_CORE_ID_DCORE0_DDMA] = mmDCORE0_DDMA_CORE_BASE,
	[DMA_CORE_ID_DCORE0_KDMA] = mmDCORE0_KDMA_CORE_BASE,
	[DMA_CORE_ID_DCORE1_PDMA0] = mmDCORE1_PDMA0_CORE_BASE,
	[DMA_CORE_ID_DCORE1_PDMA1] = mmDCORE1_PDMA1_CORE_BASE,
	[DMA_CORE_ID_DCORE1_DDMA] = mmDCORE1_DDMA_CORE_BASE,
	[DMA_CORE_ID_DCORE1_KDMA] = mmDCORE1_KDMA_CORE_BASE
};

static const u32 greco_tpc_cfg_blocks_bases[TPC_ID_SIZE] = {
	[TPC_ID_DCORE0_TPC0] = mmDCORE0_TPC0_CFG_BASE,
	[TPC_ID_DCORE0_TPC1] = mmDCORE0_TPC1_CFG_BASE,
	[TPC_ID_DCORE0_TPC2] = mmDCORE0_TPC2_CFG_BASE,
	[TPC_ID_DCORE0_TPC3] = mmDCORE0_TPC3_CFG_BASE,
	[TPC_ID_DCORE0_TPC4] = mmDCORE0_TPC4_CFG_BASE,
	[TPC_ID_DCORE1_TPC0] = mmDCORE1_TPC0_CFG_BASE,
	[TPC_ID_DCORE1_TPC1] = mmDCORE1_TPC1_CFG_BASE,
	[TPC_ID_DCORE1_TPC2] = mmDCORE1_TPC2_CFG_BASE,
	[TPC_ID_DCORE1_TPC3] = mmDCORE1_TPC3_CFG_BASE,
	[TPC_ID_DCORE1_TPC4] = mmDCORE1_TPC4_CFG_BASE
};

static const u32 greco_mme_acc_blocks_bases[MME_ID_SIZE] = {
	[MME_ID_DCORE0] = mmDCORE0_MME_ACC_BASE,
	[MME_ID_DCORE1] = mmDCORE1_MME_ACC_BASE,
};

static const u32 greco_mme_ctrl_lo_blocks_bases[MME_ID_SIZE] = {
	[MME_ID_DCORE0] = mmDCORE0_MME_CTRL_LO_BASE,
	[MME_ID_DCORE1] = mmDCORE1_MME_CTRL_LO_BASE,
};

static const u32 greco_rot_blocks_bases[ROTATOR_ID_SIZE] = {
	[ROTATOR_ID_DCORE0] = mmDCORE0_ROT_BASE,
	[ROTATOR_ID_DCORE1] = mmDCORE1_ROT_BASE
};

static const u32 greco_dma_core_wr_hbw_max_awid_val[DMA_CORE_ID_SIZE] = {
	[DMA_CORE_ID_DCORE0_PDMA0] = 0x1FFF,
	[DMA_CORE_ID_DCORE0_PDMA1] = 0x1FFF,
	[DMA_CORE_ID_DCORE0_DDMA] = 0x3FFF,
	[DMA_CORE_ID_DCORE0_KDMA] = 0x1FFF,
	[DMA_CORE_ID_DCORE1_PDMA0] = 0x1FFF,
	[DMA_CORE_ID_DCORE1_PDMA1] = 0x1FFF,
	[DMA_CORE_ID_DCORE1_DDMA] = 0x3FFF,
	[DMA_CORE_ID_DCORE1_KDMA] = 0x1FFF
};

static const char
greco_vdec_irq_name[GRECO_VDEC_MSIX_ENTRIES][GRECO_MAX_STRING_LEN] = {
	"greco vdec 0_0", "greco vdec 0_0 abnormal",
	"greco vdec 0_1", "greco vdec 0_1 abnormal",
	"greco vdec 0_2", "greco vdec 0_2 abnormal",
	"greco vdec 0_3", "greco vdec 0_3 abnormal",
	"greco vdec 0_4", "greco vdec 0_4 abnormal",
	"greco vdec 1_0", "greco vdec 1_0 abnormal",
	"greco vdec 1_1", "greco vdec 1_1 abnormal",
	"greco vdec 1_2", "greco vdec 1_2 abnormal",
	"greco vdec 1_3", "greco vdec 1_3 abnormal",
	"greco vdec 1_4", "greco vdec 1_4 abnormal"
};

static s64 greco_state_dump_specs_props[SP_MAX] = {0};

static int greco_send_job_to_kdma(struct hl_device *hdev, u64 src_addr,
				u64 dst_addr, u32 size, u32 dcore_id,
				bool is_memset);

static int greco_test_kdma_access(struct hl_device *hdev);
static int greco_memset_device_memory(struct hl_device *hdev, u64 addr,
					u64 size, u64 val, int dcore_id);

static bool is_sync_stream_supported(u32 queue_id)
{
	switch (queue_id) {
	case GRECO_QUEUE_ID_DCORE0_PDMA_0_0 ... GRECO_QUEUE_ID_DCORE0_PDMA_0_3:
	case GRECO_QUEUE_ID_DCORE0_TPC_0_0 ... GRECO_QUEUE_ID_DCORE0_TPC_0_3:
	case GRECO_QUEUE_ID_DCORE0_TPC_4_0 ... GRECO_QUEUE_ID_DCORE0_TPC_4_3:
	case GRECO_QUEUE_ID_DCORE1_PDMA_0_0 ... GRECO_QUEUE_ID_DCORE1_PDMA_0_3:
		return true;
	default:
		return false;
	}
}

static void greco_dcore_set_fixed_properties(struct hl_device *hdev,
		int dcore_id, u32 *num_sync_stream_queues)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hw_queue_properties *queue_props;
	u32 i, base;

	base = GRECO_QUEUE_ID_DCORE0_PDMA_0_0 +
			dcore_id * DCORE_QUEUE_ID_OFFSET;

	for (i = base ; i < base + NUMBER_OF_DCORE_HW_QUEUES ; i++) {
		queue_props = &prop->hw_queues_props[i];
		queue_props->type = QUEUE_TYPE_HW;
		queue_props->driver_only = 0;

		if (is_sync_stream_supported(i)) {
			queue_props->supports_sync_stream = 1;
			(*num_sync_stream_queues)++;
		}

		if ((i <= GRECO_QUEUE_ID_DCORE0_PDMA_1_3) ||
			(i >= GRECO_QUEUE_ID_DCORE1_PDMA_0_0 &&
					i <= GRECO_QUEUE_ID_DCORE1_PDMA_1_3))
			queue_props->cb_alloc_flags = hdev->mmu_enable ?
					CB_ALLOC_USER : CB_ALLOC_KERNEL;
		else
			queue_props->cb_alloc_flags = CB_ALLOC_USER;

		if ((dcore_id == 1) &&
		    !(HW_CAP_MME_MASK & HW_CAP_DCORE1_MME0) &&
		    (i >= GRECO_QUEUE_ID_DCORE1_MME_0_0) &&
		    (i <= GRECO_QUEUE_ID_DCORE1_MME_0_3))
			queue_props->slave = 1;
	}
}

void greco_set_meminfo(struct hl_device *hdev, u32 sram_size)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	prop->sram_base_address = SRAM_BASE_ADDR;
	prop->sram_size = sram_size;
	prop->sram_end_address = prop->sram_base_address +
					prop->sram_size;
	prop->sram_user_base_address = prop->sram_base_address +
					SRAM_USER_BASE_OFFSET;

	if (hdev->dram_enable) {
		prop->dram_base_address = DRAM_PHYS_BASE;
		if (hdev->pldm)
			prop->dram_size = GRECO_DRAM_SIZE_48GB;
		else
			prop->dram_size = GRECO_DRAM_SIZE_16GB;
		prop->dram_end_address = prop->dram_base_address +
						prop->dram_size;
		prop->dram_user_base_address = DRAM_BASE_ADDR_USER;

	} else {
		prop->dram_base_address = 0;
		prop->dram_size = 0;
		prop->dram_end_address = 0;
		prop->dram_user_base_address = 0;
	}
}

int greco_set_fixed_properties(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hw_queue_properties *q_props;
	u32 num_sync_stream_queues = 0;
	int i, dcore_id;

	prop->max_queues = GRECO_QUEUE_ID_SIZE;
	prop->hw_queues_props = kcalloc(prop->max_queues,
			sizeof(struct hw_queue_properties),
			GFP_KERNEL);

	if (!prop->hw_queues_props)
		return -ENOMEM;

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++)
		greco_dcore_set_fixed_properties(hdev, dcore_id,
				&num_sync_stream_queues);

	q_props = prop->hw_queues_props;
	q_props[GRECO_QUEUE_ID_CPU_PQ].type = QUEUE_TYPE_CPU;
	q_props[GRECO_QUEUE_ID_CPU_PQ].driver_only = 1;
	q_props[GRECO_QUEUE_ID_CPU_PQ].cb_alloc_flags = CB_ALLOC_KERNEL;

	prop->cfg_base_address = CFG_BASE;
	prop->device_dma_offset_for_host_access = HOST_PHYS_BASE;
	prop->completion_queues_count = 0;
	prop->sync_stream_first_sob = GRECO_NUM_RSRVD_SOBS;
	prop->sync_stream_first_mon = GRECO_NUM_RSRVD_COMPLETION_Q_MONITORS;
	prop->completion_mode = U8_MAX; /* N/A completion mode */

	/*
	 * Set default SRAM size regardless of binning
	 * After binning information is obtained sizes will be updated
	 * We assume that the SRAM base address does not change
	 */
	greco_set_meminfo(hdev, SRAM_SIZE);

	prop->mmu_pgt_addr = MMU_PAGE_TABLES_ADDR;
	if (hdev->pldm)
		prop->mmu_pgt_size = 0x800000; /* 8MB */
	else
		prop->mmu_pgt_size = MMU_PAGE_TABLES_SIZE;
	prop->mmu_pte_size = HL_PTE_SIZE;
	prop->mmu_hop_table_size = HOP_TABLE_SIZE_512_PTE;
	prop->mmu_hop0_tables_total_size = HOP0_512_PTE_TABLES_TOTAL_SIZE;
	prop->dram_page_size = PAGE_SIZE_32MB;
	prop->device_mem_alloc_default_page_size = prop->dram_page_size;
	prop->dram_supports_virtual_memory = true;

	prop->dmmu.start_addr = VA_DRAM_SPACE_DCORE0_START;
	prop->dmmu.end_addr = VA_DRAM_SPACE_DCORE0_END;
	if (hdev->use_8_bit_hops) {
		prop->dmmu.hop_shifts[MMU_HOP0] = DHOP0_8_BIT_SHIFT;
		prop->dmmu.hop_shifts[MMU_HOP1] = DHOP1_8_BIT_SHIFT;
		prop->dmmu.hop_shifts[MMU_HOP2] = DHOP2_8_BIT_SHIFT;
		prop->dmmu.hop_shifts[MMU_HOP3] = DHOP3_8_BIT_SHIFT;
		prop->dmmu.hop_masks[MMU_HOP0] = DHOP0_8_BIT_MASK;
		prop->dmmu.hop_masks[MMU_HOP1] = DHOP1_8_BIT_MASK;
		prop->dmmu.hop_masks[MMU_HOP2] = DHOP2_8_BIT_MASK;
		prop->dmmu.hop_masks[MMU_HOP3] = DHOP3_8_BIT_MASK;
		prop->dmmu.num_hops = MMU_ARCH_5_HOPS;
	} else {
		prop->dmmu.hop_shifts[MMU_HOP0] = DHOP0_SHIFT;
		prop->dmmu.hop_shifts[MMU_HOP1] = DHOP1_SHIFT;
		prop->dmmu.hop_shifts[MMU_HOP2] = DHOP2_SHIFT;
		prop->dmmu.hop_shifts[MMU_HOP3] = DHOP3_SHIFT;
		prop->dmmu.hop_masks[MMU_HOP0] = DHOP0_MASK;
		prop->dmmu.hop_masks[MMU_HOP1] = DHOP1_MASK;
		prop->dmmu.hop_masks[MMU_HOP2] = DHOP2_MASK;
		prop->dmmu.hop_masks[MMU_HOP3] = DHOP3_MASK;
		prop->dmmu.num_hops = MMU_ARCH_5_HOPS;
	}
	prop->dmmu.page_size = PAGE_SIZE_32MB;
	prop->dmmu.last_mask = LAST_MASK;
	/* TODO: will be duplicated until implementing per-MMU props */
	prop->dmmu.hop_table_size = prop->mmu_hop_table_size;
	prop->dmmu.hop0_tables_total_size = prop->mmu_hop0_tables_total_size;

	/* PMMU pgt shall always be host-resident */
	prop->pmmu.host_resident = 1;

	/* DMMU pgt can never be host-resident. They must always be located
	 * on the DRAM
	 */
	prop->dmmu.host_resident = 0;

	if (PAGE_SIZE == SZ_64K) {
		prop->pmmu.hop_shifts[MMU_HOP0] = HOP0_SHIFT_64K;
		prop->pmmu.hop_shifts[MMU_HOP1] = HOP1_SHIFT_64K;
		prop->pmmu.hop_shifts[MMU_HOP2] = HOP2_SHIFT_64K;
		prop->pmmu.hop_shifts[MMU_HOP3] = HOP3_SHIFT_64K;
		prop->pmmu.hop_shifts[MMU_HOP4] = HOP4_SHIFT_64K;
		prop->pmmu.hop_masks[MMU_HOP0] = HOP0_MASK_64K;
		prop->pmmu.hop_masks[MMU_HOP1] = HOP1_MASK_64K;
		prop->pmmu.hop_masks[MMU_HOP2] = HOP2_MASK_64K;
		prop->pmmu.hop_masks[MMU_HOP3] = HOP3_MASK_64K;
		prop->pmmu.hop_masks[MMU_HOP4] = HOP4_MASK_64K;
		prop->pmmu.start_addr = VA_HOST_SPACE_PAGE_START;
		prop->pmmu.end_addr = VA_HOST_SPACE_PAGE_END;
		prop->pmmu.page_size = PAGE_SIZE_64KB;
		prop->pmmu.num_hops = MMU_ARCH_5_HOPS;
		prop->pmmu.last_mask = LAST_MASK;
		/* TODO: will be duplicated until implementing per-MMU props */
		prop->pmmu.hop_table_size = prop->mmu_hop_table_size;
		prop->pmmu.hop0_tables_total_size = prop->mmu_hop0_tables_total_size;

		/* shifts and masks are the same in PMMU and HPMMU */
		memcpy(&prop->pmmu_huge, &prop->pmmu, sizeof(prop->pmmu));
		prop->pmmu_huge.page_size = PAGE_SIZE_16MB;
		prop->pmmu_huge.start_addr = VA_HOST_SPACE_HPAGE_START;
		prop->pmmu_huge.end_addr = VA_HOST_SPACE_HPAGE_END;
	} else {
		prop->pmmu.hop_shifts[MMU_HOP0] = HOP0_SHIFT_4K;
		prop->pmmu.hop_shifts[MMU_HOP1] = HOP1_SHIFT_4K;
		prop->pmmu.hop_shifts[MMU_HOP2] = HOP2_SHIFT_4K;
		prop->pmmu.hop_shifts[MMU_HOP3] = HOP3_SHIFT_4K;
		prop->pmmu.hop_shifts[MMU_HOP4] = HOP4_SHIFT_4K;
		prop->pmmu.hop_masks[MMU_HOP0] = HOP0_MASK_4K;
		prop->pmmu.hop_masks[MMU_HOP1] = HOP1_MASK_4K;
		prop->pmmu.hop_masks[MMU_HOP2] = HOP2_MASK_4K;
		prop->pmmu.hop_masks[MMU_HOP3] = HOP3_MASK_4K;
		prop->pmmu.hop_masks[MMU_HOP4] = HOP4_MASK_4K;
		prop->pmmu.start_addr = VA_HOST_SPACE_PAGE_START;
		prop->pmmu.end_addr = VA_HOST_SPACE_PAGE_END;
		prop->pmmu.page_size = PAGE_SIZE_4KB;
		prop->pmmu.num_hops = MMU_ARCH_5_HOPS;
		prop->pmmu.last_mask = LAST_MASK;
		/* TODO: will be duplicated until implementing per-MMU props */
		prop->pmmu.hop_table_size = prop->mmu_hop_table_size;
		prop->pmmu.hop0_tables_total_size = prop->mmu_hop0_tables_total_size;

		/* shifts and masks are the same in PMMU and HPMMU */
		memcpy(&prop->pmmu_huge, &prop->pmmu, sizeof(prop->pmmu));
		prop->pmmu_huge.page_size = PAGE_SIZE_2MB;
		prop->pmmu_huge.start_addr = VA_HOST_SPACE_HPAGE_START;
		prop->pmmu_huge.end_addr = VA_HOST_SPACE_HPAGE_END;
	}

	hdev->pmmu_huge_range = true;

	prop->cfg_size = CFG_SIZE;
	prop->max_asid = MAX_ASID;
	prop->num_of_events = GRECO_EVENT_SIZE;

	prop->max_power_default = MAX_POWER_DEFAULT;
	prop->dc_power_default = DC_POWER_DEFAULT;

	prop->cb_pool_cb_cnt = GRECO_CB_POOL_CB_CNT;
	prop->cb_pool_cb_size = GRECO_CB_POOL_CB_SIZE;

	prop->pcie_dbi_base_address = CFG_BASE + mmPCIE_DBI_BASE;
	prop->pcie_aux_dbi_reg_addr = CFG_BASE + mmPCIE_AUX_DBI;

	strncpy(prop->cpucp_info.card_name, GRECO_DEFAULT_CARD_NAME,
			CARD_NAME_MAX_LEN);

	prop->mme_master_slave_mode = 1;

	prop->max_pending_cs = GRECO_MAX_PENDING_CS;

	prop->first_available_user_sob[0] =
			GRECO_NUM_RSRVD_SOBS +
			(num_sync_stream_queues * HL_RSVD_SOBS);
	prop->first_available_user_mon[0] =
			GRECO_NUM_RSRVD_COMPLETION_Q_MONITORS +
			(num_sync_stream_queues * HL_RSVD_MONS);

	prop->first_available_user_interrupt = USHRT_MAX;

	for (i = 0 ; i < HL_MAX_DCORES ; i++)
		prop->first_available_cq[i] = USHRT_MAX;

	prop->fw_cpu_boot_dev_sts0_valid = false;
	prop->fw_cpu_boot_dev_sts1_valid = false;
	prop->hard_reset_done_by_fw = false;
	prop->gic_interrupts_enable = true;

	prop->server_type = HL_SERVER_TYPE_UNKNOWN;

	prop->max_dec = NUMBER_OF_DEC;
	prop->user_dec_intr_count = NUMBER_OF_DEC;

	prop->clk_pll_index = HL_GRECO_MME_PLL;
	prop->max_freq_value = GRECO_MAX_CLK_FREQ;

	prop->set_max_power_on_device_init = true;

	prop->dma_mask = 48;
	prop->host_base_address = HOST_PHYS_BASE;
	prop->host_end_address = prop->host_base_address + HOST_PHYS_SIZE;

	return 0;
}

static int greco_pci_bars_map(struct hl_device *hdev)
{
	static const char * const name[] = {"SRAM_CFG", "MSIX", "DRAM"};
	bool is_wc[3] = {false, false, true};
	int rc;

	rc = hl_pci_bars_map(hdev, name, is_wc);
	if (rc)
		return rc;

	hdev->rmmio = hdev->pcie_bar[SRAM_CFG_BAR_ID] +
			(CFG_BASE - SRAM_BASE_ADDR);

	return 0;
}

static u64 greco_set_dram_bar_base(struct hl_device *hdev, u64 addr)
{
	struct greco_device *greco = hdev->asic_specific;
	struct hl_inbound_pci_region pci_region;
	u64 old_addr = addr;
	int rc;

	if ((greco) && (greco->dram_bar_cur_addr == addr))
		return old_addr;

	if (hdev->asic_prop.iatu_done_by_fw)
		return U64_MAX;

	/* Inbound Region 1 - Bar 4 - Point to DRAM */
	pci_region.mode = PCI_BAR_MATCH_MODE;
	pci_region.bar = DRAM_BAR_ID;
	pci_region.addr = addr;
	rc = hl_pci_set_inbound_region(hdev, 1, &pci_region);
	if (rc)
		return U64_MAX;

	if (greco) {
		old_addr = greco->dram_bar_cur_addr;
		greco->dram_bar_cur_addr = addr;
	}

	return old_addr;
}

static int greco_init_iatu(struct hl_device *hdev)
{
	struct hl_inbound_pci_region inbound_region;
	struct hl_outbound_pci_region outbound_region;
	int rc;

	if (hdev->asic_prop.iatu_done_by_fw)
		return 0;

	/* Inbound Region 0 - Bar - - Point to SRAM and CFG */
	inbound_region.mode = PCI_BAR_MATCH_MODE;
	inbound_region.bar = SRAM_CFG_BAR_ID;
	inbound_region.addr = SRAM_BASE_ADDR;
	rc = hl_pci_set_inbound_region(hdev, 0, &inbound_region);
	if (rc)
		goto done;

	/* Inbound Region 1 - Bar 4 - Point to DRAM */
	inbound_region.mode = PCI_BAR_MATCH_MODE;
	inbound_region.bar = DRAM_BAR_ID;
	inbound_region.addr = DRAM_PHYS_BASE;
	rc = hl_pci_set_inbound_region(hdev, 1, &inbound_region);
	if (rc)
		goto done;

	/* Outbound Region 0 - Point to Host */
	outbound_region.addr = HOST_PHYS_BASE;
	outbound_region.size = HOST_PHYS_SIZE;
	rc = hl_pci_set_outbound_region(hdev, &outbound_region);

done:
	return rc;
}

static enum hl_device_hw_state greco_get_hw_state(struct hl_device *hdev)
{
	return RREG32(mmHW_STATE);
}

static int greco_early_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pci_dev *pdev = hdev->pdev;
	resource_size_t pci_bar_size;
	u32 fw_boot_status, val;
	int rc;

	rc = greco_set_fixed_properties(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to get fixed properties\n");
		return rc;
	}

	/* Check BAR sizes */
	pci_bar_size = pci_resource_len(pdev, SRAM_CFG_BAR_ID);

	if (pci_bar_size != CFG_BAR_SIZE) {
		dev_err(hdev->dev, "Not " HL_NAME "? BAR %d size %pa, expecting %llu\n",
			SRAM_CFG_BAR_ID, &pci_bar_size, CFG_BAR_SIZE);
		rc = -ENODEV;
		goto free_queue_props;
	}

	pci_bar_size = pci_resource_len(pdev, MSIX_BAR_ID);

	if (pci_bar_size != MSIX_BAR_SIZE) {
		dev_err(hdev->dev, "Not " HL_NAME "? BAR %d size %pa, expecting %llu\n",
			MSIX_BAR_ID, &pci_bar_size, MSIX_BAR_SIZE);
		rc = -ENODEV;
		goto free_queue_props;
	}

	prop->dram_pci_bar_size = pci_resource_len(pdev, DRAM_BAR_ID);
	hdev->dram_pci_bar_start = pci_resource_start(pdev, DRAM_BAR_ID);

	/* If FW security is enabled at this point it means no access to ELBI
	 * Alternatively, the user is working with an unsecured device but
	 * he wants us to skip the iATU initialization (e.g. ELBI is blocked
	 * inside the VM)
	 */
	if (hdev->asic_prop.fw_security_enabled ||
				hdev->skip_iatu_for_unsecured_device) {
		hdev->asic_prop.iatu_done_by_fw = true;
		goto pci_init;
	}

	rc = hl_pci_elbi_read(hdev, CFG_BASE + mmCPU_BOOT_DEV_STS0,
				&fw_boot_status);
	if (rc)
		goto free_queue_props;

	/* Check whether FW is configuring iATU */
	if ((fw_boot_status & CPU_BOOT_DEV_STS0_ENABLED) &&
			(fw_boot_status & CPU_BOOT_DEV_STS0_FW_IATU_CONF_EN))
		hdev->asic_prop.iatu_done_by_fw = true;

pci_init:
	rc = hl_pci_init(hdev);
	if (rc)
		goto free_queue_props;

	/* Before continuing in the initialization, we need to read the preboot
	 * version to determine whether we run with a security-enabled firmware
	 */
	rc = hl_fw_read_preboot_status(hdev);
	if (rc) {
		if (hdev->reset_on_preboot_fail)
			hdev->asic_funcs->hw_fini(hdev, true, false);
		goto pci_fini;
	}

	/* Allow SOC reset from PCIE.
	 * If Linux image is loaded to the device, then only hard reset is done
	 * by the driver and soft reset will be done by CPU-CP.
	 *
	 * TODO: Modify when hard reset is also done by CPU-CP.
	 */
	val = !(hdev->fw_components & FW_TYPE_LINUX) || hdev->pldm ?
			0 :
			PSOC_GLOBAL_CONF_RST_FROM_PCIE_CTRL_SOFT_RST_MASK_MASK;
	WREG32(mmPSOC_GLOBAL_CONF_RST_FROM_PCIE_CTRL, val);

	if (greco_get_hw_state(hdev) == HL_DEVICE_HW_STATE_DIRTY) {
		dev_dbg(hdev->dev, "H/W state is dirty, must reset before initializing\n");
		hdev->asic_funcs->hw_fini(hdev, true, false);
	}

	return 0;

pci_fini:
	hl_pci_fini(hdev);
free_queue_props:
	kfree(hdev->asic_prop.hw_queues_props);
	return rc;
}

static int greco_early_fini(struct hl_device *hdev)
{
	kfree(hdev->asic_prop.hw_queues_props);
	hl_pci_fini(hdev);

	return 0;
}

int greco_set_binning_masks(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hw_queue_properties *q_props = prop->hw_queues_props;

	/*
	 * check for error condition in which number of binning candidates
	 * is higher than the maximum supported by the driver
	 */
	if (hweight64(hdev->tpc_binning) > MAX_FAULTY_TPCS) {
		dev_err(hdev->dev,
			"TPC binning is supported for max of %u faulty TPCs, provided mask 0x%llx\n",
			MAX_FAULTY_TPCS, hdev->tpc_binning);
		hdev->tpc_binning = 0;
	}

	if (hweight32(hdev->decoder_binning) > MAX_FAULTY_DECODERS) {
		dev_err(hdev->dev,
			"Decoder binning is supported for max of %u faulty decoders, provided mask 0x%x\n",
			MAX_FAULTY_DECODERS, hdev->decoder_binning);
		hdev->decoder_binning = 0;
	}

	prop->tpc_binning_mask = hdev->tpc_binning;
	prop->tpc_enabled_mask = 0x1FF;
	/*
	 * Since we use only 9 TPCs no matter what,
	 * TPC 4 in DCORE1 is always not in use.
	 * DCORE1 TPCs are a mirror of DCORE0 TPCs so DCORE1_TPC4
	 * is actually the LSB in the mask.
	 */
	q_props[GRECO_QUEUE_ID_DCORE1_TPC_4_0].binned = 1;
	q_props[GRECO_QUEUE_ID_DCORE1_TPC_4_1].binned = 1;
	q_props[GRECO_QUEUE_ID_DCORE1_TPC_4_2].binned = 1;
	q_props[GRECO_QUEUE_ID_DCORE1_TPC_4_3].binned = 1;

	if ((hdev->decoder_mask & 0x3FF) != 0x3FF) {
		prop->decoder_binning_mask = 0;
		prop->decoder_enabled_mask = hdev->decoder_mask &
							~hdev->decoder_binning;
	} else {
		prop->decoder_binning_mask = hdev->decoder_binning;
		prop->decoder_enabled_mask = hdev->decoder_mask;
		if (hdev->decoder_binning & 0x1F)
			prop->decoder_enabled_mask &= ~0x10ull;
		if (hdev->decoder_binning & 0x3E0)
			prop->decoder_enabled_mask &= ~0x200ull;
	}

	prop->mme_binning_mask = hdev->mme_binning;
	prop->sram_binning = hdev->sram_binning;

	return 0;
}

static int greco_cpucp_info_get(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 dram_size;
	int rc;

	if (!(greco->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	rc = hl_fw_cpucp_handshake(hdev, mmCPU_BOOT_DEV_STS0,
					mmCPU_BOOT_DEV_STS1, mmCPU_BOOT_ERR0,
					mmCPU_BOOT_ERR1);
	if (rc)
		return rc;

	dram_size = le64_to_cpu(prop->cpucp_info.dram_size);
	if ((dram_size != GRECO_DRAM_SIZE_16GB) && (dram_size != GRECO_DRAM_SIZE_32GB) &&
							(dram_size != GRECO_DRAM_SIZE_64GB)) {
		dev_err(hdev->dev,
			"Failed to initialize because F/W reported invalid DRAM size %llu\n",
			dram_size);
		return -EIO;
	}

	/* Force alignment of all cards to 16GB (SW-111100) */
	dram_size = GRECO_DRAM_SIZE_16GB;

	prop->dram_size = dram_size;
	prop->dram_end_address = prop->dram_base_address + dram_size;

	if (!strlen(prop->cpucp_info.card_name))
		strncpy(prop->cpucp_info.card_name, GRECO_DEFAULT_CARD_NAME,
				CARD_NAME_MAX_LEN);

	/*
	 * Overwrite binning masks only if bfe binning was disabled
	 * and only if there are actual binned engines
	 */
	if (!hdev->tpc_binning && prop->cpucp_info.tpc_binning_mask) {
		hdev->tpc_binning =
			le64_to_cpu(prop->cpucp_info.tpc_binning_mask);
	}

	if (!hdev->decoder_binning && prop->cpucp_info.decoder_binning_mask) {
		hdev->decoder_binning =
			(u32)le64_to_cpu(prop->cpucp_info.decoder_binning_mask);
	}

	if (!hdev->sram_binning && prop->cpucp_info.sram_binning)
		hdev->sram_binning = prop->cpucp_info.sram_binning;

	hdev->mme_binning = le64_to_cpu(prop->cpucp_info.mme_binning_mask);

	return 0;
}

/**
 * greco_fetch_psoc_frequency - Fetch PSOC frequency values
 *
 * @hdev: pointer to hl_device structure
 *
 */
static void greco_fetch_psoc_frequency(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u16 pll_freq_arr[HL_PLL_NUM_OUTPUTS], freq;
	u32 div_fctr, div_sel;
	u32 ctrl_cfg_div;
	u32 fbdiv, postdiv1, postdiv2, refdiv;
	u32 pll_clk = 0;
	const u32 FBDIV_NUM_BITS = 12;
	const u32 FBDIV_SHIFT_VAL = 0;
	const u32 POSTDIV1_NUM_BITS = 3;
	const u32 POSTDIV1_SHIFT_VAL = 16;
	const u32 POSTDIV2_NUM_BITS = 3;
	const u32 POSTDIV2_SHIFT_VAL = 20;
	const u32 REFDIV_NUM_BITS = 6;
	const u32 REFDIV_SHIFT_VAL = 24;
	int rc;

	if (hdev->asic_prop.fw_security_enabled) {
		struct greco_device *greco = hdev->asic_specific;

		if (!(greco->hw_cap_initialized & HW_CAP_CPU_Q))
			return;

		rc = hl_fw_cpucp_pll_info_get(hdev, HL_GRECO_PCI_PLL,
				pll_freq_arr);

		if (rc)
			return;

		freq = pll_freq_arr[3];
	} else {
		div_sel = RREG32(mmPSOC_PCI_PLL_CTRL_DIV_SEL_3);
		div_fctr = RREG32(mmPSOC_PCI_PLL_CTRL_DIV_FACTOR_3);
		ctrl_cfg_div = RREG32(mmPSOC_PCI_PLL_CTRL_CFG_DIV);

		fbdiv = ctrl_cfg_div >> FBDIV_SHIFT_VAL &
				((1<<FBDIV_NUM_BITS) - 1);
		postdiv1 = ctrl_cfg_div >> POSTDIV1_SHIFT_VAL &
						((1<<POSTDIV1_NUM_BITS) - 1);
		postdiv2 = ctrl_cfg_div >> POSTDIV2_SHIFT_VAL &
						((1<<POSTDIV2_NUM_BITS) - 1);
		refdiv = ctrl_cfg_div >> REFDIV_SHIFT_VAL &
				((1<<REFDIV_NUM_BITS) - 1);

		if (div_sel == DIV_SEL_REF_CLK ||
				div_sel == DIV_SEL_DIVIDED_REF) {
			if (div_sel == DIV_SEL_REF_CLK)
				freq = PLL_REF_CLK;
			else
				freq = PLL_REF_CLK / (div_fctr + 1);
		} else if (div_sel == DIV_SEL_PLL_CLK ||
				div_sel == DIV_SEL_DIVIDED_PLL) {
			if (postdiv1 == 0 || postdiv2 == 0 || refdiv == 0) {
				freq = PLL_REF_CLK;
			} else {
				pll_clk = PLL_REF_CLK * fbdiv
						/ postdiv1 / postdiv2 / refdiv;
				if (div_sel == DIV_SEL_PLL_CLK)
					freq = pll_clk;
				else
					freq = pll_clk / (div_fctr + 1);
			}
		} else {
			dev_warn(hdev->dev,
				"Received invalid div select value: %d",
				div_sel);
			freq = 0;
		}
	}

	prop->psoc_timestamp_frequency = freq;
}

int greco_mmu_clear_pgt_range(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	if (!(greco->hw_cap_initialized & HW_CAP_MMU_MASK))
		return 0;

	if (!hdev->dram_enable)
		return 0;

	rc = greco_memset_device_memory(hdev, prop->mmu_pgt_addr,
					prop->mmu_pgt_size, 0, 0);

	return rc;
}

static int greco_internal_cb_pool_init(struct hl_device *hdev, struct hl_ctx *ctx)
{
	struct greco_device *greco = hdev->asic_specific;
	int min_alloc_order, rc;

	if (!(greco->hw_cap_initialized & HW_CAP_PMMU))
		return 0;

	hdev->internal_cb_pool_virt_addr = hl_asic_dma_alloc_coherent(hdev,
								HOST_SPACE_INTERNAL_CB_SZ,
								&hdev->internal_cb_pool_dma_addr,
								GFP_KERNEL | __GFP_ZERO);

	if (!hdev->internal_cb_pool_virt_addr)
		return -ENOMEM;

	min_alloc_order = ilog2(min(greco_get_signal_cb_size(hdev),
					greco_get_wait_cb_size(hdev)));

	hdev->internal_cb_pool = gen_pool_create(min_alloc_order, -1);
	if (!hdev->internal_cb_pool) {
		dev_err(hdev->dev,
			"Failed to create internal CB pool\n");
		rc = -ENOMEM;
		goto free_internal_cb_pool;
	}

	rc = gen_pool_add(hdev->internal_cb_pool,
				(uintptr_t) hdev->internal_cb_pool_virt_addr,
				HOST_SPACE_INTERNAL_CB_SZ, -1);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to add memory to internal CB pool\n");
		rc = -EFAULT;
		goto destroy_internal_cb_pool;
	}

	hdev->internal_cb_va_base = hl_reserve_va_block(hdev, ctx,
			HL_VA_RANGE_TYPE_HOST, HOST_SPACE_INTERNAL_CB_SZ,
			HL_MMU_VA_ALIGNMENT_NOT_NEEDED);

	if (!hdev->internal_cb_va_base) {
		rc = -ENOMEM;
		goto destroy_internal_cb_pool;
	}

	mutex_lock(&hdev->mmu_lock);
	rc = hl_mmu_map_contiguous(ctx, hdev->internal_cb_va_base,
			hdev->internal_cb_pool_dma_addr,
			HOST_SPACE_INTERNAL_CB_SZ);

	hl_mmu_invalidate_cache(hdev, false, MMU_OP_USERPTR);
	mutex_unlock(&hdev->mmu_lock);

	if (rc)
		goto unreserve_internal_cb_pool;

	return 0;

unreserve_internal_cb_pool:
	hl_unreserve_va_block(hdev, ctx, hdev->internal_cb_va_base,
			HOST_SPACE_INTERNAL_CB_SZ);
destroy_internal_cb_pool:
	gen_pool_destroy(hdev->internal_cb_pool);
free_internal_cb_pool:
	hl_asic_dma_free_coherent(hdev, HOST_SPACE_INTERNAL_CB_SZ, hdev->internal_cb_pool_virt_addr,
					hdev->internal_cb_pool_dma_addr);

	return rc;
}

static void greco_internal_cb_pool_fini(struct hl_device *hdev, struct hl_ctx *ctx)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_PMMU))
		return;

	mutex_lock(&hdev->mmu_lock);
	hl_mmu_unmap_contiguous(ctx, hdev->internal_cb_va_base,
			HOST_SPACE_INTERNAL_CB_SZ);
	hl_unreserve_va_block(hdev, ctx, hdev->internal_cb_va_base,
			HOST_SPACE_INTERNAL_CB_SZ);
	hl_mmu_invalidate_cache(hdev, true, MMU_OP_USERPTR);
	mutex_unlock(&hdev->mmu_lock);

	gen_pool_destroy(hdev->internal_cb_pool);

	hl_asic_dma_free_coherent(hdev, HOST_SPACE_INTERNAL_CB_SZ, hdev->internal_cb_pool_virt_addr,
					hdev->internal_cb_pool_dma_addr);
}

int greco_init_reserved_sram(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	/* First reserved bytes in SRAM should be zeroed for Tensor DMA */
	rc = greco_memset_device_memory(hdev, prop->sram_base_address,
					SRAM_USER_BASE_OFFSET, 0, 0);
	if (rc)
		dev_err(hdev->dev, "Failed to init reserved area in SRAM");

	return rc;
}

static int greco_late_init(struct hl_device *hdev)
{
	int rc;

	rc = hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_ENABLE_PCI_ACCESS, 0x0);
	if (rc) {
		dev_err(hdev->dev, "Failed to enable PCI access from CPU\n");
		return rc;
	}

	greco_fetch_psoc_frequency(hdev);

	rc = greco_mmu_clear_pgt_range(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to clear MMU page tables range\n");
		goto disable_pci_access;
	}

	rc = greco_init_reserved_sram(hdev);
	if (rc)
		goto disable_pci_access;

	/*
	 * On palladium, init the KDMA E2E credits here after all KDMA
	 * initialization jobs are done in order to reduce driver loading time.
	 */
	if (hdev->pldm)
		greco_kdma_e2e_init(hdev);

	hl_fw_set_pll_profile(hdev);

	return 0;

disable_pci_access:
	hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_DISABLE_PCI_ACCESS, 0x0);

	return rc;
}

void greco_late_fini(struct hl_device *hdev)
{
	hl_hwmon_release_resources(hdev);
}

int greco_alloc_cpu_accessible_dma_mem(struct hl_device *hdev)
{
	dma_addr_t dma_addr_arr[GRECO_ALLOC_CPU_MEM_RETRY_CNT] = {}, end_addr;
	void *virt_addr_arr[GRECO_ALLOC_CPU_MEM_RETRY_CNT] = {};
	int i, j, rc = 0;

	/* The device ARC works with 32-bits addresses, and because there is a single HW register
	 * that holds the extension bits (49..28), these bits must be identical in all the allocated
	 * range.
	 */

	for (i = 0 ; i < GRECO_ALLOC_CPU_MEM_RETRY_CNT ; i++) {
		virt_addr_arr[i] = hl_asic_dma_alloc_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE,
							&dma_addr_arr[i], GFP_KERNEL | __GFP_ZERO);
		if (!virt_addr_arr[i]) {
			rc = -ENOMEM;
			goto free_dma_mem_arr;
		}

		end_addr = dma_addr_arr[i] + HL_CPU_ACCESSIBLE_MEM_SIZE - 1;
		if (GRECO_ARC_PCI_MSB_ADDR(dma_addr_arr[i]) == GRECO_ARC_PCI_MSB_ADDR(end_addr))
			break;
	}

	if (i == GRECO_ALLOC_CPU_MEM_RETRY_CNT) {
		dev_err(hdev->dev,
			"MSB of ARC accessible DMA memory are not identical in all range\n");
		rc = -EFAULT;
		goto free_dma_mem_arr;
	}

	hdev->cpu_accessible_dma_mem = virt_addr_arr[i];
	hdev->cpu_accessible_dma_address = dma_addr_arr[i];

free_dma_mem_arr:
	for (j = 0 ; j < i ; j++)
		hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, virt_addr_arr[j],
						dma_addr_arr[j]);

	return rc;
}

void greco_set_pci_memory_regions(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pci_mem_region *region;

	/* CFG */
	region = &hdev->pci_mem_region[PCI_REGION_CFG];
	region->region_base = CFG_BASE;
	region->region_size = CFG_SIZE;
	region->offset_in_bar = CFG_BASE - SRAM_BASE_ADDR;
	region->bar_size = CFG_BAR_SIZE;
	region->bar_id = SRAM_CFG_BAR_ID;
	region->used = 1;

	/* SRAM */
	region = &hdev->pci_mem_region[PCI_REGION_SRAM];
	region->region_base = SRAM_BASE_ADDR;
	/*
	 * in this stage we still don't know whether SRAM is binned or not
	 * so taking the worst case
	 * this value will be updated when binning status is known
	 */
	region->region_size = SRAM_SIZE / 2;
	region->offset_in_bar = 0;
	region->bar_size = CFG_BAR_SIZE;
	region->bar_id = SRAM_CFG_BAR_ID;
	region->used = 1;

	/* DRAM */
	region = &hdev->pci_mem_region[PCI_REGION_DRAM];
	region->region_base = DRAM_PHYS_BASE;
	region->region_size = hdev->asic_prop.dram_size;
	region->offset_in_bar = 0;
	region->bar_size = prop->dram_pci_bar_size;
	region->bar_id = DRAM_BAR_ID;
	region->used = 1;

	/* SP SRAM */
	region = &hdev->pci_mem_region[PCI_REGION_SP_SRAM];
	region->region_base = SCRATCHPAD_SRAM_ADDR;
	region->region_size = SCRATCHPAD_SRAM_SIZE;
	region->offset_in_bar = SCRATCHPAD_SRAM_ADDR - SRAM_BASE_ADDR;
	region->bar_size = CFG_BAR_SIZE;
	region->bar_id = SRAM_CFG_BAR_ID;
	region->used = 1;
}

void greco_user_mapped_blocks_init(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	struct user_mapped_block *blocks = greco->mapped_blocks;
	u32 block_id = USR_MAPPED_BLK_DEC_START_IDX;

	HL_USR_MAPPED_BLK_INIT(&blocks[block_id++], mmDCORE0_DEC0_CMD_BASE, HL_BLOCK_SIZE);
	HL_USR_MAPPED_BLK_INIT(&blocks[block_id++], mmDCORE0_DEC1_CMD_BASE, HL_BLOCK_SIZE);
	HL_USR_MAPPED_BLK_INIT(&blocks[block_id++], mmDCORE0_DEC2_CMD_BASE, HL_BLOCK_SIZE);
	HL_USR_MAPPED_BLK_INIT(&blocks[block_id++], mmDCORE0_DEC3_CMD_BASE, HL_BLOCK_SIZE);
	HL_USR_MAPPED_BLK_INIT(&blocks[block_id++], mmDCORE0_DEC4_CMD_BASE, HL_BLOCK_SIZE);
	HL_USR_MAPPED_BLK_INIT(&blocks[block_id++], mmDCORE1_DEC0_CMD_BASE, HL_BLOCK_SIZE);
	HL_USR_MAPPED_BLK_INIT(&blocks[block_id++], mmDCORE1_DEC1_CMD_BASE, HL_BLOCK_SIZE);
	HL_USR_MAPPED_BLK_INIT(&blocks[block_id++], mmDCORE1_DEC2_CMD_BASE, HL_BLOCK_SIZE);
	HL_USR_MAPPED_BLK_INIT(&blocks[block_id++], mmDCORE1_DEC3_CMD_BASE, HL_BLOCK_SIZE);
	HL_USR_MAPPED_BLK_INIT(&blocks[block_id], mmDCORE1_DEC4_CMD_BASE, HL_BLOCK_SIZE);
}

void greco_user_interrupt_setup(struct hl_device *hdev)
{
	int i, j;

	/* Initialize common decoder interrupt */
	HL_USR_INTR_STRUCT_INIT(hdev->common_decoder_interrupt, hdev,
				HL_COMMON_DEC_INTERRUPT_ID, HL_USR_INTERRUPT_DECODER);

	/* Initialize decoder interrupts, expose only normal interrupts,
	 * error interrupts to be handled by driver
	 */
	for (i = GRECO_IRQ_NUM_DCORE0_DEC0_NRM, j = 0 ; i <= GRECO_IRQ_NUM_DCORE1_DEC4_NRM;
										i += 2, j++)
		HL_USR_INTR_STRUCT_INIT(hdev->user_interrupt[j], hdev, i,
						HL_USR_INTERRUPT_DECODER);
}

static int greco_special_blocks_config(struct hl_device *hdev)
{
	int skip_block_types[] = {GRECO_BLOCK_TYPE_PLL, GRECO_BLOCK_TYPE_HMMU};
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int i, rc;

	prop->num_of_special_blocks = ARRAY_SIZE(greco_special_blocks);
	prop->special_blocks =
		kmalloc_array(prop->num_of_special_blocks,
				sizeof(*prop->special_blocks), GFP_KERNEL);
	if (!prop->special_blocks)
		return -ENOMEM;

	for (i = 0 ; i < prop->num_of_special_blocks ; i++)
		memcpy(&prop->special_blocks[i], &greco_special_blocks[i],
						sizeof(*prop->special_blocks));

	prop->glbl_err_cause_num = GRECO_NUM_OF_GLBL_ERR_CAUSE;

	/* set skip configs */
	memset(&prop->skip_special_blocks_cfg, 0, sizeof(prop->skip_special_blocks_cfg));

	if (ARRAY_SIZE(skip_block_types)) {
		prop->skip_special_blocks_cfg.block_types =
				kmalloc_array(ARRAY_SIZE(skip_block_types),
					sizeof(skip_block_types[0]), GFP_KERNEL);
		if (!prop->skip_special_blocks_cfg.block_types) {
			rc = -ENOMEM;
			goto free_special_blocks;
		}

		memcpy(prop->skip_special_blocks_cfg.block_types, skip_block_types,
						sizeof(skip_block_types));
		prop->skip_special_blocks_cfg.block_types_len = ARRAY_SIZE(skip_block_types);
	}

	if (ARRAY_SIZE(greco_special_blocks_skip_ranges)) {
		prop->skip_special_blocks_cfg.block_ranges =
				kmalloc_array(ARRAY_SIZE(greco_special_blocks_skip_ranges),
					sizeof(greco_special_blocks_skip_ranges[0]), GFP_KERNEL);
		if (!prop->skip_special_blocks_cfg.block_ranges) {
			rc = -ENOMEM;
			goto free_types;
		}

		for (i = 0 ; i < ARRAY_SIZE(greco_special_blocks_skip_ranges) ; i++)
			memcpy(&prop->skip_special_blocks_cfg.block_ranges[i],
					&greco_special_blocks_skip_ranges[i], sizeof(struct range));
		prop->skip_special_blocks_cfg.block_ranges_len =
				ARRAY_SIZE(greco_special_blocks_skip_ranges);
	}

	return 0;

free_types:
	kfree(prop->skip_special_blocks_cfg.block_types);
free_special_blocks:
	kfree(prop->special_blocks);

	return rc;
}

static void greco_special_blocks_update_skipped_ranges(struct hl_device *hdev)
{
	struct range *block_ranges = hdev->asic_prop.skip_special_blocks_cfg.block_ranges, *range;
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	/* Remove DCORE0_VDEC4 from skipped ranges if not binned-out */
	if (prop->decoder_enabled_mask & 0x10) {
		range = &block_ranges[GRECO_SPECIAL_BLOCKS_SKIP_RANGES_D0_DEC4_IDX];
		range->start = range->end = 0x0;
	}

	/* Remove DCORE1_VDEC4 from skipped ranges if not binned-out */
	if (prop->decoder_enabled_mask & 0x200) {
		range = &block_ranges[GRECO_SPECIAL_BLOCKS_SKIP_RANGES_D1_DEC4_IDX];
		range->start = range->end = 0x0;
	}

	/* Remove DCORE1_MME_QM from skipped ranges if DCORE1_MME doesn't work in slave mode */
	if (!prop->hw_queues_props[GRECO_QUEUE_ID_DCORE1_MME_0_0].slave) {
		range = &block_ranges[GRECO_SPECIAL_BLOCKS_SKIP_RANGES_D1_MME_QM_IDX];
		range->start = range->end = 0x0;
	}

	/* Assuming that DCORE1_TPC4 is always binned-out, so not updating next ranges entries */
}

static void greco_special_blocks_free(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	kfree(prop->special_blocks);
	kfree(prop->skip_special_blocks_cfg.block_types);
	kfree(prop->skip_special_blocks_cfg.block_ranges);
}

static int greco_sw_init(struct hl_device *hdev)
{
	struct greco_device *greco;
	int rc, i;

	/* Allocate device structure */
	greco = kzalloc(sizeof(*greco), GFP_KERNEL);
	if (!greco)
		return -ENOMEM;

	for (i = 0 ; i < ARRAY_SIZE(greco_irq_map_table) ; i++) {
		if (greco_irq_map_table[i].valid) {
			if (greco->num_of_valid_events == GRECO_EVENT_SIZE) {
				dev_err(hdev->dev,
					"Event array exceeds the limit of %u events\n",
					GRECO_EVENT_SIZE);
				rc = -EINVAL;
				goto free_greco_device;
			}

			greco->events[greco->num_of_valid_events++] =
						greco_irq_map_table[i].fc_id;
		}
	}

	hdev->asic_specific = greco;

	/* Create DMA pool for small allocations */
	hdev->dma_pool = dma_pool_create(dev_name(hdev->dev),
			&hdev->pdev->dev, GRECO_DMA_POOL_BLK_SIZE, 8, 0);
	if (!hdev->dma_pool) {
		dev_err(hdev->dev, "failed to create DMA pool\n");
		rc = -ENOMEM;
		goto free_greco_device;
	}

	rc = greco_alloc_cpu_accessible_dma_mem(hdev);
	if (rc)
		goto free_dma_pool;

	hdev->cpu_accessible_dma_pool = gen_pool_create(ilog2(32), -1);
	if (!hdev->cpu_accessible_dma_pool) {
		dev_err(hdev->dev,
			"Failed to create CPU accessible DMA pool\n");
		rc = -ENOMEM;
		goto free_cpu_dma_mem;
	}

	rc = gen_pool_add(hdev->cpu_accessible_dma_pool,
				(uintptr_t) hdev->cpu_accessible_dma_mem,
				HL_CPU_ACCESSIBLE_MEM_SIZE, -1);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to add memory to CPU accessible DMA pool\n");
		rc = -EFAULT;
		goto free_cpu_accessible_dma_pool;
	}

	spin_lock_init(&greco->hw_queues_lock);

	greco->scratchpad_kernel_address = hl_asic_dma_alloc_coherent(hdev, PAGE_SIZE,
								&greco->scratchpad_bus_address,
								GFP_KERNEL | __GFP_ZERO);

	if (!greco->scratchpad_kernel_address) {
		rc = -ENOMEM;
		goto free_cpu_accessible_dma_pool;
	}

	greco_user_interrupt_setup(hdev);

	greco_user_mapped_blocks_init(hdev);

	rc = greco_special_blocks_config(hdev);
	if (rc)
		goto free_scratchpad_kernel_address;

	greco->cpucp_info_get = greco_cpucp_info_get;

	hdev->supports_coresight = true;
	hdev->asic_prop.supports_compute_reset = true;
	hdev->asic_prop.allow_inference_soft_reset = false;
	hdev->supports_sync_stream = true;
	hdev->supports_cb_mapping = true;
	hdev->supports_wait_for_multi_cs = true;
	hdev->supports_custom_fw_binning = true;
	hdev->asic_funcs->set_pci_memory_regions(hdev);
	hdev->stream_master_qid_arr =
				hdev->asic_funcs->get_stream_master_qid_arr();
	hdev->stream_master_qid_arr_size = GRECO_STREAM_MASTER_ARR_SIZE;

	return 0;

free_scratchpad_kernel_address:
	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE,
					greco->scratchpad_kernel_address,
					greco->scratchpad_bus_address);
free_cpu_accessible_dma_pool:
	gen_pool_destroy(hdev->cpu_accessible_dma_pool);
free_cpu_dma_mem:
	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, hdev->cpu_accessible_dma_mem,
					hdev->cpu_accessible_dma_address);
free_dma_pool:
	dma_pool_destroy(hdev->dma_pool);
free_greco_device:
	kfree(greco);
	return rc;
}

static int greco_sw_fini(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	greco_special_blocks_free(hdev);

	hl_asic_dma_free_coherent(hdev, PAGE_SIZE, greco->scratchpad_kernel_address,
					greco->scratchpad_bus_address);

	gen_pool_destroy(hdev->cpu_accessible_dma_pool);

	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, hdev->cpu_accessible_dma_mem,
					hdev->cpu_accessible_dma_address);

	dma_pool_destroy(hdev->dma_pool);

	kfree(greco);

	return 0;
}

static const char *greco_irq_name(u8 irq_number)
{
	switch (irq_number) {
	case GRECO_IRQ_NUM_EVENT_QUEUE:
		return "greco cpu eq";
	case GRECO_IRQ_NUM_DCORE0_DEC0_NRM ... GRECO_IRQ_NUM_DCORE1_DEC4_ABNRM:
		return greco_vdec_irq_name[irq_number -
			GRECO_IRQ_NUM_DCORE0_DEC0_NRM];
	case GRECO_IRQ_NUM_CS_FIRST ... GRECO_IRQ_NUM_CS_LAST:
		return "greco cs";
	default:
		return "unknown";
	}
}

static void greco_dec_disable_msix(struct hl_device *hdev, u32 max_irq_num)
{
	struct hl_dec *dec;
	int i, irq, relative_idx;

	for (i = GRECO_IRQ_NUM_DCORE0_DEC0_NRM; i < max_irq_num; i++) {
		irq = pci_irq_vector(hdev->pdev, i);
		relative_idx = i - GRECO_IRQ_NUM_DCORE0_DEC0_NRM;
		dec = hdev->dec + relative_idx / 2;

		/* We pass different structures depending on the irq handler. For the abnormal
		 * interrupt we pass hl_dec and for the regular interrupt we pass the relevant
		 * user_interrupt entry
		 */
		free_irq(irq, ((relative_idx % 2) ?
				(void *) dec :
				(void *) &hdev->user_interrupt[dec->core_id]));
	}
}

static int greco_dec_enable_msix(struct hl_device *hdev)
{
	int rc, i, irq_init_cnt, irq, relative_idx;
	struct hl_dec *dec;

	for (i = GRECO_IRQ_NUM_DCORE0_DEC0_NRM, irq_init_cnt = 0;
			i <= GRECO_IRQ_NUM_DCORE1_DEC4_ABNRM;
			i++, irq_init_cnt++) {
		irq = pci_irq_vector(hdev->pdev, i);
		relative_idx = i - GRECO_IRQ_NUM_DCORE0_DEC0_NRM;

		/* We pass different structures depending on the irq handler. For the abnormal
		 * interrupt we pass hl_dec and for the regular interrupt we pass the relevant
		 * user_interrupt entry
		 *
		 * TODO: change the dec_abnrm to threaded irq
		 */

		dec = hdev->dec + relative_idx / 2;
		if (relative_idx % 2) {
			rc = request_irq(irq, hl_irq_handler_dec_abnrm, 0,
					greco_irq_name(i), (void *) dec);
		} else {
			rc = request_threaded_irq(irq, hl_irq_handler_user_interrupt,
					hl_irq_user_interrupt_thread_handler, IRQF_ONESHOT,
					greco_irq_name(i),
					(void *) &hdev->user_interrupt[dec->core_id]);
		}

		if (rc) {
			dev_err(hdev->dev, "Failed to request IRQ %d", irq);
			goto free_vdec_irqs;
		}
	}

	return 0;

free_vdec_irqs:
	greco_dec_disable_msix(hdev, (GRECO_IRQ_NUM_DCORE0_DEC0_NRM + irq_init_cnt));
	return rc;
}

static int greco_enable_msix(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	struct hl_cs_irq_info *cs_irq_info;
	int rc, i, cs_irq_init_cnt, irq, relative_idx;

	if (greco->hw_cap_initialized & HW_CAP_MSIX)
		return 0;

	rc = pci_alloc_irq_vectors(hdev->pdev, MSIX_ENTRIES, MSIX_ENTRIES,
					PCI_IRQ_MSIX);
	if (rc < 0) {
		dev_err(hdev->dev,
			"MSI-X: Failed to enable support -- %d/%d\n",
			MSIX_ENTRIES, rc);
		return rc;
	}

	for (i = GRECO_IRQ_NUM_CS_FIRST, cs_irq_init_cnt = 0;
			i <= GRECO_IRQ_NUM_CS_LAST ; i++, cs_irq_init_cnt++) {
		irq = pci_irq_vector(hdev->pdev, i);
		relative_idx = i - GRECO_IRQ_NUM_CS_FIRST;
		cs_irq_info = &greco->cs_irq_info_arr[relative_idx];
		cs_irq_info->hdev = hdev;
		cs_irq_info->relative_idx = relative_idx;
		rc = request_irq(irq, hl_irq_handler_cs_cmplt, 0,
					greco_irq_name(i), cs_irq_info);
		if (rc) {
			dev_err(hdev->dev, "Failed to request IRQ %d", irq);
			goto free_cs_irqs;
		}
	}

	rc = greco_dec_enable_msix(hdev);

	if (rc) {
		dev_err(hdev->dev, "Failed to enable decoder IRQ");
		goto free_cs_irqs;
	}

	irq = pci_irq_vector(hdev->pdev, GRECO_IRQ_NUM_EVENT_QUEUE);

	rc = request_irq(irq, hl_irq_handler_eq, 0,
			greco_irq_name(GRECO_IRQ_NUM_EVENT_QUEUE),
			&hdev->event_queue);
	if (rc) {
		dev_err(hdev->dev, "Failed to request IRQ %d", irq);
		goto free_dec_irqs;
	}

	greco->hw_cap_initialized |= HW_CAP_MSIX;

	return 0;

free_dec_irqs:
	greco_dec_disable_msix(hdev, GRECO_IRQ_NUM_DCORE1_DEC4_ABNRM + 1);
free_cs_irqs:
	for (i = GRECO_IRQ_NUM_CS_FIRST;
			i < GRECO_IRQ_NUM_CS_FIRST + cs_irq_init_cnt ; i++) {
		irq = pci_irq_vector(hdev->pdev, i);
		relative_idx = i - GRECO_IRQ_NUM_CS_FIRST;
		cs_irq_info = &greco->cs_irq_info_arr[relative_idx];
		free_irq(irq, cs_irq_info);
	}

	pci_free_irq_vectors(hdev->pdev);
	return rc;
}

static void greco_sync_irqs(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	int i, irq;

	if (!(greco->hw_cap_initialized & HW_CAP_MSIX))
		return;

	/* Wait for all pending IRQs to be finished */
	for (i = GRECO_IRQ_NUM_CS_FIRST ; i <= GRECO_IRQ_NUM_CS_LAST ; i++) {
		irq = pci_irq_vector(hdev->pdev, i);
		synchronize_irq(irq);
	}

	for (i = GRECO_IRQ_NUM_DCORE0_DEC0_NRM;
			i <= GRECO_IRQ_NUM_DCORE1_DEC4_ABNRM;
			i++) {
		irq = pci_irq_vector(hdev->pdev, i);
		synchronize_irq(irq);
	}

	synchronize_irq(pci_irq_vector(hdev->pdev, GRECO_IRQ_NUM_EVENT_QUEUE));
}

static void greco_disable_msix(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	struct hl_cs_irq_info *cs_irq_info;
	int i, irq, relative_idx;

	if (!greco)
		return;

	if (!(greco->hw_cap_initialized & HW_CAP_MSIX))
		return;

	greco_sync_irqs(hdev);

	irq = pci_irq_vector(hdev->pdev, GRECO_IRQ_NUM_EVENT_QUEUE);
	free_irq(irq, &hdev->event_queue);

	greco_dec_disable_msix(hdev, GRECO_IRQ_NUM_DCORE1_DEC4_ABNRM + 1);

	for (i = GRECO_IRQ_NUM_CS_FIRST ; i <= GRECO_IRQ_NUM_CS_LAST ; i++) {
		irq = pci_irq_vector(hdev->pdev, i);
		relative_idx = i - GRECO_IRQ_NUM_CS_FIRST;
		cs_irq_info = &greco->cs_irq_info_arr[relative_idx];
		free_irq(irq, cs_irq_info);
	}

	pci_free_irq_vectors(hdev->pdev);

	greco->hw_cap_initialized &= ~HW_CAP_MSIX;
}

static u64 greco_read_pte(struct hl_device *hdev, u64 addr)
{
	struct greco_device *greco = hdev->asic_specific;

	if (hdev->reset_info.hard_reset_pending)
		return U64_MAX;

	return readq(hdev->pcie_bar[DRAM_BAR_ID] +
			(addr - greco->dram_bar_cur_addr));
}

static void greco_write_pte(struct hl_device *hdev, u64 addr, u64 val)
{
	struct greco_device *greco = hdev->asic_specific;

	if (hdev->reset_info.hard_reset_pending)
		return;

	writeq(val, hdev->pcie_bar[DRAM_BAR_ID] +
			(addr - greco->dram_bar_cur_addr));
}

static int greco_mmu_update_asid_hop0_addr(struct hl_device *hdev,
					u32 stlb_base, u32 asid, u64 phys_addr)
{
	u32 status, timeout_usec;
	int rc;

	if (hdev->pldm || !hdev->pdev)
		timeout_usec = GRECO_PLDM_MMU_TIMEOUT_USEC;
	else
		timeout_usec = MMU_CONFIG_TIMEOUT_USEC;

	WREG32(stlb_base + STLB_ASID_OFFSET, asid);
	WREG32(stlb_base + STLB_HOP0_PA43_12_OFFSET,
			phys_addr >> MMU_HOP0_PA43_12_SHIFT);
	WREG32(stlb_base + STLB_HOP0_PA49_44_OFFSET,
			phys_addr >> MMU_HOP0_PA49_44_SHIFT);
	WREG32(stlb_base + STLB_BUSY_OFFSET, 0x80000000);

	rc = hl_poll_timeout(
		hdev,
		stlb_base + STLB_BUSY_OFFSET,
		status,
		!(status & 0x80000000),
		1000,
		timeout_usec);

	if (rc) {
		dev_err(hdev->dev,
			"Timeout during MMU hop0 config of asid %d\n", asid);
		return rc;
	}

	return 0;
}

static int _greco_mmu_invalidate_cache(struct hl_device *hdev,
					u32 stlb_base,
					u32 start_offset, u32 inv_start_val,
					u32 flags)
{
	u32 status, timeout_usec;
	int rc;

	if (hdev->pldm)
		timeout_usec = GRECO_PLDM_MMU_TIMEOUT_USEC;
	else
		timeout_usec = GRECO_MMU_TIMEOUT_USEC;

	if (flags & MMU_OP_CLEAR_MEMCACHE) {
		/* Clear P/DMMU mem line cache (only needed in mmu range
		 * invalidation)
		 */
		WREG32(stlb_base + STLB_RANGE_MEM_CACHE_INV_OFFSET, 0x1);

		rc = hl_poll_timeout(
			hdev,
			stlb_base + STLB_RANGE_MEM_CACHE_INV_STATUS_OFFSET,
			status,
			status & 0x1,
			1000,
			timeout_usec);

		if (rc)
			return rc;

		/* Need to manually reset the status to 0 */
		WREG32(stlb_base + STLB_RANGE_MEM_CACHE_INV_STATUS_OFFSET, 0x0);
	}

	/* Lower cache does not work with cache lines, hence we can skip its
	 * invalidation upon map and invalidate only upon unmap
	 */
	if (flags & MMU_OP_SKIP_LOW_CACHE_INV)
		return 0;

	WREG32(stlb_base + start_offset, inv_start_val);

	rc = hl_poll_timeout(
		hdev,
		stlb_base + start_offset,
		status,
		!(status & 0x1),
		1000,
		timeout_usec);

	return rc;
}

static inline int get_dmmu_stlb_base(struct hl_device *hdev,
					int dcore_id, int dmmu_id,
					u32 *dmmu_base,
					u32 *stlb_base) {
	struct greco_device *greco = hdev->asic_specific;
	u32 offset, hw_cap;

	hw_cap = HW_CAP_DCORE0_DMMU0 <<
				(NUM_OF_DMMU_PER_DCORE * dcore_id + dmmu_id);

	if (!(greco->hw_cap_initialized & hw_cap))
		return -1;

	offset =  (u32) (dcore_id * DCORE_OFFSET +
			dmmu_id * DMMU_OFFSET);

	*dmmu_base = (u32) (mmDCORE0_HMMU0_MMU_BASE + offset);
	*stlb_base = (u32)(mmDCORE0_HMMU0_STLB_BASE + offset);

	return 0;
}

int greco_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard, u32 flags)
{
	struct greco_device *greco = hdev->asic_specific;
	u32 mmu_base, stlb_base;
	int rc, dcore_id, dmmu_id;

	if (hdev->reset_info.hard_reset_pending)
		return 0;

	if ((flags & MMU_OP_USERPTR) &&
			(greco->hw_cap_initialized & HW_CAP_PMMU)) {
		stlb_base = mmPMMU_HBW_STLB_BASE;

		rc = _greco_mmu_invalidate_cache(hdev, stlb_base,
						STLB_INV_ALL_START_OFFSET, 1,
						flags);
		if (rc)
			return rc;
	} else if ((flags & MMU_OP_PHYS_PACK) &&
			(greco->hw_cap_initialized & HW_CAP_DMMU_MASK)) {
		for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++) {
			for (dmmu_id = 0 ; dmmu_id < NUM_OF_DMMU_PER_DCORE;
					dmmu_id++) {
				rc = get_dmmu_stlb_base(hdev, dcore_id,
						dmmu_id, &mmu_base, &stlb_base);
				if (rc)
					continue;

				rc = _greco_mmu_invalidate_cache(hdev,
						stlb_base,
						STLB_INV_ALL_START_OFFSET, 1,
						flags);
				if (rc)
					return rc;
			}
		}
	}

	return 0;
}

static inline int greco_start_mmu_invalidate_cache_range(
					struct hl_device *hdev,
					u32 stlb_base, u64 start, u64 end,
					u32 inv_start_val,
					u32 flags)
{
	/* Set the addresses range
	 * Note: that the start address we set in register, is not included in
	 * the range of the invalidation, by design.
	 * that's why we need to set lower address than the one we actually
	 * want to be included in the range invalidation.
	 */
	start--;

	/* set the addresses range */
	WREG32(stlb_base + STLB_RANGE_INV_START_LSB_OFFSET,
				start >> MMU_RANGE_INV_VA_LSB_SHIFT);

	WREG32(stlb_base + STLB_RANGE_INV_START_MSB_OFFSET,
				start >> MMU_RANGE_INV_VA_MSB_SHIFT);

	WREG32(stlb_base + STLB_RANGE_INV_END_LSB_OFFSET,
				end >> MMU_RANGE_INV_VA_LSB_SHIFT);

	WREG32(stlb_base + STLB_RANGE_INV_END_MSB_OFFSET,
				end >> MMU_RANGE_INV_VA_MSB_SHIFT);

	/* start the cache range invalidation */
	return _greco_mmu_invalidate_cache(hdev, stlb_base,
			STLB_RANGE_CACHE_INVALIDATION_OFFSET,
			inv_start_val, flags);
}

int greco_mmu_invalidate_cache_range(struct hl_device *hdev, bool is_hard,
					u32 flags, u32 asid, u64 va, u64 size)
{
	struct greco_device *greco = hdev->asic_specific;
	u64 start_va, end_va;
	u32 inv_start_val, mmu_base, stlb_base;
	int rc = 0, dcore_id, dmmu_id;

	if (hdev->reset_info.hard_reset_pending)
		return 0;

	inv_start_val = (1 << MMU_RANGE_INV_EN_SHIFT |
			1 << MMU_RANGE_INV_ASID_EN_SHIFT |
			asid << MMU_RANGE_INV_ASID_SHIFT);
	start_va = va;
	end_va = start_va + size;

	if ((flags & MMU_OP_USERPTR) &&
			(greco->hw_cap_initialized & HW_CAP_PMMU)) {

		/* As range invalidation does not support zero address we will
		 * do full invalidation in this case
		 */
		if (start_va)
			rc = greco_start_mmu_invalidate_cache_range(hdev,
					mmPMMU_HBW_STLB_BASE,
					start_va, end_va,
					inv_start_val, flags | MMU_OP_CLEAR_MEMCACHE);
		else
			rc = _greco_mmu_invalidate_cache(hdev,
					mmPMMU_HBW_STLB_BASE,
					STLB_INV_ALL_START_OFFSET, 1,
					flags);
		if (rc)
			return rc;

	} else if ((flags & MMU_OP_PHYS_PACK) &&
			(greco->hw_cap_initialized & HW_CAP_DMMU_MASK)) {
		for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++) {
			for (dmmu_id = 0 ; dmmu_id < NUM_OF_DMMU_PER_DCORE;
					dmmu_id++) {

				rc = get_dmmu_stlb_base(hdev, dcore_id,
						dmmu_id,
						&mmu_base, &stlb_base);
				if (rc)
					continue;

				rc = greco_start_mmu_invalidate_cache_range(
						hdev,
						stlb_base,
						start_va, end_va,
						inv_start_val,
						flags | MMU_OP_CLEAR_MEMCACHE);
				if (rc)
					return rc;
			}
		}
	}

	return 0;
}

static int greco_mmu_update_hop0_addr(struct hl_device *hdev, u32 stlb_base,
					bool host_resident_pgt)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 hop0_addr;
	u32 asid, max_asid = prop->max_asid;
	int rc;

	/* it takes too much time to init all of the ASIDs on palladium */
	if (hdev->pldm)
		max_asid = min((u32) 8, max_asid);

	for (asid = 0 ; asid < max_asid ; asid++) {
		if (host_resident_pgt)
			hop0_addr =
			hdev->mmu_priv.hr.mmu_asid_hop0[asid].phys_addr;
		else
			hop0_addr = prop->mmu_pgt_addr +
				(asid * prop->mmu_hop_table_size);

		rc = greco_mmu_update_asid_hop0_addr(hdev, stlb_base, asid,
							hop0_addr);
		if (rc) {
			dev_err(hdev->dev,
				"failed to set hop0 addr for asid %d\n", asid);
			return rc;
		}
	}

	return 0;
}

static int greco_mmu_init_common(struct hl_device *hdev, u32 mmu_base,
					u32 stlb_base, bool host_resident_pgt)
{
	u32 status, timeout_usec;
	int rc;

	if (hdev->pldm || !hdev->pdev)
		timeout_usec = GRECO_PLDM_MMU_TIMEOUT_USEC;
	else
		timeout_usec = MMU_CONFIG_TIMEOUT_USEC;

	WREG32(stlb_base + STLB_INV_ALL_START_OFFSET, 1);

	rc = hl_poll_timeout(
		hdev,
		stlb_base + STLB_SRAM_INIT_OFFSET,
		status,
		!status,
		1000,
		timeout_usec);

	if (rc)
		dev_notice_ratelimited(hdev->dev,
			"Timeout when waiting for MMU SRAM init\n");

	rc = greco_mmu_update_hop0_addr(hdev, stlb_base, host_resident_pgt);
	if (rc)
		return rc;

	WREG32(mmu_base + MMU_BYPASS_OFFSET, 0);
	WREG32(mmu_base + MMU_SPI_SEI_MASK_OFFSET, GRECO_MMU_SPI_SEI_ENABLE_MASK);

	rc = hl_poll_timeout(
		hdev,
		stlb_base + STLB_INV_ALL_START_OFFSET,
		status,
		!status,
		1000,
		timeout_usec);

	if (rc)
		dev_notice_ratelimited(hdev->dev,
			"Timeout when waiting for MMU invalidate all\n");

	WREG32(mmu_base + MMU_ENABLE_OFFSET, 1);

	return rc;
}

static int greco_pci_mmu_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct greco_device *greco = hdev->asic_specific;
	u32 mmu_base, stlb_base;
	int rc;

	if (greco->hw_cap_initialized & HW_CAP_PMMU)
		return 0;

	dev_dbg(hdev->dev, "Initializing PCI MMU\n");

	mmu_base = mmPMMU_HBW_MMU_BASE;
	stlb_base = mmPMMU_HBW_STLB_BASE;

	/* If the page size is 64K e.g. PPC, only 8 bits are used for PTE
	 * address resolution rather than 9.
	 */
	RMWREG32(mmu_base + MMU_STATIC_MULTI_PAGE_SIZE_OFFSET,
			PAGE_SIZE == SZ_64K,
			STLB_HOP_CONFIGURATION_ONLY_LARGE_PAGE_MASK);

	rc = greco_mmu_init_common(hdev, mmu_base, stlb_base,
					prop->pmmu.host_resident);
	if (rc)
		return rc;

	greco->hw_cap_initialized |= HW_CAP_PMMU;

	dev_dbg(hdev->dev, "Finished initializing PCI MMU\n");

	return 0;
}

static int greco_dcore_dmmu_init(struct hl_device *hdev, int dcore_id,
				int dmmu_id)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct greco_device *greco = hdev->asic_specific;
	u32 offset, mmu_base, stlb_base, hw_cap, reg_val;
	int rc;

	hw_cap = HW_CAP_DCORE0_DMMU0 << (2 * dcore_id + dmmu_id);

	if (greco->hw_cap_initialized & hw_cap)
		return 0;

	dev_dbg(hdev->dev, "Initializing DCORE %d DMMU %d\n", dcore_id,
		dmmu_id);

	offset = (u32) (dcore_id * DCORE_OFFSET + dmmu_id * DMMU_OFFSET);
	mmu_base = mmDCORE0_HMMU0_MMU_BASE + offset;
	stlb_base = mmDCORE0_HMMU0_STLB_BASE + offset;

	reg_val = RREG32(mmu_base + MMU_STATIC_MULTI_PAGE_SIZE_OFFSET);
	if (hdev->use_8_bit_hops) {
		reg_val &= ~(MMU_STATIC_MULTI_PAGE_SIZE_HOP3_PAGE_SIZE_MASK |
			MMU_STATIC_MULTI_PAGE_SIZE_CFG_8_BITS_HOP_MODE_EN_MASK);
		reg_val |= (FIELD_PREP(MMU_STATIC_MULTI_PAGE_SIZE_HOP3_PAGE_SIZE_MASK, 1) |
			FIELD_PREP(MMU_STATIC_MULTI_PAGE_SIZE_CFG_8_BITS_HOP_MODE_EN_MASK, 1));
	} else {
		reg_val &= ~MMU_STATIC_MULTI_PAGE_SIZE_HOP3_PAGE_SIZE_MASK;
		reg_val |= FIELD_PREP(MMU_STATIC_MULTI_PAGE_SIZE_HOP3_PAGE_SIZE_MASK, 4);
	}
	WREG32(mmu_base + MMU_STATIC_MULTI_PAGE_SIZE_OFFSET, reg_val);

	RMWREG32(stlb_base + STLB_HOP_CONFIGURATION_OFFSET, 3,
			STLB_HOP_CONFIGURATION_LAST_HOP_MASK);
	RMWREG32(stlb_base + STLB_HOP_CONFIGURATION_OFFSET, 1,
			STLB_HOP_CONFIGURATION_ONLY_LARGE_PAGE_MASK);

	rc = greco_mmu_init_common(hdev, mmu_base, stlb_base,
					prop->dmmu.host_resident);
	if (rc)
		return rc;

	dev_dbg(hdev->dev, "Finished initializing DCORE %d DMMU %d\n", dcore_id,
		dmmu_id);

	greco->hw_cap_initialized |= hw_cap;

	return 0;
}

static int greco_dram_mmu_init(struct hl_device *hdev)
{
	int rc, dcore_id, dmmu_id;

	if (!hdev->dram_enable)
		return 0;

	if (hdev->mmu_enable != MMU_EN_ALL)
		return 0;

	dev_dbg(hdev->dev, "Initializing DRAM MMU\n");

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++)
		for (dmmu_id = 0 ; dmmu_id < NUM_OF_DMMU_PER_DCORE; dmmu_id++) {
			rc = greco_dcore_dmmu_init(hdev, dcore_id, dmmu_id);
			if (rc)
				return rc;
		}

	dev_dbg(hdev->dev, "Finished initializing DRAM MMU\n");

	return 0;
}

int greco_mmu_init(struct hl_device *hdev)
{
	int rc;

	if (!hdev->mmu_enable)
		return 0;

	dev_dbg(hdev->dev, "Initializing MMU\n");

	rc = greco_pci_mmu_init(hdev);
	if (rc)
		return rc;

	rc = greco_dram_mmu_init(hdev);
	if (rc)
		return rc;

	return 0;
}

static void greco_init_dma_core(struct hl_device *hdev, u32 reg_base,
				u32 dma_core_id, bool is_secure)
{
	struct cpu_dyn_regs *dyn_regs = &hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	u32 dma_err_cfg;

	/*
	 * TODO:
	 * Since GIC is becoming obsolete, remove GIC support once CI
	 * machines are running latest release (1.0.0) that supports COMMS
	 * Meaning - use dynamic regs only to init dma cores.
	 */
	u32 irq_handler_offset = hdev->asic_prop.dynamic_fw_load ?
			le32_to_cpu(dyn_regs->gic_dma_core_irq_ctrl) :
			mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR;
	u32 val = 1 << DCORE0_PDMA0_CORE_PROT_ERR_VAL_SHIFT;
	int map_tale_entry;

	if (is_secure)
		val |= 1 << DCORE0_PDMA0_CORE_PROT_VAL_SHIFT;

	WREG32(reg_base + DMA_CORE_PROT_OFFSET, val);

	WREG32(reg_base + DMA_CORE_WR_HBW_MAX_AWID_OFFSET,
			greco_dma_core_wr_hbw_max_awid_val[dma_core_id]);

	/* WA for H/W bug H3-2116. Relevant for DMAs with a transpose engine. */
	if (dma_core_id == DMA_CORE_ID_DCORE0_DDMA ||
			dma_core_id == DMA_CORE_ID_DCORE1_DDMA)
		WREG32(reg_base + DMA_CORE_WR_COMP_MAX_OUTSTAND_OFFSET, 15);

	map_tale_entry = greco_dma_core_async_event_id[dma_core_id];

	dma_err_cfg = 1 << DCORE0_PDMA0_CORE_ERR_CFG_ERR_MSG_EN_SHIFT |
			1 << DCORE0_PDMA0_CORE_ERR_CFG_STOP_ON_ERR_SHIFT;
	WREG32(reg_base + DMA_CORE_ERR_CFG_OFFSET, dma_err_cfg);

	WREG32(reg_base + DMA_CORE_ERRMSG_ADDR_LO_OFFSET,
			lower_32_bits(CFG_BASE + irq_handler_offset));
	WREG32(reg_base + DMA_CORE_ERRMSG_ADDR_HI_OFFSET,
			upper_32_bits(CFG_BASE + irq_handler_offset));
	WREG32(reg_base + DMA_CORE_ERRMSG_WDATA_OFFSET,
			greco_irq_map_table[map_tale_entry].cpu_id);

	/* Enable the DMA channel */
	WREG32(reg_base + DMA_CORE_CFG_0_OFFSET,
			1 << DCORE0_PDMA0_CORE_CFG_0_EN_SHIFT);
}

static void greco_init_qman_pq(struct hl_device *hdev, u32 reg_base,
				u32 queue_id_base)
{
	struct hl_hw_queue *q;
	u32 pq_id, pq_offset;

	for (pq_id = 0 ; pq_id < NUM_OF_PQ_PER_QMAN ; pq_id++) {
		q = &hdev->kernel_queues[queue_id_base + pq_id];
		pq_offset = pq_id * 4;

		WREG32(reg_base + QM_PQ_BASE_LO_0_OFFSET + pq_offset,
				lower_32_bits(q->bus_address));
		WREG32(reg_base + QM_PQ_BASE_HI_0_OFFSET + pq_offset,
				upper_32_bits(q->bus_address));
		WREG32(reg_base + QM_PQ_SIZE_0_OFFSET + pq_offset,
				ilog2(HL_QUEUE_LENGTH));
		WREG32(reg_base + QM_PQ_PI_0_OFFSET + pq_offset, 0);
		WREG32(reg_base + QM_PQ_CI_0_OFFSET + pq_offset, 0);
	}
}

static void greco_init_qman_cp(struct hl_device *hdev, u32 reg_base)
{
	u32 cp_id, cp_offset, mtr_base_lo, mtr_base_hi, so_base_lo, so_base_hi;

	mtr_base_lo = lower_32_bits(CFG_BASE +
			mmDCORE0_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	mtr_base_hi = upper_32_bits(CFG_BASE +
			mmDCORE0_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0);
	so_base_lo = lower_32_bits(CFG_BASE +
			mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE +
			mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0);

	for (cp_id = 0 ; cp_id < NUM_OF_CP_PER_QMAN; cp_id++) {
		cp_offset = cp_id * 4;

		WREG32(reg_base + QM_CP_MSG_BASE0_ADDR_LO_0_OFFSET + cp_offset,
				mtr_base_lo);
		WREG32(reg_base + QM_CP_MSG_BASE0_ADDR_HI_0_OFFSET + cp_offset,
				mtr_base_hi);
		WREG32(reg_base + QM_CP_MSG_BASE1_ADDR_LO_0_OFFSET + cp_offset,
				so_base_lo);
		WREG32(reg_base + QM_CP_MSG_BASE1_ADDR_HI_0_OFFSET + cp_offset,
				so_base_hi);
	}
}

static void greco_init_qman_pqc(struct hl_device *hdev, u32 reg_base,
				u32 queue_id_base)
{
	struct greco_device *greco = hdev->asic_specific;
	u32 pq_id, pq_offset, so_base_lo, so_base_hi;

	so_base_lo = lower_32_bits(CFG_BASE +
			mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0);
	so_base_hi = upper_32_bits(CFG_BASE +
			mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0);

	for (pq_id = 0 ; pq_id < NUM_OF_PQ_PER_QMAN ; pq_id++) {
		pq_offset = pq_id * 4;

		WREG32(reg_base + QM_PQC_HBW_BASE_LO_0_OFFSET + pq_offset,
				lower_32_bits(greco->scratchpad_bus_address));
		WREG32(reg_base + QM_PQC_HBW_BASE_HI_0_OFFSET + pq_offset,
				upper_32_bits(greco->scratchpad_bus_address));
		WREG32(reg_base + QM_PQC_SIZE_0_OFFSET + pq_offset,
				ilog2(PAGE_SIZE / sizeof(struct hl_cq_entry)));
		WREG32(reg_base + QM_PQC_PI_0_OFFSET + pq_offset, 0);
		WREG32(reg_base + QM_PQC_LBW_WDATA_0_OFFSET + pq_offset,
				QM_PQC_LBW_WDATA);
		WREG32(reg_base + QM_PQC_LBW_BASE_LO_0_OFFSET + pq_offset,
				so_base_lo);
		WREG32(reg_base + QM_PQC_LBW_BASE_HI_0_OFFSET + pq_offset,
				so_base_hi);
	}

	/* Enable QMAN H/W completion */
	WREG32(reg_base + QM_PQC_CFG_OFFSET,
			1 << DCORE0_PDMA0_QM_PQC_CFG_EN_SHIFT);

}

static u32 greco_get_dyn_sp_reg(struct hl_device *hdev, u32 queue_id_base)
{
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	u32 sp_reg_addr;

	switch (queue_id_base) {
	case GRECO_QUEUE_ID_DCORE0_PDMA_0_0 ... GRECO_QUEUE_ID_DCORE0_DDMA_0_3:
	case GRECO_QUEUE_ID_DCORE1_PDMA_0_0 ... GRECO_QUEUE_ID_DCORE1_DDMA_0_3:
		sp_reg_addr = le32_to_cpu(dyn_regs->gic_dma_qm_irq_ctrl);
		break;
	case GRECO_QUEUE_ID_DCORE0_MME_0_0 ... GRECO_QUEUE_ID_DCORE0_MME_0_3:
	case GRECO_QUEUE_ID_DCORE1_MME_0_0 ... GRECO_QUEUE_ID_DCORE1_MME_0_3:
		sp_reg_addr = le32_to_cpu(dyn_regs->gic_mme_qm_irq_ctrl);
		break;
	case GRECO_QUEUE_ID_DCORE0_TPC_0_0 ... GRECO_QUEUE_ID_DCORE0_TPC_4_3:
	case GRECO_QUEUE_ID_DCORE1_TPC_0_0 ... GRECO_QUEUE_ID_DCORE1_TPC_4_3:
		sp_reg_addr = le32_to_cpu(dyn_regs->gic_tpc_qm_irq_ctrl);
		break;
	case GRECO_QUEUE_ID_DCORE0_ROT_0_0 ... GRECO_QUEUE_ID_DCORE0_ROT_0_3:
	case GRECO_QUEUE_ID_DCORE1_ROT_0_0 ... GRECO_QUEUE_ID_DCORE1_ROT_0_3:
		sp_reg_addr = le32_to_cpu(dyn_regs->gic_rot_qm_irq_ctrl);
		break;
	default:
		dev_err(hdev->dev, "Unexpected h/w queue %d\n", queue_id_base);
		return 0;
	}

	return sp_reg_addr;
}

static void greco_init_qman_common(struct hl_device *hdev, u32 reg_base,
					u32 queue_id_base)
{
	/*
	 * TODO:
	 * Since GIC is becoming obsolete, remove GIC support once CI
	 * machines are running latest release (1.0.0) that supports COMMS
	 * Meaning - use dynamic regs only to init qmans.
	 */
	u32 irq_handler_offset = hdev->asic_prop.dynamic_fw_load ?
			greco_get_dyn_sp_reg(hdev, queue_id_base) :
					mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR;
	u32 glbl_prot = QMAN_MAKE_TRUSTED, qm_err_cfg;
	int map_tale_entry = greco_qman_async_event_id[queue_id_base];

	WREG32(reg_base + QM_GLBL_PROT_OFFSET, glbl_prot);

	qm_err_cfg = QMAN_GLBL_ERR_CFG_MSG_EN_MASK | QMAN_GLBL_ERR_CFG_STOP_ON_ERR_EN_MASK;
	WREG32(reg_base + QM_GLBL_ERR_CFG_OFFSET, qm_err_cfg);

	WREG32(reg_base + QM_GLBL_ERR_ADDR_LO_OFFSET,
			lower_32_bits(CFG_BASE + irq_handler_offset));
	WREG32(reg_base + QM_GLBL_ERR_ADDR_HI_OFFSET,
			upper_32_bits(CFG_BASE + irq_handler_offset));
	WREG32(reg_base + QM_GLBL_ERR_WDATA_OFFSET,
			greco_irq_map_table[map_tale_entry].cpu_id);

	WREG32(reg_base + QM_ARB_ERR_MSG_EN_OFFSET, QM_ARB_ERR_MSG_EN_MASK);

	/* Set timeout to maximum */
	WREG32(reg_base + QM_ARB_SLV_CHOICE_WDT, GRECO_ARB_WDT_TIMEOUT);

	WREG32(reg_base + QM_GLBL_CFG1_OFFSET, 0);

	/* Enable the QMAN channel */
	WREG32(reg_base + QM_GLBL_CFG0_OFFSET, QMAN_ENABLE);
}

static void greco_init_qman(struct hl_device *hdev, u32 reg_base,
				u32 queue_id_base)
{
	hdev->kernel_queues[queue_id_base].cq_id = queue_id_base;

	greco_init_qman_pq(hdev, reg_base, queue_id_base);
	greco_init_qman_cp(hdev, reg_base);
	greco_init_qman_pqc(hdev, reg_base, queue_id_base);
	greco_init_qman_common(hdev, reg_base, queue_id_base);
}

static void greco_init_dcore_pdma(struct hl_device *hdev, int dcore_id)
{
	u32 pdma_id, queue_id_base, dma_core_id, reg_base;

	if (dcore_id) {
		dma_core_id = DMA_CORE_ID_DCORE1_PDMA0;
		queue_id_base = GRECO_QUEUE_ID_DCORE1_PDMA_0_0;
	} else {
		dma_core_id = DMA_CORE_ID_DCORE0_PDMA0;
		queue_id_base = GRECO_QUEUE_ID_DCORE0_PDMA_0_0;
	}

	for (pdma_id = 0 ; pdma_id < NUM_OF_PDMA_PER_DCORE;
		pdma_id++, dma_core_id++, queue_id_base += NUM_OF_PQ_PER_QMAN) {

		dev_dbg(hdev->dev, "Initializing DCORE%d PDMA%d\n",
				dcore_id, pdma_id);

		reg_base = greco_dma_core_blocks_bases[dma_core_id];
		greco_init_dma_core(hdev, reg_base, dma_core_id, false);

		reg_base = greco_qm_blocks_bases[queue_id_base];
		greco_init_qman(hdev, reg_base, queue_id_base);
	}
}

void greco_init_pdma(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	int dcore_id;

	if ((greco->hw_cap_initialized & HW_CAP_PDMA_MASK) == HW_CAP_PDMA_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing PDMA QMANs\n");

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++)
		greco_init_dcore_pdma(hdev, dcore_id);

	greco->hw_cap_initialized |= HW_CAP_PDMA_MASK;
}

static void greco_init_dcore_ddma(struct hl_device *hdev, int dcore_id)
{
	u32 queue_id_base, dma_core_id, reg_base;

	dev_dbg(hdev->dev, "Initializing DCORE%d DDMA\n", dcore_id);

	if (dcore_id) {
		dma_core_id = DMA_CORE_ID_DCORE1_DDMA;
		queue_id_base = GRECO_QUEUE_ID_DCORE1_DDMA_0_0;
	} else {
		dma_core_id = DMA_CORE_ID_DCORE0_DDMA;
		queue_id_base = GRECO_QUEUE_ID_DCORE0_DDMA_0_0;
	}

	reg_base = greco_dma_core_blocks_bases[dma_core_id];
	greco_init_dma_core(hdev, reg_base, dma_core_id, false);

	reg_base = greco_qm_blocks_bases[queue_id_base];
	greco_init_qman(hdev, reg_base, queue_id_base);
}

void greco_init_ddma(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	int dcore_id;

	if ((greco->hw_cap_initialized & HW_CAP_DDMA_MASK) == HW_CAP_DDMA_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing DDMA QMANs\n");

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++)
		greco_init_dcore_ddma(hdev, dcore_id);

	greco->hw_cap_initialized |= HW_CAP_DDMA_MASK;
}

static void greco_init_mme_acc(struct hl_device *hdev, u32 reg_base)
{
	WREG32(reg_base + MME_ACC_INTR_MASK_OFFSET,
		0x7E << DCORE0_MME_ACC_INTR_MASK_N_SHIFT);

	WREG32(reg_base + MME_ACC_WBC_MAX_OFFSET, 0x7fff7fff);
}

static void greco_init_mme_ctrl_lo(struct hl_device *hdev, u32 reg_base, bool enable_slave_qm_clock)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 mme_sram_base_addr = prop->sram_base_address + SRAM_MME_BASE_OFFSET;

	WREG32(reg_base + MME_CTRL_LO_TWO_MASTERS_MODE_OFFSET, 0x0);
	WREG32(reg_base + MME_CTRL_LO_EU_OFFSET, 0x00FFFF1E);
	WREG32(reg_base + MME_CTRL_LO_FMA_FUNC_REDUN_CLK_EN32_OFFSET,
			0xFFFFFFFF);
	WREG32(reg_base + MME_CTRL_LO_FMA_FUNC_REDUN_CLK_EN33_OFFSET, 1);
	WREG32(reg_base + MME_CTRL_LO_IMA_FUNC_REDUN_CLK_EN32_OFFSET,
			0xFFFFFFFF);
	WREG32(reg_base + MME_CTRL_LO_IMA_FUNC_REDUN_CLK_EN33_OFFSET, 1);
	WREG32(reg_base + MME_CTRL_LO_EU_ISOLATION_DIS_OFFSET,
			DCORE0_MME_CTRL_LO_EU_ISOLATION_DIS_IMA_MASK |
			DCORE0_MME_CTRL_LO_EU_ISOLATION_DIS_FMA_MASK);
	WREG32(reg_base + MME_CTRL_LO_MME_AXUSER_HB_OVRD_LO_OFFSET, 0x3FE000);
	WREG32(reg_base + MME_CTRL_LO_MME_AXUSER_HB_OVRD_HI_OFFSET, 0);

	WREG32(reg_base + MME_CTRL_LO_FENCE_DUP_COLOR0_ADD_LO_OFFSET,
				lower_32_bits(mme_sram_base_addr));
	WREG32(reg_base + MME_CTRL_LO_FENCE_DUP_COLOR0_ADD_HI_OFFSET,
				upper_32_bits(mme_sram_base_addr));

	if (enable_slave_qm_clock)
		WREG32(reg_base + MME_CTRL_LO_QM_SLV_CLK_EN_OFFSET, 0x1);
}

static void greco_init_dcore_mme(struct hl_device *hdev, int dcore_id)
{
	bool configure_qm = true, enable_slave_qm_clock = false;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hw_queue_properties *queue_props;
	u32 queue_id_base, reg_base;

	dev_dbg(hdev->dev, "Initializing DCORE%d MME\n", dcore_id);

	queue_id_base = dcore_id ? GRECO_QUEUE_ID_DCORE1_MME_0_0 : GRECO_QUEUE_ID_DCORE0_MME_0_0;

	reg_base = greco_mme_acc_blocks_bases[dcore_id];
	greco_init_mme_acc(hdev, reg_base);

	/* DCORE1 MME works normally in slave mode, and thus its QMAN block shouldn't be configured.
	 * If not working in this mode, need to explicitly enable the QMAN clock.
	 */
	if (dcore_id == 1) {
		queue_props = &prop->hw_queues_props[queue_id_base];
		configure_qm = enable_slave_qm_clock = !queue_props->slave;
	}

	reg_base = greco_mme_ctrl_lo_blocks_bases[dcore_id];
	greco_init_mme_ctrl_lo(hdev, reg_base, enable_slave_qm_clock);

	if (configure_qm) {
		reg_base = greco_qm_blocks_bases[queue_id_base];
		greco_init_qman(hdev, reg_base, queue_id_base);
	}
}

void greco_init_mme(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	int dcore_id;

	if (!hdev->mme_mask)
		return;

	if ((greco->hw_cap_initialized & HW_CAP_MME_MASK) == HW_CAP_MME_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing MME QMANs\n");

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++)
		greco_init_dcore_mme(hdev, dcore_id);

	greco->hw_cap_initialized |= HW_CAP_MME_MASK;
}

static void greco_init_tpc_cfg(struct hl_device *hdev, u32 reg_base)
{
	u32 so_base_hi = upper_32_bits(CFG_BASE +
				mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0);

	WREG32(reg_base + TPC_CFG_SM_BASE_ADDRESS_HIGH_OFFSET, so_base_hi);

	WREG32(reg_base + TPC_CFG_STALL_ON_ERR_OFFSET, 1 << DCORE0_TPC0_CFG_STALL_ON_ERR_V_SHIFT);

	/* Mask arithmetic, QM, and invalid lock access interrupts in TPC */
	WREG32(reg_base + TPC_CFG_TPC_INTR_MASK_OFFSET, 0x408FFE);

	/* Set 16 cache lines */
	WREG32(reg_base + TPC_CFG_MSS_CONFIG_OFFSET,
			2 << DCORE0_TPC0_CFG_MSS_CONFIG_ICACHE_FETCH_LINE_NUM_SHIFT);
}

static void greco_init_dcore_tpc(struct hl_device *hdev, u32 dcore_id)
{
	struct greco_device *greco = hdev->asic_specific;
	u32 tpc_id, tpc_bit, queue_id_base, reg_base;

	queue_id_base = dcore_id ? GRECO_QUEUE_ID_DCORE1_TPC_0_0 :
					GRECO_QUEUE_ID_DCORE0_TPC_0_0;

	for (tpc_id = 0 ; tpc_id < NUM_OF_TPC_PER_DCORE;
		tpc_id++, queue_id_base += NUM_OF_PQ_PER_QMAN) {

		tpc_bit = dcore_id * NUM_OF_TPC_PER_DCORE + tpc_id;
		if (!(hdev->asic_prop.tpc_enabled_mask & BIT(tpc_bit)))
			continue;

		dev_dbg(hdev->dev, "Initializing DCORE%d TPC%d\n",
				dcore_id, tpc_id);

		reg_base = greco_tpc_cfg_blocks_bases[tpc_bit];
		greco_init_tpc_cfg(hdev, reg_base);

		reg_base = greco_qm_blocks_bases[queue_id_base];
		greco_init_qman(hdev, reg_base, queue_id_base);

		greco->hw_cap_initialized |=
				BIT_ULL(HW_CAP_TPC_SHIFT + tpc_bit);
	}
}

void greco_init_tpc(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	int dcore_id;

	if (!hdev->asic_prop.tpc_enabled_mask)
		return;

	if ((greco->hw_cap_initialized & HW_CAP_TPC_MASK) == HW_CAP_TPC_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing TPC QMANs\n");

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++)
		greco_init_dcore_tpc(hdev, dcore_id);
}

static void greco_init_rot_cfg(struct hl_device *hdev, u32 reg_base)
{
	WREG32(reg_base + ROT_ERR_CFG_OFFSET, 1 << DCORE0_ROT_ERR_CFG_STOP_ON_ERR_SHIFT);

	WREG32(reg_base + ROT_RSB_CAM_MAX_SIZE_OFFSET, 0);
}

static void greco_init_dcore_rotator(struct hl_device *hdev, int dcore_id)
{
	struct greco_device *greco = hdev->asic_specific;
	u32 queue_id_base, reg_base, rotator_bit = dcore_id ? 1 : 0;

	if (!(hdev->rotator_mask & BIT(rotator_bit)))
		return;

	dev_dbg(hdev->dev, "Initializing DCORE%d Rotator\n", dcore_id);

	queue_id_base = dcore_id ? GRECO_QUEUE_ID_DCORE1_ROT_0_0 :
					GRECO_QUEUE_ID_DCORE0_ROT_0_0;

	reg_base = greco_rot_blocks_bases[dcore_id];
	greco_init_rot_cfg(hdev, reg_base);

	reg_base = greco_qm_blocks_bases[queue_id_base];
	greco_init_qman(hdev, reg_base, queue_id_base);

	greco->hw_cap_initialized |= BIT_ULL(HW_CAP_ROT_SHIFT + rotator_bit);
}

void greco_init_rotator(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	int dcore_id;

	if (!hdev->rotator_mask)
		return;

	if ((greco->hw_cap_initialized & HW_CAP_ROT_MASK) == HW_CAP_ROT_MASK)
		return;

	dev_dbg(hdev->dev, "Initializing Rotator QMANs\n");

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++)
		greco_init_dcore_rotator(hdev, dcore_id);
}

static void greco_disable_qman_common(struct hl_device *hdev, u32 reg_base)
{
	WREG32(reg_base + QM_GLBL_CFG0_OFFSET, 0);
}

void greco_disable_pci_dma_qmans(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_PDMA_MASK))
		return;

	greco_disable_qman_common(hdev, mmDCORE0_PDMA0_QM_BASE);
	greco_disable_qman_common(hdev, mmDCORE0_PDMA1_QM_BASE);
	greco_disable_qman_common(hdev, mmDCORE1_PDMA0_QM_BASE);
	greco_disable_qman_common(hdev, mmDCORE1_PDMA1_QM_BASE);
}

void greco_disable_dcore_dma_qmans(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_DDMA_MASK))
		return;

	greco_disable_qman_common(hdev, mmDCORE0_DDMA_QM_BASE);
	greco_disable_qman_common(hdev, mmDCORE1_DDMA_QM_BASE);
}

void greco_disable_mme_qmans(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct greco_device *greco = hdev->asic_specific;
	struct hw_queue_properties *queue_props;

	if (!(greco->hw_cap_initialized & HW_CAP_MME_MASK))
		return;

	greco_disable_qman_common(hdev, mmDCORE0_MME_QM_BASE);

	queue_props = &prop->hw_queues_props[GRECO_QUEUE_ID_DCORE1_MME_0_0];
	if (!queue_props->slave)
		greco_disable_qman_common(hdev, mmDCORE1_MME_QM_BASE);
}

void greco_disable_tpc_qmans(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC0)
		greco_disable_qman_common(hdev, mmDCORE0_TPC0_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC1)
		greco_disable_qman_common(hdev, mmDCORE0_TPC1_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC2)
		greco_disable_qman_common(hdev, mmDCORE0_TPC2_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC3)
		greco_disable_qman_common(hdev, mmDCORE0_TPC3_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC4)
		greco_disable_qman_common(hdev, mmDCORE0_TPC4_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC0)
		greco_disable_qman_common(hdev, mmDCORE1_TPC0_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC1)
		greco_disable_qman_common(hdev, mmDCORE1_TPC1_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC2)
		greco_disable_qman_common(hdev, mmDCORE1_TPC2_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC3)
		greco_disable_qman_common(hdev, mmDCORE1_TPC3_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC4)
		greco_disable_qman_common(hdev, mmDCORE1_TPC4_QM_BASE);
}

void greco_disable_rotator_qmans(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_ROT_MASK))
		return;

	greco_disable_qman_common(hdev, mmDCORE0_ROT_QM_BASE);
	greco_disable_qman_common(hdev, mmDCORE1_ROT_QM_BASE);
}

static void greco_stop_qman_common(struct hl_device *hdev, u32 reg_base)
{
	WREG32(reg_base + QM_GLBL_CFG1_OFFSET,
					QM_GLBL_CFG1_PQF_STOP |
					QM_GLBL_CFG1_CQF_STOP |
					QM_GLBL_CFG1_CP_STOP);
}

void greco_stop_pci_dma_qmans(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_PDMA_MASK))
		return;

	greco_stop_qman_common(hdev, mmDCORE0_PDMA0_QM_BASE);
	greco_stop_qman_common(hdev, mmDCORE0_PDMA1_QM_BASE);
	greco_stop_qman_common(hdev, mmDCORE1_PDMA0_QM_BASE);
	greco_stop_qman_common(hdev, mmDCORE1_PDMA1_QM_BASE);
}

void greco_stop_dcore_dma_qmans(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_DDMA_MASK))
		return;

	greco_stop_qman_common(hdev, mmDCORE0_DDMA_QM_BASE);
	greco_stop_qman_common(hdev, mmDCORE1_DDMA_QM_BASE);
}

void greco_stop_mme_qmans(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct greco_device *greco = hdev->asic_specific;
	struct hw_queue_properties *queue_props;

	if (!(greco->hw_cap_initialized & HW_CAP_MME_MASK))
		return;

	greco_stop_qman_common(hdev, mmDCORE0_MME_QM_BASE);

	queue_props = &prop->hw_queues_props[GRECO_QUEUE_ID_DCORE1_MME_0_0];
	if (!queue_props->slave)
		greco_stop_qman_common(hdev, mmDCORE1_MME_QM_BASE);
}

void greco_stop_tpc_qmans(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC0)
		greco_stop_qman_common(hdev, mmDCORE0_TPC0_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC1)
		greco_stop_qman_common(hdev, mmDCORE0_TPC1_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC2)
		greco_stop_qman_common(hdev, mmDCORE0_TPC2_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC3)
		greco_stop_qman_common(hdev, mmDCORE0_TPC3_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC4)
		greco_stop_qman_common(hdev, mmDCORE0_TPC4_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC0)
		greco_stop_qman_common(hdev, mmDCORE1_TPC0_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC1)
		greco_stop_qman_common(hdev, mmDCORE1_TPC1_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC2)
		greco_stop_qman_common(hdev, mmDCORE1_TPC2_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC3)
		greco_stop_qman_common(hdev, mmDCORE1_TPC3_QM_BASE);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC4)
		greco_stop_qman_common(hdev, mmDCORE1_TPC4_QM_BASE);
}

void greco_stop_rotator_qmans(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_ROT_MASK))
		return;

	greco_stop_qman_common(hdev, mmDCORE0_ROT_QM_BASE);
	greco_stop_qman_common(hdev, mmDCORE1_ROT_QM_BASE);
}

static void greco_stall_dma_common(struct hl_device *hdev, u32 reg_base)
{
	WREG32(reg_base + DMA_CORE_CFG_1_OFFSET,
		1 << DCORE0_PDMA0_CORE_CFG_1_HALT_SHIFT);
}

void greco_pci_dma_stall(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_PDMA_MASK))
		return;

	greco_stall_dma_common(hdev, mmDCORE0_PDMA0_CORE_BASE);
	greco_stall_dma_common(hdev, mmDCORE0_PDMA1_CORE_BASE);
	greco_stall_dma_common(hdev, mmDCORE1_PDMA0_CORE_BASE);
	greco_stall_dma_common(hdev, mmDCORE1_PDMA1_CORE_BASE);
}

void greco_dcore_dma_stall(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_DDMA_MASK))
		return;

	greco_stall_dma_common(hdev, mmDCORE0_DDMA_CORE_BASE);
	greco_stall_dma_common(hdev, mmDCORE1_DDMA_CORE_BASE);
}

void greco_mme_stall(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_MME_MASK))
		return;

	WREG32(mmDCORE0_MME_CTRL_LO_QM_STALL, 1);
	WREG32(mmDCORE1_MME_CTRL_LO_QM_STALL, 1);
}

void greco_tpc_stall(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC0)
		WREG32(mmDCORE0_TPC0_CFG_TPC_STALL, 1);

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC1)
		WREG32(mmDCORE0_TPC1_CFG_TPC_STALL, 1);

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC2)
		WREG32(mmDCORE0_TPC2_CFG_TPC_STALL, 1);

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC3)
		WREG32(mmDCORE0_TPC3_CFG_TPC_STALL, 1);

	if (greco->hw_cap_initialized & HW_CAP_DCORE0_TPC4)
		WREG32(mmDCORE0_TPC4_CFG_TPC_STALL, 1);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC0)
		WREG32(mmDCORE1_TPC0_CFG_TPC_STALL, 1);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC1)
		WREG32(mmDCORE1_TPC1_CFG_TPC_STALL, 1);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC2)
		WREG32(mmDCORE1_TPC2_CFG_TPC_STALL, 1);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC3)
		WREG32(mmDCORE1_TPC3_CFG_TPC_STALL, 1);

	if (greco->hw_cap_initialized & HW_CAP_DCORE1_TPC4)
		WREG32(mmDCORE1_TPC4_CFG_TPC_STALL, 1);
}

void greco_rotator_stall(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	u32 reg_val;

	if (!(greco->hw_cap_initialized & HW_CAP_ROT_MASK))
		return;

	reg_val = FIELD_PREP(ROT_MSS_HALT_WBC_MASK, 0x1) |
			FIELD_PREP(ROT_MSS_HALT_SB_MASK, 0x1);
	WREG32(mmDCORE0_ROT_MSS_HALT, reg_val);
	WREG32(mmDCORE1_ROT_MSS_HALT, reg_val);
}

static void greco_stop_dcore_dec(struct hl_device *hdev, int dcore_id)
{
	u32 timeout_usec, dec_id, dec_bit, offset, graceful,
		graceful_pend_mask = DCORE0_VDEC0_BRDG_CTRL_GRACEFUL_PEND_MASK;
	int rc;

	if (hdev->pldm)
		timeout_usec = GRECO_PLDM_VDEC_TIMEOUT_USEC;
	else
		timeout_usec = GRECO_VDEC_TIMEOUT_USEC;

	for (dec_id = 0 ; dec_id < NUM_OF_DEC_PER_DCORE ; dec_id++) {
		dec_bit = dcore_id * NUM_OF_DEC_PER_DCORE + dec_id;
		if (!(hdev->asic_prop.decoder_enabled_mask & BIT(dec_bit)))
			continue;

		offset = dcore_id * DCORE_OFFSET + dec_id * VDEC_OFFSET;

		WREG32(mmDCORE0_DEC0_CMD_SWREG16 + offset, 0);

		WREG32(mmDCORE0_VDEC0_BRDG_CTRL_GRACEFUL + offset,
			0x1 << DCORE0_VDEC0_BRDG_CTRL_GRACEFUL_STOP_SHIFT);

		rc = hl_poll_timeout(
				hdev,
				mmDCORE0_VDEC0_BRDG_CTRL_GRACEFUL + offset,
				graceful,
				(graceful & graceful_pend_mask),
				100,
				timeout_usec);
		if (rc)
			dev_err(hdev->dev,
				"Failed to stop traffic from DCORE%d Decoder %d\n",
				dcore_id, dec_id);
	}
}

void greco_stop_dec(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	int dcore_id;

	if (!(greco->hw_cap_initialized & HW_CAP_DEC_MASK))
		return;

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++)
		greco_stop_dcore_dec(hdev, dcore_id);
}

static void greco_enable_timestamp(struct hl_device *hdev)
{
	/* Disable the timestamp counter */
	WREG32(mmPSOC_TIMESTAMP_BASE, 0);

	/* Zero the lower/upper parts of the 64-bit counter */
	WREG32(mmPSOC_TIMESTAMP_BASE + 0xC, 0);
	WREG32(mmPSOC_TIMESTAMP_BASE + 0x8, 0);

	/* Enable the counter */
	WREG32(mmPSOC_TIMESTAMP_BASE, 1);
}

static void greco_disable_timestamp(struct hl_device *hdev)
{
	/* Disable the timestamp counter */
	WREG32(mmPSOC_TIMESTAMP_BASE, 0);
}

static void greco_halt_engines(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	u32 wait_timeout_ms;

	if (hdev->pldm)
		wait_timeout_ms = GRECO_PLDM_RESET_WAIT_MSEC;
	else
		wait_timeout_ms = GRECO_RESET_WAIT_MSEC;

	if (fw_reset)
		goto skip_engines;

	greco_stop_rotator_qmans(hdev);
	greco_stop_mme_qmans(hdev);
	greco_stop_tpc_qmans(hdev);
	greco_stop_dcore_dma_qmans(hdev);
	greco_stop_pci_dma_qmans(hdev);

	msleep(wait_timeout_ms);

	greco_pci_dma_stall(hdev);
	greco_dcore_dma_stall(hdev);
	greco_tpc_stall(hdev);
	greco_mme_stall(hdev);
	greco_rotator_stall(hdev);

	msleep(wait_timeout_ms);

	greco_stop_dec(hdev);

	greco_disable_rotator_qmans(hdev);
	greco_disable_mme_qmans(hdev);
	greco_disable_tpc_qmans(hdev);
	greco_disable_dcore_dma_qmans(hdev);
	greco_disable_pci_dma_qmans(hdev);
	greco_disable_timestamp(hdev);

skip_engines:
	if (hard_reset)
		greco_disable_msix(hdev);
	else
		greco_sync_irqs(hdev);
}

int greco_load_firmware_to_device(struct hl_device *hdev)
{
	void __iomem *dst;

	dst = hdev->pcie_bar[DRAM_BAR_ID] + LINUX_FW_OFFSET;

	return hl_fw_load_fw_to_device(hdev, GRECO_LINUX_FW_FILE, dst, 0, 0);
}

static int greco_load_boot_fit_to_device(struct hl_device *hdev)
{
	void __iomem *dst;

	dst = hdev->pcie_bar[SRAM_CFG_BAR_ID] + BOOT_FIT_SRAM_OFFSET;

	return hl_fw_load_fw_to_device(hdev, GRECO_BOOT_FIT_FILE, dst, 0, 0);
}

static void greco_init_dynamic_firmware_loader(struct hl_device *hdev)
{
	struct dynamic_fw_load_mgr *dynamic_loader;
	struct cpu_dyn_regs *dyn_regs;

	dynamic_loader = &hdev->fw_loader.dynamic_loader;

	/*
	 * here we update initial values for few specific dynamic regs (as
	 * before reading the first descriptor from FW those value has to be
	 * hard-coded) in later stages of the protocol those values will be
	 * updated automatically by reading the FW descriptor so data there
	 * will always be up-to-date
	 */
	dyn_regs = &dynamic_loader->comm_desc.cpu_dyn_regs;
	dyn_regs->kmd_msg_to_cpu =
				cpu_to_le32(mmPSOC_GLOBAL_CONF_KMD_MSG_TO_CPU);
	dyn_regs->cpu_cmd_status_to_host =
				cpu_to_le32(mmCPU_CMD_STATUS_TO_HOST);

	dynamic_loader->wait_for_bl_timeout = GRECO_WAIT_FOR_BL_TIMEOUT_USEC;
}

static void greco_init_static_firmware_loader(struct hl_device *hdev)
{
	struct static_fw_load_mgr *static_loader;

	static_loader = &hdev->fw_loader.static_loader;

	/* greco holds the preboot version in the SP SRAM */
	static_loader->preboot_version_max_off = SCRATCHPAD_SRAM_ADDR -
			SRAM_BASE_ADDR + SCRATCHPAD_SRAM_SIZE - VERSION_MAX_LEN;
	/*
	 * as this code runs in early stage we don't know whether or not we have
	 * SRAM binning. for this reason we take the worst-case (half the SRAM)
	 */
	static_loader->boot_fit_version_max_off =
					(SRAM_SIZE / 2) - VERSION_MAX_LEN;
	static_loader->kmd_msg_to_cpu_reg = mmPSOC_GLOBAL_CONF_KMD_MSG_TO_CPU;
	static_loader->cpu_cmd_status_to_host_reg = mmCPU_CMD_STATUS_TO_HOST;
	static_loader->cpu_boot_status_reg = mmPSOC_GLOBAL_CONF_CPU_BOOT_STATUS;
	static_loader->cpu_boot_dev_status0_reg = mmCPU_BOOT_DEV_STS0;
	static_loader->cpu_boot_dev_status1_reg = mmCPU_BOOT_DEV_STS1;
	static_loader->boot_err0_reg = mmCPU_BOOT_ERR0;
	static_loader->boot_err1_reg = mmCPU_BOOT_ERR1;
	static_loader->preboot_version_offset_reg = mmPREBOOT_VER_OFFSET;
	static_loader->boot_fit_version_offset_reg = mmUBOOT_VER_OFFSET;
	static_loader->sram_offset_mask = ~(lower_32_bits(SRAM_BASE_ADDR));
	static_loader->cpu_reset_wait_msec = hdev->pldm ?
			GRECO_PLDM_RESET_WAIT_MSEC :
			GRECO_CPU_RESET_WAIT_MSEC;
}

static void greco_init_firmware_preload_params(struct hl_device *hdev)
{
	struct pre_fw_load_props *pre_fw_load = &hdev->fw_loader.pre_fw_load;

	pre_fw_load->cpu_boot_status_reg = mmPSOC_GLOBAL_CONF_CPU_BOOT_STATUS;
	pre_fw_load->sts_boot_dev_sts0_reg = mmCPU_BOOT_DEV_STS0;
	pre_fw_load->sts_boot_dev_sts1_reg = mmCPU_BOOT_DEV_STS1;
	pre_fw_load->boot_err0_reg = mmCPU_BOOT_ERR0;
	pre_fw_load->boot_err1_reg = mmCPU_BOOT_ERR1;
	pre_fw_load->wait_for_preboot_timeout = GRECO_BOOT_FIT_REQ_TIMEOUT_USEC;
}

static void greco_init_firmware_loader(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct fw_load_mgr *fw_loader = &hdev->fw_loader;

	/* fill common fields */
	fw_loader->fw_comp_loaded = FW_TYPE_NONE;
	fw_loader->boot_fit_img.image_name = GRECO_BOOT_FIT_FILE;
	fw_loader->linux_img.image_name = GRECO_LINUX_FW_FILE;
	fw_loader->boot_fit_timeout = GRECO_BOOT_FIT_REQ_TIMEOUT_USEC;
	fw_loader->skip_bmc = false;
	fw_loader->sram_bar_id = SRAM_CFG_BAR_ID;
	fw_loader->dram_bar_id = DRAM_BAR_ID;
	fw_loader->cpu_timeout = GRECO_CPU_TIMEOUT_USEC;

	if (prop->dynamic_fw_load)
		greco_init_dynamic_firmware_loader(hdev);
	else
		greco_init_static_firmware_loader(hdev);
}

static int greco_init_cpu(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	int rc;

	if (!(hdev->fw_components & FW_TYPE_PREBOOT_CPU)) {
		if (hdev->pldm) {
			/* Take ARM and ARC debug cores out of reset */
			WREG32(mmCPU_CA53_CFG_ARM_RST_CONTROL, 0x11000);
			WREG32(mmPSOC_ARC0_CFG_ARC_RST, 0x110);
			WREG32(mmPSOC_ARC1_CFG_ARC_RST, 0x110);
		}
		return 0;
	}

	if (greco->hw_cap_initialized & HW_CAP_CPU)
		return 0;

	if (hdev->pldm)
		return greco_pldm_init_cpu(hdev);

	rc = hl_fw_init_cpu(hdev);

	if (rc)
		return rc;

	greco->hw_cap_initialized |= HW_CAP_CPU;

	return 0;
}

static void greco_init_dcore_kdma(struct hl_device *hdev, int dcore_id)
{
	u32 dma_core_id, reg_base, offset = dcore_id * DCORE_OFFSET;

	dev_dbg(hdev->dev, "Initializing DCORE%d KDMA\n", dcore_id);

	WREG32(mmDCORE0_KDMA_CORE_CTX_AXUSER_HB_SEC + offset,
			1 << DCORE0_KDMA_MSTR_IF_AXUSER_HB_SEC_MMBP_SHIFT);

	dma_core_id = dcore_id ? DMA_CORE_ID_DCORE1_KDMA :
					DMA_CORE_ID_DCORE0_KDMA;
	reg_base = greco_dma_core_blocks_bases[dma_core_id];
	greco_init_dma_core(hdev, reg_base, dma_core_id, true);
}

void greco_init_kdma(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	u32 dcore_id;

	if ((greco->hw_cap_initialized & HW_CAP_KDMA_MASK) == HW_CAP_KDMA_MASK)
		return;

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++)
		greco_init_dcore_kdma(hdev, dcore_id);

	greco->hw_cap_initialized |= HW_CAP_KDMA_MASK;
}

static void greco_init_vdec_vcmd(struct hl_device *hdev, int dcore_id,
					int dec_id)
{
	u32 offset = dcore_id * DCORE_OFFSET + dec_id * VDEC_OFFSET;

	/* Clear reset indication */
	WREG32(mmDCORE0_DEC0_CMD_SWREG17 + offset,
			DCORE0_DEC0_CMD_SWREG17_SW_IRQ_RESET_MASK);
}

static void greco_init_vdec_brdg_ctrl(struct hl_device *hdev, int dcore_id,
					int dec_id)
{
	u32 offset = dcore_id * DCORE_OFFSET + dec_id * VDEC_OFFSET;

	/* The output rate of a decoder block is 4Gbps. The outputs of all decoder
	 * blocks enter into an arbiter that has three 8Gbps input buses.
	 * The "arbiter channel select" configuration is used to balance the
	 * traffic by spreading the decoders between the input buses:
	 * DEC0/1 -> channel 0, DEC2/3 -> 1, and DEC4 -> 2.
	 */
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_FT_CH1_SEL + offset, dec_id >> 1);

	/* Unmask idle indications from IP towards the CGM */
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_IDLE_MASK + offset, 0);

	/* Decoder access to HBW/LBW is non-secure */
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_DEC_HBW_AWPROT + offset, 0x2);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_DEC_HBW_ARPROT + offset, 0x2);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_DEC_LBW_AWPROT + offset, 0x2);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_DEC_LBW_ARPROT + offset, 0x2);

	/* Mask decoder start/end messages to ARC.
	 *
	 * TODO:
	 * The handling of the BRDG_CTRL_ARC_* registers was temporarily removed
	 * from F/W, because binning is currently not configured by it, and thus
	 * it cannot access the decoders and their wrappers.
	 * Need to re-enable the if statement when this handling returns to F/W.
	 */
#if 0
	if (!(hdev->fw_components & FW_TYPE_LINUX) || hdev->pldm)
#endif
		WREG32(mmDCORE0_VDEC0_BRDG_CTRL_ARC_MSG_MASK + offset, 1);

	/* Enable statistics counters */
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_STAT_CNTR_EN + offset, 1);

	/* Mask decoder interrupt indication [legacy mode] */
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_VCD_INTR_MASK + offset, 1);

	/* Mask L2C interrupt indication [legacy mode] */
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_L2C_INTR_MASK + offset, 1);

	/* VCMD normal interrupt:
	 * - Interrupt clear is enabled (write 0x40 into offset 0x44 in VCMD)
	 * - Status register read is disabled
	 * - MSI-X generation is enabled
	 */
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_NRM_INTR_MASK + offset, 0);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_NRM_MSIX_FLOW_MASK + offset,
		0x1 << DCORE0_VDEC0_BRDG_CTRL_NRM_MSIX_FLOW_MASK_APB_RD_SHIFT);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_NRM_APB_WR_ADDR + offset, 0x44);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_NRM_APB_WR_DATA + offset, 0x40);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_NRM_MSIX_LBW_AWPROT + offset, 0);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_NRM_MSIX_LBW_AWADDR + offset,
			mmPCIE_DBI_MSIX_DOORBELL_OFF);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_NRM_MSIX_LBW_WDATA + offset,
			GRECO_IRQ_NUM_DCORE0_DEC0_NRM +
			(dcore_id * NUM_OF_DEC_PER_DCORE + dec_id) * 2);

	/* VCMD abnormal interrupt:
	 * - Interrupt clear is disabled
	 * - Status register read is disabled
	 * - MSI-X generation is enabled
	 */
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_ABNRM_INTR_MASK + offset, 0);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_ABNRM_MSIX_FLOW_MASK + offset,
		0x1 << DCORE0_VDEC0_BRDG_CTRL_NRM_MSIX_FLOW_MASK_APB_WR_SHIFT |
		0x1 << DCORE0_VDEC0_BRDG_CTRL_NRM_MSIX_FLOW_MASK_APB_RD_SHIFT);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_ABNRM_MSIX_LBW_AWPROT + offset, 0);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_ABNRM_MSIX_LBW_AWADDR + offset,
			mmPCIE_DBI_MSIX_DOORBELL_OFF);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_ABNRM_MSIX_LBW_WDATA + offset,
			GRECO_IRQ_NUM_DCORE0_DEC0_NRM +
			(dcore_id * NUM_OF_DEC_PER_DCORE + dec_id) * 2 + 1);
}

static void greco_mask_vdec_ecc_interrupts(struct hl_device *hdev, int dcore_id,
						int dec_id)
{
	u32 offset = dcore_id * DCORE_OFFSET + dec_id * VDEC_OFFSET, ecc_err_mask;
	u8 l2c_mem_id = 0;
	int i;

	/* TODO: Re-enable the if statement when VDEC memories scrubbing is done by FW (SW-88332) */
#if 0
	if (hdev->fw_components & FW_TYPE_LINUX)
		return;
#endif

	/* W/A for H5-1675:
	 * Mask ECC interrupts for VDEC memories because they might be read before being written.
	 */
	ecc_err_mask = FIELD_PREP(DCORE0_VDEC0_CTRL_SPECIAL_MEM_ECC_ERR_MASK_SERR_MASK, 1) |
			FIELD_PREP(DCORE0_VDEC0_CTRL_SPECIAL_MEM_ECC_ERR_MASK_DERR_MASK, 1);

	for (i = 0 ; i < GRECO_NUM_OF_VCD_MEMORIES ; i++) {
		WREG32(mmDCORE0_VDEC0_CTRL_SPECIAL_MEM_ECC_SEL + offset, i);
		WREG32(mmDCORE0_VDEC0_CTRL_SPECIAL_MEM_ECC_ERR_MASK + offset, ecc_err_mask);
	}

	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_SPECIAL_MEM_ECC_SEL + offset, l2c_mem_id);
	WREG32(mmDCORE0_VDEC0_BRDG_CTRL_SPECIAL_MEM_ECC_ERR_MASK + offset, ecc_err_mask);
}

static void greco_init_dcore_dec(struct hl_device *hdev, int dcore_id)
{
	struct greco_device *greco = hdev->asic_specific;
	u32 dec_id, dec_bit;

	for (dec_id = 0 ; dec_id < NUM_OF_DEC_PER_DCORE ; dec_id++) {
		dec_bit = dcore_id * NUM_OF_DEC_PER_DCORE + dec_id;
		if (!(hdev->asic_prop.decoder_enabled_mask & BIT(dec_bit)))
			continue;

		dev_dbg(hdev->dev, "Initializing DCORE%d Decoder %d\n", dcore_id, dec_id);

		greco_init_vdec_vcmd(hdev, dcore_id, dec_id);
		greco_init_vdec_brdg_ctrl(hdev, dcore_id, dec_id);
		greco_mask_vdec_ecc_interrupts(hdev, dcore_id, dec_id);

		greco->hw_cap_initialized |=
				BIT_ULL(HW_CAP_DEC_SHIFT + dec_bit);
	}
}

void greco_init_dec(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	int dcore_id;

	if (!hdev->asic_prop.decoder_enabled_mask)
		return;

	if ((greco->hw_cap_initialized & HW_CAP_DEC_MASK) == HW_CAP_DEC_MASK)
		return;

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++)
		greco_init_dcore_dec(hdev, dcore_id);
}

int greco_init_cpu_queues(struct hl_device *hdev, u32 cpu_timeout)
{
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	struct hl_hw_queue *cpu_pq =
			&hdev->kernel_queues[GRECO_QUEUE_ID_CPU_PQ];
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct greco_device *greco = hdev->asic_specific;
	u32 status, irq_handler_offset, irq_handler_val;
	struct hl_eq *eq;
	int err;

	if (!hdev->cpu_queues_enable)
		return 0;

	if (greco->hw_cap_initialized & HW_CAP_CPU_Q)
		return 0;

	eq = &hdev->event_queue;

	dev_dbg(hdev->dev, "Initializing CPU PQ/CQ/EQ\n");

	WREG32(mmCPU_IF_PQ_BASE_ADDR_LOW, lower_32_bits(cpu_pq->bus_address));
	WREG32(mmCPU_IF_PQ_BASE_ADDR_HIGH, upper_32_bits(cpu_pq->bus_address));

	WREG32(mmCPU_IF_EQ_BASE_ADDR_LOW, lower_32_bits(eq->bus_address));
	WREG32(mmCPU_IF_EQ_BASE_ADDR_HIGH, upper_32_bits(eq->bus_address));

	WREG32(mmCPU_IF_CQ_BASE_ADDR_LOW,
			lower_32_bits(hdev->cpu_accessible_dma_address));
	WREG32(mmCPU_IF_CQ_BASE_ADDR_HIGH,
			upper_32_bits(hdev->cpu_accessible_dma_address));

	WREG32(mmCPU_IF_PQ_LENGTH, HL_QUEUE_SIZE_IN_BYTES);
	WREG32(mmCPU_IF_EQ_LENGTH, HL_EQ_SIZE_IN_BYTES);
	WREG32(mmCPU_IF_CQ_LENGTH, HL_CPU_ACCESSIBLE_MEM_SIZE);

	/* Used for EQ CI */
	WREG32(mmCPU_IF_EQ_RD_OFFS, 0);

	WREG32(mmCPU_IF_PF_PQ_PI, 0);

	WREG32(mmCPU_IF_QUEUE_INIT, PQ_INIT_STATUS_READY_FOR_CP);

	/* Let the ARC know we are ready as it is now handling those queues  */
	/*
	 * TODO:
	 * Since ARC1 IRQ usage from LKD is becoming obsolete,
	 * remove ARC1 IRQ support once CI machines are running latest
	 * release (1.0.0) that supports COMMS.
	 * Meaning - use dynamic regs only to trigger pi updates.
	 */
	irq_handler_offset = hdev->asic_prop.dynamic_fw_load ?
			le32_to_cpu(dyn_regs->gic_host_pi_upd_irq) :
			mmPSOC_ARC1_AUX_SW_INTR_0;

	irq_handler_val = hdev->asic_prop.dynamic_fw_load ?
			greco_irq_map_table
			[GRECO_EVENT_CPU_PI_UPDATE].cpu_id : 1;

	WREG32(irq_handler_offset, irq_handler_val);

	dev_dbg(hdev->dev,
		"Going to wait up to %ds for device CPU\n",
		cpu_timeout / 1000 / 1000);

	err = hl_poll_timeout(
		hdev,
		mmCPU_IF_QUEUE_INIT,
		status,
		(status == PQ_INIT_STATUS_READY_FOR_HOST),
		1000,
		cpu_timeout);

	if (err) {
		dev_err(hdev->dev,
			"Failed to communicate with device CPU (timeout)\n");
		return -EIO;
	}

	/* update FW application security bits */
	if (prop->fw_cpu_boot_dev_sts0_valid)
		prop->fw_app_cpu_boot_dev_sts0 = RREG32(mmCPU_BOOT_DEV_STS0);

	if (prop->fw_cpu_boot_dev_sts1_valid)
		prop->fw_app_cpu_boot_dev_sts1 = RREG32(mmCPU_BOOT_DEV_STS1);

	greco->hw_cap_initialized |= HW_CAP_CPU_Q;
	return 0;
}

static int greco_hw_init(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	int rc = 0;

	greco_pre_hw_init(hdev);

	/* Let's mark in the H/W that we have reached this point. We check
	 * this value in the reset_before_init function to understand whether
	 * we need to reset the chip before doing H/W init. This register is
	 * cleared by the H/W upon H/W reset
	 */
	WREG32(mmHW_STATE, HL_DEVICE_HW_STATE_DIRTY);

	/* Perform read from the device to make sure device is up */
	RREG32(mmHW_STATE);

	rc = greco_init_pll(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize PLLs\n");
		return rc;
	}

	rc = greco_init_dram(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize LPDDR5\n");
		return rc;
	}

	greco_dram_shift_cfg(hdev);

	/* If iATU is done by FW, the DRAM bar ALWAYS points to DRAM_PHYS_BASE.
	 * So we set it here and if anyone tries to move it later to
	 * a different address, there will be an error
	 */
	if (hdev->asic_prop.iatu_done_by_fw)
		greco->dram_bar_cur_addr = DRAM_PHYS_BASE;

	/*
	 * Before pushing u-boot/linux to device, need to set the ddr bar to
	 * base address of dram
	 */
	if (greco_set_dram_bar_base(hdev, DRAM_PHYS_BASE) == U64_MAX) {
		dev_err(hdev->dev,
			"failed to map DDR bar to DRAM base address\n");
		return -EIO;
	}

	rc = greco_init_cpu(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CPU\n");
		return rc;
	}

	greco_init_kdma(hdev);

	rc = greco_mmu_init(hdev);
	if (rc)
		return rc;

	rc = greco_init_cpu_queues(hdev, GRECO_CPU_TIMEOUT_USEC);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CPU H/W queues %d\n",
			rc);
		return rc;
	}

	/* LKD will get all binning info (among the rest). It's the driver's
	 * duty to later-on report FW that binning was performed by the driver.
	 */
	rc = greco->cpucp_info_get(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to get cpucp info\n");
		return rc;
	}

	greco_init_golden_registers(hdev);

	/* Update special blocks skipped ranges after binning info is known */
	greco_special_blocks_update_skipped_ranges(hdev);

	/* SRAM scrambler must be initialized after CPU is running from DRAM
	 * and after binning is performed
	 */
	greco_init_scrambler_sram(hdev);

	greco_init_scrambler_dram(hdev);
	/*
	 * Recalculate memory sizes if SRAM is binned
	 * if SRAM not binned PCI SRAM region size need to be updated
	 */
	if (hdev->sram_binning)
		greco_set_meminfo(hdev, SRAM_SIZE / 2);
	else
		hdev->pci_mem_region[PCI_REGION_SRAM].region_size = SRAM_SIZE;

	greco_init_security(hdev);

	greco_init_pdma(hdev);

	greco_init_ddma(hdev);

	greco_init_mme(hdev);

	greco_init_tpc(hdev);

	greco_init_rotator(hdev);

	greco_init_dec(hdev);

	greco_enable_clock_gating(hdev);

	greco_enable_timestamp(hdev);

	rc = greco_enable_msix(hdev);
	if (rc)
		goto disable_queues;

	/* Perform read from the device to flush all configuration */
	RREG32(mmHW_STATE);

	return 0;

disable_queues:
	greco_disable_tpc_qmans(hdev);
	greco_disable_mme_qmans(hdev);
	greco_disable_dcore_dma_qmans(hdev);
	greco_disable_pci_dma_qmans(hdev);

	return rc;
}

static void greco_hw_fini(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	struct greco_device *greco = hdev->asic_specific;
	u32 status, reset_timeout_ms, cpu_timeout_ms;
	u32 irq_handler_offset, irq_handler_val;
	bool driver_performs_reset = false;

	if (hdev->pldm) {
		if (hard_reset)
			reset_timeout_ms = GRECO_PLDM_HRESET_TIMEOUT_MSEC;
		else
			reset_timeout_ms = GRECO_PLDM_SRESET_TIMEOUT_MSEC;
		cpu_timeout_ms = GRECO_PLDM_RESET_WAIT_MSEC;
	} else {
		reset_timeout_ms = GRECO_RESET_TIMEOUT_MSEC;
		cpu_timeout_ms = GRECO_CPU_RESET_WAIT_MSEC;
	}

	if (fw_reset) {
		dev_dbg(hdev->dev,
			"Firmware performs HARD reset, going to wait %dms\n",
			reset_timeout_ms);

		goto skip_reset;
	}

	if (hard_reset) {
		driver_performs_reset = !!(!hdev->asic_prop.fw_security_enabled
				&& !hdev->asic_prop.hard_reset_done_by_fw);

		/* Set device to handle FLR by H/W as we will put the device
		 * CPU to halt mode
		 */
		if (driver_performs_reset)
			WREG32(mmPCIE_AUX_FLR_CTRL,
					(PCIE_AUX_FLR_CTRL_HW_CTRL_MASK |
					PCIE_AUX_FLR_CTRL_INT_MASK_MASK));

		/*
		 * If linux is loaded in the device CPU, we need to
		 * communicate with it via the SPs/GIC irqs. Otherwise,
		 * we need to use MSG_TO_CPU register in case of old F/Ws.
		 */
		if (hdev->fw_loader.fw_comp_loaded & FW_TYPE_LINUX) {
			/*
			 * TODO:
			 * Since GIC is becoming obsolete, remove GIC support
			 * once CI machines are running latest release (1.0.0)
			 * that supports COMMS, meaning - use dynamic regs only
			 * to trigger hard reset.
			 */
			irq_handler_offset = hdev->asic_prop.dynamic_fw_load ?
					le32_to_cpu(dyn_regs->gic_host_halt_irq)
					: mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR;

			WREG32(irq_handler_offset, greco_irq_map_table
					[GRECO_EVENT_CPU_HALT_MACHINE].cpu_id);
			msleep(cpu_timeout_ms);

			/* This is a hail-mary attempt to revive the card in the small chance
			 * that the f/w has experienced a watchdog event, which caused it to return
			 * back to preboot.
			 * In that case, triggering reset through GIC won't help. We need to
			 * trigger the reset as if Linux wasn't loaded.
			 *
			 * We do it only if the reset cause was HB, because that would be the
			 * indication of such an event.
			 *
			 * In case watchdog hasn't expired but we still got HB, then this won't
			 * do any damage.
			 */
			if (hdev->reset_info.curr_reset_cause == HL_RESET_CAUSE_HEARTBEAT) {
				if (hdev->asic_prop.hard_reset_done_by_fw)
					hl_fw_ask_hard_reset_without_linux(hdev);
				else
					hl_fw_ask_halt_machine_without_linux(hdev);
			}
		} else {
			if (hdev->asic_prop.hard_reset_done_by_fw)
				hl_fw_ask_hard_reset_without_linux(hdev);
			else
				hl_fw_ask_halt_machine_without_linux(hdev);
		}

		if (driver_performs_reset) {
			WREG32(mmPCIE_WRAP_PSOC_RST_CTRL,
				1 << PCIE_WRAP_PSOC_RST_CTRL_HARD_RST_SHIFT);
			dev_dbg(hdev->dev,
				"Driver issued HARD reset command, waiting up to %dms\n",
				reset_timeout_ms);
		} else {
			dev_dbg(hdev->dev,
				"Firmware performs HARD reset, going to wait %dms\n",
				reset_timeout_ms);
		}
	} else {
		/* W/A for GOYA2_0904:
		 * Perform soft reset by the CPU-CP, to make sure that ARC0
		 * doesn't access the MME TS while the MME is in reset.
		 */
		driver_performs_reset = hdev->pldm ||
				!(hdev->fw_components & FW_TYPE_LINUX);

		if (driver_performs_reset) {
			WREG32(mmPCIE_WRAP_PSOC_RST_CTRL,
				1 << PCIE_WRAP_PSOC_RST_CTRL_SOFT_RST_SHIFT);
			dev_dbg(hdev->dev,
				"Driver issued SOFT reset command, waiting up to %dms\n",
				reset_timeout_ms);
		} else {
			/*
			 * TODO:
			 * Since ARC1 IRQ usage from LKD is becoming obsolete,
			 * remove ARC1 IRQ support once CI machines are running
			 * latest release (1.0.0) that supports COMMS, meaning -
			 * use dynamic regs only to trigger soft reset.
			 */
			irq_handler_offset = hdev->asic_prop.dynamic_fw_load ?
				le32_to_cpu(dyn_regs->gic_host_soft_rst_irq) :
				mmPSOC_ARC1_AUX_SW_INTR_2;

			irq_handler_val = hdev->asic_prop.dynamic_fw_load ?
					greco_irq_map_table
					[GRECO_EVENT_CPU_SOFT_RESET].cpu_id : 1;

			WREG32(irq_handler_offset, irq_handler_val);
			dev_dbg(hdev->dev,
				"Firmware performs SOFT reset, going to wait %dms\n",
				reset_timeout_ms);
		}
	}

skip_reset:
	/* Wait a certain amount of time before checking if the reset has
	 * finished
	 */
	msleep(reset_timeout_ms);
	status = RREG32(mmPCIE_WRAP_PSOC_BOOT_MNG_DONE);
	if (!(status & PCIE_WRAP_PSOC_BOOT_MNG_DONE_PSOC_BOOT_MNG_DONE_MASK))
		dev_err(hdev->dev,
			"Timeout while waiting for device to reset 0x%x\n",
			status);

	/* Reset bit is not self-clearing, need to manually clear it */
	if (driver_performs_reset)
		WREG32(mmPCIE_WRAP_PSOC_RST_CTRL, 0);

	if (!hard_reset && greco) {
		greco->hw_cap_initialized &= ~(HW_CAP_DRAM_SCRAMBLER_SW_RESET |
				HW_CAP_PDMA_MASK | HW_CAP_DDMA_MASK | HW_CAP_MME_MASK |
				HW_CAP_TPC_MASK | HW_CAP_ROT_MASK | HW_CAP_DEC_MASK |
				HW_CAP_CLK_GATE);
		return;
	}

	if (greco) {
		greco->hw_cap_initialized &= ~(HW_CAP_DRAM | HW_CAP_PMMU | HW_CAP_CPU |
				HW_CAP_CPU_Q | HW_CAP_SRAM_SCRAMBLER |
				HW_CAP_DRAM_SCRAMBLER_MASK | HW_CAP_DMMU_MASK |
				HW_CAP_PDMA_MASK | HW_CAP_DDMA_MASK | HW_CAP_KDMA_MASK |
				HW_CAP_MME_MASK | HW_CAP_TPC_MASK | HW_CAP_ROT_MASK |
				HW_CAP_DEC_MASK | HW_CAP_CLK_GATE);

		memset(greco->events_stat, 0, sizeof(greco->events_stat));

		hdev->device_cpu_is_halted = false;
	}
}

static int greco_suspend(struct hl_device *hdev)
{
	int rc;

	rc = hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_DISABLE_PCI_ACCESS, 0x0);
	if (rc)
		dev_err(hdev->dev, "Failed to disable PCI access from CPU\n");

	return rc;
}

static int greco_resume(struct hl_device *hdev)
{
	return greco_init_iatu(hdev);
}

static int greco_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	int rc;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP |
			VM_DONTCOPY | VM_NORESERVE;

#ifdef _HAS_DMA_MMAP_COHERENT

	rc = dma_mmap_coherent(hdev->dev, vma, cpu_addr,
				(dma_addr - HOST_PHYS_BASE), size);
	if (rc)
		dev_err(hdev->dev, "dma_mmap_coherent error %d", rc);

#else

	rc = remap_pfn_range(vma, vma->vm_start,
				virt_to_phys(cpu_addr) >> PAGE_SHIFT,
				size, vma->vm_page_prot);
	if (rc)
		dev_err(hdev->dev, "remap_pfn_range error %d", rc);

#endif

	return rc;
}

static bool greco_is_queue_enabled(struct hl_device *hdev, u32 hw_queue_id)
{
	struct greco_device *greco = hdev->asic_specific;
	u32 hw_cap_bit;

	switch (hw_queue_id) {
	case GRECO_QUEUE_ID_DCORE0_PDMA_0_0 ... GRECO_QUEUE_ID_DCORE0_PDMA_1_3:
		hw_cap_bit = HW_CAP_PDMA_SHIFT +
			((hw_queue_id - GRECO_QUEUE_ID_DCORE0_PDMA_0_0) >> 2);
		break;

	case GRECO_QUEUE_ID_DCORE1_PDMA_0_0 ... GRECO_QUEUE_ID_DCORE1_PDMA_1_3:
		hw_cap_bit = HW_CAP_PDMA_SHIFT + NUM_OF_PDMA_PER_DCORE +
			((hw_queue_id - GRECO_QUEUE_ID_DCORE1_PDMA_0_0) >> 2);
		break;

	case GRECO_QUEUE_ID_DCORE0_DDMA_0_0 ... GRECO_QUEUE_ID_DCORE0_DDMA_0_3:
		hw_cap_bit = HW_CAP_DDMA_SHIFT +
			((hw_queue_id - GRECO_QUEUE_ID_DCORE0_DDMA_0_0) >> 2);
		break;

	case GRECO_QUEUE_ID_DCORE1_DDMA_0_0 ... GRECO_QUEUE_ID_DCORE1_DDMA_0_3:
		hw_cap_bit = HW_CAP_DDMA_SHIFT + NUM_OF_DDMA_PER_DCORE +
			((hw_queue_id - GRECO_QUEUE_ID_DCORE1_DDMA_0_0) >> 2);
		break;

	case GRECO_QUEUE_ID_DCORE0_MME_0_0 ... GRECO_QUEUE_ID_DCORE0_MME_0_3:
		hw_cap_bit = HW_CAP_MME_SHIFT +
			((hw_queue_id - GRECO_QUEUE_ID_DCORE0_MME_0_0) >> 2);
		break;

	case GRECO_QUEUE_ID_DCORE1_MME_0_0 ... GRECO_QUEUE_ID_DCORE1_MME_0_3:
		hw_cap_bit = HW_CAP_MME_SHIFT + NUM_OF_MME_PER_DCORE +
			((hw_queue_id - GRECO_QUEUE_ID_DCORE1_MME_0_0) >> 2);
		break;

	case GRECO_QUEUE_ID_DCORE0_TPC_0_0 ... GRECO_QUEUE_ID_DCORE0_TPC_4_3:
		hw_cap_bit = HW_CAP_TPC_SHIFT +
			((hw_queue_id - GRECO_QUEUE_ID_DCORE0_TPC_0_0) >> 2);
		break;

	case GRECO_QUEUE_ID_DCORE1_TPC_0_0 ... GRECO_QUEUE_ID_DCORE1_TPC_4_3:
		hw_cap_bit = HW_CAP_TPC_SHIFT + NUM_OF_TPC_PER_DCORE +
			((hw_queue_id - GRECO_QUEUE_ID_DCORE1_TPC_0_0) >> 2);
		break;

	case GRECO_QUEUE_ID_DCORE0_ROT_0_0 ... GRECO_QUEUE_ID_DCORE0_ROT_0_3:
		hw_cap_bit = HW_CAP_ROT_SHIFT +
			((hw_queue_id - GRECO_QUEUE_ID_DCORE0_ROT_0_0) >> 2);
		break;

	case GRECO_QUEUE_ID_DCORE1_ROT_0_0 ... GRECO_QUEUE_ID_DCORE1_ROT_0_3:
		hw_cap_bit = HW_CAP_ROT_SHIFT + NUM_OF_ROT_PER_DCORE +
			((hw_queue_id - GRECO_QUEUE_ID_DCORE1_ROT_0_0) >> 2);
		break;

	case GRECO_QUEUE_ID_CPU_PQ:
		hw_cap_bit = HW_CAP_CPU_Q_SHIFT;
		break;

	default:
		dev_err(hdev->dev, "Unexpected h/w queue %d\n", hw_queue_id);
		return false;
	}

	return !!(greco->hw_cap_initialized & BIT_ULL(hw_cap_bit));
}

void greco_ring_doorbell(struct hl_device *hdev, u32 hw_queue_id, u32 pi)
{
	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	u32 pq_offset, reg_base, db_reg_offset, db_value;
	u32 irq_handler_offset, irq_handler_val;

	/* TODO: Remove this check? */
	if (!greco_is_queue_enabled(hdev, hw_queue_id)) {
		/* Should never get here */
		dev_err(hdev->dev, "h/w queue %d is invalid. Can't set pi\n",
			hw_queue_id);
		return;
	}

	dev_dbg(hdev->dev, "submitting a job for h/w queue %d, with pi %d:\n",
		hw_queue_id, pi);

	if (hw_queue_id != GRECO_QUEUE_ID_CPU_PQ) {
		pq_offset = (hw_queue_id & 0x3) * 4;
		reg_base = greco_qm_blocks_bases[hw_queue_id];
		db_reg_offset = reg_base + QM_PQ_PI_0_OFFSET + pq_offset;
	} else {
		db_reg_offset = mmCPU_IF_PF_PQ_PI;
	}

	db_value = pi;

	dev_dbg(hdev->dev, "db_reg_offset == 0x%x\n", db_reg_offset);
	dev_dbg(hdev->dev, "db_value == 0x%08x\n", db_value);

	/* ring the doorbell */
	WREG32(db_reg_offset, db_value);

	if (hw_queue_id == GRECO_QUEUE_ID_CPU_PQ) {
		/*
		 * TODO:
		 * Since ARC1 IRQ usage from LKD is becoming obsolete,
		 * remove ARC1 IRQ support once CI machines are running latest
		 * release (1.0.0) that supports COMMS.
		 * Meaning - use dynamic regs only to trigger pi updates.
		 */
		irq_handler_offset = hdev->asic_prop.dynamic_fw_load ?
				le32_to_cpu(dyn_regs->gic_host_pi_upd_irq) :
				mmPSOC_ARC1_AUX_SW_INTR_0;

		irq_handler_val = hdev->asic_prop.dynamic_fw_load ?
				greco_irq_map_table
				[GRECO_EVENT_CPU_PI_UPDATE].cpu_id : 1;

		/* make sure device CPU will read latest data from host */
		mb();
		WREG32(irq_handler_offset, irq_handler_val);
	}
}

void greco_pqe_write(struct hl_device *hdev, __le64 *pqe, struct hl_bd *bd)
{
	__le64 *pbd = (__le64 *) bd;

	/* The QMANs are on the host memory so a simple copy suffice */
	pqe[0] = pbd[0];
	pqe[1] = pbd[1];
}

static void *greco_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	void *kernel_addr = dma_alloc_coherent(&hdev->pdev->dev, size,
						dma_handle, flags);

	/* Shift to the device's base physical address of host memory */
	if (kernel_addr)
		*dma_handle += HOST_PHYS_BASE;

	return kernel_addr;
}

static void greco_dma_free_coherent(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle)
{
	/* Cancel the device's base physical address of host memory */
	dma_addr_t fixed_dma_handle = dma_handle - HOST_PHYS_BASE;

	dma_free_coherent(&hdev->pdev->dev, size, cpu_addr, fixed_dma_handle);
}

int greco_scrub_device_dram(struct hl_device *hdev, u64 val)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 comp_addr, cur_addr = prop->dram_user_base_address;
	u32 chunk_size, busy, dcore, sob_offset, sob_addr, comp_val, ddma_commit, old_asid_mmupb;
	int rc = 0;

	sob_offset = GRECO_FIRST_AVAILABLE_SYNC_OBJECT * 4;
	sob_addr = mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0 + sob_offset;
	comp_addr = CFG_BASE + sob_addr;
	comp_val = FIELD_PREP(DCORE0_SYNC_MNGR_OBJS_SOB_OBJ_INC_MASK, 1) |
		FIELD_PREP(DCORE0_SYNC_MNGR_OBJS_SOB_OBJ_VAL_MASK, 1);

	ddma_commit =  FIELD_PREP(DCORE0_KDMA_CORE_CTX_COMMIT_LIN_MASK, 1) |
			FIELD_PREP(DCORE0_KDMA_CORE_CTX_COMMIT_MEM_SET_MASK, 1) |
			FIELD_PREP(DCORE0_KDMA_CORE_CTX_COMMIT_WR_COMP_EN_MASK, 1);

	/*
	 * set mmu bypass for the scrubbing - all ddmas are configured the same so save
	 * only the first one to restore later
	 */
	old_asid_mmupb = RREG32(mmDCORE0_DDMA_CORE_CTX_AXUSER_HB_SEC);
	for (dcore = 0 ; dcore < NUM_OF_DCORES ; dcore++)
		WREG32(mmDCORE0_DDMA_CORE_CTX_AXUSER_HB_SEC + dcore * DCORE_OFFSET,
				AXUSER_HB_SEC_MMBP_MASK);

	while (cur_addr < prop->dram_end_address) {
		int dma_num = 0;

		WREG32(sob_addr, 0);
		for (dcore = 0 ; dcore < NUM_OF_DCORES ; dcore++) {
			u32 dcore_offset = dcore * DCORE_OFFSET;

			chunk_size = min_t(u64, SZ_2G, prop->dram_end_address - cur_addr);

			dev_dbg(hdev->dev,
				"Scrubbing DRAM range 0x%09llx - 0x%09llx, DCORE%u_DDMA",
				cur_addr, cur_addr + chunk_size, dcore);

			WREG32(mmDCORE0_DDMA_CORE_CTX_SRC_BASE_LO + dcore_offset,
					lower_32_bits(val));
			WREG32(mmDCORE0_DDMA_CORE_CTX_SRC_BASE_HI + dcore_offset,
					upper_32_bits(val));

			WREG32(mmDCORE0_DDMA_CORE_CTX_DST_BASE_LO + dcore_offset,
					lower_32_bits(cur_addr));
			WREG32(mmDCORE0_DDMA_CORE_CTX_DST_BASE_HI + dcore_offset,
					upper_32_bits(cur_addr));

			WREG32(mmDCORE0_DDMA_CORE_CTX_WR_COMP_ADDR_LO + dcore_offset,
					lower_32_bits(comp_addr));
			WREG32(mmDCORE0_DDMA_CORE_CTX_WR_COMP_ADDR_HI + dcore_offset,
					upper_32_bits(comp_addr));
			WREG32(mmDCORE0_DDMA_CORE_CTX_WR_COMP_WDATA + dcore_offset,
					comp_val);

			WREG32(mmDCORE0_DDMA_CORE_CTX_DST_TSIZE_0 + dcore_offset,
					chunk_size);
			WREG32(mmDCORE0_DDMA_CORE_CTX_COMMIT + dcore_offset, ddma_commit);

			dma_num++;

			cur_addr += chunk_size;

			if (cur_addr == prop->dram_end_address)
				goto poll;
		}
poll:
		rc = hl_poll_timeout(hdev, sob_addr, busy, (busy == dma_num), 1000, 1000000);

		if (rc) {
			dev_err(hdev->dev, "DMA Timeout during DRAM scrubbing\n");
			goto end;
		}
	}
end:
	for (dcore = 0 ; dcore < NUM_OF_DCORES ; dcore++)
		WREG32(mmDCORE0_DDMA_CORE_CTX_AXUSER_HB_SEC + dcore * DCORE_OFFSET, old_asid_mmupb);

	return rc;
}

int greco_scrub_device_mem(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 val = hdev->memory_scrub_val;
	u64 addr, size;
	int rc;

	/* TODO: remove once func-sim supports hard reset without KDMA */
	if (!hdev->pdev && hdev->reset_info.in_reset)
		return 0;

	if (!hdev->memory_scrub)
		return 0;

	/* scrub SRAM */
	addr = prop->sram_user_base_address;
	size = hdev->pldm ? 0x10000 : prop->sram_size - SRAM_USER_BASE_OFFSET;
	dev_dbg(hdev->dev, "Scrubbing SRAM: 0x%09llx - 0x%09llx, val: 0x%llx",
			addr, addr + size, val);
	rc = greco_memset_device_memory(hdev, addr, size, val, 0);
	if (rc) {
		dev_err(hdev->dev, "scrubbing SRAM failed (%d)\n", rc);
		return rc;
	}

	/* scrub DRAM */
	rc = greco_scrub_device_dram(hdev, val);
	if (rc) {
		dev_err(hdev->dev, "scrubbing DRAM failed (%d)\n", rc);
		return rc;
	}
	return 0;
}

int greco_send_cpu_message(struct hl_device *hdev, u32 *msg, u16 len,
				u32 timeout, u64 *result)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_CPU_Q)) {
		if (result)
			*result = 0;
		return 0;
	}

	if (!timeout)
		timeout = GRECO_MSG_TO_CPU_TIMEOUT_USEC;

	return hl_fw_send_cpu_message(hdev, GRECO_QUEUE_ID_CPU_PQ, msg, len,
						timeout, result);
}

static void greco_qman_set_test_mode(struct hl_device *hdev,
						u32 hw_queue_id, bool enable)
{
	u32 reg_base = greco_qm_blocks_bases[hw_queue_id];

	if (enable) {
		WREG32(reg_base + QM_GLBL_PROT_OFFSET,
				QMAN_MAKE_TRUSTED_TEST_MODE);
		WREG32(reg_base + QM_PQC_CFG_OFFSET, 0);
	} else {
		WREG32(reg_base + QM_GLBL_PROT_OFFSET, QMAN_MAKE_TRUSTED);
		WREG32(reg_base + QM_PQC_CFG_OFFSET,
				1 << DCORE0_PDMA0_QM_PQC_CFG_EN_SHIFT);
	}
}

static int greco_test_queue(struct hl_device *hdev, u32 hw_queue_id)
{
	struct packet_msg_short *msg_short_pkt;
	dma_addr_t pkt_dma_addr;
	u32 timeout_usec, tmp, sob_base = 1, sob_val = 0x5a5a;
	u32 sob_offset = GRECO_FIRST_AVAILABLE_SYNC_OBJECT * 4;
	u32 sob_addr = mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0 + sob_offset;
	size_t pkt_size;
	int rc;

	dev_dbg(hdev->dev, "Testing H/W queue %d\n", hw_queue_id);

	if (hdev->pldm)
		timeout_usec = GRECO_PLDM_TEST_QUEUE_WAIT_USEC;
	else
		timeout_usec = GRECO_TEST_QUEUE_WAIT_USEC;

	pkt_size = sizeof(*msg_short_pkt);
	msg_short_pkt = hl_asic_dma_pool_zalloc(hdev, pkt_size, GFP_KERNEL, &pkt_dma_addr);
	if (!msg_short_pkt) {
		dev_err(hdev->dev,
			"Failed to allocate packet for H/W queue %d testing\n",
			hw_queue_id);
		return -ENOMEM;
	}

	tmp = (PACKET_MSG_SHORT << GRECO_PKT_CTL_OPCODE_SHIFT) |
		(1 << GRECO_PKT_CTL_EB_SHIFT) |
		(1 << GRECO_PKT_CTL_MB_SHIFT) |
		(sob_base << GRECO_PKT_SHORT_CTL_BASE_SHIFT) |
		(sob_offset << GRECO_PKT_SHORT_CTL_ADDR_SHIFT);

	msg_short_pkt->value = cpu_to_le32(sob_val);
	msg_short_pkt->ctl = cpu_to_le32(tmp);

	/* Reset the SOB value */
	WREG32(sob_addr, 0);

	rc = hl_hw_queue_send_cb_no_cmpl(hdev, hw_queue_id, pkt_size, pkt_dma_addr);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to send msg_short packet to H/W queue %d\n",
			hw_queue_id);
		goto free_pkt;
	}

	rc = hl_poll_timeout(
			hdev,
			sob_addr,
			tmp,
			(tmp == sob_val),
			1000,
			timeout_usec);

	if (rc == -ETIMEDOUT) {
		dev_err(hdev->dev,
			"H/W queue %d test failed (SOB_OBJ_0 == 0x%x)\n",
			hw_queue_id, tmp);
		rc = -EIO;
	}

	/* Reset the SOB value */
	WREG32(sob_addr, 0);

free_pkt:
	hl_asic_dma_pool_free(hdev, (void *) msg_short_pkt, pkt_dma_addr);
	return rc;
}

static int greco_test_cpu_queue(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	/*
	 * check capability here as send_cpu_message() won't update the result
	 * value if no capability
	 */
	if (!(greco->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	return hl_fw_test_cpu_queue(hdev);
}

int greco_test_queues(struct hl_device *hdev)
{
	int i, rc, ret_val = 0;
	u32 dcore_id, first_queue, total_queues = NUMBER_OF_DCORE_HW_QUEUES;

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++) {
		first_queue = GRECO_QUEUE_ID_DCORE0_PDMA_0_0 +
				dcore_id * DCORE_QUEUE_ID_OFFSET;

		for (i = first_queue; i < first_queue + total_queues ; i++) {
			if (!greco_is_queue_enabled(hdev, i))
				continue;

			greco_qman_set_test_mode(hdev, i, true);

			rc = greco_test_queue(hdev, i);
			if (rc)
				ret_val = -EINVAL;

			greco_qman_set_test_mode(hdev, i, false);
		}
	}

	rc = greco_test_cpu_queue(hdev);
	if (rc)
		ret_val = -EINVAL;

	rc = greco_test_kdma_access(hdev);
	if (rc)
		ret_val = -EINVAL;

	return ret_val;
}

static int greco_test_kdma_access(struct hl_device *hdev)
{
	u64 regs_address, regs_dma_address;
	dma_addr_t host_mem_dma_addr;
	u32 *host_mem_virtual_addr;
	u32 val = 0x3dea; /* Only 15 bits are valid in SOB regs */
	uint i;
	int rc, ret_val = 0, num_regs = 4;

	/* Setup host memory for the test */
	host_mem_virtual_addr = hl_asic_dma_pool_zalloc(hdev, num_regs * sizeof(u32), GFP_KERNEL,
								&host_mem_dma_addr);
	if (host_mem_virtual_addr == NULL) {
		dev_err(hdev->dev, "Failed to allocate memory for KDMA test\n");
		return -ENOMEM;
	}

	/* Get registers that are safe to manipulate for the test */
	regs_address = mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0 +
			   GRECO_FIRST_AVAILABLE_SYNC_OBJECT * 4;
	regs_dma_address = CFG_BASE + regs_address;

	/* Host -> Regs */

	dev_dbg(hdev->dev, "Testing KDMA Host->Registers access\n");

	/* Prepare data */
	for (i = 0 ; i < num_regs ; i++) {
		host_mem_virtual_addr[i] = val;
		WREG32(regs_address + i * sizeof(u32), 0);
	}
	/* Flush writes */
	RREG32(regs_address);

	rc = greco_send_job_to_kdma(hdev,
				    host_mem_dma_addr,
				    regs_dma_address,
				    num_regs * sizeof(u32), 0, false);
	if (rc) {
		dev_err(hdev->dev, "KDMA Host->Registers test failed\n");
		ret_val = rc;
	} else {
		/* Verify data had been DMAed */
		for (i = 0 ; i < num_regs * sizeof(u32) ; i += sizeof(u32)) {
			if (RREG32(regs_address + i) != val) {
				dev_err(hdev->dev, "KDMA Host->Registers test, data validation failed\n");
				ret_val = -EIO;
				break;
			}
		}
	}

	/* Regs -> HOST */

	dev_dbg(hdev->dev, "Testing KDMA Registers->Host access\n");

	for (i = 0 ; i < num_regs ; i++) {
		host_mem_virtual_addr[i] = 0;
		WREG32(regs_address + i * sizeof(u32), val);
	}
	/* Flush writes */
	RREG32(regs_address);

	rc = greco_send_job_to_kdma(hdev, regs_dma_address,
				    host_mem_dma_addr,
				    num_regs * sizeof(u32), 0, false);
	if (rc) {
		dev_err(hdev->dev, "KDMA Registers->Host test failed\n");
		ret_val = rc;
	} else {
		for (i = 0 ; i < num_regs ; i++) {
			if (host_mem_virtual_addr[i] != val) {
				dev_err(hdev->dev, "KDMA test Registers->Host, data validation failed\n");
				ret_val = -EIO;
				break;
			}
		}
	}

	/* Free memory */
	hl_asic_dma_pool_free(hdev, host_mem_virtual_addr, host_mem_dma_addr);
	return ret_val;
}

static void *greco_dma_pool_zalloc(struct hl_device *hdev, size_t size,
					gfp_t mem_flags, dma_addr_t *dma_handle)
{
	void *kernel_addr;

	if (size > GRECO_DMA_POOL_BLK_SIZE)
		return NULL;

	kernel_addr = dma_pool_zalloc(hdev->dma_pool, mem_flags, dma_handle);

	/* Shift to the device's base physical address of host memory */
	if (kernel_addr)
		*dma_handle += HOST_PHYS_BASE;

	return kernel_addr;
}

static void greco_dma_pool_free(struct hl_device *hdev, void *vaddr,
				dma_addr_t dma_addr)
{
	/* Cancel the device's base physical address of host memory */
	dma_addr_t fixed_dma_addr = dma_addr - HOST_PHYS_BASE;

	dma_pool_free(hdev->dma_pool, vaddr, fixed_dma_addr);
}

void *greco_cpu_accessible_dma_pool_alloc(struct hl_device *hdev, size_t size,
						dma_addr_t *dma_handle)
{
	return hl_fw_cpu_accessible_dma_pool_alloc(hdev, size, dma_handle);
}

void greco_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size,
					void *vaddr)
{
	hl_fw_cpu_accessible_dma_pool_free(hdev, size, vaddr);
}

u32 greco_get_dma_desc_list_size(struct hl_device *hdev,
					struct sg_table *sgt)
{
	struct scatterlist *sg, *sg_next_iter;
	u32 count, dma_desc_cnt;
	u64 len, len_next;
	dma_addr_t addr, addr_next;

	dma_desc_cnt = 0;

	for_each_sgtable_dma_sg(sgt, sg, count) {

		len = sg_dma_len(sg);
		addr = sg_dma_address(sg);

		if (len == 0)
			break;

		dev_dbg_once(hdev->dev,
			"SG no. %d, addr 0x%llx, size %llu\n",
			count + 1, addr, len);

		while ((count + 1) < sgt->nents) {
			sg_next_iter = sg_next(sg);
			len_next = sg_dma_len(sg_next_iter);
			addr_next = sg_dma_address(sg_next_iter);

			if (len_next == 0)
				break;

			if ((addr + len == addr_next) &&
				(len + len_next <= DMA_MAX_TRANSFER_SIZE)) {
				len += len_next;
				count++;
				sg = sg_next_iter;
			} else {
				break;
			}
		}

		dma_desc_cnt++;
	}

	dev_dbg(hdev->dev,
		"DMA descriptors required for patched CB == %d\n",
		dma_desc_cnt);

	return dma_desc_cnt * sizeof(struct packet_lin_dma);
}

static int greco_pin_memory_before_cs(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt,
				u64 addr, enum dma_data_direction dir)
{
	struct hl_userptr *userptr;
	int rc;

	if (hl_userptr_is_pinned(hdev, addr, le32_to_cpu(user_dma_pkt->tsize),
			parser->job_userptr_list, &userptr)) {
		dev_dbg(hdev->dev, "Userptr 0x%llx + 0x%x already mapped\n",
				addr, le32_to_cpu(user_dma_pkt->tsize));
		goto already_pinned;
	}

	userptr = kzalloc(sizeof(*userptr), GFP_KERNEL);
	if (!userptr)
		return -ENOMEM;

	rc = hl_pin_host_memory(hdev, addr, le32_to_cpu(user_dma_pkt->tsize),
				userptr);
	if (rc)
		goto free_userptr;

	list_add_tail(&userptr->job_node, parser->job_userptr_list);

	rc = hdev->asic_funcs->asic_dma_map_sgtable(hdev, userptr->sgt, dir);
	if (rc) {
		dev_err(hdev->dev, "failed to map sgt with DMA region\n");
		goto unpin_memory;
	}

	userptr->dma_mapped = true;
	userptr->dir = dir;

	dev_dbg(hdev->dev,
		"JOB %d.%llu.%d, 1st DMA address 0x%llx\n",
		parser->ctx_id, parser->cs_sequence, parser->job_id,
		userptr->sgt->sgl->dma_address);

already_pinned:
	parser->patched_cb_size +=
			greco_get_dma_desc_list_size(hdev, userptr->sgt);

	return 0;

unpin_memory:
	list_del(&userptr->job_node);
	hl_unpin_host_memory(hdev, userptr);
free_userptr:
	kfree(userptr);
	return rc;
}

static int greco_validate_dma_pkt_host(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt,
				bool src_in_host)
{
	enum dma_data_direction dir;
	bool skip_host_mem_pin = false, user_memset;
	u64 addr;
	int rc = 0;

	user_memset = (le32_to_cpu(user_dma_pkt->ctl) &
			GRECO_PKT_LIN_DMA_CTL_MEMSET_MASK) >>
			GRECO_PKT_LIN_DMA_CTL_MEMSET_SHIFT;

	if (src_in_host) {
		if (user_memset)
			skip_host_mem_pin = true;

		dev_dbg(hdev->dev, "DMA direction is HOST --> DEVICE\n");
		dir = DMA_TO_DEVICE;
		addr = le64_to_cpu(user_dma_pkt->src_addr);
	} else {
		dev_dbg(hdev->dev, "DMA direction is DEVICE --> HOST\n");
		dir = DMA_FROM_DEVICE;
		addr = (le64_to_cpu(user_dma_pkt->dst_addr) &
				GRECO_PKT_LIN_DMA_DST_ADDR_MASK) >>
				GRECO_PKT_LIN_DMA_DST_ADDR_SHIFT;
	}

	if (skip_host_mem_pin)
		parser->patched_cb_size += sizeof(*user_dma_pkt);
	else
		rc = greco_pin_memory_before_cs(hdev, parser, user_dma_pkt,
						addr, dir);

	return rc;
}

static int greco_validate_dma_pkt_no_mmu(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt)
{
	bool src_in_host = false;
	u64 dst_addr = (le64_to_cpu(user_dma_pkt->dst_addr) &
			GRECO_PKT_LIN_DMA_DST_ADDR_MASK) >>
			GRECO_PKT_LIN_DMA_DST_ADDR_SHIFT;

	dev_dbg(hdev->dev, "DMA packet details:\n");
	dev_dbg(hdev->dev, "source == 0x%llx\n",
				le64_to_cpu(user_dma_pkt->src_addr));
	dev_dbg(hdev->dev, "destination == 0x%llx\n", dst_addr);
	dev_dbg(hdev->dev, "size == %u\n", le32_to_cpu(user_dma_pkt->tsize));

	/*
	 * Special handling for DMA with size 0. Bypass all validations
	 * because no transactions will be done except for WR_COMP, which
	 * is not a security issue
	 */
	if (!le32_to_cpu(user_dma_pkt->tsize)) {
		dev_dbg(hdev->dev, "Got DMA with size 0\n");
		parser->patched_cb_size += sizeof(*user_dma_pkt);
		return 0;
	}

	/* If we are here, then hw_queue_id is one of the PDMA queue IDs. We
	 * now just need to find out whether it is PDMA0 or PDMA1, to know
	 * whether the user wants to do downstream or upstream DMA transfer,
	 * respectively
	 */
	if ((parser->hw_queue_id <= GRECO_QUEUE_ID_DCORE0_PDMA_0_3) ||
		((parser->hw_queue_id >= GRECO_QUEUE_ID_DCORE1_PDMA_0_0) &&
		(parser->hw_queue_id <= GRECO_QUEUE_ID_DCORE1_PDMA_0_3)))
		src_in_host = true;

	return greco_validate_dma_pkt_host(hdev, parser, user_dma_pkt,
						src_in_host);
}

static int greco_validate_cb(struct hl_device *hdev,
				struct hl_cs_parser *parser)
{
	u32 cb_parsed_length = 0;
	int rc = 0;

	parser->patched_cb_size = 0;

	/* cb_user_size is more than 0 so loop will always be executed */
	while (cb_parsed_length < parser->user_cb_size) {
		enum packet_id pkt_id;
		u16 pkt_size;
		struct greco_packet *user_pkt;

		user_pkt = (struct greco_packet *) (uintptr_t)
			(parser->user_cb->kernel_address + cb_parsed_length);

		pkt_id = (enum packet_id) (
				(le64_to_cpu(user_pkt->header) &
				PACKET_HEADER_PACKET_ID_MASK) >>
					PACKET_HEADER_PACKET_ID_SHIFT);

		if (!validate_packet_id(pkt_id)) {
			dev_err(hdev->dev, "Invalid packet id %u\n", pkt_id);
			rc = -EINVAL;
			break;
		}

		dev_dbg_ratelimited(hdev->dev, "Detected packet ID 0x%x\n",
					pkt_id);

		pkt_size = greco_packet_sizes[pkt_id];
		cb_parsed_length += pkt_size;
		if (cb_parsed_length > parser->user_cb_size) {
			dev_err(hdev->dev,
				"packet 0x%x is out of CB boundary\n", pkt_id);
			rc = -EINVAL;
			break;
		}

		switch (pkt_id) {
		case PACKET_MSG_PROT:
			dev_err(hdev->dev,
				"User not allowed to use MSG_PROT\n");
			rc = -EPERM;
			break;

		case PACKET_STOP:
			dev_err(hdev->dev, "User not allowed to use STOP\n");
			rc = -EPERM;
			break;

		case PACKET_LIN_DMA:
			rc = greco_validate_dma_pkt_no_mmu(hdev, parser,
					(struct packet_lin_dma *) user_pkt);
			break;

		case PACKET_WREG_32:
		case PACKET_WREG_BULK:
		case PACKET_MSG_LONG:
		case PACKET_MSG_SHORT:
		case PACKET_CP_DMA:
		case PACKET_REPEAT:
		case PACKET_FENCE:
		case PACKET_NOP:
		case PACKET_ARB_POINT:
		case PACKET_WAIT:
		case PACKET_CB_LIST:
		case PACKET_LOAD_AND_EXE:
			parser->patched_cb_size += pkt_size;
			break;

		default:
			dev_err(hdev->dev, "Invalid packet header 0x%x\n",
				pkt_id);
			rc = -EINVAL;
			break;
		}

		if (rc)
			break;
	}

	return rc;
}

static int greco_patch_dma_packet(struct hl_device *hdev,
				struct hl_cs_parser *parser,
				struct packet_lin_dma *user_dma_pkt,
				struct packet_lin_dma *new_dma_pkt,
				u32 *new_dma_pkt_size)
{
	struct hl_userptr *userptr;
	struct scatterlist *sg, *sg_next_iter;
	u32 count, dma_desc_cnt, user_wrcomp_mask, ctl;
	u64 len, len_next;
	dma_addr_t dma_addr, dma_addr_next;
	u64 device_memory_addr, addr;
	enum dma_data_direction dir;
	struct sg_table *sgt;
	bool src_in_host = false;
	bool skip_host_mem_pin = false;
	bool user_memset;

	ctl = le32_to_cpu(user_dma_pkt->ctl);

	/* If we are here, then hw_queue_id is one of the PDMA queue IDs. We
	 * now just need to find out whether it is PDMA0 or PDMA1, to know
	 * whether the user wants to do downstream or upstream DMA transfer,
	 * respectively
	 */
	if ((parser->hw_queue_id <= GRECO_QUEUE_ID_DCORE0_PDMA_0_3) ||
		((parser->hw_queue_id >= GRECO_QUEUE_ID_DCORE1_PDMA_0_0) &&
		(parser->hw_queue_id <= GRECO_QUEUE_ID_DCORE1_PDMA_0_3)))
		src_in_host = true;

	user_memset = (ctl & GRECO_PKT_LIN_DMA_CTL_MEMSET_MASK) >>
			GRECO_PKT_LIN_DMA_CTL_MEMSET_SHIFT;

	if (src_in_host) {
		addr = le64_to_cpu(user_dma_pkt->src_addr);
		device_memory_addr = le64_to_cpu(user_dma_pkt->dst_addr);
		dir = DMA_TO_DEVICE;
		if (user_memset)
			skip_host_mem_pin = true;
	} else {
		addr = le64_to_cpu(user_dma_pkt->dst_addr);
		device_memory_addr = le64_to_cpu(user_dma_pkt->src_addr);
		dir = DMA_FROM_DEVICE;
	}

	if ((!skip_host_mem_pin) &&
		(!hl_userptr_is_pinned(hdev, addr,
					le32_to_cpu(user_dma_pkt->tsize),
					parser->job_userptr_list, &userptr))) {
		dev_err(hdev->dev, "Userptr 0x%llx + 0x%x NOT mapped\n",
				addr, user_dma_pkt->tsize);
		return -EFAULT;
	}

	if ((user_memset) && (dir == DMA_TO_DEVICE)) {
		memcpy(new_dma_pkt, user_dma_pkt, sizeof(*user_dma_pkt));
		*new_dma_pkt_size = sizeof(*user_dma_pkt);
		return 0;
	}

	user_wrcomp_mask = ctl & GRECO_PKT_LIN_DMA_CTL_WRCOMP_MASK;

	sgt = userptr->sgt;
	dma_desc_cnt = 0;

	for_each_sgtable_dma_sg(sgt, sg, count) {
		len = sg_dma_len(sg);
		dma_addr = sg_dma_address(sg);

		if (len == 0)
			break;

		dev_dbg_once(hdev->dev,
			"SG no. %d, addr 0x%llx, size %llu\n",
			count + 1, dma_addr, len);

		while ((count + 1) < sgt->nents) {
			sg_next_iter = sg_next(sg);
			len_next = sg_dma_len(sg_next_iter);
			dma_addr_next = sg_dma_address(sg_next_iter);

			if (len_next == 0)
				break;

			if ((dma_addr + len == dma_addr_next) &&
				(len + len_next <= DMA_MAX_TRANSFER_SIZE)) {
				len += len_next;
				count++;
				sg = sg_next_iter;
			} else {
				break;
			}
		}

		new_dma_pkt->ctl = user_dma_pkt->ctl;

		ctl = le32_to_cpu(user_dma_pkt->ctl);
		if (likely(dma_desc_cnt))
			ctl &= ~GRECO_PKT_CTL_EB_MASK;
		ctl &= ~GRECO_PKT_LIN_DMA_CTL_WRCOMP_MASK;
		new_dma_pkt->ctl = cpu_to_le32(ctl);
		new_dma_pkt->tsize = cpu_to_le32(len);

		if (dir == DMA_TO_DEVICE) {
			new_dma_pkt->src_addr = cpu_to_le64(dma_addr);
			new_dma_pkt->dst_addr = cpu_to_le64(device_memory_addr);
		} else {
			new_dma_pkt->src_addr = cpu_to_le64(device_memory_addr);
			new_dma_pkt->dst_addr = cpu_to_le64(dma_addr);
		}

		if (!user_memset)
			device_memory_addr += len;
		dma_desc_cnt++;
		new_dma_pkt++;
	}

	if (!dma_desc_cnt) {
		dev_err(hdev->dev,
			"Error of 0 SG entries when patching DMA packet\n");
		return -EFAULT;
	}

	/* Fix the last dma packet - wrcomp must be as user set it */
	new_dma_pkt--;
	new_dma_pkt->ctl |= cpu_to_le32(user_wrcomp_mask);

	*new_dma_pkt_size = dma_desc_cnt * sizeof(struct packet_lin_dma);

	return 0;
}

static int greco_patch_cb(struct hl_device *hdev,
				struct hl_cs_parser *parser)
{
	u32 cb_parsed_length = 0;
	u32 cb_patched_cur_length = 0;
	int rc = 0;

	/* cb_user_size is more than 0 so loop will always be executed */
	while (cb_parsed_length < parser->user_cb_size) {
		enum packet_id pkt_id;
		u16 pkt_size;
		u32 new_pkt_size = 0;
		struct greco_packet *user_pkt, *kernel_pkt;

		user_pkt = (struct greco_packet *) (uintptr_t)
			(parser->user_cb->kernel_address + cb_parsed_length);
		kernel_pkt = (struct greco_packet *) (uintptr_t)
			(parser->patched_cb->kernel_address +
					cb_patched_cur_length);

		pkt_id = (enum packet_id) (
				(le64_to_cpu(user_pkt->header) &
				PACKET_HEADER_PACKET_ID_MASK) >>
					PACKET_HEADER_PACKET_ID_SHIFT);

		if (!validate_packet_id(pkt_id)) {
			dev_err(hdev->dev, "Invalid packet id %u\n", pkt_id);
			rc = -EINVAL;
			break;
		}

		pkt_size = greco_packet_sizes[pkt_id];
		cb_parsed_length += pkt_size;
		if (cb_parsed_length > parser->user_cb_size) {
			dev_err(hdev->dev,
				"packet 0x%x is out of CB boundary\n", pkt_id);
			rc = -EINVAL;
			break;
		}

		switch (pkt_id) {
		case PACKET_LIN_DMA:
			rc = greco_patch_dma_packet(hdev, parser,
					(struct packet_lin_dma *) user_pkt,
					(struct packet_lin_dma *) kernel_pkt,
					&new_pkt_size);
			cb_patched_cur_length += new_pkt_size;
			break;

		case PACKET_MSG_PROT:
			dev_err(hdev->dev,
				"User not allowed to use MSG_PROT\n");
			rc = -EPERM;
			break;


		case PACKET_STOP:
			dev_err(hdev->dev, "User not allowed to use STOP\n");
			rc = -EPERM;
			break;

		case PACKET_WREG_32:
		case PACKET_WREG_BULK:
		case PACKET_MSG_LONG:
		case PACKET_MSG_SHORT:
		case PACKET_CP_DMA:
		case PACKET_REPEAT:
		case PACKET_FENCE:
		case PACKET_NOP:
		case PACKET_ARB_POINT:
		case PACKET_WAIT:
		case PACKET_CB_LIST:
		case PACKET_LOAD_AND_EXE:
			dev_dbg_ratelimited(hdev->dev,
					"Copying packet ID 0x%x\n", pkt_id);
			memcpy(kernel_pkt, user_pkt, pkt_size);
			cb_patched_cur_length += pkt_size;
			break;

		default:
			dev_err(hdev->dev, "Invalid packet header 0x%x\n",
				pkt_id);
			rc = -EINVAL;
			break;
		}

		if (rc)
			break;
	}

	return rc;
}

static int greco_parse_cb_no_mmu(struct hl_device *hdev,
				struct hl_cs_parser *parser)
{
	u64 handle;
	int rc;

	rc = greco_validate_cb(hdev, parser);

	if (rc)
		goto free_userptr;

	dev_dbg(hdev->dev, "Preparing patched CB for JOB %d.%llu.%d\n",
		parser->ctx_id, parser->cs_sequence, parser->job_id);

	rc = hl_cb_create(hdev, &hdev->kernel_mem_mgr, hdev->kernel_ctx,
				parser->patched_cb_size, false, false,
				&handle);
	if (rc)
		goto free_userptr;

	parser->patched_cb = hl_cb_get(&hdev->kernel_mem_mgr, handle);
	/* hl_cb_get should never fail here */
	if (!parser->patched_cb) {
		rc = -EFAULT;
		goto out;
	}

	rc = greco_patch_cb(hdev, parser);

	if (rc)
		hl_cb_put(parser->patched_cb);

out:
	/*
	 * Always call cb destroy here because we still have 1 reference
	 * to it by calling cb_get earlier. After the job will be completed,
	 * cb_put will release it, but here we want to remove it from the
	 * idr
	 */
	hl_cb_destroy(&hdev->kernel_mem_mgr, handle);

free_userptr:
	if (rc)
		hl_userptr_delete_list(hdev, parser->job_userptr_list);
	return rc;
}

static int greco_validate_cb_address(struct hl_device *hdev,
					struct hl_cs_parser *parser)
{
	struct asic_fixed_properties *asic_prop = &hdev->asic_prop;
	struct greco_device *greco = hdev->asic_specific;

	if (!greco_is_queue_enabled(hdev, parser->hw_queue_id)) {
		dev_err(hdev->dev, "h/w queue %d is disabled\n",
			parser->hw_queue_id);
		return -EINVAL;
	}

	/* Just check if CB address is valid */

	if (hl_mem_area_inside_range((u64) (uintptr_t) parser->user_cb,
					parser->user_cb_size,
					asic_prop->sram_user_base_address,
					asic_prop->sram_end_address))
		return 0;

	if (hl_mem_area_inside_range((u64) (uintptr_t) parser->user_cb,
					parser->user_cb_size,
					asic_prop->dram_user_base_address,
					asic_prop->dram_end_address))
		return 0;

	if ((greco->hw_cap_initialized & HW_CAP_DMMU_MASK) &&
		hl_mem_area_inside_range((u64) (uintptr_t) parser->user_cb,
						parser->user_cb_size,
						asic_prop->dmmu.start_addr,
						asic_prop->dmmu.end_addr))
		return 0;

	if ((greco->hw_cap_initialized & HW_CAP_PMMU) &&
		(hl_mem_area_inside_range((u64) (uintptr_t) parser->user_cb,
					parser->user_cb_size,
					asic_prop->pmmu.start_addr,
					asic_prop->pmmu.end_addr) ||
		hl_mem_area_inside_range((u64) (uintptr_t) parser->user_cb,
					parser->user_cb_size,
					asic_prop->pmmu_huge.start_addr,
					asic_prop->pmmu_huge.end_addr)))
		return 0;

	dev_err(hdev->dev,
		"CB address 0x%px + 0x%x for internal QMAN is not valid\n",
		parser->user_cb, parser->user_cb_size);

	return -EFAULT;
}

int greco_cs_parser(struct hl_device *hdev, struct hl_cs_parser *parser)
{
	struct greco_device *greco = hdev->asic_specific;

	dev_dbg(hdev->dev, "Scanning CB for JOB %d.%llu.%d\n",
		parser->ctx_id, parser->cs_sequence, parser->job_id);

	if (!parser->is_kernel_allocated_cb)
		return greco_validate_cb_address(hdev, parser);

	if (!(greco->hw_cap_initialized & HW_CAP_PMMU))
		return greco_parse_cb_no_mmu(hdev, parser);

	return 0;
}

int greco_send_heartbeat(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	return hl_fw_send_heartbeat(hdev);
}

/* This is an internal helper function, used to update the KDMA mmu props.
 * Should be called with a proper kdma lock.
 */
static void greco_kdma_set_mmbp_asid(struct hl_device *hdev,
					   bool mmu_bypass, u32 asid)
{
	u32 hb_sec, _mmu_bypass;

	/* Sparse static code analysis will complain if not using intermediate
	 * variable - it is a false positive.
	 */
	_mmu_bypass = !!mmu_bypass;

	hb_sec = RREG32(mmDCORE0_KDMA_CORE_CTX_AXUSER_HB_SEC);
	WREG32(mmDCORE0_KDMA_CORE_CTX_AXUSER_HB_SEC,
		(hb_sec & ~MMUBP_ASID_MASK) |
		FIELD_PREP(AXUSER_HB_SEC_ASID_MASK, asid) |
		FIELD_PREP(AXUSER_HB_SEC_MMBP_MASK, _mmu_bypass));
}

/* This is an internal helper function used by greco_send_job_to_kdma only */
static int greco_send_job_to_kdma(struct hl_device *hdev, u64 src_addr,
				u64 dst_addr, u32 size, u32 dcore_id,
				bool is_memset)
{
	u32 timeout, comp_val, commit_mask, sob_addr, sob_val,
			dcore_offset = dcore_id * DCORE_OFFSET;
	u64 comp_addr;
	int rc;

	sob_addr = mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0 + (GRECO_RSRVD_KDMA_SOB_IDX * 4);

	WREG32(sob_addr, 0);

	comp_addr = CFG_BASE + sob_addr;

	comp_val = FIELD_PREP(DCORE0_SYNC_MNGR_OBJS_SOB_OBJ_INC_MASK, 1) |
			FIELD_PREP(DCORE0_SYNC_MNGR_OBJS_SOB_OBJ_VAL_MASK, 1);

	WREG32(mmDCORE0_KDMA_CORE_CTX_SRC_BASE_LO + dcore_offset, lower_32_bits(src_addr));
	WREG32(mmDCORE0_KDMA_CORE_CTX_SRC_BASE_HI + dcore_offset, upper_32_bits(src_addr));
	WREG32(mmDCORE0_KDMA_CORE_CTX_DST_BASE_LO + dcore_offset, lower_32_bits(dst_addr));
	WREG32(mmDCORE0_KDMA_CORE_CTX_DST_BASE_HI + dcore_offset, upper_32_bits(dst_addr));
	WREG32(mmDCORE0_KDMA_CORE_CTX_WR_COMP_ADDR_LO, lower_32_bits(comp_addr));
	WREG32(mmDCORE0_KDMA_CORE_CTX_WR_COMP_ADDR_HI, upper_32_bits(comp_addr));
	WREG32(mmDCORE0_KDMA_CORE_CTX_WR_COMP_WDATA, comp_val);
	WREG32(mmDCORE0_KDMA_CORE_CTX_DST_TSIZE_0 + dcore_offset, size);

	commit_mask = FIELD_PREP(DCORE0_KDMA_CORE_CTX_COMMIT_LIN_MASK, 1) |
			FIELD_PREP(DCORE0_KDMA_CORE_CTX_COMMIT_WR_COMP_EN_MASK, 1);

	if (is_memset)
		commit_mask |= FIELD_PREP(DCORE0_KDMA_CORE_CTX_COMMIT_MEM_SET_MASK, 1);

	WREG32(mmDCORE0_KDMA_CORE_CTX_COMMIT + dcore_offset, commit_mask);

	if (hdev->pldm)
		/* for each 1MB 1 second of timeout */
		timeout = ((size / SZ_1M) + 1) * USEC_PER_SEC;
	else
		timeout = KDMA_TIMEOUT_USEC;

	rc = hl_poll_timeout(
			hdev,
			sob_addr,
			sob_val,
			sob_val,
			1000,
			timeout);
	if (rc) {
		dev_err(hdev->dev, "Timeout while waiting for KDMA to be idle\n");
		return rc;
	}

	return 0;
}

int greco_compute_reset_late_init(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	/*
	 * Unmask all IRQs since some could have been received
	 * during the soft reset
	 */
	return hl_fw_unmask_irq_arr(hdev, greco->events,
			greco->num_of_valid_events * sizeof(greco->events[0]));
}

static bool greco_is_device_idle(struct hl_device *hdev, u64 *mask_arr, u8 mask_len,
					struct engines_data *e)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	const char *dma_fmt = "%-6d%-6d%-9s%#-14x%#-12s%#x\n";
	const char *tpc_fmt = "%-6d%-5d%-9s%#-14x%#-12x%#-16x%#-15x%#x\n";
	const char *mme_fmt = "%-6d%-5d%-9s%#-14x%#-12s%#x\n";
	const char *mme_slave_fmt = "%-6d%-5d%-9s%-14s%-12s%#x\n";
	const char *dec_fmt = "%-6d%-5d%-9s%#x\n";
	const char *rot_fmt = "%-6d%-5d%-9s%#-14x%#x\n";
	const char *enc_fmt = "%-6s%-5d%-9s%#x\n";
	unsigned long *mask = (unsigned long *)mask_arr;
	u64 offset, tpc_enabled_bit;
	u32 qm_glbl_sts0, qm_cgm_sts, dma_core_idle_ind_mask, tpc_cfg_sts,
		mme_arch_sts, dec_swreg15, dec_enabled_bit, rot_enabled_bit,
		enc_swreg1, tpc_tsb_occupancy, tpc_tsb_inflight_cntr;
	bool is_idle = true, is_eng_idle;
	bool encoder_enabled = !!(hdev->asic_prop.decoder_enabled_mask & 0x3E0);
	int engine_idx, i, j;

	/* DDMA, One engine per dcore */
	if (e)
		hl_engine_data_sprintf(e,
			"\nCORE  DDMA  is_idle  QM_GLBL_STS0  QM_CGM_STS  DMA_CORE_IDLE_IND_MASK\n"
			"----  ----  -------  ------------  ----------  ----------------------\n");

	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		engine_idx = GRECO_DCORE0_ENGINE_ID_DDMA +
					i * GRECO_ENGINE_ID_DCORE_OFFSET;
		offset = i * DCORE_OFFSET;

		dma_core_idle_ind_mask =
			RREG32(mmDCORE0_DDMA_CORE_IDLE_IND_MASK + offset);
		qm_glbl_sts0 = RREG32(mmDCORE0_DDMA_QM_GLBL_STS0 + offset);

		is_eng_idle = IS_DMA_QM_IDLE(qm_glbl_sts0) &&
				IS_DMA_IDLE(dma_core_idle_ind_mask);
		is_idle &= is_eng_idle;

		if (mask && !is_eng_idle)
			set_bit(engine_idx, mask);
		if (e)
			hl_engine_data_sprintf(e, dma_fmt, i, 0, is_eng_idle ? "Y" : "N",
						qm_glbl_sts0, "-", dma_core_idle_ind_mask);
	}

	/* PDMA, two engines per dcore */
	if (e)
		hl_engine_data_sprintf(e,
			"\nCORE  PDMA  is_idle  QM_GLBL_STS0  QM_CGM_STS  DMA_CORE_IDLE_IND_MASK\n"
			"----  ----  -------  ------------  ----------  ----------------------\n");

	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < NUM_OF_PDMA_PER_DCORE ; j++) {
			engine_idx = GRECO_DCORE0_ENGINE_ID_PDMA_0 +
					i * GRECO_ENGINE_ID_DCORE_OFFSET + j;
			offset = i * DCORE_OFFSET + j * DCORE_PDMA_OFFSET;

			dma_core_idle_ind_mask =
				RREG32(mmDCORE0_PDMA0_CORE_IDLE_IND_MASK +
									offset);
			qm_glbl_sts0 = RREG32(mmDCORE0_PDMA0_QM_GLBL_STS0 +
									offset);

			is_eng_idle = IS_DMA_QM_IDLE(qm_glbl_sts0) &&
					IS_DMA_IDLE(dma_core_idle_ind_mask);

			if (mask && !is_eng_idle)
				set_bit(engine_idx, mask);
			if (e)
				hl_engine_data_sprintf(e, dma_fmt, i, j,
							is_eng_idle ? "Y" : "N",
							qm_glbl_sts0,
							"-", dma_core_idle_ind_mask);
		}
	}

	/* TPC, Five engines per dcore */
	if (e && prop->tpc_enabled_mask)
		hl_engine_data_sprintf(e,
			"\nCORE  TPC  is_idle  QM_GLBL_STS0  QM_CGM_STS  TPC_CFG_STATUS  TSB_OCCUPANCY  TSB_INFLIGHT_CNTR\n"
			   "----  ---  -------  ------------  ----------  --------------  -------------  -----------------\n");

	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < NUM_OF_TPC_PER_DCORE ; j++) {
			tpc_enabled_bit = 1 << (i * NUM_OF_TPC_PER_DCORE + j);
			if (!(prop->tpc_enabled_mask & tpc_enabled_bit))
				continue;

			engine_idx = GRECO_DCORE0_ENGINE_ID_TPC_0 +
					i * GRECO_ENGINE_ID_DCORE_OFFSET + j;
			offset = i * DCORE_OFFSET + j * DCORE_TPC_OFFSET;

			tpc_cfg_sts = RREG32(mmDCORE0_TPC0_CFG_STATUS + offset);
			qm_glbl_sts0 = RREG32(mmDCORE0_TPC0_QM_GLBL_STS0 +
									offset);
			qm_cgm_sts = RREG32(mmDCORE0_TPC0_QM_CGM_STS + offset);

			tpc_tsb_occupancy =
				RREG32(mmDCORE0_TPC0_CFG_TSB_OCCUPANCY +
									offset);
			tpc_tsb_inflight_cntr =
				RREG32(mmDCORE0_TPC0_CFG_TSB_INFLIGHT_CNTR +
									offset);
			is_eng_idle = IS_QM_IDLE(qm_glbl_sts0, qm_cgm_sts) &&
					IS_TPC_IDLE(tpc_cfg_sts,
							tpc_tsb_occupancy,
							tpc_tsb_inflight_cntr);
			is_idle &= is_eng_idle;

			if (mask && !is_eng_idle)
				set_bit(engine_idx, mask);

			if (e)
				hl_engine_data_sprintf(e, tpc_fmt, i, j,
							is_eng_idle ? "Y" : "N",
							qm_glbl_sts0, qm_cgm_sts, tpc_cfg_sts,
							tpc_tsb_occupancy,
							tpc_tsb_inflight_cntr);
		}
	}

	/* MME, single engine per dcore */
	if (hdev->mme_mask) {
		if (e)
			hl_engine_data_sprintf(e,
				"\nCORE  MME  is_idle  QM_GLBL_STS0  QM_CGM_STS  MME_ARCH_STATUS\n"
				"----  ---  -------  ------------  ----------  ---------------\n");

		for (i = 0 ; i < NUM_OF_DCORES ; i++) {
			engine_idx = GRECO_DCORE0_ENGINE_ID_MME +
					i * GRECO_ENGINE_ID_DCORE_OFFSET;
			offset = i * DCORE_OFFSET;

			mme_arch_sts =
				RREG32(mmDCORE0_MME_CTRL_LO_ARCH_STATUS +
									offset);
			is_eng_idle = IS_MME_IDLE(mme_arch_sts);
			is_idle &= is_eng_idle;

			/* MME 1 is slave so no need to query its QM */
			if (i == 0) {
				qm_glbl_sts0 =
					RREG32(mmDCORE0_MME_QM_GLBL_STS0 +
									offset);
				is_eng_idle &=
					IS_MME_QM_IDLE(qm_glbl_sts0);
				is_idle &= is_eng_idle;

				if (e)
					hl_engine_data_sprintf(e, mme_fmt, i, 0,
								is_eng_idle ? "Y" : "N",
								qm_glbl_sts0,
								"-", mme_arch_sts);
			} else {
				if (e)
					hl_engine_data_sprintf(e, mme_slave_fmt, i, 0,
								is_eng_idle ? "Y" : "N",
								"-", "-", mme_arch_sts);
			}

			if (mask && !is_eng_idle)
				set_bit(engine_idx, mask);
		}
	}

	if (e && prop->decoder_enabled_mask)
		hl_engine_data_sprintf(e,
					"\nCORE  DEC  is_idle  VSI_CMD_SWREG15\n"
			   "----  ---  -------  ---------------\n");

	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		for (j = 0 ; j < NUM_OF_DEC_PER_DCORE ; j++) {
			dec_enabled_bit = 1 << (i * NUM_OF_DEC_PER_DCORE + j);
			if (!(prop->decoder_enabled_mask & dec_enabled_bit))
				continue;

			engine_idx = GRECO_DCORE0_ENGINE_ID_DEC_0 +
					i * GRECO_ENGINE_ID_DCORE_OFFSET + j;
			offset = i * DCORE_OFFSET + j * DCORE_DEC_OFFSET;

			dec_swreg15 = RREG32(mmDCORE0_DEC0_CMD_SWREG15 +
						offset);
			is_eng_idle = IS_DEC_IDLE(dec_swreg15);
			is_idle &= is_eng_idle;

			if (mask && !is_eng_idle)
				set_bit(engine_idx, mask);

			if (e)
				hl_engine_data_sprintf(e, dec_fmt, i, j,
							is_eng_idle ? "Y" : "N", dec_swreg15);
		}
	}

	if (e && hdev->rotator_mask)
		hl_engine_data_sprintf(e,
			"\nCORE  ROT  is_idle  QM_GLBL_STS0  QM_CGM_STS\n"
			   "----  ---  -------  ------------  ----------\n");

	for (i = 0 ; i < NUM_OF_DCORES ; i++) {
		rot_enabled_bit = 1 << i;
		if (!(hdev->rotator_mask & rot_enabled_bit))
			continue;

		engine_idx = GRECO_DCORE0_ENGINE_ID_ROT +
				i * GRECO_ENGINE_ID_DCORE_OFFSET;
		offset = i * DCORE_OFFSET;

		qm_glbl_sts0 = RREG32(mmDCORE0_ROT_QM_GLBL_STS0 + offset);
		qm_cgm_sts = RREG32(mmDCORE0_ROT_QM_CGM_STS + offset);

		is_eng_idle = IS_QM_IDLE(qm_glbl_sts0, qm_cgm_sts);
		is_idle &= is_eng_idle;

		if (mask && !is_eng_idle)
			set_bit(engine_idx, mask);

		if (e)
			hl_engine_data_sprintf(e, rot_fmt, i, 0,
						is_eng_idle ? "Y" : "N",
						qm_glbl_sts0, qm_cgm_sts);
	}

	if (encoder_enabled) {
		if (e)
			hl_engine_data_sprintf(e,
						"\nCORE  ENC  is_idle  VSI_VENC_SWREG1\n"
						"----  ---  -------  ---------------\n");

		enc_swreg1 = RREG32(mmVSI_VENC_SWREG1);

		is_eng_idle = IS_ENC_IDLE(enc_swreg1);
		is_idle &= is_eng_idle;

		if (mask && !is_eng_idle)
			set_bit(GRECO_ENGINE_ID_ENC, mask);

		if (e)
			hl_engine_data_sprintf(e, enc_fmt, "-", 0,
						is_eng_idle ? "Y" : "N", enc_swreg1);
	}

	return is_idle;
}

static void greco_hw_queues_lock(struct hl_device *hdev)
	__acquires(&greco->hw_queues_lock)
{
	struct greco_device *greco = hdev->asic_specific;

	spin_lock(&greco->hw_queues_lock);
}

static void greco_hw_queues_unlock(struct hl_device *hdev)
	__releases(&greco->hw_queues_lock)
{
	struct greco_device *greco = hdev->asic_specific;

	spin_unlock(&greco->hw_queues_lock);
}

int greco_nic_init(struct hl_device *hdev)
{
	return 0;
}

void greco_nic_fini(struct hl_device *hdev)
{

}

int greco_nic_control(struct hl_device *hdev, u32 op, void *input, void *output,
			struct hl_ctx *ctx)
{
	dev_err_ratelimited(hdev->dev,
			"NIC operations cannot be performed on GRECO\n");
	return -ENXIO;
}

static u32 greco_get_pci_id(struct hl_device *hdev)
{
	return hdev->pdev->device;
}

static int greco_get_eeprom_data(struct hl_device *hdev, void *data, size_t max_size)
{
	struct greco_device *greco = hdev->asic_specific;

	if (!(greco->hw_cap_initialized & HW_CAP_CPU_Q))
		return 0;

	return hl_fw_get_eeprom_data(hdev, data, max_size);
}

void greco_update_eq_ci(struct hl_device *hdev, u32 val)
{
	WREG32(mmCPU_IF_EQ_RD_OFFS, val);
}

static void greco_get_event_desc(u16 event_type, char *desc, size_t size)
{
	if (event_type >= GRECO_EVENT_SIZE)
		goto event_not_supported;

	if (!greco_irq_map_table[event_type].valid)
		goto event_not_supported;

	switch (event_type) {
	case GRECO_EVENT_HMMU_0_1:
		snprintf(desc, size, "DCORE0_HMMU0_slave_error");
		break;
	case GRECO_EVENT_HMMU_1_1:
		snprintf(desc, size, "DCORE0_HMMU1_slave_error");
		break;
	case GRECO_EVENT_HMMU_2_1:
		snprintf(desc, size, "DCORE1_HMMU0_slave_error");
		break;
	case GRECO_EVENT_HMMU_3_1:
		snprintf(desc, size, "DCORE1_HMMU1_slave_error");
		break;
	case GRECO_EVENT_PMMU_0_1:
		snprintf(desc, size, "PMMU_DEC_error");
		break;
	case GRECO_EVENT_TS_A_SOUTH_0:
		snprintf(desc, size, "Temperature_sensor_0_alert ");
		break;
	case GRECO_EVENT_TS_A_NORTH_1:
		snprintf(desc, size, "Temperature_sensor_1_alert ");
		break;
	case GRECO_EVENT_TS_A_EAST_2:
		snprintf(desc, size, "Temperature_sensor_2_alert ");
		break;
	case GRECO_EVENT_TS_A_WEST_3:
		snprintf(desc, size, "Temperature_sensor_3_alert ");
		break;
	default:
		snprintf(desc, size, greco_irq_map_table[event_type].name);
	}

	return;

event_not_supported:
	snprintf(desc, size, "N/A");
}

static void greco_handle_qman_err_generic(struct hl_device *hdev,
					  const char *qm_name,
					  u64 glbl_sts_addr,
					  u64 arb_err_addr)
{
	u32 i, j, glbl_sts_val, arb_err_val, glbl_sts_clr_val, arb_err_clr_val;
	char reg_desc[32];

	/* Iterate through all stream GLBL_ERR_STS registers + Lower CP */
	for (i = 0 ; i < QMAN_STREAMS + 1 ; i++) {
		glbl_sts_clr_val = 0;
		glbl_sts_val = RREG32(glbl_sts_addr + 4 * i);

		if (!glbl_sts_val)
			continue;

		if (i == QMAN_STREAMS)
			snprintf(reg_desc, ARRAY_SIZE(reg_desc), "LowerCP");
		else
			snprintf(reg_desc, ARRAY_SIZE(reg_desc), "stream%u", i);

		for (j = 0 ; j < GRECO_NUM_OF_QM_ERR_CAUSE ; j++) {
			if (glbl_sts_val & BIT(j)) {
				dev_err_ratelimited(hdev->dev,
						"%s %s. err cause: %s\n",
						qm_name, reg_desc,
						greco_qman_error_cause[j]);
				glbl_sts_clr_val |= BIT(j);
			}
		}
	}

	arb_err_val = RREG32(arb_err_addr);

	if (!arb_err_val)
		return;

	arb_err_clr_val = 0;
	for (j = 0 ; j < GRECO_NUM_OF_QM_ARB_ERR_CAUSE ; j++) {
		if (arb_err_val & BIT(j)) {
			dev_err_ratelimited(hdev->dev,
					"%s ARB_ERR. err cause: %s\n",
					qm_name,
					greco_qman_arb_error_cause[j]);
			arb_err_clr_val |= BIT(j);
		}
	}
}

static void greco_handle_qman_err(struct hl_device *hdev, u16 event_type)
{
	u64 glbl_sts_addr, arb_err_addr;
	u8 index;
	char desc[32];

	switch (event_type) {
	case GRECO_EVENT_TPC0_QM ... GRECO_EVENT_TPC4_QM:
		index = event_type - GRECO_EVENT_TPC0_QM;
		glbl_sts_addr =
			mmDCORE0_TPC0_QM_GLBL_ERR_STS_0 +
			index * DCORE_TPC_OFFSET;
		arb_err_addr =
			mmDCORE0_TPC0_QM_ARB_ERR_CAUSE +
			index * DCORE_TPC_OFFSET;
		snprintf(desc, ARRAY_SIZE(desc), "%s%d",
			"DCORE0_TPC_QM", index);
		break;
	case GRECO_EVENT_TPC5_QM ... GRECO_EVENT_TPC9_QM:
		index = event_type - GRECO_EVENT_TPC5_QM;
		glbl_sts_addr =
			mmDCORE1_TPC0_QM_GLBL_ERR_STS_0 +
			index * DCORE_TPC_OFFSET;
		arb_err_addr =
			mmDCORE1_TPC0_QM_ARB_ERR_CAUSE +
			index * DCORE_TPC_OFFSET;
		snprintf(desc, ARRAY_SIZE(desc), "%s%d",
			"DCORE1_TPC_QM", index);
		break;
	case GRECO_EVENT_MME0_QM:
		glbl_sts_addr = mmDCORE0_MME_QM_GLBL_ERR_STS_0;
		arb_err_addr = mmDCORE0_MME_QM_ARB_ERR_CAUSE;
		snprintf(desc, ARRAY_SIZE(desc), "DCORE0_MME_QM");
		break;

	case GRECO_EVENT_DDMA0_QM:
		glbl_sts_addr = mmDCORE0_DDMA_QM_GLBL_ERR_STS_0;
		arb_err_addr = mmDCORE0_DDMA_QM_ARB_ERR_CAUSE;
		snprintf(desc, ARRAY_SIZE(desc), "DCORE0_DDMA_QM");
		break;
	case GRECO_EVENT_DDMA1_QM:
		glbl_sts_addr = mmDCORE1_DDMA_QM_GLBL_ERR_STS_0;
		arb_err_addr = mmDCORE1_DDMA_QM_ARB_ERR_CAUSE;
		snprintf(desc, ARRAY_SIZE(desc), "DCORE1_DDMA_QM");
		break;
	case GRECO_EVENT_PDMA0_QM:
		glbl_sts_addr = mmDCORE0_PDMA0_QM_GLBL_ERR_STS_0;
		arb_err_addr = mmDCORE0_PDMA0_QM_ARB_ERR_CAUSE;
		snprintf(desc, ARRAY_SIZE(desc), "DCORE0_PDMA0_QM");
		break;
	case GRECO_EVENT_PDMA1_QM:
		glbl_sts_addr = mmDCORE0_PDMA1_QM_GLBL_ERR_STS_0;
		arb_err_addr = mmDCORE0_PDMA1_QM_ARB_ERR_CAUSE;
		snprintf(desc, ARRAY_SIZE(desc), "DCORE0_PDMA1_QM");
		break;
	case GRECO_EVENT_PDMA2_QM:
		glbl_sts_addr = mmDCORE1_PDMA0_QM_GLBL_ERR_STS_0;
		arb_err_addr = mmDCORE1_PDMA0_QM_ARB_ERR_CAUSE;
		snprintf(desc, ARRAY_SIZE(desc), "DCORE1_PDMA0_QM");
		break;
	case GRECO_EVENT_PDMA3_QM:
		glbl_sts_addr = mmDCORE1_PDMA1_QM_GLBL_ERR_STS_0;
		arb_err_addr = mmDCORE1_PDMA1_QM_ARB_ERR_CAUSE;
		snprintf(desc, ARRAY_SIZE(desc), "DCORE1_PDMA1_QM");
		break;
	case GRECO_EVENT_ROTATOR0_ROT0_QM:
		glbl_sts_addr = mmDCORE0_ROT_QM_GLBL_ERR_STS_0;
		arb_err_addr = mmDCORE0_ROT_QM_ARB_ERR_CAUSE;
		snprintf(desc, ARRAY_SIZE(desc), "DCORE0_ROT_QM");
		break;
	case GRECO_EVENT_ROTATOR1_ROT1_QM:
		glbl_sts_addr = mmDCORE1_ROT_QM_GLBL_ERR_STS_0;
		arb_err_addr = mmDCORE1_ROT_QM_ARB_ERR_CAUSE;
		snprintf(desc, ARRAY_SIZE(desc), "DCORE1_ROT_QM");
		break;
	default:
		return;
	}

	greco_handle_qman_err_generic(hdev, desc, glbl_sts_addr, arb_err_addr);
}

static void greco_print_irq_info(struct hl_device *hdev, u16 event_type)
{
	char desc[64] = "";

	greco_get_event_desc(event_type, desc, sizeof(desc));
	dev_err_ratelimited(hdev->dev, "Received H/W interrupt %d [\"%s\"]\n",
		event_type, desc);
}

static void greco_handle_psoc_razwi_info(struct hl_device *hdev, u32 razwi_reg, u64 *event_mask)
{
	u32 axuser_xy = RAZWI_GET_AXUSER_XY(razwi_reg), addr_hi = 0, addr_lo = 0;
	u64 base;
	int i;

	for (i = 0 ; i < ARRAY_SIZE(razwi_info) ; i++) {
		if (axuser_xy != razwi_info[i].axuser_xy)
			continue;

		base = razwi_info[i].rtr_ctrl;

		if (RREG32(base + DEC_RAZWI_HBW_AW_SET)) {
			addr_hi = RREG32(base + DEC_RAZWI_HBW_AW_HI_ADDR);
			addr_lo = RREG32(base + DEC_RAZWI_HBW_AW_LO_ADDR);
			dev_err(hdev->dev,
				"PSOC HBW AW RAZWI: %s, address (aligned to 128 byte): 0x%llX\n",
				razwi_info[i].eng_name, ((u64)addr_hi << 32) + addr_lo);
			hl_handle_razwi(hdev, ((u64)addr_hi << 32) + addr_lo, &razwi_info[i].eng_id,
					1, HL_RAZWI_HBW | HL_RAZWI_WRITE, event_mask);
		}

		if (RREG32(base + DEC_RAZWI_HBW_AR_SET)) {
			addr_hi = RREG32(base + DEC_RAZWI_HBW_AR_HI_ADDR);
			addr_lo = RREG32(base + DEC_RAZWI_HBW_AR_LO_ADDR);
			dev_err(hdev->dev,
				"PSOC HBW AR RAZWI: %s, address (aligned to 128 byte): 0x%llX\n",
				razwi_info[i].eng_name, ((u64)addr_hi << 32) + addr_lo);
			hl_handle_razwi(hdev, ((u64)addr_hi << 32) + addr_lo, &razwi_info[i].eng_id,
					1, HL_RAZWI_HBW | HL_RAZWI_READ, event_mask);
		}

		if (RREG32(base + DEC_RAZWI_LBW_AW_SET)) {
			addr_lo = RREG32(base + DEC_RAZWI_LBW_AW_ADDR);
			dev_err(hdev->dev,
				"PSOC LBW AW RAZWI: %s, address (aligned to 128 byte): 0x%X\n",
				razwi_info[i].eng_name, addr_lo);
			hl_handle_razwi(hdev, addr_lo, &razwi_info[i].eng_id,
					1, HL_RAZWI_LBW | HL_RAZWI_WRITE, event_mask);
		}

		if (RREG32(base + DEC_RAZWI_LBW_AR_SET)) {
			addr_lo = RREG32(base + DEC_RAZWI_LBW_AR_ADDR);
			dev_err(hdev->dev,
				"PSOC LBW AR RAZWI: %s, address (aligned to 128 byte): 0x%X\n",
				razwi_info[i].eng_name, addr_lo);
			hl_handle_razwi(hdev, addr_lo, &razwi_info[i].eng_id,
					1, HL_RAZWI_LBW | HL_RAZWI_READ, event_mask);
		}

		return;
	}
}

static void greco_print_psoc_razwi_interrupt_info(struct hl_device *hdev, char *razwi_module,
							u32 razwi_reg, u64 *event_mask)
{
	if (!razwi_reg)
		return;

	dev_err_ratelimited(hdev->dev,
		"PSOC RAZWI %s interrupt: mask %d, AR %d, AW %d, AXUSER_L 0x%x AXUSER_H 0x%x\n",
		razwi_module,
		(razwi_reg & PSOC_GLOBAL_CONF_RAZWI_SHARED_MASK_MASK) >>
				PSOC_GLOBAL_CONF_RAZWI_SHARED_MASK_SHIFT,
		(razwi_reg & PSOC_GLOBAL_CONF_RAZWI_SHARED_WAS_AR_MASK) >>
				PSOC_GLOBAL_CONF_RAZWI_SHARED_WAS_AR_SHIFT,
		(razwi_reg & PSOC_GLOBAL_CONF_RAZWI_SHARED_WAS_AW_MASK) >>
				PSOC_GLOBAL_CONF_RAZWI_SHARED_WAS_AW_SHIFT,
		(razwi_reg & PSOC_GLOBAL_CONF_RAZWI_SHARED_AXUSER_L_MASK) >>
				PSOC_GLOBAL_CONF_RAZWI_SHARED_AXUSER_L_SHIFT,
		(razwi_reg & PSOC_GLOBAL_CONF_RAZWI_SHARED_AXUSER_H_MASK) >>
				PSOC_GLOBAL_CONF_RAZWI_SHARED_AXUSER_H_SHIFT);

	greco_handle_psoc_razwi_info(hdev, razwi_reg, event_mask);

}

/*
 * PSOC RAZWI interrupt occurs only when trying to access a reserved address
 */
static void greco_ack_psoc_razwi_event(struct hl_device *hdev, u64 *event_mask)
{
	u32 razwi_shared, razwi_dcore0, razwi_dcore1, razwi_int;

	razwi_int = RREG32(mmPSOC_GLOBAL_CONF_DIS_RAZWI_ERR);
	if (razwi_int)
		dev_dbg_ratelimited(hdev->dev,
				"Called with PSOC RAZWI interrupts disabled\n");

	/* Verify we got a RAZWI interrupt at all */
	razwi_int = RREG32(mmPSOC_GLOBAL_CONF_RAZWI_INTERRUPT);
	if (!razwi_int)
		return;

	razwi_shared = RREG32(mmPSOC_GLOBAL_CONF_RAZWI_SHARED);
	razwi_dcore0 = RREG32(mmPSOC_GLOBAL_CONF_RAZWI_DCORE0);
	razwi_dcore1 = RREG32(mmPSOC_GLOBAL_CONF_RAZWI_DCORE1);

	greco_print_psoc_razwi_interrupt_info(hdev, "SHARED", razwi_shared, event_mask);
	greco_print_psoc_razwi_interrupt_info(hdev, "DCORE0", razwi_dcore0, event_mask);
	greco_print_psoc_razwi_interrupt_info(hdev, "DCORE1", razwi_dcore1, event_mask);

	/* Clear Interrupts */
	WREG32(mmPSOC_GLOBAL_CONF_RAZWI_INTERRUPT, razwi_int);
}

static int greco_ack_mstr_if_hbw_ar_razwi_errors(struct hl_device *hdev,
						u32 initiator_coordinates,
						int hbw_array_size,
						const u32 mstr_if_hbw_addr[],
						u16 eng_id, u64 *event_mask)
{
	u32 hbw_ar_razwi_happened, hbw_ar_razwi_hi, hbw_ar_razwi_lo, hbw_ar_razwi_xy;
	int i, ret = -EFAULT;

	/*
	 * razwi_happened register is cleared by read, make sure it reports our
	 * initiator errors prior of reading it
	 */
	for (i = 0 ; i < hbw_array_size ; i++) {
		hbw_ar_razwi_xy =
			RREG32(mstr_if_hbw_addr[i] + RR_HBW_AR_RAZWI_XY);
		if (hbw_ar_razwi_xy != initiator_coordinates)
			continue;

		hbw_ar_razwi_happened =
			RREG32(mstr_if_hbw_addr[i] + RR_HBW_AR_RAZWI_HAPPENED);
		if (!hbw_ar_razwi_happened)
			continue;

		/* found the reporting mstr_if */
		ret = 0;
		break;
	}

	/* did not find  the reporting master-if */
	if (ret != 0)
		return ret;

	hbw_ar_razwi_hi = RREG32(mstr_if_hbw_addr[i] + RR_HBW_AR_RAZWI_HI);
	hbw_ar_razwi_lo = RREG32(mstr_if_hbw_addr[i] + RR_HBW_AR_RAZWI_LO);

	hl_handle_razwi(hdev, ((u64)hbw_ar_razwi_hi << 32) + hbw_ar_razwi_lo, &eng_id, 1,
				HL_RAZWI_HBW | HL_RAZWI_READ, event_mask);

	dev_err_ratelimited(hdev->dev,
		"Slave HBW AR error, mstr_if 0x%x, captured address HI 0x%x LO 0x%x, Initiator coordinates 0x%x\n",
		mstr_if_hbw_addr[i], hbw_ar_razwi_hi,
		hbw_ar_razwi_lo, hbw_ar_razwi_xy);

	return 0;
}

static int greco_ack_mstr_if_hbw_aw_razwi_errors(struct hl_device *hdev,
						u32 initiator_coordinates,
						int hbw_array_size,
						const u32 mstr_if_hbw_addr[],
						u16 eng_id, u64 *event_mask)
{
	u32 hbw_aw_razwi_happened, hbw_aw_razwi_hi, hbw_aw_razwi_lo, hbw_aw_razwi_xy;
	int i, ret = -EFAULT;

	/*
	 * razwi_happened register is cleared by read, make sure it reports our
	 * initiator errors prior of reading it
	 */
	for (i = 0 ; i < hbw_array_size ; i++) {
		hbw_aw_razwi_xy =
			RREG32(mstr_if_hbw_addr[i] + RR_HBW_AW_RAZWI_XY);
		if (hbw_aw_razwi_xy != initiator_coordinates)
			continue;

		hbw_aw_razwi_happened =
			RREG32(mstr_if_hbw_addr[i] + RR_HBW_AW_RAZWI_HAPPENED);
		if (!hbw_aw_razwi_happened)
			continue;

		/* found the reporting mstr_if */
		ret = 0;
		break;
	}

	/* did not find  the reporting master-if */
	if (ret != 0)
		return ret;

	hbw_aw_razwi_hi = RREG32(mstr_if_hbw_addr[i] + RR_HBW_AW_RAZWI_HI);
	hbw_aw_razwi_lo = RREG32(mstr_if_hbw_addr[i] + RR_HBW_AW_RAZWI_LO);

	hl_handle_razwi(hdev, ((u64)hbw_aw_razwi_hi << 32) + hbw_aw_razwi_lo, &eng_id, 1,
			HL_RAZWI_HBW | HL_RAZWI_WRITE, event_mask);

	dev_err_ratelimited(hdev->dev,
		"Slave HBW WR error, mstr_if 0x%x, captured address HI 0x%x LO 0x%x, Initiator coordinates 0x%x\n",
		mstr_if_hbw_addr[i], hbw_aw_razwi_hi,
		hbw_aw_razwi_lo, hbw_aw_razwi_xy);

	return 0;
}

static int greco_ack_mstr_if_lbw_ar_razwi_errors(struct hl_device *hdev,
						u32 initiator_coordinates,
						int lbw_array_size,
						const u32 mstr_if_lbw_addr[],
						u16 eng_id, u64 *event_mask)
{
	int i, ret = -EFAULT;
	u32 lbw_ar_razwi_happened, lbw_ar_razwi_addr, lbw_ar_razwi_xy;

	/*
	 * razwi_happened register is cleared by read, make sure it reports our
	 * initiator errors prior of reading it
	 */
	for (i = 0 ; i < lbw_array_size ; i++) {
		lbw_ar_razwi_xy =
			RREG32(mstr_if_lbw_addr[i] + RR_LBW_AR_RAZWI_XY);
		if (lbw_ar_razwi_xy != initiator_coordinates)
			continue;

		lbw_ar_razwi_happened =
			RREG32(mstr_if_lbw_addr[i] + RR_LBW_AR_RAZWI_HAPPENED);
		if (!lbw_ar_razwi_happened)
			continue;

		/* found the reporting mstr_if */
		ret = 0;
		break;
	}

	/* did not find  the reporting master-if */
	if (ret != 0)
		return ret;

	lbw_ar_razwi_addr = RREG32(mstr_if_lbw_addr[i] + RR_LBW_AR_RAZWI);

	hl_handle_razwi(hdev, lbw_ar_razwi_addr, &eng_id, 1, HL_RAZWI_LBW | HL_RAZWI_READ,
			event_mask);

	dev_err_ratelimited(hdev->dev,
		"Slave LBW AR error, mstr_if 0x%x, captured address 0x%x, Initiator coordinates 0x%x\n",
		mstr_if_lbw_addr[i], lbw_ar_razwi_addr, lbw_ar_razwi_xy);

	return 0;
}

static int greco_ack_mstr_if_lbw_aw_razwi_errors(struct hl_device *hdev,
						u32 initiator_coordinates,
						int lbw_array_size,
						const u32 mstr_if_lbw_addr[],
						u16 eng_id, u64 *event_mask)
{
	int i, ret = -EFAULT;
	u32 lbw_aw_razwi_happened, lbw_aw_razwi_addr, lbw_aw_razwi_xy;

	/*
	 * razwi_happened register is cleared by read, make sure it reports our
	 * initiator errors prior of reading it
	 */
	for (i = 0 ; i < lbw_array_size ; i++) {
		lbw_aw_razwi_xy =
			RREG32(mstr_if_lbw_addr[i] + RR_LBW_AW_RAZWI_XY);
		if (lbw_aw_razwi_xy != initiator_coordinates)
			continue;

		lbw_aw_razwi_happened =
			RREG32(mstr_if_lbw_addr[i] + RR_LBW_AW_RAZWI_HAPPENED);
		if (!lbw_aw_razwi_happened)
			continue;

		/* found the reporting mstr_if */
		ret = 0;
		break;
	}

	/* did not find  the reporting master-if */
	if (ret != 0)
		return ret;

	lbw_aw_razwi_addr = RREG32(mstr_if_lbw_addr[i] + RR_LBW_AW_RAZWI);

	hl_handle_razwi(hdev, lbw_aw_razwi_addr, &eng_id, 1, HL_RAZWI_LBW | HL_RAZWI_WRITE,
			event_mask);

	dev_err_ratelimited(hdev->dev,
		"Slave LBW AW error, mstr_if 0x%x, captured address 0x%x, Initiator coordinates 0x%x\n",
		mstr_if_lbw_addr[i], lbw_aw_razwi_addr, lbw_aw_razwi_xy);

	return 0;
}

static void greco_ack_tpc_slave_errors(struct hl_device *hdev,
				int tpc_idx, u32 interrupts_cause_reg, u64 *event_mask)
{
	u32 tpc_coordinates = greco_tpc_initiator_coordinates[tpc_idx];
	u16 eng_id;
	int rc = 0;

	if (tpc_idx >= DCORE_NUM_OF_TPCS)
		eng_id = GRECO_DCORE1_ENGINE_ID_TPC_0 + tpc_idx - DCORE_NUM_OF_TPCS;
	else
		eng_id = GRECO_DCORE0_ENGINE_ID_TPC_0 + tpc_idx;

	/* Handle TPC slave errors:
	 *  Bit 19 - tpc_lbw_bresp_err
	 *  Bit 18 - tpc_lbw_rresp_err
	 *  Bit 17 - tpc_hbw_bresp_err
	 *  Bit 16 - tpc_hbw_rresp_err
	 */
	if (interrupts_cause_reg & BIT(19))
		rc += greco_ack_mstr_if_lbw_aw_razwi_errors(hdev,
			tpc_coordinates, DCORE_NUM_OF_TPCS - 1,
			greco_rr_lbw_tpc_mstr_if[tpc_idx / DCORE_NUM_OF_TPCS], eng_id, event_mask);

	if (interrupts_cause_reg & BIT(18))
		rc += greco_ack_mstr_if_lbw_ar_razwi_errors(hdev,
			tpc_coordinates, DCORE_NUM_OF_TPCS - 1,
			greco_rr_lbw_tpc_mstr_if[tpc_idx / DCORE_NUM_OF_TPCS], eng_id, event_mask);

	/*
	 * If RR did not block the LBW access maybe we've tried to access a
	 * protected register.
	 */
	if (rc)
		greco_ack_protection_bits_errors(hdev);

	if (interrupts_cause_reg & BIT(17)) {
		rc = greco_ack_mstr_if_hbw_aw_razwi_errors(hdev,
			tpc_coordinates, DCORE_NUM_OF_TPCS - 1,
			greco_rr_hbw_tpc_mstr_if[tpc_idx / DCORE_NUM_OF_TPCS], eng_id, event_mask);
		if (rc)
			dev_err_ratelimited(hdev->dev,
				"failed to ack hbw_aw_razwi_errors for tpc %d",
				tpc_idx);
	}

	if (interrupts_cause_reg & BIT(16)) {
		rc = greco_ack_mstr_if_hbw_ar_razwi_errors(hdev,
			tpc_coordinates, DCORE_NUM_OF_TPCS - 1,
			greco_rr_hbw_tpc_mstr_if[tpc_idx / DCORE_NUM_OF_TPCS], eng_id, event_mask);
		if (rc)
			dev_err_ratelimited(hdev->dev,
				"failed to ack hbw_ar_razwi_errors for tpc %d",
				tpc_idx);
	}
}

static bool greco_tpc_ack_interrupts(struct hl_device *hdev, u8 tpc_index,
					char *interrupt_name, u64 *event_mask)
{
	u32 tpc_cfg_intr_cause_addr, tpc_interrupts_cause, i;
	bool reset_required = false;

	tpc_cfg_intr_cause_addr = mmDCORE0_TPC0_CFG_TPC_INTR_CAUSE +
			DCORE_OFFSET * (tpc_index / NUM_OF_TPC_PER_DCORE) +
			DCORE_TPC_OFFSET * (tpc_index % NUM_OF_TPC_PER_DCORE);

	tpc_interrupts_cause = RREG32(tpc_cfg_intr_cause_addr) &
				DCORE0_TPC0_CFG_TPC_INTR_CAUSE_CAUSE_MASK;

	for (i = 0 ; i < GRECO_NUM_OF_TPC_INTR_CAUSE ; i++)
		if (tpc_interrupts_cause & BIT(i)) {
			dev_err_ratelimited(hdev->dev,
				"TPC%d_%s interrupt cause: %s\n",
				tpc_index, interrupt_name,
				greco_tpc_interrupts_cause[i]);
			/* If this is QM error, we need to soft-reset */
			if (i == 15)
				reset_required = true;
		}

	/* ack TPC slave errors */
	greco_ack_tpc_slave_errors(hdev, tpc_index, tpc_interrupts_cause, event_mask);

	/* Clear interrupts */
	WREG32(tpc_cfg_intr_cause_addr, 0);

	return reset_required;
}

static void greco_ack_axi_drain_event(struct hl_device *hdev)
{
	u32 drain_indication;

	/* start with lbw axi_drain event */
	if (!((RREG32(mmPCIE_WRAP_LBW_DRAIN_CFG) &
				(1 << PCIE_WRAP_LBW_DRAIN_CFG_EN_SHIFT)) ||
		(RREG32(mmPCIE_WRAP_HBW_DRAIN_CFG) &
				(1 << PCIE_WRAP_HBW_DRAIN_CFG_EN_SHIFT)))) {
		dev_dbg_ratelimited(hdev->dev,
			"Called with AXI drain functionality disabled\n");
		return;
	}

	drain_indication = RREG32(mmPCIE_WRAP_AXI_DRAIN_IND) &
			(PCIE_WRAP_AXI_DRAIN_IND_LBW_AXI_DRAIN_IND_MASK |
				PCIE_WRAP_AXI_DRAIN_IND_HBW_AXI_DRAIN_IND_MASK);

	switch (drain_indication) {
	case PCIE_WRAP_AXI_DRAIN_IND_LBW_AXI_DRAIN_IND_MASK:
		dev_err_ratelimited(hdev->dev, "AXI drain LBW event");
		break;
	case PCIE_WRAP_AXI_DRAIN_IND_HBW_AXI_DRAIN_IND_MASK:
		dev_err_ratelimited(hdev->dev, "AXI drain HBW event");
		break;
	case (PCIE_WRAP_AXI_DRAIN_IND_LBW_AXI_DRAIN_IND_MASK |
			PCIE_WRAP_AXI_DRAIN_IND_HBW_AXI_DRAIN_IND_MASK):
		dev_err_ratelimited(hdev->dev, "AXI drain LBW and HBW events");
		break;
	default:
		dev_dbg_ratelimited(hdev->dev,
			"AXI drain unknown event, 0x%x\n", drain_indication);
		break;
	}

	/* Ack the event */
	WREG32(mmPCIE_WRAP_AXI_DRAIN_IND, 0);
}

static void greco_handle_page_error(struct hl_device *hdev, u64 mmu_base, bool is_pmmu,
					u64 *event_mask)
{
	u64 addr;
	u32 val;

	val = RREG32(mmu_base + MMU_PAGE_ERROR_CAPTURE_OFFSET);

	if (!(val & DCORE0_HMMU0_MMU_PAGE_ERROR_CAPTURE_ENTRY_VALID_MASK))
		return;

	addr = val & DCORE0_HMMU0_MMU_PAGE_ERROR_CAPTURE_VA_49_32_MASK;
	addr <<= 32;
	addr |= RREG32(mmu_base + MMU_PAGE_ERROR_CAPTURE_VA_OFFSET);

	hl_handle_page_fault(hdev, addr, 0, is_pmmu, event_mask);
	dev_err_ratelimited(hdev->dev, "%s page fault on va %#llx\n",
				is_pmmu ? "PMMU" : "HMMU", addr);

	WREG32(mmu_base + MMU_PAGE_ERROR_CAPTURE_OFFSET, 0);
}

static void greco_handle_access_error(struct hl_device *hdev, u64 mmu_base, bool is_pmmu)
{
	u64 addr;
	u32 val;

	val = RREG32(mmu_base + MMU_ACCESS_ERROR_CAPTURE_OFFSET);

	if (!(val & DCORE0_HMMU0_MMU_ACCESS_ERROR_CAPTURE_ENTRY_VALID_MASK))
		return;

	addr = val & DCORE0_HMMU0_MMU_ACCESS_ERROR_CAPTURE_VA_49_32_MASK;
	addr <<= 32;
	addr |= RREG32(mmu_base + MMU_ACCESS_ERROR_CAPTURE_VA_OFFSET);

	dev_err_ratelimited(hdev->dev, "%s access error on va %#llx\n",
				is_pmmu ? "PMMU" : "HMMU", addr);

	WREG32(mmu_base + MMU_ACCESS_ERROR_CAPTURE_OFFSET, 0);
}

static void __greco_handle_mmu_spi_sei_err(struct hl_device *hdev, u64 mmu_base,
						const char *mmu_name, bool is_pmmu, u64 *event_mask)
{
	u32 spi_sei_cause, interrupt_clr = 0x0;
	int i;

	spi_sei_cause = RREG32(mmu_base + MMU_SPI_SEI_CAUSE_OFFSET);

	for (i = 0 ; i < GRECO_NUM_OF_MMU_SPI_SEI_ERR_CAUSE ; i++) {
		if (spi_sei_cause & BIT(i)) {
			dev_err_ratelimited(hdev->dev, "%s SPI_SEI ERR. err cause: %s\n",
						mmu_name, greco_mmu_spi_sei[i].cause);

			if (i == 0)
				greco_handle_page_error(hdev, mmu_base, is_pmmu, event_mask);
			else if (i == 1)
				greco_handle_access_error(hdev, mmu_base, is_pmmu);

			if (greco_mmu_spi_sei[i].clear_bit >= 0)
				interrupt_clr |= BIT(greco_mmu_spi_sei[i].clear_bit);
		}
	}

	/* Clear cause */
	WREG32_AND(mmu_base + MMU_SPI_SEI_CAUSE_OFFSET, ~spi_sei_cause);

	/* Clear interrupt */
	WREG32(mmu_base + MMU_INTERRUPT_CLR_OFFSET, interrupt_clr);

}

static void greco_handle_mmu_spi_sei_err(struct hl_device *hdev, u16 event_type, u64 *event_mask)
{
	u32 dcore_id, dmmu_id;
	bool is_pmmu = false;
	char mmu_name[32];
	u64 mmu_base;
	u8 index = 0;

	switch (event_type) {
	case GRECO_EVENT_HMMU_0_1:
	case GRECO_EVENT_HMMU_1_1:
	case GRECO_EVENT_HMMU_2_1:
	case GRECO_EVENT_HMMU_3_1:
		index = (event_type - GRECO_EVENT_HMMU_0_1) /
			(GRECO_EVENT_HMMU_1_1 - GRECO_EVENT_HMMU_0_1);
		break;
	case GRECO_EVENT_HMMU_WR_PERM_0:
	case GRECO_EVENT_HMMU_WR_PERM_1:
	case GRECO_EVENT_HMMU_WR_PERM_2:
	case GRECO_EVENT_HMMU_WR_PERM_3:
		index = (event_type - GRECO_EVENT_HMMU_WR_PERM_0) /
			(GRECO_EVENT_HMMU_WR_PERM_1 - GRECO_EVENT_HMMU_WR_PERM_0);
		break;
	case GRECO_EVENT_HMMU_DBG_BM_0:
	case GRECO_EVENT_HMMU_DBG_BM_1:
	case GRECO_EVENT_HMMU_DBG_BM_2:
	case GRECO_EVENT_HMMU_DBG_BM_3:
		index = (event_type - GRECO_EVENT_HMMU_DBG_BM_0) /
			(GRECO_EVENT_HMMU_DBG_BM_1 - GRECO_EVENT_HMMU_DBG_BM_0);
		break;
	case GRECO_EVENT_PMMU_0_1:
	case GRECO_EVENT_PMMU_WR_PERM_0:
	case GRECO_EVENT_PMMU_DBG_BM_0:
		is_pmmu = true;
		break;
	default:
		return;
	}

	if (is_pmmu) {
		mmu_base = mmPMMU_HBW_MMU_BASE;
		snprintf(mmu_name, sizeof(mmu_name), "PMMU");
	} else {
		/* 0 - DCORE0_HMMU0, 1 - DCORE1_HMMU0, 2 - DCORE0_HMMU1, 3 - DCORE1_HMMU1 */
		dcore_id = index % 2;
		dmmu_id = index / 2;
		mmu_base = mmDCORE0_HMMU0_MMU_BASE + dcore_id * DCORE_OFFSET +
				dmmu_id * DMMU_OFFSET;
		snprintf(mmu_name, sizeof(mmu_name), "DCORE%d_DMMU%d", dcore_id, dmmu_id);
	}

	__greco_handle_mmu_spi_sei_err(hdev, mmu_base, mmu_name, is_pmmu, event_mask);
}

static void greco_print_ecc_info(struct hl_device *hdev,
		struct hl_eq_ecc_data *ecc_data)
{
	u64 ecc_address, ecc_syndrom;
	u8 memory_wrapper_idx;

	ecc_address = le64_to_cpu(ecc_data->ecc_address);
	ecc_syndrom = le64_to_cpu(ecc_data->ecc_syndrom);
	memory_wrapper_idx = ecc_data->memory_wrapper_idx;

	dev_err(hdev->dev,
		"ECC error detected. address: %#llx. Syndrom: %#llx. block id %u\n",
		ecc_address, ecc_syndrom, memory_wrapper_idx);
}

static bool greco_dram_read_interrupts(struct hl_device *hdev,
						u16 event_type)
{
	u32 ddr_offset = mmDCORE0_DDR1_MISC_BASE - mmDCORE0_DDR0_MISC_BASE;
	u32 intr_c_reg = mmDCORE0_DDR0_MISC_INTERRUPT_CAUSE;
	u32 intr_m_reg = mmDCORE0_DDR0_MISC_INTERRUPT_MASK;
	u32 intr_cause;
	int dcore_num, ddr_num, i;

	switch (event_type) {
	case GRECO_EVENT_DDR0_SPI ... GRECO_EVENT_DDR3_SPI:
		dcore_num = 0;
		ddr_num = (event_type - GRECO_EVENT_DDR0_SPI);
		break;
	case GRECO_EVENT_DDR4_SPI ... GRECO_EVENT_DDR7_SPI:
		dcore_num = 1;
		ddr_num = (event_type - GRECO_EVENT_DDR4_SPI);
		break;
	default:
		return false;
	}

	intr_c_reg += dcore_num * DCORE_OFFSET + ddr_num * ddr_offset;
	intr_m_reg += dcore_num * DCORE_OFFSET + ddr_num * ddr_offset;

	intr_cause = RREG32(intr_c_reg) & RREG32(intr_m_reg);
	if (intr_cause) {
		WREG32(intr_c_reg, intr_cause);
		for (i = 0 ; i < GRECO_NUM_OF_DDR_SPI_ERR_CAUSE ; i++)
			if (intr_cause & BIT(i))
				dev_err_ratelimited(hdev->dev,
						"DRAM SPI Interrupt on DCORE%d DDR%d: %s\n",
						dcore_num, ddr_num,
						greco_ddr_spi_error_cause[i]);
	}

	/* Uncorrectable errors should be handled by resetting the device */
	if (intr_cause & (BIT(2) | BIT(8) | BIT(12) | BIT(18)))
		return true;
	else
		return false;
}

static bool greco_handle_ddr_sei_events(struct hl_device *hdev, u16 event_type)
{
	u32 ddr_offset = mmDCORE0_DDR1_MISC_BASE - mmDCORE0_DDR0_MISC_BASE;
	u32 intr_c_reg = mmDCORE0_DDR0_MISC_INTERRUPT_CAUSE;
	u32 intr_m_reg = mmDCORE0_DDR0_MISC_INTERRUPT_MASK;
	u32 intr_cause;
	int dcore_num, ddr_num, i;

	switch (event_type) {
	case GRECO_EVENT_DDR_SEI_0 ... GRECO_EVENT_DDR_SEI_3:
		dcore_num = 0;
		ddr_num = (event_type - GRECO_EVENT_DDR_SEI_0);
		break;
	case GRECO_EVENT_DDR_SEI_4 ... GRECO_EVENT_DDR_SEI_7:
		dcore_num = 1;
		ddr_num = (event_type - GRECO_EVENT_DDR_SEI_4);
		break;
	default:
		return false;
	}

	intr_c_reg += dcore_num * DCORE_OFFSET + ddr_num * ddr_offset;
	intr_m_reg += dcore_num * DCORE_OFFSET + ddr_num * ddr_offset;

	intr_cause = RREG32(intr_c_reg) & RREG32(intr_m_reg);
	if (intr_cause) {
		WREG32(intr_c_reg, intr_cause);
		for (i = 0 ; i < GRECO_NUM_OF_DDR_SEI_ERR_CAUSE ; i++)
			if (intr_cause & BIT(i))
				dev_err_ratelimited(hdev->dev,
						"DRAM SEI Interrupt on DCORE%d DDR%d: %s\n",
						dcore_num, ddr_num,
						greco_ddr_sei_error_cause[i]);
	}

	/* Uncorrectable errors should be handled by resetting the device */
	if (intr_cause & (BIT(0) | BIT(7) | BIT(13) | BIT(15)))
		return true;
	else
		return false;
}

static void greco_display_dec_sei_events(struct hl_device *hdev,
				int dcore_num, int vdec_num, u32 intr_cause)
{
	int i;

	for (i = 0 ; i < GRECO_NUM_OF_VDEC_INTR_CAUSE ; i++)
		if (intr_cause & BIT(i))
			dev_err_ratelimited(hdev->dev,
				"DCORE%d DEC%d SEI interrupt cause: %s\n",
				dcore_num, vdec_num,
				greco_vdec_interrupts_cause[i]);
}

static void greco_ack_dec_sei_events(struct hl_device *hdev, int index)
{
	u32 intr_c_reg = mmDCORE0_VDEC0_BRDG_CTRL_CAUSE_INTR;
	u32 intr_m_reg = mmDCORE0_VDEC0_BRDG_CTRL_CAUSE_INTR_MASK;
	u32 intr_cause;
	int i, dcore_num = 0;

	if (index >= NUM_OF_DEC_PER_DCORE) {
		intr_c_reg = mmDCORE1_VDEC0_BRDG_CTRL_CAUSE_INTR;
		intr_m_reg = mmDCORE1_VDEC0_BRDG_CTRL_CAUSE_INTR_MASK;
		dcore_num = 1;
	}

	/* interrupt is shared between the decore decoders */
	for (i = 0 ; i < NUM_OF_DEC_PER_DCORE ; i++) {
		intr_cause =  RREG32(intr_c_reg) & RREG32(intr_m_reg);
		if (intr_cause) {
			WREG32(intr_c_reg, intr_cause);
			greco_display_dec_sei_events(hdev, dcore_num,
							i, intr_cause);
		}
		intr_c_reg += VDEC_OFFSET;
		intr_m_reg += VDEC_OFFSET;
	}
}

static void greco_ack_dma_core_event(struct hl_device *hdev, u16 event_type)
{
	char *engine;
	u32 cause, cause_reg;
	int i;

	switch (event_type) {
	case GRECO_EVENT_DDMA0_CORE:
		cause_reg = mmDCORE0_DDMA_CORE_ERR_CAUSE;
		engine = "DCORE0 DDMA";
		break;

	case GRECO_EVENT_DDMA1_CORE:
		cause_reg = mmDCORE0_DDMA_CORE_ERR_CAUSE + DCORE_OFFSET;
		engine = "DCORE1 DDMA";
		break;

	case GRECO_EVENT_PDMA0_CORE:
		cause_reg = mmDCORE0_PDMA0_CORE_ERR_CAUSE;
		engine = "DCORE0 PDMA0";
		break;

	case GRECO_EVENT_PDMA1_CORE:
		cause_reg = mmDCORE0_PDMA1_CORE_ERR_CAUSE;
		engine = "DCORE0 PDMA1";
		break;

	case GRECO_EVENT_PDMA2_CORE:
		cause_reg = mmDCORE0_PDMA0_CORE_ERR_CAUSE + DCORE_OFFSET;
		engine = "DCORE1 PDMA0";
		break;

	case GRECO_EVENT_PDMA3_CORE:
		cause_reg = mmDCORE0_PDMA1_CORE_ERR_CAUSE + DCORE_OFFSET;
		engine = "DCORE1 PDMA1";
		break;

	case GRECO_EVENT_KDMA0_CORE:
		cause_reg = mmDCORE0_KDMA_CORE_ERR_CAUSE;
		engine = "DCORE0 KDMA";
		break;

	case GRECO_EVENT_KDMA1_CORE:
		cause_reg = mmDCORE0_KDMA_CORE_ERR_CAUSE + DCORE_OFFSET;
		engine = "DCORE1 KDMA";
		break;

	default:
		return;
	}

	cause = RREG32(cause_reg);
	if (!cause)
		return;

	WREG32(cause_reg, cause);

	for (i = 0 ; i < GRECO_NUM_OF_DMA_CORE_INTR_CAUSE ; i++) {
		if (cause & BIT(i))
			dev_err_ratelimited(hdev->dev,
					"%s. err cause: %s\n", engine,
					greco_dma_core_interrupts_cause[i]);
	}
}

static void greco_ack_enc_sei_events(struct hl_device *hdev)
{
	u32 int_cause;

	int_cause = RREG32(mmVENC_VL2C_CTRL_CAUSE_SEI_INTR);
	if (!int_cause)
		return;

	WREG32(mmVENC_VL2C_CTRL_CAUSE_SEI_INTR, int_cause);

	if (int_cause & VENC_VL2C_CTRL_CAUSE_SEI_INTR_HBW_ERR_RSP_MASK)
		dev_err_ratelimited(hdev->dev,
			"ENC SEI HBW_ERR_RSP interrupt\n");

	if (int_cause & VENC_VL2C_CTRL_CAUSE_SEI_INTR_MSIX_LBW_ERR_RSP_MASK)
		dev_err_ratelimited(hdev->dev,
			"ENC SEI MSIX_LBW_ERR_RESP interrupt\n");
}

static void greco_print_out_of_sync_info(struct hl_device *hdev,
					struct cpucp_pkt_sync_err *sync_err)
{
	struct hl_hw_queue *q = &hdev->kernel_queues[GRECO_QUEUE_ID_CPU_PQ];

	dev_err(hdev->dev, "Out of sync with FW, FW: pi=%u, ci=%u, LKD: pi=%u, ci=%d\n",
		le32_to_cpu(sync_err->pi), le32_to_cpu(sync_err->ci), q->pi, atomic_read(&q->ci));
}

static void greco_print_clk_change_info(struct hl_device *hdev, u16 event_type, u64 *event_mask)
{
	ktime_t zero_time = ktime_set(0, 0);

	mutex_lock(&hdev->clk_throttling.lock);

	switch (event_type) {
	case GRECO_EVENT_CPU_FIX_POWER_ENV_S:
		hdev->clk_throttling.current_reason |= HL_CLK_THROTTLE_POWER;
		hdev->clk_throttling.aggregated_reason |= HL_CLK_THROTTLE_POWER;
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_POWER].start = ktime_get();
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_POWER].end = zero_time;
		dev_info_ratelimited(hdev->dev,
			"Clock throttling due to power consumption\n");
		break;

	case GRECO_EVENT_CPU_FIX_POWER_ENV_E:
		hdev->clk_throttling.current_reason &= ~HL_CLK_THROTTLE_POWER;
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_POWER].end = ktime_get();
		dev_info_ratelimited(hdev->dev,
			"Power envelop is safe, back to optimal clock\n");
		break;

	case GRECO_EVENT_CPU_FIX_THERMAL_ENV_S:
		hdev->clk_throttling.current_reason |= HL_CLK_THROTTLE_THERMAL;
		hdev->clk_throttling.aggregated_reason |= HL_CLK_THROTTLE_THERMAL;
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_THERMAL].start = ktime_get();
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_THERMAL].end = zero_time;
		*event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		dev_info_ratelimited(hdev->dev,
			"Clock throttling due to overheating\n");
		break;

	case GRECO_EVENT_CPU_FIX_THERMAL_ENV_E:
		hdev->clk_throttling.current_reason &= ~HL_CLK_THROTTLE_THERMAL;
		hdev->clk_throttling.timestamp[HL_CLK_THROTTLE_TYPE_THERMAL].end = ktime_get();
		*event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		dev_info_ratelimited(hdev->dev,
			"Thermal envelop is safe, back to optimal clock\n");
		break;

	default:
		dev_err(hdev->dev, "Received invalid clock change event %d\n",
			event_type);
		break;
	}

	mutex_unlock(&hdev->clk_throttling.lock);
}

static void greco_print_cpu_pkt_failure_info(struct hl_device *hdev,
					struct cpucp_pkt_sync_err *sync_err)
{
	struct hl_hw_queue *q = &hdev->kernel_queues[GRECO_QUEUE_ID_CPU_PQ];

	dev_warn(hdev->dev,
		"FW reported sanity check failure, FW: pi=%u, ci=%u, LKD: pi=%u, ci=%d\n",
		le32_to_cpu(sync_err->pi), le32_to_cpu(sync_err->ci), q->pi, atomic_read(&q->ci));
}

static void greco_print_pcie_addr_dec_info(struct hl_device *hdev)
{
	u32 intr_cause_data;
	int i;

	intr_cause_data = RREG32(mmPCIE_WRAP_PCIE_IC_SEI_INTR_IND);

	for (i = 0 ; i < GRECO_NUM_OF_PCIE_ADDR_DEC_ERR_CAUSE ; i++)
		if (intr_cause_data & BIT_ULL(i))
			dev_err_ratelimited(hdev->dev, "PCIE ADDR DEC Error: %s\n",
					greco_pcie_addr_dec_error_cause[i]);

	/* Clear interrupt- W1C */
	WREG32(mmPCIE_WRAP_AXI_INTR, 1);
}

static void greco_handle_sm_err(struct hl_device *hdev, u8 sm_index)
{
	u32 sei_cause_addr, sei_cause_reg_val, sei_cause, sei_cause_log;
	int i;

	sei_cause_addr = mmDCORE0_SYNC_MNGR_GLBL_SM_SEI_CAUSE + (DCORE_OFFSET * sm_index);
	sei_cause_reg_val = RREG32(sei_cause_addr);
	sei_cause = FIELD_GET(DCORE0_SYNC_MNGR_GLBL_SM_SEI_CAUSE_CAUSE_MASK, sei_cause_reg_val);

	/* SEI interrupt */
	if (sei_cause) {
		/* There are corresponding SEI_CAUSE_log bits for every SEI_CAUSE_cause bit */
		sei_cause_log = FIELD_GET(DCORE0_SYNC_MNGR_GLBL_SM_SEI_CAUSE_LOG_MASK, sei_cause);

		for (i = 0 ; i < GRECO_NUM_OF_SM_SEI_ERR_CAUSE ; i++) {
			if (!(sei_cause & BIT(i)))
				continue;

			dev_err_ratelimited(hdev->dev, "SM%u SEI ERR. err cause: %s. %s: 0x%X\n",
					sm_index,
					greco_sm_sei_cause[i].cause_name,
					greco_sm_sei_cause[i].log_name,
					sei_cause_log & greco_sm_sei_cause[i].log_mask);
			break;
		}

		/* Clear SM_SEI_CAUSE */
		WREG32(sei_cause_addr, 0);
	}
}

void greco_handle_eqe(struct hl_device *hdev, struct hl_eq_entry *eq_entry)
{
	u32 ctl, reset_flags = HL_DRV_RESET_HARD | HL_DRV_RESET_DELAY;
	struct greco_device *greco = hdev->asic_specific;
	u64 event_mask = 0;
	bool reset_required;
	u16 event_type;
	int index;

	ctl = le32_to_cpu(eq_entry->hdr.ctl);
	event_type = ((ctl & EQ_CTL_EVENT_TYPE_MASK) >> EQ_CTL_EVENT_TYPE_SHIFT);
	if (event_type >= GRECO_EVENT_SIZE) {
		dev_err(hdev->dev, "Event type %u exceeds maximum of %u",
				event_type, GRECO_EVENT_SIZE - 1);
		return;
	}

	greco->events_stat[event_type]++;
	greco->events_stat_aggregate[event_type]++;

	greco_print_irq_info(hdev, event_type);

	switch (event_type) {
	case GRECO_EVENT_PCIE_CORE_DERR:
	case GRECO_EVENT_PCIE_PHY_DERR:
	case GRECO_EVENT_PCIE_IF_DERR:
	case GRECO_EVENT_TPC0_DERR ... GRECO_EVENT_TPC9_DERR:
		fallthrough;
	case GRECO_EVENT_DDMA0_DERR_ECC ... GRECO_EVENT_PDMA3_DERR_ECC:
		fallthrough;
	case GRECO_EVENT_MME0_SBTE0_DERR:
	case GRECO_EVENT_MME0_SBTE1_DERR:
	case GRECO_EVENT_MME0_SBTE2_DERR:
	case GRECO_EVENT_MME1_SBTE0_DERR:
	case GRECO_EVENT_MME1_SBTE1_DERR:
	case GRECO_EVENT_MME1_SBTE2_DERR:
		fallthrough;
	case GRECO_EVENT_MME0_WBC_DERR:
	case GRECO_EVENT_MME1_WBC_DERR:
	case GRECO_EVENT_MME0_L0_SRAM_DERR:
	case GRECO_EVENT_MME1_L0_SRAM_DERR:
		fallthrough;
	case GRECO_EVENT_DEC_VCD_DERR_0 ... GRECO_EVENT_DEC_VCD_DERR_9:
	case GRECO_EVENT_DEC_L2C_DERR_0 ... GRECO_EVENT_DEC_L2C_DERR_9:
	case GRECO_EVENT_DEC_VSI_DERR_0 ... GRECO_EVENT_DEC_VSI_DERR_1:
	case GRECO_EVENT_ENC_DERR_0 ... GRECO_EVENT_ENC_DERR_1:
		fallthrough;
	case GRECO_EVENT_SYNC_MNGR0_SM_DERR_0:
	case GRECO_EVENT_SYNC_MNGR1_SM_DERR_1:
	case GRECO_EVENT_HIF0_DOUBLE_ECC_ERROR:
	case GRECO_EVENT_HIF1_DOUBLE_ECC_ERROR:
	case GRECO_EVENT_HIF2_DOUBLE_ECC_ERROR:
	case GRECO_EVENT_HIF3_DOUBLE_ECC_ERROR:
	case GRECO_EVENT_ROTATOR0_ECC_DOUBLE_ERROR:
	case GRECO_EVENT_ROTATOR1_ECC_DOUBLE_ERROR:
	case GRECO_EVENT_REDUCTION0_ECC_DERR...GRECO_EVENT_REDUCTION3_ECC_DERR:
		fallthrough;
	case GRECO_EVENT_CPU_IF_ECC_DERR:
	case GRECO_EVENT_PSOC_MEM_DERR:
	case GRECO_EVENT_SRAM0_DERR ... GRECO_EVENT_SRAM15_DERR:
	case GRECO_EVENT_CPU_GIC500:
		fallthrough;
	case GRECO_EVENT_DDR_0_DERR ... GRECO_EVENT_DDR_7_PHY_INT_ERR:
	case GRECO_EVENT_HMMU0_STLB_MMU_ECC ... GRECO_EVENT_PMMU0_HMMU_ECC_1:
		fallthrough;
	case GRECO_EVENT_PLL0 ... GRECO_EVENT_PLL9:
		greco_print_ecc_info(hdev, &eq_entry->ecc_data);
		reset_flags |= HL_DRV_RESET_FW_FATAL_ERR;
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		goto reset_device;
	case GRECO_EVENT_PCIE_PHY_SERR:
	case GRECO_EVENT_PCIE_IF_SERR:
	case GRECO_EVENT_TPC0_SERR ... GRECO_EVENT_TPC9_SERR:
		fallthrough;
	case GRECO_EVENT_MME0_SBTE0_SERR:
	case GRECO_EVENT_MME0_SBTE1_SERR:
	case GRECO_EVENT_MME0_SBTE2_SERR:
	case GRECO_EVENT_MME1_SBTE0_SERR:
	case GRECO_EVENT_MME1_SBTE1_SERR:
	case GRECO_EVENT_MME1_SBTE2_SERR:
		fallthrough;
	case GRECO_EVENT_DDMA0_SERR_ECC ... GRECO_EVENT_PDMA3_SERR_ECC:
	case GRECO_EVENT_CPU_IF_ECC_SERR:
		fallthrough;
	case GRECO_EVENT_PCIE_CORE_SERR:
	case GRECO_EVENT_PSOC_MEM_SERR:
	case GRECO_EVENT_SRAM0_SERR ... GRECO_EVENT_SRAM15_SERR:
		fallthrough;
	case GRECO_EVENT_DDR_0_SERR ... GRECO_EVENT_DDR_7_SERR:
		greco_print_ecc_info(hdev, &eq_entry->ecc_data);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		break;

	case GRECO_EVENT_CPU_AXI_ECC:
	case GRECO_EVENT_CPU_L2_RAM_ECC:
		/* TODO: KMD will log, restart ARM application (SIGBUS) */
		reset_flags |= HL_DRV_RESET_FW_FATAL_ERR;
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		goto reset_device;

	case GRECO_EVENT_MME0_DCP0_UNDERFLOW_OR_WRONG_SKIP_VALUE:
	case GRECO_EVENT_MME0_DCP0_UNDERFLOW_OR_WRONG_SKIP_VALUE_1:
	case GRECO_EVENT_MME1_DCP0_UNDERFLOW_OR_WRONG_SKIP_VALUE:
	case GRECO_EVENT_MME1_DCP0_UNDERFLOW_OR_WRONG_SKIP_VALUE_1:
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		goto reset_device;

	case GRECO_EVENT_DDR_SEI_0 ... GRECO_EVENT_DDR_SEI_7:
		/* In case of uncorrectable error, reset the SoC */
		reset_required = greco_handle_ddr_sei_events(hdev, event_type);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		if (reset_required) {
			reset_flags |= HL_DRV_RESET_FW_FATAL_ERR;
			goto reset_device;
		}
		break;

	case GRECO_EVENT_DEC_SEI_0:
	case GRECO_EVENT_DEC_SEI_5:
		greco_ack_dec_sei_events(hdev,
					event_type - GRECO_EVENT_DEC_SEI_0);
		hl_check_for_glbl_errors(hdev);
		event_mask |= (HL_NOTIFIER_EVENT_USER_ENGINE_ERR | HL_NOTIFIER_EVENT_DEVICE_RESET);
		break;

	case GRECO_EVENT_ENC_SEI_0:
		greco_ack_enc_sei_events(hdev);
		hl_check_for_glbl_errors(hdev);
		event_mask |= (HL_NOTIFIER_EVENT_GENERAL_HW_ERR | HL_NOTIFIER_EVENT_DEVICE_RESET);
		break;

	case GRECO_EVENT_SYNC_MNGR0_AXI_ERROR_RESPONSE:
	case GRECO_EVENT_SYNC_MNGR1_AXI_ERROR_RESPONSE:
		index = event_type - GRECO_EVENT_SYNC_MNGR0_AXI_ERROR_RESPONSE;
		greco_handle_sm_err(hdev, index);
		hl_check_for_glbl_errors(hdev);
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		break;

	case GRECO_EVENT_HIF0_FIFO_OVERRUN ... GRECO_EVENT_HIF3_FIFO_OVERRUN:
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		goto reset_device;

	case GRECO_EVENT_HMMU_0_1:
	case GRECO_EVENT_HMMU_1_1:
	case GRECO_EVENT_HMMU_2_1:
	case GRECO_EVENT_HMMU_3_1:
	case GRECO_EVENT_PMMU_0_1:
	case GRECO_EVENT_HMMU_WR_PERM_0:
	case GRECO_EVENT_HMMU_WR_PERM_1:
	case GRECO_EVENT_HMMU_WR_PERM_2:
	case GRECO_EVENT_HMMU_WR_PERM_3:
	case GRECO_EVENT_PMMU_WR_PERM_0:
	case GRECO_EVENT_HMMU_DBG_BM_0:
	case GRECO_EVENT_HMMU_DBG_BM_1:
	case GRECO_EVENT_HMMU_DBG_BM_2:
	case GRECO_EVENT_HMMU_DBG_BM_3:
	case GRECO_EVENT_PMMU_DBG_BM_0:
		greco_handle_mmu_spi_sei_err(hdev, event_type, &event_mask);
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		break;

	case GRECO_EVENT_DDR0_SPI ... GRECO_EVENT_DDR7_SPI:
		reset_required = greco_dram_read_interrupts(hdev,
							event_type);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		if (reset_required) {
			reset_flags |= HL_DRV_RESET_FW_FATAL_ERR;
			goto reset_device;
		}

		break;

	case GRECO_EVENT_TPC0_KRN_ERR:
	case GRECO_EVENT_TPC1_KRN_ERR:
	case GRECO_EVENT_TPC2_KRN_ERR:
	case GRECO_EVENT_TPC3_KRN_ERR:
	case GRECO_EVENT_TPC4_KRN_ERR:
	case GRECO_EVENT_TPC5_KRN_ERR:
	case GRECO_EVENT_TPC6_KRN_ERR:
	case GRECO_EVENT_TPC7_KRN_ERR:
	case GRECO_EVENT_TPC8_KRN_ERR:
	case GRECO_EVENT_TPC9_KRN_ERR:
		index = (event_type - GRECO_EVENT_TPC0_KRN_ERR) /
			(GRECO_EVENT_TPC1_KRN_ERR - GRECO_EVENT_TPC0_KRN_ERR);
		reset_required = greco_tpc_ack_interrupts(hdev,
							index, "KRN_ERR", &event_mask);
		hl_check_for_glbl_errors(hdev);
		event_mask |= (HL_NOTIFIER_EVENT_USER_ENGINE_ERR | HL_NOTIFIER_EVENT_DEVICE_RESET);
		if (reset_required)
			goto reset_device;

		break;

	case GRECO_EVENT_TPC0_QM:
	case GRECO_EVENT_TPC1_QM:
	case GRECO_EVENT_TPC2_QM:
	case GRECO_EVENT_TPC3_QM:
	case GRECO_EVENT_TPC4_QM:
	case GRECO_EVENT_TPC5_QM:
	case GRECO_EVENT_TPC6_QM:
	case GRECO_EVENT_TPC7_QM:
	case GRECO_EVENT_TPC8_QM:
	case GRECO_EVENT_TPC9_QM:
	case GRECO_EVENT_MME0_QM:
	case GRECO_EVENT_DDMA0_QM:
	case GRECO_EVENT_DDMA1_QM:
	case GRECO_EVENT_PDMA0_QM:
	case GRECO_EVENT_PDMA1_QM:
	case GRECO_EVENT_PDMA2_QM:
	case GRECO_EVENT_PDMA3_QM:
	case GRECO_EVENT_ROTATOR0_ROT0_QM:
	case GRECO_EVENT_ROTATOR1_ROT1_QM:
		greco_handle_qman_err(hdev, event_type);
		hl_check_for_glbl_errors(hdev);
		event_mask |= (HL_NOTIFIER_EVENT_USER_ENGINE_ERR | HL_NOTIFIER_EVENT_DEVICE_RESET);
		break;

	case GRECO_EVENT_TPC0_DEC ... GRECO_EVENT_TPC9_DEC:
		index = event_type - GRECO_EVENT_TPC0_DEC;
		/* TODO: KMD will log, report error on command submission */
		reset_required = greco_tpc_ack_interrupts(hdev, index,
							"AXI_SLV_DEC_Error", &event_mask);
		hl_check_for_glbl_errors(hdev);
		event_mask |= (HL_NOTIFIER_EVENT_USER_ENGINE_ERR | HL_NOTIFIER_EVENT_DEVICE_RESET);
		if (reset_required)
			goto reset_device;

		break;

	case GRECO_EVENT_PCIE_APB_OR_DRAIN:
		greco_ack_axi_drain_event(hdev);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		goto reset_device;

	case GRECO_EVENT_PSOC63_RAZWI_OR_PID_MIN_MAX_INTERRUPT:
		greco_ack_psoc_razwi_event(hdev, &event_mask);
		event_mask |= HL_NOTIFIER_EVENT_USER_ENGINE_ERR;
		break;

	case GRECO_EVENT_PDMA0_CORE:
	case GRECO_EVENT_PDMA1_CORE:
	case GRECO_EVENT_DDMA0_CORE:
	case GRECO_EVENT_KDMA0_CORE:
	case GRECO_EVENT_PDMA2_CORE:
	case GRECO_EVENT_PDMA3_CORE:
	case GRECO_EVENT_DDMA1_CORE:
	case GRECO_EVENT_KDMA1_CORE:
		greco_ack_dma_core_event(hdev, event_type);
		hl_check_for_glbl_errors(hdev);
		event_mask |= (HL_NOTIFIER_EVENT_USER_ENGINE_ERR | HL_NOTIFIER_EVENT_DEVICE_RESET);
		break;

	case GRECO_EVENT_CPU_FIX_POWER_ENV_S:
	case GRECO_EVENT_CPU_FIX_POWER_ENV_E:
	case GRECO_EVENT_CPU_FIX_THERMAL_ENV_S:
	case GRECO_EVENT_CPU_FIX_THERMAL_ENV_E:
		greco_print_clk_change_info(hdev, event_type, &event_mask);
		break;

	case GRECO_EVENT_CPU_PKT_QUEUE_ASYNC:
		greco_print_out_of_sync_info(hdev, &eq_entry->pkt_sync_err);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		goto reset_device;

	case GRECO_EVENT_CPU_PKT_SANITY_FAILED:
		greco_print_cpu_pkt_failure_info(hdev, &eq_entry->pkt_sync_err);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		goto reset_device;

	case GRECO_EVENT_PCIE_ADDR_DEC_ERR:
		greco_print_pcie_addr_dec_info(hdev);
		hl_check_for_glbl_errors(hdev);
		event_mask |= HL_NOTIFIER_EVENT_GENERAL_HW_ERR;
		goto reset_device;

	case GRECO_EVENT_CPU_CPLD_SHUTDOWN_CAUSE:
	case GRECO_EVENT_CPU_CPLD_SHUTDOWN_EVENT:
		dev_err(hdev->dev, "CPLD shutdown, reset reason: 0x%llx\n",
					le64_to_cpu(eq_entry->data[0]));
		break;
	default:
		dev_err_ratelimited(hdev->dev,
				"Unknown event %u\n", event_type);
		return;
	}

	goto out;

reset_device:
	if (hdev->hard_reset_on_fw_events) {
		event_mask |= HL_NOTIFIER_EVENT_DEVICE_RESET;
		hl_device_cond_reset(hdev, reset_flags, event_mask);
		return;
	}

out:
	/* Send unmask irq only for interrupts not classified as MSG */
	if (!greco_irq_map_table[event_type].msg)
		hl_fw_unmask_irq(hdev, event_type);

	if (event_mask)
		hl_notifier_event_send_all(hdev, event_mask);
}

void *greco_get_events_stat(struct hl_device *hdev, bool aggregate, u32 *size)
{
	struct greco_device *greco = hdev->asic_specific;

	if (aggregate) {
		*size = (u32) sizeof(greco->events_stat_aggregate);
		return greco->events_stat_aggregate;
	}

	*size = (u32) sizeof(greco->events_stat);
	return greco->events_stat;
}

static int greco_memset_device_memory(struct hl_device *hdev, u64 addr,
					u64 size, u64 val, int dcore_id)
{
	u32 chunk_size;
	int rc;

	if (size > DMA_MAX_TRANSFER_SIZE)
		/* Set chunk size to max size aligned to our cache size */
		chunk_size = ALIGN_DOWN(DMA_MAX_TRANSFER_SIZE, 128);
	else
		chunk_size = size;

	while (size >= chunk_size) {
		rc = greco_send_job_to_kdma(hdev, val, addr, chunk_size,
						dcore_id, true);
		if (rc)
			return rc;

		addr += chunk_size;
		size -= chunk_size;

		if ((size) && (size < chunk_size))
			chunk_size = size;
	}

	return 0;
}

static void greco_mmu_shared_prepare(struct hl_device *hdev, u32 asid)
{
	bool encoder_enabled = !!(hdev->asic_prop.decoder_enabled_mask & 0x3E0);

	if (!encoder_enabled)
		return;

	RMWREG32(mmVENC_VL2C_CTRL_MSIX_LBW_AWUSER, asid, MMUBP_ASID_MASK);
	RMWREG32(mmVENC_VL2C_CTRL_ARC_START_LBW_AWUSER, asid, MMUBP_ASID_MASK);
	RMWREG32(mmVENC_VL2C_CTRL_ARC_FINISH_LBW_AWUSER, asid, MMUBP_ASID_MASK);
	RMWREG32(mmVENC_VL2C_CTRL_CPLQ_HBW_AWUSER_L, asid, MMUBP_ASID_MASK);
	RMWREG32(mmVENC_VL2C_CTRL_DEC_HBW_AWUSER_L, asid, MMUBP_ASID_MASK);
	RMWREG32(mmVENC_VL2C_CTRL_DEC_HBW_ARUSER_L, asid, MMUBP_ASID_MASK);
}

static void greco_mmu_dcore_prepare(struct hl_device *hdev, int dcore_id,
					u32 asid)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u32 dcore_offset, decoder_mask_shift, tpc_mask_shift, tpc_offset,
		dec_offset;
	int i;

	dcore_offset = dcore_id * DCORE_OFFSET;
	decoder_mask_shift = dcore_id * NUM_OF_DEC_PER_DCORE;
	tpc_mask_shift = dcore_id * NUM_OF_TPC_PER_DCORE;

	WREG32(mmDCORE0_DDMA_QM_AXUSER_NONSECURED_HB_SEC + dcore_offset, asid);
	WREG32(mmDCORE0_PDMA0_QM_AXUSER_NONSECURED_HB_SEC + dcore_offset, asid);
	WREG32(mmDCORE0_PDMA1_QM_AXUSER_NONSECURED_HB_SEC + dcore_offset, asid);

	WREG32(mmDCORE0_DDMA_CORE_CTX_AXUSER_HB_SEC + dcore_offset, asid);
	WREG32(mmDCORE0_PDMA0_CORE_CTX_AXUSER_HB_SEC + dcore_offset, asid);
	WREG32(mmDCORE0_PDMA1_CORE_CTX_AXUSER_HB_SEC + dcore_offset, asid);

	WREG32(mmDCORE0_SYNC_MNGR_MSTR_IF_AXUSER_HB_SEC + dcore_offset, asid);

	for (i = 0; i < NUM_OF_TPC_PER_DCORE; i++) {
		if (!(prop->tpc_enabled_mask & BIT(0 + tpc_mask_shift + i)))
			continue;

		tpc_offset = dcore_offset + i * DCORE_TPC_OFFSET;

		WREG32(mmDCORE0_TPC0_CFG_AXUSER_HB_SEC + tpc_offset, asid);
		WREG32(mmDCORE0_TPC0_QM_AXUSER_NONSECURED_HB_SEC + tpc_offset,
								asid);
	}

	if (hdev->mme_mask) {
		WREG32(mmDCORE0_MME_CTRL_LO_MME_AXUSER_HB_SEC + dcore_offset,
			asid);

		RMWREG32(mmDCORE0_MME_SBTEA_ARUSER + dcore_offset, asid,
							MMUBP_ASID_MASK);

		RMWREG32(mmDCORE0_MME_SBTEB_ARUSER + dcore_offset, asid,
							MMUBP_ASID_MASK);

		RMWREG32(mmDCORE0_MME_SBTEL_ARUSER + dcore_offset, asid,
							MMUBP_ASID_MASK);

		if (dcore_id == 0 || !prop->hw_queues_props[GRECO_QUEUE_ID_DCORE1_MME_0_0].slave)
			WREG32(mmDCORE0_MME_QM_AXUSER_NONSECURED_HB_SEC + dcore_offset, asid);
	}

	if (hdev->rotator_mask & BIT(dcore_id)) {
		RMWREG32(mmDCORE0_ROT_CPL_QUEUE_AWUSER + dcore_offset, asid,
							MMUBP_ASID_MASK);

		RMWREG32(mmDCORE0_ROT_DESC_CPL_MSG_AWUSER + dcore_offset, asid,
							MMUBP_ASID_MASK);

		RMWREG32(mmDCORE0_ROT_DESC_HBW_ARUSER_LO + dcore_offset, asid,
							MMUBP_ASID_MASK);

		RMWREG32(mmDCORE0_ROT_DESC_HBW_AWUSER_LO + dcore_offset, asid,
							MMUBP_ASID_MASK);

		WREG32(mmDCORE0_ROT_QM_AXUSER_NONSECURED_HB_SEC + dcore_offset,
									asid);
	}

	for (i = 0; i < NUM_OF_DEC_PER_DCORE; i++) {
		if (!(prop->decoder_enabled_mask &
					BIT(0 + decoder_mask_shift + i)))
			continue;

		dec_offset = dcore_offset + i * DCORE_DEC_OFFSET;

		WREG32(mmDCORE0_VDEC0_BRDG_CTRL_AXUSER_ARC_HB_SEC + dec_offset,
			asid);
		WREG32(mmDCORE0_VDEC0_BRDG_CTRL_AXUSER_DEC_HB_SEC + dec_offset,
			asid);
		WREG32(mmDCORE0_VDEC0_BRDG_CTRL_AXUSER_MSIX_ABNRM_HB_SEC +
							dec_offset, asid);
		WREG32(mmDCORE0_VDEC0_BRDG_CTRL_AXUSER_MSIX_L2C_HB_SEC +
							dec_offset, asid);
		WREG32(mmDCORE0_VDEC0_BRDG_CTRL_AXUSER_MSIX_NRM_HB_SEC +
							dec_offset, asid);
		WREG32(mmDCORE0_VDEC0_BRDG_CTRL_AXUSER_MSIX_VCD_HB_SEC +
							dec_offset, asid);
	}
}

/* zero the MMBP and ASID bits and then set the ASID */
static void greco_mmu_prepare(struct hl_device *hdev, u32 asid)
{
	struct greco_device *greco = hdev->asic_specific;

	if (asid & ~DCORE0_HMMU0_STLB_ASID_ASID_MASK) {
		dev_crit(hdev->dev, "asid %u is too big\n", asid);
		return;
	}

	if (!(greco->hw_cap_initialized & HW_CAP_MMU_MASK))
		return;

	greco_mmu_shared_prepare(hdev, asid);
	greco_mmu_dcore_prepare(hdev, 0, asid);
	greco_mmu_dcore_prepare(hdev, 1, asid);
}

static void greco_restore_sm_registers(struct hl_device *hdev)
{
	u64 addr;
	u32 val, size, offset;
	int rc, dcore_id;

	dev_dbg(hdev->dev, "Clearing SM's monitors\n");

	offset = GRECO_FIRST_AVAILABLE_MONITOR * 4;
	addr = CFG_BASE + mmDCORE0_SYNC_MNGR_OBJS_MON_STATUS_0 + offset;
	val = 1 << DCORE0_SYNC_MNGR_OBJS_MON_STATUS_PROT_SHIFT;
	size = mmDCORE0_SYNC_MNGR_OBJS_SM_SEC_0 -
			(mmDCORE0_SYNC_MNGR_OBJS_MON_STATUS_0 + offset);

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++) {
		rc = greco_memset_device_memory(hdev, addr, size, (u64) val, 0);
		if (rc) {
			dev_err(hdev->dev,
				"Failed to restore DCORE %d monitor registers\n",
				dcore_id);
			return;
		}

		addr += DCORE_OFFSET;
	}

	dev_dbg(hdev->dev, "Clearing SM's sync objects\n");

	offset = GRECO_FIRST_AVAILABLE_SYNC_OBJECT * 4;
	addr = CFG_BASE + mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0 + offset;
	val = 0;
	size = mmDCORE0_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0 -
			(mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0 + offset);

	for (dcore_id = 0 ; dcore_id < NUM_OF_DCORES ; dcore_id++) {
		rc = greco_memset_device_memory(hdev, addr, size, (u64) val, 0);
		if (rc) {
			dev_err(hdev->dev,
				"Failed to restore DCORE %d sync object registers\n",
				dcore_id);
			return;
		}

		addr += DCORE_OFFSET;
	}

	/* Flush all WREG to prevent race */
	val = RREG32(mmDCORE1_SYNC_MNGR_OBJS_SOB_OBJ_0 + offset);
}

static void greco_restore_qm_registers(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hw_queue_properties *queue_props;
	u32 tpc_id, reg_base, val, size;
	int rc, hw_queue_id;
	u64 addr;

	dev_dbg(hdev->dev, "Clearing QM fence counters\n");

	for (hw_queue_id = GRECO_QUEUE_ID_DCORE1_ROT_0_0;
			hw_queue_id >= GRECO_QUEUE_ID_DCORE0_PDMA_0_0;
			hw_queue_id -= NUM_OF_PQ_PER_QMAN) {

		switch (hw_queue_id) {
		case GRECO_QUEUE_ID_DCORE0_MME_0_0:
		case GRECO_QUEUE_ID_DCORE1_MME_0_0:
			queue_props = &prop->hw_queues_props[hw_queue_id];
			if (!hdev->mme_mask || queue_props->slave)
				continue;
			break;
		case GRECO_QUEUE_ID_DCORE0_TPC_0_0
			... GRECO_QUEUE_ID_DCORE0_TPC_4_0:
			tpc_id = (hw_queue_id -
					GRECO_QUEUE_ID_DCORE0_TPC_0_0) >> 2;
			if (!(hdev->asic_prop.tpc_enabled_mask & BIT(tpc_id)))
				continue;
			break;
		case GRECO_QUEUE_ID_DCORE1_TPC_0_0
			... GRECO_QUEUE_ID_DCORE1_TPC_4_0:
			tpc_id = NUM_OF_TPC_PER_DCORE +
				((hw_queue_id -
					GRECO_QUEUE_ID_DCORE1_TPC_0_0) >> 2);
			if (!(hdev->asic_prop.tpc_enabled_mask & BIT(tpc_id)))
				continue;
			break;
		case GRECO_QUEUE_ID_DCORE0_ROT_0_0:
			if (!(hdev->rotator_mask & 0x1))
				continue;
			break;
		case GRECO_QUEUE_ID_DCORE1_ROT_0_0:
			if (!(hdev->rotator_mask & 0x2))
				continue;
			break;
		default:
			break;
		}

		reg_base = greco_qm_blocks_bases[hw_queue_id];

		addr = CFG_BASE + reg_base + QM_CP_FENCE0_CNT_0_OFFSET;
		size = mmDCORE0_PDMA0_QM_CP_BARRIER_CFG -
				mmDCORE0_PDMA0_QM_CP_FENCE0_CNT_0;
		val = 0;
		rc = greco_memset_device_memory(hdev, addr, size, (u64) val, 0);
		if (rc) {
			dev_err(hdev->dev,
				"Failed to restore QM fence counter registers for h/w queue %d\n",
				hw_queue_id);
			return;
		}

		WREG32(reg_base + QM_ARB_CFG_0_OFFSET, 0);
	}

	/* Flush all WREG to prevent race */
	val = RREG32(mmDCORE0_PDMA0_QM_ARB_CFG_0);
}

int greco_context_switch(struct hl_device *hdev, u32 asid)
{
	return 0;
}

void greco_restore_phase_topology(struct hl_device *hdev)
{
}

int greco_debugfs_read_dma(struct hl_device *hdev, u64 addr, u32 size,
				void *blob_addr)
{
	void *host_mem_virtual_addr;
	dma_addr_t host_mem_dma_addr;
	u64 reserved_va_base;
	u32 pos, size_left, size_to_dma;
	struct hl_ctx *ctx;
	int rc = 0;

	/* Fetch the ctx */
	ctx = hl_get_compute_ctx(hdev);
	if (!ctx) {
		dev_err(hdev->dev, "No ctx available\n");
		return -EINVAL;
	}

	/* Allocate buffers for read and for poll */
	host_mem_virtual_addr = hl_asic_dma_alloc_coherent(hdev, SZ_2M, &host_mem_dma_addr,
								GFP_KERNEL | __GFP_ZERO);
	if (host_mem_virtual_addr == NULL) {
		dev_err(hdev->dev, "Failed to allocate memory for KDMA read\n");
		rc = -ENOMEM;
		goto put_ctx;
	}

	/* Reserve VM region on asic side */
	reserved_va_base =
		hl_reserve_va_block(hdev, ctx, HL_VA_RANGE_TYPE_HOST,
				    SZ_2M,
				    HL_MMU_VA_ALIGNMENT_NOT_NEEDED);
	if (!reserved_va_base) {
		dev_err(hdev->dev, "Failed to reserve vmem on asic\n");
		rc = -ENOMEM;
		goto free_data_buffer;
	}

	/* Create mapping on asic side */
	mutex_lock(&hdev->mmu_lock);
	rc = hl_mmu_map_contiguous(ctx, reserved_va_base, host_mem_dma_addr,
				   SZ_2M);
	hl_mmu_invalidate_cache_range(hdev, false,
				      MMU_OP_USERPTR | MMU_OP_SKIP_LOW_CACHE_INV,
				      ctx->asid, reserved_va_base, SZ_2M);
	mutex_unlock(&hdev->mmu_lock);
	if (rc) {
		dev_err(hdev->dev, "Failed to create mapping on asic mmu\n");
		goto unreserve_va;
	}

	/* Enable MMU on KDMA */
	greco_kdma_set_mmbp_asid(hdev, false, ctx->asid);

	pos = 0;
	size_left = size;
	size_to_dma = SZ_2M;

	while (size_left > 0) {
		if (size_left < SZ_2M)
			size_to_dma = size_left;

		rc = greco_send_job_to_kdma(hdev, addr, reserved_va_base, size_to_dma, 0, false);
		if (rc)
			break;

		memcpy(blob_addr + pos, host_mem_virtual_addr, size_to_dma);

		if (size_left <= SZ_2M)
			break;

		pos += SZ_2M;
		addr += SZ_2M;
		size_left -= SZ_2M;
	}

	greco_kdma_set_mmbp_asid(hdev, true, HL_KERNEL_ASID_ID);

	mutex_lock(&hdev->mmu_lock);
	hl_mmu_unmap_contiguous(ctx, reserved_va_base, SZ_2M);
	hl_mmu_invalidate_cache_range(hdev, false, MMU_OP_USERPTR,
				      ctx->asid, reserved_va_base, SZ_2M);
	mutex_unlock(&hdev->mmu_lock);
unreserve_va:
	hl_unreserve_va_block(hdev, ctx, reserved_va_base, SZ_2M);
free_data_buffer:
	hl_asic_dma_free_coherent(hdev, SZ_2M, host_mem_virtual_addr, host_mem_dma_addr);
put_ctx:
	hl_ctx_put(ctx);

	return rc;
}

static void greco_restore_user_registers(struct hl_device *hdev)
{
	greco_restore_sm_registers(hdev);
	greco_restore_qm_registers(hdev);
}

int greco_ctx_init(struct hl_ctx *ctx)
{
	if (ctx->asid == HL_KERNEL_ASID_ID)
		return 0;

	greco_mmu_prepare(ctx->hdev, ctx->asid);
	greco_restore_user_registers(ctx->hdev);

	return greco_internal_cb_pool_init(ctx->hdev, ctx);
}

void greco_ctx_fini(struct hl_ctx *ctx)
{
	if (ctx->asid == HL_KERNEL_ASID_ID)
		return;

	greco_internal_cb_pool_fini(ctx->hdev, ctx);

	/* TODO: remove once func-sim supports hard reset without KDMA */
	if (!(!ctx->hdev->pdev && ctx->hdev->reset_info.in_reset))
		greco_init_reserved_sram(ctx->hdev);
}

int greco_pre_schedule_cs(struct hl_cs *cs)
{
	struct hl_device *hdev = cs->ctx->hdev;
	int index = cs->sequence & (hdev->asic_prop.max_pending_cs - 1);
	int offset = index * 4;
	u64 msix_db_reg = CFG_BASE + mmPCIE_DBI_MSIX_DOORBELL_OFF;
	u32 msix_vec, sync_group_id, mask, mode, sync_value, mon_arm;

	if (!cs_needs_completion(cs))
		return 0;

	/*
	 * First 64 SOB/MON are reserved for driver for QMAN auto completion
	 * mechanism. Each SOB/MON pair are used for a pending CS with the same
	 * cyclic index. The SOB value is increased when each of the CS jobs is
	 * completed. When the SOB reaches the number of CS jobs, the monitor
	 * generates MSI-X interrupt.
	 */

	/* Reset the SOB value */
	WREG32(mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0 + offset, 0);

	/* Configure the monitor to trigger MSI-X interrupt */

	WREG32(mmDCORE0_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0 + offset,
			lower_32_bits(msix_db_reg));
	WREG32(mmDCORE0_SYNC_MNGR_OBJS_MON_PAY_ADDRH_0 + offset,
			upper_32_bits(msix_db_reg));

	msix_vec = (GRECO_IRQ_NUM_CS_FIRST + index) & 0x7FF;
	WREG32(mmDCORE0_SYNC_MNGR_OBJS_MON_PAY_DATA_0 + offset, msix_vec);

	sync_group_id = index / 8;
	mask = ~(1 << (index & 0x7));
	mode = 1; /* comparison mode is "equal to" */
	sync_value = cs->jobs_cnt;
	mon_arm = ((sync_value << DCORE0_SYNC_MNGR_OBJS_MON_ARM_SOD_SHIFT) &
			DCORE0_SYNC_MNGR_OBJS_MON_ARM_SOD_MASK) |
		((mode << DCORE0_SYNC_MNGR_OBJS_MON_ARM_SOP_SHIFT) &
			DCORE0_SYNC_MNGR_OBJS_MON_ARM_SOP_MASK)|
		((mask << DCORE0_SYNC_MNGR_OBJS_MON_ARM_MASK_SHIFT) &
			DCORE0_SYNC_MNGR_OBJS_MON_ARM_MASK_MASK) |
		((sync_group_id << DCORE0_SYNC_MNGR_OBJS_MON_ARM_SID_SHIFT) &
			DCORE0_SYNC_MNGR_OBJS_MON_ARM_SID_MASK);
	WREG32(mmDCORE0_SYNC_MNGR_OBJS_MON_ARM_0 + offset, mon_arm);

	return 0;
}

u32 greco_get_queue_id_for_cq(struct hl_device *hdev, u32 cq_idx)
{
	return cq_idx;
}

u32 greco_get_signal_cb_size(struct hl_device *hdev)
{
	return sizeof(struct packet_msg_short);
}

u32 greco_get_wait_cb_size(struct hl_device *hdev)
{
	return sizeof(struct packet_msg_short) * 4 +
			sizeof(struct packet_fence);
}

u32 greco_gen_signal_cb(struct hl_device *hdev, void *data, u16 sob_id,
		u32 size, bool eb)
{
	struct hl_cb *cb = (struct hl_cb *) data;
	struct packet_msg_short *pkt;
	u32 value, ctl, pkt_size = sizeof(*pkt);

	pkt = (struct packet_msg_short *) (uintptr_t) (cb->kernel_address +
									size);
	memset(pkt, 0, pkt_size);

	/* Inc by 1, Mode ADD */
	value = FIELD_PREP(GRECO_PKT_SHORT_VAL_SOB_SYNC_VAL_MASK, 1);
	value |= FIELD_PREP(GRECO_PKT_SHORT_VAL_SOB_MOD_MASK, 1);

	ctl = FIELD_PREP(GRECO_PKT_SHORT_CTL_ADDR_MASK, sob_id * 4);
	ctl |= FIELD_PREP(GRECO_PKT_SHORT_CTL_BASE_MASK, 1); /* SOB base */
	ctl |= FIELD_PREP(GRECO_PKT_CTL_OPCODE_MASK, PACKET_MSG_SHORT);
	ctl |= FIELD_PREP(GRECO_PKT_CTL_EB_MASK, eb);
	ctl |= FIELD_PREP(GRECO_PKT_CTL_MB_MASK, 1);

	pkt->value = cpu_to_le32(value);
	pkt->ctl = cpu_to_le32(ctl);

	return size + pkt_size;
}

static u32 greco_add_mon_msg_short(struct packet_msg_short *pkt, u32 value,
					u16 addr)
{
	u32 ctl, pkt_size = sizeof(*pkt);

	memset(pkt, 0, pkt_size);

	ctl = FIELD_PREP(GRECO_PKT_SHORT_CTL_ADDR_MASK, addr);
	ctl |= FIELD_PREP(GRECO_PKT_SHORT_CTL_BASE_MASK, 0);  /* MON base */
	ctl |= FIELD_PREP(GRECO_PKT_CTL_OPCODE_MASK, PACKET_MSG_SHORT);
	ctl |= FIELD_PREP(GRECO_PKT_CTL_EB_MASK, 0);
	ctl |= FIELD_PREP(GRECO_PKT_CTL_MB_MASK, 0); /* last pkt MB */

	pkt->value = cpu_to_le32(value);
	pkt->ctl = cpu_to_le32(ctl);

	return pkt_size;
}

static u32 greco_add_arm_monitor_pkt(struct hl_device *hdev,
		struct packet_msg_short *pkt, u16 sob_base, u8 sob_mask,
		u16 sob_val, u16 addr)
{
	u32 ctl, value, pkt_size = sizeof(*pkt);
	u8 mask;

	if (hl_gen_sob_mask(sob_base, sob_mask, &mask)) {
		dev_err(hdev->dev,
			"sob_base %u (mask %#x) is not valid\n",
			sob_base, sob_mask);
		return 0;
	}

	memset(pkt, 0, pkt_size);

	value = FIELD_PREP(GRECO_PKT_SHORT_VAL_MON_SYNC_GID_MASK, sob_base / 8);
	value |= FIELD_PREP(GRECO_PKT_SHORT_VAL_MON_SYNC_VAL_MASK, sob_val);
	value |= FIELD_PREP(GRECO_PKT_SHORT_VAL_MON_MODE_MASK,
			0); /* GREATER OR EQUAL*/
	value |= FIELD_PREP(GRECO_PKT_SHORT_VAL_MON_MASK_MASK, mask);

	ctl = FIELD_PREP(GRECO_PKT_SHORT_CTL_ADDR_MASK, addr);
	ctl |= FIELD_PREP(GRECO_PKT_SHORT_CTL_BASE_MASK, 0); /* MON base */
	ctl |= FIELD_PREP(GRECO_PKT_CTL_OPCODE_MASK, PACKET_MSG_SHORT);
	ctl |= FIELD_PREP(GRECO_PKT_CTL_EB_MASK, 0);
	ctl |= FIELD_PREP(GRECO_PKT_CTL_MB_MASK, 1);

	pkt->value = cpu_to_le32(value);
	pkt->ctl = cpu_to_le32(ctl);

	return pkt_size;
}

static u32 greco_add_fence_pkt(struct packet_fence *pkt)
{
	u32 ctl, cfg, pkt_size = sizeof(*pkt);

	memset(pkt, 0, pkt_size);

	cfg = FIELD_PREP(GRECO_PKT_FENCE_CFG_DEC_VAL_MASK, 1);
	cfg |= FIELD_PREP(GRECO_PKT_FENCE_CFG_TARGET_VAL_MASK, 1);
	cfg |= FIELD_PREP(GRECO_PKT_FENCE_CFG_ID_MASK, 2);

	ctl = FIELD_PREP(GRECO_PKT_CTL_OPCODE_MASK, PACKET_FENCE);
	ctl |= FIELD_PREP(GRECO_PKT_CTL_EB_MASK, 0);
	ctl |= FIELD_PREP(GRECO_PKT_CTL_MB_MASK, 1);

	pkt->cfg = cpu_to_le32(cfg);
	pkt->ctl = cpu_to_le32(ctl);

	return pkt_size;
}

u32 greco_gen_wait_cb(struct hl_device *hdev,
		struct hl_gen_wait_properties *prop)
{
	struct hl_cb *cb = (struct hl_cb *) prop->data;
	void *buf = (void *) (uintptr_t) (cb->kernel_address);

	u64 monitor_base, fence_addr = 0;
	u32 stream_index, size = prop->size;
	u16 msg_addr_offset;

	if (prop->q_idx > GRECO_QUEUE_ID_DCORE1_ROT_0_3) {
		dev_crit(hdev->dev, "wrong queue id %d for wait packet\n",
				prop->q_idx);
		return 0;
	}

	stream_index = prop->q_idx % 4;
	fence_addr = CFG_BASE + greco_qm_blocks_bases[prop->q_idx] +
			QM_FENCE2_OFFSET + stream_index * 4;

	/*
	 * monitor_base should be the content of the base0 address registers,
	 * so it will be added to the msg short offsets
	 */
	monitor_base = mmDCORE0_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0;

	/* First monitor config packet: low address of the sync */
	msg_addr_offset =
		(mmDCORE0_SYNC_MNGR_OBJS_MON_PAY_ADDRL_0 + prop->mon_id * 4) -
				monitor_base;

	size += greco_add_mon_msg_short(buf + size, (u32) fence_addr,
					msg_addr_offset);

	/* Second monitor config packet: high address of the sync */
	msg_addr_offset =
		(mmDCORE0_SYNC_MNGR_OBJS_MON_PAY_ADDRH_0 + prop->mon_id * 4) -
				monitor_base;

	size += greco_add_mon_msg_short(buf + size, (u32) (fence_addr >> 32),
					msg_addr_offset);

	/*
	 * Third monitor config packet: the payload, i.e. what to write when the
	 * sync triggers
	 */
	msg_addr_offset =
		(mmDCORE0_SYNC_MNGR_OBJS_MON_PAY_DATA_0 + prop->mon_id * 4) -
				monitor_base;

	size += greco_add_mon_msg_short(buf + size, 1, msg_addr_offset);

	/* Fourth monitor config packet: bind the monitor to a sync object */
	msg_addr_offset =
		(mmDCORE0_SYNC_MNGR_OBJS_MON_ARM_0 + prop->mon_id * 4) -
				monitor_base;

	size += greco_add_arm_monitor_pkt(hdev, buf + size, prop->sob_base,
			prop->sob_mask, prop->sob_val, msg_addr_offset);

	/* Fence packet */
	size += greco_add_fence_pkt(buf + size);

	return size;
}

void greco_reset_sob(struct hl_device *hdev, void *data)
{
	struct hl_hw_sob *hw_sob = (struct hl_hw_sob *) data;

	dev_dbg(hdev->dev, "reset SOB, q_idx: %d, sob_id: %d\n", hw_sob->q_idx,
		hw_sob->sob_id);

	WREG32(mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0 + hw_sob->sob_id * 4, 0);

	kref_init(&hw_sob->kref);
}

void greco_reset_sob_group(struct hl_device *hdev, u16 sob_group)
{

}

u64 greco_get_device_time(struct hl_device *hdev)
{
	u64 device_time = ((u64) RREG32(mmPSOC_TIMESTAMP_CNTCVU)) << 32;

	return device_time | RREG32(mmPSOC_TIMESTAMP_CNTCVL);
}

int greco_collective_wait_init_cs(struct hl_cs *cs)
{
	return 0;
}

int greco_collective_wait_create_jobs(struct hl_device *hdev,
		struct hl_ctx *ctx, struct hl_cs *cs, u32 wait_queue_id,
		u32 collective_engine_id, u32 encaps_signal_offset)
{
	return -EINVAL;
}

u32 greco_get_dec_base_addr(struct hl_device *hdev, u32 core_id)
{
	u32 base = 0, dcore_id, dec_id;

	if (core_id >= NUMBER_OF_DEC) {
		dev_err(hdev->dev, "Unexpected core number %d for decoder\n", core_id);
		goto out;
	}

	dcore_id = core_id / NUM_OF_DEC_PER_DCORE;
	dec_id = core_id % NUM_OF_DEC_PER_DCORE;
	base = mmDCORE0_DEC0_CMD_BASE + dcore_id * DCORE_OFFSET +
			dec_id * VDEC_OFFSET;
out:
	return base;
}

static int greco_get_hw_block_id(struct hl_device *hdev, u64 block_addr,
				u32 *block_size, u32 *block_id)
{
	struct greco_device *greco = hdev->asic_specific;
	int i;

	for (i = 0 ; i < NUM_USER_MAPPED_BLOCKS ; i++) {
		if (block_addr == CFG_BASE + greco->mapped_blocks[i].address) {
			*block_id = i;
			if (block_size)
				*block_size = greco->mapped_blocks[i].size;
			return 0;
		}
	}

	dev_err(hdev->dev, "Invalid block address %#llx", block_addr);

	return -EINVAL;
}

static int greco_block_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
			u32 block_id, u32 block_size)
{
	struct greco_device *greco = hdev->asic_specific;
	u64 offset_in_bar;
	u64 address;
	int rc;

	if (block_id >= NUM_USER_MAPPED_BLOCKS) {
		dev_err(hdev->dev, "Invalid block id %u", block_id);
		return -EINVAL;
	}

	/* we allow mapping only an entire block */
	if (block_size != greco->mapped_blocks[block_id].size) {
		dev_err(hdev->dev, "Invalid block size %u", block_size);
		return -EINVAL;
	}

	offset_in_bar = CFG_BASE + greco->mapped_blocks[block_id].address - SRAM_BASE_ADDR;

	address = pci_resource_start(hdev->pdev, SRAM_CFG_BAR_ID) + offset_in_bar;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP |
			VM_DONTCOPY | VM_NORESERVE;

	rc = remap_pfn_range(vma, vma->vm_start, address >> PAGE_SHIFT,
				block_size, vma->vm_page_prot);
	if (rc)
		dev_err(hdev->dev, "remap_pfn_range error %d", rc);

	return rc;
}

static void greco_enable_events_from_fw(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	struct cpu_dyn_regs *dyn_regs =
			&hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs;
	/*
	 * TODO:
	 * Since GIC is becoming obsolete, remove GIC support once CI
	 * machines are running latest release (1.0.0) that supports COMMS
	 * Meaning - use dynamic regs only to enable FW events.
	 */
	u32 irq_handler_offset = hdev->asic_prop.dynamic_fw_load ?
			le32_to_cpu(dyn_regs->gic_host_ints_irq) :
			mmGIC_DISTRIBUTOR__5_GICD_SETSPI_NSR;

	if (greco->hw_cap_initialized & HW_CAP_CPU_Q)
		WREG32(irq_handler_offset, greco_irq_map_table
				[GRECO_EVENT_CPU_INTS_REGISTER].cpu_id);
}

int greco_ack_mmu_page_fault_or_access_error(struct hl_device *hdev, u64 mmu_cap_mask)
{
	return -EINVAL;
}

static void greco_get_msi_info(__le32 *table)
{
	table[CPUCP_EVENT_QUEUE_MSI_TYPE] = cpu_to_le32(GRECO_EVENT_QUEUE_MSIX_IDX);
}

int greco_map_pll_idx_to_fw_idx(u32 pll_idx)
{
	switch (pll_idx) {
	case HL_GRECO_PCI_PLL: return PCI_PLL;
	case HL_GRECO_MESH_PLL: return MESH_PLL;
	case HL_GRECO_MME_PLL: return MME_PLL;
	case HL_GRECO_TPC_PLL: return TPC_PLL;
	case HL_GRECO_SRAM_PLL: return SRAM_PLL;
	case HL_GRECO_SIF_PLL: return IF_PLL;
	case HL_GRECO_DDR0_PLL: return DDR_PLL;
	case HL_GRECO_DDR1_PLL: return DDR_PLL;
	case HL_GRECO_VID_PLL: return VID_PLL;
	case HL_GRECO_MMU_PLL: return MMU_PLL;
	default: return -EINVAL;
	}
}

static int greco_gen_sync_to_engine_map(struct hl_device *hdev,
				struct hl_sync_to_engine_map *map)
{
	/* Not implemented */
	return 0;
}

static int greco_monitor_valid(struct hl_mon_state_dump *mon)
{
	/* Not implemented */
	return 0;
}

static int greco_print_single_monitor(char **buf, size_t *size, size_t *offset,
				struct hl_device *hdev,
				struct hl_mon_state_dump *mon)
{
	/* Not implemented */
	return 0;
}


static int greco_print_fences_single_engine(
	struct hl_device *hdev, u64 base_offset, u64 status_base_offset,
	enum hl_sync_engine_type engine_type, u32 engine_id, char **buf,
	size_t *size, size_t *offset)
{
	/* Not implemented */
	return 0;
}


static struct hl_state_dump_specs_funcs greco_state_dump_funcs = {
	.monitor_valid = greco_monitor_valid,
	.print_single_monitor = greco_print_single_monitor,
	.gen_sync_to_engine_map = greco_gen_sync_to_engine_map,
	.print_fences_single_engine = greco_print_fences_single_engine,
};

void greco_state_dump_init(struct hl_device *hdev)
{
	/* Not implemented */
	hdev->state_dump_specs.props = greco_state_dump_specs_props;
	hdev->state_dump_specs.funcs = greco_state_dump_funcs;
}


u32 greco_get_sob_addr(struct hl_device *hdev, u32 sob_id)
{
	return 0;
}

u32 *greco_get_stream_master_qid_arr(void)
{
	return greco_stream_master;
}

int greco_get_monitor_dump(struct hl_device *hdev, void *data)
{
	return -EOPNOTSUPP;
}

int greco_set_dram_properties(struct hl_device *hdev)
{
	return 0;
}

static void greco_check_if_razwi_happened(struct hl_device *hdev)
{
}

static int greco_scheduler_submit_buf(struct hl_device *hdev, u32 cpu_id, u32 queue_id,
					  void *buf, u32 len)
{
	return -EPERM;
}

static void greco_no_fw_monitor(struct hl_device *hdev, bool *stop_monitor)
{
}

static struct attribute *greco_nic_dev_attrs[] = {
	NULL,
};

static void greco_add_device_attr(struct hl_device *hdev,
				struct attribute_group *dev_clk_attr_grp,
				struct attribute_group *dev_vrm_attr_grp,
				struct attribute_group *dev_nic_attr_grp)
{
	hl_sysfs_add_dev_clk_attr(hdev, dev_clk_attr_grp);
	hl_sysfs_add_dev_vrm_attr(hdev, dev_vrm_attr_grp);
	dev_nic_attr_grp->attrs = greco_nic_dev_attrs;
}

static int greco_send_device_activity(struct hl_device *hdev, bool open)
{
	return 0;
}

void greco_fw_security_emulation_init(struct hl_device *hdev)
{
}

void greco_fw_security_emulation_fini(struct hl_device *hdev, bool asic_dirty)
{
}

void greco_set_priv_assertions(struct hl_device *hdev, bool enable)
{
}

static const struct hl_asic_funcs greco_funcs = {
	.early_init = greco_early_init,
	.early_fini = greco_early_fini,
	.late_init = greco_late_init,
	.late_fini = greco_late_fini,
	.sw_init = greco_sw_init,
	.sw_fini = greco_sw_fini,
	.hw_init = greco_hw_init,
	.hw_fini = greco_hw_fini,
	.halt_engines = greco_halt_engines,
	.suspend = greco_suspend,
	.resume = greco_resume,
	.mmap = greco_mmap,
	.ring_doorbell = greco_ring_doorbell,
	.pqe_write = greco_pqe_write,
	.asic_dma_alloc_coherent = greco_dma_alloc_coherent,
	.asic_dma_free_coherent = greco_dma_free_coherent,
	.scrub_device_mem = greco_scrub_device_mem,
	.scrub_device_dram = greco_scrub_device_dram,
	.get_int_queue_base = NULL,
	.test_queues = greco_test_queues,
	.asic_dma_pool_zalloc = greco_dma_pool_zalloc,
	.asic_dma_pool_free = greco_dma_pool_free,
	.cpu_accessible_dma_pool_alloc = greco_cpu_accessible_dma_pool_alloc,
	.cpu_accessible_dma_pool_free = greco_cpu_accessible_dma_pool_free,
	.hl_dma_unmap_sgtable = hl_dma_unmap_sgtable,
	.cs_parser = greco_cs_parser,
	.asic_dma_map_sgtable = hl_dma_map_sgtable,
	.add_end_of_cb_packets = NULL,
	.update_eq_ci = greco_update_eq_ci,
	.context_switch = greco_context_switch,
	.restore_phase_topology = greco_restore_phase_topology,
	.debugfs_read_dma = greco_debugfs_read_dma,
	.add_device_attr = greco_add_device_attr,
	.handle_eqe = greco_handle_eqe,
	.get_events_stat = greco_get_events_stat,
	.read_pte = greco_read_pte,
	.write_pte = greco_write_pte,
	.mmu_invalidate_cache = greco_mmu_invalidate_cache,
	.mmu_invalidate_cache_range = greco_mmu_invalidate_cache_range,
	.mmu_prefetch_cache_range = NULL,
	.send_heartbeat = greco_send_heartbeat,
	.debug_coresight = greco_debug_coresight,
	.is_device_idle = greco_is_device_idle,
	.compute_reset_late_init = greco_compute_reset_late_init,
	.hw_queues_lock = greco_hw_queues_lock,
	.hw_queues_unlock = greco_hw_queues_unlock,
	.get_pci_id = greco_get_pci_id,
	.get_eeprom_data = greco_get_eeprom_data,
	.get_monitor_dump = greco_get_monitor_dump,
	.send_cpu_message = greco_send_cpu_message,
	.nic_init = greco_nic_init,
	.nic_fini = greco_nic_fini,
	.nic_control = greco_nic_control,
	.pci_bars_map = greco_pci_bars_map,
	.init_iatu = greco_init_iatu,
	.rreg = hl_rreg,
	.wreg = hl_wreg,
	.halt_coresight = greco_halt_coresight,
	.ctx_init = greco_ctx_init,
	.ctx_fini = greco_ctx_fini,
	.pre_schedule_cs = greco_pre_schedule_cs,
	.get_queue_id_for_cq = greco_get_queue_id_for_cq,
	.load_firmware_to_device = greco_load_firmware_to_device,
	.load_boot_fit_to_device = greco_load_boot_fit_to_device,
	.get_signal_cb_size = greco_get_signal_cb_size,
	.get_wait_cb_size = greco_get_wait_cb_size,
	.gen_signal_cb = greco_gen_signal_cb,
	.gen_wait_cb = greco_gen_wait_cb,
	.reset_sob = greco_reset_sob,
	.reset_sob_group = greco_reset_sob_group,
	.get_device_time = greco_get_device_time,
	.pb_print_security_errors = greco_pb_print_security_errors,
	.collective_wait_init_cs = greco_collective_wait_init_cs,
	.collective_wait_create_jobs = greco_collective_wait_create_jobs,
	.get_dec_base_addr = greco_get_dec_base_addr,
	.scramble_addr = hl_mmu_scramble_addr,
	.descramble_addr = hl_mmu_descramble_addr,
	.ack_protection_bits_errors = greco_ack_protection_bits_errors,
	.get_hw_block_id = greco_get_hw_block_id,
	.hw_block_mmap = greco_block_mmap,
	.enable_events_from_fw = greco_enable_events_from_fw,
	.ack_mmu_errors = greco_ack_mmu_page_fault_or_access_error,
	.get_msi_info = greco_get_msi_info,
	.map_pll_idx_to_fw_idx = greco_map_pll_idx_to_fw_idx,
	.init_firmware_preload_params = greco_init_firmware_preload_params,
	.init_firmware_loader = greco_init_firmware_loader,
	.init_cpu_scrambler_dram = greco_cpu_init_scrambler_dram,
	.state_dump_init = greco_state_dump_init,
	.get_sob_addr = &greco_get_sob_addr,
	.set_pci_memory_regions = greco_set_pci_memory_regions,
	.get_stream_master_qid_arr = greco_get_stream_master_qid_arr,
	.check_if_razwi_happened = greco_check_if_razwi_happened,
	.scheduler_submit_buf = greco_scheduler_submit_buf,
	.no_fw_monitor = greco_no_fw_monitor,
	.mmu_get_real_page_size = hl_mmu_get_real_page_size,
	.access_dev_mem = hl_access_dev_mem,
	.set_dram_bar_base = greco_set_dram_bar_base,
	.send_device_activity = greco_send_device_activity,
	.read_fetch_memory_block = NULL,
	.fw_security_emulation_init = greco_fw_security_emulation_init,
	.fw_security_emulation_fini = greco_fw_security_emulation_fini,
	.pll_info_get = hl_fw_cpucp_pll_info_get,
	.set_dram_properties = greco_set_dram_properties,
	.set_priv_assertions = greco_set_priv_assertions,
	.set_binning_masks = greco_set_binning_masks,
};

void greco_set_asic_funcs(struct hl_device *hdev)
{
	hdev->asic_funcs = &greco_funcs;
}
