#pragma once

void settings_init(void);

void settings_set_default_animation(const char *str);
char *settings_get_default_animation(void);

void settings_set_default_app(const char *app);
char *settings_get_default_app(void);
