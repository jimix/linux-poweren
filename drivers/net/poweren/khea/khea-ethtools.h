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


#ifndef _KHEA_ETHTOOL_H
#define _KHEA_ETHTOOL_H

extern int khea_get_settings(struct net_device *, struct ethtool_cmd *);
extern void khea_get_drvinfo(struct net_device *, struct ethtool_drvinfo *);
extern void khea_get_pauseparam(struct net_device *,
				struct ethtool_pauseparam *);
extern int khea_set_pauseparam(struct net_device *,
			       struct ethtool_pauseparam *);
extern void khea_get_strings(struct net_device *, u32 stringset, u8 *);
extern void khea_get_ethtool_stats(struct net_device *, struct ethtool_stats *,
				   u64 *);
extern int khea_get_sset_count(struct net_device *, int sset);

#endif /* _KHEA_ETHTOOL_H */
