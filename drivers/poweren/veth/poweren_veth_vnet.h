#ifndef _VIRT_NET_H_
#define _VIRT_NET_H_

#include <linux/skbuff.h>

#define POWEREN_VETH_NUM_BUFFERS 64

struct poweren_virtio_ops;
struct poweren_virtio_cfg;

int poweren_veth_vnet_recv(struct poweren_virtio_cfg *cfg);
int poweren_veth_vnet_register(struct device *parent,
		struct poweren_virtio_ops *ops);
void poweren_veth_vnet_unregister(void);

struct device *poweren_veth_vnet_device(void);

#ifdef DEBUG
#define poweren_veth_info(fmt, args...) \
	if (poweren_veth_vnet_device()) \
		dev_info(poweren_veth_vnet_device(), fmt, ## args)
#else
#define poweren_veth_info(fmt, args...) /* not debugging: nothing */
#endif

#endif /* _VIRT_NET_H_ */
