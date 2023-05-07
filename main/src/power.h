#pragma once

#include "menu.h"

void power_init(void);
void power_off(void);

int power_off_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);
