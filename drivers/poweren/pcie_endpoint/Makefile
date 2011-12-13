#
# PowerEN PCI Express Endpoint Device Driver
#

ccflags-$(CONFIG_POWEREN_EP_DEBUG) += -DDEBUG -Werror

obj-$(CONFIG_POWEREN_EP_DEVICE) += poweren_ep_vf.o \
				poweren_ep_sm.o \
				poweren_ep_sm_device.o

obj-$(CONFIG_POWEREN_EP_HOST) += poweren_ep_pci_core.o

poweren_ep_pci_core-objs += poweren_ep_pci.o \
				poweren_ep_vf.o \
				poweren_ep_mm.o \
				poweren_ep_sm.o \
				poweren_ep_sm_host.o