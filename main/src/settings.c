#include "settings.h"

#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

static const char *TAG = "settings";

static nvs_handle_t nvs;

void settings_init() {
	esp_err_t err;

	err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);

	ESP_ERROR_CHECK(nvs_open("settings", NVS_READWRITE, &nvs));
}

static const char *ellipsize_key(const char *key) {
	if (strlen(key) >= NVS_KEY_NAME_MAX_SIZE) {
		key += strlen(key) - NVS_KEY_NAME_MAX_SIZE + 1;
		ESP_LOGW(TAG, "Ellipsized NVS key to \"%s\"", key);
	}

	return key;
}

static char *nvs_get_string(const char *key) {
	char *str;
	size_t len;
	esp_err_t err;

	key = ellipsize_key(key);

	err = nvs_get_str(nvs, key, NULL, &len);
	if (err) {
		if (err != ESP_ERR_NVS_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to load string size '%s' from NVS: %d", key, err);
		}
		return NULL;
	}

	str = calloc(1, len);
	if (!str) {
		return NULL;
	}
	err = nvs_get_str(nvs, key, str, &len);
	if (err) {
		if (err != ESP_ERR_NVS_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to load string '%s' from NVS: %d", key, err);
		}
		free(str);
		return NULL;
	}

	return str;
}

static void nvs_set_string(const char *key, const char *value) {
	key = ellipsize_key(key);

	if (value) {
		esp_err_t err = nvs_set_str(nvs, key, value);
		if (err) {
			ESP_LOGE(TAG, "Failed to store string '%s' to NVS: %d", key, err);
		}
	} else {
		nvs_erase_key(nvs, key);
	}
}

static bool nvs_get_bool(const char *key, bool default_value) {
	esp_err_t err;
	uint8_t val;

	key = ellipsize_key(key);
	err = nvs_get_u8(nvs, key, &val);
	if (err) {
		if (err != ESP_ERR_NVS_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to load bool '%s' from NVS: %d", key, err);
		}
		return default_value;
	}

	return !!val;
}

static void nvs_set_bool(const char *key, bool value) {
	esp_err_t err;

	key = ellipsize_key(key);
	err = nvs_set_u8(nvs, key, value ? 1 : 0);
	if (err) {
		ESP_LOGE(TAG, "Failed to store bool '%s' to NVS: %d", key, err);
	}
}

void settings_set_default_animation(const char *str) {
	nvs_set_string("DefAnimFile", str);
}

char *settings_get_default_animation(void) {
	return nvs_get_string("DefAnimFile");
}

void settings_set_default_app(const char *app) {
	nvs_set_string("DefApp", app);
}

char *settings_get_default_app(void) {
	return nvs_get_string("DefApp");
}

void settings_set_wlan_ap_psk(const char *app) {
	nvs_set_string("WlanApPsk", app);
}

char *settings_get_wlan_ap_psk(void) {
	return nvs_get_string("WlanApPsk");
}

void settings_set_wlan_ap_enable(bool enable) {
	nvs_set_bool("WlanApEn", enable);
}

bool settings_get_wlan_ap_enable(void) {
	return nvs_get_bool("WlanApEn", true);
}
