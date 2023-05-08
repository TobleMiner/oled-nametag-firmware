#include "battery_gauge.h"

#include <driver/i2c.h>
#include <esp_log.h>

#include "event_bus.h"
#include "scheduler.h"
#include "bq27546.h"

#define GAUGE_I2C_BUS		I2C_NUM_0
#define UPDATE_INTERVAL_US	MS_TO_US(2000)

static const char *TAG = "battery gauge";

static bq27546_t bq_gauge;
static scheduler_task_t gauge_update_task;

static unsigned int battery_voltage_mv = 0;
static int battery_current_ma = 0;
static unsigned int battery_soc_percent = 0;

static void battery_gauge_update(void *ctx);
static void battery_gauge_update(void *ctx) {
	int voltage_mv = bq27546_get_voltage_mv(&bq_gauge);
	int current_ma;
	int soc_percent = bq27546_get_state_of_charge_percent(&bq_gauge);
	esp_err_t err;

	if (voltage_mv >= 0) {
		if (battery_voltage_mv != voltage_mv) {
			ESP_LOGI(TAG, "Battery voltage: %.2f V", voltage_mv / 1000.f);
		}
		battery_voltage_mv = voltage_mv;
	} else {
		ESP_LOGE(TAG, "Failed to read battery voltage: %d", voltage_mv);
	}

	if (soc_percent >= 0) {
		if (soc_percent != battery_soc_percent) {
			ESP_LOGI(TAG, "State of chage: %d%%", soc_percent);
		}
		if (battery_soc_percent >= 100) {
			battery_soc_percent = 0;
		} else {
			battery_soc_percent += 5;
		}
//		battery_soc_percent = soc_percent;
	} else {
		ESP_LOGE(TAG, "Failed to read state of charge: %d", soc_percent);
	}

	err = bq27546_get_current_ma(&bq_gauge, &current_ma);
	if (err) {
		ESP_LOGE(TAG, "Failed to read battery current: %d", err);
	} else {
		if (battery_current_ma != current_ma) {
			ESP_LOGI(TAG, "Battery current: %d mA", current_ma);
		}
		battery_current_ma = current_ma;
	}

	event_bus_notify("battery_gauge", NULL);
	scheduler_schedule_task_relative(&gauge_update_task, battery_gauge_update, NULL, UPDATE_INTERVAL_US);
}

void battery_gauge_init(void) {
	ESP_ERROR_CHECK(bq27546_init(&bq_gauge, GAUGE_I2C_BUS));
	scheduler_schedule_task_relative(&gauge_update_task, battery_gauge_update, NULL, UPDATE_INTERVAL_US);
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
