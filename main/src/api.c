#include "api.h"

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include <esp_log.h>

#include "futil.h"
#include "gifplayer.h"
#include "httpd_util.h"
#include "util.h"
#include "vendor.h"

static const char *TAG = "api";

#define HTTP_ANIMATION_OPEN_ERR "{ \"error\": \"Failed to open animation file for writing\" }"
#define HTTP_ANIMATION_SOCK_ERR "{ \"error\": \"Failed to read from socket\" }"
#define HTTP_ANIMATION_HEX_ERR "{ \"error\": \"Failed to decode animation hex data\" }"
#define HTTP_ANIMATION_WRITE_ERR "{ \"error\": \"Failed to write animation to file\" }"
#define HTTP_DIRCACHE_UPDATE_ERR "{ \"error\": \"Failed to update list of animations\" }"

static esp_err_t http_post_upload_animation(struct httpd_request_ctx* ctx, void* priv) {
	httpd_req_t *req = ctx->req;
	int ret;
	FILE *fhndl;
	ssize_t param_len;
	esp_err_t err;
	char* fname;
	char* abspath;
	bool animation_switch_required = false;
	const char *current_animation_path;

	if((param_len = httpd_query_string_get_param(ctx, "filename", &fname)) <= 0) {
		return httpd_send_error(ctx, HTTPD_400);
	}

	abspath = futil_path_concat(fname, GIFPLAYER_BASE_DIR);
	if (!abspath) {
		return httpd_send_error(ctx, HTTPD_500);
	}

	gifplayer_lock();
	fhndl = fopen(abspath, "w");
	if (!fhndl) {
		ESP_LOGE(TAG, "Failed to open animation file for writing: %d", errno);
		err = httpd_send_error_msg(ctx, HTTPD_500, HTTP_ANIMATION_OPEN_ERR);
		goto out_locked;
	}

	current_animation_path = gifplayer_get_path_of_playing_animation_();
	if (!current_animation_path || !strcmp(fname, current_animation_path)) {
		ESP_LOGI(TAG, "Modifying currently active animation!");
		animation_switch_required = true;
	}

	ESP_LOGI(TAG, "POST data size: %u", req->content_len);

	while (1) {
		ssize_t res;
		size_t len;
		uint8_t read_buff[128];

		ret = httpd_req_recv(req, (char *)read_buff, sizeof(read_buff));
		if (ret < 0) {
			free(abspath);
			fclose(fhndl);
			err = httpd_send_error_msg(ctx, HTTPD_500, HTTP_ANIMATION_SOCK_ERR);
			goto out_locked;
		}
		if (!ret) {
			break;
		}
		len = ret;

		res = hex_decode_inplace(read_buff, len);
		if (res < 0) {
			ESP_LOGE(TAG, "Invalid hex data in request");
			free(abspath);
			fclose(fhndl);
			err = httpd_send_error_msg(ctx, HTTPD_400, HTTP_ANIMATION_HEX_ERR);
			goto out_locked;
		}
		len = res;

		ESP_LOG_BUFFER_HEXDUMP(TAG, read_buff, ret, ESP_LOG_VERBOSE);
		if (fwrite(read_buff, 1, len, fhndl) != len) {
			ESP_LOGE(TAG, "Failed to write to animation file: %d", ferror(fhndl));
			free(abspath);
			fclose(fhndl);
			err = httpd_send_error_msg(ctx, HTTPD_500, HTTP_ANIMATION_WRITE_ERR);
			goto out_locked;
		}
	}

	fclose(fhndl);

	if (animation_switch_required) {
		gifplayer_set_animation_(abspath);
	}
	free(abspath);
	err = gifplayer_update_available_animations_();
	gifplayer_unlock();

	if (err) {
		ESP_LOGE(TAG, "Failed to update available animations after upload: %d", err);
		httpd_send_error_msg(ctx, HTTPD_500, HTTP_DIRCACHE_UPDATE_ERR);
	} else {
		httpd_resp_send_chunk(req, "{}", strlen("{}"));
		httpd_finalize_response(ctx);
	}
	return err;

out_locked:
	gifplayer_unlock();
	return err;
}

#define append_or_flush_dir(...)								\
	do {											\
		int err = append_or_flush_(ctx, strbuf, sizeof(strbuf), &offset, __VA_ARGS__);	\
		if (err < 0) {									\
			gifplayer_unlock();							\
			return httpd_send_error(ctx, HTTPD_500);				\
		}										\
	} while (0)

static esp_err_t http_get_animations(struct httpd_request_ctx* ctx, void* priv) {
	char strbuf[64] = { 0 };
	off_t offset = 0;
	const char *current_animation_name, *cursor;

	(void)priv;
	gifplayer_lock();
	current_animation_name = gifplayer_get_name_of_playing_animation_();
	append_or_flush_dir("{ \"animations\": [");
	GIFPLAYER_FOR_EACH_ANIMATION(cursor) {
		if (cursor != gifplayer_get_first_animation_name_()) {
			append_or_flush_dir(", ");
		}
		append_or_flush_dir("{ \"name\": \"%s\"", cursor);
		if (current_animation_name && !strcmp(cursor, current_animation_name)) {
			append_or_flush_dir(", \"active\": true");
		}
		append_or_flush_dir("}");
	}
	append_or_flush_dir("]}");
	if (offset) {
		httpd_resp_send_chunk(ctx->req, strbuf, offset);
	}
	gifplayer_unlock();
	httpd_finalize_response(ctx);
	return ESP_OK;
}

static esp_err_t http_get_set_animation(struct httpd_request_ctx* ctx, void* priv) {
	ssize_t param_len;
	char* fname;
	char* abspath;
	int err;

	if((param_len = httpd_query_string_get_param(ctx, "filename", &fname)) <= 0) {
		return httpd_send_error(ctx, HTTPD_400);
	}

	abspath = futil_path_concat(fname, GIFPLAYER_BASE_DIR);
	if (!abspath) {
		return httpd_send_error(ctx, HTTPD_500);
	}

	err = gifplayer_set_animation(abspath);
	if (err) {
		ESP_LOGW(TAG, "Failed to set animation to '%s': %d", abspath, err);
		free(abspath);
		return httpd_send_error(ctx, HTTPD_500);
	}
	free(abspath);

	httpd_finalize_response(ctx);
	return ESP_OK;
}

static esp_err_t http_get_delete_animation(struct httpd_request_ctx* ctx, void* priv) {
	ssize_t param_len;
	char* fname;
	char* abspath;
	const char *current_animation_path;
	esp_err_t err;

	if((param_len = httpd_query_string_get_param(ctx, "filename", &fname)) <= 0) {
		return httpd_send_error(ctx, HTTPD_400);
	}

	abspath = futil_path_concat(fname, GIFPLAYER_BASE_DIR);
	if (!abspath) {
		return httpd_send_error(ctx, HTTPD_500);
	}

	gifplayer_lock();
	current_animation_path = gifplayer_get_path_of_playing_animation_();
	if (current_animation_path && !strcmp(fname, current_animation_path)) {
		ESP_LOGI(TAG, "Deleting currently active animation!");
		gifplayer_stop_playback();
	}

	unlink(abspath);
	free(abspath);
	err = gifplayer_update_available_animations_();
	gifplayer_unlock();
	if (err) {
		ESP_LOGE(TAG, "Failed to update available animations after deleting animation: %d", err);
	}

	httpd_finalize_response(ctx);
	return ESP_OK;
}

static esp_err_t http_get_set_serial(struct httpd_request_ctx* ctx, void *priv) {
	ssize_t param_len;
	char *serial;
	const char *current_animation_path;
	esp_err_t err;

	if((param_len = httpd_query_string_get_param(ctx, "serial", &serial)) <= 0) {
		return httpd_send_error(ctx, HTTPD_400);
	}

	vendor_set_serial_number(serial);

	httpd_finalize_response(ctx);
	return ESP_OK;
}

void api_init(httpd_t *httpd) {
	ESP_ERROR_CHECK(httpd_add_post_handler(httpd, "/api/v1/upload_animation", http_post_upload_animation, NULL, 1, "filename"));
	ESP_ERROR_CHECK(httpd_add_get_handler(httpd, "/api/v1/animations", http_get_animations, NULL, 0));
	ESP_ERROR_CHECK(httpd_add_get_handler(httpd, "/api/v1/set_animation", http_get_set_animation, NULL, 1, "filename"));
	ESP_ERROR_CHECK(httpd_add_get_handler(httpd, "/api/v1/delete_animation", http_get_delete_animation, NULL, 1, "filename"));

	ESP_ERROR_CHECK(httpd_add_get_handler(httpd, "/api/v1/vendor/set_serial", http_get_set_serial, NULL, 1, "serial"));
}
