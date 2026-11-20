#include "fontpage.h"

#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/theme.h"
#include "src/gui/shell/toast.h"
#include "src/system/core/utils.h"
#include "src/system/device/system.h"

#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

lv_obj_t *fontpage_screen;

#define MAX_ENTRIES 64
#define SCAN_LIMIT 512
#define NAME_MAX_LEN 256
#define CHECK_GREEN lv_color_make(46, 194, 126)

typedef struct {
	char name[NAME_MAX_LEN];
	bool is_dir;
} font_entry_t;

static char root_dir[PATH_MAX];
static char current_dir[PATH_MAX];

static lv_obj_t *default_check;
static lv_obj_t *path_label;
static lv_obj_t *up_row;
static lv_obj_t *rows[MAX_ENTRIES];
static lv_obj_t *checks[MAX_ENTRIES];
static font_entry_t entries[MAX_ENTRIES];
static int entry_count;
static lv_obj_t *empty_label;

static bool is_font_file(const char *name) {
	return name[0] != '.' && (has_extension(name, ".ttf") || has_extension(name, ".otf"));
}

static int compare_entries(const void *a, const void *b) {
	const font_entry_t *ea = (const font_entry_t *)a;
	const font_entry_t *eb = (const font_entry_t *)b;
	// List font files before folders so fonts on a crowded SD root are never
	// pushed past MAX_ENTRIES by music/artist directories.
	if (ea->is_dir != eb->is_dir) {
		return ea->is_dir ? 1 : -1;
	}
	return strcasecmp(ea->name, eb->name);
}

static void set_root_dir(const char *sd_root) {
	if (sd_root && sd_root[0]) {
		snprintf(root_dir, sizeof(root_dir), "%s", sd_root);
		size_t len = strlen(root_dir);
		while (len > 1 && root_dir[len - 1] == '/') {
			root_dir[--len] = '\0';
		}
	} else {
		root_dir[0] = '\0';
	}
}

static bool at_root(void) {
	return !root_dir[0] || strcmp(current_dir, root_dir) == 0;
}

static void paint_checks(void) {
	const char *chosen = fonts_get_custom_file();
	bool is_default = (!chosen || !chosen[0]);

	lv_obj_set_hidden(default_check, !is_default);

	for (int i = 0; i < entry_count; i++) {
		if (entries[i].is_dir) {
			lv_obj_set_hidden(checks[i], true);
			continue;
		}
		char full[PATH_MAX];
		snprintf(full, sizeof(full), "%s/%s", current_dir, entries[i].name);
		bool match = (!is_default && strcmp(full, chosen) == 0);
		lv_obj_set_hidden(checks[i], !match);
	}
}

static void scan_dir(void) {
	entry_count = 0;
	if (!root_dir[0] || !current_dir[0]) {
		return;
	}
	DIR *d = opendir(current_dir);
	if (!d) {
		return;
	}

	font_entry_t *all = malloc(sizeof(font_entry_t) * SCAN_LIMIT);
	if (!all) {
		closedir(d);
		return;
	}
	int total = 0;

	struct dirent *de;
	while ((de = readdir(d)) != NULL && total < SCAN_LIMIT) {
		const char *name = de->d_name;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
			continue;
		}
		// Skip hidden files/folders except .local (commonly used for .local/fonts)
		if (name[0] == '.' && strcmp(name, ".local") != 0) {
			continue;
		}
		char full[PATH_MAX];
		int n = snprintf(full, sizeof(full), "%s/%s", current_dir, name);
		if (n <= 0 || (size_t)n >= sizeof(full)) {
			continue;
		}
		// Use lstat() and skip symlinks so browsing stays strictly inside the SD card.
		struct stat st;
		if (lstat(full, &st) != 0 || S_ISLNK(st.st_mode)) {
			continue;
		}
		bool is_dir = S_ISDIR(st.st_mode);
		if (!is_dir && (!S_ISREG(st.st_mode) || !is_font_file(name))) {
			continue;
		}
		snprintf(all[total].name, sizeof(all[total].name), "%s", name);
		all[total].is_dir = is_dir;
		total++;
	}
	closedir(d);

	if (total > 1) {
		qsort(all, (size_t)total, sizeof(all[0]), compare_entries);
	}
	entry_count = total < MAX_ENTRIES ? total : MAX_ENTRIES;
	if (entry_count > 0) {
		memcpy(entries, all, sizeof(font_entry_t) * (size_t)entry_count);
	}
	free(all);
}

static void refresh(void) {
	if (!root_dir[0]) {
		const char *sd = storage_sd_root();
		if (sd && sd[0]) {
			set_root_dir(sd);
			fonts_update_card_root_only(root_dir);
		}
	}

	if (!gui_card_available() || !root_dir[0] || access(root_dir, R_OK | X_OK) != 0) {
		entry_count = 0;
		lv_label_set_text(path_label, "SD card");
		lv_obj_set_hidden(up_row, true);
		for (int i = 0; i < MAX_ENTRIES; i++) {
			lv_obj_set_hidden(rows[i], true);
		}
		lv_label_set_text(empty_label, "Insert a microSD card with .ttf or .otf fonts.");
		lv_obj_set_hidden(empty_label, false);
		paint_checks();
		return;
	}

	// Reset to SD root if a previously visited subfolder vanished after a card swap.
	if (!current_dir[0] || access(current_dir, R_OK | X_OK) != 0) {
		snprintf(current_dir, sizeof(current_dir), "%s", root_dir);
	}

	scan_dir();

	// Display path relative to SD root so it is clear this is SD-card only.
	if (at_root()) {
		lv_label_set_text(path_label, "SD card /");
	} else {
		size_t rlen = strlen(root_dir);
		const char *rel = (strncmp(current_dir, root_dir, rlen) == 0 && current_dir[rlen] == '/')
							  ? current_dir + rlen
							  : current_dir;
		char disp[PATH_MAX + 16];
		snprintf(disp, sizeof(disp), "SD card %s", rel);
		lv_label_set_text(path_label, disp);
	}

	lv_obj_set_hidden(up_row, at_root());

	for (int i = 0; i < MAX_ENTRIES; i++) {
		if (i < entry_count) {
			char label_buf[NAME_MAX_LEN + 4];
			if (entries[i].is_dir) {
				snprintf(label_buf, sizeof(label_buf), "%s/", entries[i].name);
			} else {
				snprintf(label_buf, sizeof(label_buf), "%s", entries[i].name);
			}
			lv_obj_t *lbl = lv_obj_get_child(rows[i], 0);
			if (lbl) {
				lv_label_set_text(lbl, label_buf);
			}
			lv_obj_set_hidden(rows[i], false);
		} else {
			lv_obj_set_hidden(rows[i], true);
		}
	}

	lv_label_set_text(empty_label, "No .ttf or .otf fonts in this folder.");
	lv_obj_set_hidden(empty_label, entry_count > 0);
	paint_checks();
}

static void entry_cb(lv_event_t *e) {
	if (switcher_back_drag_active()) {
		return;
	}
	int idx = (int)(intptr_t)lv_event_get_user_data(e);
	if (idx < 0 || idx >= entry_count) {
		return;
	}
	char full[PATH_MAX];
	int n = snprintf(full, sizeof(full), "%s/%s", current_dir, entries[idx].name);
	if (n <= 0 || (size_t)n >= sizeof(full)) {
		return;
	}
	if (entries[idx].is_dir) {
		snprintf(current_dir, sizeof(current_dir), "%s", full);
		refresh();
	} else {
		if (fonts_set_custom_file(full)) {
			paint_checks();
			toast_success("UI font updated");
		} else {
			toast_plain("Could not load font");
		}
	}
}

static void up_cb(lv_event_t *e) {
	(void)e;
	if (switcher_back_drag_active() || at_root()) {
		return;
	}
	char *slash = strrchr(current_dir, '/');
	if (slash && slash != current_dir) {
		*slash = '\0';
		size_t rlen = strlen(root_dir);
		if (strncmp(current_dir, root_dir, rlen) != 0) {
			snprintf(current_dir, sizeof(current_dir), "%s", root_dir);
		}
	} else {
		snprintf(current_dir, sizeof(current_dir), "%s", root_dir);
	}
	refresh();
}

static void default_cb(lv_event_t *e) {
	(void)e;
	if (switcher_back_drag_active()) {
		return;
	}
	if (fonts_set_custom_file("")) {
		paint_checks();
		toast_success("Default font");
	}
}

static lv_obj_t *add_check(lv_obj_t *row) {
	lv_obj_t *mark = lv_image_create(row);
	lv_image_set_src(mark, &icon_check);
	lv_obj_set_style_image_recolor(mark, CHECK_GREEN, 0);
	lv_obj_set_style_image_recolor_opa(mark, LV_OPA_COVER, 0);
	lv_obj_align(mark, LV_ALIGN_RIGHT_MID, 0, 0);
	lv_obj_set_hidden(mark, true);
	return mark;
}

static lv_obj_t *add_note(lv_obj_t *parent, const char *text) {
	lv_obj_t *note = lv_label_create(parent);
	lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(note, lv_pct(100));
	lv_obj_add_style(note, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(note, &font_ui_22, 0);
	lv_label_set_text(note, text);
	return note;
}

static void loaded_cb(lv_event_t *e) {
	(void)e;
	refresh();
}

void fontpage_init(gui_config_t *cfg) {
	set_root_dir(cfg->sd_root_path);
	snprintf(current_dir, sizeof(current_dir), "%s", root_dir);

	fontpage_screen = lv_obj_create(NULL);
	lv_obj_add_style(fontpage_screen, &theme_style_screen, 0);

	lv_obj_t *container = settingsrow_page(fontpage_screen, cfg, "UI font");

	lv_obj_t *default_row = settingsrow_action(container, "Default", default_cb, NULL);
	default_check = add_check(default_row);

	settingsrow_heading(container, "SD card fonts");
	path_label = add_note(container, "SD card /");

	up_row = settingsrow_action(container, ".. (Up)", up_cb, NULL);
	lv_obj_set_hidden(up_row, true);

	for (int i = 0; i < MAX_ENTRIES; i++) {
		rows[i] = settingsrow_action(container, "", entry_cb, (void *)(intptr_t)i);
		checks[i] = add_check(rows[i]);
		settingsrow_name_lines(rows[i], 1);
		lv_obj_set_hidden(rows[i], true);
	}
	empty_label = add_note(container, "No .ttf or .otf fonts in this folder.");

	lv_obj_add_event_cb(fontpage_screen, loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
	switcher_attach_back_gesture(fontpage_screen);
}
