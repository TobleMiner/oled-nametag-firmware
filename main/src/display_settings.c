#include "display_settings.h"

#include <stdint.h>
#include <stdio.h>

#include <esp_log.h>

#include "ambient_light_sensor.h"
#include "buttons.h"
#include "event_bus.h"
#include "oled.h"
#include "settings.h"
#include "util.h"

#define MIN_AMBIENT_LIGHT_LEVEL_SAMPLES 5

#define AMBIENT_LIGHT_HYSTERESIS_MLUX	10000

static const uint32_t display_brightness_mapping_mlux[] = {
	 20000,
	 32000,
	 44000,
	 66000,
	 78000,
	 90000,
	102000,
	114000,
	126000,
	138000,
	150000,
	162000,
	174000,
	186000,
	198000,
	210000
};

static const char *TAG = "display settings";

static gui_t *gui_root;

static menu_cb_f menu_cb;
static void *menu_cb_ctx;

static button_event_handler_t button_event_handler;
static event_bus_handler_t display_event_handler;
static event_bus_handler_t ambient_light_event_handler;

static gui_container_t display_brightness_container;
static gui_label_t display_brightness_label;
static char display_brightness_text[32];
static gui_rectangle_t display_brightness_slider;
static gui_rectangle_t display_brightness_knob;

static unsigned int ambient_light_level_samples = 0;
static uint32_t ambient_light_level_filtered_mlux;

static bool adaptive_brightness_control_enabled;

static void update_display_brightness_info(gui_t *gui) {
	unsigned int brightness = oled_get_brightness();
	const gui_area_t *slider_area;
	const gui_area_t *knob_area;
	unsigned int available_width;
	unsigned int pos_x;

	gui_lock(gui);
	snprintf(display_brightness_text, sizeof(display_brightness_text), "Brightness: %u", brightness);
	gui_label_set_text(&display_brightness_label, display_brightness_text);

	slider_area = &display_brightness_slider.element.area;
	knob_area = &display_brightness_knob.element.area;
	available_width = slider_area->size.x - knob_area->size.x - 4;
	pos_x = slider_area->position.x + DIV_ROUND(available_width * brightness, 15) + 2;
	gui_element_set_position(&display_brightness_knob.element, pos_x, 30);
	gui_unlock(gui);
}

static void on_display_event(void *priv, void *data) {
	update_display_brightness_info((gui_t *)priv);
}

static void on_ambient_light_level_event(void *priv, void *data) {
	uint32_t light_level_mlux = ambient_light_sensor_get_light_level_mlux();

	if (ambient_light_level_samples == 0) {
		// Take on first sample without any filtering
		ambient_light_level_filtered_mlux = light_level_mlux;
	} else {
		// Simple IIR low pass filter
		ambient_light_level_filtered_mlux =
			DIV_ROUND(ambient_light_level_filtered_mlux * 3, 4) +
			DIV_ROUND(light_level_mlux * 1, 4);
	}

	ESP_LOGI(TAG, "Current average brightness: %.2f lux", ambient_light_level_filtered_mlux / 1000.f);
	if (ambient_light_level_samples < MIN_AMBIENT_LIGHT_LEVEL_SAMPLES) {
		ambient_light_level_samples++;
	} else if (adaptive_brightness_control_enabled) {
		// Start controlling display brightness once we have enough samples
		unsigned int brightness = oled_get_brightness();
		uint32_t upper_brightness_bound_mlux;

		if (brightness >= ARRAY_SIZE(display_brightness_mapping_mlux)) {
			brightness = ARRAY_SIZE(display_brightness_mapping_mlux) - 1;
		}

		upper_brightness_bound_mlux = display_brightness_mapping_mlux[brightness];
		if (light_level_mlux > upper_brightness_bound_mlux + AMBIENT_LIGHT_HYSTERESIS_MLUX) {
			ESP_LOGI(TAG, "Adjusting display brightness up");
			brightness++;
			oled_set_brightness(brightness);
		} else if (brightness > 0) {
			uint32_t lower_brightness_bound_lux = display_brightness_mapping_mlux[brightness - 1];

			if (light_level_mlux < lower_brightness_bound_lux) {
				ESP_LOGI(TAG, "Adjusting display brightness down");
				brightness--;
				oled_set_brightness(brightness);
			}
		}
	}
}

static bool on_button_event(const button_event_t *event, void *priv) {
	if (event->button == BUTTON_EXIT) {
		buttons_disable_event_handler(&button_event_handler);
		gui_element_set_hidden(&display_brightness_container.element, true);
		menu_cb(menu_cb_ctx);
		return true;
	}

	if (event->button == BUTTON_UP) {
		unsigned int brightness = oled_get_brightness();

		if (brightness < 15) {
			brightness++;
		}
		oled_set_brightness(brightness);
		settings_set_display_brightness(brightness);
		return true;
	}

	if (event->button == BUTTON_DOWN) {
		unsigned int brightness = oled_get_brightness();

		if (brightness > 0) {
			brightness--;
		}
		oled_set_brightness(brightness);
		settings_set_display_brightness(brightness);
		return true;
	}

	return false;
}

static void apply_adaptive_brightness_control(void) {
	if (!adaptive_brightness_control_enabled) {
		oled_set_brightness(settings_get_display_brightness());
	}
}

void display_settings_init(gui_t *gui) {
	button_event_handler_multi_user_cfg_t button_event_cfg = {
		.base = {
			.cb = on_button_event,
		},
		.multi = {
			.button_filter = (1 << BUTTON_EXIT) | (1 << BUTTON_UP) | (1 << BUTTON_DOWN),
			.action_filter = (1 << BUTTON_ACTION_RELEASE)
		}
	};

	gui_root = gui;

	gui_container_init(&display_brightness_container);
	gui_element_set_size(&display_brightness_container.element, 256, 64);
	gui_element_set_hidden(&display_brightness_container.element, true);
	gui_element_add_child(&gui->container.element, &display_brightness_container.element);

	gui_label_init(&display_brightness_label, "Brightness:");
	gui_label_set_font_size(&display_brightness_label, 13);
	gui_element_set_size(&display_brightness_label.element, 180, 18);
	gui_element_set_position(&display_brightness_label.element, 1, 6);
	gui_element_add_child(&display_brightness_container.element, &display_brightness_label.element);

	gui_rectangle_init(&display_brightness_slider);
	gui_rectangle_set_color(&display_brightness_slider, 255);
	gui_element_set_position(&display_brightness_slider.element, 15, 32);
	gui_element_set_size(&display_brightness_slider.element, 225, 12);
	gui_element_add_child(&display_brightness_container.element, &display_brightness_slider.element);

	gui_rectangle_init(&display_brightness_knob);
	gui_rectangle_set_color(&display_brightness_knob, 255);
	gui_rectangle_set_filled(&display_brightness_knob, true);
	gui_element_set_position(&display_brightness_knob.element, 17, 30);
	gui_element_set_size(&display_brightness_knob.element, 8, 16);
	gui_element_add_child(&display_brightness_container.element, &display_brightness_knob.element);

	adaptive_brightness_control_enabled = settings_get_adaptive_display_brightness_enable();
	apply_adaptive_brightness_control();

	buttons_register_multi_button_event_handler(&button_event_handler, &button_event_cfg);
	event_bus_subscribe(&display_event_handler, "display", on_display_event, gui);
	event_bus_subscribe(&ambient_light_event_handler, "ambient_light_level", on_ambient_light_level_event, gui);
}

int display_settings_brightness_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	menu_cb = exit_cb;
	menu_cb_ctx = cb_ctx;
	update_display_brightness_info(gui_root);
	gui_element_set_hidden(&display_brightness_container.element, false);
	gui_element_show(&display_brightness_container.element);
	buttons_enable_event_handler(&button_event_handler);

	return 0;
}

int display_settings_endisable_adaptive_brightness_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	adaptive_brightness_control_enabled = !adaptive_brightness_control_enabled;
	settings_set_adaptive_display_brightness_enable(adaptive_brightness_control_enabled);
	apply_adaptive_brightness_control();
	event_bus_notify("display_settings", NULL);
	return 1;
}

bool display_settings_is_adaptive_brightness_enabled() {
	return adaptive_brightness_control_enabled;
}
