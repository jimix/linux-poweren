#ifndef _ARCH_POWERPC_MM_ICSWX_H_
#define _ARCH_POWERPC_MM_ICSWX_H_

/*
 *  ICSWX and ACOP Management
 *
 *  Copyright (C) 2011 Anton Blanchard, IBM Corp. <anton@samba.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/slab.h>
#include <asm/mmu_context.h>

/* also used to denote that PIDs are not used */
#define COP_PID_NONE 0

static inline void sync_cop(void *arg)
{
	struct mm_struct *mm = arg;

	if (mm == current->active_mm)
		switch_cop(current->active_mm);
}

#ifdef CONFIG_PPC_ICSWX_PID
extern int get_cop_pid(struct mm_struct *mm);
extern int disable_cop_pid(struct mm_struct *mm);
extern void free_cop_pid(int free_pid);
#else
#define get_cop_pid(m) (COP_PID_NONE)
#define disable_cop_pid(m) (COP_PID_NONE)
#define free_cop_pid(p)
#endif

/*
 * These are implementation bits for architected registers.  If this
 * ever becomes architecture the should be moved to reg.h et. al.
 */
/* UCT is the same bit for Server and Embedded */
#define ICSWX_DSI_UCT		0x00004000  /* Unavailable Coprocessor Type */

#ifdef CONFIG_BOOKE
/* Embedded implementation gives us no hits as to what the CT is */
#define ICSWX_GET_CT_HINT(x) (-1)
#else
/* Server implementation contains the CT value in the DSISR */
#define ICSWX_DSISR_CTMASK	0x00003f00
#define ICSWX_GET_CT_HINT(x)	(((x) & ICSWX_DSISR_CTMASK) >> 8)
#endif

#define ICSWX_RC_STARTED	0x8	/* The request has been started */
#define ICSWX_RC_NOT_IDLE	0x4	/* No coprocessor found idle */
#define ICSWX_RC_NOT_FOUND	0x2	/* No coprocessor found */
#define ICSWX_RC_UNDEFINED	0x1	/* Reserved */

extern int acop_handle_fault(struct pt_regs *regs, unsigned long address,
			     unsigned long error_code);

static inline u64 acop_copro_type_bit(unsigned int type)
{
	return 1ULL << (63 - type);
}

static inline int mm_used_copro_type(struct mm_struct *mm, unsigned int type)
{
	/* Safe atm w/out locking because we never remove bits from acop */
	return mm->context.acop & acop_copro_type_bit(type);
}

#ifdef CONFIG_PPC_ICSWX
static inline int mm_used_copro(struct mm_struct *mm)
{
	return mm->context.acop != 0;
}

static inline int copro_mm_context_init(struct mm_struct *mm)
{
	mm->context.cop_lockp = kmalloc(sizeof(spinlock_t), GFP_KERNEL);
	if (!mm->context.cop_lockp)
		return -ENOMEM;

	spin_lock_init(mm->context.cop_lockp);
	return 0;
}

static inline void copro_mm_context_destroy(struct mm_struct *mm)
{
	drop_cop(mm->context.acop, mm);
	kfree(mm->context.cop_lockp);
	mm->context.cop_lockp = NULL;
}

#else  /* CONFIG_PPC_ICSWX */

static inline int mm_used_copro(struct mm_struct *mm) { return 0; }
static inline int copro_mm_context_init(struct mm_struct *mm) { return 0; }
static inline void copro_mm_context_destroy(struct mm_struct *mm) { }

#endif /*CONFIG_PPC_ICSWX */

#ifdef CONFIG_PPC_WSP_COPRO
static inline int mm_used_copro_mmu(struct mm_struct *mm)
{
	mb();
	return mm->context.pbics_used != 0;
}

extern void copro_mmu_flush_entry(struct mm_struct *mm, unsigned long addr,
				  unsigned int pid, unsigned int tsize,
				  unsigned int ind);
extern void copro_exit_mm_context(struct mm_struct *mm);
extern void copro_mmu_flush_mm(struct mm_struct *mm, int full_flush);
extern void copro_mmu_flush_bolted(struct mm_struct *mm, unsigned long addr);

#else  /* CONFIG_PPC_WSP_COPRO */

static inline int mm_used_copro_mmu(struct mm_struct *mm) { return 0; }
static inline void copro_mmu_flush_entry(struct mm_struct *mm,
					unsigned long addr,
					unsigned int pid, unsigned int tsize,
					unsigned int ind) { }
static inline void copro_exit_mm_context(struct mm_struct *mm) { }
static inline void copro_mmu_flush_mm(struct mm_struct *mm, int full_flush) { }
static inline void copro_mmu_flush_bolted(struct mm_struct *mm,
					  unsigned long addr) { }

#endif	/* CONFIG_PPC_WSP_COPRO */

#endif /* !_ARCH_POWERPC_MM_ICSWX_H_ */
