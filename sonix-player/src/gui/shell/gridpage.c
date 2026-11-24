#include "gridpage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl/lvgl.h"

#include "src/gui/nowplaying/coverflow.h"
#include "src/gui/fonts/fonts.h"
#include "src/gui/nowplaying/player.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/monotile.h"
#include "src/gui/shell/theme.h"
#include "src/system/core/config.h"
#include "src/system/core/lang.h"
#include "src/system/device/usb.h"

#define GRID_GAP 14
#define MAX_GRIDS 16

static lv_obj_t *tracked_grids[MAX_GRIDS];
static int tracked_grid_count;

// A tile's picture in its own colours or, with Monochrome menu icons on, in
// the text colour -- as far as that picture can be (see monotile.h): a glyph is
// tinted, a drawing on a plate is drawn without the plate, and shaded artwork
// keeps its colours rather than turning into a blob.
//
// The picture the tile was given is kept on the image as its user data, since
// what the image shows may be the plate-less copy.
static void apply_tile_icon_mode(lv_obj_t *icon) {
	if (!icon) {
		return;
	}
	const lv_image_dsc_t *own = lv_obj_get_user_data(icon);
	if (!own) {
		own = lv_image_get_src(icon);
	}
	const void *show = own;
	bool tint = false;
	if (theme_get_mono_grid_icons()) {
		switch (monotile_kind(own)) {
		case MONOTILE_GLYPH:
			tint = true;
			break;
		case MONOTILE_PLATE:
			show = monotile_mask(own);
			tint = show != NULL;
			if (!show) {
				show = own;
			}
			break;
		default:
			break;
		}
	}
	if (lv_image_get_src(icon) != show) {
		lv_image_set_src(icon, show);
	}
	if (tint) {
		lv_obj_set_style_image_recolor(icon, theme()->text_primary, 0);
		lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
	} else {
		lv_obj_set_style_image_recolor_opa(icon, LV_OPA_TRANSP, 0);
	}
}

static void refresh_grid_icons(void);

// The tiles' pixels have changed under them (another icon pack): every tile
// goes back to its own picture before the plate-less copies are thrown away,
// and is then drawn again from the new pixels.
void gridpage_icons_changed(void) {
	for (int g = 0; g < tracked_grid_count; g++) {
		lv_obj_t *grid = tracked_grids[g];
		uint32_t cnt = grid ? lv_obj_get_child_count(grid) : 0;
		for (uint32_t i = 0; i < cnt; i++) {
			lv_obj_t *tile = lv_obj_get_child(grid, (int32_t)i);
			lv_obj_t *icon = tile && lv_obj_get_child_count(tile) > 0 ? lv_obj_get_child(tile, 0) : NULL;
			const lv_image_dsc_t *own = icon ? lv_obj_get_user_data(icon) : NULL;
			if (own && lv_image_get_src(icon) != own) {
				lv_image_set_src(icon, own);
			}
		}
	}
	monotile_reset();
	refresh_grid_icons();
}

static void refresh_grid_icons(void) {
	for (int g = 0; g < tracked_grid_count; g++) {
		lv_obj_t *grid = tracked_grids[g];
		if (!grid) {
			continue;
		}
		uint32_t cnt = lv_obj_get_child_count(grid);
		for (uint32_t i = 0; i < cnt; i++) {
			lv_obj_t *tile = lv_obj_get_child(grid, (int32_t)i);
			if (tile && lv_obj_get_child_count(tile) > 0) {
				apply_tile_icon_mode(lv_obj_get_child(tile, 0));
			}
		}
	}
}

// The two children add_tile() puts on every tile, in the order it makes them.
#define TILE_ICON_CHILD 0
#define TILE_LABEL_CHILD 1

// Set from the moment a tile is picked up until the release that puts it down
// has unwound: that release is followed by a CLICKED like any other.
static bool carry_click_guard;

static bool not_a_tap(void) {
	return carry_click_guard || player_sheet_drag_active() || switcher_back_drag_active() || coverflow_drag_active();
}

static void unavailable_cb(lv_event_t *e) {
	if (not_a_tap()) {
		return; // this was a swipe across the tile, not a tap on it
	}

	const char *label = lv_event_get_user_data(e);
	char message[160];

	// tr() on the way in as well: what is stored on the tile is the tag, and
	// the message has to name the tile the way the tile does.
	lv_snprintf(message, sizeof(message), tr("grid_not_yet_available"), tr(label));
	gui_notify_popup(message);
}

static void action_cb(lv_event_t *e) {
	if (not_a_tap()) {
		return; // a swipe, not a tap
	}

	void (*action)(void) = (void (*)(void))lv_event_get_user_data(e);
	action();
}

static void target_cb(lv_event_t *e) {
	if (not_a_tap()) {
		return;
	}
	switch_screen((lv_obj_t *)lv_event_get_user_data(e));
}

// ---------------------------------------------------------------------------
// Arranging the tiles by hand
//
// A tile held for a moment is picked up: a copy of it follows the finger on
// the top layer, and the tile itself stays in the grid, faded, as the place
// the copy would land. That place moves through the grid as the copy is
// carried over the other tiles, so they make room as they would on a phone's
// home screen, and letting go leaves things exactly as they look.
//
// From the pick-up to the release the press belongs to the carry alone: the
// grid is told the press was lost, so the player sheet, the swipe back and the
// album carousel let go of it, and the tile stops passing its events up.
// ---------------------------------------------------------------------------

typedef struct {
	lv_obj_t *grid;
	const char *key;
	grid_moved_cb_t moved;
} arrange_t;

static arrange_t arranges[MAX_GRIDS];
static int arrange_count;

static lv_obj_t *carry_tile;
static lv_obj_t *carry_ghost;
static lv_point_t carry_offset;
static bool carry_grid_scrolled;
static grid_moved_cb_t carry_moved;
static const char *carry_moved_label;
static int carry_moved_position;

// How close to the top or bottom of a grid that scrolls the finger has to be
// before the grid moves under it, and how far each time the event comes round.
#define CARRY_EDGE_PX 60
#define CARRY_EDGE_STEP 14

// How long the finger has to rest on a tile before it is picked up, and how
// far it may wander in that time and still count as resting. Half a second is
// what a phone's home screen asks for.
#define CARRY_HOLD_MS 500
#define CARRY_HOLD_SLOP 12
// How far the copy rises when it is picked up.
#define CARRY_LIFT_PX 6

static lv_obj_t *hold_tile;
static lv_point_t hold_from;
static uint32_t hold_since;

bool gridpage_carry_active(void) { return carry_tile != NULL; }

static arrange_t *arrange_for(lv_obj_t *grid) {
	for (int i = 0; i < arrange_count; i++) {
		if (arranges[i].grid == grid) {
			return &arranges[i];
		}
	}
	return NULL;
}

// The tag add_tile() leaves on every tile.
static const char *tile_tag(lv_obj_t *tile) {
	const char *tag = lv_obj_get_user_data(tile);
	return tag ? tag : "";
}

static void order_save(lv_obj_t *grid, const char *key) {
	char text[256] = "";
	size_t used = 0;
	uint32_t count = lv_obj_get_child_count(grid);
	for (uint32_t i = 0; i < count; i++) {
		int wrote = snprintf(text + used, sizeof(text) - used, "%s%s", i ? "," : "",
							 tile_tag(lv_obj_get_child(grid, (int32_t)i)));
		if (wrote < 0 || (size_t)wrote >= sizeof(text) - used) {
			return; // never half a list
		}
		used += (size_t)wrote;
	}
	config_set("tiles", key, text);
	config_save();
}

// Puts the tiles in the saved order. A name the grid does not have is passed
// over, and a tile the list does not name keeps its place after the named ones.
static void order_apply(lv_obj_t *grid, const char *key) {
	const char *at = config_get("tiles", key, "");
	int place = 0;
	while (*at) {
		const char *end = strchr(at, ',');
		size_t len = end ? (size_t)(end - at) : strlen(at);
		uint32_t count = lv_obj_get_child_count(grid);
		for (uint32_t i = (uint32_t)place; i < count; i++) {
			lv_obj_t *tile = lv_obj_get_child(grid, (int32_t)i);
			const char *tag = tile_tag(tile);
			if (strlen(tag) == len && strncmp(tag, at, len) == 0) {
				lv_obj_move_to_index(tile, place++);
				break;
			}
		}
		if (!end) {
			break;
		}
		at = end + 1;
	}
}

static void carry_guard_clear_cb(void *unused) {
	(void)unused;
	carry_click_guard = false;
}

static void carry_moved_cb(void *unused) {
	(void)unused;
	if (carry_moved) {
		carry_moved(carry_moved_label, carry_moved_position);
	}
	carry_moved = NULL;
}

// The copy that follows the finger: the tile's picture and caption on a card
// of its size, with the accent round it.
static lv_obj_t *make_ghost(lv_obj_t *tile) {
	lv_obj_t *src_icon = lv_obj_get_child(tile, TILE_ICON_CHILD);
	lv_obj_t *src_label = lv_obj_get_child(tile, TILE_LABEL_CHILD);

	lv_obj_t *ghost = lv_obj_create(lv_layer_top());
	lv_obj_remove_style_all(ghost);
	lv_obj_add_style(ghost, &theme_style_card, 0);
	lv_obj_set_size(ghost, lv_obj_get_width(tile), lv_obj_get_height(tile));
	lv_obj_set_style_bg_opa(ghost, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(ghost, 3, 0);
	lv_obj_set_style_border_color(ghost, theme()->accent, 0);
	lv_obj_set_style_border_opa(ghost, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(ghost, 8, 0);
	lv_obj_set_style_pad_gap(ghost, 8, 0);
	lv_obj_set_flex_flow(ghost, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(ghost, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_scrollable(ghost, false);
	lv_obj_set_clickable(ghost, false);

	lv_obj_t *icon = lv_image_create(ghost);
	lv_obj_set_user_data(icon, lv_obj_get_user_data(src_icon));
	lv_image_set_src(icon, lv_image_get_src(src_icon));
	lv_obj_set_size(icon, lv_obj_get_width(src_icon), lv_obj_get_height(src_icon));
	lv_image_set_inner_align(icon, LV_IMAGE_ALIGN_CONTAIN);
	apply_tile_icon_mode(icon);

	lv_obj_t *label = lv_label_create(ghost);
	lv_label_set_text(label, lv_label_get_text(src_label));
	lv_obj_add_style(label, &theme_style_text, 0);
	lv_obj_set_style_text_font(label, lv_obj_get_style_text_font(src_label, 0), 0);
	lv_obj_set_width(label, lv_pct(100));
	lv_label_set_long_mode(label, lv_label_get_long_mode(src_label));
	lv_obj_set_height(label, lv_obj_get_height(src_label));
	lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

	return ghost;
}

static void carry_end(bool dropped) {
	lv_obj_t *tile = carry_tile;
	if (!tile) {
		return;
	}
	carry_tile = NULL;
	lv_obj_t *grid = lv_obj_get_parent(tile);

	if (carry_ghost) {
		lv_obj_delete(carry_ghost);
		carry_ghost = NULL;
	}
	lv_obj_remove_local_style_prop(tile, LV_STYLE_OPA, 0);
	lv_obj_set_event_bubble(tile, true);
	if (carry_grid_scrolled) {
		lv_obj_set_scrollable(grid, true);
	}
	// The CLICKED of this same press comes after the release.
	lv_async_call(carry_guard_clear_cb, NULL);

	// Kept even when the press was lost rather than released: the tiles
	// already stand in the new order, and the saved one has to say the same.
	(void)dropped;
	arrange_t *arrange = arrange_for(grid);
	if (!arrange) {
		return;
	}
	if (arrange->key) {
		order_save(grid, arrange->key);
	}
	if (arrange->moved) {
		carry_moved = arrange->moved;
		carry_moved_label = tile_tag(tile);
		carry_moved_position = (int)lv_obj_get_index(tile);
		lv_async_call(carry_moved_cb, NULL);
	}
}

// The grid follows the finger when a carried tile reaches either end of it.
static void carry_edge_scroll(lv_obj_t *grid, lv_point_t point) {
	lv_area_t view;
	lv_obj_get_coords(grid, &view);
	int step = 0;
	if (point.y < view.y1 + CARRY_EDGE_PX) {
		step = -CARRY_EDGE_STEP;
	} else if (point.y > view.y2 - CARRY_EDGE_PX) {
		step = CARRY_EDGE_STEP;
	}
	if (step == 0) {
		return;
	}
	int scroll = lv_obj_get_scroll_y(grid);
	int limit = scroll + lv_obj_get_scroll_bottom(grid);
	int wanted = scroll + step;
	wanted = wanted < 0 ? 0 : (wanted > limit ? limit : wanted);
	if (wanted != scroll) {
		lv_obj_scroll_to_y(grid, wanted, LV_ANIM_OFF);
	}
}

static void carry_cb(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *tile = lv_event_get_current_target(e);
	lv_obj_t *grid = lv_obj_get_parent(tile);

	lv_indev_t *indev = lv_indev_active();
	lv_point_t point = {0, 0};
	if (indev) {
		lv_indev_get_point(indev, &point);
	}

	if (code == LV_EVENT_PRESSED) {
		hold_tile = tile;
		hold_from = point;
		hold_since = lv_tick_get();
		return;
	}

	// A press becomes a carry only when the finger has rested on the tile:
	// CARRY_HOLD_MS without leaving the spot it came down on. Any travel
	// before that makes it an ordinary press for good -- a swipe, a scroll or
	// a slow drag across the page never picks a tile up.
	if (code == LV_EVENT_PRESSING && !carry_tile && hold_tile == tile) {
		if (LV_ABS(point.x - hold_from.x) > CARRY_HOLD_SLOP || LV_ABS(point.y - hold_from.y) > CARRY_HOLD_SLOP) {
			hold_tile = NULL;
			return;
		}
		if (lv_tick_elaps(hold_since) < CARRY_HOLD_MS) {
			return;
		}
		hold_tile = NULL;
		if (!indev || lv_indev_get_scroll_obj(indev) || lv_obj_get_child_count(grid) < 2) {
			return;
		}
		if (player_sheet_drag_active() || switcher_back_drag_active() || coverflow_drag_active()) {
			return; // the press is already somebody's swipe
		}

		// Everything on the grid that was following this press lets go of it.
		lv_obj_send_event(grid, LV_EVENT_PRESS_LOST, indev);
		lv_obj_set_event_bubble(tile, false);
		carry_grid_scrolled = lv_obj_is_scrollable(grid);
		if (carry_grid_scrolled) {
			lv_obj_set_scrollable(grid, false);
		}

		lv_area_t area;
		lv_obj_get_coords(tile, &area);
		carry_offset.x = point.x - area.x1;
		carry_offset.y = point.y - area.y1;

		carry_tile = tile;
		carry_click_guard = true;
		carry_ghost = make_ghost(tile);
		// Lifted a little, so picking it up can be seen before it is moved.
		lv_obj_set_pos(carry_ghost, area.x1, area.y1 - CARRY_LIFT_PX);
		carry_offset.y += CARRY_LIFT_PX;
		lv_obj_set_style_opa(tile, LV_OPA_30, 0);
		return;
	}

	if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
		hold_tile = NULL;
	}

	if (carry_tile != tile) {
		return;
	}

	if (code == LV_EVENT_PRESSING) {
		int x = point.x - carry_offset.x;
		int y = point.y - carry_offset.y;
		lv_obj_set_pos(carry_ghost, x, y);

		if (carry_grid_scrolled) {
			carry_edge_scroll(grid, point);
		}

		// The place under the middle of the copy becomes the tile's place.
		lv_point_t middle = {x + lv_obj_get_width(tile) / 2, y + lv_obj_get_height(tile) / 2};
		uint32_t count = lv_obj_get_child_count(grid);
		for (uint32_t i = 0; i < count; i++) {
			lv_obj_t *other = lv_obj_get_child(grid, (int32_t)i);
			if (other == tile) {
				continue;
			}
			lv_area_t area;
			lv_obj_get_coords(other, &area);
			if (middle.x >= area.x1 && middle.x <= area.x2 && middle.y >= area.y1 && middle.y <= area.y2) {
				lv_obj_move_to_index(tile, (int32_t)i);
				lv_obj_update_layout(grid);
				break;
			}
		}
		return;
	}

	if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
		carry_end(code == LV_EVENT_RELEASED);
	}
}

static void attach_carry(lv_obj_t *tile) {
	lv_obj_add_event_cb(tile, carry_cb, LV_EVENT_PRESSED, NULL);
	lv_obj_add_event_cb(tile, carry_cb, LV_EVENT_PRESSING, NULL);
	lv_obj_add_event_cb(tile, carry_cb, LV_EVENT_RELEASED, NULL);
	lv_obj_add_event_cb(tile, carry_cb, LV_EVENT_PRESS_LOST, NULL);
}

void gridpage_set_arrangeable(lv_obj_t *grid, const char *key, grid_moved_cb_t moved) {
	if (!grid || arrange_for(grid) || arrange_count >= MAX_GRIDS) {
		return;
	}
	arranges[arrange_count].grid = grid;
	arranges[arrange_count].key = key;
	arranges[arrange_count].moved = moved;
	arrange_count++;

	uint32_t count = lv_obj_get_child_count(grid);
	for (uint32_t i = 0; i < count; i++) {
		attach_carry(lv_obj_get_child(grid, (int32_t)i));
	}
	if (key) {
		order_apply(grid, key);
	}
}

void gridpage_set_tile(lv_obj_t *grid, int index, const lv_image_dsc_t *icon, const char *label) {
	if (!grid || index < 0 || index >= (int)lv_obj_get_child_count(grid)) {
		return;
	}
	lv_obj_t *tile = lv_obj_get_child(grid, index);
	if (!tile) {
		return;
	}
	if (icon) {
		lv_obj_t *image = lv_obj_get_child(tile, TILE_ICON_CHILD);
		lv_obj_set_user_data(image, (void *)icon);
		lv_image_set_src(image, icon);
		apply_tile_icon_mode(image);
	}
	if (label) {
		lv_label_set_text(lv_obj_get_child(tile, TILE_LABEL_CHILD), tr(label));
	}
}

static void add_tile(lv_obj_t *grid, const grid_entry_t *entry, int width, int height) {
	lv_obj_t *tile = lv_btn_create(grid);
	// The tile's name, for a grid whose order is kept (see order_save()).
	lv_obj_set_user_data(tile, (void *)entry->label);
	lv_obj_set_size(tile, width, height);
	lv_obj_add_style(tile, &theme_style_card, 0);
	lv_obj_add_style(tile, &theme_style_card_pressed, LV_STATE_PRESSED);
	lv_obj_set_style_shadow_width(tile, 0, 0);
	lv_obj_set_style_pad_all(tile, 8, 0);

	// Presses bubble up to the grid so a swipe can start on a tile: the whole
	// page has to be draggable, not just the gaps between the tiles.
	lv_obj_set_event_bubble(tile, true);

	lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_gap(tile, 8, 0);
	lv_obj_set_scrollable(tile, false);

	// The tile artwork keeps its own colours unless Monochrome Grid Icons is on.
	lv_obj_t *icon = lv_image_create(tile);
	lv_obj_set_user_data(icon, (void *)entry->icon);
	lv_image_set_src(icon, entry->icon);
	// A narrow panel (AP80 Pro Max, 360 px) leaves tiles too small for the
	// artwork plus a wrapped caption: shrink the artwork into what is left
	// under a one-line caption.
	bool narrow = width < 200;
	const lv_font_t *font = narrow ? &font_ui_20_bold : &font_ui_24_bold;
	if (narrow) {
		int line_h = lv_font_get_line_height(font);
		int avail = height - 2 * 8 - 8 - line_h;
		if (avail > width - 2 * 8) {
			avail = width - 2 * 8;
		}
		if (avail < 24) {
			avail = 24;
		}
		lv_obj_set_size(icon, avail, avail);
		lv_image_set_inner_align(icon, LV_IMAGE_ALIGN_CONTAIN);
	}
	apply_tile_icon_mode(icon);

	// The caption wraps rather than running out of the tile: a translated label
	// such as "Album-Interpreten" does not fit on one line. Centred, because a
	// tile is read as a block under its picture.
	lv_obj_t *label = lv_label_create(tile);
	lv_label_set_text(label, tr(entry->label));
	lv_obj_add_style(label, &theme_style_text, 0);
	lv_obj_set_style_text_font(label, font, 0);
	lv_obj_set_width(label, lv_pct(100));
	if (narrow) {
		lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
		lv_obj_set_height(label, lv_font_get_line_height(font));
	}
	lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

	if (arrange_for(grid)) {
		attach_carry(tile);
	}

	if (entry->target) {
		lv_obj_add_event_cb(tile, target_cb, LV_EVENT_CLICKED, *entry->target);
	} else if (entry->action) {
		lv_obj_add_event_cb(tile, action_cb, LV_EVENT_CLICKED, (void *)entry->action);
	} else {
		lv_obj_add_event_cb(tile, unavailable_cb, LV_EVENT_CLICKED, (void *)entry->label);
		lv_obj_set_style_opa(icon, LV_OPA_40, 0);
		lv_obj_add_style(label, &theme_style_text_dim, 0);
	}
}

void gridpage_set_entries(lv_obj_t *grid, gui_config_t *cfg, const grid_entry_t *entries, int count, int columns,
						  int rows, bool clear_corner_buttons) {
	if (!grid) {
		return;
	}
	if (carry_tile && lv_obj_get_parent(carry_tile) == grid) {
		// The tiles are about to go, the carried one with them.
		if (carry_ghost) {
			lv_obj_delete(carry_ghost);
			carry_ghost = NULL;
		}
		if (carry_grid_scrolled) {
			lv_obj_set_scrollable(grid, true);
		}
		carry_tile = NULL;
		lv_async_call(carry_guard_clear_cb, NULL);
	}
	lv_obj_clean(grid);

	int top = clear_corner_buttons ? settingsrow_content_top(cfg) : cfg->top_bar_height;
	int usable_w = cfg->screen_width - (2 * cfg->padding);
	int usable_h = (cfg->screen_height - top) - (2 * cfg->padding);

	int tile_w = (usable_w - (columns - 1) * GRID_GAP) / columns;
	int tile_h = (usable_h - (rows - 1) * GRID_GAP) / rows;

	for (int i = 0; i < count; i++) {
		add_tile(grid, &entries[i], tile_w, tile_h);
	}
}

lv_obj_t *gridpage_build(lv_obj_t *screen, gui_config_t *cfg, const grid_entry_t *entries, int count, int columns,
						 int rows, bool clear_corner_buttons) {
	lv_obj_add_style(screen, &theme_style_screen, 0);

	int top = clear_corner_buttons ? settingsrow_content_top(cfg) : cfg->top_bar_height;

	lv_obj_t *grid = lv_obj_create(screen);
	lv_obj_set_size(grid, lv_pct(100), cfg->screen_height - top);
	lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 0, top);
	lv_obj_set_style_bg_opa(grid, 0, 0);
	lv_obj_set_style_border_width(grid, 0, 0);
	lv_obj_set_style_radius(grid, 0, 0);
	lv_obj_set_style_pad_all(grid, cfg->padding, 0);
	lv_obj_set_style_pad_gap(grid, GRID_GAP, 0);
	lv_obj_set_scrollable(grid, false);

	lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
	// START on the main axis, not CENTER: the tile size is computed for a full
	// `columns` x `rows` grid, so a page with fewer entries reads as that grid
	// with empty places. Centring would drift an odd tile into the middle of
	// its row, giving that page a shape no other one has.
	lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

	if (tracked_grid_count == 0) {
		theme_register_refresh(refresh_grid_icons);
	}
	if (tracked_grid_count < MAX_GRIDS) {
		tracked_grids[tracked_grid_count++] = grid;
	}

	gridpage_set_entries(grid, cfg, entries, count, columns, rows, clear_corner_buttons);

	// The player can be pulled in from any tiled page.
	player_sheet_attach_drag(grid, true);
	// Presses die here (the tiles bubble only one level up), so the grid must
	// carry the swipe-back drag itself, like the lists do.
	switcher_attach_back_gesture(grid);

	// Handed back for the same reason: a caller that wants a gesture of its own
	// on the page has to put it on this object, because nothing below it
	// bubbles any further.
	return grid;
}

void gridpage_set_scrolling(lv_obj_t *grid) {
	if (!grid) {
		return;
	}
	lv_obj_set_scrollable(grid, true);
	lv_obj_set_scroll_dir(grid, LV_DIR_VER);
	lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ACTIVE);
	// No give past either end, so the page does not bounce under a pull from
	// the bottom edge, which belongs to the album carousel
	// (coverflow_attach_edge).
	lv_obj_set_scroll_elastic(grid, false);
}

// How long the scrollbar stays up when a page that scrolls is shown: long
// enough to be seen, short enough not to sit over the tiles.
#define SCROLL_HINT_MS 1500

static lv_timer_t *scroll_hint_timer;
static lv_obj_t *scroll_hint_grid;

static void scroll_hint_end_cb(lv_timer_t *timer) {
	(void)timer;
	scroll_hint_timer = NULL; // a repeat count of one deletes the timer after this call
	if (scroll_hint_grid) {
		lv_obj_set_scrollbar_mode(scroll_hint_grid, LV_SCROLLBAR_MODE_ACTIVE);
		scroll_hint_grid = NULL;
	}
}

void gridpage_hint_scroll(lv_obj_t *grid) {
	if (!grid || !lv_obj_is_scrollable(grid)) {
		return;
	}
	// The page's real size: on its first showing nothing has laid it out yet.
	lv_obj_update_layout(grid);
	if (lv_obj_get_scroll_top(grid) + lv_obj_get_scroll_bottom(grid) <= 0) {
		return; // everything fits, nothing to hint at
	}
	if (scroll_hint_timer) {
		lv_timer_delete(scroll_hint_timer);
		scroll_hint_timer = NULL;
		if (scroll_hint_grid && scroll_hint_grid != grid) {
			lv_obj_set_scrollbar_mode(scroll_hint_grid, LV_SCROLLBAR_MODE_ACTIVE);
		}
	}
	scroll_hint_grid = grid;
	lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ON);
	scroll_hint_timer = lv_timer_create(scroll_hint_end_cb, SCROLL_HINT_MS, NULL);
	lv_timer_set_repeat_count(scroll_hint_timer, 1);
}

// What the empty panel was built with, so it can go back to saying "scan"
// after it has said "no card", and the check that waits for the card.
typedef struct {
	const lv_image_dsc_t *icon;
	const char *text;
	lv_timer_t *card_timer;
	void (*card_back)(void);
} empty_panel_t;

// How often a panel saying "no card" looks for the card again.
#define CARD_CHECK_MS 1000

lv_obj_t *gridpage_empty_panel(lv_obj_t *screen, gui_config_t *cfg, const lv_image_dsc_t *icon, const char *text,
							   lv_event_cb_t scan_cb) {
	int top = settingsrow_content_top(cfg);

	lv_obj_t *panel = lv_obj_create(screen);
	lv_obj_remove_style_all(panel);
	lv_obj_set_size(panel, lv_pct(100), cfg->screen_height - top);
	lv_obj_align(panel, LV_ALIGN_TOP_LEFT, 0, top);
	lv_obj_set_style_pad_hor(panel, cfg->padding * 2, 0);
	lv_obj_set_style_pad_bottom(panel, cfg->padding * 2, 0);
	lv_obj_set_style_pad_row(panel, 22, 0);
	lv_obj_set_scrollable(panel, false);
	lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_hidden(panel, true);

	lv_obj_t *picture = lv_image_create(panel);
	lv_image_set_src(picture, icon);
	lv_obj_add_style(picture, &theme_style_icon, 0);
	lv_obj_set_style_image_recolor_opa(picture, LV_OPA_COVER, 0);

	lv_obj_t *label = lv_label_create(panel);
	lv_label_set_text(label, tr(text));
	lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(label, lv_pct(100));
	lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_add_style(label, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(label, &font_ui_24, 0);

	lv_obj_t *button = lv_btn_create(panel);
	lv_obj_set_size(button, 240, 68);
	lv_obj_add_style(button, &theme_style_card_pressed, LV_STATE_PRESSED);
	lv_obj_set_style_bg_color(button, theme()->accent, 0);
	lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_shadow_width(button, 0, 0);
	lv_obj_add_event_cb(button, scan_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *button_label = lv_label_create(button);
	lv_label_set_text(button_label, tr("scan"));
	lv_obj_set_style_text_font(button_label, &font_ui_24, 0);
	lv_obj_set_style_text_color(button_label, lv_color_white(), 0);
	lv_obj_center(button_label);

	empty_panel_t *state = calloc(1, sizeof(*state));
	if (state) {
		state->icon = icon;
		state->text = text;
	}
	lv_obj_set_user_data(panel, state);

	return panel;
}

// The panel's picture, line and button, in the order gridpage_empty_panel()
// creates them.
static void empty_panel_say(lv_obj_t *panel, const lv_image_dsc_t *icon, const char *text, bool button) {
	lv_image_set_src(lv_obj_get_child(panel, 0), icon);
	lv_label_set_text(lv_obj_get_child(panel, 1), tr(text));
	lv_obj_set_hidden(lv_obj_get_child(panel, 2), !button);
}

static void stop_card_check(empty_panel_t *state) {
	if (state && state->card_timer) {
		lv_timer_pause(state->card_timer);
	}
}

static void card_check_cb(lv_timer_t *timer) {
	lv_obj_t *panel = lv_timer_get_user_data(timer);
	empty_panel_t *state = lv_obj_get_user_data(panel);
	// Only while the panel is up on the page on show; a page left behind looks
	// again when it is opened.
	if (!state || lv_obj_is_hidden(panel) || lv_obj_get_screen(panel) != lv_screen_active()) {
		lv_timer_pause(timer);
		return;
	}
	if (gui_card_available()) {
		lv_timer_pause(timer);
		if (state->card_back) {
			state->card_back();
		}
	}
}

void gridpage_show_no_card(lv_obj_t *grid, lv_obj_t *panel, void (*card_back)(void)) {
	if (grid) {
		lv_obj_set_hidden(grid, true);
	}
	if (!panel) {
		return;
	}
	empty_panel_say(panel, &icon_sd_card, usb_storage_active() ? "usb_storage_shared" : "sd_card_missing", false);
	lv_obj_set_hidden(panel, false);

	empty_panel_t *state = lv_obj_get_user_data(panel);
	if (!state) {
		return;
	}
	state->card_back = card_back;
	if (!state->card_timer) {
		state->card_timer = lv_timer_create(card_check_cb, CARD_CHECK_MS, panel);
	}
	lv_timer_resume(state->card_timer);
}

void gridpage_show_empty(lv_obj_t *grid, lv_obj_t *panel, bool empty) {
	if (grid) {
		if (empty) {
			lv_obj_set_hidden(grid, true);
		} else {
			lv_obj_set_hidden(grid, false);
		}
	}
	if (panel) {
		empty_panel_t *state = lv_obj_get_user_data(panel);
		stop_card_check(state);
		if (state) {
			empty_panel_say(panel, state->icon, state->text, true);
		}
		if (empty) {
			// The accent may have changed since the panel was built.
			lv_obj_set_style_bg_color(lv_obj_get_child(panel, 2), theme()->accent, 0);
			lv_obj_set_hidden(panel, false);
		} else {
			lv_obj_set_hidden(panel, true);
		}
	}
}
