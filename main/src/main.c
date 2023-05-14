#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "api.h"
#include "ambient_light_sensor.h"
#include "battery_gauge.h"
#include "bms_details.h"
#include "buttons.h"
#include "charger.h"
#include "charging_screen.h"
#include "display_settings.h"
#include "embedded_files.h"
#include "event_bus.h"
#include "flash.h"
#include "fonts.h"
#include "gifplayer.h"
#include "gui.h"
#include "i2c_bus.h"
#include "menutree.h"
#include "nvs.h"
#include "oled.h"
#include "pixelflut/pixelflut.h"
#include "power.h"
#include "scheduler.h"
#include "settings.h"
#include "webserver.h"
#include "wlan_settings.h"
#include "wlan.h"
#include "wlan_ap.h"

static const char *TAG = "main";

static pixelflut_t pixelflut;
static uint8_t gui_render_fb[256 * 64];

static inline unsigned int rgb_to_grayscale(const uint8_t *rgb) {
	return (rgb[0] + rgb[1] + rgb[2]) / 3;
}

static inline unsigned int rgb_pixel_to_grayscale(union fb_pixel px) {
	unsigned int red = px.color.color_bgr.red;
	unsigned int green = px.color.color_bgr.green;
	unsigned int blue = px.color.color_bgr.blue;
	unsigned int avg = (red + green + blue) / 3;
	return avg;
}

static void fb_convert(uint8_t *grayscale, const uint8_t *rgb888) {
	for (int y = 0; y < 64; y++) {
		for (int x = 0; x < 256 / 2; x++) {
			unsigned int int1 = rgb_to_grayscale(rgb888 + (y * 256 + x * 2 + 1) * 3);
			unsigned int int2 = rgb_to_grayscale(rgb888 + (y * 256 + x * 2 + 0) * 3);
			grayscale[y * 128 + x] = (int1 >> 4) | (int2 & 0xf0);
		}
	}
}

static void fb_convert_grayscale(uint8_t *stuffed_4bit, const uint8_t *grayscale) {
	for (int y = 0; y < 64; y++) {
		for (int x = 0; x < 256 / 2; x++) {
			unsigned int int1 = grayscale[y * 256 + x * 2 + 1];
			unsigned int int2 = grayscale[y * 256 + x * 2 + 0];
			stuffed_4bit[y * 128 + x] = (int1 >> 4) | (int2 & 0xf0);
		}
	}
}

gui_t gui;

static button_event_handler_t reset_button_event_handler;
static bool handle_reset_button_press(const button_event_t *event, void *priv) {
	ESP_LOGI(TAG, "Reset button pressed");
	esp_restart();
	return false;
}

TaskHandle_t main_task;

static void gui_request_render(const gui_t *gui) {
	xTaskNotifyGive(main_task);
}

const gui_ops_t gui_ops = {
	.request_render = gui_request_render
};

menu_t *menu;

void button_emulator_event_loop(void *arg) {
	uint8_t keybuf[3] = { 0 };
	while (1) {
		uint8_t byt;
		ssize_t ret = fread(&byt, 1, 1, stdin);
		if (ret > 0) {
			ESP_LOGI(TAG, "Read byte: 0x%02x", byt);
			keybuf[0] = byt;
			if (keybuf[2] == 0x1b && keybuf[1] == 0x5b) {
				switch(keybuf[0]) {
				case 0x41: //up
					buttons_emulate_press(BUTTON_UP, 100);
					break;
				case 0x42: //down
					buttons_emulate_press(BUTTON_DOWN, 100);
					break;
				case 0x44: //left
					buttons_emulate_press(BUTTON_EXIT, 100);
					break;
				case 0x43: //right
					buttons_emulate_press(BUTTON_ENTER, 100);
					break;
				}
			}
			memmove(&keybuf[1], &keybuf[0], 2);
		}
		vTaskDelay(1);
	}
}

void power_on_cb(void) {
	menu_show(menu);
}

static uint8_t oled_fb[8192] = { 0 };

void app_main(void)
{
	esp_err_t ret;

	// Ensure we keep the lights on
	power_init();

	// Initialize the display
	oled_init();

	main_task = xTaskGetCurrentTaskHandle();

	// Initialize event bus
	event_bus_init();

	// Setup scheduler
	scheduler_init();

	// Initialize charger readouts
	charger_init();

	// Initialize I2C bus
	ESP_ERROR_CHECK(i2c_bus_configure());
	i2c_detect(I2C_NUM_0);

	// Setup NVS
	nvs_init();

	// Setup settings in NVS
	settings_init();

	// Setup WLAN
	wlan_init();

	// Setup WLAN AP
	wlan_ap_init();

	// Mount main fat storage
	ESP_ERROR_CHECK(flash_fatfs_mount("flash", "/flash"));

	// Setup buttons
	buttons_init();
	const button_event_handler_single_user_cfg_t reset_button_cfg = {
		.base = {
			.cb = handle_reset_button_press,
		},
		.single = {
			.button = BUTTON_EXIT,
			.action = BUTTON_ACTION_HOLD,
			.min_hold_duration_ms = 3000,
		}
	};
	buttons_register_single_button_event_handler(&reset_button_event_handler, &reset_button_cfg);

	// Load fonts
	fonts_init();

	// Initialize GUI
	gui_init(&gui, NULL, &gui_ops);

	// Setup i2c bus UIs
	i2c_bus_init(&gui);

	// Setup gifplayer
	gifplayer_init(&gui);

	// Setup wifi settings
	wlan_settings_init(&gui);

	// Setup display settings
	display_settings_init(&gui);

	// Setup charging screen
	charging_screen_init(&gui, power_on_cb);

	// Initialize ambient light sensor
	ambient_light_sensor_init(&gui);

	// Initialize battery gauge
	battery_gauge_init();

	// Initialize BMS status app
	bms_details_init(&gui);

	// Setup menu
	menu = menutree_init(&gui.container, &gui);
	if (power_is_usb_connected()) {
		ESP_LOGI(TAG, "USB connected, switching to charging screen");
		power_off();
		charging_screen_show();
	} else {
		ESP_LOGI(TAG, "On battery, switching to main menu");
		menu_show(menu);
	}

	// Setup webserver
	httpd_t *httpd = webserver_preinit();
	api_init(httpd);
	webserver_init(httpd);

	// Setup pixelflut
	ESP_ERROR_CHECK(pixelflut_init(&pixelflut, 256, 64, 8192));

	// Start listening
	ESP_ERROR_CHECK(pixelflut_listen(&pixelflut));
	// Start polling input
	ESP_ERROR_CHECK(xTaskCreate(button_emulator_event_loop, "button_emulator_event_loop", 4096, NULL, 10, NULL) != pdPASS);

	bool slot = false;
	int render_ret = -1;
	while (1) {
		const gui_point_t render_size = {
			256,
			64
		};
		if (render_ret < 0) {
			ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		} else if (!render_ret) {
			ulTaskNotifyTake(pdTRUE, 0);
		} else {
			ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(render_ret));
		}
		gui_lock(&gui);
		render_ret = gui_render(&gui, gui_render_fb, 256, &render_size);
		gui_unlock(&gui);
		fb_convert_grayscale(oled_fb, gui_render_fb);
		slot = !slot;
		oled_write_image(oled_fb, slot ? 1 : 0);
		oled_show_image(slot ? 1 : 0);
	}
}
