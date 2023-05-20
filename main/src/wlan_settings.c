#include "wlan_settings.h"

#include <stdint.h>
#include <string.h>

#include <esp_log.h>
#include <esp_netif_ip_addr.h>
#include <sdkconfig.h>

#include <qrcodegen.h>

#include "event_bus.h"
#include "gui.h"
#include "iputil.h"
#include "util.h"
#include "wlan_ap.h"
#include "wlan_station.h"

static const char *TAG = "wlan settings";

static menu_cb_f menu_cb;
static void *menu_cb_ctx;

static gui_container_t wlan_settings_container;

static gui_label_t wlan_ssid_label;
static gui_label_t wlan_psk_label;
static gui_label_t wlan_generate_psk_label;

static gui_rectangle_t wlan_qr_code_border;
static uint8_t wlan_qr_code_image_data[64 * 64];
static gui_image_t wlan_qr_code_image;

static uint8_t qrcode_data[qrcodegen_BUFFER_LEN_MAX];
static uint8_t qrcode_temp_buffer[qrcodegen_BUFFER_LEN_MAX];
static char wlan_encode_buffer[256] = { 0 };

static button_event_handler_t button_event_handler;

static gui_container_t wait_modal_container;
static gui_rectangle_t wait_modal_border;
static gui_label_t wait_modal_label;

static gui_t *gui_root;

static event_bus_handler_t wlan_event_handler;

static char wlan_ap_ssid[128] = { 0 };
static char wlan_ap_psk[128] = { 0 };

static gui_container_t wlan_station_info_container;

static gui_label_t wlan_station_info_state_label;
static char wlan_station_info_state_text[64];

static gui_label_t wlan_station_info_address_label;

static gui_label_t wlan_station_info_ip_address_labels[4];
static char wlan_station_info_ip_address_text[4][64];

static event_bus_handler_t wlan_station_event_handler;

static gui_container_t *active_container = NULL;

static bool on_button_event(const button_event_t *event, void *priv) {
	ESP_LOGI(TAG, "Button event");

	if (event->button == BUTTON_EXIT) {
		ESP_LOGI(TAG, "Quitting WLAN info");
		buttons_disable_event_handler(&button_event_handler);
		gui_element_set_hidden(&active_container->element, true);
		ESP_LOGI(TAG, "Returning to menu");
		menu_cb(menu_cb_ctx);
		ESP_LOGI(TAG, "Done");
		return true;
	}

	if (event->button == BUTTON_ENTER && active_container == &wlan_settings_container) {
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
	unsigned int ssid_offset = 0, ssid_len = 0;
	unsigned int psk_offset = 0, psk_len = 0;

	wlan_ap_lock();
	ssid = wlan_ap_get_ssid_();
	psk = wlan_ap_get_psk_();
	if (ssid && psk) {
		snprintf(wlan_encode_buffer, sizeof(wlan_encode_buffer), "WIFI:S:%s;T:WPA;P:%s;;", ssid, psk);
		ssid_offset = 7;
		ssid_len = strlen(ssid);
		psk_offset = 7 + ssid_len + 9;
		psk_len = strlen(psk);
		ESP_LOGI(TAG, "QR-Code data: %s", wlan_encode_buffer);
	} else {
		ESP_LOGE(TAG, "WLAN SSID (%s) or PSK (%s) not set", STR_NULL(ssid), STR_NULL(psk));
	}
	wlan_ap_unlock();

	success = qrcodegen_encodeText(wlan_encode_buffer, qrcode_temp_buffer,
				       qrcode_data, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
				       11, qrcodegen_Mask_AUTO, true);
	if (!success) {
		ESP_LOGE(TAG, "Failed to generate WLAN QR-Code");
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

	snprintf(wlan_ap_ssid, sizeof(wlan_ap_ssid), "SSID: %.*s", ssid_len, &wlan_encode_buffer[ssid_offset]);
	gui_label_set_text(&wlan_ssid_label, wlan_ap_ssid);
	snprintf(wlan_ap_psk, sizeof(wlan_ap_psk), "PSK: %.*s", psk_len, &wlan_encode_buffer[psk_offset]);
	gui_label_set_text(&wlan_psk_label, wlan_ap_psk);

	gui_unlock(gui_root);
	ESP_LOGI(TAG, "WLAN QR-Code updated");
}

static void on_wlan_ap_event(void *priv, void *data) {
	wlan_encode_qrcode();
}

static void update_station_state() {
	esp_err_t err;
	int num_ipv6_addr;
	esp_netif_ip_info_t ipv4_info;
	esp_ip6_addr_t ipv6_addresses[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
	unsigned int label_idx = 0;
	int i;

	gui_lock(gui_root);
	// Update AP state
	wlan_station_lock();
	if (wlan_station_is_enabled()) {
		if (wlan_station_is_connected()) {
			snprintf(wlan_station_info_state_text, sizeof(wlan_station_info_state_text),
				 "Connected to \"%s\"", STR_NULL(wlan_station_get_ssid_()));
			gui_label_set_text(&wlan_station_info_state_label, wlan_station_info_state_text);
		} else {
			gui_label_set_text(&wlan_station_info_state_label, "WLAN station enabled");
		}
	} else {
		gui_label_set_text(&wlan_station_info_state_label, "WLAN station not enabled");
	}
	wlan_station_unlock();

	// Update IP addresses
	err = wlan_station_get_ipv4_address(&ipv4_info);
	if (!err) {
		snprintf(wlan_station_info_ip_address_text[label_idx],
			 sizeof(wlan_station_info_ip_address_text[label_idx]),
			 IPSTR, IP2STR(&ipv4_info.ip));
		gui_label_set_text(&wlan_station_info_ip_address_labels[label_idx],
				   wlan_station_info_ip_address_text[label_idx]);
		gui_element_set_hidden(&wlan_station_info_ip_address_labels[label_idx].element, false);
		label_idx++;
	}

	num_ipv6_addr = wlan_station_get_ipv6_addresses(ipv6_addresses, ARRAY_SIZE(ipv6_addresses));
	for (i = 0; i < num_ipv6_addr && label_idx < ARRAY_SIZE(wlan_station_info_ip_address_labels); i++, label_idx++) {
		esp_ip6_addr_t *ipv6_addr = &ipv6_addresses[i];

		iputil_ipv6_addr_to_str(ipv6_addr, wlan_station_info_ip_address_text[label_idx]);
		gui_label_set_text(&wlan_station_info_ip_address_labels[label_idx],
				   wlan_station_info_ip_address_text[label_idx]);
		gui_element_set_hidden(&wlan_station_info_ip_address_labels[label_idx].element, false);
	}

	for (; label_idx < ARRAY_SIZE(wlan_station_info_ip_address_labels); label_idx++) {
		gui_element_set_hidden(&wlan_station_info_ip_address_labels[label_idx].element, true);
	}
	gui_unlock(gui_root);
}

static void on_wlan_sta_event(void *priv, void *data) {
	update_station_state();
}

static void address_label_init(gui_label_t *label, unsigned int pos_y) {
	gui_label_init(label, "1234567890:abcdef");
	gui_label_set_font_size(label, 9);
	gui_label_set_text_offset(label, 0, 1);
	gui_element_set_size(&label->element, 256 - 11, 10);
	gui_element_set_position(&label->element, 11, pos_y);
	gui_element_add_child(&wlan_station_info_container.element, &label->element);
}

void wlan_settings_init(gui_t *gui) {
	int i;
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

	// AP info
	gui_container_init(&wlan_settings_container);
	gui_element_set_size(&wlan_settings_container.element, 256, 64);
	gui_element_set_hidden(&wlan_settings_container.element, true);

	gui_label_init(&wlan_ssid_label, "SSID:");
	gui_label_set_font_size(&wlan_ssid_label, 13);
	gui_element_set_size(&wlan_ssid_label.element, 180, 18);
	gui_element_set_position(&wlan_ssid_label.element, 1, 6);
	gui_element_add_child(&wlan_settings_container.element, &wlan_ssid_label.element);

	gui_label_init(&wlan_psk_label, "PSK:");
	gui_label_set_font_size(&wlan_psk_label, 13);
	gui_element_set_size(&wlan_psk_label.element, 180, 18);
	gui_element_set_position(&wlan_psk_label.element, 8, 26);
	gui_element_add_child(&wlan_settings_container.element, &wlan_psk_label.element);

	gui_label_init(&wlan_generate_psk_label, "Press <ENTER> to generate new PSK");
	gui_label_set_font_size(&wlan_generate_psk_label, 9);
	gui_element_set_size(&wlan_generate_psk_label.element, 190, 14);
	gui_element_set_position(&wlan_generate_psk_label.element, 2, 50);
	gui_element_add_child(&wlan_settings_container.element, &wlan_generate_psk_label.element);

	gui_rectangle_init(&wlan_qr_code_border);
	gui_rectangle_set_color(&wlan_qr_code_border, 255);
	gui_element_add_child(&wlan_settings_container.element, &wlan_qr_code_border.element);

	gui_image_init(&wlan_qr_code_image, 64, 64, wlan_qr_code_image_data);
	gui_element_add_child(&wlan_settings_container.element, &wlan_qr_code_image.element);

	gui_element_add_child(&gui->container.element, &wlan_settings_container.element);

	// Station info
	gui_container_init(&wlan_station_info_container);
	gui_element_set_size(&wlan_station_info_container.element, 256, 64);
	gui_element_set_hidden(&wlan_station_info_container.element, true);
	gui_element_add_child(&gui->container.element, &wlan_station_info_container.element);

	gui_label_init(&wlan_station_info_state_label, "WLAN station disconnected");
	gui_label_set_font_size(&wlan_station_info_state_label, 9);
	gui_label_set_text_offset(&wlan_station_info_state_label, 0, 1);
	gui_element_set_size(&wlan_station_info_state_label.element, 256 - 6, 12);
	gui_element_set_position(&wlan_station_info_state_label.element, 6, 0);
	gui_element_add_child(&wlan_station_info_container.element, &wlan_station_info_state_label.element);

	gui_label_init(&wlan_station_info_address_label, "Addresses");
	gui_label_set_font_size(&wlan_station_info_address_label, 9);
	gui_element_set_size(&wlan_station_info_address_label.element, 64, 10);
	gui_element_set_position(&wlan_station_info_address_label.element, 6, 13);
	gui_element_add_child(&wlan_station_info_container.element, &wlan_station_info_address_label.element);

	for (i = 0; i < ARRAY_SIZE(wlan_station_info_ip_address_labels); i++) {
		address_label_init(&wlan_station_info_ip_address_labels[i], 23 + i * 10);
	}

	// Modal
	gui_container_init(&wait_modal_container);
	gui_element_set_size(&wait_modal_container.element, 256 - 40, 64 - 20);
	gui_element_set_position(&wait_modal_container.element, 20, 10);
	gui_element_set_hidden(&wait_modal_container.element, true);
	gui_element_add_child(&gui->container.element, &wait_modal_container.element);

	gui_rectangle_init(&wait_modal_border);
	gui_element_set_size(&wait_modal_border.element, 256 - 40, 64 - 20);
	gui_element_add_child(&wait_modal_container.element, &wait_modal_border.element);

	gui_label_init(&wait_modal_label, "Please wait...");
	gui_label_set_font_size(&wait_modal_label, 15);
	gui_element_set_size(&wait_modal_label.element, 119, 22);
	gui_element_set_position(&wait_modal_container.element, (256 - 40 - 119) / 2, (64 - 20 - 22) / 2);
	gui_element_add_child(&wait_modal_container.element, &wait_modal_label.element);

	buttons_register_multi_button_event_handler(&button_event_handler, &button_event_cfg);
	event_bus_subscribe(&wlan_event_handler, "wlan_ap", on_wlan_ap_event, NULL);
	event_bus_subscribe(&wlan_station_event_handler, "wlan_station", on_wlan_sta_event, NULL);
}

int wlan_settings_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	menu_cb = exit_cb;
	menu_cb_ctx = cb_ctx;

	wlan_encode_qrcode();

	active_container = &wlan_settings_container;
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

int wlan_station_endisable_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	gui_element_set_hidden(&wait_modal_container.element, false);
	gui_element_show(&wait_modal_container.element);
	wlan_station_lock();
	if (wlan_station_is_enabled()) {
		wlan_station_disable_();
	} else {
		wlan_station_enable_();
	}
	wlan_station_unlock();
	gui_element_set_hidden(&wait_modal_container.element, true);
	return 1;
}

int wlan_station_info_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	menu_cb = exit_cb;
	menu_cb_ctx = cb_ctx;

	update_station_state();

	active_container = &wlan_station_info_container;
	gui_element_set_hidden(&wlan_station_info_container.element, false);
	gui_element_show(&wlan_station_info_container.element);
	buttons_enable_event_handler(&button_event_handler);

	return 0;
}
