// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022-2024 HabanaLabs, Ltd.
 * Copyright (C) 2024-2025, Intel Corporation.
 * All Rights Reserved.
 */

#include <generated/uapi/linux/version.h>

#include "habanalabs.h"

/*
 * Due to several major redesigns of the mmu interval notifier and HMM in
 * recent kernel releases, it was decided to begin support from kernel 5.5
 * with mmu_interval_read_begin interface defined.
 */
#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE && \
	defined(_HAS_MMU_INTERVAL_READ_BEGIN) && \
	defined(_HAS_HMM_RANGE_INTERVAL_NOTIFIER)

#include <linux/mmu_notifier.h>
#include <linux/hmm.h>

#define MAX_PAGE_IN_RETRIES 10

#define RA_SIMPLE_FORWARD 96
#define RA_SIMPLE_BACKWARD 32

static_assert(RA_SIMPLE_FORWARD > 0);
static_assert(RA_SIMPLE_BACKWARD >= 0);

#ifdef _HAS_MMU_RANGE_FLAGS
/* Note: older kernel */
static const uint64_t hmm_range_flags[HMM_PFN_FLAG_MAX] = {
	(1 << 0), /* HMM_PFN_VALID */
	(1 << 1), /* HMM_PFN_WRITE */
#ifdef _HAS_HMM_PFN_DEVICE_PRIVATE
	(1 << 2) /* HMM_PFN_DEVICE_PRIVATE */
#endif
};
static const uint64_t hmm_range_values[HMM_PFN_VALUE_MAX] = {
	0xfffffffffffffffeULL, /* HMM_PFN_ERROR */
	0, /* HMM_PFN_NONE */
	0xfffffffffffffffcULL /* HMM_PFN_SPECIAL */
};
#define HMM_PFN_REQ_FAULT hmm_range_flags[HMM_PFN_VALID]
#define HMM_PFN_REQ_WRITE hmm_range_flags[HMM_PFN_WRITE]
#endif

/**
 * struct hl_odp_region_ctx - odp context descriptor
 *
 * @notifier: MMU notifier object, to get updates on host side page events
 * @userptr: Back pointer to the owner userptr object
 * @ctx: Back pointer to the owner habanalabs device object context
 * @pt: dma-mapped page addresses table
 * @device_vaddr: device memory space address corresponding to the region start
 */
struct hl_odp_region_ctx {
	struct mmu_interval_notifier notifier;
	struct hl_userptr *userptr;
	struct hl_ctx *ctx;
	struct xarray pt;
	u64 device_vaddr;
};

static bool odp_invalidate_handler(struct mmu_interval_notifier *,
				   const struct mmu_notifier_range *,
				   unsigned long);

static const struct mmu_interval_notifier_ops odp_notifier_ops = {
	.invalidate = odp_invalidate_handler,
};

bool hl_is_odp_supported(struct hl_device *hdev)
{
	return hdev->asic_prop.supports_odp;
}

/**
 * hl_odp_page_fault_read_ahead_size - given an odp region and a virtual address
 * inside it, calculate how many pages shall be paged in.
 *
 * @rg: odp region pointer
 * @va: virtual address of the page fault
 * @ra_start_va: start virtual address of read ahead
 * @ra_npages: number of pages to read ahead
 *
 */
void hl_odp_page_fault_read_ahead_size(struct hl_odp_region_ctx *rg, u64 va,
				       u64 *ra_start_va, u32 *ra_npages)
{
	u64 start_page, region_npages, npages;

	start_page = (va - round_down(rg->device_vaddr, PAGE_SIZE)) >> PAGE_SHIFT;
	region_npages =
		(round_up(rg->device_vaddr + rg->userptr->size, PAGE_SIZE) -
		 round_down(rg->device_vaddr, PAGE_SIZE)) >>
		PAGE_SHIFT;

	if (start_page >= RA_SIMPLE_BACKWARD) {
		start_page -= RA_SIMPLE_BACKWARD;
		npages = RA_SIMPLE_BACKWARD;
	} else {
		npages = start_page;
		start_page = 0;
	}

	if (start_page + npages + RA_SIMPLE_FORWARD > region_npages)
		npages = region_npages - start_page;
	else
		npages += RA_SIMPLE_FORWARD;

	*ra_start_va = round_down(rg->device_vaddr, PAGE_SIZE) + (start_page << PAGE_SHIFT);
	*ra_npages = npages;
}

/**
 * hl_odp_set_region_va_data - set habanalabs context and va region start data
 * for the given initialized region.
 *
 * @rg: odp region pointer
 * @ctx: habanalabs user context pointer
 * @device_vaddr: starting virtual address reserved for the region
 */
void hl_odp_set_region_va_data(struct hl_odp_region_ctx *rg, struct hl_ctx *ctx,
			       u64 device_vaddr)
{
	rg->ctx = ctx;
	rg->device_vaddr = device_vaddr;
}

/**
 * hl_odp_region_ctx_find - find odp region containing given virtual address
 *
 * @ctx: habanalabs user context pointer
 * @vaddr: target virtual address
 *
 * Return region object if found, or NULL otherwise.
 */
struct hl_odp_region_ctx *hl_odp_region_ctx_find(struct hl_ctx *ctx, u64 vaddr)
{
	struct hl_vm_hash_node *hnode;
	struct hl_userptr *userptr;
	enum vm_type *vm_type;
	u64 start, end;
	int i;

	/*
	 * TODO: [SW-220012] Rather than iterating all possible regions, it is better to
	 * maintain an interval tree for efficient search.
	 */

	mutex_lock(&ctx->mem_hash_lock);
	hash_for_each(ctx->mem_hash, i, hnode, node) {
		vm_type = hnode->ptr;
		if (*vm_type != VM_TYPE_USERPTR)
			continue;
		userptr = hnode->ptr;
		if (!userptr->is_odp)
			continue;
		start = round_down(hnode->vaddr, PAGE_SIZE);
		end = round_up(hnode->vaddr + userptr->size,
				PAGE_SIZE);
		if (vaddr >= start && vaddr < end) {
			mutex_unlock(&ctx->mem_hash_lock);
			/*
			 * if odp_rg is null here, it means that vaddr was mapped without odp
			 * support. So we are not suppose to get a page fault and search for
			 * the odp range
			 */
			WARN_ON_ONCE(!userptr->odp_rg);
			return userptr->odp_rg;
		}
	}
	mutex_unlock(&ctx->mem_hash_lock);

	return NULL;
}

/**
 * hl_odp_region_ctx_create - Allocate and initialize the odp region
 *
 * @hdev: pointer to device data
 * @userptr: owner userptr object pointer
 *
 * Initializes the odp region, assume running from the user context. Returns the
 * pointer to the object on success or NULL on error.
 */
struct hl_odp_region_ctx *hl_odp_region_ctx_create(struct hl_device *hdev,
						   struct hl_userptr *userptr)
{
	struct hl_odp_region_ctx *rg = NULL;
	u64 addr, size;
	int rc;

	addr = round_down(userptr->addr, PAGE_SIZE);
	size = round_up(userptr->addr + userptr->size, PAGE_SIZE) -
	       round_down(userptr->addr, PAGE_SIZE);

	rg = kzalloc(sizeof(*rg), GFP_KERNEL);
	if (unlikely(!rg))
		goto out;

	xa_init(&rg->pt);

	rg->userptr = userptr;

	rc = mmu_interval_notifier_insert(&rg->notifier, current->mm,
					  addr, size, &odp_notifier_ops);

	if (unlikely(rc)) {
		hl_err(hdev,
			"Error trying to setup interval notifier for region %#llx of size %#llx: %d",
			addr, size, rc);
		goto free_rg;
	}

	goto out;

free_rg:
	xa_destroy(&rg->pt);
	kfree(rg);
	rg = NULL;
out:
	return rg;
}

/**
 * hl_odp_region_ctx_destroy - Deinitialize and free the odp region
 *
 * @rg: odp region
 *
 * Deinitialize the odp region and free the memory.
 */
void hl_odp_region_ctx_destroy(struct hl_odp_region_ctx *rg)
{
	struct hl_ctx *ctx = rg->ctx;
	struct hl_device *hdev;
	dma_addr_t dma_addr;
	void *entry;
	u64 device_addr;
	unsigned long va_pfn;
	int rc;

	mmu_interval_notifier_remove(&rg->notifier);

	if (!ctx)
		goto no_ctx_yet;

	hdev = ctx->hdev;

	xa_for_each(&rg->pt, va_pfn, entry) {

		device_addr = (va_pfn << PAGE_SHIFT);
		dma_addr = xa_to_value(entry);
		rc = hl_mmu_unmap_page(ctx, device_addr, PAGE_SIZE, true);
		if (rc)
			hl_err(hdev,
				"Error while unmapping %#llx (device_addr=%#llx): %d",
				dma_addr, device_addr, rc);
		hl_dma_unmap_page(hdev, dma_addr, PAGE_SIZE, rg->userptr->dir);
		xa_erase(&rg->pt, va_pfn);
	}

no_ctx_yet:
	xa_destroy(&rg->pt);
	kfree(rg);
}

/**
 * odp_mmu_update_page_in_locked - update PMMU with new pages available
 *
 * @rg: region pointer
 * @pfns: array of @npages PFNs describing the new pages
 * @start_page: first page index to page in
 * @npages: number of pages
 *
 * Assuming new pages are already swapped in on the host, update the device
 * PMMU with the new page addresses.
 */
static int odp_mmu_update_page_in_locked(struct hl_odp_region_ctx *rg,
				  unsigned long *pfns, u64 start_page,
				  u64 npages)
{
	struct hl_device *hdev = rg->ctx->hdev;
	struct hl_ctx *ctx = rg->ctx;
	u64 addr, start_device_addr, device_addr;
	dma_addr_t dma_addr;
	unsigned long va_pfn;
	void *entry;
	int rc, i;

	addr = round_down(rg->userptr->addr, PAGE_SIZE) +
	       start_page * PAGE_SIZE;
	start_device_addr = round_down(rg->device_vaddr, PAGE_SIZE) + start_page * PAGE_SIZE;
	device_addr = start_device_addr;

	for (i = 0; i < npages;
	     ++i, device_addr += PAGE_SIZE, addr += PAGE_SIZE) {
		va_pfn = device_addr >> PAGE_SHIFT;
		entry = xa_load(&rg->pt, va_pfn);

		if (entry)
			continue;

		if (unlikely(xa_reserve(&rg->pt, va_pfn, GFP_KERNEL))) {
			rc = -ENOMEM;
			goto rollback;
		}

		dma_addr = hl_dma_map_page(hdev, pfn_to_page(pfns[i]), 0, PAGE_SIZE,
						rg->userptr->dir);
		if (unlikely(!dma_addr)) {
			xa_release(&rg->pt, va_pfn);
			rc = -ENOMEM;
			goto rollback;
		}
		xa_store(&rg->pt, va_pfn, xa_mk_value(dma_addr), GFP_KERNEL);

		rc = hl_mmu_map_page(ctx, device_addr, dma_addr, PAGE_SIZE,
				     (i + 1) == npages);
		if (unlikely(rc)) {
			xa_erase(&rg->pt, va_pfn);
			hl_dma_unmap_page(hdev, dma_addr, PAGE_SIZE, rg->userptr->dir);
			goto rollback;
		}
	}

	/* Last, invalidate cache */
	rc = hl_mmu_invalidate_cache_range(
		hdev, false, MMU_OP_USERPTR | MMU_OP_SKIP_LOW_CACHE_INV,
		ctx->asid, start_device_addr,
		npages * PAGE_SIZE);

	return rc;

rollback:
	for (i = i - 1, device_addr = device_addr - PAGE_SIZE; i >= 0;
	     --i, device_addr -= PAGE_SIZE) {
		va_pfn = device_addr >> PAGE_SHIFT;
		entry = xa_load(&rg->pt, va_pfn);
		if (!entry)
			continue;
		dma_addr = xa_to_value(entry);
		hl_mmu_unmap_page(ctx, device_addr, PAGE_SIZE, i == 0);
		hl_dma_unmap_page(hdev, dma_addr, PAGE_SIZE, rg->userptr->dir);
		xa_erase(&rg->pt, va_pfn);
	}
	return rc;
}

/**
 * odp_resolve_pfns - normalize and remove hmm flags from pfns
 *
 * @range: target hmm_range struct
 * @hdev: habanalabs device pointer
 *
 * Resolve the pfns represented by given hmm range
 */
static int odp_resolve_pfns(struct hmm_range *range, struct hl_device *hdev)
{
	struct page *page;
	u32 npages = (range->end - range->start) >> PAGE_SHIFT;
	unsigned long *pfns;
	int i;

#ifdef _HAS_HMM_RANGE_HMM_PFNS
	/* Note: upstream kernel */
	pfns = (unsigned long *)range->hmm_pfns;
#else
	/* Note: older kernel */
	pfns = (unsigned long *)range->pfns;
#endif

	for (i = 0; i < npages; ++i) {
		if (!(pfns[i] & HMM_PFN_VALID))
			return -EFAULT;
		page = hmm_pfn_to_page(pfns[i]);
		if (unlikely(!page)) {
			hl_err(hdev, "Invalid page for PFN %lx\n", pfns[i]);
			return -EFAULT;
		}
		pfns[i] = page_to_pfn(page);
	}
	return 0;
}

/**
 * odp_mmu_update_page_out_locked - update PMMU with removed pages
 *
 * @rg: region pointer
 * @start: range start
 * @npages: number of pages
 *
 * Updates PMMU with invalidated pages in a given range.
 * Returns true on successfully updating the PMMU, false on a failure for any
 * reason.
 */
static bool odp_mmu_update_page_out_locked(struct hl_odp_region_ctx *rg, u64 start,
				    u64 npages)
{
	struct hl_device *hdev = rg->ctx->hdev;
	struct hl_ctx *ctx = rg->ctx;
	unsigned long va_pfn, start_va_pfn, last_va_pfn;
	u64 device_addr, addr_aligned;
	u32 start_page;
	dma_addr_t dma_addr;
	bool erased = false;
	void *entry;
	int rc;

	addr_aligned = round_down(rg->userptr->addr, PAGE_SIZE);
	start_page = (start - addr_aligned) >> PAGE_SHIFT;
	start_va_pfn = (rg->device_vaddr >> PAGE_SHIFT) + start_page;
	last_va_pfn = start_va_pfn + npages - 1;

	xa_for_each_range(&rg->pt, va_pfn, entry, start_va_pfn, last_va_pfn) {
		dma_addr = xa_to_value(entry);
		device_addr = (va_pfn << PAGE_SHIFT);
		rc = hl_mmu_unmap_page(ctx, device_addr, PAGE_SIZE, true);
		if (rc) {
			hl_err(hdev,
				"Error while unmapping %#llx (device_addr=%#llx): %d",
				dma_addr, device_addr, rc);
			return false;
		}
		erased = true;
	}

	/* It is possible invalidation was called on non mapped pages */
	if (!erased)
		return true;

	rc = hl_mmu_invalidate_cache_range(hdev, false, MMU_OP_USERPTR,
					   ctx->asid, start_va_pfn * PAGE_SIZE,
					   npages * PAGE_SIZE);
	if (rc)
		return false;

	xa_for_each_range(&rg->pt, va_pfn, entry, start_va_pfn, last_va_pfn) {
		dma_addr = xa_to_value(entry);
		hl_dma_unmap_page(hdev, dma_addr, PAGE_SIZE, rg->userptr->dir);
		xa_erase(&rg->pt, va_pfn);
	}

	return true;
}

/**
 * hl_odp_page_in - page in sub range of odp region
 *
 * @rg: region pointer
 * @start_va: page in start address in device memory space
 * @npages: number of pages
 *
 * Performs page in operation on all pages between start and end in the given
 * odp region.
 */
int hl_odp_page_in(struct hl_odp_region_ctx *rg, u64 start_va, u64 npages)
{
	struct hl_device *hdev = rg->ctx->hdev;
	struct hmm_range range;
	unsigned long *pfns;
	u64 start, end;
	u64 start_page;
	int rc, retries = MAX_PAGE_IN_RETRIES;

	pfns = kvmalloc_array(npages, sizeof(*pfns), GFP_KERNEL);
	if (unlikely(!pfns))
		return -ENOMEM;

	start_page = (start_va - round_down(rg->device_vaddr, PAGE_SIZE)) >>
		     PAGE_SHIFT;

	start = round_down(rg->userptr->addr, PAGE_SIZE) +
		start_page * PAGE_SIZE;
	end = start + npages * PAGE_SIZE;

#ifdef _HAS_MMU_RANGE_FLAGS
	/* Note: older kernel */
	range.flags = hmm_range_flags;
	range.values = hmm_range_values;
	range.pfn_shift = PAGE_SHIFT;
#endif

#ifdef _HAS_HMM_RANGE_HMM_PFNS
	/* Note: upstream kernel */
	range.hmm_pfns = pfns;
#else
	/* Note: older kernel */
	range.pfns = (u64 *)pfns;
#endif

	range.default_flags = HMM_PFN_REQ_FAULT;
	if (rg->userptr->dir == DMA_BIDIRECTIONAL ||
	    rg->userptr->dir == DMA_FROM_DEVICE)
		range.default_flags |= HMM_PFN_REQ_WRITE;

	range.notifier = &rg->notifier;
	range.start = start;
	range.end = end;

	/* Note: mmgrab is a must here, assuming held by notifier at region ctx create */

	/* Please refer to Documentation/vm/hmm.rst */
again:
	range.notifier_seq = mmu_interval_read_begin(&rg->notifier);
	mmap_read_lock(rg->notifier.mm);
#ifdef _HAS_HMM_RANGE_FAULT_FLAGS
	/* Note: older kernel */
	rc = hmm_range_fault(&range, 0);
#else
	/* Note: upstream kernel */
	rc = hmm_range_fault(&range);
#endif
	if (unlikely(rc < 0)) {
		mmap_read_unlock(rg->notifier.mm);
		if (rc == -EBUSY) {
			if (!retries--)
				rc = -ETIMEDOUT;
			else
				goto again;
		}
		kvfree(pfns);
		return rc;
	}
	mmap_read_unlock(rg->notifier.mm);

	mutex_lock(&hdev->mmu_lock);

	if (mmu_interval_read_retry(&rg->notifier, range.notifier_seq)) {
		mutex_unlock(&hdev->mmu_lock);
		if (!retries--) {
			kvfree(pfns);
			return -ETIMEDOUT;
		}
		goto again;
	}

	rc = odp_resolve_pfns(&range, hdev);
	if (rc)
		goto free_pfns;

	rc = odp_mmu_update_page_in_locked(rg, pfns, start_page, npages);

free_pfns:
	kvfree(pfns);

	mutex_unlock(&hdev->mmu_lock);

	return rc;
}

/**
 * odp_invalidate_handler - callback to notify about mm change
 *
 * @notifier: the range (mm) is about to update
 * @range: details on the invalidation
 * @cur_seq: Value to pass to mmu_interval_set_seq()
 */
static bool odp_invalidate_handler(struct mmu_interval_notifier *notifier,
				   const struct mmu_notifier_range *range,
				   unsigned long cur_seq)
{
	struct hl_odp_region_ctx *rg =
		container_of(notifier, struct hl_odp_region_ctx, notifier);
	struct hl_device *hdev = rg->ctx->hdev;
	u64 start, npages;
	bool res;

	/* Please refer to Documentation/vm/hmm.rst */

	start = round_down(range->start, PAGE_SIZE);
	npages = (round_up(range->end, PAGE_SIZE) - start) >> PAGE_SHIFT;

	/*
	 * Why in this callback we can assume ctx exists and valid:
	 * - If we are here notifier is not destroyed
	 * - Hence, region is not destroyed
	 * - Hence, at least some memory is still mapped
	 * - Hence ctx object is not destroyed yet
	 */
	if (mmu_notifier_range_blockable(range))
		mutex_lock(&hdev->mmu_lock);
	else if (!mutex_trylock(&hdev->mmu_lock))
		return false;

	mmu_interval_set_seq(notifier, cur_seq);

	res = odp_mmu_update_page_out_locked(rg, start, npages);

	mutex_unlock(&hdev->mmu_lock);
	return res;
}

#else

bool hl_is_odp_supported(struct hl_device *hdev)
{
	return false;
}

void hl_odp_page_fault_read_ahead_size(struct hl_odp_region_ctx *rg, u64 va,
				       u64 *ra_start_va, u32 *ra_npages)
{
}
void hl_odp_set_region_va_data(struct hl_odp_region_ctx *rg, struct hl_ctx *ctx,
			       u64 device_vaddr)
{
}
struct hl_odp_region_ctx *hl_odp_region_ctx_find(struct hl_ctx *ctx, u64 vaddr)
{
	return NULL;
}
struct hl_odp_region_ctx *hl_odp_region_ctx_create(struct hl_device *hdev,
						   struct hl_userptr *userptr)
{
	return NULL;
}
void hl_odp_region_ctx_destroy(struct hl_odp_region_ctx *rg)
{
}
int hl_odp_page_in(struct hl_odp_region_ctx *rg, u64 start, u64 npages)
{
	return 0;
}

#endif /*
	* KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE &&
	* defined(_HAS_MMU_INTERVAL_READ_BEGIN) &&
	* defined(_HAS_HMM_RANGE_INTERVAL_NOTIFIER)
	*/
