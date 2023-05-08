#pragma once

#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <driver/i2c.h>
#include <esp_err.h>

#include "gui.h"
#include "menu.h"

#define I2C_ADDRESS_SET(name) uint8_t name[16] = { 0 }

#define I2C_ADDRESS_SET_CONTAINS(set, id) \
	(!!((set)[(id) / 8] & (1 << (id % 8))))

#define I2C_ADDRESS_SET_SET(set, id) \
	(set)[(id) / 8] |= (1 << (id % 8))

#define I2C_ADDRESS_SET_CLEAR(set, id) \
	(set)[(id) / 8] &= ~(1 << (id % 8))

typedef uint8_t* i2c_address_set_t;

void i2c_bus_init(gui_t *gui);

esp_err_t i2c_bus_configure(void);

esp_err_t i2c_bus_scan(i2c_port_t i2c_port, i2c_address_set_t addr);
void i2c_detect(i2c_port_t i2c_port);

int i2c_bus_disable_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);
