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

#include "poweren_ep_mr.h"

/* This mutex lock down access to the list of memory areas */
DEFINE_MUTEX(local_mem_lock);

static unsigned local_len;
static struct poweren_ep_mr mr;

static void poweren_ep_memreg_translate_read(struct ep_reg_mem *target,
		struct ep_reg_mem *source)
{
	target->valid = be32_to_cpu(source->valid);
	target->key = be32_to_cpu(source->key);
	target->index = be32_to_cpu(source->index);
	target->size = be64_to_cpu(source->size);
	target->address = be64_to_cpu(source->address);
}

static void poweren_ep_memreg_translate_write(struct ep_reg_mem *target,
		struct ep_reg_mem *source)
{
	target->valid = cpu_to_be32(source->valid);
	target->key = cpu_to_be32(source->key);
	target->index = cpu_to_be32(source->index);
	target->size = cpu_to_be64(source->size);
	target->address = cpu_to_be64(source->address);
}

int poweren_ep_memreg_setup(struct poweren_ep_vf *vf)
{
	int ret;

	mr.vf = vf;

	poweren_ep_debug("dmable memory size %lu\n", MR_ROUNDED_SIZE);
	ret = __poweren_ep_memreg_setup(&mr, MR_ROUNDED_SIZE);

	if (ret) {
		poweren_ep_error("Failed to allocate memreg buffers!\n");
		return ret;
	}

	local_len = 0;

	return 0;
}

void poweren_ep_memreg_exit(void)
{
	__poweren_ep_memreg_exit(&mr, MR_ROUNDED_SIZE);
}

static int poweren_ep_get_reg_mem(struct ep_reg_mem *arg)
{
	struct ep_reg_mem *cur;
	int i;

	for (i = 0; i < MR_MEM_SLOTS; i++) {
		cur = &mr.remote_mem[i];

		/* End of List */
		if (!cur->valid)
			break;

		poweren_ep_debug("Checking remote entry key: %x, index %d,"
				" address: %llx\n",
				cur->key, cur->index, cur->address);

		if (arg->key == cur->key && arg->index == cur->index) {
			arg->valid = 1;
			arg->address = cur->address;
			arg->size = cur->size;
			return 0;
		}
	}
	poweren_ep_debug("Checked %d remote entries, no match!\n", i);

	return -EINVAL;
}

int poweren_ep_ioctl_get_reg_mem(unsigned long uarg)
{
	void __user *uptr;
	struct ep_reg_mem arg;
	int err, ret;

	uptr = (void __user *)uarg;

	err = copy_from_user(&arg, uptr, sizeof(struct ep_reg_mem));
	if (err) {
		poweren_ep_error("error copying user args");
		return -EINVAL;
	}

	if (arg.key == 0) {
		poweren_ep_error("Key must not be 0\n");
		return -EINVAL;
	}

	poweren_ep_memreg_sync_remote(&mr);

	/* convert args to big Endian */
	poweren_ep_memreg_translate_write(&arg, &arg);

	ret = poweren_ep_get_reg_mem(&arg);
	if (ret) {
		arg.valid = 0;
		arg.address = 0;
		arg.size = 0;
	}

	/* convert result to local Endian */
	poweren_ep_memreg_translate_read(&arg, &arg);

	err = copy_to_user(uptr, &arg, sizeof(struct ep_reg_mem));
	if (err)
		poweren_ep_error("error copying user args");

	return ret;
}

static int poweren_ep_reg_mem(struct ep_reg_mem *arg,
		   unsigned int pid)
{
	int i, ret, err;

	if (local_len == MR_MEM_SLOTS) {
		poweren_ep_error("Local memory registrations full.\n");
		return -ENOMEM;
	}

	err = mutex_lock_interruptible(&local_mem_lock);
	if (err) {
		poweren_ep_error("lock not acquired\n");
		return err;
	}

	for (i = 0; i < local_len; i++) {
		if (mr.local_mem[i].key == arg->key &&
		    mr.local_mem[i].index == arg->index) {

			poweren_ep_error("Registration exists!\n");
			poweren_ep_error("pid: %d key: %d index: %d\n",
				     pid, arg->key, arg->index);

			ret = -EEXIST;
			goto done;
		}
	}

	poweren_ep_inc_gencount();

	arg->valid = 1;

	i = local_len;
	memcpy(&mr.local_mem[i], arg, sizeof(*arg));
	mr.local_mem_pid[i] = pid;

	poweren_ep_debug("Registered entry %d.\n", i);
	poweren_ep_debug("key: %d index: %d\n",
		     mr.local_mem[i].key, mr.local_mem[i].index);
	poweren_ep_debug("address: 0x%llx\n", mr.local_mem[i].address);
	poweren_ep_debug("size: %lld\n", mr.local_mem[i].size);
	poweren_ep_debug("pid: %d (should be %d)\n", mr.local_mem_pid[i], pid);

	local_len++;
	poweren_ep_debug("\nLocal entries: %d\n", local_len);

	ret = poweren_ep_memreg_sync_local(&mr);

	poweren_ep_inc_gencount();
 done:
	mutex_unlock(&local_mem_lock);

	return ret;
}

int poweren_ep_ioctl_reg_mem(unsigned long uarg, unsigned int pid)
{
	void __user *uptr;
	struct ep_reg_mem arg;
	int err;

	uptr = (void __user *)uarg;

	err = copy_from_user(&arg, uptr, sizeof(struct ep_reg_mem));

	if (arg.key == 0) {
		poweren_ep_error("Key must not be 0\n");
		return -1;
	}

	poweren_ep_debug("poweren_ep_ioctl_reg_mem pid: %d\n", pid);

	poweren_ep_debug("local_mem %p, local_mem_pid %p",
			mr.local_mem,
			mr.local_mem_pid);

	poweren_ep_debug("remote_mem %p, remote_mem_scratch %p\n",
			mr.remote_mem,
			mr.remote_mem_scratch);
	/*
	 * Need to convert the user-space virtual address to a DMA-able
	 * address - Call back in to host/device driver code.  A side
	 * effect of the call is that the arg.address and arg.size are
	 * overwritten if a match is found.
	 */
	if (poweren_ep_dma_search(&arg))
		/* passed address not known to driver */
		return -EINVAL;

	/* convert args to big Endian */
	poweren_ep_memreg_translate_write(&arg, &arg);

	err = poweren_ep_reg_mem(&arg, pid);

	return err;
}

/* Deregister a single entry, no checks. Must be called with local_mem_lock.
 * Returns 1 if list was compressed, 0 otherwise. */

static int poweren_ep_dereg_i(int i)
{
	int ret;

	ret = 0;

	/* Compress list if not last entry */
	if (i != local_len - 1) {
		memcpy(&mr.local_mem[i],
		       &mr.local_mem[local_len - 1],
		       sizeof(struct ep_reg_mem));
		mr.local_mem_pid[i] =
		    mr.local_mem_pid[local_len - 1];
		ret = 1;
	}

	/* Clear out old / moved entry. */
	memset(&mr.local_mem[local_len - 1], 0,
	       sizeof(struct ep_reg_mem));
	mr.local_mem_pid[local_len - 1] = 0;

	local_len--;

	return ret;
}

static int poweren_ep_dereg_mem(struct ep_reg_mem *arg)
{
	struct ep_reg_mem *cur;
	int err, ret, i;

	ret = -ENOENT;

	err = mutex_lock_interruptible(&local_mem_lock);
	if (err) {
		poweren_ep_error("lock not acquired\n");
		return err;
	}

	poweren_ep_inc_gencount();

	for (i = 0; i < local_len; i++) {
		cur = &mr.local_mem[i];

		if (arg->key != cur->key || arg->index != cur->index)
			continue;

		poweren_ep_dereg_i(i);

		ret = 0;
		break;
	}

	poweren_ep_memreg_sync_local(&mr);

	poweren_ep_inc_gencount();
	mutex_unlock(&local_mem_lock);

	return ret;
}

void poweren_ep_cleanup_mem_reg_by_pid(unsigned int pid)
{
	unsigned int i;

	poweren_ep_debug("Entered poweren_ep_cleanup_mem_reg_by_pid.\n");
	poweren_ep_debug("Local entries: %d\n", local_len);

	mutex_lock(&local_mem_lock);
	poweren_ep_inc_gencount();

	for (i = 0; i < local_len; i++) {
		if (mr.local_mem_pid[i] != pid)
			continue;

		poweren_ep_debug("Found match.\n");
		poweren_ep_debug("pid: %d key: %d index: %d\n",
			     pid, mr.local_mem[i].key,
			     mr.local_mem[i].index);

		/* If list compressed, need to recheck current i */
		if (poweren_ep_dereg_i(i))
			i--;
	}

	poweren_ep_memreg_sync_local(&mr);

	poweren_ep_inc_gencount();
	mutex_unlock(&local_mem_lock);
}

int poweren_ep_ioctl_dereg_mem(unsigned long uarg)
{
	void __user *uptr;
	struct ep_reg_mem arg;
	int ret;

	uptr = (void __user *)uarg;

	ret = copy_from_user(&arg, uptr, sizeof(struct ep_reg_mem));

	if (arg.key == 0) {
		poweren_ep_error("Key must not be 0\n");
		return -1;
	}

	/* convert args to big Endian */
	poweren_ep_memreg_translate_write(&arg, &arg);

	ret = poweren_ep_dereg_mem(&arg);

	return ret;
}
