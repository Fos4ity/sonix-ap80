#include "menustylepage.h"

#include <stdint.h>

#include "src/gui/nowplaying/player.h"
#include <string.h>

#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/corners.h"
#include "src/gui/shell/monotile.h"
#include "src/system/core/lang.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/theme.h"
#include "src/gui/shell/topbar.h"
#include "src/system/core/config.h"

lv_obj_t *menustylepage_screen;

static lv_obj_t *surface_pill[3];
static lv_obj_t *radius_pill[4];
static lv_obj_t *border_pill[3];
static lv_obj_t *clock_pill[4];
static lv_obj_t *mono_icons_toggle;
static lv_obj_t *mono_icons_note;
static lv_obj_t *topbar_transp_toggle;
static lv_obj_t *battery_percent_toggle;
static lv_obj_t *statusbar_always_toggle;
static lv_obj_t *rounded_corners_toggle;

static const int CARD_RADII[4] = {0, 6, 12, 20};

static void refresh_pills(void) {
	theme_card_surface_t cs = theme_get_card_surface();
	for (int i = 0; i < 3; i++) {
		settingsrow_pill_active(surface_pill[i], (int)cs == i);
	}

	int rad = theme_get_card_radius();
	for (int i = 0; i < 4; i++) {
		settingsrow_pill_active(radius_pill[i], rad == CARD_RADII[i]);
	}

	theme_border_style_t bs = theme_get_border_style();
	for (int i = 0; i < 3; i++) {
		settingsrow_pill_active(border_pill[i], (int)bs == i);
	}

	int clk = (int)config_get_int("screen", "clock_pos", TOPBAR_CLOCK_CENTER);
	for (int i = 0; i < 4; i++) {
		settingsrow_pill_active(clock_pill[i], clk == i);
	}
}

static void surface_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) return;
	theme_set_card_surface((theme_card_surface_t)(intptr_t)lv_event_get_user_data(e));
	refresh_pills();
}

static void radius_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) return;
	int idx = (int)(intptr_t)lv_event_get_user_data(e);
	if (idx >= 0 && idx < 4) {
		theme_set_card_radius(CARD_RADII[idx]);
	}
	refresh_pills();
}

static void border_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) return;
	theme_set_border_style((theme_border_style_t)(intptr_t)lv_event_get_user_data(e));
	refresh_pills();
}

static void clock_pos_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) return;
	int pos = (int)(intptr_t)lv_event_get_user_data(e);
	config_set_int("screen", "clock_pos", pos);
	config_save();
	topbar_set_clock_position(pos);
	refresh_pills();
}

static void mono_icons_cb(lv_event_t *e) {
	(void)e;
	theme_set_mono_grid_icons(lv_obj_has_state(mono_icons_toggle, LV_STATE_CHECKED));
}

// The switch only where it can do something: the built-in tiles are shaded
// artwork that cannot be drawn in one colour (see monotile.h), so with them it
// is greyed out and a line says why. Looked at every time the page is opened,
// because the icon pack is chosen on another page.
static void mono_icons_refresh(lv_event_t *e) {
	(void)e;
	bool can = monotile_available();
	lv_obj_set_state(mono_icons_toggle, LV_STATE_DISABLED, !can);
	lv_obj_set_style_opa(mono_icons_toggle, can ? LV_OPA_COVER : LV_OPA_40, 0);
	lv_obj_set_hidden(mono_icons_note, can);
}

static void topbar_transp_cb(lv_event_t *e) {
	(void)e;
	topbar_set_transparent(lv_obj_has_state(topbar_transp_toggle, LV_STATE_CHECKED));
}

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

void menustylepage_init(gui_config_t *cfg) {
	lv_obj_t *container = settingsrow_page(menustylepage_screen, cfg, "Menu & bar style");

	lv_obj_t *surface_pills;
	settingsrow_pills(container, "Card surface", &surface_pills);
	surface_pill[0] = settingsrow_pill(surface_pills, "Translucent", THEME_CARD_TRANSLUCENT, surface_cb);
	surface_pill[1] = settingsrow_pill(surface_pills, "Solid", THEME_CARD_SOLID, surface_cb);
	surface_pill[2] = settingsrow_pill(surface_pills, "Glass", THEME_CARD_GLASS, surface_cb);

	lv_obj_t *radius_pills;
	settingsrow_pills(container, "Card corners", &radius_pills);
	radius_pill[0] = settingsrow_pill(radius_pills, "Sharp", 0, radius_cb);
	radius_pill[1] = settingsrow_pill(radius_pills, "6px", 1, radius_cb);
	radius_pill[2] = settingsrow_pill(radius_pills, "12px", 2, radius_cb);
	radius_pill[3] = settingsrow_pill(radius_pills, "20px", 3, radius_cb);

	lv_obj_t *border_pills;
	settingsrow_pills(container, "Card border", &border_pills);
	border_pill[0] = settingsrow_pill(border_pills, "Subtle", THEME_BORDER_SUBTLE, border_cb);
	border_pill[1] = settingsrow_pill(border_pills, "Accent", THEME_BORDER_ACCENT, border_cb);
	border_pill[2] = settingsrow_pill(border_pills, "None", THEME_BORDER_NONE, border_cb);

	settingsrow_toggle(container, "Monochrome menu icons", &mono_icons_toggle, mono_icons_cb);
	if (theme_get_mono_grid_icons()) {
		lv_obj_add_state(mono_icons_toggle, LV_STATE_CHECKED);
	}
	const char *language = lang_current();
	bool czech = language && strncmp(language, "\xC4\x8C" "e", 3) == 0; // "Čeština"
	mono_icons_note = lv_label_create(container);
	lv_label_set_text(mono_icons_note,
					  czech ? "Vestav\xC4\x9Bn\xC3\xA9 ikony jsou barevn\xC3\xA9 obr\xC3\xA1zky a jednobarevn\xC4\x9B "
							  "nakreslit nejdou. Vyber jinou sadu ikon ve Vzhledu."
							: "The built-in icons are shaded pictures and cannot be drawn in one colour. "
							  "Choose another icon pack in Appearance.");
	lv_label_set_long_mode(mono_icons_note, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(mono_icons_note, lv_pct(100));
	lv_obj_add_style(mono_icons_note, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(mono_icons_note, &font_ui_20, 0);
	lv_obj_set_style_pad_hor(mono_icons_note, 8, 0);
	mono_icons_refresh(NULL);
	lv_obj_add_event_cb(menustylepage_screen, mono_icons_refresh, LV_EVENT_SCREEN_LOADED, NULL);

	settingsrow_toggle(container, "Transparent status bar", &topbar_transp_toggle, topbar_transp_cb);
	if (topbar_get_transparent()) {
		lv_obj_add_state(topbar_transp_toggle, LV_STATE_CHECKED);
	}

	lv_obj_t *clock_pills;
	settingsrow_pills(container, "appearance_clock_position", &clock_pills);
	clock_pill[TOPBAR_CLOCK_LEFT] = settingsrow_pill(clock_pills, "left", TOPBAR_CLOCK_LEFT, clock_pos_cb);
	clock_pill[TOPBAR_CLOCK_CENTER] = settingsrow_pill(clock_pills, "centre", TOPBAR_CLOCK_CENTER, clock_pos_cb);
	clock_pill[TOPBAR_CLOCK_RIGHT] = settingsrow_pill(clock_pills, "right", TOPBAR_CLOCK_RIGHT, clock_pos_cb);
	clock_pill[TOPBAR_CLOCK_HIDDEN] = settingsrow_pill(clock_pills, "hide", TOPBAR_CLOCK_HIDDEN, clock_pos_cb);

	settingsrow_toggle(container, "appearance_battery_percentage", &battery_percent_toggle, battery_percent_cb);
	if (config_get_int("screen", "battery_percent", 1) != 0) {
		lv_obj_add_state(battery_percent_toggle, LV_STATE_CHECKED);
	}

	settingsrow_toggle(container, "appearance_statusbar_always", &statusbar_always_toggle, statusbar_always_cb);
	if (player_statusbar_always()) {
		lv_obj_add_state(statusbar_always_toggle, LV_STATE_CHECKED);
	}

	settingsrow_toggle(container, "appearance_rounded_corners", &rounded_corners_toggle, rounded_corners_cb);
	if (corners_enabled()) {
		lv_obj_add_state(rounded_corners_toggle, LV_STATE_CHECKED);
	}

	refresh_pills();
	theme_register_refresh(refresh_pills);
}
