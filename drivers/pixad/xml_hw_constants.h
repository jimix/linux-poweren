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
#ifndef _DRIVERS_PIXAD_XML_HW_CONSTANT_H_
#define _DRIVERS_PIXAD_XML_HW_CONSTANT_H_


/*
 * HARDWARE CONSTANTS
 */

#define XML_OF_ADAPTER_COMPAT "ibm,wsp-coprocessor-xmlx"

/* mmio offsets to xml registers */
#define XML_MMIO_CTRL_REG			0x0008
#define XML_MMIO_INIT_NOT_DONE			0x0010
#define XML_MMIO_FIXED_SESS_MEM_BASE(vf)	(0x0018 + ((vf) * 8))
#define XML_MMIO_FIXED_SESS_MEM_SIZE(vf)	(0x0038 + ((vf) * 8))
#define XML_MMIO_QPOOL_MEM_BASE(vf)		(0x0058 + ((vf) * 8))
#define XML_MMIO_TRANSIENT_BASE			0x0078
#define XML_MMIO_TRANSIENT_SIZE			0x0080
#define XML_MMIO_FBP_WATERMARK(vf)		(0x0098 + ((vf) * 8))
#define XML_MMIO_VF_PID_LPID(vf)		(0x00B8 + ((vf) * 8))
#define XML_MMIO_INVALIDATE_CACHES(vf)		(0x00D8 + ((vf) * 8))
#define XML_MMIO_IMQ_BASE(vf)			(0x0128 + ((vf) * 8))
#define XML_MMIO_IMQ_SIZE(vf)			(0x0150 + ((vf) * 8))
#define XML_MMIO_IMQ_READ_PTR(vf)		(0x0178 + ((vf) * 8))
#define XML_MMIO_INT_STATUS(vf)			(0x01F0 + ((vf) * 8))
#define XML_MMIO_QCODE_WATERMARK(vf)		(0x0320 + ((vf) * 8))

/* mmio offsets to PBIC registers */
#define PBIC_MMIO_THRESHOLD_CTRL(vf)		(0x00D0 + ((vf) * 8))
#define PBIC_MMIO_SPILL_QUEUE_START_ADDR(vf)	(0x0150 + ((vf) * 10))
#define PBIC_MMIO_QUEUE_CTRL(vf)		(0x0158 + ((vf) * 10))

/* PBIC spill queue staring address register masks */
#define PAUSE_ADD_MASK				0x8000000000000000UL
#define PAUSE_DRAIN_MASK			0x4000000000000000UL

/* XML open session ctrl bit mask */
#define OPEN_CTRL_QPID_SHIFT	3
#define OPEN_CTRL_QPID_MASK	0x78
#define OPEN_CTRL_QPID(x)	(((x) << OPEN_CTRL_QPID_SHIFT) &\
							OPEN_CTRL_QPID_MASK)
/* XML CRB CCW bit mask */
#define XMLCCW_CT_SHIFT		16
#define XMLCCW_CT_MASK		0x003F0000
#define XMLCCW_CT(x)		(((x) << XMLCCW_CT_SHIFT) & XMLCCW_CT_MASK)
#define XMLCCW_FC_MASK		0x0000001F

/* Software request qcode input dde mask */
#define XMLSWQR_QPID_MASK 0xC0
#define XMLSWQR_QPID_SHIFT 6
#define XMLSWQR_QPID_SWAP(x)	((0 != (x) && 3 != (x)) ? ((x)^3) : (x))
#define XMLSWQR_QPID(x)		(XMLSWQR_QPID_SWAP(x) <<\
					XMLSWQR_QPID_SHIFT & XMLSWQR_QPID_MASK)

#endif /* _DRIVERS_PIXAD_XML_HW_CONSTANT_H_ */
