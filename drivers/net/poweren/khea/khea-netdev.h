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


#ifndef _KHEA_NETDEV_H
#define _KHEA_NETDEV_H

extern int khea_init(struct net_device *dev);
extern void khea_uninit(struct net_device *dev);
extern int khea_open(struct net_device *dev);
extern int khea_stop(struct net_device *dev);
extern netdev_tx_t khea_start_xmit(struct sk_buff *skb,
				   struct net_device *dev);
extern u16 khea_select_queue(struct net_device *dev, struct sk_buff *skb);
extern void khea_change_rx_flags(struct net_device *dev, int flags);
extern void khea_set_rx_mode(struct net_device *dev);
extern void khea_set_multicast_list(struct net_device *dev);
extern int khea_set_mac_address(struct net_device *dev, void *addr);
extern int khea_validate_addr(struct net_device *dev);
extern int khea_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
extern int khea_set_config(struct net_device *dev, struct ifmap *map);
extern int khea_change_mtu(struct net_device *dev, int new_mtu);
extern void khea_tx_timeout(struct net_device *dev);
extern struct net_device_stats *khea_get_stats(struct net_device *dev);
extern void khea_vlan_rx_register(struct net_device *dev,
				  struct vlan_group *grp);
extern void khea_vlan_rx_add_vid(struct net_device *dev, unsigned short vid);
extern void khea_vlan_rx_kill_vid(struct net_device *dev, unsigned short vid);
#ifdef CONFIG_NET_POLL_CONTROLLER
extern void khea_poll_controller(struct net_device *dev);
#endif
struct net_device_stats *khea_get_stats(struct net_device *dev);

extern int khea_poll(struct napi_struct *napi, int budget);

#endif /* _KHEA_NETDEV_H */
