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

#define PSK_LENGTH 10

#define WLAN_AP_SSID		"oled-nametag"
#define WLAN_AP_CHANNEL		6
#define WLAN_AP_MAX_STATIONS	3

static const char *TAG = "WLAN AP";

static dhcps_offer_t dhcps_flag_false = 0;

static esp_netif_t *wlan_ap_iface;

static StaticSemaphore_t ap_lock_buffer;
static SemaphoreHandle_t ap_lock;

static const char *wlan_ap_ssid = WLAN_AP_SSID;
static char *wlan_ap_psk = NULL;

static bool wlan_ap_active = false;

void wlan_ap_lock(void) {
	xSemaphoreTake(ap_lock, portMAX_DELAY);
}

void wlan_ap_unlock(void) {
	xSemaphoreGive(ap_lock);
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

static void enable_ap_(void) {
	wifi_config_t wifi_config = {
		.ap = {
			.ssid = WLAN_AP_SSID,
			.ssid_len = strlen(WLAN_AP_SSID),
			.channel = WLAN_AP_CHANNEL,
			.max_connection = WLAN_AP_MAX_STATIONS,
			.authmode = WIFI_AUTH_WPA_WPA2_PSK,
			.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP,
			.pmf_cfg = {
				.required = false,
			},
		},
	};

	if (wlan_ap_psk) {
		strcpy((char *)wifi_config.ap.password, wlan_ap_psk);

		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
		ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
		ESP_ERROR_CHECK(esp_wifi_start());

		ESP_LOGI(TAG, "WLAN AP enabled, SSID: %s, PSK: %s", wifi_config.ap.ssid, wifi_config.ap.password);
		wlan_ap_active = true;
	} else {
		ESP_LOGE(TAG, "Can not enable AP, PSK not populated");
	}
	event_bus_notify("wlan_ap", NULL);
}

static void disable_ap_(void) {
	esp_wifi_stop();
	wlan_ap_active = false;
	event_bus_notify("wlan_ap", NULL);
}

void wlan_ap_init(void) {
	ap_lock = xSemaphoreCreateMutexStatic(&ap_lock_buffer);

	wlan_ap_iface = esp_netif_create_default_wifi_ap();
	ESP_ERROR_CHECK(!wlan_ap_iface);
	esp_netif_set_hostname(wlan_ap_iface, "oled-nametag");
	esp_netif_dhcps_option(wlan_ap_iface, ESP_NETIF_OP_SET, ESP_NETIF_ROUTER_SOLICITATION_ADDRESS, &dhcps_flag_false, sizeof(dhcps_flag_false));
	esp_netif_dhcps_option(wlan_ap_iface, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_flag_false, sizeof(dhcps_flag_false));

	load_psk_();
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

bool wlan_ap_is_enabled(void) {
	return wlan_ap_active;
}