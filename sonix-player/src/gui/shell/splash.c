#include "splash.h"

#include <stdio.h>
#include <stdlib.h>

#include "lvgl/lvgl.h"

#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/theme.h"
#include "src/system/core/config.h"
#include "src/system/device/sonixpkg.h"
#include "src/system/device/sysinfo.h"

// Shown at least this long, so it reads as a picture and not a flicker.
#define MIN_SHOWN_MS 1200
#define FADE_MS 350

#define TILE 132
#define BAR_W 168
#define BAR_H 6

static lv_obj_t *splash;
static lv_obj_t *bar;
static lv_obj_t *ring;
static uint32_t shown_at;

static void refresh(void) {
	lv_display_t *disp = lv_display_get_default();
	if (disp) {
		lv_refr_now(disp);
	}
}

void splash_show(void) {
	if (splash || getenv("SONIX_NO_SPLASH")) {
		return;
	}
	// The palette as it was left: dark or light, the accent, a Compas skin.
	theme_init();
	const theme_palette_t *t = theme();

	splash = lv_obj_create(lv_layer_top());
	lv_obj_remove_style_all(splash);
	lv_obj_set_size(splash, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(splash, t->screen_bg, 0);
	lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
	lv_obj_set_scrollable(splash, false);
	lv_obj_set_clickable(splash, true); // nothing underneath takes a tap yet

	const char *style_env = getenv("SONIX_SPLASH_STYLE");
	int style = style_env ? atoi(style_env) : (int)config_get_int("ui", "splash_style", 0);
	const sysinfo_model_t *model = sysinfo_model();
	const char *model_name = model && model->name ? model->name : NULL;

	if (style == 1) {
		// A record: accent rings around a dark label with the note, the
		// progress running round the outer ring.
		ring = lv_arc_create(splash);
		lv_obj_set_size(ring, 236, 236);
		lv_obj_align(ring, LV_ALIGN_CENTER, 0, -70);
		lv_arc_set_rotation(ring, 270);
		lv_arc_set_bg_angles(ring, 0, 360);
		lv_arc_set_range(ring, 0, 100);
		lv_arc_set_value(ring, 5);
		lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
		lv_obj_set_clickable(ring, false);
		lv_obj_set_style_arc_width(ring, 6, LV_PART_MAIN);
		lv_obj_set_style_arc_width(ring, 6, LV_PART_INDICATOR);
		lv_obj_set_style_arc_color(ring, t->text_secondary, LV_PART_MAIN);
		lv_obj_set_style_arc_opa(ring, LV_OPA_20, LV_PART_MAIN);
		lv_obj_set_style_arc_color(ring, t->accent, LV_PART_INDICATOR);

		lv_obj_t *disc = lv_obj_create(splash);
		lv_obj_remove_style_all(disc);
		lv_obj_set_size(disc, 196, 196);
		lv_obj_align(disc, LV_ALIGN_CENTER, 0, -70);
		lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_bg_color(disc, lv_color_mix(t->accent, lv_color_black(), 70), 0);
		lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
		for (int r = 176; r >= 120; r -= 14) { // the grooves
			lv_obj_t *g = lv_obj_create(disc);
			lv_obj_remove_style_all(g);
			lv_obj_set_size(g, r, r);
			lv_obj_center(g);
			lv_obj_set_style_radius(g, LV_RADIUS_CIRCLE, 0);
			lv_obj_set_style_border_width(g, 1, 0);
			lv_obj_set_style_border_color(g, lv_color_white(), 0);
			lv_obj_set_style_border_opa(g, LV_OPA_20, 0);
		}
		lv_obj_t *label = lv_obj_create(disc);
		lv_obj_remove_style_all(label);
		lv_obj_set_size(label, 92, 92);
		lv_obj_center(label);
		lv_obj_set_style_radius(label, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_bg_color(label, t->accent, 0);
		lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
		lv_obj_t *note = lv_image_create(label);
		lv_image_set_src(note, &icon_music_note);
		lv_image_set_scale(note, 110);
		lv_obj_set_style_image_recolor(note, lv_color_white(), 0);
		lv_obj_set_style_image_recolor_opa(note, LV_OPA_COVER, 0);
		lv_obj_center(note);

		lv_obj_t *name = lv_label_create(splash);
		lv_label_set_text(name, "Sonix");
		lv_obj_set_style_text_font(name, &font_ui_64_bold, 0);
		lv_obj_set_style_text_color(name, t->text_primary, 0);
		lv_obj_align(name, LV_ALIGN_CENTER, 0, 110);
		if (model_name) {
			lv_obj_t *player = lv_label_create(splash);
			lv_label_set_text(player, model_name);
			lv_obj_set_style_text_font(player, &font_ui_22, 0);
			lv_obj_set_style_text_color(player, t->text_secondary, 0);
			lv_obj_align(player, LV_ALIGN_CENTER, 0, 165);
		}
	} else if (style == 2) {
		// Minimal: the name spaced out, an accent line that fills edge to edge.
		lv_obj_t *name = lv_label_create(splash);
		lv_label_set_text(name, "SONIX");
		lv_obj_set_style_text_font(name, &font_ui_64_bold, 0);
		lv_obj_set_style_text_letter_space(name, 14, 0);
		lv_obj_set_style_text_color(name, t->text_primary, 0);
		lv_obj_align(name, LV_ALIGN_CENTER, 7, -30);

		lv_obj_t *dot = lv_obj_create(splash);
		lv_obj_remove_style_all(dot);
		lv_obj_set_size(dot, 14, 14);
		lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_bg_color(dot, t->accent, 0);
		lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
		lv_obj_align_to(dot, name, LV_ALIGN_OUT_RIGHT_TOP, -6, 10);

		if (model_name) {
			lv_obj_t *player = lv_label_create(splash);
			lv_label_set_text(player, model_name);
			lv_obj_set_style_text_font(player, &font_ui_20, 0);
			lv_obj_set_style_text_letter_space(player, 3, 0);
			lv_obj_set_style_text_color(player, t->text_secondary, 0);
			lv_obj_align(player, LV_ALIGN_CENTER, 0, 30);
		}
		bar = lv_bar_create(splash);
		lv_obj_set_size(bar, lv_pct(100), 4);
		lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
		lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
		lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
		lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN);
		lv_obj_set_style_bg_color(bar, t->accent, LV_PART_INDICATOR);
		lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
		lv_bar_set_range(bar, 0, 100);
		lv_bar_set_value(bar, 5, LV_ANIM_OFF);
	} else {
		// The middle: an accent tile with a note in it, the name, the player.
		lv_obj_t *column = lv_obj_create(splash);
		lv_obj_remove_style_all(column);
		lv_obj_set_size(column, lv_pct(100), LV_SIZE_CONTENT);
		lv_obj_align(column, LV_ALIGN_CENTER, 0, -40);
		lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_row(column, 14, 0);

		lv_obj_t *tile = lv_obj_create(column);
		lv_obj_remove_style_all(tile);
		lv_obj_set_size(tile, TILE, TILE);
		lv_obj_set_style_radius(tile, 36, 0);
		lv_obj_set_style_bg_color(tile, t->accent, 0);
		lv_obj_set_style_bg_grad_color(tile, lv_color_mix(t->accent, lv_color_black(), 150), 0);
		lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
		lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
		lv_obj_set_style_margin_bottom(tile, 10, 0);

		lv_obj_t *note = lv_image_create(tile);
		lv_image_set_src(note, &icon_music_note);
		lv_image_set_scale(note, 176); // 128 px icon -> about 88 px
		lv_obj_set_style_image_recolor(note, lv_color_white(), 0);
		lv_obj_set_style_image_recolor_opa(note, LV_OPA_COVER, 0);
		lv_obj_center(note);

		lv_obj_t *name = lv_label_create(column);
		lv_label_set_text(name, "Sonix");
		lv_obj_set_style_text_font(name, &font_ui_64_bold, 0);
		lv_obj_set_style_text_color(name, t->text_primary, 0);

		if (model_name) {
			lv_obj_t *player = lv_label_create(column);
			lv_label_set_text(player, model_name);
			lv_obj_set_style_text_font(player, &font_ui_22, 0);
			lv_obj_set_style_text_color(player, t->text_secondary, 0);
		}

		// The bottom: how far the start has come, and the version.
		bar = lv_bar_create(splash);
		lv_obj_set_size(bar, BAR_W, BAR_H);
		lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -92);
		lv_bar_set_range(bar, 0, 100);
		lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
		lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
		lv_obj_set_style_bg_color(bar, t->text_secondary, LV_PART_MAIN);
		lv_obj_set_style_bg_opa(bar, LV_OPA_30, LV_PART_MAIN);
		lv_obj_set_style_bg_color(bar, t->accent, LV_PART_INDICATOR);
		lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
		lv_bar_set_value(bar, 5, LV_ANIM_OFF);
	}

	if (sonixpkg_active()) {
		lv_obj_t *version = lv_label_create(splash);
		lv_label_set_text_fmt(version, "v%s", sonixpkg_version());
		lv_obj_set_style_text_font(version, &font_ui_20, 0);
		lv_obj_set_style_text_color(version, t->text_secondary, 0);
		lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0, -52);
	}

	shown_at = lv_tick_get();
	refresh();
}

void splash_step(int percent) {
	if (!splash) {
		return;
	}
	int v = percent < 0 ? 0 : percent > 100 ? 100 : percent;
	if (bar) {
		lv_bar_set_value(bar, v, LV_ANIM_OFF);
	}
	if (ring) {
		lv_arc_set_value(ring, v);
	}
	lv_obj_move_foreground(splash);
	refresh();
}

void splash_finish(void) {
	if (!splash) {
		return;
	}
	splash_step(100);
	// The pages were built over it on the same layer; it goes back on top.
	lv_obj_move_foreground(splash);

	uint32_t up = lv_tick_elaps(shown_at);
	uint32_t delay = up < MIN_SHOWN_MS ? MIN_SHOWN_MS - up : 0;
	const char *hold = getenv("SONIX_SPLASH_HOLD_MS"); // for screenshots in the simulator
	if (hold) {
		delay = (uint32_t)atoi(hold);
	}
	lv_obj_fade_out(splash, FADE_MS, delay);
	lv_obj_delete_delayed(splash, delay + FADE_MS + 50);
	splash = NULL;
	bar = NULL;
	ring = NULL;
}
