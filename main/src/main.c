#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "api.h"
#include "ambient_light_sensor.h"
#include "battery_gauge.h"
#include "buttons.h"
#include "charger.h"
#include "charging_screen.h"
#include "embedded_files.h"
#include "event_bus.h"
#include "flash.h"
#include "fonts.h"
#include "gifplayer.h"
#include "gui.h"
#include "i2c_bus.h"
#include "menutree.h"
#include "nvs.h"
#include "pixelflut/pixelflut.h"
#include "power.h"
#include "scheduler.h"
#include "settings.h"
#include "webserver.h"
#include "wlan_settings.h"
#include "wlan.h"
#include "wlan_ap.h"

#define GPIO_SPI_MOSI	36
#define GPIO_SPI_CLK	33
#define GPIO_OLED_DC	34
#define GPIO_OLED_RST	21
#define GPIO_OLED_CS	18
#define GPIO_OLED_VCC	14

#define SPI_OLED_HOST	SPI2_HOST

static const char *TAG = "main";

void oled_spi_pre_transfer_cb(spi_transaction_t *t)
{
	int dc = (int)t->user;
	gpio_set_level(GPIO_OLED_DC, dc);
}

#define OLED_CMD(cmd_, ...) do {						\
	spi_transaction_t t = { 0 };						\
	const uint8_t data[] = { __VA_ARGS__ };					\
										\
	/* Command */								\
	t.length = 8;								\
	t.flags = SPI_TRANS_USE_TXDATA;						\
	t.tx_data[0] = cmd_;							\
	t.user = (void *)0;							\
	/* gpio_set_level(GPIO_OLED_DC, 0); */					\
	ESP_ERROR_CHECK(spi_device_polling_transmit(spidev, &t));		\
	/* Data */								\
	if (sizeof(data)) {							\
		memset(&t, 0, sizeof(t));					\
		t.length = sizeof(data) * 8;					\
		t.tx_buffer = data;						\
		t.user = (void *)1;						\
		/* gpio_set_level(GPIO_OLED_DC, 1); */				\
		ESP_ERROR_CHECK(spi_device_polling_transmit(spidev, &t));	\
	}									\
} while (0)

void oled_init(spi_device_handle_t spidev)
{
	// Reset OLED
	gpio_set_level(GPIO_OLED_RST, 0);
	vTaskDelay(pdMS_TO_TICKS(10));
	gpio_set_level(GPIO_OLED_RST, 1);
	vTaskDelay(pdMS_TO_TICKS(100));

	OLED_CMD(0xFD, 0x12);		// Unlock IC for writing
	OLED_CMD(0xAE);			// Sleep mode on
//	OLED_CMD(0xB3, 0xBF);		// Set clock divider
//	OLED_CMD(0xB3, 0x91);		// Set clock divider
	OLED_CMD(0xB3, 0xA1);		// Set clock divider
	OLED_CMD(0xCA, 0x3F);		// Set multiplexing ratio (1/64)
	OLED_CMD(0xA2, 0x00);		// Zero out display offset
	OLED_CMD(0xA1, 0x00);		// Zero out starting line
	OLED_CMD(0xA0, 0x14, 0x11);	// Horizontal address increment, Nibble remap, Reverse COM scan direction, Dual COM mode
	OLED_CMD(0x15, 28, 91); 	// Not all common pins are used
	OLED_CMD(0xB5, 0x00);		// Disable GPIOs
	OLED_CMD(0xAB, 0x01);		// Enable internal VDD regulator
	OLED_CMD(0xB4, 0xA0, 0xFD);	// Use internal VSL, Enhanced display quality
//	OLED_CMD(0xC1, 0xFF);		// Maximum contrast
	OLED_CMD(0xC1, 0x9F);		// Maximum contrast
//	OLED_CMD(0xC1, 0x22);		// Maximum contrast
	OLED_CMD(0xC7, 0x0F);		// Maximum drive current
	OLED_CMD(0xB9);			// Enable grayscale mode
	OLED_CMD(0xB1, 0xE2);		// Phase length, phase1 = 5 DCLK, phase2 = 14 DCLK
	OLED_CMD(0xD1, 0x82, 0x20);	// Display enhance, Reserved
	OLED_CMD(0xBB, 0x1F);		// Precharge voltage = 0.6 * VCC
	OLED_CMD(0xB6, 0x08);		// Precharge period = 8 DCLK
	OLED_CMD(0xBE, 0x07);		// VCOMH voltage = 0.86 * VCC
	OLED_CMD(0xA6);			// Normal display mode
	OLED_CMD(0xA9);			// Exit partial display mode
	OLED_CMD(0xAF);			// Sleep mode off

	vTaskDelay(pdMS_TO_TICKS(100));
}

static void oled_write_image(spi_device_handle_t spidev, const uint8_t *image, unsigned int slot)
{
	spi_transaction_t t = { 0 };

	if (slot) {
		OLED_CMD(0x75, 64, 127); // Select slot 1
	} else {
		OLED_CMD(0x75, 0, 63); // Select slot 0
	}
	OLED_CMD(0x5C); // Write GDDRAM
	t.length = 256 * 64 * 4;
	t.tx_buffer = image;
	t.user = (void *)1;
	ESP_ERROR_CHECK(spi_device_polling_transmit(spidev, &t));
}

static void oled_show_image(spi_device_handle_t spidev, unsigned int slot) {
	if (slot) {
		OLED_CMD(0xA1, 64); // Select slot 1
	} else {
		OLED_CMD(0xA1, 0); // Select slot 0
	}
}

static pixelflut_t pixelflut;
static uint8_t oled_fb[8192] = { 0 };
static uint8_t gui_render_fb[256 * 64];

static inline unsigned int rgb_to_grayscale(const uint8_t *rgb) {
	return (rgb[0] + rgb[1] + rgb[2]) / 3;
}

static inline unsigned int rgb_pixel_to_grayscale(union fb_pixel px) {
	unsigned int red = px.color.color_bgr.red;
	unsigned int green = px.color.color_bgr.green;
	unsigned int blue = px.color.color_bgr.blue;
	unsigned int avg = (red + green + blue) / 3;
	return avg;
}

static void fb_convert(uint8_t *grayscale, const uint8_t *rgb888) {
	for (int y = 0; y < 64; y++) {
		for (int x = 0; x < 256 / 2; x++) {
			unsigned int int1 = rgb_to_grayscale(rgb888 + (y * 256 + x * 2 + 1) * 3);
			unsigned int int2 = rgb_to_grayscale(rgb888 + (y * 256 + x * 2 + 0) * 3);
			grayscale[y * 128 + x] = (int1 >> 4) | (int2 & 0xf0);
		}
	}
}

static void fb_convert_grayscale(uint8_t *stuffed_4bit, const uint8_t *grayscale) {
	for (int y = 0; y < 64; y++) {
		for (int x = 0; x < 256 / 2; x++) {
			unsigned int int1 = grayscale[y * 256 + x * 2 + 1];
			unsigned int int2 = grayscale[y * 256 + x * 2 + 0];
			stuffed_4bit[y * 128 + x] = (int1 >> 4) | (int2 & 0xf0);
		}
	}
}

gui_t gui;

static button_event_handler_t reset_button_event_handler;
static bool handle_reset_button_press(const button_event_t *event, void *priv) {
	ESP_LOGI(TAG, "Reset button pressed");
	esp_restart();
	return false;
}

TaskHandle_t main_task;

static void gui_request_render(const gui_t *gui) {
	xTaskNotifyGive(main_task);
}

const gui_ops_t gui_ops = {
	.request_render = gui_request_render
};

menu_t *menu;

void button_emulator_event_loop(void *arg) {
	uint8_t keybuf[3] = { 0 };
	while (1) {
		uint8_t byt;
		ssize_t ret = fread(&byt, 1, 1, stdin);
		if (ret > 0) {
			ESP_LOGI(TAG, "Read byte: 0x%02x", byt);
			keybuf[0] = byt;
			if (keybuf[2] == 0x1b && keybuf[1] == 0x5b) {
				switch(keybuf[0]) {
				case 0x41: //up
					buttons_emulate_press(BUTTON_UP, 100);
					break;
				case 0x42: //down
					buttons_emulate_press(BUTTON_DOWN, 100);
					break;
				case 0x44: //left
					buttons_emulate_press(BUTTON_EXIT, 100);
					break;
				case 0x43: //right
					buttons_emulate_press(BUTTON_ENTER, 100);
					break;
				}
			}
			memmove(&keybuf[1], &keybuf[0], 2);
		}
		vTaskDelay(1);
	}
}

void power_on_cb(void) {
	menu_show(menu);
}

void app_main(void)
{
	esp_err_t ret;
	spi_device_handle_t spidev;
	spi_bus_config_t buscfg = {
		.miso_io_num = -1,
		.mosi_io_num = GPIO_SPI_MOSI,
		.sclk_io_num = GPIO_SPI_CLK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = 8192
	};

	spi_device_interface_config_t devcfg = {
		.clock_speed_hz = 10 * 1000 * 1000,	// Clock out at 10 MHz
		.mode = 3,				// SPI mode 3
		.spics_io_num = GPIO_OLED_CS,		// CS pin
		.queue_size = 2,			// We want to be able to queue 7 transactions at a time
		.pre_cb = oled_spi_pre_transfer_cb	// Specify pre-transfer callback to handle D/~C line
	};

	// Ensure we keep the lights on
	power_init();

	// Setup GPIOs
	gpio_set_direction(GPIO_OLED_DC, GPIO_MODE_OUTPUT);
	gpio_set_direction(GPIO_OLED_RST, GPIO_MODE_OUTPUT);
	gpio_set_direction(GPIO_OLED_VCC, GPIO_MODE_OUTPUT);

	main_task = xTaskGetCurrentTaskHandle();

	// Initialize SPI bus
	ret = spi_bus_initialize(SPI_OLED_HOST, &buscfg, SPI_DMA_CH_AUTO);
	ESP_ERROR_CHECK(ret);
	// Attach OLED to SPI bus
	ret = spi_bus_add_device(SPI_OLED_HOST, &devcfg, &spidev);
	ESP_ERROR_CHECK(ret);
	// Initialize LCD
	oled_init(spidev);

	// Power up display
	gpio_set_level(GPIO_OLED_VCC, 1);

	// Initialize event bus
	event_bus_init();

	// Initialize charger readouts
	charger_init();

	// Setup scheduler
	scheduler_init();

	// Initialize I2C bus
	ESP_ERROR_CHECK(i2c_bus_configure());
	i2c_detect(I2C_NUM_0);

	// Setup NVS
	nvs_init();

	// Setup settings in NVS
	settings_init();

	// Setup WLAN
	wlan_init();

	// Setup WLAN AP
	wlan_ap_init();

	// Mount main fat storage
	ESP_ERROR_CHECK(flash_fatfs_mount("flash", "/flash"));

	// Setup buttons
	buttons_init();
	const button_event_handler_single_user_cfg_t reset_button_cfg = {
		.base = {
			.cb = handle_reset_button_press,
		},
		.single = {
			.button = BUTTON_EXIT,
			.action = BUTTON_ACTION_HOLD,
			.min_hold_duration_ms = 3000,
		}
	};
	buttons_register_single_button_event_handler(&reset_button_event_handler, &reset_button_cfg);

	// Load fonts
	fonts_init();

	// Initialize GUI
	gui_init(&gui, NULL, &gui_ops);

	// Setup i2c bus UIs
	i2c_bus_init(&gui);

	// Setup gifplayer
	gifplayer_init(&gui);

	// Setup wifi settings
	wlan_settings_init(&gui);

	// Setup charging screen
	charging_screen_init(&gui, power_on_cb);

	// Initialize ambient light sensor
	ambient_light_sensor_init(&gui);

	// Initialize battery gauge
	battery_gauge_init();

	// Setup menu
	menu = menutree_init(&gui.container, &gui);
	if (power_is_usb_connected()) {
		ESP_LOGI(TAG, "USB connected, switching to charging screen");
		power_off();
		charging_screen_show();
	} else {
		ESP_LOGI(TAG, "On battery, switching to main menu");
		menu_show(menu);
	}

	// Setup webserver
	httpd_t *httpd = webserver_preinit();
	api_init(httpd);
	webserver_init(httpd);

	// Setup pixelflut
	ESP_ERROR_CHECK(pixelflut_init(&pixelflut, 256, 64, 8192));

	// Start listening
	ESP_ERROR_CHECK(pixelflut_listen(&pixelflut));

	// Clear screen buffer
	oled_write_image(spidev, oled_fb, 0);
	oled_write_image(spidev, oled_fb, 1);

	// Start polling input
	ESP_ERROR_CHECK(xTaskCreate(button_emulator_event_loop, "button_emulator_event_loop", 4096, NULL, 10, NULL) != pdPASS);

	bool slot = false;
	int render_ret = -1;
	while (1) {
		const gui_point_t render_size = {
			256,
			64
		};
		if (render_ret < 0) {
			ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		} else if (!render_ret) {
			ulTaskNotifyTake(pdTRUE, 0);
		} else {
			ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(render_ret));
		}
		gui_lock(&gui);
		render_ret = gui_render(&gui, gui_render_fb, 256, &render_size);
		gui_unlock(&gui);
		fb_convert_grayscale(oled_fb, gui_render_fb);
		slot = !slot;
		oled_write_image(spidev, oled_fb, slot ? 1 : 0);
		oled_show_image(spidev, slot ? 1 : 0);
	}
}
