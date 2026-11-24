#include "src/gui/settings/lookpicker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl/lvgl.h"

#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/iconpack.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/theme.h"
#include "src/gui/shell/toast.h"
#include "src/system/core/lang.h"

#define MAX_ROWS 60			// built-in + FONTS_CUSTOM_MAX (24) / ICONPACK_MAX (16); 8 themes + 48 skins
#define PREVIEW_FONT_PX 26
#define GLYPH_PX 30
#define TILE_PX 44
#define GLYPHS_PER_ROW 4

typedef enum { PICK_FONTS, PICK_ICONS, PICK_THEMES } pick_kind_t;

static lv_obj_t *veil;
static pick_kind_t kind;
static void (*done_cb)(void);

static char row_name[MAX_ROWS][64]; // "" is the built-in choice
static int row_count;
static lv_font_t *row_font[MAX_ROWS];
static lv_image_dsc_t *row_image[MAX_ROWS][GLYPHS_PER_ROW + 1];

static char pending[64];
static bool have_pending;

// Theme rows: a colour theme (preset) or a Compas skin, by its index there.
static bool row_is_skin[MAX_ROWS];
static int row_index[MAX_ROWS];
static int pending_row;

static bool picker_czech(void) {
	const char *l = lang_current();
	return l && strncmp(l, "\xC4\x8C" "e", 3) == 0; // "Čeština"
}

// Sonix's two own palettes get plain names; the rest keep theirs.
static const char *preset_label(int index) {
	const theme_palette_t *p = theme_preset_palette(index);
	if (index == 0) {
		return picker_czech() ? "Sonix tmav\xC3\xBD" : "Sonix dark";
	}
	if (!p->dark && strcmp(theme_preset_name(index), "Light") == 0) {
		return picker_czech() ? "Sonix sv\xC4\x9Btl\xC3\xBD" : "Sonix light";
	}
	return theme_preset_name(index);
}

const char *lookpicker_theme_name(void) {
	int skin = theme_get_skin();
	return skin >= 0 ? theme_skin_name(skin) : preset_label(theme_get_preset());
}

static void apply_theme_row(int i) {
	if (!row_is_skin[i]) {
		theme_set_preset(row_index[i]);
		return;
	}
	theme_set_skin(row_index[i]);
	// The two Nothing skins each have an icon pack of the same name (white
	// tiles for the dark one, black for the light): keep them in step.
	const char *skin = theme_skin_name(row_index[i]);
	const char *pack = iconpack_current();
	bool pack_nothing = strcmp(pack, "Nothing") == 0 || strcmp(pack, "Nothing Light") == 0;
	bool skin_nothing = strcmp(skin, "Nothing") == 0 || strcmp(skin, "Nothing Light") == 0;
	if (pack_nothing && skin_nothing && strcmp(pack, skin) != 0) {
		iconpack_count();
		iconpack_set(skin);
	}
}

static void free_previews(void) {
	for (int i = 0; i < MAX_ROWS; i++) {
		fonts_preview_close(row_font[i]);
		row_font[i] = NULL;
		for (int k = 0; k <= GLYPHS_PER_ROW; k++) {
			iconpack_preview_free(row_image[i][k]);
			row_image[i][k] = NULL;
		}
	}
}

// One tick after the tap: the list is deleted first, then its preview faces
// and pictures (nothing draws with them any more), and only then the choice
// is applied -- that rebuilds fonts or repaints every screen.
static void finish_cb(void *unused) {
	(void)unused;
	if (veil) {
		lv_obj_delete(veil);
		veil = NULL;
	}
	free_previews();
	if (have_pending) {
		have_pending = false;
		if (kind == PICK_THEMES) {
			apply_theme_row(pending_row);
		} else if (kind == PICK_FONTS) {
			if (!fonts_set_custom(pending)) {
				toast_error("Font?");
			}
		} else {
			iconpack_set(pending);
		}
	}
	if (done_cb) {
		done_cb();
	}
}

static void close_cb(lv_event_t *e) {
	(void)e;
	lv_async_call(finish_cb, NULL);
}

static void row_cb(lv_event_t *e) {
	int i = (int)(intptr_t)lv_event_get_user_data(e);
	if (i >= 0 && i < row_count) {
		snprintf(pending, sizeof(pending), "%s", row_name[i]);
		pending_row = i;
		have_pending = true;
	}
	lv_async_call(finish_cb, NULL);
}

static void card_cb(lv_event_t *e) { (void)e; } // taps on the card do not fall through to the veil

static lv_obj_t *add_row(lv_obj_t *list, int index, bool current) {
	lv_obj_t *row = lv_obj_create(list);
	lv_obj_set_width(row, lv_pct(100));
	lv_obj_set_height(row, LV_SIZE_CONTENT);
	lv_obj_set_style_min_height(row, 64, 0);
	lv_obj_set_style_radius(row, 12, 0);
	lv_obj_set_style_bg_color(row, theme()->surface, 0);
	lv_obj_set_style_bg_color(row, theme()->surface_pressed, LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(row, theme()->accent, 0);
	lv_obj_set_style_border_width(row, current ? 2 : 0, 0);
	lv_obj_set_style_pad_hor(row, 14, 0);
	lv_obj_set_style_pad_ver(row, 10, 0);
	lv_obj_set_style_pad_row(row, 8, 0);
	lv_obj_set_scrollable(row, false);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
	lv_obj_set_clickable(row, true);
	lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);
	return row;
}

static lv_obj_t *add_label(lv_obj_t *parent, const char *text, const lv_font_t *font, bool dim) {
	lv_obj_t *label = lv_label_create(parent);
	lv_obj_set_width(label, lv_pct(100));
	lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
	lv_obj_set_style_text_font(label, font, 0);
	lv_obj_set_style_text_color(label, dim ? theme()->text_secondary : theme()->text_primary, 0);
	lv_label_set_text(label, text);
	lv_obj_set_clickable(label, false);
	return label;
}

static lv_obj_t *add_section(lv_obj_t *list, const char *text) {
	lv_obj_t *label = add_label(list, text, &font_ui_20_bold, true);
	lv_obj_set_style_pad_left(label, 6, 0);
	lv_obj_set_style_pad_top(label, 6, 0);
	return label;
}

// One theme, painted in its own colours: the page behind, a card with the
// name and a line of small text on it, and the accent as a dot.
static lv_obj_t *add_theme_row(lv_obj_t *list, int index, const char *name, const theme_palette_t *p, bool current) {
	lv_obj_t *row = add_row(list, index, current);
	lv_obj_set_style_bg_color(row, p->screen_bg, 0);
	lv_obj_set_style_bg_color(row, p->screen_bg, LV_STATE_PRESSED);
	lv_obj_set_style_border_color(row, current ? theme()->accent : p->text_secondary, 0);
	lv_obj_set_style_border_width(row, current ? 3 : 1, 0);
	lv_obj_set_style_border_opa(row, current ? LV_OPA_COVER : LV_OPA_40, 0);
	lv_obj_set_style_pad_all(row, 8, 0);

	lv_obj_t *card = lv_obj_create(row);
	lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_radius(card, 8, 0);
	lv_obj_set_style_bg_color(card, p->surface, 0);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(card, 0, 0);
	lv_obj_set_style_pad_hor(card, 12, 0);
	lv_obj_set_style_pad_ver(card, 8, 0);
	lv_obj_set_style_pad_column(card, 10, 0);
	lv_obj_set_scrollable(card, false);
	lv_obj_set_clickable(card, false);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	lv_obj_t *text = lv_label_create(card);
	lv_obj_set_flex_grow(text, 1);
	lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
	lv_obj_set_style_text_font(text, &font_ui_22, 0);
	lv_obj_set_style_text_color(text, p->text_primary, 0);
	lv_label_set_text(text, name);
	lv_obj_set_clickable(text, false);

	lv_obj_t *small = lv_label_create(card);
	lv_obj_set_style_text_font(small, &font_ui_20, 0);
	lv_obj_set_style_text_color(small, p->text_secondary, 0);
	lv_label_set_text(small, "Aa");
	lv_obj_set_clickable(small, false);

	lv_obj_t *dot = lv_obj_create(card);
	lv_obj_set_size(dot, 22, 22);
	lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(dot, p->accent, 0);
	lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(dot, 0, 0);
	lv_obj_set_scrollable(dot, false);
	lv_obj_set_clickable(dot, false);
	return row;
}

// The order of the themes: Sonix's own two, then the colour themes, then the
// skins on the card. `section[i]` is 0, 1 or 2 for those three groups.
static int theme_order(bool is_skin[], int index[], int section[], int max) {
	int n = 0;
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < THEME_PRESET_COUNT && n < max; i++) {
			bool own = i == 0 || strcmp(theme_preset_name(i), "Light") == 0;
			if (own != (pass == 0)) {
				continue;
			}
			is_skin[n] = false;
			index[n] = i;
			section[n++] = pass;
		}
	}
	for (int i = 0; i < theme_skin_count() && n < max; i++) {
		is_skin[n] = true;
		index[n] = i;
		section[n++] = 2;
	}
	return n;
}

static lv_obj_t *fill_themes(lv_obj_t *list) {
	static int section[MAX_ROWS];
	lv_obj_t *current_row = NULL;
	int skin = theme_get_skin(), preset = theme_get_preset();
	row_count = theme_order(row_is_skin, row_index, section, MAX_ROWS);
	for (int i = 0; i < row_count; i++) {
		if (i == 0 || section[i] != section[i - 1]) {
			static const char *const cs[] = {"Sonix", "Barevn\xC3\xA9 motivy", "Skiny"};
			static const char *const en[] = {"Sonix", "Colour themes", "Skins"};
			add_section(list, (picker_czech() ? cs : en)[section[i]]);
		}
		row_name[i][0] = 0;
		bool cur = row_is_skin[i] ? skin == row_index[i] : skin < 0 && preset == row_index[i];
		const char *name = row_is_skin[i] ? theme_skin_name(row_index[i]) : preset_label(row_index[i]);
		const theme_palette_t *p = row_is_skin[i] ? theme_skin_palette(row_index[i]) : theme_preset_palette(row_index[i]);
		lv_obj_t *row = add_theme_row(list, i, name, p, cur);
		if (cur) {
			current_row = row;
		}
	}
	return current_row;
}

void lookpicker_theme_step(int dir) {
	if (veil) {
		return; // the list is open and owns the row tables
	}
	static int section[MAX_ROWS];
	int n = theme_order(row_is_skin, row_index, section, MAX_ROWS);
	if (n <= 0) {
		return;
	}
	int skin = theme_get_skin(), preset = theme_get_preset(), at = 0;
	for (int i = 0; i < n; i++) {
		if (row_is_skin[i] ? skin == row_index[i] : skin < 0 && preset == row_index[i]) {
			at = i;
			break;
		}
	}
	apply_theme_row(((at + (dir < 0 ? -1 : 1)) % n + n) % n);
}

static void open_list(pick_kind_t what, const char *title, void (*on_done)(void)) {
	if (veil) {
		return;
	}
	kind = what;
	done_cb = on_done;
	have_pending = false;

	// The choices: built-in first, then what the card holds.
	row_count = 0;
	row_name[row_count++][0] = 0;
	int n = what == PICK_THEMES ? 0 : what == PICK_FONTS ? fonts_custom_count() : iconpack_count();
	for (int i = 0; i < n && row_count < MAX_ROWS; i++) {
		snprintf(row_name[row_count++], sizeof(row_name[0]), "%s",
				 what == PICK_FONTS ? fonts_custom_name(i) : iconpack_name(i));
	}
	const char *current = what == PICK_THEMES ? "" : what == PICK_FONTS ? fonts_get_custom() : iconpack_current();

	veil = lv_obj_create(lv_layer_top());
	lv_obj_set_size(veil, lv_pct(100), lv_pct(100));
	lv_obj_set_pos(veil, 0, 0);
	lv_obj_set_style_bg_color(veil, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(veil, LV_OPA_60, 0);
	lv_obj_set_style_border_width(veil, 0, 0);
	lv_obj_set_style_radius(veil, 0, 0);
	lv_obj_set_style_pad_all(veil, 0, 0);
	lv_obj_set_scrollable(veil, false);
	lv_obj_set_clickable(veil, true);
	lv_obj_add_event_cb(veil, close_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *card = lv_obj_create(veil);
	lv_obj_set_size(card, lv_pct(92), lv_pct(84));
	lv_obj_center(card);
	lv_obj_set_style_radius(card, 18, 0);
	lv_obj_set_style_bg_color(card, theme()->panel, 0);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(card, 0, 0);
	lv_obj_set_style_pad_all(card, 12, 0);
	lv_obj_set_style_pad_row(card, 10, 0);
	lv_obj_set_scrollable(card, false);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_clickable(card, true);
	lv_obj_add_event_cb(card, card_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *head = add_label(card, title, &font_ui_24_bold, false);
	lv_obj_set_style_pad_left(head, 6, 0);

	lv_obj_t *list = lv_obj_create(card);
	lv_obj_set_width(list, lv_pct(100));
	lv_obj_set_flex_grow(list, 1);
	lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(list, 0, 0);
	lv_obj_set_style_pad_all(list, 0, 0);
	lv_obj_set_style_pad_row(list, 8, 0);
	lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_scroll_dir(list, LV_DIR_VER);

	static const char *const GLYPHS[GLYPHS_PER_ROW] = {"play", "heart", "folder", "search"};
	lv_obj_t *current_row = NULL;
	if (what == PICK_THEMES) {
		current_row = fill_themes(list);
	}
	for (int i = 0; what != PICK_THEMES && i < row_count; i++) {
		bool is_current = strcmp(row_name[i], current) == 0;
		lv_obj_t *row = add_row(list, i, is_current);
		if (is_current) {
			current_row = row;
		}
		if (what == PICK_FONTS) {
			// The name in its own face, and a line of sample text under it.
			row_font[i] = fonts_preview_open(row_name[i], PREVIEW_FONT_PX);
			const lv_font_t *face = row_font[i] ? row_font[i] : &font_ui_24;
			add_label(row, row_name[i][0] ? row_name[i] : "MiSans", face, false);
			add_label(row, "Abc \xC4\x8D\xC5\x99\xC5\xBE 0123  12:45", face, true);
		} else {
			add_label(row, row_name[i][0] ? row_name[i] : "Sonix", &font_ui_22, false);
			lv_obj_t *strip = lv_obj_create(row);
			lv_obj_set_size(strip, lv_pct(100), LV_SIZE_CONTENT);
			lv_obj_set_style_bg_opa(strip, LV_OPA_TRANSP, 0);
			lv_obj_set_style_border_width(strip, 0, 0);
			lv_obj_set_style_pad_all(strip, 0, 0);
			lv_obj_set_style_pad_column(strip, 14, 0);
			lv_obj_set_scrollable(strip, false);
			lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
			lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
			lv_obj_set_clickable(strip, false);
			row_image[i][0] = iconpack_preview(row_name[i], "music", true, TILE_PX);
			for (int k = 0; k < GLYPHS_PER_ROW; k++) {
				row_image[i][k + 1] = iconpack_preview(row_name[i], GLYPHS[k], false, GLYPH_PX);
			}
			for (int k = 0; k <= GLYPHS_PER_ROW; k++) {
				if (!row_image[i][k]) {
					continue;
				}
				lv_obj_t *img = lv_image_create(strip);
				lv_image_set_src(img, row_image[i][k]);
				if (k > 0) { // glyphs are white shapes: the theme's text colour
					lv_obj_set_style_image_recolor(img, theme()->text_primary, 0);
					lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
				}
				lv_obj_set_clickable(img, false);
			}
		}
	}
	if (current_row) {
		lv_obj_update_layout(list);
		lv_obj_scroll_to_view(current_row, LV_ANIM_OFF);
	}
}

void lookpicker_fonts(const char *title, void (*on_done)(void)) { open_list(PICK_FONTS, title, on_done); }
void lookpicker_icons(const char *title, void (*on_done)(void)) { open_list(PICK_ICONS, title, on_done); }
void lookpicker_themes(const char *title, void (*on_done)(void)) { open_list(PICK_THEMES, title, on_done); }
