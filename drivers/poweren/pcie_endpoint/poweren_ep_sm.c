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

#include <linux/uaccess.h>

#include "poweren_ep_sm.h"

/* the set of slotmgrs - one set per pf/vf*/
static struct poweren_ep_slotmgr *slotmgrs[TOTAL_FUNCS];

struct poweren_ep_slotmgr *poweren_ep_get_slotmgr(int vf_num)
{
	if (vf_num >= TOTAL_FUNCS)
		return 0;

	return slotmgrs[vf_num];
}

long poweren_ep_ioctl_slotmgr(struct poweren_ep_vf *vf,
		struct file *f, unsigned int cmd, unsigned long arg)
{
	/* to process the user args */
	struct user_slot_req {
		u32 protocol;
		u32 flags;
	};

	struct poweren_ep_slotmgr *slotmgr;
	struct user_slot_req slot_req;
	void __user *uptr = (void __user *)arg;
	u32 rc = -1;
	unsigned long slot_size = 0;

	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return -ENOMEM;
	}

	slotmgr = slotmgrs[vf->vf_num];

	if (unlikely(!slotmgr)) {
		poweren_ep_error("no slotmgr found");
		return SLOTMGR_ERR;
	}

	rc = copy_from_user(&slot_req, uptr, sizeof(slot_req));
	if (rc == 0) {
		if (cmd == EP_IOCTL_SLOTMGR_CONNECT)
			rc = poweren_ep_slotmgr_connect(vf,
					slot_req.protocol, slot_req.flags,
					&slot_size);

		else if (cmd == EP_IOCTL_SLOTMGR_FIND_SLOT)
			rc = poweren_ep_slotmgr_find_slot(vf,
					slot_req.protocol, &slot_size);

		if (put_user(slot_size, (u64 __user *)arg))
			rc = -EFAULT;
	} else
		rc = -1;

	return rc;
}
EXPORT_SYMBOL_GPL(poweren_ep_ioctl_slotmgr);

/* setup some functions for use with mmap and vma */
static void poweren_ep_slotmgr_vma_close(struct vm_area_struct *vma)
{
	struct poweren_ep_slotmgr_vma_priv *vma_priv =
		(struct poweren_ep_slotmgr_vma_priv *) vma->vm_private_data;

	if (vma_priv) {
		poweren_ep_slotmgr_term(vma_priv->vf, vma_priv->mmio_slot);
		kfree(vma_priv);
		vma->vm_private_data = NULL;
	}

}

static struct vm_operations_struct poweren_ep_slotmgr_vma_ops = {
	.close = poweren_ep_slotmgr_vma_close,
};


/*  poweren_ep_slotmgr_mmap is used to allocate an MMIO slot to the userspace
 *  program.
 *  It is expected to be called twice, once to map the local MMIO space
 *  and again to map the remote MMIO space. It should be called in order
 *
 *  An initial call will find a slot,  map the local MMIO space and setup the
 *  release slot mechanism based on munmap.
 *  A subsequent call will map the remote MMIO space for that same slot
 *
 * Repeated calls to mmamp will alternately map local MMIO and remote MMIO,
 *
 */
int poweren_ep_slotmgr_mmap(struct poweren_ep_vf *vf, struct file *f,
		struct vm_area_struct *vma)
{
	unsigned long size;
	unsigned long offset;
	unsigned long address;
	struct poweren_ep_slotmgr_vma_priv *vma_priv;
	u32 slot_id = vma->vm_pgoff;
	struct poweren_ep_slotmgr *slotmgr;

	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return -ENOMEM;
	}

	slotmgr = slotmgrs[vf->vf_num];

	if (unlikely(!slotmgr)) {
		poweren_ep_error("no slotmgr found");
		return SLOTMGR_ERR;
	}

	poweren_ep_debug("mmap vf %u, slot_id %u local_map %d",
			vf->vf_num, slot_id,
			slotmgr->slots[slot_id].local_mmio_req);

	/* use the slot index from vm_pgoff to find the
	 * memory offset value to the MMIO slot
	 * and the size of the slot */
	offset = slotmgr->slots[slot_id].offset >> PAGE_SHIFT;
	size = slotmgr->slots[slot_id].size;

	/*if this is a new sequence for this slot*/
	if (slotmgr->slots[slot_id].local_mmio_req == 0) {

		/* setup some private data so we can release the slot later */
		vma_priv = (struct poweren_ep_slotmgr_vma_priv *)
			kzalloc(sizeof(*vma_priv), GFP_KERNEL);
		vma_priv->vf = vf;
		vma_priv->mmio_slot = slot_id;
		vma->vm_private_data = (void *)vma_priv;
		vma->vm_ops = &poweren_ep_slotmgr_vma_ops;

		/* kernel address to be mapped */
		address = (virt_to_phys(slotmgr->local_mmio->virt_addr) >>
				PAGE_SHIFT) + offset;
		slotmgr->slots[slot_id].local_mmio_req = 1;

	} else if (slotmgr->slots[slot_id].local_mmio_req == 1) {
		vma->vm_private_data = NULL;
		vma->vm_ops = NULL;

		/* kernel address to be mapped */
		address = (slotmgr->remote_mmio->phys_addr >> PAGE_SHIFT) +
			offset;


#ifdef CONFIG_PPC64
		vma->vm_page_prot = phys_mem_access_prot(f, address, size,
				vma->vm_page_prot);
#endif

		slotmgr->slots[slot_id].local_mmio_req = 0;
	} else
		return -EINVAL;

	poweren_ep_info("poweren_ep_slotmgr_mmap:"
			" vma->vm_start %lx, address %lx, size %lx",
			vma->vm_start, address, vma->vm_end - vma->vm_start);

	return io_remap_pfn_range(vma, vma->vm_start, address,
			vma->vm_end - vma->vm_start, vma->vm_page_prot);

}
EXPORT_SYMBOL_GPL(poweren_ep_slotmgr_mmap);

void poweren_ep_slotmgr_release_local_slot(
		struct poweren_ep_slotmgr *slotmgr,
		u32 slot_id)
{
	struct poweren_ep_slotmgr_slot *slot;

	slot = &slotmgr->slots[slot_id];
	slot->dpid = 0;
	slot->hpid = 0;
	slot->protocol = 0;
	/* The mmio buffers are ioremapped so they are cached */
	mb();
}

void poweren_ep_slotmgr_term(struct poweren_ep_vf *vf, u32 slot_id)
{
	int ret;
	struct poweren_ep_slotmgr *slotmgr = slotmgrs[vf->vf_num];

	poweren_ep_info("release slot %d on vf %d", slot_id, vf->vf_num);

	ret = LOCK_INT(slotmgr);
	if (ret) {
		poweren_ep_error("lock not acquired\n");
		return;
	}

	poweren_ep_slotmgr_release_local_slot(slotmgr, slot_id);

	UNLOCK(slotmgr);
}
EXPORT_SYMBOL_GPL(poweren_ep_slotmgr_term);

int poweren_ep_slotmgr_init(struct poweren_ep ep_dev[TOTAL_FUNCS])
{
	u32 i, j, ret;

	/* allocate the set of slotmgrs */
	for (i = 0; i < TOTAL_FUNCS; i++) {
		slotmgrs[i] = kzalloc(sizeof(*slotmgrs[i]), GFP_KERNEL);

		slotmgrs[i]->local_mmio = kzalloc(
				sizeof(*slotmgrs[i]->local_mmio), GFP_KERNEL);

		slotmgrs[i]->remote_mmio = kzalloc(
				sizeof(*slotmgrs[i]->remote_mmio), GFP_KERNEL);

		if (!slotmgrs[i] ||
			!slotmgrs[i]->local_mmio ||
			!slotmgrs[i]->remote_mmio) {
			poweren_ep_error("fail initializing memory for slotmgr:"
					" %d\n", i);
			kfree(slotmgrs[i]);
			kfree(slotmgrs[i]->local_mmio);
			kfree(slotmgrs[i]->remote_mmio);
			ret = -ENOMEM;
			goto slotmgr_init_failed;
		}

		/* Init slot manager mutex */
		mutex_init(&slotmgrs[i]->mutex);

		/* Initialise the allocated slot */
		slotmgrs[i]->slot_id = -1;

		/* Initialise the ep_dev pointer and function number*/
		slotmgrs[i]->ep_dev = &ep_dev[i];
		slotmgrs[i]->vf_num = i;

		for (j = 0 ; j < MAX_SLOTS; j++)
			/* flag to track slot mapping in mmap */
			slotmgrs[i]->slots[j].local_mmio_req = 0;
	}

	poweren_ep_set_slotmgrs(slotmgrs);

	poweren_ep_debug("slotmgr setup  local mmio");

	/* next setup the mmio regions */
	ret = poweren_ep_local_mmio_setup();

	if (ret) {
		poweren_ep_error("failed inbound mmio setup\n");
		goto slotmgr_init_failed;
	}

	poweren_ep_debug("slotmgr setup  remote mmio");
	ret = poweren_ep_remote_mmio_setup();

	if (ret) {
		poweren_ep_error("failed outbound mmio setup\n");
		poweren_ep_local_mmio_free();
		goto slotmgr_init_failed;
	}

	poweren_ep_debug("setup HIRS");

	/* reset the HIRS */
	poweren_ep_write_hir(SLOTMGR_CTRL_HIR, 0);
	poweren_ep_write_hir(SLOTMGR_PROTOCOL_HIR, 0);
	poweren_ep_write_hir(SLOTMGR_SLOT_HIR, 0);
	poweren_ep_write_hir(SLOTMGR_DPID_HIR, 0);
	poweren_ep_write_hir(SLOTMGR_HPID_HIR, 0);

	poweren_ep_info("slomgr_init complete");

	return 0;

 slotmgr_init_failed:
	for (i = i - 1; i >= 0; i--) {
		kfree(slotmgrs[i]);
		kfree(slotmgrs[i]->local_mmio);
		kfree(slotmgrs[i]->remote_mmio);
	}

	return 0;
}

void poweren_ep_slotmgr_exit(void)
{
	int i;

	poweren_ep_local_mmio_free();
	poweren_ep_remote_mmio_free();

	for (i = 0; i < TOTAL_FUNCS; i++) {
		kfree(slotmgrs[i]);
		kfree(slotmgrs[i]->local_mmio);
		kfree(slotmgrs[i]->remote_mmio);
	}
}

void *poweren_ep_get_slot_local_sma(struct poweren_ep_vf *vf, u32 slot)
{
	void *addr;
	struct poweren_ep_slotmgr *slotmgr;

	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return 0;
	}

	slotmgr = slotmgrs[vf->vf_num];

	if (unlikely(!slotmgr)) {
		poweren_ep_error("no slotmgr found");
		return 0;
	}

	slotmgr->slots[slot].local_mmio_req = 1;
	addr = slotmgr->local_mmio->virt_addr + slotmgr->slots[slot].offset;

	return addr;
}
EXPORT_SYMBOL_GPL(poweren_ep_get_slot_local_sma);

void *poweren_ep_get_slot_remote_sma(struct poweren_ep_vf *vf, u32 slot)
{
	void *addr;
	struct poweren_ep_slotmgr *slotmgr;

	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return 0;
	}

	slotmgr = slotmgrs[vf->vf_num];

	if (unlikely(!slotmgr)) {
		poweren_ep_error("no slotmgr found");
		return 0;
	}

	slotmgr->slots[slot].local_mmio_req = 0;
	addr = slotmgr->remote_mmio->virt_addr + slotmgr->slots[slot].offset;

	return addr;
}
EXPORT_SYMBOL_GPL(poweren_ep_get_slot_remote_sma);

int poweren_ep_find_slot_from_addr(struct poweren_ep_vf *vf, u64 addr)
{
	u32 i;
	struct poweren_ep_slotmgr *slotmgr;

	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return -1;
	}

	slotmgr = slotmgrs[vf->vf_num];

	if (unlikely(!slotmgr)) {
		poweren_ep_error("no slotmgr found");
		return -1;
	}

	for (i = 0; i < MAX_SLOTS; i++) {
		if (addr - (u64)slotmgr->local_mmio->virt_addr ==
				slotmgr->slots[i].offset)
			return i;
	}
	return -1;

}
EXPORT_SYMBOL_GPL(poweren_ep_find_slot_from_addr);
