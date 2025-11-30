/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2021-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI3P_H_
#define GAUDI3P_H_

#include <uapi/drm/habanalabs_accel.h>
#include "../common/habanalabs.h"
#include <linux/habanalabs/hl_boot_if.h>
#include "../include/gaudi3/gaudi3.h"
#include "../include/gaudi3/gaudi3_packets.h"
#include <linux/netdevice.h>
#include "../include/gaudi3/arc/gaudi3_arc_common_packets.h"

#define GAUDI3_CPU_TIMEOUT_USEC		30000000	/* 30s */

#define GAUDI3_DEFAULT_CARD_NAME	"GAUDI3"

#define HW_CAP_PDMA_MASK		GENMASK(23, 0)

#define HW_CAP_TPC_SHIFT		0
#define HW_CAP_TPC_MASK			GENMASK_ULL(63, 0)

#define HW_CAP_DEC_SHIFT		0
#define HW_CAP_DEC_MASK			GENMASK_ULL(15, 0)

#define HW_CAP_NIC_SHIFT		0
#define HW_CAP_NIC_MASK			GENMASK_ULL(11, 0)

#define HW_CAP_PLL			BIT_ULL(0)
#define HW_CAP_PMMU			BIT_ULL(1)
#define HW_CAP_MSIX			BIT_ULL(2)
#define HW_CAP_PFQ			BIT_ULL(3)
#define HW_CAP_CPU_Q			BIT_ULL(4)
#define HW_CAP_NIC_DRV			BIT_ULL(5)
#define HW_CAP_HDCORE0_HMMU		BIT_ULL(6)
#define HW_CAP_HDCORE1_HMMU		BIT_ULL(7)
#define HW_CAP_HDCORE2_HMMU		BIT_ULL(8)
#define HW_CAP_HDCORE3_HMMU		BIT_ULL(9)
#define HW_CAP_HDCORE4_HMMU		BIT_ULL(10)
#define HW_CAP_HDCORE5_HMMU		BIT_ULL(11)
#define HW_CAP_HDCORE6_HMMU		BIT_ULL(12)
#define HW_CAP_HDCORE7_HMMU		BIT_ULL(13)
#define HW_CAP_HMMU_MASK		GENMASK_ULL(13, 6)
#define HW_CAP_HMMU_SHIFT		6
#define HW_CAP_MME_MASK			GENMASK_ULL(21, 14)
#define HW_CAP_MME_SHIFT		14
#define HW_CAP_ROT_MASK			GENMASK_ULL(29, 22)
#define HW_CAP_ROT_SHIFT		22
#define HW_CAP_DRAM			BIT_ULL(30)
#define HW_CAP_EDMA_MASK		GENMASK_ULL(38, 31)
#define HW_CAP_EDMA_SHIFT		31
#define HW_CAP_HBM_SCRAMBLER_MASK	BIT_ULL(39)
#define HW_CAP_CPU			BIT_ULL(40)
#define HW_CAP_CBC			BIT_ULL(41)
#define HW_CAP_SRAM_SCRAMBLER_MASK	BIT_ULL(42)
#define HW_CAP_D2D			BIT_ULL(43)
#define HW_CAP_REGULATOR_MASK		BIT_ULL(44)
#define HW_CAP_CREDITS_MASK		BIT_ULL(45)
#define HW_CAP_QOS_MASK			BIT_ULL(46)
#define HW_CAP_CACHE_MASK		BIT_ULL(47)
#define HW_CAP_SET_CACHE_MODE_MASK	BIT_ULL(48)
#define HW_CAP_CSLICE			BIT_ULL(49)

#define HW_CAP_MMU_MASK			(HW_CAP_PMMU | HW_CAP_HMMU_MASK)
#define HW_CAP_SCRAMBLER_MASK		(HW_CAP_HBM_SCRAMBLER_MASK | HW_CAP_SRAM_SCRAMBLER_MASK)

#define KDMA_CH_ID			23

/* In Gaudi3, there are two separated identical groups of SOBs/MONs under the same SM block */
#define HDCORE_NUM_OF_SOB_PER_GRP	8192
#define HDCORE_NUM_OF_MON_PER_GRP	1024

#define HDCORE_NUM_OF_CQ		64

/* Total number of decoders */
#define NUMBER_OF_DEC			16

/* Total number of edmas */
#define NUMBER_OF_EDMA 8

/* Total number of tpcs */
#define NUMBER_OF_TPC 64

/* Total number of mmes */
#define NUMBER_OF_MME 8

#define NUM_OF_MME_EU_PER_HDCORE	2
#define NUM_OF_MME_SBTE_PER_EU		4

#define NUM_OF_TPC_THREADS		4
#define NUM_OF_TPC_TENSORS		16

#define NUM_HBM_PER_DIE			4
#define NUM_MCS_PER_HBM			8
#define SINGLE_HBM_SIZE			SZ_16G

#define GAUDI3_NIC_CLK_FREQ		533000000ull	/* 533 MHz */

#define GAUDI3_FPGA_CPU_TIMEOUT_USEC		100000000	/* 100s */
#define GAUDI3_PLDM_CPU_TIMEOUT_USEC		120000000	/* 120s */
#define GAUDI3_PLDM_FIT_CPU_TIMEOUT_USEC	3600000000	/* 3600s */
#define GAUDI3_CPU_TIMEOUT_USEC			30000000	/* 30s */
#define GAUDI3_BOOT_FIT_REQ_TIMEOUT_USEC	10000000	/* 10s */
#define GAUDI3_PREBOOT_REQ_TIMEOUT_USEC		25000000	/* 25s */
#define GAUDI3_PREBOOT_EXT_REQ_TIMEOUT_USEC	85000000	/* 85s */
#define GAUDI3_PLDM_BOOT_FIT_REQ_TIMEOUT_USEC	1800000000	/* 1800s */
#define GAUDI3_BOOT_FIT_FILE	"habanalabs/gaudi3/gaudi3-boot-fit.itb"

/*
 * AxCACHE[0] - WT/WB indication (not used in H9)
 * AxCACHE[1] - Skip cache indication (0: skip, 1: do not skip))
 * AxCACHE[2] - allocH indication
 * AxCACHE[3] - allocD indication
 */
#define AXCACHE_NO_ALLOC		0x3

#define GAUDI3_ARC_PCI_MSB_ADDR(addr)	(((addr) & GENMASK_ULL(49, 28)) >> 28)

#define MAX_BINNED_TPCS_PER_DCORE	1
#define MAX_BINNED_DECODERS_PER_DIE	1
#define MAX_BINNED_ROTATORS_PER_DIE	1

/* H/W blocks that user can mmap:
 * - all ARCs DCCMs
 * - ARC schedulers ACP/AF blocks
 * - NIC UMR blocks
 * - PDMA CH_A blocks
 * - Decoder VCMD blocks
 * - CBC_USER block of DIE0
 * - SYNC_MNGR_OBJS0/SYNC_MNGR_OBJS1/GLBL blocks (for DIE0.HD0, only SYNC_MNGR_OBJS1)
 * - NUM_OF_EXPOSED_PSOC_BLOCKS blocks (PSOC TIMESTAMP of DIE0)
 */
#define NUM_OF_USER_ACP_AF_BLOCKS	(NUM_OF_SCHEDULER_ARC)
#define NUM_OF_NIC_DB_FIFO_UMR_BLOCKS	24
#define NUM_OF_NIC_CQ_UMR_BLOCKS	16
#define MAX_NUM_NIC_USER_BLOCKS		(NIC_MAX_NUM_OF_ENGINES * \
					(NUM_OF_NIC_DB_FIFO_UMR_BLOCKS + \
					NUM_OF_NIC_CQ_UMR_BLOCKS))
#define NUM_OF_CBC_USER_BLOCKS		1
#define NUM_OF_EXPOSED_SM_BLOCKS	(((MAX_NUM_OF_DIES * NUM_OF_HDCORES_PER_DIE - 1) * 3) + 1)
#define NUM_OF_EXPOSED_PSOC_BLOCKS	1

#define NUM_USER_MAPPED_BLOCKS		(NUM_ARC_CPUS + \
					NUM_OF_USER_ACP_AF_BLOCKS + \
					MAX_NUM_NIC_USER_BLOCKS + \
					NUM_OF_PDMA_CH + \
					NUMBER_OF_DEC + \
					NUM_OF_CBC_USER_BLOCKS + \
					NUM_OF_EXPOSED_SM_BLOCKS + \
					NUM_OF_EXPOSED_PSOC_BLOCKS)

/*
 * return register offset in terms of LBW area offset
 * (used when configuring register address to engines)
 */
#define REG_OFF_TO_LBW_OFF(reg_off)	lower_32_bits((reg_off) + CFG_BAR_BASE - LBW_BASE)

#define LBW_DUP_PUSH_BLOCK_SIZE		512
#define LBW_DUP_NUMBER_OF_GROUPS	14
#define LBW_DUP_NUMBER_OF_64_BIT_GROUPS 12
#define LBW_DUP_NUMBER_OF_ENGINES	2

#define SM_OBJS_BLOCK_SIZE	\
		(mmSOB_OBJS_SM_SEC_0_0 - mmSOB_OBJS_SOB_OBJ_0_0)

/* Host virtual address space. */

#define VA_PMMU_HUGE_HINT_BIT		0x0080000000000000ull /* Bit 55 */

#define VA_HOST_SPACE_PAGE_START	0xFF70000000000000ull
#define VA_HOST_SPACE_PAGE_END		0xFF70800000000000ull /* 140TB */

#define VA_HOST_SPACE_HPAGE_START	(VA_HOST_SPACE_PAGE_START | VA_PMMU_HUGE_HINT_BIT)
#define VA_HOST_SPACE_HPAGE_END		(VA_HOST_SPACE_PAGE_END | VA_PMMU_HUGE_HINT_BIT)

/*
 * it is a HW limitation that CNTRL_PAGE_SIZE_TYPE1 and CNTRL_PAGE_SIZE_TYPE2
 * are for small pages (64MB and below), hence we can support 2 small pages
 * while CNTRL_PAGE_SIZE_TYPE3 is only for large page (greater than 64MB) hence
 * support at most single large page
 */
#define GAUDI3_HMMU_MAX_SMALL_PAGES_NUM	2U
#define GAUDI3_HMMU_MAX_LARGE_PAGES_NUM	1U

#define GAUDI3_HMMU_VALID_SMALL_PAGES_MASK	(PAGE_SIZE_32MB | PAGE_SIZE_64MB)
#define GAUDI3_HMMU_VALID_LARGE_PAGES_MASK	(PAGE_SIZE_256MB | PAGE_SIZE_512MB | \
							PAGE_SIZE_1GB | PAGE_SIZE_2GB)

/*
 * HBM virtual address space
 * Gaudi3 has 4 HBM devices per die each of size 16GB so at most we'll have 128GB (for 2 dies).
 * No core separation is supported so we can have one chunk of virtual address
 * space just above the physical ones.
 * The virtual address space starts immediately after the end of the physical
 * address space which is determined at run-time.
 */
#define VA_HBM_SPACE_END	0x0202000000000000ull

#define ETR_BUF_PER_DIE	2
#define ETR_BUF_SIZE		0x02000000				/* 32MB */
#define ETR_BUF_ORDER		(ilog2(ETR_BUF_SIZE))
#define ETR_BUF_HALF_BUF	(ETR_BUF_SIZE >> 1)

static_assert(IS_POWER_OF_2(ETR_BUF_SIZE));

#define NIC_DRV_SIZE		0x20000000				/* 512MB */

#define PMMU_PAGE_TABLES_SIZE	0x0C000000				/* 192MB */
#define HMMU_PAGE_TABLES_SIZE	0x500000				/* 5MB */

#define FW_RESERVED_HBM_SIZE	SZ_32M

#define MAX_FAULTY_HBMS		1

extern const char *gaudi3_engine_id_str[];

#define GAUDI3_ENG_ID_TO_STR(initiator) ((initiator) >= GAUDI3_ENGINE_ID_SIZE ? "not found" :	\
						gaudi3_engine_id_str[initiator])

enum gaudi3_reserved_sob_id {
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_0,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_1,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_2,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_3,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_4,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_5,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_6,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_7,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_8,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_9,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_10,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_11,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_12,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_13,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_14,
	GAUDI3_RESERVED_SOB_HMMU_MAINT_JOB_15,
	GAUDI3_RESERVED_SOB_PMMU_CACHE_INV,
	GAUDI3_RESERVED_SOB_PDMA,
	GAUDI3_RESERVED_SOB_CBC_INVALIDATION,
	GAUDI3_RESERVED_SOB_NUMBER,
};

enum gaudi3_reserved_mon_id {
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_0,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_1,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_2,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_3,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_4,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_5,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_6,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_7,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_8,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_9,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_10,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_11,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_12,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_13,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_14,
	GAUDI3_RESERVED_MON_HMMU_MAINT_JOB_15,
	GAUDI3_RESERVED_MON_PMMU_CACHE_INV,
	GAUDI3_RESERVED_MON_PDMA,
	GAUDI3_RESERVED_MON_CBC_INVALIDATION,
	GAUDI3_RESERVED_MON_NUMBER,
};

enum gaudi3_reserved_cq_id {
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_0,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_1,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_2,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_3,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_4,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_5,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_6,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_7,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_8,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_9,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_10,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_11,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_12,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_13,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_14,
	GAUDI3_RESERVED_CQ_HMMU_MAINT_JOB_15,
	GAUDI3_RESERVED_CQ_PMMU_INV_CMPL,
	GAUDI3_RESERVED_CQ_PDMA,
	GAUDI3_RESERVED_CQ_CBC_INVALIDATION,
	GAUDI3_RESERVED_CQ_NUMBER,
};

enum gaudi3_dup_group {
	GAUDI3_DUP_GRP_PMMU_BASE,
	GAUDI3_DUP_GRP_STLB_BASE,
	GAUDI3_DUP_GRP_STLB_INTR_SPI_CAUSE,
	GAUDI3_DUP_GRP_MAX,
};

/*
 * note that the values below match the field MAINT_TRIGGER.INV_OR_PF:
 * 0- invalidate
 * 1- pre-fetch
 */
enum gaudi3_cache_maint_type {
	GAUDI3_CACHE_MAINT_INV,
	GAUDI3_CACHE_MAINT_PF,
};

struct gaudi3_etr_ac_config {
	u64 ac_off;
	u64 etr_off;
};

#define GAUDI3_MIN_ETR_BUFS	16
#define GAUDI3_MAX_ETR_BUFS	32

extern const u32 gaudi3_arc_blocks_bases[NUM_ACTIVE_ARCS];
extern const u32 gaudi3_pdma_grp_blocks_bases[NUM_OF_PDMA_GRP];

/* User interrupt count is aligned with HW CQ count.
 * We have 64 CQ's per hdcore.
 */
#define GAUDI3_NUM_USER_INTERRUPTS 256

/* PLDM interrupts: CPU/PSOC interrupt aggregators and QM SW interrupts */
#define CPU_INTR_AGGR_NUM_OF_HDCORE_AGGR	4
#define CPU_INTR_AGGR_NUM_OF_SHARED_AGGR	1
#define CPU_INTR_AGGR_NUM_OF_EVENTS_GROUPS	4
#define CPU_INTR_AGGR_NUM_OF_MSIX_VECTORS	((CPU_INTR_AGGR_NUM_OF_HDCORE_AGGR + \
							CPU_INTR_AGGR_NUM_OF_SHARED_AGGR) * \
								CPU_INTR_AGGR_NUM_OF_EVENTS_GROUPS)
#define PSOC_INTR_AGGR_NUM_OF_AGGR_BLOCKS	34
#define PSOC_INTR_AGGR_NUM_OF_MSIX_VECTORS	PSOC_INTR_AGGR_NUM_OF_AGGR_BLOCKS
#define INTR_AGGR_NUM_OF_MSIX_VECTORS_PER_DIE	(CPU_INTR_AGGR_NUM_OF_MSIX_VECTORS + \
							PSOC_INTR_AGGR_NUM_OF_MSIX_VECTORS)
#define INTR_AGGR_NUM_OF_MSIX_VECTORS		(MAX_NUM_OF_DIES * \
							INTR_AGGR_NUM_OF_MSIX_VECTORS_PER_DIE)
#define GAUDI3_NUM_OF_SW_QM_INTR	88 /* This should be ARRAY_SIZE(gaudi3_qm_irq_map_table) */
#define GAUDI3_NUM_OF_PLDM_INTR	(INTR_AGGR_NUM_OF_MSIX_VECTORS + GAUDI3_NUM_OF_SW_QM_INTR)

#define GAUDI3_EVENT_QUEUE_MSIX_IDX	0

enum gaudi3_irq_num {
	GAUDI3_IRQ_NUM_EVENT_QUEUE = 0,
	GAUDI3_IRQ_NUM_PAGE_FAULT_0,
	GAUDI3_IRQ_NUM_PAGE_FAULT_1,
	GAUDI3_IRQ_NUM_DEC_NRM_FIRST,
	GAUDI3_IRQ_NUM_DEC_NRM_LAST = GAUDI3_IRQ_NUM_DEC_NRM_FIRST + NUMBER_OF_DEC - 1,
	GAUDI3_IRQ_NUM_NIC_PORT_FIRST,
	GAUDI3_IRQ_NUM_NIC_PORT_LAST = GAUDI3_IRQ_NUM_NIC_PORT_FIRST + NIC_MAX_NUM_OF_PORTS - 1,
	GAUDI3_IRQ_NUM_ETR_FIRST,
	GAUDI3_IRQ_NUM_ETR_LAST = GAUDI3_IRQ_NUM_ETR_FIRST + GAUDI3_NUM_ETR - 1,
	GAUDI3_IRQ_NUM_USER_FIRST,
	GAUDI3_IRQ_NUM_USER_LAST = GAUDI3_IRQ_NUM_USER_FIRST + GAUDI3_NUM_USER_INTERRUPTS - 1,
	GAUDI3_PLDM_AGGR_IRQ_FIRST,
	GAUDI3_PLDM_AGGR_IRQ_LAST = GAUDI3_PLDM_AGGR_IRQ_FIRST + INTR_AGGR_NUM_OF_MSIX_VECTORS - 1,
	GAUDI3_PLDM_QM_SW_IRQ_FIRST,
	GAUDI3_PLDM_QM_SW_IRQ_LAST = GAUDI3_PLDM_QM_SW_IRQ_FIRST + GAUDI3_NUM_OF_SW_QM_INTR - 1,
	GAUDI3_IRQ_NUM_EQ_ERROR,
	GAUDI3_IRQ_NUM_RESERVED_FIRST,
	GAUDI3_IRQ_NUM_RESERVED_LAST = (GAUDI3_MSIX_ENTRIES - 2),
	GAUDI3_IRQ_NUM_UNEXPECTED_ERROR = RESERVED_MSIX_UNEXPECTED_USER_ERROR_INTERRUPT,
	GAUDI3_IRQ_NUM_LAST = (GAUDI3_MSIX_ENTRIES - 1)
};

static_assert(GAUDI3_IRQ_NUM_USER_FIRST > GAUDI3_IRQ_NUM_DEC_NRM_LAST);

/* This value cannot change due to errata H9-5304 */
#define GAUDI3_PAGE_FAULT_QUEUE_SIZE		0x2000	/* 8KB */
#define GAUDI3_PAGE_FAULT_QUEUE_ENTRY_SIZE	0x10	/* 16B */
static_assert(GAUDI3_PAGE_FAULT_QUEUE_SIZE == 0x2000);

/**
 * struct gaudi3_hbm - holds HBM subsystem settings
 * @phy_chiplet: tracking of current HBM PHY chiplet type (CHANNEL/MASTER/INITENG)
 * @phy_offset: Address offset for current HBM PHY chiplet type (CHANNEL/MASTER/INITENG)
 */

struct gaudi3_hbm {
	u8 phy_chiplet;
	u32 phy_offset;
};

enum gaudi3_pgf_cause {
	GAUDI3_PGF_CAUSE_NOT_PRESENT	= 0b00,
	GAUDI3_PGF_CAUSE_PERMISSION	= 0b01,
	GAUDI3_PGF_CAUSE_ODQ_QRY	= 0b10,
	GAUDI3_PGF_CAUSE_RESERVED	= 0b11,
};

/**
 * struct gaudi3_page_fault_queue_entry - single gaudi3 page fault queue entry descriptor
 * @cause: the cause of the page fault
 * @va: virtual address of the failed walk
 * @ptw_id: unique identifier of the walk used by the handler to reply PMMU
 * @hop: the hop page fault occurred on during the walk
 * @asid: ASID the walk occurred on
 * @is_valid: safety bit set by the page fault, indicating the entry is valid
 */
struct gaudi3_page_fault_queue_entry {
	enum	gaudi3_pgf_cause cause;
	u64	va;
	u16	ptw_id;
	u8	hop;
	u8	asid;
	bool	is_valid;
};

#define GAUDI3_NUM_TESTED_QMANS	\
			(1 + GAUDI3_HDCORE6_ENGINE_ID_ROT_1 - GAUDI3_HDCORE1_ENGINE_ID_EDMA_0)

/**
 * struct gaudi3_sob_info - Holds information regarding a sync-object being used
 * @offset: the sob offset in the sync-obj group being used.
 * @addr: The physical address of the sync object.
 * @base: indication for QMAN CP message base for SOB.
 * @val: the value being used.
 */
struct gaudi3_sob_info {
	u32 offset;
	u32 addr;
	u32 base;
	u32 val;
};

/**
 * struct gaudi3_qmans_test_info - Holds the addresses of a the messages used for testing the
 *                                 device qmans.
 * @dma_addr: the address used by the HW for accessing the message.
 * @kern_addr: The address used by the driver for accessing the message.
 * @sob: The SOB to use and its expected value.
 */
struct gaudi3_qmans_test_info {
	dma_addr_t dma_addr;
	void *kern_addr;
	struct gaudi3_sob_info sob;
};

/**
 * struct gaudi3_pldm_msix_info - ASIC specific info passed to the PLDM MSIX interrupt at interrupt
 *                                time.
 * @hdev: the device that owns/issued the interrupt
 * @eq_dyn_entry: a placeholder for the information gathered by the interrupt.
 */
struct gaudi3_pldm_msix_info {
	struct hl_device *hdev;
	struct hl_eq_dynamic_entry eq_dyn_entry;
};

/**
 * struct gaudi3_eq_work - EQ handling work info.
 * @work: work object to be queued on the EQ workqueue.
 * @hdev: pointer to the device structure.
 */
struct gaudi3_eq_work {
	struct work_struct work;
	struct hl_device *hdev;
};

/**
 * struct gaudi3_device - ASIC specific manage structure.
 * @cpucp_info_get: get information on device from CPU-CP
 * @mapped_blocks: Array that holds the base address and size of all blocks
 *                 the user can map.
 * @page_fault_queue: page fault queue, (since PMMU1 is disabled, only one is needed)
 * @pgf_q_entries: buffer that holds current page fault queue entries to be handled
 * @hbm_cfg: HBM subsystem settings
 * @kdma_lock_mutex: Lock protecting the access to the KDMA engine
 * @qmans_test_info: Information used by the driver when testing the QMANs.
 * @pldm_msix_info: information used by the driver MSIX interrupts when running on Palladium.
 * @eq_work: EQ handling work info.
 * @hw_cap_initialized: This field contains a bit per H/W component. When that component is
 *                      initialized, that bit is set by the driver to signal we can use this
 *                      component in later code paths. Each bit is cleared upon reset of its
 *                      corresponding H/W component.
 * @hw_cap_pdma_initialized: This field contains a bit per PDMA channel component.
 *                           Once a PDMA channel is initialized, that bit is set by
 *                           the driver to signal we can use this channel in later
 *                           code paths. Each bit is cleared upon reset of its
 *                           corresponding PDMA channel component.
 * @hw_cap_tpc_initialized: This field contains a bit per TPC H/W engine.
 *                          When that engine is initialized, that bit is set by the driver to signal
 *                          we can use this engine in later code paths. Each bit is cleared upon
 *                          reset of its corresponding H/W engine.
 * @hw_cap_dec_initialized: This field contains a bit per decoder H/W engine.
 *                          When that engine is initialized, that bit is set by the driver to signal
 *                          we can use this engine in later code paths. Each bit is cleared upon
 *                          reset of its corresponding H/W engine.
 * @hw_cap_nic_initialized: This field contains a bit per NIC H/W engine.
 *                          When the QMAN of that engine is initialized, this bit is set by the
 *                          driver to signal we can use this QMAN in later code paths. Each bit is
 *                          cleared upon reset of its corresponding H/W engine.
 * @hbm_region_cur_addr: Current address of HBM PCI bar region.
 * @active_hw_arc: This field contains a bit per ARC of an H/W engine with
 *                 exception of SCHED, TPC and NIC engines. Once an engine arc is
 *                 initialized, its respective bit is set. Driver can uniquely
 *                 identify each initialized ARC and use this information in
 *                 later code paths. Each respective bit is cleared upon reset
 *                 of its corresponding ARC of the H/W engine.
 * @active_sched_arc: This field contains a bit per ARC of the TPC engines.
 *                    Once an engine arc is initialized, its respective bit is
 *                    set. Each respective bit is cleared upon reset of its
 *                    corresponding ARC of the TPC engine.
 * @active_tpc_arc: This field contains a bit per ARC of the TPC engines.
 *                  Once an engine arc is initialized, its respective bit is
 *                  set. Each respective bit is cleared upon reset of its
 *                  corresponding ARC of the TPC engine.
 * @coll_lag_size: This field contains the collective operation's lag size.
 * @iatu_dram_region_id: IATU region ID for DRAM.
 */
struct gaudi3_device {
	int (*cpucp_info_get)(struct hl_device *hdev);
	struct user_mapped_block		mapped_blocks[NUM_USER_MAPPED_BLOCKS];
	struct hl_page_fault_queue		page_fault_queue;
	struct gaudi3_page_fault_queue_entry	pgf_q_entries[GAUDI3_PAGE_FAULT_QUEUE_SIZE];
	struct gaudi3_hbm			hbm_cfg;
	struct mutex				kdma_lock_mutex;
	struct gaudi3_qmans_test_info		qmans_test_info[GAUDI3_NUM_TESTED_QMANS];
	struct gaudi3_pldm_msix_info		pldm_msix_info[GAUDI3_NUM_OF_PLDM_INTR];
	struct gaudi3_eq_work			eq_work;
	u64					hw_cap_initialized;
	u64					hw_cap_pdma_initialized;
	u64					hw_cap_tpc_initialized;
	u64					hw_cap_dec_initialized;
	u64					hw_cap_nic_initialized;
	u64					hbm_region_cur_addr;
	u64					active_hw_arc;
	u64					active_sched_arc;
	u64					active_tpc_arc;
	u32					coll_lag_size;
	u8					iatu_dram_region_id;
};

struct gaudi3_pll_params {
	u32	div_cfg;
	u32	div_fact[4];
	u32	div_sel[4];
};

/*
 * Types of the Gaudi3 IP blocks, used by special blocks iterator.
 * Required for scenarios where only particular block types can be
 * addressed (e.g., special PLDM images).
 */
enum gaudi3_block_types {
	GAUDI3_BLOCK_TYPE_PLL,
	GAUDI3_BLOCK_TYPE_RTR,
	GAUDI3_BLOCK_TYPE_CPU,
	GAUDI3_BLOCK_TYPE_DPHY,
	GAUDI3_BLOCK_TYPE_HIF,
	GAUDI3_BLOCK_TYPE_HBM,
	GAUDI3_BLOCK_TYPE_IDD,
	GAUDI3_BLOCK_TYPE_NIC,
	GAUDI3_BLOCK_TYPE_PCIE,
	GAUDI3_BLOCK_TYPE_PCIE_PMA,
	GAUDI3_BLOCK_TYPE_PDMA,
	GAUDI3_BLOCK_TYPE_EDMA,
	GAUDI3_BLOCK_TYPE_PMMU,
	GAUDI3_BLOCK_TYPE_PSOC,
	GAUDI3_BLOCK_TYPE_ROT,
	GAUDI3_BLOCK_TYPE_ARC_FARM,
	GAUDI3_BLOCK_TYPE_DEC,
	GAUDI3_BLOCK_TYPE_MME,
	GAUDI3_BLOCK_TYPE_EU_BIST,
	GAUDI3_BLOCK_TYPE_MSH,
	GAUDI3_BLOCK_TYPE_SYNC_MNGR,
	GAUDI3_BLOCK_TYPE_STLB,
	GAUDI3_BLOCK_TYPE_TPC,
	GAUDI3_BLOCK_TYPE_MAX
};

#define RR_LBW_LONG_ADDR_BYTE_GRANULARITY	8

#define RR_LBW_SHORT_ADDR_MASK_SHORT		GENMASK_ULL(28, 12)
#define RR_LBW_LONG_ADDR_MASK			GENMASK_ULL(28, 2)
#define RR_HBW_LO_ADDR_MASK_SHORT		GENMASK_ULL(31, 14)

/* enum for RR LBW ranges types */
enum rr_lbw_range_type {
	RR_LBW_RANGE_TYPE_SEC_SHORT,
	RR_LBW_RANGE_TYPE_SEC,
	RR_LBW_RANGE_TYPE_PRIV_SHORT,
	RR_LBW_RANGE_TYPE_PRIV_SHORT_13,
	RR_LBW_RANGE_TYPE_PRIV,
	RR_LBW_RANGE_TYPE_NUMBER,
};

/* enum for RR HBW ranges types */
enum rr_hbw_range_type {
	RR_HBW_RANGE_TYPE_SEC,
	RR_HBW_RANGE_TYPE_PRIV,
	RR_HBW_RANGE_TYPE_PRIV_7,
	RR_HBW_RANGE_TYPE_NUMBER,
};

/**
 * struct rr_range - single RR range configuration
 * @min: min address
 * @max: max address
 * @rd: if true the range config applies to read access
 * @wr: if true the range config applies to write access
 */
struct rr_range {
	u64 min;
	u64 max;
	bool rd;
	bool wr;
};

/**
 * struct rr_lbw_type_prop - properties of RR LBW range type
 * @en_off: offset of first copy of EN reg
 * @min_off: offset of first copy of MIN reg
 * @max_off: offset of first copy of MAX reg
 * @addr_mask: mask for the addresses configured to MIN and MAX
 */
struct rr_lbw_type_prop {
	u32 en_off;
	u32 min_off;
	u32 max_off;
	u32 addr_mask;
};

/**
 * struct rr_hbw_type_prop - properties of RR range type
 * @en_off: offset of first copy of EN reg
 * @min_hi_off: offset of first copy of MIN HI reg
 * @min_lo_off: offset of first copy of MIN LO reg
 * @max_hi_off: offset of first copy of MAX HI reg
 * @max_lo_off: offset of first copy of MAX LO reg
 */
struct rr_hbw_type_prop {
	u32 en_off;
	u32 min_hi_off;
	u32 min_lo_off;
	u32 max_hi_off;
	u32 max_lo_off;
};

/**
 * struct rr_type_config - configuration data for specific RR LBW or HBW type
 * @lbw_prop: properties of LBW range type
 * @hbw_prop: properties of HBW range type
 * @ranges: array of configuration element
 * @num_ranges: number of configuration elements in ranges
 */
struct rr_type_config {
	union {
		struct rr_lbw_type_prop lbw_prop;
		struct rr_hbw_type_prop hbw_prop;
	};
	struct rr_range *ranges;
	u8 num_ranges;
};

/**
 * struct rtr_ctrl_rr_config - complete configuration data for LBW and HBW RR
 * @lbw_config_array: pointer to array of LBW RR configuration data
 * @hbw_config_array: pointer to array of HBW RR configuration data
 */
struct rtr_ctrl_rr_config {
	struct rr_type_config *lbw_config_array;
	struct rr_type_config *hbw_config_array;
};

/* Gaudi3 declarations for simulator */
int gaudi3_cpucp_info_get(struct hl_device *hdev);
int gaudi3_scrub_device_dram(struct hl_device *hdev, u64 val);
int gaudi3_scrub_device_mem(struct hl_device *hdev);
int gaudi3_late_init(struct hl_device *hdev);
void gaudi3_late_fini(struct hl_device *hdev);
void gaudi3_ring_doorbell(struct hl_device *hdev, u32 hw_queue_id, u32 pi);
void gaudi3_pqe_write(struct hl_device *hdev, __le64 *pqe, struct hl_bd *bd);
int gaudi3_test_queues(struct hl_device *hdev);
int gaudi3_alloc_cpu_accessible_dma_mem(struct hl_device *hdev);
void *gaudi3_cpu_accessible_dma_pool_alloc(struct hl_device *hdev,
					size_t size, dma_addr_t *dma_handle);
void gaudi3_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size,
					void *vaddr);
void gaudi3_update_eq_ci(struct hl_device *hdev, u32 val);
int gaudi3_context_switch(struct hl_device *hdev, u32 asid);
void gaudi3_restore_phase_topology(struct hl_device *hdev);
int gaudi3_debugfs_read_dma(struct hl_device *hdev, u64 addr, u32 size,
			void *blob_addr);
void *gaudi3_get_events_stat(struct hl_device *hdev, bool aggregate, u32 *size);
int gaudi3_send_heartbeat(struct hl_device *hdev);
int gaudi3_send_device_activity(struct hl_device *hdev, bool open);
int gaudi3_compute_reset_late_init(struct hl_device *hdev);
void gaudi3_hw_queues_lock(struct hl_device *hdev);
void gaudi3_hw_queues_unlock(struct hl_device *hdev);
int gaudi3_send_cpu_message(struct hl_device *hdev, u32 *msg, u16 len,
					u32 timeout, u64 *result);
int gaudi3_ctx_init(struct hl_ctx *ctx);
void gaudi3_ctx_fini(struct hl_ctx *ctx);
u32 gaudi3_get_queue_id_for_cq(struct hl_device *hdev, u32 cq_idx);
u64 gaudi3_get_device_time(struct hl_device *hdev, u32 die_index);
void gaudi3_ack_protection_bits_errors(struct hl_device *hdev);
int gaudi3_get_hw_block_id(struct hl_device *hdev, u64 block_addr,
				u32 *block_size, u32 *block_id);
int gaudi3_block_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
			u32 block_id, u32 block_size);
void gaudi3_enable_events_from_fw(struct hl_device *hdev);
int gaudi3_ack_mmu_page_fault_or_access_error(struct hl_device *hdev,
						u64 mmu_cap_mask);
int gaudi3_map_pll_idx_to_fw_idx(u32 pll_idx);
int gaudi3_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard,
				u32 flags);
int gaudi3_mmu_invalidate_cache_range(struct hl_device *hdev, bool is_hard,
				u32 flags, u32 asid, u64 va, u64 size);
int gaudi3_mmu_prefetch_cache_range(struct hl_ctx *ctx, u32 flags, u32 asid, u64 va, u64 size);
int gaudi3_set_dram_properties(struct hl_device *hdev);
int gaudi3_set_binning_masks(struct hl_device *hdev);
int gaudi3_set_fixed_properties(struct hl_device *hdev);
bool gaudi3_host_phys_addr_valid(u64 addr);
void gaudi3_init_firmware_preload_params(struct hl_device *hdev);
void gaudi3_init_firmware_loader(struct hl_device *hdev);
void gaudi3_state_dump_init(struct hl_device *hdev);
u32 gaudi3_get_sob_addr(struct hl_device *hdev, u32 sob_id);
void gaudi3_set_pci_memory_regions(struct hl_device *hdev);
u32 *gaudi3_get_stream_master_qid_arr(void);
void gaudi3_init_arcs(struct hl_device *hdev);
void gaudi3_lbw_dup_init(struct hl_device *hdev);
void gaudi3_lbw_dup_group_push(struct hl_device *hdev, enum gaudi3_dup_group dup_group_id,
				u32 offset, u32 data);
int gaudi3_mmu_init(struct hl_device *hdev);
void gaudi3_init_cbc(struct hl_device *hdev);
void gaudi3_init_pdma(struct hl_device *hdev);
void gaudi3_user_mapped_blocks_init(struct hl_device *hdev);
void gaudi3_init_sm(struct hl_device *hdev);
void gaudi3_init_edma(struct hl_device *hdev);
void gaudi3_init_tpc(struct hl_device *hdev);
void gaudi3_init_mme(struct hl_device *hdev);
void gaudi3_init_rotator(struct hl_device *hdev);
void gaudi3_init_decoder(struct hl_device *hdev);
int gaudi3_init_security(struct hl_device *hdev);
void gaudi3_stop_edma_qmans(struct hl_device *hdev);
void gaudi3_stop_tpc_qmans(struct hl_device *hdev);
void gaudi3_stop_mme_qmans(struct hl_device *hdev);
void gaudi3_stop_rotator_qmans(struct hl_device *hdev);
void gaudi3_halt_arcs(struct hl_device *hdev);
void gaudi3_halt_pdma(struct hl_device *hdev);
void gaudi3_halt_dup(struct hl_device *hdev);
void gaudi3_stall_edma(struct hl_device *hdev);
void gaudi3_stall_tpc(struct hl_device *hdev);
void gaudi3_stall_mme(struct hl_device *hdev);
void gaudi3_stall_rotator(struct hl_device *hdev);
void gaudi3_stop_decoder(struct hl_device *hdev);
void gaudi3_disable_edma_qmans(struct hl_device *hdev);
void gaudi3_disable_tpc_qmans(struct hl_device *hdev);
void gaudi3_disable_mme_qmans(struct hl_device *hdev);
void gaudi3_disable_rotator_qmans(struct hl_device *hdev);
void gaudi3_clear_arcs_hw_cap(struct hl_device *hdev);
void gaudi3_reset_arcs(struct hl_device *hdev);
int gaudi3_enable_msix(struct hl_device *hdev);
void gaudi3_disable_msix(struct hl_device *hdev);
void gaudi3_user_interrupt_setup(struct hl_device *hdev);
void gaudi3_sync_irqs(struct hl_device *hdev);
int gaudi3_page_fault_queue_sw_init(struct hl_device *hdev);
void gaudi3_page_fault_queue_sw_fini(struct hl_device *hdev);
int gaudi3_page_fault_queue_hw_init(struct hl_device *hdev);
int gaudi3_etr_buf_store_sw_init(struct hl_device *hdev);
void gaudi3_etr_buf_store_sw_fini(struct hl_device *hdev);
int gaudi3_etr_buf_store_late_init(struct hl_device *hdev);
void gaudi3_etr_buf_store_late_fini(struct hl_device *hdev);
struct hl_etr_buf *gaudi3_etr_buf_store_pop_buf(struct hl_device *hdev, u32 etr_idx);
void gaudi3_etr_buf_store_return_buf_to_pool(struct hl_device *hdev, struct hl_etr_buf *buf);
int gaudi3_etr_fetch_buffer_to_host(struct hl_device *hdev, u32 etr_idx, bool last_buffer);
void gaudi3_init_msix_gw_table(struct hl_device *hdev);
void gaudi3_iterate_edmas(struct hl_device *hdev, struct iterate_module_ctx *ctx);
void gaudi3_iterate_pdma_grps(struct hl_device *hdev, struct iterate_module_ctx *ctx);
void gaudi3_iterate_tpcs(struct hl_device *hdev, struct iterate_module_ctx *ctx);
void gaudi3_iterate_mmes(struct hl_device *hdev, struct iterate_module_ctx *ctx);
void gaudi3_iterate_mmes_v3_mstr_ifs(struct hl_device *hdev, struct iterate_module_ctx *ctx);
void gaudi3_iterate_rotators(struct hl_device *hdev, struct iterate_module_ctx *ctx);
void gaudi3_iterate_decoders(struct hl_device *hdev, struct iterate_module_ctx *ctx);
void gaudi3_iterate_rtr_ctrls(struct hl_device *hdev, struct iterate_module_ctx *ctx);
void gaudi3_iterate_cache_slices(struct hl_device *hdev, struct iterate_module_ctx *ctx);
void gaudi3_iterate_rrtrs(struct hl_device *hdev, struct iterate_module_ctx *ctx);
void gaudi3_axuser_hbw_mmu_bp_set(struct hl_device *hdev, u32 axuser_hbw_reg_base, bool bypass);
void gaudi3_axuser_hbw_mmu_bp_clear(struct hl_device *hdev, u32 axuser_hbw_reg_base);
void gaudi3_axuser_hbw_asid_set(struct hl_device *hdev, u32 axuser_hbw_reg_base, u32 asid);
void gaudi3_axuser_hbw_asid_clear(struct hl_device *hdev, u32 axuser_hbw_reg_base);
bool gaudi3_is_device_idle(struct hl_device *hdev, u64 *mask_arr,
					u8 mask_len, struct engines_data *e);
void gaudi3_fw_security_emulation_init(struct hl_device *hdev);
void gaudi3_fw_security_emulation_fini(struct hl_device *hdev, bool asic_dirty);
void gaudi3_clear_hw_cap(struct hl_device *hdev, bool hard_reset);
int gaudi3_hw_fini(struct hl_device *hdev, bool hard_reset, bool fw_reset);

int gaudi3_scheduler_submit_buf(struct hl_device *hdev, u32 cpu_id, u32 queue_id, void *buf,
					u32 len);
bool gaudi3_is_valid_dram_page_size(u32 page_size);
int gaudi3_mmu_get_real_page_size(struct hl_device *hdev, struct hl_mmu_properties *mmu_prop,
					u32 page_size, u32 *real_page_size, bool is_dram_addr);
int gaudi3_get_monitor_dump(struct hl_device *hdev, void *data);
u32 gaudi3_get_dec_base_addr(struct hl_device *hdev, u32 core_id);
int gaudi3_init_cpu(struct hl_device *hdev);
int gaudi3_init_cpu_queues(struct hl_device *hdev, u32 cpu_timeout);
int gaudi3_test_cpu_queue(struct hl_device *hdev);
void gaudi3_send_hard_reset_cmd(struct hl_device *hdev);
int gaudi3_handle_eqe(struct hl_device *hdev, struct hl_eq_dynamic_entry *eq_dynamic_entry);
int gaudi3_alloc_irq_vectors(struct hl_device *hdev, unsigned int min_vecs,
			unsigned int max_vecs, unsigned int flags);
void gaudi3_free_irq_vectors(struct hl_device *hdev);
int gaudi3_init_pb_security(struct hl_device *hdev);
int gaudi3_special_blocks_iterator_config(struct hl_device *hdev);
void gaudi3_special_blocks_iterator_free(struct hl_device *hdev);
void gaudi3_set_dynamic_dram_properties(struct hl_device *hdev);
int gaudi3_validate_set_tpc_binning(struct hl_device *hdev);
void gaudi3_set_dram_binning_masks(struct hl_device *hdev);
int gaudi3_test_qmans_msgs_alloc(struct hl_device *hdev);
void gaudi3_test_qmans_msgs_free(struct hl_device *hdev);
void gaudi3_eq_handler(struct work_struct *work);

/* Functions exported for bring-up support */
void gaudi3_print_sol_config_version(struct hl_device *hdev);
void gaudi3_iterate_mcs(struct hl_device *hdev, struct iterate_module_ctx *ctx);
void gaudi3_init_regulators(struct hl_device *hdev);
void gaudi3_init_qos(struct hl_device *hdev);
void gaudi3_init_cache(struct hl_device *hdev);
void gaudi3_init_n2r_credits(struct hl_device *hdev);
void gaudi3_init_r2c_credits(struct hl_device *hdev);
void gaudi3_init_mc(struct hl_device *hdev);
void gaudi3_init_axiovrd(struct hl_device *hdev);
void gaudi3_pdma_print_debug_info(struct hl_device *hdev, u32 ch_idx);
int gaudi3_init_security_privileged(struct hl_device *hdev);
void gaudi3_rtr_ctrl_config_rr(struct hl_device *hdev, int block, int inst, u32 offset,
				struct iterate_module_ctx *ctx);
void gaudi3_iterate_dtlbs(struct hl_device *hdev, struct iterate_module_ctx *ctx);
void gaudi3_iterate_nrtr_dtlbs(struct hl_device *hdev, struct iterate_module_ctx *ctx);

/* Functions exported for FPGA support */
int gaudi3_early_fini(struct hl_device *hdev);
int gaudi3_sw_init(struct hl_device *hdev);
int gaudi3_sw_fini(struct hl_device *hdev);
int gaudi3_mmap(struct hl_device *hdev, struct vm_area_struct *vma, void *cpu_addr,
			dma_addr_t dma_addr, size_t size);
void *gaudi3_dma_alloc_coherent(struct hl_device *hdev, size_t size,
			dma_addr_t *dma_handle, gfp_t flags);
void gaudi3_dma_free_coherent(struct hl_device *hdev, size_t size,
			void *cpu_addr, dma_addr_t dma_handle);
void *gaudi3_dma_pool_zalloc(struct hl_device *hdev, size_t size,
			gfp_t mem_flags, dma_addr_t *dma_handle);
void gaudi3_dma_pool_free(struct hl_device *hdev, void *vaddr,
			dma_addr_t dma_addr);
void gaudi3_add_device_attr(struct hl_device *hdev, struct attribute_group *dev_clk_attr_grp,
			struct attribute_group *dev_vrm_attr_grp);
int gaudi3_coresight_init(struct hl_device *hdev);
int gaudi3_debug_coresight(struct hl_device *hdev, struct hl_ctx *ctx, void *data);
void gaudi3_halt_coresight(struct hl_device *hdev, struct hl_ctx *ctx);
int gaudi3_init_security(struct hl_device *hdev);
int gaudi3_set_engine_cores(struct hl_device *hdev, u32 *core_ids,
				u32 num_cores, u32 core_command);
int gaudi3_eq_enable_msix(struct hl_device *hdev);
void gaudi3_eq_disable_msix(struct hl_device *hdev);
int gaudi3_set_engines(struct hl_device *hdev, u32 *engine_ids,
					u32 num_engines, u32 engine_command);
int gaudi3_irq_vector(struct hl_device *hdev, unsigned int nr);
int gaudi3_get_eeprom_data(struct hl_device *hdev, void *data, size_t max_size);
void gaudi3_get_msi_info(__le32 *table);

#endif /* GAUDI3P_H_ */
