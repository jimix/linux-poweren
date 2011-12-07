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

#ifndef _NET_POWEREN_RHEA_BASE_H_
#define _NET_POWEREN_RHEA_BASE_H_

#include "rhea-funcs.h"
#include <rhea-channel.h>

#define RHEA_DEVICE_PAGE_SIZE 0x1000

struct rhea_gen_base {
	u64 g_gba;
	const u64 g_nid;
	const u64 g_vid;
	const u64 g_heacap;
	const u64 g_bimac;
	PAD(0x28, 0x48);
	u64 g_qpuba;
	u64 g_qppba;
	PAD(0x58, 0x60);
	u64 g_qpsba;
	PAD(0x68, 0xa8);
	u64 g_qptsz;
	const u64 g_qpciac;
	u64 g_cquba;
	u64 g_cqpba;
	PAD(0xc8, 0xd0);
	u64 g_cqsba;
	PAD(0xd8, 0xf8);
	u64 g_cqtsz;
	u64 g_eqpba;
	PAD(0x108, 0x110);
	u64 g_eqsba;
	PAD(0x118, 0x138);
	u64 g_eqtsz;
	u64 g_buidbase;
	u64 g_heac;
	u64 g_uaelog;
	u64 g_fuec[16];
	u64 g_faec[16];
	u64 g_thaba;
	u64 g_nnc;
	u64 g_cuba;
	const u64 g_rbac;
	const u64 g_heatime;
	const u64 g_mmccap;
	u64 g_edp;
	PAD(0x290, 0x2b8);
	const u64 g_cntcap;
	u64 g_timeinc;
	PAD(0x2c8, 0x2e8);
	PAD(0x2e8, 0x400);
};

struct rhea_gen_thmbx {
	u64 g_thmbx[256];
};

struct rhea_gen_its {
	u64 g_its[1024];
};

struct rhea_gen_fir {
	u64 g_firhes;
	u64 g_fir2hes;
	PAD(0x8010, 0x9000);
};

#include "rhea-channel.h"

struct rhea_gen {
	/* 0x000000 - 0x0003ff */
	struct rhea_gen_base base;

	PAD(0x000400, 0x001000);
	/* 0x001000 - 0x0017ff */
	struct rhea_gen_thmbx thmbx;

	PAD(0x001800, 0x004000);
	/* 0x004000 - 0x005fff */
	struct rhea_gen_its its;

	PAD(0x006000, 0x008000);
	/* 0x008000 - 0x008fff */
	struct rhea_gen_fir fir;

	PAD(0x009000, 0x040000);

	/* 0x040000 - The rest are ports */
	struct rhea_pport_regs pport[4];

	/* architecturally there are 31 ports */
	struct rhea_pport_regs _resv_port[31 - 4];
};



enum rhea_memory_type {
	RHEA_ADDRESS_PHYSICAL,
	RHEA_ADDRESS_VIRTUAL,

};

struct rhea_mem {
	ulong pa;
	u64 *va;
	ulong size;
	ulong page_count;
};

extern const char *rhea_priv_mode_str[];

struct rhea_qinfo {
	struct rhea_mem base;
	unsigned dev_page_sz;
	unsigned os_page_sz;
};

/*
 * QList interfaces
 */
struct rhea_qlist {
	void **q;
	unsigned alloced;
	unsigned max;
	spinlock_t lock;
};

#define rhea_reg_print(base, member, fmt, ...)			\
({								\
	ulong __addr = (ulong)(base);				\
	__addr += offsetof(__typeof__(*(base)), member);	\
	rhea_reg_print_internal((u64 *)__addr, fmt, ##__VA_ARGS__); \
})

extern void rhea_gen_reset(struct rhea_gen *gen);

extern int rhea_ql_alloc_init(struct rhea_qlist *ql, unsigned qn);
extern void rhea_ql_alloc_fini(struct rhea_qlist *ql);

extern int rhea_ql_alloc(struct rhea_qlist *ql, void *q);

extern void *rhea_ql_get(struct rhea_qlist *ql, unsigned int id);

extern void rhea_ql_free(struct rhea_qlist *ql, unsigned id);

extern void *rhea_qte(const struct rhea_qinfo *qi,
		      unsigned id,
		      enum rhea_memory_type addr_type);

extern void rhea_gen_dump(struct rhea_gen *gen);

extern u64 rhea_q_qbase_init(struct rhea_qinfo *qi, unsigned num,
				   enum hea_priv_mode priv,
				   int lg, u64 addr,
				   const char *str_q, u64 *addr_new);

extern void rhea_q_qbase_fini(struct rhea_qinfo *qi);

extern int rhea_pt_alloc(struct rhea_qinfo *qi,
			 struct rhea_mem *pt, struct rhea_mem *q,
			 unsigned size,
			 unsigned hw_mng, unsigned auto_toggling);

extern int rhea_pt_free(struct rhea_mem *pt,
			struct rhea_mem *q, unsigned hw_mng);

extern struct rhea_pport *_rhea_pport_get(unsigned pport_nr);

#endif /* _NET_POWEREN_RHEA_BASE_H_ */
