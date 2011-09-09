/*
 * Copyright 2008-2011, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/of.h>
#include <linux/memblock.h>

#include "wsp.h"

/* Allocate memory for each XML coprocessor,
 *
 *	256M for transient bufflets (aligned to the size), plus
 *
 *	4 x 32M (20 + 12 padding) for VF0 Qcode table
 *		(32MB * aligned), plus
 *
 *	4 x 16M for VF0 fixed session state (16MB aligned)
 *		=> (16bit session id), plus
 *
 *	4 x 1M  for VF0 Imq (aligned to the size)
 *
 *	= 452 M on 256M boundary
 *
 *	The order has to be respected for alignment issues
 */
ulong __initdata xmlx_early_size = 452UL << 20; /* 452M */
static ulong __initdata xmlx_early_align = 256UL << 20; /* 256M */

static int __init xmlx_early_param(char *str)
{
	if (!str)
		return 1;
	if (strncmp(str, "off", 3) == 0)
		xmlx_early_size = 0;

	return 0;
}
early_param("xmlx", xmlx_early_param);


/* hack, we know there is only 4, but it is ok to over (way) allocate
 * since it will get released after init, we also make sure to leave
 * at least one 0 entry to know when we are done later */
u64 __initdata wsp_xmlx_early_allocs[NR_CPUS + 1];

void __init wsp_setup_xmlx(void)
{
	struct device_node *dn;
	u64 pa;
	int chips = 0;

	if (xmlx_early_size == 0) {
		pr_info("xmlx turned off\n");
		return;
	}

	for_each_compatible_node(dn, NULL, XML_UNIT_COMPATIBLE)	{
		if (chips > ARRAY_SIZE(wsp_xmlx_early_allocs) - 1)
			break;
		pa = memblock_alloc_base(xmlx_early_size, xmlx_early_align, 0);
		if (pa == 0) {
			pr_err("xmlx: Failed allocating memory for %s\n",
				dn->full_name);
			break;
		}
		wsp_xmlx_early_allocs[chips] = pa;
		chips++;
	}
}
