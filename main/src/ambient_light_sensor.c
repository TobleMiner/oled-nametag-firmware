#include "ambient_light_sensor.h"

#include <inttypes.h>

#include <driver/i2c.h>
#include <esp_log.h>

#include "event_bus.h"
#include "gui.h"
#include "menu.h"
#include "scheduler.h"
#include "veml3235sl.h"

#define VEML_I2C_BUS		I2C_NUM_0
#define UPDATE_INTERVAL_US	MS_TO_US(500)

static const char *TAG = "ambient light sensor";

static veml3235sl_t veml;
static scheduler_task_t veml_update_task;
static uint32_t ambient_light_level_mlux = 0;

gui_t *gui_root;

static menu_cb_f menu_cb;
static void *menu_cb_ctx;

static gui_container_t app_container;
static gui_label_t app_luxmeter_reading_label;
static char app_luxmeter_reading_text[64];

static button_event_handler_t button_event_handler;
static event_bus_handler_t sensor_event_handler;

static void ambient_light_sensor_update(void *ctx);
static void ambient_light_sensor_update(void *ctx) {
	int32_t brightness_mlux = veml3235sl_get_brightness_mlux(&veml, VEML3235SL_DETECT_WHITE);

	if (brightness_mlux >= 0) {
		ESP_LOGD(TAG, "Ambient light level: %.02f lux", brightness_mlux / 1000.0f);
		ambient_light_level_mlux = brightness_mlux;
		event_bus_notify("ambient_light_level", NULL);
	} else {
		ESP_LOGE(TAG, "Failed to update ambient light level: %"PRId32, brightness_mlux);
	}
	scheduler_schedule_task_relative(&veml_update_task, ambient_light_sensor_update, NULL, UPDATE_INTERVAL_US);
}

static bool on_button_event(const button_event_t *event, void *priv) {
	if (event->button == BUTTON_EXIT) {
		buttons_disable_event_handler(&button_event_handler);
		gui_element_set_hidden(&app_container.element, true);
		menu_cb(menu_cb_ctx);
		return true;
	}

	return false;
}

static void on_light_level_event(void *priv, void *data) {
	gui_lock(gui_root);
	snprintf(app_luxmeter_reading_text, sizeof(app_luxmeter_reading_text), "%.2f lux", ambient_light_sensor_get_light_level_mlux() / 1000.0f);
	gui_label_set_text(&app_luxmeter_reading_label, app_luxmeter_reading_text);
	gui_unlock(gui_root);
}

void ambient_light_sensor_init(gui_t *gui) {
	button_event_handler_multi_user_cfg_t button_event_cfg = {
		.base = {
			.cb = on_button_event
		},
		.multi = {
			.button_filter = (1 << BUTTON_EXIT),
			.action_filter = (1 << BUTTON_ACTION_RELEASE)
		}
	};

	gui_root = gui;

	gui_container_init(&app_container);
	gui_element_set_size(&app_container.element, 256, 64);
	gui_element_set_hidden(&app_container.element, true);
	gui_element_add_child(&gui->container.element, &app_container.element);

	gui_label_init(&app_luxmeter_reading_label, "No data received yet");
	gui_label_set_font_size(&app_luxmeter_reading_label, 15);
	gui_label_set_text_offset(&app_luxmeter_reading_label, 3, 0);
	gui_element_set_size(&app_luxmeter_reading_label.element, 200, 25);
	gui_element_add_child(&app_container.element, &app_luxmeter_reading_label.element);

	buttons_register_multi_button_event_handler(&button_event_handler, &button_event_cfg);
	event_bus_subscribe(&sensor_event_handler, "ambient_light_level", on_light_level_event, NULL);

	ESP_ERROR_CHECK(veml3235sl_init(&veml, VEML_I2C_BUS));
	scheduler_schedule_task_relative(&veml_update_task, ambient_light_sensor_update, NULL, UPDATE_INTERVAL_US);
}

uint32_t ambient_light_sensor_get_light_level_mlux(void) {
	return ambient_light_level_mlux;
}

int ambient_light_sensor_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	menu_cb = exit_cb;
	menu_cb_ctx = cb_ctx;

	gui_element_set_hidden(&app_container.element, false);
	gui_element_show(&app_container.element);
	buttons_enable_event_handler(&button_event_handler);

	return 0;
}
