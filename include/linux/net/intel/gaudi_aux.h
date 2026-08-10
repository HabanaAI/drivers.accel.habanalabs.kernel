/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2021-2023 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HBL_GAUDI_AUX_H_
#define HBL_GAUDI_AUX_H_

#include <linux/types.h>
#include <linux/net/intel/cn_aux.h>

/**
 * struct gaudi_card_types - ASIC card type.
 * @gaudi_card_type_pci: PCI card.
 * @gaudi_card_type_pmc: PCI Mezzanine Card.
 */
enum gaudi_card_types {
	gaudi_card_type_pci,
	gaudi_card_type_pmc
};

/**
 * struct gaudi_cn_aux_data - Gaudi CN driver data.
 * @pcie_bar: PCIe bar virtual addresses.
 * @hbm_bar_cur_addr: pointer to current HBM bar address.
 * @mmap_type_flag: flag to indicate CN MMAP type.
 * @card_type: PCI/Mezzanine card.
 * @rx_msi_addr: RX MSI address.
 * @vendor_part_id: vendor part ID.
 * @kernel_asid: kernel ASID.
 * @card_location: the OAM number in the HLS (relevant for PMC card type).
 * @minor: minor id of the device.
 * @mac_ch_secured: MAC channels are secured by FW.
 * @stat_xpcs91_secured: STAT and XPCS91 are secured by FW.
 * @stat_ext_secured_read: FW supports extended read of all secured registers in one request.
 * @dev_mgmt_fw: is device management FW enabled.
 */
struct gaudi_cn_aux_data {
	void __iomem *pcie_bar;
	u64 *hbm_bar_cur_addr;
	u64 mmap_type_flag;
	enum gaudi_card_types card_type;
	u32 rx_msi_addr;
	u32 vendor_part_id;
	u32 kernel_asid;
	u32 card_location;
	u16 minor;
	u8 mac_ch_secured;
	u8 stat_xpcs91_secured;
	u8 stat_ext_secured_read;
	u8 dev_mgmt_fw;
};

/**
 * struct gaudi_cn_sim_properties - simulator properties for CN initialization (gaudi)
 * @nic_drv_addr: the base address of the memory in the device
 * @nic_drv_size: the size of the memory in the device
 * @nic_drv_base_addr: the aligned base address of the memory in the device
 * @nic_drv_end_addr: the aligned end address of the memory in the device
 * @sb_base_addr: the base address of a Tx eth pkt cyclic buffer
 * @swq_base_addr: the base address of a Tx workqueue cyclic buffer
 * @txs_base_addr: base address of the ports timer cfg
 * @tmr_base_addr: base address of the macros timer cfg
 * @req_qpc_base_addr: the base address of a requester (sender) QP context buffer
 * @res_qpc_base_addr: the base address of a responder (receiver) QP context buffer
 */
struct gaudi_cn_sim_properties {
	u64 nic_drv_addr;
	u64 nic_drv_size;
	u64 nic_drv_base_addr;
	u64 nic_drv_end_addr;
	u64 sb_base_addr;
	u64 swq_base_addr;
	u64 txs_base_addr;
	u64 tmr_base_addr;
	u64 req_qpc_base_addr;
	u64 res_qpc_base_addr;
};

/**
 * struct gaudi_cn_aux_ops - ASIC specific functions for cn <-> accel drivers communication.
 * @map_device_va: Map device virtual address.
 * @unmap_device_va: Unmap device virtual address.
 * @read_all_mac_cnts: Read all MAC counters from FW.
 * @read_xpcs91_regs: Read XPCS91 registers.
 * @get_fault_counters: Read fault counters.
 * @config_port_mac_ch: Configure port MAC channel.
 * @set_pfc: Set port PFC cfg.
 * @set_lpbk: Set port loopback cfg.
 * @read_mac_cnt: Read MAC counter.
 * @reset_mac_stats: Clear MAC statistics registers.
 * @sim_init_props: Fill CN SIM static props.
 * @dma_alloc_coherent: Allocate coherent DMA memory.
 * @dma_free_coherent: Free coherent DMA memory.
 * @dma_pool_zalloc: Allocate small size DMA memory from the pool.
 * @dma_pool_free: Free small size DMA memory from the pool.
 * @get_hw_block_handle: Map block and return its handle.
 * @dma_mmap: Map DMA memory region.
 * @spmu_get_stats_names: get SPMU statistics names.
 * @spmu_get_stats_event_types: get SPMU statistics event types.
 * @spmu_config: config the SPMU.
 * @spmu_sample: read SPMU counters.
 * @poll_reg: Poll on a register until a given condition is fulfilled or timeout.
 * @send_cpu_message: send message to F/W. If the message is timedout, the driver will eventually
 *                    reset the device. The timeout is passed as an argument. If it is 0 the
 *                    timeout set is the default timeout for the specific ASIC.
 * @post_send_status: handler for post sending status packet to FW.
 * @device_reset: Perform device reset..
 * @rx_irq_handler: Handle Rx interrupt.
 * @cq_irq_handler: Handle CQ interrupt.
 * @handle_qp_err: Handle QP error.
 * @ctx_init: initialize user context.
 * @ctx_fini: de-initialize user context.
 * @ports_stop_prepare: prepare the ports for a stop.
 * @send_port_cpucp_status: Send port status to FW.
 * @dump_port_statistics: dump port statistics.
 * @mmap: Map CN memory.
 * @cmd_control: command control IOCTL.
 */
struct gaudi_cn_aux_ops {
	/* cn2accel */
	int (*map_device_va)(struct hbl_aux_dev *aux_dev, void *args, u64 *va);
	int (*unmap_device_va)(struct hbl_aux_dev *aux_dev, void *args);
	int (*read_all_mac_cnts)(struct hbl_aux_dev *aux_dev, u32 port, u64 *mac_cnts, u32 size);
	int (*read_xpcs91_regs)(struct hbl_aux_dev *aux_dev, u32 port, u64 fw_tuning_mask,
				u32 *regs);
	u32 (*get_fault_counters)(struct hbl_aux_dev *aux_dev, u32 port, u64 fw_tuning_mask);
	int (*config_port_mac_ch)(struct hbl_aux_dev *aux_dev, u32 port, u32 speed);
	int (*set_pfc)(struct hbl_aux_dev *aux_dev, u32 port, u64 fw_tuning_mask, bool enable);
	int (*set_lpbk)(struct hbl_aux_dev *aux_dev, u32 port, u64 fw_tuning_mask, bool enable);
	int (*read_mac_cnt)(struct hbl_aux_dev *aux_dev, u32 port, int offset, bool is_rx);
	void (*reset_mac_stats)(struct hbl_aux_dev *aux_dev, u32 port);
	void (*sim_init_props)(struct gaudi_cn_sim_properties *cn_prop);
	void *(*dma_alloc_coherent)(struct hbl_aux_dev *aux_dev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag);
	void (*dma_free_coherent)(struct hbl_aux_dev *aux_dev, size_t size, void *cpu_addr,
					dma_addr_t dma_handle);
	void *(*dma_pool_zalloc)(struct hbl_aux_dev *aux_dev, size_t size, gfp_t mem_flags,
					dma_addr_t *dma_handle);
	void (*dma_pool_free)(struct hbl_aux_dev *aux_dev, void *vaddr, dma_addr_t dma_addr);
	int (*get_hw_block_handle)(struct hbl_aux_dev *aux_dev, u64 address, u64 *handle);
	int (*dma_mmap)(struct hbl_aux_dev *aux_dev, struct vm_area_struct *vma, void *cpu_addr,
			dma_addr_t dma_addr, size_t size);
	void (*spmu_get_stats_names)(struct hbl_aux_dev *aux_dev, u32 port, char ***names,
					u32 *n_stats);
	void (*spmu_get_stats_event_types)(struct hbl_aux_dev *aux_dev, u32 port, u32 **event_types,
						u32 *n_stats);
	int (*spmu_config)(struct hbl_aux_dev *aux_dev, u32 port, u32 num_event_types,
				u32 event_types[], bool enable);
	int (*spmu_sample)(struct hbl_aux_dev *aux_dev, u32 port, u32 num_out_data, u64 out_data[]);
	int (*poll_reg)(struct hbl_aux_dev *aux_dev, u32 reg, u64 timeout_us,
			hbl_cn_poll_cond_func func, void *arg);
	int (*send_cpu_message)(struct hbl_aux_dev *aux_dev, u32 *msg, u16 len, u32 timeout,
				u64 *result);
	void (*post_send_status)(struct hbl_aux_dev *aux_dev, u32 port);
	void (*device_reset)(struct hbl_aux_dev *aux_dev);
	/* accel2cn */
	irqreturn_t (*rx_irq_handler)(struct hbl_aux_dev *aux_dev, int irq);
	irqreturn_t (*cq_irq_handler)(struct hbl_aux_dev *aux_dev, int irq);
	void (*handle_qp_err)(struct hbl_aux_dev *aux_dev, u32 port);
	int (*ctx_init)(struct hbl_aux_dev *aux_dev, u32 asid);
	void (*ctx_fini)(struct hbl_aux_dev *aux_dev, u32 asid);
	void (*ports_stop_prepare)(struct hbl_aux_dev *aux_dev, bool fw_reset, bool in_teardown);
	int (*send_port_cpucp_status)(struct hbl_aux_dev *aux_dev, u32 port, u8 cmd, u8 period);
	int (*dump_port_statistics)(struct hbl_aux_dev *aux_dev, u32 port, u64 str_buf_ptr,
					u64 val_buf_ptr, u32 *num_of_stat);
	int (*mmap)(struct hbl_aux_dev *aux_dev, u32 asid, struct vm_area_struct *vma);
	int (*cmd_control)(struct hbl_aux_dev *aux_dev, u32 op, void *input, void *output,
				u32 asid);
};

#endif /* HBL_GAUDI_AUX_H_ */
