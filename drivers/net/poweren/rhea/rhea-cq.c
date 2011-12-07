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

#include "rhea-cq.h"
#include "rhea-base.h"

struct rhea_context_cqs {
	struct rhea_qlist list;
	struct rhea_qinfo sp_info;
	struct rhea_qinfo p_info;
	struct rhea_qinfo u_info;
};

static struct rhea_context_cqs s_context_cqs;

static struct rhea_qinfo *rhea_get_cqinfo(enum hea_priv_mode priv,
					  struct rhea_context_cqs *context)
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

unsigned rhea_cq_init(struct rhea_gen_base *rhea_base)
{
	u64 reg;
	unsigned cap;
	unsigned num;

	reg = in_be64(&rhea_base->g_cqtsz);
	cap = hea_get_u64_bits(reg, 0, 15);	/* internal CQ */

	out_be64(&rhea_base->g_cqtsz, cap);

	num = cap + 1;

	memset(&s_context_cqs, 0, sizeof(s_context_cqs));

	rhea_ql_alloc_init(&s_context_cqs.list, num);

	return num;
}

void rhea_cq_fini(struct rhea_gen_base *rhea_base)
{
	if (NULL == rhea_base)
		return;

	rhea_ql_alloc_fini(&s_context_cqs.list);
}

u64 rhea_cq_qbase_init(struct rhea_gen_base *rhea_base,
		       enum hea_priv_mode priv,
		       unsigned num, int lg,
		       u64 addr)
{
	unsigned size;
	u64 addr_new;
	struct rhea_qinfo *qi;

	qi = rhea_get_cqinfo(priv, &s_context_cqs);
	if (NULL == qi) {
		rhea_error("Could not find queue information");
		return 0;
	}

	size = rhea_q_qbase_init(qi, num, priv, lg, addr, "CQ", &addr_new);
	if (0 == size) {
		rhea_error("Was not able to map the CQ");
		BUG_ON(0 == size);
	}

	switch (priv) {
	case HEA_PRIV_SUPER:
		/* set base for super privilege event queues */
		out_be64(&rhea_base->g_cqsba, addr_new);
		break;

	case HEA_PRIV_PRIV:
		/* set base for privilege event queues */
		out_be64(&rhea_base->g_cqpba, addr_new);
		break;

	case HEA_PRIV_USER:
		/* set base for user event queues */
		out_be64(&rhea_base->g_cquba, addr_new);
		break;

	default:
		rhea_error("Invalid privilege type");
		return 0;
	}

	return addr + size;
}

void rhea_cq_qbase_fini(struct rhea_gen_base *rhea_base,
			enum hea_priv_mode priv)
{
	struct rhea_qinfo *qi;

	if (NULL == rhea_base)
		return;

	qi = rhea_get_cqinfo(priv, &s_context_cqs);
	if (NULL == qi || 0 == qi->base.va)
		return;

	rhea_q_qbase_fini(qi);
}

static inline struct rhea_cq *_rhea_cq_alloc(void)
{
	struct rhea_cq *cq;
	int id;

	cq = rhea_align_alloc(sizeof(*cq), __alignof__(*cq), GFP_KERNEL);
	if (cq == NULL)
		return NULL;

	id = rhea_ql_alloc(&s_context_cqs.list, cq);
	if (id == -1) {
		rhea_align_free(cq, sizeof(*cq));
		return NULL;
	}

	cq->id = id;

	return cq;
}

static inline int rhea_cq_base_sz(ulong sz)
{
	ulong count = 0;

	/* finds only 2^x */
	/* find bits before leading 1 */
	while (sz > 0) {
		sz >>= 1;
		++count;
	}

	/* correct bit count */
	if (count)
		--count;

	/* minimum number is 0xC */
	if (0xC > count)
		count = 0xC;

	return count;
}

static void rhea_cq_reset(struct rhea_cqte *cqte)
{
	u64 reg;

	if (NULL == cqte) {
		rhea_error("Invalid parameter passed in!");
		return;
	}

	/* CQx_AER : Clear affiliated errors */
	out_be64(&cqte->cq_aer, 0x0ULL);

	/* make sure that the CQ is disabled and the error is cleared */
	reg = 0x0ULL;
	reg = hea_set_u64_bits(reg, 1, 23, 23);
	out_be64(&cqte->cq_c, reg);

	/* Wait until the Disable Complete bit is set */
	while ((in_be64(&cqte->cq_c) & hea_set_u64_bits(0x0ULL, 1, 1, 1)) == 0)
		continue;

	out_be64(&cqte->cq_hcr, 0);
	out_be64(&cqte->cq_herr, 0);
	out_be64(&cqte->cq_ptp, 0);
	out_be64(&cqte->cq_tp, 0);
	out_be64(&cqte->cq_fec, 0);
	out_be64(&cqte->cq_feca, 0);
	out_be64(&cqte->cq_ep, 0);
	out_be64(&cqte->cq_eq, 0);
	out_be64(&cqte->cq_n0, 0);
	out_be64(&cqte->cq_n1, 0);
	out_be64(&cqte->cq_hp, 0);
	out_be64(&cqte->cq_base, 0);
	out_be64(&cqte->cq_sm0, 0);
	out_be64(&cqte->cq_sc, 0);
	out_be64(&cqte->cq_pd, 0);
	out_be64(&cqte->cq_hwcnt, 0);

	/* CQx_AER : Clear affiliated errors */
	out_be64(&cqte->cq_aer, 0x0ULL);
}

static int rhea_cq_create_internal(struct rhea_qinfo *qi,
				   struct rhea_cq *cq,
				   unsigned pgs,
				   unsigned lpar,
				   unsigned cq_token,
				   struct hea_cq_cfg *cq_cfg)
{
	int rc = 0;
	u64 reg;
	unsigned sz;

	if (NULL == cq_cfg)
		return -EINVAL;

	/* reset CQ and set all registers to a default values */
	rhea_cq_reset(cq->cqt);

	rc = rhea_pt_alloc(qi, &cq->pt, &cq->q,
			   pgs * qi->dev_page_sz,
			   cq_cfg->hw_managed, cq_cfg->cqe_auto_toggle);
	if (rc) {
		rhea_error("Was not able to allocate CQ ptp");
		return rc;
	}

	/* get size for each cqe */
	cq->cqe_size = sizeof(struct hea_cqe);

	/* get number of CQEs which fit into CQ */
	cq->cqe_count = cq->q.size / cq->cqe_size;

	if (cq_cfg->hw_managed && HEA_CQ_MAX_LENGTH < cq->cqe_count) {
		rhea_error("The maximum CQ length is: %u", HEA_CQ_MAX_LENGTH);
		return -EINVAL;
	}

	/* save pointer to CQ */
	cq->cqe_begin = (struct hea_cqe *)cq->q.va;

	reg = 0;
	/* LPAR ID is valid */
	reg = hea_set_u64_bits(reg, 1, 0, 0);
	/* set LPAR ID */
	reg = hea_set_u64_bits(reg, lpar, 56, 63);

	reg = hea_set_u64_bits(reg, cq_cfg->cache_inject, 8, 9);
	reg = hea_set_u64_bits(reg, cq_cfg->target_cache, 10, 11);

	out_be64(&cq->cqt->cq_hcr, reg);

	/* save ptp address */
	out_be64(&cq->cqt->cq_ptp, cq->pt.pa);

	if (cq_cfg->hw_managed) {
		/* set the CQ base pointer */
		reg = cq->q.pa;

		/* CQ Warning Threshold */
		reg = hea_set_u64_bits(reg, 0, 53, 55);

		/* CQ Size */
		sz = rhea_cq_base_sz(cq->q.size);
		reg = hea_set_u64_bits(reg, sz, 56, 63);
		out_be64(&cq->cqt->cq_base, reg);

		/* head pointer */
		out_be64(&cq->cqt->cq_hp, cq->q.pa);

		/* set CQx_SM to enable all threads for dispatch */
		out_be64(&cq->cqt->cq_sm0, 0xffffffffffffffffULL);

		/* set the tenure to 31 */
		reg = hea_set_u64_bits(0x0ULL, 15, 12, 15);
		out_be64(&cq->cqt->cq_sc, reg);
	} else {
		/* CQx_HP and CQx_BASE are not required in software
		 * managed mode */
		out_be64(&cq->cqt->cq_hp, 0x0ULL);
		out_be64(&cq->cqt->cq_base, 0x0ULL);

		/* set available complication queue elements */
		out_be64(&cq->cqt->cq_fec, cq->cqe_count);
	}

	/* event queue link */
	/* set token */
	reg = hea_set_u64_bits(0x0ULL, cq_token, 32, 63);

	/* completion event queue id */
	reg = hea_set_u64_bits(reg, cq->ceq->id, 8, 15);

	/* Async events always go to EQ0 */
	/* JX why isn't this EQ0 */
	reg = hea_set_u64_bits(reg, cq->aeq->id, 24, 31);

	out_be64(&cq->cqt->cq_eq, reg);

	if (HEA_IRQ_COALESING_2 == cq_cfg->irq_type) {
		rhea_debug("Prepare CQ[%u] for Coalesing 2 IRQs", cq->id);
		/* Generate event on solicited or unsolicited completion */
		reg = hea_set_u64_bits(0x0ULL, 1, 0, 0);
		out_be64(&cq->cqt->cq_n1, reg);
	}

	/* enable the CQ */
	reg = hea_set_u64_bits(0ull, 1, 0, 0);

	/* enable HW managed queue */
	if (cq_cfg->hw_managed) {
		/* enable queue thresholding */
		reg = hea_set_u64_bits(reg, 1, 5, 5);
		/* hardware managed */
		reg = hea_set_u64_bits(reg, 1, 6, 6);
		/* enable the ordering ticket */
		reg = hea_set_u64_bits(reg, 1, 7, 7);
	}

	if (HEA_IRQ_COALESING_2 == cq_cfg->irq_type) {
		/* Perform Event generation
		 * processing only when EP=0 and N1=1
		 * */
		reg = hea_set_u64_bits(reg, 0, 3, 3);
	}

	out_be64(&cq->cqt->cq_c, reg);

	return rc;
}

static inline void rhea_cq_internal_free(struct rhea_cq *cq)
{
	if (cq == NULL)
		return;

	/* reset CQ and set all registers to a default value */
	rhea_cq_reset(cq->cqt);

	rhea_ql_free(&s_context_cqs.list, cq->id);

	rhea_align_free(cq, sizeof(*cq));
}

struct rhea_cq *_rhea_cq_get(unsigned int cq_id)
{
	struct rhea_cq *cq;

	cq = rhea_ql_get(&s_context_cqs.list, cq_id);
	if (NULL == cq)
		rhea_error("Was not able to get CQ");

	return cq;
}

struct rhea_cq *rhea_cq_create(struct rhea_eq *ceq,
			       struct rhea_eq *aeq,
			       struct hea_process *process,
			       struct hea_cq_cfg *cq_cfg)
{
	unsigned pgs;
	struct rhea_cq *cq;
	struct rhea_qinfo *qi;
	int rc;

	if (NULL == aeq || NULL == aeq) {
		rhea_error("No EQ passed in");
		return NULL;
	}

	if (NULL == cq_cfg || NULL == process)
		return NULL;

	if (HEA_IRQ_COALESING_2 == cq_cfg->irq_type &&
	    HEA_IRQ_COALESING_2 != aeq->eq_cfg.irq_type &&
	    HEA_IRQ_COALESING_2 != ceq->eq_cfg.irq_type) {
		rhea_error("None of the EQs have IRQs enabled.");
		return NULL;
	}

	qi = rhea_get_cqinfo(HEA_PRIV_SUPER, &s_context_cqs);
	if (NULL == qi || 0 == qi->base.va) {
		rhea_error("Could not find queue information");
		return NULL;
	}

	cq = _rhea_cq_alloc();
	if (NULL == cq) {
		rhea_error("Could not allocate new CQ memory");
		return NULL;
	}
	/* save configuraqtion data */
	cq->cfg = *cq_cfg;
	cq->cqt = rhea_qte(qi, cq->id, RHEA_ADDRESS_VIRTUAL);
	cq->cq_info = qi;
	cq->aeq = aeq;
	cq->ceq = ceq;

	cq->process = *process;

	pgs = ALIGN_UP(sizeof(struct hea_cqe) * cq_cfg->cqe_count,
		       qi->dev_page_sz);
	pgs /= qi->dev_page_sz;

	rc = rhea_cq_create_internal(qi, cq, pgs, process->lpar, cq->id,
				     cq_cfg);
	if (rc) {
		rhea_cq_internal_free(cq);
		rhea_error("Could not create CQ");
		return NULL;
	}

	return cq;
}

int rhea_cq_destroy(struct rhea_cq *cq)
{
	struct rhea_qinfo *qi;

	if (NULL == cq)
		return -EINVAL;

	qi = rhea_get_cqinfo(HEA_PRIV_SUPER, &s_context_cqs);
	if (NULL == qi || 0 == qi->base.va)
		return -EINVAL;

	rhea_pt_free(&cq->pt, &cq->q, cq->cfg.hw_managed);

	rhea_cq_internal_free(cq);

	return 0;
}

int rhea_cq_feature_get(struct rhea_cq *cq, enum hea_cq_feature_get feature,
			u64 *value)
{
	int rc = 0;

	if (NULL == cq || NULL == value)
		return -EINVAL;

	switch (feature) {
	case HEA_CQ_TOKEN_GET:
		*value = cq->id;
		break;

	case HEA_CQ_AER_GET:
		*value = in_be64(&cq->cqt->cq_aer);
		break;

	case HEA_CQ_ENABLED_GET:
		{
			u64 reg = in_be64(&cq->cqt->cq_c);
			*value = hea_get_u64_bits(reg, 0, 0);
		}
		break;

	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

int _rhea_cq_error_reset(struct rhea_cq *cq, u64 error_reg)
{
	int rc = 0;
	u64 reg;

	/* clear the errors */
	out_be64(&cq->cqt->cq_aer, error_reg);

	/* get current CQ state */
	reg = in_be64(&cq->cqt->cq_c);

	/* find out if the CQ is in error state or disabled */
	if (hea_get_u64_bits(reg, 2, 2) || 0 == hea_get_u64_bits(reg, 0, 0)) {
		/* reset error bit */
		reg = hea_set_u64_bits(reg, 1, 23, 23);
		out_be64(&cq->cqt->cq_c, reg);

		/* enable CQ again */
		reg = hea_set_u64_bits(reg, 1, 0, 0);
		out_be64(&cq->cqt->cq_c, reg);
	}

	return rc;
}

int rhea_cq_feature_set(struct rhea_cq *cq, enum hea_cq_feature_set feature,
			u64 value)
{
	int rc = 0;

	if (NULL == cq)
		return -EINVAL;

	switch (feature) {
	case HEA_CQ_AER_SET:
		{
			rc = _rhea_cq_error_reset(cq, value);
		}
		break;

	default:
		{
			rc = -EINVAL;
		}
		break;
	}

	return rc;
}

int rhea_cq_mapinfo_get(struct rhea_cq *cq,
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

	qi = rhea_get_cqinfo(priv, &s_context_cqs);
	if (NULL == qi || 0 == qi->base.va)
		return -EINVAL;

	/* get correct address type */
	addr_type = ((use_va) ? RHEA_ADDRESS_VIRTUAL : RHEA_ADDRESS_PHYSICAL);

	/* get pointer to QP page */
	*pointer = rhea_qte(qi, cq->id, addr_type);

	/* get size of page */
	*size = qi->os_page_sz;

	if (NULL == *pointer || 0 == size) {
		rhea_error("Invalid CQ setup");
		return -EINVAL;
	}

	return rc;
}

void rhea_cqte_dump(struct rhea_cqte *cqte, const char *privs, unsigned cq)
{
	rhea_reg_print(cqte, cq_hcr, "%s CQ[%d]_HCR", privs, cq);
	rhea_reg_print(cqte, cq_c, "%s CQ[%d]_C", privs, cq);
	rhea_reg_print(cqte, cq_herr, "%s CQ[%d]_AERR", privs, cq);
	rhea_reg_print(cqte, cq_aer, "%s CQ[%d]_AER", privs, cq);
	rhea_reg_print(cqte, cq_ptp, "%s CQ[%d]_PTP", privs, cq);
	rhea_reg_print(cqte, cq_tp, "%s CQ[%d]_TP", privs, cq);
	rhea_reg_print(cqte, cq_fec, "%s CQ[%d]_FEC", privs, cq);
	rhea_reg_print(cqte, cq_feca, "%s CQ[%d]_FECA", privs, cq);
	rhea_reg_print(cqte, cq_ep, "%s CQ[%d]_EP", privs, cq);
	rhea_reg_print(cqte, cq_eq, "%s CQ[%d]_EQ", privs, cq);
	rhea_reg_print(cqte, cq_n0, "%s CQ[%d]_N0", privs, cq);
	rhea_reg_print(cqte, cq_n1, "%s CQ[%d]_N1", privs, cq);
	rhea_reg_print(cqte, cq_hp, "%s CQ[%d]_HP", privs, cq);
	rhea_reg_print(cqte, cq_base, "%s CQ[%d]_BASE", privs, cq);
	rhea_reg_print(cqte, cq_sm0, "%s CQ[%d]_SM", privs, cq);
	rhea_reg_print(cqte, cq_sc, "%s CQ[%d]_SC", privs, cq);
	rhea_reg_print(cqte, cq_pd, "%s CQ[%d]_PD", privs, cq);
	rhea_reg_print(cqte, cq_hwcnt, "%s CQ[%d]_HWCNT", privs, cq);
}

void rhea_cq_dump(struct rhea_gen *gen, struct rhea_cq *cqp)
{
	u64 cqa = 0;
	struct rhea_gen_base *gb = &gen->base;
	enum hea_priv_mode priv = HEA_PRIV_SUPER;

	if (NULL == gen || NULL == cqp)
		return;

	switch (priv) {
	default:
		rhea_error("bad privilege mode");
		return;
	case HEA_PRIV_SUPER:
		cqa = rhea_reg_print(gb, g_cqsba, "G_CQSBA");
		break;
	case HEA_PRIV_PRIV:
		cqa = rhea_reg_print(gb, g_cqpba, "G_CQPBA");
		break;
	case HEA_PRIV_USER:
		cqa = rhea_reg_print(gb, g_cquba, "G_CQUBA");
		break;
	}

	cqa = hea_get_u64_bits(cqa, 1, 63);
	if (cqa == 0x00003ffffffff000ULL) {
		rhea_warning("Warning: %s CQs not configured",
			     rhea_priv_mode_str[priv]);
		return;
	}

	rhea_cqte_dump(cqp->cqt, rhea_priv_mode_str[priv], cqp->id);
}
