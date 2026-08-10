// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2024 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"
#include <generated/uapi/linux/version.h>
#include <linux/pci-p2pdma.h>
#include <linux/blkdev.h>
#include <linux/overflow.h>
#include <linux/vmalloc.h>

#define HL_DIO_MAX_BVEC_PAGES	min_t(u64, (u64)INT_MAX, (u64)(SIZE_MAX / sizeof(struct bio_vec)))

/*
 * This file is for NVME POC, So no aim to make it perfect, It just should work!
 *
 * MY ASSUMPTIONS
 * ==============
 * 1. No IOMMU (well, technically it can work with IOMMU, but it is *almost useless).
 * 2. Only READ operations (can extend in the future).
 * 3. No sparse files (can overcome this in the future).
 * 4. Kernel version >= 6.9
 * 5. Requiring page alignment is OK (I don't see a solution to this one right,
 *    now, how do we read partial pages?)
 * 6. Kernel compiled with CONFIG_PCI_P2PDMA. This requires a CUSTOM kernel.
 *    Theoretically I have a slight idea on how this could be solvable, but it
 *    is probably inacceptable for the upstream. Also may not work in the end.
 * 7. Either make sure our cards and disks are under the same PCI bridge, or
 *    compile a custom kernel to hack around this.
 */

#define IO_STABILIZE_TIMEOUT 10000000

/*
 * This struct contains all the useful data I could milk out of the file handle
 * provided by the user.
 * @TODO: right now it is retrieved on each IO, but can be done once with some
 * dedicated IOCTL, call it for example HL_REGISTER_HANDLE.
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

static int hl_dio_fd_register(struct hl_ctx *ctx, int fd, struct hl_dio_fd *f)
{
	struct hl_device *hdev = ctx->hdev;
	struct block_device *bd;
	struct super_block *sb;
	struct inode *inode;
	struct gendisk *gd;
	int rc;

	f->filp = fget(fd);
	if (!f->filp) {
		rc = -ENOENT;
		goto out;
	}

	if (!(f->filp->f_flags & O_DIRECT)) {
		hl_err(hdev, "File is not in the direct mode\n");
		rc = -EINVAL;
		goto fput;
	}

	if (!f->filp->f_op->read_iter) {
		hl_err(hdev, "Read iter is not supported, need to fall back to legacy\n");
		rc = -EINVAL;
		goto fput;
	}

	inode = file_inode(f->filp);
	sb = inode->i_sb;
	bd = sb->s_bdev;
	gd = bd->bd_disk;

	if (inode->i_blocks << sb->s_blocksize_bits < i_size_read(inode)) {
		hl_err(hdev, "Sparse files are not currently supported\n");
		rc = -EINVAL;
		goto fput;
	}

	if (sb->s_magic != EXT4_SUPER_MAGIC) {
		hl_err(hdev, "Only ext4 filesystem is supported\n");
		rc = -EINVAL;
		goto fput;
	}

	if (strncmp("nvme", gd->disk_name, 4)) {
		hl_err(hdev, "The physical disk is not an NVME disk\n");
		rc = -EINVAL;
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
	u64 end;
	u64 npages;

	if (!io->len_bytes) {
		hl_dbg(hdev, "IO length must be non-zero\n");
		return false;
	}

	npages = io->len_bytes >> PAGE_SHIFT;

	if ((u64)io->device_va & ~PAGE_MASK) {
		hl_dbg(hdev, "Device address must be page aligned\n");
		return false;
	}

	if (io->len_bytes & ~PAGE_MASK) {
		hl_dbg(hdev, "IO length must be page aligned\n");
		return false;
	}

	if (io->off_bytes & ~PAGE_MASK) {
		hl_dbg(hdev, "IO offset must be page aligned\n");
		return false;
	}

	if (io->off_bytes > LLONG_MAX) {
		hl_dbg(hdev, "IO offset %#llx exceeds loff_t range\n", io->off_bytes);
		return false;
	}

	if (check_add_overflow(io->off_bytes, io->len_bytes, &end)) {
		hl_dbg(hdev, "IO range start=%#llx len=%#llx overflows u64\n",
		       io->off_bytes, io->len_bytes);
		return false;
	}

	if (end > LLONG_MAX) {
		hl_dbg(hdev, "IO range end %#llx exceeds loff_t range\n", end);
		return false;
	}

	if (npages > HL_DIO_MAX_BVEC_PAGES) {
		hl_dbg(hdev, "IO length %#llx exceeds max supported %#llx\n",
		       io->len_bytes, HL_DIO_MAX_BVEC_PAGES << PAGE_SHIFT);
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
		hl_err(hdev, "Device virtual address translation error: %#llx (%d)",
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
		hl_info(hdev, "Can't schedule a new IO, IO is disabled\n");
		return -ESHUTDOWN;
	}

	init_sync_kiocb(&io->kio, io->f.filp);
	io->kio.ki_pos = io->off_bytes;

	npages = (io->len_bytes >> PAGE_SHIFT);

	/* @TODO: this can be implemented smarter, vmalloc in iopath is not
	 * ideal. Maybe some variation of genpool. Number of pages may differ
	 * greatly, so maybe even use pools of different sizes and chose the
	 * closest one.
	 */
	io->bv = vzalloc(npages * sizeof(struct bio_vec));
	if (!io->bv) {
		rc = -ENOMEM;
		goto cleanup;
	}

	for (i = 0, device_va = io->device_va; i < npages ; ++i, device_va += PAGE_SIZE) {
		io->bv[i].bv_page = hl_dio_va2page(hdev, io->f.ctx, device_va);
		if (!io->bv[i].bv_page) {
			hl_err(hdev, "Error getting page struct for device va %#llx",
					device_va);
			rc = -EFAULT;
			goto cleanup;
		}
		io->bv[i].bv_offset = 0;
		io->bv[i].bv_len = PAGE_SIZE;
	}

	iov_iter_bvec(&io->iter, io->type, io->bv, npages, io->len_bytes);
	rc = io->f.filp->f_op->read_iter(&io->kio, &io->iter);

cleanup:
	vfree(io->bv); /* @TODO: skip this label in async IO */
	hl_dio_put_iopath(io->f.ctx);

	hl_dbg(hdev, "IO ended with %ld\n", rc);

	return rc;
}

/*
 * @TODO: This function can be used as a callback for io completion under
 * kio->ki_complete in order to implement async IO.
 * Note that on more recent kernels there is no ret2.
 */
__maybe_unused static void hl_direct_io_complete(struct kiocb *kio, long ret, long ret2)
{
	struct hl_direct_io *io = container_of(kio, struct hl_direct_io, kio);

	hl_dbg(io->f.ctx->hdev, "IO completed with %ld\n", ret);

	/* Do something to copy result to user / notify completion */

	hl_dio_put_iopath(io->f.ctx);

	hl_dio_fd_unregister(&io->f);
}

/*
 * DMA disk to ASIC, wait for results. Must be invoked from the user context
 */
int hl_dio_ssd2hl(struct hl_device *hdev, struct hl_ctx *ctx, int fd, u64 device_va,
		off_t off_bytes, size_t len_bytes, size_t *len_read)
{
	struct hl_direct_io *io;
	ssize_t rc;

	hl_dbg(hdev, "SSD2HL fd=%d va=%#llx len=%#lx\n", fd, device_va, len_bytes);

	/* TODO: This allocation should be done from a genpool */
	io = kzalloc(sizeof(*io), GFP_KERNEL);
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
		hl_dbg(hdev, "Freeing P2P mem from %px, size=%#llx\n",
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
		hl_err(hdev, "Error adding p2p resource: %d\n", rc);
		goto err;
	}

	/* Alloc all p2p mem */
	p2pr->p2pmem = pci_alloc_p2pmem(hdev->pdev, p2pr->size);
	if (!p2pr->p2pmem) {
		hl_err(hdev, "Error allocating p2p memory\n");
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

/* Placeholder for now */
int hl_dio_start(struct hl_device *hdev)
{
	hl_dbg(hdev, "Initializing HLDIO\n");

	/* Initialize the IO counter and enable IO */
	hdev->hldio.inflight_ios = alloc_percpu(s64);
	if (!hdev->hldio.inflight_ios)
		return -ENOMEM;

	hl_dio_set_io_enabled(hdev, true);

	return 0;
}

/* Placeholder for now */
void hl_dio_stop(struct hl_device *hdev)
{
	hl_dbg(hdev, "Deinitializing HLDIO\n");

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
