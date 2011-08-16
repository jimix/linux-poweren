/*
 * Copyright 2008-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/hugetlb.h>
#include <linux/of.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mmu_context.h>

#include <asm/copro-driver.h>
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>

#include <mm/icswx.h>

#include "cop.h"
#include "pbic_cop.h"


/* Leave 8 slots (0-7) for non-bolted entries */
#define PBIC_WATERMARK_LIMIT 7

static inline int mm_used_pbic(struct pbic *pbic, struct mm_struct *mm)
{
	int rc;

	spin_lock(mm->context.cop_lockp);
	rc = test_bit(pbic->index, (unsigned long *)
		      &mm->context.pbics_used);
	spin_unlock(mm->context.cop_lockp);

	return rc;
}

static void set_mm_used_pbic(struct pbic *pbic, struct mm_struct *mm)
{
	spin_lock(mm->context.cop_lockp);
	if (pbic)
		set_bit(pbic->index, (unsigned long *)
			&mm->context.pbics_used);
	else
		mm->context.pbics_used = ~0ULL;
	spin_unlock(mm->context.cop_lockp);
	mb();
}

/**
 * pbic_tlb_reserve_bolting_slot - Reserve a PBIC TLB slot for bolting.
 * @pbic: The PBIC to reserve the slot in.
 *
 * Reserves a slot in @pbic's TLB, usable for inserting a bolted TLB
 * entry. If there is no space left returns -ENOSPC.
 *
 * Must be called with the @pbic's lock held.
 */
static int pbic_tlb_reserve_bolting_slot(struct pbic *pbic)
{
	int slot;

	/* Try and find a hole first */
	for (slot = pbic->tlb_size - 1; slot > pbic->watermark; slot--) {
		if (pbic->tlb_info[slot].count == 0)
			goto out;
	}

	if (pbic->next_slot == pbic->watermark) {
		if (pbic->watermark == PBIC_WATERMARK_LIMIT) {
			pbic_debug(pbic, "ran out of bolt slots\n");
			return -ENOSPC;
		}

		pbic->watermark--;
		pbic_set_watermark(pbic);
	}

	slot = pbic->next_slot--;
out:
	pbic_debug(pbic, "allocated slot %d\n", slot);

	return slot;
}

/**
 * pbic_tlb_find_bolted - Search for an entry in the PBIC's TLB that maps addr
 * @pbic: The PBIC to search in.
 * @addr: The address to search for.
 * @tid: The tid the mapping applies to.
 * @ts : The address space to search for (can be TS_ANY)
 *
 * Search @pbic's TLB to find an existing bolted entry mapping @addr for @tid.
 * Actually searches our shadow of the TLB entries, not the actual TLB in the
 * PBIC - but they should be identical.
 *
 * Must be called with the @pbic's lock held.
 */
static int pbic_tlb_find_bolted(struct pbic *pbic, unsigned long addr,
				int tid, int ts)
{
	struct pbic_tlb_info *info;
	int i;

	for (i = pbic->tlb_size - 1; i > pbic->watermark; i--) {
		info = &pbic->tlb_info[i];

		if (info->count && info->address == addr && info->pid == tid
		    && (ts == TS_ANY || info->ts == ts)) {
			pbic_debug(pbic, "matched bolted entry %d\n", i);
			return i;
		}
	}

	return -ENOENT;
}

static void pbic_tlb_write_entry(struct pbic *pbic, struct pbic_tlb_entry *e)
{
	int cc;
	u32 ccw;

	pbic_cxb_recycle(pbic);
	ccw = pbic_pack_ccw(pbic, PBIC_CD_WRITE);
	pbic->crb->subfunction = PBIC_SF_NONE;
	pbic->crb->tlb_entry = *e;

	cc = pbic_crb_execute(pbic, ccw);

	BUG_ON(cc && cc != PBIC_CC_PRESENT);
}

struct pbic_tlb_entry *pbic_tlb_read_entry(struct pbic *pbic, int slot)
{
	struct pbic_tlb_entry *tlbe;
	u32 ccw;

	pbic_cxb_recycle(pbic);
	ccw = pbic_pack_ccw(pbic, PBIC_CD_READ);
	pbic->crb->subfunction = PBIC_SF_READ_INDEX;

	pbic->crb->tlb_entry_index = slot;

	BUG_ON(pbic_crb_execute(pbic, ccw));

	tlbe = &pbic->csb->tlb_entry;

	return tlbe;
}

void pbic_invalidate_slot(struct pbic *pbic, int slot)
{
	u32 ccw;

	pbic_cxb_recycle(pbic);

	ccw = pbic_pack_ccw(pbic, PBIC_CD_WRITE);
	pbic->crb->subfunction = PBIC_SF_NONE;

	pbic->crb->tlb_entry.mas1 = 0;
	pbic->crb->tlb_entry.mas0 = MAS0_ESEL(slot);

	BUG_ON(pbic_crb_execute(pbic, ccw));
}

/**
 * pbic_tlbinv_entry - Invalidate PBIC TLB using the passed pbic_tlb_entry.
 * @pbic: The PBIC's TLB to invalidate.
 * @tlbe: The pbic_tlb_entry to use as the argument of the invalidate.
 *
 * Uses @tlbe to invalidate @pbic's TLB.
 * Must be called with the @pbic's lock held.
 */
static void pbic_tlbinv_entry(struct pbic *pbic, struct pbic_tlb_entry *tlbe)
{
	int cc;
	u32 ccw;

	pbic_cxb_recycle(pbic);

	/* MAS6[SPID] and MAS1[TID] occupy same bits */
	pbic->crb->tlb_search.mas6 = MAS6_SPID & tlbe->mas1;

	/* MAS6[ISIZE] and MAS1[TSIZE] occupy same bits */
	pbic->crb->tlb_search.mas6 |=  MAS6_ISIZE_MASK & tlbe->mas1;

	if (tlbe->mas1 & MAS1_IND)
		pbic->crb->tlb_search.mas6 |= MAS6_SIND;
	if (tlbe->mas1 & MAS1_TS)
		pbic->crb->tlb_search.mas6 |= MAS6_SAS;

	/* The only differ in VF which is reserved in MAS5 */
	pbic->crb->tlb_search.mas5 = tlbe->mas8;

	pbic->crb->tlb_search.mas2 = tlbe->mas2;

	ccw = pbic_pack_ccw(pbic, PBIC_CD_INV);
	pbic->crb->subfunction = PBIC_SF_INV_SEARCH;
	cc = pbic_crb_execute(pbic, ccw);

	BUG_ON(cc && cc != PBIC_CC_NOT_FOUND);
}

/**
 * pbic_tlb_insert - Insert an entry into the PBIC's TLB, possibly bolting.
 * @pbic: The PBIC to insert the entry into.
 * @tlbe: The TLB entry to insert.
 * @flags: Flags that control how the entry is inserted.
 * @refcount: Refcount of the TLB entry if it is being bolted.
 *
 * Inserts @tlbe into @pbic's TLB. If flags contains COPRO_MAP_BOLT, takes
 * care of everything requried to bolt the entry into the TLB.
 *
 * @refcount is used if the entry is bolted, in which case it is the
 * refcount given to the bolted entry.
 *
 * NB. May modify @tlbe under some circumstances.
 */
int pbic_tlb_insert(struct pbic *pbic, struct pbic_tlb_entry *tlbe,
		    int flags, int refcount)
{
	int rc, slot;

	spin_lock(&pbic->lock);

	if (flags & COPRO_MAP_BOLT) {
		unsigned long addr;
		u32 tid = MAS1_TID_GET(tlbe->mas1);
		int tsize = MAS1_TSIZE_GET(tlbe->mas1);
		int ts = !!(tlbe->mas1 & MAS1_TS);

		addr = tlbe->mas2 & MAS2_EPN_MASK(tsize);
		slot = pbic_tlb_find_bolted(pbic, addr, tid, ts);
		if (slot >= 0) {
			pbic->tlb_info[slot].count += refcount;
			rc = 0;	/* Already bolted */
			goto out;
		}

		if (current->mm) {
			rc = mm_context_protect(current->mm);
			if (rc)
				goto out;
		}

		slot = pbic_tlb_reserve_bolting_slot(pbic);
		if (slot < 0) {
			if (current->mm)
				mm_context_unprotect(current->mm);

			rc = slot;
			goto out;
		}

		pbic->tlb_info[slot].count = refcount;
		pbic->tlb_info[slot].address = addr;
		pbic->tlb_info[slot].pid = MAS1_TID_GET(tlbe->mas1);
		if (tlbe->mas1 & MAS1_TS)
			pbic->tlb_info[slot].ts = 1;

		tlbe->mas1 |= MAS1_IPROT;
		tlbe->mas0 = MAS0_ESEL(slot);

		pbic_tlbinv_entry(pbic, tlbe);
	}

	pbic_tlb_write_entry(pbic, tlbe);

	rc = 0;
out:
	spin_unlock(&pbic->lock);

	return rc;
}

static void pbic_tlbinv(struct pbic *pbic, struct mm_struct *mm, int full_flush)
{
	int cc;
	u32 ccw;

	pbic_debug_invalidate(pbic);

	spin_lock(&pbic->lock);

	pbic_cxb_recycle(pbic);
	pbic->crb->tlb_search.mas6 |= (mm->context.id << 16) & MAS6_SPID;

	if (!full_flush)	/* Only invalidate direct entries */
		pbic->crb->tlb_search.mas6 |= MAS6_SIND;

	ccw = pbic_pack_ccw(pbic, PBIC_CD_INV);
	pbic->crb->subfunction = PBIC_SF_INV_LPID_PID;

	cc = pbic_crb_execute(pbic, ccw);

	BUG_ON(cc && cc != PBIC_CC_NOT_FOUND);

	spin_unlock(&pbic->lock);
}

static void pbic_tlbsync(void)
{
	/*
	 * We need to make sure the PBIC load/store queue is flushed
	 * so that no outstanding load/store can be using a translation
	 * we just invalidated.
	 */
	nohash_tlbsync();
}

void copro_mmu_flush_mm(struct mm_struct *mm, int full_flush)
{
	struct pbic *pbic;

	for_each_pbic(pbic) {
		if (mm_used_pbic(pbic, mm))
			pbic_tlbinv(pbic, mm, full_flush);
	}

	pbic_tlbsync();
}

void copro_mmu_flush_entry(struct mm_struct *mm, unsigned long addr,
			   unsigned int pid, unsigned int tsize,
			   unsigned int ind)
{
	struct pbic *pbic;
	struct pbic_tlb_entry tlb_entry;

	for_each_pbic(pbic) {
		if (!mm_used_pbic(pbic, mm))
			continue;

		pbic_tlb_entry_init(&tlb_entry);

		tlb_entry.mas1 |= MAS1_TID(pid);
		tlb_entry.mas1 |= MAS1_TSIZE(tsize);
		if (ind)
			tlb_entry.mas1 = MAS1_IND;

		tlb_entry.mas2 |= addr & MAS2_EPN_MASK(tsize);

		spin_lock(&pbic->lock);

		pbic_tlbinv_entry(pbic, &tlb_entry);

		spin_unlock(&pbic->lock);
	}

	pbic_tlbsync();
}

static inline int pbic_admin(u32 ccw, void *crb, int retry)
{
	int rc;
	int i;

	rc = icswx(ccw, crb);
	i = 0;
	while (rc == EAGAIN && i < retry) {
		/* FIXME - how long should we wait? */
		usleep_range(500, 1000);
		rc = icswx_retry(ccw, crb);
	}
	return rc;
}

static int pbic_suspend(struct pbic *pbic)
{
	u32 ccw;

	pbic_cxb_recycle(pbic);
	ccw = pbic_pack_ccw(pbic, PBIC_CD_SUSPEND);
	pbic->crb->subfunction = PBIC_SF_NONE;

	pbic_debug(pbic, "sending SUSPEND\n");

	return pbic_admin(ccw, pbic->crb, 10);
}

static void pbic_resume(struct pbic *pbic)
{
	u32 ccw;

	pbic_cxb_recycle(pbic);
	ccw = pbic_pack_ccw(pbic, PBIC_CD_RESUME);
	pbic->crb->subfunction = PBIC_SF_NONE;

	pbic_debug(pbic, "sending RESUME\n");

	BUG_ON(pbic_admin(ccw, pbic->crb, 1));
}

void pbic_maintenance(struct work_struct *w)
{
	struct pbic_tlb_entry tlbe;
	struct pbic *pbic;
	int low_slot, i;

	pbic = container_of(w, struct pbic, maint_q.work);

	spin_lock(&pbic->lock);

	/* Check if there are no holes, can happen if a bolt beats us */
	for (i = pbic->tlb_size - 1; i > pbic->watermark; i--)
		if (pbic->tlb_info[i].count != 0)
			break;

	if (i == pbic->watermark)	/* Nothing to do */
		goto out;

	low_slot = pbic->next_slot + 1;

	if (pbic_suspend(pbic)) {
#ifndef CONFIG_WORKAROUND_DD1_PBIC_SUSPEND
		pbic_debug(pbic, "failed to suspend, rescheduling\n");
		schedule_delayed_work(&pbic->maint_q, HZ);
#endif
		goto resume;
	}

	for (i = pbic->tlb_size - 1; i >= low_slot; i--) {
		while (pbic->tlb_info[low_slot].count == 0 && low_slot <= i)
			low_slot++;

		if (pbic->tlb_info[i].count > 0)
			continue;

		if (i > low_slot) {
			pbic_debug(pbic, "moving entry from %d to %d\n",
				   low_slot, i);

			tlbe = *pbic_tlb_read_entry(pbic, low_slot);
			tlbe.mas0 &= ~MAS0_ESEL_MASK;
			tlbe.mas0 |= MAS0_ESEL(i);

			pbic_invalidate_slot(pbic, low_slot);
			pbic_tlb_write_entry(pbic, &tlbe);

			pbic->tlb_info[i] = pbic->tlb_info[low_slot];
			pbic->tlb_info[low_slot].count = 0;
			low_slot++;
		}
	}

	pbic->next_slot = low_slot - 1;
	pbic->watermark = pbic->next_slot;
	pbic_set_watermark(pbic);

	pbic_debug_check_bolted_info(pbic);
resume:
	pbic_resume(pbic);
out:
	spin_unlock(&pbic->lock);
}

static void pbic_flush_bolted_entry(struct pbic *pbic, struct mm_struct *mm,
				    int slot)
{
	pbic_invalidate_slot(pbic, slot);
	pbic->tlb_info[slot].count = 0;

	mm_context_unprotect(mm);

	if (!work_pending(&pbic->maint_q.work))
		schedule_delayed_work(&pbic->maint_q, 0);
}

static void pbic_flush_bolted_entries(struct pbic *pbic, struct mm_struct *mm,
				      unsigned long addr)
{
	int slot;

	pbic_debug(pbic, "flushing addr %#lx for mm %p\n", addr, mm);

	spin_lock(&pbic->lock);

	slot = pbic_tlb_find_bolted(pbic, addr, mm->context.id, TS_ANY);
	if (slot >= 0)
		pbic_flush_bolted_entry(pbic, mm, slot);

	spin_unlock(&pbic->lock);
}

/**
 * copro_mmu_flush_bolted - Flush any bolted TLB entries matching addr & mm
 * @mm: The mm we're flushing.
 * @addr: The address to flush.
 *
 * Searches all PBICs for bolted TLB entries that match @mm & @addr,
 * and flushes them. Makes no attempt to round or align @addr, the
 * caller must make sure @addr matches the address of the TLB entry,
 * ie. is aligned to the PAGE_SIZE, huge page size, or indirect page
 * size.
 */
void copro_mmu_flush_bolted(struct mm_struct *mm, unsigned long addr)
{
	struct pbic *pbic;
	unsigned int tid;

	tid = mm->context.id;

	for_each_pbic(pbic) {
		if (mm_used_pbic(pbic, mm))
			pbic_flush_bolted_entries(pbic, mm, addr);
	}

	pbic_tlbsync();
}

static int calc_nr_pages(unsigned long address, unsigned long end, int shift)
{
	unsigned long range;
	int nr_pages;

	/* The number of base-page-size pages covered by the address range,
	 * up to the limit of the specified shift. */
	range = min(end - address, 1UL << shift);
	nr_pages = range / PAGE_SIZE;

	return max(nr_pages, 1); /* Round up to at least one page */
}

static int insert_indirect_mapping(struct pbic *pbic, pmd_t *pmd,
				   unsigned long address, unsigned long end,
				   int flags)
{
	unsigned long pte_addr;
	struct pbic_tlb_entry tlb_entry;
	int rc, nr_pages, shift;

	pte_addr = __pa(pmd_val(*pmd));

	/*
	 * mmucr3.x = 0
	 * MAS8.tgs = 0
	 * mas8.tlpid = 0
	 * mas0.hes = 0
	 * mas2.i = mas2.g = 0
	 */

	pbic_debug(pbic, "pmd %#lx addr %#lx\n", pte_addr, address);

	pbic_tlb_entry_init(&tlb_entry);
	tlb_entry.mas1 |= MAS1_VALID;
	tlb_entry.mas1 |= MAS1_TID(current->mm->context.id);
	tlb_entry.mas1 |= MAS1_IND;

	tlb_entry.mas0 = MAS0_HES;

	tlb_entry.mas7 = pte_addr >> 32;
	tlb_entry.mas3 = pte_addr & MAS3_RPN;
	tlb_entry.mas3 |= MAS3_SPSIZE(psize_to_pbic_tsize(MMU_PAGE_BASE));

	shift = mmu_psize_defs[MMU_PAGE_BASE].ind;
	tlb_entry.mas1 |= MAS1_TSIZE(shift_to_pbic_tsize(shift));

	nr_pages = calc_nr_pages(address, end, shift);

	tlb_entry.mas2 = MAS2_VAL(address, 1UL << shift, 0);

	rc = pbic_tlb_insert(pbic, &tlb_entry, flags, nr_pages);
	if (rc != 0)
		return rc;

	return shift;
}

static int insert_direct_mapping(struct pbic *pbic, pmd_t *pmd,
				 unsigned long address, unsigned long end,
				 int flags)
{
	struct pbic_tlb_entry tlb_entry;
	struct mm_struct *mm = current->mm;
	unsigned long ra;
	spinlock_t *ptl;
	u32 mask, baps;
	pte_t *ptep;
	int rc;
	int ig = 0;

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);

	pbic_debug(pbic, "ptep %p addr %#lx\n", ptep, address);

	if (!pte_present(*ptep)) {
		pbic_debug(pbic, "pte not present for %#lx\n", address);
		rc = -EFAULT;
		goto out_unlock;
	}

	ra = pte_pfn(*ptep) << PAGE_SHIFT;

	pbic_debug(pbic, "ptep %p pte %#lx ra %#lx\n", ptep, pte_val(*ptep),
		   ra);

	pbic_tlb_entry_init(&tlb_entry);
	tlb_entry.mas1 = MAS1_VALID;
	tlb_entry.mas1 |= MAS1_TID(current->mm->context.id);

	tlb_entry.mas0 = MAS0_HES;

	/* Copy I & G from the PTE into mas2 */
	if (pte_val(*ptep) & _PAGE_NO_CACHE)
		ig |= MAS2_I;
	if (pte_val(*ptep) & _PAGE_GUARDED)
		ig |= MAS2_G;
	tlb_entry.mas2 = MAS2_VAL(address, BOOK3E_PAGESZ_4K, ig);

	tlb_entry.mas7 = ra >> 32;
	tlb_entry.mas3 = ra & MAS3_RPN;

	/* This is messy */
	mask = _PAGE_BAP_UW | _PAGE_BAP_SW | _PAGE_BAP_UR | _PAGE_BAP_SR;
	baps = pte_val(*ptep) & mask;
	tlb_entry.mas3 &= ~(mask >> 2);
	tlb_entry.mas3 |= baps >> 2;

	tlb_entry.mas1 |=
		MAS1_TSIZE((pte_val(*ptep) & _PAGE_PSIZE_MSK) >> 8);

	rc = pbic_tlb_insert(pbic, &tlb_entry, flags, 1);
	if (rc != 0)
		goto out_unlock;

	rc = PAGE_SHIFT;
out_unlock:
	pte_unmap_unlock(ptep, ptl);

	return rc;
}

#ifdef CONFIG_HUGETLB_PAGE
static int insert_huge_indirect_mapping(struct pbic *pbic, hugepd_t *hugepd,
					unsigned pdshift, int psize,
					unsigned long address,
					unsigned long end, int flags)
{
	unsigned long pte_addr;
	struct pbic_tlb_entry tlb_entry;
	int rc, nr_pages, shift;

	shift = mmu_psize_defs[psize].ind;

	pbic_debug(pbic, "hugepd %#lx addr %#lx shift %d\n", hugepd->pd,
		   address, shift);

	pte_addr = __pa(hugepte_offset(hugepd, address, pdshift));

	/*
	 * mmucr3.x = 0
	 * MAS8.tgs = 0
	 * mas8.tlpid = 0
	 * mas0.hes = 0
	 * mas2.i = mas2.g = 0
	 */

	pbic_debug(pbic, "pte_addr %#lx  addr %#lx\n", pte_addr, address);

	pbic_tlb_entry_init(&tlb_entry);
	tlb_entry.mas1 = MAS1_VALID;
	tlb_entry.mas1 |= MAS1_TID(current->mm->context.id);
	tlb_entry.mas1 |= MAS1_IND;
	tlb_entry.mas1 |= MAS1_TSIZE(shift_to_pbic_tsize(shift));

	tlb_entry.mas0 = MAS0_HES;

	tlb_entry.mas7 = pte_addr >> 32;
	tlb_entry.mas3 = pte_addr & MAS3_RPN;
	tlb_entry.mas3 |= MAS3_SPSIZE(psize_to_pbic_tsize(psize));

	nr_pages = calc_nr_pages(address, end, shift);

	tlb_entry.mas2 = MAS2_VAL(address, 1UL << shift, 0);

	rc = pbic_tlb_insert(pbic, &tlb_entry, flags, nr_pages);
	if (rc != 0)
		return rc;

	return shift;
}
#endif

static int insert_huge_mapping(struct pbic *pbic, hugepd_t *hugepd,
			       unsigned pdshift, unsigned long address,
			       unsigned long end, int flags)
{
#ifdef CONFIG_HUGETLB_PAGE
	struct mm_struct *mm = current->mm;
	struct pbic_tlb_entry tlb_entry;
	unsigned long ra, shift;
	int psize;
	unsigned int ind;
	int nr_pages, rc;
	u32 mask, baps;
	pte_t *ptep;

	pbic_debug(pbic, "%#lx\n", address);

	shift = hugepd_shift(*hugepd);
	psize = shift_to_mmu_psize(shift);

	pr_warn("shift %ld, psize %d\n", shift, psize);
	BUG_ON(psize < 0);

	/* If our page tables are laid out such that we can use
	 * an indirect entry, do so */
	ind = mmu_psize_defs[psize].ind;
	pbic_debug(pbic, "ind: %#x pd: %#x\n", ind, pdshift);
	if (ind && (pdshift >= ind))
		return insert_huge_indirect_mapping(pbic, hugepd, pdshift,
						    psize, address, end,
						    flags);

	if (flags & COPRO_MAP_BOLT) {
		pbic_debug(pbic, "can't bolt hugepages with direct mappings\n");
		return -EINVAL;
	}

	spin_lock(&mm->page_table_lock);

	ptep = hugepte_offset(hugepd, address, pdshift);
	if (!ptep) {
		rc = -EFAULT;
		goto out;
	}

	ra = pte_pfn(*ptep) << PAGE_SHIFT;

	pbic_debug(pbic, "ptep %p pte %#lx ra %#lx\n",
		   ptep, pte_val(*ptep), ra);

	pbic_tlb_entry_init(&tlb_entry);
	tlb_entry.mas1 |= MAS1_VALID;
	tlb_entry.mas1 |= MAS1_TID(current->mm->context.id);

	tlb_entry.mas0 = MAS0_HES;

	tlb_entry.mas7 = ra >> 32;
	tlb_entry.mas3 = ra & MAS3_RPN;
	tlb_entry.mas3 |= MAS3_SPSIZE(psize_to_pbic_tsize(MMU_PAGE_BASE));

	/* This is messy */
	mask = _PAGE_BAP_UW | _PAGE_BAP_SW | _PAGE_BAP_UR | _PAGE_BAP_SR;
	baps = pte_val(*ptep) & mask;
	tlb_entry.mas3 &= ~(mask >> 2);
	tlb_entry.mas3 |= baps >> 2;

	tlb_entry.mas1 |= MAS1_TSIZE(shift_to_pbic_tsize(shift));
	/* psize_to_pbic_tsize(psize); */

	nr_pages = calc_nr_pages(address, end, shift);

	tlb_entry.mas2 = MAS2_VAL(address, 1UL << shift, 0);

	rc = pbic_tlb_insert(pbic, &tlb_entry, flags, nr_pages);
	if (rc)
		goto out;

	rc = shift;
out:
	spin_unlock(&mm->page_table_lock);
	return rc;
#else
	BUG();

	return 0;
#endif /* CONFIG_HUGETLB_PAGE */
}

static int insert_mapping(struct pbic *pbic, unsigned long address,
			  unsigned long end, u64 flags)
{
	struct mm_struct *mm = current->mm;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset(mm, address);
	if (is_hugepd(pgd))
		return insert_huge_mapping(pbic, (hugepd_t *)pgd, PGDIR_SHIFT,
					   address, end, flags);

	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd))) {
		pbic_debug(pbic, "fault looking for pgd for %#lx\n", address);
		return -EFAULT;
	}

	pud = pud_offset(pgd, address);
	if (is_hugepd(pud))
		return insert_huge_mapping(pbic, (hugepd_t *)pud, PUD_SHIFT,
					   address, end, flags);

	if (pud_none(*pud) || unlikely(pud_bad(*pud))) {
		pbic_debug(pbic, "fault looking for pud for %#lx\n", address);
		return -EFAULT;
	}

	pmd = pmd_offset(pud, address);
	if (is_hugepd(pmd))
		return insert_huge_mapping(pbic, (hugepd_t *)pmd, PMD_SHIFT,
					   address, end, flags);

	if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd))) {
		pbic_debug(pbic, "fault looking for pmd for %#lx\n", address);
		return -EFAULT;
	}

	if (book3e_htw_enabled)
		return insert_indirect_mapping(pbic, pmd, address, end, flags);
	else {
		if (flags & COPRO_MAP_BOLT) {
			pbic_debug(pbic, "can't bolt when HTW disabled\n");
			return -EINVAL;
		}

		return insert_direct_mapping(pbic, pmd, address, end, flags);
	}
}

static int unbolt_mapping(struct pbic *pbic, unsigned long address,
			  unsigned long end)
{
	int shift, nr_pages, slot;

	if (!book3e_htw_enabled) {
		pbic_debug(pbic, "can't unbolt when HTW disabled\n");
		return -EINVAL;
	}

	shift = mmu_psize_defs[MMU_PAGE_BASE].ind;

	nr_pages = calc_nr_pages(address, end, shift);
	address = _ALIGN_DOWN(address, 1UL << shift);

	pbic_debug(pbic, "address %#lx refcount %d\n", address, nr_pages);

	spin_lock(&pbic->lock);

	slot = pbic_tlb_find_bolted(pbic, address,
				    current->mm->context.id, TS_ANY);
	if (slot < 0)
		goto out;

	pbic->tlb_info[slot].count -= nr_pages;

	pbic_debug(pbic, "slot %d count %d\n", slot,
		   pbic->tlb_info[slot].count);

	if (pbic->tlb_info[slot].count > 0)
		goto out;

	WARN_ON(pbic->tlb_info[slot].count < 0);

	pbic_flush_bolted_entry(pbic, current->mm, slot);
out:
	spin_unlock(&pbic->lock);

	return shift;
}

static int pbic_unmap_entry(struct pbic *pbic, struct copro_map_entry *entry)
{
	unsigned long address, end, size;
	int rc;

	address = entry->addr;
	end = address + entry->len;

	pbic_debug(pbic, "%#lx-%#lx\n", address, end);

	while (address < end) {
		rc = unbolt_mapping(pbic, address, end);
		if (rc < 0)
			goto out;

		/* rc is the shift of the page size we just tried to remove */
		size = 1UL << rc;
		address = _ALIGN_DOWN(address, size);
		address += size;
	}

	rc = 0;
out:
	return rc;
}

int pbic_unmap(struct pbic *pbic, struct copro_map_args *args)
{
	struct copro_map_entry *entries = args->entries;
	struct mm_struct *mm = current->mm;
	int i, rc;

	down_read(&mm->mmap_sem);

	for (i = 0; i < args->count; i++) {
		rc = pbic_unmap_entry(pbic, &entries[i]);
		if (rc)
			goto out;
	}

	rc = 0;
out:
	up_read(&mm->mmap_sem);

	pbic_debug(pbic, "returns %d\n", rc);

	return rc;
}
EXPORT_SYMBOL_GPL(pbic_unmap);

static int pbic_map_entry(struct pbic *pbic,
			  struct copro_map_entry *entry, int flags)
{
	unsigned long address, end, size;
	int rc;

	address = entry->addr;
	end = address + entry->len;

	pbic_debug(pbic, "%#lx-%#lx\n", address, end);

	while (address < end) {
		rc = insert_mapping(pbic, address, end, flags);
		if (rc < 0)
			goto out;

		/* rc is the shift of the page size we just mapped */
		size = 1UL << rc;
		address = _ALIGN_DOWN(address, size);
		if (address < entry->addr)
			entry->addr = address;

		address += size;
	}

	entry->len = address - entry->addr;

	pbic_debug(pbic, "%#llx %#llx\n", entry->addr, entry->len);

	rc = 0;
out:
	return rc;
}

static int contained_in_range(struct copro_map_entry *entry,
			      struct copro_map_entry *range)
{
	return (entry->addr >= range->addr) &&
		(entry->addr + entry->len) <= (range->addr + range->len);
}

static int already_mapped(struct copro_map_entry *entries, int i)
{
	int j;

	for (j = 0; j < i; j++) {
		if (contained_in_range(&entries[i], &entries[j])) {
			cop_debug("range %#llx-%#llx already mapped\n",
				  entries[i].addr,
				  entries[i].addr + entries[i].len);
			return 1;
		}
	}

	return 0;
}

int pbic_map(struct pbic *pbic, struct copro_map_args *args)
{
	struct copro_map_entry *entries = args->entries;
	struct mm_struct *mm = current->mm;
	struct copro_map_args unmap_args;
	int i, rc, bolt;

	pbic_debug(pbic, "mm %p\n", mm);

	/* Copy args because pbic_map_entry() modifies the entries */
	memcpy(&unmap_args, args, sizeof(*args));

	if (!mm_used_pbic(pbic, mm))
		set_mm_used_pbic(pbic, mm);

	bolt = args->flags & COPRO_MAP_BOLT;

	down_read(&mm->mmap_sem);

	for (i = 0; i < args->count; i++) {
		if (!bolt && already_mapped(entries, i))
			continue;

		rc = pbic_map_entry(pbic, &entries[i], args->flags);
		if (rc) {
			unmap_args.count = i;
			goto out;
		}
	}

	rc = 0;
out:
	up_read(&mm->mmap_sem);

	if (rc && bolt)
		pbic_unmap(pbic, &unmap_args);

	pbic_debug(pbic, "returns %d\n", rc);

	return rc;
}
EXPORT_SYMBOL_GPL(pbic_map);

int pbic_map_args_ioctl(struct pbic *pbic, unsigned int cmd, void __user *uptr)
{
	struct copro_map_args args;

	if (copy_from_user(&args, uptr, sizeof(struct copro_map_args)))
		return -EFAULT;

	if (args.count > COPRO_MAP_MAX_COUNT)
		return -EINVAL;

	if (args.flags & (~COPRO_MAP_ALLOWED_FLAGS)) {
		pbic_debug(pbic, "illegal flags %#llx\n", args.flags);
		return -EINVAL;
	}

	pbic_debug(pbic, "%smapping %llu entries\n",
		   cmd == COPRO_IOCTL_MAP ? "" : "un", args.count);

	if (cmd == COPRO_IOCTL_MAP)
		return pbic_map(pbic, &args);
	else
		return pbic_unmap(pbic, &args);
}
EXPORT_SYMBOL_GPL(pbic_map_args_ioctl);
