#include "wlan_settings.h"

#include <stdint.h>

#include <esp_log.h>

#include <qrcodegen.h>

#include "embedded_files.h"
#include "gui.h"
#include "wlan_ap.h"

static const char *TAG = "wlan settings";

static menu_cb_f menu_cb;
static void *menu_cb_ctx;

static gui_container_t wlan_settings_container;

static uint8_t wlan_qr_code_image_data[64 * 64];
static gui_image_t wlan_qr_code_image;

static uint8_t qrcode_data[qrcodegen_BUFFER_LEN_MAX];
static uint8_t qrcode_temp_buffer[qrcodegen_BUFFER_LEN_MAX];
static char wlan_encode_buffer[256];

static button_event_handler_t button_event_handler;

static gui_container_t wait_modal_container;
static gui_rectangle_t wait_modal_border;
static gui_image_t wait_modal_image;

static bool on_button_event(const button_event_t *event, void *priv) {
	ESP_LOGI(TAG, "Button event");

	if (event->button == BUTTON_EXIT) {
		ESP_LOGI(TAG, "Quitting WLAN settings");
		buttons_disable_event_handler(&button_event_handler);
		gui_element_set_hidden(&wlan_settings_container.element, true);
		ESP_LOGI(TAG, "Returning to menu");
		menu_cb(menu_cb_ctx);
		ESP_LOGI(TAG, "Done");
		return true;
	}

	return false;
}

void wlan_settings_init(gui_t *gui) {
	button_event_handler_multi_user_cfg_t button_event_cfg = {
		.base = {
			.cb = on_button_event
		},
		.multi = {
			.button_filter = (1 << BUTTON_EXIT),
			.action_filter = (1 << BUTTON_ACTION_RELEASE)
		}
	};

	gui_container_init(&wlan_settings_container);
	gui_element_set_size(&wlan_settings_container.element, 256, 64);
	gui_element_set_hidden(&wlan_settings_container.element, true);

	gui_image_init(&wlan_qr_code_image, 64, 64, wlan_qr_code_image_data);
	gui_element_set_position(&wlan_qr_code_image.element, 256 - 64, 0);
	gui_element_add_child(&wlan_settings_container.element, &wlan_qr_code_image.element);

	gui_element_add_child(&gui->container.element, &wlan_settings_container.element);

	gui_container_init(&wait_modal_container);
	gui_element_set_size(&wait_modal_container.element, 256 - 40, 64 - 20);
	gui_element_set_position(&wait_modal_container.element, 20, 10);
	gui_element_set_hidden(&wait_modal_container.element, true);
	gui_element_add_child(&gui->container.element, &wait_modal_container.element);

	gui_rectangle_init(&wait_modal_border);
	gui_element_set_size(&wait_modal_border.element, 256 - 40, 64 - 20);
	gui_element_add_child(&wait_modal_container.element, &wait_modal_border.element);

	gui_image_init(&wait_modal_image, 119, 22, EMBEDDED_FILE_PTR(please_wait_119x22_raw));
	gui_element_set_position(&wait_modal_container.element, (256 - 40 - 119) / 2, (64 - 20 - 22) / 2);
	gui_element_add_child(&wait_modal_container.element, &wait_modal_image.element);

	buttons_register_multi_button_event_handler(&button_event_handler, &button_event_cfg);
}

static void wlan_encode_qrcode(void) {
	bool success;
	int size, y, iscale;

	success = qrcodegen_encodeText("This could be WiFi SSID information", qrcode_temp_buffer,
				       qrcode_data, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
				       11, qrcodegen_Mask_AUTO, true);
	if (!success) {
		ESP_LOGE(TAG, "Failed to generate WiFi QR-Code");
		return;
	}

	size = qrcodegen_getSize(qrcode_data);
	ESP_LOGI(TAG, "QR Code size: %d", size);
	if (size > 64) {
		ESP_LOGE(TAG, "QR Code too large, can not continue");
		return;
	}

	iscale = 64 / size;
	for (y = 0;  y < size; y++) {
		int x;

		for (x = 0; x < size; x++) {
			bool module = qrcodegen_getModule(qrcode_data, x, y);
			int module_base_x = x * iscale;
			int module_base_y = y * iscale;
			int module_y;

			for (module_y = 0; module_y < iscale; module_y++) {
				int module_x;

				for (module_x = 0; module_x < iscale; module_x++) {
					wlan_qr_code_image_data[(module_base_y + module_y) * 64 + module_base_x + module_x] =
						module ? 255 : 0;
				}
			}
		}
	}
}

int wlan_settings_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	menu_cb = exit_cb;
	menu_cb_ctx = cb_ctx;
	wlan_encode_qrcode();
	gui_element_set_hidden(&wlan_settings_container.element, false);
	gui_element_show(&wlan_settings_container.element);
	buttons_enable_event_handler(&button_event_handler);

	return 0;
}

int wlan_ap_endisable_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	gui_element_set_hidden(&wait_modal_container.element, false);
	gui_element_show(&wait_modal_container.element);
	wlan_ap_lock();
	if (wlan_ap_is_enabled()) {
		wlan_ap_disable_();
	} else {
		wlan_ap_enable_();
	}
	wlan_ap_unlock();
	gui_element_set_hidden(&wait_modal_container.element, true);
	return 1;
}
