#include "bms_details.h"

#include "battery_gauge.h"
#include "buttons.h"
#include "event_bus.h"

static gui_container_t app_container;

static gui_label_t title_label;

static gui_label_t voltage_label;
static char voltage_label_text[32];

static gui_label_t current_label;
static char current_label_text[32];

static gui_label_t soc_label;
static char soc_label_text[32];

static gui_label_t soh_label;
static char soh_label_text[32];

static gui_label_t time_to_empty_label;
static char time_to_empty_label_text[32];

static gui_label_t temperature_label;
static char temperature_label_text[32];

static gui_t *gui;

static event_bus_handler_t battery_gauge_event_handler;
static button_event_handler_t button_event_handler;

static menu_cb_f menu_cb = NULL;
static void *menu_cb_ctx;

static bool on_button_event(const button_event_t *event, void *priv) {
	if (event->button == BUTTON_EXIT) {
		buttons_disable_event_handler(&button_event_handler);
		gui_element_set_hidden(&app_container.element, true);
		menu_cb(menu_cb_ctx);
		return true;
	}

	return false;
}

static void battery_gauge_update_gui(gui_t *gui) {
	unsigned int voltage_mv = battery_gauge_get_voltage_mv();
	int current_ma = battery_gauge_get_current_ma();
	unsigned int soc = battery_gauge_get_soc_percent();
	unsigned int soh = battery_gauge_get_soh_percent();
	unsigned int time_to_empty = battery_gauge_get_time_to_empty_min();
	int temperature = battery_gauge_get_temperature_0_1degc();

	gui_lock(gui);
	snprintf(voltage_label_text, sizeof(voltage_label_text), "Voltage: %u mV", voltage_mv);
	gui_label_set_text(&voltage_label, voltage_label_text);

	snprintf(current_label_text, sizeof(current_label_text), "Current: %d mA", current_ma);
	gui_label_set_text(&current_label, current_label_text);

	snprintf(soc_label_text, sizeof(soc_label_text), "SoC: %u%%", soc);
	gui_label_set_text(&soc_label, soc_label_text);

	snprintf(soh_label_text, sizeof(soh_label_text), "SoH: %u%%", soh);
	gui_label_set_text(&soh_label, soh_label_text);

	snprintf(time_to_empty_label_text, sizeof(time_to_empty_label_text), "Time left: %u min", time_to_empty);
	gui_label_set_text(&time_to_empty_label, time_to_empty_label_text);

	snprintf(temperature_label_text, sizeof(temperature_label_text), "Temperature: %.1f °C", temperature / 10.f);
	gui_label_set_text(&temperature_label, temperature_label_text);

	gui_unlock(gui);
}

static void on_battery_gauge_event(void *priv, void *data) {
	gui_t *gui = priv;

	battery_gauge_update_gui(gui);
}

static void label_init(gui_label_t *label, unsigned int x, unsigned int y) {
	gui_label_init(label, "");
	gui_label_set_font_size(label, 10);
	gui_element_set_position(&label->element, x, y);
	gui_element_set_size(&label->element, 128, 12);
	gui_element_add_child(&app_container.element, &label->element);
}

void bms_details_init(gui_t *gui_root) {
	button_event_handler_multi_user_cfg_t button_event_cfg = {
		.base = {
			.cb = on_button_event
		},
		.multi = {
			.button_filter = (1 << BUTTON_EXIT),
			.action_filter = (1 << BUTTON_ACTION_RELEASE)
		}
	};

	gui = gui_root;

	gui_container_init(&app_container);
	gui_element_set_size(&app_container.element, 256, 64);
	gui_element_set_hidden(&app_container.element, true);
	gui_element_add_child(&gui->container.element, &app_container.element);

	label_init(&voltage_label, 1, 5);
	label_init(&current_label, 1, 5 + 14 * 1);
	label_init(&soc_label, 1, 5 + 14 * 2);
	label_init(&soh_label, 1, 5 + 14 * 3);

	label_init(&time_to_empty_label, 128, 5 + 14 * 0);
	label_init(&temperature_label, 128, 5 + 14 * 1);

	battery_gauge_update_gui(gui_root);

	event_bus_subscribe(&battery_gauge_event_handler, "battery_gauge", on_battery_gauge_event, gui_root);
	buttons_register_multi_button_event_handler(&button_event_handler, &button_event_cfg);
}

int bms_details_run(menu_cb_f exit_cb, void *cb_ctx) {
	menu_cb = exit_cb;
	menu_cb_ctx = cb_ctx;
	gui_element_set_hidden(&app_container.element, false);
	gui_element_show(&app_container.element);
	buttons_enable_event_handler(&button_event_handler);
	return 0;
}
