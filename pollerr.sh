#!/system/bin/sh
c=1
while [ $c -le 5 ]
do
    logcat -b radio -d > /mnt/asec/test.txt
    grep /mnt/asec/test.txt -q -i -e "POLLERR"
    if [ $? -eq 0 ]
    then
        sleep 30
        radiol="`busybox date +%Y-%m-%d-%H-%M-%S`-radio.log"
        sysl="`busybox date +%Y-%m-%d-%H-%M-%S`-sys.log"
        xmesgl="`busybox date +%Y-%m-%d-%H-%M-%S`-xmesg.log"
        dmesgl="`busybox date +%Y-%m-%d-%H-%M-%S`-dmesg.log"
        logcat -v time -b radio -d -f /sdcard/logs/$radiol
        logcat -v time -d -f /sdcard/logs/$sysl
        uname -a > /sdcard/logs/$xmesgl
        uname -a > /sdcard/logs/$dmesgl
        xmesg >> /sdcard/logs/$xmesgl
        dmesg -c >> /sdcard/logs/$dmesgl
        logcat -b radio -c
        logcat -c
    fi
    sleep 30
done

