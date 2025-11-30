// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#define pr_fmt(fmt)		"hl_importer: " fmt

#include "habanalabs.h"
#include "../include/common/importer_drv.h"

#include <linux/module.h>

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/pci.h>
#include <rdma/ib_umem.h>

static int importer_driver;

module_param(importer_driver, int, 0444);
MODULE_PARM_DESC(importer_driver, "Importer driver (0 = no, 1 = yes, default no)");

#define HL_IMPORTER_NAME	"hl_importer"

#ifdef __IMPORTER

struct hl_importer_mr {
	struct hl_importer_device	*idev;
	struct ib_umem			*umem;
	struct pci_dev			*pdev;
	void __iomem			*kptr;
	u64				id;
	int				fd;
	int				access_flags;
	unsigned int			page_shift;
	int				bar;
};

struct hl_importer_mr_mgr {
	spinlock_t			lock;
	struct idr			handles;
};

static struct hl_importer_device {
	struct ib_device		ib_device;
	struct class			*iclass;
	struct cdev			cdev;
	struct device			*dev;
	struct hl_importer_mr_mgr	mr_mgr;
	u32				major;
	u32				minor;
} importer_dev;

static int hl_importer_get_bar_from_resource(struct pci_dev *pdev,
						const struct resource *resource)
{
	struct resource *res1, *res2 = (struct resource *) resource;
	int i;

	for (i = 0; i < PCI_STD_NUM_BARS; i++) {
		res1 = &pdev->resource[i];

		if (res1->start && resource_contains(res1, res2))
			return i;
	}

	return -EFAULT;
}

static int hl_importer_match_pdev_by_resource(struct device *dev,
						const void *data)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	const struct resource *resource = data;

	if (hl_importer_get_bar_from_resource(pdev, resource) >= 0)
		return 1;

	return 0;
}

static struct pci_dev *
hl_importer_get_pdev_from_resource(const struct resource *resource)
{
	struct pci_dev *pdev = NULL;
	struct device *dev;

	dev = bus_find_device(&pci_bus_type, NULL, resource,
				hl_importer_match_pdev_by_resource);
	if (dev)
		pdev = to_pci_dev(dev);

	return pdev;
}

static int hl_importer_init_dmabuf_mr(struct hl_importer_mr *mr)
{
	struct ib_umem_dmabuf *umem_dmabuf = to_ib_umem_dmabuf(mr->umem);
	struct hl_importer_device *idev = mr->idev;
	struct resource resource = {};
	struct scatterlist *sg;
	int ret, i;

	dma_resv_lock(umem_dmabuf->attach->dmabuf->resv, NULL);
	ret = ib_umem_dmabuf_map_pages(umem_dmabuf);
	if (ret) {
		dma_resv_unlock(umem_dmabuf->attach->dmabuf->resv);
		return ret;
	}

	dma_resv_unlock(umem_dmabuf->attach->dmabuf->resv);

	for_each_sgtable_dma_sg(umem_dmabuf->sgt, sg, i) {
		dev_dbg(idev->dev,
			"sgt node %d: address 0x%llx, length 0x%x\n",
			i, sg_dma_address(sg), sg_dma_len(sg));
		if (!sg_dma_len(sg)) {
			dev_err(idev->dev,
				"length of scatterlist node %d is 0\n", i);
			return -EINVAL;
		}
	}

	/* scatterlist node 0 is used arbitrarily as all memory chunks are for
	 * the same PCI BAR.
	 */
	resource.start = sg_dma_address(&umem_dmabuf->sgt->sgl[0]);
	resource.end = resource.start + sg_dma_len(&umem_dmabuf->sgt->sgl[0]);
	resource.flags = IORESOURCE_MEM;
	mr->pdev = hl_importer_get_pdev_from_resource(&resource);
	if (!mr->pdev) {
		dev_err(idev->dev,
			"failed to find a PCI device with a matching BAR address\n");
		return -EINVAL;
	}

	mr->bar = hl_importer_get_bar_from_resource(mr->pdev, &resource);
	if (mr->bar < 0) {
		dev_err(idev->dev,
			"failed to find a BAR with a matching address\n");
		return -EINVAL;
	}

	mr->kptr = pci_ioremap_wc_bar(mr->pdev, mr->bar);
	if (!mr->kptr) {
		dev_err(idev->dev, "failed to map PCI BAR into CPU space\n");
		return -ENODEV;
	}

	return 0;
}

static struct hl_importer_mr *
alloc_cacheable_mr(struct hl_importer_device *idev, struct ib_umem *umem,
			u64 iova, int access_flags)
{
	struct hl_importer_mr *mr;
	unsigned int page_size;

	umem->iova = iova;
	page_size = PAGE_SIZE;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->access_flags = access_flags;
	mr->page_shift = order_base_2(page_size);

	mr->umem = umem;
	mr->idev = idev;

	return mr;
}

static void hl_importer_fence_dmabuf_mr(struct hl_importer_mr *mr)
{
	struct ib_umem_dmabuf *umem_dmabuf = to_ib_umem_dmabuf(mr->umem);

	dma_resv_lock(umem_dmabuf->attach->dmabuf->resv, NULL);
	umem_dmabuf->private = NULL;
	ib_umem_dmabuf_unmap_pages(umem_dmabuf);
	dma_resv_unlock(umem_dmabuf->attach->dmabuf->resv);
}

static void hl_importer_dereg_mr(struct hl_importer_device *idev,
				struct hl_importer_mr *mr)
{
	struct ib_umem *umem = mr->umem;

	/* Stop all DMA */
	hl_importer_fence_dmabuf_mr(mr);

	ib_umem_release(umem);

	iounmap(mr->kptr);
	pci_dev_put(mr->pdev);
	kfree(mr);
}

static void hl_importer_dmabuf_invalidate_cb(struct dma_buf_attachment *attach)
{
	struct ib_umem_dmabuf *umem_dmabuf = attach->importer_priv;
	struct hl_importer_mr *mr = umem_dmabuf->private;

	dma_resv_assert_held(umem_dmabuf->attach->dmabuf->resv);

	if (!umem_dmabuf->sgt)
		return;

	dev_warn(mr->idev->dev, "Got unexpected move notify request!\n");
}

static struct dma_buf_attach_ops hl_importer_dmabuf_attach_ops = {
	.allow_peer2peer = 1,
	.move_notify = hl_importer_dmabuf_invalidate_cb
};

static struct hl_importer_mr*
hl_importer_reg_user_mr_dmabuf(struct hl_importer_device *idev, u64 offset,
				u64 length, u64 virt_addr, int fd,
				int access_flags)
{
	struct ib_umem_dmabuf *umem_dmabuf;
	struct hl_importer_mr *mr;
	int rc;

	umem_dmabuf = ib_umem_dmabuf_get(&idev->ib_device, offset, length, fd,
					 access_flags,
					 &hl_importer_dmabuf_attach_ops);
	if (IS_ERR(umem_dmabuf)) {
		dev_err(idev->dev, "Failed to get dmabuf %ld\n",
			PTR_ERR(umem_dmabuf));
		return ERR_CAST(umem_dmabuf);
	}

	mr = alloc_cacheable_mr(idev, &umem_dmabuf->umem, virt_addr,
				access_flags);
	if (IS_ERR(mr)) {
		ib_umem_release(&umem_dmabuf->umem);
		dev_err(idev->dev, "Failed to allocate MR %ld\n", PTR_ERR(mr));
		return ERR_CAST(mr);
	}

	umem_dmabuf->private = mr;

	rc = hl_importer_init_dmabuf_mr(mr);
	if (rc) {
		dev_err(idev->dev, "Failed to init MR %d\n", rc);
		goto err_dereg_mr;
	}

	return mr;

err_dereg_mr:
	hl_importer_dereg_mr(idev, mr);
	return ERR_PTR(rc);
}

static int hl_importer_reg_dmabuf_mr_ioctl(struct hl_importer_device *idev,
						void *data)
{
	union hl_importer_reg_dmabuf_mr_args *args = data;
	struct hl_importer_reg_dmabuf_mr_in *in = &args->in;
	struct hl_importer_mr_mgr *mr_mgr = &idev->mr_mgr;
	struct hl_importer_mr *new_mr;
	int rc;

	new_mr = hl_importer_reg_user_mr_dmabuf(idev, in->offset, in->length,
						in->iova, in->fd,
						in->access_flags);
	if (IS_ERR(new_mr))
		return PTR_ERR(new_mr);

	spin_lock(&mr_mgr->lock);
	rc = idr_alloc(&mr_mgr->handles, new_mr, 1, 0, GFP_KERNEL);
	spin_unlock(&mr_mgr->lock);

	if (rc < 0) {
		dev_err(idev->dev, "Failed to allocate IDR for a new MR\n");
		goto dereg_mr;
	}

	new_mr->id = (u64) rc;

	memset(&args->out, 0, sizeof(args->out));
	args->out.mr_handle = new_mr->id;

	return 0;

dereg_mr:
	hl_importer_dereg_mr(idev, new_mr);

	return rc;
}

static int hl_importer_dereg_mr_ioctl(struct hl_importer_device *idev,
					void *data)
{
	struct hl_importer_mr_mgr *mr_mgr = &idev->mr_mgr;
	struct hl_importer_dereg_mr_args *args = data;
	struct hl_importer_mr *mr;

	spin_lock(&mr_mgr->lock);

	mr = idr_find(&mr_mgr->handles, args->mr_handle);

	if (!mr) {
		spin_unlock(&mr_mgr->lock);
		dev_err(idev->dev,
			"MR destroy failed, no match to handle 0x%llx\n",
			args->mr_handle);
		return -EINVAL;
	}

	idr_remove(&mr_mgr->handles, args->mr_handle);

	spin_unlock(&mr_mgr->lock);

	hl_importer_dereg_mr(idev, mr);

	return 0;
}

static int hl_importer_access_mr(struct hl_importer_device *idev,
					struct hl_importer_mr *mr, u64 userptr,
					u64 offset, u64 size, bool is_write)
{
	struct ib_umem_dmabuf *umem_dmabuf = to_ib_umem_dmabuf(mr->umem);
	u64 left_size, copy_size;
	struct scatterlist *sg;
	void __iomem *kptr;
	int rc, i;

	if (offset + size > mr->umem->length) {
		dev_err(idev->dev,
			"offset 0x%llx + size 0x%llx is larger than length of dmabuf 0x%zx\n",
			offset, size, mr->umem->length);
		return -EINVAL;
	}

	left_size = size;
	for_each_sgtable_dma_sg(umem_dmabuf->sgt, sg, i) {
		if (offset >= sg_dma_len(sg)) {
			offset -= sg_dma_len(sg);
			continue;
		}

		copy_size = left_size > sg_dma_len(sg) ? sg_dma_len(sg)
							: left_size;
		kptr = mr->kptr + (sg_dma_address(sg) -
					pci_resource_start(mr->pdev, mr->bar));

		if (offset) {
			if (offset + copy_size > sg_dma_len(sg))
				copy_size = sg_dma_len(sg) - offset;
			kptr += offset;
			offset = 0;
		}

		if (is_write)
			rc = copy_from_user(kptr,
					(void __user *) (uintptr_t) userptr,
					copy_size);
		else
			rc = copy_to_user((void __user *) (uintptr_t) userptr,
					kptr, copy_size);
		if (rc) {
			dev_err(idev->dev,
				"%s failed to %s MR, address 0x%llx, length 0x%llx\n",
				is_write ? "copy_from_user" : "copy_to_user",
				is_write ? "write to" : "read from",
				sg_dma_address(sg), copy_size);
			return rc;
		}

		left_size -= copy_size;
		if (!left_size)
			break;

		userptr += copy_size;
	}

	return rc;
}

static int hl_importer_write_to_mr_ioctl(struct hl_importer_device *idev,
					void *data)
{
	struct hl_importer_mr_mgr *mr_mgr = &idev->mr_mgr;
	struct hl_importer_write_to_mr_args *args = data;
	struct hl_importer_mr *mr;

	spin_lock(&mr_mgr->lock);

	mr = idr_find(&mr_mgr->handles, args->mr_handle);

	if (!mr) {
		spin_unlock(&mr_mgr->lock);
		dev_err(idev->dev,
			"MR write failed, no match to handle 0x%llx\n",
			args->mr_handle);
		return -EINVAL;
	}

	spin_unlock(&mr_mgr->lock);

	return hl_importer_access_mr(idev, mr, args->userptr, args->offset,
					args->size, true);
}

static int hl_importer_read_from_mr_ioctl(struct hl_importer_device *idev,
					void *data)
{
	struct hl_importer_mr_mgr *mr_mgr = &idev->mr_mgr;
	struct hl_importer_read_from_mr_args *args = data;
	struct hl_importer_mr *mr;

	spin_lock(&mr_mgr->lock);

	mr = idr_find(&mr_mgr->handles, args->mr_handle);

	if (!mr) {
		spin_unlock(&mr_mgr->lock);
		dev_err(idev->dev,
			"MR read failed, no match to handle 0x%llx\n",
			args->mr_handle);
		return -EINVAL;
	}

	spin_unlock(&mr_mgr->lock);

	return hl_importer_access_mr(idev, mr, args->userptr, args->offset,
					args->size, false);
}

static int hl_importer_open(struct inode *inode, struct file *filp)
{
	struct hl_importer_device *idev = &importer_dev;

	filp->private_data = idev;
	nonseekable_open(inode, filp);

	spin_lock_init(&idev->mr_mgr.lock);
	idr_init(&idev->mr_mgr.handles);

	return 0;
}

static int hl_importer_release(struct inode *inode, struct file *filp)
{
	struct hl_importer_device *idev = filp->private_data;
	struct hl_importer_mr *mr;
	struct idr *idp;
	u32 id;

	idp = &idev->mr_mgr.handles;

	idr_for_each_entry(idp, mr, id)
		hl_importer_dereg_mr(idev, mr);

	idr_destroy(&idev->mr_mgr.handles);

	return 0;
}

typedef int hl_importer_ioctl_t(struct hl_importer_device *idev, void *data);

struct hl_importer_ioctl_desc {
	unsigned int cmd;
	hl_importer_ioctl_t *func;
};

#define HL_IMPORTER_IOCTL_DEF(ioctl, _func) \
	[_IOC_NR(ioctl)] = {.cmd = ioctl, .func = _func}

/* Ioctl table */
static const struct hl_importer_ioctl_desc hl_importer_ioctls[] = {
	HL_IMPORTER_IOCTL_DEF(HL_IMPORTER_IOCTL_REG_DMABUF_MR,
				hl_importer_reg_dmabuf_mr_ioctl),
	HL_IMPORTER_IOCTL_DEF(HL_IMPORTER_IOCTL_DEREG_MR,
				hl_importer_dereg_mr_ioctl),
	HL_IMPORTER_IOCTL_DEF(HL_IMPORTER_IOCTL_WRITE_TO_MR,
				hl_importer_write_to_mr_ioctl),
	HL_IMPORTER_IOCTL_DEF(HL_IMPORTER_IOCTL_READ_FROM_MR,
				hl_importer_read_from_mr_ioctl),
};

#define HL_IMPORTER_IOCTL_COUNT	ARRAY_SIZE(hl_importer_ioctls)

static long hl_importer_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	struct hl_importer_device *idev = filp->private_data;
	const struct hl_importer_ioctl_desc *ioctl = NULL;
	unsigned int nr = _IOC_NR(cmd);
	hl_importer_ioctl_t *func;
	unsigned int usize, asize;
	char stack_kdata[128];
	char *kdata = NULL;
	u32 hl_size;
	int retcode = -EINVAL;

	if (nr >= HL_IMPORTER_IOCTL_COUNT)
		goto err_i1;

	if (nr < HL_IMPORTER_COMMAND_START || nr >= HL_IMPORTER_COMMAND_END)
		goto err_i1;

	ioctl = &hl_importer_ioctls[nr];

	hl_size = _IOC_SIZE(ioctl->cmd);
	usize = asize = _IOC_SIZE(cmd);
	if (hl_size > asize)
		asize = hl_size;

	cmd = ioctl->cmd;

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		dev_dbg(idev->dev, "no function\n");
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

	retcode = func(idev, kdata);

	if (cmd & IOC_OUT)
		if (copy_to_user((void __user *)arg, kdata, usize))
			retcode = -EFAULT;

err_i1:
	if (!ioctl)
		dev_dbg(idev->dev,
			"invalid ioctl: pid=%d, cmd=0x%02x, nr=0x%02x\n",
			task_pid_nr(current), cmd, nr);

	if (kdata != stack_kdata)
		kfree(kdata);

	if (retcode)
		dev_dbg(idev->dev, "ret = %d\n", retcode);

	return retcode;
}

static const struct file_operations hl_importer_ops = {
	.owner = THIS_MODULE,
	.open = hl_importer_open,
	.release = hl_importer_release,
	.unlocked_ioctl = hl_importer_ioctl,
	.compat_ioctl = hl_importer_ioctl,
};

int hl_importer_init(void)
{
	struct hl_importer_device *idev = &importer_dev;
	char devname[] = "hli";
	dev_t dev;
	int rc;

	if (!importer_driver)
		return 0;

	rc = alloc_chrdev_region(&dev, 0, 1, HL_IMPORTER_NAME);
	if (rc < 0) {
		pr_err("unable to get major\n");
		return rc;
	}

	idev->major = MAJOR(dev);
	idev->minor = MINOR(dev);

	idev->iclass = class_create(HL_IMPORTER_NAME);
	if (IS_ERR(idev->iclass)) {
		pr_err("failed to allocate class\n");
		rc = PTR_ERR(idev->iclass);
		goto remove_major;
	}

	cdev_init(&idev->cdev, &hl_importer_ops);
	idev->cdev.owner = THIS_MODULE;
	rc = cdev_add(&idev->cdev, dev, 1);
	if (rc) {
		pr_err("Failed to add char device %s\n", devname);
		goto remove_class;
	}

	idev->dev = device_create(idev->iclass, NULL, dev, NULL, devname);
	if (IS_ERR(idev->dev)) {
		pr_err("Failed to create device %s\n", devname);
		rc = PTR_ERR(idev->dev);
		goto delete_cdev;
	}

	idev->ib_device.dma_device = idev->dev;

	pr_debug("driver loaded\n");

	return 0;

delete_cdev:
	cdev_del(&idev->cdev);
remove_class:
	class_destroy(idev->iclass);
remove_major:
	unregister_chrdev_region(MKDEV(idev->major, 0), 1);
	return rc;
}

int hl_importer_exit(void)
{
	struct hl_importer_device *idev = &importer_dev;

	if (!importer_driver)
		return 0;

	/* Hide simulator mode device from user */
	device_destroy(idev->iclass, idev->dev->devt);
	cdev_del(&idev->cdev);
	class_destroy(idev->iclass);
	unregister_chrdev_region(MKDEV(idev->major, idev->minor), 1);

	pr_debug("driver removed\n");

	return 0;
}
#else

int hl_importer_init(void)
{
	if (!importer_driver)
		return 0;

	pr_err("importer driver requires at least kernel version 5.12\n");

	return -EINVAL;
}

int hl_importer_exit(void)
{
	return 0;
}

#endif
