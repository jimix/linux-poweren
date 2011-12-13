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

#include "poweren_ep_bp.h"
#include "poweren_ep_mr.h"

void poweren_ep_inc_gencount(void)
{
	u64 gencount;

	gencount = poweren_ep_read_hir(MR_HOST_GEN_HIR) + 1;
	poweren_ep_write_hir(MR_HOST_GEN_HIR, gencount);
}

static int poweren_ep_alloc_mapped_mem(struct poweren_ep_mr *mr,
			struct ep_reg_mem **virt, dma_addr_t dma_addr,
			unsigned long size)
{
	int rc;
	struct poweren_ep_vf *vf;
	struct iommu_domain *iommu_dom;

	vf = mr->vf;

	poweren_ep_debug("Func_num %d, *Virt %p, dma_addr %llx,"
			" size %lu order %d\n",
			vf->vf_num, *virt, dma_addr, size, get_order(size));

	iommu_dom = poweren_ep_get_iommu_dom(vf);
	if (!iommu_dom) {
		poweren_ep_error("error obtaining iommu domain for fn %d\n",
				vf->vf_num);
		return -ENOMEM;
	}

	*virt = (void *) __get_free_pages(GFP_KERNEL | __GFP_ZERO,
		get_order(size));
	if (!(*virt)) {
		poweren_ep_error("get_free_pages returned %p", *virt);
		return -ENOMEM;
	}

	poweren_ep_debug("Mapping pages at %llx to dma addr %llx\n",
			virt_to_phys(virt), dma_addr);

	rc = iommu_map_range(iommu_dom, dma_addr, virt_to_phys(*virt),
			size, IOMMU_READ | IOMMU_WRITE);
	if (rc) {
		poweren_ep_debug("Error mapping pages at %llx"
				" to dma addr %llx\n",
				virt_to_phys(*virt), dma_addr);
		free_pages((unsigned long) *virt, get_order(size));
		return rc;
	}

	return 0;
}

static void poweren_ep_free_mapped_mem(struct poweren_ep_mr *mr,
		struct ep_reg_mem *virt, dma_addr_t dma_addr,
		unsigned long size)
{
	struct poweren_ep_vf *vf;
	struct iommu_domain *iommu_dom;

	vf = mr->vf;

	iommu_dom = poweren_ep_get_iommu_dom(vf);

	iommu_unmap_range(iommu_dom, dma_addr, size);

	free_pages((unsigned long) virt, get_order(size));
}

int poweren_ep_memreg_sync_local(struct poweren_ep_mr *mr)
{
	return 0;
}

int poweren_ep_memreg_sync_remote(struct poweren_ep_mr *mr)
{
	return 0;
}

int __poweren_ep_memreg_setup(struct poweren_ep_mr *mr, unsigned long size)
{
	int err;
	struct poweren_ep_vf *vf = mr->vf;

	mr->local_mem_pid = kzalloc(size, GFP_KERNEL);
	mr->remote_mem_scratch = kzalloc(size, GFP_KERNEL);

	if (!mr->local_mem_pid || !mr->remote_mem_scratch) {
		poweren_ep_error("failed allocating memory\n");
		goto failed_mr_alloc;
	}

	err = poweren_ep_alloc_mapped_mem(mr, &mr->remote_mem,
			MR_REMOTE_HANDLE(vf->vf_num), size);

	poweren_ep_debug("remote_mem %p, remote_mem_scratch %p\n",
			mr->remote_mem, mr->remote_mem_scratch);

	if (err) {
		poweren_ep_error("failed allocating/mapping remote_mem\n");
		goto failed_remote_mem;
	}

	err = poweren_ep_alloc_mapped_mem(mr, &mr->local_mem,
			MR_LOCAL_HANDLE(vf->vf_num), size);

	poweren_ep_debug("local_mem %p, local_mem_pid %p",
			mr->local_mem, mr->local_mem_pid);

	if (err) {
		poweren_ep_error("failed allocating/mapping local_mem\n");
		goto failed_local_mem;
	}

	poweren_ep_debug("Remote handle %lx, local handle %lx\n",
			MR_REMOTE_HANDLE(vf->vf_num),
			MR_LOCAL_HANDLE(vf->vf_num));

	return 0;

 failed_local_mem:
	poweren_ep_free_mapped_mem(mr, mr->remote_mem,
			MR_REMOTE_HANDLE(vf->vf_num), size);
 failed_remote_mem:
 failed_mr_alloc:
	kfree(mr->remote_mem_scratch);
	kfree(mr->local_mem_pid);
	return -ENOMEM;
}

int __poweren_ep_memreg_exit(struct poweren_ep_mr *mr, unsigned long size)
{
	struct poweren_ep_vf *vf = mr->vf;

	kfree(mr->local_mem_pid);
	kfree(mr->remote_mem_scratch);
	poweren_ep_free_mapped_mem(mr, mr->local_mem,
			MR_LOCAL_HANDLE(vf->vf_num), size);
	poweren_ep_free_mapped_mem(mr, mr->remote_mem,
			MR_REMOTE_HANDLE(vf->vf_num), size);

	return 0;
}
