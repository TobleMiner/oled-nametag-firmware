#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "futil.h"

void futil_normalize_path(char* path) {
  size_t len = strlen(path);
  char* slash_ptr = path, *end_ptr = path + len;
  while(slash_ptr < end_ptr && (slash_ptr = strchr(slash_ptr, '/'))) {
    while(++slash_ptr < end_ptr && *slash_ptr == '/') {
      memmove(slash_ptr, slash_ptr + 1, end_ptr - slash_ptr);
      slash_ptr--;
    }
  }
}

const char* futil_relpath(const char* path, const char* basepath) {
  size_t base_len;
  size_t path_len;

  if (!path || !basepath) {
    return NULL;
  }

  path_len = strlen(path);
  base_len = strlen(basepath);

  if(base_len > path_len) {
    return NULL;
  }

  if(strncmp(basepath, path, base_len)) {
    return NULL;
  }

  return path + base_len;
}

esp_err_t futil_relpath_inplace(char* path, const char* basepath) {
  size_t base_len = strlen(basepath);
  size_t path_len = strlen(path);

  if(base_len > path_len) {
    return ESP_ERR_INVALID_ARG;
  }

  if(strncmp(basepath, path, base_len)) {
    return ESP_ERR_INVALID_ARG;
  }

  memmove(path, path + base_len, path_len - base_len + 1);

  return ESP_OK;
}

bool futil_is_path_relative(const char* path) {
  return path[0] != '/';
}

int futil_dir_exists(const char *path) {
    DIR *dir = opendir(path);
    if (dir) {
        closedir(dir);
        return 0;
    }
    return errno ? -errno : -1;
}

char* futil_path_concat(const char* path, const char* basepath) {
  size_t abspath_len = strlen(basepath) + 1 + strlen(path) + 1;
  char* abspath = calloc(1, abspath_len);
  if(!abspath) {
    return NULL;
  }

  strcat(abspath, basepath);
  strncat(abspath, "/", abspath_len);
  strncat(abspath, path, abspath_len);

  futil_normalize_path(abspath);

  return abspath;
}

char *futil_abspath(char *path, const char *basepath) {
  if (!path) {
    return NULL;
  }

  if (!futil_is_path_relative(path) || !basepath) {
    return path;
  }
  return futil_path_concat(path, basepath);
}

static char* futil_get_fext_limit(char* path, char* limit) {
  char* fext_ptr = limit;
  if(limit < path) {
    return NULL;
  }

  while(fext_ptr-- > path) {
    if(*fext_ptr == '.') {
      return fext_ptr + 1;
    }
  }
  return NULL;
}

char* futil_get_fext(char* path) {
  if (!path) {
    return NULL;
  }

  return futil_get_fext_limit(path, path + strlen(path));
}

esp_err_t futil_get_bytes(void* dst, size_t len, const char* path) {
  esp_err_t err = ESP_OK;
  int fd = open(path, O_RDONLY);
  if(fd < 0) {
    err = errno;
    goto fail;
  }

  while(len > 0) {
    ssize_t read_len = read(fd, dst, len);
    if(read_len < 0) {
      err = errno;
      goto fail_fd;
    }

    if(read_len == 0) {
      err = ESP_ERR_INVALID_ARG;
      goto fail_fd;
    }

    dst += read_len;
    len -= read_len;
  }

fail_fd:
  close(fd);
fail:
  return err;
}

esp_err_t futil_read_file(void* ctx, const char* path, futil_write_cb cb) {
  esp_err_t err = ESP_OK;
  int fd;
  ssize_t read_len;
  char buff[FUTIL_CHUNK_SIZE];

  if((fd = open(path, O_RDONLY)) < 0) {
    err = errno;
    goto fail;
  }

  while((read_len = read(fd, buff, sizeof(buff))) > 0) {
    if((err = cb(ctx, buff, read_len))) {
      printf("Failed to handle chunk: %d size: %zd\n", err, read_len);
      goto fail_open;
    }
  }

  if(read_len < 0) {
    printf("Read failed: %s(%d)\n", strerror(errno), errno);
    err = errno;
    goto fail_open;
  }

fail_open:
  close(fd);
fail:
  return err;
}

const char* futil_fname(const char* path) {
  size_t len;
  char* slash_ptr;

  if (!path) {
    return NULL;
  }

  len = strlen(path);
  slash_ptr = strrchr(path, '/');

  if (!slash_ptr) {
	return path + len;
  }

  return slash_ptr + 1;
}
