#include "menu.h"

#include <stdbool.h>
#include <esp_log.h>

#include "util.h"

static const char *TAG = "menu";

static menu_entry_t *menu_find_first_entry(const menu_entry_submenu_t *submenu) {
	if (!submenu || LIST_IS_EMPTY(&submenu->entries)) {
		return NULL;
	}

	return LIST_GET_ENTRY(submenu->entries.next, menu_entry_t, list);
}

static menu_entry_t *menu_find_next_entry(menu_entry_t *entry, bool reverse)  {
	struct list_head *start = &entry->list;
	struct list_head *cursor = start;
	menu_entry_submenu_t *submenu = entry->parent;

	if (!submenu) {
		return NULL;
	}

	do {
		if (reverse) {
			cursor = cursor->prev;
		} else {
			cursor = cursor->next;
		}

		if (cursor != &submenu->entries) {
			menu_entry_t *entry = container_of(cursor, menu_entry_t, list);

			return entry;
		}
	} while (cursor != start);

	return NULL;
}

static void app_exit_cb(void *cb_ctx) {
	menu_t *menu = cb_ctx;

	if (menu->in_app) {
		menu->in_app = false;
		menu_show(menu);
	}
}

static bool on_button_event(const button_event_t *event, void *priv) {
	menu_t *menu = priv;

	ESP_LOGI(TAG, "Button %s pressed", button_to_name(event->button));

	if (event->button == BUTTON_DOWN || event->button == BUTTON_UP) {
		menu_entry_t *selected_entry = menu->selected_entry;
		menu_entry_t *new_selected_entry = menu_find_next_entry(selected_entry, event->button == BUTTON_UP);

		if (new_selected_entry) {
			menu->selected_entry = new_selected_entry;
			gui_list_set_selected_entry(selected_entry->parent->gui_list, new_selected_entry->gui_element);
		}
	}

	if (event->button == BUTTON_EXIT) {
		menu_entry_submenu_t *parent = menu->selected_entry->parent;
		menu_entry_submenu_t *parent_of_parent = parent->base.parent;
		menu_entry_t *new_selected_entry;

		if (!parent_of_parent) {
			// TODO: support exit from menu root level
			ESP_LOGI(TAG, "Trying to exit from menu root");
			return false;
		}
		new_selected_entry = menu_find_first_entry(parent_of_parent);
		if (new_selected_entry) {
			gui_element_set_hidden(&parent->gui_list->container.element, true);
			gui_element_set_hidden(&parent_of_parent->gui_list->container.element, false);
			menu->selected_entry = new_selected_entry;
			gui_list_set_selected_entry(parent_of_parent->gui_list, new_selected_entry->gui_element);
			return true;
		}
	}

	if (event->button == BUTTON_ENTER) {
		menu_entry_t *selected_entry = menu->selected_entry;

		if (selected_entry->type == MENU_ENTRY_APP) {
			menu_entry_app_t *entry_app = container_of(selected_entry, menu_entry_app_t, base);
			// TODO: support starting an application
			if (!menu->in_app) {
				int err;

				ESP_LOGI(TAG, "Starting application");
				menu_hide(menu);
				menu->in_app = true;
				err = entry_app->run(app_exit_cb, menu, entry_app->priv);
				if (err) {
					ESP_LOGE(TAG, "Application startup failed: %d", err);
					app_exit_cb(menu);
				}
				return true;
			}
		}
		if (selected_entry->type == MENU_ENTRY_SUBMENU) {
			menu_entry_submenu_t *entry_submenu = container_of(selected_entry, menu_entry_submenu_t, base);
			menu_entry_t *new_selected_entry = menu_find_first_entry(entry_submenu);
			menu_entry_submenu_t *parent = selected_entry->parent;

			if (new_selected_entry) {
				gui_element_set_hidden(&parent->gui_list->container.element, true);
				gui_element_set_hidden(&entry_submenu->gui_list->container.element, false);
				menu->selected_entry = new_selected_entry;
				gui_list_set_selected_entry(entry_submenu->gui_list, new_selected_entry->gui_element);
				return true;
			}
		}
	}

	return false;
}

void menu_init(menu_t *menu, menu_entry_submenu_t *root, gui_element_t *gui_root) {
	menu->root = root;
	menu->gui_root = gui_root;

	button_event_handler_multi_user_cfg_t button_event_cfg = {
		.base = {
			.cb = on_button_event,
			.ctx = menu
		},
		.multi = {
			.button_filter = (1 << BUTTON_UP) | (1 << BUTTON_DOWN) | (1 << BUTTON_EXIT) | (1 << BUTTON_ENTER),
			.action_filter = (1 << BUTTON_ACTION_RELEASE)
		}
	};
	buttons_register_multi_button_event_handler(&menu->button_event_handler, &button_event_cfg);
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

static void menu_setup_gui_(menu_entry_t *entry, gui_container_t *gui_root, gui_list_t *gui_list);
static void menu_setup_gui_(menu_entry_t *entry, gui_container_t *gui_root, gui_list_t *gui_list) {
	if (entry->gui_element) {
		if (gui_list) {
			ESP_LOGI(TAG, "Adding entry %s to gui", STR_NULL(entry->name));
			gui_element_add_child(&gui_list->container.element, entry->gui_element);
		} else {
			ESP_LOGW(TAG, "Have no gui container to place %s", STR_NULL(entry->name));
		}
	} else {
		ESP_LOGW(TAG, "Entry %s does not have a gui element and will thus be invisible", STR_NULL(entry->name));
	}

	if (entry->type == MENU_ENTRY_SUBMENU) {
		menu_entry_submenu_t *entry_submenu = container_of(entry, menu_entry_submenu_t, base);
		menu_entry_t *cursor;

		if (entry_submenu->gui_list) {
			gui_container_t *submenu_container = &entry_submenu->gui_list->container;

			gui_element_add_child(&gui_root->element, &submenu_container->element);
			gui_element_set_hidden(&submenu_container->element, true);
		} else {
			ESP_LOGW(TAG, "Submenu %s has no gui container element", STR_NULL(entry->name));
		}

		LIST_FOR_EACH_ENTRY(cursor, &entry_submenu->entries, list) {
			ESP_LOGI(TAG, "Adding entry %s to menu %s", STR_NULL(cursor->name), STR_NULL(entry->name));
			menu_setup_gui_(cursor, gui_root, entry_submenu->gui_list);
		}
	}
}

void menu_setup_gui(menu_t *menu, gui_container_t *container) {
	menu_setup_gui_(&menu->root->base, container, NULL);
}

void menu_show(menu_t *menu) {
	if (!menu->selected_entry) {
		menu->selected_entry = menu_find_first_entry(menu->root);
	}

	if (menu->selected_entry) {
		menu_entry_t *active_entry = menu->selected_entry;
		menu_entry_submenu_t *parent = active_entry->parent;

		ESP_LOGI(TAG, "Unhiding %s", STR_NULL(parent->base.name));
		gui_list_set_selected_entry(parent->gui_list, active_entry->gui_element);
		gui_element_set_hidden(&parent->gui_list->container.element, false);
	}
	gui_element_show(menu->gui_root);
	gui_element_set_hidden(menu->gui_root, false);
	buttons_enable_event_handler(&menu->button_event_handler);
}

void menu_hide(menu_t *menu) {
	buttons_disable_event_handler(&menu->button_event_handler);
	gui_element_set_hidden(menu->gui_root, true);
}
