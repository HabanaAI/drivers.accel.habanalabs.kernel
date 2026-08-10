// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "../habanalabs.h"
#include "../../include/hw_ip/mmu/mmu_general.h"
#include "../../include/hw_ip/mmu/mmu_v3_0.h"

#include <linux/slab.h>

/*
 * Up until now it was assumed that every ASIC will have the same MMU ARCH for
 * both HMMU/PMMU and so- some MMU specific properties were defined by the ASIC level
 * (e.g. HOP table size).
 * In order to address this issue without heavily refactoring the code current MMU V3
 * will take advantage of the fact that only Gaudi3 HMMU is using V3 ARCH and
 * so will address internally those specific structures.
 * After refactoring the MMU properties the V3 code will be modified accordingly
 */

/**
 * hl_mmu_hr_v3_get_pgt_info() - get pgt_info structure from hash
 * @ctx: pointer to the context structure to initialize.
 * @phys_hop_addr: phys address of the HOP for which we want the info.
 *
 * @return valid pgt_info structure on hash hit, NULL if no node was found in hash
 *
 * note: the hop phys addr is the hash key
 */
static struct pgt_info *hl_mmu_hr_v3_get_pgt_info(struct hl_ctx *ctx, u64 phys_hop_addr)
{
	struct pgt_info *pgt_info = NULL;

	hash_for_each_possible(ctx->hr_hmmu_phys_hash, pgt_info, node,
				(unsigned long) phys_hop_addr)
		if (phys_hop_addr == pgt_info->phys_addr)
			break;

	return pgt_info;
}

/**
 * hl_mmu_hr_v3_add_pgt_info() - add pgt_info structure to the hash
 * @ctx: pointer to the context structure to initialize.
 * @pgt_info: page table info to add.
 * @phys_addr: HOP's phys addr (used as hash key)
 *
 * note: hash contains pgt_info only for HOPS created after MMU init (all HOPs but HOP0)
 */
static void hl_mmu_hr_v3_add_pgt_info(struct hl_ctx *ctx, struct pgt_info *pgt_info,
									dma_addr_t phys_addr)
{
	hash_add(ctx->hr_hmmu_phys_hash, &pgt_info->node, phys_addr);
}

/**
 * hl_mmu_hr_v3_get_hop0_pgt_info() - add pgt_info structure of HOP0
 * @ctx: pointer to the context structure to initialize.
 *
 * @return the ASID's pgt_info structure
 *
 * @note HOP0 pgt's are pre-allocated (i.e. allocated during MMU init phase) and so are treated
 *       slightly differently than other HOPs.
 */
static struct pgt_info *hl_mmu_hr_v3_get_hop0_pgt_info(struct hl_ctx *ctx)
{
	return &ctx->hdev->hmmu_info.priv.hr.mmu_asid_hop0[ctx->asid];
}

/**
 * hl_mmu_v3_hr_init() - initialize the MMU module.
 * @hdev: habanalabs device structure.
 *
 * @return 0 on success otherwise non-zero error code
 *
 * This function does the following:
 * - Create a pool of pages for pgt_infos.
 * - Create a shadow table for pgt
 */
static inline int hl_mmu_v3_hr_init(struct hl_device *hdev)
{
	struct hl_mmu_info *mmu_info = &hdev->hmmu_info;

	return hl_mmu_hr_init(hdev, &mmu_info->priv.hr, mmu_info->prop->hop_table_size,
				mmu_info->prop->pgt_size);
}

/**
 * hl_mmu_v3_hr_fini() - release the MMU module.
 * @hdev: habanalabs device structure.
 *
 * This function does the following:
 * - Disable MMU in H/W.
 * - Free the pgt_infos pool.
 *
 * All contexts should be freed before calling this function.
 */
static inline void hl_mmu_v3_hr_fini(struct hl_device *hdev)
{
	struct hl_mmu_info *mmu_info = &hdev->hmmu_info;

	hl_mmu_hr_fini(hdev, &mmu_info->priv.hr, mmu_info->prop->hop_table_size);
}

/**
 * hl_mmu_v3_hr_ctx_init() - initialize a context for using the MMU module.
 * @ctx: pointer to the context structure to initialize.
 *
 * @return 0 on success otherwise non-zero error code
 *
 * Initialize a hash to hold all page tables hops related to this context.
 */
static int hl_mmu_v3_hr_ctx_init(struct hl_ctx *ctx)
{
	hash_init(ctx->hr_hmmu_phys_hash);
	return 0;
}

/**
 * hl_mmu_v3_hr_ctx_fini - disable a ctx from using the mmu module
 *
 * @ctx: pointer to the context structure
 *
 * This function does the following:
 * - Free any pgts which were not freed yet
 * - Free the mutex
 * - Free DRAM default page mapping hops
 */
static void hl_mmu_v3_hr_ctx_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct pgt_info *pgt_info;
	struct hlist_node *tmp;
	int i;

	if (!hash_empty(ctx->hr_hmmu_phys_hash))
		dev_err(hdev->dev, "ctx %d is freed while it has pgts in use\n", ctx->asid);

	hash_for_each_safe(ctx->hr_hmmu_phys_hash, i, tmp, pgt_info, node) {
		dev_err_ratelimited(hdev->dev,
			"pgt_info of addr 0x%llx of asid %d was not destroyed, num_ptes: %d\n",
			pgt_info->phys_addr, ctx->asid, pgt_info->num_of_ptes);
		hl_mmu_hr_free_hop_remove_pgt(pgt_info, &hdev->hmmu_info.priv.hr,
							hdev->hmmu_info.prop->hop_table_size);
	}
}

/**
 * hl_mmu_v3_hr_create_single_pte() - create single page table entry
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
static int hl_mmu_v3_hr_create_single_pte(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr,
									u8 tlb_page_size_code)
{
	u64 hop_pte_phys_addr[MMU_ARCH_3_HOPS] = { 0 }, curr_pte = 0;
	struct pgt_info *hops_pgt_info[MMU_ARCH_3_HOPS] = { NULL };
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
		if (i == 0)
			hops_pgt_info[i] = hl_mmu_hr_v3_get_hop0_pgt_info(ctx);
		else
			hops_pgt_info[i] = hl_mmu_hr_get_alloc_next_hop(ctx,
								&ctx->hdev->hmmu_info.priv.hr,
								&ctx->hdev->hmmu_info.func.hr_funcs,
								mmu_prop, curr_pte, &hop_new[i]);

		if (!hops_pgt_info[i])
			goto err;

		hop_pte_phys_addr[i] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
									hops_pgt_info[i]->phys_addr,
									virt_addr);

		curr_pte = *(u64 *) (uintptr_t)
				hl_mmu_hr_pte_phys_to_virt(ctx, hops_pgt_info[i],
									hop_pte_phys_addr[i],
									mmu_prop->hop_table_size);
	}

	if (curr_pte & PAGE_PRESENT_MASK) {
		dev_err(hdev->dev, "mapping already exists for virt_addr 0x%llx\n", virt_addr);

		for (i = 0 ; i < num_hops ; i++)
			dev_dbg(hdev->dev, "hop%d pte: 0x%llx (0x%llx)\n",
					i,
					*(u64 *) (uintptr_t)
						hl_mmu_hr_pte_phys_to_virt(ctx, hops_pgt_info[i],
									hop_pte_phys_addr[i],
									mmu_prop->hop_table_size),
					hop_pte_phys_addr[i]);
		rc = -EINVAL;
		goto err;
	}

	curr_pte = (phys_addr & HOP_PHYS_ADDR_MASK) |
					mmu_prop->last_mask |
					PAGE_PRESENT_MASK |
					FIELD_PREP(TLB_PAGE_SIZE_MASK, tlb_page_size_code);

	/* Write the PTEs */
	hl_mmu_hr_write_pte(ctx, hops_pgt_info[hop_last], hop_pte_phys_addr[hop_last],
							curr_pte, mmu_prop->hop_table_size);

	/* for each new hop, add its address to the table of previous-hop */
	for (i = 1 ; i < num_hops ; i++) {
		int cur_hop_idx = i, prev_hop_idx = i - 1;

		if (!hop_new[cur_hop_idx])
			continue;

		curr_pte = (hops_pgt_info[cur_hop_idx]->phys_addr & HOP_PHYS_ADDR_MASK) |
						PAGE_PRESENT_MASK;

		/*
		 * for PTW that continues until the last HOP (HOP2) we should set
		 * HOP1_TLB_PAGE_SIZE_NOT_FINAL in the TLB_PAGE_SIZE in the PTE of HOP1
		 */
		if (cur_hop_idx == (mmu_prop->num_hops - 1))
			curr_pte |= FIELD_PREP(TLB_PAGE_SIZE_MASK, HOP1_TLB_PAGE_SIZE_NOT_FINAL);

		hl_mmu_hr_write_pte(ctx, hops_pgt_info[prev_hop_idx],
						hop_pte_phys_addr[prev_hop_idx],
						curr_pte, mmu_prop->hop_table_size);
		if (prev_hop_idx)
			hl_mmu_hr_get_pte(ctx, &ctx->hdev->hmmu_info.func.hr_funcs,
						hops_pgt_info[prev_hop_idx]->phys_addr);
	}
	hl_mmu_hr_get_pte(ctx, &ctx->hdev->hmmu_info.func.hr_funcs,
				hops_pgt_info[hop_last]->phys_addr);

	return 0;

err:
	for (i = 1 ; i < num_hops ; i++)
		if (hop_new[i] && hops_pgt_info[i])
			hl_mmu_hr_free_hop_remove_pgt(hops_pgt_info[i],
								&ctx->hdev->hmmu_info.priv.hr,
								mmu_prop->hop_table_size);

	return rc;
}

/**
 * hl_v3_hr_unmap_single_pte() - unmap single page pointed by PTE
 * @ctx: pointer to the context structure to initialize.
 * @virt_addr: virtual address to the mapped page.
 *
 * @return 0 on success, otherwise non zero error code.
 */
static int hl_v3_hr_unmap_single_pte(struct hl_ctx *ctx, u64 virt_addr)
{
	struct pgt_info *hops_pgt_info[MMU_ARCH_3_HOPS] = { NULL };
	u64 curr_pte, hop_pte_phys_addr[MMU_ARCH_3_HOPS] = { 0 };
	struct hl_device *hdev = ctx->hdev;
	struct hl_mmu_properties *mmu_prop;
	int i, hop_last;

	mmu_prop = hdev->hmmu_info.prop;
	hop_last = mmu_prop->num_hops - 1;
	curr_pte = 0;

	for (i = 0 ; i < mmu_prop->num_hops ; i++) {
		/* we get HOP0 differently, it doesn't need curr_pte */
		if (i == 0)
			hops_pgt_info[i] = hl_mmu_hr_v3_get_hop0_pgt_info(ctx);
		else
			hops_pgt_info[i] = hl_mmu_hr_get_next_hop_pgt_info(ctx,
								&ctx->hdev->hmmu_info.func.hr_funcs,
								curr_pte);
		if (!hops_pgt_info[i])
			goto not_mapped;

		hop_pte_phys_addr[i] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
									hops_pgt_info[i]->phys_addr,
									virt_addr);
		if (hop_pte_phys_addr[i] == U64_MAX)
			return -EFAULT;

		curr_pte = *(u64 *) (uintptr_t)
				hl_mmu_hr_pte_phys_to_virt(ctx, hops_pgt_info[i],
									hop_pte_phys_addr[i],
									mmu_prop->hop_table_size);

		if (!(curr_pte & PAGE_PRESENT_MASK))
			goto not_mapped;

		if (curr_pte & mmu_prop->last_mask) {
			hop_last = i;
			break;
		}
	}

	for (i = hop_last ; i > 0 ; i--) {
		hl_mmu_hr_clear_pte(ctx, hops_pgt_info[i], hop_pte_phys_addr[i],
								mmu_prop->hop_table_size);

		if (hl_mmu_hr_put_pte(ctx, hops_pgt_info[i], &ctx->hdev->hmmu_info.priv.hr,
								mmu_prop->hop_table_size))
			goto mapped;
	}
	hl_mmu_hr_clear_pte(ctx, hops_pgt_info[0], hop_pte_phys_addr[0],
							mmu_prop->hop_table_size);

mapped:
	return 0;

not_mapped:
	dev_err(hdev->dev, "virt addr 0x%llx is not mapped to phys addr\n", virt_addr);

	return -EINVAL;
}

/**
 * hl_mmu_v3_hr_swap_out - marks all mapping of the given ctx as swapped out
 *
 * @ctx: pointer to the context structure
 */
static void hl_mmu_v3_hr_swap_out(struct hl_ctx *ctx)
{

}

/**
 * hl_mmu_v3_hr_swap_in - marks all mapping of the given ctx as swapped in
 *
 * @ctx: pointer to the context structure
 */
static void hl_mmu_v3_hr_swap_in(struct hl_ctx *ctx)
{

}

/**
 * hl_mmu_hr_v3_get_tlb_mapping_params() - exatract the mapping parameters
 * @hdev: pointer to the device structure
 * @mmu_prop: container to store the MMU properties
 * @hops: HOP info structure
 * @virt_addr: virtual address to the mapped page
 * @is_huge: denote if it's huge page
 *
 * @return 0 on success, otherwise non zero error code.
 *
 * @note this function allows all host resident functions share the same "get TLB info" function
 *       by getting the MMU specific parameters.
 */
static int hl_mmu_hr_v3_get_tlb_mapping_params(struct hl_device *hdev,
								struct hl_mmu_properties **mmu_prop,
								struct hl_mmu_hop_info *hops,
								u64 virt_addr, bool *is_huge)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	bool is_dram_addr;

	is_dram_addr = hl_mem_area_inside_range(virt_addr, prop->dmmu.page_size,
						prop->dmmu.start_addr,
						prop->dmmu.end_addr);

	if (!is_dram_addr) {
		dev_err(hdev->dev, "MMU V3 is only for DRAM mappings\n");
		return -EINVAL;
	}

	*mmu_prop = hdev->hmmu_info.prop;
	*is_huge = false;
	hops->range_type = HL_VA_RANGE_TYPE_DRAM;

	return 0;
}

/**
 * hl_mmu_v3_hr_get_tlb_info - get TLB info
 *
 * @ctx: pointer to the context structure
 * @virt_addr: the virtual address
 * @hops: hops info structure to be filled by the function
 *
 * @return 0 on success, otherwise non-zero error code
 */
static int hl_mmu_v3_hr_get_tlb_info(struct hl_ctx *ctx, u64 virt_addr,
						struct hl_mmu_hop_info *hops)
{
	return hl_mmu_hr_get_tlb_info(ctx, virt_addr, hops, &ctx->hdev->hmmu_info.func.hr_funcs);
}

/**
 * hl_mmu_v3_hr_set_funcs - set MMU functions for working with host resident mmu v3
 *
 * @hdev: pointer to the device structure
 * @mmu: pointer to the mmu functions structure
 */
void hl_mmu_v3_hr_set_funcs(struct hl_device *hdev, struct hl_mmu_funcs *mmu)
{
	/* MMU V3 is currently for HMMU only */
	if (!hdev->dram_enable)
		return;

	mmu->init = hl_mmu_v3_hr_init;
	mmu->fini = hl_mmu_v3_hr_fini;
	mmu->ctx_init = hl_mmu_v3_hr_ctx_init;
	mmu->ctx_fini = hl_mmu_v3_hr_ctx_fini;
	mmu->map_page = hl_mmu_map_page_by_multiple_ptes;
	mmu->unmap_page = hl_mmu_unmap_page_by_multiple_ptes;
	mmu->flush = hl_mmu_hr_flush;
	mmu->swap_out = hl_mmu_v3_hr_swap_out;
	mmu->swap_in = hl_mmu_v3_hr_swap_in;
	mmu->get_tlb_info = hl_mmu_v3_hr_get_tlb_info;
	mmu->map_page_pte = hl_mmu_v3_hr_create_single_pte;
	mmu->unmap_page_pte = hl_v3_hr_unmap_single_pte;
	mmu->hr_funcs.get_hop0_pgt_info = hl_mmu_hr_v3_get_hop0_pgt_info;
	mmu->hr_funcs.get_pgt_info = hl_mmu_hr_v3_get_pgt_info;
	mmu->hr_funcs.add_pgt_info = hl_mmu_hr_v3_add_pgt_info;
	mmu->hr_funcs.get_tlb_mapping_params = hl_mmu_hr_v3_get_tlb_mapping_params;
}
