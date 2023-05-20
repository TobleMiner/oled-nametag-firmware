#include "wlan_ap.h"

#include <stddef.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_random.h>

#include <dhcpserver/dhcpserver.h>
#include <lwip/err.h>
#include <lwip/sys.h>

#include "event_bus.h"
#include "settings.h"
#include "util.h"
#include "vendor.h"
#include "wlan.h"

#define PSK_LENGTH 10

#define WLAN_AP_SSID_PREFIX	"oled badge "
#define WLAN_AP_CHANNEL		6
#define WLAN_AP_MAX_STATIONS	3

static const char *TAG = "WLAN AP";

static dhcps_offer_t dhcps_flag_false = 0;

static esp_netif_t *wlan_ap_iface;

static StaticSemaphore_t ap_lock_buffer;
static SemaphoreHandle_t ap_lock;

static char *wlan_ap_ssid = NULL;
static char *wlan_ap_psk = NULL;

static bool wlan_ap_active = false;
static bool wlan_ap_enabled = false;

static wifi_sta_list_t station_list;

void wlan_ap_lock(void) {
	xSemaphoreTakeRecursive(ap_lock, portMAX_DELAY);
}

void wlan_ap_unlock(void) {
	xSemaphoreGiveRecursive(ap_lock);
}

static void generate_psk(char *dst, size_t len) {
	static const char *non_confusable_characters = "23467abcdefjkprtxyz";

	while (len--) {
		uint32_t rnd = esp_random();

		rnd %= strlen(non_confusable_characters);
		*dst++ = non_confusable_characters[rnd];
	}
}

static char *generate_psk_(void) {
	char *psk = calloc(1, PSK_LENGTH + 1);
	if (!psk) {
		return NULL;
	}

	generate_psk(psk, PSK_LENGTH);
	return psk;
}

static void generate_new_psk(void) {
	char *ap_psk;

	ap_psk = generate_psk_();
	if (!ap_psk) {
		return;
	}
	if (wlan_ap_psk) {
		free(wlan_ap_psk);
	}
	wlan_ap_psk = ap_psk;
	settings_set_wlan_ap_psk(ap_psk);
}

static void load_psk_(void) {
	char *ap_psk;

	ap_psk = settings_get_wlan_ap_psk();
	if (ap_psk) {
		if (wlan_ap_psk) {
			free(wlan_ap_psk);
		}
		wlan_ap_psk = ap_psk;
	} else {
		ESP_LOGI(TAG, "No PSK set on AP, generating a new one");
		generate_new_psk();
	}
}

static void generate_ssid_(void) {
	char *ap_ssid;
	const char *serial;
	unsigned int ssid_len;

	vendor_lock();
	serial = vendor_get_serial_number_();
	ssid_len = strlen(WLAN_AP_SSID_PREFIX) + strlen(serial) + 1;
	ap_ssid = calloc(1, ssid_len);
	if (ap_ssid) {
		snprintf(ap_ssid, ssid_len, "%s%s", WLAN_AP_SSID_PREFIX, serial);
		if (wlan_ap_ssid) {
			free(wlan_ap_ssid);
		}
		wlan_ap_ssid = ap_ssid;
	}
	vendor_unlock();
}

static void enable_ap_(void) {
	wifi_config_t wifi_config = {
		.ap = {
			.channel = WLAN_AP_CHANNEL,
			.max_connection = WLAN_AP_MAX_STATIONS,
			.authmode = WIFI_AUTH_WPA_WPA2_PSK,
			.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP,
			.pmf_cfg = {
				.required = false,
			},
		},
	};
	const char *ssid = STR_NULL(wlan_ap_ssid);

	strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
	wifi_config.ap.ssid_len = strlen(ssid);

	wlan_ap_enabled = true;
	if (wlan_ap_psk) {
		strcpy((char *)wifi_config.ap.password, wlan_ap_psk);

		wlan_stop();
		wlan_reconfigure();
		ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
		wlan_start();

		ESP_LOGI(TAG, "WLAN AP enabled, SSID: %s, PSK: %s", wifi_config.ap.ssid, wifi_config.ap.password);
		wlan_ap_active = wlan_is_started();
	} else {
		ESP_LOGE(TAG, "Can not enable AP, PSK not populated");
	}
	event_bus_notify("wlan_ap", NULL);
}

static void disable_ap_(void) {
	wlan_ap_enabled = false;
	wlan_restart();
	wlan_ap_active = false;
	event_bus_notify("wlan_ap", NULL);
}

void wlan_ap_init(void) {
	ap_lock = xSemaphoreCreateRecursiveMutexStatic(&ap_lock_buffer);

	wlan_ap_iface = esp_netif_create_default_wifi_ap();
	ESP_ERROR_CHECK(!wlan_ap_iface);
	vendor_lock();
	esp_netif_set_hostname(wlan_ap_iface, vendor_get_hostname_());
	vendor_unlock();
	esp_netif_dhcps_option(wlan_ap_iface, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &dhcps_flag_false, sizeof(dhcps_flag_false));
	esp_netif_dhcps_option(wlan_ap_iface, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_flag_false, sizeof(dhcps_flag_false));

	load_psk_();
	generate_ssid_();
	if (settings_get_wlan_ap_enable()) {
		enable_ap_();
	}
}

const char *wlan_ap_get_ssid_(void) {
	return wlan_ap_ssid;
}

const char *wlan_ap_get_psk_(void) {
	return wlan_ap_psk;
}

void wlan_ap_generate_new_psk(void) {
	wlan_ap_lock();
	generate_new_psk();
	if (wlan_ap_active) {
		disable_ap_();
		enable_ap_();
	}
	wlan_ap_unlock();
}

void wlan_ap_enable_(void) {
	if (!wlan_ap_active) {
		enable_ap_();
	}
	settings_set_wlan_ap_enable(true);
}

void wlan_ap_enable(void) {
	wlan_ap_lock();
	wlan_ap_enable_();
	wlan_ap_unlock();
}

void wlan_ap_disable_(void) {
	if (wlan_ap_active) {
		disable_ap_();
	}
	settings_set_wlan_ap_enable(false);
}

void wlan_ap_disable(void) {
	wlan_ap_lock();
	wlan_ap_disable_();
	wlan_ap_unlock();
}

bool wlan_ap_is_active(void) {
	return wlan_ap_active;
}

bool wlan_ap_is_enabled(void) {
	return wlan_ap_enabled;
}

unsigned int wlan_ap_get_num_connected_stations(void) {
	esp_err_t err;
	unsigned int num_stations = 0;

	wlan_ap_lock();
	err = esp_wifi_ap_get_sta_list(&station_list);
	if (!err && station_list.num >= 0) {
		num_stations = station_list.num;
	}
	wlan_ap_unlock();

	return num_stations;
}
