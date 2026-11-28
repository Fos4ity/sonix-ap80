#!/bin/bash
# Puts together everything that goes on the SD card of the AP80 Pro Max.
#
#   ./ap80/make_sdcard.sh [output_dir]        (default: ./ap80/out)
#
# Run it from the root of the repository after `make target` in sonix-player/.
# Copy the CONTENTS of the output folder to the root of the SD card.
# Licensed under the GNU GPL v3 or later, like the rest of Sonix Player.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"
out="${1:-$here/out}"
bin="$repo/sonix-player/sonix_player"

[ -f "$bin" ] || { echo "Build the player first: cd sonix-player && make -j8 target" >&2; exit 1; }

mkdir -p "$out/sonix/resource"
cp "$here/sdcard/sonix.sh" "$out/sonix.sh"
cp "$bin" "$out/sonix/sonix_player"
# Sonix's own resources (fonts, language, gui, components) ...
cp -r "$repo/sonix-packer/assets/R3PII/usr/resource/sonix/." "$out/sonix/resource/"
# ... then the AP80 extras on top (system-info.json, custom fonts, icon packs, skins)
cp -r "$here/sdcard/sonix-resource-overlay/." "$out/sonix/resource/"

# A note in .rockbox: where Rockbox goes, and why the folder alone does not add
# ROCKBOX to the boot menu. The menu keeps its last choice there, or in sonix/.
mkdir -p "$out/.rockbox"
cp "$here/sdcard/rockbox-folder-note.txt" "$out/.rockbox/sonix-ap80-note.txt"
cp "$repo/LICENSE" "$out/sonix/LICENSE.txt"

# Root certificates for HTTPS (AutoEq, streaming, YouTube Music)
if [ ! -f "$out/sonix/resource/cacert.pem" ]; then
    curl -fsSL -o "$out/sonix/resource/cacert.pem" https://curl.se/ca/cacert.pem \
        || echo "Could not download cacert.pem. Put Mozilla's CA bundle at sonix/resource/cacert.pem yourself." >&2
fi

# The release asset, zipped with Python: names like Čeština.ini get the UTF-8
# flag, so Windows' own unzip and the updater in Sonix read them right.
# (Info-ZIP on Linux stores the bytes without the flag; Windows then garbles them.)
python3 - "$out" "$out/../sonix-ap80-sdcard.zip" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1], os.path.abspath(sys.argv[2])
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for top in ("sonix.sh", "sonix", ".rockbox"):
        path = os.path.join(src, top)
        if os.path.isfile(path):
            z.write(path, top)
            continue
        for root, dirs, files in os.walk(path):
            dirs.sort()
            for name in sorted(files):
                full = os.path.join(root, name)
                z.write(full, os.path.relpath(full, src).replace(os.sep, "/"))
bad = [i.filename for i in zipfile.ZipFile(dst).infolist()
       if not i.filename.isascii() and not i.flag_bits & 0x800]
if bad:
    sys.exit("names without the UTF-8 flag: %s" % bad)
# An update replaces every file that is in the package. A user's own settings,
# keys and sign-ins must therefore never be in it.
PRIVATE = ("device_config.ini", "ebook_config.ini", "tidal_session.ini",
           "ytmusic_session.ini", "lastfm.session", "listenbrainz.session",
           "audiobookshelf_profiles.ini", "streaming-keys.ini",
           "streaming-keys.bin", "streaming-keys.key", "boot_direct.txt",
           "rb_bl_mode.txt", "library.db")
private = [i.filename for i in zipfile.ZipFile(dst).infolist()
           if os.path.basename(i.filename) in PRIVATE
           or os.path.basename(i.filename).startswith("device_config.ini")
           or i.filename.endswith((".session", ".log"))]
if private:
    os.remove(dst)
    sys.exit("user files must not be in the package: %s" % private)
PY

echo "Done. Copy the contents of $out to the root of the SD card."
echo "Release asset: $(cd "$out/.." && pwd)/sonix-ap80-sdcard.zip"
