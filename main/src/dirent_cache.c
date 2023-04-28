#include "dirent_cache.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "util.h"

void dirent_cache_init(dirent_cache_t *cache) {
	cache->lock = xSemaphoreCreateMutexStatic(&cache->lock_buffer);
	cache->cache = NULL;
	cache->cache_size = 0;
}

void dirent_cache_lock(dirent_cache_t *cache) {
	xSemaphoreTake(cache->lock, portMAX_DELAY);
}

void dirent_cache_unlock(dirent_cache_t *cache) {
	xSemaphoreGive(cache->lock);
}

static int iterate_dir(const char *path, char *cache, size_t *cache_size) {
	DIR* dir;
	struct dirent* cursor;
	size_t cache_len = 0;

	dir = opendir(path);
	if (!dir) {
		return -errno;
	}
	DIRENT_FOR_EACH(cursor, dir) {
		size_t entry_size;

		if (!cursor) {
			closedir(dir);
			return -errno;
		}
		entry_size = strlen(cursor->d_name) + 1;
		cache_len += entry_size;
		if (cache_size && *cache_size) {
			if (cache_len > *cache_size) {
				return -ENOBUFS;
			}
		}

		if (cache) {
			strcpy(cache, cursor->d_name);
			cache += entry_size;
		}
	}
	closedir(dir);
	if (cache_size) {
		*cache_size = cache_len;
	}

	return 0;
}

int dirent_cache_update_(dirent_cache_t *cache, const char *path) {
	size_t required_cache_size = 0;
	int err;
	char *cache_data;

	err = iterate_dir(path, NULL, &required_cache_size);
	if (err) {
		return err;
	}

	if (!required_cache_size) {
		if (cache->cache) {
			free(cache->cache);
		}
		cache->cache = NULL;
		cache->cache_size = 0;
		return 0;
	}

	cache_data = calloc(1, required_cache_size);
	if (!cache_data) {
		return -ENOMEM;
	}

	err = iterate_dir(path, cache_data, &required_cache_size);
	if (err) {
		return err;
	}

	if (cache->cache) {
		free(cache->cache);
	}
	cache->cache = cache_data;
	cache->cache_size = required_cache_size;
	return 0;
}

int dirent_cache_update(dirent_cache_t *cache, const char *path) {
	int err;

	dirent_cache_lock(cache);
	err = dirent_cache_update_(cache, path);
	dirent_cache_unlock(cache);
	return err;
}

bool dirent_cache_iter_valid_(dirent_cache_t *cache, const char *iter) {
	return iter >= cache->cache && iter < cache->cache + cache->cache_size;
}

const char *dirent_cache_iter_first_(dirent_cache_t *cache) {
	return cache->cache;
}

const char *dirent_cache_iter_last_(dirent_cache_t *cache) {
	const char *iter;

	if (cache->cache_size < 2) {
		return NULL;
	}
	iter = cache->cache + (cache->cache_size - 2);

	// Find terminating nul-byte of preceeding string
	while (*iter) {
		if (iter == cache->cache) {
			return cache->cache;
		}
		iter--;
	}

	// Finally start of last string is one byte post the terminating nul-byte
	// of second to last string
	return iter + 1;
}

const char *dirent_cache_iter_next_(dirent_cache_t *cache, const char *iter) {
	if (!dirent_cache_iter_valid_(cache, iter)) {
		return NULL;
	}

	iter += strlen(iter) + 1;
	if (!dirent_cache_iter_valid_(cache, iter)) {
		return NULL;
	}

	return iter;
}

const char *dirent_cache_iter_prev_(dirent_cache_t *cache, const char *iter) {
	if (!dirent_cache_iter_valid_(cache, iter)) {
		return NULL;
	}

	// Find terminating nul-byte of preceeding string
	while (*iter) {
		if (iter == cache->cache) {
			return NULL;
		}
		iter--;
	}

	// Step into preceeding string
	iter--;
	if (!dirent_cache_iter_valid_(cache, iter)) {
		return NULL;
	}

	// Find terminating nul-byte of predecessor of preceeding string
	while (*iter) {
		if (iter == cache->cache) {
			return cache->cache;
		}
		iter--;
	}

	// Finally start of preceeding string is one byte post the terminating nul-byte
	// of the preceeding strings predecessor
	return iter + 1;
}

const char *dirent_cache_find_next_entry_(dirent_cache_t *cache, const char *iter, const char *name) {
	if (!name) {
		return NULL;
	}

	for (;
	     dirent_cache_iter_valid_(cache, iter);
	     iter = dirent_cache_iter_next_(cache, iter)) {
		if (!strcmp(iter, name)) {
			return iter;
		}
	}

	return NULL;
}
