#include "hibykeys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The stock player is five to six megabytes. Anything much larger is not it.
#define BIN_MAX (24 * 1024 * 1024)

// The text of the stock player's own Qobuz login request. The keys are the
// strings the linker placed right after it.
static const char ANCHOR[] = "/user/login?app_id=%s";

// How far past the anchor to look. The two values sit within a hundred bytes
// of it; the rest is room for a build that orders its strings a little
// differently.
#define WINDOW 4096

static bool all_of(const char *s, size_t len, const char *set) { return strspn(s, set) == len; }

static const unsigned char *find(const unsigned char *hay, size_t hay_len, const char *needle, size_t needle_len) {
	if (needle_len == 0 || hay_len < needle_len) {
		return NULL;
	}
	const unsigned char *end = hay + hay_len - needle_len;
	for (const unsigned char *p = hay; p <= end; p++) {
		p = memchr(p, needle[0], (size_t)(end - p) + 1);
		if (!p) {
			return NULL;
		}
		if (memcmp(p, needle, needle_len) == 0) {
			return p;
		}
	}
	return NULL;
}

// Walks the NUL-terminated strings in [p, end) and keeps the first that looks
// like an application id (digits only) and the first that looks like its
// secret (32 lower-case hex digits).
static bool pick(const unsigned char *p, const unsigned char *end, char *id, size_t id_size, char *secret,
				 size_t secret_size) {
	id[0] = '\0';
	secret[0] = '\0';
	while (p < end && !(id[0] && secret[0])) {
		const unsigned char *nul = memchr(p, '\0', (size_t)(end - p));
		if (!nul) {
			break;
		}
		const char *s = (const char *)p;
		size_t len = (size_t)(nul - p);
		if (!id[0] && len >= 6 && len <= 12 && len < id_size && all_of(s, len, "0123456789")) {
			memcpy(id, s, len + 1);
		} else if (!secret[0] && len == 32 && len < secret_size && all_of(s, len, "0123456789abcdef")) {
			memcpy(secret, s, len + 1);
		}
		p = nul + 1;
	}
	return id[0] && secret[0];
}

bool hibykeys_qobuz(char *id, size_t id_size, char *secret, size_t secret_size) {
	id[0] = '\0';
	secret[0] = '\0';

	const char *path = getenv("SONIX_HIBY_BIN");
	if (!path || !*path) {
		path = HIBYKEYS_BIN;
	}
	FILE *f = fopen(path, "rb");
	if (!f) {
		return false;
	}
	bool found = false;
	long size = -1;
	if (fseek(f, 0, SEEK_END) == 0) {
		size = ftell(f);
	}
	unsigned char *data = NULL;
	if (size > 0 && size <= BIN_MAX && fseek(f, 0, SEEK_SET) == 0) {
		data = malloc((size_t)size);
	}
	if (data && fread(data, 1, (size_t)size, f) == (size_t)size) {
		const unsigned char *end = data + size;
		const unsigned char *at = data;
		// The anchor may occur more than once; the keys follow one of them.
		while (!found && (at = find(at, (size_t)(end - at), ANCHOR, sizeof(ANCHOR) - 1)) != NULL) {
			at += sizeof(ANCHOR) - 1;
			const unsigned char *stop = (size_t)(end - at) > WINDOW ? at + WINDOW : end;
			found = pick(at, stop, id, id_size, secret, secret_size);
		}
	}
	if (data) {
		// The whole firmware binary was in here, the keys with it.
		volatile unsigned char *v = data;
		for (long i = 0; i < size; i++) {
			v[i] = 0;
		}
		free(data);
	}
	fclose(f);
	if (!found) {
		id[0] = '\0';
		secret[0] = '\0';
	}
	return found;
}
