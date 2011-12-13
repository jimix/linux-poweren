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

#include "poweren_ep_mm.h"
#include "poweren_ep_driver.h"

/* we have 2^38 bits available, so chop it into slabs */
#define MMIO_SLAB_SIZE	0x10000000ull	/* 256 MB */
#define LARGE_SLAB_SIZE	0x40000000ull	/* 1   GB */
#define SMALL_SLAB_SIZE	0x100000ull	/* 1   MB */
#define TINY_SLAB_SIZE	0x10000ull	/* 64  KB */

#define MMIO_AREA	0x180000000ull			       /* 6GB */
#define DMA_LARGE_SLAB_AREA (LARGE_SLAB_SIZE * LARGE_SLAB_NUM) /* 248GB */
#define DMA_SMALL_SLAB_AREA (SMALL_SLAB_SIZE * SMALL_SLAB_NUM) /* 1GB */
#define DMA_TINY_SLAB_AREA  (TINY_SLAB_SIZE  * TINY_SLAB_NUM)  /* 1MB */

/* get/initiate the memory map for a domain  */
struct poweren_ep_slab_map *poweren_ep_domain_mem_map_alloc(void)
{
	struct poweren_ep_slab_map *mem_map;
	int i;

	mem_map = kzalloc(sizeof(*mem_map), GFP_KERNEL);
	if (!mem_map) {
		poweren_ep_error(" ERROR allocating slab map struct\n");
		return NULL;
	}

	mem_map->mmio_space = 0;
	mem_map->dma_large_slab_space = mem_map->mmio_space + MMIO_AREA;
	mem_map->dma_small_slab_space = mem_map->dma_large_slab_space +
		DMA_LARGE_SLAB_AREA;
	mem_map->dma_tiny_slab_space = mem_map->dma_small_slab_space +
		DMA_SMALL_SLAB_AREA;

	mem_map->dma_large_slab_index = 0;
	mem_map->dma_small_slab_index = 0;
	mem_map->dma_tiny_slab_index = 0;

	for (i = 0; i < LARGE_SLAB_NUM; i++)
		mem_map->dma_large_slab[i] = 0;

	for (i = 0; i < SMALL_SLAB_NUM; i++)
		mem_map->dma_small_slab[i] = 0;

	for (i = 0; i < TINY_SLAB_NUM; i++)
		mem_map->dma_tiny_slab[i] = 0;

	poweren_ep_debug("mem map large %llx, small %llx , tiny DMA %llx\n",
			mem_map->dma_large_slab_space,
			mem_map->dma_small_slab_space,
			mem_map->dma_tiny_slab_space);

	return mem_map;
}

void poweren_ep_domain_mem_map_free(struct poweren_ep_slab_map *mem_map)
{
	int i;

	for (i = 0; i < LARGE_SLAB_NUM; i++)
		mem_map->dma_large_slab[i] = 0;

	for (i = 0; i < SMALL_SLAB_NUM; i++)
		mem_map->dma_small_slab[i] = 0;

	for (i = 0; i < TINY_SLAB_NUM; i++)
		mem_map->dma_tiny_slab[i] = 0;

	kfree(mem_map);
}

/* return an address */
dma_addr_t poweren_ep_get_dma_addr(struct poweren_ep_vf *vf, size_t size)
{
	struct poweren_ep_slab_map *mem_map;
	dma_addr_t ret_addr = 0;
	int i, j;

	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return 0;
	}

	mem_map = poweren_ep_get_slab_map(vf);

	/* use the next available slot for the size */
	/* consider allowing sizes larger than a large slab (1GB) */

	if (size > SMALL_SLAB_SIZE) {
		i = mem_map->dma_large_slab_index;

		for (j = 0; j < LARGE_SLAB_NUM; j++) {
			if (mem_map->dma_large_slab[i] == 0) {
				ret_addr = mem_map->dma_large_slab_space +
					(LARGE_SLAB_SIZE * i);
				mem_map->dma_large_slab[i] = 1;
				mem_map->dma_large_slab_index = (i + 1) %
					LARGE_SLAB_NUM;

				poweren_ep_debug("found large slab %d,"
						" address %llx\n", i, ret_addr);
				break;
			}
			i = (i + 1) % LARGE_SLAB_NUM;
		}
	} else if (size <= SMALL_SLAB_SIZE && size > TINY_SLAB_SIZE) {
		i = mem_map->dma_small_slab_index;

		for (j = 0; j < SMALL_SLAB_NUM; j++) {
			if (mem_map->dma_small_slab[i] == 0) {
				ret_addr = mem_map->dma_small_slab_space +
					(SMALL_SLAB_SIZE * i);
				mem_map->dma_small_slab[i] = 1;
				mem_map->dma_small_slab_index = (i + 1) %
					SMALL_SLAB_NUM;

				poweren_ep_debug("found small slab %d,"
						" address %llx\n", i, ret_addr);
				break;
			}
			i = (i + 1) % SMALL_SLAB_NUM;
		}
	} else {
		i = mem_map->dma_tiny_slab_index;

		for (j = 0; j < TINY_SLAB_NUM; j++) {
			if (mem_map->dma_tiny_slab[i] == 0) {
				ret_addr = mem_map->dma_tiny_slab_space +
					(TINY_SLAB_SIZE * i);
				mem_map->dma_tiny_slab[i] = 1;
				mem_map->dma_tiny_slab_index = (i + 1) %
					TINY_SLAB_NUM;

				poweren_ep_debug("found tiny slab %d,"
						" address %llx\n", i, ret_addr);
				break;
			}
			i = (i + 1) % TINY_SLAB_NUM;
		}
	}
	return ret_addr;
}
EXPORT_SYMBOL_GPL(poweren_ep_get_dma_addr);

void poweren_ep_release_dma_addr(struct poweren_ep_vf *vf,
		dma_addr_t dma_addr)
{
	/*
	 * find out what memory range the addr is in and mark
	 * that slab as free
	 */
	int i;
	struct poweren_ep_slab_map *mem_map;

	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return;
	}

	mem_map = poweren_ep_get_slab_map(vf);

	if (dma_addr < mem_map->dma_small_slab_space &&
			dma_addr >= mem_map->dma_large_slab_space) {

		i = (dma_addr - mem_map->dma_large_slab_space) /
			LARGE_SLAB_SIZE;

		mem_map->dma_large_slab[i] = 0;
		poweren_ep_debug("reset large slab %d, for address %llx\n",
				i, dma_addr);

	} else if (dma_addr < mem_map->dma_tiny_slab_space &&
			dma_addr >= mem_map->dma_small_slab_space) {

		i = (dma_addr - mem_map->dma_small_slab_space) /
			SMALL_SLAB_SIZE;

		mem_map->dma_small_slab[i] = 0;
		poweren_ep_debug("reset small slab %d, for address %llx\n",
				i, dma_addr);

	} else {
		i = (dma_addr - mem_map->dma_tiny_slab_space) /
			TINY_SLAB_SIZE;

		mem_map->dma_tiny_slab[i] = 0;
		poweren_ep_debug("reset tiny slab %d, for address %llx\n",
				i, dma_addr);

	}
}
EXPORT_SYMBOL_GPL(poweren_ep_release_dma_addr);

int poweren_ep_reserve_dma_addr(struct poweren_ep_vf *vf,
		dma_addr_t dma_addr)
{
	/*
	 * find out what memory range the addr is in and if that slab is free
	 * then reserve it
	 *
	 * consider a size thing here in case the address requested assumes
	 * a size that overruns a slab boundary
	 */

	int i;
	struct poweren_ep_slab_map *mem_map;

	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return 0;
	}

	mem_map = poweren_ep_get_slab_map(vf);

	if (dma_addr < mem_map->dma_small_slab_space &&
			dma_addr >= mem_map->dma_large_slab_space) {

		i = (dma_addr - mem_map->dma_large_slab_space) /
			LARGE_SLAB_SIZE;

		poweren_ep_debug("search large slab %d, for address %llx  %d\n",
				i, dma_addr, mem_map->dma_large_slab[i]);


		if (mem_map->dma_large_slab[i] == 0) {
			mem_map->dma_large_slab[i] = 1;
			return 1;
		} else
			return 0;

	} else if (dma_addr < mem_map->dma_tiny_slab_space &&
			dma_addr >= mem_map->dma_small_slab_space) {

		i = (dma_addr - mem_map->dma_small_slab_space) /
			SMALL_SLAB_SIZE;

		mem_map->dma_small_slab[i] = 1;

		poweren_ep_debug("search small slab %d, for address %llx  %d\n",
				i, dma_addr, mem_map->dma_small_slab[i]);

		if (mem_map->dma_small_slab[i] == 0) {
			mem_map->dma_small_slab[i] = 1;
			return 1;
		} else
			return 0;


	} else {
		i = (dma_addr - mem_map->dma_tiny_slab_space) /
			TINY_SLAB_SIZE;

		mem_map->dma_tiny_slab[i] = 1;

		poweren_ep_debug("search tiny slab %d, for address %llx  %d\n",
				i, dma_addr, mem_map->dma_tiny_slab[i]);

		if (mem_map->dma_tiny_slab[i] == 0) {
			mem_map->dma_tiny_slab[i] = 1;
			return 1;
		} else
			return 0;

	}
}
EXPORT_SYMBOL_GPL(poweren_ep_reserve_dma_addr);
