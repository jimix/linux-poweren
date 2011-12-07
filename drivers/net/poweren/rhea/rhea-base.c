/*
 * Copyright (C) 2011, 2012 IBM Corporation.
 * Author:	Davide Pasetto <pasetto_davide@ie.ibm.com>
 *			Karol Lynch <karol_lynch@ie.ibm.com>
 *			Kay Muller <kay.muller@ie.ibm.com>
 *			Jimi Xenidis <jimix@watson.ibm.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http:/www.gnu.org/licenses/>.
 */

#include "rhea-base.h"

const char *rhea_priv_mode_str[] = {
	"HEA_PRIV_NO",
	"HEA_PRIV_SUPER",
	"HEA_PRIV_PRIV",
	"HEA_PRIV_USER"
};

static void rhea_itsn_reset(struct rhea_gen_its *its)
{
	int i;

	if (NULL == its)
		return;

	for (i = 0; i < ARRAY_SIZE(its->g_its); ++i)
		out_be64(&its->g_its[i], 0x0ULL);
}


static void rhea_thaba_reset(struct rhea_gen_thmbx *thmbx)
{
	int i;
	u64 reg;

	if (NULL == thmbx)
		return;

	reg = 0x0ULL;
	for (i = 0; i < ARRAY_SIZE(thmbx->g_thmbx); ++i)
		out_be64(&thmbx->g_thmbx[i], reg);
}

static void rhea_base_reset(struct rhea_gen_base *base)
{
	u64 reg;
	int i;

	if (NULL == base)
		return;

	/* HEA control registers */
	reg = 0x0ULL;
	reg = hea_set_u64_bits(reg, 128, 24, 31);
	reg = hea_set_u64_bits(reg, 4, 51, 55);
	reg = hea_set_u64_bits(reg, 3, 41, 42);
	out_be64(&base->g_heac, reg);

	/* reset base registers */
	reg = 0x0ULL;
	reg = hea_set_u64_bits(reg, ~0ULL, 18, 51);
	out_be64(&base->g_eqpba, reg);
	out_be64(&base->g_eqsba, reg);

	out_be64(&base->g_cqpba, reg);
	out_be64(&base->g_cqsba, reg);
	out_be64(&base->g_cquba, reg);

	out_be64(&base->g_qppba, reg);
	out_be64(&base->g_qpsba, reg);
	out_be64(&base->g_qpuba, reg);

	out_be64(&base->g_thaba, reg);
	out_be64(&base->g_buidbase, 0x0ULL);

	/* Q table size registers */
	reg = 0x0ULL;
	out_be64(&base->g_cqtsz, reg);
	out_be64(&base->g_eqtsz, reg);
	out_be64(&base->g_qptsz, reg);

	/* probability discard registers */
	reg = 0x0ULL;
	reg = hea_set_u64_bits(reg, 2, 36, 39);
	reg = hea_set_u64_bits(reg, 4, 44, 47);
	reg = hea_set_u64_bits(reg, 8, 52, 55);
	reg = hea_set_u64_bits(reg, 14, 60, 63);
	out_be64(&base->g_edp, reg);

	/* NN registers */
	reg = 0x0ULL;
	out_be64(&base->g_cuba, reg);

	reg = 0x0ULL;
	reg = hea_set_u64_bits(reg, 128, 8, 15);
	reg = hea_set_u64_bits(reg, 3, 45, 47);
	out_be64(&base->g_nnc, reg);

	/* reset First Affiliated Error Capture Registers */
	reg = 0x0ULL;
	for (i = 0; i < ARRAY_SIZE(base->g_faec); ++i)
		out_be64(&base->g_faec[i], reg);

	/* reset First UnAffiliated Error Capture Register */
	reg = 0x0ULL;
	for (i = 0; i < ARRAY_SIZE(base->g_fuec); ++i)
		out_be64(&base->g_fuec[i], reg);

	/* Timestamp Increment Register */
	reg = 0x0ULL;
	reg = hea_set_u64_bits(reg, 0x06, 0, 4);
	reg = hea_set_u64_bits(reg, 0xDF37F675, 5, 36);
	out_be64(&base->g_timeinc, reg);

	/* UnAffiliated Asynchronous Event Log Register */
	reg = 0x0ULL;
	/* reset all the error flags */
	reg = hea_set_u64_bits(reg, ~0x0ULL, 0, 20);
	out_be64(&base->g_uaelog, reg);
}

void rhea_gen_reset(struct rhea_gen *gen)
{
	if (NULL == gen)
		return;

	rhea_base_reset(&gen->base);

	rhea_itsn_reset(&gen->its);

	rhea_thaba_reset(&gen->thmbx);
}

int rhea_ql_alloc_init(struct rhea_qlist *ql, unsigned qn)
{
	ql->q = rhea_alloc(sizeof(*ql->q) * qn, GFP_KERNEL);
	if (NULL == ql->q)
		return -ENOMEM;

	ql->max = qn;
	ql->alloced = 0;

	spin_lock_init(&ql->lock);

	return 0;
}

void rhea_ql_alloc_fini(struct rhea_qlist *ql)
{
	rhea_free(ql->q, sizeof(*ql->q));

	memset(ql, 0, sizeof(*ql));
}

int rhea_ql_alloc(struct rhea_qlist *ql, void *q)
{
	unsigned id;

	spin_lock(&ql->lock);

	if (ql->alloced >= ql->max) {
		spin_unlock(&ql->lock);
		rhea_error("Max number of Qs have been "
			"allocated: %d", ql->max);
		return -ENOMEM;
	}

	for (id = 0; id < ql->max; id++) {
		if (ql->q[id] == NULL) {
			ql->q[id] = q;
			break;
		}
	}

	if (id >= ql->max) {
		spin_unlock(&ql->lock);
		rhea_error("no empty slot found");
		return -ENOMEM;
	}

	++ql->alloced;

	spin_unlock(&ql->lock);

	return id;
}

void *rhea_ql_get(struct rhea_qlist *ql, unsigned int id)
{
	if (NULL != ql->q[id])
		return ql->q[id];
	else
		return NULL;
}

void rhea_ql_free(struct rhea_qlist *ql, unsigned id)
{

	if (id >= ql->max) {
		rhea_warning("Warning: bad QList id: %d", id);
		return;
	}

	spin_lock(&ql->lock);

	ql->q[id] = NULL;

	--ql->alloced;

	spin_unlock(&ql->lock);
}

void *rhea_qte(const struct rhea_qinfo *qi, unsigned id,
	       enum rhea_memory_type addr_type)
{
	ulong addr;

	if (id > qi->base.page_count)
		return NULL;

	if (RHEA_ADDRESS_VIRTUAL == addr_type)
		addr = (ulong)qi->base.va;
	else
		addr = qi->base.pa;

	addr += id * qi->os_page_sz;

	return (void *)addr;
}



u64 rhea_q_qbase_init(struct rhea_qinfo *qi,  unsigned num,
			    enum hea_priv_mode priv,
			    int lg, u64 addr,
			    const char *str_q, u64 *addr_new)
{
	unsigned size;
	char str_map_name[64] = { 0 };

	BUG_ON(NULL == qi);
	BUG_ON(NULL == str_q);
	BUG_ON(NULL == addr_new);

	/* Setup the base register */
	if (lg == 1)
		qi->os_page_sz = 64 << 10;
	else
		qi->os_page_sz = PAGE_SIZE;

	/* set device page size */
	qi->dev_page_sz = RHEA_DEVICE_PAGE_SIZE;

	/* make sure that the addr is page aligned */
	*addr_new = ALIGN_UP(addr, qi->os_page_sz);

	/* get size */
	size = (qi->os_page_sz * num);

	/* set large page bit */
	if (lg == 1)
		*addr_new = hea_set_u64_bits(*addr_new, 1, 0, 0);

	/* create name for memory region */
	snprintf(str_map_name, sizeof(str_map_name) - 1,
		 "%s_%s", str_q, rhea_priv_mode_str[priv]);

	qi->base.va = rhea_ioremap(str_map_name, *addr_new, size);
	if (NULL == qi->base.va) {
		/* reset base structure */
		memset(&qi->base, 0, sizeof(qi->base));
		rhea_error("Could not map Q");
		return -ENOMEM;
	}

	/* save rest of values */
	qi->base.pa = *addr_new;
	qi->base.size = size;
	qi->base.page_count = num;

	return size;
}

void rhea_q_qbase_fini(struct rhea_qinfo *qi)
{
	if (NULL == qi)
		return;

	rhea_iounmap(qi->base.va, qi->base.pa, qi->base.size);
	memset(&qi->base, 0, sizeof(qi->base));
}

int rhea_pt_alloc(struct rhea_qinfo *qi,
		  struct rhea_mem *pt, struct rhea_mem *q,
		  unsigned size, unsigned hw_mng, unsigned auto_toggling)
{
	int rc = 0;

	/* number of pages for Queue */
	unsigned pgs_q;
	/* number of pages for page table */
	unsigned pgs_pt;

	unsigned pg;

	ulong align;

	if (NULL == qi || NULL == q || NULL == pt || 0 == size) {
		rhea_error("Invalid parameters passed in");
		return -EINVAL;
	}

	pgs_q = ALIGN_UP(size, qi->dev_page_sz);
	pgs_q /= qi->dev_page_sz;

	/* align with big page size and divide by smaller */
	pgs_pt = ALIGN_UP(size, qi->dev_page_sz);
	pgs_pt /= qi->dev_page_sz;

	/* save number of pages */
	q->page_count = pgs_q;
	pt->page_count = pgs_pt;

	/* set size */
	q->size = q->page_count * qi->dev_page_sz;
	pt->size = sizeof(*pt->va) * (pt->page_count + 1);

	if (hw_mng) {
		/* natural alignment */
		align = pgs_pt * qi->dev_page_sz;
	} else {
		align = qi->dev_page_sz;
	}

	if (hw_mng) {
		/* need to allocate a memory for the QPs here */
		q->va = rhea_align_alloc(q->size, align, __GFP_DMA);
		if (q->va == NULL)
			return -ENOMEM;
	} else {
		/* need to allocate a memory for the QPs here */
		q->va = rhea_pages_alloc(q->size, __GFP_DMA);
		if (q->va == NULL)
			return -ENOMEM;
	}

	/* get physical address */
	q->pa = virt_to_phys(q->va);

	if (!hw_mng) {
		pt->va = rhea_pages_alloc(pt->size, __GFP_DMA);
		if (NULL == pt->va) {
			rhea_pages_free(q->va, q->size);
			memset(q, 0, sizeof(*q));
			return -ENOMEM;
		}

		/* get physical address for page table address */
		pt->pa = virt_to_phys(pt->va);

		/* set up the PTP */
		for (pg = 0; pg < pt->page_count; pg++) {
			ulong offset = pg * qi->dev_page_sz;

			void *page_va = (void *)((ulong)q->va + offset);
			ulong page_pa = q->pa + offset;

			memset(page_va, 0, qi->dev_page_sz);

			pt->va[pg] = page_pa;
		}

		/* set the link bit */
		if (auto_toggling)
			pt->va[pg] = pt->pa | 3;
		else
			pt->va[pg] = pt->pa | 1;
	}

	return rc;
}

int rhea_pt_free(struct rhea_mem *pt, struct rhea_mem *q, unsigned hw_mng)
{
	if (pt && !hw_mng) {
		rhea_pages_free(pt->va, pt->size);
		memset(pt, 0, sizeof(*pt));
	}

	if (q && hw_mng) {
		rhea_align_free(q->va, q->size);
		memset(q, 0, sizeof(*q));
	} else {
		rhea_pages_free(q->va, q->size);
		memset(q, 0, sizeof(*q));
	}

	return 0;
}

void rhea_gen_dump(struct rhea_gen *gen)
{
	struct rhea_gen_base *gb = &gen->base;
	struct rhea_gen_thmbx *tb = &gen->thmbx;

	rhea_reg_print(gb, g_gba, "G_GBA");
	rhea_reg_print(gb, g_nid, "G_NID");
	rhea_reg_print(gb, g_vid, "G_VID");
	rhea_reg_print(gb, g_heacap, "G_HEACAP");
	rhea_reg_print(gb, g_bimac, "G_BIMAC");

	rhea_reg_print(gb, g_qpsba, "G_QPSBA");
	rhea_reg_print(gb, g_qppba, "G_QPPBA");
	rhea_reg_print(gb, g_qpuba, "G_QPUBA");
	rhea_reg_print(gb, g_qptsz, "G_QPTSZ");
	rhea_reg_print(gb, g_qpciac, "G_QPCIAC");

	rhea_reg_print(gb, g_cqsba, "G_CQSBA");
	rhea_reg_print(gb, g_cqpba, "G_CQPBA");
	rhea_reg_print(gb, g_cquba, "G_CQUBA");
	rhea_reg_print(gb, g_cqtsz, "G_CQTSZ");

	rhea_reg_print(gb, g_eqsba, "G_EQSBA");
	rhea_reg_print(gb, g_eqpba, "G_EQPBA");
	rhea_reg_print(gb, g_eqtsz, "G_EQTSZ");

	rhea_reg_print(gb, g_buidbase, "G_BUIDBASE");
	rhea_reg_print(gb, g_heac, "G_HEAC");
	rhea_reg_print(gb, g_uaelog, "G_UAELOG");

	rhea_reg_print(gb, g_thaba, "G_THABA");
	rhea_reg_print(tb, g_thmbx[0], "G_THMBX[%02d]", 0);
	rhea_reg_print(tb, g_thmbx[1], "G_THMBX[%02d]", 1);
	rhea_reg_print(tb, g_thmbx[63], "G_THMBX[%02d]", 63);

	rhea_reg_print(gb, g_nnc, "G_NNC");
	rhea_reg_print(gb, g_cuba, "G_CUBA");

}
