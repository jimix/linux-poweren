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

#ifndef _POWEREN_HEA_QUEUE_H_
#define _POWEREN_HEA_QUEUE_H_

#include <linux/atomic.h>

struct hea_q {
	void *q_begin;
	void *q_end;
	void *qe_current;
	unsigned qe_size;
	unsigned qe_count;
	atomic_t qe_free;
	unsigned q_toggle_bit;
	unsigned q_offset;
};


static inline void heaq_inc_count(struct hea_q *q)
{
	atomic_inc(&(q->qe_free));
}


static inline void heaq_dec_count(struct hea_q *q)
{
	atomic_dec(&(q->qe_free));
}

static inline int heaq_get_count(struct hea_q *q)
{
	return atomic_read(&q->qe_free);
}


static inline void heaq_set_count(struct hea_q *q, int count)
{
	atomic_set(&q->qe_free, count);
}


static inline void heaq_set_next_qe(struct hea_q *q)
{
	/* get next QE */
	q->qe_current += q->qe_size;
	q->q_offset++;

	/* check if we have wrapped */
	if (q->q_end <= q->qe_current) {
		q->qe_current    = q->q_begin;
		q->q_toggle_bit ^= 1;
		q->q_offset      = 0;
	}
}

static inline unsigned short heaq_get_offset(struct hea_q *q)
{
	return q->q_offset;
}

static inline unsigned short heaq_get_max_offset(struct hea_q *q)
{
	return q->qe_count;
}

/*
 * Call this function after having received the Q information from rHEA
 */
static inline void heaq_init(struct hea_q *q)
{
	if (NULL == q || NULL == q->q_begin ||
		0 == q->qe_size || 0 == q->qe_count) {
		return;
	}

	q->q_end         = q->q_begin + (q->qe_count * q->qe_size);
	q->qe_current    = q->q_begin;
	q->q_toggle_bit  = 1;
	q->q_offset      = 0;

	/* this one should be set later if required */
	heaq_set_count(q, q->qe_count - 1);
}

#endif /* _POWEREN_HEA_QUEUE_H_ */
