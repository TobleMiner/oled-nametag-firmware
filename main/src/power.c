#include "power.h"

#include "driver/gpio.h"

#define GPIO_PWR_EN	35

void power_init(void) {
	gpio_set_direction(GPIO_PWR_EN, GPIO_MODE_OUTPUT);
	gpio_set_level(GPIO_PWR_EN, 1);
}

void power_off(void) {
	gpio_set_level(GPIO_PWR_EN, 0);
}

int power_off_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	power_off();
	return 1;
}
