/*
 *  IBM Power Edge of Network (PowerEN)
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
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <asm/system.h>		/* cli(), *_flags */
#include <linux/uaccess.h>	/* copy_from/to_user */
#include <linux/io.h>		/* copy_from/to_user */
#include <linux/bootmem.h>
#include <linux/log2.h>

#include <platforms/wsp/copro/cop.h>
#include <platforms/wsp/copro/pbic.h>
#include <platforms/wsp/copro/unit_xmlx.h>

#include "mem_utils.h"
#include "pixad.h"
#include "xml_registers.h"

/* VF's imq/qpool/session state memory have already carved from system memory
 * in xmlx driver initialization. This function just assigned them.
 */
static void assign_vf_memory(struct pixad_copro *copro_info)
{
	int i;
	const u64 phys_base = copro_info->phys_transient;
	const u64 virt_base = copro_info->virt_transient;

	for (i = 0; i < ARRAY_SIZE(copro_info->vf_info); i++) {
		struct pixad_xml_vf *vf = &copro_info->vf_info[i];
		vf->num_users = 0;

		vf->size_qpool = XMLX_QCODE_MEM_SIZE;
		vf->phys_qpool = phys_base + xmlx_qcode_mem_offset(i);
		vf->virt_qpool = virt_base + xmlx_qcode_mem_offset(i);

		vf->size_session = XMLX_FIXED_SESSION_SIZE;
		vf->phys_session = phys_base + xmlx_fixed_session_mem_offset(i);
		vf->virt_session = virt_base + xmlx_fixed_session_mem_offset(i);

		vf->size_imq = XMLX_IMQ_SIZE;
		vf->phys_imq = phys_base + xmlx_imq_offset(i);
		vf->virt_imq = virt_base + xmlx_imq_offset(i);
	}
}

/* Shared between VF of the same XML unit */
static void pixad_init_transient(struct pixad_copro *copro_info)
{
	unsigned int entry_num;
	struct transient_mem *transient_pool;
	int i;

	/*
	 * Each entry is a 4KB page.
	 * First 1MB stores pointers(32 bits) to transient pages
	 */
	entry_num = (copro_info->size_transient - MB(1)) / KB(4);

	transient_pool = (struct transient_mem *)copro_info->virt_transient;

	/* xml workbook: first pointer points to 0x100 and continue 0x101 etc */
	for (i = 0; i < entry_num; i++)
		transient_pool->ptrs[i] = TRANSIENT_PTR_BASE + i;
}

static void pixad_init_imq(struct pixad_copro *copro_info)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(copro_info->vf_info); i++)
		memset((void *)copro_info->vf_info[i].virt_imq, 0,
					copro_info->vf_info[i].size_imq);
}

static int pixad_init_qpool(struct pixad_copro *copro_info, int index)
{
	struct xml_qm_t *xml_vf_qm;
	int i, j, free_pool_seq;
	u32 k, qm_area_c_seq;

	if (!IS_ALIGNED(copro_info->vf_info[index].virt_qpool, MB(32))) {
		pixad_err("ALIGN ERROR\n");
		return -1;
	}

	xml_vf_qm = (struct xml_qm_t *)copro_info->vf_info[index].virt_qpool;

	memset(xml_vf_qm->qcode_memory, 0, sizeof(xml_vf_qm->qcode_memory));

	/*
	 * Prefix/local/URI free pool initialization: write serial qcode number
	 */

	for (i = 0; i < ARRAY_SIZE(xml_vf_qm->free_pool); i++) {
		const int pool_size = ARRAY_SIZE(xml_vf_qm->free_pool[i]);

		for (j = 0, free_pool_seq = 1; j < pool_size - 2;
							j++, free_pool_seq++) {
			xml_vf_qm->free_pool[i][j] = free_pool_seq;
		}
		xml_vf_qm->free_pool[i][j] = 0;
		xml_vf_qm->free_pool[i][j+1] = 0;
	}

	/* Expansion free pool initialization
	 * 0x18001: first pointer to (3MB+32)%32
	 * 0x68000: 13MB / 32B = 0x68000.
	 */
	for (k = 0, qm_area_c_seq = 0x18001; k < 0x68000; k++, qm_area_c_seq++)
		xml_vf_qm->expansion_free_pool[k] = qm_area_c_seq;

	return 0;
}

static void pixad_init_session(struct pixad_xml_vf *vf)
{
	int i;
	int num_entries;
	struct state_session_entry *sessions;

	/*
	 * Set session state to 2 in order to work around HW175418 bug.
	 */
	sessions = (struct state_session_entry *) vf->virt_session;
	num_entries = vf->size_session / sizeof(struct state_session_entry);
	for (i = 0; i < num_entries; i++)
		sessions[i].status = 2;
}

static void pixad_init_register_session(struct pixad_copro *copro_info,
	int index)
{
	void *xml_mmio_addr;
	int shift;
	u64 value;

	xml_mmio_addr = copro_info->copro_unit->mmio_addr;
	out_be64(xml_mmio_addr + XML_MMIO_FIXED_SESS_MEM_BASE(index),
				copro_info->vf_info[index].phys_session);

	/* Bits 34:39 (bit 0 is most significant; bit 63 is least significant)
	 * Memory size in units of 16 MB:
	 * 111111   1 entry (216 session IDs are supported). 16 MB allocated
	 * 111110   2 entries (217 session IDs are supported). 32 MB allocated
	 * 111100   4 entries (217 session IDs are supported). 64 MB allocated
	 * 111000   8 entries (217 session IDs are supported). 128 MB allocated
	 * 110000   16 entries (217 session IDs are supported). 256 MB allocated
	 * 100000   32 entries (221 session IDs are supported). 512 MB allocated
	 * 000000   64 entries (222 session IDs are supported). 1 GB allocated
	 */
	shift = ilog2(XMLX_FIXED_SESSION_SIZE);
	value = (FIXED_SESSION_SIZE_BASE << shift) & FIXED_SESSION_SIZE_MASK;
	out_be64(xml_mmio_addr + XML_MMIO_FIXED_SESS_MEM_SIZE(index), value);
}

/* REGISTERING QPOOL */
static void pixad_init_register_qpool(struct pixad_copro *copro_info,
	int index)
{
	void *xml_mmio_addr = copro_info->copro_unit->mmio_addr;
	out_be64(xml_mmio_addr + XML_MMIO_QPOOL_MEM_BASE(index),
					copro_info->vf_info[index].phys_qpool);
}

/* REGISTERING IMQ */
static void pixad_init_register_imq(struct pixad_copro *copro_info,
	int index)
{
	void *xml_mmio_addr;

	xml_mmio_addr = copro_info->copro_unit->mmio_addr;

	out_be64(xml_mmio_addr + XML_MMIO_IMQ_BASE(index),
					copro_info->vf_info[index].phys_imq);

	out_be64(xml_mmio_addr + XML_MMIO_IMQ_SIZE(index), IMQ_MAX_INDEX_MASK);
}

static int pixad_init_register_transient(struct pixad_copro *copro_info)
{
	int shift;
	u64 value;
	void *xml_mmio_addr = copro_info->copro_unit->mmio_addr;

	out_be64(xml_mmio_addr + XML_MMIO_TRANSIENT_BASE,
						copro_info->phys_transient);

	shift = ilog2(XMLX_TRANSIENT_BUFFLET_SIZE);

	/*
	 * Bit 33:35: (bit 0 is most significant; bit 63 is least significant)
	 * The memory size in units of 256 MB follow:
	 * 111		1 entry. 256 MB allocated.
	 * 110		2 entries. 512 MB allocated.
	 * 100		4 entries. 1 GB allocated.
	 */
	value = (TRANSIENT_SIZE_BASE << shift) & TRANSIENT_SIZE_MASK;
	out_be64(xml_mmio_addr + XML_MMIO_TRANSIENT_SIZE, value);
	return 0;
}

int pixad_init_xml_unit(struct pixad_copro *copro_info)
{
	if (!IS_ALIGNED(copro_info->phys_transient,
					XMLX_TRANSIENT_BUFFLET_ALIGNMENT)) {
		pixad_err("ALIGN ERROR\n");
		return -EINVAL;
	}

	/* Write 0x20 to ctrl register to clear hard-reset bit */
	out_be64(copro_info->copro_unit->mmio_addr + XML_MMIO_CTRL_REG, 0x020);
	pixad_init_transient(copro_info);
	pixad_init_register_transient(copro_info);

	return 0;
}

/*
 * Initialize and Register all the memory location for a VF:
 * @param[in/out] copro_info: the target unit data structure
 * @param[int] index:      the target VF inside of the selected unit
 */
void pixad_init_vf(struct pixad_copro *copro_info, int index)
{
	pixad_debug("init imq/qpool/session mem and write address to register");
	pixad_init_register_imq(copro_info, index);
	pixad_init_qpool(copro_info, index);
	pixad_init_register_qpool(copro_info, index);
	pixad_init_session(&copro_info->vf_info[index]);
	pixad_init_register_session(copro_info, index);
	pixad_debug("done init VF %d\n", index);
}

void init_xml_data_structures(unsigned int copro_num,
			      struct pixad_copro *copro_info)
{
	int i;

	for (i = 0; i < copro_num; i++) {
		copro_info[i].copro_unit = wsp_xml_get_copro_device(i);
		copro_info[i].phys_transient = wsp_xml_get_mem_addr_index(i);
		copro_info[i].virt_transient = (u64)phys_to_virt(
						copro_info[i].phys_transient);
		copro_info[i].size_transient = MB(256);

		pixad_init_transient(&copro_info[i]);

		assign_vf_memory(&copro_info[i]);

		copro_info[i].vf_info[i].max_index_imq = IMQ_MAX_INDEX;

		/* Init IMQ data to 0x0 only when whole driver init */
		pixad_init_imq(&copro_info[i]);
	}
}

static inline void set_pbic_threshold_ctrl_reg(void *pbic_mmio, int vf_id)
{
	u64 reg_val = XMLPBIC_PID_MATCH | XMLPBIC_PID(current->mm->context.id);
	out_be64(pbic_mmio + PBIC_MMIO_THRESHOLD_CTRL(vf_id), reg_val);
}

static inline void set_xml_vf_pid_lpid_reg(u64 mmio, int vf_id)
{
	u64 value = XMLPDLP_VALID | XMLPDLP_PID(current->mm->context.id);
	out_be64((u64 *) (mmio + XML_MMIO_VF_PID_LPID(vf_id)), value);
}

void run_vf(struct pixad_copro *copro_info, int vf_id)
{
	u64 xml_mmio_addr;
	u64 res;
	u32 write_ptr;

	set_pbic_threshold_ctrl_reg(copro_info->copro_unit->pbic->mmio_addr,
									vf_id);

	xml_mmio_addr = (u64)copro_info->copro_unit->mmio_addr;

	set_xml_vf_pid_lpid_reg(xml_mmio_addr, vf_id);

	/* Update imq read ptr to current position, ie, current write ptr. */
	write_ptr = xmlreg_get_imq_write_ptr(xml_mmio_addr, vf_id);
	xmlreg_set_imq_read_ptr(xml_mmio_addr, vf_id, write_ptr);

	/* Set qcode watermark */
	xmlreg_set_qcode_watermark(xml_mmio_addr, vf_id, 64UL);

	/* Set free buffer pool watermark */
	xmlreg_set_fbp_watermark(xml_mmio_addr, vf_id, 500, 1000);

	/* VF6: Set the V(n) Go bit */
	out_be64((u64 *)(xml_mmio_addr + XML_MMIO_CTRL_REG), XMLTRL_GO(vf_id));


#ifndef _MAMBO_	/* IBM Mambo simulator */
	/* VF7 Wait on Init_Not_Done(n) cleaning */
	pixad_debug("Checking VF %d init register...\n", vf_id);
	do {
		res = in_be64((u64 *)(xml_mmio_addr+XML_MMIO_INIT_NOT_DONE));
	} while (res & XMLIND_VF(vf_id));
	pixad_debug("VF %d is initialized successfully\n", vf_id);
#endif
}
