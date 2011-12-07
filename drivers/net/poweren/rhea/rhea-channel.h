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
#ifndef _RHEA_CHANNEL_H_
#define _RHEA_CHANNEL_H_

#include <asm/poweren_hea_common_types.h>
#include <asm/poweren_hea_channel.h>

#include <rhea-channel-resource.h>
#include <rhea-funcs.h>

struct rhea_pport_bpfc {
	/* 0x00000 */
	u64 p_rc;
	PAD(0x00008, 0x00200);
	u64 pl_mac[4];	/* 0x00200 */

	PAD(0x00220, 0x00400);
	u64 p_rxo;	/* 0x00400 */
	u64 p_rxbcp;	/* 0x00408 */
	u64 p_rxmcp;	/* 0x00410 */

	PAD(0x00418, 0x00608);
	u64 pg_trcpfc;	/* 0x00608 */

	PAD(0x00610, 0x00800);
	u64 pl_rc[4];	/* 0x00800 */

	PAD(0x00820, 0x00a00);
	u64 p_rcb;	/* 0x00a00 */
	u64 p_rcm;	/* 0x00a08 */
	u64 p_rcu;	/* 0x00a10 */

	PAD(0x00a18, 0x00c00);
	u64 pl_rxo[4];	/* 0x00c00 */

	PAD(0x00c20, 0x00e00);
	u64 p_rx64;	/* 0x00e00 */
	u64 p_rx65;	/* 0x00e08 */
	u64 p_rx128;	/* 0x00e10 */
	u64 p_rx256;	/* 0x00e18 */
	u64 p_rx512;	/* 0x00e20 */
	u64 p_rx1024;	/* 0x00e28 */
	u64 p_rxftl;	/* 0x00e30 */

	PAD(0x00e38, 0x01000);
	u64 pl_rxftl[4];	/* 0x01000 */

	PAD(0x01020, 0x01200);
	u64 pl_rxerr[4];	/* 0x01200 */

	PAD(0x01220, 0x01400);
	u64 pl_rxfd[4];	/* 0x01400 */

	PAD(0x01420, 0x01600);
	u64 p_rxse;	/* 0x01600 */
	u64 p_rxce;	/* 0x01608 */
	u64 p_rxjab;	/* 0x01610 */
	u64 p_rxfrag;	/* 0x01618 */
	u64 p_rxbfcs;	/* 0x01620 */
	u64 p_rxrle;	/* 0x01628 */
	u64 p_rxorle;	/* 0x01630 */
	u64 p_rxrf;	/* 0x01638 */

	PAD(0x01640, 0x01800);
	u64 p_rxoerr;	/* 0x01800 */
	u64 p_rxuoc;	/* 0x01808 */
	u64 p_rxcpf;	/* 0x01810 */
	u64 p_rxime;	/* 0x01818 */
	u64 p_rxfd;	/* 0x01820 */
	u64 p_rxaln;	/* 0x01828 */

	PAD(0x01830, 0x04000);
	u64 pl_qosm[4];	/* 0x04000 */

	PAD(0x04020, 0x04200);
	u64 p_qosm;	/* 0x04200 */

	PAD(0x04208, 0x04400);
	u64 pg_qpn[32];	/* 0x04400 */

	PAD(0x04500, 0x04800);
	u64 pg_hashm[2];	/* 0x04800 */
	u64 pg_hashsc;	/* 0x04810 */

	PAD(0x04818, 0x04a00);
	u64 pg_tcampr[16];	/* 0x04a00 */

	PAD(0x04a80, 0x04c00);
	u64 pg_tcamm[16];	/* 0x04c00 */

	PAD(0x04c80, 0x04e00);
	u64 pl_mfsize[4];	/* 0x04e00 */

	PAD(0x04e20, 0x05000);
	u64 p_mfsizeb;	/* 0x05000 */
	u64 p_mfsizem;	/* 0x05008 */
	u64 p_mfsizeu;	/* 0x05010 */

	PAD(0x05018, 0x05200);
	u64 pg_rrange[8];	/* 0x05200 */
	PAD(0x05240, 0x05400);
	u64 pg_sabsel[4];	/* 0x05240 */
	PAD(0x05420, 0x0c000);
};

struct rhea_pport_mmc {
	u64 p_uaa;	/* 0x0c000 */
	u64 p_macvc;	/* 0x0c008 */
	u64 p_macc;	/* 0x0c010 */
	u64 p_pst;	/* 0x0c018 */
	u64 p_pc;		/* 0x0c020 */
	u64 p_js;		/* 0x0c028 */

	PAD(0x0c030, 0x0c208);
	u64 p_mma;	/* 0x0c208 */

	PAD(0x0c210, 0x0c600);
	u64 p_xpcsc;	/* 0x0c600 */

	PAD(0x0c608, 0x0c610);
	u64 p_xpcsp;	/* 0x0c610 */
	u64 p_xpcsst;	/* 0x0c618 */

	PAD(0x0c620, 0x0c800);
	u64 p_pcsid;	/* 0x0c800 */
	u64 p_spcsc;	/* 0x0c808 */
	u64 p_spcsst;	/* 0x0c810 */

	PAD(0x0c818, 0x0ca00);
	u64 pg_ec;	/* 0x0ca00 */
	u64 pg_est;	/* 0x0ca08 */
	u64 pg_tc;	/* 0x0ca10 */

	PAD(0x0ca18, 0x10000);
};

struct rhea_pport_txm {
	u64 pl_txo[4];	/* 0x10000 */

	PAD(0x10020, 0x10200);
	u64 p_tx64;	/* 0x10200 */
	u64 p_tx65;	/* 0x10208 */
	u64 p_tx128;	/* 0x10210 */
	u64 p_tx256;	/* 0x10218 */
	u64 p_tx512;	/* 0x10220 */
	u64 p_tx1024;	/* 0x10228 */

	PAD(0x10230, 0x10400);
	u64 pl_txucp[4];	/* 0x10400 */

	PAD(0x10420, 0x10600);
	u64 pl_txmcp[4];	/* 0x10600 */

	PAD(0x10620, 0x10800);
	u64 pl_txbcp[4];	/* 0x10800 */

	PAD(0x10820, 0x10a00);
	u64 p_txbfcs;	/* 0x10a00 */
	u64 p_txlf;	/* 0x10a08 */
	u64 p_txrf;	/* 0x10a10 */
	u64 p_txime;	/* 0x10a18 */
	u64 p_txcpf;	/* 0x10a20 */

	PAD(0x10a28, 0x10c00);
	u64 pl_txfd[4];	/* 0x10c00 */

	PAD(0x10c20, 0x10e00);
	u64 p_txo;	/* 0x10e00 */
	u64 p_txbcp;	/* 0x10e08 */
	u64 p_txmcp;	/* 0x10e10 */

	PAD(0x10e18, 0x14000);
};

struct rhea_pport_em {
	u64 pl_rxucp[4];	/* 0x14000 */

	PAD(0x14020, 0x14200);
	u64 pl_rxmcp[4];	/* 0x14200 */

	PAD(0x14220, 0x14400);
	u64 pl_rxbcp[4];	/* 0x14400 */

	PAD(0x14420, 0x14608);
	u64 pg_uaelog;	/* 0x14608 */
	u64 pg_uaelogm;	/* 0x14610 */

	PAD(0x14618, 0x14800);
	u64 p_trcxcs;	/* 0x14800 */
	u64 p_trcxbb;	/* 0x14808 */

	PAD(0x14810, 0x14a00);
	u64 pg_trc;	/* 0x14a00 */

	PAD(0x14a08, 0x14a20);
	u64 pg_hwem;	/* 0x14a20 */

	PAD(0x14a28, 0x14c00);
	u64 pl_rxwdd[4];	/* 0x14c00 */

	PAD(0x14c20, 0x18000);
};

struct rhea_pport_rbb {
	PAD(0x18000, 0x18008);
	u64 p_rxbor;	/* 0x18008 */

	PAD(0x18010, 0x18200);
	u64 pg_pthlb;	/* 0x18200 */
	u64 p_pthrb;	/* 0x18208 */
	u64 p_pqu;	/* 0x18210 */
	u64 p_pqd;	/* 0x18218 */
	u64 p_wsth;	/* 0x18220 */
	u64 p_prt;	/* 0x18228 */
	u64 p_lbc;	/* 0x18230 */

	PAD(0x18238, 0x18400);
	u64 pg_trcrbb;	/* 0x18400 */
	PAD(0x18408, 0x20000);
};

struct rhea_pport_pfc {
	u64 pl_vlanf[4][64];	/* 0x20000 */

	PAD(0x20800, 0x2b000);
	u64 p_mhash[64];	/* 0x2b000 */

	PAD(0x2b200, 0x2c000);
	u64 p_qosa[4];	/* 0x2c000 */

	PAD(0x2c020, 0x30000);
	u64 pl_qosa[4][4];	/* 0x30000 */

	PAD(0x30080, 0x38000);
	u64 p_rulem[640];	/* 0x38000 */
	PAD(0x39400, 0x40000);
};

struct rhea_pport_regs {
	struct rhea_pport_bpfc bpfc;
	struct rhea_pport_mmc mmc;
	struct rhea_pport_txm txm;
	struct rhea_pport_em em;
	struct rhea_pport_rbb rbb;
	struct rhea_pport_pfc pfc;
};

enum rhea_channel_state {
	RHEA_CHANNEL_NO_STATE,
	RHEA_CHANNEL_INIT,
	RHEA_CHANNEL_ENABLED,
	RHEA_CHANNEL_DISABLED,
};

#define RHEA_TCAM_ARRAY 16
struct rhea_tcam {
	u8 tcam_array[RHEA_TCAM_ARRAY];
	unsigned base;
	unsigned alloced;
	unsigned alloced_max;
};

struct rhea_hasher {
	u64 id;
	u64 mask0;
	u64 mask1;
	u64 sc;
};

#define HEA_LPORT_VLAN_FILTER_COUNT 64
struct rhea_vlan {
	__u64 filter[HEA_MAX_PPORT_LPORT_COUNT][HEA_LPORT_VLAN_FILTER_COUNT];
};

#define RHEA_QPN_ARRAY_SIZE 32

/* planing on only using that id to put unused channels in there! */
#define RHEA_LAST_QUEUE_ID 127

struct rhea_channel {
	union hea_mac_addr mac_address;
	enum hea_channel_type type;
	unsigned pport_nr;
	unsigned id;
	unsigned qpn_base;
	unsigned hasher_used;
	struct rhea_channel_resource_map *qpn;
	struct rhea_channel_resource_map *tcam;
	struct rhea_vlan vlan;
	enum rhea_channel_state state;
	struct hea_channel_cfg channel_cfg;
	struct rhea_channel *channel_lport;
};


#define MAX_HEA_TYPE_COUNT (1 + HEA_MAX_PPORT_LPORT_COUNT)
#define MAX_HEA_DEFAULT_TYPE_COUNT 4
#define MAX_PPORT_CHANNEL_COUNT (MAX_HEA_TYPE_COUNT *		\
				 MAX_HEA_DEFAULT_TYPE_COUNT +	\
				 HEA_MAX_PPORT_LPORT_COUNT)

struct rhea_pport {
	struct rhea_pport_regs *pport_regs;
	struct hea_pport_cfg port_cfg;
	struct rhea_channel *channel[HEA_MAX_PPORT_CHANNEL_COUNT];
	struct rhea_hasher *hasher;
	struct rhea_channel_resource qpn;
	struct rhea_channel_resource tcam;
	spinlock_t lock;
	enum rhea_channel_state state;
	unsigned int link_state_up;
	unsigned int mac_loopback;
};

/************************* CHANNEL *************************/

extern int rhea_pport_init(struct rhea_gen *rhea_gen,
			   struct hea_pport_cfg *pport);
extern int rhea_pport_fini(unsigned pport_nr);

extern struct rhea_channel *rhea_channel_create(struct hea_channel_cfg
						*channel_cfg);

extern int rhea_channel_destroy(struct rhea_channel *channel);

extern int rhea_channel_start(struct rhea_channel *channel);
extern int rhea_channel_stop(struct rhea_channel *channel);

/*********************** PPORT information ********************/

extern enum rhea_channel_state rhea_pport_state(unsigned int pport_nr);

extern int rhea_pport_link_state_get(unsigned pport_nr);
extern int rhea_pport_err_reset(unsigned int pport_nr);
extern int rhea_pport_avail(unsigned pport_nr);
extern int rhea_channel_avail(unsigned pport_nr, enum hea_channel_type);

extern void _rhea_port_register_reset(struct rhea_pport *pport);
extern int _rhea_pport_counters_clear(struct rhea_pport *pport);

extern int rhea_channel_info_get(struct rhea_channel *channel,
				    enum hea_channel_feature_get feature,
				    u64 *value);

extern int rhea_channel_info_set(struct rhea_channel *channel,
				    enum hea_channel_feature_set feature,
				    u64 value);

/************************* Dumping ****************************/

enum hea_channel_counter_type;
struct hea_channel_counters;
extern int channel_counters_get(struct rhea_channel *channel,
				enum hea_channel_counter_type counter_type,
				struct hea_channel_counters *counter);

extern void rhea_port_dump(struct rhea_channel *channel);
extern void rhea_pport_counters_dump(struct rhea_channel *channel);

/*************************** VLAN ***************************/

extern int rhea_channel_vlan_set(struct rhea_channel *channel,
				 struct hea_channel_vlan_cfg *vlan_cfg);

extern int rhea_channel_vlan_filter_set(struct rhea_channel *channel,
					u64 vlan_filter);

int rhea_channel_vlan_filter_clear(struct rhea_channel *channel,
				   u64 vlan_filter);

extern int rhea_channel_vlan_filter_get(struct rhea_channel *channel,
					u64 *vlan_filter);

int rhea_channel_all_vlan_filters_clear(struct rhea_channel *channel);

/*************************** QPN ***************************/

extern int _rhea_qpn_init(struct rhea_pport *pport);
extern int _rhea_qpn_fini(struct rhea_pport *pport);

extern int rhea_qpn_alloc(struct rhea_channel *channel,
			  struct hea_qpn_cfg *qpn_cfg);

extern int rhea_qpn_free(struct rhea_channel *channel);
extern int _rhea_qpn_free(struct rhea_channel *channel);

extern int rhea_qpn_share(struct rhea_channel *channel_target,
			  const struct rhea_channel *channel_source);

/*
 * Returns the size of the largest
 * free block in the QPN array.
 */
extern unsigned rhea_qpn_max(struct rhea_channel *channel);

extern int rhea_qpn_set(struct rhea_channel *channel, unsigned qp_id,
			unsigned qpn_offset);

/*************************** TCAM ***************************/

extern int _rhea_tcam_init(struct rhea_pport *pport);

extern int _rhea_tcam_fini(struct rhea_pport *pport);

extern int rhea_tcam_alloc(struct rhea_channel *channel,
			   struct hea_tcam_cfg *tcam_cfg, unsigned *slot_base);

extern int rhea_tcam_free(struct rhea_channel *channel, unsigned slot_base);
extern int _rhea_tcam_free(struct rhea_channel *channel, unsigned slot_base);

extern int _rhea_tcam_free_all(struct rhea_channel *channel);

extern int rhea_tcam_set(struct rhea_channel *channel,
			 unsigned tcam_base, unsigned tcam_offset,
			 unsigned qpn_offset,
			 unsigned pattern, unsigned mask);

extern int rhea_tcam_get(struct rhea_channel *channel,
			 unsigned tcam_base, unsigned tcam_offset,
			 unsigned *qpn_offset,
			 unsigned *pattern, unsigned *mask);

extern int rhea_tcam_register_set_status(struct rhea_channel *channel,
					 unsigned tcam_base,
					 unsigned tcam_offset,
					 unsigned enable);

/*************************** HASHER ***************************/

extern int _rhea_hasher_init(struct rhea_pport *pport);

extern int _rhea_hasher_fini(struct rhea_pport *pport);

extern struct rhea_hasher *rhea_hasher_alloc(struct rhea_channel *channel);

extern int rhea_hasher_free(struct rhea_channel *channel);
extern int _rhea_hasher_free(struct rhea_channel *channel);

extern int rhea_hasher_set(struct rhea_channel *channel, u64 sc,
			   u64 mask0, u64 mask1);

extern int rhea_hasher_get(struct rhea_channel *channel,
			   u64 *sc, u64 *mask0,
			   u64 *mask1);

#endif /* _RHEA_CHANNEL_H_ */
