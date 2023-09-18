// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#define pr_fmt(fmt)			"habanalabs: " fmt

#include "goyaP.h"
#include "include/common/simulator.h"
#include "include/goya/goya_simulator.h"
#include "include/hw_ip/mmu/mmu_general.h"
#include "include/hw_ip/mmu/mmu_v1_0.h"
#include "include/goya/asic_reg/goya_masks.h"
#include "include/common/pci_ids.h"

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>


static DEFINE_MUTEX(simulator_open);

static struct hl_simulator_device *goya_simulator_dev_table[HL_MAX_MINORS];

static struct attribute *goya_sim_dev_attrs[] = {
	NULL,
};

static int goya_sim_start_device(struct hl_simulator_device *edev);

static int goya_sim_access_dev_mem(struct hl_device *hdev, enum pci_region reg_type,
				u64 addr, u64 *val, enum debugfs_access_type acc_type)
{
	return hl_sim_access_dev_mem(hdev, goya_simulator_dev_table[hdev->id],
			reg_type, addr, val, acc_type);
}

static void goya_simulator_create_device(struct work_struct *work)
{
	struct hl_simulator_device *edev =
			container_of(work, struct hl_simulator_device, work_create.work);
	int rc;

	rc = goya_sim_start_device(edev);
	if (rc) {
		/* Set hdev to NULL to prevent a call to goya_sim_stop_device() */
		edev->hdev = NULL;
		dev_err(edev->dev, "Failed to create Goya Simulator device\n");
	}
}

/**
 * goya_simulator_open - open function for goya simulator device
 *
 * @inode: pointer to inode structure
 * @filp: pointer to file structure
 *
 * Called when the functional simulator opens the goya simulator device.
 */
static int goya_simulator_open(struct inode *inode, struct file *filp)
{
	u32 minor = iminor(inode) - HLV_SIM_ID_OFFSET;
	struct hl_simulator_device *edev;
	int rc = 0;

	if (minor >= HL_MAX_MINORS) {
		pr_crit("habanalabs: minor is out of bounds %u, can't open sim\n", minor);
		return -EINVAL;
	}

	mutex_lock(&simulator_open);

	edev = goya_simulator_dev_table[minor];

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
		"Opening file descriptor on goya simulator device\n");

	filp->private_data = edev;
	nonseekable_open(inode, filp);

	mutex_unlock(&simulator_open);

	if ((edev->sram_user_address) && (edev->dram_user_address)) {
		INIT_DELAYED_WORK(&edev->work_create,
				goya_simulator_create_device);
		schedule_delayed_work(&edev->work_create,
					usecs_to_jiffies(1000));
	}

	return 0;

unlock_mutex:
	mutex_unlock(&simulator_open);
	return rc;
}

/**
 * goya_simulator_release - release function for goya simulator device
 *
 * @inode: pointer to inode structure
 * @filp: pointer to file structure
 *
 * Called when the functional simulator closes the goya simulator device.
 */
static int goya_simulator_release(struct inode *inode, struct file *filp)
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
	}

	goya_simulator_stop(minor);
	hl_sim_remove(minor);

	return 0;
}

static ssize_t goya_simulator_read(struct file *filp, char __user *buffer,
		size_t len, loff_t *off)
{
	struct hl_simulator_device *edev = filp->private_data;

	dev_dbg_once(edev->dev, "Reading %zu bytes from %s in offset 0x%llx\n",
			len, filp->f_path.dentry->d_name.name, *off);

	return hl_sim_read_h2c_fifo(edev, filp, buffer, len);
}

static __poll_t goya_simulator_poll(struct file *filp,
				struct poll_table_struct *wait)
{
	return hl_sim_poll(filp->private_data, filp, wait);
}

static ssize_t goya_simulator_write(struct file *filp, const char __user *buff,
		size_t len, loff_t *off)
{
	struct hl_simulator_device *edev = filp->private_data;

	dev_dbg_once(edev->dev, "Writing %zu bytes to %s in offset 0x%llx\n",
			len, filp->f_path.dentry->d_name.name, *off);

	return hl_sim_write_c2h_fifo(edev, filp, buff, len);
}

/**
 * goya_simulator_mmap - mmap function for goya simulator device
 *
 * @filp: pointer to file structure
 * @vma: pointer to vm_area_struct of the process
 *
 * Called when the functional simulator does an mmap on goya simulator device
 */
static int goya_simulator_mmap(struct file *filp, struct vm_area_struct *vma)
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
				"HBM is already allocated in userspace\n");
			return -EINVAL;
		}
		if (edev->dram_user_address) {
			dev_err(edev->dev,
				"DDR is already mapped to userspace!!!\n");
			return -EINVAL;
		}
		address = edev->dram;
		address_to_update = &edev->dram_user_address;
		dev_dbg(edev->dev, "mapping DDR:\n");
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
		INIT_DELAYED_WORK(&edev->work_create,
				goya_simulator_create_device);
		schedule_delayed_work(&edev->work_create,
					usecs_to_jiffies(1000));
	}

	return rc;
}

static int goya_simulator_gen_int_ioctl(struct hl_simulator_device *edev,
					void *data)
{
	struct simulator_gen_int_args *args = data;

	if (args->id >= NUMBER_OF_INTERRUPTS) {
		dev_err(edev->dev, "interrupt id %d invalid", args->id);
		return -EINVAL;
	}

	mutex_lock(&edev->irq_mutex[args->id]);

	if (unlikely(edev->reset))
		goto out;

	if (args->id < NUMBER_OF_CMPLT_QUEUES)
		hl_irq_handler_cq(args->id,
				&edev->hdev->completion_queue[args->id]);
	else if (args->id == GOYA_EVENT_QUEUE_MSIX_IDX)
		hl_irq_handler_eq(args->id, &edev->hdev->event_queue);
	else
		dev_err(edev->dev, "unexpected interrupt id %d", args->id);
out:
	mutex_unlock(&edev->irq_mutex[args->id]);

	return 0;
}

static int goya_simulator_pci_access_ioctl(
		struct hl_simulator_device *edev, void *data)
{
	struct simulator_pci_access_args *args = data;
	void *src_addr, *dst_addr;
	u64 sram_end_address = edev->sram_user_address + SRAM_SIZE;
	u64 ddr_end_address = edev->dram_user_address + edev->dram_size;
	bool is_shmem = false;
	int rc = 0;

	dev_dbg_once(edev->dev,
			"Goya simulator PCI access IOCTL details:\n");
	dev_dbg_once(edev->dev, "host == 0x%llx\n", args->host_address);
	dev_dbg_once(edev->dev, "device == 0x%llx\n", args->device_address);
	dev_dbg_once(edev->dev, "size == %u\n", args->length);
	dev_dbg_once(edev->dev, "is_write == %d\n", args->is_write);

	if (hl_mem_area_inside_range(args->device_address, args->length,
			edev->sram_user_address, sram_end_address)) {
		args->device_address -= edev->sram_user_address;
		args->device_address += (u64) (edev->sram);
	} else if (hl_mem_area_inside_range(args->device_address, args->length,
			edev->dram_user_address, ddr_end_address)) {
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
			rc = copy_from_user(dst_addr,
				(const void __user *) src_addr, args->length);
		else
			rc = copy_to_user((void __user *) dst_addr,
				(const void *) src_addr, args->length);

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

static int goya_simulator_reset_device_ioctl(struct hl_simulator_device *edev, void *data)
{
	dev_err(edev->dev, "Goya simulator hard-reset request isn't supported\n");

	return -EIO;
}
/*
 * Ioctl function type.
 *
 * \param edev pointer to goya simulator device.
 * \param data pointer to arg that was copied from user.
 */
typedef int goya_simulator_ioctl_t(struct hl_simulator_device *edev,
					void *data);

struct goya_simulator_ioctl_desc {
	unsigned int cmd;
	goya_simulator_ioctl_t *func;
};

#define GOYA_SIMULATOR_IOCTL_DEF(ioctl, _func) \
	[_IOC_NR(ioctl)] = {.cmd = ioctl, .func = _func}

/** Ioctl table */
static const struct goya_simulator_ioctl_desc goya_simulator_ioctls[] = {
	GOYA_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_GEN_INT,
			goya_simulator_gen_int_ioctl),
	GOYA_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_PCI_ACCESS_FROM_DEVICE,
			goya_simulator_pci_access_ioctl),
	GOYA_SIMULATOR_IOCTL_DEF(SIMULATOR_IOCTL_RESET_DEVICE,
			goya_simulator_reset_device_ioctl),
};

#define GOYA_SIMULATOR_IOCTL_COUNT	ARRAY_SIZE(goya_simulator_ioctls)

static long goya_simulator_ioctl(struct file *filep, unsigned int cmd,
			unsigned long arg)
{
	struct hl_simulator_device *edev = filep->private_data;
	goya_simulator_ioctl_t *func;
	const struct goya_simulator_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);
	char stack_kdata[128];
	char *kdata = NULL;
	unsigned int usize, asize;
	int retcode = -EINVAL;

	if (edev->reset) {
		dev_err(edev->dev, "chip has been reset but got IOCTL\n");
		return -ENXIO;
	}

	if (nr >= GOYA_SIMULATOR_IOCTL_COUNT)
		goto err_i1;

	if ((nr >= SIMULATOR_COMMAND_START) &&
		(nr < SIMULATOR_COMMAND_END)) {
		u32 hl_size;

		ioctl = &goya_simulator_ioctls[nr];

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

static const struct file_operations goya_simulator_ops = {
	.owner = THIS_MODULE,
	.open = goya_simulator_open,
	.release = goya_simulator_release,
	.read = goya_simulator_read,
	.poll = goya_simulator_poll,
	.write = goya_simulator_write,
	.mmap = goya_simulator_mmap,
	.unlocked_ioctl = goya_simulator_ioctl,
	.compat_ioctl = goya_simulator_ioctl,
};

static ssize_t device_name_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	const char *name = "goya_simulator";

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

static int goya_sim_start_device(struct hl_simulator_device *edev)
{
	int rc;

	rc = hl_sim_create_hdev(edev);
	if (rc) {
		dev_err(edev->dev, "Failed to create real device for GAUDI simulator\n");
		return rc;
	}

	rc = hl_device_init(edev->hdev);
	if (rc) {
		dev_err(edev->dev, "fatal error during GOYA simulator init\n");
		rc = -ENODEV;
		goto out_err;
	}

	return 0;

out_err:
	hl_sim_destroy_hdev(edev->hdev);
	return rc;
}

static void goya_sim_stop_device(struct hl_device *hdev)
{
	hl_device_fini(hdev);
	hl_sim_destroy_hdev(hdev);
}

int goya_simulator_start(struct simulator_start_args *args)
{
	struct hl_simulator_device *edev;
	bool can_put_dev = false;
	int i, rc;

	edev = kzalloc(sizeof(*edev), GFP_KERNEL);
	if (!edev)
		return -ENOMEM;

	edev->irq_mutex = kcalloc(NUMBER_OF_INTERRUPTS,
			sizeof(*edev->irq_mutex), GFP_KERNEL);
	if (!edev->irq_mutex) {
		kfree(edev);
		return -ENOMEM;
	}

	edev->hclass = args->hclass;
	edev->major = args->major;
	edev->id = args->minor + HLV_SIM_ID_OFFSET;
	edev->rw_reg_timeout = GOYA_SIM_RW_REG_TIMEOUT_US;
	edev->reset = true;
	edev->single_msi_mode = args->single_msi_mode;
	edev->virt_dev_type = args->virt_dev_type;
	edev->dram_user_provided_ptr = args->dram_user_pointer;
	edev->sram_user_provided_ptr = args->sram_user_pointer;

	edev->dram_size = (u64)args->dram_size_in_mb * SZ_1M;

#if IS_ENABLED(CONFIG_DRM_ACCEL)
	sprintf(edev->name, "hlv%d", args->minor + HLV_SIM_ID_OFFSET);
#else
	sprintf(edev->name, "hlv%d", args->minor / 2 + HLV_SIM_ID_OFFSET);
#endif

	sim_devices_init(edev, edev->hclass, edev->id, &goya_simulator_ops,
					edev->name);

	rc = cdev_device_add(&edev->cdev, edev->dev);
	if (rc) {
		dev_err(edev->dev, "Failed to add char device\n");
		goto free_edev;
	}

	/* Allocate shared region between KMD/User and goya simulator */
	edev->shmem = vmalloc_user(GOYA_SHMEM_SIZE);
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
		edev->dram_vmalloc_address =
			vmalloc_user(edev->dram_size + PAGE_SIZE_2MB);
		if (!edev->dram_vmalloc_address) {
			dev_err(edev->dev,
				"Failed to allocate simulator DDR\n");
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
		goto free_ddr;
	}

	rc = gen_pool_add(edev->pool, (u64) edev->shmem, GOYA_SHMEM_SIZE, -1);
	if (rc) {
		dev_err(edev->dev,
			"Failed to add memory to shared memory pool\n");
		rc = -ENOMEM;
		goto free_shared_mem_pool;
	}

	spin_lock_init(&edev->h2c_lock);
	rc = kfifo_alloc(&edev->h2c_fifo,
			GOYA_SIM_PCI_OUTSTANDING * sizeof(void *),
			GFP_KERNEL);
	if (rc) {
		dev_err(edev->dev, "Failed to allocate fifo for h2c\n");
		goto free_shared_mem_pool;
	}

	atomic_set(&edev->h2c_seq, 0);

	spin_lock_init(&edev->c2h_lock);
	init_waitqueue_head(&edev->pollq);
	rc = kfifo_alloc(&edev->c2h_fifo,
			GOYA_SIM_PCI_OUTSTANDING * sizeof(void *),
			GFP_KERNEL);
	if (rc) {
		dev_err(edev->dev, "Failed to allocate fifo for c2h\n");
		goto free_h2c_fifo;
	}

	for (i = 0 ; i < NUMBER_OF_INTERRUPTS ; i++)
		mutex_init(&edev->irq_mutex[i]);

	dev_info(edev->dev,
		"added %s: Goya simulator device [0000:00:%02d.0]\n",
		edev->name, args->minor);

	goya_simulator_dev_table[edev->id - HLV_SIM_ID_OFFSET] = edev;

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
	for (i = 0 ; i < NUMBER_OF_INTERRUPTS ; i++)
		mutex_destroy(&edev->irq_mutex[i]);
	kfifo_free(&edev->c2h_fifo);
free_h2c_fifo:
	kfifo_free(&edev->h2c_fifo);
free_shared_mem_pool:
	goya_simulator_dev_table[edev->id - HLV_SIM_ID_OFFSET] = NULL;
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
	kfree(edev->irq_mutex);

	if (can_put_dev)
		put_device(edev->dev);
	else
		kfree(edev);

	return rc;
}

void goya_simulator_stop(u32 minor)
{
	struct hl_simulator_device *edev;
	struct simulator_msg *msg;
	int count, i;

	if (minor >= HL_MAX_MINORS) {
		pr_crit("habanalabs: minor is out of bounds %u, can't stop sim\n", minor);
		return;
	}

	edev = goya_simulator_dev_table[minor];

	dev_dbg(edev->dev, "Removing Goya simulator device\n");

	/* Make sure work to create simulator has finished */
	cancel_delayed_work_sync(&edev->work_create);

	if (edev->hdev)
		goya_sim_stop_device(edev->hdev);

	/* Disable open on device */
	goya_simulator_dev_table[minor] = NULL;

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

	for (i = 0 ; i < NUMBER_OF_INTERRUPTS ; i++)
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
 * goya_sim_rreg - Read an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 *
 * Returns the value of the MMIO register we are asked to read
 *
 */
static u32 goya_sim_rreg(struct hl_device *hdev, u32 reg)
{
	struct hl_simulator_device *edev = goya_simulator_dev_table[hdev->id];
	u64 reg_addr = CFG_BASE + reg;

	return hl_sim_rreg(hdev, reg_addr, edev);
}

/**
 * goya_sim_wreg - Write to an MMIO register
 *
 * @hdev: pointer to habanalabs device structure
 * @reg: MMIO register offset (in bytes)
 * @val: 32-bit value
 *
 * Writes the 32-bit value into the MMIO register
 *
 */
static void goya_sim_wreg(struct hl_device *hdev, u32 reg, u32 val)
{
	struct hl_simulator_device *edev = goya_simulator_dev_table[hdev->id];
	u64 reg_addr = CFG_BASE + reg;
	hl_sim_wreg(hdev, reg_addr, edev, val);
}

static void goya_sim_notify_reset(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = goya_simulator_dev_table[hdev->id];

	hl_sim_notify_reset(hdev, edev);
}

/* All the code below this point is the goya simulator device implementation */

static int goya_sim_debug_coresight(struct hl_device *hdev, struct hl_ctx *ctx, void *data)
{
	dev_err(hdev->dev, "CoreSight not supported in simulator\n");

	return -ENXIO;
}

static int goya_sim_set_fixed_properties(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_simulator_device *edev =
			goya_simulator_dev_table[hdev->id];
	int rc;

	rc = goya_set_fixed_properties(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to get fixed properties\n");
		return rc;
	}

	prop->dram_size = edev->dram_size;
	prop->dram_end_address = prop->dram_base_address + prop->dram_size;
	prop->dram_user_base_address = GOYA_SIM_DRAM_BASE_ADDR_USER;

	prop->mmu_pgt_addr = GOYA_SIM_MMU_PAGE_TABLES_ADDR;
	prop->mmu_dram_default_page_addr = GOYA_SIM_MMU_DRAM_DEFAULT_PAGE_ADDR;
	prop->mmu_pgt_size = GOYA_SIM_MMU_PAGE_TABLES_SIZE;

	prop->cb_pool_cb_cnt = GOYA_SIM_CB_POOL_CB_CNT;
	prop->cb_pool_cb_size = GOYA_SIM_CB_POOL_CB_SIZE;

	return 0;
}

/*
 * goya_sim_pci_bars_map - Map PCI BARS of Goya device
 *
 * @hdev: pointer to hl_device structure
 *
 * Request PCI regions and map them to kernel virtual addresses.
 * Returns 0 on success
 *
 */
static int goya_sim_pci_bars_map(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = goya_simulator_dev_table[hdev->id];

	/* Simulate SRAM BAR */
	hdev->pcie_bar[SRAM_CFG_BAR_ID] = (void __iomem *) edev->sram;
	dev_dbg(hdev->dev, "CFG at 0x%px\n", hdev->pcie_bar[SRAM_CFG_BAR_ID]);

	/* Simulate DDR BAR */
	hdev->pcie_bar[DDR_BAR_ID] = (void __iomem *) edev->dram;
	dev_dbg(hdev->dev, "DDR at 0x%px\n", hdev->pcie_bar[DDR_BAR_ID]);

	hdev->rmmio = NULL;

	return 0;
}

/*
 * goya_sim_pci_bars_unmap - Unmap PCI BARS of GOYA simulator device
 *
 * @hdev: pointer to habanalabs device structure
 *
 */
static void goya_sim_pci_bars_unmap(struct hl_device *hdev)
{
	hdev->pcie_bar[DDR_BAR_ID] = NULL;
	hdev->pcie_bar[SRAM_CFG_BAR_ID] = NULL;
	hdev->rmmio = NULL;
}

/*
 * goya_sim_early_init - GOYA simulator early initialization code
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Map PCI bars
 *
 */
static int goya_sim_early_init(struct hl_device *hdev)
{
	int rc;

	rc = goya_sim_set_fixed_properties(hdev);
	if (rc)
		return rc;

	rc = goya_sim_pci_bars_map(hdev);
	if (rc)
		goto free_queue_props;

	return 0;

free_queue_props:
	kfree(hdev->asic_prop.hw_queues_props);
	return rc;
}

/*
 * goya_sim_early_fini - GOYA simulator early finalization code
 *
 * @hdev: pointer to habanalabs device structure
 *
 * Unmap PCI bars
 *
 */
static int goya_sim_early_fini(struct hl_device *hdev)
{
	kfree(hdev->asic_prop.hw_queues_props);
	goya_sim_pci_bars_unmap(hdev);

	return 0;
}

/*
 * goya_sim_sw_init - Goya Simulator software initialization code
 *
 * @hdev: pointer to habanalabs device structure
 *
 */
static int goya_sim_sw_init(struct hl_device *hdev)
{
	struct goya_device *goya;
	int rc;

	/* Allocate device structure */
	goya = kzalloc(sizeof(*goya), GFP_KERNEL);
	if (!goya)
		return -ENOMEM;

	goya->mme_clk = GOYA_PLL_FREQ_LOW;
	goya->tpc_clk = GOYA_PLL_FREQ_LOW;
	goya->ic_clk = GOYA_PLL_FREQ_LOW;

	hdev->asic_specific = goya;

	hdev->cpu_accessible_dma_mem = hl_asic_dma_alloc_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE,
							&hdev->cpu_accessible_dma_address,
							GFP_KERNEL | __GFP_ZERO);

	if (!hdev->cpu_accessible_dma_mem) {
		rc = -ENOMEM;
		goto free_goya_device;
	}

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

	goya->goya_work = kmalloc(sizeof(struct goya_work_freq), GFP_KERNEL);
	if (!goya->goya_work) {
		rc = -ENOMEM;
		goto free_cpu_accessible_dma_pool;
	}

	goya->goya_work->hdev = hdev;
	INIT_DELAYED_WORK(&goya->goya_work->work_freq, goya_set_freq_to_low_job);

	mutex_init(&goya->hw_queues_lock_mutex);
	hdev->supports_coresight = false;
	hdev->supports_wait_for_multi_cs = false;
	hdev->supports_ctx_switch = true;

	hdev->asic_funcs->set_pci_memory_regions(hdev);

	return 0;

free_cpu_accessible_dma_pool:
	gen_pool_destroy(hdev->cpu_accessible_dma_pool);
free_cpu_dma_mem:
	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE,
					hdev->cpu_accessible_dma_mem,
					hdev->cpu_accessible_dma_address);
free_goya_device:
	kfree(goya);

	return rc;
}

/*
 * goya_sim_sw_fini - Goya Simulator software tear-down code
 *
 * @hdev: pointer to habanalabs device structure
 *
 */
static int goya_sim_sw_fini(struct hl_device *hdev)
{
	struct goya_device *goya = hdev->asic_specific;

	mutex_destroy(&goya->hw_queues_lock_mutex);

	gen_pool_destroy(hdev->cpu_accessible_dma_pool);

	hl_asic_dma_free_coherent(hdev, HL_CPU_ACCESSIBLE_MEM_SIZE, hdev->cpu_accessible_dma_mem,
					hdev->cpu_accessible_dma_address);

	kfree(goya->goya_work);
	kfree(goya);

	return 0;
}

static void goya_sim_halt_engines(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	struct hl_simulator_device *edev = goya_simulator_dev_table[hdev->id];
	int i;

	goya_stop_external_queues(hdev);
	goya_stop_internal_queues(hdev);

	goya_dma_stall(hdev);
	goya_tpc_stall(hdev);
	goya_mme_stall(hdev);

	goya_disable_external_queues(hdev);
	goya_disable_internal_queues(hdev);

	goya_sim_notify_reset(hdev);

	goya_mmu_remove_device_cpu_mappings(hdev);

	/* Give simulator some time to prepare for reset */
	msleep(GOYA_SIM_HALT_WAIT_MSEC);

	edev->reset = true;

	/* Flush any in progress handling of the gen_int ioctl */
	for (i = 0 ; i < NUMBER_OF_INTERRUPTS ; i++) {
		mutex_lock(&edev->irq_mutex[i]);
		mutex_unlock(&edev->irq_mutex[i]);
	}
}

/*
 * goya_sim_hw_init - Goya hardware initialization code
 *
 * @hdev: pointer to hl_device structure
 *
 * Returns 0 on success
 *
 */
static int goya_sim_hw_init(struct hl_device *hdev)
{
	struct hl_simulator_device *edev = goya_simulator_dev_table[hdev->id];
	int rc;

	rc = goya_mmu_init(hdev);
	if (rc)
		return rc;

	goya_init_security(hdev);

	goya_init_dma_qmans(hdev);

	goya_init_mme_qmans(hdev);

	goya_init_tpc_qmans(hdev);

	edev->reset = false;

	return rc;
}

static int goya_sim_hw_fini(struct hl_device *hdev, bool hard_reset, bool fw_reset)
{
	struct goya_device *goya = hdev->asic_specific;
	u32 status, reset_timeout_ms, reset_timeout_us;
	int rc;

	reset_timeout_ms = GOYA_SIM_RESET_WAIT_MSEC;
	reset_timeout_us = reset_timeout_ms * 1000;

	WREG32(mmPSOC_GLOBAL_CONF_SW_ALL_RST_CFG, RESET_ALL);
	dev_dbg(hdev->dev,
		"Issued HARD reset command, going to wait %dms\n",
		reset_timeout_ms);

	/* Wait a certain amount of time before checking if the reset has
	 * finished
	 */
	rc = hl_poll_timeout(hdev, mmPSOC_GLOBAL_CONF_BTM_FSM, status,
		(status & PSOC_GLOBAL_CONF_BTM_FSM_STATE_MASK),
		10000, reset_timeout_us);

	if (rc == -ETIMEDOUT)
		dev_err(hdev->dev,
			"Timeout while waiting for device to reset 0x%x\n",
			status);

	if (goya) {
		goya->hw_cap_initialized &= ~(HW_CAP_CPU | HW_CAP_CPU_Q |
				HW_CAP_DDR_0 | HW_CAP_DDR_1 |
				HW_CAP_DMA | HW_CAP_MME |
				HW_CAP_MMU | HW_CAP_TPC_MBIST |
				HW_CAP_GOLDEN | HW_CAP_TPC);

		memset(goya->events_stat, 0, sizeof(goya->events_stat));
	}
	return 0;
}

static int goya_sim_suspend(struct hl_device *hdev)
{
	return 0;
}

static int goya_sim_resume(struct hl_device *hdev)
{
	return 0;
}

static int goya_sim_mmap(struct hl_device *hdev, struct vm_area_struct *vma,
			void *cpu_addr, dma_addr_t dma_addr, size_t size)
{
	int rc;

	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY | VM_NORESERVE);

	rc = remap_vmalloc_range(vma, cpu_addr, 0);
	if (rc)
		dev_err(hdev->dev, "remap vmalloc error %d", rc);

	return rc;
}

static void *goya_sim_dma_alloc_coherent(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	struct hl_simulator_device *edev = goya_simulator_dev_table[hdev->id];
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

static void goya_sim_dma_free_coherent(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle)
{
	struct hl_simulator_device *edev = goya_simulator_dev_table[hdev->id];

	gen_pool_free(edev->pool, (u64) cpu_addr, size);
}

static void *goya_sim_dma_pool_zalloc(struct hl_device *hdev, size_t size,
				gfp_t mem_flags, dma_addr_t *dma_handle)
{
	if (size > GOYA_SIM_DMA_POOL_BLK_SIZE)
		return NULL;

	return goya_sim_dma_alloc_coherent(hdev, PAGE_SIZE, dma_handle,
					mem_flags | __GFP_ZERO);
}

static void goya_sim_dma_pool_free(struct hl_device *hdev, void *vaddr,
				dma_addr_t dma_addr)
{
	goya_sim_dma_free_coherent(hdev, PAGE_SIZE, vaddr, dma_addr);
}

static u64 goya_sim_read_pte(struct hl_device *hdev, u64 addr)
{
	u64 val;

	if (hdev->reset_info.hard_reset_pending)
		return U64_MAX;

	hl_sim_read_dram(goya_simulator_dev_table[hdev->id], &val,
			 (addr - DRAM_PHYS_BASE), sizeof(val));

	return val;
}

static void goya_sim_write_pte(struct hl_device *hdev, u64 addr, u64 val)
{
	if (hdev->reset_info.hard_reset_pending)
		return;

	hl_sim_write_dram(goya_simulator_dev_table[hdev->id],
			  (addr - DRAM_PHYS_BASE), &val, sizeof(val));
}

static int goya_sim_compute_reset_late_init(struct hl_device *hdev)
{
	return 0;
}

static bool goya_sim_is_device_idle(struct hl_device *hdev, u64 *mask_arr, u8 mask_len,
					struct engines_data *e)
{
	return true;
}

static void goya_sim_hw_queues_lock(struct hl_device *hdev)
	__acquires(&goya->hw_queues_lock_mutex)
{
	struct goya_device *goya = hdev->asic_specific;

	mutex_lock(&goya->hw_queues_lock_mutex);
}

static void goya_sim_hw_queues_unlock(struct hl_device *hdev)
	__releases(&goya->hw_queues_lock_mutex)
{
	struct goya_device *goya = hdev->asic_specific;

	mutex_unlock(&goya->hw_queues_lock_mutex);
}

static u32 goya_sim_get_pci_id(struct hl_device *hdev)
{
	return PCI_IDS_GOYA_SIMULATOR;
}

static int goya_sim_get_eeprom_data(struct hl_device *hdev, void *data,
		size_t max_size)
{
	const char *str = "no EEPROM data on simulator";

	memcpy(data, str, strlen(str) + 1);

	return 0;
}

static void goya_sim_halt_coresight(struct hl_device *hdev, struct hl_ctx *ctx)
{

}

static void goya_sim_add_device_attr(struct hl_device *hdev,
					struct attribute_group *dev_clk_attr_grp,
					struct attribute_group *dev_vrm_attr_grp)
{
	dev_clk_attr_grp->attrs = goya_sim_dev_attrs;
	dev_vrm_attr_grp->attrs = goya_sim_dev_attrs;
}

static int goya_sim_mmu_invalidate_cache_range(struct hl_device *hdev,
		bool is_hard, u32 flags, u32 asid, u64 va, u64 size)
{
	/* Treat as invalidate all */
	return goya_mmu_invalidate_cache(hdev, is_hard, flags);
}

static int goya_sim_send_device_activity(struct hl_device *hdev, bool open)
{
	return 0;
}

static int goya_sim_pll_info_get(struct hl_device *hdev, u32 pll_index,
		u16 *pll_freq_arr)
{
	/*
	 * in simulation PLLs are not supported.
	 */
	memset(pll_freq_arr, 0x0, sizeof(u16) * HL_PLL_NUM_OUTPUTS);

	return 0;
}

static const struct hl_asic_funcs goya_sim_funcs = {
	.early_init = goya_sim_early_init,
	.early_fini = goya_sim_early_fini,
	.late_init = goya_late_init,
	.late_fini = goya_late_fini,
	.sw_init = goya_sim_sw_init,
	.sw_fini = goya_sim_sw_fini,
	.hw_init = goya_sim_hw_init,
	.hw_fini = goya_sim_hw_fini,
	.halt_engines = goya_sim_halt_engines,
	.suspend = goya_sim_suspend,
	.resume = goya_sim_resume,
	.mmap = goya_sim_mmap,
	.ring_doorbell = goya_ring_doorbell,
	.pqe_write = goya_pqe_write,
	.asic_dma_alloc_coherent = goya_sim_dma_alloc_coherent,
	.asic_dma_free_coherent = goya_sim_dma_free_coherent,
	.scrub_device_mem = goya_scrub_device_mem,
	.scrub_device_dram = goya_scrub_device_dram,
	.get_int_queue_base = goya_get_int_queue_base,
	.test_queues = goya_test_queues,
	.asic_dma_pool_zalloc = goya_sim_dma_pool_zalloc,
	.asic_dma_pool_free = goya_sim_dma_pool_free,
	.cpu_accessible_dma_pool_alloc = goya_cpu_accessible_dma_pool_alloc,
	.cpu_accessible_dma_pool_free = goya_cpu_accessible_dma_pool_free,
	.dma_unmap_sgtable = hl_sim_dma_unmap_sgtable,
	.cs_parser = goya_cs_parser,
	.dma_map_sgtable = hl_sim_dma_map_sgtable,
	.add_end_of_cb_packets = goya_add_end_of_cb_packets,
	.update_eq_ci = goya_update_eq_ci,
	.context_switch = goya_context_switch,
	.restore_phase_topology = goya_restore_phase_topology,
	.debugfs_read_dma = goya_debugfs_read_dma,
	.add_device_attr = goya_sim_add_device_attr,
	.handle_eqe = goya_handle_eqe,
	.get_events_stat = goya_get_events_stat,
	.read_pte = goya_sim_read_pte,
	.write_pte = goya_sim_write_pte,
	.mmu_invalidate_cache = goya_mmu_invalidate_cache,
	.mmu_invalidate_cache_range = goya_sim_mmu_invalidate_cache_range,
	.mmu_prefetch_cache_range = NULL,
	.send_heartbeat = goya_send_heartbeat,
	.debug_coresight = goya_sim_debug_coresight,
	.is_device_idle = goya_sim_is_device_idle,
	.compute_reset_late_init = goya_sim_compute_reset_late_init,
	.hw_queues_lock = goya_sim_hw_queues_lock,
	.hw_queues_unlock = goya_sim_hw_queues_unlock,
	.get_pci_id = goya_sim_get_pci_id,
	.get_eeprom_data = goya_sim_get_eeprom_data,
	.get_monitor_dump = goya_get_monitor_dump,
	.send_cpu_message = goya_send_cpu_message,
	.cn_init = goya_cn_init,
	.cn_fini = goya_cn_fini,
	.cn_control = goya_cn_control,
	.pci_bars_map = NULL,
	.init_iatu = NULL,
	.rreg = goya_sim_rreg,
	.wreg = goya_sim_wreg,
	.halt_coresight = goya_sim_halt_coresight,
	.ctx_init = goya_ctx_init,
	.ctx_fini = goya_ctx_fini,
	.pre_schedule_cs = goya_pre_schedule_cs,
	.get_queue_id_for_cq = goya_get_queue_id_for_cq,
	.load_firmware_to_device = NULL,
	.load_boot_fit_to_device = NULL,
	.get_signal_cb_size = goya_get_signal_cb_size,
	.get_wait_cb_size = goya_get_wait_cb_size,
	.gen_signal_cb = goya_gen_signal_cb,
	.gen_wait_cb = goya_gen_wait_cb,
	.reset_sob = goya_reset_sob,
	.reset_sob_group = goya_reset_sob_group,
	.get_device_time = goya_get_device_time,
	.pb_print_security_errors = NULL,
	.collective_wait_init_cs = goya_collective_wait_init_cs,
	.collective_wait_create_jobs = goya_collective_wait_create_jobs,
	.get_dec_base_addr = NULL,
	.scramble_addr = hl_mmu_scramble_addr,
	.descramble_addr = hl_mmu_descramble_addr,
	.ack_protection_bits_errors = goya_ack_protection_bits_errors,
	.get_hw_block_id = goya_get_hw_block_id,
	.hw_block_mmap = goya_block_mmap,
	.enable_events_from_fw = goya_enable_events_from_fw,
	.ack_mmu_errors = goya_ack_mmu_page_fault_or_access_error,
	.map_pll_idx_to_fw_idx = goya_map_pll_idx_to_fw_idx,
	.state_dump_init = goya_state_dump_init,
	.get_sob_addr = &goya_get_sob_addr,
	.set_pci_memory_regions = goya_set_pci_memory_regions,
	.get_stream_master_qid_arr = goya_get_stream_master_qid_arr,
	.mmu_get_real_page_size = hl_mmu_get_real_page_size,
	.access_dev_mem = goya_sim_access_dev_mem,
	.set_dram_bar_base = NULL,
	.init_firmware_preload_params = NULL,
	.send_device_activity = goya_sim_send_device_activity,
	.fw_security_emulation_init = goya_fw_security_emulation_init,
	.fw_security_emulation_fini = goya_fw_security_emulation_fini,
	.pll_info_get = goya_sim_pll_info_get,
	.set_dram_properties = goya_set_dram_properties,
	.set_priv_assertions = goya_set_priv_assertions,
	.set_binning_masks = goya_set_binning_masks,
};

/**
 * goya_sim_set_asic_funcs - set GOYA Simulator function pointers
 *
 * @hdev: pointer to habanalabs device structure
 *
 */
void goya_sim_set_asic_funcs(struct hl_device *hdev)
{
	hdev->asic_funcs = &goya_sim_funcs;
}
