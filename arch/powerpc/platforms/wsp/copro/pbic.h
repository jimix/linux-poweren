#ifndef _WSP_COPRO_PBIC_H
#define _WSP_COPRO_PBIC_H

/*
 * Copyright 2008-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/copro-driver.h>

#define TS_ANY 2

struct pbic_tlb_info {
	u64 address;
	int pid;
	int count;
	int ts;
};

struct pbic_spillq {
	int reg_offset;
	int size;
	void *buffer;
};

struct pbic {
	spinlock_t lock;
	u32 type;
	u32 instance;
	u32 watermark;
	u32 next_slot;
	u32 tlb_size;
	u8 index;
	void __iomem *mmio_addr;
	struct delayed_work maint_q;
	struct list_head list;
	struct pbic_crb *crb;
	struct pbic_csb *csb;
	struct pbic_tlb_info *tlb_info;
	char name[16];
	struct device_node *dn;
#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
	struct dentry *dentry;
#endif
#ifdef DEBUG
	u64 invalidate_counter;
#endif
};

extern int pbic_driver_init(void);
extern struct pbic *pbic_get_parent_pbic(struct device_node *dn);
extern void pbic_put_parent_pbic(struct pbic *pbic);

extern int pbic_map_args_ioctl(struct pbic *pbic, unsigned int cmd,
			       void __user *uptr);

extern int pbic_map(struct pbic *pbic, struct copro_map_args *args);
extern int pbic_unmap(struct pbic *pbic, struct copro_map_args *args);
extern void pbic_configure_marker_trace(struct pbic *pbic,
					struct copro_instance *copro,
					int enable, u64 flags);
extern int pbic_alloc_spill_queue(struct copro_unit *unit, int qnum);
extern int pbic_user_flush(struct pbic *pbic, unsigned long address);

#define pbic_debug(_pbic, fmt, ...)	\
	cop_debug("%s " fmt, _pbic->name, ##__VA_ARGS__)

#endif /* _WSP_COPRO_PBIC_H */
