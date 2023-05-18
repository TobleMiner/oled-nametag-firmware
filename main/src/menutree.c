#include "menutree.h"

#include <stddef.h>

#include <esp_log.h>

#include "ambient_light_sensor.h"
#include "battery_gauge.h"
#include "bms_details.h"
#include "display_settings.h"
#include "embedded_files.h"
#include "event_bus.h"
#include "gifplayer.h"
#include "i2c_bus.h"
#include "power.h"
#include "settings.h"
#include "util.h"
#include "wlan_ap.h"
#include "wlan_settings.h"
#include "wlan_station.h"

static const char *TAG = "menutree";

// Root
static gui_container_t menutree_root_gui_container;
static gui_container_t menutree_list_gui_container;
static gui_list_t menutree_root_gui_list;
static menu_entry_submenu_t menutree_root = {
	.base = {
		.name = "/",
		.parent = NULL,
		.gui_element = NULL
	},
	.gui_list = &menutree_root_gui_list
};

// Root menu - Applications
static gui_list_t menutree_applications_gui_list;
static gui_label_t menutree_applications_gui_label;
static menu_entry_submenu_t menutree_root_applications = {
	.base = {
		.name = "apps",
		.parent = &menutree_root,
		.gui_element = &menutree_applications_gui_label.element
	},
	.gui_list = &menutree_applications_gui_list
};

// Root menu - Settings
static gui_list_t menutree_settings_gui_list;
static gui_label_t menutree_settings_gui_label;
static menu_entry_submenu_t menutree_root_settings = {
	.base = {
		.name = "settings",
		.parent = &menutree_root,
		.gui_element = &menutree_settings_gui_label.element
	},
	.gui_list = &menutree_settings_gui_list
};

// Root menu - Power off
static gui_label_t menutree_power_off_gui_label;
static menu_entry_app_t menutree_root_power_off = {
	.base = {
		.name = NULL, // NULL to make sure poweroff is never stored as default app
		.parent = &menutree_root,
		.gui_element = &menutree_power_off_gui_label.element
	},
	.run = power_off_run
};

// Root menu - Applications - GIF player
static gui_label_t menutree_gifplayer_gui_label;
static menu_entry_app_t menutree_root_applications_gif_player = {
	.base = {
		.name = "gifplayer",
		.parent = &menutree_root_applications,
		.gui_element = &menutree_gifplayer_gui_label.element
	},
	.run = gifplayer_run
};

// Root menu - Applications - Ambient light meter
static gui_label_t menutree_ambient_light_meter_gui_label;
static menu_entry_app_t menutree_root_applications_ambient_light_meter = {
	.base = {
		.name = "ambient_light_meter",
		.parent = &menutree_root_applications,
		.gui_element = &menutree_ambient_light_meter_gui_label.element
	},
	.run = ambient_light_sensor_run
};

// Root menu - Applications - BMS status
static gui_label_t menutree_bms_status_gui_label;
static menu_entry_app_t menutree_root_applications_bms_status = {
	.base = {
		.name = "bms_status",
		.parent = &menutree_root_applications,
		.gui_element = &menutree_bms_status_gui_label.element
	},
	.run = bms_details_run
};

// Root menu - Settings - Display Settings
static gui_list_t menutree_display_settings_gui_list;
static gui_label_t menutree_display_settings_gui_label;
static menu_entry_submenu_t menutree_root_settings_display_settings = {
	.base = {
		.name = "display",
		.parent = &menutree_root_settings,
		.gui_element = &menutree_display_settings_gui_label.element
	},
	.gui_list = &menutree_display_settings_gui_list
};

// Root menu - Settings - WLAN Settings
static gui_list_t menutree_wlan_settings_gui_list;
static gui_label_t menutree_wlan_settings_gui_label;
static menu_entry_submenu_t menutree_root_settings_wlan_settings = {
	.base = {
		.name = "wlan",
		.parent = &menutree_root_settings,
		.gui_element = &menutree_wlan_settings_gui_label.element
	},
	.gui_list = &menutree_wlan_settings_gui_list
};

// Root menu - Settings - System Settings
static gui_list_t menutree_system_settings_gui_list;
static gui_label_t menutree_system_settings_gui_label;
static menu_entry_submenu_t menutree_root_settings_system_settings = {
	.base = {
		.name = "system",
		.parent = &menutree_root_settings,
		.gui_element = &menutree_system_settings_gui_label.element
	},
	.gui_list = &menutree_system_settings_gui_list
};

// Root menu - Settings - Display Settings - Brightness
static gui_label_t menutree_brightness_gui_label;
static menu_entry_app_t menutree_root_settings_display_settings_brightness = {
	.base = {
		.name = "brightness",
		.parent = &menutree_root_settings_display_settings,
		.gui_element = &menutree_brightness_gui_label.element
	},
	.run = display_settings_brightness_run
};

// Root menu - Settings - Display Settings - Enable/Disable adaptive brightness
static gui_label_t menutree_endisable_adaptive_brightness_gui_label;
static gui_marquee_t menutree_endisable_adaptive_brightness_gui_marquee;
static menu_entry_app_t menutree_root_settings_display_settings_endisable_adaptive_brightness = {
	.base = {
		.name = NULL,
		.parent = &menutree_root_settings_display_settings,
		.gui_element = &menutree_endisable_adaptive_brightness_gui_marquee.container.element
	},
	.keep_menu_visible = true,
	.run = display_settings_endisable_adaptive_brightness_run
};

// Root menu - Settings - WLAN Settings - AP info
static gui_label_t menutree_apinfo_gui_label;
static menu_entry_app_t menutree_root_settings_wlan_settings_ap_info = {
	.base = {
		.name = "apinfo",
		.parent = &menutree_root_settings_wlan_settings,
		.gui_element = &menutree_apinfo_gui_label.element
	},
	.run = wlan_settings_run
};

// Root menu - Settings - WLAN Settings - Enable/Disable AP
static gui_label_t menutree_endisable_ap_gui_label;
static menu_entry_app_t menutree_root_settings_wlan_settings_endisable_ap = {
	.base = {
		.name = "endisableap",
		.parent = &menutree_root_settings_wlan_settings,
		.gui_element = &menutree_endisable_ap_gui_label.element
	},
	.run = wlan_ap_endisable_run
};

// Root menu - Settings - WLAN Settings - Enable/Disable station
static gui_label_t menutree_endisable_station_gui_label;
static gui_marquee_t menutree_endisable_station_gui_marquee;
static menu_entry_app_t menutree_root_settings_wlan_settings_endisable_station = {
	.base = {
		.name = "endisablestation",
		.parent = &menutree_root_settings_wlan_settings,
		.gui_element = &menutree_endisable_station_gui_marquee.container.element
	},
	.run = wlan_station_endisable_run
};

// Root menu - Settings - System Settings - Disable I2C
static gui_label_t menutree_disable_i2c_gui_label;
static menu_entry_app_t menutree_root_settings_system_settings_disable_i2c = {
	.base = {
		.name = NULL,
		.parent = &menutree_root_settings_system_settings,
		.gui_element = &menutree_disable_i2c_gui_label.element
	},
	.run = i2c_bus_disable_run
};

// Vertical separator
static gui_rectangle_t menutree_vertical_separator;

// WLAN AP indicator
static gui_image_t menutree_wlan_ap_gui_image;

// WLAN station indicator
static gui_image_t menutree_wlan_station_gui_image;

// Battery indicator
static gui_image_t menutree_battery_gui_image;

// Battery SoC
static gui_rectangle_t menutree_battery_soc_gui_rect;

// Battery attention label
static gui_label_t menutree_battery_attention_label;

// Battery SoC text
static gui_label_t menutree_battery_soc_gui_label;
static char menutree_battery_soc_text[10];

// Application version text
static gui_label_t menutree_app_version_gui_label;

// UI update events
static event_bus_handler_t wlan_ap_event_handler;
static event_bus_handler_t wlan_station_event_handler;
static event_bus_handler_t battery_gauge_event_handler;
static event_bus_handler_t display_settings_event_handler;

static void apply_wlan_ap_state(void) {
	bool wlan_ap_active = wlan_ap_is_enabled();

	// Update enable/disable menu entry
	gui_label_set_text(&menutree_endisable_ap_gui_label,
			   wlan_ap_active ? "Disable AP" : "Enable AP");
	// Update indicator icon
	gui_element_set_hidden(&menutree_wlan_ap_gui_image.element, !wlan_ap_active);
}

static void on_wlan_ap_event(void *priv, void *data) {
	gui_t *gui = priv;

	gui_lock(gui);
	apply_wlan_ap_state();
	gui_unlock(gui);
}

static void apply_wlan_station_state(void) {
	bool wlan_station_enabled = wlan_station_is_enabled();

	// Update enable/disable menu entry
	gui_label_set_text(&menutree_endisable_station_gui_label,
			   wlan_station_enabled ? "Disable station" : "Enable station");
	// Update indicator icon
	gui_element_set_hidden(&menutree_wlan_station_gui_image.element, !wlan_station_enabled);
}

static void on_wlan_station_event(void *priv, void *data) {
	gui_t *gui = priv;

	gui_lock(gui);
	apply_wlan_station_state();
	gui_unlock(gui);
}

static void update_battery_status(gui_t *gui) {
	unsigned int soc = battery_gauge_get_soc_percent();
	bool is_healthy = battery_gauge_is_healthy();

	gui_lock(gui);
	snprintf(menutree_battery_soc_text, sizeof(menutree_battery_soc_text), "%u%%", soc);
	gui_label_set_text(&menutree_battery_soc_gui_label, menutree_battery_soc_text);
	gui_element_set_size(&menutree_battery_soc_gui_rect.element, DIV_ROUND(soc * 15, 100), 6);
	gui_element_set_hidden(&menutree_battery_attention_label.element, is_healthy);
	gui_unlock(gui);
}

static void on_battery_gauge_event(void *priv, void *data) {
	gui_t *gui = priv;

	update_battery_status(gui);
}

static void apply_display_settings_state(void) {
	bool adaptive_brightness_enabled = display_settings_is_adaptive_brightness_enabled();

	// Update enable/disable menu entry
	gui_label_set_text(&menutree_endisable_adaptive_brightness_gui_label,
			   adaptive_brightness_enabled ? "Disable adaptive brightness" : "Enable adaptive brightness");
}

static void on_display_settings_event(void *priv, void *data) {
	gui_t *gui = priv;

	gui_lock(gui);
	apply_display_settings_state();
	gui_unlock(gui);
}

#define MENU_LIST_WIDTH		144
#define MENU_LIST_HEIGHT	 64

static void gui_element_init(gui_container_t *root) {
	// Root
	gui_container_init(&menutree_root_gui_container);
	gui_element_add_child(&root->element, &menutree_root_gui_container.element);
	gui_element_set_size(&menutree_root_gui_container.element, 256, 64);

	// List container
	gui_container_init(&menutree_list_gui_container);
	gui_element_add_child(&menutree_root_gui_container.element, &menutree_list_gui_container.element);
	gui_element_set_position(&menutree_list_gui_container.element, 14, 0);
	gui_element_set_size(&menutree_list_gui_container.element, MENU_LIST_WIDTH, MENU_LIST_HEIGHT);

	// Root list
	gui_list_init(&menutree_root_gui_list);
	gui_element_set_size(&menutree_root_gui_list.container.element, MENU_LIST_WIDTH, MENU_LIST_HEIGHT);

	// Root menu - Applications
	gui_list_init(&menutree_applications_gui_list);
	gui_element_set_size(&menutree_applications_gui_list.container.element, MENU_LIST_WIDTH, MENU_LIST_HEIGHT);

	gui_label_init(&menutree_applications_gui_label, "Applications");
	gui_label_set_font_size(&menutree_applications_gui_label, 15);
	gui_label_set_text_offset(&menutree_applications_gui_label, 3, 1);
	gui_element_set_size(&menutree_applications_gui_label.element, 119, 22);

	// Root menu - Settings
	gui_list_init(&menutree_settings_gui_list);
	gui_element_set_size(&menutree_settings_gui_list.container.element, MENU_LIST_WIDTH, MENU_LIST_HEIGHT);

	gui_label_init(&menutree_settings_gui_label, "Settings");
	gui_label_set_font_size(&menutree_settings_gui_label, 15);
	gui_label_set_text_offset(&menutree_settings_gui_label, 3, 2);
	gui_element_set_size(&menutree_settings_gui_label.element, 119, 22);
	gui_element_set_position(&menutree_settings_gui_label.element, 0, 22);

	// Root menu - Power off
	gui_label_init(&menutree_power_off_gui_label, "Power off");
	gui_label_set_font_size(&menutree_power_off_gui_label, 15);
	gui_label_set_text_offset(&menutree_power_off_gui_label, 3, 1);
	gui_element_set_size(&menutree_power_off_gui_label.element, 119, 18);
	gui_element_set_position(&menutree_power_off_gui_label.element, 0, 44);

	// Root menu - Applications - GIF player
	gui_label_init(&menutree_gifplayer_gui_label, "GIF player");
	gui_label_set_font_size(&menutree_gifplayer_gui_label, 15);
	gui_label_set_text_offset(&menutree_gifplayer_gui_label, 3, 1);
	gui_element_set_size(&menutree_gifplayer_gui_label.element, 132, 22);

	// Root menu - Applications - Ambient light meter
	gui_label_init(&menutree_ambient_light_meter_gui_label, "Ambient light");
	gui_label_set_font_size(&menutree_ambient_light_meter_gui_label, 15);
	gui_label_set_text_offset(&menutree_ambient_light_meter_gui_label, 3, 2);
	gui_element_set_size(&menutree_ambient_light_meter_gui_label.element, 132, 22);
	gui_element_set_position(&menutree_ambient_light_meter_gui_label.element, 0, 22);

	// Root menu - Applications - BMS status
	gui_label_init(&menutree_bms_status_gui_label, "BMS status");
	gui_label_set_font_size(&menutree_bms_status_gui_label, 15);
	gui_label_set_text_offset(&menutree_bms_status_gui_label, 3, 2);
	gui_element_set_size(&menutree_bms_status_gui_label.element, 132, 20);
	gui_element_set_position(&menutree_bms_status_gui_label.element, 0, 44);

	// Root menu - Settings - Display Settings
	gui_list_init(&menutree_display_settings_gui_list);
	gui_element_set_size(&menutree_display_settings_gui_list.container.element, MENU_LIST_WIDTH, MENU_LIST_HEIGHT);

	gui_label_init(&menutree_display_settings_gui_label, "Display");
	gui_label_set_font_size(&menutree_display_settings_gui_label, 15);
	gui_label_set_text_offset(&menutree_display_settings_gui_label, 3, 2);
	gui_element_set_size(&menutree_display_settings_gui_label.element, 132, 22);

	// Root menu - Settings - WLAN Settings
	gui_list_init(&menutree_wlan_settings_gui_list);
	gui_element_set_size(&menutree_wlan_settings_gui_list.container.element, MENU_LIST_WIDTH, MENU_LIST_HEIGHT);

	gui_label_init(&menutree_wlan_settings_gui_label, "WLAN");
	gui_label_set_font_size(&menutree_wlan_settings_gui_label, 15);
	gui_label_set_text_offset(&menutree_wlan_settings_gui_label, 3, 3);
	gui_element_set_size(&menutree_wlan_settings_gui_label.element, 132, 20);
	gui_element_set_position(&menutree_wlan_settings_gui_label.element, 0, 22);

	// Root menu - Settings - System Settings
	gui_list_init(&menutree_system_settings_gui_list);
	gui_element_set_size(&menutree_system_settings_gui_list.container.element, MENU_LIST_WIDTH, MENU_LIST_HEIGHT);

	gui_label_init(&menutree_system_settings_gui_label, "System");
	gui_label_set_font_size(&menutree_system_settings_gui_label, 15);
	gui_label_set_text_offset(&menutree_system_settings_gui_label, 3, 2);
	gui_element_set_size(&menutree_system_settings_gui_label.element, 132, 22);
	gui_element_set_position(&menutree_system_settings_gui_label.element, 0, 42);

	// Root menu - Settings - Display Settings - Brightness
	gui_label_init(&menutree_brightness_gui_label, "Brightness");
	gui_label_set_font_size(&menutree_brightness_gui_label, 15);
	gui_label_set_text_offset(&menutree_brightness_gui_label, 3, 2);
	gui_element_set_size(&menutree_brightness_gui_label.element, 119, 22);

	// Root menu - Settings - Display Settings - Enable/Disable adaptive brightness
	gui_label_init(&menutree_endisable_adaptive_brightness_gui_label, "Enable auto brightness");
	gui_label_set_font_size(&menutree_endisable_adaptive_brightness_gui_label, 15);
	gui_label_set_text_offset(&menutree_endisable_adaptive_brightness_gui_label, 3, 2);
	gui_element_set_size(&menutree_endisable_adaptive_brightness_gui_label.element, 250, 22);

	gui_marquee_init(&menutree_endisable_adaptive_brightness_gui_marquee);
	gui_element_set_size(&menutree_endisable_adaptive_brightness_gui_marquee.container.element, 119, 22);
	gui_element_set_position(&menutree_endisable_adaptive_brightness_gui_marquee.container.element, 0, 22);
	gui_element_add_child(&menutree_endisable_adaptive_brightness_gui_marquee.container.element,
			      &menutree_endisable_adaptive_brightness_gui_label.element);

	// Root menu - Settings - WLAN Settings - AP info
	gui_label_init(&menutree_apinfo_gui_label, "AP info");
	gui_label_set_font_size(&menutree_apinfo_gui_label, 15);
	gui_label_set_text_offset(&menutree_apinfo_gui_label, 3, 2);
	gui_element_set_size(&menutree_apinfo_gui_label.element, 119, 22);

	// Root menu - Settings - WLAN Settings - Enable/Disable AP
	gui_label_init(&menutree_endisable_ap_gui_label, "Enable AP");
	gui_label_set_font_size(&menutree_endisable_ap_gui_label, 15);
	gui_label_set_text_offset(&menutree_endisable_ap_gui_label, 3, 3);
	gui_element_set_size(&menutree_endisable_ap_gui_label.element, 119, 22);
	gui_element_set_position(&menutree_endisable_ap_gui_label.element, 0, 22);

	// Root menu - Settings - WLAN Settings - Enable/Disable station
	gui_label_init(&menutree_endisable_station_gui_label, "Enable station");
	gui_label_set_font_size(&menutree_endisable_station_gui_label, 15);
	gui_label_set_text_offset(&menutree_endisable_station_gui_label, 3, 3);
	gui_element_set_size(&menutree_endisable_station_gui_label.element, 140, 22);

	gui_marquee_init(&menutree_endisable_station_gui_marquee);
	gui_element_set_size(&menutree_endisable_station_gui_marquee.container.element, 119, 22);
	gui_element_set_position(&menutree_endisable_station_gui_marquee.container.element, 0, 44);
	gui_element_add_child(&menutree_endisable_station_gui_marquee.container.element,
			      &menutree_endisable_station_gui_label.element);

	// Root menu - Settings - System Settings - Disable I2C
	gui_label_init(&menutree_disable_i2c_gui_label, "Disable I2C");
	gui_label_set_font_size(&menutree_disable_i2c_gui_label, 15);
	gui_label_set_text_offset(&menutree_disable_i2c_gui_label, 3, 3);
	gui_element_set_size(&menutree_disable_i2c_gui_label.element, 119, 22);

	// Vertical separator
	gui_rectangle_init(&menutree_vertical_separator);
	gui_rectangle_set_color(&menutree_vertical_separator, 255);
	gui_element_set_position(&menutree_vertical_separator.element, 158, 0);
	gui_element_set_size(&menutree_vertical_separator.element, 1, MENU_LIST_HEIGHT);
	gui_element_add_child(&menutree_root_gui_container.element, &menutree_vertical_separator.element);

	// WLAN AP indicator
	gui_image_init(&menutree_wlan_ap_gui_image, 15, 12, EMBEDDED_FILE_PTR(wlan_ap_15x12_raw));
	gui_element_set_position(&menutree_wlan_ap_gui_image.element, 163, 2);
	gui_element_add_child(&menutree_root_gui_container.element, &menutree_wlan_ap_gui_image.element);

	// WLAN station indicator
	gui_image_init(&menutree_wlan_station_gui_image, 20, 12, EMBEDDED_FILE_PTR(wlan_station_20x12_raw));
	gui_element_set_position(&menutree_wlan_station_gui_image.element, 183, 2);
	gui_element_add_child(&menutree_root_gui_container.element, &menutree_wlan_station_gui_image.element);

	// Battery indicator
	gui_image_init(&menutree_battery_gui_image, 21, 10, EMBEDDED_FILE_PTR(battery_21x10_raw));
	gui_element_set_position(&menutree_battery_gui_image.element, 233, 3);
	gui_element_add_child(&menutree_root_gui_container.element, &menutree_battery_gui_image.element);

	// Battery SoC
	gui_rectangle_init(&menutree_battery_soc_gui_rect);
	gui_rectangle_set_color(&menutree_battery_soc_gui_rect, 255);
	gui_rectangle_set_filled(&menutree_battery_soc_gui_rect, true);
	gui_element_set_position(&menutree_battery_soc_gui_rect.element, 235, 5);
	gui_element_set_size(&menutree_battery_soc_gui_rect.element, 7, 6);
	gui_element_add_child(&menutree_root_gui_container.element, &menutree_battery_soc_gui_rect.element);

	// Battery attention label
	gui_label_init(&menutree_battery_attention_label, "!");
	gui_label_set_font_size(&menutree_battery_attention_label, 8);
	gui_label_set_text_offset(&menutree_battery_attention_label, 1, 1);
	gui_element_set_position(&menutree_battery_attention_label.element, 233 + 21 / 2 - 3, 3);
	gui_element_set_size(&menutree_battery_attention_label.element, 3, 10);
	gui_element_set_hidden(&menutree_battery_attention_label.element, true);
	gui_element_add_child(&menutree_root_gui_container.element, &menutree_battery_attention_label.element);

	// Battery SoC text
	gui_label_init(&menutree_battery_soc_gui_label, "100%");
	gui_label_set_font_size(&menutree_battery_soc_gui_label, 8);
	gui_label_set_text_offset(&menutree_battery_soc_gui_label, -1, 0);
	gui_label_set_text_alignment(&menutree_battery_soc_gui_label, GUI_TEXT_ALIGN_END);
	gui_element_set_position(&menutree_battery_soc_gui_label.element, 206, 4);
	gui_element_set_size(&menutree_battery_soc_gui_label.element, 25, 8);
	gui_element_add_child(&menutree_root_gui_container.element, &menutree_battery_soc_gui_label.element);

	// Application version text
	gui_label_init(&menutree_app_version_gui_label, XSTRINGIFY(BADGE_APP_VERSION));
	gui_label_set_font_size(&menutree_app_version_gui_label, 8);
	gui_label_set_text_alignment(&menutree_app_version_gui_label, GUI_TEXT_ALIGN_CENTER);
	gui_element_set_position(&menutree_app_version_gui_label.element, 159, 64 - 11);
	gui_element_set_size(&menutree_app_version_gui_label.element, 256 - 159, 11);
	gui_element_add_child(&menutree_root_gui_container.element, &menutree_app_version_gui_label.element);
}


static void menu_element_init(void) {
	// Root
	menu_entry_submenu_init(&menutree_root);

	// Root menu - Applications
	menu_entry_submenu_init(&menutree_root_applications);
	menu_entry_submenu_add_entry(&menutree_root, &menutree_root_applications.base);

	// Root menu - Settings
	menu_entry_submenu_init(&menutree_root_settings);
	menu_entry_submenu_add_entry(&menutree_root, &menutree_root_settings.base);

	// Root menu - Power off
	menu_entry_app_init(&menutree_root_power_off);
	menu_entry_submenu_add_entry(&menutree_root, &menutree_root_power_off.base);

	// Root menu - Applications - GIF player
	menu_entry_app_init(&menutree_root_applications_gif_player);
	menu_entry_submenu_add_entry(&menutree_root_applications, &menutree_root_applications_gif_player.base);

	// Root menu - Applications - Ambient light meter
	menu_entry_app_init(&menutree_root_applications_ambient_light_meter);
	menu_entry_submenu_add_entry(&menutree_root_applications, &menutree_root_applications_ambient_light_meter.base);

	// Root menu - Applications - BMS status
	menu_entry_app_init(&menutree_root_applications_bms_status);
	menu_entry_submenu_add_entry(&menutree_root_applications, &menutree_root_applications_bms_status.base);

	// Root menu - Settings - Display Settings
	menu_entry_submenu_init(&menutree_root_settings_display_settings);
	menu_entry_submenu_add_entry(&menutree_root_settings, &menutree_root_settings_display_settings.base);

	// Root menu - Settings - WLAN Settings
	menu_entry_submenu_init(&menutree_root_settings_wlan_settings);
	menu_entry_submenu_add_entry(&menutree_root_settings, &menutree_root_settings_wlan_settings.base);

	// Root menu - Settings - System Settings
	menu_entry_submenu_init(&menutree_root_settings_system_settings);
	menu_entry_submenu_add_entry(&menutree_root_settings, &menutree_root_settings_system_settings.base);

	// Root menu - Settings - Display Settings - Brightness
	menu_entry_app_init(&menutree_root_settings_display_settings_brightness);
	menu_entry_submenu_add_entry(&menutree_root_settings_display_settings, &menutree_root_settings_display_settings_brightness.base);

	// Root menu - Settings - Display Settings - Enable/Disable adaptive brightness
	menu_entry_app_init(&menutree_root_settings_display_settings_endisable_adaptive_brightness);
	menu_entry_submenu_add_entry(&menutree_root_settings_display_settings, &menutree_root_settings_display_settings_endisable_adaptive_brightness.base);

	// Root menu - Settings - WLAN Settings - AP info
	menu_entry_app_init(&menutree_root_settings_wlan_settings_ap_info);
	menu_entry_submenu_add_entry(&menutree_root_settings_wlan_settings, &menutree_root_settings_wlan_settings_ap_info.base);

	// Root menu - Settings - WLAN Settings - Enable/Disable AP
	menu_entry_app_init(&menutree_root_settings_wlan_settings_endisable_ap);
	menu_entry_submenu_add_entry(&menutree_root_settings_wlan_settings, &menutree_root_settings_wlan_settings_endisable_ap.base);

	// Root menu - Settings - WLAN Settings - Enable/Disable statiob
	menu_entry_app_init(&menutree_root_settings_wlan_settings_endisable_station);
	menu_entry_submenu_add_entry(&menutree_root_settings_wlan_settings, &menutree_root_settings_wlan_settings_endisable_station.base);

	// Root menu - Settings - System Settings - Disable I2C
	menu_entry_app_init(&menutree_root_settings_system_settings_disable_i2c);
	menu_entry_submenu_add_entry(&menutree_root_settings_system_settings, &menutree_root_settings_system_settings_disable_i2c.base);
}

static menu_t menutree_menu;

static void on_app_entry(const menu_t *menu, const menu_entry_app_t *app, void *ctx) {
	if (!app->base.name) {
		ESP_LOGW(TAG, "App does not have a name, can't store state");
	}
	settings_set_default_app(app->base.name);
}

static void on_app_exit(const menu_t *menu, void *ctx) {
	settings_set_default_app(NULL);
}

static const menu_cbs_t menu_cbs = {
	.on_app_entry = on_app_entry,
	.on_app_exit = on_app_exit
};

menu_t *menutree_init(gui_container_t *gui_root, gui_t *gui) {
	char *default_app_name;

	gui_element_init(gui_root);
	menu_element_init();
	menu_init(&menutree_menu, &menutree_root, &menutree_root_gui_container.element);
	menutree_menu.cbs = &menu_cbs;
	menu_setup_gui(&menutree_menu, &menutree_list_gui_container);

	default_app_name = settings_get_default_app();
	ESP_LOGI(TAG, "Default application: %s", STR_NULL(default_app_name));
	if (default_app_name) {
		menu_entry_app_t *app;

		app = menu_find_app_by_name(&menutree_menu, default_app_name);
		if (app) {
			menu_set_app_active(&menutree_menu, app);
		}
		free(default_app_name);
	}

	event_bus_subscribe(&wlan_ap_event_handler, "wlan_ap", on_wlan_ap_event, gui);
	event_bus_subscribe(&wlan_station_event_handler, "wlan_station", on_wlan_station_event, gui);
	event_bus_subscribe(&battery_gauge_event_handler, "battery_gauge", on_battery_gauge_event, gui);
	event_bus_subscribe(&display_settings_event_handler, "display_settings", on_display_settings_event, gui);
	apply_wlan_ap_state();
	apply_wlan_station_state();
	apply_display_settings_state();
	update_battery_status(gui);

	return &menutree_menu;
}

