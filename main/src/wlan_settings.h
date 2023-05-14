#pragma once

#include "gui.h"
#include "menu.h"

void wlan_settings_init(gui_t *gui);

int wlan_settings_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);

int wlan_ap_endisable_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);

int wlan_station_endisable_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);
