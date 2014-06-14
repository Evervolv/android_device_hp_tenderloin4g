# inherit from common tenderloin
-include device/hp/tenderloin-common/BoardConfigCommon.mk

TARGET_OTA_ASSERT_DEVICE := tenderloin

# ATH6KL uses hostapd built from source
BOARD_HOSTAPD_DRIVER := NL80211
BOARD_HOSTAPD_PRIVATE_LIB := lib_driver_cmd_ath6kl

# MBM support
BOARD_USES_MBM_RIL := true
BOARD_USES_MBM_GPS := true
BOARD_GPS_LIBRARIES := gps.$(TARGET_BOOTLOADER_BOARD_NAME)
USE_QEMU_GPS_HARDWARE := false
# MBM RIL does not currently support CELL_INFO_LIST commands
BOARD_RIL_NO_CELLINFOLIST := true

# Needed for blobs
COMMON_GLOBAL_CFLAGS += -DNEEDS_VECTORIMPL_SYMBOLS

TARGET_RECOVERY_FSTAB := device/hp/tenderloin/fstab.tenderloin

# Define Prebuilt kernel locations
TARGET_PREBUILT_KERNEL := device/hp/tenderloin-common/prebuilt/boot/kernel

TARGET_KERNEL_CONFIG := tenderloin4g_android_defconfig