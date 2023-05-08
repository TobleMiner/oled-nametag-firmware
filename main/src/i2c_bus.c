#include "i2c_bus.h"

#include <stdlib.h>
#include <string.h>

#include <esp_log.h>
#include <driver/gpio.h>

#include "ambient_light_sensor.h"
#include "battery_gauge.h"

static const char *TAG = "i2c bus";

#define	I2C_BUS		I2C_NUM_0
#define I2C_SPEED	100000
#define GPIO_SDA	3
#define GPIO_SCL	2

static menu_cb_f menu_cb;
static void *menu_cb_ctx;

static gui_container_t app_container;
static gui_label_t app_info_label;

static button_event_handler_t button_event_handler;

static bool on_button_event(const button_event_t *event, void *priv) {
	if (event->button == BUTTON_EXIT) {
		i2c_bus_configure();
		ambient_light_sensor_start();
		battery_gauge_start();

		buttons_disable_event_handler(&button_event_handler);
		gui_element_set_hidden(&app_container.element, true);
		menu_cb(menu_cb_ctx);
		return true;
	}

	return false;
}

void i2c_bus_init(gui_t *gui) {
	button_event_handler_multi_user_cfg_t button_event_cfg = {
		.base = {
			.cb = on_button_event
		},
		.multi = {
			.button_filter = (1 << BUTTON_EXIT),
			.action_filter = (1 << BUTTON_ACTION_RELEASE)
		}
	};

	gui_container_init(&app_container);
	gui_element_set_size(&app_container.element, 256, 64);
	gui_element_set_hidden(&app_container.element, true);
	gui_element_add_child(&gui->container.element, &app_container.element);

	gui_label_init(&app_info_label, "= I2C bus released =");
	gui_label_set_font_size(&app_info_label, 15);
	gui_label_set_text_offset(&app_info_label, 3, 0);
	gui_element_set_size(&app_info_label.element, 200, 25);
	gui_element_add_child(&app_container.element, &app_info_label.element);

	buttons_register_multi_button_event_handler(&button_event_handler, &button_event_cfg);
}

esp_err_t i2c_bus_configure() {
	i2c_config_t i2c_config = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = GPIO_SDA,
		.scl_io_num = GPIO_SCL,
		.master.clk_speed = I2C_SPEED,
	};

	esp_err_t err = i2c_param_config(I2C_BUS, &i2c_config);
	if(err) {
		return err;
	}
	return i2c_driver_install(I2C_BUS, I2C_MODE_MASTER, 0, 0, 0);
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

int i2c_bus_disable_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	const gpio_config_t i2c_float_conf = {
		.pin_bit_mask = (1ULL << GPIO_SCL) | (1ULL << GPIO_SDA),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE
	};

	menu_cb = exit_cb;
	menu_cb_ctx = cb_ctx;

	gui_element_set_hidden(&app_container.element, false);
	gui_element_show(&app_container.element);
	buttons_enable_event_handler(&button_event_handler);

	ambient_light_sensor_stop();
	battery_gauge_stop();
	i2c_driver_delete(I2C_BUS);
	gpio_config(&i2c_float_conf);
	return 0;
}
