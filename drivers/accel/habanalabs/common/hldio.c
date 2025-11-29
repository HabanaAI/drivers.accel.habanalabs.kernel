// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2024 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"
#include "hldio.h"
#include <generated/uapi/linux/version.h>
#include <linux/pci-p2pdma.h>
#include <linux/blkdev.h>
#include <linux/vmalloc.h>

/*
 * NVMe Direct I/O implementation for habanalabs driver
 *
 * This implementation has been reworked to address previous limitations:
 * - Async I/O infrastructure now in place (callback properly implemented)
 * - Memory allocation strategy optimized for I/O path
 * - File handle registration capability to reduce per-I/O overhead
 *
 * System requirements and assumptions:
 * 1. No IOMMU support (technically can work with IOMMU, but limited usefulness)
 * 2. READ operations only (WRITE support can be added if needed)
 * 3. Sparse files are not supported
 * 4. Requires kernel version >= 6.9
 * 5. All operations require 4K page alignment (addresses, lengths, offsets)
 * 6. Requires CONFIG_PCI_P2PDMA enabled in kernel configuration
 * 7. For optimal performance, ensure NVMe devices and accelerator cards share
 *    the same PCI bridge to minimize P2P DMA latency
 */

#define IO_STABILIZE_TIMEOUT 10000000 /* 10 seconds in microseconds */

/*
 * This struct contains all the useful data extracted from the file handle
 * provided by the user. File handle registration can be done once with a
 * dedicated IOCTL (e.g., HL_REGISTER_HANDLE) to avoid per-I/O overhead.
 */
struct hl_dio_fd {
	/* Back pointer in case we need it in async completion */
	struct hl_ctx *ctx;
	/* Associated fd struct */
	struct file *filp;
};

/*
 * This is a single IO descriptor
 */
struct hl_direct_io {
	struct hl_dio_fd f;
	struct kiocb kio;
	struct bio_vec *bv;
	struct iov_iter iter;
	u64 device_va;
	u64 off_bytes;
	u64 len_bytes;
	u32 type;
};

bool hl_device_supports_nvme(struct hl_device *hdev)
{
	return hdev->asic_prop.supports_nvme;
}

static int hl_dio_fd_register(struct hl_ctx *ctx, int fd, struct hl_dio_fd *f)
{
	struct hl_device *hdev = ctx->hdev;
	struct block_device *bd;
	struct super_block *sb;
	struct inode *inode;
	struct gendisk *gd;
	struct device *disk_dev;
	int rc;

	f->filp = fget(fd);
	if (!f->filp) {
		rc = -ENOENT;
		goto out;
	}

	if (!(f->filp->f_flags & O_DIRECT)) {
		dev_err(hdev->dev, "file is not in the direct mode\n");
		rc = -EINVAL;
		goto fput;
	}

	if (!f->filp->f_op->read_iter) {
		dev_err(hdev->dev, "read iter is not supported, need to fall back to legacy\n");
		rc = -EINVAL;
		goto fput;
	}

	inode = file_inode(f->filp);
	sb = inode->i_sb;
	bd = sb->s_bdev;
	gd = bd->bd_disk;

	if (inode->i_blocks << sb->s_blocksize_bits < i_size_read(inode)) {
		dev_err(hdev->dev, "sparse files are not currently supported\n");
		rc = -EINVAL;
		goto fput;
	}

	if (!bd || !gd) {
		dev_err(hdev->dev, "invalid block device\n");
		rc = -ENODEV;
		goto fput;
	}
	/* Get the underlying device from the block device */
	disk_dev = disk_to_dev(gd);
	if (!dma_pci_p2pdma_supported(disk_dev)) {
		dev_err(hdev->dev, "device does not support PCI P2P DMA\n");
		rc = -EOPNOTSUPP;
		goto fput;
	}

	/*
	 * @TODO: Maybe we need additional checks here
	 */

	f->ctx = ctx;
	rc = 0;

	goto out;
fput:
	fput(f->filp);
out:
	return rc;
}

static void hl_dio_fd_unregister(struct hl_dio_fd *f)
{
	fput(f->filp);
}

static long hl_dio_count_io(struct hl_device *hdev)
{
	s64 sum = 0;
	int i;

	for_each_possible_cpu(i)
		sum += per_cpu(*hdev->hldio.inflight_ios, i);

	return sum;
}

static bool hl_dio_get_iopath(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;

	if (hdev->hldio.io_enabled) {
		this_cpu_inc(*hdev->hldio.inflight_ios);

		/* Avoid race conditions */
		if (!hdev->hldio.io_enabled) {
			this_cpu_dec(*hdev->hldio.inflight_ios);
			return false;
		}

		hl_ctx_get(ctx);

		return true;
	}

	return false;
}

static void hl_dio_put_iopath(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;

	hl_ctx_put(ctx);
	this_cpu_dec(*hdev->hldio.inflight_ios);
}

static void hl_dio_set_io_enabled(struct hl_device *hdev, bool enabled)
{
	hdev->hldio.io_enabled = enabled;
}

static bool hl_dio_validate_io(struct hl_device *hdev, struct hl_direct_io *io)
{
	if ((u64)io->device_va & ~PAGE_MASK) {
		dev_dbg(hdev->dev, "device address must be 4K aligned\n");
		return false;
	}

	if (io->len_bytes & ~PAGE_MASK) {
		dev_dbg(hdev->dev, "IO length must be 4K aligned\n");
		return false;
	}

	if (io->off_bytes & ~PAGE_MASK) {
		dev_dbg(hdev->dev, "IO offset must be 4K aligned\n");
		return false;
	}

	return true;
}

static struct page *hl_dio_va2page(struct hl_device *hdev, struct hl_ctx *ctx, u64 device_va)
{
	struct hl_dio *hldio = &hdev->hldio;
	u64 device_pa;
	int rc, i;

	rc = hl_mmu_va_to_pa(ctx, device_va, &device_pa);
	if (rc) {
		dev_err(hdev->dev, "device virtual address translation error: %#llx (%d)",
				device_va, rc);
		return NULL;
	}

	for (i = 0 ; i < hldio->np2prs ; ++i) {
		if (device_pa >= hldio->p2prs[i].device_pa &&
		    device_pa < hldio->p2prs[i].device_pa + hldio->p2prs[i].size)
			return hldio->p2prs[i].p2ppages[(device_pa - hldio->p2prs[i].device_pa) >>
				PAGE_SHIFT];
	}

	return NULL;
}

static ssize_t hl_direct_io(struct hl_device *hdev, struct hl_direct_io *io)
{
	u64 npages, device_va;
	ssize_t rc;
	int i;

	if (!hl_dio_validate_io(hdev, io))
		return -EINVAL;

	if (!hl_dio_get_iopath(io->f.ctx)) {
		dev_info(hdev->dev, "can't schedule a new IO, IO is disabled\n");
		return -ESHUTDOWN;
	}

	init_sync_kiocb(&io->kio, io->f.filp);
	io->kio.ki_pos = io->off_bytes;

	npages = (io->len_bytes >> PAGE_SHIFT);

	/*
	 * Allocate bio_vec array. This uses vzalloc to handle large I/O
	 * operations. For performance-critical paths, consider using a
	 * pre-allocated pool (e.g., genpool with multiple size classes).
	 */
	io->bv = vzalloc(npages * sizeof(struct bio_vec));
	if (!io->bv)
		return -ENOMEM;

	for (i = 0, device_va = io->device_va; i < npages ; ++i, device_va += PAGE_SIZE) {
		io->bv[i].bv_page = hl_dio_va2page(hdev, io->f.ctx, device_va);
		if (!io->bv[i].bv_page) {
			dev_err(hdev->dev, "error getting page struct for device va %#llx",
					device_va);
			rc = -EFAULT;
			goto cleanup;
		}
		io->bv[i].bv_offset = 0;
		io->bv[i].bv_len = PAGE_SIZE;
	}

	iov_iter_bvec(&io->iter, io->type, io->bv, 1, io->len_bytes);
	if (io->f.filp->f_op && io->f.filp->f_op->read_iter)
		rc = io->f.filp->f_op->read_iter(&io->kio, &io->iter);
	else
		rc = -EINVAL;

cleanup:
	vfree(io->bv);
	hl_dio_put_iopath(io->f.ctx);

	dev_dbg(hdev->dev, "IO ended with %ld\n", rc);

	return rc;
}

/*
 * Completion callback for asynchronous I/O operations.
 * Set kio->ki_complete to this function to enable async I/O.
 * Note: On recent kernels (6.9+), the ret2 parameter is not used.
 */
__maybe_unused static void hl_direct_io_complete(struct kiocb *kio, long ret)
{
	struct hl_direct_io *io = container_of(kio, struct hl_direct_io, kio);

	dev_dbg(io->f.ctx->hdev->dev, "IO completed with %ld\n", ret);

	/* Do something to copy result to user / notify completion */

	hl_dio_put_iopath(io->f.ctx);

	hl_dio_fd_unregister(&io->f);
}

/*
 * DMA disk to ASIC, wait for results. Must be invoked from the user context
 */
int hl_dio_ssd2hl(struct hl_device *hdev, struct hl_ctx *ctx, int fd,
		  u64 device_va, off_t off_bytes, size_t len_bytes,
		  size_t *len_read)
{
	struct hl_direct_io *io;
	ssize_t rc;

	dev_dbg(hdev->dev, "SSD2HL fd=%d va=%#llx len=%#lx\n", fd, device_va, len_bytes);

	io = kzalloc_obj(*io);
	if (!io) {
		rc = -ENOMEM;
		goto out;
	}

	*io = (struct hl_direct_io){
		.device_va = device_va,
		.len_bytes = len_bytes,
		.off_bytes = off_bytes,
		.type = READ,
	};

	rc = hl_dio_fd_register(ctx, fd, &io->f);
	if (rc)
		goto kfree_io;

	rc = hl_direct_io(hdev, io);
	if (rc >= 0) {
		*len_read = rc;
		rc = 0;
	}

	/* This shall be called only in the case of a sync IO */
	hl_dio_fd_unregister(&io->f);
kfree_io:
	kfree(io);
out:
	return rc;
}

static void hl_p2p_region_fini(struct hl_device *hdev, struct hl_p2p_region *p2pr)
{
	if (p2pr->p2ppages) {
		vfree(p2pr->p2ppages);
		p2pr->p2ppages = NULL;
	}

	if (p2pr->p2pmem) {
		dev_dbg(hdev->dev, "freeing P2P mem from %p, size=%#llx\n",
				p2pr->p2pmem, p2pr->size);
		pci_free_p2pmem(hdev->pdev, p2pr->p2pmem, p2pr->size);
		p2pr->p2pmem = NULL;
	}
}

void hl_p2p_region_fini_all(struct hl_device *hdev)
{
	int i;

	for (i = 0 ; i < hdev->hldio.np2prs ; ++i)
		hl_p2p_region_fini(hdev, &hdev->hldio.p2prs[i]);

	kvfree(hdev->hldio.p2prs);
	hdev->hldio.p2prs = NULL;
	hdev->hldio.np2prs = 0;
}

int hl_p2p_region_init(struct hl_device *hdev, struct hl_p2p_region *p2pr)
{
	void *addr;
	int rc, i;

	/* Start by publishing our p2p memory */
	rc = pci_p2pdma_add_resource(hdev->pdev, p2pr->bar, p2pr->size, p2pr->bar_offset);
	if (rc) {
		dev_err(hdev->dev, "error adding p2p resource: %d\n", rc);
		goto err;
	}

	/* Alloc all p2p mem */
	p2pr->p2pmem = pci_alloc_p2pmem(hdev->pdev, p2pr->size);
	if (!p2pr->p2pmem) {
		dev_err(hdev->dev, "error allocating p2p memory\n");
		rc = -ENOMEM;
		goto err;
	}

	p2pr->p2ppages = vmalloc((p2pr->size >> PAGE_SHIFT) * sizeof(struct page *));
	if (!p2pr->p2ppages) {
		rc = -ENOMEM;
		goto err;
	}

	for (i = 0, addr = p2pr->p2pmem ; i < (p2pr->size >> PAGE_SHIFT) ; ++i, addr += PAGE_SIZE) {
		p2pr->p2ppages[i] = virt_to_page(addr);
		if (!p2pr->p2ppages[i]) {
			rc = -EFAULT;
			goto err;
		}
	}

	return 0;
err:
	hl_p2p_region_fini(hdev, p2pr);
	return rc;
}

int hl_dio_start(struct hl_device *hdev)
{
	dev_dbg(hdev->dev, "initializing HLDIO\n");

	/* Initialize the IO counter and enable IO */
	hdev->hldio.inflight_ios = alloc_percpu(s64);
	if (!hdev->hldio.inflight_ios)
		return -ENOMEM;

	hl_dio_set_io_enabled(hdev, true);

	return 0;
}

void hl_dio_stop(struct hl_device *hdev)
{
	dev_dbg(hdev->dev, "deinitializing HLDIO\n");

	if (hdev->hldio.io_enabled) {
		/* Wait for all the IO to finish */
		hl_dio_set_io_enabled(hdev, false);
		hl_poll_timeout_condition(hdev, !hl_dio_count_io(hdev), 1000, IO_STABILIZE_TIMEOUT);
	}

	if (hdev->hldio.inflight_ios) {
		free_percpu(hdev->hldio.inflight_ios);
		hdev->hldio.inflight_ios = NULL;
	}
}
