#!/bin/bash
# Puts the Sonix boot menu into a Hidizs AP80 Pro Max firmware update file.
#
#   ./ap80/make_upt.sh ap80max.upt bootloader.ap80max [output.upt]
#
#   ap80max.upt         a firmware file YOU supply: the original one from
#                       Hidizs, or the Rockbox-patched one from rockbox.org
#   bootloader.ap80max  the boot menu you built from Rockbox with
#                       ap80/bootloader/rockbox-bootloader-ap80-sonix.patch
#
# Nothing else in the firmware is touched. No HiBy / Hidizs file is part of
# this repository: the script only repacks the file you give it.
#
# Linux:  sudo apt install p7zip-full squashfs-tools genisoimage coreutils
# macOS:  brew install p7zip squashfs cdrtools coreutils
#
# Based on tools/r1_patcher/r1_patcher.sh from Rockbox (https://www.rockbox.org),
# licensed under the GNU GPL v2 or later. This file keeps that licence.
set -euo pipefail
trap 'echo "ERROR at line ${LINENO}: ${BASH_COMMAND}" >&2' ERR

if [[ "$OSTYPE" == "darwin"* ]]; then
    realpath()    { grealpath "$@"; }
    stat()        { gstat "$@"; }
    split()       { gsplit "$@"; }
    md5sum()      { gmd5sum "$@"; }
    genisoimage() { mkisofs "$@"; }
fi
for cmd in 7z unsquashfs mksquashfs genisoimage md5sum split; do
    command -v "$cmd" >/dev/null || [[ "$(type -t "$cmd")" == function ]] \
        || { echo "Missing tool: $cmd (see the top of this script)" >&2; exit 1; }
done

if [[ $# -lt 2 || $# -gt 3 ]]; then
    sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 1
fi

upt_in="$(realpath "$1")"
bootloader="$(realpath "$2")"
upt_out="$(realpath -m "${3:-${1%.*}-sonix.upt}")"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

### unpack
7z x -y -o"$work/iso" "$upt_in" >/dev/null
[ -d "$work/iso/ota_v0" ] || { echo "$1 does not look like an AP80 Pro Max update (no ota_v0)" >&2; exit 1; }
cat "$work/iso/ota_v0"/xImage.[0-9]* > "$work/xImage"
cat "$work/iso/ota_v0"/rootfs.squashfs.[0-9]* > "$work/rootfs.squashfs"
# as root, so that owners and device nodes survive; fakeroot works too
unsquashfs -f -d "$work/rootfs" "$work/rootfs.squashfs" >/dev/null

### the boot menu
bin="$work/rootfs/usr/bin"
cp "$bootloader" "$bin/bootloader.rb"
chmod 0755 "$bin/bootloader.rb"

if ! grep -q bootloader.rb "$bin/hiby_player.sh" 2>/dev/null; then
    # Original firmware: start the boot menu instead of the HiBy player,
    # exactly as Rockbox's own patcher does.
    cat << 'EOS' > "$bin/hiby_player.sh"
#!/bin/sh

killall    hiby_player    &>/dev/null
killall -9 hiby_player    &>/dev/null

killall    bootloader.rb    &>/dev/null
killall -9 bootloader.rb    &>/dev/null

/usr/bin/bootloader.rb
sleep 1s
EOS
    chmod 0755 "$bin/hiby_player.sh"

    cat << 'EOS' > "$bin/rb_hotplug.sh"
#!/bin/sh
MNT_SD="/data/mnt/sd_0"
MNT_USB="/data/mnt/usb"

case "$MDEV" in
    mmcblk*) MNT="$MNT_SD" ;;
    sd*)     MNT="$MNT_USB" ;;
    *)       exit 0 ;;
esac

case "$ACTION" in
    add|"")
        mkdir -p "$MNT"
        mount -t auto -o sync,noatime "/dev/$MDEV" "$MNT"
        ;;
    remove)
        umount -l "$MNT"
        ;;
esac
EOS
    chmod 0755 "$bin/rb_hotplug.sh"

    conf="$work/rootfs/etc/mdev.conf"
    grep -q "mmcblk" "$conf"      || echo "mmcblk[0-9]p[0-9] 0:0 660 */usr/bin/rb_hotplug.sh" >> "$conf"
    grep -q "sd\[a-z\]" "$conf"   || echo "sd[a-z][0-9]      0:0 660 */usr/bin/rb_hotplug.sh" >> "$conf"
fi

### repack
mkdir -p "$work/out/ota_v0"
mksquashfs "$work/rootfs" "$work/new.squashfs" -comp lzo -all-root -noappend >/dev/null
cd "$work/out/ota_v0"

chunks() {  # chunks <file> <name>: 512 KiB pieces, each named after the md5 of the one before
    local file="$1" name="$2" md5 next part
    split -b 512k "$file" --numeric-suffixes=0 -a 4 "$name."
    md5=($(md5sum "$file"))
    local list="ota_md5_$name.$md5"
    for part in $(ls "$name".[0-9]* | sort); do
        next=($(md5sum "$part"))
        echo "$next" >> "$list"
        mv "$part" "$part.$md5"
        md5=$next
    done
}
chunks "$work/new.squashfs" rootfs.squashfs
chunks "$work/xImage" xImage

rootfs_md5=($(md5sum "$work/new.squashfs"))
ximage_md5=($(md5sum "$work/xImage"))
cat > ota_update.in << EOS
ota_version=0

img_type=kernel
img_name=xImage
img_size=$(stat -c%s "$work/xImage")
img_md5=$ximage_md5

img_type=rootfs
img_name=rootfs.squashfs
img_size=$(stat -c%s "$work/new.squashfs")
img_md5=$rootfs_md5

EOS
echo > ota_v0.ok
echo "current_version=0" > ../ota_config.in

genisoimage -quiet -f -U -J -joliet-long -r -allow-lowercase -allow-multidot \
    -input-charset utf-8 -o "$upt_out" "$work/out/"

echo "Created: $upt_out"
echo "md5:     $(md5sum "$upt_out" | cut -d' ' -f1)"
