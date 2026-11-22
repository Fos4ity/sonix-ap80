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

# The boot menu stores the last choice in .rockbox/rb_bl_mode.txt, so the folder
# has to exist even when Rockbox itself is not installed.
mkdir -p "$out/.rockbox"
cp "$here/sdcard/rockbox-folder-note.txt" "$out/.rockbox/sonix-ap80-note.txt"
cp "$repo/LICENSE" "$out/sonix/LICENSE.txt"

# Root certificates for HTTPS (AutoEq, streaming, YouTube Music)
if [ ! -f "$out/sonix/resource/cacert.pem" ]; then
    curl -fsSL -o "$out/sonix/resource/cacert.pem" https://curl.se/ca/cacert.pem \
        || echo "Could not download cacert.pem. Put Mozilla's CA bundle at sonix/resource/cacert.pem yourself." >&2
fi

echo "Done. Copy the contents of $out to the root of the SD card."
