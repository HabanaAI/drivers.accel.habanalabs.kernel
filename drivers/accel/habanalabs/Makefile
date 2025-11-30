# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for HabanaLabs AI accelerators driver
#

CONFIG_DRM_ACCEL_HABANALABS := m

obj-$(CONFIG_DRM_ACCEL_HABANALABS) := habanalabs.o

include $(src)/common/Makefile
habanalabs-y += $(HL_COMMON_FILES)

include $(src)/gaudi3/Makefile
habanalabs-y += $(HL_GAUDI3_FILES)

include $(src)/gaudi2/Makefile
habanalabs-y += $(HL_GAUDI2_FILES)

include $(src)/gaudi/Makefile
habanalabs-y += $(HL_GAUDI_FILES)

include $(src)/goya/Makefile
habanalabs-y += $(HL_GOYA_FILES)

habanalabs-$(CONFIG_DEBUG_FS) += common/debugfs.o

#include $(src)/common/Makefile.compat

ccflags-y += $(HL_CFLAGS) -Werror -Wmaybe-uninitialized

habanalabs-y += common/importer_drv.o common/habanalabs_compat_accel.o

LINUXINCLUDE := -I$(src)/../../../include -I$(src) $(LINUXINCLUDE)

ifdef OFED_PATH
KBUILD_EXTRA_SYMBOLS := $(OFED_PATH)/Module.symvers
endif
