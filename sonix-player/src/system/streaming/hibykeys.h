#ifndef HIBYKEYS_H
#define HIBYKEYS_H

#include <stdbool.h>
#include <stddef.h>

// The Qobuz application keys, read from the stock HiBy player on this device.
//
// Where Sonix runs beside the stock firmware (Hidizs AP80 Pro Max, from the
// card), the stock player is still installed as /usr/bin/hiby_player, and the
// pair of keys Qobuz asks every application for is inside it as plain text,
// beside the text of its own login request. This looks them up there, at
// startup, when no streaming-keys file provides them. Nothing is copied
// anywhere: the values are read from the owner's own firmware each time the
// player starts, stay in memory, and are never written to the card or the log.
// They are not in this source either -- only the way to find them is.
//
// Tidal's keys cannot be had this way: the stock player assembles them at run
// time and they are nowhere in the file as text.
//
// SONIX_HIBY_BIN names another file to read, for the simulator.

#define HIBYKEYS_BIN "/usr/bin/hiby_player"

// False when the file is not there or the keys are not where they are expected
// (another firmware version may lay them out differently). `id` and `secret`
// are left empty then.
bool hibykeys_qobuz(char *id, size_t id_size, char *secret, size_t secret_size);

#endif /* HIBYKEYS_H */
