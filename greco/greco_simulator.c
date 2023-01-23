// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2019-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#define pr_fmt(fmt)			"habanalabs: " fmt

#include "grecoP.h"
#include "greco_masks.h"
#include "../common/simulator.h"
#include "../include/common/simulator.h"
#include "../include/hw_ip/mmu/mmu_general.h"
#include "../include/greco/greco_fw_if.h"
#include "../include/common/pci_ids.h"
#include "../include/greco/greco_reg_map.h"

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/hwmon.h>

#define GRECO_SHMEM_SIZE		0x80000000ull		/* 2 GB */

static DEFINE_MUTEX(simulator_open);

static struct hl_simulator_device *greco_simulator_dev_table[HL_MAX_MINORS];

static struct attribute *greco_sim_dev_attrs[] = {
	NULL,
};

static int greco_sim_access_dev_mem(struct hl_device *hdev, enum pci_region reg_type,
				u64 addr, u64 *val, enum debugfs_access_type acc_type)
{
	return hl_sim_access_dev_mem(hdev, greco_simulator_dev_table[hdev->id],
			reg_type, addr, val, acc_type);
}

static int greco_sim_start(struct hl_simulator_device *edev, int major,
				int minor, struct hl_device **hdev);

static void greco_simulator_create_device(struct work_struct *work)
{
	int rc;
	struct hl_simulator_device *edev =
		container_of(work, struct hl_simulator_device,
				work_create.work);

	dev_dbg(edev->dev,
		"Starting delayed work to create simulated device\n");

	rc = greco_sim_start(edev, edev->major, edev->id - HLV_SIM_ID_OFFSET,
				&edev->hdev);
	if (rc) {
		dev_err(edev->dev, "Failed to create GRECO Simulator device\n");
		/* Set hdev to NULL to prevent call of greco_sim_stop() */
		edev->hdev = NULL;
	}
}

static int greco_simulator_open(struct inode *inode, struct file *filp)
{
	u32 minor = iminor(inode) - HLV_SIM_ID_OFFSET;
	struct hl_simulator_device *edev;
	int rc = 0;

	if (minor >= HL_MAX_MINORS) {
		pr_crit("habanalabs: minor is out of bounds %u, can't open sim\n", minor);
		return -EINVAL;
	}

	mutex_lock(&simulator_open);

	edev = greco_simulator_dev_table[minor];

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
		"Opening file descriptor on greco simulator device\n");

	filp->private_data = edev;
	nonseekable_open(inode, filp);

	mutex_unlock(&simulator_open);

	if ((edev->sram_user_address) && (edev->dram_user_address)) {
		dev_dbg(edev->dev,
			"Creating delayed work to start simulated device\n");
		INIT_DELAYED_WORK(&edev->work_create,
					greco_simulator_create_device);
		schedule_delayed_work(&edev->work_create,
					usecs_to_jiffies(1000));
	}

	return 0;

unlock_mutex:
	mutex_unlock(&simulator_open);
	return rc;
}

static int greco_simulator_release(struct inode *inode, struct file *filp)
{
	struct hl_simulator_device *edev = filp->private_data;
	u32 minor = iminor(inode) - HLV_SIM_ID_OFFSET;

	edev->open = 0;

	if (edev->hdev) {
		dev_warn(edev->dev,
			"Simulator was closed, shouldn't use the hl%d device!\n",
			edev->hdev->id);
		edev->hdev->disabled = true;
		edev->hdev->simulator_crashed = true;
	}

	greco_simulator_stop(minor);
	hl_sim_remove(minor);

	return 0;
}

static ssize_t greco_simulator_read(struct file *filp, char __user *buffer,
		size_t len, loff_t *off)
{
	struct hl_simulator_device *edev = filp->private_data;

	dev_dbg_once(edev->dev, "Reading %zu bytes from %s in offset 0x%llx\n",
			len, filp->f_path.dentry->d_name.name, *off);

	return hl_sim_read_h2c_fifo(edev, filp, buffer, len);
}

static __poll_t greco_simulator_poll(struct file *filp,
				struct poll_table_struct *wait)
{
	return hl_sim_poll(filp->private_data, filp, wait);
}

static ssize_t greco_simulator_write(struct file *filp, const char __user *buff,
					size_t len, loff_t *off)
{
	struct hl_simulator_device *edev = filp->private_data;

	dev_dbg_once(edev->dev, "Writing %zu bytes to %s in offset 0x%llx\n",
			len, filp->f_path.dentry->d_name.name, *off);

	return hl_sim_write_c2h_fifo(edev, filp, buff, len);
}

static int greco_simulator_mmap(struct file *filp, struct vm_area_struct *vma)
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

	if (size == SRAM_SIZE || size == SRAM_SIZE / 2) {
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
		dev_dbg(edev->dev, "mapping DRAM:\n");
	} else {
		dev_err(edev->dev, "mmap size illegal 0x%lx\n", size);
		return -EINVAL;
	}

	*address_to_update = vma->vm_start;

	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY |
			VM_NORESERVE;

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
					greco_simulator_create_device);
		schedule_delayed_work(&edev->work_create,
					usecs_to_jiffies(1000));
	}

	return rc;
}

static int greco_simulator_gen_int_ioctl(struct hl_simulator_device *edev, void *data)
{
	struct simulator_gen_int_args *args = data;
	struct hl_device *hdev = edev->hdev;
	struct greco_device *greco;
	struct hl_dec *dec;
	u32 relative_idx;

	greco = hdev->asic_specific;

	if (args->id >= MSIX_ENTRIES) {
		dev_err(edev->dev, "interrupt id %d invalid", args->id);
		return -EINVAL;
	}

	mutex_lock(&edev->irq_mutex[args->id]);

	if (unlikely(edev->reset))
		goto out;

	if (args->id == GRECO_IRQ_NUM_EVENT_QUEUE) {
		hl_irq_handler_eq(args->id, &hdev->event_queue);

	} else if (args->id >= GRECO_IRQ_NUM_DCORE0_DEC0_NRM &&
			args->id <= GRECO_IRQ_NUM_DCORE1_DEC4_ABNRM) {

		relative_idx = args->id - GRECO_IRQ_NUM_DCORE0_DEC0_NRM;

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

			hl_irq_handler_user_interrupt(args->id,
							&hdev->user_interrupt[dec->core_id]);
		}

	} else if (args->id >= GRECO_IRQ_NUM_CS_FIRST && args->id <= GRECO_IRQ_NUM_CS_LAST) {

		relative_idx = args->id - GRECO_IRQ_NUM_CS_FIRST;
		hl_irq_handler_cs_cmplt(args->id, &greco->cs_irq_info_arr[relative_idx]);

	} else {
		dev_err(edev->dev, "unexpected interrupt id %d", args->id);
	}
out:
	mutex_unlock(&edev->irq_mutex[args->id]);

	return 0;
}

static int greco_simulator_pci_access_ioctl(struct hl_simulator_device *edev, void *data)
{
	struct simulator_pci_access_args *args = data;
	void *src_addr, *dst_addr;
	u64 sram_end_address = edev->sram_user_address + SRAM_SIZE;
	u64 dram_end_address = edev->dram_user_address + edev->dram_size;
	bool is_shmem = false;
	int rc = 0;

	dev_dbg_once(edev->dev,
			"GRECO simulator PCI access IOCTL details:\n");
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

static int greco_simulator_reset_device_ioctl(struct hl_simulator_device *edev, void *data)
{
	dev_err(edev->dev, "Greco simulator hard-reset request isn't supported\n");

	return -EIO;
}

/*
 * Ioctl function type.
 *
 * \param edev pointer to greco simulator device.
 * \param data pointer to arg that was copied from user.
 */
typedef int greco_simulator_ioctl_t(struct hl_simulator_device *edev,
					void *data);

struct greco_simulator_ioctl_desc {
	unsigned int cmd;
	greco_simulator_ioctl_t *func;
};

#define GRECO_SIMULATOR_IOCTL_DEF(ioctl, _func) \
	[_IOC_NR(ioctl)] = {.cmd = ioctl, .func = _func}

/** Ioctl table */
static const struct greco_simulator_ioctl_desc greco_simulator_ioctls[] = {
	GRECO_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_GEN_INT,
			greco_simulator_gen_int_ioctl),
	GRECO_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_PCI_ACCESS_FROM_DEVICE,
			greco_simulator_pci_access_ioctl),
	GRECO_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_RESET_DEVICE,
			greco_simulator_reset_device_ioctl),
};

#define GRECO_SIMULATOR_IOCTL_COUNT	ARRAY_SIZE(greco_simulator_ioctls)

static long greco_simulator_ioctl(struct file *filep, unsigned int cmd,
					unsigned long arg)
{
	struct hl_simulator_device *edev = filep->private_data;
	greco_simulator_ioctl_t *func;
	const struct greco_simulator_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);
	char stack_kdata[128];
	char *kdata = NULL;
	unsigned int usize, asize;
	int retcode = -EINVAL;

	if (edev->reset) {
		dev_err(edev->dev, "chip has been reset but got IOCTL\n");
		return -ENXIO;
	}

	if (nr >= GRECO_SIMULATOR_IOCTL_COUNT)
		goto err_i1;

	if ((nr >= SIMULATOR_COMMAND_START) &&
		(nr < SIMULATOR_COMMAND_END)) {
		u32 hl_size;

		ioctl = &greco_simulator_ioctls[nr];

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

static const struct file_operations greco_simulator_ops = {
	.owner = THIS_MODULE,
	.open = greco_simulator_open,
	.release = greco_simulator_release,
	.read = greco_simulator_read,
	.poll = greco_simulator_poll,
	.write = greco_simulator_write,
	.mmap = greco_simulator_mmap,
	.unlocked_ioctl = greco_simulator_ioctl,
	.compat_ioctl = greco_simulator_ioctl,
};

static ssize_t device_name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	const char *name = "greco_simulator";

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

static int greco_sim_start(struct hl_simulator_device *edev, int major,
			int minor, struct hl_device **hdev)
{
	int rc;

	rc = create_hdev(hdev, NULL, edev->virt_dev_type, minor);
	if (rc) {
		dev_err(edev->dev,
			"Failed to create real device for GRECO simulator\n");
		return rc;
	}

	(*hdev)->sdev = &edev->sdev;

	rc = hl_device_init(*hdev);
	if (rc) {
		dev_err(edev->dev, "fatal error during GRECO simulator init\n");
		rc = -ENODEV;
		goto free_hdev;
	}

	return 0;

free_hdev:
	kfree(*hdev);

	return rc;
}

static void greco_sim_stop(struct hl_device *hdev)
{
	hl_device_fini(hdev);
	kfree(hdev);
}

int greco_simulator_start(struct simulator_start_args *args)
{
	struct hl_simulator_device *edev;
	bool can_put_dev = false;
	int i, rc;

	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return -ENOMEM;

	edev->irq_mutex = kcalloc(MSIX_ENTRIES,
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

	sprintf(edev->name, "hlv%d", args->minor / 2 + HLV_SIM_ID_OFFSET);

	sim_devices_init(edev, edev->hclass, edev->id, &greco_simulator_ops,
					edev->name);

	rc = cdev_device_add(&edev->cdev, edev->dev);
	if (rc) {
		dev_err(edev->dev, "Failed to add char device\n");
		goto free_edev;
	}

	/* Allocate shared region between KMD/User and greco simulator */
	edev->shmem = vmalloc_user(GRECO_SHMEM_SIZE);
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
					    DRAM_DRIVER_END_ADDR -
						    DRAM_PHYS_BASE,
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

	rc = gen_pool_add(edev->pool, (u64) edev->shmem, GRECO_SHMEM_SIZE, -1);
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

	for (i = 0 ; i < MSIX_ENTRIES ; i++)
		mutex_init(&edev->irq_mutex[i]);

	dev_info(edev->dev,
		"added %s: GRECO simulator device [0000:00:%02d.0]\n",
		edev->name, args->minor);

	greco_simulator_dev_table[edev->id - HLV_SIM_ID_OFFSET] = edev;

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
	for (i = 0 ; i < MSIX_ENTRIES ; i++)
		mutex_destroy(&edev->irq_mutex[i]);
	kfifo_free(&edev->c2h_fifo);
free_h2c_fifo:
	kfifo_free(&edev->h2c_fifo);
free_shared_mem_pool:
	greco_simulator_dev_table[edev->id - HLV_SIM_ID_OFFSET] = NULL;
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

void greco_simulator_stop(u32 minor)
{
	struct hl_simulator_device *edev;
	struct simulator_msg *msg;
	int count, i;

	if (minor >= HL_MAX_MINORS) {
		pr_crit("habanalabs: minor is out of bounds %u, can't stop sim\n", minor);
		return;
	}

	edev = greco_simulator_dev_table[minor];

	dev_dbg(edev->dev, "Removing GRECO simulator device\n");

	/* Make sure work to create simulator has finished */
	cancel_delayed_work_sync(&edev->work_create);

	if (edev->hdev)
		greco_sim_stop(edev->hdev);

	/* Disable open on device */
	greco_simulator_dev_table[minor] = NULL;

	device_remove_file(edev->dev, &dev_attr_device_name);

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

	for (i = 0 ; i < MSIX_ENTRIES ; i++)
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
 * greco_sim_rreg - Read an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 *
 * Returns the value of the MMIO register we are asked to read
 *
 */
static u32 greco_sim_rreg(struct hl_device *hdev, u32 reg)
{
	struct hl_simulator_device *edev = greco_simulator_dev_table[hdev->id];
	u64 reg_addr = CFG_BASE + reg;

	return hl_sim_rreg(hdev, reg_addr, edev);
}

/**
 * greco_sim_wreg - Write to an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 * @val: 32-bit value
 *
 * Writes the 32-bit value into the MMIO register
 *
 */
static void greco_sim_wreg(struct hl_device *hdev, u32 reg, u32 val)
{
	struct hl_simulator_device *edev = greco_simulator_dev_table[hdev->id];
	u64 reg_addr = CFG_BASE + reg;
	hl_sim_wreg(hdev, reg_addr, edev, val);
}

static void greco_sim_notify_reset(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = greco_simulator_dev_table[hdev->id];

	hl_sim_notify_reset(hdev, edev);
}

/* All the code below this point is the greco simulator device implementation */

static int greco_sim_set_fixed_properties(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_simulator_device *edev = greco_simulator_dev_table[hdev->id];
	int rc;

	rc = greco_set_fixed_properties(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to get fixed properties\n");
		return rc;
	}

	if (hdev->dram_enable) {
		prop->dram_size = edev->dram_size;
		prop->dram_end_address = prop->dram_base_address +
						prop->dram_size;
	}

	prop->cb_pool_cb_cnt = SIM_CB_POOL_CB_CNT;
	prop->cb_pool_cb_size = SIM_CB_POOL_CB_SIZE;

	return 0;
}

static int greco_sim_pci_bars_map(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = greco_simulator_dev_table[hdev->id];

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

static void greco_sim_pci_bars_unmap(struct hl_device *hdev)
{
	hdev->pcie_bar[DRAM_BAR_ID] = NULL;
	hdev->pcie_bar[SRAM_CFG_BAR_ID] = NULL;
	hdev->rmmio = NULL;
}

static int greco_sim_early_init(struct hl_device *hdev)
{
	int rc;

	rc = greco_sim_set_fixed_properties(hdev);
	if (rc)
		return rc;

	rc = greco_sim_pci_bars_map(hdev);
	if (rc)
		goto free_queue_props;

	return 0;

free_queue_props:
	kfree(hdev->asic_prop.hw_queues_props);
	return rc;
}

static int greco_sim_early_fini(struct hl_device *hdev)
{
	kfree(hdev->asic_prop.hw_queues_props);
	greco_sim_pci_bars_unmap(hdev);

	return 0;
}

static int greco_sim_late_init(struct hl_device *hdev)
{
	int rc;

	rc = greco_mmu_clear_pgt_range(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to clear MMU page tables range\n");
		return rc;
	}

	rc = greco_init_reserved_sram(hdev);
	if (rc)
		return rc;

	return 0;
}

static int greco_sim_sw_init(struct hl_device *hdev)
{
	struct greco_device *greco;
	int rc;

	/* Allocate device structure */
	greco = kzalloc(sizeof(*greco), GFP_KERNEL);
	if (!greco)
		return -ENOMEM;

	hdev->asic_specific = greco;

	rc = greco_alloc_cpu_accessible_dma_mem(hdev);
	if (rc)
		goto free_greco_device;

	hdev->cpu_accessible_dma_pool = gen_pool_create(ilog2(32), -1);
	if (!hdev->cpu_accessible_dma_pool) {
		dev_err(hdev->dev,
			"Failed to create CPU accessible DMA pool\n");
		rc = -ENOMEM;
		goto free_cpu_dma_mem;
	}

	rc = gen_pool_add(hdev->cpu_accessible_dma_pool,
				(uintptr_t) hdev->cpu_accessible_dma_mem,
				HL_CPU_ACCESSIBLE_MEM_SIZE, -1);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to add memory to CPU accessible DMA pool\n");
		rc = -EFAULT;
		goto free_cpu_accessible_dma_pool;
	}

	mutex_init(&greco->hw_queues_lock_mutex);

	greco->scratchpad_kernel_address = hl_asic_dma_alloc_coherent(hdev, PAGE_SIZE,
								&greco->scratchpad_bus_address,
								GFP_KERNEL | __GFP_ZERO);
	if (!greco->scratchpad_kernel_address) {
		rc = -ENOMEM;
		goto free_cpu_accessible_dma_pool;
	}

	greco_user_mapped_blocks_init(hdev);

	greco_user_interrupt_setup(hdev);

	hdev->supports_coresight = false;
	hdev->supports_sync_stream = true;
	hdev->supports_cb_mapping = true;
	hdev->supports_wait_for_multi_cs = true;

	hdev->asic_funcs->set_pci_memory_regions(hdev);
	hdev->stream_master_qid_arr =
				hdev->asic_funcs->get_stream_master_qid_arr();
	hdev->stream_master_qid_arr_size = GRECO_STREAM_MASTER_ARR_SIZE;

	return 0;

free_cpu_accessible_dma_pool:
	gen_pool_destroy(hdev->cpu_accessible_dma_pool);
free_cpu_dma_mem:
	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, hdev->cpu_accessible_dma_mem,
					hdev->cpu_accessible_dma_address);
free_greco_device:
	kfree(greco);
	return rc;
}

static int greco_sim_sw_fini(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;

	hl_asic_dma_free_coherent(hdev, PAGE_SIZE, greco->scratchpad_kernel_address,
					greco->scratchpad_bus_address);

	mutex_destroy(&greco->hw_queues_lock_mutex);

	gen_pool_destroy(hdev->cpu_accessible_dma_pool);

	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, hdev->cpu_accessible_dma_mem,
					hdev->cpu_accessible_dma_address);

	kfree(greco);

	return 0;
}

static void greco_sim_halt_engines(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	struct hl_simulator_device *edev = greco_simulator_dev_table[hdev->id];
	int i;

	greco_stop_rotator_qmans(hdev);
	greco_stop_mme_qmans(hdev);
	greco_stop_tpc_qmans(hdev);
	greco_stop_dcore_dma_qmans(hdev);
	greco_stop_pci_dma_qmans(hdev);

	greco_pci_dma_stall(hdev);
	greco_dcore_dma_stall(hdev);
	greco_tpc_stall(hdev);
	greco_mme_stall(hdev);
	greco_rotator_stall(hdev);

	greco_stop_dec(hdev);

	greco_disable_rotator_qmans(hdev);
	greco_disable_mme_qmans(hdev);
	greco_disable_tpc_qmans(hdev);
	greco_disable_dcore_dma_qmans(hdev);
	greco_disable_pci_dma_qmans(hdev);

	greco_sim_notify_reset(hdev);

	/* Give simulator some time to prepare for reset */
	msleep(SIM_HALT_WAIT_MSEC);

	edev->reset = true;

	/* Flush any in progress handling of the gen_int ioctl */
	for (i = 0 ; i < MSIX_ENTRIES ; i++) {
		mutex_lock(&edev->irq_mutex[i]);
		mutex_unlock(&edev->irq_mutex[i]);
	}
}

static void greco_sim_enable_msix(struct hl_device *hdev)
{
	struct greco_device *greco = hdev->asic_specific;
	struct hl_cs_irq_info *cs_irq_info;
	int i, relative_idx;

	for (i = GRECO_IRQ_NUM_CS_FIRST ; i <= GRECO_IRQ_NUM_CS_LAST ; i++) {
		relative_idx = i - GRECO_IRQ_NUM_CS_FIRST;
		cs_irq_info = &greco->cs_irq_info_arr[relative_idx];
		cs_irq_info->hdev = hdev;
		cs_irq_info->relative_idx = relative_idx;
	}
}

static int greco_sim_hw_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_simulator_device *edev = greco_simulator_dev_table[hdev->id];
	int rc;

	edev->reset = false;

	greco_init_kdma(hdev);

	rc = greco_mmu_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "failed to initialize MMU\n");
		return rc;
	}

	greco_init_golden_registers(hdev);

	/* Recalculate memory sizes if SRAM is binned */
	if (hdev->sram_binning) {
		greco_set_meminfo(hdev, SRAM_SIZE / 2);

		prop->dram_size = edev->dram_size;
		prop->dram_end_address = prop->dram_base_address +
						prop->dram_size;
	} else {
		/* PCI SRAM region size need to be updated */
		hdev->pci_mem_region[PCI_REGION_SRAM].region_size = SRAM_SIZE;
	}

	greco_init_security(hdev);
	greco_init_pdma(hdev);
	greco_init_ddma(hdev);

	greco_init_mme(hdev);
	greco_init_tpc(hdev);
	greco_init_rotator(hdev);

	greco_init_dec(hdev);

	greco_sim_enable_msix(hdev);

	return 0;
}

static void greco_sim_hw_fini(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	struct greco_device *greco = hdev->asic_specific;
	u32 status, reset_timeout_ms, reset_timeout_us;
	int rc;

	reset_timeout_ms = SIM_RESET_WAIT_MSEC;
	reset_timeout_us = reset_timeout_ms * 1000;

	WREG32(mmPCIE_WRAP_PSOC_RST_CTRL,
			1 << PCIE_WRAP_PSOC_RST_CTRL_HARD_RST_SHIFT);
	dev_info(hdev->dev,
		"Issued HARD reset command, waiting up to %dms\n",
		reset_timeout_ms);

	/* Wait a certain amount of time before checking if the reset has
	 * finished
	 */
	rc = hl_poll_timeout(hdev, mmPCIE_WRAP_PSOC_BOOT_MNG_DONE, status,
		(status & PCIE_WRAP_PSOC_BOOT_MNG_DONE_PSOC_BOOT_MNG_DONE_MASK),
		10000, reset_timeout_us);

	if (rc == -ETIMEDOUT)
		dev_err(hdev->dev,
			"Timeout while waiting for device to reset 0x%x\n",
			status);

	/* Reset bit is not self-clearing, need to manually clear it */
	WREG32(mmPCIE_WRAP_PSOC_RST_CTRL, 0);

	if (greco) {
		greco->hw_cap_initialized &= ~(HW_CAP_DRAM |
				HW_CAP_PMMU | HW_CAP_CPU |
				HW_CAP_CPU_Q | HW_CAP_SRAM_SCRAMBLER |
				HW_CAP_DRAM_SCRAMBLER_MASK |
				HW_CAP_DMMU_MASK |
				HW_CAP_PDMA_MASK |
				HW_CAP_DDMA_MASK | HW_CAP_KDMA_MASK |
				HW_CAP_MME_MASK | HW_CAP_TPC_MASK |
				HW_CAP_ROT_MASK | HW_CAP_DEC_MASK);

		memset(greco->events_stat, 0, sizeof(greco->events_stat));
	}
}

static int greco_sim_suspend(struct hl_device *hdev)
{
	return 0;
}

static int greco_sim_resume(struct hl_device *hdev)
{
	return 0;
}

static int greco_sim_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
			void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	int rc;

	vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY |
			VM_NORESERVE;

	rc = remap_vmalloc_range(vma, cpu_addr, 0);
	if (rc)
		dev_err(hdev->dev, "remap vmalloc error %d", rc);

	return rc;
}

static void *greco_sim_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	struct hl_simulator_device *edev = greco_simulator_dev_table[hdev->id];

	void *address = (void *) gen_pool_alloc(edev->pool, size);

	if (address) {
		if (flags & __GFP_ZERO)
			memset(address, 0, size);

		*dma_handle = virt_to_phys(address);
		if (*dma_handle > HOST_PHYS_SIZE)
			dev_crit(hdev->dev, "invalid dma addr 0x%llx\n",
					*dma_handle);

		/* Shift to the device's base physical address of host memory */
		*dma_handle += HOST_PHYS_BASE;
	}

	return address;
}

static void greco_sim_dma_free_coherent(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle)
{
	struct hl_simulator_device *edev = greco_simulator_dev_table[hdev->id];

	gen_pool_free(edev->pool, (u64) cpu_addr, size);
}

static void *greco_sim_dma_pool_zalloc(struct hl_device *hdev, size_t size,
					gfp_t mem_flags, dma_addr_t *dma_handle)
{
	if (size > SIM_DMA_POOL_BLK_SIZE)
		return NULL;

	return greco_sim_dma_alloc_coherent(hdev, PAGE_SIZE, dma_handle,
					mem_flags | __GFP_ZERO);
}

static void greco_sim_dma_pool_free(struct hl_device *hdev, void *vaddr,
				dma_addr_t dma_addr)
{
	greco_sim_dma_free_coherent(hdev, PAGE_SIZE, vaddr, dma_addr);
}

static u64 greco_sim_read_pte(struct hl_device *hdev, u64 addr)
{
	u64 val;

	if (hdev->reset_info.hard_reset_pending)
		return U64_MAX;

	hl_sim_read_dram(greco_simulator_dev_table[hdev->id], &val,
			 (addr - DRAM_PHYS_BASE), sizeof(val));

	return val;
}

static void greco_sim_write_pte(struct hl_device *hdev, u64 addr, u64 val)
{
	if (hdev->reset_info.hard_reset_pending)
		return;

	hl_sim_write_dram(greco_simulator_dev_table[hdev->id],
			  (addr - DRAM_PHYS_BASE), &val, sizeof(val));
}

static int greco_sim_debug_coresight(struct hl_device *hdev, struct hl_ctx *ctx, void *data)
{
	dev_err(hdev->dev, "CoreSight not supported in simulator\n");

	return -ENXIO;
}

static bool greco_sim_is_device_idle(struct hl_device *hdev, u64 *mask_arr, u8 mask_len,
					struct engines_data *e)
{
	return true;
}

static void greco_sim_hw_queues_lock(struct hl_device *hdev)
	__acquires(&greco->hw_queues_lock_mutex)
{
	struct greco_device *greco = hdev->asic_specific;

	mutex_lock(&greco->hw_queues_lock_mutex);
}

static void greco_sim_hw_queues_unlock(struct hl_device *hdev)
	__releases(&greco->hw_queues_lock_mutex)
{
	struct greco_device *greco = hdev->asic_specific;

	mutex_unlock(&greco->hw_queues_lock_mutex);
}

static u32 greco_sim_get_pci_id(struct hl_device *hdev)
{
	return PCI_IDS_GRECO_SIMULATOR;
}

static int greco_sim_get_eeprom_data(struct hl_device *hdev, void *data,
					size_t max_size)
{
	const char *str = "no EEPROM data on simulator";

	memcpy(data, str, strlen(str) + 1);

	return 0;
}

static void greco_sim_halt_coresight(struct hl_device *hdev, struct hl_ctx *ctx)
{

}

static void greco_sim_ack_protection_bits_errors(struct hl_device *hdev)
{

}

static void greco_sim_add_device_attr(struct hl_device *hdev,
					struct attribute_group *dev_clk_attr_grp,
					struct attribute_group *dev_vrm_attr_grp,
					struct attribute_group *dev_nic_attr_grp)
{
	dev_clk_attr_grp->attrs = greco_sim_dev_attrs;
	dev_vrm_attr_grp->attrs = greco_sim_dev_attrs;
	dev_nic_attr_grp->attrs = greco_sim_dev_attrs;
}

static int greco_sim_block_mmap(struct hl_device *hdev,
			struct vm_area_struct *vma, u32 block_id,
			u32 block_size)
{
	return -EPERM;
}

static int greco_sim_get_hw_block_id(struct hl_device *hdev, u64 block_addr,
					u32 *block_size, u32 *block_id)
{
	struct greco_device *greco = hdev->asic_specific;
	int i;

	for (i = 0 ; i < NUM_USER_MAPPED_BLOCKS ; i++) {
		if ((block_addr >= CFG_BASE + greco->mapped_blocks[i].address)
				&& (block_addr < (CFG_BASE +
				greco->mapped_blocks[i].address +
				greco->mapped_blocks[i].size))) {
			*block_id = i;
			if (block_size)
				*block_size = greco->mapped_blocks[i].size;
			return 0;
		}
	}

	dev_err(hdev->dev, "Invalid block address %#llx", block_addr);

	return -EINVAL;
}

static void greco_sim_enable_events_from_fw(struct hl_device *hdev)
{
	/* Not to be implemented */
}

static int greco_sim_mmu_invalidate_cache_range(struct hl_device *hdev,
				bool is_hard, u32 flags,
				u32 asid, u64 va, u64 size)
{
	/* Not supported by simulator yet, treat as invalidate all */
	return greco_mmu_invalidate_cache(hdev, is_hard, flags);
}

static int greco_sim_send_device_activity(struct hl_device *hdev, bool open)
{
	return 0;
}

static int greco_sim_pll_info_get(struct hl_device *hdev, u32 pll_index,
		u16 *pll_freq_arr)
{
	/*
	 * in simulation PLLs are not supported.
	 */
	memset(pll_freq_arr, 0x0, sizeof(u16) * HL_PLL_NUM_OUTPUTS);

	return 0;
}

static const struct hl_asic_funcs greco_sim_funcs = {
	.early_init = greco_sim_early_init,
	.early_fini = greco_sim_early_fini,
	.late_init = greco_sim_late_init,
	.late_fini = greco_late_fini,
	.sw_init = greco_sim_sw_init,
	.sw_fini = greco_sim_sw_fini,
	.hw_init = greco_sim_hw_init,
	.hw_fini = greco_sim_hw_fini,
	.halt_engines = greco_sim_halt_engines,
	.suspend = greco_sim_suspend,
	.resume = greco_sim_resume,
	.mmap = greco_sim_mmap,
	.ring_doorbell = greco_ring_doorbell,
	.pqe_write = greco_pqe_write,
	.asic_dma_alloc_coherent = greco_sim_dma_alloc_coherent,
	.asic_dma_free_coherent = greco_sim_dma_free_coherent,
	.scrub_device_mem = greco_scrub_device_mem,
	.scrub_device_dram = greco_scrub_device_dram,
	.get_int_queue_base = NULL,
	.test_queues = greco_test_queues,
	.asic_dma_pool_zalloc = greco_sim_dma_pool_zalloc,
	.asic_dma_pool_free = greco_sim_dma_pool_free,
	.cpu_accessible_dma_pool_alloc = greco_cpu_accessible_dma_pool_alloc,
	.cpu_accessible_dma_pool_free = greco_cpu_accessible_dma_pool_free,
	.hl_dma_unmap_sgtable = hl_sim_dma_unmap_sgtable,
	.cs_parser = greco_cs_parser,
	.asic_dma_map_sgtable = hl_sim_dma_map_sgtable,
	.add_end_of_cb_packets = NULL,
	.update_eq_ci = greco_update_eq_ci,
	.context_switch = greco_context_switch,
	.restore_phase_topology = greco_restore_phase_topology,
	.debugfs_read_dma = greco_debugfs_read_dma,
	.add_device_attr = greco_sim_add_device_attr,
	.handle_eqe = greco_handle_eqe,
	.get_events_stat = greco_get_events_stat,
	.read_pte = greco_sim_read_pte,
	.write_pte = greco_sim_write_pte,
	.mmu_invalidate_cache = greco_mmu_invalidate_cache,
	.mmu_invalidate_cache_range = greco_sim_mmu_invalidate_cache_range,
	.mmu_prefetch_cache_range = NULL,
	.send_heartbeat = greco_send_heartbeat,
	.debug_coresight = greco_sim_debug_coresight,
	.is_device_idle = greco_sim_is_device_idle,
	.compute_reset_late_init = greco_compute_reset_late_init,
	.hw_queues_lock = greco_sim_hw_queues_lock,
	.hw_queues_unlock = greco_sim_hw_queues_unlock,
	.get_pci_id = greco_sim_get_pci_id,
	.get_eeprom_data = greco_sim_get_eeprom_data,
	.get_monitor_dump = greco_get_monitor_dump,
	.send_cpu_message = greco_send_cpu_message,
	.nic_init = greco_nic_init,
	.nic_fini = greco_nic_fini,
	.nic_control = greco_nic_control,
	.pci_bars_map = NULL,
	.init_iatu = NULL,
	.rreg = greco_sim_rreg,
	.wreg = greco_sim_wreg,
	.halt_coresight = greco_sim_halt_coresight,
	.ctx_init = greco_ctx_init,
	.ctx_fini = greco_ctx_fini,
	.pre_schedule_cs = greco_pre_schedule_cs,
	.get_queue_id_for_cq = greco_get_queue_id_for_cq,
	.load_firmware_to_device = NULL,
	.load_boot_fit_to_device = NULL,
	.get_signal_cb_size = greco_get_signal_cb_size,
	.get_wait_cb_size = greco_get_wait_cb_size,
	.gen_signal_cb = greco_gen_signal_cb,
	.gen_wait_cb = greco_gen_wait_cb,
	.reset_sob = greco_reset_sob,
	.reset_sob_group = greco_reset_sob_group,
	.get_device_time = greco_get_device_time,
	.pb_print_security_errors = NULL,
	.collective_wait_init_cs = greco_collective_wait_init_cs,
	.collective_wait_create_jobs = greco_collective_wait_create_jobs,
	.get_dec_base_addr = greco_get_dec_base_addr,
	.scramble_addr = hl_mmu_scramble_addr,
	.descramble_addr = hl_mmu_descramble_addr,
	.ack_protection_bits_errors = greco_sim_ack_protection_bits_errors,
	.get_hw_block_id = greco_sim_get_hw_block_id,
	.hw_block_mmap = greco_sim_block_mmap,
	.enable_events_from_fw = greco_sim_enable_events_from_fw,
	.ack_mmu_errors = greco_ack_mmu_page_fault_or_access_error,
	.map_pll_idx_to_fw_idx = greco_map_pll_idx_to_fw_idx,
	.init_cpu_scrambler_dram = NULL,
	.state_dump_init = greco_state_dump_init,
	.get_sob_addr = &greco_get_sob_addr,
	.set_pci_memory_regions = greco_set_pci_memory_regions,
	.get_stream_master_qid_arr = greco_get_stream_master_qid_arr,
	.mmu_get_real_page_size = hl_mmu_get_real_page_size,
	.access_dev_mem = greco_sim_access_dev_mem,
	.set_dram_bar_base = NULL,
	.init_firmware_preload_params = NULL,
	.send_device_activity = greco_sim_send_device_activity,
	.read_fetch_memory_block = NULL,
	.fw_security_emulation_init = greco_fw_security_emulation_init,
	.fw_security_emulation_fini = greco_fw_security_emulation_fini,
	.pll_info_get = greco_sim_pll_info_get,
	.set_dram_properties = greco_set_dram_properties,
	.set_priv_assertions = greco_set_priv_assertions,
	.set_binning_masks = greco_set_binning_masks,
};

void greco_sim_set_asic_funcs(struct hl_device *hdev)
{
	hdev->asic_funcs = &greco_sim_funcs;
}
