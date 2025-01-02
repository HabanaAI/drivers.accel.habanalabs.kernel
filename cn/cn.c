// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "cn.h"

#include "../common/habanalabs.h"
#include "../include/common/pci_ids.h"
#include <linux/file.h>

#ifndef _HAS_AUX_BUS_H
#define HL_CN_NAME		"habanalabs_cn"
#define HL_IB_NAME		"habanalabs_ib"
#endif


static_assert(HBL_CN_AUX_MODULE_EEPROM_MAX_LEN == CPUCP_NIC_QSFP_EEPROM_MAX_LEN);

/**
 * enum hbl_cn_status_cmd - status cmd type.
 * @HBL_CN_STATUS_ONE_SHOT: one shot command.
 * @HBL_CN_STATUS_PERIODIC_START: start periodic status update.
 * @HBL_CN_STATUS_PERIODIC_STOP: stop periodic status update.
 */
enum hbl_cn_status_cmd {
	HBL_CN_STATUS_ONE_SHOT,
	HBL_CN_STATUS_PERIODIC_START,
	HBL_CN_STATUS_PERIODIC_STOP,
};

static int hl_cn_get_nic_gen(struct hl_device *hdev, enum hbl_cn_aux_nic_gen *nic_gen);

static int hl_cn_send_empty_status(struct hl_device *hdev, int port)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct cpucp_nic_status status = {};
	struct hl_cn_properties *cn_props;
	struct cpucp_nic_status_packet *pkt;
	size_t total_pkt_size, data_size;
	u64 result;
	int rc;

	cn_props = &hdev->asic_prop.cn_props;
	data_size = cn_props->status_packet_size;

	total_pkt_size = sizeof(struct cpucp_nic_status_packet) + data_size;

	/* data should be aligned to 8 bytes in order to CPU-CP to copy it */
	total_pkt_size = (total_pkt_size + 0x7) & ~0x7;

	/* total_pkt_size is casted to u16 later on */
	if (total_pkt_size > USHRT_MAX) {
		dev_err(hdev->dev, "NIC status data is too big\n");
		rc = -EINVAL;
		goto out;
	}

	pkt = kzalloc(total_pkt_size, GFP_KERNEL);
	if (!pkt) {
		rc = -ENOMEM;
		goto out;
	}

	status.port = cpu_to_le32(port);
	status.up = false;

	pkt->length = cpu_to_le32(data_size / sizeof(u32));
	memcpy(&pkt->data, &status, data_size);

	pkt->cpucp_pkt.ctl = cpu_to_le32(CPUCP_PACKET_NIC_STATUS << CPUCP_PKT_CTL_OPCODE_SHIFT);

	rc = hdev->asic_funcs->send_cpu_message(hdev, (u32 *) pkt, total_pkt_size, 0, &result);

	if (rc)
		dev_err(hdev->dev, "failed to send NIC status, port %d\n", port);

	kfree(pkt);
out:
	cn_funcs->port_funcs->post_send_status(hdev, port);

	return rc;
}

static enum hbl_cn_aux_device_status hl_cn_get_device_status(struct hbl_aux_dev *aux_dev)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	enum hl_device_status current_status;

	current_status = hl_device_status(hdev);

	switch (current_status) {
	case HL_DEVICE_STATUS_MALFUNCTION:
	case HL_DEVICE_STATUS_IN_RESET:
	case HL_DEVICE_STATUS_IN_RESET_AFTER_DEVICE_RELEASE:
	case HL_DEVICE_STATUS_NEEDS_RESET:
		return HBL_CN_AUX_DEVICE_STATUS_DISABLED;
	case HL_DEVICE_STATUS_OPERATIONAL:
	case HL_DEVICE_STATUS_IN_DEVICE_CREATION:
	default:
		return HBL_CN_AUX_DEVICE_STATUS_OPERATIONAL;
	}

	return HBL_CN_AUX_DEVICE_STATUS_DISABLED;
}

static int hl_cn_get_device_info(struct hbl_aux_dev *aux_dev,
				 struct hbl_cn_aux_device_info *device_info)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct asic_fixed_properties *asic_props = &hdev->asic_prop;
	int rc;

	memset(device_info, 0, sizeof(*device_info));

	device_info->asic_specific = hdev->cn.asic_specific_dev_info;
	rc = hl_cn_get_nic_gen(hdev, &device_info->nic_gen);
	if (rc)
		return rc;

	if (hdev->pldm)
		device_info->plat_type = HBL_CN_AUX_PLAT_TYPE_PLDM;
	else if (hdev->pdev)
		device_info->plat_type = HBL_CN_AUX_PLAT_TYPE_ASIC;
	else
		device_info->plat_type = HBL_CN_AUX_PLAT_TYPE_SIM;

	device_info->cfg_base_address = asic_props->cfg_base_address;
	device_info->lbw_base_address = asic_props->lbw_base_address;
	device_info->irq_enabled = hdev->asic_funcs->is_irq_enabled(hdev);
	device_info->nic_ports_mask = hdev->cn.ports_mask;
	device_info->vendor_id = PCI_VENDOR_ID_HABANALABS;
	device_info->dev_idx = hdev->cdev_idx;
	device_info->dev_cline_size = asic_props->cache_line_size;
	device_info->clk = asic_props->clk;
	device_info->pcie_cfg_bar_id = asic_props->pcie_cfg_bar_id;
	device_info->num_phys_nics = asic_props->num_phys_nics;
	device_info->lanes_per_port = hdev->cn.lanes_per_port;
	strscpy(device_info->driver_ver, hdev->driver_ver, sizeof(device_info->driver_ver));
	strscpy(device_info->fw_ver, asic_props->cpucp_info.cpucp_version,
		sizeof(device_info->fw_ver));

	return 0;
}

static void hl_cn_device_lock(struct hbl_aux_dev *aux_dev)
	__acquires(&hdev->cn.device_lock)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	mutex_lock(&hdev->cn.device_lock);
}

static void hl_cn_device_unlock(struct hbl_aux_dev *aux_dev)
	__releases(&hdev->cn.device_lock)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	mutex_unlock(&hdev->cn.device_lock);
}

void *hl_cn_dma_alloc_coherent(struct hbl_aux_dev *aux_dev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_asic_dma_alloc_coherent(hdev, size, dma_handle, flag);
}

void hl_cn_dma_free_coherent(struct hbl_aux_dev *aux_dev, size_t size, void *cpu_addr,
					dma_addr_t dma_handle)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	hl_asic_dma_free_coherent(hdev, size, cpu_addr, dma_handle);
}

void *hl_cn_dma_pool_zalloc(struct hbl_aux_dev *aux_dev, size_t size, gfp_t mem_flags,
					dma_addr_t *dma_handle)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_asic_dma_pool_zalloc(hdev, size, mem_flags, dma_handle);
}

void hl_cn_dma_pool_free(struct hbl_aux_dev *aux_dev, void *vaddr, dma_addr_t dma_addr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	hl_asic_dma_pool_free(hdev, vaddr, dma_addr);
}

static int hl_cn_vm_dev_mmu_map(struct hbl_aux_dev *aux_dev, u64 vm_handle,
				enum hbl_cn_mem_type mem_type, u64 addr, u64 dva, size_t size)

{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);

	return hl_map_vmalloc_range(cn->ctx, addr, dva, size);
}

static void hl_cn_vm_dev_mmu_unmap(struct hbl_aux_dev *aux_dev, u64 vm_handle, u64 dva, size_t size)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	int rc;

	rc = hl_unmap_vmalloc_range(cn->ctx, dva);
	if (rc)
		dev_crit(hdev->dev, "Failed to unmap dva 0x%llx with size 0x%lx, err %d\n", dva,
				size, rc);
}

static int hl_cn_vm_reserve_dva_block(struct hbl_aux_dev *aux_dev, u64 vm_handle, u64 size,
					u64 *dva)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	u64 addr;

	addr = hl_reserve_va_block(hdev, cn->ctx, HL_VA_RANGE_TYPE_HOST, size, PAGE_SIZE);
	if (!addr)
		return -ENOMEM;

	*dva = addr;

	return 0;
}
static void hl_cn_vm_unreserve_dva_block(struct hbl_aux_dev *aux_dev, u64 vm_handle, u64 dva,
						u64 size)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	hl_unreserve_va_block(hdev, cn->ctx, dva, size);
}

int hl_cn_get_hw_block_handle(struct hbl_aux_dev *aux_dev, u64 address, u64 *handle)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_get_hw_block_handle(hdev, address, handle, NULL);
}

void hl_cn_spmu_get_stats_names(struct hbl_aux_dev *aux_dev, u32 port, char ***names,
				u32 *n_stats)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hl_cn_port_funcs *port_funcs = hdev->asic_funcs->cn_funcs->port_funcs;

	port_funcs->spmu_get_stats_names(hdev, port, names, n_stats);
}

void hl_cn_spmu_get_stats_event_types(struct hbl_aux_dev *aux_dev, u32 port, u32 **event_types,
					u32 *n_stats)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hl_cn_port_funcs *port_funcs = hdev->asic_funcs->cn_funcs->port_funcs;

	port_funcs->spmu_get_stats_event_types(hdev, port, event_types, n_stats);
}

int hl_cn_spmu_config(struct hbl_aux_dev *aux_dev, u32 port, u32 num_event_types, u32 event_types[],
			bool enable)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hl_cn_port_funcs *port_funcs = hdev->asic_funcs->cn_funcs->port_funcs;

	return port_funcs->spmu_config(hdev, port, num_event_types, event_types, enable);
}

int hl_cn_spmu_sample(struct hbl_aux_dev *aux_dev, u32 port, u32 num_out_data, u64 out_data[])
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hl_cn_port_funcs *port_funcs = hdev->asic_funcs->cn_funcs->port_funcs;

	return port_funcs->spmu_sample(hdev, port, num_out_data, out_data);
}

int hl_cn_poll_reg(struct hbl_aux_dev *aux_dev, u32 reg, u64 timeout_us, hbl_cn_poll_cond_func func,
			void *arg)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	u32 val;

	return hl_poll_timeout(hdev, reg, val, func(val, arg), 1000, timeout_us);
}

int hl_cn_send_cpu_message(struct hbl_aux_dev *aux_dev, u32 *msg, u16 len, u32 timeout, u64 *result)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hdev->asic_funcs->send_cpu_message(hdev, msg, len, timeout, result);
}

void hl_cn_set_priv_assertions(struct hbl_aux_dev *aux_dev, bool enable)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	hdev->asic_funcs->set_priv_assertions(hdev, enable);
}

void hl_cn_post_send_status(struct hbl_aux_dev *aux_dev, u32 port)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hl_cn_port_funcs *port_funcs = hdev->asic_funcs->cn_funcs->port_funcs;

	port_funcs->post_send_status(hdev, port);
}

static u32 hl_cn_read_mem(struct hbl_aux_dev *aux_dev, u64 addr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	u64 val = 0;
	int rc;

	rc = hdev->asic_funcs->access_dev_mem(hdev, PCI_REGION_DRAM, addr, &val, DEBUGFS_READ32);
	if (rc)
		dev_crit(hdev->dev, "Failed to readl from dev_mem addr 0x%llx\n", addr);

	return val;
}

static void hl_cn_write_mem(struct hbl_aux_dev *aux_dev, u32 val, u64 addr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	u64 data = val;
	int rc;

	rc = hdev->asic_funcs->access_dev_mem(hdev, PCI_REGION_DRAM, addr, &data, DEBUGFS_WRITE32);
	if (rc)
		dev_crit(hdev->dev, "Failed to writel to dev_mem addr 0x%llx\n", addr);
}

static u32 hl_cn_rreg(struct hbl_aux_dev *aux_dev, u32 reg)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hdev->asic_funcs->rreg(hdev, reg);
}

static void hl_cn_wreg(struct hbl_aux_dev *aux_dev, u32 reg, u32 val)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hdev->asic_funcs->wreg(hdev, reg, val);
}

static int hl_cn_get_reg_pcie_addr(struct hbl_aux_dev *aux_dev, u32 reg, u64 *pci_addr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hdev->asic_funcs->get_reg_pcie_addr(hdev, reg, pci_addr);
}

static int hl_cn_register_cn_user_context(struct hbl_aux_dev *aux_dev, int user_fd,
				const void *cn_ctx, u64 *comp_handle, u64 *vm_handle)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct drm_file *file_priv;
	struct hl_fpriv *hpriv;
	struct file *file;
	int rc = 0;

	if (atomic_cmpxchg(&cn->ctx_registered, 0, 1)) {
		dev_dbg(hdev->dev, "user context is already registered\n");
		return -EBUSY;
	}

	/* CN driver can independently manage its resources and context.
	 * However, for HL devices, corresponding HW resources can also be managed by compute side.
	 * To avoid contention (e.g. abrupt application close) between them, enforce orderly FD
	 * closure. This facilitates that CN destroy runs first, followed by compute fini.
	 */
	file = fget(user_fd);
	if (!file || !hl_check_fd(file)) {
		rc = -EBADF;
		goto file_err;
	}

	mutex_lock(&hdev->fpriv_list_lock);

	if (list_empty(&hdev->fpriv_list)) {
		dev_dbg(hdev->dev, "no open user context\n");
		rc = -ESRCH;
		goto open_ctx_err;
	}

	/* The list should contain a single element as currently only a single user context is
	 * allowed. Therefore get the first entry.
	 */
	hpriv = list_first_entry(&hdev->fpriv_list, struct hl_fpriv, dev_node);

	file_priv = file->private_data;
	if (hpriv != file_priv->driver_priv) {
		dev_dbg(hdev->dev, "user FD mismatch\n");
		rc = -EINVAL;
		goto fd_mismatch_err;
	}

	mutex_unlock(&hdev->fpriv_list_lock);

	/* these must have different values to allow data transfer */
	*comp_handle = 0;
	*vm_handle = 1;

	return 0;

fd_mismatch_err:
open_ctx_err:
	mutex_unlock(&hdev->fpriv_list_lock);
	fput(file);
file_err:
	atomic_set(&cn->ctx_registered, 0);

	return rc;
}

static void hl_cn_deregister_cn_user_context(struct hbl_aux_dev *aux_dev, u64 vm_handle)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hl_fpriv *hpriv;
	struct file *file;

	mutex_lock(&hdev->fpriv_list_lock);
	hpriv = list_first_entry(&hdev->fpriv_list, struct hl_fpriv, dev_node);
	mutex_unlock(&hdev->fpriv_list_lock);

	file = hpriv->file_priv->filp;

	/* We can assert here that all CN resources which might have dependency on compute side are
	 * already released. Hence, release reference to compute file.
	 */
	fput(file);

	atomic_set(&cn->ctx_registered, 0);
}

static int hl_cn_vm_create(struct hbl_aux_dev *aux_dev, u64 comp_handle, u32 flags, u64 *vm_handle)
{
	*vm_handle = 0;

	return 0;
}

static void hl_cn_vm_destroy(struct hbl_aux_dev *aux_dev, u64 vm_handle)
{

}

static int hl_cn_get_vm_info(struct hbl_aux_dev *aux_dev, u64 vm_handle,
				struct hbl_cn_vm_info *vm_info)
{
	vm_info->mmu_mode = HBL_CN_MMU_MODE_EXTERNAL;
	vm_info->ext_mmu.work_id = 1;

	return 0;
}

static int hl_cn_get_reserved_stolen_dev_mem(struct hbl_aux_dev *aux_dev, u32 nms_idx, u64 *addr,
					     size_t *size)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct asic_fixed_properties *asic_props = &hdev->asic_prop;

	/* ignore nms_idx as it is used for future generations of CN */

	*addr = asic_props->nic_drv_addr;
	*size = asic_props->nic_drv_size;

	return 0;
}

static void hl_cn_get_ports_info(struct hbl_aux_dev *aux_dev,
					struct hbl_cn_aux_ports_info *hbl_cn_aux_ports_info)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hbl_cn_cpucp_info *cn_cpucp_info;

	cn_cpucp_info = &hdev->asic_prop.cn_props.cpucp_info;

	memcpy(hbl_cn_aux_ports_info->mac_addrs, cn_cpucp_info->mac_addrs,
	       sizeof(hbl_cn_aux_ports_info->mac_addrs));
	memcpy(hbl_cn_aux_ports_info->module_eeprom, cn_cpucp_info->qsfp_eeprom,
	       sizeof(hbl_cn_aux_ports_info->module_eeprom));

	hbl_cn_aux_ports_info->lanes_pol_tx_mask_lo = cn_cpucp_info->pol_tx_mask[0];
	hbl_cn_aux_ports_info->lanes_pol_rx_mask_lo = cn_cpucp_info->pol_rx_mask[0];
	hbl_cn_aux_ports_info->ports_ext_mask = hdev->cn.ports_ext_mask;
	hbl_cn_aux_ports_info->ports_auto_neg_mask = hdev->cn.auto_neg_mask;
}

static void hl_cn_cpucp_info_le_to_cpu(struct cpucp_nic_info *cpucp_nic_info,
					struct hbl_cn_cpucp_info *hbl_cn_cpucp_info)
{
	int i;

	for (i = 0 ; i < CPUCP_MAX_NICS ; i++) {
		memcpy(&hbl_cn_cpucp_info->mac_addrs[i], &cpucp_nic_info->mac_addrs[i],
			sizeof(cpucp_nic_info->mac_addrs[i]));
		hbl_cn_cpucp_info->tx_swap_map[i] = le16_to_cpu(cpucp_nic_info->tx_swap_map[i]);
	}

	for (i = 0 ; i < CPUCP_NIC_MASK_ARR_LEN ; i++) {
		hbl_cn_cpucp_info->link_mask[i] = le64_to_cpu(cpucp_nic_info->link_mask[i]);
		hbl_cn_cpucp_info->link_ext_mask[i] = le64_to_cpu(cpucp_nic_info->link_ext_mask[i]);
		hbl_cn_cpucp_info->auto_neg_mask[i] = le64_to_cpu(cpucp_nic_info->auto_neg_mask[i]);
	}

	for (i = 0 ; i < CPUCP_NIC_POLARITY_ARR_LEN ; i++) {
		hbl_cn_cpucp_info->pol_tx_mask[i] = le64_to_cpu(cpucp_nic_info->pol_tx_mask[i]);
		hbl_cn_cpucp_info->pol_rx_mask[i] = le64_to_cpu(cpucp_nic_info->pol_rx_mask[i]);
	}

	hbl_cn_cpucp_info->serdes_type = (enum cpucp_serdes_type)
					le16_to_cpu(cpucp_nic_info->serdes_type);

	memcpy(hbl_cn_cpucp_info->qsfp_eeprom, cpucp_nic_info->qsfp_eeprom,
		sizeof(cpucp_nic_info->qsfp_eeprom));
}

static int hl_cn_get_nic_gen(struct hl_device *hdev, enum hbl_cn_aux_nic_gen *nic_gen)

{
	switch (hdev->asic_type) {
	case ASIC_GAUDI_SIM:
	case ASIC_GAUDI_HL2000M_SIM:
	case ASIC_GAUDI:
	case ASIC_GAUDI_SEC:
	case ASIC_GAUDI_HL2000M:
	case ASIC_GAUDI_HL2000M_SEC:
		*nic_gen = HBL_CN_AUX_NIC_GEN1;
		break;
	case ASIC_GAUDI2_SIM:
	case ASIC_GAUDI2B_SIM:
	case ASIC_GAUDI2C_SIM:
	case ASIC_GAUDI2D_SIM:
	case ASIC_GAUDI2_HL_288_SIM:
	case ASIC_GAUDI2D_HL_288_SIM:
	case ASIC_GAUDI2_SIM_ARC:
	case ASIC_GAUDI2B_SIM_ARC:
	case ASIC_GAUDI2C_SIM_ARC:
	case ASIC_GAUDI2D_SIM_ARC:
	case ASIC_GAUDI2_HL_288_SIM_ARC:
	case ASIC_GAUDI2D_HL_288_SIM_ARC:
	case ASIC_GAUDI2:
	case ASIC_GAUDI2B:
	case ASIC_GAUDI2C:
	case ASIC_GAUDI2D:
	case ASIC_GAUDI2_HL_288:
	case ASIC_GAUDI2D_HL_288:
		*nic_gen = HBL_CN_AUX_NIC_GEN2;
		break;
	case ASIC_GAUDI3:
	case ASIC_GAUDI3D:
	case ASIC_GAUDI3D_SIM:
	case ASIC_GAUDI3D_SIM_ARC:
	case ASIC_GAUDI3D_HL_338:
	case ASIC_GAUDI3_HL_338:
	case ASIC_GAUDI3_SIM:
	case ASIC_GAUDI3_SIM_ARC:
	case ASIC_GAUDI3_HL_338_SIM:
	case ASIC_GAUDI3D_HL_338_SIM:
	case ASIC_GAUDI3_HL_338_SIM_ARC:
	case ASIC_GAUDI3D_HL_338_SIM_ARC:
	case ASIC_GAUDI3_FPGA:
		*nic_gen = HBL_CN_AUX_NIC_GEN3;
		break;
	default:
		dev_err(hdev->dev, "Unrecognized ASIC type %d\n", hdev->asic_type);
		return -EINVAL;
	}

	return 0;
}

static int hl_cn_aux_data_init(struct hl_device *hdev)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hbl_cn_aux_data *aux_data;
	struct hbl_cn_aux_ops *aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev;
	int rc;

	aux_data = kzalloc(sizeof(*aux_data), GFP_KERNEL);
	if (!aux_data)
		return -ENOMEM;

	aux_ops = kzalloc(sizeof(*aux_ops), GFP_KERNEL);
	if (!aux_ops) {
		rc = -ENOMEM;
		goto free_aux_data;
	}

	aux_dev = &cn->cn_aux_dev;
	aux_dev->aux_data = aux_data;
	aux_dev->aux_ops = aux_ops;
	aux_dev->type = HBL_AUX_DEV_CN;

	aux_data->pdev = hdev->pdev;
	aux_data->dev = hdev->dev;

	/* set cn -> accel ops */
	aux_ops->get_device_status = hl_cn_get_device_status;
	aux_ops->get_device_info = hl_cn_get_device_info;
	aux_ops->device_lock = hl_cn_device_lock;
	aux_ops->device_unlock = hl_cn_device_unlock;
	aux_ops->vm_dev_mmu_map = hl_cn_vm_dev_mmu_map;
	aux_ops->vm_dev_mmu_unmap = hl_cn_vm_dev_mmu_unmap;
	aux_ops->vm_reserve_dva_block = hl_cn_vm_reserve_dva_block;
	aux_ops->vm_unreserve_dva_block = hl_cn_vm_unreserve_dva_block;
	aux_ops->read_mem = hl_cn_read_mem;
	aux_ops->write_mem = hl_cn_write_mem;
	aux_ops->rreg = hl_cn_rreg;
	aux_ops->wreg = hl_cn_wreg;
	aux_ops->get_reg_pcie_addr = hl_cn_get_reg_pcie_addr;
	aux_ops->register_cn_user_context = hl_cn_register_cn_user_context;
	aux_ops->deregister_cn_user_context = hl_cn_deregister_cn_user_context;
	aux_ops->vm_create = hl_cn_vm_create;
	aux_ops->vm_destroy = hl_cn_vm_destroy;
	aux_ops->get_vm_info = hl_cn_get_vm_info;
	aux_ops->get_ports_info = hl_cn_get_ports_info;
	aux_ops->get_reserved_stolen_dev_mem = hl_cn_get_reserved_stolen_dev_mem;

	cn_funcs->set_cn_data(hdev);

	return 0;

free_aux_data:
	kfree(aux_data);

	return rc;
}

static void hl_cn_aux_data_fini(struct hl_device *hdev)
{
	struct hbl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;

	kfree(aux_dev->aux_ops);
	kfree(aux_dev->aux_data);
}

#ifdef _HAS_AUX_BUS_H
static void cn_adev_release(struct device *dev)
{
	struct hbl_aux_dev *aux_dev = container_of(dev, struct hbl_aux_dev, adev.dev);
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);

	cn->is_cn_aux_dev_initialized = false;
}

static int hl_cn_aux_drv_init(struct hl_device *hdev)
{
	struct hl_cn *cn = &hdev->cn;
	struct hbl_aux_dev *aux_dev = &cn->cn_aux_dev;
	struct auxiliary_device *adev;
	int rc;

	rc = hl_cn_aux_data_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "CN aux data init failed\n");
		return rc;
	}

	adev = &aux_dev->adev;
	adev->id = hdev->id;
	adev->name = "cn";
	adev->dev.parent = hdev->dev;
	adev->dev.release = cn_adev_release;

	rc = auxiliary_device_init(adev);
	if (rc) {
		dev_err(hdev->dev, "CN auxiliary_device_init failed\n");
		goto aux_data_free;
	}

	rc = auxiliary_device_add(adev);
	if (rc) {
		dev_err(hdev->dev, "CN auxiliary_device_add failed\n");
		goto uninit_adev;
	}

	cn->is_cn_aux_dev_initialized = true;

	return 0;

uninit_adev:
	auxiliary_device_uninit(adev);
aux_data_free:
	hl_cn_aux_data_fini(hdev);

	return rc;
}

static void hl_cn_aux_drv_fini(struct hl_device *hdev)
{
	struct hl_cn *cn = &hdev->cn;
	struct auxiliary_device *adev;

	if (!cn->is_cn_aux_dev_initialized)
		return;

	adev = &cn->cn_aux_dev.adev;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);

	hl_cn_aux_data_fini(hdev);
}
#else
static int hl_cn_aux_drv_init(struct hl_device *hdev)
{
	int (*probe)(struct hbl_aux_dev *aux_dev);
	struct hl_cn *cn = &hdev->cn;
	struct module *module;
	int rc;

	rc = hl_cn_aux_data_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "CN aux data init failed\n");
		return rc;
	}

	module = find_module(HL_CN_NAME);
	if (!module) {
		dev_err(hdev->dev, "Module %s was not found\n", HL_CN_NAME);
		rc = -EIO;
		goto module_fail;
	}

	/* don't allow module unloading */
	if (!try_module_get(module)) {
		dev_err(hdev->dev, "Failed to increment %s module refcount\n", HL_CN_NAME);
		rc = -EIO;
		goto module_fail;
	}

	probe = symbol_get(hbl_cn_probe);
	if (!probe) {
		dev_err(hdev->dev, "hbl_cn_probe symbol was not found\n");
		rc = -ENODEV;
		goto probe_fail;
	}

	rc = probe(&hdev->cn.cn_aux_dev);
	symbol_put(hbl_cn_probe);

	cn->is_cn_aux_dev_initialized = true;

	return 0;

probe_fail:
	module_put(module);
module_fail:
	hl_cn_aux_data_fini(hdev);

	return rc;
}

static void hl_cn_aux_drv_fini(struct hl_device *hdev)
{
	void (*remove)(struct hbl_aux_dev *aux_dev);
	struct hl_cn *cn = &hdev->cn;
	struct module *module;

	if (!cn->is_cn_aux_dev_initialized)
		return;

	module = find_module(HL_CN_NAME);
	if (!module) {
		dev_err(hdev->dev, "Module %s was not found\n", HL_CN_NAME);
		return;
	}

	remove = symbol_get(hbl_cn_remove);
	if (!remove) {
		dev_err(hdev->dev, "hbl_cn_remove symbol was not found\n");
		return;
	}

	remove(&hdev->cn.cn_aux_dev);
	symbol_put(hbl_cn_remove);
	module_put(module);

	hl_cn_aux_data_fini(hdev);
}
#endif

int hl_cn_reopen(struct hl_device *hdev)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hbl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct hbl_cn_aux_ops *aux_ops = aux_dev->aux_ops;
	int rc;

	/* check if the NIC is enabled */
	if (!hdev->cn.ports_mask)
		return 0;

	if (aux_ops->ports_reopen) {
		rc = aux_ops->ports_reopen(aux_dev);
		if (rc) {
			dev_err(hdev->dev, "Failed to reopen the eth ports, %d\n", rc);
			return rc;
		}
	}

	cn_funcs->set_hw_cap(hdev, true);

	return 0;
}

int hl_cn_init(struct hl_device *hdev)
{
	struct hl_cn_properties *cn_props = &hdev->asic_prop.cn_props;
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hl_cn *cn = &hdev->cn;
	int rc;

	/*
	 * In init flow we initialize the NIC ports from scratch. In hard reset
	 * flow, we get here after the NIC ports were halted, hence we only need to reopen them.
	 */
	if (hdev->reset_info.in_reset)
		return hl_cn_reopen(hdev);

	cn->ports_mask &= GENMASK(cn_props->max_num_of_ports - 1, 0);
	cn->ports_ext_mask &= cn->ports_mask;
	cn->auto_neg_mask &= cn->ports_mask;

	/* check if the NIC is enabled */
	if (!hdev->cn.ports_mask)
		return 0;

	rc = cn_funcs->pre_core_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to pre init the NIC, %d\n", rc);
		return rc;
	}

	/* check if all ports are disabled by the FW */
	if (!hdev->cn.ports_mask) {
		dev_dbg(hdev->dev, "all NIC ports are disabled by the FW\n");
		return 0;
	}

	cn->eth_ports_mask = hdev->cn.eth_on_internal ? hdev->cn.ports_mask :
									hdev->cn.ports_ext_mask;

	/* verify the kernel module name as the auxiliary drivers will bind according to it */
	WARN_ONCE(strcmp(HL_NAME, KBUILD_MODNAME),
			"habanalabs name not in sync with kernel module name");

	rc = hl_cn_aux_drv_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to init CN driver, %d\n", rc);
		return rc;
	}

	cn_funcs->set_hw_cap(hdev, true);

	cn->is_initialized = true;

	return 0;
}

void hl_cn_fini(struct hl_device *hdev)
{
	struct hl_cn *cn = &hdev->cn;

	/* The NIC capability bit of each ASIC cannot be used as a prerequisite for this function,
	 * as we may arrive here after a failing hard reset w/o calling to hl_cn_reopen().
	 * But we can check if the NIC is totally disabled.
	 */
	if (!hdev->cn.ports_mask)
		return;

	if (!cn->is_initialized)
		return;

	hl_cn_aux_drv_fini(hdev);

	cn->is_initialized = false;
}

void hl_cn_stop(struct hl_device *hdev)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hl_cn *cn = &hdev->cn;
	struct hbl_cn_aux_ops *aux_ops;
	struct hbl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	aux_ops = aux_dev->aux_ops;

	if (!cn_funcs->get_hw_cap(hdev))
		return;

	if (aux_ops->ports_close)
		aux_ops->ports_close(aux_dev);

	/* Set NIC as not initialized. */
	cn_funcs->set_hw_cap(hdev, false);
}

void hl_cn_hard_reset_prepare(struct hl_device *hdev)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;

	if (!cn_funcs->get_hw_cap(hdev))
		return;

	cn_funcs->port_funcs->ports_stop_prepare(hdev, hdev->reset_info.fw_reset,
							hdev->device_fini_pending);
}

int hl_cn_control(struct hl_device *hdev, u32 op, void *input,	void *output, struct hl_ctx *ctx)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;

	if (!cn_funcs->get_hw_cap(hdev)) {
		dev_dbg(hdev->dev, "NIC is not initialized, can't execute request %d\n", op);
		return -EFAULT;
	}

	return cn_funcs->cmd_control(hdev, op, input, output, ctx->asid);
}

int hl_cn_ctx_init(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;

	if (!hdev->asic_funcs->cn_funcs->get_hw_cap(hdev))
		return 0;

	/* save this ctx for future usage */
	hdev->cn.ctx = ctx;

	return 0;
}

void hl_cn_ctx_fini(struct hl_ctx *ctx)
{
	ctx->hdev->cn.ctx = NULL;
}

int hl_cn_send_status(struct hl_device *hdev, int port, u8 cmd, u8 period)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;

	if (!cn_funcs->get_hw_cap(hdev)) {
		if (cmd != HBL_CN_STATUS_PERIODIC_STOP)
			return hl_cn_send_empty_status(hdev, port);
		return 0;
	}

	return cn_funcs->port_funcs->send_port_cpucp_status(hdev, port, cmd, period);
}

void hl_cn_synchronize_irqs(struct hl_device *hdev)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hbl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct hbl_cn_aux_ops *aux_ops;

	aux_ops = aux_dev->aux_ops;

	if (!cn_funcs->get_hw_cap(hdev))
		return;

	if (aux_ops->synchronize_irqs)
		aux_ops->synchronize_irqs(aux_dev);
}

int hl_cn_mmap(struct hl_device *hdev, u32 asid, struct vm_area_struct *vma)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;

	if (!cn_funcs->get_hw_cap(hdev))
		return -EFAULT;

	return cn_funcs->mmap(hdev, asid, vma);
}

static int hl_cn_aux_link_qual_to_hl_link_qual(struct hl_device *hdev,
						enum hbl_cn_aux_link_qual cn_aux_link_qual,
						enum hl_link_qual *hl_link_qual)
{
	switch (cn_aux_link_qual) {
	case HBL_CN_AUX_LINK_QUAL_POOR:
		*hl_link_qual = HL_LINK_QUAL_POOR;
		break;
	case HBL_CN_AUX_LINK_QUAL_GOOD:
		*hl_link_qual = HL_LINK_QUAL_GOOD;
		break;
	case HBL_CN_AUX_LINK_QUAL_EXCELLENT:
		*hl_link_qual = HL_LINK_QUAL_EXCELLENT;
		break;
	default:
		dev_dbg(hdev->dev, "link_type %d is invalid\n", cn_aux_link_qual);
		return -EINVAL;
	}

	return 0;
}

int hl_cn_get_port_status(struct hl_device *hdev, u32 port,
				struct hl_info_habana_link_state *link_state_info)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hbl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct hbl_cn_aux_port_status port_status;
	struct hbl_cn_aux_ops *aux_ops;
	enum hl_link_qual hl_link_qual;
	int rc;

	aux_ops = aux_dev->aux_ops;

	if (!cn_funcs->get_hw_cap(hdev))
		return -EFAULT;

	if (!aux_ops->get_port_status)
		return -EFAULT;

	rc = aux_ops->get_port_status(aux_dev, port, &port_status);
	if (rc)
		return rc;

	rc = hl_cn_aux_link_qual_to_hl_link_qual(hdev, port_status.link_qual, &hl_link_qual);
	if (rc)
		return rc;

	link_state_info->up = port_status.link_up;
	link_state_info->port_open = port_status.port_open;
	link_state_info->link_qual = (u8)hl_link_qual;

	return 0;
}

int hl_cn_dump_port_statistics(struct hl_device *hdev, u32 port, u64 str_buf_ptr, u64 val_buf_ptr,
				u32 *num_of_stat)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;

	if (!cn_funcs->get_hw_cap(hdev))
		return -EFAULT;

	return cn_funcs->port_funcs->dump_port_statistics(hdev, port, str_buf_ptr, val_buf_ptr,
								num_of_stat);
}

#ifdef _HAS_AUX_BUS_H
int hl_cn_check_ib_driver(struct hl_device *hdev)
{
	/* With Aux bus support, IB driver need not be loaded beforehand */
	return 0;
}
#else
int hl_cn_check_ib_driver(struct hl_device *hdev)
{
#ifdef HL_LOAD_IB
	struct module *modules_list;

	list_for_each_entry(modules_list, THIS_MODULE->list.prev, list) {
		if (!strcmp(modules_list->name, HL_IB_NAME))
			return 0;
	}

	dev_err(hdev->dev, "%s module is not found. Maybe it is unloaded?\n", HL_IB_NAME);
	return -ENODEV;
#else
	return 0;
#endif
}
#endif

int hl_cn_cpucp_info_get(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct cpucp_nic_info *cpucp_nic_info;
	dma_addr_t cpucp_nic_info_dma_addr;
	int rc;

	cpucp_nic_info = hl_cpu_accessible_dma_pool_alloc(hdev,
							sizeof(struct cpucp_nic_info),
							&cpucp_nic_info_dma_addr);
	if (!cpucp_nic_info) {
		dev_err(hdev->dev,
			"Failed to allocate DMA memory for CPU-CP NIC info packet\n");
		return -ENOMEM;
	}

	memset(cpucp_nic_info, 0, sizeof(struct cpucp_nic_info));

	/* Unfortunately, 0 is a valid type in this field from f/w perspective,
	 * so to support older f/w where they don't return this field, put
	 * here the max value so when converting serdes type to server type,
	 * we will put the UNKNOWN value into the server type.
	 */
	cpucp_nic_info->serdes_type = cpu_to_le16(U16_MAX);

	rc = hl_fw_cpucp_nic_info_get(hdev, cpucp_nic_info_dma_addr);
	if (rc)
		goto out;

	hl_cn_cpucp_info_le_to_cpu(cpucp_nic_info, &prop->cn_props.cpucp_info);

out:
	hl_cpu_accessible_dma_pool_free(hdev, sizeof(struct cpucp_nic_info), cpucp_nic_info);

	return 0;
}
