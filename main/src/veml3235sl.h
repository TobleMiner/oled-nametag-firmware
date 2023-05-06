#pragma once

#include <driver/i2c.h>
#include <esp_err.h>

typedef enum veml3235sl_gain {
	VEML3235SL_GAIN_X1,
	VEML3235SL_GAIN_X2,
	VEML3235SL_GAIN_X4
} veml3235sl_gain_t;

typedef enum veml3235sl_integration_time {
	VEML3235SL_INTEGRATION_TIME_50MS,
	VEML3235SL_INTEGRATION_TIME_100MS,
	VEML3235SL_INTEGRATION_TIME_200MS,
	VEML3235SL_INTEGRATION_TIME_400MS,
	VEML3235SL_INTEGRATION_TIME_800MS
} veml3235sl_integration_time_t;

typedef enum veml3235sl_detection_mode {
	 VEML3235SL_DETECT_WHITE,
	 VEML3235SL_DETECT_ALS
} veml3235sl_detection_mode_t;

typedef struct veml3235sl {
	i2c_port_t port;
	veml3235sl_gain_t gain;
	veml3235sl_integration_time_t integration_time;
} veml3235sl_t;

esp_err_t veml3235sl_init(veml3235sl_t *veml, i2c_port_t port);

int32_t veml3235sl_get_brightness_mlux(veml3235sl_t *veml, veml3235sl_detection_mode_t mode);
