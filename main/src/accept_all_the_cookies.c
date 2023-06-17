#include "accept_all_the_cookies.h"

#include "buttons.h"

static gui_container_t app_container;

static const char *accept_all_the_cookies =
	"ACCEPT ALL COOKIES ACCEPT ALL COOKIES ACCEPT ALL COOKIES ACCEPT ALL COOKIES ACCEPT ALL COOKIES";
static gui_marquee_t cookies_marquee;
static gui_label_t cookies_label;

static gui_t *gui;

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

void accept_all_the_cookies_init(gui_t *gui_root) {
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

	gui_label_init(&cookies_label, accept_all_the_cookies);
	gui_label_set_font_size(&cookies_label, 15);
	gui_label_set_text_offset(&cookies_label, 0, 4);
	gui_element_set_size(&cookies_label.element, 1000, 22);
//	gui_element_set_inverted(&cookies_label.element, true);

	gui_marquee_init(&cookies_marquee);
	gui_element_set_size(&cookies_marquee.container.element, 256, 22);
	gui_element_set_position(&cookies_marquee.container.element, 0, 64 / 2 - 22 / 2);
	gui_element_add_child(&cookies_marquee.container.element,
			      &cookies_label.element);
	gui_element_add_child(&app_container.element,
			      &cookies_marquee.container.element);

	buttons_register_multi_button_event_handler(&button_event_handler, &button_event_cfg);
}

int accept_all_the_cookies_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	menu_cb = exit_cb;
	menu_cb_ctx = cb_ctx;
	gui_element_set_hidden(&app_container.element, false);
	gui_element_show(&app_container.element);
	buttons_enable_event_handler(&button_event_handler);
	return 0;
}
