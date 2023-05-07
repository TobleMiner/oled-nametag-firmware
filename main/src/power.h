#pragma once

#include <stdbool.h>

#include "menu.h"

void power_init(void);
void power_on(void);
void power_off(void);
bool power_is_usb_connected(void);

int power_off_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);
