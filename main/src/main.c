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
#include "buttons.h"
#include "embedded_files.h"
#include "event_bus.h"
#include "flash.h"
#include "gifplayer.h"
#include "gui.h"
#include "menutree.h"
#include "nvs.h"
#include "pixelflut/pixelflut.h"
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

const uint8_t image1[] =
#include "image1.inc"
;
const uint8_t image2[] =
#include "image2.inc"
;

void wifi_main(void);

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

static button_event_handler_t gifplayer_button_event_handler;
static bool handle_gifplayer_button_press(const button_event_t *event, void *priv) {
	ESP_LOGI(TAG, "Button %s pressed", button_to_name(event->button));
/*
	if (event->button == BUTTON_UP) {
		gifplayer_play_prev_animation();
	} else if (event->button == BUTTON_DOWN) {
		gifplayer_play_next_animation();
        }
*/
	return false;
}

static button_event_handler_t reset_button_event_handler;
static bool handle_reset_button_press(const button_event_t *event, void *priv) {
	ESP_LOGI(TAG, "Reset button pressed");
//	esp_restart();
	return false;
}

gui_list_t gui_list_settings;
gui_image_t gui_image_gif_player;
gui_image_t gui_image_wlan_settings;
gui_image_t gui_image_power_off;
gui_container_t gui_container_wlan_settings2;
gui_image_t gui_image_wlan_settings2;
gui_rectangle_t gui_rectangle;

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

	main_task = xTaskGetCurrentTaskHandle();

	// Setup GPIOs
	gpio_set_direction(GPIO_OLED_DC, GPIO_MODE_OUTPUT);
	gpio_set_direction(GPIO_OLED_RST, GPIO_MODE_OUTPUT);
	gpio_set_direction(GPIO_OLED_VCC, GPIO_MODE_OUTPUT);

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

/*
	gui_list_init(&gui_list_settings);
	gui_image_init(&gui_image_gif_player, 119, 21, EMBEDDED_FILE_PTR(gif_player_119x21_raw));
	gui_image_init(&gui_image_wlan_settings, 119, 20, EMBEDDED_FILE_PTR(wlan_settings_119x20_raw));
	gui_image_init(&gui_image_power_off, 119, 18, EMBEDDED_FILE_PTR(power_off_119x18_raw));
	gui_image_init(&gui_image_wlan_settings2, 119, 20, EMBEDDED_FILE_PTR(wlan_settings_119x20_raw));
	gui_container_init(&gui_container_wlan_settings2);
	gui_rectangle_init(&gui_rectangle);

	gui_element_add_child(&gui.container.element, &gui_list_settings.container.element);
	gui_element_set_position(&gui_list_settings.container.element, 14, 0);
	gui_element_set_size(&gui_list_settings.container.element, 144, 64);

	gui_element_set_selectable(&gui_image_gif_player.element, true);
	gui_element_add_child(&gui_list_settings.container.element, &gui_image_gif_player.element);
	gui_element_set_position(&gui_image_gif_player.element, 0, 2);
	gui_event_handler_cfg_t event_handler_cfg = {
		.event = GUI_EVENT_CLICK,
		.cb = on_image_gif_player_click,
	};
	gui_element_add_event_handler(&gui_image_gif_player.element, &gui_image_gif_player_on_click_handler, &event_handler_cfg);

	gui_element_set_selectable(&gui_image_wlan_settings.element, true);
	gui_element_add_child(&gui_list_settings.container.element, &gui_image_wlan_settings.element);
	gui_element_set_position(&gui_image_wlan_settings.element, 0, 23);
	event_handler_cfg.cb = on_image_wlan_settings_click;
	gui_element_add_event_handler(&gui_image_wlan_settings.element, &gui_image_wlan_settings_on_click_handler, &event_handler_cfg);

	gui_element_set_selectable(&gui_image_power_off.element, true);
	gui_element_add_child(&gui_list_settings.container.element, &gui_image_power_off.element);
	gui_element_set_position(&gui_image_power_off.element, 0, 43);
	event_handler_cfg.cb = on_image_power_off_click;
	gui_element_add_event_handler(&gui_image_power_off.element, &gui_image_power_off_on_click_handler, &event_handler_cfg);

	gui_element_set_selectable(&gui_container_wlan_settings2.element, true);
	gui_element_add_child(&gui_list_settings.container.element, &gui_container_wlan_settings2.element);
	gui_element_set_position(&gui_container_wlan_settings2.element, 0, 66);
	gui_element_set_size(&gui_container_wlan_settings2.element, 270, 70);

	gui_element_set_selectable(&gui_image_wlan_settings2.element, true);
	gui_element_add_child(&gui_container_wlan_settings2.element, &gui_image_wlan_settings2.element);
	gui_element_set_position(&gui_image_wlan_settings2.element, 0, 25);

	gui_element_add_child(&gui.container.element, &gui_rectangle.element);
	gui_element_set_size(&gui_rectangle.element, 256, 64);
	gui_rectangle_set_color(&gui_rectangle, 255);

	gui_element_show(&gui_list_settings.container.element);
	gui_element_show(&gui_rectangle.element);
*/
	// Setup buttons
	buttons_init();
	const button_event_handler_multi_user_cfg_t button_cfg = {
		.base = {
			.cb = handle_gifplayer_button_press,
		},
		.multi = {
			.button_filter = (1 << BUTTON_UP) | (1 << BUTTON_DOWN) | (1 << BUTTON_ENTER) | (1 << BUTTON_EXIT),
			.action_filter = (1 << BUTTON_ACTION_RELEASE)
		}
	};
	const button_event_handler_single_user_cfg_t reset_button_cfg = {
		.base = {
			.cb = handle_reset_button_press,
		},
		.single = {
			.button = BUTTON_EXIT,
			.action = BUTTON_ACTION_HOLD,
			.min_hold_duration_ms = 1000,
		}
	};
	buttons_register_multi_button_event_handler(&gifplayer_button_event_handler, &button_cfg);
	buttons_register_single_button_event_handler(&reset_button_event_handler, &reset_button_cfg);

	// Initialize GUI
	gui_init(&gui, NULL, &gui_ops);

	// Setup gifplayer
	gifplayer_init(&gui);

	// Setup wifi settings
	wlan_settings_init(&gui);

	// Setup menu
	menu = menutree_init(&gui.container, &gui);
	menu_show(menu);

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

	int64_t last = esp_timer_get_time();
	uint32_t flips = 0;
	bool slot = false;
	int last_frame_duration_ms = 0;
	uint32_t cnt = 0;
	bool hidden = false;
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
/*
		vTaskDelay(10);
		cnt++;

		if (cnt % 10 == 0) {
			hidden = !hidden;
			gui_lock(&gui);
			gui_element_set_hidden(&gui_list_settings.container.element, hidden);
			gui_unlock(&gui);
		}
*/
/*
		gifplayer_lock();
		if (gifplayer_is_animation_playing()) {
			int frame_duration_ms;

			int64_t time_start_us = esp_timer_get_time();
			oled_show_image(spidev, slot ? 1 : 0);
			slot = !slot;
			gifplayer_render_next_frame_(rgb888_fb, 256, 64, &frame_duration_ms);
			const char *current_animation = gifplayer_get_path_of_playing_animation_();
			if (!current_default_animation || strcmp(current_default_animation, current_animation)) {
				if (current_default_animation) {
					free(current_default_animation);
				}
				settings_set_default_animation(current_animation);
				current_default_animation = strdup(current_animation);
				ESP_LOGI(TAG, "Saved default animation: %s", STR_NULL(current_default_animation));
			}
			gifplayer_unlock();
			fb_convert(oled_fb, rgb888_fb);
			oled_write_image(spidev, oled_fb, slot ? 1 : 0);
			int64_t time_display_us = last_frame_duration_ms * 1000;
			int64_t time_finish_us = esp_timer_get_time();
			int64_t time_passed = time_finish_us - time_start_us;
			int64_t time_wait = time_display_us - time_passed;
			// TODO: use CONFIG_FREERTOS_HZ
			if (time_wait < 10000) {
				vTaskDelay(1);
			} else {
				vTaskDelay(time_wait / 10000);
				do {
					time_finish_us = esp_timer_get_time();
					time_passed = time_finish_us - time_start_us;
					time_wait = time_display_us - time_passed;
				} while (time_wait > 0);
			}
			last_frame_duration_ms = frame_duration_ms;
		} else {
			gifplayer_unlock();
			vTaskDelay(5);
		}
*/
/*
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
*/
	}
}
