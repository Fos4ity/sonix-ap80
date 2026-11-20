#include "appearance.h"

#include <stdint.h>

#include "lvgl/lvgl.h"

#include "src/gui/fonts/fonts.h"
#include "src/gui/settings/bootlogopage.h"
#include "src/gui/settings/menustylepage.h"
#include "src/gui/settings/playerstylepage.h"
#include "src/gui/settings/wallpaperpage.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/theme.h"
#include "src/gui/shell/toast.h"
#include "src/system/core/config.h"
#include "src/system/core/lang.h"

lv_obj_t *appearance_screen;

static lv_obj_t *btn_theme[THEME_PRESET_COUNT];
static lv_obj_t *btn_accent[THEME_ACCENT_COUNT]; // the coloured circles
static lv_obj_t *hue_slider;
static lv_obj_t *tint_toggle;
static lv_obj_t *text_size_pill[2]; // normal / large
static lv_obj_t *font_family_pill[FONTS_FAMILY_COUNT];

static void refresh_font_family_pills(void) {
	int chosen = fonts_get_family();
	for (int i = 0; i < FONTS_FAMILY_COUNT; i++) {
		if (font_family_pill[i]) {
			settingsrow_pill_active(font_family_pill[i], i == chosen);
		}
	}
}

static void font_family_pick_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) {
		return;
	}
	int family = (int)(intptr_t)lv_event_get_user_data(e);
	if (!fonts_family_available(family)) {
		toast_plain("Put font in .local/fonts on SD");
		return;
	}
	if (!fonts_set_family(family)) {
		return;
	}
	config_set_int("ui", "font_family", family);
	config_save();
	refresh_font_family_pills();
}

// The accent's hue spread over the backgrounds and cards. theme_set_dynamic_tint
// repaints the interface itself, including this page.
static void dynamic_tint_cb(lv_event_t *e) {
	(void)e;
	theme_set_dynamic_tint(lv_obj_has_state(tint_toggle, LV_STATE_CHECKED));
}

static void refresh_text_size_pills(void) {
	int active = fonts_large_text() ? FONTS_TEXT_LARGE : FONTS_TEXT_NORMAL;
	for (int i = 0; i < 2; i++) {
		settingsrow_pill_active(text_size_pill[i], i == active);
	}
}

// Applied at once, on every page: see fonts_set_large_text().
static void text_size_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) {
		return;
	}
	int size = (int)(intptr_t)lv_event_get_user_data(e);
	if (!fonts_set_large_text(size == FONTS_TEXT_LARGE)) {
		return;
	}
	config_set_int("ui", "text_size", size);
	config_save();
	refresh_text_size_pills();
}

static void refresh_buttons(void) {
	int chosen = theme_get_preset();
	for (int i = 0; i < THEME_PRESET_COUNT; i++) {
		if (btn_theme[i]) {
			settingsrow_pill_active(btn_theme[i], i == chosen);
		}
	}
}

// The active circle wears a ring in the text colour when a custom accent
// override is active; otherwise all circles sit flat (preset's own accent).
static void refresh_accent_buttons(void) {
	bool has_override = theme_has_custom_accent();
	bool has_hue = theme_has_custom_hue();
	int active_accent = theme_get_accent();
	for (int i = 0; i < THEME_ACCENT_COUNT; i++) {
		if (!btn_accent[i]) {
			continue;
		}
		lv_obj_set_style_bg_color(btn_accent[i], theme_accent_preset(i), 0);
		if (has_override && !has_hue && i == active_accent) {
			lv_obj_set_style_border_width(btn_accent[i], 3, 0);
			lv_obj_set_style_border_color(btn_accent[i], theme()->text_primary, 0);
			lv_obj_set_style_border_opa(btn_accent[i], LV_OPA_COVER, 0);
		} else {
			lv_obj_set_style_border_width(btn_accent[i], 0, 0);
		}
	}
	if (hue_slider) {
		int h = theme_get_custom_accent_hue();
		if (h >= 0) {
			lv_slider_set_value(hue_slider, h, LV_ANIM_OFF);
		}
		theme_apply_slider_knob(hue_slider);
	}
}

static void hue_slider_cb(lv_event_t *e) {
	(void)e;
	if (!hue_slider) {
		return;
	}
	theme_set_custom_accent_hue((int)lv_slider_get_value(hue_slider));
	refresh_accent_buttons();
}

static void accent_pick_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) {
		return;
	}
	int index = (int)(intptr_t)lv_event_get_user_data(e);
	theme_set_accent(index);
	refresh_accent_buttons();
}

static lv_obj_t *make_accent_circle(lv_obj_t *parent, int index) {
	lv_obj_t *btn = lv_btn_create(parent);
	lv_obj_set_size(btn, 48, 48);
	lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_shadow_width(btn, 0, 0);
	lv_obj_set_style_bg_color(btn, theme_accent_preset(index), 0);
	lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
	lv_obj_add_event_cb(btn, accent_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);
	return btn;
}

static void pick_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) {
		return;
	}
	int preset = (int)(intptr_t)lv_event_get_user_data(e);
	theme_set_preset(preset);
	refresh_buttons();
	refresh_accent_buttons();
}

void appearance_init(gui_config_t *cfg) {
	lv_obj_t *container = settingsrow_page(appearance_screen, cfg, "appearance");

	// Theme selector: Omarchy palettes (CpUnk, Miasma, Ash, Matte Black, Tokyo Night, Gruvbox) + Light.
	lv_obj_t *theme_pills;
	settingsrow_pills(container, "appearance_theme", &theme_pills);
	for (int i = 0; i < THEME_PRESET_COUNT; i++) {
		btn_theme[i] = settingsrow_pill(theme_pills, theme_preset_name(i), i, pick_cb);
	}

	// Accent colour override: a row of coloured circles.
	lv_obj_t *accent_card = lv_obj_create(container);
	lv_obj_set_width(accent_card, lv_pct(100));
	lv_obj_set_height(accent_card, LV_SIZE_CONTENT);
	lv_obj_add_style(accent_card, &theme_style_card, 0);
	lv_obj_set_style_shadow_width(accent_card, 0, 0);
	lv_obj_set_style_pad_all(accent_card, 20, 0);
	lv_obj_set_style_pad_gap(accent_card, 18, 0);
	lv_obj_set_scrollable(accent_card, false);
	lv_obj_set_event_bubble(accent_card, true);
	lv_obj_set_flex_flow(accent_card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(accent_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

	lv_obj_t *accent_name = lv_label_create(accent_card);
	lv_label_set_text(accent_name, tr("appearance_accent_colour"));
	lv_obj_add_style(accent_name, &theme_style_text, 0);
	lv_obj_set_style_text_font(accent_name, &font_ui_24, 0);

	lv_obj_t *accent_row = lv_obj_create(accent_card);
	lv_obj_set_size(accent_row, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(accent_row, 0, 0);
	lv_obj_set_style_border_width(accent_row, 0, 0);
	lv_obj_set_style_pad_all(accent_row, 0, 0);
	lv_obj_set_style_pad_gap(accent_row, 14, 0);
	lv_obj_set_scrollable(accent_row, false);
	lv_obj_set_flex_flow(accent_row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(accent_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	for (int i = 0; i < THEME_ACCENT_COUNT; i++) {
		btn_accent[i] = make_accent_circle(accent_row, i);
	}

	lv_obj_t *hue_label = lv_label_create(accent_card);
	lv_label_set_text(hue_label, "Custom hue");
	lv_obj_add_style(hue_label, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(hue_label, &font_ui_20, 0);

	hue_slider = lv_slider_create(accent_card);
	lv_obj_set_width(hue_slider, lv_pct(100));
	lv_obj_set_height(hue_slider, 10);
	lv_slider_set_range(hue_slider, 0, 359);
	lv_obj_add_style(hue_slider, &theme_style_slider_track, LV_PART_MAIN);
	lv_obj_set_style_radius(hue_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
	lv_obj_add_style(hue_slider, &theme_style_accent_bg, LV_PART_INDICATOR);
	lv_obj_set_style_radius(hue_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
	lv_obj_set_style_bg_color(hue_slider, lv_color_white(), LV_PART_KNOB);
	theme_apply_slider_knob(hue_slider);
	lv_obj_set_style_pad_all(hue_slider, 8, LV_PART_KNOB);
	lv_obj_set_ext_click_area(hue_slider, 28);
	lv_obj_add_event_cb(hue_slider, hue_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

	// Straight under the accent, because it is what the tint is made from: the
	// same hue carried across the backgrounds, the cards and the status bar.
	settingsrow_toggle(container, "appearance_dynamic_tint", &tint_toggle, dynamic_tint_cb);
	if (theme_dynamic_tint()) {
		lv_obj_add_state(tint_toggle, LV_STATE_CHECKED);
	}

	lv_obj_t *tint_note = lv_label_create(container);
	lv_label_set_long_mode(tint_note, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(tint_note, lv_pct(100));
	lv_obj_add_style(tint_note, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(tint_note, &font_ui_22, 0);
	lv_label_set_text(tint_note, tr("appearance_dynamic_tint_note"));

	// Dedicated sub-pages for Player visuals, Menu & bar visuals, Wallpaper, and Boot logo.
	settingsrow_add(container, "Player style", NULL, switch_screen_cb, playerstylepage_screen);
	settingsrow_add(container, "Menu & bar style", NULL, switch_screen_cb, menustylepage_screen);
	settingsrow_add(container, "Menu wallpaper", NULL, switch_screen_cb, wallpaperpage_screen);
	settingsrow_add(container, "bootlogo", NULL, switch_screen_cb, bootlogopage_screen);

	// UI font family (Default, JetBrains, Victor, Bookerly) — switches live.
	lv_obj_t *font_pills;
	settingsrow_pills(container, "UI font", &font_pills);
	for (int i = 0; i < FONTS_FAMILY_COUNT; i++) {
		font_family_pill[i] = settingsrow_pill(font_pills, fonts_family_name(i), i, font_family_pick_cb);
	}

	// How big the small text on every page is: the secondary lines, notes,
	// clocks and counters. See fonts.c for which sizes grow and by how much.
	lv_obj_t *text_size_pills;
	settingsrow_pills(container, "appearance_text_size", &text_size_pills);
	text_size_pill[FONTS_TEXT_NORMAL] =
		settingsrow_pill(text_size_pills, "appearance_text_normal", FONTS_TEXT_NORMAL, text_size_cb);
	text_size_pill[FONTS_TEXT_LARGE] =
		settingsrow_pill(text_size_pills, "appearance_text_large", FONTS_TEXT_LARGE, text_size_cb);

	refresh_buttons();
	refresh_accent_buttons();
	refresh_text_size_pills();
	refresh_font_family_pills();
	theme_register_refresh(refresh_buttons);
	theme_register_refresh(refresh_text_size_pills);
	theme_register_refresh(refresh_font_family_pills);
	theme_register_refresh(refresh_accent_buttons);
}
