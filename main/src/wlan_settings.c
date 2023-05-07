#include "wlan_settings.h"

#include <stdint.h>

#include <esp_log.h>

#include <qrcodegen.h>

#include "embedded_files.h"
#include "event_bus.h"
#include "gui.h"
#include "util.h"
#include "wlan_ap.h"

static const char *TAG = "wlan settings";

static menu_cb_f menu_cb;
static void *menu_cb_ctx;

static gui_container_t wlan_settings_container;

static gui_image_t wlan_ssid_image;
static gui_image_t wlan_psk_image;
static gui_image_t wlan_generate_psk_image;

static gui_rectangle_t wlan_qr_code_border;
static uint8_t wlan_qr_code_image_data[64 * 64];
static gui_image_t wlan_qr_code_image;

static uint8_t qrcode_data[qrcodegen_BUFFER_LEN_MAX];
static uint8_t qrcode_temp_buffer[qrcodegen_BUFFER_LEN_MAX];
static char wlan_encode_buffer[256] = { 0 };

static button_event_handler_t button_event_handler;

static gui_container_t wait_modal_container;
static gui_rectangle_t wait_modal_border;
static gui_image_t wait_modal_image;

static gui_t *gui_root;

static event_bus_handler_t wlan_event_handler;

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

	if (event->button == BUTTON_ENTER) {
		ESP_LOGI(TAG, "Generating new WLAN AP PSK");
		wlan_ap_generate_new_psk();
		return true;
	}

	return false;
}

static void wlan_encode_qrcode(void) {
	bool success;
	int size, y, iscale, scaled_size;
	const char *ssid, *psk;

	wlan_ap_lock();
	ssid = wlan_ap_get_ssid_();
	psk = wlan_ap_get_psk_();
	if (ssid && psk) {
		snprintf(wlan_encode_buffer, sizeof(wlan_encode_buffer), "WIFI:S:%s;T:WPA;P:%s;;", ssid, psk);
		ESP_LOGI(TAG, "QR-Code data: %s", wlan_encode_buffer);
	} else {
		ESP_LOGE(TAG, "WLAN SSID (%s) or PSK (%s) not set", STR_NULL(ssid), STR_NULL(psk));
	}
	wlan_ap_unlock();

	success = qrcodegen_encodeText(wlan_encode_buffer, qrcode_temp_buffer,
				       qrcode_data, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
				       11, qrcodegen_Mask_AUTO, true);
	if (!success) {
		ESP_LOGE(TAG, "Failed to generate WiFi QR-Code");
		return;
	}

	size = qrcodegen_getSize(qrcode_data);
	ESP_LOGI(TAG, "QR Code size: %d", size);
	if (size > 62) {
		ESP_LOGE(TAG, "QR Code too large, can not continue");
		return;
	}

	gui_lock(gui_root);
	iscale = 64 / size;
	scaled_size = size * iscale;
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
					wlan_qr_code_image_data[(module_base_y + module_y) * scaled_size + module_base_x + module_x] =
						module ? 0 : 255;
				}
			}
		}
	}
	gui_element_set_size(&wlan_qr_code_image.element, scaled_size, scaled_size);
	gui_element_set_position(&wlan_qr_code_image.element, 256 - 64 + (64 - scaled_size) / 2, (64 - scaled_size) / 2);
	gui_element_set_size(&wlan_qr_code_border.element, scaled_size + 2, scaled_size + 2);
	gui_element_set_position(&wlan_qr_code_border.element, 256 - 64 + (64 - scaled_size) / 2 - 1, (64 - scaled_size) / 2 - 1);
	gui_unlock(gui_root);
	ESP_LOGI(TAG, "WiFi QR-Code updated");
}

static void on_wlan_ap_event(void *priv, void *data) {
	wlan_encode_qrcode();
}

void wlan_settings_init(gui_t *gui) {
	button_event_handler_multi_user_cfg_t button_event_cfg = {
		.base = {
			.cb = on_button_event
		},
		.multi = {
			.button_filter = (1 << BUTTON_EXIT) | (1 << BUTTON_ENTER),
			.action_filter = (1 << BUTTON_ACTION_RELEASE)
		}
	};

	gui_root = gui;

	gui_container_init(&wlan_settings_container);
	gui_element_set_size(&wlan_settings_container.element, 256, 64);
	gui_element_set_hidden(&wlan_settings_container.element, true);

	gui_image_init(&wlan_ssid_image, 37, 12, EMBEDDED_FILE_PTR(ssid_37x12_raw));
	gui_element_set_position(&wlan_ssid_image.element, 4, 7);
	gui_element_add_child(&wlan_settings_container.element, &wlan_ssid_image.element);

	gui_image_init(&wlan_psk_image, 31, 12, EMBEDDED_FILE_PTR(psk_31x12_raw));
	gui_element_set_position(&wlan_psk_image.element, 11, 26);
	gui_element_add_child(&wlan_settings_container.element, &wlan_psk_image.element);

	gui_image_init(&wlan_generate_psk_image, 176, 9, EMBEDDED_FILE_PTR(generate_psk_176x9_raw));
	gui_element_set_position(&wlan_generate_psk_image.element, 5, 48);
	gui_element_add_child(&wlan_settings_container.element, &wlan_generate_psk_image.element);

	gui_rectangle_init(&wlan_qr_code_border);
	gui_rectangle_set_color(&wlan_qr_code_border, 255);
	gui_element_add_child(&wlan_settings_container.element, &wlan_qr_code_border.element);

	gui_image_init(&wlan_qr_code_image, 64, 64, wlan_qr_code_image_data);
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
	event_bus_subscribe(&wlan_event_handler, "wlan_ap", on_wlan_ap_event, NULL);
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
