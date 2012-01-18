#ifndef VIRT_VRING_H_
#define VIRT_VRING_H_

/* vring */
#define POWEREN_VRING_ALIGN 4096

static inline bool vring_has_avail(struct vring *vring, uint16_t last_idx)
{
	return (last_idx != vring->avail->idx);
}

static inline bool vring_avail_free(struct vring *vring, uint16_t last_idx)
{
	return vring->num - (2 * (vring->avail->idx - last_idx));
}

static inline bool vring_is_input_desc(struct vring *vring, uint16_t idx)
{
	return vring->desc[idx].flags & VRING_DESC_F_WRITE;
}

static inline void vring_continue_desc_chain(struct vring *vring, uint16_t idx)
{
	vring->desc[idx].flags |= VRING_DESC_F_NEXT;
}

static inline void vring_end_desc_chain(struct vring *vring, uint16_t idx)
{
	vring->desc[idx].flags &= (~VRING_DESC_F_NEXT);
}

static inline void *vring_desc_to_skb(struct vring_desc *desc)
{
	return phys_to_virt(desc->addr) - offsetof(struct sk_buff, cb);
}

static inline u16 vring_get_avail_head(struct vring *vring, u16 last_avail)
{
	return vring->avail->ring[last_avail % vring->num];
}

static inline bool vring_next_desc(struct vring *vring, u16 *idx)
{
	return vring->desc[*idx].flags & VRING_DESC_F_NEXT
			&& (*idx = vring->desc[*idx].next);
}

static inline void vring_set_used(struct vring *vring, u16 idx, u32 len)
{
	struct vring_used_elem *used =
		&vring->used->ring[vring->used->idx % vring->num];

	used->len = len;
	used->id = idx;

	wmb(); /* ensure buffer is written before updating index */

	vring->used->idx++;
}

/* Set to advise virtio whether to kick the driver when it
 * adds a buffer. Unreliable optimization. Virtio will still
 * kick if it's out of buffers. */
inline void vring_set_used_notify(struct vring *vring, bool enable)
{
	if (enable)
		vring->used->flags &= (~VRING_USED_F_NO_NOTIFY);
	else
		vring->used->flags |= VRING_USED_F_NO_NOTIFY;
}

/* Check whether virtio wants to be interrupted when the
 * driver consumes a buffer. Unreliable optimization. */
inline bool vring_avail_irq_on(struct vring *vring)
{
	return !(vring->avail->flags & VRING_AVAIL_F_NO_INTERRUPT);
}

/*Converts vring to addresses including start descriptor */
static inline int vring_to_addrs_all(struct vring *vring, u16 *i,
		struct mem_addr *dest, u16 num)
{
	do {
		dest[num].addr = (dma_addr_t)phys_to_virt(vring->desc[*i].addr);
		dest[num].len = vring->desc[*i].len;
		dest[num].idx = *i;
		num++;
	} while (vring_next_desc(vring, i));

	return num;
}

/*Converts vring to addresses excluding start descriptor */
static inline int vring_to_addrs(struct vring *vring, u16 *i,
		struct mem_addr *dest, u16 num)
{
	while (vring_next_desc(vring, i)) {
		dest[num].addr = (dma_addr_t)phys_to_virt(vring->desc[*i].addr);
		dest[num].len = vring->desc[*i].len;
		dest[num].idx = *i;
		num++;
	}

	return num;
}

#endif /* VIRT_VRING_H_ */
