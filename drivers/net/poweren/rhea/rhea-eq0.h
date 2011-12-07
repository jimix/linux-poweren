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

#ifndef _NET_POWEREN_EQ0_H_
#define _NET_POWEREN_EQ0_H_

#include <asm/poweren_hea_channel.h>

#include <rhea-interface.h>
#include <hea-queue.h>
#include <rhea-linux.h>

#include <linux/sched.h>
#include <linux/workqueue.h>


struct rhea_eq0 {
	spinlock_t lock;
	unsigned char pad[64];

	unsigned char stop;
	unsigned char running;

	struct delayed_work irq_work;
	struct work_struct timer_work;
	struct workqueue_struct *irq_workqueue;
	struct hea_adapter *aps;
	struct rhea_eq *eq;
	struct hea_q q;
	struct hea_eq0_pport_state_change
	pport_state_change[HEA_MAX_PPORT_COUNT][HEA_MAX_PPORT_CHANNEL_COUNT];

	struct timer_list timer;
	int link_state[HEA_MAX_PPORT_COUNT];
};


extern int hea_eq0_alloc(struct rhea_eq0 *eq0, struct hea_adapter *ap);
extern int hea_eq0_free(struct rhea_eq0 *eq0);

extern int eq0_pport_event_callback_register(struct rhea_eq0 *eq0,
					     unsigned int pport_nr,
					     enum hea_channel_type type,
				      struct hea_eq0_pport_state_change *event);

extern int eq0_pport_event_callback_unregister(struct rhea_eq0 *eq0,
						unsigned int pport_nr,
						enum hea_channel_type type);

#endif /* _NET_POWEREN_EQ0_H_ */

