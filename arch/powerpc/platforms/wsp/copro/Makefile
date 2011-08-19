ifeq ($(CONFIG_PPC_WSP_COPRO_DEBUG),y)
EXTRA_CFLAGS		+= -DDEBUG -Werror
endif

ifeq ($(CONFIG_PPC_WSP_COPRO_GCOV),y)
GCOV_PROFILE := y
endif

obj-y					+= cop.o
obj-y					+= pbic.o
obj-y					+= pbic_tlb.o
obj-y					+= instance.o
obj-y					+= abort.o
obj-y					+= type.o
obj-y					+= driver.o
obj-y					+= sync.o
obj-y					+= imq.o
obj-y					+= topology.o
obj-y					+= unit.o 	# must be first unit
obj-$(CONFIG_PPC_WSP_UNIT_CDMX)		+= unit_cdmx.o
obj-$(CONFIG_PPC_WSP_UNIT_CMPX)		+= unit_cmpx.o
obj-$(CONFIG_PPC_WSP_UNIT_REGX)		+= unit_regx.o
obj-y					+= unit_fini.o  # must be last unit

obj-$(CONFIG_PPC_WSP_COPRO_DEBUGFS)	+= pbic_debug.o
