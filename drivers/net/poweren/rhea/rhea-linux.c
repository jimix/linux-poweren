/*
 * Copyright (C) 2011, 2012 IBM Corporation.
 * Author:	Davide Pasetto <pasetto_davide@ie.ibm.com>
 *			Karol Lynch <karol_lynch@ie.ibm.com>
 *			Kay Muller <kay.muller@ie.ibm.com>
 *			Jimi Xenidis <jimix@watson.ibm.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http:/www.gnu.org/licenses/>.
 */

/*************** Mapping functions ********************/

#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/memory.h>
#include <linux/kexec.h>
#include <linux/mutex.h>
#include <linux/utsname.h>

#include <linux/of_device.h>
#include <linux/of_platform.h>

#include <linux/firmware.h>
#include <asm/wsp.h>

#include <linux/interrupt.h>

#include "rhea-linux.h"
#include "rhea-funcs.h"

void __iomem *rhea_ioremap(const char *name, phys_addr_t addr,
			   unsigned long size)
{
	int rc = 0;
	void __iomem *r;

	if (NULL == name || 0 == size)
		return NULL;

	rc = check_mem_region(addr, size);
	if (rc) {
		rhea_error("memory region %s: 0x%lx with size: 0x%lx "
			   "is not available",
			 name, (ulong)addr, size);
		return NULL;
	}

	request_mem_region(addr, size, name);

	r = ioremap(addr, size);
	rhea_debug("rhea_ioremap: %s: 0x%p 0x%llx 0x%lx", name, r, addr, size);
	return r;
}

void rhea_iounmap(void __iomem *addr, unsigned long phys_addr,
		  unsigned long size)
{
	if (NULL == addr || 0 == size)
		return;

	rhea_debug("rhea_iounmap: %p and 0x%lx and size: 0x%lx", addr,
		   phys_addr, size);
	iounmap(addr);
	release_mem_region(phys_addr, size);
}

/*************** Allocation functions ********************/

void *rhea_alloc(ulong size, unsigned flags)
{
	void *ptr;

	if (0 == size)
		return NULL;

	flags |= GFP_KERNEL;

	ptr = kzalloc(size, flags);

	return ptr;
}

void rhea_free(void *ptr, ulong size)
{
	if (NULL == ptr)
		return;

	kfree(ptr);
}

void *rhea_align_alloc(ulong size, ulong align, unsigned flags)
{
	void *ptr;
	void *ptr_align;
	unsigned size_new;

	if (0 == size || 0 == align)
		return NULL;

	size_new = size + sizeof(void *) + align - 1;

	ptr = rhea_alloc(size_new, flags);
	if (NULL == ptr)
		return ptr;

	/* perform alignment */
	ptr_align = (void *)ALIGN_UP((ulong)ptr, align);

	/* compute address after memory block for old ptr */
	*((void **)((ulong)ptr_align + size + sizeof(void *))) = ptr;

	return ptr_align;
}

void rhea_align_free(void *ptr_align, ulong size)
{
	void *ptr;
	if (NULL == ptr_align || 0 == size)
		return;

	/* get position of pointer */
	ptr = *((void **)((ulong)ptr_align + size + sizeof(void *)));

	/* compute address after memory block for old ptr */
	rhea_free(ptr, size);
}

void *rhea_pages_alloc(ulong size, unsigned flags)
{
	void *ptr;

	flags |= GFP_KERNEL;

	if (GFP_DMA & flags)
		ptr = (void *)__get_dma_pages(flags, get_order(size));
	else
		ptr = (void *)__get_free_pages(flags, get_order(size));

	if (ptr)
		memset(ptr, 0, size);

	return ptr;
}

void rhea_pages_free(void *ptr, ulong size)
{
	if (NULL == ptr)
		return;

	free_pages((ulong)ptr, get_order(size));
}

/*************** Interrupt setup ********************/

int rhea_irq_request(u32 hwirq, hea_irq_handler_t handler,
		     unsigned long irq_flags, const char *devname,
		     void *dev_id)
{
	int virq;

	rhea_debug("=> %s: %d %s %#lx", __func__, hwirq, devname, irq_flags);
	virq = irq_create_mapping(NULL, hwirq);
	if (NO_IRQ == virq)
		goto out;

	rhea_debug("   %s: %d %d", __func__, hwirq, virq);
	if (request_irq
	    (virq, (irq_handler_t) handler, irq_flags, devname, dev_id)) {
		irq_dispose_mapping(virq);
		virq = NO_IRQ;
	}

out:

	rhea_debug("<= %s: %s %d %d %p %#lx %p", __func__, devname, hwirq,
		   virq, handler, irq_flags, dev_id);

	return virq;
}

void rhea_irq_free(unsigned virq, void *dev_id)
{
	rhea_debug("   %s: %d %p", __func__, virq, dev_id);
	free_irq(virq, dev_id);
	irq_dispose_mapping(virq);
}

/*************** Dump Registers ********************/

u64 rhea_reg_print_internal(u64 *addr, const char *fmt, ...)
{
	va_list arg;
	int sz;
	int pad = 16;
	u64 result;

	result = in_be64(addr);

	va_start(arg, fmt);
	sz = vprintk(fmt, arg);
	va_end(arg);

	while (sz < pad) {
		pr_info(" ");
		++sz;
	}

	pr_info("\t[0x%016lx] = 0x%016llx\n", (ulong)addr, result);

	return result;
}

/************* send signal to user process ****************************/

int hea_signal_send(struct hea_process *process, int signal_nr)
{
	int rc = 0;
	struct siginfo sig_info = { 0 };
	struct task_struct *task;

	if (NULL == process || 0 == process->pid)
		return -EINVAL;

	/* get task struct */
	task = (struct task_struct *) process->user_process;
	if (NULL == task)
		return -EINVAL;

	/* specify signal type and other settings */
	sig_info.si_signo = signal_nr;
	sig_info.si_errno = 0;
	sig_info.si_code = SI_USER;
	sig_info.si_pid = process->pid;
	sig_info.si_uid = process->uid;

	rc = kill_pid(get_pid(task_pid(task)), signal_nr, 1);
	if (rc)
		rhea_error("Could not send signal to user process");

	return rc;
}
