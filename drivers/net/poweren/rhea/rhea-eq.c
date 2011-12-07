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

#include "rhea-eq.h"
#include "rhea-base.h"

#include <asm/irq.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include "rhea-funcs.h"

struct rhea_context_eqs {
	struct rhea_qlist list;
	struct rhea_qinfo sp_info;
	struct rhea_qinfo p_info;
};

static struct rhea_context_eqs s_context_eqs;

static struct rhea_qinfo *rhea_get_eqinfo(enum hea_priv_mode priv,
					  struct rhea_context_eqs *context)
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

	default:
		return NULL;
	}

	return qi;
}

unsigned rhea_eq_init(struct rhea_gen_base *rhea_base)
{
	u64 reg;
	u64 cap;
	unsigned num;

	reg = in_be64(&rhea_base->g_eqtsz);

	cap = hea_get_u64_bits(reg, 0, 15);	/* internal EQ */

	out_be64(&rhea_base->g_eqtsz, cap);

	num = cap + 1;

	memset(&s_context_eqs, 0, sizeof(s_context_eqs));

	rhea_ql_alloc_init(&s_context_eqs.list, num);

	return num;
}

void rhea_eq_fini(struct rhea_gen_base *rhea_base)
{
	if (NULL == rhea_base)
		return;

	rhea_ql_alloc_fini(&s_context_eqs.list);
}

u64 rhea_eq_qbase_init(struct rhea_gen_base *rhea_base,
			     enum hea_priv_mode priv,
			     int num, int lg, u64 addr)
{
	unsigned size;
	u64 addr_new;
	struct rhea_qinfo *qi;

	qi = rhea_get_eqinfo(priv, &s_context_eqs);
	if (NULL == qi) {
		rhea_error("Did not find Queue information for EQ");
		return 0;
	}

	rhea_debug("addr: 0x%llx", addr);

	size = rhea_q_qbase_init(qi, num, priv, lg, addr, "EQ", &addr_new);
	if (0 == size) {
		rhea_error("Was not able to map the EQ");
		BUG_ON(0 == size);
	}

	switch (priv) {
	case HEA_PRIV_SUPER:
		/* set base for super privilege event queues */
		out_be64(&rhea_base->g_eqsba, addr_new);
		break;

	case HEA_PRIV_PRIV:
		/* set base for privilege event queues */
		out_be64(&rhea_base->g_eqpba, addr_new);
		break;

	default:
		return 0;
	}

	rhea_debug("return addr: 0x%llx", addr);

	return addr_new + size;
}

void rhea_eq_qbase_fini(struct rhea_gen_base *rhea_base,
			enum hea_priv_mode priv)
{
	struct rhea_qinfo *qi;

	if (NULL == rhea_base)
		return;

	qi = rhea_get_eqinfo(priv, &s_context_eqs);
	if (NULL == qi || 0 == qi->base.va)
		return;

	rhea_q_qbase_fini(qi);
}

static struct rhea_eq *_rhea_eq_alloc(void)
{
	struct rhea_eq *eq;
	int id;

	eq = rhea_align_alloc(sizeof(*eq), __alignof__(*eq), GFP_KERNEL);
	if (eq == NULL)
		return NULL;

	id = rhea_ql_alloc(&s_context_eqs.list, eq);
	if (0 > id) {
		rhea_align_free(eq, sizeof(*eq));
		return NULL;
	}

	eq->id = id;

	return eq;
}

static void rhea_eq_reset(struct rhea_eqte *eqte)
{
	u64 reg;

	if (NULL == eqte)
		return;

	/* EQx_AER : Clear affiliated errors */
	out_be64(&eqte->eq_aer, 0x0ULL);

	reg = 0x0ULL;
	reg = hea_set_u64_bits(reg, 1, 23, 23);
	out_be64(&eqte->eq_c, reg);

	/* Wait until the Disable Complete bit is set */
	while ((in_be64(&eqte->eq_c) & hea_set_u64_bits(0ULL, 1, 1, 1)) == 0)
		continue;

	reg = 0x0ULL;
	out_be64(&eqte->eq_hcr, reg);
	out_be64(&eqte->eq_herr, reg);
	out_be64(&eqte->eq_ptp, reg);
	out_be64(&eqte->eq_ssba, reg);
	out_be64(&eqte->eq_psba, reg);
	out_be64(&eqte->eq_cec, reg);
	out_be64(&eqte->eq_meql, reg);
	out_be64(&eqte->eq_xisbi, reg);
	out_be64(&eqte->eq_xisc, reg);
	out_be64(&eqte->eq_it, reg);

	/* EQx_AER : Clear affiliated errors */
	out_be64(&eqte->eq_aer, 0x0000000000000000ULL);

	/* EQx_TP  : Clear tail pointer */
	out_be64(&eqte->eq_tp, 0x000000000fddf000ULL);

}

static inline void rhea_eq_internal_free(struct rhea_eq *eq)
{
	if (eq == NULL)
		return;

	/* reset EQ registers to default values */
	rhea_eq_reset(eq->eqt);

	rhea_ql_free(&s_context_eqs.list, eq->id);

	rhea_align_free(eq, sizeof(*eq));
}

static int rhea_eq_create_internal(struct rhea_qinfo *qi, struct rhea_eq *eq,
				   unsigned pgs, unsigned lpar,
				   struct hea_eq_cfg *eq_cfg)
{
	int rc = 0;
	u64 reg;

	if (NULL == qi || NULL == eq || NULL == eq_cfg) {
		rhea_error("Invalid parameter");
		return -EINVAL;
	}

	/* reset EQ registers to default values */
	rhea_eq_reset(eq->eqt);

	rc = rhea_pt_alloc(qi, &eq->pt, &eq->q, pgs * qi->dev_page_sz, 0, 0);
	if (rc) {
		rhea_error("Was not able to allocate EQ ptp");
		return rc;
	}

	/* get size for each EQE */
	eq->eqe_size = sizeof(struct hea_eqe);

	/* get number of EQEs */
	eq->eqe_count = eq->q.size / eq->eqe_size;

	/* get address to EQ */
	eq->eqe_begin = (struct hea_eqe *)eq->q.va;

	reg = 0x0ULL;
	/* make LPAR valid */
	reg = hea_set_u64_bits(reg, 1, 0, 0);
	/* set LPAR */
	reg = hea_set_u64_bits(reg, lpar, 56, 63);
	if (HEA_IRQ_COALESING_2 == eq_cfg->irq_type) {
		/* Disable Writing Primary Summary Byte */
		reg = hea_set_u64_bits(reg, 0, 8, 8);

		/* Enable Sending Interrupts to Processor(s) */
		reg = hea_set_u64_bits(reg, 1, 9, 9);

		/*
		 * Either the Primary Summary Byte is written OR a
		 * hardware interrupt is generated
		 */
		reg = hea_set_u64_bits(reg, 0, 10, 10);
	}
	out_be64(&eq->eqt->eq_hcr, reg);

	/* set ptp address */
	out_be64(&eq->eqt->eq_ptp, eq->pt.pa);

	/* load summary byte addresses */
	/* this is scary and do not intend to enable it just yet */
	out_be64(&eq->eqt->eq_psba,
		 (ulong)virt_to_phys(&eq->summary_bytes[0]));
	out_be64(&eq->eqt->eq_ssba,
		 (ulong)virt_to_phys(&eq->summary_bytes[1]));

	if (HEA_IRQ_COALESING_2 == eq_cfg->irq_type) {
		reg = 0x0ULL;

		/* Next Interrupt Delay: 0x0000 - 1 tick(s) */
		reg = hea_set_u64_bits(reg, eq_cfg->coalesing2_delay, 0, 15);

		/* Disable Interrupt Timer mode */
		reg = hea_set_u64_bits(reg, 0, 16, 16);

		/* Enable Interrupt Coalescing mode */
		reg = hea_set_u64_bits(reg, 1, 17, 17);

		out_be64(&eq->eqt->eq_it, reg);

		reg = 0x0ULL;

		/* offset for hardware interrupt */
		reg = hea_set_u64_bits(reg, eq->id, 48, 63);
		out_be64(&eq->eqt->eq_xisbi, reg);

		reg = 0x0ULL;
		/* external interrupt sources */
		out_be64(&eq->eqt->eq_xisc, reg);
	}

	reg = 0x0ULL;
	if (HEA_IRQ_COALESING_2 == eq_cfg->irq_type) {
		/* Immediate Interrupt Required */
		reg = hea_set_u64_bits(reg, 1, 16, 16);

		if (HEA_EQ_GEN_COM_EVENT_ENABLE ==
		    eq_cfg->generate_completion_events) {
			/* Enable Completion Event Generation ==>
			 * creates EQEs */
			reg = hea_set_u64_bits(reg, 1, 17, 17);
		}

		/* Disable Writing Secondary Summary Byte */
		reg = hea_set_u64_bits(reg, 0, 18, 18);
	}

	/* enable the EQ */
	reg = hea_set_u64_bits(reg, 1, 0, 0);
	out_be64(&eq->eqt->eq_c, reg);

	return rc;
}

struct rhea_eq *_rhea_eq_get(unsigned int eq_id)
{
	struct rhea_eq *eq;

	eq = rhea_ql_get(&s_context_eqs.list, eq_id);
	if (NULL == eq)
		rhea_error("Was not able to get EQ");

	return eq;
}

struct rhea_eq *rhea_eq_create(struct hea_process *process,
			       struct hea_eq_cfg *eq_cfg)
{
	unsigned pgs;
	struct rhea_eq *eq;
	struct rhea_qinfo *qi;
	int rc;

	if (NULL == eq_cfg || NULL == process)
		return NULL;

	eq = _rhea_eq_alloc();
	if (NULL == eq)
		return NULL;

	qi = rhea_get_eqinfo(HEA_PRIV_SUPER, &s_context_eqs);
	if (NULL == qi || 0 == qi->base.va)
		return NULL;

	/* save config */
	eq->eq_cfg = *eq_cfg;

	eq->eqt = rhea_qte(qi, eq->id, RHEA_ADDRESS_VIRTUAL);
	eq->eq_info = (struct rhea_qinfo *)qi;

	eq->irq.irq_type = eq_cfg->irq_type;

	eq->process = *process;

	/* get page count */
	pgs = ALIGN_UP(sizeof(struct hea_eqe) * eq_cfg->eqe_count,
		       qi->dev_page_sz);
	pgs /= qi->dev_page_sz;

	rc = rhea_eq_create_internal(qi, eq, pgs, process->lpar, eq_cfg);
	if (rc) {
		rhea_error("Was not able to create EQ");
		rhea_eq_internal_free(eq);
		return NULL;
	}

	return eq;
}

int rhea_eq_destroy(struct rhea_eq *eq)
{
	const struct rhea_qinfo *qi;

	if (NULL == eq)
		return -EINVAL;

	qi = rhea_get_eqinfo(HEA_PRIV_SUPER, &s_context_eqs);
	if (NULL == qi || 0 == qi->base.va) {
		rhea_error("Did not find Queue information for EQ");
		return 0;
	}

	/* unload IRQ if we have one installed */
	if (eq->irq.hwirq)
		rhea_interrupts_free(eq);

	rhea_pt_free(&eq->pt, &eq->q, 0);

	rhea_eq_internal_free(eq);

	return 0;
}

int rhea_eq_mapinfo_get(struct rhea_eq *eq,
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

	qi = rhea_get_eqinfo(priv, &s_context_eqs);
	if (NULL == qi || 0 == qi->base.va)
		return -EINVAL;

	/* get correct address type */
	addr_type = ((use_va) ? RHEA_ADDRESS_VIRTUAL : RHEA_ADDRESS_PHYSICAL);

	/* get pointer to QP page */
	*pointer = rhea_qte(qi, eq->id, addr_type);

	/* get size of page */
	*size = qi->os_page_sz;

	if (NULL == *pointer || 0 == *size) {
		rhea_error("EQ not setup correctly");
		return -EINVAL;
	}

	return rc;
}

int rhea_eq_feature_get(struct rhea_eq *eq,
			enum hea_eq_feature_get feature,
			u64 *value)
{
	int rc = 0;

	if (NULL == eq || NULL == value)
		return -EINVAL;

	switch (feature) {
	case HEA_EQ_AER_GET:
		*value = in_be64(&eq->eqt->eq_aer);
		break;

	case HEA_EQ_ENABLED_GET:
		{
			u64 reg = in_be64(&eq->eqt->eq_c);
			*value = hea_get_u64_bits(reg, 0, 0);
		}
		break;

	case HEA_EQ_IRQ_NR_GET:
		*value = (u64)eq->irq.virq;
		break;

	default:
		rc = -1;
		break;
	}

	return rc;
}

int _rhea_eq_error_reset(struct rhea_eq *eq, u64 error_reg)
{
	int rc = 0;
	u64 reg;

	/* clear the errors */
	out_be64(&eq->eqt->eq_aer, error_reg);

	/* get current CQ state */
	reg = in_be64(&eq->eqt->eq_c);

	/* find out if the CQ is in error state or disabled */
	if (hea_get_u64_bits(reg, 2, 2) || 0 == hea_get_u64_bits(reg, 0, 0)) {
		/* reset error bit */
		reg = hea_set_u64_bits(reg, 1, 23, 23);
		out_be64(&eq->eqt->eq_c, reg);

		/* enable CQ again */
		reg = hea_set_u64_bits(reg, 1, 0, 0);
		out_be64(&eq->eqt->eq_c, reg);
	}

	return rc;
}

int rhea_eq_feature_set(struct rhea_eq *eq,
			enum hea_eq_feature_set feature,
			u64 value)
{
	int rc = 0;

	if (NULL == eq)
		return -EINVAL;

	switch (feature) {
	case HEA_EQ_AER_SET:
		{
			rc = _rhea_eq_error_reset(eq, value);
		}
		break;

	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static void rhea_eqte_dump(struct rhea_eqte *eqte, const char *privs,
			   unsigned eq)
{
	rhea_reg_print(eqte, eq_hcr, "%s EQ[%d]_HCR", privs, eq);
	rhea_reg_print(eqte, eq_c, "%s EQ[%d]_C", privs, eq);
	rhea_reg_print(eqte, eq_herr, "%s EQ[%d]_HERR", privs, eq);
	rhea_reg_print(eqte, eq_aer, "%s EQ[%d]_AER", privs, eq);
	rhea_reg_print(eqte, eq_ptp, "%s EQ[%d]_PTP", privs, eq);
	rhea_reg_print(eqte, eq_tp, "%s EQ[%d]_TP", privs, eq);
	rhea_reg_print(eqte, eq_ssba, "%s EQ[%d]_SSBAC", privs, eq);
	rhea_reg_print(eqte, eq_psba, "%s EQ[%d]_PSBA", privs, eq);
	rhea_reg_print(eqte, eq_cec, "%s EQ[%d]_CEC", privs, eq);
	rhea_reg_print(eqte, eq_meql, "%s EQ[%d]_MEQL", privs, eq);
	rhea_reg_print(eqte, eq_xisbi, "%s EQ[%d]_XISBI", privs, eq);
	rhea_reg_print(eqte, eq_xisc, "%s EQ[%d]_XISC", privs, eq);
	rhea_reg_print(eqte, eq_it, "%s EQ[%d]_IT", privs, eq);
}

static void rhea_irq_dump(struct rhea_gen_its *its, const char *privs,
			  int index)
{
	int i;
	u64 reg;

	rhea_reg_print(its, g_its[index], "%s G_ITS[%d]_HCR", privs, index);

	reg = in_be64(&its->g_its[0]);
	for (i = 1; i < 1024; ++i) {
		if (reg != in_be64(&its->g_its[i])) {
			rhea_reg_print(its, g_its[i], "%s G_ITS[%d]_HCR",
				       privs, i);
		}
	}
}

void rhea_eq_dump(struct rhea_gen *gen, struct rhea_eq *eqp, unsigned eq)
{
	u64 eqa = 0;
	struct rhea_gen_base *gb = &gen->base;
	enum hea_priv_mode priv = HEA_PRIV_SUPER;

	if (NULL == gen || NULL == eqp)
		return;

	switch (priv) {
	default:
		return;
		break;
	case HEA_PRIV_SUPER:
		eqa = rhea_reg_print(gb, g_eqsba, "G_EQSBA");
		break;
	case HEA_PRIV_PRIV:
		eqa = rhea_reg_print(gb, g_eqpba, "G_EQPBA");
		break;
	}

	eqa = hea_get_u64_bits(eqa, 1, 63);
	if (eqa == 0x00003ffffffff000ULL) {
		rhea_warning("Warning: %s EQs not configured",
			     rhea_priv_mode_str[priv]);
		return;
	}

	rhea_eqte_dump(eqp->eqt, rhea_priv_mode_str[priv], eq);

	rhea_irq_dump(&gen->its, rhea_priv_mode_str[priv], eqp->id);
}

#define EQFMT "eq%03u"

/* nee hea_reg_interrupts */
int rhea_interrupts_setup(struct rhea_eq *eqp,
			  const char *name,
			  unsigned hwirq_base,
			  unsigned hwirq_count,
			  hea_irq_handler_t irq_handler,
			  void *irq_handler_args)
{
	int err = 0;

	if (NULL == name) {
		rhea_error("Invalid parameter");
		return -EINVAL;
	}

	if (HEA_IRQ_NO == eqp->irq.irq_type) {
		rhea_error("EQ does not support interrupts");
		return -EINVAL;
	}

	if (eqp->irq.hwirq) {
		rhea_error("EQ[%u] has already registered one IRQ", eqp->id);
		return -EINVAL;
	}

	rhea_debug("=> %s: %d %d", __func__, 0, eqp->id);

	eqp->irq.hwirq = eqp->id + hwirq_base;

	/* save IRQ handler */
	eqp->irq.irq_handler = irq_handler;

	/* make sure we pass in a different identifier for each request */
	eqp->irq.irq_handler_args = irq_handler_args ? irq_handler_args : eqp;

	/* create name for memory region */
	memset(eqp->irq.eq_irq_name, 0, sizeof(eqp->irq.eq_irq_name));
	snprintf(eqp->irq.eq_irq_name, sizeof(eqp->irq.eq_irq_name) - 1,
		 "%s-" EQFMT, name, eqp->id);

	eqp->irq.virq = rhea_irq_request(eqp->irq.hwirq,
					 eqp->irq.irq_handler, 0,
					 eqp->irq.eq_irq_name,
					 eqp->irq.irq_handler_args);
	if (NO_IRQ == eqp->irq.virq) {
		rhea_error("%s: failed registering hwirq=%d "
			   "please set rhea.timer",
			   eqp->irq.eq_irq_name, eqp->irq.hwirq);

		err = -EINVAL;
		goto irq_out;
	}

	/* Enable Sending Interrupts to Processor(s) */
	rhea_info("%s: registered with virq %d", eqp->irq.eq_irq_name,
		  eqp->irq.virq);

irq_out:

	rhea_debug("<= %s: %d %d %d", __func__, err, 0, eqp->id);

	return err;
}

void rhea_interrupts_free(struct rhea_eq *eqp)
{
	if (NULL == eqp)
		return;

	if (eqp->irq.hwirq) {
		rhea_info("Free IRQ for EQ: %u (%u,%u)", eqp->id,
			  eqp->irq.hwirq, eqp->irq.virq);

		/* Disable Sending Interrupts to Processor(s) */
		rhea_irq_free(eqp->irq.virq, eqp->irq.irq_handler_args);

		rhea_debug("free completion virq %d for EQ_ID%d",
			   eqp->irq.virq, eqp->id);

		/* reset struct */
		memset(&eqp->irq, 0, sizeof(eqp->irq));
		eqp->irq.irq_type = eqp->eq_cfg.irq_type;
	}

	rhea_debug("<= %s: %d %d %d", __func__, 0, 0, eqp->id);
}
