#include "menu.h"

#include <stdbool.h>
#include <esp_log.h>

#include "util.h"

static const char *TAG = "menu";

void menu_init(menu_t *menu, menu_entry_submenu_t *root, gui_element_t *gui_root) {
	menu->root = root;
	menu->gui_root = gui_root;
}

void menu_entry_submenu_init(menu_entry_submenu_t *entry) {
	entry->base.type = MENU_ENTRY_SUBMENU;
	INIT_LIST_HEAD(entry->base.list);
	INIT_LIST_HEAD(entry->entries);
}

void menu_entry_app_init(menu_entry_app_t *entry) {
	entry->base.type = MENU_ENTRY_APP;
	INIT_LIST_HEAD(entry->base.list);
}

void menu_entry_submenu_add_entry(menu_entry_submenu_t *submenu, menu_entry_t *entry) {
	LIST_APPEND_TAIL(&entry->list, &submenu->entries);
}

static menu_entry_t *menu_find_first_entry(const menu_entry_submenu_t *submenu) {
	if (!submenu || LIST_IS_EMPTY(&submenu->entries)) {
		return NULL;
	}

	return LIST_GET_ENTRY(submenu->entries.next, menu_entry_t, list);
}

static bool menu_navigation_gui_cb(const gui_event_handler_t *handler, gui_event_t event, gui_element_t *elem) {
	menu_entry_t *entry = handler->cfg.priv;
	menu_entry_submenu_t *parent = entry->parent;

	if (event == GUI_EVENT_BACK) {
		menu_entry_submenu_t *parent_of_parent;
		menu_entry_t *new_current_entry;

		if (!parent) {
			return false;
		}
		parent_of_parent = parent->base.parent;
		if (!parent_of_parent) {
			return false;
		}
		new_current_entry = menu_find_first_entry(parent_of_parent);
		if (!new_current_entry) {
			return false;
		}

		gui_element_set_hidden(&parent->gui_container->element, true);
		gui_element_set_hidden(&parent_of_parent->gui_container->element, false);
//		menu->active_entry = new_current_entry;
		return true;
	}

	if (entry->type == MENU_ENTRY_SUBMENU) {
		menu_entry_submenu_t *entry_submenu = container_of(entry, menu_entry_submenu_t, base);
		menu_entry_t *new_current_entry;

		if (!parent) {
			return false;
		}

		new_current_entry = menu_find_first_entry(entry_submenu);
		if (!new_current_entry) {
			return false;
		}

		ESP_LOGI(TAG, "Entering submenu, hiding %s, showing %s", parent->base.name, entry_submenu->base.name);
		ESP_LOGI(TAG, "%s shown: %d", entry_submenu->base.name, entry_submenu->gui_container->element.shown);
		gui_element_set_hidden(&parent->gui_container->element, true);
		gui_element_set_hidden(&entry_submenu->gui_container->element, false);
//		menu->active_entry = new_current_entry;
		return true;
	}

	return false;
}

static void menu_setup_gui_(menu_entry_t *entry, gui_container_t *gui_root, gui_container_t *gui_container);
static void menu_setup_gui_(menu_entry_t *entry, gui_container_t *gui_root, gui_container_t *gui_container) {
	if (entry->gui_element) {
		gui_event_handler_cfg_t event_handler_cfg = {
			.event_filter = (1 << GUI_EVENT_CLICK) | (1 << GUI_EVENT_BACK),
			.cb = menu_navigation_gui_cb,
			.priv = entry
		};

		gui_element_add_event_handler(entry->gui_element, &entry->gui_event_handler, &event_handler_cfg);
		if (gui_container) {
			ESP_LOGI(TAG, "Adding entry %s to gui", STR_NULL(entry->name));
			gui_element_add_child(&gui_container->element, entry->gui_element);
		} else {
			ESP_LOGW(TAG, "Have no gui container to place %s", STR_NULL(entry->name));
		}
	} else {
		ESP_LOGW(TAG, "Entry %s does not have a gui element and will thus be invisible", STR_NULL(entry->name));
	}

	if (entry->type == MENU_ENTRY_SUBMENU) {
		menu_entry_submenu_t *entry_submenu = container_of(entry, menu_entry_submenu_t, base);
		menu_entry_t *cursor;

		if (entry_submenu->gui_container) {
			gui_container_t *submenu_container = entry_submenu->gui_container;

			gui_element_add_child(&gui_root->element, &submenu_container->element);
			gui_element_set_hidden(&submenu_container->element, true);
		} else {
			ESP_LOGW(TAG, "Submenu %s has no gui container element", STR_NULL(entry->name));
		}

		LIST_FOR_EACH_ENTRY(cursor, &entry_submenu->entries, list) {
			ESP_LOGI(TAG, "Adding entry %s to menu %s", STR_NULL(cursor->name), STR_NULL(entry->name));
			menu_setup_gui_(cursor, gui_root, entry_submenu->gui_container);
		}
	}
}

void menu_setup_gui(menu_t *menu, gui_container_t *container) {
	menu_setup_gui_(&menu->root->base, container, NULL);
}

void menu_show(menu_t *menu) {
	if (!menu->active_menu_entry) {
		menu->active_menu_entry = menu_find_first_entry(menu->root);
	}

	if (menu->active_menu_entry) {
		menu_entry_t *active_entry = menu->active_menu_entry;
		menu_entry_submenu_t *parent = active_entry->parent;

		ESP_LOGI(TAG, "Unhiding %s", STR_NULL(parent->base.name));
		gui_element_set_hidden(&parent->gui_container->element, false);
	}
	gui_element_show(menu->gui_root);
}
