#ifndef __POWEREN_EP_SM_H__
#define __POWEREN_EP_SM_H__

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

#include "poweren_ep_vf.h"
#include "poweren_ep_driver.h"

/* MMIO defines */
#define MMIO_START		0x2000000000ull
#define MMIO_SIZE		0x100000ul
#define MMIO_MAX_SIZE		0x10000000ul

/* number of slots per pf/vf */
#define MAX_SLOTS				16

/* slotmgr: need to use a common page size to calculate mmio offsets */
#define POWEREN_PAGE_SIZE			65536

/* reserve a slot for Veth */
#define RESERVED_SLOTS				1
#define VETH_SLOT				(MAX_SLOTS - 1)
#define RES_SLOT_VETH				0xFFFE

/* values for the ctrl hir */
#define NO_SLOT_REQ				0
#define SLOT_REQ				1
#define SLOT_REQ_ACK				2
#define SLOT_ALLOC				3
#define SLOT_ALLOC_ACK				4
#define SLOT_REQ_TIMEOUT			5

/* status information for get_slot - ioctl return values */
#define HOST_SLOT_REQ				600
#define HOST_NO_SLOT_REQ			601
#define HOST_NO_FREE_SLOTS			602
#define HOST_SLOT_REQ_TIMEOUT			603
#define DEVICE_SLOT_REQ				700
#define DEVICE_NO_SLOT_REQ			701
#define DEVICE_NO_FREE_SLOTS			702
#define DEVICE_SLOT_REQ_TIMEOUT			703
#define SLOT_TIMEOUT				800
#define SLOTMGR_ERR				801

#define LOCK_INT(slotmgr)	mutex_lock_interruptible(&slotmgr->mutex);
#define LOCK(slotmgr)		mutex_lock(&slotmgr->mutex);
#define UNLOCK(slotmgr)		mutex_unlock(&slotmgr->mutex);

/* allow the vma to release a slot after mmap/munmap */
struct poweren_ep_slotmgr_vma_priv {
	u32			mmio_slot;
	struct poweren_ep_vf	*vf;
};

/* local struct representing a slot to be managed by the slotmgr */
struct poweren_ep_slotmgr_slot {
	u32		protocol;	 /* protocol for pairing host/device */
	u32		hpid;		 /* host app pid_t */
	u32		dpid;		 /* device app pid_t */
	unsigned long	offset;		 /* offset to start of slot in SMA */
	unsigned long	size;		 /* slot size */
	u32		local_mmio_req;	 /* flag to track slot mapping in mmap*/
	u32		remote_mmio_req; /* flag to track slot mapping in mmap*/
};

struct poweren_ep_slotmgr {
	unsigned long			flags;			/* irq flags */
	struct mutex			mutex;
	int				slot_id;	/*last allocated slot*/
	struct poweren_ep_slotmgr_slot	slots[MAX_SLOTS];
	struct poweren_ep_mmio_mem	*local_mmio;
	struct poweren_ep_mmio_mem	*remote_mmio;

	struct poweren_ep		*ep_dev;
	int				vf_num;
	int				mtrr_reg;
};

/** Exported functions **/
extern long poweren_ep_ioctl_slotmgr(struct poweren_ep_vf *vf,
		struct file *f, unsigned int cmd, unsigned long arg);
extern int poweren_ep_slotmgr_mmap(struct poweren_ep_vf *vf,
		struct file *f, struct vm_area_struct *vma);
extern void *poweren_ep_get_slot_local_sma(struct poweren_ep_vf *vf, u32 slot);
extern void *poweren_ep_get_slot_remote_sma(struct poweren_ep_vf *vf, u32 slot);
extern int poweren_ep_find_slot_from_addr(struct poweren_ep_vf *vf, u64 addr);
extern int poweren_ep_slotmgr_connect(struct poweren_ep_vf *vf,
		u32 protocol, u32 flags, unsigned long *slot_size);
extern void poweren_ep_slotmgr_connect_cleanup(struct poweren_ep_vf *vf);
extern int poweren_ep_slotmgr_find_slot(struct poweren_ep_vf *vf,
		u32 protocol, unsigned long *slot_size);
extern void poweren_ep_slotmgr_term(struct poweren_ep_vf *vf,
		u32 slot_id);

/** Not exported functions **/
extern void poweren_ep_set_slotmgrs(
	struct poweren_ep_slotmgr *slotmgrs[TOTAL_FUNCS]);
extern int poweren_ep_slotmgr_init(
	struct poweren_ep ep_dev[TOTAL_FUNCS]);
extern void poweren_ep_slotmgr_release_local_slot(
	struct poweren_ep_slotmgr *slotmgr, u32 slot_id);
extern void poweren_ep_slotmgr_exit(void);
extern int poweren_ep_local_mmio_setup(void);
extern int poweren_ep_remote_mmio_setup(void);
extern void poweren_ep_local_mmio_free(void);
extern void poweren_ep_remote_mmio_free(void);

#endif /* __POWEREN_EP_SM_H__ */
