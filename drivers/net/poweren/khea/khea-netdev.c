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
 * khea-netdev.c --  HEA kernel network interface
 *
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_net.h>
#include <linux/scatterlist.h>
#include <linux/if_vlan.h>
#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/inet_lro.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/tcp.h>

#include <khea.h>
#include <khea-netdev.h>
#include <khea-ethtools.h>


#include <hea-qp-regs.h>
#include <hea-cq-regs.h>

/*
 * This function is called when network device transistions to the up state.
 */
int khea_open(struct net_device *dev)
{
	struct khea_private *kp = netdev_priv(dev);
	int err, sched, q;
	u64 status;

	khea_debug(" ");

	if (kp->i_am_up)
		return 0;

	err = khea_init_interface(kp);
	if (err != 0)
		return err;

	netif_carrier_off(dev);

	/* Alloc receive buffers for each queue */
	/* pair and push in receive queues */
	for (q = 0; q < kp->num_qp; ++q) {
		sched = khea_fill_recvq(&(kp->qp_vec[q]), GFP_KERNEL);

		/* If we didn't even get one input buffer, we're useless. */
		if (sched) {
			khea_error("no memory for input buffers\n");
			khea_fini_interface(kp);
			return -ENOMEM;
		}
	}

	/* bring up QP and all channels */
	for (q = 0; q < kp->num_qp; ++q) {
		err = rhea_qp_up(kp->hea_id, kp->qp_vec[q].qp_id);
		if (err) {
			khea_error("%d bringing up QP", err);
			khea_fini_interface(kp);
			return err;
		}
	}

	err = rhea_channel_enable(kp->hea_id, kp->bc_channel_id);
	if (err) {
		khea_error("%d bringing up BC channel", err);
		khea_fini_interface(kp);
		return err;
	}

	if (kp->uc_channel_id != 0) {
		err = rhea_channel_enable(kp->hea_id, kp->uc_channel_id);
		if (err) {
			khea_error("%d bringing up UC channel", err);
			khea_fini_interface(kp);
			return err;
		}
	}

	if (kp->mc_channel_id != 0) {
		err = rhea_channel_enable(kp->hea_id, kp->mc_channel_id);
		if (err) {
			khea_error("%d bringing up MC channel", err);
			khea_fini_interface(kp);
			return err;
		}
	}

	err = rhea_channel_enable(kp->hea_id, kp->lport_channel_id);
	if (err) {
		khea_error("%d bringing up channel", err);
		khea_fini_interface(kp);
		return err;
	}

	rhea_channel_feature_get(kp->hea_id, kp->lport_channel_id,
				HEA_CHANNEL_GET_LINK_STATE,
				&status);

	/* tell kernel whether link is up or down */
	if (status) {
		netif_carrier_on(kp->ndev);
		khea_info("pport %u link is up", kp->rhea_pport + 1);
	} else {
		netif_carrier_off(kp->ndev);
		khea_info("pport %u link is down", kp->rhea_pport + 1);
	}

	kp->i_am_up = 1;

	for (q = 0; q < kp->num_qp; ++q)
		napi_enable(&kp->qp_vec[q].napi);

	kp->interface_up = status;

	netif_start_queue(dev);
	for (q = 0; q < kp->num_qp; ++q)
		if (napi_schedule_prep(&kp->qp_vec[q].napi))
			__napi_schedule(&kp->qp_vec[q].napi);

	return 0;
}

/*
 * This function is called when network device transitions to the down state.
 */
int khea_stop(struct net_device *dev)
{
	struct khea_private *kp;
	int q = 0;
	khea_debug(" ");

	kp = netdev_priv(dev);
	if (kp->i_am_up) {
		kp->i_am_up = 0;

		for (q = 0; q < kp->num_qp; ++q)
			napi_disable(&kp->qp_vec[q].napi);

		netif_carrier_off(dev);
		netif_stop_queue(dev);

		rhea_channel_disable(kp->hea_id, kp->bc_channel_id);

		if (kp->mc_channel_id != 0)
			rhea_channel_disable(kp->hea_id, kp->mc_channel_id);

		if (kp->uc_channel_id != 0)
			rhea_channel_disable(kp->hea_id, kp->uc_channel_id);

		rhea_channel_disable(kp->hea_id, kp->lport_channel_id);
		for (q = 0; q < kp->num_qp; ++q)
			rhea_qp_down(kp->hea_id, kp->qp_vec[q].qp_id);

	}
	khea_fini_interface(kp);

	return 0;
}

/* Called when a packet needs to be transmitted. Must return
 * NETDEV_TX_OK , NETDEV_TX_BUSY.  (can also return NETDEV_TX_LOCKED
 * iff NETIF_F_LLTX)
 */
netdev_tx_t khea_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct khea_private *kp;
	struct snd_wqe_1 *wqe1;
	int err, offset;
	int f, q;
	skb_frag_t *frags;

	kp = netdev_priv(dev);
	if ((kp->interface_up == 0) || (skb->len <= 0)) {
		dev_kfree_skb_any(skb);
		kp->stats_tx_ifdown += 1;
		return NETDEV_TX_OK;
	}

	q = skb_get_queue_mapping(skb);
	khea_debug("skb_get_queue_mapping = %d", q);
	if (heaq_get_count(&kp->qp_vec[q].sq) <= 0) {
		kp->stats_tx_busy += 1;
		return NETDEV_TX_BUSY;
	}

	kp->qp_vec[q].stats_sq_packets++;

	wqe1 = kp->qp_vec[q].sq.qe_current;

	/* clear send WQE */
	((u64 *) &(wqe1->hdr))[0] = 0;
	((u64 *) &(wqe1->hdr))[1] = 0;
	((u64 *) &(wqe1->hdr))[2] = 0;
	((u64 *) &(wqe1->hdr))[3] = 0;

	if (unlikely(kp->qp_vec[q].send_q_inserted == 0)) {
		wqe1->hdr.tx_control =
			HEA_TC_CTRL_DO_ETH_CRC | HEA_TC_CTRL_SIGNAL_COMPLETION |
			HEA_TC_CTRL_WRAP_NORMAL;
		kp->qp_vec[q].send_q_inserted = kp->qp_vec[q].send_q_reclaim;
	} else {
		wqe1->hdr.tx_control =
			HEA_TC_CTRL_DO_ETH_CRC | HEA_TC_CTRL_WRAP_NORMAL;
		kp->qp_vec[q].send_q_inserted--;
	}

	/* handle checksum acceleration */
	if (likely(skb->protocol == htons(ETH_P_IP))) {
		if (skb->ip_summed == CHECKSUM_PARTIAL) {
			wqe1->hdr.tx_control |= HEA_TC_CTRL_DO_IP_CKSUM;

			wqe1->hdr.ip_start = skb_network_offset(skb);
			wqe1->hdr.ip_end = wqe1->hdr.ip_start +
						ip_hdrlen(skb) - 1;

			switch (ip_hdr(skb)->protocol) {
			case IPPROTO_UDP:
				wqe1->hdr.tx_control |=
					HEA_TC_CTRL_DO_UDP_CKSUM |
					HEA_TC_CTRL_DO_TCP_CKSUM;
				wqe1->hdr.cksum_offset =
					wqe1->hdr.ip_end + 1 +
					offsetof(struct udphdr, check);
				break;

			case IPPROTO_TCP:
				wqe1->hdr.tx_control |=
					HEA_TC_CTRL_DO_TCP_CKSUM;
				wqe1->hdr.cksum_offset =
					wqe1->hdr.ip_end + 1 +
					offsetof(struct tcphdr, check);
				break;
			}
		}
	}

	/* handle VLAN acceleration */
	if (unlikely(khea_vlan_used(kp)) &&
			unlikely(vlan_tx_tag_present(skb))) {
		khea_debug2("HEA will insert VLAN tag");
		wqe1->hdr.tx_control |= HEA_TC_CTRL_INSERT_VLAN;
		wqe1->hdr.vlan_tag = vlan_tx_tag_get(skb);
	}

	offset = heaq_get_offset(&kp->qp_vec[q].sq);

	if (unlikely(skb->len <= kp->send_ll_limit)) {
		/* use low latency send */
		struct snd_wqe_3 *wqe3 = (struct snd_wqe_3 *)wqe1;
		u8 *dptr;

		khea_debug2("low latency send of packet len %d "
				"dlen %d frags %d",
				skb->len, skb->data_len,
				skb_shinfo(skb)->nr_frags);

		kp->qp_vec[q].sendq[offset] = NULL;
		wqe3->hdr.wreq_id = offset;
		wqe3->hdr.tx_control |= HEA_TC_CTRL_TYPE_3;

		wqe3->hdr.imm_data_len = skb->len;
		dptr = wqe3->immediate_data;

		skb_copy_from_linear_data(skb, dptr, skb_headlen(skb));

		if (skb_shinfo(skb)->nr_frags > 0) {
			dptr += skb_headlen(skb);
			for (f = 0, frags = &(skb_shinfo(skb)->frags[0]);
					f < skb_shinfo(skb)->nr_frags;
					f++, frags++) {
				memcpy(dptr, (page_address(frags->page) +
						frags->page_offset),
						frags->size);
				dptr += frags->size;
			}
		}
		dev_kfree_skb_any(skb);
	} else if (!skb_is_gso(skb)) {
		/* must use indirect send */
		khea_debug2("normal send of full packet len %d "
				"dlen %d frags %d",
				skb->len, skb->data_len,
				skb_shinfo(skb)->nr_frags);

		kp->qp_vec[q].sendq[offset] = skb;
		wqe1->hdr.wreq_id = offset;
		wqe1->hdr.tx_control |= HEA_TC_CTRL_TYPE_1;
		wqe1->hdr.num_descriptors = skb_shinfo(skb)->nr_frags + 1;

		hea_wqe_address_set(
				&wqe1->descriptors[0],
				(ulong)  skb->data,
				skb_headlen(skb));

		if (skb_shinfo(skb)->nr_frags > 0) {
			for (f = 0, frags = &(skb_shinfo(skb)->frags[0]);
					f < skb_shinfo(skb)->nr_frags &&
					f < kp->send_max_desc_1; f++) {

				hea_wqe_address_set(&wqe1->descriptors[f + 1],
					(ulong)(page_address(frags->page) +
					frags->page_offset),
					frags->size);

				frags++;
			}
		}
	} else {		/* here we have GSO */
		struct snd_wqe_2 *wqe2 = (struct snd_wqe_2 *)wqe1;
		int headersize, skb_data_size, i;

		khea_debug2("doing GSO nfrags %d (%d), datalen %d, "
				"len%d, hdr %d, skbd %d, mss %d",
				skb_shinfo(skb)->nr_frags, kp->send_max_desc_2,
				skb->data_len, skb->len,
				ETH_HLEN + ip_hdrlen(skb) + tcp_hdrlen(skb),
				skb->len - skb->data_len,
				skb_shinfo(skb)->gso_size);

		if (unlikely(skb_header_cloned(skb))) {
			err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
			if (err)
				return err;
		}

		headersize = ETH_HLEN + ip_hdrlen(skb) + tcp_hdrlen(skb);
		skb_data_size = skb->len - skb->data_len;
		kp->qp_vec[q].sendq[offset] = skb;
		wqe2->hdr.wreq_id = offset;

		wqe2->hdr.tx_control |=
				HEA_TC_CTRL_TYPE_2 | HEA_TC_CTRL_TSO_ENABLE;
		wqe2->hdr.mss = skb_shinfo(skb)->gso_size;
		ip_hdr(skb)->check = 0;
		tcp_hdr(skb)->check = 0;
		f = 0;

		if (likely(skb_data_size >= headersize)) {
			/* copy immediate data */
			skb_copy_from_linear_data(skb, wqe2->immediate_data,
					headersize);
			wqe2->hdr.imm_data_len = headersize;
			if (skb_data_size > headersize) {

				/* put more data in first descriptor */
				hea_wqe_address_set(&wqe2->descr0,
					(ulong) (skb->data + headersize),
					(skb_data_size - headersize));
				f++;
			}
		} else {
			khea_error("cannot handle fragmented headers");
			dev_kfree_skb_any(skb);
			kp->stats_tx_other += 1;
			return NETDEV_TX_OK;
		}

		/* now put rest of fragments */
		if (skb_shinfo(skb)->nr_frags > 0) {
			i = 0;
			frags = &(skb_shinfo(skb)->frags[0]);
			if (!f) {
				hea_wqe_address_set(&wqe2->descr0,
					(ulong)(page_address(frags->page) +
					frags->page_offset),
					frags->size);

				frags++;
				i++;
			}

			for ( ; i < skb_shinfo(skb)->nr_frags &&
				f < kp->send_max_desc_2; i++, f++, frags++) {

				hea_wqe_address_set(&wqe2->descriptors[f],
					(ulong)(page_address(frags->page) +
					frags->page_offset),
					frags->size);
			}
		}
		wqe2->hdr.num_descriptors = f + 1;
	}

	/* write in add register */
	iosync();
	out_be64(&kp->qp_vec[q].qp_registers->qp_sqa, 1);

	/* mark one less WQE in SQ */
	heaq_dec_count(&kp->qp_vec[q].sq);

	/* move to next SQ WQE */
	heaq_set_next_qe(&kp->qp_vec[q].sq);

	return NETDEV_TX_OK;
}

/* u16 (*ndo_select_queue)(struct net_device *dev, struct sk_buff *skb);
 * Called to decide which queue to when device supports multiple
 * transmit queues. Currently use default kernel function.
 */
u16 khea_select_queue(struct net_device *dev, struct sk_buff *skb)
{
	/*struct khea_private *kp = netdev_priv(dev); */
	khea_debug(" ");
	return 0;
}

struct net_device_stats *khea_get_stats(struct net_device *dev)
{
	struct khea_private *kp = netdev_priv(dev);
	struct net_device_stats *stats;
	struct hea_channel_counters counter;
	int err;

	khea_debug(" ");

	stats = &kp->stats;

	if (kp->inited == 0)
		return stats;

	err = rhea_channel_counters_get(kp->hea_id, kp->lport_channel_id,
					HEA_LPORT_COUNTERS, &counter);
	if (err != 0)
		return stats;

	/* total packets received       */
	stats->rx_packets =
		counter.lport_counter.pl_rxucp +
		counter.lport_counter.pl_rxbcp +
		counter.lport_counter.pl_rxmcp;

	/* total packets transmitted    */
	stats->tx_packets =
		counter.lport_counter.pl_txucp +
		counter.lport_counter.pl_txbcp +
		counter.lport_counter.pl_txmcp;

	/* total bytes received         */
	stats->rx_bytes = counter.lport_counter.pl_rxo;

	/* total bytes transmitted      */
	stats->tx_bytes = counter.lport_counter.pl_txo;

	/* bad packets received         */
	stats->rx_errors =
		counter.lport_counter.pl_rxerr +
		counter.lport_counter.pl_rxfd + counter.lport_counter.pl_rxftl;

	/* packet transmit problems     */
	stats->tx_errors = counter.lport_counter.pl_txfd;

	/* no space in linux buffers    */
	stats->rx_dropped =
		counter.lport_counter.pl_rxwdd + kp->stats_rx_dropped;

	/* no space available in linux  */
	stats->tx_dropped =
		kp->stats_tx_ifdown + kp->stats_tx_busy + kp->stats_tx_other;

	/* multicast packets received   */
	stats->multicast = counter.lport_counter.pl_txmcp;
	stats->collisions = 0;

	/* detailed rx_errors */
	stats->rx_length_errors = kp->stats_rx_length_errors;
	stats->rx_over_errors = 0;
	stats->rx_crc_errors = kp->stats_rx_crc_errors;

	stats->rx_frame_errors = kp->stats_rx_frame_errors;
	stats->rx_fifo_errors = kp->stats_rx_fifo_errors;
	stats->rx_missed_errors = 0;

	/* detailed tx_errors */
	stats->tx_aborted_errors = 0;
	stats->tx_carrier_errors = kp->stats_tx_ifdown;

	stats->tx_fifo_errors = kp->stats_tx_busy;
	stats->tx_heartbeat_errors = 0;
	stats->tx_window_errors = 0;

	return stats;
}

/* void (*ndo_set_rx_mode)(struct net_device *dev);
 *      This function is called device changes address list filtering.
 */
void khea_set_rx_mode(struct net_device *dev)
{
	struct netdev_hw_addr *ha;
	u8 promisc, allmulti;
	struct netdev_hw_addr *mca;
	int i, err;
	struct khea_private *kp = netdev_priv(dev);
	khea_debug(" ");

	promisc = ((dev->flags & IFF_PROMISC) != 0);
	allmulti = ((dev->flags & IFF_ALLMULTI) != 0);

	if (promisc) {
		khea_debug("Must receive all packets (promisc mode)");
		/* free unicast filter list (if any) */
		kfree(kp->uc_list);
		kp->uc_list_len = 0;
	} else if (dev->uc.count > 0) {
		khea_debug("I have other %d unicast addresses\n",
			   dev->uc.count);

		/* free old unicast filter list (if any ) */
		kfree(kp->uc_list);
		kp->uc_list_len = 0;

		/* build new unicast filter list */
		kp->uc_list = kmalloc(dev->uc.count * ETH_ALEN, GFP_ATOMIC);
		if (!kp->uc_list)
			khea_error("out of memory for UC filter list");
		else {
			kp->uc_list_len = dev->uc.count;
			i = 0;
			list_for_each_entry(ha, &dev->uc.list, list) {
				khea_debug("UC addr 0x%lx",
					   *((const unsigned long *)ha->addr));
				memcpy(kp->uc_list + (i * ETH_ALEN), ha->addr,
				       ETH_ALEN);
				i++;
			}
		}
		promisc = 1;
	}

	if (allmulti) {
		khea_debug("Must receive all multicast packets");

		/* free multicast filter list (if any) */
		kfree(kp->mc_list);
		kp->mc_list_len = 0;
	} else if (netdev_mc_count(dev) > 0) {
		khea_debug("I have other %d multicast addresses\n",
			   netdev_mc_count(dev));

		/* free old multicast filter list (if any) */
		kfree(kp->mc_list);
		kp->mc_list_len = 0;

		/* build new multicast filter list */
		kp->mc_list =
			kmalloc(netdev_mc_count(dev) * ETH_ALEN, GFP_ATOMIC);
		if (!kp->mc_list)
			khea_error("out of memory for MC filter list");
		else {
			kp->mc_list_len = netdev_mc_count(dev);

			i = 0;
			netdev_for_each_mc_addr(mca, dev) {
				khea_debug("MC addr 0x%lx",
					   *((const unsigned long *)mca->
					     addr));
				memcpy(kp->mc_list + (i * ETH_ALEN), mca->addr,
				       ETH_ALEN);
				++i;
			}
			allmulti = 1;
		}
	}

	if (allmulti && kp->mc_channel_id == 0) {
		struct hea_channel_context context_channel;
		memset(&context_channel, 0, sizeof(context_channel));

		/* create MC channel and manager */
		context_channel.cfg.type = HEA_MC_PORT;
		context_channel.cfg.bc.lport_channel_id = kp->lport_channel_id;
		context_channel.cfg.bc.channel_usuage =
			HEA_DEFAULT_CHANNEL_SHARE;

		err = rhea_channel_alloc(kp->hea_id, &kp->mc_channel_id,
					 &context_channel);
		if (err != 0) {
			khea_error("cannot init MULTICAST on adapter %d, "
				   "port %d - error %d",
				   kp->rhea_adapter, kp->rhea_pport, err);
			kp->mc_channel_id = 0;
		} else
			rhea_channel_enable(kp->hea_id, kp->mc_channel_id);
	} else if (!allmulti && kp->mc_channel_id != 0) {
		/* we do not want multicast anymore - disable it */
		rhea_channel_disable(kp->hea_id, kp->mc_channel_id);
		rhea_channel_free(kp->hea_id, kp->mc_channel_id);
		kp->mc_channel_id = 0;
	}

	if (promisc && kp->uc_channel_id == 0) {
		struct hea_channel_context context_channel;
		memset(&context_channel, 0, sizeof(context_channel));

		/* create UC channel and manager */
		context_channel.cfg.type = HEA_UC_PORT;
		context_channel.cfg.bc.lport_channel_id = kp->lport_channel_id;
		context_channel.cfg.bc.channel_usuage =
			HEA_DEFAULT_CHANNEL_SHARE;

		err = rhea_channel_alloc(kp->hea_id, &kp->uc_channel_id,
					 &context_channel);
		if (err != 0) {
			khea_error("cannot init UNICAST on adapter %d, "
				   "port %d - error %d",
				 kp->rhea_adapter, kp->rhea_pport, err);
			kp->uc_channel_id = 0;
		} else
			rhea_channel_enable(kp->hea_id, kp->uc_channel_id);
	} else if (!promisc && kp->uc_channel_id != 0) {
		/* we do not want all unicast anymore - disable it */
		rhea_channel_disable(kp->hea_id, kp->uc_channel_id);
		rhea_channel_free(kp->hea_id, kp->uc_channel_id);
		kp->uc_channel_id = 0;
	}
}

/*
 * void (*ndo_set_multicast_list)(struct net_device *dev);
 *      This function is called when the multicast address list changes.
 */
void khea_set_multicast_list(struct net_device *dev)
{
	struct khea_private *kp = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	int i;

	khea_debug(" ");

	/* free old multicast filter list (if any) */
	kfree(kp->mc_list);
	kp->mc_list_len = 0;

	if (netdev_mc_count(dev) > 0) {
		/* build new multicast filter list */

		kp->mc_list =
			kmalloc(netdev_mc_count(dev) * ETH_ALEN, GFP_ATOMIC);
		if (!kp->mc_list)
			khea_error("out of memory for MC filter list");
		else {
			kp->mc_list_len = netdev_mc_count(dev);
			i = 0;
			netdev_for_each_mc_addr(ha, dev) {
				khea_debug("MC addr 0x%lx",
					   *((const unsigned long *)ha->addr));
				memcpy(kp->mc_list + (i * ETH_ALEN), ha->addr,
				       ETH_ALEN);
				++i;
			}
		}
	}
}

/*
 * int (*ndo_set_mac_address)(struct net_device *dev, void *addr);
 * This function  is called when the Media Access Control address
 * needs to be changed. If this interface is not defined, the
 * mac address can not be changed.
 */
int khea_set_mac_address(struct net_device *dev, void *p)
{
	/*struct khea_private *kp = netdev_priv(dev); */
	struct sockaddr *addr = p;

	khea_debug(" ");

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	/*
	 * set the MAC address and return 0
	 * write the PxLy_MAC register bits 16:63
	 */
	return -EADDRNOTAVAIL;
}

/*
 * int (*ndo_validate_addr)(struct net_device *dev);
 *      Test if Media Access Control address is valid for the device.
 */
int khea_validate_addr(struct net_device *dev)
{
	khea_debug(" ");

	if (!is_valid_ether_addr(dev->dev_addr)) {
		khea_debug("invalid address 0x%lx",
			   *((const unsigned long *)dev->dev_addr));
		return -EADDRNOTAVAIL;
	}

	return 0;
}

/*
 * int (*ndo_do_ioctl)(struct net_device *dev, struct ifreq *ifr, int cmd);
 * Called when a user request an ioctl which can't be handled by
 * the generic interface code. If not defined ioctl's return
 * not supported error code.
 */
int khea_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	/* struct khea_private *kp = netdev_priv(dev); */
	khea_debug(" ");
	return 0;
}

/*
 * int (*ndo_change_mtu)(struct net_device *dev, int new_mtu);
 *      Called when a user wants to change the Maximum Transfer Unit
 *      of a device. If not defined, any request to change MTU will
 *      will return an error.
 */
int khea_change_mtu(struct net_device *dev, int new_mtu)
{
	int q = 0;
	int new_rq_size;
	struct khea_private *kp = netdev_priv(dev);

	for (q = 0; q < kp->num_qp; ++q)
		khea_debug("from %d to %d (%d)", dev->mtu, new_mtu,
		   kp->qp_vec[q].recv_rq_size[kp->recv_q_num - 1]);

	if (dev->mtu == new_mtu)
		return 0;

	if (new_mtu > 9000)
		return -EOVERFLOW;

	dev->mtu = new_mtu;
	kp->mtu = new_mtu + 22;

	khea_stop(dev);

	/* change RQx max packet size */
	for (q = 0; q < kp->num_qp; ++q) {
		if (kp->mtu >= kp->qp_vec[q].recv_rq_size[kp->recv_q_num - 1]) {
			new_rq_size = 1;
			while (new_rq_size < kp->mtu)
				new_rq_size <<= 1;
			kp->qp_vec[q].recv_rq_size[kp->recv_q_num - 1] =
								new_rq_size;

			khea_debug("updating rq(%d) to %d", kp->recv_q_num - 1,
				kp->qp_vec[q].
					recv_rq_size[kp->recv_q_num - 1]);
		}
	}
	khea_open(dev);

	return 0;
}

/*
 * void (*ndo_tx_timeout)(struct net_device *dev);
 * Callback uses when the transmitter has not made any progress
 * for dev->watchdog ticks.
 */
void khea_tx_timeout(struct net_device *dev)
{
	/*struct khea_private *kp = netdev_priv(dev); */
	khea_debug(" ");
}

static void khea_vlan_filter_on_off(struct khea_private *kp, bool on)
{
	u64 value;
	int err;

	if (on) {
		khea_debug("enable VLAN insert/strip");
		value = 1;
		err = rhea_channel_feature_set(kp->hea_id,
					       kp->lport_channel_id,
					       HEA_CHANNEL_SET_VLAN_EXTRACT,
					       value);
		if (err < 0)
			khea_error
				("error %d in HEA_CHANNEL_SET_VLAN_EXTRACT=1",
				 err);
		value = 0;
		err = rhea_channel_feature_set(kp->hea_id,
					       kp->lport_channel_id,
					       HEA_CHANNEL_SET_DISCARD_UNTAGGED,
					       value);
		if (err < 0)
			khea_error("error %d in "
				   "HEA_CHANNEL_SET_DISCARD_UNTAGGED=0",
				   err);

		value = HEA_VLAN_SELECTIVELY_FILTER;
		err = rhea_channel_feature_set(kp->hea_id,
					       kp->lport_channel_id,
					       HEA_CHANNEL_SET_TAG_FILTER_MODE,
					       value);
		if (err < 0)
			khea_error("error %d in "
				   "HEA_CHANNEL_SET_TAG_FILTER_MODE="
				   "HEA_VLAN_HEA_VLAN_SELECTIVELY_FILTER",
				   err);
	} else {
		khea_debug("disable VLAN insert/strip");
		value = 0;
		err = rhea_channel_feature_set(kp->hea_id,
					       kp->lport_channel_id,
					       HEA_CHANNEL_SET_VLAN_EXTRACT,
					       value);
		if (err < 0)
			khea_error
				("error %d in HEA_CHANNEL_SET_VLAN_EXTRACT=0",
				 err);

		value = 0;
		err = rhea_channel_feature_set(kp->hea_id,
					       kp->lport_channel_id,
					       HEA_CHANNEL_SET_DISCARD_UNTAGGED,
					       value);
		if (err < 0)
			khea_error("error %d in "
				   "HEA_CHANNEL_SET_DISCARD_UNTAGGED=0",
				   err);

		value = HEA_VLAN_DISCARD_ALL;
		err = rhea_channel_feature_set(kp->hea_id,
					       kp->lport_channel_id,
					       HEA_CHANNEL_SET_TAG_FILTER_MODE,
					       value);
		if (err < 0)
			khea_error("error %d in "
				   "HEA_CHANNEL_SET_TAG_FILTER_MODE="
				   "HEA_VLAN_DISCARD_ALL",
				   err);

		value = 0;
		err = rhea_channel_feature_set(
			kp->hea_id,
			kp->lport_channel_id,
			HEA_CHANNEL_CLEAR_ALL_VLAN_FILTERS,
			value);
		if (err < 0)
			khea_error("error %d in "
				   "HEA_CHANNEL_CLEAR_ALL_VLAN_FILTERS",
				   err);
	}
}

/*
 * void (*ndo_vlan_rx_add_vid)(struct net_device *dev, unsigned short vid);
 *      If device support VLAN filtering (dev->features &
 *      NETIF_F_HW_VLAN_FILTER) this function is called when a VLAN id
 *      is registered.
 */
void khea_vlan_rx_add_vid(struct net_device *dev, unsigned short vid)
{
	struct khea_private *kp = netdev_priv(dev);
	u64 value;
	int err;
	ulong flags;

	khea_debug(" VLAN ID=`%x", vid);

	value = vid;

	spin_lock_irqsave(&kp->vlanlock, flags);

	if (!khea_vlan_used(kp))
		khea_vlan_filter_on_off(kp, true);

	err = rhea_channel_feature_set(kp->hea_id, kp->lport_channel_id,
				       HEA_CHANNEL_SET_VLAN_FILTER, value);
	if (err >= 0)
		set_bit(vid, kp->active_vlans);
	else
		khea_error("error %d in HEA_CHANNEL_SET_VLAN_FILTER for %d",
			   err, vid);
	spin_unlock_irqrestore(&kp->vlanlock, flags);
}

/*
 * void (*ndo_vlan_rx_kill_vid)(struct net_device *dev, unsigned short vid);
 *      If device support VLAN filtering (dev->features &
 *      NETIF_F_HW_VLAN_FILTER) this function is called when a VLAN id
 *      is unregistered.
 */
void khea_vlan_rx_kill_vid(struct net_device *dev, unsigned short vid)
{
	struct khea_private *kp = netdev_priv(dev);
	u64 value;
	int err;
	ulong flags;

	khea_debug(" VLAN ID=%x", vid);

	value = vid;

	spin_lock_irqsave(&kp->vlanlock, flags);
	err = rhea_channel_feature_set(kp->hea_id, kp->lport_channel_id,
				       HEA_CHANNEL_CLEAR_VLAN_FILTER, value);
	if (err >= 0)
		clear_bit(vid, kp->active_vlans);
	else
		khea_error("error %d in HEA_CHANNEL_CLEAR_VLAN_FILTER for %d",
			   err, vid);

	if (!khea_vlan_used(kp))
		khea_vlan_filter_on_off(kp, false);

	spin_unlock_irqrestore(&kp->vlanlock, flags);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
void khea_poll_controller(struct net_device *dev)
{
	struct khea_private *kp = netdev_priv(dev);
	int q;

	khea_debug(" ");

	for (q = 0; q < kp->num_qp; ++q)
		napi_schedule(&kp->qp_vec[q].napi);
}
#endif

int khea_poll(struct napi_struct *napi, int budget)
{
	struct khea_qp_vec  *kp_qp =
		container_of(napi, struct khea_qp_vec, napi);
	/*  unsigned int received_all=0; */
	unsigned int received = 0;

	khea_debug2("polling qpair %d", kp_qp->qp_id);

	/* cleanup receive queue */
	received = khea_scan_recv_cq(kp_qp, budget);
	khea_scan_send_cq(kp_qp);


	while (received < budget) {
		napi_complete(napi);

		/* re-enable interrupts */
		out_be64(&(kp_qp->r_cq_registers->cq_ep), 0);
		out_be64(&(kp_qp->r_cq_registers->cq_n1), ((u64) 1) << 63);
		out_be64(&(kp_qp->s_cq_registers->cq_ep), 0);
		out_be64(&(kp_qp->s_cq_registers->cq_n1), ((u64) 1) << 63);

		/* barrier */
		rmb();

		if (!hea_cqe_is_valid((struct hea_cqe *)kp_qp->r_cq.qe_current,
			kp_qp->r_cq.q_toggle_bit) &&
			!hea_cqe_is_valid(
				(struct hea_cqe *)kp_qp->s_cq.qe_current,
				kp_qp->s_cq.q_toggle_bit))
			return received;

		if (!napi_reschedule(napi))
			return received;

		received += khea_scan_recv_cq(kp_qp, budget - received);
		khea_scan_send_cq(kp_qp);
	}

	return received;
}
