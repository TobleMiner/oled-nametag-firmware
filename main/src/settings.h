#pragma once

#include <stdbool.h>

void settings_init(void);

void settings_set_default_animation(const char *str);
char *settings_get_default_animation(void);

void settings_set_default_app(const char *app);
char *settings_get_default_app(void);

void settings_set_wlan_ap_psk(const char *app);
char *settings_get_wlan_ap_psk(void);

void settings_set_wlan_ap_enable(bool enable);
bool settings_get_wlan_ap_enable(void);
