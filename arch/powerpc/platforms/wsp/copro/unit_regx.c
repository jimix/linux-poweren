/*
 * Copyright 2009-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/capability.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/uaccess.h>

#include <asm/copro-regx.h>
#include <asm/mmu_context.h>

#include "cop.h"
#include "unit.h"

#define RXUMAPR_OFFSET		0x18
#define RXUMAPR_PID_MASK	0x3FFF
#define RXUMAPR_PID_SHIFT	16
#define RXUMAPR_ACCESS_SHIFT	63


/* Private data for the Regx driver */
struct regx_priv_data {
	struct list_head list;
	struct mutex lock;
	struct mm_struct *mm;
	struct copro_unit *unit;
};

struct regx_reg_desc {
	u64 valid_mask;
	u64 offset;
};

static struct regx_reg_desc regx_regs[] = {
	[REGX_REG_RXBPCR]   = { RXBPCR_MASK,   0x400 },
	[REGX_REG_RXBP0SR]  = { RXBPnSR_MASK,  0x410 },
	[REGX_REG_RXBP1SR]  = { RXBPnSR_MASK,  0x418 },
	[REGX_REG_RXBP0HR]  = { RXBPnHR_MASK,  0x420 },
	[REGX_REG_RXBP1HR]  = { RXBPnHR_MASK,  0x428 },
	[REGX_REG_RXBP0MR0] = { RXBPnMRn_MASK, 0x440 },
	[REGX_REG_RXBP0MR1] = { RXBPnMRn_MASK, 0x448 },
	[REGX_REG_RXBP1MR0] = { RXBPnMRn_MASK, 0x450 },
	[REGX_REG_RXBP1MR1] = { RXBPnMRn_MASK, 0x458 },
	/* Require RATB/AS/GS = 0, allow PR, but we set it regardless */
	[REGX_REG_RXRBAR]   = { 0xFFFFFFFFFF1F0001UL, 0x040 },
};

static int current_is_um(struct regx_priv_data *regx_priv)
{
	return regx_priv->mm == current->mm;
}

static inline void regx_set_reg(struct copro_unit *unit, u64 offset, u64 value)
{
	cop_debug("writing 0x%llx to 0x%llx\n", value, offset);
	out_be64(unit->mmio_addr + offset, value);
}

static long regx_reg_ioctl(struct copro_unit *unit, unsigned int cmd,
			   struct copro_reg_args __user *uargs)
{
	struct regx_priv_data *regx_priv = unit->priv;
	struct regx_reg_desc *reg;
	struct copro_reg_args args;
	int rc;

	if (copy_from_user(&args, uargs, sizeof(args))) {
		cop_debug("error copying args from %p\n", uargs);
		return -EFAULT;
	}

	if (args.regnr >= ARRAY_SIZE(regx_regs))
		return copro_unit_reg_ioctl(unit, cmd, uargs, &args);

	mutex_lock(&regx_priv->lock);

	if (!current_is_um(regx_priv)) {
		cop_debug("current is not the UM\n");
		rc = -EPERM;
		goto out;
	}

	reg = &regx_regs[args.regnr];

	rc = 0;

	switch (cmd) {
	case COPRO_UNIT_IOCTL_GET_REG:
		cop_debug("reading from 0x%llx\n", reg->offset);
		args.value = in_be64(unit->mmio_addr + reg->offset);
		if (copy_to_user(uargs, &args, sizeof(args)))
			rc = -EFAULT;
		break;
	case COPRO_UNIT_IOCTL_SET_REG:
		if (args.value & (~reg->valid_mask)) {
			cop_debug("invalid reg value 0x%llx\n", args.value);
			rc = -EINVAL;
			goto out;
		}

		if (args.regnr == REGX_REG_RXRBAR)
			/* FIXME on LPAR we need to set GS here */
			args.value |= 1;	/* Force PR=1 */

		regx_set_reg(unit, reg->offset, args.value);
		break;
	default:
		rc = -EINVAL;
		break;
	}

out:
	mutex_unlock(&regx_priv->lock);

	return rc;
}

static long request_um_priv(struct copro_unit *unit)
{
	struct regx_priv_data *regx_priv = unit->priv;
	u64 pid, value;
	int rc;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	/* Protect ourselves against PID stealing */
	rc = mm_context_protect(current->mm);
	if (rc)
		return rc;

	mutex_lock(&regx_priv->lock);

	if (regx_priv->mm) {
		if (current_is_um(regx_priv)) {
			cop_debug("current is already the UM\n");
			rc = 0;
		} else {
			cop_debug("pid 0x%x is already the UM\n",
				   regx_priv->mm->context.id);
			rc = -EBUSY;
		}

		mutex_unlock(&regx_priv->lock);

		/* Don't leave us protected, or doubly protected */
		mm_context_unprotect(current->mm);
		return rc;
	}

	regx_priv->mm = current->mm;

	pid = current->mm->context.id;
	BUG_ON(pid & (~RXUMAPR_PID_MASK));

	value = (1UL << RXUMAPR_ACCESS_SHIFT) | RXUMAPR_PID_MASK |
		(pid << RXUMAPR_PID_SHIFT);

	regx_set_reg(unit, RXUMAPR_OFFSET, value);

	mutex_unlock(&regx_priv->lock);

	return 0;
}

static void detach_um(struct regx_priv_data *regx_priv)
{
	regx_set_reg(regx_priv->unit, RXUMAPR_OFFSET, 0);
	regx_set_reg(regx_priv->unit, regx_regs[REGX_REG_RXRBAR].offset, 0);
	regx_priv->mm = NULL;
}

static long drop_um_priv(struct copro_unit *unit)
{
	struct regx_priv_data *regx_priv = unit->priv;

	mutex_lock(&regx_priv->lock);

	if (!current_is_um(regx_priv)) {
		cop_debug("current is not the UM\n");
		mutex_unlock(&regx_priv->lock);
		return -EINVAL;
	}

	detach_um(regx_priv);

	mutex_unlock(&regx_priv->lock);

	mm_context_unprotect(current->mm);

	return 0;
}

static long regx_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct copro_unit *unit = f->private_data;
	void __user *uptr = (void __user *)arg;

	cop_debug("regx ioctl 0x%x\n", cmd);

	switch (cmd) {
	case COPRO_UNIT_IOCTL_SET_REG:
	case COPRO_UNIT_IOCTL_GET_REG:
		return regx_reg_ioctl(unit, cmd, uptr);
	case REGX_IOCTL_REQUEST_UM_PRIV:
		return request_um_priv(unit);
	case REGX_IOCTL_DROP_UM_PRIV:
		return drop_um_priv(unit);
	}

	return copro_unit_ioctl(unit, cmd, arg);
}

static const struct file_operations regx_fops = {
	.unlocked_ioctl = regx_ioctl,
	.compat_ioctl = regx_ioctl,
};

#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
static struct reg_range regx_debug_regs[] = {
	{   0x0,	10 },	/* RXCFGR .. RXEIR */
	{  0x80,	32 },	/* RXCU0ACR .. RXGPCCR[15] */
	{ 0x200,	48 },	/* RXISNR0 .. RXMESR5 */
	{ 0x400,	 1 },	/* RXBPCR */
	{ 0x410,	10 },	/* RXBP0SR .. RXBP1MR[1] */
	{ 0x540,	 4 },	/* RXAEDVCR .. RXDEDVR */
	{   0x0,	 0 },	/* End */
};
#else
#define regx_debug_regs	NULL
#endif

static struct copro_unit_regs regx_unit_regs = {
	.marker_trace = 0x38,
	.abort = { 0x80, 8 },
};

static struct list_head regx_list;

static int regx_unit_probe(struct platform_device *pdev)
{
	struct regx_priv_data *priv;
	struct copro_unit *unit;
	int rc;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	rc = copro_unit_probe(pdev);
	if (rc) {
		kfree(priv);
		return rc;
	}

	unit = dev_get_drvdata(&pdev->dev);
	unit->fops = &regx_fops;
	unit->priv = priv;
	priv->unit = unit;
	mutex_init(&priv->lock);
	unit->regs = &regx_unit_regs;

	copro_unit_set_debug_regs(unit, regx_debug_regs);

	pbic_alloc_spill_queue(unit, 0);
	pbic_alloc_spill_queue(unit, 1);

	list_add_tail(&priv->list, &regx_list);

	dev_printk(KERN_DEBUG, &pdev->dev, "bound to regx\n");

	return 0;
}

static const struct of_device_id regx_unit_device_id[] = {
	{ .compatible	= "ibm,wsp-coprocessor-regx" },
	{}
};

static struct platform_driver regx_unit_driver = {
	.probe		= regx_unit_probe,
	.driver		= {
		.name	= "wsp-copro-regx-unit",
		.owner	= THIS_MODULE,
		.of_match_table = regx_unit_device_id,
	},
};

static int regx_cleanup_mm(struct notifier_block *nb,
			   unsigned long val, void *p)
{
	struct regx_priv_data *regx_priv;
	struct mm_struct *mm = p;
	int match;

	/* O(n) search, but we expect n <= 4 */
	list_for_each_entry(regx_priv, &regx_list, list) {
		mutex_lock(&regx_priv->lock);

		match = 0;
		if (regx_priv->mm == mm) {
			detach_um(regx_priv);
			match = 1;
		}

		mutex_unlock(&regx_priv->lock);

		if (match) {
			mm_context_unprotect(mm);
			cop_debug("forced 0x%x to drop UM\n", mm->context.id);
		}
	}

	return NOTIFY_DONE;
}

static struct notifier_block regx_cleanup_notifier = {
	.notifier_call = regx_cleanup_mm,
};

int __init regx_unit_init(void)
{
	int rc;

	INIT_LIST_HEAD(&regx_list);

	rc = srcu_notifier_chain_register(&mm_protect_cleanup_notifier,
					  &regx_cleanup_notifier);
	if (rc) {
		pr_err("%s: Failed registering cleanup notifier\n", __func__);
		return rc;
	}

	rc = platform_driver_register(&regx_unit_driver);

	return rc;
}
__copro_unit(regx, regx_unit_init);
