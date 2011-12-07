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

#include "rhea-qp.h"

struct rhea_context_qps {
	struct rhea_qlist list;
	struct rhea_qinfo sp_info;
	struct rhea_qinfo p_info;
	struct rhea_qinfo u_info;
};

static struct rhea_context_qps s_context_qps;

static struct rhea_qinfo *rhea_get_qpinfo(enum hea_priv_mode priv,
					  struct rhea_context_qps *context)
{
	struct rhea_qinfo *qi = NULL;

	if (NULL == context)
		return NULL;

	switch (priv) {

	case HEA_PRIV_SUPER:
		qi = &context->sp_info;
		break;

	case HEA_PRIV_PRIV:
		qi = &context->p_info;
		break;

	case HEA_PRIV_USER:
		qi = &context->u_info;
		break;

	default:
		return NULL;
	}

	return qi;
}

static struct rhea_qp *_rhea_qp_alloc(void)
{
	struct rhea_qp *qp;
	int id;

	qp = rhea_align_alloc(sizeof(*qp), __alignof__(*qp), GFP_KERNEL);
	if (qp == NULL)
		return NULL;

	id = rhea_ql_alloc(&s_context_qps.list, qp);
	if (id == -1) {
		rhea_align_free(qp, sizeof(*qp));
		return NULL;
	}

	qp->id = id;

	return qp;
}

unsigned rhea_qp_init(struct rhea_gen_base *rhea_base)
{
	u64 reg;
	unsigned cap;
	unsigned num;

	reg = in_be64(&rhea_base->g_qptsz);

	/* get the maximum number of QPs the HEA is capable of */
	cap = hea_get_u64_bits(reg, 8, 31);

	out_be64(&rhea_base->g_qptsz, cap);

	num = cap + 1;

	memset(&s_context_qps, 0, sizeof(s_context_qps));

	rhea_ql_alloc_init(&s_context_qps.list, num);

	return num;
}

void rhea_qp_fini(struct rhea_gen_base *rhea_base)
{
	if (NULL == rhea_base)
		return;

	rhea_ql_alloc_fini(&s_context_qps.list);
}

u64 rhea_qp_qbase_init(struct rhea_gen_base *rhea_base,
		       enum hea_priv_mode priv,
		       unsigned num,
		       unsigned lg, u64 addr)
{
	unsigned size;
	u64 addr_new;
	struct rhea_qinfo *qi;

	/* get queue context for privilege level */
	qi = rhea_get_qpinfo(priv, &s_context_qps);
	if (NULL == qi) {
		rhea_error("Did not find Queue information for QP");
		return 0;
	}

	size = rhea_q_qbase_init(qi, num, priv, lg, addr, "QP", &addr_new);
	if (0 == size) {
		rhea_error("Was not able to map the QP");
		BUG_ON(0 == size);
	}

	switch (priv) {
	case HEA_PRIV_SUPER:
		/* set base for super privilege event queues */
		out_be64(&rhea_base->g_qpsba, addr_new);
		break;

	case HEA_PRIV_PRIV:
		/* set base for privilege event queues */
		out_be64(&rhea_base->g_qppba, addr_new);
		break;

	case HEA_PRIV_USER:
		/* set base for privilege event queues */
		out_be64(&rhea_base->g_qpuba, addr_new);
		break;

	default:
		return 0;
	}

	return addr + size;
}

void rhea_qp_qbase_fini(struct rhea_gen_base *rhea_base,
			enum hea_priv_mode priv)
{
	struct rhea_qinfo *qi;

	if (NULL == rhea_base)
		return;

	qi = rhea_get_qpinfo(priv, &s_context_qps);
	if (NULL == qi || 0 == qi->base.va)
		return;

	rhea_q_qbase_fini(qi);
}

static inline union rhea_qpte_c rhea_qp_control_get(struct rhea_qpte *qpte)
{
	union rhea_qpte_c qp_c;

	qp_c.val = in_be64(&qpte->qp_c.val);
	return qp_c;
}

static inline void rhea_qp_control_set(struct rhea_qpte *qpte,
				       union rhea_qpte_c qp_c)
{
	out_be64(&qpte->qp_c.val, qp_c.val);
}

static inline int rhea_qp_in_err(struct rhea_qpte *qpte)
{
	union rhea_qpte_c qp_c;

	qp_c = rhea_qp_control_get(qpte);

	return qp_c.qp_error;
}

static inline u64 rhea_qp_aer(struct rhea_qpte *qpte)
{
	u64 val;

	val = in_be64(&qpte->qp_aer);

	return val;
}

static inline u64 rhea_qp_aerr(struct rhea_qpte *qpte)
{
	u64 val;

	if (NULL == qpte)
		return 0;

	val = in_be64(&qpte->qp_aerr);
	return val;
}

static void rhea_qp_reset(struct rhea_qpte *qpte)
{
	u64 reg;
	union rhea_qpte_c qp_c;

	if (NULL == qpte)
		return;

	/* Clear out any affiliated error registers */
	out_be64(&qpte->qp_aer, hea_set_u64_bits(0x0ULL, ~0x0ULL, 0, 31));
	out_be64(&qpte->qp_aerr, hea_set_u64_bits(0x0ULL, ~0x0ULL, 0, 31));

	qp_c.qp_enable = 0;
	qp_c.qp_req_qp_state = rhea_qp_state_reset;
	rhea_qp_control_set(qpte, qp_c);

	reg = 0x0ULL;
	out_be64(&qpte->qp_hcr, reg);
	out_be64(&qpte->qp_sqa, reg);
	out_be64(&qpte->qp_sqc, reg);
	out_be64(&qpte->qp_st, reg);
	out_be64(&qpte->qp_tenure, reg);
	out_be64(&qpte->qp_portp, reg);
	out_be64(&qpte->qp_sl, reg);
	out_be64(&qpte->qp_t, reg);
	out_be64(&qpte->qp_sqhp, reg);
	out_be64(&qpte->qp_sqptp, reg);
	out_be64(&qpte->qp_sqwsize, reg);
	out_be64(&qpte->qp_sqsize, reg);
	out_be64(&qpte->qp_sigt, reg);
	out_be64(&qpte->qp_wqecnt, reg);
	out_be64(&qpte->qp_rq1a, reg);
	out_be64(&qpte->qp_rq1c, reg);
	out_be64(&qpte->qp_rq1hp, reg);
	out_be64(&qpte->qp_rq1ptp, reg);
	out_be64(&qpte->qp_rq1size, reg);
	out_be64(&qpte->qp_rq1wsize, reg);
	out_be64(&qpte->qp_rq2a, reg);
	out_be64(&qpte->qp_rq2c, reg);
	out_be64(&qpte->qp_rq2hp, reg);
	out_be64(&qpte->qp_rq2ptp, reg);
	out_be64(&qpte->qp_rq2size, reg);
	out_be64(&qpte->qp_rq2wsize, reg);
	out_be64(&qpte->qp_rq2th, reg);
	out_be64(&qpte->qp_rq3a, reg);
	out_be64(&qpte->qp_rq3c, reg);
	out_be64(&qpte->qp_rq3hp, reg);
	out_be64(&qpte->qp_rq3ptp, reg);
	out_be64(&qpte->qp_rq3size, reg);
	out_be64(&qpte->qp_rq3wsize, reg);
	out_be64(&qpte->qp_rq3th, reg);
	out_be64(&qpte->qp_pd, reg);
	out_be64(&qpte->qp_scqn, reg);
	out_be64(&qpte->qp_rcqn, reg);
	out_be64(&qpte->qp_aeqn, reg);
	out_be64(&qpte->qp_ram, reg);
	out_be64(&qpte->qp_lpn, reg);
	out_be64(&qpte->qp_rpphws, reg);
	out_be64(&qpte->qp_rpphws2, reg);

	/* Clear out any affiliated error registers */
	out_be64(&qpte->qp_aer, hea_set_u64_bits(0x0ULL, ~0x0ULL, 0, 31));
	out_be64(&qpte->qp_aerr, hea_set_u64_bits(0x0ULL, ~0x0ULL, 0, 31));

	/* ensure QP is disabled */
	qp_c.val = in_be64(&qpte->qp_c.val);

	if (qp_c.qp_enable) {
		qp_c.qp_enable = 0;
		qp_c.qp_req_qp_state = rhea_qp_state_reset;

		rhea_qp_control_set(qpte, qp_c);

		while (1) {
			qp_c = rhea_qp_control_get(qpte);

			if (0 == qp_c.qp_enable && qp_c.qp_disable_complete)
				break;
			barrier();
		}
	}
}

static int rhea_qp_create_internal(struct rhea_qp *qp,
				   unsigned lpar, unsigned pgrp,
				   enum hea_channel_type channel_type,
				   unsigned rq_count,
				   struct hea_pd_cfg *pd_cfg,
				   struct hea_qp_cfg *qp_cfg)
{
	u64 reg;
	union rhea_qpte_c qp_c;

	if (NULL == qp || NULL == pd_cfg || NULL == qp_cfg)
		return -EINVAL;

	/* reset QP to default values */
	rhea_qp_reset(qp->qpt);

	out_be64(&qp->qpt->qp_rq1c, 0x10);
	out_be64(&qp->qpt->qp_rq2c, 0x10);
	out_be64(&qp->qpt->qp_rq3c, 0x10);

	out_be64(&qp->qpt->qp_rpphws, 0);
	out_be64(&qp->qpt->qp_rpphws2, 0);

	out_be64(&qp->qpt->qp_rq1c, 0);
	out_be64(&qp->qpt->qp_rq2c, 0);
	out_be64(&qp->qpt->qp_rq3c, 0);

	if (0 != qp_c.qp_error) {
		rhea_error("QP in Error!");
		return -EINVAL;
	}

	reg = 0;
	if (qp_cfg->hw_managed) {
		/* Packet DMA Read Type: RWITM */
		reg = hea_set_u64_bits(reg, 1, 12, 12);
	}

	/* LPAR valid */
	reg = hea_set_u64_bits(reg, 1, 0, 0);
	reg = hea_set_u64_bits(reg, lpar, 56, 63);

	/* wrap control */
	reg = hea_set_u64_bits(reg, 1, 3, 3);

	/* DD1 BUG (in HEA or PBIC), leaving this as 0 (HW default)
	 * causes byte lose for unaligned DMAs at 0x01 to 0x0f for
	 * 0x40 bytes */
	/* cache line DMA Mode */
	reg = hea_set_u64_bits(reg, qp_cfg->dma_64_bit_aligned ? 0 : 1, 1, 1);

	reg = hea_set_u64_bits(reg, qp_cfg->cache_inject, 8, 9);
	reg = hea_set_u64_bits(reg, qp_cfg->target_cache, 10, 11);

	if (!(qp_cfg->hw_managed))
		reg = hea_set_u64_bits(reg, qp_cfg->ep.ll_cache_inject, 13, 14);

	out_be64(&qp->qpt->qp_hcr, reg);

	/* logical port support */
	reg = 0x0ULL;
	if (HEA_LPORT_0 <= channel_type && HEA_LPORT_3 >= channel_type) {
		/* get lport number */
		unsigned channel_nr = channel_type - HEA_LPORT_0;

		/* specify lport */
		reg = hea_set_u64_bits(reg, 1, 56, 56);
		reg = hea_set_u64_bits(reg, channel_nr, 62, 63);
	}
	out_be64(&qp->qpt->qp_lpn, reg);

	/* assign port number */
	out_be64(&qp->qpt->qp_portp, hea_set_u64_bits(0x0ULL, pgrp, 59, 63));

	/* set protection domain */
	reg = 0x0ULL;
	if (pd_cfg->enable_pid_validation) {
		int enable_pid_validation =
				pd_cfg->enable_pid_validation ? 1 : 0;
		reg = hea_set_u64_bits(reg, pd_cfg->as_bit ? 1 : 0, 32, 32);
		reg = hea_set_u64_bits(reg, pd_cfg->gs_bit ? 1 : 0, 33, 33);
		reg = hea_set_u64_bits(reg, pd_cfg->pr_bit ? 1 : 0, 34, 34);
		reg = hea_set_u64_bits(reg, enable_pid_validation, 35, 35);
		reg = hea_set_u64_bits(reg, pd_cfg->pid, 50, 63);
	}
	out_be64(&qp->qpt->qp_pd, reg);

	/* allow real addresses in QP */
	reg = hea_set_u64_bits(0x0ULL, qp_cfg->real_mode ? 1 : 0, 63, 63);
	out_be64(&qp->qpt->qp_ram, reg);

	/* Assign Send CQ */
	out_be64(&qp->qpt->qp_scqn, qp->scq->id);

	/* Assign Receive CQ */
	out_be64(&qp->qpt->qp_rcqn, qp->rcq->id);

	/* Assign QP EQ */
	out_be64(&qp->qpt->qp_aeqn, qp->eq->id);

	/* Assign Initial threshold to the max */
	out_be64(&qp->qpt->qp_rq2th, HEA_THRESHOLD_VAL_9022);
	out_be64(&qp->qpt->qp_rq3th, HEA_THRESHOLD_VAL_9022);

	/* Service Type */
	reg = 0x0ULL;
	if (qp_cfg->hw_managed) {
		/* HW managed SQ and RQ */
		reg = hea_set_u64_bits(reg, 1, 56, 56);
		reg = hea_set_u64_bits(reg, 2, 57, 59);
	} else {
		/* not HW managed */
		reg = hea_set_u64_bits(reg, 0, 56, 56);

		if (qp_cfg->rq1.low_latency) {
			/* LL_QP enabled */
			reg = hea_set_u64_bits(reg, 1, 57, 59);
		} else {
			/* not enhanced */
			reg = hea_set_u64_bits(reg, 0, 57, 59);
		}
	}

	/* setting active RQs */
	if (1 == rq_count) {
		/* RQ1 only */
		reg = hea_set_u64_bits(reg, 0x4, 61, 63);
	} else if (2 == rq_count) {
		/* RQ1 + RQ2 */
		reg = hea_set_u64_bits(reg, 0x5, 61, 63);
	} else if (3 == rq_count) {
		if (qp_cfg->hw_managed) {
			rhea_warning("Invalid RQ setting for NN");
			/* RQ1 only */
			reg = hea_set_u64_bits(reg, 0x4, 61, 63);
		} else {
			/* RQ1 + RQ2 + RQ3 */
			reg = hea_set_u64_bits(reg, 0x6, 61, 63);
		}
	}
	out_be64(&qp->qpt->qp_st, reg);

	/* Signal Type */
	reg = 0;
	if (qp_cfg->hw_managed) {
		/* RQ Replenish enable */
		reg = hea_set_u64_bits(reg, 1, 56, 56);
		/* Replenish Realignement, 0 blocks */
		reg = hea_set_u64_bits(reg, 0, 59, 61);
		/* Signal type: No CQEs generated for send queue WQEs */
		reg = hea_set_u64_bits(reg, 0, 62, 63);
	}

	if (qp_cfg->r_cq_use) {
		/* Place all completion information in CQE */
		reg = hea_set_u64_bits(reg, 0, 57, 57);
	} else {
		/* Low-Latency RQ1 gets CQE information */
		reg = hea_set_u64_bits(reg, 1, 57, 57);
	}

	/* enable SQ CQE generation */
	if (qp_cfg->s_cq_use && 0 == hea_get_u64_bits(reg, 56, 56))
		reg = hea_set_u64_bits(reg, qp_cfg->s_cq_use, 62, 63);
	out_be64(&qp->qpt->qp_sigt, reg);

	/* token */
	reg = hea_set_u64_bits(0x0ULL, qp->id, 32, 63);
	out_be64(&qp->qpt->qp_t, reg);

	/* reset QP */
	qp_c.val = 0;
	qp_c.qp_enable = 1;
	/* QP in reset state */
	qp_c.qp_req_qp_state = rhea_qp_state_reset;

	rhea_qp_control_set(qp->qpt, qp_c);

	return 0;
}

static void rhea_qp_internal_free(struct rhea_qp *qp)
{
	if (qp == NULL)
		return;

	/* reset QP to default values */
	rhea_qp_reset(qp->qpt);

	rhea_ql_free(&s_context_qps.list, qp->id);

	rhea_align_free(qp, sizeof(*qp));
}

static const int rhea_qp_size[] = {
	[0x0] = 128,
	[0x1] = 256,
	[0x2] = 512,
	[0x3] = 1024,
	[0x4] = 2048,
	[0x5] = 4096,
	[0x8] = 1518,
	[0x9] = 1522,
	[0xa] = 9022,
};

static inline unsigned rhea_qp_wqe_max_enc(unsigned wqes)
{
	unsigned i;
	unsigned max;

	i = 0;
	for (;;) {
		max = (1 << (i + 1)) - 1;

		if (max >= wqes)
			return i;

		++i;
	}
}

static int rhea_qp_sq_create(struct rhea_qinfo *qi, struct rhea_qp *qp,
			     struct hea_sq_cfg *sq_cfg, unsigned hw_mng)
{
	int rc = 0;
	unsigned wqes_log_max;
	struct rhea_qp_sq *sq;
	u64 reg;

	if (NULL == sq_cfg || NULL == qi || NULL == qp)
		return -EINVAL;

	sq = &qp->sq;

	wqes_log_max = rhea_qp_wqe_max_enc(sq_cfg->wqe_count);

	sq->wqe_count = (1U << wqes_log_max);

	if (sq_cfg->wqe_count != sq->wqe_count) {
		rhea_error("Queue length has to be power of 2");
		return -EINVAL;
	}

	if (HEA_QUEUE_MAX_LENGTH < sq->wqe_count) {
		rhea_error("Maximum queue length is: %u",
			   HEA_QUEUE_MAX_LENGTH);
		return -EINVAL;
	}

	/* check that size is not too big */
	if (sq_cfg->wqe_size > ARRAY_SIZE(rhea_qp_size))
		return -EINVAL;

	/* get the correct WQE size */
	if (hw_mng) {
		if (0x4 != sq_cfg->wqe_size) {
			rhea_error("the SQ WQE SIZE field for compact "
				   "SWQE should be 4");
			return -EINVAL;
		}

		sq->wqe_size = 16;
	} else {
		sq->wqe_size = rhea_qp_size[sq_cfg->wqe_size];
	}

	/* allocate and configure page table */
	rc = rhea_pt_alloc(qi, &sq->pt, &sq->q,
			   sq->wqe_size * sq->wqe_count, hw_mng, 0);
	if (rc) {
		rhea_error("Was not able to setup page table pointer for SQ");
		return rc;
	}

	/* pass pointer to first WQE */
	sq->wqe_begin = (union snd_wqe *)sq->q.va;

	/* set the head pointer */
	out_be64(&qp->qpt->qp_sqhp, sq->q.pa);

	/* Set WQE Size */
	out_be64(&qp->qpt->qp_sqwsize, sq_cfg->wqe_size);

	/* reset SQ counter */
	out_be64(&qp->qpt->qp_sqc, 0x0ULL);

	/* Disable early discard */
	reg = hea_set_u64_bits(0ull, 0, 45, 47);

	/* Max Number of WQEs */
	reg = hea_set_u64_bits(reg, wqes_log_max - 1, 60, 63);
	out_be64(&qp->qpt->qp_sqsize, reg);

	/* Set SQ priority (0 = low, 1 = high) */
	reg = hea_set_u64_bits(0x0ULL, sq_cfg->priority ? 1 : 0, 59, 59);
	out_be64(&qp->qpt->qp_sl, reg);

	/* Set tenure, about a packet */
	if (256 <= sq_cfg->tenure)
		sq_cfg->tenure = 255;

	reg = hea_set_u64_bits(0x0ULL, sq_cfg->tenure, 56, 63);
	out_be64(&qp->qpt->qp_tenure, reg);

	reg = 0;
	if (hw_mng) {
		/* DMA read type: RWITM */
		reg = hea_set_u64_bits(reg, 1, 12, 12);
	} else {
		/* Page Table Pointer */
		if (NULL == sq->pt.va) {
			rhea_error("PTEs were not initialized");
			rhea_pt_free(&sq->pt, &sq->q, hw_mng);
			return -ENOMEM;
		}

		/* set SQ PTP */
		reg |= sq->pt.pa;
	}

	out_be64(&qp->qpt->qp_sqptp, reg);

	return rc;
}

static inline void rhea_qp_sq_destroy(struct rhea_qp_sq *sq,
				      unsigned hw_managed)
{
	if (NULL == sq)
		return;

	if (sq->q.va)
		rhea_pt_free(&sq->pt, &sq->q, hw_managed);
}

static int rhea_qp_rq1_create(struct rhea_qinfo *qi, struct rhea_qp *qp,
			      struct hea_rq1_cfg *rq1_cfg,
			      unsigned int rq2_threshhold, unsigned int hw_mng)
{
	int rc;
	unsigned wqes_log_max;
	struct rhea_qp_rqn *rq = &qp->rq1;
	u64 reg;

	if (NULL == qi || NULL == qp || NULL == rq1_cfg)
		return -EINVAL;

	wqes_log_max = rhea_qp_wqe_max_enc(rq1_cfg->wqe_count);

	/* clear rq1 count register */
	out_be64(&qp->qpt->qp_rq1c, 0);

	rq->wqe_count = (1U << wqes_log_max);

	if (rq1_cfg->wqe_count != rq->wqe_count) {
		rhea_error("Queue length has to be power of 2");
		return -EINVAL;
	}

	if (HEA_QUEUE_MAX_LENGTH < rq->wqe_count) {
		rhea_error("Maximum queue length is: %u",
			   HEA_QUEUE_MAX_LENGTH);
		return -EINVAL;
	}

	/* check that size is not too big */
	if (rq1_cfg->wqe_size > ARRAY_SIZE(rhea_qp_size))
		return -EINVAL;

	if (hw_mng) {
		if (0x3 == rq1_cfg->wqe_size) {
			rhea_warning("the RQ WQE SIZE field for compact "
				     "RWQE should be 3");
			rq->wqe_size = 8;

		}
	} else {
		rq->wqe_size = rhea_qp_size[rq1_cfg->wqe_size];
	}

	/* allocate and configure page table */
	rc = rhea_pt_alloc(qi, &rq->pt, &rq->q,
			   rq->wqe_size * rq->wqe_count, hw_mng, 1);
	if (rc) {
		rhea_error("Was not able to setup page table pointer for RQ1");
		return rc;
	}

	/* save RQ1 pointer */
	rq->wqe_begin = (union rcv_wqe *)rq->q.va;

	/* set the head pointer */
	out_be64(&qp->qpt->qp_rq1hp, rq->q.pa);

	/* Set WQE Size */
	out_be64(&qp->qpt->qp_rq1wsize, rq1_cfg->wqe_size);

	/* reset RQ count */
	out_be64(&qp->qpt->qp_rq1c, 0x0ULL);

	/* Disable early discard */
	reg = hea_set_u64_bits(0ull, 0, 45, 47);

	/* Max Number of WQEs */
	reg = hea_set_u64_bits(reg, wqes_log_max - 1, 60, 63);
	out_be64(&qp->qpt->qp_rq1size, reg);

	/* set rq2 and rq3 thresdhold */
	out_be64(&qp->qpt->qp_rq2th,
		 rq2_threshhold ? rq2_threshhold : HEA_THRESHOLD_VAL_9022);
	out_be64(&qp->qpt->qp_rq3th,
		 rq2_threshhold ? rq2_threshhold : HEA_THRESHOLD_VAL_9022);

	/* set RQ1 PTP */
	reg = 0;
	if (hw_mng) {
		/* DMA read type: RWITM */
		reg = hea_set_u64_bits(reg, 1, 12, 12);
	} else {
		/* Page Table Pointer */
		if (NULL == rq->pt.va) {
			rhea_error("PTEs were not initialized");

			rhea_pt_free(&rq->pt, &rq->q, hw_mng);

			return -EINVAL;
		}

		reg |= rq->pt.pa;

		/* set RQE Valid bit */
		reg = hea_set_u64_bits(reg, 1, 62, 62);
	}

	out_be64(&qp->qpt->qp_rq1ptp, reg);

	/* no need to update QPx_ST since RQ1 is always enabled and
	 * the LL_QP options is already set */
	reg = in_be64(&qp->qpt->qp_st);

	if (hw_mng) {
		if (hea_get_u64_bits(reg, 57, 59) == 2) {
			rhea_error("HW managed SQ/RQ not enabled: 0x%llx",
				   reg);
			return -EINVAL;
		}
	}

	return 0;
}

static inline void rhea_qp_rq1_destroy(struct rhea_qp_rqn *rq1,
				       unsigned hw_managed)
{
	if (NULL == rq1)
		return;

	if (rq1->q.va)
		rhea_pt_free(&rq1->pt, &rq1->q, hw_managed);
}

static int rhea_qp_rq2_create(struct rhea_qinfo *qi, struct rhea_qp *qp,
			      struct hea_rq_cfg *rq2_cfg,
			      unsigned int rq3_threshhold, unsigned hw_mng)
{
	int rc = 0;
	unsigned wqes_log_max;
	struct rhea_qp_rqn *rq = &qp->rq2;
	u64 reg;

	if (NULL == qi || NULL == qp || NULL == rq2_cfg)
		return -EINVAL;

	wqes_log_max = rhea_qp_wqe_max_enc(rq2_cfg->wqe_count);

	/* clear rq count register */
	out_be64(&qp->qpt->qp_rq2c, 0);

	rq->wqe_count = (1U << wqes_log_max);

	if (rq2_cfg->wqe_count != rq->wqe_count) {
		rhea_error("Queue length has to be power of 2");
		return -EINVAL;
	}

	if (HEA_QUEUE_MAX_LENGTH < rq->wqe_count) {
		rhea_error("Maximum queue length is: %u",
			   HEA_QUEUE_MAX_LENGTH);
		return -EINVAL;
	}

	/* check that size is not too big */
	if (rq2_cfg->wqe_size > ARRAY_SIZE(rhea_qp_size))
		return -EINVAL;

	if (hw_mng) {
		if (0x3 == rq2_cfg->wqe_size) {
			rhea_warning("the RQ WQE SIZE field for compact "
				     "RWQE should be 3");
			rq->wqe_size = 8;

		}
	} else {
		rq->wqe_size = rhea_qp_size[rq2_cfg->wqe_size];
	}

	/* allocate and configure page table */
	rc = rhea_pt_alloc(qi, &rq->pt, &rq->q,
			   rq->wqe_size * rq->wqe_count, hw_mng, 0);
	if (rc) {
		rhea_error("Was not able to setup page table pointer for RQ1");
		return rc;
	}

	/* save RQ2 pointer */
	rq->wqe_begin = (union rcv_wqe *)rq->q.va;

	/* set the head pointer */
	out_be64(&qp->qpt->qp_rq2hp, rq->q.pa);

	/* Set WQE Size */
	out_be64(&qp->qpt->qp_rq2wsize, rq2_cfg->wqe_size);

	/* reset RQ count */
	out_be64(&qp->qpt->qp_rq2c, 0x0ULL);

	/* Disable early discard */
	reg = hea_set_u64_bits(0ull, 0, 45, 47);

	/* Max Number of WQEs */
	reg = hea_set_u64_bits(reg, wqes_log_max - 1, 60, 63);
	out_be64(&qp->qpt->qp_rq2size, reg);

	/* set RQ2 threshold */
	out_be64(&qp->qpt->qp_rq2th, rq2_cfg->data_threshold);

	/* set rq3 threshold */
	out_be64(&qp->qpt->qp_rq3th,
		 rq3_threshhold ? rq3_threshhold : HEA_THRESHOLD_VAL_9022);

	/* set RQ PTP */
	reg = 0;
	if (hw_mng) {
		/* DMA read type: RWITM */
		reg = hea_set_u64_bits(reg, 1, 12, 12);
	} else {
		/* Page Table Pointer */
		if (NULL == rq->pt.va) {
			rhea_error("PTEs were not initialized");

			rhea_pt_free(&rq->pt, &rq->q, hw_mng);

			return -EINVAL;
		}

		reg |= rq->pt.pa;
		/* set RQE Valid bit */
		reg = hea_set_u64_bits(reg, 1, 62, 62);

		out_be64(&qp->qpt->qp_rq2ptp, reg);
	}

	return rc;
}

static inline void rhea_qp_rq2_destroy(struct rhea_qp_rqn *rq2,
				       unsigned hw_managed)
{
	if (NULL == rq2)
		return;

	if (rq2->q.va)
		rhea_pt_free(&rq2->pt, &rq2->q, hw_managed);
}

static int rhea_qp_rq3_create(struct rhea_qinfo *qi, struct rhea_qp *qp,
			      struct hea_rq_cfg *rq3_cfg)
{
	int rc = 0;
	unsigned wqes_log_max;
	struct rhea_qp_rqn *rq = &qp->rq3;
	u64 reg;

	if (NULL == qi || NULL == qp || NULL == rq3_cfg)
		return -EINVAL;

	wqes_log_max = rhea_qp_wqe_max_enc(rq3_cfg->wqe_count);

	/* clear rq count register */
	out_be64(&qp->qpt->qp_rq3c, 0);

	rq->wqe_count = (1U << wqes_log_max);

	if (rq3_cfg->wqe_count != rq->wqe_count) {
		rhea_error("Queue length has to be power of 2");
		return -EINVAL;
	}

	if (HEA_QUEUE_MAX_LENGTH < rq->wqe_count) {
		rhea_error("Maximum queue length is: %u",
			   HEA_QUEUE_MAX_LENGTH);
		return -EINVAL;
	}

	/* check that size is not too big */
	if (rq3_cfg->wqe_size > ARRAY_SIZE(rhea_qp_size))
		return -EINVAL;

	rq->wqe_size = rhea_qp_size[rq3_cfg->wqe_size];

	/* allocate and configure page table */
	rc = rhea_pt_alloc(qi, &rq->pt, &rq->q,
			   rq->wqe_size * rq->wqe_count, 0, 0);
	if (rc) {
		rhea_error("Was not able to setup page table pointer for RQ3");
		return rc;
	}

	/* save RQ3 pointer */
	rq->wqe_begin = (union rcv_wqe *)rq->q.va;

	/* set the head pointer */
	out_be64(&qp->qpt->qp_rq3hp, rq->q.pa);

	/* Set WQE Size */
	out_be64(&qp->qpt->qp_rq3wsize, rq3_cfg->wqe_size);

	/* reset RQ count */
	out_be64(&qp->qpt->qp_rq3c, 0x0ULL);

	/* Disable early discard */
	reg = hea_set_u64_bits(0ull, 0, 45, 47);

	/* Max Number of WQEs */
	reg = hea_set_u64_bits(reg, wqes_log_max - 1, 60, 63);
	out_be64(&qp->qpt->qp_rq3size, reg);

	/* set RQ3 threshold */
	out_be64(&qp->qpt->qp_rq3th, rq3_cfg->data_threshold);

	/* set RQ PTP */
	reg = 0;

	/* Page Table Pointer */
	if (NULL == rq->pt.va) {
		rhea_error("PTEs were not initialized");

		rhea_pt_free(&rq->pt, &rq->q, 0);

		return -EINVAL;
	}

	reg |= rq->pt.pa;
	/* set RQE Valid bit */
	reg = hea_set_u64_bits(reg, 1, 62, 62);

	out_be64(&qp->qpt->qp_rq3ptp, reg);

	return rc;
}

static inline void rhea_qp_rq3_destroy(struct rhea_qp_rqn *rq3,
				       unsigned hw_managed)
{
	if (NULL == rq3)
		return;

	if (rq3->q.va)
		rhea_pt_free(&rq3->pt, &rq3->q, hw_managed);
}

struct rhea_qp *_rhea_qp_get(unsigned int qp_id)
{
	struct rhea_qp *qp;

	qp = rhea_ql_get(&s_context_qps.list, qp_id);
	if (NULL == qp)
		rhea_error("Was not able to get QP");
	return qp;
}

struct rhea_qp *rhea_qp_create(unsigned pport_nr,
			       enum hea_channel_type channel_type,
			       struct hea_process *process,
			       struct rhea_eq *eq,
			       struct rhea_cq *rcq,
			       struct rhea_cq *scq,
			       struct hea_pd_cfg *pd_cfg,
			       struct hea_qp_cfg *qp_cfg)
{
	unsigned pgrp = pport_nr + 1;
	unsigned rq_count = 0;
	int rc;
	struct rhea_qp *qp;
	union rhea_qpte_c qp_c;
	struct rhea_qinfo *qi;

	if (NULL == eq || NULL == rcq || NULL == pd_cfg ||
	    NULL == scq || NULL == qp_cfg || NULL == process)
		return NULL;

	qi = rhea_get_qpinfo(HEA_PRIV_SUPER, &s_context_qps);
	if (NULL == qi) {
		rhea_error("Could not find Queue context");
		return NULL;
	}

	qp = _rhea_qp_alloc();
	if (NULL == qp) {
		rhea_error("Was not able to allocate QP");
		goto fail_qp_alloc;
	}

	/* save config */
	qp->qp_cfg = *qp_cfg;

	/* save all the other settings/pointers/... */
	qp->qpt = rhea_qte(qi, qp->id, RHEA_ADDRESS_VIRTUAL);
	qp->qp_info = qi;
	qp->rcq = rcq;
	qp->scq = scq;
	qp->eq = eq;
	qp->pport_nr = pport_nr;
	qp->channel_type = channel_type;

	qp->process = *process;

	/* get number of configured RQs */
	rq_count += (qp_cfg->rq1.wqe_count ? 1 : 0);
	rq_count += (qp_cfg->rq2.wqe_count ? 1 : 0);
	rq_count += (qp_cfg->rq3_ep.wqe_count ? 1 : 0);

	rc = rhea_qp_create_internal(qp, process->lpar, pgrp,
				     channel_type, rq_count, pd_cfg, qp_cfg);
	if (0 != rc) {
		rhea_error("Could not create internal QP: %i", rc);
		goto fail_qp_create_internal;
	}

	if (qp_cfg->sq.wqe_count) {
		if (qp_cfg->sq.wqe_count > HEA_SQ_WQES_MAX) {
			rhea_warning("Warning: SQ WQE max is %d, using that",
				     HEA_SQ_WQES_MAX);

			qp_cfg->sq.wqe_count = HEA_SQ_WQES_MAX;
		}

		rhea_info("Create SQ for %s QP[%u] with WQE size: %u, "
			  "WQE count: %u",
			  ((qp_cfg->hw_managed) ? "NN" : "EP"), qp->id,
			  qp_cfg->sq.wqe_size, qp_cfg->sq.wqe_count);

		rc = rhea_qp_sq_create(qi, qp, &qp_cfg->sq,
				       qp_cfg->hw_managed);
		if (0 != rc) {
			rhea_error("Could not create SQ for QP: %i", rc);
			goto fail_sq_create;
		}

	}

	if (qp_cfg->rq1.wqe_count) {
		if (qp_cfg->rq1.wqe_count > HEA_RQ_WQES_MAX) {
			rhea_warning("RQ1 WQE max is %d, using that",
				     HEA_RQ_WQES_MAX);
			qp_cfg->rq1.wqe_count = HEA_RQ_WQES_MAX;
		}

		rhea_info("Create RQ1 for %s QP[%u] with WQE size: %u, "
			  "WQE count: %u and low latency: %u",
			  ((qp_cfg->hw_managed) ? "NN" : "EP"), qp->id,
			  qp_cfg->rq1.wqe_size, qp_cfg->rq1.wqe_count,
			  qp_cfg->rq1.low_latency);

		rc = rhea_qp_rq1_create(qi, qp, &qp_cfg->rq1,
					qp_cfg->rq2.data_threshold,
					qp_cfg->hw_managed);
		if (0 != rc) {
			rhea_error("could not create RQ1 for QP[%u]", qp->id);
			goto fail_rq1_create;
		}
	} else {
		rhea_error("must have RQ1 created at least");
		return NULL;
	}

	if (qp_cfg->rq2.wqe_count && qp_cfg->rq1.wqe_count) {
		if (qp_cfg->rq2.wqe_count > HEA_RQ_WQES_MAX) {
			rhea_warning("RQ2 WQE max is %d, using that",
				     HEA_RQ_WQES_MAX);
			qp_cfg->rq2.wqe_count = HEA_RQ_WQES_MAX;
		}

		rhea_info("Created RQ2 for %s QP[%u] with WQE size: %u, "
			  "WQE count: %u and threshold: %u",
			  ((qp_cfg->hw_managed) ? "NN" : "EP"), qp->id,
			  qp_cfg->rq2.wqe_size, qp_cfg->rq2.wqe_count,
			  qp_cfg->rq2.data_threshold);

		rc = rhea_qp_rq2_create(qi, qp, &qp_cfg->rq2,
					qp_cfg->rq3_ep.data_threshold,
					qp_cfg->hw_managed);
		if (0 != rc) {
			rhea_error("could not create RQ2 for QP[%u]", qp->id);
			goto fail_rq2_create;
		}
	}

	if (qp_cfg->rq3_ep.wqe_count && qp_cfg->rq2.wqe_count &&
	    qp_cfg->rq1.wqe_count && !qp_cfg->hw_managed) {
		if (qp_cfg->rq3_ep.wqe_count > HEA_RQ_WQES_MAX) {
			rhea_warning("RQ3 WQE max is %d, using that",
				     HEA_RQ_WQES_MAX);
			qp_cfg->rq3_ep.wqe_count = HEA_RQ_WQES_MAX;
		}

		rhea_info("Created RQ3 for EP QP[%u] with WQE size: %u, "
			  "WQE count: %u and threshold: %u",
			  qp->id, qp_cfg->rq3_ep.wqe_size,
			  qp_cfg->rq3_ep.wqe_count,
			  qp_cfg->rq3_ep.data_threshold);

		rc = rhea_qp_rq3_create(qi, qp, &qp_cfg->rq3_ep);
		if (0 != rc) {
			rhea_error("could not create RQ3 for QP[%u]", qp->id);
			goto fail_rq3_create;
		}

		rhea_info("Created RQ3 for QP[%u]", qp->id);
	}

	/* make sure the QP has been reset */
	qp_c = rhea_qp_control_get(qp->qpt);

	if (rhea_qp_state_reset != qp_c.qp_res_qp_state) {
		rhea_error("QP not in reset, qp_c: 0x%016llx", qp_c.val);
		goto fail_wrong_state;
	}

	/* only support header separation if RQ1 is low latency */
	if (qp_cfg->rq1.low_latency &&
	    HEA_HEAD_SEP_NONE != qp_cfg->ep.header_sep) {
		u64 reg;

		qp_c.qp_hdr_sep = qp_cfg->ep.header_sep;

		/* set this register, in case it was not set before */
		reg = hea_set_u64_bits(0x0ULL, 1, 59, 59);
		out_be64(&qp->qpt->qp_sl, reg);
	}

	/* Place QP in initialized state */
	qp_c.qp_req_qp_state = rhea_qp_state_init;
	rhea_qp_control_set(qp->qpt, qp_c);

	return qp;

fail_wrong_state:
	rhea_qp_rq3_destroy(&qp->rq3, qp->qp_cfg.hw_managed);
fail_rq3_create:
	rhea_qp_rq2_destroy(&qp->rq2, qp->qp_cfg.hw_managed);
fail_rq2_create:
	rhea_qp_rq2_destroy(&qp->rq1, qp->qp_cfg.hw_managed);
fail_rq1_create:
	rhea_qp_sq_destroy(&qp->sq, qp->qp_cfg.hw_managed);
fail_sq_create:
fail_qp_create_internal:
	rhea_qp_internal_free(qp);
fail_qp_alloc:
	qp = NULL;
	return qp;
}

int rhea_qp_destroy(struct rhea_qp *qp)
{
	int rc = 0;

	struct rhea_qinfo *qi;

	if (NULL == qp)
		return -EINVAL;

	qi = rhea_get_qpinfo(HEA_PRIV_SUPER, &s_context_qps);
	if (NULL == qi || 0 == qi->base.va)
		return -EINVAL;

	/* destroy SQ */
	rhea_qp_sq_destroy(&qp->sq, qp->qp_cfg.hw_managed);

	/* destroy all RQs */
	rhea_qp_rq1_destroy(&qp->rq1, qp->qp_cfg.hw_managed);
	rhea_qp_rq2_destroy(&qp->rq2, qp->qp_cfg.hw_managed);
	rhea_qp_rq3_destroy(&qp->rq3, qp->qp_cfg.hw_managed);

	/* QP */
	rhea_qp_internal_free(qp);

	return rc;
}

int rhea_qp_feature_get(struct rhea_qp *qp, enum hea_qp_feature_get feature,
			u64 *value)
{
	int rc = 0;

	if (NULL == qp || NULL == value)
		return -EINVAL;

	switch (feature) {
	case HEA_QP_TOKEN_GET:
		*value = qp->id;
		break;

	case HEA_QP_AER_GET:
		*value = in_be64(&qp->qpt->qp_aer);
		break;

	case HEA_QP_AERR_GET:
		*value = in_be64(&qp->qpt->qp_aerr);
		break;

	case HEA_QP_ENABLED_GET:
		{
			union rhea_qpte_c qp_c = rhea_qp_control_get(qp->qpt);

			*value = qp_c.qp_enable;
		}
		break;

	default:
		rhea_error("Invalid feature parameter");
		rc = -EINVAL;
		break;
	}

	return rc;
}

int _rhea_qp_error_reset(struct rhea_qp *qp, u64 reg,
			 enum hea_qp_feature_set feature)
{
	int rc = 0;
	union rhea_qpte_c qp_c;

	if (NULL == qp)
		return -EINVAL;

	/* write values to register */
	switch (feature) {
	case HEA_QP_AER_SET:
		out_be64(&qp->qpt->qp_aer, reg);
		break;

	case HEA_QP_AERR_SET:
		out_be64(&qp->qpt->qp_aerr, reg);
		break;

	default:
		rhea_error("Invalid parameter");
		rc = -EINVAL;
	}

	qp_c = rhea_qp_control_get(qp->qpt);

	/* checks if QP is in error state */
	if (qp_c.qp_error) {
		/* reset QP */
		qp_c.qp_req_qp_state = rhea_qp_state_reset;
		rhea_qp_control_set(qp->qpt, qp_c);
	}

	return rc;
}

int rhea_qp_feature_set(struct rhea_qp *qp, enum hea_qp_feature_set feature,
			u64 value)
{
	int rc = 0;

	if (NULL == qp)
		return -EINVAL;

	switch (feature) {
	case HEA_QP_AERR_SET:
	case HEA_QP_AER_SET:
		{
			rc = _rhea_qp_error_reset(qp, value, feature);
			if (rc)
				return rc;

			rc = rhea_qp_enable(qp, qp->qp_cfg.hw_managed);
			if (rc) {
				rhea_error
					("Was not able to enable QP again");
				return rc;
			}
		}
		break;

	default:
		rhea_error("Invalid feature parameter");
		rc = -EINVAL;
		break;
	}

	return rc;
}

int rhea_qp_enable(struct rhea_qp *qp, unsigned hw_mng)
{
	u64 reg;
	union rhea_qpte_c qp_c;

	qp_c = rhea_qp_control_get(qp->qpt);

	/* enable QP */
	qp_c.qp_enable = 1;

	switch (qp_c.qp_res_qp_state) {
	case rhea_qp_state_reset:

		/* Place QP in initialized state */
		qp_c.qp_req_qp_state = rhea_qp_state_init;
		rhea_qp_control_set(qp->qpt, qp_c);

	case rhea_qp_state_init:

		/* make sure the QP has been initialised */
		if (rhea_qp_state_init != qp_c.qp_res_qp_state) {
			rhea_error("qp_c not INIT: 0x%016llx", qp_c.val);
			return -EINVAL;
		}

		if (!hw_mng) {
			/* check if RQ1 is filled */
			reg = in_be64(&qp->qpt->qp_rq1c);
			if (0 == reg) {
				rhea_error("RQ1 is still empty!!");
				return -EINVAL;
			}

#ifdef RHE_REVISIT_THIS_CODE
			if (qp->qp_cfg.rq1.low_latency && 0 ==
			    qp->rq2.q.size) {
				rhea_error("No RQ2 configured for low "
					   "latency RQ1!");
				return -EINVAL;
			}
#endif

			/* check if RQ2 is enabled */
			if (qp->rq2.q.size) {
				/* check if RQ2 is filled */
				reg = in_be64(&qp->qpt->qp_rq2c);
				if (0 == reg) {
					rhea_error("RQ2 is still empty!!");
					return -EINVAL;
				}
			}

			/* check if RQ3 is enabled */
			if (qp->rq3.q.size) {
				/* check if RQ3 is filled */
				reg = in_be64(&qp->qpt->qp_rq3c);
				if (0 == reg) {
					rhea_error("RQ3 is still empty!!");
					return -EINVAL;
				}
			}
		}

		/* release the hounds */
		qp_c.qp_req_qp_state = rhea_qp_state_ready2rcv;
		rhea_qp_control_set(qp->qpt, qp_c);

	case rhea_qp_state_ready2rcv:

		/* need a wait of some sort */
		qp_c = rhea_qp_control_get(qp->qpt);
		if (rhea_qp_state_ready2rcv != qp_c.qp_res_qp_state) {
			rhea_error("qp_c not READY2RCV: 0x%016llx", qp_c.val);
			return -EINVAL;
		}

		/* release the hounds */
		qp_c.qp_req_qp_state = rhea_qp_state_ready2send;
		rhea_qp_control_set(qp->qpt, qp_c);

	case rhea_qp_state_ready2send:

		/* need a wait of some sort */
		qp_c = rhea_qp_control_get(qp->qpt);
		if (rhea_qp_state_ready2send != qp_c.qp_res_qp_state) {
			rhea_error("qp_c not READY2SEND: 0x%016llx", qp_c.val);
			return -EINVAL;
		}
		break;

	default:
		rhea_error("QP is in invalid state");
		return -EINVAL;
	}

	return 0;
}

int rhea_qp_disable(struct rhea_qp *qp)
{
	union rhea_qpte_c qp_c;

	if (NULL == qp)
		return -EINVAL;

	qp_c = rhea_qp_control_get(qp->qpt);

	/* disable QP */
	qp_c.qp_enable = 0;

	qp_c.qp_req_qp_state = rhea_qp_state_init;

	rhea_qp_control_set(qp->qpt, qp_c);

	/* wait until the QP is disabled */
	while (0 != qp_c.qp_disable_complete)
		qp_c = rhea_qp_control_get(qp->qpt);

	return 0;
}

int rhea_qp_mapinfo_get(struct rhea_qp *qp,
			enum hea_priv_mode priv,
			void **pointer, unsigned *size, unsigned use_va)
{
	int rc = 0;
	struct rhea_qinfo *qi;
	enum rhea_memory_type addr_type;

	if (NULL == size || NULL == pointer) {
		rhea_error("Invalid parameter");
		return -EINVAL;
	}

	qi = rhea_get_qpinfo(priv, &s_context_qps);
	if (NULL == qi || 0 == qi->base.va)
		return -EINVAL;

	/* get correct address type */
	addr_type = ((use_va) ? RHEA_ADDRESS_VIRTUAL : RHEA_ADDRESS_PHYSICAL);

	/* get pointer to QP page */
	*pointer = rhea_qte(qi, qp->id, addr_type);

	/* get size of page */
	*size = qi->os_page_sz;

	if (NULL == *pointer || 0 == *size) {
		rhea_error("QP not setup correctly");
		return -EINVAL;
	}

	return rc;
}

void rhea_qpte_dump(struct rhea_qpte *qpte, const char *privs, unsigned qp)
{
	rhea_reg_print(qpte, qp_hcr, "%s QP[%d]_HCR", privs, qp);
	rhea_reg_print(qpte, qp_c, "%s QP[%d]_C", privs, qp);
	rhea_reg_print(qpte, qp_aer, "%s QP[%d]_AER", privs, qp);
	rhea_reg_print(qpte, qp_aerr, "%s QP[%d]_AERR", privs, qp);
	rhea_reg_print(qpte, qp_sqa, "%s QP[%d]_SQA", privs, qp);
	rhea_reg_print(qpte, qp_sqc, "%s QP[%d]_SQC", privs, qp);
	rhea_reg_print(qpte, qp_st, "%s QP[%d]_ST", privs, qp);
	rhea_reg_print(qpte, qp_tenure, "%s QP[%d]_TENURE", privs, qp);
	rhea_reg_print(qpte, qp_portp, "%s QP[%d]_PORTP", privs, qp);
	rhea_reg_print(qpte, qp_sl, "%s QP[%d]_SL	", privs, qp);
	rhea_reg_print(qpte, qp_t, "%s QP[%d]_T", privs, qp);
	rhea_reg_print(qpte, qp_sqhp, "%s QP[%d]_SQHP", privs, qp);
	rhea_reg_print(qpte, qp_sqptp, "%s QP[%d]_SQPTP", privs, qp);
	rhea_reg_print(qpte, qp_sqwsize, "%s QP[%d]_SQWSIZE", privs, qp);
	rhea_reg_print(qpte, qp_sqsize, "%s QP[%d]_SQSIZE", privs, qp);
	rhea_reg_print(qpte, qp_sigt, "%s QP[%d]_SIGT", privs, qp);
	rhea_reg_print(qpte, qp_wqecnt, "%s QP[%d]_WQECNT", privs, qp);
	rhea_reg_print(qpte, qp_rq1a, "%s QP[%d]_RQ1A", privs, qp);
	rhea_reg_print(qpte, qp_rq1c, "%s QP[%d]_RQ1C", privs, qp);
	rhea_reg_print(qpte, qp_rq1hp, "%s QP[%d]_RQ1HP", privs, qp);
	rhea_reg_print(qpte, qp_rq1ptp, "%s QP[%d]_RQ1PTP", privs, qp);
	rhea_reg_print(qpte, qp_rq1size, "%s QP[%d]_RQ1SIZE", privs, qp);
	rhea_reg_print(qpte, qp_rq1wsize, "%s QP[%d]_RQ1WSIZE", privs, qp);
	rhea_reg_print(qpte, qp_rq2a, "%s QP[%d]_RQ2A", privs, qp);
	rhea_reg_print(qpte, qp_rq2c, "%s QP[%d]_RQ2C", privs, qp);
	rhea_reg_print(qpte, qp_rq2hp, "%s QP[%d]_RQ2HP", privs, qp);
	rhea_reg_print(qpte, qp_rq2ptp, "%s QP[%d]_RQ2PTP", privs, qp);
	rhea_reg_print(qpte, qp_rq2size, "%s QP[%d]_RQ2SIZE", privs, qp);
	rhea_reg_print(qpte, qp_rq2wsize, "%s QP[%d]_RQ2WSIZE", privs, qp);
	rhea_reg_print(qpte, qp_rq2th, "%s QP[%d]_RQ2TH", privs, qp);
	rhea_reg_print(qpte, qp_rq3a, "%s QP[%d]_RQ3A", privs, qp);
	rhea_reg_print(qpte, qp_rq3c, "%s QP[%d]_RQ3C", privs, qp);
	rhea_reg_print(qpte, qp_rq3hp, "%s QP[%d]_RQ3HP", privs, qp);
	rhea_reg_print(qpte, qp_rq3ptp, "%s QP[%d]_RQ3PTP", privs, qp);
	rhea_reg_print(qpte, qp_rq3size, "%s QP[%d]_RQ3SIZE", privs, qp);
	rhea_reg_print(qpte, qp_rq3wsize, "%s QP[%d]_RQ3WSIZE", privs, qp);
	rhea_reg_print(qpte, qp_rq3th, "%s QP[%d]_RQ3TH", privs, qp);
	rhea_reg_print(qpte, qp_pd, "%s QP[%d]_PD", privs, qp);
	rhea_reg_print(qpte, qp_scqn, "%s QP[%d]_SCQN", privs, qp);
	rhea_reg_print(qpte, qp_rcqn, "%s QP[%d]_RCQN", privs, qp);
	rhea_reg_print(qpte, qp_aeqn, "%s QP[%d]_AEQN", privs, qp);
	rhea_reg_print(qpte, qp_ram, "%s QP[%d]_RAM", privs, qp);
	rhea_reg_print(qpte, qp_lpn, "%s QP[%d]_LPN", privs, qp);
	rhea_reg_print(qpte, qp_rpphws, "%s QP[%d]_RPPHWS", privs, qp);
	rhea_reg_print(qpte, qp_rpphws2, "%s QP[%d]_RPPHWS2", privs, qp);
}

void rhea_qp_dump(struct rhea_gen *gen, struct rhea_qp *qp)
{
	u64 qpa = 0;
	struct rhea_gen_base *gb = &gen->base;
	enum hea_priv_mode priv = HEA_PRIV_SUPER;

	if (NULL == gen || NULL == qp)
		return;

	switch (priv) {
	default:
		rhea_error("bad privilege mode");
		return;
	case HEA_PRIV_SUPER:
		qpa = rhea_reg_print(gb, g_qpsba, "G_QPSBA");
		break;
	case HEA_PRIV_PRIV:
		qpa = rhea_reg_print(gb, g_qppba, "G_QPPBA");
		break;
	case HEA_PRIV_USER:
		qpa = rhea_reg_print(gb, g_qpuba, "G_QPUBA");
		break;
	}

	qpa = hea_get_u64_bits(qpa, 1, 63);
	if (qpa == 0x00003ffffffff000ULL) {
		rhea_warning("%s QPs not configured",
			     rhea_priv_mode_str[priv]);
		return;
	}

	rhea_qpte_dump(qp->qpt, rhea_priv_mode_str[priv], qp->id);
}
