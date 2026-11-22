#ifndef YTMCACHE_H
#define YTMCACHE_H

#include <stdbool.h>
#include <stddef.h>

#include "src/system/streaming/ytm.h"

// YouTube Music tracks downloaded to the card and then played like any other file.
//
// The twin of qobuzcache.h, and the reasons for downloading instead of decoding
// on the fly are the same: decoder_open() wants a path, seeking inside a track
// requires going backwards, and with the file on disk the queue, the equalizer,
// ReplayGain, chapters and the progress bar work with no extra code. See
// qobuzcache.h for the full reasoning.
//
// Separate directory, and not for tidiness: the two services number tracks
// independently, and nothing prevents track 12345678 existing on both. In a
// single directory the second would find the first's file, decide it already
// had it and play the wrong song -- a silent failure of the kind that looks
// like witchcraft. Distinct directories make that impossible by construction,
// and give qobuzcache_owns()/ytmcache_owns() a clean answer about who owns a
// path: that is how the player knows which badge to draw and which account to
// send the star to.
//
// What is here and not in Qobuz: segments. Qobuz answers with a URL and one
// file is downloaded. YouTube Music, in hi-res, answers with a list of MP4 segments to
// reassemble. That work lives in mp4flac.c; here is the loop that requests them
// one by one and feeds them in. From outside nothing changes: a growing .flac
// comes out, and whatever plays it neither knows nor needs to know where it
// came from.

#define YTMCACHE_DIR ".local/ytm-cache"

// The same ceiling as Qobuz. The two count separately, so in the worst case --
// using both on the same day -- they take twice as much. That is deliberate:
// they are two transient caches that empty themselves, and a shared budget
// would mean listening to YouTube Music deletes what was being listened to on Qobuz.
#define YTMCACHE_MAX_BYTES (1024L * 1024L * 1024L)

// Where the card is. Once at startup and on every card change; NULL or empty
// disables the cache (and with it YouTube Music, which cannot play anything without
// it).
void ytmcache_set_root(const char *sd_root);
bool ytmcache_ready(void);

// The path this track would have. Says nothing about whether it exists.
void ytmcache_path(long track_id, const char *mime, char *out, size_t size);

// True if the track has been downloaded in full.
bool ytmcache_has(long track_id, const char *mime, char *out, size_t size);

// Like ytmcache_has, but without knowing the format: tries every extension
// this cache may have used. Needed by callers that queue a track before asking
// YouTube Music for it -- the name is guessed from the selected quality, and if YouTube Music
// sent a different container the file is still there under another name. It
// matters more here than on Qobuz: which container arrives depends on the
// manifest, and the manifest is only seen at play time.
bool ytmcache_find(long track_id, char *out, size_t size);

// Starts downloading the track described by `stream` and puts the local path in
// `out`.
//
// Returns as soon as there is enough material for the decoder to open the file;
// the rest keeps downloading on its own thread, and the caller can hand the
// path to playback immediately. A track already in the cache returns
// immediately without touching the network.
//
// Blocks for the duration of the prebuffer, so call it from a worker thread,
// never from the GUI thread.
//
// `duration_secs` is the duration according to the API: it gives the bytes per
// second playback will consume, and hence how much must be in hand before
// starting. 0 when unknown.
//
// `stream` is copied: the caller can free its own as soon as this returns.
bool ytmcache_start(long track_id, const ytm_stream_t *stream, int duration_secs, char *out, size_t size);

// The extension this stream's file will have, without the dot. Gives the name
// before the download begins.
const char *ytmcache_extension(const ytm_stream_t *stream);

// Waits until no download is in progress. Only one runs at a time, for the same
// reason as Qobuz: a caller prefetching the next track calls this before
// starting, otherwise it competes for network and card with the track being
// listened to right now.
//
// The turn is shared with qobuzcache: it is the network and the card that are
// single, not the service. See streamturn.h.
void ytmcache_wait_idle(void);

// Called when the prebuffer will take more than a moment, with the estimate in
// seconds. Called from the download thread: the receiver must bounce to the GUI
// thread itself.
void ytmcache_set_slow_start_cb(void (*cb)(int seconds));

// Abandons downloads in progress. Call when switching to another track: the
// previous one is no longer needed and the bandwidth belongs to this one.
void ytmcache_abandon_all(void);

// The id of the track downloading right now, 0 if none.
long ytmcache_downloading_id(void);

// "YouTube Music needs the network", even when nothing is downloading at this instant.
// Needed by the Wi-Fi parking logic (power.c): see the long comment on
// qobuzcache_set_network_wanted().
void ytmcache_set_network_wanted(bool wanted);
bool ytmcache_network_wanted(void);

// True if `path` is inside the YouTube Music cache.
bool ytmcache_owns(const char *path);

// The cache directory, or NULL with no card.
const char *ytmcache_dir(void);

// Writes beside the track what the API knows and the file does not: title,
// artist, album (in `<file>.tags`) and the cover (in `<stem>.jpg`).
//
// This matters more than on Qobuz, not less: a FLAC stitched back from DASH
// segments has no tags at all, because the tags live in the metadata blocks the
// MP4 does not carry. Without this sidecar the player would show
// "12345678.flac" for every hi-res track.
void ytmcache_write_sidecars(long track_id, const char *mime, const char *title, const char *artist,
							   const char *album, const char *album_id, int track_number, const char *cover_url);

// The YouTube Music album id the track belongs to, read back from the sidecar. False if
// the path is not from this cache or the sidecar does not say. Used by "Show
// album" in the player.
bool ytmcache_album_id(const char *path, char *out, size_t size);

// Why the last ytmcache_start() failed, as a sentence fit to display. Empty
// on success. Per thread, like http_last_error().
const char *ytmcache_last_error(void);

// The YouTube Music track id from the local path, 0 if that path is not a track of this
// cache. The file name is the id ("12345678.flac"): a playing file always maps
// back to the YouTube Music track, which is what lets the player's favourite star and
// playlists reach the account, and the YouTube Music badge appear on the cover.
long ytmcache_track_id(const char *path);

// How many tracks between automatic cache sweeps.
#define YTMCACHE_CLEAR_EVERY 10

// Call for every track that enters the cache, with its path.
void ytmcache_note_played(const char *path);

#define YTMCACHE_PROTECTED_MAX 64

// The tracks the queue expects to play: the current one and those after it. The
// automatic sweep skips them. Call from the GUI thread.
void ytmcache_set_protected(const char *const *paths, int count);

// Call on the way out (shutdown, reboot).
void ytmcache_clear_on_exit(void);

// Current size, and how to empty it.
long ytmcache_bytes(void);
void ytmcache_clear(void);

#endif /* YTMCACHE_H */
