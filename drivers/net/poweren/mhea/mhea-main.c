/*
 * Copyright IBM Corporation. 2011
 * Author:	Massimiliano Meneghin <massimim@ie.ibm.com>
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

#include <linux/spinlock.h>
#include <linux/device.h>

#include <asm/poweren_hea_common_types.h>
#include <platforms/wsp/copro/cop.h>
#include <platforms/wsp/copro/pbic.h>

#include "mhea-interface.h"
#include "mhea.h"
#include "../include/rhea-interface.h"
#include "../include/rhea-poweren.h"

static struct hea_adapter *ap_v;
static unsigned hea_adapter_num;

static int mhea_open(struct inode *inode, struct file *file)
{
	int id_target_adapter;
	struct mhea_p_info *p_info;
	mhea_debug("opening");
	id_target_adapter = 0;

	file->private_data = kzalloc(sizeof(struct mhea_p_info), GFP_KERNEL);
	if (file->private_data == NULL)
		return -1;
	p_info = (struct mhea_p_info *)file->private_data;
	p_info->adapter_id = id_target_adapter;

	mhea_debug("done");
	return 0;
}

/* close function - called when the "file" is closed */
static int mhea_release(struct inode *inode, struct file *file)
{
	mhea_debug("Entering");
	kfree(file->private_data);
	mhea_debug("Leaving");
	return 0;
}

/* read function called when the "file" is read */
static ssize_t mhea_read(struct file *file, char *buf, size_t count,
			 loff_t *ppos)
{
	mhea_debug("");
	return 0;
}

static ssize_t mhea_write(struct file *file, const char *buf, size_t count,
			  loff_t *ppos)
{
	mhea_debug("");
	return 0;
}

static long mhea_ioctl(struct file *file, unsigned int cmd, unsigned long arg_)
{
	void __user *arg;
	struct mhea_p_info *p_info;
	int retval = -EINVAL;
	arg = (void __user *)arg_;
	p_info = (struct mhea_p_info *)file->private_data;
	pr_info("arg pointer %p, pbic %p %p\n", arg,
	       ap_v[p_info->adapter_id].mmu.parent_pbic,
	       ap_v[0].mmu.parent_pbic);

	mhea_debug("IOCTL");

	retval = pbic_map_args_ioctl(ap_v[p_info->adapter_id].mmu.parent_pbic,
				     cmd,  arg);
	return retval;
}

/* define which file operations are supported */
const struct file_operations mhea_fops = {
	.owner = THIS_MODULE,
	.read = mhea_read,
	.write = mhea_write,
	.unlocked_ioctl = mhea_ioctl,
	.compat_ioctl = mhea_ioctl,
	.open = mhea_open,
	.release = mhea_release,
};

/* initialize module */
static int __init mhea_init_module(void)
{
	int i, err;

	mhea_info("starting");

	/* Registering Device */
	i = register_chrdev(MHEA_MAJOR, MHEA_NAME, &mhea_fops);
	if (i != 0)
		return -EIO;

	hea_adapter_num = rhea_adapter_count();

	ap_v = kzalloc(sizeof(struct hea_adapter) * hea_adapter_num,
		       GFP_KERNEL);
	if (ap_v == NULL)
		return -1;

	pr_info("number of adapter %d\n", hea_adapter_num);
	for (i = 0; i < hea_adapter_num; i++) {
		err = rhea_adapter_get(i, &ap_v[i]);
		if (err != 0)
			return err;

		pr_info("pbic pointer %p\n", ap_v[i].mmu.parent_pbic);
	}

	mhea_info("success");
	return 0;
}

/* close and cleanup module */
static void __exit mhea_cleanup_module(void)
{
	mhea_info("");
	kfree(ap_v);
	unregister_chrdev(MHEA_MAJOR, MHEA_NAME);
}

module_init(mhea_init_module);
module_exit(mhea_cleanup_module);

MODULE_AUTHOR("Massimiliano Meneghin");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Driver module to map memory on HEA pbic. "
		   "Based module for the FMA user space allocator");
