/*
 * IBM Power Edge of Network (PowerEN)
 *
 * Copyright 2010-2011 Massimiliano Meneghin, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _DRIVERS_PIXAD_XML_REGISTERS_H_
#define _DRIVERS_PIXAD_XML_REGISTERS_H_

#include "xml_hw_constants.h"

/* XML control register */
#define XMLTRL_TDVF0		0x8000000000000000UL /* take down VF 0 */
#define XMLTRL_TDVF1		0x4000000000000000UL /* take down VF 1 */
#define XMLTRL_TDVF3		0x2000000000000000UL /* take down VF 2 */
#define XMLTRL_TDVF4		0x1000000000000000UL /* take down VF 3 */
#define XMLTRL_TDVF(vf)		(0x1UL << (63 - (vf)))
#define XMLTRL_HD_RST		0x0000000000000020UL /* hard reset */
#define XMLTRL_SF_RST		0x0000000000000010UL /* soft reset */
#define XMLTRL_GO0		0x0000000000000008UL /* go VF 0 */
#define XMLTRL_GO1		0x0000000000000004UL /* go VF 1 */
#define XMLTRL_GO2		0x0000000000000002UL /* go VF 2 */
#define XMLTRL_GO3		0x0000000000000001UL /* go VF 3 */
#define XMLTRL_GO(vf)		(0x1UL << (3 - vf))

/* XML Invalidate Caches Register */
#define XMLINVC_REQ		0x0000000000000001UL

/* XML Init not done register */
#define XMLIND_VF0		0x0000000000000008UL
#define XMLIND_VF1		0x0000000000000004UL
#define XMLIND_VF2		0x0000000000000002UL
#define XMLIND_VF3		0x0000000000000001UL
#define XMLIND_VF(vf)		(0x1UL << (3 - vf))

/* XML free buffer pool watermark register */
#define XMLFWK_RED_MASK		0x00000000000FFC00UL
#define XMLFWK_RED_SHIFT	10
#define XMLFWK_RED(x)		(((x) << XMLFWK_RED_SHIFT) & XMLFWK_RED_MASK)
#define XMLFWK_RED_GET(m1)	(((m1) & XMLFWK_RED_MASK) >> XMLFWK_RED_SHIFT)
#define XMLFWK_YELLOW		0x00000000000003FFUL

/* XML qcode watermark register */
#define XMLQWMKRK_PFX_RED_MASK		0xFF00000000000000UL
#define XMLQWMKRK_PFX_RED_SHIFT		56
#define XMLQWMKRK_PFX_RED(x)		(((x) << XMLQWMKRK_PFX_RED_SHIFT)\
						& XMLQWMKRK_PFX_RED_MASK)
#define XMLQWMKRK_PFX_RED_GET(m1)	(((m1) & XMLQWMKRK_PFX_RED_MASK)\
						>> XMLQWMKRK_PFX_RED_SHIFT)
#define XMLQWMKRK_PFX_YELLOW_MASK	0x00FF000000000000UL
#define XMLQWMKRK_PFX_YELLOW_SHIFT	48
#define XMLQWMKRK_PFX_YELLOW(x)		(((x) << XMLQWMKRK_PFX_YELLOW_SHIFT)\
						& XMLQWMKRK_PFX_YELLOW_MASK)
#define XMLQWMKRK_PFX_YELLOW_GET(m1)	(((m1) & XMLQWMKRK_PFX_YELLOW_MASK)\
						>> XMLQWMKRK_PFX_YELLOW_SHIFT)
#define XMLQWMKRK_URI_RED_MASK		0x0000FF0000000000UL
#define XMLQWMKRK_URI_RED_SHIFT		40
#define XMLQWMKRK_URI_RED(x)		(((x) << XMLQWMKRK_URI_RED_SHIFT)\
						& XMLQWMKRK_URI_RED_MASK)
#define XMLQWMKRK_URI_RED_GET(m1)	(((m1) & XMLQWMKRK_URI_RED_MASK)\
						>> XMLQWMKRK_URI_RED_SHIFT)
#define XMLQWMKRK_URI_YELLOW_MASK	0x000000FF00000000UL
#define XMLQWMKRK_URI_YELLOW_SHIFT	32
#define XMLQWMKRK_URI_YELLOW(x)		(((x) << XMLQWMKRK_URI_YELLOW_SHIFT)\
						& XMLQWMKRK_URI_YELLOW_MASK)
#define XMLQWMKRK_URI_YELLOW_GET(m1)	(((m1) & XMLQWMKRK_URI_YELLOW_MASK)\
						>> XMLQWMKRK_URI_YELLOW_SHIFT)
#define XMLQWMKRK_LOC_RED_MASK		0x00000000FF000000UL
#define XMLQWMKRK_LOC_RED_SHIFT		24
#define XMLQWMKRK_LOC_RED(x)		(((x) << XMLQWMKRK_LOC_RED_SHIFT)\
						& XMLQWMKRK_LOC_RED_MASK)
#define XMLQWMKRK_LOC_RED_GET(m1)	(((m1) & XMLQWMKRK_LOC_RED_MASK)\
						>> XMLQWMKRK_LOC_RED_SHIFT)
#define XMLQWMKRK_LOC_YELLOW_MASK	0x0000000000FF0000UL
#define XMLQWMKRK_LOC_YELLOW_SHIFT	16
#define XMLQWMKRK_LOC_YELLOW(x)		(((x) << XMLQWMKRK_LOC_YELLOW_SHIFT)\
						& XMLQWMKRK_LOC_YELLOW_MASK)
#define XMLQWMKRK_LOC_YELLOW_GET(m1)	(((m1) & XMLQWMKRK_LOC_YELLOW_MASK)\
						>> XMLQWMKRK_LOC_YELLOW_SHIFT)
#define XMLQWMKRK_EXP_RED_MASK		0x000000000000FF00UL
#define XMLQWMKRK_EXP_RED_SHIFT		8
#define XMLQWMKRK_EXP_RED(x)		(((x) << XMLQWMKRK_EXP_RED_SHIFT)\
						& XMLQWMKRK_EXP_RED_MASK)
#define XMLQWMKRK_EXP_RED_GET(m1)	(((m1) & XMLQWMKRK_EXP_RED_MASK)\
						>> XMLQWMKRK_EXP_RED_SHIFT)

#define XMLQWMKRK_EXP_YELLOW_MASK	0x00000000000000FFUL
#define XMLQWMKRK_EXP_YELLOW(x)		(((x) & XMLQWMKRK_EXP_YELLOW_MASK))

/* XML IMQ read pointer register */
#define XMLIMRA_RD_PTR_MASK		0x00000000003FFFF0UL
#define XMLIMRA_RD_PTR_SHIFT		4
#define XMLIMRA_RD_PTR(x)		(((x) << XMLIMRA_RD_PTR_SHIFT)\
						& XMLIMRA_RD_PTR_MASK)
#define	XMLIMRA_RD_PTR_GET(m1)		(((m1) & XMLIMRA_RD_PTR_MASK)\
						>> XMLIMRA_RD_PTR_SHIFT)


/* XML interrupt status register */
#define XMLIS_ALMOST_FULL		0x0000000400000000UL
#define XMLIS_FULL			0x0000000200000000UL
#define XMLIS_OVERFLOW			0x0000000100000000UL
#define XMLIS_VALID_TOGGLE		0x0000000000400000UL
#define XMLIS_WR_PTR_MASK		0x00000000000ffff0UL
#define XMLIS_WR_PTR_SHIFT		4
#define XMLIS_WR_PTR(x)			(((x) << XMLIS_WR_PTR_SHIFT)\
						& XMLIS_WR_PTR_MASK)
#define XMLIS_WR_PTR_GET(m1)		(((m1) & XMLIS_WR_PTR_MASK)\
						>> XMLIS_WR_PTR_SHIFT)
#define XMLIS_STATE_MASK		0x0000000000000006UL
#define XMLIS_STATE_SHIFT		1
#define XMLIS_STATE(x)			(((x) << XMLIS_STATE_SHIFT)\
						& XMLIS_STATE_MASK)
#define XMLIS_STATE_GET(m1)		(((m1) & XMLIS_STATE_MASK)_\
						>> XMLIS_STATE_SHIFT)
#define XMLIS_INT_PENDING		0x0000000000000001UL


/* XML VFn PID LPID register */
#define XMLPDLP_VALID		0x0000000004000000UL
#define XMLPDLP_LE		0x0000000002000000UL
#define XMLPDLP_AS		0x0000000001000000UL
#define XMLPDLP_GS		0x0000000000800000UL
#define XMLPDLP_PR		0x0000000000400000UL
#define XMLPDLP_PID_MASK	0x00000000003FFF00UL
#define XMLPDLP_PID_SHIFT	8
#define XMLPDLP_PID(x)		(((x) << XMLPDLP_PID_SHIFT) & XMLPDLP_PID_MASK)
#define XMLPDLP_PID_GET(m1)	(((m1) & XMLPDLP_PID_MASK) >> XMLPDLP_PID_SHIFT)
#define XMLPDLP_LPID		0x00000000000000FFUL

/* XML's PBIC threshold control register */
#define XMLPBIC_CUR_COUNT	0xFFFF000000000000UL
#define XMLPBIC_THRESHOLD_MASK	0x0000FFFF00000000UL
#define XMLPBIC_THRESHOLD_SHIFT	32
#define XMLPBIC_THRESHOLD(x)	(((x) << XMLPBIC_THRESHOLD_SHIFT) &\
						XMLPBIC_THRESHOLD_MASK)
#define XMLPBIC_PID_MATCH	0x0000000080000000UL
#define XMLPBIC_LPID_MATCH	0x0000000040000000UL
#define XMLPBIC_PID_MASK	0x0000000000FFFC00UL
#define XMLPBIC_PID_SHIFT	10
#define XMLPBIC_PID(x)		(((x) << XMLPBIC_PID_SHIFT) & XMLPBIC_PID_MASK)
#define XMLPBIC_LPID		0x00000000000000FFUL


static inline u32 xmlreg_get_imq_read_ptr(u64 mmio, int vf_id)
{
	u64 val;

	val = in_be64((u64 *)(mmio + XML_MMIO_IMQ_READ_PTR(vf_id)));
	return XMLIMRA_RD_PTR_GET(val);
}

static inline u32 xmlreg_get_imq_write_ptr(u64 mmio, int vf_id)
{
	u64 val;

	val = in_be64((u64 *)(mmio + XML_MMIO_INT_STATUS(vf_id)));
	return XMLIS_WR_PTR_GET(val);
}

static inline void xmlreg_set_imq_read_ptr(u64 mmio, int vf_id, u32 read_index)
{
	u64 val = XMLIMRA_RD_PTR(read_index); /* all other bits are reserved */
	out_be64((u64 *)(mmio + XML_MMIO_IMQ_READ_PTR(vf_id)), val);
}

static inline int xmlreg_get_imq_valid_bit(u64 mmio, int vf_id)
{
	u64 val = in_be64((u64 *)(mmio + XML_MMIO_INT_STATUS(vf_id)));
	return (val & XMLIS_VALID_TOGGLE) ? 1 : 0;
}

static inline void xmlreg_set_fbp_watermark(u64 mmio_addr, int vf_id,
						u16 red_wmk, u16 yellow_wmk)
{
	u64 value = XMLFWK_RED(red_wmk) | (XMLFWK_YELLOW & yellow_wmk);
	out_be64((u64 *)(mmio_addr + XML_MMIO_FBP_WATERMARK(vf_id)), value);
}

static inline void xmlreg_set_qcode_watermark(u64 mmio_addr, int vf_id, u64 wmk)
{
	u64 value = XMLQWMKRK_PFX_YELLOW(wmk) | XMLQWMKRK_LOC_YELLOW(wmk) |
			XMLQWMKRK_URI_YELLOW(wmk) | XMLQWMKRK_EXP_YELLOW(wmk);

	out_be64((u64 *)(mmio_addr + XML_MMIO_QCODE_WATERMARK(vf_id)), value);
}

#endif /* _DRIVERS_PIXAD_XML_REGISTERS_H_ */
