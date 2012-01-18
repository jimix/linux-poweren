
/*
 * For PowerEN, in the absence of device-side HW interrupts,
 * we use this scheduled means of polling for RX packets.
*/

#include <linux/interrupt.h>

struct pseudo_interrupt {
	struct tasklet_struct task;
	struct timer_list timer;
	bool schedule;
	int misses;
};

#define IRQ_MISSES 25000

static inline void poweren_veth_irq_init(struct pseudo_interrupt *irq,
		void (*irq_func)(unsigned long), u64 cfg)
{
	tasklet_init(&irq->task, irq_func, cfg);
	init_timer(&irq->timer);
}

static inline void poweren_veth_irq_sched(struct pseudo_interrupt *irq)
{
	irq->schedule = true;
	tasklet_schedule(&irq->task);
}

static inline void poweren_veth_irq_stop(struct pseudo_interrupt *irq)
{
	irq->schedule = false;
	tasklet_kill(&irq->task);
	del_timer_sync(&irq->timer);
}

static inline irqreturn_t poweren_veth_rx_irq(int irqnum, void *ptr)
{
	poweren_veth_vnet_recv(ptr);
	return IRQ_HANDLED;
}

static inline void poweren_veth_rx_soft_irq(unsigned long ptr)
{
	struct poweren_virtio_cfg *cfg = (struct poweren_virtio_cfg *)ptr;
	struct pseudo_interrupt *irq = (struct pseudo_interrupt *)cfg->priv;

	if (irq->schedule) {
		if (cfg->ops->can_recv_pkt(cfg->hndl)) {
			irq->misses = 0;
			poweren_veth_vnet_recv(cfg);
		} else {
			irq->misses++;
		}

		if (irq->misses < IRQ_MISSES) {
			tasklet_schedule(&irq->task);
		} else {
			/* No activity - switch to timer */
			init_timer(&irq->timer);
			irq->timer.function = poweren_veth_rx_soft_irq;
			irq->timer.data = (unsigned long)ptr;
			irq->timer.expires = jiffies;
			add_timer(&irq->timer);
		}
	}
}
