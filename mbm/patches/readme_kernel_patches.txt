================================================================================
Linux 2.6.35 to 3.0.26 CDC_NCM, CDC_ACM, CDC_WDM, and USBNET kernel patches
================================================================================
These kernel drivers should work for Linux 2.6.35 to 3.0.26.  Drivers from Linux
2.6.39+ are inclued along with patches to make them compatible with the range of 
Linux kernels supported.  The patches also include a correction in the ncm driver 
for a known firmware issue in the F5521gw module which causes a hard data-stall.  

It is up to the integrator to make sure everything works as intended.

'copy_revised_giles.sh' is provided to assist with intallation.  You will need 
to set paths to the top of your build directories before running the script:

  export $KERNEL_BUILD_TOP=~/your_kernel_top
  export $ANDROID_BUILD_TOP=~/your_android_top

--------------------------------------------------------------------------------
CDC and USBNET driver files from Linux 2.6.39+ 
--------------------------------------------------------------------------------
  include/linux/usb/cdc.h (2.6.39.4)
  drivers/usb/class/cdc-acm.c (2.6.39.4)
  drivers/usb/class/cdc-acm.h (2.6.39.4)
  drivers/usb/class/cdc-wdm.c (next-2012_03-09)
  include/linux/usb/cdc-wdm.h (next-2012_03-09)
  drivers/net/usb/cdc_ncm.c (next-2012-03-17)
  drivers/net/usb/usbnet.c (next-2012_03_24)
  include/linux/usb/usbnet.h (next-2012_03_24)

--------------------------------------------------------------------------------
Patches for Linux 2.6.35 kernel compatibility and Ericsson F5521gw module
--------------------------------------------------------------------------------
  cdc-acm (v0.26-mbm_2) [cdc-acm.patch]
    submit INT URB during probe
    added ABSTRACT state
    corrected multiple write during suspend state
    flush any encapsulated command
    allocated the input request
    added extra debug print for open port
      
  cdc-wdm (v0.03-mbm) [cdc-wdm.patch]
    fixes MBM select and proper packet size
    modifed low-level module function
          
  cdc_ncm (14-Mar-2012-mbm) [cdc_ncm.patch / cdc_ncm_fix_ifc_error_count.patch]
    start with one bulk transfer to ensure sync
    ensure next URB is 512 bytes to capture spurious pakets
    discard spurious 512 byte packets to prevent stalls
    set truesize for TCP Sliding Window
    enable autosuspend & remotewakeup on the device
    modifed low-level module function
      
  usbnet (22-Aug-2005-mbm) [usbnet.c.patch / usbnet.h.patch}
    add driver hook for transmit timestamping
    support suspend and urb size checking
