obj-$(CONFIG_WCN_BOOT) := marlin.o
# enable gnss mode
# gnss mode
marlin-$(CONFIG_WCN_GNSS) += gnss/gnss_common_ctl.o \
			     gnss/gnss_pmnotify_ctl.o \
			     gnss/gnss_dbg.o \
			     gnss/gnss_dump.o
#end
MARLINOFILES += wcn_boot.o
marlin-$(CONFIG_WCN_PCIE) += pcie_boot.o
MARLINOFILES += wcn_dump.o
MARLINOFILES += bufring.o
MARLINOFILES += wcn_txrx.o
MARLINOFILES += wcn_op.o
ifdef CONFIG_SC2342_INTEG
MARLINOFILES += rf/rf.o
MARLINOFILES += wcn_misc.o
endif
MARLINOFILES += wcn_parn_parser.o
MARLINOFILES += rdc_debug.o
MARLINOFILES += wcn_log.o
MARLINOFILES += wcn_procfs.o
MARLINOFILES += loopcheck.o
marlin-$(CONFIG_SDIOHAL) += mem_pd_mgr.o
marlin-$(CONFIG_WCN_UTILS) += \
		wcn_debugfs.o
ifneq ($(TARGET_BUILD_VARIANT),user)
ccflags-y += -DCONFIG_WCN_LOOPCHECK
ccflags-y += -DCONFIG_CPLOG_DEBUG
endif
marlin-objs += $(MARLINOFILES)

#WCN_PLATFORM
obj-$(CONFIG_WCN_PLATFORM) := wcn_platform.o
wcn_platform-c-objs += wcn_parn_parser.o

wcn_platform-c-objs +=	gnss/gnss_common_ctl.o \
			gnss/gnss_pmnotify_ctl.o \
			gnss/gnss_dbg.o \
			gnss/gnss_dump.o
wcn_platform-c-objs +=  wcn_procfs.o \
			wcn_txrx.o \
			rdc_debug.o \
			wcn_log.o \
			rf/rf.o \
			bufring.o
ifeq ($(CONFIG_SC2342_INTEG),y)
wcn_platform-c-objs +=  wcn_dump_integrate.o \
			wcn_misc.o
endif
wcn_platform-objs += $(wcn_platform-c-objs)
