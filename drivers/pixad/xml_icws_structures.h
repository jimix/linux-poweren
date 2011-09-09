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

#ifndef _DRIVERS_PIXAD_XML_ICWS_STRUCTURES_H_
#define _DRIVERS_PIXAD_XML_ICWS_STRUCTURES_H_

#define CT_XML 53


#define MAX_NUM_REPLENISH_CRB 8

#define ENGINE_QUEUE_BASE_ID 0x10
#define REPLENISH_MSG_TYPE_CODE 0x5
#define DIRECT_MESSAGE_1_FLAG 0x1
#define DIRECT_MESSAGE_2_FLAG 0x2
#define DIRECT_MESSAGE_3_FLAG 0x3
#define CRB_FLAG_DELETE_QPOOL 0x11

#define LE (1<<10)
#define IGNORE_PI (1<<9)
#define IGNORE_COMMENT (1<<8)
#define ERROR_IF_PI (1<<7)
#define DROP_WHITE_SPACES (1<<2)
#define BUILT_TREE (1<<1)
#define RUN_PPE (1)

/*****************************************************
 * CCW
 *****************************************************/
/* CD(Coprocessor Definition, [16:31]) in CCW */
	/* CL(Function Class [16:17]) */
#define CCW_CL_NORMAL_COP_REQ 0x0
#define CCW_CL_UTILITY_COP_REQ 0x1
	/* CI(Coprocessor Instance [18:26]) */
#define CCW_CI_MAX 0x4
#define CCW_CI_ALL_INSTANCE 0x0
#define CCW_CI_PRISM0 0x01
#define CCW_CI_PRISM1 0x02
#define CCW_CI_PRISM2 0x03
#define CCW_CI_PRISM3 0x04
#define CD_CI(ci) ((ci) << 5)
	/* FC(Function Code [27:31]) bytes encoding (workbook v0.91 p136) */
#define CCW_FC_REQ_QCODE_FROM_A2	0x00
#define CCW_FC_REQ_QCODE_FROM_XML 0x01 /* remote XML cop in other PRISM chip. */

/*
 * From XML message types
 */
#define MSG_XML_BUFFER             0x1
#define MSG_OPEN_SESSION           0x2
#define MSG_CLOSE_SESSION          0x3
#define MSG_REPLENISH_BUF          0x5 /* Used in Replenish_buf message array */
#define MSG_RETIRE_QPOOL           0x8
#define MSG_TLA_READY              0x10
#define MSG_TLA_READY_SESSION_FINISHED 0x50
#define MSG_TLA_CONT_READY             0x13
#define MSG_TAG_BUFFER_READY   0x12
#define MSG_QCODE_BUFFER_READY   0x14
#define MSG_TEXT_BUFFER_READY    0x18
#define MSG_TEXT_BUFFER_READY2   0x98
#define MSG_TEXT_CONT_BUFFER_READY 0x19
#define MSG_NAMESPACE_BUFFER_READY 0x1c
#define MSG_TREE_BUFFER_READY    0x30
#define MSG_TREE_BUFFER_READY2   0x31
#define MSG_NEED_REPLENISH_YELLOW  0x80
#define MSG_NEED_REPLENISH_RED   0xff
#define MSG_XML_BUFFER_DONE      0x20
#define MSG_INDIRECT_XML_BLOCK_DONE  0x21
#define MSG_INDIRECT_REPLENISH_BLOCK_DONE  0x22
#define MSG_XML_BUFFER_DONE_XLAT_ERROR 0x23
#define MSG_XML_BUFFER_DONE_PROTECTION_ERROR 0x24
#define MSG_NEED_QPOOL_YELLOW    0x2A
#define MSG_NEED_QPOOL_RED   0x2B
#define MSG_RETIRE_QPOOL_DONE    0x28
#define MSG_SESSION_FINISHED     0x40
#define MSG_OPEN_REJECT      0x41
#define MSG_XML_ERROR        0x7f

/* Session ID mask */
#define XMLSESSION_ID_MASK	0x003FFFFF

struct xml_close_msg {
	/* word 0 */
	u32 session_id;
	u8 msg_type;
	u8 reserved1;
	u16 reserved2;
	/* word 1 */
	u64 reserved3;
};

struct xml_open_msg {
	/* word 0 */
	u32 session_id;
	u8 msg_type;
	u8 reserved1;
	u16 ctrl;
	/* word 1 */
	u64 ppe;
};

struct xml_input_msg {
	u32 session_id;
	u8 msg_type;
	u8 reserved1;
	u16 length;
	u64 buffer;
};

struct xml_retire_qpool_msg {
	/* word 0 */
	u32 reserved1;
	u8  msg_type;
	/* separate one u16 to two u8 to avoid padding */
	u8 reserved2_1;
	u8 reserved2_2;
	u8 qpool_id;
	/* word 1 */
	u64 reserved3;
};

union xmlmsg {
	struct xml_open_msg open;
	struct xml_close_msg close;
	struct xml_input_msg xml_buffer;
	struct xml_retire_qpool_msg retire;
};

struct xml_qcode_request {
	u32 hash;
	u16 len;
	u8 type;
	u8 qpid;
	u8 qname[];
};

#define QCRES_RESULT_NO_SPACE	0x8000
#define QCRES_RESULT_INV_ARG	0x4000
#define QCRES_RESULT_ERR_MASK  (QCRES_RESULT_NO_SPACE | QCRES_RESULT_INV_ARG)
#define QCRES_DONE_MASK		0x00000001

struct xml_qcode_response {
	u16 qcode;
	u16 result;
	u32 done;
};

struct xml_crb_req_qcode {
	u32 ccw;
	u16 qos_and_other;
	u8 flags;
	u8 reserved1;

	u64 reserved2;

	u16 reserved3;
	u8 dde_count;   /* always zero for the XML */
	u8 pool_index;  /* ingored by xml */
	u32 byte_count;

	u64 in_dde_addr;
	u64 reserved4;
	u64 out_dde_addr;
	u64 reserved5;
	u64 reserved6;
} __aligned(128);

struct xml_crb {
	/* word 1 */
	u32 ccw;
	u16 qos;
	u8 flags;
	u8 reserved_2;

	/* word 2 */
	u64 reserved;

	union xmlmsg msg_1;
	union xmlmsg msg_2;
	union xmlmsg msg_3;
} __aligned(128);

/*
 * IMQ Messages
 */
struct xml_generic_imqe {
	u32 id;
	u8 type;
	u8 valid;
	u16 reserved1;
	u64 reserved2;
};

enum xml_qcode_type {
	prefix_qcode = 0,
	local_qcode = 1,
	uri_qcode = 2
};


extern int send_close_msg(unsigned int vf_id, unsigned int session_id);

extern int send_sync_crb(unsigned int vf_id, const char *flush_text,
					unsigned int text_len, int engine);
extern int wait_session_closed_imqe(struct pixad_copro *copro_info,
			 unsigned int unit_id, unsigned int vf_id, u32 sid);
extern int wait_imqe(struct pixad_copro *copro_info, unsigned int unit_id,
					unsigned int vf_id, int msgType);
extern int request_qcode(const char *qname, int qname_len,
		enum xml_qcode_type type, int qpid, int ci,
		struct pixad_user_proc *target);
extern int retire_qpool(int qpid);

#endif /* !_DRIVERS_PIXAD_XML_ICWS_STRUCTURES_H_ */
