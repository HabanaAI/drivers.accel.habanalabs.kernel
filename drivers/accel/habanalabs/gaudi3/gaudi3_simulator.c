// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021-2024 HabanaLabs, Ltd.
 * Copyright (C) 2024-2025, Intel Corporation.
 * All Rights Reserved.
 */

#define pr_fmt(fmt)			"habanalabs: " fmt

#include "gaudi3P.h"
#include "gaudi3_masks.h"
#include "gaudi3_cn.h"
#include "../common/simulator.h"
#include "include/common/simulator.h"
#include "include/common/pci_ids.h"
#include "../include/gaudi3/gaudi3_reg_map.h"
#include "../include/hw_ip/nic/nic_general.h"

#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/sysfs.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/hwmon.h>

/* DRAM Memory Map */
#define GAUDI3_SIM_NIC_DRV_SIZE		NIC_DRV_SIZE
#define GAUDI3_SIM_DRAM_DRV_END_ADDR	(DRAM_PHYS_BASE + GAUDI3_SIM_NIC_DRV_SIZE)

static DEFINE_MUTEX(simulator_open);

static struct hl_simulator_device *gaudi3_simulator_dev_table[HL_MAX_MINORS];

static struct attribute *gaudi3_sim_dev_attrs[] = {
	NULL,
};

static int gaudi3_sim_access_dev_mem(struct hl_device *hdev, enum pci_region reg_type,
				u64 addr, u64 *val, enum debugfs_access_type acc_type)
{
	return hl_sim_access_dev_mem(hdev, gaudi3_simulator_dev_table[hdev->id],
			reg_type, addr, val, acc_type);
}

static int gaudi3_sim_start_device(struct hl_simulator_device *edev);

static void gaudi3_simulator_create_device(struct work_struct *work)
{
	struct hl_simulator_device *edev =
			container_of(work, struct hl_simulator_device, work_create.work);
	int rc;

	dev_dbg(edev->dev, "Starting delayed work to create simulated device\n");

	rc = gaudi3_sim_start_device(edev);
	if (rc) {
		/* Set hdev to NULL to prevent a call to gaudi3_sim_stop_device() */
		edev->hdev = NULL;
		dev_err(edev->dev, "Failed to create Gaudi3 Simulator device\n");
	}
}

/**
 * gaudi3_simulator_open - open function for gaudi3 simulator device
 *
 * @inode: pointer to inode structure
 * @filp: pointer to file structure
 *
 * Called when the functional simulator opens the gaudi3 simulator device.
 */
static int gaudi3_simulator_open(struct inode *inode, struct file *filp)
{
	u32 minor = iminor(inode) - HLV_SIM_ID_OFFSET;
	struct hl_simulator_device *edev;
	int rc = 0;

	if (minor >= HL_MAX_MINORS) {
		pr_crit("habanalabs: minor is out of bounds %u, can't open sim\n", minor);
		return -EINVAL;
	}

	mutex_lock(&simulator_open);

	edev = gaudi3_simulator_dev_table[minor];

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
			"If func-sim is not running rmmod and insmod habanalabs.ko before running func-sim again\n");
		rc = -EPERM;
		goto unlock_mutex;
	}

	edev->open = 1;

	dev_dbg(edev->dev,
		"Opening file descriptor on gaudi3 simulator device\n");

	filp->private_data = edev;
	nonseekable_open(inode, filp);

	mutex_unlock(&simulator_open);

	if ((edev->sram_user_address) && (edev->dram_user_address)) {
		dev_dbg(edev->dev,
			"Creating delayed work to start simulated device\n");
		INIT_DELAYED_WORK(&edev->work_create,
				gaudi3_simulator_create_device);
		schedule_delayed_work(&edev->work_create,
					usecs_to_jiffies(1000));
	}

	return 0;

unlock_mutex:
	mutex_unlock(&simulator_open);
	return rc;
}

/**
 * gaudi3_simulator_release - release function for gaudi3 simulator device
 *
 * @inode: pointer to inode structure
 * @filp: pointer to file structure
 *
 * Called when the functional simulator closes the gaudi3 simulator device.
 */
static int gaudi3_simulator_release(struct inode *inode, struct file *filp)
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

	gaudi3_simulator_stop(minor);
	hl_sim_remove(minor);

	return 0;
}

static ssize_t gaudi3_simulator_read(struct file *filp, char __user *buffer,
		size_t len, loff_t *off)
{
	struct hl_simulator_device *edev = filp->private_data;

	dev_dbg_once(edev->dev, "Reading %zu bytes from %s in offset 0x%llx\n",
			len, filp->f_path.dentry->d_name.name, *off);

	return hl_sim_read_h2c_fifo(edev, filp, buffer, len);
}

static __poll_t gaudi3_simulator_poll(struct file *filp,
				struct poll_table_struct *wait)
{
	return hl_sim_poll(filp->private_data, filp, wait);
}

static ssize_t gaudi3_simulator_write(struct file *filp,
		const char __user *buff, size_t len, loff_t *off)
{
	struct hl_simulator_device *edev = filp->private_data;

	dev_dbg_once(edev->dev, "Writing %zu bytes to %s in offset 0x%llx\n",
			len, filp->f_path.dentry->d_name.name, *off);

	return hl_sim_write_c2h_fifo(edev, filp, buff, len);
}

static int gaudi3_simulator_gen_int_ioctl(struct hl_simulator_device *edev,
					void *data)
{
	struct simulator_gen_int_args *args = data;

	mutex_lock(&edev->irq_mutex[args->id]);

	if (unlikely(edev->reset))
		goto out;

	hl_sim_send_irq(edev, args->id);

out:
	mutex_unlock(&edev->irq_mutex[args->id]);

	return 0;
}

static int gaudi3_simulator_pci_access_ioctl(struct hl_simulator_device *edev, void *data)
{
	struct simulator_pci_access_args *args = data;
	void *src_addr, *dst_addr;
	int not_copied;

	dev_dbg_once(edev->dev, "Gaudi3 simulator PCI access IOCTL details:\n");
	dev_dbg_once(edev->dev, "host == 0x%llx\n", args->host_address);
	dev_dbg_once(edev->dev, "device == 0x%llx\n", args->device_address);
	dev_dbg_once(edev->dev, "size == %u\n", args->length);
	dev_dbg_once(edev->dev, "is_write == %d\n", args->is_write);

	if (args->is_write) {
		src_addr = (void *) (args->device_address);
		dst_addr = phys_to_virt(args->host_address);
		not_copied = copy_from_user(dst_addr, (void __user *)src_addr, args->length);
	} else {
		src_addr = phys_to_virt(args->host_address);
		dst_addr = (void *) (args->device_address);
		not_copied = copy_to_user((void __user *)dst_addr, src_addr, args->length);
	}

	if (not_copied) {
		dev_err(edev->dev, "Error copying %u bytes %s simulator. Copied %d\n",
			args->length, (args->is_write) ? "from" : "to", args->length - not_copied);
		return -EFAULT;
	}

	return 0;
}

static int gaudi3_simulator_reset_device_ioctl(struct hl_simulator_device *edev, void *data)
{
	struct hl_device *hdev = edev->hdev;

	hl_warn(hdev, "Gaudi3 simulator encountered a failure, going to reset device\n");

	return hl_device_reset(hdev, HL_DRV_RESET_HARD);
}

static int gaudi3_simulator_memory_ioctl(struct hl_simulator_device *edev, void *data)
{
	struct simulator_memory_args *args = data;
	int rc;

	dev_dbg(edev->dev, "receive memory ioctl. op=%d\n", args->op);
	switch (args->op) {
	case MEMORY_RELEASE_SHARED_OP:
		rc = hl_sim_release_shared_block(edev, args);
		break;
	case MEMORY_CREATE_SHARED_OP:
		rc = hl_sim_create_shared_block(edev, args);
		break;
	default:
		dev_err(edev->dev, "Gaudi3 simulator wrong op: %d for ioctl memory\n", args->op);
		rc = -EINVAL;
	}

	return rc;
}

/*
 * Ioctl function type.
 *
 * \param edev pointer to gaudi3 simulator device.
 * \param data pointer to arg that was copied from user.
 */
typedef int gaudi3_simulator_ioctl_t(struct hl_simulator_device *edev,
					void *data);

struct gaudi3_simulator_ioctl_desc {
	unsigned int cmd;
	gaudi3_simulator_ioctl_t *func;
};

#define GAUDI3_SIMULATOR_IOCTL_DEF(ioctl, _func) \
	[_IOC_NR(ioctl)] = {.cmd = ioctl, .func = _func}

/** Ioctl table */
static const struct gaudi3_simulator_ioctl_desc gaudi3_simulator_ioctls[] = {
	GAUDI3_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_GEN_INT,
			gaudi3_simulator_gen_int_ioctl),
	GAUDI3_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_PCI_ACCESS_FROM_DEVICE,
			gaudi3_simulator_pci_access_ioctl),
	GAUDI3_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_RESET_DEVICE,
			gaudi3_simulator_reset_device_ioctl),
	GAUDI3_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_MEMORY,
			gaudi3_simulator_memory_ioctl),
};

#define GAUDI3_SIMULATOR_IOCTL_COUNT	ARRAY_SIZE(gaudi3_simulator_ioctls)

static long gaudi3_simulator_ioctl(struct file *filep, unsigned int cmd,
			unsigned long arg)
{
	struct hl_simulator_device *edev = filep->private_data;
	gaudi3_simulator_ioctl_t *func;
	const struct gaudi3_simulator_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);
	char stack_kdata[128];
	char *kdata = NULL;
	unsigned int usize, asize;
	int retcode = -EINVAL;

	if (nr >= GAUDI3_SIMULATOR_IOCTL_COUNT)
		goto err_i1;

	if ((nr >= SIMULATOR_COMMAND_START) &&
		(nr < SIMULATOR_COMMAND_END)) {
		u32 hl_size;

		ioctl = &gaudi3_simulator_ioctls[nr];

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

static const struct file_operations gaudi3_simulator_ops = {
	.owner = THIS_MODULE,
	.open = gaudi3_simulator_open,
	.release = gaudi3_simulator_release,
	.read = gaudi3_simulator_read,
	.poll = gaudi3_simulator_poll,
	.write = gaudi3_simulator_write,
	.unlocked_ioctl = gaudi3_simulator_ioctl,
	.compat_ioctl = gaudi3_simulator_ioctl,
};

static ssize_t device_name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	const char *name = "gaudi3_simulator";

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

static int gaudi3_sim_start_device(struct hl_simulator_device *edev)
{
	int rc;

	rc = hl_sim_create_hdev(edev);
	if (rc) {
		dev_err(edev->dev, "Failed to create real device for GAUDI3 simulator\n");
		return rc;
	}

	hl_sim_set_priv_assertions(edev, true);

	rc = hl_device_init(edev->hdev);
	if (rc) {
		dev_err(edev->dev, "fatal error during GAUDI3 simulator init\n");
		rc = -ENODEV;
		goto out_err;
	}

	return 0;

out_err:
	hl_sim_destroy_hdev(edev->hdev);
	return rc;
}

static void gaudi3_sim_stop_device(struct hl_device *hdev)
{
	hl_device_fini(hdev);
	hl_sim_destroy_hdev(hdev);
}

int gaudi3_simulator_start(struct simulator_start_args *args)
{
	struct hl_simulator_device *edev;
	bool can_put_dev = false;
	int rc, i;

	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return -ENOMEM;

	edev->irq_mutex = kcalloc(GAUDI3_MSIX_ENTRIES,
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

	if (!args->sram_dram_user_pointer) {
		pr_err("habanalabs: A unified sram/dram pointer must be provided\n");
		rc = -EINVAL;
		goto free_edev;
	}

	if (args->dram_size_in_mb > 0x20000) {
		pr_err("habanalabs: HBM size must be <= 128GB\n");
		rc = -EINVAL;
		goto free_edev;
	}
	edev->dram_size = (u64)args->dram_size_in_mb * SZ_1M;
	edev->sram_size = (u64)args->sram_size_in_mb * SZ_1M;

#if IS_ENABLED(CONFIG_DRM_ACCEL)
	sprintf(edev->name, "hlv%d", args->minor + HLV_SIM_ID_OFFSET);
#else
	sprintf(edev->name, "hlv%d", args->minor / 2 + HLV_SIM_ID_OFFSET);
#endif

	sim_devices_init(edev, edev->hclass, edev->id, &gaudi3_simulator_ops,
					edev->name);

	rc = cdev_device_add(&edev->cdev, edev->dev);
	if (rc) {
		dev_err(edev->dev, "Failed to add char device\n");
		goto free_edev;
	}

	/* Allocate shared region between KMD/User and gaudi3 simulator */
	edev->shmem = vmalloc_user(SIM_SHMEM_SIZE);
	if (!edev->shmem) {
		dev_err(edev->dev,
			"Failed to allocate simulator shared memory\n");
		rc = -ENOMEM;
		goto delete_cdev;
	}

	/*
	 * SRAM and DRAM are allocated by user, we only do the mapping.
	 * Note - we map only what we need for direct access which is sram and the part
	 * of the dram needed for the NIC
	 */
	rc = hl_sim_vmap_user_pages(args->sram_dram_user_pointer,
		edev->sram_size + (GAUDI3_SIM_DRAM_DRV_END_ADDR - DRAM_PHYS_BASE),
		&edev->user_sram_dram, false);
	if (rc) {
		dev_err(edev->dev,
			"Error during vmap user SRAM/DRAM address\n");
		goto free_shmem;
	}

	if (edev->sram_size) {
		edev->sram = edev->user_sram_dram.vaddr;
		edev->sram_user_address = args->sram_dram_user_pointer;
		edev->dram = edev->sram + edev->sram_size;
		edev->dram_user_address = edev->sram_user_address + edev->sram_size;

	} else {
		edev->dram = edev->user_sram_dram.vaddr;
		edev->dram_user_address = args->sram_dram_user_pointer;
	}
	edev->dram_user_provided_ptr = edev->dram_user_address;
	edev->dram_off_in_user_sram_dram = edev->sram_size;

	edev->pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!edev->pool) {
		dev_err(edev->dev, "Failed to create shared memory pool\n");
		rc = -ENOMEM;
		goto free_sram_dram;
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

	for (i = 0 ; i < GAUDI3_MSIX_ENTRIES ; i++)
		mutex_init(&edev->irq_mutex[i]);

	dev_info(edev->dev,
		"added %s: Gaudi3 simulator device [0000:00:%02d.0]\n",
		edev->name, args->minor);

	gaudi3_simulator_dev_table[edev->id - HLV_SIM_ID_OFFSET] = edev;

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
	for (i = 0 ; i < GAUDI3_MSIX_ENTRIES ; i++)
		mutex_destroy(&edev->irq_mutex[i]);
free_h2c_fifo:
	kfifo_free(&edev->h2c_fifo);
free_shared_mem_pool:
	gaudi3_simulator_dev_table[edev->id - HLV_SIM_ID_OFFSET] = NULL;
	gen_pool_destroy(edev->pool);
free_sram_dram:
	hl_sim_vunmap_user_pages(&edev->user_sram_dram);
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

void gaudi3_simulator_stop(u32 minor)
{
	struct simulator_shared_mem_block *shared_block;
	struct hl_simulator_device *edev;
	struct simulator_msg *msg;
	int count, i, handle;

	if (minor >= HL_MAX_MINORS) {
		pr_crit("habanalabs: minor is out of bounds %u, can't stop sim\n", minor);
		return;
	}

	edev = gaudi3_simulator_dev_table[minor];

	dev_dbg(edev->dev, "Removing Gaudi3 simulator device\n");

	/* Make sure work to create simulator has finished */
	cancel_delayed_work_sync(&edev->work_create);

	if (edev->hdev)
		gaudi3_sim_stop_device(edev->hdev);

	/* Disable open on device */
	gaudi3_simulator_dev_table[minor] = NULL;

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

	for (i = 0 ; i < GAUDI3_MSIX_ENTRIES ; i++)
		mutex_destroy(&edev->irq_mutex[i]);

	hl_sim_vunmap_user_pages(&edev->user_sram_dram);

	vfree(edev->shmem);

	put_device(edev->dev);
}

/**
 * gaudi3_sim_rreg - Read an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 *
 * Returns the value of the MMIO register we are asked to read
 *
 */
static u32 gaudi3_sim_rreg(struct hl_device *hdev, u32 reg)
{
	struct hl_simulator_device *edev = gaudi3_simulator_dev_table[hdev->id];
	u64 reg_addr = CFG_BAR_BASE + reg;

	return hl_sim_rreg(hdev, reg_addr, edev);
}

/**
 * gaudi3_sim_wreg - Write to an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 * @val: 32-bit value
 *
 * Writes the 32-bit value into the MMIO register
 *
 */
static void gaudi3_sim_wreg(struct hl_device *hdev, u32 reg, u32 val)
{
	struct hl_simulator_device *edev = gaudi3_simulator_dev_table[hdev->id];
	u64 reg_addr = CFG_BAR_BASE + reg;

	hl_sim_wreg(hdev, reg_addr, edev, val);
}

static int gaudi3_sim_validate_set_decoder_binning(struct hl_device *hdev)
{
	u32 decoder_full_mask, die_decoder_full_mask, die_decoder_binning_mask;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u8 i, num_die_decoders, num_binned;

	/* no binning but maybe not all engines are enabled */
	if (!hdev->decoder_binning) {
		prop->decoder_enabled_mask = hdev->decoder_mask;
		prop->decoder_binning_mask = hdev->decoder_binning;
		return 0;
	}

	decoder_full_mask = GENMASK((prop->num_of_hdcores * NUM_OF_DECODER_PER_HDCORE) - 1, 0);

	if (hdev->decoder_mask != decoder_full_mask) {
		hl_err(hdev,
			"Decoder binning is valid only with full decoder enabled mask\n");
		return -EINVAL;
	}

	/* in decoder binning we can have, at most, single binned decoder per DIE */
	num_die_decoders = NUM_OF_HDCORES_PER_DIE * NUM_OF_DECODER_PER_HDCORE;
	die_decoder_full_mask = GENMASK(num_die_decoders - 1, 0);
	prop->decoder_enabled_mask = 0;
	for (i = 0; i < prop->num_of_dies; i++) {
		u8 shift = i * num_die_decoders;

		die_decoder_binning_mask = (hdev->decoder_binning >> shift) & die_decoder_full_mask;
		if (!die_decoder_binning_mask) {
			prop->decoder_enabled_mask |= (die_decoder_full_mask << shift);
			continue;
		}

		num_binned = hweight32(die_decoder_binning_mask);
		if (num_binned > MAX_BINNED_DECODERS_PER_DIE) {
			hl_err(hdev, "too many binned decoders (%#x)\n",
							hdev->decoder_binning);
			return -EINVAL;
		}

		/* set last decoder in each die as not enabled */
		prop->decoder_enabled_mask |= (GENMASK(num_die_decoders - 2, 0) << shift);
	}

	prop->decoder_binning_mask = hdev->decoder_binning;

	return 0;
}

static int gaudi3_sim_validate_set_rotator_binning(struct hl_device *hdev)
{
	u32 rotator_full_mask, die_rotator_full_mask, die_rotator_binning_mask;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u8 i, num_binned;

	/* no binning but maybe not all engines are enabled */
	if (!hdev->rotator_binning) {
		prop->rotator_enabled_mask = hdev->rotator_mask;
		prop->rotator_binning_mask = hdev->rotator_binning;
		return 0;
	}

	rotator_full_mask = GENMASK((prop->num_of_dies * NUM_OF_ROTATOR_PER_DIE) - 1, 0);

	if (hdev->rotator_mask != rotator_full_mask) {
		hl_err(hdev,
			"Rotator binning is valid only with full rotator enabled mask\n");
		return -EINVAL;
	}

	/* in rotator binning we can have, at most, single binned rotator per DIE */
	die_rotator_full_mask = GENMASK(NUM_OF_ROTATOR_PER_DIE - 1, 0);
	prop->rotator_enabled_mask = 0;
	for (i = 0; i < prop->num_of_dies; i++) {
		u8 shift = i * NUM_OF_ROTATOR_PER_DIE;

		die_rotator_binning_mask = (hdev->rotator_binning >> shift) & die_rotator_full_mask;
		if (!die_rotator_binning_mask) {
			prop->rotator_enabled_mask |= (die_rotator_full_mask << shift);
			continue;
		}

		num_binned = hweight32(die_rotator_binning_mask);
		if (num_binned > MAX_BINNED_ROTATORS_PER_DIE) {
			hl_err(hdev, "too many binned rotators (%#x)\n",
							hdev->rotator_binning);
			return -EINVAL;
		}

		/* set last rotator in each die as not enabled */
		prop->rotator_enabled_mask |= (GENMASK(NUM_OF_ROTATOR_PER_DIE - 2, 0) << shift);
	}

	prop->rotator_binning_mask = hdev->rotator_binning;

	return 0;
}

static int gaudi3_sim_set_binning_masks(struct hl_device *hdev)
{
	int rc;

	rc = gaudi3_validate_set_tpc_binning(hdev);
	if (rc)
		return rc;

	rc = gaudi3_sim_validate_set_decoder_binning(hdev);
	if (rc)
		return rc;

	rc = gaudi3_sim_validate_set_rotator_binning(hdev);
	if (rc)
		return rc;

	gaudi3_set_dram_binning_masks(hdev);

	return 0;
}

/* All the code below this point is the gaudi3 simulator device implementation */

static u32 gaudi3_sim_get_pci_id(struct hl_device *hdev)
{
	switch (hdev->asic_type) {
	case ASIC_GAUDI3_SIM:
		return PCI_IDS_GAUDI3_SIMULATOR;
	case ASIC_GAUDI3D_SIM:
		return PCI_IDS_GAUDI3D_SIMULATOR;
	case ASIC_GAUDI3E_SIM:
		return PCI_IDS_GAUDI3E_SIMULATOR;
	case ASIC_GAUDI3_SIM_ARC:
		return PCI_IDS_GAUDI3_ARC_SIMULATOR;
	case ASIC_GAUDI3D_SIM_ARC:
		return PCI_IDS_GAUDI3D_ARC_SIMULATOR;
	case ASIC_GAUDI3E_SIM_ARC:
		return PCI_IDS_GAUDI3E_ARC_SIMULATOR;
	case ASIC_GAUDI3_HL_338_SIM:
		return PCI_IDS_GAUDI3_HL_338_SIMULATOR;
	case ASIC_GAUDI3D_HL_338_SIM:
		return PCI_IDS_GAUDI3D_HL_338_SIMULATOR;
	case ASIC_GAUDI3E_HL_338_SIM:
		return PCI_IDS_GAUDI3E_HL_338_SIMULATOR;
	case ASIC_GAUDI3_HL_338_SIM_ARC:
		return PCI_IDS_GAUDI3_HL_338_ARC_SIMULATOR;
	case ASIC_GAUDI3D_HL_338_SIM_ARC:
		return PCI_IDS_GAUDI3D_HL_338_ARC_SIMULATOR;
	case ASIC_GAUDI3E_HL_338_SIM_ARC:
		return PCI_IDS_GAUDI3E_HL_338_ARC_SIMULATOR;
	default:
		return PCI_IDS_GAUDI3_SIMULATOR;
	}
}

static int gaudi3_sim_set_fixed_properties(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = gaudi3_simulator_dev_table[hdev->id];
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	rc = gaudi3_set_fixed_properties(hdev);
	if (rc) {
		hl_err(hdev, "Failed setting fixed properties\n");
		return rc;
	}

	/* Note that while being loaded - Coral always needs SRAM memory access
	 * regardless of SRAM's configuration (either as a memory or a cache).
	 */
	if (!prop->sram_base_address || !prop->sram_size) {
		hl_err(hdev, "Simulator cannot run without SRAM\n");
		return -EINVAL;
	}

	if (edev->sram_size != prop->sram_size) {
		hl_err(hdev, "Simulator SRAM size is %#llx while expected value is %#x\n",
			edev->sram_size, prop->sram_size);
		return -EINVAL;
	}

	if (hdev->dram_enable) {
		prop->dram_size =
				DIV_ROUND_DOWN_ULL(edev->dram_size,
						prop->device_mem_alloc_default_page_size) *
						prop->device_mem_alloc_default_page_size;

		prop->dram_end_address = prop->dram_base_address + prop->dram_size;
	}

	prop->support_glbl_priv_fetch = true;
	prop->pci_id = gaudi3_sim_get_pci_id(hdev);

	return 0;
}

static int gaudi3_sim_pci_bars_map(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = gaudi3_simulator_dev_table[hdev->id];

	/* Simulate SRAM & HBM BAR */
	if (edev->sram_size)
		hdev->pcie_bar[SRAM_DRAM_BAR_ID] = (void __iomem *)edev->sram;
	else
		hdev->pcie_bar[SRAM_DRAM_BAR_ID] = (void __iomem *)edev->dram;

	hl_dbg(hdev, "SRAM/HBM at %p\n",
			hdev->pcie_bar[SRAM_DRAM_BAR_ID]);

	/* CFG is not simulated as BAR */
	hdev->rmmio = NULL;

	return 0;
}

static void gaudi3_sim_pci_bars_unmap(struct hl_device *hdev)
{
	hdev->pcie_bar[SRAM_DRAM_BAR_ID] = NULL;
	hdev->rmmio = NULL;
}

static int gaudi3_sim_early_init(struct hl_device *hdev)
{
	int rc;

	rc = gaudi3_sim_set_fixed_properties(hdev);
	if (rc)
		return rc;

	rc = gaudi3_sim_pci_bars_map(hdev);
	if (rc)
		goto free_queue_props;

	rc = hl_fw_read_preboot_status(hdev);
	if (rc)
		goto free_queue_props;

	return 0;

free_queue_props:
	kfree(hdev->asic_prop.hw_queues_props);
	return rc;
}

static int gaudi3_sim_early_fini(struct hl_device *hdev)
{
	kfree(hdev->asic_prop.hw_queues_props);
	gaudi3_sim_pci_bars_unmap(hdev);

	return 0;
}

static int gaudi3_sim_sw_init(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3;
	int rc;

	/* Allocate device structure */
	gaudi3 = kzalloc(sizeof(*gaudi3), GFP_KERNEL);
	if (!gaudi3)
		return -ENOMEM;

	mutex_init(&gaudi3->kdma_lock_mutex);

	INIT_WORK(&gaudi3->eq_work.work, gaudi3_eq_handler);
	gaudi3->eq_work.hdev = hdev;

	rc = gaudi3_alloc_cpu_accessible_dma_mem(hdev);
	if (rc)
		goto free_gaudi3_device;

	hdev->cpu_accessible_dma_pool = gen_pool_create(ilog2(32), -1);
	if (!hdev->cpu_accessible_dma_pool) {
		hl_err(hdev, "Failed to create CPU accessible DMA pool\n");
		rc = -ENOMEM;
		goto free_cpu_dma_mem;
	}

	rc = gen_pool_add(hdev->cpu_accessible_dma_pool, (uintptr_t) hdev->cpu_accessible_dma_mem,
				HL_CPU_ACCESSIBLE_MEM_SIZE, -1);
	if (rc) {
		hl_err(hdev, "Failed to add memory to CPU accessible DMA pool\n");
		rc = -EFAULT;
		goto free_cpu_accessible_dma_pool;
	}

	gaudi3->cpucp_info_get = gaudi3_cpucp_info_get;
	hdev->asic_prop.supports_compute_reset = true;
	hdev->asic_specific = gaudi3;

	gaudi3_user_interrupt_setup(hdev);
	hdev->asic_funcs->set_pci_memory_regions(hdev);

	rc = gaudi3_etr_buf_store_sw_init(hdev);
	if (rc) {
		hl_err(hdev, "Failed to init ETR buffer storing S/W\n");
		goto free_cpu_accessible_dma_pool;
	}

	hdev->supports_cb_mapping = true;

	rc = gaudi3_special_blocks_iterator_config(hdev);
	if (rc)
		goto etr_sw_fini;

	rc = gaudi3_page_fault_queue_sw_init(hdev);
	if (rc)
		goto special_blocks_fini;

	rc = gaudi3_test_qmans_msgs_alloc(hdev);
	if (rc)
		goto page_fault_queue_sw_fini;

	return 0;

page_fault_queue_sw_fini:
	gaudi3_page_fault_queue_sw_fini(hdev);
special_blocks_fini:
	gaudi3_special_blocks_iterator_free(hdev);
etr_sw_fini:
	gaudi3_etr_buf_store_sw_fini(hdev);
free_cpu_accessible_dma_pool:
	gen_pool_destroy(hdev->cpu_accessible_dma_pool);
free_cpu_dma_mem:
	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, hdev->cpu_accessible_dma_mem,
					hdev->cpu_accessible_dma_address);
free_gaudi3_device:
	mutex_destroy(&gaudi3->kdma_lock_mutex);
	kfree(gaudi3);

	return rc;
}

static int gaudi3_sim_sw_fini(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;

	gaudi3_test_qmans_msgs_free(hdev);

	gaudi3_page_fault_queue_sw_fini(hdev);

	gaudi3_special_blocks_iterator_free(hdev);

	gaudi3_etr_buf_store_sw_fini(hdev);

	gen_pool_destroy(hdev->cpu_accessible_dma_pool);

	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, hdev->cpu_accessible_dma_mem,
						hdev->cpu_accessible_dma_address);

	mutex_destroy(&gaudi3->kdma_lock_mutex);

	kfree(gaudi3);

	return 0;
}

static void gaudi3_sim_halt_engines_fw_config(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = gaudi3_simulator_dev_table[hdev->id];

	hl_sim_set_priv_assertions(edev, false);

	gaudi3_halt_engines_fw_config(hdev);

	hl_sim_set_priv_assertions(edev, true);
}

static void gaudi3_sim_halt_engines(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	gaudi3_sim_halt_engines_fw_config(hdev);

	/*
	 * Mark the NIC as in reset to avoid any new NIC accesses to the HW. This must be done
	 * before we stop the CPU as the NIC might use it e.g. get/set EEPROM data.
	 */
	if (hard_reset)
		hl_cn_hard_reset_prepare(hdev);

	gaudi3_stop_edma_qmans(hdev);
	gaudi3_stop_tpc_qmans(hdev);
	gaudi3_stop_mme_qmans(hdev);
	gaudi3_stop_rotator_qmans(hdev);

	gaudi3_halt_arcs(hdev);
	gaudi3_halt_pdma(hdev);
	gaudi3_halt_dup(hdev);
	gaudi3_stall_edma(hdev);
	gaudi3_stall_tpc(hdev);
	gaudi3_stall_mme(hdev);
	gaudi3_stall_rotator(hdev);
	gaudi3_stop_decoder(hdev);

	gaudi3_disable_edma_qmans(hdev);
	gaudi3_disable_tpc_qmans(hdev);
	gaudi3_disable_mme_qmans(hdev);
	gaudi3_disable_rotator_qmans(hdev);

	if (hard_reset) {
		hl_cn_stop(hdev);
		gaudi3_disable_msix(hdev);
		return;
	}

	gaudi3_cn_compute_reset_prepare(hdev);
	gaudi3_sync_irqs(hdev);
	hl_cn_synchronize_irqs(hdev);
}

static int gaudi3_sim_fw_config(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = gaudi3_simulator_dev_table[hdev->id];
	int rc;

	hl_sim_set_priv_assertions(edev, false);

	rc = gaudi3_pre_hw_init(hdev);
	if (rc)
		return rc;

	gaudi3_hw_init_fw_config(hdev);

	hl_sim_set_priv_assertions(edev, true);

	return 0;
}

static void gaudi3_sim_set_isolation(struct hl_device *hdev, bool isolate_engines,
					bool isolate_nic_and_hbm)
{
	struct hl_simulator_device *edev = gaudi3_simulator_dev_table[hdev->id];

	hl_sim_set_priv_assertions(edev, false);
	gaudi3_set_isolation(hdev, isolate_engines, isolate_nic_and_hbm);
	hl_sim_set_priv_assertions(edev, true);
}

static int gaudi3_sim_init_security_privileged(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = gaudi3_simulator_dev_table[hdev->id];
	int rc;

	hl_sim_set_priv_assertions(edev, false);
	rc = gaudi3_init_security_privileged(hdev);
	hl_sim_set_priv_assertions(edev, true);

	return rc;
}

static int gaudi3_sim_hw_init(struct hl_device *hdev)
{
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	struct hl_simulator_device *edev = gaudi3_simulator_dev_table[hdev->id];
	int rc;

	/* Some blocks are isolated by default, so de-isolation must happen early enough,
	 * before privileged security is enabled on simulator.
	 */
	gaudi3_sim_set_isolation(hdev, false, false);

	rc = gaudi3_sim_init_security_privileged(hdev);
	if (rc)
		return rc;

	gaudi3_lbw_dup_init(hdev);

	/* Set all privileged registers instead of FW */
	rc = gaudi3_sim_fw_config(hdev);
	if (rc)
		return rc;

	rc = gaudi3_init_cpu(hdev);
	if (rc) {
		hl_err(hdev, "failed to initialize CPU\n");
		return rc;
	}

	if (hdev->cache_enable) {
		rc = gaudi3_set_cache_mode(hdev);
		if (rc) {
			hl_err(hdev, "failed setting cache mode\n");
			return rc;
		}
	}

	gaudi3_init_scrambler(hdev);

	gaudi3_init_msix_gw_table(hdev);

	rc = gaudi3_init_cpu_queues(hdev, GAUDI3_CPU_TIMEOUT_USEC);
	if (rc) {
		hl_err(hdev, "failed to initialize CPU H/W queues %d\n", rc);
		return rc;
	}

	rc = gaudi3->cpucp_info_get(hdev);
	if (rc) {
		hl_err(hdev, "Failed to get cpucp info\n");
		return rc;
	}

	rc = gaudi3_mmu_init(hdev);
	if (rc) {
		hl_err(hdev, "failed to initialize MMU\n");
		return rc;
	}

	gaudi3_init_cbc(hdev);
	gaudi3_init_pdma(hdev);
	gaudi3_init_sm(hdev);

	/* Invalidate MMU cache, must be run after SM init */
	rc = hl_mmu_invalidate_cache(hdev, 0, 0);
	if (rc)
		return rc;

	gaudi3_init_edma(hdev);
	gaudi3_init_tpc(hdev);
	gaudi3_init_mme(hdev);
	gaudi3_init_rotator(hdev);
	gaudi3_init_decoder(hdev);

	rc = gaudi3_init_security(hdev);
	if (rc)
		return rc;

	rc = gaudi3_page_fault_queue_hw_init(hdev);
	if (rc)
		return rc;

	rc = gaudi3_coresight_init(hdev);
	if (rc)
		return rc;

	rc = gaudi3_enable_msix(hdev);
	if (rc)
		return rc;

	edev->reset = false;

	return 0;
}

static void gaudi3_sim_trigger_reset(struct hl_device *hdev, bool hard_reset, u32 die)
{
	u32 reset_timeout_ms = SIM_RESET_WAIT_MSEC;

	WREG32(mmD0_PSOC_RESET_CONF_BASE + die * DIE_OFFSET +
			(hard_reset ? mmPSOC_RESET_CONF_SW_ALL_RST : mmPSOC_RESET_CONF_SOFT_RST),
			0x1);

	hl_dbg(hdev,
		"Issued %s reset command to DIE%d, waiting up to %dms\n",
		hard_reset ? "HARD" : "SOFT",
		die, reset_timeout_ms);
}

static void gaudi3_sim_poll_on_reset_complete(struct hl_device *hdev, u32 die)
{
	u32 reset_timeout_us, reg_val;
	int rc;

	reset_timeout_us = SIM_RESET_WAIT_MSEC * 1000;

	/* Wait a certain amount of time before checking if the reset has
	 * finished
	 */
	rc = hl_poll_timeout(hdev,
			die * DIE_OFFSET + mmD0_PSOC_GLOBAL_CONF_BASE + mmGLOBAL_CONF_BTM_FSM,
			reg_val, reg_val == 0, 10000, reset_timeout_us);

	if (rc == -ETIMEDOUT)
		hl_err(hdev,
			"Timeout while waiting for device to reset 0x%x\n",
			reg_val);
}

static int gaudi3_sim_hw_fini(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	struct hl_simulator_device *edev = gaudi3_simulator_dev_table[hdev->id];
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int die, rc;

	hl_sim_set_priv_assertions(edev, false);
	gaudi3_reset_arcs(hdev);
	hl_sim_set_priv_assertions(edev, true);

	gaudi3_sim_set_isolation(hdev, true, hard_reset);

	if (hdev->simulator_crashed)
		goto clear_hw_cap;

	if (hard_reset || !(hdev->fw_components & FW_TYPE_BOOT_CPU)) {
		/* Order of reset is DIE1 followed by DIE0 */
		for (die = prop->num_of_dies - 1; die >= 0 ; die--) {
			gaudi3_sim_trigger_reset(hdev, hard_reset, die);
			gaudi3_sim_poll_on_reset_complete(hdev, die);
		}

		/* Reset bit is not self-clearing, need to manually clear it */
		for (die = 0; die < prop->num_of_dies; die++)
			WREG32(mmD0_PSOC_RESET_CONF_BASE + die * DIE_OFFSET +
				(hard_reset ? mmPSOC_RESET_CONF_SW_ALL_RST :
					mmPSOC_RESET_CONF_SOFT_RST), 0x0);
	} else {
		rc = hl_fw_send_soft_reset(hdev);
		if (rc)
			return rc;
	}

clear_hw_cap:
	gaudi3_clear_hw_cap(hdev, hard_reset);

	return 0;
}

static int gaudi3_sim_mmap(struct hl_device *hdev,
		struct vm_area_struct *vma, void *cpu_addr, dma_addr_t dma_addr,
		size_t size)
{
	int rc;

	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY | VM_NORESERVE);

	rc = remap_vmalloc_range(vma, cpu_addr, 0);
	if (rc)
		hl_err(hdev, "remap vmalloc error %d", rc);

	return rc;
}

static void *gaudi3_sim_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	struct hl_simulator_device *edev =
			gaudi3_simulator_dev_table[hdev->id];

	void *address = (void *) gen_pool_alloc(edev->pool, size);

	if (address) {
		if (flags & __GFP_ZERO)
			memset(address, 0, size);

		*dma_handle = virt_to_phys(address);
		if (!gaudi3_host_phys_addr_valid(*dma_handle))
			hl_crit(hdev, "invalid host phys addr 0x%llx\n",
					*dma_handle);
	}

	return address;
}

static void gaudi3_sim_dma_free_coherent(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle)
{
	struct hl_simulator_device *edev = gaudi3_simulator_dev_table[hdev->id];

	gen_pool_free(edev->pool, (u64) cpu_addr, size);
}

static void *gaudi3_sim_dma_pool_zalloc(struct hl_device *hdev, size_t size,
					gfp_t mem_flags, dma_addr_t *dma_handle)
{
	if (size > SIM_DMA_POOL_BLK_SIZE)
		return NULL;

	return gaudi3_sim_dma_alloc_coherent(hdev, PAGE_SIZE, dma_handle,
					mem_flags | __GFP_ZERO);
}

static void gaudi3_sim_dma_pool_free(struct hl_device *hdev, void *vaddr,
				dma_addr_t dma_addr)
{
	gaudi3_sim_dma_free_coherent(hdev, PAGE_SIZE, vaddr, dma_addr);
}

static dma_addr_t gaudi3_sim_dma_map_page(struct hl_device *hdev, struct page *page,
			int offset, int len, enum dma_data_direction dir)
{
	dma_addr_t pa;

	pa = page_to_phys(page) + offset;
	return pa;
}

static void gaudi3_sim_dma_unmap_page(struct hl_device *hdev, dma_addr_t addr,
			int len, enum dma_data_direction dir)
{
}

static u64 gaudi3_sim_read_pte(struct hl_device *hdev, u64 addr)
{
	u64 val;

	if (hdev->reset_info.hard_reset_pending)
		return U64_MAX;

	hl_sim_read_dram(gaudi3_simulator_dev_table[hdev->id],
			 &val, (addr - DRAM_PHYS_BASE), sizeof(val));

	return val;
}

static void gaudi3_sim_write_pte(struct hl_device *hdev, u64 addr, u64 val)
{
	if (hdev->reset_info.hard_reset_pending)
		return;

	hl_sim_write_dram(gaudi3_simulator_dev_table[hdev->id],
			  (addr - DRAM_PHYS_BASE), &val, sizeof(val));
}

static int gaudi3_sim_suspend(struct hl_device *hdev)
{
	return 0;
}

static int gaudi3_sim_resume(struct hl_device *hdev)
{
	return 0;
}

static int gaudi3_sim_get_eeprom_data(struct hl_device *hdev, void *data,
		size_t max_size)
{
	const char *str = "no EEPROM data on simulator";

	memcpy(data, str, strlen(str) + 1);

	return 0;
}

static int gaudi3_sim_block_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
		u32 block_id, u32 block_size)
{
	return -EPERM;
}

static int gaudi3_sim_get_hw_block_id(struct hl_device *hdev, u64 block_addr,
					u32 *block_size, u32 *block_id)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gaudi3_device *gaudi3 = hdev->asic_specific;
	int i;

	for (i = 0 ; i < NUM_USER_MAPPED_BLOCKS ; i++) {
		u64 mapped_block_addr = prop->cfg_base_address +
				gaudi3->mapped_blocks[i].address;
		u64 mapped_block_addr_last = mapped_block_addr +
				gaudi3->mapped_blocks[i].size;
		if ((block_addr >= mapped_block_addr) &&
				(block_addr < mapped_block_addr_last)) {
			*block_id = i;
			if (block_size)
				*block_size = gaudi3->mapped_blocks[i].size;
			return 0;
		}
	}

	hl_err(hdev, "Invalid block address %#llx", block_addr);

	return -EINVAL;
}

static void gaudi3_sim_add_device_attr(struct hl_device *hdev,
					struct attribute_group *dev_clk_attr_grp,
					struct attribute_group *dev_vrm_attr_grp)
{
	dev_clk_attr_grp->attrs = gaudi3_sim_dev_attrs;
	dev_vrm_attr_grp->attrs = gaudi3_sim_dev_attrs;
}

static int gaudi3_sim_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard, u32 flags)
{
	if (hdev->simulator_crashed)
		return 0;

	return gaudi3_mmu_invalidate_cache(hdev, is_hard, flags);
}

static int gaudi3_sim_mmu_invalidate_cache_range(struct hl_device *hdev,
		bool is_hard, u32 flags, u32 asid, u64 va, u64 size)
{
	if (hdev->simulator_crashed)
		return 0;

	/* Treat as invalidate all */
	return gaudi3_mmu_invalidate_cache(hdev, is_hard, flags);
}

static int gaudi3_sim_alloc_irq_vectors(struct hl_device *hdev, unsigned int min_vecs,
			unsigned int max_vecs, unsigned int flags)
{
	return hl_sim_alloc_irq_vectors(gaudi3_simulator_dev_table[hdev->id], min_vecs, max_vecs,
					flags);
}

static void gaudi3_sim_free_irq_vectors(struct hl_device *hdev)
{
	hl_sim_free_irq_vectors(gaudi3_simulator_dev_table[hdev->id]);
}

static int gaudi3_sim_irq_vector(struct hl_device *hdev, unsigned int nr)
{
	return hl_sim_irq_vector(gaudi3_simulator_dev_table[hdev->id], nr);
}

static int gaudi3_sim_pll_info_get(struct hl_device *hdev, u32 pll_index,
		u16 *pll_freq_arr)
{
	/*
	 * in simulation PLLs are not supported.
	 */
	memset(pll_freq_arr, 0x0, sizeof(u16) * HL_PLL_NUM_OUTPUTS);

	return 0;
}

static void gaudi3_sim_set_priv_assertions(struct hl_device *hdev, bool enable)
{
	struct hl_simulator_device *edev = gaudi3_simulator_dev_table[hdev->id];

	hl_sim_set_priv_assertions(edev, enable);
}

static bool gaudi3_sim_is_device_idle(struct hl_device *hdev, u64 *mask_arr, u8 mask_len,
					struct engines_data *e)
{
	if (hdev->simulator_crashed)
		return true;

	return gaudi3_is_device_idle(hdev, mask_arr, mask_len, e);
}

static int gaudi3_sim_get_reg_pcie_addr(struct hl_device *hdev, u32 reg, u64 *pci_addr)
{
	return -EINVAL;
}

static const struct hl_asic_funcs gaudi3_sim_funcs = {
	.early_init = gaudi3_sim_early_init,
	.early_fini = gaudi3_sim_early_fini,
	.late_init = gaudi3_late_init,
	.late_fini = gaudi3_late_fini,
	.sw_init = gaudi3_sim_sw_init,
	.sw_fini = gaudi3_sim_sw_fini,
	.hw_init = gaudi3_sim_hw_init,
	.hw_fini = gaudi3_sim_hw_fini,
	.halt_engines = gaudi3_sim_halt_engines,
	.suspend = gaudi3_sim_suspend,
	.resume = gaudi3_sim_resume,
	.mmap = gaudi3_sim_mmap,
	.ring_doorbell = gaudi3_ring_doorbell,
	.pqe_write = gaudi3_pqe_write,
	.asic_dma_alloc_coherent = gaudi3_sim_dma_alloc_coherent,
	.asic_dma_free_coherent = gaudi3_sim_dma_free_coherent,
	.scrub_device_mem = gaudi3_scrub_device_mem,
	.scrub_device_dram = gaudi3_scrub_device_dram,
	.get_int_queue_base = NULL,
	.test_queues = gaudi3_test_queues,
	.asic_dma_pool_zalloc = gaudi3_sim_dma_pool_zalloc,
	.asic_dma_pool_free = gaudi3_sim_dma_pool_free,
	.cpu_accessible_dma_pool_alloc = gaudi3_cpu_accessible_dma_pool_alloc,
	.cpu_accessible_dma_pool_free = gaudi3_cpu_accessible_dma_pool_free,
	.asic_dma_unmap_page = gaudi3_sim_dma_unmap_page,
	.asic_dma_map_page = gaudi3_sim_dma_map_page,
	.dma_unmap_sgtable = hl_sim_dma_unmap_sgtable,
	.cs_parser = gaudi3_cs_parser,
	.dma_map_sgtable = hl_sim_dma_map_sgtable,
	.update_eq_ci = gaudi3_update_eq_ci,
	.context_switch = gaudi3_context_switch,
	.restore_phase_topology = gaudi3_restore_phase_topology,
	.debugfs_read_dma = gaudi3_debugfs_read_dma,
	.add_device_attr = gaudi3_sim_add_device_attr,
	.handle_eqe = NULL,
	.get_events_stat = gaudi3_get_events_stat,
	.read_pte = gaudi3_sim_read_pte,
	.write_pte = gaudi3_sim_write_pte,
	.mmu_invalidate_cache = gaudi3_sim_mmu_invalidate_cache,
	.mmu_invalidate_cache_range = gaudi3_sim_mmu_invalidate_cache_range,
	.mmu_prefetch_cache_range = gaudi3_mmu_prefetch_cache_range,
	.send_heartbeat = gaudi3_send_heartbeat,
	.debug_coresight = gaudi3_debug_coresight,
	.is_device_idle = gaudi3_sim_is_device_idle,
	.compute_reset_late_init = gaudi3_compute_reset_late_init,
	.hw_queues_lock = gaudi3_hw_queues_lock,
	.hw_queues_unlock = gaudi3_hw_queues_unlock,
	.get_eeprom_data = gaudi3_sim_get_eeprom_data,
	.get_monitor_dump = gaudi3_get_monitor_dump,
	.send_cpu_message = gaudi3_send_cpu_message,
	.cn_init = hl_cn_init,
	.cn_fini = hl_cn_fini,
	.cn_control = hl_cn_control,
	.pci_bars_map = NULL,
	.init_iatu = NULL,
	.rreg = gaudi3_sim_rreg,
	.wreg = gaudi3_sim_wreg,
	.get_reg_pcie_addr = gaudi3_sim_get_reg_pcie_addr,
	.halt_coresight = gaudi3_halt_coresight,
	.ctx_init = gaudi3_ctx_init,
	.ctx_fini = gaudi3_ctx_fini,
	.get_queue_id_for_cq = gaudi3_get_queue_id_for_cq,
	.load_firmware_to_device = NULL,
	.load_boot_fit_to_device = NULL,
	.get_device_time = gaudi3_get_device_time,
	.pb_print_security_errors = NULL,
	.get_dec_base_addr = gaudi3_get_dec_base_addr,
	.scramble_addr = hl_mmu_scramble_addr,
	.descramble_addr = hl_mmu_descramble_addr,
	.ack_protection_bits_errors = gaudi3_ack_protection_bits_errors,
	.get_hw_block_id = gaudi3_sim_get_hw_block_id,
	.hw_block_mmap = gaudi3_sim_block_mmap,
	.enable_events_from_fw = gaudi3_enable_events_from_fw,
	.ack_mmu_errors = gaudi3_ack_mmu_page_fault_or_access_error,
	.map_pll_idx_to_fw_idx = gaudi3_map_pll_idx_to_fw_idx,
	.init_cpu_scrambler_dram = gaudi3_init_scrambler,
	.state_dump_init = gaudi3_state_dump_init,
	.get_sob_addr = &gaudi3_get_sob_addr,
	.set_pci_memory_regions = gaudi3_set_pci_memory_regions,
	.get_stream_master_qid_arr = gaudi3_get_stream_master_qid_arr,
	.cn_funcs = &gaudi3_cn_funcs,
	.alloc_irq_vectors = gaudi3_sim_alloc_irq_vectors,
	.free_irq_vectors = gaudi3_sim_free_irq_vectors,
	.irq_vector = gaudi3_sim_irq_vector,
	.scheduler_submit_buf = gaudi3_scheduler_submit_buf,
	.mmu_get_real_page_size = gaudi3_mmu_get_real_page_size,
	.access_dev_mem = gaudi3_sim_access_dev_mem,
	.set_dram_bar_base = NULL,
	.init_firmware_preload_params = gaudi3_init_firmware_preload_params,
	.init_firmware_loader = gaudi3_init_firmware_loader,
	.set_engine_cores = gaudi3_set_engine_cores,
	.set_engines = gaudi3_set_engines,
	.send_device_activity = gaudi3_send_device_activity,
	.read_fetch_memory_block = NULL,
	.fw_security_emulation_init = gaudi3_fw_security_emulation_init,
	.fw_security_emulation_fini = gaudi3_fw_security_emulation_fini,
	.pll_info_get = gaudi3_sim_pll_info_get,
	.set_dram_properties = gaudi3_set_dram_properties,
	.set_priv_assertions = gaudi3_sim_set_priv_assertions,
	.set_binning_masks = gaudi3_sim_set_binning_masks,
	.get_msi_info = gaudi3_get_msi_info,
	.is_irq_enabled = gaudi3_is_irq_enabled,
};

/**
 * gaudi3_sim_set_asic_funcs - set GAUDI3 Simulator function pointers
 *
 * @hdev: pointer to hl_device structure
 *
 */
void gaudi3_sim_set_asic_funcs(struct hl_device *hdev)
{
	hdev->asic_funcs = &gaudi3_sim_funcs;
}
