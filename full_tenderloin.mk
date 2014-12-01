#
# Copyright (C) 2011 The Evervolv Project
# Copyright (C) 2011 The CyanogenMod Project
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

# Include tenderloin4g's BoardConfig.mk
include device/hp/tenderloin4g/BoardConfig.mk

# Proprietary stuff.
$(call inherit-product-if-exists, vendor/hp/tenderloin/tenderloin-vendor.mk)

# common tenderloin configs
$(call inherit-product, device/hp/tenderloin-common/tenderloin-common.mk)

# This is the hardware-specific overlay, which points to the location
# of hardware-specific resource overrides, typically the frameworks and
# application settings that are stored in resourced.

PRODUCT_COPY_FILES += \
    device/hp/tenderloin4g/init.tenderloin.rc:root/init.tenderloin.rc

DEVICE_PACKAGE_OVERLAYS += device/hp/tenderloin4g/overlay


# Custom init files.
PRODUCT_COPY_FILES += \
    device/hp/tenderloin4g/init.tenderloin.usb.rc:root/init.tenderloin.usb.rc \
    device/hp/tenderloin4g/ueventd.tenderloin.rc:root/ueventd.tenderloin.rc \
    device/hp/tenderloin4g/prebuilt/boot/moboot.splash.Evervolv.tga:moboot.splash.Evervolv.tga \
    device/hp/tenderloin4g/prebuilt/boot/moboot.default:moboot.default

PRODUCT_COPY_FILES += \
    device/hp/tenderloin4g/prebuilt/wifi/hostapd.conf:system/etc/wifi/hostapd.conf \
    device/hp/tenderloin4g/prebuilt/wifi/udhcpd.conf:system/etc/wifi/udhcpd.conf \
    device/hp/tenderloin4g/makemulti.sh:makemulti.sh \

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/tablet_core_hardware.xml:system/etc/permissions/tablet_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:system/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.camera.front.xml:system/etc/permissions/android.hardware.camera.front.xml \
    frameworks/native/data/etc/android.hardware.camera.autofocus.xml:system/etc/permissions/android.hardware.camera.autofocus.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:system/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.hardware.sensor.light.xml:system/etc/permissions/android.hardware.sensor.light.xml \
    frameworks/native/data/etc/android.hardware.sensor.gyroscope.xml:system/etc/permissions/android.hardware.sensor.gyroscope.xml \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:system/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:system/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:system/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.software.sip.voip.xml:system/etc/permissions/android.software.sip.voip.xml

# MBM
PRODUCT_PACKAGES += \
    gps.tenderloin \
    MbmService \
    libmbm-ril \
    Mms

PRODUCT_COPY_FILES += \
    device/hp/tenderloin4g/aldtf.sh:system/xbin/aldtf.sh \
    device/hp/tenderloin4g/xmesg:system/bin/xmesg \
    device/hp/tenderloin4g/pollerr.sh:system/bin/pollerr.sh \
    device/hp/tenderloin4g/gps.conf:system/etc/gps.conf

$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)
$(call inherit-product, frameworks/native/build/tablet-dalvik-heap.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/languages_full.mk)
