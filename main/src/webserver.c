#include "webserver.h"

#include <stdio.h>
#include <stdlib.h>

#include <esp_err.h>
#include <esp_log.h>

#include "embedded_files.h"
#include "httpd.h"
#include "mime.h"

const char *TAG = "webserver";

#define ADD_EMBEDDED_STATIC_FILE(path_, mime_, name_) \
	ESP_ERROR_CHECK(httpd_add_embedded_static_file(&httpd, path_, mime_, EMBEDDED_FILE_PTRS(name_)))

#define ADD_EMBEDDED_STATIC_FILE_JS(path_, name_) \
	ADD_EMBEDDED_STATIC_FILE("/js/"path_, MIME_TYPE_TEXT_JAVASCRIPT, name_)

#define ADD_EMBEDDED_STATIC_FILE_CSS(path_, name_) \
	ADD_EMBEDDED_STATIC_FILE("/css/"path_, MIME_TYPE_TEXT_CSS, name_)

#define ADD_EMBEDDED_STATIC_FILE_HTML(path_, name_) \
	ADD_EMBEDDED_STATIC_FILE("/"path_, MIME_TYPE_TEXT_HTML, name_)

#define ADD_EMBEDDED_TEMPLATE_FILE(path_, name_) \
	ESP_ERROR_CHECK(httpd_add_embedded_template_file(&httpd, "/"path_, MIME_TYPE_TEXT_HTML, EMBEDDED_FILE_PTRS(name_)))

static httpd_t httpd;

void webserver_init() {
	ESP_ERROR_CHECK(httpd_init(&httpd, "/storage", 256));
	/* Templates */
	/* Embedded files */
	ADD_EMBEDDED_STATIC_FILE_JS("animation.js", animation_js);
	ADD_EMBEDDED_TEMPLATE_FILE("animation.thtml", animation_thtml);
	ADD_EMBEDDED_STATIC_FILE_JS("bootstrap.bundle.min.js", bootstrap_bundle_min_js);
	ADD_EMBEDDED_STATIC_FILE_CSS("bootstrap.min.css", bootstrap_min_css);
	ADD_EMBEDDED_STATIC_FILE("/favicon.ico", MIME_TYPE_IMAGE_VND_ICON, favicon_ico);
	ADD_EMBEDDED_STATIC_FILE_JS("jquery-3.3.1.min.js", jquery_3_3_1_min_js);
	ADD_EMBEDDED_TEMPLATE_FILE("include/navbar.thtml", navbar_thtml);
	ADD_EMBEDDED_STATIC_FILE_HTML("include/resources.html", resources_html);
	/* Index */
	ESP_ERROR_CHECK(httpd_add_redirect(&httpd, "/", "/animation.thtml"));
}
