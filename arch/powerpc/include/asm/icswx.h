#ifndef _ASM_POWERPC_ICSWX_H
#define _ASM_POWERPC_ICSWX_H

/*
 * Copyright 2008-2009 Michael Ellerman, IBM Corporation
 * Copyright 2008 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <asm/ppc-opcode.h>

/**
 * This is the C binding for the ICSWX instruction.  It stands for
 * Initiate Coprocessor Store Word Indexed.
 * @ccw: 4 byte Coprocessor Control Word that is actually stored into
 *       the address of crb.
 * @crb: Coprocessor Request Block which is the address of the
 *       "request" to send to a coprocessor. After the initial 4 bytes
 *       from the ccw the remaining bytes and size of the CRB are
 *       implementation dependent altough it must alwasy be 128 bytes
 *       aligned.
 *
 * Returns 0 on success, or a negative errno, EAGAIN for retry and
 * ENODEV if there is no suct coprocessor.
 *
 * We pass the CCW separately so that it never actually needs to get
 * stored ahead of time, but it won't hurt if it is.
 */
static inline int icswx_raw(u32 ccw, void *crb)
{
	int rc;

	asm volatile (
		"# icswx						\n"
		"# it can be faster to branch then to mfcr		\n"
		"	li %[rc],0					\n"
		"	"PPC_ICSWX_DOT(%[rs], %[ra])"			\n"
		"	blt+ 2f		# success			\n"
		"	li %[rc],%[eagain]				\n"
		"	bgt- 2f		# eagain			\n"
		"	li %[rc],%[enodev]				\n"
		"2:							\n"
		: [rc] "=&r" (rc), "=m" (*(unsigned *)crb)
		: [rs] "r" (ccw), [ra] "r" (crb),
		  [eagain] "I" (-EAGAIN), [enodev] "I" (-ENODEV)
		: "cr0");

	/* return code is in CR0 */
	return rc;
}

static inline int icswx(u32 ccw, void *crb)
{
	/*
	 * First attempt should push out all dependent data to caches.
	 * This should officially be lwsync, but this seems to the be
	 * portable definition
	 */
	asm volatile (PPC_RELEASE_BARRIER);
	return icswx_raw(ccw, crb);
}

static inline int icswx_retry(u32 ccw, void *crb)
{
	/* on retry there is no need for any barrier */
	return icswx_raw(ccw, crb);
}

#endif /* !_ASM_POWERPC_ICSWX_H */
