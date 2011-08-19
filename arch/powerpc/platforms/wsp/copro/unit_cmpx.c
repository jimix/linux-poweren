/*
 * Copyright 2010-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/copro-driver.h>

#include "cop.h"
#include "unit.h"

#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
static struct reg_range cmpx_debug_regs[] = {
	{   0x0,	64 },	/* Config .. VF5 MMIO Error */
	{   0x0,	 0 },	/* End */
};
#else
#define cmpx_debug_regs	NULL
#endif

static struct copro_unit_regs cmpx_regs = {
	.marker_trace = 0x58,
	.abort = { 0x60, 2 },
};

static int cmpx_unit_probe(struct platform_device *pdev)
{
	struct copro_unit *unit;
	int rc;

	rc = copro_unit_probe(pdev);
	if (rc)
		return rc;

	unit = dev_get_drvdata(&pdev->dev);
	unit->regs = &cmpx_regs;
	copro_unit_set_debug_regs(unit, cmpx_debug_regs);

	pbic_alloc_spill_queue(unit, 0);
	pbic_alloc_spill_queue(unit, 1);

	dev_printk(KERN_DEBUG, &pdev->dev, "bound to cmpx\n");

	return 0;
}

static const struct of_device_id cmpx_unit_device_id[] = {
	{ .compatible	= "ibm,wsp-coprocessor-cmpx" },
	{}
};

static struct platform_driver cmpx_unit_driver = {
	.probe		= cmpx_unit_probe,
	.driver		= {
		.name	= "wsp-copro-cmpx-unit",
		.owner	= THIS_MODULE,
		.of_match_table = cmpx_unit_device_id,
	},
};

int __init cmpx_unit_init(void)
{
	return platform_driver_register(&cmpx_unit_driver);
}
__copro_unit(cmpx, cmpx_unit_init);
