/*
 * Copyright (C) 2011, 2012 IBM Corporation.
 * Author:	Davide Pasetto <pasetto_davide@ie.ibm.com>
 *		Karol Lynch <karol_lynch@ie.ibm.com>
 *		Kay Muller <kay.muller@ie.ibm.com>
 *		Jimi Xenidis <jimix@watson.ibm.com>
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

#ifndef _ASM_POWEREN_HEA_QP_H_
#define _ASM_POWEREN_HEA_QP_H_

#include "poweren_hea_bitops.h"

static const __u32 HEA_QUEUE_MAX_LENGTH = 65536U;

enum hea_qp_feature_get {
	HEA_QP_TOKEN_GET,
	HEA_QP_AER_GET,
	HEA_QP_AERR_GET,
	HEA_QP_ENABLED_GET,
};

enum hea_qp_feature_set {
	HEA_QP_AER_SET,
	HEA_QP_AERR_SET,
};

enum hea_rq_cqe_generation {
	HEA_RQ_CQE_ENABLE = 0,
	HEA_RQ_CQE_DISABLE = 1,
};

enum hea_sq_cqe_generation {
	HEA_SQ_CQE_DISABLE = 0,
	HEA_SQ_CQE_WQE_SPECIFIED = 1,
	HEA_SQ_CQE_ENABLE = 2,
};

enum hea_rx_early_discard_threshold {
	HEA_RX_EARLY_DISCARD_THRESHOLD_0, /* Disables early discard mechanism */
	HEA_RX_EARLY_DISCARD_THRESHOLD_32,
	HEA_RX_EARLY_DISCARD_THRESHOLD_64,
	HEA_RX_EARLY_DISCARD_THRESHOLD_128,
	HEA_RX_EARLY_DISCARD_THRESHOLD_256,
	HEA_RX_EARLY_DISCARD_THRESHOLD_512,
	HEA_RX_EARLY_DISCARD_THRESHOLD_1024,
	HEA_RX_EARLY_DISCARD_THRESHOLD_2048
};

enum hea_wqe_size {
	HEA_WQE_SIZE_128 = 0x0,
	HEA_WQE_SIZE_256 = 0x1,
	HEA_WQE_SIZE_512 = 0x2,
	HEA_WQE_SIZE_1024 = 0x3,
	HEA_WQE_SIZE_2048 = 0x4,
	HEA_WQE_SIZE_4096 = 0x5,
	HEA_WQE_SIZE_8 = 0x3,	/* 8-byte compact RWQE */
	HEA_WQE_SIZE_16 = 0x4	/* 16-byte compact SWQE */
};

enum hea_threshold_vals {
	HEA_THRESHOLD_VAL_128 = 0x0,
	HEA_THRESHOLD_VAL_256 = 0x1,
	HEA_THRESHOLD_VAL_512 = 0x2,
	HEA_THRESHOLD_VAL_1024 = 0x3,
	HEA_THRESHOLD_VAL_2048 = 0x4,
	HEA_THRESHOLD_VAL_4096 = 0x5,
	HEA_THRESHOLD_VAL_1518 = 0x8,
	HEA_THRESHOLD_VAL_1522 = 0x9,
	HEA_THRESHOLD_VAL_9022 = 0xa
};

enum header_separation {
	HEA_HEAD_SEP_NONE = 0,
	HEA_HEAD_SEP_ALL = 3,
	HEA_HEAD_SEP_MED_LARGE = 2
};

/*
 * SQ options used when creating a QP.
 *   wqe_size	The size of each send WQE
 *   wqe_count	The maximum number of WQEs this CQ can hold.
 *   priority	Set the priority of a SQ for transmitting (i.e. sets QPx_SL).
 *		1 to set to high priority
 *		0 to low priority (0 is the default priority)
 *   tenure	Sets the tenure of a SQ for transmitting
 *		(i.e. sets QPx_TENURE). 64*tenure is the maximum amount of
 *		data that can be transmitted from this QP when scheduled.
 */
struct hea_sq_cfg {
	enum hea_wqe_size wqe_size;
	enum hea_q_size wqe_count;
	__u32 priority;
	__u8 tenure;
};

/*
 * RQ options used when creating a QP.
 *
 * wqe_size		The size of each send WQE
 * discard_threshold -	point at which to start discarding
 *			packets according to the early discard mechanism.
 * wqe_count		The maximum number of WQEs this CQ can hold.
 */
struct hea_rq_cfg {
	enum hea_wqe_size wqe_size;
	enum hea_rx_early_discard_threshold discard_threshold;
	enum hea_threshold_vals data_threshold;
	enum hea_q_size wqe_count;
	__u32 pad;
};

struct hea_rq1_cfg {
	enum hea_wqe_size wqe_size;
	enum hea_rx_early_discard_threshold discard_threshold;
	enum hea_threshold_vals data_threshold;
	enum hea_q_size wqe_count;
	__u32 low_latency;
	__u32 pad;
};

/*
 * Specifies parameters for protection domain
 *
 */
struct hea_pd_cfg {
	__u8 as_bit;
	__u8 gs_bit;
	__u8 pr_bit;
	__u8 enable_pid_validation;
	__u32 pid;
};

/*
 *	sq		options for this SQ
 *	rq1		options for RQ 1
 *	rq2		options for RQ 2, can be null.
 *	rq3		options for RQ 3, end point only, can be null.
 *	rcq		CQ used for receive completions, must be on same
 *			HEA as channel
 *	scq		CQ used for transmit completions, must be on same
 *			HEA as channel
 *	rq1_data_limit	The maximum amount of data that can be stored by a
 *			RQ1 WQE
 *	rq2_data_limit	The maximum amount of data that can be stored by a
 *			RQ2 WQE
 *
 *	low_latency	enable or disable header separation.
 *	head_sep	header separation can only be enabled if low
 *			 latency is also enabled.
 *	cqe_in_wqe	enabled or disabled CQE-in-WQE.
 *			It is only possible if low latency is enabled.
 *	channel		channel which wants to use this QP
 *
 */
struct hea_qp_cfg {
	struct hea_sq_cfg sq;
	struct hea_rq1_cfg rq1;
	struct hea_rq_cfg rq2;
	struct hea_rq_cfg rq3_ep;
	enum hea_cache_inject_type cache_inject;
	enum hea_target_cache target_cache;

	union {

		/* EP only options */
		struct {
			enum header_separation header_sep;
			enum hea_cache_inject_type ll_cache_inject;
		} ep;
	};

	enum hea_rq_cqe_generation r_cq_use;
	enum hea_sq_cqe_generation s_cq_use;
	__u32 hw_managed;
	__u32 dma_64_bit_aligned;
	__u32 real_mode;
	__u32 pad;
};

/*
 * Defines inline functions to obtain AER QP Error Status bits
 */
static inline int hea_qp_aer_data_bus_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 32);
}

static inline int hea_qp_aer_mem_access_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 33);
}

static inline int hea_qp_aer_intern_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 34);
}

static inline int hea_qp_aer_length_align_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 35);
}

static inline int hea_qp_aer_reserv_facility_access_err_check(const __u64 *
							      aer)
{
	return hea_test_u64_bit(*aer, 36);
}

static inline int hea_qp_aer_inv_class_access_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 37);
}

static inline int hea_qp_aer_sq_overflow_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 38);
}

static inline int hea_qp_aer_sq_completion_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 39);
}

static inline int hea_qp_aer_sq_inval_link_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 40);
}

static inline int hea_qp_aer_rq_overflow_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 41);
}

static inline int hea_qp_aer_rq_completion_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 42);
}

static inline int hea_qp_aer_cq_overflow_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 46);
}

static inline int hea_qp_aer_cq_process_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 47);
}

static inline int hea_qp_aer_cq_valid_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 48);
}

static inline int hea_qp_aer_eq_process_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 51);
}

static inline int hea_qp_aer_wqe_data_access_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 54);
}

static inline int hea_qp_aer_sq_threshold_exceed_warn_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 55);
}

/*
 * Defines inline functions to reset AER QP Error Status bits. This
 * function does not reset the actual register.  It sets the
 * appropriate bits in a variable which has to be passed to the
 * register using a different function.
 */
static inline void hea_qp_aer_data_bus_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 0, 0);
}

static inline void hea_qp_aer_mem_access_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 1, 1);
}

static inline void hea_qp_aer_intern_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 2, 2);
}

static inline void hea_qp_aer_length_align_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 3, 3);
}

static inline void hea_qp_aer_reserv_facility_access_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 4, 4);
}

static inline void hea_qp_aer_inv_class_access_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 5, 5);
}

static inline void hea_qp_aer_sq_overflow_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 6, 6);
}

static inline void hea_qp_aer_sq_completion_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 7, 7);
}

static inline void hea_qp_aer_sq_inval_link_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 8, 8);
}

static inline void hea_qp_aer_rq_overflow_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 9, 9);
}

static inline void hea_qp_aer_rq_completion_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 10, 10);
}

static inline void hea_qp_aer_cq_overflow_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 14, 14);
}

static inline void hea_qp_aer_cq_process_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 15, 15);
}

static inline void hea_qp_aer_cq_valid_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 16, 16);
}

static inline void hea_qp_aer_eq_process_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 19, 19);
}

static inline void hea_qp_aer_wqe_data_access_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 22, 22);
}

static inline void hea_qp_aer_sq_threshold_exceed_warn_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 1, 23, 23);
}

/*
 * Defines inline functions to obtain AERR QP Error Status bits
 */
static inline int hea_qp_aerr_data_bus_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 32);
}

static inline int hea_qp_aerr_mem_access_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 33);
}

static inline int hea_qp_aerr_intern_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 34);
}

static inline int hea_qp_aerr_length_align_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 35);
}

static inline int
hea_qp_aerr_reserv_facility_access_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 36);
}

static inline int hea_qp_aerr_inv_class_access_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 37);
}

static inline int hea_qp_aerr_sq_overflow_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 38);
}

static inline int hea_qp_aerr_sq_completion_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 39);
}

static inline int hea_qp_aerr_sq_inval_link_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 40);
}

static inline int hea_qp_aerr_rq_overflow_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 41);
}

static inline int hea_qp_aerr_rq_completion_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 42);
}

static inline int hea_qp_aerr_malformed_wqe_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 43);
}

static inline int hea_qp_aerr_rq_ll_size_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 44);
}

static inline int hea_qp_aerr_rq_count_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 45);
}

static inline int hea_qp_aerr_cq_overflow_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 46);
}

static inline int hea_qp_aerr_cq_process_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 47);
}

static inline int hea_qp_aerr_cq_valid_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 48);
}

static inline int hea_qp_aerr_eq_process_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 51);
}

static inline int hea_qp_aerr_rq_dispatch_err_check(const __u64 *aerr)
{
	return hea_test_u64_bit(*aerr, 54);
}

static inline int hea_qp_aerr_rq_replenish_overflow_err_check(const __u64 *
							      aerr)
{
	return hea_test_u64_bit(*aerr, 55);
}

static inline void hea_qp_aerr_data_bus_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 0, 0);
}

static inline void hea_qp_aerr_mem_access_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 1, 1);
}

static inline void hea_qp_aerr_intern_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 2, 2);
}

static inline void hea_qp_aerr_length_align_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 3, 3);
}

static inline void hea_qp_aerr_reserv_facility_access_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 4, 4);
}

static inline void hea_qp_aerr_inv_class_access_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 5, 5);
}

static inline void hea_qp_aerr_sq_overflow_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 6, 6);
}

static inline void hea_qp_aerr_sq_completion_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 7, 7);
}

static inline void hea_qp_aerr_sq_inval_link_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 8, 8);
}

static inline void hea_qp_aerr_rq_overflow_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 9, 9);
}

static inline void hea_qp_aerr_rq_completion_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 10, 10);
}

static inline void hea_qp_aerr_malformed_wqe_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 11, 11);
}

static inline void hea_qp_aerr_rq_ll_size_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 12, 12);
}

static inline void hea_qp_aerr_rq_count_size_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 13, 13);
}

static inline void hea_qp_aerr_cq_overflow_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 14, 14);
}

static inline void hea_qp_aerr_cq_process_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 15, 15);
}

static inline void hea_qp_aerr_cq_valid_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 16, 16);
}

static inline void hea_qp_aerr_eq_process_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 19, 19);
}

static inline void hea_qp_aerr_rq_dispatch_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 22, 22);
}

static inline void hea_qp_aerr_rq_replenish_overflow_err_reset(__u64 *aerr)
{
	*aerr = hea_set_u64_bits(*aerr, 1, 23, 23);
}

#endif /* _ASM_POWEREN_HEA_QP_H_ */
