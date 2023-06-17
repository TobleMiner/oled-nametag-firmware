#pragma once

#include "list.h"

typedef struct ota_http_ctx {
	struct list_head releases;
} ota_http_ctx_t;

typedef struct ota_http_firmware_update {
	struct list_head list;
	char *name;
	char *url;
} ota_http_firmware_update_t;

void ota_http_update_init(ota_http_ctx_t *ota_http_ctx);
void ota_http_update_add_release(ota_http_ctx_t *ota_http_ctx, ota_http_firmware_update_t *update);
void ota_http_update_free_releases(ota_http_ctx_t *ota_http_ctx);

ota_http_firmware_update_t *ota_http_firmware_update_new(void);
int ota_http_firmware_update_set_name(ota_http_firmware_update_t *update, const char *name);
int ota_http_firmware_update_set_url(ota_http_firmware_update_t *update, const char *url);
