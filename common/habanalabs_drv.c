// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#define pr_fmt(fmt)		"habanalabs: " fmt

#include "habanalabs.h"
#include "version.h"
#include "../include/hw_ip/pci/pci_general.h"
#include "habanalabs_compat_accel.h"

#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/module.h>
#include <linux/kthread.h>
#ifdef _HAS_SCHED_CLOCK_H
#include <linux/sched/clock.h>
#endif

#define CREATE_TRACE_POINTS
#include <trace/events/habanalabs.h>

#define HL_DRIVER_AUTHOR	"HabanaLabs Kernel Driver Team"

#define HL_DRIVER_DESC		"Driver for HabanaLabs's AI Accelerators"

#define HL_MODULE_VERSION	__stringify(HL_DRIVER_MAJOR) "."\
				__stringify(HL_DRIVER_MINOR) "."\
				__stringify(HL_DRIVER_PATCHLEVEL) "-"\
				__stringify(HL_DRIVER_GIT_SHA)

MODULE_AUTHOR(HL_DRIVER_AUTHOR);
MODULE_DESCRIPTION(HL_DRIVER_DESC);
MODULE_LICENSE("GPL v2");
MODULE_VERSION(HL_MODULE_VERSION);

static int hl_major;
static struct class *hl_class;
static DEFINE_IDR(hl_devs_idr);
static DEFINE_MUTEX(hl_devs_idr_lock);

static int ifh;

struct hl_pci_link_monitor {
	struct completion comp;
	struct task_struct *thread;
	bool in_teardown;
};

static struct hl_pci_link_monitor hl_pci_mon;

#define HL_DEFAULT_TIMEOUT_LOCKED	30	/* 30 seconds */
#define GAUDI_DEFAULT_TIMEOUT_LOCKED	600	/* 10 minutes */

static int timeout_locked = HL_DEFAULT_TIMEOUT_LOCKED;
static int reset_on_lockup = 1;
static int memory_scrub;
static int nic_poll_enable = 1;
static ulong boot_error_status_mask = ULONG_MAX;

/* Parameters that don't need bringup_flags_enable but are not upstreamed */
static int low_freq;
static int card_type = cpucp_card_type_pmc;
static uint nic_ports_mask = GENMASK(23, 0);
static uint nic_ports_ext_mask = GENMASK(23, 0);
static uint nic_auto_neg_mask = GENMASK(23, 0);
static uint nic_qp_drain_time = NIC_QP_DRAIN_TIME;
static int ignore_fw_nic_info;
static int nic_lanes_per_port = PORT_LANES_2;
static int skip_iatu_for_unsecured_device;
static int reset_upon_device_release = 1;
static int gaudi2_setup_type;
static ulong enable_events_tracing;
static int ignore_eeprom_errors;

/* Parameters for bring-up/debugging */
static int pldm;
static int bringup_flags_enable;
static int bfe_gaudi_huge_page_optimization = 1;
static int bfe_mmu_enable = MMU_EN_ALL;
static int bfe_clock_gating = 1;
static uint bfe_mme_mask = 0x3;
static ulong bfe_tpc_mask = 0x3FF;
static uint bfe_decoder_mask = 0x3FF;
static uint bfe_rotator_mask = 0x3;
static uint bfe_pdma_ch_mask = 0xFFF;
static uint bfe_edma_mask = 0xF;
static int bfe_dram_enable = 1;
static int bfe_reset_pcilink;
static int bfe_config_pll;
static ulong bfe_fw_components = FW_TYPE_ALL_TYPES;
static int bfe_fw_communication_enable = 1;
static int bfe_heartbeat = 1;
static int bfe_axi_drain = AXI_DRAIN_SKIP;
static int bfe_security_enable = 1;
static int bfe_sram_scrambler_enable = 1;
static int bfe_dram_scrambler_enable = 1;
static int bfe_hbm_ecc_enable = 1;
static int bfe_compatibility_mode;
static int bfe_hard_reset_on_fw_events = 1;
static int bfe_bmc_enable = 1;
static int bfe_nic_load_fw;
static int bfe_rl_enable = 1;
static int bfe_sram_binning;
static ulong bfe_tpc_binning = 0x1000000; /* 25th tpc is binned by default */
static ulong bfe_dram_binning;
static uint bfe_edma_binning;
static uint bfe_decoder_binning = 0x200; /* 10th decoder is binned by default */
static int bfe_reset_on_preboot_fail = 1;
static int bfe_force_driver_reset = FORCE_DRIVER_RESET_NONE;
static int bfe_force_driver_clock_gating;
static int bfe_pll_async_if_enable;
static int bfe_bootfit_relocatable;
static int bfe_reset_if_device_not_idle = 1;
static int bfe_hbm_pll_freq = 1600;
static int bfe_half_nominal_pll_mode;
static int bfe_scrub_arc_dccm;
static int bfe_skip_cluster_config = 1;
static int bfe_fw_cfg_skip;
static int bfe_bmu_enable = 1;
static int bfe_nic_eth_on_internal;
static int bfe_config_qman_arc_for_stub_mme;
static int bfe_skip_nic_phy_init;
/*
 * The debug_[rw]reg control on tracing LBW RW
 * The logic is as follow
 * if 0- disable RW LBW traces
 * if 1- do not disable RW LBW traces. traces will be active only
 *       if, in addition, the corresponding RW LBW trace point is
 *       enabled as well.
 * This is done to preserve the general events tracing logic while allowing
 * a user to "debug LBW RWs" only to specific sections of the code.
 * In which case the user should load LKD with  bfe_debug_[rw]reg = 0
 * and wrap the specific code with hdev->debug_[rw]reg = 1 and 0.
 */
static int bfe_debug_rreg = 1;
static int bfe_debug_wreg = 1;
static ulong bfe_sched_arc_mask = 0xFFFF;
static int bfe_enable_odp = 1;
static int bfe_use_8_bit_hops = 1;
static int bfe_cache_enable;
static int bfe_enable_intr_aggr;
static int bfe_halt_eng_upon_fw_events;
static ulong bfe_hmmu_supported_pages_mask;
static ulong bfe_hmmu_default_page_size;
static int bfe_priv_security_enable;
static uint bfe_mme_row_repair_l;
static uint bfe_mme_row_repair_h;
static int bfe_pci_rev_id;
static int bfe_ptw_bypass_enable = 1;
static uint bfe_rotator_binning;
static int bfe_hbm_compression_enable = 1;
static int bfe_nic_enable_h9_rx_drop_eco = 1;
static int bfe_enable_h9_cache_eta_eco = 1;
static int bfe_force_h9_single_die;
static int bfe_nic_enable_h9_qp_doorbells_eco = 1;
static int bfe_nic_enable_h9_cc_msg_drops_eco = 1;
static int bfe_nic_enable_h9_remote_pi_update_eco = 1;
static int bfe_nic_enable_h9_rxb_mem_deadlock_eco = 1;
static int bfe_nic_enable_h9_single_qp_perf_fix_eco = 1;
static int bfe_nic_enable_h9_sal_override_eco = 1;
static int bfe_nic_enable_h9_sack_deadlock_eco = 1;
static int bfe_nic_enable_h9_txe_buff_alloc_eco = 1;
static int bfe_heartbeat_reset_enable = 1;

/* special-case of parameter handling - polling */
static bool nic_poll_enable_param_was_set;

static int nic_poll_param_set(const char *val, const struct kernel_param *kp)
{
	int rc = param_set_int(val, kp);

	if (!rc)
		nic_poll_enable_param_was_set = true;

	return rc;
}

static const struct kernel_param_ops poll_cb_ops = {
	.set = nic_poll_param_set,
	.get = param_get_int,
};

/* module parameters */

module_param(timeout_locked, int, 0444);
MODULE_PARM_DESC(timeout_locked,
	"Device lockup timeout in seconds (0 = disabled, default 30s)");

module_param(reset_on_lockup, int, 0444);
MODULE_PARM_DESC(reset_on_lockup,
	"Do device reset on lockup (0 = no, 1 = yes, default yes)");

module_param(memory_scrub, int, 0444);
MODULE_PARM_DESC(memory_scrub,
	"Scrub device memory in various states (0 = no, 1 = yes, default no)");

module_param_cb(nic_poll_enable, &poll_cb_ops, &nic_poll_enable, 0444);
MODULE_PARM_DESC(nic_poll_enable,
	"Enable NIC polling rather than IRQ (0 = no, 1 = yes, default yes)");

module_param(boot_error_status_mask, ulong, 0444);
MODULE_PARM_DESC(boot_error_status_mask,
	"Mask of the error status during device CPU boot (If bitX is cleared then error X is masked. Default all 1's)");

/* flags not upstreamed */
module_param(low_freq, int, 0444);
MODULE_PARM_DESC(low_freq,
	"Enable low PLL frequency mode (0 = no, 1 = yes, default no)");

module_param(card_type, int, 0444);
MODULE_PARM_DESC(card_type,
	"Card type (0 = PCI, 1 = PMC, default PMC)");

module_param(nic_ports_mask, uint, 0444);
MODULE_PARM_DESC(nic_ports_mask,
	"NIC ports mask, 1 bit per NIC port (0 = none, default all ports enabled)");

module_param(nic_ports_ext_mask, uint, 0444);
MODULE_PARM_DESC(nic_ports_ext_mask,
	"NIC ports external mask, 1 bit per NIC port (0 = none, default all ports are external)");

module_param(nic_auto_neg_mask, uint, 0444);
MODULE_PARM_DESC(nic_auto_neg_mask,
	"NIC Autoneg mask, 1 bit per NIC port (0 = none, default enable Autoneg on all ports)");

module_param(ignore_fw_nic_info, uint, 0444);
MODULE_PARM_DESC(ignore_fw_nic_info,
	"Ignore NIC info from FW (0 = no, 1 = yes, default no)");

module_param(nic_lanes_per_port, uint, 0444);
MODULE_PARM_DESC(nic_lanes_per_port,
	"Number of lanes per NIC port (default 2)");

module_param(nic_qp_drain_time, uint, 0444);
MODULE_PARM_DESC(nic_qp_drain_time,
	"NIC QP drain time in seconds after QP invalidation (default 2 Sec)");

module_param(skip_iatu_for_unsecured_device, uint, 0444);
MODULE_PARM_DESC(skip_iatu_for_unsecured_device,
	"Skip the initialization of iATU for unsecured device and assume the F/W has done it (0 = no, 1 = yes, 2 = force no for Gaudi2, default yes for Gaudi2, no for all the rest)");

module_param(reset_upon_device_release, int, 0444);
MODULE_PARM_DESC(reset_upon_device_release,
	"Enable reset upon device release, relevant for GAUDI2 and later (0 = no, 1 = yes, default yes)");

module_param(gaudi2_setup_type, int, 0444);
MODULE_PARM_DESC(gaudi2_setup_type,
	"The type of setup according to which the gaudi2 PHY should be configured (0 - HLS2, 1 - HL225-S with external loopbacks, 2 - HL325-S with external loopbacks, default 0)");

module_param(enable_events_tracing, ulong, 0444);
MODULE_PARM_DESC(enable_events_tracing,
	"Bitmask for enable various events tracing indication (values in HL_TRACE_*_MASK definitions, default 0)");

module_param(ignore_eeprom_errors, int, 0444);
MODULE_PARM_DESC(ignore_eeprom_errors,
	"Ignore eeprom errors (0 - disabled, 1 - enabled, default 0)");

/* Bring-Up flags */
module_param(pldm, int, 0444);
MODULE_PARM_DESC(pldm,
	"Palladium (0 = no, 1 = yes, default no)");

module_param(ifh, int, 0444);
MODULE_PARM_DESC(ifh,
	"Infinitely Fast Hardware (0 = no, 1 = yes, default no)");

module_param(bringup_flags_enable, int, 0444);
MODULE_PARM_DESC(bringup_flags_enable,
	"Enable bring-up flags (0 = no, 1 = yes, default no)");

module_param(bfe_gaudi_huge_page_optimization, int, 0444);
MODULE_PARM_DESC(bfe_gaudi_huge_page_optimization,
	"GAUDI MMU huge page optimization enabled (0 = no, 1 = yes, default yes)");

module_param(bfe_mmu_enable, int, 0444);
MODULE_PARM_DESC(bfe_mmu_enable,
	"Device MMU enabled (0 = no, 1 = yes (all), 3 = PMMU only (N/A for GOYA/GAUDI), default yes)");

module_param(bfe_clock_gating, int, 0444);
MODULE_PARM_DESC(bfe_clock_gating,
	"Enable clock gating (0 = disabled, 1 = enabled, default enabled, N/A for GOYA/GAUDI, N/A when f/w is loaded)");

module_param(bfe_mme_mask, uint, 0444);
MODULE_PARM_DESC(bfe_mme_mask,
	"MME mask, 1 bit per MME instance (0 = none, default ALL MMEs enabled)");

module_param(bfe_tpc_mask, ulong, 0444);
MODULE_PARM_DESC(bfe_tpc_mask,
	"TPC mask, 1 bit per TPC instance (0 = none, default ALL TPCs enabled)");

module_param(bfe_decoder_mask, uint, 0444);
MODULE_PARM_DESC(bfe_decoder_mask,
	"Decoder mask, 1 bit per decoder instance (0 = none (default), 0x3FF = all decoders enabled)");

module_param(bfe_rotator_mask, uint, 0444);
MODULE_PARM_DESC(bfe_rotator_mask,
	"Rotator mask, 1 bit per rotator instance (0 = none, default 0x3 = all rotators enabled)");

module_param(bfe_pdma_ch_mask, uint, 0444);
MODULE_PARM_DESC(bfe_pdma_ch_mask,
	"PDMA channels mask, 1 bit for each of the x24 PDMA channels (0 = none, default 0xFFF = x12 channels enabled on 1st die). Gaudi3 only");

module_param(bfe_edma_mask, uint, 0444);
MODULE_PARM_DESC(bfe_edma_mask,
	"EDMA mask, 1 bit per EDMA instance (0 = none, default ALL EDMAs enabled). Relevant to Gaudi3 and later");

module_param(bfe_dram_enable, int, 0444);
MODULE_PARM_DESC(bfe_dram_enable,
	"DRAM enabled (0 = no, 1 = yes, default yes)");

module_param(bfe_reset_pcilink, int, 0444);
MODULE_PARM_DESC(bfe_reset_pcilink,
	"Reset PCIe link before init (0 = no, 1 = yes, default no)");

module_param(bfe_config_pll, int, 0444);
MODULE_PARM_DESC(bfe_config_pll,
	"Configure PLL (0 = no, 1 = yes, default no)");

module_param(bfe_fw_components, ulong, 0444);
MODULE_PARM_DESC(bfe_fw_components,
	"Bitmask for various firmwares indication (values in enum hl_fw_types, default FW_TYPE_ALL_TYPES)");

module_param(bfe_fw_communication_enable, int, 0444);
MODULE_PARM_DESC(bfe_fw_communication_enable,
	"Enable communication with the firmware (0 = no, 1 = yes, default yes)");

module_param(bfe_heartbeat, int, 0444);
MODULE_PARM_DESC(bfe_heartbeat,
	"Enable device CPU heartbeat check (0 = no, 1 = yes, default yes)");

module_param(bfe_axi_drain, int, 0444);
MODULE_PARM_DESC(bfe_axi_drain,
	"Enable/Skip AXI drain (values in enum hl_axi_drain_mode, default AXI_DRAIN_SKIP)");

module_param(bfe_security_enable, int, 0444);
MODULE_PARM_DESC(bfe_security_enable,
	"Enable security (0 = no, 1 = yes, default yes)");

module_param(bfe_sram_scrambler_enable, int, 0444);
MODULE_PARM_DESC(bfe_sram_scrambler_enable,
	"Enable SRAM scrambler (0 = no, 1 = yes, default yes)");

module_param(bfe_dram_scrambler_enable, int, 0444);
MODULE_PARM_DESC(bfe_dram_scrambler_enable,
	"Enable DRAM scrambler (0 = no, 1 = yes, default yes)");

module_param(bfe_hbm_ecc_enable, int, 0444);
MODULE_PARM_DESC(bfe_hbm_ecc_enable,
	"Enable HBM ECC (0 = no, 1 = yes, default yes)");

module_param(bfe_compatibility_mode, int, 0444);
MODULE_PARM_DESC(bfe_compatibility_mode,
	"Enable compatibility mode (0 = no, 1 = yes, default no)");

module_param(bfe_hard_reset_on_fw_events, int, 0444);
MODULE_PARM_DESC(bfe_hard_reset_on_fw_events,
	"Perform hard-reset on relevant F/W events (0 = no, 1 = yes, default yes)");

module_param(bfe_bmc_enable, int, 0444);
MODULE_PARM_DESC(bfe_bmc_enable,
	"BMC enable (0 = no, 1 = yes, default yes)");

module_param(bfe_nic_load_fw, int, 0444);
MODULE_PARM_DESC(bfe_nic_load_fw,
	"Load NIC PHY F/W (0 = no, 1 = yes, default no)");

module_param(bfe_rl_enable, int, 0444);
MODULE_PARM_DESC(bfe_rl_enable,
	"Enable rate limiters in compatibility mode (0 = no, 1 = yes, default yes)");

module_param(bfe_sram_binning, int, 0444);
MODULE_PARM_DESC(bfe_sram_binning,
	"Categorize SRAM functionality (0 = fully functional, 1 = lower-half is not functional, 2 = upper-half is not functional, default 0)");

module_param(bfe_tpc_binning, ulong, 0444);
MODULE_PARM_DESC(bfe_tpc_binning,
	"TPC binning mask, 1 bit per TPC instance (0 = functional, 1 = binned)");

module_param(bfe_dram_binning, ulong, 0444);
MODULE_PARM_DESC(bfe_dram_binning,
	"HBM binning mask, 1 bit per HBM instance (0 = functional, 1 = binned)");

module_param(bfe_decoder_binning, uint, 0444);
MODULE_PARM_DESC(bfe_decoder_binning,
	"Decoder binning mask, 1 bit per decoder instance (0 = functional, 1 = binned), maximum 1 per dcore");

module_param(bfe_edma_binning, uint, 0444);
MODULE_PARM_DESC(bfe_edma_binning,
	"EDMA binning mask, 1 bit per DMA instance (0 = functional, 1 = binned), maximum 1");

module_param(bfe_reset_on_preboot_fail, int, 0444);
MODULE_PARM_DESC(bfe_reset_on_preboot_fail,
	"Reset on preboot version read fail (0 = no, 1 = yes, default yes)");

module_param(bfe_force_driver_reset, int, 0444);
MODULE_PARM_DESC(bfe_force_driver_reset,
	"Force the driver to do soft/hard-reset even if F/W should do it (values in enum hl_force_driver_reset, default FORCE_DRIVER_RESET_NONE)");

module_param(bfe_force_driver_clock_gating, int, 0444);
MODULE_PARM_DESC(bfe_force_driver_clock_gating,
	"Force the driver to configure even if F/W should do it (0 = no, 1 = yes, default no)");

module_param(bfe_pll_async_if_enable, int, 0444);
MODULE_PARM_DESC(bfe_pll_async_if_enable,
	"Set PLL through async IF (0 = no, 1 = yes, default no)");

module_param(bfe_bootfit_relocatable, int, 0444);
MODULE_PARM_DESC(bfe_bootfit_relocatable,
	"If Boot Fit is relocatable it will be copied out of SRAM after booting (0 = no, 1 = yes, default no)");

module_param(bfe_reset_if_device_not_idle, int, 0444);
MODULE_PARM_DESC(bfe_reset_if_device_not_idle,
	"Perform reset if device is not idle upon user FD close (0 = no, 1 = yes, default yes)");

module_param(bfe_hbm_pll_freq, int, 0444);
MODULE_PARM_DESC(bfe_hbm_pll_freq,
	"Recommended frequency for the HBM in MHz (possible values: 1200, 1600, 1800, Default=1600");

module_param(bfe_half_nominal_pll_mode, int, 0444);
MODULE_PARM_DESC(bfe_half_nominal_pll_mode,
	"Configures the MSS pll in half of nominal mode (0 = no, 1 = yes, default no)");

module_param(bfe_nic_eth_on_internal, int, 0444);
MODULE_PARM_DESC(bfe_nic_eth_on_internal,
	"Enable Ethernet capabilities on internal NIC ports (0 = no, 1 = yes, default no)");

module_param(bfe_scrub_arc_dccm, int, 0444);
MODULE_PARM_DESC(bfe_scrub_arc_dccm,
	"scrub ARC dccm upon soft/hard reset (0 = no, 1 = yes, default no)");

module_param(bfe_skip_cluster_config, int, 0444);
MODULE_PARM_DESC(bfe_skip_cluster_config,
	"skip implicit binning/isolation of faulty HBM cluster's components (0 = no, 1 = yes, default no)");

module_param(bfe_config_qman_arc_for_stub_mme, int, 0444);
MODULE_PARM_DESC(bfe_config_qman_arc_for_stub_mme,
	"Configure ARC and QMAN for stubbed MME (0 = no, 1 = yes, default no)");

module_param(bfe_fw_cfg_skip, int, 0444);
MODULE_PARM_DESC(bfe_fw_cfg_skip,
	"instruct FW to skip all configurations performed by uboot (0 = no, 1 = yes, default no)");

module_param(bfe_bmu_enable, int, 0444);
MODULE_PARM_DESC(bfe_bmu_enable,
	"use BMU (Bar Mapping Unit), relevant for Gaudi3 or later (0 = no, 1 = yes, default yes)");

module_param(bfe_skip_nic_phy_init, uint, 0444);
MODULE_PARM_DESC(bfe_skip_nic_phy_init,
	"Avoid writing/reading PHY registers, relevant for Gaudi2 or later (0 = no, 1 = yes, default no)");

module_param(bfe_debug_rreg, int, 0444);
MODULE_PARM_DESC(bfe_debug_rreg, "Debug all reads from registers (0 = no, 1 = yes, default no)");

module_param(bfe_debug_wreg, int, 0444);
MODULE_PARM_DESC(bfe_debug_wreg, "Debug all writes to registers (0 = no, 1 = yes, default no)");

module_param(bfe_sched_arc_mask, ulong, 0444);
MODULE_PARM_DESC(bfe_sched_arc_mask,
	"Scheduler arcs mask relevant for Gaudi2 or later, 1 bit per scheduler arc (0 = none, default: All Enabled)");

module_param(bfe_enable_odp, int, 0444);
MODULE_PARM_DESC(bfe_enable_odp,
	"Flag to enable or disable ODP hardware support (0 - ODP disabled, 1 - ODP enabled, default 1)");

module_param(bfe_use_8_bit_hops, int, 0444);
MODULE_PARM_DESC(bfe_use_8_bit_hops,
	"Flag to enable or disable 8 bit hops for DRAM (0 - disabled, 1 - enabled, default 1)");

module_param(bfe_cache_enable, int, 0444);
MODULE_PARM_DESC(bfe_cache_enable,
	"Enable cache mode instead of sram, relevant for Gaudi3 or later (0 = no, 1 = yes, default no)");

module_param(bfe_enable_intr_aggr, int, 0444);
MODULE_PARM_DESC(bfe_enable_intr_aggr,
	"Enable interrupt aggregators messages, relevant for Gaudi3 or later (0 = no, 1 = yes, default no)");

module_param(bfe_halt_eng_upon_fw_events, int, 0444);
MODULE_PARM_DESC(bfe_halt_eng_upon_fw_events,
	"Perform halt engines upon FW events (0 = no, 1 = yes, default no), supported in gaudi2");

module_param(bfe_hmmu_supported_pages_mask, ulong, 0444);
MODULE_PARM_DESC(bfe_hmmu_supported_pages_mask,
	"Bitmask for supported HMMU pages (bit set- page supported, bit clear- page not supported. all 0s- use default page sizes, default 0)");

module_param(bfe_hmmu_default_page_size, ulong, 0444);
MODULE_PARM_DESC(bfe_hmmu_default_page_size,
	"Set default HMMU page size (must be value supported by HMMU, 0 mean use defined default, default 0)");

module_param(bfe_priv_security_enable, int, 0444);
MODULE_PARM_DESC(bfe_priv_security_enable,
	"Enable privileged PB security & assert upon invalid access. Relevant only for GaudiX devices (0 = no, 1 = yes, default no)");

module_param(bfe_mme_row_repair_l, uint, 0444);
MODULE_PARM_DESC(bfe_mme_row_repair_l, "MME row repair mask lower 32 bits, default 0");

module_param(bfe_mme_row_repair_h, uint, 0444);
MODULE_PARM_DESC(bfe_mme_row_repair_h, "MME row repair mask higher 32 bits, default 0");

module_param(bfe_pci_rev_id, int, 0444);
MODULE_PARM_DESC(bfe_pci_rev_id, "Override PCI revision ID, (0 means do not override, default 0)");

module_param(bfe_ptw_bypass_enable, int, 0444);
MODULE_PARM_DESC(bfe_ptw_bypass_enable,
	"Flag to enable or disable PTW bypass support(0 - disabled, 1 - enabled, default 1)");

module_param(bfe_rotator_binning, uint, 0444);
MODULE_PARM_DESC(bfe_rotator_binning,
	"Rotator binning mask, 1 bit per rotator instance(relevant for Gaudi3 and above), default 0");

module_param(bfe_hbm_compression_enable, int, 0444);
MODULE_PARM_DESC(bfe_hbm_compression_enable,
	"Enable HBM compression, relevant for Gaudi3 or later (0 = no, 1 = yes, default yes)");

module_param(bfe_nic_enable_h9_rx_drop_eco, int, 0444);
MODULE_PARM_DESC(bfe_nic_enable_h9_rx_drop_eco,
	"Enable H9-5384 ECO, which avoids packet drops in RXB (0 - disabled, 1 - enabled, default 1)");

module_param(bfe_enable_h9_cache_eta_eco, int, 0444);
MODULE_PARM_DESC(bfe_enable_h9_cache_eta_eco,
	"Enable H9 Cache ETA ECO (0 - disabled, 1 - enabled, default 1)");

module_param(bfe_force_h9_single_die, int, 0444);
MODULE_PARM_DESC(bfe_force_h9_single_die,
	"Force H9 device to work in single-die mode (0 - disabled, 1 - enabled, default 0)");

module_param(bfe_nic_enable_h9_qp_doorbells_eco, int, 0444);
MODULE_PARM_DESC(bfe_nic_enable_h9_qp_doorbells_eco,
	"Enable H9-4960 ECO, fixes unexpectedly doorbells for QPs with no work in QPC (0 - disabled, 1 - enabled, default 1)");

module_param(bfe_nic_enable_h9_cc_msg_drops_eco, int, 0444);
MODULE_PARM_DESC(bfe_nic_enable_h9_cc_msg_drops_eco,
	"Enable H9-5456 ECO, fixes message drops in CC mode when SACK enabled (0 - disabled, 1 - enabled, default 1)");

module_param(bfe_nic_enable_h9_remote_pi_update_eco, int, 0444);
MODULE_PARM_DESC(bfe_nic_enable_h9_remote_pi_update_eco,
	"Enable H9-5490 ECO, fixes remote PI wrong update on wraparound (0 - disabled, 1 - enabled, default 1)");

module_param(bfe_nic_enable_h9_rxb_mem_deadlock_eco, int, 0444);
MODULE_PARM_DESC(bfe_nic_enable_h9_rxb_mem_read_deadlock_eco,
	"Enable H9-5454 ECO, fixes RXB memory read deadlock (0 - disabled, 1 - enabled, default 1)");

module_param(bfe_nic_enable_h9_single_qp_perf_fix_eco, int, 0444);
MODULE_PARM_DESC(bfe_nic_enable_h9_single_qp_perf_fix_eco,
	"Enable H9-5216 ECO, fixes single QP performance (0 - disabled, 1 - enabled, default 1)");

module_param(bfe_nic_enable_h9_sal_override_eco, int, 0444);
MODULE_PARM_DESC(bfe_nic_enable_h9_sal_override_eco,
	"Enable H9-5499 ECO, fixes SAL override issue (0 - disabled, 1 - enabled, default 1)");

module_param(bfe_nic_enable_h9_sack_deadlock_eco, int, 0444);
MODULE_PARM_DESC(bfe_nic_enable_h9_sack_deadlock_eco,
	"Enable H9-5457 ECO, fixes SACK deadlock (0 - disabled, 1 - enabled, default 1)");

module_param(bfe_nic_enable_h9_txe_buff_alloc_eco, int, 0444);
MODULE_PARM_DESC(bfe_nic_enable_h9_txe_buff_alloc_eco,
	"Enable H9-5471 ECO, fixes TXE buff allocation issue (0 - disabled, 1 - enabled, default 1)");

module_param(bfe_heartbeat_reset_enable, int, 0444);
MODULE_PARM_DESC(bfe_heartbeat_reset_enable,
	"Enable hard-reset after heartbeat failure (0 - disabled, 1 - enabled, default 1)");

#define PCI_VENDOR_ID_HABANALABS	0x1da3

#define PCI_IDS_GOYA			0x0001

#define PCI_IDS_GRECO			0x0020	/* Deprecated, to be removed */
#define PCI_IDS_GRECO_ADDIN		0x0030

#define PCI_IDS_GAUDI			0x1000
#define PCI_IDS_GAUDI_SEC		0x1010

#define PCI_IDS_GAUDI_HL2000M		0x1001
#define PCI_IDS_GAUDI_HL2000M_SEC	0x1011

#define PCI_IDS_GAUDI2			0x1020

#define PCI_IDS_GAUDI3			0x1060
#define PCI_IDS_GAUDI3_SINGLE_DIE	0x1062

#define PCI_IDS_GAUDI2_FPGA		0xFF08
#define PCI_IDS_GAUDI3_FPGA		0xFF0D

static const struct pci_device_id ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GOYA), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GRECO), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GRECO_ADDIN), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GAUDI), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GAUDI_SEC), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GAUDI_HL2000M), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GAUDI_HL2000M_SEC), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GAUDI2), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GAUDI3), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GAUDI3_SINGLE_DIE), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GAUDI2_FPGA), },
	{ PCI_DEVICE(PCI_VENDOR_ID_HABANALABS, PCI_IDS_GAUDI3_FPGA), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ids);

void set_pci_revision_id(struct hl_device *hdev, enum hl_asic_type asic_type)
{
	struct pci_dev *pdev = hdev->pdev;

	if (hdev->pci_rev_id_override) {
		hdev->pci_revision_id = hdev->pci_rev_id_override;
	} else if (pdev) {
		hdev->pci_revision_id = pdev->revision;
	} else {
		switch (asic_type) {
		case ASIC_GAUDI2B_SIM:
		case ASIC_GAUDI2B_SIM_ARC:
			hdev->pci_revision_id = REV_ID_B;
			break;
		default:
			hdev->pci_revision_id = REV_ID_A;
		}
	}
}

/*
 * get_asic_type - translate device id to asic type
 *
 * @hdev: pointer to habanalabs device structure.
 *
 * Translate device id and revision id to asic type.
 * In case of unidentified device, return -1
 */
static enum hl_asic_type get_asic_type(struct hl_device *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	enum hl_asic_type asic_type = ASIC_INVALID;

	switch (pdev->device) {
	case PCI_IDS_GOYA:
		asic_type = ASIC_GOYA;
		break;
	case PCI_IDS_GRECO:
	case PCI_IDS_GRECO_ADDIN:
		asic_type = ASIC_GRECO;
		break;
	case PCI_IDS_GAUDI:
		asic_type = ASIC_GAUDI;
		break;
	case PCI_IDS_GAUDI_SEC:
		asic_type = ASIC_GAUDI_SEC;
		break;
	case PCI_IDS_GAUDI_HL2000M:
		asic_type = ASIC_GAUDI_HL2000M;
		break;
	case PCI_IDS_GAUDI_HL2000M_SEC:
		asic_type = ASIC_GAUDI_HL2000M_SEC;
		break;
	case PCI_IDS_GAUDI2:
		switch (hdev->pci_revision_id) {
		case REV_ID_A:
			asic_type = ASIC_GAUDI2;
			break;
		case REV_ID_B:
			asic_type = ASIC_GAUDI2B;
			break;
		default:
			break;
		}
		break;
	case PCI_IDS_GAUDI3:
		if (hdev->force_h9_single_die)
			asic_type = ASIC_GAUDI3_SINGLE_DIE;
		else
			asic_type = ASIC_GAUDI3;
		break;
	case PCI_IDS_GAUDI3_SINGLE_DIE:
		asic_type = ASIC_GAUDI3_SINGLE_DIE;
		break;
	case PCI_IDS_GAUDI2_FPGA:
		asic_type = ASIC_GAUDI2_FPGA;
		break;
	case PCI_IDS_GAUDI3_FPGA:
		asic_type = ASIC_GAUDI3_FPGA;
		break;
	default:
		break;
	}

	return asic_type;
}

#define CHECK_INT_PORTS_STATUS_EXECUTION_DELAY_SEC	40
#define CHECK_INT_PORTS_STATUS_INTERVAL_SEC		10
#define WAIT_FOR_INT_PORTS_STATUS_SEC			40
#define MAX_NUM_OF_TRIALS				3

enum int_ports_status_state {
	NOT_RUNNING = 0x0,
	NOT_ALL_OPERATIONAL = 0x1,
	ALL_OPERATIONAL,
	ALL_INT_PORTS_UP,
};

struct int_ports_status {
	struct workqueue_struct		*wq;
	struct delayed_work		dwork;
	enum int_ports_status_state	state;
	u8				num_of_trials;
	u8				timeout;
};

static struct int_ports_status int_ports_sts;

static bool check_all_int_ports_up(struct hl_device *hdev)
{
	struct hl_nic_properties *nic_props;
	struct hl_nic_port *nic_port;
	struct hl_nic *nic;
	int i;

	nic_props = &hdev->asic_prop.nic_props;
	nic = &hdev->nic;

	for (i = 0 ; i < nic_props->max_num_of_ports ; i++) {
		if (!(hdev->nic_ports_mask & BIT(i)) || (nic->eth_ports_mask & BIT(i)))
			continue;

		nic_port = &nic->nic_ports[i];
		if (!nic_port->pcs_link)
			return false;
	}

	return true;
}

/* This function is called under hl_devs_idr_lock, hence shouldn't take it again */
static void disable_all_devices(void)
{
	struct hl_device *hdev;
	int id;

	idr_for_each_entry(&hl_devs_idr, hdev, id) {
		/* continue if it's not main device */
		if (hdev->id != id)
			continue;

		hdev->disabled = true;
	}
}

/* This function is called under hl_devs_idr_lock, hence shouldn't take it again */
static void reset_all_devices(void)
{
	struct hl_device *hdev;
	int id;

	idr_for_each_entry(&hl_devs_idr, hdev, id) {
		/* continue if it's not main device */
		if (hdev->id != id)
			continue;

		hl_device_reset(hdev, HL_DRV_RESET_HARD);
	}
}

static bool is_device_needs_int_ports_check(struct hl_device *hdev)
{
	return false;
}

static void check_int_ports_status_work(struct work_struct *unused)
{
	enum hl_device_status status;
	struct hl_device *hdev;
	int id;

	mutex_lock(&hl_devs_idr_lock);

	/* Check once that this operation is relevant for this devices - if all relevant mark them
	 * as not operational.
	 */
	if (int_ports_sts.state < NOT_ALL_OPERATIONAL) {
		idr_for_each_entry(&hl_devs_idr, hdev, id) {
			if (!is_device_needs_int_ports_check(hdev)) {
				mutex_unlock(&hl_devs_idr_lock);
				return;
			}
		}

		/* In case number of devices (main + control) is different than 16, i.e. number of
		 * cards is different than 8, it means that we are not running on HLS2, hence this
		 * mechanism is not relevant and we can return.
		 */
		if (id != 16) {
			mutex_unlock(&hl_devs_idr_lock);
			return;
		}

		int_ports_sts.state = NOT_ALL_OPERATIONAL;
	}

	int_ports_sts.timeout = CHECK_INT_PORTS_STATUS_INTERVAL_SEC;

	idr_for_each_entry(&hl_devs_idr, hdev, id) {
		/* continue if it's not main device */
		if (hdev->id != id)
			continue;

		hl_device_operational(hdev, &status);
		if (status != HL_DEVICE_STATUS_OPERATIONAL && !hdev->reset_info.in_compute_reset) {
			dev_dbg(hdev->dev,
				"Internal ports status check: device is not operational, will try again in %d seconds\n",
				CHECK_INT_PORTS_STATUS_INTERVAL_SEC);

			int_ports_sts.state = NOT_ALL_OPERATIONAL;
			goto again;
		}
	}

	if (int_ports_sts.state == NOT_ALL_OPERATIONAL) {
		pr_debug("Internal ports status check: all devices are operational, waiting %d seconds before checking links\n",
			WAIT_FOR_INT_PORTS_STATUS_SEC);
		int_ports_sts.state = ALL_OPERATIONAL;
		int_ports_sts.timeout = WAIT_FOR_INT_PORTS_STATUS_SEC;

		goto again;
	}

	if (int_ports_sts.state == ALL_INT_PORTS_UP)
		goto again;

	idr_for_each_entry(&hl_devs_idr, hdev, id) {
		/* continue if it's not main device */
		if (hdev->id != id)
			continue;

		if (!check_all_int_ports_up(hdev)) {
			int_ports_sts.num_of_trials++;
			if (int_ports_sts.num_of_trials > MAX_NUM_OF_TRIALS) {
				dev_err(hdev->dev,
					"Internal ports status check: reached the maximum number of trials (%d),  devices are not usable!\n",
					MAX_NUM_OF_TRIALS);
				disable_all_devices();
				int_ports_sts.state = NOT_RUNNING;
				mutex_unlock(&hl_devs_idr_lock);
				return;
			}

			dev_err(hdev->dev, "Internal ports status check: not all ports are UP, reset all devices\n");
			reset_all_devices();
			goto again;
		}
	}

	int_ports_sts.state = ALL_INT_PORTS_UP;
	int_ports_sts.num_of_trials = 0;
	pr_info("Internal ports status check: all ports are UP\n");

again:
	mutex_unlock(&hl_devs_idr_lock);

	queue_delayed_work(int_ports_sts.wq, &int_ports_sts.dwork,
				msecs_to_jiffies(int_ports_sts.timeout * 1000));
}

bool hl_is_internal_ports_status_work_running(void)
{
	return (int_ports_sts.state == NOT_ALL_OPERATIONAL ||
			int_ports_sts.state == ALL_OPERATIONAL);
}

static bool is_asic_secured(enum hl_asic_type asic_type)
{
	switch (asic_type) {
	case ASIC_GAUDI_SEC:
	case ASIC_GAUDI_HL2000M_SEC:
		return true;
	default:
		return false;
	}
}

static bool is_cpu_queue_enabled(struct hl_device *hdev)
{
	bool enabled;

	switch (hdev->asic_type) {
	case ASIC_GAUDI3:
	case ASIC_GAUDI3_SINGLE_DIE:
	case ASIC_GAUDI3_FPGA:
	case ASIC_GAUDI3_SIM:
	case ASIC_GAUDI3_SIM_ARC:
	case ASIC_GAUDI3_SIM_SINGLE_DIE_ARC:
	case ASIC_GAUDI2:
	case ASIC_GAUDI2B:
	case ASIC_GAUDI2_SIM:
	case ASIC_GAUDI2B_SIM:
	case ASIC_GAUDI2_SIM_ARC:
	case ASIC_GAUDI2B_SIM_ARC:
	case ASIC_GAUDI2_FPGA:
		enabled = !!(hdev->fw_components & FW_TYPE_BOOT_CPU);
		break;
	default:
		enabled = !!(hdev->fw_components & FW_TYPE_LINUX);
		break;
	}

	return (enabled && !!(hdev->fw_communication_enable));
}

/*
 * hl_device_open - open function for habanalabs device
 *
 * @inode: pointer to inode structure
 * @filp: pointer to file structure
 *
 * Called when process opens an habanalabs device.
 */
int hl_device_open(struct inode *inode, struct file *filp)
{
	enum hl_device_status status;
	struct hl_device *hdev;
	struct hl_fpriv *hpriv;
	int rc;

	mutex_lock(&hl_devs_idr_lock);
	hdev = idr_find(&hl_devs_idr, iminor(inode));
	mutex_unlock(&hl_devs_idr_lock);

	if (!hdev) {
		pr_err("Couldn't find device %d:%d\n",
			imajor(inode), iminor(inode));
		return -ENXIO;
	}

	hpriv = kzalloc(sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;

	hpriv->hdev = hdev;
	filp->private_data = hpriv;
	hpriv->filp = filp;

	mutex_init(&hpriv->notifier_event.lock);
	mutex_init(&hpriv->restore_phase_mutex);
	mutex_init(&hpriv->ctx_lock);
	kref_init(&hpriv->refcount);
	nonseekable_open(inode, filp);

	hl_ctx_mgr_init(&hpriv->ctx_mgr);
	hl_mem_mgr_init(hpriv->hdev->dev, &hpriv->mem_mgr);

	hpriv->taskpid = get_task_pid(current, PIDTYPE_PID);

	mutex_lock(&hdev->fpriv_list_lock);

	if (!hl_device_operational(hdev, &status)) {
		dev_dbg_ratelimited(hdev->dev,
			"Can't open %s because it is %s\n",
			dev_name(hdev->dev), hdev->status[status]);

		if (status == HL_DEVICE_STATUS_IN_RESET ||
					status == HL_DEVICE_STATUS_IN_RESET_AFTER_DEVICE_RELEASE)
			rc = -EAGAIN;
		else
			rc = -EPERM;

		goto out_err;
	}

	if (hdev->is_in_dram_scrub) {
		dev_dbg_ratelimited(hdev->dev,
			"Can't open %s during dram scrub\n",
			dev_name(hdev->dev));
		rc = -EAGAIN;
		goto out_err;
	}

	if (hdev->compute_ctx_in_release) {
		dev_dbg_ratelimited(hdev->dev,
			"Can't open %s because another user is still releasing it\n",
			dev_name(hdev->dev));
		rc = -EAGAIN;
		goto out_err;
	}

	if (hdev->is_compute_ctx_active) {
		dev_dbg_ratelimited(hdev->dev,
			"Can't open %s because another user is working on it\n",
			dev_name(hdev->dev));
		rc = -EBUSY;
		goto out_err;
	}

	rc = hl_ctx_create(hdev, hpriv);
	if (rc) {
		dev_err(hdev->dev, "Failed to create context %d\n", rc);
		goto out_err;
	}

	list_add(&hpriv->dev_node, &hdev->fpriv_list);
	mutex_unlock(&hdev->fpriv_list_lock);

	hdev->asic_funcs->send_device_activity(hdev, true);

	hl_debugfs_add_file(hpriv);

	memset(&hdev->captured_err_info, 0, sizeof(hdev->captured_err_info));
	atomic_set(&hdev->captured_err_info.cs_timeout.write_enable, 1);
	hdev->captured_err_info.undef_opcode.write_enable = true;

	hdev->open_counter++;
	hdev->last_successful_open_jif = jiffies;
	hdev->last_successful_open_ktime = ktime_get();

	return 0;

out_err:
	mutex_unlock(&hdev->fpriv_list_lock);
	hl_mem_mgr_fini(&hpriv->mem_mgr);
	hl_mem_mgr_idr_destroy(&hpriv->mem_mgr);
	hl_ctx_mgr_fini(hpriv->hdev, &hpriv->ctx_mgr);
	filp->private_data = NULL;
	mutex_destroy(&hpriv->ctx_lock);
	mutex_destroy(&hpriv->restore_phase_mutex);
	mutex_destroy(&hpriv->notifier_event.lock);
	put_pid(hpriv->taskpid);

	kfree(hpriv);

	return rc;
}

int hl_device_open_ctrl(struct inode *inode, struct file *filp)
{
	struct hl_device *hdev;
	struct hl_fpriv *hpriv;
	int rc;

	mutex_lock(&hl_devs_idr_lock);
	hdev = idr_find(&hl_devs_idr, iminor(inode));
	mutex_unlock(&hl_devs_idr_lock);

	if (!hdev) {
		pr_err("Couldn't find device %d:%d\n",
			imajor(inode), iminor(inode));
		return -ENXIO;
	}

	hpriv = kzalloc(sizeof(*hpriv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;

	/* Prevent other routines from reading partial hpriv data by
	 * initializing hpriv fields before inserting it to the list
	 */
	hpriv->hdev = hdev;
	filp->private_data = hpriv;
	hpriv->filp = filp;

	mutex_init(&hpriv->notifier_event.lock);
	nonseekable_open(inode, filp);

	hpriv->taskpid = get_task_pid(current, PIDTYPE_PID);

	mutex_lock(&hdev->fpriv_ctrl_list_lock);

	if (!hl_ctrl_device_operational(hdev, NULL)) {
		dev_dbg_ratelimited(hdev->dev_ctrl,
			"Can't open %s because it is disabled\n",
			dev_name(hdev->dev_ctrl));
		rc = -EPERM;
		goto out_err;
	}

	list_add(&hpriv->dev_node, &hdev->fpriv_ctrl_list);
	mutex_unlock(&hdev->fpriv_ctrl_list_lock);

	return 0;

out_err:
	mutex_unlock(&hdev->fpriv_ctrl_list_lock);
	filp->private_data = NULL;
	put_pid(hpriv->taskpid);

	kfree(hpriv);

	return rc;
}

static u32 get_dev_nic_ports_mask(enum hl_asic_type asic_type)
{
	u32 mask;

	switch (asic_type) {
	case ASIC_GAUDI3:
	case ASIC_GAUDI3_SIM:
	case ASIC_GAUDI3_SIM_ARC:
		mask = (nic_lanes_per_port == PORT_LANES_4) ? 0xFFF : 0xFFFFFF;
		break;
	case ASIC_GAUDI3_SINGLE_DIE:
	case ASIC_GAUDI3_SIM_SINGLE_DIE:
	case ASIC_GAUDI3_SIM_SINGLE_DIE_ARC:
		mask = (nic_lanes_per_port == PORT_LANES_4) ? 0x3F : 0xFFF;
		break;
	case ASIC_GAUDI2_SIM:
	case ASIC_GAUDI2_SIM_ARC:
	case ASIC_GAUDI2:
		/* 24 ports are supported */
		mask = 0xFFFFFF;
		break;
	case ASIC_GAUDI2B_SIM:
	case ASIC_GAUDI2B_SIM_ARC:
	case ASIC_GAUDI2B:
		/* 24 ports are supported */
		mask = 0xFFFFFF;
		break;

	case ASIC_GAUDI:
	case ASIC_GAUDI_SEC:
	case ASIC_GAUDI_SIM:
	case ASIC_GAUDI_HL2000M:
	case ASIC_GAUDI_HL2000M_SEC:
	case ASIC_GAUDI_HL2000M_SIM:
		/* 10 ports are supported */
		mask = 0x3FF;
		break;
	default:
		mask = 0;
	}

	return mask;
}

static void set_driver_behavior_per_device(struct hl_device *hdev)
{
	if (hdev->bringup_flags_enable)
		return;

	switch (hdev->asic_type) {
	case ASIC_GRECO:
		hdev->dram_enable = 1;
		hdev->fw_components = FW_TYPE_ALL_TYPES;
		hdev->mmu_enable = MMU_EN_ALL;
		hdev->security_enable = 1;
		hdev->tpc_mask = 0x3FF;
		hdev->mme_mask = 0x3;
		hdev->pdma_ch_mask = 0x0;
		hdev->hard_reset_on_fw_events = 1;
		hdev->decoder_mask = 0x3FF;
		hdev->dram_binning = 0x0;
		hdev->edma_binning = 0x0;
		hdev->tpc_binning = 0x0;
		hdev->decoder_binning = 0x0;
		hdev->scrub_arc_dccm = 0;
		hdev->axi_drain = AXI_DRAIN_SKIP;
		hdev->fw_communication_enable = 1;
		hdev->sched_arc_mask = 0;
		hdev->rotator_mask = 0x3;
		hdev->use_8_bit_hops = 1;
		hdev->priv_security_enable = 0;
		hdev->cache_enable = 0;
		hdev->rotator_binning = 0;
		hdev->hbm_compression_enable = 0;
		hdev->heartbeat_reset_enable = 1;
		break;

	case ASIC_GAUDI2_SIM:
	case ASIC_GAUDI2B_SIM:
		hdev->dram_enable = 1;
		hdev->fw_components = 0;
		hdev->mmu_enable = MMU_EN_ALL;
		hdev->security_enable = 1;
		hdev->tpc_mask = 0x1FFFFFF;
		hdev->mme_mask = 0xF;
		hdev->pdma_ch_mask = 0x0;
		hdev->edma_mask = 0x0;
		hdev->hard_reset_on_fw_events = 1;
		hdev->decoder_mask = 0x3FF;
		hdev->dram_binning = 0x0;
		hdev->edma_binning = 0x0;
		hdev->tpc_binning = 0x1000000;
		hdev->decoder_binning = 0x200;
		hdev->scrub_arc_dccm = 0;
		hdev->axi_drain = AXI_DRAIN_SKIP;
		hdev->fw_communication_enable = 1;
		hdev->sched_arc_mask = 0x3F;
		hdev->rotator_mask = 0x3;
		hdev->use_8_bit_hops = 0;
		hdev->priv_security_enable = 1;
		hdev->cache_enable = 0;
		hdev->rotator_binning = 0;
		hdev->hbm_compression_enable = 0;
		hdev->heartbeat_reset_enable = 1;
		break;

	case ASIC_GAUDI2_SIM_ARC:
	case ASIC_GAUDI2B_SIM_ARC:
		hdev->dram_enable = 1;
		hdev->fw_components = 0;
		hdev->mmu_enable = MMU_EN_ALL;
		hdev->security_enable = 1;
		hdev->tpc_mask = 0x1FFFFFF;
		hdev->mme_mask = 0xF;
		hdev->pdma_ch_mask = 0x0;
		hdev->edma_mask = 0x0;
		hdev->hard_reset_on_fw_events = 1;
		hdev->decoder_mask = 0x3FF;
		hdev->dram_binning = 0x0;
		hdev->edma_binning = 0x0;
		hdev->tpc_binning = 0x1000000;
		hdev->decoder_binning = 0x200;
		hdev->scrub_arc_dccm = 1;
		hdev->axi_drain = AXI_DRAIN_SKIP;
		hdev->fw_communication_enable = 1;
		hdev->sched_arc_mask = 0x3F;
		hdev->rotator_mask = 0x3;
		hdev->use_8_bit_hops = 0;
		hdev->priv_security_enable = 1;
		hdev->cache_enable = 0;
		hdev->rotator_binning = 0;
		hdev->hbm_compression_enable = 0;
		hdev->heartbeat_reset_enable = 1;
		break;

	case ASIC_GAUDI2:
	case ASIC_GAUDI2B:
		hdev->dram_enable = 1;
		hdev->fw_components = FW_TYPE_ALL_TYPES;
		hdev->mmu_enable = MMU_EN_ALL;
		hdev->security_enable = 1;
		hdev->tpc_mask = 0x1FFFFFF;
		hdev->mme_mask = 0xF;
		hdev->pdma_ch_mask = 0x0;
		hdev->edma_mask = 0x0;
		hdev->hard_reset_on_fw_events = 1;
		hdev->decoder_mask = 0x3FF;
		hdev->dram_binning = 0x0;
		hdev->edma_binning = 0x0;
		hdev->tpc_binning = 0x1000000;
		hdev->decoder_binning = 0x200;
		hdev->scrub_arc_dccm = 1;
		hdev->axi_drain = AXI_DRAIN_SKIP;
		hdev->fw_communication_enable = 1;
		hdev->sched_arc_mask = 0x3F;
		hdev->rotator_mask = 0x3;
		hdev->use_8_bit_hops = 0;
		hdev->priv_security_enable = 0;
		hdev->cache_enable = 0;
		hdev->rotator_binning = 0;
		hdev->hbm_compression_enable = 0;
		hdev->heartbeat_reset_enable = 0;
		break;

	case ASIC_GAUDI2_FPGA:
		hdev->dram_enable = 1;
		hdev->fw_components = FW_TYPE_ALL_TYPES;
		hdev->mmu_enable = MMU_EN_NONE;
		hdev->security_enable = 0;
		hdev->tpc_mask = 0;
		hdev->mme_mask = 0;
		hdev->pdma_ch_mask = 0x0;
		hdev->edma_mask = 0x0;
		hdev->hard_reset_on_fw_events = 1;
		hdev->decoder_mask = 0;
		hdev->dram_binning = 0x0;
		hdev->edma_binning = 0x0;
		hdev->tpc_binning = 0x0;
		hdev->decoder_binning = 0x0;
		hdev->scrub_arc_dccm = 0;
		hdev->axi_drain = AXI_DRAIN_SKIP;
		hdev->fw_communication_enable = 1;
		hdev->sched_arc_mask = 0x3F;
		hdev->rotator_mask = 0x0;
		hdev->use_8_bit_hops = 0;
		hdev->priv_security_enable = 0;
		hdev->cache_enable = 0;
		hdev->rotator_binning = 0;
		hdev->hbm_compression_enable = 0;
		hdev->heartbeat_reset_enable = 1;
		break;

	case ASIC_GAUDI3:
		hdev->dram_enable = 0;
		hdev->fw_components = 0;
		hdev->mmu_enable = 0;
		hdev->security_enable = 1;
		hdev->tpc_mask = 0;
		hdev->mme_mask = 0;
		hdev->pdma_ch_mask = 0xFFFFFF;
		hdev->edma_mask = 0x0;
		hdev->hard_reset_on_fw_events = 0;
		hdev->decoder_mask = 0;
		hdev->dram_binning = 0x0;
		hdev->edma_binning = 0x0;
		hdev->tpc_binning = 0x0;
		hdev->decoder_binning = 0x0;
		hdev->scrub_arc_dccm = 0;
		hdev->axi_drain = AXI_DRAIN_SKIP;
		hdev->fw_communication_enable = 0;
		hdev->sched_arc_mask = 0xFFFF;
		hdev->rotator_mask = 0x0;
		hdev->use_8_bit_hops = 0;
		hdev->priv_security_enable = 0;
		hdev->cache_enable = 1;
		hdev->ptw_bypass_enable = 1;
		hdev->rotator_binning = 0;
		hdev->hbm_compression_enable = 1;
		hdev->heartbeat_reset_enable = 1;
		break;

	case ASIC_GAUDI3_SIM:
		hdev->dram_enable = 1;
		hdev->fw_components = 0;
		hdev->mmu_enable = MMU_EN_ALL;
		hdev->security_enable = 1;
		hdev->tpc_mask = 0xFFFFFFFFFFFFFFFFull;
		hdev->mme_mask = 0xFF;
		hdev->pdma_ch_mask = 0xFFFFFF;
		hdev->edma_mask = 0xFF;
		hdev->hard_reset_on_fw_events = 0;
		hdev->decoder_mask = 0xFFFF;
		hdev->dram_binning = 0x0;
		hdev->edma_binning = 0x0;
		hdev->tpc_binning = 0x0;
		hdev->decoder_binning = 0x0;
		hdev->scrub_arc_dccm = 1;
		hdev->axi_drain = AXI_DRAIN_SKIP;
		hdev->fw_communication_enable = 0;
		hdev->sched_arc_mask = 0xFFFF;
		hdev->rotator_mask = 0xFF;
		hdev->use_8_bit_hops = 0;
		hdev->priv_security_enable = 1;
		hdev->cache_enable = 1;
		hdev->rotator_binning = 0;
		hdev->hbm_compression_enable = 1;
		hdev->heartbeat_reset_enable = 1;
		break;

	case ASIC_GAUDI3_SIM_ARC:
		hdev->dram_enable = 1;
		hdev->fw_components = FW_TYPE_BOOT_CPU | FW_TYPE_PREBOOT_CPU;
		hdev->mmu_enable = MMU_EN_ALL;
		hdev->security_enable = 1;
		hdev->tpc_mask = 0xFFFFFFFFFFFFFFFFull;
		hdev->mme_mask = 0xFF;
		hdev->pdma_ch_mask = 0xFFFFFF;
		hdev->edma_mask = 0xFF;
		hdev->hard_reset_on_fw_events = 1;
		hdev->decoder_mask = 0xFFFF;
		hdev->dram_binning = 0x0;
		hdev->edma_binning = 0x0;
		hdev->tpc_binning = 0x0;
		hdev->decoder_binning = 0x0;
		hdev->scrub_arc_dccm = 1;
		hdev->axi_drain = AXI_DRAIN_SKIP;
		hdev->fw_communication_enable = 1;
		hdev->sched_arc_mask = 0xFFFF;
		hdev->rotator_mask = 0xFF;
		hdev->use_8_bit_hops = 0;
		hdev->priv_security_enable = 1;
		hdev->cache_enable = 1;
		hdev->rotator_binning = 0;
		hdev->hbm_compression_enable = 1;
		hdev->heartbeat_reset_enable = 1;
		break;

	case ASIC_GAUDI3_SIM_SINGLE_DIE:
		hdev->dram_enable = 1;
		hdev->fw_components = 0;
		hdev->mmu_enable = MMU_EN_ALL;
		hdev->security_enable = 0;
		hdev->tpc_mask = 0xFFFFFFFF;
		hdev->mme_mask = 0xF;
		hdev->pdma_ch_mask = 0xFFF;
		hdev->edma_mask = 0xF;
		hdev->hard_reset_on_fw_events = 0;
		hdev->decoder_mask = 0xFF;
		hdev->dram_binning = 0x0;
		hdev->edma_binning = 0x0;
		hdev->tpc_binning = 0x0;
		hdev->decoder_binning = 0x0;
		hdev->scrub_arc_dccm = 1;
		hdev->axi_drain = AXI_DRAIN_SKIP;
		hdev->fw_communication_enable = 0;
		hdev->sched_arc_mask = 0xFF;
		hdev->rotator_mask = 0xF;
		hdev->use_8_bit_hops = 0;
		hdev->priv_security_enable = 0;
		hdev->cache_enable = 1;
		hdev->rotator_binning = 0;
		hdev->hbm_compression_enable = 1;
		hdev->heartbeat_reset_enable = 1;
		break;

	case ASIC_GAUDI3_SIM_SINGLE_DIE_ARC:
		hdev->dram_enable = 1;
		hdev->fw_components = FW_TYPE_BOOT_CPU | FW_TYPE_PREBOOT_CPU;
		hdev->mmu_enable = MMU_EN_ALL;
		hdev->security_enable = 0;
		hdev->tpc_mask = 0xFFFFFFFF;
		hdev->mme_mask = 0xF;
		hdev->pdma_ch_mask = 0xFFF;
		hdev->edma_mask = 0xF;
		hdev->hard_reset_on_fw_events = 1;
		hdev->decoder_mask = 0xFF;
		hdev->dram_binning = 0x0;
		hdev->edma_binning = 0x0;
		hdev->tpc_binning = 0x0;
		hdev->decoder_binning = 0x0;
		hdev->scrub_arc_dccm = 1;
		hdev->axi_drain = AXI_DRAIN_SKIP;
		hdev->fw_communication_enable = 1;
		hdev->sched_arc_mask = 0xFF;
		hdev->rotator_mask = 0xF;
		hdev->use_8_bit_hops = 0;
		hdev->priv_security_enable = 0;
		hdev->cache_enable = 1;
		hdev->rotator_binning = 0;
		hdev->hbm_compression_enable = 1;
		hdev->heartbeat_reset_enable = 1;
		break;

	case ASIC_GAUDI3_FPGA:
		hdev->dram_enable = 1;
		hdev->fw_components = FW_TYPE_BOOT_CPU | FW_TYPE_PREBOOT_CPU;
		hdev->mmu_enable = 0;
		hdev->security_enable = 0;
		hdev->tpc_mask = 0;
		hdev->mme_mask = 0;
		hdev->pdma_ch_mask = 0x0;
		hdev->edma_mask = 0x0;
		hdev->hard_reset_on_fw_events = 1;
		hdev->decoder_mask = 0;
		hdev->dram_binning = 0x0;
		hdev->edma_binning = 0x0;
		hdev->tpc_binning = 0x0;
		hdev->decoder_binning = 0x0;
		hdev->scrub_arc_dccm = 0;
		hdev->axi_drain = AXI_DRAIN_SKIP;
		hdev->fw_communication_enable = 1;
		hdev->sched_arc_mask = 0;
		hdev->rotator_mask = 0x0;
		hdev->use_8_bit_hops = 0;
		hdev->priv_security_enable = 0;
		hdev->cache_enable = 0;
		hdev->rotator_binning = 0;
		hdev->hbm_compression_enable = 1;
		hdev->heartbeat_reset_enable = 1;
		break;

	default:
		hdev->dram_enable = 1;
		hdev->fw_components = FW_TYPE_ALL_TYPES;
		hdev->mmu_enable = MMU_EN_ALL;
		hdev->security_enable = 1;
		hdev->tpc_mask = 0x3FF;
		hdev->mme_mask = 0x3;
		hdev->pdma_ch_mask = 0x0;
		hdev->edma_mask = 0x0;
		hdev->hard_reset_on_fw_events = 1;
		hdev->decoder_mask = 0x3FF;
		hdev->dram_binning = 0x0;
		hdev->edma_binning = 0x0;
		hdev->tpc_binning = 0x0;
		hdev->decoder_binning = 0x0;
		hdev->scrub_arc_dccm = 0;
		hdev->axi_drain = AXI_DRAIN_ENABLED;
		hdev->fw_communication_enable = 1;
		hdev->sched_arc_mask = 0;
		hdev->rotator_mask = 0x3;
		hdev->use_8_bit_hops = 0;
		hdev->priv_security_enable = 1;
		hdev->cache_enable = 0;
		hdev->rotator_binning = 0;
		hdev->hbm_compression_enable = 0;
		break;
	}

	hdev->clock_gating_enabled = 1;
	hdev->heartbeat = 1;
	hdev->sram_scrambler_enable = 1;
	hdev->dram_scrambler_enable = 1;
	hdev->hbm_ecc_enable = 1;
	hdev->bmc_enable = 1;
	hdev->mmu_huge_page_opt = 1;
	hdev->reset_on_preboot_fail = 1;
	hdev->rl_enable = 1;

	hdev->reset_pcilink = 0;
	hdev->config_pll = 0;
	hdev->nic_load_fw = 0;
	hdev->sram_binning = 0;
	hdev->compatibility_mode = 0;
	hdev->force_driver_reset = FORCE_DRIVER_RESET_NONE;
	hdev->force_driver_clock_gating = 0;
	hdev->pll_async_if_enable = 0;
	hdev->bootfit_relocatable = 0;
	hdev->reset_if_device_not_idle = 1;
	hdev->usr_hbm_pll_freq = 1600;
	hdev->half_nominal_pll_mode = 0;
	hdev->skip_cluster_config = 1;
	hdev->fw_cfg_skip = 0;
	hdev->bmu_enable = 1;
	hdev->nic_eth_on_internal = 0;
	hdev->config_qman_arc_for_stub_mme = 0;
	hdev->skip_nic_phy_init = 0;
	hdev->odp_enabled = 1;
	hdev->pci_rev_id_override = 0;
	hdev->debug_wreg = 1;
	hdev->debug_rreg = 1;
	hdev->enable_h9_cache_eta_eco = 0;

	/* ECOs should be enabled by default */
	hdev->nic_enable_h9_rx_drop_eco = 1;
	hdev->nic_enable_h9_qp_doorbells_eco = 1;
	hdev->nic_enable_h9_cc_msg_drops_eco = 1;
	hdev->nic_enable_h9_remote_pi_update_eco = 1;
	hdev->nic_enable_h9_rxb_mem_deadlock_eco = 1;
	hdev->nic_enable_h9_single_qp_perf_fix_eco = 1;
	hdev->nic_enable_h9_sal_override_eco = 1;
	hdev->nic_enable_h9_sack_deadlock_eco = 1;
	hdev->nic_enable_h9_txe_buff_alloc_eco = 1;
	hdev->enable_h9_cache_eta_eco = 1;
}

static void copy_kernel_module_params_to_device(struct hl_device *hdev)
{
	hdev->asic_prop.fw_security_enabled = is_asic_secured(hdev->asic_type);

	hdev->ifh = ifh;
	hdev->major = hl_major;
	hdev->accel_major = hl_accel_get_major();
	hdev->hclass = hl_class;
	hdev->aclass = hl_accel_get_class();
	hdev->low_freq = low_freq;
	hdev->card_type = card_type;
	hdev->memory_scrub = memory_scrub;
	hdev->reset_on_lockup = reset_on_lockup;
	hdev->nic_poll_enable = nic_poll_enable;
	hdev->ignore_fw_nic_info = ignore_fw_nic_info;
	hdev->nic_lanes_per_port = nic_lanes_per_port;
	hdev->boot_error_status_mask = boot_error_status_mask;
	hdev->reset_upon_device_release = reset_upon_device_release;
	hdev->skip_iatu_for_unsecured_device = skip_iatu_for_unsecured_device;
	hdev->gaudi2_setup_type = gaudi2_setup_type;
	hdev->ignore_eeprom_errors = ignore_eeprom_errors;
}

static void copy_bfe_params_to_device(struct hl_device *hdev)
{
	struct lkd_fw_binning_info *dbg_conf = &hdev->dbg_binning_conf;

	hdev->pldm = pldm;
	hdev->bringup_flags_enable = bringup_flags_enable;
	if (hdev->pldm)
		hdev->bringup_flags_enable = 1;

	if (!hdev->bringup_flags_enable)
		return;

	hdev->mmu_enable = bfe_mmu_enable;
	hdev->clock_gating_enabled = bfe_clock_gating;
	hdev->fw_components = bfe_fw_components;
	hdev->fw_communication_enable = bfe_fw_communication_enable;
	hdev->heartbeat = bfe_heartbeat;
	hdev->mme_mask = bfe_mme_mask;
	hdev->tpc_mask = bfe_tpc_mask;
	hdev->decoder_mask = bfe_decoder_mask;
	hdev->rotator_mask = bfe_rotator_mask;
	hdev->pdma_ch_mask = bfe_pdma_ch_mask;
	hdev->edma_mask = bfe_edma_mask;
	hdev->sched_arc_mask = bfe_sched_arc_mask;
	hdev->dram_enable = bfe_dram_enable;
	hdev->axi_drain = bfe_axi_drain;
	hdev->security_enable = bfe_security_enable;
	hdev->sram_scrambler_enable = bfe_sram_scrambler_enable;
	hdev->dram_scrambler_enable = bfe_dram_scrambler_enable;
	hdev->hbm_ecc_enable = bfe_hbm_ecc_enable;
	hdev->compatibility_mode = bfe_compatibility_mode;
	hdev->hard_reset_on_fw_events = bfe_hard_reset_on_fw_events;
	hdev->reset_if_device_not_idle = bfe_reset_if_device_not_idle;
	hdev->half_nominal_pll_mode = bfe_half_nominal_pll_mode;
	hdev->bmc_enable = bfe_bmc_enable;
	hdev->nic_load_fw = bfe_nic_load_fw;
	hdev->mmu_huge_page_opt = bfe_gaudi_huge_page_optimization;
	hdev->rl_enable = bfe_rl_enable;
	hdev->reset_pcilink = bfe_reset_pcilink;
	hdev->config_pll = bfe_config_pll;
	hdev->sram_binning = bfe_sram_binning;
	hdev->tpc_binning = bfe_tpc_binning;
	hdev->dram_binning = bfe_dram_binning;
	hdev->decoder_binning = bfe_decoder_binning;
	hdev->edma_binning = bfe_edma_binning;
	hdev->usr_hbm_pll_freq = bfe_hbm_pll_freq;
	hdev->reset_on_preboot_fail = bfe_reset_on_preboot_fail;
	hdev->force_driver_reset = bfe_force_driver_reset;
	hdev->force_driver_clock_gating = bfe_force_driver_clock_gating;
	hdev->pll_async_if_enable = bfe_pll_async_if_enable;
	hdev->bootfit_relocatable = bfe_bootfit_relocatable;
	hdev->scrub_arc_dccm = bfe_scrub_arc_dccm;
	hdev->skip_cluster_config = bfe_skip_cluster_config;
	hdev->fw_cfg_skip = bfe_fw_cfg_skip;
	hdev->bmu_enable = bfe_bmu_enable;
	hdev->nic_eth_on_internal = bfe_nic_eth_on_internal;
	hdev->config_qman_arc_for_stub_mme = bfe_config_qman_arc_for_stub_mme;
	hdev->skip_nic_phy_init = bfe_skip_nic_phy_init;
	hdev->debug_rreg = bfe_debug_rreg;
	hdev->debug_wreg = bfe_debug_wreg;
	hdev->odp_enabled = bfe_enable_odp;
	hdev->use_8_bit_hops = bfe_use_8_bit_hops;
	hdev->cache_enable = bfe_cache_enable;
	hdev->enable_intr_aggr = bfe_enable_intr_aggr;
	hdev->halt_eng_upon_fw_events = bfe_halt_eng_upon_fw_events;
	hdev->hmmu_supported_pages_mask = bfe_hmmu_supported_pages_mask;
	hdev->hmmu_default_page_size = bfe_hmmu_default_page_size;
	hdev->priv_security_enable = bfe_priv_security_enable;
	hdev->pci_rev_id_override = bfe_pci_rev_id;
	hdev->ptw_bypass_enable = bfe_ptw_bypass_enable;
	hdev->rotator_binning = bfe_rotator_binning;
	hdev->hbm_compression_enable = bfe_hbm_compression_enable;
	hdev->nic_enable_h9_rx_drop_eco = bfe_nic_enable_h9_rx_drop_eco;
	hdev->enable_h9_cache_eta_eco = bfe_enable_h9_cache_eta_eco;
	hdev->force_h9_single_die = bfe_force_h9_single_die;
	hdev->nic_enable_h9_qp_doorbells_eco = bfe_nic_enable_h9_qp_doorbells_eco;
	hdev->nic_enable_h9_cc_msg_drops_eco = bfe_nic_enable_h9_cc_msg_drops_eco;
	hdev->nic_enable_h9_remote_pi_update_eco = bfe_nic_enable_h9_remote_pi_update_eco;
	hdev->nic_enable_h9_rxb_mem_deadlock_eco = bfe_nic_enable_h9_rxb_mem_deadlock_eco;
	hdev->nic_enable_h9_single_qp_perf_fix_eco = bfe_nic_enable_h9_single_qp_perf_fix_eco;
	hdev->nic_enable_h9_sal_override_eco = bfe_nic_enable_h9_sal_override_eco;
	hdev->nic_enable_h9_sack_deadlock_eco = bfe_nic_enable_h9_sack_deadlock_eco;
	hdev->nic_enable_h9_txe_buff_alloc_eco = bfe_nic_enable_h9_txe_buff_alloc_eco;
	hdev->heartbeat_reset_enable = bfe_heartbeat_reset_enable;

	/* Debug feature:
	 * Store a copy of binning information to override f/w binning configuration later
	 * when requested by the user
	 */
	dbg_conf->tpc_mask_l = cpu_to_le64(bfe_tpc_binning);
	dbg_conf->dram_mask = cpu_to_le32(bfe_dram_binning);
	dbg_conf->dec_mask = cpu_to_le32(bfe_decoder_binning);
	dbg_conf->edma_mask = cpu_to_le32(bfe_edma_binning);
	dbg_conf->mme_mask_l = cpu_to_le32(bfe_mme_row_repair_l);
	dbg_conf->mme_mask_h = cpu_to_le32(bfe_mme_row_repair_h);
	dbg_conf->rot_mask = cpu_to_le32(bfe_rotator_binning);
}

static void fixup_fw_components_param(struct hl_device *hdev)
{
	switch (hdev->asic_type) {
	case ASIC_GOYA_SIM:
	case ASIC_GRECO_SIM:
	case ASIC_GAUDI_SIM:
	case ASIC_GAUDI_HL2000M_SIM:
	case ASIC_GAUDI2_SIM:
	case ASIC_GAUDI2B_SIM:
	case ASIC_GAUDI3_SIM:
	case ASIC_GAUDI3_SIM_SINGLE_DIE:
		/* Enforce running without F/W for non SIM_ARC simulators */
		hdev->fw_components = FW_TYPE_NONE;
		break;
	default:
		break;
	}
}

static void fixup_device_params_per_asic(struct hl_device *hdev, int timeout)
{
	bool single_die_asic = false;

	switch (hdev->asic_type) {
	case ASIC_GAUDI:
	case ASIC_GAUDI_HL2000M:
	case ASIC_GAUDI_SEC:
	case ASIC_GAUDI_HL2000M_SEC:
	case ASIC_GAUDI_SIM:
	case ASIC_GAUDI_HL2000M_SIM:
		/* If user didn't request a different timeout than the default one, we have
		 * a different default timeout for Gaudi
		 */
		if (timeout == HL_DEFAULT_TIMEOUT_LOCKED)
			hdev->timeout_jiffies = msecs_to_jiffies(GAUDI_DEFAULT_TIMEOUT_LOCKED *
										MSEC_PER_SEC);

		hdev->reset_upon_device_release = 0;
		break;

	case ASIC_GAUDI2_SIM:
	case ASIC_GAUDI2B_SIM:
	case ASIC_GAUDI2_SIM_ARC:
	case ASIC_GAUDI2B_SIM_ARC:
	case ASIC_GAUDI2:
	case ASIC_GAUDI2B:
		if (!nic_poll_enable_param_was_set)
			hdev->nic_poll_enable = false;

		/* TODO: remove this workaround after f/w fix the boot bug which cause us
		 * to fail on reading ELBI. I keep an option for the user to force not to skip
		 * for any strange reason he might have.
		 */
		if (!hdev->skip_iatu_for_unsecured_device)
			hdev->skip_iatu_for_unsecured_device = 1;
		else if (hdev->skip_iatu_for_unsecured_device == 2)
			hdev->skip_iatu_for_unsecured_device = 0;

		break;

	case ASIC_GAUDI3_SIM_SINGLE_DIE:
	case ASIC_GAUDI3_SIM_SINGLE_DIE_ARC:
	case ASIC_GAUDI3_SINGLE_DIE:
		single_die_asic = true;
		fallthrough;
	case ASIC_GAUDI3_SIM:
	case ASIC_GAUDI3_SIM_ARC:
	case ASIC_GAUDI3:
		if (!nic_poll_enable_param_was_set)
			hdev->nic_poll_enable = false;

		/* DRAM cannot be used if SRAM is enabled */
		if (!hdev->cache_enable)
			hdev->dram_enable = 0;

		if ((single_die_asic || hdev->force_h9_single_die) &&
				(hdev->security_enable || hdev->priv_security_enable)) {
			pr_err("Security is disabled (sec/priv) as it isn't supported on single-die mode\n");
			hdev->security_enable = false;
			hdev->priv_security_enable = false;
		}

		break;

	default:
		hdev->reset_upon_device_release = 0;
		break;
	}
}

static int fixup_device_params(struct hl_device *hdev)
{
	u32 dev_nic_ports_mask;
	int tmp_timeout;

	tmp_timeout = timeout_locked;

	if (hdev->pldm) {
		hdev->memory_scrub = 0;
		hdev->fw_poll_interval_usec = HL_FW_STATUS_PLDM_POLL_INTERVAL_USEC;
		hdev->fw_comms_poll_interval_usec = HL_FW_COMMS_STATUS_PLDM_POLL_INTERVAL_USEC;

		/* All hl-thunk tests that run on default should not fail
		 * on 20 seconds timeout
		 */
		if (tmp_timeout > 0 && tmp_timeout < 20)
			tmp_timeout = 20;
	} else {
		hdev->fw_poll_interval_usec = HL_FW_STATUS_POLL_INTERVAL_USEC;
		hdev->fw_comms_poll_interval_usec = HL_FW_STATUS_POLL_INTERVAL_USEC;
	}

	if (tmp_timeout)
		hdev->timeout_jiffies = msecs_to_jiffies(tmp_timeout * MSEC_PER_SEC);
	else
		hdev->timeout_jiffies = MAX_SCHEDULE_TIMEOUT;

	hdev->stop_on_err = true;
	hdev->driver_ver = HL_MODULE_VERSION;
	hdev->reset_info.curr_reset_cause = HL_RESET_CAUSE_UNKNOWN;
	hdev->reset_info.prev_reset_trigger = HL_RESET_TRIGGER_DEFAULT;

	/* Enable only after the initialization of the device */
	hdev->disabled = true;

	/* If DRAM is disabled, don't load legacy F/W, don't enable DMMU/MMU */
	if (!hdev->dram_enable) {
		hdev->fw_components &= ~FW_TYPE_LINUX;
		if (hdev->mmu_enable)
			hdev->mmu_enable = MMU_EN_PMMU_ONLY;
		else
			hdev->mmu_enable = MMU_EN_NONE;

		hdev->dram_scrambler_enable = 0;
		hdev->hbm_ecc_enable = 0;
	}

	fixup_fw_components_param(hdev);

	if (!(hdev->fw_components & FW_TYPE_PREBOOT_CPU) &&
			(hdev->fw_components & ~FW_TYPE_PREBOOT_CPU)) {
		pr_err("Preboot must be set along with other components");
		return -EINVAL;
	}

	hdev->cpu_queues_enable = is_cpu_queue_enabled(hdev);

	/* If CPU queues not enabled, no way to do heartbeat */
	if (!hdev->cpu_queues_enable)
		hdev->heartbeat = 0;

	if (!hdev->mmu_enable)
		hdev->security_enable = 0;

	/* Adjust NIC ports parameters according to the device in-hand */
	dev_nic_ports_mask = get_dev_nic_ports_mask(hdev->asic_type);

	hdev->nic_ports_mask = nic_ports_mask & dev_nic_ports_mask;
	/* ports ext and autoneg masks are subsets of device ports_pask */
	hdev->nic_ports_ext_mask = nic_ports_ext_mask & hdev->nic_ports_mask;
	hdev->nic_auto_neg_mask = nic_auto_neg_mask & hdev->nic_ports_mask;

	hdev->nic_qp_drain_time = nic_qp_drain_time;
	/*
	 * if security is enabled we cannot force driver reset.
	 * in PLDM we'll work only with driver resets due to long latency
	 */
	if (hdev->asic_prop.fw_security_enabled)
		hdev->force_driver_reset = FORCE_DRIVER_RESET_NONE;
	else if (hdev->pldm)
		hdev->force_driver_reset = FORCE_DRIVER_RESET_ALL;

	if (hdev->ifh) {
		hdev->heartbeat = 0;
		hdev->nic_ports_mask = 0;
	}

	fixup_device_params_per_asic(hdev, tmp_timeout);

	if (hdev->nic_lanes_per_port != PORT_LANES_4 && hdev->nic_lanes_per_port != PORT_LANES_2) {
		pr_err("%d lanes per NIC port is invalid\n", hdev->nic_lanes_per_port);
		return -EINVAL;
	}

	return 0;
}

/**
 * create_hdev - create habanalabs device instance
 *
 * @dev: will hold the pointer to the new habanalabs device structure
 * @pdev: pointer to the pci device
 * @asic_type: in case of simulator device, which device is it
 * @minor: in case of simulator device, the minor of the device
 *
 * Allocate memory for habanalabs device and initialize basic fields
 * Identify the ASIC type
 * Allocate ID (minor) for the device (only for real devices)
 */
int create_hdev(struct hl_device **dev, struct pci_dev *pdev,
		enum hl_asic_type asic_type, int minor)
{
	int rc, main_id, ctrl_id = 0;
	struct hl_device *hdev;

	*dev = NULL;

	hdev = kzalloc(sizeof(*hdev), GFP_KERNEL);
	if (!hdev)
		return -ENOMEM;

	/* Will be NULL in case of simulator device */
	hdev->pdev = pdev;

	/* Assign status description string */
	strncpy(hdev->status[HL_DEVICE_STATUS_OPERATIONAL], "operational", HL_STR_MAX);
	strncpy(hdev->status[HL_DEVICE_STATUS_IN_RESET], "in reset", HL_STR_MAX);
	strncpy(hdev->status[HL_DEVICE_STATUS_MALFUNCTION], "disabled", HL_STR_MAX);
	strncpy(hdev->status[HL_DEVICE_STATUS_NEEDS_RESET], "needs reset", HL_STR_MAX);
	strncpy(hdev->status[HL_DEVICE_STATUS_IN_DEVICE_CREATION],
					"in device creation", HL_STR_MAX);
	strncpy(hdev->status[HL_DEVICE_STATUS_IN_RESET_AFTER_DEVICE_RELEASE],
					"in reset after device release", HL_STR_MAX);

	copy_bfe_params_to_device(hdev);
	set_pci_revision_id(hdev, asic_type);

	/* First, we must find out which ASIC are we handling. This is needed
	 * to configure the behavior of the driver (kernel parameters)
	 */
	if (hdev->pdev) {
		hdev->asic_type = get_asic_type(hdev);
		if (hdev->asic_type == ASIC_INVALID) {
			dev_err(&pdev->dev, "Unsupported ASIC\n");
			rc = -ENODEV;
			goto free_hdev;
		}
	} else {
		hdev->asic_type = asic_type;
	}

	copy_kernel_module_params_to_device(hdev);

	set_driver_behavior_per_device(hdev);

	rc = fixup_device_params(hdev);
	if (rc)
		goto free_hdev;

	mutex_lock(&hl_devs_idr_lock);

	if (minor == -1) {
		/* Always save 2 numbers, 1 for main device and 1 for control.
		 * They must be consecutive
		 */
		main_id = idr_alloc(&hl_devs_idr, hdev, 0, HL_MAX_MINORS,
					GFP_KERNEL);

		if (main_id >= 0)
			ctrl_id = idr_alloc(&hl_devs_idr, hdev, main_id + 1,
						main_id + 2, GFP_KERNEL);
	} else {
		void *old_idr = idr_replace(&hl_devs_idr, hdev, minor);

		if (IS_ERR_VALUE(old_idr)) {
			rc = PTR_ERR(old_idr);
			pr_err("Error %d when trying to replace minor %d\n",
				rc, minor);
			mutex_unlock(&hl_devs_idr_lock);
			goto free_hdev;
		}
		main_id = minor;

		old_idr = idr_replace(&hl_devs_idr, hdev, main_id + 1);
		if (IS_ERR_VALUE(old_idr)) {
			rc = PTR_ERR(old_idr);
			pr_err("Error %d when trying to replace 2nd minor %d\n",
				rc, main_id + 1);
			mutex_unlock(&hl_devs_idr_lock);
			goto free_hdev;
		}
		ctrl_id = main_id + 1;
	}

	mutex_unlock(&hl_devs_idr_lock);

	if ((main_id < 0) || (ctrl_id < 0)) {
		if ((main_id == -ENOSPC) || (ctrl_id == -ENOSPC))
			pr_err("too many devices in the system\n");

		if (main_id >= 0) {
			mutex_lock(&hl_devs_idr_lock);
			idr_remove(&hl_devs_idr, main_id);
			mutex_unlock(&hl_devs_idr_lock);
		}

		rc = -EBUSY;
		goto free_hdev;
	}

	hdev->id = main_id;
	hdev->id_control = ctrl_id;

	*dev = hdev;

	return 0;

free_hdev:
	kfree(hdev);
	return rc;
}

/*
 * destroy_hdev - destroy habanalabs device instance
 *
 * @dev: pointer to the habanalabs device structure
 *
 */
static void destroy_hdev(struct hl_device *hdev)
{
	/* Remove device from the device list */
	mutex_lock(&hl_devs_idr_lock);
	idr_remove(&hl_devs_idr, hdev->id);
	idr_remove(&hl_devs_idr, hdev->id_control);
	mutex_unlock(&hl_devs_idr_lock);

	kfree(hdev);
}

static int hl_pmops_suspend(struct device *dev)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	pr_debug("Going to suspend PCI device\n");

	if (!hdev) {
		pr_err("device pointer is NULL in suspend\n");
		return 0;
	}

	return hl_device_suspend(hdev);
}

static int hl_pmops_resume(struct device *dev)
{
	struct hl_device *hdev = dev_get_drvdata(dev);

	pr_debug("Going to resume PCI device\n");

	if (!hdev) {
		pr_err("device pointer is NULL in resume\n");
		return 0;
	}

	return hl_device_resume(hdev);
}

static void pci_remove_device(struct work_struct *work)
{
	struct hl_device *hdev = container_of(work, struct hl_device, work_pci);

	if (hdev->pdev)
		pci_stop_and_remove_bus_device_locked(hdev->pdev);
}

/*
 * hl_pci_probe - probe PCI habanalabs devices
 *
 * @pdev: pointer to pci device
 * @id: pointer to pci device id structure
 *
 * Standard PCI probe function for habanalabs device.
 * Create a new habanalabs device and initialize it according to the
 * device's type
 */
static int hl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct hl_device *hdev;
	int rc;

	dev_info(&pdev->dev, HL_NAME
		 " device found [%04x:%04x] (rev %x)\n",
		 (int)pdev->vendor, (int)pdev->device, (int)pdev->revision);

	rc = create_hdev(&hdev, pdev, ASIC_INVALID, -1);
	if (rc)
		return rc;

	pci_set_drvdata(pdev, hdev);

	/*
	 * Must be initialized before hl_device_init() as the device might
	 * need to be removed from PCI during initialization due to a failure.
	 */
	INIT_WORK(&hdev->work_pci, pci_remove_device);

	pci_enable_pcie_error_reporting(pdev);

	rc = hl_device_init(hdev);
	if (rc) {
		dev_err(&pdev->dev,
			"Fatal error during habanalabs device init\n");
		rc = -ENODEV;
		goto disable_device;
	}

	return 0;

disable_device:
	pci_disable_pcie_error_reporting(pdev);
	pci_set_drvdata(pdev, NULL);
	destroy_hdev(hdev);

	return rc;
}

/*
 * hl_pci_remove - remove PCI habanalabs devices
 *
 * @pdev: pointer to pci device
 *
 * Standard PCI remove function for habanalabs device
 */
static void hl_pci_remove(struct pci_dev *pdev)
{
	struct hl_device *hdev;

	hdev = pci_get_drvdata(pdev);
	if (!hdev)
		return;

	hl_device_fini(hdev);
	pci_disable_pcie_error_reporting(pdev);
	pci_set_drvdata(pdev, NULL);
	destroy_hdev(hdev);
}

/**
 * hl_pci_err_detected - a PCI bus error detected on this device
 *
 * @pdev: pointer to pci device
 * @state: PCI error type
 *
 * Called by the PCI subsystem whenever a non-correctable
 * PCI bus error is detected
 */
static pci_ers_result_t
hl_pci_err_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct hl_device *hdev = pci_get_drvdata(pdev);
	enum pci_ers_result result;

	switch (state) {
	case pci_channel_io_normal:
		dev_warn(hdev->dev, "PCI normal state error detected\n");
		return PCI_ERS_RESULT_CAN_RECOVER;

	case pci_channel_io_frozen:
		dev_warn(hdev->dev, "PCI frozen state error detected\n");
		result = PCI_ERS_RESULT_NEED_RESET;
		break;

	case pci_channel_io_perm_failure:
		dev_warn(hdev->dev, "PCI failure state error detected\n");
		result = PCI_ERS_RESULT_DISCONNECT;
		break;

	default:
		result = PCI_ERS_RESULT_NONE;
	}

	hdev->asic_funcs->halt_engines(hdev, true, false);

	return result;
}

/**
 * hl_pci_err_resume - resume after a PCI slot reset
 *
 * @pdev: pointer to pci device
 *
 */
static void hl_pci_err_resume(struct pci_dev *pdev)
{
	struct hl_device *hdev = pci_get_drvdata(pdev);

	dev_warn(hdev->dev, "Resuming device after PCI slot reset\n");
	hl_device_resume(hdev);
}

/**
 * hl_pci_err_slot_reset - a PCI slot reset has just happened
 *
 * @pdev: pointer to pci device
 *
 * Determine if the driver can recover from the PCI slot reset
 */
static pci_ers_result_t hl_pci_err_slot_reset(struct pci_dev *pdev)
{
	struct hl_device *hdev = pci_get_drvdata(pdev);

	dev_warn(hdev->dev, "PCI slot reset detected\n");

	return PCI_ERS_RESULT_RECOVERED;
}

static const struct dev_pm_ops hl_pm_ops = {
	.suspend = hl_pmops_suspend,
	.resume = hl_pmops_resume,
};

static const struct pci_error_handlers hl_pci_err_handler = {
	.error_detected = hl_pci_err_detected,
	.slot_reset = hl_pci_err_slot_reset,
	.resume = hl_pci_err_resume,
};

static struct pci_driver hl_pci_driver = {
	.name = HL_NAME,
	.id_table = ids,
	.probe = hl_pci_probe,
	.remove = hl_pci_remove,
	.shutdown = hl_pci_remove,
	.driver = {
		.name = HL_NAME,
		.pm = &hl_pm_ops,
#if KERNEL_VERSION(4, 9, 0) <= LINUX_VERSION_CODE
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
#endif
	},
	.err_handler = &hl_pci_err_handler,
};

static int pci_mon_func(void *param)
{
	struct hl_device *hdev;
	unsigned long timeout = msecs_to_jiffies(60 * MSEC_PER_SEC);
	int rc, id;

	while (!kthread_should_stop()) {
		/* Use timeout to avoid warnings for sleeping too long */
		rc = wait_for_completion_interruptible_timeout(&hl_pci_mon.comp,
								timeout);

		/*
		 * No need to iterate on the devices when timed out or signaled
		 * but only when completed.
		 */
		if ((rc != 0) && (rc != -ERESTARTSYS)) {
			/*
			 * Remove the device outside of the loop in order to
			 * avoid possible deadlocks as the for-each loop is
			 * under the idr mutex.
			 * If there is more than one device to remove,
			 * wait_for_completion() will immediately return to
			 * remove it.
			 */
			mutex_lock(&hl_devs_idr_lock);
			idr_for_each_entry(&hl_devs_idr, hdev, id)
				if (hdev->pci_remove)
					break;
			mutex_unlock(&hl_devs_idr_lock);

			/*
			 * Dispatch the remove to other thread so we will
			 * immediately handle other waiting devices.
			 */
			if (hdev)
				queue_work(system_long_wq, &hdev->work_pci);
			else if (!hl_pci_mon.in_teardown)
				pr_crit("No device to be removed was found\n");
		}
	}

	return 0;
}

void hl_pci_force_remove_device(struct hl_device *hdev)
{
	hdev->pci_remove = true;
	/* Set the remove flag before waking up the waiting thread */
	mb();
	complete(&hl_pci_mon.comp);
}

#define HL_EVENT_FILE_MAX_NAME_LEN	128
#define HL_EVENTS_DIR	"/sys/kernel/debug/tracing/events/habanalabs"

static char *hl_events[HL_TRACE_NUM_EVENTS] __initdata = {
	[HL_TRACE_MMU_MAP] = "habanalabs_mmu_map",
	[HL_TRACE_MMU_UNMAP] = "habanalabs_mmu_unmap",
	[HL_TRACE_DMA_ALLOC] = "habanalabs_dma_alloc",
	[HL_TRACE_DMA_FREE] = "habanalabs_dma_free",
	[HL_TRACE_COMMS_PROT_CMD] = "habanalabs_comms_protocol_cmd",
	[HL_TRACE_COMMS_SEND_CMD] = "habanalabs_comms_send_cmd",
	[HL_TRACE_COMMS_WAIT_STAT] = "habanalabs_comms_wait_status",
	[HL_TRACE_COMMS_WAIT_STAT_DONE] = "habanalabs_comms_wait_status_done",
	[HL_TRACE_RREG32] = "habanalabs_rreg32",
	[HL_TRACE_WREG32] = "habanalabs_wreg32",
	[HL_TRACE_ELBI_READ] = "habanalabs_elbi_read",
	[HL_TRACE_ELBI_WRITE] = "habanalabs_elbi_write",
};

static char hl_event_filename_buffer[HL_EVENT_FILE_MAX_NAME_LEN] __initdata;

static long __init hl_enable_trace_event(const char *fpath)
{
	struct file *filp;
	ssize_t n, nr;
	loff_t pos;
	long rc = 0;

	filp = filp_open(fpath, O_RDWR, 0);
	if (IS_ERR(filp)) {
		rc = PTR_ERR(filp);
		pr_err("habanalabs: trace file %s open error %ld\n", fpath, rc);
		return rc;
	}

	pos = filp->f_pos;

	/* expected to write single char */
	nr = 1;

#ifdef _HAS_KERNEL_WRITE_WITH_PTR
	n = kernel_write(filp, "1", nr, &pos);
#else
	n = kernel_write(filp, "1", nr, pos);
#endif

	if (n != nr) {
		pr_err("habanalabs: trace file write error %ld\n", (long)n);
		rc = -EFAULT;
	}

	filp_close(filp, NULL);

	return rc;
}

static void hl_trace_print_sync_timestamp(void)
{
	u64 ts_nsec, ts_sec;

	ts_nsec = sched_clock();
	ts_sec = ts_nsec / 1000000000;
	ts_nsec %= 1000000000;

	pr_info("habanalabs: sync trace timestamp %llu.%llu\n", ts_sec, ts_nsec);
}

static void __init hl_enable_trace_events(void)
{
	int i, dir_namelen;

	if (!enable_events_tracing)
		return;

	hl_trace_print_sync_timestamp();

	if ((enable_events_tracing & HL_TRACE_ALL_EVENTS_MASK) == HL_TRACE_ALL_EVENTS_MASK) {
		hl_enable_trace_event(HL_EVENTS_DIR "/enable");
		return;
	}

	dir_namelen = snprintf(hl_event_filename_buffer, HL_EVENT_FILE_MAX_NAME_LEN,
					"%s/", HL_EVENTS_DIR);
	if ((dir_namelen < 0) || (dir_namelen >= HL_EVENT_FILE_MAX_NAME_LEN)) {
		pr_err("failed to snprintf hl trace dir %d\n", dir_namelen);
		return;
	}

	for (i = 0; i < HL_TRACE_NUM_EVENTS; i++) {
		int tracefile_len = HL_EVENT_FILE_MAX_NAME_LEN - dir_namelen;
		long rc;

		if (!(enable_events_tracing & BIT_ULL(i)))
			continue;

		rc = snprintf(&hl_event_filename_buffer[dir_namelen], tracefile_len,
					"%s/enable", hl_events[i]);

		if ((rc < 0) || (rc >= tracefile_len)) {
			pr_err("failed to snprintf hl trace file %s %ld\n",
					hl_events[i], rc);
			return;
		}

		rc = hl_enable_trace_event(hl_event_filename_buffer);
		if (rc)
			return;
	}
}

/*
 * hl_init - Initialize the habanalabs kernel driver
 */
static int __init hl_init(void)
{
	int rc;
	dev_t dev;

	pr_info("loading driver, version: %s\n", HL_MODULE_VERSION);

	rc = alloc_chrdev_region(&dev, 0, HL_MAX_MINORS, HL_NAME);
	if (rc < 0) {
		pr_err("unable to get major\n");
		return rc;
	}

	hl_major = MAJOR(dev);

	hl_class = class_create(THIS_MODULE, HL_NAME);
	if (IS_ERR(hl_class)) {
		pr_err("failed to allocate class\n");
		rc = PTR_ERR(hl_class);
		goto remove_major;
	}

	hl_debugfs_init();

	hl_enable_trace_events();

	rc = hl_accel_init();
	if (rc)
		goto remove_debugfs;

	/* SIMULATOR CODE */
	rc = hl_sim_init(hl_class, hl_major, &hl_devs_idr, &hl_devs_idr_lock);
	if (rc < 0) {
		pr_err("fatal error during simulator mode device init\n");
		goto remove_accel;
	} else if (rc > 0) {
		pr_info("driver loaded in simulator only mode\n");
		return 0;
	}
	/* END OF SIMULATOR CODE */

	/* IMPORTER CODE */
	rc = hl_importer_init();
	if (rc) {
		pr_err("fatal error during importer driver init\n");
		goto remove_sim;
	}
	/* END OF IMPORTER CODE */

	init_completion(&hl_pci_mon.comp);

	hl_pci_mon.thread = kthread_run(pci_mon_func, NULL, "hl_pci_mon");
	if (IS_ERR(hl_pci_mon.thread)) {
		pr_err("failed to create pci monitor\n");
		rc = PTR_ERR(hl_pci_mon.thread);
		goto remove_importer;
	}

	/* Create a WQ to check ports status */
	int_ports_sts.wq = create_singlethread_workqueue("hl_ports_status");
	if (!int_ports_sts.wq) {
		rc = -ENOMEM;
		pr_err("Failed to create ports status WQ\n");
		goto remove_pci_mon;
	}

	INIT_DELAYED_WORK(&int_ports_sts.dwork, check_int_ports_status_work);
	queue_delayed_work(int_ports_sts.wq, &int_ports_sts.dwork,
			msecs_to_jiffies(CHECK_INT_PORTS_STATUS_EXECUTION_DELAY_SEC * 1000));

	rc = pci_register_driver(&hl_pci_driver);
	if (rc) {
		pr_err("failed to register pci device\n");
		goto destroy_int_ports_sts_wq;
	}

	pr_debug("driver loaded\n");

	return 0;

destroy_int_ports_sts_wq:
	cancel_delayed_work_sync(&int_ports_sts.dwork);
	destroy_workqueue(int_ports_sts.wq);
remove_pci_mon:
	hl_pci_mon.in_teardown = true;
	/* Set the teardown flag before waking up the waiting thread */
	mb();
	complete_all(&hl_pci_mon.comp);
	kthread_stop(hl_pci_mon.thread);
/* IMPORTER CODE */
remove_importer:
	hl_importer_exit();
/* END OF IMPORTER CODE */
/* SIMULATOR CODE */
remove_sim:
	hl_sim_fini();
/* END OF SIMULATOR CODE */
remove_accel:
	hl_accel_exit();
remove_debugfs:
	hl_debugfs_fini();
	class_destroy(hl_class);
remove_major:
	unregister_chrdev_region(MKDEV(hl_major, 0), HL_MAX_MINORS);
	return rc;
}

/*
 * hl_exit - Release all resources of the habanalabs kernel driver
 */
static void __exit hl_exit(void)
{
	/* SIMULATOR CODE */
	if (hl_sim_fini())
		goto skip_pci;
	/* END OF SIMULATOR CODE */

	cancel_delayed_work_sync(&int_ports_sts.dwork);
	destroy_workqueue(int_ports_sts.wq);

	hl_pci_mon.in_teardown = true;
	/* Set the teardown flag before waking up the waiting thread */
	mb();
	complete_all(&hl_pci_mon.comp);
	kthread_stop(hl_pci_mon.thread);

	pci_unregister_driver(&hl_pci_driver);

/* SIMULATOR CODE */
skip_pci:
/* END OF SIMULATOR CODE */

	hl_accel_exit();

	/*
	 * Removing debugfs must be after all devices or simulator devices
	 * have been removed because otherwise we get a bug in the
	 * debugfs module for referencing NULL objects
	 */
	hl_debugfs_fini();

	/* IMPORTER CODE */
	hl_importer_exit();
	/* END OF IMPORTER CODE */

	class_destroy(hl_class);
	unregister_chrdev_region(MKDEV(hl_major, 0), HL_MAX_MINORS);

	idr_destroy(&hl_devs_idr);

	pr_debug("driver removed\n");
}

module_init(hl_init);
module_exit(hl_exit);
