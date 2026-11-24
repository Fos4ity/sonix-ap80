#include "appearance.h"

#include <stdint.h>
#include <stdio.h>

#include "lvgl/lvgl.h"
#include "src/gui/shell/splash.h"

#include "src/gui/fonts/fonts.h"
#include "src/gui/nowplaying/player.h"
#include "src/gui/settings/bootlogopage.h"
#include "src/gui/settings/menustylepage.h"
#include "src/gui/settings/playerstylepage.h"
#include "src/gui/settings/wallpaperpage.h"
#include "src/gui/shell/corners.h"
#include "src/gui/shell/iconpack.h"
#include "src/gui/settings/lookpicker.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/toast.h"
#include <string.h>
#include "src/gui/shell/theme.h"
#include "src/gui/shell/topbar.h"
#include "src/system/core/config.h"
#include "src/system/core/lang.h"

lv_obj_t *appearance_screen;

static lv_obj_t *btn_clock[4]; // left / centre / right / hidden
static lv_obj_t *btn_accent[THEME_ACCENT_COUNT]; // the coloured circles
static lv_obj_t *tint_toggle;
static lv_obj_t *hue_slider;
static lv_obj_t *hue_val_label;
static lv_obj_t *sat_slider;
static lv_obj_t *sat_val_label;
static lv_obj_t *battery_percent_toggle;
static lv_obj_t *statusbar_always_toggle;
static lv_obj_t *rounded_corners_toggle;
static lv_obj_t *text_size_pill[2]; // normal / large
// The status bar draws the battery either way; this setting is only about the
// number beside it, and is on by default.
static void battery_percent_cb(lv_event_t *e) {
	(void)e;
	bool shown = lv_obj_has_state(battery_percent_toggle, LV_STATE_CHECKED);
	topbar_set_battery_percent(shown);
	config_set_int("screen", "battery_percent", shown ? 1 : 0);
	config_save();
}

static void statusbar_always_cb(lv_event_t *e) {
	(void)e;
	bool on = lv_obj_has_state(statusbar_always_toggle, LV_STATE_CHECKED);
	player_set_statusbar_always(on);
	config_set_int("screen", "statusbar_always", on ? 1 : 0);
	config_save();
}

static void rounded_corners_cb(lv_event_t *e) {
	(void)e;
	corners_set_enabled(lv_obj_has_state(rounded_corners_toggle, LV_STATE_CHECKED));
}

// The accent's hue spread over the backgrounds and cards. theme_set_dynamic_tint
// repaints the interface itself, including this page.
static bool appearance_czech(void) {
	const char *l = lang_current();
	return l && strncmp(l, "\xC4\x8C" "e", 3) == 0; // "Čeština"
}

static void sync_tint_toggle(void) {
	if (!tint_toggle) return;
	if (theme_dynamic_tint()) {
		lv_obj_add_state(tint_toggle, LV_STATE_CHECKED);
	} else {
		lv_obj_remove_state(tint_toggle, LV_STATE_CHECKED);
	}
}

static void refresh_nothing_toggle(void); // the Nothing preset's switch, below

// Settings -> Appearance -> Theme: one row for the whole palette. A tap opens
// the list of Sonix's own dark and light, the colour themes and the skins on
// the card, each painted in its own colours.
static lv_obj_t *theme_value;

static void refresh_theme_row(void) {
	if (theme_value) {
		lv_label_set_text(theme_value, lookpicker_theme_name());
	}
}

static void dynamic_tint_cb(lv_event_t *e) {
	(void)e;
	bool on = lv_obj_has_state(tint_toggle, LV_STATE_CHECKED);
	bool had_skin = theme_get_skin() >= 0;
	theme_set_dynamic_tint(on); // switches a Compas skin off
	if (on && had_skin) toast_success(appearance_czech() ? "Skin vypnut" : "Skin turned off");
	refresh_theme_row();
	refresh_nothing_toggle();
}

// Settings -> Appearance -> Font: a tap steps through the built-in font and
// every font in resource/fonts/custom, applied at once.
static lv_obj_t *font_value;

static void refresh_font_row(void) {
	if (!font_value) return;
	const char *cur = fonts_get_custom();
	lv_label_set_text(font_value, cur[0] ? cur : "MiSans");
}

static void font_picked(void) {
	refresh_font_row();
	refresh_nothing_toggle();
}

// A tap opens the list of fonts, each drawn in its own face.
static void font_next_cb(lv_event_t *e) {
	(void)e;
	if (switcher_back_drag_active()) {
		return;
	}
	lookpicker_fonts(appearance_czech() ? "P\xC3\xADsmo" : "Font", font_picked);
}

// Settings -> Appearance -> Icons: a tap steps through the built-in icons and
// every pack in resource/icons.
static lv_obj_t *iconpack_value;

static void refresh_iconpack_row(void) {
	if (!iconpack_value) return;
	const char *cur = iconpack_current();
	lv_label_set_text(iconpack_value, cur[0] ? cur : "Sonix");
}

static void iconpack_picked(void) {
	refresh_iconpack_row();
	refresh_nothing_toggle();
}

// A tap opens the list of packs, each shown as a strip of its icons.
static void iconpack_next_cb(lv_event_t *e) {
	(void)e;
	if (switcher_back_drag_active()) {
		return;
	}
	lookpicker_icons(appearance_czech() ? "Ikony" : "Icons", iconpack_picked);
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

// The Nothing preset: one switch for the whole look -- the "Nothing" skin
// (black, red accent), the dot-matrix font and the Nothing icon pack. On when
// all three are in place; switching it off goes back to Sonix's own skin-less
// look, built-in font and icons. The pieces stay separately choosable below.
#define NOTHING_SKIN "Nothing"
#define NOTHING_SKIN_LIGHT "Nothing Light"
#define NOTHING_FONT "Nothing Dot"
static lv_obj_t *nothing_toggle;

static int nothing_skin_index(const char *name) {
	for (int i = 0; i < theme_skin_count(); i++) {
		if (strcmp(theme_skin_name(i), name) == 0) return i;
	}
	return -1;
}

static bool nothing_active(void) {
	int idx = theme_get_skin();
	if (idx < 0) return false;
	const char *skin = theme_skin_name(idx);
	if (strcmp(skin, NOTHING_SKIN) != 0 && strcmp(skin, NOTHING_SKIN_LIGHT) != 0) return false;
	return strcmp(fonts_get_custom(), NOTHING_FONT) == 0 && strcmp(iconpack_current(), skin) == 0;
}

static void refresh_nothing_toggle(void) {
	if (!nothing_toggle) return;
	if (nothing_active()) {
		lv_obj_add_state(nothing_toggle, LV_STATE_CHECKED);
	} else {
		lv_obj_remove_state(nothing_toggle, LV_STATE_CHECKED);
	}
}

static void nothing_toggle_cb(lv_event_t *e) {
	(void)e;
	bool on = lv_obj_has_state(nothing_toggle, LV_STATE_CHECKED);
	if (on) {
		int idx = nothing_skin_index(NOTHING_SKIN);
		if (idx < 0) {
			toast_error(appearance_czech() ? "Chyb\xC3\xAD sonix/resource/themes/Nothing.theme"
										   : "sonix/resource/themes/Nothing.theme is missing");
			refresh_nothing_toggle();
			return;
		}
		theme_set_skin(idx); // switches the dynamic tint off
		fonts_custom_count();
		bool font_ok = fonts_set_custom(NOTHING_FONT);
		iconpack_count();
		bool icons_ok = iconpack_set(NOTHING_SKIN) > 0;
		if (font_ok && icons_ok) {
			toast_success(appearance_czech() ? "Nothing: skin, p\xC3\xADsmo a ikony" : "Nothing: skin, font and icons");
		} else {
			toast_error(appearance_czech() ? "Nothing: chyb\xC3\xAD p\xC3\xADsmo nebo ikony na kart\xC4\x9B"
										   : "Nothing: font or icons missing on the card");
		}
	} else {
		theme_set_skin(-1);
		fonts_set_custom("");
		iconpack_set("");
	}
	sync_tint_toggle();
	refresh_theme_row();
	refresh_font_row();
	refresh_iconpack_row();
	refresh_nothing_toggle();
}


static lv_obj_t *splash_value;
static lv_obj_t *splash_toggle;

// The one shown when they do not take turns, in splash.c's order.
static void refresh_splash_row(void) {
	static const char *const cs[] = {"Dlaždice", "Deska", "Minimal"};
	static const char *const en[] = {"Tile", "Record", "Minimal"};
	int s = (int)config_get_int("ui", "splash_style", 0);
	if (s < 0 || s >= SPLASH_STYLES) {
		s = 0;
	}
	bool random = config_get_bool("ui", "splash_random", true);
	if (random) {
		lv_label_set_text(splash_value, appearance_czech() ? "Náhodně" : "Random");
	} else {
		lv_label_set_text(splash_value, (appearance_czech() ? cs : en)[s]);
	}
	if (random) {
		lv_obj_add_state(splash_toggle, LV_STATE_CHECKED);
	} else {
		lv_obj_remove_state(splash_toggle, LV_STATE_CHECKED);
	}
}

static void splash_toggle_cb(lv_event_t *e) {
	(void)e;
	config_set_bool("ui", "splash_random", lv_obj_has_state(splash_toggle, LV_STATE_CHECKED));
	config_save();
	refresh_splash_row();
}

// A tap picks the next one and makes it the only one.
static void splash_next_cb(lv_event_t *e) {
	(void)e;
	int s = (int)config_get_int("ui", "splash_style", 0);
	if (!config_get_bool("ui", "splash_random", true)) {
		s = (s + 1) % SPLASH_STYLES;
	}
	config_set_int("ui", "splash_style", s);
	config_set_bool("ui", "splash_random", false);
	config_save();
	refresh_splash_row();
}

// The active circle wears a ring in the text colour; the rest sit flat.
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

	int h = 0, s = 85;
	theme_get_active_accent_hs(&h, &s);
	char buf[16];

	if (hue_slider) {
		lv_slider_set_value(hue_slider, h, LV_ANIM_OFF);
		theme_apply_slider_knob(hue_slider);
	}
	if (hue_val_label) {
		snprintf(buf, sizeof(buf), "%d\xC2\xB0", h);
		lv_label_set_text(hue_val_label, buf);
	}
	if (sat_slider) {
		lv_slider_set_value(sat_slider, s, LV_ANIM_OFF);
		theme_apply_slider_knob(sat_slider);
	}
	if (sat_val_label) {
		snprintf(buf, sizeof(buf), "%d%%", s);
		lv_label_set_text(sat_val_label, buf);
	}
}

static void accent_pick_cb(lv_event_t *e) {
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

static void hue_slider_cb(lv_event_t *e) {
	(void)e;
	if (!hue_slider) {
		return;
	}
	theme_set_custom_accent_hue((int)lv_slider_get_value(hue_slider));
}

static void sat_slider_cb(lv_event_t *e) {
	(void)e;
	if (!sat_slider) {
		return;
	}
	theme_set_custom_accent_saturation((int)lv_slider_get_value(sat_slider));
}

static void hs_slider_released_cb(lv_event_t *e) {
	(void)e;
	theme_commit_custom_accent();
}

static lv_obj_t *make_hs_slider(lv_obj_t *card, const char *title, int min_v, int max_v,
								lv_obj_t **val_label_out, lv_event_cb_t change_cb) {
	lv_obj_t *hdr = lv_obj_create(card);
	lv_obj_set_size(hdr, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(hdr, 0, 0);
	lv_obj_set_style_border_width(hdr, 0, 0);
	lv_obj_set_style_pad_all(hdr, 0, 0);
	lv_obj_set_scrollable(hdr, false);
	lv_obj_set_clickable(hdr, false);
	lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	lv_obj_t *lbl = lv_label_create(hdr);
	lv_label_set_text(lbl, title);
	lv_obj_add_style(lbl, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(lbl, &font_ui_20, 0);

	lv_obj_t *val = lv_label_create(hdr);
	lv_label_set_text(val, "");
	lv_obj_add_style(val, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(val, &font_ui_20, 0);
	if (val_label_out) {
		*val_label_out = val;
	}

	// Dedicated 56 px touch wrapper so LVGL's parent bounding box never clips
	// the slider's extended touch target. 20 px horizontal padding keeps the
	// 34 px knob + shadow unclipped at 0% and 100%, and event bubbling preserves
	// swipe-back/scroll gestures around the slider.
	lv_obj_t *wrap = lv_obj_create(card);
	lv_obj_set_size(wrap, lv_pct(100), 56);
	lv_obj_set_style_bg_opa(wrap, 0, 0);
	lv_obj_set_style_border_width(wrap, 0, 0);
	lv_obj_set_style_pad_ver(wrap, 0, 0);
	lv_obj_set_style_pad_hor(wrap, 20, 0);
	lv_obj_set_scrollable(wrap, false);
	lv_obj_set_event_bubble(wrap, true);

	lv_obj_t *s = lv_slider_create(wrap);
	lv_obj_set_width(s, lv_pct(100));
	lv_obj_set_height(s, 14);
	lv_obj_center(s);
	lv_slider_set_range(s, min_v, max_v);
	lv_obj_add_style(s, &theme_style_slider_track, LV_PART_MAIN);
	lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
	lv_obj_add_style(s, &theme_style_accent_bg, LV_PART_INDICATOR);
	lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_KNOB);
	lv_obj_set_style_bg_color(s, lv_color_white(), LV_PART_KNOB);
	theme_apply_slider_knob(s);
	lv_obj_set_style_pad_all(s, 10, LV_PART_KNOB);
	lv_obj_set_style_shadow_width(s, 8, LV_PART_KNOB);
	lv_obj_set_style_shadow_opa(s, LV_OPA_30, LV_PART_KNOB);
	lv_obj_set_style_shadow_color(s, lv_color_black(), LV_PART_KNOB);
	lv_obj_set_style_shadow_offset_y(s, 1, LV_PART_KNOB);
	lv_obj_set_ext_click_area(s, 28);
	lv_obj_add_event_cb(s, change_cb, LV_EVENT_VALUE_CHANGED, NULL);
	lv_obj_add_event_cb(s, hs_slider_released_cb, LV_EVENT_RELEASED, NULL);
	lv_obj_add_event_cb(s, hs_slider_released_cb, LV_EVENT_PRESS_LOST, NULL);
	return s;
}

// Paints the clock-position pills the same way: accent on the active one.
static void refresh_clock_buttons(void) {
	int active = (int)config_get_int("screen", "clock_pos", TOPBAR_CLOCK_CENTER);
	for (int i = 0; i < 4; i++) {
		if (!btn_clock[i]) {
			continue;
		}
		if (i == active) {
			lv_obj_set_style_bg_color(btn_clock[i], theme()->accent, 0);
			lv_obj_set_style_text_color(lv_obj_get_child(btn_clock[i], 0), lv_color_white(), 0);
		} else {
			lv_obj_set_style_bg_color(btn_clock[i], theme()->surface_pressed, 0);
			lv_obj_set_style_text_color(lv_obj_get_child(btn_clock[i], 0), theme()->text_primary, 0);
		}
	}
}

static void clock_pos_cb(lv_event_t *e) {
	int pos = (int)(intptr_t)lv_event_get_user_data(e);
	config_set_int("screen", "clock_pos", pos);
	config_save();
	topbar_set_clock_position(pos);
	refresh_clock_buttons();
}

static lv_obj_t *make_clock_choice(lv_obj_t *parent, const char *text, int pos) {
	lv_obj_t *btn = lv_btn_create(parent);
	lv_obj_set_size(btn, LV_SIZE_CONTENT, 64);
	lv_obj_set_style_pad_hor(btn, 14, 0);
	lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0); // Adwaita pill button
	lv_obj_set_style_shadow_width(btn, 0, 0);
	lv_obj_set_style_border_width(btn, 0, 0);
	lv_obj_add_event_cb(btn, clock_pos_cb, LV_EVENT_CLICKED, (void *)(intptr_t)pos);

	lv_obj_t *label = lv_label_create(btn);
	lv_label_set_text(label, tr(text));
	lv_obj_set_style_text_font(label, &font_ui_24, 0);
	lv_obj_center(label);

	return btn;
}

// The page's own widgets after the palette moved, whoever moved it.
static void refresh_theme_widgets(void) {
	refresh_theme_row();
	sync_tint_toggle();
	refresh_nothing_toggle();
}

static void theme_picked(void) {
	refresh_theme_widgets();
	refresh_iconpack_row();
	refresh_accent_buttons();
}

static void theme_open_cb(lv_event_t *e) {
	(void)e;
	if (switcher_back_drag_active()) {
		return;
	}
	lookpicker_themes(tr("appearance_theme"), theme_picked);
}

// A card with a name on top and a row of choices under it.
static lv_obj_t *make_choice_card(lv_obj_t *container, const char *title, int gap, lv_flex_flow_t flow,
								  lv_flex_align_t main_align, lv_obj_t **card_out) {
	lv_obj_t *card = lv_obj_create(container);
	lv_obj_set_width(card, lv_pct(100));
	lv_obj_set_height(card, LV_SIZE_CONTENT);
	lv_obj_add_style(card, &theme_style_card, 0);
	lv_obj_set_style_radius(card, 12, 0);
	lv_obj_set_style_border_width(card, 0, 0);
	lv_obj_set_style_shadow_width(card, 0, 0);
	lv_obj_set_style_pad_all(card, 20, 0);
	lv_obj_set_style_pad_gap(card, 18, 0);
	lv_obj_set_scrollable(card, false);
	lv_obj_set_event_bubble(card, true);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

	lv_obj_t *name = lv_label_create(card);
	lv_label_set_text(name, tr(title));
	lv_obj_add_style(name, &theme_style_text, 0);
	lv_obj_set_style_text_font(name, &font_ui_24, 0);

	lv_obj_t *row = lv_obj_create(card);
	lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(row, 0, 0);
	lv_obj_set_style_border_width(row, 0, 0);
	lv_obj_set_style_pad_all(row, 0, 0);
	lv_obj_set_style_pad_gap(row, gap, 0);
	lv_obj_set_scrollable(row, false);
	lv_obj_set_flex_flow(row, flow);
	lv_obj_set_flex_align(row, main_align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
	if (card_out) {
		*card_out = card;
	}
	return row;
}

static lv_obj_t *add_note(lv_obj_t *container, const char *text) {
	lv_obj_t *note = lv_label_create(container);
	lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(note, lv_pct(100));
	lv_obj_add_style(note, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(note, &font_ui_22, 0);
	lv_label_set_text(note, text);
	return note;
}

// Five groups, each under its heading, in the order one sets a look up: the
// colours, the letters and pictures, how the pages are laid out, the status
// bar, and what shows while the player starts.
void appearance_init(gui_config_t *cfg) {
	lv_obj_t *container = settingsrow_page(appearance_screen, cfg, "appearance");
	bool cz = appearance_czech();

	// ---- Theme and colours -------------------------------------------------
	settingsrow_heading(container, cz ? "Motiv a barvy" : "Theme and colours");

	// One row for the whole palette: Sonix dark and light, the colour themes
	// (from SonixOS HUE) and the Compas skins from <resource>/themes.
	settingsrow_add(container, "appearance_theme", &theme_value, theme_open_cb, NULL);
	refresh_theme_row();

	// One switch for the whole Nothing look: skin, font and icons. Shown only
	// when the Nothing skin is on the card.
	if (nothing_skin_index(NOTHING_SKIN) >= 0) {
		settingsrow_toggle(container, cz ? "Preset Nothing" : "Nothing preset", &nothing_toggle, nothing_toggle_cb);
		refresh_nothing_toggle();
	}

	// The accent colour, as a row of coloured circles, and any colour at all
	// under it: hue and saturation (from SonixOS HUE).
	lv_obj_t *accent_card;
	lv_obj_t *accent_row = make_choice_card(container, "appearance_accent_colour", 14, LV_FLEX_FLOW_ROW,
											LV_FLEX_ALIGN_SPACE_BETWEEN, &accent_card);
	for (int i = 0; i < THEME_ACCENT_COUNT; i++) {
		btn_accent[i] = make_accent_circle(accent_row, i);
	}
	hue_slider = make_hs_slider(accent_card, cz ? "Vlastní odstín" : "Custom hue", 0, 359, &hue_val_label,
								hue_slider_cb);
	sat_slider = make_hs_slider(accent_card, cz ? "Vlastní sytost" : "Custom saturation", 0, 100, &sat_val_label,
								sat_slider_cb);

	// Straight under the accent, because it is what the tint is made from: the
	// same hue carried across the backgrounds, the cards and the status bar.
	settingsrow_toggle(container, "appearance_dynamic_tint", &tint_toggle, dynamic_tint_cb);
	sync_tint_toggle();
	char tint_text[400];
	snprintf(tint_text, sizeof(tint_text), "%s %s", tr("appearance_dynamic_tint_note"),
			 cz ? "Nejde spolu se skinem: zapnutí jedno vypne druhé."
				: "Not together with a skin: turning one on turns the other off.");
	add_note(container, tint_text);

	settingsrow_add(container, cz ? "Tapeta" : "Wallpaper", NULL, switch_screen_cb, wallpaperpage_screen);

	// ---- Font and icons ----------------------------------------------------
	settingsrow_heading(container, cz ? "Písmo a ikony" : "Font and icons");
	settingsrow_add(container, cz ? "P\xC3\xADsmo" : "Font", &font_value, font_next_cb, NULL);
	refresh_font_row();
	settingsrow_add(container, cz ? "Ikony" : "Icons", &iconpack_value, iconpack_next_cb, NULL);
	refresh_iconpack_row();
	// How big the small text on every page is: the secondary lines, notes,
	// clocks and counters. See fonts.c for which sizes grow and by how much.
	lv_obj_t *text_size_pills;
	settingsrow_pills(container, "appearance_text_size", &text_size_pills);
	text_size_pill[FONTS_TEXT_NORMAL] =
		settingsrow_pill(text_size_pills, "appearance_text_normal", FONTS_TEXT_NORMAL, text_size_cb);
	text_size_pill[FONTS_TEXT_LARGE] =
		settingsrow_pill(text_size_pills, "appearance_text_large", FONTS_TEXT_LARGE, text_size_cb);

	// ---- Layout ------------------------------------------------------------
	settingsrow_heading(container, cz ? "Rozložení" : "Layout");
	settingsrow_add(container, cz ? "Vzhled přehrávače" : "Player style", NULL, switch_screen_cb,
					playerstylepage_screen);
	settingsrow_add(container, cz ? "Vzhled menu a lišty" : "Menu & bar style", NULL, switch_screen_cb,
					menustylepage_screen);
	// Rounds off the four corners of the screen, over every page and overlay.
	settingsrow_toggle(container, "appearance_rounded_corners", &rounded_corners_toggle, rounded_corners_cb);
	if (corners_enabled()) {
		lv_obj_add_state(rounded_corners_toggle, LV_STATE_CHECKED);
	}

	// ---- Status bar --------------------------------------------------------
	settingsrow_heading(container, cz ? "Stavový řádek" : "Status bar");
	// Wrapped, not a single line: four pills of translated words do not fit
	// across the panel in every language.
	lv_obj_t *clock_row = make_choice_card(container, "appearance_clock_position", 10, LV_FLEX_FLOW_ROW_WRAP,
										   LV_FLEX_ALIGN_START, NULL);
	btn_clock[TOPBAR_CLOCK_LEFT] = make_clock_choice(clock_row, "left", TOPBAR_CLOCK_LEFT);
	btn_clock[TOPBAR_CLOCK_CENTER] = make_clock_choice(clock_row, "centre", TOPBAR_CLOCK_CENTER);
	btn_clock[TOPBAR_CLOCK_RIGHT] = make_clock_choice(clock_row, "right", TOPBAR_CLOCK_RIGHT);
	btn_clock[TOPBAR_CLOCK_HIDDEN] = make_clock_choice(clock_row, "hide", TOPBAR_CLOCK_HIDDEN);
	// Whether the bar prints the charge as a number as well as drawing it.
	settingsrow_toggle(container, "appearance_battery_percentage", &battery_percent_toggle, battery_percent_cb);
	if (config_get_int("screen", "battery_percent", 1) != 0) {
		lv_obj_add_state(battery_percent_toggle, LV_STATE_CHECKED);
	}
	// The bar up in the player too, where it otherwise steps aside for the
	// artwork.
	settingsrow_toggle(container, "appearance_statusbar_always", &statusbar_always_toggle, statusbar_always_cb);
	if (player_statusbar_always()) {
		lv_obj_add_state(statusbar_always_toggle, LV_STATE_CHECKED);
	}
	add_note(container, tr("appearance_statusbar_always_note"));

	// ---- Start -------------------------------------------------------------
	settingsrow_heading(container, cz ? "Spuštění" : "Start");
	// The start screen: one of the three, or a different one each start.
	settingsrow_add(container, cz ? "Úvodní obrazovka" : "Start screen", &splash_value, splash_next_cb, NULL);
	settingsrow_toggle(container, cz ? "Střídat úvodní obrazovky" : "Rotate start screens", &splash_toggle,
					   splash_toggle_cb);
	refresh_splash_row();
	// The picture shown while the player boots, which follows the theme and
	// the accent unless one of the user's own is chosen.
	settingsrow_add(container, "bootlogo", NULL, switch_screen_cb, bootlogopage_screen);

	refresh_clock_buttons();
	refresh_accent_buttons();
	refresh_text_size_pills();
	theme_register_refresh(refresh_theme_widgets);
	theme_register_refresh(refresh_text_size_pills);
	theme_register_refresh(refresh_clock_buttons);
	theme_register_refresh(refresh_accent_buttons);
}
