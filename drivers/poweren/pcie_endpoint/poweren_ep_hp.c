/*
 *  PowerEN PCIe EP device driver
 *
 *  Copyright 2010, IBM Corp.
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

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/poweren/pcie_endpoint/poweren_ep.h>

#include "poweren_ep_bp.h"
#include "poweren_ep_mr.h"
#include "poweren_ep_sm.h"
#include "poweren_ep_driver.h"

#define WSP_EP_HP_DEF_NAME	"poweren_ep_hp_0"

static dev_t poweren_ep_hp_dev;
static struct poweren_ep_vf *g_vf_dev;
static struct cdev *poweren_ep_hp_cdev;
static struct class *poweren_ep_hp_class;
static struct vf_driver poweren_ep_hp_driver;

/* Input parameters and default values */
static int poweren_ep_hp_fn;
static char *poweren_ep_hp_name = WSP_EP_HP_DEF_NAME;
module_param(poweren_ep_hp_fn, int, S_IRUGO);
module_param(poweren_ep_hp_name, charp, S_IRUGO);

static int poweren_ep_hp_open(struct inode *inode, struct file *file)
{
	poweren_ep_debug("comm \"%s\" pid %d tgid %d\n", current->comm,
				current->pid, current->tgid);
	poweren_ep_debug("Major = %d, Minor = %d\n",
		imajor(inode), iminor(inode));

	poweren_ep_debug("opened  device: %s\n", poweren_ep_hp_name);

	return 0;
}

static int poweren_ep_hp_release(struct inode *inode, struct file *file)
{
	poweren_ep_debug("comm \"%s\" pid %d tgid %d\n", current->comm,
				current->pid, current->tgid);

	poweren_ep_cleanup_mem_reg_by_pid(current->tgid);

	poweren_ep_unmap_dma_by_pid(g_vf_dev, current->tgid);

	poweren_ep_slotmgr_connect_cleanup(g_vf_dev);

	poweren_ep_debug("closed device: %s:\n\n", poweren_ep_hp_name);
	return 0;
}

static int poweren_ep_hp_mmap(struct file *f, struct vm_area_struct *vma)
{
	poweren_ep_debug("comm \"%s\" pid %d tgid %d\n", current->comm,
				current->pid, current->tgid);
	return poweren_ep_slotmgr_mmap(g_vf_dev, f, vma);
}

long poweren_ep_hp_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	unsigned int tgid = current->tgid;

	switch (cmd) {
	case EP_IOCTL_SLOTMGR_CONNECT:
	case EP_IOCTL_SLOTMGR_FIND_SLOT:
		return poweren_ep_ioctl_slotmgr(g_vf_dev, f, cmd, arg);
	case EP_IOCTL_REGISTER_MEM:
		return poweren_ep_ioctl_reg_mem(arg, tgid);
	case EP_IOCTL_DEREGISTER_MEM:
		return poweren_ep_ioctl_dereg_mem(arg);
	case EP_IOCTL_GET_REG_MEM:
		return poweren_ep_ioctl_get_reg_mem(arg);
	case EP_IOCTL_MAP_UDMA_BUF:
		return poweren_ep_map_udma_buf(g_vf_dev, arg);
	case EP_IOCTL_UNMAP_UDMA_BUF:
		return poweren_ep_unmap_udma_buf(g_vf_dev, arg);
	default:
		poweren_ep_error("Unrecognised ioctl code %d\n", cmd);
	}

return -ENOENT;
}

static const struct file_operations poweren_ep_hp_fops = {
	.owner = THIS_MODULE,
	.open = poweren_ep_hp_open,
	.release = poweren_ep_hp_release,
	.mmap = poweren_ep_hp_mmap,
	.unlocked_ioctl = poweren_ep_hp_ioctl,
};

static int poweren_ep_hp_probe(struct poweren_ep_vf *vf_dev)
{
	int err;
	struct device *dev;

	err = alloc_chrdev_region(&poweren_ep_hp_dev, 0, 1, poweren_ep_hp_name);
	if (err) {
		poweren_ep_error("failed to allocate char device region\n");
		goto failed_alloc_region;
	}

	poweren_ep_hp_cdev = cdev_alloc();
	if (!poweren_ep_hp_cdev) {
		poweren_ep_error("failed to allocate char device\n");
		goto failed_alloc_cdev;
	}

	cdev_init(poweren_ep_hp_cdev, &poweren_ep_hp_fops);
	err = cdev_add(poweren_ep_hp_cdev, poweren_ep_hp_dev, 1);
	if (err) {
		poweren_ep_error("failed to add char device\n");
		goto failed_add_cdev;
	}

	poweren_ep_hp_class = class_create(THIS_MODULE, poweren_ep_hp_name);
	if (IS_ERR(poweren_ep_hp_class)) {
		poweren_ep_error("error creating the class for the device\n");
		goto failed_class_create;
	}

	dev = device_create(poweren_ep_hp_class, NULL, poweren_ep_hp_dev,
				"%s", poweren_ep_hp_name);
	if (IS_ERR(dev)) {
		poweren_ep_error("failed to create the device\n");
		goto failed_device_create;
	}

	err = poweren_ep_memreg_setup(vf_dev);
	if (err) {
		poweren_ep_error("failed to setup memory registration\n");
		goto failed_mem_reg;
	}

	g_vf_dev = vf_dev;

	poweren_ep_info("Registered char device %s\n", poweren_ep_hp_name);
	poweren_ep_info("Function = %d, Major = %d, Minor = %d\n",
		poweren_ep_hp_fn, MAJOR(poweren_ep_hp_dev),
		MINOR(poweren_ep_hp_dev));
	return 0;

 failed_mem_reg:
	device_destroy(poweren_ep_hp_class, poweren_ep_hp_dev);
 failed_device_create:
	class_destroy(poweren_ep_hp_class);
 failed_class_create:
	cdev_del(poweren_ep_hp_cdev);
 failed_add_cdev:
 failed_alloc_cdev:
	unregister_chrdev_region(poweren_ep_hp_dev, 1);
 failed_alloc_region:
	return -ENODEV;
}

static void poweren_ep_hp_remove(struct poweren_ep_vf *vf_dev)
{
	poweren_ep_memreg_exit();
	device_destroy(poweren_ep_hp_class, poweren_ep_hp_dev);
	class_destroy(poweren_ep_hp_class);

	if (poweren_ep_hp_cdev)
		cdev_del(poweren_ep_hp_cdev);
	unregister_chrdev_region(poweren_ep_hp_dev, 1);

	poweren_ep_info("Unregistered char device %s\n", poweren_ep_hp_name);
	poweren_ep_info("Function = %d, Major = %d, Minor = %d\n",
		poweren_ep_hp_fn, MAJOR(poweren_ep_hp_dev),
		MINOR(poweren_ep_hp_dev));
}

static int poweren_ep_hp_init(void)
{
	int err;

	poweren_ep_hp_driver.vf_num = poweren_ep_hp_fn;
	poweren_ep_hp_driver.name = poweren_ep_hp_name;
	poweren_ep_hp_driver.probe = poweren_ep_hp_probe;
	poweren_ep_hp_driver.remove = poweren_ep_hp_remove;

	err = poweren_ep_register_vf_driver(&poweren_ep_hp_driver);

	if (err) {
		poweren_ep_error("fail registering \"%s\" driver\n",
					poweren_ep_hp_name);
		return err;
	}

	poweren_ep_info("registered vf driver: \"%s\"\n", poweren_ep_hp_name);
	poweren_ep_info("on function: %d\n", poweren_ep_hp_fn);

	poweren_ep_dma_init(g_vf_dev);

	return 0;
}

static void __exit poweren_ep_hp_exit(void)
{
	poweren_ep_unregister_vf_driver(&poweren_ep_hp_driver);
	poweren_ep_dma_fini(g_vf_dev);

	poweren_ep_info("unregistered vf driver: \"%s\"\n", poweren_ep_hp_name);
	poweren_ep_info("on function: %d\n", poweren_ep_hp_fn);

}

module_init(poweren_ep_hp_init);
module_exit(poweren_ep_hp_exit);

/* Kernel module information */
MODULE_AUTHOR("Michael Barry <mgbarry@linux.vnet.ibm.com>");
MODULE_AUTHOR("Owen Callanan <owencall@linux.vnet.ibm.com>");
MODULE_AUTHOR("Antonino Castelfranco <antonino@linux.vnet.ibm.com>");
MODULE_AUTHOR("Jack Miller <jack@codezen.org>");
MODULE_AUTHOR("Jimi Xenidis <jimix@pobox.com>");
MODULE_VERSION("2.0");
MODULE_DESCRIPTION("PowerEN PCIe EP High Performance Device Driver");
MODULE_LICENSE("GPL v2");
