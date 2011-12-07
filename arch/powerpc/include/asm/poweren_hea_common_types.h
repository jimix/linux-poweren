#ifndef _ASM_POWEREN_HEA_COMMON_TYPES_H_
#define _ASM_POWEREN_HEA_COMMON_TYPES_H_

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

/* look for a better place */
enum hea_q_size {
	HEA_Q_SIZE_0 = 0x0,
	HEA_Q_SIZE_2 = 0x2,
	HEA_Q_SIZE_4 = 0x4,
	HEA_Q_SIZE_8 = 0x8,
	HEA_Q_SIZE_16 = 0x10,
	HEA_Q_SIZE_32 = 0x20,
	HEA_Q_SIZE_64 = 0x40,
	HEA_Q_SIZE_128 = 0x80,
	HEA_Q_SIZE_256 = 0x100,
	HEA_Q_SIZE_512 = 0x200,
	HEA_Q_SIZE_1K = 0x400,
	HEA_Q_SIZE_2K = 0x800,
	HEA_Q_SIZE_4K = 0x1000,
	HEA_Q_SIZE_8K = 0x2000,
	HEA_Q_SIZE_16K = 0x4000,
	HEA_Q_SIZE_32K = 0x8000,
	HEA_Q_SIZE_64K = 0x10000,
	HEA_Q_SIZE_128K = 0x20000,
	HEA_Q_SIZE_256K = 0x40000,
};


enum hea_cache_inject_type {
	HEA_NO_CACHE_INJECT,
	HEA_NORMAL_CACHE_INJECT,
	HEA_FCI_RR_TARGET_CACHE,
	HEA_FCI_SPECIFIED_TARGET_CACHE
};

enum hea_target_cache {
	HEA_CACHE_0,
	HEA_CACHE_1,
	HEA_CACHE_2,
	HEA_CACHE_3
};

enum hea_priv_mode {
	HEA_PRIV_NO,
	HEA_PRIV_SUPER,
	HEA_PRIV_PRIV,
	HEA_PRIV_USER,
};

#include "poweren_hea_eq.h"
#include "poweren_hea_cq.h"
#include "poweren_hea_qp.h"
#include "poweren_hea_channel.h"

/******* Prototypes *****/

/*
 * Struct which contains userspace information
 *
 * lpar				Logical partition number of process
 * pid				Process ID of userspace process
 * uid				User ID of userspace process
 * user_process		Pointer to OS user process information
 */
struct hea_process {
	__u32 lpar;
	__u32 pid;
	__u32 uid;
	void *user_process;
};

/*
 * Context for EQ
 *
 * process	contains user process information
 * cfg		EQ configuration data
 */
struct hea_eq_context {
	struct hea_process process;
	struct hea_eq_cfg cfg;
};

/*
 * Context for CQ
 *

 * ceq		Completion event queue number
 * aeq		Asynchronous event queue number
 * process	contains user process information
 * cfg		Configuration data for CQ
 */
struct hea_cq_context {
	__u32 ceq;
	__u32 aeq;
	struct hea_process process;
	struct hea_cq_cfg cfg;
};

/*
 * Context for channel
 *
 * cfg	Channel configuration
 */
struct hea_channel_context {
	struct hea_channel_cfg cfg;
	struct hea_eq0_pport_state_change pport_event;
};

/*
 * Context for QP
 *
 * eq		EQ for QP
 * r_cq		Receive Completion Queue
 * s_cq		Send Completion Queue
 * channel	Channel ID which identifies which channel this
 *		application should be running on
 * process	contains user process information
 * pd_cfg	Specifies parameters for protection domain
 * cfg		QP setup configuration
 */
struct hea_qp_context {
	__u32 eq;
	__u32 r_cq;
	__u32 s_cq;
	__u32 channel;
	struct hea_process process;
	struct hea_pd_cfg pd_cfg;
	struct hea_qp_cfg cfg;
};

struct hea_qpn_context {

	struct hea_qpn_cfg qpn_cfg;
};

struct hea_tcam_context {
	struct hea_tcam_cfg tcam_cfg;
};


 #endif /* _ASM_POWEREN_HEA_COMMON_TYPES_H_ */

