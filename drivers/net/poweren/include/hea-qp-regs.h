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

#ifndef _POWEREN_HEA_QP_REGS_H_
#define _POWEREN_HEA_QP_REGS_H_

#include <hea-bits.h>

#define HEA_SQ_WQES_MAX ((32U << 10) - 1)
#define HEA_RQ_WQES_MAX ((64U << 10) - 1)

#define HEA_QP_STATE_REQ_ERROR	0x80

#define HEA_SND_TX_WRAP_NORMAL 0
#define HEA_SND_TX_WRAP_FORCE 1
#define HEA_SND_TX_WRAP_RECIRCULATE 2

enum rhea_qp_state {
	rhea_qp_state_res_error = 0x00,
	rhea_qp_state_reset = 0x01,
	rhea_qp_state_init = 0x02,
	rhea_qp_state_ready2rcv = 0x03,
	_rhea_qp_state_resv = 0x04,
	rhea_qp_state_ready2send = 0x05,
};

union rhea_qpte_c {
	u64 val;
	struct {
		unsigned qp_enable:1;
		unsigned qp_disable_complete:1;
		unsigned qp_error:1;
		unsigned _resv_0b03:1;
		unsigned qp_hdr_sep:2;
		unsigned qp_sq_thold_warning:1;
		unsigned _resv_0b07_0b15:9;
		enum rhea_qp_state qp_req_qp_state:8;
		unsigned _resv_0b24:1;
		enum rhea_qp_state qp_res_qp_state:7;
		unsigned _resv_0b32_0b63;
	};
};

struct rhea_qpte {
	u64 qp_hcr;
	union rhea_qpte_c qp_c;
	PAD(0x010, 0x018);
	u64 qp_aer;
	u64 qp_sqa;
	u64 qp_sqc;
	u64 qp_rq1a;
	u64 qp_rq1c;
	u64 qp_st;
	u64 qp_aerr;
	u64 qp_tenure;
	PAD(0x058, 0x098);
	u64 qp_portp;
	PAD(0x0a0, 0x0d0);
	u64 qp_sl;
	PAD(0x0d8, 0x100);
	u64 qp_t;
	u64 qp_sqhp;
	u64 qp_sqptp;
	PAD(0x118, 0x140);
	u64 qp_sqwsize;
	PAD(0x148, 0x170);
	u64 qp_sqsize;
	PAD(0x178, 0x1b0);
	u64 qp_sigt;
	u64 qp_wqecnt;
	u64 qp_rq1hp;
	u64 qp_rq1ptp;
	u64 qp_rq1size;
	PAD(0x1d8, 0x220);
	u64 qp_rq1wsize;
	PAD(0x228, 0x240);
	u64 qp_pd;
	u64 qp_scqn;
	u64 qp_rcqn;
	u64 qp_aeqn;
	PAD(0x260, 0x268);
	u64 qp_ram;
	PAD(0x270, 0x300);
	u64 qp_rq2a;
	u64 qp_rq2c;
	u64 qp_rq2hp;
	u64 qp_rq2ptp;
	u64 qp_rq2size;
	u64 qp_rq2wsize;
	u64 qp_rq2th;
	u64 qp_rq3a;
	u64 qp_rq3c;
	u64 qp_rq3hp;
	u64 qp_rq3ptp;
	u64 qp_rq3size;
	u64 qp_rq3wsize;
	u64 qp_rq3th;
	u64 qp_lpn;
	PAD(0x378, 0x3e8);
	u64 qp_rpphws;
	u64 qp_rpphws2;
	PAD(0x3f8, 0x400);
};

#endif /* _POWEREN_HEA_QP_REGS_H_ */
