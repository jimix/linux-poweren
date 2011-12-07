#ifndef _DHEA_H_
#define _DHEA_H_

#include <asm/poweren_hea_common_types.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <rhea-interface.h>

#define DHEA_MAJOR_VERSION 3
#define DHEA_MINOR_VERSION 0
#define DHEA_RELEASE_VERSION 1

#define DHEA_NAME "dHEA"
#define MAX_NUM_ADAPTERS 4
#define MAX_NUM_PPORTS 4
#define MAX_NUMBER_CHANNELS 7

struct channel_res {
	unsigned adapter_id;
	unsigned channel_id;
	unsigned last_reported_state;
	struct qp_res *head_qps;
	struct channel_res *next;
	struct pid *owner;      /* The process that owns this channel */
};


struct cq_res {
	unsigned cq_id;

	unsigned int cqe_count;
	unsigned int cq_map_size;

	unsigned long long *priv_cq_registers;
	unsigned long long *user_cq_registers;

	struct hea_cqe *begin_cqe;
	struct hea_cqe *end_cqe;
	struct hea_cqe *current_cqe;

	struct cq_res *next;
};


struct qp_res {
	unsigned qp_id;
	unsigned long long *priv_qp_registers;
	unsigned long long *user_qp_registers;

	unsigned char *sq_begin;
	unsigned char *sq_end;
	unsigned char *rq_begin[3];
	unsigned char *rq_end[3];

	struct qp_res *next;
};


/* We track the resources associated with each process so that we can handle
 events when they occur. */
struct adapter_res {
	struct list_head my_list;
	unsigned id;
	struct channel_res *head_channels;
	struct list_head eq_list_head;
	struct cq_res *head_cqs;
	unsigned long long *qp_rx_error_status;
	unsigned long long *qp_tx_error_status;
	unsigned long long *cq_error_status;

	unsigned long long *qp_rx_error_status_page_aligned;
	unsigned long long *qp_tx_error_status_page_aligned;
	unsigned long long *cq_error_status_page_aligned;

	struct adapter_res *next;
};

struct eq_res {
	unsigned adapter_id;
	unsigned eq_id;

	struct hea_q eq			____cacheline_aligned;
	struct adapter_res *adapter;

	struct list_head my_list;
	struct eq_res *next;
};

/* Next object that we need to map to userspace */
struct object_to_mmap {
	unsigned int index;
			/* For QPs: Regs=1, SQ=2, RQ1=3, RQ2=4, RQ3=5
			For CQs: Regs=1, CQE=2
			For Adapters: qp_rx = 1, qp_tx = 2, cq = 3 */
	struct cq_res *cq;
	struct qp_res *qp;
	struct adapter_res *error_status;
};

/* There is a unique instance of "dhea_user" per fd. */
struct dhea_user {
	struct list_head adapter_list_head;
	struct object_to_mmap ob_to_mmap;
	unsigned int enable_mac_loopback;
};

void add_adapter_to_head(struct adapter_res *, struct adapter_res *);
struct adapter_res *delete_adapter_element(struct adapter_res *,
	unsigned adapter_id);
struct adapter_res *find_adapter_element(struct adapter_res *,
	unsigned adapter_id);
struct adapter_res *find_adapter_element_new(struct dhea_user *duser,
	unsigned adapter_id);

void add_channel_to_head(struct channel_res*, struct channel_res*);
struct channel_res *delete_channel_element(struct channel_res*,
	unsigned channel_id);
struct channel_res *find_channel_element(struct channel_res*,
	unsigned channel_id);

void add_eq_to_head(struct eq_res*, struct eq_res*);
struct eq_res *delete_eq_element(struct eq_res *, unsigned eq_id);
struct eq_res *find_eq_element(struct eq_res *, unsigned eq_id);

void add_cq_to_head(struct cq_res*, struct cq_res *);
struct cq_res *delete_cq_element(struct cq_res *, unsigned cq_id);
struct cq_res *find_cq_element(struct cq_res *, unsigned cq_id);

void add_qp_to_head(struct qp_res*, struct qp_res*);
struct qp_res *delete_qp_element(struct qp_res *, unsigned qp_id);
struct qp_res *find_qp_element(struct qp_res *, unsigned qp_id);

static int  __init dhea_init_module(void);
static void __exit dhea_cleanup_module(void);

struct some_info {
	unsigned int size;
	u64 offset;
};

int dhea_adapter_count(struct dhea_user *duser, unsigned long arg);
int dhea_adapter_init(struct dhea_user *duser, unsigned long arg);
int dhea_adapter_fini(struct dhea_user *duser, unsigned long arg);
int dhea_get_version(struct dhea_user *duser, unsigned long arg);
int dhea_pport_count(struct dhea_user *duser, unsigned long arg);
int dhea_channel_alloc(struct dhea_user *duser, unsigned long arg);
int dhea_channel_free(struct dhea_user *duser, unsigned long arg);
int dhea_eq_alloc(struct dhea_user *duser, unsigned long arg);
int dhea_eq_free(struct dhea_user *duser, unsigned long arg);
int dhea_cq_alloc(struct dhea_user *duser, unsigned long arg);
int dhea_cq_free(struct dhea_user *duser, unsigned long arg);
int dhea_qp_alloc(struct dhea_user *duser, unsigned long arg);
int dhea_qp_free(struct dhea_user *duser, unsigned long arg);
int dhea_qpn_array_alloc(struct dhea_user *duser, unsigned long arg);
int dhea_qpn_array_free(struct dhea_user *duser, unsigned long arg);
int dhea_wire_qpn_to_qp(struct dhea_user *duser, unsigned long arg);
int dhea_get_default_mac(struct dhea_user *duser, unsigned long arg);
int dhea_qp_up(struct dhea_user *duser, unsigned long arg);
int dhea_qp_down(struct dhea_user *duser, unsigned long arg);
int dhea_channel_up(struct dhea_user *duser, unsigned long arg);
int dhea_channel_down(struct dhea_user *duser, unsigned long arg);
int dhea_tcam_slot_alloc(struct dhea_user *duser, unsigned long arg);
int dhea_tcam_slot_free(struct dhea_user *duser, unsigned long arg);
int dhea_tcam_set(struct dhea_user *duser, unsigned long arg);
int dhea_tcam_get(struct dhea_user *duser, unsigned long arg);
int dhea_tcam_enable(struct dhea_user *duser, unsigned long arg);
int dhea_tcam_disable(struct dhea_user *duser, unsigned long arg);
int dhea_mac_loopback_enable(struct dhea_user *duser, unsigned long arg);
int dhea_mac_loopback_disable(struct dhea_user *duser, unsigned long arg);
int dhea_register_default_packets(struct dhea_user *duser,
	unsigned long arg);
int dhea_deregister_default_packets(struct dhea_user *duser,
	unsigned long arg);
int dhea_channel_feature_set(struct dhea_user *duser, unsigned long arg);
int dhea_channel_feature_get(struct dhea_user *duser, unsigned long arg);
void dhea_scan_event_queue(struct eq_res *eq);


#define dhea_info(fmt, args...)			\
	pr_info("poweren_dhea: " fmt "\n", ## args)

#define dhea_warning(fmt, args...)		\
	pr_warning("poweren_dhea: " fmt "\n", ## args)

#ifdef __KERNEL__
#define dhea_error(fmt, args...)					\
	pr_err("poweren_dhea: Error in %s(): " fmt "\n", __func__, ## args)
#endif	/* __KERNEL__ */

#define dhea_debug(fmt, args...)

#endif
