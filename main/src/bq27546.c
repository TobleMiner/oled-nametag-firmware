#include "bq27546.h"

#include <endian.h>
#include <stdint.h>

#include <esp_log.h>

#define CMD_CONTROL			0x00
#define CMD_TEMPERATURE_0_1K		0x06
#define CMD_CELL_VOLTAGE_MV		0x08
#define CMD_AVERAGE_CURRENT_MA		0x14
#define CMD_TIME_TO_EMPTY		0x16
#define CMD_FULL_CHARGE_CAPACITY	0x18
#define CMD_REMAINING_CAPACITY		0x22
#define CMD_STATE_OF_CHARGE		0x2c
#define CMD_STATE_OF_HEALTH		0x2e

#define SUBCMD_DEVICE_TYPE		0x0001
#define SUBCMD_FW_VERSION		0x0002
#define SUBCMD_HW_VERSION		0x0003

#define ADDRESS	0x55
#define CHIP_ID 0x0546

static const char *TAG = "BQ27546";

static void lock(bq27546_t *bq) {
	xSemaphoreTake(bq->lock, portMAX_DELAY);
}

static void unlock(bq27546_t *bq) {
	xSemaphoreGive(bq->lock);
}

static esp_err_t read_word_subcmd(bq27546_t *bq, uint8_t cmd, uint16_t subcmd, uint16_t *word) {
	esp_err_t err;
	uint8_t buf[3] = { cmd };

	le16enc(&buf[1], subcmd);
	lock(bq);
	err = i2c_master_write_to_device(bq->port, ADDRESS, buf, sizeof(buf), pdMS_TO_TICKS(100));
	if (err) {
		unlock(bq);
		return err;
	}

	err = i2c_master_write_read_device(bq->port, ADDRESS, &cmd, 1, buf, 2, pdMS_TO_TICKS(100));
	unlock(bq);
	if (!err) {
		*word = le16dec(buf);
	}
	return err;
}

static esp_err_t read_word(bq27546_t *bq, uint8_t cmd, uint16_t *word) {
	esp_err_t err;
	uint8_t buf[2];

	lock(bq);
	err = i2c_master_write_read_device(bq->port, ADDRESS, &cmd, 1, buf, sizeof(buf), pdMS_TO_TICKS(100));
	unlock(bq);
	if (!err) {
		*word = le16dec(buf);
	}
	return err;
}

esp_err_t bq27546_init(bq27546_t *bq, i2c_port_t port) {
	esp_err_t err;
	uint16_t gauge_id, fw_version, hw_version;

	bq->port = port;
	bq->lock = xSemaphoreCreateMutexStatic(&bq->lock_buffer);

	err = read_word_subcmd(bq, CMD_CONTROL, SUBCMD_DEVICE_TYPE, &gauge_id);
	if (err) {
		ESP_LOGE(TAG, "Failed to read gauge id: %d", err);
		return err;
	}

	if (gauge_id != CHIP_ID) {
		ESP_LOGE(TAG, "Invalid gauge id, expected 0x%04x but got 0x%04x", CHIP_ID, gauge_id);
		return ESP_ERR_NOT_SUPPORTED;
	}

	err = read_word_subcmd(bq, CMD_CONTROL, SUBCMD_FW_VERSION, &fw_version);
	if (err) {
		ESP_LOGE(TAG, "Failed to read gauge firmware version: %d", err);
		return err;
	}

	err = read_word_subcmd(bq, CMD_CONTROL, SUBCMD_HW_VERSION, &hw_version);
	if (err) {
		ESP_LOGE(TAG, "Failed to read gauge hardware version: %d", err);
		return err;
	}

	ESP_LOGI(TAG, "BQ27546 battery gauge @0x%02x, hardware version %u, firmware version %u", ADDRESS, hw_version, fw_version);

	return 0;
}

static int bq27546_get_unsigned_param(bq27546_t *bq, uint8_t cmd) {
	uint16_t param;
	esp_err_t err;

	err = read_word(bq, cmd, &param);
	if (err) {
		if (err > 0) {
			err = -err;
		}
		return err;
	}

	return param;
}

int bq27546_get_voltage_mv(bq27546_t *bq) {
	return bq27546_get_unsigned_param(bq, CMD_CELL_VOLTAGE_MV);
}

esp_err_t bq27546_get_current_ma(bq27546_t *bq, int *current_ma_out) {
	esp_err_t err;
	uint16_t ucurrent_ma;
	int16_t current_ma;

	err = read_word(bq, CMD_AVERAGE_CURRENT_MA, &ucurrent_ma);
	if (err) {
		return err;
	}
	current_ma = (int16_t)ucurrent_ma;
	*current_ma_out = current_ma;
	return 0;
}

int bq27546_get_state_of_charge_percent(bq27546_t *bq) {
	int val = bq27546_get_unsigned_param(bq, CMD_STATE_OF_CHARGE);

	if (val > 100) {
		return ESP_ERR_INVALID_RESPONSE;
	}
	return val;
}

int bq27546_get_state_of_health_percent(bq27546_t *bq) {
	int val = bq27546_get_unsigned_param(bq, CMD_STATE_OF_HEALTH);

	if (val > 100) {
		return ESP_ERR_INVALID_RESPONSE;
	}
	return val;
}

int bq27546_get_time_to_empty_min(bq27546_t *bq) {
	return bq27546_get_unsigned_param(bq, CMD_TIME_TO_EMPTY);
}

int bq27546_get_temperature_0_1k(bq27546_t *bq) {
	return bq27546_get_unsigned_param(bq, CMD_TEMPERATURE_0_1K);
}


int bq27546_get_full_charge_capacity_mah(bq27546_t *bq) {
	return bq27546_get_unsigned_param(bq, CMD_FULL_CHARGE_CAPACITY);
}

int bq27546_get_remaining_capacity_mah(bq27546_t *bq) {
	return bq27546_get_unsigned_param(bq, CMD_REMAINING_CAPACITY);
}
