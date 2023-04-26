#ifndef _MAGIC_H_
#define _MAGIC_H_

#include <stdint.h>

#include "esp_err.h"

#define MAGIC_DECLARE(name_, ...) \
  const uint8_t name_[] = { __VA_ARGS__ }

esp_err_t magic_buffer_is_gzip(const void *ptr);
esp_err_t magic_file_is_gzip(const char* path);

#endif
