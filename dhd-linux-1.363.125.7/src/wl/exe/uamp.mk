#
# GNUmakefile for wl/exe
#
# Copyright (C) 2017, Broadcom Corporation
# All Rights Reserved.
# 
# This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
# the contents of this file may not be disclosed to third parties, copied
# or duplicated in any form, in whole or in part, without the prior
# written permission of Broadcom Corporation.
#
#
# <<Broadcom-WL-IPTag/Proprietary:>>
#
# $Id: uamp.mk 514727 2014-11-12 03:02:48Z $

ifndef	SRCBASE
	SRCBASE = ../..
endif

UNAME = $(shell uname)


#-----------------------------------------------------------------
# Linux build
#

# This should be one of values recognized in src/Makerules
# 2) not windows, need to include first to pick up TARGETENV dependent vars
include $(SRCBASE)/Makerules

# Discard any "MMX" or other qualifications on x86 so that
# any TARGETARCH containing x86 is just "x86"
ifeq ($(findstring x86,$(TARGETARCH)),x86)
        TARGETARCH = x86
endif

# $(TARGETARCH) is set based on TARGETENV in src/Makerules.* files
UAMP_OBJS      := uamp_linux.o

# Prefix obj/<type>/TARGETARCH to produced .obj files
UAMP_OBJS      := $(UAMP_OBJS:%.o=obj/uamp/$(TARGETARCH)/%.o)

# TODO: Move final built objects to respective TARGETARCH dirs as well
# Final exe names
ARCH_SFX     := $(if $(findstring x86,$(TARGETARCH)),,$(TARGETARCH))
UAMP_EXE       := uamp$(ARCH_SFX)

# extra warnings
CFLAGS += -Wextra $(CUSTOM_FLAGS)

CFLAGS += -DLINUX -DWLBTAMP

LDFLAGS += -pthread -lrt

vpath %.c $(SRCBASE)/wl/sys $(WLAN_StdSrcDirsA) $(WLAN_ComponentSrcDirs)

.PHONY: all
all: $(UAMP_EXE)

# Compilation targets
obj/uamp/$(TARGETARCH)/%.o: %.c
	@mkdir -pv $(@D)
	$(CC) -c $(CFLAGS) -o $@ $^

# Final link targets
$(UAMP_EXE): $(UAMP_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ 

.PHONY: clean
clean:
	 rm -fv $(UAMP_EXE) $(UAMP_OBJS)
