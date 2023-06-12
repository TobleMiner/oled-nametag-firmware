#include "power.h"

#include <driver/gpio.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>

#include "charging_screen.h"

#define GPIO_PWR_EN		35
#define GPIO_USB_DET		 4
#define ADC_USB_DET	 	ADC_UNIT_1
#define ADC_CHANNEL_USB_DET	ADC_CHANNEL_3

#ifndef GPIO_USB_DET
static const char *TAG = "power";

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t adc_cal_handle;
#endif

static button_event_handler_t shutdown_button_event_handler;

static bool handle_shutdown_button_press(const button_event_t *event, void *priv) {
	power_off();

	return true;
}

void power_early_init(void) {
#ifndef GPIO_USB_DET
	const adc_oneshot_unit_init_cfg_t adc_config = {
		.unit_id = ADC_USB_DET,
		.ulp_mode = ADC_ULP_MODE_DISABLE,
	};
	const adc_oneshot_chan_cfg_t adc_channel_config = {
		.bitwidth = ADC_BITWIDTH_DEFAULT,
		.atten = ADC_ATTEN_DB_11,
	};
	const adc_cali_curve_fitting_config_t adc_cal_config = {
		.unit_id = ADC_USB_DET,
		.atten = ADC_ATTEN_DB_11,
		.bitwidth = ADC_BITWIDTH_DEFAULT,
	};
#endif
	gpio_set_direction(GPIO_PWR_EN, GPIO_MODE_OUTPUT);
	power_on();

#ifdef GPIO_USB_DET
	gpio_set_direction(GPIO_USB_DET, GPIO_MODE_INPUT);
#else
	ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_config, &adc_handle));
	ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_USB_DET, &adc_channel_config));
	ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&adc_cal_config, &adc_cal_handle));
#endif
}

void power_late_init(void) {
	const button_event_handler_single_user_cfg_t shutdown_button_cfg = {
		.base = {
			.cb = handle_shutdown_button_press,
		},
		.single = {
			.button = BUTTON_EXIT,
			.action = BUTTON_ACTION_HOLD,
			.min_hold_duration_ms = 5000,
		}
	};

	buttons_register_single_button_event_handler(&shutdown_button_event_handler, &shutdown_button_cfg);
	buttons_enable_event_handler(&shutdown_button_event_handler);
}

void power_on(void) {
	gpio_set_level(GPIO_PWR_EN, 1);
}

void power_off(void) {
	gpio_set_level(GPIO_PWR_EN, 0);
}

int power_off_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	power_off();

	return charging_screen_run(exit_cb, cb_ctx);
}

bool power_is_usb_connected(void) {
#ifndef GPIO_USB_DET
	esp_err_t err;
	int value_raw, value_mv, vbus_mv;

	err = adc_oneshot_read(adc_handle, ADC_CHANNEL_USB_DET, &value_raw);
	if (err) {
		ESP_LOGE(TAG, "Failed to read USB voltage: %d", err);
		return false;
	}

	err = adc_cali_raw_to_voltage(adc_cal_handle, value_raw, &value_mv);
	if (err) {
		ESP_LOGE(TAG, "Failed to convert raw USB voltage reading to mV: %d", err);
		return false;
	}

	vbus_mv = value_mv * 3;
	ESP_LOGI(TAG, "VBUS: %d mV", vbus_mv);
	return vbus_mv > 4000;
#else
	return !!gpio_get_level(GPIO_USB_DET);
#endif
}
