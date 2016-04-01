# inherit from common tenderloin
-include device/hp/tenderloin-common/BoardConfigCommon.mk

# MBM support
BOARD_USES_MBM_RIL := true
BOARD_USES_MBM_GPS := true
BOARD_GPS_LIBRARIES := gps.$(TARGET_BOOTLOADER_BOARD_NAME)
USE_QEMU_GPS_HARDWARE := false
# MBM RIL does not currently support CELL_INFO_LIST commands
BOARD_RIL_NO_CELLINFOLIST := true

# Define Prebuilt kernel locations
TARGET_PREBUILT_KERNEL := device/hp/tenderloin-common/prebuilt/boot/kernel

TARGET_KERNEL_CONFIG := tenderloin4g_android_defconfig

# To allow use of the touchpad recovery
TARGET_OTA_ASSERT_DEVICE := tenderloin