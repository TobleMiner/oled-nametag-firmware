#pragma once

#include "httpd.h"

#define append_or_flush(...)									\
	do {											\
		int err = append_or_flush_(ctx, strbuf, sizeof(strbuf), &offset, __VA_ARGS__);	\
		if (err < 0) {									\
			return httpd_send_error(ctx, HTTPD_500);				\
		}										\
	} while (0)

esp_err_t generic_bool_getter(void* ctx, void* priv, struct templ_slice* slice);
esp_err_t generic_bool_getter_inverted(void* ctx, void* priv, struct templ_slice* slice);
int append_or_flush_(struct httpd_request_ctx* ctx, char *strbuf, size_t strbuf_len, off_t *offset, const char *fmt, ...);
