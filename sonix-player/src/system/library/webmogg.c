#include "webmogg.h"

#include <stdlib.h>
#include <string.h>

// --- EBML --------------------------------------------------------------------

#define ID_EBML 0x1A45DFA3
#define ID_SEGMENT 0x18538067
#define ID_TRACKS 0x1654AE6B
#define ID_TRACK_ENTRY 0xAE
#define ID_TRACK_NUMBER 0xD7
#define ID_CODEC_ID 0x86
#define ID_CODEC_PRIVATE 0x63A2
#define ID_CLUSTER 0x1F43B675
#define ID_SIMPLE_BLOCK 0xA3
#define ID_BLOCK_GROUP 0xA0
#define ID_BLOCK 0xA1

// An element ID: the length marker stays part of the value, as the
// specification writes IDs. False when it runs past `end` or is malformed.
static bool read_id(const uint8_t *d, size_t *at, size_t end, uint32_t *id) {
	if (*at >= end) {
		return false;
	}
	uint8_t first = d[*at];
	int len = 1;
	while (len <= 4 && !(first & (0x80 >> (len - 1)))) {
		len++;
	}
	if (len > 4 || *at + (size_t)len > end) {
		return false;
	}
	uint32_t v = 0;
	for (int i = 0; i < len; i++) {
		v = (v << 8) | d[*at + (size_t)i];
	}
	*at += (size_t)len;
	*id = v;
	return true;
}

// A size (or a block's track number): the marker bit is dropped. `unknown`
// is set for the all-ones value, which means "until the parent ends".
static bool read_size(const uint8_t *d, size_t *at, size_t end, uint64_t *size, bool *unknown) {
	if (*at >= end) {
		return false;
	}
	uint8_t first = d[*at];
	int len = 1;
	while (len <= 8 && !(first & (0x80 >> (len - 1)))) {
		len++;
	}
	if (len > 8 || *at + (size_t)len > end) {
		return false;
	}
	uint64_t v = first & (0xFF >> len);
	bool all_ones = v == (uint64_t)(0xFF >> len);
	for (int i = 1; i < len; i++) {
		uint8_t b = d[*at + (size_t)i];
		v = (v << 8) | b;
		all_ones = all_ones && b == 0xFF;
	}
	*at += (size_t)len;
	*size = v;
	if (unknown) {
		*unknown = all_ones;
	}
	return true;
}

typedef struct {
	uint32_t id;
	size_t payload; // first byte of the content
	size_t end;		// one past the last byte
} element_t;

static bool read_element(const uint8_t *d, size_t at, size_t end, element_t *e) {
	uint64_t size;
	bool unknown;
	if (!read_id(d, &at, end, &e->id) || !read_size(d, &at, end, &size, &unknown)) {
		return false;
	}
	e->payload = at;
	if (unknown || size > end - at) {
		// Unknown size (a live stream's cluster) or a file cut short: the
		// element runs to the end of what encloses it.
		e->end = end;
	} else {
		e->end = at + (size_t)size;
	}
	return true;
}

static uint64_t read_uint(const uint8_t *d, size_t from, size_t end) {
	uint64_t v = 0;
	for (size_t i = from; i < end && i < from + 8; i++) {
		v = (v << 8) | d[i];
	}
	return v;
}

// --- Ogg ---------------------------------------------------------------------

static uint32_t crc_table[256];

static void crc_init(void) {
	if (crc_table[1]) {
		return;
	}
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t r = i << 24;
		for (int k = 0; k < 8; k++) {
			r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : (r << 1);
		}
		crc_table[i] = r;
	}
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n) {
	for (size_t i = 0; i < n; i++) {
		crc = (crc << 8) ^ crc_table[((crc >> 24) & 0xFF) ^ p[i]];
	}
	return crc;
}

#define PAGE_MAX_PACKETS 50 // about a second of 20 ms packets

typedef struct {
	FILE *out;
	uint32_t serial;
	uint32_t sequence;
	// The page being filled.
	uint8_t lacing[255];
	int segments;
	int packets;
	uint8_t *body;
	size_t body_len;
	size_t body_cap;
	uint64_t granule;
} ogg_t;

static bool page_flush(ogg_t *o, uint8_t flags) {
	if (o->segments == 0 && !(flags & 0x04)) {
		return true;
	}
	uint8_t h[27 + 255];
	memcpy(h, "OggS", 4);
	h[4] = 0;
	h[5] = flags;
	for (int i = 0; i < 8; i++) {
		h[6 + i] = (uint8_t)(o->granule >> (8 * i));
	}
	for (int i = 0; i < 4; i++) {
		h[14 + i] = (uint8_t)(o->serial >> (8 * i));
		h[18 + i] = (uint8_t)(o->sequence >> (8 * i));
		h[22 + i] = 0; // the CRC, computed over the page with this zeroed
	}
	h[26] = (uint8_t)o->segments;
	memcpy(h + 27, o->lacing, (size_t)o->segments);
	size_t hlen = 27 + (size_t)o->segments;

	uint32_t crc = crc_update(0, h, hlen);
	crc = crc_update(crc, o->body, o->body_len);
	for (int i = 0; i < 4; i++) {
		h[22 + i] = (uint8_t)(crc >> (8 * i));
	}
	bool ok = fwrite(h, 1, hlen, o->out) == hlen && fwrite(o->body, 1, o->body_len, o->out) == o->body_len;
	o->sequence++;
	o->segments = 0;
	o->packets = 0;
	o->body_len = 0;
	return ok;
}

// Adds one packet to the page, first flushing the page when it would not fit.
// `granule_after` is the position at the end of this packet.
static bool page_add(ogg_t *o, const uint8_t *p, size_t len, uint64_t granule_after) {
	size_t segs = len / 255 + 1;
	if (segs > 255) {
		return false; // over 64 KB: no Opus packet is
	}
	if ((size_t)o->segments + segs > 255 || o->packets >= PAGE_MAX_PACKETS) {
		if (!page_flush(o, 0)) {
			return false;
		}
	}
	if (o->body_len + len > o->body_cap) {
		size_t cap = o->body_cap ? o->body_cap * 2 : 65536;
		while (cap < o->body_len + len) {
			cap *= 2;
		}
		uint8_t *grown = realloc(o->body, cap);
		if (!grown) {
			return false;
		}
		o->body = grown;
		o->body_cap = cap;
	}
	memcpy(o->body + o->body_len, p, len);
	o->body_len += len;
	size_t left = len;
	for (size_t i = 0; i < segs; i++) {
		size_t piece = left >= 255 ? 255 : left;
		o->lacing[o->segments++] = (uint8_t)piece;
		left -= piece;
	}
	o->packets++;
	o->granule = granule_after;
	return true;
}

// --- Opus --------------------------------------------------------------------

// Samples at 48 kHz in one packet, from its TOC byte (RFC 6716, 3.1).
static int opus_samples(const uint8_t *p, size_t len) {
	if (len < 1) {
		return 0;
	}
	int config = p[0] >> 3;
	int frame; // in units of 1/400 s, i.e. 120 samples
	if (config < 12) {
		static const int silk[4] = {4, 8, 16, 24}; // 10, 20, 40, 60 ms
		frame = silk[config & 3];
	} else if (config < 16) {
		frame = (config & 1) ? 8 : 4; // 20 or 10 ms
	} else {
		static const int celt[4] = {1, 2, 4, 8}; // 2.5, 5, 10, 20 ms
		frame = celt[config & 3];
	}
	int count;
	switch (p[0] & 3) {
	case 0:
		count = 1;
		break;
	case 1:
	case 2:
		count = 2;
		break;
	default:
		if (len < 2) {
			return 0;
		}
		count = p[1] & 0x3F;
		break;
	}
	return frame * count * 120;
}

// --- walking the file --------------------------------------------------------

typedef struct {
	const uint8_t *d;
	size_t size;
	uint64_t track;
	ogg_t ogg;
	uint64_t samples;
	long packets;
} walk_t;

static webmogg_result_t do_block(walk_t *w, size_t at, size_t end) {
	uint64_t track;
	if (!read_size(w->d, &at, end, &track, NULL) || at + 3 > end) {
		return WEBMOGG_BROKEN;
	}
	if (track != w->track) {
		return WEBMOGG_OK; // another track: not ours
	}
	uint8_t flags = w->d[at + 2];
	at += 3; // relative timecode (2), flags (1)
	int lacing = (flags >> 1) & 3;
	if (lacing == 0) {
		size_t len = end - at;
		w->samples += (uint64_t)opus_samples(w->d + at, len);
		if (!page_add(&w->ogg, w->d + at, len, w->samples)) {
			return WEBMOGG_WRITE_FAILED;
		}
		w->packets++;
		return WEBMOGG_OK;
	}
	if (lacing == 2) { // fixed-size lacing: N+1 equal frames
		if (at >= end) {
			return WEBMOGG_BROKEN;
		}
		size_t frames = (size_t)w->d[at++] + 1;
		size_t total = end - at;
		if (total % frames != 0) {
			return WEBMOGG_BROKEN;
		}
		size_t len = total / frames;
		for (size_t i = 0; i < frames; i++) {
			const uint8_t *p = w->d + at + i * len;
			w->samples += (uint64_t)opus_samples(p, len);
			if (!page_add(&w->ogg, p, len, w->samples)) {
				return WEBMOGG_WRITE_FAILED;
			}
			w->packets++;
		}
		return WEBMOGG_OK;
	}
	return WEBMOGG_BROKEN; // Xiph or EBML lacing: not something YouTube sends
}

static webmogg_result_t do_cluster(walk_t *w, const element_t *cluster) {
	size_t at = cluster->payload;
	element_t e;
	while (read_element(w->d, at, cluster->end, &e)) {
		at = e.end;
		if (e.id == ID_SIMPLE_BLOCK) {
			webmogg_result_t r = do_block(w, e.payload, e.end);
			if (r != WEBMOGG_OK) {
				return r;
			}
		} else if (e.id == ID_BLOCK_GROUP) {
			size_t gat = e.payload;
			element_t g;
			while (read_element(w->d, gat, e.end, &g)) {
				gat = g.end;
				if (g.id == ID_BLOCK) {
					webmogg_result_t r = do_block(w, g.payload, g.end);
					if (r != WEBMOGG_OK) {
						return r;
					}
				}
			}
		}
	}
	return WEBMOGG_OK;
}

// The Opus track: its number and its OpusHead.
static bool find_opus_track(const uint8_t *d, const element_t *tracks, uint64_t *number, size_t *head,
							size_t *head_len) {
	size_t at = tracks->payload;
	element_t entry;
	while (read_element(d, at, tracks->end, &entry)) {
		at = entry.end;
		if (entry.id != ID_TRACK_ENTRY) {
			continue;
		}
		uint64_t num = 0;
		bool opus = false;
		size_t priv = 0, priv_len = 0;
		size_t eat = entry.payload;
		element_t f;
		while (read_element(d, eat, entry.end, &f)) {
			eat = f.end;
			if (f.id == ID_TRACK_NUMBER) {
				num = read_uint(d, f.payload, f.end);
			} else if (f.id == ID_CODEC_ID) {
				opus = f.end - f.payload == 6 && memcmp(d + f.payload, "A_OPUS", 6) == 0;
			} else if (f.id == ID_CODEC_PRIVATE) {
				priv = f.payload;
				priv_len = f.end - f.payload;
			}
		}
		if (opus && num > 0 && priv_len >= 19 && memcmp(d + priv, "OpusHead", 8) == 0) {
			*number = num;
			*head = priv;
			*head_len = priv_len;
			return true;
		}
	}
	return false;
}

webmogg_result_t webmogg_convert(const uint8_t *d, size_t size, FILE *out, long *packets_out) {
	if (packets_out) {
		*packets_out = 0;
	}
	crc_init();

	size_t at = 0;
	element_t e;
	if (!read_element(d, at, size, &e) || e.id != ID_EBML) {
		return WEBMOGG_BROKEN;
	}
	at = e.end;
	element_t segment;
	bool found = false;
	while (read_element(d, at, size, &segment)) {
		at = segment.end;
		if (segment.id == ID_SEGMENT) {
			found = true;
			break;
		}
	}
	if (!found) {
		return WEBMOGG_BROKEN;
	}

	walk_t w;
	memset(&w, 0, sizeof(w));
	w.d = d;
	w.size = size;
	w.ogg.out = out;
	w.ogg.serial = 0x59544D55; // "YTMU": any number will do for one stream

	size_t head = 0, head_len = 0;
	bool have_track = false;
	webmogg_result_t result = WEBMOGG_OK;

	at = segment.payload;
	while (read_element(d, at, segment.end, &e)) {
		at = e.end;
		if (e.id == ID_TRACKS && !have_track) {
			if (!find_opus_track(d, &e, &w.track, &head, &head_len)) {
				result = WEBMOGG_NOT_OPUS;
				break;
			}
			have_track = true;
			// Page 0: the OpusHead alone. Page 1: OpusTags, empty but for
			// the vendor. Both at granule 0, as RFC 7845 asks.
			static const char vendor[] = "sonix webmogg";
			uint8_t tags[8 + 4 + sizeof(vendor) - 1 + 4];
			size_t vl = sizeof(vendor) - 1;
			memcpy(tags, "OpusTags", 8);
			tags[8] = (uint8_t)vl;
			tags[9] = tags[10] = tags[11] = 0;
			memcpy(tags + 12, vendor, vl);
			memset(tags + 12 + vl, 0, 4);
			if (!page_add(&w.ogg, d + head, head_len, 0) || !page_flush(&w.ogg, 0x02) ||
				!page_add(&w.ogg, tags, sizeof(tags), 0) || !page_flush(&w.ogg, 0)) {
				result = WEBMOGG_WRITE_FAILED;
				break;
			}
		} else if (e.id == ID_CLUSTER) {
			if (!have_track) {
				result = WEBMOGG_NOT_OPUS;
				break;
			}
			result = do_cluster(&w, &e);
			if (result != WEBMOGG_OK) {
				break;
			}
		}
	}

	if (result == WEBMOGG_OK) {
		if (!have_track) {
			result = WEBMOGG_NOT_OPUS;
		} else if (w.packets == 0) {
			result = WEBMOGG_BROKEN;
		} else if (!page_flush(&w.ogg, 0x04)) {
			result = WEBMOGG_WRITE_FAILED;
		}
	}
	free(w.ogg.body);
	if (packets_out) {
		*packets_out = w.packets;
	}
	return result;
}
