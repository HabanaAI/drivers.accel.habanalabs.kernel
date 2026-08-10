# SPDX-License-Identifier: GPL-2.0-only
HL_GOYA_FILES :=  goya/goya.o goya/goya_security.o goya/goya_hwmgr.o \
	goya/goya_coresight.o

HL_GOYA_FILES += goya/goya_bringup.o

ifeq ($(HL_DOWNSTREAM),y)
HL_GAUDI2_FILES += goya/goya_simulator.o
endif