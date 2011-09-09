#ifndef _ASM_POWERPC_PIXAD_XML_H
#define _ASM_POWERPC_PIXAD_XML_H
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


#include <linux/ioctl.h>
#include <linux/types.h>

#define PIXAD_IOC_TYPE	0xf2

#define PIXAD_IOCTL_GET_VF_INFO		_IOR(PIXAD_IOC_TYPE, 1, \
							struct pixad_vf_info)
#define PIXAD_IOCTL_MMAP_IMQ		_IOWR(PIXAD_IOC_TYPE, 2, \
							struct pixad_map_info)
#define PIXAD_IOCTL_MMAP_QPOOL		_IOWR(PIXAD_IOC_TYPE, 3, \
							struct pixad_map_info)
#define PIXAD_IOCTL_MMAP_MMIO		_IO(PIXAD_IOC_TYPE, 4)

#define PIXAD_IOCTL_GET_IMQ_MAX_INDEX	_IOR(PIXAD_IOC_TYPE, 7, __u32)
#define PIXAD_IOCTL_GET_IMQ_VALID_BIT	_IOR(PIXAD_IOC_TYPE, 8, __u32)
#define PIXAD_IOCTL_GET_IMQ_READ_INDEX	_IOR(PIXAD_IOC_TYPE, 9, __u32)
#define PIXAD_IOCTL_SET_IMQ_READ_INDEX	_IOW(PIXAD_IOC_TYPE, 10, __u32)

#define PIXAD_IOCTL_MMAP_TAKEDOWN_MPOOL	_IOWR(PIXAD_IOC_TYPE, 11, \
							struct pixad_map_info)
#define PIXAD_IOCTL_RESET_SESSION_ID	_IOW(PIXAD_IOC_TYPE, 12, __u32)

#define PIXAD_IOCTL_SET_ACTIVE_QPOOL_ID _IOW(PIXAD_IOC_TYPE, 13, __u32)

struct pixad_map_info {
	__u32 size;
	__u64 offset;
};

struct pixad_vf_info {
	__u32 vf_id;
	__u32 type;
};


#endif /* _ASM_POWERPC_PIXAD_XML_H */
