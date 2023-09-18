// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "cn.h"

#include "../common/habanalabs.h"
#include "../include/common/pci_ids.h"
#include <linux/file.h>

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

static bool hl_cn_device_operational(struct hl_aux_dev *aux_dev)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_device_operational(hdev, NULL);
}

static void hl_cn_hw_access_lock(struct hl_aux_dev *aux_dev)
	__acquires(&hdev->cn.hw_access_lock)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	mutex_lock(&hdev->cn.hw_access_lock);
}

static void hl_cn_hw_access_unlock(struct hl_aux_dev *aux_dev)
	__releases(&hdev->cn.hw_access_lock)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	mutex_unlock(&hdev->cn.hw_access_lock);
}

static void hl_cn_spmu_get_stats_info(struct hl_aux_dev *aux_dev, u32 port,
					struct hl_cn_stat **stats, u32 *n_stats)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hl_cn_port_funcs *port_funcs = hdev->asic_funcs->cn_funcs->port_funcs;

	port_funcs->spmu_get_stats_info(hdev, port, stats, n_stats);
}

static int hl_cn_spmu_config(struct hl_aux_dev *aux_dev, u32 port, u32 num_event_types,
				u32 event_types[], bool enable)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hl_cn_port_funcs *port_funcs = hdev->asic_funcs->cn_funcs->port_funcs;

	return port_funcs->spmu_config(hdev, port, num_event_types, event_types, enable);
}

static int hl_cn_spmu_sample(struct hl_aux_dev *aux_dev, u32 port, u32 num_out_data,
				u64 out_data[])
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hl_cn_port_funcs *port_funcs = hdev->asic_funcs->cn_funcs->port_funcs;

	return port_funcs->spmu_sample(hdev, port, num_out_data, out_data);
}

static void hl_cn_device_reset(struct hl_aux_dev *aux_dev)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	hl_device_reset(hdev, HL_DRV_RESET_HARD);
}

static void *hl_cn_dma_alloc_coherent(struct hl_aux_dev *aux_dev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_asic_dma_alloc_coherent(hdev, size, dma_handle, flag);
}

static void hl_cn_dma_free_coherent(struct hl_aux_dev *aux_dev, size_t size, void *cpu_addr,
					dma_addr_t dma_handle)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	hl_asic_dma_free_coherent(hdev, size, cpu_addr, dma_handle);
}

static void *hl_cn_dma_pool_zalloc(struct hl_aux_dev *aux_dev, size_t size, gfp_t mem_flags,
					dma_addr_t *dma_handle)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_asic_dma_pool_zalloc(hdev, size, mem_flags, dma_handle);
}

static void hl_cn_dma_pool_free(struct hl_aux_dev *aux_dev, void *vaddr, dma_addr_t dma_addr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	hl_asic_dma_pool_free(hdev, vaddr, dma_addr);
}

static int hl_cn_map_vmalloc_range(struct hl_aux_dev *aux_dev, u64 vmalloc_va, u64 device_va,
					u64 size)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);

	return hl_map_vmalloc_range(cn->ctx, vmalloc_va, device_va, size);
}

static int hl_cn_unmap_vmalloc_range(struct hl_aux_dev *aux_dev, u64 device_va)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);

	return hl_unmap_vmalloc_range(cn->ctx, device_va);
}

static u64 hl_cn_reserve_va_block(struct hl_aux_dev *aux_dev, u64 size)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_reserve_va_block(hdev, cn->ctx, HL_VA_RANGE_TYPE_HOST, size, PAGE_SIZE);
}

static int hl_cn_unreserve_va_block(struct hl_aux_dev *aux_dev, u64 addr, u64 size)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_unreserve_va_block(hdev, cn->ctx, addr, size);
}

static int hl_cn_get_hw_block_handle(struct hl_aux_dev *aux_dev, u64 address, u64 *handle)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_get_hw_block_handle(hdev, address, handle, NULL);
}

static int hl_cn_dma_mmap(struct hl_aux_dev *aux_dev, struct vm_area_struct *vma, void *cpu_addr,
				dma_addr_t dma_addr, size_t size)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hdev->asic_funcs->mmap(hdev, vma, cpu_addr, dma_addr, size);
}

static int hl_cn_user_mmap(struct hl_aux_dev *aux_dev, struct vm_area_struct *vma)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);

	return __hl_mmap(cn->ctx->hpriv, vma);
}

static u8 hl_cn_dram_readb(struct hl_aux_dev *aux_dev, u64 addr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	u64 val = 0;
	int rc;

	rc = hdev->asic_funcs->access_dev_mem(hdev, PCI_REGION_DRAM, addr, &val, DEBUGFS_READ8);
	if (rc)
		dev_crit(hdev->dev, "Failed to readb from dev_mem addr 0x%llx\n", addr);

	return val;
}

static u32 hl_cn_dram_readl(struct hl_aux_dev *aux_dev, u64 addr)
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

static u64 hl_cn_dram_readq(struct hl_aux_dev *aux_dev, u64 addr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	u64 val = 0;
	int rc;

	rc = hdev->asic_funcs->access_dev_mem(hdev, PCI_REGION_DRAM, addr, &val, DEBUGFS_READ64);
	if (rc)
		dev_crit(hdev->dev, "Failed to readq from dev_mem addr 0x%llx\n", addr);

	return val;
}

static void hl_cn_dram_writeb(struct hl_aux_dev *aux_dev, u8 val, u64 addr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	u64 data = val;
	int rc;

	rc = hdev->asic_funcs->access_dev_mem(hdev, PCI_REGION_DRAM, addr, &data, DEBUGFS_WRITE8);
	if (rc)
		dev_crit(hdev->dev, "Failed to writeb to dev_mem addr 0x%llx\n", addr);
}

static void hl_cn_dram_writel(struct hl_aux_dev *aux_dev, u32 val, u64 addr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	u64 data = val;
	int rc;

	rc = hdev->asic_funcs->access_dev_mem(hdev, PCI_REGION_DRAM, addr, &data, DEBUGFS_WRITE32);
	if (rc)
		dev_crit(hdev->dev, "Failed to writel to dev_mem addr 0x%llx\n", addr);
}

static int hl_cn_dram_writeq(struct hl_aux_dev *aux_dev, u64 val, u64 addr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	u64 data = val;
	int rc;

	rc = hdev->asic_funcs->access_dev_mem(hdev, PCI_REGION_DRAM, addr, &data, DEBUGFS_WRITE64);
	if (rc)
		dev_crit(hdev->dev, "Failed to writel to dev_mem addr 0x%llx\n", addr);

	return rc;
}

static u32 hl_cn_rreg(struct hl_aux_dev *aux_dev, u32 reg)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hdev->asic_funcs->rreg(hdev, reg);
}

static void hl_cn_wreg(struct hl_aux_dev *aux_dev, u32 reg, u32 val)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hdev->asic_funcs->wreg(hdev, reg, val);
}

static void hl_cn_set_priv_assertions(struct hl_aux_dev *aux_dev, bool enable)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	hdev->asic_funcs->set_priv_assertions(hdev, enable);
}

static int hl_cn_get_compute_user_ctx(struct hl_aux_dev *aux_dev, int user_fd)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct drm_file *file_priv;
	struct hl_fpriv *hpriv;
	struct file *file;
	int rc = 0;

	/* CN driver can independently manage its resources and context.
	 * However, for HL devices, corresponding HW resources can also be managed by compute side.
	 * To avoid contention (e.g. abrupt application close) between them, enforce orderly FD
	 * closure. This facilitates that CN destroy runs first, followed by compute fini.
	 */
	file = fget(user_fd);
	if (!file)
		return -EBADF;

	mutex_lock(&hdev->fpriv_list_lock);

	if (list_empty(&hdev->fpriv_list)) {
		dev_dbg(hdev->dev, "no open user context\n");
		rc = -ESRCH;
		goto out;
	}

	/* The list should contain a single element as currently only a single user context is
	 * allowed. Therefore get the first entry.
	 */
	hpriv = list_first_entry(&hdev->fpriv_list, struct hl_fpriv, dev_node);

	file_priv = file->private_data;
	if (hpriv != file_priv->driver_priv) {
		dev_dbg(hdev->dev, "user FD mismatch\n");
		rc = -EINVAL;
	}

out:
	mutex_unlock(&hdev->fpriv_list_lock);

	if (rc)
		fput(file);

	return rc;
}

static void hl_cn_put_compute_user_ctx(struct hl_aux_dev *aux_dev)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hl_fpriv *hpriv;
	struct file *file;

	mutex_lock(&hdev->fpriv_list_lock);
	hpriv = list_first_entry(&hdev->fpriv_list, struct hl_fpriv, dev_node);
	mutex_unlock(&hdev->fpriv_list_lock);

	file = hpriv->file_priv->filp;

	/* We can assert here that all CN resources which might have dependency on compute side
	 * are already released. Hence, release reference to compute file.
	 */
	fput(file);
}

static int hl_cn_poll_reg(struct hl_aux_dev *aux_dev, u32 reg, u64 timeout_us,
				hl_cn_poll_cond_func func, void *arg)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	u32 val;

	return hl_poll_timeout(hdev, reg, val, func(val, arg), 1000, timeout_us);
}

static void hl_cn_get_cpucp_info(struct hl_aux_dev *aux_dev,
					struct hl_cn_cpucp_info *hl_cn_cpucp_info)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hl_cn_cpucp_info *cn_cpucp_info;

	cn_cpucp_info = &hdev->asic_prop.cn_props.cpucp_info;

	memcpy(hl_cn_cpucp_info, cn_cpucp_info, sizeof(*cn_cpucp_info));
}

static void hl_cn_cpucp_info_le_to_cpu(struct cpucp_nic_info *cpucp_nic_info,
					struct hl_cn_cpucp_info *hl_cn_cpucp_info)
{
	int i;

	for (i = 0 ; i < CPUCP_MAX_NICS ; i++) {
		memcpy(&hl_cn_cpucp_info->mac_addrs[i], &cpucp_nic_info->mac_addrs[i],
			sizeof(cpucp_nic_info->mac_addrs[i]));
		hl_cn_cpucp_info->tx_swap_map[i] = le16_to_cpu(cpucp_nic_info->tx_swap_map[i]);
	}

	for (i = 0 ; i < CPUCP_NIC_MASK_ARR_LEN ; i++) {
		hl_cn_cpucp_info->link_mask[i] = le64_to_cpu(cpucp_nic_info->link_mask[i]);
		hl_cn_cpucp_info->link_ext_mask[i] = le64_to_cpu(cpucp_nic_info->link_ext_mask[i]);
		hl_cn_cpucp_info->auto_neg_mask[i] = le64_to_cpu(cpucp_nic_info->auto_neg_mask[i]);
	}

	for (i = 0 ; i < CPUCP_NIC_POLARITY_ARR_LEN ; i++) {
		hl_cn_cpucp_info->pol_tx_mask[i] = le64_to_cpu(cpucp_nic_info->pol_tx_mask[i]);
		hl_cn_cpucp_info->pol_rx_mask[i] = le64_to_cpu(cpucp_nic_info->pol_rx_mask[i]);
	}

	hl_cn_cpucp_info->serdes_type = (enum cpucp_serdes_type)
					le16_to_cpu(cpucp_nic_info->serdes_type);

	memcpy(hl_cn_cpucp_info->qsfp_eeprom, cpucp_nic_info->qsfp_eeprom,
		sizeof(cpucp_nic_info->qsfp_eeprom));
}

static int hl_cn_send_cpu_message(struct hl_aux_dev *aux_dev, u32 *msg, u16 len, u32 timeout,
					u64 *result)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hdev->asic_funcs->send_cpu_message(hdev, msg, len, timeout, result);
}

static void *hl_cn_cpu_accessible_dma_pool_alloc(struct hl_aux_dev *aux_dev, size_t size,
							dma_addr_t *dma_handle)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	return hl_cpu_accessible_dma_pool_alloc(hdev, size, dma_handle);
}

static void hl_cn_cpu_accessible_dma_pool_free(struct hl_aux_dev *aux_dev, size_t size,
						void *vaddr)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);

	hl_cpu_accessible_dma_pool_free(hdev,  size, vaddr);
}

static void hl_cn_post_send_status(struct hl_aux_dev *aux_dev, u32 port)
{
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);
	struct hl_device *hdev = container_of(cn, struct hl_device, cn);
	struct hl_cn_port_funcs *port_funcs = hdev->asic_funcs->cn_funcs->port_funcs;

	port_funcs->post_send_status(hdev, port);
}

static int hl_cn_get_asic_type(struct hl_device *hdev, enum hl_cn_asic_type *asic_type)
{
	switch (hdev->asic_type) {
	case ASIC_GAUDI_SIM:
	case ASIC_GAUDI_HL2000M_SIM:
	case ASIC_GAUDI:
	case ASIC_GAUDI_SEC:
	case ASIC_GAUDI_HL2000M:
	case ASIC_GAUDI_HL2000M_SEC:
		*asic_type = HL_ASIC_GAUDI;
		break;
	case ASIC_GAUDI2_SIM:
	case ASIC_GAUDI2B_SIM:
	case ASIC_GAUDI2_SIM_ARC:
	case ASIC_GAUDI2B_SIM_ARC:
	case ASIC_GAUDI2:
	case ASIC_GAUDI2B:
		*asic_type = HL_ASIC_GAUDI2;
		break;
	case ASIC_GAUDI3:
	case ASIC_GAUDI3_SINGLE_DIE:
	case ASIC_GAUDI3_SIM:
	case ASIC_GAUDI3_SIM_ARC:
	case ASIC_GAUDI3_SIM_SINGLE_DIE:
	case ASIC_GAUDI3_SIM_SINGLE_DIE_ARC:
	case ASIC_GAUDI3_FPGA:
		*asic_type = HL_ASIC_GAUDI3;
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
	struct asic_fixed_properties *asic_props = &hdev->asic_prop;
	struct hl_cn_aux_data *aux_data;
	struct hl_cn_aux_ops *aux_ops;
	struct hl_cn *cn = &hdev->cn;
	struct hl_aux_dev *aux_dev;
	u64 dram_kmd_size;
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
	aux_dev->type = HL_AUX_DEV_CN;

	aux_data->pdev = hdev->pdev;
	aux_data->dev = hdev->dev;
	aux_data->driver_ver = hdev->driver_ver;
	aux_data->ports_mask = hdev->cn.ports_mask;
	aux_data->ext_ports_mask = cn->eth_ports_mask;
	aux_data->auto_neg_mask = hdev->cn.auto_neg_mask;
	aux_data->vendor_id = PCI_VENDOR_ID_HABANALABS;
	aux_data->pci_id = hdev->asic_funcs->get_pci_id(hdev);
	aux_data->minor = hdev->id;
	aux_data->fw_ver = asic_props->cpucp_info.cpucp_version;
	aux_data->nic_drv_addr = asic_props->nic_drv_addr;
	aux_data->nic_drv_size = asic_props->nic_drv_size;
	aux_data->macro_cfg_size = asic_props->macro_cfg_size;
	aux_data->pending_reset_long_timeout = hdev->pldm ? HL_PLDM_HARD_RESET_MAX_TIMEOUT :
									HL_HARD_RESET_MAX_TIMEOUT;
	aux_data->id = hdev->cdev_idx;
	aux_data->pldm = hdev->pldm;
	aux_data->skip_phy_init = hdev->cn.skip_phy_init;
	aux_data->load_phy_fw = hdev->cn.load_fw;
	aux_data->cpucp_fw = !!(hdev->fw_components & FW_TYPE_BOOT_CPU);
	aux_data->supports_coresight = hdev->supports_coresight;
	aux_data->use_fw_serdes_info = cn->use_fw_serdes_info;
	aux_data->cache_line_size = asic_props->cache_line_size;
	aux_data->clk = asic_props->clk;
	aux_data->kernel_asid = HL_KERNEL_ASID_ID;
	aux_data->card_location = cn->card_location;
	aux_data->mmu_enable = true;
	aux_data->lanes_per_port = hdev->cn.lanes_per_port;
	aux_data->mmap_type_flag = HL_MMAP_TYPE_CN_MEM;
	aux_data->device_timeout = HL_DEVICE_TIMEOUT_USEC;
	aux_data->dram_enable = hdev->dram_enable;
	aux_data->fw_major_version = hdev->fw_inner_major_ver;
	aux_data->fw_minor_version = hdev->fw_inner_minor_ver;
	aux_data->fw_app_cpu_boot_dev_sts0 = asic_props->fw_app_cpu_boot_dev_sts0;
	aux_data->fw_app_cpu_boot_dev_sts1 = asic_props->fw_app_cpu_boot_dev_sts1;
	aux_data->cpucp_checkers_shift = NIC_CHECKERS_CHECK_SHIFT;
	aux_data->num_of_dies = hdev->asic_prop.num_of_dies;

	aux_data->gaudi2_setup_type = hdev->gaudi2_setup_type;

	rc = hl_cn_get_asic_type(hdev, &aux_data->asic_type);
	if (rc) {
		dev_err(hdev->dev, "failed to set eth aux data asic type\n");
		goto free_aux_ops;
	}

	dram_kmd_size = asic_props->dram_user_base_address - asic_props->dram_base_address;
	aux_data->dram_size = (asic_props->dram_size < dram_kmd_size) ? 0 : dram_kmd_size;

	/* set cn -> accel ops */
	aux_ops->device_operational = hl_cn_device_operational;
	aux_ops->hw_access_lock = hl_cn_hw_access_lock;
	aux_ops->hw_access_unlock = hl_cn_hw_access_unlock;
	aux_ops->spmu_get_stats_info = hl_cn_spmu_get_stats_info;
	aux_ops->spmu_config = hl_cn_spmu_config;
	aux_ops->spmu_sample = hl_cn_spmu_sample;
	aux_ops->device_reset = hl_cn_device_reset;
	aux_ops->dma_alloc_coherent = hl_cn_dma_alloc_coherent;
	aux_ops->dma_free_coherent = hl_cn_dma_free_coherent;
	aux_ops->dma_pool_zalloc = hl_cn_dma_pool_zalloc;
	aux_ops->dma_pool_free = hl_cn_dma_pool_free;
	aux_ops->map_vmalloc_range = hl_cn_map_vmalloc_range;
	aux_ops->unmap_vmalloc_range = hl_cn_unmap_vmalloc_range;
	aux_ops->reserve_va_block = hl_cn_reserve_va_block;
	aux_ops->unreserve_va_block = hl_cn_unreserve_va_block;
	aux_ops->get_hw_block_handle = hl_cn_get_hw_block_handle;
	aux_ops->dma_mmap = hl_cn_dma_mmap;
	aux_ops->user_mmap = hl_cn_user_mmap;
	aux_ops->dram_readb = hl_cn_dram_readb;
	aux_ops->dram_readl = hl_cn_dram_readl;
	aux_ops->dram_readq = hl_cn_dram_readq;
	aux_ops->dram_writeb = hl_cn_dram_writeb;
	aux_ops->dram_writel = hl_cn_dram_writel;
	aux_ops->dram_writeq = hl_cn_dram_writeq;
	aux_ops->rreg = hl_cn_rreg;
	aux_ops->wreg = hl_cn_wreg;
	aux_ops->set_priv_assertions = hl_cn_set_priv_assertions;
	aux_ops->get_compute_user_ctx = hl_cn_get_compute_user_ctx;
	aux_ops->put_compute_user_ctx = hl_cn_put_compute_user_ctx;
	aux_ops->poll_reg = hl_cn_poll_reg;
	aux_ops->get_cpucp_info = hl_cn_get_cpucp_info;
	aux_ops->send_cpu_message = hl_cn_send_cpu_message;
	aux_ops->cpu_accessible_dma_pool_alloc = hl_cn_cpu_accessible_dma_pool_alloc;
	aux_ops->cpu_accessible_dma_pool_free = hl_cn_cpu_accessible_dma_pool_free;
	aux_ops->post_send_status = hl_cn_post_send_status;

	cn_funcs->set_cn_data(hdev);

	return 0;

free_aux_ops:
	kfree(aux_ops);
free_aux_data:
	kfree(aux_data);

	return rc;
}

static void hl_cn_aux_data_fini(struct hl_device *hdev)
{
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;

	kfree(aux_dev->aux_ops);
	kfree(aux_dev->aux_data);
}

#ifdef _HAS_AUX_BUS_H
static void cn_adev_release(struct device *dev)
{
	struct hl_aux_dev *aux_dev = container_of(dev, struct hl_aux_dev, adev.dev);
	struct hl_cn *cn = container_of(aux_dev, struct hl_cn, cn_aux_dev);

	cn->is_cn_aux_dev_initialized = false;
}

static int hl_cn_aux_drv_init(struct hl_device *hdev)
{
	struct hl_cn *cn = &hdev->cn;
	struct hl_aux_dev *aux_dev = &cn->cn_aux_dev;
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
	int (*probe)(struct hl_aux_dev *aux_dev);
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

	probe = symbol_get(hl_cn_probe);
	if (!probe) {
		dev_err(hdev->dev, "hl_cn_probe symbol wasn't found\n");
		rc = -ENODEV;
		goto probe_fail;
	}

	rc = probe(&hdev->cn.cn_aux_dev);
	symbol_put(hl_cn_probe);

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
	void (*remove)(struct hl_aux_dev *aux_dev);
	struct hl_cn *cn = &hdev->cn;
	struct module *module;

	if (!cn->is_cn_aux_dev_initialized)
		return;

	module = find_module(HL_CN_NAME);
	if (!module) {
		dev_err(hdev->dev, "Module %s was not found\n", HL_CN_NAME);
		return;
	}

	remove = symbol_get(hl_cn_remove);
	if (!remove) {
		dev_err(hdev->dev, "hl_cn_remove symbol wasn't found\n");
		return;
	}

	remove(&hdev->cn.cn_aux_dev);
	symbol_put(hl_cn_remove);
	module_put(module);

	hl_cn_aux_data_fini(hdev);
}
#endif

int hl_cn_reopen(struct hl_device *hdev)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct hl_cn_aux_ops *aux_ops = aux_dev->aux_ops;
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

	hdev->cn.ports_mask &= GENMASK(cn_props->max_num_of_ports - 1, 0);
	hdev->cn.ports_ext_mask &= hdev->cn.ports_mask;
	hdev->cn.auto_neg_mask &= hdev->cn.ports_mask;

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

	/* The NIC capability bit of each ASIC cannot be used as a prerequisite
	 * for this function, as we may arrive here after a failing hard reset
	 * w/o calling to hl_cn_ports_reopen().
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
	struct hl_cn_aux_ops *aux_ops;
	struct hl_aux_dev *aux_dev;

	aux_dev = &cn->cn_aux_dev;
	aux_ops = aux_dev->aux_ops;

	if (!cn_funcs->get_hw_cap(hdev))
		return;

	if (aux_ops->ports_stop)
		aux_ops->ports_stop(aux_dev);

	/* Set NIC as not initialized. */
	cn_funcs->set_hw_cap(hdev, false);
}

void hl_cn_hard_reset_prepare(struct hl_device *hdev)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct hl_cn_aux_ops *aux_ops;

	aux_ops = aux_dev->aux_ops;

	if (!cn_funcs->get_hw_cap(hdev))
		return;

	if (aux_ops->ports_stop_prepare)
		aux_ops->ports_stop_prepare(aux_dev, hdev->reset_info.fw_reset,
						hdev->device_fini_pending);
}

int hl_cn_control(struct hl_device *hdev, u32 op, void *input,	void *output, struct hl_ctx *ctx)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct hl_cn_aux_ops *aux_ops;

	aux_ops = aux_dev->aux_ops;

	if (!cn_funcs->get_hw_cap(hdev)) {
		dev_dbg(hdev->dev, "NIC is not initialized, can't execute request %d\n", op);
		return -EFAULT;
	}

	if (aux_ops->cmd_control)
		return aux_ops->cmd_control(aux_dev, op, input, output, ctx->asid);

	return -EFAULT;
}

int hl_cn_ctx_init(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hl_cn *cn = &hdev->cn;
	struct hl_aux_dev *aux_dev = &cn->cn_aux_dev;
	struct hl_cn_aux_ops *aux_ops;
	int rc;

	aux_ops = aux_dev->aux_ops;

	if (!cn_funcs->get_hw_cap(hdev))
		return 0;

	if (aux_ops->ctx_init) {
		/* must be done before calling CN ctx_init as it might be used there */
		cn->ctx = ctx;

		rc = aux_ops->ctx_init(aux_dev, ctx->asid);
		if (rc) {
			cn->ctx = NULL;
			return rc;
		}
	}

	return 0;
}

void hl_cn_ctx_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_cn *cn = &hdev->cn;
	struct hl_aux_dev *aux_dev = &cn->cn_aux_dev;
	struct hl_cn_aux_ops *aux_ops;

	aux_ops = aux_dev->aux_ops;

	/* Check the context pointer instead of the capability bit because the CN ctx_fini should
	 * be called even if the ports are stopped.
	 */
	if (!cn->ctx)
		return;

	/* No need to check for NULL pointer because here the context is not NULL so we can be sure
	 * that the aux_ops pointer is not NULL either.
	 */
	aux_ops->ctx_fini(aux_dev, ctx->asid);

	/* must be done after calling CN ctx_fini as it might be used there */
	cn->ctx = NULL;
}

int hl_cn_send_status(struct hl_device *hdev, int port, u8 cmd, u8 period)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct hl_cn_aux_ops *aux_ops;

	aux_ops = aux_dev->aux_ops;

	if (!cn_funcs->get_hw_cap(hdev)) {
		if (cmd != HL_CN_STATUS_PERIODIC_STOP)
			return hl_cn_send_empty_status(hdev, port);
		return 0;
	}

	if (aux_ops->send_port_cpucp_status)
		return aux_ops->send_port_cpucp_status(aux_dev, port, cmd, period);

	return -EFAULT;
}

void hl_cn_synchronize_irqs(struct hl_device *hdev)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct hl_cn_aux_ops *aux_ops;

	aux_ops = aux_dev->aux_ops;

	if (!cn_funcs->get_hw_cap(hdev))
		return;

	if (aux_ops->synchronize_irqs)
		aux_ops->synchronize_irqs(aux_dev);
}

int hl_cn_mmap(struct hl_device *hdev, u32 asid, struct vm_area_struct *vma)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct hl_cn_aux_ops *aux_ops;

	aux_ops = aux_dev->aux_ops;

	if (!cn_funcs->get_hw_cap(hdev))
		return -EFAULT;

	if (aux_ops->mmap)
		return aux_ops->mmap(aux_dev, asid, vma);

	return -EFAULT;
}

int hl_cn_get_port_state(struct hl_device *hdev, u32 port, bool *up)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct hl_cn_aux_ops *aux_ops;

	aux_ops = aux_dev->aux_ops;

	if (!cn_funcs->get_hw_cap(hdev))
		return -EFAULT;

	if (aux_ops->get_port_state)
		return aux_ops->get_port_state(aux_dev, port, up);

	return -EFAULT;
}

int hl_cn_get_port_statistics(struct hl_device *hdev, u32 port,
				struct hl_cn_port_statistics *out)
{
	struct hl_cn_funcs *cn_funcs = hdev->asic_funcs->cn_funcs;
	struct hl_aux_dev *aux_dev = &hdev->cn.cn_aux_dev;
	struct hl_cn_aux_ops *aux_ops;

	aux_ops = aux_dev->aux_ops;

	if (!cn_funcs->get_hw_cap(hdev))
		return -EFAULT;

	if (aux_ops->get_port_statistics)
		return aux_ops->get_port_statistics(aux_dev, port, out);

	return -EFAULT;
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

	dev_err(hdev->dev, "habanalabs_ib module is not found. Maybe %s module is unloaded?\n",
		HL_IB_NAME);
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
