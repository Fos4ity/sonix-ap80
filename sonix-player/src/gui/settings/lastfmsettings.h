#ifndef LASTFMSETTINGS_H
#define LASTFMSETTINGS_H

#include "src/gui/shell/gui.h"

#include "lvgl/lvgl.h"

// The Last.fm page, reached from Music settings, and its sign-in screen.
void lastfmsettings_init(gui_config_t *cfg);

lv_obj_t *lastfmsettings_screen(void);

// The Last.fm tile on the Streaming page: the settings, or while there are
// no API keys, the guide to getting them.
void lastfmsettings_open(void);

#endif /* LASTFMSETTINGS_H */
