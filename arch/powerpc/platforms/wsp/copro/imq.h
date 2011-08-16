#ifndef _WSP_COPRO_IMQ_H
#define _WSP_COPRO_IMQ_H

/*
 * Copyright 2008-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/copro-driver.h>

struct copro_imq {
	struct mutex lock;
	struct copro_unit *unit;
	struct work_struct work;
	struct copro_imq_entry *queue;
	struct copro_imq_entry *next;
	void __iomem *mmio_addr;
	unsigned int nr_entries;
	unsigned int virq;
	char *name;
	u8 valid;
	u8 number;
	u8 is_hv;
	u8 layout;
#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
	u8 paused;
#endif
};

extern int copro_imq_init(void);
extern void copro_imq_exit_mm_context(struct mm_struct *mm);
extern struct task_struct *cop_driver_handle_imqe(struct copro_imq *imq,
						  struct copro_imq_entry *imqe,
						  struct mm_struct *mm);
static inline void copro_imqe_set_valid(struct copro_imq_entry *imqe, u32 valid)
{
	imqe->flags &= ~(1u << 7);
	imqe->flags |= ((valid & 1) << 7);
}

static inline void copro_imqe_set_as(struct copro_imq_entry *imqe, u32 as)
{
	imqe->mm_info &= ~(1u << 23);
	imqe->mm_info |= ((as & 1) << 23);
}

extern int copro_unit_probe_imqs(struct platform_device *unit_pdev);

#endif	/* _WSP_COPRO_IMQ_H */
