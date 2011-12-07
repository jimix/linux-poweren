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

#ifndef RHEA_QP_H_
#define RHEA_QP_H_

#include "rhea-funcs.h"
#include "rhea-eq.h"
#include "rhea-cq.h"

#include <asm/poweren_hea_qp.h>
#include "hea-qp-regs.h"

struct rhea_qp_sq {
	union snd_wqe *wqe_begin;
	struct rhea_mem q;
	struct rhea_mem pt;
	unsigned wqe_size;
	unsigned wqe_count;
};

struct rhea_qp_rqn {
	union rcv_wqe *wqe_begin;
	u64 *ptes;
	struct rhea_mem q;
	struct rhea_mem pt;
	unsigned wqe_size;
	unsigned pgs;
	unsigned wqe_count;
};

struct rhea_qp {
	struct rhea_qpte *qpt;
	struct rhea_qp_rqn rq1;
	struct rhea_qp_rqn rq2;
	struct rhea_qp_rqn rq3;
	struct rhea_qp_sq sq;
	struct rhea_qinfo *qp_info;
	struct rhea_eq *eq;
	struct rhea_cq *rcq;
	struct rhea_cq *scq;
	struct hea_process process;
	unsigned id;
	unsigned pport_nr;
	enum hea_channel_type channel_type;
	struct hea_qp_cfg qp_cfg;
};

extern unsigned rhea_qp_init(struct rhea_gen_base *rhea_base);
extern void rhea_qp_fini(struct rhea_gen_base *rhea_base);

extern u64 rhea_qp_qbase_init(struct rhea_gen_base *rhea_base,
				    enum hea_priv_mode priv,
				    unsigned num,
				    unsigned lg, u64 addr);

extern void rhea_qp_qbase_fini(struct rhea_gen_base *rhea_base,
			       enum hea_priv_mode priv);

extern struct rhea_qp *rhea_qp_create(unsigned pport_nr,
				      enum hea_channel_type channel_type,
				      struct hea_process *process,
				      struct rhea_eq *eq,
				      struct rhea_cq *rcq,
				      struct rhea_cq *scq,
				      struct hea_pd_cfg *pd_cfg,
				      struct hea_qp_cfg *qp_cfg);

extern struct rhea_qp *_rhea_qp_get(unsigned int qp_id);

extern int rhea_qp_enable(struct rhea_qp *qp, unsigned hw_mng);

extern int rhea_qp_disable(struct rhea_qp *qp);

extern int rhea_qp_destroy(struct rhea_qp *qp);

extern int rhea_qp_feature_get(struct rhea_qp *qp,
			       enum hea_qp_feature_get feature_get,
			       u64 *value);
extern int rhea_qp_feature_set(struct rhea_qp *qp,
			       enum hea_qp_feature_set feature_set,
			       u64 value);

extern void rhea_qp_dump(struct rhea_gen *gen, struct rhea_qp *qp);

extern int rhea_qp_mapinfo_get(struct rhea_qp *qp,
			       enum hea_priv_mode priv,
			       void **pointer, unsigned *size,
			       unsigned use_va);

#endif /* RHEA_QP_H_ */
