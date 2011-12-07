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

#include "rhea-channel.h"
#include "rhea-base.h"

static void _rhea_hasher_channel_register_set(struct rhea_channel *channel,
					      unsigned hash_bits)
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

	if (NULL == channel->qpn) {
		rhea_error("Only the main channel ID can set the hasher");
		return;
	}

	bpfc = &pport->pport_regs->bpfc;

	/* reset number of used hash bits for all channels */
	switch (channel->type) {
	case HEA_UC_PORT:

		reg = in_be64(&bpfc->p_rcu);
		reg = hea_set_u64_bits(reg, hash_bits, 9, 11);
		out_be64(&bpfc->p_rcu, reg);
		break;

	case HEA_MC_PORT:

		reg = in_be64(&bpfc->p_rcm);
		reg = hea_set_u64_bits(reg, hash_bits, 9, 11);
		out_be64(&bpfc->p_rcm, reg);
		break;

	case HEA_BC_PORT:

		reg = in_be64(&bpfc->p_rcb);
		reg = hea_set_u64_bits(reg, hash_bits, 9, 11);
		out_be64(&bpfc->p_rcb, reg);
		break;

	default:

		if (0 <= channel->type - HEA_LPORT_0) {
			int lport_index = hea_lport_index_get(channel->type);

			reg = in_be64(&bpfc->pl_rc[lport_index]);
			reg = hea_set_u64_bits(reg, hash_bits, 9, 11);
			out_be64(&bpfc->pl_rc[lport_index], reg);
		}
		break;
	}
}

static void _rhea_hasher_register_reset(struct rhea_pport_bpfc *bpfc)
{
	if (NULL == bpfc)
		return;

	/* reset hasher registers */
	out_be64(&bpfc->pg_hashm[0], ~(0x0ULL));
	out_be64(&bpfc->pg_hashm[1], ~(0x0ULL));

	out_be64(&bpfc->pg_hashsc, 0x0ULL);
}

struct rhea_hasher *rhea_hasher_alloc(struct rhea_channel *channel)
{
	struct rhea_pport *pport;
	struct rhea_hasher *hasher;

	if (NULL == channel)
		return NULL;

	if (!is_hea_lport(channel->type) &&
	    HEA_DEFAULT_CHANNEL_SHARE ==
	    channel->channel_cfg.dc.channel_usuage) {
		rhea_warning("Shared channel is not allowed to hasher");
		return NULL;
	}

	/* always get pport from channel */
	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return NULL;
	}

	spin_lock(&pport->lock);

	if (pport->hasher) {
		spin_unlock(&pport->lock);
		rhea_error("Hasher is already allocated");
		return NULL;
	}

	pport->hasher =
		rhea_align_alloc(sizeof(*pport->hasher), 8, GFP_KERNEL);
	if (NULL == pport->hasher) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to allocate hasher");
		return NULL;
	}

	/* there can only be one --> identifies channel which is using it! */
	pport->hasher->id = channel->id;

	/* pass id back to caller */
	hasher = pport->hasher;

	/* mark that this channel is using the hasher */
	channel->hasher_used = 1;

	rhea_info("Allocated HASHER for channel: %u of pport: %u",
		  channel->type, channel->pport_nr + 1);

	spin_unlock(&pport->lock);

	return hasher;
}

int _rhea_hasher_free(struct rhea_channel *channel)
{
	int rc = 0;

	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	/* always get pport from channel */
	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	if (channel->hasher_used && pport->hasher) {

		rhea_align_free(pport->hasher, sizeof(*pport->hasher));

		/* set registers to their default values */
		_rhea_hasher_register_reset(&pport->pport_regs->bpfc);

		_rhea_hasher_channel_register_set(channel, 0);

		channel->hasher_used = 0;
		pport->hasher = NULL;
	}

	return rc;
}

int rhea_hasher_free(struct rhea_channel *channel)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	/* always get pport from channel */
	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	rc = _rhea_hasher_free(channel);
	if (rc) {
		rhea_error("Was not able to free hasher");
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

int rhea_hasher_set(struct rhea_channel *channel, u64 sc,
		    u64 mask0, u64 mask1)
{
	int rc = 0;
	unsigned hash_bits = 0;
	struct rhea_pport *pport = NULL;
	struct rhea_channel_resource_map *map_qpn = NULL;

	if (NULL == channel)
		return -EINVAL;

	/* always get pport from channel */
	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	if (NULL == channel->qpn) {
		rhea_error("Only the main channel ID can set the hasher");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	/* set registers */
	out_be64(&pport->pport_regs->bpfc.pg_hashm[0], mask0);
	out_be64(&pport->pport_regs->bpfc.pg_hashm[1], mask1);

	out_be64(&pport->pport_regs->bpfc.pg_hashsc, sc);

	/* save the values */
	pport->hasher->mask0 = mask0;
	pport->hasher->mask1 = mask1;
	pport->hasher->sc = sc;

	map_qpn =
		_rhea_channel_resource_map_get(channel->qpn,
					       channel->qpn_base);
	if (NULL == map_qpn) {
		rhea_error("Was not able to find the QPN map");
		return -EINVAL;
	}

	/* get number of bits used by qpn alloc */
	hash_bits = map_qpn->bits;

	rhea_debug("Hash bits: %u", hash_bits);

	/* sets the bits for the hasher */
	_rhea_hasher_channel_register_set(channel, hash_bits);

	spin_unlock(&pport->lock);

	return rc;
}

int rhea_hasher_get(struct rhea_channel *channel,
		    u64 *sc, u64 *mask0, u64 *mask1)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	if (NULL == sc || NULL == mask0 || NULL == mask1)
		return -EINVAL;

	/* always get pport from channel */
	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	/* get register values */
	*mask0 = in_be64(&pport->pport_regs->bpfc.pg_hashm[0]);
	*mask1 = in_be64(&pport->pport_regs->bpfc.pg_hashm[1]);
	*sc = in_be64(&pport->pport_regs->bpfc.pg_hashsc);

	spin_unlock(&pport->lock);

	return rc;
}

int _rhea_hasher_init(struct rhea_pport *pport)
{
	int i;
	int rc = 0;

	if (NULL == pport)
		return -EINVAL;

	/* make sure that the registers */
	/* are set to their default values */
	_rhea_hasher_register_reset(&pport->pport_regs->bpfc);

	for (i = 0; i < HEA_MAX_PPORT_CHANNEL_COUNT; ++i) {
		/* if used reset hash bits */
		if (pport->channel)
			_rhea_hasher_channel_register_set(pport->channel[i],
							  0);
	}

	return rc;
}

int _rhea_hasher_fini(struct rhea_pport *pport)
{
	int i;
	int rc = 0;

	if (NULL == pport)
		return -EINVAL;

	if (pport->hasher) {
		rc = _rhea_hasher_free(pport->channel[pport->hasher->id]);
		if (rc)
			rhea_error("Was not able to free hasher!");
	}

	/* make sure that the registers are set to their default values */
	_rhea_hasher_register_reset(&pport->pport_regs->bpfc);

	for (i = 0; i < HEA_MAX_PPORT_CHANNEL_COUNT; ++i) {
		/* if used reset hash bits */
		if (pport->channel)
			_rhea_hasher_channel_register_set(pport->channel[i],
							  0);
	}

	return rc;
}
