
#ifndef _POWEREN_VETH_VF_H_
#define _POWEREN_VETH_VF_H_

#include <linux/interrupt.h>
#include <linux/iommu.h>

#include <poweren_ep_driver.h>
#include <poweren_ep_vf.h>

/* MSB = msix[0], LSB = msix[15] */
#define POWEREN_VETH_VF_MSIX	0
#define POWEREN_VETH_VF_TRIGGER	(0x8000 >> POWEREN_VETH_VF_MSIX)
#define POWEREN_VETH_VF_RESET	0x0000000000000000ull

typedef irqreturn_t (*irq_func_t)(int irqnum, void *ptr);

int poweren_veth_vf_init(void);
int poweren_veth_vf_unregister(void);

void *poweren_veth_vf_map_sma(u8 type, u64 *size);

#ifndef CONFIG_POWEREN_EP_DEVICE
	int poweren_veth_vf_request_irq(irq_func_t func, void *ptr);
	void poweren_veth_vf_free_irq(void *ptr);

	struct iommu_domain *poweren_veth_vf_get_iommu_dom(void);
#else
	void poweren_veth_vf_poke(unsigned short flags);
#endif

#endif /* _POWEREN_VETH_VF_H_ */
