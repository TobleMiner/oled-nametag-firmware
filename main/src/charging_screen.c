#include "charging_screen.h"

#include <stdbool.h>
#include <stdint.h>

#include <esp_random.h>

#include "battery_gauge.h"
#include "buttons.h"
#include "embedded_files.h"
#include "event_bus.h"
#include "power.h"
#include "scheduler.h"
#include "util.h"

#define STATUS_MOVE_INTERVAL_US	MS_TO_US(5000)

static gui_container_t charging_status_container;
static gui_image_t battery_icon;
static gui_label_t battery_icon_attention_label;
static gui_rectangle_t battery_soc_gui_rect;

// Battery SoC text
static gui_label_t battery_soc_gui_label;
static char battery_soc_text[10];

static event_bus_handler_t battery_gauge_event_handler;
static button_event_handler_t button_event_handler;

static charging_screen_power_on_cb_f power_on_cb;

static menu_cb_f menu_cb = NULL;
static void *menu_cb_ctx;

static scheduler_task_t status_container_move_task;

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

static void update_battery_gauge_display(gui_t *gui) {
	unsigned int soc = battery_gauge_get_soc_percent();
	bool is_healthy = battery_gauge_is_healthy();

	gui_lock(gui);
	snprintf(battery_soc_text, sizeof(battery_soc_text), "%u%%", soc);
	gui_label_set_text(&battery_soc_gui_label, battery_soc_text);
	gui_element_set_size(&battery_soc_gui_rect.element, DIV_ROUND(soc * 15, 100), 6);
	gui_element_set_hidden(&battery_icon_attention_label.element, is_healthy);
	gui_unlock(gui);
}

static void on_battery_gauge_event(void *priv, void *data) {
	gui_t *gui = priv;

	update_battery_gauge_display(gui);
}

void charging_status_move_task(void *ctx);
void charging_status_move_task(void *ctx) {
	gui_t *gui = ctx;
	uint32_t container_pos_x = esp_random();
	uint32_t container_pos_y = esp_random();

	gui_lock(gui);
	container_pos_x %= 256 - charging_status_container.element.area.size.x;
	container_pos_y %= 64 - charging_status_container.element.area.size.y;
	gui_element_set_position(&charging_status_container.element, container_pos_x, container_pos_y);
	gui_unlock(gui);
	scheduler_schedule_task_relative(&status_container_move_task, charging_status_move_task, ctx, STATUS_MOVE_INTERVAL_US);
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

	gui_rectangle_init(&battery_soc_gui_rect);
	gui_rectangle_set_filled(&battery_soc_gui_rect, true);
	gui_rectangle_set_color(&battery_soc_gui_rect, 255);
	gui_element_set_position(&battery_soc_gui_rect.element, 33, 2);
	gui_element_set_size(&battery_soc_gui_rect.element, 11, 6);
	gui_element_add_child(&charging_status_container.element, &battery_soc_gui_rect.element);

	// Battery attention label
	gui_label_init(&battery_icon_attention_label, "!");
	gui_label_set_font_size(&battery_icon_attention_label, 8);
	gui_label_set_text_offset(&battery_icon_attention_label, 1, 1);
	gui_element_set_position(&battery_icon_attention_label.element, 31 + 21 / 2 - 3, 0);
	gui_element_set_size(&battery_icon_attention_label.element, 3, 10);
	gui_element_set_hidden(&battery_icon_attention_label.element, true);
	gui_element_add_child(&charging_status_container.element, &battery_icon_attention_label.element);

	// Battery SoC text
	gui_label_init(&battery_soc_gui_label, "100%");
	gui_label_set_font_size(&battery_soc_gui_label, 8);
	gui_label_set_text_offset(&battery_soc_gui_label, -1, 1);
	gui_label_set_text_alignment(&battery_soc_gui_label, GUI_TEXT_ALIGN_END);
	gui_element_set_position(&battery_soc_gui_label.element, 0, 0);
	gui_element_set_size(&battery_soc_gui_label.element, 28, 9);
	gui_element_add_child(&charging_status_container.element, &battery_soc_gui_label.element);

	event_bus_subscribe(&battery_gauge_event_handler, "battery_gauge", on_battery_gauge_event, gui);
	buttons_register_multi_button_event_handler(&button_event_handler, &button_event_cfg);
	scheduler_task_init(&status_container_move_task);
	scheduler_schedule_task_relative(&status_container_move_task, charging_status_move_task, gui, STATUS_MOVE_INTERVAL_US);

	update_battery_gauge_display(gui);
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
