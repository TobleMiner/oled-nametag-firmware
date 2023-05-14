#pragma once

#include "gui.h"
#include "menu.h"

void bms_details_init(gui_t *gui_root);

int bms_details_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);
