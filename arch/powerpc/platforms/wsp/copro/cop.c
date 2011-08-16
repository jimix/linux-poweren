/*
 * Copyright 2008 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/of.h>
#include <asm/mmu.h>
#include <asm/machdep.h>

#include "cop.h"
#include "pbic.h"
#include "unit.h"

struct kmem_cache *cop_cxb_cache;
EXPORT_SYMBOL_GPL(cop_cxb_cache);

static int __init create_cop_cxb_cache(void)
{
	cop_cxb_cache = kmem_cache_create("cop_cxb", COP_CXB_SIZE,
					   COP_CXB_ALIGN, 0, NULL);

	if (!cop_cxb_cache) {
		pr_err("%s: can't allocate cop_cxb cache!\n", __func__);
		return -ENOMEM;
	}

	return 0;
}

#ifdef DEBUG
u32 copro_debug = 1;	/* Debugging messages disabled by default */

static int __init setup_copro_debug(char *str)
{
	if (strcmp(str, "on") == 0 || strcmp(str, "1") == 0)
		copro_debug = 1;

	return 1;
}
__setup("copro_debug=", setup_copro_debug);

static u32 htw_enabled;

/* Allow raw reg access to copro units */
u32 copro_raw_reg_enabled;

static void cop_debug_init(void)
{
	htw_enabled = book3e_htw_enabled;

	debugfs_create_u32("copro_debug", 0600, powerpc_debugfs_root,
			   &copro_debug);
	debugfs_create_u32("htw_enabled", 0400, powerpc_debugfs_root,
			   &htw_enabled);
	debugfs_create_u32("copro_raw_reg_enabled", 0600, powerpc_debugfs_root,
			   &copro_raw_reg_enabled);
}
#else
static inline void cop_debug_init(void) { };
#endif

static int __init cop_probe_devices(void)
{
	static __initdata struct of_device_id bus_ids[] = {
		/* every node in between need to be here or you won't find it */
		{ .compatible = WSP_SOC_COMPATIBLE, },
		{ .compatible = PBIC_COMPATIBLE, },
		{ .compatible = COPRO_COMPATIBLE, },
		{},
	};
	of_platform_bus_probe(NULL, bus_ids, NULL);

	return 0;
}

static int __init cop_init(void)
{
	int rc;

	cop_probe_devices();

	cop_debug_init();

	rc = create_cop_cxb_cache();
	if (rc)
		return rc;

	rc = pbic_driver_init();
	if (rc)
		goto out;

	rc = copro_unit_init();
	if (rc)
		goto out;

	rc = copro_instance_init();
	if (rc)
		goto out;

	rc = copro_types_init();
	if (rc)
		goto out;

	/* NB. This needs to be last because it's the mechanism by which
	 * userspace interacts with all the code we just initialised. If
	 * any of them failed we can't safely register the driver. */
	rc = copro_driver_init();
	if (rc)
		goto out;

	return 0;

out:
	kmem_cache_destroy(cop_cxb_cache);

	return rc;
}
arch_initcall(cop_init);
