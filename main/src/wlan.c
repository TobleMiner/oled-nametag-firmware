#include "wlan.h"

#include <esp_err.h>
#include <esp_event.h>
#include <esp_wifi.h>

void wlan_init() {
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}
