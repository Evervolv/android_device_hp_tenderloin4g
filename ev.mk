# Inherit AOSP device configuration for inc.
$(call inherit-product, device/hp/tenderloin/full_tenderloin.mk)

# Inherit some common evervolv stuff.
$(call inherit-product, vendor/ev/config/common_full_tablet_wifionly.mk)

#
# Setup device specific product configuration.
#
PRODUCT_NAME := ev_tenderloin
PRODUCT_BRAND := HP
PRODUCT_DEVICE := tenderloin
PRODUCT_MODEL := Touchpad
PRODUCT_MANUFACTURER := HP
PRODUCT_BUILD_PROP_OVERRIDES += \
    PRODUCT_NAME=touchpad \
    BUILD_FINGERPRINT="google/mantaray/manta:4.2.1/JOP40C/527662:user/release-keys" \
    PRIVATE_BUILD_DESC="manta-user 4.2.1 JOP40C 527662 release-keys"

# Set up the product codename, build version & MOTD.
PRODUCT_CODENAME := Queo
PRODUCT_VERSION_DEVICE_SPECIFIC := p1

PRODUCT_MOTD :="\n\n\n--------------------MESSAGE---------------------\nThank you for choosing Evervolv for your HP Touchpad\nPlease visit us at \#evervolv on irc.freenode.net\nFollow @preludedrew for the latest Evervolv updates\nGet the latest rom at evervolv.com\n------------------------------------------------\n"

#PRODUCT_PACKAGES += \
#    Camera

# Hot reboot
PRODUCT_PACKAGE_OVERLAYS += \
    vendor/ev/overlay/hot_reboot

# Copy compatible prebuilt files
PRODUCT_COPY_FILES +=  \
    vendor/ev/prebuilt/xga/media/bootanimation.zip:system/media/bootanimation.zip

# USB
#ADDITIONAL_DEFAULT_PROPERTIES += persist.sys.usb.config=mass_storage,adb
#ADDITIONAL_DEFAULT_PROPERTIES += persist.sys.usb.config=mtp,adb
