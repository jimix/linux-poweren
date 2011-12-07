# make sure all know about this directory
EXTRA_CFLAGS += -Idrivers/net/poweren/include

# due to GCC bug 43949
EXTRA_CFLAGS += -Wno-array-bounds

obj-$(CONFIG_POWEREN_RHEA) += rhea.o
rhea-objs := rhea-poweren.o rhea-eq0.o rhea-main.o
rhea-objs += rhea-interface.o
rhea-objs += rhea-base.o rhea-eq.o rhea-cq.o rhea-qp.o
rhea-objs += rhea-rules.o rhea-rules-set.o
rhea-objs += rhea-channel.o rhea-qpn.o rhea-tcam.o rhea-hasher.o
rhea-objs += rhea-channel-resource.o
rhea-objs += rhea-main.o rhea-linux.o
rhea-objs += rhea-mc-bc-manager.o

