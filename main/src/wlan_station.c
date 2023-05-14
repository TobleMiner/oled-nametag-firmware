#include "wlan_station.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_log.h>

#include "event_bus.h"
#include "settings.h"
#include "wlan.h"

static esp_netif_t *wlan_sta_iface;

static StaticSemaphore_t sta_lock_buffer;
static SemaphoreHandle_t sta_lock;

static bool sta_enabled = false;
static bool scan_in_progress = false;

static void sta_event_handler(void* arg, esp_event_base_t event_base,
			      int32_t event_id, void* event_data) {

}

void wlan_station_init() {
	sta_lock = xSemaphoreCreateRecursiveMutexStatic(&sta_lock_buffer);

	wlan_sta_iface = esp_netif_create_default_wifi_sta();
	ESP_ERROR_CHECK(!wlan_sta_iface);
	esp_netif_set_hostname(wlan_sta_iface, "oled-nametag");

	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
							    &sta_event_handler, NULL, NULL));

	sta_enabled = settings_get_wlan_station_enable();
	if (sta_enabled) {
		wlan_station_enable();
	}
}

bool wlan_station_is_enabled() {
	return sta_enabled;
}

void wlan_station_lock() {
	xSemaphoreTakeRecursive(sta_lock, portMAX_DELAY);
}

void wlan_station_unlock() {
	xSemaphoreGiveRecursive(sta_lock);
}

static void wlan_station_set_enable_(bool enable) {
	sta_enabled = enable;
	wlan_restart();
	scan_in_progress = false;
	settings_set_wlan_station_enable(enable);
	event_bus_notify("wlan_station", NULL);
}

static void wlan_station_set_enable(bool enable) {
	wlan_station_lock();
	wlan_station_set_enable_(enable);
	wlan_station_unlock();
}

void wlan_station_enable() {
	wlan_station_set_enable(true);
}

void wlan_station_disable() {
	wlan_station_set_enable(false);
}

void wlan_station_enable_(void) {
	wlan_station_set_enable_(true);
}

void wlan_station_disable_(void) {
	wlan_station_set_enable_(false);
}
