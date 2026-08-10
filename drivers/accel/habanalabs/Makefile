# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for HabanaLabs AI accelerators driver
#

CONFIG_DRM_ACCEL_HABANALABS := m

obj-$(CONFIG_DRM_ACCEL_HABANALABS) := habanalabs.o

include $(src)/common/Makefile
habanalabs-y += $(HL_COMMON_FILES)

include $(src)/cn/Makefile
habanalabs-y += $(HL_CN_FILES)

include $(src)/gaudi3/Makefile
habanalabs-y += $(HL_GAUDI3_FILES)

include $(src)/gaudi2/Makefile
habanalabs-y += $(HL_GAUDI2_FILES)

include $(src)/gaudi/Makefile
habanalabs-y += $(HL_GAUDI_FILES)

include $(src)/goya/Makefile
habanalabs-y += $(HL_GOYA_FILES)

habanalabs-$(CONFIG_DEBUG_FS) += common/debugfs.o

habanalabs-y += common/habanalabs_compat_accel.o
habanalabs-$(HL_DOWNSTREAM) += common/importer_drv.o common/simulator.o

ccflags-y += -I$(src)
ccflags-$(HL_GENERATE_USED_MACROS) += -fplugin=$(src)/../../../scripts/sanitize_headers/usedmacros.so
