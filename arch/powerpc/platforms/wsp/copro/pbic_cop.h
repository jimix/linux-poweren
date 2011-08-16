#ifndef _WSP_COPRO_COP_PBIC_H
#define _WSP_COPRO_COP_PBIC_H

/*
 * Copyright 2008 Michael Ellerman, IBM Corporation
 * Copyright 2008 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm_types.h>

#include <asm/icswx.h>

#include "cop.h"
#include "pbic.h"

/* PBIC icswx request codes */
#define PBIC_CD_FLUSH		0
#define PBIC_CD_READ		1
#define PBIC_CD_WRITE		2
#define PBIC_CD_INV		3
#define PBIC_CD_FC_MASK		3
#define PBIC_CD_SUSPEND		0xC000
#define PBIC_CD_RESUME		0xC001

/* PBIC icswx sub function codes */
#define PBIC_SF_NONE		0
#define PBIC_SF_READ_INDEX	0
#define PBIC_SF_READ_SEARCH	1
#define PBIC_SF_INV_LPID	0
#define PBIC_SF_INV_LPID_PID	1
#define PBIC_SF_INV_SEARCH	3

/* PBIC icswx CSB codes */
#define	PBIC_CC_OK		0
#define	PBIC_CC_NOT_FOUND	35	/* Not found for read or invalidate */
#define	PBIC_CC_PRESENT		15	/* Existing entry matches for write */


#define PBIC_MAX_TLB_SIZE	(1 << 8) /* Determined by mas0.esel */

extern struct list_head pbic_list;

#define for_each_pbic(_iter)	\
	list_for_each_entry(_iter, &pbic_list, list)

union mmucr3 {
	u32 _val;
	struct {
		u32 _reserved_1:17;
		u32 x:1;
		u32 r:1;
		u32 c:1;
		u32 ecl:2;
		u32 class:2;
		u32 wlc:2;
		u32 resvattr:1;
		u32 _reserved_2:1;
		u32 thdid:4;
	};
};

struct pbic_tlb_entry {
	u32 mmucr3;
	u32 mas8;
	u32 mas0;
	u32 mas1;
	u64 mas2;
	u32 mas7;
	u32 mas3;
};

struct pbic_tlb_search {
	u32 mas5;
	u32 mas6;
	u32 mas2;
};


struct pbic_crb {
	u32 ccw;
	u8 reserved;
	u8 subfunction;
	u16 tlb_entry_index;
	u64 crb_csb;
	union {
		struct pbic_tlb_entry tlb_entry;
		struct pbic_tlb_search tlb_search;
	};
} __aligned(128);


struct pbic_csb {
	u16 valid;		/* only bit 0 is used */
	u8 cc;
	u8 ce;
	u16 reserved2;
	u16 tlb_entry_index;
	u64 address;
	struct pbic_tlb_entry tlb_entry;
} __aligned(128);


static inline void pbic_attach_csb(struct pbic_crb *crb, struct pbic_csb *csb)
{
	crb->crb_csb = __pa(csb);	/* Must be real address */
}

static inline void pbic_tlb_entry_init(struct pbic_tlb_entry *tlbe)
{
	memset(tlbe, 0, sizeof(struct pbic_tlb_entry));
}

static inline u32 pbic_pack_ccw(struct pbic *pbic, int fc)
{
	return (pbic->type << 16) | pbic->instance | fc;
}

#define PBIC_WATERMARK_REG	0x68

static inline void pbic_set_watermark(struct pbic *pbic)
{
	pbic_debug(pbic, "adjusting watermark to %d\n", pbic->watermark);
	BUG_ON(pbic->watermark > pbic->tlb_size);
	out_be64(pbic->mmio_addr + PBIC_WATERMARK_REG, pbic->watermark);
}

static inline int psize_to_pbic_tsize(int psize)
{
	return mmu_psize_defs[psize].enc;
}

static inline int shift_to_pbic_tsize(int shift)
{
	return shift - 10;
}

extern int pbic_tlb_insert(struct pbic *pbic, struct pbic_tlb_entry *tlbe,
			   int flags, int refcount);
extern struct pbic_tlb_entry *pbic_tlb_read_entry(struct pbic *pbic, int slot);

extern void pbic_invalidate_slot(struct pbic *pbic, int slot);

extern void pbic_maintenance(struct work_struct *w);

#ifdef DEBUG
extern void pbic_tlb_entry_dump(struct pbic *pbic, struct pbic_tlb_entry *p);
extern void pbic_debug_invalidate(struct pbic *pbic);
extern void pbic_debug_check_bolted_info(struct pbic *pbic);
extern void pbic_tlb_entry_dump(struct pbic *pbic, struct pbic_tlb_entry *e);

static inline void pbic_crb_dump(struct pbic *pbic, struct pbic_crb *crb)
{
	cop_debug("%s crb @ %p ct:%d cd:%d sf:%d idx:%d csb:%llx\n",
		  pbic->name, crb,
		  crb->ccw >> 16,
		  crb->ccw & 0xFFFF,
		  crb->subfunction, crb->tlb_entry_index, crb->crb_csb);

	if ((crb->ccw & PBIC_CD_FC_MASK) == PBIC_CD_WRITE)
		pbic_tlb_entry_dump(pbic, &crb->tlb_entry);
}

#else
static inline void pbic_tlb_entry_dump(struct pbic *pbic,
				       struct pbic_tlb_entry *p) { }
static inline void pbic_crb_dump(struct pbic *pbic, struct pbic_crb *crb) { }
static inline void pbic_debug_invalidate(struct pbic *pbic) { }
static inline void pbic_debug_check_bolted_info(struct pbic *pbic) { };
#endif /* DEBUG */

#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
extern void pbic_debugfs_init(void);
extern void pbic_debugfs_init_pbic(struct pbic *pbic);
#else
static inline void pbic_debugfs_init(void) { }
static inline void pbic_debugfs_init_pbic(struct pbic *pbic) { }
#endif

extern int pbic_icswx(struct pbic *pbic, u32 ccw, struct pbic_csb *csb);
extern int pbic_crb_execute(struct pbic *pbic, u32 ccw);

static inline void pbic_cxb_recycle(struct pbic *pbic)
{
	/* FIXME use dcbz for the CRB */
	memset(pbic->crb, 0, sizeof(*pbic->crb));
	pbic->csb->valid = 0;
	pbic_attach_csb(pbic->crb, pbic->csb);
}

#endif /* _WSP_COPRO_COP_PBIC_H */
