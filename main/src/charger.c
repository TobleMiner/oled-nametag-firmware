#include "charger.h"

#include <driver/gpio.h>
#include <esp_err.h>

#define CHARGER_GPIO_STAT1	 1
#define CHARGER_GPIO_STAT2	46

void charger_init(void) {
	const gpio_config_t gpio_cfg = {
		.pin_bit_mask = (1ULL << CHARGER_GPIO_STAT1) | (1ULL << CHARGER_GPIO_STAT2),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = true,
		.pull_down_en = false,
		.intr_type = GPIO_INTR_DISABLE
	};

	ESP_ERROR_CHECK(gpio_config(&gpio_cfg));
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
