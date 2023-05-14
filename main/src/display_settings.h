#pragma once

#include <stdbool.h>

#include "gui.h"
#include "menu.h"

void display_settings_init(gui_t *gui);

bool display_settings_is_adaptive_brightness_enabled(void);

int display_settings_brightness_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);
int display_settings_endisable_adaptive_brightness_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);
