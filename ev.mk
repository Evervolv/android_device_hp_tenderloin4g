# Inherit AOSP device configuration for inc.
$(call inherit-product, device/hp/tenderloin4g/full_tenderloin.mk)

# Inherit some common evervolv stuff.
$(call inherit-product, vendor/ev/config/common_full_phone.mk)
$(call inherit-product, vendor/ev/config/apns.mk)

#
# Setup device specific product configuration.
#
PRODUCT_NAME := ev_tenderloin4g
PRODUCT_BRAND := HP
PRODUCT_DEVICE := tenderloin4g
PRODUCT_MODEL := Touchpad4g
PRODUCT_RELEASE_NAME := touchpad4g
PRODUCT_MANUFACTURER := HP
PRODUCT_BUILD_PROP_OVERRIDES += \
    PRODUCT_NAME=touchpad \
    BUILD_FINGERPRINT="hp/hp_tenderloin/tenderloin:6.0/MRA58K/2256973:user/release-keys" \
    PRIVATE_BUILD_DESC="tenderloin-user 6.0 MRA58K 2256973 release-keys" \
    BUILD_NUMBER=228551

# Set up the product codename, build version & MOTD.
PRODUCT_CODENAME := Quae
PRODUCT_VERSION_DEVICE_SPECIFIC := p1

PRODUCT_MOTD :="\n\n\n--------------------MESSAGE---------------------\nThank you for choosing Evervolv for your HP Touchpad4G\nPlease visit us at \#evervolv on irc.freenode.net\nFollow @preludedrew for the latest Evervolv updates\nGet the latest rom at evervolv.com\n------------------------------------------------\n"

# Bootanimation
TARGET_BOOTANIMATION_HALF_RES := true
BOOT_ANIMATION_SIZE := xga

