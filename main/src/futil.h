#ifndef _FUTIL_H_
#define _FUTIL_H_

#include <stdbool.h>

#include "esp_err.h"

#define FUTIL_CHUNK_SIZE 256

typedef esp_err_t (*futil_write_cb)(void* ctx, char* buff, size_t len);

void futil_normalize_path(char* path);
char* futil_relpath(char* path, const char* basepath);
esp_err_t futil_relpath_inplace(char* path, const char* basepath);
char* futil_get_fext(char* path);
esp_err_t futil_get_bytes(void* dst, size_t len, const char* path);
esp_err_t futil_read_file(void* ctx, const char* path, futil_write_cb cb);
int futil_dir_exists(const char *path);
char *futil_abspath(char *path, const char *basepath);
bool futil_is_path_relative(const char* path);
char* futil_path_concat(const char* path, const char* basepath);
const char* futil_fname(const char* path);

#endif
