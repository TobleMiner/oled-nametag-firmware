#pragma once

#include "gui.h"
#include "menu.h"

typedef void (*charging_screen_power_on_cb_f)(void);

void charging_screen_init(gui_t *gui, charging_screen_power_on_cb_f cb);

void charging_screen_show(void);

int charging_screen_run(menu_cb_f exit_cb, void *cb_ctx);
