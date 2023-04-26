#include <string.h>

#include "mime.h"
#include "futil.h"
#include "util.h"

const struct mime_pair mimedb[] = {
  {
    .fext = "html",
    .mime_type = MIME_TYPE_TEXT_HTML,
  },
  {
    .fext = "thtml",
    .mime_type = MIME_TYPE_TEXT_HTML,
  },
  {
    .fext = "js",
    .mime_type = MIME_TYPE_TEXT_JAVASCRIPT,
  },
  {
    .fext = "css",
    .mime_type = MIME_TYPE_TEXT_CSS,
  },
  {
    .fext = "jpg",
    .mime_type = MIME_TYPE_IMAGE_JPEG,
  },
  {
    .fext = "jpeg",
    .mime_type = MIME_TYPE_IMAGE_JPEG,
  },
  {
    .fext = "png",
    .mime_type = MIME_TYPE_IMAGE_PNG,
  },
  {
    .fext = "json",
    .mime_type = MIME_TYPE_APPLICATION_JSON,
  },
  {
    .fext = "gz",
    .mime_type = MIME_TYPE_APPLICATION_GZIP,
  },
  {
    .fext = "ico",
    .mime_type = MIME_TYPE_IMAGE_VND_ICON,
  },
};

const char* mime_get_type_from_filename(char* path) {
  size_t i;
  const char* fext = futil_get_fext(path);
  if(!fext) {
    return NULL;
  }

  for(i = 0; i < ARRAY_SIZE(mimedb); i++) {
    const struct mime_pair* mime = &mimedb[i];
    if(!strcmp(mime->fext, fext)) {
      return mime->mime_type;
    }
  }
  return NULL;
}
