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

#include "dirent_cache.h"
#include "futil.h"
#include "gui_priv.h"
#include "util.h"

static const char *TAG = "gifplayer";

static gui_gifplayer_t gifplayer;
static gui_pixel_t render_fb[256 * 64];
static char *current_animation_path = NULL;
static gui_t *gui_root;

static dirent_cache_t animation_dirent_cache;

static menu_cb_f menu_cb;
static void *menu_cb_ctx;
static button_event_handler_t button_event_handler;

static bool on_button_event(const button_event_t *event, void *priv) {
	ESP_LOGI(TAG, "Button event");

	if (event->button == BUTTON_EXIT) {
		ESP_LOGI(TAG, "Quitting GIF player");
		buttons_disable_event_handler(&button_event_handler);
		gui_element_set_hidden(&gifplayer.element, true);
		ESP_LOGI(TAG, "Returning to menu");
		menu_cb(menu_cb_ctx);
		ESP_LOGI(TAG, "Done");
		return true;
	}

	if (event->button == BUTTON_UP) {
		gifplayer_play_prev_animation();
	}

	if (event->button == BUTTON_DOWN) {
		gifplayer_play_next_animation();
	}

	return false;
}

void gifplayer_init(gui_t *gui) {
	button_event_handler_multi_user_cfg_t button_event_cfg = {
		.base = {
			.cb = on_button_event
		},
		.multi = {
			.button_filter = (1 << BUTTON_UP) | (1 << BUTTON_DOWN) | (1 << BUTTON_EXIT),
			.action_filter = (1 << BUTTON_ACTION_RELEASE)
		}
	};

	if (futil_dir_exists(GIFPLAYER_BASE)) {
		ESP_LOGI(TAG, "Animation directory '"GIFPLAYER_BASE_DIR"' does not exists, creating it");
		remove(GIFPLAYER_BASE);
		if (mkdir(GIFPLAYER_BASE_DIR, 0)) {
			ESP_LOGE(TAG, "Failed to create animation directory: %d\n", errno);
		}
	}

	dirent_cache_init(&animation_dirent_cache);
	ESP_ERROR_CHECK(dirent_cache_update(&animation_dirent_cache, GIFPLAYER_BASE));

	gui_root = gui;
	gui_gifplayer_init(&gifplayer, render_fb);
	gui_element_set_size(&gifplayer.element, 256, 64);
	gui_element_add_child(&gui->container.element, &gifplayer.element);

	buttons_register_multi_button_event_handler(&button_event_handler, &button_event_cfg);
}

int gifplayer_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	menu_cb = exit_cb;
	menu_cb_ctx = cb_ctx;
	gui_element_set_hidden(&gifplayer.element, false);
	gui_element_show(&gifplayer.element);
	buttons_enable_event_handler(&button_event_handler);
	return 0;
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

static int gifplayer_set_animation__(const char *path, const char *dir_prefix) {
	gui_lock(gui_root);
	if (current_animation_path) {
		gui_gifplayer_load_animation_from_file(&gifplayer, NULL);
		free(current_animation_path);
		current_animation_path = NULL;
	}

	if (path) {
		int err;

		if (dir_prefix) {
			current_animation_path = futil_path_concat(path, dir_prefix);
		} else {
			current_animation_path = strdup(path);
		}
		if (!current_animation_path) {
			gui_unlock(gui_root);
			return -ENOMEM;
		}

		err = gui_gifplayer_load_animation_from_file(&gifplayer, current_animation_path);
		if (err) {
			ESP_LOGE(TAG, "Failed to open GIF file '%s': %d", current_animation_path, err);
			free(current_animation_path);
			current_animation_path = NULL;
			gui_unlock(gui_root);
			return err;
		}
	}

	gui_unlock(gui_root);
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

static int gui_gifplayer_render(gui_element_t *element, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size) {
	gui_gifplayer_t *player = container_of(element, gui_gifplayer_t, element);
	int64_t now;
	int copy_width = MIN(element->area.size.x - source_offset->x, destination_size->x);
	int copy_height = MIN(element->area.size.y - source_offset->y, destination_size->y);
	int y;

	if (!player->animation_loaded) {
		ESP_LOGI(TAG, "No animation loaded, nothing to render");
		return -1;
	}

	now = esp_timer_get_time();
	if (now >= player->next_frame_deadline_us && player->next_frame_deadline_us >= 0) {
		int duration_ms = -1;
		int ret;

		ret = GIF_playFrame(&player->animation, &duration_ms, player);
		if (duration_ms > 0) {
			player->next_frame_deadline_us = now + duration_ms * 1000;
		} else {
			player->next_frame_deadline_us = -1;
		}
	}

	for (y = 0; y < copy_height; y++) {
		gui_pixel_t *dst = &fb->pixels[y * fb->stride];
		const uint8_t *src = &player->render_fb[player->element.area.size.x * (y + source_offset->y) + source_offset->x];

		memcpy(dst, src, copy_width * sizeof(gui_pixel_t));
	}

	now = esp_timer_get_time();
	if (now >= player->next_frame_deadline_us) {
		return 0;
	}

	return DIV_ROUND(player->next_frame_deadline_us - now, 1000);
}

static const gui_element_ops_t gui_gifplayer_ops = {
	.render = gui_gifplayer_render,
};

gui_element_t *gui_gifplayer_init(gui_gifplayer_t *player, gui_pixel_t *render_fb) {
	player->render_fb = render_fb;
	player->animation_loaded = false;
	player->next_frame_deadline_us = 0;
	return gui_element_init(&player->element, &gui_gifplayer_ops);
}

static inline void gui_gifplayer_draw_pixel(GIFDRAW *draw, unsigned char palette_idx, int x, int y) {
	gui_gifplayer_t *player = draw->pUser;
	gui_element_t *element = &player->element;
	uint8_t *palette = draw->pPalette24 + 3 * palette_idx;
	unsigned int r, g, b;

	r = palette[0];
	g = palette[1];
	b = palette[2];
	player->render_fb[y * element->area.size.x + x] = (r + g + b) / 3;
}

static void gui_gifplayer_draw_frame(GIFDRAW *draw) {
	gui_gifplayer_t *player = draw->pUser;
	gui_element_t *element = &player->element;
	int y = draw->iY + draw->y;

	if (y < element->area.size.y) {
		int x;
		int draw_width = MIN(draw->iWidth, element->area.size.x);

		for (x = draw->iX; x < draw_width; x++) {
			if (draw->pPixels[x - draw->iX] == draw->ucTransparent && draw->ucDisposalMethod == 2) {
				gui_gifplayer_draw_pixel(draw, draw->ucBackground, x, y);
			} else if (!draw->ucHasTransparency || draw->pPixels[x - draw->iX] != draw->ucTransparent) {
				gui_gifplayer_draw_pixel(draw, draw->pPixels[x - draw->iX], x, y);
			}
		}
	}
}

int gui_gifplayer_load_animation_from_file(gui_gifplayer_t *player, const char *path) {
	if (player->animation_loaded) {
		GIF_close(&player->animation);
		player->animation_loaded = false;
	}

	if (path) {
		GIF_begin(&player->animation, GIF_PALETTE_RGB888);
		if (GIF_openFile(&player->animation, path, gui_gifplayer_draw_frame)) {
			player->animation_loaded = true;
		} else {
			return player->animation.iError ? player->animation.iError : -1;
		}
	}

	gui_element_invalidate(&player->element);
	gui_element_check_render(&player->element);

	return 0;
}

int gui_gifplayer_load_animation_from_memory(gui_gifplayer_t *player, const uint8_t *start, const uint8_t *end) {
	if (player->animation_loaded) {
		GIF_close(&player->animation);
		player->animation_loaded = false;
	}

	if (start) {
		GIF_begin(&player->animation, GIF_PALETTE_RGB888);
		if (GIF_openRAM(&player->animation, (void *)start /* const correctness is hard */, end - start, gui_gifplayer_draw_frame)) {
			player->animation_loaded = true;
		} else {
			return player->animation.iError ? player->animation.iError : -1;
		}
	}

	gui_element_invalidate(&player->element);
	gui_element_check_render(&player->element);

	return 0;
}
