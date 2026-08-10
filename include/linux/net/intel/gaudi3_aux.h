/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2021-2023 HabanaLabs, Ltd.
 * Copyright (C) 2023-2025, Intel Corporation.
 * All Rights Reserved.
 *
 */

#ifndef HBL_GAUDI3_AUX_H_
#define HBL_GAUDI3_AUX_H_

#include <linux/types.h>
#include <linux/net/intel/cn_aux.h>
#include <linux/habanalabs/cpucp_if.h>

/**
 * enum gaudi3_setup_type - Setup types a gaudi3 device can reside on.
 * GAUDI3_SETUP_TYPE_HLS3: HLS setup.
 * GAUDI3_SETUP_TYPE_HL325_S_EXT_LB: Standalone, one card only.
 * GAUDI3_SETUP_TYPE_HLB325: HLB setup.
 * GAUDI3_SETUP_TYPE_HL338: HL338 setup.
 * GAUDI3_SETUP_TYPE_HL338_S_EXT_LB: HL338 standalone, one card only.
 * GAUDI3_SETUP_TYPE_RACK: Gaudi3 rack.
 * GAUDI3_SETUP_TYPE_RACK_WHITEBOX: Gaudi3 rack whitebox.
 */
enum gaudi3_setup_type {
	GAUDI3_SETUP_TYPE_HLS3,
	GAUDI3_SETUP_TYPE_HL325_S_EXT_LB,
	GAUDI3_SETUP_TYPE_HLB325,
	GAUDI3_SETUP_TYPE_HL338,
	GAUDI3_SETUP_TYPE_HL338_S_EXT_LB,
	GAUDI3_SETUP_TYPE_RACK,
	GAUDI3_SETUP_TYPE_RACK_WHITEBOX,
};

/**
 * struct gaudi3_cn_aux_data - Gaudi3 CN driver data.
 * @setup_type: type of setup connectivity.
 * @coll_lag_size: Collective operation's lag size.
 * @scale_out_coll_lag_size: Scale out collective operation's lag size.
 * @pci_id: vendor part ID.
 * @kernel_asid: kernel ASID.
 * @card_location: the OAM number in the HLS (relevant for PMC card type).
 * @minor: minor id of the device.
 * @num_of_dies: Number of dies in the asic.
 * @num_of_hdcores: Number of hdcores used in the asic.
 * @enable_h9_rx_drop_eco: Enable Rx drop ECO.
 * @dev_mgmt_fw: is device management FW enabled.
 * @cpucp_checkers_shift: CPUCP checkers flags shift.
 */
struct gaudi3_cn_aux_data {
	enum gaudi3_setup_type setup_type;
	u32 coll_lag_size;
	u32 scale_out_coll_lag_size;
	u32 vendor_part_id;
	u32 kernel_asid;
	u32 card_location;
	u16 minor;
	u8 num_of_dies;
	u8 num_of_hdcores;
	u8 enable_h9_rx_drop_eco;
	u8 dev_mgmt_fw;
	u8 cpucp_checkers_shift;
};

/**
 * struct gaudi3_cn_aux_ops - ASIC specific functions for cn <-> compute drivers communication.
 * @irq_vector: Get Linux IRQ number.
 * @get_bfe_status: Get BFE status.
 * @axuser_hbw_mmu_bp_set: Set auxuser MMU BP.
 * @is_preboot_fw_enabled: Check if FW preboot enabled.
 * @is_full_fw_enabled: Check if full FW enabled.
 * @is_fw_security_enabled: Check if FW security enabled.
 * @dma_alloc_coherent: Allocate coherent DMA memory.
 * @dma_free_coherent: Free coherent DMA memory.
 * @dma_pool_zalloc: Allocate small size DMA memory from the pool.
 * @dma_pool_free: Free small size DMA memory from the pool.
 * @spmu_get_stats_names: get SPMU statistics names.
 * @spmu_get_stats_event_types: get SPMU statistics event types.
 * @spmu_config: config the SPMU.
 * @spmu_sample: read SPMU counters.
 * @set_priv_assertions: Enable/disable privilege assertions.
 * @poll_reg: Poll on a register until a given condition is fulfilled or timeout.
 * @send_cpu_message: send message to F/W. If the message is timedout, the driver will eventually
 *                    reset the device. The timeout is passed as an argument. If it is 0 the
 *                    timeout set is the default timeout for the specific ASIC.
 * @post_send_status: handler for post sending status packet to FW.
 * @spi_event_handler: handle SPI event.
 * @sei_err_event_handler: handle SEI error event.
 * @restore_dynamic_cfg_soft_reset_fw: Restore dynamic configuration upon soft reset.
 * @reset_prepare: Prepare to reset.
 * @reset_late_init: Notify that compute device finished reset.
 * @ports_stop_prepare: prepare the ports for a stop.
 * @send_port_cpucp_status: Send port status to FW.
 * @dump_port_statistics: dump port statistics.
 * @device_reset: Perform device reset.
 * @cmd_control: command control IOCTL.
 */
struct gaudi3_cn_aux_ops {
	/* cn2compute */
	int (*irq_vector)(struct hbl_aux_dev *aux_dev, unsigned int nr);
	bool (*get_bfe_status)(struct hbl_aux_dev *aux_dev, u8 bfe);
	void (*axuser_hbw_mmu_bp_set)(struct hbl_aux_dev *aux_dev, u32 axuser_hbw_reg_base,
		bool bypass);
	bool (*is_preboot_fw_enabled)(struct hbl_aux_dev *aux_dev);
	bool (*is_full_fw_enabled)(struct hbl_aux_dev *aux_dev);
	bool (*is_fw_security_enabled)(struct hbl_aux_dev *aux_dev);
	void *(*dma_alloc_coherent)(struct hbl_aux_dev *aux_dev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag);
	void (*dma_free_coherent)(struct hbl_aux_dev *aux_dev, size_t size, void *cpu_addr,
					dma_addr_t dma_handle);
	void *(*dma_pool_zalloc)(struct hbl_aux_dev *aux_dev, size_t size, gfp_t mem_flags,
					dma_addr_t *dma_handle);
	void (*dma_pool_free)(struct hbl_aux_dev *aux_dev, void *vaddr, dma_addr_t dma_addr);
	void (*spmu_get_stats_names)(struct hbl_aux_dev *aux_dev, u32 port, char ***names,
					u32 *n_stats);
	void (*spmu_get_stats_event_types)(struct hbl_aux_dev *aux_dev, u32 port, u32 **event_types,
						u32 *n_stats);
	int (*spmu_config)(struct hbl_aux_dev *aux_dev, u32 port, u32 num_event_types,
				u32 event_types[], bool enable);
	int (*spmu_sample)(struct hbl_aux_dev *aux_dev, u32 port, u32 num_out_data, u64 out_data[]);
	void (*set_priv_assertions)(struct hbl_aux_dev *aux_dev, bool enable);
	int (*poll_reg)(struct hbl_aux_dev *aux_dev, u32 reg, u64 timeout_us,
			hbl_cn_poll_cond_func func, void *arg);
	int (*send_cpu_message)(struct hbl_aux_dev *aux_dev, u32 *msg, u16 len, u32 timeout,
				u64 *result);
	void (*post_send_status)(struct hbl_aux_dev *aux_dev, u32 port);
	/* compute2cn */
	u32 (*spi_event_handler)(struct hbl_aux_dev *aux_dev,
					const struct hl_eq_nic_spi_data *nic_spi_data,
					u32 macro_index);
	u32 (*sei_err_event_handler)(struct hbl_aux_dev *aux_dev,
					 const struct hl_eq_nic_sei_data *nic_sei_data,
					 u32 macro_index);
	void (*restore_dynamic_cfg_soft_reset_fw)(struct hbl_aux_dev *aux_dev);
	void (*reset_prepare)(struct hbl_aux_dev *aux_dev);
	void (*reset_late_init)(struct hbl_aux_dev *aux_dev);
	void (*ports_stop_prepare)(struct hbl_aux_dev *aux_dev, bool fw_reset, bool in_teardown);
	int (*send_port_cpucp_status)(struct hbl_aux_dev *aux_dev, u32 port, u8 cmd, u8 period);
	int (*dump_port_statistics)(struct hbl_aux_dev *aux_dev, u32 port, u64 str_buf_ptr,
					u64 val_buf_ptr, u32 *num_of_stat);
	void (*device_reset)(struct hbl_aux_dev *aux_dev);
	int (*cmd_control)(struct hbl_aux_dev *aux_dev, u32 op, void *input, void *output,
				u32 asid);
};

#endif /* HBL_GAUDI3_AUX_H_ */
