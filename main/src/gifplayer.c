#include "gifplayer.h"

#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <gif.h>

#include "dirent_cache.h"
#include "futil.h"
#include "util.h"

static const char *TAG = "gifplayer";

static GIFIMAGE current_animation;
static char *current_animation_path = NULL;

static dirent_cache_t animation_dirent_cache;

void gifplayer_init() {
	if (futil_dir_exists(GIFPLAYER_BASE)) {
		ESP_LOGI(TAG, "Animation directory '"GIFPLAYER_BASE_DIR"' does not exists, creating it");
		remove(GIFPLAYER_BASE);
		if (mkdir(GIFPLAYER_BASE_DIR, 0)) {
			ESP_LOGE(TAG, "Failed to create animation directory: %d\n", errno);
		}
	}

	dirent_cache_init(&animation_dirent_cache);
	ESP_ERROR_CHECK(dirent_cache_update(&animation_dirent_cache, GIFPLAYER_BASE));
}

void gifplayer_lock() {
	dirent_cache_lock(&animation_dirent_cache);
}

void gifplayer_unlock() {
	dirent_cache_unlock(&animation_dirent_cache);
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

static int gifplayer_set_animation__(const char *path, const char *dir_prefix) {
	if (current_animation_path) {
		GIF_close(&current_animation);
		free(current_animation_path);
		current_animation_path = NULL;
	}

	if (path) {
		if (dir_prefix) {
			current_animation_path = futil_path_concat(path, dir_prefix);
		} else {
			current_animation_path = strdup(path);
		}
		if (!current_animation_path) {
			return -ENOMEM;
		}

		GIF_begin(&current_animation, GIF_PALETTE_RGB888);
		if (!GIF_openFile(&current_animation, current_animation_path, draw_gif_frame)) {
			ESP_LOGE(TAG, "Failed to open GIF file '%s': %d", current_animation_path, current_animation.iError);
			free(current_animation_path);
			current_animation_path = NULL;
			return current_animation.iError ? current_animation.iError : -1;
		}
	}

	return 0;
}

int gifplayer_set_animation_(const char *path) {
	return gifplayer_set_animation__(path, NULL);
}

int gifplayer_set_animation_relative_(const char *path) {
	return gifplayer_set_animation__(path, GIFPLAYER_BASE_DIR);
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

int gifplayer_update_available_animations_(void) {
	return dirent_cache_update_(&animation_dirent_cache, GIFPLAYER_BASE);
}

int gifplayer_update_available_animations(void) {
	int err;

	gifplayer_lock();
	err = gifplayer_update_available_animations_();
	gifplayer_unlock();
	return err;
}

static void switch_to_first_animation_(void) {
	const char *first_animation = gifplayer_get_first_animation_name_();

	gifplayer_set_animation_relative_(first_animation);
}

const char *get_next_animation_name_(const char *animation_name) {
	const char *dircache_entry = dirent_cache_find_entry_(&animation_dirent_cache, animation_name);
	return dirent_cache_iter_next_(&animation_dirent_cache, dircache_entry);
}

void gifplayer_play_next_animation(void) {
	const char *next_animation_name;

	ESP_LOGI(TAG, "Trying to switch to next animation");
	gifplayer_lock();
	next_animation_name = get_next_animation_name_(gifplayer_get_name_of_playing_animation_());
	ESP_LOGI(TAG, "Filename of next animation: '%s'", STR_NULL(next_animation_name));
	if (next_animation_name) {
		gifplayer_set_animation_relative_(next_animation_name);
	} else {
		ESP_LOGI(TAG, "Can't switch to next animation, using first animation");
		switch_to_first_animation_();
	}
	gifplayer_unlock();
}

static void switch_to_last_animation_(void) {
	const char *last_animation = gifplayer_get_last_animation_name_();

	gifplayer_set_animation_relative_(last_animation);
}

const char *get_prev_animation_name_(const char *animation_name) {
	const char *dircache_entry = dirent_cache_find_entry_(&animation_dirent_cache, animation_name);
	return dirent_cache_iter_prev_(&animation_dirent_cache, dircache_entry);
}

void gifplayer_play_prev_animation(void) {
	const char *prev_animation_name;

	ESP_LOGI(TAG, "Trying to switch to previous animation");
	gifplayer_lock();
	prev_animation_name = get_prev_animation_name_(gifplayer_get_name_of_playing_animation_());
	ESP_LOGI(TAG, "Filename of previous animation: '%s'", STR_NULL(prev_animation_name));
	if (prev_animation_name) {
		gifplayer_set_animation_relative_(prev_animation_name);
	} else {
		ESP_LOGI(TAG, "Can't switch to previous animation, using last animation");
		switch_to_last_animation_();
	}
	gifplayer_unlock();
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

const char *gifplayer_get_name_of_playing_animation_(void) {
	return futil_fname(current_animation_path);
}

const char *gifplayer_get_first_animation_name_(void) {
	return dirent_cache_iter_first_(&animation_dirent_cache);
}

const char *gifplayer_get_last_animation_name_(void) {
	return dirent_cache_iter_last_(&animation_dirent_cache);
}

const char *gifplayer_get_next_animation_name_(const char *cursor) {
	return dirent_cache_iter_next_(&animation_dirent_cache, cursor);
}
