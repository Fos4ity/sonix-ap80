#include "src/gui/shell/iconpack.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "lvgl/src/display/lv_display_private.h"
#include "lvgl/src/misc/cache/instance/lv_image_cache.h"

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

static int load_pack(const char *name) {
	if (!name[0] || strchr(name, '/')) {
		return -1;
	}
	int replaced = 0;
	for (int i = 0; i < iconpack_table_count; i++) {
		iconpack_entry_t *e = &iconpack_table[i];
		char path[700];
		// Coloured artwork (the big tiles) lives in the pack's "tiles" folder:
		// several tiles share a name with a glyph (album, artist, bluetooth).
		snprintf(path, sizeof(path), "%s/%.63s/%s%s.png", PACK_DIR, name, e->glyph ? "" : "tiles/", e->file);
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
	redraw_everything();
	fprintf(stderr, "iconpack: now %s (%d icons)\n", current[0] ? current : "built-in", n);
	return n;
}
