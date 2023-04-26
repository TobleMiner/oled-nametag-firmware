#ifndef _MIME_H_
#define _MIME_H_

#define MIME_TYPE_APPLICATION_GZIP	"application/gzip"
#define MIME_TYPE_APPLICATION_JSON	"application/json"
#define MIME_TYPE_IMAGE_JPEG		"image/jpeg"
#define MIME_TYPE_IMAGE_PNG		"image/png"
#define MIME_TYPE_IMAGE_VND_ICON	"image/vnd.microsoft.icon"
#define MIME_TYPE_TEXT_CSS		"text/css"
#define MIME_TYPE_TEXT_HTML		"text/html"
#define MIME_TYPE_TEXT_JAVASCRIPT	"text/javascript"

struct mime_pair {
  const char* fext;
  const char* mime_type;
};

const char* mime_get_type_from_filename(char* path);

#endif
