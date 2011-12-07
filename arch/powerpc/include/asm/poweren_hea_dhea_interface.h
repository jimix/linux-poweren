/*
 User-space programs that wish to use dhea must include this file.  This file is
 also included by the dhea kernel module.
*/
#ifndef DHEA_INTERFACE_H
#define DHEA_INTERFACE_H

#define DHEA_MAJOR 33

#include <linux/ioctl.h>
#include "poweren_hea_common_types.h"

struct dhea_adapter_count_parms {
	unsigned int adapter_count; /* output */
};

struct dhea_adapter_init_parms {
	unsigned int adapter_number; /* input */

	unsigned long qp_rx_error_status_size; /* output */
	unsigned long qp_tx_error_status_size; /* output */
	unsigned long cq_error_status_size; /* output */
	unsigned adapter_id; /* output */
	int error_number; /* output */
};

struct dhea_adapter_fini_parms {
	unsigned adapter_id; /* input */
	int error_number; /* output */
};

struct dhea_get_version_parms {
	unsigned int major;	/* output */
	unsigned int minor;	/* output */
	unsigned int release;	/* output */

	int error_number; /* output */
};

struct dhea_pport_count_parms {
	unsigned adapter_id; /* input */
	unsigned int pport_count; /* output */
};

struct dhea_channel_alloc_parms {
	unsigned adapter_id; /* input */
	unsigned int pport_number; /* input */
	struct hea_channel_context channel_context; /* input */

	unsigned channel_id; /* output */
	int error_number; /* output */
};

struct dhea_channel_free_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */

	int error_number; /* output */
};

struct dhea_register_default_packets_parms {
	unsigned adapter_id; /* input */
	unsigned int pport_number; /* input */
	unsigned channel_id; /* input */
	enum hea_channel_type type; /* input */

	unsigned def_channel_id; /* output */
	int error_number; /* output */
};

struct dhea_deregister_default_packets_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */

	int error_number; /* output */
};

struct dhea_eq_alloc_parms {
	unsigned adapter_id; /* input */
	unsigned int eqe_count;	/* input */

	unsigned eq_id; /* output */
	int error_number; /* output */
};

struct dhea_eq_free_parms {
	unsigned adapter_id; /* input */
	unsigned eq_id; /* input */
	int error_number; /* output */
};

struct dhea_cq_alloc_parms {
	unsigned adapter_id; /* input */
	unsigned ceq; /* input */
	unsigned aeq; /* input */
	unsigned int cqe_count; /* input */
	unsigned int hw_managed; /* input */
	unsigned int nn_ticket_set_id; /* input */
	unsigned int nn_packet_ordering; /* input */

	unsigned int cq_map_size; /* output */
	unsigned int cq_size_bytes; /* output = number of bytes in CQ. */
	enum hea_cache_inject_type cache_inject;
	enum hea_target_cache target_cache;
	unsigned cq_id; /* output */
	int error_number; /* output */
};

struct dhea_cq_free_parms {
	unsigned adapter_id; /* input */
	unsigned cq_id; /* input */
	int error_number; /* output */
};

struct dhea_qp_alloc_parms {
	unsigned adapter_id; /* input */
	struct hea_sq_cfg sq;  /* input */
	struct hea_rq1_cfg rq1; /* input */
	struct hea_rq_cfg rq2; /* input */
	struct hea_rq_cfg rq3; /* input */

	enum hea_cache_inject_type cache_inject; /* input */
	enum hea_target_cache target_cache; /* input */
	enum hea_cache_inject_type ll_cache_inject; /* input */
	enum header_separation header_sep; /* input */

	unsigned eq_id;
	unsigned rcq_id;  /* input */
	unsigned scq_id; /* input */
	unsigned channel_id; /* input */
	enum hea_rq_cqe_generation r_cq_use;  /* input */
	enum hea_sq_cqe_generation s_cq_use;  /* input */

	unsigned int hw_managed; /* input */

	unsigned int nn_ticket_set_id; /* input */
	unsigned int service_level; /* input */

	unsigned int qp_map_size; /* output */
	unsigned int sq_size_bytes; /* output */
	unsigned int rq_size_bytes[3]; /* output */
	unsigned qp_id; /* output */
	int error_number; /* output */
};


struct dhea_qp_free_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id;	/* input */
	unsigned qp_id; /* input */

	int error_number; /* output */
};

struct dhea_qpn_array_alloc_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */
	enum hea_qpn_slot_number num_slots; /* input */

	unsigned int base_slot; /* output */
	int error_number; /* output */
};

struct dhea_qpn_array_free_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */

	int error_number; /* output */
};

struct dhea_get_default_mac_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */

	union hea_mac_addr mac_address; /* output */
	int error_number; /* output */
};

struct dhea_qp_up_parms {
	unsigned adapter_id; /* input */
	unsigned qp_id;	/* input */

	int error_number; /* output */
};

struct dhea_qp_down_parms {
	unsigned adapter_id; /* input */
	unsigned qp_id; /* input */

	int error_number; /* output */
};

struct dhea_channel_up_parms {
	unsigned adapter_id;	/* input */
	unsigned channel_id;	/* input */

	int error_number; /* output */
};

struct dhea_channel_down_parms {
	unsigned adapter_id;	/* input */
	unsigned channel_id;	/* input */

	int error_number; /* output */
};

struct dhea_wire_qpn_to_qp_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */
	unsigned qp_id; /* input */
	unsigned base_slot; /* input */
	unsigned int offset; /* input */

	int error_number; /* output */
};

struct dhea_tcam_slot_alloc_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */
	struct hea_tcam_context ctx; /* input */

	unsigned tcam_id; /* output */
	int error_number; /* output */
};

struct dhea_tcam_slot_free_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */
	unsigned tcam_id; /* input */

	int error_number; /* output */
};

struct dhea_tcam_set_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */
	unsigned tcam_id; /* input */
	struct hea_tcam_setting tcam_setting; /* input */

	int error_number; /* output */
};

struct dhea_tcam_get_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */
	unsigned tcam_id; /* input */

	struct hea_tcam_setting tcam_setting;
	int error_number; /* output */
};

struct dhea_tcam_enable_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */
	unsigned tcam_id; /* input */
	unsigned int tcam_offset; /* input */

	int error_number; /* output */
};

struct dhea_tcam_disable_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */
	unsigned tcam_id; /* input */
	unsigned int tcam_offset; /* input */

	int error_number; /* output */
};

struct dhea_mac_loopback_parms {
	int error_number; /* output */
};

struct dhea_channel_feature_set_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */
	enum hea_channel_feature_set feature; /* input */
	unsigned long long value; /* input */

	int error_number; /* output */
};

struct dhea_channel_feature_get_parms {
	unsigned adapter_id; /* input */
	unsigned channel_id; /* input */
	enum hea_channel_feature_get feature; /* input */
	unsigned long long index;  /* input */

	unsigned long long value; /* output */
	int error_number; /* output */
};

#define IOCTL_DHEA_ADAPTER_COUNT \
	_IOR(DHEA_MAJOR, 0, struct dhea_adapter_count_parms*)

#define IOCTL_DHEA_ADAPTER_INIT \
	_IOWR(DHEA_MAJOR, 1, struct dhea_adapter_init_parms*)

#define IOCTL_DHEA_ADAPTER_FINI \
	_IOWR(DHEA_MAJOR, 2, struct dhea_adapter_fini_parms*)
#define IOCTL_DHEA_GET_VERSION \
	_IOWR(DHEA_MAJOR, 3, struct dhea_get_version_parms*)

#define IOCTL_DHEA_PPORT_COUNT \
	_IOWR(DHEA_MAJOR, 10, struct dhea_pport_count_parms*)

#define IOCTL_DHEA_CHANNEL_ALLOC \
	_IOWR(DHEA_MAJOR, 20, struct dhea_channel_alloc_parms*)
#define IOCTL_DHEA_CHANNEL_FREE \
	_IOWR(DHEA_MAJOR, 21, struct dhea_channel_free_parms*)
#define IOCTL_DHEA_CHANNEL_UP \
	_IOWR(DHEA_MAJOR, 22, struct dhea_channel_up_parms*)
#define IOCTL_DHEA_CHANNEL_DOWN \
	_IOWR(DHEA_MAJOR, 23, struct dhea_channel_down_parms*)
#define IOCTL_DHEA_DEFAULT_PACKETS_REGISTER \
	_IOWR(DHEA_MAJOR, 24, struct dhea_register_default_packets_parms*)
#define IOCTL_DHEA_DEFAULT_PACKETS_DEREGISTER \
	_IOWR(DHEA_MAJOR, 25, struct dhea_deregister_default_packets_parms*)
#define IOCTL_DHEA_CHANNEL_FEATURE_SET \
	_IOWR(DHEA_MAJOR, 26, struct dhea_channel_feature_set_parms*)
#define IOCTL_DHEA_CHANNEL_FEATURE_GET \
	_IOWR(DHEA_MAJOR, 27, struct dhea_channel_feature_get_parms*)


#define IOCTL_DHEA_EQ_ALLOC \
	_IOWR(DHEA_MAJOR, 30, struct dhea_eq_alloc_parms*)

#define IOCTL_DHEA_EQ_FREE \
	_IOWR(DHEA_MAJOR, 31, struct dhea_eq_free_parms*)

#define IOCTL_DHEA_CQ_ALLOC \
	_IOWR(DHEA_MAJOR, 40, struct dhea_cq_alloc_parms*)
#define IOCTL_DHEA_CQ_FREE \
	_IOWR(DHEA_MAJOR, 41, struct dhea_cq_free_parms*)

#define IOCTL_DHEA_QP_ALLOC \
	_IOWR(DHEA_MAJOR, 50, struct dhea_qp_alloc_parms*)
#define IOCTL_DHEA_QP_FREE \
	_IOWR(DHEA_MAJOR, 51, struct dhea_qp_alloc_parms*)
#define IOCTL_DHEA_QP_UP \
	_IOWR(DHEA_MAJOR, 52, struct dhea_qp_up_parms*)
#define IOCTL_DHEA_QP_DOWN \
	_IOWR(DHEA_MAJOR, 53, struct dhea_qp_down_parms*)

#define IOCTL_DHEA_QPN_ARRAY_ALLOC \
	_IOWR(DHEA_MAJOR, 60, struct dhea_qpn_array_alloc_parms*)
#define IOCTL_DHEA_QPN_ARRAY_FREE \
	_IOWR(DHEA_MAJOR, 61, struct dhea_qpn_array_free_parms*)
#define IOCTL_DHEA_WIRE_QPN_TO_QP \
	_IOWR(DHEA_MAJOR, 62, struct dhea_wire_qpn_to_qp_parms*)

#define IOCTL_DHEA_GET_DEFAULT_MAC \
	_IOWR(DHEA_MAJOR, 70, struct dhea_get_default_mac_parms*)

#define IOCTL_DHEA_TCAM_SLOT_ALLOC \
	_IOWR(DHEA_MAJOR, 80, struct dhea_tcam_slot_alloc_parms*)
#define IOCTL_DHEA_TCAM_SLOT_FREE \
	_IOWR(DHEA_MAJOR, 81, struct dhea_tcam_slot_free_parms*)
#define IOCTL_DHEA_TCAM_SET \
	_IOWR(DHEA_MAJOR, 82, struct dhea_tcam_set_parms*)
#define IOCTL_DHEA_TCAM_GET \
	_IOWR(DHEA_MAJOR, 83, struct dhea_tcam_get_parms*)
#define IOCTL_DHEA_TCAM_ENABLE \
	_IOWR(DHEA_MAJOR, 84, struct dhea_tcam_enable_parms*)
#define IOCTL_DHEA_TCAM_DISABLE \
	_IOWR(DHEA_MAJOR, 85, struct dhea_tcam_disable_parms*)

#define IOCTL_DHEA_MAC_LOOPBACK_ENABLE \
	_IOWR(DHEA_MAJOR, 90, struct dhea_mac_loopback_parms*)
#define IOCTL_DHEA_MAC_LOOPBACK_DISABLE \
	_IOWR(DHEA_MAJOR, 91, struct dhea_mac_loopback_parms*)

#endif

