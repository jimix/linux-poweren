/*
 * Copyright 2009-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <asm/copro-driver.h>

#include "cop.h"
#include "imq.h"
#include "unit.h"
#include "unit_init.h"


static u16 __initdata unit_next_index;

struct platform_device *copro_find_parent_unit(struct device_node *dn)
{
	struct platform_device *pdev;

	/* Allow a node to be both a unit and an instance */
	dn = of_node_get(dn);
	while (dn && !of_device_is_compatible(dn, COPRO_COMPATIBLE))
		dn = of_get_next_parent(dn);

	if (!dn)
		return NULL;

	pdev = of_find_device_by_node(dn);
	of_node_put(dn);

	return pdev;
}

static long copro_unit_reg_offset(struct copro_unit *unit, unsigned int cmd,
				  struct copro_reg_args *args)
{
	long rc = -ENXIO;

	switch (args->regnr) {
	case COPRO_UNIT_REG_MTRACE:
		if (unit->regs)
			return unit->regs->marker_trace;
		break;
	}

#ifdef DEBUG
	if (args->regnr & COPRO_UNIT_REG_RAW) {
		if (!copro_raw_reg_enabled) {
			cop_debug("raw reg access disabled\n");
			rc = -EACCES;
		} else
			rc = args->regnr & ~COPRO_UNIT_REG_RAW;
	}
#endif
	return rc;
}

long copro_unit_reg_ioctl(struct copro_unit *unit, unsigned int cmd,
			  void __user *uptr, struct copro_reg_args *args)
{
	void __iomem *addr;
	long offset;

	offset = copro_unit_reg_offset(unit, cmd, args);
	if (offset < 0) {
		cop_debug("invalid reg nr %#llx\n", args->regnr);
		return offset;
	}

	addr = unit->mmio_addr + offset;

	switch (cmd) {
	case COPRO_UNIT_IOCTL_GET_REG:
		cop_debug("reading from %p (%#lx)\n", addr, offset);
		args->value = in_be64(addr);
		if (copy_to_user(uptr, args, sizeof(*args)))
			return -EFAULT;
		break;
	case COPRO_UNIT_IOCTL_SET_REG:
		cop_debug("writing %#llx to %p (%#lx)\n", args->value,
			   addr, offset);
		out_be64(addr, args->value);
		break;
	}

	return 0;
}

long copro_unit_ioctl(struct copro_unit *unit, unsigned int cmd,
		      unsigned long arg)
{
	void __user *uptr = (void __user *)arg;
	struct copro_reg_args args;
	u64 val;

	switch (cmd) {
	case COPRO_UNIT_IOCTL_GET_ID:
		val = unit->index;
		if (put_user(val, (u64 __user *)arg))
			return -EFAULT;
		return 0;
	case COPRO_UNIT_IOCTL_SET_REG:
	case COPRO_UNIT_IOCTL_GET_REG:
		if (copy_from_user(&args, uptr, sizeof(args))) {
			cop_debug("error copying args from %p\n", uptr);
			return -EFAULT;
		}
		return copro_unit_reg_ioctl(unit, cmd, uptr, &args);
	case COPRO_UNIT_IOCTL_ABORT_CRB:
		if (get_user(val, (u64 __user *)arg))
			return -EFAULT;
		return copro_unit_abort(unit, val, current->mm->context.id);
	}

	cop_debug("unknown ioctl 0x%x\n", cmd);

	return -EINVAL;
}

static long unit_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct copro_unit *unit = f->private_data;
	return copro_unit_ioctl(unit, cmd, arg);
}

static const struct file_operations unit_fops = {
	.unlocked_ioctl = unit_ioctl,
	.compat_ioctl = unit_ioctl,
};

#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
static int unit_regs_show(struct seq_file *m, void *private)
{
	struct copro_unit *unit = m->private;
	struct reg_range *r;
	const u64 *p;
	u64 val;
	int offset, i;

	p = of_get_property(unit->dn, "reg", NULL);
	if (!p)
		return -EIO;

	seq_printf(m, "Unit %d (index %d) MMIO 0x%llx (%p)\n",
		   unit->number, unit->index, *p, unit->mmio_addr);

	if (!unit->debug_regs) {
		seq_printf(m, " No debug regs defined\n");
		return 0;
	}

	r = unit->debug_regs;
	while (r->count) {
		for (i = 0; i < r->count; i++) {
			offset = r->start + (i * sizeof(u64));
			val = in_be64(unit->mmio_addr + offset);
			seq_printf(m, "0x%03x: 0x%016llx\n", offset, val);
		}
		r++;
	}

	return 0;
}

static int unit_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, unit_regs_show, inode->i_private);
}

static const struct file_operations regs_fops = {
	.open = unit_regs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void __init copro_unit_debug_init(struct copro_unit *unit)
{
	char name[16];

	unit->dentry = debugfs_create_dir(unit->name, unit->pbic->dentry);

	debugfs_create_file("regs", 0400, unit->dentry, unit, &regs_fops);
	debugfs_create_u32("number", 0400, unit->dentry, &unit->number);
	debugfs_create_u16("index", 0400, unit->dentry, &unit->index);

	snprintf(name, sizeof(name), "unit_num%d", unit->number);
	debugfs_create_symlink(name, unit->pbic->dentry, unit->name);
}
#else
static inline void copro_unit_debug_init(struct copro_unit *unit) { }
#endif

int copro_unit_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct copro_unit *unit;
	void __iomem *mmio_addr;
	struct pbic *pbic;
	const u32 *p;
	int rc, len;
	u16 index;

	p = of_get_property(dn, "ibm,pbic-unit-number", &len);
	if (!p || len < sizeof(u32)) {
		dev_err(&pdev->dev, "invalid ibm,pbic-unit-number\n");
		return -EINVAL;
	}

	pbic = pbic_get_parent_pbic(dn);
	if (!pbic) {
		dev_err(&pdev->dev, "no PBIC found?\n");
		return -ENOENT;
	}

	index = ++unit_next_index;
	if (index == 0) {
		dev_err(&pdev->dev, "too many units!\n");
		rc = -ENOSPC;
		goto out_drop_pbic;
	}

	mmio_addr = of_iomap(dn, 0);
	if (mmio_addr == NULL) {
		dev_err(&pdev->dev, "couldn't iomap reg\n");
		rc = -EFAULT;
		goto out_free_index;
	}

	unit = kzalloc(sizeof(*unit), GFP_KERNEL);
	if (!unit) {
		rc = -ENOMEM;
		goto out_unmap;
	}

	unit->dn = of_node_get(dn);
	unit->fops = &unit_fops;
	unit->mmio_addr = mmio_addr;
	unit->number = p[0];
	unit->index = index;
	unit->pbic = pbic;
	snprintf(unit->name, sizeof(unit->name), "unit%d", unit->index);

	dev_set_drvdata(&pdev->dev, unit);

	copro_unit_debug_init(unit);
	unit->nr_imqs = copro_unit_probe_imqs(pdev);

	dev_printk(KERN_DEBUG, &pdev->dev, "%s, under pbic 0x%x, unit %c\n",
		   unit->name, pbic->instance, unit->number ? 'A' : 'B');

	return 0;

out_unmap:
	iounmap(mmio_addr);
out_free_index:
	unit_next_index -= 1;
out_drop_pbic:
	pbic_put_parent_pbic(pbic);

	return rc;
}

static const struct of_device_id copro_unit_device_id[] = {
	{ .compatible	= COPRO_COMPATIBLE },
	{}
};

static struct platform_driver copro_unit_driver = {
	.probe		= copro_unit_probe,
	.driver		= {
		.name	= "wsp-copro-unit",
		.owner	= THIS_MODULE,
		.of_match_table	= copro_unit_device_id,
	},
};


/*
 * This is the first one, which we will skip so it, but is gives us a
 * symbolic hook into the list
 */
__copro_unit(start, NULL);

int __init copro_unit_init(void)
{
	int rc;
	struct copro_unit_list_entry *cu;

	cu = &__copro_unit_start;
	++cu;			/* skip the first */
	while (cu->cu_fn) {
		pr_info("%s: calling %s()\n", __func__, cu->cu_name);
		rc = cu->cu_fn();
		if (rc) {
			pr_warn("%s: %s() returned %d\n",
				__func__, cu->cu_name, rc);
		}
		++cu;
	}

	return platform_driver_register(&copro_unit_driver);
}
