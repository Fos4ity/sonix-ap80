#include "streaming.h"

#include "lvgl/lvgl.h"

#include "src/gui/shell/gridpage.h"
#include "src/gui/shell/icons.h"
#include "src/gui/settings/lastfmsettings.h"
#include "src/gui/streaming/audiobookshelfpage.h"
#include "src/gui/streaming/podcastpage.h"
#include "src/gui/streaming/qobuzpage.h"
#include "src/gui/streaming/radiopage.h"
#include "src/gui/shell/settingsrow.h"
#include "src/gui/streaming/subsonicpage.h"
#include "src/gui/streaming/tidalpage.h"
#include "src/gui/streaming/ytmpage.h"

lv_obj_t *streaming_screen;

static void streaming_loaded_cb(lv_event_t *e) {
	gridpage_hint_scroll(lv_event_get_user_data(e));
}

void streaming_init(gui_config_t *cfg) {
	qobuzpage_init(cfg);
	tidalpage_init(cfg);
	ytmpage_init(cfg);
	podcastpage_init(cfg);
	audiobookshelfpage_init(cfg);
	subsonicpage_init(cfg);

	const grid_entry_t entries[] = {
		{"tidal", &icon_menu_tidal, &tidal_screen, NULL},
		{"ytm", &icon_menu_ytmusic, &ytm_screen, NULL},
		{"qobuz", &icon_menu_qobuz, &qobuz_screen, NULL},
		{"radio", &icon_menu_radio, &radiopage_screen, NULL},
		{"podcasts", &icon_menu_podcast, &podcast_screen, NULL},
		{"audiobookshelf", &icon_menu_audiobooks, &audiobookshelf_screen, NULL},
		{"subsonic", &icon_menu_streaming, &subsonic_screen, NULL},
		{"lastfm", &icon_menu_lastfm, NULL, lastfmsettings_open},
	};

	// Two columns and three rows of tile, exactly like Music and Wireless, so a
	// tile here is the same size as a tile there. Five of them fill the first
	// two rows and the first cell of the third; the rest of it stays empty
	// rather than the tiles growing to fill it.
	lv_obj_t *grid =
		gridpage_build(streaming_screen, cfg, entries, (int)(sizeof(entries) / sizeof(entries[0])), 2, 3, true);
	// Eight tiles in a grid of six: the page scrolls to the last row, as Music
	// does, and shows its scrollbar for a moment when it is opened.
	gridpage_set_scrolling(grid);
	lv_obj_add_event_cb(streaming_screen, streaming_loaded_cb, LV_EVENT_SCREEN_LOADED, grid);
	gridpage_set_arrangeable(grid, "streaming", NULL);

	settingsrow_title(streaming_screen, cfg, "streaming");
}
