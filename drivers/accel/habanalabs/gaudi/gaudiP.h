/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2019-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDIP_H_
#define GAUDIP_H_

#include <linux/net/intel/cn_aux.h>
#include <linux/net/intel/gaudi_aux.h>
#include <uapi/drm/habanalabs_accel.h>
#include "../common/habanalabs.h"
#include <linux/habanalabs/hl_boot_if.h>
#include "../include/gaudi/gaudi_packets.h"
#include "../include/gaudi/gaudi.h"
#include "../include/gaudi/gaudi_async_events.h"
#include "../include/gaudi/gaudi_fw_if.h"

#include <linux/netdevice.h>

#define NUMBER_OF_EXT_HW_QUEUES		8
#define NUMBER_OF_CMPLT_QUEUES		NUMBER_OF_EXT_HW_QUEUES
#define NUMBER_OF_CPU_HW_QUEUES		1
#define NUMBER_OF_INT_HW_QUEUES		100
#define NUMBER_OF_HW_QUEUES		(NUMBER_OF_EXT_HW_QUEUES + \
					NUMBER_OF_CPU_HW_QUEUES + \
					NUMBER_OF_INT_HW_QUEUES)

/* 10 NIC QMANs, DMA5 QMAN, TPC7 QMAN */
#define NUMBER_OF_COLLECTIVE_QUEUES	12
#define NUMBER_OF_SOBS_IN_GRP		11

#define GAUDI_STREAM_MASTER_ARR_SIZE	8

#define CORESIGHT_TIMEOUT_USEC		100000		/* 100 ms */

#define GAUDI_MAX_CLK_FREQ		2200000000ull	/* 2200 MHz */

#define MAX_POWER_DEFAULT_PCI		200000		/* 200W */
#define MAX_POWER_DEFAULT_PMC		350000		/* 350W */

#define DC_POWER_DEFAULT_PCI		60000		/* 60W */
#define DC_POWER_DEFAULT_PMC		60000		/* 60W */

#define DC_POWER_DEFAULT_PMC_SEC	97000		/* 97W */

#define GAUDI_CPU_TIMEOUT_USEC		30000000	/* 30s */

#define GAUDI_NIC_FW_TIMEOUT_USEC	12000000	/* 12s */

#define TPC_ENABLED_MASK		0xFF

#define GAUDI_HBM_SIZE_32GB		0x800000000ull
#define GAUDI_HBM_DEVICES		4
#define GAUDI_HBM_CHANNELS		8
#define GAUDI_HBM_CFG_BASE		(mmHBM0_BASE - CFG_BASE)
#define GAUDI_HBM_CFG_OFFSET		(mmHBM1_BASE - mmHBM0_BASE)

#define DMA_MAX_TRANSFER_SIZE		U32_MAX

#define GAUDI_DEFAULT_CARD_NAME		"HL205"

#define GAUDI_MAX_PENDING_CS		SZ_16K

#if !IS_MAX_PENDING_CS_VALID(GAUDI_MAX_PENDING_CS)
#error "GAUDI_MAX_PENDING_CS must be power of 2 and greater than 1"
#endif

#define PCI_DMA_NUMBER_OF_CHNLS		2
#define HBM_DMA_NUMBER_OF_CHNLS		6
#define DMA_NUMBER_OF_CHNLS		(PCI_DMA_NUMBER_OF_CHNLS + \
						HBM_DMA_NUMBER_OF_CHNLS)

#define MME_NUMBER_OF_SLAVE_ENGINES	2
#define MME_NUMBER_OF_ENGINES		(MME_NUMBER_OF_MASTER_ENGINES + \
					MME_NUMBER_OF_SLAVE_ENGINES)
#define MME_NUMBER_OF_QMANS		(MME_NUMBER_OF_MASTER_ENGINES * \
					QMAN_STREAMS)

#define QMAN_STREAMS		4
#define PQ_FETCHER_CACHE_SIZE	8

#define DMA_QMAN_OFFSET		(mmDMA1_QM_BASE - mmDMA0_QM_BASE)
#define TPC_QMAN_OFFSET		(mmTPC1_QM_BASE - mmTPC0_QM_BASE)
#define MME_QMAN_OFFSET		(mmMME1_QM_BASE - mmMME0_QM_BASE)
#define NIC_MACRO_QMAN_OFFSET	(mmNIC1_QM0_BASE - mmNIC0_QM0_BASE)
#define NIC_ENGINE_QMAN_OFFSET	(mmNIC0_QM1_BASE - mmNIC0_QM0_BASE)

#define TPC_CFG_OFFSET		(mmTPC1_CFG_BASE - mmTPC0_CFG_BASE)

#define DMA_CORE_OFFSET		(mmDMA1_CORE_BASE - mmDMA0_CORE_BASE)

#define QMAN_LDMA_SRC_OFFSET	(mmDMA0_CORE_SRC_BASE_LO - mmDMA0_CORE_CFG_0)
#define QMAN_LDMA_DST_OFFSET	(mmDMA0_CORE_DST_BASE_LO - mmDMA0_CORE_CFG_0)
#define QMAN_LDMA_SIZE_OFFSET	(mmDMA0_CORE_DST_TSIZE_0 - mmDMA0_CORE_CFG_0)

#define QMAN_CPDMA_SRC_OFFSET	(mmDMA0_QM_CQ_PTR_LO_4 - mmDMA0_CORE_CFG_0)
#define QMAN_CPDMA_DST_OFFSET	(mmDMA0_CORE_DST_BASE_LO - mmDMA0_CORE_CFG_0)
#define QMAN_CPDMA_SIZE_OFFSET	(mmDMA0_QM_CQ_TSIZE_4 - mmDMA0_CORE_CFG_0)

#define SIF_RTR_CTRL_OFFSET	(mmSIF_RTR_CTRL_1_BASE - mmSIF_RTR_CTRL_0_BASE)

#define NIF_RTR_CTRL_OFFSET	(mmNIF_RTR_CTRL_1_BASE - mmNIF_RTR_CTRL_0_BASE)

#define MME_ACC_OFFSET		(mmMME1_ACC_BASE - mmMME0_ACC_BASE)
#define SRAM_BANK_OFFSET	(mmSRAM_Y0_X1_RTR_BASE - mmSRAM_Y0_X0_RTR_BASE)

#define NUM_OF_SOB_IN_BLOCK		\
	(((mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_2047 - \
	mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0) + 4) >> 2)

#define NUM_OF_MONITORS_IN_BLOCK	\
	(((mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_STATUS_511 - \
	mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_MON_STATUS_0) + 4) >> 2)

#define MONITOR_MAX_SOBS	8

#define NIC_NUMBER_OF_PORTS	NIC_NUMBER_OF_ENGINES
#define NIC_MAX_NUM_OF_LANES	(NIC_NUMBER_OF_MACROS * NIC_MAC_LANES)

/* DRAM Memory Map */

#define CPU_FW_IMAGE_SIZE	0x10000000	/* 256MB */
#define MMU_PAGE_TABLES_SIZE	0x0BF00000	/* 191MB */
#define MMU_CACHE_MNG_SIZE	0x00100000	/* 1MB */
#define NIC_DRV_SIZE		0x04000000	/* 64MB */

#define CPU_FW_IMAGE_ADDR	DRAM_PHYS_BASE
#define MMU_PAGE_TABLES_ADDR	(CPU_FW_IMAGE_ADDR + CPU_FW_IMAGE_SIZE)
#define MMU_CACHE_MNG_ADDR	(MMU_PAGE_TABLES_ADDR + MMU_PAGE_TABLES_SIZE)
#define NIC_DRV_ADDR		(MMU_CACHE_MNG_ADDR + MMU_CACHE_MNG_SIZE)

#define DRAM_DRV_END_ADDR	(NIC_DRV_ADDR + NIC_DRV_SIZE)

#define DRAM_BASE_ADDR_USER	0x20000000

#if (DRAM_DRV_END_ADDR > DRAM_BASE_ADDR_USER)
#error "Driver must reserve no more than 512MB"
#endif

/* Internal QMANs PQ sizes */

#define MME_QMAN_LENGTH			1024
#define MME_QMAN_SIZE_IN_BYTES		(MME_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE)

#define HBM_DMA_QMAN_LENGTH		4096
#define HBM_DMA_QMAN_SIZE_IN_BYTES	\
				(HBM_DMA_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE)

#define TPC_QMAN_LENGTH			1024
#define TPC_QMAN_SIZE_IN_BYTES		(TPC_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE)

#define NIC_QMAN_LENGTH			4096
#define NIC_QMAN_SIZE_IN_BYTES		(NIC_QMAN_LENGTH * QMAN_PQ_ENTRY_SIZE)


#define SRAM_USER_BASE_OFFSET  GAUDI_DRIVER_SRAM_RESERVED_SIZE_FROM_START

/* Virtual address space */
#define VA_HOST_SPACE_START	0x1000000000000ull	/* 256TB */
#define VA_HOST_SPACE_END	0x3FF8000000000ull	/* 1PB - 512GB */
#define VA_HOST_SPACE_SIZE	(VA_HOST_SPACE_END - \
					VA_HOST_SPACE_START) /* 767TB */
#define HOST_SPACE_INTERNAL_CB_SZ	SZ_2M

#define FUSE_WORDS_PER_BANK		0x40

#define HW_CAP_PLL		BIT(0)
#define HW_CAP_HBM		BIT(1)
#define HW_CAP_MMU		BIT(2)
#define HW_CAP_MME		BIT(3)
#define HW_CAP_CPU		BIT(4)
#define HW_CAP_PCI_DMA		BIT(5)
#define HW_CAP_MSI		BIT(6)
#define HW_CAP_CPU_Q		BIT(7)
#define HW_CAP_HBM_DMA		BIT(8)
#define HW_CAP_SRAM_SCRAMBLER	BIT(10)
#define HW_CAP_HBM_SCRAMBLER	BIT(11)
#define HW_CAP_NIC_DRV		BIT(12)

#define HW_CAP_NIC0		BIT(14)
#define HW_CAP_NIC1		BIT(15)
#define HW_CAP_NIC2		BIT(16)
#define HW_CAP_NIC3		BIT(17)
#define HW_CAP_NIC4		BIT(18)
#define HW_CAP_NIC5		BIT(19)
#define HW_CAP_NIC6		BIT(20)
#define HW_CAP_NIC7		BIT(21)
#define HW_CAP_NIC8		BIT(22)
#define HW_CAP_NIC9		BIT(23)
#define HW_CAP_NIC_MASK		GENMASK(23, 14)
#define HW_CAP_NIC_SHIFT	14

#define HW_CAP_TPC0		BIT(24)
#define HW_CAP_TPC1		BIT(25)
#define HW_CAP_TPC2		BIT(26)
#define HW_CAP_TPC3		BIT(27)
#define HW_CAP_TPC4		BIT(28)
#define HW_CAP_TPC5		BIT(29)
#define HW_CAP_TPC6		BIT(30)
#define HW_CAP_TPC7		BIT(31)
#define HW_CAP_TPC_MASK		GENMASK(31, 24)
#define HW_CAP_TPC_SHIFT	24

#define NEXT_SYNC_OBJ_ADDR_INTERVAL \
	(mmSYNC_MNGR_W_N_SYNC_MNGR_OBJS_SOB_OBJ_0 - \
	 mmSYNC_MNGR_E_N_SYNC_MNGR_OBJS_SOB_OBJ_0)
#define NUM_OF_MME_ENGINES			2
#define NUM_OF_MME_SUB_ENGINES		2
#define NUM_OF_TPC_ENGINES			8
#define NUM_OF_DMA_ENGINES			8
#define NUM_OF_QUEUES				5
#define NUM_OF_STREAMS				4
#define NUM_OF_FENCES				4


#define GAUDI_CPU_PCI_MSB_ADDR(addr)	(((addr) & GENMASK_ULL(49, 39)) >> 39)
#define GAUDI_PCI_TO_CPU_ADDR(addr)			\
	do {						\
		(addr) &= ~GENMASK_ULL(49, 39);		\
		(addr) |= BIT_ULL(39);			\
	} while (0)
#define GAUDI_CPU_TO_PCI_ADDR(addr, extension)		\
	do {						\
		(addr) &= ~GENMASK_ULL(49, 39);		\
		(addr) |= (u64) (extension) << 39;	\
	} while (0)

enum gaudi_dma_channels {
	GAUDI_PCI_DMA_1,
	GAUDI_PCI_DMA_2,
	GAUDI_HBM_DMA_1,
	GAUDI_HBM_DMA_2,
	GAUDI_HBM_DMA_3,
	GAUDI_HBM_DMA_4,
	GAUDI_HBM_DMA_5,
	GAUDI_HBM_DMA_6,
	GAUDI_DMA_MAX
};

enum gaudi_tpc_mask {
	GAUDI_TPC_MASK_TPC0 = 0x01,
	GAUDI_TPC_MASK_TPC1 = 0x02,
	GAUDI_TPC_MASK_TPC2 = 0x04,
	GAUDI_TPC_MASK_TPC3 = 0x08,
	GAUDI_TPC_MASK_TPC4 = 0x10,
	GAUDI_TPC_MASK_TPC5 = 0x20,
	GAUDI_TPC_MASK_TPC6 = 0x40,
	GAUDI_TPC_MASK_TPC7 = 0x80,
	GAUDI_TPC_MASK_ALL = 0xFF
};

enum gaudi_nic_mask {
	GAUDI_NIC_MASK_NIC0 = 0x01,
	GAUDI_NIC_MASK_NIC1 = 0x02,
	GAUDI_NIC_MASK_NIC2 = 0x04,
	GAUDI_NIC_MASK_NIC3 = 0x08,
	GAUDI_NIC_MASK_NIC4 = 0x10,
	GAUDI_NIC_MASK_NIC5 = 0x20,
	GAUDI_NIC_MASK_NIC6 = 0x40,
	GAUDI_NIC_MASK_NIC7 = 0x80,
	GAUDI_NIC_MASK_NIC8 = 0x100,
	GAUDI_NIC_MASK_NIC9 = 0x200,
	GAUDI_NIC_MASK_ALL = 0x3FF
};

enum gaudi_unit_rst {
	GAUDI_UNIT_RST_PSOC,
	GAUDI_UNIT_RST_PCIE,
	GAUDI_UNIT_RST_PCIE_IF,
	GAUDI_UNIT_RST_HBM_S_PLL,
	GAUDI_UNIT_RST_TPC_S_PLL,
	GAUDI_UNIT_RST_MME_S_PLL,
	GAUDI_UNIT_RST_CPU_PLL,
	GAUDI_UNIT_RST_PCIE_PLL,
	GAUDI_UNIT_RST_NIC_S_PLL,
	GAUDI_UNIT_RST_HBM_N_PLL,
	GAUDI_UNIT_RST_TPC_N_PLL,
	GAUDI_UNIT_RST_MME_N_PLL,
	GAUDI_UNIT_RST_NIC_N_PLL,
	GAUDI_UNIT_RST_DMA_W_PLL,
	GAUDI_UNIT_RST_SIF_W_PLL,
	GAUDI_UNIT_RST_MESH_W_PL,
	GAUDI_UNIT_RST_SRAM_W_PL,
	GAUDI_UNIT_RST_DMA_E_PLL,
	GAUDI_UNIT_RST_SIF_E_PLL,
	GAUDI_UNIT_RST_MESH_E_PLL,
	GAUDI_UNIT_RST_SRAM_E_PLL,
	GAUDI_UNIT_RST_TPC_0,
	GAUDI_UNIT_RST_TPC_1,
	GAUDI_UNIT_RST_TPC_2,
	GAUDI_UNIT_RST_TPC_3,
	GAUDI_UNIT_RST_TPC_4,
	GAUDI_UNIT_RST_TPC_5,
	GAUDI_UNIT_RST_TPC_6,
	GAUDI_UNIT_RST_TPC_7,
	GAUDI_UNIT_RST_MME_0,
	GAUDI_UNIT_RST_MME_1,
	GAUDI_UNIT_RST_MME_2,
	GAUDI_UNIT_RST_MME_3,
	GAUDI_UNIT_RST_HBM_0,
	GAUDI_UNIT_RST_HBM_1,
	GAUDI_UNIT_RST_HBM_2,
	GAUDI_UNIT_RST_HBM_3,
	GAUDI_UNIT_RST_NIC_0,
	GAUDI_UNIT_RST_NIC_1,
	GAUDI_UNIT_RST_NIC_2,
	GAUDI_UNIT_RST_NIC_3,
	GAUDI_UNIT_RST_NIC_4,
	GAUDI_UNIT_RST_SM_0,
	GAUDI_UNIT_RST_SM_1,
	GAUDI_UNIT_RST_SM_2,
	GAUDI_UNIT_RST_SM_3,
	GAUDI_UNIT_RST_IF_0,
	GAUDI_UNIT_RST_IF_1,
	GAUDI_UNIT_RST_IF_2,
	GAUDI_UNIT_RST_IF_3,
	GAUDI_UNIT_RST_DMA_0,
	GAUDI_UNIT_RST_DMA_1,
	GAUDI_UNIT_RST_CPU,
	GAUDI_UNIT_RST_MMU
};

/*
 * struct gaudi_hw_sob_group - H/W SOB group info.
 * @hdev: habanalabs device structure.
 * @kref: refcount of this SOB group. group will reset once refcount is zero.
 * @base_sob_id: base sob id of this SOB group.
 * @queue_id: id of the queue that waits on this sob group
 */
struct gaudi_hw_sob_group {
	struct hl_device	*hdev;
	struct kref		kref;
	u32			base_sob_id;
	u32			queue_id;
};

#define NUM_SOB_GROUPS (HL_RSVD_SOBS * QMAN_STREAMS)
/**
 * struct gaudi_collective_properties -
 *     holds all SOB groups and queues info reserved for the collective
 * @hw_sob_group: H/W SOB groups.
 * @next_sob_group_val: the next value to use for the currently used SOB group.
 * @curr_sob_group_idx: the index of the currently used SOB group.
 * @mstr_sob_mask: pre-defined masks for collective master monitors
 */
struct gaudi_collective_properties {
	struct gaudi_hw_sob_group hw_sob_group[NUM_SOB_GROUPS];
	u16			next_sob_group_val[QMAN_STREAMS];
	u8			curr_sob_group_idx[QMAN_STREAMS];
	u8			mstr_sob_mask[HL_COLLECTIVE_RSVD_MSTR_MONS];
};

/**
 * struct gaudi_internal_qman_info - Internal QMAN information.
 * @pq_kernel_addr: Kernel address of the PQ memory area in the host.
 * @pq_dma_addr: DMA address of the PQ memory area in the host.
 * @pq_size: Size of allocated host memory for PQ.
 */
struct gaudi_internal_qman_info {
	void		*pq_kernel_addr;
	dma_addr_t	pq_dma_addr;
	size_t		pq_size;
};

/**
 * struct gaudi_device - ASIC specific manage structure.
 * @cpucp_info_get: get information on device from CPU-CP.
 * @cn_aux_ops: functions for cn <-> accel drivers communication.
 * @cn_aux_data: data to be used by the cn driver.
 * @hw_queues_lock: protects the H/W queues from concurrent access.
 * @hw_queues_lock_mutex: used by simulator instead of hw_queues_lock.
 * @internal_qmans: Internal QMANs information. The array size is larger than
 *                  the actual number of internal queues because they are not in
 *                  consecutive order.
 * @hbm_bar_cur_addr: current address of HBM PCI bar.
 * @events: array that holds all event id's
 * @events_stat: array that holds histogram of all received events.
 * @events_stat_aggregate: same as events_stat but doesn't get cleared on reset
 * @hw_cap_initialized: This field contains a bit per H/W engine. When that
 *                      engine is initialized, that bit is set by the driver to
 *                      signal we can use this engine in later code paths.
 *                      Each bit is cleared upon reset of its corresponding H/W
 *                      engine.
 * @mmu_cache_inv_pi: PI for MMU cache invalidation flow. The H/W expects an
 *                    8-bit value so use u8.
 */
struct gaudi_device {
	int (*cpucp_info_get)(struct hl_device *hdev);
	struct gaudi_cn_aux_ops cn_aux_ops;
	struct gaudi_cn_aux_data cn_aux_data;

	/* TODO: remove hw_queues_lock after moving to scheduler code */
	spinlock_t hw_queues_lock;
	struct mutex hw_queues_lock_mutex;

	struct gaudi_internal_qman_info internal_qmans[GAUDI_QUEUE_ID_SIZE];

	struct gaudi_collective_properties collective_props;

	u64 hbm_bar_cur_addr;

	u32 events[GAUDI_EVENT_SIZE];
	u32 events_stat[GAUDI_EVENT_SIZE];
	u32 events_stat_aggregate[GAUDI_EVENT_SIZE];
	u32 hw_cap_initialized;
	u8 mmu_cache_inv_pi;

	/* parameters for bring-up */
	u32 hbm_sdii_cnt;
	u32 hbm_static_dll_aword;
	u32 hbm_static_dll_dword_wr;
	u32 hbm_static_dll_dword_rd;
	u32 hbm_acc_initial;
	u32 hbm_set_mode_reg_by_mlb_num;
	u32 hbm_set_mode_reg_by_mlb_op;
	u32 hbm_internal_lb_length_usec;
	u32 hbm_freq;
	u8 hbm_device;
	u8 hbm_continuous;
	u8 hbm_ecc_enable;
	u8 hbm_dbi_enable;
	u8 hbm_inc;
	u8 hbm_mode;
	u8 hbm_loop;
	u8 hbm_interleave;
	u8 hbm_training_delay;
	u8 hbm_hardware_training_repeat;
	u8 hbm_debug;
	u8 compat_mode;
};

extern struct hl_cn_funcs gaudi_cn_funcs;

void gaudi_init_security(struct hl_device *hdev);
void gaudi_ack_protection_bits_errors(struct hl_device *hdev);
int gaudi_debug_coresight(struct hl_device *hdev, struct hl_ctx *ctx, void *data);
void gaudi_halt_coresight(struct hl_device *hdev, struct hl_ctx *ctx);
void gaudi_mmu_prepare_reg(struct hl_device *hdev, u64 reg, u32 asid);

/* Functions exported due to simulator */
int gaudi_scrub_device_dram(struct hl_device *hdev, u64 val);
int gaudi_scrub_device_mem(struct hl_device *hdev);
int gaudi_set_fixed_properties(struct hl_device *hdev);
int gaudi_early_fini(struct hl_device *hdev);
int gaudi_late_init(struct hl_device *hdev);
void gaudi_late_fini(struct hl_device *hdev);
int gaudi_sw_fini(struct hl_device *hdev);
int gaudi_init_cpu(struct hl_device *hdev);
int gaudi_init_cpu_queues(struct hl_device *hdev, u32 cpu_timeout);
int gaudi_init_hbm(struct hl_device *hdev);
void gaudi_init_pci_dma_qmans(struct hl_device *hdev);
void gaudi_init_hbm_dma_qmans(struct hl_device *hdev);
void gaudi_init_mme_qmans(struct hl_device *hdev);
void gaudi_init_tpc_qmans(struct hl_device *hdev);
void gaudi_init_nic_qmans(struct hl_device *hdev);
int gaudi_alloc_cpu_accessible_dma_mem(struct hl_device *hdev);
void gaudi_free_internal_qmans_pq_mem(struct hl_device *hdev);
int gaudi_alloc_internal_qmans_pq_mem(struct hl_device *hdev);
void gaudi_set_pci_memory_regions(struct hl_device *hdev);
void gaudi_init_scrambler_sram(struct hl_device *hdev);
int gaudi_load_firmware_to_device(struct hl_device *hdev);
int gaudi_mmu_update_asid_hop0_addr(struct hl_device *hdev, u32 asid, u64 phys_addr);
void gaudi_fw_security_emulation_init(struct hl_device *hdev);
void gaudi_fw_security_emulation_fini(struct hl_device *hdev, bool asic_dirty);

int gaudi_debugfs_i2c_read(struct hl_device *hdev, u8 i2c_bus, u8 i2c_addr,
		u8 i2c_reg, u32 *val);
int gaudi_debugfs_i2c_write(struct hl_device *hdev, u8 i2c_bus, u8 i2c_addr,
		u8 i2c_reg, u32 val);
void gaudi_debugfs_led_set(struct hl_device *hdev, u8 led, u8 state);
int gaudi_debugfs_read32(struct hl_device *hdev, u64 addr, bool user_address,
			u32 *val);
int gaudi_debugfs_write32(struct hl_device *hdev, u64 addr, bool user_address,
			u32 val);

long gaudi_get_temperature(struct hl_device *hdev, int sensor_index, u32 attr);
long gaudi_get_voltage(struct hl_device *hdev, int sensor_index, u32 attr);
long gaudi_get_current(struct hl_device *hdev, int sensor_index, u32 attr);
long gaudi_get_fan_speed(struct hl_device *hdev, int sensor_index, u32 attr);
long gaudi_get_pwm_info(struct hl_device *hdev, int sensor_index, u32 attr);
void gaudi_set_pwm_info(struct hl_device *hdev, int sensor_index, u32 attr,
			long value);
u64 gaudi_get_max_power(struct hl_device *hdev);
void gaudi_set_max_power(struct hl_device *hdev, u64 value);
int gaudi_get_eeprom_data(struct hl_device *hdev, void *data, size_t max_size);
int gaudi_get_monitor_dump(struct hl_device *hdev, void *data);

int gaudi_test_cpu_queue(struct hl_device *hdev);
int gaudi_test_queue(struct hl_device *hdev, u32 hw_queue_id);
int gaudi_test_queues(struct hl_device *hdev);
int gaudi_send_cpu_message(struct hl_device *hdev, u32 *msg,
				u16 len, u32 timeout, u64 *result);
int gaudi_cpucp_info_get(struct hl_device *hdev);
int gaudi_send_heartbeat(struct hl_device *hdev);
int gaudi_compute_reset_late_init(struct hl_device *hdev);

int gaudi_hbm_read_interrupts(struct hl_device *hdev, int device,
		struct hl_eq_hbm_ecc_data *hbm_ecc_data);
void gaudi_handle_eqe(struct hl_device *hdev, struct hl_eq_entry *eq_entry);
void *gaudi_get_events_stat(struct hl_device *hdev, bool aggregate, u32 *size);
void *gaudi_get_int_queue_base(struct hl_device *hdev, u32 queue_id,
				dma_addr_t *dma_handle,	u16 *queue_len);
int gaudi_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard, u32 flags);
int gaudi_mmu_invalidate_cache_range(struct hl_device *hdev, bool is_hard,
					u32 flags, u32 asid, u64 va, u64 size);

int gaudi_cs_parser(struct hl_device *hdev, struct hl_cs_parser *parser);
u32 gaudi_get_dma_desc_list_size(struct hl_device *hdev, struct sg_table *sgt);
void gaudi_add_end_of_cb_packets(struct hl_device *hdev, void *kernel_address,
				u32 len, u32 original_len, u64 cq_addr, u32 cq_val,
				u32 msi_vec, bool eb);
void gaudi_restore_phase_topology(struct hl_device *hdev);
int gaudi_context_switch(struct hl_device *hdev, u32 asid);
void *gaudi_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags);
void gaudi_dma_free_coherent(struct hl_device *hdev, size_t size,
		void *cpu_addr, dma_addr_t dma_handle);
void *gaudi_dma_pool_zalloc(struct hl_device *hdev, size_t size,
		gfp_t mem_flags, dma_addr_t *dma_handle);
void gaudi_dma_pool_free(struct hl_device *hdev, void *vaddr,
			dma_addr_t dma_addr);
void *gaudi_cpu_accessible_dma_pool_alloc(struct hl_device *hdev, size_t size,
			dma_addr_t *dma_handle);
void gaudi_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size,
			void *vaddr);
void gaudi_stop_nic_qmans(struct hl_device *hdev);
void gaudi_stop_mme_qmans(struct hl_device *hdev);
void gaudi_stop_tpc_qmans(struct hl_device *hdev);
void gaudi_stop_hbm_dma_qmans(struct hl_device *hdev);
void gaudi_stop_pci_dma_qmans(struct hl_device *hdev);
void gaudi_pci_dma_stall(struct hl_device *hdev);
void gaudi_hbm_dma_stall(struct hl_device *hdev);
void gaudi_tpc_stall(struct hl_device *hdev);
void gaudi_mme_stall(struct hl_device *hdev);
void gaudi_disable_nic_qmans(struct hl_device *hdev);
void gaudi_disable_mme_qmans(struct hl_device *hdev);
void gaudi_disable_tpc_qmans(struct hl_device *hdev);
void gaudi_disable_hbm_dma_qmans(struct hl_device *hdev);
void gaudi_disable_pci_dma_qmans(struct hl_device *hdev);

int gaudi_suspend(struct hl_device *hdev);
int gaudi_resume(struct hl_device *hdev);
int gaudi_cb_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size);
void gaudi_ring_doorbell(struct hl_device *hdev, u32 hw_queue_id, u32 pi);
int gaudi_pci_bars_map(struct hl_device *hdev);
void gaudi_pqe_write(struct hl_device *hdev, __le64 *pqe, struct hl_bd *bd);
void gaudi_hw_queues_lock(struct hl_device *hdev);
void gaudi_hw_queues_unlock(struct hl_device *hdev);
void gaudi_mmu_prepare(struct hl_device *hdev, u32 asid);
int gaudi_mmu_clear_pgt_range(struct hl_device *hdev);
int gaudi_ctx_init(struct hl_ctx *ctx);
void gaudi_ctx_fini(struct hl_ctx *ctx);
int gaudi_pre_schedule_cs(struct hl_cs *cs);
void gaudi_update_eq_ci(struct hl_device *hdev, u32 val);
int gaudi_run_tpc_kernel(struct hl_device *hdev, u64 tpc_kernel, u32 tpc_id);
u64 gaudi_get_device_time(struct hl_device *hdev, u32 die_index);
int gaudi_collective_wait_init_cs(struct hl_cs *cs);
int gaudi_collective_wait_create_jobs(struct hl_device *hdev,
		struct hl_ctx *ctx, struct hl_cs *cs, u32 wait_queue_id,
		u32 collective_engine_id, u32 encaps_signal_offset);
int gaudi_get_hw_block_id(struct hl_device *hdev, u64 block_addr,
				u32 *block_size, u32 *block_id);
int gaudi_block_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
			u32 block_id, u32 block_size);

irqreturn_t gaudi_irq_handler_single(int irq, void *arg);
u32 gaudi_gen_signal_cb(struct hl_device *hdev, void *data, u16 sob_id,
		u32 size, bool eb);
u32 gaudi_gen_wait_cb(struct hl_device *hdev,
		struct hl_gen_wait_properties *prop);
u32 gaudi_get_sob_addr(struct hl_device *hdev, u32 sob_id);
void gaudi_reset_sob(struct hl_device *hdev, void *data);
void gaudi_reset_sob_group(struct hl_device *hdev, u16 sob_group);
u32 gaudi_get_queue_id_for_cq(struct hl_device *hdev, u32 cq_idx);
void gaudi_enable_events_from_fw(struct hl_device *hdev);
int gaudi_debugfs_read_dma(struct hl_device *hdev, u64 addr, u32 size,
				void *blob_addr);
int gaudi_ack_mmu_page_fault_or_access_error(struct hl_device *hdev,
							u64 mmu_cap_mask);
int gaudi_map_pll_idx_to_fw_idx(u32 pll_idx);
void gaudi_state_dump_init(struct hl_device *hdev);
u32 *gaudi_get_stream_master_qid_arr(void);
int gaudi_set_dram_properties(struct hl_device *hdev);
void gaudi_set_priv_assertions(struct hl_device *hdev, bool enable);
int gaudi_set_binning_masks(struct hl_device *hdev);
void gaudi_sim_cn_early_init_props_ext(struct gaudi_cn_sim_properties *cn_prop);
u8 gaudi_is_irq_enabled(struct hl_device *hdev);

/* CN functions */
void gaudi_cn_handle_qp_err(struct hl_device *hdev, u16 event_type);
void gaudi_cn_spmu_get_stats_names(struct hl_device *hdev, u32 port, char ***names, u32 *n_stats);
void gaudi_cn_spmu_get_stats_event_types(struct hl_device *hdev, u32 port, u32 **event_types,
						u32 *n_stats);
int gaudi_cn_spmu_config(struct hl_device *hdev, u32 port, u32 num_event_types, u32 event_types[],
				bool enable);
int gaudi_cn_spmu_sample(struct hl_device *hdev, u32 port, u32 num_out_data, u64 out_data[]);
int gaudi_cn_ctx_init(struct hl_ctx *ctx);
void gaudi_cn_ctx_fini(struct hl_ctx *ctx);

/* Bringup functions */
void gaudi_init_pll(struct hl_device *hdev);
int gaudi_pldm_init_cpu(struct hl_device *hdev);

#endif /* GAUDIP_H_ */
