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

#include <linux/version.h>
#include <linux/delay.h>

#include "poweren_ep_vf.h"
#include "poweren_ep_mm.h"
#include "poweren_ep_sm.h"
#include "poweren_ep_driver.h"

/* PowerEN PCIe device driver ids */
#define VENDOR_ID		0x1014
#define DEVICE_ID		0x03b7

/* Endpoint Control Register Constants */
#define ENABLE_MBX_INT		0x1000000000000000ull
#define ENABLE_SFW_INT		0x0800000000000000ull
#define ENABLE_DMA_INT		0x0400000000000000ull
#define ENABLE_DMA		0x0100000000000000ull

/* Init defines */
#define INIT_RETRIES		100

/**
 * struct poweren_ep_user_regs - PCIe Endpoint Register Offsets (BAR 0/1)
 * @in_mbx:			Inbound Mailbox
 * @reserved:			0x048 - 0x100
 * @out_mbx:			Outbound Mailbox
 * @reserved:			0x150 - 0x200
 * @inc_dec_doorbell_reg:	Increment/Decrement Doorbell Register
 * @overload_doorbell_reg:	Overload Doorbell Register
 * @reserved:			0x210 - 0x300
 * @hirs:			PCIe Host Interface Registers
 *				0 - 7  | rw from host | rw from WSP dev
 *				8 - 15 | r  from host |  w from WSP dev
 * @reserved:			0x380 - 0xf00
 * @endpoint_ctrl_reg		Endpoint Control Register
 * @dma_status_reg		DMA Status Register
 * @interrupts_clear_reg	PCIe Interrupts Clear Register
 * @reserved:			0xf18 - 0x1000
 */

struct poweren_ep_user_regs {
	struct poweren_ep_in_mbx in_mbx;
	RESERVED(0x048, 0x100);
	struct poweren_ep_out_mbx out_mbx;
	RESERVED(0x150, 0x200);
	u64 inc_dec_doorbell_reg;
	u64 overload_doorbell_reg;
	RESERVED(0x210, 0x300);
	struct poweren_ep_hirs hirs;
	RESERVED(0x380, 0xf00);
	u64 endpoint_ctrl_reg;
	u64 dma_status_reg;
	u64 interrupts_clear_reg;
	RESERVED(0xf18, 0x1000);
};

static unsigned count;
static struct poweren_ep ep_dev[TOTAL_FUNCS];

void poweren_ep_write_hir(int index, u64 value)
{
	writeq(value, &ep_dev->hirs->buf[index]);
}
EXPORT_SYMBOL_GPL(poweren_ep_write_hir);

u64 poweren_ep_read_hir(int index)
{
	return readq(&ep_dev->hirs->buf[index]);
}
EXPORT_SYMBOL_GPL(poweren_ep_read_hir);

struct msix_entry *poweren_ep_get_msix_entries(struct poweren_ep_vf *vf)
{
	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return 0;
	}

	return ep_dev[vf->vf_num].msix_entries;
}
EXPORT_SYMBOL_GPL(poweren_ep_get_msix_entries);

struct pci_dev *poweren_ep_get_pdev(struct poweren_ep_vf *vf)
{
	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return 0;
	}

	return ep_dev[vf->vf_num].pdev;
}
EXPORT_SYMBOL_GPL(poweren_ep_get_pdev);

struct iommu_domain *poweren_ep_get_iommu_dom(struct poweren_ep_vf *vf)
{
	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return 0;
	}

	return ep_dev[vf->vf_num].iommu_dom;
}
EXPORT_SYMBOL_GPL(poweren_ep_get_iommu_dom);

struct poweren_ep_slab_map *poweren_ep_get_slab_map(struct poweren_ep_vf *vf)
{
	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return 0;
	}

	return ep_dev[vf->vf_num].mem_map;
}
EXPORT_SYMBOL_GPL(poweren_ep_get_slab_map);

struct poweren_ep_mbx_regs *poweren_ep_get_mbx(struct poweren_ep_vf *vf)
{
	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return 0;
	}

	if (vf->vf_num == SERIAL_CONSOLE_FN)
		return 0;

	return &ep_dev[vf->vf_num].mbx_regs;
}
EXPORT_SYMBOL_GPL(poweren_ep_get_mbx);

u64 *poweren_ep_get_inc_dec_doorbell(struct poweren_ep_vf *vf)
{
	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return 0;
	}

	return ep_dev[vf->vf_num].inc_dec_doorbell_reg;
}
EXPORT_SYMBOL_GPL(poweren_ep_get_inc_dec_doorbell);

u64 *poweren_ep_get_overload_doorbell(struct poweren_ep_vf *vf)
{
	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return 0;
	}

	return ep_dev[vf->vf_num].overload_doorbell_reg;
}
EXPORT_SYMBOL_GPL(poweren_ep_get_overload_doorbell);

static struct pci_device_id poweren_ep_ids[] __devinitdata = {
	{VENDOR_ID, DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{},
};

static int __devinit poweren_ep_probe(struct pci_dev *pdev,
		const struct pci_device_id *pci_dev_id);
static void __devexit poweren_ep_remove(struct pci_dev *pdev);

static struct pci_driver poweren_ep_driver = {
	.name = DRV_NAME,
	.id_table = poweren_ep_ids,
	.probe = poweren_ep_probe,
	.remove = poweren_ep_remove,
};

static int poweren_ep_iommu_init(struct pci_dev *pdev)
{
	u32 fn;
	int err;

	fn = PCI_FUNC(pdev->devfn);

	ep_dev[fn].mem_map = poweren_ep_domain_mem_map_alloc();
	if (!ep_dev[fn].mem_map) {
		poweren_ep_error("domain map not allocated correctly\n");
		err = -ENOMEM;
		goto map_alloc_failed;
	}

	ep_dev[fn].iommu_dom = iommu_domain_alloc();
	if (!ep_dev[fn].iommu_dom) {
		poweren_ep_error("iommu domain for fn %u was not allocated"
				" correctly\n", fn);
		err = -ENOMEM;
		goto dom_alloc_failed;
	}
	poweren_ep_debug("allocated the iommu domain for fn %u\n", fn);

	err = iommu_attach_device(ep_dev[fn].iommu_dom, &pdev->dev);

	if (err) {
		poweren_ep_error("Error attaching fn %u to the iommu domain."
				"Error code: %d\n", fn, err);
		goto attach_failed;
	}
	poweren_ep_debug("Attached pf %d to the iommu domain\n", fn);

	return 0;

attach_failed:
	iommu_domain_free(ep_dev[fn].iommu_dom);
dom_alloc_failed:
	poweren_ep_domain_mem_map_free(ep_dev[fn].mem_map);
map_alloc_failed:
	return err;
}

static void poweren_ep_iommu_free(struct pci_dev *pdev)
{
	u32 fn;

	fn = PCI_FUNC(pdev->devfn);

	iommu_detach_device(ep_dev[fn].iommu_dom, &pdev->dev);
	iommu_domain_free(ep_dev[fn].iommu_dom);
	poweren_ep_domain_mem_map_free(ep_dev[fn].mem_map);
}

static int __devinit poweren_ep_probe(struct pci_dev *pdev,
		const struct pci_device_id *pci_dev_id)
{
	u64 rc;
	int i, err;
	u32 fn;
	unsigned long base;
	struct poweren_ep_user_regs *uregs;

	/* Saving function number */
	fn = PCI_FUNC(pdev->devfn);

	poweren_ep_debug("probe physical function %u\n", fn);

	/* Enabling pci device */
	if (pci_enable_device(pdev)) {
		poweren_ep_error("unable to enable pci device\n");
		return -ENODEV;
	}

	poweren_ep_debug("device enabled\n");

	/* Setting pci bus master */
	pci_set_master(pdev);

	poweren_ep_debug("pci master enabled\n");

	/* Requesting pci regions */
	if (pci_request_regions(pdev, DRV_NAME)) {
		poweren_ep_error("unable to request pci regions\n");
		return -ENODEV;
	}

	poweren_ep_debug("pci regions requested\n");

	/* Mapping user space registers */
	base = pci_resource_start(pdev, BAR_0_1);
	if (!base) {
		poweren_ep_error("BAR 0/1 addr is %lx\n", base);
		goto bar_failed;
	}

	uregs = pci_ioremap_bar(pdev, BAR_0_1);

	if (!uregs) {
		poweren_ep_error("fail to ioremap BAR 0/1 virtual addr %p",
				uregs);
		goto map_failed;
	}

	/* Enabling MSIx interrupts */
	for (i = 0; i < MAX_MSIX_ENTRIES; ++i)
		ep_dev[fn].msix_entries[i].entry = i;

	err = pci_enable_msix(pdev, ep_dev[fn].msix_entries, MAX_MSIX_ENTRIES);

	if (err) {
		poweren_ep_error("unable to enable msix: %d\n", err);
		goto msix_failed;
	}

	/* Enabling DMA and all interrupts */
	rc = readq(&uregs->endpoint_ctrl_reg);

	poweren_ep_debug("endpoint ctrl: %llx\n", rc);

	writeq(rc | ENABLE_SFW_INT | ENABLE_DMA,
			&uregs->endpoint_ctrl_reg);

	poweren_ep_debug("Enabled sw interrupts\n");
	poweren_ep_debug("Enabled dma\n");

	/* Initializing IOMMU domain*/
	err = poweren_ep_iommu_init(pdev);

	if (err) {
		poweren_ep_error("Error initializing iommu domain on"
				" fn %u: %d\n", fn, err);
		goto iommu_failed;
	}
	poweren_ep_debug("Initialized iommu domain on fn %u\n", fn);

	/* Saving dev info into the global variable */
	ep_dev[fn].hirs = &uregs->hirs;
	ep_dev[fn].mbx_regs.in_mbx = &uregs->in_mbx;
	ep_dev[fn].mbx_regs.out_mbx = &uregs->out_mbx;
	ep_dev[fn].inc_dec_doorbell_reg = &uregs->inc_dec_doorbell_reg;
	ep_dev[fn].overload_doorbell_reg = &uregs->overload_doorbell_reg;
	ep_dev[fn].pdev = pdev;

	/* Saving regs in the pci_dev */
	pci_set_drvdata(pdev, uregs);

	/* Increment functions counter */
	count++;

	return 0;

iommu_failed:
	pci_disable_msix(pdev);
msix_failed:
	iounmap(uregs);
map_failed:
bar_failed:
	pci_release_regions(pdev);
	pci_clear_master(pdev);
	return -ENODEV;
}

static void __devexit poweren_ep_remove(struct pci_dev *pdev)
{
	int fn;
	struct poweren_ep_user_regs *uregs;

	fn = PCI_FUNC(pdev->devfn);

	uregs = pci_get_drvdata(pdev);

	poweren_ep_debug("remove fn %d\n", fn);

	poweren_ep_iommu_free(pdev);

	poweren_ep_debug("freed iommu on fn %d\n", fn);

	pci_disable_msix(pdev);

	poweren_ep_debug("disabled interrupts on fn %d\n", fn);

	iounmap(uregs);

	poweren_ep_debug("unmapped BAR 0/1 on fn %d\n", fn);

	pci_release_regions(pdev);

	poweren_ep_debug("released memory regions on fn %d\n", fn);

	pci_clear_master(pdev);

	poweren_ep_debug("cleared bus master on fn %d\n", fn);
}

static int poweren_ep_init(void)
{
	u64 ack;
	int ret, retries;

	/* Register PowerEN PCI endpoint */
	ret = pci_register_driver(&poweren_ep_driver);

	if (ret < 0) {
		poweren_ep_error("fails registering PowerEN pci endpoint\n");
		return ret;
	}
	poweren_ep_info("registered PowerEN pci endpoint\n");

	if (count != TOTAL_FUNCS) {
		poweren_ep_error("unexpected number of functions."
				" found %u of %u\n", count, TOTAL_FUNCS);
		pci_unregister_driver(&poweren_ep_driver);
		return -ENODEV;
	}

/* TODO: remove the surrounded lines once we know that everything
is working with no sync between host and device */
	retries = INIT_RETRIES;

	do {
		ack = poweren_ep_read_hir(DEVICE_INIT_HIR);
		msleep(INIT_WAIT);
		retries--;
	} while (retries > 0 && ack != INIT_ACK);

	if (retries == 0) {
		poweren_ep_error("device platform driver is not present\n");
		pci_unregister_driver(&poweren_ep_driver);
		return -1;
	}

	poweren_ep_write_hir(HOST_INIT_HIR, INIT_ACK);

	poweren_ep_debug("ack received from the device: %llx\n", ack);
/* TODO: remove the surrounded lines once we know that everything
is working with no sync between host and device */

	ret = poweren_ep_slotmgr_init(ep_dev);

	if (ret) {
		poweren_ep_error("failed slot manager init\n");
		pci_unregister_driver(&poweren_ep_driver);
		return ret;
	}

	poweren_ep_info("init completed\n");

	return 0;
}

static void poweren_ep_exit(void)
{
	poweren_ep_info("exit\n");

	poweren_ep_slotmgr_exit();

	poweren_ep_empty_vf_manager_list();
	poweren_ep_debug("emptied vf manager list\n");

	pci_unregister_driver(&poweren_ep_driver);
	poweren_ep_debug("unregistered pci devices\n");
}

module_init(poweren_ep_init);
module_exit(poweren_ep_exit);

/* Kernel module information */
MODULE_AUTHOR("Michael Barry <mgbarry@linux.vnet.ibm.com>");
MODULE_AUTHOR("Owen Callanan <owencall@linux.vnet.ibm.com>");
MODULE_AUTHOR("Antonino Castelfranco <antonino@linux.vnet.ibm.com>");
MODULE_AUTHOR("Jack Miller <jack@codezen.org>");
MODULE_AUTHOR("Jimi Xenidis <jimix@pobox.com>");
MODULE_VERSION("2.0");
MODULE_DESCRIPTION("PowerEN PCIe Endpoint Device Driver");
MODULE_LICENSE("GPL v2");
