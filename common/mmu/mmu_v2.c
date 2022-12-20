// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "../habanalabs.h"
#include "../../include/hw_ip/mmu/mmu_general.h"

#include <linux/slab.h>

static inline u64 get_phys_addr(struct hl_ctx *ctx, u64 shadow_addr);

static struct pgt_info *get_pgt_info(struct hl_ctx *ctx, u64 hop_addr)
{
	struct pgt_info *pgt_info = NULL;

	hash_for_each_possible(ctx->mmu_shadow_hash, pgt_info, node,
				(unsigned long) hop_addr)
		if (hop_addr == pgt_info->shadow_addr)
			break;

	return pgt_info;
}

static void _free_hop(struct hl_ctx *ctx, struct pgt_info *pgt_info)
{
	struct hl_device *hdev = ctx->hdev;

	gen_pool_free(hdev->mmu_priv.dr.mmu_pgt_pool, pgt_info->phys_addr,
			hdev->asic_prop.mmu_hop_table_size);
	hash_del(&pgt_info->node);
	kfree((u64 *) (uintptr_t) pgt_info->shadow_addr);
	kfree(pgt_info);
}

static void free_hop(struct hl_ctx *ctx, u64 hop_addr)
{
	struct pgt_info *pgt_info = get_pgt_info(ctx, hop_addr);

	_free_hop(ctx, pgt_info);
}

static u64 alloc_hop(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pgt_info *pgt_info;
	u64 phys_addr, shadow_addr;

	pgt_info = kmalloc(sizeof(*pgt_info), GFP_KERNEL);
	if (!pgt_info)
		return ULLONG_MAX;

	phys_addr = (u64) gen_pool_alloc(hdev->mmu_priv.dr.mmu_pgt_pool,
					prop->mmu_hop_table_size);
	if (!phys_addr) {
		dev_err(hdev->dev, "failed to allocate page\n");
		goto pool_add_err;
	}

	shadow_addr = (u64) (uintptr_t) kzalloc(prop->mmu_hop_table_size,
						GFP_KERNEL);
	if (!shadow_addr)
		goto shadow_err;

	pgt_info->phys_addr = phys_addr;
	pgt_info->shadow_addr = shadow_addr;
	pgt_info->ctx = ctx;
	pgt_info->num_of_ptes = 0;
	hash_add(ctx->mmu_shadow_hash, &pgt_info->node, shadow_addr);

	return shadow_addr;

shadow_err:
	gen_pool_free(hdev->mmu_priv.dr.mmu_pgt_pool,
			phys_addr, prop->mmu_hop_table_size);
pool_add_err:
	kfree(pgt_info);

	return ULLONG_MAX;
}

static inline u64 get_phys_hop0_addr(struct hl_ctx *ctx)
{
	return ctx->hdev->asic_prop.mmu_pgt_addr +
			(ctx->asid * ctx->hdev->asic_prop.mmu_hop_table_size);
}

static inline u64 get_hop0_addr(struct hl_ctx *ctx)
{
	return (u64) (uintptr_t) ctx->hdev->mmu_priv.dr.mmu_shadow_hop0 +
			(ctx->asid * ctx->hdev->asic_prop.mmu_hop_table_size);
}

/* transform the value to physical address when writing to H/W */
static inline void write_pte(struct hl_ctx *ctx, u64 shadow_pte_addr, u64 val)
{
	/*
	 * The value to write is actually the address of the next shadow hop +
	 * flags at the 12 LSBs.
	 * Hence in order to get the value to write to the physical PTE, we
	 * clear the 12 LSBs and translate the shadow hop to its associated
	 * physical hop, and add back the original 12 LSBs.
	 */
	u64 phys_val = get_phys_addr(ctx, val & HOP_PHYS_ADDR_MASK) |
				(val & FLAGS_MASK);

	ctx->hdev->asic_funcs->write_pte(ctx->hdev,
					get_phys_addr(ctx, shadow_pte_addr),
					phys_val);

	*(u64 *) (uintptr_t) shadow_pte_addr = val;
}

/* do not transform the value to physical address when writing to H/W */
static inline void write_final_pte(struct hl_ctx *ctx, u64 shadow_pte_addr,
					u64 val)
{
	ctx->hdev->asic_funcs->write_pte(ctx->hdev,
					get_phys_addr(ctx, shadow_pte_addr),
					val);
	*(u64 *) (uintptr_t) shadow_pte_addr = val;
}

/* clear the last and present bits */
static inline void clear_pte(struct hl_ctx *ctx, u64 pte_addr)
{
	/* no need to transform the value to physical address */
	write_final_pte(ctx, pte_addr, 0);
}

static inline void get_pte(struct hl_ctx *ctx, u64 hop_addr)
{
	get_pgt_info(ctx, hop_addr)->num_of_ptes++;
}

/*
 * put_pte - decrement the num of ptes and free the hop if possible
 *
 * @ctx: pointer to the context structure
 * @hop_addr: addr of the hop
 *
 * This function returns the number of ptes left on this hop. If the number is
 * 0, it means the pte was freed.
 */
static inline int put_pte(struct hl_ctx *ctx, u64 hop_addr)
{
	struct pgt_info *pgt_info = get_pgt_info(ctx, hop_addr);
	int num_of_ptes_left;

	pgt_info->num_of_ptes--;

	/*
	 * Need to save the number of ptes left because free_hop might free
	 * the pgt_info
	 */
	num_of_ptes_left = pgt_info->num_of_ptes;
	if (!num_of_ptes_left)
		_free_hop(ctx, pgt_info);

	return num_of_ptes_left;
}

static inline u64 get_alloc_next_hop_addr(struct hl_ctx *ctx, u64 curr_pte,
						bool *is_new_hop)
{
	u64 hop_addr = hl_mmu_get_next_hop_addr(ctx, curr_pte);

	if (hop_addr == ULLONG_MAX) {
		hop_addr = alloc_hop(ctx);
		*is_new_hop = (hop_addr != ULLONG_MAX);
	}

	return hop_addr;
}

/* translates shadow address inside hop to a physical address */
static inline u64 get_phys_addr(struct hl_ctx *ctx, u64 shadow_addr)
{
	u64 page_mask = (ctx->hdev->asic_prop.mmu_hop_table_size - 1);
	u64 shadow_hop_addr = shadow_addr & ~page_mask;
	u64 pte_offset = shadow_addr & page_mask;
	u64 phys_hop_addr;

	if (shadow_hop_addr != get_hop0_addr(ctx))
		phys_hop_addr = get_pgt_info(ctx, shadow_hop_addr)->phys_addr;
	else
		phys_hop_addr = get_phys_hop0_addr(ctx);

	return phys_hop_addr + pte_offset;
}

static void flush(struct hl_ctx *ctx)
{
	/* flush all writes from all cores to reach PCI */
	mb();
	ctx->hdev->asic_funcs->read_pte(ctx->hdev, get_phys_hop0_addr(ctx));
}

/**
 * hl_mmu_v2_init() - initialize the MMU module.
 * @hdev: habanalabs device structure.
 *
 * This function does the following:
 * - Create a pool of pages for pgt_infos.
 * - Create a shadow table for pgt
 *
 * Return: 0 for success, non-zero for failure.
 */
static int hl_mmu_v2_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	if (!hdev->mmu_enable)
		return 0;

	hdev->mmu_priv.dr.mmu_pgt_pool =
			gen_pool_create(__ffs(prop->mmu_hop_table_size), -1);

	if (!hdev->mmu_priv.dr.mmu_pgt_pool) {
		dev_err(hdev->dev, "Failed to create page gen pool\n");
		return -ENOMEM;
	}

	rc = gen_pool_add(hdev->mmu_priv.dr.mmu_pgt_pool, prop->mmu_pgt_addr +
			prop->mmu_hop0_tables_total_size,
			prop->mmu_pgt_size - prop->mmu_hop0_tables_total_size,
			-1);
	if (rc) {
		dev_err(hdev->dev, "Failed to add memory to page gen pool\n");
		goto err_pool_add;
	}

	hdev->mmu_priv.dr.mmu_shadow_hop0 = kvcalloc(prop->max_asid, prop->mmu_hop_table_size,
										GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(hdev->mmu_priv.dr.mmu_shadow_hop0)) {
		rc = -ENOMEM;
		goto err_pool_add;
	}

	/* MMU H/W init will be done in device hw_init() */

	return 0;

err_pool_add:
	gen_pool_destroy(hdev->mmu_priv.dr.mmu_pgt_pool);

	return rc;
}

/**
 * hl_mmu_v2_fini() - release the MMU module.
 * @hdev: habanalabs device structure.
 *
 * This function does the following:
 * - Disable MMU in H/W.
 * - Free the pgt_infos pool.
 *
 * All contexts should be freed before calling this function.
 */
static void hl_mmu_v2_fini(struct hl_device *hdev)
{
	/* MMU H/W fini was already done in device hw_fini() */

	if (!ZERO_OR_NULL_PTR(hdev->mmu_priv.dr.mmu_shadow_hop0)) {
		kvfree(hdev->mmu_priv.dr.mmu_shadow_hop0);
		gen_pool_destroy(hdev->mmu_priv.dr.mmu_pgt_pool);

		/* Make sure that if we arrive here again without init was
		 * called we won't cause kernel panic. This can happen for
		 * example if we fail during hard reset code at certain points
		 */
		hdev->mmu_priv.dr.mmu_shadow_hop0 = NULL;
	}
}

/**
 * hl_mmu_v2_ctx_init() - initialize a context for using the MMU module.
 * @ctx: pointer to the context structure to initialize.
 *
 * Initialize a mutex to protect the concurrent mapping flow, a hash to hold all
 * page tables hops related to this context.
 * Return: 0 on success, non-zero otherwise.
 */
static int hl_mmu_v2_ctx_init(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;

	if (!hdev->mmu_enable)
		return 0;

	hash_init(ctx->mmu_shadow_hash);

	return 0;
}

/*
 * hl_mmu_v2_ctx_fini - disable a ctx from using the mmu module
 *
 * @ctx: pointer to the context structure
 *
 * This function does the following:
 * - Free any pgts which were not freed yet
 * - Free the mutex
 * - Free DRAM default page mapping hops
 */
static void hl_mmu_v2_ctx_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct pgt_info *pgt_info;
	struct hlist_node *tmp;
	int i;

	if (!hdev->mmu_enable)
		return;

	if (!hash_empty(ctx->mmu_shadow_hash))
		dev_err(hdev->dev, "ctx %d is freed while it has pgts in use\n",
			ctx->asid);

	hash_for_each_safe(ctx->mmu_shadow_hash, i, tmp, pgt_info, node) {
		dev_err_ratelimited(hdev->dev,
			"pgt_info of addr 0x%llx of asid %d was not destroyed, num_ptes: %d\n",
			pgt_info->phys_addr, ctx->asid, pgt_info->num_of_ptes);
		_free_hop(ctx, pgt_info);
	}
}

static int _hl_mmu_v2_unmap(struct hl_ctx *ctx,
				u64 virt_addr, bool is_dram_addr)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_mmu_properties *mmu_prop;
	u64 hop_addr[MMU_ARCH_6_HOPS] = { 0 },
		hop_pte_addr[MMU_ARCH_6_HOPS] = { 0 },
		curr_pte, scrambled_virt_addr;
	bool is_huge = false;
	int i, hop_last;

	/* shifts and masks are the same in PMMU and HPMMU, use one of them */
	mmu_prop = is_dram_addr ? &prop->dmmu : &prop->pmmu;
	hop_last = mmu_prop->num_hops - 1;

	scrambled_virt_addr = hdev->asic_funcs->scramble_addr(hdev, virt_addr);

	hop_addr[0] = get_hop0_addr(ctx);
	hop_pte_addr[0] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, 0,
					hop_addr[0], scrambled_virt_addr);
	curr_pte = *(u64 *) (uintptr_t) hop_pte_addr[0];

	for (i = 1 ; i < mmu_prop->num_hops ; i++) {
		hop_addr[i] = hl_mmu_get_next_hop_addr(ctx, curr_pte);
		if (hop_addr[i] == ULLONG_MAX)
			goto not_mapped;

		hop_pte_addr[i] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
					hop_addr[i], scrambled_virt_addr);
		if (hop_pte_addr[i] == U64_MAX)
			return -EFAULT;

		curr_pte = *(u64 *) (uintptr_t) hop_pte_addr[i];

		if ((i < hop_last) && (curr_pte & mmu_prop->last_mask)) {
			hop_last = i;
			is_huge = true;
			break;
		}
	}

	if (is_dram_addr && !is_huge) {
		dev_err(hdev->dev,
				"DRAM unmapping should use huge pages only\n");
		return -EFAULT;
	}

	if (!(curr_pte & PAGE_PRESENT_MASK))
		goto not_mapped;

	for (i = hop_last ; i > 0 ; i--) {
		clear_pte(ctx, hop_pte_addr[i]);
		if (put_pte(ctx, hop_addr[i]))
			goto mapped;
	}
	clear_pte(ctx, hop_pte_addr[0]);

mapped:
	return 0;

not_mapped:
	dev_err(hdev->dev, "virt addr 0x%llx is not mapped to phys addr\n",
		virt_addr);

	return -EINVAL;
}

static int _hl_mmu_v2_map(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr,
			u32 page_size, bool is_dram_addr)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_mmu_properties *mmu_prop;
	u64 hop_addr[MMU_ARCH_6_HOPS] = { 0 },
		hop_pte_addr[MMU_ARCH_6_HOPS] = { 0 },
		curr_pte = 0, scrambled_virt_addr, scrambled_phys_addr;
	bool hop_new[MMU_ARCH_6_HOPS] = { false }, is_huge;
	int rc = -ENOMEM, i, hop_last;

	/*
	 * This mapping function can map a page or a huge page. For huge page
	 * there are only 4 hops rather than 5. Currently the DRAM allocation
	 * uses huge pages only but user memory could have been allocated with
	 * one of the two page sizes. Since this is a common code for all the
	 * three cases, we need this hugs page check.
	 */
	if (is_dram_addr) {
		mmu_prop = &prop->dmmu;
		is_huge = true;
	} else if (page_size == prop->pmmu_huge.page_size) {
		mmu_prop = &prop->pmmu_huge;
		is_huge = true;
	} else {
		mmu_prop = &prop->pmmu;
		is_huge = false;
	}

	hop_last = mmu_prop->num_hops - 1;

	/* huge pages use lesser hops */
	if (is_huge)
		hop_last--;

	scrambled_virt_addr = hdev->asic_funcs->scramble_addr(hdev, virt_addr);
	scrambled_phys_addr = hdev->asic_funcs->scramble_addr(hdev, phys_addr);

	/* First hop is preallocated therefore it is treated differently  */
	hop_addr[0] = get_hop0_addr(ctx);
	hop_pte_addr[0] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop,
					0, hop_addr[0], scrambled_virt_addr);
	curr_pte = *(u64 *) (uintptr_t) hop_pte_addr[0];

	/* Handle hop1 to hop_last */
	for (i = 1 ; i <= hop_last ; i++) {
		hop_addr[i] = get_alloc_next_hop_addr(ctx,
						curr_pte, &hop_new[i]);
		if (hop_addr[i] == ULLONG_MAX)
			goto err;

		hop_pte_addr[i] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
					hop_addr[i], scrambled_virt_addr);
		if (hop_addr[i] == U64_MAX)
			goto err;

		curr_pte = *(u64 *) (uintptr_t) hop_pte_addr[i];
	}

	if (curr_pte & PAGE_PRESENT_MASK) {
		dev_err(hdev->dev,
			"mapping already exists for virt_addr 0x%llx\n",
				virt_addr);

		for (i = 0 ; i <= hop_last ; i++)
			dev_dbg(hdev->dev, "hop%d pte: 0x%llx (0x%llx)\n",
				i, *(u64 *) (uintptr_t) hop_pte_addr[i],
				hop_pte_addr[i]);

		rc = -EINVAL;
		goto err;
	}

	curr_pte = (scrambled_phys_addr & HOP_PHYS_ADDR_MASK) | mmu_prop->last_mask
			| PAGE_PRESENT_MASK;

	/* Write the PTEs */
	write_final_pte(ctx, hop_pte_addr[hop_last], curr_pte);

	/* for each new hop, add its address to the table of previous-hop */
	for (i = 1 ; i <= hop_last ; i++) {
		if (hop_new[i]) {
			curr_pte = (hop_addr[i] & HOP_PHYS_ADDR_MASK) |
							PAGE_PRESENT_MASK;
			write_pte(ctx, hop_pte_addr[i - 1], curr_pte);
			if (i - 1)
				get_pte(ctx, hop_addr[i - 1]);
		}
	}
	get_pte(ctx, hop_addr[hop_last]);

	return 0;

err:
	for (i = 1 ; i <= hop_last ; i++)
		if (hop_new[i] && (hop_addr[i] != U64_MAX))
			free_hop(ctx, hop_addr[i]);

	return rc;
}

/*
 * hl_mmu_v2_swap_out - marks all mapping of the given ctx as swapped out
 *
 * @ctx: pointer to the context structure
 *
 */
static void hl_mmu_v2_swap_out(struct hl_ctx *ctx)
{

}

/*
 * hl_mmu_v2_swap_in - marks all mapping of the given ctx as swapped in
 *
 * @ctx: pointer to the context structure
 *
 */
static void hl_mmu_v2_swap_in(struct hl_ctx *ctx)
{

}

static int hl_mmu_v2_get_tlb_info(struct hl_ctx *ctx, u64 virt_addr,
				struct hl_mmu_hop_info *hops)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_mmu_properties *mmu_prop;
	bool is_dram_addr, is_pmmu_addr, is_pmmu_h_addr, is_huge;
	int i, used_hops;

	is_dram_addr = hl_mem_area_inside_range(virt_addr, prop->dmmu.page_size,
						prop->dmmu.start_addr,
						prop->dmmu.end_addr);
	is_pmmu_addr = hl_mem_area_inside_range(virt_addr, prop->pmmu.page_size,
						prop->pmmu.start_addr,
						prop->pmmu.end_addr);
	is_pmmu_h_addr = hl_mem_area_inside_range(virt_addr,
						prop->pmmu_huge.page_size,
						prop->pmmu_huge.start_addr,
						prop->pmmu_huge.end_addr);
	if (is_dram_addr) {
		mmu_prop = &prop->dmmu;
		is_huge = true;
		hops->range_type = HL_VA_RANGE_TYPE_DRAM;
	} else if (is_pmmu_addr) {
		mmu_prop = &prop->pmmu;
		is_huge = false;
		hops->range_type = HL_VA_RANGE_TYPE_HOST;
	} else if (is_pmmu_h_addr) {
		mmu_prop = &prop->pmmu_huge;
		is_huge = true;
		hops->range_type = HL_VA_RANGE_TYPE_HOST_HUGE;
	} else {
		return -EINVAL;
	}

	used_hops = mmu_prop->num_hops;

	/* huge pages use lesser hops */
	if (is_huge)
		used_hops--;

	hops->scrambled_vaddr =
			hdev->asic_funcs->scramble_addr(hdev, virt_addr);

	hops->hop_info[0].hop_addr = get_phys_hop0_addr(ctx);
	hops->hop_info[0].hop_pte_addr =
			hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, 0,
					hops->hop_info[0].hop_addr,
					hops->scrambled_vaddr);
	hops->hop_info[0].hop_pte_val =
			hdev->asic_funcs->read_pte(hdev,
						hops->hop_info[0].hop_pte_addr);

	for (i = 1 ; i < used_hops ; i++) {
		hops->hop_info[i].hop_addr =
			hl_mmu_get_next_hop_addr(ctx,
					hops->hop_info[i - 1].hop_pte_val);
		if (hops->hop_info[i].hop_addr == ULLONG_MAX)
			return -EFAULT;

		hops->hop_info[i].hop_pte_addr =
				hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
						hops->hop_info[i].hop_addr,
						hops->scrambled_vaddr);
		hops->hop_info[i].hop_pte_val =
				hdev->asic_funcs->read_pte(hdev,
						hops->hop_info[i].hop_pte_addr);

		if (!(hops->hop_info[i].hop_pte_val & PAGE_PRESENT_MASK))
			return -EFAULT;

		if (hops->hop_info[i].hop_pte_val & mmu_prop->last_mask)
			break;
	}

	/* if passed over all hops then no last hop was found */
	if (i == mmu_prop->num_hops)
		return -EFAULT;

	if (!(hops->hop_info[i].hop_pte_val & PAGE_PRESENT_MASK))
		return -EFAULT;

	if (hops->scrambled_vaddr != virt_addr)
		hops->unscrambled_paddr = hdev->asic_funcs->descramble_addr
				(hdev, hops->hop_info[i].hop_pte_val);
	else
		hops->unscrambled_paddr = hops->hop_info[i].hop_pte_val;

	hops->used_hops = i + 1;

	return 0;
}

/*
 * hl_mmu_v2_prepare - prepare mmu_if for working with mmu v2
 *
 * @hdev: pointer to the device structure
 * @mmu_if: pointer to the mmu interface structure
 */
void hl_mmu_v2_set_funcs(struct hl_device *hdev, struct hl_mmu_funcs *mmu)
{
	mmu->init = hl_mmu_v2_init;
	mmu->fini = hl_mmu_v2_fini;
	mmu->ctx_init = hl_mmu_v2_ctx_init;
	mmu->ctx_fini = hl_mmu_v2_ctx_fini;
	mmu->map = _hl_mmu_v2_map;
	mmu->unmap = _hl_mmu_v2_unmap;
	mmu->flush = flush;
	mmu->swap_out = hl_mmu_v2_swap_out;
	mmu->swap_in = hl_mmu_v2_swap_in;
	mmu->get_tlb_info = hl_mmu_v2_get_tlb_info;
}
