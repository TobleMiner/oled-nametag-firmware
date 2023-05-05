#include "menutree.h"

#include "embedded_files.h"
#include "gifplayer.h"

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
static gui_image_t menutree_applications_gui_image;
static menu_entry_submenu_t menutree_root_applications = {
	.base = {
		.name = "apps",
		.parent = &menutree_root,
		.gui_element = &menutree_applications_gui_image.element
	},
	.gui_list = &menutree_applications_gui_list
};

// Root menu - Settings
static gui_list_t menutree_settings_gui_list;
static gui_image_t menutree_settings_gui_image;
static menu_entry_submenu_t menutree_root_settings = {
	.base = {
		.name = "settings",
		.parent = &menutree_root,
		.gui_element = &menutree_settings_gui_image.element
	},
	.gui_list = &menutree_settings_gui_list
};

// Root menu - Power off
static gui_image_t menutree_power_off_gui_image;
static menu_entry_app_t menutree_root_power_off = {
	.base = {
		.name = "poweroff",
		.parent = &menutree_root,
		.gui_element = &menutree_power_off_gui_image.element
	}
};

// Root menu - Applications - GIF player
static gui_image_t menutree_gifplayer_gui_image;
static menu_entry_app_t menutree_root_applications_gif_player = {
	.base = {
		.name = "gifplayer",
		.parent = &menutree_root_applications,
		.gui_element = &menutree_gifplayer_gui_image.element
	},
	.run = gifplayer_run
};

// Root menu - Settings - WLAN Settings
static gui_image_t menutree_wlan_settings_gui_image;
static menu_entry_app_t menutree_root_settings_wlan_settings = {
	.base = {
		.name = "wlan",
		.parent = &menutree_root_settings,
		.gui_element = &menutree_wlan_settings_gui_image.element
	}
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
//	gui_element_add_child(&menutree_root_gui_container.element, &menutree_root_gui_list.container.element);
	gui_element_set_size(&menutree_root_gui_list.container.element, MENU_LIST_WIDTH, MENU_LIST_HEIGHT);

	// Root menu - Applications
	gui_list_init(&menutree_applications_gui_list);
	gui_element_set_size(&menutree_applications_gui_list.container.element, MENU_LIST_WIDTH, MENU_LIST_HEIGHT);
	gui_image_init(&menutree_applications_gui_image, 119, 22, EMBEDDED_FILE_PTR(applications_119x22_raw));

	// Root menu - Settings
	gui_list_init(&menutree_settings_gui_list);
	gui_element_set_size(&menutree_settings_gui_list.container.element, MENU_LIST_WIDTH, MENU_LIST_HEIGHT);
	gui_image_init(&menutree_settings_gui_image, 119, 22, EMBEDDED_FILE_PTR(settings_119x22_raw));
	gui_element_set_position(&menutree_settings_gui_image.element, 0, 22);

	// Root menu - Power off
	gui_image_init(&menutree_power_off_gui_image, 119, 18, EMBEDDED_FILE_PTR(power_off_119x18_raw));
	gui_element_set_position(&menutree_power_off_gui_image.element, 0, 44);

	// Root menu - Applications - GIF player
	gui_image_init(&menutree_gifplayer_gui_image, 119, 22, EMBEDDED_FILE_PTR(gif_player_119x22_raw));

	// Root menu - Settings - WLAN Settings
	gui_image_init(&menutree_wlan_settings_gui_image, 119, 22, EMBEDDED_FILE_PTR(wlan_settings_119x22_raw));

	// Vertical separator
	gui_rectangle_init(&menutree_vertical_separator);
	gui_rectangle_set_color(&menutree_vertical_separator, 255);
	gui_element_set_position(&menutree_vertical_separator.element, 158, 0);
	gui_element_set_size(&menutree_vertical_separator.element, 1, MENU_LIST_HEIGHT);
	gui_element_add_child(&menutree_root_gui_container.element, &menutree_vertical_separator.element);

	// WLAN AP indicator
	gui_image_init(&menutree_wlan_ap_gui_image, 15, 12, EMBEDDED_FILE_PTR(wlan_ap_15x12_raw));
	gui_element_set_position(&menutree_wlan_ap_gui_image.element, 162, 2);
	gui_element_add_child(&menutree_root_gui_container.element, &menutree_wlan_ap_gui_image.element);

	// WLAN station indicator
	gui_image_init(&menutree_wlan_station_gui_image, 20, 12, EMBEDDED_FILE_PTR(wlan_station_20x12_raw));
	gui_element_set_position(&menutree_wlan_station_gui_image.element, 181, 2);
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

	// Root menu - Settings - WLAN Settings
	menu_entry_app_init(&menutree_root_settings_wlan_settings);
	menu_entry_submenu_add_entry(&menutree_root_settings, &menutree_root_settings_wlan_settings.base);
}

static menu_t menutree_menu;

menu_t *menutree_init(gui_container_t *gui_root) {
	gui_element_init(gui_root);
	menu_element_init();
	menu_init(&menutree_menu, &menutree_root, &menutree_root_gui_container.element);
	menu_setup_gui(&menutree_menu, &menutree_list_gui_container);
	return &menutree_menu;
}

