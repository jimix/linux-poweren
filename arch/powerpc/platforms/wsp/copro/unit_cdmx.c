/*
 * Copyright 2010-2011 Michael Ellerman, IBM Corporation
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
#include <linux/firmware.h>
#include <linux/mmu_context.h>

#include <asm/copro-driver.h>

#include "cop.h"
#include "unit.h"

static void cdmx_mask_wsp_firs(struct copro_unit *unit)
{
	/* Workaround for erratum 122 */
#ifdef CONFIG_PPC_A2_PSR2
	u64 val;

	if (firmware_has_feature(FW_FEATURE_MAMBO))
		return;

	val  = in_be64(unit->mmio_addr + 0x230);

	/* Disable error reporting for engines 6 & 7 */
	out_be64(unit->mmio_addr + 0x230, val | 0x6);
#endif
}

#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
static struct reg_range cdmx_debug_regs[] = {
	{   0x0,	61 },	/* Status .. VF5 Irq Status */
	{   0x200,	12 },	/* FIR Data .. Error Injection Control */
	{   0x280,	 3 },	/* PMC Control 0 .. Marker Trace Control */
	{   0x300,	 8 },	/* VFC .. VF5 MMIO Status */
	{   0x0,	 0 },	/* End */
};
#else
#define cdmx_debug_regs	NULL
#endif

static struct copro_unit_regs cdmx_regs = {
	.marker_trace	= 0x290,
	.abort = { 0x18, 8 },
};

static int cdmx_unit_probe(struct platform_device *pdev)
{
	struct copro_unit *unit;
	int i, rc;

	rc = copro_unit_probe(pdev);
	if (rc)
		return rc;

	unit = dev_get_drvdata(&pdev->dev);
	unit->regs = &cdmx_regs;
	copro_unit_set_debug_regs(unit, cdmx_debug_regs);

	cdmx_mask_wsp_firs(unit);

	for (i = 0; i < 4; i++)
		pbic_alloc_spill_queue(unit, i);

	dev_printk(KERN_DEBUG, &pdev->dev, "bound to cdmx\n");

	return 0;
}

static const struct of_device_id cdmx_unit_device_id[] = {
	{ .compatible	= "ibm,wsp-coprocessor-cdmx" },
	{}
};

static struct platform_driver cdmx_unit_driver = {
	.probe		= cdmx_unit_probe,
	.driver		= {
		.name	= "wsp-copro-cdmx-unit",
		.owner	= THIS_MODULE,
		.of_match_table	= cdmx_unit_device_id,
	},
};

int __init cdmx_unit_init(void)
{
	return platform_driver_register(&cdmx_unit_driver);
}
__copro_unit(cdmx, cdmx_unit_init);
