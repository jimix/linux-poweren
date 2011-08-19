/*
 * Copyright 2010-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/mm.h>

#include <asm/scom.h>

#include "cop.h"
#include "unit.h"

#define XML_UNIT_COMPATIBLE     "ibm,wsp-coprocessor-xmlx"

struct xml_mem {
	struct device_node *dn;
	u64 addr;
	int inited;
	struct copro_unit *unit;
};

static struct xml_mem *xml_mem;
static int num_xmlx;

void __init wsp_xml_memory_alloc(void)
{
	struct device_node *dn;
	int i, size;
	u64 pa;

	num_xmlx = 0;
	for_each_compatible_node(dn, NULL, XML_UNIT_COMPATIBLE)
		num_xmlx++;

	if (num_xmlx == 0) {
		cop_debug("no xmlx found\n");
		return;
	}

	size = num_xmlx * sizeof(struct xml_mem);
	pa = memblock_alloc_base(size, sizeof(struct xml_mem), 0x40000000u);
	xml_mem = (struct xml_mem *)__va(pa);
	memset(xml_mem, 0, size);

	/* The XML coprocessor needs large blocks of contiguous physical
	 * memory, to make sure we get it we need to preallocate it. */
	i = 0;
	for_each_compatible_node(dn, NULL, XML_UNIT_COMPATIBLE)
	{
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

		pa = memblock_alloc_base(452*1024*1024, 256*1024*1024, 0);
		if (pa == 0) {
			pr_err("xmlx: Failed allocating memory for %s\n",
				dn->full_name);
			continue;
		}

		xml_mem[i].addr = pa;
		xml_mem[i].dn = of_node_get(dn);
		pr_debug("xmlx: Allocated 452M for %s at %#llx\n",
			dn->full_name, xml_mem[i].addr);
		i++;
	}
}

u64 wsp_xml_get_mem_addr(struct device_node *dn)
{
	int i;

	for (i = 0; i < num_xmlx; i++) {
		if (xml_mem[i].dn == dn)
			return xml_mem[i].addr;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wsp_xml_get_mem_addr);

u64 wsp_xml_get_mem_addr_index(unsigned index)
{
	if (index >= num_xmlx)
		return 0;
	return xml_mem[index].addr;
}
EXPORT_SYMBOL_GPL(wsp_xml_get_mem_addr_index);

u32 wsp_xml_get_num_device(void)
{
	return num_xmlx;
}
EXPORT_SYMBOL_GPL(wsp_xml_get_num_device);

struct copro_unit *wsp_xml_get_copro_device(int index)
{
	struct copro_unit *unit;
	struct copro_instance *copro;
	void __iomem *mmio_addr;
	struct pbic *pbic;
	const u32 *p;
	int rc, len;
	struct platform_device *pdev;

	struct device_node *dn;

	if (index >= num_xmlx)
		return NULL;

	if (1 == xml_mem[index].inited)
		return xml_mem[index].unit;

	dn = xml_mem[index].dn;

	/***
	   getting the copro_type info
	   copro_type.c:: int __init copro_types_init(void)
	*/
	pdev = of_find_device_by_node(dn);
	copro = dev_get_drvdata(&pdev->dev);


	p = of_get_property(dn, "ibm,pbic-unit-number", &len);
	if (!p || len < sizeof(u32)) {
		pr_err("invalid ibm,pbic-unit-number\n");
		return NULL;
	}

	pbic = pbic_get_parent_pbic(dn);
	if (!pbic) {
		pr_err("no PBIC found?\n");
		return NULL;
	}

	mmio_addr = of_iomap(dn, 0);
	if (mmio_addr == NULL) {
		pr_err("couldn't iomap reg\n");
		return NULL;
	}

	unit = kzalloc(sizeof(*unit), GFP_KERNEL);
	if (!unit) {
		rc = -ENOMEM;
		/* FIXME iounmap(mmio_addr); */
		return NULL;
	}

	unit->dn = of_node_get(dn);
	unit->mmio_addr = mmio_addr;
	unit->number = p[0];
	unit->index = index;
	unit->pbic = pbic;
	/* perhaps an inappropriate use of the types field */
	unit->types = copro->type;

	/* CHECKME copro_unit_debug_init(unit); */

	return unit;
}
EXPORT_SYMBOL_GPL(wsp_xml_get_copro_device);

#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
static struct reg_range xmlx_debug_regs[] = {
	{   0x0,	17 },	/* XMLFG .. XMLTMS */
	{  0x98,	12 },	/* XMLFWK_0 .. XMLINVC_3 */
	{ 0x100,	35 },	/* XMLISN_0 .. XMLIS_4 */
	{ 0x268,	 6 },	/* XMLESR_0 .. XMLMTR */
	{ 0x300,	13 },	/* QCFPE_0 .. X2XMBX */
	{ 0x400,	23 },	/* XMLGPCCR00 .. XMLDBGCT */
	{   0x0,	 0 },	/* End */
};
#else
#define xmlx_debug_regs	NULL
#endif

static struct copro_unit_regs xmlx_regs = {
	.marker_trace = 0x290,
};

static int xmlx_unit_probe(struct platform_device *pdev)
{
	struct copro_unit *unit;
	int i, rc;

	rc = copro_unit_probe(pdev);
	if (rc)
		return rc;

	unit = dev_get_drvdata(&pdev->dev);
	unit->regs = &xmlx_regs;
	copro_unit_set_debug_regs(unit, xmlx_debug_regs);

	for (i = 0; i < 11; i++)
		pbic_alloc_spill_queue(unit, i);

	dev_printk(KERN_DEBUG, &pdev->dev, "bound to xmlx\n");
#ifdef CONFIG_WORKAROUND_ERRATUM_512
	{
		struct device_node *dn;
		for_each_compatible_node(dn, NULL, XML_UNIT_COMPATIBLE)	{
			struct device_node *xscom;
			scom_map_t map;
			pr_debug("Unsetting the parity check in XML memory\n");

			xscom = scom_find_parent(dn);
			pr_debug("xscom: %llx\n", (u64)xscom);
			map = scom_map(xscom, 0x0c010900, 8);
			pr_debug("map: %llx\n", (u64)map);
			scom_write(map, 3, 0);
			scom_write(map, 4, 0);

			scom_unmap(map);
			pr_debug("Unset DONE\n");
		}
	}
#endif

	return 0;
}

static const struct of_device_id xmlx_unit_device_id[] = {
	{ .compatible	= "ibm,wsp-coprocessor-xmlx" },
	{}
};

static struct platform_driver xmlx_unit_driver = {
	.probe		= xmlx_unit_probe,
	.driver		= {
		.name	= "wsp-copro-xmlx-unit",
		.owner	= THIS_MODULE,
		.of_match_table = xmlx_unit_device_id,
	},
};

int __init xmlx_unit_init(void)
{
	return platform_driver_register(&xmlx_unit_driver);
}
__copro_unit(xmlx, xmlx_unit_init);
