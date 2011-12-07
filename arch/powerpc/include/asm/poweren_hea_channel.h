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

#ifndef _ASM_POWEREN_HEA_CHANNEL_H_
#define _ASM_POWEREN_HEA_CHANNEL_H_

#include <linux/types.h>
#include <linux/if_ether.h>

#define HEA_MAX_ADAPTERS		4
#define HEA_MAX_PPORT_COUNT		4
#define HEA_MAX_PPORT_LPORT_COUNT	4
#define HEA_MAX_PPORT_CHANNEL_COUNT	8
#define HEA_MAX_FRAME_SIZE		9022

enum hea_channel_feature_get {
	/* general channel features */
	HEA_CHANNEL_GET_LINK_STATE,
	HEA_CHANNEL_GET_MAC_ADDRESS,
	HEA_CHANNEL_GET_MAX_FRAME_SIZE,

	/*** The following channel options affect QP Determination ***/
	HEA_CHANNEL_GET_BASE_SLOT,	/* base slot in the QPN Array */
	HEA_CHANNEL_GET_NUM_SLOTS,	/* number of slots in
					 * the QPN Array */
	HEA_CHANNEL_GET_NUM_TCAM_HASH_BITS,	/* The number
						 * of hash or
						 * TCAM bits
						 * used for QP
						 * det. */
	/****** VLAN features *****/
	HEA_CHANNEL_GET_VLAN_EXTRACT,
	HEA_CHANNEL_GET_DISCARD_UNTAGGED,
	HEA_CHANNEL_GET_TAG_FILTER_MODE,
	HEA_CHANNEL_GET_VLAN_FILTER,
};

enum hea_channel_feature_set {
	/* general channel features */
	HEA_CHANNEL_SET_MAC_ADDRESS,
	HEA_CHANNEL_SET_MAX_FRAME_SIZE,
	HEA_CHANNEL_RESET_UAELOG,

	/****** VLAN features *****/
	HEA_CHANNEL_SET_VLAN_EXTRACT,
	HEA_CHANNEL_SET_DISCARD_UNTAGGED,
	HEA_CHANNEL_SET_TAG_FILTER_MODE,
	HEA_CHANNEL_SET_VLAN_FILTER,
	HEA_CHANNEL_CLEAR_VLAN_FILTER,
	HEA_CHANNEL_CLEAR_ALL_VLAN_FILTERS,
};

/*
 * Specifies channel types which are available for
 * each physical port
 */
enum hea_channel_type {
	HEA_UC_PORT, HEA_MC_PORT, HEA_BC_PORT, HEA_LPORT,
	HEA_LPORT_0, HEA_LPORT_1, HEA_LPORT_2, HEA_LPORT_3
};

enum hea_qpn_slot_number {
	HEA_QPN_1, HEA_QPN_2, HEA_QPN_4, HEA_QPN_8, HEA_QPN_16, HEA_QPN_32
};

/*
 * Structure which lists all supported
 * speeds
 */
enum hea_speed {
	HEA_SPEED_NONE		= 0,
	HEA_SPEED_AUTONEG	= 1,
	HEA_SPEED_10M		= 10,
	HEA_SPEED_100M		= 100,
	HEA_SPEED_1G		= 1000,
	HEA_SPEED_10G		= 10000,
};

/*
 * Structures which represents the physical
 * connection type.
 */
enum hea_phy_connection_type {
	HEA_PHY_SGMII,
	HEA_PHY_XAUI,
};

/*
 * Union which is capable of storing a MAC address
 */
union hea_mac_addr {
	__u64 _be64;
	struct {
		__u16 __unused;
		__u8 addr[ETH_ALEN];
	} sa;
};


/**
 * This struct contains the configuration parameters for the qpn
 *
 * slot_count	Number of slots which are supposed to be allocated
 */
struct hea_qpn_cfg {
	enum hea_qpn_slot_number slot_count;
};

/*
 * slot_count		Number of TCAM slots
 */
struct hea_tcam_cfg {
	__u32 slot_count;
};

/*
 * tcam_offset		Offset in allocated TCAMs
 * tcam_pattern		TCAM Pattern
 * tcam_mask		TCAM Mask
 * qpn_offset		Indicates which allocated QPN slot to use for
 *			determiniation
 */
struct hea_tcam_setting {
	__u32 tcam_offset;
	__u32 tcam_pattern;
	__u32 tcam_mask;
	__u32 qpn_offset;
};

struct hea_hasher_setting {
	__u64 mask0;	/* Configure bits 0-63 of hash mask. */
	__u64 mask1;	/* Configure bits 64-123 of
					 * hash mask. */
	__u8 sc_0_8;	/* 1 enable, 0 disable symmetry
				 * control on bytes 0 and 8 */
	__u8 sc_1_9;
	__u8 sc_2_10;
	__u8 sc_3_11;
	__u8 sc_4_12;
	__u8 sc_5_13;
	__u8 sc_6_14;
	__u8 sc_7_15;
};

enum hea_vlan_tagged_filtering_mode {
	HEA_VLAN_PERMIT_ALL = 0x0,
	HEA_VLAN_DISCARD_ALL = 0x1,
	HEA_VLAN_SELECTIVELY_FILTER = 0x2,
};

struct hea_vlan_tag_s {
	__u16 priority:3;
	__u16 cfi:1;
	__u16 vid:12;
};

union hea_vlan_tag_u {
	struct hea_vlan_tag_s a;
	__u16 b;
};

struct hea_channel_vlan_cfg {
	__u32 vlan_extract;
	__u8 discard_untagged;
	enum hea_vlan_tagged_filtering_mode tag_filtering_mode;
	union hea_vlan_tag_u default_tag;
};

/**
 * This struct saves logical port configuration information
 *
 * mac_address	MAC address of logical port. If it is not specified the
 *		default HEA MAC addresses are chosen
 * lport_nr	This field can be ignored and it is only used internally
 */
struct hea_channel_lport {
	union hea_mac_addr mac_address;
	__u32 lport_nr;
};

enum hea_default_channel_usuage {
		HEA_DEFAULT_CHANNEL_ALONE,
		HEA_DEFAULT_CHANNEL_MANAGER,
		HEA_DEFAULT_CHANNEL_SHARE,
};

struct hea_channel_uc_mc_bc {
	__u32 lport_channel_id;
	enum hea_default_channel_usuage channel_usuage;
	__u64 test;
};

/*
 * Callback in case a the link state of the physical port has changed
 *
 * @param	pport_nr	Number of physical port
 * @param	link_state	Shows whether the the port is turned on(1) or off(0)
 *
 */
typedef int (*hea_pport_link_state_callback_t) (__u32 port_nr, __u32 link_state,
						void *args);

/**
 * Struct which contains information about the function which should be called
 * in case of a pport link state change
 *
 * @param	args		Argument which is passed to the callback
 * @param	fkt_ptr		callback function pointer
 */
struct hea_eq0_pport_state_change {
	void *args;
	hea_pport_link_state_callback_t fkt_ptr;
};

/*
 * Structure representing the configuration features
 * for channel.
 *
 * @param type		Specifies the channel type (logical port, broadcast channel, ...)
 * @param pport_nr	Specifies the physical port (0-3)
 * @param max_frame_size	Specifies the maximum framesize for that channel
 * @param lport		Structure which saves lport specific configuration data
 * @param mc		Structure which saves multicast specific configuration data
 * @param uc		Structure which saves uniicast specific configuration data
 * @param bc		Structure which saves broadcast specific configuration data
 */
struct hea_channel_cfg {
	struct hea_channel_vlan_cfg vlan;
	enum hea_channel_type type;
	__u32 pport_nr;
	__u32 max_frame_size;
	union {
		struct hea_channel_lport lport;
		struct hea_channel_uc_mc_bc dc;
		struct hea_channel_uc_mc_bc mc;
		struct hea_channel_uc_mc_bc bc;
		struct hea_channel_uc_mc_bc uc;
	};
};

/*
 * Structure containing physical port configuration features
 */
struct hea_pport_cfg {
	struct hea_adapter *ap;
	unsigned int mac_lport_count_max;
	union hea_mac_addr mac_address;
	enum hea_speed speed_hw;
	enum hea_speed speed_dt;
	int ext_phy_addr;
	int int_pcs_addr;
	__u32 pport_nr;
	enum hea_phy_connection_type conn_type;
};

#ifdef __KERNEL__

static inline int is_hea_pport(__u32 pport_nr)
{
	return HEA_MAX_PPORT_COUNT > pport_nr ? 1 : 0;
}

static inline int is_hea_lport(enum hea_channel_type type)
{
	return (HEA_LPORT <= type && HEA_LPORT_3 >= type);
}

static inline int hea_lport_index_get(enum hea_channel_type type)
{
	if (is_hea_lport(type))
		return type - HEA_LPORT_0;

	return -ENODEV;
}

#endif /*  __KERNEL__ */

#endif /* _ASM_POWEREN_HEA_CHANNEL_H_ */

