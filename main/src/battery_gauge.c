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

static void battery_gauge_update(void *ctx);
static void battery_gauge_update(void *ctx) {
	int voltage_mv = bq27546_get_voltage_mv(&bq_gauge);
	int current_ma;
	int soc_percent = bq27546_get_state_of_charge_percent(&bq_gauge);
	esp_err_t err;

	if (voltage_mv >= 0) {
		ESP_LOGI(TAG, "Battery voltage: %.2f V", voltage_mv / 1000.f);
	} else {
		ESP_LOGE(TAG, "Failed to read battery voltage: %d", voltage_mv);
	}

	if (soc_percent >= 0) {
		ESP_LOGI(TAG, "State of chage: %d%%", soc_percent);
	} else {
		ESP_LOGE(TAG, "Failed to read state of charge: %d", soc_percent);
	}

	err = bq27546_get_current_ma(&bq_gauge, &current_ma);
	if (err) {
		ESP_LOGE(TAG, "Failed to read battery current: %d", err);
	} else {
		ESP_LOGI(TAG, "Battery current: %d mA", current_ma);
	}

	event_bus_notify("battery_gauge", NULL);
	scheduler_schedule_task_relative(&gauge_update_task, battery_gauge_update, NULL, UPDATE_INTERVAL_US);
}

void battery_gauge_init(void) {
	ESP_ERROR_CHECK(bq27546_init(&bq_gauge, GAUGE_I2C_BUS));
	scheduler_schedule_task_relative(&gauge_update_task, battery_gauge_update, NULL, UPDATE_INTERVAL_US);
}
