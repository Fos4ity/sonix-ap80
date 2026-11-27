#include "lastfmsettings.h"

#include <stdio.h>
#include <string.h>

#include "src/gui/fonts/fonts.h"
#include "src/gui/shell/confirm.h"
#include "src/gui/shell/icons.h"
#include "src/gui/shell/keyboard.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/shell/switcher.h"
#include "src/gui/shell/theme.h"
#include "src/gui/shell/toast.h"
#include "src/system/core/lang.h"
#include "src/system/lastfm/lastfm.h"
#include "src/system/net/wifi.h"

#define TICK_MS 1000
#define QR_MAX_SIZE 200
#define QR_QUIET 10

static lv_obj_t *page_screen;
static lv_obj_t *enabled_switch;
static lv_obj_t *account_value;
static lv_obj_t *status_value;
static lv_obj_t *queued_value;
static lv_obj_t *note_label;
static lv_obj_t *setting_rows[5]; // hidden while the guide is shown
static lv_obj_t *keys_row;
static lv_obj_t *keys_value;
static lv_obj_t *guide_box;

static lv_obj_t *keys_screen;
static lv_obj_t *key_field;
static lv_obj_t *secret_field;
static keyboard_t *keys_keyboard;

static lv_obj_t *login_screen;
static lv_obj_t *login_message;
static lv_obj_t *login_qr;
static lv_obj_t *login_url;
static lv_obj_t *login_countdown;
static lv_obj_t *login_done;
static lv_obj_t *login_retry;

static unsigned seen_login_serial;
static unsigned seen_auth_serial;
static bool waiting_for_login;

lv_obj_t *lastfmsettings_screen(void) { return page_screen; }

void lastfmsettings_open(void) {
	if (page_screen) {
		switch_screen(page_screen);
	}
}

// Where a Last.fm API account is made. The QR on the guide carries it.
#define LASTFM_API_CREATE_URL "https://www.last.fm/api/account/create"
#define LASTFM_RED lv_color_hex(0xD51007)

// ---------------------------------------------------------------------------
// the page
// ---------------------------------------------------------------------------

static void refresh(void) {
	lastfm_status_t st;
	lastfm_get_status(&st);

	if (st.enabled) {
		lv_obj_add_state(enabled_switch, LV_STATE_CHECKED);
	} else {
		lv_obj_remove_state(enabled_switch, LV_STATE_CHECKED);
	}
	if (st.state == LASTFM_STATE_UNAVAILABLE) {
		lv_obj_add_state(enabled_switch, LV_STATE_DISABLED);
	}

	lv_label_set_text(account_value, st.user[0] ? st.user : tr("lastfm_not_signed_in"));

	const char *status = "";
	switch (st.state) {
	case LASTFM_STATE_UNAVAILABLE:
		status = tr("lastfm_unavailable");
		break;
	case LASTFM_STATE_OFF:
		status = tr("lastfm_state_off");
		break;
	case LASTFM_STATE_SIGNED_OUT:
		status = tr("lastfm_not_signed_in");
		break;
	case LASTFM_STATE_SIGNING_IN:
		status = tr("lastfm_state_signing_in");
		break;
	case LASTFM_STATE_AWAITING_AUTH:
		status = tr("lastfm_auth_waiting");
		break;
	case LASTFM_STATE_CONNECTED:
		status = tr("connected");
		break;
	case LASTFM_STATE_SESSION_EXPIRED:
		status = tr("lastfm_state_session_expired");
		break;
	case LASTFM_STATE_REFUSED:
		status = st.detail;
		break;
	}
	lv_label_set_text(status_value, status);
	lv_label_set_text(note_label, tr(st.state == LASTFM_STATE_UNAVAILABLE ? "api_keys_unavailable" : "lastfm_note"));
	lv_label_set_text_fmt(queued_value, "%d", st.queued);

	// Without keys the page is the guide to getting them; with them, the
	// settings. The keys row is for typed keys only: a keys file wins.
	bool guide = st.state == LASTFM_STATE_UNAVAILABLE;
	for (size_t i = 0; i < sizeof(setting_rows) / sizeof(setting_rows[0]); i++) {
		if (guide) {
			lv_obj_add_flag(setting_rows[i], LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_remove_flag(setting_rows[i], LV_OBJ_FLAG_HIDDEN);
		}
	}
	if (guide || lastfm_keys_from_file()) {
		lv_obj_add_flag(keys_row, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_remove_flag(keys_row, LV_OBJ_FLAG_HIDDEN);
		lv_label_set_text(keys_value, tr("lastfm_keys_typed"));
	}
	if (guide) {
		lv_obj_remove_flag(guide_box, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(guide_box, LV_OBJ_FLAG_HIDDEN);
	}
}

// ---------------------------------------------------------------------------
// signing in with a token
// ---------------------------------------------------------------------------

static void set_visible(lv_obj_t *obj, bool visible) { lv_obj_set_hidden(obj, !visible); }

// The QR is the URL; the written URL stays as a fallback when the encoder
// cannot fit it, so a login flow never ends as a dead end.
static void show_auth_url(const char *url) {
	bool qr_ok = url && url[0] && lv_qrcode_update(login_qr, url, (uint32_t)strlen(url)) == LV_RESULT_OK;
	set_visible(login_qr, qr_ok);
	set_visible(login_url, !qr_ok);
	if (qr_ok) {
		lv_label_set_text(login_url, "");
	} else {
		lv_label_set_text(login_url, url ? url : "");
	}
	lv_label_set_text(login_message, tr("lastfm_auth_note"));
}

static void update_countdown(int seconds) {
	if (seconds > 0) {
		lv_label_set_text_fmt(login_countdown, "%s (%d:%02d)", tr("lastfm_auth_waiting"), seconds / 60, seconds % 60);
	} else {
		lv_label_set_text(login_countdown, tr("lastfm_auth_expired"));
	}
}

static void open_login(void) {
	wifi_status_t wifi;
	wifi_get_status(&wifi);
	if (wifi.state != WIFI_STATE_CONNECTED || !wifi.ip[0]) {
		toast_error("enable_wi_fi_first");
		return;
	}

	lastfm_status_t st;
	lastfm_get_status(&st);
	seen_auth_serial = st.auth_serial;
	show_auth_url(st.auth_url);
	update_countdown(st.auth_seconds_left);

	if (st.state != LASTFM_STATE_AWAITING_AUTH) {
		waiting_for_login = true;
		toast_busy("lastfm_state_signing_in");
		lastfm_begin_login();
	}
	switch_screen(login_screen);
}

static void enabled_cb(lv_event_t *e) {
	(void)e;
	bool on = lv_obj_has_state(enabled_switch, LV_STATE_CHECKED);
	lastfm_set_enabled(on);
	lastfm_status_t st;
	lastfm_get_status(&st);
	// Switched on with no account behind it: the sign-in is the next step.
	if (on && !st.user[0] && st.state != LASTFM_STATE_SIGNING_IN && st.state != LASTFM_STATE_AWAITING_AUTH) {
		open_login();
	}
	refresh();
}

static void do_logout(void *user) {
	(void)user;
	lastfm_logout();
	toast_success("signed_out");
	refresh();
}

static void account_cb(lv_event_t *e) {
	(void)e;
	lastfm_status_t st;
	lastfm_get_status(&st);
	if (st.state == LASTFM_STATE_UNAVAILABLE) {
		toast_error("api_keys_unavailable");
		return;
	}
	if (st.state == LASTFM_STATE_SIGNING_IN) {
		return;
	}
	if (st.user[0]) {
		char message[160];
		snprintf(message, sizeof(message), tr("sign_out_confirm_note"), "Last.fm");
		confirm_show("lastfm_sign_out", message, "leave", do_logout, NULL);
		return;
	}
	open_login();
}

static void loaded_cb(lv_event_t *e) {
	(void)e;
	refresh();
}


// ---------------------------------------------------------------------------
// the guide: what the page shows while there are no API keys
// ---------------------------------------------------------------------------

static void open_keys(void);

static void keys_row_cb(lv_event_t *e) {
	(void)e;
	open_keys();
}

static void enter_keys_cb(lv_event_t *e) {
	(void)e;
	open_keys();
}

static lv_obj_t *plain_box(lv_obj_t *parent) {
	lv_obj_t *box = lv_obj_create(parent);
	lv_obj_remove_style_all(box);
	lv_obj_set_width(box, lv_pct(100));
	lv_obj_set_height(box, LV_SIZE_CONTENT);
	lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(box, LV_OBJ_FLAG_EVENT_BUBBLE);
	return box;
}

static lv_obj_t *wrap_label(lv_obj_t *parent, const char *text, const lv_font_t *font, bool dim) {
	lv_obj_t *l = lv_label_create(parent);
	lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(l, lv_pct(100));
	lv_obj_add_style(l, dim ? &theme_style_text_dim : &theme_style_text, 0);
	lv_obj_set_style_text_font(l, font, 0);
	lv_label_set_text(l, text);
	return l;
}

// One numbered step: a red disc with the number, a bold title and the text
// under it. Returns the column, for a step that needs more in it.
static lv_obj_t *guide_step(lv_obj_t *parent, int number, const char *title, const char *body) {
	lv_obj_t *card = lv_obj_create(parent);
	lv_obj_add_style(card, &theme_style_card, 0);
	lv_obj_set_style_radius(card, 16, 0);
	lv_obj_set_style_border_width(card, 0, 0);
	lv_obj_set_style_shadow_width(card, 0, 0);
	lv_obj_set_style_pad_all(card, 14, 0);
	lv_obj_set_style_pad_column(card, 12, 0);
	lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

	lv_obj_t *disc = lv_obj_create(card);
	lv_obj_remove_style_all(disc);
	lv_obj_set_size(disc, 36, 36);
	lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(disc, LASTFM_RED, 0);
	lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
	lv_obj_remove_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_t *num = lv_label_create(disc);
	lv_obj_set_style_text_font(num, &font_ui_22_bold, 0);
	lv_obj_set_style_text_color(num, lv_color_white(), 0);
	lv_label_set_text_fmt(num, "%d", number);
	lv_obj_center(num);

	lv_obj_t *col = plain_box(card);
	lv_obj_set_flex_grow(col, 1);
	lv_obj_set_width(col, 0);
	lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(col, 6, 0);
	wrap_label(col, tr(title), &font_ui_22_bold, false);
	if (body) {
		wrap_label(col, tr(body), &font_ui_20, true);
	}
	return col;
}

// Text the user has to copy exactly: on a darker strip, so it reads as
// something to type rather than as a sentence.
static void guide_code(lv_obj_t *parent, const char *text) {
	lv_obj_t *strip = plain_box(parent);
	lv_obj_set_style_bg_color(strip, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(strip, LV_OPA_40, 0);
	lv_obj_set_style_radius(strip, 10, 0);
	lv_obj_set_style_pad_all(strip, 10, 0);
	lv_obj_t *l = wrap_label(strip, text, &font_ui_16, false);
	lv_obj_set_width(l, lv_pct(100));
}

static void build_guide(lv_obj_t *parent, gui_config_t *cfg) {
	(void)cfg;
	guide_box = plain_box(parent);
	lv_obj_set_flex_flow(guide_box, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(guide_box, 12, 0);
	lv_obj_set_style_pad_bottom(guide_box, 24, 0);

	// The tile, a heading and why the keys are needed.
	lv_obj_t *hero = plain_box(guide_box);
	lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(hero, 12, 0);
	lv_obj_t *logo = lv_image_create(hero);
	lv_image_set_src(logo, &icon_menu_lastfm);
	lv_image_set_scale(logo, 200); // 112 px at 256
	lv_obj_set_size(logo, 88, 88);
	lv_image_set_inner_align(logo, LV_IMAGE_ALIGN_CENTER);
	lv_obj_t *hero_text = plain_box(hero);
	lv_obj_set_flex_grow(hero_text, 1);
	lv_obj_set_width(hero_text, 0);
	lv_obj_set_flex_flow(hero_text, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(hero_text, 4, 0);
	wrap_label(hero_text, tr("lastfm_guide_title"), &font_ui_24_bold, false);
	wrap_label(hero_text, tr("lastfm_guide_intro"), &font_ui_18, true);

	// 1: the page that makes an API account, as a QR for the phone.
	lv_obj_t *step = guide_step(guide_box, 1, "lastfm_guide_step1", "lastfm_guide_step1_text");
	lv_obj_t *qr_row = plain_box(step);
	lv_obj_set_flex_flow(qr_row, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(qr_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_row(qr_row, 6, 0);
	lv_obj_set_style_pad_top(qr_row, 6, 0);
	// Black on white with a quiet zone, as on the Tidal sign-in: a camera
	// has to lock onto it.
	lv_obj_t *qr = lv_qrcode_create(qr_row);
	lv_qrcode_set_size(qr, 150);
	lv_qrcode_set_dark_color(qr, lv_color_black());
	lv_qrcode_set_light_color(qr, lv_color_white());
	lv_obj_set_style_border_color(qr, lv_color_white(), 0);
	lv_obj_set_style_border_width(qr, 8, 0);
	lv_obj_set_style_bg_color(qr, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(qr, LV_OPA_COVER, 0);
	lv_qrcode_update(qr, LASTFM_API_CREATE_URL, (uint32_t)strlen(LASTFM_API_CREATE_URL));
	lv_obj_t *url = wrap_label(qr_row, "last.fm/api/account/create", &font_ui_18, false);
	lv_obj_set_style_text_align(url, LV_TEXT_ALIGN_CENTER, 0);

	// 2: the form.
	step = guide_step(guide_box, 2, "lastfm_guide_step2", "lastfm_guide_step2_text");
	guide_code(step, "Application name:  Sonix AP80\nCallback URL:  -\nApplication homepage:  -");

	// 3: what Last.fm shows.
	guide_step(guide_box, 3, "lastfm_guide_step3", "lastfm_guide_step3_text");

	// 4: typing them in, or the file on the card.
	step = guide_step(guide_box, 4, "lastfm_guide_step4", "lastfm_guide_step4_text");
	lv_obj_t *btn = lv_button_create(step);
	lv_obj_set_size(btn, lv_pct(100), 56);
	lv_obj_set_style_radius(btn, 14, 0);
	lv_obj_set_style_bg_color(btn, LASTFM_RED, 0);
	lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
	lv_obj_set_style_shadow_width(btn, 0, 0);
	lv_obj_add_event_cb(btn, enter_keys_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *btn_label = lv_label_create(btn);
	lv_obj_set_style_text_font(btn_label, &font_ui_22_bold, 0);
	lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
	lv_label_set_text(btn_label, tr("lastfm_guide_enter_keys"));
	lv_obj_center(btn_label);
	wrap_label(step, tr("lastfm_guide_or_file"), &font_ui_18, true);
	guide_code(step, "sonix/resource/\ncomponents/streaming-keys.ini\n\n[lastfm]\napi_key = ...\napi_secret = ...");

	// 5: switching it on.
	guide_step(guide_box, 5, "lastfm_guide_step5", "lastfm_guide_step5_text");

	wrap_label(guide_box, tr("lastfm_guide_private"), &font_ui_18, true);
}

// ---------------------------------------------------------------------------
// typing the keys in
// ---------------------------------------------------------------------------

static void keys_field_focus_cb(lv_event_t *e) {
	lv_obj_t *field = lv_event_get_target(e);
	lv_obj_t *other = field == key_field ? secret_field : key_field;
	keyboard_set_field(keys_keyboard, field);
	lv_obj_add_state(field, LV_STATE_FOCUSED);
	lv_obj_remove_state(other, LV_STATE_FOCUSED);
	keyboard_show_caret(field, true);
	keyboard_show_caret(other, false);
}

static void open_keys(void) {
	lv_textarea_set_text(key_field, "");
	lv_textarea_set_text(secret_field, "");
	keyboard_reset(keys_keyboard);
	keyboard_set_field(keys_keyboard, key_field);
	lv_obj_add_state(key_field, LV_STATE_FOCUSED);
	lv_obj_remove_state(secret_field, LV_STATE_FOCUSED);
	keyboard_show_caret(key_field, true);
	keyboard_show_caret(secret_field, false);
	switch_screen(keys_screen);
}

static void keys_accept_cb(lv_event_t *e) {
	(void)e;
	// The keyboard's Enter on the first field moves on to the second.
	if (lv_obj_has_state(key_field, LV_STATE_FOCUSED) && !lv_textarea_get_text(secret_field)[0]) {
		char k[33];
		if (!lastfm_normalise_key(lv_textarea_get_text(key_field), k)) {
			toast_error("lastfm_keys_bad_key");
			return;
		}
		lv_obj_send_event(secret_field, LV_EVENT_CLICKED, NULL);
		return;
	}
	switch (lastfm_set_keys(lv_textarea_get_text(key_field), lv_textarea_get_text(secret_field))) {
	case LASTFM_KEYS_BAD_KEY:
		toast_error("lastfm_keys_bad_key");
		return;
	case LASTFM_KEYS_BAD_SECRET:
		toast_error("lastfm_keys_bad_secret");
		return;
	case LASTFM_KEYS_AFTER_RESTART:
		toast_success("lastfm_keys_after_restart");
		back_btn_cb(NULL);
		return;
	case LASTFM_KEYS_OK:
		break;
	}
	lv_textarea_set_text(secret_field, "");
	toast_success("lastfm_keys_saved");
	back_btn_cb(NULL);
	refresh();
	// Keys in, so the next thing is the account: switch scrobbling on and
	// ask for it straight away.
	lastfm_set_enabled(true);
	lastfm_status_t st;
	lastfm_get_status(&st);
	if (!st.user[0]) {
		open_login();
	}
}

static lv_obj_t *keys_make_field(lv_obj_t *parent, gui_config_t *cfg, const char *placeholder, int y) {
	lv_obj_t *field = lv_textarea_create(parent);
	lv_textarea_set_one_line(field, true);
	lv_textarea_set_max_length(field, 40);
	lv_textarea_set_placeholder_text(field, tr(placeholder));
	lv_obj_set_size(field, cfg->screen_width - 2 * cfg->padding, 54);
	lv_obj_set_scrollbar_mode(field, LV_SCROLLBAR_MODE_OFF);
	lv_obj_align(field, LV_ALIGN_TOP_LEFT, cfg->padding, y);
	lv_obj_add_style(field, &theme_style_card, 0);
	lv_obj_set_style_radius(field, 12, 0);
	lv_obj_set_style_border_width(field, 0, 0);
	lv_obj_set_style_shadow_width(field, 0, 0);
	lv_obj_set_style_pad_all(field, 12, 0);
	lv_obj_set_style_text_font(field, &font_ui_20, 0);
	keyboard_style_caret(field);
	lv_obj_add_event_cb(field, keys_field_focus_cb, LV_EVENT_CLICKED, NULL);
	return field;
}

static void build_keys(gui_config_t *cfg) {
	keys_screen = lv_obj_create(NULL);
	lv_obj_add_style(keys_screen, &theme_style_screen, 0);

	settingsrow_title(keys_screen, cfg, "lastfm_keys");
	int top = settingsrow_content_top(cfg);

	// A little lower than the sign-in fields, so the note still fits above
	// the keyboard on a 640 px panel.
	key_field = keys_make_field(keys_screen, cfg, "lastfm_api_key", top);
	secret_field = keys_make_field(keys_screen, cfg, "lastfm_shared_secret", top + 64);

	lv_obj_t *note = lv_label_create(keys_screen);
	lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(note, cfg->screen_width - 2 * cfg->padding);
	lv_obj_align(note, LV_ALIGN_TOP_LEFT, cfg->padding, top + 126);
	lv_obj_add_style(note, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(note, &font_ui_16, 0);
	lv_label_set_text(note, tr("lastfm_keys_note"));

	keys_keyboard = keyboard_create(keys_screen, cfg->screen_width, 316, key_field, NULL, "ok", keys_accept_cb, NULL);
	switcher_attach_back_gesture(keys_screen);
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *label, lv_event_cb_t cb, int x) {
	lv_obj_t *btn = lv_btn_create(parent);
	lv_obj_set_size(btn, LV_SIZE_CONTENT, 56);
	lv_obj_set_style_pad_hor(btn, 22, 0);
	lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_shadow_width(btn, 0, 0);
	lv_obj_set_style_border_width(btn, 0, 0);
	lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, x, -24);
	lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *text = lv_label_create(btn);
	lv_label_set_text(text, tr(label));
	lv_obj_set_style_text_font(text, &font_ui_20, 0);
	lv_obj_center(text);
	return btn;
}

static void build_page(gui_config_t *cfg) {
	page_screen = lv_obj_create(NULL);
	lv_obj_t *container = settingsrow_page(page_screen, cfg, "lastfm");

	setting_rows[0] = settingsrow_toggle(container, "lastfm_scrobble", &enabled_switch, enabled_cb);
	setting_rows[1] = settingsrow_add(container, "lastfm_account", &account_value, account_cb, NULL);
	setting_rows[2] = settingsrow_add(container, "lastfm_status", &status_value, NULL, NULL);
	setting_rows[3] = settingsrow_add(container, "lastfm_queued", &queued_value, NULL, NULL);
	keys_row = settingsrow_add(container, "lastfm_keys", &keys_value, keys_row_cb, NULL);

	// A user name or a message from Last.fm can be longer than the room beside
	// the row's name: one line, cut with an ellipsis.
	lv_obj_t *values[] = {account_value, status_value};
	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		lv_label_set_long_mode(values[i], LV_LABEL_LONG_DOT);
		lv_obj_set_style_max_width(values[i], lv_pct(60), 0);
		lv_obj_set_height(values[i], lv_font_get_line_height(&font_ui_24));
	}

	note_label = lv_label_create(container);
	lv_label_set_long_mode(note_label, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(note_label, lv_pct(100));
	lv_obj_add_style(note_label, &theme_style_text_dim, 0);
	lv_obj_set_style_text_font(note_label, &font_ui_22, 0);
	lv_label_set_text(note_label, tr("lastfm_note"));
	setting_rows[4] = note_label;

	build_guide(container, cfg);

	lv_obj_add_event_cb(page_screen, loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
	switcher_attach_back_gesture(page_screen);
}

static void done_cb(lv_event_t *e) {
	(void)e;
	if (switcher_back_drag_active()) {
		return;
	}
	lastfm_complete_login();
}

static void retry_cb(lv_event_t *e) {
	(void)e;
	if (switcher_back_drag_active()) {
		return;
	}
	lv_label_set_text(login_message, tr("lastfm_auth_note"));
	waiting_for_login = true;
	toast_busy("lastfm_state_signing_in");
	lastfm_begin_login();
}

static void build_login(gui_config_t *cfg) {
	login_screen = lv_obj_create(NULL);
	lv_obj_add_style(login_screen, &theme_style_screen, 0);

	settingsrow_title(login_screen, cfg, "lastfm_sign_in");
	int top = settingsrow_content_top(cfg);

	lv_obj_t *box = lv_obj_create(login_screen);
	lv_obj_set_size(box, cfg->screen_width - 2 * cfg->padding, LV_SIZE_CONTENT);
	lv_obj_align(box, LV_ALIGN_TOP_MID, 0, top + 20);
	lv_obj_set_style_bg_opa(box, 0, 0);
	lv_obj_set_style_border_width(box, 0, 0);
	lv_obj_set_style_pad_all(box, 0, 0);
	lv_obj_set_style_pad_row(box, 18, 0);
	lv_obj_set_scrollable(box, false);
	lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	login_message = lv_label_create(box);
	lv_label_set_long_mode(login_message, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(login_message, lv_pct(100));
	lv_obj_set_style_text_align(login_message, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_add_style(login_message, &theme_style_text, 0);
	lv_obj_set_style_text_font(login_message, &font_ui_20, 0);
	lv_label_set_text(login_message, tr("lastfm_auth_note"));

	// Fixed black on white with a quiet zone: a QR is an optical target, not
	// a themed decoration. The size follows the smaller of the panel and 200.
	int qr_size = cfg->screen_width - 2 * cfg->padding;
	if (qr_size > QR_MAX_SIZE) {
		qr_size = QR_MAX_SIZE;
	}
	login_qr = lv_qrcode_create(box);
	lv_qrcode_set_size(login_qr, qr_size);
	lv_qrcode_set_dark_color(login_qr, lv_color_black());
	lv_qrcode_set_light_color(login_qr, lv_color_white());
	lv_obj_set_style_border_color(login_qr, lv_color_white(), 0);
	lv_obj_set_style_border_width(login_qr, QR_QUIET, 0);
	lv_obj_set_style_bg_color(login_qr, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(login_qr, LV_OPA_COVER, 0);

	login_url = lv_label_create(box);
	lv_label_set_long_mode(login_url, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(login_url, lv_pct(100));
	lv_obj_set_style_text_align(login_url, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_font(login_url, &font_ui_18, 0);
	lv_label_set_text(login_url, "");

	login_countdown = lv_label_create(box);
	lv_obj_set_style_text_font(login_countdown, &font_ui_20, 0);
	lv_label_set_text(login_countdown, "");

	login_done = make_button(login_screen, "lastfm_auth_done", done_cb, -70);
	login_retry = make_button(login_screen, "lastfm_auth_retry", retry_cb, 70);
	set_visible(login_qr, false);
	set_visible(login_url, false);
	set_visible(login_retry, false);

	switcher_attach_back_gesture(login_screen);
}

static void login_finished(const lastfm_status_t *st) {
	if (!waiting_for_login) {
		return;
	}
	waiting_for_login = false;
	toast_busy_end();

	switch (st->login_result) {
	case LASTFM_LOGIN_OK:
		if (lv_screen_active() == login_screen) {
			back_btn_cb(NULL);
		}
		toast_success("signed_in");
		break;
	case LASTFM_LOGIN_NO_REPLY:
		toast_error("lastfm_no_reply");
		break;
	case LASTFM_LOGIN_EXPIRED:
		lv_label_set_text(login_message, tr("lastfm_auth_expired"));
		set_visible(login_qr, false);
		set_visible(login_url, false);
		set_visible(login_retry, true);
		break;
	case LASTFM_LOGIN_NOT_AUTHORIZED:
		lv_label_set_text(login_message, tr("lastfm_auth_not_authorized"));
		set_visible(login_retry, true);
		break;
	case LASTFM_LOGIN_REFUSED:
		toast_error(st->detail);
		break;
	}
}

// ---------------------------------------------------------------------------
// the timer: sign-in result, token and page refresh. What is playing is
// followed by src/system/library/playcount.c, not here.
// ---------------------------------------------------------------------------

static void tick_cb(lv_timer_t *t) {
	(void)t;
	lastfm_status_t st;
	lastfm_get_status(&st);

	if (st.auth_serial != seen_auth_serial) {
		seen_auth_serial = st.auth_serial;
		show_auth_url(st.auth_url);
		set_visible(login_retry, st.auth_url[0] == '\0');
	}
	if (lv_screen_active() == login_screen) {
		update_countdown(st.auth_seconds_left);
	}
	if (st.login_serial != seen_login_serial) {
		seen_login_serial = st.login_serial;
		login_finished(&st);
	}
	if (lv_screen_active() == page_screen) {
		refresh();
	}
}

void lastfmsettings_init(gui_config_t *cfg) {
	build_page(cfg);
	build_login(cfg);
	build_keys(cfg);

	lastfm_status_t st;
	lastfm_get_status(&st);
	seen_login_serial = st.login_serial;
	seen_auth_serial = st.auth_serial;
	refresh();

	lv_timer_create(tick_cb, TICK_MS, NULL);
}
