#include "wlan_station.h"

#include <assert.h>
#include <errno.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp_debug_helpers.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <sdkconfig.h>

#include "event_bus.h"
#include "scheduler.h"
#include "settings.h"
#include "util.h"
#include "vendor.h"
#include "wlan.h"
#include "wlan_ap.h"

#define SCAN_INTERVAL_DEFAULT_MS	10000
#define SCAN_INTERVAL_AP_ACTIVE		30000
#define SCAN_INTERVAL_AP_CONNECTED	300000

static const char *TAG = "wlan station";

static esp_netif_t *wlan_sta_iface;

static StaticSemaphore_t sta_lock_buffer;
static SemaphoreHandle_t sta_lock;

static bool sta_enabled = false;
static bool scan_in_progress = false;
static bool sta_connected = false;

static unsigned int num_discovered_aps = 0;
static wifi_ap_record_t *discovered_aps = NULL;

static scheduler_task_t scan_task;

static char *sta_ssid = NULL;
static char *sta_psk = NULL;

static event_bus_handler_t vendor_event_handler;

static esp_err_t wlan_station_start_scan_(void) {
	if (!scan_in_progress) {
		esp_err_t err;

		scan_in_progress = true;
		err = esp_wifi_scan_start(NULL, false);
		if (err) {
			scan_in_progress = false;
			ESP_LOGW(TAG, "Failed to start scan: %d", err);
			return err;
		}
	}

	return ESP_OK;
}

static esp_err_t wlan_station_start_scan(void) {
	esp_err_t err;

	wlan_station_lock();
	err = wlan_station_start_scan_();
	wlan_station_unlock();
	return err;
}

static void wlan_station_schedule_scan_cb(void *ctx);
static void wlan_station_schedule_scan(unsigned int deadline_ms) {
	scheduler_schedule_task_relative(&scan_task, wlan_station_schedule_scan_cb, NULL, MS_TO_US((uint32_t)deadline_ms));
}

static void wlan_station_schedule_scan_conditional(void) {
	if (sta_enabled && !sta_connected) {
		unsigned int scan_interval_ms = SCAN_INTERVAL_DEFAULT_MS;

		if (wlan_ap_is_active()) {
			scan_interval_ms = SCAN_INTERVAL_AP_ACTIVE;

			if (wlan_ap_get_num_connected_stations() > 0) {
				scan_interval_ms = SCAN_INTERVAL_AP_CONNECTED;
			}
		}

		wlan_station_schedule_scan(scan_interval_ms);
	}
}

static void wlan_station_schedule_scan_cb(void *ctx) {
	if (wlan_station_start_scan() && !sta_connected) {
		wlan_station_schedule_scan_conditional();
	}
}

static esp_err_t update_ap_list_(void) {
	uint16_t num_aps = 0;
	esp_err_t err;

	err = esp_wifi_scan_get_ap_num(&num_aps);
	if (err) {
		ESP_LOGE(TAG, "Failed to get number of APs after scan: %d", err);
		return err;
	}

	if (discovered_aps) {
		free(discovered_aps);
		num_discovered_aps = 0;
	}

	if (num_aps) {
		discovered_aps = calloc(num_aps, sizeof(wifi_ap_record_t));
		if (!discovered_aps) {
			return -ESP_ERR_NO_MEM;
		}

		err = esp_wifi_scan_get_ap_records(&num_aps, discovered_aps);
		if (err) {
			ESP_LOGE(TAG, "Failed to get AP records: %d", err);
			esp_wifi_clear_ap_list();
			return err;
		}
	} else {
		esp_wifi_clear_ap_list();
	}

	num_discovered_aps = num_aps;
	ESP_LOGI(TAG, "Found %u APs", num_aps);
	return ESP_OK;
}

static void connect_to_ap_(void) {
	unsigned int i;

	for (i = 0; i < num_discovered_aps; i++) {
		wifi_ap_record_t *ap = &discovered_aps[i];

		ESP_LOGI(TAG, "'%s' == '%s'?", ap->ssid, STR_NULL(sta_ssid));
		if (!strcmp_null(ap->ssid, sta_ssid)) {
			ESP_LOGI(TAG, "Trying to connect to %s", STR_NULL(sta_ssid));
			if (!sta_connected) {
				esp_err_t err;

				err = esp_wifi_connect();
				if (!err) {
					return;
				} else {
					ESP_LOGW(TAG, "Failed to connect to %s: %d", STR_NULL(sta_ssid), err);
				}
			}
		}
	}
}

static void handle_scan_done_event(void) {
	esp_err_t err;

	wlan_station_lock();
	scan_in_progress = false;
	err = update_ap_list_();
	if (err) {
		ESP_LOGW(TAG, "Failed to update list of discovered APs: %d", err);
	} else {
		connect_to_ap_();
	}
	wlan_station_schedule_scan_conditional();
	wlan_station_unlock();
}

static void handle_station_connected_event(void) {
	wlan_station_lock();
	sta_connected = true;
	scheduler_abort_task(&scan_task);
	wlan_station_unlock();
	ESP_LOGI(TAG, "Connected to AP");
	esp_netif_create_ip6_linklocal(wlan_sta_iface);
	event_bus_notify("wlan_station", NULL);
}

static void handle_station_disconnected_event(void) {
	wlan_station_lock();
	sta_connected = false;
	wlan_station_schedule_scan_conditional();
	wlan_station_unlock();
	ESP_LOGI(TAG, "Disconnected from AP");
	event_bus_notify("wlan_station", NULL);
}

static void sta_event_handler(void *arg, esp_event_base_t event_base,
			      int32_t event_id, void *event_data) {
	assert(event_base == WIFI_EVENT);

	switch (event_id) {
	case WIFI_EVENT_SCAN_DONE:
		handle_scan_done_event();
		break;
	case WIFI_EVENT_STA_CONNECTED:
		handle_station_connected_event();
		break;
	case WIFI_EVENT_STA_DISCONNECTED:
		handle_station_disconnected_event();
		break;
	}
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
			     int32_t event_id, void *event_data) {
	assert(event_base == IP_EVENT);

	if (event_id == IP_EVENT_GOT_IP6 ||
	    event_id == IP_EVENT_STA_GOT_IP) {
		event_bus_notify("wlan_station", NULL);
	}
}

static void wlan_station_enable_(void) {
	wifi_config_t wifi_config = { 0 };

	if (sta_ssid && sta_psk) {
		strncpy((char *)wifi_config.sta.ssid, sta_ssid, sizeof(wifi_config.sta.ssid) - 1);
		strncpy((char *)wifi_config.sta.password, sta_psk, sizeof(wifi_config.sta.password) - 1);
	}

	sta_enabled = true;
	scheduler_abort_task(&scan_task);
	sta_connected = false;
	wlan_stop();
	wlan_reconfigure();
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
	wlan_start();
	scan_in_progress = false;
	event_bus_notify("wlan_station", NULL);
	wlan_station_schedule_scan(0);
}

static void on_vendor_event(void *priv, void *data) {
	vendor_lock();
	esp_netif_set_hostname(wlan_sta_iface, vendor_get_hostname_());
	vendor_unlock();
}

void wlan_station_init() {
	sta_lock = xSemaphoreCreateRecursiveMutexStatic(&sta_lock_buffer);

	wlan_sta_iface = esp_netif_create_default_wifi_sta();
	ESP_ERROR_CHECK(!wlan_sta_iface);
	vendor_lock();
	esp_netif_set_hostname(wlan_sta_iface, vendor_get_hostname_());
	vendor_unlock();

	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
							    &sta_event_handler, NULL, NULL));

	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
							    &ip_event_handler, NULL, NULL));

	scheduler_task_init(&scan_task);

	sta_ssid = settings_get_wlan_station_ssid();
	sta_psk = settings_get_wlan_station_psk();

	sta_enabled = settings_get_wlan_station_enable();
	if (sta_enabled) {
		wlan_station_enable_();
	}
	event_bus_subscribe(&vendor_event_handler, "vendor", on_vendor_event, NULL);
}

bool wlan_station_is_enabled() {
	return sta_enabled;
}

bool wlan_station_is_connected() {
	return sta_connected;
}

void wlan_station_lock() {
	xSemaphoreTakeRecursive(sta_lock, portMAX_DELAY);
}

void wlan_station_unlock() {
	xSemaphoreGiveRecursive(sta_lock);
}

static void wlan_station_disable_(void) {
	if (sta_enabled) {
		sta_enabled = false;
		sta_connected = false;
		scheduler_abort_task(&scan_task);
		wlan_restart();
		scan_in_progress = false;
		event_bus_notify("wlan_station", NULL);
	}
}

static void wlan_station_reconfigure_(void) {
	if (sta_enabled) {
		wlan_station_disable_();
		wlan_station_enable_();
	}
}

static void wlan_station_set_ssid_(const char *ssid) {
	bool ssid_change = !strcmp_null(sta_ssid, ssid);

	if (sta_ssid) {
		free(sta_ssid);
		sta_ssid = NULL;
	}
	if (ssid) {
		sta_ssid = strdup(ssid);
	}
	if (ssid_change) {
		wlan_station_reconfigure_();
	}
	settings_set_wlan_station_ssid(ssid);
}

void wlan_station_set_ssid(const char *ssid) {
	wlan_station_lock();
	wlan_station_set_ssid_(ssid);
	wlan_station_unlock();
}

static void wlan_station_set_psk_(const char *psk) {
	bool psk_change = !strcmp_null(sta_psk, psk);

	if (sta_psk) {
		free(sta_psk);
		sta_psk = NULL;
	}
	if (psk) {
		sta_psk = strdup(psk);
	}
	if (psk_change) {
		wlan_station_reconfigure_();
	}
	settings_set_wlan_station_psk(psk);
}

void wlan_station_set_psk(const char *psk) {
	wlan_station_lock();
	wlan_station_set_psk_(psk);
	wlan_station_unlock();
}

void wlan_station_enable() {
	wlan_station_lock();
	wlan_station_enable_();
	settings_set_wlan_station_enable(true);
	wlan_station_unlock();
}

void wlan_station_disable() {
	wlan_station_lock();
	wlan_station_disable_();
	settings_set_wlan_station_enable(false);
	wlan_station_unlock();
}

const char *wlan_station_get_ssid_(void) {
	return sta_ssid;
}

esp_err_t wlan_station_get_ipv4_address(esp_netif_ip_info_t *ip_info) {
	return esp_netif_get_ip_info(wlan_sta_iface, ip_info);
}

int wlan_station_get_ipv6_addresses(esp_ip6_addr_t *addresses, unsigned int num_addresses) {
	if (num_addresses < CONFIG_LWIP_IPV6_NUM_ADDRESSES) {
		return -EINVAL;
	}

	return esp_netif_get_all_ip6(wlan_sta_iface, addresses);
}
