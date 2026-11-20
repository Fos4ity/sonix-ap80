#include "playerstylepage.h"

#include <stdint.h>

#include "src/gui/nowplaying/player.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/theme.h"

lv_obj_t *playerstylepage_screen;

static lv_obj_t *layout_pill[3];
static lv_obj_t *backdrop_pill[3];
static lv_obj_t *radius_pill[4];
static lv_obj_t *border_pill[3];
static lv_obj_t *cover_accent_toggle;
static lv_obj_t *format_toggle;
static lv_obj_t *queue_pos_toggle;

static const int RADIUS_VALUES[4] = {0, 8, 14, 24};

static void refresh_pills(void) {
	player_layout_t layout = player_layout_get();
	settingsrow_pill_active(layout_pill[0], layout == PLAYER_LAYOUT_STANDARD);
	settingsrow_pill_active(layout_pill[1], layout == PLAYER_LAYOUT_COMPACT);
	settingsrow_pill_active(layout_pill[2], layout == PLAYER_LAYOUT_CAROUSEL);

	player_backdrop_mode_t bm = player_backdrop_mode_get();
	for (int i = 0; i < 3; i++) {
		settingsrow_pill_active(backdrop_pill[i], (int)bm == i);
	}

	int rad = player_art_radius_get();
	for (int i = 0; i < 4; i++) {
		settingsrow_pill_active(radius_pill[i], rad == RADIUS_VALUES[i]);
	}

	player_art_border_t ab = player_art_border_get();
	for (int i = 0; i < 3; i++) {
		settingsrow_pill_active(border_pill[i], (int)ab == i);
	}
}

static void layout_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) return;
	player_layout_set((player_layout_t)(intptr_t)lv_event_get_user_data(e));
	refresh_pills();
}

static void backdrop_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) return;
	player_backdrop_mode_set((player_backdrop_mode_t)(intptr_t)lv_event_get_user_data(e));
	refresh_pills();
}

static void radius_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) return;
	int idx = (int)(intptr_t)lv_event_get_user_data(e);
	if (idx >= 0 && idx < 4) {
		player_art_radius_set(RADIUS_VALUES[idx]);
	}
	refresh_pills();
}

static void border_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) return;
	player_art_border_set((player_art_border_t)(intptr_t)lv_event_get_user_data(e));
	refresh_pills();
}

static void cover_accent_cb(lv_event_t *e) {
	(void)e;
	player_cover_accent_set(lv_obj_has_state(cover_accent_toggle, LV_STATE_CHECKED));
}

static void format_cb(lv_event_t *e) {
	(void)e;
	player_show_format_set(lv_obj_has_state(format_toggle, LV_STATE_CHECKED));
}

static void queue_pos_cb(lv_event_t *e) {
	(void)e;
	player_show_queue_pos_set(lv_obj_has_state(queue_pos_toggle, LV_STATE_CHECKED));
}

void playerstylepage_init(gui_config_t *cfg) {
	lv_obj_t *container = settingsrow_page(playerstylepage_screen, cfg, "Player style");

	lv_obj_t *layout_pills;
	settingsrow_pills(container, "musicsettings_player_layout", &layout_pills);
	layout_pill[0] = settingsrow_pill(layout_pills, "musicsettings_layout_standard", PLAYER_LAYOUT_STANDARD, layout_cb);
	layout_pill[1] = settingsrow_pill(layout_pills, "Square", PLAYER_LAYOUT_COMPACT, layout_cb);
	layout_pill[2] = settingsrow_pill(layout_pills, "Carousel", PLAYER_LAYOUT_CAROUSEL, layout_cb);

	lv_obj_t *backdrop_pills;
	settingsrow_pills(container, "Player backdrop", &backdrop_pills);
	backdrop_pill[0] = settingsrow_pill(backdrop_pills, "Blurred art", PLAYER_BACKDROP_BLUR, backdrop_cb);
	backdrop_pill[1] = settingsrow_pill(backdrop_pills, "Theme solid", PLAYER_BACKDROP_THEME, backdrop_cb);
	backdrop_pill[2] = settingsrow_pill(backdrop_pills, "Pitch black", PLAYER_BACKDROP_BLACK, backdrop_cb);

	lv_obj_t *radius_pills;
	settingsrow_pills(container, "Artwork corners", &radius_pills);
	radius_pill[0] = settingsrow_pill(radius_pills, "Sharp", 0, radius_cb);
	radius_pill[1] = settingsrow_pill(radius_pills, "8px", 1, radius_cb);
	radius_pill[2] = settingsrow_pill(radius_pills, "14px", 2, radius_cb);
	radius_pill[3] = settingsrow_pill(radius_pills, "24px", 3, radius_cb);

	lv_obj_t *border_pills;
	settingsrow_pills(container, "Artwork border", &border_pills);
	border_pill[0] = settingsrow_pill(border_pills, "Off", PLAYER_ART_BORDER_NONE, border_cb);
	border_pill[1] = settingsrow_pill(border_pills, "Theme", PLAYER_ART_BORDER_THEME, border_cb);
	border_pill[2] = settingsrow_pill(border_pills, "Accent", PLAYER_ART_BORDER_ACCENT, border_cb);

	settingsrow_toggle(container, "musicsettings_cover_accent", &cover_accent_toggle, cover_accent_cb);
	if (player_cover_accent()) {
		lv_obj_add_state(cover_accent_toggle, LV_STATE_CHECKED);
	}

	settingsrow_toggle(container, "Show format badge", &format_toggle, format_cb);
	if (player_show_format_get()) {
		lv_obj_add_state(format_toggle, LV_STATE_CHECKED);
	}

	settingsrow_toggle(container, "Show queue position", &queue_pos_toggle, queue_pos_cb);
	if (player_show_queue_pos_get()) {
		lv_obj_add_state(queue_pos_toggle, LV_STATE_CHECKED);
	}

	refresh_pills();
	theme_register_refresh(refresh_pills);
}
