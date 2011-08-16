/*
 * Copyright 2008 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/hugetlb.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/workqueue.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/firmware.h>
#include <linux/io.h>

#include <asm/pgtable.h>
#include <asm/scom.h>

#include "cop.h"
#include "pbic.h"
#include "pbic_cop.h"


/* For now we just use a ulong bitmap to track PBICs */
#define MAX_PBICS	BITS_PER_LONG
static int __initdata pbic_next_index;

/* OF data */
#define PBIC_NAME	"wsp-pbic"

struct list_head pbic_list;

#ifdef CONFIG_USE_256M_64K_FOR_BOLTED
/* 256M / 64K for testing until we can boot with > 64G */
#define LINEAR_PSIZE		0x10000000UL
#define LINEAR_PSIZE_MAS1	BOOK3E_PAGESZ_256M
#define LINEAR_SPSIZE_SHIFT	16
#define LINEAR_SPSIZE_PTE	_PAGE_PSIZE_64K
#define LINEAR_SPSIZE_MAS3	BOOK3E_PAGESZ_64K
#else /* CONFIG_USE_256M_64K_FOR_BOLTED */
/* We use 64G indirect pages to map the linear mapping */
#define LINEAR_PSIZE		0x1000000000UL
#define LINEAR_PSIZE_MAS1	BOOK3E_PAGESZ_64GB

/* With a 16M subpage size */
#define LINEAR_SPSIZE_SHIFT	24

/* These must match LINEAR_SPSIZE */
#define LINEAR_SPSIZE_PTE	_PAGE_PSIZE_16M
#define LINEAR_SPSIZE_MAS3	BOOK3E_PAGESZ_16M
#endif  /* CONFIG_USE_256M_64K_FOR_BOLTED */

#define LINEAR_SPSIZE		(1 << LINEAR_SPSIZE_SHIFT)
#define LINEAR_SPSIZE_MASK	(~(LINEAR_SPSIZE - 1))

/* The size of our PTE page to map the above */
#define LINEAR_PTE_SIZE		((LINEAR_PSIZE / LINEAR_SPSIZE) * sizeof(pte_t))

static int __init
pbic_bolt_linear_mapping_page(unsigned long addr, pte_t *pte_p)
{
	struct pbic_tlb_entry tlb_entry;
	struct pbic *pbic;
	unsigned long end;
	pgprot_t prot;

	pbic_tlb_entry_init(&tlb_entry);
	tlb_entry.mas0 = 0;		/* Use non-LRU slots */
	tlb_entry.mas1 = MAS1_VALID |  MAS1_IPROT | MAS1_TID(0) | MAS1_IND;
	tlb_entry.mas1 |= MAS1_TSIZE(LINEAR_PSIZE_MAS1);
	tlb_entry.mas2 = MAS2_VAL((ulong)__va(addr), LINEAR_PSIZE_MAS1, 0);
	tlb_entry.mas7 = __pa(pte_p) >> 32;
	tlb_entry.mas3 = __pa(pte_p) & (ulong)MAS3_RPN;
	tlb_entry.mas3 |= MAS3_SPSIZE(LINEAR_SPSIZE_MAS3);

	prot = __pgprot(_PAGE_KERNEL_RW | _PAGE_PRESENT | _PAGE_ACCESSED |
			_PAGE_COHERENT | LINEAR_SPSIZE_PTE);

	end = min(addr + LINEAR_PSIZE, linear_map_top);
	while (addr < end) {
		if (pfn_valid(addr >> PAGE_SHIFT))
			*(pte_p++) = pfn_pte(addr >> PAGE_SHIFT, prot);

		addr += LINEAR_SPSIZE;
	}

	/* FIXME only need to bolt for some PBICs ? */
	for_each_pbic(pbic) {
		pbic_tlb_insert(pbic, &tlb_entry, COPRO_MAP_BOLT,
				LINEAR_PSIZE / PAGE_SIZE);
	}

	return 0;
}

static int __init pbic_bolt_linear_mapping(void)
{
	unsigned long addr, pg, mask;
	int rc, order;

	if (!book3e_htw_enabled) {
		pr_warning("%s: HTW disabled, not bolting!\n", __func__);
		return 0;
	}

	order = get_order(LINEAR_PTE_SIZE);
	mask = (1ul << order) - 1;

	/* Indirect pages must be naturally aligned. We thus allocate
	 * to the nearest page order using the buddy allocator and
	 * divide the resulting allocation into chunks of the size
	 * of an indirect page.
	 */
	for (addr = 0, pg = 0; addr < linear_map_top;
	     addr += LINEAR_PSIZE, pg += LINEAR_PTE_SIZE) {
		if ((pg & mask) == 0) {
			pg = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
			if (pg == 0) {
				pr_err("%s: Can't allocate memory for PTEs\n",
				       __func__);
				return -ENOMEM;
			}
		}

		rc = pbic_bolt_linear_mapping_page(addr, (pte_t *)pg);
		if (rc)
			return rc;
	}

	return 0;
}

void pbic_configure_marker_trace(struct pbic *pbic,
				 struct copro_instance *copro,
				 int enable, u64 flags)
{
	unsigned int offset;
	u64 val;

	/* FIXME Got to be a better way .. */
	if (of_device_is_compatible(copro->dn, "ibm,cmpx") ||
	    of_device_is_compatible(copro->dn, "ibm,regx"))
		offset = 0x1d8;
	else if (of_device_is_compatible(copro->dn, "ibm,admx") ||
		 of_device_is_compatible(copro->dn, "ibm,xmlx"))
		offset = 0x1e0;
	else if (of_device_is_compatible(copro->dn, "ibm,sacx"))
		offset = 0x1e8;
	else if (of_device_is_compatible(copro->dn, "ibm,aacx"))
		offset = 0x1f0;
	else if (of_device_is_compatible(copro->dn, "ibm,rngx"))
		offset = 0x1f8;
	else {
		WARN(1, "%s: No matching copro found", __func__);
		return;
	}

	val  = in_be64(pbic->mmio_addr + offset);
	val &= ~0x1cUL;
	val |= (flags << 3) | (!!enable << 2);

	pbic_debug(pbic, "writing 0x%llx to 0x%x\n", val, offset);

	out_be64(pbic->mmio_addr + offset, val);
}

static struct device_node *find_mmux(struct device_node *parent)
{
	struct device_node *child;

	for_each_child_of_node(parent, child)
		if (of_device_is_compatible(child, "ibm,mmux"))
			return child;

	cop_debug("%s no mmux found\n", parent->full_name);
	return NULL;
}

#define PBIC_SPILLQ_ADDR_MASK	0x3fffffff000UL
#define PBIC_SPILLQ_SIZE_MASK	0xffUL

static int pbic_spill_fill_enable;

static int __init setup_pbic_spill(char *__unused)
{
	pbic_spill_fill_enable = 1;

	return 1;
}
__setup("pbic_spill", setup_pbic_spill);

int pbic_alloc_spill_queue(struct copro_unit *unit, int qnum)
{
	struct pbic_spillq *psq;
	unsigned long addr;
	u64 val;

	if (!pbic_spill_fill_enable) {
		pr_info("%s: spill/fill disabled for %s under %s\n",
			__func__, unit->name, unit->pbic->name);
		return 0;
	}

	/* Unit A (0) gets two queues, the other 11 go to B */
	if ((unit->number == 0 && qnum > 1) || (qnum > 10)) {
		pr_err("%s: queue number %d out of range\n", __func__, qnum);
		return -EINVAL;
	}

	psq = kzalloc(sizeof(*psq), GFP_KERNEL);
	if (!psq) {
		pr_err("%s: no memory for struct\n", __func__);
		return -ENOMEM;
	}

	/* Must be naturally aligned - and is as long as it's one page */
	addr = get_zeroed_page(GFP_KERNEL);
	if (addr == 0) {
		pr_err("%s: no memory for spill queue\n", __func__);
		kfree(psq);
		return -ENOMEM;
	}

	if (unit->number == 1)
		qnum += 2;	/* Unit B regs are shifted by 2 */

	psq->reg_offset = 0x100 + ((qnum) * 8 * 2);
	psq->buffer = (void *)addr;

	pr_debug("copro: spillq %d size %#lx at %p for %s under %s\n",
		 qnum, PAGE_SIZE, psq->buffer, unit->name, unit->pbic->name);

	/* Size is encoded as number of 4K pages */
	val  = ((PAGE_SIZE >> 12) & PBIC_SPILLQ_SIZE_MASK);
	val |= (__pa(addr) & PBIC_SPILLQ_ADDR_MASK);

#ifdef CONFIG_WORKAROUND_ERRATUM_457
	/* Erratum 457, PBIC - Spill Fill for XML does not Work Correctly.
	 * Workaround by disabling pbic spill/fill queue.
	 */
	out_be64(unit->pbic->mmio_addr + psq->reg_offset, 0);
	out_be64(unit->pbic->mmio_addr + psq->reg_offset + 8, 0);
	return 0;
#endif

	out_be64(unit->pbic->mmio_addr + psq->reg_offset, val);
	out_be64(unit->pbic->mmio_addr + psq->reg_offset + 8, 0);

	return 0;
}

static int __init pbic_probe(struct platform_device *device)
{
	struct device_node *mmux;
	struct device_node *dn;
	void __iomem *mmio_addr;
	int tlb_size, len;
	struct pbic *pbic;
	const u32 *p;
	u32 ct, ci;
	u64 val;
	int i;

	dn = device->dev.of_node;

	mmux = find_mmux(dn);

	if (read_coprocessor_reg(mmux, &ct, &ci)) {
		dev_err(&device->dev, "invalid/missing ibm,coprocessor-reg\n");
		return -EINVAL;
	}

	mmio_addr = of_iomap(dn, 0);
	if (mmio_addr == NULL) {
		dev_err(&device->dev, "couldn't iomap reg\n");
		return -EFAULT;
	}

	if (pbic_next_index >= MAX_PBICS) {
		dev_err(&device->dev, "ran out of PBIC index space!\n");
		return -ENOSPC;
	}

	p = of_get_property(mmux, "tlb-size", &len);
	if (!p || len < sizeof(u32)) {
		dev_err(&device->dev, "missing/invalid tlb-size!\n");
		return -EINVAL;
	}

	tlb_size = *p;
	if (!tlb_size || tlb_size > PBIC_MAX_TLB_SIZE) {
		dev_err(&device->dev, "tlb-size (%d) out of range\n", tlb_size);
		return -EINVAL;
	}

	pbic = kzalloc(sizeof(*pbic), GFP_KERNEL);
	if (!pbic)
		return -ENOMEM;

	pbic->tlb_info = kzalloc(sizeof(struct pbic_tlb_info) * tlb_size,
				 GFP_KERNEL);
	pbic->crb = cop_cxb_alloc(GFP_KERNEL);
	pbic->csb = cop_cxb_alloc(GFP_KERNEL);

	if (!pbic->crb || !pbic->csb || !pbic->tlb_info) {
		/* These are all NULL if not initialised, so safe to kfree */
		kfree(pbic->tlb_info);
		kfree(pbic->crb);
		kfree(pbic->csb);
		kfree(pbic);
		return -ENOMEM;
	}

	snprintf(pbic->name, sizeof(pbic->name), "pbic%d", ci);
	pbic->tlb_size = tlb_size;
	pbic->index = pbic_next_index++;
	pbic->type = ct;
	pbic->instance = ci;
	pbic->mmio_addr = mmio_addr;
	pbic->dn = of_node_get(dn);

	pbic_attach_csb(pbic->crb, pbic->csb);

	/* Enable xlate logging on both units */
	val = in_be64(pbic->mmio_addr + 8);
	val |= 0x0020002000000000ul;
	out_be64(pbic->mmio_addr + 8, val);

	/* Invalidate the entire TLB before setting the watermark */
	out_be64(pbic->mmio_addr + PBIC_WATERMARK_REG, 1ul << 63);

	/* Turn off all TCR registers except the match alls */
	out_be64(pbic->mmio_addr + 0xc0, 0x80000000ul);
	out_be64(pbic->mmio_addr + 0xf0, 0x80000000ul);
	for (i = 0; i < 4; i++) {
		out_be64(pbic->mmio_addr + 0xd0 + (i * 8), 0);
		out_be64(pbic->mmio_addr + 0xa0 + (i * 8), 0);
	}

	pbic->next_slot = pbic->tlb_size - 1;
	pbic->watermark = pbic->next_slot;
	pbic_set_watermark(pbic);

	spin_lock_init(&pbic->lock);

	INIT_DELAYED_WORK(&pbic->maint_q, pbic_maintenance);

	dev_set_drvdata(&device->dev, pbic);

	pbic_debugfs_init_pbic(pbic);

	list_add_tail(&pbic->list, &pbic_list);

	dev_printk(KERN_DEBUG, &device->dev,
		   "initialised %s with CT/CI 0x%x/0x%x\n",
		   pbic->name, pbic->type, pbic->instance);

	return 0;
}

static const struct of_device_id pbic_device_id[] = {
	{ .compatible	= PBIC_COMPATIBLE },
	{}
};

static struct platform_driver pbic_driver = {
	.probe		= pbic_probe,
	.driver		= {
		.name	= PBIC_NAME,
		.owner	= THIS_MODULE,
		.of_match_table	= pbic_device_id,
	},
};

int __init pbic_driver_init(void)
{
	int rc;

	INIT_LIST_HEAD(&pbic_list);
	rc = platform_driver_register(&pbic_driver);
	if (rc)
		return rc;

	rc = pbic_bolt_linear_mapping();
	if (rc)
		return rc;

	pbic_debugfs_init();

	return 0;
}

struct pbic *pbic_get_parent_pbic(struct device_node *dn)
{
	struct platform_device *device;

	dn = of_node_get(dn);
	while (dn && !of_device_is_compatible(dn, PBIC_COMPATIBLE))
		dn = of_get_next_parent(dn);

	if (!dn)
		return NULL;

	device = of_find_device_by_node(dn);
	of_node_put(dn);

	if (!device || !dev_get_drvdata(&device->dev))
		return NULL;

	get_device(&device->dev);
	return dev_get_drvdata(&device->dev);
}
EXPORT_SYMBOL_GPL(pbic_get_parent_pbic);

void pbic_put_parent_pbic(struct pbic *pbic)
{
	struct platform_device *device;

	device = of_find_device_by_node(pbic->dn);
	if (WARN_ON(!device))
		return;

	put_device(&device->dev);
}
EXPORT_SYMBOL_GPL(pbic_put_parent_pbic);

/*
 * The virtual address of the CSB is passed in because the CSB address
 * in the CRB is physical when we talk to the PBIC
 */
int pbic_icswx(struct pbic *pbic, u32 ccw, struct pbic_csb *csb)
{
	unsigned long timeout;
	int rc;

	/* How long is too long? */
	timeout = jiffies + 5 * HZ;

	rc = icswx(ccw, pbic->crb);
	while (rc == -EAGAIN) {
		if (time_after(jiffies, timeout))
			break;
		rc = icswx_retry(ccw, pbic->crb);
	}

	if (rc) {
		cop_debug("%s rc %d from icswx\n", pbic->name, rc);
		BUG();
	}

	if (csb_wait_valid(csb)) {
		cop_debug("%s timeout waiting for csb p=%llx v=%p\n",
			  pbic->name, pbic->crb->crb_csb, csb);
		BUG();
	}

	return csb->cc;
}

int pbic_crb_execute(struct pbic *pbic, u32 ccw)
{
	struct pbic_csb *csb;
	int rc, retry = 0;

	csb = __va(COP_CRB_CSB_ADDR(pbic->crb->crb_csb));

	rc = pbic_icswx(pbic, ccw, csb);
	while (rc == 128 && retry++ < 5) {
		csb->valid = 0;
		rc = pbic_icswx(pbic, ccw, csb);
	}

	return rc;
}
