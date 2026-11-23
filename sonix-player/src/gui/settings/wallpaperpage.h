#ifndef WALLPAPERPAGE_H
#define WALLPAPERPAGE_H

#include "lvgl/lvgl.h"
#include "src/gui/shell/gui.h"

extern lv_obj_t *wallpaperpage_screen;

void wallpaperpage_init(gui_config_t *cfg);

#endif // WALLPAPERPAGE_H
