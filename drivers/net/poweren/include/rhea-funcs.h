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

#ifndef _NET_POWEREN_RHEA_FUNCS_H_
#define _NET_POWEREN_RHEA_FUNCS_H_

#include <rhea-linux.h>
#include <hea-bits.h>
#include <rhea.h>

#define ALIGN_UP(a, s) \
	(((unsigned long long)(a) + ((unsigned long long)(s) - 1)) & \
	(~((unsigned long long)(s) - 1)))

#define ALIGN_DOWN(a, s) ((unsigned long long)(a) & \
			 (~((unsigned long long)(s) - 1)))

#define ALIGN_TO(a, t) ALIGN_UP((a), __alignof__(t))


#define RHEA_FIND_FREE_ID(struct_instance, array, max_id, return_id)	\
	do {								\
		typeof(return_id) _id = -1;				\
		for (_id = 0; _id < max_id; ++_id)			\
			if (NULL == (struct_instance).array[_id])	\
				break;					\
		return_id = _id;					\
	} while (0)

#define RHEA_VALID_INSTANCE_CHECK(array, id) \
		((ARRAY_SIZE(array) > id && \
		 (NULL != array[id]) ? 1 : 0))


static inline int on_mambo(void)
{
#ifdef CONFIG_PPC_MAMBO_A2
#define MSR_MAMBO	0x20000000	/* 34 */
	return (mfmsr() & MSR_MAMBO) ? 1 : 0;
#else
	return 0;
#endif
}

/* computes the logarithmus dualis */
static inline int hea_ld(u64 size)
{
	ulong count = 0;

	if (0 == size)
		return -EPERM;

	/* finds only 2^x */
	while (size > 1) {
		size >>= 1;
		++count;
	}

	return count;
}

#endif /* _NET_POWEREN_RHEA_FUNCS_H_ */
