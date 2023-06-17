#include "ota.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

void ota_http_update_init(ota_http_ctx_t *ota_http_ctx) {
	INIT_LIST_HEAD(ota_http_ctx->releases);
}

void ota_http_update_add_release(ota_http_ctx_t *ota_http_ctx, ota_http_firmware_update_t *update) {
	LIST_APPEND(&update->list, &ota_http_ctx->releases);
}

void ota_http_update_free_releases(ota_http_ctx_t *ota_http_ctx) {
	ota_http_firmware_update_t *cursor;
	struct list_head *next;

	LIST_FOR_EACH_ENTRY_SAFE(cursor, next, &ota_http_ctx->releases, list) {
		free(cursor->name);
		free(cursor->url);
		free(cursor);
	}
}

ota_http_firmware_update_t *ota_http_firmware_update_new(void) {
	ota_http_firmware_update_t *update = calloc(1, sizeof(ota_http_firmware_update_t));

	if (!update) {
		return NULL;
	}

	INIT_LIST_HEAD(update->list);
	return update;
}

static int ota_http_firmware_update_set_prop_strdup(const char *s, char **dst) {
	char *str = strdup(s);
	if (!str) {
		return -ENOMEM;
	}

	if (*dst) {
		free(*dst);
	}

	*dst = str;
	return 0;
}

int ota_http_firmware_update_set_name(ota_http_firmware_update_t *update, const char *name) {
	return ota_http_firmware_update_set_prop_strdup(name, &update->name);
}

int ota_http_firmware_update_set_url(ota_http_firmware_update_t *update, const char *url) {
	return ota_http_firmware_update_set_prop_strdup(url, &update->url);
}
