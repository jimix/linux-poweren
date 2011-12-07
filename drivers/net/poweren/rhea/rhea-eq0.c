#include "rhea-eq0.h"

#include <linux/kernel.h>
#include <linux/timer.h>

#include <hea-queue.h>
#include <rhea-linux.h>
#include <rhea-funcs.h>

#include <asm/poweren_hea_cq.h>

#include "rhea-channel.h"
#include "rhea-qp.h"
#include "rhea-cq.h"
#include "rhea-eq.h"


int eq0_pport_event_callback_register(struct rhea_eq0 *eq0,
				      unsigned int pport_nr,
				      enum hea_channel_type type,
				      struct hea_eq0_pport_state_change *event)
{
	struct hea_eq0_pport_state_change *conf;

	if (NULL == event || NULL == eq0)
		return -EINVAL;

	spin_lock(&eq0->lock);

	conf = &eq0->pport_state_change[pport_nr][type];

	/* copy data over */
	memcpy(conf, event, sizeof(*conf));

	spin_unlock(&eq0->lock);

	return 0;
}

int eq0_pport_event_callback_unregister(struct rhea_eq0 *eq0,
					unsigned int pport_nr,
					enum hea_channel_type type)
{
	struct hea_eq0_pport_state_change *conf;

	if (NULL == eq0)
		return -EINVAL;

	spin_lock(&eq0->lock);

	conf = &eq0->pport_state_change[pport_nr][type];

	memset(conf, 0, sizeof(*conf));

	spin_unlock(&eq0->lock);

	return 0;
}

static inline int eq0_signal_send(struct hea_process *process, int signal_nr)
{
	return hea_signal_send(process, signal_nr);
}

static inline int _eq0_irq_port_event(struct rhea_eq0 *eq0,
				     unsigned int pport_nr)
{
	int rc;
	int i;
	int link_state;

	rhea_debug("pport state change: %u", pport_nr + 1);

	/* handle with the state change in the system */
	link_state = rhea_pport_link_state_get(pport_nr);
	if (0 > link_state) {
		rhea_error("Was not able to change the state of the channel");
		rc = link_state;
		goto out;
	}

	rc = rhea_pport_err_reset(pport_nr);
	if (rc)
		rhea_error("Could not reset uaelog for pport: %u",
			   pport_nr + 1);

	rhea_debug("Found state change in pport[%u] to %u",
			   pport_nr + 1, link_state);

	/* let the rest know what happened */
	for (i = 0; i < HEA_MAX_PPORT_CHANNEL_COUNT; ++i) {

		hea_pport_link_state_callback_t callback =
			eq0->pport_state_change[pport_nr][i].fkt_ptr;

		/* check for registered call-backs */
		if (callback) {
			void *args;

			args = eq0->pport_state_change[pport_nr][i].args;

			/* execute callback */
			rc = (*callback)(pport_nr, link_state, args);
			if (rc) {
				rhea_error("Error when dealing with "
					   "link state change");
				goto out;
			}
		}
	}

	return 0;
out:
	return rc;
}


static void eq0_scan_eq(struct work_struct *work)
{
	int rc = 0;
	int error_count = 0;
	struct rhea_eq0 *eq0;
	struct hea_eqe *eqe_current;
	const unsigned int sig_nr = SIGTERM;

	struct delayed_work *delayed_work;

	if (NULL == work)
		return;

	delayed_work = to_delayed_work(work);
	eq0 = container_of(delayed_work, struct rhea_eq0, irq_work);
	if (NULL == eq0)
		return;

	while (!spin_trylock(&eq0->lock)) {
		if (0 != __sync_add_and_fetch(&eq0->stop, 0))
			return;
	}

	if (0 != __sync_add_and_fetch(&eq0->stop, 0)) {
		spin_unlock(&eq0->lock);
		return;
	}

	/* get current eqe */
	eqe_current = eq0->q.qe_current;
	if (NULL == eqe_current)
		return;

	rhea_debug("event 0x%016llx", eqe_current->eqe);

	while (hea_eqe_is_valid(eqe_current)) {
		rhea_debug(" event 0x%016llx", eqe_current->eqe);

		switch (hea_eqe_event_type(eqe_current)) {
		case HEA_EQE_ET_PORT_EVENT:
		{
			/* get physical port number */
			int pport_nr =
				hea_eqe_pport_number(eqe_current) - 1;

			/* handle the port event */
			rc = _eq0_irq_port_event(eq0, pport_nr);
			if (rc) {
				++error_count;
				goto MARKER_NEXT_QE;
			}
		}
		break;

		case HEA_EQE_ET_QP_ERROR:
		case HEA_EQE_ET_QP_ERROR_EQ0:
		{
			struct rhea_qp *qp;
			unsigned int qp_id;

			qp_id = hea_eqe_qp_number(eqe_current);

			qp = _rhea_qp_get(qp_id);
			if (NULL == qp)
				continue;

			if (qp->process.pid) {
				eq0_signal_send(&qp->process, sig_nr);
			} else {
				rhea_error("Found QP error in QP %u "
					   " which could not be "
					   "dealt with.", qp_id);
				BUG_ON(1);
			}
		}
		break;

		case HEA_EQE_ET_CQ_ERROR:
		case HEA_EQE_ET_CQ_ERROR_EQ0:
		{
			struct rhea_cq *cq;
			unsigned int cq_id;

			cq_id = hea_eqe_cq_number(eqe_current);

			cq = _rhea_cq_get(cq_id);
			if (NULL == cq)
				continue;

			if (cq->process.pid)
				eq0_signal_send(&cq->process, sig_nr);
			else {
				rhea_error("Found CQ error in CQ %u"
					   " which could not be "
					   "dealt with.", cq_id);
				BUG_ON(1);
			}
		}
		break;

		case HEA_EQE_ET_EQ_ERROR:
		{
			struct rhea_eq *eq;
			unsigned int eq_id;

			eq_id = hea_eqe_eq_number(eqe_current);

			eq = _rhea_eq_get(eq_id);
			if (NULL == eq)
				continue;

			if (eq->process.pid) {
				eq0_signal_send(&eq->process, sig_nr);
			} else {
				rhea_error("Found EQ error in EQ %u"
					   " which could not be "
					   "dealt with.", eq_id);
				BUG_ON(1);
			}
		}
		break;

		case HEA_EQE_ET_QP_WARNING:
		case HEA_EQE_ET_CP_WARNING:

		case HEA_EQE_ET_FIRST_ERROR_CAPTURE_INFO:
		case HEA_EQE_ET_COP_CQ_ACCESS_ERROR:
		case HEA_EQE_ET_COP_QP_ACCESS_ERROR:
		case HEA_EQE_ET_COP_TICKET_ACCESS_ERROR:
		case HEA_EQE_ET_COP_TICKET_ERROR:
		case HEA_EQE_ET_COP_DATA_ERROR:

		default:
		rhea_error("EQ was called and I don't know why: %i",
			   hea_eqe_event_type(eqe_current));
		BUG_ON(1);
		break;
		}

MARKER_NEXT_QE:

		eqe_current->eqe = 0;
		heaq_set_next_qe(&eq0->q);
		eqe_current = eq0->q.qe_current;
	}

	spin_unlock(&eq0->lock);

	return;
}

irqreturn_t eq0_irq_handler(int irq, void *args)
{
	struct rhea_eq0 *eq0;

	rhea_debug("EQ0 IRQ Handler started for IRQ: %i", irq);

	if (NULL == args)
		return IRQ_NONE;

	eq0 = (struct rhea_eq0 *) args;

	while (queue_delayed_work(eq0->irq_workqueue, &eq0->irq_work, 0))
		;

	rhea_debug("Leaving EQ0 IRQ Handler for IRQ: %i", irq);

	return IRQ_HANDLED;
}

static void hea_eq0_timer_pport_event(struct work_struct *work)
{
	int pport_nr;
	int link_state;
	struct rhea_eq0 *eq0;

	if (NULL == work)
		return;

	eq0 = container_of(work, struct rhea_eq0, timer_work);
	if (NULL == eq0)
		return;

	while (!spin_trylock(&eq0->lock)) {
		if (0 != __sync_add_and_fetch(&eq0->stop, 0))
			return;
	}

	if (0 != __sync_add_and_fetch(&eq0->stop, 0)) {
		spin_unlock(&eq0->lock);
		return;
	}

	for (pport_nr = 0; pport_nr < 2; ++pport_nr) {

		/* get link state */
		link_state = rhea_pport_link_state_get(pport_nr);
		if (0 > link_state) {
			rhea_error("Was not able to get the state "
				   "of the pport: %u", pport_nr + 1);
			goto out;
		}

		/* check for a new state, if one has been found --> act */
		if (link_state != eq0->link_state[pport_nr]) {
			rhea_debug("Found different link state");

			/* get new state */
			eq0->link_state[pport_nr] = link_state;

			/* handle port event */
			_eq0_irq_port_event(eq0, pport_nr);
		}

		/* schedule next timer */
		if (likely(0 == eq0->stop)) {
			mod_timer(&eq0->timer,
				  jiffies +
				msecs_to_jiffies(CONFIG_POWEREN_RHEA_TIMER_MS));
		}
	}
out:
	spin_unlock(&eq0->lock);
}

void hea_eq0_timer_callback(unsigned long data)
{
	struct rhea_eq0 *eq0 = (struct rhea_eq0 *) data;

	if (NULL == eq0)
		return;

	while (schedule_work(&eq0->timer_work))
		;

	return;
}


int hea_eq0_alloc(struct rhea_eq0 *eq0, struct hea_adapter *ap)
{
	int rc;
	struct hea_eq_context context_eq;
	struct hea_process process = { 0 };

	if (NULL == eq0) {
		rhea_error("Invalid parameters passed in");
		return -EINVAL;
	}

	rhea_debug("Create EQ0 for hypervisor");

	context_eq.cfg.eqe_count = 16;
	context_eq.cfg.coalesing2_delay = HEA_EQ_COALESING_DELAY_0;

	context_eq.cfg.generate_completion_events =
					HEA_EQ_GEN_COM_EVENT_DISABLE;

	context_eq.cfg.irq_type = HEA_IRQ_COALESING_2;

	/* make sure we create a real EQ0 */
	process.lpar = 0xFF;

	eq0->eq = rhea_eq_create(&process, &context_eq.cfg);
	if (NULL == eq0->eq) {
		rhea_error("Was not able to allocate EQ0");
		return -ENOMEM;
	}

	/* make sure that this EQ is released again */
	if (0 != eq0->eq->id) {
		rhea_error("Was not able to get EQ0");
		rhea_eq_destroy(eq0->eq);
		return -EPERM;
	}

	/* set base information */
	eq0->q.q_begin = (unsigned char *) eq0->eq->q.va;
	eq0->q.qe_size = sizeof(struct hea_eqe);
	eq0->q.qe_count = eq0->eq->q.size / sizeof(struct hea_eqe);

	/* initialise rest */
	heaq_init(&eq0->q);

	eq0->irq_workqueue = create_singlethread_workqueue("EQ0");
	if (NULL == eq0->irq_workqueue) {
		rhea_error("Was not able to allocate workqueue");
		return -ENOMEM;
	}

	/* prepare work queue */
	INIT_DELAYED_WORK(&eq0->irq_work, &eq0_scan_eq);
	PREPARE_DELAYED_WORK(&eq0->irq_work, &eq0_scan_eq);

	INIT_WORK(&eq0->timer_work, &hea_eq0_timer_pport_event);
	PREPARE_WORK(&eq0->timer_work, &hea_eq0_timer_pport_event);

	/* timer */
	setup_timer(&eq0->timer,
		    hea_eq0_timer_callback, (ulong) eq0);

	rc = mod_timer(&eq0->timer,
		       jiffies +
		       msecs_to_jiffies(CONFIG_POWEREN_RHEA_TIMER_MS));
	if (rc) {
		rhea_error("Error in mod_timer");
		goto out;
	}


	rc = rhea_interrupts_setup(eq0->eq, ap->name, ap->hwirq_base,
				   ap->hwirq_count, eq0_irq_handler, eq0);
	if (rc) {
		rhea_error("Was not able to register interupt "
			   "handler for EQ0");
		goto out;
	}

	spin_lock_init(&eq0->lock);

	return 0;
out:
	if (eq0->eq)
		rhea_eq_destroy(eq0->eq);

	if (eq0->irq_workqueue)
		destroy_workqueue(eq0->irq_workqueue);

	del_timer(&eq0->timer);

	return rc;
}


int hea_eq0_free(struct rhea_eq0 *eq0)
{
	int rc;

	if (NULL == eq0) {
		rhea_error("Invalid parameters passed in");
		return -EINVAL;
	}

	rhea_debug("Clean EQ0");

	spin_lock(&eq0->lock);

	/* tell handler to go away */
	eq0->stop = 1;

	rc = rhea_eq_destroy(eq0->eq);
	if (rc)
		rhea_error("Could not free EQ0");

	/* wait for work queue to finish */
	if (eq0->irq_workqueue) {
		cancel_delayed_work(&eq0->irq_work);
		flush_workqueue(eq0->irq_workqueue);
		/* delete queue */
		destroy_workqueue(eq0->irq_workqueue);
		eq0->irq_workqueue = NULL;
	}

	del_timer_sync(&eq0->timer);

	memset(eq0, 0, sizeof(*eq0));

	return rc;
}
