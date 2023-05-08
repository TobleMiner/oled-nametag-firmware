#pragma once

#include <stdint.h>

#include "menu.h"

void ambient_light_sensor_init(gui_t *gui);
void ambient_light_sensor_stop(void);
void ambient_light_sensor_start(void);

uint32_t ambient_light_sensor_get_light_level_mlux(void);

int ambient_light_sensor_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);
