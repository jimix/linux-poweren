#ifndef _POWEREN_VETH_SLOTMGR_H_
#define _POWEREN_VETH_SLOTMGR_H_

#include <poweren_ep_sm.h>

void *poweren_veth_get_slot(struct poweren_ep_vf*, u8 type, u64 *size);
int poweren_veth_release_slot(struct poweren_ep_vf*, void *ptr, int size);

#endif
