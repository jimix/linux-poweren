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

static void _rhea_tcam_register_reset(struct rhea_pport_bpfc *bpfc)
{
	int i;

	if (NULL == bpfc)
		return;

	/* reset tcam registers */
	for (i = 0; i < ARRAY_SIZE(bpfc->pg_tcampr); ++i)
		out_be64(&bpfc->pg_tcampr[i], 0x0ULL);

	for (i = 0; i < ARRAY_SIZE(bpfc->pg_tcamm); ++i)
		out_be64(&bpfc->pg_tcamm[i], 0x0ULL);
}

static void _rhea_tcam_channel_register_set(struct rhea_channel *channel,
					    unsigned tcam_bits)
{
	u64 reg = 0x0ULL;
	struct rhea_pport *pport;
	struct rhea_pport_bpfc *bpfc;

	if (NULL == channel)
		return;

	/* always get pport from channel */
	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return;
	}

	bpfc = &pport->pport_regs->bpfc;

	/* reset number of used hash bits for all channels */
	switch (channel->type) {
	case HEA_UC_PORT:

		reg = in_be64(&bpfc->p_rcu);
		reg = hea_set_u64_bits(reg, tcam_bits, 9, 11);
		out_be64(&bpfc->p_rcu, reg);
		break;

	case HEA_MC_PORT:

		reg = in_be64(&bpfc->p_rcm);
		reg = hea_set_u64_bits(reg, tcam_bits, 9, 11);
		out_be64(&bpfc->p_rcm, reg);
		break;

	case HEA_BC_PORT:

		reg = in_be64(&bpfc->p_rcb);
		reg = hea_set_u64_bits(reg, tcam_bits, 9, 11);
		out_be64(&bpfc->p_rcb, reg);
		break;

	default:

		if (is_hea_lport(channel->type)) {
			int lport_index = hea_lport_index_get(channel->type);

			reg = in_be64(&bpfc->pl_rc[lport_index]);
			reg = hea_set_u64_bits(reg, tcam_bits, 9, 11);

			out_be64(&bpfc->pl_rc[lport_index], reg);
		}
		break;
	}

	return;
}

static int _rhea_tcam_register_set(struct rhea_channel *channel,
				   unsigned tcam_base,
				   unsigned tcam_offset,
				   unsigned qpn_offset,
				   unsigned pattern, unsigned mask)
{

	int rc = 0;
	unsigned tcam_index;
	unsigned qpn_index;
	u64 reg_pattern;
	u64 reg_mask;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	if (NULL == channel->tcam || NULL == channel->qpn) {
		rhea_error("TCAM or QPN is not allocated");
		return -EINVAL;
	}

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* get the real index in the TCAM array */
	rc = _rhea_channel_resource_index_get(channel->tcam,
					      tcam_base, tcam_offset,
					      &tcam_index);
	if (rc) {
		rhea_error("Was not able to find index in TCAM map");
		return -EINVAL;
	}

	/* check if the offset is valid for this channel */
	rc = _rhea_channel_resource_index_get(channel->qpn,
					      channel->qpn_base, qpn_offset,
					      &qpn_index);
	if (rc) {
		rhea_error("Was not able to find index in QPN map");
		return -EINVAL;
	}

	reg_pattern = in_be64(&pport->pport_regs->bpfc.pg_tcampr[tcam_index]);
	reg_mask = in_be64(&pport->pport_regs->bpfc.pg_tcamm[tcam_index]);

	/* set pattern and mask */
	reg_pattern = hea_set_u64_bits(reg_pattern, pattern, 0, 31);
	reg_mask = hea_set_u64_bits(reg_mask, mask, 0, 31);

	/* configure LPORT or PPORT */
	switch (channel->type) {
	case HEA_BC_PORT:
	case HEA_MC_PORT:
	case HEA_UC_PORT:
		/* case UC_MC_HEA_BC_PORT: */

		/* is physical port */
		reg_pattern = hea_set_u64_bits(reg_pattern, 0, 48, 48);
		reg_mask = hea_set_u64_bits(reg_mask, 1, 48, 48);
		break;

	default:

		/* is logical port */
		reg_pattern = hea_set_u64_bits(reg_pattern, 1, 48, 48);
		reg_mask = hea_set_u64_bits(reg_mask, 1, 48, 48);
		break;
	}

	/* configure which channel type is using this TCAM */
	switch (channel->type) {
	case HEA_BC_PORT:
		reg_pattern = hea_set_u64_bits(reg_pattern, 1, 54, 55);
		reg_mask = hea_set_u64_bits(reg_mask, 3, 54, 55);
		break;

	case HEA_MC_PORT:
		reg_pattern = hea_set_u64_bits(reg_pattern, 2, 54, 55);
		reg_mask = hea_set_u64_bits(reg_mask, 3, 54, 55);
		break;

	case HEA_UC_PORT:
		reg_pattern = hea_set_u64_bits(reg_pattern, 3, 54, 55);
		reg_mask = hea_set_u64_bits(reg_mask, 3, 54, 55);
		break;

	default:
	    {
		int lport_index = hea_lport_index_get(channel->type);

		/* only allow one logical port at a time
		 * --> not all combinations are possible */
		reg_pattern =
			hea_set_u64_bits(reg_pattern, lport_index, 54, 55);
		reg_mask = hea_set_u64_bits(reg_mask, 3, 54, 55);
	    }
		break;
	}

	/* This is the offset from the QPN base to be used
	 * if the packet data matches the pattern */
	reg_pattern = hea_set_u64_bits(reg_pattern, qpn_offset, 59, 63);

	/* write back registers */
	out_be64(&pport->pport_regs->bpfc.pg_tcampr[tcam_index], reg_pattern);
	out_be64(&pport->pport_regs->bpfc.pg_tcamm[tcam_index], reg_mask);

	return rc;
}

static int _rhea_tcam_set(struct rhea_channel *channel,
			  unsigned tcam_base, unsigned tcam_offset,
			  unsigned qpn_offset,
			  unsigned pattern, unsigned mask)
{
	int rc = 0;
	unsigned tcam_bits;
	struct rhea_pport *pport;
	struct rhea_channel_resource_map *map_qpn = NULL;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	if (NULL == channel->qpn) {
		rhea_error("QPN not not allocated!");
		return -EINVAL;
	}

	if (NULL == channel->tcam) {
		rhea_error("TCAM not not allocated!");
		return -EINVAL;
	}

	rhea_debug("Set TCAM slot: %u and channel: %u of pport: "
		   "%u for QPN slot: %u",
		   tcam_base + tcam_offset, channel->type,
		   channel->pport_nr + 1,
		   channel->qpn_base + qpn_offset);

	map_qpn = _rhea_channel_resource_map_get(channel->qpn,
						 channel->qpn_base);
	if (NULL == map_qpn) {
		rhea_error("Was not able to find the QPN map");
		return -EINVAL;
	}

	rc = _rhea_tcam_register_set(channel, tcam_base, tcam_offset,
				     qpn_offset, pattern, mask);
	if (rc) {
		rhea_error("Was not able to set TCAM registers");
		return rc;
	}

	/* get number of bits used by qpn alloc */
	tcam_bits = map_qpn->bits;

	_rhea_tcam_channel_register_set(channel, tcam_bits);

	return rc;
}

int rhea_tcam_set(struct rhea_channel *channel,
		  unsigned tcam_base, unsigned tcam_offset,
		  unsigned qpn_offset,
		  unsigned pattern, unsigned mask)
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

	rc = _rhea_tcam_set(channel, tcam_base, tcam_offset,
			    qpn_offset, pattern, mask);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to set TCAM");
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

static int _rhea_tcam_register_get(struct rhea_channel *channel,
				   unsigned tcam_base,
				   unsigned tcam_offset,
				   unsigned *qpn_offset, unsigned *pattern,
				   unsigned *mask)
{
	int rc = 0;
	unsigned tcam_index;
	u64 reg_pattern;
	u64 reg_mask;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == qpn_offset ||
	    NULL == pattern || NULL == mask)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* get the real index in the TCAM array */
	rc = _rhea_channel_resource_index_get(channel->tcam,
					      tcam_base, tcam_offset,
					      &tcam_index);
	if (rc) {
		rhea_error("Was not able to find index in TCAM map");
		return -EINVAL;
	}

	reg_pattern = in_be64(&pport->pport_regs->bpfc.pg_tcampr[tcam_index]);
	reg_mask = in_be64(&pport->pport_regs->bpfc.pg_tcamm[tcam_index]);

	/* get QPN setting */
	*qpn_offset = hea_get_u64_bits(reg_pattern, 59, 63);

	/* get TCAM pattern */
	*pattern = hea_get_u64_bits(reg_pattern, 0, 31);

	/* get TCAM mask */
	*mask = hea_get_u64_bits(reg_mask, 0, 31);

	return rc;
}

int rhea_tcam_get(struct rhea_channel *channel,
		  unsigned tcam_base, unsigned tcam_offset,
		  unsigned *qpn_offset, unsigned *pattern, unsigned *mask)
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

	rc = _rhea_tcam_register_get(channel,
				     tcam_base, tcam_offset,
				     qpn_offset, pattern, mask);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to get TCAM registers");
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

static int _rhea_tcam_register_set_status(struct rhea_channel *channel,
					  unsigned tcam_base,
					  unsigned tcam_offset,
					  unsigned enable)
{
	int rc = 0;
	unsigned tcam_index;
	struct rhea_pport *pport;
	u64 reg_pattern;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	if (NULL == channel->qpn || NULL == channel->tcam) {
		rhea_error("TCAM or QPN are not initialised");
		return -EINVAL;
	}

	/* get the real index in the TCAM array */
	rc = _rhea_channel_resource_index_get(channel->tcam,
					      tcam_base, tcam_offset,
					      &tcam_index);
	if (rc) {
		rhea_error("Was not able to find index in TCAM map");
		return -EINVAL;
	}

	reg_pattern = in_be64(&pport->pport_regs->bpfc.pg_tcampr[tcam_index]);

	/* enable/disable TCAM slot */
	reg_pattern = hea_set_u64_bits(reg_pattern, enable ? 1 : 0, 47, 47);

	out_be64(&pport->pport_regs->bpfc.pg_tcampr[tcam_index], reg_pattern);

	return rc;
}

int rhea_tcam_register_set_status(struct rhea_channel *channel,
				  unsigned tcam_base,
				  unsigned tcam_offset,
				  unsigned enable)
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

	/* get the real index in the TCAM array */
	rc = _rhea_tcam_register_set_status(channel, tcam_base,
					    tcam_offset, enable);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to set tcam status");
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

int rhea_tcam_alloc(struct rhea_channel *channel,
		    struct hea_tcam_cfg *tcam_cfg, unsigned *slot_base)
{
	int rc = 0;
	unsigned slot_decr;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == slot_base || NULL == tcam_cfg)
		return -EINVAL;

	if (NULL == channel->qpn) {
		rhea_error("QPN has not been initialised");
		return -EINVAL;
	}

	if (NULL == channel->tcam) {
		/* make sure it can be used by somebody */
		rc = _rhea_channel_map_init(&channel->tcam);
		if (rc) {
			rhea_error("Was not able to free TCAM map");
			return rc;
		}
	}

	if (channel->qpn->alloced < tcam_cfg->slot_count) {
		rhea_error("Number of requested TCAM slots exceeds "
			   "allocated QPN slots!");
		return -EINVAL;
	}

	slot_decr = tcam_cfg->slot_count;

	if (!is_hea_lport(channel->type) &&
	    HEA_DEFAULT_CHANNEL_SHARE ==
	    channel->channel_cfg.dc.channel_usuage) {
		rhea_info("Shared channel is not allowed to allocate "
			  "TCAM slots");
		return -EINVAL;
	}

	/* always get pport from channel */
	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	if (pport->hasher) {
		rhea_error("Hasher is enabled for this port");
		return -EINVAL;
	}

	if (ARRAY_SIZE(pport->pport_regs->bpfc.pg_tcampr) <
	    tcam_cfg->slot_count) {
		rhea_error("Number of requested slots is too high!");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	if (0 == slot_decr) {
		/* allocate the whole QPN array */
		slot_decr = pport->tcam.alloced_max;
	}

	rc = _rhea_channel_resource_alloc(&pport->tcam, channel->tcam,
					  slot_decr, slot_base);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Error when allocating resource for channel");
		return rc;
	}

	rhea_info("Allocated %u TCAM slots from base: %u and channel: "
		  "%u of pport: %u",
		  channel->tcam->alloced, *slot_base, channel->type,
		  channel->pport_nr + 1);

	spin_unlock(&pport->lock);

	return rc;
}

int _rhea_tcam_free(struct rhea_channel *channel, unsigned base)
{
	int rc = 0;
	unsigned tcam_offset;
	struct rhea_pport *pport;
	struct rhea_channel_resource_map *tcam_base;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	if (NULL == channel->tcam) {
		rhea_error("TCAM was not allocated");
		return -EINVAL;
	}

	if (NULL == channel->qpn) {
		rhea_error("QPN was not allocated");
		return -EINVAL;
	}

	tcam_base = _rhea_channel_resource_map_get(channel->tcam, base);
	if (NULL == tcam_base) {
		rhea_error("Did not find this tcam base");
		return -EINVAL;
	}

	rhea_info("Free %u TCAM slots from base: %u and channel: "
		  "%u of pport: %u",
		  tcam_base->alloced, base, channel->type,
		  channel->pport_nr + 1);

	/* make sure that the tcam is reset */
	for (tcam_offset = 0; tcam_offset < tcam_base->alloced; ++tcam_offset) {
		unsigned int qpn_offset = 0;
		unsigned int pattern = 0;
		unsigned int mask = 0;

		/* get current tcam configuration */
		rc = _rhea_tcam_register_get(channel, base, tcam_offset,
					     &qpn_offset, &pattern, &mask);
		if (rc) {
			rhea_error("Was not able to obtain tcam information");
			return rc;
		}

		/* reset the registers */
		rc = _rhea_tcam_set(channel, base, tcam_offset,
				    qpn_offset, 0, 0);
		if (rc) {
			rhea_error("Was not able to reset tcam!");
			return rc;
		}

		/* disable this tcam */
		rc = _rhea_tcam_register_set_status(channel, base, tcam_offset,
						    0);
		if (rc) {
			rhea_error("Was not able to disable tcam");
			return rc;
		}
	}

	/* mark block as free */
	rc = _rhea_channel_resource_free(&pport->tcam, channel->tcam, base);
	if (rc) {
		rhea_error("Was not able to free resources");
		return rc;
	}

	return rc;
}

int _rhea_tcam_free_all(struct rhea_channel *channel)
{
	int rc = 0;
	struct rhea_pport *pport;

	/* get map with new base */
	struct rhea_channel_resource_map *map_tcam = NULL;

	if (NULL == channel)
		return -EINVAL;

	if (NULL == channel->tcam) {
		rhea_error("TCAM not allocated");
		return -EINVAL;
	}

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* get first element */
	map_tcam = _rhea_channel_resource_map_element_first(channel->tcam);

	while (NULL != map_tcam) {
		unsigned base = map_tcam->base;

		/* free all tcam resources of this instance */
		rc = _rhea_tcam_free(channel, base);
		if (rc) {
			rhea_error("Was not able to free tcam");
			return rc;
		}

		/* get next element */
		map_tcam =
			_rhea_channel_resource_map_element_next(channel->tcam,
								base);
	}

	rc = _rhea_channel_map_fini(&pport->tcam, &channel->tcam);
	if (rc)
		rhea_error("Was not able to free TCAM");

	return rc;
}

int rhea_tcam_free(struct rhea_channel *channel, unsigned base)
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
	rc = _rhea_tcam_free(channel, base);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to free resources");
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

int _rhea_tcam_init(struct rhea_pport *pport)
{
	int i;
	int rc = 0;

	if (NULL == pport)
		return -EINVAL;

	/* initialise resource */
	rc = _rhea_channel_resource_init(&pport->tcam,
					 ARRAY_SIZE(pport->pport_regs->bpfc.
						    pg_tcampr), 0, 0, 1);
	if (rc) {
		rhea_error("Was not able to initialise TCAM resource: %i", rc);
		return rc;
	}

	for (i = 0; i < HEA_MAX_PPORT_CHANNEL_COUNT; ++i) {
		/* if used reset hash bits */
		if (pport->channel)
			_rhea_tcam_channel_register_set(pport->channel[i], 0);
	}

	/* make sure that the registers are set to their default values */
	_rhea_tcam_register_reset(&pport->pport_regs->bpfc);

	return rc;
}

int _rhea_tcam_fini(struct rhea_pport *pport)
{
	int i;
	int rc = 0;

	if (NULL == pport)
		return -EINVAL;

	for (i = 0; i < HEA_MAX_PPORT_CHANNEL_COUNT; ++i) {
		/* if used reset hash bits */
		if (pport->channel)
			_rhea_tcam_channel_register_set(pport->channel[i], 0);
	}

	/* make sure that the registers are set to their default values */
	_rhea_tcam_register_reset(&pport->pport_regs->bpfc);

	return rc;
}
