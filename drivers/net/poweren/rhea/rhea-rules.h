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

#ifndef _RHEA_RULES_H_
#define _RHEA_RULES_H_

#include "rhea-funcs.h"

/* datatypes */

struct rhea_port_rule_values {
	u64 val;
	ulong idx;
};

/* datatype prototypes */

struct rhea_pport_regs;

/* function prototypes */
extern void rhea_port_rules(struct rhea_pport_regs *pport);

extern void rhea_port_rules_ranges(struct rhea_pport_regs *pport);

extern void rhea_port_sabsel(struct rhea_pport_regs *pport);

extern void rhea_port_copy_rulemem(struct rhea_pport_regs *pport,
				   const struct rhea_port_rule_values *rulemem,
				   unsigned rmsz);

#endif /* _RHEA_RULES_H_ */
