#pragma once

#include <driver/i2c.h>
#include <esp_err.h>

typedef struct bq27546 {
	i2c_port_t port;
} bq27546_t;

esp_err_t bq27546_init(bq27546_t *bq, i2c_port_t port);

int bq27546_get_voltage_mv(bq27546_t *bq);
esp_err_t bq27546_get_current_ma(bq27546_t *bq, int *current_ma_out);
int bq27546_get_state_of_charge_percent(bq27546_t *bq);
