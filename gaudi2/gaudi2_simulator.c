// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#define pr_fmt(fmt)			"habanalabs: " fmt

#include "gaudi2P.h"
#include "gaudi2_masks.h"
#include "gaudi2_cn.h"
#include "../common/simulator.h"
#include "../include/common/simulator.h"
#include "../include/hw_ip/mmu/mmu_general.h"
#include "../include/common/pci_ids.h"
#include "../include/gaudi2/gaudi2_reg_map.h"
#include "../include/hw_ip/nic/nic_general.h"

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/hwmon.h>

#define PCIE_WRAP_PSOC_BOOT_MNG_DONE_PSOC_BOOT_MNG_DONE_MASK 0x1
#define PCIE_WRAP_PSOC_RST_CTRL_HARD_RST_MASK 0x1
#define PCIE_WRAP_PSOC_RST_CTRL_SOFT_RST_MASK 0x2

/* DRAM Memory Map */
#define GAUDI2_SIM_NIC_DRV_SIZE		0x20000000ull	/* 512MB */

#define GAUDI2_SIM_NIC_DRV_ADDR		DRAM_PHYS_BASE
#define GAUDI2_SIM_DRAM_DRV_END_ADDR	(GAUDI2_SIM_NIC_DRV_ADDR + \
						GAUDI2_SIM_NIC_DRV_SIZE)

/* Highest possible user address, user address might be lower than this */
#define GAUDI2_SIM_DRAM_BASE_ADDR_USER	(DRAM_PHYS_BASE + 0x20000000ull)

#if (GAUDI2_SIM_DRAM_DRV_END_ADDR > GAUDI2_SIM_DRAM_BASE_ADDR_USER)
#error "SIM driver must reserve no more than 512MB"
#endif

/* All the code below this point is the gaudi simulator device implementation */

static DEFINE_MUTEX(simulator_open);

static struct hl_simulator_device *gaudi2_simulator_dev_table[HL_MAX_MINORS];

static struct attribute *gaudi2_sim_dev_attrs[] = {
	NULL,
};

static int gaudi2_sim_access_dev_mem(struct hl_device *hdev, enum pci_region reg_type,
				u64 addr, u64 *val, enum debugfs_access_type acc_type)
{
	return hl_sim_access_dev_mem(hdev, gaudi2_simulator_dev_table[hdev->id],
			reg_type, addr, val, acc_type);
}

static int gaudi2_sim_start_device(struct hl_simulator_device *edev);

static void gaudi2_simulator_create_device(struct work_struct *work)
{
	struct hl_simulator_device *edev =
			container_of(work, struct hl_simulator_device, work_create.work);
	int rc;

	dev_dbg(edev->dev, "Starting delayed work to create simulated device\n");

	rc = gaudi2_sim_start_device(edev);
	if (rc) {
		/* Set hdev to NULL to prevent a call to gaudi2_sim_stop_device() */
		edev->hdev = NULL;
		dev_err(edev->dev, "Failed to create Gaudi2 Simulator device\n");
	}
}

static int gaudi2_simulator_open(struct inode *inode, struct file *filp)
{
	u32 minor = iminor(inode) - HLV_SIM_ID_OFFSET;
	struct hl_simulator_device *edev;
	int rc = 0;

	if (minor >= HL_MAX_MINORS) {
		pr_crit("habanalabs: minor is out of bounds %u, can't open sim\n", minor);
		return -EINVAL;
	}

	mutex_lock(&simulator_open);

	edev = gaudi2_simulator_dev_table[minor];

	if (!edev) {
		pr_err("habanalabs: Device %d:%d is not yet ready for work\n",
			imajor(inode), iminor(inode));
		rc = -EPERM;
		goto unlock_mutex;
	}

	if (edev->open) {
		dev_err(edev->dev,
			"Device %s is/was already attached to func-sim\n",
			edev->name);
		dev_err(edev->dev,
			"If func-sim is not running, rmmod and insmod habanalabs.ko before running func-sim again\n");
		rc = -EPERM;
		goto unlock_mutex;
	}

	edev->open = 1;

	dev_dbg(edev->dev,
		"Opening file descriptor on gaudi2 simulator device\n");

	filp->private_data = edev;
	nonseekable_open(inode, filp);

	mutex_unlock(&simulator_open);

	if ((edev->sram_user_address) && (edev->dram_user_address)) {
		dev_dbg(edev->dev,
			"Creating delayed work to start simulated device\n");
		INIT_DELAYED_WORK(&edev->work_create,
					gaudi2_simulator_create_device);
		schedule_delayed_work(&edev->work_create,
					usecs_to_jiffies(1000));
	}

	return 0;

unlock_mutex:
	mutex_unlock(&simulator_open);
	return rc;
}

static int gaudi2_simulator_release(struct inode *inode, struct file *filp)
{
	struct hl_simulator_device *edev = filp->private_data;
	u32 minor = iminor(inode) - HLV_SIM_ID_OFFSET;

	edev->open = 0;

	if (edev->hdev) {
		edev->hdev->disabled = true;
		edev->hdev->simulator_crashed = true;
		dev_warn(edev->dev,
			"Simulator was closed, shouldn't use the accel%d device!\n",
#if IS_ENABLED(CONFIG_DRM_ACCEL)
			edev->hdev->id);
#else
			edev->hdev->id / 2);
#endif
		hl_sim_notify_simulator_close(edev->hdev);
	}

	gaudi2_simulator_stop(minor);
	hl_sim_remove(minor);

	return 0;
}

static ssize_t gaudi2_simulator_read(struct file *filp, char __user *buffer,
		size_t len, loff_t *off)
{
	struct hl_simulator_device *edev = filp->private_data;

	dev_dbg_once(edev->dev, "Reading %zu bytes from %s in offset 0x%llx\n",
			len, filp->f_path.dentry->d_name.name, *off);

	return hl_sim_read_h2c_fifo(edev, filp, buffer, len);
}

static __poll_t gaudi2_simulator_poll(struct file *filp,
				struct poll_table_struct *wait)
{
	return hl_sim_poll(filp->private_data, filp, wait);
}

static ssize_t gaudi2_simulator_write(struct file *filp,
		const char __user *buff, size_t len, loff_t *off)
{
	struct hl_simulator_device *edev = filp->private_data;

	dev_dbg_once(edev->dev, "Writing %zu bytes to %s in offset 0x%llx\n",
			len, filp->f_path.dentry->d_name.name, *off);

	return hl_sim_write_c2h_fifo(edev, filp, buff, len);
}

static int gaudi2_simulator_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct hl_simulator_device *edev = filp->private_data;
	void *address;
	u64 *address_to_update;
	unsigned long size = vma->vm_end - vma->vm_start;
	int rc;

	if (vma->vm_pgoff) {
		dev_err(edev->dev, "mmap offset 0x%lx invalid, must be 0\n",
			vma->vm_pgoff);
		return -EINVAL;
	}

	if (size == SRAM_SIZE) {
		if (edev->sram_user_address) {
			dev_err(edev->dev,
				"CFG is already mapped to userspace!!!\n");
			return -EINVAL;
		}
		address = edev->sram;
		address_to_update = &edev->sram_user_address;
		dev_dbg(edev->dev, "mapping CFG:\n");
	} else if (size == edev->dram_size) {
		if (edev->dram_user_provided_ptr) {
			dev_err(edev->dev,
				"DRAM is already allocated in userspace\n");
			return -EINVAL;
		}
		if (edev->dram_user_address) {
			dev_err(edev->dev,
				"DRAM is already mapped to userspace!!!\n");
			return -EINVAL;
		}
		address = edev->dram;
		address_to_update = &edev->dram_user_address;
		dev_dbg(edev->dev, "mapping HBM:\n");
	} else {
		dev_err(edev->dev, "mmap size illegal 0x%lx\n", size);
		return -EINVAL;
	}

	*address_to_update = vma->vm_start;

	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY | VM_NORESERVE);

	dev_dbg(edev->dev, "  user address   == 0x%08lx\n",
		vma->vm_start);
	dev_dbg(edev->dev, "  virt address   == 0x%px\n", address);
	dev_dbg(edev->dev, "  vm_flags       == 0x%04lx\n",
		vma->vm_flags);
	dev_dbg(edev->dev, "  size           == 0x%04lx\n", size);
	dev_dbg(edev->dev, "  vm_page_prot   == 0x%08lx\n",
					pgprot_val(vma->vm_page_prot));

	rc = remap_vmalloc_range(vma, address, vma->vm_pgoff);
	if (rc) {
		dev_err(edev->dev, "remap vmalloc error %d", rc);
		*address_to_update = 0;
		return rc;
	}

	if ((edev->sram_user_address) && (edev->dram_user_address)) {
		dev_dbg(edev->dev,
			"Creating delayed work to start simulated device\n");
		INIT_DELAYED_WORK(&edev->work_create,
					gaudi2_simulator_create_device);
		schedule_delayed_work(&edev->work_create,
					usecs_to_jiffies(1000));
	}

	return rc;
}

static int gaudi2_simulator_gen_int_ioctl(struct hl_simulator_device *edev, void *data)
{
	struct simulator_gen_int_args *args = data;
	struct gaudi2_cn_aux_ops *gaudi2_aux_ops;
	struct hl_user_interrupt *user_interrupt;
	struct hl_device *hdev = edev->hdev;
	int nic_eq_interrupt, int_idx;
	struct gaudi2_device *gaudi2;
	struct hl_aux_dev *aux_dev;
	struct hl_dec *dec;
	struct hl_cq *cq;
	u32 relative_idx;

	gaudi2 = hdev->asic_specific;
	aux_dev = &hdev->cn.cn_aux_dev;
	gaudi2_aux_ops = &gaudi2->cn_aux_ops;

	if (args->id >= GAUDI2_MSIX_ENTRIES) {
		dev_err(edev->dev, "interrupt id %d invalid", args->id);
		return -EINVAL;
	}

	mutex_lock(&edev->irq_mutex[args->id]);

	if (unlikely(edev->reset))
		goto out;

	switch (args->id) {
	case GAUDI2_IRQ_NUM_EVENT_QUEUE:
		hl_irq_handler_eq(args->id, &hdev->event_queue);
		break;
	case GAUDI2_IRQ_NUM_DCORE0_DEC0_NRM ... GAUDI2_IRQ_NUM_SHARED_DEC1_ABNRM:
		relative_idx = args->id - GAUDI2_IRQ_NUM_DCORE0_DEC0_NRM;
		dec = hdev->dec + relative_idx / 2;

		if (relative_idx % 2) {
			hl_irq_handler_dec_abnrm(args->id, dec);
		} else {
			if (hdev->user_interrupt[dec->core_id].interrupt_id != args->id) {
				dev_err(hdev->dev,
					"decoder irq %d from sim doesn't match decoder interrupt %d\n",
					args->id, hdev->user_interrupt[dec->core_id].interrupt_id);
				goto out;
			}

			hl_irq_user_interrupt_handler(args->id,
					&hdev->user_interrupt[dec->core_id]);
		}
		break;
	case GAUDI2_IRQ_NUM_COMPLETION:
		cq = &hdev->completion_queue[GAUDI2_RESERVED_CQ_CS_COMPLETION];
		hl_irq_handler_cq(args->id, cq);
		break;
	case GAUDI2_IRQ_NUM_NIC_PORT_FIRST ... GAUDI2_IRQ_NUM_NIC_PORT_LAST:
		nic_eq_interrupt = args->id - GAUDI2_IRQ_NUM_NIC_PORT_FIRST;

		if (gaudi2_aux_ops->eq_irq_handler)
			gaudi2_aux_ops->eq_irq_handler(aux_dev, nic_eq_interrupt);
		break;
	case GAUDI2_IRQ_NUM_TPC_ASSERT:
		hl_irq_user_interrupt_thread_handler(args->id, &hdev->tpc_interrupt);
		break;
	case GAUDI2_IRQ_NUM_UNEXPECTED_ERROR:
		hl_irq_user_interrupt_thread_handler(args->id, &hdev->unexpected_error_interrupt);
		break;
	case GAUDI2_IRQ_NUM_USER_FIRST ... GAUDI2_IRQ_NUM_USER_LAST:
		int_idx = args->id - GAUDI2_IRQ_NUM_USER_FIRST +
				hdev->asic_prop.user_dec_intr_count;

		user_interrupt = &hdev->user_interrupt[int_idx];

		if (user_interrupt->interrupt_id != args->id) {
			dev_err(hdev->dev, "irq %d from sim doesn't match user interrupt %d\n",
				args->id, user_interrupt->interrupt_id);
			goto out;
		}
		hl_irq_user_interrupt_handler(args->id, user_interrupt);
		break;
	case GAUDI2_IRQ_NUM_EQ_ERROR:
		hl_irq_eq_error_interrupt_thread_handler(args->id, hdev);
		break;
	default:
		dev_err(edev->dev, "unexpected interrupt id %d", args->id);
		break;
	}
out:
	mutex_unlock(&edev->irq_mutex[args->id]);

	return 0;
}

static int gaudi2_simulator_pci_access_ioctl(struct hl_simulator_device *edev, void *data)
{
	struct simulator_pci_access_args *args = data;
	void *src_addr, *dst_addr;
	u64 sram_end_address = edev->sram_user_address + SRAM_SIZE;
	u64 dram_end_address = edev->dram_user_address + edev->dram_size;
	bool is_shmem = false;
	int rc = 0;

	dev_dbg_once(edev->dev,
			"Gaudi2 simulator PCI access IOCTL details:\n");
	dev_dbg_once(edev->dev, "host == 0x%llx\n", args->host_address);
	dev_dbg_once(edev->dev, "device == 0x%llx\n", args->device_address);
	dev_dbg_once(edev->dev, "size == %u\n", args->length);
	dev_dbg_once(edev->dev, "is_write == %d\n", args->is_write);

	if (hl_mem_area_inside_range(args->device_address, args->length,
			edev->sram_user_address, sram_end_address)) {
		args->device_address -= edev->sram_user_address;
		args->device_address += (u64) (edev->sram);
	} else if (hl_mem_area_inside_range(args->device_address, args->length,
			edev->dram_user_address, dram_end_address)) {
		args->device_address -= edev->dram_user_address;
		args->device_address += (u64) (edev->dram);
	} else {
		/* assume shared memory area address */
		is_shmem = true;
	}

	if (args->is_write) {
		src_addr = (void *) (args->device_address);
		dst_addr = phys_to_virt(args->host_address);
	} else {
		src_addr = phys_to_virt(args->host_address);
		dst_addr = (void *) (args->device_address);
	}

	if (is_shmem) {
		if (args->is_write)
			rc = copy_from_user(dst_addr, (void __user *) src_addr,
						args->length);
		else
			rc = copy_to_user((void __user *) dst_addr, src_addr,
						args->length);

		if (rc) {
			dev_err(edev->dev,
				"Error in copying data %s simulator %d\n",
				(args->is_write) ? "from" : "to", rc);
			rc = -EFAULT;
		}
	} else {
		memcpy(dst_addr, src_addr, args->length);
	}

	return rc;
}

static int gaudi2_simulator_reset_device_ioctl(struct hl_simulator_device *edev, void *data)
{
	struct hl_device *hdev = edev->hdev;

	dev_warn(hdev->dev, "Gaudi2 simulator encountered a failure, going to reset device\n");

	return hl_device_reset(hdev, HL_DRV_RESET_HARD);
}

static int gaudi2_simulator_memory_ioctl(struct hl_simulator_device *edev, void *data)
{
	struct simulator_memory_args *args = data;
	int rc;

	dev_dbg(edev->dev, "receive memory ioctl. op=%d\n", args->op);

	switch (args->op) {
	case MEMORY_CREATE_SHARED_OP:
		rc = hl_sim_create_shared_block(edev, args);
		break;
	case MEMORY_RELEASE_SHARED_OP:
		rc = hl_sim_release_shared_block(edev, args);
		break;
	default:
		dev_err(edev->dev, "Gaudi2 Simulator wrong op: %d for ioctl memory\n", args->op);
		rc = -EINVAL;
	}

	return rc;
}

/*
 * Ioctl function type.
 *
 * \param edev pointer to gaudi2 simulator device.
 * \param data pointer to arg that was copied from user.
 */
typedef int gaudi2_simulator_ioctl_t(struct hl_simulator_device *edev,
					void *data);

struct gaudi2_simulator_ioctl_desc {
	unsigned int cmd;
	gaudi2_simulator_ioctl_t *func;
};

#define GAUDI2_SIMULATOR_IOCTL_DEF(ioctl, _func) \
	[_IOC_NR(ioctl)] = {.cmd = ioctl, .func = _func}

/** Ioctl table */
static const struct gaudi2_simulator_ioctl_desc gaudi2_simulator_ioctls[] = {
	GAUDI2_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_GEN_INT,
			gaudi2_simulator_gen_int_ioctl),
	GAUDI2_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_PCI_ACCESS_FROM_DEVICE,
			gaudi2_simulator_pci_access_ioctl),
	GAUDI2_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_RESET_DEVICE,
			gaudi2_simulator_reset_device_ioctl),
	GAUDI2_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_MEMORY,
			gaudi2_simulator_memory_ioctl),
};

#define GAUDI2_SIMULATOR_IOCTL_COUNT	ARRAY_SIZE(gaudi2_simulator_ioctls)

static long gaudi2_simulator_ioctl(struct file *filep, unsigned int cmd,
					unsigned long arg)
{
	struct hl_simulator_device *edev = filep->private_data;
	gaudi2_simulator_ioctl_t *func;
	const struct gaudi2_simulator_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);
	char stack_kdata[128];
	char *kdata = NULL;
	unsigned int usize, asize;
	int retcode = -EINVAL;

	if (edev->reset) {
		dev_err(edev->dev, "chip has been reset but got IOCTL\n");
		return -ENXIO;
	}

	if (nr >= GAUDI2_SIMULATOR_IOCTL_COUNT)
		goto err_i1;

	if ((nr >= SIMULATOR_COMMAND_START) &&
		(nr < SIMULATOR_COMMAND_END)) {
		u32 hl_size;

		ioctl = &gaudi2_simulator_ioctls[nr];

		hl_size = _IOC_SIZE(ioctl->cmd);
		usize = asize = _IOC_SIZE(cmd);
		if (hl_size > asize)
			asize = hl_size;

		cmd = ioctl->cmd;
	} else {
		goto err_i1;
	}

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		dev_dbg(edev->dev, "no function\n");
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

	retcode = func(edev, kdata);

	if (cmd & IOC_OUT)
		if (copy_to_user((void __user *)arg, kdata, usize))
			retcode = -EFAULT;

err_i1:
	if (!ioctl)
		dev_dbg(edev->dev,
			"invalid ioctl: pid=%d, cmd=0x%02x, nr=0x%02x\n",
			  task_pid_nr(current), cmd, nr);

	if (kdata != stack_kdata)
		kfree(kdata);

	if (retcode)
		dev_dbg(edev->dev, "ret = %d\n", retcode);

	return retcode;
}

static const struct file_operations gaudi2_simulator_ops = {
	.owner = THIS_MODULE,
	.open = gaudi2_simulator_open,
	.release = gaudi2_simulator_release,
	.read = gaudi2_simulator_read,
	.poll = gaudi2_simulator_poll,
	.write = gaudi2_simulator_write,
	.mmap = gaudi2_simulator_mmap,
	.unlocked_ioctl = gaudi2_simulator_ioctl,
	.compat_ioctl = gaudi2_simulator_ioctl,
};

static ssize_t device_name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	const char *name = "gaudi2_simulator";

	return sprintf(buf, "%s\n", name);
}

static DEVICE_ATTR_RO(device_name);

static ssize_t rw_regs_timeout_us_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct hl_simulator_device *edev = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", edev->rw_reg_timeout);
}

static ssize_t rw_regs_timeout_us_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct hl_simulator_device *edev = dev_get_drvdata(dev);
	u32 val;
	int error;

	error = kstrtou32(buf, 0, &val);
	if (error)
		return error;

	edev->rw_reg_timeout = val;

	return count;
}

static DEVICE_ATTR_RW(rw_regs_timeout_us);

static int gaudi2_sim_start_device(struct hl_simulator_device *edev)
{
	int rc;

	rc = hl_sim_create_hdev(edev);
	if (rc) {
		dev_err(edev->dev, "Failed to create real device for GAUDI2 simulator\n");
		return rc;
	}

	hl_sim_set_priv_assertions(edev, true);

	rc = hl_device_init(edev->hdev);
	if (rc) {
		dev_err(edev->dev, "fatal error during GAUDI2 simulator init\n");
		rc = -ENODEV;
		goto out_err;
	}

	return 0;

out_err:
	hl_sim_destroy_hdev(edev->hdev);
	return rc;
}

static void gaudi2_sim_stop_device(struct hl_device *hdev)
{
	hl_device_fini(hdev);
	hl_sim_destroy_hdev(hdev);
}

int gaudi2_simulator_start(struct simulator_start_args *args)
{
	struct hl_simulator_device *edev;
	bool can_put_dev = false;
	int i, rc;

	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return -ENOMEM;

	edev->irq_mutex = kcalloc(GAUDI2_MSIX_ENTRIES,
			sizeof(*edev->irq_mutex), GFP_KERNEL);
	if (!edev->irq_mutex) {
		kfree(edev);
		return -ENOMEM;
	}

	edev->hclass = args->hclass;
	edev->major = args->major;
	edev->id = args->minor + HLV_SIM_ID_OFFSET;
	edev->rw_reg_timeout = SIM_RW_REG_TIMEOUT_US;
	edev->reset = true;
	edev->single_msi_mode = args->single_msi_mode;
	edev->virt_dev_type = args->virt_dev_type;
	edev->dram_user_provided_ptr = args->dram_user_pointer;
	edev->sram_user_provided_ptr = args->sram_user_pointer;

	if (args->dram_size_in_mb > 65536) {
		pr_err("habanalabs: DRAM size must be <= 64GB");
		rc = -EINVAL;
		goto free_edev;
	}

	edev->dram_size = (u64)args->dram_size_in_mb * SZ_1M;

#if IS_ENABLED(CONFIG_DRM_ACCEL)
	sprintf(edev->name, "hlv%d", args->minor + HLV_SIM_ID_OFFSET);
#else
	sprintf(edev->name, "hlv%d", args->minor / 2 + HLV_SIM_ID_OFFSET);
#endif

	sim_devices_init(edev, edev->hclass, edev->id, &gaudi2_simulator_ops,
					edev->name);

	rc = cdev_device_add(&edev->cdev, edev->dev);
	if (rc) {
		dev_err(edev->dev, "Failed to add char device\n");
		goto free_edev;
	}

	/* Allocate shared region between KMD/User and gaudi2 simulator */
	edev->shmem = vmalloc_user(SIM_SHMEM_SIZE);
	if (!edev->shmem) {
		dev_err(edev->dev,
			"Failed to allocate simulator shared memory\n");
		rc = -ENOMEM;
		goto delete_cdev;
	}

	/* Allocate or map memory for SRAM */
	if (!edev->sram_user_provided_ptr) {
		/* Sram is allocated by kernel */
		edev->sram = vmalloc_user(SRAM_SIZE);
		if (!edev->sram) {
			dev_err(edev->dev, "Failed to allocate simulator SRAM\n");
			rc = -ENOMEM;
			goto free_shmem;
		}
	} else {
		/* Sram is allocated by user */
		rc = hl_sim_vmap_user_pages(edev->sram_user_provided_ptr,
						SRAM_SIZE,
						&edev->user_sram, false);
		if (rc) {
			dev_err(edev->dev,
				"Error during vmap user SRAM address\n");
			goto free_shmem;
		}
		edev->sram = edev->user_sram.vaddr;
		edev->sram_user_address = edev->sram_user_provided_ptr;
	}

	/* Allocate or map memory for DRAM */
	if (!edev->dram_user_provided_ptr) {
		edev->dram_vmalloc_address =
			vmalloc_user(edev->dram_size + PAGE_SIZE_2MB);
		if (!edev->dram_vmalloc_address) {
			dev_err(edev->dev,
				"Failed to allocate simulator DRAM\n");
			rc = -ENOMEM;
			goto free_sram;
		}
		edev->dram = (void *)round_up((u64)edev->dram_vmalloc_address,
					      PAGE_SIZE_2MB);
	} else {
		/* Dram is allocated by user */
		rc = hl_sim_vmap_user_pages(edev->dram_user_provided_ptr,
				GAUDI2_SIM_DRAM_DRV_END_ADDR - DRAM_PHYS_BASE,
				&edev->user_sram_dram, false);
		if (rc) {
			dev_err(edev->dev,
				"Error during vmap user DRAM address\n");
			goto free_sram;
		}
		edev->dram = edev->user_sram_dram.vaddr;
		edev->dram_user_address = edev->dram_user_provided_ptr;
	}

	edev->pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!edev->pool) {
		dev_err(edev->dev, "Failed to create shared memory pool\n");
		rc = -ENOMEM;
		goto free_dram;
	}

	rc = gen_pool_add(edev->pool, (u64) edev->shmem, SIM_SHMEM_SIZE, -1);
	if (rc) {
		dev_err(edev->dev,
			"Failed to add memory to shared memory pool\n");
		rc = -ENOMEM;
		goto free_shared_mem_pool;
	}

	spin_lock_init(&edev->h2c_lock);
	rc = kfifo_alloc(&edev->h2c_fifo, SIM_PCI_OUTSTANDING * sizeof(void *), GFP_KERNEL);
	if (rc) {
		dev_err(edev->dev, "Failed to allocate fifo for h2c\n");
		goto free_shared_mem_pool;
	}

	atomic_set(&edev->h2c_seq, 0);

	spin_lock_init(&edev->c2h_lock);
	init_waitqueue_head(&edev->pollq);
	rc = kfifo_alloc(&edev->c2h_fifo, SIM_PCI_OUTSTANDING * sizeof(void *), GFP_KERNEL);
	if (rc) {
		dev_err(edev->dev, "Failed to allocate fifo for c2h\n");
		goto free_h2c_fifo;
	}

	for (i = 0 ; i < GAUDI2_MSIX_ENTRIES ; i++)
		mutex_init(&edev->irq_mutex[i]);

	dev_info(edev->dev,
		"added %s: Gaudi2 simulator device [0000:00:%02d.0]\n",
		edev->name, args->minor);

	gaudi2_simulator_dev_table[edev->id - HLV_SIM_ID_OFFSET] = edev;

	rc = device_create_file(edev->dev, &dev_attr_device_name);
	if (rc < 0) {
		dev_err(edev->dev, "Failed to set sysfs name\n");
		goto destroy_mutex;
	}

	rc = device_create_file(edev->dev, &dev_attr_rw_regs_timeout_us);
	if (rc < 0) {
		dev_err(edev->dev, "Failed to create timeout file\n");
		goto remove_name;
	}

	mutex_init(&edev->shared_block_idr_mutex);
	idr_init(&edev->shared_block_idr);
	dev_set_drvdata(edev->dev, edev);

	return 0;

remove_name:
	device_remove_file(edev->dev, &dev_attr_device_name);
destroy_mutex:
	for (i = 0 ; i < GAUDI2_MSIX_ENTRIES ; i++)
		mutex_destroy(&edev->irq_mutex[i]);
	kfifo_free(&edev->c2h_fifo);
free_h2c_fifo:
	kfifo_free(&edev->h2c_fifo);
free_shared_mem_pool:
	gaudi2_simulator_dev_table[edev->id - HLV_SIM_ID_OFFSET] = NULL;
	gen_pool_destroy(edev->pool);
free_dram:
	if (!edev->dram_user_provided_ptr)
		vfree(edev->dram_vmalloc_address);
	else
		hl_sim_vunmap_user_pages(&edev->user_sram_dram);
free_sram:
	if (!edev->sram_user_provided_ptr)
		vfree(edev->sram);
	else
		hl_sim_vunmap_user_pages(&edev->user_sram);
free_shmem:
	vfree(edev->shmem);
delete_cdev:
	cdev_device_del(&edev->cdev, edev->dev);
	can_put_dev = true;
free_edev:

	if (can_put_dev) {
		put_device(edev->dev);
	} else {
		kfree(edev->irq_mutex);
		kfree(edev);
	}

	return rc;
}

void gaudi2_simulator_stop(u32 minor)
{
	struct simulator_shared_mem_block *shared_block;
	struct hl_simulator_device *edev;
	struct simulator_msg *msg;
	int count, i, handle;

	if (minor >= HL_MAX_MINORS) {
		pr_crit("habanalabs: minor is out of bounds %u, can't stop sim\n", minor);
		return;
	}

	edev = gaudi2_simulator_dev_table[minor];

	dev_dbg(edev->dev, "Removing Gaudi2 simulator device\n");

	hl_sim_set_priv_assertions(edev, false);

	/* Make sure work to create simulator has finished */
	cancel_delayed_work_sync(&edev->work_create);

	if (edev->hdev)
		gaudi2_sim_stop_device(edev->hdev);

	/* Disable open on device */
	gaudi2_simulator_dev_table[minor] = NULL;

	device_remove_file(edev->dev, &dev_attr_device_name);
	device_remove_file(edev->dev, &dev_attr_rw_regs_timeout_us);

	/* Hide device from user */
	cdev_device_del(&edev->cdev, edev->dev);

	if (!idr_is_empty(&edev->shared_block_idr)) {
		idr_for_each_entry(&edev->shared_block_idr, shared_block, handle) {
			hl_sim_free_shared_block(shared_block, true);
			idr_remove(&edev->shared_block_idr, handle);
		}
	}

	idr_destroy(&edev->shared_block_idr);
	mutex_destroy(&edev->shared_block_idr_mutex);

	gen_pool_destroy(edev->pool);

	while (!kfifo_is_empty(&edev->h2c_fifo)) {
		count = kfifo_out(&edev->h2c_fifo, &msg, sizeof(msg));
		if (count) {
			kfree(msg);
			msg = NULL;
		}
	}

	while (!kfifo_is_empty(&edev->c2h_fifo)) {
		count = kfifo_out(&edev->c2h_fifo, &msg, sizeof(msg));
		if (count)
			kfree(msg);
	}

	kfifo_free(&edev->h2c_fifo);
	kfifo_free(&edev->c2h_fifo);

	for (i = 0 ; i < GAUDI2_MSIX_ENTRIES ; i++)
		mutex_destroy(&edev->irq_mutex[i]);

	if (!edev->dram_user_provided_ptr)
		vfree(edev->dram_vmalloc_address);
	else
		hl_sim_vunmap_user_pages(&edev->user_sram_dram);
	if (!edev->sram_user_provided_ptr)
		vfree(edev->sram);
	else
		hl_sim_vunmap_user_pages(&edev->user_sram);
	vfree(edev->shmem);

	put_device(edev->dev);
}

/**
 * gaudi2_sim_rreg - Read an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 *
 * Returns the value of the MMIO register we are asked to read
 *
 */
static u32 gaudi2_sim_rreg(struct hl_device *hdev, u32 reg)
{
	struct hl_simulator_device *edev =
			gaudi2_simulator_dev_table[hdev->id];
	u64 reg_addr = CFG_BASE + reg;

	return hl_sim_rreg(hdev, reg_addr, edev);
}

/**
 * gaudi2_sim_wreg - Write to an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 * @val: 32-bit value
 *
 * Writes the 32-bit value into the MMIO register
 *
 */
static void gaudi2_sim_wreg(struct hl_device *hdev, u32 reg, u32 val)
{
	struct hl_simulator_device *edev =
			gaudi2_simulator_dev_table[hdev->id];
	u64 reg_addr = CFG_BASE + reg;
	hl_sim_wreg(hdev, reg_addr, edev, val);
}

static void gaudi2_sim_notify_reset(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = gaudi2_simulator_dev_table[hdev->id];

	hl_sim_notify_reset(hdev, edev);
}

/* All the code below this point is the gaudi2 sim device implementation */

static int gaudi2_sim_set_fixed_properties(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = gaudi2_simulator_dev_table[hdev->id];
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 hbm_user_base_offset, nic_drv_size;
	int rc;

	nic_drv_size = GAUDI2_SIM_NIC_DRV_SIZE;

	rc = gaudi2_set_fixed_properties(hdev);
	if (rc)
		return rc;

	prop->dram_size =
		DIV_ROUND_DOWN_ULL(edev->dram_size, prop->dram_page_size) *
							prop->dram_page_size;

	prop->dram_end_address = prop->dram_base_address + prop->dram_size;

	hbm_user_base_offset = nic_drv_size;
	prop->dram_user_base_address = DRAM_PHYS_BASE +
		roundup(hbm_user_base_offset, prop->dram_page_size);

	prop->dmmu.start_addr = prop->dram_base_address +
			roundup(prop->dram_size, prop->dram_page_size);
	prop->dmmu.end_addr = prop->dmmu.start_addr +
			((VA_HBM_SPACE_END - prop->dmmu.start_addr) /
			prop->dmmu.page_size) * prop->dram_page_size;

	prop->cb_pool_cb_cnt = SIM_CB_POOL_CB_CNT;
	prop->cb_pool_cb_size = SIM_CB_POOL_CB_SIZE;
	prop->nic_drv_size = nic_drv_size;
	prop->nic_drv_addr = GAUDI2_SIM_NIC_DRV_ADDR;

	return 0;
}

static int gaudi2_sim_pci_bars_map(struct hl_device *hdev)
{
	struct hl_simulator_device *edev =
			gaudi2_simulator_dev_table[hdev->id];

	/* Simulate SRAM BAR */
	hdev->pcie_bar[SRAM_CFG_BAR_ID] = (void __iomem *) edev->sram;
	dev_dbg(hdev->dev, "SRAM at 0x%px\n",
			hdev->pcie_bar[SRAM_CFG_BAR_ID]);

	/* Simulate DRAM BAR */
	hdev->pcie_bar[DRAM_BAR_ID] = (void __iomem *) edev->dram;
	dev_dbg(hdev->dev, "DRAM at 0x%px\n", hdev->pcie_bar[DRAM_BAR_ID]);

	/* CFG is not simulated as BAR */
	hdev->rmmio = NULL;

	return 0;
}

static void gaudi2_sim_pci_bars_unmap(struct hl_device *hdev)
{
	hdev->pcie_bar[DRAM_BAR_ID] = NULL;
	hdev->pcie_bar[SRAM_CFG_BAR_ID] = NULL;
	hdev->rmmio = NULL;
}

static int gaudi2_sim_early_init(struct hl_device *hdev)
{
	int rc;

	rc = hl_cn_check_ib_driver(hdev);
	if (rc)
		return rc;

	rc = gaudi2_sim_set_fixed_properties(hdev);
	if (rc)
		return rc;

	rc = gaudi2_sim_pci_bars_map(hdev);

	return rc;
}

static int gaudi2_sim_early_fini(struct hl_device *hdev)
{
	kfree(hdev->asic_prop.hw_queues_props);
	gaudi2_sim_pci_bars_unmap(hdev);

	return 0;
}

static void gaudi2_sim_get_nic_info(struct hl_device *hdev)
{
	struct hl_cn_cpucp_info *cn_cpucp_info = &hdev->asic_prop.cn_props.cpucp_info;

	/* Assume HLS2 connections */
	if ((hdev->asic_type == ASIC_GAUDI2B_SIM) || (hdev->asic_type == ASIC_GAUDI2B_SIM_ARC)) {
		cn_cpucp_info->link_mask[0] = 0xFFFFFF & ~GAUDI2_HLS2_EXTERN_PORTS_MASK;
		cn_cpucp_info->link_ext_mask[0] = 0;
	} else {
		cn_cpucp_info->link_mask[0] = 0xFFFFFF;
		cn_cpucp_info->link_ext_mask[0] = GAUDI2_HLS2_EXTERN_PORTS_MASK;
	}
}

static int gaudi2_sim_cpucp_info_get(struct hl_device *hdev)
{
	struct hl_simulator_device *edev =
			gaudi2_simulator_dev_table[hdev->id];
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_cn_cpucp_info *cn_cpucp_info = &prop->cn_props.cpucp_info;
	u64 dram_size;
	int rc;

	if (!(gaudi2->hw_cap_initialized & HW_CAP_CPU_Q)) {
		/* Skip for hard or device release reset flow. No need to repopulate. */
		if (!hdev->reset_info.in_reset) {
			/* Set nic_info by the driver because F/W is disabled on simulator.
			 * Override info from module param.
			 */
			if (!hdev->ignore_fw_nic_info) {
				gaudi2_sim_get_nic_info(hdev);
				hdev->cn.ports_ext_mask &= cn_cpucp_info->link_ext_mask[0];
				hdev->cn.ports_mask &= cn_cpucp_info->link_mask[0];
				hdev->cn.auto_neg_mask &= cn_cpucp_info->auto_neg_mask[0];
			}

			rc = gaudi2_cn_set_info(hdev, false);
			if (rc)
				return rc;
		}

		return 0;
	}

	/* No point of asking this information again when not doing hard reset, as the device
	 * CPU hasn't been reset
	 */
	if (hdev->reset_info.in_compute_reset)
		return 0;

	rc = hl_fw_cpucp_handshake(hdev, mmCPU_BOOT_DEV_STS0,
					mmCPU_BOOT_DEV_STS1, mmCPU_BOOT_ERR0,
					mmCPU_BOOT_ERR1);
	if (rc)
		return rc;

	/* Make sure we don't expose HWMON for simulator */
	if (hdev->hl_chip_info->info) {
		const struct hwmon_channel_info * const *channel_info_arr;
		int i = 0;

		channel_info_arr = hdev->hl_chip_info->info;

		while (channel_info_arr[i]) {
			kfree(channel_info_arr[i]->config);
			kfree(channel_info_arr[i]);
			i++;
		}

		kfree(channel_info_arr);

		hdev->hl_chip_info->info = NULL;
	}

	dram_size = le64_to_cpu(prop->cpucp_info.dram_size);
	if (dram_size) {
		if (dram_size != edev->dram_size) {
			dev_err(hdev->dev,
				"F/W reported invalid HBM size %llu != %llu\n",
				dram_size, edev->dram_size);
			dram_size = edev->dram_size;
		}

		prop->dram_size = dram_size;
		prop->dram_end_address = prop->dram_base_address + dram_size;
	}

	if (!hdev->reset_info.in_reset) {
		/* Override info from module param. */
		if (!hdev->ignore_fw_nic_info)
			hdev->card_type = le32_to_cpu(hdev->asic_prop.cpucp_info.card_type);

		/* For NIC do not populate from FW. */
		rc = gaudi2_cn_set_info(hdev, false);
	}

	return rc;
}

static int gaudi2_sim_sw_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi2_device *gaudi2;
	int rc;

	/* Allocate device structure */
	gaudi2 = kzalloc(sizeof(*gaudi2), GFP_KERNEL);
	if (!gaudi2)
		return -ENOMEM;

	gaudi2->cpucp_info_get = gaudi2_sim_cpucp_info_get;

	hdev->asic_specific = gaudi2;

	mutex_init(&gaudi2->hw_queues_lock_mutex);

	gaudi2->virt_msix_db_cpu_addr = hl_asic_dma_alloc_coherent(hdev, prop->pmmu.page_size,
								&gaudi2->virt_msix_db_dma_addr,
								GFP_KERNEL | __GFP_ZERO);
	if (!gaudi2->virt_msix_db_cpu_addr) {
		dev_err(hdev->dev, "Failed to allocate DMA memory for virtual MSI-X doorbell\n");
		rc = -ENOMEM;
		goto free_gaudi2_device;
	}

	gaudi2->scratchpad_kernel_address = hl_asic_dma_alloc_coherent(hdev, PAGE_SIZE,
								&gaudi2->scratchpad_bus_address,
								GFP_KERNEL | __GFP_ZERO);
	if (!gaudi2->scratchpad_kernel_address) {
		rc = -ENOMEM;
		goto free_virt_msix_db_mem;
	}

	gaudi2_user_mapped_blocks_init(hdev);

	/* Initialize user interrupts */
	gaudi2_user_interrupt_setup(hdev);

	hdev->supports_coresight = false;
	hdev->asic_prop.supports_compute_reset = true;
	hdev->supports_sync_stream = true;
	hdev->supports_cb_mapping = true;
	hdev->supports_wait_for_multi_cs = false;

	hdev->asic_funcs->set_pci_memory_regions(hdev);

	rc = gaudi2_special_blocks_iterator_config(hdev);
	if (rc)
		goto free_scratchpad_mem;

	rc = gaudi2_test_queues_msgs_alloc(hdev);
	if (rc)
		goto special_blocks_free;

	/*
	 * Init the engine core interrupt register, because it won't be set in case of simulator w/o
	 * F/W, while gaudi2_init_protection_bits() expects that it would be a scratchpad register.
	 */
	hdev->fw_loader.dynamic_loader.comm_desc.cpu_dyn_regs.eng_arc_irq_ctrl =
						cpu_to_le32(mmPSOC_GLOBAL_CONF_SCRATCHPAD_15);

	return 0;

special_blocks_free:
	gaudi2_special_blocks_iterator_free(hdev);

free_scratchpad_mem:
	hl_asic_dma_free_coherent(hdev, PAGE_SIZE, gaudi2->scratchpad_kernel_address,
					gaudi2->scratchpad_bus_address);
free_virt_msix_db_mem:
	hl_asic_dma_free_coherent(hdev, prop->pmmu.page_size, gaudi2->virt_msix_db_cpu_addr,
					gaudi2->virt_msix_db_dma_addr);
free_gaudi2_device:
	mutex_destroy(&gaudi2->hw_queues_lock_mutex);
	kfree(gaudi2);
	return rc;
}

static int gaudi2_sim_sw_fini(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi2_device *gaudi2 = hdev->asic_specific;

	gaudi2_test_queues_msgs_free(hdev);

	gaudi2_special_blocks_iterator_free(hdev);

	hl_asic_dma_free_coherent(hdev, PAGE_SIZE, gaudi2->scratchpad_kernel_address,
					gaudi2->scratchpad_bus_address);

	hl_asic_dma_free_coherent(hdev, prop->pmmu.page_size, gaudi2->virt_msix_db_cpu_addr,
					gaudi2->virt_msix_db_dma_addr);

	mutex_destroy(&gaudi2->hw_queues_lock_mutex);

	kfree(gaudi2);

	return 0;
}

static void gaudi2_sim_halt_engines(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	struct hl_simulator_device *edev =
			gaudi2_simulator_dev_table[hdev->id];
	int i;

	/*
	 * Mark the NIC as in reset to avoid any new NIC accesses to the
	 * H/W. This must be done before we stop the CPU as the NIC
	 * might use it e.g. get/set EEPROM data.
	 */
	if (hard_reset)
		hl_cn_hard_reset_prepare(hdev);

	gaudi2_stop_dma_qmans(hdev);
	gaudi2_stop_mme_qmans(hdev);
	gaudi2_stop_tpc_qmans(hdev);
	gaudi2_stop_rot_qmans(hdev);
	gaudi2_stop_nic_qmans(hdev);

	gaudi2_halt_arcs(hdev);
	gaudi2_dma_stall(hdev);
	gaudi2_mme_stall(hdev);
	gaudi2_tpc_stall(hdev);
	gaudi2_rotator_stall(hdev);

	gaudi2_stop_dec(hdev);

	/*
	 * in case of soft reset do a manual flush for QMANs (currently called
	 * only for NIC QMANs
	 */
	if (!hard_reset)
		gaudi2_nic_qmans_manual_flush(hdev);

	gaudi2_disable_dma_qmans(hdev);
	gaudi2_disable_mme_qmans(hdev);
	gaudi2_disable_tpc_qmans(hdev);
	gaudi2_disable_rot_qmans(hdev);
	gaudi2_disable_nic_qmans(hdev);

	if (hard_reset)
		hl_cn_stop(hdev);
	else
		gaudi2_cn_compute_reset_prepare(hdev);

	if (hard_reset)
		gaudi2_sim_notify_reset(hdev);

	/* Give simulator some time to prepare for reset */
	msleep(SIM_HALT_WAIT_MSEC);

	if (hard_reset)
		edev->reset = true;

	/* Flush any in progress handling of the gen_int ioctl */
	for (i = 0 ; i < GAUDI2_MSIX_ENTRIES ; i++) {
		mutex_lock(&edev->irq_mutex[i]);
		mutex_unlock(&edev->irq_mutex[i]);
	}
}

static int gaudi2_sim_fw_config(struct hl_device *hdev)
{
	struct hl_simulator_device *edev =
			gaudi2_simulator_dev_table[hdev->id];
	int rc;

	hl_sim_set_priv_assertions(edev, false);

	/* Do not reset following blocks during soft reset */
	WREG32(mmPSOC_RESET_CONF_NIC_PRT_SOFT_RST_CFG, 0x0);
	WREG32(mmPSOC_RESET_CONF_KDMA_SOFT_RST_CFG, 0x0);

	rc = gaudi2_init_golden_registers(hdev);
	if (rc)
		return rc;

	hl_sim_set_priv_assertions(edev, true);

	return 0;
}

static int gaudi2_sim_hw_init(struct hl_device *hdev)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	struct hl_simulator_device *edev =
			gaudi2_simulator_dev_table[hdev->id];
	int rc;

	rc = hl_init_pb_security(hdev, true);
	if (rc) {
		dev_err(hdev->dev, "Configuring privileged PBs failed!");
		return rc;
	}

	gaudi2_cn_quiescence(hdev);

	/* Set all privileged registers instead of FW */
	rc = gaudi2_sim_fw_config(hdev);
	if (rc)
		return rc;

	rc = gaudi2->cpucp_info_get(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to get cpucp info\n");
		return rc;
	}

	rc = gaudi2_mmu_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize MMU\n");
		return rc;
	}

	gaudi2_init_kdma(hdev);
	gaudi2_init_pdma(hdev);
	gaudi2_init_edma(hdev);
	gaudi2_init_sm(hdev);
	gaudi2_init_mme(hdev);
	gaudi2_init_tpc(hdev);
	gaudi2_init_rotator(hdev);
	gaudi2_init_dec(hdev);
	gaudi2_init_cn(hdev);

	gaudi2->dram_bar_cur_addr = DRAM_PHYS_BASE;

	edev->reset = false;

	return 0;
}

static int gaudi2_sim_hw_fini(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	u32 status, reset_timeout_ms, reset_timeout_us, reg_val;
	int rc;

	reset_timeout_ms = SIM_RESET_WAIT_MSEC;
	reset_timeout_us = reset_timeout_ms * 1000;

	gaudi2_reset_arcs(hdev);

	if (hard_reset) {
		reg_val = FIELD_PREP(PCIE_WRAP_PSOC_RST_CTRL_HARD_RST_MASK, 0x1);
		WREG32(mmPCIE_WRAP_PSOC_RST_CTRL, reg_val);

		dev_dbg(hdev->dev,
			"Issued HARD reset command, waiting up to %dms\n",
			reset_timeout_ms);
	} else {
		if (hdev->security_enable) {
			/* Block access to engines, QMANs and SM during reset, these
			 * RRs will be reconfigured after soft reset.
			 * PCIE_MSIX is left unsecured to allow NIC packets processing during the
			 * reset.
			 */
			gaudi2_write_rr_to_all_lbw_rtrs(hdev, RR_TYPE_LONG, NUM_LONG_LBW_RR - 1,
						mmDCORE0_TPC0_QM_DCCM_BASE, mmPCIE_MSIX_BASE);

			gaudi2_write_rr_to_all_lbw_rtrs(hdev, RR_TYPE_LONG, NUM_LONG_LBW_RR - 2,
					mmPCIE_MSIX_BASE + HL_BLOCK_SIZE,
					mmPCIE_VDEC1_MSTR_IF_RR_SHRD_HBW_BASE + HL_BLOCK_SIZE);
		}

		reg_val =
			FIELD_PREP(PCIE_WRAP_PSOC_RST_CTRL_SOFT_RST_MASK, 0x1);
		WREG32(mmPCIE_WRAP_PSOC_RST_CTRL, reg_val);

		dev_dbg(hdev->dev,
			"Issued SOFT reset command, waiting up to %dms\n",
			reset_timeout_ms);
	}

	/* Wait a certain amount of time before checking if the reset has
	 * finished
	 */
	rc = hl_poll_timeout(hdev,
		mmPCIE_WRAP_PSOC_BOOT_MNG_DONE, status, (status &
		PCIE_WRAP_PSOC_BOOT_MNG_DONE_PSOC_BOOT_MNG_DONE_MASK),
		10000, reset_timeout_us);

	if (rc == -ETIMEDOUT)
		dev_err(hdev->dev,
			"Timeout while waiting for device to reset 0x%x\n",
			status);

	/* Reset bit is not self-clearing, need to manually clear it */
	WREG32(mmPCIE_WRAP_PSOC_RST_CTRL, 0);

	if (!hard_reset && gaudi2) {
		gaudi2->hw_cap_initialized &=
			~(HW_CAP_HBM_SCRAMBLER_SW_RESET | HW_CAP_PDMA_MASK |
			HW_CAP_EDMA_MASK | HW_CAP_MME_MASK | HW_CAP_ROT_MASK);

		gaudi2->dec_hw_cap_initialized &= ~(HW_CAP_DEC_MASK);
		gaudi2->tpc_hw_cap_initialized &= ~(HW_CAP_TPC_MASK);

		/*
		 * Clear NIC capability mask in order for driver to re-configure
		 * NIC QMANs. NIC ports will not be re-configured during soft
		 * reset as we call gaudi2_cn_init only during hard reset
		 */
		gaudi2->nic_hw_cap_initialized &= ~(HW_CAP_NIC_MASK);

		return 0;
	}

	if (gaudi2) {
		gaudi2->hw_cap_initialized &=
			~(HW_CAP_DRAM | HW_CAP_PMMU | HW_CAP_CPU |
			HW_CAP_CPU_Q | HW_CAP_SRAM_SCRAMBLER |
			HW_CAP_DMMU_MASK | HW_CAP_PDMA_MASK | HW_CAP_NIC_DRV |
			HW_CAP_EDMA_MASK | HW_CAP_KDMA | HW_CAP_MME_MASK |
			HW_CAP_ROT_MASK);

		gaudi2->dec_hw_cap_initialized &= ~(HW_CAP_DEC_MASK);
		gaudi2->tpc_hw_cap_initialized &= ~(HW_CAP_TPC_MASK);
		gaudi2->nic_hw_cap_initialized &= ~(HW_CAP_NIC_MASK);

		memset(gaudi2->events_stat, 0, sizeof(gaudi2->events_stat));
	}
	return 0;
}

static int gaudi2_sim_suspend(struct hl_device *hdev)
{
	return 0;
}

static int gaudi2_sim_resume(struct hl_device *hdev)
{
	return 0;
}

static int gaudi2_sim_mmap(struct hl_device *hdev,
				struct vm_area_struct *vma, void *cpu_addr,
				dma_addr_t dma_addr, size_t size)
{
	int rc;

	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY | VM_NORESERVE);

	rc = remap_vmalloc_range(vma, cpu_addr, 0);
	if (rc)
		dev_err(hdev->dev, "remap vmalloc error %d", rc);

	return rc;
}

static void *gaudi2_sim_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	struct hl_simulator_device *edev =
			gaudi2_simulator_dev_table[hdev->id];

	void *address = (void *) gen_pool_alloc(edev->pool, size);

	if (address) {
		if (flags & __GFP_ZERO)
			memset(address, 0, size);

		*dma_handle = virt_to_phys(address);
		if (!gaudi2_host_phys_addr_valid(*dma_handle))
			dev_crit(hdev->dev, "invalid host phys addr 0x%llx\n",
					*dma_handle);
	}

	return address;
}

static void gaudi2_sim_dma_free_coherent(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle)
{
	struct hl_simulator_device *edev =
			gaudi2_simulator_dev_table[hdev->id];

	gen_pool_free(edev->pool, (u64) cpu_addr, size);
}

static void *gaudi2_sim_dma_pool_zalloc(struct hl_device *hdev, size_t size,
					gfp_t mem_flags, dma_addr_t *dma_handle)
{
	if (size > SIM_DMA_POOL_BLK_SIZE)
		return NULL;

	return gaudi2_sim_dma_alloc_coherent(hdev, PAGE_SIZE, dma_handle,
					mem_flags | __GFP_ZERO);
}

static void gaudi2_sim_dma_pool_free(struct hl_device *hdev, void *vaddr,
				dma_addr_t dma_addr)
{
	gaudi2_sim_dma_free_coherent(hdev, PAGE_SIZE, vaddr, dma_addr);
}

static u64 gaudi2_sim_read_pte(struct hl_device *hdev, u64 addr)
{
	u64 val;

	if (hdev->reset_info.hard_reset_pending)
		return U64_MAX;

	hl_sim_read_dram(gaudi2_simulator_dev_table[hdev->id],
			 &val, (addr - DRAM_PHYS_BASE), sizeof(val));

	return val;
}

static void gaudi2_sim_write_pte(struct hl_device *hdev, u64 addr, u64 val)
{
	if (hdev->reset_info.hard_reset_pending)
		return;

	hl_sim_write_dram(gaudi2_simulator_dev_table[hdev->id],
			  (addr - DRAM_PHYS_BASE), &val, sizeof(val));
}

static void gaudi2_sim_hw_queues_lock(struct hl_device *hdev)
	__acquires(&gaudi2->hw_queues_lock_mutex)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;

	mutex_lock(&gaudi2->hw_queues_lock_mutex);
}

static void gaudi2_sim_hw_queues_unlock(struct hl_device *hdev)
	__releases(&gaudi2->hw_queues_lock_mutex)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;

	mutex_unlock(&gaudi2->hw_queues_lock_mutex);
}

static u32 gaudi2_sim_get_pci_id(struct hl_device *hdev)
{
	switch (hdev->asic_type) {
	case ASIC_GAUDI2_SIM:
		return PCI_IDS_GAUDI2_SIMULATOR;
	case ASIC_GAUDI2B_SIM:
		return PCI_IDS_GAUDI2B_SIMULATOR;
	case ASIC_GAUDI2_SIM_ARC:
		return PCI_IDS_GAUDI2_ARC_SIMULATOR;
	case ASIC_GAUDI2B_SIM_ARC:
		return PCI_IDS_GAUDI2B_ARC_SIMULATOR;
	default:
		return PCI_IDS_GAUDI2_SIMULATOR;
	}
}

static int gaudi2_sim_get_eeprom_data(struct hl_device *hdev, void *data,
		size_t max_size)
{
	const char *str = "no EEPROM data on simulator";

	memcpy(data, str, strlen(str) + 1);

	return 0;
}

static void gaudi2_sim_ack_protection_bits_errors(struct hl_device *hdev)
{

}

static int gaudi2_sim_block_mmap(struct hl_device *hdev,
			struct vm_area_struct *vma, u32 block_id,
			u32 block_size)
{
	return -EPERM;
}

static void gaudi2_sim_add_device_attr(struct hl_device *hdev,
					struct attribute_group *dev_clk_attr_grp,
					struct attribute_group *dev_vrm_attr_grp)
{
	dev_clk_attr_grp->attrs = gaudi2_sim_dev_attrs;
	dev_vrm_attr_grp->attrs = gaudi2_sim_dev_attrs;
}

static void gaudi2_sim_enable_events_from_fw(struct hl_device *hdev)
{
	/* Not to be implemented */
}

static int gaudi2_sim_get_hw_block_id(struct hl_device *hdev, u64 block_addr,
					u32 *block_size, u32 *block_id)
{
	struct gaudi2_device *gaudi2 = hdev->asic_specific;
	int i;

	for (i = 0 ; i < NUM_USER_MAPPED_BLOCKS ; i++) {
		if ((block_addr >= CFG_BASE + gaudi2->mapped_blocks[i].address)
				&& (block_addr < (CFG_BASE +
				gaudi2->mapped_blocks[i].address +
				gaudi2->mapped_blocks[i].size))) {
			*block_id = i;
			if (block_size)
				*block_size = gaudi2->mapped_blocks[i].size;
			return 0;
		}
	}

	dev_err(hdev->dev, "Invalid block address %#llx", block_addr);

	return -EINVAL;
}

static int gaudi2_sim_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard, u32 flags)
{
	if (hdev->simulator_crashed)
		return 0;

	return gaudi2_mmu_invalidate_cache(hdev, is_hard, flags);
}

static int gaudi2_sim_mmu_invalidate_cache_range(struct hl_device *hdev,
				bool is_hard, u32 flags,
				u32 asid, u64 va, u64 size)
{
	if (hdev->simulator_crashed)
		return 0;

	/* Not supported by simulator yet, treat as invalidate all */
	return gaudi2_mmu_invalidate_cache(hdev, is_hard, flags);
}

static void *gaudi2_sim_cpu_accessible_dma_pool_alloc(
						struct hl_device *hdev,
						size_t size,
						dma_addr_t *dma_handle)
{
	return kmalloc(size, GFP_KERNEL);
}

static void gaudi2_sim_cpu_accessible_dma_pool_free(
						struct hl_device *hdev,
						size_t size, void *vaddr)
{
	kfree(vaddr);
}

static int gaudi2_sim_pll_info_get(struct hl_device *hdev, u32 pll_index,
		u16 *pll_freq_arr)
{
	/*
	 * in simulation PLLs are not supported.
	 */
	memset(pll_freq_arr, 0x0, sizeof(u16) * HL_PLL_NUM_OUTPUTS);

	return 0;
}

static void gaudi2_sim_set_priv_assertions(struct hl_device *hdev, bool enable)
{
	struct hl_simulator_device *edev = gaudi2_simulator_dev_table[hdev->id];

	hl_sim_set_priv_assertions(edev, enable);
}

static bool gaudi2_sim_is_device_idle(struct hl_device *hdev, u64 *mask_arr, u8 mask_len,
					struct engines_data *e)
{
	if (hdev->simulator_crashed)
		return true;

	return gaudi2_is_device_idle(hdev, mask_arr, mask_len, e);
}

static int gaudi2_sim_memset_device_memory(struct hl_device *hdev, u64 addr, u64 size, u64 val)
{
	return 0;
}

static const struct hl_asic_funcs gaudi2_sim_funcs = {
	.early_init = gaudi2_sim_early_init,
	.early_fini = gaudi2_sim_early_fini,
	.late_init = gaudi2_late_init,
	.late_fini = gaudi2_late_fini,
	.sw_init = gaudi2_sim_sw_init,
	.sw_fini = gaudi2_sim_sw_fini,
	.hw_init = gaudi2_sim_hw_init,
	.hw_fini = gaudi2_sim_hw_fini,
	.halt_engines = gaudi2_sim_halt_engines,
	.suspend = gaudi2_sim_suspend,
	.resume = gaudi2_sim_resume,
	.mmap = gaudi2_sim_mmap,
	.ring_doorbell = gaudi2_ring_doorbell,
	.pqe_write = gaudi2_pqe_write,
	.asic_dma_alloc_coherent = gaudi2_sim_dma_alloc_coherent,
	.asic_dma_free_coherent = gaudi2_sim_dma_free_coherent,
	.scrub_device_mem = gaudi2_scrub_device_mem,
	.scrub_device_dram = gaudi2_scrub_device_dram,
	.get_int_queue_base = NULL,
	.test_queues = gaudi2_test_queues,
	.asic_dma_pool_zalloc = gaudi2_sim_dma_pool_zalloc,
	.asic_dma_pool_free = gaudi2_sim_dma_pool_free,
	.cpu_accessible_dma_pool_alloc =
				gaudi2_sim_cpu_accessible_dma_pool_alloc,
	.cpu_accessible_dma_pool_free = gaudi2_sim_cpu_accessible_dma_pool_free,
	.dma_map_sgtable = hl_sim_dma_map_sgtable,
	.dma_unmap_sgtable = hl_sim_dma_unmap_sgtable,
	.cs_parser = gaudi2_cs_parser,
	.add_end_of_cb_packets = NULL,
	.update_eq_ci = gaudi2_update_eq_ci,
	.context_switch = gaudi2_context_switch,
	.restore_phase_topology = gaudi2_restore_phase_topology,
	.debugfs_read_dma = gaudi2_debugfs_read_dma,
	.add_device_attr = gaudi2_sim_add_device_attr,
	.handle_eqe = NULL,
	.get_events_stat = gaudi2_get_events_stat,
	.read_pte = gaudi2_sim_read_pte,
	.write_pte = gaudi2_sim_write_pte,
	.mmu_invalidate_cache = gaudi2_sim_mmu_invalidate_cache,
	.mmu_invalidate_cache_range = gaudi2_sim_mmu_invalidate_cache_range,
	.mmu_prefetch_cache_range = NULL,
	.send_heartbeat = gaudi2_send_heartbeat,
	.debug_coresight = gaudi2_debug_coresight,
	.is_device_idle = gaudi2_sim_is_device_idle,
	.compute_reset_late_init = gaudi2_compute_reset_late_init,
	.hw_queues_lock = gaudi2_sim_hw_queues_lock,
	.hw_queues_unlock = gaudi2_sim_hw_queues_unlock,
	.get_pci_id = gaudi2_sim_get_pci_id,
	.get_eeprom_data = gaudi2_sim_get_eeprom_data,
	.get_monitor_dump = gaudi2_get_monitor_dump,
	.send_cpu_message = gaudi2_send_cpu_message,
	.cn_init = hl_cn_init,
	.cn_fini = hl_cn_fini,
	.cn_control = hl_cn_control,
	.pci_bars_map = NULL,
	.init_iatu = NULL,
	.rreg = gaudi2_sim_rreg,
	.wreg = gaudi2_sim_wreg,
	.halt_coresight = gaudi2_halt_coresight,
	.ctx_init = gaudi2_ctx_init,
	.ctx_fini = gaudi2_ctx_fini,
	.pre_schedule_cs = gaudi2_pre_schedule_cs,
	.get_queue_id_for_cq = gaudi2_get_queue_id_for_cq,
	.load_firmware_to_device = NULL,
	.load_boot_fit_to_device = NULL,
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
	.scramble_addr = gaudi2_mmu_scramble_addr,
	.descramble_addr = gaudi2_mmu_descramble_addr,
	.ack_protection_bits_errors = gaudi2_sim_ack_protection_bits_errors,
	.get_hw_block_id = gaudi2_sim_get_hw_block_id,
	.hw_block_mmap = gaudi2_sim_block_mmap,
	.enable_events_from_fw = gaudi2_sim_enable_events_from_fw,
	.ack_mmu_errors = gaudi2_ack_mmu_page_fault_or_access_error,
	.map_pll_idx_to_fw_idx = gaudi2_map_pll_idx_to_fw_idx,
	.init_cpu_scrambler_dram = NULL,
	.state_dump_init = gaudi2_state_dump_init,
	.get_sob_addr = &gaudi2_get_sob_addr,
	.set_pci_memory_regions = gaudi2_set_pci_memory_regions,
	.get_stream_master_qid_arr = gaudi2_get_stream_master_qid_arr,
	.scheduler_submit_buf = gaudi2_scheduler_submit_buf,
	.mmu_get_real_page_size = gaudi2_mmu_get_real_page_size,
	.cn_funcs = &gaudi2_cn_funcs,
	.access_dev_mem = gaudi2_sim_access_dev_mem,
	.set_dram_bar_base = NULL,
	.init_firmware_preload_params = NULL,
	.set_engine_cores = gaudi2_set_engine_cores,
	.set_engines = gaudi2_set_engines,
	.send_device_activity = gaudi2_send_device_activity,
	.read_fetch_memory_block = NULL,
	.fw_security_emulation_init = gaudi2_fw_security_emulation_init,
	.fw_security_emulation_fini = gaudi2_fw_security_emulation_fini,
	.pll_info_get = gaudi2_sim_pll_info_get,
	.set_dram_properties = gaudi2_set_dram_properties,
	.set_priv_assertions = gaudi2_sim_set_priv_assertions,
	.set_binning_masks = gaudi2_set_binning_masks,
	.memset_device_memory = gaudi2_sim_memset_device_memory,
};

/**
 * gaudi2_sim_set_asic_funcs() - Set GAUDI2 Simulator function pointers.
 * @hdev: Pointer to hl_device structure.
 */
void gaudi2_sim_set_asic_funcs(struct hl_device *hdev)
{
	hdev->asic_funcs = &gaudi2_sim_funcs;
}
