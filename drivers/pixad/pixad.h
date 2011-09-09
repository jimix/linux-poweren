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
#ifndef _DRIVERS_PIXAD_HEADER_H_
#define _DRIVERS_PIXAD_HEADER_H_

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/mmu_notifier.h>
#include <platforms/wsp/copro/unit_xmlx.h>

#define PIXAD_MAJOR 32
#define PIXAD_NAME "PiXAD"

#ifdef DEBUG
#define pixad_debug(fmt, ...) pr_debug("[%s: in %s()] " fmt,\
				PIXAD_NAME, __func__, ##__VA_ARGS__)
#else
#define pixad_debug(fmt, ...)
#endif

#define pixad_info(fmt, ...) pr_info("[%s: in %s()] " fmt,\
			PIXAD_NAME, __func__, ##__VA_ARGS__)
#define pixad_err(fmt, ...) pr_err("[%s: in %s()] " fmt,\
			PIXAD_NAME, __func__, ##__VA_ARGS__)

/* IMQ size is 16K */
#define IMQ_MAX_INDEX		(16 << 10)
#define IMQ_MAX_INDEX_MASK	0xC0000

enum map_target {
	MAP_TARGET_NA = 0,
	MAP_TARGET_QPOOL = 1,
	MAP_TARGET_IMQ = 2,
	MAP_TARGET_MMIO = 3,
	MAP_TARGET_TAKEDOWN_MPOOL = 4,
	MAP_TARGET_MAX_COUNT = 5
};

struct pixad_mem_pool {
	unsigned long uv_start;
	unsigned long kv_start;
	unsigned long size;
	unsigned long cur_ptr;
	struct page *pg;
};

struct pixad_user_proc {
	struct file *descriptor;
	unsigned int vf_id;
	unsigned int unit_id;
	u64 map_addr;
	enum map_target map_target;
	bool is_mapped[MAP_TARGET_MAX_COUNT];
	struct list_head list;
	struct pixad_mem_pool mempool;
	struct mmu_notifier mmu_notifier;
};

struct pixad_xml_vf {
	/* XML Data Structures Memory Info */
	u64 phys_qpool;			/* 32MB ALIGNED */
	u64 virt_qpool;
	unsigned int size_qpool;	/* 20MB (+padding 12) */

	u64 phys_session;		/* 16MB ALIGNED */
	u64 virt_session;
	unsigned int size_session;	/* { 16MB | 1GB } */

	u64 virt_imq;			/* size align */
	u64 phys_imq;
	unsigned int size_imq;		/* from 128 byte to 1M */
	unsigned int max_index_imq;	/* from 8 to 65536. Default 16384 */

	/* SOME info on VF and the user */
	int num_users;

	u32 curr_qpid;

	/* lock for add/remove entry in proc_list */
	spinlock_t vf_lock;
	/* Used only in the shared case */
	struct pixad_user_proc proc_list;
};

struct pixad_copro {
	u64 phys_transient;		/* size_fixed ALIGED */
	u64 virt_transient;
	unsigned int size_transient;	/* {256MB | 512MB | 1GB} */

	struct pixad_xml_vf vf_info[XMLX_VF_PER_UNIT];
	struct copro_unit *copro_unit;
};

struct qpool_packet_offset_info {
	int align_val;
	int qcode_entry;
};

extern int mpool_alloc(struct pixad_mem_pool *pool, unsigned int size,
			unsigned int align, void **uvaddr, void **kvaddr);

#endif /* _DRIVERS_PIXAD_HEADER_H_ */
