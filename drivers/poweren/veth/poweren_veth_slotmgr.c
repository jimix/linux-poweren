
#include <asm/page.h>
#include <linux/jiffies.h>

#include "poweren_veth_slotmgr.h"

#define POWEREN_VETH_SLOT_LOCAL			0
#define POWEREN_VETH_SLOT_REMOTE		1

#define POWEREN_VETH_SLOT_FLAG			0xFFFE
#define POWEREN_VETH_SLOT_KEY			9778384 /* WSPVETH */

static long get_slot(struct poweren_ep_vf *wsp_vf,
		u32 key, u32 flags, u64 *size) {

	long unsigned int slot_size;
	long slot = -1;

	slot = poweren_ep_slotmgr_connect(wsp_vf, key, flags, &slot_size);
	*size = slot_size;

	return slot;
}

long find_slot(struct poweren_ep_vf *wsp_vf, u32 key, u64 *size)
{
	long int slot;
	long unsigned int slot_size;

	slot =  poweren_ep_slotmgr_find_slot(wsp_vf, key, &slot_size);

	/* ignore anythiang that isn't a valid slot value */
	if ((slot >= HOST_SLOT_REQ) && (slot <= SLOT_TIMEOUT))
		slot = 0 - slot;
	else
		*size = slot_size;

	return slot;
}

/*
 * Public functions which can be called by other kernel modules.
 */
inline void *poweren_veth_get_slot(struct poweren_ep_vf *wsp_vf,
		u8 type, u64 *size)
{
	long int slot = -1;
	void *address;

	if (type == POWEREN_VETH_SLOT_LOCAL) {

		slot = get_slot(wsp_vf, POWEREN_VETH_SLOT_KEY,
				POWEREN_VETH_SLOT_FLAG, size);

		if (slot < 0)
			return NULL;

		slot = slot >> PAGE_SHIFT;

		address = poweren_ep_get_slot_local_sma(wsp_vf, slot);

		return address;

	} else if (type == POWEREN_VETH_SLOT_REMOTE) {

		slot = find_slot(wsp_vf, POWEREN_VETH_SLOT_KEY, size);

		if (slot  < 0)
			return NULL;

		slot = slot >> PAGE_SHIFT;

		address = poweren_ep_get_slot_remote_sma(wsp_vf, slot);

		return address;

	} else
		return NULL;
}

inline int poweren_veth_release_slot(struct poweren_ep_vf *wsp_vf,
		void *ptr, int size)
{
	int slot;

	slot =  poweren_ep_find_slot_from_addr(wsp_vf, (u64)ptr);

	if (slot != -1)
		poweren_ep_slotmgr_term(wsp_vf, slot);
	else
		return SLOTMGR_ERR;

	return 0;
}
