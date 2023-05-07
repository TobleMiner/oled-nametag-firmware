#include "i2c_bus.h"

#include <stdlib.h>
#include <string.h>

#include <esp_log.h>

static const char *TAG = "i2c bus";

esp_err_t i2c_bus_init(i2c_port_t i2c_port, unsigned int gpio_sda, unsigned int gpio_scl, uint32_t speed) {
	i2c_config_t i2c_config = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = gpio_sda,
		.scl_io_num = gpio_scl,
		.master.clk_speed = speed,
	};

	esp_err_t err = i2c_param_config(i2c_port, &i2c_config);
	if(err) {
		return err;
	}
	return i2c_driver_install(i2c_port, I2C_MODE_MASTER, 0, 0, 0);
}

esp_err_t i2c_bus_scan(i2c_port_t i2c_port, i2c_address_set_t addr) {
	esp_err_t err = ESP_OK;
	size_t link_buf_size = I2C_LINK_RECOMMENDED_SIZE(1);
	void *link_buf = malloc(link_buf_size);
	if (!link_buf) {
		return ESP_ERR_NO_MEM;
	}

	for (uint8_t i = 0; i < 128; i++) {
		I2C_ADDRESS_SET_CLEAR(addr, i);
		i2c_cmd_handle_t cmd = i2c_cmd_link_create_static(link_buf, link_buf_size);
		if(!cmd) {
			err = ESP_ERR_NO_MEM;
			continue;
		}
		if((err = i2c_master_start(cmd))) {
			goto fail_link;
		}
		if((err = i2c_master_write_byte(cmd, (i << 1), true))) {
			goto fail_link;
		}
		if((err = i2c_master_stop(cmd))) {
			goto fail_link;
		}
		esp_err_t nacked = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(100));
		if(!nacked) {
			I2C_ADDRESS_SET_SET(addr, i);
		}
		i2c_cmd_link_delete_static(cmd);
		continue;
fail_link:
		i2c_cmd_link_delete_static(cmd);
		break;
	}
	free(link_buf);
	return err;
}

void i2c_detect(i2c_port_t i2c_port) {
	ESP_LOGI(TAG, "Scanning i2c bus %d for devices", i2c_port);
	I2C_ADDRESS_SET(devices);
	esp_err_t err = i2c_bus_scan(i2c_port, devices);
	if (err) {
		ESP_LOGE(TAG, "Failed to scan bus %d: %d", i2c_port, err);
	} else {
		ESP_LOGI(TAG, "=== Detected devices ===");
		for (uint8_t i = 0; i < 128; i++) {
			if (I2C_ADDRESS_SET_CONTAINS(devices, i)) {
				ESP_LOGI(TAG, "  0x%02x", i);
			}
		}
		ESP_LOGI(TAG, "========================");
	}
}
