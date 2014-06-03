#!/bin/bash
#
KERNDIR=$KERNEL_BUILD_TOP
MYDIR=$ANDROID_BUILD_TOP/vendor_mbm/patches/03_Revised
#
old_dir=`pwd`
#####################################
cp $MYDIR/cdc.h $KERNEL_BUILD_TOP/include/linux/usb/cdc.h
cp $MYDIR/cdc-acm.c $KERNEL_BUILD_TOP/drivers/usb/class/cdc-acm.c
cp $MYDIR/cdc-acm.h $KERNEL_BUILD_TOP/drivers/usb/class/cdc-acm.h
cp $MYDIR/cdc-wdm.c $KERNEL_BUILD_TOP/drivers/usb/class/cdc-wdm.c
cp $MYDIR/cdc-wdm.h $KERNEL_BUILD_TOP/include/linux/usb/cdc-wdm.h
cp $MYDIR/cdc_ncm.c $KERNEL_BUILD_TOP/drivers/net/usb/cdc_ncm.c
cp $MYDIR/usbnet.c $KERNEL_BUILD_TOP/drivers/net/usb/usbnet.c
cp $MYDIR/usbnet.h $KERNEL_BUILD_TOP/include/linux/usb/usbnet.h
#####################################
cd $old_dir
exit
