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

#ifndef __POWEREN_EP_VF_H__
#define __POWEREN_EP_VF_H__

#include <linux/types.h>

struct poweren_ep_vf {
	int	vf_num;
	void	*priv;
};

struct vf_driver {
	int vf_num;
	char *name;
	int  (*probe)		(struct poweren_ep_vf *);
	void (*remove)		(struct poweren_ep_vf *);
};

/** Exported functions **/
extern int poweren_ep_register_vf_driver(struct vf_driver *vf);
extern void poweren_ep_unregister_vf_driver(struct vf_driver *vf);

/** Not exported functions **/
void poweren_ep_empty_vf_manager_list(void);

#endif /* __POWEREN_EP_VF_H__ */
