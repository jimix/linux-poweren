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


#ifndef _ASM_POWEREN_HEA_CQ_H_
#define _ASM_POWEREN_HEA_CQ_H_

#include "poweren_hea_bitops.h"

#include <linux/types.h>

enum hea_interrupts {
	HEA_IRQ_NO,
	HEA_IRQ_COALESING_2,
};

/*
 * Specifies features for CQ
 * which can be set at runtime
 */
enum hea_cq_feature_get {
	HEA_CQ_TOKEN_GET,
	HEA_CQ_AER_GET,
	HEA_CQ_ENABLED_GET,
};

enum hea_cq_feature_set {
	HEA_CQ_AER_SET,
};

/* specifies irq type */

/*
 * This struct contains CQ configuration information
 *
 * cqe_count		Number of CQEs which should be placed in the CQ
 * cqe_auto_toggle	Changes the value of the valid bit, for each
 *			iteration of the CQ
 * hw_managed		Specifies whether the CQ is running in NN (1) or EP (0)
 * irq_type		Specifies whether the CQ supports IRQs
 */
struct hea_cq_cfg {
	enum hea_q_size cqe_count;
	__u32 cqe_auto_toggle;
	__u32 hw_managed;
	enum hea_interrupts irq_type;
	enum hea_cache_inject_type cache_inject;
	enum hea_target_cache target_cache;
};

/*
 * Structure which maps transmit and receive completion CQE
 */
struct hea_cqe {
	__u64 wr_id;
	/* 0x08 */
	volatile __u8 type;
	volatile __u8 valid;
	volatile __u16 status;
	__u8 flags;
	__u8 source;
	__u16 n_bytes_xfered;
	/* 0x10 */
	__u16 vlan_tag;
	__u16 inet_cksum;
	__u32 reserved0;
	/* 0x18 */
	__u16 page_offset0;
	__u16 wqe_count;
	__u32 qp_token;
	/* 0x20 */
	__u32 timestamp_seconds;
	__u32 timestamp_frac;
	/* 0x28 */
	union {
		__u64 pack_pointer;
		struct {
			__u8 l4_protocol;
			__u8 l2_5_offset;
			__u16 l3_offset;
			__u16 l4_offset;
			__u16 l5_offset;
		};
	};
	/* 0x30 */
	__u32 markers;
	__u32 hash;
	/* 0x38 */
	__u32 reserved1;
	__u16 reserved2;
	__u16 ticket;
};

/*
 * Structure which maps transmit and receive completion for CQE-in-WQE
 */
struct hea_ll_cqe {
	__u64 wr_id;
	/* 0x08 */
	volatile __u8 type;
	volatile __u8 valid;
	volatile __u16 status;
	__u16 reserved0;
	__u16 n_bytes_xfered;
	/* 0x10 */
	__u16 vlan_tag;
	__u16 inet_cksum;
	__u8 reserved1;
	__u8 header_len;
	__u16 reserved2;
	/* 0x18 */
	__u16 page_offset0;
	__u16 reserved3;
	__u32 qp_token;
	/* 0x20 */
	__u32 timestamp_seconds;
	__u32 timestamp_frac;
	/* 0x28 */
	union {
		__u64 pack_pointer;
		struct {
			__u8 l4_protocol;
			__u8 l2_5_offset;
			__u16 l3_offset;
			__u16 l4_offset;
			__u16 l5_offset;
		};
	};
	/* 0x30 */
	__u32 markers;
	__u32 hash;
	/* 0x38 */
	__u64 reserved4;
};

/*
 * Union which contains all CQE (depend on ruleset)
 */
union hea_cqe_all {
	struct hea_cqe cqe;
};

/*
 * inline functions which can be used to obtain information from CQE
 */
static inline int hea_cqe_is_valid(volatile struct hea_cqe *e, int tog)
{
	return (hea_test_u8_bit(e->valid, 0) == tog) ? 1 : 0;
}

static inline int hea_cqe_is_receive(volatile struct hea_cqe *e)
{
	return hea_test_u8_bit(e->type, 0) ? 0 : 1;
}

static inline int hea_cqe_is_transmit(volatile struct hea_cqe *e)
{
	return hea_test_u8_bit(e->type, 0) ? 1 : 0;
}

static inline int hea_cqe_rq_used(struct hea_cqe *e)
{
	return hea_get_u8_bits(e->type, 1, 2);
}

static inline int hea_cqe_is_user_cqe(struct hea_cqe *e)
{
	return hea_test_u8_bit(e->type, 3) ? 1 : 0;
}

static inline int hea_cqe_is_hardware_cqe(struct hea_cqe *e)
{
	return hea_test_u8_bit(e->type, 3) ? 0 : 1;
}

static inline int hea_cqe_has_status(volatile struct hea_cqe *e)
{
	return e->status != 0;
}

static inline int hea_cqe_status_tcp_cksum_err(struct hea_cqe *e)
{
	return hea_test_u16_bit(e->status, 1) ? 1 : 0;
}

static inline int hea_cqe_status_ip_cksum_err(struct hea_cqe *e)
{
	return hea_test_u16_bit(e->status, 2) ? 1 : 0;
}

static inline int hea_cqe_status_bad_crc(struct hea_cqe *e)
{
	return hea_test_u16_bit(e->status, 3) ? 1 : 0;
}

static inline int hea_cqe_status_header_split(struct hea_cqe *e)
{
	return hea_test_u16_bit(e->status, 4) ? 1 : 0;
}

static inline int hea_cqe_status_vlan_tag_extracted(struct hea_cqe *e)
{
	return hea_test_u16_bit(e->status, 5) ? 1 : 0;
}

static inline int hea_cqe_status_header_too_long(struct hea_cqe *e)
{
	return hea_test_u16_bit(e->status, 6) ? 1 : 0;
}

static inline int hea_cqe_status_hash_valid(struct hea_cqe *e)
{
	return hea_test_u16_bit(e->status, 7) ? 1 : 0;
}

static inline int hea_cqe_status_length_error(struct hea_cqe *e)
{
	return hea_test_u16_bit(e->status, 8) ? 1 : 0;
}

static inline int hea_cqe_status_bad_frame(struct hea_cqe *e)
{
	return hea_test_u16_bit(e->status, 10) ? 1 : 0;
}

static inline int hea_cqe_status_local_len_error(struct hea_cqe *e)
{
	return hea_get_u16_bits(e->status, 11, 15) == 0x01;
}

static inline int hea_cqe_status_local_qp_error(struct hea_cqe *e)
{
	return hea_get_u16_bits(e->status, 11, 15) == 0x02;
}

static inline int hea_cqe_status_local_prot_error(struct hea_cqe *e)
{
	return hea_get_u16_bits(e->status, 11, 15) == 0x04;
}

static inline int hea_cqe_status_tso_len_error(struct hea_cqe *e)
{
	return hea_get_u16_bits(e->status, 11, 15) == 0x05;
}

static inline int hea_cqe_status_wqe_purged(struct hea_cqe *e)
{
	return hea_get_u16_bits(e->status, 11, 15) == 0x10;
}

#define HEA_CQE_STATUS_TCP_CKSUM_ERR_BIT  0x4000
#define HEA_CQE_STATUS_IP_CKSUM_ERR_BIT   0x2000
#define HEA_CQE_STATUS_BAD_CRC_BIT        0x1000

#define HEA_CQE_STATUS_ANY_CSUM_BIT       0x7000

#define HEA_CQE_STATUS_LENGTH_ERROR_BIT   0x0080
#define HEA_CQE_STATUS_BAD_FRAME_BIT      0x0020
#define HEA_CQE_STATUS_ERRORS_BIT         0x001f
#define HEA_CQE_ANY_ERROR_BIT             0x70bf

static inline int hea_cqe_is_uc(struct hea_cqe *e)
{
	return hea_get_u8_bits(e->flags, 0, 1) == 0x00;
}

static inline int hea_cqe_is_mc(struct hea_cqe *e)
{
	return hea_get_u8_bits(e->flags, 0, 1) == 0x01;
}

static inline int hea_cqe_is_bc(struct hea_cqe *e)
{
	return hea_get_u8_bits(e->flags, 0, 1) == 0x03;
}

static inline int hea_cqe_is_wrapped(struct hea_cqe *e)
{
	return hea_test_u8_bit(e->source, 0);
}

static inline int hea_cqe_page_offset(struct hea_cqe *e)
{
	return hea_get_u16_bits(e->page_offset0, 4, 15);
}

static inline int hea_cqe_marker_dix(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 1);
}

static inline int hea_cqe_marker_sap(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 2);
}

static inline int hea_cqe_marker_snap(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 3);
}

static inline int hea_cqe_marker_q(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 4);
}

static inline int hea_cqe_marker_qq(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 5);
}

static inline int hea_cqe_marker_ppp(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 6);
}

static inline int hea_cqe_marker_mpls(struct hea_cqe *e)
{
	return hea_get_u32_bits(e->markers, 7, 12);
}

static inline int hea_cqe_marker_ipv4(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 13);
}

static inline int hea_cqe_marker_2ndipv4(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 14);
}

static inline int hea_cqe_marker_ipv6(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 15);
}

static inline int hea_cqe_marker_2ndipv6(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 16);
}

static inline int hea_cqe_marker_1st_frag(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 17);
}

static inline int hea_cqe_marker_mid_frag(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 18);
}

static inline int hea_cqe_marker_last_frag(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 19);
}

static inline int hea_cqe_marker_ipv6_rh(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 20);
}

static inline int hea_cqe_marker_l4_valid(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 21);
}

static inline int hea_cqe_marker_l5_valid(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 22);
}

static inline int hea_cqe_marker_unrec_pkt(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 29);
}

static inline int hea_cqe_marker_malf_pkt(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 30);
}

static inline int hea_cqe_marker_abort(struct hea_cqe *e)
{
	return hea_test_u32_bit(e->markers, 31);
}

static inline int hea_cqe_hash_get(struct hea_cqe *e)
{
	return hea_get_u32_bits(e->hash, 0, 31);
}

#define HEA_CQE_MARKER_ANY_FRAGMENT_BIT  0x00007000

static inline int hea_cqe_ticket_valid(struct hea_cqe *e)
{
	return hea_test_u16_bit(e->ticket, 0);
}

static inline int hea_cqe_ticket(struct hea_cqe *e)
{
	return e->ticket & (~hea_u16_bit(0));
}

/****************** AER ******************************************/

/*
 * Defines inline functions to obtain AER CQ Error Status bits
 */
static inline int hea_cq_aer_cq_overflow_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 0);
}

static inline int hea_cq_aer_cq_invalid_link_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 1);
}

static inline int hea_cq_aer_eq_par_id_invalid_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 9);
}

static inline int hea_cq_aer_eq_disabled_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 10);
}

static inline int hea_cq_aer_eq_out_of_range_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 11);
}

static inline int hea_cq_aer_eq_err_state_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 12);
}

static inline int hea_cq_aer_data_bus_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 32);
}

static inline int hea_cq_aer_mem_access_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 33);
}

static inline int hea_cq_aer_intern_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 34);
}

static inline int hea_cq_aer_length_align_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 35);
}

static inline int hea_cq_aer_reserv_facility_access_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 36);
}

static inline int hea_cq_aer_inv_class_access_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 37);
}

static inline int hea_cq_aer_eq_format_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 50);
}

static inline int hea_cq_aer_cq_threshold_exceed_warn_check(const __u64 *
							    aer)
{
	return hea_test_u64_bit(*aer, 51);
}

/*
 * Defines inline functions to reset AER CQ Error Status bits. This
 * function does not reset the actual register.  It sets the
 * appropriate bits in a variable which has to be passed to the
 * register using a different function.
 */
static inline void hea_cq_aer_cq_overflow_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 0, 0);
}

static inline void hea_cq_aer_cq_invalid_link_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 1, 1);
}

static inline void hea_cq_aer_eq_par_id_invalid_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 9, 9);
}

static inline void hea_cq_aer_eq_disabled_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 10);
}

static inline void hea_cq_aer_eq_out_of_range_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 11);
}

static inline void hea_cq_aer_eq_err_state_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 12);
}

static inline void hea_cq_aer_data_bus_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 32);
}

static inline void hea_cq_aer_mem_access_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 33);
}

static inline void hea_cq_aer_intern_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 34);
}

static inline void hea_cq_aer_length_align_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 35);
}

static inline void hea_cq_aer_reserv_facility_access_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 36);
}

static inline void hea_cq_aer_inv_class_access_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 37);
}

static inline void hea_cq_aer_eq_format_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 50);
}

static inline void hea_cq_aer_cq_threshold_exceed_warn_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 51);
}

/************** time measurement **************************************/

/*
 * Time at which the packet was received or transmitted by the HEA.
 * The time is given in two parts: seconds and fractions of seconds.
 * The time is taken from the HEA Timestamp Counter Register
 * (G_HEATIME) on page 70. For transmit, this is the time at which the
 * first packet was launched for the corresponding SQ WQE (TSO case).
 */

static inline __u32 hea_cq_timestamp_ns(__u32 frac)
{
	const __u32 log_prec = 10;
	const __u64 ns = 1000000000ULL << log_prec;
	const __u32 bits = sizeof(frac) * 8;
	__u64 ret = 0;
	__u32 i;

	for (i = 0; i < bits; i++) {
		if (hea_get_u64_bits(frac, i, i))
			ret += ns >> (i + 1);
	}

	return (__u32)((ret + (1UL << (log_prec - 1))) >> log_prec);
}

/*
 * This function can be used to obtain the timestamp from the CQ
 *
 * Usuage: double time = hea_cq_timestamp_double(cqe->timestamp_seconds,
 *						 cqe->timestamp_frac);
 */
static inline double hea_cq_timestamp_double(__u32 s, __u32 frac)
{
	const double div = 4294967296.;	/* 2 ^ 32 */
	__u64 v;
	double ret;

	v = s;
	v <<= 32;
	v |= frac;

	ret = (double)v / div;

	return ret;
}

#endif /* _ASM_POWEREN_HEA_CQ_H_ */
