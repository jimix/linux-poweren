#include <linux/device.h>
#include <linux/virtio.h>
#include <linux/virtio_net.h>
#include <linux/virtio_config.h>
#include <linux/if_ether.h>
#include <linux/virtio_ring.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>

#include "poweren_virtio.h"
#include "poweren_vring.h"

#include "poweren_veth_vnet.h"

/*
PRISM/x86 insmod
	input.add_buf 0-POWEREN_VETH_NUM_BUFFERS times
	input.kick 1 time
PRISM ifconfig up
	input.disable_cb
	input.get_buf
	input.enable_cb

x86 ifconfig up
	input.disable_cb
	input.get_buf
	input.enable_cb
	output.get_buf
	output.add_buf
	output.enable_cb
*/

/**** VirtQueue *****/
#define QUEUE_TYPE_INPUT  0
#define QUEUE_TYPE_OUTPUT 1
#define INPUT_QUEUE(q) ((q->type) == QUEUE_TYPE_INPUT)
#define OUTPUT_QUEUE(q) ((q->type) == QUEUE_TYPE_OUTPUT)

#define TX_SUSPEND_THRESHOLD (MAX_SKB_FRAGS+2)

struct virt_net_virtqueue {
	struct virtqueue *vq;
	struct vring vring;
	struct poweren_virtio_cfg cfg;
	struct mem_addr *pkts;
	u16 last_avail_idx;
	u16 last_post_idx;
	u8 type;
};

#define vq_to_virt_net_vq(_vq) \
	(&virt_net_dev->vqs[(_vq) == virt_net_dev->vqs[1].vq])

struct virt_net_device {
	struct virtio_device vdev;
	struct virtio_net_config config;
	struct virt_net_virtqueue vqs[2];
	struct tasklet_struct wakeup; /* re-waking suspended queue */

	struct poweren_virtio_ops *ops;
	void *hndl;
	u8 status;
};

static struct virt_net_device *virt_net_dev;

#define to_virt_net_dev(vd) container_of(vd, struct virt_net_device, vdev)
#define to_virt_net_vq(cfg) container_of(cfg, struct virt_net_virtqueue, cfg)
#define vdev_to_net_dev(vd) (*((void **)((vd)->priv + (sizeof(void *) * 4))))

int poweren_veth_vnet_recv(struct poweren_virtio_cfg *cfg)
{
	struct virt_net_virtqueue *q = to_virt_net_vq(cfg);
	struct sk_buff *skb;
	struct mem_addr buf;
	u16 head, i;

	int pkts = 0;

	while (q->cfg.ops->can_recv_pkt(q->cfg.hndl)) {
		if (!vring_has_avail(&q->vring, q->last_avail_idx))
			break;

		/* 1. desc[head] = skb->cb = virtio_hdr, [i] = skb->data */
		head = vring_get_avail_head(&q->vring, q->last_avail_idx);
		i = q->vring.desc[head].next;

		/* 2. get the addresses handled by the descriptor */
		buf.addr = (dma_addr_t)phys_to_virt(q->vring.desc[i].addr);
		skb = vring_desc_to_skb(&q->vring.desc[head]);

		/* 3. copy the packet to the address */
		if (!q->cfg.ops->recv_pkt(q->cfg.hndl, &buf, i, skb))
			break;

		/* 4. mark the packet as occupying two chained descriptors */
		vring_continue_desc_chain(&q->vring, head);
		vring_end_desc_chain(&q->vring, i);

		/* 5. mark the descriptor as used & set length */
		vring_set_used(&q->vring, head,
			buf.len+sizeof(struct virtio_net_hdr));

		q->last_avail_idx++;
		pkts++;
	}

	/* New packets - let the network stack know about them */
	if (pkts)
		vring_interrupt(q->type, q->vq);

	return pkts;
}

static void poweren_veth_vnet_notify(struct virtqueue *vq)
{
	struct virt_net_virtqueue *q = vq_to_virt_net_vq(vq);
	struct vring *vring = &q->vring;
	u16 i, head, num = 0;

	if (OUTPUT_QUEUE(q)) {
		while (vring_has_avail(vring, q->last_avail_idx)) {
			i = vring_get_avail_head(&q->vring, q->last_avail_idx);
			head = i;

			num = vring_to_addrs(&q->vring, &i, q->pkts, num);

			if (!q->cfg.ops->can_xmit_pkt(q->cfg.hndl)) {
				poweren_veth_info("TX queue not ready");

				if (vring_avail_free(vring, q->last_avail_idx)
						< TX_SUSPEND_THRESHOLD)
					tasklet_schedule(&virt_net_dev->wakeup);

				break;
			}

			if (!q->cfg.ops->xmit_pkt(q->cfg.hndl,
							&q->pkts[num-1])) {
				poweren_veth_info("Packet transmission failed");
				break;
			}

			q->last_avail_idx++;
			vring_set_used(&q->vring, head, 0);
		}
	} else if (q->cfg.ops->recv_post != NULL) {
		while (vring_has_avail(vring, q->last_post_idx)) {
			i = vring_get_avail_head(&q->vring, q->last_post_idx);
			num = vring_to_addrs(&q->vring, &i, q->pkts, num);

			q->last_post_idx++;
		}

		q->cfg.ops->recv_post(q->cfg.hndl, q->pkts, num);
	}
}

void poweren_veth_vnet_interrupt(struct poweren_virtio_cfg *cfg)
{
	struct virt_net_virtqueue *q = to_virt_net_vq(cfg);
	vring_interrupt(q->type, q->vq);
}

static void poweren_veth_vnet_wake(unsigned long ptr)
{
	struct virt_net_virtqueue *q = (struct virt_net_virtqueue *)ptr;
	struct net_device *ndev = vdev_to_net_dev(&virt_net_dev->vdev);

	/*
		If the transmission queue was suspended we need to drain the
		vring of existing buffers and then re-activate the queue
	*/
	if (netif_queue_stopped(ndev)) {
		poweren_veth_vnet_notify(q->vq);
		vring_interrupt(q->type, q->vq);
	} else {
		tasklet_schedule(&virt_net_dev->wakeup);
	}
}

/**** VirtQueue *****/


/**** Config *****/
static u32 poweren_veth_vnet_features(struct virtio_device *vdev)
{
	long unsigned int features = 0;

	set_bit(VIRTIO_NET_F_MAC, &features);

	return features;
}

static void poweren_veth_vnet_finalize(struct virtio_device *vdev)
{
}

static void poweren_veth_vnet_get(struct virtio_device *vdev, u32 offset,
				void *buf, u32 len)
{
	struct virt_net_device *dev = to_virt_net_dev(vdev);

	BUG_ON(offset + len > sizeof(dev->config));
	memcpy(buf, (char *)(&dev->config) + offset, len);
}

static void poweren_veth_vnet_set(struct virtio_device *vdev, u32 offset,
				 const void *buf, u32 len)
{
	struct virt_net_device *dev = to_virt_net_dev(vdev);

	BUG_ON(offset + len > sizeof(dev->config));
	memcpy((char *)(&dev->config + offset), buf, len);
}

static u8 poweren_veth_vnet_get_status(struct virtio_device *vdev)
{
	return to_virt_net_dev(vdev)->status;
}

static void poweren_veth_vnet_set_status(struct virtio_device *vdev, u8 status)
{
	to_virt_net_dev(vdev)->status = status;
}

static void poweren_veth_vnet_reset(struct virtio_device *vdev)
{
	/* TODO: stop the virtqueues? */
	poweren_veth_vnet_set_status(vdev, 0);
}

static void poweren_veth_vnet_del_vqs(struct virtio_device *vdev)
{
	struct virt_net_virtqueue *q;
	struct virtqueue *cur_vq, *next_vq;

	poweren_veth_info("del_vqs...");

	list_for_each_entry_safe(cur_vq, next_vq, &vdev->vqs, list) {
		q = vq_to_virt_net_vq(cur_vq);
		vring_del_virtqueue(cur_vq);
		kfree((void *)q->vring.desc);
		kfree(q->pkts);
	}

	poweren_veth_info("del_vqs.");
}

static int poweren_veth_vnet_find_vqs(struct virtio_device *vdev, unsigned nvqs,
				struct virtqueue *vqs[],
				vq_callback_t *callbacks[],
				const char *names[])
{
	struct virt_net_device *dev = to_virt_net_dev(vdev);
	struct virt_net_virtqueue *q;
	void *pages;
	int i, size;

	poweren_veth_info("find_vqs...");

	for (i = 0; i < nvqs; i++) {
		q = &dev->vqs[i];

		size = vring_size(POWEREN_VETH_NUM_BUFFERS*2,
				POWEREN_VRING_ALIGN);

		poweren_veth_info("vring size: %d", size);
		pages = kzalloc(size, GFP_KERNEL);

		vqs[i] = vring_new_virtqueue(POWEREN_VETH_NUM_BUFFERS*2,
					POWEREN_VRING_ALIGN, vdev, pages,
					poweren_veth_vnet_notify, callbacks[i],
					names[i]);

		q->vq = vqs[i];

		if (IS_ERR(vqs[i]))
			goto error;

		vring_init(&q->vring, POWEREN_VETH_NUM_BUFFERS*2, pages,
				POWEREN_VRING_ALIGN);

		q->pkts = kzalloc(POWEREN_VETH_NUM_BUFFERS*sizeof(*q->pkts),
					GFP_KERNEL);

		q->cfg.ops = dev->ops;
		q->cfg.hndl = dev->hndl;

		if (strcmp("input", names[i]) == 0) {
			q->type = QUEUE_TYPE_INPUT;
			q->cfg.ops->recv_init(&q->cfg);
		} else if (strcmp("output", names[i]) == 0) {
			q->type = QUEUE_TYPE_OUTPUT;

			tasklet_init(&dev->wakeup,
					poweren_veth_vnet_wake, (u64)q);
		}
	}

	poweren_veth_info("find_vqs.");
	return 0;

error:
	poweren_veth_vnet_del_vqs(vdev);
	return PTR_ERR(vqs[i]);
}

static struct virtio_config_ops virt_net_config_ops = {
	.get = poweren_veth_vnet_get,
	.set = poweren_veth_vnet_set,
	.get_features = poweren_veth_vnet_features,
	.finalize_features = poweren_veth_vnet_finalize,
	.get_status = poweren_veth_vnet_get_status,
	.set_status = poweren_veth_vnet_set_status,
	.reset = poweren_veth_vnet_reset,
	.find_vqs = poweren_veth_vnet_find_vqs,
	.del_vqs = poweren_veth_vnet_del_vqs
};

/**** Config *****/

void poweren_veth_vnet_release(struct device *dev)
{
	struct virt_net_virtqueue *q =
			&virt_net_dev->vqs[QUEUE_TYPE_INPUT];

	tasklet_kill(&virt_net_dev->wakeup);

	virt_net_dev->ops->close(virt_net_dev->hndl);
	virt_net_dev->ops->term(&q->cfg);

	kfree(virt_net_dev);
	virt_net_dev = NULL;
}

void poweren_veth_vnet_unregister(void)
{
	poweren_veth_info("Closing device");
	unregister_virtio_device(&virt_net_dev->vdev);
}

int poweren_veth_vnet_register(struct device *parent,
		struct poweren_virtio_ops *ops)
{
	int err = 0;

	virt_net_dev = kzalloc(sizeof(*virt_net_dev), GFP_KERNEL);

	if (!virt_net_dev) {
		poweren_veth_info("cannot allocate virtio_device!");
		return -ENOMEM;
	}

	virt_net_dev->vdev.dev.parent = parent;
	virt_net_dev->vdev.dev.release = poweren_veth_vnet_release;

	virt_net_dev->vdev.id.vendor = 0;
	virt_net_dev->vdev.id.device = VIRTIO_ID_NET;
	virt_net_dev->vdev.config = &virt_net_config_ops;
	virt_net_dev->ops = ops;

	poweren_veth_info("Initializing SMA for device");

	err = ops->init(&virt_net_dev->config);

	if (err) {
		poweren_veth_info("SMA initialization error: %d", err);
		goto sma_error;
	}

	poweren_veth_info("Opening SMA for device");

	err = ops->open(&virt_net_dev->hndl);

	if (err) {
		poweren_veth_info("SMA open failure: %d", err);
		goto poweren_veth_sma_open_error;
	}

	err = register_virtio_device(&virt_net_dev->vdev);

	if (err) {
		poweren_veth_info("virtio registration failure: %d", err);
		goto virtio_error;
	}

	if (ops->init_done != NULL)
		ops->init_done(&virt_net_dev->vdev);

	return err;

virtio_error:
	ops->close(virt_net_dev->hndl);

poweren_veth_sma_open_error:
	ops->term(virt_net_dev->hndl);

sma_error:
	kfree(virt_net_dev);
	virt_net_dev = NULL;

	return err;
}

struct device *poweren_veth_vnet_device(void)
{
	return virt_net_dev->vdev.dev.parent;
}
