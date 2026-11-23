#ifndef POWERMENU_H
#define POWERMENU_H

#include <stdbool.h>

#include "src/gui/shell/gui.h"

// What a long press on the power key brings up: shut down, restart, or back
// out. A short press still just toggles the screen.
void powermenu_init(gui_config_t *cfg);

void powermenu_show(void);
bool powermenu_is_open(void);

// Safe to call from the input thread: the panel is raised on the UI thread via
// lv_async_call rather than from underneath it.
void powermenu_request_show(void);

// Flushes everything and restarts the device, as the slider does. Used after
// Sonix updated itself; only logs on the host build.
void powermenu_reboot(void);

#endif /* POWERMENU_H */
