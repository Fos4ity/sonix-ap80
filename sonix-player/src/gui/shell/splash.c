#include "splash.h"

#include <stdio.h>
#include <stdlib.h>

#include "lvgl/lvgl.h"

#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/theme.h"
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

	const sysinfo_model_t *model = sysinfo_model();
	if (model && model->name) {
		lv_obj_t *player = lv_label_create(column);
		lv_label_set_text(player, model->name);
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
	if (!splash || !bar) {
		return;
	}
	lv_bar_set_value(bar, percent < 0 ? 0 : percent > 100 ? 100 : percent, LV_ANIM_OFF);
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
}
