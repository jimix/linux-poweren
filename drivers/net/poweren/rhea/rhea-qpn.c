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

#include "rhea-channel.h"
#include "rhea-base.h"

static int _rhea_qpn_register_reset(struct rhea_pport *pport)
{
	int rc = 0;

	if (NULL == pport)
		return -EINVAL;

	out_be64(&pport->pport_regs->bpfc.p_rcu, 0x0ULL);
	out_be64(&pport->pport_regs->bpfc.p_rcm, 0x0ULL);
	out_be64(&pport->pport_regs->bpfc.p_rcb, 0x0ULL);

	return rc;
}

static int _rhea_qpn_channel_register_set(struct rhea_channel *channel,
					  unsigned slot_base)
{
	int rc = 0;
	u64 reg;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	/* always get pport from channel */
	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* set qpn offset in port registers */
	switch (channel->type) {
	case HEA_UC_PORT:

		reg = in_be64(&pport->pport_regs->bpfc.p_rcu);
		reg = hea_set_u64_bits(reg, slot_base, 3, 7);
		out_be64(&pport->pport_regs->bpfc.p_rcu, reg);
		break;

	case HEA_MC_PORT:

		reg = in_be64(&pport->pport_regs->bpfc.p_rcm);
		reg = hea_set_u64_bits(reg, slot_base, 3, 7);
		out_be64(&pport->pport_regs->bpfc.p_rcm, reg);
		break;

	case HEA_BC_PORT:

		reg = in_be64(&pport->pport_regs->bpfc.p_rcb);
		reg = hea_set_u64_bits(reg, slot_base, 3, 7);
		out_be64(&pport->pport_regs->bpfc.p_rcb, reg);
		break;

	case HEA_LPORT_0:

		reg = in_be64(&pport->pport_regs->bpfc.pl_rc[0]);
		reg = hea_set_u64_bits(reg, slot_base, 3, 7);
		out_be64(&pport->pport_regs->bpfc.pl_rc[0], reg);
		break;

	case HEA_LPORT_1:

		reg = in_be64(&pport->pport_regs->bpfc.pl_rc[1]);
		reg = hea_set_u64_bits(reg, slot_base, 3, 7);
		out_be64(&pport->pport_regs->bpfc.pl_rc[1], reg);
		break;

	case HEA_LPORT_2:

		reg = in_be64(&pport->pport_regs->bpfc.pl_rc[2]);
		reg = hea_set_u64_bits(reg, slot_base, 3, 7);
		out_be64(&pport->pport_regs->bpfc.pl_rc[2], reg);
		break;

	case HEA_LPORT_3:

		reg = in_be64(&pport->pport_regs->bpfc.pl_rc[3]);
		reg = hea_set_u64_bits(reg, slot_base, 3, 7);
		out_be64(&pport->pport_regs->bpfc.pl_rc[3], reg);
		break;

	default:

		rhea_error("Invalid type");
		return -EINVAL;
	}

	return rc;
}

int _rhea_qpn_init(struct rhea_pport *pport)
{
	int rc = 0;

	if (NULL == pport)
		return -EINVAL;

	rc = _rhea_channel_resource_init(&pport->qpn, RHEA_QPN_ARRAY_SIZE,
					 1, 1, 1);

	_rhea_qpn_register_reset(pport);

	return rc;
}

int _rhea_qpn_fini(struct rhea_pport *pport)
{
	int rc = 0;

	if (NULL == pport)
		return -EINVAL;

	rc = _rhea_channel_resource_fini(&pport->qpn);
	if (rc)
		rhea_error("Error when cleaning QPN resources");

	_rhea_qpn_register_reset(pport);

	return rc;
}

unsigned rhea_qpn_max(struct rhea_channel *channel)
{
	int max = 0;
	struct rhea_pport *pport;
	struct rhea_channel_resource_block *block_current = NULL;

	if (NULL == channel)
		return 0;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return 0;
	}

	spin_lock(&pport->lock);

	/* find biggest available block */
	_rhea_channel_resource_max(&pport->qpn,
				   pport->qpn.alloced_max, &block_current);

	if (block_current)
		max = block_current->alloced;

	spin_unlock(&pport->lock);

	return max;
}

static int _rhea_qpn_set(struct rhea_channel *channel,
			 unsigned qp_id, unsigned qpn_offset)
{
	int rc = 0;
	int real_index;
	u64 reg;
	struct rhea_pport *pport = NULL;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	rc = _rhea_channel_resource_index_get(channel->qpn,
					      channel->qpn_base,
					      qpn_offset, &real_index);
	if (rc) {
		rhea_error("Was not able to find index in map");
		return -EINVAL;
	}

	rhea_debug("Map QP[%u] to QPN of pport: %u to slot: %u",
		   qp_id, channel->pport_nr + 1, real_index);

	/* set qpn */
	reg = in_be64(&pport->pport_regs->bpfc.pg_qpn[real_index]);
	reg = hea_set_u64_bits(reg, qp_id, 57, 63);
	out_be64(&pport->pport_regs->bpfc.pg_qpn[real_index], reg);

	return rc;
}

int rhea_qpn_set(struct rhea_channel *channel,
		 unsigned qp_id, unsigned qpn_offset)
{
	int rc = 0;
	struct rhea_pport *pport = NULL;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	rc = _rhea_qpn_set(channel, qp_id, qpn_offset);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to find index in map");
		return -EINVAL;
	}

	spin_unlock(&pport->lock);

	return rc;
}

int rhea_qpn_alloc(struct rhea_channel *channel, struct hea_qpn_cfg *qpn_cfg)
{
	int rc = 0;
	unsigned requested_slots;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == qpn_cfg)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	if (NULL == channel->qpn) {
		/* make sure it can be used by somebody */
		rc = _rhea_channel_map_init(&channel->qpn);
		if (rc) {
			rhea_error("Was not able to free QPN map");
			return rc;
		}
	}

	requested_slots = 1 << qpn_cfg->slot_count;

	if (RHEA_QPN_ARRAY_SIZE < requested_slots) {
		rhea_error("Number of requested QPN slots is too high");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	if (channel->qpn->alloced) {
		spin_unlock(&pport->lock);
		rhea_error("Already allocated qpn block for this channel!");
		return -EINVAL;
	}

	rc = _rhea_channel_resource_alloc(&pport->qpn, channel->qpn,
					  requested_slots, &channel->qpn_base);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Error when allocating resource for channel");
		return rc;
	}

	rc = _rhea_qpn_channel_register_set(channel, channel->qpn_base);
	if (rc) {
		_rhea_channel_resource_free(&pport->qpn, channel->qpn,
					    channel->qpn_base);
		spin_unlock(&pport->lock);
		rhea_error("Error when writing qpn registers");
		return -EINVAL;
	}

	rhea_info("Allocated %u QPN slots from base: %u and channel: "
		  "%u of pport: %u",
		  channel->qpn->alloced, channel->qpn_base, channel->type,
		  channel->pport_nr + 1);

	spin_unlock(&pport->lock);

	return rc;
}

int _rhea_qpn_free(struct rhea_channel *channel)
{
	int rc = 0;
	unsigned int qpn_offset;
	struct rhea_pport *pport;
	struct rhea_channel_resource_map *qpn_map;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	if (NULL == channel->qpn) {
		rhea_error("QPN is not initialised");
		return -EINVAL;
	}

	rhea_info
		("Free %u QPN slots from base: %u and channel: %u of pport: %u",
		 channel->qpn->alloced, channel->qpn_base, channel->type,
		 channel->pport_nr + 1);

	if (1 >= channel->qpn->instance_count) {
		qpn_map =
			_rhea_channel_resource_map_element_first(channel->qpn);

		if (NULL != qpn_map) {
			/* reset qpn registers to default value */
			for (qpn_offset = 0; qpn_offset < qpn_map->alloced;
			     ++qpn_offset) {
				rc = _rhea_qpn_set(channel, 0, qpn_offset);
				if (rc) {
					rhea_error("Was not able to find "
						   "index in map");
					return -EINVAL;
				}
			}
		}

		/* mark block as free */
		rc = _rhea_channel_resource_free(&pport->qpn, channel->qpn,
						 channel->qpn_base);
		if (rc) {
			rhea_error("Was not able to free resources");
			return rc;
		}
	}

	rc = _rhea_channel_map_fini(&pport->qpn, &channel->qpn);
	if (rc)
		rhea_error("Was not able to free QPN map");

	/* save slot base */
	channel->qpn_base = 0;

	return rc;
}

int rhea_qpn_free(struct rhea_channel *channel)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	/* mark block as free */
	rc = _rhea_qpn_free(channel);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to free resources");
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

int rhea_qpn_share(struct rhea_channel *channel_target,
		   const struct rhea_channel *channel_source)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel_target || NULL == channel_source ||
	    NULL == channel_source->qpn) {
		rhea_debug("channel_source: %p", channel_source);
		rhea_debug("channel_target: %p", channel_target);
		rhea_debug("channel_source->qpn: %p", channel_source->qpn);
		return -EINVAL;
	}

	/* if both are the same --> don't bother */
	if (channel_target == channel_source) {
		rhea_debug("Both pointers are the same");
		return 0;
	}

	pport = _rhea_pport_get(channel_source->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	/* copy qpn information */
	_rhea_channel_map_share(&channel_target->qpn, channel_source->qpn);
	channel_target->qpn_base = channel_source->qpn_base;

	/* register qpn */
	rc = _rhea_qpn_channel_register_set(channel_target,
					    channel_target->qpn_base);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Error when writing qpn registers");
		return -EINVAL;
	}

	spin_unlock(&pport->lock);

	rhea_info("Shared %u QPN slots from base: %u and channel: "
		  "%u of pport: %u and count: %u(%u)",
		  channel_target->qpn->alloced, channel_target->qpn_base,
		  channel_target->type, channel_target->pport_nr + 1,
		  channel_source->qpn->instance_count,
		  channel_target->qpn->instance_count);

	return rc;
}
