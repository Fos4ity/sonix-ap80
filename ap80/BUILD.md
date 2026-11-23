# Building Sonix for the Hidizs AP80 Pro Max

Everything here is built from source. You supply the firmware file yourself.
If you only want to install, use the ready-made files on the
[Releases](https://github.com/Fos4ity/sonix-ap80/releases) page instead.

## 1. The player

```
cd sonix-player
make -j8 target        # -> ./sonix_player (MIPS, Ingenic X1600)
```

The first run builds the cross compiler with Rockbox's `rockboxdev.sh`
(binutils 2.38, gcc 9.5.0, glibc 2.27) and static copies of FreeType and the audio
libraries. It downloads source archives and takes a long time. Later builds are quick.

Packages needed on Debian / Ubuntu, besides the ones in the original
[README](../README.sonix.md):

```
sudo apt install libtool-bin texinfo libgmp-dev libmpfr-dev libmpc-dev \
                 gtk-doc-tools gettext autopoint libglib2.0-dev-bin gawk bison flex
```

Two warnings are expected and harmless: `format-truncation` in `theme.c` and `fonts.c`.

## 2. The SD card

```
./ap80/make_sdcard.sh            # -> ap80/out/
```

The card ends up like this:

```
sonix.sh                         started by the boot menu
.rockbox/                        only a note; Rockbox goes here if you install it
sonix/
├── sonix_player                 the program
├── device_config.ini            Sonix's settings (written by Sonix, not in the package)
├── sonix_player.log             Sonix's log, overwritten at every start
└── resource/
    ├── components/              system-info.json says "Hidizs AP80 Pro Max"
    ├── fonts/  fonts/custom/    Sonix's fonts, and the extra ones
    ├── icons/                   icon packs
    ├── themes/                  skins (*.theme)
    ├── language/  gui/
    └── cacert.pem               root certificates for HTTPS
```

On the AP80 Pro Max Sonix runs beside HiBy OS, which it recognises by
`/usr/bin/hiby_player` still being installed. In that case it keeps its settings,
the reader's file and the streaming sessions in `sonix/` on the card instead of
`/usr/data`, does not clear HiBy OS's data on its first start, and its factory
reset removes only its own files. Settings an earlier build left in `/usr/data`
are copied to the card once; the originals are not changed.

Sonix writes its log to `sonix/sonix_player.log` (`sonix.sh` sets `SONIX_LOG`);
`sonix/sonix_log.txt` holds what the start script itself saw.

### Skins

A skin is a small text file in `sonix/resource/themes/`:

```
name = Nothing
screen = 0x000000
card = 0x101010
list_row = 0x1A1A1A
text_primary = 0xFFFFFF
text_muted = 0x8A8A8A
accent = 0xD71921        # optional
```

### Streaming keys

Tidal, Qobuz, podcasts and Last.fm need API keys, which are **not** in this
repository. See "Streaming keys" in the original [README](../README.sonix.md).
YouTube Music searches and plays without any key. To sign in to your account,
create your own OAuth client of type "TVs and Limited Input devices" in Google
Cloud and put it in `sonix/resource/components/streaming-keys.ini`:

```
[youtube]
client_id = ...
client_secret = ...
```

Never share that file or commit it.

## 3. The boot menu (optional)

Without this step Sonix starts from **TOOLS > Run script > sonix.sh**.
With it, the boot menu shows a fourth entry, SONIX.

The boot menu is Rockbox's hosted bootloader. Build it with our patch:

```
git clone https://github.com/Rockbox/rockbox.git
cd rockbox
git checkout c6593ee2540b585d04602854b0c632de759da459
git apply ../sonix-ap80/ap80/bootloader/rockbox-bootloader-ap80-sonix.patch
mkdir build-bl && cd build-bl
PATH=../../sonix-ap80/sonix-player/rockbox-toolchain/bin:$PATH \
    ../tools/configure --target=hidizsap80max --type=b
make -j4               # -> bootloader.ap80max
```

The patch:

- adds the SONIX entry, which runs `/data/mnt/sd_0/sonix.sh`,
- shows ROCKBOX and SONIX only when their files are on the SD card, so the
  player works with HiBy OS alone, with Sonix alone, or with all three,
- always gives you 10 seconds to choose before the last player starts again,
- remembers the choice in `sonix/` when the card has no `.rockbox` folder,
- keeps the TOOLS menu open until you choose Back,
- frees memory before HiBy OS starts (its display driver needs one 8 MB block
  and the player has 64 MB of RAM),
- ignores a stored choice that is out of range.

**Trying it before flashing** is possible but limited. Copy `bootloader.ap80max`
to the SD card as `bl_test` together with `ap80/bootloader/testbl.sh`, then run
*TOOLS > Run script > testbl.sh* in the existing boot menu. The new menu then runs
on top of the old one. This is good for looking at the menu, TOOLS and Sonix.
**Do not judge HiBy OS from this test:** started this way it can come up with a
black screen, because too little memory is left. Hold the power button to switch
off, and start HiBy OS normally.

**Put it into the firmware:**

```
sudo ./ap80/make_upt.sh ap80max.upt bootloader.ap80max
```

`ap80max.upt` is the file you already have: the Rockbox-patched one from
[rockbox.org](https://www.rockbox.org/wiki/HidizsPort), or the original from Hidizs.
The script replaces `/usr/bin/bootloader.rb` inside it and repacks it. Nothing else
changes. (`sudo` keeps file owners and device nodes intact when the image is unpacked.)

The script was checked by repacking a Rockbox-patched image: the result has the
same files as the input, with only the boot menu replaced. Starting from the
plain Hidizs original is less tested; installing Rockbox first is the safer way.

Copy the result to the SD card as `ap80max.upt` and install it in HiBy OS:
*System Settings > Firmware Update > via SD Card*. Afterwards remove the file from
the card so the update does not run a second time.

## Simulator on the PC

```
cd sonix-player
make -j8 host          # -> ./sonix_player_host (SDL2)
SONIX_PANEL=360x640 ./sonix_player_host
```

It looks for resources in `usr/resource/sonix` under the current folder.
`SONIX_SD_ROOT=<folder with Music/>` stands in for the SD card.
