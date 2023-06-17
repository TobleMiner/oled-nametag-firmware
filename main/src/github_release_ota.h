#pragma once

#include "gui.h"
#include "menu.h"

void github_release_ota_init(gui_t *gui_root);
int github_release_ota_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);
