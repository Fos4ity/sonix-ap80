#include "tidalsource.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A source file with two key lines in it is a few kilobytes. This is room to
// spare, and a bound on what a wrong file can make the player read.
#define SOURCE_MAX (512 * 1024)

// The quoted string that starts the text after `from`. Empty when
// there is none on the line, or it does not fit.
static void quoted(const char *from, const char *line_end, char *out, size_t out_size) {
	out[0] = '\0';
	// The value has to be a plain string: the quote comes straight after the
	// '=', so os.environ["..."] or a function call is not mistaken for one.
	const char *p = from;
	while (p < line_end && (*p == ' ' || *p == '\t')) {
		p++;
	}
	if (p >= line_end || (*p != '"' && *p != '\'')) {
		return;
	}
	char q = *p++;
	const char *start = p;
	while (p < line_end && *p != q) {
		p++;
	}
	if (p >= line_end) {
		return; // no closing quote on this line
	}
	size_t len = (size_t)(p - start);
	if (len == 0 || len >= out_size) {
		return;
	}
	memcpy(out, start, len);
	out[len] = '\0';
}

// `name` as an assignment target on this line: at its start (after blanks),
// followed by blanks and '='. That keeps CLIENT_ID from matching inside
// another name and skips lines that only mention it.
static const char *assigned(const char *line, const char *line_end, const char *name) {
	size_t n = strlen(name);
	const char *p = line;
	while (p < line_end && (*p == ' ' || *p == '\t')) {
		p++;
	}
	if ((size_t)(line_end - p) < n || memcmp(p, name, n) != 0) {
		return NULL;
	}
	p += n;
	while (p < line_end && (*p == ' ' || *p == '\t')) {
		p++;
	}
	if (p >= line_end || *p != '=') {
		return NULL;
	}
	return p + 1;
}

bool tidalsource_read(char *id, size_t id_size, char *secret, size_t secret_size) {
	id[0] = '\0';
	secret[0] = '\0';

	const char *path = getenv("SONIX_TIDAL_SOURCE");
	if (!path || !*path) {
		path = TIDALSOURCE_PATH;
	}
	FILE *f = fopen(path, "rb");
	if (!f) {
		return false;
	}
	char *data = malloc(SOURCE_MAX + 1);
	size_t len = data ? fread(data, 1, SOURCE_MAX, f) : 0;
	fclose(f);
	if (!data) {
		return false;
	}
	data[len] = '\0';

	const char *p = data;
	const char *end = data + len;
	while (p < end && !(id[0] && secret[0])) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));
		const char *line_end = nl ? nl : end;
		// A commented-out line starts with '#' and never reaches assigned():
		// it does not start with the name.
		const char *v;
		if (!id[0] && (v = assigned(p, line_end, "CLIENT_ID")) != NULL) {
			quoted(v, line_end, id, id_size);
		} else if (!secret[0] && (v = assigned(p, line_end, "CLIENT_SECRET")) != NULL) {
			quoted(v, line_end, secret, secret_size);
		}
		p = line_end + 1;
	}

	// The file held the keys in the clear.
	volatile char *w = data;
	for (size_t i = 0; i < len; i++) {
		w[i] = 0;
	}
	free(data);

	if (!(id[0] && secret[0])) {
		id[0] = '\0';
		secret[0] = '\0';
		return false;
	}
	return true;
}
