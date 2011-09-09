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
#include <linux/kernel.h>
#include <platforms/wsp/copro/unit_xmlx.h>
#include "proc_utils.h"
#include "pixad.h"

void init_pixad_proc_set(unsigned int copro_num,
		    struct pixad_copro *copro_info)
{
	int i, j;
	struct pixad_xml_vf *vf;

	pixad_debug("corpo_vet pointer = %p\n", copro_info);

	for (i = 0; i < copro_num; i++) {
		for (j = 0; j < ARRAY_SIZE(copro_info->vf_info); j++) {
			vf = &copro_info[i].vf_info[j];

			vf->num_users = 0;
			INIT_LIST_HEAD(&vf->proc_list.list);
			spin_lock_init(&vf->vf_lock);
		}
	}
}

/* Open Lock has to be acquired before calling this func. */
int find_free_VF(unsigned int copro_num, struct pixad_copro *copro_info,
		int *target_unit, int *target_vf)
{
	int i, j;
	*target_unit = -1;
	*target_vf = -1;

	for (i = 0; i < copro_num; i++) {
		pixad_debug("search XML copro %d\n", i);
		for (j = 0; j < ARRAY_SIZE(copro_info[i].vf_info); j++) {

			pixad_debug("VF %d: num_users %d\n", j,
					copro_info[i].vf_info[j].num_users);

			if (copro_info[i].vf_info[j].num_users == 0) {
				copro_info[i].vf_info[j].num_users = 1;
				*target_unit = i;
				*target_vf = j;
				return 0;
			}

		}
	}

	return -EBUSY;
}

/*
 * Retrieve information of an already registered process
 */
struct pixad_user_proc *find_proc(unsigned int copro_num,
		  struct pixad_copro *copro_info, struct file *descriptor)
{
	struct pixad_user_proc *tmp;
	struct list_head *pos;
	struct list_head *list;
	int i, j;

	for (i = 0; i < copro_num; i++) {
		for (j = 0; j < ARRAY_SIZE(copro_info[i].vf_info); j++) {
			list = &copro_info[i].vf_info[j].proc_list.list;
			if (list_empty(list))
				continue;

			list_for_each(pos, list) {
				tmp = list_entry(pos, struct pixad_user_proc,
									list);
				if (tmp->descriptor == descriptor)
					return tmp;
			}
		}
	}

	return NULL;
}
