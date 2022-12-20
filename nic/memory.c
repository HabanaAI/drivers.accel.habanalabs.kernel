// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 */
#include <linux/vmalloc.h>

#include "nic.h"
#include "../common/habanalabs.h"

struct nic_mem_buf_alloc_arg {
	struct hl_ctx *ctx;
	struct hl_nic_mem_data *mem_data;
};

static int nic_mem_buf_alloc(struct hl_mmap_mem_buf *buf, gfp_t gfp,
				    void *args);
static void nic_mem_buf_release(struct hl_mmap_mem_buf *buf);
static int nic_mem_buf_mmap(struct hl_mmap_mem_buf *buf,
			    struct vm_area_struct *vma, void *args);

static struct hl_mmap_mem_buf_behavior nic_behavior = {
	.topic = "NIC",
	.mem_id = HL_MMAP_TYPE_NIC_MEM,
	.alloc = nic_mem_buf_alloc,
	.release = nic_mem_buf_release,
	.mmap = nic_mem_buf_mmap,
};


static void mem_do_release(struct hl_device *hdev, struct hl_mmap_mem_buf *buf)
{
	struct hl_nic_mem *mem = buf->private;
	struct hl_ctx *ctx = mem->ctx;
	struct hl_nic_ctx *nic_ctx = &ctx->nic_ctx;

	if (mem->mem_id == HL_NIC_DRV_MEM_HOST_DMA_COHERENT)
		hl_asic_dma_free_coherent(hdev, buf->mappable_size, mem->kernel_address,
						mem->bus_address);
	else if (mem->mem_id == HL_NIC_DRV_MEM_HOST_VIRTUAL)
		vfree(mem->kernel_address);
	else if (mem->mem_id == HL_NIC_DRV_MEM_DEVICE)
		gen_pool_free(nic_ctx->wq_arrays_pool, mem->device_addr, buf->mappable_size);

	kfree(mem);
}

static struct hl_nic_mem *alloc_mem(struct hl_mmap_mem_buf *buf, gfp_t gfp, struct hl_ctx *ctx,
					struct hl_nic_mem_data *mem_data)
{
	struct hl_nic_ctx *nic_ctx = &ctx->nic_ctx;
	struct hl_device *hdev = ctx->hdev;
	struct hl_nic_mem *mem;
	void *p = NULL;
	u64 device_addr, size = mem_data->size;
	u32 mem_id = mem_data->mem_id;

	mem = kzalloc(sizeof(*mem), gfp);
	if (!mem)
		return NULL;

	switch (mem_id) {
	case HL_NIC_DRV_MEM_HOST_DMA_COHERENT:
		if (get_order(size) > MAX_ORDER) {
			dev_err(hdev->dev, "memory size 0x%llx must be less than 0x%lx\n",
				size, 1UL << (PAGE_SHIFT + MAX_ORDER - 1));
			goto free_mem;
		}

		p = hl_asic_dma_alloc_coherent(hdev, size, &mem->bus_address,
						GFP_USER | __GFP_ZERO);
		if (!p) {
			dev_err(hdev->dev,
				"failed to allocate 0x%llx of dma memory for the NIC\n", size);
			goto free_mem;
		}

		break;
	case HL_NIC_DRV_MEM_HOST_VIRTUAL:
		p = vmalloc_user(size);
		if (!p) {
			dev_err(hdev->dev,
				"failed to allocate vmalloc memory, size 0x%llx\n", size);
			goto free_mem;
		}

		break;
	case HL_NIC_DRV_MEM_HOST_MAP_ONLY:
		p = mem_data->in.host_map_data.kernel_address;
		mem->bus_address = mem_data->in.host_map_data.bus_address;
		break;
	case HL_NIC_DRV_MEM_DEVICE:
		device_addr = (u64) gen_pool_alloc(nic_ctx->wq_arrays_pool, size);
		if (!device_addr) {
			dev_err(hdev->dev, "Failed to allocate device memory, size 0x%llx\n", size);
			goto free_mem;
		}

		mem->device_addr = device_addr;
		break;
	default:
		dev_err(hdev->dev, "Invalid mem_id %d\n", mem_id);
		goto free_mem;
	}

	mem->kernel_address = p;
	buf->mappable_size = size;
	buf->private = mem;

	return mem;
free_mem:
	kfree(mem);
	return NULL;
}

static int map_mem(struct hl_ctx *ctx, struct hl_mmap_mem_buf *buf,
		   struct hl_nic_mem_data *mem_data)
{
	struct hl_nic_mem *mem = buf->private;
	int rc;

	if (mem_data->mem_id == HL_NIC_DRV_MEM_HOST_DMA_COHERENT) {
		dev_err(ctx->hdev->dev, "Mapping DMA coherent host memory is not yet supported\n");
		return -EPERM;
	}

	rc = hl_map_vmalloc_range(ctx, (u64)mem->kernel_address,
				  mem_data->device_va, buf->mappable_size);
	if (rc)
		return rc;

	mem->device_va = mem_data->device_va;

	return 0;
}

static void nic_mem_buf_release(struct hl_mmap_mem_buf *buf)
{
	struct hl_device *hdev;
	struct hl_nic_mem *mem = buf->private;

	mem = buf->private;
	hdev = mem->hdev;

	if (mem->device_va)
		hl_unmap_vmalloc_range(mem->ctx, mem->device_va);

	mem_do_release(hdev, buf);
}

static int nic_mem_buf_alloc(struct hl_mmap_mem_buf *buf, gfp_t gfp, void *args)
{
	struct nic_mem_buf_alloc_arg *alloc_args = args;
	struct hl_ctx *ctx = alloc_args->ctx;
	struct hl_device *hdev = ctx->hdev;
	struct hl_nic_mem_data *mem_data = alloc_args->mem_data;
	struct hl_nic_mem *mem;
	int rc;

	if (mem_data->mem_id != HL_NIC_DRV_MEM_DEVICE)
		mem_data->size = PAGE_ALIGN(mem_data->size);

	mem = alloc_mem(buf, gfp, ctx, mem_data);
	if (!mem) {
		rc = -ENOMEM;
		goto out_err;
	}

	mem->hdev = hdev;
	mem->ctx = ctx;
	mem->mem_id = mem_data->mem_id;

	buf->private = mem;

	if (mem_data->device_va) {
		mem_data->device_va = PAGE_ALIGN(mem_data->device_va);
		rc = map_mem(ctx, buf, mem_data);
		if (rc)
			goto release_mem;
	}

	return 0;

release_mem:
	mem_do_release(hdev, buf);
out_err:
	return rc;
}

static int nic_mem_buf_mmap(struct hl_mmap_mem_buf *buf,
			    struct vm_area_struct *vma, void *args)
{
	struct hl_nic_mem *mem = buf->private;
	struct hl_device *hdev = mem->hdev;
	int rc = -EINVAL;

	if (mem->mem_id == HL_NIC_DRV_MEM_HOST_DMA_COHERENT ||
		mem->mem_id == HL_NIC_DRV_MEM_HOST_MAP_ONLY) {
		rc = hdev->asic_funcs->mmap(hdev, vma, mem->kernel_address,
						mem->bus_address, buf->mappable_size);
	} else if (mem->mem_id == HL_NIC_DRV_MEM_HOST_VIRTUAL) {
		vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY | VM_NORESERVE;

		rc = remap_vmalloc_range(vma, mem->kernel_address, 0);
	}

	return rc;
}

static int nic_mem_alloc(struct hl_ctx *ctx, struct hl_nic_mem_data *mem_data)
{
	struct hl_mem_mgr *mmg = &ctx->hpriv->mem_mgr;
	struct hl_mmap_mem_buf *buf;
	struct hl_nic_mem *mem;
	struct nic_mem_buf_alloc_arg args = {
		.ctx = ctx,
		.mem_data = mem_data
	};

	buf = hl_mmap_mem_buf_alloc(mmg, &nic_behavior, GFP_KERNEL, &args);
	if (!buf)
		return -ENOMEM;

	mem = buf->private;

	mem_data->handle = buf->handle;

	if (mem_data->mem_id == HL_NIC_DRV_MEM_HOST_DMA_COHERENT)
		mem_data->addr = (u64) mem->bus_address;
	else if (mem_data->mem_id == HL_NIC_DRV_MEM_HOST_VIRTUAL)
		mem_data->addr = (u64) mem->kernel_address;
	else if (mem_data->mem_id == HL_NIC_DRV_MEM_DEVICE)
		mem_data->addr = (u64) mem->device_addr;

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

int hl_nic_mem_destroy(struct hl_ctx *ctx, u64 handle)
{
	struct hl_mem_mgr *mmg = &ctx->hpriv->mem_mgr;
	struct hl_mmap_mem_buf *buf;
	struct hl_nic_mem *mem;
	int rc;

	buf = hl_mmap_mem_buf_get(mmg, handle);
	if (!buf) {
		dev_dbg(mmg->dev, "Memory destroy failed, no match for handle 0x%llx\n", handle);
		return -EINVAL;
	}

	mem = buf->private;

	rc = atomic_cmpxchg(&mem->is_destroyed, 0, 1);
	hl_mmap_mem_buf_put(buf);
	if (rc) {
		dev_dbg(mmg->dev, "Memory destroy failed, handle 0x%llx was already destroyed\n",
			handle);
		return -EINVAL;
	}

	rc = hl_mmap_mem_buf_put_handle(mmg, handle);
	if (rc < 0)
		return rc;

	if (rc == 0)
		dev_dbg(mmg->dev, "Handle 0x%llx is destroyed while still in use\n", handle);

	return 0;
}
