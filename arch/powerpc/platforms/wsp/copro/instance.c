/*
 * Copyright 2009-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/debugfs.h>

#include "cop.h"
#include "unit.h"

static inline void copro_instance_debug_init(struct platform_device *pdev)
{
#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
	struct copro_instance *copro = dev_get_drvdata(&pdev->dev);

	copro->dentry = debugfs_create_dir(dev_name(&pdev->dev),
					   copro->unit->dentry);

	debugfs_create_u32("type", 0400, copro->dentry, &copro->type);
	debugfs_create_u32("instance", 0400, copro->dentry, &copro->instance);
#endif
}

static int copro_instance_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct copro_instance *copro;
	struct platform_device *unit_pdev;
	u32 ct, ci;

	if (of_device_is_compatible(dn, "ibm,mmux"))
		return -ENODEV;	/* Skip PBICs */

	if (read_coprocessor_reg(dn, &ct, &ci)) {
		dev_err(&pdev->dev, "invalid/missing ibm,coprocessor-reg\n");
		return -EINVAL;
	}

	unit_pdev = copro_find_parent_unit(dn);
	if (!unit_pdev || !dev_get_drvdata(&unit_pdev->dev)) {
		dev_err(&pdev->dev, "no copro unit found?\n");
		return -ENOENT;
	}

	copro = kzalloc(sizeof(*copro), GFP_KERNEL);
	if (!copro)
		return -ENOMEM;

	copro->dn = of_node_get(dn);
	copro->type = ct;
	copro->instance = ci;
	get_device(&unit_pdev->dev);
	copro->unit = dev_get_drvdata(&unit_pdev->dev);
	copro->unit->types |= copro->type;
	snprintf(copro->name, sizeof(copro->name), "%#x/%#x", ct, ci);

	dev_set_drvdata(&pdev->dev, copro);

	copro_instance_debug_init(pdev);

	dev_printk(KERN_DEBUG, &pdev->dev, "instance with CT/CI %s under %s\n",
		   copro->name, copro->unit->name);

	return 0;
}

static const struct of_device_id copro_instance_device_id[] = {
	{ .compatible	= COPRO_TYPE_COMPATIBLE },
	{}
};

static struct platform_driver copro_instance_driver = {
	.probe		= copro_instance_probe,
	.driver		= {
		.name	= "wsp-copro-instance",
		.owner	= THIS_MODULE,
		.of_match_table	= copro_instance_device_id,
	},
};

int __init copro_instance_init(void)
{
	return platform_driver_register(&copro_instance_driver);
}
