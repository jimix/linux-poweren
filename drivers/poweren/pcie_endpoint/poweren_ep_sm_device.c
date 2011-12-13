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

#include "poweren_ep_sm.h"

/* Inbound mbox/doorbell mapping table constants */
#define AS_BIT_INMAP            0x1000000
#define PID_INMAP_MASK          0x3FFF

#define HPID_IN_NEGOTIATION		-810

static struct poweren_ep_slotmgr **g_slotmgrs;

static int poweren_ep_slotmgr_reserve_local_slot(
		struct poweren_ep_slotmgr *slotmgr, u32 protocol)
{
	struct poweren_ep_slotmgr_slot *slot;
	u32 idx;

	/* Find a free slot, but start at last used slot+1 */
	for (idx = 0; idx < (MAX_SLOTS - RESERVED_SLOTS); idx++) {
		slotmgr->slot_id = (slotmgr->slot_id + 1) %
			(MAX_SLOTS - RESERVED_SLOTS);
		slot = &slotmgr->slots[slotmgr->slot_id];
		if (slot->protocol == 0) {
			/* immediately set valid dpid and invalid hpid */
			slot->hpid = HPID_IN_NEGOTIATION;
			slot->dpid = current->pid;
			slot->protocol = protocol;
			/* The mmio buffers are ioremapped so they are cached */
			mb();

			return slotmgr->slot_id;
		}
	}

	return -1;
}

/*
 * device waits for host request and sends a reply
 * slotmgr - slotmgr for the vf/pf
 * local_mmio - local MMIO area
 * remote_mmio- remote MMIO area
 * protocol - unique id for app/device to pair
 * *size - size of "slot"
 *
 * returns - a slot id
 */
int poweren_ep_slotmgr_connect(struct poweren_ep_vf *vf,
		u32 protocol, u32 flags, unsigned long *slot_size)
{
	int err;
	u32 slot_id = -1;
	unsigned long slot_offset = -1;
	pid_t hpid;
	pid_t my_dpid = current->pid;
	struct poweren_ep_slotmgr *slotmgr;

	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return -ENOMEM;
	}

	slotmgr = g_slotmgrs[vf->vf_num];

	if (unlikely(!slotmgr)) {
		poweren_ep_error("no slotmgr found");
		return SLOTMGR_ERR;
	}

	/* Veth uses a reserved slot */
	if (flags == RES_SLOT_VETH) {
		err = LOCK_INT(slotmgr);
		if (err) {
			poweren_ep_error("lock not acquired\n");
			return -EAGAIN;
		}

		slot_id = VETH_SLOT;

		/* calculate an offset and an allocated memory size */
		*slot_size = slotmgr->local_mmio->size / MAX_SLOTS;

		/* need to make size an integer multiple of PAGE_SIZE */
		*slot_size = (*slot_size / POWEREN_PAGE_SIZE) *
				POWEREN_PAGE_SIZE;
		slot_offset = slot_id * (*slot_size);

		/* memset the slot */
		memset(slotmgr->local_mmio->virt_addr + slot_offset,
				0, *slot_size);
		memset(slotmgr->remote_mmio->virt_addr + slot_offset,
				0, *slot_size);

		/* setup the slot for mmap */
		slotmgr->slots[slot_id].protocol = protocol;
		slotmgr->slots[slot_id].offset = slot_offset;
		slotmgr->slots[slot_id].size = *slot_size;
		slotmgr->slots[slot_id].local_mmio_req = 0;
		slotmgr->slots[slot_id].remote_mmio_req = 0;
		slotmgr->slots[slot_id].dpid = my_dpid;

		UNLOCK(slotmgr);
		return slot_id  << PAGE_SHIFT;
	}


	/* if I am not already in a slot exchange  */
	if (!flags) {
		err = LOCK_INT(slotmgr);
		if (err) {
			poweren_ep_error("lock not acquired\n");
			return -ENODEV;
		}

		/* check if the host is looking for an app like me*/
		if (poweren_ep_read_hir(SLOTMGR_PROTOCOL_HIR) != protocol) {
			UNLOCK(slotmgr);
			return DEVICE_NO_SLOT_REQ;
		}

		/* another app like me could have got the lock first so
		 * double check that the host is still waiting */
		if (poweren_ep_read_hir(SLOTMGR_CTRL_HIR) != SLOT_REQ) {
			UNLOCK(slotmgr);
			return DEVICE_NO_SLOT_REQ;
		}

		poweren_ep_write_hir(SLOTMGR_CTRL_HIR, SLOT_REQ_ACK);

		/* find a slot and set it up */
		slot_id = poweren_ep_slotmgr_reserve_local_slot(slotmgr,
				protocol);
		if (slot_id == -1) {
			/* if there are no free slots then reset the SLOT_REQ */
			poweren_ep_write_hir(SLOTMGR_CTRL_HIR, SLOT_REQ);
			UNLOCK(slotmgr);
			return DEVICE_NO_FREE_SLOTS;
		}

		/* we have a free slot and a waiting host, setup the slot
		 * details and pass the informaion to the host */

		/* calculate an offset and an allocated memory size */
		*slot_size = slotmgr->local_mmio->size / MAX_SLOTS;
		/* need to make size an integer multiple of POWEREN_PAGE_SIZE */
		*slot_size = (*slot_size / POWEREN_PAGE_SIZE) *
			POWEREN_PAGE_SIZE;
		slot_offset = slot_id * (*slot_size);

		/* memset the slot */
		memset(slotmgr->local_mmio->virt_addr + slot_offset, 0,
				*slot_size);
		memset(slotmgr->remote_mmio->virt_addr + slot_offset, 0,
				*slot_size);

		/* setup the slot for mmap */
		slotmgr->slots[slot_id].protocol = protocol;
		slotmgr->slots[slot_id].offset = slot_offset;
		slotmgr->slots[slot_id].size = *slot_size;
		slotmgr->slots[slot_id].local_mmio_req = 0;
		slotmgr->slots[slot_id].remote_mmio_req = 0;

		/* push the slot id to the host*/
		poweren_ep_write_hir(SLOTMGR_DPID_HIR, my_dpid);
		poweren_ep_write_hir(SLOTMGR_SLOT_HIR, slot_id);
		poweren_ep_write_hir(SLOTMGR_CTRL_HIR, SLOT_ALLOC);

		UNLOCK(slotmgr);
	}

	/* has the host taken the slot */
	err = LOCK_INT(slotmgr);
	if (err) {
		poweren_ep_error("lock not acquired\n");
		return -EAGAIN;
	}
	if (poweren_ep_read_hir(SLOTMGR_CTRL_HIR) == SLOT_ALLOC_ACK) {

		/* retrieve the slot from the hirs */
		slot_id =  poweren_ep_read_hir(SLOTMGR_SLOT_HIR);

		/*  store pids */
		hpid = poweren_ep_read_hir(SLOTMGR_HPID_HIR);
		slotmgr->slots[slot_id].dpid = my_dpid;
		slotmgr->slots[slot_id].hpid = hpid;

		/*reset the hirs */
		poweren_ep_write_hir(SLOTMGR_PROTOCOL_HIR, 0);
		poweren_ep_write_hir(SLOTMGR_CTRL_HIR, NO_SLOT_REQ);
		UNLOCK(slotmgr);

		/* set the slot size to be returned */
		*slot_size = slotmgr->slots[slot_id].size;

		poweren_ep_debug("slot %d, protocol %d, offset %lu,"
				" size %lu pid %d ", slot_id,
				slotmgr->slots[slot_id].protocol,
				slotmgr->slots[slot_id].offset,
				slotmgr->slots[slot_id].size,
				slotmgr->slots[slot_id].dpid);

		return slot_id << PAGE_SHIFT;
	}

	/* Check if we or the host have timed out */
	if ((poweren_ep_read_hir(SLOTMGR_CTRL_HIR) ==
				SLOT_REQ_TIMEOUT) || (flags == SLOT_TIMEOUT)) {

		/*release the slot*/
		slot_id =  poweren_ep_read_hir(SLOTMGR_SLOT_HIR);
		poweren_ep_slotmgr_release_local_slot(slotmgr, slot_id);

		/* reset the hirs */
		poweren_ep_write_hir(SLOTMGR_PROTOCOL_HIR, 0);
		poweren_ep_write_hir(SLOTMGR_CTRL_HIR, NO_SLOT_REQ);

		UNLOCK(slotmgr);

		return DEVICE_SLOT_REQ_TIMEOUT;
	}
	UNLOCK(slotmgr);
	return DEVICE_SLOT_REQ;
}
EXPORT_SYMBOL_GPL(poweren_ep_slotmgr_connect);

void poweren_ep_slotmgr_connect_cleanup(struct poweren_ep_vf *vf)
{
	int err, i;
	struct poweren_ep_slotmgr *slotmgr;

	slotmgr = g_slotmgrs[vf->vf_num];

	err = LOCK_INT(slotmgr);

	if (err) {
		poweren_ep_error("lock not acquired\n");
		return;
	}

	for (i = 0; i < MAX_SLOTS; i++) {
		if (slotmgr->slots[i].dpid == current->pid) {
			if(slotmgr->slots[i].hpid == HPID_IN_NEGOTIATION) {
				/* mid-negotiation - reset the hirs */
				poweren_ep_write_hir(SLOTMGR_PROTOCOL_HIR, 0);
				poweren_ep_write_hir(SLOTMGR_CTRL_HIR, NO_SLOT_REQ);
			}

			/*release the slot*/
			poweren_ep_slotmgr_release_local_slot(slotmgr, i);

			break;
		}
	}

	UNLOCK(slotmgr);
}
EXPORT_SYMBOL_GPL(poweren_ep_slotmgr_connect_cleanup);

int poweren_ep_slotmgr_find_slot(struct poweren_ep_vf *vf,
		u32 protocol, unsigned long *slot_size)
{
	u32 i;
	pid_t mypid = current->pid;
	struct poweren_ep_slotmgr *slotmgr;

	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return -1;
	}

	slotmgr = g_slotmgrs[vf->vf_num];

	if (unlikely(!slotmgr)) {
		poweren_ep_error("no slotmgr found");
		return -1;
	}

	for (i = 0; i < MAX_SLOTS; i++) {
		if ((slotmgr->slots[i].dpid == mypid) &&
				(slotmgr->slots[i].protocol == protocol) &&
				(slotmgr->slots[i].local_mmio_req == 1)) {
			*slot_size = slotmgr->slots[i].size;
			poweren_ep_info("slot %d, protocol %d,"
					" offset %lu, size %lu pid %d ", i,
					slotmgr->slots[i].protocol,
					slotmgr->slots[i].offset,
					slotmgr->slots[i].size,
					slotmgr->slots[i].hpid);
			return i << PAGE_SHIFT;
		}
	}
	return -1;
}
EXPORT_SYMBOL_GPL(poweren_ep_slotmgr_find_slot);

void poweren_ep_set_slotmgrs(struct poweren_ep_slotmgr *slotmgrs[TOTAL_FUNCS])
{
	g_slotmgrs = slotmgrs;
}

int poweren_ep_local_mmio_setup(void)
{
	u64 val;
	int i, ret;
	unsigned long order;
	struct poweren_ep_slotmgr *slotmgr;

	order = get_order(MMIO_SIZE);

	for (i = 0; i < TOTAL_FUNCS; i++) {
		slotmgr = g_slotmgrs[i];

		/* Prepare memory */
		slotmgr->local_mmio->virt_addr =
			(void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					order);

		if (!slotmgr->local_mmio->virt_addr) {
			ret = -ENOMEM;
			goto local_mmio_setup_failed;
		}

		slotmgr->local_mmio->size = MMIO_SIZE;

		/* Setup the Inbound Mapping Table to associate inbound MMIO
		 * transfers from PF0 and PF1 to use different AS bits.
		 *
		 * This is our only option for differentiating the two (easily -
		 * without using non-kernel pids) because PF0 and PF1 writes all
		 * come in the same address range (0 - MMIO area size), but with
		 * different AS/GS/LPID/PID as setup in the Inbound Mapping
		 * Table.
		 */

		val = in_be64(slotmgr->ep_dev->in_map_table);
		val |= AS_BIT_INMAP;
		val &= ~PID_INMAP_MASK;
		val |= i + 1;
		out_be64(slotmgr->ep_dev->in_map_table, val);

		/* Bolt memory in EP PBIC */
		slotmgr->local_mmio->phys_addr =
			__pa((unsigned long) slotmgr->local_mmio->virt_addr);

		ret = poweren_ep_map_inbound_mem(
				slotmgr->local_mmio->phys_addr,
				0, BOOK3E_PAGESZ_1M, i);
		if (ret < 0) {
			free_pages((u64) slotmgr->local_mmio->virt_addr, order);
			goto local_mmio_setup_failed;
		}

		poweren_ep_debug("slotmgr %d LOCAL  phys %llx", i,
				slotmgr->local_mmio->phys_addr);
		poweren_ep_debug("slotmgr %d LOCAL  virt %p", i,
				slotmgr->local_mmio->virt_addr);
	}

	return 0;

 local_mmio_setup_failed:
	for (i = i - 1; i >= 0; i--) {
		slotmgr = g_slotmgrs[i];
		poweren_ep_unmap_inbound_mem(i);
		free_pages((u64) slotmgr->local_mmio->virt_addr,
				order);
	}

	return ret;
}

int poweren_ep_remote_mmio_setup(void)
{
	int i;
	u64 base, mask, offset;
	struct poweren_ep_slotmgr *slotmgr;

	for (i = 0; i < TOTAL_FUNCS; i++) {
		slotmgr = g_slotmgrs[i];

		base = slotmgr->ep_dev->out_mmio_base;
		mask = slotmgr->ep_dev->out_mmio_mask;
		offset = (MMIO_START + i * MMIO_MAX_SIZE) & ~mask;

		slotmgr->remote_mmio->phys_addr = base + offset;
		slotmgr->remote_mmio->size = MMIO_SIZE;
		slotmgr->remote_mmio->virt_addr = (void __iomem *) ioremap(
				slotmgr->remote_mmio->phys_addr,
				MMIO_SIZE);

		if (!slotmgr->remote_mmio->virt_addr) {
			poweren_ep_error("error ioremapping outbound"
					" mmio addr\n");
			for (i = i - 1; i >= 0; i--)
				iounmap(slotmgr->remote_mmio->virt_addr);
			return -ENOMEM;
		}
		poweren_ep_debug("slotmgr %d REMOTE  phys %llx", i,
				slotmgr->remote_mmio->phys_addr);
		poweren_ep_debug("slotmgr %d REMOTE  virt %p", i,
				slotmgr->remote_mmio->virt_addr);
	}

	return 0;
}

void poweren_ep_local_mmio_free(void)
{
	int i;
	struct poweren_ep_slotmgr *slotmgr;

	for (i = 0; i < TOTAL_FUNCS; i++) {
		slotmgr = g_slotmgrs[i];

		if (slotmgr->local_mmio->virt_addr)
			poweren_ep_unmap_inbound_mem(i);
			__free_pages(slotmgr->local_mmio->virt_addr,
					get_order(slotmgr->local_mmio->size));
	}
}

void poweren_ep_remote_mmio_free(void)
{
	int i;
	struct poweren_ep_slotmgr *slotmgr;

	for (i = 0; i < TOTAL_FUNCS; i++) {
		slotmgr = g_slotmgrs[i];

		if (slotmgr->remote_mmio->virt_addr)
			iounmap(slotmgr->remote_mmio->virt_addr);
	}
}
