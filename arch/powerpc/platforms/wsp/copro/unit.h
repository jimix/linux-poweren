#ifndef _WSP_COPRO_UNIT_H
#define _WSP_COPRO_UNIT_H

/*
 * Copyright 2008-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "pbic.h"
#include "unit_init.h"

extern long copro_unit_ioctl(struct copro_unit *unit, unsigned int cmd,
			     unsigned long arg);
extern long copro_unit_reg_ioctl(struct copro_unit *unit, unsigned int cmd,
			void __user *uptr, struct copro_reg_args *args);
extern int copro_unit_probe(struct platform_device *dev);
extern int copro_unit_init(void);
extern int copro_unit_abort(struct copro_unit *unit, u64 csb, int pid);

static inline void copro_unit_set_debug_regs(struct copro_unit *unit,
					     struct reg_range *regs)
{
#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
	unit->debug_regs = regs;
#endif
}

extern struct platform_device *copro_find_parent_unit(struct device_node *dn);


extern int regx_unit_init(void);
extern int cdmx_unit_init(void);
extern int xmlx_unit_init(void);
extern int cmpx_unit_init(void);

#ifdef CONFIG_WSP_EP
extern int wsp_ep_driver_init(void);
#else
static inline int wsp_ep_driver_init(void) { return 0; }
#endif

#endif	/* _WSP_COPRO_UNIT_H */
