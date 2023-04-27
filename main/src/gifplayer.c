#include "gifplayer.h"

#include <errno.h>
#include <stddef.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <gif.h>

static StaticSemaphore_t gifplayer_mutex_buffer;
static SemaphoreHandle_t gifplayer_mutex;

static GIFIMAGE current_animation;
static char *current_animation_path = NULL;

void gifplayer_init() {
	gifplayer_mutex = xSemaphoreCreateMutexStatic(&gifplayer_mutex_buffer);
}

void gifplayer_lock() {
	xSemaphoreTake(gifplayer_mutex, portMAX_DELAY);
}

void gifplayer_unlock() {
	xSemaphoreGive(gifplayer_mutex);
}

bool gifplayer_is_animation_playing(void) {
	return current_animation_path != NULL;
}

typedef struct frame_draw_ctx {
	unsigned int width;
	unsigned int height;
	void *dst_rgb888;
} frame_draw_ctx_t;

static inline void draw_pixel(GIFDRAW *draw, unsigned char palette_idx, int x, int y) {
	frame_draw_ctx_t *ctx = draw->pUser;
	uint8_t *rgb888 = ctx->dst_rgb888;
	unsigned int base = (y * ctx->width + x) * 3;
	uint8_t *palette = draw->pPalette24 + 3 * palette_idx;

	rgb888[base + 0] = palette[0];
	rgb888[base + 1] = palette[1];
	rgb888[base + 2] = palette[2];
}

static void draw_gif_frame(GIFDRAW *draw) {
	int x, y, x_base;

	y = draw->iY + draw->y;
	x_base = draw->iX;

	for (x = 0; x < draw->iWidth; x++) {
		if (draw->pPixels[x] == draw->ucTransparent && draw->ucDisposalMethod == 2) {
			draw_pixel(draw, draw->ucBackground, x_base + x, y);
		} else if (!draw->ucHasTransparency || draw->pPixels[x] != draw->ucTransparent) {
			draw_pixel(draw, draw->pPixels[x], x_base + x, y);
		}
	}
}

int gifplayer_set_animation_(const char *path) {
	if (current_animation_path) {
		GIF_close(&current_animation);
		free(current_animation_path);
		current_animation_path = NULL;
	}

	if (path) {
		current_animation_path = strdup(path);
		if (!current_animation_path) {
			return -ENOMEM;
		}

		GIF_begin(&current_animation, GIF_PALETTE_RGB888);
		if (!GIF_openFile(&current_animation, path, draw_gif_frame)) {
			free(current_animation_path);
			current_animation_path = NULL;
			return current_animation.iError ? current_animation.iError : -1;
		}
	}

	return 0;
}

int gifplayer_set_animation(const char *path) {
	int err;

	gifplayer_lock();
	err = gifplayer_set_animation_(path);
	gifplayer_unlock();

	return err;
}

void gifplayer_stop_playback(void) {
	gifplayer_set_animation(NULL);
}

int gifplayer_render_next_frame_(void *dst_rgb888, unsigned int width, unsigned int height, int *duration_ms) {
	frame_draw_ctx_t ctx = {
		.width = width,
		.height = height,
		.dst_rgb888 = dst_rgb888
	};
	return GIF_playFrame(&current_animation, duration_ms, &ctx);
}

const char *gifplayer_get_path_of_playing_animation_(void) {
	return current_animation_path;
}
