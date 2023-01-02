// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi2P.h"
#include "../include/gaudi2/gaudi2_fw_if.h"
#include "../include/gaudi2/asic_reg/gaudi2_regs.h"
#include "../include/gaudi2/gaudi2_reg_map.h"

#include "../include/hw_ip/mmu/mmu_general.h"

#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>

#define GAUDI2_FPGA_CPU_Q_TIMEOUT	60000000	/* 60s */
#define GAUDI2_FPGA_CPUCP_INFO_TIMEOUT	60000000	/* 60s */

#define GAUDI2_DMA_POOL_BLK_SIZE	0x100		/* 256 bytes */
#define GAUDI2_FPGA_DDR_SIZE		0x40000000ull	/* 1GB */

#define GAUDI2_FPGA_MSIX_BAR_SIZE	0x8000ull	/* 32KB */
#define GAUDI2_FPGA_CFG_BAR_SIZE	0x4000000ull	/* 64M */

/*
 * In FPGA the debug configuration area (DCORE0_TPC0_ROM_TABLE - DCORE3_TPC5_EML_CS)
 * is not implemented and BAR0 starting from DCORE0_TPC0_QM_DCCM (1000007FFC000000)
 * note that this "gap" of unmapped config area must be taken into account in
 * further calculations
 */
#define GAUDI2_FPGA_CFG_BASE		0x1000007FFC000000ull
#define GAUDI2_FPGA_SRAM_BASE		0x1001000200000000ull
#define GAUDI2_FPGA_SRAM_SIZE		0x600000ull
#define GAUDI2_FPGA_CFG_SIZE		0x1000000ull

#define GAUDI2_FPGA_WAIT_FOR_BL_TIMEOUT_USEC	15000000        /* 15s */

static int gaudi2_fpga_set_fixed_properties(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	rc = gaudi2_set_fixed_properties(hdev);
	if (rc)
		return rc;

	prop->device_dma_offset_for_host_access = HOST_PHYS_BASE_1;

	prop->host_base_address = HOST_PHYS_BASE_1;
	prop->host_end_address = prop->host_base_address + HOST_PHYS_SIZE_1;

	prop->sram_base_address = GAUDI2_FPGA_SRAM_BASE;
	prop->sram_size = GAUDI2_FPGA_SRAM_SIZE;
	prop->sram_end_address = prop->sram_base_address + prop->sram_size;
	prop->sram_user_base_address = prop->sram_base_address +
					SRAM_USER_BASE_OFFSET;

	prop->dram_size = GAUDI2_FPGA_DDR_SIZE;
	prop->dram_end_address = prop->dram_base_address +
					prop->dram_size;

	prop->cfg_base_address = GAUDI2_FPGA_CFG_BASE;
	prop->cfg_size = GAUDI2_FPGA_CFG_SIZE;

	/* no need in CB pool in FPGA */
	prop->cb_pool_cb_cnt = 0;
	prop->cb_pool_cb_size = 0;

	prop->clk_pll_index = HL_GAUDI2_MME_PLL;

	return 0;
}

static int gaudi2_fpga_init_iatu(struct hl_device *hdev)
{
	return 0;
}

static int gaudi2_fpga_early_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pci_dev *pdev = hdev->pdev;
	int rc;

	rc = gaudi2_fpga_set_fixed_properties(hdev);
	if (rc)
		return rc;

	/* Check BAR sizes */
	if (pci_resource_len(pdev, SRAM_CFG_BAR_ID) !=
			GAUDI2_FPGA_CFG_BAR_SIZE) {
		dev_err(hdev->dev,
			"Not " HL_NAME "? BAR %d size %llu, expecting %llu\n",
			SRAM_CFG_BAR_ID,
			pci_resource_len(pdev, SRAM_CFG_BAR_ID),
			GAUDI2_FPGA_CFG_BAR_SIZE);
		rc = -ENODEV;
		goto free_queue_props;
	}

	if (pci_resource_len(pdev, MSIX_BAR_ID) != GAUDI2_FPGA_MSIX_BAR_SIZE) {
		dev_err(hdev->dev,
			"Not " HL_NAME "? BAR %d size %llu, expecting %llu\n",
			MSIX_BAR_ID, pci_resource_len(pdev, MSIX_BAR_ID),
			GAUDI2_FPGA_MSIX_BAR_SIZE);
		rc = -ENODEV;
		goto free_queue_props;
	}

	prop->dram_pci_bar_size = pci_resource_len(pdev, DRAM_BAR_ID);
	hdev->dram_pci_bar_start = pci_resource_start(pdev, DRAM_BAR_ID);

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

	return 0;

pci_fini:
	hl_pci_fini(hdev);
free_queue_props:
	kfree(hdev->asic_prop.hw_queues_props);
	return rc;
}

static int gaudi2_fpga_enable_msix(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	int rc, irq;

	if (gaudi2->hw_cap_initialized & HW_CAP_MSIX)
		return 0;

	rc = pci_alloc_irq_vectors(hdev->pdev, 1, 1, PCI_IRQ_MSI);
	if (rc < 0) {
		dev_err(hdev->dev,
			"MSI-X: Failed to enable support -- %d/%d\n",
			1, rc);
		return rc;
	}

	dev_dbg(hdev->dev,
		"Going to enable MSI-X interrupt for CS completions\n");

	dev_dbg(hdev->dev, "Going to enable MSI-X EQ\n");

	irq = pci_irq_vector(hdev->pdev, GAUDI2_IRQ_NUM_EVENT_QUEUE);
	rc = request_irq(irq, hl_irq_handler_eq, 0,
			"gaudi2 cpu eq", &hdev->event_queue);
	if (rc) {
		dev_err(hdev->dev, "Failed to request IRQ %d", irq);
		goto free_irq_vectors;
	}

	gaudi2->hw_cap_initialized |= HW_CAP_MSIX;

	return 0;

free_irq_vectors:
	pci_free_irq_vectors(hdev->pdev);

	return rc;
}

static void gaudi2_fpga_set_pci_memory_regions(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pci_mem_region *region;

	/* CFG */
	/*
	 * TODO: need to revise this code to comply with the configuration
	 * area BAR mapping gap.
	 * till then, mark region as disabled
	 */
	region = &hdev->pci_mem_region[PCI_REGION_CFG];
	region->region_base = GAUDI2_FPGA_CFG_BASE;
	region->region_size = GAUDI2_FPGA_CFG_SIZE;
	region->offset_in_bar = GAUDI2_FPGA_CFG_BASE - CFG_BASE;
	region->bar_size = GAUDI2_FPGA_CFG_BAR_SIZE;
	region->bar_id = SRAM_CFG_BAR_ID;
	region->used = 0;

	/* SRAM */
	region = &hdev->pci_mem_region[PCI_REGION_SRAM];
	region->region_base = GAUDI2_FPGA_SRAM_BASE;
	region->region_size = GAUDI2_FPGA_SRAM_SIZE;
	region->offset_in_bar = (GAUDI2_FPGA_SRAM_BASE - DRAM_PHYS_BASE);
	region->bar_size = prop->dram_pci_bar_size;
	region->bar_id = DRAM_BAR_ID;
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
	region->used = 0;
}

static int gaudi2_fpga_hw_init(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	int rc = 0;

	gaudi2->dram_bar_cur_addr = DRAM_PHYS_BASE;

	gaudi2_pre_hw_init_reset_config(hdev);

	rc = gaudi2_init_cpu(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CPU\n");
		return rc;
	}

	rc = gaudi2_init_cpu_queues(hdev, GAUDI2_FPGA_CPU_Q_TIMEOUT);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CPU H/W queues\n");
		return rc;
	}

	rc = gaudi2->cpucp_info_get(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to get cpucp info\n");
		return rc;
	}

	rc = gaudi2_fpga_enable_msix(hdev);
	if (rc)
		return rc;

	return 0;
}

static void gaudi2_fpga_sync_irqs(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;

	if (!(gaudi2->hw_cap_initialized & HW_CAP_MSIX))
		return;

	synchronize_irq(pci_irq_vector(hdev->pdev, GAUDI2_IRQ_NUM_EVENT_QUEUE));
}

static void gaudi2_fpga_disable_msix(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	int irq;

	/* no gaudi2 device means sw/hw init did not run */
	if (!gaudi2)
		return;

	if (!(gaudi2->hw_cap_initialized & HW_CAP_MSIX))
		return;

	gaudi2_fpga_sync_irqs(hdev);

	irq = pci_irq_vector(hdev->pdev, GAUDI2_IRQ_NUM_EVENT_QUEUE);
	free_irq(irq, &hdev->event_queue);
	pci_free_irq_vectors(hdev->pdev);

	gaudi2->hw_cap_initialized &= ~HW_CAP_MSIX;
}

static void gaudi2_fpga_hw_fini(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	gaudi2_fpga_disable_msix(hdev);

	WREG32(mmCPU_IF_QUEUE_INIT, PQ_INIT_STATUS_NA);
}

static int gaudi2_fpga_late_init(struct hl_device *hdev)
{
	int rc;

	rc = hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_ENABLE_PCI_ACCESS, 0x0);
	if (rc) {
		dev_err(hdev->dev, "Failed to enable PCI access from CPU\n");
		return rc;
	}

	return 0;
}

static int gaudi2_fpga_test_queues(struct hl_device *hdev)
{
	int rc, ret_val = 0;

	if (hdev->cpu_queues_enable) {
		rc = gaudi2_test_cpu_queue(hdev);
		if (rc)
			ret_val = -EINVAL;
	}

	return ret_val;
}

static void gaudi2_fpga_halt_engines(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
}

static int gaudi2_fpga_debug_coresight(struct hl_device *hdev, struct hl_ctx *ctx, void *data)
{
	return 0;
}

static void gaudi2_fpga_halt_coresight(struct hl_device *hdev, struct hl_ctx *ctx)
{
}

static void gaudi2_fpga_init_dynamic_firmware_loader(struct hl_device *hdev)
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

	dynamic_loader->wait_for_bl_timeout = GAUDI2_FPGA_WAIT_FOR_BL_TIMEOUT_USEC;
}

static void gaudi2_fpga_init_firmware_preload_params(struct hl_device *hdev)
{
	struct pre_fw_load_props *pre_fw_load = &hdev->fw_loader.pre_fw_load;

	pre_fw_load->cpu_boot_status_reg = mmPSOC_GLOBAL_CONF_CPU_BOOT_STATUS;
	pre_fw_load->sts_boot_dev_sts0_reg = mmCPU_BOOT_DEV_STS0;
	pre_fw_load->sts_boot_dev_sts1_reg = mmCPU_BOOT_DEV_STS1;
	pre_fw_load->boot_err0_reg = mmCPU_BOOT_ERR0;
	pre_fw_load->boot_err1_reg = mmCPU_BOOT_ERR1;
	pre_fw_load->wait_for_preboot_timeout = GAUDI2_BOOT_FIT_REQ_TIMEOUT_USEC;
}

static void gaudi2_fpga_init_firmware_loader(struct hl_device *hdev)
{
	struct fw_load_mgr *fw_loader = &hdev->fw_loader;

	/* fill common fields */
	fw_loader->fw_comp_loaded = FW_TYPE_NONE;
	fw_loader->boot_fit_img.image_name = GAUDI2_BOOT_FIT_FILE;
	fw_loader->linux_img.image_name = GAUDI2_LINUX_FW_FILE;
	fw_loader->boot_fit_timeout = GAUDI2_BOOT_FIT_REQ_TIMEOUT_USEC;
	fw_loader->skip_bmc = false;
	fw_loader->sram_bar_id = DRAM_BAR_ID;
	fw_loader->dram_bar_id = DRAM_BAR_ID;
	fw_loader->cpu_timeout = GAUDI2_FPGA_CPU_TIMEOUT;

	gaudi2_fpga_init_dynamic_firmware_loader(hdev);
}

static bool gaudi2_fpga_is_device_idle(struct hl_device *hdev, u64 *mask_arr, u8 mask_len,
					struct engines_data *e)
{
	return false;
}

static int gaudi2_fpga_send_cpu_message(struct hl_device *hdev, u32 *msg,
				u16 len, u32 timeout, u64 *result)
{
	if (timeout)
		timeout *= 10;
	else
		timeout = GAUDI2_FPGA_CPU_TIMEOUT;

	return gaudi2_send_cpu_message(hdev, msg, len, timeout, result);
}

static int gaudi2_fpga_ctx_init(struct hl_ctx *ctx)
{
	return 0;
}

static void gaudi2_fpga_ctx_fini(struct hl_ctx *ctx)
{
}

static int gaudi2_fpga_scrub_device_mem(struct hl_device *hdev)
{
	return 0;
}

static int gaudi2_fpga_pci_bars_map(struct hl_device *hdev)
{
	static const char * const name[] = {"CFG_SRAM", "MSIX", "DRAM"};
	bool is_wc[3] = {false, false, true};
	int rc;

	rc = hl_pci_bars_map(hdev, name, is_wc);
	if (rc)
		return rc;

	/*
	 * rmmio should point to the config base.
	 * since in FPGA the first portion of the config area (the debug space)
	 * is not mapped the gaudi2 definition pointing to the first mapped
	 * config area (see comment in definition of GAUDI2_FPGA_CFG_BASE).
	 * since the use of rmmio (and hence the RREG/WREG) relies on the fact that
	 * giving register offset will produce the right address we giving a
	 * negative offset to the BAR start address.
	 * this will be valid as long as no access is made to the debug area.
	 */
	hdev->rmmio = hdev->pcie_bar[SRAM_CFG_BAR_ID] -
			(GAUDI2_FPGA_CFG_BASE - CFG_BASE);

	return 0;
}

static int gaudi2_fpga_load_boot_fit_to_device(struct hl_device *hdev)
{
	void __iomem *dst;
	u64 ddr_bar_offset = GAUDI2_FPGA_SRAM_BASE - DRAM_PHYS_BASE;

	dst = hdev->pcie_bar[DRAM_BAR_ID] + ddr_bar_offset +
					BOOT_FIT_SRAM_OFFSET;

	return hl_fw_load_fw_to_device(hdev, GAUDI2_BOOT_FIT_FILE, dst, 0, 0);
}

static void *gaudi2_fpga_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	void *kernel_addr = dma_alloc_coherent(&hdev->pdev->dev, size,
						dma_handle, flags);

	/* Shift to the device's base physical address of host memory */
	if (kernel_addr)
		*dma_handle += HOST_PHYS_BASE_1;

	return kernel_addr;
}

static void gaudi2_fpga_dma_free_coherent(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle)
{
	/* Cancel the device's base physical address of host memory */
	dma_addr_t fixed_dma_handle = dma_handle - HOST_PHYS_BASE_1;

	dma_free_coherent(&hdev->pdev->dev, size, cpu_addr, fixed_dma_handle);
}

static uint64_t gaudi2_fpga_set_hbm_bar_base(struct hl_device *hdev, u64 addr)
{
	return addr;
}

static int gaudi2_fpga_scrub_device_dram(struct hl_device *hdev, u64 val)
{
	return -EOPNOTSUPP;
}

static const struct hl_asic_funcs gaudi2_fpga_funcs = {
	.early_init = gaudi2_fpga_early_init,
	.early_fini = gaudi2_early_fini,
	.late_init = gaudi2_fpga_late_init,
	.late_fini = gaudi2_late_fini,
	.sw_init = gaudi2_sw_init,
	.sw_fini = gaudi2_sw_fini,
	.hw_init = gaudi2_fpga_hw_init,
	.hw_fini = gaudi2_fpga_hw_fini,
	.halt_engines = gaudi2_fpga_halt_engines,
	.suspend = NULL,
	.resume = NULL,
	.mmap = gaudi2_mmap,
	.ring_doorbell = gaudi2_ring_doorbell,
	.pqe_write = gaudi2_pqe_write,
	.asic_dma_alloc_coherent = gaudi2_fpga_dma_alloc_coherent,
	.asic_dma_free_coherent = gaudi2_fpga_dma_free_coherent,
	.scrub_device_mem = gaudi2_fpga_scrub_device_mem,
	.scrub_device_dram = gaudi2_fpga_scrub_device_dram,
	.get_int_queue_base = NULL,
	.test_queues = gaudi2_fpga_test_queues,
	.asic_dma_pool_zalloc = gaudi2_dma_pool_zalloc,
	.asic_dma_pool_free = gaudi2_dma_pool_free,
	.cpu_accessible_dma_pool_alloc = gaudi2_cpu_accessible_dma_pool_alloc,
	.cpu_accessible_dma_pool_free = gaudi2_cpu_accessible_dma_pool_free,
	.hl_dma_unmap_sgtable = hl_dma_unmap_sgtable,
	.cs_parser = gaudi2_cs_parser,
	.asic_dma_map_sgtable = hl_dma_map_sgtable,
	.add_end_of_cb_packets = NULL,
	.update_eq_ci = gaudi2_update_eq_ci,
	.context_switch = gaudi2_context_switch,
	.restore_phase_topology = gaudi2_restore_phase_topology,
	.debugfs_read_dma = gaudi2_debugfs_read_dma,
	.add_device_attr = gaudi2_add_device_attr,
	.handle_eqe = gaudi2_handle_eqe,
	.get_events_stat = gaudi2_get_events_stat,
	.read_pte = NULL,
	.write_pte = NULL,
	.mmu_invalidate_cache = gaudi2_mmu_invalidate_cache,
	.mmu_invalidate_cache_range = gaudi2_mmu_invalidate_cache_range,
	.mmu_prefetch_cache_range = NULL,
	.send_heartbeat = gaudi2_send_heartbeat,
	.debug_coresight = gaudi2_fpga_debug_coresight,
	.is_device_idle = gaudi2_fpga_is_device_idle,
	.compute_reset_late_init = gaudi2_compute_reset_late_init,
	.hw_queues_lock = gaudi2_hw_queues_lock,
	.hw_queues_unlock = gaudi2_hw_queues_unlock,
	.get_pci_id = gaudi2_get_pci_id,
	.get_eeprom_data = gaudi2_get_eeprom_data,
	.get_monitor_dump = gaudi2_get_monitor_dump,
	.send_cpu_message = gaudi2_fpga_send_cpu_message,
	.nic_init = hl_nic_init,
	.nic_fini = hl_nic_fini,
	.nic_control = hl_nic_control,
	.pci_bars_map = gaudi2_fpga_pci_bars_map,
	.init_iatu = gaudi2_fpga_init_iatu,
	.rreg = hl_rreg,
	.wreg = hl_wreg,
	.halt_coresight = gaudi2_fpga_halt_coresight,
	.ctx_init = gaudi2_fpga_ctx_init,
	.ctx_fini = gaudi2_fpga_ctx_fini,
	.pre_schedule_cs = gaudi2_pre_schedule_cs,
	.get_queue_id_for_cq = gaudi2_get_queue_id_for_cq,
	.load_firmware_to_device = gaudi2_load_firmware_to_device,
	.load_boot_fit_to_device = gaudi2_fpga_load_boot_fit_to_device,
	.get_signal_cb_size = gaudi2_get_signal_cb_size,
	.get_wait_cb_size = gaudi2_get_wait_cb_size,
	.gen_signal_cb = gaudi2_gen_signal_cb,
	.gen_wait_cb = gaudi2_gen_wait_cb,
	.reset_sob = gaudi2_reset_sob,
	.reset_sob_group = gaudi2_reset_sob_group,
	.get_device_time = gaudi2_get_device_time,
	.pb_print_security_errors = NULL,
	.collective_wait_init_cs = gaudi2_collective_wait_init_cs,
	.collective_wait_create_jobs = gaudi2_collective_wait_create_jobs,
	.get_dec_base_addr = gaudi2_get_dec_base_addr,
	.scramble_addr = hl_mmu_scramble_addr,
	.descramble_addr = hl_mmu_descramble_addr,
	.ack_protection_bits_errors = gaudi2_ack_protection_bits_errors,
	.get_hw_block_id = gaudi2_get_hw_block_id,
	.hw_block_mmap = gaudi2_block_mmap,
	.enable_events_from_fw = gaudi2_enable_events_from_fw,
	.ack_mmu_errors = gaudi2_ack_mmu_page_fault_or_access_error,
	.map_pll_idx_to_fw_idx = gaudi2_map_pll_idx_to_fw_idx,
	.init_firmware_preload_params = gaudi2_fpga_init_firmware_preload_params,
	.init_firmware_loader = gaudi2_fpga_init_firmware_loader,
	.init_cpu_scrambler_dram = gaudi2_init_scrambler_hbm,
	.state_dump_init = gaudi2_state_dump_init,
	.get_sob_addr = &gaudi2_get_sob_addr,
	.set_pci_memory_regions = gaudi2_fpga_set_pci_memory_regions,
	.get_stream_master_qid_arr = gaudi2_get_stream_master_qid_arr,
	.mmu_get_real_page_size = gaudi2_mmu_get_real_page_size,
	.nic_funcs = &gaudi2_nic_funcs,
	.access_dev_mem = hl_access_dev_mem,
	.set_dram_bar_base = gaudi2_fpga_set_hbm_bar_base,
	.send_device_activity = gaudi2_send_device_activity,
	.set_binning_masks = gaudi2_set_binning_masks,
};

void gaudi2_fpga_set_asic_funcs(struct hl_device *hdev)
{
	hdev->asic_funcs = &gaudi2_fpga_funcs;
}
