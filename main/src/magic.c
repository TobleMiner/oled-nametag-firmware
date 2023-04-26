#include <string.h>

#include "magic.h"
#include "futil.h"
#include "util.h"

MAGIC_DECLARE(MAGIC_GZIP, 0x1f, 0x8b);

static esp_err_t magic_cmp_buffer(const void* magic, const void *ptr) {
  return !!memcmp(ptr, magic, 2);
}

static esp_err_t magic_cmp_file(const void* magic, const char* path) {
  esp_err_t err;
  char file_magic[2];

  if((err = -futil_get_bytes(file_magic, ARRAY_SIZE(file_magic), path))) {
    return err;
  }

  return magic_cmp_buffer(file_magic, magic);
}

esp_err_t magic_buffer_is_gzip(const void *ptr) {
  return magic_cmp_buffer(MAGIC_GZIP, ptr);
}

esp_err_t magic_file_is_gzip(const char* path) {
  return magic_cmp_file(MAGIC_GZIP, path);
}
