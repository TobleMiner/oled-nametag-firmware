#pragma once

#include <stdbool.h>

#define GIFPLAYER_BASE		"/flash/animations"
#define GIFPLAYER_BASE_DIR	GIFPLAYER_BASE"/"

/* Init only */
void gifplayer_init(void);

/* Threadsafe */
void gifplayer_lock(void);
void gifplayer_unlock(void);
bool gifplayer_is_animation_playing(void);
int gifplayer_set_animation(const char *path);
void gifplayer_stop_playback(void);

/* Use only with player lock acquired */
int gifplayer_set_animation_(const char *path);
int gifplayer_render_next_frame_(void *dst_rgb888, unsigned int width, unsigned int height, int *duration_ms);

/* Use method and result only with player lock acquired */
const char *gifplayer_get_path_of_playing_animation_(void);
