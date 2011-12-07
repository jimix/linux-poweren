/*
 * Copyright (C) 2011 IBM Corporation.
 * Author:	Davide Pasetto <pasetto_davide@ie.ibm.com>
 *		Karol Lynch <karol_lynch@ie.ibm.com>
 *		Kay Muller <kay.muller@ie.ibm.com>
 *		Jimi Xenidis <jimix@watson.ibm.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http:/www.gnu.org/licenses/>.
 */

#ifndef _NET_POWEREN_RHEA_LINUX_H_
#define _NET_POWEREN_RHEA_LINUX_H_

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <asm/abs_addr.h>
#include <linux/io.h>

#include <linux/types.h>
#include <linux/delay.h>

#include <rhea.h>
#include <asm/poweren_hea_common_types.h>
#include <rhea-interface.h>

/********************* MACROS *********************************/

#define rhea_info(fmt, args...)			\
	pr_info(DRV_NAME ": " fmt "\n", ## args)

#define rhea_warning(fmt, args...)		\
	pr_warning(DRV_NAME ": " fmt "\n", ## args)

#define rhea_error(fmt, args...)					\
	pr_err(DRV_NAME ": Error in %s: " fmt "\n", __func__, ## args)

#define rhea_debug(fmt, args...)		\
	pr_debug(DRV_NAME ": " fmt, ## args)

/***************** Function prototypes ********************/

extern void rhea_iounmap(void __iomem *addr,
			 unsigned long phys_addr,
			 unsigned long size);

extern void __iomem *rhea_ioremap(const char *name, phys_addr_t addr,
				  unsigned long size);

extern int rhea_irq_request(u32 hwirq, hea_irq_handler_t handler,
			    unsigned long irq_flags, const char *devname,
			    void *dev_id);

extern void rhea_irq_free(unsigned virq, void *dev_id);

/*
 * Allocates memory with the alignment align.
 */
extern void *rhea_align_alloc(ulong size, ulong align,
			      unsigned flags);

/*
 * Frees a pointer which was allocated with rhea_align_alloc
 */
extern void rhea_align_free(void *ptr, ulong size);

/*
 * Allocates memory with the alignment of 8.
 */
extern void *rhea_alloc(ulong size, unsigned flags);

/*
 * Frees a pointer which was allocated with rhea_alloc
 */
extern void rhea_free(void *ptr, ulong size);

/**
 * Allocates a contiguous number of pages so that
 * size fits into the memory block
 */
extern void *rhea_pages_alloc(ulong size, unsigned flags);

/**
 * Frees memory block which was allocated with rhea_pages_alloc()
 */
extern void rhea_pages_free(void *ptr, ulong size);

/********************* Dumping registers **********************/

extern u64 rhea_reg_print_internal(u64 *addr, const char *fmt, ...);

#define rhea_reg_print(base, member, fmt, ...)			\
({								\
	ulong __addr = (ulong)(base);				\
	__addr += offsetof(__typeof__(*(base)), member);	\
	rhea_reg_print_internal((u64 *)__addr, fmt, ##__VA_ARGS__);	\
})

/******************* Send signal to user process ***************/

int hea_signal_send(struct hea_process *process, int signal_nr);

#endif /* _NET_POWEREN_RHEA_LINUX_H_ */
