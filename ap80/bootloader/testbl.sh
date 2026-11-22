#!/bin/sh
# Tries a freshly built boot menu without flashing anything:  #
# copy bootloader.ap80max to the SD card as bl_test, then  #
# TOOLS -> Run script -> testbl.sh in the existing boot menu.  #
cp /data/mnt/sd_0/bl_test /tmp/bl_test || exit 3 #
chmod +x /tmp/bl_test #
/tmp/bl_test #
exit 0 #
