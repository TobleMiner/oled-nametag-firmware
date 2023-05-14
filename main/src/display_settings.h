#pragma once

#include "gui.h"
#include "menu.h"

void display_settings_init(gui_t *gui);

int display_settings_brightness_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);
