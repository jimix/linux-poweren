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

#ifndef __POWEREN_EP_DRIVER_H__
#define __POWEREN_EP_DRIVER_H__

#include <linux/pci.h>
#include <linux/iommu.h>
#include <linux/interrupt.h>
#include <linux/poweren/pcie_endpoint/poweren_ep.h>

#include "poweren_ep_vf.h"

#define DRV_NAME		"poweren_ep"

/* Hardware defines */
#define	DEVICE_INIT_HIR		0
#define	HOST_INIT_HIR		1
#define RW_HIRS			8
#define WO_HIRS			8
#define MAX_HIRS		(RW_HIRS + WO_HIRS)

#define MAX_MBX			8

#define MAX_MSIX_ENTRIES	16

/* Functions defines */
#define SERIAL_CONSOLE_FN	0
#define MAX_PFS			2
#define MAX_VFS			16
#define TOTAL_FUNCS		MAX_PFS
/* The total functions will be greater then MAX_PFS in the future */
/* #define TOTAL_FUNCS		MAX_PFS + MAX_VFS */

/* Init defines */
#define INIT_ACK		0xAABBBBAAAABBBBAA
#define INIT_WAIT		100

/* Log utils */
#define poweren_ep_info(fmt, args...) \
	pr_info(DRV_NAME " - %s: " fmt "", __func__, ## args)

#define poweren_ep_error(fmt, args...) \
	pr_err(DRV_NAME " - error in %s: " fmt "", __func__, ## args)

#define poweren_ep_warn(fmt, args...) \
	pr_warn(DRV_NAME " - warning in %s: " fmt "", __func__, ## args)

#define poweren_ep_debug(fmt, args...) \
	pr_debug(DRV_NAME " - %s: " fmt "" , __func__, ## args)

#define RESERVED(s, t) const unsigned char _pad_to_ ## s ## t[(t) - (s)]

/* Hardware enum-structs */
enum bar_offset {
	BAR_0_1 = 0,
	BAR_2_3 = 2,
	BAR_4_5 = 4
};

struct poweren_ep_in_mbx {
	u64 buf[MAX_MBX];
	u64 ctrl_reg;
};

struct poweren_ep_out_mbx {
	u64 buf[MAX_MBX];
	u64 status_reg;
	u64 ctrl_reg;
};

struct poweren_ep_mmio_mem {
	u64		phys_addr;
	void		*virt_addr;
	unsigned long	size;
};

struct poweren_ep_mbx_regs {
	struct  poweren_ep_in_mbx   *in_mbx;
	struct  poweren_ep_out_mbx  *out_mbx;
};

struct poweren_ep_hirs {
	u64     buf[MAX_HIRS];
};

struct poweren_ep {

	/* Common */
	u64				*inc_dec_doorbell_reg;
	u64				*overload_doorbell_reg;
	struct poweren_ep_hirs		*hirs;
	struct poweren_ep_mbx_regs	mbx_regs;

	/* Device only */
	u64				*in_map_table;
	u64				out_mmio_base;
	u64				out_mmio_mask;
	u64				*pci_interrupt_trigger;

	/* Host only */
	struct pci_dev			*pdev;
	struct msix_entry		msix_entries[MAX_MSIX_ENTRIES];
	struct iommu_domain		*iommu_dom;
	int				*iommu_flags;
	struct poweren_ep_slab_map	*mem_map;
};

/** Exported functions **/
extern void poweren_ep_write_hir(int index, u64 value);
extern u64 poweren_ep_read_hir(int index);
extern struct poweren_ep_mbx_regs *poweren_ep_get_mbx(
		struct poweren_ep_vf *vf);
extern u64 *poweren_ep_get_inc_dec_doorbell(
		struct poweren_ep_vf *vf);
extern u64 *poweren_ep_get_overload_doorbell(
		struct poweren_ep_vf *vf);

/* Host only */
extern struct msix_entry *poweren_ep_get_msix_entries(
		struct poweren_ep_vf *vf);
extern struct pci_dev *poweren_ep_get_pdev(
		struct poweren_ep_vf *vf);
extern struct iommu_domain *poweren_ep_get_iommu_dom(
		struct poweren_ep_vf *vf);
extern struct poweren_ep_slab_map *poweren_ep_get_slab_map(
		struct poweren_ep_vf *vf);

/* Device only */
extern u64 *poweren_ep_get_interrupt_trigger(
		struct poweren_ep_vf *vf);
extern void *poweren_ep_cxb_alloc(gfp_t flags);
extern void poweren_ep_cxb_free(void *cxb);
extern int poweren_ep_csb_wait_valid(void *p);
extern u64 poweren_ep_host_ready(void);


/** Not exported functions **/
extern struct poweren_ep *poweren_ep_get_ep_dev(int vf_num);

/* Device only */
extern int poweren_ep_map_inbound_mem(u64 phys,
		unsigned long virt, unsigned long size,
		unsigned fn);
void poweren_ep_unmap_inbound_mem(unsigned fn);

#endif /* __POWEREN_EP_DRIVER_H__ */
