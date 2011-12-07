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

#ifndef _NET_POWEREN_RHEA_CQ_H_
#define _NET_POWEREN_RHEA_CQ_H_

#include <asm/poweren_hea_common_types.h>
#include "rhea-funcs.h"
#include "rhea-base.h"
#include <asm/poweren_hea_cq.h>
#include "hea-cq-regs.h"
#include "rhea-eq.h"


static const unsigned int HEA_CQ_MAX_LENGTH = 0x040000U;	/* 256K */

struct rhea_cq {
	struct hea_cqe *cqe_begin;
	struct rhea_cqte *cqt;
	struct rhea_qinfo *cq_info;
	struct rhea_eq *ceq;
	struct rhea_eq *aeq;
	struct rhea_mem q;
	struct rhea_mem pt;
	struct hea_process process;
	unsigned token;
	unsigned id;
	unsigned cqe_count;
	unsigned cqe_size;
	unsigned final_token;
	struct hea_cq_cfg cfg;
};

/* prototype declaration */
struct rhea_qinfo;
struct hea_adapter;
struct rhea_gen__base;

/* definition of interface */

extern unsigned rhea_cq_init(struct rhea_gen_base *rhea_base);
extern void rhea_cq_fini(struct rhea_gen_base *rhea_base);

extern u64 rhea_cq_qbase_init(struct rhea_gen_base *rhea_base,
				    enum hea_priv_mode priv,
				    unsigned num,
				    int lg, u64 addr);

extern void rhea_cq_qbase_fini(struct rhea_gen_base *rhea_base,
			       enum hea_priv_mode priv);

extern struct rhea_cq *rhea_cq_create(struct rhea_eq *ceq,
				      struct rhea_eq *aeq,
				      struct hea_process *process,
				      struct hea_cq_cfg *cq_cfg);

extern struct rhea_cq *_rhea_cq_get(unsigned int cq_id);

extern int rhea_cq_destroy(struct rhea_cq *cq);

extern int rhea_cq_feature_get(struct rhea_cq *cq,
			       enum hea_cq_feature_get feature,
			       u64 *value);
extern int rhea_cq_feature_set(struct rhea_cq *cq,
			       enum hea_cq_feature_set feature,
			       u64 value);

extern int rhea_cq_mapinfo_get(struct rhea_cq *cq,
			       enum hea_priv_mode priv,
			       void **pointer, unsigned *size,
			       unsigned use_va);

extern void rhea_cq_dump(struct rhea_gen *gen, struct rhea_cq *cqp);

#endif /* _NET_POWEREN_RHEA_CQ_H_ */
