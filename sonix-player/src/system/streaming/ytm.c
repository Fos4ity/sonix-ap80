#include "ytm.h"

#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "src/system/core/config.h"
#include "src/system/core/json.h"
#include "src/system/core/lang.h"
#include "src/system/net/http.h"
#include "src/system/streaming/streamkeys.h"

// YouTube Music, spoken to the way its own clients do: InnerTube, the JSON API
// behind music.youtube.com and the apps (youtubei/v1/...).
//
// Two kinds of request, two client identities:
//
//   * catalogue (search, browse): WEB_REMIX, the music.youtube.com web client.
//     Its answers are the "renderer" trees the website draws from. Anonymous
//     works for everything except the account's own library; signed in, an
//     OAuth bearer token rides along.
//   * player (where is the audio): a client whose stream URLs are plain --
//     no signature cipher, no 'n' parameter to run through YouTube's
//     JavaScript, no PO token, no SABR. Which clients still qualify changes
//     every few months; the list below follows what the Morphe patches
//     (github.com/MorpheApp/morphe-patches, ClientType.java) use, and the
//     order can be changed with [ytmusic] player_clients without a rebuild.
//
// Everything here blocks: call from worker threads only.

#define YTM_TIMEOUT_SECS 20
#define YTM_BODY_LIMIT (6 * 1024 * 1024) // a search answer is a few hundred KB

#define YTM_MUSIC_API "https://music.youtube.com/youtubei/v1/"
#define YTM_PLAYER_API "https://youtubei.googleapis.com/youtubei/v1/player?prettyPrint=false"
#define YTM_OAUTH_DEVICE "https://oauth2.googleapis.com/device/code"
#define YTM_OAUTH_TOKEN "https://oauth2.googleapis.com/token"
#define YTM_OAUTH_SCOPE "https://www.googleapis.com/auth/youtube"

#define WEB_REMIX_VERSION_DEFAULT "1.20251110.03.00"
#define WEB_REMIX_UA                                                                                           \
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/142.0.0.0 "      \
	"Safari/537.36"

#define COVER_PX_DEFAULT 320
#define TOKEN_MAX 2048
#define TOKEN_MARGIN_SECS 120

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

static char web_remix_version[48];
static char hl[8] = "en";
static char gl[8] = "US";
static char player_clients[128];
static int cover_px = COVER_PX_DEFAULT;

static char client_id[128];
static char client_secret[128];
static char access_token[TOKEN_MAX];
static char refresh_token[TOKEN_MAX];
static long token_expires_at;
static char display_name[YTM_NAME_MAX];
static pthread_mutex_t token_lock = PTHREAD_MUTEX_INITIALIZER;

static __thread char last_error[256];

// The visitor id YouTube hands every anonymous client (responseContext.
// visitorData). A player request without one is far more likely to meet "Sign
// in to confirm you're not a bot"; with one it looks like the same visitor
// that just searched. Taken from every catalogue answer, renewed on refusal.
static char visitor_id[256];
static pthread_mutex_t visitor_lock = PTHREAD_MUTEX_INITIALIZER;

const char *ytm_last_error(void) { return last_error; }

static void set_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_error(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vsnprintf(last_error, sizeof(last_error), fmt, args);
	va_end(args);
}

static void clear_error(void) { last_error[0] = '\0'; }

// ---------------------------------------------------------------------------
// quality
// ---------------------------------------------------------------------------

static int quality = YTM_QUALITY_OPUS160;

int ytm_get_quality(void) { return quality; }
void ytm_set_quality(int q) {
	if (q != YTM_QUALITY_AAC128 && q != YTM_QUALITY_OPUS160) {
		return;
	}
	quality = q;
	config_set_int("ytmusic", "quality", quality);
	config_save();
}
const char *ytm_quality_name(int q) { return q == YTM_QUALITY_AAC128 ? "AAC 128" : "Opus 160"; }
const char *ytm_expected_mime(void) { return quality == YTM_QUALITY_AAC128 ? "audio/aac" : "audio/opus"; }

// ---------------------------------------------------------------------------
// identifiers
// ---------------------------------------------------------------------------

#define REGISTRY_SIZE 8192 // power of two

typedef struct {
	long id;
	char key[YTM_ID_MAX];
} reg_entry_t;

static reg_entry_t *registry;
static pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;

static long fnv31(const char *s) {
	uint32_t h = 2166136261u;
	for (; *s; s++) {
		h ^= (uint8_t)*s;
		h *= 16777619u;
	}
	h &= 0x7FFFFFFF;
	return h ? (long)h : 1; // 0 means "none" everywhere in the page code
}

long ytm_id_for(const char *key) {
	if (!key || !key[0]) {
		return 0;
	}
	long id = fnv31(key);
	pthread_mutex_lock(&registry_lock);
	if (!registry) {
		registry = calloc(REGISTRY_SIZE, sizeof(*registry));
	}
	if (registry) {
		unsigned slot = (unsigned)id & (REGISTRY_SIZE - 1);
		for (int probe = 0; probe < 16; probe++) {
			reg_entry_t *e = &registry[(slot + probe) & (REGISTRY_SIZE - 1)];
			if (e->id == id && strcmp(e->key, key) == 0) {
				break;
			}
			if (e->id == 0 || probe == 15) {
				// Full neighbourhood: the oldest-looking slot is reused. The
				// cost of losing an entry is one unplayable row until relisted.
				e->id = id;
				snprintf(e->key, sizeof(e->key), "%s", key);
				break;
			}
		}
	}
	pthread_mutex_unlock(&registry_lock);
	return id;
}

bool ytm_key_for(long id, char *out, size_t size) {
	bool found = false;
	pthread_mutex_lock(&registry_lock);
	if (registry && id > 0) {
		unsigned slot = (unsigned)id & (REGISTRY_SIZE - 1);
		for (int probe = 0; probe < 16; probe++) {
			reg_entry_t *e = &registry[(slot + probe) & (REGISTRY_SIZE - 1)];
			if (e->id == id) {
				snprintf(out, size, "%s", e->key);
				found = true;
				break;
			}
			if (e->id == 0) {
				break;
			}
		}
	}
	pthread_mutex_unlock(&registry_lock);
	return found;
}

// ---------------------------------------------------------------------------
// session (only with the optional OAuth client)
// ---------------------------------------------------------------------------

#define SESSION_FILE "ytmusic_session.ini"

static void session_path(char *out, size_t size) {
	out[0] = '\0';
	const char *cfg = config_path();
	if (!cfg || !cfg[0]) {
		return;
	}
	const char *slash = strrchr(cfg, '/');
	if (slash) {
		snprintf(out, size, "%.*s/%s", (int)(slash - cfg), cfg, SESSION_FILE);
	} else {
		snprintf(out, size, "%s", SESSION_FILE);
	}
}

static void save_session(void) {
	char path[600], tmp[640];
	session_path(path, sizeof(path));
	if (!path[0]) {
		return;
	}
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		return;
	}
	fprintf(f, "access_token=%s\nrefresh_token=%s\nexpires_at=%ld\nuser=%s\n", access_token, refresh_token,
			token_expires_at, display_name);
	bool ok = fclose(f) == 0;
	if (!ok || rename(tmp, path) != 0) {
		remove(tmp);
	}
}

static void load_session(void) {
	char path[600];
	session_path(path, sizeof(path));
	FILE *f = path[0] ? fopen(path, "r") : NULL;
	if (!f) {
		return;
	}
	static char line[TOKEN_MAX + 64];
	while (fgets(line, sizeof(line), f)) {
		char *nl = strchr(line, '\n');
		if (nl) {
			*nl = '\0';
		}
		char *eq = strchr(line, '=');
		if (!eq) {
			continue;
		}
		*eq = '\0';
		const char *v = eq + 1;
		if (strcmp(line, "access_token") == 0) {
			snprintf(access_token, sizeof(access_token), "%s", v);
		} else if (strcmp(line, "refresh_token") == 0) {
			snprintf(refresh_token, sizeof(refresh_token), "%s", v);
		} else if (strcmp(line, "expires_at") == 0) {
			token_expires_at = strtol(v, NULL, 10);
		} else if (strcmp(line, "user") == 0) {
			snprintf(display_name, sizeof(display_name), "%s", v);
		}
	}
	fclose(f);
}

void ytm_init(void) {
	snprintf(web_remix_version, sizeof(web_remix_version), "%s",
			 config_get("ytmusic", "web_remix_version", WEB_REMIX_VERSION_DEFAULT));
	snprintf(hl, sizeof(hl), "%s", config_get("ytmusic", "hl", "en"));
	snprintf(gl, sizeof(gl), "%s", config_get("ytmusic", "gl", "CZ"));
	snprintf(player_clients, sizeof(player_clients), "%s",
			 config_get("ytmusic", "player_clients", "visionos,visionos_102,android_vr"));
	cover_px = (int)config_get_int("ytmusic", "cover_size", COVER_PX_DEFAULT);
	quality = (int)config_get_int("ytmusic", "quality", YTM_QUALITY_OPUS160) == YTM_QUALITY_AAC128 ? YTM_QUALITY_AAC128
																							   : YTM_QUALITY_OPUS160;
	if (cover_px < 60 || cover_px > 1200) {
		cover_px = COVER_PX_DEFAULT;
	}

	const char *id = streamkeys_youtube_client_id();
	const char *secret = streamkeys_youtube_client_secret();
	snprintf(client_id, sizeof(client_id), "%s", id ? id : "");
	snprintf(client_secret, sizeof(client_secret), "%s", secret ? secret : "");
	load_session();

	printf("ytmusic: %s, %s, player clients %s\n", client_id[0] ? "OAuth client present" : "anonymous only",
		   ytm_logged_in() ? "saved session" : "no session", player_clients);
}

// Always usable: searching and playing need no keys at all.
bool ytm_configured(void) { return true; }

bool ytm_can_sign_in(void) { return client_id[0] && client_secret[0]; }
static bool can_sign_in(void) { return ytm_can_sign_in(); }

bool ytm_logged_in(void) { return can_sign_in() && refresh_token[0]; }

const char *ytm_display_name(void) { return display_name; }

void ytm_logout(void) {
	pthread_mutex_lock(&token_lock);
	access_token[0] = refresh_token[0] = '\0';
	token_expires_at = 0;
	display_name[0] = '\0';
	save_session();
	pthread_mutex_unlock(&token_lock);
}

// ---------------------------------------------------------------------------
// small text helpers
// ---------------------------------------------------------------------------

// Appends `s` to a JSON string literal under construction, escaped.
static void json_escape(const char *s, char *out, size_t size) {
	size_t n = 0;
	for (; s && *s && n + 7 < size; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') {
			out[n++] = '\\';
			out[n++] = (char)c;
		} else if (c < 0x20) {
			n += (size_t)snprintf(out + n, size - n, "\\u%04x", c);
		} else {
			out[n++] = (char)c;
		}
	}
	out[n] = '\0';
}

static void form_escape(const char *s, char *out, size_t size) { http_url_encode(s, out, size); }

// ---------------------------------------------------------------------------
// OAuth (device flow), only when [youtube] keys exist
// ---------------------------------------------------------------------------

static bool post_form(const char *url, const char *form, char **out, int *status) {
	http_req_t req = {0};
	req.method = "POST";
	req.body = form;
	req.content_type = "application/x-www-form-urlencoded";
	req.want_error_body = true;
	req.allow_empty_body = true;
	size_t len = 0;
	return http_request(url, &req, out, &len, 64 * 1024, YTM_TIMEOUT_SECS, status);
}

static bool take_tokens(const json_doc_t *doc) {
	int root = json_root(doc);
	char at[TOKEN_MAX];
	if (!json_obj_str(doc, root, "access_token", at, sizeof(at)) || !at[0]) {
		return false;
	}
	snprintf(access_token, sizeof(access_token), "%s", at);
	char rt[TOKEN_MAX];
	if (json_obj_str(doc, root, "refresh_token", rt, sizeof(rt)) && rt[0]) {
		snprintf(refresh_token, sizeof(refresh_token), "%s", rt);
	}
	long expires = json_obj_long(doc, root, "expires_in", 3600);
	token_expires_at = (long)time(NULL) + expires;
	return true;
}

// Called with token_lock held.
static bool refresh_locked(void) {
	if (!refresh_token[0]) {
		return false;
	}
	char form[TOKEN_MAX * 3 + 512], rt[TOKEN_MAX * 3], id[384], secret[384];
	form_escape(refresh_token, rt, sizeof(rt));
	form_escape(client_id, id, sizeof(id));
	form_escape(client_secret, secret, sizeof(secret));
	snprintf(form, sizeof(form), "client_id=%s&client_secret=%s&refresh_token=%s&grant_type=refresh_token", id,
			 secret, rt);
	char *body = NULL;
	int status = 0;
	bool ok = post_form(YTM_OAUTH_TOKEN, form, &body, &status) && status == 200;
	if (ok) {
		json_doc_t doc;
		ok = body && json_parse(body, &doc);
		if (ok) {
			ok = take_tokens(&doc);
			json_free(&doc);
		}
	}
	free(body);
	if (ok) {
		save_session();
	}
	return ok;
}

// The Authorization header for a signed-in request, or "" when anonymous.
static void auth_header(char *out, size_t size) {
	out[0] = '\0';
	if (!ytm_logged_in()) {
		return;
	}
	pthread_mutex_lock(&token_lock);
	if (!access_token[0] || (long)time(NULL) + TOKEN_MARGIN_SECS >= token_expires_at) {
		if (!refresh_locked()) {
			printf("ytmusic: token refresh failed, going on anonymously\n");
		}
	}
	if (access_token[0]) {
		snprintf(out, size, "Authorization: Bearer %s\r\nX-Goog-Request-Time: %ld\r\n", access_token,
				 (long)time(NULL));
	}
	pthread_mutex_unlock(&token_lock);
}

bool ytm_login_begin(ytm_login_t *out) {
	clear_error();
	if (!can_sign_in()) {
		set_error("%s", tr("ytm_no_oauth_keys"));
		return false;
	}
	char form[1024], id[384], scope[256];
	form_escape(client_id, id, sizeof(id));
	form_escape(YTM_OAUTH_SCOPE, scope, sizeof(scope));
	snprintf(form, sizeof(form), "client_id=%s&scope=%s", id, scope);
	char *body = NULL;
	int status = 0;
	if (!post_form(YTM_OAUTH_DEVICE, form, &body, &status) || !body) {
		free(body);
		set_error("%s", tr("ytm_cannot_reach_ytm"));
		return false;
	}
	json_doc_t doc;
	bool ok = json_parse(body, &doc);
	if (ok) {
		int root = json_root(&doc);
		ok = json_obj_str(&doc, root, "user_code", out->user_code, sizeof(out->user_code)) &&
			 json_obj_str(&doc, root, "device_code", out->device_code, sizeof(out->device_code));
		char url[200] = "";
		json_obj_str(&doc, root, "verification_url", url, sizeof(url));
		snprintf(out->verification_url, sizeof(out->verification_url), "%s",
				 url[0] ? url : "https://www.google.com/device");
		out->interval_secs = (int)json_obj_long(&doc, root, "interval", 5);
		out->expires_secs = (int)json_obj_long(&doc, root, "expires_in", 1800);
		if (!ok) {
			char err[160] = "";
			if (!json_obj_str(&doc, root, "error_description", err, sizeof(err))) {
				json_obj_str(&doc, root, "error", err, sizeof(err));
			}
			set_error("%s", err[0] ? err : tr("ytm_sign_in_failed"));
		}
		json_free(&doc);
	} else {
		set_error("%s", tr("ytm_sign_in_failed"));
	}
	free(body);
	return ok;
}

ytm_login_state_t ytm_login_poll(const ytm_login_t *login) {
	clear_error();
	char form[2048], id[384], secret[384], dc[512];
	form_escape(client_id, id, sizeof(id));
	form_escape(client_secret, secret, sizeof(secret));
	form_escape(login->device_code, dc, sizeof(dc));
	snprintf(form, sizeof(form),
			 "client_id=%s&client_secret=%s&device_code=%s&grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%"
			 "3Adevice_code",
			 id, secret, dc);
	char *body = NULL;
	int status = 0;
	if (!post_form(YTM_OAUTH_TOKEN, form, &body, &status) || !body) {
		free(body);
		return YTM_LOGIN_PENDING; // a dropped poll is not a failed sign-in
	}
	ytm_login_state_t state = YTM_LOGIN_ERROR;
	json_doc_t doc;
	if (json_parse(body, &doc)) {
		int root = json_root(&doc);
		char err[64] = "";
		json_obj_str(&doc, root, "error", err, sizeof(err));
		if (strcmp(err, "authorization_pending") == 0 || strcmp(err, "slow_down") == 0) {
			state = YTM_LOGIN_PENDING;
		} else if (strcmp(err, "expired_token") == 0) {
			state = YTM_LOGIN_EXPIRED;
		} else if (!err[0]) {
			pthread_mutex_lock(&token_lock);
			if (take_tokens(&doc)) {
				snprintf(display_name, sizeof(display_name), "YouTube");
				save_session();
				state = YTM_LOGIN_OK;
			}
			pthread_mutex_unlock(&token_lock);
		} else {
			set_error("%s", err);
		}
		json_free(&doc);
	}
	free(body);
	if (state == YTM_LOGIN_ERROR && !last_error[0]) {
		set_error("%s", tr("ytm_sign_in_failed"));
	}
	return state;
}

// ---------------------------------------------------------------------------
// InnerTube requests
// ---------------------------------------------------------------------------

// POSTs `{"context":{...WEB_REMIX...}, <fields>}` to music.youtube.com.
// `fields` is the rest of the object body, without the braces, may be "".
// On success *doc is parsed and *text owns the response (free both).
static bool music_call(const char *endpoint, const char *query, const char *fields, char **text, json_doc_t *doc) {
	*text = NULL;
	char url[2600];
	snprintf(url, sizeof(url), YTM_MUSIC_API "%s?prettyPrint=false%s%s", endpoint, query && query[0] ? "&" : "",
			 query ? query : "");

	size_t body_size = strlen(fields ? fields : "") + 512;
	char *body = malloc(body_size);
	if (!body) {
		set_error("%s", tr("out_of_memory"));
		return false;
	}
	snprintf(body, body_size,
			 "{\"context\":{\"client\":{\"clientName\":\"WEB_REMIX\",\"clientVersion\":\"%s\",\"hl\":\"%s\",\"gl\":"
			 "\"%s\"},\"user\":{}}%s%s}",
			 web_remix_version, hl, gl, fields && fields[0] ? "," : "", fields ? fields : "");

	char auth[TOKEN_MAX + 128];
	auth_header(auth, sizeof(auth));
	char headers[TOKEN_MAX + 512];
	snprintf(headers, sizeof(headers),
			 "User-Agent: " WEB_REMIX_UA "\r\nOrigin: https://music.youtube.com\r\nReferer: "
			 "https://music.youtube.com/\r\nX-Youtube-Client-Name: 67\r\nX-Youtube-Client-Version: %s\r\n%s",
			 web_remix_version, auth);

	http_req_t req = {0};
	req.method = "POST";
	req.body = body;
	req.content_type = "application/json";
	req.extra_headers = headers;
	req.want_error_body = true;
	size_t len = 0;
	int status = 0;
	bool ok = http_request(url, &req, text, &len, YTM_BODY_LIMIT, YTM_TIMEOUT_SECS, &status);
	free(body);
	if (!ok || !*text) {
		const char *why = http_last_error();
		set_error("%s", why && why[0] ? why : tr("ytm_cannot_reach_ytm"));
		free(*text);
		*text = NULL;
		return false;
	}
	if (status >= 400) {
		set_error(tr("ytm_answered_with"), status);
		free(*text);
		*text = NULL;
		return false;
	}
	if (!json_parse(*text, doc)) {
		set_error("%s", tr("ytm_unreadable_reply"));
		free(*text);
		*text = NULL;
		return false;
	}
	char visitor[256] = "";
	int rc = json_get(doc, json_root(doc), "responseContext");
	if (rc >= 0 && json_obj_str(doc, rc, "visitorData", visitor, sizeof(visitor)) && visitor[0]) {
		pthread_mutex_lock(&visitor_lock);
		snprintf(visitor_id, sizeof(visitor_id), "%s", visitor);
		pthread_mutex_unlock(&visitor_lock);
	}
	return true;
}

// A fresh visitor id: any cheap catalogue call hands one out.
static void renew_visitor(void) {
	char *text = NULL;
	json_doc_t doc;
	pthread_mutex_lock(&visitor_lock);
	visitor_id[0] = '\0';
	pthread_mutex_unlock(&visitor_lock);
	if (music_call("browse", NULL, "\"browseId\":\"FEmusic_home\"", &text, &doc)) {
		json_free(&doc);
		free(text);
	}
}

static void current_visitor(char *out, size_t size) {
	pthread_mutex_lock(&visitor_lock);
	// URL-decoded: the id arrives percent-encoded ("...%3D%3D").
	size_t n = 0;
	for (const char *p = visitor_id; *p && n + 1 < size; p++) {
		if (p[0] == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
			char hex[3] = {p[1], p[2], 0};
			out[n++] = (char)strtol(hex, NULL, 16);
			p += 2;
		} else {
			out[n++] = *p;
		}
	}
	out[n] = '\0';
	pthread_mutex_unlock(&visitor_lock);
}

// ---------------------------------------------------------------------------
// walking renderer trees
//
// InnerTube answers are deep and change shape between pages (and over time).
// Instead of fixed paths, items are found by renderer name anywhere in the
// tree, and fields by key anywhere inside the item. The tokens are in document
// order, so "inside" is simply "between the item's first and last token".
// ---------------------------------------------------------------------------

static bool key_is(const json_doc_t *doc, int tok, const char *key) {
	const json_tok_t *t = &doc->toks[tok];
	size_t n = strlen(key);
	return t->type == JSON_STRING && (size_t)(t->end - t->start) == n && memcmp(doc->text + t->start, key, n) == 0;
}

// Is token `tok` a key (a string whose parent is an object)?
static bool is_key(const json_doc_t *doc, int tok) {
	int p = doc->toks[tok].parent;
	return doc->toks[tok].type == JSON_STRING && p >= 0 && doc->toks[p].type == JSON_OBJECT;
}

// The token one past everything inside `tok`.
static int tok_end(const json_doc_t *doc, int tok) {
	int end = doc->toks[tok].end;
	int i = tok + 1;
	while (i < doc->count && doc->toks[i].start < end) {
		i++;
	}
	return i;
}

// The value of the first `key` found inside `tok` (at any depth), after token
// `from` (exclusive; pass `tok` to search all). -1 when absent.
static int find_in(const json_doc_t *doc, int tok, int from, const char *key) {
	int end = tok_end(doc, tok);
	for (int i = from + 1; i < end; i++) {
		if (is_key(doc, i) && key_is(doc, i, key)) {
			return i + 1 < doc->count ? i + 1 : -1;
		}
	}
	return -1;
}

static bool find_str(const json_doc_t *doc, int tok, const char *key, char *out, size_t size) {
	out[0] = '\0';
	int v = find_in(doc, tok, tok, key);
	return v >= 0 && json_str(doc, v, out, size) && out[0];
}

// The concatenated text of a {"runs":[{"text":..},..]} or {"simpleText":..}.
static void text_of(const json_doc_t *doc, int obj, char *out, size_t size) {
	out[0] = '\0';
	if (obj < 0 || doc->toks[obj].type != JSON_OBJECT) {
		return;
	}
	int simple = json_get(doc, obj, "simpleText");
	if (simple >= 0) {
		json_str(doc, simple, out, size);
		return;
	}
	int runs = json_get(doc, obj, "runs");
	int n = json_len(doc, runs);
	size_t used = 0;
	for (int i = 0; i < n && used + 1 < size; i++) {
		char piece[256];
		if (json_obj_str(doc, json_at(doc, runs, i), "text", piece, sizeof(piece))) {
			used += (size_t)snprintf(out + used, size - used, "%s", piece);
		}
	}
}

// The largest thumbnail inside `tok`, resized to cover_px when it is a
// googleusercontent one (those take size parameters after '=').
static void cover_in(const json_doc_t *doc, int tok, char *out, size_t size) {
	out[0] = '\0';
	int thumbs = find_in(doc, tok, tok, "thumbnails");
	int n = json_len(doc, thumbs);
	if (n <= 0) {
		return;
	}
	char url[YTM_COVER_MAX];
	if (!json_obj_str(doc, json_at(doc, thumbs, n - 1), "url", url, sizeof(url))) {
		return;
	}
	if (strstr(url, "googleusercontent.com") || strstr(url, "ggpht.com")) {
		char *eq = strrchr(url, '=');
		if (eq) {
			*eq = '\0';
		}
		snprintf(out, size, "%s=w%d-h%d-l90-rj", url, cover_px, cover_px);
	} else {
		snprintf(out, size, "%s", url);
	}
}

// "3:45" or "1:02:03" -> seconds; 0 when not a duration.
static int parse_duration(const char *s) {
	int total = 0, parts = 0;
	const char *p = s;
	while (*p) {
		if (!isdigit((unsigned char)*p)) {
			return 0;
		}
		int v = 0;
		while (isdigit((unsigned char)*p)) {
			v = v * 10 + (*p - '0');
			p++;
		}
		total = total * 60 + v;
		parts++;
		if (*p == ':') {
			p++;
		} else if (*p) {
			return 0;
		}
	}
	return parts >= 2 ? total : 0;
}

// The pageType of the browseEndpoint inside run/item `tok`, with its browseId.
static bool browse_target(const json_doc_t *doc, int tok, char *page_type, size_t pt_size, char *browse_id,
						  size_t id_size) {
	page_type[0] = browse_id[0] = '\0';
	int be = find_in(doc, tok, tok, "browseEndpoint");
	if (be < 0) {
		return false;
	}
	json_obj_str(doc, be, "browseId", browse_id, id_size);
	find_str(doc, be, "pageType", page_type, pt_size);
	return browse_id[0] != '\0';
}

// Iterates every renderer named `name`: calls `each` with the renderer's
// object token. Stops when `each` returns false.
typedef bool (*renderer_cb)(const json_doc_t *doc, int obj, void *user);

static void for_each_renderer(const json_doc_t *doc, const char *name, renderer_cb each, void *user) {
	for (int i = 0; i < doc->count; i++) {
		if (is_key(doc, i) && key_is(doc, i, name) && i + 1 < doc->count && doc->toks[i + 1].type == JSON_OBJECT) {
			if (!each(doc, i + 1, user)) {
				return;
			}
			i = tok_end(doc, i + 1) - 1;
		}
	}
}

// ---------------------------------------------------------------------------
// continuations: page two of anything
// ---------------------------------------------------------------------------

// One remembered "next page" per thread, tagged with what list it belongs to.
static __thread char cont_owner[300];
static __thread char cont_token[1024];
static __thread bool cont_is_command; // continuationCommand (body) vs nextContinuationData (URL)

static void remember_continuation(const json_doc_t *doc, const char *owner) {
	cont_owner[0] = cont_token[0] = '\0';
	int root = json_root(doc);
	int ncd = find_in(doc, root, root, "nextContinuationData");
	if (ncd >= 0 && json_obj_str(doc, ncd, "continuation", cont_token, sizeof(cont_token))) {
		cont_is_command = false;
	} else {
		int cc = find_in(doc, root, root, "continuationCommand");
		if (cc >= 0 && json_obj_str(doc, cc, "token", cont_token, sizeof(cont_token))) {
			cont_is_command = true;
		}
	}
	if (cont_token[0]) {
		snprintf(cont_owner, sizeof(cont_owner), "%s", owner);
	}
}

// Makes the next call for `owner`: returns false (and 0 items is the answer)
// when there is no next page for it.
static bool continue_call(const char *endpoint, const char *owner, char **text, json_doc_t *doc) {
	if (!cont_token[0] || strcmp(cont_owner, owner) != 0) {
		return false;
	}
	char token[1024];
	snprintf(token, sizeof(token), "%s", cont_token);
	cont_token[0] = '\0';
	if (cont_is_command) {
		char fields[1200];
		snprintf(fields, sizeof(fields), "\"continuation\":\"%s\"", token);
		return music_call(endpoint, NULL, fields, text, doc);
	}
	char enc[2048], query[4200];
	http_url_encode(token, enc, sizeof(enc));
	snprintf(query, sizeof(query), "ctoken=%s&continuation=%s&type=next", enc, enc);
	return music_call(endpoint, query, "", text, doc);
}

// ---------------------------------------------------------------------------
// parsing list items
// ---------------------------------------------------------------------------

typedef struct {
	ytm_track_t *out;
	int max;
	int count;
	// Filled in for album pages, where rows leave artist/album/cover out.
	const char *album_title;
	const char *album_id;
	const char *album_artist;
	const char *album_cover;
} track_sink_t;

static bool track_from_item(const json_doc_t *doc, int item, void *user) {
	track_sink_t *sink = user;
	if (sink->count >= sink->max) {
		return false;
	}

	char video_id[24] = "";
	int pid = find_in(doc, item, item, "playlistItemData");
	if (pid >= 0) {
		json_obj_str(doc, pid, "videoId", video_id, sizeof(video_id));
	}
	if (!video_id[0]) {
		int we = find_in(doc, item, item, "watchEndpoint");
		if (we >= 0) {
			json_obj_str(doc, we, "videoId", video_id, sizeof(video_id));
		}
	}
	if (!video_id[0]) {
		return true; // a header row, an artist, a "show all": not a song
	}

	ytm_track_t *t = &sink->out[sink->count];
	memset(t, 0, sizeof(*t));
	t->id = ytm_id_for(video_id);
	t->streamable = true;
	t->sample_rate = 44100;
	t->bit_depth = 16;

	int flex = json_get(doc, item, "flexColumns");
	int columns = json_len(doc, flex);
	for (int c = 0; c < columns; c++) {
		int col = find_in(doc, json_at(doc, flex, c), json_at(doc, flex, c), "text");
		if (c == 0) {
			text_of(doc, col, t->title, sizeof(t->title));
			continue;
		}
		int runs = col >= 0 ? json_get(doc, col, "runs") : -1;
		int n = json_len(doc, runs);
		for (int r = 0; r < n; r++) {
			int run = json_at(doc, runs, r);
			char text[256] = "", pt[64], bid[YTM_ID_MAX];
			json_obj_str(doc, run, "text", text, sizeof(text));
			if (browse_target(doc, run, pt, sizeof(pt), bid, sizeof(bid))) {
				if (strstr(pt, "ARTIST") && !t->artist[0]) {
					snprintf(t->artist, sizeof(t->artist), "%s", text);
				} else if (strstr(pt, "ALBUM") && !t->album[0]) {
					snprintf(t->album, sizeof(t->album), "%s", text);
					snprintf(t->album_id, sizeof(t->album_id), "%s", bid);
				}
			} else if (!t->duration) {
				t->duration = parse_duration(text);
			}
		}
		// Songs uploaded as plain videos name the artist without a link: the
		// first run of the second column is the best guess.
		if (c == 1 && !t->artist[0] && n > 0) {
			char text[256] = "";
			json_obj_str(doc, json_at(doc, runs, 0), "text", text, sizeof(text));
			if (!parse_duration(text) && strcmp(text, "Song") != 0 && strcmp(text, "Video") != 0) {
				snprintf(t->artist, sizeof(t->artist), "%s", text);
			}
		}
	}

	if (!t->duration) {
		int fixed = json_get(doc, item, "fixedColumns");
		if (fixed >= 0) {
			char text[32];
			int col = find_in(doc, fixed, fixed, "text");
			text_of(doc, col, text, sizeof(text));
			t->duration = parse_duration(text);
		}
	}

	int index = json_get(doc, item, "index");
	if (index >= 0) {
		char num[16];
		text_of(doc, index, num, sizeof(num));
		t->track_number = atoi(num);
	}

	int thumb = json_get(doc, item, "thumbnail");
	if (thumb >= 0) {
		cover_in(doc, thumb, t->cover, sizeof(t->cover));
	}
	if (sink->album_title) {
		if (!t->album[0]) {
			snprintf(t->album, sizeof(t->album), "%s", sink->album_title);
		}
		if (!t->album_id[0] && sink->album_id) {
			snprintf(t->album_id, sizeof(t->album_id), "%s", sink->album_id);
		}
		if (!t->artist[0] && sink->album_artist) {
			snprintf(t->artist, sizeof(t->artist), "%s", sink->album_artist);
		}
		if (!t->cover[0] && sink->album_cover) {
			snprintf(t->cover, sizeof(t->cover), "%s", sink->album_cover);
		}
	}
	if (!t->cover[0]) {
		snprintf(t->cover, sizeof(t->cover), "https://i.ytimg.com/vi/%s/hqdefault.jpg", video_id);
	}
	if (!t->track_number) {
		t->track_number = sink->count + 1;
	}
	sink->count++;
	return true;
}

typedef struct {
	ytm_album_t *out;
	int max;
	int count;
} album_sink_t;

// Albums come as list rows (search) or as cards (artist page, new releases).
static bool album_from_item(const json_doc_t *doc, int item, void *user) {
	album_sink_t *sink = user;
	if (sink->count >= sink->max) {
		return false;
	}
	char pt[64], bid[YTM_ID_MAX];
	int nav = json_get(doc, item, "navigationEndpoint");
	if (nav < 0 || !browse_target(doc, nav, pt, sizeof(pt), bid, sizeof(bid)) || !strstr(pt, "ALBUM")) {
		return true;
	}
	ytm_album_t *a = &sink->out[sink->count];
	memset(a, 0, sizeof(*a));
	snprintf(a->id_text, sizeof(a->id_text), "%s", bid);
	a->id = ytm_id_for(bid);

	int title = json_get(doc, item, "title");
	if (title >= 0) {
		text_of(doc, title, a->title, sizeof(a->title)); // a card
	}
	int flex = json_get(doc, item, "flexColumns");
	if (!a->title[0] && flex >= 0) {
		text_of(doc, find_in(doc, json_at(doc, flex, 0), json_at(doc, flex, 0), "text"), a->title,
				sizeof(a->title));
	}

	// The subtitle: "Album • Artist • 2023" in one order or another.
	int sub = json_get(doc, item, "subtitle");
	if (sub < 0 && flex >= 0) {
		sub = find_in(doc, json_at(doc, flex, 1), json_at(doc, flex, 1), "text");
	}
	int runs = sub >= 0 ? json_get(doc, sub, "runs") : -1;
	for (int r = 0; r < json_len(doc, runs); r++) {
		int run = json_at(doc, runs, r);
		char text[200] = "", rpt[64], rid[YTM_ID_MAX];
		json_obj_str(doc, run, "text", text, sizeof(text));
		if (browse_target(doc, run, rpt, sizeof(rpt), rid, sizeof(rid)) && strstr(rpt, "ARTIST")) {
			if (!a->artist[0]) {
				snprintf(a->artist, sizeof(a->artist), "%s", text);
			}
		} else if (strlen(text) == 4 && isdigit((unsigned char)text[0]) && isdigit((unsigned char)text[3])) {
			snprintf(a->released, sizeof(a->released), "%s", text);
		}
	}
	int thumb = json_get(doc, item, "thumbnailRenderer");
	if (thumb < 0) {
		thumb = json_get(doc, item, "thumbnail");
	}
	if (thumb >= 0) {
		cover_in(doc, thumb, a->cover, sizeof(a->cover));
	}
	// Albums show up more than once on an artist page (albums and singles
	// shelves, "fans might also like"...): keep the first.
	for (int i = 0; i < sink->count; i++) {
		if (strcmp(sink->out[i].id_text, a->id_text) == 0) {
			return true;
		}
	}
	sink->count++;
	return true;
}

typedef struct {
	ytm_artist_t *out;
	int max;
	int count;
} artist_sink_t;

static bool artist_from_item(const json_doc_t *doc, int item, void *user) {
	artist_sink_t *sink = user;
	if (sink->count >= sink->max) {
		return false;
	}
	char pt[64], bid[YTM_ID_MAX];
	int nav = json_get(doc, item, "navigationEndpoint");
	if (nav < 0 || !browse_target(doc, nav, pt, sizeof(pt), bid, sizeof(bid)) || !strstr(pt, "ARTIST")) {
		return true;
	}
	ytm_artist_t *a = &sink->out[sink->count];
	memset(a, 0, sizeof(*a));
	a->id = ytm_id_for(bid);
	int flex = json_get(doc, item, "flexColumns");
	if (flex >= 0) {
		text_of(doc, find_in(doc, json_at(doc, flex, 0), json_at(doc, flex, 0), "text"), a->name, sizeof(a->name));
	} else {
		text_of(doc, json_get(doc, item, "title"), a->name, sizeof(a->name));
	}
	int thumb = json_get(doc, item, "thumbnail");
	if (thumb < 0) {
		thumb = json_get(doc, item, "thumbnailRenderer");
	}
	if (thumb >= 0) {
		cover_in(doc, thumb, a->image, sizeof(a->image));
	}
	sink->count++;
	return true;
}

typedef struct {
	ytm_playlist_t *out;
	int max;
	int count;
} playlist_sink_t;

static bool playlist_from_item(const json_doc_t *doc, int item, void *user) {
	playlist_sink_t *sink = user;
	if (sink->count >= sink->max) {
		return false;
	}
	char pt[64], bid[YTM_ID_MAX];
	int nav = json_get(doc, item, "navigationEndpoint");
	if (nav < 0 || !browse_target(doc, nav, pt, sizeof(pt), bid, sizeof(bid)) || !strstr(pt, "PLAYLIST")) {
		return true;
	}
	ytm_playlist_t *p = &sink->out[sink->count];
	memset(p, 0, sizeof(*p));
	snprintf(p->id, sizeof(p->id), "%s", bid); // keeps the "VL" prefix: it is what browse wants
	int title = json_get(doc, item, "title");
	if (title >= 0) {
		text_of(doc, title, p->name, sizeof(p->name));
	} else {
		int flex = json_get(doc, item, "flexColumns");
		text_of(doc, find_in(doc, json_at(doc, flex, 0), json_at(doc, flex, 0), "text"), p->name, sizeof(p->name));
	}
	int sub = json_get(doc, item, "subtitle");
	if (sub >= 0) {
		text_of(doc, sub, p->owner, sizeof(p->owner));
	}
	int thumb = json_get(doc, item, "thumbnailRenderer");
	if (thumb >= 0) {
		cover_in(doc, thumb, p->image, sizeof(p->image));
	}
	sink->count++;
	return true;
}

// ---------------------------------------------------------------------------
// search
// ---------------------------------------------------------------------------

// The filter parameters music.youtube.com itself puts on its result chips
// (read off a live answer, November 2026).
#define PARAMS_SONGS "EgWKAQIIAWoKEAQQBRAVEAoQEQ=="
#define PARAMS_ALBUMS "EgWKAQIYAWoKEAQQBRAVEAoQEQ=="
#define PARAMS_ARTISTS "EgWKAQIgAWoKEAQQBRAVEAoQEQ=="
#define PARAMS_NONE ""

// Runs a filtered search, or its next page. *text and *doc to free on true.
static bool search_call(const char *query, const char *params, int offset, char **text, json_doc_t *doc,
						bool *no_more) {
	*no_more = false;
	char owner[300];
	snprintf(owner, sizeof(owner), "search|%s|%.200s", params, query);
	if (offset > 0) {
		if (!continue_call("search", owner, text, doc)) {
			*no_more = true;
			return false;
		}
	} else {
		char q[600], fields[800];
		json_escape(query, q, sizeof(q));
		if (params[0]) {
			snprintf(fields, sizeof(fields), "\"query\":\"%s\",\"params\":\"%s\"", q, params);
		} else {
			snprintf(fields, sizeof(fields), "\"query\":\"%s\"", q);
		}
		if (!music_call("search", NULL, fields, text, doc)) {
			return false;
		}
	}
	remember_continuation(doc, owner);
	return true;
}

int ytm_search_tracks(const char *query, int offset, ytm_track_t *out, int max) {
	clear_error();
	char *text;
	json_doc_t doc;
	bool no_more;
	if (!search_call(query, PARAMS_SONGS, offset, &text, &doc, &no_more)) {
		return no_more ? 0 : -1;
	}
	track_sink_t sink = {.out = out, .max = max};
	for_each_renderer(&doc, "musicResponsiveListItemRenderer", track_from_item, &sink);
	json_free(&doc);
	free(text);

	// Where YouTube Music has no song catalogue (or the filter changed name
	// again) the Songs filter answers "No results". The unfiltered search
	// still lists songs and videos: better those than nothing.
	if (sink.count == 0 && offset == 0) {
		if (!search_call(query, PARAMS_NONE, 0, &text, &doc, &no_more)) {
			return 0;
		}
		for_each_renderer(&doc, "musicResponsiveListItemRenderer", track_from_item, &sink);
		json_free(&doc);
		free(text);
		cont_token[0] = '\0'; // the unfiltered answer pages differently: one page only
	}
	return sink.count;
}

int ytm_search_albums(const char *query, int offset, ytm_album_t *out, int max) {
	clear_error();
	char *text;
	json_doc_t doc;
	bool no_more;
	if (!search_call(query, PARAMS_ALBUMS, offset, &text, &doc, &no_more)) {
		return no_more ? 0 : -1;
	}
	album_sink_t sink = {.out = out, .max = max};
	for_each_renderer(&doc, "musicResponsiveListItemRenderer", album_from_item, &sink);
	json_free(&doc);
	free(text);
	return sink.count;
}

int ytm_search_artists(const char *query, int offset, ytm_artist_t *out, int max) {
	clear_error();
	char *text;
	json_doc_t doc;
	bool no_more;
	if (!search_call(query, PARAMS_ARTISTS, offset, &text, &doc, &no_more)) {
		return no_more ? 0 : -1;
	}
	artist_sink_t sink = {.out = out, .max = max};
	for_each_renderer(&doc, "musicResponsiveListItemRenderer", artist_from_item, &sink);
	json_free(&doc);
	free(text);
	return sink.count;
}

// ---------------------------------------------------------------------------
// browse
// ---------------------------------------------------------------------------

static bool browse_call(const char *browse_id, int offset, char **text, json_doc_t *doc, bool *no_more) {
	*no_more = false;
	char owner[300];
	snprintf(owner, sizeof(owner), "browse|%.250s", browse_id);
	if (offset > 0) {
		if (!continue_call("browse", owner, text, doc)) {
			*no_more = true;
			return false;
		}
	} else {
		char id[200], fields[300];
		json_escape(browse_id, id, sizeof(id));
		snprintf(fields, sizeof(fields), "\"browseId\":\"%s\"", id);
		if (!music_call("browse", NULL, fields, text, doc)) {
			return false;
		}
	}
	remember_continuation(doc, owner);
	return true;
}

int ytm_album_tracks(const char *album_id, int offset, ytm_track_t *out, int max) {
	clear_error();
	if (!album_id || !album_id[0]) {
		set_error("%s", tr("ytm_album_without_id"));
		return -1;
	}
	char *text;
	json_doc_t doc;
	bool no_more;
	if (!browse_call(album_id, offset, &text, &doc, &no_more)) {
		return no_more ? 0 : -1;
	}

	// The header: title, artist, cover. Its renderer has had several names.
	char title[YTM_TITLE_MAX] = "", artist[YTM_NAME_MAX] = "", cover[YTM_COVER_MAX] = "";
	int root = json_root(&doc);
	const char *headers[] = {"musicResponsiveHeaderRenderer", "musicDetailHeaderRenderer"};
	for (size_t h = 0; h < 2 && !title[0]; h++) {
		int hdr = find_in(&doc, root, root, headers[h]);
		if (hdr < 0) {
			continue;
		}
		text_of(&doc, json_get(&doc, hdr, "title"), title, sizeof(title));
		int strap = json_get(&doc, hdr, "straplineTextOne");
		if (strap < 0) {
			strap = json_get(&doc, hdr, "subtitle");
		}
		int runs = strap >= 0 ? json_get(&doc, strap, "runs") : -1;
		for (int r = 0; r < json_len(&doc, runs) && !artist[0]; r++) {
			int run = json_at(&doc, runs, r);
			char pt[64], bid[YTM_ID_MAX];
			if (browse_target(&doc, run, pt, sizeof(pt), bid, sizeof(bid)) && strstr(pt, "ARTIST")) {
				json_obj_str(&doc, run, "text", artist, sizeof(artist));
			}
		}
		if (!artist[0] && strap >= 0) {
			text_of(&doc, strap, artist, sizeof(artist));
		}
		cover_in(&doc, hdr, cover, sizeof(cover));
	}

	track_sink_t sink = {.out = out,
						 .max = max,
						 .album_title = title[0] ? title : NULL,
						 .album_id = album_id,
						 .album_artist = artist[0] ? artist : NULL,
						 .album_cover = cover[0] ? cover : NULL};
	for_each_renderer(&doc, "musicResponsiveListItemRenderer", track_from_item, &sink);
	for (int i = 0; i < sink.count; i++) {
		out[i].track_number = offset + i + 1;
	}
	json_free(&doc);
	free(text);
	return sink.count;
}

int ytm_artist_albums(long artist_id, int offset, ytm_album_t *out, int max) {
	clear_error();
	if (offset > 0) {
		return 0; // the artist page is one answer
	}
	char channel[YTM_ID_MAX];
	if (!ytm_key_for(artist_id, channel, sizeof(channel))) {
		set_error("%s", tr("ytm_cannot_reach_ytm"));
		return -1;
	}
	char *text;
	json_doc_t doc;
	bool no_more;
	if (!browse_call(channel, 0, &text, &doc, &no_more)) {
		return -1;
	}
	album_sink_t sink = {.out = out, .max = max};
	for_each_renderer(&doc, "musicTwoRowItemRenderer", album_from_item, &sink);
	json_free(&doc);
	free(text);
	return sink.count;
}

static int tracks_of_browse(const char *browse_id, int offset, ytm_track_t *out, int max) {
	char *text;
	json_doc_t doc;
	bool no_more;
	if (!browse_call(browse_id, offset, &text, &doc, &no_more)) {
		return no_more ? 0 : -1;
	}
	track_sink_t sink = {.out = out, .max = max};
	for_each_renderer(&doc, "musicResponsiveListItemRenderer", track_from_item, &sink);
	for (int i = 0; i < sink.count; i++) {
		out[i].track_number = offset + i + 1;
	}
	json_free(&doc);
	free(text);
	return sink.count;
}

int ytm_playlist_tracks(const char *playlist_id, int offset, ytm_track_t *out, int max) {
	clear_error();
	if (!playlist_id || !playlist_id[0]) {
		set_error("%s", tr("ytm_playlist_without_id"));
		return -1;
	}
	char id[YTM_ID_MAX + 4];
	snprintf(id, sizeof(id), "%s%s", strncmp(playlist_id, "VL", 2) == 0 ? "" : "VL", playlist_id);
	return tracks_of_browse(id, offset, out, max);
}

static bool need_login(void) {
	if (ytm_logged_in()) {
		return false;
	}
	set_error("%s", tr("ytm_not_signed_in"));
	return true;
}

// "Liked music" is a playlist named LM.
int ytm_favorite_tracks(int offset, ytm_track_t *out, int max) {
	clear_error();
	if (need_login()) {
		return -1;
	}
	return tracks_of_browse("VLLM", offset, out, max);
}

int ytm_favorite_albums(int offset, ytm_album_t *out, int max) {
	clear_error();
	if (need_login()) {
		return -1;
	}
	char *text;
	json_doc_t doc;
	bool no_more;
	if (!browse_call("FEmusic_liked_albums", offset, &text, &doc, &no_more)) {
		return no_more ? 0 : -1;
	}
	album_sink_t sink = {.out = out, .max = max};
	for_each_renderer(&doc, "musicTwoRowItemRenderer", album_from_item, &sink);
	json_free(&doc);
	free(text);
	return sink.count;
}

int ytm_favorite_artists(int offset, ytm_artist_t *out, int max) {
	clear_error();
	if (need_login()) {
		return -1;
	}
	char *text;
	json_doc_t doc;
	bool no_more;
	if (!browse_call("FEmusic_library_corpus_track_artists", offset, &text, &doc, &no_more)) {
		return no_more ? 0 : -1;
	}
	artist_sink_t sink = {.out = out, .max = max};
	for_each_renderer(&doc, "musicResponsiveListItemRenderer", artist_from_item, &sink);
	json_free(&doc);
	free(text);
	return sink.count;
}

int ytm_user_playlists(int offset, ytm_playlist_t *out, int max) {
	clear_error();
	if (need_login()) {
		return -1;
	}
	char *text;
	json_doc_t doc;
	bool no_more;
	if (!browse_call("FEmusic_liked_playlists", offset, &text, &doc, &no_more)) {
		return no_more ? 0 : -1;
	}
	playlist_sink_t sink = {.out = out, .max = max};
	for_each_renderer(&doc, "musicTwoRowItemRenderer", playlist_from_item, &sink);
	json_free(&doc);
	free(text);
	return sink.count;
}

// "new" is the new-releases grid; the other two showcases have no album-shaped
// equivalent without an account, so they answer nothing and the page drops
// their rows.
int ytm_featured_albums(const char *type, int offset, ytm_album_t *out, int max) {
	clear_error();
	if (!type || strcmp(type, "new") != 0) {
		return 0;
	}
	char *text;
	json_doc_t doc;
	bool no_more;
	if (!browse_call("FEmusic_new_releases_albums", offset, &text, &doc, &no_more) &&
		(no_more || offset > 0 || !browse_call("FEmusic_new_releases", 0, &text, &doc, &no_more))) {
		return 0; // the page drops the row rather than showing an error
	}
	album_sink_t sink = {.out = out, .max = max};
	for_each_renderer(&doc, "musicTwoRowItemRenderer", album_from_item, &sink);
	json_free(&doc);
	free(text);
	return sink.count;
}

// ---------------------------------------------------------------------------
// the stream
// ---------------------------------------------------------------------------

typedef struct {
	const char *name;
	const char *context; // the "client" object
	const char *agent;
} player_client_t;

static const player_client_t PLAYER_CLIENTS[] = {
	{"visionos",
	 "{\"clientName\":\"VISIONOS\",\"clientVersion\":\"1.03\",\"deviceMake\":\"Apple\",\"deviceModel\":"
	 "\"RealityDevice17,1\",\"osName\":\"visionOS\",\"osVersion\":\"26.6.1\"",
	 "com.google.visionosyoutube/1.03 (RealityDevice17,1; U; CPU visionOS 26_6_1 like Mac OS X; en_US) gzip"},
	{"visionos_102",
	 "{\"clientName\":\"VISIONOS\",\"clientVersion\":\"1.02\",\"deviceMake\":\"Apple\",\"deviceModel\":"
	 "\"RealityDevice14,1\",\"osName\":\"visionOS\",\"osVersion\":\"26.6.1\"",
	 "com.google.visionosyoutube/1.02 (RealityDevice14,1; U; CPU visionOS 26_6_1 like Mac OS X; en_US) gzip"},
	{"android_music",
	 "{\"clientName\":\"ANDROID_MUSIC\",\"clientVersion\":\"7.12.52\",\"deviceMake\":\"Google\",\"deviceModel\":"
	 "\"Pixel 8\",\"osName\":\"Android\",\"osVersion\":\"14\"",
	 "com.google.android.apps.youtube.music/7.12.52 (Linux; U; Android 14) gzip"},
	{"android_vr",
	 "{\"clientName\":\"ANDROID_VR\",\"clientVersion\":\"1.64.34\",\"deviceMake\":\"Pico\",\"deviceModel\":"
	 "\"A8110\",\"osName\":\"Android\",\"osVersion\":\"10\",\"androidSdkVersion\":29",
	 "com.google.android.apps.youtube.vr.pico/1.64.34 (Linux; U; Android 10; en_US; A8110 Build/5.13.7)"},
	{"android_vr_old",
	 "{\"clientName\":\"ANDROID_VR\",\"clientVersion\":\"1.62.27\",\"deviceMake\":\"Oculus\",\"deviceModel\":"
	 "\"Quest 3\",\"osName\":\"Android\",\"osVersion\":\"12L\",\"androidSdkVersion\":32",
	 "com.google.android.apps.youtube.vr.oculus/1.62.27 (Linux; U; Android 12L; eureka-user "
	 "Build/SQ3A.220605.009.A1) gzip"},
};

// Asks one client. True with `out` filled when an itag 251 or 140 with a plain URL
// came back. `reason` gets YouTube's own words when it refused.
static bool try_player(const player_client_t *c, const char *video_id, ytm_stream_t *out, char *reason,
					   size_t reason_size) {
	char visitor[256];
	current_visitor(visitor, sizeof(visitor));
	char body[1400];
	snprintf(body, sizeof(body),
			 "{\"context\":{\"client\":%s,\"visitorData\":\"%s\",\"hl\":\"%s\",\"gl\":\"%s\"}},\"videoId\":\"%s\","
			 "\"contentCheckOk\":true,\"racyCheckOk\":true}",
			 c->context, visitor, hl, gl, video_id);
	char headers[700];
	snprintf(headers, sizeof(headers), "User-Agent: %s\r\nX-Goog-Api-Format-Version: 2\r\n%s%s%s", c->agent,
			 visitor[0] ? "X-Goog-Visitor-Id: " : "", visitor, visitor[0] ? "\r\n" : "");

	http_req_t req = {0};
	req.method = "POST";
	req.body = body;
	req.content_type = "application/json";
	req.extra_headers = headers;
	req.want_error_body = true;
	char *text = NULL;
	size_t len = 0;
	int status = 0;
	if (!http_request(YTM_PLAYER_API, &req, &text, &len, YTM_BODY_LIMIT, YTM_TIMEOUT_SECS, &status) || !text) {
		free(text);
		snprintf(reason, reason_size, "%s", tr("ytm_cannot_reach_ytm"));
		return false;
	}

	bool found = false;
	json_doc_t doc;
	if (json_parse(text, &doc)) {
		int root = json_root(&doc);
		char state[32] = "";
		int ps = json_get(&doc, root, "playabilityStatus");
		json_obj_str(&doc, ps, "status", state, sizeof(state));
		if (strcmp(state, "OK") != 0) {
			char why[200] = "";
			json_obj_str(&doc, ps, "reason", why, sizeof(why));
			snprintf(reason, reason_size, "%s", why[0] ? why : state);
		}
		// The chosen stream first, the other one when this track lacks it.
		int formats = json_path(&doc, root, "streamingData.adaptiveFormats");
		int wanted[2] = {251, 140};
		if (quality == YTM_QUALITY_AAC128) {
			wanted[0] = 140;
			wanted[1] = 251;
		}
		for (int w = 0; w < 2 && !found; w++) {
			for (int i = 0; i < json_len(&doc, formats) && !found; i++) {
				int f = json_at(&doc, formats, i);
				if (json_obj_long(&doc, f, "itag", 0) != wanted[w]) {
					continue;
				}
				if (!json_obj_str(&doc, f, "url", out->url, sizeof(out->url)) || !out->url[0]) {
					continue; // ciphered: not for us
				}
				char cl[24] = "";
				json_obj_str(&doc, f, "contentLength", cl, sizeof(cl));
				out->content_length = strtol(cl, NULL, 10);
				out->kind = YTM_STREAM_FILE;
				if (wanted[w] == 251) {
					snprintf(out->mime, sizeof(out->mime), "audio/opus");
					snprintf(out->codecs, sizeof(out->codecs), "opus");
					out->sample_rate = 48000;
				} else {
					snprintf(out->mime, sizeof(out->mime), "audio/aac");
					snprintf(out->codecs, sizeof(out->codecs), "mp4a.40.2");
					out->sample_rate = (int)json_obj_long(&doc, f, "audioSampleRate", 44100);
				}
				out->bit_depth = 16;
				snprintf(out->user_agent, sizeof(out->user_agent), "%s", c->agent);
				found = true;
			}
		}
		json_free(&doc);
	} else {
		snprintf(reason, reason_size, "%s", tr("ytm_unreadable_reply"));
	}
	free(text);
	return found;
}

bool ytm_track_stream(long track_id, ytm_stream_t *out) {
	clear_error();
	memset(out, 0, sizeof(*out));
	char video_id[YTM_ID_MAX];
	if (!ytm_key_for(track_id, video_id, sizeof(video_id))) {
		set_error("%s", tr("ytm_track_without_id"));
		return false;
	}

	char reason[200] = "";
	// The configured order: names separated by commas.
	char list[128];
	snprintf(list, sizeof(list), "%s", player_clients);
	char *save = NULL;
	for (char *name = strtok_r(list, ", ", &save); name; name = strtok_r(NULL, ", ", &save)) {
		for (size_t i = 0; i < sizeof(PLAYER_CLIENTS) / sizeof(PLAYER_CLIENTS[0]); i++) {
			if (strcmp(PLAYER_CLIENTS[i].name, name) != 0) {
				continue;
			}
			// Up to three visitor ids per client: some ids land in an A/B test
			// that refuses everything, a new one does not (Morphe does the same).
			for (int attempt = 0; attempt < 3; attempt++) {
				char visitor[8];
				current_visitor(visitor, sizeof(visitor));
				if (!visitor[0]) {
					renew_visitor();
				}
				if (try_player(&PLAYER_CLIENTS[i], video_id, out, reason, sizeof(reason))) {
					printf("ytmusic: %s via %s, %ld bytes\n", video_id, name, out->content_length);
					return true;
				}
				printf("ytmusic: %s refused by %s: %s\n", video_id, name, reason);
				if (!strstr(reason, "bot") && !strstr(reason, "LOGIN")) {
					break; // not a visitor problem: the next client may do better
				}
				renew_visitor();
			}
		}
	}
	set_error("%s", reason[0] ? reason : tr("ytm_no_track_address"));
	return false;
}

bool ytm_dash_segment_url(const ytm_stream_t *stream, int number, char *out, size_t size) {
	(void)stream;
	(void)number;
	(void)out;
	(void)size;
	return false; // YouTube Music never sends segments here
}

// ---------------------------------------------------------------------------
// covers: the lists already carry finished URLs
// ---------------------------------------------------------------------------

void ytm_cover_url(const char *uuid, int size_px, char *out, size_t out_size) {
	(void)size_px;
	snprintf(out, out_size, "%s", uuid ? uuid : "");
}

int ytm_cover_px(void) { return cover_px; }

void ytm_artist_image_url(const char *uuid, int size_px, char *out, size_t out_size) {
	ytm_cover_url(uuid, size_px, out, out_size);
}

// ---------------------------------------------------------------------------
// writing to the account
// ---------------------------------------------------------------------------

static bool simple_action(const char *endpoint, const char *fields) {
	if (need_login()) {
		return false;
	}
	char *text;
	json_doc_t doc;
	if (!music_call(endpoint, NULL, fields, &text, &doc)) {
		return false;
	}
	json_free(&doc);
	free(text);
	return true;
}

static bool like_video(long track_id, const char *endpoint) {
	clear_error();
	char vid[YTM_ID_MAX], fields[160];
	if (!ytm_key_for(track_id, vid, sizeof(vid))) {
		set_error("%s", tr("ytm_track_without_id"));
		return false;
	}
	snprintf(fields, sizeof(fields), "\"target\":{\"videoId\":\"%s\"}", vid);
	return simple_action(endpoint, fields);
}

bool ytm_favorite_add_track(long track_id) { return like_video(track_id, "like/like"); }
bool ytm_favorite_remove_track(long track_id) { return like_video(track_id, "like/removelike"); }

int ytm_favorite_track_ids(long *out, int max) {
	clear_error();
	if (need_login()) {
		return -1;
	}
	ytm_track_t *tracks = calloc((size_t)max, sizeof(*tracks));
	if (!tracks) {
		return -1;
	}
	int n = tracks_of_browse("VLLM", 0, tracks, max);
	for (int i = 0; i < n; i++) {
		out[i] = tracks[i].id;
	}
	free(tracks);
	return n;
}

// Saving an album means liking its audio playlist (OLAK5uy_...), which only the
// album page names. Not worth a second request per tap for now.
bool ytm_favorite_add_album(const char *album_id) {
	(void)album_id;
	set_error("%s", tr("ytm_not_supported"));
	return false;
}

bool ytm_favorite_remove_album(const char *album_id) {
	(void)album_id;
	set_error("%s", tr("ytm_not_supported"));
	return false;
}

static const char *bare_playlist(const char *id) { return strncmp(id, "VL", 2) == 0 ? id + 2 : id; }

bool ytm_playlist_delete(const char *playlist_id) {
	clear_error();
	char fields[200];
	snprintf(fields, sizeof(fields), "\"playlistId\":\"%s\"", bare_playlist(playlist_id));
	return simple_action("playlist/delete", fields);
}

bool ytm_playlist_create(const char *name, char *out_id, size_t out_size) {
	clear_error();
	if (need_login()) {
		return false;
	}
	char title[400], fields[600];
	json_escape(name, title, sizeof(title));
	snprintf(fields, sizeof(fields), "\"title\":\"%s\",\"description\":\"\",\"privacyStatus\":\"PRIVATE\"", title);
	char *text;
	json_doc_t doc;
	if (!music_call("playlist/create", NULL, fields, &text, &doc)) {
		return false;
	}
	char id[YTM_ID_MAX] = "";
	json_obj_str(&doc, json_root(&doc), "playlistId", id, sizeof(id));
	json_free(&doc);
	free(text);
	if (!id[0]) {
		set_error("%s", tr("ytm_change_refused"));
		return false;
	}
	snprintf(out_id, out_size, "VL%s", id);
	return true;
}

bool ytm_playlist_add_track(const char *playlist_id, long track_id) {
	clear_error();
	char vid[YTM_ID_MAX], fields[300];
	if (!ytm_key_for(track_id, vid, sizeof(vid))) {
		set_error("%s", tr("ytm_track_without_id"));
		return false;
	}
	snprintf(fields, sizeof(fields),
			 "\"playlistId\":\"%s\",\"actions\":[{\"action\":\"ACTION_ADD_VIDEO\",\"addedVideoId\":\"%s\"}]",
			 bare_playlist(playlist_id), vid);
	return simple_action("browse/edit_playlist", fields);
}
