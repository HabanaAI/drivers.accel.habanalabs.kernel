// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#define pr_fmt(fmt)		"habanalabs: " fmt

#include "habanalabs.h"
#include "../include/common/simulator.h"

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/irq.h>

#include <trace/events/habanalabs.h>

/* The last available minor number is occupied by the simulator mode device */
#define HL_SIM_MAX_MINORS	(HL_MAX_MINORS - 1)
#define HL_SIM_MODE_DEV_MINOR	HL_SIM_MAX_MINORS

/* The coral shall no request more than - 2MB */
#define HL_SIM_MAX_SHARED_BLOCK_SIZE	(512 * PAGE_SIZE)

static int sim_mode;
static int sim_single_msi;

module_param(sim_mode, int, 0444);
MODULE_PARM_DESC(sim_mode, "Simulator mode (0 = no, 1 = only sim, 2 = ASIC and sim, default no)");

module_param(sim_single_msi, int, 0444);
MODULE_PARM_DESC(sim_single_msi,
		"Simulator single MSI mode (0 = no, 1 = yes, default no)");

static struct hl_sim_mode_device {
	struct class	*hclass;
	struct cdev	cdev;
	struct device	*dev;
	struct idr	*hl_devs_idr;
	struct mutex	*hl_devs_idr_lock;
	u32		major;
	u32		virt_dev_type[HL_SIM_MAX_MINORS];
	u8		single_msi;
} sim_mode_dev;

static int hl_sim_access_dram_region(struct hl_device *hdev, struct hl_simulator_device *edev,
		u64 addr, u64 *val, enum debugfs_access_type acc_type)
{
	struct pci_mem_region *dram_region = &hdev->pci_mem_region[PCI_REGION_DRAM];

	switch (acc_type) {
	case DEBUGFS_READ8:
		return hl_sim_read_dram(edev, val,
			(addr - dram_region->region_base), sizeof(uint8_t));
	case DEBUGFS_WRITE8:
		return hl_sim_write_dram(edev, addr - dram_region->region_base,
			val, sizeof(uint8_t));
	case DEBUGFS_READ32:
		return hl_sim_read_dram(edev, val,
			(addr - dram_region->region_base), sizeof(uint32_t));
	case DEBUGFS_WRITE32:
		return hl_sim_write_dram(edev, addr - dram_region->region_base,
			val, sizeof(uint32_t));
	case DEBUGFS_READ64:
		return hl_sim_read_dram(edev, val,
			(addr - dram_region->region_base), sizeof(uint64_t));
	case DEBUGFS_WRITE64:
		return hl_sim_write_dram(edev, addr - dram_region->region_base,
			val, sizeof(uint64_t));
	}

	return 0;
}

static int hl_sim_access_sram_region(struct hl_device *hdev, u64 addr, u64 *val,
	enum debugfs_access_type acc_type)
{
	struct pci_mem_region *sram_region = &hdev->pci_mem_region[PCI_REGION_SRAM];

	switch (acc_type) {
	case DEBUGFS_READ32:
		*val = *(__force u32 *) (hdev->pcie_bar[sram_region->bar_id] +
			(addr - sram_region->region_base));
		break;
	case DEBUGFS_WRITE32:
		*(__force u32 *) (hdev->pcie_bar[sram_region->bar_id] +
				(addr - sram_region->region_base)) = *val;
		break;
	case DEBUGFS_READ64:
		*val = *(__force u64 *) (hdev->pcie_bar[sram_region->bar_id] +
			(addr - sram_region->region_base));
		break;
	case DEBUGFS_WRITE64:
		*(__force u64 *) (hdev->pcie_bar[sram_region->bar_id] +
				(addr - sram_region->region_base)) = *val;
		break;
	default:
		hl_err(hdev, "sram access-type %d is not supported\n", acc_type);
		return -EOPNOTSUPP;
	}

	return 0;
}

int hl_sim_dma_map_sgtable(struct hl_device *hdev, struct sg_table *sgt,
				enum dma_data_direction dir)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct scatterlist *sg;
	int i;

	if (sgt->nents == 0 || sgt->sgl[0].length == 0)
		hl_crit(hdev, "no scatterlist entries\n");

	for_each_sgtable_sg(sgt, sg, i) {
		if (!sg_page(sg))
			hl_crit(hdev, "error getting scatterlist page\n");
		sg->dma_address = sg_phys(sg);
		sg->dma_length = sg->length;

		/* Shift to the device's base physical address of host memory */
		sg->dma_address += prop->device_dma_offset_for_host_access;
	}

	return 0;
}

void hl_sim_dma_unmap_sgtable(struct hl_device *hdev, struct sg_table *sgt,
				enum dma_data_direction dir)
{
}

/**
 * hl_sim_access_dev_mem - access dev memory for simulators
 *
 * @hdev: pointer to habanalabs device structure
 * @edev: pointer to extended simulator device struct
 * @region_type: the region type, the address belongs to
 * @addr: the address to access
 * @val: the value to write from or read to
 * @acc_type: the type of access (r/w, 32/64)
 */
int hl_sim_access_dev_mem(struct hl_device *hdev, struct hl_simulator_device *edev,
		enum pci_region region_type, u64 addr, u64 *val,
		enum debugfs_access_type acc_type)

{
	switch (region_type) {
	case PCI_REGION_CFG:
		return hl_access_cfg_region(hdev, addr, val, acc_type);
	case PCI_REGION_SRAM:
		return hl_sim_access_sram_region(hdev, addr, val, acc_type);
	case PCI_REGION_DRAM:
		return hl_sim_access_dram_region(hdev, edev, addr, val, acc_type);
	default:
		return -EFAULT;
	}

	return 0;
}

int hl_sim_alloc_irq_vectors(struct hl_simulator_device *edev, unsigned int min_vecs,
			unsigned int max_vecs, unsigned int flags)
{
	int rc, i;

	rc = __irq_alloc_descs(-1, -1, max_vecs, cpu_to_node(get_cpu()), THIS_MODULE, NULL);
	put_cpu();
	if (rc < 0)
		return rc;

	edev->sirq.irq0 = rc;
	edev->sirq.size = max_vecs;
	for (i = 0 ; i < edev->sirq.size ; ++i)
		irq_set_chip_and_handler(i + edev->sirq.irq0,
					&dummy_irq_chip,
					handle_simple_irq);
	return 0;
}

void hl_sim_free_irq_vectors(struct hl_simulator_device *edev)
{
	irq_free_descs(edev->sirq.irq0, edev->sirq.size);
}

int hl_sim_irq_vector(struct hl_simulator_device *edev, unsigned int nr)
{
	if (!edev->sirq.size)
		return -EINVAL;

	return edev->sirq.irq0 + nr;
}

int hl_sim_send_irq(struct hl_simulator_device *edev, unsigned int nr)
{
	unsigned long flags;
	int irq, rc;

	irq = hl_sim_irq_vector(edev, nr);
	if (irq < 0)
		return irq;

	local_irq_save(flags);
	preempt_disable();
	rc = generic_handle_irq(irq);
	preempt_enable();
	local_irq_restore(flags);

	return rc;
}

static int hl_sim_mode_open(struct inode *inode, struct file *filp)
{
	struct hl_sim_mode_device *sdev = &sim_mode_dev;

	filp->private_data = sdev;
	nonseekable_open(inode, filp);

	return 0;
}

static int hl_sim_mode_release(struct inode *inode, struct file *filp)
{
	return 0;
}

#if IS_ENABLED(CONFIG_DRM_ACCEL)
static int hl_sim_allocate_device_id(struct hl_sim_mode_device *sdev, int *minor)
{
	mutex_lock(sdev->hl_devs_idr_lock);
	*minor = idr_alloc(sdev->hl_devs_idr, NULL, 0, HL_SIM_MAX_MINORS, GFP_KERNEL);
	mutex_unlock(sdev->hl_devs_idr_lock);

	if (*minor < 0) {
		pr_err("too many devices in the system\n");
		return -EBUSY;
	}

	return 0;
}
#else
static int hl_sim_allocate_device_id(struct hl_sim_mode_device *sdev, int *minor)
{
	int minor_control = 0;

	mutex_lock(sdev->hl_devs_idr_lock);

	*minor = idr_alloc(sdev->hl_devs_idr, NULL, 0, HL_SIM_MAX_MINORS, GFP_KERNEL);
	if (*minor < 0)
		goto mutex_unlock;

	minor_control = idr_alloc(sdev->hl_devs_idr, NULL, *minor + 1, *minor + 2, GFP_KERNEL);
	if (minor_control < 0)
		idr_remove(sdev->hl_devs_idr, *minor);

mutex_unlock:
	mutex_unlock(sdev->hl_devs_idr_lock);

	if (*minor < 0 || minor_control < 0) {
		pr_err("too many devices in the system\n");
		return -EBUSY;
	}

	return 0;
}
#endif /* IS_ENABLED(CONFIG_DRM_ACCEL) */

/* Should be called while the hl_devs_idr lock is taken */
static void hl_sim_release_minor_locked(struct hl_sim_mode_device *sdev, u32 minor)
{
#if !IS_ENABLED(CONFIG_DRM_ACCEL)
	idr_remove(sdev->hl_devs_idr, minor + 1);
#endif
	idr_remove(sdev->hl_devs_idr, minor);
}

static int hl_sim_set_devtype_get_minor_ioctl(struct hl_sim_mode_device *sdev,
						void *data)
{
	int (*simulator_start)(struct simulator_start_args *sim_start_args);
	struct hlv_sim_devtype_minor_args *args = data;
	struct simulator_start_args sim_start_args;
	int minor, rc;
	u8 args_size;

	memset(&sim_start_args, 0, sizeof(sim_start_args));

	sim_start_args.virt_dev_type = args->devtype_or_minor;
	args_size = FIELD_GET(SIM_ARGS_SIZE_MASK, args->dram_mask);

	if (args_size >= offsetof(struct hlv_sim_devtype_minor_args,
				dram_user_pointer) +
				sizeof(sim_start_args.dram_user_pointer))
		sim_start_args.dram_user_pointer = args->dram_user_pointer;
	else
		sim_start_args.dram_user_pointer = 0;

	if (args_size >= offsetof(struct hlv_sim_devtype_minor_args,
				sram_user_pointer) +
				sizeof(sim_start_args.sram_user_pointer))
		sim_start_args.sram_user_pointer = args->sram_user_pointer;
	else
		sim_start_args.sram_user_pointer = 0;

	if (args_size >= offsetof(struct hlv_sim_devtype_minor_args,
			sram_size_mb) +
			sizeof(sim_start_args.sram_dram_user_pointer)) {
		sim_start_args.sram_size_in_mb = args->sram_size_mb;
		sim_start_args.sram_dram_user_pointer =
						args->sram_dram_user_pointer;

	} else {
		sim_start_args.sram_size_in_mb = 0;
		sim_start_args.sram_dram_user_pointer = 0;
	}

	switch (args->devtype_or_minor) {
	case HLV_SIM_GOYA:
		sim_start_args.virt_dev_type = ASIC_GOYA_SIM;
		simulator_start = goya_simulator_start;
		break;
	case HLV_SIM_GAUDI:
		sim_start_args.virt_dev_type = ASIC_GAUDI_SIM;
		simulator_start = gaudi_simulator_start;
		break;
	case HLV_SIM_GAUDI_HL2000M:
		sim_start_args.virt_dev_type = ASIC_GAUDI_HL2000M_SIM;
		simulator_start = gaudi_simulator_start;
		break;
	case HLV_SIM_GAUDI2:
		sim_start_args.virt_dev_type = ASIC_GAUDI2_SIM;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2B:
		sim_start_args.virt_dev_type = ASIC_GAUDI2B_SIM;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2C:
		sim_start_args.virt_dev_type = ASIC_GAUDI2C_SIM;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2D:
		sim_start_args.virt_dev_type = ASIC_GAUDI2D_SIM;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2E:
		sim_start_args.virt_dev_type = ASIC_GAUDI2E_SIM;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI2_SIM_ARC;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2B_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI2B_SIM_ARC;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2C_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI2C_SIM_ARC;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2D_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI2D_SIM_ARC;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2E_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI2E_SIM_ARC;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2_HL_288:
		sim_start_args.virt_dev_type = ASIC_GAUDI2_HL_288_SIM;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2D_HL_288:
		sim_start_args.virt_dev_type = ASIC_GAUDI2D_HL_288_SIM;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2E_HL_288:
		sim_start_args.virt_dev_type = ASIC_GAUDI2E_HL_288_SIM;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2_HL_288_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI2_HL_288_SIM_ARC;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2D_HL_288_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI2D_HL_288_SIM_ARC;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI2E_HL_288_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI2E_HL_288_SIM_ARC;
		simulator_start = gaudi2_simulator_start;
		break;
	case HLV_SIM_GAUDI3:
		sim_start_args.virt_dev_type = ASIC_GAUDI3_SIM;
		simulator_start = gaudi3_simulator_start;
		break;
	case HLV_SIM_GAUDI3D:
		sim_start_args.virt_dev_type = ASIC_GAUDI3D_SIM;
		simulator_start = gaudi3_simulator_start;
		break;
	case HLV_SIM_GAUDI3E:
		sim_start_args.virt_dev_type = ASIC_GAUDI3E_SIM;
		simulator_start = gaudi3_simulator_start;
		break;
	case HLV_SIM_GAUDI3_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI3_SIM_ARC;
		simulator_start = gaudi3_simulator_start;
		break;
	case HLV_SIM_GAUDI3D_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI3D_SIM;
		simulator_start = gaudi3_simulator_start;
		break;
	case HLV_SIM_GAUDI3E_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI3E_SIM_ARC;
		simulator_start = gaudi3_simulator_start;
		break;
	case HLV_SIM_GAUDI3_HL_338:
		sim_start_args.virt_dev_type = ASIC_GAUDI3_HL_338_SIM;
		simulator_start = gaudi3_simulator_start;
		break;
	case HLV_SIM_GAUDI3D_HL_338:
		sim_start_args.virt_dev_type = ASIC_GAUDI3D_HL_338_SIM;
		simulator_start = gaudi3_simulator_start;
		break;
	case HLV_SIM_GAUDI3E_HL_338:
		sim_start_args.virt_dev_type = ASIC_GAUDI3E_HL_338_SIM;
		simulator_start = gaudi3_simulator_start;
		break;
	case HLV_SIM_GAUDI3_HL_338_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI3_HL_338_SIM_ARC;
		simulator_start = gaudi3_simulator_start;
		break;
	case HLV_SIM_GAUDI3D_HL_338_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI3D_HL_338_SIM_ARC;
		simulator_start = gaudi3_simulator_start;
		break;
	case HLV_SIM_GAUDI3E_HL_338_ARC:
		sim_start_args.virt_dev_type = ASIC_GAUDI3E_HL_338_SIM_ARC;
		simulator_start = gaudi3_simulator_start;
		break;
	default:
		pr_err("invalid simulator device type in ioctl %d\n",
			args->devtype_or_minor);
		return -EINVAL;
	}

	rc = hl_sim_allocate_device_id(sdev, &minor);
	if (rc)
		return rc;

	sim_start_args.minor = minor;
	sim_start_args.major = sdev->major;
	sim_start_args.hclass = sdev->hclass;
	sim_start_args.dram_size_in_mb = FIELD_GET(DRAM_SIZE_IN_GB_MASK, args->dram_mask) * 1024 +
						FIELD_GET(DRAM_SIZE_IN_MB_MASK, args->dram_mask);
	rc = simulator_start(&sim_start_args);
	if (rc)
		goto release_device_id;

	sdev->virt_dev_type[minor] = sim_start_args.virt_dev_type;

#if IS_ENABLED(CONFIG_DRM_ACCEL)
	args->devtype_or_minor = minor + HLV_SIM_ID_OFFSET;
#else
	args->devtype_or_minor = minor / 2 + HLV_SIM_ID_OFFSET;
#endif

	return 0;

release_device_id:
	mutex_lock(sdev->hl_devs_idr_lock);
	hl_sim_release_minor_locked(sdev, minor);
	mutex_unlock(sdev->hl_devs_idr_lock);
	return rc;
}

static int hl_sim_get_supported_features(struct hl_sim_mode_device *sdev,
					 void *data)
{
	struct hlv_sim_supported_features *args = data;

	memset(args, 0, sizeof(*args));

	__set_bit(HLV_FEATURE_DRAM_USER_POINTER_BIT,
			(unsigned long *) args->features_mask);

	__set_bit(HLV_FEATURE_SRAM_USER_POINTER_BIT,
			(unsigned long *) args->features_mask);

	__set_bit(HLV_FEATURE_SINGLE_USER_POINTER_BIT,
			(unsigned long *) args->features_mask);

	return 0;
}

/**
 * hl_sim_send_cmd_msg - send a read/write cmd msg to the simulator
 *
 * @edev: pointer to extended simulator device struct
 * @msg: simulator command message
 *
 * Returns U32_MAX on failure, 0 on success.
 *
 */
static u32 hl_sim_send_cmd_msg(struct hl_simulator_device *edev,
		struct simulator_msg *msg)
{
	bool pushed;
	ktime_t timeout = ktime_add_us(ktime_get(), edev->rw_reg_timeout);

hl_sim_msg_try_h2c:
	spin_lock(&edev->h2c_lock);
	pushed = kfifo_in(&edev->h2c_fifo, &msg, sizeof(msg)) == sizeof(msg);
	spin_unlock(&edev->h2c_lock);

	if (!pushed) {
		if ((edev->rw_reg_timeout &&
				(ktime_compare(ktime_get(), timeout) > 0)) ||
				!edev->open) {
			edev->reg_err_cnt++;
			return U32_MAX;
		}

		usleep_range((1000 >> 2) + 1, 1000);
		goto hl_sim_msg_try_h2c;
	}

	wake_up_interruptible_poll(&edev->pollq, EPOLLIN | EPOLLRDNORM);

	return 0;
}

/**
 * hl_sim_rreg - Read an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg_addr: MMIO register full address
 * @edev: pointer to extended simulator device structure
 *
 * Returns the value of the MMIO register we are asked to read
 *
 */
u32 hl_sim_rreg(struct hl_device *hdev, u64 reg_addr, struct hl_simulator_device *edev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct simulator_msg *msg;
	int count;
	u32 op_id, val;
	ktime_t timeout;

	if (!edev) {
		pr_crit("BUG! simulator device gone but driver still tries to read from it\n");
		return U32_MAX;
	}

	if (edev->reg_err_cnt >= 1 || !edev->open)
		return U32_MAX;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return U32_MAX;

	op_id = atomic_inc_return(&edev->h2c_seq);
	msg->id = op_id;
	msg->cmd = SIM_CMD_READ;
	msg->addr = reg_addr;

	if (hl_sim_send_cmd_msg(edev, msg)) {
		if (edev->open)
			hl_crit(hdev, "Read register 0x%llx request to simulator failed\n",
				reg_addr);
		kfree(msg);
		return U32_MAX;
	}

	msg = NULL;
	timeout = ktime_add_us(ktime_get(), edev->rw_reg_timeout);

hl_sim_rreg_try_c2h:
	spin_lock(&edev->c2h_lock);
	count = kfifo_out_peek(&edev->c2h_fifo, &msg, sizeof(msg));

	if ((count == 0) || (msg->id != op_id) ||
					(msg->addr != reg_addr)) {
		spin_unlock(&edev->c2h_lock);
		if ((edev->rw_reg_timeout && ktime_compare(ktime_get(), timeout) > 0) ||
									!edev->open) {
			if (edev->open)
				hl_crit(hdev, "Read register 0x%llx from simulator failed\n",
					reg_addr);
			edev->reg_err_cnt++;
			return U32_MAX;
		}

		usleep_range((1000 >> 2) + 1, 1000);
		goto hl_sim_rreg_try_c2h;
	}

	count = kfifo_out(&edev->c2h_fifo, &msg, sizeof(msg));

	spin_unlock(&edev->c2h_lock);

	val = msg->val;
	kfree(msg);

	if (unlikely(trace_habanalabs_rreg32_enabled() && hdev->debug_rreg))
		trace_habanalabs_rreg32(HL_PARENT_DEV(hdev), reg_addr - prop->cfg_base_address,
					val);

	return val;
}

/**
 * hl_sim_wreg - Write to an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg_addr: MMIO register full address
 * @edev: pointer to extended simulator device structure
 * @val: 32-bit value
 *
 * Writes the 32-bit value into the MMIO register
 *
 */
void hl_sim_wreg(struct hl_device *hdev, u64 reg_addr, struct hl_simulator_device *edev, u32 val)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct simulator_msg *msg;

	if (!edev) {
		pr_crit("BUG! simulator device gone but driver still tries to write to it\n");
		return;
	}

	if (edev->reg_err_cnt >= 1 || !edev->open)
		return;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return;

	msg->id = atomic_inc_return(&edev->h2c_seq);
	msg->cmd = SIM_CMD_WRITE;
	msg->addr = reg_addr;
	msg->val = val;

	if (hl_sim_send_cmd_msg(edev, msg)) {
		if (edev->open)
			hl_crit(hdev, "Write register 0x%llx to simulator failed\n",
				reg_addr);
		kfree(msg);
	}

	if (unlikely(trace_habanalabs_wreg32_enabled() && hdev->debug_wreg))
		trace_habanalabs_wreg32(HL_PARENT_DEV(hdev), reg_addr - prop->cfg_base_address,
					val);
}

void hl_sim_notify_reset(struct hl_device *hdev, struct hl_simulator_device *edev)
{
	struct simulator_msg *msg;

	if (!edev) {
		pr_crit("BUG! simulator device gone but driver still tries to notify reset\n");
		return;
	}

	if (edev->reg_err_cnt >= 1 || !edev->open)
		return;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return;

	msg->id = atomic_inc_return(&edev->h2c_seq);
	msg->cmd = SIM_CMD_RESET;

	if (hl_sim_send_cmd_msg(edev, msg)) {
		hl_crit(hdev,
		"Failed to send reset request to simulator. Maybe the simulator crashed ?\n");
		kfree(msg);
	}
}

void hl_sim_set_priv_assertions(struct hl_simulator_device *edev, bool enable)
{
	struct simulator_msg *msg;

	if (edev->reg_err_cnt >= 1 || !edev->open)
		return;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return;

	msg->id = atomic_inc_return(&edev->h2c_seq);
	msg->cmd = enable ? SIM_CMD_PRIV_ASSERTION_ENABLE : SIM_CMD_PRIV_ASSERTION_DISABLE;

	if (hl_sim_send_cmd_msg(edev, msg)) {
		dev_crit(edev->dev,
		"Failed to send reset request to simulator. Maybe the simulator crashed ?\n");
		kfree(msg);
	}
}

void hl_sim_notify_simulator_close(struct hl_device *hdev)
{
	u64 event_mask;

	event_mask = HL_NOTIFIER_EVENT_DEVICE_RESET
			| HL_NOTIFIER_EVENT_DEVICE_UNAVAILABLE
			| HL_NOTIFIER_EVENT_CRITICL_HW_ERR;
	hl_notifier_event_send_all(hdev, event_mask);
}

/*
 * Ioctl function type.
 *
 * \param sdev pointer to simulator mode device.
 * \param data pointer to arg that was copied from user.
 */
typedef int hl_sim_mode_ioctl_t(struct hl_sim_mode_device *sdev, void *data);

struct hl_sim_mode_ioctl_desc {
	unsigned int cmd;
	hl_sim_mode_ioctl_t *func;
};

#define HL_SIMULATOR_IOCTL_DEF(ioctl, _func) \
	[_IOC_NR(ioctl)] = {.cmd = ioctl, .func = _func}

/** Ioctl table */
static const struct hl_sim_mode_ioctl_desc hl_sim_mode_ioctls[] = {
	HL_SIMULATOR_IOCTL_DEF(HLV_SIMULATOR_IOCTL_SET_DEVTYPE_GET_MINOR,
			hl_sim_set_devtype_get_minor_ioctl),
	HL_SIMULATOR_IOCTL_DEF(HLV_SIMULATOR_IOCTL_GET_SUPPORTED_FEATURES,
			hl_sim_get_supported_features)
};

#define HL_SIMULATOR_IOCTL_COUNT	ARRAY_SIZE(hl_sim_mode_ioctls)

static long hl_sim_mode_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct hl_sim_mode_device *sdev = filp->private_data;
	const struct hl_sim_mode_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);
	hl_sim_mode_ioctl_t *func;
	unsigned int usize, asize;
	char stack_kdata[128];
	char *kdata = NULL;
	u32 hl_size;
	int retcode = -EINVAL;

	if (nr >= HL_SIMULATOR_IOCTL_COUNT)
		goto err_i1;

	if (nr < HLV_SIMULATOR_COMMAND_START || nr >= HLV_SIMULATOR_COMMAND_END)
		goto err_i1;

	ioctl = &hl_sim_mode_ioctls[nr];

	hl_size = _IOC_SIZE(ioctl->cmd);
	usize = asize = _IOC_SIZE(cmd);
	if (hl_size > asize)
		asize = hl_size;

	cmd = ioctl->cmd;

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		dev_dbg(sdev->dev, "no function\n");
		retcode = -EINVAL;
		goto err_i1;
	}

	if (cmd & (IOC_IN | IOC_OUT)) {
		if (asize <= sizeof(stack_kdata)) {
			kdata = stack_kdata;
		} else {
			kdata = kmalloc(asize, GFP_KERNEL);
			if (!kdata) {
				retcode = -ENOMEM;
				goto err_i1;
			}
		}
		if (asize > usize)
			memset(kdata + usize, 0, asize - usize);
	}

	if (cmd & IOC_IN) {
		if (copy_from_user(kdata, (void __user *)arg, usize)) {
			retcode = -EFAULT;
			goto err_i1;
		}
	} else if (cmd & IOC_OUT) {
		memset(kdata, 0, usize);
	}

	retcode = func(sdev, kdata);

	if (cmd & IOC_OUT)
		if (copy_to_user((void __user *)arg, kdata, usize))
			retcode = -EFAULT;

err_i1:
	if (!ioctl)
		dev_dbg(sdev->dev,
			"invalid ioctl: pid=%d, cmd=0x%02x, nr=0x%02x\n",
			task_pid_nr(current), cmd, nr);

	if (kdata != stack_kdata)
		kfree(kdata);

	if (retcode)
		dev_dbg(sdev->dev, "ret = %d\n", retcode);

	return retcode;
}

static const struct file_operations hl_sim_mode_ops = {
	.owner = THIS_MODULE,
	.open = hl_sim_mode_open,
	.release = hl_sim_mode_release,
	.unlocked_ioctl = hl_sim_mode_ioctl,
	.compat_ioctl = hl_sim_mode_ioctl,
};

int sim_mem_access_debug_handler(struct hl_device *hdev, void *info)
{
	struct hl_debug_args *args = (struct hl_debug_args *) info;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_debug_params_mem_access *data;
	u32 block_id, count = 0, regs_num;
	u32 *output, *inputs;
	void *user_data;
	int rc = 0;

	if (args->input_size != sizeof(struct hl_debug_params_mem_access)) {
		hl_err(hdev, "invalid input size\n");
		rc = -EINVAL;
		goto out;
	}

	user_data = kzalloc(args->input_size, GFP_KERNEL);
	if (!user_data) {
		rc = -ENOMEM;
		goto out;
	}

	if (copy_from_user(user_data, u64_to_user_ptr(args->input_ptr),
						args->input_size)) {
		hl_err(hdev, "failed to copy input debug data\n");
		rc = -EFAULT;
		goto free_user_data;
	}

	data = (struct hl_debug_params_mem_access *) user_data;

	if (data->size == 0 || !data->user_address || data->size > SZ_64K) {
		hl_err(hdev, "invalid request inputs\n");
		rc = -EINVAL;
		goto free_user_data;
	}

	rc = hdev->asic_funcs->get_hw_block_id(hdev, data->cfg_address, NULL,
						&block_id);
	if (rc) {
		hl_err(hdev, "failed to get block id for addr 0x%llx\n",
			data->cfg_address);
		rc = -EINVAL;
		goto free_user_data;
	}

	regs_num = data->size / sizeof(u32);

	switch (args->op) {
	case HL_DEBUG_OP_READMEM: {
		u32 *output_ptr;

		output = kzalloc(data->size, GFP_KERNEL);
		if (!output) {
			rc = -ENOMEM;
			goto free_user_data;
		}

		output_ptr = output;

		while (count < regs_num) {
			*output_ptr =
			RREG32(data->cfg_address - prop->cfg_base_address);
			data->cfg_address += sizeof(u32);
			output_ptr++;
			count++;
		}

		if (copy_to_user(u64_to_user_ptr(data->user_address),
				output, data->size)) {
			hl_err(hdev, "failed to copy output to user\n");
			rc = -EFAULT;
			kfree(output);
			goto free_user_data;
		}

		kfree(output);
	} break;

	case HL_DEBUG_OP_MEMCPY: {
		u32 *input_ptr;

		inputs = kzalloc(data->size, GFP_KERNEL);
		if (!inputs) {
			rc = -ENOMEM;
			goto free_user_data;
		}

		input_ptr = inputs;

		if (copy_from_user(inputs, u64_to_user_ptr(data->user_address),
				data->size)) {
			hl_err(hdev, "failed to copy input debug data\n");
			rc = -EFAULT;
			kfree(inputs);
			goto free_user_data;
		}

		while (count < regs_num) {
			WREG32(data->cfg_address - prop->cfg_base_address,
							*input_ptr);
			data->cfg_address += sizeof(u32);
			input_ptr++;
			count++;
		}
		kfree(inputs);
	} break;
	}

free_user_data:
	kfree(user_data);
out:
	return rc;
}

/**
 * hl_sim_init() - Initialize simulator mode.
 * @hclass: Pointer to class structure of the driver.
 * @major: Major number of the driver.
 * @hl_devs_idr: pointer to IDR of the devices
 * @hl_devs_idr_lock: pointer to mutex that protects access to devices IDR
 *
 * Create a generic simulator device file for a control channel between
 * simulators and the driver.
 *
 * Return: 1 for success, negative value for failure, and 0 if simulator mode is
 *         disabled.
 */
int hl_sim_init(struct class *hclass, u32 major, struct idr *hl_devs_idr,
		struct mutex *hl_devs_idr_lock)
{
	struct hl_sim_mode_device *sdev = &sim_mode_dev;
	unsigned int minor = HL_SIM_MAX_MINORS;
	dev_t devno = MKDEV(major, minor);
	char name[] = "hls";
	int rc;

	if ((sim_mode != 1) && (sim_mode != 2))
		return 0;

	sdev->hl_devs_idr = hl_devs_idr;
	sdev->hl_devs_idr_lock = hl_devs_idr_lock;
	sdev->single_msi = sim_single_msi;

	cdev_init(&sdev->cdev, &hl_sim_mode_ops);
	sdev->cdev.owner = THIS_MODULE;
	rc = cdev_add(&sdev->cdev, devno, 1);
	if (rc) {
		pr_err("habanalabs: Failed to add char device %s\n", name);
		return rc;
	}

	sdev->hclass = hclass;
	sdev->dev = device_create(sdev->hclass, NULL, devno, NULL, "%s", name);
	if (IS_ERR(sdev->dev)) {
		pr_err("habanalabs: Failed to create device %s\n", name);
		rc = PTR_ERR(sdev->dev);
		goto delete_cdev;
	}

	sdev->major = major;

	if (sim_mode == 2)
		pr_info("driver loaded with simulator support\n");

	return (sim_mode == 1 ? 1 : 0);

delete_cdev:
	cdev_del(&sdev->cdev);
	return rc;
}

/**
 * hl_sim_destroy_devices() - call simulator release methods.
 * this function is needed for cases where simulator
 * shutdown too early, even before opening a file descriptor
 * on hlv device. in such cases the device release function
 * won't be called and the hlv device will hang in there forever.
 * @sdev: pointer to simulator device.
 *
 * Return:  void
 */
static void hl_sim_destroy_devices(struct hl_sim_mode_device *sdev)
{
	int minor;

	for (minor = 0 ; minor < HL_SIM_MAX_MINORS ; minor++) {
		switch (sdev->virt_dev_type[minor]) {
		case ASIC_GOYA_SIM:
			goya_simulator_stop(minor);
			break;
		case ASIC_GAUDI_SIM:
		case ASIC_GAUDI_HL2000M_SIM:
			gaudi_simulator_stop(minor);
			break;
		case ASIC_GAUDI2_SIM:
		case ASIC_GAUDI2_HL_288_SIM:
		case ASIC_GAUDI2B_SIM:
		case ASIC_GAUDI2C_SIM:
		case ASIC_GAUDI2D_SIM:
		case ASIC_GAUDI2D_HL_288_SIM:
		case ASIC_GAUDI2E_SIM:
		case ASIC_GAUDI2E_HL_288_SIM:
		case ASIC_GAUDI2_SIM_ARC:
		case ASIC_GAUDI2_HL_288_SIM_ARC:
		case ASIC_GAUDI2B_SIM_ARC:
		case ASIC_GAUDI2C_SIM_ARC:
		case ASIC_GAUDI2D_SIM_ARC:
		case ASIC_GAUDI2D_HL_288_SIM_ARC:
		case ASIC_GAUDI2E_SIM_ARC:
		case ASIC_GAUDI2E_HL_288_SIM_ARC:
			gaudi2_simulator_stop(minor);
			break;
		case ASIC_GAUDI3_SIM:
		case ASIC_GAUDI3D_SIM:
		case ASIC_GAUDI3E_SIM:
		case ASIC_GAUDI3_SIM_ARC:
		case ASIC_GAUDI3D_SIM_ARC:
		case ASIC_GAUDI3E_SIM_ARC:
		case ASIC_GAUDI3_HL_338_SIM:
		case ASIC_GAUDI3D_HL_338_SIM:
		case ASIC_GAUDI3E_HL_338_SIM:
		case ASIC_GAUDI3_HL_338_SIM_ARC:
		case ASIC_GAUDI3D_HL_338_SIM_ARC:
		case ASIC_GAUDI3E_HL_338_SIM_ARC:
			gaudi3_simulator_stop(minor);
			break;
		default:
			continue;
		}

		hl_sim_remove(minor);
	}
}

/**
 * hl_sim_fini() - Exit simulator mode and destroy simulator devices.
 *
 * Return: 1 for success and 0 if simulator mode is disabled.
 */
int hl_sim_fini(void)
{
	struct hl_sim_mode_device *sdev = &sim_mode_dev;

	if (!sim_mode)
		return 0;

	/* Hide simulator mode device from user */
	device_destroy(sdev->hclass, sdev->dev->devt);
	cdev_del(&sdev->cdev);

	hl_sim_destroy_devices(sdev);

	return (sim_mode == 1 ? 1 : 0);
}

/* Should be called while the hl_devs_idr lock is taken */
static void hl_sim_invalidate_minor_locked(struct hl_sim_mode_device *sdev, u32 minor)
{
	sdev->virt_dev_type[minor] = ASIC_INVALID;
}

/**
 * hl_sim_remove() - Mark simulator as inactive and release its minor number.
 * @minor: Minor number of the simulator.
 */
void hl_sim_remove(u32 minor)
{
	struct hl_sim_mode_device *sdev = &sim_mode_dev;

	mutex_lock(sdev->hl_devs_idr_lock);
	hl_sim_invalidate_minor_locked(sdev, minor);
	hl_sim_release_minor_locked(sdev, minor);
	mutex_unlock(sdev->hl_devs_idr_lock);
}

/**
 * hl_sim_create_hdev - wrapper for an habanalabs device instance creation.
 * @edev: pointer to simulator device structure.
 *
 * Wrapper around create_hdev() for simulator.
 * Internally adds a platform device which is used a parent device for the DRM device
 * initialization.
 *
 * Return: 0 for success, negative value for failure.
 */
int hl_sim_create_hdev(struct hl_simulator_device *edev)
{
	int rc, minor = edev->id - HLV_SIM_ID_OFFSET;

	edev->plat_dev = platform_device_register_simple(edev->name, PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(edev->plat_dev))
		return PTR_ERR(edev->plat_dev);

	if (!devres_open_group(&edev->plat_dev->dev, NULL, GFP_KERNEL)) {
		rc = -ENOMEM;
		goto unregister_plat_device;
	}

	rc = create_hdev(&edev->hdev, NULL, &edev->plat_dev->dev, edev->virt_dev_type, minor);
	if (rc)
		goto release_devres_group;

	edev->hdev->sdev = edev->dev;
	platform_set_drvdata(edev->plat_dev, edev->hdev);

	return 0;

release_devres_group:
	devres_release_group(&edev->plat_dev->dev, NULL);
unregister_plat_device:
	platform_device_unregister(edev->plat_dev);
	return rc;
}

/**
 * hl_sim_destroy_hdev - wrapper for an habanalabs device instance teardown.
 * @hdev: pointer to habanalabs device structure
 *
 * Wrapper around the habanalabs device instance teardown.
 * Unregister the platform device which was used as a parent device of the DRM device.
 * If the accel subsystem is enabled, this unregisteration will lead to releasing devres entries,
 * including freeing the habanalabs device instance. If it is not enabled, call kfree explicitly.
 */
void hl_sim_destroy_hdev(struct hl_device *hdev)
{
	struct hl_simulator_device *edev =
			container_of(hdev->sdev, struct hl_simulator_device, sdev);

	devres_release_group(&edev->plat_dev->dev, NULL);
	platform_device_unregister(edev->plat_dev);

#if !IS_ENABLED(CONFIG_DRM_ACCEL)
	kfree(hdev);
#endif
}

/**
 * hl_sim_vunmap_user_pages() - Teardown previously initialized hl_vm_user_pages.
 * @user_pages: The user pages structure to destroy.
 */
void hl_sim_vunmap_user_pages(struct hl_vm_user_pages *user_pages)
{
	struct mm_struct *owner_mm;
	long i;

	if (user_pages->pages) {
		if (user_pages->vaddr) {
			vunmap(user_pages->vaddr);

			owner_mm = user_pages->owner_mm;
			user_pages->owner_mm = NULL;
			user_pages->tsk = NULL;

			if (owner_mm)
				mmdrop(owner_mm);
			/* From this point user pages cannot be accessed */
		}
		for (i = 0; i < user_pages->nr_pages; ++i)
			if (user_pages->pages[i])
				put_page(user_pages->pages[i]);
		vfree(user_pages->pages);
	}
	user_pages->nr_pages = 0;
	user_pages->pages = NULL;
	user_pages->vaddr = NULL;
}

/**
 * hl_sim_vmap_user_pages() - Create a mapping to supplied user memory.
 * @user_pointer: Pointer to current context user space process
 * @size_in_bytes: Memory size in bytes
 * @out_user_pages: Output object to be generated
 * @locked: Is mmap_lock taken?
 *
 * This function will pin user pages, and create a virtual pointer to
 * user memory buffer start, mapped to kernel space.
 * In addition, it will grab current mm structure to prevent user memory to
 * be released, and save all the information required to access extra user
 * memory,that was not mapped, on demand.
 *
 * Return: 0 on success or error code on failure.
 */
int hl_sim_vmap_user_pages(u64 user_pointer, u64 size_in_bytes,
			struct hl_vm_user_pages *out_user_pages, bool locked)
{
	u64 nr_pages;
	int rc;

	if (user_pointer & ~PAGE_MASK) {
		pr_err("User pointer must be page aligned\n");
		rc = -EINVAL;
		goto err;
	}
	nr_pages = size_in_bytes >> PAGE_SHIFT;

	out_user_pages->pages =
		vzalloc(sizeof(*out_user_pages->pages) * nr_pages);
	if (!out_user_pages->pages) {
		rc = -ENOMEM;
		goto err;
	}

	if (!locked) /* not running from mmap context */
		mmap_read_lock(current->mm);

#ifndef _HAS_GET_USER_PAGES_WITH_TASK_PTR
	out_user_pages->nr_pages = get_user_pages(user_pointer, nr_pages,
#ifdef _HAS_FOLL_POPULATE
						FOLL_POPULATE |
#endif
						FOLL_WRITE,
#ifndef _HAS_GET_USER_PAGES_WITH_VMAS
						out_user_pages->pages);
#else
						out_user_pages->pages, NULL);
#endif
#else /* _HAS_GET_USER_PAGES_WITH_TASK_PTR */
	out_user_pages->nr_pages =
		get_user_pages(current, current->mm, user_pointer,
				nr_pages, true, false,
				out_user_pages->pages, NULL);
#endif
	if (!locked)
		mmap_read_unlock(current->mm);

	if (out_user_pages->nr_pages != nr_pages) {
		pr_err("Unable to pin all user pages (%ld)\n", out_user_pages->nr_pages);
		rc = -EACCES;
		goto err;
	}

	out_user_pages->vaddr = vmap(out_user_pages->pages,
					out_user_pages->nr_pages,
					VM_MAP | VM_USERMAP, PAGE_KERNEL);
	if (!out_user_pages->vaddr) {
		pr_err("vmap failed\n");
		rc = -ENOMEM;
		goto err;
	}

	out_user_pages->owner_mm = current->mm;
	out_user_pages->tsk = current;
	mmgrab(out_user_pages->owner_mm);

	return 0;
err:
	hl_sim_vunmap_user_pages(out_user_pages);
	return rc;
}

/**
 * hl_sim_copy_user_remote() - Copy data to/from non-current user process
 * @user_pages: User pages data structure describing the user data
 * @usr_addr: User space address
 * @kern_addr: Kernel space address
 * @size: Size of data
 * @to_user: True if copy from kernel to user, false if from user to kernel
 *
 * This function is similar to copy_to_user, however it does not require to
 * be run from the same user context. All information required to access the
 * user process is saved in the user_pages structure.
 *
 * This function will grab the user process memory context, find the pages
 * it needs to pin, pin them, map them into one buffer, do the copy, and
 * undo all the mappings/pins.
 *
 * Return: number of bytes that could not be copied
 */
static unsigned long
hl_sim_copy_user_remote(struct hl_vm_user_pages *user_pages, u64 usr_addr,
			u64 kern_addr, unsigned long size, bool to_user)
{
	u64 start_page_addr, end_page_addr, nr_pages, nr_pages_pinned;
	struct mm_struct *owner_mm;
	unsigned long copied = 0;
	struct page **pages;
	void *vmap_addr;
	int i, locked;

	/* Assume owner_mm exists. Since we are called from the context
	 * of debugfs, which is removed before simulator release,
	 * this assumption is valid. If in the future this behavior changes,
	 * onwner_mm existence must be assured.
	 */
	owner_mm = user_pages->owner_mm;

	start_page_addr = usr_addr & PAGE_MASK;
	end_page_addr = (usr_addr + size) & PAGE_MASK;
	nr_pages = ((end_page_addr - start_page_addr) >> PAGE_SHIFT) + 1;

	pages = vzalloc(nr_pages * sizeof(*pages));
	if (!pages)
		goto out;

	mmap_read_lock(owner_mm);
	locked = 1;
#ifdef _HAS_GET_USER_PAGES_REMOTE
#ifdef _HAS_GET_USER_PAGES_REMOTE_WITH_TASK_PTR
#ifdef _HAS_GET_USER_PAGES_WITH_GUP_FLAGS
#ifdef _HAS_GET_USER_PAGES_REMOTE_LOCKED
	nr_pages_pinned =
		get_user_pages_remote(user_pages->tsk, owner_mm, start_page_addr, nr_pages,
					FOLL_WRITE | FOLL_POPULATE, pages, NULL,
					&locked);
#else /* _HAS_GET_USER_PAGES_REMOTE_LOCKED */
	nr_pages_pinned =
		get_user_pages_remote(user_pages->tsk, owner_mm, start_page_addr, nr_pages,
				FOLL_WRITE | FOLL_POPULATE, pages, NULL);
#endif
#else /* _HAS_GET_USER_PAGES_WITH_GUP_FLAGS */
	nr_pages_pinned =
		get_user_pages_remote(user_pages->tsk, owner_mm, start_page_addr, nr_pages,
					true, false, pages, NULL);
#endif /* ifdef _HAS_GET_USER_PAGES_WITH_GUP_FLAGS */
#else /* _HAS_GET_USER_PAGES_REMOTE_WITH_TASK_PTR */
	nr_pages_pinned =
		get_user_pages_remote(owner_mm, start_page_addr, nr_pages,
#ifdef _HAS_FOLL_POPULATE
					FOLL_POPULATE |
#endif
					FOLL_WRITE, pages,
#ifdef _HAS_GET_USER_PAGES_WITH_VMAS
					NULL,
#endif
					&locked);
#endif /* _HAS_GET_USER_PAGES_REMOTE_WITH_TASK_PTR */
#else /* _HAS_GET_USER_PAGES_REMOTE */
	nr_pages_pinned = get_user_pages(user_pages->tsk, owner_mm, start_page_addr,
					 nr_pages, true, false, pages, NULL);
#endif /* _HAS_GET_USER_PAGES_REMOTE */
	if (locked)
		mmap_read_unlock(owner_mm);
	if (nr_pages_pinned != nr_pages) {
		pr_err("Could not pin %lld pages (%lld)\n", nr_pages,
			nr_pages_pinned);
		goto free_pages;
	}

	vmap_addr =
		vmap(pages, nr_pages_pinned, VM_MAP | VM_USERMAP, PAGE_KERNEL);
	if (!vmap_addr) {
		pr_err("Could not vmap pages\n");
		goto put_pages;
	}

	if (to_user)
		memcpy(vmap_addr + (usr_addr - start_page_addr),
			(void *)kern_addr, size);
	else
		memcpy((void *)kern_addr,
			vmap_addr + (usr_addr - start_page_addr), size);
	copied = size;

	vunmap(vmap_addr);
put_pages:
	for (i = 0; i < nr_pages_pinned; ++i)
		put_page(pages[i]);
free_pages:
	vfree(pages);
out:
	return size - copied;
}

/**
 * hl_sim_poll - poll simulator fifos in order to find if data is available
 * @edev: pointer to extended simulator device struct.
 * @filp: file structure pointer for device access
 * @wait: poll table structure pointer for which the driver adds a wait queue
 *
 * Returns a mask representing the data available.
 */
__poll_t hl_sim_poll(struct hl_simulator_device *edev, struct file *filp,
				struct poll_table_struct *wait)
{
	__poll_t mask = 0;

	poll_wait(filp, &edev->pollq, wait);
	if (!kfifo_is_empty(&edev->h2c_fifo))
		mask |= (EPOLLIN | EPOLLRDNORM);
	return mask;
}

/**
 * hl_sim_read_dram - Read DRAM through simulated PCI bar
 * @edev: pointer to extended simulator device struct.
 * @dst: Destination address to write result into
 * @offset: PCI-E bar offset (in bytes)
 * @size: The size of the data to read
 *
 * Copies the value of DRAM at the given offset to @dst.
 * The memory may be mapped into kernel space, or needs to be mapped.
 * Returns 0 on success or error code on error.
 *
 */
int hl_sim_read_dram(struct hl_simulator_device *edev, void *dst, u64 offset,
			u64 size)
{
	struct hl_vm_user_pages *user_pages = &edev->user_sram_dram;
	u64 not_copied;

	if (!edev->dram_user_provided_ptr) {
		/* Legacy - memory belongs to the kernel.
		 * Cannot use pcie_bar with memcpy as this triggers static code
		 * analysis error, pcie_bar is defined as __iomem (which is
		 * not true in simulator) specifically. So use dram directly.
		 */
		memcpy(dst, edev->dram + offset, size);
		return 0;
	}

	if (offset + edev->dram_off_in_user_sram_dram <= user_pages->nr_pages * PAGE_SIZE - size) {
		/* fully inside the mapped region or DRAM is in kernel space */
		memcpy(dst, user_pages->vaddr + edev->dram_off_in_user_sram_dram + offset, size);
		return 0;
	}

	not_copied =
		hl_sim_copy_user_remote(&edev->user_sram_dram,
					edev->dram_user_provided_ptr + offset,
					(u64)dst, size, false);

	if (not_copied)
		return -EIO;
	return 0;
}

/**
 * hl_sim_write_dram - Write to DRAM through simulated PCI bar
 * @edev: pointer to extended simulator device struct
 * @offset: PCI-E bar offset (in bytes)
 * @src: destination address to write result into
 * @size: the size of the data to read
 *
 * Copies a value to DRAM at a given offset from @src.
 * The memory may be mapped into kernel space, or needs to be mapped.
 * Returns 0 on success or error code on error.
 */
int hl_sim_write_dram(struct hl_simulator_device *edev, u64 offset,
			const void *src, u64 size)
{
	struct hl_vm_user_pages *user_pages = &edev->user_sram_dram;
	u64 not_copied;

	if (!edev->dram_user_provided_ptr) {
		/* Legacy - memory belongs to the kernel.
		 * Cannot use pcie_bar with memcpy as this triggers static code
		 * analysis error, pcie_bar is defined as __iomem (which is
		 * not true in simulator) specifically. So use dram directly.
		 */
		memcpy(edev->dram + offset, src, size);
		return 0;
	}

	if (offset + edev->dram_off_in_user_sram_dram <= user_pages->nr_pages * PAGE_SIZE - size) {
		/* fully inside the mapped region or DRAM is in kernel space */
		memcpy(user_pages->vaddr + edev->dram_off_in_user_sram_dram + offset, src, size);
		return 0;
	}

	not_copied =
		hl_sim_copy_user_remote(&edev->user_sram_dram,
					edev->dram_user_provided_ptr + offset,
					(u64)src, size, true);

	if (not_copied)
		return -EIO;

	return 0;
}

ssize_t hl_sim_read_h2c_fifo(struct hl_simulator_device *edev, struct file *filp,
				char __user *buffer, size_t len)
{
	struct simulator_msg *msg;
	size_t total_data_copied;
	int rc, count;

	if (signal_pending(current))
		return -EINTR;

try_h2c:
	spin_lock(&edev->h2c_lock);
	count = kfifo_out(&edev->h2c_fifo, &msg, sizeof(msg));
	spin_unlock(&edev->h2c_lock);

	if (count == 0) {
		if (filp->f_flags & O_NONBLOCK)
			return 0;

		usleep_range((1000 >> 2) + 1, 1000);

		if (signal_pending(current))
			return -EINTR;

		goto try_h2c;
	}

	rc = copy_to_user(buffer, msg, sizeof(struct simulator_msg));
	total_data_copied = sizeof(*msg) - rc;
	kfree(msg);

	if (unlikely(rc))
		dev_err(edev->dev, "Failed to copy msg to user\n");

	return rc == 0 ? total_data_copied : rc;
}

ssize_t hl_sim_write_c2h_fifo(struct hl_simulator_device *edev, struct file *filp,
				const char __user *buffer, size_t len)
{
	struct simulator_msg *msg;
	size_t copy_to_request_size;
	bool pushed;
	int rc, tries;

	if (signal_pending(current))
		return -EINTR;

	msg = kzalloc(sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	copy_to_request_size =
			min_t(size_t, len, sizeof(struct simulator_msg));
	rc = copy_from_user(msg, buffer, copy_to_request_size);
	if (unlikely(rc)) {
		dev_err(edev->dev, "Failed to copy msg from user\n");
		goto err_out;
	}

	tries = 1000;

try_c2h:
	spin_lock(&edev->c2h_lock);
	pushed = kfifo_in(&edev->c2h_fifo, &msg, sizeof(msg)) == sizeof(msg);
	spin_unlock(&edev->c2h_lock);

	if (!pushed) {
		tries--;
		if (tries == 0) {
			dev_crit(edev->dev, "Failed to push msg to c2h fifo\n");
			goto err_out;
		}

		usleep_range((1000 >> 2) + 1, 1000);

		if (signal_pending(current))
			return -EINTR;

		goto try_c2h;
	}

	return copy_to_request_size;

err_out:
	kfree(msg);
	return -EINVAL;
}

static void sim_device_release_func(struct device *dev)
{
	struct hl_simulator_device *edev =
			container_of(dev, struct hl_simulator_device, sdev);

	kfree(edev->irq_mutex);
	kfree(edev);
}

void sim_devices_init(struct hl_simulator_device *sim_dev, struct class *hclass,
			int minor, const struct file_operations *fops, char *name)
{
	struct device *dev = &sim_dev->sdev;
	struct cdev *cdev = &sim_dev->cdev;

	cdev_init(cdev, fops);
	cdev->owner = THIS_MODULE;

	device_initialize(dev);
	(dev)->devt = MKDEV(sim_dev->major, minor);
	(dev)->class = hclass;
	(dev)->release = sim_device_release_func;
	dev_set_name(dev, "%s", name);

	sim_dev->dev = dev;
}

static struct simulator_shared_mem_block *hl_sim_alloc_shared_block(u64 block_size)
{
	struct simulator_shared_mem_block *shared_block;
	int num_pages = block_size / PAGE_SIZE;

	shared_block = kzalloc(sizeof(*shared_block), GFP_KERNEL);
	if (!shared_block)
		return NULL;

	shared_block->pfn_arr = kcalloc(num_pages, sizeof(*shared_block->pfn_arr), GFP_KERNEL);

	if (!shared_block->pfn_arr) {
		kfree(shared_block);
		return NULL;
	}

	shared_block->num_pages = num_pages;
	return shared_block;
}

void hl_sim_free_shared_block(struct simulator_shared_mem_block *shared_block, bool refcount)
{
	struct page *page;
	int i;

	if (refcount) {
		for (i = 0; i < shared_block->num_pages; i++) {
			page = pfn_to_page(shared_block->pfn_arr[i]);
			page_ref_dec(page);
		}
	}

	if (shared_block->num_pages > 0)
		kfree(shared_block->pfn_arr);

	kfree(shared_block);
}

int hl_sim_create_shared_block(struct hl_simulator_device *edev,
				struct simulator_memory_args *args)
{
	struct hl_mmu_hop_info hops_info = {};
	struct simulator_shared_mem_block *shared_block;
	u64 virt_addr = args->device_address, out_handle;
	struct hl_device *hdev = edev->hdev;
	struct page *page;
	struct hl_ctx *ctx;
	int rc = 0, handle, i, err_idx;

	if (!args->size || args->size % PAGE_SIZE || args->size > HL_SIM_MAX_SHARED_BLOCK_SIZE) {
		dev_err(edev->dev, "invalid size for shared block. size: %llu\n", args->size);
		return -EINVAL;
	}

	ctx = hl_get_compute_ctx(hdev);
	if (!ctx) {
		dev_err(edev->dev, "Can't get compute context\n");
		return -EINVAL;
	}

	shared_block = hl_sim_alloc_shared_block(args->size);
	if (!shared_block) {
		rc = -ENOMEM;
		goto put_ctx;
	}

	/* Use the idr with start value as '1', to support current .mmap,
	 * which uses the 'offset' parameter and refers offset - 0 value
	 * to memory allocations (legacy code).
	 */
	mutex_lock(&edev->shared_block_idr_mutex);
	handle = idr_alloc(&edev->shared_block_idr, shared_block, 1, 0,
				GFP_KERNEL);
	mutex_unlock(&edev->shared_block_idr_mutex);

	if (handle < 0) {
		dev_err(edev->dev, "Failed to get handle for shared block\n");
		rc = -EINVAL;
		goto free_shared_block;
	}

	out_handle = (u64)handle << PAGE_SHIFT;
	dev_dbg(edev->dev, "create shared block: virt: %#llx size: %llu handle: 0x%llx (0x%x)\n",
			virt_addr, args->size, out_handle, handle);

	for (i = 0 ; i < shared_block->num_pages ; i++) {
		mutex_lock(&hdev->mmu_lock);
		rc = hl_mmu_hr_get_tlb_info(ctx, virt_addr + (i * PAGE_SIZE), &hops_info,
			&ctx->hdev->mmu_func[MMU_HR_PGT].hr_funcs);
		mutex_unlock(&hdev->mmu_lock);
		if (rc) {
			dev_err(edev->dev, "failed to get tlb info. virt: %#llx\n",
					virt_addr + (i * PAGE_SIZE));
			goto remove_idr;
		}

		shared_block->pfn_arr[i] = hops_info.unscrambled_paddr >> PAGE_SHIFT;

		/* until the release block is called, hold a ref count to avoid page free.
		 * In case page ref count is zero, it probably means that page was
		 * unmapped and freed, right after get_tlb_info was called.
		 */
		page = pfn_to_page(shared_block->pfn_arr[i]);
		if (!get_page_unless_zero(page)) {
			dev_err(edev->dev, "Failed to get page-%d. %#llx\n",
					i, shared_block->pfn_arr[i]);

			rc = -EINVAL;
			goto remove_idr;
		}

		dev_dbg(edev->dev, "	page-%d: address - %#llx\n", i, shared_block->pfn_arr[i]);
	}

	hl_ctx_put(ctx);
	args->handle = out_handle;

	return 0;

remove_idr:
	err_idx = i;
	for (i = 0; i < err_idx; i++) {
		page = pfn_to_page(shared_block->pfn_arr[i]);
		page_ref_dec(page);
	}

	mutex_lock(&edev->shared_block_idr_mutex);
	idr_remove(&edev->shared_block_idr, handle);
	mutex_unlock(&edev->shared_block_idr_mutex);

free_shared_block:
	hl_sim_free_shared_block(shared_block, false);

put_ctx:
	hl_ctx_put(ctx);
	return rc;
}

int hl_sim_release_shared_block(struct hl_simulator_device *edev,
				struct simulator_memory_args *args)
{
	struct simulator_shared_mem_block *shared_block;
	int handle = args->handle >> PAGE_SHIFT;

	mutex_lock(&edev->shared_block_idr_mutex);
	shared_block = idr_find(&edev->shared_block_idr, handle);
	if (!shared_block) {
		mutex_unlock(&edev->shared_block_idr_mutex);
		dev_err(edev->dev, "Failed to release shared block, no such handle 0x%llx\n",
					args->handle);
		return -EINVAL;
	}

	idr_remove(&edev->shared_block_idr, handle);
	mutex_unlock(&edev->shared_block_idr_mutex);

	hl_sim_free_shared_block(shared_block, true);

	return 0;
}
