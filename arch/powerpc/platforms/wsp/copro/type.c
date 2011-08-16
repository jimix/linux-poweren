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

#include "cop.h"


struct list_head copro_type_list;


/* Common code */

static struct copro_type * __init
copro_type_alloc(struct copro_instance *copro, const char *name)
{
	struct copro_type *copro_type;

	copro_type = kzalloc(sizeof(*copro_type), GFP_KERNEL);
	if (!copro_type) {
		pr_err("%s: no memory to allocate struct\n", __func__);
		return NULL;
	}

	copro_type->name = kstrdup(name, GFP_KERNEL);
	if (!copro_type->name) {
		pr_err("%s: no memory to allocate string\n", __func__);
		kfree(copro_type);
		return NULL;
	}

	copro_type->type = copro->type;
	INIT_LIST_HEAD(&copro_type->instance_list);

	list_add_tail(&copro_type->list, &copro_type_list);

	return copro_type;
}

static struct copro_type * __init
copro_type_for_instance(struct copro_instance *copro, const char *name)
{
	struct copro_type *copro_type;

	list_for_each_entry(copro_type, &copro_type_list, list)
		if (copro_type->type == copro->type)
			return copro_type;

	return copro_type_alloc(copro, name);
}

static const char * __init of_compatible_next(const char *compat, int *len)
{
	int l;

	if (*len <= 0)
		return NULL;

	l = strlen(compat) + 1;

	/* Check things are sane */
	if (l >= *len)
		return NULL;

	*len -= l;

	return compat + l;
}

static int __init
check_compatible_value(struct copro_type *copro_type,
		       struct platform_device *pdev,
		       const char *compat)
{
	struct copro_instance *copro;

	list_for_each_entry(copro, &copro_type->instance_list, type_list) {
		if (!of_device_is_compatible(copro->dn, compat)) {
			/* Having inconsistent compatible values really
			 * makes no sense, so shout loudly. */
			WARN_ON(1);
			dev_err(&pdev->dev, "Compatible value '%s' doesn't " \
				"match %s\n", compat, copro->dn->full_name);
			return 0;
		}
	}

	return 1;
}

static int __init compatible_with_type(struct copro_type *copro_type,
				       struct platform_device *pdev)
{
	const char *compat;
	int len;

	compat = of_get_property(pdev->dev.of_node, "compatible", &len);
	if (!compat)
		return 0;

	while (compat) {
		if (!check_compatible_value(copro_type, pdev, compat))
			return 0;

		compat = of_compatible_next(compat, &len);
	}

	return 1;
}

static void check_instance_uniqueness(struct copro_type *type)
{
	struct copro_instance *copro, *other;

	list_for_each_entry(copro, &type->instance_list, type_list) {
		list_for_each_entry(other, &type->instance_list, type_list) {
			WARN_ON(copro != other &&
				copro->instance == other->instance);
		}
	}
}

/* New device tree layout for copros. Each copro instance has a node
 * of its own, such as:
 *
 * regx {
 *	compatible = "ibm,wsp-regx-1.0.0", "ibm,regx-1.0.0", "ibm,regx",
 *		     "ibm,coprocessor-type";
 *	ibm,coprocessor-reg = < x y >;
 * };
 *
 * Each node represents one instance of the copro type defined by the
 * compatible property. All instances of that type should have identical
 * compatible values. The name of the nodes should also match, these
 * are used as a "short name" for userspace.
 *
 * To build up copro types, we iterate through all instances (by finding
 * nodes compatible with ibm,coprocessor-type), and then attach them
 * to a type.
 */

int __init copro_types_init(void)
{
	struct copro_type *copro_type, *tmp;
	struct copro_instance *copro;
	struct platform_device *pdev;
	struct device_node *dn;
	const char *compat;
	int rc, n_types;

	INIT_LIST_HEAD(&copro_type_list);

	for_each_compatible_node(dn, NULL, COPRO_TYPE_COMPATIBLE) {

		pdev = of_find_device_by_node(dn);
		if (!pdev)
			continue;

		copro = dev_get_drvdata(&pdev->dev);
		if (!copro)
			continue;

		compat = of_get_property(dn, "compatible", NULL);
		BUG_ON(!compat);	/* we just searched on it */

		copro_type = copro_type_for_instance(copro, dn->name);
		if (!copro_type)
			continue;

		if (strcmp(copro_type->name, dn->name) != 0) {
			dev_err(&pdev->dev, "mismatching short name '%s'\n",
				dn->name);
			continue;
		}

		if (!compatible_with_type(copro_type, pdev))
			continue;

		list_add_tail(&copro->type_list, &copro_type->instance_list);
		copro_type->num_instances++;
	}

	list_for_each_entry_safe(copro_type, tmp, &copro_type_list, list) {
		rc = copro_type_topology_init(copro_type);
		if (rc)
			list_del(&copro_type->list);
	}

	n_types = 0;
	list_for_each_entry(copro_type, &copro_type_list, list) {
		check_instance_uniqueness(copro_type);

		/* Unfortunately some copro types don't implement sync */
		if (!strcmp(copro_type->name, "jppu") ||
		    !strcmp(copro_type->name, "xmlx") ||
		    !strcmp(copro_type->name, "pcieep") ||
		    !strcmp(copro_type->name, "dmax")) {
			copro_type->no_sync = true;
		} else {
			copro_type->no_sync = false;
		}

		pr_debug("Setup copro type %s, type 0x%x, "
			"with %d instances ", copro_type->name,
			copro_type->type, copro_type->num_instances);

		list_for_each_entry(copro, &copro_type->instance_list,
				    type_list) {
			printk(KERN_CONT "0x%x ", copro->instance);
		}
		printk("\n");

		n_types++;
	}

	return n_types == 0;
}
