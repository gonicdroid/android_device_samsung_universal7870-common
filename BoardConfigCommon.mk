#
# Copyright (C) 2026 The LineageOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

COMMON_PATH := device/samsung/universal7870-common

# Include path
TARGET_SPECIFIC_HEADER_PATH := $(COMMON_PATH)/include

# Architecture
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_VARIANT := cortex-a53

# Wifi
BOARD_WLAN_DEVICE                := bcmdhd
BOARD_WLAN_DEVICE_REV            := bcm4343
WPA_SUPPLICANT_VERSION           := VER_0_8_X
BOARD_WPA_SUPPLICANT_DRIVER      := NL80211
BOARD_WPA_SUPPLICANT_PRIVATE_LIB := lib_driver_cmd_bcmdhd
BOARD_HOSTAPD_DRIVER             := NL80211
BOARD_HOSTAPD_PRIVATE_LIB        := lib_driver_cmd_bcmdhd
WIFI_DRIVER_FW_PATH_PARAM        := "/sys/module/dhd/parameters/firmware_path"
WIFI_DRIVER_NVRAM_PATH_PARAM     := "/sys/module/dhd/parameters/nvram_path"
WIFI_DRIVER_NVRAM_PATH           := "/vendor/etc/wifi/nvram.txt"
WIFI_DRIVER_FW_PATH_STA          := "/vendor/etc/wifi/bcmdhd_sta.bin"
WIFI_DRIVER_FW_PATH_AP           := "/vendor/etc/wifi/bcmdhd_apsta.bin"
WIFI_BAND                        := 802_11_ABG
WPA_SUPPLICANT_USE_HIDL          := true
BOARD_HAVE_SAMSUNG_WIFI          := true

# Bluetooth
BOARD_HAVE_BLUETOOTH := true
BOARD_HAVE_BLUETOOTH_BCM := true
BOARD_HAVE_SAMSUNG_BLUETOOTH := true
#BOARD_CUSTOM_BT_CONFIG := $(COMMON_PATH)/configs/bluetooth/libbt_vndcfg.txt

# Boot image
BOARD_CUSTOM_BOOTIMG := true
BOARD_CUSTOM_BOOTIMG_MK := hardware/samsung/mkbootimg.mk
BOARD_MKBOOTIMG_ARGS := --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 --tags_offset 0x00000100
BOARD_KERNEL_BASE := 0x10000000
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_IMAGE_NAME := Image
BOARD_KERNEL_SEPARATED_DT := true
TARGET_CUSTOM_DTBTOOL := dtbhtoolExynos

# Charger
BOARD_CHARGING_MODE_BOOTING_LPM := /sys/class/power_supply/battery/batt_lp_charging
BOARD_CHARGER_SHOW_PERCENTAGE := true
CHARGING_ENABLED_PATH := /sys/class/power_supply/battery/batt_lp_charging

# Filesystems
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true

# HIDL
DEVICE_MANIFEST_FILE := $(COMMON_PATH)/manifest.xml
DEVICE_MATRIX_FILE := $(COMMON_PATH)/compatibility_matrix.xml

# Init
TARGET_INIT_VENDOR_LIB := libinit_universal7870

# Kernel
TARGET_KERNEL_ARCH := arm64
TARGET_KERNEL_HEADER_ARCH := arm64
TARGET_KERNEL_SOURCE := kernel/samsung/exynos7870
TARGET_KERNEL_LLVM_BINUTILS := false
KERNEL_TOOLCHAIN := $(shell pwd)/prebuilts/gcc/linux-x86/aarch64/linaro-7.5/bin
TARGET_KERNEL_CROSS_COMPILE_PREFIX := aarch64-linux-gnu-
TARGET_USES_64_BIT_BINDER := true

# LIBHWJPEG
TARGET_USES_GTAXL_LIBHWJPEG := false
TARGET_USES_HWC2ON1ADAPTER := true
TARGET_USES_HWC2 := true

# Lineage Health
TARGET_HEALTH_CHARGING_CONTROL_CHARGING_PATH := /sys/class/power_supply/battery/batt_slate_mode
TARGET_HEALTH_CHARGING_CONTROL_CHARGING_ENABLED := 0
TARGET_HEALTH_CHARGING_CONTROL_CHARGING_DISABLED := 1
TARGET_HEALTH_CHARGING_CONTROL_SUPPORTS_BYPASS := false
TARGET_HEALTH_CHARGING_CONTROL_SUPPORTS_TOGGLE := true
TARGET_HEALTH_CHARGING_CONTROL_SUPPORTS_DEADLINE := false

# Partitions
BOARD_BOOTIMAGE_PARTITION_SIZE     := 33554432
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 39845888
BOARD_CACHEIMAGE_PARTITION_SIZE    := 209715200
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE  := ext4
BOARD_FLASH_BLOCK_SIZE := 4096

# Platform
BOARD_VENDOR := samsung
TARGET_BOARD_PLATFORM := exynos5
TARGET_BOOTLOADER_BOARD_NAME := exynos7870
TARGET_SOC := exynos7870

TARGET_NO_BOOTLOADER := true
TARGET_NO_RADIOIMAGE := true

# Properties
TARGET_SYSTEM_PROP += $(COMMON_PATH)/system.prop
TARGET_VENDOR_PROP += $(COMMON_PATH)/vendor.prop

# Recovery
TARGET_RECOVERY_FSTAB := $(COMMON_PATH)/rootdir/recovery.fstab
TARGET_RECOVERY_PIXEL_FORMAT := "ABGR_8888"

# Release tools
TARGET_RELEASETOOLS_EXTENSIONS := $(COMMON_PATH)

# RIL
TARGET_USES_VND_SECRIL := true
ENABLE_VENDOR_RIL_SERVICE := true

# Root
BOARD_ROOT_EXTRA_FOLDERS := efs

# SELinux
BOARD_SEPOLICY_DIRS += $(COMMON_PATH)/sepolicy/vendor
BOARD_VENDOR_SEPOLICY_DIRS += $(COMMON_PATH)/sepolicy/vendor
BOARD_PLAT_PRIVATE_SEPOLICY_DIR += $(COMMON_PATH)/sepolicy/private
SELINUX_IGNORE_NEVERALLOWS := true

# Bootanimation
TARGET_SCREEN_WIDTH := 720
TARGET_SCREEN_HEIGHT := 1280

# Vendor security patch level - J710MNUBS4CTF2
VENDOR_SECURITY_PATCH := 2020-03-01

# Vendor separation
TARGET_COPY_OUT_VENDOR := system/vendor

# Verity
# Only needed for signing
BOARD_AVB_ENABLE := false

ALLOW_MISSING_DEPENDENCIES := true

BOARD_VNDK_VERSION := current
BOARD_PROPERTY_OVERRIDES_SPLIT_ENABLED := true

# AUDIO
USE_XML_AUDIO_POLICY_CONF := 1

# LineageHW
BOARD_HARDWARE_CLASS += $(COMMON_PATH)/lineagehw
