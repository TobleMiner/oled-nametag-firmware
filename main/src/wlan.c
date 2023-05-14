#include "wlan.h"

#include <stdbool.h>

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>

#include "wlan_ap.h"
#include "wlan_station.h"

static const char *TAG = "wlan";

static bool wlan_started = false;

void wlan_init() {
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	// Setup WLAN AP
	wlan_ap_init();

	// Setup WLAN station
	wlan_station_init();
}

void wlan_stop(void) {
	if (wlan_started) {
		esp_err_t err = esp_wifi_stop();

		if (err) {
			ESP_LOGE(TAG, "Failed to stop WLAN: %d", err);
		} else {
			wlan_started = false;
		}
	}
}

void wlan_reconfigure(void) {
	bool ap_enabled = wlan_ap_is_enabled();
	bool station_enabled = wlan_station_is_enabled();

	if (ap_enabled && station_enabled) {
		esp_wifi_set_mode(WIFI_MODE_APSTA);
	} else if(ap_enabled) {
		esp_wifi_set_mode(WIFI_MODE_AP);
	} else if(station_enabled) {
		esp_wifi_set_mode(WIFI_MODE_STA);
	}
}

void wlan_start(void) {
	bool ap_enabled = wlan_ap_is_enabled();
	bool station_enabled = wlan_station_is_enabled();

	if (ap_enabled || station_enabled) {
		esp_err_t err = esp_wifi_start();

		if (err) {
			ESP_LOGE(TAG, "Failed to start WLAN: %d", err);
		} else {
			wlan_started = true;
		}
	}
}

void wlan_restart(void) {
	wlan_stop();
	wlan_reconfigure();
	wlan_start();
}

bool wlan_is_started(void) {
	return wlan_started;
}
