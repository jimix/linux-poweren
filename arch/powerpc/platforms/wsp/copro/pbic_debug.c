/*
 * Copyright 2008-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/io.h>

#include <asm/syscalls.h>
#include <asm/machdep.h>
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>

#include <mm/icswx.h>

#include "cop.h"
#include "pbic.h"
#include "pbic_cop.h"


#ifdef DEBUG

void pbic_debug_invalidate(struct pbic *pbic)
{
	/* Not atomic, meh. */
	pbic->invalidate_counter++;
}

void pbic_debug_check_bolted_info(struct pbic *pbic)
{
	int i;

	for (i = 0; i <= pbic->next_slot; i++)
		if (pbic->tlb_info[i].count != 0)
			cop_debug("%s TLB slot %d should be free!\n",
				   pbic->name, i);

	for (i = pbic->next_slot + 1; i < pbic->tlb_size; i++)
		if (pbic->tlb_info[i].count == 0)
			cop_debug("%s TLB slot %d should be used!\n",
				   pbic->name, i);
}

static void entry_maps_range(struct pbic_tlb_entry *tlbe, unsigned long *start,
			     unsigned long *end, int nr)
{
	ulong ent_start, ent_end, new_start, new_end;
	ulong sz;

	ent_start = tlbe->mas2 & MAS2_EPN;

	sz = 1UL << (MAS1_TSIZE_GET(tlbe->mas1) * 2 + 10);
	ent_end = ent_start + sz - 1;

	new_end = *end;
	if (*end <= ent_end && *end >= ent_start)
		new_end = ent_start - 1;

	new_start = *start;
	if (*start >= ent_start && *start < ent_end)
		new_start = ent_end + 1;

	cop_debug("%d 0x%lx-0x%lx ?= 0x%lx-0x%lx -> 0x%lx-0x%lx\n",
		  nr, ent_start, ent_end, *start, *end,
		  new_start, new_end);

	*start = new_start;
	*end = new_end;
}

static int __pbic_range_mapped(struct pbic *pbic, unsigned long start,
			       unsigned long end, int tid, int bolted)
{
	int i, j;
	u32 ccw;

	cop_debug("checking 0x%lx-0x%lx for %smappings\n",
		  start, end, bolted ? "bolted " : "");

	spin_lock(&pbic->lock);

	pbic_cxb_recycle(pbic);
	ccw = pbic_pack_ccw(pbic, 1);
	pbic->crb->subfunction = 0;
	pbic->crb->subfunction = 0;	/* by index */

	j = bolted ? pbic->watermark + 1 : 0;

	for (i = j; i < pbic->tlb_size; i++) {
		pbic->crb->tlb_entry_index = i;

		pbic->csb->valid = 0;
		BUG_ON(pbic_crb_execute(pbic, ccw));

		if (!(pbic->csb->tlb_entry.mas1 & MAS1_VALID))
			continue;

		if (MAS1_TID_GET(pbic->csb->tlb_entry.mas1) != tid)
			continue;

		if (bolted && !(pbic->csb->tlb_entry.mas1 & MAS1_IPROT))
			continue;

		entry_maps_range(&pbic->csb->tlb_entry, &start, &end, i);
	}

	spin_unlock(&pbic->lock);

	return start >= end;
}

static int pbic_range_mapped(struct pbic *pbic, unsigned long start,
			     unsigned long end)
{
	return __pbic_range_mapped(pbic, start, end,
				   current->mm->context.id, 0);
}

static int pbic_range_bolted(struct pbic *pbic, unsigned long start,
			     unsigned long end)
{
	return __pbic_range_mapped(pbic, start, end,
				   current->mm->context.id, 1);
}

static unsigned long mmap(unsigned long size)
{
	long rc;

	rc = sys_mmap(0, size, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (rc < 0) {
		cop_debug("mmap failed %ld\n", rc);
		return 0;
	}

	return rc;
}

static void add_entry(struct copro_map_args *args, unsigned long addr,
		      unsigned long len)
{
	args->entries[args->count].addr = addr;
	args->entries[args->count].len = len;
	args->count++;
	args->entries[args->count].addr = 0;
	args->entries[args->count].len = 0;
}

#define TEST_SETUP_FAIL	512
#define TEST_FAIL	513

/* pbic_map() modifies it's argument, which is naughty but saves an alloc
 * and is usually safe - because it's called from the userspace API which
 * has just copied the args and never passes them back. For us it's a pain
 * so add a wrapper to copy the args before calling.
 */
static int pbic_map_wrapper(struct pbic *pbic, struct copro_map_args *args)
{
	struct copro_map_args *copy;
	int rc;

	copy = kmalloc(sizeof(*copy), GFP_KERNEL);
	if (!copy)
		return -ENOMEM;

	memcpy(copy, args, sizeof(*args));

	rc = pbic_map(pbic, copy);

	kfree(copy);

	return rc;
}

/* Helper for mmapping a page and mapping it */
static int setup_test_mapping(struct pbic *pbic, struct copro_map_args *args)
{
	int rc, status = TEST_FAIL;
	unsigned long addr;
	char *p;

	memset(args, 0, sizeof(*args));

	addr = mmap(PAGE_SIZE);
	if (!addr)
		return TEST_SETUP_FAIL;

	cop_debug("mapped at 0x%lx\n", addr);

	args->count = 0;
	args->flags = 0;
	add_entry(args, addr, PAGE_SIZE);

	if (pbic_range_mapped(pbic, addr, addr + PAGE_SIZE - 1)) {
		cop_debug("pbic_range_mapped() didn't fail\n");
		status = TEST_SETUP_FAIL;
		goto out;
	}

	/* Touch the memory to fault PTEs in */
	p = (char *)addr;
	*p = 0;

	rc = pbic_map_wrapper(pbic, args);
	if (rc < 0) {
		cop_debug("pbic_map failed %d\n", rc);
		goto out;
	}

	if (!pbic_range_mapped(pbic, addr, addr + PAGE_SIZE - 1)) {
		cop_debug("pbic_range_mapped() failed\n");
		goto out;
	}

	status = 0;
out:
	sys_munmap(addr, PAGE_SIZE);

	return status;
}

static int map_test(struct pbic *pbic)
{
	struct copro_map_args args;
	int rc;

	rc = setup_test_mapping(pbic, &args);

	sys_munmap(args.entries[0].addr, PAGE_SIZE);

	return rc;
}

static int pbic_unmap_test(struct pbic *pbic)
{
	/* Test that pbic_unmap() works */
	struct copro_map_args args;
	unsigned long addr;
	int rc, status = TEST_FAIL;

	rc = setup_test_mapping(pbic, &args);
	if (rc)
		return rc;

	addr = args.entries[0].addr;

	args.flags = COPRO_MAP_BOLT;
	rc = pbic_map_wrapper(pbic, &args);
	if (rc < 0) {
		cop_debug("pbic_map (bolt) failed %d\n", rc);
		goto out;
	}

	if (!pbic_range_bolted(pbic, addr, addr + PAGE_SIZE - 1)) {
		cop_debug("pbic_range_bolted() failed\n");
		goto out;
	}

	rc = pbic_unmap(pbic, &args);
	if (rc < 0) {
		cop_debug("pbic_unmap failed %d\n", rc);
		goto out;
	}

	if (pbic_range_bolted(pbic, addr, addr + PAGE_SIZE - 1)) {
		cop_debug("still mapped after unbolt\n");
		goto out;
	}

	status = 0;
out:
	sys_munmap(addr, PAGE_SIZE);

	return status;
}

static int pbic_bolt_multiple_test(struct pbic *pbic)
{
	/* Test that multiple boltings require multiple unboltings */
	struct copro_map_args args;
	unsigned long addr;
	int i, rc, status = TEST_FAIL;

	rc = setup_test_mapping(pbic, &args);
	if (rc)
		return rc;

	addr = args.entries[0].addr;
	args.flags = COPRO_MAP_BOLT;

	/* Bolt 16 times, one byte at a time */
	args.entries[0].len = 1;
	for (i = 0; i < 16; i++) {
		args.entries[0].addr = addr + i;

		rc = pbic_map_wrapper(pbic, &args);
		if (rc < 0) {
			cop_debug("pbic_map (bolt) failed %d loop %d\n", rc, i);
			goto out;
		}
	}

	if (!pbic_range_bolted(pbic, addr, addr + 15)) {
		cop_debug("pbic_range_bolted() failed\n");
		goto out;
	}

	/* Unbolt 15 times, the mapping should still be there */
	args.entries[0].len = 1;
	for (i = 0; i < 15; i++) {
		args.entries[0].addr = addr + i;

		rc = pbic_unmap(pbic, &args);
		if (rc < 0) {
			cop_debug("pbic_unmap failed %d loop %d\n", rc, i);
			goto out;
		}
	}

	/* Should still be bolted */
	if (!pbic_range_bolted(pbic, addr, addr + 15)) {
		cop_debug("not mapped after 15 unbolts\n");
		goto out;
	}

	args.entries[0].addr = addr + 15;
	rc = pbic_unmap(pbic, &args);
	if (rc < 0) {
		cop_debug("pbic_unmap failed %d\n", rc);
		goto out;
	}

	/* It should really be unbolted now */
	if (pbic_range_bolted(pbic, addr, addr + 15)) {
		cop_debug("still mapped after final unbolt\n");
		goto out;
	}

	status = 0;
out:
	sys_munmap(addr, PAGE_SIZE);

	return status;
}

static int flush_mm_bolted_test(struct pbic *pbic)
{
	/* Test that flush_tlb_mm() doesn't flush bolted entries */
	struct copro_map_args args;
	unsigned long addr;
	int rc, status = TEST_FAIL;

	rc = setup_test_mapping(pbic, &args);
	if (rc)
		return rc;

	addr = args.entries[0].addr;

	args.flags = COPRO_MAP_BOLT;
	rc = pbic_map_wrapper(pbic, &args);
	if (rc < 0) {
		cop_debug("pbic_map (bolt) failed %d\n", rc);
		goto out;
	}

	if (!pbic_range_bolted(pbic, addr, addr + PAGE_SIZE - 1)) {
		cop_debug("pbic_range_bolted() failed\n");
		goto out;
	}

	flush_tlb_mm(current->mm);

	if (!pbic_range_bolted(pbic, addr, addr + PAGE_SIZE - 1)) {
		cop_debug("flush_tlb_mm() flushed bolted\n");
		goto out;
	}

	status = 0;
out:
	rc = pbic_unmap(pbic, &args);
	if (rc < 0) {
		cop_debug("pbic_unmap failed %d\n", rc);
		status = TEST_FAIL;
	}

	sys_munmap(addr, PAGE_SIZE);

	return status;
}

static int pbic_flush_bolted_test(struct pbic *pbic)
{
	/* Test that copro_mmu_flush_bolted() works */
	struct copro_map_args args;
	unsigned long addr;
	int rc, status = TEST_FAIL;

	rc = setup_test_mapping(pbic, &args);
	if (rc)
		return rc;

	addr = args.entries[0].addr;

	args.flags = COPRO_MAP_BOLT;
	rc = pbic_map(pbic, &args);	/* NB. _Don't_ use the wrapper */
	if (rc < 0) {
		cop_debug("pbic_map (bolt) failed %d\n", rc);
		goto out;
	}

	if (!pbic_range_bolted(pbic, addr, addr + PAGE_SIZE - 1)) {
		cop_debug("pbic_range_bolted() failed\n");
		goto out;
	}

	/* We should be taking the tlbivax lock here, x-fingers.
	 * We use the addr from entries because it's properly aligned. */
	copro_mmu_flush_bolted(current->mm, args.entries[0].addr);

	if (pbic_range_bolted(pbic, addr, addr + PAGE_SIZE - 1)) {
		cop_debug("copro_mmu_flush_bolted() didn't flush\n");
		goto out;
	}

	status = 0;
out:
	rc = pbic_unmap(pbic, &args);
	if (rc < 0) {
		cop_debug("pbic_unmap failed %d\n", rc);
		status = TEST_FAIL;
	}

	sys_munmap(addr, PAGE_SIZE);

	return status;
}

static int kernel_bolted_mapping_test(struct pbic *pbic)
{
	/* Test that the linear mapping is bolted */
	unsigned long start, end;

	start = (unsigned long)__va(0);
	end   = (unsigned long)__va(linear_map_top - 1);

	if (!__pbic_range_mapped(pbic, start, end, 0, 1)) {
		cop_debug("pbic_range_mapped() failed\n");
		return TEST_FAIL;
	}

	return 0;
}

struct pbic_test_args {
	struct pbic *pbic;
	int rc;
};

typedef int (*test_function_t)(struct pbic *);
typedef int (*test_wrapper_function_t)(struct pbic *, test_function_t func);

struct test_descriptor {
	test_function_t func;
	test_wrapper_function_t wrapper;
	char name[64];
};

static int test_if_htw_enabled(struct pbic *pbic, test_function_t func)
{
	if (!book3e_htw_enabled) {
		cop_debug("Test disabled because HTW is disabled\n");
		return 0;
	}

	return func(pbic);
}


#define TEST(_func) { .func = _func, .name = #_func, .wrapper = NULL }
#define TEST_IF_HTW(_func) \
	{ .func = _func, .name = #_func, .wrapper = test_if_htw_enabled }

static struct test_descriptor test_descs[] = {
	TEST(map_test),
	TEST(kernel_bolted_mapping_test),
	TEST_IF_HTW(pbic_unmap_test),
	TEST_IF_HTW(flush_mm_bolted_test),
	TEST_IF_HTW(pbic_flush_bolted_test),
	TEST_IF_HTW(pbic_bolt_multiple_test),
};

static int test_mux_set(void *data, u64 val)
{
	struct pbic_test_args *args = data;
	test_wrapper_function_t wrapper;
	test_function_t func;

	cop_debug("running test %llu\n", val);

	if (val >= ARRAY_SIZE(test_descs)) {
		pr_err("%s: Bad test number %llu\n", __func__, val);
		return -EINVAL;
	}

	wrapper = test_descs[val].wrapper;
	func = test_descs[val].func;

	if (wrapper)
		args->rc = wrapper(args->pbic, func);
	else
		args->rc = func(args->pbic);

	return 0;
}

static int test_mux_get(void *data, u64 *val)
{
	struct pbic_test_args *args = data;

	*val = args->rc;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(test_mux_fops, test_mux_get, test_mux_set, "%lld\n");

static int tests_descs_show(struct seq_file *m, void *private)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_descs); i++)
		seq_printf(m, "%s\n", test_descs[i].name);

	return 0;
}

static int test_descs_open(struct inode *inode, struct file *file)
{
	return single_open(file, tests_descs_show, inode->i_private);
}

static const struct file_operations test_descs_fops = {
	.open = test_descs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void __init pbic_debug_init(struct pbic *pbic)
{
	struct pbic_test_args *args;

	debugfs_create_u64("invalidate_counter", 0400, pbic->dentry,
			   &pbic->invalidate_counter);

	args = kzalloc(sizeof(*args), GFP_KERNEL);
	if (!args) {
		pr_err("%s: no memory for args\n", __func__);
		return;
	}

	args->pbic = pbic;
	debugfs_create_file("test_mux", 0600, pbic->dentry, args,
			    &test_mux_fops);
	debugfs_create_file("test_descs", 0400, pbic->dentry, args,
			    &test_descs_fops);
}
#else
static inline void pbic_debug_init(struct pbic *pbic) {}
#endif /* DEBUG */

/* this is how big the buffer passed to pbic_tlb_entry_buf() must be */
#define PBIC_TLB_ENTRY_BUFSZ (42 + 36 + 28 + 28)

static void pbic_tlb_entry_buf(void *buf, int sz, struct pbic_tlb_entry *e)
{
	u32 tsize;
	u64 pn;
	int len;

	BUG_ON(sz < PBIC_TLB_ENTRY_BUFSZ);

	tsize = MAS1_TSIZE_GET(e->mas1);

	len = snprintf(buf, sz, "v:%d pid:%d ts:%d iprot:%d size:%d %s ",
		       (e->mas1 & MAS1_VALID) ? 1 : 0,
		       MAS1_TID_GET(e->mas1),
		       (e->mas1 & MAS1_TS) ? 1 : 0,
		       (e->mas1 & MAS1_IPROT) ? 1 : 0,
		       tsize, book3e_page_size_name(tsize));
	BUG_ON(len > sz);
	sz -= len;
	buf += len;

	if (e->mas1 & MAS1_IND) {
		u32 spsize;

		spsize = MAS3_SPSIZE_GET(e->mas3);
		len = snprintf(buf, sz, "ind:1 spsize:%d %s ra52:%d\n",
			       spsize,
			       book3e_page_size_name(spsize),
			       (e->mas3 & MAS3_RA52) ? 1 : 0);
	} else {
		len = snprintf(buf, sz,
			       "ind:0 uw:%d sw:%d ur:%d sr:%d i:%d g:%d\n",
			       (e->mas3 & MAS3_UW) ? 1 : 0,
			       (e->mas3 & MAS3_SW) ? 1 : 0,
			       (e->mas3 & MAS3_UR) ? 1 : 0,
			       (e->mas3 & MAS3_SR) ? 1 : 0,
			       (e->mas2 & MAS2_I) ? 1 : 0,
			       (e->mas2 & MAS2_G) ? 1 : 0);
	}
	BUG_ON(len > sz);
	sz -= len;
	buf += len;

	pn = (e->mas2 & MAS2_EPN);
	len = snprintf(buf, sz, "     epn:0x%013llxXXX\n", pn >> 12);

	BUG_ON(len > sz);
	sz -= len;
	buf += len;

	pn = e->mas7;
	pn <<= 32;
	pn |= e->mas3 & MAS3_RPN;

	len = snprintf(buf, sz, "     rpn:0x%013llxXXX\n", pn >> 12);
	BUG_ON(len > sz);
}

static void pbic_show_tlb_entry(struct seq_file *m,
				struct pbic_tlb_entry *e, int i)
{
	char buf[PBIC_TLB_ENTRY_BUFSZ];

	pbic_tlb_entry_buf(buf, sizeof(buf), e);
	seq_printf(m, "%03d: %s", i, buf);
}

#ifdef DEBUG
void pbic_tlb_entry_dump(struct pbic *pbic, struct pbic_tlb_entry *e)
{
	char buf[PBIC_TLB_ENTRY_BUFSZ];
	int hes;

	if (!copro_debug)
		return;

	hes = (e->mas0 & MAS0_HES) ? 1 : 0;

	pr_debug("pbic: %d hes %d ", pbic->instance, hes);
	if (hes == 0)
		pr_debug("esel %d ", MAS0_ESEL_GET(e->mas0));

	pbic_tlb_entry_buf(buf, sizeof(buf), e);
	pr_debug("%s", buf);

}
#endif /* DEBUG */

static int pbic_tlb_dump(struct seq_file *m, struct pbic *pbic, int user_only)
{
	struct pbic_tlb_entry *tlbe;
	int i;

	spin_lock(&pbic->lock);

	for (i = 0; i < pbic->tlb_size; i++) {
		pbic->crb->tlb_entry_index = i;

		tlbe = pbic_tlb_read_entry(pbic, i);

		if (!(tlbe->mas1 & MAS1_VALID))
			continue;

		if (user_only) {
			if ((tlbe->mas1 & MAS1_TID_MASK) == 0)
				continue;
		}
		pbic_show_tlb_entry(m, tlbe, i);
	}

	spin_unlock(&pbic->lock);

	return 0;
}

static int pbic_tlb_show(struct seq_file *m, void *private)
{
	struct pbic *pbic = m->private;
	return pbic_tlb_dump(m, pbic, 0);
}

static int pbic_tlb_open(struct inode *inode, struct file *file)
{
	return single_open(file, pbic_tlb_show, inode->i_private);
}

static const struct file_operations tlb_fops = {
	.open = pbic_tlb_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int pbic_user_tlb_show(struct seq_file *m, void *private)
{
	struct pbic *pbic = m->private;
	return pbic_tlb_dump(m, pbic, 1);
}

static int pbic_user_tlb_open(struct inode *inode, struct file *file)
{
	return single_open(file, pbic_user_tlb_show, inode->i_private);
}

static const struct file_operations user_tlb_fops = {
	.open = pbic_user_tlb_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int pbic_bolted_show(struct seq_file *m, void *private)
{
	struct pbic *pbic = m->private;
	struct pbic_tlb_info *info;
	int i;

	spin_lock(&pbic->lock);

	for (i = pbic->watermark + 1; i < pbic->tlb_size; i++) {
		info = &pbic->tlb_info[i];

		if (info->count)
			seq_printf(m, "%03d: pid:%03d epn:0x%016llx count:%d\n",
				   i, info->pid, info->address, info->count);
	}

	spin_unlock(&pbic->lock);

	return 0;
}

static int pbic_bolted_open(struct inode *inode, struct file *file)
{
	return single_open(file, pbic_bolted_show, inode->i_private);
}

static const struct file_operations bolted_fops = {
	.open = pbic_bolted_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct reg_range pbic_regs[] = {
	{ 0,	24 },
	{ 0xD0,	38 },
	{ 0,	 0 },
	/* FIXME Add perf regs at 0x3000 when FW is updated */
};

static int pbic_regs_show(struct seq_file *m, void *private)
{
	struct pbic *pbic = m->private;
	struct resource mmio;
	struct reg_range *r;
	int i, offset;
	u64 val;

	if (of_address_to_resource(pbic->dn, 0, &mmio))
		return -EIO;

	seq_printf(m, "PBIC %d (index %d) CT 0x%x CI 0x%x MMIO %pR (%p)\n",
		   pbic->instance, pbic->index, pbic->type, pbic->instance,
		   &mmio, pbic->mmio_addr);

	r = pbic_regs;
	while (r->count) {
		for (i = 0; i < r->count; i++) {
			offset = r->start + (i * sizeof(u64));
			val = in_be64(pbic->mmio_addr + offset);
			seq_printf(m, "0x%03x: 0x%016llx\n", offset, val);
		}
		r++;
	}

	return 0;
}

static int pbic_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, pbic_regs_show, inode->i_private);
}

static const struct file_operations regs_fops = {
	.open = pbic_regs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};


void __init pbic_debugfs_init_pbic(struct pbic *pbic)
{
	struct dentry *d;

	d = debugfs_create_dir(pbic->name, powerpc_debugfs_root);

	if (!d)
		return;

	pbic->dentry = d;
	debugfs_create_file("regs", 0600, d, pbic, &regs_fops);
	debugfs_create_file("tlb", 0600, d, pbic, &tlb_fops);
	debugfs_create_file("user_tlb", 0600, d, pbic, &user_tlb_fops);
	debugfs_create_file("bolted", 0600, d, pbic, &bolted_fops);
	debugfs_create_u32("watermark", 0600, d, &pbic->watermark);
	debugfs_create_u32("next_slot", 0600, d, &pbic->next_slot);

	pbic_debug_init(pbic);
}

#define PBIC_XLATE_ERR		0x90
#define PBIC_XLATE_EA		0x98

#define XLATE_ERR_VALID		(1ul << 63)
#define XLATE_ERR_MISS		(1ul << 62)
#define XLATE_ERR_MODE		(1ul << 61)
#define XLATE_ERR_UNIT		(1ul << 60)
#define XLATE_ERR_PID_MASK	0x3fff00
#define XLATE_ERR_PID_SHIFT	8

static void pbic_check_xlate_error(struct pbic *pbic)
{
	unsigned miss, mode, unit, pid;
	u64 status, eaddr;

	/* We shouldn't /need/ to lock the pbic here, but .. ? */

	status = in_be64(pbic->mmio_addr + PBIC_XLATE_ERR);
	if (!(status & XLATE_ERR_VALID))
		return;

	eaddr = in_be64(pbic->mmio_addr + PBIC_XLATE_EA);
	/* Clear it so another can be detected */
	out_be64(pbic->mmio_addr + PBIC_XLATE_ERR, XLATE_ERR_VALID);

	miss = !!(status & XLATE_ERR_MISS);
	mode = !!(status & XLATE_ERR_MODE);
	unit = !!(status & XLATE_ERR_UNIT);
	pid  = (status & XLATE_ERR_PID_MASK) >> XLATE_ERR_PID_SHIFT;

	pr_debug("%s: translation failure %s%sunit %s pid 0x%x "
		"ea 0x%llx\n", pbic->name, miss ? "miss " : "",
		mode ? "mode " : "", unit ? "B" : "A", pid, eaddr);
}

static struct delayed_work xlate_scan_work;
static u64 xlate_scan_delay;

static void pbic_xlate_scan(struct work_struct *w)
{
	struct pbic *pbic;

	for_each_pbic(pbic)
		pbic_check_xlate_error(pbic);

	if (xlate_scan_delay)
		schedule_delayed_work(&xlate_scan_work, xlate_scan_delay);
}

static int xlate_scan_delay_set(void *data, u64 val)
{
	xlate_scan_delay = msecs_to_jiffies(val);

	if (xlate_scan_delay && !work_pending(&xlate_scan_work.work))
		schedule_delayed_work(&xlate_scan_work, xlate_scan_delay);

	return 0;
}

static int xlate_scan_delay_get(void *data, u64 *val)
{
	*val = jiffies_to_msecs(xlate_scan_delay);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(xlate_scan_delay_fops, xlate_scan_delay_get,
			xlate_scan_delay_set, "%lld\n");

void __init pbic_debugfs_init(void)
{
	xlate_scan_delay = msecs_to_jiffies(1000);

	debugfs_create_file("pbic_xlate_scan_delay", 0600,
			    powerpc_debugfs_root, &xlate_scan_delay,
			    &xlate_scan_delay_fops);

	INIT_DELAYED_WORK(&xlate_scan_work, pbic_xlate_scan);
	schedule_delayed_work(&xlate_scan_work, xlate_scan_delay);
}
