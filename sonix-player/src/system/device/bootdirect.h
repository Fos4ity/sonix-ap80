#ifndef BOOTDIRECT_H
#define BOOTDIRECT_H

#include <stdbool.h>

// "Boot straight into Sonix", for a player where Sonix runs from the card
// beside the stock firmware and is started by the boot menu (Hidizs AP80 Pro
// Max).
//
// The switch is a file on the card, <card>/sonix/boot_direct.txt. The boot
// menu looks for it at power-on: while it is there it starts Sonix at once
// instead of showing the menu. Holding a button other than Power while the
// player starts brings the menu back, and so does deleting the file from a
// computer -- which is why it is a file on the card and not a line in the
// settings, where a Sonix that no longer starts could not be reached.
//
// Only a boot menu that knows the file acts on it (ap80/bootloader, version 5
// and later). An older one ignores it and nothing changes.

#define BOOTDIRECT_FILE "boot_direct.txt"

// True where the switch makes sense at all: Sonix on the card beside the stock
// firmware.
bool bootdirect_applies(void);

// True when the installed boot menu reads the file. Looked up once.
bool bootdirect_supported(void);

bool bootdirect_enabled(void);

// Creates or removes the file. False when the card refused.
bool bootdirect_set(bool on);

#endif /* BOOTDIRECT_H */
