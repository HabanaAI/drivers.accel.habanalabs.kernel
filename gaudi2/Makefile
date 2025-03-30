# SPDX-License-Identifier: GPL-2.0-only

# Goes upstream
HL_GAUDI2_FILES := gaudi2/gaudi2.o gaudi2/gaudi2_security.o \
		gaudi2/gaudi2_coresight.o

# Doesn't go upstream
HL_GAUDI2_FILES += gaudi2/gaudi2_cn.o \
		gaudi2/gaudi2_bringup.o gaudi2/gaudi2_hbm_bringup.o \
		gaudi2/gaudi2_cn_bringup.o

ifeq ($(HL_DOWNSTREAM),y)
HL_GAUDI2_FILES += gaudi2/gaudi2_simulator.o
endif
