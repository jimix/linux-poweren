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

#ifndef RHEA_MC_BC_MANAGER_H_
#define RHEA_MC_BC_MANAGER_H_

#include <asm/poweren_hea_channel.h>

struct rhea_bc_mc_uc_manager {

	int (*manager_init)(unsigned pport_nnr);
	int (*manager_fini)(unsigned pport_nnr);

	int (*channel_create)(unsigned pport_nr,
			      enum hea_channel_type channel_type);
	int (*channel_destroy)(unsigned pport_nr,
			       enum hea_channel_type channel_type);

	int (*channel_registered_count)(unsigned pport_nr,
					enum hea_channel_type channel_type);

	int (*manager_lport_register)(
		unsigned pport_nr,
		enum hea_channel_type register_type,
		struct hea_channel_cfg *lport_cfg);
	int (*manager_lport_unregister)(
		unsigned pport_nr,
		enum hea_channel_type register_type,
		struct hea_channel_cfg *lport_cfg);

	int (*manager_lport_enable)(unsigned pport_nr,
				    enum hea_channel_type register_type,
				    unsigned lport_nr);
	int (*manager_lport_disable)(unsigned pport_nr,
				    enum hea_channel_type register_type,
				    unsigned lport_nr);
};

extern int rhea_mc_bc_manager_init(unsigned pport_nr);
extern int rhea_mc_bc_manager_fini(unsigned pport_nr);

extern int rhea_mc_bc_channel_create(unsigned pport_nr,
				     enum hea_channel_type channel_type);

extern int rhea_mc_bc_channel_destroy(unsigned pport_nr,
				      enum hea_channel_type
				      channel_type);

extern int rhea_mc_bc_channel_registered_count(unsigned pport_nr,
					       enum hea_channel_type
					       channel_type);

extern int rhea_mc_bc_manager_lport_register(unsigned pport_nr,
					     enum hea_channel_type
					     register_type,
					     struct hea_channel_cfg
					     *lport_cfg);

extern int rhea_mc_bc_manager_lport_unregister(unsigned pport_nr,
					enum hea_channel_type register_type,
					struct hea_channel_cfg *lport_cfg);

extern int rhea_mc_bc_manager_lport_enable(unsigned pport_nr,
					enum hea_channel_type register_type,
					unsigned lport_nr);

extern int rhea_mc_bc_manager_lport_disable(unsigned pport_nr,
					enum hea_channel_type register_type,
					unsigned lport_nr);

#endif /* RHEA_MC_BC_MANAGER_H_ */
