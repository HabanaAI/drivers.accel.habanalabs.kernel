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

ifdef OFED_PATH
KBUILD_EXTRA_SYMBOLS := $(OFED_PATH)/Module.symvers
endif

else
# normal makefile

RED=$(shell echo "\033[0;31m")
RESET=$(shell echo "\033[0m")

# Backwards compatiblity

ifneq ($(filter custom debug_custom custom_importer,${MAKECMDGOALS}),)
$(info ${RED}deprecated: the target '${MAKECMDGOALS}' is deprecated, to use a custom kernel, export 'KERNELDIR=<path to kernel build dir>'${RESET})
ifdef CUSTOMKERNELDIR
KERNELDIR := ${CUSTOMKERNELDIR}
endif
endif

KVERSION ?= $(shell uname -r)
KERNELDIR ?= /lib/modules/$(KVERSION)/build
SRC_DIR ?= $(shell pwd)
GIT_LOCAL_CHANGES_STR := $(shell cd ${HABANALABS_ROOT}; if [ -n "$$(git status --porcelain 2> /dev/null | grep "^[[:space:]]*M ")" ]; then echo "+"; fi)
GIT_SHA ?= $(shell git --git-dir=${HABANALABS_ROOT}/.git rev-parse --short HEAD 2> /dev/null)
GIT_SHA_DRV_STR := $(shell echo "${GIT_SHA}${GIT_LOCAL_CHANGES_STR}")
DEBUG_CFLAGS += -g -DDEBUG
KVERSION_MAJOR := $(shell uname -r | awk -F'[.-]' '{print $$1}')
KVERSION_MINOR := $(shell uname -r | awk -F'[.-]' '{print $$2}')
KVERSION_REV := $(shell uname -r | awk -F'[.-]' '{print $$3}')

RUN_ALL_EXTRA_WARNINGS := W=1
SELECTED_EXTRA_WARNINGS :=
ifeq ($(KVERSION_MAJOR),5)
ifeq ($(KVERSION_MINOR),10)
ifeq ($(shell test $(KVERSION_REV) -ge 210; echo $$?),0)
RUN_ALL_EXTRA_WARNINGS :=
SELECTED_EXTRA_WARNINGS := -Wextra -Wunused -Wno-unused-parameter
SELECTED_EXTRA_WARNINGS += -Wmissing-declarations
SELECTED_EXTRA_WARNINGS += -Wmissing-format-attribute
SELECTED_EXTRA_WARNINGS += -Wmissing-prototypes
SELECTED_EXTRA_WARNINGS += -Wold-style-definition
SELECTED_EXTRA_WARNINGS += -Wmissing-include-dirs
SELECTED_EXTRA_WARNINGS += $(call cc-option, -Wunused-but-set-variable)
SELECTED_EXTRA_WARNINGS += $(call cc-option, -Wunused-const-variable)
SELECTED_EXTRA_WARNINGS += $(call cc-option, -Wpacked-not-aligned)
SELECTED_EXTRA_WARNINGS += $(call cc-option, -Wstringop-truncation)
# The following turn off the warnings enabled by -Wextra
SELECTED_EXTRA_WARNINGS += -Wno-missing-field-initializers
SELECTED_EXTRA_WARNINGS += -Wno-sign-compare
SELECTED_EXTRA_WARNINGS += -Wno-type-limits
endif
endif
endif
DRV_CFLAGS_MODULE="-DHL_DRIVER_GIT_SHA=$(GIT_SHA_DRV_STR) $(SELECTED_EXTRA_WARNINGS)"

default custom: modules

modules:
	$(MAKE) CFLAGS_MODULE=$(DRV_CFLAGS_MODULE) -C $(KERNELDIR) M=$(SRC_DIR) $(RUN_ALL_EXTRA_WARNINGS) $@

debug debug_custom:
	$(MAKE) CFLAGS_MODULE=$(DRV_CFLAGS_MODULE) -C $(KERNELDIR) M=$(SRC_DIR) $(RUN_ALL_EXTRA_WARNINGS) EXTRA_CFLAGS="$(DEBUG_CFLAGS)" modules

importer custom_importer:
	$(MAKE) CFLAGS_MODULE='$(DRV_CFLAGS_MODULE) -D__IMPORTER' -C $(KERNELDIR) M=$(SRC_DIR) $(RUN_ALL_EXTRA_WARNINGS) modules

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
