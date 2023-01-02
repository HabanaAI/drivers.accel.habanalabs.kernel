/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2019-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GRECOP_H_
#define GRECOP_H_

#include <uapi/drm/habanalabs_accel.h>
#include "../common/habanalabs.h"
#include "../include/common/hl_boot_if.h"
#include "../include/greco/greco.h"
#include "../include/greco/greco_packets.h"
#include "../include/greco/greco_fw_if.h"
#include "../include/greco/greco_async_events.h"

#define NUMBER_OF_CPU_QUEUES		1

#define NUMBER_OF_DCORE_HW_QUEUES	((NUM_OF_PDMA_PER_DCORE + \
					NUM_OF_DDMA_PER_DCORE + \
					NUM_OF_MME_PER_DCORE + \
					NUM_OF_TPC_PER_DCORE + \
					NUM_OF_ROT_PER_DCORE) * \
					NUM_OF_PQ_PER_QMAN)

#define NUMBER_OF_HW_QUEUES		(NUMBER_OF_DCORE_HW_QUEUES * \
					NUM_OF_DCORES)

#define NUMBER_OF_QUEUES		(NUMBER_OF_CPU_QUEUES + \
					NUMBER_OF_HW_QUEUES)

#define DCORE_QUEUE_ID_OFFSET		(GRECO_QUEUE_ID_DCORE1_PDMA_0_0 - \
					GRECO_QUEUE_ID_DCORE0_PDMA_0_0)


#define DCORE1_DDMA_QUEUE_ID_ADJ	(GRECO_QUEUE_ID_DCORE0_DDMA_0_3 + 1 - \
					GRECO_QUEUE_ID_DCORE0_DDMA_0_0)

#define DCORE1_MME_QUEUE_ID_ADJ		(GRECO_QUEUE_ID_DCORE0_MME_0_3 + 1 - \
					GRECO_QUEUE_ID_DCORE0_MME_0_0)

#define DCORE1_TPC_QUEUE_ID_ADJ		(GRECO_QUEUE_ID_DCORE0_TPC_4_3 + 1 - \
					GRECO_QUEUE_ID_DCORE0_TPC_0_0)

#define NUMBER_OF_DEC			(NUM_OF_DEC_PER_DCORE * NUM_OF_DCORES)

#define GRECO_MAX_PENDING_CS		64

#if !IS_MAX_PENDING_CS_VALID(GRECO_MAX_PENDING_CS)
#error "GRECO_MAX_PENDING_CS must be power of 2 and greater than 1"
#endif

/*
 * Number of MSIX interrupts IDS:
 * Each pending CS has 1 ID
 * The event queue has 1 ID
 */
#define NUMBER_OF_INTERRUPTS		(GRECO_MAX_PENDING_CS + 1)

#define GRECO_STREAM_MASTER_ARR_SIZE	4

#if (NUMBER_OF_INTERRUPTS > MSIX_ENTRIES)
#error "Number of interrupts must be smaller or equal to MSIX_ENTRIES"
#endif

#define NUM_USER_MAPPED_BLOCKS		NUMBER_OF_DEC
/* Within the user mapped array, DEC entries start post all the ARC related
 * entries
 */
#define USR_MAPPED_BLK_DEC_START_IDX	0

#define CORESIGHT_TIMEOUT_USEC		100000		/* 100 ms */

#define GRECO_CPU_TIMEOUT_USEC		60000000	/* 60s */

#define GRECO_BOOT_FIT_REQ_TIMEOUT_USEC	1000000		/* 1s */

#define GRECO_MAX_CLK_FREQ		1500000000ull	/* 1500 MHz */

#define MAX_POWER_DEFAULT		75000		/* 75W */

#define DC_POWER_DEFAULT		20000		/* 20W */

#define GRECO_DRAM_SIZE_16GB		0x0400000000ull
#define GRECO_DRAM_SIZE_32GB		0x0800000000ull
#define GRECO_DRAM_SIZE_48GB		0x0C00000000ull
#define GRECO_DRAM_SIZE_64GB		0x1000000000ull

#define DMA_MAX_TRANSFER_SIZE		U32_MAX

#define GRECO_DEFAULT_CARD_NAME		"HL110"

#define DCORE_OFFSET			(mmDCORE1_TPC0_QM_BASE - \
					mmDCORE0_TPC0_QM_BASE)

#define DMMU_OFFSET			(mmDCORE0_HMMU1_MMU_BASE - \
					mmDCORE0_HMMU0_MMU_BASE)

#define VDEC_OFFSET			(mmDCORE0_VDEC1_BRDG_CTRL_BASE - \
					mmDCORE0_VDEC0_BRDG_CTRL_BASE)

#define QMAN_STREAMS			4

#define GRECO_NUM_RSRVD_COMPLETION_Q_SOBS	64
#define GRECO_NUM_RSRVD_KDMA_COMPLETION_SOBS	1
#define GRECO_RSRVD_KDMA_SOB_IDX		GRECO_NUM_RSRVD_COMPLETION_Q_SOBS

/* 1 SOB reserved for KDMA completion */
#define GRECO_NUM_RSRVD_SOBS \
	(GRECO_NUM_RSRVD_COMPLETION_Q_SOBS + GRECO_NUM_RSRVD_KDMA_COMPLETION_SOBS)
#define GRECO_NUM_RSRVD_COMPLETION_Q_MONITORS	64

#define DCORE_NUM_OF_SOB		\
	(((mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_2047 - \
	mmDCORE0_SYNC_MNGR_OBJS_SOB_OBJ_0) + 4) >> 2)

#define DCORE_NUM_OF_MONITORS		\
	(((mmDCORE0_SYNC_MNGR_OBJS_MON_STATUS_511 - \
	mmDCORE0_SYNC_MNGR_OBJS_MON_STATUS_0) + 4) >> 2)

#define DCORE_NUM_OF_TPCS 5
#define TPC_NUM_OF_KERNEL_TENSORS 16
#define TPC_NUM_OF_QM_TENSORS 16
#define DCORE_TPC_OFFSET (mmDCORE0_TPC1_CFG_BASE - mmDCORE0_TPC0_CFG_BASE)
#define DCORE_PDMA_OFFSET (mmDCORE0_PDMA1_QM_BASE - mmDCORE0_PDMA0_QM_BASE)
#define DCORE_DEC_OFFSET (mmDCORE0_DEC1_VSI_BASE - mmDCORE0_DEC0_VSI_BASE)

#define GRECO_ENGINE_ID_DCORE_OFFSET \
		(GRECO_DCORE1_ENGINE_ID_DDMA - GRECO_DCORE0_ENGINE_ID_DDMA)

#define GRECO_NUM_OF_GLBL_ERR_CAUSE		8

/*
 * RAZWI registers offsets for HBW (from MSTR_IF_RR_<SHRD/PRVT>_HBW_BASE
 */

/* RAZWI captured aw addr high (offset 0xF8) */
#define RR_HBW_AW_RAZWI_HI (mmPSOC_MSTR_IF_RR_SHRD_HBW_AW_RAZWI_HI - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured aw addr low (offset 0xFC) */
#define RR_HBW_AW_RAZWI_LO (mmPSOC_MSTR_IF_RR_SHRD_HBW_AW_RAZWI_LO - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured ar addr high (offset 0x100) */
#define RR_HBW_AR_RAZWI_HI (mmPSOC_MSTR_IF_RR_SHRD_HBW_AR_RAZWI_HI - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured ar addr low (offset 0x104) */
#define RR_HBW_AR_RAZWI_LO (mmPSOC_MSTR_IF_RR_SHRD_HBW_AR_RAZWI_LO - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured aw XY coordinates (offset 0x108) */
#define RR_HBW_AW_RAZWI_XY (mmPSOC_MSTR_IF_RR_SHRD_HBW_AW_RAZWI_XY - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI captured ar XY coordinates (offset 0x10C) */
#define RR_HBW_AR_RAZWI_XY (mmPSOC_MSTR_IF_RR_SHRD_HBW_AR_RAZWI_XY - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI occurred due to write access (offset 0x110) */
#define RR_HBW_AW_RAZWI_HAPPENED \
			(mmPSOC_MSTR_IF_RR_SHRD_HBW_AW_RAZWI_HAPPENED - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* RAZWI occurred due to read access (offset 0x114) */
#define RR_HBW_AR_RAZWI_HAPPENED \
			(mmPSOC_MSTR_IF_RR_SHRD_HBW_AR_RAZWI_HAPPENED - \
				mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/* Enable RAZWI response with error indication */
#define RR_HBW_RAZWI_ERR_RESP (mmPSOC_MSTR_IF_RR_SHRD_HBW_RAZWI_ERR_RESP - \
					mmPSOC_MSTR_IF_RR_SHRD_HBW_BASE)

/*
 * RAZWI registers offsets for LBW (from MSTR_IF_RR_<SHRD/PRVT>_LBW_BASE
 */

/* RAZWI captured aw addr (offset 0x130) */
#define RR_LBW_AW_RAZWI (mmPSOC_MSTR_IF_RR_SHRD_LBW_AW_RAZWI - \
				mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE)

/* RAZWI captured ar addr (offset 0x134) */
#define RR_LBW_AR_RAZWI (mmPSOC_MSTR_IF_RR_SHRD_LBW_AR_RAZWI - \
				mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE)

/* RAZWI captured aw XY coordinates (offset 0x138) */
#define RR_LBW_AW_RAZWI_XY (mmPSOC_MSTR_IF_RR_SHRD_LBW_AW_RAZWI_XY - \
				mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE)

/* RAZWI captured ar XY coordinates (offset 0x13C) */
#define RR_LBW_AR_RAZWI_XY (mmPSOC_MSTR_IF_RR_SHRD_LBW_AR_RAZWI_XY - \
				mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE)

/* RAZWI occurred due to write access (offset 0x140) */
#define RR_LBW_AW_RAZWI_HAPPENED \
			(mmPSOC_MSTR_IF_RR_SHRD_LBW_AW_RAZWI_HAPPENED - \
				mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE)

/* RAZWI occurred due to read access (offset 0x144) */
#define RR_LBW_AR_RAZWI_HAPPENED       \
			(mmPSOC_MSTR_IF_RR_SHRD_LBW_AR_RAZWI_HAPPENED - \
				mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE)

/* Enable RAZWI response (offset 0x148) */
#define RR_LBW_RAZWI_ERR_RESP (mmPSOC_MSTR_IF_RR_SHRD_LBW_RAZWI_ERR_RESP - \
				mmPSOC_MSTR_IF_RR_SHRD_LBW_BASE)

/* DRAM Memory Map */

#define CPU_FW_IMAGE_SIZE	0x10000000	/* 256MB */
#define MMU_PAGE_TABLES_SIZE	0x10000000	/* 256MB */

#define CPU_FW_IMAGE_ADDR	DRAM_PHYS_BASE
#define MMU_PAGE_TABLES_ADDR	(CPU_FW_IMAGE_ADDR + CPU_FW_IMAGE_SIZE)
#define DRAM_DRIVER_END_ADDR	(MMU_PAGE_TABLES_ADDR + MMU_PAGE_TABLES_SIZE)

#define DRAM_BASE_ADDR_USER	(DRAM_PHYS_BASE + 0x20000000)

#if (DRAM_DRIVER_END_ADDR > DRAM_BASE_ADDR_USER)
#error "Driver must reserve no more than 512MB"
#endif

#define SRAM_MME_BASE_OFFSET	0
#define SRAM_USER_BASE_OFFSET	GRECO_DRIVER_SRAM_RESERVED_SIZE_FROM_START

/*
 * Virtual address space.
 *
 * Bit 49:48 of a Host VA must be '10', and '01' for a DRAM address.
 *
 * The DRAM size is 64GB at most.
 * In order to distinct between physical and virtual addresses - the DRAM VA
 * range starts at 2^36.
 *
 * We limit the VA range at bit 47 because it has special meaning:
 * - Host address - Indicates the first hop in TLB lookup (0/1 - hop4/3).
 * - DRAM address - Indicates the DCORE number in case of DCORE separation.
 * The value of the bit will be set accordingly, concatenated to the allocated
 * VA from within the range, so the actual range is ~twofold.
 */

#define VA_HOST_SPACE_PAGE_START	0x2000000000000ull /* 512TB */
#define VA_HOST_SPACE_PAGE_END		0x2800000000000ull /* 640TB */

#define VA_HOST_SPACE_HPAGE_START	0x2800000000000ull /* 640TB */
#define VA_HOST_SPACE_HPAGE_END		0x3000000000000ull /* 768TB */

#define VA_HOST_SPACE_PAGE_SIZE		(VA_HOST_SPACE_PAGE_END - \
					VA_HOST_SPACE_PAGE_START) /* 128TB */

#define VA_HOST_SPACE_HPAGE_SIZE	(VA_HOST_SPACE_HPAGE_END - \
				VA_HOST_SPACE_HPAGE_START) /* 128TB */

#define VA_HOST_SPACE_SIZE		(VA_HOST_SPACE_PAGE_SIZE + \
				VA_HOST_SPACE_HPAGE_SIZE) /* 256TB */

#define HOST_SPACE_INTERNAL_CB_SZ	SZ_2M

#define VA_DRAM_SPACE_DCORE0_START	0x1001000000000ull /* 256TB + 64GB */
#define VA_DRAM_SPACE_DCORE0_END	0x1800000000000ull /* 384TB */

#define VA_DRAM_SPACE_DCORE1_START	0x1801000000000ull /* 384TB + 64GB */
#define VA_DRAM_SPACE_DCORE1_END	0x2000000000000ull /* 512TB */

#define VA_DRAM_SPACE_DCORE0_SIZE	(VA_DRAM_SPACE_DCORE0_END - \
				VA_DRAM_SPACE_DCORE0_START) /* 128TB - 64GB */

#define VA_DRAM_SPACE_DCORE1_SIZE	(VA_DRAM_SPACE_DCORE1_END - \
				VA_DRAM_SPACE_DCORE1_START) /* 128TB - 64GB */

#define VA_DRAM_SPACE_SIZE		(VA_DRAM_SPACE_DCORE0_SIZE + \
				VA_DRAM_SPACE_DCORE1_SIZE) /* 256TB - 128GB */

#define HW_CAP_PLL			BIT_ULL(0)
#define HW_CAP_DRAM			BIT_ULL(1)
#define HW_CAP_PMMU			BIT_ULL(2)
#define HW_CAP_CPU			BIT_ULL(3)
#define HW_CAP_MSIX			BIT_ULL(4)

#define HW_CAP_CPU_Q			BIT_ULL(5)
#define HW_CAP_CPU_Q_SHIFT		5

#define HW_CAP_CLK_GATE			BIT_ULL(6)
#define HW_CAP_SRAM_SCRAMBLER		BIT_ULL(7)

#define HW_CAP_DRAM_SCRAMBLER_HW_RESET	BIT_ULL(8)
#define HW_CAP_DRAM_SCRAMBLER_SW_RESET	BIT_ULL(9)
#define HW_CAP_DRAM_SCRAMBLER_MASK	GENMASK_ULL(9, 8)
#define HW_CAP_DRAM_SCRAMBLER_SHIFT	8

#define HW_CAP_DCORE0_DMMU0		BIT_ULL(10)
#define HW_CAP_DCORE0_DMMU1		BIT_ULL(11)
#define HW_CAP_DCORE1_DMMU0		BIT_ULL(12)
#define HW_CAP_DCORE1_DMMU1		BIT_ULL(13)
#define HW_CAP_DMMU_MASK		GENMASK_ULL(13, 10)
#define HW_CAP_DMMU_SHIFT		10

#define HW_CAP_MMU_MASK			(HW_CAP_PMMU | HW_CAP_DMMU_MASK)

#define HW_CAP_DCORE0_PDMA0		BIT_ULL(14)
#define HW_CAP_DCORE0_PDMA1		BIT_ULL(15)
#define HW_CAP_DCORE1_PDMA0		BIT_ULL(16)
#define HW_CAP_DCORE1_PDMA1		BIT_ULL(17)
#define HW_CAP_PDMA_MASK		GENMASK_ULL(17, 14)
#define HW_CAP_PDMA_SHIFT		14

#define HW_CAP_DCORE0_DDMA0		BIT_ULL(18)
#define HW_CAP_DCORE1_DDMA0		BIT_ULL(19)
#define HW_CAP_DDMA_MASK		GENMASK_ULL(19, 18)
#define HW_CAP_DDMA_SHIFT		18

#define HW_CAP_DCORE0_KDMA0		BIT_ULL(20)
#define HW_CAP_DCORE1_KDMA0		BIT_ULL(21)
#define HW_CAP_KDMA_MASK		GENMASK_ULL(21, 20)
#define HW_CAP_KDMA_SHIFT		20

#define HW_CAP_DCORE0_MME0		BIT_ULL(22)
#define HW_CAP_DCORE1_MME0		BIT_ULL(23)
/* DCORE1 MME is in slave mode, do not advertise it */
#define HW_CAP_MME_MASK			GENMASK_ULL(22, 22)
#define HW_CAP_MME_SHIFT		22

#define HW_CAP_DCORE0_TPC0		BIT_ULL(24)
#define HW_CAP_DCORE0_TPC1		BIT_ULL(25)
#define HW_CAP_DCORE0_TPC2		BIT_ULL(26)
#define HW_CAP_DCORE0_TPC3		BIT_ULL(27)
#define HW_CAP_DCORE0_TPC4		BIT_ULL(28)
#define HW_CAP_DCORE1_TPC0		BIT_ULL(29)
#define HW_CAP_DCORE1_TPC1		BIT_ULL(30)
#define HW_CAP_DCORE1_TPC2		BIT_ULL(31)
#define HW_CAP_DCORE1_TPC3		BIT_ULL(32)
#define HW_CAP_DCORE1_TPC4		BIT_ULL(33)
#define HW_CAP_TPC_MASK			GENMASK_ULL(33, 24)
#define HW_CAP_TPC_SHIFT		24

#define HW_CAP_DCORE0_ROT0		BIT_ULL(34)
#define HW_CAP_DCORE1_ROT0		BIT_ULL(35)
#define HW_CAP_ROT_MASK			GENMASK_ULL(35, 34)
#define HW_CAP_ROT_SHIFT		34

#define HW_CAP_DCORE0_DEC0	36
#define HW_CAP_DCORE0_DEC1	37
#define HW_CAP_DCORE0_DEC2	38
#define HW_CAP_DCORE0_DEC3	39
#define HW_CAP_DCORE0_DEC4	40
#define HW_CAP_DCORE1_DEC0	41
#define HW_CAP_DCORE1_DEC1	42
#define HW_CAP_DCORE1_DEC2	43
#define HW_CAP_DCORE1_DEC3	44
#define HW_CAP_DCORE1_DEC4	45
#define HW_CAP_DEC_MASK		GENMASK(45, 36)
#define HW_CAP_DEC_SHIFT	36

#define GRECO_ARC_PCI_MSB_ADDR(addr)	(((addr) & GENMASK_ULL(49, 28)) >> 28)

enum greco_dma_core_id {
	DMA_CORE_ID_DCORE0_PDMA0,
	DMA_CORE_ID_DCORE0_PDMA1,
	DMA_CORE_ID_DCORE0_DDMA,
	DMA_CORE_ID_DCORE0_KDMA,
	DMA_CORE_ID_DCORE1_PDMA0,
	DMA_CORE_ID_DCORE1_PDMA1,
	DMA_CORE_ID_DCORE1_DDMA,
	DMA_CORE_ID_DCORE1_KDMA,
	DMA_CORE_ID_SIZE
};

enum greco_tpc_id {
	TPC_ID_DCORE0_TPC0,
	TPC_ID_DCORE0_TPC1,
	TPC_ID_DCORE0_TPC2,
	TPC_ID_DCORE0_TPC3,
	TPC_ID_DCORE0_TPC4,
	TPC_ID_DCORE1_TPC0,
	TPC_ID_DCORE1_TPC1,
	TPC_ID_DCORE1_TPC2,
	TPC_ID_DCORE1_TPC3,
	TPC_ID_DCORE1_TPC4,
	TPC_ID_SIZE,
};

enum greco_mme_id {
	MME_ID_DCORE0,
	MME_ID_DCORE1,
	MME_ID_SIZE,
};

enum greco_rotator_id {
	ROTATOR_ID_DCORE0,
	ROTATOR_ID_DCORE1,
	ROTATOR_ID_SIZE,
};

enum greco_irq_num {
	GRECO_IRQ_NUM_EVENT_QUEUE = GRECO_EVENT_QUEUE_MSIX_IDX,
	GRECO_IRQ_NUM_DCORE0_DEC0_NRM,
	GRECO_IRQ_NUM_DCORE0_DEC0_ABNRM,
	GRECO_IRQ_NUM_DCORE0_DEC1_NRM,
	GRECO_IRQ_NUM_DCORE0_DEC1_ABNRM,
	GRECO_IRQ_NUM_DCORE0_DEC2_NRM,
	GRECO_IRQ_NUM_DCORE0_DEC2_ABNRM,
	GRECO_IRQ_NUM_DCORE0_DEC3_NRM,
	GRECO_IRQ_NUM_DCORE0_DEC3_ABNRM,
	GRECO_IRQ_NUM_DCORE0_DEC4_NRM,
	GRECO_IRQ_NUM_DCORE0_DEC4_ABNRM,
	GRECO_IRQ_NUM_DCORE1_DEC0_NRM,
	GRECO_IRQ_NUM_DCORE1_DEC0_ABNRM,
	GRECO_IRQ_NUM_DCORE1_DEC1_NRM,
	GRECO_IRQ_NUM_DCORE1_DEC1_ABNRM,
	GRECO_IRQ_NUM_DCORE1_DEC2_NRM,
	GRECO_IRQ_NUM_DCORE1_DEC2_ABNRM,
	GRECO_IRQ_NUM_DCORE1_DEC3_NRM,
	GRECO_IRQ_NUM_DCORE1_DEC3_ABNRM,
	GRECO_IRQ_NUM_DCORE1_DEC4_NRM,
	GRECO_IRQ_NUM_DCORE1_DEC4_ABNRM,
	GRECO_IRQ_NUM_CS_FIRST = (MSIX_ENTRIES - GRECO_MAX_PENDING_CS),
	GRECO_IRQ_NUM_CS_LAST = (MSIX_ENTRIES - 1)
};

struct greco_pll_params {
	u32	div_cfg;
	u32	div_fact[4];
	u32	div_sel[4];
};

/**
 * struct greco_device - ASIC specific manage structure.
 * @cpucp_info_get: get information on device from CPU CP
 * @cs_irq_info_arr: IRQ info array for CS completion interrupts.
 * @hw_queues_lock: protects the H/W queues from concurrent access.
 * @hw_queues_lock_mutex: used by simulator instead of hw_queues_lock.
 * @mapped_blocks: array that holds the base address and size of all blocks
 *                 the user can map.
 * @scratchpad_kernel_address: general purpose PAGE_SIZE contiguous memory,
 *                             this memory region should be write-only.
 *                             currently used for HBW QMAN writes which is
 *                             redundant.
 * @scratchpad_bus_address: scratchpad bus address
 * @dram_bar_cur_addr: current address of DRAM PCI bar.
 * @hw_cap_initialized: This field contains a bit per H/W engine. When that
 *                      engine is initialized, that bit is set by the driver to
 *                      signal we can use this engine in later code paths.
 *                      Each bit is cleared upon reset of its corresponding H/W
 *                      engine.
 * @events: array that holds all received events that are defined valid.
 * @events_stat: array that holds histogram of all received events.
 * @events_stat_aggregate: same as events_stat but doesn't get cleared on reset
 * @num_of_valid_events: used to hold the number of valid events.
 */
struct greco_device {
	int (*cpucp_info_get)(struct hl_device *hdev);
	struct hl_cs_irq_info cs_irq_info_arr[GRECO_MAX_PENDING_CS];

	/* TODO: remove hw_queues_lock after moving to scheduler code */
	spinlock_t	hw_queues_lock;
	struct mutex	hw_queues_lock_mutex;

	struct user_mapped_block	mapped_blocks[NUM_USER_MAPPED_BLOCKS];

	void		*scratchpad_kernel_address;
	dma_addr_t	scratchpad_bus_address;

	u64		dram_bar_cur_addr;

	u64		hw_cap_initialized;
	u32		events[GRECO_EVENT_SIZE];
	u32		events_stat[GRECO_EVENT_SIZE];
	u32		events_stat_aggregate[GRECO_EVENT_SIZE];
	u32		num_of_valid_events;
};

/*
 * Types of the Greco IP blocks, used by special blocks iterator.
 * Required for scenarios where only particular block types can be
 * addressed (e.g., special PLDM images).
 */
enum greco_block_types {
	GRECO_BLOCK_TYPE_PLL,
	GRECO_BLOCK_TYPE_RTR,
	GRECO_BLOCK_TYPE_CPU,
	GRECO_BLOCK_TYPE_HIF,
	GRECO_BLOCK_TYPE_XIF,
	GRECO_BLOCK_TYPE_PCIE,
	GRECO_BLOCK_TYPE_PCIE_PMA,
	GRECO_BLOCK_TYPE_PDMA,
	GRECO_BLOCK_TYPE_PMMU,
	GRECO_BLOCK_TYPE_PSOC,
	GRECO_BLOCK_TYPE_ROT,
	GRECO_BLOCK_TYPE_DEC,
	GRECO_BLOCK_TYPE_MME,
	GRECO_BLOCK_TYPE_SYNC_MNGR,
	GRECO_BLOCK_TYPE_STLB,
	GRECO_BLOCK_TYPE_TPC,
	GRECO_BLOCK_TYPE_HMMU,
	GRECO_BLOCK_TYPE_SRAM,
	GRECO_BLOCK_TYPE_KDMA,
	GRECO_BLOCK_TYPE_MAX
};

int greco_scrub_device_dram(struct hl_device *hdev, u64 val);
int greco_scrub_device_mem(struct hl_device *hdev);
void greco_set_meminfo(struct hl_device *hdev, u32 sram_size);
int greco_set_fixed_properties(struct hl_device *hdev);
void greco_init_security(struct hl_device *hdev);
void greco_init_kdma(struct hl_device *hdev);
void greco_init_pdma(struct hl_device *hdev);
void greco_init_ddma(struct hl_device *hdev);
void greco_init_mme(struct hl_device *hdev);
void greco_init_tpc(struct hl_device *hdev);
void greco_init_rotator(struct hl_device *hdev);
void greco_init_dec(struct hl_device *hdev);
int greco_alloc_cpu_accessible_dma_mem(struct hl_device *hdev);

int greco_test_queues(struct hl_device *hdev);
void *greco_cpu_accessible_dma_pool_alloc(struct hl_device *hdev, size_t size,
						dma_addr_t *dma_handle);
void greco_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size,
					void *vaddr);

void greco_ring_doorbell(struct hl_device *hdev, u32 hw_queue_id, u32 pi);
int greco_cs_parser(struct hl_device *hdev, struct hl_cs_parser *parser);
u32 greco_get_dma_desc_list_size(struct hl_device *hdev,
					struct sg_table *sgt);

int greco_send_heartbeat(struct hl_device *hdev);
int greco_send_cpu_message(struct hl_device *hdev, u32 *msg, u16 len,
				u32 timeout, u64 *result);

int greco_nic_init(struct hl_device *hdev);
void greco_nic_fini(struct hl_device *hdev);
int greco_nic_control(struct hl_device *hdev, u32 op, void *input,
			void *output, struct hl_ctx *ctx);

int greco_context_switch(struct hl_device *hdev, u32 asid);
void greco_restore_phase_topology(struct hl_device *hdev);
void greco_pqe_write(struct hl_device *hdev, __le64 *pqe, struct hl_bd *bd);
int greco_ctx_init(struct hl_ctx *ctx);
void greco_ctx_fini(struct hl_ctx *ctx);
int greco_pre_schedule_cs(struct hl_cs *cs);

int greco_compute_reset_late_init(struct hl_device *hdev);
u32 greco_get_queue_id_for_cq(struct hl_device *hdev, u32 cq_idx);

int greco_debug_coresight(struct hl_device *hdev, struct hl_ctx *ctx, void *data);
void greco_halt_coresight(struct hl_device *hdev, struct hl_ctx *ctx);
int greco_mmu_init(struct hl_device *hdev);
int greco_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard, u32 flags);
int greco_mmu_invalidate_cache_range(struct hl_device *hdev, bool is_hard,
					u32 flags, u32 asid, u64 va, u64 size);
int greco_mmu_clear_pgt_range(struct hl_device *hdev);
void greco_update_eq_ci(struct hl_device *hdev, u32 val);
void *greco_get_events_stat(struct hl_device *hdev, bool aggregate, u32 *size);
void greco_handle_eqe(struct hl_device *hdev, struct hl_eq_entry *eq_entry);
void greco_ack_protection_bits_errors(struct hl_device *hdev);
void greco_pb_print_security_errors(struct hl_device *hdev, u32 block_addr,
		u32 cause, u32 offended_addr);
int greco_map_pll_idx_to_fw_idx(u32 pll_idx);
void greco_fw_security_emulation_init(struct hl_device *hdev);
void greco_fw_security_emulation_fini(struct hl_device *hdev, bool asic_dirty);

void greco_stop_rotator_qmans(struct hl_device *hdev);
void greco_stop_mme_qmans(struct hl_device *hdev);
void greco_stop_tpc_qmans(struct hl_device *hdev);
void greco_stop_dcore_dma_qmans(struct hl_device *hdev);
void greco_stop_pci_dma_qmans(struct hl_device *hdev);
void greco_pci_dma_stall(struct hl_device *hdev);
void greco_dcore_dma_stall(struct hl_device *hdev);
void greco_tpc_stall(struct hl_device *hdev);
void greco_mme_stall(struct hl_device *hdev);
void greco_rotator_stall(struct hl_device *hdev);
void greco_stop_dec(struct hl_device *hdev);
void greco_disable_rotator_qmans(struct hl_device *hdev);
void greco_disable_mme_qmans(struct hl_device *hdev);
void greco_disable_tpc_qmans(struct hl_device *hdev);
void greco_disable_dcore_dma_qmans(struct hl_device *hdev);
void greco_disable_pci_dma_qmans(struct hl_device *hdev);
void greco_user_mapped_blocks_init(struct hl_device *hdev);
void greco_user_interrupt_setup(struct hl_device *hdev);

/* Functions exported for bring-up support */
int greco_init_pll(struct hl_device *hdev);
int greco_init_dram(struct hl_device *hdev);
void greco_dram_shift_cfg(struct hl_device *hdev);
void greco_enable_clock_gating(struct hl_device *hdev);

void greco_kdma_e2e_init(struct hl_device *hdev);
int greco_pldm_init_cpu(struct hl_device *hdev);
void greco_init_scrambler_sram(struct hl_device *hdev);
void greco_cpu_init_scrambler_dram(struct hl_device *hdev);
void greco_init_scrambler_dram(struct hl_device *hdev);
void greco_pre_hw_init(struct hl_device *hdev);
void greco_init_golden_registers(struct hl_device *hdev);
void greco_set_pci_memory_regions(struct hl_device *hdev);
int greco_init_cpu_queues(struct hl_device *hdev, u32 cpu_timeout);
void greco_late_fini(struct hl_device *hdev);
int greco_load_firmware_to_device(struct hl_device *hdev);
u32 greco_get_signal_cb_size(struct hl_device *hdev);
u32 greco_get_wait_cb_size(struct hl_device *hdev);
u32 greco_gen_signal_cb(struct hl_device *hdev, void *data, u16 sob_id,
		u32 size, bool eb);
u32 greco_gen_wait_cb(struct hl_device *hdev,
		struct hl_gen_wait_properties *prop);
u32 greco_get_sob_addr(struct hl_device *hdev, u32 sob_id);
u32 *greco_get_stream_master_qid_arr(void);
int greco_get_monitor_dump(struct hl_device *hdev, void *data);
int greco_set_dram_properties(struct hl_device *hdev);
int greco_set_binning_masks(struct hl_device *hdev);
void greco_reset_sob(struct hl_device *hdev, void *data);
void greco_reset_sob_group(struct hl_device *hdev, u16 sob_group);
u64 greco_get_device_time(struct hl_device *hdev);
int greco_init_reserved_sram(struct hl_device *hdev);
int greco_collective_wait_init_cs(struct hl_cs *cs);
int greco_collective_wait_create_jobs(struct hl_device *hdev,
		struct hl_ctx *ctx, struct hl_cs *cs, u32 wait_queue_id,
		u32 collective_engine_id, u32 encaps_signal_offset);
u32 greco_get_dec_base_addr(struct hl_device *hdev, u32 core_id);
int greco_ack_mmu_page_fault_or_access_error(struct hl_device *hdev,
							u64 mmu_cap_mask);
int greco_debugfs_read_dma(struct hl_device *hdev, u64 addr, u32 size,
				void *blob_addr);
void greco_state_dump_init(struct hl_device *hdev);

/* Greco declarations for simulator */
void greco_set_priv_assertions(struct hl_device *hdev, bool enable);

#endif /* GRECOP_H_ */
