#include "appearance.h"

#include <stdint.h>
#include <stdio.h>

#include "lvgl/lvgl.h"
#include "src/gui/shell/splash.h"

#include "src/gui/fonts/fonts.h"
#include "src/gui/nowplaying/player.h"
#include "src/gui/settings/bootlogopage.h"
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

static lv_obj_t *btn_dark;
static lv_obj_t *btn_light;
static lv_obj_t *btn_clock[4]; // left / centre / right / hidden
static lv_obj_t *btn_accent[THEME_ACCENT_COUNT]; // the coloured circles
static lv_obj_t *tint_toggle;
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

static void dynamic_tint_cb(lv_event_t *e) {
	(void)e;
	bool on = lv_obj_has_state(tint_toggle, LV_STATE_CHECKED);
	bool had_skin = theme_get_skin() >= 0;
	theme_set_dynamic_tint(on); // switches a Compas skin off
	if (on && had_skin) toast_success(appearance_czech() ? "Compas skin vypnut" : "Compas skin turned off");
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

// Paints the pair: the active choice is the filled accent button, the other a
// quiet neutral, like libadwaita's suggested-action beside a regular button.
static lv_obj_t *skin_toggle;
static lv_obj_t *skin_value;
static int skin_last = 0; // the skin the toggle brings back

static void refresh_skin_row(void) {
	int idx = theme_get_skin();
	if (skin_toggle) {
		if (idx >= 0) {
			lv_obj_add_state(skin_toggle, LV_STATE_CHECKED);
		} else {
			lv_obj_remove_state(skin_toggle, LV_STATE_CHECKED);
		}
	}
	if (skin_value) {
		int show = idx >= 0 ? idx : skin_last;
		lv_label_set_text(skin_value, theme_skin_count() ? theme_skin_name(show) : "\xE2\x80\x94");
	}
}

static void skin_toggle_cb(lv_event_t *e) {
	(void)e;
	bool on = lv_obj_has_state(skin_toggle, LV_STATE_CHECKED);
	if (on && theme_skin_count() == 0) {
		lv_obj_remove_state(skin_toggle, LV_STATE_CHECKED);
		return;
	}
	bool had_tint = theme_dynamic_tint();
	theme_set_skin(on ? skin_last : -1); // switches the dynamic tint off
	sync_tint_toggle();
	refresh_nothing_toggle();
	if (on && had_tint) toast_success(appearance_czech() ? "Dynamický odstín vypnut" : "Dynamic tint turned off");
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
		skin_last = idx;
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
	refresh_skin_row();
	refresh_font_row();
	refresh_iconpack_row();
	refresh_nothing_toggle();
}


// A tap on the row steps to the next skin, and switches skins on.
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

static void skin_next_cb(lv_event_t *e) {
	(void)e;
	int n = theme_skin_count();
	if (n == 0) {
		return;
	}
	int idx = theme_get_skin();
	skin_last = idx >= 0 ? (idx + 1) % n : skin_last;
	bool had_tint = theme_dynamic_tint();
	theme_set_skin(skin_last);
	// The two Nothing skins each have an icon pack of the same name (white
	// tiles for the dark one, black for the light): keep them in step.
	const char *skin = theme_skin_name(skin_last);
	const char *pack = iconpack_current();
	if ((strcmp(pack, NOTHING_SKIN) == 0 || strcmp(pack, NOTHING_SKIN_LIGHT) == 0) && strcmp(pack, skin) != 0 &&
		(strcmp(skin, NOTHING_SKIN) == 0 || strcmp(skin, NOTHING_SKIN_LIGHT) == 0)) {
		iconpack_count();
		iconpack_set(skin);
		refresh_iconpack_row();
	}
	sync_tint_toggle();
	refresh_skin_row();
	refresh_nothing_toggle();
	if (had_tint) toast_success(appearance_czech() ? "Dynamický odstín vypnut" : "Dynamic tint turned off");
}

static void refresh_buttons(void) {
	bool dark = theme_is_dark();

	lv_obj_t *on = dark ? btn_dark : btn_light;
	lv_obj_t *off = dark ? btn_light : btn_dark;

	if (theme_get_skin() >= 0) {
		on = NULL; // a skin is neither of the two
	}
	if (theme_get_skin() >= 0) {
		lv_obj_set_style_bg_color(btn_dark, theme()->surface_pressed, 0);
		lv_obj_set_style_text_color(lv_obj_get_child(btn_dark, 0), theme()->text_primary, 0);
		lv_obj_set_style_bg_color(btn_light, theme()->surface_pressed, 0);
		lv_obj_set_style_text_color(lv_obj_get_child(btn_light, 0), theme()->text_primary, 0);
		refresh_skin_row();
		sync_tint_toggle();
		return;
	}
	refresh_skin_row();
	sync_tint_toggle();

	lv_obj_set_style_bg_color(on, theme()->accent, 0);
	lv_obj_set_style_text_color(lv_obj_get_child(on, 0), lv_color_white(), 0);

	lv_obj_set_style_bg_color(off, theme()->surface_pressed, 0);
	lv_obj_set_style_text_color(lv_obj_get_child(off, 0), theme()->text_primary, 0);
}

// The active circle wears a ring in the text colour; the rest sit flat.
static void refresh_accent_buttons(void) {
	int active_accent = theme_get_accent();
	for (int i = 0; i < THEME_ACCENT_COUNT; i++) {
		if (!btn_accent[i]) {
			continue;
		}
		lv_obj_set_style_bg_color(btn_accent[i], theme_accent_preset(i), 0);
		if (i == active_accent) {
			lv_obj_set_style_border_width(btn_accent[i], 3, 0);
			lv_obj_set_style_border_color(btn_accent[i], theme()->text_primary, 0);
			lv_obj_set_style_border_opa(btn_accent[i], LV_OPA_COVER, 0);
		} else {
			lv_obj_set_style_border_width(btn_accent[i], 0, 0);
		}
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

static void pick_cb(lv_event_t *e) {
	bool dark = (bool)(uintptr_t)lv_event_get_user_data(e);
	theme_set_dark(dark);
	refresh_buttons();
}

static lv_obj_t *make_choice(lv_obj_t *parent, const char *text, bool dark) {
	lv_obj_t *btn = lv_btn_create(parent);
	lv_obj_set_size(btn, LV_SIZE_CONTENT, 64);
	lv_obj_set_style_pad_hor(btn, 34, 0);
	lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0); // Adwaita pill button
	lv_obj_set_style_shadow_width(btn, 0, 0);
	lv_obj_set_style_border_width(btn, 0, 0);
	lv_obj_add_event_cb(btn, pick_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)dark);

	lv_obj_t *label = lv_label_create(btn);
	lv_label_set_text(label, tr(text));
	lv_obj_set_style_text_font(label, &font_ui_24, 0);
	lv_obj_center(label);

	return btn;
}

void appearance_init(gui_config_t *cfg) {
	lv_obj_t *container = settingsrow_page(appearance_screen, cfg, "appearance");

	// One tall card: the option's name on top, the two choices under it.
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
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

	lv_obj_t *name = lv_label_create(card);
	lv_label_set_text(name, tr("appearance_theme"));
	lv_obj_add_style(name, &theme_style_text, 0);
	lv_obj_set_style_text_font(name, &font_ui_24, 0);

	lv_obj_t *row = lv_obj_create(card);
	lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(row, 0, 0);
	lv_obj_set_style_border_width(row, 0, 0);
	lv_obj_set_style_pad_all(row, 0, 0);
	lv_obj_set_style_pad_gap(row, 14, 0);
	lv_obj_set_scrollable(row, false);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	btn_dark = make_choice(row, "appearance_dark", true);
	btn_light = make_choice(row, "appearance_light", false);

	// Compas skins from <resource>/themes: a switch, and a row that steps
	// through them on each tap.
	if (theme_get_skin() >= 0) {
		skin_last = theme_get_skin();
	}
	settingsrow_toggle(container, "Compas skin", &skin_toggle, skin_toggle_cb);
	settingsrow_add(container, "Skin", &skin_value, skin_next_cb, NULL);
	// The start screen: one of the three, or a different one each start.
	settingsrow_toggle(container, appearance_czech() ? "Střídat úvodní obrazovky" : "Rotate start screens",
					   &splash_toggle, splash_toggle_cb);
	settingsrow_add(container, appearance_czech() ? "Úvodní obrazovka" : "Start screen",
					&splash_value, splash_next_cb, NULL);
	refresh_splash_row();
	// Shown only when the Nothing skin is on the card.
	if (nothing_skin_index(NOTHING_SKIN) >= 0) {
		settingsrow_toggle(container, appearance_czech() ? "Preset Nothing" : "Nothing preset",
						   &nothing_toggle, nothing_toggle_cb);
		refresh_nothing_toggle();
	}

	// A second card: where the clock sits in the status bar.
	lv_obj_t *clock_card = lv_obj_create(container);
	lv_obj_set_width(clock_card, lv_pct(100));
	lv_obj_set_height(clock_card, LV_SIZE_CONTENT);
	lv_obj_add_style(clock_card, &theme_style_card, 0);
	lv_obj_set_style_radius(clock_card, 12, 0);
	lv_obj_set_style_border_width(clock_card, 0, 0);
	lv_obj_set_style_shadow_width(clock_card, 0, 0);
	lv_obj_set_style_pad_all(clock_card, 20, 0);
	lv_obj_set_style_pad_gap(clock_card, 18, 0);
	lv_obj_set_scrollable(clock_card, false);
	lv_obj_set_event_bubble(clock_card, true);
	lv_obj_set_flex_flow(clock_card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(clock_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

	lv_obj_t *clock_name = lv_label_create(clock_card);
	lv_label_set_text(clock_name, tr("appearance_clock_position"));
	lv_obj_add_style(clock_name, &theme_style_text, 0);
	lv_obj_set_style_text_font(clock_name, &font_ui_24, 0);

	lv_obj_t *clock_row = lv_obj_create(clock_card);
	lv_obj_set_size(clock_row, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(clock_row, 0, 0);
	lv_obj_set_style_border_width(clock_row, 0, 0);
	lv_obj_set_style_pad_all(clock_row, 0, 0);
	lv_obj_set_style_pad_gap(clock_row, 10, 0);
	lv_obj_set_scrollable(clock_row, false);
	// Wrapped, not a single line: four pills of translated words do not fit
	// across 480 pixels in every language, and one that does not fit is drawn
	// off the card rather than shrunk.
	lv_obj_set_flex_flow(clock_row, LV_FLEX_FLOW_ROW_WRAP);
	lv_obj_set_flex_align(clock_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

	btn_clock[TOPBAR_CLOCK_LEFT] = make_clock_choice(clock_row, "left", TOPBAR_CLOCK_LEFT);
	btn_clock[TOPBAR_CLOCK_CENTER] = make_clock_choice(clock_row, "centre", TOPBAR_CLOCK_CENTER);
	btn_clock[TOPBAR_CLOCK_RIGHT] = make_clock_choice(clock_row, "right", TOPBAR_CLOCK_RIGHT);
	btn_clock[TOPBAR_CLOCK_HIDDEN] = make_clock_choice(clock_row, "hide", TOPBAR_CLOCK_HIDDEN);

	// A third card: the accent colour, as a row of coloured circles.
	lv_obj_t *accent_card = lv_obj_create(container);
	lv_obj_set_width(accent_card, lv_pct(100));
	lv_obj_set_height(accent_card, LV_SIZE_CONTENT);
	lv_obj_add_style(accent_card, &theme_style_card, 0);
	lv_obj_set_style_radius(accent_card, 12, 0);
	lv_obj_set_style_border_width(accent_card, 0, 0);
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
	lv_label_set_text_fmt(tint_note, "%s %s", tr("appearance_dynamic_tint_note"),
						  appearance_czech() ? "Nejde spolu s Compas skinem: zapnutí jedno vypne druhé."
										  : "Not together with a Compas skin: turning one on turns the other off.");

	// The picture shown while the player boots, which follows the theme and
	// the accent unless one of the user's own is chosen.
	settingsrow_add(container, "bootlogo", NULL, switch_screen_cb, bootlogopage_screen);

	// How big the small text on every page is: the secondary lines, notes,
	// clocks and counters. See fonts.c for which sizes grow and by how much.
	settingsrow_add(container, appearance_czech() ? "P\xC3\xADsmo" : "Font", &font_value, font_next_cb, NULL);
	refresh_font_row();

	settingsrow_add(container, appearance_czech() ? "Ikony" : "Icons", &iconpack_value, iconpack_next_cb, NULL);
	refresh_iconpack_row();

	lv_obj_t *text_size_pills;
	settingsrow_pills(container, "appearance_text_size", &text_size_pills);
	text_size_pill[FONTS_TEXT_NORMAL] =
		settingsrow_pill(text_size_pills, "appearance_text_normal", FONTS_TEXT_NORMAL, text_size_cb);
	text_size_pill[FONTS_TEXT_LARGE] =
		settingsrow_pill(text_size_pills, "appearance_text_large", FONTS_TEXT_LARGE, text_size_cb);

	// A fifth card, and a plain switch: whether the status
	// bar prints the charge as a number as well as drawing it.
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
	lv_obj_t *statusbar_note = lv_label_create(container);
	lv_label_set_long_mode(statusbar_note, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(statusbar_note, lv_pct(100));
	lv_obj_add_style(statusbar_note, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(statusbar_note, &font_ui_22, 0);
	lv_label_set_text(statusbar_note, tr("appearance_statusbar_always_note"));

	// Rounds off the four corners of the screen, over every page and overlay.
	settingsrow_toggle(container, "appearance_rounded_corners", &rounded_corners_toggle, rounded_corners_cb);
	if (corners_enabled()) {
		lv_obj_add_state(rounded_corners_toggle, LV_STATE_CHECKED);
	}

	refresh_buttons();
	refresh_clock_buttons();
	refresh_accent_buttons();
	refresh_text_size_pills();
	theme_register_refresh(refresh_buttons);
	theme_register_refresh(refresh_text_size_pills);
	theme_register_refresh(refresh_clock_buttons);
	theme_register_refresh(refresh_accent_buttons);
}
