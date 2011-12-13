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

#include <linux/sched.h>
#include <asm/mtrr.h>

#include "poweren_ep_sm.h"

static struct poweren_ep_slotmgr **g_slotmgrs;

/*
 * host sends a command to device to request a "slot"
 * slotmgr - slotmgr for the vf/pf
 * protocol - unique id for app/device to pair
 * *slot_size - size of "slot"
 * flags - user flags
 *
 * returns - a slot id
 */
int poweren_ep_slotmgr_connect(struct poweren_ep_vf *vf,
		u32 protocol, u32 flags, unsigned long *slot_size)
{
	int err;
	u32 slot_id = -1;
	unsigned long slot_offset = -1;
	u64 slotmgr_status;
	pid_t mypid = current->pid;
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

		slotmgr_status = poweren_ep_read_hir(SLOTMGR_CTRL_HIR);
		slot_id = VETH_SLOT;
		*slot_size = slotmgr->local_mmio->size / MAX_SLOTS;

		/* need to make size an integer multiple of PAGE_SIZE */
		*slot_size = (*slot_size / POWEREN_PAGE_SIZE) *
			POWEREN_PAGE_SIZE;
		slot_offset = slot_id * (*slot_size);

		/* setup for mmap and return */
		slotmgr->slots[slot_id].protocol = protocol;
		slotmgr->slots[slot_id].offset = slot_offset;
		slotmgr->slots[slot_id].size = *slot_size;
		slotmgr->slots[slot_id].local_mmio_req = 0;
		slotmgr->slots[slot_id].remote_mmio_req = 0;
		slotmgr->slots[slot_id].hpid = mypid;

		/* set the last used slot */
		UNLOCK(slotmgr);
		return slot_id << PAGE_SHIFT;
	}

	if (!flags) {
		/* first grab the lock*/
		err = LOCK_INT(slotmgr);
		if (err) {
			poweren_ep_error("lock not acquired\n");
			return -ENODEV;
		}

		/* if there is a slot request in progress not started by me */
		if (poweren_ep_read_hir(SLOTMGR_CTRL_HIR)) {
			UNLOCK(slotmgr);
			return HOST_NO_SLOT_REQ;
		} else {
			/* if there is no slot request in progress start one */
			poweren_ep_write_hir(SLOTMGR_CTRL_HIR, SLOT_REQ);
			poweren_ep_write_hir(SLOTMGR_PROTOCOL_HIR, protocol);
			poweren_ep_write_hir(SLOTMGR_HPID_HIR, mypid);
			UNLOCK(slotmgr);
		}
	}

	if (flags == SLOT_TIMEOUT) { /* we have timed out */
		/* if a slot was pending then let the device sort it out */
		err = LOCK_INT(slotmgr);
		if (err) {
			poweren_ep_error("lock not acquired\n");
			return -EAGAIN;
		}

		if (poweren_ep_read_hir(SLOTMGR_CTRL_HIR) != SLOT_REQ) {
			poweren_ep_write_hir(SLOTMGR_CTRL_HIR,
					SLOT_REQ_TIMEOUT);

			UNLOCK(slotmgr);
			return HOST_SLOT_REQ_TIMEOUT;
		} else {
			/* reset the locks and the hirs */
			poweren_ep_write_hir(SLOTMGR_PROTOCOL_HIR, 0);
			poweren_ep_write_hir(SLOTMGR_CTRL_HIR, NO_SLOT_REQ);

			UNLOCK(slotmgr);
			return HOST_SLOT_REQ_TIMEOUT;
		}
	}

	err = LOCK_INT(slotmgr);
	if (err) {
		poweren_ep_error("lock not acquired\n");
		return -EAGAIN;
	}
	/* Check for any pending slots from the device */
	if (poweren_ep_read_hir(SLOTMGR_CTRL_HIR) == SLOT_ALLOC) {

		/* read the slot_id */
		slot_id = poweren_ep_read_hir(SLOTMGR_SLOT_HIR);
		poweren_ep_write_hir(SLOTMGR_CTRL_HIR, SLOT_ALLOC_ACK);
		UNLOCK(slotmgr);

		*slot_size = slotmgr->local_mmio->size / MAX_SLOTS;
		/* need to make size an integer multiple of POWEREN_PAGE_SIZE */
		*slot_size = (*slot_size / POWEREN_PAGE_SIZE) *
			POWEREN_PAGE_SIZE;
		slot_offset = slot_id*(*slot_size);

		/* setup for mmap and return */
		slotmgr->slots[slot_id].protocol = protocol;
		slotmgr->slots[slot_id].offset = slot_offset;
		slotmgr->slots[slot_id].size = *slot_size;
		slotmgr->slots[slot_id].local_mmio_req = 0;
		slotmgr->slots[slot_id].remote_mmio_req = 0;
		slotmgr->slots[slot_id].hpid = mypid;

		poweren_ep_debug("slot %d, protocol %d, offset %lu,"
				" size %lu pid %d ", slot_id,
				slotmgr->slots[slot_id].protocol,
				slotmgr->slots[slot_id].offset,
				slotmgr->slots[slot_id].size,
				slotmgr->slots[slot_id].hpid);

		return slot_id << PAGE_SHIFT;
	}
	UNLOCK(slotmgr);
	return HOST_SLOT_REQ;

}
EXPORT_SYMBOL_GPL(poweren_ep_slotmgr_connect);

void poweren_ep_slotmgr_connect_cleanup(struct poweren_ep_vf *vf)
{
}
EXPORT_SYMBOL_GPL(poweren_ep_slotmgr_connect_cleanup);

int poweren_ep_slotmgr_find_slot(struct poweren_ep_vf *vf,
		u32 protocol,  unsigned long *slot_size)
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
		if ((slotmgr->slots[i].hpid == mypid) &&
				(slotmgr->slots[i].protocol == protocol) &&
				(slotmgr->slots[i].local_mmio_req == 1)) {
			*slot_size = slotmgr->slots[i].size;
			poweren_ep_info("slot %d, protocol %d, offset %lu,"
					" size %lu pid %d ", i,
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
	int i;
	int ret;
	unsigned long order;
	struct poweren_ep_slotmgr *slotmgr;

	order = get_order(MMIO_SIZE);

	for (i = 0; i < TOTAL_FUNCS; i++) {
		slotmgr = g_slotmgrs[i];

		slotmgr->local_mmio->size = MMIO_SIZE;
		slotmgr->local_mmio->virt_addr = (void *) __get_free_pages(
				GFP_KERNEL | __GFP_ZERO, order);

		if (!slotmgr->local_mmio->virt_addr) {
			poweren_ep_error("Failed to allocate MMIO mem for"
					" function %d!\n", i);
			ret = -ENOMEM;
			goto local_mmio_setup_failed;
		}

		slotmgr->local_mmio->phys_addr = MMIO_START +
			(i * MMIO_MAX_SIZE);

		ret = iommu_map_range(slotmgr->ep_dev->iommu_dom,
				slotmgr->local_mmio->phys_addr,
				virt_to_phys(slotmgr->local_mmio->virt_addr),
				MMIO_SIZE, IOMMU_READ | IOMMU_WRITE);

		if (ret) {
			poweren_ep_error("failed to iommu map\n");
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
		iommu_unmap_range(slotmgr->ep_dev->iommu_dom,
				slotmgr->local_mmio->phys_addr,
				MMIO_SIZE);
		free_pages((u64) slotmgr->local_mmio->virt_addr,
				order);
	}

	return ret;
}

int poweren_ep_remote_mmio_setup(void)
{
	int i = 0;
	int ret = 0;
	struct poweren_ep_slotmgr *slotmgr;

	for (i = 0; i < TOTAL_FUNCS; i++) {
		slotmgr = g_slotmgrs[i];

		/* Mapping bar 4/5 for accessing outbound mmio buffers */
		slotmgr->remote_mmio->phys_addr =
			pci_resource_start(slotmgr->ep_dev->pdev, BAR_4_5);

		if (!slotmgr->remote_mmio->phys_addr) {
			poweren_ep_error("fail to obtain BAR 4/5"
					" for vf %u\n", i);
			ret = -ENODEV;
			goto remote_mmio_setup_failed;
		}

		slotmgr->mtrr_reg = mtrr_add(slotmgr->remote_mmio->phys_addr,
				MMIO_SIZE, MTRR_TYPE_WRCOMB, 0);

		if (slotmgr->mtrr_reg <= 0) {
			poweren_ep_error("it is not possible to enable write"
					" combining %d on fn %d for addr %llx"
					" size %lu\n", ret, i,
					slotmgr->remote_mmio->phys_addr,
					MMIO_SIZE);
			ret = slotmgr->mtrr_reg;
			goto remote_mmio_setup_failed;
		}

		slotmgr->remote_mmio->virt_addr =
			pci_ioremap_bar(slotmgr->ep_dev->pdev, BAR_4_5);

		if (!slotmgr->remote_mmio->virt_addr) {
			poweren_ep_error("fail to ioremap BAR 4/5 for vf %u"
					" virtual addr %p", i,
					slotmgr->remote_mmio->virt_addr);
			mtrr_del(slotmgr->mtrr_reg,
					slotmgr->remote_mmio->phys_addr,
					MMIO_SIZE);
			ret = -ENOMEM;
			goto remote_mmio_setup_failed;
		}

		slotmgr->remote_mmio->size = MMIO_SIZE;

		poweren_ep_debug("slotmgr %d REMOTE  phys %llx", i,
				slotmgr->remote_mmio->phys_addr);
		poweren_ep_debug("slotmgr %d REMOTE  virt %p", i,
				slotmgr->remote_mmio->virt_addr);

	}

	return 0;

remote_mmio_setup_failed:
	for (i = i - 1; i >= 0; i--) {
		slotmgr = g_slotmgrs[i];
		mtrr_del(slotmgr->mtrr_reg,
				slotmgr->remote_mmio->phys_addr, MMIO_SIZE);
		iounmap(slotmgr->remote_mmio->virt_addr);
	}

	return ret;
}

void poweren_ep_local_mmio_free(void)
{
	int i;
	struct poweren_ep_slotmgr *slotmgr;

	for (i = 0; i < TOTAL_FUNCS; i++) {
		slotmgr = g_slotmgrs[i];

		if (slotmgr->local_mmio->virt_addr) {
			iommu_unmap_range(slotmgr->ep_dev->iommu_dom,
					slotmgr->local_mmio->phys_addr,
					slotmgr->local_mmio->size);

			__free_pages(slotmgr->local_mmio->virt_addr,
					get_order(slotmgr->local_mmio->size));
		}
	}
}

void poweren_ep_remote_mmio_free(void)
{
	int i;
	struct poweren_ep_slotmgr *slotmgr;

	for (i = 0; i < TOTAL_FUNCS; i++) {
		slotmgr = g_slotmgrs[i];

		if (slotmgr->remote_mmio->virt_addr) {
			mtrr_del(slotmgr->mtrr_reg,
					slotmgr->remote_mmio->phys_addr,
					MMIO_SIZE);
			iounmap(slotmgr->remote_mmio->virt_addr);
		}
	}
}
