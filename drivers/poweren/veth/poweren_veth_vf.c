
#include <linux/pci.h>

#include "poweren_veth_vf.h"
#include "poweren_veth_slotmgr.h"

static struct vf_driver vf_drv;
static struct poweren_ep_vf *vf_dev;

static int poweren_veth_vf_dev_probe(struct poweren_ep_vf *vfd)
{
	vf_dev = vfd;
	return 0;
}

static void poweren_veth_vf_dev_remove(struct poweren_ep_vf *vfd)
{
	/* nothing to do here */
}

inline int poweren_veth_vf_init()
{
	int rc;

	vf_drv.vf_num = 1;
	vf_drv.name = "veth_1";
	vf_drv.probe = poweren_veth_vf_dev_probe;
	vf_drv.remove = poweren_veth_vf_dev_remove;

	rc = poweren_ep_register_vf_driver(&vf_drv);

	if (rc)
		return -ENOSPC;

	return 0;
}

inline void *poweren_veth_vf_map_sma(u8 type, u64 *size)
{
	return poweren_veth_get_slot(vf_dev, type, size);
}

inline int poweren_veth_vf_unregister()
{
	poweren_ep_unregister_vf_driver(&vf_drv);
	return 0;
}

#ifndef CONFIG_POWEREN_EP_DEVICE
	inline int poweren_veth_vf_request_irq(irq_func_t func, void *ptr)
	{
		int err;
		struct msix_entry *msix;

		msix = poweren_ep_get_msix_entries(vf_dev);

		err = request_irq(msix[POWEREN_VETH_VF_MSIX].vector, func,
				  0, "vf_veth_irq", ptr);

		if (err)
			return -ENOSPC;

		return 0;
	}

	inline void poweren_veth_vf_free_irq(void *ptr)
	{
		struct msix_entry *msix;

		msix = poweren_ep_get_msix_entries(vf_dev);
		free_irq(msix[POWEREN_VETH_VF_MSIX].vector, ptr);
	}

	inline struct iommu_domain *poweren_veth_vf_get_iommu_dom()
	{
		return poweren_ep_get_iommu_dom(vf_dev);
	}
#else
	inline void poweren_veth_vf_poke(unsigned short flags)
	{
		u64 *irq_trigger = poweren_ep_get_interrupt_trigger(vf_dev);

		/* reset pci trigger register, then poke */
		out_be64(irq_trigger, POWEREN_VETH_VF_RESET);
		out_be64(irq_trigger, flags);
	}
#endif
