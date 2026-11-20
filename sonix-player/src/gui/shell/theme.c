#include "theme.h"

#include "src/gui/shell/gui.h"
#include "src/system/device/bootlogo.h"
#include "src/system/core/config.h"
#include "src/system/core/respath.h"
#include "src/system/image/stb_image_decl.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

lv_style_t theme_style_screen;
lv_style_t theme_style_panel;
lv_style_t theme_style_card;
lv_style_t theme_style_card_pressed;
lv_style_t theme_style_text;
lv_style_t theme_style_text_dim;
lv_style_t theme_style_icon;
lv_style_t theme_style_switch;		   // switch track, off state
lv_style_t theme_style_switch_checked; // switch indicator, on state
lv_style_t theme_style_accent_bg;	   // anything that must follow the accent
lv_style_t theme_style_slider_track;   // a slider's unfilled part

typedef struct {
	const char *name;
	theme_palette_t palette;
	lv_color_t border;
	const char *wallpaper; // relative to SONIX_RESOURCE_DIR "/gui/", or NULL
} theme_preset_t;

// Authentic Omarchy theme palettes (from colors.toml) + Light mode.
static const theme_preset_t THEME_PRESETS[THEME_PRESET_COUNT] = {
	{
		.name = "CpUnk",
		.palette = {
			.screen_bg = LV_COLOR_MAKE(4, 3, 3),            // #040303
			.surface = LV_COLOR_MAKE(20, 15, 15),           // #140f0f
			.surface_pressed = LV_COLOR_MAKE(46, 28, 28),   // dim red-black wash
			.panel = LV_COLOR_MAKE(10, 8, 8),               // #0a0808
			.cover_bg = LV_COLOR_MAKE(10, 8, 8),            // #0a0808
			.text_primary = LV_COLOR_MAKE(255, 255, 255),   // #ffffff
			.text_secondary = LV_COLOR_MAKE(127, 110, 110), // #7f6e6e
			.accent = LV_COLOR_MAKE(248, 42, 42),           // #F82A2A signal red
			.dark = true,
		},
		.border = LV_COLOR_MAKE(127, 110, 110), // #7f6e6e
		.wallpaper = SONIX_RESOURCE_DIR "/gui/wallpaper-cpunk.png",
	},
	{
		.name = "Miasma",
		.palette = {
			.screen_bg = LV_COLOR_MAKE(34, 34, 34),         // #222222
			.surface = LV_COLOR_MAKE(44, 44, 44),           // #2c2c2c
			.surface_pressed = LV_COLOR_MAKE(58, 60, 48),   // subtle olive wash
			.panel = LV_COLOR_MAKE(25, 25, 25),             // #191919
			.cover_bg = LV_COLOR_MAKE(25, 25, 25),          // #191919
			.text_primary = LV_COLOR_MAKE(194, 194, 176),   // #c2c2b0
			.text_secondary = LV_COLOR_MAKE(120, 120, 108), // muted bone
			.accent = LV_COLOR_MAKE(120, 130, 75),          // #78824b moss olive
			.dark = true,
		},
		.border = LV_COLOR_MAKE(102, 102, 102), // #666666
		.wallpaper = SONIX_RESOURCE_DIR "/gui/wallpaper-miasma.png",
	},
	{
		.name = "Ash",
		.palette = {
			.screen_bg = LV_COLOR_MAKE(18, 18, 18),         // #121212
			.surface = LV_COLOR_MAKE(33, 33, 33),           // #212121
			.surface_pressed = LV_COLOR_MAKE(52, 52, 52),   // #343434
			.panel = LV_COLOR_MAKE(13, 13, 13),             // #0d0d0d
			.cover_bg = LV_COLOR_MAKE(13, 13, 13),          // #0d0d0d
			.text_primary = LV_COLOR_MAKE(224, 224, 224),   // #e0e0e0
			.text_secondary = LV_COLOR_MAKE(154, 154, 154), // #9a9a9a
			.accent = LV_COLOR_MAKE(138, 138, 138),         // #8a8a8a monochrome steel
			.dark = true,
		},
		.border = LV_COLOR_MAKE(90, 90, 90), // #5a5a5a
		.wallpaper = SONIX_RESOURCE_DIR "/gui/wallpaper-ash.png",
	},
	{
		.name = "Matte Black",
		.palette = {
			.screen_bg = LV_COLOR_MAKE(18, 18, 18),         // #121212
			.surface = LV_COLOR_MAKE(30, 30, 30),           // #1e1e1e
			.surface_pressed = LV_COLOR_MAKE(46, 40, 32),   // subtle amber wash
			.panel = LV_COLOR_MAKE(13, 13, 13),             // #0d0d0d
			.cover_bg = LV_COLOR_MAKE(13, 13, 13),          // #0d0d0d
			.text_primary = LV_COLOR_MAKE(210, 210, 210),   // #d2d2d2
			.text_secondary = LV_COLOR_MAKE(122, 122, 122), // #7a7a7a
			.accent = LV_COLOR_MAKE(230, 142, 13),          // #e68e0d amber
			.dark = true,
		},
		.border = LV_COLOR_MAKE(68, 68, 68), // #444444
		.wallpaper = SONIX_RESOURCE_DIR "/gui/wallpaper-matte-black.png",
	},
	{
		.name = "Tokyo Night",
		.palette = {
			.screen_bg = LV_COLOR_MAKE(26, 27, 38),         // #1a1b26
			.surface = LV_COLOR_MAKE(36, 40, 59),           // #24283b
			.surface_pressed = LV_COLOR_MAKE(52, 59, 88),   // #343b58
			.panel = LV_COLOR_MAKE(19, 20, 28),             // #13141c
			.cover_bg = LV_COLOR_MAKE(19, 20, 28),          // #13141c
			.text_primary = LV_COLOR_MAKE(192, 202, 245),   // #c0caf5
			.text_secondary = LV_COLOR_MAKE(115, 122, 162), // #737aa2
			.accent = LV_COLOR_MAKE(122, 162, 247),         // #7aa2f7 blue
			.dark = true,
		},
		.border = LV_COLOR_MAKE(65, 72, 104), // #414868
		.wallpaper = SONIX_RESOURCE_DIR "/gui/wallpaper-tokyo-night.png",
	},
	{
		.name = "Gruvbox",
		.palette = {
			.screen_bg = LV_COLOR_MAKE(40, 40, 40),         // #282828
			.surface = LV_COLOR_MAKE(60, 56, 54),           // #3c3836
			.surface_pressed = LV_COLOR_MAKE(80, 73, 69),   // #504945
			.panel = LV_COLOR_MAKE(30, 30, 30),             // #1e1e1e
			.cover_bg = LV_COLOR_MAKE(30, 30, 30),          // #1e1e1e
			.text_primary = LV_COLOR_MAKE(212, 190, 152),   // #d4be98
			.text_secondary = LV_COLOR_MAKE(146, 131, 116), // #928374
			.accent = LV_COLOR_MAKE(216, 166, 87),          // #d8a657 warm gold
			.dark = true,
		},
		.border = LV_COLOR_MAKE(102, 92, 84), // #665c54
		.wallpaper = SONIX_RESOURCE_DIR "/gui/wallpaper-gruvbox.png",
	},
	{
		.name = "Light",
		.palette = {
			.screen_bg = LV_COLOR_MAKE(236, 237, 240),
			.surface = LV_COLOR_MAKE(255, 255, 255),
			.surface_pressed = LV_COLOR_MAKE(221, 223, 228),
			.panel = LV_COLOR_MAKE(255, 255, 255),
			.cover_bg = LV_COLOR_MAKE(224, 226, 230),
			.text_primary = LV_COLOR_MAKE(50, 50, 50),
			.text_secondary = LV_COLOR_MAKE(115, 115, 115),
			.accent = LV_COLOR_MAKE(53, 132, 228),
			.dark = false,
		},
		.border = LV_COLOR_MAKE(200, 200, 200),
		.wallpaper = NULL,
	},
};

static theme_palette_t palette_dark;
static theme_palette_t palette_light;
static int preset_index = 0;      // active preset (0..THEME_PRESET_COUNT-1)
static int last_dark_preset = 0;  // remembered when toggling Light <-> Dark
static bool custom_accent_override = false;
static int custom_hue = -1; // 0..359 when custom hue slider is active, -1 otherwise
static char sd_wallpaper_dir[448];
static char sd_wallpaper_buf[512];

// Whether the dark preset is the chosen one, kept apart from `active` because
// with the dynamic tint on `active` points at a derived copy rather than at
// either preset.
static bool dark_selected = true;

// The dynamic tint: the neutral colours -- backgrounds, cards, the status bar
// -- borrow the accent's hue, the way Material You colours a system from one
// seed. Off by default, remembered in the config.
static bool dynamic_tint;

// The tinted copy of whichever preset is chosen. Rebuilt on every theme,
// accent or tint change; `active` points here while the tint is on.
static theme_palette_t tinted;

static theme_palette_t *active = &palette_dark;

// CpUnk accent presets from colors.toml: signal red (default), chrome steel,
// yellow, bright red, magenta, industrial silver.
static const lv_color_t ACCENT_COLORS[THEME_ACCENT_COUNT] = {
	LV_COLOR_MAKE(248, 42, 42),   // #F82A2A signal red (default)
	LV_COLOR_MAKE(170, 172, 172), // #aaacac chrome steel
	LV_COLOR_MAKE(254, 243, 72),  // #fef348 yellow
	LV_COLOR_MAKE(255, 127, 127), // #ff7f7f bright red / coral
	LV_COLOR_MAKE(247, 110, 120), // #f76e78 magenta
	LV_COLOR_MAKE(209, 210, 209), // #D1D2D1 industrial silver
};
static int accent_index;

lv_color_t theme_accent_preset(int index) {
	if (index < 0 || index >= THEME_ACCENT_COUNT) {
		index = 0;
	}
	return ACCENT_COLORS[index];
}

int theme_get_accent(void) { return accent_index; }
bool theme_has_custom_accent(void) { return custom_accent_override; }
int theme_get_custom_accent_hue(void) { return custom_hue; }
bool theme_has_custom_hue(void) { return custom_accent_override && custom_hue >= 0; }

static lv_color_t color_from_hue(int hue) {
	int h = ((hue % 360) + 360) % 360;
	int sector = h / 60;
	int rem = (h % 60) * 255 / 60;
	int hi = 245;
	int lo = 37;
	int rise = lo + ((hi - lo) * rem) / 255;
	int fall = hi - ((hi - lo) * rem) / 255;
	switch (sector) {
	case 0: return lv_color_make(hi, rise, lo);
	case 1: return lv_color_make(fall, hi, lo);
	case 2: return lv_color_make(lo, hi, rise);
	case 3: return lv_color_make(lo, fall, hi);
	case 4: return lv_color_make(rise, lo, hi);
	default: return lv_color_make(hi, lo, fall);
	}
}

// Callers asking to be called back after a theme or accent change. The list
// grows on demand and has no fixed limit: a dropped registration shows up only
// as a page that keeps the colours it was built with, which is invisible until
// somebody changes the accent. A failed allocation is logged for the same
// reason.
static theme_refresh_cb_t *refresh_cbs;
static int refresh_cb_count;
static int refresh_cb_capacity;

// How much of the accent's own saturation a neutral borrows. Dark surfaces
// take more before the hue stops reading as a shade and starts reading as a
// colour; the light ones are nearly white, where the same amount would turn
// the page pastel.
#define TINT_STRENGTH_DARK 0.30f
#define TINT_STRENGTH_LIGHT 0.15f

// Gives a neutral the accent's hue while keeping exactly how light it is: the
// brightest channel of the result is the brightest channel of the input. The
// colour is never pulled towards the accent's own brightness, which is what
// would turn a dark background light under a pale accent; the two channels
// below the brightest do come down, so a tinted surface is fractionally darker
// than the plain one, and that is what the two strengths above are set from.
static lv_color_t tint_with(lv_color_t c, lv_color_t accent, float strength) {
	uint8_t amax = accent.red > accent.green ? accent.red : accent.green;
	if (accent.blue > amax) {
		amax = accent.blue;
	}
	uint8_t amin = accent.red < accent.green ? accent.red : accent.green;
	if (accent.blue < amin) {
		amin = accent.blue;
	}
	uint8_t cmax = c.red > c.green ? c.red : c.green;
	if (c.blue > cmax) {
		cmax = c.blue;
	}
	if (amax == amin || cmax == 0) {
		return c; // a grey accent has no hue to lend, and black takes none
	}

	// The accent's shape: 1 on its brightest channel, 0 on its darkest. Applied
	// to the neutral's own brightness it reproduces that hue at the neutral's
	// level, at a fraction of the accent's saturation.
	float span = (float)(amax - amin);
	float sat = span / (float)amax * strength;
	float shape[3] = {
		((float)accent.red - (float)amin) / span,
		((float)accent.green - (float)amin) / span,
		((float)accent.blue - (float)amin) / span,
	};

	uint8_t out[3];
	for (int i = 0; i < 3; i++) {
		out[i] = (uint8_t)((float)cmax * (1.0f - sat * (1.0f - shape[i])) + 0.5f);
	}
	return lv_color_make(out[0], out[1], out[2]);
}

// Points `active` at the chosen preset, or at a tinted copy of it.
static void rebuild_active(void) {
	theme_palette_t *base = dark_selected ? &palette_dark : &palette_light;

	if (!dynamic_tint) {
		active = base;
		return;
	}

	float strength = base->dark ? TINT_STRENGTH_DARK : TINT_STRENGTH_LIGHT;
	tinted = *base;
	tinted.screen_bg = tint_with(base->screen_bg, base->accent, strength);
	tinted.surface = tint_with(base->surface, base->accent, strength);
	tinted.surface_pressed = tint_with(base->surface_pressed, base->accent, strength);
	tinted.panel = tint_with(base->panel, base->accent, strength);
	tinted.cover_bg = tint_with(base->cover_bg, base->accent, strength);
	// Text keeps the plain colours: the monochrome icons are recoloured with
	// text_primary, and they are meant to come through the tint untouched.
	active = &tinted;
}

// Optional menu wallpaper (480x800 for HiBy R1), pre-blended at configurable
// opacity against screen_bg so LVGL can blit at LV_OPA_COVER with zero
// per-frame alpha math.
static lv_image_dsc_t wallpaper_dsc;
static uint16_t *wallpaper_raw_pixels;
static uint16_t *wallpaper_pixels;
static int wallpaper_w;
static int wallpaper_h;
static bool wallpaper_ready;
static int wallpaper_opacity_pct = 30;
static lv_opa_t wallpaper_opa = (lv_opa_t)((30 * 255 + 50) / 100); // 30% default
static int wallpaper_scale_pct = 100; // 100..250
static int wallpaper_pan_x = 0;       // -100..100
static int wallpaper_pan_y = 0;       // -100..100
static char wallpaper_saved_path[512];

static theme_card_surface_t card_surface = THEME_CARD_TRANSLUCENT;
static int card_radius = 12;
static theme_border_style_t border_style = THEME_BORDER_SUBTLE;
static bool mono_grid_icons = false;

static void apply_palette(void);
static void refresh_tree(lv_obj_t *obj);
static void refresh_all(void);
static unsigned palette_generation;

// Pre-blends wallpaper_raw_pixels with active->screen_bg at wallpaper_opa into
// wallpaper_pixels so LVGL renders the 480x800 backdrop with LV_OPA_COVER.
static void rebuild_wallpaper_blended(void) {
	if (!wallpaper_raw_pixels || wallpaper_w <= 0 || wallpaper_h <= 0 || wallpaper_opa == 0) {
		wallpaper_ready = false;
		return;
	}

	size_t count = (size_t)wallpaper_w * (size_t)wallpaper_h;
	if (!wallpaper_pixels) {
		wallpaper_pixels = malloc(count * sizeof(uint16_t));
		if (!wallpaper_pixels) {
			wallpaper_ready = false;
			return;
		}
	}

	if (wallpaper_opa >= 255) {
		memcpy(wallpaper_pixels, wallpaper_raw_pixels, count * sizeof(uint16_t));
	} else {
		uint32_t a = wallpaper_opa;
		uint32_t inv_a = 255 - a;
		uint32_t bg_r = active->screen_bg.red;
		uint32_t bg_g = active->screen_bg.green;
		uint32_t bg_b = active->screen_bg.blue;

		for (size_t i = 0; i < count; i++) {
			uint16_t px = wallpaper_raw_pixels[i];
			uint32_t r5 = (px >> 11) & 0x1F;
			uint32_t g6 = (px >> 5) & 0x3F;
			uint32_t b5 = px & 0x1F;
			uint32_t fg_r = (r5 << 3) | (r5 >> 2);
			uint32_t fg_g = (g6 << 2) | (g6 >> 4);
			uint32_t fg_b = (b5 << 3) | (b5 >> 2);

			uint32_t sr = fg_r * a + bg_r * inv_a;
			uint32_t sg = fg_g * a + bg_g * inv_a;
			uint32_t sb = fg_b * a + bg_b * inv_a;
			uint32_t r = (sr + 1 + (sr >> 8)) >> 8;
			uint32_t g = (sg + 1 + (sg >> 8)) >> 8;
			uint32_t b = (sb + 1 + (sb >> 8)) >> 8;
			wallpaper_pixels[i] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
		}
	}

	memset(&wallpaper_dsc, 0, sizeof(wallpaper_dsc));
	wallpaper_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
	wallpaper_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
	wallpaper_dsc.header.w = (uint32_t)wallpaper_w;
	wallpaper_dsc.header.h = (uint32_t)wallpaper_h;
	wallpaper_dsc.header.stride = (uint32_t)wallpaper_w * 2;
	wallpaper_dsc.data_size = (uint32_t)(count * sizeof(uint16_t));
	wallpaper_dsc.data = (const uint8_t *)wallpaper_pixels;
	wallpaper_ready = true;
}

// Decodes a PNG/JPEG file and aspect-fill crops/scales it to target_w x target_h
// in RGB565 using scale_pct (100..250) and pan_x/pan_y (-100..100).
// Safe to call on a worker thread.
uint16_t *theme_decode_wallpaper_rgb565(const char *path, int target_w, int target_h,
										int scale_pct, int pan_x, int pan_y,
										int *out_w, int *out_h) {
	if (!path || !path[0] || target_w <= 0 || target_h <= 0) {
		return NULL;
	}
	if (scale_pct < 100) scale_pct = 100;
	if (scale_pct > 250) scale_pct = 250;
	if (pan_x < -100) pan_x = -100;
	if (pan_x > 100) pan_x = 100;
	if (pan_y < -100) pan_y = -100;
	if (pan_y > 100) pan_y = 100;

	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0 || size > 32 * 1024 * 1024) {
		fclose(f);
		return NULL;
	}

	uint8_t *buf = malloc((size_t)size);
	if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);

	int sw = 0, sh = 0, channels = 0;
	uint8_t *rgb = stbi_load_from_memory(buf, (int)size, &sw, &sh, &channels, 3);
	free(buf);
	if (!rgb || sw <= 0 || sh <= 0) {
		stbi_image_free(rgb);
		return NULL;
	}

	int dw = target_w;
	int dh = target_h;
	uint16_t *dst = malloc((size_t)dw * (size_t)dh * sizeof(uint16_t));
	if (!dst) {
		stbi_image_free(rgb);
		return NULL;
	}

	if (sw == dw && sh == dh && scale_pct == 100 && pan_x == 0 && pan_y == 0) {
		size_t count = (size_t)dw * (size_t)dh;
		for (size_t i = 0; i < count; i++) {
			const uint8_t *px = rgb + i * 3;
			dst[i] = (uint16_t)(((px[0] & 0xF8) << 8) | ((px[1] & 0xFC) << 3) | (px[2] >> 3));
		}
	} else {
		// Aspect-fill base scale, then apply user zoom (scale_pct) and pan (pan_x, pan_y).
		float scale_x = (float)sw / (float)dw;
		float scale_y = (float)sh / (float)dh;
		float base_scale = scale_x < scale_y ? scale_x : scale_y;
		float scale = base_scale * (100.0f / (float)scale_pct);
		float crop_w = (float)dw * scale;
		float crop_h = (float)dh * scale;
		float slack_x = (float)sw - crop_w;
		float slack_y = (float)sh - crop_h;
		if (slack_x < 0.0f) slack_x = 0.0f;
		if (slack_y < 0.0f) slack_y = 0.0f;

		float off_x = (0.5f - (float)pan_x / 200.0f) * slack_x;
		float off_y = (0.5f - (float)pan_y / 200.0f) * slack_y;
		if (off_x < 0.0f) off_x = 0.0f;
		if (off_x > slack_x) off_x = slack_x;
		if (off_y < 0.0f) off_y = 0.0f;
		if (off_y > slack_y) off_y = slack_y;

		for (int y = 0; y < dh; y++) {
			float sy = off_y + ((float)y + 0.5f) * scale - 0.5f;
			int y0 = (int)sy;
			if (y0 < 0) y0 = 0;
			if (y0 >= sh) y0 = sh - 1;
			int y1 = y0 + 1 < sh ? y0 + 1 : sh - 1;
			float fy = sy - (float)y0;
			if (fy < 0.0f) fy = 0.0f;
			if (fy > 1.0f) fy = 1.0f;

			for (int x = 0; x < dw; x++) {
				float sx = off_x + ((float)x + 0.5f) * scale - 0.5f;
				int x0 = (int)sx;
				if (x0 < 0) x0 = 0;
				if (x0 >= sw) x0 = sw - 1;
				int x1 = x0 + 1 < sw ? x0 + 1 : sw - 1;
				float fx = sx - (float)x0;
				if (fx < 0.0f) fx = 0.0f;
				if (fx > 1.0f) fx = 1.0f;

				const uint8_t *p00 = rgb + ((size_t)y0 * (size_t)sw + (size_t)x0) * 3;
				const uint8_t *p10 = rgb + ((size_t)y0 * (size_t)sw + (size_t)x1) * 3;
				const uint8_t *p01 = rgb + ((size_t)y1 * (size_t)sw + (size_t)x0) * 3;
				const uint8_t *p11 = rgb + ((size_t)y1 * (size_t)sw + (size_t)x1) * 3;

				float w00 = (1.0f - fx) * (1.0f - fy);
				float w10 = fx * (1.0f - fy);
				float w01 = (1.0f - fx) * fy;
				float w11 = fx * fy;

				uint8_t r = (uint8_t)(p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11 + 0.5f);
				uint8_t g = (uint8_t)(p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11 + 0.5f);
				uint8_t b = (uint8_t)(p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11 + 0.5f);
				dst[(size_t)y * (size_t)dw + (size_t)x] =
					(uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
			}
		}
	}

	stbi_image_free(rgb);
	if (out_w) *out_w = dw;
	if (out_h) *out_h = dh;
	return dst;
}

void theme_apply_wallpaper_pixels(uint16_t *pixels, int w, int h, const char *saved_path) {
	// Detach from style before freeing old pixel buffers.
	lv_style_set_bg_image_src(&theme_style_screen, NULL);
	free(wallpaper_raw_pixels);
	free(wallpaper_pixels);
	wallpaper_raw_pixels = pixels;
	wallpaper_pixels = NULL;
	wallpaper_w = w;
	wallpaper_h = h;

	if (pixels && w > 0 && h > 0) {
		rebuild_wallpaper_blended();
	} else {
		wallpaper_ready = false;
	}

	if (saved_path) {
		snprintf(wallpaper_saved_path, sizeof(wallpaper_saved_path), "%s", saved_path);
		config_set("ui", "wallpaper_path", wallpaper_saved_path);
		config_save();
	}

	apply_palette();
	refresh_all();
}

static const char *preset_slug(int idx) {
	static const char *const SLUGS[THEME_PRESET_COUNT] = {
		"cpunk", "miasma", "ash", "matte-black", "tokyo-night", "gruvbox", "light",
	};
	return (idx >= 0 && idx < THEME_PRESET_COUNT) ? SLUGS[idx] : "cpunk";
}

static const char *preset_default_wallpaper(void) {
	if (sd_wallpaper_dir[0]) {
		const char *slug = preset_slug(preset_index);
		static const char *const PATS[4] = {"%s/wallpaper-%s.png", "%s/wallpaper-%s.jpg", "%s/%s.png", "%s/%s.jpg"};
		for (int i = 0; i < 4; i++) {
			int n = snprintf(sd_wallpaper_buf, sizeof(sd_wallpaper_buf), PATS[i], sd_wallpaper_dir, slug);
			if (n > 0 && (size_t)n < sizeof(sd_wallpaper_buf) && access(sd_wallpaper_buf, R_OK) == 0) {
				return sd_wallpaper_buf;
			}
		}
	}
	const char *wp = THEME_PRESETS[preset_index].wallpaper;
	return wp ? wp : (SONIX_RESOURCE_DIR "/gui/wallpaper.png");
}

bool theme_set_wallpaper_file(const char *path, int target_w, int target_h) {
	if (target_w <= 0) target_w = 480;
	if (target_h <= 0) target_h = 800;

	if (path && strcmp(path, "none") == 0) {
		theme_apply_wallpaper_pixels(NULL, 0, 0, "none");
		return true;
	}

	const char *load_path = (path && path[0]) ? path : preset_default_wallpaper();
	int w = 0, h = 0;
	uint16_t *px = theme_decode_wallpaper_rgb565(load_path, target_w, target_h,
												 wallpaper_scale_pct, wallpaper_pan_x, wallpaper_pan_y,
												 &w, &h);
	if (!px) {
		return false;
	}
	theme_apply_wallpaper_pixels(px, w, h, (path && path[0]) ? path : "");
	return true;
}

const char *theme_get_wallpaper_path(void) { return wallpaper_saved_path; }

const char *theme_get_wallpaper_load_path(void) {
	if (strcmp(wallpaper_saved_path, "none") == 0) {
		return NULL;
	}
	return wallpaper_saved_path[0] ? wallpaper_saved_path : preset_default_wallpaper();
}

int theme_get_wallpaper_opacity(void) { return wallpaper_opacity_pct; }

void theme_set_wallpaper_opacity(int pct) {
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	wallpaper_opacity_pct = pct;
	wallpaper_opa = (lv_opa_t)((pct * 255 + 50) / 100);
	config_set_int("ui", "wallpaper_opacity", pct);
	config_save();

	rebuild_wallpaper_blended();
	apply_palette();
	refresh_all();
}

int theme_get_wallpaper_scale(void) { return wallpaper_scale_pct; }
int theme_get_wallpaper_pan_x(void) { return wallpaper_pan_x; }
int theme_get_wallpaper_pan_y(void) { return wallpaper_pan_y; }

void theme_set_wallpaper_transform(int scale_pct, int pan_x, int pan_y) {
	if (scale_pct < 100) scale_pct = 100;
	if (scale_pct > 250) scale_pct = 250;
	if (pan_x < -100) pan_x = -100;
	if (pan_x > 100) pan_x = 100;
	if (pan_y < -100) pan_y = -100;
	if (pan_y > 100) pan_y = 100;

	wallpaper_scale_pct = scale_pct;
	wallpaper_pan_x = pan_x;
	wallpaper_pan_y = pan_y;
	config_set_int("ui", "wallpaper_scale", scale_pct);
	config_set_int("ui", "wallpaper_pan_x", pan_x);
	config_set_int("ui", "wallpaper_pan_y", pan_y);
	config_save();
}

static void wallpaper_load(void) {
	wallpaper_opacity_pct = (int)config_get_int("ui", "wallpaper_opacity", 30);
	const char *opa_env = getenv("SONIX_WALLPAPER_OPA");
	if (opa_env && opa_env[0]) {
		wallpaper_opacity_pct = atoi(opa_env);
	}
	if (wallpaper_opacity_pct < 0) wallpaper_opacity_pct = 0;
	if (wallpaper_opacity_pct > 100) wallpaper_opacity_pct = 100;
	wallpaper_opa = (lv_opa_t)((wallpaper_opacity_pct * 255 + 50) / 100);

	wallpaper_scale_pct = (int)config_get_int("ui", "wallpaper_scale", 100);
	if (wallpaper_scale_pct < 100) wallpaper_scale_pct = 100;
	if (wallpaper_scale_pct > 250) wallpaper_scale_pct = 250;
	wallpaper_pan_x = (int)config_get_int("ui", "wallpaper_pan_x", 0);
	if (wallpaper_pan_x < -100) wallpaper_pan_x = -100;
	if (wallpaper_pan_x > 100) wallpaper_pan_x = 100;
	wallpaper_pan_y = (int)config_get_int("ui", "wallpaper_pan_y", 0);
	if (wallpaper_pan_y < -100) wallpaper_pan_y = -100;
	if (wallpaper_pan_y > 100) wallpaper_pan_y = 100;

	const char *env_path = getenv("SONIX_WALLPAPER");
	const char *cfg_path = config_get("ui", "wallpaper_path", "");
	const char *chosen = (env_path && env_path[0]) ? env_path : cfg_path;

	snprintf(wallpaper_saved_path, sizeof(wallpaper_saved_path), "%s", chosen ? chosen : "");

	if (chosen && strcmp(chosen, "none") == 0) {
		wallpaper_ready = false;
		return;
	}

	const char *load_path = (chosen && chosen[0]) ? chosen : preset_default_wallpaper();
	int w = 0, h = 0;
	uint16_t *px = theme_decode_wallpaper_rgb565(load_path, 480, 800,
												 wallpaper_scale_pct, wallpaper_pan_x, wallpaper_pan_y,
												 &w, &h);
	if (!px && chosen && chosen[0]) {
		// Fall back to preset wallpaper if saved file is missing.
		px = theme_decode_wallpaper_rgb565(preset_default_wallpaper(), 480, 800,
										   wallpaper_scale_pct, wallpaper_pan_x, wallpaper_pan_y,
										   &w, &h);
	}
	if (!px) {
		wallpaper_ready = false;
		return;
	}
	wallpaper_raw_pixels = px;
	wallpaper_w = w;
	wallpaper_h = h;
	rebuild_wallpaper_blended();
}

// Pushes the active palette into the shared styles.
static void apply_palette(void) {
	lv_style_set_bg_color(&theme_style_screen, active->screen_bg);
	if (active->dark && wallpaper_ready && wallpaper_opa > 0) {
		lv_style_set_bg_image_src(&theme_style_screen, &wallpaper_dsc);
		lv_style_set_bg_image_opa(&theme_style_screen, LV_OPA_COVER);
	} else {
		lv_style_set_bg_image_src(&theme_style_screen, NULL);
	}

	lv_opa_t panel_opa = LV_OPA_COVER;
	lv_opa_t card_opa = LV_OPA_COVER;
	if (card_surface == THEME_CARD_GLASS) {
		panel_opa = (active->dark && wallpaper_ready) ? LV_OPA_80 : LV_OPA_COVER;
		card_opa = (active->dark && wallpaper_ready) ? LV_OPA_50 : LV_OPA_90;
	} else if (card_surface == THEME_CARD_TRANSLUCENT && active->dark && wallpaper_ready) {
		panel_opa = LV_OPA_90;
		card_opa = LV_OPA_80;
	}

	lv_style_set_bg_color(&theme_style_panel, active->panel);
	lv_style_set_bg_opa(&theme_style_panel, panel_opa);

	lv_style_set_bg_color(&theme_style_card, active->surface);
	lv_style_set_bg_opa(&theme_style_card, card_opa);
	lv_style_set_radius(&theme_style_card, card_radius);
	lv_style_set_text_color(&theme_style_card, active->text_primary);

	if (border_style == THEME_BORDER_NONE) {
		lv_style_set_border_width(&theme_style_card, 0);
	} else if (border_style == THEME_BORDER_ACCENT) {
		lv_style_set_border_color(&theme_style_card, active->accent);
		lv_style_set_border_width(&theme_style_card, 1);
		lv_style_set_border_opa(&theme_style_card, LV_OPA_70);
	} else if (active->dark || card_surface == THEME_CARD_GLASS) {
		lv_style_set_border_color(&theme_style_card, THEME_PRESETS[preset_index].border);
		lv_style_set_border_width(&theme_style_card, 1);
		lv_style_set_border_opa(&theme_style_card, card_surface == THEME_CARD_GLASS ? LV_OPA_70 : LV_OPA_50);
	} else {
		lv_style_set_border_width(&theme_style_card, 0);
	}

	lv_style_set_bg_color(&theme_style_card_pressed, active->surface_pressed);
	lv_style_set_radius(&theme_style_card_pressed, card_radius);
	if (border_style == THEME_BORDER_NONE) {
		lv_style_set_border_width(&theme_style_card_pressed, 0);
	} else if (border_style == THEME_BORDER_ACCENT) {
		lv_style_set_border_color(&theme_style_card_pressed, active->accent);
		lv_style_set_border_width(&theme_style_card_pressed, 2);
		lv_style_set_border_opa(&theme_style_card_pressed, LV_OPA_COVER);
	} else if (active->dark || card_surface == THEME_CARD_GLASS) {
		lv_style_set_border_color(&theme_style_card_pressed, active->text_primary);
		lv_style_set_border_width(&theme_style_card_pressed, 1);
		lv_style_set_border_opa(&theme_style_card_pressed, LV_OPA_60);
	} else {
		lv_style_set_border_width(&theme_style_card_pressed, 0);
	}

	lv_style_set_text_color(&theme_style_text, active->text_primary);
	lv_style_set_text_color(&theme_style_text_dim, active->text_secondary);

	lv_style_set_image_recolor(&theme_style_icon, active->text_primary);

	lv_style_set_bg_color(&theme_style_switch, active->surface_pressed);
	lv_style_set_bg_color(&theme_style_switch_checked, active->accent);

	lv_style_set_bg_color(&theme_style_accent_bg, active->accent);
	lv_style_set_bg_color(&theme_style_slider_track, active->text_secondary);
}

void theme_init(void) {
	lv_style_init(&theme_style_screen);
	lv_style_set_bg_opa(&theme_style_screen, LV_OPA_COVER);

	lv_style_init(&theme_style_panel);
	lv_style_set_bg_opa(&theme_style_panel, LV_OPA_COVER);

	lv_style_init(&theme_style_card);
	lv_style_set_bg_opa(&theme_style_card, LV_OPA_COVER);

	lv_style_init(&theme_style_card_pressed);
	lv_style_set_bg_opa(&theme_style_card_pressed, LV_OPA_COVER);
	// The default theme grows a pressed button by a few pixels; on a list of
	// cards that reads as the row jumping out of the list, so pin it flat.
	lv_style_set_transform_width(&theme_style_card_pressed, 0);
	lv_style_set_transform_height(&theme_style_card_pressed, 0);

	lv_style_init(&theme_style_text);
	lv_style_init(&theme_style_text_dim);

	lv_style_init(&theme_style_icon);
	lv_style_set_image_recolor_opa(&theme_style_icon, LV_OPA_COVER);

	lv_style_init(&theme_style_switch);
	lv_style_set_bg_opa(&theme_style_switch, LV_OPA_COVER);
	lv_style_init(&theme_style_switch_checked);
	lv_style_set_bg_opa(&theme_style_switch_checked, LV_OPA_COVER);

	lv_style_init(&theme_style_accent_bg);
	lv_style_set_bg_opa(&theme_style_accent_bg, LV_OPA_COVER);
	lv_style_init(&theme_style_slider_track);
	lv_style_set_bg_opa(&theme_style_slider_track, LV_OPA_40);
	lv_style_set_radius(&theme_style_slider_track, LV_RADIUS_CIRCLE);

	// Load saved theme preset (defaults to 0 = CpUnk).
	preset_index = (int)config_get_int("ui", "theme_preset", 0);
	if (preset_index < 0 || preset_index >= THEME_PRESET_COUNT) {
		preset_index = 0;
	}
	palette_light = THEME_PRESETS[THEME_PRESET_COUNT - 1].palette;
	if (THEME_PRESETS[preset_index].palette.dark) {
		last_dark_preset = preset_index;
		palette_dark = THEME_PRESETS[preset_index].palette;
		dark_selected = true;
	} else {
		last_dark_preset = 0;
		palette_dark = THEME_PRESETS[0].palette;
		dark_selected = false;
	}

	// Optional accent override; only applied if the user explicitly picked an accent circle.
	accent_index = (int)config_get_int("ui", "accent", 0);
	if (accent_index < 0 || accent_index >= THEME_ACCENT_COUNT) {
		accent_index = 0;
	}
	custom_accent_override = config_get_bool("ui", "accent_override", false);
	custom_hue = (int)config_get_int("ui", "accent_hue", -1);
	if (custom_accent_override) {
		lv_color_t ac = (custom_hue >= 0 && custom_hue < 360) ? color_from_hue(custom_hue) : ACCENT_COLORS[accent_index];
		palette_dark.accent = ac;
		palette_light.accent = ac;
	}

	dynamic_tint = config_get_bool("ui", "dynamic_tint", false);

	int cs = (int)config_get_int("ui", "card_surface", THEME_CARD_TRANSLUCENT);
	if (cs < THEME_CARD_TRANSLUCENT || cs > THEME_CARD_GLASS) cs = THEME_CARD_TRANSLUCENT;
	card_surface = (theme_card_surface_t)cs;

	card_radius = (int)config_get_int("ui", "card_radius", 12);
	if (card_radius != 0 && card_radius != 6 && card_radius != 12 && card_radius != 20) {
		card_radius = 12;
	}

	int bs = (int)config_get_int("ui", "border_style", THEME_BORDER_SUBTLE);
	if (bs < THEME_BORDER_SUBTLE || bs > THEME_BORDER_NONE) bs = THEME_BORDER_SUBTLE;
	border_style = (theme_border_style_t)bs;

	mono_grid_icons = config_get_bool("ui", "mono_grid_icons", false);

	wallpaper_load();
	rebuild_active();
	apply_palette();

	// The marker on the flash may be missing or older than these settings.
	bootlogo_follow(active->dark, accent_index);
}

void theme_apply_slider_knob(lv_obj_t *slider) {
	if (!slider) {
		return;
	}
	bool dark = theme()->dark;
	lv_obj_set_style_border_color(slider, dark ? lv_color_black() : lv_color_make(120, 120, 120), LV_PART_KNOB);
	lv_obj_set_style_border_opa(slider, dark ? LV_OPA_10 : LV_OPA_COVER, LV_PART_KNOB);
	lv_obj_set_style_border_width(slider, dark ? 1 : 2, LV_PART_KNOB);
}

void theme_register_refresh(theme_refresh_cb_t cb) {
	if (!cb) {
		return;
	}

	if (refresh_cb_count == refresh_cb_capacity) {
		int wanted = refresh_cb_capacity ? refresh_cb_capacity * 2 : 16;
		theme_refresh_cb_t *grown = realloc(refresh_cbs, (size_t)wanted * sizeof(*grown));
		if (!grown) {
			// A device that cannot find a few dozen bytes has bigger problems,
			// but a registration must never be lost unnoticed.
			fprintf(stderr, "theme: out of memory for refresh callback number %d\n",
					refresh_cb_count + 1);
			return;
		}
		refresh_cbs = grown;
		refresh_cb_capacity = wanted;
	}

	refresh_cbs[refresh_cb_count++] = cb;
}

const theme_palette_t *theme(void) { return active; }

bool theme_is_dark(void) { return active->dark; }

void theme_set_dark(bool dark) {
	if (active->dark == dark) {
		return;
	}
	theme_toggle();
}

// Which palette the pages on screen were last walked for.
//
// lv_obj_report_style_change(NULL) walks every object of every screen and
// refreshes each one against its whole subtree. The pages here are built at
// startup and stay built, so that is the entire interface -- thousands of
// objects -- for a change that moves nothing but colours, and it takes far
// longer than a frame: the finger is still on the switch when it starts.
//
// A colour is read out of the style at draw time, so the page in front of the
// user is the only one that has to be walked at the moment of the change. The
// pages behind it are walked the first time they are shown again, which is
// where a redraw was going to happen anyway.
static unsigned palette_generation = 1;

static struct screen_generation {
	lv_obj_t *screen;
	unsigned generation;
} *screen_gens;
static int screen_gen_count;
static int screen_gen_capacity;

// True when `screen` still carries an older palette than the one in force, and
// records it as walked for this one. A screen this has never seen is taken as
// current: it was built from the styles as they stand.
//
// An allocation that fails costs a walk per page change rather than a page
// with the wrong colours, which is why the failure is silent here.
static bool screen_is_stale(lv_obj_t *screen) {
	for (int i = 0; i < screen_gen_count; i++) {
		if (screen_gens[i].screen != screen) {
			continue;
		}
		if (screen_gens[i].generation == palette_generation) {
			return false;
		}
		screen_gens[i].generation = palette_generation;
		return true;
	}

	if (screen_gen_count == screen_gen_capacity) {
		int wanted = screen_gen_capacity ? screen_gen_capacity * 2 : 32;
		struct screen_generation *grown = realloc(screen_gens, (size_t)wanted * sizeof(*grown));
		if (!grown) {
			return true;
		}
		screen_gens = grown;
		screen_gen_capacity = wanted;
	}

	screen_gens[screen_gen_count].screen = screen;
	screen_gens[screen_gen_count].generation = palette_generation;
	screen_gen_count++;
	return false;
}

// One tree, refreshed against the styles it carries. LV_STYLE_PROP_ANY takes
// the object's own properties and recurses into its children.
static void refresh_tree(lv_obj_t *obj) {
	if (obj) {
		lv_obj_refresh_style(obj, LV_PART_ANY, LV_STYLE_PROP_ANY);
	}
}

void theme_notify_screen_shown(lv_obj_t *screen) {
	if (screen && screen_is_stale(screen)) {
		refresh_tree(screen);
	}
}

// Repaints after a palette change: the page on screen and the three layers the
// status bar, the player sheet and the popups live on, then the callbacks,
// which catch what cannot be expressed as a shared style -- an icon recoloured
// at runtime, the blurred artwork behind the controls. Those run for every
// page, on screen or not, and cost about as much as one frame in total.
static void refresh_all(void) {
	palette_generation++;

	lv_obj_t *screen = lv_screen_active();
	screen_is_stale(screen);
	refresh_tree(screen);
	refresh_tree(lv_layer_top());
	refresh_tree(lv_layer_sys());
	refresh_tree(lv_layer_bottom());

	for (int i = 0; i < refresh_cb_count; i++) {
		refresh_cbs[i]();
	}
}

const char *theme_preset_name(int index) {
	if (index < 0 || index >= THEME_PRESET_COUNT) {
		index = 0;
	}
	return THEME_PRESETS[index].name;
}

int theme_get_preset(void) { return preset_index; }

typedef struct {
	unsigned seq;
	char path[512];
	int scale_pct;
	int pan_x;
	int pan_y;
	uint16_t *pixels;
	int w;
	int h;
} preset_wp_job_t;

static unsigned preset_wp_seq;

static void preset_wp_done_cb(void *arg) {
	preset_wp_job_t *job = (preset_wp_job_t *)arg;
	if (job->seq == preset_wp_seq && !wallpaper_saved_path[0] && job->pixels) {
		lv_style_set_bg_image_src(&theme_style_screen, NULL);
		free(wallpaper_raw_pixels);
		free(wallpaper_pixels);
		wallpaper_raw_pixels = job->pixels;
		wallpaper_pixels = NULL;
		wallpaper_w = job->w;
		wallpaper_h = job->h;
		rebuild_wallpaper_blended();
		apply_palette();
		refresh_all();
	} else {
		free(job->pixels);
	}
	free(job);
}

static void *preset_wp_thread(void *arg) {
	preset_wp_job_t *job = (preset_wp_job_t *)arg;
	job->pixels = theme_decode_wallpaper_rgb565(job->path, 480, 800,
												  job->scale_pct, job->pan_x, job->pan_y,
												  &job->w, &job->h);
	if (!gui_post(preset_wp_done_cb, job)) {
		free(job->pixels);
		free(job);
	}
	return NULL;
}

void theme_set_preset(int index) {
	if (index < 0 || index >= THEME_PRESET_COUNT) {
		return;
	}
	preset_index = index;
	preset_wp_seq++;
	custom_accent_override = false;
	custom_hue = -1;
	if (THEME_PRESETS[index].palette.dark) {
		last_dark_preset = index;
		palette_dark = THEME_PRESETS[index].palette;
		dark_selected = true;
	} else {
		palette_light = THEME_PRESETS[index].palette;
		dark_selected = false;
	}
	rebuild_active();

	// If using the theme-default wallpaper (not a custom file or "none"), swap
	// to this theme's 480x800 wallpaper (from SD .local/wallpapers or flash)
	// asynchronously so the UI responds immediately without blocking on PNG decode.
	bool async_wp_queued = false;
	const char *default_wp = preset_default_wallpaper();
	if (!wallpaper_saved_path[0] && default_wp) {
		preset_wp_job_t *job = calloc(1, sizeof(*job));
		if (job) {
			job->seq = preset_wp_seq;
			snprintf(job->path, sizeof(job->path), "%s", default_wp);
			job->scale_pct = wallpaper_scale_pct;
			job->pan_x = wallpaper_pan_x;
			job->pan_y = wallpaper_pan_y;
			pthread_t th;
			if (pthread_create(&th, NULL, preset_wp_thread, job) == 0) {
				pthread_detach(th);
				async_wp_queued = true;
			} else {
				free(job);
			}
		}
	}
	if (!async_wp_queued && active->dark) {
		rebuild_wallpaper_blended();
	}
	apply_palette();

	config_set_int("ui", "theme_preset", preset_index);
	config_set_bool("ui", "dark_theme", active->dark);
	config_set_bool("ui", "accent_override", false);
	config_set_int("ui", "accent_hue", -1);
	config_save();

	bootlogo_follow(active->dark, accent_index);
	refresh_all();
}

void theme_toggle(void) {
	int target = dark_selected ? (THEME_PRESET_COUNT - 1) : last_dark_preset;
	theme_set_preset(target);
}

void theme_set_accent(int index) {
	if (index < 0 || index >= THEME_ACCENT_COUNT) {
		index = 0;
	}
	accent_index = index;
	custom_accent_override = true;
	custom_hue = -1;
	palette_dark.accent = ACCENT_COLORS[index];
	palette_light.accent = ACCENT_COLORS[index];
	rebuild_active(); // the tint is drawn from the accent, so it moves with it
	if (dynamic_tint) {
		rebuild_wallpaper_blended();
	}
	apply_palette();

	config_set_int("ui", "accent", index);
	config_set_bool("ui", "accent_override", true);
	config_set_int("ui", "accent_hue", -1);
	config_save();

	bootlogo_follow(active->dark, accent_index);

	refresh_all();
}

void theme_set_custom_accent_hue(int hue) {
	hue = ((hue % 360) + 360) % 360;
	custom_hue = hue;
	custom_accent_override = true;
	lv_color_t ac = color_from_hue(hue);
	palette_dark.accent = ac;
	palette_light.accent = ac;
	rebuild_active();
	if (dynamic_tint) {
		rebuild_wallpaper_blended();
	}
	apply_palette();

	config_set_bool("ui", "accent_override", true);
	config_set_int("ui", "accent_hue", hue);
	config_save();

	refresh_all();
}

void theme_set_card_root(const char *sd_root) {
	if (sd_root && sd_root[0]) {
		snprintf(sd_wallpaper_dir, sizeof(sd_wallpaper_dir), "%s/.local/wallpapers", sd_root);
	} else {
		sd_wallpaper_dir[0] = '\0';
	}
}

bool theme_dynamic_tint(void) { return dynamic_tint; }

void theme_set_dynamic_tint(bool on) {
	if (dynamic_tint == on) {
		return;
	}
	dynamic_tint = on;
	rebuild_active();
	rebuild_wallpaper_blended();
	apply_palette();

	config_set_bool("ui", "dynamic_tint", on);
	config_save();

	refresh_all();
}

theme_card_surface_t theme_get_card_surface(void) { return card_surface; }

void theme_set_card_surface(theme_card_surface_t surface) {
	if (surface < THEME_CARD_TRANSLUCENT || surface > THEME_CARD_GLASS) {
		surface = THEME_CARD_TRANSLUCENT;
	}
	if (card_surface == surface) {
		return;
	}
	card_surface = surface;
	config_set_int("ui", "card_surface", (int)surface);
	config_save();
	apply_palette();
	refresh_all();
}

int theme_get_card_radius(void) { return card_radius; }

void theme_set_card_radius(int px) {
	if (px != 0 && px != 6 && px != 12 && px != 20) {
		px = 12;
	}
	if (card_radius == px) {
		return;
	}
	card_radius = px;
	config_set_int("ui", "card_radius", px);
	config_save();
	apply_palette();
	refresh_all();
}

theme_border_style_t theme_get_border_style(void) { return border_style; }

void theme_set_border_style(theme_border_style_t style) {
	if (style < THEME_BORDER_SUBTLE || style > THEME_BORDER_NONE) {
		style = THEME_BORDER_SUBTLE;
	}
	if (border_style == style) {
		return;
	}
	border_style = style;
	config_set_int("ui", "border_style", (int)style);
	config_save();
	apply_palette();
	refresh_all();
}

bool theme_get_mono_grid_icons(void) { return mono_grid_icons; }

void theme_set_mono_grid_icons(bool on) {
	if (mono_grid_icons == on) {
		return;
	}
	mono_grid_icons = on;
	config_set_bool("ui", "mono_grid_icons", on);
	config_save();
	refresh_all();
}
