/*
 * Copyright 2010-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/cpumask.h>
#include <linux/of.h>
#include "cop.h"


static u32 __init read_default_copro_map(struct device_node *dn, u32 type)
{
	const u32 *prop, *p;
	int len;

	prop = of_get_property(dn, "ibm,default-coprocessor-map", &len);
	if (!prop || !len)
		return 0;

	for (p = prop; p < prop + len / sizeof(u32); p += 2) {
		if (*p == type)
			return *(p + 1);
	}

	return 0;
}

static u32 __init default_ci_for_cpu(int cpu, u32 type)
{
	struct device_node *dn, *tmp;
	const u32 *p;
	u32 ci = 0;

	dn = of_get_cpu_node(cpu, NULL);
	if (!dn)
		goto out;

	/* Check on the cpu node itself */
	ci = read_default_copro_map(dn, type);
	if (ci)
		goto out;

	/* Traverse to the at-node */
	p = of_get_property(dn, "at-node", NULL);
	if (!p)
		goto out;

	tmp = of_find_node_by_phandle(*p);
	if (!tmp)
		goto out;

	dn = tmp;

	/* Search up to the root node from the at-node */
	do {
		ci = read_default_copro_map(dn, type);
		if (ci)
			goto out;

		dn = of_get_next_parent(dn);
	} while (dn);

out:
	of_node_put(dn);
	return ci;
}

static struct copro_instance * __init
default_copro_for_cpu(struct copro_type *type, int cpu)
{
	struct copro_instance *copro;
	u32 ci;

	ci = default_ci_for_cpu(cpu, type->type);
	if (ci) {
		list_for_each_entry(copro, &type->instance_list, type_list)
			if (copro->instance == ci)
				return copro;
	}

	return NULL;
}

int __init copro_type_topology_init(struct copro_type *copro_type)
{
	struct copro_instance *copro, **default_map;
	int cpu, found;

	default_map = kmalloc(sizeof(struct copro_instance *) *
			      num_possible_cpus(), GFP_KERNEL);
	if (!default_map) {
		printk(KERN_ERR "%s: no memory for copro map!\n", __func__);
		return -ENOMEM;
	}

	found = 0;
	for_each_possible_cpu(cpu) {
		copro = default_copro_for_cpu(copro_type, cpu);
		default_map[cpu] = copro;
		if (copro) {
			cop_debug("Default %s on cpu %d is 0x%x\n",
				  copro_type->name, cpu, copro->instance);
			found++;
		}
	}

	if (found)
		copro_type->default_map = default_map;
	else
		kfree(default_map);

	return 0;
}
