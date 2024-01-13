#pragma once

#include <stdbool.h>

#include "buttons.h"
#include "gui.h"
#include "list.h"

typedef enum menu_entry_type {
	MENU_ENTRY_SUBMENU,
	MENU_ENTRY_APP
} menu_entry_type_t;

typedef struct menu menu_t;
typedef struct menu_entry menu_entry_t;
typedef struct menu_entry_submenu menu_entry_submenu_t;

struct menu_entry {
	struct list_head list;
	menu_entry_type_t type;
	const char *name;
	menu_entry_submenu_t *parent;
	gui_element_t *gui_element;
};

typedef struct menu_entry_submenu {
        menu_entry_t base;

	struct list_head entries;
	gui_list_t *gui_list;
} menu_entry_submenu_t;

typedef void (*menu_cb_f)(void *cb_ctx);

typedef struct menu_entry_app {
        menu_entry_t base;

	bool keep_menu_visible;
	int (*run)(menu_cb_f exit_cb, void *cb_ctx, void *priv);
	void *priv;
} menu_entry_app_t;

typedef struct menu_cbs {
	void (*on_app_entry)(const menu_t *menu, const menu_entry_app_t *app, void *ctx);
	void (*on_app_exit)(const menu_t *menu, void *ctx);
	void (*on_menu_exit)(void *ctx);
} menu_cbs_t;

struct menu {
	gui_element_t *gui_root;
	menu_entry_submenu_t *root;
	menu_entry_t *selected_entry;
	bool in_app;
	button_event_handler_t button_event_handler;
	void *cb_ctx;
	const menu_cbs_t *cbs;
};

void menu_init(menu_t *menu, menu_entry_submenu_t *root, gui_element_t *gui_root);
void menu_entry_submenu_init(menu_entry_submenu_t *entry);
void menu_entry_app_init(menu_entry_app_t *entry);
void menu_entry_submenu_add_entry(menu_entry_submenu_t *submenu, menu_entry_t *entry);
void menu_setup_gui(menu_t *menu, gui_container_t *gui_root);
void menu_show(menu_t *menu);
void menu_hide(menu_t *menu);
menu_entry_app_t *menu_find_app_by_name(menu_t *menu, const char *name);
void menu_set_app_active(menu_t *menu, menu_entry_app_t *app);
