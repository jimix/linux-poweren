/*
 * Copyright IBM Corporation. 2011
 * Author:	Massimiliano Meneghin <massimim@ie.ibm.com>
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
#ifndef _DHEA_H_
#define _DHEA_H_

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/vmalloc.h>

#define MHEA_NAME "mhea"

/*
 * Information associated to a process that open the device.
 * A pointer to such data type is safed in the private field
 * of the process fd
 */
struct mhea_p_info {
	int adapter_id;
};

#define mhea_info(fmt, args...) pr_info("mhea: " fmt "\n", ## args)

#define mhea_warning(fmt, args...) pr_warning("mhea: " fmt "\n", ## args)

#define mhea_error(fmt, args...) \
	pr_err("mhea: Error in %s(): " fmt "\n", __func__, ## args)

#define mhea_debug(fmt, args...) \
	pr_info("mhea: in %s(): " fmt "\n", __func__, ## args)

#endif /* _DHEA_H_ */
