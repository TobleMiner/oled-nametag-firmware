#pragma once

#include <esp_err.h>
#include <esp_http_client.h>

#include "async_http_client.h"
#include "cbjson.h"
#include "ota.h"

typedef struct github_release_ctx github_release_ctx_t;

typedef void (*github_release_cb_f)(github_release_ctx_t *release, int err, void *ctx);

struct github_release_ctx {
	async_http_client_t http_client;
	char *url;
	cbjson_t cbjson;
	cbjson_path_t cbjson_path_release;
	cbjson_path_t cbjson_path_tag;
	cbjson_path_t cbjson_path_download_url;
	void *ctx;
	github_release_cb_f cb;
	ota_http_ctx_t ota_releases;
	ota_http_firmware_update_t *current_release;
};

esp_err_t github_list_releases(github_release_ctx_t *release, const char *repository, github_release_cb_f cb, void *ctx);
void github_abort(github_release_ctx_t *release);
