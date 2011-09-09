/*
 * IBM Power Edge of Network (PowerEN)
 *
 * Copyright 2010-2011 Matt Hsiao, IBM Corporation
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

#ifndef _HW172320_H_
#define _HW172320_H_

#include "pixad.h"

int retire_all_qpools(struct pixad_user_proc *target,
					struct pixad_copro *copro_info);
int align_qpool_tables_packet_offset(struct pixad_user_proc *target,
					struct pixad_copro *copro_info);

#endif
