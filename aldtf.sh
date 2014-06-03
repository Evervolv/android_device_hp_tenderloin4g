#/bin/bash
if [ ! -d /sdcard/logs ]; then
  mkdir /sdcard/logs
  echo "Created log directory"
fi
now="$(date +'%Y%m%d-%H%M')"
radiolog="/sdcard/logs/radio-$now.log"
mainlog="/sdcard/logs/main-$now.log"
eventlog="/sdcard/logs/event-$now.log"
systemlog="/sdcard/logs/system-$now.log"
klog1="/sdcard/logs/klog-$now.log"
klog2="/sdcard/logs/klog2-$now.log"
klog3="/sdcard/logs/klog3-$now.log"
compfile="/sdcard/logs/$now-logs.tar.gz"
wildcard="sdcard/logs/*-$now.log"
echo "Dumping kernel log"
dmesg > /sdcard/logs/dmesg-$now.log
echo "Dumping special kernel log"
xmesg > /sdcard/logs/xmesg-$now.log
echo "Dumping system log"
logcat -b system -v threadtime -d -f $systemlog *:v
echo "Dumping main log"
logcat -b main -v threadtime -d -f $mainlog *:v
echo "Dumping event log"
logcat -b events -v threadtime -d -f $eventlog *:v
echo "Dumping radio log"
logcat -b radio -v threadtime -d -f $radiolog *:v
echo "Dumping property log"
getprop > /sdcard/logs/property-$now.log
if [ -e /proc/last_klog ]; then
  echo "Dumping last_klog"
  cp /proc/last_klog $klog1
fi
if [ -e /proc/last_klog2 ]; then
  echo "Dumping last_klog2"
  cp /proc/last_klog2 $klog2
fi
if [ -e /proc/last_klog3 ]; then
  echo "Dumping last_klog3"
  cp /proc/last_klog3 $klog3
fi
echo "Compressing and deleting"
tar -czf $compfile -C / $wildcard
rm /$wildcard
echo "Finished"
