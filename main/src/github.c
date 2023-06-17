#include "github.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_log.h>

#define HTTP_TIMEOUT_MS	10000

const char *TAG = "github";

static const char *release_list_format = "https://api.github.com/repos/%s/releases";

static void tag_name_cb(const cbjson_value_t *value, void *priv) {
	if (value->type == CBJSON_TYPE_STRING) {
		ESP_LOGI(TAG, "Got tag name: %s", value->string);
	} else {
		ESP_LOGE(TAG, "Unexpected data type for tag name: %d", value->type);
	}
}

static esp_err_t http_progress_cb(void *data, size_t len, void *ctx) {
	github_release_ctx_t *release = ctx;

	return -cbjson_process(&release->cbjson, data, len);
}

static void http_done_cb(void *ctx) {
	github_release_ctx_t *release = ctx;

	free(release->url);
	release->url = NULL;
	if (release->cb) {
		release->cb(release, 0, release->ctx);
	}
}

static void http_error_cb(void *ctx) {
	github_release_ctx_t *release = ctx;

	free(release->url);
	release->url = NULL;
	if (release->cb) {
		release->cb(release, 1, release->ctx);
	}
}

static const async_http_client_ops_t http_client_ops = {
	.progress = http_progress_cb,
	.done = http_done_cb,
	.error = http_error_cb,
};

esp_err_t github_list_releases(github_release_ctx_t *release, const char *repository, github_release_cb_f cb, void *ctx) {
	memset(release, 0, sizeof(*release));
	size_t url_len = strlen(release_list_format) + strlen(repository);
	char *url;

	release->cb = cb;
	release->ctx = ctx;

	url = malloc(url_len);
	if (!url) {
		ESP_LOGE(TAG, "Failed to allocate URL");
		return -ENOMEM;
	}
	snprintf(url, url_len, release_list_format, repository);
	release->url = url;

	cbjson_init(&release->cbjson);
	cbjson_path_init(&release->cbjson_path_tag, "[]..tag_name", tag_name_cb, release);
	cbjson_add_path(&release->cbjson, &release->cbjson_path_tag);

	async_http_client_init(&release->http_client, &http_client_ops);
	async_http_client_request(&release->http_client, release->url, release);

	return 0;
}
