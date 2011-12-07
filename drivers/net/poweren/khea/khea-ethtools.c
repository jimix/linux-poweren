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
 * khea-ethtools.c --  HEA kernel network interface
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
#include <khea-ethtools.h>

int khea_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	khea_debug(" ");
	cmd->autoneg = AUTONEG_DISABLE;
	cmd->speed = SPEED_10000;	/* SPEED_1000 */
	cmd->duplex = DUPLEX_FULL;
	cmd->port = PORT_MII;
	cmd->supported = SUPPORTED_MII;
	cmd->advertising = ADVERTISED_MII;
	cmd->transceiver = XCVR_INTERNAL;
	return 0;
}

void khea_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	khea_debug(" ");
	strlcpy(info->driver, "kHEA", sizeof(info->driver));
	strlcpy(info->version, KHEA_VERSION_STRING, sizeof(info->version));
	strlcpy(info->fw_version, "0.0", sizeof(info->fw_version));
	info->bus_info[0] = 0;
}

void khea_get_pauseparam(struct net_device *dev, struct ethtool_pauseparam *pp)
{
	khea_debug(" ");
}

int khea_set_pauseparam(struct net_device *dev, struct ethtool_pauseparam *pp)
{
	khea_debug(" ");
	return 0;
}

static const char khea_main_stats_strings[][ETH_GSTRING_LEN] = {
	"rx_packets", "tx_packets", "rx_bytes", "tx_bytes", "rx_errors",
	"tx_errors", "rx_dropped", "tx_dropped", "multicast", "collisions",
	"rx_length_errors", "rx_over_errors", "rx_crc_errors",
	"rx_frame_errors", "rx_fifo_errors", "rx_missed_errors",
	"tx_aborted_errors", "tx_carrier_errors", "tx_fifo_errors",
	"tx_heartbeat_errors", "tx_window_errors",
	/* device-specific stats */
	"link up"
};

static const char khea_qp_stats_strings[][ETH_GSTRING_LEN] = {
	"------ QP ------",
	"SQ packets", "RQ1 packets", "RQ2 packets",
	"LRO aggregated", "LRO flushed", "LRO avr aggr", "LRO no_desc"
};

#define KHEA_MAIN_STATS_LEN  ARRAY_SIZE(khea_main_stats_strings)
#define KHEA_QP_STATS_LEN    ARRAY_SIZE(khea_qp_stats_strings)

int khea_get_sset_count(struct net_device *dev, int sset)
{
	struct khea_private *kp = netdev_priv(dev);
	khea_debug(" ");

	switch (sset) {
	case ETH_SS_STATS:
		return KHEA_MAIN_STATS_LEN +
			kp->num_qp * KHEA_QP_STATS_LEN;
	}
	return -EOPNOTSUPP;
}

void khea_get_strings(struct net_device *dev, u32 stringset, u8 * data)
{
	struct khea_private *kp = netdev_priv(dev);
	int q;

	khea_debug(" ");
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, *khea_main_stats_strings,
		       sizeof(khea_main_stats_strings));
		data += sizeof(khea_main_stats_strings);

		for (q = 0; q < kp->num_qp; ++q) {
			memcpy(data, *khea_qp_stats_strings,
				sizeof(khea_qp_stats_strings));
			data += sizeof(khea_qp_stats_strings);
		}
		break;
	}
}

void khea_get_ethtool_stats(struct net_device *dev,
			    struct ethtool_stats *stats, u64 * data)
{
	struct khea_private *kp = netdev_priv(dev);
	struct net_device_stats *st;
	int i, q;

	khea_debug(" ");

	i = 0;
	st = khea_get_stats(dev);

	/* global stats */
	data[i++] = st->rx_packets;
	data[i++] = st->tx_packets;
	data[i++] = st->rx_bytes;
	data[i++] = st->tx_bytes;
	data[i++] = st->rx_errors;
	data[i++] = st->tx_errors;
	data[i++] = st->rx_dropped;
	data[i++] = st->tx_dropped;
	data[i++] = st->multicast;
	data[i++] = st->collisions;
	data[i++] = st->rx_length_errors;
	data[i++] = st->rx_over_errors;
	data[i++] = st->rx_crc_errors;
	data[i++] = st->rx_frame_errors;
	data[i++] = st->rx_fifo_errors;
	data[i++] = st->rx_missed_errors;
	data[i++] = st->tx_aborted_errors;
	data[i++] = st->tx_carrier_errors;
	data[i++] = st->tx_fifo_errors;
	data[i++] = st->tx_heartbeat_errors;
	data[i++] = st->tx_window_errors;
	/* device-specific stats */
	data[i++] = kp->interface_up;/* link up */

	/* per QP stats */
	for (q = 0; q < kp->num_qp; ++q) {
		data[i++] = q;
		data[i++] = kp->qp_vec[q].stats_sq_packets;
		data[i++] = kp->qp_vec[q].stats_rqx_packets[0];
		data[i++] = kp->qp_vec[q].stats_rqx_packets[1];
		data[i++] = kp->qp_vec[q].lro_mgr.stats.aggregated;
		data[i++] = kp->qp_vec[q].lro_mgr.stats.flushed;
		data[i++] = kp->qp_vec[q].lro_mgr.stats.aggregated /
				kp->qp_vec[q].lro_mgr.stats.flushed;
		data[i++] = kp->qp_vec[q].lro_mgr.stats.no_desc;
	}
}
