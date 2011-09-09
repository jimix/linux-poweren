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
#include <asm/icswx.h>
#include <platforms/wsp/copro/cop.h>
#include <linux/delay.h>
#include <linux/mman.h>

#include "mem_utils.h"
#include "pixad.h"
#include "xml_registers.h"
#include "xml_icws_structures.h"

static inline void set_open_session_msg(struct xml_open_msg *msg, u32 sess_id,
								u8 qpool_id)
{
	msg->session_id = sess_id;
	msg->msg_type = MSG_OPEN_SESSION;
	msg->ctrl |= OPEN_CTRL_QPID(qpool_id);
}

static inline void set_close_session_msg(struct xml_close_msg *msg, u32 sess_id)
{
	msg->session_id = sess_id;
	msg->msg_type = MSG_CLOSE_SESSION;
}

static inline void set_crb_immediate_msg(struct xml_crb *m, u8 func_code)
{
	m->ccw |= XMLCCW_CT(CT_XML);
	m->ccw |= (func_code & XMLCCW_FC_MASK);
	m->flags = DIRECT_MESSAGE_1_FLAG;
}

static inline void set_crb_qpool_remove(struct xml_crb *m)
{
	m->ccw |= XMLCCW_CT(CT_XML);
	m->flags = CRB_FLAG_DELETE_QPOOL;
}

static void set_in_xml_buffer_msg(struct xml_input_msg *msg,
			u32 ses_id, void *buff, u16 len)
{
	msg->session_id = ses_id;
	msg->msg_type = MSG_XML_BUFFER;
	msg->length = len;
	msg->buffer = (u64)buff;
}

static inline void set_retire_qpool_msg(struct xml_retire_qpool_msg *msg,
								int qpid)
{
	msg->msg_type = MSG_RETIRE_QPOOL;
	msg->qpool_id = qpid;
}

static int xml_send(u32 ccw, void *crb)
{
	int rc;
	unsigned long timeout;

	timeout = jiffies + (5 * HZ);

	rc = icswx(ccw, crb);
	while (rc == -EAGAIN) {
		if (time_after(jiffies, timeout)) {
			pixad_debug("crb is not accepted by xml cop\n");
			return -EBUSY;
		}
		usleep_range(500, 1000);
		rc = icswx_retry(ccw, crb);
	}

	if (rc != 0)
		cop_debug("crb to xml returned %d\n", rc);

	return rc;
}

/*
 * Sends a crb message to close a xml session
 */
int send_close_msg(unsigned int vf_id, unsigned int session_id)
{
	int out;
	struct xml_crb *crb;

	crb = cop_cxb_alloc(GFP_KERNEL);
	if (!crb)
		return -ENOMEM;

	set_crb_immediate_msg(crb, ENGINE_QUEUE_BASE_ID + vf_id);
	set_close_session_msg(&crb->msg_1.close, session_id);

	out = xml_send(crb->ccw, crb);
	cop_cxb_free(crb);

	return out;
}

int send_sync_crb(unsigned int vf_id, const char *flush_text,
				unsigned int text_len, int session_id)
{
	int out;
	struct xml_crb *crb = cop_cxb_alloc(GFP_KERNEL);

	if (!crb)
		return -ENOMEM;

	set_crb_immediate_msg(crb, vf_id + ENGINE_QUEUE_BASE_ID);
	crb->flags = DIRECT_MESSAGE_3_FLAG;

	set_open_session_msg(&crb->msg_1.open, session_id, 0);
	set_in_xml_buffer_msg(&crb->msg_2.xml_buffer, session_id,
						(void *)flush_text, text_len);
	set_close_session_msg(&crb->msg_3.close, session_id);

	out = xml_send(crb->ccw, crb);
	cop_cxb_free(crb);

	return out;
}

static int consume_imq(struct pixad_copro *copro_info, int unit_id, int vf_id,
						u32 *session_id, u8 *type)
{
	struct xml_generic_imqe *imq_head;
	struct xml_generic_imqe *msg;
	struct pixad_xml_vf *vf;
	int imq_size;
	u32 read_index;
	u32 write_index;

	vf = &copro_info[unit_id].vf_info[vf_id];
	imq_size = vf->max_index_imq;

	read_index = xmlreg_get_imq_read_ptr(
			(u64)copro_info[unit_id].copro_unit->mmio_addr, vf_id);
	write_index = xmlreg_get_imq_write_ptr(
			(u64)copro_info[unit_id].copro_unit->mmio_addr, vf_id);

	if (read_index == write_index)
		return -1;

	imq_head = (struct xml_generic_imqe *)vf->virt_imq;
	msg = imq_head + read_index;

	*type = msg->type;
	*session_id = msg->id;

	/* update read ptr */

	if (read_index + 1 == imq_size)
		read_index = 0;
	else
		read_index += 1;

	xmlreg_set_imq_read_ptr((u64)copro_info[unit_id].copro_unit->mmio_addr,
							vf_id, read_index);

	return 0;
}


int wait_session_closed_imqe(struct pixad_copro *copro_info,
			unsigned int unit_id, unsigned int vf_id, u32 sid)
{
	u32 session_id;
	u8  imqe_type;
	unsigned long timeout;
	int rc;
	timeout = jiffies + (5 * HZ);

	while (1) {
		rc = consume_imq(copro_info, unit_id, vf_id, &session_id,
								&imqe_type);

		if (rc == 0 && (imqe_type & 0x40) &&
				(session_id & XMLSESSION_ID_MASK) == sid)
			return 0;

		if (time_after(jiffies, timeout)) {
			pixad_debug("Timeout: session %d not closed\n", sid);
			return -1;
		}

		usleep_range(500, 1000);
	}
}

int wait_imqe(struct pixad_copro *copro_info,
		       unsigned int unit_id, unsigned int vf_id, int msgType)
{
	u32 session_id;
	u8 imqe_type;
	unsigned long timeout;
	int rc;

	timeout = jiffies + (5 * HZ);

	while (1) {
		rc = consume_imq(copro_info, unit_id, vf_id, &session_id,
								&imqe_type);

		if (rc == 0 && imqe_type == msgType)
			return 0;

		if (time_after(jiffies, timeout)) {
			pixad_debug("Timeout: wait on imq msg 0x%x\n", msgType);
			return -1;
		}

		usleep_range(500, 1000);
	}
}

static unsigned int get_qname_hashcode(const unsigned char *qname,
				       int qname_len)
{
	unsigned int hascode = qname_len;
	while (qname_len > 3) {
		hascode += qname[0];
		hascode += qname[1] << 8;
		hascode += qname[2];
		hascode += qname[3] << 8;

		qname += 4;
		qname_len -= 4;
	}
	switch (qname_len) {
	case 3:
		hascode += qname[2];
	case 2:
		hascode += qname[1] << 8;
	case 1:
		hascode += qname[0];
	}
	return hascode;
}

static inline void set_qcode_req_dde_in(struct xml_qcode_request *req, u8 type,
		u8 qpid, const char *name, u16 len)
{
	int i;

	req->len = len;
	req->type = type;
	req->qpid = XMLSWQR_QPID(qpid);
	req->hash = get_qname_hashcode((unsigned char *) name, len);
	for (i = 0; i < len; i++)
		req->qname[i] = name[i];
}

/* qname_len: length without null termination */
int request_qcode(const char *qname, int qname_len, enum xml_qcode_type type,
	int qpid, int ci, struct pixad_user_proc *target)
{
	void *dde_in_uvaddr = NULL;
	void *dde_in_kvaddr = NULL;
	void *dde_out_uvaddr = NULL;
	void *dde_out_kvaddr = NULL;

	int dde_in_size = 0;
	int dde_out_size = 0;
	int out = 0;
	volatile struct xml_qcode_response *res = NULL;
	unsigned long timeout;

	struct xml_crb_req_qcode *crb =
		(struct xml_crb_req_qcode *)cop_cxb_alloc(GFP_KERNEL);
	if (!crb)
		return -ENOMEM;
	memset(crb, 0, sizeof(struct xml_crb_req_qcode));

	dde_in_size = qname_len + sizeof(struct xml_qcode_request);
	if (mpool_alloc(&target->mempool, dde_in_size, 64, &dde_in_uvaddr,
			&dde_in_kvaddr) != 0)
		return -ENOMEM;
	memset(dde_in_kvaddr, 0, dde_in_size);

	dde_out_size = sizeof(struct xml_qcode_response);
	if (mpool_alloc(&target->mempool, dde_out_size, 16, &dde_out_uvaddr,
			&dde_out_kvaddr) != 0)
		return -ENOMEM;
	memset(dde_out_kvaddr, 0, dde_out_size);

	/* fill in the 'input DDE' and CRB structure */
	set_qcode_req_dde_in((struct xml_qcode_request *) dde_in_kvaddr, type,
							qpid, qname, qname_len);

	crb->byte_count = dde_in_size;
	crb->dde_count = 0;
	crb->pool_index = 0;
	crb->in_dde_addr = (u64)dde_in_uvaddr;
	crb->out_dde_addr = (u64)dde_out_uvaddr;
	crb->flags = 0x10;
	crb->ccw = XMLCCW_CT(CT_XML);

	/* send out crb. */
	out = xml_send(crb->ccw, crb);
	if (out != 0) {
		pixad_err("send crb error: %d\n", out);
		goto exit;
	}

	/* wait 5 seconds most for mailbox(dde out) completion */
	res = (struct xml_qcode_response *) dde_out_kvaddr;

	timeout = jiffies + (5 * HZ);
	while (!(res->done & QCRES_DONE_MASK)) {
		if (time_after(jiffies, timeout))
			break;
		usleep_range(500, 1000);
	}

	/* check mailbox for : DONE, ERRs, QCODE(can't be zero)*/
	if (!(res->done & QCRES_DONE_MASK)) {
		pixad_err("no mailbox response from XML\n");
		out = -EIO;
	} else if (res->result & QCRES_RESULT_ERR_MASK) {
		pixad_err("qcode request failed\n");
		out = -EINVAL;
	} else {
		if ((0 == res->qcode) || (0xFFFF == res->qcode)) {
			pixad_err("SWQR mailbox done. but wrong qcode = %hu\n"
								, res->qcode);
			out = -EINVAL;
		} else
			out = 0;
	}

exit:
	cop_cxb_free(crb);

	return out;
}

int retire_qpool(int qpid)
{
	int out;
	struct xml_crb *crb;

	crb = cop_cxb_alloc(GFP_KERNEL);
	if (!crb) {
		pixad_err("cop_cxb_alloc failed\n");
		return -ENOMEM;
	}

	set_crb_qpool_remove(crb);
	set_retire_qpool_msg(&crb->msg_1.retire, qpid);

	out = xml_send(crb->ccw, crb);
	if (out != 0)
		pixad_err("send crb error: %d\n", out);

	cop_cxb_free(crb);

	return out;
}
