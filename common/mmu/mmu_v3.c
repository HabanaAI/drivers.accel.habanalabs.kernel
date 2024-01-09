// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2024 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "../habanalabs.h"
#include "../../include/hw_ip/mmu/mmu_general.h"
#include "../../include/hw_ip/mmu/mmu_v3_0.h"

#include <linux/slab.h>

/**
 * hl_mmu_v3_create_single_pte() - create single page table entry
 * @ctx: pointer to the context structure to initialize.
 * @virt_addr: virtual address to map to
 * @phys_addr: physical address to be mapped
 * @tlb_page_size_code: the code of PGT size to set into PTE flags
 *
 * @return 0 on success, otherwise non zero error code.
 *
 * @note MMU V3 allows to map bigger page than one PTE entry in the last HOP can point to (1MB).
 *       Example: we can map 32 MB page. this is done by creating 32 consequent PTEs. yet, the fact
 *       that we code in the PTE flags the matching code for 32MB page (tlb_page_size_code) hints
 *       the HW to create only single TLB entry and so to be able to store more mappings.
 */
static int hl_mmu_v3_create_single_pte(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr,
									u8 tlb_page_size_code)
{
	u64 hop_pte_phys_addr[MMU_ARCH_3_HOPS] = { 0 }, hop_addr[MMU_ARCH_3_HOPS] = { 0 },
							curr_pte = 0;
	bool hop_new[MMU_ARCH_3_HOPS] = { false };
	int i, num_hops, hop_last, rc = -ENOMEM;
	struct hl_device *hdev = ctx->hdev;
	struct hl_mmu_properties *mmu_prop;

	mmu_prop = hdev->hmmu_info.prop;

	/* 1GB page use only 2 HOPs */
	num_hops = (tlb_page_size_code == HOP1_TLB_PAGE_SIZE_1G) ?
					(mmu_prop->num_hops - 1) : mmu_prop->num_hops;
	hop_last = num_hops - 1;

	for (i = 0 ; i < num_hops ; i++) {
		if (i == 0) {
			hop_addr[0] = hl_mmu_dr_get_hop0_addr(ctx);
			hop_pte_phys_addr[0] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, 0,
								hop_addr[0], virt_addr);
		} else {
			hop_addr[i] = hl_mmu_dr_get_alloc_next_hop_addr(ctx, curr_pte, &hop_new[i]);
			if (hop_addr[i] == ULLONG_MAX) {
				rc = -ENOMEM;
				goto err;
			}

			hop_pte_phys_addr[i] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
						hop_addr[i], virt_addr);
			if (hop_pte_phys_addr[i] == U64_MAX) {
				rc = -EINVAL;
				goto err;
			}
		}

		if (!hop_pte_phys_addr[i])
			goto err;

		curr_pte = *(u64 *) (uintptr_t) hop_pte_phys_addr[i];
	}

	if (curr_pte & PAGE_PRESENT_MASK) {
		dev_err(hdev->dev, "mapping already exists for virt_addr 0x%llx\n", virt_addr);
		rc = -EINVAL;
		goto err;
	}

	curr_pte = (phys_addr & HOP_PHYS_ADDR_MASK) |
					mmu_prop->last_mask |
					PAGE_PRESENT_MASK |
					FIELD_PREP(TLB_PAGE_SIZE_MASK, tlb_page_size_code);

	/* Write the PTEs */
	hl_mmu_dr_write_final_pte(ctx, hop_pte_phys_addr[hop_last], curr_pte);

	/* for each new hop, add its address to the table of previous-hop */
	for (i = 1 ; i <= hop_last ; i++) {
		int cur_hop_idx = i, prev_hop_idx = i - 1;

		if (!hop_new[cur_hop_idx])
			continue;

		curr_pte = (hop_addr[i] & HOP_PHYS_ADDR_MASK) | PAGE_PRESENT_MASK;

		/*
		 * for PTW that continues until the last HOP (HOP2) we should set
		 * HOP1_TLB_PAGE_SIZE_NOT_FINAL in the TLB_PAGE_SIZE in the PTE of HOP1
		 */
		if (cur_hop_idx == (mmu_prop->num_hops - 1))
			curr_pte |= FIELD_PREP(TLB_PAGE_SIZE_MASK, HOP1_TLB_PAGE_SIZE_NOT_FINAL);

		hl_mmu_dr_write_pte(ctx, hop_pte_phys_addr[i - 1], curr_pte);

		if (prev_hop_idx)
			hl_mmu_dr_get_pte(ctx, hop_addr[prev_hop_idx]);
	}
	hl_mmu_dr_get_pte(ctx, hop_addr[hop_last]);

	return 0;

err:
	for (i = 1 ; i <= hop_last ; i++)
		if (hop_new[i] && (hop_addr[i] != U64_MAX))
			hl_mmu_dr_free_hop(ctx, hop_addr[i]);
	return rc;
}

/**
 * unmap_single_pte() - unmap single page pointed by PTE
 * @ctx: pointer to the context structure to initialize.
 * @virt_addr: virtual address to the mapped page.
 * @last_pte: pointer to hold value of last PTE (if non NULL)
 *
 * @return 0 on success, otherwise non zero error code.
 */
static int unmap_single_pte(struct hl_ctx *ctx, u64 virt_addr, u64 *last_pte)
{
	u64 curr_pte, hop_pte_phys_addr[MMU_ARCH_3_HOPS] = { 0 },
				hop_addr[MMU_ARCH_3_HOPS] = { 0 };
	struct hl_device *hdev = ctx->hdev;
	struct hl_mmu_properties *mmu_prop;
	int i, hop_last, rc = 0;

	mmu_prop = hdev->hmmu_info.prop;
	hop_last = mmu_prop->num_hops - 1;
	curr_pte = 0;

	for (i = 0 ; i < mmu_prop->num_hops ; i++) {
		if (i == 0) {
			hop_addr[0] = hl_mmu_dr_get_hop0_addr(ctx);
			hop_pte_phys_addr[0] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, 0,
								hop_addr[0], virt_addr);
			if (hop_pte_phys_addr[i] == U64_MAX) {
				rc = -EINVAL;
				goto not_mapped;
			}
		} else {
			hop_addr[i] = hl_mmu_get_next_hop_addr(ctx, curr_pte);
			if (hop_addr[i] == ULLONG_MAX)
				goto not_mapped;

			hop_pte_phys_addr[i] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
						hop_addr[i], virt_addr);
			if (hop_pte_phys_addr[i] == U64_MAX)
				return -EFAULT;
		}

		if (!hop_pte_phys_addr[i])
			goto not_mapped;

		curr_pte = *(u64 *) (uintptr_t) hop_pte_phys_addr[i];

		if (!(curr_pte & PAGE_PRESENT_MASK))
			goto not_mapped;

		if (curr_pte & mmu_prop->last_mask) {
			hop_last = i;
			break;
		}
	}

	if (last_pte)
		*last_pte = curr_pte;

	for (i = hop_last ; i > 0 ; i--) {
		hl_mmu_dr_clear_pte(ctx, hop_pte_phys_addr[i]);
		if (hl_mmu_dr_put_pte(ctx, hop_addr[i]))
			goto mapped;
	}
	hl_mmu_dr_clear_pte(ctx, hop_pte_phys_addr[0]);

mapped:
	return 0;

not_mapped:
	dev_err(hdev->dev, "virt addr 0x%llx is not mapped to phys addr\n", virt_addr);

	return rc;
}

/*
 * looking at HMMU specs we can see that code -> order (size) mapping is:
 * code		order	(size)
 * 0x0		20	(1M)
 * 0x1		21	(2M)
 * ...
 * 0xB		31	(2G)
 *
 * Note that 0x7 (i.e. 128MB page) is reserved but is already checked
 * in the flow so allowing it here to be more efficient.
 */
u64 hl_mmu_v3_page_map_code_to_size(u32 code)
{
	u8 order = code + PAGE_SHIFT_1MB;

	return BIT_ULL(order);
}

/* see doc of hl_mmu_v3_page_map_code_to_size */
u32 hl_mmu_v3_page_map_size_to_code(u64 size)
{
	u8 order = __ffs(size);

	return order - PAGE_SHIFT_1MB;
}

inline bool hl_mmu_v3_is_valid_page_code(u8 code)
{
	return (code != TLB_PAGE_SIZE_INVALID) &&
			(code <= HOP1_TLB_PAGE_SIZE_2G);
}

inline bool hl_mmu_v3_is_hop2_page_code(u32 code)
{
	return (code < HOP1_TLB_PAGE_SIZE_256M);
}

static int hl_mmu_v3_map_page_in_chunks(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr,
								u32 page_size)
{
	u8 num_ptes, tlb_page_size_code;
	u64 mem_chunk_size;
	int i, rc;

	tlb_page_size_code = hl_mmu_v3_page_map_size_to_code(page_size);

	if (hl_mmu_v3_is_hop2_page_code(tlb_page_size_code)) {
		num_ptes = page_size >> PAGE_SHIFT_1MB;
		mem_chunk_size = PAGE_SIZE_1MB;
	} else {
		num_ptes = page_size >> PAGE_SHIFT_256MB;
		mem_chunk_size = PAGE_SIZE_256MB;
	}

	/*
	 * HOP2 basic page size is 1M and HOP1 basic page size is 256M.
	 * for 32MB page 32 PTEs, each pointing to 1MB section, will be created in HOP2 and
	 * for 1GB page 4 PTEs, each pointing to 256MB section, will be created in HOP1.
	 * In both cases the TLB page size "hint" will create only single TLB entry.
	 */
	for (i = 0; i < num_ptes; i++) {
		rc = hl_mmu_v3_create_single_pte(ctx, virt_addr, phys_addr, tlb_page_size_code);
		if (rc)
			goto unmap;

		/* increment to map the next 1MB page */
		virt_addr += mem_chunk_size;
		phys_addr += mem_chunk_size;
	}

	return 0;

unmap:
	while (i > 0) {
		/* go back to the former successful mapping */
		i--;
		virt_addr -= mem_chunk_size;
		if (unmap_single_pte(ctx, virt_addr, NULL))
			break;
	}

	return rc;
}

/**
 * hl_mmu_v3_map - add mapping for virtual address
 *
 * @ctx: pointer to the context structure
 * @virt_addr: the virtual address to map to
 * @phys_addr: the physical address to map
 * @page_size: page size
 * @is_dram_addr: true is DRAM address, otherwise false
 *
 * @return 0 on success otherwise non-zero error code
 */
static int hl_mmu_v3_map(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr,
				u32 page_size, bool is_dram_addr)
{
	u64 supported_pages_mask = ctx->hdev->asic_prop.dmmu.supported_pages_mask;

	if (!(page_size & supported_pages_mask)) {
		dev_err(ctx->hdev->dev,
				"%x page size not supported (supported_pages_mask: %llx)\n",
				page_size, supported_pages_mask);
		return -EINVAL;
	}

	return hl_mmu_v3_map_page_in_chunks(ctx, virt_addr, phys_addr, page_size);
}

/**
 * hl_mmu_v3_unmap - unmap virtual address from page tables
 *
 * @ctx: pointer to the context structure
 * @virt_addr: the virtual address to unmap
 * @is_dram_addr: true is DRAM address, otherwise false
 *
 * @return 0 on success otherwise non-zero error code
 */
static int hl_mmu_v3_unmap(struct hl_ctx *ctx, u64 virt_addr, bool is_dram_addr)
{
	u64 last_pte, mem_chunk_size, page_size;
	u8 tlb_page_size_code, num_ptes;
	int i, rc;

	/* first, unmap the first PTE and get BTW the PTE value to determine mapped page size */
	rc = unmap_single_pte(ctx, virt_addr, &last_pte);
	if (rc)
		return rc;

	tlb_page_size_code = FIELD_GET(TLB_PAGE_SIZE_MASK, last_pte);

	if (!hl_mmu_v3_is_valid_page_code(tlb_page_size_code)) {
		dev_err(ctx->hdev->dev, "Invalid TLB page size: %u\n", tlb_page_size_code);
		return -EFAULT;
	}

	/*
	 * page size below 256MB is divided to 1MB bytes pages while page size from 256MB on
	 * is divided to 256MB pages
	 */
	page_size = hl_mmu_v3_page_map_code_to_size(tlb_page_size_code);
	if (hl_mmu_v3_is_hop2_page_code(tlb_page_size_code)) {
		num_ptes = page_size >> PAGE_SHIFT_1MB;
		mem_chunk_size = PAGE_SIZE_1MB;
	} else {
		num_ptes = page_size >> PAGE_SHIFT_256MB;
		mem_chunk_size = PAGE_SIZE_256MB;
	}

	/* unmap the last 31 PTEs */
	for (i = 1; i < num_ptes; i++) {
		/* increment to map the next 1MB page */
		virt_addr += mem_chunk_size;

		rc = unmap_single_pte(ctx, virt_addr, NULL);
		if (rc)
			return rc;
	}

	return 0;
}

/**
 * hl_mmu_v3_swap_out - marks all mapping of the given ctx as swapped out
 *
 * @ctx: pointer to the context structure
 */
static void hl_mmu_v3_swap_out(struct hl_ctx *ctx)
{

}

/**
 * hl_mmu_v3_swap_in - marks all mapping of the given ctx as swapped in
 *
 * @ctx: pointer to the context structure
 */
static void hl_mmu_v3_swap_in(struct hl_ctx *ctx)
{

}

/**
 * hl_mmu_v3_set_funcs - set MMU functions for working with DRAM mmu v3
 *
 * @hdev: pointer to the device structure
 * @mmu: pointer to the mmu functions structure
 */
void hl_mmu_v3_set_funcs(struct hl_device *hdev, struct hl_mmu_funcs *mmu)
{
	/* MMU V3 is currently for HMMU only */
	if (!hdev->dram_enable)
		return;

	mmu->init = hl_mmu_dr_init;
	mmu->fini = hl_mmu_dr_fini;
	mmu->ctx_init = hl_mmu_v2_ctx_init;
	mmu->ctx_fini = hl_mmu_v2_ctx_fini;
	mmu->map = hl_mmu_v3_map;
	mmu->unmap = hl_mmu_v3_unmap;
	mmu->flush = hl_mmu_dr_flush;
	mmu->swap_out = hl_mmu_v3_swap_out;
	mmu->swap_in = hl_mmu_v3_swap_in;
	mmu->get_tlb_info = hl_mmu_v2_get_tlb_info;
}
