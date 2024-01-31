# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for HabanaLabs AI accelerators driver
#

ifneq ($(PATCHLEVEL),)
# kbuild part of makefile

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

include $(src)/common/Makefile.compat

ccflags-y += $(HL_CFLAGS) -Werror -Wmaybe-uninitialized

habanalabs-y += common/simulator.o common/habanalabs_compat.o \
		common/importer_drv.o common/habanalabs_compat_accel.o

LINUXINCLUDE := -I$(src)/../../../include -I$(src) $(LINUXINCLUDE)

else
# normal makefile

KVERSION ?= $(shell uname -r)
KERNELDIR := /lib/modules/$(KVERSION)/build
SRC_DIR ?= $(shell pwd)
GIT_SHA ?= $(shell git --git-dir=${HABANALABS_ROOT}/.git rev-parse --short HEAD)
DEBUG_CFLAGS += -g -DDEBUG

DRV_CFLAGS_MODULE="-DHL_DRIVER_GIT_SHA=$(GIT_SHA)"

default:
	$(MAKE) CFLAGS_MODULE=$(DRV_CFLAGS_MODULE) -C $(KERNELDIR) M=$(SRC_DIR) W=1 modules

debug:
	$(MAKE) EXTRA_CFLAGS="$(DEBUG_CFLAGS)" CFLAGS_MODULE=$(DRV_CFLAGS_MODULE) -C $(KERNELDIR) M=$(SRC_DIR) W=1 modules

custom:
	$(MAKE) CFLAGS_MODULE=$(DRV_CFLAGS_MODULE) -C $(CUSTOMKERNELDIR) M=$(SRC_DIR) W=1 modules

debug_custom:
	$(MAKE) EXTRA_CFLAGS="$(DEBUG_CFLAGS)" CFLAGS_MODULE=$(DRV_CFLAGS_MODULE) -C $(CUSTOMKERNELDIR) M=$(SRC_DIR) W=1 modules

importer:
	$(MAKE) CFLAGS_MODULE="$(DRV_CFLAGS_MODULE) -D__IMPORTER" -C $(KERNELDIR) M=$(SRC_DIR) W=1 modules

custom_importer:
	$(MAKE) CFLAGS_MODULE="$(DRV_CFLAGS_MODULE) -D__IMPORTER" -C $(CUSTOMKERNELDIR) M=$(SRC_DIR) W=1 modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(SRC_DIR) clean
	rm -f Module.symvers
	rm -f *.ur-safe
	rm -f common/*.ur-safe
	rm -f goya/*.ur-safe
	rm -f gaudi/*.ur-safe
	rm -f gaudi2/*.ur-safe
	rm -f gaudi3/*.ur-safe
	rm -f cn/*.ur-safe

endif
