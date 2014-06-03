# inherit from common tenderloin
-include device/hp/tenderloin-common/BoardConfigCommon.mk

TARGET_OTA_ASSERT_DEVICE := tenderloin

# MBM support
BOARD_USES_MBM_GPS := true
BOARD_GPS_LIBRARIES := gps.$(TARGET_BOOTLOADER_BOARD_NAME)
USE_QEMU_GPS_HARDWARE := false

# Needed for blobs
COMMON_GLOBAL_CFLAGS += -DNEEDS_VECTORIMPL_SYMBOLS

TARGET_RECOVERY_FSTAB := device/hp/tenderloin/fstab.tenderloin

# Define Prebuilt kernel locations
TARGET_PREBUILT_KERNEL := device/hp/tenderloin-common/prebuilt/boot/kernel

TARGET_KERNEL_CONFIG := tenderloin4g_android_defconfig