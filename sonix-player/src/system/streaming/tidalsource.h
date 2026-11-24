#ifndef TIDALSOURCE_H
#define TIDALSOURCE_H

#include <stdbool.h>
#include <stddef.h>

#include "src/system/core/respath.h"

// The Tidal application keys, read from a source file the owner puts on the
// card.
//
// Tidal's keys are not in this project and not in the release. Several public
// projects carry a pair in their own source, as two lines of the form
//
//     CLIENT_ID = "..."
//     CLIENT_SECRET = "..."
//
// Whoever wants Tidal saves such a file, unchanged, to the card as
// tidal-source.py beside the other component files. This reads the two values
// out of it at startup, when no streaming-keys file provides them. Only the two
// quoted values are taken; the rest of the file is ignored and nothing in it is
// run. The values stay in memory and are never written out or logged.
//
// A [tidal] section in a streaming-keys file wins over this, so keys someone
// set by hand are never overridden.
//
// SONIX_TIDAL_SOURCE names another file to read, for the simulator.

#define TIDALSOURCE_PATH SONIX_RESOURCE_DIR "/components/tidal-source.py"

// False when the file is not there or does not hold both lines. `id` and
// `secret` are left empty then.
bool tidalsource_read(char *id, size_t id_size, char *secret, size_t secret_size);

#endif /* TIDALSOURCE_H */
