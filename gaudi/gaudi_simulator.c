// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2024 HabanaLabs, Ltd.
 * Copyright (C) 2024-2025, Intel Corporation.
 * All Rights Reserved.
 */

#define pr_fmt(fmt)			"habanalabs: " fmt

#include "gaudiP.h"
#include "gaudi_cn.h"
#include "../common/simulator.h"
#include "include/common/simulator.h"
#include "include/gaudi/gaudi_fw_if.h"
#include "include/hw_ip/mmu/mmu_general.h"
#include "include/hw_ip/mmu/mmu_v1_1.h"
#include "include/gaudi/gaudi_masks.h"
#include "include/common/pci_ids.h"
#include "include/gaudi/gaudi_reg_map.h"
#include "include/gaudi/gaudi_simulator.h"
#include "include/hw_ip/nic/nic_general.h"

#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/sysfs.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/hwmon.h>

static DEFINE_MUTEX(simulator_open);

static struct hl_simulator_device *gaudi_simulator_dev_table[HL_MAX_MINORS];

static struct attribute *gaudi_sim_dev_attrs[] = {
	NULL,
};

static int gaudi_sim_access_dev_mem(struct hl_device *hdev, enum pci_region reg_type,
				u64 addr, u64 *val, enum debugfs_access_type acc_type)
{
	return hl_sim_access_dev_mem(hdev, gaudi_simulator_dev_table[hdev->id],
			reg_type, addr, val, acc_type);
}

static int gaudi_sim_start_device(struct hl_simulator_device *edev);

static void gaudi_simulator_create_device(struct work_struct *work)
{
	struct hl_simulator_device *edev =
			container_of(work, struct hl_simulator_device, work_create.work);
	int rc;

	dev_dbg(edev->dev, "Starting delayed work to create simulated device\n");

	rc = gaudi_sim_start_device(edev);
	if (rc) {
		/* Set hdev to NULL to prevent a call to gaudi_sim_stop_device() */
		edev->hdev = NULL;
		dev_err(edev->dev, "Failed to create Gaudi Simulator device\n");
	}
}

/**
 * gaudi_simulator_open - open function for gaudi simulator device
 *
 * @inode: pointer to inode structure
 * @filp: pointer to file structure
 *
 * Called when the functional simulator opens the gaudi simulator device.
 */
static int gaudi_simulator_open(struct inode *inode, struct file *filp)
{
	u32 minor = iminor(inode) - HLV_SIM_ID_OFFSET;
	struct hl_simulator_device *edev;
	int rc = 0;

	if (minor >= HL_MAX_MINORS) {
		pr_crit("habanalabs: minor is out of bounds %u, can't open sim\n", minor);
		return -EINVAL;
	}

	mutex_lock(&simulator_open);

	edev = gaudi_simulator_dev_table[minor];

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
			"If func-sim is not running, "
			"rmmod and insmod habanalabs.ko before running func-sim again\n");
		rc = -EPERM;
		goto unlock_mutex;
	}

	edev->open = 1;

	dev_dbg(edev->dev,
		"Opening file descriptor on gaudi simulator device\n");

	filp->private_data = edev;
	nonseekable_open(inode, filp);

	mutex_unlock(&simulator_open);

	if ((edev->sram_user_address) && (edev->dram_user_address)) {
		dev_dbg(edev->dev,
			"Creating delayed work to start simulated device\n");
		INIT_DELAYED_WORK(&edev->work_create,
				gaudi_simulator_create_device);
		schedule_delayed_work(&edev->work_create,
					usecs_to_jiffies(1000));
	}

	return 0;

unlock_mutex:
	mutex_unlock(&simulator_open);
	return rc;
}

/**
 * gaudi_simulator_release - release function for gaudi simulator device
 *
 * @inode: pointer to inode structure
 * @filp: pointer to file structure
 *
 * Called when the functional simulator closes the gaudi simulator device.
 */
static int gaudi_simulator_release(struct inode *inode, struct file *filp)
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

	gaudi_simulator_stop(minor);
	hl_sim_remove(minor);

	return 0;
}

static ssize_t gaudi_simulator_read(struct file *filp, char __user *buffer,
		size_t len, loff_t * off)
{
	struct hl_simulator_device *edev = filp->private_data;

	dev_dbg_once(edev->dev, "Reading %zu bytes from %s in offset 0x%llx\n",
			len, filp->f_path.dentry->d_name.name, *off);

	return hl_sim_read_h2c_fifo(edev, filp, buffer, len);
}

static __poll_t gaudi_simulator_poll(struct file *filp,
				struct poll_table_struct *wait)
{
	return hl_sim_poll(filp->private_data, filp, wait);
}

static ssize_t gaudi_simulator_write(struct file *filp,
		const char __user *buff, size_t len, loff_t *off)
{
	struct hl_simulator_device *edev = filp->private_data;

	dev_dbg_once(edev->dev, "Writing %zu bytes to %s in offset 0x%llx\n",
			len, filp->f_path.dentry->d_name.name, *off);

	return hl_sim_write_c2h_fifo(edev, filp, buff, len);
}

/**
 * gaudi_simulator_mmap - mmap function for gaudi simulator device
 *
 * @filp: pointer to file structure
 * @vma: pointer to vm_area_struct of the process
 *
 * Called when the functional simulator does an mmap on gaudi simulator device
 */
static int gaudi_simulator_mmap(struct file *filp, struct vm_area_struct *vma)
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
				"SRAM is already mapped to userspace!!!\n");
			return -EINVAL;
		}
		address = edev->sram;
		address_to_update = &edev->sram_user_address;
		dev_dbg(edev->dev, "mapping SRAM:\n");
	} else if (size == edev->dram_size) {
		if (edev->dram_user_provided_ptr) {
			dev_err(edev->dev,
				"HBM is already allocated in userspace\n");
			return -EINVAL;
		}
		if (edev->dram_user_address) {
			dev_err(edev->dev,
				"HBM is already mapped to userspace!!!\n");
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
				gaudi_simulator_create_device);
		schedule_delayed_work(&edev->work_create,
					usecs_to_jiffies(1000));
	}

	return rc;
}

static int gaudi_simulator_gen_int_ioctl(struct hl_simulator_device *edev,
					void *data)
{
	struct simulator_gen_int_args *args = data;
	struct hl_device *hdev = edev->hdev;

	mutex_lock(edev->irq_mutex);

	if (unlikely(edev->reset))
		goto out;

	gaudi_irq_handler_single(args->id, hdev);

out:
	mutex_unlock(edev->irq_mutex);

	return 0;
}

static int gaudi_simulator_pci_access_ioctl(
		struct hl_simulator_device *edev, void *data)
{
	struct simulator_pci_access_args *args = data;
	void *src_addr, *dst_addr;
	u64 sram_end_address = edev->sram_user_address + SRAM_SIZE;
	u64 hbm_end_address = edev->dram_user_address + edev->dram_size;
	bool is_shmem = false;
	int rc = 0;

	dev_dbg_once(edev->dev,
			"Gaudi simulator PCI access IOCTL details:\n");
	dev_dbg_once(edev->dev, "host == 0x%llx\n", args->host_address);
	dev_dbg_once(edev->dev, "device == 0x%llx\n", args->device_address);
	dev_dbg_once(edev->dev, "size == %u\n", args->length);
	dev_dbg_once(edev->dev, "is_write == %d\n", args->is_write);

	if (hl_mem_area_inside_range(args->device_address, args->length,
			edev->sram_user_address, sram_end_address)) {
		args->device_address -= edev->sram_user_address;
		args->device_address += (u64) (edev->sram);
	} else if (hl_mem_area_inside_range(args->device_address, args->length,
			edev->dram_user_address, hbm_end_address)) {
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

static int gaudi_simulator_reset_device_ioctl(struct hl_simulator_device *edev, void *data)
{
	struct hl_device *hdev = edev->hdev;

	hl_warn(hdev, "Gaudi simulator encountered a failure, going to reset device\n");

	return hl_device_reset(hdev, HL_DRV_RESET_HARD);
}

/*
 * Ioctl function type.
 *
 * \param edev pointer to gaudi simulator device.
 * \param data pointer to arg that was copied from user.
 */
typedef int gaudi_simulator_ioctl_t(struct hl_simulator_device *edev,
					void *data);

struct gaudi_simulator_ioctl_desc {
	unsigned int cmd;
	gaudi_simulator_ioctl_t *func;
};

#define GAUDI_SIMULATOR_IOCTL_DEF(ioctl, _func) \
	[_IOC_NR(ioctl)] = {.cmd = ioctl, .func = _func}

/** Ioctl table */
static const struct gaudi_simulator_ioctl_desc gaudi_simulator_ioctls[] = {
	GAUDI_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_GEN_INT,
			gaudi_simulator_gen_int_ioctl),
	GAUDI_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_PCI_ACCESS_FROM_DEVICE,
			gaudi_simulator_pci_access_ioctl),
	GAUDI_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_RESET_DEVICE,
			gaudi_simulator_reset_device_ioctl),
};

#define GAUDI_SIMULATOR_IOCTL_COUNT	ARRAY_SIZE(gaudi_simulator_ioctls)

static long gaudi_simulator_ioctl(struct file *filep, unsigned int cmd,
			unsigned long arg)
{
	struct hl_simulator_device *edev = filep->private_data;
	gaudi_simulator_ioctl_t *func;
	const struct gaudi_simulator_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);
	char stack_kdata[128];
	char *kdata = NULL;
	unsigned int usize, asize;
	int retcode = -EINVAL;

	if (edev->reset) {
		dev_err(edev->dev, "chip has been reset but got IOCTL\n");
		return -ENXIO;
	}

	if (nr >= GAUDI_SIMULATOR_IOCTL_COUNT)
		goto err_i1;

	if ((nr >= SIMULATOR_COMMAND_START) &&
		(nr < SIMULATOR_COMMAND_END)) {
		u32 hl_size;

		ioctl = &gaudi_simulator_ioctls[nr];

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

static struct file_operations gaudi_simulator_ops = {
	.owner = THIS_MODULE,
	.open = gaudi_simulator_open,
	.release = gaudi_simulator_release,
	.read = gaudi_simulator_read,
	.poll = gaudi_simulator_poll,
	.write = gaudi_simulator_write,
	.mmap = gaudi_simulator_mmap,
	.unlocked_ioctl = gaudi_simulator_ioctl,
	.compat_ioctl = gaudi_simulator_ioctl,
};

static ssize_t device_name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	const char *name = "gaudi_simulator";

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

static int gaudi_sim_start_device(struct hl_simulator_device *edev)
{
	int rc;

	rc = hl_sim_create_hdev(edev);
	if (rc) {
		dev_err(edev->dev, "Failed to create real device for GAUDI simulator\n");
		return rc;
	}

	rc = hl_device_init(edev->hdev);
	if (rc) {
		dev_err(edev->dev, "fatal error during GAUDI simulator init\n");
		rc = -ENODEV;
		goto out_err;
	}

	return 0;

out_err:
	hl_sim_destroy_hdev(edev->hdev);
	return rc;
}

static void gaudi_sim_stop_device(struct hl_device *hdev)
{
	hl_device_fini(hdev);
	hl_sim_destroy_hdev(hdev);
}

int gaudi_simulator_start(struct simulator_start_args *args)
{
	struct hl_simulator_device *edev;
	bool can_put_dev = false;
	int rc;

	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return -ENOMEM;

	edev->irq_mutex = kzalloc(sizeof(*edev->irq_mutex), GFP_KERNEL);
	if (!edev->irq_mutex) {
		kfree(edev);
		return -ENOMEM;
	}

	edev->hclass = args->hclass;
	edev->major = args->major;
	edev->id = args->minor + HLV_SIM_ID_OFFSET;
	edev->rw_reg_timeout = SIM_RW_REG_TIMEOUT_US;
	edev->reset = true;
	edev->single_msi_mode = true; /* force single msi mode */
	edev->virt_dev_type = args->virt_dev_type;
	edev->dram_user_provided_ptr = args->dram_user_pointer;
	edev->sram_user_provided_ptr = args->sram_user_pointer;

	if (args->dram_size_in_mb > 32768) {
		pr_err("habanalabs: HBM size must be <= 32GB");
		rc = -EINVAL;
		goto free_edev;
	}
	edev->dram_size = (u64)args->dram_size_in_mb * SZ_1M;

#if IS_ENABLED(CONFIG_DRM_ACCEL)
	sprintf(edev->name, "hlv%d", args->minor + HLV_SIM_ID_OFFSET);
#else
	sprintf(edev->name, "hlv%d", args->minor / 2 + HLV_SIM_ID_OFFSET);
#endif

	sim_devices_init(edev, edev->hclass, edev->id, &gaudi_simulator_ops,
					edev->name);

	rc = cdev_device_add(&edev->cdev, edev->dev);
	if (rc) {
		dev_err(edev->dev, "Failed to add char device\n");
		goto free_edev;
	}

	/* Allocate shared region between KMD/User and gaudi simulator */
	edev->shmem = vmalloc_user(GAUDI_SIM_SHMEM_SIZE);
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

	/* Allocate or map memory for DDR */
	if (!edev->dram_user_provided_ptr) {
		/* Dram is allocated by kernel */
		edev->dram_vmalloc_address =
			vmalloc_user(edev->dram_size + PAGE_SIZE_2MB);
		if (!edev->dram_vmalloc_address) {
			dev_err(edev->dev,
				"Failed to allocate simulator HBM\n");
			rc = -ENOMEM;
			goto free_sram;
		}
		edev->dram = (void *)round_up((u64)edev->dram_vmalloc_address,
					      PAGE_SIZE_2MB);
	} else {
		/* Dram is allocated by user */
		rc = hl_sim_vmap_user_pages(edev->dram_user_provided_ptr,
				GAUDI_SIM_DRAM_DRV_END_ADDR - DRAM_PHYS_BASE,
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
		goto free_ddr;
	}

	rc = gen_pool_add(edev->pool,
			(u64) edev->shmem, GAUDI_SIM_SHMEM_SIZE, -1);
	if (rc) {
		dev_err(edev->dev,
			"Failed to add memory to shared memory pool\n");
		rc = -ENOMEM;
		goto free_shared_mem_pool;
	}

	spin_lock_init(&edev->h2c_lock);
	rc = kfifo_alloc(&edev->h2c_fifo,
			GAUDI_SIM_PCI_OUTSTANDING * sizeof(void *),
			GFP_KERNEL);
	if (rc) {
		dev_err(edev->dev, "Failed to allocate fifo for h2c\n");
		goto free_shared_mem_pool;
	}

	atomic_set(&edev->h2c_seq, 0);

	spin_lock_init(&edev->c2h_lock);
	init_waitqueue_head(&edev->pollq);
	rc = kfifo_alloc(&edev->c2h_fifo,
			GAUDI_SIM_PCI_OUTSTANDING * sizeof(void *),
			GFP_KERNEL);
	if (rc) {
		dev_err(edev->dev, "Failed to allocate fifo for c2h\n");
		goto free_h2c_fifo;
	}

	mutex_init(edev->irq_mutex);

	dev_info(edev->dev,
		"added %s: Gaudi simulator device [0000:00:%02d.0]\n",
		edev->name, args->minor);

	gaudi_simulator_dev_table[edev->id - HLV_SIM_ID_OFFSET] = edev;

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

	dev_set_drvdata(edev->dev, edev);

	return 0;

remove_name:
	device_remove_file(edev->dev, &dev_attr_device_name);
destroy_mutex:
	mutex_destroy(edev->irq_mutex);
	kfifo_free(&edev->c2h_fifo);
free_h2c_fifo:
	kfifo_free(&edev->h2c_fifo);
free_shared_mem_pool:
	gaudi_simulator_dev_table[edev->id - HLV_SIM_ID_OFFSET] = NULL;
	gen_pool_destroy(edev->pool);
free_ddr:
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

void gaudi_simulator_stop(u32 minor)
{
	struct hl_simulator_device *edev;
	struct simulator_msg *msg;
	int count;

	if (minor >= HL_MAX_MINORS) {
		pr_crit("habanalabs: minor is out of bounds %u, can't stop sim\n", minor);
		return;
	}

	edev = gaudi_simulator_dev_table[minor];

	dev_dbg(edev->dev, "Removing Gaudi simulator device\n");

	/* Make sure work to create simulator has finished */
	cancel_delayed_work_sync(&edev->work_create);

	if (edev->hdev)
		gaudi_sim_stop_device(edev->hdev);

	/* Disable open on device */
	gaudi_simulator_dev_table[minor] = NULL;

	device_remove_file(edev->dev, &dev_attr_device_name);
	device_remove_file(edev->dev, &dev_attr_rw_regs_timeout_us);

	/* Hide device from user */
	cdev_device_del(&edev->cdev, edev->dev);

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

	mutex_destroy(edev->irq_mutex);

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
 * gaudi_sim_rreg - Read an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 *
 * Returns the value of the MMIO register we are asked to read
 *
 */
static u32 gaudi_sim_rreg(struct hl_device *hdev, u32 reg)
{
	struct hl_simulator_device *edev = gaudi_simulator_dev_table[hdev->id];
	u64 reg_addr = CFG_BASE + reg;

	return hl_sim_rreg(hdev, reg_addr, edev);
}

/**
 * gaudi_sim_wreg - Write to an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 * @val: 32-bit value
 *
 * Writes the 32-bit value into the MMIO register
 *
 */
static void gaudi_sim_wreg(struct hl_device *hdev, u32 reg, u32 val)
{
	struct hl_simulator_device *edev = gaudi_simulator_dev_table[hdev->id];
	u64 reg_addr = CFG_BASE + reg;
	hl_sim_wreg(hdev, reg_addr, edev, val);
}

static void gaudi_sim_notify_reset(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = gaudi_simulator_dev_table[hdev->id];

	hl_sim_notify_reset(hdev, edev);
}

/* All the code below this point is the gaudi simulator device implementation */

static u32 gaudi_sim_get_pci_id(struct hl_device *hdev)
{
	if (hdev->asic_type == ASIC_GAUDI_HL2000M_SIM)
		return PCI_IDS_GAUDI_HL2000M_SIMULATOR;

	return PCI_IDS_GAUDI_SIMULATOR;
}

void gaudi_sim_cn_early_init_props_ext(struct gaudi_cn_sim_properties *cn_prop)
{
	cn_prop->nic_drv_addr = GAUDI_SIM_NIC_DRV_ADDR;
	cn_prop->nic_drv_size = GAUDI_SIM_NIC_DRV_SIZE;
	cn_prop->nic_drv_base_addr = GAUDI_SIM_NIC_DRV_BASE_ADDR;
	cn_prop->nic_drv_end_addr = GAUDI_SIM_NIC_DRV_END_ADDR;

	cn_prop->sb_base_addr = GAUDI_SIM_SB_BASE_ADDR;
	cn_prop->swq_base_addr = GAUDI_SIM_SWQ_BASE_ADDR;
	cn_prop->txs_base_addr = GAUDI_SIM_TXS_BASE_ADDR;
	cn_prop->tmr_base_addr = GAUDI_SIM_TMR_BASE_ADDR;
	cn_prop->req_qpc_base_addr = GAUDI_SIM_REQ_QPC_BASE_ADDR;
	cn_prop->res_qpc_base_addr = GAUDI_SIM_RES_QPC_BASE_ADDR;
}

static int gaudi_sim_set_fixed_properties(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = gaudi_simulator_dev_table[hdev->id];
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	rc = gaudi_set_fixed_properties(hdev);
	if (rc) {
		hl_err(hdev, "Failed setting fixed properties\n");
		return rc;
	}

	prop->dram_size = edev->dram_size;
	prop->dram_end_address = prop->dram_base_address + prop->dram_size;
	prop->dram_user_base_address = GAUDI_SIM_DRAM_BASE_ADDR_USER;

	prop->mmu_pgt_addr = GAUDI_SIM_MMU_PAGE_TABLES_ADDR;
	prop->mmu_pgt_size = GAUDI_SIM_MMU_PAGE_TABLES_SIZE;
	prop->dmmu.pgt_size = prop->mmu_pgt_size;
	prop->mmu_cache_mng_addr = GAUDI_SIM_MMU_CACHE_MNG_ADDR;
	prop->mmu_cache_mng_size = GAUDI_SIM_MMU_CACHE_MNG_SIZE;

	prop->cb_pool_cb_cnt = GAUDI_SIM_CB_POOL_CB_CNT;
	prop->cb_pool_cb_size = GAUDI_SIM_CB_POOL_CB_SIZE;
	prop->nic_drv_addr = GAUDI_SIM_NIC_DRV_ADDR;
	prop->nic_drv_size = GAUDI_SIM_NIC_DRV_SIZE;
	prop->pci_id = gaudi_sim_get_pci_id(hdev);

	return 0;
}

static int gaudi_sim_pci_bars_map(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = gaudi_simulator_dev_table[hdev->id];

	/* Simulate SRAM & SM BAR */
	hdev->pcie_bar[SRAM_BAR_ID] = (void __iomem *) edev->sram;
	hl_dbg(hdev, "SRAM at 0x%px\n", hdev->pcie_bar[SRAM_BAR_ID]);

	/* Simulate HBM BAR */
	hdev->pcie_bar[HBM_BAR_ID] = (void __iomem *) edev->dram;
	hl_dbg(hdev, "HBM at 0x%px\n", hdev->pcie_bar[HBM_BAR_ID]);

	/* CFG is not simulated as BAR */
	hdev->rmmio = NULL;

	return 0;
}

static void gaudi_sim_pci_bars_unmap(struct hl_device *hdev)
{
	hdev->pcie_bar[HBM_BAR_ID] = NULL;
	hdev->pcie_bar[SRAM_BAR_ID] = NULL;
	hdev->rmmio = NULL;
}

static int gaudi_sim_early_init(struct hl_device *hdev)
{
	int rc;

	rc = gaudi_sim_set_fixed_properties(hdev);
	if (rc)
		return rc;

	rc = gaudi_sim_pci_bars_map(hdev);
	if (rc)
		goto free_queue_props;

	return 0;

free_queue_props:
	kfree(hdev->asic_prop.hw_queues_props);
	return rc;
}

static int gaudi_sim_early_fini(struct hl_device *hdev)
{
	kfree(hdev->asic_prop.hw_queues_props);
	gaudi_sim_pci_bars_unmap(hdev);

	return 0;
}

static void gaudi_sim_get_nic_info(struct hl_device *hdev)
{
	struct hbl_cn_cpucp_info *cn_cpucp_info = &hdev->asic_prop.cn_props.cpucp_info;
	struct cpucp_info *cpucp_info = &hdev->asic_prop.cpucp_info;
	u32 i, card_location;
	u8 mac[ETH_ALEN];

	/* Assumes HLS1 connections and HLS1 SerDes. */
	cn_cpucp_info->serdes_type = HLS1_SERDES_TYPE;

	for (i = 0 ; i < 3 ; i++)
		mac[i] = HABANALABS_MAC_OUI_1 >> (8 * (2 - i));

	mac[3] = task_pid_nr(current) % 256;
	get_random_bytes(&mac[4], sizeof(mac[4]));

	for (i = 0 ; i < CPUCP_MAX_NICS ; i++) {
		mac[ETH_ALEN - 1] = i;
		memcpy(cn_cpucp_info->mac_addrs[i].mac_addr, mac, ETH_ALEN);
	}

	cn_cpucp_info->link_mask[0] = 0x3FF;
	cn_cpucp_info->link_ext_mask[0] = 0x302;

	card_location = RREG32(mmPSOC_GLOBAL_CONF_BOOT_STRAP_PINS);
	cpucp_info->card_location = cpu_to_le32((card_location >> 22) & 0x7);
}

static int gaudi_sim_cpucp_info_get(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = gaudi_simulator_dev_table[hdev->id];
	struct gaudi_device *gaudi = hdev->asic_specific;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 dram_size;
	int rc;

	if (!(gaudi->hw_cap_initialized & HW_CAP_CPU_Q)) {
		/* Set nic_info by the driver because F/W is disabled on simulator. */
		gaudi_sim_get_nic_info(hdev);
		return 0;
	}

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
			hl_err(hdev,
				"F/W reported invalid HBM size %llu != %llu\n",
				dram_size, edev->dram_size);
			dram_size = edev->dram_size;
		}

		prop->dram_size = dram_size;
		prop->dram_end_address = prop->dram_base_address + dram_size;
	}

	if (!hdev->ignore_fw_nic_info)
		hdev->card_type =
			le32_to_cpu(hdev->asic_prop.cpucp_info.card_type);

	return 0;
}

static int gaudi_sim_sw_init(struct hl_device *hdev)
{
	struct gaudi_device *gaudi;
	int rc;

	/* Allocate device structure */
	gaudi = kzalloc(sizeof(*gaudi), GFP_KERNEL);
	if (!gaudi)
		return -ENOMEM;

	gaudi->cpucp_info_get = gaudi_sim_cpucp_info_get;
	hdev->asic_specific = gaudi;

	rc = gaudi_alloc_cpu_accessible_dma_mem(hdev);
	if (rc)
		goto free_gaudi_device;


	hdev->cpu_accessible_dma_pool = gen_pool_create(ilog2(32), -1);
	if (!hdev->cpu_accessible_dma_pool) {
		hl_err(hdev,
			"Failed to create CPU accessible DMA pool\n");
		rc = -ENOMEM;
		goto free_cpu_dma_mem;
	}

	rc = gen_pool_add(hdev->cpu_accessible_dma_pool,
				(uintptr_t) hdev->cpu_accessible_dma_mem,
				HL_CPU_ACCESSIBLE_MEM_SIZE, -1);
	if (rc) {
		hl_err(hdev,
			"Failed to add memory to CPU accessible DMA pool\n");
		rc = -EFAULT;
		goto free_cpu_accessible_dma_pool;
	}

	rc = gaudi_alloc_internal_qmans_pq_mem(hdev);
	if (rc)
		goto free_cpu_accessible_dma_pool;

	mutex_init(&gaudi->hw_queues_lock_mutex);

	hdev->supports_sync_stream = true;
	hdev->supports_coresight = false;
	hdev->supports_default_cs = true;
	hdev->supports_staged_submission = true;
	hdev->supports_wait_for_multi_cs = true;

	hdev->asic_funcs->set_pci_memory_regions(hdev);
	hdev->stream_master_qid_arr =
				hdev->asic_funcs->get_stream_master_qid_arr();
	hdev->stream_master_qid_arr_size = GAUDI_STREAM_MASTER_ARR_SIZE;

	return 0;

free_cpu_accessible_dma_pool:
	gen_pool_destroy(hdev->cpu_accessible_dma_pool);
free_cpu_dma_mem:
	if (!hdev->asic_prop.fw_security_enabled)
		GAUDI_CPU_TO_PCI_ADDR(hdev->cpu_accessible_dma_address,
					hdev->cpu_pci_msb_addr);
	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, hdev->cpu_accessible_dma_mem,
					hdev->cpu_accessible_dma_address);
free_gaudi_device:
	kfree(gaudi);

	return rc;
}

static int gaudi_sim_sw_fini(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	mutex_destroy(&gaudi->hw_queues_lock_mutex);

	gaudi_free_internal_qmans_pq_mem(hdev);

	gen_pool_destroy(hdev->cpu_accessible_dma_pool);

	GAUDI_CPU_TO_PCI_ADDR(hdev->cpu_accessible_dma_address,
				hdev->cpu_pci_msb_addr);
	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, hdev->cpu_accessible_dma_mem,
					hdev->cpu_accessible_dma_address);

	kfree(gaudi);

	return 0;
}

static void gaudi_sim_halt_engines(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	struct hl_simulator_device *edev = gaudi_simulator_dev_table[hdev->id];

	/*
	 * Mark the NIC as in reset to avoid any new NIC accesses to the
	 * H/W. This must be done before we stop the CPU as the NIC
	 * might use it e.g. get/set EEPROM data.
	 */
	hl_cn_hard_reset_prepare(hdev);

	gaudi_stop_nic_qmans(hdev);
	gaudi_stop_mme_qmans(hdev);
	gaudi_stop_tpc_qmans(hdev);
	gaudi_stop_hbm_dma_qmans(hdev);
	gaudi_stop_pci_dma_qmans(hdev);

	gaudi_pci_dma_stall(hdev);
	gaudi_hbm_dma_stall(hdev);
	gaudi_tpc_stall(hdev);
	gaudi_mme_stall(hdev);

	gaudi_disable_nic_qmans(hdev);
	gaudi_disable_mme_qmans(hdev);
	gaudi_disable_tpc_qmans(hdev);
	gaudi_disable_hbm_dma_qmans(hdev);
	gaudi_disable_pci_dma_qmans(hdev);

	hl_cn_stop(hdev);

	gaudi_sim_notify_reset(hdev);

	/* Give simulator some time to prepare for reset */
	msleep(GAUDI_SIM_HALT_WAIT_MSEC);

	edev->reset = true;

	/* Flush any in progress handling of the gen_int ioctl */
	mutex_lock(edev->irq_mutex);
	mutex_unlock(edev->irq_mutex);
}

static int gaudi_sim_init_cpu(struct hl_device *hdev)
{
	/*
	 * The device CPU works with 40 bits addresses.
	 * This register sets the extension to 50 bits.
	 */
	WREG32(mmCPU_IF_CPU_MSB_ADDR, hdev->cpu_pci_msb_addr);

	return 0;
}

static int gaudi_sim_mmu_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi_device *gaudi = hdev->asic_specific;
	u64 hop0_addr;
	int rc, i;

	if (gaudi->hw_cap_initialized & HW_CAP_MMU)
		return 0;

	for (i = 0 ; i < prop->max_asid ; i++) {
		hop0_addr = prop->mmu_pgt_addr +
				(i * prop->dmmu.hop_table_size);

		rc = gaudi_mmu_update_asid_hop0_addr(hdev, i, hop0_addr);
		if (rc) {
			hl_err(hdev,
				"failed to set hop0 addr for asid %d\n", i);
			return rc;
		}
	}

	/* init MMU cache manage page */
	WREG32(mmSTLB_CACHE_INV_BASE_39_8, prop->mmu_cache_mng_addr >> 8);
	WREG32(mmSTLB_CACHE_INV_BASE_49_40, prop->mmu_cache_mng_addr >> 40);

	rc = hl_mmu_invalidate_cache(hdev, true, 0);
	if (rc)
		return rc;

	WREG32(mmMMU_UP_MMU_ENABLE, 1);
	WREG32(mmMMU_UP_SPI_MASK, 0xF);

	WREG32(mmSTLB_HOP_CONFIGURATION,
			hdev->mmu_huge_page_opt ? 0x30440 : 0x40440);

	/*
	 * The H/W expects the first PI after init to be 1. After wraparound
	 * we'll write 0.
	 */
	gaudi->mmu_cache_inv_pi = 1;

	gaudi->hw_cap_initialized |= HW_CAP_MMU;

	return 0;
}

static int gaudi_sim_hw_init(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = gaudi_simulator_dev_table[hdev->id];
	int rc = 0;

	gaudi_init_pci_dma_qmans(hdev);

	gaudi_init_hbm_dma_qmans(hdev);

	rc = gaudi_sim_init_cpu(hdev);
	if (rc) {
		hl_err(hdev, "failed to initialize CPU\n");
		return rc;
	}

	rc = gaudi_sim_mmu_init(hdev);
	if (rc) {
		hl_err(hdev, "failed to init MMU, error: %d\n", rc);
		return rc;
	}

	gaudi_init_security(hdev);

	gaudi_init_mme_qmans(hdev);

	gaudi_init_tpc_qmans(hdev);

	gaudi_init_nic_qmans(hdev);

	rc = gaudi_init_cpu_queues(hdev, GAUDI_CPU_TIMEOUT_USEC);
	if (rc) {
		hl_err(hdev, "failed to initialize CPU H/W queues %d\n",
			rc);
		return rc;
	}

	edev->reset = false;

	return rc;
}

static int gaudi_sim_hw_fini(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	struct gaudi_device *gaudi = hdev->asic_specific;
	u32 status, reset_timeout_ms, reset_timeout_us;
	int rc;

	if (!hard_reset) {
		hl_err(hdev, "GAUDI doesn't support soft-reset\n");
		return 0;
	}

	reset_timeout_ms = GAUDI_SIM_RESET_WAIT_MSEC;
	reset_timeout_us = reset_timeout_ms * 1000;

	/* Configure the reset registers. Must be done as early as
	 * possible in case we fail during H/W initialization
	 */
	WREG32(mmPSOC_GLOBAL_CONF_SOFT_RST_CFG_H,
					(CFG_RST_H_DMA_MASK |
					CFG_RST_H_MME_MASK |
					CFG_RST_H_SM_MASK |
					CFG_RST_H_TPC_7_MASK));

	WREG32(mmPSOC_GLOBAL_CONF_SOFT_RST_CFG_L, CFG_RST_L_TPC_MASK);

	WREG32(mmPSOC_GLOBAL_CONF_SW_ALL_RST_CFG_H,
					(CFG_RST_H_HBM_MASK |
					CFG_RST_H_TPC_7_MASK |
					CFG_RST_H_NIC_MASK |
					CFG_RST_H_SM_MASK |
					CFG_RST_H_DMA_MASK |
					CFG_RST_H_MME_MASK |
					CFG_RST_H_CPU_MASK |
					CFG_RST_H_MMU_MASK));

	WREG32(mmPSOC_GLOBAL_CONF_SW_ALL_RST_CFG_L,
					(CFG_RST_L_IF_MASK |
					CFG_RST_L_PSOC_MASK |
					CFG_RST_L_TPC_MASK));

	WREG32(mmPSOC_GLOBAL_CONF_SW_ALL_RST,
		1 << PSOC_GLOBAL_CONF_SW_ALL_RST_IND_SHIFT);

	hl_dbg(hdev,
		"Issued HARD reset command, going to wait %dms\n",
		reset_timeout_ms);

	/* Wait a certain amount of time before checking if the reset has
	 * finished
	 */
	rc = hl_poll_timeout(hdev, mmPSOC_GLOBAL_CONF_BTM_FSM, status,
		(status & PSOC_GLOBAL_CONF_BTM_FSM_STATE_MASK),
		10000, reset_timeout_us);

	if (rc == -ETIMEDOUT)
		hl_err(hdev,
			"Timeout while waiting for device to reset (status = 0x%x)\n",
			status);

	if (gaudi) {
		gaudi->hw_cap_initialized &= ~(HW_CAP_CPU | HW_CAP_CPU_Q | HW_CAP_HBM |
						HW_CAP_PCI_DMA | HW_CAP_MME | HW_CAP_TPC_MASK |
						HW_CAP_HBM_DMA | HW_CAP_PLL | HW_CAP_NIC_MASK |
						HW_CAP_MMU | HW_CAP_SRAM_SCRAMBLER |
						HW_CAP_HBM_SCRAMBLER | HW_CAP_NIC_DRV);

		memset(gaudi->events_stat, 0, sizeof(gaudi->events_stat));
	}
	return 0;
}

static int gaudi_sim_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
			void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	int rc;

	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY | VM_NORESERVE);

	rc = remap_vmalloc_range(vma, cpu_addr, 0);
	if (rc)
		hl_err(hdev, "remap vmalloc error %d", rc);

	return rc;
}

static void *gaudi_sim_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	struct hl_simulator_device *edev = gaudi_simulator_dev_table[hdev->id];

	void *address = (void *) gen_pool_alloc(edev->pool, size);

	if (address) {
		if (flags & __GFP_ZERO)
			memset(address, 0, size);

		*dma_handle = virt_to_phys(address);
		if (*dma_handle > HOST_PHYS_SIZE)
			hl_crit(hdev, "invalid dma addr 0x%llx\n",
					*dma_handle);

		/* Shift to the device's base physical address of host memory */
		*dma_handle += HOST_PHYS_BASE;
	}

	return address;
}

static void gaudi_sim_dma_free_coherent(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle)
{
	struct hl_simulator_device *edev = gaudi_simulator_dev_table[hdev->id];

	gen_pool_free(edev->pool, (u64) cpu_addr, size);
}

static void *gaudi_sim_dma_pool_zalloc(struct hl_device *hdev, size_t size,
					gfp_t mem_flags, dma_addr_t *dma_handle)
{
	if (size > GAUDI_SIM_DMA_POOL_BLK_SIZE)
		return NULL;

	return gaudi_sim_dma_alloc_coherent(hdev, PAGE_SIZE, dma_handle,
					mem_flags | __GFP_ZERO);
}

static void gaudi_sim_dma_pool_free(struct hl_device *hdev, void *vaddr,
				dma_addr_t dma_addr)
{
	gaudi_sim_dma_free_coherent(hdev, PAGE_SIZE, vaddr, dma_addr);
}

static u64 gaudi_sim_read_pte(struct hl_device *hdev, u64 addr)
{
	u64 val;

	if (hdev->reset_info.hard_reset_pending)
		return U64_MAX;

	hl_sim_read_dram(gaudi_simulator_dev_table[hdev->id], &val,
			 (addr - DRAM_PHYS_BASE), sizeof(val));

	return val;
}

static void gaudi_sim_write_pte(struct hl_device *hdev, u64 addr, u64 val)
{
	if (hdev->reset_info.hard_reset_pending)
		return;

	hl_sim_write_dram(gaudi_simulator_dev_table[hdev->id],
			  (addr - DRAM_PHYS_BASE), &val, sizeof(val));
}

static int gaudi_sim_suspend(struct hl_device *hdev)
{
	return 0;
}

static int gaudi_sim_resume(struct hl_device *hdev)
{
	return 0;
}

static int gaudi_sim_debug_coresight(struct hl_device *hdev, struct hl_ctx *ctx, void *data)
{
	hl_err(hdev, "CoreSight not supported in simulator\n");

	return -ENXIO;
}

static bool gaudi_sim_is_device_idle(struct hl_device *hdev, u64 *mask_arr, u8 mask_len,
					struct engines_data *e)
{
	return true;
}

static void gaudi_sim_hw_queues_lock(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	mutex_lock(&gaudi->hw_queues_lock_mutex);
}

static void gaudi_sim_hw_queues_unlock(struct hl_device *hdev)
{
	struct gaudi_device *gaudi = hdev->asic_specific;

	mutex_unlock(&gaudi->hw_queues_lock_mutex);
}

static int gaudi_sim_get_eeprom_data(struct hl_device *hdev, void *data,
		size_t max_size)
{
	const char *str = "no EEPROM data on simulator";

	memcpy(data, str, strlen(str) + 1);

	return 0;
}

static void gaudi_sim_halt_coresight(struct hl_device *hdev, struct hl_ctx *ctx)
{

}

static void gaudi_sim_add_device_attr(struct hl_device *hdev,
					struct attribute_group *dev_clk_attr_grp,
					struct attribute_group *dev_vrm_attr_grp)
{
	dev_clk_attr_grp->attrs = gaudi_sim_dev_attrs;
	dev_vrm_attr_grp->attrs = gaudi_sim_dev_attrs;
}

static int gaudi_sim_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard, u32 flags)
{
	if (hdev->simulator_crashed)
		return 0;

	return gaudi_mmu_invalidate_cache(hdev, is_hard, flags);
}

static int gaudi_sim_mmu_invalidate_cache_range(struct hl_device *hdev,
		bool is_hard, u32 flags, u32 asid, u64 va, u64 size)
{
	if (hdev->simulator_crashed)
		return 0;

	/* Treat as invalidate all */
	return gaudi_mmu_invalidate_cache(hdev, is_hard, flags);
}

static int gaudi_sim_send_device_activity(struct hl_device *hdev, bool open)
{
	return 0;
}

static int gaudi_sim_pll_info_get(struct hl_device *hdev, u32 pll_index,
		u16 *pll_freq_arr)
{
	/*
	 * in simulation PLLs are not supported.
	 */
	memset(pll_freq_arr, 0x0, sizeof(u16) * HL_PLL_NUM_OUTPUTS);

	return 0;
}

static int gaudi_sim_get_reg_pcie_addr(struct hl_device *hdev, u32 reg, u64 *pci_addr)
{
	return -EINVAL;
}

static const struct hl_asic_funcs gaudi_sim_funcs = {
	.early_init = gaudi_sim_early_init,
	.early_fini = gaudi_sim_early_fini,
	.late_init = gaudi_late_init,
	.late_fini = gaudi_late_fini,
	.sw_init = gaudi_sim_sw_init,
	.sw_fini = gaudi_sim_sw_fini,
	.hw_init = gaudi_sim_hw_init,
	.hw_fini = gaudi_sim_hw_fini,
	.halt_engines = gaudi_sim_halt_engines,
	.suspend = gaudi_sim_suspend,
	.resume = gaudi_sim_resume,
	.mmap = gaudi_sim_mmap,
	.ring_doorbell = gaudi_ring_doorbell,
	.pqe_write = gaudi_pqe_write,
	.asic_dma_alloc_coherent = gaudi_sim_dma_alloc_coherent,
	.asic_dma_free_coherent = gaudi_sim_dma_free_coherent,
	.scrub_device_mem = gaudi_scrub_device_mem,
	.scrub_device_dram = gaudi_scrub_device_dram,
	.get_int_queue_base = gaudi_get_int_queue_base,
	.test_queues = gaudi_test_queues,
	.asic_dma_pool_zalloc = gaudi_sim_dma_pool_zalloc,
	.asic_dma_pool_free = gaudi_sim_dma_pool_free,
	.cpu_accessible_dma_pool_alloc = gaudi_cpu_accessible_dma_pool_alloc,
	.cpu_accessible_dma_pool_free = gaudi_cpu_accessible_dma_pool_free,
	.dma_unmap_sgtable = hl_sim_dma_unmap_sgtable,
	.cs_parser = gaudi_cs_parser,
	.dma_map_sgtable = hl_sim_dma_map_sgtable,
	.add_end_of_cb_packets = gaudi_add_end_of_cb_packets,
	.update_eq_ci = gaudi_update_eq_ci,
	.context_switch = gaudi_context_switch,
	.restore_phase_topology = gaudi_restore_phase_topology,
	.debugfs_read_dma = gaudi_debugfs_read_dma,
	.add_device_attr = gaudi_sim_add_device_attr,
	.handle_eqe = gaudi_handle_eqe,
	.get_events_stat = gaudi_get_events_stat,
	.read_pte = gaudi_sim_read_pte,
	.write_pte = gaudi_sim_write_pte,
	.mmu_invalidate_cache = gaudi_sim_mmu_invalidate_cache,
	.mmu_invalidate_cache_range = gaudi_sim_mmu_invalidate_cache_range,
	.mmu_prefetch_cache_range = NULL,
	.send_heartbeat = gaudi_send_heartbeat,
	.debug_coresight = gaudi_sim_debug_coresight,
	.is_device_idle = gaudi_sim_is_device_idle,
	.compute_reset_late_init = gaudi_compute_reset_late_init,
	.hw_queues_lock = gaudi_sim_hw_queues_lock,
	.hw_queues_unlock = gaudi_sim_hw_queues_unlock,
	.get_eeprom_data = gaudi_sim_get_eeprom_data,
	.get_monitor_dump = gaudi_get_monitor_dump,
	.send_cpu_message = gaudi_send_cpu_message,
	.cn_init = hl_cn_init,
	.cn_fini = hl_cn_fini,
	.cn_control = hl_cn_control,
	.pci_bars_map = NULL,
	.init_iatu = NULL,
	.rreg = gaudi_sim_rreg,
	.wreg = gaudi_sim_wreg,
	.get_reg_pcie_addr = gaudi_sim_get_reg_pcie_addr,
	.halt_coresight = gaudi_sim_halt_coresight,
	.ctx_init = gaudi_ctx_init,
	.ctx_fini = gaudi_ctx_fini,
	.pre_schedule_cs = gaudi_pre_schedule_cs,
	.get_queue_id_for_cq = gaudi_get_queue_id_for_cq,
	.load_firmware_to_device = NULL,
	.load_boot_fit_to_device = NULL,
	.gen_signal_cb = gaudi_gen_signal_cb,
	.gen_wait_cb = gaudi_gen_wait_cb,
	.reset_sob = gaudi_reset_sob,
	.reset_sob_group = gaudi_reset_sob_group,
	.get_device_time = gaudi_get_device_time,
	.pb_print_security_errors = NULL,
	.collective_wait_init_cs = gaudi_collective_wait_init_cs,
	.collective_wait_create_jobs = gaudi_collective_wait_create_jobs,
	.get_dec_base_addr = NULL,
	.scramble_addr = hl_mmu_scramble_addr,
	.descramble_addr = hl_mmu_descramble_addr,
	.ack_protection_bits_errors = gaudi_ack_protection_bits_errors,
	.get_hw_block_id = gaudi_get_hw_block_id,
	.hw_block_mmap = gaudi_block_mmap,
	.enable_events_from_fw = gaudi_enable_events_from_fw,
	.ack_mmu_errors = gaudi_ack_mmu_page_fault_or_access_error,
	.map_pll_idx_to_fw_idx = gaudi_map_pll_idx_to_fw_idx,
	.init_cpu_scrambler_dram = NULL,
	.state_dump_init = gaudi_state_dump_init,
	.get_sob_addr = &gaudi_get_sob_addr,
	.set_pci_memory_regions = gaudi_set_pci_memory_regions,
	.get_stream_master_qid_arr = gaudi_get_stream_master_qid_arr,
	.mmu_get_real_page_size = hl_mmu_get_real_page_size,
	.cn_funcs = &gaudi_cn_funcs,
	.access_dev_mem = gaudi_sim_access_dev_mem,
	.set_dram_bar_base = NULL,
	.init_firmware_preload_params = NULL,
	.send_device_activity = gaudi_sim_send_device_activity,
	.read_fetch_memory_block = NULL,
	.fw_security_emulation_init = gaudi_fw_security_emulation_init,
	.fw_security_emulation_fini = gaudi_fw_security_emulation_fini,
	.pll_info_get = gaudi_sim_pll_info_get,
	.set_dram_properties = gaudi_set_dram_properties,
	.set_priv_assertions = gaudi_set_priv_assertions,
	.set_binning_masks = gaudi_set_binning_masks,
	.is_irq_enabled = gaudi_is_irq_enabled,
};

/**
 * gaudi_sim_set_asic_funcs - set GAUDI Simulator function pointers
 *
 * @hdev: pointer to hl_device structure
 *
 */
void gaudi_sim_set_asic_funcs(struct hl_device *hdev)
{
	hdev->asic_funcs = &gaudi_sim_funcs;
}
