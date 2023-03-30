// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */
#include <linux/vmalloc.h>

#include "nic.h"
#include "../common/habanalabs.h"

static int alloc_mem(struct hl_nic_mem_buf *buf, gfp_t gfp, struct hl_ctx *ctx,
			struct hl_nic_mem_data *mem_data)
{
	struct hl_nic_ctx *nic_ctx = &ctx->nic_ctx;
	struct hl_device *hdev = ctx->hdev;
	void *p = NULL;
	u64 device_addr, size = mem_data->size;
	u32 mem_id = mem_data->mem_id;

	switch (mem_id) {
	case HL_NIC_DRV_MEM_HOST_DMA_COHERENT:
		if (get_order(size) > MAX_ORDER) {
			dev_err(hdev->dev, "memory size 0x%llx must be less than 0x%lx\n",
				size, 1UL << (PAGE_SHIFT + MAX_ORDER - 1));
			return -ENOMEM;
		}

		p = hl_asic_dma_alloc_coherent(hdev, size, &buf->bus_address,
						GFP_USER | __GFP_ZERO);
		if (!p) {
			dev_err(hdev->dev,
				"failed to allocate 0x%llx of dma memory for the NIC\n", size);
			return -ENOMEM;
		}

		break;
	case HL_NIC_DRV_MEM_HOST_VIRTUAL:
		p = vmalloc_user(size);
		if (!p) {
			dev_err(hdev->dev,
				"failed to allocate vmalloc memory, size 0x%llx\n", size);
			return -ENOMEM;
		}

		break;
	case HL_NIC_DRV_MEM_HOST_MAP_ONLY:
		p = mem_data->in.host_map_data.kernel_address;
		buf->bus_address = mem_data->in.host_map_data.bus_address;
		break;
	case HL_NIC_DRV_MEM_DEVICE:
		device_addr = (u64) gen_pool_alloc(nic_ctx->wq_arrays_pool, size);
		if (!device_addr) {
			dev_err(hdev->dev, "Failed to allocate device memory, size 0x%llx\n", size);
			return -ENOMEM;
		}

		buf->device_addr = device_addr;
		break;
	default:
		dev_err(hdev->dev, "Invalid mem_id %d\n", mem_id);
		return -EINVAL;
	}

	buf->kernel_address = p;
	buf->mappable_size = size;

	return 0;
}

static int map_mem(struct hl_ctx *ctx, struct hl_nic_mem_buf *buf, struct hl_nic_mem_data *mem_data)
{
	int rc;

	if (mem_data->mem_id == HL_NIC_DRV_MEM_HOST_DMA_COHERENT) {
		dev_err(ctx->hdev->dev, "Mapping DMA coherent host memory is not yet supported\n");
		return -EPERM;
	}

	rc = hl_map_vmalloc_range(ctx, (u64) buf->kernel_address, mem_data->device_va,
					buf->mappable_size);
	if (rc)
		return rc;

	buf->device_va = mem_data->device_va;

	return 0;
}

static void mem_do_release(struct hl_device *hdev, struct hl_nic_mem_buf *buf)
{
	struct hl_ctx *ctx = buf->ctx;
	struct hl_nic_ctx *nic_ctx = &ctx->nic_ctx;

	if (buf->mem_id == HL_NIC_DRV_MEM_HOST_DMA_COHERENT)
		hl_asic_dma_free_coherent(hdev, buf->mappable_size, buf->kernel_address,
						buf->bus_address);
	else if (buf->mem_id == HL_NIC_DRV_MEM_HOST_VIRTUAL)
		vfree(buf->kernel_address);
	else if (buf->mem_id == HL_NIC_DRV_MEM_DEVICE)
		gen_pool_free(nic_ctx->wq_arrays_pool, buf->device_addr, buf->mappable_size);
}

static int __nic_mem_buf_alloc(struct hl_nic_mem_buf *buf, gfp_t gfp,
				struct hl_nic_mem_data *mem_data)
{
	struct hl_ctx *ctx = buf->ctx;
	struct hl_device *hdev = buf->hdev;
	int rc;

	if (mem_data->mem_id != HL_NIC_DRV_MEM_DEVICE)
		mem_data->size = PAGE_ALIGN(mem_data->size);

	rc = alloc_mem(buf, gfp, ctx, mem_data);
	if (rc)
		return rc;

	if (mem_data->device_va) {
		mem_data->device_va = PAGE_ALIGN(mem_data->device_va);
		rc = map_mem(ctx, buf, mem_data);
		if (rc)
			goto release_mem;
	}

	return 0;

release_mem:
	mem_do_release(hdev, buf);
	return rc;
}

static struct hl_nic_mem_buf *nic_mem_buf_alloc(struct hl_ctx *ctx, gfp_t gfp,
						struct hl_nic_mem_data *mem_data)
{
	struct xa_limit id_limit = XA_LIMIT(1, INT_MAX);
	struct hl_nic_ctx *nic_ctx = &ctx->nic_ctx;
	struct hl_device *hdev = ctx->hdev;
	struct hl_nic_mem_buf *buf;
	int rc;
	u32 id;

	buf = kzalloc(sizeof(*buf), gfp);
	if (!buf)
		return NULL;

	rc = xa_alloc(&nic_ctx->mem_ids, &id, buf, id_limit, GFP_ATOMIC);
	if (rc) {
		dev_err(hdev->dev,
			"Failed to allocate xarray for a new NIC buffer, rc=%d\n", rc);
		goto free_buf;
	}

	buf->hdev = hdev;
	buf->ctx = ctx;
	buf->mem_id = mem_data->mem_id;

	buf->handle = (((u64) id | HL_MMAP_TYPE_NIC_MEM) << PAGE_SHIFT);
	kref_init(&buf->refcount);

	rc = __nic_mem_buf_alloc(buf, gfp, mem_data);
	if (rc)
		goto remove_xa;

	return buf;

remove_xa:
	xa_erase(&nic_ctx->mem_ids, lower_32_bits(buf->handle >> PAGE_SHIFT));
free_buf:
	kfree(buf);
	return NULL;
}

static int nic_mem_alloc(struct hl_ctx *ctx, struct hl_nic_mem_data *mem_data)
{
	struct hl_nic_mem_buf *buf;

	buf = nic_mem_buf_alloc(ctx, GFP_KERNEL, mem_data);
	if (!buf)
		return -ENOMEM;

	mem_data->handle = buf->handle;

	if (mem_data->mem_id == HL_NIC_DRV_MEM_HOST_DMA_COHERENT)
		mem_data->addr = (u64) buf->bus_address;
	else if (mem_data->mem_id == HL_NIC_DRV_MEM_HOST_VIRTUAL)
		mem_data->addr = (u64) buf->kernel_address;
	else if (mem_data->mem_id == HL_NIC_DRV_MEM_DEVICE)
		mem_data->addr = (u64) buf->device_addr;

	return 0;
}

int hl_nic_mem_alloc(struct hl_ctx *ctx, struct hl_nic_mem_data *mem_data)
{
	struct hl_device *hdev = ctx->hdev;
	int rc;

	switch (mem_data->mem_id) {
	case HL_NIC_DRV_MEM_HOST_DMA_COHERENT:
	case HL_NIC_DRV_MEM_HOST_VIRTUAL:
	case HL_NIC_DRV_MEM_HOST_MAP_ONLY:
	case HL_NIC_DRV_MEM_DEVICE:
		rc = nic_mem_alloc(ctx, mem_data);
		break;
	default:
		dev_dbg(hdev->dev, "Invalid mem_id %d\n", mem_data->mem_id);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static void nic_mem_buf_destroy(struct hl_nic_mem_buf *buf)
{
	if (buf->device_va)
		hl_unmap_vmalloc_range(buf->ctx, buf->device_va);

	mem_do_release(buf->hdev, buf);

	kfree(buf);
}

int hl_nic_mem_destroy(struct hl_ctx *ctx, u64 handle)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_nic_mem_buf *buf;
	int rc;

	buf = hl_nic_mem_buf_get(ctx, handle);
	if (!buf) {
		dev_dbg(hdev->dev, "Memory destroy failed, no match for handle 0x%llx\n", handle);
		return -EINVAL;
	}

	rc = atomic_cmpxchg(&buf->is_destroyed, 0, 1);
	hl_nic_mem_buf_put(buf);
	if (rc) {
		dev_dbg(hdev->dev, "Memory destroy failed, handle 0x%llx was already destroyed\n",
			handle);
		return -EINVAL;
	}

	rc = hl_nic_mem_buf_put_handle(ctx, handle);
	if (rc < 0)
		return rc;

	if (rc == 0)
		dev_dbg(hdev->dev, "Handle 0x%llx is destroyed while still in use\n", handle);

	return 0;
}

static int nic_mem_buf_mmap(struct hl_nic_mem_buf *buf, struct vm_area_struct *vma)
{
	struct hl_device *hdev = buf->hdev;
	int rc = -EINVAL;

	if (buf->mem_id == HL_NIC_DRV_MEM_HOST_DMA_COHERENT ||
			buf->mem_id == HL_NIC_DRV_MEM_HOST_MAP_ONLY) {
		rc = hdev->asic_funcs->mmap(hdev, vma, buf->kernel_address, buf->bus_address,
						buf->mappable_size);
	} else if (buf->mem_id == HL_NIC_DRV_MEM_HOST_VIRTUAL) {
		vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY | VM_NORESERVE);

		rc = remap_vmalloc_range(vma, buf->kernel_address, 0);
	}

	return rc;
}

static void nic_mem_buf_vm_close(struct vm_area_struct *vma)
{
	struct hl_nic_mem_buf *buf = (struct hl_nic_mem_buf *) vma->vm_private_data;
	long new_mmap_size;

	new_mmap_size = buf->real_mapped_size - (vma->vm_end - vma->vm_start);

	if (new_mmap_size > 0) {
		buf->real_mapped_size = new_mmap_size;
		return;
	}

	atomic_set(&buf->mmap, 0);
	hl_nic_mem_buf_put(buf);
	vma->vm_private_data = NULL;
}

static const struct vm_operations_struct nic_mem_buf_vm_ops = {
	.close = nic_mem_buf_vm_close
};

int hl_nic_mem_mmap(struct hl_ctx *ctx, struct vm_area_struct *vma)
{
	struct hl_nic_mem_buf *buf;
	struct hl_device *hdev;
	u64 user_mem_size;
	u64 handle;
	int rc;

	hdev = ctx->hdev;

	/* We use the page offset to hold the xarray and thus we need to clear
	 * it before doing the mmap itself
	 */
	handle = vma->vm_pgoff << PAGE_SHIFT;
	vma->vm_pgoff = 0;

	/* Reference was taken here */
	buf = hl_nic_mem_buf_get(ctx, handle);
	if (!buf) {
		dev_err(hdev->dev,
			"NIC: Memory mmap failed, no match to handle %#llx\n", handle);
		return -EINVAL;
	}

	/* Validation check */
	user_mem_size = vma->vm_end - vma->vm_start;
	if (user_mem_size != ALIGN(buf->mappable_size, PAGE_SIZE)) {
		dev_err(hdev->dev,
			"NIC: Memory mmap failed, mmap VM size 0x%llx != 0x%llx allocated physical mem size\n",
			user_mem_size, buf->mappable_size);
		rc = -EINVAL;
		goto put_mem;
	}

#ifdef _HAS_TYPE_ARG_IN_ACCESS_OK
	if (!access_ok(VERIFY_WRITE, (void __user *)(uintptr_t)vma->vm_start,
		       user_mem_size)) {
#else
	if (!access_ok((void __user *)(uintptr_t)vma->vm_start, user_mem_size)) {
#endif
		dev_err(hdev->dev, "NIC: User pointer is invalid - 0x%lx\n", vma->vm_start);

		rc = -EINVAL;
		goto put_mem;
	}

	if (atomic_cmpxchg(&buf->mmap, 0, 1)) {
		dev_err(hdev->dev, "NIC: Memory mmap failed, already mapped to user\n");
		rc = -EINVAL;
		goto put_mem;
	}

	vma->vm_ops = &nic_mem_buf_vm_ops;

	/* Note: We're transferring the memory reference to vma->vm_private_data here. */

	vma->vm_private_data = buf;

	rc = nic_mem_buf_mmap(buf, vma);
	if (rc) {
		atomic_set(&buf->mmap, 0);
		goto put_mem;
	}

	buf->real_mapped_size = buf->mappable_size;
	vma->vm_pgoff = handle >> PAGE_SHIFT;

	return 0;

put_mem:
	hl_nic_mem_buf_put(buf);
	return rc;
}

static void nic_mem_buf_release(struct kref *kref)
{
	struct hl_nic_mem_buf *buf = container_of(kref, struct hl_nic_mem_buf, refcount);
	struct hl_nic_ctx *nic_ctx = &buf->ctx->nic_ctx;

	xa_erase(&nic_ctx->mem_ids, lower_32_bits(buf->handle >> PAGE_SHIFT));

	nic_mem_buf_destroy(buf);
}

struct hl_nic_mem_buf *hl_nic_mem_buf_get(struct hl_ctx *ctx, u64 handle)
{
	struct hl_nic_ctx *nic_ctx = &ctx->nic_ctx;
	struct hl_nic_mem_buf *buf;

	xa_lock(&nic_ctx->mem_ids);
	buf = xa_load(&nic_ctx->mem_ids, lower_32_bits(handle >> PAGE_SHIFT));
	if (!buf) {
		xa_unlock(&nic_ctx->mem_ids);
		dev_dbg(ctx->hdev->dev, "Buff get failed, no match to handle %#llx\n", handle);
		return NULL;
	}

	kref_get(&buf->refcount);
	xa_unlock(&nic_ctx->mem_ids);

	return buf;
}

int hl_nic_mem_buf_put(struct hl_nic_mem_buf *buf)
{
	return kref_put(&buf->refcount, nic_mem_buf_release);
}

static void nic_mem_buf_remove_xa_locked(struct kref *kref)
{
	struct hl_nic_mem_buf *buf = container_of(kref, struct hl_nic_mem_buf, refcount);

	__xa_erase(&buf->ctx->nic_ctx.mem_ids, lower_32_bits(buf->handle >> PAGE_SHIFT));
}

int hl_nic_mem_buf_put_handle(struct hl_ctx *ctx, u64 handle)
{
	struct hl_nic_ctx *nic_ctx = &ctx->nic_ctx;
	struct hl_nic_mem_buf *buf;

	xa_lock(&nic_ctx->mem_ids);
	buf = xa_load(&nic_ctx->mem_ids, lower_32_bits(handle >> PAGE_SHIFT));
	if (!buf) {
		xa_unlock(&nic_ctx->mem_ids);
		dev_dbg(ctx->hdev->dev, "Buff put failed, no match to handle %#llx\n", handle);
		return -EINVAL;
	}

	if (kref_put(&buf->refcount, nic_mem_buf_remove_xa_locked)) {
		xa_unlock(&nic_ctx->mem_ids);
		nic_mem_buf_destroy(buf);
		return 1;
	}

	xa_unlock(&nic_ctx->mem_ids);
	return 0;
}

void hl_nic_mem_init(struct hl_ctx *ctx)
{
	struct hl_nic_ctx *nic_ctx = &ctx->nic_ctx;

	xa_init_flags(&nic_ctx->mem_ids, XA_FLAGS_ALLOC);
}

void hl_nic_mem_fini(struct hl_ctx *ctx)
{
	struct hl_nic_ctx *nic_ctx = &ctx->nic_ctx;
	struct xarray *mem_ids;

	mem_ids = &nic_ctx->mem_ids;

	if (!xa_empty(mem_ids))
		dev_crit(ctx->hdev->dev, "NIC memory manager IDR is destroyed while it is not empty!\n");

	xa_destroy(mem_ids);
}
