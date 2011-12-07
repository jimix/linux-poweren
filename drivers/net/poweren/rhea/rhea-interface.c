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

#include "rhea-interface.h"

#include <asm/poweren_hea_eq.h>
#include <asm/poweren_hea_wqe.h>

#include "rhea.h"
#include "rhea-cq.h"
#include "rhea-qp.h"
#include "rhea-rules.h"
#include "rhea-eq0.h"
#include "rhea-channel.h"
#include "rhea-mc-bc-manager.h"

#include <linux/interrupt.h>

/* used to send signal to userspace */
#include <linux/sched.h>
#include <asm/siginfo.h>	/* siginfo */

#define HEA_MAX_EQ_COUNT 128
#define HEA_MAX_CQ_COUNT 128
#define HEA_MAX_QP_COUNT 128
#define HEA_MAX_SESSION_COUNT (4 * HEA_MAX_PPORT_COUNT * \
				HEA_MAX_PPORT_CHANNEL_COUNT)
#define HEA_MAX_CHANNEL_COUNT (HEA_MAX_PPORT_COUNT * MAX_PPORT_CHANNEL_COUNT)

/********************** Datatypes *************************/
struct rhea_session {
	struct rhea_eq *eqs[HEA_MAX_EQ_COUNT];
	struct rhea_cq *cqs[HEA_MAX_CQ_COUNT];
	struct rhea_qp *qps[HEA_MAX_QP_COUNT];
	struct rhea_channel *channels[HEA_MAX_CHANNEL_COUNT];
	struct rhea_hasher *hasher;
	unsigned adapter_number;
	unsigned id;
	spinlock_t lock;
};

struct rhea_sessions {
	struct rhea_session *ids[HEA_MAX_SESSION_COUNT];
	unsigned alloced;
	unsigned alloced_max;
	spinlock_t lock;
};



struct rhea_context {
	unsigned int hasher_used;
	unsigned int hasher_session;
	struct rhea_sessions sessions;
	struct hea_adapter *aps[HEA_MAX_ADAPTERS];

	struct rhea_eq0 eq0;
	struct rhea_bc_mc_uc_manager *manager[HEA_MAX_PPORT_COUNT];
};

/****************** Global Data Structures ***************/

static struct rhea_context s_rhea_context;

/********************** Functions *************************/


extern int rhea_get_version(unsigned int *major, unsigned int *minor,
			    unsigned int *release)
{
	*major = RHEA_MAJOR_VERSION;
	*minor = RHEA_MINOR_VERSION;
	*release = RHEA_RELEASE_VERSION;
	return 0;
}
EXPORT_SYMBOL(rhea_get_version);

int rhea_eq_alloc(unsigned rhea_id,
		  unsigned *eq_id, struct hea_eq_context *context_eq)
{
	int rc = 0;
	struct rhea_eq *eq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == eq_id || NULL == context_eq)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	/* get new EQ */
	eq = rhea_eq_create(&context_eq->process, &context_eq->cfg);
	if (NULL == eq) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -ENOMEM;
	}

	/* save pointer in session */
	sessions->ids[rhea_id]->eqs[eq->id] = eq;

	/* pass new id to caller */
	*eq_id = eq->id;

	spin_unlock(&sessions->ids[rhea_id]->lock);

	if (0)
		rhea_eq_dumps(rhea_id, eq->id);

	return rc;
}
EXPORT_SYMBOL(rhea_eq_alloc);

int rhea_eq_dumps(unsigned rhea_id, unsigned eq_id)
{
	struct hea_adapter *ap;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->eqs, eq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get adapter */
	ap = s_rhea_context.aps[sessions->ids[rhea_id]->adapter_number];

	rhea_eq_dump(ap->mmio, sessions->ids[rhea_id]->eqs[eq_id],
		     sessions->ids[rhea_id]->eqs[eq_id]->id);

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return 0;
}
EXPORT_SYMBOL(rhea_eq_dumps);

static int _rhea_eq_free(unsigned rhea_id, unsigned eq_id)
{
	int rc = 0;
	struct rhea_eq *eq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	/* get EQ Pointer */
	eq = sessions->ids[rhea_id]->eqs[eq_id];

	/* make sure that nobody else tries to do the same!! */
	sessions->ids[rhea_id]->eqs[eq_id] = NULL;

	/* destroy EQ */
	rc = rhea_eq_destroy(eq);

	return rc;
}

int rhea_eq_free(unsigned rhea_id, unsigned eq_id)
{
	int rc = 0;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->eqs, eq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	rc = _rhea_eq_free(rhea_id, eq_id);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Was not able to free EQ");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_eq_free);

int rhea_eq_table(unsigned rhea_id,
		  unsigned eq_id,
		  struct hea_eqe **eqe_begin,
		  unsigned *eqe_size, unsigned *eqe_count)
{
	int rc = 0;
	struct rhea_eq *eq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == eqe_size || NULL == eqe_count)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->eqs, eq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get EQ Pointer */
	eq = sessions->ids[rhea_id]->eqs[eq_id];

	if (NULL == eq->eqe_begin || 0 == eq->eqe_count || 0 == eq->eqe_size) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("EQ not setup correctly");
		return -EINVAL;
	}

	/* pass parameters to caller */
	*eqe_begin = eq->eqe_begin;
	*eqe_count = eq->eqe_count;
	*eqe_size = eq->eqe_size;

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;

}
EXPORT_SYMBOL(rhea_eq_table);

int rhea_eq_mapinfo(unsigned rhea_id,
		    unsigned eq_id,
		    enum hea_priv_mode priv,
		    void **pointer, unsigned *size, unsigned use_va)
{
	int rc = 0;
	struct rhea_eq *eq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == size)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->eqs, eq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;

	}

	eq = sessions->ids[rhea_id]->eqs[eq_id];

	rc = rhea_eq_mapinfo_get(eq, priv, pointer, size, use_va);
	if (rc) {
		rhea_error("Was not able to obtain EQ mapping information");
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_eq_mapinfo);

int _rhea_eq_feature_get(unsigned rhea_id,
		unsigned eq_id,
		enum hea_eq_feature_get feature, u64 *value)
{
	int rc = 0;
	struct rhea_eq *eq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == value)
		return -EINVAL;

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->eqs, eq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;

	}

	eq = sessions->ids[rhea_id]->eqs[eq_id];

	rc = rhea_eq_feature_get(eq, feature, value);
	if (rc) {
		rhea_error("Was not able to get EQ feature");
		return -EINVAL;
	}

	return rc;
}


int rhea_eq_get(unsigned rhea_id,
		unsigned eq_id,
		enum hea_eq_feature_get feature, u64 *value)
{
	int rc = 0;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == value)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->eqs, eq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;

	}

	rc = _rhea_eq_feature_get(rhea_id, eq_id, feature, value);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Was not able to get EQ feature");
		return -EINVAL;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_eq_get);

int rhea_eq_set(unsigned rhea_id,
		unsigned eq_id,
		enum hea_eq_feature_set feature, u64 value)
{
	int rc = 0;
	struct rhea_eq *eq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->eqs, eq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;

	}

	eq = sessions->ids[rhea_id]->eqs[eq_id];

	rc = rhea_eq_feature_set(eq, feature, value);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Was not able to set EQ feature");
		return -EINVAL;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_eq_set);

static int _rhea_interrupt_setup(struct rhea_eq *eq, struct hea_adapter *ap,
				 hea_irq_handler_t irq_handler,
				 void *irq_handler_args)
{
	int rc = 0;

	if (NULL == eq || NULL == ap)
		return -EINVAL;

	rc = rhea_interrupts_setup(eq, ap->name, ap->hwirq_base,
				   ap->hwirq_count, irq_handler,
				   irq_handler_args);
	if (rc) {
		rhea_error("Unable to setup IRQ");
		return -EINVAL;
	}

	return rc;
}


int rhea_interrupt_setup(unsigned int rhea_id,
			 unsigned int eq_id,
			 hea_irq_handler_t irq_handler, void *irq_handler_args)
{
	int rc = 0;

	struct rhea_eq *eq;
	struct hea_adapter *ap;

	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == irq_handler) {
		rhea_error("Invalid Interrupt Handler");
		return -EINVAL;
	}

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->eqs, eq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	eq = sessions->ids[rhea_id]->eqs[eq_id];

	if (HEA_IRQ_COALESING_2 != eq->irq.irq_type) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("EQ[%u] is not setup to support interrupts",
			   eq->id);
		return -EINVAL;
	}

	if (!RHEA_VALID_INSTANCE_CHECK
	    (s_rhea_context.aps, sessions->ids[rhea_id]->adapter_number)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);

		rhea_error("Invalid adapter number");

		return -EINVAL;
	}

	ap = s_rhea_context.aps[sessions->ids[rhea_id]->adapter_number];

	rc = _rhea_interrupt_setup(eq, ap, irq_handler, irq_handler_args);
	if (rc) {
		rhea_error("Unable to setup IRQ");
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;

}
EXPORT_SYMBOL(rhea_interrupt_setup);

void rhea_interrupt_free(unsigned rhea_id, unsigned eq_id)
{
	struct rhea_eq *eq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->eqs, eq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return;
	}

	eq = sessions->ids[rhea_id]->eqs[eq_id];

	rhea_interrupts_free(eq);

	spin_unlock(&sessions->ids[rhea_id]->lock);

}
EXPORT_SYMBOL(rhea_interrupt_free);

int rhea_cq_alloc(unsigned rhea_id,
		  unsigned *cq_id, struct hea_cq_context *context_cq)
{
	int rc = 0;
	struct rhea_eq *aeq;
	struct rhea_eq *ceq;
	struct rhea_cq *cq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == cq_id)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (NULL == cq_id ||
	    NULL == context_cq ||
	    !RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->eqs, context_cq->ceq) ||
	    !RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->eqs,
				       context_cq->aeq)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);

		rhea_error("The EQ IDs do not match to this session!");

		return -EINVAL;
	}

	/* get EQs */
	aeq = sessions->ids[rhea_id]->eqs[context_cq->aeq];
	ceq = sessions->ids[rhea_id]->eqs[context_cq->ceq];

	/* get new CQ */
	cq = rhea_cq_create(ceq, aeq, &context_cq->process, &context_cq->cfg);
	if (NULL == cq) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -ENOMEM;
	}

	/* save pointer in session */
	sessions->ids[rhea_id]->cqs[cq->id] = cq;

	/* pass CQ ID to caller */
	*cq_id = cq->id;

	spin_unlock(&sessions->ids[rhea_id]->lock);

	if (0)
		rhea_cq_dumps(rhea_id, cq->id);

	return rc;
}
EXPORT_SYMBOL(rhea_cq_alloc);

int rhea_cq_dumps(unsigned rhea_id, unsigned cq_id)
{
	struct hea_adapter *ap;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->cqs, cq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get adapter */
	ap = s_rhea_context.aps[sessions->ids[rhea_id]->adapter_number];

	rhea_cq_dump(ap->mmio, sessions->ids[rhea_id]->cqs[cq_id]);

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return 0;
}
EXPORT_SYMBOL(rhea_cq_dumps);

static int _rhea_cq_free(unsigned rhea_id, unsigned cq_id)
{
	int rc = 0;
	struct rhea_cq *cq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	/* get CQ */
	cq = sessions->ids[rhea_id]->cqs[cq_id];

	/* make sure that nobody else tries to do the same!! */
	sessions->ids[rhea_id]->cqs[cq_id] = NULL;

	/* destroy CQ */
	rc = rhea_cq_destroy(cq);

	return rc;
}

int rhea_cq_free(unsigned rhea_id, unsigned cq_id)
{
	int rc = 0;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->cqs, cq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	rc = _rhea_cq_free(rhea_id, cq_id);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);

		rhea_error("Was not able to free CQ");
		return rc;

	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_cq_free);

int rhea_cq_table(unsigned rhea_id,
		  unsigned cq_id,
		  struct hea_cqe **cqe_begin,
		  unsigned *cqe_size, unsigned *cqe_count)
{

	int rc = 0;
	struct rhea_cq *cq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == cqe_size || NULL == cqe_count)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->cqs, cq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get CQ Pointer */
	cq = sessions->ids[rhea_id]->cqs[cq_id];

	if (NULL == cq->cqe_begin || 0 == cq->cqe_count || 0 == cq->cqe_size) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("CQ not setup correctly");
		return -EINVAL;
	}

	/* pass parameters to caller */
	*cqe_begin = cq->cqe_begin;
	*cqe_count = cq->cqe_count;
	*cqe_size = cq->cqe_size;

	spin_unlock(&sessions->ids[rhea_id]->lock);

	rhea_debug("CQ Ptr: %p, Count: %u, Size: %u", *cqe_begin, *cqe_count,
		   *cqe_size);

	return rc;
}
EXPORT_SYMBOL(rhea_cq_table);

int rhea_cq_mapinfo(unsigned rhea_id,
		    unsigned cq_id,
		    enum hea_priv_mode priv,
		    void **pointer, unsigned *size, unsigned use_va)
{
	int rc = 0;
	struct rhea_cq *cq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == size)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->cqs, cq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	cq = sessions->ids[rhea_id]->cqs[cq_id];

	rc = rhea_cq_mapinfo_get(cq, priv, pointer, size, use_va);
	if (rc) {
		rhea_error("Was not able to obtain CQ mapping information");
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_cq_mapinfo);

int rhea_cq_set(unsigned rhea_id,
		unsigned cq_id,
		enum hea_cq_feature_set feature, u64 value)
{
	int rc = 0;
	struct rhea_cq *cq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->cqs, cq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	cq = sessions->ids[rhea_id]->cqs[cq_id];

	rc = rhea_cq_feature_set(cq, feature, value);
	if (rc) {
		rhea_error("Was not able to set CQ feature");
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_cq_set);

int rhea_cq_get(unsigned rhea_id,
		unsigned cq_id,
		enum hea_cq_feature_get feature, u64 *value)
{
	int rc = 0;
	struct rhea_cq *cq;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == value)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->cqs, cq_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	cq = sessions->ids[rhea_id]->cqs[cq_id];

	rc = rhea_cq_feature_get(cq, feature, value);
	if (rc) {
		rhea_error("Was not able to obtain CQ feature information");
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_cq_get);


int rhea_qp_alloc(unsigned rhea_id,
		  unsigned *qp_id, struct hea_qp_context *context_qp)
{
	int rc = 0;
	unsigned pport_nr;
	enum hea_channel_type channel_type;

	struct rhea_eq *eq;
	struct rhea_cq *rcq;
	struct rhea_cq *scq;
	struct rhea_qp *qp;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == qp_id)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (NULL == qp_id ||
	    NULL == context_qp ||
	    !RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->eqs, context_qp->eq)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("The EQ IDs do not match to this session!");
		return -EINVAL;
	}

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->cqs, context_qp->r_cq) ||
	    !RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->cqs,
				       context_qp->s_cq)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("The CQ IDs do not match to this session!");
		return -EINVAL;
	}

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, context_qp->channel)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("The channel id does not match to this session!");
		return -EINVAL;
	}

	/* get EQ */
	eq = sessions->ids[rhea_id]->eqs[context_qp->eq];

	/* get CQs */
	rcq = sessions->ids[rhea_id]->cqs[context_qp->r_cq];
	scq = sessions->ids[rhea_id]->cqs[context_qp->s_cq];

	/* get physical port number */
	pport_nr =
		sessions->ids[rhea_id]->channels[context_qp->channel]->
		pport_nr;
	channel_type =
		sessions->ids[rhea_id]->channels[context_qp->channel]->type;

	if (context_qp->cfg.hw_managed && context_qp->cfg.rq3_ep.wqe_count) {
		rhea_error("RQ3 is not support in NN mode!");
		return -EINVAL;
	}

	/* create new QP */
	qp = rhea_qp_create(pport_nr, channel_type,
			    &context_qp->process,
			    eq, rcq, scq,
			    &context_qp->pd_cfg, &context_qp->cfg);
	if (NULL == qp) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Could not create QP");
		return -ENOMEM;
	}

	/* save pointer in session */
	sessions->ids[rhea_id]->qps[qp->id] = qp;

	/* pass id back to caller */
	*qp_id = qp->id;

	spin_unlock(&sessions->ids[rhea_id]->lock);

	if (0)
		rhea_qp_dumps(rhea_id, *qp_id);

	return rc;
}
EXPORT_SYMBOL(rhea_qp_alloc);

int rhea_qp_dumps(unsigned rhea_id, unsigned qp_id)
{

	struct hea_adapter *ap;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id) ||
	    !RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->qps, qp_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	/* get adapter */
	ap = s_rhea_context.aps[sessions->ids[rhea_id]->adapter_number];
	rhea_qp_dump(ap->mmio, sessions->ids[rhea_id]->qps[qp_id]);

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return 0;
}
EXPORT_SYMBOL(rhea_qp_dumps);

static int _rhea_qp_free(unsigned rhea_id, unsigned qp_id)
{
	int rc = 0;
	struct rhea_qp *qp;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	/* get EQ Pointer */
	qp = sessions->ids[rhea_id]->qps[qp_id];

	/* make sure that nobody else tries to do the same!! */
	sessions->ids[rhea_id]->qps[qp_id] = NULL;

	/* destroy QP */
	rc = rhea_qp_destroy(qp);

	return rc;
}

int rhea_qp_free(unsigned rhea_id, unsigned qp_id)
{
	int rc = 0;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->qps, qp_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* free QP */
	rc = _rhea_qp_free(rhea_id, qp_id);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);

		rhea_error("Was not able to free QP");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_qp_free);

int rhea_qp_mapinfo(unsigned rhea_id,
		    unsigned qp_id,
		    enum hea_priv_mode priv,
		    void **pointer, unsigned *size, unsigned use_va)
{

	int rc = 0;
	struct rhea_qp *qp;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == size)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->qps, qp_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	qp = sessions->ids[rhea_id]->qps[qp_id];

	rc = rhea_qp_mapinfo_get(qp, priv, pointer, size, use_va);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);

		rhea_error("Was not able to obtain QP mapping information");
		return -EINVAL;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_qp_mapinfo);

int rhea_sq_table(unsigned rhea_id,
		  unsigned qp_id,
		  union snd_wqe **wqes,
		  unsigned *wqe_size, unsigned *wqe_count)
{
	int rc = 0;
	struct rhea_qp *qp;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == wqe_size || NULL == wqe_count)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id) ||
	    !RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->qps, qp_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	/* get QP Pointer */
	qp = sessions->ids[rhea_id]->qps[qp_id];

	if (NULL == qp->sq.wqe_begin || 0 == qp->sq.wqe_count ||
	    0 == qp->sq.wqe_size) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("SQ not setup correctly");
		return -EINVAL;
	}

	/* pass parameters to caller */
	*wqes = qp->sq.wqe_begin;
	*wqe_count = qp->sq.wqe_count;
	*wqe_size = qp->sq.wqe_size;

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_sq_table);

int rhea_rq_table(unsigned rhea_id,
		  unsigned qp_id,
		  unsigned rq_nr,
		  union rcv_wqe **wqes,
		  unsigned *wqe_size, unsigned *wqe_count)
{
	int rc = 0;
	struct rhea_qp *qp;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == wqe_size || NULL == wqe_count)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->qps, qp_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get QP Pointer */
	qp = sessions->ids[rhea_id]->qps[qp_id];

	switch (rq_nr) {

	default:
		rhea_error("Invalid RQ number");
		rc = -EINVAL;
		break;

	case 1:

		if (NULL == qp->rq1.wqe_begin || 0 == qp->rq1.wqe_count) {
			spin_unlock(&sessions->ids[rhea_id]->lock);
			return -EINVAL;
		}

		/* pass parameters to caller */
		*wqes = qp->rq1.wqe_begin;
		*wqe_count = qp->rq1.wqe_count;
		*wqe_size = qp->rq1.wqe_size;
		break;

	case 2:

		if (NULL == qp->rq2.wqe_begin || 0 == qp->rq2.wqe_count) {
			spin_unlock(&sessions->ids[rhea_id]->lock);
			return -EINVAL;
		}

		/* pass parameters to caller */
		*wqes = qp->rq2.wqe_begin;
		*wqe_count = qp->rq2.wqe_count;
		*wqe_size = qp->rq2.wqe_size;
		break;

	case 3:

		if (NULL == qp->rq3.wqe_begin || 0 == qp->rq3.wqe_count) {
			spin_unlock(&sessions->ids[rhea_id]->lock);
			return -EINVAL;
		}

		/* pass parameters to caller */
		*wqes = qp->rq3.wqe_begin;
		*wqe_count = qp->rq3.wqe_count;
		*wqe_size = qp->rq3.wqe_size;
		break;

	}

	if (NULL == *wqes || 0 == *wqe_count || 0 == *wqe_size) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("RQ[%u] not setup correctly", rq_nr);
		return -EINVAL;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_rq_table);

int rhea_qp_get(unsigned rhea_id,
		unsigned qp_id,
		enum hea_qp_feature_get feature, u64 *value)
{
	int rc = 0;
	struct rhea_qp *qp;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == value)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->qps, qp_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get QP Pointer */
	qp = sessions->ids[rhea_id]->qps[qp_id];

	rc = rhea_qp_feature_get(qp, feature, value);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Could not get QP feature");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_qp_get);

int rhea_qp_set(unsigned rhea_id,
		unsigned qp_id,
		enum hea_qp_feature_set feature, u64 value)
{
	int rc = 0;
	struct rhea_qp *qp;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->qps, qp_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get QP Pointer */
	qp = sessions->ids[rhea_id]->qps[qp_id];

	rc = rhea_qp_feature_set(qp, feature, value);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Could not set QP feature");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_qp_set);

int rhea_qp_up(unsigned rhea_id, unsigned qp_id)
{
	int rc = 0;
	struct rhea_qp *qp;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->qps, qp_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get QP Pointer */
	qp = sessions->ids[rhea_id]->qps[qp_id];

	rhea_info("Enable QP[%u]", qp->id);

	rc = rhea_qp_enable(qp, qp->qp_cfg.hw_managed);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Error in QP enable");
		return -EINVAL;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_qp_up);

int rhea_qp_down(unsigned rhea_id, unsigned qp_id)
{
	int rc = 0;
	struct rhea_qp *qp;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->qps, qp_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get QP Pointer */
	qp = sessions->ids[rhea_id]->qps[qp_id];

	rhea_info("Disable QP[%u]", qp->id);

	rc = rhea_qp_disable(qp);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Error in QP disable");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_qp_down);

int rhea_channel_feature_set(unsigned rhea_id,
			     unsigned channel_id,
			     enum hea_channel_feature_set feature,
			     u64 value)
{
	int rc = 0;

	struct hea_channel_cfg channel_cfg;

	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	memset(&channel_cfg, 0, sizeof(channel_cfg));

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		rhea_error("Passed in invalid parameters");
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	if (!is_hea_lport(channel->type) &&
	    HEA_DEFAULT_CHANNEL_SHARE ==
	    channel->channel_cfg.dc.channel_usuage) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("It is not possible to set a channel values, "
			   "if the channel is shared!");
		return -EPERM;
	}

	rc = rhea_channel_info_set(channel, feature, value);
	if (rc)
		rhea_info("Was not able to set feature %i for channel: %u",
			   feature, channel->id);


	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_feature_set);

int rhea_channel_feature_get(unsigned rhea_id,
			     unsigned channel_id,
			     enum hea_channel_feature_get feature,
			     u64 *value)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		rhea_error("Passed in invalid parameters");
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	rc = rhea_channel_info_get(channel, feature, value);
	if (rc)
		rhea_error("Was not able to obtain feature for channel");

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_feature_get);

int rhea_channel_qpn_alloc(unsigned rhea_id,
			   unsigned channel_id,
			   struct hea_qpn_context *qpn_context)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == qpn_context)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	rc = rhea_qpn_alloc(channel, &qpn_context->qpn_cfg);
	if (0 > rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Was not able to allocate QPN");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return 0;
}
EXPORT_SYMBOL(rhea_channel_qpn_alloc);

int rhea_channel_qpn_free(unsigned rhea_id, unsigned channel_id)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	rc = rhea_qpn_free(channel);
	if (0 > rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Was not able to allocate QPN");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return 0;
}
EXPORT_SYMBOL(rhea_channel_qpn_free);

int rhea_channel_qpn_share(unsigned rhea_id,
			   unsigned target_channel_id,
			   unsigned source_channel_id)
{
	int rc = 0;
	struct rhea_channel *channel_target;
	struct rhea_channel *channel_source;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		rhea_error("Invalid rhea id");
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->channels,
				       target_channel_id)) {
		rhea_error("Invalid target id");
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel_target = sessions->ids[rhea_id]->channels[target_channel_id];

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, source_channel_id)) {
		rhea_error("Invalid source id");
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel_source = sessions->ids[rhea_id]->channels[source_channel_id];

	rc = rhea_qpn_share(channel_target, channel_source);
	if (0 > rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Was not able to share QPN");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return 0;
}
EXPORT_SYMBOL(rhea_channel_qpn_share);

int rhea_channel_qpn_query(unsigned rhea_id,
			   unsigned channel_id, int *num_free)
{

	int max = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == num_free)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	/* find out how many qpn slots are available */
	max = rhea_qpn_max(channel);

	/* pass result back */
	*num_free = max;

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return 0;
}
EXPORT_SYMBOL(rhea_channel_qpn_query);

int rhea_channel_wire_qpn_to_qp(unsigned rhea_id,
				unsigned channel_id,
				unsigned qp_id, unsigned qpn_offset)
{
	int rc = 0;
	struct rhea_qp *qp;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->qps, qp_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get qp */
	qp = sessions->ids[rhea_id]->qps[qp_id];

	/* check if we support this channel */
	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->channels,
				       channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Invalid channel type!");
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	rc = rhea_qpn_set(channel, qp->id, qpn_offset);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Was not able to set QPN");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return 0;
}
EXPORT_SYMBOL(rhea_channel_wire_qpn_to_qp);

int rhea_channel_tcam_alloc(unsigned rhea_id,
			    unsigned channel_id,
			    unsigned *tcam_id,
			    struct hea_tcam_context *tcam_context)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == tcam_id || NULL == tcam_context)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	if (s_rhea_context.hasher_used &&
		s_rhea_context.hasher_session != rhea_id) {
		spin_unlock(&sessions->lock);
		rhea_warning("Can not allocate TCAM, since Hasher is used");
		return -EPERM;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	rc = rhea_tcam_alloc(channel, &tcam_context->tcam_cfg, tcam_id);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Error when allocating tcam");
		return -EINVAL;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_tcam_alloc);

int rhea_channel_hasher_alloc(unsigned rhea_id, unsigned channel_id)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_hasher *hasher;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	/* now get the hasher */
	hasher = rhea_hasher_alloc(channel);
	if (NULL == hasher) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Could not allocate hasher");
		return -EINVAL;
	}

	sessions->ids[rhea_id]->hasher = hasher;
	s_rhea_context.hasher_used = 1;
	s_rhea_context.hasher_session = rhea_id;

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_hasher_alloc);


int rhea_channel_hasher_free(unsigned rhea_id, unsigned channel_id)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
			(sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	if (sessions->ids[rhea_id]->hasher) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	/* now get the hasher */
	rc = rhea_hasher_free(channel);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Could not free hasher");
		return rc;
	}

	sessions->ids[rhea_id]->hasher = NULL;
	s_rhea_context.hasher_used = 0;
	s_rhea_context.hasher_session = 0;

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_hasher_free);


int rhea_channel_tcam_free(unsigned rhea_id,
			   unsigned channel_id,
			   unsigned tcam_id)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	rc = rhea_tcam_free(channel, tcam_id);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Error when freeing tcam");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;

}
EXPORT_SYMBOL(rhea_channel_tcam_free);

int rhea_channel_tcam_set(unsigned rhea_id,
			  unsigned channel_id,
			  unsigned tcam_id,
			  struct hea_tcam_setting *tcam_setting)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == tcam_setting)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	rc = rhea_tcam_set(channel,
			   tcam_id, tcam_setting->tcam_offset,
			   tcam_setting->qpn_offset,
			   tcam_setting->tcam_pattern,
			   tcam_setting->tcam_mask);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Error when setting tcam");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_tcam_set);

int rhea_channel_tcam_get(unsigned rhea_id,
			  unsigned channel_id,
			  unsigned tcam_id,
			  struct hea_tcam_setting *tcam_setting)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == tcam_setting)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	rc = rhea_tcam_get(channel,
			   tcam_id, tcam_setting->tcam_offset,
			   &tcam_setting->qpn_offset,
			   &tcam_setting->tcam_pattern,
			   &tcam_setting->tcam_mask);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Error when reading tcam");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_tcam_get);

int rhea_channel_tcam_enable(unsigned rhea_id,
			     unsigned channel_id,
			     unsigned tcam_id,
			     unsigned tcam_offset)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	/* enable TCAM slot */
	rc = rhea_tcam_register_set_status(channel, tcam_id, tcam_offset, 1);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Error enabling tcam");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_tcam_enable);

int rhea_channel_tcam_disable(unsigned rhea_id,
			      unsigned channel_id,
			      unsigned tcam_id,
			      unsigned tcam_offset)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	/* enable TCAM slot */
	rc = rhea_tcam_register_set_status(channel, tcam_id, tcam_offset, 0);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Error enabling tcam");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_tcam_disable);

int rhea_channel_hasher_set(unsigned rhea_id,
			    unsigned channel_id,
			    struct hea_hasher_setting *hasher_setting)
{
	int rc = 0;
	u64 sc = 0x0ULL;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == hasher_setting)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	if (NULL == sessions->ids[rhea_id]->hasher) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Hasher is not setup for this session");
		return -EINVAL;
	}

	/* setup symmetry register settings */
	sc = hea_set_u64_bits(sc, hasher_setting->sc_0_8 ? 1 : 0, 0, 0);
	sc = hea_set_u64_bits(sc, hasher_setting->sc_1_9 ? 1 : 0, 1, 1);
	sc = hea_set_u64_bits(sc, hasher_setting->sc_2_10 ? 1 : 0, 2, 2);
	sc = hea_set_u64_bits(sc, hasher_setting->sc_3_11 ? 1 : 0, 3, 3);
	sc = hea_set_u64_bits(sc, hasher_setting->sc_4_12 ? 1 : 0, 4, 4);
	sc = hea_set_u64_bits(sc, hasher_setting->sc_5_13 ? 1 : 0, 5, 5);
	sc = hea_set_u64_bits(sc, hasher_setting->sc_6_14 ? 1 : 0, 6, 6);
	sc = hea_set_u64_bits(sc, hasher_setting->sc_7_15 ? 1 : 0, 7, 7);

	/* set hasher config */
	rc = rhea_hasher_set(channel, sc,
			     hasher_setting->mask0, hasher_setting->mask1);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Error setting hasher");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_hasher_set);

int rhea_channel_hasher_get(unsigned rhea_id,
			    unsigned channel_id,
			    struct hea_hasher_setting *hasher_setting)
{
	int rc = 0;
	u64 sc = 0x0ULL;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == hasher_setting)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	if (NULL == sessions->ids[rhea_id]->hasher) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Hasher is not setup for this session");
		return -EINVAL;
	}

	/* get hasher config */
	rc = rhea_hasher_get(channel, &sc,
			     &hasher_setting->mask0, &hasher_setting->mask1);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Error getting hasher config");
		return rc;
	}

	/* get symmetry register settings */
	hasher_setting->sc_0_8 = hea_get_u64_bits(sc, 0, 0);
	hasher_setting->sc_1_9 = hea_get_u64_bits(sc, 1, 1);
	hasher_setting->sc_2_10 = hea_get_u64_bits(sc, 2, 2);
	hasher_setting->sc_3_11 = hea_get_u64_bits(sc, 3, 3);
	hasher_setting->sc_4_12 = hea_get_u64_bits(sc, 4, 4);
	hasher_setting->sc_5_13 = hea_get_u64_bits(sc, 5, 5);
	hasher_setting->sc_6_14 = hea_get_u64_bits(sc, 6, 6);
	hasher_setting->sc_7_15 = hea_get_u64_bits(sc, 7, 7);

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_hasher_get);


unsigned rhea_pport_count(unsigned rhea_id)
{
	int i;
	unsigned pport_count = 0;

	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);

		rhea_error("Invalid parameters passed in");
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	for (i = 0; i < HEA_MAX_PPORT_COUNT; ++i) {
		if (0 < rhea_pport_avail(i))
			++pport_count;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	/* return number of available physical ports */
	return pport_count;
}
EXPORT_SYMBOL(rhea_pport_count);

int rhea_channel_alloc(unsigned rhea_id,
		       unsigned *channel_id,
		       struct hea_channel_context *context_channel)
{
	int rc = 0;
	int saved = 0;
	struct rhea_channel *channel = NULL;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == context_channel || NULL == channel_id)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		rc = -EINVAL;
		goto out;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!is_hea_lport(context_channel->cfg.type) &&
	    HEA_DEFAULT_CHANNEL_SHARE == context_channel->cfg.dc.channel_usuage
	    && !RHEA_VALID_INSTANCE_CHECK(s_rhea_context.manager,
					  context_channel->cfg.pport_nr)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);

		rhea_error("No MC/BC/UC manager registered");
		rc = -EINVAL;
		goto out;
	}

	if (!is_hea_lport(context_channel->cfg.type) &&
	    HEA_DEFAULT_CHANNEL_SHARE == context_channel->cfg.dc.channel_usuage
	    && !RHEA_VALID_INSTANCE_CHECK(sessions->ids[rhea_id]->channels,
					  context_channel->cfg.dc.
					  lport_channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);

		rhea_error("Invalid parameters passed in for channel id");
		rc = -EINVAL;
		goto out;

	} else if (!is_hea_lport(context_channel->cfg.type) &&
		   HEA_DEFAULT_CHANNEL_SHARE ==
		   context_channel->cfg.dc.channel_usuage) {
		struct rhea_channel *channel_lport = NULL;

		/* get logical port channel object */
		channel_lport =
			sessions->ids[rhea_id]->channels[context_channel->cfg.
							 dc.lport_channel_id];

		/* save the port number */
		context_channel->cfg.pport_nr = channel_lport->pport_nr;
	}

	/* check if manager channel can be freed up for this */
	if (!is_hea_lport(context_channel->cfg.type) &&
	    HEA_DEFAULT_CHANNEL_ALONE ==
	    context_channel->cfg.dc.channel_usuage) {
		struct rhea_bc_mc_uc_manager *channel_manager;

		/* get channel manager */
		channel_manager =
			s_rhea_context.manager[context_channel->cfg.pport_nr];

		/* check if anybody is using this shared channel */
		if (0 == channel_manager->channel_registered_count(
						context_channel->cfg.pport_nr,
						context_channel->cfg.type)) {

			/* free channel resources so that they can be
			 * used by somebody else */
			rc = channel_manager->channel_destroy(
						context_channel->cfg.pport_nr,
						context_channel->cfg.type);
			if (rc) {
				spin_unlock(&sessions->ids[rhea_id]->lock);
				rhea_error("Was not able to unregister the "
					   "channel: %u in manager",
					   context_channel->cfg.type);
				goto out;
			}
		} else {
			spin_unlock(&sessions->ids[rhea_id]->lock);
			rhea_error("Channel is already used");
			rc = -EINVAL;
			goto out;
		}
	}

	channel = rhea_channel_create(&context_channel->cfg);
	if (NULL == channel) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Was not able to obtain the channel");
		rc = -EINVAL;
		goto out;
	}

	if (NULL != sessions->ids[rhea_id]->channels[channel->id]) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Channel already exists!!!");
		rc = -EPERM;
		goto out;
	}

	rc = eq0_pport_event_callback_register(&s_rhea_context.eq0,
						channel->channel_cfg.pport_nr,
						channel->type,
						&context_channel->pport_event);
	if (rc) {
		rhea_error("Was not able to register pport event callback");
		goto out;
	}

	sessions->ids[rhea_id]->channels[channel->id] = channel;
	saved = 1;

	/* in case it is a shared channel register it with the manager */
	if (!is_hea_lport(context_channel->cfg.type) &&
	    HEA_DEFAULT_CHANNEL_SHARE ==
	    context_channel->cfg.dc.channel_usuage) {
		struct rhea_bc_mc_uc_manager *channel_manager;

		/* get channel manager */
		channel_manager =
			s_rhea_context.manager[context_channel->cfg.pport_nr];

		/* get lport which get's associated with it */
		channel->channel_lport =
			sessions->ids[rhea_id]->channels[channel->channel_cfg.
							 dc.lport_channel_id];

		rhea_info("Register channel: %u with id: %u on PPORT[%u]",
			  channel->type, channel->id, channel->pport_nr + 1);

		rc = channel_manager->manager_lport_register(
					channel->pport_nr, channel->type,
					&channel->channel_lport->channel_cfg);
		if (rc) {

			spin_unlock(&sessions->ids[rhea_id]->lock);
			rhea_error("Was not able to register lport "
				   "with channel manager!");
			goto out;
		}
	}

	/* pass back id */
	*channel_id = channel->id;

	if (0)
		rhea_pport_counters_dump(channel);

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return 0;
out:
	if (saved)
		sessions->ids[rhea_id]->channels[channel->id] = NULL;

	if (channel) {
		eq0_pport_event_callback_unregister(&s_rhea_context.eq0,
						  channel->channel_cfg.pport_nr,
						  channel->type);
		rhea_channel_destroy(channel);
	}

	return rc;
}
EXPORT_SYMBOL(rhea_channel_alloc);

static inline int _rhea_manager_channel_create(
					struct rhea_bc_mc_uc_manager *manager,
					unsigned int channel_type,
					unsigned int pport_nr)
{
	int rc;

	if (NULL == manager)
		return -EINVAL;

	switch (channel_type) {

	case HEA_UC_PORT:

		/* check if UC channels are not allocated anymore */
		if (1 == rhea_channel_avail(pport_nr, HEA_UC_PORT)) {
			rc = manager->channel_create(pport_nr,
						     HEA_UC_PORT);
			if (rc) {
				rhea_error("Was not able to enable "
					    "UC channel "
					   "manager on pport: %u",
					   pport_nr + 1);
				return rc;
			}

			rhea_info("Enabled UC Manager for pport: %u",
				  pport_nr + 1);
		}
		break;

	case HEA_MC_PORT:

		/* check if MC channels are not allocated anymore */
		if (1 == rhea_channel_avail(pport_nr, HEA_MC_PORT)) {
			rc = manager->channel_create(pport_nr,
							HEA_MC_PORT);
			if (rc) {
				rhea_error("Was not able to "
					   "enable MC channel "
					   "manager on pport: %u",
					   pport_nr + 1);
				return rc;
			}

		rhea_info("Enabled MC Manager for pport: %u",
			  pport_nr + 1);
		}
		break;

	case HEA_BC_PORT:

		/* check if BC channels are not allocated anymore */
		if (1 == rhea_channel_avail(pport_nr, HEA_BC_PORT)) {
			rc = manager->channel_create(pport_nr,
						 HEA_BC_PORT);
			if (rc) {
				rhea_error("Was not able to enable BC "
				       "channel manager on pport: %u",
				       pport_nr + 1);
				return rc;
			}

			rhea_info("Enabled BC Manager for pport: %u",
				  pport_nr + 1);
		}
		break;

	default:
		break;
	}

	return 0;
}


static int _rhea_channel_free(unsigned rhea_id, unsigned channel_id)
{
	int rc = 0;
	unsigned int pport_nr;
	enum hea_default_channel_usuage channel_usuage =
		HEA_DEFAULT_CHANNEL_ALONE;
	enum hea_channel_type channel_type;
	struct rhea_channel *channel;
	struct rhea_bc_mc_uc_manager *manager;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	/* get channel instance */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	if (!RHEA_VALID_INSTANCE_CHECK
	    (s_rhea_context.manager, channel->pport_nr)) {
		rhea_warning("Manager is not registered");
		rc = -EINVAL;
		goto out;
	}

	/* get manager instance */
	manager = s_rhea_context.manager[channel->pport_nr];

	/* delete in local object */
	sessions->ids[rhea_id]->channels[channel->id] = NULL;

	/* get channel usuage */
	channel_usuage = channel->channel_cfg.dc.channel_usuage;
	channel_type = channel->type;

	if (!is_hea_lport(channel->type) &&
	    HEA_DEFAULT_CHANNEL_SHARE == channel_usuage) {

		struct rhea_bc_mc_uc_manager *channel_manager =
			s_rhea_context.manager[channel->pport_nr];

		if (NULL == channel->channel_lport) {
			rhea_error("LPORT channel not set");
			rc = -EINVAL;
			goto out;
		}

		rhea_debug("Unregister channel: %u", channel->type);

		rc = channel_manager->manager_lport_unregister(
				channel->pport_nr, channel->type,
				&channel->channel_lport->channel_cfg);
	} else {
		/*
		 * check if the logical port has anything
		 * registered with the manager
		 **/
		struct rhea_bc_mc_uc_manager *channel_manager;

		/* get channel manager */
		channel_manager = s_rhea_context.manager[channel->pport_nr];

		/*
		 * just call these functions,
		 * in case the lport has registered something
		 **/
		channel_manager->manager_lport_unregister(channel->pport_nr,
							HEA_UC_PORT,
							&channel->channel_cfg);

		channel_manager->manager_lport_unregister(channel->pport_nr,
							HEA_BC_PORT,
							&channel->channel_cfg);

		channel_manager->manager_lport_unregister(channel->pport_nr,
							HEA_MC_PORT,
							&channel->channel_cfg);
	}

	/* get pport nr */
	pport_nr = channel->pport_nr;

	eq0_pport_event_callback_unregister(&s_rhea_context.eq0, pport_nr,
					    channel->type);

	/* destroy the channel for good */
	rc = rhea_channel_destroy(channel);
	if (rc) {
		rhea_error("Was not able to free channel: %u", channel->type);
		goto out;
	}

	/* if alone channel was disabled --> check if manager has to
	 * take over */
	if (HEA_DEFAULT_CHANNEL_ALONE == channel_usuage &&
	    RHEA_CHANNEL_ENABLED == rhea_pport_state(pport_nr)) {

		rc = _rhea_manager_channel_create(manager, channel_type,
						  pport_nr);
		if (rc) {
			rhea_info("Was not able to create channel in manager");
			goto out;
		}
	} else if (HEA_DEFAULT_CHANNEL_MANAGER != channel_usuage &&
		   RHEA_CHANNEL_DISABLED == rhea_pport_state(pport_nr)) {
		/* if the port is off --> turn manager off as well */
		manager->channel_destroy(pport_nr, HEA_BC_PORT);
		manager->channel_destroy(pport_nr, HEA_MC_PORT);
		manager->channel_destroy(pport_nr, HEA_UC_PORT);
	}

	return 0;
out:
	return rc;
}

int rhea_channel_free(unsigned rhea_id, unsigned channel_id)
{
	int rc = 0;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);

		rhea_error("Invalid parameters passed in");
		return -EINVAL;
	}

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->lock);

		rhea_error("Invalid parameters passed in");
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	rc = _rhea_channel_free(rhea_id, channel_id);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);

		rhea_error("Was not able to free channel");
		return rc;
	}

	if (sessions->ids[rhea_id]->hasher) {
		sessions->ids[rhea_id]->hasher = NULL;
		s_rhea_context.hasher_used = 0;
		s_rhea_context.hasher_session = 0;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_free);

int rhea_channel_macaddr_set(unsigned rhea_id,
			     unsigned channel_id,
			     union hea_mac_addr mac_address)
{
	int rc = 0;
	struct rhea_channel *channel = NULL;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->lock);
		rhea_error("Invalid parameters passed in");
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	channel = sessions->ids[rhea_id]->channels[channel->id];

	rc = rhea_channel_info_set(channel, HEA_CHANNEL_SET_MAC_ADDRESS,
				   mac_address._be64);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Was not able to set MAC address");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_macaddr_set);

int rhea_channel_macaddr_get(unsigned rhea_id,
			     unsigned channel_id,
			     union hea_mac_addr *mac_address)
{
	int rc;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == mac_address)
		return -EINVAL;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		rhea_error("Invalid parameters passed in");
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	rc = rhea_channel_info_get(channel, HEA_CHANNEL_GET_MAC_ADDRESS,
				   &mac_address->_be64);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Was not able to get MAC address");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	rhea_debug("MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
		   mac_address->sa.addr[0], mac_address->sa.addr[1],
		   mac_address->sa.addr[2], mac_address->sa.addr[3],
		   mac_address->sa.addr[4], mac_address->sa.addr[5]);

	return 0;
}
EXPORT_SYMBOL(rhea_channel_macaddr_get);

int rhea_channel_disable(unsigned rhea_id, unsigned channel_id)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	if (unlikely(!is_hea_lport(channel->type) &&
		     HEA_DEFAULT_CHANNEL_SHARE ==
		     channel->channel_cfg.dc.channel_usuage)) {
		struct rhea_bc_mc_uc_manager *manager;

		if (!RHEA_VALID_INSTANCE_CHECK
		    (s_rhea_context.manager, channel->pport_nr)) {
			spin_unlock(&sessions->ids[rhea_id]->lock);
			rhea_warning("Manager is not registered");
			return -EINVAL;
		}

		if (NULL == channel->channel_lport) {
			spin_unlock(&sessions->ids[rhea_id]->lock);
			rhea_warning("Lport is no longer available");
			return -EINVAL;
		}

		/* get manager instance */
		manager = s_rhea_context.manager[channel->pport_nr];

		rc = manager->manager_lport_disable(channel->pport_nr,
						    channel->type,
						    channel->channel_lport->
						    channel_cfg.lport.
						    lport_nr);
		if (rc) {
			spin_unlock(&sessions->ids[rhea_id]->lock);
			rhea_error
				("Was not able to enable manager channel: %u",
				 channel->type);
			return rc;
		}
	} else {
		/* make sure that manager does not
		 * send packets anymore to lport*/
		if (is_hea_lport(channel->type)) {
			struct rhea_bc_mc_uc_manager *manager;

			/* get manager instance */
			manager = s_rhea_context.manager[channel->pport_nr];

			manager->manager_lport_disable(channel->pport_nr,
						       HEA_UC_PORT,
						       channel->channel_cfg.
						       lport.lport_nr);
			manager->manager_lport_disable(channel->pport_nr,
						       HEA_MC_PORT,
						       channel->channel_cfg.
						       lport.lport_nr);
			manager->manager_lport_disable(channel->pport_nr,
						       HEA_BC_PORT,
						       channel->channel_cfg.
						       lport.lport_nr);
		}

		rc = rhea_channel_stop(channel);
		if (rc) {
			spin_unlock(&sessions->ids[rhea_id]->lock);
			rhea_error("Problems when enabling the port");
			return rc;
		}
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_disable);

int rhea_channel_enable(unsigned rhea_id, unsigned channel_id)
{
	int rc = 0;

	struct rhea_channel *channel;
	struct rhea_bc_mc_uc_manager *manager;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		rhea_error("Passed in invalid parameters");
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	if (!RHEA_VALID_INSTANCE_CHECK
	    (s_rhea_context.manager, channel->pport_nr)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_warning("Manager is not registered");
		return -EINVAL;
	}

	/* get manager instance */
	manager = s_rhea_context.manager[channel->pport_nr];

	if (unlikely(!is_hea_lport(channel->type) &&
		     HEA_DEFAULT_CHANNEL_SHARE ==
		     channel->channel_cfg.dc.channel_usuage)) {
		if (NULL == channel->channel_lport) {
			spin_unlock(&sessions->ids[rhea_id]->lock);
			rhea_warning("Lport is no longer available");
			return -EINVAL;
		}

		rc = manager->manager_lport_enable(channel->pport_nr,
						   channel->type,
						   channel->channel_lport->
						   channel_cfg.lport.lport_nr);
		if (rc) {
			spin_unlock(&sessions->ids[rhea_id]->lock);
			rhea_error
				("Was not able to enable manager channel: %u",
				 channel->type);
			return rc;
		}

		rhea_info("Registered logical port: %u with channel: "
			  "%u on pport: %u.",
			  channel->channel_lport->channel_cfg.lport.lport_nr,
			  channel->type, channel->pport_nr + 1);
	} else {

		/* make sure that we have all the manager channels running
		 * before starting up the port
		 * */
		if (RHEA_CHANNEL_ENABLED !=
		    rhea_pport_state(channel->pport_nr)) {

			_rhea_manager_channel_create(manager, HEA_UC_PORT,
						    channel->pport_nr);

			_rhea_manager_channel_create(manager, HEA_MC_PORT,
						    channel->pport_nr);

			_rhea_manager_channel_create(manager, HEA_BC_PORT,
						    channel->pport_nr);
		}

		/* start the channel */
		rc = rhea_channel_start(channel);
		if (rc) {
			spin_unlock(&sessions->ids[rhea_id]->lock);
			rhea_error("Problems when enabling the channel: "
				   "%u on port %u",
				   channel->type, channel->pport_nr + 1);
			return rc;
		}

		/* make sure that manager does send packets to lport again */
		/* --> only works if channel is registered anyway */
		if (is_hea_lport(channel->type)) {
			struct rhea_bc_mc_uc_manager *manager;

			/* get manager instance */
			manager = s_rhea_context.manager[channel->pport_nr];

			manager->manager_lport_enable(channel->pport_nr,
						      HEA_UC_PORT,
						      channel->channel_cfg.
						      lport.lport_nr);

			manager->manager_lport_enable(channel->pport_nr,
						      HEA_MC_PORT,
						      channel->channel_cfg.
						      lport.lport_nr);

			manager->manager_lport_enable(channel->pport_nr,
						      HEA_BC_PORT,
						      channel->channel_cfg.
						      lport.lport_nr);
		}
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_enable);

int rhea_channel_counters_get(unsigned rhea_id,
			      unsigned channel_id,
			      enum hea_channel_counter_type counter_type,
			      struct hea_channel_counters *counter)
{
	int rc = 0;
	struct rhea_channel *channel;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK
	    (sessions->ids[rhea_id]->channels, channel_id)) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		return -EINVAL;
	}

	/* get channel */
	channel = sessions->ids[rhea_id]->channels[channel_id];

	rc = channel_counters_get(channel, counter_type, counter);
	if (rc) {
		spin_unlock(&sessions->ids[rhea_id]->lock);
		rhea_error("Problems when getting counter values for channel");
		return rc;
	}

	spin_unlock(&sessions->ids[rhea_id]->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_channel_counters_get);


unsigned rhea_adapter_count()
{
	int i;
	unsigned int adapter_count = 0;

	/* find number of registered adapters */
	for (i = 0; i < HEA_MAX_ADAPTERS; ++i) {
		if (s_rhea_context.aps[i])
			++adapter_count;
	}

	return adapter_count;
}
EXPORT_SYMBOL(rhea_adapter_count);

int rhea_adapter_get(unsigned adapter_number, struct hea_adapter *ap)
{
	int rc = 0;

	if (NULL == ap)
		return -EINVAL;

	if (!RHEA_VALID_INSTANCE_CHECK(s_rhea_context.aps, adapter_number)) {
		rhea_error("Adapter Context is not known to rHEA: %u",
			   adapter_number);
		return -EINVAL;
	}

	/* save structure */
	*ap = *s_rhea_context.aps[adapter_number];

	return rc;
}
EXPORT_SYMBOL(rhea_adapter_get);

int rhea_session_init(unsigned *rhea_id, unsigned adapter_number)
{
	int rc = 0;
	int id = -1;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == rhea_id)
		return -EINVAL;

	if (!RHEA_VALID_INSTANCE_CHECK(s_rhea_context.aps, adapter_number)) {
		rhea_error("Adapter Context is not known to rHEA: %u",
			   adapter_number);
		return -EINVAL;
	}

	if (sessions->alloced_max <= sessions->alloced) {
		rhea_error("Maximum number of supported sessions reached");
		return -EINVAL;
	}

	switch (adapter_number) {
	default:
		rhea_debug("Init rHEA adapter: %i", adapter_number);
	}

	spin_lock(&sessions->lock);

	RHEA_FIND_FREE_ID(*sessions, ids, sessions->alloced_max, id);
	if (-1 == id) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	sessions->ids[id] = rhea_alloc(sizeof(*sessions->ids[id]), GFP_KERNEL);
	if (NULL == sessions->ids[id])
		return -ENOMEM;

	++sessions->alloced;

	sessions->ids[id]->adapter_number = adapter_number;

	/* initialise lock for this session */
	spin_lock_init(&sessions->ids[id]->lock);

	/* pass id back to caller */
	*rhea_id = id;
	sessions->ids[id]->id = id;

	spin_unlock(&sessions->lock);

	return rc;
}
EXPORT_SYMBOL(rhea_session_init);

int _rhea_session_fini(struct rhea_session *session)
{
	int i;
	int rc = 0;
	unsigned rhea_id;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	if (NULL == session)
		return -EINVAL;

	/* get id */
	rhea_id = session->id;

	/* Disable all IRQs which are registered for this session */
	for (i = 0; i < ARRAY_SIZE(session->eqs); ++i) {
		if (NULL != session->eqs[i]) {
			u64 irq_nr;
			rc = _rhea_eq_feature_get(rhea_id, i,
						  HEA_EQ_IRQ_NR_GET, &irq_nr);
			if (rc) {
				rhea_error("Error when getting IRQ number "
					   "EQ[%u]: %i",
					   session->eqs[i]->id, rc);
			}

			if (irq_nr)
				disable_irq(irq_nr);
		}
	}

	/* check channel */
	for (i = 0; i < ARRAY_SIZE(session->channels); ++i) {
		if (NULL != session->channels[i]) {
			rc = _rhea_channel_free(rhea_id, i);
			if (rc)
				rhea_error("Error when freeing channel "
					   "id[%u]: %i",
					   session->channels[i]->id, rc);
		}
	}

	/* check QPs */
	for (i = 0; i < ARRAY_SIZE(session->qps); ++i) {
		if (NULL != session->qps[i]) {
			rc = _rhea_qp_free(rhea_id, i);
			if (rc) {
				rhea_error("Error when freeing QP[%u]: %i",
					   session->qps[i]->id, rc);
			}
		}
	}

	/* check CQs */
	for (i = 0; i < ARRAY_SIZE(session->cqs); ++i) {
		if (NULL != session->cqs[i]) {
			rc = _rhea_cq_free(rhea_id, i);
			if (rc) {
				rhea_error("Error when freeing CQ[%u]: %i",
					   session->cqs[i]->id, rc);
			}
		}
	}

	/* check EQs */
	for (i = 0; i < ARRAY_SIZE(session->eqs); ++i) {
		if (NULL != session->eqs[i]) {
			rc = _rhea_eq_free(rhea_id, i);
			if (rc) {
				rhea_error("Error when freeing EQ[%u]: %i",
					   session->eqs[i]->id, rc);
			}
		}
	}

	kfree(session);

	/* delete session */
	sessions->ids[rhea_id] = NULL;
	--sessions->alloced;

	return rc;
}


int rhea_session_fini(unsigned rhea_id)
{
	int rc = 0;
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	rhea_debug("rhea_session_fini(%d)", rhea_id);

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	/* in case somebody is still using it, at least it sleeps forever ;-) */
	spin_lock(&sessions->ids[rhea_id]->lock);

	spin_unlock(&sessions->lock);

	rc = _rhea_session_fini(sessions->ids[rhea_id]);
	if (rc)
		rhea_error("Was not able free all adapter resources");

	return rc;
}
EXPORT_SYMBOL(rhea_session_fini);



static int _rhea_register_manager(unsigned pport_nr,
				  struct rhea_bc_mc_uc_manager *callbacks)
{
	int rc = 0;
	struct rhea_bc_mc_uc_manager *manager;

	if (!is_hea_pport(pport_nr) ||
	    NULL == callbacks ||
	    NULL == callbacks->manager_lport_register ||
	    NULL == callbacks->manager_lport_unregister ||
	    NULL == callbacks->manager_fini ||
	    NULL == callbacks->manager_init ||
	    NULL == callbacks->manager_lport_enable ||
	    NULL == callbacks->manager_lport_disable ||
	    NULL == callbacks->channel_destroy ||
	    NULL == callbacks->channel_create ||
	    NULL == callbacks->channel_registered_count) {
		rhea_error("Input not valid");
		return -EINVAL;
	}

	if (RHEA_VALID_INSTANCE_CHECK(s_rhea_context.manager, pport_nr)) {
		rhea_error("Already a manager registered for pport: %u",
			   pport_nr + 1);
		return -EINVAL;
	}

	manager = rhea_align_alloc(sizeof(*manager),
				   __alignof__(*manager), GFP_KERNEL);
	if (NULL == manager) {
		rhea_error("Was not able to get memory for manager");
		return -EINVAL;
	}

	/* save callback information for manager */
	*manager = *callbacks;

	/* initialise the manager */
	rc = (*manager->manager_init) (pport_nr);
	if (rc) {
		rhea_error("Was not able to initialise the "
			   "BC/MC/UC manager for pport: %u",
			   pport_nr + 1);
		rhea_align_free(manager, sizeof(*manager));
		return -EINVAL;
	}

	s_rhea_context.manager[pport_nr] = manager;

	return rc;
}

static int _rhea_unregister_manager(unsigned pport_nr)
{
	int rc = 0;
	struct rhea_bc_mc_uc_manager *manager;

	rhea_debug("Unregister manager");

	if (!is_hea_pport(pport_nr))
		return -EINVAL;

	if (!RHEA_VALID_INSTANCE_CHECK(s_rhea_context.manager, pport_nr)) {
		rhea_error("No manager registered for pport: %u",
			   pport_nr + 1);
		return -EINVAL;
	}

	/* get manager */
	manager = s_rhea_context.manager[pport_nr];
	s_rhea_context.manager[pport_nr] = NULL;

	/* make sure that all resources are freed */
	rc = (*manager->manager_fini)(pport_nr);
	if (rc)
		rhea_error("Error when freeing MC/BC/UC manager");

	/* free memory which was allocated for manager */
	rhea_align_free(manager, sizeof(*manager));

	return rc;
}




int rhea_adapter_init(struct hea_adapter *ap)
{
	int i;
	int rc = 0;

	u64 map_base_end;	/* The start of a register region. */
	unsigned qnum;
	unsigned lg = ((PAGE_SIZE == 64 << 10) ? 1 : 0);
	unsigned pport_nr;
	u64 reg;

	if (NULL == ap)
		return -EINVAL;

	if (lg)
		rhea_debug("Using large pages (64k)");
	else
		rhea_debug("Using page size: %lu", PAGE_SIZE);

	/* set rhea_context to 0 */
	memset(&s_rhea_context, 0, sizeof(s_rhea_context));

	/* setup session */
	s_rhea_context.sessions.alloced_max =
		ARRAY_SIZE(s_rhea_context.sessions.ids);

	spin_lock_init(&s_rhea_context.sessions.lock);

	rhea_debug("Save instance number: %i", ap->instance);
	s_rhea_context.aps[ap->instance] = ap;

	/* check if this size can be obtained from somewhere */
	ap->max_region_size = 0x800000;

	/* map HEA MMIO register space */
	ap->mmio = rhea_ioremap("HEA core", ap->gba, ap->max_region_size);
	if (NULL == ap->mmio) {
		rhea_error
			("Was not able to map address: 0x%lx and size: 0x%lx",
			 ap->gba, ap->max_region_size);
		return -ENODEV;
	}

	rhea_debug("Base Region size: %lx and GBA: 0x%lx", ap->max_region_size,
		   ap->gba);

	map_base_end = ap->gba + ap->max_region_size;

	/*
	 * Reset HEA base registers to default values
	 */
	rhea_gen_reset(ap->mmio);

	/*
	 * Event Queues
	 */
	qnum = rhea_eq_init(&ap->mmio->base);

	rhea_debug("Max EQs: %u", qnum);

	map_base_end = rhea_eq_qbase_init(&ap->mmio->base,
					  HEA_PRIV_SUPER,
					  qnum, lg, map_base_end);

	map_base_end = rhea_eq_qbase_init(&ap->mmio->base,
					  HEA_PRIV_PRIV,
					  qnum, lg, map_base_end);

	/* set interrupt base register */
	reg = hea_set_u64_bits(0x0ULL, ap->hwirq_base, 53, 63);
	out_be64(&ap->mmio->base.g_buidbase, reg);

	/*
	 * Completion Queues
	 */
	qnum = rhea_cq_init(&ap->mmio->base);

	rhea_debug("Max CQs: %u", qnum);

	map_base_end = rhea_cq_qbase_init(&ap->mmio->base,
					  HEA_PRIV_SUPER,
					  qnum, lg, map_base_end);

	map_base_end = rhea_cq_qbase_init(&ap->mmio->base,
					  HEA_PRIV_PRIV,
					  qnum, lg, map_base_end);

	map_base_end = rhea_cq_qbase_init(&ap->mmio->base,
					  HEA_PRIV_USER,
					  qnum, lg, map_base_end);

	/*
	 * Queue Pairs
	 */

	qnum = rhea_qp_init(&ap->mmio->base);
	rhea_debug("Max QPs: %u", qnum);

	map_base_end = rhea_qp_qbase_init(&ap->mmio->base,
					  HEA_PRIV_SUPER,
					  qnum, lg, map_base_end);

	map_base_end = rhea_qp_qbase_init(&ap->mmio->base,
					  HEA_PRIV_PRIV,
					  qnum, lg, map_base_end);

	map_base_end = rhea_qp_qbase_init(&ap->mmio->base,
					  HEA_PRIV_USER,
					  qnum, lg, map_base_end);

	rc = hea_eq0_alloc(&s_rhea_context.eq0, ap);
	if (rc) {
		rhea_error("Was not able to allocate EQ0");
		return rc;
	}

	/*
	 * Physical Port Config
	 */
	for (pport_nr = 0; pport_nr < ap->pport_count; ++pport_nr) {
		/* save physical port configuration data */
		rc = rhea_pport_init(ap->mmio, &ap->pports[pport_nr]);
		if (rc) {
			rhea_error("Could not configure physical "
					   "port: %u",
					   pport_nr + 1);
			return rc;
		}
	}

	rhea_port_rules(ap->mmio->pport);

	for (i = 0; i < ap->pport_count; ++i) {
		struct rhea_bc_mc_uc_manager callbacks;

		callbacks.manager_init = &rhea_mc_bc_manager_init;
		callbacks.manager_fini = &rhea_mc_bc_manager_fini;

		callbacks.manager_lport_register =
			&rhea_mc_bc_manager_lport_register;
		callbacks.manager_lport_unregister =
			&rhea_mc_bc_manager_lport_unregister;

		callbacks.manager_lport_enable =
			&rhea_mc_bc_manager_lport_enable;
		callbacks.manager_lport_disable =
			&rhea_mc_bc_manager_lport_disable;

		callbacks.channel_destroy = &rhea_mc_bc_channel_destroy;
		callbacks.channel_create = &rhea_mc_bc_channel_create;

		callbacks.channel_registered_count =
			&rhea_mc_bc_channel_registered_count;

		/* check if this physical port has been registered */

		rc = _rhea_register_manager(i, &callbacks);
		if (rc) {
				rhea_error("When creating MC/BC manager "
					   "for pport: %u", i);
				return rc;
			}
	}

	if (0)
		rhea_gen_dump(ap->mmio);

	return rc;
}

int rhea_gen_dumps(unsigned rhea_id)
{
	struct rhea_sessions *sessions = &s_rhea_context.sessions;

	spin_lock(&sessions->lock);

	if (!RHEA_VALID_INSTANCE_CHECK(sessions->ids, rhea_id)) {
		spin_unlock(&sessions->lock);
		return -EINVAL;
	}

	rhea_gen_dump(s_rhea_context.
		      aps[sessions->ids[rhea_id]->adapter_number]->mmio);

	spin_unlock(&sessions->lock);

	return 0;
}
EXPORT_SYMBOL(rhea_gen_dumps);

int rhea_adapter_fini(struct hea_adapter *ap)
{
	int i;
	int rc = 0;

	if (NULL == ap)
		return -EINVAL;

	spin_lock(&s_rhea_context.sessions.lock);

	for (i = 0; i < ARRAY_SIZE(s_rhea_context.sessions.ids); ++i) {
		if (NULL != s_rhea_context.sessions.ids[i]) {
			rc = _rhea_session_fini(s_rhea_context.sessions.
						ids[i]);
			if (rc) {
				rhea_error("Error when freeing rhea_id: %u",
					   s_rhea_context.sessions.ids[i]->id);
			}
		}
	}

	spin_unlock(&s_rhea_context.sessions.lock);

	for (i = 0; i < ap->pport_count; ++i) {
		rc = _rhea_unregister_manager(i);
		if (rc) {
				rhea_error("Was not able to free the "
					   "BC/MC/UC manager");
		}
	}

	spin_lock(&s_rhea_context.sessions.lock);

	/* if we have an EQ0 --> destroy and free resources */
	if (s_rhea_context.eq0.eq) {
		rhea_debug("Clean EQ0");
		rc = hea_eq0_free(&s_rhea_context.eq0);
		if (rc)
			rhea_error("Could not free EQ0");

	}

	/* free all Q resources */
	rhea_debug("Free QP");
	rhea_qp_qbase_fini(&ap->mmio->base, HEA_PRIV_SUPER);
	rhea_qp_qbase_fini(&ap->mmio->base, HEA_PRIV_PRIV);
	rhea_qp_qbase_fini(&ap->mmio->base, HEA_PRIV_USER);
	rhea_qp_fini(&ap->mmio->base);

	rhea_debug("Free CQ");
	rhea_cq_qbase_fini(&ap->mmio->base, HEA_PRIV_SUPER);
	rhea_cq_qbase_fini(&ap->mmio->base, HEA_PRIV_PRIV);
	rhea_cq_qbase_fini(&ap->mmio->base, HEA_PRIV_USER);
	rhea_cq_fini(&ap->mmio->base);

	rhea_debug("Free EQ");
	rhea_eq_qbase_fini(&ap->mmio->base, HEA_PRIV_SUPER);
	rhea_eq_qbase_fini(&ap->mmio->base, HEA_PRIV_PRIV);
	rhea_eq_fini(&ap->mmio->base);

	/*
	 * Reset HEA base registers to default values
	 */
	rhea_gen_reset(ap->mmio);

	/* make sure all the resources get unmapped */
	rhea_iounmap(ap->mmio, ap->gba, ap->max_region_size);
	ap->mmio = 0;

	/* clear all the rest */
	memset(&s_rhea_context, 0, sizeof(s_rhea_context));

	/* make sure we are not longer remembered */
	s_rhea_context.aps[ap->instance] = NULL;

	return rc;
}
EXPORT_SYMBOL(rhea_adapter_fini);
