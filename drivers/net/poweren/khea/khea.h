/*
 * Copyright (C) 2011, 2012 IBM Corporation.
 * Author:  Davide Pasetto <pasetto_davide@ie.ibm.com>
 *      Karol Lynch <karol_lynch@ie.ibm.com>
 *      Kay Muller <kay.muller@ie.ibm.com>
 *      John Sheehan <john.d.sheehan@ie.ibm.com>
 *      Jimi Xenidis <jimix@watson.ibm.com>
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


#ifndef _KHEA_H_
#define _KHEA_H_

#include <linux/kernel.h>
#include <linux/atomic.h>

#include <linux/if_vlan.h>
#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/inet_lro.h>

#include <rhea-interface.h>
#include <hea-queue.h>

#include <asm/poweren_hea_common_types.h>
#include <asm/poweren_hea_eq.h>
#include <asm/poweren_hea_cq.h>
#include <asm/poweren_hea_channel.h>
#include <asm/poweren_hea_wqe.h>

#include "khea-defaults.h"

#define KHEA_VERSION_STRING "v0.5"

/* 4 chips, 4 pport, 4 lports */
#define KHEA_MAX_DEVICES     (4*4*4)
#define KHEA_MAX_RECV_QP     2
#define KHEA_MAX_FRAGS       1
#define KHEA_MAX_FRAGS_ALLOC (KHEA_MAX_FRAGS + 1)
#define KHEA_MAX_QP          16

struct khea_kobj {
	struct kobject kobj;
	struct kobj_type ktype;
	struct khea_private *kp;
};

struct khea_page_allocator {
	struct page *page;
	unsigned offset;
	unsigned end;
	unsigned size;
};

struct khea_frags {
	struct skb_frag_struct frags[KHEA_MAX_FRAGS_ALLOC];
};

struct khea_qp_vec {
	struct khea_private *parent;

	/* HEA ringbuffer structures */
	struct hea_q eq ____cacheline_aligned;
	struct hea_q s_cq ____cacheline_aligned;
	struct hea_q r_cq ____cacheline_aligned;
	struct hea_q sq ____cacheline_aligned;
	struct hea_q rq[KHEA_MAX_RECV_QP] ____cacheline_aligned;
	struct hea_q rq1_track ____cacheline_aligned;

	/* HEA registers */
	struct rhea_cqte *s_cq_registers;
	struct rhea_cqte *r_cq_registers;
	struct rhea_qpte *qp_registers;

	/* send & receive queues */
	struct sk_buff **sendq;
	struct khea_page_allocator rq_allocator[KHEA_MAX_RECV_QP];
	struct khea_frags *recvq[KHEA_MAX_RECV_QP];
	struct sk_buff **recvq_skb[KHEA_MAX_RECV_QP];

	/* lro manager */
	struct net_lro_mgr lro_mgr;
	struct net_lro_desc *lro_arr;

	/* NAPI interface */
	struct napi_struct napi ____cacheline_aligned;

	unsigned eq_id;
	unsigned s_cq_id;
	unsigned r_cq_id;
	unsigned qp_id;

	int send_cq_len;
	int send_q_len;
	int send_q_reclaim;
	int recv_cq_len;

	int send_wqe_type;
	int recv_rq_len[KHEA_MAX_RECV_QP];
	int recv_rq_low[KHEA_MAX_RECV_QP];
	int recv_rq_size[KHEA_MAX_RECV_QP];

	unsigned send_q_inserted;
	unsigned send_q_offset;

	int use_ll_rq1;
	int max_num_lro;

	unsigned stats_sq_packets;
	unsigned stats_rqx_packets[KHEA_MAX_RECV_QP];
};

struct khea_private {

	unsigned num_qp;
	unsigned hea_qpn_shift;
	struct khea_qp_vec *qp_vec;


	/* operation support */
	struct net_device *ndev;
	unsigned i_am_up;
	unsigned mtu;
	unsigned interface_up;
	unsigned inited;

	u8 *mc_list;
	unsigned mc_list_len;
	u8 *uc_list;
	unsigned uc_list_len;

	spinlock_t vlanlock;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];

	int recv_qp_size[KHEA_MAX_RECV_QP];

	/* rhea interface */
	unsigned hea_id;

	unsigned lport_channel_id;
	unsigned bc_channel_id;
	unsigned mc_channel_id;
	unsigned uc_channel_id;

	unsigned slot_base;
	unsigned s_cq_token;
	unsigned r_cq_token;

	/* configuration parameters */
	int rhea_adapter;
	int rhea_pport;
	int rhea_lport;

	int recv_q_num;
	int frag_alloc_order;
	int do_gso;

	unsigned send_ll_limit;
	unsigned send_max_desc_1;
	unsigned send_max_desc_2;

	/* statistics */
	unsigned stats_rx_crc_errors;
	unsigned stats_rx_length_errors;
	unsigned stats_rx_frame_errors;
	unsigned stats_rx_fifo_errors;

	unsigned stats_rx_dropped;
	unsigned stats_tx_ifdown;
	unsigned stats_tx_busy;
	unsigned stats_tx_other;

	struct net_device_stats stats;

	/* sysfs interface */

	struct khea_kobj *params_obj;
	struct khea_kobj *stats_obj;
};

#define khea_info(fmt, args...) \
	pr_info("poweren_khea: " fmt "\n", ## args)

#define khea_warning(fmt, args...) \
	pr_warning("poweren_khea: " fmt "\n", ## args)

#define khea_error(fmt, args...) \
	pr_err("poweren_khea: Error in %s(): " fmt "\n", __func__, ## args)

#ifdef KHEA_DEBUG
#define khea_debug(fmt, ...)	\
	pr_info("poweren_khea: in %s(): " fmt "\n", __func__, ##__VA_ARGS__)
#else
	#define khea_debug(fmt, ...)   no_printk(fmt,  ##__VA_ARGS__)
#endif

#if defined(KHEA_DEBUG) && (KHEA_DEBUG > 1)
#define khea_debug2(fmt, ...)	\
	pr_info("poweren_khea: in %s(): " fmt "\n", __func__, ##__VA_ARGS__)
#else
	#define khea_debug2(fmt, ...)   no_printk(fmt,  ##__VA_ARGS__)
#endif


extern int khea_fill_recvq(struct khea_qp_vec *kp_qp, gfp_t gfp);
extern void khea_scan_event_queue(struct khea_qp_vec *kp_qp);
extern void khea_scan_send_cq(struct khea_qp_vec *kp_qp);
extern int khea_scan_recv_cq(struct khea_qp_vec *kp_qp, int budget);
extern int khea_init_interface(struct khea_private *kp);
extern void khea_fini_interface(struct khea_private *kp);
extern void khea_sysfs_init_params(struct kobj_type *ktype);
extern void khea_sysfs_init_stats(struct kobj_type *ktype);
extern int khea_init_allocator(struct khea_private *kp,
				struct khea_page_allocator *ka, int _frag_len);
extern void khea_free_allocator(struct khea_private *kp,
				struct khea_page_allocator *ka);
extern int khea_alloc_frag(struct khea_private *kp,
				struct khea_page_allocator *ka,
				struct skb_frag_struct *skb_frags);
extern int khea_alloc_frags(struct khea_private *kp,
				struct khea_page_allocator *ka,
				struct khea_frags *kfrags, int totlen);
extern struct net_device_stats *khea_get_stats(struct net_device *dev);
extern int khea_get_skb_header(struct sk_buff *skb, void **iphdr, void **tcph,
				u64 *hdr_flags, void *priv);
extern int khea_get_frag_header(struct skb_frag_struct *frag, void **mac_hdr,
				void **ip_hdr, void **tcpudp_hdr,
				u64 *hdr_flags, void *priv);

static inline bool khea_vlan_used(struct khea_private *kp)
{
	u16 vid;

	for_each_set_bit(vid, kp->active_vlans, VLAN_N_VID)
		return true;
	return false;
}

#endif /* _KHEA_H_ */
