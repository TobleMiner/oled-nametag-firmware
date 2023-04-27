#include "api.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <esp_log.h>

#include "futil.h"
#include "gifplayer.h"
#include "httpd_util.h"
#include "util.h"

static const char *TAG = "api";

#define HTTP_ANIMATION_OPEN_ERR "{ \"error\": \"Failed to open animation file for writing\" }"
#define HTTP_ANIMATION_SOCK_ERR "{ \"error\": \"Failed to read from socket\" }"
#define HTTP_ANIMATION_HEX_ERR "{ \"error\": \"Failed to decode animation hex data\" }"
#define HTTP_ANIMATION_WRITE_ERR "{ \"error\": \"Failed to write animation to file\" }"

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

	gifplayer_unlock();
	httpd_resp_send_chunk(req, "{}", strlen("{}"));
	httpd_finalize_response(ctx);
	return ESP_OK;

out_locked:
	gifplayer_unlock();
	return err;
}

#define append_or_flush_dir(...)								\
	do {											\
		int err = append_or_flush_(ctx, strbuf, sizeof(strbuf), &offset, __VA_ARGS__);	\
		if (err < 0) {									\
			gifplayer_unlock();							\
			closedir(dir);								\
			return httpd_send_error(ctx, HTTPD_500);				\
		}										\
	} while (0)

static esp_err_t http_get_animations(struct httpd_request_ctx* ctx, void* priv) {
	char strbuf[64] = { 0 };
	off_t offset = 0;
	esp_err_t err;
	struct dirent* cursor;
	char *path = GIFPLAYER_BASE;
	DIR* dir = opendir(path);
	bool first_entry = true;
	const char *current_animation_path;

	(void)priv;
	if(!dir) {
		ESP_LOGE(TAG, "Failed to open animation directory");
		httpd_send_error(ctx, HTTPD_500);
		return xlate_err(errno);
	}
	gifplayer_lock();
	current_animation_path = gifplayer_get_path_of_playing_animation_();
	append_or_flush_dir("{ \"animations\": [");
	DIRENT_FOR_EACH(cursor, dir) {
		if(!cursor) {
			err = xlate_err(errno);
			goto fail;
		}

		if (!first_entry) {
			append_or_flush_dir(", ");
		}
		append_or_flush_dir("{ \"name\": \"%s\"", cursor->d_name);
		if (current_animation_path) {
			const char *relpath = futil_relpath(current_animation_path, GIFPLAYER_BASE_DIR);

			if (!strcmp(cursor->d_name, relpath)) {
				append_or_flush_dir(", \"active\": true");
			}
		}
		append_or_flush_dir("}");
		first_entry = false;
	}
	append_or_flush_dir("]}");
	if (offset) {
		httpd_resp_send_chunk(ctx->req, strbuf, offset);
	}
	gifplayer_unlock();
	httpd_finalize_response(ctx);
	closedir(dir);
	return ESP_OK;
fail:
	gifplayer_unlock();
	httpd_send_error(ctx, HTTPD_500);
	closedir(dir);
	return err;
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
	gifplayer_unlock();

	unlink(abspath);
	free(abspath);

	httpd_finalize_response(ctx);
	return ESP_OK;
}

void api_init(httpd_t *httpd) {
	DIR* dir = opendir(GIFPLAYER_BASE);
	if (dir) {
		closedir(dir);
	} else {
		ESP_LOGI(TAG, "Animation directroy '"GIFPLAYER_BASE_DIR"' does not exists, creating it");
		remove(GIFPLAYER_BASE);
		if (mkdir(GIFPLAYER_BASE_DIR, 0)) {
			ESP_LOGE(TAG, "Failed to create animation directory: %d\n", errno);
		}
	}

	ESP_ERROR_CHECK(httpd_add_post_handler(httpd, "/api/v1/upload_animation", http_post_upload_animation, NULL, 1, "filename"));
	ESP_ERROR_CHECK(httpd_add_get_handler(httpd, "/api/v1/animations", http_get_animations, NULL, 0));
	ESP_ERROR_CHECK(httpd_add_get_handler(httpd, "/api/v1/set_animation", http_get_set_animation, NULL, 1, "filename"));
	ESP_ERROR_CHECK(httpd_add_get_handler(httpd, "/api/v1/delete_animation", http_get_delete_animation, NULL, 1, "filename"));
}
