#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define DIRENT_CACHE_FOR_EACH_ENTRY(cursor_, cache_) \
	for (cursor_ = dirent_cache_iter_first_(cache_); \
	     dirent_cache_iter_valid_(cache_, cursor_); \
	     cursor_ = dirent_cache_iter_next_(cache_, cursor_))

typedef struct dirent_cache {
	char *cache;
	size_t cache_size;
	SemaphoreHandle_t lock;
	StaticSemaphore_t lock_buffer;
} dirent_cache_t;

// Init
void dirent_cache_init(dirent_cache_t *cache);

// Threadsafe methods
int dirent_cache_update(dirent_cache_t *cache, const char *path);
void dirent_cache_lock(dirent_cache_t *cache);
void dirent_cache_unlock(dirent_cache_t *cache);

// Non-threadsafe methods, call only with cache lock held
int dirent_cache_update_(dirent_cache_t *cache, const char *path);

// Execution and result not threadsafe, obtain and use result only with cache lock held
bool dirent_cache_iter_valid_(dirent_cache_t *cache, const char *iter);
const char *dirent_cache_iter_first_(dirent_cache_t *cache);
const char *dirent_cache_iter_last_(dirent_cache_t *cache);
const char *dirent_cache_iter_next_(dirent_cache_t *cache, const char *iter);
const char *dirent_cache_iter_prev_(dirent_cache_t *cache, const char *iter);
const char *dirent_cache_find_next_entry_(dirent_cache_t *cache, const char *iter, const char *name);

static inline const char *dirent_cache_find_entry_(dirent_cache_t *cache, const char *name) {
	return dirent_cache_find_next_entry_(cache, dirent_cache_iter_first_(cache), name);
}
