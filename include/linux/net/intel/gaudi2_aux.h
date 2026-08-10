/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2020-2024 HabanaLabs, Ltd.
 * Copyright (C) 2023-2024, Intel Corporation.
 * All Rights Reserved.
 */

#ifndef HBL_GAUDI2_AUX_H_
#define HBL_GAUDI2_AUX_H_

#include <linux/types.h>
#include <linux/net/intel/cn_aux.h>
#include <linux/habanalabs/cpucp_if.h>

enum gaudi2_setup_type {
	GAUDI2_SETUP_TYPE_HLS2,
	GAUDI2_SETUP_TYPE_HL225_S_EXT_LB,
	GAUDI2_SETUP_TYPE_HL325_S_EXT_LB,
	GAUDI2_SETUP_TYPE_HLS3,
	GAUDI2_SETUP_TYPE_HL288,
};

/**
 * struct gaudi2_cn_aux_data - Gaudi2 CN driver data.
 * @setup_type: type of setup connectivity.
 * @sob_id_base: first reserved SOB ID.
 * @sob_inc_cfg_val: configuration value for incrementing SOB by one.
 * @vendor_part_id: vendor part ID.
 * @kernel_asid: kernel ASID.
 * @card_location: the OAM number in the HLS (relevant for PMC card type).
 * @fw_major_version: major version of current loaded preboot.
 * @fw_minor_version: minor version of current loaded preboot.
 * @fw_app_cpu_boot_dev_sts0: bitmap representation of application security
 *                            status reported by FW, bit description can be
 *                            found in CPU_BOOT_DEV_STS0
 * @fw_app_cpu_boot_dev_sts1: bitmap representation of application security
 *                            status reported by FW, bit description can be
 *                            found in CPU_BOOT_DEV_STS1
 * @minor: minor id of the device.
 * @fw_security_enabled: FW security enabled.
 * @dev_mgmt_fw: is device management FW enabled.
 */
struct gaudi2_cn_aux_data {
	enum gaudi2_setup_type setup_type;
	u32 sob_id_base;
	u32 sob_inc_cfg_val;
	u32 vendor_part_id;
	u32 kernel_asid;
	u32 card_location;
	u32 fw_major_version;
	u32 fw_minor_version;
	u32 fw_app_cpu_boot_dev_sts0;
	u32 fw_app_cpu_boot_dev_sts1;
	u16 minor;
	u8 fw_security_enabled;
	u8 dev_mgmt_fw;
};

/**
 * struct gaudi2_cn_aux_ops - ASIC specific functions for cn <-> compute drivers communication.
 * @get_event_name: Translate event type to name.
 * @poll_mem: Poll on a memory address until a given condition is fulfilled or timeout.
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
 * @reset_prepare: Prepare to reset.
 * @reset_late_init: Notify that compute device finished reset.
 * @eq_irq_handler: EQ interrupt handler (used for simulator only).
 * @sw_err_event_handler: Handle SW error event.
 * @axi_error_response_event_handler: Handle AXI error.
 * @ports_stop_prepare: prepare the ports for a stop.
 * @send_port_cpucp_status: Send port status to FW.
 * @dump_port_statistics: dump port statistics.
 * @get_tx_swap_map: Get tx lane swap map.
 * @device_reset: Perform device reset.
 * @cmd_control: command control IOCTL.
 */
struct gaudi2_cn_aux_ops {
	/* cn2compute */
	char *(*get_event_name)(struct hbl_aux_dev *aux_dev, u16 event_type);
	int (*poll_mem)(struct hbl_aux_dev *aux_dev, u32 *addr, u32 *val,
			hbl_cn_poll_cond_func func);
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
	void (*reset_prepare)(struct hbl_aux_dev *aux_dev);
	void (*reset_late_init)(struct hbl_aux_dev *aux_dev);
	irqreturn_t (*eq_irq_handler)(struct hbl_aux_dev *aux_dev, int irq);
	int (*sw_err_event_handler)(struct hbl_aux_dev *aux_dev, u16 event_type, u8 macro_index,
				    struct hl_eq_nic_intr_cause *intr_cause_cpucp);
	int (*axi_error_response_event_handler)(struct hbl_aux_dev *aux_dev, u16 event_type,
						u8 macro_index,
						struct hl_eq_nic_intr_cause *intr_cause_cpucp);
	void (*ports_stop_prepare)(struct hbl_aux_dev *aux_dev, bool fw_reset, bool in_teardown);
	int (*send_port_cpucp_status)(struct hbl_aux_dev *aux_dev, u32 port, u8 cmd, u8 period);
	int (*dump_port_statistics)(struct hbl_aux_dev *aux_dev, u32 port, u64 str_buf_ptr,
					u64 val_buf_ptr, u32 *num_of_stat);
	int (*get_tx_swap_map)(struct hbl_aux_dev *aux_dev, u16 *tx_swap_map, u32 tx_swap_map_size);
	void (*device_reset)(struct hbl_aux_dev *aux_dev);
	int (*cmd_control)(struct hbl_aux_dev *aux_dev, u32 op, void *input, void *output,
				u32 asid);
};

#endif /* HBL_GAUDI2_AUX_H_ */
