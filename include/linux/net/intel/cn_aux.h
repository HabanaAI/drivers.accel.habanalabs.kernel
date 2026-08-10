/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2020-2024 HabanaLabs, Ltd.
 * Copyright (C) 2023-2025, Intel Corporation.
 * All Rights Reserved.
 */

#ifndef HBL_CN_AUX_H_
#define HBL_CN_AUX_H_

#include <linux/irqreturn.h>
#include <linux/auxiliary_bus.h>
#include <linux/if_vlan.h>
#include <uapi/linux/ethtool.h>

#define HBL_EN_MAX_HEADERS_SZ	(ETH_HLEN + 2 * VLAN_HLEN + ETH_FCS_LEN)

#define HBL_CN_AUX_MAX_NICS			24
#define HBL_CN_AUX_MODULE_EEPROM_MAX_LEN	1024

/* Scale up memory semantics */
#define HBL_CN_AUX_NODE_SUP_MEM_SEM_MAX_REM_DEVS	8

/**
 * enum hbl_aux_dev_type - auxiliary device type.
 * HBL_AUX_DEV_CN: Shared Network Interface.
 * HBL_AUX_DEV_ETH: Ethernet.
 * HBL_AUX_DEV_IB: InfiniBand.
 */
enum hbl_aux_dev_type {
	HBL_AUX_DEV_CN,
	HBL_AUX_DEV_ETH,
	HBL_AUX_DEV_IB,
};

/**
 * struct hbl_aux_dev - habanalabs auxiliary device structure.
 * @adev: auxiliary device.
 * @aux_ops: pointer functions for drivers communication.
 * @aux_data: essential data for operating the auxiliary device.
 * @priv: auxiliary device private data.
 * @type: type of the auxiliary device.
 */
struct hbl_aux_dev {
	struct auxiliary_device adev;
	void *aux_ops;
	void *aux_data;
	void *priv;
	enum hbl_aux_dev_type type;
};

/*
 * struct hbl_cn_aux_mac_addr - port MAC address received from FW.
 * @mac_addr: port MAC address.
 */
struct hbl_cn_aux_mac_addr {
	u8 mac_addr[ETH_ALEN];
};

/**
 * struct hbl_cn_aux_data - habanalabs data for the cn driver.
 * @pdev: pointer to PCI device, can be NULL in case of simulator device.
 * @dev: related kernel basic device structure.
 */
struct hbl_cn_aux_data {
	struct pci_dev *pdev;
	struct device *dev;
};

#define HBL_CN_AUX_DRIVER_VER_MAX_LEN	32
#define HBL_CN_AUX_FW_VER_MAX_LEN	64

/**
 * enum hbl_cn_aux_nic_gen - supported ASIC types.
 * @HBL_CN_AUX_NIC_GEN1: NIC first generation.
 * @HBL_CN_AUX_NIC_GEN2: NIC second generation.
 * @HBL_CN_AUX_NIC_GEN3: NIC third generation.
 * @HBL_CN_AUX_NIC_GEN4: NIC fourth generation.
 */
enum hbl_cn_aux_nic_gen {
	HBL_CN_AUX_NIC_GEN1,
	HBL_CN_AUX_NIC_GEN2,
	HBL_CN_AUX_NIC_GEN3,
	HBL_CN_AUX_NIC_GEN4,
};

/**
 * enum hbl_cn_aux_plat_type - type of platform
 * @HBL_CN_AUX_PLAT_TYPE_ASIC: ASIC,
 * @HBL_CN_AUX_PLAT_TYPE_PLDM: palladium emulation,
 * @HBL_CN_AUX_PLAT_TYPE_SIM: simulator,
 */
enum hbl_cn_aux_plat_type {
	HBL_CN_AUX_PLAT_TYPE_ASIC,
	HBL_CN_AUX_PLAT_TYPE_PLDM,
	HBL_CN_AUX_PLAT_TYPE_SIM,
};

/**
 * struct hbl_cn_aux_device_info - device information for the CN driver.
 * @asic_specific: ASIC specific data.
 * @nic_gen: NIC generation.
 * @plat_type: platform type.
 * @cfg_base_address: config space base address.
 * @lbw_base_address: low BW base address.
 * @nic_ports_mask: available NIC ports mask.
 * @nms_mask: available NMSes mask.
 * @pdma_mask: available pDMAs mask.
 * @edma_mask: available eDMAs mask.
 * @vendor_id: PCI vendor ID.
 * @dev_mmu_max_map_size: the device-MMU maximum mappable size.
 * @dev_idx: device index.
 * @dev_cline_size: device cache line size.
 * @clk: clock frequency in MHz.
 * @irq_enabled: IRQs are enabled.
 * @pcie_cfg_bar_id: configuration bar id.
 * @num_phys_nics: total number of available NICs in the HW.
 * @lanes_per_port: number of lanes per port.
 * @driver_ver: kernel driver version.
 * @fw_ver: PSOC FW version.
 */
struct hbl_cn_aux_device_info {
	void *asic_specific;
	enum hbl_cn_aux_nic_gen nic_gen;
	enum hbl_cn_aux_plat_type plat_type;
	u64 cfg_base_address;
	u64 lbw_base_address;
	u64 nic_ports_mask;
	u32 nms_mask;
	u32 pdma_mask;
	u32 edma_mask;
	u32 vendor_id;
	u32 dev_mmu_max_map_size;
	u16 dev_idx;
	u16 dev_cline_size;
	u16 clk;
	u8 irq_enabled;
	u8 pcie_cfg_bar_id;
	u8 num_phys_nics;
	u8 lanes_per_port;
	char driver_ver[HBL_CN_AUX_DRIVER_VER_MAX_LEN];
	char fw_ver[HBL_CN_AUX_FW_VER_MAX_LEN];
};

/**
 * enum hbl_cn_mmu_mode - MMU modes the CN can work with.
 * @HBL_CN_MMU_MODE_EXTERNAL: using external MMU HW IP.
 * @HBL_CN_MMU_MODE_NETWORK_TLB: Using internal network TLB (but external page-table).
 */
enum hbl_cn_mmu_mode {
	HBL_CN_MMU_MODE_EXTERNAL,
	HBL_CN_MMU_MODE_NETWORK_TLB,
};

#define GLOBAL_TABLE_VM_HANDLE	0xcafee

/**
 * struct hbl_cn_vm_info - VM related info for the cn driver.
 * @mmu_mode: the type (or mode) of MMU currently configured.
 * @ext_mmu.work_id: the unique work-ID assigned to this VM when in external MMU mode.
 * @net_tlb.pasid: the PCI process space address ID assigned to the device.
 * @net_tlb.page_tbl_addr: the address of the MMU page table of this VM.
 */
struct hbl_cn_vm_info {
	enum hbl_cn_mmu_mode mmu_mode;
	union {
		struct {
			u32 work_id;
		} ext_mmu;

		struct {
			u32 pasid;
			u64 page_tbl_addr;
		} net_tlb;
	};
};

typedef bool (*hbl_cn_poll_cond_func)(u32 val, void *arg);

enum hbl_cn_mem_type {
	HBL_CN_MEM_TYPE_HOST,
	HBL_CN_MEM_TYPE_DEVICE,
};

#define CPUCP_NIC_DEFAULT_LANES_NUM	48

struct tx_taps {
	u8 pre3[CPUCP_NIC_DEFAULT_LANES_NUM];
	u8 pre2[CPUCP_NIC_DEFAULT_LANES_NUM];
	u8 pre1[CPUCP_NIC_DEFAULT_LANES_NUM];
	u8 c0[CPUCP_NIC_DEFAULT_LANES_NUM];
	u8 post1[CPUCP_NIC_DEFAULT_LANES_NUM];
};

/*
 * struct hbl_cn_aux_ports_info - NIC ports information.
 * @mac_addrs: array of MAC address for all physical ports.
 * @module_eeprom: module EEPROM info.
 * @ports_ext_mask: mask of external ports.
 * @ports_auto_neg_mask: mask of ports which supports Autonegotiation.
 * @lanes_pol_tx_mask_lo: Tx polarity value for lanes 0-63. There are 4 lanes per NIC.
 * @lanes_pol_rx_mask_lo: Rx polarity value for lanes 0-63. There are 4 lanes per NIC.
 * @lanes_pol_tx_mask_hi: Tx polarity value for lanes 64-95. There are 4 lanes per NIC.
 * @lanes_pol_rx_mask_hi: Rx polarity value for lanes 64-95. There are 4 lanes per NIC.
 * @tx_taps: struct of arrays of tx taps values per lane, received from FW.
 * @use_taps_from_fw: if true, use tx_taps received from FW, else use default values.
 */
struct hbl_cn_aux_ports_info {
	struct hbl_cn_aux_mac_addr mac_addrs[HBL_CN_AUX_MAX_NICS];
	u8 module_eeprom[HBL_CN_AUX_MODULE_EEPROM_MAX_LEN];
	u64 ports_ext_mask;
	u64 ports_auto_neg_mask;
	u64 lanes_pol_tx_mask_lo;
	u64 lanes_pol_rx_mask_lo;
	u32 lanes_pol_tx_mask_hi;
	u32 lanes_pol_rx_mask_hi;
	struct tx_taps tx_taps;
	bool use_taps_from_fw;
};

/**
 * enum hbl_cn_aux_link_qual - Quality of a link
 * HBL_CN_AUX_LINK_QUAL_POOR: Poor link quality.
 * HBL_CN_AUX_LINK_QUAL_GOOD: Good link quality.
 * HBL_CN_AUX_LINK_QUAL_EXCELLENT: Excellent link quality.
 */
enum hbl_cn_aux_link_qual {
	HBL_CN_AUX_LINK_QUAL_POOR,
	HBL_CN_AUX_LINK_QUAL_GOOD,
	HBL_CN_AUX_LINK_QUAL_EXCELLENT,
};

/*
 * struct hbl_cn_aux_port_status - Port status indicators.
 * @link_qual: quality of the link. Relevant only when link_up = 1.
 * @port_open: is port open.
 * @link_up: is link up. Relevant only when port_open = 1.
 * @anlt_enabled: is ANLT enabled. Relevant only when link_up = 1.
 */
struct hbl_cn_aux_port_status {
	enum hbl_cn_aux_link_qual link_qual;
	u8 port_open;
	u8 link_up;
	u8 anlt_enabled;
};

/*
 * struct hl_cn_aux_port_statistics - Port statistics.
 * @rx_packets: number of packets received.
 * @tx_packets: number of packets sent.
 * @rx_bytes: total bytes of data received.
 * @tx_bytes: total bytes of data sent.
 * @tx_errors: number of errors in the TX.
 * @rx_dropped: number of packets dropped by the RX.
 * @tx_dropped: number of packets dropped by the TX.
 * @correctable_errors: count the correctable FEC blocks.
 * @uncorrectable_errors: count the uncorrectable FEC blocks.
 * @latency_int: Transmission latency in usec - integer part.
 * @latency_frac: Transmission latency in usec - fraction part.
 * @bandwidth_int: Bandwidth in Gpbs - integer part.
 * @bandwidth_frac: Bandwidth in Gpbs - fraction part.
 * @pre_fec_ser_int: pre FEC SER value - integer part
 * @pre_fec_ser_exp: pre FEC SER value - exp part.
 * @post_fec_ser_int: post FEC SER value - integer part
 * @post_fec_ser_exp: post FEC SER value - exp part.
 * @port_toggle: counts how many times the link toggled since last port PHY init.
 * @high_ber: high ber events.
 * @high_ber_reinit: link reinit due to high BER.
 * @link_reinit: link reinit due issues other than high BER.
 * @rxb_out_of_buffer: RXB out of buffer.
 * @local_fault: number of local faults.
 * @remote_fault: number of remote faults.
 */
struct hl_cn_aux_port_statistics {
	u64 rx_packets;
	u64 tx_packets;
	u64 rx_bytes;
	u64 tx_bytes;
	u64 tx_errors;
	u64 rx_dropped;
	u64 tx_dropped;
	u64 correctable_errors;
	u64 uncorrectable_errors;
	u32 latency_int;
	u32 latency_frac;
	u32 bandwidth_int;
	u32 bandwidth_frac;
	u32 pre_fec_ser_int;
	u32 pre_fec_ser_exp;
	u32 post_fec_set_int;
	u32 post_fec_set_exp;
	u32 port_toggle;
	u32 high_ber;
	u32 high_ber_reinit;
	u32 link_reinit;
	u32 rxb_out_of_buffer;
	u32 local_fault;
	u32 remote_fault;
};

#define HBL_CN_AUX_MAC_NAME_MAX_LEN 64

/**
 * struct hbl_cn_aux_mac_statistics - holds a name-value pair of a MAC statistic.
 * @name: name of the MAC statistic.
 * @value: value of the MAC statistic.
 */
struct hbl_cn_aux_mac_statistics {
	char *name[HBL_CN_AUX_MAC_NAME_MAX_LEN];
	u64 value;
};

/**
 * enum hbl_cn_aux_device_status -Device status
 * HBL_CN_AUX_DEVICE_STATUS_OPERATIONAL: Device is operational.
 * HBL_CN_AUX_DEVICE_STATUS_IN_RESET: Device is in reset.
 * HBL_CN_AUX_DEVICE_STATUS_DISABLED: Device is disabled.
 */
enum hbl_cn_aux_device_status {
	HBL_CN_AUX_DEVICE_STATUS_OPERATIONAL,
	HBL_CN_AUX_DEVICE_STATUS_IN_RESET,
	HBL_CN_AUX_DEVICE_STATUS_DISABLED,
};

/**
 * struct hbl_cn_aux_node_sup_mem_sem_port - scale up memory semantics port info.
 * @rem_mac_addrs: array with the remote device port MAC addresses.
 * @rem_dev_ids: array with the remote device indices.
 * @valid: indicates that this entry contains valid data.
 * @num_rem_devs: number of remote devices that this port is connected with.
 */
struct hbl_cn_aux_node_sup_mem_sem_port {
	struct hbl_cn_aux_mac_addr rem_mac_addrs[HBL_CN_AUX_NODE_SUP_MEM_SEM_MAX_REM_DEVS];
	u8 rem_dev_ids[HBL_CN_AUX_NODE_SUP_MEM_SEM_MAX_REM_DEVS];
	u8 valid;
	u8 num_rem_devs;
};

/**
 * struct hbl_cn_aux_node_sup_mem_sem_in - scale up memory semantics input.
 * @ports: array with the relevant info for each port.
 */
struct hbl_cn_aux_node_sup_mem_sem_in {
	struct hbl_cn_aux_node_sup_mem_sem_port ports[HBL_CN_AUX_MAX_NICS];
};

/**
 * struct hbl_cn_aux_ops - pointer functions for cn <-> compute drivers communication.
 * @get_device_status: get device status.
 * @get_device_info: device information for the cn driver.
 * @device_lock: prevent device access.
 * @device_unlock: allow device access.
 * @vm_dev_mmu_map: map cpu/kernel address or device memory range to device address range in order
 *                  to provide device-memory access.
 * @vm_dev_mmu_unmap: unmap a previously mapped address range.
 * @vm_dev_mem_alloc: allocate device memory
 * @vm_dev_mem_dealloc: deallocate device memory
 * @vm_reserve_dva_block: Reserve a device virtual block of a given size.
 * @vm_unreserve_dva_block: Release a given device virtual block.
 * @read_mem: Read from memory.
 * @write_mem: Write to memory.
 * @rreg: Read register.
 * @wreg: Write register.
 * @get_reg_pcie_addr: Retrieve pci address.
 * @register_cn_user_context: register a user context represented by user provided FD. If the
 *                            returned comp_handle and vm_handle are equal then this context doesn't
 *                            support data transfer.
 * @deregister_cn_user_context: de-register the user context represented by the vm_handle returned
 *                              from calling register_cn_user_context.
 * @vm_create: create a VM in registered context.
 * @vm_destroy: destroy a VM in registered context.
 * @get_vm_info: get information on a VM.
 * @reserve_irqs: get the MSI-X IRQs that are needed for the NSS operation.
 * @get_reserved_stolen_dev_mem: get the device stolen memory that is allocated for the NSS
 *                               operation.
 * @reserve_global_mem: reserve global memory, either on device or host memory, for the network
 *                      subsystem operation.
 * @unreserve_global_mem: release a previously reserved global memory.
 * @ports_reopen: reopen the ports after hard reset.
 * @ports_close: close ports.
 * @spi_event_handler: handle SPI error event.
 * @sei_event_handler: handle SEI error event.
 * @synchronize_irqs: Synchronize IRQs.
 * @ctx_kill: Kill user context.
 * @get_port_status: Get port link status.
 * @vm_ctx_invalidate: vm context invalidate.
 * @get_port_statistics: get port statistics information.
 * @get_mac_statistics: get MAC statistics information.
 * @init_node_sup_mem_sem: init node scale up memory semantics.
 * @asic_ops: pointer for ASIC specific ops struct.
 */
struct hbl_cn_aux_ops {
	/* cn2compute */
	enum hbl_cn_aux_device_status (*get_device_status)(struct hbl_aux_dev *aux_dev);
	int (*get_device_info)(struct hbl_aux_dev *aux_dev,
			       struct hbl_cn_aux_device_info *device_info);
	void (*device_lock)(struct hbl_aux_dev *aux_dev);
	void (*device_unlock)(struct hbl_aux_dev *aux_dev);
	int (*vm_dev_mmu_map)(struct hbl_aux_dev *aux_dev, u64 vm_handle,
			      enum hbl_cn_mem_type mem_type, u64 addr, u64 dva, size_t size);
	void (*vm_dev_mmu_unmap)(struct hbl_aux_dev *aux_dev, u64 vm_handle, u64 dva, size_t size);
	int (*vm_dev_mem_alloc)(struct hbl_aux_dev *aux_dev, u64 vm_handle, u32 flags, size_t size,
				size_t align, u64 *addr);
	void (*vm_dev_mem_dealloc)(struct hbl_aux_dev *aux_dev, u64 vm_handle, u64 addr);
	int (*vm_reserve_dva_block)(struct hbl_aux_dev *aux_dev, u64 vm_handle, u64 size, u64 *dva);
	void (*vm_unreserve_dva_block)(struct hbl_aux_dev *aux_dev, u64 vm_handle, u64 dva,
				       u64 size);
	u32 (*read_mem)(struct hbl_aux_dev *aux_dev, u64 addr);
	void (*write_mem)(struct hbl_aux_dev *aux_dev, u32 val, u64 addr);
	u32 (*rreg)(struct hbl_aux_dev *aux_dev, u32 reg);
	void (*wreg)(struct hbl_aux_dev *aux_dev, u32 reg, u32 val);
	int (*get_reg_pcie_addr)(struct hbl_aux_dev *aux_dev, u32 reg, u64 *pci_addr);
	void (*get_ports_info)(struct hbl_aux_dev *aux_dev,
			       struct hbl_cn_aux_ports_info *hbl_cn_aux_ports_info);
	int (*register_cn_user_context)(struct hbl_aux_dev *aux_dev, int user_fd,
					const void *cn_ctx, u64 *comp_handle, u64 *vm_handle);
	void (*deregister_cn_user_context)(struct hbl_aux_dev *aux_dev, u64 vm_handle);
	int (*vm_create)(struct hbl_aux_dev *aux_dev, u64 comp_handle, u32 flags, u64 *vm_handle);
	void (*vm_destroy)(struct hbl_aux_dev *aux_dev, u64 vm_handle);
	int (*get_vm_info)(struct hbl_aux_dev *aux_dev, u64 vm_handle,
			   struct hbl_cn_vm_info *vm_info);
	int (*reserve_irqs)(struct hbl_aux_dev *aux_dev, u32 num, int *base_irq);
	int (*get_reserved_stolen_dev_mem)(struct hbl_aux_dev *aux_dev, u32 nms_idx, u64 *addr,
					   size_t *size);
	int (*reserve_global_mem)(struct hbl_aux_dev *aux_dev, size_t size,
				  enum hbl_cn_mem_type mem_type, u64 *dva);
	void (*unreserve_global_mem)(struct hbl_aux_dev *aux_dev, u64 dva);

	/* compute2cn */
	int (*ports_reopen)(struct hbl_aux_dev *aux_dev);
	void (*ports_close)(struct hbl_aux_dev *aux_dev);
	u32 (*spi_event_handler)(struct hbl_aux_dev *aux_dev, u32 nic_index);
	u32 (*sei_event_handler)(struct hbl_aux_dev *aux_dev, u32 nic_index);
	void (*synchronize_irqs)(struct hbl_aux_dev *aux_dev);
	void (*ctx_kill)(struct hbl_aux_dev *aux_dev, void *cn_ctx);
	int (*get_port_status)(struct hbl_aux_dev *aux_dev, u32 port,
			       struct hbl_cn_aux_port_status *port_status);
	int (*vm_ctx_invalidate)(struct hbl_aux_dev *aux_dev, const void *cn_ctx, u64 vm_handle,
				 u64 start_addr, size_t size);
	int (*get_port_statistics)(struct hbl_aux_dev *aux_dev, u32 port,
				   struct hl_cn_aux_port_statistics *stats);
	int (*get_mac_statistics)(struct hbl_aux_dev *aux_dev, u32 port,
				  struct hbl_cn_aux_mac_statistics *stats, u32 *num_stats);
	int (*init_node_sup_mem_sem)(struct hbl_aux_dev *aux_dev,
				     struct hbl_cn_aux_node_sup_mem_sem_in *in);
	void *asic_ops;
};

#endif /* HBL_CN_AUX_H_ */
