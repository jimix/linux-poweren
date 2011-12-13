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
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/scatterlist.h>
#include <linux/pci.h>
#include <linux/iommu.h>
#include <linux/sched.h>

#include "poweren_ep_mm.h"
#include "poweren_ep_bp.h"
#include "poweren_ep_driver.h"

#define DMA_BASE_ADDR		0x10000000ull
#define DMA_ALLOC_ADDR		0xfffff000ull
#define DMA_ALLOC_SLOT_SIZE	0x1000ull

/**
 * struct poweren_ep_dma_map - describes user space DMA mapping
 * @maps		list of mappings for this process
 * @proc		list of all processes with mappings
 * @pid			process id
 * @v_addr		virtual address in process address space
 * @size:		Size of buffer, in bytes
 * @sg:			vmaloc'ed scatter/gather list for pci_map/unmap_sg
 */
struct poweren_ep_dma_map {
	struct list_head	maps;
	struct list_head	procs;
	unsigned int		pid;
	void			*v_addr;
	u64			size;
	struct scatterlist	*sg;
};

/* DMA mapping specific data structures */
DEFINE_MUTEX(poweren_ep_dma_map_lock);
static struct list_head	poweren_ep_dma_map_list;

static void poweren_ep_unmap_mem_sg(struct poweren_ep_vf *vf, void *sg_in,
			int n_pages)
{
	int i = 0;
	struct iommu_domain *iommu_dom = NULL;
	struct scatterlist *sg, *sg_elem = NULL;

	iommu_dom = poweren_ep_get_iommu_dom(vf);

	sg = (struct scatterlist *)sg_in;
	poweren_ep_debug("Func_num %d, *sg %p, n_pages %d\n",
			vf->vf_num, sg, n_pages);

	for_each_sg(sg, sg_elem, n_pages, i) {
		iommu_unmap_range(iommu_dom, sg_elem->dma_address,
				PAGE_SIZE);
		poweren_ep_release_dma_addr(vf, sg_elem->dma_address);
		sg_elem->dma_address = 0;
		sg_elem->dma_length = 0;
	}
}

static int poweren_ep_map_mem_sg(struct poweren_ep_vf *vf, void *sg_in,
			int n_pages)
{
	int i, rc = 0;
	dma_addr_t dma_base;
	struct iommu_domain *iommu_dom = NULL;
	struct scatterlist *sg, *sg_elem = NULL;

	iommu_dom = poweren_ep_get_iommu_dom(vf);

	sg = (struct scatterlist *)sg_in;
	poweren_ep_debug("Func_num %d, *sg %p, n_pages %d\n",
			vf->vf_num, sg, n_pages);

	dma_base = poweren_ep_get_dma_addr(vf, n_pages * PAGE_SIZE);
	if (dma_base == 0) {
		poweren_ep_error("Error allocating DMA Address\n");
		return -1;
	}

	for_each_sg(sg, sg_elem, n_pages, i)
	{
		poweren_ep_debug("Attempting map on ");
		poweren_ep_debug("dma_addr: %p, page addr: %p page: %d\n",
				(void *)dma_base + (i*PAGE_SIZE),
				(void *)page_to_phys(sg_page(sg_elem)),
				i);
		rc = iommu_map_range(iommu_dom, dma_base + (i*PAGE_SIZE),
				page_to_phys(sg_page(sg_elem)),
				PAGE_SIZE, IOMMU_READ | IOMMU_WRITE);
		if (rc)
			break;

		sg_elem->dma_address = (dma_addr_t)(dma_base +
				(i*PAGE_SIZE));
		sg_elem->dma_length = sg_elem->length;
	}

	if (rc) {
		poweren_ep_error("poweren_ep_map_mem returned error %d", rc);
		poweren_ep_error("dma_addr: %p, page addr: %p page: %d\n",
				(void *)DMA_BASE_ADDR + (i*PAGE_SIZE),
				(void *)page_to_phys(sg_page(sg_elem)),
				i);
		poweren_ep_unmap_mem_sg(vf, sg_in, i);
		return rc;
	}

	return rc;
}

void poweren_ep_unmap_dma_by_pid(struct poweren_ep_vf *vf, unsigned int pid)
{
	struct poweren_ep_dma_map *pe, *me, *te;
	int i, ret;

	/*
	 * look for proc on list of procs with DMA mappings,  if found
	 * remove from the list.
	 */
	ret = mutex_lock_interruptible(&poweren_ep_dma_map_lock);
	if (ret) {
		poweren_ep_error("lock not acquired\n");
		return;
	}

	list_for_each_entry(pe, &poweren_ep_dma_map_list, procs) {
		if (pe->pid == pid)
			break;
	}
	if (pe->pid == pid)
		list_del(&pe->procs);
	mutex_unlock(&poweren_ep_dma_map_lock);

	if (pe->pid != pid)
		return;		/* proc not found */

	/*
	 * unmap each entry for this proc.  no need to hold lock as anchor
	 * entry has already been removed from proc list.
	 */
	list_for_each_entry_safe(me, te, &pe->maps, maps) {
		poweren_ep_unmap_mem_sg(vf, me->sg,
				me->size >> PAGE_SHIFT);
		for (i = 0; i < (me->size >> PAGE_SHIFT); i++)
			put_page(sg_page(&(me->sg[i])));
		vfree(me->sg);
		list_del(&me->maps);

		kfree(me);
	}

	/*
	 * don't forget to unmap/free the proc anchor entry
	 */
	poweren_ep_unmap_mem_sg(vf, pe->sg, pe->size >> PAGE_SHIFT);

	for (i = 0; i < (pe->size >> PAGE_SHIFT); i++)
		put_page(sg_page(&(me->sg[i])));
	vfree(pe->sg);

	kfree(pe);

	return;
}

static int poweren_ep_add_dma_list(void *v_addr, u64 size,
		struct scatterlist *sg)
{
	int err;
	struct poweren_ep_dma_map *me;
	struct poweren_ep_dma_map *le;

	me = kmalloc(sizeof(*me), GFP_KERNEL);
	if (!me)
		return -ENOMEM;

	me->pid = current->tgid;
	me->v_addr = v_addr;
	me->size = size;
	me->sg = sg;

	err = mutex_lock_interruptible(&poweren_ep_dma_map_lock);
	if (err) {
		poweren_ep_error("lock not acquired\n");
		kfree(me);
		return err;
	}

	poweren_ep_debug("dma map list @ %p\n",
			(void *)&poweren_ep_dma_map_list);

	list_for_each_entry(le, &poweren_ep_dma_map_list, procs) {
		if (le->pid == me->pid) {
			/*
			 * proc already in list, add to per-proc map list
			 */
			INIT_LIST_HEAD(&me->procs);
			list_add(&me->maps, &le->maps);
			goto add_found_proc;
		}
	}

	/*
	 * did not find proc in list, add it
	 */
	INIT_LIST_HEAD(&me->maps);
	list_add(&me->procs, &poweren_ep_dma_map_list);

add_found_proc:
	mutex_unlock(&poweren_ep_dma_map_lock);

	return 0;
}

int poweren_ep_dma_search(struct ep_reg_mem *r_arg)
{
	struct poweren_ep_dma_map *pe, *me;
	unsigned int pid;
	void *v_addr;
	int rc, err;

	pid = current->tgid;
	v_addr = (void *)r_arg->address;
	rc = 0; /* assume found */

	err = mutex_lock_interruptible(&poweren_ep_dma_map_lock);
	if (err) {
		poweren_ep_error("lock not acquired\n");
		return -1;
	}

	/*
	 * search the lists for a match based on pid and v_addr;
	 */
	list_for_each_entry(pe, &poweren_ep_dma_map_list, procs) {
		if (pe->pid == pid) {
			if (pe->v_addr == v_addr) {
				r_arg->address = (u64)sg_dma_address(pe->sg);
				r_arg->size = pe->size;
				goto search_found;
			}
			list_for_each_entry(me, &pe->maps, maps) {
				if (me->v_addr == v_addr) {
					r_arg->address = (u64)
						sg_dma_address(me->sg);
					r_arg->size = me->size;
					goto search_found;
				}
			}
		}
	}

	rc = -1; /* not found */

search_found:
	mutex_unlock(&poweren_ep_dma_map_lock);

	return rc;
}

static struct poweren_ep_dma_map *poweren_ep_del_dma_list(void *v_addr)
{
	int err;
	struct poweren_ep_dma_map *pe, *me;
	unsigned int pid = current->tgid;

	err = mutex_lock_interruptible(&poweren_ep_dma_map_lock);
	if (err) {
		poweren_ep_error("lock not acquired\n");
		return NULL;
	}

	/*
	 * search the proc list for a match, once a match is found then
	 * search the per-process map list.
	 */
	list_for_each_entry(pe, &poweren_ep_dma_map_list, procs) {
		if (pe->pid == pid) {
			if (pe->v_addr == v_addr) {
				me = pe;
				goto del_found_me;
			}
			list_for_each_entry(me, &pe->maps, maps) {
				if (me->v_addr == v_addr)
					goto del_found_me;
			}
		}
	}

	/*
	 * no matching entry found
	 */
	me = NULL;
	goto del_return_me;

del_found_me:
	if (list_empty(&me->maps)) {		/* only in proc list */
		list_del(&me->procs);
	} else if (list_empty(&me->procs)) {	/* only in map list */
		list_del(&me->maps);
	} else {				/* in both lists */
		list_del(&me->procs);
		pe = list_first_entry(&me->maps,
				struct poweren_ep_dma_map, maps);
		list_add(&pe->procs, &poweren_ep_dma_map_list);
		list_del(&me->maps);
	}

del_return_me:
	mutex_unlock(&poweren_ep_dma_map_lock);

	return me;
}


int poweren_ep_map_udma_buf(struct poweren_ep_vf *vf, unsigned long uarg)
{
	void __user *uptr;
	struct ep_usr_dma_buf arg;
	struct page **pages = NULL;
	struct scatterlist *sg = NULL;
	int n_pages;
	int i, rc;

	uptr = (void __user *)uarg;
	if (copy_from_user(&arg, uptr, sizeof(struct ep_reg_mem)))
		return -EFAULT;
	/*
	 * as good of a capability test as any as we are pinning
	 * pages.  shouldn't let "just anybody" do this.
	 */
	if (!can_do_mlock())
		return -EPERM;

	arg.daddr = (u64)0;

	/*
	 * uaddr must be page aligned.  size must be a multiple of page size.
	 */
	if (PAGE_ALIGN(arg.uaddr) != arg.uaddr ||
			!arg.size ||
			PAGE_ALIGN(arg.size) != arg.size) {
		poweren_ep_error("Invalid argument, uaddr or size\n");
		return -EINVAL;
	}

	/*
	 * get list of pages associated with user buffer
	 */
	n_pages = arg.size >> PAGE_SHIFT;
	pages = vmalloc(n_pages * sizeof(struct page *));
	if (!pages) {
		poweren_ep_error("vmalloc for page list failed (%ld bytes)\n",
				n_pages * sizeof(struct page *));
		return -ENOMEM;
	}
	down_read(&current->mm->mmap_sem);
	rc = get_user_pages(current, current->mm, arg.uaddr, n_pages, 1, 0,
			pages, NULL);
	up_read(&current->mm->mmap_sem);
	if (rc != n_pages) {
		poweren_ep_error("get_user_pages failure %d != %d\n",
				rc, n_pages);
		goto wemub_err_out;
	}

	/*
	 * create scatter/gather list from pages associated with user buffer
	 */
	sg = vmalloc(n_pages * sizeof(struct scatterlist));
	if (!sg) {
		poweren_ep_error("vmalloc for sg list failed(%ld bytes)\n",
				n_pages * sizeof(struct scatterlist));
		rc = -ENOMEM;
		goto wemub_err_out;
	}
	sg_init_table(sg, n_pages);
	for (i = 0; i < n_pages; i++)
		sg_set_page(&(sg[i]), pages[i], PAGE_SIZE, 0);

	vfree(pages);
	pages = NULL;

	rc = poweren_ep_map_mem_sg(vf, sg, n_pages);
	if (rc) {
		poweren_ep_error("poweren_ep_map_mem_sg failed\n");
		goto wemub_err_out;
	}

	arg.daddr = (u64) sg_dma_address(sg);

	/*
	 * add to list of mappings tracked by driver
	 */
	rc = poweren_ep_add_dma_list((void *)arg.uaddr, arg.size, sg);
	if (rc) {
		poweren_ep_error("Error adding map entry to list!!!\n");
		goto wemub_err_out;
	}

	poweren_ep_debug("poweren_ep_add_dma_list completed."
			" Trying copy_to_user...\n");

	/*
	 * success! copy arg structure containing DMA address to user
	 */
	rc = copy_to_user(uptr, &arg, sizeof(struct ep_reg_mem));
	if (rc) {
		/* argh!!! */
		poweren_ep_error("copy_to_user failed!\n");
		rc = -EFAULT;
		goto wemub_err_out;
	}

	poweren_ep_debug("Mapped user buf for DMA: uaddr %p,"
			" daddr %p, pid %d\n", (void *)arg.uaddr,
			(void *)arg.daddr, current->tgid);

	goto wemub_out;

wemub_err_out:

	poweren_ep_debug("Error in mapping user-space buffer.. Cleaning up\n");
	/*
	 * pages are mapped and we may have added entry to lists
	 */
	if (arg.daddr) {
		struct poweren_ep_dma_map *me;

		me = poweren_ep_del_dma_list((void *)arg.uaddr);
		if (me) {
			/* note me->sg == sg and will be free'ed below */
			kfree(me);
		}

		if (rc)
			poweren_ep_unmap_mem_sg(vf, sg, n_pages);
	}

	/*
	 * called get_user_pages, so must put each page
	 */
	if (sg) {
		for (i = 0; i < n_pages; i++)
			put_page(sg_page(&(sg[i])));

		vfree(sg);
	}

	if (pages)
		vfree(pages);

wemub_out:
	return rc;
}

int poweren_ep_unmap_udma_buf(struct poweren_ep_vf *vf, unsigned long uarg)
{
	void __user *uptr;
	u64 u_addr;
	struct poweren_ep_dma_map *me;
	int i, n_pages;

	uptr = (void __user *)uarg;
	if (copy_from_user(&u_addr, uptr, sizeof(u64)))
		return -EFAULT;

	/*
	 * search for mapping in lists
	 */
	me = poweren_ep_del_dma_list((void *)u_addr);
	if (!me)
		return -EINVAL;

	poweren_ep_debug("Unmapping user buf for DMA: uaddr %p, "
			"size %llx, pid %d\n",
			(void *)me->v_addr, me->size, me->pid);
	/*
	 * put all pages in scatter/gather list
	 */
	n_pages = me->size >> PAGE_SHIFT;
	poweren_ep_unmap_mem_sg(vf, me->sg, n_pages);
	for (i = 0; i < (me->size >> PAGE_SHIFT); i++)
		put_page(sg_page(&(me->sg[i])));

	vfree(me->sg);
	kfree(me);

	return 0;
}

int poweren_ep_dma_init(struct poweren_ep_vf *vf)
{
	INIT_LIST_HEAD(&poweren_ep_dma_map_list);

	poweren_ep_debug("Dma map list is at: %p\n",
			(void *)&poweren_ep_dma_map_list);

	return 0;
}

void poweren_ep_dma_fini(struct poweren_ep_vf *vf)
{
	struct poweren_ep_dma_map *pe, *tpe, *me, *tme;
	int i;

	/*
	 * no need for locking, we are going away.
	 * simply traverse the lists and unmap/free each entry
	 */
	list_for_each_entry_safe(pe, tpe, &poweren_ep_dma_map_list, procs) {
		/*
		 * for each proc, traverse list of per-proc maps
		 */
		list_for_each_entry_safe(me, tme, &pe->maps, maps) {
			poweren_ep_unmap_mem_sg(vf, me->sg,
					me->size >> PAGE_SHIFT);
			for (i = 0; i < (me->size >> PAGE_SHIFT); i++)
				put_page(sg_page(&(me->sg[i])));
			vfree(me->sg);

			list_del(&me->maps);
			kfree(me);
		}

		/*
		 * don't forget entry anchoring proc
		 */
		poweren_ep_unmap_mem_sg(vf, pe->sg,
				pe->size >> PAGE_SHIFT);
		for (i = 0; i < (pe->size >> PAGE_SHIFT); i++)
			put_page(sg_page(&(pe->sg[i])));
		vfree(pe->sg);

		list_del(&pe->procs);
		kfree(pe);
	}

	return;
}
