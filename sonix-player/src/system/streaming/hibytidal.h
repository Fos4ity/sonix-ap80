#ifndef HIBYTIDAL_H
#define HIBYTIDAL_H

#include <stdbool.h>
#include <stddef.h>

// The Tidal client secret, read from the stock HiBy player on this device.
//
// Where Sonix runs beside the stock firmware (Hidizs AP80 Pro Max, from the
// card), the stock player is still installed as /usr/bin/hiby_player, and its
// Tidal client secret is inside it as plain text: a single 44-character
// base64 string, the only one of its kind in the whole binary, sitting beside
// the "client_secret" JSON key the player parses its own tokens with. This
// looks it up there, at startup, when no streaming-keys file provides one.
// Nothing is copied anywhere: the value is read from the owner's own firmware
// each time the player starts, stays in memory, and is never written to the
// card or the log. It is not in this source either -- only the way to find it
// is.
//
// The client id the secret pairs with is NOT read from the binary: it is
// assembled byte by byte at run time and never appears as a string. But it is
// also not a secret -- it is the public identifier every auth request carries
// in plain text (and public third-party Tidal clients use the very same one),
// so it can simply live here:
#define HIBYTIDAL_CLIENT_ID "fX2JxdmntZWK0ixT"
//
// The pairing was verified on a real device (2026-11-24): device login at
// link.tidal.com plus playback works with the stock player's own secret and
// this public id.
//
// SONIX_HIBY_BIN names another file to read, for the simulator (same variable
// hibykeys uses).

#define HIBYTIDAL_BIN "/usr/bin/hiby_player"

// False when the file is not there, or carries no unique candidate (another
// firmware version may lay things out differently, or predate Tidal's OAuth
// client entirely). `secret` is left empty then.
bool hibytidal_secret(char *secret, size_t secret_size);

#endif /* HIBYTIDAL_H */
