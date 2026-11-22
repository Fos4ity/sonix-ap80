#ifndef YTM_H
#define YTM_H

#include <stdbool.h>
#include <stddef.h>

// YouTube Music.
//
// Same shape as qobuz.h -- search, browse, ask for a track's address, write to
// the account -- so both pages behave the same way. Underneath, three
// differences explain every odd choice in here.
//
// One: there is no username and password.
//
// Qobuz still accepts a direct login; YouTube Music closed that to third-party clients
// years ago. What is left is the OAuth2 device code flow, the same one
// televisions use: the player asks for a code, shows it on screen, and the user
// opens link.ytm.com on a phone and types it in. Meanwhile the player polls
// every two seconds to see whether it has been authorized. It needs a second
// device, but it is the only path available and the password never passes
// through here. On a device with no keyboard, typing five characters on a phone
// beats spelling out a password with the wheel.
//
// Two: the token expires and has to be renewed.
//
// The Qobuz token lasts until revoked. The YouTube Music one lasts a week, and comes
// with a second token (the refresh token) used to get a new one without redoing
// the whole flow. Renewal happens automatically when a request returns 401,
// invisible to both the caller and the user.
//
// Three: a track's address may not be an address.
//
// Qobuz answers with a URL and that is that. YouTube Music answers with a base64-encoded
// manifest holding one of two things:
//
//   * BTS (application/vnd.ytm.bts) -- JSON containing a single address: one
//     file, downloadable and playable as is. This is what LOW, HIGH and LOSSLESS
//     return;
//   * DASH (application/dash+xml) -- an MPD, a list of segments to download and
//     reassemble. This is what hi-res returns.
//
// Which one arrives cannot be predicted from the requested quality: it also
// depends on the application keys in use. Never guess -- read
// `manifestMimeType` and decide afterwards. ytm_stream_t below carries both
// cases, and ytmcache.c can download either.
//
// The application credentials (client_id, client_secret) are not in here: they
// are read from a file on the device, see streamkeys.h. YouTube Music revokes them from
// time to time, so when login starts failing that file is the first thing to
// check, not this code.
//
// Every function that talks to the network blocks. Call them from a worker
// thread, never from the GUI thread -- same rule as http.h, radio.c and
// qobuz.c.

// ---------------------------------------------------------------------------
// Quality
// ---------------------------------------------------------------------------

// Without Premium YouTube Music hands out two audio streams worth having on
// this player: itag 140, AAC-LC 128 kbps in a fragmented MP4 (rewritten as
// ADTS, see mp4adts.h), and itag 251, Opus around 130-160 kbps in WebM
// (rewritten as Ogg Opus, see webmogg.h). Opus sounds better at the same rate
// and is the default; when a track has no 251 the other one is taken.
typedef enum {
	YTM_QUALITY_AAC128 = 0, // itag 140, AAC-LC ~128 kbps
	YTM_QUALITY_OPUS160 = 1, // itag 251, Opus ~130-160 kbps VBR
} ytm_quality_t;

int ytm_get_quality(void);
void ytm_set_quality(int quality);
const char *ytm_quality_name(int quality);

// Always "audio/aac": the cache rewrites the fragmented MP4 into plain ADTS,
// which the AAC decoder plays directly.
const char *ytm_expected_mime(void);

// ---------------------------------------------------------------------------
// What the lists show
// ---------------------------------------------------------------------------

#define YTM_TITLE_MAX 200
#define YTM_NAME_MAX 128
#define YTM_URL_MAX 2048 // stream addresses: googlevideo URLs run past a kilobyte
#define YTM_COVER_MAX 512
#define YTM_ID_MAX 64

typedef struct {
	long id;
	char title[YTM_TITLE_MAX];
	char artist[YTM_NAME_MAX];
	char album[YTM_TITLE_MAX];
	// Text rather than a number, for the same reason as Qobuz: both pages then
	// pass the album identifier to the same functions.
	char album_id[YTM_ID_MAX];
	char cover[YTM_COVER_MAX]; // already built, see ytm_cover_url()
	int duration;			   // seconds
	int track_number;
	bool hires;
	bool streamable; // false = present but not playable on this subscription
	int bit_depth;
	int sample_rate; // Hz
} ytm_track_t;

typedef struct {
	long id;
	char id_text[YTM_ID_MAX];
	char title[YTM_TITLE_MAX];
	char artist[YTM_NAME_MAX];
	char cover[YTM_COVER_MAX];
	char released[16]; // year only: that is all a row shows
	int track_count;
	bool hires;
} ytm_album_t;

typedef struct {
	long id;
	char name[YTM_NAME_MAX];
	char image[YTM_COVER_MAX];
	int album_count;
} ytm_artist_t;

typedef struct {
	// The only thing in all of YouTube Music without a numeric identifier: playlists are
	// addressed by UUID ("854e65fb-857a-4db3-..."), hence the string here and in
	// the functions that take it.
	char id[YTM_ID_MAX];
	char name[YTM_TITLE_MAX];
	char owner[YTM_NAME_MAX];
	char image[YTM_COVER_MAX];
	int track_count;
} ytm_playlist_t;

// ---------------------------------------------------------------------------
// Identifiers
//
// YouTube names a song by an 11-character video id and an artist by a channel
// id, but the page, the cache and the sync code (all born from Tidal's) carry
// tracks and artists as a `long`. Rather than rewrite all of it, the string is
// hashed to a positive 31-bit number (FNV-1a), which is stable across restarts
// -- a cache file "123456.aac" means the same song tomorrow -- and the string
// is remembered so the number can be turned back into the id it came from.
// ---------------------------------------------------------------------------

long ytm_id_for(const char *key);
// The string behind a number seen earlier, false when unknown.
bool ytm_key_for(long id, char *out, size_t size);

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

// Reads the application credentials and the stored tokens. Once at startup,
// after config_init() and streamkeys_init().
void ytm_init(void);

// False when the keys file is missing: the service is not broken, it is off,
// and the menu entry has to say so instead of trying and failing.
bool ytm_configured(void);

bool ytm_logged_in(void);

// True when an OAuth client is in streaming-keys.ini, so signing in is possible.
bool ytm_can_sign_in(void);
const char *ytm_display_name(void); // empty when not logged in

// Forgets the tokens, both here and in the configuration.
void ytm_logout(void);

// Why the last call failed, already in a form fit to display. Empty on success.
// Per thread, like http_last_error().
const char *ytm_last_error(void);

// --- device code login -----------------------------------------------------

// What is shown on screen while waiting.
typedef struct {
	char user_code[16];			 // the five characters to type
	char verification_url[256];	 // "link.ytm.com" -- already prefixed with https://
	char device_code[128];		 // internal: only ytm_login_poll() needs it
	int interval_secs;			 // how often YouTube Music wants to be polled
	int expires_secs;			 // how long the code stays valid
} ytm_login_t;

typedef enum {
	YTM_LOGIN_PENDING = 0, // nobody has typed the code yet: poll again
	YTM_LOGIN_OK,			 // logged in; tokens already stored
	YTM_LOGIN_EXPIRED,	 // the code expired: ask for another
	YTM_LOGIN_ERROR,		 // ytm_last_error() says what went wrong
} ytm_login_state_t;

// Asks YouTube Music for a code. Blocks. False on error.
bool ytm_login_begin(ytm_login_t *out);

// Asks whether anyone has authorized that code. Blocks (one request), and must
// not be called more often than `interval_secs`. On YTM_LOGIN_OK the tokens
// are already written to the configuration and ytm_logged_in() is true.
ytm_login_state_t ytm_login_poll(const ytm_login_t *login);

// ---------------------------------------------------------------------------
// Requests. Each returns how many items it wrote, or -1 on error
// (`ytm_last_error()` says what happened). All of them block.
// ---------------------------------------------------------------------------

int ytm_search_tracks(const char *query, int offset, ytm_track_t *out, int max);
int ytm_search_albums(const char *query, int offset, ytm_album_t *out, int max);
int ytm_search_artists(const char *query, int offset, ytm_artist_t *out, int max);

// The contents of a single item are paged too: a three-hundred-track playlist
// does not fit in one response, and neither does a prolific artist. `offset`
// says which entry to start from, as for searches.
int ytm_album_tracks(const char *album_id, int offset, ytm_track_t *out, int max);
int ytm_artist_albums(long artist_id, int offset, ytm_album_t *out, int max);
int ytm_playlist_tracks(const char *playlist_id, int offset, ytm_track_t *out, int max);

int ytm_favorite_tracks(int offset, ytm_track_t *out, int max);
int ytm_favorite_albums(int offset, ytm_album_t *out, int max);
int ytm_favorite_artists(int offset, ytm_artist_t *out, int max);
int ytm_user_playlists(int offset, ytm_playlist_t *out, int max);

// The collections YouTube Music features. `type` is one of the local short names, not
// one of YouTube Music's: "new", "top", "recommended". See the table in ytm.c for how
// they become URLs.
int ytm_featured_albums(const char *type, int offset, ytm_album_t *out, int max);

// ---------------------------------------------------------------------------
// Where the file is
// ---------------------------------------------------------------------------

typedef enum {
	// A single file at `url`. Download and play, like Qobuz.
	YTM_STREAM_FILE = 0,

	// Segments to reassemble: first `init_url`, then `media_template` with
	// $Number$ replaced, from start_number to start_number+count-1.
	YTM_STREAM_DASH,
} ytm_stream_kind_t;

typedef struct {
	ytm_stream_kind_t kind;

	char mime[64];	  // "audio/flac", "audio/mp4"...
	char codecs[24];  // "flac", "mp4a.40.2"...
	int sample_rate;  // Hz, 0 when not declared
	int bit_depth;	  // 16 or 24, 0 when not declared

	// YTM_STREAM_FILE
	char url[YTM_URL_MAX];
	long content_length; // bytes, from the player response; 0 when unknown
	char user_agent[200]; // the client the URL was issued to: googlevideo checks it

	// YTM_STREAM_DASH
	char init_url[YTM_URL_MAX];
	char media_template[YTM_URL_MAX]; // still contains "$Number$"
	int start_number;
	int segment_count;
} ytm_stream_t;

// Asks where the track is. Blocks. False on error.
//
// The struct is large (nearly three kilobytes of URLs: signed DASH ones run past
// five hundred characters each). The caller allocates it, and it must not go on
// the stack of a small thread.
bool ytm_track_stream(long track_id, ytm_stream_t *out);

// The URL of a DASH segment: `media_template` with $Number$ substituted. False
// when it does not fit.
bool ytm_dash_segment_url(const ytm_stream_t *stream, int number, char *out, size_t size);

// ---------------------------------------------------------------------------
// Cover art
// ---------------------------------------------------------------------------

// YouTube Music does not send image URLs: it sends a UUID, and the URL is built by
// replacing the dashes with slashes. Valid sizes for album covers are 80, 160,
// 320, 640 and 1280; artist photos take different ones (160, 320, 480, 750) and
// answer 404 for 80 or 1280.
void ytm_cover_url(const char *uuid, int size_px, char *out, size_t out_size);

// The size currently being requested. Used by whoever writes the cover cache
// file: the size is part of the name, so changing it yields a new file instead
// of keeping the old one forever.
int ytm_cover_px(void);
void ytm_artist_image_url(const char *uuid, int size_px, char *out, size_t out_size);

// ---------------------------------------------------------------------------
// Writing to the account
//
// Same reason as Qobuz: for a YouTube Music track, the player's star and "add to
// playlist" must land here and not in the local database -- the cache file
// being played is transient, and a favorite pointing at a path that will
// disappear is not a favorite.
// ---------------------------------------------------------------------------

bool ytm_favorite_add_track(long track_id);
bool ytm_favorite_remove_track(long track_id);

// The identifiers of the favorite tracks, so the star can be drawn correctly:
// one request for the whole list instead of one per track. Returns how many it
// wrote, or -1.
int ytm_favorite_track_ids(long *out, int max);

bool ytm_favorite_add_album(const char *album_id);
bool ytm_favorite_remove_album(const char *album_id);

bool ytm_playlist_delete(const char *playlist_id);
bool ytm_playlist_create(const char *name, char *out_id, size_t out_size);
bool ytm_playlist_add_track(const char *playlist_id, long track_id);

#endif /* YTM_H */
