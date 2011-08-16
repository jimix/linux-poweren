/*
 * Copyright 2010-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) "copro_unit_abort: " fmt

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/io.h>

#include "cop.h"
#include "unit.h"


#define ABORT_DO_ABORT	(1ull << 63)
#define ABORT_VALID_3	0x80000000ull
#define ABORT_VALID_2	0x40000000ull
#define ABORT_VALID_1	0x20000000ull
#define ABORT_VALID_0	0x10000000ull
#define ABORT_VALID	(ABORT_VALID_3 | ABORT_VALID_2 | \
			 ABORT_VALID_1 | ABORT_VALID_0)

#define ABORT_PR(val)	((val >> 24) & 1ul)
#define ABORT_GS(val)	((val >> 23) & 1ul)
#define ABORT_AS(val)	((val >> 22) & 1ul)
#define ABORT_PID(val)	((val >>  8) & 0x3ffful)

#define ABORT_STATUS_REG(unit, offset)	(unit->mmio_addr + offset + 8)
#define ABORT_CSB_REG(unit, offset)	(unit->mmio_addr + offset)

static int try_abort(struct copro_unit *unit, u64 csb, int pid, int offset)
{
	u64 status, unit_csb;

	status = in_be64(ABORT_STATUS_REG(unit, offset));
	if (!(status & ABORT_VALID_3))
		return -1;

	unit_csb = ~0xFul & in_be64(ABORT_CSB_REG(unit, offset));
	if (csb && unit_csb != csb) {
		cop_debug("csb mismatch (%#llx != %#llx) at %#x\n",
			   csb, unit_csb, offset);
		return -1;
	}

	status = in_be64(ABORT_STATUS_REG(unit, offset));

	if ((status & ABORT_VALID) != ABORT_VALID) {
		cop_debug("not all bits set (%#llx) at %#x\n", status, offset);
		return -1;
	}

	if (pid && ABORT_PID(status) != pid) {
		cop_debug("pid mismatch %#x != %#llx at %#x\n",
			   pid, ABORT_PID(status), offset);
		return -1;
	}

	if (ABORT_GS(status) != 0 || ABORT_AS(status) != 0) {
		/* FIXME Check PR in DD2 */
		/* FIXME Check GS and LPID correctly under HV */
		cop_debug("as/gs/pr mismatch (%#llx) at %#x\n", status, offset);
		return -1;
	}

	cop_debug("aborting csb @ %#llx status %#llx\n", unit_csb, status);
	out_be64(ABORT_STATUS_REG(unit, offset), ABORT_DO_ABORT);

	return 0;
}

int copro_unit_abort(struct copro_unit *unit, u64 csb, int pid)
{
	int offset, i;

	if (!unit->regs || unit->regs->abort.count <= 0) {
		cop_debug("not supported on %s\n", unit->name);
		return -EOPNOTSUPP;
	}

	for (i = 0; i < unit->regs->abort.count; i++) {
		/* Count is the count of register pairs */
		offset = unit->regs->abort.start + (2 * i * sizeof(u64));

		if (try_abort(unit, csb, pid, offset) == 0)
			return 0;
	}

	return -ESRCH;
}
