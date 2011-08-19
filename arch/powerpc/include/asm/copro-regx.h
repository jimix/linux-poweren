#ifndef _ASM_POWERPC_COPRO_REGX_H
#define _ASM_POWERPC_COPRO_REGX_H

/*
 * Copyright 2009 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/copro-driver.h>


#define _REGX_IOCTL_NR(_nr)	(COPRO_UNIT_EXTN_NR_START + (_nr))

#define REGX_IOCTL_REQUEST_UM_PRIV	_IO('c', _REGX_IOCTL_NR(0))
#define REGX_IOCTL_DROP_UM_PRIV		_IO('c', _REGX_IOCTL_NR(1))


/* Register numbers for SET_REG/GET_REG ioctls */
#define REGX_REG_RXBPCR		0
#define REGX_REG_RXBP0SR	1
#define REGX_REG_RXBP1SR	2
#define REGX_REG_RXBP0HR	3
#define REGX_REG_RXBP1HR	4
#define REGX_REG_RXBP0MR0	5
#define REGX_REG_RXBP0MR1	6
#define REGX_REG_RXBP1MR0	7
#define REGX_REG_RXBP1MR1	8
#define REGX_REG_RXRBAR		9

/* Mask of valid bits for above registers */
#define RXBPCR_MASK	0xFF0307
#define RXBPnSR_MASK	0x1FFFFFFFFULL
#define RXBPnHR_MASK	0x1F1F1F1F1F1F1F00ULL
#define RXBPnMRn_MASK	0xFFFFFFFFULL

#endif /* _ASM_POWERPC_COPRO_REGX_H */
