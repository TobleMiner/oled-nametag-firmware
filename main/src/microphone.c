#include "microphone.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <driver/i2s_pdm.h>
#include <driver/gpio.h>
#include <esp_dsp.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <hal/i2s_ll.h>

#include "util.h"

#define GPIO_PDM_CLK	38
#define GPIO_PDM_DATA	37

#define SAMPLE_BUFFER_SIZE	1024
//#define SAMPLE_BUFFER_OVERLAP	 768
#define SAMPLE_BUFFER_OVERLAP	 960
#define FFT_SIZE		(SAMPLE_BUFFER_SIZE / 2)

static const char *TAG = "microphone";

static i2s_chan_handle_t rx_chan;
static StaticSemaphore_t lock_buffer;
static SemaphoreHandle_t lock;

static int16_t __attribute__((aligned(16))) sample_buffer[SAMPLE_BUFFER_SIZE - SAMPLE_BUFFER_OVERLAP];
static float __attribute__((aligned(16))) sample_buffer_float[SAMPLE_BUFFER_SIZE];
static float __attribute__((aligned(16))) fft_window[SAMPLE_BUFFER_SIZE];
static float __attribute__((aligned(16))) fft_buffer[SAMPLE_BUFFER_SIZE];
static float __attribute__((aligned(16))) fft_squared[SAMPLE_BUFFER_SIZE];

unsigned int fft_count = 0;
static float fft_result[FFT_SIZE];

static int64_t total_us = 0;
static int64_t conversion_cnt = 0;

static portMUX_TYPE fft_lock = portMUX_INITIALIZER_UNLOCKED;

static void pdm_data_task(void *arg) {
	// Setup FFT
	dsps_fft2r_init_fc32(NULL, FFT_SIZE);
	dsps_fft4r_init_fc32(NULL, FFT_SIZE);
	dsps_wind_hann_f32(fft_window, SAMPLE_BUFFER_SIZE);
	dsps_tone_gen_f32(sample_buffer_float, SAMPLE_BUFFER_SIZE, 1.0, 0.16, 0);

	while (1) {
		size_t bytes_read;
		esp_err_t err;

		err = i2s_channel_read(rx_chan, sample_buffer, sizeof(sample_buffer), &bytes_read, portMAX_DELAY);
		if (!err) {
			int64_t before_us = esp_timer_get_time();

			// Make space for new data (dsp lib is not setup for ring buffers, loop once through MMU maybe?)
			memmove(sample_buffer_float,
				&sample_buffer_float[SAMPLE_BUFFER_SIZE - SAMPLE_BUFFER_OVERLAP],
				SAMPLE_BUFFER_OVERLAP * sizeof(float));

			// Copy over samples, converting to float
			for (unsigned int i = 0; i < ARRAY_SIZE(sample_buffer); i++) {
				unsigned int dst = SAMPLE_BUFFER_OVERLAP + i;
				// int16_t -> float
				float val = sample_buffer[i];
				// normalize
				val /= 32768.0f;
				// store in free space at end of buffer
				sample_buffer_float[dst] = val;
			}

			// Apply window
			dsps_mul_f32(sample_buffer_float, fft_window, fft_buffer, SAMPLE_BUFFER_SIZE, 1, 1, 1);

			// FFT
			dsps_fft2r_fc32(fft_buffer, FFT_SIZE);
			dsps_bit_rev2r_fc32(fft_buffer, FFT_SIZE);
			dsps_cplx2real_fc32(fft_buffer, FFT_SIZE);

			// Amplitude
			dsps_mul_f32(fft_buffer, fft_buffer, fft_squared, SAMPLE_BUFFER_SIZE, 1, 1, 1);
			dsps_add_f32_ansi(fft_squared, fft_squared + 1, fft_buffer, FFT_SIZE, 2, 2, 1);

			taskENTER_CRITICAL(&fft_lock);
			for (int i = 0; i < FFT_SIZE; i++) {
				fft_result[i] += fft_buffer[i];
			}
			fft_count++;
			taskEXIT_CRITICAL(&fft_lock);
/*
			for (int i = 0; i < FFT_SIZE; i++) {
				fft_buffer[i] = log10f(fft_buffer[i] + 0.000001);
			}

			taskENTER_CRITICAL(&fft_lock);
			memcpy(fft_result, fft_buffer, sizeof(fft_result));
			taskEXIT_CRITICAL(&fft_lock);
*/
			int64_t after_us = esp_timer_get_time();
			total_us += after_us - before_us;
			conversion_cnt++;

			if (conversion_cnt % 200 == 0) {
				ESP_LOGI(TAG, "Average FFT time: %ld us", (long)(total_us / conversion_cnt));
			}
		}
	}
}

void microphone_get_last_fft(float *bins, unsigned int num_bins) {
	num_bins = MIN(num_bins, FFT_SIZE / 2);
	taskENTER_CRITICAL(&fft_lock);
	unsigned int fft_cnt = fft_count;
	if (fft_cnt) {
		memcpy(bins, fft_result, num_bins * sizeof(*bins));
	}
	memset(fft_result, 0, sizeof(fft_result));
	fft_count = 0;
	taskEXIT_CRITICAL(&fft_lock);
	if (fft_cnt) {
		for (int i = 0; i < num_bins; i++) {
			bins[i] = 10 * log10f(bins[i] / (float)fft_cnt + 0.000001);
		}
	}
}

void microphone_init() {
	i2s_chan_config_t rx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
	i2s_pdm_rx_config_t pdm_rx_cfg = {
//		.clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(4000),
		.clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(12000),
//		.clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(48000),
		.slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
		.gpio_cfg = {
			.clk = GPIO_PDM_CLK,
			.din = GPIO_PDM_DATA,
			.invert_flags = {
				.clk_inv = false,
			},
		},
	};
	rx_chan_cfg.dma_desc_num = 16;
	rx_chan_cfg.dma_frame_num = 512;
//	pdm_rx_cfg.slot_cfg.slot_mask = I2S_PDM_SLOT_RIGHT;

	lock = xSemaphoreCreateMutexStatic(&lock_buffer);

	ESP_ERROR_CHECK(i2s_new_channel(&rx_chan_cfg, NULL, &rx_chan));

	ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_chan, &pdm_rx_cfg));

//	i2s_ll_rx_set_pcm_type(&I2S0, I2S_PCM_U_COMPRESS);

	ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
	ESP_ERROR_CHECK(!xTaskCreate(pdm_data_task, "pdm_data", 8192, NULL, 20, NULL));
}
