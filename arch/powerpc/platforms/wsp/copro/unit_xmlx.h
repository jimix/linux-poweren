#ifndef _WSP_COPRO_UNIT_XMLX_H
#define _WSP_COPRO_UNIT_XMLX_H
/*
 * Copyright 2010-2011 Shawn Liang, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

struct copro_unit;

#define XMLX_VF_PER_UNIT		4

/* Size and layout of curved memory for XMLX */
#define XMLX_TRANSIENT_BUFFLET_SIZE	(256UL << 20)
#define XMLX_QCODE_MEM_SIZE		(20UL << 20)
#define XMLX_QCODE_MEM_AILGNED_SIZE	(32UL << 20)
#define XMLX_FIXED_SESSION_SIZE		(16UL << 20)
#define XMLX_IMQ_SIZE			(1UL << 20)

#define XMLX_TANSIENT_BUFFLET_OFFSET	0

#define XMLX_TRANSIENT_BUFFLET_ALIGNMENT	(256UL << 20)

extern struct copro_unit *wsp_xml_get_copro_device(int index);
extern u64 wsp_xml_get_mem_addr_index(unsigned index);
extern u32 wsp_xml_get_num_device(void);


static inline u64 xmlx_qcode_mem_offset(int vf_id)
{
	u64 base =  XMLX_TANSIENT_BUFFLET_OFFSET + XMLX_TRANSIENT_BUFFLET_SIZE;
	return base + (vf_id * XMLX_QCODE_MEM_AILGNED_SIZE);
}

static inline u64 xmlx_fixed_session_mem_offset(int vf_id)
{
	u64 base = xmlx_qcode_mem_offset(XMLX_VF_PER_UNIT);
	return base + (vf_id * XMLX_FIXED_SESSION_SIZE);
}

static inline u64 xmlx_imq_offset(int vf_id)
{
	u64 base = xmlx_fixed_session_mem_offset(XMLX_VF_PER_UNIT);
	return base + (vf_id * XMLX_IMQ_SIZE);
}

#endif /* !_WSP_COPRO_UNIT_XMLX_H */
