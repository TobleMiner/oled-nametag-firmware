#pragma once

#include "gui.h"
#include "menu.h"

void accept_all_the_cookies_init(gui_t *gui_root);
int accept_all_the_cookies_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);
