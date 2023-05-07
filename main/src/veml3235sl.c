#include "veml3235sl.h"

#include <endian.h>
#include <stdint.h>

#include <esp_log.h>

#define REG_CONFIG	0x00
#define REG_WHITE	0x04
#define REG_ALS		0x05
#define REG_ID		0x09

#define ID_3235		0b00110101

#define ADDRESS		0x10

static const char *TAG = "VEML3235SL";

static esp_err_t read_word(veml3235sl_t *veml, uint8_t reg, uint16_t *word) {
	esp_err_t err;
	uint8_t buf[2];

	err = i2c_master_write_read_device(veml->port, ADDRESS, &reg, 1, buf, sizeof(buf), pdMS_TO_TICKS(100));
	if (!err) {
		*word = le16dec(buf);
	}
	return err;
}

static esp_err_t write_word(veml3235sl_t *veml, uint8_t reg, uint16_t word) {
	uint8_t buf[3] = { reg, 0, 0 };

	le16enc(&buf[1], word);
	return i2c_master_write_to_device(veml->port, ADDRESS, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

esp_err_t veml3235sl_init(veml3235sl_t *veml, i2c_port_t port) {
	esp_err_t err;
	uint16_t id;

	veml->port = port;

	err = read_word(veml, REG_ID, &id);
	if (err) {
		ESP_LOGE(TAG, "Failed to read chip id");
		return err;
	}
	id &= 0xff;

	if (id != ID_3235) {
		ESP_LOGE(TAG, "Invalid id, expected 0x%02x but got 0x%02x", ID_3235, id);
		return ESP_ERR_NOT_SUPPORTED;
	}

	// Power up sensor
	err = write_word(veml, REG_CONFIG, 0x0120);
	if (err) {
		ESP_LOGE(TAG, "Failed to power up chip");
	}

	veml->gain = VEML3235SL_GAIN_X1;
	veml->integration_time = VEML3235SL_INTEGRATION_TIME_200MS;

	return err;
}

int32_t veml3235sl_get_brightness_mlux(veml3235sl_t *veml, veml3235sl_detection_mode_t mode) {
	uint8_t reg = REG_WHITE;
	uint16_t result;
	esp_err_t err;

	if (mode == VEML3235SL_DETECT_ALS) {
		reg = REG_ALS;
	}

	err = read_word(veml, reg, &result);
	if (err) {
		if (err > 0) {
			err = -err;
		}
		return err;
	}

	return (int32_t)result * 68;
}
