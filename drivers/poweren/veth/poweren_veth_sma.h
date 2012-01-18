#ifndef _SMA_H_
#define _SMA_H_

#include "poweren_virtio.h"

#define SMA_MAP_LOCAL	0
#define SMA_MAP_REMOTE	1

typedef char *(*sma_mapfunc_t)(u8 type, u64 *size);
typedef void *(*sma_writefunc_t)(void *dest, const void *src, size_t len);

int poweren_veth_sma_init(sma_mapfunc_t);
int poweren_veth_sma_open(void **hndl);
int poweren_veth_sma_close(void *hndl);

int poweren_veth_sma_newpkts(void *hndl);
int poweren_veth_sma_space(void *hndl);

int poweren_veth_sma_readpkt(void *hndl, struct mem_addr *buf);
int poweren_veth_sma_writepkt(void *hndl, const struct mem_addr *src,
		sma_writefunc_t func);

#ifndef CONFIG_POWEREN_EP_DEVICE
typedef dma_addr_t (*sma_pinfunc_t)(dma_addr_t, u16);
typedef void (*sma_unpinfunc_t)(dma_addr_t);

int poweren_veth_sma_post(void *hndl, struct mem_addr *sg,
		u16 count, sma_pinfunc_t pin);

int poweren_veth_sma_readpkt_unpin(void *hndl, struct mem_addr *buf,
		u16 idx, sma_unpinfunc_t unpin);
#else
int poweren_veth_sma_cmd_newpkts(void *hndl);
int poweren_veth_sma_buffers_space(void *hndl);
#endif

#endif /* _SMA_H_ */
