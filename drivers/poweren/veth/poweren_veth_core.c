/*
 * IBM PowerEN Virtual Ethernet Driver
 * Copyright (C) 2011 IBM Corp.
 *
 * Bernard Gorman <berngorm@ie.ibm.com>
 * Mark Purcell <mark_purcell@ie.ibm.com>
 * Michael Barry <michael_barry@ie.ibm.com>
 * Antonino Castelfranco <antonino_castelfranco@ie.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Author(s) : Bernard Gorman, Mark Purcell, Michael Barry,
 * Antonino Castelfranco
 *
 * PowerEN is an accelerator card that attaches to its host machine via the
 * PCIe bus. A full operating system runs on the card; the host can offload
 * work to PowerEN, or the card may preprocess network data before delivery
 * to the host. This driver exposes a virtio-based virtual ethernet interface,
 * used to allow communication between the host and PowerEN via an underlying
 * PCIe driver. Packets are transferred from host to device over MMIO, and
 * from device to host over DMA. Input SKBs allocated by virtio_net on the
 * host are "pinned" using the kernel's iommu_map API, and then "posted" to
 * a buffer queue on the device. The buffers are subsequently popped off the
 * queue and used as the DMA target address when the device wishes to send a
 * packet to the host.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/kthread.h>
#include <linux/ip.h>
#include <linux/virtio.h>
#include <linux/virtio_net.h>

#include "poweren_veth_vnet.h"

#include "poweren_veth_vf.h"
#include "poweren_veth_sma.h"
#include "poweren_veth_irq.h"

static struct pseudo_interrupt rx_poll;

#ifndef CONFIG_POWEREN_EP_DEVICE

#include <linux/pci.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>

#include "poweren_veth_iommu.h"

#define MAX_PACKET_LEN (ETH_HLEN + VLAN_HLEN + ETH_DATA_LEN)

static const u8 poweren_mac[6] = { 0x00, 0x80, 0xfe, 0xed, 0xde, 0xad };

inline dma_addr_t poweren_veth_pin(dma_addr_t addr, u16 size)
{
	return poweren_veth_iommu_map((void *)addr, size, IOMMU_WRITE);
}

inline int poweren_veth_post_buffers(void *hndl, struct mem_addr *sg, u16 count)
{
	return poweren_veth_sma_post(hndl, sg, count, poweren_veth_pin);
}

inline int poweren_veth_writepkt(void *hndl, struct mem_addr *src)
{
	return poweren_veth_sma_writepkt(hndl, src, memcpy);
}

inline int poweren_veth_readpkt(void *hndl,
		struct mem_addr *buf, u16 idx, void *skb)
{
	return poweren_veth_sma_readpkt(hndl, buf);
}

inline int poweren_veth_open(void **hndl)
{
	return poweren_veth_sma_open(hndl);
}

inline int poweren_veth_term(void *ptr)
{
	poweren_veth_vf_free_irq(ptr);
	poweren_veth_vf_unregister();
	return 0;
}

int poweren_veth_rx_init(struct poweren_virtio_cfg *cfg)
{
	/* set up pseudo-irq for compatibility */
	cfg->priv = &rx_poll;

	poweren_veth_irq_init(&rx_poll, poweren_veth_rx_soft_irq, (u64)cfg);
	rx_poll.schedule = true;

	poweren_veth_vf_request_irq(poweren_veth_rx_irq, cfg);

	return 0;
}

#else

#include <asm/icswx.h>

#include "poweren_cop.h"

#define DMA_READ 1
#define DMA_WRITE 0

#define DMAX_CT 0x3c

static const u8 poweren_mac[6] = { 0x00, 0x80, 0xfe, 0xed, 0xde, 0xac };

struct cop_crb_dmax *crb;
struct cop_csb_common *csb;

void *poweren_dma_pkt(void *dest, const void *src, size_t len)
{
	/* Prepare the CRB */
	crb->ccw = 0;
	crb->ccw = (DMAX_CT << 16) | DMA_WRITE;
	crb->csbp = virt_to_phys(csb);

	crb->source.addr = virt_to_phys((void *)src);
	crb->source.byte_count = len;

	crb->target.addr = (dma_addr_t)dest;
	crb->target.byte_count = len;

	crb->flags = 0;

	/* DMA issued using physical function 1 */
	CXB_SET_BITS(crb->flags, CRB_FLAG_PCIE_FN, 1);

	 /* bypass PBIC, all addrs physical or IOMMU */
	CXB_SET_BITS(crb->csbp, CSBP_ADDR_TRANS, 1);

	csb->control = 0;

	if (icswx(crb->ccw, crb) == -EAGAIN) {
		while (icswx_raw(crb->ccw, crb) == -EAGAIN)
			;
	}

	poweren_ep_csb_wait_valid(csb);

	if (CXB_CHECK_BITS(csb->control, CSB_CTRL_CC, 0xFF))
		poweren_veth_info("DMA failure!");

	return NULL;
}

inline int poweren_veth_writepkt(void *hndl, struct mem_addr *src)
{
	int rc;

	rc = poweren_veth_sma_writepkt(hndl, src, poweren_dma_pkt);

	if (rc > 0)
		poweren_veth_vf_poke(POWEREN_VETH_VF_TRIGGER);

	return rc;
}

inline int poweren_veth_readpkt(void *hndl,
		struct mem_addr *buf, u16 idx, void *skb)
{
	return poweren_veth_sma_readpkt(hndl, buf);
}

inline int poweren_veth_open(void **hndl)
{
	if (!crb) {
		crb = kzalloc(ALIGN(sizeof(*crb), 128), GFP_KERNEL);
		csb = kzalloc(ALIGN(sizeof(*csb), 16), GFP_KERNEL);
	}

	return poweren_veth_sma_open(hndl);
}

inline int poweren_veth_term(void *ptr)
{
	if (crb) {
		kfree((void *)crb);
		kfree((void *)csb);
	}

	poweren_veth_vf_unregister();

	return 0;
}

int poweren_veth_rx_init(struct poweren_virtio_cfg *cfg)
{
	/* cfg attached to input queue */
	cfg->priv = &rx_poll;

	poweren_veth_irq_init(&rx_poll, poweren_veth_rx_soft_irq, (u64)cfg);
	poweren_veth_irq_sched(&rx_poll);

	poweren_veth_info("Recv threshold: %d", IRQ_MISSES);

	return 0;
}

#endif

inline void poweren_veth_set_mac(struct virtio_net_config *config)
{
	memcpy(&config->mac, poweren_mac, ETH_ALEN);
}

char *map_sma(u8 type, u64* size)
{
	void *sma = NULL;

	sma = poweren_veth_vf_map_sma(type, size);

	poweren_veth_info("sma addr[%hhu]: %p", type, sma);

	return (char *)sma;
}

static inline int poweren_veth_sma_vf_init(void *config)
{
	int rc;

	poweren_veth_set_mac((struct virtio_net_config *)config);

	rc = poweren_veth_vf_init();

	if (rc < 0) {
		poweren_veth_info("vf_init failed!");
		return rc;
	}

	rc = poweren_veth_sma_init(map_sma);

	if (rc < 0)
		poweren_veth_vf_unregister();

	return rc;
}


struct poweren_virtio_ops poweren_veth_ops = {
	.init = poweren_veth_sma_vf_init,
	.recv_init = poweren_veth_rx_init,
	.term = poweren_veth_term,
	.open = poweren_veth_open,
	.close = poweren_veth_sma_close,
	.xmit_pkt = poweren_veth_writepkt,
	.recv_pkt = poweren_veth_readpkt,
#ifndef CONFIG_POWEREN_EP_DEVICE
	.can_xmit_pkt = poweren_veth_sma_space,
	.can_recv_pkt = poweren_veth_sma_newpkts,
	.recv_post = poweren_veth_post_buffers,
#else
	.can_xmit_pkt = poweren_veth_sma_buffers_space,
	.can_recv_pkt = poweren_veth_sma_cmd_newpkts,
#endif
};


static struct device *poweren_veth_root;

static int poweren_veth_async_init(void)
{
	int err = 0;

	poweren_veth_root =
		root_device_register("poweren_veth");

	if (IS_ERR(poweren_veth_root)) {
		printk(KERN_ALERT "Could not register root device!\n");
		return PTR_ERR(poweren_veth_root);
	}

	err = poweren_veth_vnet_register(poweren_veth_root, &poweren_veth_ops);

	if (err) {
		printk(KERN_ALERT "Could not register driver!\n");
		root_device_unregister(poweren_veth_root);
		return err;
	}

	poweren_veth_info("initialized.");

	return err;
}

#ifdef CONFIG_POWEREN_EP_DEVICE
struct task_struct *poweren_veth_async_init_thread;

int poweren_veth_async_init_threadfunc(void *data)
{
	while (!poweren_ep_host_ready())
		msleep(20);

	poweren_veth_async_init();
	do_exit(0);

	return 0;
}
#endif

static int poweren_veth_init(void)
{
#ifndef CONFIG_POWEREN_EP_DEVICE
	return poweren_veth_async_init();
#else
	poweren_veth_async_init_thread =
		kthread_run(poweren_veth_async_init_threadfunc,
			NULL, "poweren_veth_async_init_thread");

	return 0;
#endif
}

static void poweren_veth_exit(void)
{
	poweren_veth_info("exiting...");

	poweren_veth_irq_stop(&rx_poll);

	poweren_veth_vnet_unregister();

	root_device_unregister(poweren_veth_root);
}

module_init(poweren_veth_init);
module_exit(poweren_veth_exit);


MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Bernard Gorman");
MODULE_AUTHOR("Mark Purcell");
MODULE_AUTHOR("Michael Barry");
MODULE_AUTHOR("Antonino Castelfranco");
MODULE_DESCRIPTION("PowerEN Virtual Ethernet Driver");
