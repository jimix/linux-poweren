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

obj-$(CONFIG_POWEREN_EP_HP_DEVICE) += poweren_ep_hp_dd_device.o

poweren_ep_hp_dd_device-objs += poweren_ep_bp_device.o \
				poweren_ep_mr.o \
				poweren_ep_mr_device.o \
				poweren_ep_hp.o

obj-$(CONFIG_POWEREN_EP_HP_HOST) += poweren_ep_hp_dd_host.o

poweren_ep_hp_dd_host-objs += poweren_ep_bp_host.o \
				poweren_ep_mr.o \
				poweren_ep_mr_host.o \
				poweren_ep_hp.o
