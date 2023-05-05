#pragma once

#include <stdbool.h>

#include <esp_timer.h>

#include <gif.h>

#include "gui.h"
#include "menu.h"

#define GIFPLAYER_BASE		"/flash/animations"
#define GIFPLAYER_BASE_DIR	GIFPLAYER_BASE"/"

#define GIFPLAYER_FOR_EACH_ANIMATION(cursor_) \
	for (cursor_ = gifplayer_get_first_animation_name_(); \
	     cursor_; \
	     cursor_ = gifplayer_get_next_animation_name_(cursor_))

/* Init only */
void gifplayer_init(gui_t *gui);

/* Threadsafe */
void gifplayer_lock(void);
void gifplayer_unlock(void);
bool gifplayer_is_animation_playing(void);
int gifplayer_set_animation(const char *path);
void gifplayer_stop_playback(void);
int gifplayer_update_available_animations(void);
void gifplayer_play_next_animation(void);
void gifplayer_play_prev_animation(void);
int gifplayer_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);

/* Use only with player lock acquired */
int gifplayer_set_animation_(const char *path);
int gifplayer_update_available_animations_(void);

/* Use method and result only with player lock acquired */
const char *gifplayer_get_path_of_playing_animation_(void);
const char *gifplayer_get_name_of_playing_animation_(void);
const char *gifplayer_get_first_animation_name_(void);
const char *gifplayer_get_last_animation_name_(void);
const char *gifplayer_get_next_animation_name_(const char *cursor);

typedef struct gui_gifplayer {
	gui_element_t element;

	int64_t next_frame_deadline_us;
	bool animation_loaded;
	GIFIMAGE animation;
	gui_pixel_t *render_fb;
} gui_gifplayer_t;

gui_element_t *gui_gifplayer_init(gui_gifplayer_t *player, gui_pixel_t *render_fb);
int gui_gifplayer_load_animation_from_file(gui_gifplayer_t *player, const char *path);
int gui_gifplayer_load_animation_from_memory(gui_gifplayer_t *player, const uint8_t *start, const uint8_t *end);
