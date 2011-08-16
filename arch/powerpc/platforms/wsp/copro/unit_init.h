#ifndef _WSP_COPRO_UNIT_INIT_H
#define _WSP_COPRO_UNIT_INIT_H
/*
 * Copyright 2011 Jimi Xenidis, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * These let us build a set of copro units that can be inited baed on
 * configuration
 */

struct copro_unit_list_entry {
	const char *cu_name;
	int (*cu_fn)(void);
};

#define __copro_unit(name, fn)						\
	struct copro_unit_list_entry __copro_unit_ ## name		\
		__attribute__ ((__section__(".init.copro_unit"))) =	\
	{ .cu_name = # name, .cu_fn = fn, };

#endif	/* _WSP_COPRO_UNIT_INIT_H */
