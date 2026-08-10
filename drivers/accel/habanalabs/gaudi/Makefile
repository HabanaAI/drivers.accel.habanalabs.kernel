# SPDX-License-Identifier: GPL-2.0-only
HL_GAUDI_FILES := gaudi/gaudi.o gaudi/gaudi_security.o \
	gaudi/gaudi_coresight.o

HL_GAUDI_FILES += gaudi/gaudi_cn.o gaudi/gaudi_bringup.o \
	gaudi/gaudi_security_emulation.o

ifeq ($(HL_DOWNSTREAM),y)
HL_GAUDI2_FILES += gaudi/gaudi_simulator.o
endif
