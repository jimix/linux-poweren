/*
 * Copyright 2011 Hartmut Penner, IBM Corporation
 * Copyright 2011 Horst Tabel, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Virtual Console based on PCIe MBX protocol
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <asm/reg_a2.h>
#include <asm/udbg.h>
#include <asm/wsp.h>

#include "hvc_console.h"

struct mbx_service_context *mbx_ctx[MBX_MAX_SERVICE];

#define MBX_RX_INDEXED ((uint64_t *) POWEREN_PCIE_MBX_VIRT)
#define MBX_RX_CTRL ((uint64_t *) (POWEREN_PCIE_MBX_VIRT | 0x40))
#define MBX_TX_INDEXED ((uint64_t *) (POWEREN_PCIE_MBX_VIRT | 0x100))
#define MBX_TX_STAT ((uint64_t *) (POWEREN_PCIE_MBX_VIRT | 0x140))
#define MBX_TX_CTRL ((uint64_t *) (POWEREN_PCIE_MBX_VIRT | 0x148))

int mbx_install_service(uint16_t service,
			void (*service_func) (uint16_t flags, char *data,
					      uint32_t len),
			void *data,
			uint32_t max_len, struct mbx_service_context *ctx)
{
	int ret_val = 0;
	if (!max_len || (max_len % 8))
		return -EINVAL;
	ctx->service_func = service_func;
	ctx->data = data;
	ctx->max_len = max_len;
	mbx_ctx[service] = ctx;

	/* Enable inbound mbx */
	out_be64(MBX_RX_CTRL, 2);

	return ret_val;
}

#define MAX_BUF 56
#define MAX_RETRY 10000

int mbx_write_packet(uint16_t service, uint16_t flags,
		     char *packet, int32_t len)
{
	int index = 0;
	uint64_t *pdata = (void *) packet;
	union mbx_packet_header header;
	static int retry;

	if (!(in_be64(MBX_TX_CTRL) & 0x20))
		return 0;

	if (in_be64(MBX_TX_STAT) & 0x1) {
		retry++;
		if (retry < MAX_RETRY)
			return -EAGAIN;
		else
			return 0;
	} else {
		retry = 0;
	}

	len = (len > MAX_BUF) ? MAX_BUF : len;

	header.service = service;
	header.flags = flags;
	header.len = len;

	out_be64(MBX_TX_INDEXED + index++, header.val);

	while (len > 0) {
		out_be64(MBX_TX_INDEXED + index++, *pdata++);
		len -= sizeof(*pdata);
	}

	out_be64(MBX_TX_STAT, 1);
	return header.len;
}

static void mbx_poll_packet(void)
{
	uint16_t service;
	int retry;
	union mbx_packet_header header;
	int index = 1;
	int len, max_len = 0, ret_len;
	uint64_t *pdata = NULL;

	out_be64(MBX_RX_CTRL, 2);

	/* Something to receive? */
	if (!(in_be64(MBX_RX_CTRL) & 0x1))
		return;

	header.val = in_be64(MBX_RX_INDEXED);

	len = header.len;
	service = header.service;

	if ((service < MBX_MAX_SERVICE) && mbx_ctx[service]) {
		pdata = mbx_ctx[service]->data;
		max_len = mbx_ctx[service]->max_len;

		if (len > max_len) {
			pr_warning("Chroma mailbox: %016llx service %x len %u\n",
				   header.val, service, len);
			len = max_len;
		}
	} else {
		if (service >= MBX_MAX_SERVICE)
			pr_warning("Chroma mailbox: invalid service %x\n",
				   service);
		/* data needs to be consumed, even with unknown service */
	}

	ret_len = len;

	while (len > 0) {

		if (index == 8) {
			index = 0;
			retry = 0;

			out_be64(MBX_RX_CTRL, 3);
			eieio();
			udelay(2);

			while (!in_be64(MBX_RX_CTRL) & 0x1) {
				if (retry++ > 100)
					return;
				udelay(1);
			}
		}
		if (pdata && (max_len > 0)) {
			*pdata++ = in_be64(MBX_RX_INDEXED + index);
			max_len -= sizeof(*pdata);
		}
		index++;
		len -= sizeof(*pdata);
	}

	if (pdata) {
		mbx_ctx[service]->service_func(header.flags,
					       mbx_ctx[service]->data,
					       ret_len);
	}

	out_be64(MBX_RX_CTRL, 3);
	eieio();
}

#define RX_BUF_LEN 256
static char mbx_rx_buff[RX_BUF_LEN];
static int rd_idx;
static int wr_idx;

/*
 * Callback for serial console service,
 * will only be called by mbx_poll_packet for the console type packets
 */

static void mbx_console_rx(uint16_t flags, char *pdata, uint32_t len)
{
	int i;
	for (i = 0; i < len; i++)
		mbx_rx_buff[(wr_idx++) % RX_BUF_LEN] = pdata[i];
}

static int mbx_poll_get_packet(char *buf, int len)
{
	int i;
	mbx_poll_packet();

	for (i = 0; i < len; i++) {
		if (wr_idx == rd_idx)
			break;
		buf[i] = mbx_rx_buff[(rd_idx++) % RX_BUF_LEN];
	}

	return i;
}

/* hvc console using MBX PCIe Protocol */

static int hvc_mbx_put(uint32_t vtermno, const char *buf, int len)
{
	return mbx_write_packet(MBX_SERVICE_CONSOLE, MBX_DEFAULT_VFN,
				(char *) buf, len);
}

static int hvc_mbx_get(uint32_t vtermno, char *buf, int len)
{
	return mbx_poll_get_packet(buf, len);
}

static const struct hv_ops hvc_mbx_ops = {
	.get_chars = hvc_mbx_get,
	.put_chars = hvc_mbx_put,
};

static struct mbx_service_context mbx_console;
static struct hvc_struct *hvc_mbx_dev;
static char rx_buff[256];

static int __init hvc_mbx_init(void)
{
	struct hvc_struct *hp;

	mbx_install_service(MBX_SERVICE_CONSOLE,
			    mbx_console_rx,
			    rx_buff, sizeof(rx_buff), &mbx_console);

	hp = hvc_alloc(0, NO_IRQ, &hvc_mbx_ops, 56);
	if (IS_ERR(hp))
		return PTR_ERR(hp);

	hvc_mbx_dev = hp;

	return 0;
}
module_init(hvc_mbx_init);

static void __exit hvc_mbx_exit(void)
{
	if (hvc_mbx_dev)
		hvc_remove(hvc_mbx_dev);
}
module_exit(hvc_mbx_exit);

static int __init hvc_mbx_console_init(void)
{
	mbx_install_service(MBX_SERVICE_CONSOLE,
			    mbx_console_rx,
			    rx_buff, sizeof(rx_buff), &mbx_console);

	hvc_instantiate(0, 0, &hvc_mbx_ops);
	add_preferred_console("hvc", 0, NULL);

	return 0;
}
console_initcall(hvc_mbx_console_init);

#ifdef CONFIG_PPC_EARLY_DEBUG_WSP

/*
 * Early udbg console using MBX PCIe Protocol
 * This function will be called from udgb_wsp_*
 */


void mbx_putc(char c)
{
	while (mbx_write_packet(MBX_SERVICE_CONSOLE,
				MBX_DEFAULT_VFN, &c, 1) == -EAGAIN)
		;
}

int mbx_getc_poll(void)
{
	mbx_poll_packet();
	if (rd_idx == wr_idx)
		return -1;
	else
		return mbx_rx_buff[(rd_idx++) % RX_BUF_LEN];
}

int mbx_getc(void)
{
	int c;
	while ((c = mbx_getc_poll()) == -1)
		udelay(1);
	return c;
}


static int enabled;
void __init udbg_init_poweren_pcie_vc(void)
{
	if (!enabled) {

		rd_idx = 0;
		wr_idx = 0;

		mbx_install_service(MBX_SERVICE_CONSOLE,
				    mbx_console_rx,
				    rx_buff, sizeof(rx_buff), &mbx_console);

		enabled = 1;
	}
}
#endif
