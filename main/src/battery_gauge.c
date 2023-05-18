#include "battery_gauge.h"

#include <driver/i2c.h>
#include <esp_log.h>

#include "event_bus.h"
#include "power.h"
#include "scheduler.h"
#include "bq27546.h"

#define GAUGE_I2C_BUS			I2C_NUM_0
#define UPDATE_INTERVAL_US		MS_TO_US(2000)
#define UPDATE_INTERVAL_MAX_BACKOFF	4

static const char *TAG = "battery gauge";

static bq27546_t bq_gauge;
static scheduler_task_t gauge_update_task;
static int gauge_update_interval_exponent = 0;

static unsigned int battery_voltage_mv = 0;
static int battery_current_ma = 0;
static unsigned int battery_soc_percent = 0;
static unsigned int battery_soh_percent = 0;
static unsigned int battery_time_to_empty_min = 0;
static int battery_temperature_0_1degc = 0;
static bool battery_gauge_healthy = false;

#define POWEROFF_THRESHOLD_MV	2800
#define POWEROFF_SAMPLES	5

static unsigned int samples_below_poweroff_threshold = 0;

static void battery_gauge_update(void *ctx);
static void battery_gauge_schedule_update(bool success) {
	uint32_t update_deadline_us;

	if (success) {
		gauge_update_interval_exponent = 0;
		battery_gauge_healthy = true;
	} else {
		if (gauge_update_interval_exponent < UPDATE_INTERVAL_MAX_BACKOFF) {
			gauge_update_interval_exponent++;
		}
		battery_gauge_healthy = false;
	}

	update_deadline_us = UPDATE_INTERVAL_US << gauge_update_interval_exponent;
	scheduler_schedule_task_relative(&gauge_update_task, battery_gauge_update, NULL, update_deadline_us);
}

static void battery_gauge_update(void *ctx) {
	int voltage_mv = bq27546_get_voltage_mv(&bq_gauge);
	int current_ma;
	int soc_percent = bq27546_get_state_of_charge_percent(&bq_gauge);
	int soh_percent = bq27546_get_state_of_health_percent(&bq_gauge);
	int temperature_0_1k = bq27546_get_temperature_0_1k(&bq_gauge);
	int time_to_empty_min = bq27546_get_time_to_empty_min(&bq_gauge);
	esp_err_t err;
	bool parameter_get_success = true;

	if (voltage_mv >= 0) {
		if (battery_voltage_mv != voltage_mv) {
			ESP_LOGI(TAG, "Battery voltage: %.2f V", voltage_mv / 1000.f);
		}
		if (voltage_mv < POWEROFF_THRESHOLD_MV && !power_is_usb_connected()) {
			samples_below_poweroff_threshold += 1;
			if (samples_below_poweroff_threshold >= POWEROFF_SAMPLES) {
				power_off();
			}
		} else {
			samples_below_poweroff_threshold = 0;
		}
		battery_voltage_mv = voltage_mv;
	} else {
		ESP_LOGE(TAG, "Failed to read battery voltage: %d", voltage_mv);
		parameter_get_success = false;
	}

	if (soc_percent >= 0) {
		if (soc_percent != battery_soc_percent) {
			ESP_LOGI(TAG, "State of chage: %d%%", soc_percent);
		}
		battery_soc_percent = soc_percent;
	} else {
		ESP_LOGE(TAG, "Failed to read state of charge: %d", soc_percent);
		parameter_get_success = false;
	}

	if (soh_percent >= 0) {
		if (soh_percent != battery_soh_percent) {
			ESP_LOGI(TAG, "State of health: %d%%", soh_percent);
		}
		battery_soh_percent = soh_percent;
	} else {
		ESP_LOGE(TAG, "Failed to read state of health: %d", soh_percent);
		parameter_get_success = false;
	}

	if (temperature_0_1k >= 0) {
		int temperature_0_1degc = (int)temperature_0_1k - 2732;

		if (temperature_0_1degc != battery_temperature_0_1degc) {
			ESP_LOGI(TAG, "Temperature: %.1f degC", temperature_0_1degc / 10.f);
		}
		battery_temperature_0_1degc = temperature_0_1degc;
	} else {
		ESP_LOGE(TAG, "Failed to read state of health: %d", soh_percent);
		parameter_get_success = false;
	}

	if (time_to_empty_min >= 0) {
		if (time_to_empty_min != battery_time_to_empty_min) {
			ESP_LOGI(TAG, "Time to empty: %d minute%s", time_to_empty_min, time_to_empty_min == 1 ? "s" : "");
		}
		battery_time_to_empty_min = time_to_empty_min;
	} else {
		ESP_LOGE(TAG, "Failed to read state time to empty: %d", time_to_empty_min);
		parameter_get_success = false;
	}

	err = bq27546_get_current_ma(&bq_gauge, &current_ma);
	if (err) {
		ESP_LOGE(TAG, "Failed to read battery current: %d", err);
		parameter_get_success = false;
	} else {
		if (battery_current_ma != current_ma) {
			ESP_LOGI(TAG, "Battery current: %d mA", current_ma);
		}
		battery_current_ma = current_ma;
	}

	battery_gauge_schedule_update(parameter_get_success);
	event_bus_notify("battery_gauge", NULL);
}

void battery_gauge_init(void) {
	esp_err_t err;

	err = bq27546_init(&bq_gauge, GAUGE_I2C_BUS);

	if (err) {
		ESP_LOGE(TAG, "Failed to initialize battery gauge: %d", err);
	} else {
		battery_gauge_healthy = true;
		scheduler_schedule_task_relative(&gauge_update_task, battery_gauge_update, NULL, 0);
	}
}

void battery_gauge_stop(void) {
	scheduler_abort_task(&gauge_update_task);
}

void battery_gauge_start(void) {
	scheduler_schedule_task_relative(&gauge_update_task, battery_gauge_update, NULL, UPDATE_INTERVAL_US);
}

unsigned int battery_gauge_get_voltage_mv(void) {
	return battery_voltage_mv;
}

int battery_gauge_get_current_ma(void) {
	return battery_current_ma;
}

unsigned int battery_gauge_get_soc_percent(void) {
	return battery_soc_percent;
}

unsigned int battery_gauge_get_soh_percent(void) {
	return battery_soh_percent;
}

unsigned int battery_gauge_get_time_to_empty_min(void) {
	return battery_time_to_empty_min;
}

int battery_gauge_get_temperature_0_1degc(void) {
	return battery_temperature_0_1degc;
}

bool battery_gauge_is_healthy(void) {
	return battery_gauge_healthy;
}
