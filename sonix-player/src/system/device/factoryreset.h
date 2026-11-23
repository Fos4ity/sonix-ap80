#ifndef FACTORYRESET_H
#define FACTORYRESET_H

#include <stdbool.h>

// Factory reset.
//
// Nothing is erased on the spot, and this is not a shortcut: it is the stock
// firmware's own mechanism, which leaves the real work to a rootfs component
// that runs at boot. See factoryreset.c for why, and for the non-obvious reason
// it is the only safe way to do it.
//
// Does not return: it reboots.
void factoryreset_run(void);

// Whether the rootfs component that does the erasing is actually present. When
// it is missing the reset degrades to clearing this player's own configuration,
// which is worth saying in the log rather than glossing over.
bool factoryreset_supported(void);

// On the first start after the player is installed over the stock firmware,
// removes what the stock firmware left in /usr/data -- its settings, the
// Bluetooth pairings, the Wi-Fi networks, the daemons' state -- as a factory
// reset would, without restarting. Some of it keeps Bluetooth and Wi-Fi from
// working. The card's mount point, the radios' addresses and this player's own
// files stay. `first_start` is whether this player's configuration file was
// missing; nothing happens unless it was and the stock player's settings file
// is there. Call before the card is mounted and before either radio starts.
void factoryreset_clear_stock_data(bool first_start);

// Whether this player runs beside the stock firmware instead of replacing it:
// started from the SD card by a boot menu (Hidizs AP80 Pro Max), with the stock
// player still installed. Then the stock firmware's data in /usr/data is not
// ours to touch: no first-start erase, no stock factory reset, and our own
// settings live on the card next to the program, not in the player's memory.
bool factoryreset_beside_stock(void);

#endif /* FACTORYRESET_H */
