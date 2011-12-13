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

#include <linux/io.h>
#include <linux/slab.h>

#include "poweren_ep_vf.h"
#include "poweren_ep_driver.h"

struct poweren_ep_vf_entry {
	struct list_head list;
	struct poweren_ep_vf *vf;
	struct vf_driver *vf_drv;
};

static LIST_HEAD(vf_driver_list);

static int poweren_ep_is_vf_name_used(const char *name)
{
	struct poweren_ep_vf_entry *vfe;

	if (list_empty(&vf_driver_list)) {
		poweren_ep_debug("list is empty");
		return 0;
	}

	list_for_each_entry(vfe, &vf_driver_list, list) {
		if (!strcmp(vfe->vf_drv->name, name)) {
			poweren_ep_debug("found a vf dd having the name "
					"%s\"\n", name);
			return 1;
		}
	}

	return 0;
}

static unsigned poweren_ep_find_invalid_entries(void)
{
	unsigned i = 0;
	struct poweren_ep_vf_entry *tfe;
	struct poweren_ep_vf_entry *vfe;

	list_for_each_entry_safe(vfe, tfe, &vf_driver_list, list) {
		if (!vfe->vf_drv->probe ||
				!vfe->vf_drv->remove) {
			poweren_ep_debug("found an invalid entry\n");
			list_del(&vfe->list);
			kfree(vfe->vf);
			kfree(vfe);
			++i;
		}
	}

	return i;
}

void poweren_ep_empty_vf_manager_list(void)
{
	struct poweren_ep_vf_entry *vfe;

	poweren_ep_debug("emptying the list\n");

	if (list_empty(&vf_driver_list)) {
		poweren_ep_debug("the list is empty\n");
		return;
	}

	poweren_ep_debug("iterating through the list\n");
	list_for_each_entry(vfe, &vf_driver_list, list) {
		poweren_ep_debug("removing driver: %s\n", vfe->vf_drv->name);
		list_del(&vfe->list);
		kfree(vfe);
	}
}

int poweren_ep_register_vf_driver(struct vf_driver *vf_drv)
{
	int err;
	struct poweren_ep_vf *vf;
	struct poweren_ep_vf_entry *vfe;

	if (!vf_drv->probe || !vf_drv->remove) {
		poweren_ep_error("probe and remove were not initialized\n");
		return -ENOENT;
	}

	if (vf_drv->vf_num >= TOTAL_FUNCS) {
		poweren_ep_error("fail inserting vf driver on function %d\n",
				vf_drv->vf_num);
		poweren_ep_error("the maximum function num is %d\n",
				TOTAL_FUNCS);
		return -ENOENT;
	}

	if (!vf_drv->name) {
		poweren_ep_error("vf name attribute not allocated\n");
		return -ENOMEM;
	}

	if (!strcmp(vf_drv->name, "")) {
		poweren_ep_error("vf name attribute not initialized\n");
		return -ENOENT;
	}

	if (poweren_ep_find_invalid_entries()) {
		poweren_ep_error("unregister_vf_driver was not called\n");
		return -EINVAL;
	}

	poweren_ep_debug("checking if vf key is existing\n");
	if (poweren_ep_is_vf_name_used(vf_drv->name)) {
		poweren_ep_error("name \"%s\" is already used in the list\n",
				vf_drv->name);
		return -ENOENT;
	}

	poweren_ep_debug("allocating vf driver entry\n");
	vf = kzalloc(sizeof(*vf), GFP_KERNEL);
	if (!vf) {
		poweren_ep_error("failed allocating vf\n");
		return -ENOMEM;
	}
	vfe = kzalloc(sizeof(*vfe), GFP_KERNEL);
	if (!vfe) {
		poweren_ep_error("failed allocating vf entry\n");
		kfree(vf);
		return -ENOMEM;
	}

	poweren_ep_debug("initializing vf entry\n");

	vf->vf_num = vf_drv->vf_num;

	vfe->vf = vf;
	vfe->vf_drv = vf_drv;

	poweren_ep_debug("adding driver \"%s\" to the list. pf: %d\n",
			vf_drv->name, vf->vf_num);
	list_add(&vfe->list, &vf_driver_list);

	poweren_ep_debug("getting hw info to be given to the driver \"%s\"\n",
			vf_drv->name);

	err = vf_drv->probe(vf);

	if (err) {
		poweren_ep_unregister_vf_driver(vf_drv);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(poweren_ep_register_vf_driver);

void poweren_ep_unregister_vf_driver(struct vf_driver *vf_drv)
{
	struct poweren_ep_vf_entry *tfe;
	struct poweren_ep_vf_entry *vfe;

	list_for_each_entry_safe(vfe, tfe, &vf_driver_list, list) {
		if (!strcmp(vfe->vf_drv->name, vf_drv->name)) {
			poweren_ep_debug("removing driver: %s\n", vf_drv->name);
			vfe->vf_drv->remove(vfe->vf);
			list_del(&vfe->list);
			kfree(vfe->vf);
			kfree(vfe);
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(poweren_ep_unregister_vf_driver);
