#ifndef SONIXPKG_H
#define SONIXPKG_H

#include <stdbool.h>
#include <stddef.h>

// Sonix updating itself on a player where it runs from the microSD card beside
// the stock firmware (Hidizs AP80 Pro Max, started by the boot menu).
//
// There the "firmware" the update card offers is not the HiBy OS image -- that
// is flashed by hand, never from here -- but the SD card package published on
// the project's GitHub releases: a zip with `sonix.sh` and the `sonix/` folder.
// ota.c finds and downloads it (tag "ap80-v<version>", asset
// "sonix-ap80-sdcard.zip", SHA-256 from GitHub's asset digest), and this file
// puts it on the card:
//
//   1. every file of the package is compared with the one on the card (size and
//      CRC-32); only the ones that differ are unpacked, beside the old one, as
//      "<name>.new". A failure here removes those .new files and leaves the
//      card exactly as it was.
//   2. then each old file is MOVED (not deleted) into
//      "<card>/sonix-zaloha-<old version>/", keeping its path, and its .new
//      takes its place. A list of what changed goes into that folder.
//
// Files that are not in the package -- the settings (device_config.ini), the
// reader's file, sessions, logs, anything the user added -- are never touched.
// Nothing on the card is deleted; the old files can be put back by hand.

// The version of the SD card package this binary belongs to. The release tag
// is "ap80-v" followed by it; bump it together with the tag.
#define SONIXPKG_VERSION "1.2"

#define SONIXPKG_REPO "Fos4ity/sonix-ap80"
#define SONIXPKG_TAG_PREFIX "ap80-v"
#define SONIXPKG_ASSET "sonix-ap80-sdcard.zip"

// True when Sonix runs from its folder on the card beside the stock firmware:
// updates then mean this package, and the firmware-from-card path is closed.
// SONIX_UPDATE_ROOT=<folder holding sonix/ and sonix.sh> turns it on for
// testing (the host build included).
bool sonixpkg_active(void);

// The card root (the folder holding sonix/ and sonix.sh); "" when not active.
const char *sonixpkg_root(void);

// The package version installed: SONIXPKG_VERSION, or SONIX_UPDATE_FAKE_VERSION
// from the environment, for testing an update to the current release.
const char *sonixpkg_version(void);

typedef enum {
	PKG_IDLE = 0,
	PKG_RUNNING,
	PKG_FINISHED,
} pkg_state_t;

typedef enum {
	PKG_OK = 0,
	PKG_NOT_PACKAGE, // the zip is damaged or has no sonix/sonix_player
	PKG_UNPACK_FAILED, // a file could not be unpacked; the card is unchanged
	PKG_PARTIAL, // moving files failed halfway; see the backup folder
} pkg_result_t;

// Installs the package in `zip_path` on a worker thread. False when an install
// is already running.
bool sonixpkg_install_start(const char *zip_path);

pkg_state_t sonixpkg_state(void);
pkg_result_t sonixpkg_result(void);
void sonixpkg_acknowledge(void);

// Files looked at so far and in total, for the progress bar.
void sonixpkg_progress(int *done, int *total);

// After PKG_OK: how many files were replaced or added, and the backup folder's
// name (relative to the card root; "" when nothing needed replacing).
int sonixpkg_changed(void);
const char *sonixpkg_backup_name(void);

// Synchronous install, for the worker and for tests. Fills `changed` and the
// backup name.
pkg_result_t sonixpkg_install(const char *zip_path);

#endif /* SONIXPKG_H */
