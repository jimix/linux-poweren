/*
 * Copyright 2008-2011, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/time.h>

#include <asm/machdep.h>
#include <asm/system.h>
#include <asm/udbg.h>

#include "ics.h"
#include "wsp.h"

static char *cmd_buffer[1024];
static struct mbx_service_context mbx_service_ctx;
static void chroma_pcie_cmd(uint16_t flags, char *data, uint32_t len)
{
	int rc;
	union poweren_cmd_packet_header *cmd_pkt = (void *) data;
	switch (cmd_pkt->cmd) {
	case POWEREN_PING:
		cmd_pkt->args[0] = 3;
		rc = mbx_write_packet(MBX_SERVICE_CMD, 0, data, len);
		break;
	case POWEREN_EVAL:
		if (memcmp("reboot",
			   (char *) cmd_pkt->args, cmd_pkt->data_size) == 0) {
/* FIXME Fill in Reboot code */
			cmd_pkt->args[0] = 0;
			mbx_write_packet(MBX_SERVICE_CMD, 0, data, len);
		} else if (memcmp("halt",
				  (char *) cmd_pkt->args,
				  cmd_pkt->data_size) == 0) {
/* FIXME Fill in Halt code */
			cmd_pkt->args[0] = 0;
			mbx_write_packet(MBX_SERVICE_CMD, 0, data, len);
		} else {
			cmd_pkt->args[0] = -1;
			mbx_write_packet(MBX_SERVICE_CMD, 0, data, len);
		}
		break;
	default:
		cmd_pkt->args[0] = -1;
		mbx_write_packet(MBX_SERVICE_CMD, 0, data, len);
	}
}

void __init chroma_setup_arch(void)
{
	wsp_setup_arch();
	wsp_setup_h8();
	mbx_install_service(MBX_SERVICE_CMD, chroma_pcie_cmd,
			    &cmd_buffer[0], sizeof(cmd_buffer),
			    &mbx_service_ctx);
}

static int __init chroma_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "ibm,wsp-chroma"))
		return 0;

	return 1;
}

define_machine(chroma_md) {
	.name			= "Chroma PCIe",
	.probe			= chroma_probe,
	.setup_arch		= chroma_setup_arch,
	.restart		= wsp_h8_restart,
	.power_off		= wsp_h8_power_off,
	.halt			= wsp_halt,
	.calibrate_decr		= generic_calibrate_decr,
	.init_IRQ		= wsp_setup_irq,
	.progress		= udbg_progress,
	.power_save		= book3e_idle,
};

machine_arch_initcall(chroma_md, wsp_probe_devices);
