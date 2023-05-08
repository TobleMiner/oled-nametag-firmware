#include "charger.h"

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>

#include "event_bus.h"
#include "scheduler.h"

#define CHARGER_GPIO_STAT1	 1
#define CHARGER_GPIO_STAT2	46

#define UPDATE_INTERVAL_US	MS_TO_US(100)

static const char *TAG = "battery charger";

static scheduler_task_t charger_update_task;

static bool battery_charging = false;
static bool battery_charging_finished = false;

static void battery_charger_update(void *ctx);
static void battery_charger_update(void *ctx) {
	bool new_battery_charging = charger_is_charging();
	bool new_battery_charging_finished = charger_is_charging();

	if (new_battery_charging != battery_charging) {
		if (new_battery_charging) {
			ESP_LOGI(TAG, "Battery now charging");
		}
		battery_charging = new_battery_charging;

	}
	if (new_battery_charging_finished != battery_charging_finished) {
		if (new_battery_charging_finished) {
			ESP_LOGI(TAG, "Battery charging finished");
		}
		battery_charging_finished = new_battery_charging_finished;
	}
	event_bus_notify("battery_charger", NULL);
	scheduler_schedule_task_relative(&charger_update_task, battery_charger_update, NULL, UPDATE_INTERVAL_US);
}

void charger_init(void) {
	const gpio_config_t gpio_cfg = {
		.pin_bit_mask = (1ULL << CHARGER_GPIO_STAT1) | (1ULL << CHARGER_GPIO_STAT2),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = true,
		.pull_down_en = false,
		.intr_type = GPIO_INTR_DISABLE
	};

	ESP_ERROR_CHECK(gpio_config(&gpio_cfg));
	scheduler_schedule_task_relative(&charger_update_task, battery_charger_update, NULL, UPDATE_INTERVAL_US);
}

bool charger_is_charging(void) {
	return !gpio_get_level(CHARGER_GPIO_STAT1);
}

bool charger_has_charging_finished(void) {
	if (charger_is_charging()) {
		return false;
	}

	return !gpio_get_level(CHARGER_GPIO_STAT2);
}
