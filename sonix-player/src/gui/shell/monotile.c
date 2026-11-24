#include "monotile.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl/src/misc/cache/instance/lv_image_cache.h"

#include "src/gui/shell/icons.h"

// Every tile the interface has, with room to spare; a tile that does not fit
// simply keeps its colours.
#define CACHE_MAX 64

// A pixel counts as part of the picture from this much alpha.
#define OPAQUE_FROM 200

// A glyph covers little of its square and is one colour; a plate covers most
// of it and is mostly one colour. Measured over all six packs and the built-in
// set: glyphs cover 5-27 % and are 96-100 % one colour, plates cover 66-74 %
// and are 80-94 % plate, the built-in artwork is never more than 42 % any one
// colour (its two badges apart, which are plates and are treated as such).
#define GLYPH_MAX_COVER_PCT 35
#define GLYPH_MIN_FLAT_PCT 85
#define PLATE_MIN_COVER_PCT 50
#define PLATE_MIN_FLAT_PCT 70

// What stands out from the plate: nothing below MASK_LOW_PCT of the largest
// difference found on the tile, everything above MASK_HIGH_PCT, a ramp between,
// so the plate's own shading and its soft edge fall away and the drawing keeps
// its smooth outline.
#define MASK_LOW_PCT 30
#define MASK_HIGH_PCT 65

typedef struct {
	const lv_image_dsc_t *tile;
	const uint8_t *data; // the pixels it was worked out from
	monotile_kind_t kind;
	lv_image_dsc_t *mask;
} entry_t;

static entry_t cache[CACHE_MAX];
static int cache_count;

static int channel_gap(const uint8_t *p, int b, int g, int r) {
	int db = abs((int)p[0] - b), dg = abs((int)p[1] - g), dr = abs((int)p[2] - r);
	int d = db > dg ? db : dg;
	return d > dr ? d : dr;
}

static void work_out(entry_t *e) {
	const lv_image_dsc_t *tile = e->tile;
	e->kind = MONOTILE_ARTWORK;
	e->mask = NULL;
	e->data = tile->data;

	int w = tile->header.w, h = tile->header.h, stride = tile->header.stride;
	if (tile->header.cf != LV_COLOR_FORMAT_ARGB8888 || !tile->data || w <= 0 || h <= 0 || w > 512 || h > 512) {
		return;
	}

	// How much of the square is picture, and which colour most of it has: the
	// colours in 16 steps a channel, so a flat area with its dither or its
	// rounding falls into one bin.
	static uint32_t bins[4096];
	memset(bins, 0, sizeof(bins));
	uint32_t opaque = 0;
	for (int y = 0; y < h; y++) {
		const uint8_t *p = tile->data + (size_t)y * stride;
		for (int x = 0; x < w; x++, p += 4) {
			if (p[3] > OPAQUE_FROM) {
				opaque++;
				bins[((p[2] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[0] >> 4)]++;
			}
		}
	}
	if (opaque == 0) {
		return;
	}
	int top = 0;
	for (int i = 1; i < 4096; i++) {
		if (bins[i] > bins[top]) {
			top = i;
		}
	}
	uint32_t cover_pct = opaque * 100u / ((uint32_t)w * (uint32_t)h);
	uint32_t flat_pct = bins[top] * 100u / opaque;

	if (cover_pct < GLYPH_MAX_COVER_PCT && flat_pct >= GLYPH_MIN_FLAT_PCT) {
		e->kind = MONOTILE_GLYPH;
		return;
	}
	if (cover_pct < PLATE_MIN_COVER_PCT || flat_pct < PLATE_MIN_FLAT_PCT) {
		return; // artwork
	}

	// The plate's colour: the mean of that bin.
	uint64_t sb = 0, sg = 0, sr = 0;
	for (int y = 0; y < h; y++) {
		const uint8_t *p = tile->data + (size_t)y * stride;
		for (int x = 0; x < w; x++, p += 4) {
			if (p[3] > OPAQUE_FROM && ((((p[2] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[0] >> 4)) == top)) {
				sb += p[0];
				sg += p[1];
				sr += p[2];
			}
		}
	}
	int pb = (int)(sb / bins[top]), pg = (int)(sg / bins[top]), pr = (int)(sr / bins[top]);

	int gap_max = 1;
	for (int y = 0; y < h; y++) {
		const uint8_t *p = tile->data + (size_t)y * stride;
		for (int x = 0; x < w; x++, p += 4) {
			if (p[3] > OPAQUE_FROM) {
				int d = channel_gap(p, pb, pg, pr);
				if (d > gap_max) {
					gap_max = d;
				}
			}
		}
	}
	int low = gap_max * MASK_LOW_PCT / 100, high = gap_max * MASK_HIGH_PCT / 100;
	if (high <= low) {
		high = low + 1;
	}

	lv_image_dsc_t *mask = calloc(1, sizeof(*mask));
	uint8_t *pixels = calloc((size_t)stride, (size_t)h);
	if (!mask || !pixels) {
		free(mask);
		free(pixels);
		return; // keeps its colours rather than losing its drawing
	}
	for (int y = 0; y < h; y++) {
		const uint8_t *p = tile->data + (size_t)y * stride;
		uint8_t *o = pixels + (size_t)y * stride;
		for (int x = 0; x < w; x++, p += 4, o += 4) {
			o[0] = o[1] = o[2] = 255;
			if (p[3] == 0) {
				continue;
			}
			int d = channel_gap(p, pb, pg, pr);
			int t = d <= low ? 0 : (d >= high ? 255 : (d - low) * 255 / (high - low));
			o[3] = (uint8_t)((int)p[3] * t / 255);
		}
	}
	mask->header = tile->header;
	mask->data_size = (uint32_t)((size_t)stride * (size_t)h);
	mask->data = pixels;

	e->kind = MONOTILE_PLATE;
	e->mask = mask;
}

static entry_t *entry_for(const lv_image_dsc_t *tile) {
	if (!tile) {
		return NULL;
	}
	for (int i = 0; i < cache_count; i++) {
		if (cache[i].tile == tile) {
			// Still the pixels it was worked out from? An icon pack swaps them
			// under the same descriptor; monotile_reset() is the proper way to
			// be told, this is the belt to those braces.
			if (cache[i].data != tile->data && !cache[i].mask) {
				work_out(&cache[i]);
			}
			return &cache[i];
		}
	}
	if (cache_count >= CACHE_MAX) {
		return NULL;
	}
	entry_t *e = &cache[cache_count++];
	e->tile = tile;
	work_out(e);
	return e;
}

monotile_kind_t monotile_kind(const lv_image_dsc_t *tile) {
	entry_t *e = entry_for(tile);
	return e ? e->kind : MONOTILE_ARTWORK;
}

const lv_image_dsc_t *monotile_mask(const lv_image_dsc_t *tile) {
	entry_t *e = entry_for(tile);
	return e && e->kind == MONOTILE_PLATE ? e->mask : NULL;
}

void monotile_reset(void) {
	for (int i = 0; i < cache_count; i++) {
		if (cache[i].mask) {
			lv_image_cache_drop(cache[i].mask);
			free((void *)cache[i].mask->data);
			free(cache[i].mask);
		}
	}
	memset(cache, 0, sizeof(cache));
	cache_count = 0;
}

bool monotile_available(void) { return monotile_kind(&icon_menu_music) != MONOTILE_ARTWORK; }
