// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2024 HabanaLabs, Ltd.
 * Copyright (C) 2024-2025, Intel Corporation.
 * All Rights Reserved.
 */

#define pr_fmt(fmt)	"habanalabs: " fmt

#include <uapi/drm/habanalabs_accel.h>
#include "habanalabs.h"
#include "habanalabs_compat_accel.h"

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <asm/msr.h>

/* make sure there is space for all the signed info */
static_assert(sizeof(struct cpucp_info) <= SEC_DEV_INFO_BUF_SZ);

#define MAX_SCHEDULER_BUF_SIZE	SZ_4K

static u32 hl_debug_struct_size[HL_DEBUG_OP_FETCH_TRACE + 1] = {
	[HL_DEBUG_OP_ETR] = sizeof(struct hl_debug_params_etr),
	[HL_DEBUG_OP_ETF] = sizeof(struct hl_debug_params_etf),
	[HL_DEBUG_OP_STM] = sizeof(struct hl_debug_params_stm),
	[HL_DEBUG_OP_FUNNEL] = 0,
	[HL_DEBUG_OP_BMON] = sizeof(struct hl_debug_params_bmon),
	[HL_DEBUG_OP_SPMU] = sizeof(struct hl_debug_params_spmu),
	[HL_DEBUG_OP_TIMESTAMP] = 0,
	[HL_DEBUG_OP_FETCH_TRACE] = sizeof(struct hl_debug_params_fetch_trace)
};

static u32 hl_nic_input_size[HL_NIC_OP_DUMP_QP + 1] = {
	[HL_NIC_OP_ALLOC_CONN] = sizeof(struct hl_nic_alloc_conn_in),
	[HL_NIC_OP_SET_REQ_CONN_CTX] = sizeof(struct hl_nic_req_conn_ctx_in),
	[HL_NIC_OP_SET_RES_CONN_CTX] = sizeof(struct hl_nic_res_conn_ctx_in),
	[HL_NIC_OP_DESTROY_CONN] = sizeof(struct hl_nic_destroy_conn_in),
	[HL_NIC_OP_USER_WQ_SET] = sizeof(struct hl_nic_user_wq_arr_set_in),
	[HL_NIC_OP_USER_WQ_UNSET] = sizeof(struct hl_nic_user_wq_arr_unset_in),
	[HL_NIC_OP_USER_CQ_SET] = sizeof(struct hl_nic_user_cq_set_in),
	[HL_NIC_OP_USER_CQ_UNSET] = sizeof(struct hl_nic_user_cq_unset_in),
	[HL_NIC_OP_USER_CQ_UPDATE_CI] =
				sizeof(struct hl_nic_user_cq_update_ci_in),
	[HL_NIC_OP_ALLOC_USER_CQ_ID] = sizeof(struct hl_nic_alloc_user_cq_id_in),
	[HL_NIC_OP_SET_USER_APP_PARAMS] =
				sizeof(struct hl_nic_set_user_app_params_in),
	[HL_NIC_OP_GET_USER_APP_PARAMS] =
				sizeof(struct hl_nic_get_user_app_params_in),
	[HL_NIC_OP_ALLOC_USER_DB_FIFO] =
				sizeof(struct hl_nic_alloc_user_db_fifo_in),
	[HL_NIC_OP_USER_DB_FIFO_SET] =
				sizeof(struct hl_nic_user_db_fifo_set_in),
	[HL_NIC_OP_USER_DB_FIFO_UNSET] =
				sizeof(struct hl_nic_user_db_fifo_unset_in),
	[HL_NIC_OP_EQ_POLL] = sizeof(struct hl_nic_eq_poll_in),
	[HL_NIC_OP_USER_ENCAP_ALLOC] =
				sizeof(struct hl_nic_user_encap_alloc_in),
	[HL_NIC_OP_USER_ENCAP_SET] =
				sizeof(struct hl_nic_user_encap_set_in),
	[HL_NIC_OP_USER_ENCAP_UNSET] =
				sizeof(struct hl_nic_user_encap_unset_in),
	[HL_NIC_OP_USER_CCQ_SET] = sizeof(struct hl_nic_user_ccq_set_in),
	[HL_NIC_OP_USER_CCQ_UNSET] = sizeof(struct hl_nic_user_ccq_unset_in),
	[HL_NIC_OP_USER_CQ_ID_SET] = sizeof(struct hl_nic_user_cq_id_set_in),
	[HL_NIC_OP_USER_CQ_ID_UNSET] = sizeof(struct hl_nic_user_cq_id_unset_in),
	[HL_NIC_OP_ALLOC_COLL_CONN] = sizeof(struct hl_nic_alloc_coll_conn_in),
	[HL_NIC_OP_DUMP_QP] = sizeof(struct hl_nic_dump_qp_in),
};

static u32 hl_nic_output_size[HL_NIC_OP_DUMP_QP + 1] = {
	[HL_NIC_OP_ALLOC_CONN] = sizeof(struct hl_nic_alloc_conn_out),
	[HL_NIC_OP_SET_REQ_CONN_CTX] = sizeof(struct hl_nic_req_conn_ctx_out),
	[HL_NIC_OP_SET_RES_CONN_CTX] = 0,
	[HL_NIC_OP_DESTROY_CONN] = 0,
	[HL_NIC_OP_USER_WQ_SET] = sizeof(struct hl_nic_user_wq_arr_set_out),
	[HL_NIC_OP_USER_WQ_UNSET] = 0,
	[HL_NIC_OP_USER_CQ_SET] = 0,
	[HL_NIC_OP_USER_CQ_UNSET] = 0,
	[HL_NIC_OP_USER_CQ_UPDATE_CI] = 0,
	[HL_NIC_OP_ALLOC_USER_CQ_ID] = sizeof(struct hl_nic_alloc_user_cq_id_out),
	[HL_NIC_OP_SET_USER_APP_PARAMS] = 0,
	[HL_NIC_OP_GET_USER_APP_PARAMS] =
				sizeof(struct hl_nic_get_user_app_params_out),
	[HL_NIC_OP_ALLOC_USER_DB_FIFO] =
				sizeof(struct hl_nic_alloc_user_db_fifo_out),
	[HL_NIC_OP_USER_DB_FIFO_SET] =
				sizeof(struct hl_nic_user_db_fifo_set_out),
	[HL_NIC_OP_USER_DB_FIFO_UNSET] = 0,
	[HL_NIC_OP_EQ_POLL] = sizeof(struct hl_nic_eq_poll_out),
	[HL_NIC_OP_USER_ENCAP_ALLOC] =
				sizeof(struct hl_nic_user_encap_alloc_out),
	[HL_NIC_OP_USER_ENCAP_SET] = 0,
	[HL_NIC_OP_USER_ENCAP_UNSET] = 0,
	[HL_NIC_OP_USER_CCQ_SET] = sizeof(struct hl_nic_user_ccq_set_out),
	[HL_NIC_OP_USER_CCQ_UNSET] = 0,
	[HL_NIC_OP_USER_CQ_ID_SET] = sizeof(struct hl_nic_user_cq_id_set_out),
	[HL_NIC_OP_USER_CQ_ID_UNSET] = 0,
	[HL_NIC_OP_ALLOC_COLL_CONN] = sizeof(struct hl_nic_alloc_coll_conn_out),
	[HL_NIC_OP_DUMP_QP] = 0,
};

static int device_status_info(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_device_status dev_stat = {0};
	u32 size = min_t(u32, args->return_size, sizeof(dev_stat));
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!size) || (!out))
		return -EINVAL;

	if (copy_from_user(&dev_stat, u64_to_user_ptr(args->return_pointer), size))
		return -EFAULT;

	if (dev_stat.soft_reset_stall)
		hdev->soft_reset_stall_timestamp =
			ktime_add_ms(ktime_get(), HL_SOFT_RESET_STALL_AFTER_POLLING_MS);
	else
		hdev->soft_reset_stall_timestamp = 0;

	dev_stat.status = hl_device_status(hdev);

	return copy_to_user(out, &dev_stat, size) ? -EFAULT : 0;
}

static int hw_ip_info(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_hw_ip_info hw_ip = {0};
	u32 size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 sram_kmd_size, dram_kmd_size, dram_available_size;

	if ((!size) || (!out))
		return -EINVAL;

	sram_kmd_size = (prop->sram_user_base_address -
				prop->sram_base_address);
	dram_kmd_size = (prop->dram_user_base_address -
				prop->dram_base_address);

	hw_ip.device_id = prop->pci_id;
	hw_ip.sram_base_address = prop->sram_user_base_address;
	hw_ip.dram_base_address =
			prop->dram_supports_virtual_memory ?
			prop->dmmu.start_addr : prop->dram_user_base_address;
	hw_ip.tpc_enabled_mask = prop->tpc_enabled_mask & 0xFF;
	hw_ip.tpc_enabled_mask_ext = prop->tpc_enabled_mask;

	if (hdev->cache_enable)
		hw_ip.sram_size = 0;
	else
		hw_ip.sram_size = prop->sram_size - sram_kmd_size;

	/*
	 * dram size can't be smaller than minimal requirement.
	 * Such a bug could appear, for example, when the user chooses dram_size
	 * such as when running a simulator.
	 */
	if (prop->dram_size < dram_kmd_size)
		dram_available_size = 0;
	else
		dram_available_size = prop->dram_size - dram_kmd_size;

	if (hdev->dram_enable)
		hw_ip.dram_size = DIV_ROUND_DOWN_ULL(dram_available_size, prop->dram_page_size) *
					prop->dram_page_size;
	else
		hw_ip.dram_size = dram_available_size;

	if (hw_ip.dram_size > PAGE_SIZE)
		hw_ip.dram_enabled = 1;

	hw_ip.dram_page_size = prop->dram_page_size;
	hw_ip.device_mem_alloc_default_page_size = prop->device_mem_alloc_default_page_size;
	hw_ip.num_of_events = prop->num_of_events;

	memcpy(hw_ip.cpucp_version, prop->cpucp_info.cpucp_version,
		min(VERSION_MAX_LEN, HL_INFO_VERSION_MAX_LEN));

	memcpy(hw_ip.card_name, prop->cpucp_info.card_name,
		min(CARD_NAME_MAX_LEN, HL_INFO_CARD_NAME_MAX_LEN));

	hw_ip.cpld_version = le32_to_cpu(prop->cpucp_info.cpld_version);
	hw_ip.module_id = hl_get_module_id(hdev);
	hw_ip.interposer_version = prop->cpucp_info.interposer_version;
	hw_ip.substrate_version = prop->cpucp_info.substrate_version;

	hw_ip.psoc_pci_pll_nr = prop->psoc_pci_pll_nr;
	hw_ip.psoc_pci_pll_nf = prop->psoc_pci_pll_nf;
	hw_ip.psoc_pci_pll_od = prop->psoc_pci_pll_od;
	hw_ip.psoc_pci_pll_div_factor = prop->psoc_pci_pll_div_factor;

	hw_ip.decoder_enabled_mask = prop->decoder_enabled_mask;
	hw_ip.mme_master_slave_mode = prop->mme_master_slave_mode;
	hw_ip.first_available_interrupt_id = prop->first_available_user_interrupt;
	hw_ip.number_of_user_interrupts = prop->user_interrupt_count;
	hw_ip.tpc_interrupt_id = prop->tpc_interrupt_id;

	hw_ip.edma_enabled_mask = prop->edma_enabled_mask;
	hw_ip.pdma_user_owned_ch_mask = prop->pdma_user_owned_ch_mask;
	hw_ip.server_type = prop->server_type;

	hw_ip.nic_ports_mask = hdev->cn.ports_mask;
	hw_ip.nic_ports_external_mask = hdev->cn.eth_ports_mask;
	hw_ip.security_enabled = prop->fw_security_enabled;
	hw_ip.mme_enabled_mask = hdev->mme_mask;
	hw_ip.odp_supported = hl_is_odp_supported(hdev);
	hw_ip.revision_id = hdev->pci_revision_id;
	hw_ip.rotator_enabled_mask = prop->rotator_enabled_mask;
	hw_ip.sched_arc_enabled_mask = hdev->sched_arc_mask;
	hw_ip.engine_core_interrupt_reg_addr = prop->engine_core_interrupt_reg_addr;
	hw_ip.reserved_dram_size = dram_kmd_size;

	return copy_to_user(out, &hw_ip,
		min((size_t) size, sizeof(hw_ip))) ? -EFAULT : 0;
}

static int hw_events_info(struct hl_device *hdev, bool aggregate,
			struct hl_info_args *args)
{
	u32 size, max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	void *arr;

	if ((!max_size) || (!out))
		return -EINVAL;

	arr = hdev->asic_funcs->get_events_stat(hdev, aggregate, &size);
	if (!arr) {
		hl_err(hdev, "Events info not supported\n");
		return -EOPNOTSUPP;
	}

	return copy_to_user(out, arr, min(max_size, size)) ? -EFAULT : 0;
}

static int events_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	u32 max_size = args->return_size;
	u64 events_mask;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((max_size < sizeof(u64)) || (!out))
		return -EINVAL;

	mutex_lock(&hpriv->notifier_event.lock);
	events_mask = hpriv->notifier_event.events_mask;
	hpriv->notifier_event.events_mask = 0;
	mutex_unlock(&hpriv->notifier_event.lock);

	return copy_to_user(out, &events_mask, sizeof(u64)) ? -EFAULT : 0;
}

static int dram_usage_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_info_dram_usage dram_usage = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 dram_kmd_size;

	if ((!max_size) || (!out))
		return -EINVAL;

	dram_kmd_size = (prop->dram_user_base_address -
				prop->dram_base_address);
	dram_usage.dram_free_mem = (prop->dram_size - dram_kmd_size) -
					atomic64_read(&hdev->dram_used_mem);
	if (hpriv->ctx)
		dram_usage.ctx_dram_mem =
			atomic64_read(&hpriv->ctx->dram_phys_mem);

	return copy_to_user(out, &dram_usage,
		min((size_t) max_size, sizeof(dram_usage))) ? -EFAULT : 0;
}

static int hw_idle(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_hw_idle hw_idle = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	hw_idle.is_idle = hdev->asic_funcs->is_device_idle(hdev,
					hw_idle.busy_engines_mask_ext,
					HL_BUSY_ENGINES_MASK_EXT_SIZE, NULL);
	hw_idle.busy_engines_mask =
			lower_32_bits(hw_idle.busy_engines_mask_ext[0]);

	return copy_to_user(out, &hw_idle,
		min((size_t) max_size, sizeof(hw_idle))) ? -EFAULT : 0;
}

static int debug_sched_ioctl(struct hl_device *hdev, struct hl_ctx *ctx,
				struct hl_debug_args *args)
{
	struct hl_debug_params_scheduler sched_args;
	int rc = -EINVAL;
	void *buf;

	if (args->input_size != sizeof(struct hl_debug_params_scheduler))
		return -EINVAL;

	if (copy_from_user(&sched_args, u64_to_user_ptr(args->input_ptr), args->input_size))
		return -EFAULT;

	switch (args->op) {
	case HL_DEBUG_OP_SCHED_SUBMIT_BUF:
		if (sched_args.size <= 0 || sched_args.size > MAX_SCHEDULER_BUF_SIZE)
			return -EINVAL;
		if (sched_args.size & 3) /* Size must be a multiple of 4 */
			return -EINVAL;
		buf = kmalloc(sched_args.size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
		if (copy_from_user(buf, u64_to_user_ptr(sched_args.buffer), sched_args.size)) {
			kfree(buf);
			return -EFAULT;
		}
		rc = hdev->asic_funcs->scheduler_submit_buf(hdev, sched_args.cpu_id,
								sched_args.queue_id, buf,
								sched_args.size);
		kfree(buf);
		break;
	default:
		return -EINVAL;
	}

	return rc;
}

static int debug_read_dev_mem_block(struct hl_fpriv *hpriv, struct hl_debug_args *args)
{
	struct hl_debug_params_read_block read_args;
	struct hl_device *hdev = hpriv->hdev;
	void __user *out_buf;
	int rc = 0;
	u32 size, *buf;

	if (args->input_size != sizeof(read_args))
		return -EINVAL;

	if (copy_from_user(&read_args, u64_to_user_ptr(args->input_ptr), args->input_size))
		return -EFAULT;

	if (!read_args.size || read_args.size > HL_DEBUG_MAX_READ_BLOCK_SIZE) {
		hl_err(hdev, "Read debug device memory invalid size %d\n", read_args.size);
		return -EINVAL;
	}

	size = read_args.size;
	out_buf = (void __user *) (uintptr_t) read_args.user_address;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rc = hl_read_memory_block(hdev, buf, read_args.cfg_address, size);
	if (rc) {
		hl_err(hdev, "Read debug device memory failed address: %#llx size: %u\n",
					read_args.cfg_address, size);
		goto out;
	}

	if (copy_to_user(out_buf, buf, size))
		rc = -EFAULT;

out:
	kfree(buf);
	return rc;
}

static int debug_coresight(struct hl_device *hdev, struct hl_ctx *ctx, struct hl_debug_args *args)
{
	struct hl_debug_params *params;
	void *input = NULL, *output = NULL;
	int rc;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	params->reg_idx = args->reg_idx;
	params->enable = args->enable;
	params->op = args->op;

	if (args->input_ptr && args->input_size) {
		input = kzalloc(hl_debug_struct_size[args->op], GFP_KERNEL);
		if (!input) {
			rc = -ENOMEM;
			goto out;
		}

		if (copy_from_user(input, u64_to_user_ptr(args->input_ptr),
					args->input_size)) {
			rc = -EFAULT;
			hl_err(hdev, "failed to copy input debug data\n");
			goto out;
		}

		params->input = input;
	}

	if (args->output_ptr && args->output_size) {
		output = kzalloc(args->output_size, GFP_KERNEL);
		if (!output) {
			rc = -ENOMEM;
			goto out;
		}

		params->output = output;
		params->output_size = args->output_size;
	}

	rc = hdev->asic_funcs->debug_coresight(hdev, ctx, params);
	if (rc) {
		hl_err(hdev,
			"debug coresight operation failed %d\n", rc);
		goto out;
	}

	if (output && copy_to_user((void __user *) (uintptr_t) args->output_ptr,
					output, args->output_size)) {
		hl_err(hdev, "copy to user failed in debug ioctl\n");
		rc = -EFAULT;
		goto out;
	}


out:
	kfree(params);
	kfree(output);
	kfree(input);

	return rc;
}

static int device_utilization(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_device_utilization device_util = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_device_utilization(hdev, &device_util.utilization);
	if (rc)
		return -EINVAL;

	return copy_to_user(out, &device_util,
		min((size_t) max_size, sizeof(device_util))) ? -EFAULT : 0;
}

static int get_clk_rate(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_clk_rate clk_rate = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_get_clk_rate(hdev, &clk_rate.cur_clk_rate_mhz, &clk_rate.max_clk_rate_mhz);
	if (rc)
		return rc;

	return copy_to_user(out, &clk_rate, min_t(size_t, max_size, sizeof(clk_rate)))
										? -EFAULT : 0;
}

static int get_reset_count(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_reset_count reset_count = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	reset_count.hard_reset_cnt = hdev->reset_info.hard_reset_cnt;
	reset_count.soft_reset_cnt = hdev->reset_info.compute_reset_cnt;

	return copy_to_user(out, &reset_count,
		min((size_t) max_size, sizeof(reset_count))) ? -EFAULT : 0;
}

static int time_sync_info(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_time_sync time_sync = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	time_sync.device_time = hdev->asic_funcs->get_device_time(hdev, 0);
	time_sync.host_time = ktime_get_raw_ns();
	time_sync.tsc_time = rdtsc();

	return copy_to_user(out, &time_sync,
		min((size_t) max_size, sizeof(time_sync))) ? -EFAULT : 0;
}

static int time_sync_info_per_die(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_time_sync_per_die ts_info = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;
	u8 num_of_dies;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = copy_from_user(&ts_info, out, min_t(size_t, max_size, sizeof(ts_info)));
	if (rc)
		return -EFAULT;

	num_of_dies = hdev->asic_prop.num_of_dies != 0x0 ? hdev->asic_prop.num_of_dies : 0x1;
	if (ts_info.die_index >= num_of_dies)
		return -EINVAL;

	ts_info.pad = 0x0;
	ts_info.device_time = hdev->asic_funcs->get_device_time(hdev, ts_info.die_index);
	ts_info.host_time = ktime_get_raw_ns();
	ts_info.tsc_time = rdtsc();

	return copy_to_user(out, &ts_info,
		min((size_t) max_size, sizeof(ts_info))) ? -EFAULT : 0;
}

static int pci_counters_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_info_pci_counters pci_counters = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_cpucp_pci_counters_get(hdev, &pci_counters);
	if (rc)
		return rc;

	return copy_to_user(out, &pci_counters,
		min((size_t) max_size, sizeof(pci_counters))) ? -EFAULT : 0;
}

static int clk_throttle_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_device *hdev = hpriv->hdev;
	struct hl_info_clk_throttle clk_throttle = {0};
	ktime_t end_time, zero_time = ktime_set(0, 0);
	u32 max_size = args->return_size;
	int i;

	if ((!max_size) || (!out))
		return -EINVAL;

	mutex_lock(&hdev->clk_throttling.lock);

	clk_throttle.clk_throttling_reason = hdev->clk_throttling.current_reason;

	for (i = 0 ; i < HL_CLK_THROTTLE_TYPE_MAX ; i++) {
		if (!(hdev->clk_throttling.aggregated_reason & BIT(i)))
			continue;

		clk_throttle.clk_throttling_timestamp_us[i] =
			ktime_to_us(hdev->clk_throttling.timestamp[i].start);

		if (ktime_compare(hdev->clk_throttling.timestamp[i].end, zero_time))
			end_time = hdev->clk_throttling.timestamp[i].end;
		else
			end_time = ktime_get();

		clk_throttle.clk_throttling_duration_ns[i] =
			ktime_to_ns(ktime_sub(end_time,
				hdev->clk_throttling.timestamp[i].start));

	}
	mutex_unlock(&hdev->clk_throttling.lock);

	return copy_to_user(out, &clk_throttle,
		min((size_t) max_size, sizeof(clk_throttle))) ? -EFAULT : 0;
}

static int cs_counters_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_info_cs_counters cs_counters = {0};
	struct hl_device *hdev = hpriv->hdev;
	struct hl_cs_counters_atomic *cntr;
	u32 max_size = args->return_size;

	cntr = &hdev->aggregated_cs_counters;

	if ((!max_size) || (!out))
		return -EINVAL;

	cs_counters.total_out_of_mem_drop_cnt =
			atomic64_read(&cntr->out_of_mem_drop_cnt);
	cs_counters.total_parsing_drop_cnt =
			atomic64_read(&cntr->parsing_drop_cnt);
	cs_counters.total_queue_full_drop_cnt =
			atomic64_read(&cntr->queue_full_drop_cnt);
	cs_counters.total_device_in_reset_drop_cnt =
			atomic64_read(&cntr->device_in_reset_drop_cnt);
	cs_counters.total_max_cs_in_flight_drop_cnt =
			atomic64_read(&cntr->max_cs_in_flight_drop_cnt);
	cs_counters.total_validation_drop_cnt =
			atomic64_read(&cntr->validation_drop_cnt);

	if (hpriv->ctx) {
		cs_counters.ctx_out_of_mem_drop_cnt =
				atomic64_read(
				&hpriv->ctx->cs_counters.out_of_mem_drop_cnt);
		cs_counters.ctx_parsing_drop_cnt =
				atomic64_read(
				&hpriv->ctx->cs_counters.parsing_drop_cnt);
		cs_counters.ctx_queue_full_drop_cnt =
				atomic64_read(
				&hpriv->ctx->cs_counters.queue_full_drop_cnt);
		cs_counters.ctx_device_in_reset_drop_cnt =
				atomic64_read(
			&hpriv->ctx->cs_counters.device_in_reset_drop_cnt);
		cs_counters.ctx_max_cs_in_flight_drop_cnt =
				atomic64_read(
			&hpriv->ctx->cs_counters.max_cs_in_flight_drop_cnt);
		cs_counters.ctx_validation_drop_cnt =
				atomic64_read(
				&hpriv->ctx->cs_counters.validation_drop_cnt);
	}

	return copy_to_user(out, &cs_counters,
		min((size_t) max_size, sizeof(cs_counters))) ? -EFAULT : 0;
}

static int sync_manager_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_info_sync_manager sm_info = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	if (args->dcore_id >= HL_MAX_DCORES)
		return -EINVAL;

	sm_info.first_available_sync_object =
			prop->first_available_user_sob[args->dcore_id];
	sm_info.first_available_monitor =
			prop->first_available_user_mon[args->dcore_id];
	sm_info.first_available_cq =
			prop->first_available_cq[args->dcore_id];

	return copy_to_user(out, &sm_info, min_t(size_t, (size_t) max_size,
			sizeof(sm_info))) ? -EFAULT : 0;
}

static int total_energy_consumption_info(struct hl_fpriv *hpriv,
			struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_info_energy total_energy = {0};
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_cpucp_total_energy_get(hdev,
			&total_energy.total_energy_consumption);
	if (rc)
		return rc;

	return copy_to_user(out, &total_energy,
		min((size_t) max_size, sizeof(total_energy))) ? -EFAULT : 0;
}

static int pll_frequency_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	struct hl_pll_frequency_info freq_info = { {0} };
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hdev->asic_funcs->pll_info_get(hdev, args->pll_index, freq_info.output);
	if (rc)
		return rc;

	return copy_to_user(out, &freq_info,
		min((size_t) max_size, sizeof(freq_info))) ? -EFAULT : 0;
}

static int power_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct hl_power_info power_info = {0};
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_cpucp_power_get(hdev, &power_info.power);
	if (rc)
		return rc;

	return copy_to_user(out, &power_info,
		min((size_t) max_size, sizeof(power_info))) ? -EFAULT : 0;
}

static int open_stats_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct hl_open_stats_info open_stats_info = {0};
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	open_stats_info.last_open_period_ms = jiffies64_to_msecs(
		hdev->last_open_session_duration_jif);
	open_stats_info.open_counter = hdev->open_counter;
	open_stats_info.is_compute_ctx_active = hdev->is_compute_ctx_active;
	open_stats_info.compute_ctx_in_release = hdev->compute_ctx_in_release;
	open_stats_info.compute_ctx_has_mapped_resources =
		atomic_read(&hdev->mapped_resource_cnt) > 0;

	return copy_to_user(out, &open_stats_info,
		min((size_t) max_size, sizeof(open_stats_info))) ? -EFAULT : 0;
}

static int cn_link_state_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct hl_info_habana_link_state link_state_info = {};
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_cn_get_port_status(hdev, args->habana_link_id, &link_state_info);
	if (rc)
		return rc;

	return copy_to_user(out, &link_state_info,
		min_t(size_t, max_size, sizeof(link_state_info))) ? -EFAULT : 0;
}

static int cn_statistics(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_info_habana_link_counters stat = {};
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = copy_from_user(&stat, out, min_t(size_t, max_size, sizeof(stat)));
	if (rc)
		return -EFAULT;

	rc = hl_cn_dump_port_statistics(hdev, args->habana_link_id, stat.str_buf_ptr,
					stat.val_buf_ptr, &stat.num_of_stat);
	if (rc)
		return rc;

	return copy_to_user(out, &stat, min_t(size_t, max_size, sizeof(stat))) ? -EFAULT : 0;
}

static int dram_pending_rows_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	u32 pend_rows_num = 0;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_dram_pending_row_get(hdev, &pend_rows_num);
	if (rc)
		return rc;

	return copy_to_user(out, &pend_rows_num,
			min_t(size_t, max_size, sizeof(pend_rows_num))) ? -EFAULT : 0;
}

static int dram_replaced_rows_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct cpucp_hbm_row_info info = {0};
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	rc = hl_fw_dram_replaced_row_get(hdev, &info);
	if (rc)
		return rc;

	return copy_to_user(out, &info, min_t(size_t, max_size, sizeof(info))) ? -EFAULT : 0;
}

static int last_err_open_dev_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_info_last_err_open_dev_time info = {0};
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	info.timestamp = ktime_to_ns(hdev->last_successful_open_ktime);

	return copy_to_user(out, &info, min_t(size_t, max_size, sizeof(info))) ? -EFAULT : 0;
}

static int cs_timeout_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_info_cs_timeout_event info = {0};
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	info.seq = hdev->captured_err_info.cs_timeout.seq;
	info.timestamp = ktime_to_ns(hdev->captured_err_info.cs_timeout.timestamp);

	return copy_to_user(out, &info, min_t(size_t, max_size, sizeof(info))) ? -EFAULT : 0;
}

static int razwi_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct razwi_info *razwi_info;

	if ((!max_size) || (!out))
		return -EINVAL;

	razwi_info = &hdev->captured_err_info.razwi_info;
	if (!razwi_info->razwi_info_available)
		return 0;

	return copy_to_user(out, &razwi_info->razwi,
			min_t(size_t, max_size, sizeof(struct hl_info_razwi_event))) ? -EFAULT : 0;
}

static int undefined_opcode_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct hl_info_undefined_opcode_event info = {0};
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;

	if ((!max_size) || (!out))
		return -EINVAL;

	info.timestamp = ktime_to_ns(hdev->captured_err_info.undef_opcode.timestamp);
	info.engine_id = hdev->captured_err_info.undef_opcode.engine_id;
	info.cq_addr = hdev->captured_err_info.undef_opcode.cq_addr;
	info.cq_size = hdev->captured_err_info.undef_opcode.cq_size;
	info.stream_id = hdev->captured_err_info.undef_opcode.stream_id;
	info.cb_addr_streams_len = hdev->captured_err_info.undef_opcode.cb_addr_streams_len;
	memcpy(info.cb_addr_streams, hdev->captured_err_info.undef_opcode.cb_addr_streams,
			sizeof(info.cb_addr_streams));

	return copy_to_user(out, &info, min_t(size_t, max_size, sizeof(info))) ? -EFAULT : 0;
}

static int dev_mem_alloc_page_sizes_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_info_dev_memalloc_page_sizes info = {0};
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;

	if ((!max_size) || (!out))
		return -EINVAL;

	/*
	 * Future ASICs that will support multiple DRAM page sizes will support only "powers of 2"
	 * pages (unlike some of the ASICs before supporting multiple page sizes).
	 * For this reason for all ASICs that not support multiple page size the function will
	 * return an empty bitmask indicating that multiple page sizes is not supported.
	 */
	info.page_order_bitmask = hdev->asic_prop.dmmu.supported_pages_mask;

	return copy_to_user(out, &info, min_t(size_t, max_size, sizeof(info))) ? -EFAULT : 0;
}

static int sec_attest_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct cpucp_sec_attest_info *sec_attest_info;
	struct hl_info_sec_attest *info;
	u32 max_size = args->return_size;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	sec_attest_info = kmalloc(sizeof(*sec_attest_info), GFP_KERNEL);
	if (!sec_attest_info)
		return -ENOMEM;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		rc = -ENOMEM;
		goto free_sec_attest_info;
	}

	rc = hl_fw_get_sec_attest_info(hpriv->hdev, sec_attest_info, args->sec_attest_nonce);
	if (rc)
		goto free_info;

	info->nonce = le32_to_cpu(sec_attest_info->nonce);
	info->pcr_quote_len = le16_to_cpu(sec_attest_info->pcr_quote_len);
	info->pub_data_len = le16_to_cpu(sec_attest_info->pub_data_len);
	info->certificate_len = le16_to_cpu(sec_attest_info->certificate_len);
	info->pcr_num_reg = sec_attest_info->pcr_num_reg;
	info->pcr_reg_len = sec_attest_info->pcr_reg_len;
	info->quote_sig_len = sec_attest_info->quote_sig_len;
	memcpy(&info->pcr_data, &sec_attest_info->pcr_data, sizeof(info->pcr_data));
	memcpy(&info->pcr_quote, &sec_attest_info->pcr_quote, sizeof(info->pcr_quote));
	memcpy(&info->public_data, &sec_attest_info->public_data, sizeof(info->public_data));
	memcpy(&info->certificate, &sec_attest_info->certificate, sizeof(info->certificate));
	memcpy(&info->quote_sig, &sec_attest_info->quote_sig, sizeof(info->quote_sig));

	rc = copy_to_user(out, info,
				min_t(size_t, max_size, sizeof(*info))) ? -EFAULT : 0;

free_info:
	kfree(info);
free_sec_attest_info:
	kfree(sec_attest_info);

	return rc;
}

static int dev_info_signed(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct cpucp_dev_info_signed *dev_info_signed;
	struct hl_info_signed *info;
	u32 max_size = args->return_size;
	int rc;

	if ((!max_size) || (!out))
		return -EINVAL;

	dev_info_signed = kzalloc(sizeof(*dev_info_signed), GFP_KERNEL);
	if (!dev_info_signed)
		return -ENOMEM;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		rc = -ENOMEM;
		goto free_dev_info_signed;
	}

	rc = hl_fw_get_dev_info_signed(hpriv->hdev,
					dev_info_signed, args->sec_attest_nonce);
	if (rc)
		goto free_info;

	info->nonce = le32_to_cpu(dev_info_signed->nonce);
	info->info_sig_len = dev_info_signed->info_sig_len;
	info->pub_data_len = le16_to_cpu(dev_info_signed->pub_data_len);
	info->certificate_len = le16_to_cpu(dev_info_signed->certificate_len);
	info->dev_info_len = sizeof(struct cpucp_info);
	memcpy(&info->info_sig, &dev_info_signed->info_sig, sizeof(info->info_sig));
	memcpy(&info->public_data, &dev_info_signed->public_data, sizeof(info->public_data));
	memcpy(&info->certificate, &dev_info_signed->certificate, sizeof(info->certificate));
	memcpy(&info->dev_info, &dev_info_signed->info, info->dev_info_len);

	rc = copy_to_user(out, info, min_t(size_t, max_size, sizeof(*info))) ? -EFAULT : 0;

free_info:
	kfree(info);
free_dev_info_signed:
	kfree(dev_info_signed);

	return rc;
}


static int eventfd_register(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	int rc;

	/* check if there is already a registered on that process */
	mutex_lock(&hpriv->notifier_event.lock);
	if (hpriv->notifier_event.eventfd) {
		mutex_unlock(&hpriv->notifier_event.lock);
		return -EINVAL;
	}

	hpriv->notifier_event.eventfd = eventfd_ctx_fdget(args->eventfd);
	if (IS_ERR(hpriv->notifier_event.eventfd)) {
		rc = PTR_ERR(hpriv->notifier_event.eventfd);
		hpriv->notifier_event.eventfd = NULL;
		mutex_unlock(&hpriv->notifier_event.lock);
		return rc;
	}

	mutex_unlock(&hpriv->notifier_event.lock);
	return 0;
}

static int eventfd_unregister(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	mutex_lock(&hpriv->notifier_event.lock);
	if (!hpriv->notifier_event.eventfd) {
		mutex_unlock(&hpriv->notifier_event.lock);
		return -EINVAL;
	}

	eventfd_ctx_put(hpriv->notifier_event.eventfd);
	hpriv->notifier_event.eventfd = NULL;
	mutex_unlock(&hpriv->notifier_event.lock);
	return 0;
}

static int engine_status_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	u32 status_buf_size = args->return_size;
	struct hl_device *hdev = hpriv->hdev;
	struct engines_data eng_data;
	int rc;

	if ((status_buf_size < SZ_1K) || (status_buf_size > HL_ENGINES_DATA_MAX_SIZE) || (!out))
		return -EINVAL;

	eng_data.actual_size = 0;
	eng_data.allocated_buf_size = status_buf_size;
	eng_data.buf = vmalloc(status_buf_size);
	if (!eng_data.buf)
		return -ENOMEM;

	hdev->asic_funcs->is_device_idle(hdev, NULL, 0, &eng_data);

	if (eng_data.actual_size > eng_data.allocated_buf_size) {
		hl_err(hdev,
			"Engines data size (%d Bytes) is bigger than allocated size (%u Bytes)\n",
			eng_data.actual_size, status_buf_size);
		vfree(eng_data.buf);
		return -ENOMEM;
	}

	args->user_buffer_actual_size = eng_data.actual_size;
	rc = copy_to_user(out, eng_data.buf, min_t(size_t, status_buf_size, eng_data.actual_size)) ?
				-EFAULT : 0;

	vfree(eng_data.buf);

	return rc;
}

static int page_fault_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_device *hdev = hpriv->hdev;
	u32 max_size = args->return_size;
	struct page_fault_info *pgf_info;

	if ((!max_size) || (!out))
		return -EINVAL;

	pgf_info = &hdev->captured_err_info.page_fault_info;
	if (!pgf_info->page_fault_info_available)
		return 0;

	return copy_to_user(out, &pgf_info->page_fault,
			min_t(size_t, max_size, sizeof(struct hl_page_fault_info))) ? -EFAULT : 0;
}

static int user_mappings_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	u32 user_buf_size = args->return_size;
	struct hl_device *hdev = hpriv->hdev;
	struct page_fault_info *pgf_info;
	u64 actual_size;

	if (!out)
		return -EINVAL;

	pgf_info = &hdev->captured_err_info.page_fault_info;
	if (!pgf_info->page_fault_info_available)
		return 0;

	args->array_size = pgf_info->num_of_user_mappings;

	actual_size = pgf_info->num_of_user_mappings * sizeof(struct hl_user_mapping);
	if (user_buf_size < actual_size)
		return -ENOMEM;

	return copy_to_user(out, pgf_info->user_mappings, actual_size) ? -EFAULT : 0;
}

static int hw_err_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *user_buf = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_device *hdev = hpriv->hdev;
	u32 user_buf_size = args->return_size;
	struct hw_err_info *info;
	int rc;

	if (!user_buf)
		return -EINVAL;

	info = &hdev->captured_err_info.hw_err;
	if (!info->event_info_available)
		return 0;

	if (user_buf_size < sizeof(struct hl_info_hw_err_event))
		return -ENOMEM;

	rc = copy_to_user(user_buf, &info->event, sizeof(struct hl_info_hw_err_event));
	return rc ? -EFAULT : 0;
}

static int fw_err_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *user_buf = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_device *hdev = hpriv->hdev;
	u32 user_buf_size = args->return_size;
	struct fw_err_info *info;
	int rc;

	if (!user_buf)
		return -EINVAL;

	info = &hdev->captured_err_info.fw_err;
	if (!info->event_info_available)
		return 0;

	if (user_buf_size < sizeof(struct hl_info_fw_err_event))
		return -ENOMEM;

	rc = copy_to_user(user_buf, &info->event, sizeof(struct hl_info_fw_err_event));
	return rc ? -EFAULT : 0;
}

static int engine_err_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *user_buf = (void __user *) (uintptr_t) args->return_pointer;
	struct hl_device *hdev = hpriv->hdev;
	u32 user_buf_size = args->return_size;
	struct engine_err_info *info;
	int rc;

	if (!user_buf)
		return -EINVAL;

	info = &hdev->captured_err_info.engine_err;
	if (!info->event_info_available)
		return 0;

	if (user_buf_size < sizeof(struct hl_info_engine_err_event))
		return -ENOMEM;

	rc = copy_to_user(user_buf, &info->event, sizeof(struct hl_info_engine_err_event));
	return rc ? -EFAULT : 0;
}

static int module_params_info(struct hl_device *hdev, struct hl_info_args *args)
{
	struct hl_info_module_params *module_params;
	u32 max_size = args->return_size;
	void __user *out = (void __user *) (uintptr_t) args->return_pointer;
	int rc;

	if (!max_size || !out)
		return -EINVAL;

	module_params = kzalloc(sizeof(*module_params), GFP_KERNEL);
	if (!module_params)
		return -ENOMEM;

	module_params->gaudi_huge_page_optimization = hdev->mmu_huge_page_opt;
	module_params->timeout_locked =
			jiffies_to_msecs(hdev->timeout_jiffies) / 1000;
	module_params->reset_on_lockup = hdev->reset_on_lockup;
	module_params->pldm = hdev->pldm;
	module_params->mmu_enable = true;
	if (hdev->clock_gating_enabled) {
		module_params->clock_gating = U32_MAX;
		module_params->clock_gating_ext = U32_MAX;
	}
	module_params->mme_enable = hdev->mme_mask ? true : false;
	module_params->tpc_mask = hdev->tpc_mask;
	module_params->nic_ports_mask = hdev->cn.ports_mask;
	module_params->nic_lanes_per_port = hdev->cn.lanes_per_port;
	module_params->dram_enable = hdev->dram_enable;
	module_params->cpu_enable = !!hdev->fw_components;
	module_params->reset_pcilink = hdev->reset_pcilink;
	module_params->config_pll = hdev->config_pll;
	module_params->cpu_queues_enable = hdev->cpu_queues_enable;
	module_params->fw_loading = lower_32_bits(hdev->fw_components);
	module_params->fw_loading_ext = upper_32_bits(hdev->fw_components);
	module_params->heartbeat = hdev->heartbeat;
	module_params->axi_drain = hdev->axi_drain;
	module_params->security_enable = hdev->security_enable;
	module_params->sram_scrambler_enable = hdev->sram_scrambler_enable;
	module_params->dram_scrambler_enable = hdev->dram_scrambler_enable;
	module_params->hbm_ecc_enable = hdev->hbm_ecc_enable;
	module_params->compatibility_mode = hdev->compatibility_mode;
	module_params->hard_reset_on_fw_events = hdev->hard_reset_on_fw_events;
	module_params->decoder_mask = hdev->decoder_mask;
	module_params->rotator_mask = hdev->rotator_mask;
	module_params->dram_page_scrub = hdev->memory_scrub;
	module_params->cache_enabled = hdev->cache_enable;

	rc = copy_to_user(out, module_params,
		min((size_t) max_size, sizeof(*module_params))) ? -EFAULT : 0;

	kfree(module_params);

	return rc;
}

static int send_fw_generic_request(struct hl_device *hdev, struct hl_info_args *info_args)
{
	void __user *buff = (void __user *) (uintptr_t) info_args->return_pointer;
	u32 size = info_args->return_size;
	dma_addr_t dma_handle;
	bool need_input_buff;
	void *fw_buff;
	int rc = 0;

	switch (info_args->fw_sub_opcode) {
	case HL_PASSTHROUGH_VERSIONS:
		need_input_buff = false;
		break;
	case HL_ECC_INJECTION:
		need_input_buff = true;
		break;
	case HL_PASSTHROUGH_PID_CMD:
		need_input_buff = true;
		break;
	case  HL_GET_ERR_COUNTERS_CMD:
		need_input_buff = true;
		break;
	case HL_GET_P_STATE:
		need_input_buff = false;
		break;
	case HL_GET_SUPPORTED_P_STATES:
		need_input_buff = false;
		break;
	case HL_GET_POWER_LIMIT_CONSTRAINTS:
		need_input_buff = false;
		break;
	default:
		return -EINVAL;
	}

	if (size > SZ_1M) {
		hl_err(hdev, "buffer size cannot exceed 1MB\n");
		return -EINVAL;
	}

	fw_buff = hl_cpu_accessible_dma_pool_alloc(hdev, size, &dma_handle);
	if (!fw_buff)
		return -ENOMEM;


	if (need_input_buff && copy_from_user(fw_buff, buff, size)) {
		hl_dbg(hdev, "Failed to copy from user FW buff\n");
		rc = -EFAULT;
		goto free_buff;
	}

	rc = hl_fw_send_generic_request(hdev, info_args->fw_sub_opcode, dma_handle, &size);
	if (rc)
		goto free_buff;

	if (copy_to_user(buff, fw_buff, min(size, info_args->return_size))) {
		hl_dbg(hdev, "Failed to copy to user FW generic req output\n");
		rc = -EFAULT;
	}

free_buff:
	hl_cpu_accessible_dma_pool_free(hdev, info_args->return_size, fw_buff);

	return rc;
}

static int report_memory_consumption_ioctl(struct hl_device *hdev, struct hl_info_args *info_args)
{
	void __user *buff = (void __user *) (uintptr_t) info_args->return_pointer;
	struct hl_info_memory_consumption input;
	u32 input_size = info_args->return_size;

	if (input_size != sizeof(struct hl_info_memory_consumption)) {
		hl_dbg(hdev, "Unexpected size of user input buffer\n");
		return -EINVAL;
	}

	if (copy_from_user((void *) &input, buff, input_size)) {
		hl_dbg(hdev, "Failed to copy from user input buffer\n");
		return -EFAULT;
	}

	if (input.used_mem > atomic64_read(&hdev->dram_used_mem)) {
		hl_dbg(hdev, "Reported used memory is larger than allocated\n");
		return -EINVAL;
	}

	return hl_report_memory_consumption_to_fw(hdev, input.used_mem, input.timestamp_sec);
}

static int drm_accel_enabled_info(struct hl_fpriv *hpriv, struct hl_info_args *args)
{
	void __user *user_buf = (void __user *)(uintptr_t)args->return_pointer;
	u32 user_buf_size = args->return_size;
	u64 accel_enabled_info;
	int rc;

	if (!user_buf)
		return -EINVAL;

	if (user_buf_size < sizeof(accel_enabled_info))
		return -EINVAL;

#if IS_ENABLED(CONFIG_DRM_ACCEL)
	accel_enabled_info = 1;
#else
	accel_enabled_info = 0;
#endif
	rc = copy_to_user(user_buf, &accel_enabled_info, sizeof(accel_enabled_info));
	return rc ? -EFAULT : 0;
}

static int _hl_info_ioctl(struct hl_fpriv *hpriv, void *data, char *prefix)
{
	enum hl_device_status status;
	struct hl_info_args *args = data;
	struct hl_device *hdev = hpriv->hdev;
	int rc;

	if (args->pad) {
		hl_dbg(hdev, "%sPadding bytes must be 0\n", prefix);
		return -EINVAL;
	}

	/*
	 * Information is returned for the following opcodes even if the device
	 * is disabled or in reset.
	 */
	switch (args->op) {
	case HL_INFO_HW_IP_INFO:
		return hw_ip_info(hdev, args);

	case HL_INFO_DEVICE_STATUS:
		return device_status_info(hdev, args);

	case HL_INFO_MODULE_PARAMS:
		return module_params_info(hdev, args);

	case HL_INFO_RESET_COUNT:
		return get_reset_count(hdev, args);

	case HL_INFO_HW_EVENTS:
		return hw_events_info(hdev, false, args);

	case HL_INFO_HW_EVENTS_AGGREGATE:
		return hw_events_info(hdev, true, args);

	case HL_INFO_CS_COUNTERS:
		return cs_counters_info(hpriv, args);

	case HL_INFO_CLK_THROTTLE_REASON:
		return clk_throttle_info(hpriv, args);

	case HL_INFO_SYNC_MANAGER:
		return sync_manager_info(hpriv, args);

	case HL_INFO_OPEN_STATS:
		return open_stats_info(hpriv, args);

	case HL_INFO_LAST_ERR_OPEN_DEV_TIME:
		return last_err_open_dev_info(hpriv, args);

	case HL_INFO_CS_TIMEOUT_EVENT:
		return cs_timeout_info(hpriv, args);

	case HL_INFO_RAZWI_EVENT:
		return razwi_info(hpriv, args);

	case HL_INFO_UNDEFINED_OPCODE_EVENT:
		return undefined_opcode_info(hpriv, args);

	case HL_INFO_DEV_MEM_ALLOC_PAGE_SIZES:
		return dev_mem_alloc_page_sizes_info(hpriv, args);

	case HL_INFO_GET_EVENTS:
		return events_info(hpriv, args);

	case HL_INFO_PAGE_FAULT_EVENT:
		return page_fault_info(hpriv, args);

	case HL_INFO_USER_MAPPINGS:
		return user_mappings_info(hpriv, args);

	case HL_INFO_UNREGISTER_EVENTFD:
		return eventfd_unregister(hpriv, args);

	case HL_INFO_HW_ERR_EVENT:
		return hw_err_info(hpriv, args);

	case HL_INFO_FW_ERR_EVENT:
		return fw_err_info(hpriv, args);

	case HL_INFO_USER_ENGINE_ERR_EVENT:
		return engine_err_info(hpriv, args);

	case HL_INFO_DRAM_USAGE:
		return dram_usage_info(hpriv, args);

	case HL_INFO_CONFIG_DRM_ACCEL_ENABLED:
		return drm_accel_enabled_info(hpriv, args);

	default:
		break;
	}

	if (!hl_device_operational(hdev, &status)) {
		hl_dbg_ratelimited(hdev,
			"%sDevice is %s. Can't execute INFO IOCTL\n",
			prefix, hdev->status[status]);
		return -EBUSY;
	}

	switch (args->op) {
	case HL_INFO_HW_IDLE:
		rc = hw_idle(hdev, args);
		break;

	case HL_INFO_DEVICE_UTILIZATION:
		rc = device_utilization(hdev, args);
		break;

	case HL_INFO_CLK_RATE:
		rc = get_clk_rate(hdev, args);
		break;

	case HL_INFO_TIME_SYNC:
		return time_sync_info(hdev, args);

	case HL_INFO_PCI_COUNTERS:
		return pci_counters_info(hpriv, args);

	case HL_INFO_TOTAL_ENERGY:
		return total_energy_consumption_info(hpriv, args);

	case HL_INFO_PLL_FREQUENCY:
		return pll_frequency_info(hpriv, args);

	case HL_INFO_POWER:
		return power_info(hpriv, args);

	case HL_INFO_HABANA_LINK_STATE:
		return cn_link_state_info(hpriv, args);

	case HL_INFO_HABANA_LINK_COUNTERS:
		return cn_statistics(hpriv, args);

	case HL_INFO_DRAM_REPLACED_ROWS:
		return dram_replaced_rows_info(hpriv, args);

	case HL_INFO_DRAM_PENDING_ROWS:
		return dram_pending_rows_info(hpriv, args);

	case HL_INFO_SECURED_ATTESTATION:
		return sec_attest_info(hpriv, args);

	case HL_INFO_REGISTER_EVENTFD:
		return eventfd_register(hpriv, args);

	case HL_INFO_ENGINE_STATUS:
		return engine_status_info(hpriv, args);

	case HL_INFO_FW_GENERIC_REQ:
		return send_fw_generic_request(hdev, args);

	case HL_INFO_TIME_SYNC_PER_DIE:
		return time_sync_info_per_die(hdev, args);

	case HL_INFO_DEV_SIGNED:
		return dev_info_signed(hpriv, args);

	case HL_INFO_MEMORY_CONSUMPTION:
		return report_memory_consumption_ioctl(hdev, args);

	default:
		hl_err(hdev, "%sInvalid request %d\n", prefix, args->op);
		rc = -EINVAL;
		break;
	}

	return rc;
}

int hl_info_ioctl(struct drm_device *ddev, void *data, struct drm_file *file_priv)
{
	struct hl_fpriv *hpriv = file_priv->driver_priv;

	return _hl_info_ioctl(hpriv, data, "");
}

static int hl_info_ioctl_control(struct hl_fpriv *hpriv, void *data)
{
	struct hl_info_args *args = data;

	switch (args->op) {
	case HL_INFO_GET_EVENTS:
	case HL_INFO_UNREGISTER_EVENTFD:
	case HL_INFO_REGISTER_EVENTFD:
		return -EOPNOTSUPP;
	default:
		break;
	}

	return _hl_info_ioctl(hpriv, data, "(control) ");
}

static int debug_dio_ioctl(struct drm_file *file_priv, struct hl_debug_args *args)
{
	struct hl_fpriv *hpriv = file_priv->driver_priv;
	struct hl_device *hdev = hpriv->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_dio_args dio_data;
	size_t len_read;
	int rc;

	if (!prop->supports_nvme)
		return 0;

	if (args->input_size != sizeof(struct hl_dio_args))
		return -EINVAL;

	if (copy_from_user(&dio_data, u64_to_user_ptr(args->input_ptr), args->input_size))
		return -EFAULT;

	switch (dio_data.op) {
	case HL_DIO_CMD_SSD2HL:
		rc = hl_dio_ssd2hl(hdev, hpriv->ctx, dio_data.ssd2hl.fd,
				dio_data.ssd2hl.device_va,
				dio_data.ssd2hl.off_bytes,
				dio_data.ssd2hl.len_bytes,
				&len_read);
		if (rc < 0) {
			hl_err(hdev, "SSD2HL error: %d\n", rc);
		} else {
			if (copy_to_user((void __user *)(uintptr_t) args->output_ptr, &len_read,
					sizeof(len_read))) {
				hl_err(hdev, "Error copying IO outcome to the user\n");
				rc = -EFAULT;
			}
		} break;
	case HL_DIO_CMD_HL2SSD:
		hl_err(hdev, "HL2SSD is not supported at this time\n");
		rc = -EINVAL;
		break;
	default:
		hl_err(hdev, "Invalid HLDIO request %u\n", dio_data.op);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static bool hl_debug_op_requires_debug(u32 op)
{
	switch (op) {
	case HL_DEBUG_OP_ETR:
	case HL_DEBUG_OP_ETF:
	case HL_DEBUG_OP_STM:
	case HL_DEBUG_OP_FUNNEL:
	case HL_DEBUG_OP_BMON:
	case HL_DEBUG_OP_SPMU:
	case HL_DEBUG_OP_TIMESTAMP:
	case HL_DEBUG_OP_FETCH_TRACE:
	case HL_DEBUG_OP_READBLOCK:
	case HL_DEBUG_OP_SCHED_SUBMIT_BUF:
		return true;
	default:
		return false;
	}
}

int hl_debug_ioctl(struct drm_device *ddev, void *data, struct drm_file *file_priv)
{
	struct hl_fpriv *hpriv = file_priv->driver_priv;
	struct hl_device *hdev = hpriv->hdev;
	struct hl_debug_args *args = data;
	enum hl_device_status status;
	int rc = 0;

	if (!hl_device_operational(hdev, &status)) {
		hl_dbg_ratelimited(hdev,
			"Device is %s. Can't execute DEBUG IOCTL\n",
			hdev->status[status]);
		return -EBUSY;
	}

	if (hl_debug_op_requires_debug(args->op) && !hdev->in_debug) {
		hl_err_ratelimited(hdev,
				"Rejecting debug request because device not in debug mode\n");
		return -EPERM;
	}

	switch (args->op) {
	case HL_DEBUG_OP_ETR:
	case HL_DEBUG_OP_ETF:
	case HL_DEBUG_OP_STM:
	case HL_DEBUG_OP_FUNNEL:
	case HL_DEBUG_OP_BMON:
	case HL_DEBUG_OP_SPMU:
	case HL_DEBUG_OP_TIMESTAMP:
	case HL_DEBUG_OP_FETCH_TRACE:
		args->input_size = min(args->input_size, hl_debug_struct_size[args->op]);
		args->output_size = min_t(u32, args->output_size, PAGE_SIZE);
		rc = debug_coresight(hdev, hpriv->ctx, args);
		break;

	case HL_DEBUG_OP_SET_MODE:
		rc = hl_device_set_debug_mode(hdev, hpriv->ctx, (bool) args->enable);
		break;
#ifdef HL_DOWNSTREAM
	case HL_DEBUG_OP_READMEM:
	case HL_DEBUG_OP_MEMCPY:
		if (hdev->pdev) {
			hl_err_ratelimited(hdev,
				"Rejecting memory access debug request, because device not in simulator mode\n");
			return -EPERM;
		}
		rc = sim_mem_access_debug_handler(hdev, args);
		break;
#endif /* HL_DOWNSTREAM */
	case HL_DEBUG_OP_SCHED_SUBMIT_BUF:
		rc = debug_sched_ioctl(hdev, hpriv->ctx, args);
		break;
	case HL_DEBUG_OP_READBLOCK:
		rc = debug_read_dev_mem_block(hpriv, args);
		break;
	case HL_DEBUG_ENABLE_ERR_INFO_CAPTURE:
		hl_enable_err_info_capture(&hdev->captured_err_info);
		break;
	case HL_DEBUG_OP_DIO:
		rc = debug_dio_ioctl(file_priv, args);
		break;
	default:
		hl_err(hdev, "Invalid request %d\n", args->op);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int cn_control(struct hl_fpriv *hpriv, struct hl_nic_args *args)
{
	struct hl_device *hdev = hpriv->hdev;
	void *input = NULL, *output = NULL;
	int rc;

	if (args->input_ptr && args->input_size) {
		input = kzalloc(hl_nic_input_size[args->op], GFP_KERNEL);
		if (!input) {
			rc = -ENOMEM;
			goto out;
		}

		if (copy_from_user(input, u64_to_user_ptr(args->input_ptr),
					args->input_size)) {
			rc = -EFAULT;
			hl_dbg(hdev, "failed to copy input NIC data\n");
			goto out;
		}
	}

	if (args->output_ptr && args->output_size) {
		output = kzalloc(hl_nic_output_size[args->op], GFP_KERNEL);
		if (!output) {
			rc = -ENOMEM;
			goto out;
		}
	}

	rc = hdev->asic_funcs->cn_control(hdev, args->op, input, output, hpriv->ctx);
	if (rc) {
		/* SW-52983: overcome CI failing us on err message issued due to
		 * temporary lack of connections caused by the graceful QP release
		 */
		if ((rc == -EBUSY) &&
			(args->op == HL_NIC_OP_ALLOC_CONN || args->op == HL_NIC_OP_ALLOC_COLL_CONN))
			hl_dbg_ratelimited(hdev,
					"Need to retry NIC control operation %d (RC %d)\n",
					args->op, rc);
		else
			hl_dbg_ratelimited(hdev,
					"NIC control operation %d failed %d\n", args->op, rc);
	}

	if (output && copy_to_user((void __user *) (uintptr_t) args->output_ptr,
					output, args->output_size)) {
		hl_dbg(hdev, "copy to user failed in nic ioctl\n");
		rc = -EFAULT;
		goto out;
	}

out:
	kfree(output);
	kfree(input);

	return rc;
}

int hl_nic_ioctl(struct drm_device *ddev, void *data, struct drm_file *file_priv)
{
	struct hl_fpriv *hpriv = file_priv->driver_priv;
	struct hl_device *hdev = hpriv->hdev;
	struct hl_nic_args *args = data;
	enum hl_device_status status;

	int rc;

	if (!hl_device_operational(hdev, &status)) {
		hl_dbg_ratelimited(hdev,
			"Device is %s. Can't execute NIC IOCTL\n",
			hdev->status[status]);
		return -EBUSY;
	}

	switch (args->op) {
	case HL_NIC_OP_ALLOC_CONN:
	case HL_NIC_OP_SET_REQ_CONN_CTX:
	case HL_NIC_OP_SET_RES_CONN_CTX:
	case HL_NIC_OP_DESTROY_CONN:
	case HL_NIC_OP_USER_WQ_SET:
	case HL_NIC_OP_USER_WQ_UNSET:
	case HL_NIC_OP_USER_CQ_SET:
	case HL_NIC_OP_USER_CQ_UNSET:
	case HL_NIC_OP_USER_CQ_UPDATE_CI:
	case HL_NIC_OP_ALLOC_USER_CQ_ID:
	case HL_NIC_OP_SET_USER_APP_PARAMS:
	case HL_NIC_OP_GET_USER_APP_PARAMS:
	case HL_NIC_OP_ALLOC_USER_DB_FIFO:
	case HL_NIC_OP_USER_DB_FIFO_SET:
	case HL_NIC_OP_USER_DB_FIFO_UNSET:
	case HL_NIC_OP_EQ_POLL:
	case HL_NIC_OP_USER_ENCAP_ALLOC:
	case HL_NIC_OP_USER_ENCAP_SET:
	case HL_NIC_OP_USER_ENCAP_UNSET:
	case HL_NIC_OP_USER_CCQ_SET:
	case HL_NIC_OP_USER_CCQ_UNSET:
	case HL_NIC_OP_USER_CQ_ID_SET:
	case HL_NIC_OP_USER_CQ_ID_UNSET:
	case HL_NIC_OP_ALLOC_COLL_CONN:
	case HL_NIC_OP_DUMP_QP:
		args->input_size =
			min(args->input_size, hl_nic_input_size[args->op]);
		args->output_size =
			min(args->output_size, hl_nic_output_size[args->op]);
		rc = cn_control(hpriv, args);
		break;
	default:
		hl_dbg(hdev, "Invalid request %d\n", args->op);
		rc = -EINVAL;
		break;
	}

	return rc;
}

#define HL_IOCTL_DEF(ioctl, _func) \
	[_IOC_NR(ioctl) - HL_COMMAND_START] = {.cmd = ioctl, .func = _func}

#if !IS_ENABLED(CONFIG_DRM_ACCEL)
static const struct hl_ioctl_desc hl_ioctls[] = {
	HL_IOCTL_DEF(DRM_IOCTL_HL_INFO, hl_accel_info_ioctl),
	HL_IOCTL_DEF(DRM_IOCTL_HL_CB, hl_accel_cb_ioctl),
	HL_IOCTL_DEF(DRM_IOCTL_HL_CS, hl_accel_cs_ioctl),
	HL_IOCTL_DEF(DRM_IOCTL_HL_WAIT_CS, hl_accel_wait_ioctl),
	HL_IOCTL_DEF(DRM_IOCTL_HL_MEMORY, hl_accel_mem_ioctl),
	HL_IOCTL_DEF(DRM_IOCTL_HL_DEBUG, hl_accel_debug_ioctl),
	HL_IOCTL_DEF(DRM_IOCTL_HL_NIC, hl_accel_nic_ioctl)
};
#endif /* !IS_ENABLED(CONFIG_DRM_ACCEL) */

static const struct hl_ioctl_desc hl_ioctls_control[] = {
	HL_IOCTL_DEF(DRM_IOCTL_HL_INFO, hl_info_ioctl_control)
};

static long _hl_ioctl(struct hl_fpriv *hpriv, unsigned int cmd, unsigned long arg,
		      const char *prefix, const struct hl_ioctl_desc *ioctl)
{
	unsigned int nr = _IOC_NR(cmd);
	char stack_kdata[128] = {0};
	char *kdata = NULL;
	unsigned int usize, asize;
	hl_ioctl_t *func;
	u32 hl_size;
	int retcode;

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		hl_dbg(hpriv->hdev, "%sno function\n", prefix);
		retcode = -ENOTTY;
		goto out_err;
	}

	hl_size = _IOC_SIZE(ioctl->cmd);
	usize = asize = _IOC_SIZE(cmd);
	if (hl_size > asize)
		asize = hl_size;

	cmd = ioctl->cmd;

	if (cmd & (IOC_IN | IOC_OUT)) {
		if (asize <= sizeof(stack_kdata)) {
			kdata = stack_kdata;
		} else {
			kdata = kzalloc(asize, GFP_KERNEL);
			if (!kdata) {
				retcode = -ENOMEM;
				goto out_err;
			}
		}
	}

	if (cmd & IOC_IN) {
		if (copy_from_user(kdata, (void __user *)arg, usize)) {
			retcode = -EFAULT;
			goto out_err;
		}
	}

	retcode = func(hpriv, kdata);

	if ((cmd & IOC_OUT) && copy_to_user((void __user *)arg, kdata, usize))
		retcode = -EFAULT;

out_err:
	if (retcode) {
		char task_comm[TASK_COMM_LEN];

		hl_dbg_ratelimited(hpriv->hdev,
				"%serror in ioctl: pid=%d, comm=\"%s\", cmd=%#010x, nr=%#04x\n",
				prefix, task_pid_nr(current), get_task_comm(task_comm, current),
				cmd, nr);
	}

	if (kdata != stack_kdata)
		kfree(kdata);

	return retcode;
}

#if !IS_ENABLED(CONFIG_DRM_ACCEL)
long hl_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct drm_file *file_priv = filep->private_data;
	struct hl_fpriv *hpriv = file_priv->driver_priv;
	struct hl_device *hdev = hpriv->hdev;
	const struct hl_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);

	if (!hdev) {
		pr_err_ratelimited("Sending ioctl after device was removed! Please close FD\n");
		return -ENODEV;
	}

	if (nr >= HL_COMMAND_START && nr < HL_COMMAND_END) {
		ioctl = &hl_ioctls[nr - HL_COMMAND_START];
	} else {
		char task_comm[TASK_COMM_LEN];

		hl_dbg_ratelimited(hdev,
				"invalid ioctl: pid=%d, comm=\"%s\", cmd=%#010x, nr=%#04x\n",
				task_pid_nr(current), get_task_comm(task_comm, current), cmd, nr);
		return -ENOTTY;
	}

	return _hl_ioctl(hpriv, cmd, arg, "", ioctl);
}
#endif /* !IS_ENABLED(CONFIG_DRM_ACCEL) */

long hl_ioctl_control(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct hl_fpriv *hpriv = filep->private_data;
	struct hl_device *hdev = hpriv->hdev;
	const struct hl_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);

	if (!hdev) {
		pr_err_ratelimited("Sending ioctl after device was removed! Please close FD\n");
		return -ENODEV;
	}

	if (nr == _IOC_NR(DRM_IOCTL_HL_INFO)) {
		ioctl = &hl_ioctls_control[nr - HL_COMMAND_START];
	} else {
		char task_comm[TASK_COMM_LEN];

		hl_dbg_ratelimited(hdev,
				"invalid ioctl: pid=%d, comm=\"%s\", cmd=%#010x, nr=%#04x\n",
				task_pid_nr(current), get_task_comm(task_comm, current), cmd, nr);
		return -ENOTTY;
	}

	return _hl_ioctl(hpriv, cmd, arg, "(control) ",ioctl);
}
