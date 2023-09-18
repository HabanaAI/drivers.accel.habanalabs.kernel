// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "gaudi3P.h"
#include "../include/gaudi3/asic_reg/gaudi3_regs.h"
#include "../include/hw_ip/mmu/mmu_general.h"
#include "../include/gaudi3/gaudi3_reg_map.h"

#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>

#define GAUDI3_FPGA_CPU_Q_TIMEOUT	60000000	/* 60s  */

#define GAUDI3_FPGA_MSIX_BAR_SIZE	0x8000ull	/* 32KB */
#define GAUDI3_FPGA_CFG_BAR_SIZE	0x4000000ull	/* 64M  */

#define GAUDI3_FPGA_CFG_BASE		CFG_BASE
#define GAUDI3_FPGA_CFG_SIZE		0x4000000ull    /* 64MB */

/*
 * In FPGA, the DRAM mapped at lower address, the SRAM is after it. 0xD000000 after the
 * end of the DRAM area. This address configuration has set, as a result of FPGA limition
 * with the asic SRAM start address.
 */

#define GAUDI3_FPGA_DRAM_BASE		0x201000000000000ull
#define GAUDI3_FPGA_DRAM_SIZE		0x200000000ull  /* 8GB  */

#define GAUDI3_FPGA_SRAM_BASE		(GAUDI3_FPGA_DRAM_BASE + GAUDI3_FPGA_DRAM_SIZE)
#define GAUDI3_FPGA_SRAM_USR_BASE	(GAUDI3_FPGA_SRAM_BASE + SRAM_MODE_0_SINGLE_DIE_OFFSET)

#define GAUDI3_FPGA_SRAM_SIZE			0x600000ull	/* 6MB  */
#define GAUDI3_FPGA_WAIT_FOR_BL_TIMEOUT_USEC	15000000	/* 15s */


static int gaudi3_fpga_set_fixed_properties(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	rc = gaudi3_set_fixed_properties(hdev);
	if (rc)
		return rc;

	prop->device_dma_offset_for_host_access = HOST_PHYS_BASE_1;

	prop->host_base_address = HOST_PHYS_BASE_1;
	prop->host_end_address = prop->host_base_address + HOST_PHYS_SIZE_1;

	prop->sram_base_address = GAUDI3_FPGA_SRAM_USR_BASE;
	prop->sram_size = GAUDI3_FPGA_SRAM_SIZE;
	prop->sram_end_address = prop->sram_base_address + prop->sram_size;
	prop->sram_user_base_address = prop->sram_base_address;

	prop->dram_size = GAUDI3_FPGA_DRAM_SIZE;
	prop->dram_end_address = prop->dram_base_address +
					prop->dram_size;

	prop->cfg_base_address = GAUDI3_FPGA_CFG_BASE;
	prop->cfg_size = GAUDI3_FPGA_CFG_SIZE;

	/* no need in CB pool in FPGA */
	prop->cb_pool_cb_cnt = 0;
	prop->cb_pool_cb_size = 0;

	return 0;
}

static int gaudi3_fpga_init_iatu(struct hl_device *hdev)
{
	return 0;
}

static void gaudi3_fpga_init_firmware_loader(struct hl_device *hdev)
{
	struct fw_load_mgr *fw_loader = &hdev->fw_loader;
	struct dynamic_fw_load_mgr *dynamic_loader;
	struct cpu_dyn_regs *dyn_regs;

	/* fill common fields */
	fw_loader->fw_comp_loaded = FW_TYPE_NONE;
	fw_loader->boot_fit_img.image_name = GAUDI3_BOOT_FIT_FILE;
	fw_loader->boot_fit_timeout = GAUDI3_BOOT_FIT_REQ_TIMEOUT_USEC;
	fw_loader->skip_bmc = false;
	fw_loader->sram_bar_id = SRAM_DRAM_BAR_ID;
	fw_loader->dram_bar_id = SRAM_DRAM_BAR_ID;
	fw_loader->cpu_timeout = GAUDI3_FPGA_CPU_TIMEOUT_USEC;

	/* here we update initial values for few specific dynamic regs (as
	 * before reading the first descriptor from FW those value has to be
	 * hard-coded) in later stages of the protocol those values will be
	 * updated automatically by reading the FW descriptor so data there
	 * will always be up-to-date
	 */
	dynamic_loader = &hdev->fw_loader.dynamic_loader;
	dyn_regs = &dynamic_loader->comm_desc.cpu_dyn_regs;
	dyn_regs->kmd_msg_to_cpu = cpu_to_le32(mmD0_PSOC_GLOBAL_CONF_BASE +
						mmGLOBAL_CONF_KMD_MSG_TO_CPU);
	dyn_regs->cpu_cmd_status_to_host = cpu_to_le32(mmD0_PSOC_GLOBAL_CONF_BASE +
							mmCPU_CMD_STATUS_TO_HOST);
	dynamic_loader->wait_for_bl_timeout = GAUDI3_FPGA_WAIT_FOR_BL_TIMEOUT_USEC;
}

static int gaudi3_fpga_early_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pci_dev *pdev = hdev->pdev;
	int rc;

	rc = hl_cn_check_ib_driver(hdev);
	if (rc)
		return rc;

	rc = gaudi3_fpga_set_fixed_properties(hdev);
	if (rc)
		return rc;

	/* Check BAR sizes */
	if (pci_resource_len(pdev, CFG_BAR_ID) !=
		GAUDI3_FPGA_CFG_BAR_SIZE) {
		dev_err(hdev->dev,
			"Not " HL_NAME "? BAR %d size %llu, expecting %llu\n",
			CFG_BAR_ID,
			pci_resource_len(pdev, CFG_BAR_ID),
			GAUDI3_FPGA_CFG_BAR_SIZE);
		rc = -ENODEV;
		goto exit;
	}

	if (pci_resource_len(pdev, MSIX_BAR_ID) != GAUDI3_FPGA_MSIX_BAR_SIZE) {
		dev_err(hdev->dev,
			"Not " HL_NAME "? BAR %d size %llu, expecting %llu\n",
			MSIX_BAR_ID, pci_resource_len(pdev, MSIX_BAR_ID),
			GAUDI3_FPGA_MSIX_BAR_SIZE);
		rc = -ENODEV;
		goto exit;
	}

	prop->dram_pci_bar_size = pci_resource_len(pdev, SRAM_DRAM_BAR_ID);
	hdev->dram_pci_bar_start = pci_resource_start(pdev, SRAM_DRAM_BAR_ID);
	rc = hl_pci_init(hdev);

	if (rc)
		goto exit;

	/* Before continuing in the initialization, we need to read the
	 * preboot state and capabilities
	 */
	rc = hl_fw_read_preboot_status(hdev);
	if (rc) {
		if (hdev->reset_on_preboot_fail)
			/* we are already on failure flow, so don't check if hw_fini fails. */
			hdev->asic_funcs->hw_fini(hdev, true, false);
		goto pci_fini;
	}

	return 0;

pci_fini:
	hl_pci_fini(hdev);
exit:
	return rc;
}

static void gaudi3_fpga_set_pci_memory_regions(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pci_mem_region *region;

	/* CFG
	 * Note that we set here region base to TPC_DBG_BASE, and not just the
	 * mapped portion(DBG/TPC not mapped), since we wanted to support both accesses
	 * to cfg area from debugfs and direct call to RREG/WREG.
	 * For debugfs access we subtract this region base from the whole address
	 * and the result will be offset from rmmio, and if this region base
	 * was set to GAUDI3_FPGA_CFG_BASE then the offset will be wrong.
	 * In addition this region boundaries are not checked in FW logic
	 * so no big deal.
	 */
	region = &hdev->pci_mem_region[PCI_REGION_CFG];
	region->region_base = TPC_DBG_BASE;
	region->region_size = GAUDI3_FPGA_CFG_SIZE + TPC_DBG_SIZE + DBG_SIZE;
	region->offset_in_bar = 0;
	region->bar_size = GAUDI3_FPGA_CFG_BAR_SIZE;
	region->bar_id = CFG_BAR_ID;
	region->used = 1;

	/* DRAM */
	region = &hdev->pci_mem_region[PCI_REGION_DRAM];
	region->region_base = GAUDI3_FPGA_DRAM_BASE;
	region->region_size = hdev->asic_prop.dram_size;
	region->offset_in_bar = 0;
	region->bar_size = prop->dram_pci_bar_size;
	region->bar_id = SRAM_DRAM_BAR_ID;
	region->used = 1;

	/* SRAM */
	region = &hdev->pci_mem_region[PCI_REGION_SRAM];
	region->region_base = GAUDI3_FPGA_SRAM_USR_BASE;
	region->region_size = GAUDI3_FPGA_SRAM_SIZE;
	region->offset_in_bar = GAUDI3_FPGA_DRAM_SIZE + SRAM_MODE_0_SINGLE_DIE_OFFSET;
	region->bar_size = prop->dram_pci_bar_size;
	region->bar_id = SRAM_DRAM_BAR_ID;
	region->used = 1;
}

static int gaudi3_fpga_enable_msix(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	int rc;

	if (gaudi3->hw_cap_initialized & HW_CAP_MSIX)
		return 0;

	rc = hl_alloc_irq_vectors(hdev, 1, 1, PCI_IRQ_MSI);
	if (rc < 0) {
		dev_err(hdev->dev, "MSI-X: Failed to enable support -- %d/%d\n", 1, rc);
		return rc;
	}

	rc = gaudi3_eq_enable_msix(hdev);
	if (rc) {
		dev_err(hdev->dev, "MSI-X: Failed to enable EQ interrupt, %d", rc);
		goto free_irq_vectors;
	}

	gaudi3->hw_cap_initialized |= HW_CAP_MSIX;

	return 0;

free_irq_vectors:
	pci_free_irq_vectors(hdev->pdev);

	return rc;
}

static void gaudi3_fpga_sync_irqs(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;

	if (!(gaudi3->hw_cap_initialized & HW_CAP_MSIX))
		return;

	synchronize_irq(pci_irq_vector(hdev->pdev, GAUDI3_IRQ_NUM_EVENT_QUEUE));
}

static void gaudi3_fpga_disable_msix(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;

	if (!gaudi3)
		return;

	if (!(gaudi3->hw_cap_initialized & HW_CAP_MSIX))
		return;

	gaudi3_fpga_sync_irqs(hdev);

	gaudi3_eq_disable_msix(hdev);

	hl_free_irq_vectors(hdev);

	gaudi3->hw_cap_initialized &= ~HW_CAP_MSIX;
}

static int gaudi3_fpga_sw_init(struct hl_device *hdev)
{
	int rc;

	rc = gaudi3_sw_init(hdev);
	if (rc)
		return rc;
	hdev->supports_cb_mapping = false;

	return 0;
}

static int gaudi3_fpga_hw_init(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	int rc;

	rc = gaudi3_init_cpu(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CPU\n");
		return rc;
	}

	rc = gaudi3_init_cpu_queues(hdev, GAUDI3_FPGA_CPU_Q_TIMEOUT);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize CPU H/W queues\n");
		return rc;
	}

	/* Get CPU info */
	gaudi3->cpucp_info_get(hdev);

	/* Enable MSIX in FPGA */
	rc = gaudi3_fpga_enable_msix(hdev);
	if (rc)
		return rc;

	return 0;
}

static int gaudi3_fpga_hw_fini(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	int rc;

	gaudi3_fpga_disable_msix(hdev);
	rc = gaudi3_hw_fini(hdev, hard_reset, fw_reset);
	if (rc) {
		dev_err(hdev->dev, "Failed in HW finish\n");
		return rc;
	}

	return 0;
}

static int gaudi3_fpga_late_init(struct hl_device *hdev)
{
	int rc;

	rc = hl_fw_send_pci_access_msg(hdev, CPUCP_PACKET_ENABLE_PCI_ACCESS, 0x0);
	if (rc) {
		dev_err(hdev->dev, "Failed to enable PCI access from CPU\n");
		return rc;
	}

	return 0;
}

static void gaudi3_fpga_halt_engines(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
}

static int gaudi3_fpga_debug_coresight(struct hl_device *hdev, struct hl_ctx *ctx, void *data)
{
	return 0;
}

static void gaudi3_fpga_halt_coresight(struct hl_device *hdev, struct hl_ctx *ctx)
{
}

static bool gaudi3_fpga_is_device_idle(struct hl_device *hdev, u64 *mask_arr, u8 mask_len,
					struct engines_data *e)
{
	return true;
}

static int gaudi3_fpga_compute_reset_late_init(struct hl_device *hdev)
{
	return 0;
}

static int gaudi3_fpga_send_cpu_message(struct hl_device *hdev, u32 *msg,
				u16 len, u32 timeout, u64 *result)
{
	if (timeout)
		timeout *= 10;
	else
		timeout = GAUDI3_FPGA_CPU_TIMEOUT_USEC;

	return gaudi3_send_cpu_message(hdev, msg, len, timeout, result);
}

static int gaudi3_fpga_ctx_init(struct hl_ctx *ctx)
{
	return 0;
}

static void gaudi3_fpga_ctx_fini(struct hl_ctx *ctx)
{
}

static int gaudi3_fpga_scrub_device_mem(struct hl_device *hdev)
{
	return 0;
}

static void gaudi3_fpga_no_fw_monitor(struct hl_device *hdev, bool *stop_monitor)
{
}

static int gaudi3_fpga_pci_bars_map(struct hl_device *hdev)
{
	static const char * const name[] = {"CFG", "MSIX", "SRAM_DRAM"};
	bool is_wc[3] = {false, false, true};
	int rc;

	rc = hl_pci_bars_map(hdev, name, is_wc);
	if (rc)
		return rc;

	/*
	 * rmmio should always point to start of the cfg bar
	 * (since all registers offsets in specs are from this value)
	 * TPC and DBG areas are not mapped in fpga.
	 * since the use of rmmio relies on the fact that giving register
	 * offset will generate the right address, we should set the rmmio
	 * to the beginning of the bar.
	 */
	hdev->rmmio = hdev->pcie_bar[CFG_BAR_ID] - (GAUDI3_FPGA_CFG_BASE - TPC_DBG_BASE);

	return 0;
}

static int gaudi3_fpga_load_boot_fit_to_device(struct hl_device *hdev)
{
	return 0;
}

static void *gaudi3_fpga_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	void *kernel_addr = dma_alloc_coherent(&hdev->pdev->dev, size,
						dma_handle, flags);

	/* Shift to the device's base physical address of host memory */
	if (kernel_addr)
		*dma_handle += HOST_PHYS_BASE_1;

	return kernel_addr;
}

static void gaudi3_fpga_dma_free_coherent(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle)
{
	/* Cancel the device's base physical address of host memory */
	dma_addr_t fixed_dma_handle = dma_handle - HOST_PHYS_BASE_1;

	dma_free_coherent(&hdev->pdev->dev, size, cpu_addr, fixed_dma_handle);
}

static uint64_t gaudi3_fpga_set_hbm_bar_base(struct hl_device *hdev, u64 addr)
{
	return addr == GAUDI3_FPGA_DRAM_BASE ? addr : U64_MAX;
}

static int gaudi3_fpga_scrub_device_dram(struct hl_device *hdev, u64 val)
{
	return -EOPNOTSUPP;
}

static const struct hl_asic_funcs gaudi3_fpga_funcs = {
	.early_init = gaudi3_fpga_early_init,
	.early_fini = gaudi3_early_fini,
	.late_init = gaudi3_fpga_late_init,
	.late_fini = gaudi3_late_fini,
	.sw_init = gaudi3_fpga_sw_init,
	.sw_fini = gaudi3_sw_fini,
	.hw_init = gaudi3_fpga_hw_init,
	.hw_fini = gaudi3_fpga_hw_fini,
	.halt_engines = gaudi3_fpga_halt_engines,
	.suspend = NULL,
	.resume = NULL,
	.mmap = gaudi3_mmap,
	.ring_doorbell = gaudi3_ring_doorbell,
	.pqe_write = gaudi3_pqe_write,
	.asic_dma_alloc_coherent = gaudi3_fpga_dma_alloc_coherent,
	.asic_dma_free_coherent = gaudi3_fpga_dma_free_coherent,
	.scrub_device_mem = gaudi3_fpga_scrub_device_mem,
	.scrub_device_dram = gaudi3_fpga_scrub_device_dram,
	.get_int_queue_base = NULL,
	.test_queues = gaudi3_test_cpu_queue,
	.asic_dma_pool_zalloc = gaudi3_dma_pool_zalloc,
	.asic_dma_pool_free = gaudi3_dma_pool_free,
	.cpu_accessible_dma_pool_alloc = gaudi3_cpu_accessible_dma_pool_alloc,
	.cpu_accessible_dma_pool_free = gaudi3_cpu_accessible_dma_pool_free,
	.dma_unmap_sgtable = hl_asic_dma_unmap_sgtable,
	.cs_parser = NULL,
	.dma_map_sgtable = hl_asic_dma_map_sgtable,
	.add_end_of_cb_packets = NULL,
	.update_eq_ci = gaudi3_update_eq_ci,
	.context_switch = gaudi3_context_switch,
	.restore_phase_topology = gaudi3_restore_phase_topology,
	.debugfs_read_dma = gaudi3_debugfs_read_dma,
	.add_device_attr = gaudi3_add_device_attr,
	.handle_eqe = NULL,
	.get_events_stat = gaudi3_get_events_stat,
	.read_pte = NULL,
	.write_pte = NULL,
	.mmu_invalidate_cache = gaudi3_mmu_invalidate_cache,
	.mmu_invalidate_cache_range = gaudi3_mmu_invalidate_cache_range,
	.mmu_prefetch_cache_range = gaudi3_mmu_prefetch_cache_range,
	.send_heartbeat = gaudi3_send_heartbeat,
	.debug_coresight = gaudi3_fpga_debug_coresight,
	.is_device_idle = gaudi3_fpga_is_device_idle,
	.compute_reset_late_init = gaudi3_fpga_compute_reset_late_init,
	.hw_queues_lock = gaudi3_hw_queues_lock,
	.hw_queues_unlock = gaudi3_hw_queues_unlock,
	.get_pci_id = gaudi3_get_pci_id,
	.get_eeprom_data = gaudi3_get_eeprom_data,
	.get_monitor_dump = gaudi3_get_monitor_dump,
	.send_cpu_message = gaudi3_fpga_send_cpu_message,
	.cn_init = hl_cn_init,
	.cn_fini = hl_cn_fini,
	.cn_control = hl_cn_control,
	.pci_bars_map = gaudi3_fpga_pci_bars_map,
	.init_iatu = gaudi3_fpga_init_iatu,
	.rreg = hl_rreg,
	.wreg = hl_wreg,
	.halt_coresight = gaudi3_fpga_halt_coresight,
	.ctx_init = gaudi3_fpga_ctx_init,
	.ctx_fini = gaudi3_fpga_ctx_fini,
	.pre_schedule_cs = NULL,
	.get_queue_id_for_cq = gaudi3_get_queue_id_for_cq,
	.load_firmware_to_device = NULL,
	.load_boot_fit_to_device = gaudi3_fpga_load_boot_fit_to_device,
	.get_signal_cb_size = NULL,
	.get_wait_cb_size = NULL,
	.gen_signal_cb = NULL,
	.gen_wait_cb = NULL,
	.reset_sob = NULL,
	.reset_sob_group = NULL,
	.get_device_time = gaudi3_get_device_time,
	.pb_print_security_errors = NULL,
	.collective_wait_init_cs = NULL,
	.collective_wait_create_jobs = NULL,
	.get_dec_base_addr = gaudi3_get_dec_base_addr,
	.scramble_addr = hl_mmu_scramble_addr,
	.descramble_addr = hl_mmu_descramble_addr,
	.ack_protection_bits_errors = gaudi3_ack_protection_bits_errors,
	.get_hw_block_id = gaudi3_get_hw_block_id,
	.hw_block_mmap = gaudi3_block_mmap,
	.enable_events_from_fw = gaudi3_enable_events_from_fw,
	.ack_mmu_errors = gaudi3_ack_mmu_page_fault_or_access_error,
	.map_pll_idx_to_fw_idx = gaudi3_map_pll_idx_to_fw_idx,
	.init_firmware_preload_params = gaudi3_init_firmware_preload_params,
	.init_firmware_loader = gaudi3_fpga_init_firmware_loader,
	.init_cpu_scrambler_dram = gaudi3_init_scrambler,
	.state_dump_init = gaudi3_state_dump_init,
	.no_fw_monitor = gaudi3_fpga_no_fw_monitor,
	.get_sob_addr = &gaudi3_get_sob_addr,
	.set_pci_memory_regions = gaudi3_fpga_set_pci_memory_regions,
	.get_stream_master_qid_arr = gaudi3_get_stream_master_qid_arr,
	.cn_funcs = &gaudi3_cn_funcs,
	.access_dev_mem = hl_access_dev_mem,
	.set_dram_bar_base = gaudi3_fpga_set_hbm_bar_base,
	.send_device_activity = gaudi3_send_device_activity,
	.alloc_irq_vectors = gaudi3_alloc_irq_vectors,
	.free_irq_vectors = gaudi3_free_irq_vectors,
	.irq_vector = gaudi3_irq_vector,
	.fw_security_emulation_init = gaudi3_fw_security_emulation_init,
	.fw_security_emulation_fini = gaudi3_fw_security_emulation_fini,
	.set_dram_properties = gaudi3_set_dram_properties,
	.set_binning_masks = gaudi3_set_binning_masks,
};

void gaudi3_fpga_set_asic_funcs(struct hl_device *hdev)
{
	hdev->asic_funcs = &gaudi3_fpga_funcs;
}
