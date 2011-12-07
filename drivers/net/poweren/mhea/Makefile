# make sure all know about this directory
EXTRA_CFLAGS += -Idrivers/net/poweren/include

# due to GCC bug 43949
EXTRA_CFLAGS += -Wno-array-bounds

obj-$(CONFIG_POWEREN_MHEA) += mhea.o
mhea-objs := mhea-main.o

