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
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_net.h>
#include <linux/scatterlist.h>
#include <linux/if_vlan.h>
#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/inet_lro.h>

#include <khea.h>
#include <khea-netdev.h>
#include <khea-ethtools.h>

#include <hea-cq-regs.h>

MODULE_DESCRIPTION("HEA kernel ethernet driver");
MODULE_AUTHOR("Davide Pasetto");
MODULE_LICENSE("GPL");

/* which adapter interfaces to enable (enable bits: adapter*16 +
 * pport*4 + lport) */
static long khea_enable = CONFIG_KHEA_ENABLE;
module_param(khea_enable, long, 0444);


/* number of q-pairs to use */
static char *num_qp[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "" };
static int num_qp_c;
module_param_array(num_qp, charp, &num_qp_c, 0444);

/* napi weight when receiving packets */
static char *napi_weight[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int napi_weight_c;
module_param_array(napi_weight, charp, &napi_weight_c, 0444);

/* length of send Q */
static char *send_q_len[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int send_q_len_c;
module_param_array(send_q_len, charp, &send_q_len_c, 0444);

/* interval for send Q reclaim */
static char *send_q_reclaim[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int send_q_reclaim_c;
module_param_array(send_q_reclaim, charp, &send_q_reclaim_c, 0444);

/* length of QP.RQ1 */
static char *recv_rq1_len[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int recv_rq1_len_c;
module_param_array(recv_rq1_len, charp, &recv_rq1_len_c, 0444);

/* length of QP.RQ2 */
static char *recv_rq2_len[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int recv_rq2_len_c;
module_param_array(recv_rq2_len, charp, &recv_rq2_len_c, 0444);

/* low buffer level trigger for QP.RQ1 */
static char *recv_rq1_low[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int recv_rq1_low_c;
module_param_array(recv_rq1_low, charp, &recv_rq1_low_c, 0444);

/* low buffer level trigger for QP.RQ2 */
static char *recv_rq2_low[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int recv_rq2_low_c;
module_param_array(recv_rq2_low, charp, &recv_rq2_low_c, 0444);

/* Number of TCP sessions to track for LRO */
static char *max_num_lro[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int max_num_lro_c;
module_param_array(max_num_lro, charp, &max_num_lro_c, 0444);

/* how many receive Q to use */
static char *recv_q_num[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int recv_q_num_c;
module_param_array(recv_q_num, charp, &recv_q_num_c, 0444);

/* data size for QP.RQ1 */
static char *recv_rq1_size[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int recv_rq1_size_c;
module_param_array(recv_rq1_size, charp, &recv_rq1_size_c, 0444);

/* data size for QP.RQ2 */
static char *recv_rq2_size[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int recv_rq2_size_c;
module_param_array(recv_rq2_size, charp, &recv_rq2_size_c, 0444);

/* maximum number of bytes to send using Low Latency send */
static char *send_ll_limit[16] = {
	"*-*-*-80", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", ""
};
static int send_ll_limit_c;
module_param_array(send_ll_limit, charp, &send_ll_limit_c, 0444);

/* type of send WQE */
static char *send_wqe_type[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int send_wqe_type_c;
module_param_array(send_wqe_type, charp, &send_wqe_type_c, 0444);

/* basic name for HEA interfaces in the kernel */
static char *basename = CONFIG_KHEA_BASENAME;
module_param(basename, charp, 0444);

/* use Low Latency receive RQ1 */
static char *use_ll_rq1[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int use_ll_rq1_c;
module_param_array(use_ll_rq1, charp, &use_ll_rq1_c, 0444);

/* implement GSO */
static char *do_gso[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int do_gso_c;
module_param_array(do_gso, charp, &do_gso_c, 0444);

/* alloc order for skb fragments/buffers */
static char *frag_alloc_order[16] = {
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};
static int frag_alloc_order_c;
module_param_array(frag_alloc_order, charp, &frag_alloc_order_c, 0444);

static struct khea_private *all_kp[KHEA_MAX_DEVICES] = { NULL };


static const struct net_device_ops khea_ops = {
	.ndo_open = khea_open,
	.ndo_stop = khea_stop,
	.ndo_start_xmit = khea_start_xmit,
	/*	.ndo_select_queue = khea_select_queue, */
	.ndo_set_rx_mode = khea_set_rx_mode,
	.ndo_set_multicast_list = khea_set_multicast_list,
	.ndo_set_mac_address = khea_set_mac_address,
	.ndo_validate_addr = khea_validate_addr,
	/*	.ndo_do_ioctl           = khea_do_ioctl, */
	.ndo_change_mtu = khea_change_mtu,
	.ndo_tx_timeout = khea_tx_timeout,
	.ndo_get_stats = khea_get_stats,
	.ndo_vlan_rx_add_vid = khea_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = khea_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = khea_poll_controller,
#endif
};


static const struct ethtool_ops khea_eops = {
	.set_tx_csum = ethtool_op_set_tx_hw_csum,
	.set_sg = ethtool_op_set_sg,
	.set_tso = ethtool_op_set_tso,
	.get_link = ethtool_op_get_link,
	.get_flags = ethtool_op_get_flags,
	.get_settings = khea_get_settings,
	.get_drvinfo = khea_get_drvinfo,
	.get_strings = khea_get_strings,
	.get_ethtool_stats = khea_get_ethtool_stats,
	.get_sset_count = khea_get_sset_count,
};


struct etherdev_id {
	int rhea_adapter;
	int rhea_pport;
	int rhea_lport;
};


struct module_inputs {
	unsigned send_max_desc_1;
	unsigned send_max_desc_2;

	int num_qpair;
	int qpn_shift;

	int send_ll_limit;
	int send_wqe_type;
	int send_cq_len;
	int send_q_len;
	int send_q_reclaim;

	int max_num_lro;
	int recv_cq_len;
	int recv_rq1_len;
	int recv_rq2_len;

	int recv_rq1_low;
	int recv_rq2_low;
	int recv_rq1_size;
	int recv_rq2_size;

	int recv_q_num;
	int use_ll_rq1;

	int do_gso;
	int frag_alloc_order;
	int napi_w;
};


static int module_params_get(int adapter, int pport, int lport, char **values,
			 int values_c, int def)
{
	int i;
	char *t;
	int v;

	for (i = 0; i < values_c; i++) {
		t = values[i];
		if (!t)
			continue;

		if (*t != '*' && !kstrtoint(t, 10, &v) && v != adapter)
			continue;

		t = strchr(t, '-');
		if (!t)
			continue;
		t++;

		if (*t != '*' && !kstrtoint(t, 10, &v) && v != pport)
			continue;

		t = strchr(t, '-');
		if (!t)
			continue;
		t++;

		if (*t != '*' && !kstrtoint(t, 10, &v) && v != lport)
			continue;

		t = strchr(t, '-');
		if (!t)
			continue;
		t++;

		if (!kstrtoint(t, 10, &v))
			return v;
		else
			return 0;
	}
	return def;
}


static void module_params_parse(struct module_inputs *in, int rhea_adapter,
					int rhea_pport, int rhea_lport)
{
	in->num_qpair =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, num_qp, num_qp_c,
			CONFIG_KHEA_NUM_QP);

	in->napi_w =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, napi_weight,
			napi_weight_c,
			CONFIG_KHEA_NAPI_WEIGHT);

	in->send_ll_limit =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, send_ll_limit,
			send_ll_limit_c,
			CONFIG_KHEA_SEND_LL_LIMIT);
	in->send_wqe_type =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, send_wqe_type,
			send_wqe_type_c,
			CONFIG_KHEA_SEND_WQE_TYPE);
	in->send_q_len =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, send_q_len,
			send_q_len_c,
			CONFIG_KHEA_SENDQ_LEN);
	in->send_q_reclaim =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, send_q_reclaim,
			send_q_reclaim_c,
			CONFIG_KHEA_SENDQ_RECLAIM);

	in->recv_rq1_len =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, recv_rq1_len,
			recv_rq1_len_c,
			CONFIG_KHEA_RECVQ1_LEN);
	in->recv_rq2_len =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, recv_rq2_len,
			recv_rq2_len_c,
			CONFIG_KHEA_RECVQ2_LEN);
	in->recv_rq1_low =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, recv_rq1_low,
			recv_rq1_low_c,
			CONFIG_KHEA_RECVQ1_LOW);
	in->recv_rq2_low =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, recv_rq2_low,
			recv_rq2_low_c,
			CONFIG_KHEA_RECVQ2_LOW);
	in->recv_rq1_size =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, recv_rq1_size,
			recv_rq1_size_c,
			CONFIG_KHEA_RECVQ1_SIZE);
	in->recv_rq2_size =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, recv_rq2_size,
			recv_rq2_size_c,
			CONFIG_KHEA_RECVQ2_SIZE);
	in->recv_q_num =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, recv_q_num,
			recv_q_num_c,
			CONFIG_KHEA_RECVQ_NUM);

	in->max_num_lro =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, max_num_lro,
			max_num_lro_c,
			CONFIG_KHEA_MAX_LRO);
	in->use_ll_rq1 =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, use_ll_rq1,
			use_ll_rq1_c,
			CONFIG_KHEA_USE_LL_RQ1);

	in->do_gso =
		module_params_get(rhea_adapter, rhea_pport,
		rhea_lport, do_gso, do_gso_c,
		CONFIG_KHEA_DO_GSO);
	in->frag_alloc_order =
		module_params_get(rhea_adapter, rhea_pport,
			rhea_lport, frag_alloc_order,
			frag_alloc_order_c,
			CONFIG_KHEA_FRAG_ALLOC_ORDER);
}


static int module_params_validate(struct module_inputs *in, int rhea_adapter,
					int rhea_pport, int rhea_lport)
{
	const struct khea_sanity {
		unsigned limit_2;
		unsigned descr_1;
		unsigned descr_2;
	} sanity_vals[] = {
		{
			HEA_MAX_SND_WQE_2_IMMDATA_SZ0,
			HEA_MAX_SND_WQE_1_DESC_SZ0,
			HEA_MAX_SND_WQE_2_DESC_SZ0}, {
		HEA_MAX_SND_WQE_2_IMMDATA_SZ1,
			HEA_MAX_SND_WQE_1_DESC_SZ1,
			HEA_MAX_SND_WQE_2_DESC_SZ1}, {
		HEA_MAX_SND_WQE_2_IMMDATA_SZ2,
			HEA_MAX_SND_WQE_1_DESC_SZ2,
			HEA_MAX_SND_WQE_2_DESC_SZ2}, {
		HEA_MAX_SND_WQE_2_IMMDATA_SZ3,
			HEA_MAX_SND_WQE_1_DESC_SZ3,
			HEA_MAX_SND_WQE_2_DESC_SZ3}, {
		HEA_MAX_SND_WQE_2_IMMDATA_SZ4,
			HEA_MAX_SND_WQE_1_DESC_SZ4,
			HEA_MAX_SND_WQE_2_DESC_SZ4}, {
		HEA_MAX_SND_WQE_2_IMMDATA_SZ5,
			HEA_MAX_SND_WQE_1_DESC_SZ5,
			HEA_MAX_SND_WQE_2_DESC_SZ5},};

	switch (in->num_qpair) {
	case 1:
		in->qpn_shift = HEA_QPN_1;
		break;

	case 2:
		in->qpn_shift = HEA_QPN_2;
		break;

	case 4:
		in->qpn_shift = HEA_QPN_4;
		break;

	case 8:
		in->qpn_shift = HEA_QPN_8;
		break;

	case 16:
		in->qpn_shift = HEA_QPN_16;
		break;

	default:
		khea_error("Invalid value for num_qp for port %d/%d/%d, "
				"requested : %d, granted : %d",
				rhea_adapter, rhea_pport, rhea_lport,
				in->num_qpair, 8);
		in->num_qpair = 8;
		in->qpn_shift = HEA_QPN_8;

	}

	if (in->recv_q_num < 2) {
		in->recv_rq2_len = 0;
		in->recv_rq2_low = 0;
		in->recv_rq2_size = 0;
	}

	if (in->use_ll_rq1)
		in->recv_rq1_size = 128;

	if (in->recv_q_num < 1 || in->recv_q_num > 2) {
		khea_error("Invalid value for recv_qp_num for "
				"port %d/%d/%d",
				rhea_adapter, rhea_pport, rhea_lport);
		return -EINVAL;
	}

	if (in->recv_q_num > 1 && in->recv_rq1_size >= in->recv_rq2_size) {
		khea_error("Invalid value for recv_qp2_size for "
				"port %d/%d/%d",
				rhea_adapter, rhea_pport, rhea_lport);
		return -EINVAL;
	}

	if (in->send_wqe_type > 5) {
		khea_error("Invalid value for send_wqe_type");
		return -EINVAL;
	}

	if (in->send_ll_limit > sanity_vals[in->send_wqe_type].limit_2) {
		khea_error("Invalid value for send_ll_limit for "
				"port %d/%d/%d",
				rhea_adapter, rhea_pport, rhea_lport);
		return -EINVAL;
	}

	if (in->recv_rq1_len < in->recv_rq1_low) {
		khea_error("Invalid value for recv_qp1_low "
				"for port %d/%d/%d",
				rhea_adapter, rhea_pport, rhea_lport);
		return -EINVAL;
	}

	if (in->recv_rq2_len < in->recv_rq2_low) {
		khea_error("Invalid value for recv_qp2_low for "
				"port %d/%d/%d",
				rhea_adapter, rhea_pport, rhea_lport);
		return -EINVAL;
	}

	in->send_max_desc_1 = sanity_vals[in->send_wqe_type].descr_1;
	in->send_max_desc_2 = sanity_vals[in->send_wqe_type].descr_2;
	in->send_cq_len = in->send_q_len;
	in->recv_cq_len = in->recv_rq1_len + in->recv_rq2_len;

	return 0;
}


static int init_etherdev(struct etherdev_id *edev,
				struct hea_channel_context *context_channel,
				char *hea_name)
{
	struct khea_private *kp;
	struct net_device *dev;
	struct module_inputs in;
	union hea_mac_addr mac_address;

	unsigned hea_id;
	unsigned channel_id;

	int err, index, rc, q;
	int rhea_adapter, rhea_pport, rhea_lport;
	khea_debug("with %ld", khea_enable);

	rhea_adapter = edev->rhea_adapter;
	rhea_pport = edev->rhea_pport;
	rhea_lport = edev->rhea_lport;
	index = (rhea_adapter * 16) + (rhea_pport * 4) + rhea_lport;

	if (khea_enable & (((long)1) << index)) {
		khea_debug("rhea_adapter %d rhea_pport %d "
				"rhea_lport %d is enabled",
				rhea_adapter, rhea_pport, rhea_lport);

		err = rhea_session_init(&hea_id, rhea_adapter);
		if (err != 0) {
			khea_error("cannot init adapter %d", rhea_adapter);
			return -EPERM;
		}

		if (rhea_pport >= rhea_pport_count(hea_id)) {
			rhea_session_fini(hea_id);
			return -EPERM;
		}

		context_channel->cfg.pport_nr = rhea_pport;
		context_channel->cfg.type = rhea_lport + HEA_LPORT_0;

		err = rhea_channel_alloc(hea_id, &channel_id,
						context_channel);
		if (err != 0) {
			khea_error("cannot open channel %d,%d,%d",
					rhea_adapter, rhea_pport, rhea_lport);
			rhea_session_fini(hea_id);
			return -EPERM;
		}

		/* get device MAC address */
		err = rhea_channel_macaddr_get(hea_id, channel_id,
							&mac_address);
		if (err != 0) {
			khea_error("cannot get port MAC address");
			rhea_channel_free(hea_id, channel_id);
			rhea_session_fini(hea_id);
			return -EPERM;
		}

		rhea_channel_free(hea_id, channel_id);
		rhea_session_fini(hea_id);

		khea_debug("trying %d/%d/%d",
				rhea_adapter, rhea_pport, rhea_lport);

		module_params_parse(&in, rhea_adapter, rhea_pport, rhea_lport);
		rc = module_params_validate(&in, rhea_adapter, rhea_pport,
					rhea_lport);
		if (rc < 0)
			return -EINVAL;

		/* create and setup netdevice */
		dev = alloc_etherdev_mq(sizeof(struct khea_private),
					KHEA_MAX_QP);

		if (!dev) {
			khea_error("alloc etherdevice failed");
			return -EPERM;
		}

		/* set device function pointers */
		dev->netdev_ops = &khea_ops;
		SET_ETHTOOL_OPS(dev, &khea_eops);

		/* set device features */
		dev->features =
			NETIF_F_HIGHDMA | NETIF_F_SG |
			NETIF_F_FRAGLIST | NETIF_F_IP_CSUM;
		if (in.do_gso)
			dev->features |= NETIF_F_TSO;

		dev->features |=
			NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX |
			NETIF_F_HW_VLAN_FILTER;

		dev->vlan_features =
			NETIF_F_HIGHDMA | NETIF_F_SG |
			NETIF_F_FRAGLIST | NETIF_F_IP_CSUM;

		if (in.do_gso)
			dev->vlan_features |= NETIF_F_TSO;

		dev->tx_queue_len = in.send_q_len;

		/* set device MAC address */
		err = dev->addr_len;
		if (err > sizeof(long long))
			err = sizeof(long long);

		memcpy(dev->dev_addr, &mac_address.sa.addr,
				sizeof(mac_address.sa.addr));

		/* init our private stuff */
		kp = netdev_priv(dev);
		memset(kp, 0, sizeof(*kp));

		kp->num_qp = in.num_qpair;
		kp->hea_qpn_shift = in.qpn_shift;
		/* alloc queue pair array */
		kp->qp_vec = kzalloc(kp->num_qp * sizeof(struct khea_qp_vec),
					GFP_KERNEL);
		if (kp->qp_vec == NULL) {
			khea_error("cannot allocate queue pair array\n");
			return -ENOMEM;
		}

		spin_lock_init(&kp->vlanlock);

		kp->ndev = dev;
		/* 22 = header(14) + crc(4) + vlan_tag(4) */
		kp->mtu = dev->mtu + 22;
		kp->rhea_adapter = rhea_adapter;
		kp->rhea_pport = rhea_pport;
		kp->rhea_lport = rhea_lport;

		/* save module config parameters */
		kp->frag_alloc_order = in.frag_alloc_order;
		kp->recv_q_num = in.recv_q_num;
		kp->send_ll_limit = in.send_ll_limit;

		kp->send_max_desc_1 = in.send_max_desc_1;
		kp->send_max_desc_2 = in.send_max_desc_2;
		kp->do_gso = in.do_gso;

		for (q = 0; q < kp->num_qp; ++q) {
			kp->qp_vec[q].parent = kp;

			kp->qp_vec[q].send_cq_len = in.send_cq_len;
			kp->qp_vec[q].recv_cq_len = in.recv_cq_len;
			kp->qp_vec[q].send_q_len = in.send_q_len;
			kp->qp_vec[q].send_q_reclaim = in.send_q_reclaim;

			kp->qp_vec[q].recv_rq_len[0] = in.recv_rq1_len;
			kp->qp_vec[q].recv_rq_len[1] = in.recv_rq2_len;

			kp->qp_vec[q].recv_rq_low[0] = in.recv_rq1_low;
			kp->qp_vec[q].recv_rq_low[1] = in.recv_rq2_low;

			kp->qp_vec[q].recv_rq_size[0] = in.recv_rq1_size;
			kp->qp_vec[q].recv_rq_size[1] = in.recv_rq2_size;

			kp->qp_vec[q].send_wqe_type = in.send_wqe_type;
			kp->qp_vec[q].use_ll_rq1 = in.use_ll_rq1;
			kp->qp_vec[q].max_num_lro = in.max_num_lro;
		}

		/* register the device with NAPI */
		for (q = 0; q < kp->num_qp; ++q)
			netif_napi_add(dev, &kp->qp_vec[q].napi,
					khea_poll, in.napi_w);

		netif_set_real_num_tx_queues(dev, kp->num_qp);
		netif_set_real_num_rx_queues(dev, kp->num_qp);

		/* register the device */
		strncpy(dev->name, hea_name, sizeof(dev->name) - 1);
		err = register_netdev(dev);
		if (err) {
			khea_error("registering netdevice failed");
			free_netdev(dev);

			goto cleanup;
		}

		netif_carrier_off(dev);
		{
			int has_vlan = 1;

			khea_info("Registered device %s: NAPI weight %d; "
					"sendq %d/%d/%d/%d",
					dev->name, in.napi_w, in.send_wqe_type,
					in.send_q_len, in.send_q_reclaim,
					in.send_ll_limit);

			khea_info("     recvq %d %d/%d/%d %d/%d/%d %s%s%s",
					in.recv_q_num,
				in.recv_rq1_len, in.recv_rq1_low,
				in.recv_rq1_size,
				in.recv_rq2_len, in.recv_rq2_low,
				in.recv_rq2_size,
				in.max_num_lro > 0 ? "LRO " : "",
				in.do_gso ? "GSO " : "",
				has_vlan ? "VLAN" : "");
		}
		all_kp[index] = kp;
	}

	return 0;


cleanup:
	kfree(kp->qp_vec);

	return -EPERM;
}


static int __init init(void)
{
	struct etherdev_id edev;
	struct hea_channel_context context_channel;

	int rc;
	char hea_name[16], *t;
	int created;

	strncpy(hea_name, basename, sizeof(basename));
	hea_name[sizeof(hea_name) - 1] = 0;
	t = strchr(hea_name, '%');
	if (t != NULL)
		*t = 0;
	strncat(hea_name, "%d", sizeof(hea_name - 1));

	/* Init all enabled etherdevs */
	memset(&context_channel, 0, sizeof(context_channel));

	created = 0;
	for (edev.rhea_adapter = 0; edev.rhea_adapter < 4;
					edev.rhea_adapter++) {
		for (edev.rhea_pport = 0; edev.rhea_pport < 4;
						edev.rhea_pport++) {
			for (edev.rhea_lport = 0; edev.rhea_lport < 4;
							edev.rhea_lport++) {

				rc = init_etherdev(&edev, &context_channel,
						hea_name);

				if (rc == 0)
					++created;
			}
		}
	}

	khea_debug("done");
	return created ? 0 : -EINVAL;
}


static void __exit fini(void)
{
	int ni;

	khea_debug("start");

	for (ni = 0; ni < KHEA_MAX_DEVICES; ni++) {
		if (all_kp[ni]) {
			khea_fini_interface(all_kp[ni]);
			unregister_netdev(all_kp[ni]->ndev);

			if (all_kp[ni]->params_obj) {
				all_kp[ni]->params_obj->kp = NULL;
				kobject_put(&(all_kp[ni]->params_obj->kobj));
			}

			free_netdev(all_kp[ni]->ndev);
			kfree(all_kp[ni]->qp_vec);
			all_kp[ni] = NULL;
		}
	}

	khea_debug("done");
}


module_init(init);
module_exit(fini);
