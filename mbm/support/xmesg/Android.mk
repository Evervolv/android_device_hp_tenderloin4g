# Copyright (C) ST-Ericsson AB 2008-2014
# Copyright 2006 The Android Open Source Project
#
# Based on reference-ril
# Modified for ST-Ericsson U300 modems.
# Author: Christian Bejram <christian.bejram@stericsson.com>
#
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= xmesg.c

LOCAL_C_INCLUDES := $(KERNEL_HEADERS)

# Disable prelink, or add to build/core/prelink-linux-arm.map
LOCAL_PRELINK_MODULE := false

LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS += -Wall

LOCAL_MODULE:= xmesg

include $(BUILD_EXECUTABLE)
