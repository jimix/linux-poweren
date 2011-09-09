/*
 * IBM Power Edge of Network (PowerEN)
 *
 * Copyright 2010-2011 Massimiliano Meneghin, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _DRIVERS_PIXAD_MEM_UTILS_H_
#define _DRIVERS_PIXAD_MEM_UTILS_H_

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include "pixad.h"
#include <platforms/wsp/copro/cop.h>

#define KB(X) (X##UL << 10)
#define MB(X) (X##UL << 20)

#define MAX_TRANSIENT_MEM_ENTRIES	(256UL << 10)
#define TRANSIENT_PTR_BASE		0x100
#define TRANSIENT_SIZE_BASE		0x7UL
#define TRANSIENT_SIZE_MASK		0x70000000UL
#define FIXED_SESSION_SIZE_BASE		0x3FUL
#define FIXED_SESSION_SIZE_MASK		0x3F000000UL


extern void init_xml_data_structures(unsigned int copro_num,
					struct pixad_copro *copro_info);
extern int pixad_init_xml_unit(struct pixad_copro *copro_info);

extern void pixad_init_vf(struct pixad_copro *copro_info, int index);

extern void run_vf(struct pixad_copro *copro_info, int index);


/* Represent the layout of qpool memory used by XML HW.
 * reference: <XML workbook v0.91> #2.7.4, #5.1.2
 */
struct xml_qm_t {
	/* First 16MBs of Qpool memory: */
	u8 qcode_memory[MB(16)];

	/* Prefix/Local/URI tables. 64K entries(16 bits per entry) per table: */
	u16 free_pool[3][KB(64)];

	/* Uninitialized area: */
	u8 unused[MB(2) - (3 * KB(64) * 2)];

	/* Table of 425984 32 bit values: */
	u32 expansion_free_pool[0x68000];
};


struct transient_mem {
	/* pointers to transient pages */
	u32 ptrs[MAX_TRANSIENT_MEM_ENTRIES];
};

struct state_session_entry {
	u8 reserved[255];
	u8 status;
};

#endif /* _DRIVERS_PIXAD_MEM_UTILS_H_ */
