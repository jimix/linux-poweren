/*
 * Copyright (C) 2011 IBM Corporation.
 * Author:	Davide Pasetto <pasetto_davide@ie.ibm.com>
 *		Karol Lynch <karol_lynch@ie.ibm.com>
 *		Kay Muller <kay.muller@ie.ibm.com>
 *		Jimi Xenidis <jimix@watson.ibm.com>
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
#ifndef _NET_POWEREN_RHEA_POWEREN_H_
#define _NET_POWEREN_RHEA_POWEREN_H_

#include <linux/init.h>
#include <linux/of_device.h>

#include <platforms/wsp/copro/cop.h>
#include <platforms/wsp/copro/pbic.h>

#include <asm/poweren_hea_channel.h>

#define DRV_VERSION  "3.0.1"
#define DRV_NAME "poweren_rhea"

#define RHEA_MAJOR_VERSION 3
#define RHEA_MINOR_VERSION 0
#define RHEA_RELEASE_VERSION 1

#define RHEA_OF_ADAPTER_COMPAT	"ibm,wsp-hea"
#define RHEA_OF_ADAPTER		"wsp-hea"
#define RHEA_OF_PORT_COMPAT	"ibm,wsp-hea-port"

#if 0
#define COPRO_TYPE_COMPATIBLE	"ibm,coprocessor-type"
#define WSP_SOC_COMPATIBLE	"ibm,wsp-soc"
#define PBIC_COMPATIBLE		"ibm,wsp-pbic"
#define COPRO_COMPATIBLE	"ibm,coprocessor"
#endif

struct rhea_mmu {
	struct pbic *parent_pbic;
};

/**
 * struct hea_adapter - Top level components of an HEA adapter.
 * %DRV_NAMESZ : Size of the drv_name member.
 * @name : The name of this adapter instance.
 * @instance : the instance id.
 * @handle : Firmware handle for the adapter.
 * @ofdev : Pointer to OFW device.
 * @dn : Pointer to device node.
 * @gba : Physical base address.
 * @max_region_size :
 * @hwirq_base : Start of irq range.
 * @hwirq_count : count of irqs.
 * @q_region_size :
 * @pport_count : number of physical ports of this adapter
 * @map_base_end : physical address of last mapped mmio area
 * @mmio : pointer to base mmio area
 * @mmu : pointer to mmu
 * pports : pointer to port configuration
 */
struct hea_adapter {
#define DRV_NAMESZ (16)
	char name[DRV_NAMESZ];

	int instance;
	__u64 handle;
	struct of_device *ofdev;
	struct device_node *dn;

	unsigned long gba;
	long max_region_size;
	int hwirq_base;
	int hwirq_count;
	long q_region_size;
	__u32 pport_count;

	/* holds value of current mapping */
	__u64 map_base_end;

	struct rhea_gen *mmio;
	struct hea_pport_cfg pports[HEA_MAX_PPORT_COUNT];

	struct rhea_mmu mmu;
};

/**
 * This function is used to perform the device discovery
 */
extern int rhea_discover_adapter(struct device *dev, struct hea_adapter *ap);

#endif /* _NET_POWEREN_RHEA_POWEREN_H_ */
