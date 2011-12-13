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

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/of_platform.h>

#include <poweren_ep_vf.h>
#include <poweren_ep_sm.h>
#include <poweren_ep_driver.h>

#include "cop.h"
#include "unit.h"
#include "pbic_cop.h"

/* Device Tree Constants */
#define DT_OUT_MMIO_RANGE_BASE		2
#define DT_OUT_MMIO_RANGE_SIZE		3

/* Inbound MMIO Mapping Constants */
#define MAS1_TID_INC			1

/**
 * Inbound PCI MMIO Mapping Table
 *
 * It holds PowerEN memory access attributes (PowerEN VF, PID, LPID, AS,
 * GS, PR) for each inbound MMIO/Mailbox/Doorbell buffer to be used for
 * ERAT and notification purposes.
 **/

struct poweren_ep_in_map_table {
	u64 buf[MAX_PFS + MAX_VFS];
};

struct poweren_ep_qos {
	u64 iwq_isn;
	u64 iwq_bar;
	u64 iwq_size;
	u64 iwq_pace_ctrl;
	u64 iwq_read;
	u64 iwq_status;
	u64 iwq_control;
};

struct poweren_ep_hyp {
	u64 prism_vf_pcie_access_validation_table[5];
	RESERVED(0x028, 0x040);
	struct poweren_ep_in_map_table in_map_table;
	RESERVED(0x0d0, 0x200);
	u64 endpoint_control;
	RESERVED(0x208, 0x300);
	struct poweren_ep_qos iwq[6];
	u64 m64_outbound_mmio_base_addr;
	u64 m64_outbound_mmio_base_addr_mask;
	u64 m64_outbound_mmio_starting_addr;
};

struct poweren_ep_hyp_utl {
	u64 system_bus_control;
	u64 status;
	u64 system_bus_agent_status;
	u64 system_bus_agent_err_enable;
	u64 system_bus_agent_interrupt_enable;
	RESERVED(0x528, 0x540);
	u64 system_bus_burst_size_conf;
	u64 revision_id;
	RESERVED(0x550, 0x5c0);
	u64 outbound_posted_header_buff_alloc;
	RESERVED(0x5c8, 0x5d0);
	u64 outbound_posted_data_buff_alloc;
	RESERVED(0x5d8, 0x5e0);
	u64 inbound_posted_header_buff_alloc;
	RESERVED(0x5e8, 0x5f0);
	u64 inbound_posted_data_buff_alloc;
	RESERVED(0x5f8, 0x600);
	u64 outbound_non_posted_buff_alloc;
	RESERVED(0x608, 0x610);
	u64 inbound_non_posted_buff_alloc;
	RESERVED(0x618, 0x620);
	u64 pcie_tags_alloc;
	RESERVED(0x628, 0x630);
	u64 gbif_read_tags_alloc;
	RESERVED(0x638, 0x640);
	u64 pcie_port_control;
	u64 pcie_port_status;
	u64 pcie_port_err_severity;
	u64 pcie_port_interrupt_enable;
	u64 rc_status;			/* not used in ep mode */
	u64 rc_error_enable;		/* not used in ep mode */
	u64 rc_interrupt_enable;	/* not used in ep mode */
	u64 ep_status;
	u64 ep_enable;
	u64 ep_interrupt_enable;	/* not used in ep mode */
	u64 pci_power_management_control1;
	u64 pci_power_management_control2;
};

struct poweren_ep_hyp_stack {
	u64 system_conf1;
	u64 system_conf2;
	RESERVED(710, 718);
	u64 ep_system_conf;
	u64 ep_flr;
	u64 ep_bar_conf;
	u64 link_conf;
	RESERVED(0x738, 0x740);
	u64 power_manangement_conf;
	RESERVED(0x748, 0x750);
	u64 dlp_control;
	u64 dlp_loopback_status;
	u64 err_report_control;
	RESERVED(0x768, 0x770);
	u64 slot_control1;
	u64 slot_control2;
	u64 utl_conf;
	RESERVED(0x788, 0x790);
	u64 buffer_conf;
	u64 err_inject;
	u64 sr_iov_conf;
	u64 pf0_sr_iov_status;
	u64 pf1_sr_iov_status;
	RESERVED(0x7b8, 0x800);
	u64 port_number;
	u64 por_system_config;
	RESERVED(0x810, 0x908);
	u64 reset;
};

struct poweren_ep_hyp_fir {
	u64 lem_acc;
	u64 lem_and_mask;
	u64 lem_or_mask;
	u64 lem_action0;
	u64 lem_action1;
	RESERVED(0xd28, 0xd30);
	u64 lem_err_mask;
	u64 lem_err_and_mask;
	u64 lem_err_or_mask;
};

struct poweren_ep_hyp_err {
	u64 err_status;
	u64 first_err_status;
	u64 err_inject;
	u64 err_lem_report_enable;
	u64 err_interrupt_enable;
	RESERVED(0xda8, 0xdb8);
	u64 err_side_effect_enable;
	u64 err_log0;
	u64 err_log1;
	u64 err_status_mask;
	u64 first_err_status_mask;
};

struct poweren_ep_hyp_control_debug {
	u64 trace_control;
	RESERVED(0xe08, 0xf00);
	u64 dsd_dbg_control;
	RESERVED(0xf08, 0xf10);
	u64 ccl_dbg_control;
	RESERVED(0xf18, 0xf20);
	u64 sddf1_dbg_control;
	RESERVED(0xf28, 0xf30);
	u64 dddf1_dbg_control;
	RESERVED(0xf38, 0xf40);
	u64 fddf1_dbg_control;
	RESERVED(0xf48, 0xf50);
	u64 sdf_dbg_control;
	RESERVED(0xf58, 0xf60);
	u64 wdp_dbg_control;
	RESERVED(0xf68, 0xf70);
	u64 wdm_dbg_control;
	RESERVED(0xf78, 0xf80);
	u64 sddf_dbg_control;
	RESERVED(0xf88, 0xf90);
	u64 dddf_dbg_control;
	RESERVED(0xf98, 0xfa0);
	u64 frg_dbg_control;
	RESERVED(0xfa8, 0xfb0);
	u64 prm_dbg_control;
	RESERVED(0xfb8, 0xfc0);
	u64 rdp_dbg_control;
	RESERVED(0xfc8, 0xfd0);
	u64 rdm_dbg_control;
	RESERVED(0xfd8, 0xfe0);
	u64 epvfreg_dbg_control;
	RESERVED(0xfe8, 0xff0);
	u64 icl_dbg_control;
};

struct poweren_ep_hyp_port {
	struct poweren_ep_hyp hyp;
	RESERVED(0x468, 0x500);
	struct poweren_ep_hyp_utl utl;
	RESERVED(0x6a0, 0x700);
	struct poweren_ep_hyp_stack stack;
	RESERVED(0x910, 0xa00);
	struct poweren_ep_hirs hirs;
	RESERVED(0xa80, 0xd00);
	struct poweren_ep_hyp_fir fir;
	RESERVED(0xd48, 0xd80);
	struct poweren_ep_hyp_err err;
	RESERVED(0xde0, 0xe00);
	struct poweren_ep_hyp_control_debug dbg;
};

struct poweren_ep_user_regs {
	struct poweren_ep_in_mbx in_mbx;
	RESERVED(0x048, 0x100);
	struct poweren_ep_out_mbx out_mbx;
	RESERVED(0x150, 0x200);
	u64 incr_decr_doorbell;
	u64 overload_doorbell;
	u64 doorbell_mask_control;
	RESERVED(0x218, 0x300);
	u64 pci_interrupt_trigger;
	RESERVED(0x308, 0x400);
	u64 prism_dma_control;
	u64 prism_dma_status;
	RESERVED(0x410, 0x800);
};

struct poweren_ep_user_regs_shared {
	u64 inb_mbx_notify;
	u64 res2;
	u64 doorbell_notify;
	u64 res3[29];
	u64 imq_read;
	u64 imq_status;
	u64 imq_ctrl;
};

struct poweren_ep_platform_user_regs {
	struct poweren_ep_user_regs pf[MAX_PFS];
	struct poweren_ep_user_regs vf[MAX_VFS];
	RESERVED(0xa000, 0xf000);
	struct poweren_ep_user_regs_shared shared;
	RESERVED(0xf118, 0x10000);
};

static struct pbic *g_pbic;
static struct poweren_ep ep_dev[TOTAL_FUNCS];
static int g_tlb_entry_slot[TOTAL_FUNCS];

void *poweren_ep_cxb_alloc(gfp_t flags)
{
	return cop_cxb_alloc(flags);
}
EXPORT_SYMBOL_GPL(poweren_ep_cxb_alloc);

void poweren_ep_cxb_free(void *cxb)
{
	cop_cxb_free(cxb);
}
EXPORT_SYMBOL_GPL(poweren_ep_cxb_free);

int poweren_ep_csb_wait_valid(void *p)
{
	return csb_wait_valid(p);
}
EXPORT_SYMBOL_GPL(poweren_ep_csb_wait_valid);

u64 *poweren_ep_get_interrupt_trigger(struct poweren_ep_vf *vf)
{
	if (unlikely(!vf)) {
		poweren_ep_error("need to register against vf manager\n");
		return 0;
	}

	return ep_dev[vf->vf_num].pci_interrupt_trigger;
}
EXPORT_SYMBOL_GPL(poweren_ep_get_interrupt_trigger);

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

/* TODO: remove this when interrupts on device are available */
u64 poweren_ep_host_ready(void)
{
	return readq(&ep_dev->hirs->buf[HOST_INIT_HIR]);
}
EXPORT_SYMBOL_GPL(poweren_ep_host_ready);

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

int poweren_execute_icswx(struct pbic_crb *crb, u32 ccw, struct pbic_csb *csb)
{
	int rc;
	unsigned long timeout = jiffies + 5 * HZ;

	rc = icswx(ccw, crb);
	while (rc == -EAGAIN) {
		if (time_after(jiffies, timeout))
			break;
		rc = icswx_retry(ccw, crb);
	}

	if (rc) {
		poweren_ep_debug("rc %d from icswx\n", rc);
		BUG();
	}

	if (csb_wait_valid(csb)) {
		poweren_ep_debug("timeout waiting for csb p=%llx v=%p\n",
				crb->crb_csb, csb);
		BUG();
	}

	return csb->cc;
}

void poweren_ep_unmap_inbound_mem(unsigned fn)
{
	int rc;
	u32 ccw;
	struct pbic_crb *crb;
	struct pbic_csb *csb;

	int retry = 0;
	int slot = g_tlb_entry_slot[fn];

	/* Setup pbic crb and csb */
	crb = cop_cxb_alloc(GFP_KERNEL);
	csb = cop_cxb_alloc(GFP_KERNEL);

	crb->crb_csb = __pa(csb);

	ccw = (g_pbic->type << 16) | g_pbic->instance | PBIC_CD_WRITE;
	crb->subfunction = PBIC_SF_NONE;

	crb->tlb_entry.mas1 = 0;
	crb->tlb_entry.mas0 = MAS0_ESEL(slot);

	/* Execute crb */
	rc = poweren_execute_icswx(crb, ccw, csb);
	while (rc == 128 && retry++ < 5) {
		csb->valid = 0;
		rc = poweren_execute_icswx(crb, ccw, csb);
	}

	g_pbic->tlb_info[slot].count = 0;

	cop_cxb_free(crb);
	cop_cxb_free(csb);
}

int poweren_ep_map_inbound_mem(u64 phys,
		unsigned long virt, unsigned long size, unsigned fn)
{
	u32 tid;
	unsigned long addr;
	int i, rc, ts, tsize;
	struct pbic_tlb_info *info;
	struct pbic_tlb_entry tlb_entry;

	pbic_tlb_entry_init(&tlb_entry);

	tlb_entry.mas1 = MAS1_VALID | MAS1_IPROT |
		MAS1_TID(fn + MAS1_TID_INC);
	tlb_entry.mas1 |= MAS1_TS;

	tlb_entry.mas7 = phys >> 32;

	tlb_entry.mas3 = phys & MAS3_RPN;

	/* Mapping virt onward */
	tlb_entry.mas2 = MAS2_VAL(virt, size, 0);

	/* Proper permissions */
	tlb_entry.mas3 |= MAS3_SW | MAS3_SR;

	tlb_entry.mas1 |= MAS1_TSIZE(size);
	poweren_ep_debug("pbic_entry, mas1 %x, mas 7 %x,"
			" mas 3 %x, mas 2 %llx\n",
			tlb_entry.mas1, tlb_entry.mas7,
			tlb_entry.mas3, tlb_entry.mas2);

	/* Map the TLB entry */
	rc = pbic_tlb_insert(g_pbic, &tlb_entry, COPRO_MAP_BOLT, 1);

	tid = MAS1_TID_GET(tlb_entry.mas1);
	tsize = MAS1_TSIZE_GET(tlb_entry.mas1);
	ts = !!(tlb_entry.mas1 & MAS1_TS);
	addr = tlb_entry.mas2 & MAS2_EPN_MASK(tsize);

	/* Obtain the slot number for the entry */
	for (i = g_pbic->tlb_size - 1; i > g_pbic->watermark; i--) {
		info = &g_pbic->tlb_info[i];

		if (info->count && info->address == addr && info->pid == tid
				&& (ts == TS_ANY || info->ts == ts)) {
			poweren_ep_debug("matched bolted entry %d\n", i);
			g_tlb_entry_slot[fn] = i;
			break;
		}
	}

	return rc;
}

static int __init poweren_ep_probe(struct platform_device *dev)
{
	const u64 *regp;
	int i, ret, len;
	struct copro_unit *unit;
	struct poweren_ep_hyp_port *hyp_regs;
	struct poweren_ep_platform_user_regs *uregs;
	u64 start, out_mmio_base, out_mmio_mask;

	/* Probing copro unit */
	ret = copro_unit_probe(dev);

	if (ret) {
		poweren_ep_error("fail to probe copro unit\n");
		return ret;
	}

	poweren_ep_debug("copro unit probe done\n");

	/* Getting copro unit data */
	unit = dev_get_drvdata(&dev->dev);

	poweren_ep_debug("obtained copro unit data\n");

	/* Saving hypervisor registers base address */
	hyp_regs = unit->mmio_addr;

	/* Obtaining user space registers base address */
	regp = of_get_property(unit->dn, "reg", &len);

	if (!regp || len < (sizeof(u64) * DT_OUT_MMIO_RANGE_SIZE)) {
		poweren_ep_error("missing/invalid \"reg\" property\n");
		return -EIO;
	}

	uregs = ioremap_nocache(regp[DT_OUT_MMIO_RANGE_BASE],
			regp[DT_OUT_MMIO_RANGE_SIZE]);

	if (!uregs) {
		poweren_ep_error("fail ioremap user space regs base address\n");
		return -ENOMEM;
	}

	poweren_ep_debug("obtained user regs base address\n");
	poweren_ep_debug("user reg phys: %llx\n", regp[DT_OUT_MMIO_RANGE_BASE]);
	poweren_ep_debug("user reg virt: %p\n", uregs);

	poweren_ep_debug("set outbound mmio starting address register\n");

	/* Setting outbound mmio starting address register */
	out_mmio_base =
		in_be64(&hyp_regs->hyp.m64_outbound_mmio_base_addr);
	out_mmio_mask =
		in_be64(&hyp_regs->hyp.m64_outbound_mmio_base_addr_mask);
	start = (MMIO_START & out_mmio_mask) | 1;
	out_be64(&hyp_regs->hyp.m64_outbound_mmio_starting_addr, start);

	/* Expose some hw resources in a global variable */
	for (i = 0; i < TOTAL_FUNCS; ++i) {
		ep_dev[i].mbx_regs.in_mbx = &uregs->pf[i].in_mbx;
		ep_dev[i].mbx_regs.out_mbx = &uregs->pf[i].out_mbx;
		ep_dev[i].inc_dec_doorbell_reg =
			&uregs->pf[i].incr_decr_doorbell;
		ep_dev[i].overload_doorbell_reg =
			&uregs->pf[i].overload_doorbell;
		ep_dev[i].pci_interrupt_trigger =
			&uregs->pf[i].pci_interrupt_trigger;
		ep_dev[i].in_map_table = &hyp_regs->hyp.in_map_table.buf[i];
		ep_dev[i].out_mmio_base = out_mmio_base;
		ep_dev[i].out_mmio_mask = out_mmio_mask;
		ep_dev[i].hirs = &hyp_regs->hirs;
	}

	/* Save pbic in a global variable */
	g_pbic = unit->pbic;

	ret = poweren_ep_slotmgr_init(ep_dev);
	if (ret) {
		poweren_ep_error("failed slot manager init\n");
		iounmap(uregs);
		return ret;
	}

	/* TODO: remove the following line once we know that we don't need
	sync between host and device. To be tested with fw 3.6 */
	/* Notify the host that the init thread is running */
	poweren_ep_write_hir(DEVICE_INIT_HIR, INIT_ACK);

	poweren_ep_debug("init completed\n");

	return 0;
}

static const struct of_device_id poweren_ep_id[] = {
	{ .compatible	= "ibm,wsp-coprocessor-pcieep" },
	{}
};

static struct platform_driver poweren_ep_driver = {
	.probe		= poweren_ep_probe,
	.driver         = {
		.name   = "wsp-copro-pcieep-unit",
		.owner  = THIS_MODULE,
		.of_match_table = poweren_ep_id,
	},
};

static int __init poweren_ep_pcieep_unit_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&poweren_ep_driver);
	if (ret) {
		poweren_ep_error("Failed to register %s driver: %d\n",
				DRV_NAME, ret);
		return ret;
	}

	poweren_ep_info("Registered poweren_ep driver.\n");

	return 0;
}
__copro_unit(pcieep, poweren_ep_pcieep_unit_init);
