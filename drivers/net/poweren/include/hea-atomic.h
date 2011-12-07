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

#ifndef _NET_POWEREN_HEA_ATOMICS_H_
#define _NET_POWEREN_HEA_ATOMICS_H_

#include <linux/atomic.h>

/********************* ATOMIC *********************************/

typedef atomic_t hea_atomic_t;

static inline void hea_atomic_set32(hea_atomic_t *ptr, unsigned int value)
{
	atomic_set(ptr, value);
}

static inline unsigned int hea_atomic_get32(hea_atomic_t *ptr)
{
	return atomic_read(ptr);
}

static inline void hea_atomic_inc32(hea_atomic_t *ptr)
{
	atomic_inc(ptr);
}

static inline void hea_atomic_dec32(hea_atomic_t *ptr)
{
	atomic_dec(ptr);
}

static inline void hea_atomic_add32(hea_atomic_t *ptr, unsigned int value)
{
	atomic_add(value, ptr);
}

static inline void hea_atomic_sub32(hea_atomic_t *ptr, unsigned int value)
{
	atomic_sub(value, ptr);
}

#endif /* _NET_POWEREN_HEA_ATOMICS_H_ */
