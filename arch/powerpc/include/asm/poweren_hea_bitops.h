/*
 * Copyright (C) 2011 IBM Corporation.
 * Author:	Davide Pasetto <pasetto_davide@ie.ibm.com>
 *		Karol Lynch <karol_lynch@ie.ibm.com>
 *		Kay Muller <kay.muller@ie.ibm.com>
 *		Jimi Xenidis <jimix@watson.ibm.com>
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

#ifndef _ASM_POWEREN_HEA_BITOPS_H_
#define _ASM_POWEREN_HEA_BITOPS_H_

#include <linux/types.h>

#define __HEA_MASK(s, e) ((~0ULL << (63 - (e))) & (~0ULL >> (s)))
#define __HEA_MASK_SET(v, s, e) \
	((((__u64)(v)) << (63 - (e))) & __HEA_MASK(s, e))
#define __HEA_MASK_GET(v, s, e) \
	((((__u64)(v)) & __HEA_MASK(s, e)) >> (63 - (e)))

/* make bit */
static inline __u8 hea_u8_bit(int b) { return __HEA_MASK(b + 56, b + 56); }
static inline __u16 hea_u16_bit(int b) { return __HEA_MASK(b + 48, b + 48); }
static inline __u32 hea_u32_bit(int b) { return __HEA_MASK(b + 32, b + 32); }
static inline __u64 hea_u64_bit(int b) { return __HEA_MASK(b, b); }

/* test bit */
static inline int hea_test_u8_bit(__u8 v, int b)
{
	return !!(v & hea_u8_bit(b));
}
static inline int hea_test_u16_bit(__u16 v, int b)
{
	return !!(v & hea_u16_bit(b));
}
static inline int hea_test_u32_bit(__u32 v, int b)
{
	return !!(v & hea_u32_bit(b));
}
static inline int hea_test_u64_bit(__u64 v, int b)
{
	return !!(v & hea_u64_bit(b));
}

/* set bits */
static inline __u8 hea_set_u8_bit(__u8 v, int b)
{
	return v | hea_u8_bit(b);
}
static inline __u16 hea_set_u16_bit(__u16 v, int b)
{
	return v | hea_u16_bit(b);
}
static inline __u32 hea_set_u32_bit(__u32 v, int b)
{
	return v | hea_u32_bit(b);
}
static inline __u64 hea_set_u64_bit(__u64 v, int b)
{
	return v | hea_u64_bit(b);
}

/* clr bits */
static inline __u8 hea_clr_u8_bit(__u8 v, int b)
{
	return v & ~hea_u8_bit(b);
}
static inline __u16 hea_clr_u16_bit(__u16 v, int b)
{
	return v & ~hea_u16_bit(b);
}
static inline __u32 hea_clr_u32_bit(__u32 v, int b)
{
	return v & ~hea_u32_bit(b);
}
static inline __u64 hea_clr_u64_bit(__u64 v, int b)
{
	return v & ~hea_u64_bit(b);
}

/* get bitfield */
static inline __u8 hea_get_u8_bits(__u8 val, int start, int end)
{
	return __HEA_MASK_GET(val, start + 56, end + 56);
}
static inline __u16 hea_get_u16_bits(__u16 val, int start, int end)
{
	return __HEA_MASK_GET(val, start + 48, end + 48);
}
static inline __u32 hea_get_u32_bits(__u32 val, int start, int end)
{
	return __HEA_MASK_GET(val, start + 32, end + 32);
}
static inline __u64 hea_get_u64_bits(__u64 val, int start, int end)
{
	return __HEA_MASK_GET(val, start, end);
}

/* set bitfield */
static inline __u64 hea_set_u64_bits(__u64 oval, __u64 nval, int start, int end)
{
	__u64 set = __HEA_MASK_SET(nval, start, end);
	__u64 mask = __HEA_MASK_SET(~0ull, start, end);
	return set | (oval & ~mask);
}

static inline __u8 hea_set_u8_bits(__u8 oval, __u8 nval, int start, int end)
{
	return hea_set_u64_bits(oval, nval, start + 56, end + 56);
}
static inline __u16 hea_set_u16_bits(__u16 oval, __u16 nval, int start, int end)
{
	return hea_set_u64_bits(oval, nval, start + 48, end + 48);
}
static inline __u32 hea_set_u32_bits(__u32 oval, __u32 nval, int start, int end)
{
	return hea_set_u64_bits(oval, nval, start + 32, end + 32);
}

#endif /* _ASM_POWEREN_HEA_BITOPS_H_ */
