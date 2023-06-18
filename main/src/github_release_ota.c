#include "github_release_ota.h"

#include <esp_log.h>

#include "buttons.h"
#include "github.h"

static const char *TAG = "GitHub OTA";

static gui_container_t app_container;

static gui_label_t status_label;

static gui_container_t wait_modal_container;
static gui_rectangle_t wait_modal_border;
static gui_label_t wait_modal_label;

static gui_t *gui;

static button_event_handler_t button_event_handler;

static menu_cb_f menu_cb = NULL;
static void *menu_cb_ctx;

static github_release_ctx_t release_ctx;

static gui_label_t *ota_labels;

static void remove_ota_labels(void) {
	ota_http_firmware_update_t *update;
	int i = 0;

	LIST_FOR_EACH_ENTRY(update, &release_ctx.ota_releases.releases, list) {
		gui_label_t *label = &ota_labels[i];

		gui_element_remove_child(&app_container.element,
					 &label->element);
		i++;
	}

	free(ota_labels);
	ota_labels = NULL;
}

static bool on_button_event(const button_event_t *event, void *priv) {
	if (event->button == BUTTON_EXIT) {
		github_abort(&release_ctx);
		if (ota_labels) {
			gui_lock(gui);
			remove_ota_labels();
			gui_unlock(gui);
		}
		ota_http_update_free_releases(&release_ctx.ota_releases);
		buttons_disable_event_handler(&button_event_handler);
		gui_element_set_hidden(&app_container.element, true);
		gui_element_set_hidden(&wait_modal_container.element, true);
		menu_cb(menu_cb_ctx);
		return true;
	}

	return false;
}

void github_release_ota_init(gui_t *gui_root) {
	button_event_handler_multi_user_cfg_t button_event_cfg = {
		.base = {
			.cb = on_button_event
		},
		.multi = {
			.button_filter = (1 << BUTTON_EXIT) | (1 << BUTTON_UP) | (1 << BUTTON_DOWN) | (1 << BUTTON_ENTER),
			.action_filter = (1 << BUTTON_ACTION_RELEASE)
		}
	};

	gui = gui_root;

	gui_container_init(&app_container);
	gui_element_set_size(&app_container.element, 256, 64);
	gui_element_set_hidden(&app_container.element, true);
	gui_element_add_child(&gui->container.element, &app_container.element);

	gui_label_init(&status_label, "uninitialized");
	gui_label_set_font_size(&status_label, 9);
	gui_element_set_position(&status_label.element, 3, 0);
	gui_element_set_size(&status_label.element, 256, 10);
	gui_element_add_child(&app_container.element,
			      &status_label.element);

	// Modal
	gui_container_init(&wait_modal_container);
	gui_element_set_size(&wait_modal_container.element, 256 - 40, 64 - 20);
	gui_element_set_position(&wait_modal_container.element, 20, 10);
	gui_element_set_hidden(&wait_modal_container.element, true);
	gui_element_add_child(&gui->container.element, &wait_modal_container.element);

	gui_rectangle_init(&wait_modal_border);
	gui_rectangle_set_color(&wait_modal_border, 255);
	gui_element_set_size(&wait_modal_border.element, 256 - 40, 64 - 20);
	gui_element_add_child(&wait_modal_container.element, &wait_modal_border.element);

	gui_label_init(&wait_modal_label, "Loading releases...");
	gui_label_set_font_size(&wait_modal_label, 12);
	gui_label_set_text_alignment(&wait_modal_label, GUI_TEXT_ALIGN_CENTER);
	gui_element_set_size(&wait_modal_label.element, 256 - 40, 16);
	gui_element_set_position(&wait_modal_label.element, 0, (64 - 20 - 14) / 2);
	gui_element_add_child(&wait_modal_container.element, &wait_modal_label.element);

	buttons_register_multi_button_event_handler(&button_event_handler, &button_event_cfg);
}

void ota_cb(github_release_ctx_t *releases, int err, void *ctx) {
	int i = 0;

	gui_element_set_hidden(&wait_modal_container.element, true);

	if (err) {
		gui_label_set_text(&status_label, "Failed to load releases");
		gui_element_set_hidden(&app_container.element, false);
		gui_element_show(&app_container.element);
		ESP_LOGE(TAG, "Download of release list failed");
		return;
	}

	gui_label_set_text(&status_label, "Available firmware versions:");

	ota_labels = calloc(LIST_LENGTH(&releases->ota_releases.releases), sizeof(gui_label_t));
	if (ota_labels) {
		ota_http_firmware_update_t *update;

		gui_lock(gui);
		ESP_LOGI(TAG, "Available OTA firmware versions:");
		LIST_FOR_EACH_ENTRY(update, &releases->ota_releases.releases, list) {
			gui_label_t *label = &ota_labels[i];

			gui_label_init(label, update->name);
			gui_label_set_font_size(label, 9);
			gui_element_set_position(&label->element, 10, 10 + 10 * i);
			gui_element_set_size(&label->element, 256, 10);
			gui_element_add_child(&app_container.element,
					      &label->element);
			ESP_LOGI(TAG, "\tFirmware update %s: %s", update->name, update->url);
			i++;
		}
		gui_unlock(gui);
	}

	gui_element_set_hidden(&app_container.element, false);
	gui_element_show(&app_container.element);
}

int github_release_ota_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	menu_cb = exit_cb;
	menu_cb_ctx = cb_ctx;
	ota_labels = NULL;
	gui_element_set_hidden(&wait_modal_container.element, false);
	gui_element_show(&wait_modal_container.element);

	github_list_releases(&release_ctx, "atf-builds/atf", ota_cb, NULL);

	buttons_enable_event_handler(&button_event_handler);
	return 0;
}
