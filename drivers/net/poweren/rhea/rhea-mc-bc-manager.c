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

#include <linux/irqreturn.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/interrupt.h>

#include <linux/sched.h>
#include <linux/workqueue.h>

#include "rhea-mc-bc-manager.h"

#include "rhea-base.h"

#include <rhea-interface.h>
#include <hea-queue.h>
#include <asm/poweren_hea_wqe.h>
#include <hea-qp-regs.h>
#include <hea-cq-regs.h>
#include <hea-atomic.h>

#include <asm/poweren_hea_cq.h>
#include <asm/poweren_hea_eq.h>
#include <asm/poweren_hea_qp.h>

#include <rhea-linux.h>

#define HEA_MAX_SQ_ITERATION 10000000UL
#define HEA_MAX_BC_MC_RECV_Q 1
#define HEA_MAX_BC_MC_RECV_Q_BUFFER 3

#define RHEA_MANAGER_EQ_WQES HEA_Q_SIZE_1K
#define RHEA_MANAGER_RQ_WQES HEA_Q_SIZE_1K
#define RHEA_MANAGER_CQ_WQES HEA_Q_SIZE_1K
#define RHEA_MANAGER_SQ_WQES HEA_Q_SIZE_1K

#define HEA_MANAGER_RECEIVE_BUDGET (RHEA_MANAGER_RQ_WQES / 2 - 1)

enum hea_bc_mc_uc_manager_state {
	HEA_MANAGER_NO_STATE,
	HEA_MANAGER_INIT,
	HEA_MANAGER_DESTROYED,
	HEA_MANAGER_ENABLED,
	HEA_MANAGER_DISABLED,
};

struct rhea_mb_bc_manager_rhea {
	unsigned rhea_id;
	unsigned eq_id;
	unsigned r_cq_id;
	unsigned s_cq_id;
	unsigned qp_id;

	unsigned qpn_channel_id;

	struct hea_q eq;

	struct hea_q r_cq;
	struct hea_q s_cq;

	struct hea_q sq;
	struct hea_q rq[HEA_MAX_BC_MC_RECV_Q];

	struct rhea_cqte *s_cq_registers;
	struct rhea_cqte *r_cq_registers;
	struct rhea_qpte *qp_registers;
};

struct rhea_mb_bc_manager_channel {
	spinlock_t lock;
	enum hea_bc_mc_uc_manager_state state;
	hea_atomic_t instance_count;
	unsigned channel_id;
	struct hea_channel_cfg *active_lports[HEA_MAX_PPORT_LPORT_COUNT];
	struct hea_channel_cfg *registered_lports[HEA_MAX_PPORT_LPORT_COUNT];
};

#define HEA_MANAGER_IDLE 0
#define HEA_MANAGER_PROCESSING 1
#define HEA_MANAGER_SETUP 2

struct rhea_mc_bc_manager_context {
	spinlock_t lock;
	unsigned char pad[64];
	unsigned int count;
	unsigned int irq_nr;
	unsigned int process_state;
	unsigned int request_setup;
	unsigned int pport_nr;
	enum hea_bc_mc_uc_manager_state state;
	unsigned int mem_buf_index;
	unsigned int mem_buf_slot;
	unsigned int rq_buf_slot_size;
	struct rhea_mem rq_mem[HEA_MAX_BC_MC_RECV_Q_BUFFER];
	struct rhea_mb_bc_manager_rhea rhea;
	struct delayed_work irq_work;
	struct workqueue_struct *irq_workqueue;
	struct rhea_mb_bc_manager_channel bc;
	struct rhea_mb_bc_manager_channel mc;
	struct rhea_mb_bc_manager_channel uc;
};

static inline void
hea_mc_bc_state_acquire(struct rhea_mc_bc_manager_context *cntxt,
			unsigned int state_new)
{
	spin_lock(&cntxt->lock);
}

static inline void
hea_mc_bc_state_release(struct rhea_mc_bc_manager_context *cntxt)
{
	spin_unlock(&cntxt->lock);
}

/**
 * This function computes the amount of time which should pass, until
 * the next request is issued.
 *
 * @param cntxt[in]	Context manager
 * @return		Returns number of jiffies
 */
static inline unsigned int
hea_mc_bc_delay_get(struct rhea_mc_bc_manager_context *cntxt)
{
	return 0;
}

static struct rhea_mc_bc_manager_context
	*s_mc_bc_manager_context[HEA_MAX_PPORT_COUNT];

static int hea_scan_event_queue(struct rhea_mc_bc_manager_context *cntxt);

static int hea_handle_receive_bc(struct rhea_mc_bc_manager_context *cntxt,
				 struct hea_cqe *cqe_current,
				 unsigned long buffer_packet);
static int hea_handle_receive_mc(struct rhea_mc_bc_manager_context *cntxt,
				 struct hea_cqe *cqe_current,
				 unsigned long buffer_packet);
static int hea_handle_receive_uc(struct rhea_mc_bc_manager_context *cntxt,
				 struct hea_cqe *cqe_current,
				 unsigned long buffer_packet);

static int hea_scan_recv_cq(struct rhea_mc_bc_manager_context *cntxt,
			    unsigned int budget);

static int _hea_fill_recvq(struct hea_q *rq, unsigned rq_nr,
			   void **buffer_array, unsigned buffer_count,
			   unsigned buffer_element_size,
			   struct rhea_qpte *qp_registers);

static int _rhea_mc_bc_manager_start(
	struct rhea_mc_bc_manager_context *mng_ctx,
	enum hea_channel_type register_type);
static int _rhea_mc_bc_manager_stop(struct rhea_mc_bc_manager_context *mng_ctx,
				    enum hea_channel_type register_type);

static int _rhea_mc_bc_manager_create(struct rhea_mc_bc_manager_context
				      *mng_ctx,
				      enum hea_channel_type registered_type);
static int _rhea_mc_bc_manager_destroy(struct rhea_mc_bc_manager_context
				       *mng_ctx);

static int
_rhea_mc_bc_manager_channel_create(struct rhea_mc_bc_manager_context *mng_ctx,
				   enum hea_channel_type registered_type,
				   unsigned *channel_id);
static int
_rhea_mc_bc_manager_channel_destroy(struct rhea_mc_bc_manager_context *mng_ctx,
				    enum hea_channel_type registered_type);

static int
_rhea_mc_bc_manager_channel_start(struct rhea_mc_bc_manager_context *mng_ctx,
				  enum hea_channel_type registered_type);

static int
_rhea_mc_bc_manager_channel_stop(struct rhea_mc_bc_manager_context *mng_ctx,
				 enum hea_channel_type registered_type);

static int hea_scan_event_queue(struct rhea_mc_bc_manager_context *cntxt)
{
	struct hea_eqe *eqe_current;
	struct hea_q *eq;

	if (unlikely(NULL == cntxt))
		return -EINVAL;

	eq = &cntxt->rhea.eq;

	if (unlikely(NULL == eq))
		return -EINVAL;

	eqe_current =  eq->qe_current;
	while (hea_eqe_is_valid(eqe_current)) {
		if (unlikely(!hea_eqe_is_completion(eqe_current))) {
			unsigned q_nr;
			unsigned long long value;

			rhea_info(" event 0x%016llx", eqe_current->eqe);

			switch (hea_eqe_event_type(eqe_current)) {
			case HEA_EQE_ET_QP_WARNING:

				q_nr = hea_eqe_qp_number(eqe_current);
				rhea_qp_get(cntxt->rhea.rhea_id, q_nr,
					    HEA_QP_AER_GET, &value);

				rhea_info("Warning for QP: %u and send "
					  "warning: 0x%llx",
					  q_nr, value);
				rhea_qp_get(cntxt->rhea.rhea_id, q_nr,
					    HEA_QP_AERR_GET, &value);

				rhea_info("Warning for QP: %u and receive "
					  "warning: 0x%llx",
					 q_nr, value);
				break;

			case HEA_EQE_ET_CP_WARNING:
				q_nr = hea_eqe_cq_number(eqe_current);

				rhea_cq_get(cntxt->rhea.rhea_id, q_nr,
					    HEA_CQ_AER_GET, &value);

				rhea_info
					("Warning for CQ: %u and warning: "
					 "0x%llx",
					 q_nr, value);
				break;

			case HEA_EQE_ET_QP_ERROR_EQ0:
			case HEA_EQE_ET_QP_ERROR:

				q_nr = hea_eqe_qp_number(eqe_current);

				rhea_qp_get(cntxt->rhea.rhea_id, q_nr,
					    HEA_QP_AER_GET, &value);

				rhea_info("Error for QP: %u and send error: "
					  "0x%llx for pport: %u",
					  q_nr, value, cntxt->pport_nr + 1);

				rhea_qp_get(cntxt->rhea.rhea_id, q_nr,
					    HEA_QP_AERR_GET, &value);

				rhea_info("Error for QP: %u and receive "
					  "error: 0x%llx",
					 q_nr, value);

				rhea_qp_dumps(cntxt->rhea.rhea_id, q_nr);

				BUG();
				break;

			case HEA_EQE_ET_CQ_ERROR_EQ0:
			case HEA_EQE_ET_CQ_ERROR:

				q_nr = hea_eqe_cq_number(eqe_current);
				rhea_cq_get(cntxt->rhea.rhea_id, q_nr,
					    HEA_CQ_AER_GET, &value);

				rhea_info("Error for CQ: %u and error: "
					  "0x%llx for pport: %u",
					  q_nr, value, cntxt->pport_nr + 1);

				if (cntxt->rhea.s_cq_id == q_nr)
					rhea_info("S_CQ");
				else
					rhea_info("R_CQ");

				rhea_cq_dumps(cntxt->rhea.rhea_id, q_nr);

				BUG();
				break;

			case HEA_EQE_ET_PORT_EVENT:
				rhea_info("HEA_EQE_ET_PORT_EVENT");
				BUG();
				break;

			case HEA_EQE_ET_EQ_ERROR:
				rhea_info("HEA_EQE_ET_EQ_ERROR");
				BUG();
				break;

			case HEA_EQE_ET_UA_ERROR:
				rhea_info("HEA_EQE_ET_UA_ERROR");
				BUG();
				break;

			case HEA_EQE_ET_FIRST_ERROR_CAPTURE_INFO:
				rhea_info("First Error Capture info");
				BUG();
				break;

			case HEA_EQE_ET_COP_CQ_ACCESS_ERROR:
				rhea_info("HEA_EQE_ET_COP_CQ_ACCESS_ERROR");
				BUG();
				break;

			case HEA_EQE_ET_COP_QP_ACCESS_ERROR:
				rhea_info("HEA_EQE_ET_COP_QP_ACCESS_ERROR");
				BUG();
				break;

			case HEA_EQE_ET_COP_TICKET_ACCESS_ERROR:
			case HEA_EQE_ET_COP_TICKET_ERROR:
			case HEA_EQE_ET_COP_DATA_ERROR:
				BUG();
			default:
				;
			}
		}

		/* clear this entry and move to next one */
		eqe_current->eqe = 0;
		heaq_set_next_qe(eq);
		eqe_current = eq->qe_current;
	}

	return 0;
}

static inline int _hea_rq_wqe_pointer_swap(struct rcv_wqe_normal *rwqe,
					   unsigned long *buffer_old,
					   unsigned long buffer_new)
{
	/* get old pointer */
	(*buffer_old) = rwqe->descriptors[0].addr;

	/* fill rq wqe will parameters */
	rwqe->descriptors[0].addr = buffer_new;
	return 0;
}

int hea_scan_send_cq(struct rhea_mc_bc_manager_context *cntxt)
{
	struct hea_cqe *cqe_current;
	int cqe_num = 0;
	int rq_qe_num = 0;

	cqe_current = cntxt->rhea.s_cq.qe_current;

	while (hea_cqe_is_valid(cqe_current, cntxt->rhea.s_cq.q_toggle_bit)) {
		BUG_ON(hea_cqe_is_receive(cqe_current));

		/* check if this is an error */
		if (unlikely(hea_cqe_has_status(cqe_current)))
			rhea_error("has send CQ error");
		else
			++rq_qe_num;

		++cqe_num;

		/* mark that one more WQE is available in SQ */
		heaq_inc_count(&cntxt->rhea.sq);

		heaq_set_next_qe(&cntxt->rhea.s_cq);
		cqe_current = cntxt->rhea.s_cq.qe_current;
	}

	if (likely(cqe_num > 0)) {
		rhea_debug("processed %d entries for sq", cqe_num);
		iosync();
		out_be64(&cntxt->rhea.s_cq_registers->cq_feca, cqe_num);
	}

	rhea_debug("Found %u RQ WQEs", rq_qe_num);

	return rq_qe_num;
}

#define HEA_ETHERNET_MAC_SRC_OFFSET 6
#define HEA_ETHERNET_MAC_DEST_OFFSET 0

static inline int _hea_mac_lport_get(const struct hea_cqe *current_cqe,
				     unsigned long buffer_packet,
				     struct hea_channel_cfg *lports[],
				     unsigned int mac_offset)
{
	unsigned int lport_nr;
	unsigned int HEA_LPORT_0;
	unsigned int HEA_LPORT_1;
	unsigned int HEA_LPORT_2;
	unsigned int HEA_LPORT_3;
	unsigned char *ptr_mac;
	union hea_mac_addr mac_address;

	/* get pointer to source MAC address in received Ethernet header */
	ptr_mac = ((unsigned char *)buffer_packet) + mac_offset;

	/**
	 * copy MAC address --> pointer access not possible due to size of MAC
	 * then correct 6 byte MAC into the correct position
	 **/
	mac_address._be64 = (*((union hea_mac_addr *)ptr_mac))._be64 >> 16;

	rhea_debug("Got source MAC Address in Manager: "
		   "%02x:%02x:%02x:%02x:%02x:%02x",
		   mac_address.sa.addr[0], mac_address.sa.addr[1],
		   mac_address.sa.addr[2], mac_address.sa.addr[3],
		   mac_address.sa.addr[4], mac_address.sa.addr[5]);

	/* find out what all have to say */
	HEA_LPORT_0 = (NULL == lports[0]) ? 0x0 :
		(lports[0]->lport.mac_address._be64 == mac_address._be64) * 1;

	HEA_LPORT_1 = (NULL == lports[1]) ? 0x0 :
		(lports[1]->lport.mac_address._be64 == mac_address._be64) * 2;

	HEA_LPORT_2 = (NULL == lports[2]) ? 0x0 :
		(lports[2]->lport.mac_address._be64 == mac_address._be64) * 3;

	HEA_LPORT_3 = (NULL == lports[3]) ? 0x0 :
		(lports[3]->lport.mac_address._be64 == mac_address._be64) * 4;

	/* get lport */
	lport_nr = HEA_LPORT_0 | HEA_LPORT_1 | HEA_LPORT_2 | HEA_LPORT_3;

	/**
	 * in case none was found we return -1,
	 * if one was found we correct the result
	 **/
	return lport_nr - 1;
}

static inline int _hea_src_mac_lport_get(const struct hea_cqe *current_cqe,
					 unsigned long buffer_packet,
					 struct hea_channel_cfg *lports[])
{
	return _hea_mac_lport_get(current_cqe, buffer_packet, lports,
				  HEA_ETHERNET_MAC_SRC_OFFSET);
}

static inline int _hea_dest_mac_lport_get(const struct hea_cqe *current_cqe,
					  unsigned long buffer_packet,
					  struct hea_channel_cfg *lports[])
{
	return _hea_mac_lport_get(current_cqe, buffer_packet, lports,
				  HEA_ETHERNET_MAC_DEST_OFFSET);
}

static inline int _hea_sq_wqe_set(struct snd_wqe_1 *snd_wqe1,
				  const struct hea_cqe *current_cqe,
				  unsigned long buffer_packet,
				  int tx_flag,
				  unsigned int lport_nr)
{
	int rc = 0;
	/* check if we have to resend it again */
	int recirculate = (HEA_TC_CTRL_RECIRCULATE &
			  (unsigned long long) tx_flag);
	unsigned long packet_size;

	if (NULL == current_cqe)
		return -EINVAL;

	packet_size = current_cqe->n_bytes_xfered;

	snd_wqe1->hdr.tx_control |= tx_flag;
	
	/* if we do not do this, we might create packets
	 * which are larger than 5018 bytes */
	if (1514 < packet_size)
		packet_size -= 4;

	snd_wqe1->hdr.tx_control |= HEA_TC_CTRL_DO_ETH_CRC;

	/* set lport in case the packet has to be wrapped to logical port */
	if (recirculate)
		rhea_debug("Write to lport: %u", lport_nr);

	snd_wqe1->hdr.wrap_tag = (recirculate) ? lport_nr : 0;

	snd_wqe1->hdr.wreq_id = (ulong)buffer_packet;
	snd_wqe1->hdr.tx_control |= HEA_TC_CTRL_TYPE_1;

	snd_wqe1->hdr.num_descriptors = 1;

	hea_wqe_address_set(&snd_wqe1->descriptors[0],
			    (ulong) buffer_packet, packet_size);

	return rc;

}

static inline int hea_wqe_to_out_send(struct rhea_mc_bc_manager_context *cntxt,
				      unsigned long buffer_packet,
				      struct hea_cqe *current_cqe)
{
	int tx_flag;
	unsigned int max_it = HEA_MAX_SQ_ITERATION;
	struct snd_wqe_1 *snd_wqe1;

	if (NULL == cntxt || NULL == current_cqe)
		return 0;

	/* wait until SQ is empty for 1 send */
	while (max_it-- && 2 > cntxt->rhea.sq.qe_count -
	       in_be64(&cntxt->rhea.qp_registers->qp_sqc) - 1)
		continue;

	if (unlikely(0 == max_it))
		return 0;

	snd_wqe1 = cntxt->rhea.sq.qe_current;

	/* set output flags */
	tx_flag = HEA_TC_CTRL_FORCE_OUT;

	/* set the wqe */
	_hea_sq_wqe_set(snd_wqe1, current_cqe, buffer_packet, tx_flag, 0);

	/* mark one less WQE in SQ */
	heaq_dec_count(&cntxt->rhea.sq);

	/* move to next SQ WQE */
	heaq_set_next_qe(&cntxt->rhea.sq);

	/* write in add register */
	iosync();
	out_be64(&cntxt->rhea.qp_registers->qp_sqa, 1);

	rhea_debug("Finished sending 1 packet to the outside world "
		   "from pport: %u", cntxt->pport_nr + 1);

	return 1;
}

static inline int hea_wqe_to_lports_send(struct rhea_mc_bc_manager_context
					 *cntxt, struct hea_cqe *current_cqe,
					 unsigned long buffer_packet,
					 unsigned lport_mask)
{
	int i;
	int tx_flag;
	unsigned count = 0;
	unsigned int max_it = HEA_MAX_SQ_ITERATION;
	struct snd_wqe_1 *wqe1;

	if (NULL == cntxt || NULL == current_cqe)
		return 0;

	if (0 == lport_mask)
		return 0;

	/* wait until SQ is empty for 1 send */
	while (max_it-- && 5 > cntxt->rhea.sq.qe_count -
	       in_be64(&cntxt->rhea.qp_registers->qp_sqc) - 1)
		continue;

	if (unlikely(0 == max_it))
		return 0;

	for (i = 0; i < HEA_MAX_PPORT_LPORT_COUNT; ++i) {

		/* check if this lport should go */
		if (0 == (lport_mask & (1 << i)))
			continue;

		/* send this packet */
		++count;

		wqe1 = cntxt->rhea.sq.qe_current;

		/* set output flags */
		tx_flag = HEA_TC_CTRL_RECIRCULATE;

		/* set the wqe */
		_hea_sq_wqe_set(wqe1, current_cqe, buffer_packet, tx_flag, i);

		rhea_debug("Send packet to logical port: %u on pport %u",
			   i, cntxt->pport_nr + 1);

		/* mark one less WQE in SQ */
		heaq_dec_count(&cntxt->rhea.sq);

		/* move to next SQ WQE */
		heaq_set_next_qe(&cntxt->rhea.sq);
	}

	/* write in add register */
	iosync();
	out_be64(&cntxt->rhea.qp_registers->qp_sqa, count);

	rhea_debug("Finished sending %u packet(s)", count);

	return count;
}

static inline int wire_2_hea_all(struct rhea_mc_bc_manager_context *cntxt,
				 struct hea_cqe *cqe_current,
				 unsigned long buffer_packet,
				 struct hea_channel_cfg *active_lports[])
{
	int rc = 0;
	unsigned int count;
	unsigned int lport_mask = 0;

	if (unlikely(NULL == cntxt || NULL == cqe_current ||
		     NULL == active_lports))
		return 0;

	rhea_debug("Got message from wire");

	/* create mask of available lports */
	lport_mask |= active_lports[0] ? 1 << 0 : 0;
	lport_mask |= active_lports[1] ? 1 << 1 : 0;
	lport_mask |= active_lports[2] ? 1 << 2 : 0;
	lport_mask |= active_lports[3] ? 1 << 3 : 0;

	rhea_debug("Logical port packet mask: %u for pport: %u",
		   lport_mask, cntxt->pport_nr + 1);
	rc = hea_wqe_to_lports_send(cntxt, cqe_current, buffer_packet,
				    lport_mask);
	if (unlikely(0 > rc)) {
		rhea_error("Was not able to send the BC packet to the RQ.");
		return rc;
	}

	/* find out how many packets were send */
	count = rc;

	return count;
}

static inline int hea_2_wire_mc_bc(struct rhea_mc_bc_manager_context *cntxt,
				   struct hea_cqe *cqe_current,
				   unsigned long buffer_packet,
				   struct hea_channel_cfg *active_lports[])
{
	int rc = 0;
	unsigned int count;
	int lport_source = 0;
	unsigned int lport_mask = 0;

	if (unlikely(NULL == cntxt || NULL == cqe_current ||
		     NULL == active_lports))
		return 0;

	rhea_debug("Got MC/BC message from HEA");

	lport_source = _hea_src_mac_lport_get(cqe_current, buffer_packet,
					      active_lports);
	if (unlikely(3 < lport_source || 0 > lport_source))
		goto fail_mac_detect;

	/* create mask of available lports */
	lport_mask |= (active_lports[0] && 0 != lport_source) ? 1 << 0 : 0;
	lport_mask |= (active_lports[1] && 1 != lport_source) ? 1 << 1 : 0;
	lport_mask |= (active_lports[2] && 2 != lport_source) ? 1 << 2 : 0;
	lport_mask |= (active_lports[3] && 3 != lport_source) ? 1 << 3 : 0;

	rhea_debug("Logical port packet mask: %u for pport: %u",
		   lport_mask, cntxt->pport_nr + 1);

	rc = hea_wqe_to_lports_send(cntxt, cqe_current, buffer_packet,
				    lport_mask);
	if (unlikely(0 > rc)) {
		rhea_error("Was not able to send the packet to the RQ");
		return rc;
	}

	/* find out how many packets were send */
	count = rc;

fail_mac_detect:

	rhea_debug("Send MC/BC packet to external world ");

	rc = hea_wqe_to_out_send(cntxt, buffer_packet, cqe_current);
	if (unlikely(0 > rc)) {
		rhea_error("Was not able to send the packet to the RQ.");
		return rc;
	}

	/* find out how many packets were send */
	count += 1;

	return count;
}

static inline int hea_2_wire_uc(struct rhea_mc_bc_manager_context *cntxt,
				struct hea_cqe *cqe_current,
				unsigned long buffer_packet,
				struct hea_channel_cfg *active_lports[])
{
	int rc = 0;
	unsigned int count;
	int lport_dest = 0;
	unsigned int lport_mask = 0;

	if (unlikely(NULL == cntxt || NULL == cqe_current ||
		     NULL == active_lports))
		return 0;

	rhea_debug("Got UC message from HEA");

	lport_dest = _hea_dest_mac_lport_get(cqe_current,
					     buffer_packet, active_lports);
	if (unlikely(3 < lport_dest || 0 > lport_dest))
		goto fail_mac_detect;

	/* create mask of available lports */
	lport_mask |= (active_lports[lport_dest]) ? 1 << lport_dest : 0;

	rhea_debug("Logical port packet mask: %u for pport: %u",
		   lport_mask, cntxt->pport_nr + 1);

	rc = hea_wqe_to_lports_send(cntxt, cqe_current, buffer_packet,
				    lport_mask);
	if (unlikely(0 > rc)) {
		rhea_error("Was not able to send the packet to the RQ");
		return rc;
	}

	/* find out how many packets were send */
	count = 1;

	return count;

fail_mac_detect:

	rhea_debug("Send UC packet to external world ");

	rhea_warning("Got UC packet from HEA which "
		     "does not belong to pport: %u", cntxt->pport_nr + 1);

	rc = hea_wqe_to_out_send(cntxt, buffer_packet, cqe_current);
	if (unlikely(0 > rc)) {
		rhea_error("Was not able to send the packet to the RQ.");
		return rc;
	}

	/* find out how many packets were send */
	count = 1;

	return count;
}

static inline int hea_handle_receive_bc(struct rhea_mc_bc_manager_context
					*cntxt, struct hea_cqe *cqe_current,
					unsigned long buffer_packet)
{
	int rc = 0;

	spin_lock(&cntxt->bc.lock);

	if (likely(HEA_MANAGER_ENABLED == cntxt->bc.state)) {

		/* check where packet is coming from */
		if (unlikely(hea_cqe_is_wrapped(cqe_current))) {

			rc = hea_2_wire_mc_bc(cntxt, cqe_current,
					      buffer_packet,
					      cntxt->bc.active_lports);
		} else {
			rc = wire_2_hea_all(cntxt, cqe_current, buffer_packet,
					    cntxt->bc.active_lports);
		}
	}

	spin_unlock(&cntxt->bc.lock);

	return rc;
}

static int hea_handle_receive_mc(struct rhea_mc_bc_manager_context *cntxt,
				 struct hea_cqe *cqe_current,
				 unsigned long buffer_packet)
{
	int rc = 0;

	spin_lock(&cntxt->mc.lock);

	if (likely(HEA_MANAGER_ENABLED == cntxt->mc.state)) {

		/* check where packet is coming from */
		if (unlikely(hea_cqe_is_wrapped(cqe_current))) {

			rc = hea_2_wire_mc_bc(cntxt, cqe_current,
					      buffer_packet,
					      cntxt->mc.active_lports);
		} else {
			rc = wire_2_hea_all(cntxt, cqe_current, buffer_packet,
					    cntxt->mc.active_lports);

		}
	}

	spin_unlock(&cntxt->mc.lock);

	return rc;
}

static int hea_handle_receive_uc(struct rhea_mc_bc_manager_context *cntxt,
				 struct hea_cqe *cqe_current,
				 unsigned long buffer_packet)
{
	int rc = 0;

	spin_lock(&cntxt->uc.lock);

	if (likely(HEA_MANAGER_ENABLED == cntxt->uc.state)) {

		/* check where packet is coming from */
		if (unlikely(hea_cqe_is_wrapped(cqe_current))) {

			rc = hea_2_wire_uc(cntxt, cqe_current, buffer_packet,
					   cntxt->uc.active_lports);
		} else {
			rc = wire_2_hea_all(cntxt, cqe_current, buffer_packet,
					    cntxt->uc.active_lports);
		}
	}

	spin_unlock(&cntxt->uc.lock);

	return rc;
}

static int hea_scan_recv_cq(struct rhea_mc_bc_manager_context *cntxt,
			    unsigned int budget)
{
	int rq_nr = 0;
	int discard;
	int received = 0;
	int num = 0;
	int num_not_send = 0;

	struct hea_q *cq;
	struct hea_cqe *cqe_current;

	if (unlikely(NULL == cntxt))
		return 0;

	/* use receive cq */
	cq = &cntxt->rhea.r_cq;

	if (unlikely(NULL == cq))
		return 0;

	cqe_current = cq->qe_current;

	while (likely(num < budget &&
		      hea_cqe_is_valid(cqe_current, cq->q_toggle_bit))) {

		struct rcv_wqe_normal *rwqe;
		unsigned long buffer_current;
		unsigned long buffer_new;
		struct rhea_mem rq_buff;
		unsigned int rq_index = cntxt->mem_buf_index;
		unsigned int rq_slot = cntxt->mem_buf_slot;

		/* check which rq was used */
		rq_nr = hea_cqe_rq_used(cqe_current);

		BUG_ON(1 != rq_nr);
		BUG_ON(hea_cqe_is_transmit(cqe_current));

		/* RQ WQE */
		rwqe = (struct rcv_wqe_normal *) cqe_current->wr_id;

		/* get correct buffer */
		rq_buff = cntxt->rq_mem[rq_index];

		/* get new slot */
		buffer_new = ((unsigned long)rq_buff.va) +
			cntxt->rq_buf_slot_size * rq_slot;

		/* swap the two pointers in the RQ WQE */
		_hea_rq_wqe_pointer_swap(rwqe, &buffer_current, buffer_new);

		discard = 0;

		rhea_debug("MC/BC manager got one CQE for RQ %d - %d bytes",
			   rq_nr, cqe_current->n_bytes_xfered);

		/* correct value */
		--rq_nr;

		/* check if this is an input error */
		if (cqe_current->status & (HEA_CQE_STATUS_BAD_CRC_BIT |
					   HEA_CQE_STATUS_LENGTH_ERROR_BIT |
					   HEA_CQE_STATUS_BAD_FRAME_BIT |
					   HEA_CQE_STATUS_ERRORS_BIT)) {
			rhea_debug("input error %x",
				   cqe_current->
				   status & HEA_CQE_ANY_ERROR_BIT);

			discard = 1;
		}

		/* remove packet from input queue */
		if (unlikely(discard)) {
			/* free up rq wqe */
			rhea_debug("discarding");
			++num_not_send;
		} else {
			int send_packets = 0;
			received++;

			/* find out what happened */
			switch (hea_get_u8_bits(cqe_current->flags, 0, 1)) {
				/* UC */
			case 0x0:
				send_packets =
					hea_handle_receive_uc(cntxt,
							      cqe_current,
							      buffer_current);
				break;

				/* MC */
			case 0x1:
				send_packets =
					hea_handle_receive_mc(cntxt,
							      cqe_current,
							      buffer_current);
				break;

				/* BC */
			case 0x3:
				send_packets =
					hea_handle_receive_bc(cntxt,
							      cqe_current,
							      buffer_current);
				break;

			default:
				rhea_debug("Undefined channel type");
				break;
			}

			/* in case no packet was send, recycle rq wqe */
			if (unlikely(0 == send_packets)) {
				rhea_debug
					("Did not send a packet on pport: %u",
					 cntxt->pport_nr + 1);
				++num_not_send;
			} else
				/* we sent a packet
				 * --> rest of wqes freed later */
				num_not_send = 0;

		}

		/* move to next entry */
		heaq_set_next_qe(&cntxt->rhea.r_cq);
		cqe_current = cntxt->rhea.r_cq.qe_current;

		/* count number of received packets */
		++num;

		/* get next slot */
		cntxt->mem_buf_slot = (cntxt->mem_buf_slot + 1) %
			RHEA_MANAGER_RQ_WQES;

		/* in case we have gone around --> change index */
		if (unlikely(0 == cntxt->mem_buf_slot))
			cntxt->mem_buf_index = (cntxt->mem_buf_index + 1) %
				HEA_MAX_BC_MC_RECV_Q_BUFFER;

		/* write to register to free CQ and RQ */
		iosync();
		out_be64(&cntxt->rhea.r_cq_registers->cq_feca, 1);
		out_be64(&cntxt->rhea.qp_registers->qp_rq1a, 1);
	}

	return received;
}

static void irq_work_execute(struct work_struct *work)
{
	int repeat = 10;
	int new_packet;
	unsigned int budget;
	struct hea_cqe *cqe_current;
	struct hea_q *cq;
	struct delayed_work *delayed_work;
	struct rhea_mc_bc_manager_context *cntxt;

	delayed_work = to_delayed_work(work);

	cntxt = container_of(delayed_work,
			     struct rhea_mc_bc_manager_context, irq_work);

	rhea_debug("Waiting before lock");

	hea_mc_bc_state_acquire(cntxt, HEA_MANAGER_PROCESSING);

	if (unlikely(HEA_MANAGER_ENABLED != cntxt->state)) {
		hea_mc_bc_state_release(cntxt);
		rhea_warning("Manager for pport: %u is not enabled.",
			     cntxt->pport_nr + 1);
		return;
	}

	rhea_debug("Entering the irq handler");

	BUG_ON(0 == cntxt->rhea.sq.qe_count);
	BUG_ON(0 == cntxt->rhea.rq[0].qe_count);

scan_again:

	/* set budget */
	budget = HEA_MANAGER_RECEIVE_BUDGET;

	rhea_debug("In this queue with budget: %u", budget);

	/* check for errors on the EQ */
	hea_scan_event_queue(cntxt);

	/* start working on the cq */
	budget -= hea_scan_recv_cq(cntxt, budget);

	/* get completion queue  and current cqe */
	cq = &cntxt->rhea.r_cq;
	cqe_current = cq->qe_current;

	/* check if a new packet has arrived */
	new_packet = hea_cqe_is_valid(cqe_current, cq->q_toggle_bit);

	/* say likely, since here we need most of the performance */
	if (likely(0 < repeat && (0 == budget || new_packet))) {

		/* make sure we only loop for a limited amount of time */
		--repeat;

		goto scan_again;
	}

	rhea_debug("Enable IRQ again");

	iosync();
	/* all events have been looked at */
	out_be64(&cntxt->rhea.r_cq_registers->cq_ep, 0);
	/* enable IRQ again */
	out_be64(&cntxt->rhea.r_cq_registers->cq_n1, ((u64) 1ULL) << 63);

	hea_mc_bc_state_release(cntxt);

	rhea_debug("Leaving the irq handler");
}

irqreturn_t hea_irq_bc_mc_handler(int irq_nr, void *param)
{
	unsigned int delay;
	struct rhea_mc_bc_manager_context *cntxt =
		(struct rhea_mc_bc_manager_context *)param;

	if (unlikely(irq_nr != cntxt->irq_nr)) {

		rhea_debug("Got incorrect IRQ: %u for manager on port: %u",
			   irq_nr, cntxt->pport_nr + 1);
		/* not our interrupt */
		return IRQ_NONE;
	}

	rhea_debug("Call MC/BC handler: with ID: %u and pport: %u for IRQ: %u",
		   cntxt->rhea.rhea_id, cntxt->pport_nr + 1, irq_nr);

	/* send out a new job */
	delay = hea_mc_bc_delay_get(cntxt);
	while (0 == queue_delayed_work
		(cntxt->irq_workqueue, &cntxt->irq_work, delay))
		continue;

	return IRQ_HANDLED;
}

/* Returns 0 if we couldn't fill entirely (OOM). */
static int _hea_fill_recvq(struct hea_q *rq, unsigned rq_nr,
			   void **buffer_array,
			   unsigned buffer_count,
			   unsigned buffer_element_size,
			   struct rhea_qpte *qp_registers)
{
	int i;
	struct rcv_wqe_normal *rwqe;

	if (NULL == rq || NULL == qp_registers)
		return -EINVAL;

	if (0 == buffer_element_size) {
		rhea_error("Buffer size is not valid");
		return -EINVAL;
	}

	if (0 == buffer_count) {
		rhea_error("Buffer count is 0");
		return -EINVAL;
	}

	for (i = 0; i < buffer_count; ++i) {
		if (NULL == buffer_array[i]) {
			rhea_error("Data pointer is not valid");
			return -EINVAL;
		}

		/* enqueue receive WQE */
		rwqe = rq->qe_current;

		/* fill rq wqe will parameters */
		rwqe->wreq_id = (ulong)rwqe;
		rwqe->num_data_segs = 1;

		hea_wqe_address_set(&rwqe->descriptors[0],
				    (ulong) buffer_array[i],
				    buffer_element_size);

		/* goto next element */
		heaq_set_next_qe(rq);
		heaq_dec_count(rq);
	}

	if (i > 0) {
		iosync();
		/* push the ADD register */
		out_be64(&qp_registers->qp_rq1a, i - 1);
		heaq_set_count(rq, 0);
	}

	return 0;
}

static inline struct rhea_mc_bc_manager_context
	*_rhea_mc_mc_manager_get_manager(unsigned int pport_nr)
{
	struct rhea_mc_bc_manager_context *mng_ctx;

	if (!RHEA_VALID_INSTANCE_CHECK(s_mc_bc_manager_context, pport_nr))
		return NULL;

	/* manager context for this physical port */
	mng_ctx = s_mc_bc_manager_context[pport_nr];

	return mng_ctx;
}

static inline int
_rhea_mc_bc_manager_lport_enable(struct rhea_mc_bc_manager_context *mng_ctx,
				 enum hea_channel_type register_type,
				 unsigned int lport_nr)
{
	int rc = 0;

	if (unlikely(NULL == mng_ctx) || 3 < lport_nr)
		return -EINVAL;

	switch (register_type) {
	case HEA_UC_PORT:

		spin_lock(&mng_ctx->uc.lock);

		mng_ctx->uc.active_lports[lport_nr] =
			mng_ctx->uc.registered_lports[lport_nr];

		spin_unlock(&mng_ctx->uc.lock);

		break;

	case HEA_MC_PORT:

		spin_lock(&mng_ctx->mc.lock);

		mng_ctx->mc.active_lports[lport_nr] =
			mng_ctx->mc.registered_lports[lport_nr];

		spin_unlock(&mng_ctx->mc.lock);

		break;

	case HEA_BC_PORT:

		spin_lock(&mng_ctx->bc.lock);

		mng_ctx->bc.active_lports[lport_nr] =
			mng_ctx->bc.registered_lports[lport_nr];

		spin_unlock(&mng_ctx->bc.lock);

		break;

	default:
		rhea_error("lports are not supported");
		break;

	}

	return rc;
}

int rhea_mc_bc_manager_lport_enable(unsigned int pport_nr,
				    enum hea_channel_type register_type,
				    unsigned int lport_nr)
{
	int rc = 0;
	struct rhea_mc_bc_manager_context *mng_ctx;

	mng_ctx = _rhea_mc_mc_manager_get_manager(pport_nr);
	if (NULL == mng_ctx)
		return -EINVAL;

	rc = _rhea_mc_bc_manager_lport_enable(mng_ctx, register_type,
					      lport_nr);

	return rc;
}

static inline int
_rhea_mc_bc_manager_lport_disable(struct rhea_mc_bc_manager_context *mng_ctx,
				  enum hea_channel_type register_type,
				  unsigned int lport_nr)
{
	int rc = 0;

	if (unlikely(NULL == mng_ctx) || 3 < lport_nr)
		return -EINVAL;

	switch (register_type) {
	case HEA_UC_PORT:

		spin_lock(&mng_ctx->uc.lock);

		mng_ctx->uc.active_lports[lport_nr] = NULL;

		spin_unlock(&mng_ctx->uc.lock);

		break;

	case HEA_MC_PORT:

		spin_lock(&mng_ctx->mc.lock);

		mng_ctx->mc.active_lports[lport_nr] = NULL;

		spin_unlock(&mng_ctx->mc.lock);

		break;

	case HEA_BC_PORT:

		spin_lock(&mng_ctx->bc.lock);

		mng_ctx->bc.active_lports[lport_nr] = NULL;

		spin_unlock(&mng_ctx->bc.lock);

		break;

	default:
		rhea_error("lports are not supported");
		break;
	}

	return rc;
}

int rhea_mc_bc_manager_lport_disable(unsigned int pport_nr,
				     enum hea_channel_type register_type,
				     unsigned int lport_nr)
{
	int rc = 0;
	struct rhea_mc_bc_manager_context *mng_ctx;

	mng_ctx = _rhea_mc_mc_manager_get_manager(pport_nr);
	if (NULL == mng_ctx)
		return -EINVAL;

	rc = _rhea_mc_bc_manager_lport_disable(mng_ctx, register_type,
					       lport_nr);

	return rc;
}

int _rhea_mc_bc_manager_lport_register(struct rhea_mc_bc_manager_context
				       *mng_ctx, unsigned int pport_nr,
				       enum hea_channel_type
				       register_type,
				       struct hea_channel_cfg *lport_cfg)
{
	int rc = 0;
	unsigned int lport_nr;

	if (NULL == mng_ctx || NULL == lport_cfg ||
	    !is_hea_lport(lport_cfg->type) || 3 < lport_cfg->lport.lport_nr)
		return -EINVAL;

	lport_nr = lport_cfg->lport.lport_nr;

	switch (register_type) {
	case HEA_UC_PORT:

		spin_lock(&mng_ctx->uc.lock);

		if (NULL ==
		    mng_ctx->uc.registered_lports[lport_nr]) {
			mng_ctx->uc.registered_lports[lport_nr] = lport_cfg;
			hea_atomic_inc32(&mng_ctx->uc.instance_count);
		} else
			rc = -EINVAL;

		spin_unlock(&mng_ctx->uc.lock);

		break;

	case HEA_MC_PORT:

		spin_lock(&mng_ctx->mc.lock);

		if (NULL ==
		    mng_ctx->mc.registered_lports[lport_nr]) {
			mng_ctx->mc.registered_lports[lport_nr] = lport_cfg;
			hea_atomic_inc32(&mng_ctx->mc.instance_count);
		} else
			rc = -EINVAL;

		spin_unlock(&mng_ctx->mc.lock);

		break;

	case HEA_BC_PORT:

		spin_lock(&mng_ctx->bc.lock);

		if (NULL ==
		    mng_ctx->bc.registered_lports[lport_nr]) {
			mng_ctx->bc.registered_lports[lport_nr] = lport_cfg;
			hea_atomic_inc32(&mng_ctx->bc.instance_count);
		} else
			rc = -EINVAL;

		spin_unlock(&mng_ctx->bc.lock);

		break;

	default:
		rhea_error("No support for LPORTs");
		rc = -EINVAL;
	}

	return rc;
}


int _rhea_mc_bc_manager_start(struct rhea_mc_bc_manager_context *mng_ctx,
			      enum hea_channel_type register_type)
{
	int rc = 0;
	unsigned channel_id;

	if (NULL == mng_ctx)
		return -EINVAL;

	hea_mc_bc_state_acquire(mng_ctx, HEA_MANAGER_SETUP);

	/* check if manager is on, if not turn it on */
	if (HEA_MANAGER_ENABLED != mng_ctx->state) {
		rc = _rhea_mc_bc_manager_create(mng_ctx, register_type);
		if (rc) {
			hea_mc_bc_state_release(mng_ctx);
			rhea_error("Was not able to start MC/BC/UC manager");
			return rc;
		}

		mng_ctx->state = HEA_MANAGER_ENABLED;
	} else {

		rc = _rhea_mc_bc_manager_channel_create(mng_ctx, register_type,
							&channel_id);
		if (rc) {
			hea_mc_bc_state_release(mng_ctx);
			rhea_error("Was not able to alloc channel: %u and "
				   "got code: %u",
				 register_type, rc);

			return rc;
		}

		rc = _rhea_mc_bc_manager_channel_start(mng_ctx, register_type);
		if (rc) {
			_rhea_mc_bc_manager_channel_destroy(mng_ctx,
							    register_type);
			hea_mc_bc_state_release(mng_ctx);
			rhea_error("Was not able to start channel: %u and "
				   "got code: %u",
				 register_type, rc);
			return rc;
		}
	}

	hea_mc_bc_state_release(mng_ctx);

	return rc;
}

int _rhea_mc_bc_manager_stop(struct rhea_mc_bc_manager_context *mng_ctx,
			     enum hea_channel_type register_type)
{
	int rc = 0;

	if (NULL == mng_ctx)
		return -EINVAL;

	rc = _rhea_mc_bc_manager_channel_stop(mng_ctx, register_type);
	if (rc)
		rhea_error("Was not able to stop channel: %u", register_type);

	rc = _rhea_mc_bc_manager_channel_destroy(mng_ctx, register_type);
	if (rc) {
		rhea_error("Was not able to destroy channel: %u",
			   register_type);
	}

	hea_mc_bc_state_acquire(mng_ctx, HEA_MANAGER_SETUP);

	/* in case everybody has left, turn manager off */
	if (HEA_MANAGER_ENABLED != mng_ctx->bc.state &&
	    HEA_MANAGER_ENABLED != mng_ctx->mc.state &&
	    HEA_MANAGER_ENABLED != mng_ctx->uc.state &&
	    HEA_MANAGER_ENABLED == mng_ctx->state) {
		rc = _rhea_mc_bc_manager_destroy(mng_ctx);
		if (rc) {
			rhea_error("Was not able to stop MC/BC/UC manager");
			rc = -EINVAL;
		}

		mng_ctx->state = HEA_MANAGER_DESTROYED;
	}

	hea_mc_bc_state_release(mng_ctx);

	rhea_info("Successfully shutdown the UC/MC/BC manager!");

	return rc;
}

int _rhea_mc_bc_manager_lport_unregister(unsigned int pport_nr,
					 enum hea_channel_type
					 register_type,
					 struct hea_channel_cfg *lport_cfg)
{
	int rc = 0;
	unsigned int lport_nr;
	struct rhea_mc_bc_manager_context *mng_ctx;

	if (NULL == lport_cfg || !is_hea_lport(lport_cfg->type) ||
	    3 < lport_cfg->lport.lport_nr)
		return -EINVAL;

	mng_ctx = _rhea_mc_mc_manager_get_manager(pport_nr);
	if (NULL == mng_ctx)
		return -EINVAL;

	lport_nr =  lport_cfg->lport.lport_nr;

	/* make sure it is disabled first */
	_rhea_mc_bc_manager_lport_disable(mng_ctx, register_type, lport_nr);

	switch (register_type) {
	case HEA_UC_PORT:

		spin_lock(&mng_ctx->uc.lock);

		if (lport_cfg == mng_ctx->uc.registered_lports[lport_nr]) {
			mng_ctx->uc.registered_lports[lport_nr] = NULL;
			hea_atomic_dec32(&mng_ctx->uc.instance_count);
			rhea_debug("Unregister UC: %u",
				   hea_atomic_get32(&mng_ctx->uc.
						    instance_count));
		} else
			rc = -EINVAL;

		spin_unlock(&mng_ctx->uc.lock);

		break;

	case HEA_MC_PORT:

		spin_lock(&mng_ctx->mc.lock);

		if (lport_cfg == mng_ctx->mc.registered_lports[lport_nr]) {
			mng_ctx->mc.registered_lports[lport_nr] = NULL;
			hea_atomic_dec32(&mng_ctx->mc.instance_count);
			rhea_debug("Unregister MC: %u",
				   hea_atomic_get32(&mng_ctx->mc.
						    instance_count));
		} else
			rc = -EINVAL;

		spin_unlock(&mng_ctx->mc.lock);

		break;

	case HEA_BC_PORT:

		spin_lock(&mng_ctx->bc.lock);

		if (lport_cfg == mng_ctx->bc.registered_lports[lport_nr]) {
			mng_ctx->bc.registered_lports[lport_nr] = NULL;
			hea_atomic_dec32(&mng_ctx->bc.instance_count);

			rhea_debug("Unregister BC: %u",
				   hea_atomic_get32(&mng_ctx->bc.
						    instance_count));
		} else
			rc = -EINVAL;

		spin_unlock(&mng_ctx->bc.lock);

		break;

	default:
		rhea_error("No support for LPORTs");
		rc = -EINVAL;
	}

	return rc;
}


int rhea_mc_bc_manager_lport_register(unsigned int pport_nr,
				      enum hea_channel_type
				      register_type,
				      struct hea_channel_cfg *lport_cfg)
{
	int rc = 0;
	struct rhea_mc_bc_manager_context *mng_ctx;

	if (NULL == lport_cfg || !is_hea_lport(lport_cfg->type))
		return -EINVAL;

	mng_ctx = _rhea_mc_mc_manager_get_manager(pport_nr);
	if (NULL == mng_ctx) {
		rhea_error("Manager is not registered");
		return -EINVAL;
	}

	rc = _rhea_mc_bc_manager_lport_register(mng_ctx, pport_nr,
						register_type, lport_cfg);
	if (rc) {
		rhea_error("Was not able to register the lport");
		goto fail_rhea_mc_bc_manager_channel_register;
	}

	rc = _rhea_mc_bc_manager_start(mng_ctx, register_type);
	if (rc) {
		rhea_error("Was not able to start channel: %u", register_type);
		goto fail_rhea_mc_bc_manager_channel_start;
	}

	return 0;

fail_rhea_mc_bc_manager_channel_start:
	_rhea_mc_bc_manager_stop(mng_ctx, register_type);
fail_rhea_mc_bc_manager_channel_register:
	_rhea_mc_bc_manager_lport_unregister(pport_nr, register_type,
					     lport_cfg);
	return rc;
}

int rhea_mc_bc_manager_lport_unregister(unsigned int pport_nr,
					enum hea_channel_type
					register_type,
					struct hea_channel_cfg *lport_cfg)
{
	int rc = 0;
	struct rhea_mc_bc_manager_context *mng_ctx;

	if (NULL == lport_cfg || !is_hea_lport(lport_cfg->type))
		return -EINVAL;

	mng_ctx = _rhea_mc_mc_manager_get_manager(pport_nr);
	if (NULL == mng_ctx)
		return -EINVAL;

	rc = _rhea_mc_bc_manager_lport_unregister(pport_nr, register_type,
						  lport_cfg);
	if (rc)
		goto fail_unregister;

	return rc;

fail_unregister:

	return rc;
}

static int _rhea_mc_bc_manager_channel_create(struct rhea_mc_bc_manager_context
					      *mng_ctx,
					      enum hea_channel_type
					      registered_type,
					      unsigned *channel_id)
{
	int rc = 0;
	struct hea_channel_context channel_context;

	if (NULL == mng_ctx) {
		rhea_error("Broadcast manager is not initialised");
		return -EINVAL;
	}

	memset(&channel_context, 0, sizeof(channel_context));

	switch (registered_type) {
	case HEA_UC_PORT:

		spin_lock(&mng_ctx->uc.lock);

		if (HEA_MANAGER_DESTROYED == mng_ctx->uc.state ||
		    HEA_MANAGER_NO_STATE == mng_ctx->uc.state) {

			rhea_info("Create UC channel: %u", mng_ctx->uc.state);

			channel_context.cfg.type = HEA_UC_PORT;
			channel_context.cfg.pport_nr = mng_ctx->pport_nr;
			channel_context.cfg.uc.channel_usuage =
				HEA_DEFAULT_CHANNEL_MANAGER;
			rc = rhea_channel_alloc(mng_ctx->rhea.rhea_id,
						&mng_ctx->uc.channel_id,
						&channel_context);
			if (rc) {
				rhea_error
					("UC rhea_channel_alloc failed 0: %i",
					 rc);
				goto fail_rhea_channel_alloc_uc;
			}

			if (channel_id)
				*channel_id = mng_ctx->uc.channel_id;

			mng_ctx->uc.state = HEA_MANAGER_INIT;

			rhea_debug("Created UC channel");
		}

fail_rhea_channel_alloc_uc:

		spin_unlock(&mng_ctx->uc.lock);

		break;

	case HEA_MC_PORT:

		spin_lock(&mng_ctx->mc.lock);

		if (HEA_MANAGER_DESTROYED == mng_ctx->mc.state ||
		    HEA_MANAGER_NO_STATE == mng_ctx->mc.state) {

			rhea_info("Create MC channel: %u", mng_ctx->mc.state);

			channel_context.cfg.type = HEA_MC_PORT;
			channel_context.cfg.pport_nr = mng_ctx->pport_nr;
			channel_context.cfg.mc.channel_usuage =
				HEA_DEFAULT_CHANNEL_MANAGER;
			rc = rhea_channel_alloc(mng_ctx->rhea.rhea_id,
						&mng_ctx->mc.channel_id,
						&channel_context);
			if (rc) {
				rhea_error
					("MC rhea_channel_alloc failed 0: %i",
					 rc);
				goto fail_rhea_channel_alloc_mc;
			}

			if (channel_id)
				*channel_id = mng_ctx->mc.channel_id;

			mng_ctx->mc.state = HEA_MANAGER_INIT;

			rhea_debug("Created MC channel");
		}

fail_rhea_channel_alloc_mc:

		spin_unlock(&mng_ctx->mc.lock);

		break;

	case HEA_BC_PORT:

		spin_lock(&mng_ctx->bc.lock);

		if (HEA_MANAGER_DESTROYED == mng_ctx->bc.state ||
		    HEA_MANAGER_NO_STATE == mng_ctx->bc.state) {
			rhea_info("Create BC channel: %u", mng_ctx->bc.state);

			channel_context.cfg.type = HEA_BC_PORT;
			channel_context.cfg.pport_nr = mng_ctx->pport_nr;
			channel_context.cfg.bc.channel_usuage =
				HEA_DEFAULT_CHANNEL_MANAGER;
			rc = rhea_channel_alloc(mng_ctx->rhea.rhea_id,
						&mng_ctx->bc.channel_id,
						&channel_context);
			if (rc) {
				rhea_error
					("BC rhea_channel_alloc failed 0: %i",
					 rc);
				goto fail_rhea_channel_alloc_bc;
			}

			if (channel_id)
				*channel_id = mng_ctx->bc.channel_id;

			mng_ctx->bc.state = HEA_MANAGER_INIT;

			rhea_debug("Created BC channel");
		}

fail_rhea_channel_alloc_bc:

		spin_unlock(&mng_ctx->bc.lock);

		break;

	default:

		rhea_error("No logical port supported");
		rc = -EINVAL;
	}

	return rc;
}

static int _rhea_mc_bc_manager_channel_destroy(struct
					       rhea_mc_bc_manager_context
					       *mng_ctx,
					       enum hea_channel_type
					       registered_type)
{
	unsigned int i;
	int rc = 0;

	if (unlikely(NULL == mng_ctx)) {
		rhea_error("Broadcast manager is not initialised");
		return -EINVAL;
	}

	switch (registered_type) {
	case HEA_UC_PORT:

		spin_lock(&mng_ctx->uc.lock);

		if (0 == hea_atomic_get32(&mng_ctx->uc.instance_count) &&
		    HEA_MANAGER_DISABLED == mng_ctx->uc.state) {
			for (i = 0; i < HEA_MAX_PPORT_LPORT_COUNT; ++i) {
				if (mng_ctx->uc.registered_lports[i]) {
					_rhea_mc_bc_manager_lport_unregister
						(mng_ctx->pport_nr, HEA_UC_PORT,
						 mng_ctx->uc.
						 registered_lports[i]);
				}
			}

			rhea_info("Destroy UC Manager");
			rc = rhea_channel_free(mng_ctx->rhea.rhea_id,
					       mng_ctx->uc.channel_id);
			if (rc) {
				rhea_error("UC rhea_channel_free failed: %i",
					   rc);
			}

			memset(&mng_ctx->uc.registered_lports, 0,
			       sizeof(mng_ctx->uc.registered_lports));

			memset(&mng_ctx->uc.active_lports, 0,
			       sizeof(mng_ctx->uc.active_lports));

			mng_ctx->uc.state = HEA_MANAGER_DESTROYED;
			mng_ctx->uc.channel_id = 0;
		}

		spin_unlock(&mng_ctx->uc.lock);

		break;

	case HEA_MC_PORT:

		spin_lock(&mng_ctx->mc.lock);

		if (0 == hea_atomic_get32(&mng_ctx->mc.instance_count) &&
		    HEA_MANAGER_DISABLED == mng_ctx->mc.state) {
			rhea_info("Destroy MC Manager");

			for (i = 0; i < HEA_MAX_PPORT_LPORT_COUNT; ++i) {
				if (mng_ctx->mc.registered_lports[i]) {
					_rhea_mc_bc_manager_lport_unregister
						(mng_ctx->pport_nr, HEA_MC_PORT,
						 mng_ctx->mc.
						 registered_lports[i]);
				}
			}

			rc = rhea_channel_free(mng_ctx->rhea.rhea_id,
					       mng_ctx->mc.channel_id);
			if (rc) {
				rhea_error("MC rhea_channel_free failed: %i",
					   rc);
			}

			memset(&mng_ctx->mc.registered_lports, 0,
			       sizeof(mng_ctx->mc.registered_lports));

			memset(&mng_ctx->mc.active_lports, 0,
			       sizeof(mng_ctx->mc.active_lports));

			mng_ctx->mc.state = HEA_MANAGER_DESTROYED;
			mng_ctx->mc.channel_id = 0;
		}

		spin_unlock(&mng_ctx->mc.lock);

		break;

	case HEA_BC_PORT:

		spin_lock(&mng_ctx->bc.lock);

		if (0 == hea_atomic_get32(&mng_ctx->bc.instance_count) &&
		    HEA_MANAGER_DISABLED == mng_ctx->bc.state) {
			rhea_info("Destroy BC Manager");

			for (i = 0; i < HEA_MAX_PPORT_LPORT_COUNT; ++i) {
				if (mng_ctx->bc.registered_lports[i]) {
					_rhea_mc_bc_manager_lport_unregister
						(mng_ctx->pport_nr, HEA_BC_PORT,
						 mng_ctx->bc.
						 registered_lports[i]);
				}
			}

			rc = rhea_channel_free(mng_ctx->rhea.rhea_id,
					       mng_ctx->bc.channel_id);
			if (rc) {
				rhea_error("BC rhea_channel_free failed: %i",
					   rc);
			}

			memset(&mng_ctx->bc.registered_lports, 0,
			       sizeof(mng_ctx->bc.registered_lports));

			memset(&mng_ctx->bc.active_lports, 0,
			       sizeof(mng_ctx->bc.active_lports));

			mng_ctx->bc.state = HEA_MANAGER_DESTROYED;
			mng_ctx->bc.channel_id = 0;
		}

		spin_unlock(&mng_ctx->bc.lock);

		break;

	default:

		rhea_error("No logical port supported");
		rc = -EINVAL;
	}

	return rc;
}

static int _rhea_mc_bc_manager_channel_start(struct rhea_mc_bc_manager_context
					     *mng_ctx,
					     enum hea_channel_type
					     registered_type)
{
	int rc = 0;

	if (unlikely(NULL == mng_ctx))
		return -EINVAL;

	switch (registered_type) {
	case HEA_UC_PORT:

		spin_lock(&mng_ctx->uc.lock);

		if (HEA_MANAGER_INIT == mng_ctx->uc.state) {

			rc = rhea_channel_qpn_share(mng_ctx->rhea.rhea_id,
						    mng_ctx->uc.channel_id,
						    mng_ctx->rhea.
						    qpn_channel_id);
			if (rc) {
				spin_unlock(&mng_ctx->uc.lock);
				rhea_error("rhea_channel_qpn_share failed: %i",
					   rc);
				break;
			}

			mng_ctx->uc.state = HEA_MANAGER_ENABLED;

			rhea_info("Started UC Manager");
		}

		spin_unlock(&mng_ctx->uc.lock);

		break;

	case HEA_MC_PORT:

		spin_lock(&mng_ctx->mc.lock);

		if (HEA_MANAGER_INIT == mng_ctx->mc.state) {

			rc = rhea_channel_qpn_share(mng_ctx->rhea.rhea_id,
						    mng_ctx->mc.channel_id,
						    mng_ctx->rhea.
						    qpn_channel_id);
			if (rc) {
				spin_unlock(&mng_ctx->mc.lock);
				rhea_error("rhea_channel_qpn_share "
					   "failed: %i", rc);
				break;
			}

			mng_ctx->mc.state = HEA_MANAGER_ENABLED;

			rhea_info("Started MC Manager");
		}

		spin_unlock(&mng_ctx->mc.lock);

		break;

	case HEA_BC_PORT:

		spin_unlock(&mng_ctx->bc.lock);

		if (HEA_MANAGER_INIT == mng_ctx->bc.state) {

			rc = rhea_channel_qpn_share(mng_ctx->rhea.rhea_id,
						    mng_ctx->bc.channel_id,
						    mng_ctx->rhea.
						    qpn_channel_id);
			if (rc) {
				spin_unlock(&mng_ctx->bc.lock);
				rhea_error("rhea_channel_qpn_share "
					   "failed: %i", rc);
				break;
			}

			mng_ctx->bc.state = HEA_MANAGER_ENABLED;

			rhea_info("Started BC Manager");
		}

		spin_unlock(&mng_ctx->bc.lock);

		break;

	default:

		rhea_error("No logical port supported");
		rc = -EINVAL;
	}

	return rc;
}

static int
_rhea_mc_bc_manager_channel_stop(struct rhea_mc_bc_manager_context *mng_ctx,
				 enum hea_channel_type registered_type)
{
	int rc = 0;

	if (unlikely(NULL == mng_ctx)) {
		rhea_error("Broadcast manager is not initialised");
		return -EINVAL;
	}

	switch (registered_type) {
	case HEA_UC_PORT:

		spin_lock(&mng_ctx->uc.lock);

		if (0 == hea_atomic_get32(&mng_ctx->uc.instance_count) &&
		    HEA_MANAGER_ENABLED == mng_ctx->uc.state) {
			mng_ctx->uc.state = HEA_MANAGER_DISABLED;

			if (mng_ctx->rhea.qpn_channel_id ==
			    mng_ctx->uc.channel_id) {
				if (HEA_MANAGER_ENABLED == mng_ctx->mc.state)
					mng_ctx->rhea.qpn_channel_id =
						mng_ctx->mc.channel_id;
				else
					mng_ctx->rhea.qpn_channel_id =
						mng_ctx->bc.channel_id;
			}

			rhea_info("Disabled UC manager");
		}

		spin_unlock(&mng_ctx->uc.lock);

		break;

	case HEA_MC_PORT:

		spin_lock(&mng_ctx->mc.lock);

		if (0 == hea_atomic_get32(&mng_ctx->mc.instance_count) &&
		    HEA_MANAGER_ENABLED == mng_ctx->mc.state) {

			mng_ctx->mc.state = HEA_MANAGER_DISABLED;

			if (mng_ctx->rhea.qpn_channel_id ==
			    mng_ctx->mc.channel_id) {
				if (HEA_MANAGER_ENABLED == mng_ctx->uc.state)
					mng_ctx->rhea.qpn_channel_id =
						mng_ctx->uc.channel_id;
				else
					mng_ctx->rhea.qpn_channel_id =
						mng_ctx->bc.channel_id;
			}

			rhea_info("Disabled MC manager");
		}

		spin_unlock(&mng_ctx->mc.lock);

		break;

	case HEA_BC_PORT:

		spin_lock(&mng_ctx->bc.lock);

		if (0 == hea_atomic_get32(&mng_ctx->bc.instance_count) &&
		    HEA_MANAGER_ENABLED == mng_ctx->bc.state) {

			mng_ctx->bc.state = HEA_MANAGER_DISABLED;

			if (mng_ctx->rhea.qpn_channel_id ==
			    mng_ctx->bc.channel_id) {
				if (HEA_MANAGER_ENABLED == mng_ctx->uc.state)
					mng_ctx->rhea.qpn_channel_id =
						mng_ctx->uc.channel_id;
				else
					mng_ctx->rhea.qpn_channel_id =
						mng_ctx->mc.channel_id;
			}

			rhea_info("Disabled BC manager");
		}

		spin_unlock(&mng_ctx->bc.lock);

		break;

	default:

		rhea_error("No logical port supported");
		rc = -EINVAL;
	}

	return rc;
}

static int _rhea_mc_bc_manager_create(struct rhea_mc_bc_manager_context
				      *mng_ctx,
				      enum hea_channel_type registered_type)
{
	int rc = 0;
	int i;
	int j;

	unsigned map_size;
	unsigned long long irq_nr;

	unsigned channel_id = 0;

	struct hea_eq_context context_eq;
	struct hea_cq_context context_r_cq;
	struct hea_cq_context context_s_cq;
	struct hea_qp_context context_qp;
	struct hea_channel_context context_channel_bc;
	struct hea_channel_context context_channel_mc;
	struct hea_qpn_context qpn_context;

	memset(&context_eq, 0, sizeof(context_eq));
	memset(&context_r_cq, 0, sizeof(context_r_cq));
	memset(&context_s_cq, 0, sizeof(context_s_cq));
	memset(&context_qp, 0, sizeof(context_qp));
	memset(&context_channel_bc, 0, sizeof(context_channel_bc));
	memset(&context_channel_mc, 0, sizeof(context_channel_mc));
	memset(&qpn_context, 0, sizeof(qpn_context));

	rhea_debug("Start to create manager for type: %u", registered_type);

	/* create instance */
	rc = rhea_session_init(&mng_ctx->rhea.rhea_id, 0);
	if (rc) {
		rhea_error("rhea_session_init failed: %i", rc);
		goto fail_rhea_session_init;
	}

	/* get EQ */
	context_eq.cfg.eqe_count = RHEA_MANAGER_EQ_WQES;
	context_eq.cfg.irq_type = HEA_IRQ_COALESING_2;
	rc = rhea_eq_alloc(mng_ctx->rhea.rhea_id,
			   &mng_ctx->rhea.eq_id, &context_eq);
	if (rc) {
		rhea_error("rhea_eq_alloc failed: %i", rc);
		goto fail_rhea_eq_alloc;
	}

	context_r_cq.cfg.cqe_count =
		RHEA_MANAGER_RQ_WQES * HEA_MAX_BC_MC_RECV_Q +
		RHEA_MANAGER_CQ_WQES;
	context_r_cq.cfg.cqe_auto_toggle = 1;
	context_r_cq.cfg.irq_type = HEA_IRQ_COALESING_2;
	context_r_cq.ceq = mng_ctx->rhea.eq_id;
	context_r_cq.aeq = mng_ctx->rhea.eq_id;

	rc = rhea_cq_alloc(mng_ctx->rhea.rhea_id,
			   &mng_ctx->rhea.r_cq_id, &context_r_cq);
	if (rc) {
		rhea_error("rhea_cq_alloc failed: %i", rc);
		goto fail_rhea_r_cq_alloc;
	}

	context_s_cq.cfg.cqe_count = RHEA_MANAGER_CQ_WQES;
	context_s_cq.cfg.cqe_auto_toggle = 1;
	context_s_cq.cfg.irq_type = HEA_IRQ_NO;
	context_s_cq.ceq = mng_ctx->rhea.eq_id;
	context_s_cq.aeq = mng_ctx->rhea.eq_id;

	rc = rhea_cq_alloc(mng_ctx->rhea.rhea_id,
			   &mng_ctx->rhea.s_cq_id, &context_s_cq);
	if (rc) {
		rhea_error("rhea_cq_alloc failed: %i", rc);
		goto fail_rhea_s_cq_alloc;
	}

	rc = _rhea_mc_bc_manager_channel_create(mng_ctx,
						registered_type, &channel_id);
	if (rc) {
		rhea_error("Was not able to start channel");
		goto fail_channel_alloc;
	}

	context_qp.channel = channel_id;

	context_qp.eq = mng_ctx->rhea.eq_id;
	context_qp.r_cq = mng_ctx->rhea.r_cq_id;
	context_qp.s_cq = mng_ctx->rhea.s_cq_id;

	context_qp.cfg.sq.wqe_count = RHEA_MANAGER_SQ_WQES;
	context_qp.cfg.sq.wqe_size = HEA_WQE_SIZE_128;
	context_qp.cfg.sq.priority = 1;
	context_qp.cfg.sq.tenure = 255;

	context_qp.cfg.rq1.wqe_count = RHEA_MANAGER_RQ_WQES;
	context_qp.cfg.rq1.wqe_size = HEA_WQE_SIZE_128;
	context_qp.cfg.rq1.low_latency = 0;

	context_qp.cfg.r_cq_use = HEA_RQ_CQE_ENABLE;
	context_qp.cfg.s_cq_use = HEA_SQ_CQE_WQE_SPECIFIED;

	/* tell HEA that all the buffers are aligned to 64-bit */
	context_qp.cfg.dma_64_bit_aligned = 1;

	if (HEA_USE_REAL_ADDRESS)
		context_qp.cfg.real_mode = 1;

	rc = rhea_qp_alloc(mng_ctx->rhea.rhea_id, &mng_ctx->rhea.qp_id,
			   &context_qp);
	if (rc) {
		rhea_error("rhea_qp_alloc failed: %i", rc);
		goto fail_qp_alloc;
	}

	rc = rhea_qp_mapinfo(mng_ctx->rhea.rhea_id,
			     mng_ctx->rhea.qp_id, HEA_PRIV_PRIV,
			     (void **)&mng_ctx->rhea.qp_registers, &map_size,
			     1);
	if (rc) {
		rhea_error("Was not able to map QP into userspace");
		goto fail_qp_mapinfo;
	}

	rc = rhea_cq_mapinfo(mng_ctx->rhea.rhea_id, mng_ctx->rhea.r_cq_id,
			     HEA_PRIV_PRIV,
			     (void **)&mng_ctx->rhea.r_cq_registers, &map_size,
			     1);
	if (rc) {
		rhea_error("Was not able to map receive CQ");
		goto fail_r_cq_mapinfo;
	}

	rc = rhea_cq_mapinfo(mng_ctx->rhea.rhea_id, mng_ctx->rhea.s_cq_id,
			     HEA_PRIV_PRIV,
			     (void **)&mng_ctx->rhea.s_cq_registers, &map_size,
			     1);
	if (rc) {
		rhea_error("Was not able to map send CQ");
		goto fail_s_cq_mapinfo;
	}

	rc = rhea_sq_table(mng_ctx->rhea.rhea_id, mng_ctx->rhea.qp_id,
			   (union snd_wqe **)&mng_ctx->rhea.sq.q_begin,
			   &mng_ctx->rhea.sq.qe_size,
			   &mng_ctx->rhea.sq.qe_count);
	if (rc) {
		rhea_error("Was not able to map SQ table");
		goto fail_sq_table;
	}

	heaq_init(&mng_ctx->rhea.sq);

	rc = rhea_cq_table(mng_ctx->rhea.rhea_id, mng_ctx->rhea.r_cq_id,
			   (struct hea_cqe **)&mng_ctx->rhea.r_cq.q_begin,
			   &mng_ctx->rhea.r_cq.qe_size,
			   &mng_ctx->rhea.r_cq.qe_count);
	if (rc) {
		rhea_error("Was not able to map receive CQ table");
		goto fail_r_cq_table;
	}

	heaq_init(&mng_ctx->rhea.r_cq);

	rc = rhea_cq_table(mng_ctx->rhea.rhea_id,
			   mng_ctx->rhea.s_cq_id,
			   (struct hea_cqe **)&mng_ctx->rhea.s_cq.q_begin,
			   &mng_ctx->rhea.s_cq.qe_size,
			   &mng_ctx->rhea.s_cq.qe_count);
	if (rc) {
		rhea_error("Was not able to map send CQ table");
		goto fail_s_cq_table;
	}

	heaq_init(&mng_ctx->rhea.s_cq);

	rc = rhea_eq_table(mng_ctx->rhea.rhea_id, mng_ctx->rhea.eq_id,
			   (struct hea_eqe **)&mng_ctx->rhea.eq.q_begin,
			   &mng_ctx->rhea.eq.qe_size,
			   &mng_ctx->rhea.eq.qe_count);
	if (rc) {
		rhea_error("Was not able to map EQ table");
		goto fail_eq_table;
	}

	heaq_init(&mng_ctx->rhea.eq);

	{
		void *buffer;
		void **buffer_ptr;
		int buffer_element_size;

		rc = rhea_rq_table(mng_ctx->rhea.rhea_id,
				   mng_ctx->rhea.qp_id, 1, (union rcv_wqe **)
				   &mng_ctx->rhea.rq[0].q_begin,
				   &mng_ctx->rhea.rq[0].qe_size,
				   &mng_ctx->rhea.rq[0].qe_count);
		if (rc) {
			rhea_error("Was not able to map EQ table");
			goto fail_prepare_rq;
		}

		heaq_init(&mng_ctx->rhea.rq[0]);

		/* get size of buffer for aligned buffers */
		buffer_element_size = ALIGN_UP(9022 + 20, 64);
		mng_ctx->rq_mem[0].size = buffer_element_size *
			mng_ctx->rhea.rq[0].qe_count;

		mng_ctx->rq_mem[1].size = buffer_element_size *
			mng_ctx->rhea.rq[0].qe_count;

		mng_ctx->rq_mem[2].size = buffer_element_size *
			mng_ctx->rhea.rq[0].qe_count;

		/* get slot size */
		mng_ctx->rq_buf_slot_size = buffer_element_size;

		/* allocate buffers for WQEs */
		mng_ctx->rq_mem[0].va =
			rhea_pages_alloc(mng_ctx->rq_mem[0].size,
					 GFP_KERNEL | __GFP_DMA);
		if (NULL == mng_ctx->rq_mem[0].va) {
			rhea_error
				("Was not able to allocate new buffer for RQ");
			goto fail_prepare_rq;
		}

		/* allocate buffer which holds pointer to WQEs */
		buffer_ptr = rhea_pages_alloc(mng_ctx->rhea.rq[0].qe_count *
					      sizeof(void *), GFP_KERNEL);
		if (NULL == buffer_ptr) {
			rhea_error
				("Was not able to allocate new buffer for RQ");

			rhea_pages_free(mng_ctx->rq_mem[0].va,
					mng_ctx->rq_mem[0].size);

			goto fail_prepare_rq;
		}

		/* prepare the WQE buffers + make sure all of them are
		 * aligned */
		buffer = mng_ctx->rq_mem[0].va;
		for (j = 0; j < mng_ctx->rhea.rq[0].qe_count; ++j) {
			buffer_ptr[j] = buffer;
			buffer += buffer_element_size;
		}

		rc = _hea_fill_recvq(&mng_ctx->rhea.rq[0], 0,
				     buffer_ptr, mng_ctx->rhea.rq[0].qe_count,
				     buffer_element_size,
				     mng_ctx->rhea.qp_registers);
		if (rc) {
			rhea_error("Was not able to fill rq 0");

			rhea_pages_free(mng_ctx->rq_mem[0].va,
					mng_ctx->rq_mem[0].size);

			rhea_pages_free(buffer_ptr,
					mng_ctx->rhea.rq[0].qe_count *
					sizeof(void *));

			goto fail_prepare_rq;
		}

		for (i = 1; i < HEA_MAX_BC_MC_RECV_Q_BUFFER; ++i) {

			/* allocate buffers for WQEs */
			mng_ctx->rq_mem[i].va =
				rhea_pages_alloc(mng_ctx->rq_mem[i].size,
						 GFP_KERNEL | __GFP_DMA);
			if (NULL == mng_ctx->rq_mem[i].va) {
				rhea_error("Was not able to allocate new "
					   "buffer for RQ");
				goto fail_prepare_extra_rq;
			}
		}

		/* this is the new block of buffers were to get data from */
		mng_ctx->mem_buf_index = 1;
	}

	/* enable interrupt */
	rc = rhea_interrupt_setup(mng_ctx->rhea.rhea_id, mng_ctx->rhea.eq_id,
				  hea_irq_bc_mc_handler, mng_ctx);
	if (rc) {
		rhea_error("Could not allocate new IRQ");
		goto fail_eq_setup;
	}

	/* get assigned IRQ number */
	rhea_eq_get(mng_ctx->rhea.rhea_id, mng_ctx->rhea.eq_id,
		    HEA_EQ_IRQ_NR_GET, &irq_nr);

	mng_ctx->irq_nr = (unsigned int)irq_nr;

	/* enable QP */
	rc = rhea_qp_up(mng_ctx->rhea.rhea_id, mng_ctx->rhea.qp_id);
	if (rc) {
		rhea_error("rhea_qp_up failed: %i", rc);
		goto fail_qp_up;
	}

	/* allocate QPN slot which is shared between all */
	qpn_context.qpn_cfg.slot_count = HEA_QPN_1;
	rc = rhea_channel_qpn_alloc(mng_ctx->rhea.rhea_id,
				    channel_id, &qpn_context);
	if (rc) {
		rhea_error("rhea_qpn_alloc failed: %i", rc);
		goto fail_rhea_channel_qpn_alloc_uc;
	}

	mng_ctx->rhea.qpn_channel_id = channel_id;

	rc = rhea_channel_wire_qpn_to_qp(mng_ctx->rhea.rhea_id,
					 channel_id, mng_ctx->rhea.qp_id, 0);
	if (rc) {
		rhea_error("rhea_channel_wire_qpn_to_qp failed: %i", rc);
		goto fail_rhea_channel_wire_qpn_to_qp;
	}

	rc = _rhea_mc_bc_manager_channel_start(mng_ctx, registered_type);
	if (rc) {
		rhea_error("Was not able to start manager channel: %u",
			   registered_type);
	}

	rhea_info("Successfully created MC/BC Manager for pport: %u",
		  mng_ctx->pport_nr + 1);

	return rc;

	/* in case something went wrong */

fail_rhea_channel_wire_qpn_to_qp:
	rhea_qp_down(mng_ctx->rhea.rhea_id, mng_ctx->rhea.qp_id);

fail_qp_up:
	rhea_interrupt_free(mng_ctx->rhea.rhea_id, mng_ctx->rhea.eq_id);

fail_eq_setup:
	destroy_workqueue(mng_ctx->irq_workqueue);
fail_prepare_extra_rq:
	for (i = 1; i < HEA_MAX_BC_MC_RECV_Q_BUFFER; ++i) {
		/* free buffers for WQEs */
		rhea_pages_free(mng_ctx->rq_mem[i].va,
				mng_ctx->rq_mem[i].size);
		memset(&mng_ctx->rq_mem[i], 0, sizeof(mng_ctx->rq_mem[i]));
	}
fail_prepare_rq:
fail_eq_table:
fail_s_cq_table:
fail_r_cq_table:
fail_sq_table:
fail_s_cq_mapinfo:
fail_r_cq_mapinfo:
fail_qp_mapinfo:
	rhea_qp_free(mng_ctx->rhea.rhea_id, mng_ctx->rhea.qp_id);

fail_qp_alloc:
	rhea_channel_qpn_free(mng_ctx->rhea.rhea_id,
			      mng_ctx->rhea.qpn_channel_id);

fail_rhea_channel_qpn_alloc_uc:
	_rhea_mc_bc_manager_channel_destroy(mng_ctx, registered_type);

fail_channel_alloc:
	rhea_cq_free(mng_ctx->rhea.rhea_id, mng_ctx->rhea.s_cq_id);

fail_rhea_s_cq_alloc:
	rhea_cq_free(mng_ctx->rhea.rhea_id, mng_ctx->rhea.r_cq_id);

fail_rhea_r_cq_alloc:
	rhea_eq_free(mng_ctx->rhea.rhea_id, mng_ctx->rhea.eq_id);

fail_rhea_eq_alloc:
	rhea_session_fini(mng_ctx->rhea.rhea_id);

fail_rhea_session_init:

	return rc;
}

static int
_rhea_mc_bc_manager_destroy(struct rhea_mc_bc_manager_context *mng_ctx)
{
	int rc = 0;
	int i;

	if (HEA_MANAGER_DESTROYED == mng_ctx->state)
		return rc;

	rhea_session_fini(mng_ctx->rhea.rhea_id);

	rhea_debug("Destroy RQ buffer");

	for (i = 0; i < HEA_MAX_BC_MC_RECV_Q_BUFFER; ++i) {
		rhea_pages_free(mng_ctx->rq_mem[i].va,
				mng_ctx->rq_mem[i].size);
		memset(&mng_ctx->rq_mem[i], 0, sizeof(mng_ctx->rq_mem[i]));
	}

	memset(&mng_ctx->rhea, 0, sizeof(mng_ctx->rhea));

	return rc;
}

int rhea_mc_bc_channel_create(unsigned int pport_nr,
			      enum hea_channel_type channel_type)
{
	int rc = 0;
	struct rhea_mc_bc_manager_context *mng_ctx;

	mng_ctx = _rhea_mc_mc_manager_get_manager(pport_nr);
	if (NULL == mng_ctx)
		return -EINVAL;

	rc = _rhea_mc_bc_manager_start(mng_ctx, channel_type);
	if (rc) {
		rhea_error("Was not able to stop channel: %u", channel_type);
		return rc;
	}

	return 0;
}

int rhea_mc_bc_channel_destroy(unsigned int pport_nr,
			       enum hea_channel_type channel_type)
{
	int rc = 0;
	struct rhea_mc_bc_manager_context *mng_ctx;

	mng_ctx = _rhea_mc_mc_manager_get_manager(pport_nr);
	if (NULL == mng_ctx)
		return -EINVAL;

	rc = _rhea_mc_bc_manager_stop(mng_ctx, channel_type);
	if (rc) {
		rhea_error("Was not able to stop channel: %u", channel_type);
		return rc;
	}

	return rc;
}

int rhea_mc_bc_channel_registered_count(unsigned int pport_nr,
					enum hea_channel_type
					channel_type)
{
	int rc = 0;
	unsigned int count = 0;

	struct rhea_mc_bc_manager_context *mng_ctx;

	mng_ctx = _rhea_mc_mc_manager_get_manager(pport_nr);
	if (NULL == mng_ctx)
		return -EINVAL;

	switch (channel_type) {
	case HEA_UC_PORT:
		spin_lock(&mng_ctx->uc.lock);
		count = hea_atomic_get32(&mng_ctx->uc.instance_count);
		spin_unlock(&mng_ctx->uc.lock);
		break;

	case HEA_BC_PORT:
		spin_lock(&mng_ctx->bc.lock);
		count = hea_atomic_get32(&mng_ctx->bc.instance_count);
		spin_unlock(&mng_ctx->bc.lock);
		break;

	case HEA_MC_PORT:
		spin_lock(&mng_ctx->mc.lock);
		count = hea_atomic_get32(&mng_ctx->mc.instance_count);
		spin_unlock(&mng_ctx->mc.lock);
		break;

	default:
		rhea_warning("This channel type is not supported");
	}

	return rc;
}

int rhea_mc_bc_manager_init(unsigned pport_nr)
{
	int rc = 0;
	char queue_name[1024] = { 0 };
	struct rhea_mc_bc_manager_context *mng_ctx;

	if (!is_hea_pport(pport_nr)) {
		rhea_error("Port number is not valid: %u", pport_nr + 1);
		return -EINVAL;
	}

	mng_ctx = rhea_align_alloc(sizeof(*mng_ctx), 8, GFP_KERNEL);
	if (NULL == mng_ctx) {
		rhea_error("Was not able to allocate BC/MC manager");
		return -EINVAL;
	}

	mng_ctx->pport_nr = pport_nr;

	/* init locks */
	spin_lock_init(&mng_ctx->lock);
	spin_lock_init(&mng_ctx->bc.lock);
	spin_lock_init(&mng_ctx->mc.lock);
	spin_lock_init(&mng_ctx->uc.lock);

	mng_ctx->state = HEA_MANAGER_INIT;
	mng_ctx->bc.state = HEA_MANAGER_NO_STATE;
	mng_ctx->uc.state = HEA_MANAGER_NO_STATE;
	mng_ctx->mc.state = HEA_MANAGER_NO_STATE;

	/* create work queue */
	snprintf(queue_name, sizeof(queue_name) - 1,
		 "hea-work-queue-%d", pport_nr + 1);
	mng_ctx->irq_workqueue = create_singlethread_workqueue(queue_name);
	if (NULL == mng_ctx->irq_workqueue) {
		rhea_error("Was not able to allocate workqueue");
		return -ENOMEM;
	}

	/* prepare work queue */
	INIT_DELAYED_WORK(&mng_ctx->irq_work, &irq_work_execute);
	PREPARE_DELAYED_WORK(&mng_ctx->irq_work, &irq_work_execute);

	/* save pointer */
	s_mc_bc_manager_context[pport_nr] = mng_ctx;

	return rc;
}

int rhea_mc_bc_manager_fini(unsigned pport_nr)
{
	int rc = 0;
	struct rhea_mc_bc_manager_context *mng_ctx;

	mng_ctx = _rhea_mc_mc_manager_get_manager(pport_nr);
	if (NULL == mng_ctx)
		return -EINVAL;

	if (HEA_MANAGER_NO_STATE == mng_ctx->state)
		return 0;

	/* make sure that we don't get any more irqs */
	disable_irq(mng_ctx->irq_nr);

	/* wait for work queue to finish */
	if (mng_ctx->irq_workqueue) {
		cancel_delayed_work(&mng_ctx->irq_work);
		flush_workqueue(mng_ctx->irq_workqueue);
		/* delete queue */
		destroy_workqueue(mng_ctx->irq_workqueue);
		mng_ctx->irq_workqueue = NULL;
	}

	/* no lock required anymore, since nobody should be existing anymore */

	_rhea_mc_bc_manager_channel_stop(mng_ctx, HEA_UC_PORT);
	_rhea_mc_bc_manager_channel_stop(mng_ctx, HEA_MC_PORT);
	_rhea_mc_bc_manager_channel_stop(mng_ctx, HEA_BC_PORT);

	_rhea_mc_bc_manager_channel_destroy(mng_ctx, HEA_UC_PORT);
	_rhea_mc_bc_manager_channel_destroy(mng_ctx, HEA_MC_PORT);
	_rhea_mc_bc_manager_channel_destroy(mng_ctx, HEA_BC_PORT);

	_rhea_mc_bc_manager_destroy(mng_ctx);

	mng_ctx->state = HEA_MANAGER_DESTROYED;

	rhea_align_free(mng_ctx, sizeof(*mng_ctx));
	s_mc_bc_manager_context[pport_nr] = NULL;

	return rc;
}
