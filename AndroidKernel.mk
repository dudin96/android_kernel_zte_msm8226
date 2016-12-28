#Android makefile to build kernel as a part of Android Build
PERL		= perl

ifeq ($(TARGET_PREBUILT_KERNEL),)

KERNEL_OUT := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ
KERNEL_CONFIG := $(KERNEL_OUT)/.config
ifeq ($(TARGET_KERNEL_APPEND_DTB), true)
TARGET_PREBUILT_INT_KERNEL := $(KERNEL_OUT)/arch/arm/boot/zImage-dtb
else
TARGET_PREBUILT_INT_KERNEL := $(KERNEL_OUT)/arch/arm/boot/zImage
endif
KERNEL_HEADERS_INSTALL := $(KERNEL_OUT)/usr
KERNEL_MODULES_INSTALL := system
KERNEL_MODULES_OUT := $(TARGET_OUT)/lib/modules
KERNEL_IMG=$(KERNEL_OUT)/arch/arm/boot/Image
# relative path from KERNEL_OUT to kernel source directory
KERNEL_SOURCE_RELATIVE_PATH := ../../../../../../kernel

DTS_NAMES ?= $(shell $(PERL) -e 'while (<>) {$$a = $$1 if /CONFIG_ARCH_((?:MSM|QSD|MPQ)[a-zA-Z0-9]+)=y/; $$r = $$1 if /CONFIG_MSM_SOC_REV_(?!NONE)(\w+)=y/; $$arch = $$arch.lc("$$a$$r ") if /CONFIG_ARCH_((?:MSM|QSD|MPQ)[a-zA-Z0-9]+)=y/} print $$arch;' $(KERNEL_CONFIG))
KERNEL_USE_OF ?= $(shell $(PERL) -e '$$of = "n"; while (<>) { if (/CONFIG_USE_OF=y/) { $$of = "y"; break; } } print $$of;' kernel/arch/arm/configs/$(KERNEL_DEFCONFIG))

# Avoid auto-generating .dts files that match QC's pattern
ifeq ($(TARGET_KERNEL_SELECT_OF_DT),true)
    KERNEL_USE_OF := "n"
endif

ifeq "$(KERNEL_USE_OF)" "y"
DTS_FILES = $(wildcard $(TOP)/kernel/arch/arm/boot/dts/$(DTS_NAME)*.dts)
DTS_FILE = $(lastword $(subst /, ,$(1)))
DTB_FILE = $(addprefix $(KERNEL_OUT)/arch/arm/boot/,$(patsubst %.dts,%.dtb,$(call DTS_FILE,$(1))))
ZIMG_FILE = $(addprefix $(KERNEL_OUT)/arch/arm/boot/,$(patsubst %.dts,%-zImage,$(call DTS_FILE,$(1))))
KERNEL_ZIMG = $(KERNEL_OUT)/arch/arm/boot/zImage
DTC = $(KERNEL_OUT)/scripts/dtc/dtc

define append-dtb
mkdir -p $(KERNEL_OUT)/arch/arm/boot;\
$(foreach DTS_NAME, $(DTS_NAMES), \
   $(foreach d, $(DTS_FILES), \
      $(DTC) -p 1024 -O dtb -o $(call DTB_FILE,$(d)) $(d); \
      cat $(KERNEL_ZIMG) $(call DTB_FILE,$(d)) > $(call ZIMG_FILE,$(d));))
endef
else

define append-dtb
endef
endif

ifeq ($(TARGET_USES_UNCOMPRESSED_KERNEL),true)
$(info Using uncompressed kernel)
TARGET_PREBUILT_KERNEL := $(KERNEL_OUT)/piggy
else
TARGET_PREBUILT_KERNEL := $(TARGET_PREBUILT_INT_KERNEL)
endif

define clean-module-folder
mdpath=`find $(KERNEL_MODULES_OUT) -type f -name modules.order`;\
if [ "$$mdpath" != "" ];then\
mpath=`dirname $$mdpath`; rm -rf $$mpath;\
fi
endef

include kernel/defconfig.mk

define do-kernel-config
	( cp $(3) $(2) && $(7) -C $(4) O=$(1) ARCH=$(5) CROSS_COMPILE=$(6) KBUILD_BUILD_USER=$(TARGET_KERNEL_BUILD_USER) KBUILD_BUILD_HOST=$(TARGET_KERNEL_BUILD_HOST) defoldconfig ) || ( rm -f $(2) && false )
endef

$(KERNEL_OUT):
	mkdir -p $(KERNEL_OUT)

$(KERNEL_CONFIG): $(KERNEL_OUT) $(TARGET_DEFCONFIG)
	$(call do-kernel-config,../$(KERNEL_OUT),$@,$(TARGET_DEFCONFIG),kernel,arm,arm-linux-eabi-,$(MAKE))

$(KERNEL_OUT)/piggy : $(TARGET_PREBUILT_INT_KERNEL)
	$(hide) gunzip -c $(KERNEL_OUT)/arch/arm/boot/compressed/piggy.gzip > $(KERNEL_OUT)/piggy

$(TARGET_PREBUILT_INT_KERNEL): $(KERNEL_OUT) $(KERNEL_CONFIG) $(KERNEL_HEADERS_INSTALL)
	$(MAKE) -C kernel KBUILD_RELSRC=$(KERNEL_SOURCE_RELATIVE_PATH) O=../$(KERNEL_OUT) ARCH=arm CROSS_COMPILE=arm-linux-eabi- KBUILD_BUILD_USER=$(TARGET_KERNEL_BUILD_USER) KBUILD_BUILD_HOST=$(TARGET_KERNEL_BUILD_HOST)
	$(MAKE) -C kernel KBUILD_RELSRC=$(KERNEL_SOURCE_RELATIVE_PATH) O=../$(KERNEL_OUT) ARCH=arm CROSS_COMPILE=arm-linux-eabi- KBUILD_BUILD_USER=$(TARGET_KERNEL_BUILD_USER) KBUILD_BUILD_HOST=$(TARGET_KERNEL_BUILD_HOST) dtbs
	$(MAKE) -C kernel KBUILD_RELSRC=$(KERNEL_SOURCE_RELATIVE_PATH) O=../$(KERNEL_OUT) ARCH=arm CROSS_COMPILE=arm-linux-eabi- KBUILD_BUILD_USER=$(TARGET_KERNEL_BUILD_USER) KBUILD_BUILD_HOST=$(TARGET_KERNEL_BUILD_HOST) modules
	$(MAKE) -C kernel KBUILD_RELSRC=$(KERNEL_SOURCE_RELATIVE_PATH) O=../$(KERNEL_OUT) INSTALL_MOD_PATH=../../$(KERNEL_MODULES_INSTALL) INSTALL_MOD_STRIP="--strip-debug --remove-section=.note.gnu.build-id" ARCH=arm CROSS_COMPILE=arm-linux-eabi- KBUILD_BUILD_USER=$(TARGET_KERNEL_BUILD_USER) KBUILD_BUILD_HOST=$(TARGET_KERNEL_BUILD_HOST) modules_install
	$(mv-modules)
	$(clean-module-folder)
	$(append-dtb)

$(KERNEL_HEADERS_INSTALL): $(KERNEL_OUT) $(KERNEL_CONFIG)
	$(MAKE) -C kernel O=../$(KERNEL_OUT) ARCH=arm CROSS_COMPILE=arm-linux-eabi- headers_install

kerneltags: $(KERNEL_OUT) $(KERNEL_CONFIG)
	$(MAKE) -C kernel O=../$(KERNEL_OUT) ARCH=arm CROSS_COMPILE=arm-linux-eabi- tags

kernelconfig: $(KERNEL_OUT) $(KERNEL_CONFIG)
	env KCONFIG_NOTIMESTAMP=true \
	     $(MAKE) -C kernel O=../$(KERNEL_OUT) ARCH=arm CROSS_COMPILE=arm-linux-eabi- KBUILD_BUILD_USER=$(TARGET_KERNEL_BUILD_USER) KBUILD_BUILD_HOST=$(TARGET_KERNEL_BUILD_HOST) menuconfig
	env KCONFIG_NOTIMESTAMP=true \
	     $(MAKE) -C kernel O=../$(KERNEL_OUT) ARCH=arm CROSS_COMPILE=arm-linux-eabi- KBUILD_BUILD_USER=$(TARGET_KERNEL_BUILD_USER) KBUILD_BUILD_HOST=$(TARGET_KERNEL_BUILD_HOST) savedefconfig
	cp $(KERNEL_OUT)/defconfig kernel/arch/arm/configs/$(KERNEL_DEFCONFIG)

endif