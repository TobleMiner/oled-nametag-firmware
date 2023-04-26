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

#include "flash.h"
#include "pixelflut/pixelflut.h"
#include "webserver.h"

#define GPIO_SPI_MOSI	11
#define GPIO_SPI_CLK	12
#define GPIO_OLED_DC	10
#define GPIO_OLED_RST	 9
#define GPIO_OLED_CS	46

#define SPI_OLED_HOST	SPI2_HOST

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
	OLED_CMD(0xB3, 0xBF);		// Set clock divider
	OLED_CMD(0xCA, 0x3F);		// Set multiplexing ratio (1/64)
	OLED_CMD(0xA2, 0x00);		// Zero out display offset
	OLED_CMD(0xA1, 0x00);		// Zero out starting line
	OLED_CMD(0xA0, 0x14, 0x11);	// Horizontal address increment, Nibble remap, Reverse COM scan direction, Dual COM mode
	OLED_CMD(0x15, 28, 91); 	// Not all common pins are used
	OLED_CMD(0xB5, 0x00);		// Disable GPIOs
	OLED_CMD(0xAB, 0x01);		// Enable internal VDD regulator
	OLED_CMD(0xB4, 0xA0, 0xFD);	// Use internal VSL, Enhanced display quality
	OLED_CMD(0xC1, 0xFF);		// Maximum contrast
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

const uint8_t image1[] =
#include "image1.inc"
;
const uint8_t image2[] =
#include "image2.inc"
;

void wifi_main(void);

static pixelflut_t pixelflut;
static uint8_t oled_fb[8192];

static inline unsigned int rgb_pixel_to_grayscale(union fb_pixel px) {
	unsigned int red = px.color.color_bgr.red;
	unsigned int green = px.color.color_bgr.green;
	unsigned int blue = px.color.color_bgr.blue;
	unsigned int avg = (red + green + blue) / 3;
	return avg;
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

	// Setup GPIOs
	gpio_set_direction(GPIO_OLED_DC, GPIO_MODE_OUTPUT);
	gpio_set_direction(GPIO_OLED_RST, GPIO_MODE_OUTPUT);

	// Initialize SPI bus
	ret = spi_bus_initialize(SPI_OLED_HOST, &buscfg, SPI_DMA_CH_AUTO);
	ESP_ERROR_CHECK(ret);
	// Attach OLED to SPI bus
	ret = spi_bus_add_device(SPI_OLED_HOST, &devcfg, &spidev);
	ESP_ERROR_CHECK(ret);
	// Initialize LCD
	oled_init(spidev);

	// Mount main fat storage
	ESP_ERROR_CHECK(flash_fatfs_mount("storage", "/storage"));

	// Setup WiFi
	wifi_main();

	// Setup webserver
	webserver_init();

	// Setup pixelflut
	ESP_ERROR_CHECK(pixelflut_init(&pixelflut, 256, 64, 8192));

	// Start listening
	ESP_ERROR_CHECK(pixelflut_listen(&pixelflut));

	int64_t last = esp_timer_get_time();
	uint32_t flips = 0;
	while (1) {
		for (int i = 0; i < 2; i++) {
			for (int y = 0; y < 64; y++) {
				for (int x = 0; x < 256 / 2; x++) {
					union fb_pixel px1 = pixelflut.fb->pixels[y * 256 + x * 2 + 1];
					unsigned int int1 = rgb_pixel_to_grayscale(px1);
					union fb_pixel px2 = pixelflut.fb->pixels[y * 256 + x * 2 + 0];
					unsigned int int2 = rgb_pixel_to_grayscale(px2);
					oled_fb[y * 128 + x] = (int1 >> 4) | (int2 & 0xf0);
				}
			}
			oled_write_image(spidev, oled_fb, i);
			oled_show_image(spidev, i);
			flips++;
			int64_t now = esp_timer_get_time();
			if (now - last >= 1000000) {
				ESP_LOGI("main", "%lu fps", flips);
				flips = 0;
				last = now;
			}
			vTaskDelay(2);
//	 		taskYIELD();
		}
	}
}
