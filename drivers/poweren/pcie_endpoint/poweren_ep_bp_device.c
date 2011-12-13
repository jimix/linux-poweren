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

#include <linux/errno.h>
#include <linux/poweren/pcie_endpoint/poweren_ep.h>

#include "poweren_ep_bp.h"

void poweren_ep_unmap_dma_by_pid(struct poweren_ep_vf *vf, unsigned int pid)
{
}

int poweren_ep_dma_search(struct ep_reg_mem *r_arg)
{
	return 0;
}

int poweren_ep_map_udma_buf(struct poweren_ep_vf *vf, unsigned long uarg)
{
	return -EPERM;
}

int poweren_ep_unmap_udma_buf(struct poweren_ep_vf *vf, unsigned long uarg)
{
	return -EPERM;
}

int poweren_ep_dma_init(struct poweren_ep_vf *vf)
{
	return 0;
}

void poweren_ep_dma_fini(struct poweren_ep_vf *vf)
{
}
