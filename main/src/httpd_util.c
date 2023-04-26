#include "httpd_util.h"

#include <errno.h>
#include <string.h>

static esp_err_t generic_bool_getter_(void* ctx, void* priv, struct templ_slice* slice, const char *str, bool inverted) {
	struct httpd_slice_ctx *slice_ctx = ctx;
	bool *val = priv;

	(void)slice;

	if (*val != inverted) {
		return httpd_response_write(slice_ctx->req_ctx, str, strlen(str));
	}

	return ESP_OK;
}

esp_err_t generic_bool_getter(void* ctx, void* priv, struct templ_slice* slice) {
	return generic_bool_getter_(ctx, priv, slice, "checked", false);
}

esp_err_t generic_bool_getter_inverted(void* ctx, void* priv, struct templ_slice* slice) {
	return generic_bool_getter_(ctx, priv, slice, "checked", true);
}

int append_or_flush_(struct httpd_request_ctx* ctx, char *strbuf, size_t strbuf_len, off_t *offset, const char *fmt, ...) {
	va_list valist;
	int res;

	va_start(valist, fmt);
	res = vsnprintf(strbuf + *offset, strbuf_len - *offset, fmt, valist);
	if (res < 0) {
		return res;
	}
	if (res >= strbuf_len - *offset) {
		httpd_resp_send_chunk(ctx->req, strbuf, *offset);
		*offset = 0;
		res = vsnprintf(strbuf + *offset, strbuf_len - *offset, fmt, valist);
		if (res < 0) {
			return res;
		}
		if (res >= strbuf_len) {
			return -ENOBUFS;
		}
	}
	*offset += res;
	va_end(valist);

	return 0;
}
