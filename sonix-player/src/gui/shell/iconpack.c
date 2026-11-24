#include "src/gui/shell/iconpack.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "lvgl/src/display/lv_display_private.h"
#include "lvgl/src/misc/cache/instance/lv_image_cache.h"

#include "src/gui/shell/gridpage.h"
#include "src/system/core/config.h"
#include "src/system/core/respath.h"
#include "src/system/image/stb_image_decl.h"

#define PACK_DIR SONIX_RESOURCE_DIR "/icons"
#define PNG_MAX_BYTES (2 * 1024 * 1024)

static char names[ICONPACK_MAX][64];
static int name_count;
static char current[64];
static bool remembered;

static void remember_builtin(void) {
	if (remembered) {
		return;
	}
	for (int i = 0; i < iconpack_table_count; i++) {
		iconpack_table[i].builtin_data = iconpack_table[i].dsc->data;
	}
	remembered = true;
}

int iconpack_count(void) {
	name_count = 0;
	DIR *d = opendir(PACK_DIR);
	if (!d) {
		return 0;
	}
	struct dirent *e;
	while ((e = readdir(d)) && name_count < ICONPACK_MAX) {
		if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(names[0])) {
			continue;
		}
		char path[600];
		snprintf(path, sizeof(path), "%s/%.200s", PACK_DIR, e->d_name);
		struct stat st;
		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
			snprintf(names[name_count++], sizeof(names[0]), "%s", e->d_name);
		}
	}
	closedir(d);
	for (int i = 1; i < name_count; i++) {
		for (int j = i; j > 0 && strcasecmp(names[j - 1], names[j]) > 0; j--) {
			char t[64];
			memcpy(t, names[j], sizeof(t));
			memcpy(names[j], names[j - 1], sizeof(t));
			memcpy(names[j - 1], t, sizeof(t));
		}
	}
	return name_count;
}

const char *iconpack_name(int index) { return index >= 0 && index < name_count ? names[index] : ""; }
const char *iconpack_current(void) { return current; }

static uint8_t *read_png(const char *path, int *w, int *h) {
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > PNG_MAX_BYTES) {
		return NULL;
	}
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	uint8_t *file = malloc((size_t)st.st_size);
	if (!file || fread(file, 1, (size_t)st.st_size, f) != (size_t)st.st_size) {
		free(file);
		fclose(f);
		return NULL;
	}
	fclose(f);
	int channels = 0;
	uint8_t *rgba = stbi_load_from_memory(file, (int)st.st_size, w, h, &channels, 4);
	free(file);
	if (!rgba || *w <= 0 || *h <= 0 || *w > 1024 || *h > 1024) {
		stbi_image_free(rgba);
		return NULL;
	}
	return rgba;
}

// Scales RGBA (sw x sh) into LVGL's ARGB8888 (B, G, R, A) at dw x dh. Each
// output pixel is the average of the source area under it, edge pixels
// counted by how much of them it covers (8-bit fractions), colours weighted
// by alpha. Enlarging falls out of the same sum as plain interpolation-free
// replication, which is fine: packs are meant to be drawn larger than needed.
static void scale_into(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh, int stride, bool glyph) {
	for (int y = 0; y < dh; y++) {
		// Source span of this row in 24.8 fixed point.
		int64_t fy0 = (int64_t)y * sh * 256 / dh, fy1 = (int64_t)(y + 1) * sh * 256 / dh;
		for (int x = 0; x < dw; x++) {
			int64_t fx0 = (int64_t)x * sw * 256 / dw, fx1 = (int64_t)(x + 1) * sw * 256 / dw;
			uint64_t r = 0, g = 0, b = 0, a = 0, wsum = 0;
			for (int yy = (int)(fy0 >> 8); yy < sh && ((int64_t)yy << 8) < fy1; yy++) {
				int64_t top = (int64_t)yy << 8, bottom = top + 256;
				uint32_t wy = (uint32_t)((bottom < fy1 ? bottom : fy1) - (top > fy0 ? top : fy0));
				for (int xx = (int)(fx0 >> 8); xx < sw && ((int64_t)xx << 8) < fx1; xx++) {
					int64_t left = (int64_t)xx << 8, right = left + 256;
					uint32_t wx = (uint32_t)((right < fx1 ? right : fx1) - (left > fx0 ? left : fx0));
					uint64_t w = (uint64_t)wx * wy;
					const uint8_t *p = src + ((size_t)yy * sw + xx) * 4;
					r += w * p[0] * p[3];
					g += w * p[1] * p[3];
					b += w * p[2] * p[3];
					a += w * p[3];
					wsum += w;
				}
			}
			uint8_t *o = dst + (size_t)y * stride + (size_t)x * 4;
			if (glyph) {
				o[0] = o[1] = o[2] = 255;
			} else if (a) {
				o[0] = (uint8_t)(b / a);
				o[1] = (uint8_t)(g / a);
				o[2] = (uint8_t)(r / a);
			} else {
				o[0] = o[1] = o[2] = 0;
			}
			o[3] = (uint8_t)(wsum ? a / wsum : 0);
		}
	}
}

// Puts every icon back to the built-in picture and frees the pack's pixels.
static void restore_builtin(void) {
	for (int i = 0; i < iconpack_table_count; i++) {
		iconpack_entry_t *e = &iconpack_table[i];
		if (e->pack_data) {
			e->dsc->data = e->builtin_data;
			lv_image_cache_drop(e->dsc);
			free(e->pack_data);
			e->pack_data = NULL;
		}
	}
}

// --- Decoded-pack cache -----------------------------------------------------
// Decoding and scaling a pack's PNGs takes seconds on the player, every start.
// After the first load the finished pixels are kept in <pack>/icons.cache and
// later starts read that one file. The cache is tied to a signature of every
// icon's target size and its PNG's size and date, so a changed, added or
// removed PNG (or a build that draws an icon at another size) rebuilds it.

#define CACHE_MAGIC 0x31434953u // "SIC1"

static uint32_t sig_add(uint32_t h, const void *data, size_t n) {
	const uint8_t *p = data;
	while (n--) {
		h = (h ^ *p++) * 16777619u;
	}
	return h;
}

static void entry_path(char *path, size_t size, const char *name, const iconpack_entry_t *e) {
	snprintf(path, size, "%s/%.63s/%s%s.png", PACK_DIR, name, e->glyph ? "" : "tiles/", e->file);
}

static uint32_t pack_signature(const char *name) {
	uint32_t h = 2166136261u;
	for (int i = 0; i < iconpack_table_count; i++) {
		iconpack_entry_t *e = &iconpack_table[i];
		char path[700];
		entry_path(path, sizeof(path), name, e);
		struct stat st;
		int64_t meta[2] = {-1, -1};
		if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
			meta[0] = (int64_t)st.st_size;
			meta[1] = (int64_t)st.st_mtime;
		}
		uint32_t dims[4] = {e->dsc->header.w, e->dsc->header.h, e->dsc->header.stride, e->glyph};
		h = sig_add(h, e->file, strlen(e->file) + 1);
		h = sig_add(h, dims, sizeof(dims));
		h = sig_add(h, meta, sizeof(meta));
	}
	return h;
}

static void cache_path(char *path, size_t size, const char *name) {
	snprintf(path, size, "%s/%.63s/icons.cache", PACK_DIR, name);
}

// File: magic, signature, count, then per icon: table index, pixel bytes.
// Glyphs keep only their alpha (colour is always white), a quarter the size.
static int cache_load(const char *name, uint32_t sig) {
	char path[700];
	cache_path(path, sizeof(path), name);
	FILE *f = fopen(path, "rb");
	if (!f) {
		return -1;
	}
	uint32_t head[3];
	int replaced = 0;
	bool ok = fread(head, sizeof(head), 1, f) == 1 && head[0] == CACHE_MAGIC && head[1] == sig &&
			  head[2] <= (uint32_t)iconpack_table_count;
	for (uint32_t n = 0; ok && n < head[2]; n++) {
		uint32_t rec[2];
		ok = fread(rec, sizeof(rec), 1, f) == 1 && rec[0] < (uint32_t)iconpack_table_count;
		if (!ok) {
			break;
		}
		iconpack_entry_t *e = &iconpack_table[rec[0]];
		size_t dw = e->dsc->header.w, dh = e->dsc->header.h, stride = e->dsc->header.stride;
		size_t want = e->glyph ? dw * dh : stride * dh;
		ok = rec[1] == want && !e->pack_data && stride >= dw * 4;
		if (!ok) {
			break;
		}
		uint8_t *pixels = calloc(1, stride * dh);
		if (!pixels) {
			ok = false;
			break;
		}
		if (e->glyph) {
			// Read the alphas into the tail of the buffer, then spread them
			// out front to back: the write position never passes the read one.
			uint8_t *alpha = pixels + stride * dh - want;
			ok = fread(alpha, 1, want, f) == want;
			for (size_t y = 0; ok && y < dh; y++) {
				uint8_t *o = pixels + y * stride;
				for (size_t x = 0; x < dw; x++) {
					uint8_t a = alpha[y * dw + x];
					o[x * 4] = o[x * 4 + 1] = o[x * 4 + 2] = 255;
					o[x * 4 + 3] = a;
				}
			}
		} else {
			ok = fread(pixels, 1, want, f) == want;
		}
		if (!ok) {
			free(pixels);
			break;
		}
		e->pack_data = pixels;
		e->dsc->data = pixels;
		lv_image_cache_drop(e->dsc);
		replaced++;
	}
	fclose(f);
	if (!ok) {
		restore_builtin();
		return -1;
	}
	return replaced;
}

static void cache_save(const char *name, uint32_t sig, int replaced) {
	char path[700], tmp[720];
	cache_path(path, sizeof(path), name);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (!f) {
		return;
	}
	uint32_t head[3] = {CACHE_MAGIC, sig, (uint32_t)replaced};
	bool ok = fwrite(head, sizeof(head), 1, f) == 1;
	for (int i = 0; ok && i < iconpack_table_count; i++) {
		iconpack_entry_t *e = &iconpack_table[i];
		if (!e->pack_data) {
			continue;
		}
		size_t dw = e->dsc->header.w, dh = e->dsc->header.h, stride = e->dsc->header.stride;
		uint32_t rec[2] = {(uint32_t)i, (uint32_t)(e->glyph ? dw * dh : stride * dh)};
		ok = fwrite(rec, sizeof(rec), 1, f) == 1;
		if (!e->glyph) {
			ok = ok && fwrite(e->pack_data, 1, rec[1], f) == rec[1];
			continue;
		}
		for (size_t y = 0; ok && y < dh; y++) {
			uint8_t row[1024];
			for (size_t x = 0; x < dw && x < sizeof(row); x++) {
				row[x] = e->pack_data[y * stride + x * 4 + 3];
			}
			ok = dw <= sizeof(row) && fwrite(row, 1, dw, f) == dw;
		}
	}
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp, path) != 0) {
		remove(tmp);
	}
}

static int load_pack(const char *name) {
	if (!name[0] || strchr(name, '/')) {
		return -1;
	}
	uint32_t sig = pack_signature(name);
	int cached = cache_load(name, sig);
	if (cached > 0) {
		fprintf(stderr, "iconpack: %s from cache\n", name);
		return cached;
	}
	int replaced = 0;
	for (int i = 0; i < iconpack_table_count; i++) {
		iconpack_entry_t *e = &iconpack_table[i];
		char path[700];
		// Coloured artwork (the big tiles) lives in the pack's "tiles" folder:
		// several tiles share a name with a glyph (album, artist, bluetooth).
		entry_path(path, sizeof(path), name, e);
		int w = 0, h = 0;
		uint8_t *rgba = read_png(path, &w, &h);
		if (!rgba) {
			continue;
		}
		int dw = e->dsc->header.w, dh = e->dsc->header.h, stride = e->dsc->header.stride;
		uint8_t *pixels = calloc(1, (size_t)stride * (size_t)dh);
		if (pixels) {
			scale_into(rgba, w, h, pixels, dw, dh, stride, e->glyph);
			e->pack_data = pixels;
			e->dsc->data = pixels;
			lv_image_cache_drop(e->dsc);
			replaced++;
		}
		stbi_image_free(rgba);
	}
	if (replaced) {
		cache_save(name, sig, replaced);
	}
	return replaced ? replaced : -1;
}

static void redraw_everything(void) {
	for (lv_display_t *disp = lv_display_get_next(NULL); disp; disp = lv_display_get_next(disp)) {
		for (uint32_t i = 0; i < disp->screen_cnt; i++) {
			lv_obj_invalidate(disp->screens[i]);
		}
	}
}

void iconpack_init(void) {
	remember_builtin();
	const char *saved = config_get("ui", "icon_pack", "");
	if (saved && saved[0]) {
		int n = load_pack(saved);
		if (n > 0) {
			snprintf(current, sizeof(current), "%s", saved);
			fprintf(stderr, "iconpack: %s, %d icons\n", current, n);
		} else {
			fprintf(stderr, "iconpack: %s has no usable icons, built-in set\n", saved);
		}
	}
}

int iconpack_set(const char *name) {
	if (!name) {
		name = "";
	}
	remember_builtin();
	restore_builtin();
	current[0] = 0;
	int n = 0;
	if (name[0]) {
		n = load_pack(name);
		if (n > 0) {
			snprintf(current, sizeof(current), "%s", name);
		}
	}
	config_set("ui", "icon_pack", current);
	config_save();
	// The menu tiles drawn in one colour are worked out from these pixels.
	gridpage_icons_changed();
	redraw_everything();
	fprintf(stderr, "iconpack: now %s (%d icons)\n", current[0] ? current : "built-in", n);
	return n;
}

// A picture of one icon as `pack` draws it ("" = the built-in set), scaled to
// size x size, for the pack picker. NULL when the pack has no such icon.
lv_image_dsc_t *iconpack_preview(const char *pack, const char *file, bool tile, int size) {
	if (!pack || !file || size <= 0 || size > 256 || strchr(pack, '/')) {
		return NULL;
	}
	remember_builtin();
	int w = 0, h = 0;
	uint8_t *rgba = NULL;
	if (pack[0]) {
		char path[700];
		snprintf(path, sizeof(path), "%s/%.63s/%s%s.png", PACK_DIR, pack, tile ? "tiles/" : "", file);
		rgba = read_png(path, &w, &h);
	}
	if (!rgba) {
		if (pack[0] && !tile) {
			return NULL;
		}
		// Built-in picture (also the tile of a pack that brings no tiles):
		// LVGL's B, G, R, A back to R, G, B, A for scale_into.
		for (int i = 0; i < iconpack_table_count && !rgba; i++) {
			iconpack_entry_t *e = &iconpack_table[i];
			if (e->glyph == tile || strcmp(e->file, file) != 0 || e->dsc->header.cf != LV_COLOR_FORMAT_ARGB8888) {
				continue;
			}
			w = e->dsc->header.w;
			h = e->dsc->header.h;
			int stride = e->dsc->header.stride;
			rgba = malloc((size_t)w * (size_t)h * 4);
			if (!rgba) {
				return NULL;
			}
			for (int y = 0; y < h; y++) {
				const uint8_t *s = e->builtin_data + (size_t)y * stride;
				uint8_t *d = rgba + (size_t)y * w * 4;
				for (int x = 0; x < w; x++) {
					d[x * 4] = s[x * 4 + 2];
					d[x * 4 + 1] = s[x * 4 + 1];
					d[x * 4 + 2] = s[x * 4];
					d[x * 4 + 3] = s[x * 4 + 3];
				}
			}
		}
		if (!rgba) {
			return NULL;
		}
	}
	lv_image_dsc_t *dsc = calloc(1, sizeof(*dsc) + (size_t)size * (size_t)size * 4);
	if (dsc) {
		uint8_t *pixels = (uint8_t *)(dsc + 1);
		scale_into(rgba, w, h, pixels, size, size, size * 4, !tile);
		dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
		dsc->header.cf = LV_COLOR_FORMAT_ARGB8888;
		dsc->header.w = (uint32_t)size;
		dsc->header.h = (uint32_t)size;
		dsc->header.stride = (uint32_t)size * 4;
		dsc->data = pixels;
		dsc->data_size = (uint32_t)size * (uint32_t)size * 4;
	}
	free(rgba); // stb's allocator is malloc
	return dsc;
}

void iconpack_preview_free(lv_image_dsc_t *dsc) {
	if (dsc) {
		lv_image_cache_drop(dsc);
		free(dsc);
	}
}
