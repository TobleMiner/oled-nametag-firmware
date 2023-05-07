#include "charging_screen.h"

#include <stdbool.h>

#include "buttons.h"
#include "embedded_files.h"
#include "power.h"

static gui_container_t charging_status_container;
static gui_image_t battery_icon;
static gui_rectangle_t battery_soc_indicator;

static button_event_handler_t button_event_handler;

static charging_screen_power_on_cb_f power_on_cb;

static menu_cb_f menu_cb = NULL;
static void *menu_cb_ctx;

static bool on_button_event(const button_event_t *event, void *priv) {
	if (event->button == BUTTON_ENTER) {
		power_on();
		buttons_disable_event_handler(&button_event_handler);
		gui_element_set_hidden(&charging_status_container.element, true);
		if (menu_cb) {
			menu_cb(menu_cb_ctx);
		} else {
			power_on_cb();
		}
		return true;
	}

	return false;
}

void charging_screen_init(gui_t *gui, charging_screen_power_on_cb_f cb) {
	button_event_handler_multi_user_cfg_t button_event_cfg = {
		.base = {
			.cb = on_button_event
		},
		.multi = {
			.button_filter = (1 << BUTTON_ENTER),
			.action_filter = (1 << BUTTON_ACTION_RELEASE)
		}
	};

	power_on_cb = cb;

	gui_container_init(&charging_status_container);
	gui_element_set_position(&charging_status_container.element, 2, 64 - 10 - 2);
	gui_element_set_size(&charging_status_container.element, 75, 10);
	gui_element_set_hidden(&charging_status_container.element, true);
	gui_element_add_child(&gui->container.element, &charging_status_container.element);

	gui_image_init(&battery_icon, 21, 10, EMBEDDED_FILE_PTR(battery_21x10_raw));
	gui_element_set_position(&battery_icon.element, 31, 0);
	gui_element_add_child(&charging_status_container.element, &battery_icon.element);

	gui_rectangle_init(&battery_soc_indicator);
	gui_rectangle_set_filled(&battery_soc_indicator, true);
	gui_rectangle_set_color(&battery_soc_indicator, 255);
	gui_element_set_position(&battery_soc_indicator.element, 33, 2);
	gui_element_set_size(&battery_soc_indicator.element, 11, 6);
	gui_element_add_child(&charging_status_container.element, &battery_soc_indicator.element);

	buttons_register_multi_button_event_handler(&button_event_handler, &button_event_cfg);
}

void charging_screen_show(void) {
	gui_element_set_hidden(&charging_status_container.element, false);
	gui_element_show(&charging_status_container.element);
	buttons_enable_event_handler(&button_event_handler);
}

int charging_screen_run(menu_cb_f exit_cb, void *cb_ctx) {
	menu_cb = exit_cb;
	menu_cb_ctx = cb_ctx;
	charging_screen_show();
	return 0;
}
