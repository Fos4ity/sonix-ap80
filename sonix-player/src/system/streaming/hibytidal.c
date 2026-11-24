#include "hibytidal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The stock player is five to six megabytes. Anything much larger is not it.
#define BIN_MAX (24 * 1024 * 1024)

// The Tidal client secret is a 44-character base64 string (trailing '=').
#define SECRET_LEN 44

static bool looks_like_secret(const char *s, size_t len) {
	if (len != SECRET_LEN || s[SECRET_LEN - 1] != '=') {
		return false;
	}
	for (size_t i = 0; i < SECRET_LEN - 1; i++) {
		char c = s[i];
		bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/';
		if (!ok) {
			return false;
		}
	}
	return true;
}

bool hibytidal_secret(char *secret, size_t secret_size) {
	secret[0] = '\0';

	const char *path = getenv("SONIX_HIBY_BIN");
	if (!path || !*path) {
		path = HIBYTIDAL_BIN;
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
		// Walk the NUL-terminated strings and count the candidates. The stock
		// player holds exactly one string of this shape in the whole binary;
		// a file that holds none, or several, is some other firmware and is
		// not touched.
		const unsigned char *end = data + size;
		const unsigned char *p = data;
		const unsigned char *hit = NULL;
		int hits = 0;
		while (p < end && hits < 2) {
			const unsigned char *nul = memchr(p, '\0', (size_t)(end - p));
			if (!nul) {
				break;
			}
			if (looks_like_secret((const char *)p, (size_t)(nul - p))) {
				hit = p;
				hits++;
			}
			p = nul + 1;
		}
		if (hits == 1) {
			size_t n = strlen((const char *)hit);
			if (n < secret_size) {
				memcpy(secret, hit, n + 1);
				found = true;
			}
		}
	}
	if (data) {
		// The whole firmware binary was in here, the secret with it.
		volatile unsigned char *v = data;
		for (long i = 0; i < size; i++) {
			v[i] = 0;
		}
		free(data);
	}
	fclose(f);
	if (!found) {
		secret[0] = '\0';
	}
	return found;
}
