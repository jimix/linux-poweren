/*
 * Copyright (C) 2011, 2012 IBM Corporation.
 * Author:	Davide Pasetto <pasetto_davide@ie.ibm.com>
 *			Karol Lynch <karol_lynch@ie.ibm.com>
 *			Kay Muller <kay.muller@ie.ibm.com>
 *			Jimi Xenidis <jimix@watson.ibm.com>
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

#ifndef RHEA_CHANNEL_RESOURCE_H_
#define RHEA_CHANNEL_RESOURCE_H_

#include <linux/list.h>

struct rhea_channel_resource_block {
	unsigned used;
	unsigned base;
	unsigned alloced;
	struct list_head list;
};

/*
 *	Struct which helps to keep track of all elements which have
 *	been assigned to a resource
 *
 *	curr_block		-	Pointer to current resource block
 *	next_elemeent	-	Pointer to next element of the same type
 */
struct rhea_channel_resource_element {
	struct rhea_channel_resource_block *curr_block;
	struct rhea_channel_resource_element *next_element;
};

/*
 * This struct is used to keep track of all resources which have been
 * allocated in one alloc
 *
 * element	-	Pointer to the first allocated element
 * list		-	Pointer to list element
 * init		-	Shows that the map has been initialised
 * block_count	-	Shows the number of blocks which have been assigned
 * base		-	Shows the base of that alloc
 * alloced	-	Shows the number of slots which have been allocated
 * bits		-	Number of bits used in this alloc (ld of alloced)
 */

struct rhea_channel_resource_map {
	struct rhea_channel_resource_element *element;
	struct list_head list;
	unsigned init;
	unsigned block_count;
	unsigned base;
	unsigned alloced;
	unsigned bits;
	unsigned int instance_count;
};

/*
 * This struct is used to show keep track of the allocated resources
 * of a channel
 *
 * block	-	This is the current resource block
 * wrap		-	Specifies, whether it is possible to combine the
 *			last and the first free block
 * contiguous	-	Specifies, whether the block to be allocated has
 *			to be of one big peace
 * block_count	-	Shows how many blocks have been allocated
 * allocated	-	Shows how many slots have been allocated for this
 *			resource
 * alloced_max	-	Shows the maximum number of slots which can be
 *			allocated for this device
 */
struct rhea_channel_resource {
	struct rhea_channel_resource_block block;
	unsigned wrap;
	unsigned contiguous;
	unsigned block_count;
	unsigned alloced;
	unsigned alloced_max;
};

extern struct rhea_channel_resource_block
	*_rhea_channel_resource_list_next(struct rhea_channel_resource_block
					  *block);

extern struct rhea_channel_resource_block
	*_rhea_channel_resource_list_prev(struct rhea_channel_resource_block
					  *block);

extern struct rhea_channel_resource_block
	 *_rhea_channel_resource_list_first(struct rhea_channel_resource *res);

extern struct rhea_channel_resource_block
	*_rhea_channel_resource_list_last(struct rhea_channel_resource *res,
					  unsigned alloced_max);

extern int _rhea_channel_resource_init(struct rhea_channel_resource *res,
				       unsigned max_size,
				       unsigned contiguous,
				       unsigned wrap,
				       unsigned add_first_element);

extern int _rhea_channel_resource_fini(struct rhea_channel_resource *res);

extern int _rhea_channel_map_init(struct rhea_channel_resource_map **map);
extern int _rhea_channel_map_fini(struct rhea_channel_resource *res,
				  struct rhea_channel_resource_map **map);

extern int _rhea_channel_map_share(struct rhea_channel_resource_map
				   **target_map,
				   const struct rhea_channel_resource_map
				   *source_map);

extern int _rhea_channel_resource_max(struct rhea_channel_resource *res,
				      unsigned requested_slots,
				      struct rhea_channel_resource_block
				      **block_max);

extern int _rhea_channel_resource_min(struct rhea_channel_resource *res,
				      unsigned requested_slots,
				      struct rhea_channel_resource_block
				      **block_min);

extern int _rhea_channel_resource_alloc(struct rhea_channel_resource *res,
					struct rhea_channel_resource_map
					*map_res,
					unsigned requested_slots,
					unsigned *slot_bas);

extern int _rhea_channel_resource_free(struct rhea_channel_resource *res,
				       struct rhea_channel_resource_map
				       *map_res, unsigned base);

extern int _rhea_channel_resource_index_get(struct rhea_channel_resource_map
					    *map, unsigned base,
					    unsigned offset,
					    unsigned *slot_index);

extern struct rhea_channel_resource_map *_rhea_channel_resource_map_get(
	struct rhea_channel_resource_map *map_res, unsigned base);

extern unsigned rhea_channel_resource_map_element_count(
	struct rhea_channel_resource_map *map);

extern struct rhea_channel_resource_map
	*_rhea_channel_resource_map_element_first(
		struct rhea_channel_resource_map *map);

extern struct rhea_channel_resource_map
	*_rhea_channel_resource_map_element_next(
		struct rhea_channel_resource_map *map, unsigned int index);

#endif /* RHEA_CHANNEL_RESOURCE_H_ */
