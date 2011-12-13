#ifndef _LINUX_POWEREN_EP_H
#define _LINUX_POWEREN_EP_H

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

#include <linux/ioctl.h>
#include <linux/types.h>

struct ep_reg_mem {
	__u64 address;
	__u64 size;
	__u32 valid;
	__u32 index;
	__u32 key;
	__u32 padding;
};

struct ep_usr_dma_buf {
	__u64 uaddr;		/* input */
	__u64 size;		/* input */
	__u64 daddr;		/* output/returned value */
};

#define EP_IOCTL_REGISTER_MEM		_IOW('c', 0, struct ep_reg_mem)
#define EP_IOCTL_DEREGISTER_MEM		_IOW('c', 1, struct ep_reg_mem)
#define EP_IOCTL_GET_REG_MEM		_IOWR('c', 2, struct ep_reg_mem)
#define EP_IOCTL_MAP_UDMA_BUF		_IOW('c', 3, struct ep_usr_dma_buf)
#define EP_IOCTL_UNMAP_UDMA_BUF		_IOW('c', 4, __u64)
#define EP_IOCTL_SLOTMGR_CONNECT	_IO('c', 5)
#define EP_IOCTL_SLOTMGR_FIND_SLOT	_IO('c', 6)

#endif /* _LINUX_POWEREN_EP_H */
