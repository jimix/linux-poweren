/*
 * Copyright 2009-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) "copro_sync: " fmt

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/ratelimit.h>
#include <asm/icswx.h>

#include <mm/icswx.h>

#include "cop.h"
#include "unit.h"
#include "imq.h"

static u32 copro_pack_ccw(struct copro_instance *copro, int fc)
{
	return (copro->type << 16) | copro->instance | fc;
}

struct sync_crb {
	u32 ccw;
	u32 reserved;
	u64 crb_csb;
} __aligned(128);

#define SYNC_CRB_FC	0x4000		/* CL = 01b and FC = 0 */

struct sync_csb {
	u16 valid;		/* only bit 0 is used */
	u8 cc;
	u8 ce;
	u32 pbc;
	u64 addr;
} __aligned(16);


static int send_sync(struct copro_instance *copro, struct sync_crb *crb,
		     struct sync_csb *csb)
{
	unsigned long timeout;
	int rc;
	u32 ccw;

	ccw = copro_pack_ccw(copro, SYNC_CRB_FC);

	/*
	 * Since we are running in HV mode lets do this without
	 * translate that way we do not need the linear map to
	 * necessarily occupy the pbic
	 */
	BUG_ON(mfmsr() & MSR_GS);
	crb->crb_csb = __pa(csb);
	crb->crb_csb |= COP_CRB_CSB_AT;

	csb->valid = 0;

	timeout = jiffies + 10 * HZ;

	rc = icswx(ccw, crb);
	while (rc == -EAGAIN) {
		if (time_after(jiffies, timeout)) {
			cop_debug("sync not accepted by instance %s\n",
				  copro->name);
			return -1;
		}
		usleep_range(500, 1000);
		rc = icswx_retry(ccw, crb);
	}

	if (rc) {
		cop_debug("sync to instance %s returned %d\n", copro->name, rc);
		return -1;
	}

	timeout = jiffies + 10 * HZ;

	while (csb->valid == 0) {
		if (time_after(jiffies, timeout)) {
			cop_debug("no csb from copro %s\n", copro->name);
			return -1;
		}
		usleep_range(500, 1000);
		barrier();
	}

	if (csb->cc) {
		cop_debug("csb status %d from copro %s\n", csb->cc,
			  copro->name);
		return -1;
	}

	return 0;
}

static void sync_copro_instance(struct copro_instance *copro,
				struct mm_struct *mm,
				struct sync_crb *crb, struct sync_csb *csb)
{
	cop_debug("syncing copro %s for mm %p pid %#x\n", copro->name, mm,
		   mm->context.id);

	if (send_sync(copro, crb, csb) == 0)
		return;

	if (copro_unit_abort(copro->unit, (u64)csb, 0) == 0) {
		/* Our sync crb was stuck, we're probably done */
		cop_debug("sync got stuck but aborted OK\n");
		return;
	}

	copro_unit_abort(copro->unit, 0, 0);

	if (send_sync(copro, crb, csb) == 0) {
		cop_debug("second sync completed OK\n");
		return;
	}

	copro_unit_abort(copro->unit, 0, 0);

	pr_err("failed to sync copro %s for mm %p pid %#x\n", copro->name, mm,
		mm->context.id);
}

static void sync_copro_type(struct copro_type *copro_type, struct mm_struct *mm,
			    struct sync_crb *crb, struct sync_csb *csb)
{
	struct copro_instance *copro;

	if (copro_type->no_sync) {
		/* FIXME do custom sync logic where required */
		pr_warn_ratelimited("warning: can't sync copro type %s\n",
				    copro_type->name);
		return;
	}

	list_for_each_entry(copro, &copro_type->instance_list, type_list)
		sync_copro_instance(copro, mm, crb, csb);
}

void copro_exit_mm_context(struct mm_struct *mm)
{
	struct copro_type *copro_type;
	struct sync_crb *crb;
	struct sync_csb *csb;

	copro_mmu_flush_mm(mm, 1);

	/* FIXME Flush bolted entries by hand and then clear used_pbics */

	crb = cop_cxb_alloc(GFP_KERNEL);
	csb = cop_cxb_alloc(GFP_KERNEL);

	list_for_each_entry(copro_type, &copro_type_list, list) {
		if (mm_used_copro_type(mm, copro_type->type))
			sync_copro_type(copro_type, mm, crb, csb);
	}

	cop_cxb_free(crb);
	cop_cxb_free(csb);

	copro_imq_exit_mm_context(mm);
}
