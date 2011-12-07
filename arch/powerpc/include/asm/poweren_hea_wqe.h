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

#ifndef _ASM_POWEREN_HEA_WQE_H_
#define _ASM_POWEREN_HEA_WQE_H_

#include "poweren_hea_cq.h"

/********************************************************/
/****************   SEND QUEUE WQE     ******************/
/********************************************************/

/* maximum number of data descriptor for each WQE size */
#define HEA_MAX_SND_WQE_1_DESC 254
#define HEA_MAX_SND_WQE_1_DESC_SZ0 6
#define HEA_MAX_SND_WQE_1_DESC_SZ1 14
#define HEA_MAX_SND_WQE_1_DESC_SZ2 30
#define HEA_MAX_SND_WQE_1_DESC_SZ3 62
#define HEA_MAX_SND_WQE_1_DESC_SZ4 126
#define HEA_MAX_SND_WQE_1_DESC_SZ5 254

#define HEA_MAX_SND_WQE_2_DESC 244
#define HEA_MAX_SND_WQE_2_DESC_SZ0 1
#define HEA_MAX_SND_WQE_2_DESC_SZ1 4
#define HEA_MAX_SND_WQE_2_DESC_SZ2 20
#define HEA_MAX_SND_WQE_2_DESC_SZ3 52
#define HEA_MAX_SND_WQE_2_DESC_SZ4 116
#define HEA_MAX_SND_WQE_2_DESC_SZ5 244

/* maximum size of immediate data for each WQE size (in 64 bit words) */
#define HEA_MAX_SND_WQE_2_IMMDATA 160
#define HEA_MAX_SND_WQE_2_IMMDATA_SZ0 80
#define HEA_MAX_SND_WQE_2_IMMDATA_SZ1 160
#define HEA_MAX_SND_WQE_2_IMMDATA_SZ2 160
#define HEA_MAX_SND_WQE_2_IMMDATA_SZ3 160
#define HEA_MAX_SND_WQE_2_IMMDATA_SZ4 160
#define HEA_MAX_SND_WQE_2_IMMDATA_SZ5 160

#define HEA_MAX_SND_WQE_3_IMMDATA 224
#define HEA_MAX_SND_WQE_3_IMMDATA_SZ0 96
#define HEA_MAX_SND_WQE_3_IMMDATA_SZ1 224
#define HEA_MAX_SND_WQE_3_IMMDATA_SZ2 224
#define HEA_MAX_SND_WQE_3_IMMDATA_SZ3 224
#define HEA_MAX_SND_WQE_3_IMMDATA_SZ4 224
#define HEA_MAX_SND_WQE_3_IMMDATA_SZ5 224

struct snd_wqe_common {
	__u64 wreq_id;
	__u16 tx_control;
	__u16 vlan_tag;
	__u8 pad0;
	__u8 ip_start;
	__u16 ip_end;
	__u16 cksum_offset;
	__u16 pad1;
	__u8 wrap_tag;
	__u8 num_descriptors;
	__u8 pad2;
	__u8 imm_data_len;
	__u16 pad3;
	__u16 mss;
	__u32 pad4;
};

#define HEA_WQE_LKEY_REAL 0xffffff00U
#define HEA_WQE_LKEY_XLATE 0x100U

struct wqe_desc {
	__u64 addr;
	__u32 key;
	__u32 len;
};

struct snd_wqe_1 {
	struct snd_wqe_common hdr;
	struct wqe_desc descriptors[HEA_MAX_SND_WQE_1_DESC];
};

struct snd_wqe_2 {
	struct snd_wqe_common hdr;
	struct wqe_desc descr0;
	__u8 immediate_data[HEA_MAX_SND_WQE_2_IMMDATA];
	struct wqe_desc descriptors[HEA_MAX_SND_WQE_2_DESC - 1];
};

struct snd_wqe_3 {
	struct snd_wqe_common hdr;
	__u8 immediate_data[HEA_MAX_SND_WQE_3_IMMDATA];
};

union snd_wqe {
	void *ptr;
	struct snd_wqe_1 type_1;
	struct snd_wqe_2 type_2;
	struct snd_wqe_3 type_3;
};

#define HEA_TC_CTRL_DO_ETH_CRC         0x8000
#define HEA_TC_CTRL_DO_IP_CKSUM        0x4000
#define HEA_TC_CTRL_DO_TCP_CKSUM       0x2000
#define HEA_TC_CTRL_TSO_ENABLE         0x1000
#define HEA_TC_CTRL_SIGNAL_COMPLETION  0x0800
#define HEA_TC_CTRL_INSERT_VLAN        0x0400
#define HEA_TC_CTRL_TYPE_1             0x0100
#define HEA_TC_CTRL_TYPE_2             0x0300
#define HEA_TC_CTRL_TYPE_3             0x0200
#define HEA_TC_CTRL_TYPE_4             0x0
#define HEA_TC_CTRL_WRAP_NORMAL        0x0
#define HEA_TC_CTRL_FORCE_OUT          0x0040
#define HEA_TC_CTRL_RECIRCULATE        0x0080
#define HEA_TC_CTRL_PURGE              0x0010
#define HEA_TC_CTRL_DO_UDP_CKSUM       0x0008

/********************************************************/
/*************   RECEIVE QUEUE WQE     ******************/
/********************************************************/

/* maximum number of data descriptor for each WQE size */
#define HEA_MAX_RCV_WQE_N_DESC 254
#define HEA_MAX_RCV_WQE_N_DESC_SZ0 6
#define HEA_MAX_RCV_WQE_N_DESC_SZ1 14
#define HEA_MAX_RCV_WQE_N_DESC_SZ2 30
#define HEA_MAX_RCV_WQE_N_DESC_SZ3 62
#define HEA_MAX_RCV_WQE_N_DESC_SZ4 126
#define HEA_MAX_RCV_WQE_N_DESC_SZ5 254

/* maximum size of immediate data for each WQE size */
#define HEA_MAX_RCV_WQE_LL_DATA     4096
#define HEA_MAX_RCV_WQE_LL_DATA_SZ0  128
#define HEA_MAX_RCV_WQE_LL_DATA_SZ1  256
#define HEA_MAX_RCV_WQE_LL_DATA_SZ2  512
#define HEA_MAX_RCV_WQE_LL_DATA_SZ3 1024
#define HEA_MAX_RCV_WQE_LL_DATA_SZ4 2048
#define HEA_MAX_RCV_WQE_LL_DATA_SZ5 4096

#define HEA_MAX_RCV_WQE_LLCQ_DATA     (HEA_MAX_RCV_WQE_LL_DATA - 32)
#define HEA_MAX_RCV_WQE_LLCQ_DATA_SZ0 (HEA_MAX_RCV_WQE_LL_DATA_SZ0 - 32)
#define HEA_MAX_RCV_WQE_LLCQ_DATA_SZ1 (HEA_MAX_RCV_WQE_LL_DATA_SZ1 - 32)
#define HEA_MAX_RCV_WQE_LLCQ_DATA_SZ2 (HEA_MAX_RCV_WQE_LL_DATA_SZ2 - 32)
#define HEA_MAX_RCV_WQE_LLCQ_DATA_SZ3 (HEA_MAX_RCV_WQE_LL_DATA_SZ3 - 32)
#define HEA_MAX_RCV_WQE_LLCQ_DATA_SZ4 (HEA_MAX_RCV_WQE_LL_DATA_SZ4 - 32)
#define HEA_MAX_RCV_WQE_LLCQ_DATA_SZ5 (HEA_MAX_RCV_WQE_LL_DATA_SZ5 - 32)

struct rcv_wqe_normal {
	__u64 wreq_id;
	__u32 pad0;
	__u8 pad1;
	__u8 num_data_segs;
	__u16 pad2;
	__u64 pad3;
	__u64 pad4;
	struct wqe_desc descriptors[HEA_MAX_RCV_WQE_N_DESC];
};

struct rcv_wqe_ll {
	__u8 data[HEA_MAX_RCV_WQE_LL_DATA];
};

struct rcv_wqe_ll_cqinrq {
	struct hea_ll_cqe cqe;
	__u8 data[HEA_MAX_RCV_WQE_LLCQ_DATA];
};

union rcv_wqe {
	void *ptr;
	struct wqe_desc desc;
	struct rcv_wqe_normal normal;
	struct rcv_wqe_ll ll;
	struct rcv_wqe_ll_cqinrq ll_cq_in_rq;
};

static inline void _hea_wqe_address_set(struct wqe_desc *wqe_desc,
					unsigned long addr, __u32 len)
{
#if defined(__KERNEL__) && defined(CONFIG_PPC_WSP_COPRO)
	wqe_desc->addr = addr;
	wqe_desc->key = HEA_WQE_LKEY_XLATE;
	wqe_desc->len = len;
#else
	wqe_desc->addr = virt_to_phys((void *) addr);
	wqe_desc->key = HEA_WQE_LKEY_REAL;
	wqe_desc->len = len;
#endif
}

#if defined(__KERNEL__) && defined(HEA_DEBUG_ADDRESSES)

#define HEA_USE_REAL_ADDRESS 0

#define hea_wqe_address_set(d, a, l) do {			\
	if (!virt_addr_valid(a)) {				\
		pr_err("hea_address error %lx: %s: %u\n",	\
		       (a), __func__, __LINE__);		\
			BUG_ON(1);				\
		}						\
		_hea_wqe_address_set(d, a, l);			\
	} while (0)
#else

#define HEA_USE_REAL_ADDRESS 1

#define hea_wqe_address_set(d, a, l) _hea_wqe_address_set(d, a, l)
#endif

#endif /* _ASM_POWEREN_HEA_WQE_H_ */
