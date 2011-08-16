#ifndef _WSP_COPRO_COP_H
#define _WSP_COPRO_COP_H

/*
 * Copyright 2008-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#define COPRO_TYPE_COMPATIBLE	"ibm,coprocessor-type"
#define WSP_SOC_COMPATIBLE	"ibm,wsp-soc"
#define PBIC_COMPATIBLE		"ibm,wsp-pbic"
#define COPRO_COMPATIBLE	"ibm,wsp-coprocessor"

struct reg_range {
	s16 start;
	s16 count;
};

struct copro_unit_regs {
	s16 marker_trace;
	struct reg_range abort;	/* Start & number of abort register pairs */
};

/* There are only enough bits in the IMQE for IMQ numbers 0-7 */
#define MAX_NUMBER_OF_IMQS	8

struct copro_unit {
	struct pbic *pbic;
	void __iomem *mmio_addr;
	const struct file_operations *fops;
	char name[16];
	struct copro_imq *imq[MAX_NUMBER_OF_IMQS];
	struct device_node *dn;
	/* For use by copro-type specific drivers */
	void *priv;
#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
	struct dentry *dentry;
	struct reg_range *debug_regs;
#endif
	struct copro_unit_regs *regs;
	/* PBIC unit number */
	u32 number;
	/* Mask of copro types below this unit */
	u32 types;
	/* Kernel index, just a unique ID per unit */
	u16 index;
	/* Number of IMQs on this unit */
	u16 nr_imqs;
};

struct copro_instance {
	struct copro_unit *unit;
	struct device_node *dn;
	struct list_head type_list;
	u32 type;
	u32 instance;
	char name[32];
#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
	struct dentry *dentry;
#endif
};

extern struct list_head copro_type_list;

struct copro_type {
	struct list_head list;
	struct list_head instance_list;
	struct copro_instance **default_map;
	char *name;
	u32 num_instances;
	u32 type;
	bool no_sync;
};

extern int context_enable_copro_access(unsigned int type);

extern int copro_types_init(void);
extern int copro_driver_init(void);
extern int copro_sync_init(void);
extern int copro_type_topology_init(struct copro_type *copro_type);
extern int copro_instance_init(void);
extern void copro_sysctl_init(void);

/*
 * NB. Although RFC 2130 specifies CSB alignment as 16 bytes, the PBICs
 * require the CSB to 128 byte aligned (§ 4.3.8.1).
 */
#define COP_CXB_SIZE	128
#define COP_CXB_ALIGN	128

extern struct kmem_cache *cop_cxb_cache;

static inline void *cop_cxb_alloc(gfp_t flags)
{
	void *p;

	p = kmem_cache_zalloc(cop_cxb_cache, flags);

	BUG_ON(!p);
	BUG_ON(!IS_ALIGNED((unsigned long)p, COP_CXB_ALIGN));

	return p;
}

static inline void cop_cxb_free(void *cxb)
{
	kmem_cache_free(cop_cxb_cache, cxb);
}

#ifdef DEBUG
extern u32 copro_debug;
extern u32 copro_raw_reg_enabled;
#define cop_debug(fmt...)	if (copro_debug) trace_printk(fmt)
#else
#define cop_debug(fmt,...)	no_printk(pr_fmt(fmt), ##__VA_ARGS__)
#endif

#define pbic_debug(_pbic, fmt, ...)	\
	cop_debug("%s " fmt, _pbic->name, ##__VA_ARGS__)

static inline int read_coprocessor_reg(struct device_node *dn, u32 *ct, u32 *ci)
{
	const u32 *p;
	int len;

	p = of_get_property(dn, "ibm,coprocessor-reg", &len);
	if (!p || len != (2 * sizeof(u32)))
		return -EINVAL;

	*ct = p[0];
	*ci = p[1];

	return 0;
}

static inline int csb_wait_valid(void *p)
{
	unsigned long timeout;
	u8 *csb_valid = p;

	/* How long is too long? */
	timeout = jiffies + 5 * HZ;

	while ((*csb_valid >> 7) == 0) {
		if (time_after(jiffies, timeout)) {
			WARN_ON(1);
			return -ETIMEDOUT;
		}

		barrier();
	}

	return 0;
}

#define COP_CRB_CSB_C		0x8ull
#define COP_CRB_CSB_UNUSED	0x4ull
#define COP_CRB_CSB_AT		0x2ull
#define COP_CRB_CSB_M		0x1ull
#define COP_CRB_CSB_BIT_MASK	0xFull
#define COP_CRB_CSB_ADDR(x)	((x) & ~(COP_CRB_CSB_BIT_MASK))

#endif /* _WSP_COPRO_COP_H */
