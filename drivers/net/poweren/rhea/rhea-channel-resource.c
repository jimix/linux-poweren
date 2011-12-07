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

#include "rhea-channel-resource.h"
#include "rhea.h"
#include "rhea-funcs.h"

struct rhea_channel_resource_map *_rhea_channel_resource_map_get(
	struct rhea_channel_resource_map *map_res,
	unsigned base)
{
	struct rhea_channel_resource_map *map_return = NULL;

	if (NULL == map_res)
		return NULL;

	/* find correct map */
	list_for_each_entry(map_return, &map_res->list, list) {
		if (base == map_return->base) {
			/* found the first */
			break;
		}
	}

	if (NULL == map_return) {
		rhea_error("Was not able to find base in map!");
		return NULL;
	}

	return map_return;
}

unsigned rhea_channel_resource_map_element_count(struct
						 rhea_channel_resource_map
						 *map)
{
	if (NULL == map)
		return 0;

	return map->block_count;
}

struct rhea_channel_resource_map
	*_rhea_channel_resource_map_element_next(struct
						 rhea_channel_resource_map
						 *map,
						 const unsigned int
						 current_base)
{
	struct rhea_channel_resource_map *map_it = NULL;

	if (NULL == map)
		return NULL;

	list_for_each_entry(map_it, &map->list, list) {
		if (current_base != map_it->base)
			return map_it;
	}

	return NULL;
}

struct rhea_channel_resource_map
	*_rhea_channel_resource_map_element_first(struct
						  rhea_channel_resource_map
						  *map)
{
	struct rhea_channel_resource_map *map_it = NULL;

	if (NULL == map)
		return NULL;

	list_for_each_entry(map_it, &map->list, list) {
		return map_it;
	}

	return NULL;
}

struct rhea_channel_resource_block *_rhea_channel_resource_list_next(
	struct rhea_channel_resource_block *block_current)
{
	struct rhea_channel_resource_block *block_it = NULL;
	struct rhea_channel_resource_block *block_return = NULL;

	if (NULL == block_current)
		return NULL;

	list_for_each_entry(block_it, &block_current->list, list) {
		if (block_current->base + block_current->alloced ==
		    block_it->base) {
			block_return = block_it;
		}
	}

	if (NULL == block_return)
		return NULL;

	if (block_return == block_current) {
		rhea_error("Found same object");
		BUG_ON(1);
	}

	return block_return;
}

struct rhea_channel_resource_block *_rhea_channel_resource_list_prev(
	struct rhea_channel_resource_block *block_current)
{
	struct rhea_channel_resource_block *block_it = NULL;
	struct rhea_channel_resource_block *block_return = NULL;

	if (NULL == block_current)
		return NULL;

	list_for_each_entry_reverse(block_it, &block_current->list, list) {
		if (block_it->base + block_it->alloced == block_current->base) {
			block_return = block_it;
			break;
		}
	}

	if (NULL == block_return)
		return NULL;

	if (block_return == block_current) {
		rhea_error("Found same object");
		BUG_ON(1);
	}

	return block_return;
}

struct rhea_channel_resource_block *_rhea_channel_resource_list_first(
	struct rhea_channel_resource *res)
{
	struct rhea_channel_resource_block *block_it = NULL;
	struct rhea_channel_resource_block *block_return = NULL;

	if (NULL == res)
		return NULL;

	list_for_each_entry(block_it, &res->block.list, list) {
		if (0 == block_it->base) {
			block_return = block_it;
			break;
		}
	}

	if (NULL == block_return)
		return NULL;

	return block_return;
}

struct rhea_channel_resource_block *_rhea_channel_resource_list_last(
	struct rhea_channel_resource *res,
	unsigned alloced_max)
{
	struct rhea_channel_resource_block *block_it = NULL;
	struct rhea_channel_resource_block *block_return = NULL;

	if (NULL == res)
		return NULL;

	list_for_each_entry_reverse(block_it, &res->block.list, list) {
		if (alloced_max == block_it->base + block_it->alloced) {
			block_return = block_it;
			break;
		}
	}

	if (NULL == block_return)
		return NULL;

	return block_return;
}

static void _rhea_channel_resource_list_delete(struct rhea_channel_resource
					       *res,
					       struct
					       rhea_channel_resource_block
					       *block_free)
{
	if (NULL == res || NULL == block_free)
		return;

	if (0 == res->block_count) {
		rhea_error
			("Could not delete component! List is already empty!");
		BUG_ON(1);
	}

	res->block_count -= 1;

	/* delete block from out list */
	list_del(&block_free->list);

	/* delete block after allocation */
	rhea_align_free(block_free, sizeof(*block_free));
}

static int _rhea_channel_resource_list_free(struct rhea_channel_resource *res,
					    struct rhea_channel_resource_map
					    *map_res,
					    struct rhea_channel_resource_map
					    *map_current)
{
	int rc = 0;
	struct rhea_channel_resource_block *block_return_next = NULL;
	struct rhea_channel_resource_block *block_return_prev = NULL;
	struct rhea_channel_resource_block *block_current = NULL;
	struct rhea_channel_resource_element *element = NULL;

	if (NULL == map_current || NULL == res || NULL == map_res)
		return -EINVAL;

	/* get current pointer */
	element = map_current->element;

	if (NULL == element) {
		rhea_error("No valid element!");
		return -EINVAL;
	}

	block_current = element->curr_block;

	if (NULL == block_current) {
		rhea_error("NO valid block");
		return -EINVAL;
	}

	/* merge block with its neighbours */
	while (NULL != block_current) {
		/* mark block as free */
		block_current->used = 0;

		block_return_next =
			_rhea_channel_resource_list_next(block_current);
		if (block_return_next) {
			if (0 == block_return_next->used) {
				/* adjust base + resource */
				block_return_next->base -=
					block_current->alloced;
				block_return_next->alloced +=
					block_current->alloced;

				/* delete block from list */
				_rhea_channel_resource_list_delete(
					res, block_current);

				/* save new block */
				block_current = block_return_next;
			}
		}

		block_return_prev =
			_rhea_channel_resource_list_prev(block_current);
		if (block_return_prev) {
			if (0 == block_return_prev->used) {
				/* adjust base + resource */
				block_return_prev->alloced +=
					block_current->alloced;

				/* delete block from list */
				_rhea_channel_resource_list_delete(
					res, block_current);

				/* save new current block */
				block_current = block_return_prev;
			}
		}

		rhea_debug("New free block: %u from base: %u and slots: %u",
			   block_current->used, block_current->base,
			   block_current->alloced);

		element = element->next_element;
		if (NULL == element)
			break;

		block_current = element->curr_block;
	}

	res->alloced -= map_current->alloced;

	map_res->block_count -= 1;
	map_res->alloced -= map_current->alloced;

	/* delete block from out list */
	list_del(&map_current->list);

	/* delete map element */
	rhea_align_free(map_current->element, sizeof(*map_current->element));

	/* delete map */
	rhea_align_free(map_current, sizeof(*map_current));

	return rc;
}

int _rhea_channel_resource_init(struct rhea_channel_resource *res,
				unsigned max_size,
				unsigned contiguous,
				unsigned wrap,
				unsigned add_first_element)
{
	struct rhea_channel_resource_block *block_new;

	if (NULL == res)
		return -EINVAL;

	res->alloced_max = max_size;

	INIT_LIST_HEAD(&(res->block.list));

	res->block.base = 0xFFFFFFFFU;
	res->block.alloced = 0;

	res->contiguous = contiguous;
	res->wrap = wrap;

	if (add_first_element) {
		res->block_count += 1;

		block_new = rhea_align_alloc(sizeof(*block_new), 8, GFP_KERNEL);
		if (NULL == block_new) {
			rhea_error
				("Was not able to allocate memory for array");
			return -ENOMEM;
		}

		/* create first block */
		block_new->used = 0;
		block_new->base = 0;
		block_new->alloced = max_size;

		/* add new element to list */
		list_add(&(block_new->list), &(res->block.list));
	}

	return 0;
}

int _rhea_channel_resource_fini(struct rhea_channel_resource *res)
{
	struct rhea_channel_resource_block *temp = NULL;
	struct rhea_channel_resource_block *block_current = NULL;

	if (NULL == res)
		return -EINVAL;

	if (list_empty(&res->block.list)) {
		rhea_error("List is empty");
		return -EINVAL;
	}

	list_for_each_entry_safe(block_current, temp, &res->block.list, list) {
		if (0xFFFFFFFFU != block_current->base) {
			/* delete block from list */
			_rhea_channel_resource_list_delete(res, block_current);
			block_current = NULL;
		}
	}

	return 0;
}

static int _rhea_channel_element_add(struct rhea_channel_resource_block
				     *block_suitable,
				     struct rhea_channel_resource *res,
				     struct rhea_channel_resource_map *map,
				     unsigned *requested_slots)
{
	int rc = 0;
	struct rhea_channel_resource_block *block_new = NULL;
	struct rhea_channel_resource_element *element_prev = NULL;
	struct rhea_channel_resource_element *element_new = NULL;

	if (NULL == block_suitable ||
	    NULL == res || NULL == map || NULL == requested_slots) {
		return -EINVAL;
	}

	/* partition the block into two if block is too big!! */
	if (block_suitable->alloced > *requested_slots) {
		block_new = rhea_align_alloc(sizeof(*block_new), 8, GFP_KERNEL);
		if (NULL == block_new) {
			rhea_error("Was not able to allocate memory for "
				   "array block");
			return -EINVAL;
		}

		/* create new block */
		block_new->used = 1;

		/* assign everything to new block */
		block_new->base = block_suitable->base;
		block_new->alloced = *requested_slots;

		/* adjust old block */
		block_suitable->base += *requested_slots;
		block_suitable->alloced -= *requested_slots;

		/* we add another block */
		res->block_count += 1;

		/* add new element before the chosen element */
		list_add(&(block_new->list), &(res->block.list));
	} else {
		/* mark whole block as being used */
		block_suitable->used = 1;

		/* save block pointer */
		block_new = block_suitable;
	}

	/* figure out how many more slots have to be looked at! */
	*requested_slots -= block_new->alloced;

	/* track how much memory has been found */
	map->alloced += block_new->alloced;
	map->block_count += 1;

	res->alloced += block_new->alloced;

	/* get new element, which can be added to the list */
	element_new = rhea_align_alloc(sizeof(*element_new), 8, GFP_KERNEL);
	if (NULL == element_new) {
		rhea_error("Was not able to allocate memory for new element");
		return -ENOMEM;
	}

	/* add to list */
	element_new->curr_block = block_new;

	/* find last known element */
	element_prev = map->element;
	while (element_prev && element_prev->next_element)
		element_prev = element_prev->next_element;

	if (element_prev) {
		/* save next element */
		element_prev->next_element = element_new;
	} else {
		/* save first element */
		map->element = element_new;
		map->base = element_new->curr_block->base;
	}

	return rc;
}

int _rhea_channel_resource_alloc(struct rhea_channel_resource *res,
				 struct rhea_channel_resource_map *map_res,
				 unsigned requested_slots,
				 unsigned *slot_base)
{
	int rc = 0;
	unsigned request_slot_count = requested_slots;

	struct rhea_channel_resource_map *map_new;
	struct rhea_channel_resource_block *block_suitable = NULL;

	if (NULL == res || NULL == map_res || NULL == slot_base)
		return -EINVAL;

	if (0 == requested_slots) {
		rhea_error("No requested slot count specified");
		return -EINVAL;
	}

	if (res->alloced_max < requested_slots) {
		rhea_error("Number of requested slots is too high");
		return -EINVAL;
	}

	if (res->alloced_max <= res->alloced) {
		rhea_error("All slots have been taken");
		return -EINVAL;
	}

	if (requested_slots > res->alloced_max - res->alloced) {
		rhea_error("Not enough slots available!");
		return -EINVAL;
	}

	if (list_empty(&res->block.list)) {
		rhea_error("List is empty");
		return -EINVAL;
	}

	map_new = rhea_align_alloc(sizeof(*map_new), 8, GFP_KERNEL);
	if (NULL == map_new) {
		rhea_error("Was not able to allocate memory for new element");
		return -ENOMEM;
	}

	/* set the right amount of memory we want to get */
	block_suitable = NULL;

	if (res->contiguous) {
		/* find the smallest block, which fits the requirements */
		_rhea_channel_resource_min(res, requested_slots,
					   &block_suitable);
	} else {
		/* first try to get full block */
		_rhea_channel_resource_min(res, requested_slots,
					   &block_suitable);
		if (NULL == block_suitable) {
			/* ok, now try smallest available block */
			_rhea_channel_resource_min(res, 1, &block_suitable);
		}
	}

	/* a suitable block was found and not all the memory has been found */
	while (block_suitable && request_slot_count) {
		rc = _rhea_channel_element_add(block_suitable, res, map_new,
					       &request_slot_count);
		if (rc) {
			rhea_error("Error when adding element to the "
				   "resource map");
			return rc;
		}

		/* find the smallest block, which fits the requirements */
		_rhea_channel_resource_min(res, 1, &block_suitable);
	}

	/* if allowed to try to combine last and first element! */
	if (map_new->alloced != requested_slots && res->wrap) {
		struct rhea_channel_resource_block *block_last = NULL;
		struct rhea_channel_resource_block *block_first = NULL;

		block_first = _rhea_channel_resource_list_first(res);

		block_last = _rhea_channel_resource_list_last(
			res, res->alloced_max);

		/* check if both of them are free */
		if (block_first != block_last &&
		    0 == block_first->used &&
		    0 == block_last->used &&
		    block_last->alloced + block_first->alloced >=
		    request_slot_count) {
			rc = _rhea_channel_element_add(block_last, res,
						       map_new,
						       &request_slot_count);
			if (rc) {
				rhea_error("Error when adding element to "
					   "the resource map");
			}

			rc = _rhea_channel_element_add(block_first, res,
						       map_new,
						       &request_slot_count);
			if (rc) {
				rhea_error("Error when adding element to "
					   "the resource map");
			}
		}
	}

	/* check if all the memory was found */
	if (map_new->alloced != requested_slots) {
		rhea_error("Did not manage to find all the memory!");
		return -EINVAL;
	}

	/* only add first element to the map */
	list_add(&(map_new->list), &(map_res->list));

	map_res->block_count += 1;
	map_res->alloced += requested_slots;

	/* return base back to caller */
	*slot_base = map_new->base;

	/* Find out how many bits are used by the alloc */
	map_new->bits = hea_ld(map_new->alloced);
	if (0 > map_new->bits) {
		rhea_error("Was not able to get number of bits");
		return -EPERM;
	}

	return rc;
}


int _rhea_channel_resource_free(struct rhea_channel_resource *res,
				struct rhea_channel_resource_map *map_res,
				unsigned base)
{
	int rc = 0;
	struct rhea_channel_resource_map *map_current = NULL;

	if (NULL == res || NULL == map_res)
		return -EINVAL;

	map_current = _rhea_channel_resource_map_get(map_res, base);
	if (NULL == map_current) {
		rhea_error("Was not able to find map");
		return -EINVAL;
	}

	rc = _rhea_channel_resource_list_free(res, map_res, map_current);
	if (rc) {
		rhea_error("Was not able to free map");
		return rc;
	}

	return rc;
}

int _rhea_channel_resource_max(struct rhea_channel_resource *res,
			       unsigned requested_slots,
			       struct rhea_channel_resource_block **block_max)
{
	int rc = 0;
	/* set to lowest possible value */
	int max = 0;
	struct rhea_channel_resource_block *block_current = NULL;

	if (NULL == res)
		return -EINVAL;

	/* find largest free block */
	list_for_each_entry(block_current, &res->block.list, list) {
		if (0 == block_current->used &&
		    requested_slots <= block_current->alloced &&
		    max < block_current->alloced) {
			max = block_current->alloced;

			*block_max = block_current;
		}
	}

	return rc;
}

int _rhea_channel_resource_min(struct rhea_channel_resource *res,
			       unsigned requested_slots,
			       struct rhea_channel_resource_block **block_min)
{
	int rc = 0;
	int min;
	struct rhea_channel_resource_block *block_current = NULL;

	if (NULL == res)
		return -EINVAL;

	/* set minimum to maximum + 1 value */
	min = res->alloced_max + 1;

	/* find largest free block */
	list_for_each_entry(block_current, &res->block.list, list) {
		if (0 == block_current->used &&
		    requested_slots <= block_current->alloced &&
		    min > block_current->alloced) {
			min = block_current->alloced;

			*block_min = block_current;
		}
	}

	return rc;
}

int _rhea_channel_resource_index_get(struct rhea_channel_resource_map *map_res,
				     unsigned base,
				     unsigned offset,
				     unsigned *slot_index)
{
	int rc = 0;
	int index = offset;
	struct rhea_channel_resource_element *element = NULL;
	struct rhea_channel_resource_map *map_current = NULL;

	if (NULL == map_res || NULL == slot_index)
		return -EINVAL;

	if (offset > map_res->alloced) {
		rhea_error("Offset is too high");
		return -EINVAL;
	}

	map_current = _rhea_channel_resource_map_get(map_res, base);
	if (NULL == map_current) {
		rhea_error("Was not able to find map");
		return -EINVAL;
	}

	if (0 == map_current->alloced) {
		rhea_error("Nothing allocated for that map");
		return -EINVAL;
	}

	if (offset > map_current->alloced) {
		rhea_error("Offset too big");
		return -EINVAL;
	}

	/* get first element */
	element = map_current->element;

	if (NULL == element || NULL == element->curr_block) {
		rhea_error("Did not find valid element");
		return -EINVAL;
	}

	while (element->curr_block->alloced < index) {
		/* correct offset */
		index -= element->curr_block->alloced;

		/* get next element */
		element = element->next_element;

		if (NULL == element)
			break;
	}

	/* compute index */
	*slot_index = element->curr_block->base + index;

	return rc;
}

int _rhea_channel_map_share(struct rhea_channel_resource_map **target_map,
			    const struct rhea_channel_resource_map *source_map)
{
	int rc = 0;

	if (NULL == source_map || NULL == target_map || NULL == *target_map)
		return -EINVAL;

	*target_map = (struct rhea_channel_resource_map *)source_map;

	/* mark the next instance */
	(*target_map)->instance_count++;

	return rc;
}

int _rhea_channel_map_init(struct rhea_channel_resource_map **map)
{
	int rc = 0;
	struct rhea_channel_resource_map *ptr;

	if (NULL == map || NULL != *map)
		return -EINVAL;

	ptr = rhea_align_alloc(sizeof(*ptr), 8, GFP_KERNEL);
	if (NULL == ptr) {
		rhea_error("Was not able to allocate resource map");
		return -ENOMEM;
	}

	memset(ptr, 0, sizeof(*ptr));
	INIT_LIST_HEAD(&(ptr->list));

	ptr->init = 1;
	ptr->instance_count = 1;

	ptr->base = 0xFFFFFFFFU;

	/* pass pointer back to caller */
	*map = ptr;

	return rc;
}

int _rhea_channel_map_fini(struct rhea_channel_resource *res,
			   struct rhea_channel_resource_map **map_res)
{
	int rc = 0;
	struct rhea_channel_resource_map *temp = NULL;
	struct rhea_channel_resource_map *map_current = NULL;

	if (NULL == res)
		return -EINVAL;

	if (NULL == *map_res)
		return -EINVAL;

	if (1 >= (*map_res)->instance_count) {
		/* find correct map */
		list_for_each_entry_safe(map_current, temp, &(*map_res)->list,
					 list) {
			if (0xFFFFFFFFU != map_current->base) {
				rc = _rhea_channel_resource_list_free(
					res, *map_res, map_current);
				if (rc) {
					rhea_error("Was not able to free map");
					return rc;
				}
			}
		}

		rhea_align_free(*map_res, sizeof(**map_res));
	} else {
		--(*map_res)->instance_count;
	}

	/* make sure that this map is not going to be used anymore */
	*map_res = NULL;

	return rc;
}
