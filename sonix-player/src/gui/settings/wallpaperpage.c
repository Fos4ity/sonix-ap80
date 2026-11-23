#include "wallpaperpage.h"

#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/theme.h"
#include "src/gui/shell/toast.h"
#include "src/system/core/config.h"
#include "src/system/core/lang.h"
#include "src/system/core/utils.h"

#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

lv_obj_t *wallpaperpage_screen;

#define MAX_ENTRIES 64
#define NAME_MAX_LEN 256

#define CHECK_GREEN lv_color_make(46, 194, 126)

// Slider steps
#define OPACITY_STEPS 21 // 0%..100% in 5% steps
#define SCALE_STEPS 16   // 100%..250% in 10% steps
#define PAN_STEPS 21     // -100%..+100% in 10% steps (10 = 0%)

typedef struct {
	char name[NAME_MAX_LEN];
	bool is_dir;
} entry_t;

typedef struct {
	char path[PATH_MAX];
	char save_path[PATH_MAX];
	bool update_saved_path;
	bool silent_toast;
	int scale_pct;
	int pan_x;
	int pan_y;
	uint16_t *pixels;
	int w;
	int h;
} load_job_t;

static int panel_w = 480;
static int panel_h = 800;
static char root_dir[PATH_MAX];
static char current_dir[PATH_MAX];

static lv_obj_t *opa_slider;
static lv_obj_t *opa_val;
static lv_obj_t *scale_slider;
static lv_obj_t *scale_val;
static lv_obj_t *pan_x_slider;
static lv_obj_t *pan_x_val;
static lv_obj_t *pan_y_slider;
static lv_obj_t *pan_y_val;

static lv_obj_t *default_check;
static lv_obj_t *none_check;
static lv_obj_t *path_label;
static lv_obj_t *up_row;
static lv_obj_t *rows[MAX_ENTRIES];
static lv_obj_t *checks[MAX_ENTRIES];
static entry_t entries[MAX_ENTRIES];
static int entry_count;
static lv_obj_t *empty_label;
static bool busy;
static bool pending_rerender;

static void start_load(const char *load_path, const char *save_path, bool update_saved_path, bool silent_toast);

static bool is_picture(const char *name) {
	return name[0] != '.' &&
		   (has_extension(name, ".png") || has_extension(name, ".jpg") || has_extension(name, ".jpeg"));
}

static int compare_entries(const void *a, const void *b) {
	const entry_t *ea = (const entry_t *)a;
	const entry_t *eb = (const entry_t *)b;
	if (ea->is_dir != eb->is_dir) {
		return ea->is_dir ? -1 : 1; // folders first
	}
	return strcasecmp(ea->name, eb->name);
}

static void sync_slider_labels(void) {
	char buf[32];

	int opa = theme_get_wallpaper_opacity();
	int opa_step = (opa + 2) / 5;
	if (opa_step < 0) opa_step = 0;
	if (opa_step >= OPACITY_STEPS) opa_step = OPACITY_STEPS - 1;
	lv_slider_set_value(opa_slider, opa_step, LV_ANIM_OFF);
	snprintf(buf, sizeof(buf), "%d%%", opa_step * 5);
	lv_label_set_text(opa_val, buf);

	int sc = theme_get_wallpaper_scale();
	int sc_step = (sc - 100 + 5) / 10;
	if (sc_step < 0) sc_step = 0;
	if (sc_step >= SCALE_STEPS) sc_step = SCALE_STEPS - 1;
	lv_slider_set_value(scale_slider, sc_step, LV_ANIM_OFF);
	snprintf(buf, sizeof(buf), "%d%%", 100 + sc_step * 10);
	lv_label_set_text(scale_val, buf);

	int px = theme_get_wallpaper_pan_x();
	int px_step = (px + 100 + 5) / 10;
	if (px_step < 0) px_step = 0;
	if (px_step >= PAN_STEPS) px_step = PAN_STEPS - 1;
	lv_slider_set_value(pan_x_slider, px_step, LV_ANIM_OFF);
	int px_disp = (px_step - 10) * 10;
	snprintf(buf, sizeof(buf), px_disp > 0 ? "+%d%%" : "%d%%", px_disp);
	lv_label_set_text(pan_x_val, buf);

	int py = theme_get_wallpaper_pan_y();
	int py_step = (py + 100 + 5) / 10;
	if (py_step < 0) py_step = 0;
	if (py_step >= PAN_STEPS) py_step = PAN_STEPS - 1;
	lv_slider_set_value(pan_y_slider, py_step, LV_ANIM_OFF);
	int py_disp = (py_step - 10) * 10;
	snprintf(buf, sizeof(buf), py_disp > 0 ? "+%d%%" : "%d%%", py_disp);
	lv_label_set_text(pan_y_val, buf);
}

static void paint_checks(void) {
	const char *chosen = theme_get_wallpaper_path();
	bool is_default = (!chosen || !chosen[0]);
	bool is_none = (chosen && strcmp(chosen, "none") == 0);

	lv_obj_set_hidden(default_check, !is_default);
	lv_obj_set_hidden(none_check, !is_none);

	for (int i = 0; i < entry_count; i++) {
		if (entries[i].is_dir) {
			lv_obj_set_hidden(checks[i], true);
			continue;
		}
		char full[PATH_MAX];
		snprintf(full, sizeof(full), "%s/%s", current_dir, entries[i].name);
		bool match = (!is_default && !is_none && strcmp(full, chosen) == 0);
		lv_obj_set_hidden(checks[i], !match);
	}
}

static void scan_dir(void) {
	entry_count = 0;
	DIR *d = current_dir[0] ? opendir(current_dir) : NULL;
	if (!d) {
		return;
	}

	struct dirent *e;
	while (entry_count < MAX_ENTRIES && (e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') {
			continue;
		}
		if (strlen(e->d_name) >= NAME_MAX_LEN) {
			continue;
		}
		char full[PATH_MAX];
		if ((size_t)snprintf(full, sizeof(full), "%s/%s", current_dir, e->d_name) >= sizeof(full)) {
			continue;
		}
		struct stat st;
		if (stat(full, &st) != 0) {
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			snprintf(entries[entry_count].name, NAME_MAX_LEN, "%s", e->d_name);
			entries[entry_count].is_dir = true;
			entry_count++;
		} else if (S_ISREG(st.st_mode) && is_picture(e->d_name)) {
			snprintf(entries[entry_count].name, NAME_MAX_LEN, "%s", e->d_name);
			entries[entry_count].is_dir = false;
			entry_count++;
		}
	}
	closedir(d);
	qsort(entries, (size_t)entry_count, sizeof(entry_t), compare_entries);
}

static void refresh(void) {
	scan_dir();
	lv_label_set_text(path_label, current_dir);

	bool at_root = (strcmp(current_dir, root_dir) == 0 || strcmp(current_dir, "/") == 0);
	lv_obj_set_hidden(up_row, at_root);

	for (int i = 0; i < MAX_ENTRIES; i++) {
		if (i < entry_count) {
			char label[NAME_MAX_LEN + 4];
			if (entries[i].is_dir) {
				snprintf(label, sizeof(label), "%s/", entries[i].name);
			} else {
				snprintf(label, sizeof(label), "%s", entries[i].name);
			}
			lv_label_set_text(settingsrow_name_label(rows[i]), label);
		}
		lv_obj_set_hidden(rows[i], i >= entry_count);
	}
	lv_obj_set_hidden(empty_label, entry_count > 0);
	sync_slider_labels();
	paint_checks();
}

static void rerender_current(void) {
	const char *load_path = theme_get_wallpaper_load_path();
	if (!load_path) {
		return;
	}
	if (busy) {
		pending_rerender = true;
		return;
	}
	start_load(load_path, NULL, false, true);
}

static void load_done(void *user) {
	load_job_t *job = user;
	busy = false;
	if (!job->silent_toast) {
		toast_busy_end();
	}
	if (job->pixels) {
		theme_apply_wallpaper_pixels(job->pixels, job->w, job->h,
									 job->update_saved_path ? job->save_path : NULL);
		if (!job->silent_toast) {
			toast_success(tr("Wallpaper updated"));
		}
	} else if (!job->silent_toast) {
		toast_error(tr("Could not load image"));
	}
	paint_checks();
	free(job);

	if (pending_rerender) {
		pending_rerender = false;
		rerender_current();
	}
}

static void *load_worker(void *arg) {
	load_job_t *job = arg;
	thread_be_low_priority("wallpaper");
	job->pixels = theme_decode_wallpaper_rgb565(job->path, panel_w, panel_h,
												job->scale_pct, job->pan_x, job->pan_y,
												&job->w, &job->h);
	while (!gui_post(load_done, job)) {
		usleep(50 * 1000);
	}
	return NULL;
}

static void start_load(const char *load_path, const char *save_path, bool update_saved_path, bool silent_toast) {
	load_job_t *job = calloc(1, sizeof(*job));
	if (!job) {
		return;
	}
	snprintf(job->path, sizeof(job->path), "%s", load_path);
	if (save_path) {
		snprintf(job->save_path, sizeof(job->save_path), "%s", save_path);
	}
	job->update_saved_path = update_saved_path;
	job->silent_toast = silent_toast;
	job->scale_pct = theme_get_wallpaper_scale();
	job->pan_x = theme_get_wallpaper_pan_x();
	job->pan_y = theme_get_wallpaper_pan_y();

	pthread_t thread;
	if (pthread_create(&thread, NULL, load_worker, job) != 0) {
		free(job);
		if (!silent_toast) {
			toast_error(tr("Could not load image"));
		}
		return;
	}
	pthread_detach(thread);
	busy = true;
	if (!silent_toast) {
		toast_busy(tr("Loading wallpaper..."));
	}
}

static void opacity_slider_cb(lv_event_t *e) {
	(void)e;
	int step = (int)lv_slider_get_value(opa_slider);
	int pct = step * 5;
	char buf[16];
	snprintf(buf, sizeof(buf), "%d%%", pct);
	lv_label_set_text(opa_val, buf);
	theme_set_wallpaper_opacity(pct);
}

static void transform_value_cb(lv_event_t *e) {
	(void)e;
	char buf[16];
	int sc = 100 + (int)lv_slider_get_value(scale_slider) * 10;
	int px = ((int)lv_slider_get_value(pan_x_slider) - 10) * 10;
	int py = ((int)lv_slider_get_value(pan_y_slider) - 10) * 10;

	snprintf(buf, sizeof(buf), "%d%%", sc);
	lv_label_set_text(scale_val, buf);
	snprintf(buf, sizeof(buf), px > 0 ? "+%d%%" : "%d%%", px);
	lv_label_set_text(pan_x_val, buf);
	snprintf(buf, sizeof(buf), py > 0 ? "+%d%%" : "%d%%", py);
	lv_label_set_text(pan_y_val, buf);
}

static void transform_release_cb(lv_event_t *e) {
	(void)e;
	int sc = 100 + (int)lv_slider_get_value(scale_slider) * 10;
	int px = ((int)lv_slider_get_value(pan_x_slider) - 10) * 10;
	int py = ((int)lv_slider_get_value(pan_y_slider) - 10) * 10;
	theme_set_wallpaper_transform(sc, px, py);
	rerender_current();
}

static void reset_transform_cb(lv_event_t *e) {
	(void)e;
	if (switcher_back_drag_active()) {
		return;
	}
	theme_set_wallpaper_transform(100, 0, 0);
	sync_slider_labels();
	rerender_current();
	toast_success(tr("Position & scale reset"));
}

static void entry_cb(lv_event_t *e) {
	int idx = (int)(intptr_t)lv_event_get_user_data(e);
	if (busy || switcher_back_drag_active() || idx < 0 || idx >= entry_count) {
		return;
	}

	char full[PATH_MAX];
	if ((size_t)snprintf(full, sizeof(full), "%s/%s", current_dir, entries[idx].name) >= sizeof(full)) {
		return;
	}

	if (entries[idx].is_dir) {
		snprintf(current_dir, sizeof(current_dir), "%s", full);
		refresh();
	} else {
		start_load(full, full, true, false);
	}
}

static void up_cb(lv_event_t *e) {
	(void)e;
	if (busy || switcher_back_drag_active()) {
		return;
	}
	char *slash = strrchr(current_dir, '/');
	if (slash && slash != current_dir) {
		*slash = '\0';
	} else {
		snprintf(current_dir, sizeof(current_dir), "%s", root_dir);
	}
	refresh();
}

static void default_cb(lv_event_t *e) {
	(void)e;
	if (busy || switcher_back_drag_active()) {
		return;
	}
	theme_set_wallpaper_file("", panel_w, panel_h);
	paint_checks();
	toast_success(tr("Default wallpaper"));
}

static void none_cb(lv_event_t *e) {
	(void)e;
	if (busy || switcher_back_drag_active()) {
		return;
	}
	theme_set_wallpaper_file("none", panel_w, panel_h);
	paint_checks();
	toast_success(tr("Wallpaper disabled"));
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
	lv_label_set_text(note, tr(text));
	return note;
}

static void loaded_cb(lv_event_t *e) {
	(void)e;
	if (!current_dir[0]) {
		snprintf(current_dir, sizeof(current_dir), "%s", root_dir);
	}
	refresh();
}

void wallpaperpage_init(gui_config_t *cfg) {
	panel_w = cfg->screen_width > 0 ? cfg->screen_width : 480;
	panel_h = cfg->screen_height > 0 ? cfg->screen_height : 800;
	snprintf(root_dir, sizeof(root_dir), "%s", cfg->sd_root_path && cfg->sd_root_path[0] ? cfg->sd_root_path : "/");
	snprintf(current_dir, sizeof(current_dir), "%s", root_dir);

	wallpaperpage_screen = lv_obj_create(NULL);
	lv_obj_add_style(wallpaperpage_screen, &theme_style_screen, 0);

	lv_obj_t *container = settingsrow_page(wallpaperpage_screen, cfg, "Menu wallpaper");

	// --- Adjustments ---
	settingsrow_heading(container, "Adjustments");

	settingsrow_slider(container, "Opacity", OPACITY_STEPS, &opa_val, &opa_slider, opacity_slider_cb);

	settingsrow_slider(container, "Scale", SCALE_STEPS, &scale_val, &scale_slider, transform_value_cb);
	lv_obj_add_event_cb(scale_slider, transform_release_cb, LV_EVENT_RELEASED, NULL);

	settingsrow_slider(container, "Horizontal position", PAN_STEPS, &pan_x_val, &pan_x_slider, transform_value_cb);
	lv_obj_add_event_cb(pan_x_slider, transform_release_cb, LV_EVENT_RELEASED, NULL);

	settingsrow_slider(container, "Vertical position", PAN_STEPS, &pan_y_val, &pan_y_slider, transform_value_cb);
	lv_obj_add_event_cb(pan_y_slider, transform_release_cb, LV_EVENT_RELEASED, NULL);

	settingsrow_action(container, "Reset position & scale", reset_transform_cb, NULL);

	// --- Source image ---
	settingsrow_heading(container, "Image");

	lv_obj_t *default_row = settingsrow_action(container, "Default (matches theme)", default_cb, NULL);
	default_check = add_check(default_row);

	lv_obj_t *none_row = settingsrow_action(container, "None", none_cb, NULL);
	none_check = add_check(none_row);

	path_label = add_note(container, current_dir);

	up_row = settingsrow_action(container, ".. (Up)", up_cb, NULL);
	lv_obj_set_hidden(up_row, true);

	for (int i = 0; i < MAX_ENTRIES; i++) {
		rows[i] = settingsrow_action(container, "", entry_cb, (void *)(intptr_t)i);
		checks[i] = add_check(rows[i]);
		settingsrow_name_lines(rows[i], 1);
		lv_obj_set_hidden(rows[i], true);
	}
	empty_label = add_note(container, "No PNG/JPG images in this folder.");

	sync_slider_labels();
	lv_obj_add_event_cb(wallpaperpage_screen, loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
	switcher_attach_back_gesture(wallpaperpage_screen);
}
