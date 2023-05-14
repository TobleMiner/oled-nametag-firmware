#include "oled.h"

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_log.h>

#define GPIO_SPI_MOSI	36
#define GPIO_SPI_CLK	33
#define GPIO_OLED_DC	34
#define GPIO_OLED_RST	21
#define GPIO_OLED_CS	18
#define GPIO_OLED_VCC	14

#define SPI_OLED_HOST	SPI2_HOST

static spi_device_handle_t oled_spidev;

static const uint8_t oled_blank[8192] = { 0 };

static void oled_spi_pre_transfer_cb(spi_transaction_t *t)
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

static void oled_configure(spi_device_handle_t spidev)
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

static void oled_write_image_(spi_device_handle_t spidev, const uint8_t *image, unsigned int slot) {
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

void oled_write_image(const uint8_t *image, unsigned int slot)
{
	oled_write_image_(oled_spidev, image, slot);
}

void oled_show_image(unsigned int slot) {
	spi_device_handle_t spidev = oled_spidev;

	if (slot) {
		OLED_CMD(0xA1, 64); // Select slot 1
	} else {
		OLED_CMD(0xA1, 0); // Select slot 0
	}
}

void oled_init() {
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
	gpio_set_direction(GPIO_OLED_VCC, GPIO_MODE_OUTPUT);

	// Initialize SPI bus
	ESP_ERROR_CHECK(spi_bus_initialize(SPI_OLED_HOST, &buscfg, SPI_DMA_CH_AUTO));
	// Attach OLED to SPI bus
	ESP_ERROR_CHECK(spi_bus_add_device(SPI_OLED_HOST, &devcfg, &spidev));

	// Configure OLED
	oled_configure(spidev);

	// Clear screen buffer
	oled_write_image_(spidev, oled_blank, 0);
	oled_write_image_(spidev, oled_blank, 1);

	// Power up display
	gpio_set_level(GPIO_OLED_VCC, 1);

	oled_spidev = spidev;
}
