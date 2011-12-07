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

#ifndef _NET_POWEREN_HEA_CQ_REGS_H_
#define _NET_POWEREN_HEA_CQ_REGS_H_

#include <hea-bits.h>

struct rhea_cqte {
	u64 cq_hcr;
	u64 cq_c;
	u64 cq_herr;
	u64 cq_aer;
	u64 cq_ptp;
	u64 cq_tp;
	u64 cq_fec;
	u64 cq_feca;
	u64 cq_ep;
	u64 cq_eq;
	PAD(0x050, 0x58);
	u64 cq_n0;
	u64 cq_n1;
	PAD(0x068, 0x100);
	u64 cq_hp;
	u64 cq_base;
	u64 cq_sm0;
	u64 cq_sm1;
	u64 cq_sm2;
	u64 cq_sm3;
	u64 cq_sc;
	PAD(0x138, 0x140);
	u64 cq_pd;
	u64 cq_hwcnt;
};

#endif /* _NET_POWEREN_HEA_CQ_REGS_H_ */
