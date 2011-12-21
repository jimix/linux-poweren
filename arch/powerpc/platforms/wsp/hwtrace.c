/*
 * Copyright 2010,2011 Benjamin Herrenschmidt, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/gfp.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <asm/scom.h>
#include <asm/cache.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>
#include <asm/uaccess.h>
#include <asm/reg.h>
#include <asm/reg_a2.h>
#include <asm/poweren_hwtrace.h>
#include <asm/io.h>

#include "wsp.h"

/*
 * Register definitions, offsets are with the nHTM register block defined
 * in the device_tree. We only document the 'NHTM Fabric Trace' mode.
 */

#define NHTM_MODE			0
#define NHTM_MEM			1
#define NHTM_STAT			2
#define NHTM_LAST			3
#define NHTM_TRIG			4
#define NHTM_CTRL			5
#define NHTM_FILT			6

/* Memory controller register definitions */
#define MCS_MCMODE			0x9
#define MCS_MCMODE_BLOCK_HANG			0x4000000000000000ull
#define MCS_MCCLMSK			0xa
#define MCS_MCCLMSK_HTM_CMD_MSK_MASK		0x0000000000fff000ull
#define MCS_MCCLMSK_HTM_CMD_MSK_VAL		0x0000000000f00000ull


/* TTAG pattern: 0:2  Node ID
 *               3:5  Chip ID
 *               6:11 Unit ID
 */

/* Max 1 nHTM per chip and max 4 chips */
#define WSP_HTM_MAX_DEVS	4

struct wsp_htm_mc {
	scom_map_t		map;
	u64			mcmode;
	u64			mcclmsk;
};

struct wsp_htm {
	struct device_node	*node;
	scom_map_t		map;
	struct wsp_htm_mc	mcs[2];
	struct page		*pages;
	unsigned long		phys_addr;
	unsigned long		size;
	void __iomem		*kmap;
	dev_t			htm_dev;
	int			enabled;
	struct mutex		lock;
	struct cdev		htm_cdev;
	struct device		*htm_device;
};

static struct wsp_htm *wsp_htms[WSP_HTM_MAX_DEVS];
static int wsp_htm_major;
static struct class *wsp_htm_class;

static void wsp_htm_setup_core(void *data)
{
	int enable = *(int *)data;
	u32 ccr2;

	ccr2 = mfspr(SPRN_A2_CCR2) & ~A2_CCR2_ENABLE_TRACE;
	if (enable)
		ccr2 |= A2_CCR2_ENABLE_TRACE;
	mtspr(SPRN_A2_CCR2, ccr2);
	mtspr(SPRN_XUCR0, mfspr(SPRN_XUCR0) |
	      XUCR0_TRACE_UM_T0 |
	      XUCR0_TRACE_UM_T1 |
	      XUCR0_TRACE_UM_T2 |
	      XUCR0_TRACE_UM_T3);
	asm volatile("sync; isync;\n" : : : "memory");
}

static void wsp_htm_init_hw(struct wsp_htm *htm,
			    struct wsp_htm_enable_args *args)
{
	/* Defaults for various registers */
	u64	htm_mode = NHTM_MODE_WRAP_MODE |
			   NHTM_MODE_CAPTURE_CRESP_IGNORE;

	/* We set markers to be fwded to the PBUS and captured in the trace
	 * but unlike the doco example, we -do- accept all triggers so
	 * the TRACE SPR can be used as one. XXX Is that necessary ? --BenH.
	 */
	u64	htm_ctrl = NHTM_CTRL_TRIGGER_FWD_OFF_CTRL_ALL |
		           NHTM_CTRL_MARKER_FWD_ON_TRACE_GLOBAL;

	/* Configure filters, according to doco, ie, an invalid ttag
	 * so we only see Pmisc
	 */
	u64	htm_filt =  (0x038ull << NHTM_FILT_TTAG_PATTERN_SHIFT) |
			    (0x003ull << NHTM_FILT_TTAG_MASK_SHIFT);
	u64	reg;
	int	i;

	/* Apply user arguments if needed */
	if (args) {
		htm_mode = args->htm_mode & NHTM_MODE_USER_MASK;
		htm_ctrl = args->htm_ctrl;
		htm_filt = args->htm_filt;
	}

	/* Make sure we are sane, reset the bloody thing */
	scom_write(htm->map, NHTM_MODE, 0);

	/* Doco says, enable it first */
	scom_write(htm->map, NHTM_MODE, htm_mode | NHTM_MODE_ENABLE);

	/* Configure */
	scom_write(htm->map, NHTM_CTRL, htm_ctrl);
	scom_write(htm->map, NHTM_FILT, htm_filt);

	/* Clear memory enable (and everything else in there) */
	scom_write(htm->map, NHTM_MEM, 0);

	/* Configure memory */
	reg = ((htm->size - 1) >> 9) & NHTM_MEM_SIZE_MASK;
	scom_write(htm->map, NHTM_MEM,
		   htm->phys_addr | reg | NHTM_MEM_TRACE_MEM_ALLOCATED);

	/* Reset trigger to start memory pre-request */
	scom_write(htm->map, NHTM_TRIG, NHTM_TRIG_RESET);

	/* Wait for ready */
	for (i = 0; i < 1000; i++) {
		reg = scom_read(htm->map, NHTM_STAT);
		if (reg & NHTM_STAT_READY_STATE)
			break;
		msleep(1);
	}
	if (!(reg & NHTM_STAT_READY_STATE)) {
		pr_err("wsp_htm: HTM HW init timeout, state=0x%llx !\n", reg);
		scom_write(htm->map, NHTM_MODE, 0);
		return;
	}
}

static int wsp_htm_wait_complete(struct wsp_htm *htm)
{
	u64 reg;
	int i;

	for (i = 0; i < 1000; i++) {
		reg = scom_read(htm->map, NHTM_STAT);
		if (reg & NHTM_STAT_READY_STATE)
			break;
		msleep(1);
	}

	if (reg & NHTM_STAT_COMPLETED_STATE)
		return 0;
	else
		return -EAGAIN;
}


static void wsp_htm_setup_one_mc(struct wsp_htm_mc *mc, int enable)
{
	u64 mcmode, mcclmsk;

	if (enable) {
		mc->mcmode = scom_read(mc->map, MCS_MCMODE);
		mc->mcclmsk = scom_read(mc->map, MCS_MCCLMSK);
		mcmode = mc->mcmode & ~MCS_MCMODE_BLOCK_HANG;
		mcclmsk = mc->mcclmsk & ~MCS_MCCLMSK_HTM_CMD_MSK_MASK;
		mcclmsk |= MCS_MCCLMSK_HTM_CMD_MSK_VAL;
	} else {
		mcmode = mc->mcmode;
		mcclmsk = mc->mcclmsk;
	}
	scom_write(mc->map, MCS_MCMODE, mcmode);
	scom_write(mc->map, MCS_MCCLMSK, mcclmsk);
}

static void wsp_htm_setup_mcs(struct wsp_htm *htm, int enable)
{
	if (scom_map_ok(htm->mcs[0].map))
		wsp_htm_setup_one_mc(&htm->mcs[0], enable);
	if (scom_map_ok(htm->mcs[1].map))
		wsp_htm_setup_one_mc(&htm->mcs[1], enable);
}

static void wsp_htm_enable(struct wsp_htm *htm,
			   struct wsp_htm_enable_args *args)
{
	mutex_lock(&htm->lock);

	/* Setup the memory controllers */
	wsp_htm_setup_mcs(htm, 1);

	/* Setup the HW */
	wsp_htm_init_hw(htm, args);

	htm->enabled = 1;

	mutex_unlock(&htm->lock);
}

static void wsp_htm_disable(struct wsp_htm *htm)
{
	/* This may be called from the driver remove so double
	 * check that we are indeed enabled
	 */
	if (!htm->enabled)
		return;

	mutex_lock(&htm->lock);

	/* Disable the whole thing */
	scom_write(htm->map, NHTM_MEM, 0);
	msleep(10);
	scom_write(htm->map, NHTM_MODE, 0);

	/* Not sure hoe long it takes for memory writes to
	 * complete so wait a bit more...
	 */
	msleep(10);

	/* Disable HTM specific bits in memory controller */
	wsp_htm_setup_mcs(htm, 0);

	htm->enabled = 0;

	mutex_unlock(&htm->lock);
}


static int wsp_htm_get_status(struct wsp_htm *htm,
			      struct wsp_htm_status __user *arg)
{
	int	rc;
	u64	laddr;

	rc = put_user(scom_read(htm->map, NHTM_STAT), &arg->stat);
	laddr = scom_read(htm->map, NHTM_LAST);
	rc |= put_user(laddr & (htm->size - 1), &arg->last_offset);
	if (rc)
		return -EFAULT;
	return 0;
}

static long wsp_htm_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	struct wsp_htm *htm = filp->private_data;

	switch(cmd) {
	case WSP_HTM_IOC_API_VERSION:
		return WSP_HTM_API_VERSION;
	case WSP_HTM_IOC_ENABLE:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (htm->enabled)
			return -EBUSY;
		wsp_htm_enable(htm, (struct wsp_htm_enable_args *)arg);
		break;
	case WSP_HTM_IOC_DISABLE:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (!htm->enabled)
			return -EACCES;
		wsp_htm_disable(htm);
		break;
	case WSP_HTM_IOC_START:
		if (!htm->enabled)
			return -EACCES;
		scom_write(htm->map, NHTM_TRIG, NHTM_TRIG_START);
		break;
	case WSP_HTM_IOC_STOP:
		if (!htm->enabled)
			return -EACCES;
		scom_write(htm->map, NHTM_TRIG, NHTM_TRIG_STOP);
		break;
	case WSP_HTM_IOC_GET_ORDER:
		return get_order(htm->size) + PAGE_SHIFT;
	case WSP_HTM_IOC_WAIT_COMPLETE:
		if (!htm->enabled)
			return -EACCES;
		return wsp_htm_wait_complete(htm);
	case WSP_HTM_IOC_RESET:
		if (!htm->enabled)
			return -EACCES;
		scom_write(htm->map, NHTM_TRIG, NHTM_TRIG_RESET);
		break;
	case WSP_HTM_IOC_GET_STATUS:
		if (!htm->enabled)
			return -EACCES;
		return wsp_htm_get_status(htm,
					  (struct wsp_htm_status __user *)arg);
	default:
		return -ENOSYS;
	}
	return 0;
}

static ssize_t wsp_htm_read(struct file *filp, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct wsp_htm *htm = filp->private_data;
	loff_t p = *ppos;
	void *tmp;
	size_t orig_count = count;

	if (!htm->kmap)
		return -ENXIO;
	if (p >= htm->size)
		return 0;
	if ((p + count) > htm->size)
		count = htm->size - p;
	tmp = (void *)__get_free_page(GFP_KERNEL);
	while(count) {
		size_t chunk = min(count, (size_t)PAGE_SIZE);
		printk("tmp=%p, kmap=%p, pos=%llx, chunk=%lx\n",
		       tmp, htm->kmap, p, chunk);
		memcpy_fromio(tmp, htm->kmap + p, chunk);
		if (copy_to_user(buf, tmp, chunk)) {
			free_page((unsigned long)tmp);
			return -EFAULT;
		}
		count -= chunk;
		p += chunk;
		buf += chunk;
	}
	*ppos = p;
	return orig_count;
}

static int wsp_htm_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long size   = vma->vm_end - vma->vm_start;
	struct wsp_htm *htm = filp->private_data;

	vma->vm_page_prot = pgprot_noncached_wc(vma->vm_page_prot);

	if (size > htm->size)
		return -EINVAL;

	/* XXX Maybe handle offsets inside the HTM memory ? */
	if (io_remap_pfn_range(vma, vma->vm_start, htm->phys_addr >> PAGE_SHIFT,
			       size, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

static int wsp_htm_open(struct inode * inode, struct file * filp)
{
	struct cdev *cdev = inode->i_cdev;
	struct wsp_htm *htm = container_of(cdev, struct wsp_htm, htm_cdev);

	filp->private_data = htm;

	return 0;
}

static const struct file_operations wsp_htm_fops = {
	.owner	= THIS_MODULE,
	.mmap	= wsp_htm_mmap,
	.open	= wsp_htm_open,
	.read	= wsp_htm_read,
	.unlocked_ioctl = wsp_htm_ioctl,
	.compat_ioctl = wsp_htm_ioctl,
};

static ssize_t wsp_htm_size_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct wsp_htm *htm = dev_get_drvdata(dev);

	return sprintf(buf, "0x%lx\n", htm->size);
}

static ssize_t wsp_htm_state_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct wsp_htm *htm = dev_get_drvdata(dev);

	return sprintf(buf, "0x%llx\n", scom_read(htm->map, NHTM_STAT));
}

static ssize_t wsp_htm_last_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct wsp_htm *htm = dev_get_drvdata(dev);

	return sprintf(buf, "0x%llx\n", scom_read(htm->map, NHTM_LAST));
}

static struct device_attribute wsp_htm_dev_attrs[] = {
	__ATTR(size, S_IRUGO, wsp_htm_size_show, NULL),
	__ATTR(state, S_IRUGO,wsp_htm_state_show, NULL),
	__ATTR(last, S_IRUGO, wsp_htm_last_show, NULL),
	__ATTR_NULL
};

static int wsp_htm_init_mem_fw(struct wsp_htm *htm, int nid)
{
	struct device_node *xscom;
	u64 start, size;
	const u64 *reg;

	/* XXX FIXME: We cannot use of_address_to_resource() as the
	 * reg property points to a region of RAM which isn't something
	 * that gets translated through the "ranges" in the wsp node
	 *
	 * This workaround makes assumptions on the format of said
	 * "reg" property though
	 */
	reg = of_get_property(htm->node, "reg", NULL);
	if (!reg) {
		pr_err("wsp_htm: Cannot find reserved memory for %s\n",
		       htm->node->full_name);
		return -ENXIO;
	}
	start = reg[0];	/* XXX FIXME: Use #address-cells */
	size = reg[1];	/* XXX FIXME: Use #size-cells */
	if (size == 0) {
		pr_err("wsp_htm: Empty reserved memory for %s\n",
		       htm->node->full_name);
		return -ENXIO;
	}

	pr_info("wsp_htm: 0x%016llx..0x%016llx reserved for node %d\n",
		start, start + size - 1, nid);

	htm->phys_addr	= start;
	htm->size	= size;
	htm->pages	= NULL;

	/* Find the memory controller(s) */

	/* XXX Todo: get help from FW. Current FW only exposes one
	 * MC node and not the right range of SCOM registers so
	 * we have to hard wire
	 */
	xscom = scom_find_parent(htm->node);
	BUG_ON(!xscom);
	htm->mcs[0].map = scom_map(xscom, 0x02010800, 0x10);
	htm->mcs[1].map = scom_map(xscom, 0x02010c00, 0x10);
	of_node_put(xscom);

	return 0;
}

static int wsp_htm_init_global(void)
{
	dev_t dev = MKDEV(0, 0);
	int rc;

	wsp_htm_class = class_create(THIS_MODULE, "wsp_htm");
	if (IS_ERR(wsp_htm_class)) {
		pr_err("class_create() failed for wsp_hwclass\n");
		wsp_htm_class = NULL;
		return -ENXIO;
	}
	wsp_htm_class->dev_attrs = wsp_htm_dev_attrs;

	rc = alloc_chrdev_region(&dev, 0, WSP_HTM_MAX_DEVS, "wsp_htm");
	if (rc < 0) {
		pr_err("wsp_htm: Failed get major number\n");
		return rc;
	}
	wsp_htm_major = MAJOR(dev);

	/* Enable trace SPR on core */
	rc = 1;
	on_each_cpu(wsp_htm_setup_core, &rc, 1);

	return 0;
}

static int __devinit wsp_htm_probe(struct platform_device *pdev)
{
	struct wsp_htm	*htm = NULL;
	struct device_node *np = pdev->dev.of_node;
	int rc, nid = of_node_to_nid(np);
	int dev_added = 0;

	if (wsp_htm_major == 0 && wsp_htm_init_global())
		return -ENXIO;

	/* Check & sanitize node ID */
	if (nid == -1)
		nid = 0;
	if (nid >= WSP_HTM_MAX_DEVS) {
		pr_err("wsp_htm: Node ID out of range (%d) for %s\n",
		       nid, np->full_name);
		goto err;
	}
	if (wsp_htms[nid]) {
		pr_err("wsp_htm: Duplicate HTM for node %d: %s and %s\n",
		       nid, wsp_htms[nid]->node->full_name, np->full_name);
		goto err;
	}

	/* Alloc our instance structure and setup the cdev */
	htm = kzalloc(sizeof(struct wsp_htm), GFP_KERNEL);
	if (!htm) {
		pr_err("wsp_htm: Cannot allocate HTM data for %s\n",
		       np->full_name);
		goto err;
	}
	htm->node = of_node_get(np);
	htm->htm_dev = MKDEV(wsp_htm_major, nid);
	htm->mcs[0].map = htm->mcs[1].map = htm->map = SCOM_MAP_INVALID;
	mutex_init(&htm->lock);

	/* Map the SCOM registes */
	htm->map = scom_map_device(np, 0);
	if (!scom_map_ok(htm->map)) {
		pr_err("wsp_htm: Failed to map SCOM registers for %s\n",
		       np->full_name);
		goto err;
	}

	rc = wsp_htm_init_mem_fw(htm, nid);
	if (rc < 0)
		goto err;

	/* Map it */
#if 0 /* XXX FIXME: Something is not right with ioremap_prot */
	htm->kmap = ioremap_prot(htm->phys_addr, htm->size,
				 pgprot_noncached_wc(PAGE_KERNEL));
#else
	htm->kmap = ioremap(htm->phys_addr, htm->size);
#endif
	if (htm->kmap == NULL) {
		pr_warning("wsp_htm: Failed to map trace buffer in kernel !");
		pr_warning("wsp_htm: read() operations disabled for %s",
			   np->full_name);
	}

	/* Setup the char device instance */
	cdev_init(&htm->htm_cdev, &wsp_htm_fops);
	rc = cdev_add(&htm->htm_cdev, htm->htm_dev, 1);
	if (rc < 0)
		goto err;
	dev_added = 1;

	wsp_htms[nid] = htm;

	/* Create a class device */
	htm->htm_device = device_create(wsp_htm_class, NULL, htm->htm_dev,
					htm, "nhtm%d", nid);
	if (!htm->htm_device) {
		pr_err("wsp_htm: Failed to create sysfs device !");
		goto err;
	}

	/* Set driver data on OF device */
	dev_set_drvdata(&pdev->dev, htm);

	return 0;
 err:
	if (htm->kmap)
		iounmap(htm->kmap);
	if (dev_added)
		cdev_del(&htm->htm_cdev);
	scom_unmap(htm->mcs[0].map);
	scom_unmap(htm->mcs[1].map);
	scom_unmap(htm->map);
	if (htm)
		of_node_put(htm->node);
	if (htm && htm->pages)
		__free_pages(htm->pages, get_order(htm->size));
	kfree(htm);
	return -ENXIO;
}

static int __devexit wsp_htm_remove(struct platform_device *pdev)
{
	struct wsp_htm *htm = dev_get_drvdata(&pdev->dev);

	/* Make sure the HTM is disabled */
	wsp_htm_disable(htm);

	/* Unmap the kernel mapping */
	iounmap(htm->kmap);

	/* Remove the sysfs class dev */
	device_del(htm->htm_device);

	/* Remove the char dev */
	cdev_del(&htm->htm_cdev);

	/* If necessary, restore the MC */
	scom_unmap(htm->mcs[0].map);
	scom_unmap(htm->mcs[1].map);

	/* Unmap SCOM regs */
	scom_unmap(htm->map);

	/* Free memory if we allocated it */
	if (htm->pages)
		__free_pages(htm->pages, get_order(htm->size));
	of_node_put(htm->node);
	kfree(htm);

	return 0;
}

static const struct of_device_id wsp_htm_device_id[] = {
	{ .compatible	= NHTM_COMPATIBLE },
	{}
};

static struct platform_driver wsp_htm_driver = {
	.probe		= wsp_htm_probe,
	.remove		= wsp_htm_remove,
	.driver		= {
		.name	= "wsp_htm",
		.owner	= THIS_MODULE,
		.of_match_table = wsp_htm_device_id,
	},
};

static int __init wsp_htm_init(void)
{
	return platform_driver_register(&wsp_htm_driver);
}

static void __exit wsp_htm_exit(void)
{
	int rc;

	/* Remove all instances */
	platform_driver_unregister(&wsp_htm_driver);

	/* Free the char device region */
	if (wsp_htm_major != 0) {
		dev_t dev = MKDEV(wsp_htm_major, 0);
		unregister_chrdev_region(dev, WSP_HTM_MAX_DEVS);
	}

	/* Free the class */
	if (wsp_htm_class)
		class_destroy(wsp_htm_class);

	/* Disable trace SPR on core */
	rc = 0;
	on_each_cpu(wsp_htm_setup_core, &rc, 1);
}

module_init(wsp_htm_init);
module_exit(wsp_htm_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
