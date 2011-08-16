#ifndef _ASM_POWERPC_COPRO_DRIVER_H
#define _ASM_POWERPC_COPRO_DRIVER_H

/*
 * Copyright 2009 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/ioctl.h>
#include <linux/types.h>

struct copro_map_entry {
	__u64 addr;
	__u64 len;
};

#define COPRO_MAP_MAX_COUNT	7

/* Mapping flags */
#define COPRO_MAP_BOLT	1UL
#define COPRO_MAP_ALLOWED_FLAGS	COPRO_MAP_BOLT

struct copro_map_args {
	__u64 count;
	__u64 flags;
	struct copro_map_entry entries[COPRO_MAP_MAX_COUNT];
};

#define COPRO_INSTANCE_INVALID	0
#define COPRO_INSTANCE_LIST_SIZE	(4096 / sizeof(__u64))

struct copro_instance_list {
	__u64 instances[COPRO_INSTANCE_LIST_SIZE];
};

#define COPRO_COMPAT_BUF_SIZE	(4096 - sizeof(__u32))

struct copro_compat_info {
	__u32 len;
	char buf[COPRO_COMPAT_BUF_SIZE];
};

#define COPRO_MTRACE_MODE_SW		0x0
#define COPRO_MTRACE_MODE_HW		0x1
#define COPRO_MTRACE_MODE_HW_FORCE	0x2
#define COPRO_MTRACE_MODE_MIXED		0x3
#define COPRO_MTRACE_MASK		0x3UL

struct copro_affinity_args {
	__u64 instance;
	__u64 addr;
	__u32 len;
};

/* ioctls available on a copro fd - opened via /dev/xxx */
#define COPRO_IOCTL_GET_API_VERSION	_IO('c', 0)
#define COPRO_IOCTL_MAP			_IOW('c', 1, struct copro_map_args)
#define COPRO_IOCTL_UNMAP		_IOW('c', 6, struct copro_map_args)
#define COPRO_IOCTL_GET_TYPE		_IOR('c', 2, __u64)
#define COPRO_IOCTL_GET_INSTANCES	_IOR('c', 3, struct copro_instance_list)
#define COPRO_IOCTL_GET_PBIC		_IOR('c', 8, __u64)
#define COPRO_IOCTL_BIND		_IOW('c', 4, __u64)
#define COPRO_IOCTL_UNBIND		_IO('c', 5)
#define COPRO_IOCTL_GET_COMPATIBLE	_IOR('c', 13, struct copro_compat_info)
#define COPRO_IOCTL_ALLOC_IMQ		_IOW('c', 9, __u64)
#define COPRO_IOCTL_ENABLE_IMQ		_IO('c', 10)
#define COPRO_IOCTL_DISABLE_IMQ		_IO('c', 11)
#define COPRO_IOCTL_FREE_IMQ		_IO('c', 12)
#define COPRO_IOCTL_ENABLE_MTRACE	_IOW('c', 14, __u64)
#define COPRO_IOCTL_DISABLE_MTRACE	_IO('c', 15)
#define COPRO_IOCTL_GET_AFFINITY	_IOW('c', 16, \
					      struct copro_affinity_args)
#define COPRO_IOCTL_OPEN_UNIT		_IO('c', 7)

/* For WORKAROUND_PBIC_DUP_ENTRIES, remove before upstreaming */
#define COPRO_IOCTL_INVALIDATE		_IOW('c', 99, __u64)


struct copro_reg_args {
	__u64 regnr;
	__u64 value;
};

/* ioctls available on a copro unit fd - opened via OPEN_UNIT ioctl */
#define COPRO_UNIT_IOCTL_GET_ID		_IOR('c', 128, __u64)

/* get/set a copro unit register */
#define COPRO_UNIT_IOCTL_SET_REG	_IOW('c', 129, struct copro_reg_args)
#define COPRO_UNIT_IOCTL_GET_REG	_IOWR('c', 130, struct copro_reg_args)

#define COPRO_UNIT_IOCTL_ABORT_CRB	_IOW('c', 131, __u64)

/* Common registers, other numbers are unit-type specific */
#define COPRO_UNIT_REG_MTRACE		0x10000

/* Raw access, read/write any register, low bits specifiy the offset */
#define COPRO_UNIT_REG_RAW		(1ull << 63)

/* Extension ioctl range, used by unit-type specific drivers */
#define COPRO_UNIT_EXTN_NR_START	160
#define COPRO_UNIT_EXTN_NR_END		191


#define COPRO_API_VERSION	((1 << 16U) | 14U)
#define COPRO_API_MAJOR(x)	(x >> 16)
#define COPRO_API_MINOR(x)	(x & 0xFFFF)

static inline int copro_api_is_compatible(unsigned version)
{
	/* Major version signifies incompatible API change, so must match */
	if (COPRO_API_MAJOR(COPRO_API_VERSION) != COPRO_API_MAJOR(version))
		return 0;

	/* The compiled version can't be greater than the kernel version */
	if (COPRO_API_MINOR(COPRO_API_VERSION) > COPRO_API_MINOR(version))
		return 0;

	return 1;
}

/*
 * If a CSB or CCB write causes an error the kernel is notified and expected
 * to do the operation on behalf of the coprocessor. In these cases the
 * kernel will set a bit in the CSB's CE field to indicate that an error has
 * occured and userspace should try and rectifiy it before proceeding.
 */
#define COPRO_CSB_CE_CSB_ERROR	0x08
#define COPRO_CSB_CE_CCB_ERROR	0x10

/* Interrupt Message Queue entry and accessors */
struct copro_imq_entry {
	__u8 flags;	/* valid:1, format:1, trigger:4, state:2 */
	__u8 src_info;	/* overflow:1, vf:3, reserved:4 */
	__u16 _reserved1;
	__u32 mm_info;	/* reserved:7, pr:1, as:1, gs:1, pid:14, lpid:8 */
	__u64 csb_c;
	__u8 csb[16];
	__u64 ccb[2];
	__u8 _reserved2[16];
};

static inline __u8 copro_imqe_valid(struct copro_imq_entry *imqe)
{
	return (imqe->flags >> 7) & 1;
}

static inline __u8 copro_imqe_format(struct copro_imq_entry *imqe)
{
	return (imqe->flags >> 6) & 1;
}

static inline __u8 copro_imqe_trigger(struct copro_imq_entry *imqe)
{
	return (imqe->flags >> 2) & 0xF;
}

static inline __u8 copro_imqe_state(struct copro_imq_entry *imqe)
{
	return imqe->flags & 3;
}

static inline __u8 copro_imqe_overflow(struct copro_imq_entry *imqe)
{
	return (imqe->src_info >> 7) & 1;
}

static inline __u8 copro_imqe_src_vf(struct copro_imq_entry *imqe)
{
	return (imqe->src_info >> 4) & 7;
}

static inline __u8 copro_imqe_pr(struct copro_imq_entry *imqe)
{
	return (imqe->mm_info >> 24) & 1;	/* DD2 only */
}

static inline __u8 copro_imqe_as(struct copro_imq_entry *imqe)
{
	return (imqe->mm_info >> 23) & 1;
}

static inline __u8 copro_imqe_gs(struct copro_imq_entry *imqe)
{
	return (imqe->mm_info >> 22) & 1;
}

static inline __u16 copro_imqe_pid(struct copro_imq_entry *imqe)
{
	return (imqe->mm_info >> 8) & 0x3FFF;
}

static inline __u8 copro_imqe_lpid(struct copro_imq_entry *imqe)
{
	return imqe->mm_info & 0xFF;
}

static inline __u64 copro_imqe_csb_ptr(struct copro_imq_entry *imqe)
{
	return imqe->csb_c & 0xFFFFFFFFFFFFFFF0ULL;
}

static inline void *copro_imqe_csb(struct copro_imq_entry *imqe)
{
	return (void *)&imqe->csb;
}

static inline void *copro_imqe_ccb(struct copro_imq_entry *imqe)
{
	return (void *)&imqe->ccb;
}

static inline __u8 copro_imqe_csb_c(struct copro_imq_entry *imqe)
{
	return imqe->csb_c & 0xFUL;
}

#endif /* _ASM_POWERPC_COPRO_DRIVER_H */
