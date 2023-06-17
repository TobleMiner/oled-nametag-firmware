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

static int release_cb(const cbjson_value_t *value, void *priv) {
	github_release_ctx_t *github_release = priv;
	ota_http_firmware_update_t *release;
	ESP_LOGI(TAG, "Got release");

	if (github_release->current_release) {
		ota_http_update_add_release(&github_release->ota_releases,
					    github_release->current_release);
		github_release->current_release = NULL;
	}

	release = ota_http_firmware_update_new();
	if (!release) {
		return -ENOMEM;
	}

	github_release->current_release = release;
	return 0;
}

static int tag_name_cb(const cbjson_value_t *value, void *priv) {
	github_release_ctx_t *github_release = priv;

	if (value->type != CBJSON_TYPE_STRING) {
		ESP_LOGE(TAG, "Unexpected data type for tag name: %d", value->type);
		return 0;
	}

	if (!github_release->current_release) {
		ESP_LOGE(TAG, "Found tag name but release has not been setup yet");
		return 0;
	}

	ESP_LOGI(TAG, "\tGot tag name: %s", value->string);
	return ota_http_firmware_update_set_name(github_release->current_release, value->string);
}

static int download_url_cb(const cbjson_value_t *value, void *priv) {
	github_release_ctx_t *github_release = priv;

	if (value->type != CBJSON_TYPE_STRING) {
		ESP_LOGE(TAG, "\tUnexpected data type for asset download url: %d", value->type);
		return 0;
	}

	if (!github_release->current_release) {
		ESP_LOGE(TAG, "Found asset download url but release has not been setup yet");
		return 0;
	}

	ESP_LOGI(TAG, "\tGot asset download url: %s", value->string);
	return ota_http_firmware_update_set_url(github_release->current_release, value->string);
}

static esp_err_t http_progress_cb(void *data, size_t len, void *ctx) {
	github_release_ctx_t *release = ctx;

	return -cbjson_process(&release->cbjson, data, len);
}

static void http_done_cb(void *ctx) {
	github_release_ctx_t *release = ctx;

	free(release->url);
	release->url = NULL;

	if (release->current_release) {
		ota_http_update_add_release(&release->ota_releases,
					    release->current_release);
		release->current_release = NULL;
	}

	if (release->cb) {
		release->cb(release, 0, release->ctx);
	}
}

static void http_error_cb(void *ctx) {
	github_release_ctx_t *release = ctx;

	free(release->url);
	release->url = NULL;

	if (release->current_release) {
		free(release->current_release->name);
		free(release->current_release->url);
		free(release->current_release);
		release->current_release = NULL;
	}

	ota_http_update_free_releases(&release->ota_releases);

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
	release->current_release = NULL;
	ota_http_update_init(&release->ota_releases);

	url = malloc(url_len);
	if (!url) {
		ESP_LOGE(TAG, "Failed to allocate URL");
		return -ENOMEM;
	}
	snprintf(url, url_len, release_list_format, repository);
	release->url = url;

	cbjson_init(&release->cbjson);
	cbjson_path_init(&release->cbjson_path_release, "[]..", release_cb, release);
	cbjson_add_path(&release->cbjson, &release->cbjson_path_release);
	cbjson_path_init(&release->cbjson_path_tag, "[]..tag_name", tag_name_cb, release);
	cbjson_add_path(&release->cbjson, &release->cbjson_path_tag);
	cbjson_path_init(&release->cbjson_path_download_url, "[]..assets.[].browser_download_url", download_url_cb, release);
	cbjson_add_path(&release->cbjson, &release->cbjson_path_download_url);

	async_http_client_init(&release->http_client, &http_client_ops);
	async_http_client_request(&release->http_client, release->url, release);

	return 0;
}
