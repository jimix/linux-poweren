#ifndef _POWEREN_VIRTIO_H_
#define _POWEREN_VIRTIO_H_

#include <linux/virtio.h>

struct mem_addr {
	dma_addr_t addr;
	u32 len;
	u16 idx;
};

struct poweren_virtio_ops;

struct poweren_virtio_cfg {
	struct poweren_virtio_ops *ops;
	void *hndl;
	void *priv;
};

struct poweren_virtio_ops {
	int (*init)(void *config);
	int (*recv_init)(struct poweren_virtio_cfg *);
	int (*xmit_init)(struct poweren_virtio_cfg *);
	int (*init_done)(struct virtio_device *);
	int (*term)(void *hndl);
	int (*open)(void **hndl);
	int (*close)(void *hndl);
	int (*can_xmit_pkt)(void *hndl);
	int (*xmit_pkt)(void *hndl, struct mem_addr *dst);
	int (*can_recv_pkt)(void *hndl);
	int (*recv_post)(void *hndl, struct mem_addr *addrs, u16 count);
	int (*recv_pkt)(void *hndl, struct mem_addr *src, u16 idx, void *data);
};

#endif /* _POWEREN_VIRTIO_H_ */
