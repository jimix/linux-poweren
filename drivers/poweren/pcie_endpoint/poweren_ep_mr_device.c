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

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/iommu.h>

#include <asm/icswx.h>

#include "poweren_ep_mr.h"
#include "poweren_ep_driver.h"

#define DMAX_CT		0x3c
#define DMAX_WRITE	0
#define DMAX_READ	1

struct poweren_ep_dde {
	u16 p;
	u8 count;
	u8 pool_idx;
	u32 byte_count;
	u64 addr;
};

struct poweren_ep_ccb {
	u64 cv;
	u64 ca_cm;
};

struct poweren_ep_crb_dmax {
	u32 ccw;
	u32 flags;
	u64 csbp;
	struct poweren_ep_dde source;
	struct poweren_ep_dde target;
	struct poweren_ep_ccb ccb;
};

struct poweren_ep_csb {
	u32 control;
	u32 pbc;
	u64 addr;
};

/* This mutex lock down access to the list of memory areas */
DEFINE_MUTEX(remote_mem_lock);

static u64 cur_gencount;

void poweren_ep_inc_gencount(void)
{
	u64 gencount;

	gencount = poweren_ep_read_hir(MR_A2_GEN_HIR) + 1;
	poweren_ep_write_hir(MR_A2_GEN_HIR, gencount);
}

int poweren_ep_memreg_sync_local(struct poweren_ep_mr *mr)
{
	int i, rc, ret;
	struct poweren_ep_vf *vf;
	struct poweren_ep_crb_dmax *crb;
	struct poweren_ep_csb *csb;

	vf = mr->vf;
	crb = poweren_ep_cxb_alloc(GFP_KERNEL);
	csb = poweren_ep_cxb_alloc(GFP_KERNEL);

	crb->ccw = 0;
	crb->ccw = (DMAX_CT << 16) | DMAX_WRITE;
	crb->csbp = (u64) csb;

	crb->source.byte_count = MR_SIZE;
	crb->source.addr = ((uint64_t) mr->local_mem);

	crb->target.byte_count = MR_SIZE;
	crb->target.addr = MR_REMOTE_HANDLE(vf->vf_num);

	csb->control = 0;

	poweren_ep_debug("Source @ 0x%llx\n", (u64) crb->source.addr);
	poweren_ep_debug("Target @ 0x%llx\n", (u64) crb->target.addr);

	poweren_ep_debug("Issuing ICSWX\n");

	ret = icswx(crb->ccw, crb);
	for (i = 0; i < MR_ICSWX_RETRIES && ret == -EAGAIN; ++i)
		ret = icswx_raw(crb->ccw, crb);

	if (!ret) {
		rc = poweren_ep_csb_wait_valid(csb);

		if (unlikely(rc != 0)) {
			poweren_ep_error("CSB WAIT TIMEOUT!\n");
			poweren_ep_cxb_free(crb);
			poweren_ep_cxb_free(csb);
			return rc;
		}

		ret = (csb->control & 0x0000FF00) >> 8;

		if (!ret)
			poweren_ep_debug("DMA fulfilled\n");
		else
			poweren_ep_error("DMA FAILED! (%x)\n", ret);

	} else {
		poweren_ep_error("ICSWX err: %d\n", ret);
	}

	poweren_ep_cxb_free(crb);
	poweren_ep_cxb_free(csb);

	return ret;
}

int poweren_ep_memreg_sync_remote(struct poweren_ep_mr *mr)
{
	u64 gencount;
	int remote_len, ret, rc, i;
	struct poweren_ep_vf *vf;
	struct poweren_ep_crb_dmax *crb;
	struct poweren_ep_csb *csb;

	/* Lock before data structure init so we don't have to
	 * unlock/relock if we get a mismatching gencount at the end */

	vf = mr->vf;

 try_sync:
	ret = mutex_lock_interruptible(&remote_mem_lock);
	if (ret) {
		poweren_ep_error("lock not acquired\n");
		return ret;
	}

	gencount = poweren_ep_read_hir(MR_HOST_GEN_HIR);

	/* If the gencount hasn't changed, we don't
	 * need to waste the DMA icswx */

	if (gencount == cur_gencount) {
		poweren_ep_debug("Same gencount, update unneeded.\n");
		ret = 0;
		mutex_unlock(&remote_mem_lock);

		return ret;
	}

	/* If gencount is odd, the host is in the middle
	 * of an update, so we should wait and try again. */

	if (gencount % 2 != 0) {
		mutex_unlock(&remote_mem_lock);
		msleep(100);
		goto try_sync;
	}

	poweren_ep_debug("Trying for gencount %lld\n", gencount);

	crb = poweren_ep_cxb_alloc(GFP_KERNEL);
	csb = poweren_ep_cxb_alloc(GFP_KERNEL);

	crb->ccw = 0;
	crb->ccw = (DMAX_CT << 16) | DMAX_READ;
	crb->csbp = (u64) csb;

	poweren_ep_debug("HOST LOCAL @ 0x%llx\n",
			(u64) MR_LOCAL_HANDLE(vf->vf_num));
	crb->source.byte_count = MR_SIZE;
	crb->source.addr = ((uint64_t) MR_LOCAL_HANDLE(vf->vf_num));

	poweren_ep_debug("A2 REMOTE @ 0x%llx\n", (u64) mr->remote_mem_scratch);
	crb->target.byte_count = MR_SIZE;
	crb->target.addr = ((uint64_t) mr->remote_mem_scratch);

	csb->control = 0;

	poweren_ep_debug("Issuing ICSWX\n");

	ret = icswx(crb->ccw, crb);
	for (i = 0; i < MR_ICSWX_RETRIES && ret == -EAGAIN; ++i)
		ret = icswx_raw(crb->ccw, crb);

	if (!ret) {
		rc = poweren_ep_csb_wait_valid(csb);

		if (unlikely(rc != 0)) {
			poweren_ep_error("CSB WAIT TIMEOUT!\n");
			mutex_unlock(&remote_mem_lock);
			poweren_ep_cxb_free(crb);
			poweren_ep_cxb_free(csb);
		}

		ret = (csb->control & 0x0000FF00) >> 8;

		if (!ret) {
			cur_gencount = gencount;
		} else {
			mutex_unlock(&remote_mem_lock);
			poweren_ep_error("DMA FAILED! (%x)\n", ret);
			return ret;
		}

	} else {
		mutex_unlock(&remote_mem_lock);
		poweren_ep_error("ICSWX err: %d\n", ret);
		return ret;
	}

	poweren_ep_debug("Succeeded on gencount %lld\n", cur_gencount);

	/* We can't just swap buffers because the endpoint updates the
	 * host remote registration buffer and has no way to know the address
	 * has changed */

	memcpy(mr->remote_mem, mr->remote_mem_scratch, MR_SIZE);

	remote_len = 0;
	for (i = 0; i < MR_MEM_SLOTS; i++) {
		if (mr->remote_mem[i].valid)
			remote_len++;
		else
			break;
	}

	mutex_unlock(&remote_mem_lock);
	poweren_ep_cxb_free(crb);
	poweren_ep_cxb_free(csb);

	poweren_ep_debug("Got %d remote entries.\n", remote_len);

	return ret;
}

int __poweren_ep_memreg_setup(struct poweren_ep_mr *mr, unsigned long size)
{
	mr->local_mem_pid = kzalloc(size, GFP_KERNEL);
	mr->remote_mem_scratch = kzalloc(size, GFP_KERNEL);
	mr->remote_mem = kzalloc(size, GFP_KERNEL);
	mr->local_mem = kzalloc(size, GFP_KERNEL);

	if (!mr->local_mem_pid || !mr->remote_mem_scratch ||
		!mr->remote_mem || !mr->local_mem) {
		poweren_ep_error("failed allocating memory\n");
		kfree(mr->local_mem);
		kfree(mr->remote_mem);
		kfree(mr->remote_mem_scratch);
		kfree(mr->local_mem_pid);
		return -ENOMEM;
	}

	poweren_ep_debug("remote_mem %p, remote_mem_scratch %p\n",
			mr->remote_mem, mr->remote_mem_scratch);
	poweren_ep_debug("local_mem %p, local_mem_pid %p",
			mr->local_mem, mr->local_mem_pid);

	return 0;
}

int __poweren_ep_memreg_exit(struct poweren_ep_mr *mr, unsigned long size)
{
	kfree(mr->local_mem_pid);
	kfree(mr->remote_mem);
	kfree(mr->remote_mem_scratch);
	kfree(mr->local_mem);

	return 0;
}
