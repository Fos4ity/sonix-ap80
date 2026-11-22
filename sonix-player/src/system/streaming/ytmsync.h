#ifndef YTMSYNC_H
#define YTMSYNC_H

#include <stdbool.h>
#include <stddef.h>

#include "src/system/streaming/ytm.h"

// What the player writes back to the YouTube Music account, off the UI thread.
//
// The twin of qobuzsync.h, same reasons and same contract. The star and "add to
// playlist" are one tap each, and for a YouTube Music track they have to land on the
// account rather than in the local database: the stored path would be a cache
// file that is gone twenty tracks later, and no other device of the user's
// would see the favourite.
//
// An HTTP request takes seconds and the UI thread cannot stall, so this holds
// one worker with a single-slot queue, and every job ends in a callback. That
// callback runs on the worker, not on the UI thread -- the receiver must bounce
// it itself with gui_post(), as the rest of the program does.
//
// The star also cannot wait for the network to know how to draw itself, so a
// mirror of the favourite track ids is kept in memory, filled once and updated
// on every tap.
//
// The only real difference from Qobuz is that YouTube Music playlists are named by UUID
// rather than by number, so they pass through here as strings.

// --- the favourites mirror -------------------------------------------------

bool ytmsync_favorites_known(void);
bool ytmsync_is_favorite(long track_id);
void ytmsync_forget(void);
void ytmsync_favorites_refresh(void (*done)(void));

// --- the jobs --------------------------------------------------------------

typedef void (*ytmsync_done_cb)(bool ok, const char *error, void *user);

// Adds or removes the track from the account's favourites. The mirror is
// updated immediately, before the network, so the star responds to the tap, and
// put back if the request fails.
void ytmsync_favorite_toggle(long track_id, bool want, ytmsync_done_cb done, void *user);

void ytmsync_playlist_add(const char *playlist_id, long track_id, ytmsync_done_cb done, void *user);
void ytmsync_playlist_create_with(const char *name, long track_id, ytmsync_done_cb done, void *user);

// Adds or removes an album from the favourites. Albums have no mirror like
// tracks do: in the lists the menu already states which way it will go (add
// among search results, remove in the favourites list), so the answer is known
// without asking.
void ytmsync_album_favorite(const char *album_id, bool want, ytmsync_done_cb done, void *user);

void ytmsync_playlist_remove(const char *playlist_id, ytmsync_done_cb done, void *user);

// --- the account's playlists -----------------------------------------------

#define YTMSYNC_PLAYLISTS_MAX 60

void ytmsync_playlists_refresh(void (*done)(void));
int ytmsync_playlists(ytm_playlist_t *out, int max);
bool ytmsync_playlists_known(void);

#endif /* YTMSYNC_H */
