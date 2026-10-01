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

# -----------------------------------------------------------
# 1. Vendor Blobs & Base Platform
# -----------------------------------------------------------
$(call inherit-product, vendor/samsung/universal7870-common/universal7870-common-vendor.mk)

# Set Dalvik heap configuration for low RAM devices
$(call inherit-product, frameworks/native/build/phone-xhdpi-2048-dalvik-heap.mk)

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(COMMON_PATH) \
    hardware/samsung

# -----------------------------------------------------------
# 2. VINTF (Project Treble Manifests)
# -----------------------------------------------------------
DEVICE_MANIFEST_FILE += $(COMMON_PATH)/manifest.xml
DEVICE_MATRIX_FILE += $(COMMON_PATH)/compatibility_matrix.xml

# -----------------------------------------------------------
# 3. HIDL Services & Hardware Packages
# -----------------------------------------------------------

# Audio
PRODUCT_PACKAGES += \
    android.hardware.audio@2.0-impl \
    android.hardware.audio@2.0-service \
    android.hardware.audio.effect@2.0-impl \
    audio.primary.universal7870_go \
    audio.r_submix.default \
    audio.a2dp.default \
    audio.usb.default \
    libtinyalsa \
    libtinycompress

# Bluetooth
PRODUCT_PACKAGES += \
    android.hardware.bluetooth@1.0-impl \
    android.hardware.bluetooth@1.0-service

# Camera
PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.4-impl \
    android.hardware.camera.provider@2.4-service \
    libcsc \
    libhwjpeg \
    libexynoscamera \
    libexynoscamera3 \
    camera.exynos5

# ConfigStore
PRODUCT_PACKAGES += \
    android.hardware.configstore@1.0-service

# DRM
PRODUCT_PACKAGES += \
    android.hardware.drm@1.0-impl \
    android.hardware.drm@1.0-service \
    android.hardware.drm@1.0-service.clearkey \
    android.hardware.drm@1.0-service.widevine

# Gatekeeper 
PRODUCT_PACKAGES += \
    android.hardware.gatekeeper@1.0-service \
    android.hardware.gatekeeper@1.0-impl 

# GNSS
PRODUCT_PACKAGES += \
    android.hardware.gnss@1.0-impl \
    android.hardware.gnss@1.0-service

# Graphics & Display
PRODUCT_PACKAGES += \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.allocator@2.0-service \
    android.hardware.graphics.composer@2.1-impl \
    android.hardware.graphics.composer@2.1-service \
    android.hardware.graphics.mapper@2.0-impl \
    gralloc.exynos5 \
    hwcomposer.exynos5 \
    vulkan.exynos5

# Health
PRODUCT_PACKAGES += \
    android.hardware.health@1.0-impl \
    android.hardware.health@1.0-service

# Keymaster
PRODUCT_PACKAGES += \
    android.hardware.keymaster@3.0-impl \
    android.hardware.keymaster@3.0-service 

# Lights
PRODUCT_PACKAGES += \
    android.hardware.light@2.0-impl \
    android.hardware.light@2.0-service \
    lights.exynos5

# Media / OMX 
PRODUCT_PACKAGES += \
    android.hardware.media.omx@1.0-impl \
    android.hardware.media.omx@1.0-service \
    libExynosOMX_Core \
    libExynosOMX_Resourcemanager \
    libOMX.Exynos.AVC.Decoder \
    libOMX.Exynos.AVC.Encoder \
    libOMX.Exynos.HEVC.Decoder \
    libOMX.Exynos.HEVC.Encoder \
    libOMX.Exynos.MPEG4.Decoder \
    libOMX.Exynos.MPEG4.Encoder \
    libOMX.Exynos.VP8.Decoder \
    libOMX.Exynos.VP8.Encoder \
    libOMX.Exynos.WMV.Decoder

# Memtrack
PRODUCT_PACKAGES += \
    android.hardware.memtrack@1.0-impl \
    android.hardware.memtrack@1.0-service \
    memtrack.exynos5

# Power
PRODUCT_PACKAGES += \
    android.hardware.power@1.0-impl \
    android.hardware.power@1.0-service \
    power.universal7870_go

# RIL
PRODUCT_PACKAGES += \
    android.hardware.radio@1.0 \
    android.hardware.radio@1.1 \
    android.hardware.radio.deprecated@1.0 \
    libxml2 \
    rild

# Sensors
PRODUCT_PACKAGES += \
    android.hardware.sensors@1.0-impl \
    android.hardware.sensors@1.0-service \
    sensors.universal7870_go

# Thermal
PRODUCT_PACKAGES += \
    android.hardware.thermal@1.0-impl \
    android.hardware.thermal@1.0-service

# Vibrator
PRODUCT_PACKAGES += \
    android.hardware.vibrator@1.0-impl \
    android.hardware.vibrator@1.0-service

# WiFi
PRODUCT_PACKAGES += \
    android.hardware.wifi@1.0 \
    android.hardware.wifi@1.0-service \
    android.hardware.wifi@1.1 \
    android.hardware.wifi@1.2 \
    android.hardware.wifi.supplicant@1.0 \
    android.hardware.wifi.supplicant@1.1 \
    hostapd \
    libwpa_client \
    wificond \
    wpa_supplicant \
    wpa_supplicant.conf

# -----------------------------------------------------------
# 4. Configs, XMLs y Scripts (.rc)
# -----------------------------------------------------------

# Audio XMLs
PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/audio/SoundBoosterParam.txt:$(TARGET_COPY_OUT_VENDOR)/etc/SoundBoosterParam.txt \
    $(COMMON_PATH)/configs/audio/audio_effects_common.conf:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects_common.conf \
    $(COMMON_PATH)/configs/audio/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    $(COMMON_PATH)/configs/audio/audio_policy_configuration_sec.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration_sec.xml \
    $(COMMON_PATH)/configs/audio/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml \
    $(COMMON_PATH)/configs/audio/a2dp_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/a2dp_audio_policy_configuration.xml \
    $(COMMON_PATH)/configs/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    $(COMMON_PATH)/configs/audio/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    $(COMMON_PATH)/configs/audio/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml

# Media Codecs
PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/media/media_codecs.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs.xml \
    $(COMMON_PATH)/configs/media/media_codecs_performance.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_performance.xml \
    $(COMMON_PATH)/configs/media/media_profiles_V1_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_profiles_V1_0.xml \
    $(COMMON_PATH)/configs/media/media_codecs_google_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_audio.xml \
    $(COMMON_PATH)/configs/media/media_codecs_google_telephony.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_telephony.xml \
    $(COMMON_PATH)/configs/media/media_codecs_google_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_video.xml \
    $(COMMON_PATH)/configs/media/media_codecs_google_video_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_video_le.xml


# Keylayouts
PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/keylayout/gpio-keys.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/gpio-keys.kl \
    $(COMMON_PATH)/configs/keylayout/sec_touchkey.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/sec_touchkey.kl

# WiFi Overlays
#PRODUCT_COPY_FILES += \
#    $(COMMON_PATH)/configs/wifi/p2p_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/p2p_supplicant_overlay.conf \
#    $(COMMON_PATH)/configs/wifi/wpa_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant_overlay.conf

# Seccomp Policy
PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/seccomp/mediacodec-seccomp.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/mediacodec.policy

# Public Libraries
PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/public.libraries.txt:$(TARGET_COPY_OUT_VENDOR)/etc/public.libraries.txt

# Permissions
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.audio.low_latency.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.low_latency.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.camera.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.xml \
    frameworks/native/data/etc/android.hardware.camera.flash-autofocus.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.flash-autofocus.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.opengles.aep.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.opengles.aep.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.vulkan.level-1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.level.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_0_3.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.version.xml \
    frameworks/native/data/etc/android.hardware.sensor.accelerometer.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.accelerometer.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.proximity.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.sensor.proximity.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/handheld_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/handheld_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.telephony.cdma.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.cdma.xml \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml

# -----------------------------------------------------------
# 5. Boot / Ramdisk / Init Scripts
# -----------------------------------------------------------
PRODUCT_PACKAGES += \
    fstab.samsungexynos7870 \
    init.baseband.rc \
    init.power.rc \
    init.samsung.rc \
    init.samsungexynos7870.rc \
    init.samsungexynos7870.usb.rc \
    init.recovery.samsungexynos7870.rc \
    init.vendor.rilchip.rc \
    init.vendor.rilcommon.rc \
    init.wifi.rc \
    init.gps.rc \
    ueventd.samsungexynos7870.rc \
    init.mobicore.rc \
    cbd 

# Offmode charger
PRODUCT_PACKAGES += \
    charger_res_images \
    lineage_charger_res_image

# -----------------------------------------------------------
# 6. Device Overrides
# -----------------------------------------------------------
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xhdpi
PRODUCT_CHARACTERISTICS := default
DEVICE_PACKAGE_OVERLAYS += $(COMMON_PATH)/overlay

# Zygote fix for older Mali binaries
PRODUCT_PROPERTY_OVERRIDES += ro.zygote.disable_gl_preload=1
