/*
 * IBM Power Edge of Network (PowerEN)
 *
 * Copyright 2010-2011 Massimiliano Meneghin, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef _DRIVERS_PIXAD_PROC_UTILITY_H_
#define _DRIVERS_PIXAD_PROC_UTILITY_H_

struct pixad_copro;
struct file;

extern void init_pixad_proc_set(unsigned int copro_num,
						struct pixad_copro *copro_info);

extern int find_free_VF(unsigned int copro_num, struct pixad_copro *copro_info,
	int *target_unit, int *target_vf);

extern struct pixad_user_proc *find_proc(unsigned int copro_num,
		struct pixad_copro *copro_info, struct file *descriptor);

#endif /* _DRIVERS_PIXAD_PROC_UTILITY_H_ */
