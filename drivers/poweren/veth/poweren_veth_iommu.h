
#ifndef POWEREN_VETH_IOMMU_H_
#define POWEREN_VETH_IOMMU_H_

#include <linux/iommu.h>
#include <asm/page.h>

#include "poweren_veth_vf.h"

#define POWEREN_VETH_IOMMU_ADDR_INC PAGE_SIZE
#define POWEREN_VETH_IOMMU_ADDR_BASE 0x40000000ul

static struct iommu_domain *iommu_dom;

static inline dma_addr_t poweren_veth_iommu_map(void *addr, u32 size, int prot)
{
	phys_addr_t paddr;
	u64 page_off;

	if (!iommu_dom)
		iommu_dom = poweren_veth_vf_get_iommu_dom();

	paddr = page_to_phys(virt_to_page(addr));
	page_off = (unsigned long)addr & ~PAGE_MASK;

	if (!iommu_iova_to_phys(iommu_dom, paddr))
		iommu_map(iommu_dom, paddr, paddr, get_order(size), prot);

	return (dma_addr_t)(paddr + page_off);
}

#endif /* POWEREN_VETH_IOMMU_H_ */
