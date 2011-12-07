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


/*
 * khea.c --  HEA kernel network interface
 *
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/string.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_net.h>
#include <linux/scatterlist.h>

#include <khea.h>
#include <khea-netdev.h>
#include <khea-ethtools.h>

#include <hea-qp-regs.h>
#include <hea-cq-regs.h>

irqreturn_t khea_irq_handler(int irq, void *eqp)
{
	struct khea_qp_vec *kp_qp = (struct khea_qp_vec *)eqp;

	khea_debug("handling irq for qpair %d", kp_qp->qp_id);

	khea_scan_event_queue(kp_qp);
	if (kp_qp->parent->i_am_up)
		napi_schedule(&(kp_qp->napi));
	return IRQ_HANDLED;
}

static int khea_pport_link_state_callback(__u32 port_nr, __u32 link_state,
					 void *args)
{
	struct khea_private *kp = (struct khea_private *) args;

	/* tell kernel whether link is up or down */
	if (link_state) {
		netif_carrier_on(kp->ndev);
		kp->interface_up = 1;

		khea_info("pport %u link is up", kp->rhea_pport + 1);
	} else {
		netif_carrier_off(kp->ndev);
		kp->interface_up = 0;

		khea_info("pport %u link is down", kp->rhea_pport + 1);
	}

	return 0;
}


int khea_init_interface(struct khea_private *kp)
{
	int err, i, k, size, q;
	unsigned hea_id;
	unsigned lport_channel_id = 0;
	unsigned bc_channel_id = 0;
	unsigned eq_id[KHEA_MAX_QP];
	unsigned s_cq_id[KHEA_MAX_QP], r_cq_id[KHEA_MAX_QP];
	unsigned qp_id[KHEA_MAX_QP];
	struct hea_channel_context context_channel;
	struct hea_eq_context context_eq;
	struct hea_cq_context context_cq;
	struct hea_qp_context context_qp;
	struct hea_qpn_context context_qpn;
	struct hea_hasher_setting hasher_setting;

	const struct khea_tresh {
		unsigned size;
		enum hea_threshold_vals thresh;
	} threshold_vals[] = {
		{
		128, HEA_THRESHOLD_VAL_128}, {
		256, HEA_THRESHOLD_VAL_256}, {
		512, HEA_THRESHOLD_VAL_512}, {
		1024, HEA_THRESHOLD_VAL_1024}, {
		1518, HEA_THRESHOLD_VAL_1518}, {
		1522, HEA_THRESHOLD_VAL_1522}, {
		2048, HEA_THRESHOLD_VAL_2048}, {
		4096, HEA_THRESHOLD_VAL_4096}, {
		9022, HEA_THRESHOLD_VAL_9022}, {
		0, HEA_THRESHOLD_VAL_9022}
	};

	memset(&context_channel, 0, sizeof(context_channel));

	khea_debug("opening adapter %d, pport %d, lport %d (mtu %d)",
		   kp->rhea_adapter, kp->rhea_pport, kp->rhea_lport, kp->mtu);

	err = rhea_session_init(&hea_id, kp->rhea_adapter);
	if (err != 0) {
		khea_error
			("cannot init adapter %d, port %d, lport %d - error %d",
			 kp->rhea_adapter, kp->rhea_pport, kp->rhea_lport,
			 err);
		return err;
	}

	context_channel.cfg.pport_nr = kp->rhea_pport;
	context_channel.cfg.type = kp->rhea_lport + HEA_LPORT_0;
	context_channel.cfg.max_frame_size = kp->mtu;
	context_channel.cfg.vlan.vlan_extract = 1;
	context_channel.cfg.vlan.discard_untagged = 0;
	context_channel.cfg.vlan.tag_filtering_mode = HEA_VLAN_DISCARD_ALL;

	/* specify callback for link state change */
	context_channel.pport_event.fkt_ptr = &khea_pport_link_state_callback;
	context_channel.pport_event.args = kp;

	khea_debug("trying adapter %d port %d lport %d", kp->rhea_adapter,
		   kp->rhea_pport, kp->rhea_lport);
	err = rhea_channel_alloc(hea_id, &lport_channel_id, &context_channel);
	if (err != 0) {
		rhea_session_fini(hea_id);
		khea_error
			("cannot init adapter %d, port %d, lport %d - error %d",
			 kp->rhea_adapter, kp->rhea_pport, kp->rhea_lport,
			 err);
		return err;
	}

	/* we always want to receive MAC broadcast packets here */
	context_channel.cfg.type = HEA_BC_PORT;
	context_channel.cfg.bc.lport_channel_id = lport_channel_id;
	context_channel.cfg.bc.channel_usuage = HEA_DEFAULT_CHANNEL_SHARE;
	khea_debug("trying adapter %d port %d BC", kp->rhea_adapter,
		   kp->rhea_pport);

	err = rhea_channel_alloc(hea_id, &bc_channel_id, &context_channel);
	if (err != 0) {
		rhea_session_fini(hea_id);
		khea_error("cannot init adapter %d, port %d, BC - error %d",
			   kp->rhea_adapter, kp->rhea_pport, err);
		return err;
	}

	/* -----------------------------------------------------
	 * if we arrive here we have: hea_id and channel_id
	 * -----------------------------------------------------*/

	for (q = 0; q < kp->num_qp; ++q) {
		/* alloc a EQ in SuperPrivilegied mode */
		/* at least 2 x #QP + 3 x #CQ +1   = 1 QP, 1 CP + slack */
		memset(&context_eq, 0, sizeof(context_eq));
		context_eq.cfg.eqe_count = 16;
		context_eq.cfg.irq_type = HEA_IRQ_COALESING_2;
		context_eq.cfg.coalesing2_delay = HEA_EQ_COALESING_DELAY_3;
		context_eq.cfg.generate_completion_events =
			HEA_EQ_GEN_COM_EVENT_DISABLE;

		err = rhea_eq_alloc(hea_id, &eq_id[q], &context_eq);
		if (err) {
			khea_error(" %d allocating EventQueue", err);
			goto rheadealloc;
		}
	}

	/* alloc a send CQ */
	for (q = 0; q < kp->num_qp; ++q) {
		memset(&context_cq, 0, sizeof(context_cq));
		context_cq.ceq = eq_id[q];
		context_cq.aeq = eq_id[q];
		context_cq.cfg.cqe_count = kp->qp_vec[q].send_q_len;

		context_cq.cfg.cqe_auto_toggle = 1;
		context_cq.cfg.irq_type = HEA_IRQ_COALESING_2;

		err = rhea_cq_alloc(hea_id, &s_cq_id[q], &context_cq);
		if (err) {
			khea_error(" %d allocating send CompletitionQueue",
					err);
			goto rheadealloc_eq;
		}
	}

	/* alloc a receive CQ */
	for (q = 0; q < kp->num_qp; ++q) {
		context_cq.ceq = eq_id[q];
		context_cq.aeq = eq_id[q];
		context_cq.cfg.cqe_count = kp->qp_vec[q].recv_cq_len;

		context_cq.cfg.cqe_auto_toggle = 1;
		context_cq.cfg.irq_type = HEA_IRQ_COALESING_2;

		err = rhea_cq_alloc(hea_id, &r_cq_id[q], &context_cq);
		if (err) {
			khea_error(" %d allocating receive CompletitionQueue",
					err);
			goto rheadealloc_s_cq;
		}
	}

	/* alloc and assign the QP */
	memset(&context_qpn, 0, sizeof(context_qpn));

	/* alloc QPn slots */
	context_qpn.qpn_cfg.slot_count = kp->hea_qpn_shift;
	err = rhea_channel_qpn_alloc(hea_id, lport_channel_id, &context_qpn);
	if (err) {
		khea_error(" %d allocating QPN entry", err);
		goto rheadealloc_qp;
	}

	for (q = 0; q < kp->num_qp; ++q) {
		memset(&context_qp, 0, sizeof(context_qp));
		context_qp.cfg.sq.wqe_size = kp->qp_vec[q].send_wqe_type;
		context_qp.cfg.sq.wqe_count = kp->qp_vec[q].send_cq_len;
		context_qp.cfg.rq1.wqe_size = HEA_WQE_SIZE_128;

		context_qp.cfg.rq1.wqe_count = kp->qp_vec[q].recv_rq_len[0];
		context_qp.cfg.rq1.low_latency =
				(kp->qp_vec[q].use_ll_rq1 != 0) ? 1 : 0;

		if (kp->recv_q_num > 1) {
			for (k = 0;
				threshold_vals[k].size > 0 &&
				threshold_vals[k].size <=
					kp->qp_vec[q].recv_rq_size[0];
				k++)
				continue;

			if (k > 0)
				k--;
			khea_debug("RQ1-2 threshold %d=%d (size %d)", k,
					threshold_vals[k].thresh,
					kp->qp_vec[q].recv_rq_size[0]);

			context_qp.cfg.rq2.wqe_size = HEA_WQE_SIZE_128;
			context_qp.cfg.rq2.wqe_count =
						kp->qp_vec[q].recv_rq_len[1];
			context_qp.cfg.rq2.data_threshold =
						threshold_vals[k].thresh;
		}

		context_qp.eq = eq_id[q];
		context_qp.r_cq = r_cq_id[q];
		context_qp.s_cq = s_cq_id[q];

		context_qp.cfg.r_cq_use = HEA_RQ_CQE_DISABLE;
		context_qp.cfg.s_cq_use = HEA_SQ_CQE_WQE_SPECIFIED;

		context_qp.cfg.sq.tenure = 25;
		context_qp.cfg.dma_64_bit_aligned = 1;
		context_qp.channel = lport_channel_id;

		if (HEA_USE_REAL_ADDRESS)
			context_qp.cfg.real_mode = 1;

		err = rhea_qp_alloc(hea_id, &qp_id[q], &context_qp);
		if (err) {
			khea_error(" %d allocating QueuePair", err);
			goto rheadealloc_r_cq;
		}

		khea_debug("allocated eq=%d, s_cq=%d, r_cq=%d, qp=%d",
				eq_id[q], s_cq_id[q], r_cq_id[q], qp_id[q]);

		/* wire channel to QPn to QP */
		err = rhea_channel_wire_qpn_to_qp(hea_id,
						lport_channel_id, qp_id[q], q);
		if (err) {
			khea_error(" %d wiring QP to QPN entry", err);
			goto rheadealloc_qpn;
		}

		err = rhea_channel_feature_set(hea_id, lport_channel_id,
				HEA_CHANNEL_SET_MAX_FRAME_SIZE,
				kp->mtu);
		if (err) {
			khea_error(" was not able to set maximum "
					"frame size to :%u\n",
					kp->mtu);
			goto rheadealloc_qpn;
		}
		khea_debug("allocated and wired qp[%d]=%d", q, qp_id[q]);
	}

	/* -----------------------------------------------------
	 * we allocated all required rHEA resources
	 * now initialize our private data structures
	 * ----------------------------------------------------- */


	if (kp->num_qp > 1) {

		err = rhea_channel_hasher_alloc(hea_id, lport_channel_id);
		if (err) {
			khea_error("unable to allocate hasher");
			goto free;
		}

		memset(&hasher_setting, 0, sizeof(hasher_setting));
		hasher_setting.mask0 = ~0ULL;
		hasher_setting.mask1 = ~0ULL;

		hasher_setting.sc_0_8 = 1;
		hasher_setting.sc_1_9 = 1;
		hasher_setting.sc_2_10 = 1;
		hasher_setting.sc_3_11 = 1;

		hasher_setting.sc_4_12 = 1;
		hasher_setting.sc_5_13 = 1;
		hasher_setting.sc_6_14 = 1;
		hasher_setting.sc_7_15 = 1;

		err = rhea_channel_hasher_set(hea_id, lport_channel_id,
					&hasher_setting);
		if (err) {
			khea_error("unable to configure hasher");
			goto free;
		}
	}

	/* save all our resource ids */
	kp->hea_id = hea_id;
	kp->lport_channel_id = lport_channel_id;
	kp->bc_channel_id = bc_channel_id;

	kp->mc_channel_id = 0;
	kp->uc_channel_id = 0;

	for (q = 0; q < kp->num_qp; ++q) {
		kp->qp_vec[q].eq_id = eq_id[q];
		kp->qp_vec[q].s_cq_id = s_cq_id[q];
		kp->qp_vec[q].r_cq_id = r_cq_id[q];
		kp->qp_vec[q].qp_id = qp_id[q];
	}

	/* initialize other operating variables */
	kp->i_am_up = 0;
	kp->interface_up = 0;

	/* now allocate everything we need in kernel memory */
	/* NOTE: we do not use allocator number 0 */
	for (q = 0; q < kp->num_qp; ++q) {
		for (i = 1; i < kp->recv_q_num; i++) {
			err = khea_init_allocator(kp,
					&kp->qp_vec[q].rq_allocator[i],
					kp->qp_vec[q].recv_rq_size[i]);
			if (err) {
				khea_error("Could not init rq%d allocator",
						i + 1);
				goto free;
			}
			kp->qp_vec[q].recvq[i] = kzalloc(
					sizeof(struct khea_frags) *
					kp->qp_vec[q].recv_rq_len[i],
					GFP_KERNEL);

			if (!kp->qp_vec[q].recvq[i]) {
				khea_error("Could not allocate rq %d "
						"support vector",
						i + 1);
				goto free;
			}
		}

		for (i = 0; i < kp->recv_q_num; i++) {
			kp->qp_vec[q].recvq_skb[i] = kzalloc(
					sizeof(struct sk_buff *) *
					kp->qp_vec[q].recv_rq_len[i],
					GFP_KERNEL);

			if (!kp->qp_vec[q].recvq_skb[i]) {
				khea_error("out of memory allocating "
						"kp->recvq_skb[%d]", i);
				goto free;
			}
		}

		kp->qp_vec[q].sendq = kzalloc(sizeof(struct sk_buff *) *
					kp->qp_vec[q].send_q_len, GFP_KERNEL);

		if (!kp->qp_vec[q].sendq) {
			khea_error("out of memory allocating kp->sendq");
			goto free;
		}

		if (kp->qp_vec[q].max_num_lro > 0) {
			kp->qp_vec[q].lro_arr = kzalloc(
					sizeof(struct net_lro_desc) *
					kp->qp_vec[q].max_num_lro,
					GFP_KERNEL);

			if (!kp->qp_vec[q].lro_arr) {
				khea_error("out of memory allocating "
						"kp->lro_arr");
				goto free;
			}

			kp->qp_vec[q].lro_mgr.dev = kp->ndev;
			memset(&kp->qp_vec[q].lro_mgr.stats, 0,
				sizeof(kp->qp_vec[q].lro_mgr.stats));

			kp->qp_vec[q].lro_mgr.features = LRO_F_NAPI;
			kp->qp_vec[q].lro_mgr.ip_summed =
						CHECKSUM_UNNECESSARY;
			kp->qp_vec[q].lro_mgr.ip_summed_aggr =
						CHECKSUM_UNNECESSARY;
			kp->qp_vec[q].lro_mgr.max_desc =
						kp->qp_vec[q].max_num_lro;

			kp->qp_vec[q].lro_mgr.max_aggr = 32;
			kp->qp_vec[q].lro_mgr.frag_align_pad = 0;
			kp->qp_vec[q].lro_mgr.lro_arr = kp->qp_vec[q].lro_arr;
			kp->qp_vec[q].lro_mgr.get_skb_header =
						khea_get_skb_header;
			kp->qp_vec[q].lro_mgr.get_frag_header =
						khea_get_frag_header;
		}
	}

	/* setup access to EQ */
	for (q = 0; q < kp->num_qp; ++q) {
		err = rhea_eq_table(hea_id, eq_id[q],
				(struct hea_eqe **)(&kp->qp_vec[q].eq.q_begin),
				&kp->qp_vec[q].eq.qe_size,
				&kp->qp_vec[q].eq.qe_count);
		if (err) {
			khea_error("rhea_eq_table for eq failed\n");
			goto free;
		}
		heaq_init(&kp->qp_vec[q].eq);
	}

	/* setup access to CQ */
	for (q = 0; q < kp->num_qp; ++q) {
		err = rhea_cq_table(hea_id, s_cq_id[q],
				(struct hea_cqe **)
					(&kp->qp_vec[q].s_cq.q_begin),
				&kp->qp_vec[q].s_cq.qe_size,
				&kp->qp_vec[q].s_cq.qe_count);
		if (err) {
			khea_error("rhea_cq_table for send cq failed\n");
			goto free;
		}
		heaq_init(&kp->qp_vec[q].s_cq);

		err = rhea_cq_mapinfo(hea_id, s_cq_id[q], HEA_PRIV_PRIV,
				(void **)&(kp->qp_vec[q].s_cq_registers),
				&size, 1);
		if (err) {
			khea_error("rhea_sp_mapinfo failed for send "
					"completion queue\n");
			goto free;
		}

		err = rhea_cq_table(hea_id, r_cq_id[q],
				(struct hea_cqe **)
					(&kp->qp_vec[q].r_cq.q_begin),
				&kp->qp_vec[q].r_cq.qe_size,
				&kp->qp_vec[q].r_cq.qe_count);
		if (err) {
			khea_error("rhea_cq_table for receive cq failed\n");
			goto free;
		}
		heaq_init(&kp->qp_vec[q].r_cq);

		err = rhea_cq_mapinfo(hea_id, r_cq_id[q], HEA_PRIV_PRIV,
				(void **)&(kp->qp_vec[q].r_cq_registers),
				&size, 1);
		if (err) {
			khea_error("rhea_sp_mapinfo failed for receive "
					"completion queue\n");
			goto free;
		}
	}

	/* setup access to QP */
	for (q = 0; q < kp->num_qp; ++q) {
		err = rhea_qp_mapinfo(hea_id, qp_id[q], HEA_PRIV_PRIV,
				(void **)&(kp->qp_vec[q].qp_registers),
				&size, 1);
		if (err) {
			khea_error("rhea_qp_mapinfo failed\n");
			goto free;
		}
	}

	/* setup WQE pointers */
	for (q = 0; q < kp->num_qp; ++q) {
		err = rhea_sq_table(hea_id, qp_id[q],
					(union snd_wqe **)
						(&kp->qp_vec[q].sq.q_begin),
					&kp->qp_vec[q].sq.qe_size,
					&kp->qp_vec[q].sq.qe_count);
		if (err) {
			khea_error("rhea_sq_table failed\n");
			goto free;
		}
		heaq_init(&kp->qp_vec[q].sq);
	}

	/* setup access to all QP receive queues */
	for (q = 0; q < kp->num_qp; ++q) {
		for (i = 0; i < kp->recv_q_num; i++) {
			err = rhea_rq_table(hea_id, qp_id[q], i + 1,
					(union rcv_wqe **)
						(&kp->qp_vec[q].rq[i].q_begin),
					&kp->qp_vec[q].rq[i].qe_size,
					&kp->qp_vec[q].rq[i].qe_count);
			if (err) {
				khea_error("rhea_rq_table for RQ %d failed\n",
						i + 1);
				goto free;
			}

			heaq_init(&kp->qp_vec[q].rq[i]);
		}

		kp->qp_vec[q].rq1_track = kp->qp_vec[q].rq[0];
	}

	/* setup interrupts */
	for (q = 0; q < kp->num_qp; ++q) {
		khea_debug("setup interrupt for qpair (%d)", q);
		err = rhea_interrupt_setup(hea_id, eq_id[q], khea_irq_handler,
					   (void *)&(kp->qp_vec[q]));
		if (err) {
			khea_error("Could not allocate new IRQ for qpair %d",
					q);
			goto free;
		}
	}

	kp->inited = 1;
	kp->interface_up = 0;
	khea_debug("initialised device %s", kp->ndev->name);

	return 0;

free:
	for (q = 0; q < kp->num_qp; ++q) {
		for (i = 1; i < kp->recv_q_num; i++) {
			khea_free_allocator(kp,
				&kp->qp_vec[q].rq_allocator[i]);

			if (kp->qp_vec[q].recvq[i])
				kzfree(kp->qp_vec[q].recvq[i]);
			kp->qp_vec[q].recvq[i] = NULL;
		}
	}

	for (q = 0; q < kp->num_qp; ++q) {
		for (i = 0; i < kp->recv_q_num; i++) {
			if (kp->qp_vec[q].recvq_skb[i]) {
				kzfree(kp->qp_vec[q].recvq_skb[i]);
				kp->qp_vec[q].recvq_skb[i] = NULL;
			}
		}
	}

	for (q = 0; q < kp->num_qp; ++q) {
		if (kp->qp_vec[q].sendq) {
			kzfree(kp->qp_vec[q].sendq);
			kp->qp_vec[q].sendq = NULL;
		}
	}

	for (q = 0; q < kp->num_qp; ++q) {
		if (kp->qp_vec[q].lro_arr) {
			kzfree(kp->qp_vec[q].lro_arr);
			kp->qp_vec[q].lro_arr = NULL;
		}
	}

rheadealloc_qpn:
	rhea_channel_qpn_free(hea_id, lport_channel_id);
rheadealloc_qp:
	for (q = 0; q < kp->num_qp; ++q)
		rhea_qp_free(hea_id, qp_id[q]);
rheadealloc_r_cq:
	for (q = 0; q < kp->num_qp; ++q)
		rhea_cq_free(hea_id, r_cq_id[q]);
rheadealloc_s_cq:
	for (q = 0; q < kp->num_qp; ++q)
		rhea_cq_free(hea_id, s_cq_id[q]);
rheadealloc_eq:
	for (q = 0; q < kp->num_qp; ++q)
		rhea_eq_free(hea_id, eq_id[q]);
rheadealloc:
	kfree(kp->qp_vec);

	rhea_channel_free(hea_id, lport_channel_id);
	rhea_channel_free(hea_id, bc_channel_id);
	rhea_session_fini(hea_id);

	kp = NULL;

	return err;
}

void khea_fini_interface(struct khea_private *kp)
{
	int q = 0;
	int ni, rq_nr;

	if (!kp || !kp->inited)
		return;

	kp->inited = 0;

	if (kp->i_am_up) {
		kp->i_am_up = 0;
		rhea_channel_disable(kp->hea_id, kp->bc_channel_id);
		if (kp->mc_channel_id != 0)
			rhea_channel_disable(kp->hea_id, kp->mc_channel_id);
		if (kp->uc_channel_id != 0)
			rhea_channel_disable(kp->hea_id, kp->uc_channel_id);
		rhea_channel_disable(kp->hea_id, kp->lport_channel_id);
		for (q = 0; q < kp->num_qp; ++q)
			rhea_qp_down(kp->hea_id, kp->qp_vec[q].qp_id);
	}

	rhea_channel_qpn_free(kp->hea_id, kp->lport_channel_id);

	for (q = 0; q < kp->num_qp; ++q) {
		rhea_qp_free(kp->hea_id, kp->qp_vec[q].qp_id);
		rhea_cq_free(kp->hea_id, kp->qp_vec[q].r_cq_id);
		rhea_cq_free(kp->hea_id, kp->qp_vec[q].s_cq_id);
		rhea_eq_free(kp->hea_id, kp->qp_vec[q].eq_id);
	}

	rhea_channel_free(kp->hea_id, kp->lport_channel_id);
	rhea_channel_free(kp->hea_id, kp->bc_channel_id);

	if (kp->mc_channel_id != 0)
		rhea_channel_free(kp->hea_id, kp->mc_channel_id);
	if (kp->uc_channel_id != 0)
		rhea_channel_free(kp->hea_id, kp->uc_channel_id);
	rhea_session_fini(kp->hea_id);

	for (q = 0; q < kp->num_qp; ++q) {
		for (ni = 1; ni < kp->recv_q_num; ni++) {
			khea_free_allocator(kp,
					&kp->qp_vec[q].rq_allocator[ni]);

			if (kp->qp_vec[q].recvq[ni]) {
				kzfree(kp->qp_vec[q].recvq[ni]);
				kp->qp_vec[q].recvq[ni] = NULL;
			}
		}
	}

	/* Free our skbs in send and recv queues, if any. */
	for (q = 0; q < kp->num_qp; ++q) {
		for (rq_nr = 0; rq_nr < kp->recv_q_num; rq_nr++) {
			if (kp->qp_vec[q].recvq_skb[rq_nr]) {
				for (ni = 0;
					ni < kp->qp_vec[q].recv_rq_len[rq_nr];
					ni++) {

					if (kp->qp_vec[q].
						recvq_skb[rq_nr][ni]) {

						dev_kfree_skb_any(
							kp->qp_vec[q].
							recvq_skb[rq_nr][ni]);

						kp->qp_vec[q].
						    recvq_skb[rq_nr][ni] =
							NULL;
					}
				}
				kzfree(kp->qp_vec[q].recvq_skb[rq_nr]);
				kp->qp_vec[q].recvq_skb[rq_nr] = NULL;
			}
		}
	}

	for (q = 0; q < kp->num_qp; ++q) {
		if (kp->qp_vec[q].sendq) {
			for (ni = 0; ni < kp->qp_vec[q].sq.qe_count; ni++) {
				if (kp->qp_vec[q].sendq[ni]) {
					dev_kfree_skb_any(kp->qp_vec[q].
								sendq[ni]);
					kp->qp_vec[q].sendq[ni] = NULL;
				}
			}
			kzfree(kp->qp_vec[q].sendq);
			kp->qp_vec[q].sendq = NULL;
		}
	}

	for (q = 0; q < kp->num_qp; ++q) {
		if (kp->qp_vec[q].lro_arr) {
			kzfree(kp->qp_vec[q].lro_arr);
			kp->qp_vec[q].lro_arr = NULL;
		}
	}
}

/* Returns 0 if we couldn't fill entirely (OOM). */
int khea_fill_recvq(struct khea_qp_vec *kp_qp, gfp_t gfp)
{
	/*
	 * we fill RQ1 with standard skbuf we fill RQ2 with
	 * fragmented skbuf we prealloc all required skbuf
	 * structures to reduce receive latency
	 */
	struct sk_buff *skb;
	struct skb_frag_struct *skbf;
	int size, size2, added, count, ret, i;
	unsigned temp;
	struct khea_private *kp = NULL;
	struct rcv_wqe_normal *rwqe = NULL;
	khea_debug("processing qp %d", kp_qp->qp_id);

	ret = 0;
	kp = kp_qp->parent;

	/* start with RQ1 */
	count = heaq_get_count(&(kp_qp->rq[0]));
	size = kp_qp->recv_rq_size[0];
	added = 0;

	khea_debug2("%d-%d", count, size);

	while (count > 0) {
		/* alloc input data buffer */
		skb = netdev_alloc_skb(kp->ndev, size + NET_IP_ALIGN);
		if (unlikely(!skb)) {
			khea_error("out of memory");
			ret = 1;
			break;
		}

		skb_reserve(skb, NET_IP_ALIGN);
		temp = heaq_get_offset(&(kp_qp->rq[0]));
		skb->dev = kp->ndev;
		kp_qp->recvq_skb[0][temp] = skb;
		if (!(kp_qp->use_ll_rq1)) {
			/* enqueue receive WQE */
			rwqe = (struct rcv_wqe_normal *)
					(kp_qp->rq[0].qe_current);
			rwqe->wreq_id = (ulong)temp;
			rwqe->num_data_segs = 1;

			hea_wqe_address_set(
					&rwqe->descriptors[0],
					(ulong) skb_tail_pointer(skb),
					size);
		}
		heaq_set_next_qe(&(kp_qp->rq[0]));
		heaq_dec_count(&(kp_qp->rq[0]));

		added++;
		count--;
	}

	if (added > 0) {
		iosync();
		/* push the ADD register */
		out_be64(&(kp_qp->qp_registers->qp_rq1a), added);
		khea_debug("added %d buffers to rq %d in qp %d",
					added, 0, kp_qp->qp_id);
	}


	if (kp->recv_q_num == 1)
		return ret;

	/* now do RQ2 */
	count = heaq_get_count(&(kp_qp->rq[1]));
	size = kp_qp->recv_rq_size[1];
	size2 = kp_qp->rq_allocator[1].size;
	added = 0;
	while (count > 0) {
		temp = heaq_get_offset(&(kp_qp->rq[1]));
		if (unlikely(khea_alloc_frags(kp, &(kp_qp->rq_allocator[1]),
					&(kp_qp->recvq[1][temp]), size))) {
			khea_error("out of memory");
			ret = 1;
			break;
		}

		if (kp_qp->max_num_lro == 0) {
			skb = kp_qp->recvq_skb[1][temp];
			if (likely(!skb)) {
				skb = netdev_alloc_skb(kp->ndev,
							64 + NET_IP_ALIGN);
				if (unlikely(!skb)) {
					khea_error("out of memory");
					ret = 1;
					break;
				}
				skb_reserve(skb, NET_IP_ALIGN);
			}

			kp_qp->recvq_skb[1][temp] = skb;
		}

		/* enqueue receive WQE */
		skbf = kp_qp->recvq[1][temp].frags;
		rwqe = (struct rcv_wqe_normal *) (kp_qp->rq[1].qe_current);
		rwqe->wreq_id = (ulong) temp;

		for (i = 0; skbf[i].page; i++) {

			hea_wqe_address_set(
					&rwqe->descriptors[i],
					(ulong) (page_address(skbf[i].page) +
						skbf[i].page_offset),
					size2);
		}

		rwqe->num_data_segs = i;
		heaq_set_next_qe(&(kp_qp->rq[1]));
		heaq_dec_count(&(kp_qp->rq[1]));

		count--;
		added++;
	}

	if (added > 0) {
		/* push the ADD register */
		iosync();
		out_be64(&(kp_qp->qp_registers->qp_rq2a), added);
		khea_debug2("khea_fill_recvq() - added %d to rq%d, sz "
				"%d fragsz %d len %d",
				added, 1, size, size2, rwqe->num_data_segs);
	}

	return ret;
}

int khea_scan_recv_cq(struct khea_qp_vec *kp_qp, int budget)
{
	int rq, discard, len, num, i, ip_summed, hlen, lroed;
	struct sk_buff *skb;
	struct hea_cqe *cqe_current;
	unsigned temp;
	struct khea_frags *frags = NULL;
	struct ethhdr *mac_hdr;
	void *rq1_va = NULL;
	__wsum csum = 0;
	struct khea_private *kp = NULL;
	struct skb_frag_struct *skb_frags;
	void *va;

	khea_debug("processing recv for qpair %d", kp_qp->qp_id);

	num = 0;
	lroed = 0;
	kp = kp_qp->parent;


	cqe_current = kp_qp->r_cq.qe_current;
	while (budget > 0 &&
			hea_cqe_is_valid(cqe_current,
			kp_qp->r_cq.q_toggle_bit)) {
		BUG_ON(hea_cqe_is_transmit(cqe_current));

		budget--;
		num++;
		heaq_set_next_qe(&(kp_qp->r_cq));

		/* check which rq was used */
		rq = hea_cqe_rq_used(cqe_current) - 1;
		discard = 0;
		len = cqe_current->n_bytes_xfered - 4;
		heaq_inc_count(&(kp_qp->rq[rq]));
		kp_qp->stats_rqx_packets[rq]++;

		khea_debug("qp_id(%d), got one CQE for RQ %d "
				"- %d bytes (%d)", kp_qp->qp_id, rq + 1,
				cqe_current->n_bytes_xfered,
				heaq_get_count(&(kp_qp->rq[rq])));
		khea_debug("qp_id(%d), hash is %u", kp_qp->qp_id,
				 hea_cqe_hash_get(cqe_current));
		khea_debug("qp_id(%d), hash is %s", kp_qp->qp_id,
				(hea_cqe_status_hash_valid(cqe_current)
				? "valid" : "invalid"));

		if (len < ETH_HLEN) {
			khea_error("packet too short??? len=%d\n", len);
			discard = 1;
		}

		if (rq == 0) {
			temp = heaq_get_offset(&(kp_qp->rq1_track));
			rq1_va = (void *)kp_qp->rq1_track.qe_current;
			heaq_set_next_qe(&(kp_qp->rq1_track));
		} else {
			temp = cqe_current->wr_id;
			frags = &(kp_qp->recvq[rq][temp]);
		}

		/* check if this is an input error */
		if (unlikely(cqe_current->status &
				(HEA_CQE_STATUS_BAD_CRC_BIT |
				HEA_CQE_STATUS_LENGTH_ERROR_BIT |
				HEA_CQE_STATUS_BAD_FRAME_BIT |
				HEA_CQE_STATUS_ERRORS_BIT))) {

			khea_debug("input error %x", cqe_current->status &
				HEA_CQE_ANY_ERROR_BIT);

			if (cqe_current->status & HEA_CQE_STATUS_BAD_CRC_BIT)
				kp->stats_rx_crc_errors += 1;

			if (cqe_current->
				status & HEA_CQE_STATUS_LENGTH_ERROR_BIT)
				kp->stats_rx_length_errors += 1;

			if (cqe_current->
				status & HEA_CQE_STATUS_BAD_FRAME_BIT)
				kp->stats_rx_frame_errors += 1;

			if (cqe_current->
				status & HEA_CQE_STATUS_ERRORS_BIT)
				kp->stats_rx_fifo_errors += 1;

			discard = 1;
		} else if (unlikely(hea_cqe_is_wrapped(cqe_current) &&
					!(kp->ndev->flags & IFF_PROMISC))) {

			if (rq == 0) {
				if (kp_qp->use_ll_rq1)
					mac_hdr = (struct ethhdr *)rq1_va;
				else
					mac_hdr =
						(struct ethhdr *)
						skb_tail_pointer(kp_qp->
							recvq_skb[0][temp]);
			} else {
				mac_hdr =
					(struct ethhdr
					 *)(page_address(frags->
							frags[0].page) +
						 frags->frags[0].page_offset);
			}

			if (likely(hea_cqe_is_bc(cqe_current)))
				khea_debug("wrapped broadcast packet");
			else if (unlikely(hea_cqe_is_mc(cqe_current))) {
				if (compare_ether_addr_64bits
						(mac_hdr->h_dest,
						kp->ndev->broadcast)) {

					/* filter address */
					khea_debug("wrapped multicast packet");
					discard = 1;

					for (i = 0; i < kp->mc_list_len; i++) {
						if (compare_ether_addr(
							mac_hdr->h_dest,
							kp->mc_list +
							(i * ETH_ALEN))) {

							discard = 0;
							break;
						}
					}

					if (discard == 1)
						khea_debug("discarding "
							"multicast packet "
							"not for me");
				}
			} else if (unlikely(compare_ether_addr_64bits(mac_hdr->
						h_dest, kp->ndev->dev_addr))) {
				/* filter address */
				khea_debug("wrapped unicast packet");
				discard = 1;

				for (i = 0; i < kp->uc_list_len; i++) {
					if (compare_ether_addr(mac_hdr->h_dest,
							kp->uc_list +
							(i * ETH_ALEN))) {

						discard = 0;
						break;
					}
				}

				if (discard == 1)
					khea_debug("discarding unicast packet "
							"not for me");
			}
		}

		if (unlikely(discard == 1)) {
			/* discard this packet */
			khea_debug2("discarding packet");
			if (rq == 0) {
				skb = kp_qp->recvq_skb[0][temp];
				kp_qp->recvq_skb[0][temp] = NULL;
				dev_kfree_skb_any(skb);
			} else {
				for (i = 0; frags->frags[i].page; i++) {
					khea_debug2("discarding frag %d is %p",
						i, frags->frags[i].page);
					put_page(frags->frags[i].page);
					frags->frags[i].page = NULL;
					frags->frags[i].page_offset = 0;
				}
			}

			/* move to next entry */
			cqe_current = kp_qp->r_cq.qe_current;
			continue;
		}

		if (rq == 0) {
			/* this is RQ1 (can be low latency or normal) */
			skb = kp_qp->recvq_skb[0][temp];
			kp_qp->recvq_skb[0][temp] = NULL;

			if (likely(kp_qp->use_ll_rq1))
				memcpy(skb_put(skb, len), rq1_va, len);
			else
				skb_put(skb, len);

			skb->protocol = eth_type_trans(skb, kp->ndev);
			if (likely(hea_cqe_marker_ipv4(cqe_current))) {
				/* the device verified the checksum if
				 * it is an IPv4 packet */
				if (unlikely(cqe_current->status &
					(HEA_CQE_STATUS_TCP_CKSUM_ERR_BIT |
					HEA_CQE_STATUS_IP_CKSUM_ERR_BIT))) {

					skb->ip_summed = CHECKSUM_NONE;
				} else
					skb->ip_summed = CHECKSUM_UNNECESSARY;
			} else if (cqe_current->inet_cksum != 0) {
				/* the device computed a blind checksum */
				skb->csum =
					(__force __sum16) cqe_current->
					inet_cksum;
				skb->ip_summed = CHECKSUM_COMPLETE;
			} else
				skb->ip_summed = CHECKSUM_NONE;

			if (unlikely(khea_vlan_used(kp) &&
				hea_cqe_status_vlan_tag_extracted(cqe_current)))
				__vlan_hwaccel_put_tag(skb,
						cqe_current->vlan_tag);
			netif_receive_skb(skb);

			/* move to next entry */
			cqe_current = kp_qp->r_cq.qe_current;
			continue;
		}

		va = page_address(frags->frags[0].page) +
			frags->frags[0].page_offset;

		if (likely(hea_cqe_marker_ipv4(cqe_current))) {
			/* the device verified the checksum if it is
			 * an IPv4 packet */
			if (unlikely(cqe_current->status &
				(HEA_CQE_STATUS_TCP_CKSUM_ERR_BIT |
				HEA_CQE_STATUS_IP_CKSUM_ERR_BIT))) {

				ip_summed = CHECKSUM_NONE;
				csum = 0;
			} else {
				ip_summed = CHECKSUM_UNNECESSARY;
				if (hea_cqe_marker_l4_valid(cqe_current) &&
					(cqe_current->l4_protocol ==
					IPPROTO_TCP) &
					likely(!(cqe_current->markers &
					HEA_CQE_MARKER_ANY_FRAGMENT_BIT)))
					discard = kp_qp->max_num_lro;
			}
		} else if (cqe_current->inet_cksum != 0) {
			/* the device computed a blind checksum */
			csum = (__force __sum16) cqe_current->inet_cksum;
			ip_summed = CHECKSUM_COMPLETE;
		} else {
			ip_summed = CHECKSUM_NONE;
			csum = 0;
		}

		/* pass skb up the stack */
		if (kp_qp->max_num_lro > 0) {
			lro_receive_frags(&(kp_qp->lro_mgr), frags->frags,
						len, len, NULL, csum);
			kp_qp->recvq_skb[rq][temp] = NULL;
			frags->frags[0].page = NULL;
			++lroed;
		} else {
			skb = kp_qp->recvq_skb[rq][temp];
			kp_qp->recvq_skb[rq][temp] = NULL;
			hlen = len > 64 ? 64 : len;
			skb->len = len;
			skb->data_len = len;
			skb->truesize = len + sizeof(struct sk_buff);

			/* attach the page(s) */
			skb_frags = skb_shinfo(skb)->frags;
			for (i = 0; frags->frags[i].page; i++) {
				memcpy(skb_frags, &(frags->frags[i]),
						sizeof(*skb_frags));
				frags->frags[i].page = NULL;
				skb_frags++;
				skb_shinfo(skb)->nr_frags++;
			}

			skb_copy_to_linear_data(skb, va, hlen);
			skb_shinfo(skb)->frags[0].page_offset += hlen;
			skb_shinfo(skb)->frags[0].size -= hlen;
			skb->data_len -= hlen;
			skb->tail += hlen;

			if (skb_shinfo(skb)->frags[0].size <= 0) {
				khea_debug("freeing fragment 0 page - "
						"all was pull inside header");
				put_page(skb_shinfo(skb)->frags[0].page);
				skb_shinfo(skb)->nr_frags = 0;
			} else
				khea_debug("fragment 0 page still has %d bytes",
						skb_shinfo(skb)->frags[0].size);

			skb->protocol = eth_type_trans(skb, kp->ndev);
			skb->csum = csum;
			skb->ip_summed = ip_summed;

		/* we can handle TCP acceleration: we know that it is */
		/* IPV4, TCP, not fragmented, checksum are correct */
			if (discard) {
				khea_debug2("using LRO");
				lro_receive_skb(&(kp_qp->lro_mgr), skb, NULL);
				lroed++;
			} else {
				khea_debug2("using standard skb");
				if (unlikely(khea_vlan_used(kp) &&
					hea_cqe_status_vlan_tag_extracted(
					    cqe_current)))
					__vlan_hwaccel_put_tag(skb,
							cqe_current->vlan_tag);
				netif_receive_skb(skb);
			}
		}
		/* move to next entry */
		cqe_current = kp_qp->r_cq.qe_current;
		continue;
	}

	if (lroed > 0)
		lro_flush_all(&(kp_qp->lro_mgr));

	if (num > 0) {
		out_be64(&(kp_qp->r_cq_registers->cq_feca), num);
		khea_fill_recvq(kp_qp, GFP_ATOMIC);
	}

	return num;
}

void khea_scan_send_cq(struct khea_qp_vec *kp_qp)
{
	struct hea_cqe *cqe_current;
	struct khea_private *kp;
	struct sk_buff *skb;
	int num = 0;

	khea_debug("processing send for qpair %d", kp_qp->qp_id);

	kp = kp_qp->parent;
	cqe_current = kp_qp->s_cq.qe_current;
	while (hea_cqe_is_valid(cqe_current, kp_qp->s_cq.q_toggle_bit)) {
		BUG_ON(hea_cqe_is_receive(cqe_current));

		/* check if this is an error */
		if (hea_cqe_has_status(cqe_current))
			khea_error("has send error");

		while (kp_qp->send_q_offset != cqe_current->wr_id) {
			skb = kp_qp->sendq[kp_qp->send_q_offset];
			if (skb) {
				kp_qp->sendq[kp_qp->send_q_offset] = NULL;
				dev_kfree_skb_any(skb);
			}

			/* mark that one more WQE is available in SQ */
			heaq_inc_count(&(kp_qp->sq));

			/* move to next entry */
			kp_qp->send_q_offset++;
			if (kp_qp->send_q_offset >=
				heaq_get_max_offset(&(kp_qp->sq)))
				kp_qp->send_q_offset = 0;
		}

		num++;
		heaq_set_next_qe(&(kp_qp->s_cq));
		cqe_current = kp_qp->s_cq.qe_current;
	}

	if (num > 0) {
		khea_debug2("qp_id(%d), processed %d entries for sq",
				kp_qp->qp_id, num);
		out_be64(&(kp_qp->s_cq_registers->cq_feca), num);
	}
}

void khea_scan_event_queue(struct khea_qp_vec *kp_qp)
{
	struct hea_eqe *eqe_current;
	struct khea_private *kp;
	unsigned q_nr;
	unsigned long long value;

	kp = kp_qp->parent;
	eqe_current = kp_qp->eq.qe_current;

	while (hea_eqe_is_valid(eqe_current)) {
		if (unlikely(hea_eqe_is_completion(eqe_current)))
			khea_error(" event 0x%016llx",
					*((unsigned long long *)eqe_current));
		else {
			khea_info(" event 0x%016llx", eqe_current->eqe);

			switch (hea_eqe_event_type(eqe_current)) {
			case HEA_EQE_ET_QP_WARNING:
				q_nr = hea_eqe_qp_number(eqe_current);
				rhea_qp_get(kp->hea_id, q_nr, HEA_QP_AER_GET,
				&value);

				khea_info("Warning for QP: %u and send "
				"warning: 0x%llx\n",
				q_nr, value);

				rhea_qp_get(kp->hea_id, q_nr, HEA_QP_AERR_GET,
				&value);

				khea_info("Warning for QP: %u and receive "
				"warning: 0x%llx\n",
				q_nr, value);
				break;

			case HEA_EQE_ET_CP_WARNING:
				q_nr = hea_eqe_cq_number(eqe_current);

				rhea_cq_get(kp->hea_id, q_nr, HEA_CQ_AER_GET,
				&value);

				khea_info("Warning for CQ: %u and warning: "
				"0x%llx\n", q_nr, value);
				break;

			case HEA_EQE_ET_QP_ERROR_EQ0:
			case HEA_EQE_ET_QP_ERROR:

				q_nr = hea_eqe_qp_number(eqe_current);

				rhea_qp_get(kp->hea_id, q_nr, HEA_QP_AER_GET,
				&value);
				khea_info("Error for QP: %u and send error: "
				"0x%llx\n",
				q_nr, value);

				rhea_qp_get(kp->hea_id, q_nr, HEA_QP_AERR_GET,
				&value);
				khea_info("Error for QP: %u - receive error: "
				"0x%llx\n",
				q_nr, value);
				break;

			case HEA_EQE_ET_CQ_ERROR_EQ0:
			case HEA_EQE_ET_CQ_ERROR:
				q_nr = hea_eqe_cq_number(eqe_current);

				rhea_cq_get(kp->hea_id, q_nr, HEA_CQ_AER_GET,
				&value);

				khea_info
				("Error for CQ: %u and error: 0x%llx\n",
				q_nr, value);
				break;

			case HEA_EQE_ET_PORT_EVENT:
				khea_info("HEA_EQE_ET_PORT_EVENT\n");
				break;

			case HEA_EQE_ET_EQ_ERROR:
				khea_info("HEA_EQE_ET_EQ_ERROR\n");
				break;

			case HEA_EQE_ET_UA_ERROR:
				khea_info("HEA_EQE_ET_UA_ERROR\n");
				break;

			case HEA_EQE_ET_FIRST_ERROR_CAPTURE_INFO:
				khea_info("First Error Capture info\n");
				break;

			case HEA_EQE_ET_COP_CQ_ACCESS_ERROR:
				khea_info("HEA_EQE_ET_COP_CQ_ACCESS_ERROR\n");
				break;

			case HEA_EQE_ET_COP_QP_ACCESS_ERROR:
				khea_info("HEA_EQE_ET_COP_QP_ACCESS_ERROR\n");
				break;

			case HEA_EQE_ET_COP_TICKET_ACCESS_ERROR:
			case HEA_EQE_ET_COP_TICKET_ERROR:
			case HEA_EQE_ET_COP_DATA_ERROR:
			default:
				khea_info("Warning unknown error");
			}
		}

		/* clear this entry and move to next one */
		eqe_current->eqe = 0;
		heaq_set_next_qe(&(kp_qp->eq));
		eqe_current = kp_qp->eq.qe_current;
	}
}

int khea_init_allocator(struct khea_private *kp,
			struct khea_page_allocator *ka, int _frag_len)
{
	/* frag_len is rounded to 64 bytes */
	int end, pagelen;
	int frag_len = _frag_len;
	if (frag_len % 64)
		frag_len += 64 - (frag_len % 64);

	pagelen = PAGE_SIZE << kp->frag_alloc_order;
	for (end = 0; end + frag_len < pagelen; end += frag_len)
		continue;

	ka->page = alloc_pages(GFP_ATOMIC | __GFP_COMP, kp->frag_alloc_order);
	ka->offset = 0;
	ka->end = end;
	ka->size = frag_len;
	if (!ka->page)
		return -ENOMEM;

	khea_debug(" %d on 2^%d pages - frag_len=%d, end=%d", _frag_len,
		   kp->frag_alloc_order, frag_len, end);

	return 0;
}

void khea_free_allocator(struct khea_private *kp,
			 struct khea_page_allocator *ka)
{
	if (ka->page) {
		put_page(ka->page);
		ka->page = NULL;
	}
}

int khea_alloc_frag(struct khea_private *kp, struct khea_page_allocator *ka,
		    struct skb_frag_struct *skb_frags)
{
	struct page *page;

	if (unlikely(ka->offset == ka->end)) {
		/* Allocate new page */
		page = alloc_pages(GFP_ATOMIC | __GFP_COMP,
				   kp->frag_alloc_order);
		if (!page)
			return -ENOMEM;

		skb_frags->page = ka->page;
		skb_frags->page_offset = ka->offset;
		ka->page = page;
		ka->offset = 0;
	} else {
		page = ka->page;
		get_page(page);

		skb_frags->page = page;
		skb_frags->page_offset = ka->offset;
		ka->offset += ka->size;
	}

	skb_frags->size = ka->size;

	return 0;
}

int khea_alloc_frags(struct khea_private *kp, struct khea_page_allocator *ka,
		     struct khea_frags *kfrags, int totlen)
{
	int i;

	i = 0;
	while ((i < KHEA_MAX_FRAGS) && totlen > 0) {
		if (khea_alloc_frag(kp, ka, &(kfrags->frags[i]))) {
			i--;
			while (i >= 0) {
				put_page(kfrags->frags[i].page);
				kfrags->frags[i].page = NULL;
				i--;
			}
			return -ENOMEM;
		}
		i++;
		totlen -= ka->size;
	}

	while (i < KHEA_MAX_FRAGS_ALLOC)
		kfrags->frags[i++].page = NULL;

	return 0;
}

int khea_get_skb_header(struct sk_buff *skb, void **iphdr, void **tcph,
			u64 *hdr_flags, void *priv)
{
	skb_reset_network_header(skb);
	skb_set_transport_header(skb, ip_hdrlen(skb));
	*iphdr = ip_hdr(skb);
	*tcph = tcp_hdr(skb);
	*hdr_flags = LRO_IPV4 | LRO_TCP;

	return 0;
}

int khea_get_frag_header(struct skb_frag_struct *frag, void **mac_hdr,
				void **iphdr, void **tcph,
				u64 *hdr_flags, void *priv)
{
	u8 *va;
	struct iphdr *iph;

	va = page_address(frag->page) + frag->page_offset;
	*mac_hdr = (struct ethhdr *) va;

	iph = (struct iphdr *)(va + ETH_HLEN);
	*iphdr = iph;

	*tcph = (u8 *) (iph) + (iph->ihl << 2);

	return 0;
}
