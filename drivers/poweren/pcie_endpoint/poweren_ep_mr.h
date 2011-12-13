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

#ifndef __POWEREN_EP_MR_H__
#define __POWEREN_EP_MR_H__

#include <linux/poweren/pcie_endpoint/poweren_ep.h>

#include "poweren_ep_vf.h"
#include "poweren_ep_bp.h"
#include "poweren_ep_driver.h"

#define MR_ICSWX_RETRIES	10
#define MR_PAGE_SIZE		4096ul
#define MR_PAGE_ROUND_UP(x)	((((unsigned long)(x)) + MR_PAGE_SIZE - 1) & \
				(~(MR_PAGE_SIZE - 1)))
#define MR_MEM_SLOTS		256
#define MR_SIZE			(sizeof(struct ep_reg_mem) * MR_MEM_SLOTS)
#define	MR_ROUNDED_SIZE		MR_PAGE_ROUND_UP(MR_SIZE)
#define MR_START_ADDR		0x140000000ul /* 5 GB */
#define MR_LOCAL_HANDLE(fn)	(MR_START_ADDR + (2 * fn) * MR_ROUNDED_SIZE)
#define MR_REMOTE_HANDLE(fn)	(MR_START_ADDR + (2 * fn + 1) * MR_ROUNDED_SIZE)

struct poweren_ep_mr {
	unsigned		*local_mem_pid;
	struct ep_reg_mem	*local_mem;
	struct ep_reg_mem	*remote_mem;
	struct ep_reg_mem	*remote_mem_scratch;
	struct poweren_ep_vf	*vf;
};

/** function used within the memory registration component **/
extern int poweren_ep_memreg_sync_local(struct poweren_ep_mr *mr);
extern int poweren_ep_memreg_sync_remote(struct poweren_ep_mr *mr);
extern int __poweren_ep_memreg_setup(struct poweren_ep_mr *mr,
		unsigned long size);
extern int __poweren_ep_memreg_exit(struct poweren_ep_mr *mr,
		unsigned long size);

/** function used in the high performance device driver **/
extern int poweren_ep_ioctl_get_reg_mem(unsigned long uarg);
extern int poweren_ep_ioctl_reg_mem(unsigned long uarg, unsigned int pid);
extern int poweren_ep_ioctl_dereg_mem(unsigned long uarg);
extern void poweren_ep_cleanup_mem_reg_by_pid(unsigned int pid);
extern int poweren_ep_memreg_setup(struct poweren_ep_vf *vf);
extern void poweren_ep_memreg_exit(void);
extern void poweren_ep_inc_gencount(void);

#endif /* __POWEREN_EP_MR_H__ */
