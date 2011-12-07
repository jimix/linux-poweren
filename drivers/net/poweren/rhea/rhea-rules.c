/*
 * Copyright (C) 2011, 2012 IBM Corporation.
 * Author:	Davide Pasetto <pasetto_davide@ie.ibm.com>
 *			Karol Lynch <karol_lynch@ie.ibm.com>
 *			Kay Muller <kay.muller@ie.ibm.com>
 *			Jimi Xenidis <jimix@watson.ibm.com>
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

#include "rhea-rules.h"
#include "rhea-base.h"

static const u64 rhea_pg_rrange[8] = {
	0x0000000000000000ULL,
	0x0000000000000000ULL,
	0x0000000f000005ffULL,
	0x000000110005ffffULL,
	0x000000085000ffffULL,
	0x0000000f12345678ULL,
	0x0000000f7abcdef0ULL,
	0x0000000f5555aaaaULL,
};

static const u64 rhea_pg_sabsel[4] = {
	0x0000000002000000ULL,	/*  - Px_sabsel0 */
	0x0000000000000A0FULL,	/*  - Px_sabsel1 */
	0x0000000000040906ULL,	/*  - Px_sabsel2 */
	0x0000001E00001A1FULL,	/*  - P0_sabsel3 */
};

void rhea_port_rules_ranges(struct rhea_pport_regs *pport)
{
	unsigned m;

	for (m = 0; m < ARRAY_SIZE(rhea_pg_rrange); m++) {
		out_be64(&pport[0].bpfc.pg_rrange[m], rhea_pg_rrange[m]);
		out_be64(&pport[1].bpfc.pg_rrange[m], rhea_pg_rrange[m]);
		out_be64(&pport[2].bpfc.pg_rrange[m], rhea_pg_rrange[m]);
		out_be64(&pport[3].bpfc.pg_rrange[m], rhea_pg_rrange[m]);
	}

	rhea_info("... Done setting up HEA Rule ranges");
}

void rhea_port_sabsel(struct rhea_pport_regs *pport)
{
	unsigned m;

	for (m = 0; m < ARRAY_SIZE(rhea_pg_sabsel); m++) {
		out_be64(&pport[0].bpfc.pg_sabsel[m], rhea_pg_sabsel[m]);
		out_be64(&pport[1].bpfc.pg_sabsel[m], rhea_pg_sabsel[m]);
		out_be64(&pport[2].bpfc.pg_sabsel[m], rhea_pg_sabsel[m]);
		out_be64(&pport[3].bpfc.pg_sabsel[m], rhea_pg_sabsel[m]);
	}

	rhea_info("... Done setting up HEA SRM Address Bit Selection");
}

void rhea_port_copy_rulemem(struct rhea_pport_regs *pport,
			    const struct rhea_port_rule_values *rulemem,
			    unsigned rmsz)
{
	unsigned i;
	unsigned m;
	unsigned sz;
	u64 val;

	sz = ARRAY_SIZE(pport[0].pfc.p_rulem);

	i = 0;
	for (m = 0; m < sz; m++) {
		if (i >= rmsz || m < rulemem[i].idx) {
			val = 0ULL;
		} else {
			val = rulemem[i].val;
			++i;
		}

		/* port 0 covers 0 & 1 and port 2 covers 2 & 3 */
		out_be64(&pport[0].pfc.p_rulem[m], val);
		out_be64(&pport[2].pfc.p_rulem[m], val);
	}
}
