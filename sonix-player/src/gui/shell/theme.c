#include "theme.h"

#include "src/system/device/bootlogo.h"
#include "src/system/core/config.h"
#include "src/system/core/respath.h"
#include "src/system/image/stb_image_decl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// Omarchy CpUnk palette (from ~/.config/omarchy/themes/cpunk/colors.toml):
// Steel-and-signal cyberpunk: near-black surfaces (#040303 / #0a0808 / #140f0f),
// hairline chrome (#7f6e6e / #aaacac), and #F82A2A signal red as the accent.
static theme_palette_t palette_dark = {
	.screen_bg = LV_COLOR_MAKE(4, 3, 3),        // #040303 background
	.surface = LV_COLOR_MAKE(20, 15, 15),       // #140f0f lighter_background
	.surface_pressed = LV_COLOR_MAKE(46, 28, 28), // pressed dim red-black wash
	.panel = LV_COLOR_MAKE(10, 8, 8),           // #0a0808 dark_background
	.cover_bg = LV_COLOR_MAKE(10, 8, 8),        // #0a0808
	.text_primary = LV_COLOR_MAKE(255, 255, 255), // #ffffff foreground
	.text_secondary = LV_COLOR_MAKE(127, 110, 110), // #7f6e6e muted
	.accent = LV_COLOR_MAKE(248, 42, 42),       // #F82A2A signal red
	.dark = true,
};

// Adwaita light: window #fafafa, cards white, pressed = black at 8%%, text
// black at 80%%. The accent is the same #3584e4 in both variants.
static theme_palette_t palette_light = {
	// The window is pushed further from white than stock Adwaita's #fafafa:
	// on this panel white cards on a #fafafa window are indistinguishable, so
	// the window is a light grey and the pressed state a step deeper.
	.screen_bg = LV_COLOR_MAKE(236, 237, 240),
	.surface = LV_COLOR_MAKE(255, 255, 255),
	.surface_pressed = LV_COLOR_MAKE(221, 223, 228),
	.panel = LV_COLOR_MAKE(255, 255, 255),
	.cover_bg = LV_COLOR_MAKE(224, 226, 230),
	.text_primary = LV_COLOR_MAKE(50, 50, 50),
	.text_secondary = LV_COLOR_MAKE(115, 115, 115),
	.accent = LV_COLOR_MAKE(53, 132, 228),
	.dark = false,
};

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

// Optional menu wallpaper (480x800 for HiBy R1), blended at configurable
// opacity, zoom scale, and X/Y pan offset behind every screen.
static lv_image_dsc_t wallpaper_dsc;
static uint16_t *wallpaper_pixels;
static bool wallpaper_ready;
static int wallpaper_opacity_pct = 30;
static lv_opa_t wallpaper_opa = (lv_opa_t)((30 * 255 + 50) / 100); // 30% default
static int wallpaper_scale_pct = 100; // 100..250
static int wallpaper_pan_x = 0;       // -100..100
static int wallpaper_pan_y = 0;       // -100..100
static char wallpaper_saved_path[512];

static void apply_palette(void);
static void refresh_tree(lv_obj_t *obj);
static unsigned palette_generation;

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
	// Detach from style before freeing old pixel buffer.
	lv_style_set_bg_image_src(&theme_style_screen, NULL);
	free(wallpaper_pixels);
	wallpaper_pixels = pixels;

	if (pixels && w > 0 && h > 0) {
		memset(&wallpaper_dsc, 0, sizeof(wallpaper_dsc));
		wallpaper_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
		wallpaper_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
		wallpaper_dsc.header.w = (uint32_t)w;
		wallpaper_dsc.header.h = (uint32_t)h;
		wallpaper_dsc.header.stride = (uint32_t)w * 2;
		wallpaper_dsc.data_size = (uint32_t)((size_t)w * (size_t)h * sizeof(uint16_t));
		wallpaper_dsc.data = (const uint8_t *)wallpaper_pixels;
		wallpaper_ready = true;
	} else {
		wallpaper_ready = false;
	}

	if (saved_path) {
		snprintf(wallpaper_saved_path, sizeof(wallpaper_saved_path), "%s", saved_path);
		config_set("ui", "wallpaper_path", wallpaper_saved_path);
		config_save();
	}

	apply_palette();
	palette_generation++;
	if (lv_screen_active()) {
		refresh_tree(lv_screen_active());
	}
}

bool theme_set_wallpaper_file(const char *path, int target_w, int target_h) {
	if (target_w <= 0) target_w = 480;
	if (target_h <= 0) target_h = 800;

	if (path && strcmp(path, "none") == 0) {
		theme_apply_wallpaper_pixels(NULL, 0, 0, "none");
		return true;
	}

	const char *load_path = (path && path[0]) ? path : (SONIX_RESOURCE_DIR "/gui/wallpaper.png");
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
	return wallpaper_saved_path[0] ? wallpaper_saved_path : (SONIX_RESOURCE_DIR "/gui/wallpaper.png");
}

int theme_get_wallpaper_opacity(void) { return wallpaper_opacity_pct; }

void theme_set_wallpaper_opacity(int pct) {
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	wallpaper_opacity_pct = pct;
	wallpaper_opa = (lv_opa_t)((pct * 255 + 50) / 100);
	config_set_int("ui", "wallpaper_opacity", pct);
	config_save();

	apply_palette();
	palette_generation++;
	if (lv_screen_active()) {
		refresh_tree(lv_screen_active());
	}
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

	const char *load_path = (chosen && chosen[0]) ? chosen : (SONIX_RESOURCE_DIR "/gui/wallpaper.png");
	int w = 0, h = 0;
	uint16_t *px = theme_decode_wallpaper_rgb565(load_path, 480, 800,
												 wallpaper_scale_pct, wallpaper_pan_x, wallpaper_pan_y,
												 &w, &h);
	if (!px && chosen && chosen[0]) {
		// Fall back to default bundled wallpaper if saved file is missing.
		px = theme_decode_wallpaper_rgb565(SONIX_RESOURCE_DIR "/gui/wallpaper.png", 480, 800,
										   wallpaper_scale_pct, wallpaper_pan_x, wallpaper_pan_y,
										   &w, &h);
	}
	if (!px) {
		wallpaper_ready = false;
		return;
	}
	wallpaper_pixels = px;
	memset(&wallpaper_dsc, 0, sizeof(wallpaper_dsc));
	wallpaper_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
	wallpaper_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
	wallpaper_dsc.header.w = (uint32_t)w;
	wallpaper_dsc.header.h = (uint32_t)h;
	wallpaper_dsc.header.stride = (uint32_t)w * 2;
	wallpaper_dsc.data_size = (uint32_t)((size_t)w * (size_t)h * sizeof(uint16_t));
	wallpaper_dsc.data = (const uint8_t *)wallpaper_pixels;
	wallpaper_ready = true;
}

// Pushes the active palette into the shared styles.
static void apply_palette(void) {
	lv_style_set_bg_color(&theme_style_screen, active->screen_bg);
	if (active->dark && wallpaper_ready && wallpaper_opa > 0) {
		lv_style_set_bg_image_src(&theme_style_screen, &wallpaper_dsc);
		lv_style_set_bg_image_opa(&theme_style_screen, wallpaper_opa);
	} else {
		lv_style_set_bg_image_src(&theme_style_screen, NULL);
	}

	lv_style_set_bg_color(&theme_style_panel, active->panel);
	lv_style_set_bg_opa(&theme_style_panel, (active->dark && wallpaper_ready) ? LV_OPA_90 : LV_OPA_COVER);

	lv_style_set_bg_color(&theme_style_card, active->surface);
	lv_style_set_bg_opa(&theme_style_card, (active->dark && wallpaper_ready) ? LV_OPA_80 : LV_OPA_COVER);
	lv_style_set_text_color(&theme_style_card, active->text_primary);
	if (active->dark) {
		lv_style_set_border_color(&theme_style_card, lv_color_make(127, 110, 110)); // #7f6e6e hairline chrome
		lv_style_set_border_width(&theme_style_card, 1);
		lv_style_set_border_opa(&theme_style_card, LV_OPA_50);
	} else {
		lv_style_set_border_width(&theme_style_card, 0);
	}

	lv_style_set_bg_color(&theme_style_card_pressed, active->surface_pressed);
	if (active->dark) {
		lv_style_set_border_color(&theme_style_card_pressed, lv_color_make(170, 172, 172)); // #aaacac active chrome
		lv_style_set_border_width(&theme_style_card_pressed, 1);
		lv_style_set_border_opa(&theme_style_card_pressed, LV_OPA_90);
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

	// Whatever was chosen last time. Dark is the default for a first boot.
	dark_selected = config_get_bool("ui", "dark_theme", true);

	// The accent as it was left; both palettes carry the same one.
	accent_index = (int)config_get_int("ui", "accent", 0);
	if (accent_index < 0 || accent_index >= THEME_ACCENT_COUNT) {
		accent_index = 0;
	}
	palette_dark.accent = ACCENT_COLORS[accent_index];
	palette_light.accent = ACCENT_COLORS[accent_index];

	dynamic_tint = config_get_bool("ui", "dynamic_tint", false);

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

void theme_toggle(void) {
	dark_selected = !dark_selected;
	rebuild_active();
	apply_palette();

	config_set_bool("ui", "dark_theme", active->dark);
	config_save();

	// The boot logo follows the theme and the accent; see bootlogo.h.
	bootlogo_follow(active->dark, accent_index);

	refresh_all();
}

void theme_set_accent(int index) {
	if (index < 0 || index >= THEME_ACCENT_COUNT) {
		index = 0;
	}
	accent_index = index;
	palette_dark.accent = ACCENT_COLORS[index];
	palette_light.accent = ACCENT_COLORS[index];
	rebuild_active(); // the tint is drawn from the accent, so it moves with it
	apply_palette();

	config_set_int("ui", "accent", index);
	config_save();

	bootlogo_follow(active->dark, accent_index);

	refresh_all();
}

bool theme_dynamic_tint(void) { return dynamic_tint; }

void theme_set_dynamic_tint(bool on) {
	if (dynamic_tint == on) {
		return;
	}
	dynamic_tint = on;
	rebuild_active();
	apply_palette();

	config_set_bool("ui", "dynamic_tint", on);
	config_save();

	refresh_all();
}
