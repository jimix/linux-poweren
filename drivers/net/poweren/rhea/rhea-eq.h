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

#ifndef _RHEA_EQ_H_
#define _RHEA_EQ_H_

#include <linux/types.h>
#include <linux/irqreturn.h>

#include "rhea-base.h"

#include <asm/poweren_hea_eq.h>
#include <asm/poweren_hea_common_types.h>

struct rhea_eqte {
	u64 eq_hcr;
	u64 eq_c;
	u64 eq_herr;
	u64 eq_aer;
	u64 eq_ptp;
	u64 eq_tp;
	u64 eq_ssba;
	u64 eq_psba;
	u64 eq_cec;
	u64 eq_meql;
	u64 eq_xisbi;
	u64 eq_xisc;
	u64 eq_it;
};

struct rhea_irq {
	char eq_irq_name[64];
	hea_irq_handler_t irq_handler;
	void *irq_handler_args;
	int virq;
	int hwirq;
	enum hea_interrupts irq_type;
};

struct rhea_eq {
	struct hea_eqe *eqe_begin;
	struct rhea_eqte *eqt;
	struct rhea_qinfo *eq_info;
	struct rhea_irq irq;
	struct hea_eq_cfg eq_cfg;
	/* number of EQEs the EQ can hold */
	u64 eqe_count;
	unsigned eqe_size;
	struct rhea_mem q;
	struct rhea_mem pt;
	struct hea_process process;
	u8 summary_bytes[2];
	u16 id;
};

/* prototype declaration */
struct rhea_qinfo;
struct hea_adapter;
struct rhea_gen_base;

/* definition of interface */
extern unsigned rhea_eq_init(struct rhea_gen_base *rhea_base);
extern void rhea_eq_fini(struct rhea_gen_base *rhea_base);

extern u64 rhea_eq_qbase_init(struct rhea_gen_base *rhea_base,
				    enum hea_priv_mode priv,
				    int num, int lg,
				    u64 addr);
extern void rhea_eq_qbase_fini(struct rhea_gen_base *rhea_base,
			       enum hea_priv_mode priv);

extern int rhea_eq_destroy(struct rhea_eq *eq);

extern struct rhea_eq *rhea_eq_create(struct hea_process *process,
				      struct hea_eq_cfg *eq_cfg);

extern struct rhea_eq *_rhea_eq_get(unsigned int eq_id);

extern int rhea_eq_mapinfo_get(struct rhea_eq *eq,
			       enum hea_priv_mode priv,
			       void **pointer, unsigned *size,
			       unsigned use_va);

extern int rhea_eq_feature_get(struct rhea_eq *eq,
			       enum hea_eq_feature_get feature,
			       u64 *value);
extern int rhea_eq_feature_set(struct rhea_eq *eq,
			       enum hea_eq_feature_set feature,
			       u64 value);

extern void rhea_eq_dump(struct rhea_gen *gen, struct rhea_eq *eqp,
			 unsigned eq);

extern void rhea_interrupts_free(struct rhea_eq *eqp);

extern int rhea_interrupts_setup(struct rhea_eq *eqp,
				 const char *name,
				 unsigned hwirq_base,
				 unsigned hwirq_count,
				 hea_irq_handler_t irq_handler,
				 void *irq_handler_args);

#endif /* _RHEA_EQ_H_ */
