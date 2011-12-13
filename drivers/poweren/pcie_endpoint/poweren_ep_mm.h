/*
 *  PowerEN PCIe Endpoint Device Driver
 *
 *  Copyright 2010-2011, IBM Corp.
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

#ifndef __POWEREN_EP_MM_H__
#define __POWEREN_EP_MM_H__

#include <linux/types.h>

#include "poweren_ep_vf.h"

#define LARGE_SLAB_NUM	100ull
#define SMALL_SLAB_NUM	1024ull
#define TINY_SLAB_NUM	512ull

struct poweren_ep_slab_map {

	int dma_large_slab[LARGE_SLAB_NUM];
	int dma_small_slab[SMALL_SLAB_NUM];
	int dma_tiny_slab[TINY_SLAB_NUM];

	u64 mmio_space;
	u64 dma_large_slab_space;
	u64 dma_small_slab_space;
	u64 dma_tiny_slab_space;

	int dma_large_slab_index;
	int dma_small_slab_index;
	int dma_tiny_slab_index;
};

/** Not exported functions **/
extern struct poweren_ep_slab_map *poweren_ep_domain_mem_map_alloc(void);
extern void poweren_ep_domain_mem_map_free(struct poweren_ep_slab_map *mem_map);

/** Exported functions **/
extern dma_addr_t poweren_ep_get_dma_addr(struct poweren_ep_vf *vf,
		size_t size);
extern void poweren_ep_release_dma_addr(struct poweren_ep_vf *vf,
		dma_addr_t dma_addr);
extern int poweren_ep_reserve_dma_addr(struct poweren_ep_vf *vf,
		dma_addr_t dma_addr);

#endif /* __POWEREN_EP_MM_H__ */
