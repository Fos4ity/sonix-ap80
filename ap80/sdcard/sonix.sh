#!/bin/sh
# Starts Sonix Player from the SD card on the Hidizs AP80 Pro Max.  #
# Run by the boot menu entry SONIX (or TOOLS -> Run script -> sonix.sh).  #
# Every line ends with a # so the script still works with Windows line ends.  #
SD=/data/mnt/sd_0 #
LOG=$SD/sonix/sonix_log.txt #
echo "start $(date)" > $LOG #
/usr/bin/bt_suspend >> $LOG 2>&1 #
killall bluealsa bt-agent bluetoothd >> $LOG 2>&1 #
ulimit -s 1024 #
export MALLOC_ARENA_MAX=1 #
# Sonix's own log, written on every start (overwritten each time).  #
export SONIX_LOG=$SD/sonix/sonix_player.log #
chmod +x $SD/sonix/sonix_player 2>/dev/null #
cd /tmp #
$SD/sonix/sonix_player >> $LOG 2>&1 #
echo "sonix exited with code $?" >> $LOG #
sync #
exit 0 #
