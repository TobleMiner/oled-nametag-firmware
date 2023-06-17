#include "async_http_client.h"

#include <string.h>

#include <esp_http_client.h>
#include <esp_timer.h>

#include "util.h"

#define HTTP_TIMEOUT_MS 10000

static esp_err_t http_event_cb(esp_http_client_event_t *evt) {
	async_http_client_t *client = evt->user_data;
	const async_http_client_ops_t *ops = client->ops;

	switch (evt->event_id) {
	case HTTP_EVENT_ERROR:
		if (ops->error) {
			ops->error(client);
		}
		break;
	case HTTP_EVENT_ON_DATA:
		if (ops->progress) {
			return ops->progress(evt->data, evt->data_len, client);
		}
		break;
	case HTTP_EVENT_ON_FINISH:
		if (ops->done) {
			ops->done(client);
		}
		break;
	default:
		break;
	}

	return ESP_OK;
}

static void async_http_client_run(void *arg) {
	async_http_client_t *client = arg;
	const async_http_client_ops_t *ops = client->ops;
	int err;
	bool is_https = !strncmp(client->url, "https", strlen("https"));
	esp_http_client_config_t http_client_cfg = {
		.method = HTTP_METHOD_GET,
		.timeout_ms = HTTP_TIMEOUT_MS,
		.transport_type = is_https ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP,
		.event_handler = http_event_cb,
		.buffer_size = 1024,
		.buffer_size_tx = 256,
		.user_data = client,
		.is_async = false,
		.skip_cert_common_name_check = true,
		.url = client->url,
	};
	esp_http_client_handle_t http_client;
	int64_t deadline = esp_timer_get_time() + MS_TO_US(HTTP_TIMEOUT_MS);

	http_client = esp_http_client_init(&http_client_cfg);
	if (!http_client) {
		if (ops->error) {
			ops->error(client);
		}
		return;
	}

	do {
		err = esp_http_client_perform(http_client);
	} while (err == ESP_ERR_HTTP_EAGAIN && esp_timer_get_time() < deadline);

	if (err) {
		if (ops->error) {
			ops->error(client);
		}
	}

	esp_http_client_cleanup(http_client);
	vTaskDelete(NULL);
}

void async_http_client_init(async_http_client_t *client, const async_http_client_ops_t *ops) {
	client->ops = ops;
}

void async_http_client_request(async_http_client_t *client, const char *url, void *ctx) {
	client->url = url;
	client->ctx = ctx;
	client->task = xTaskCreateStatic(async_http_client_run, "async_http", ASYNC_HTTP_TASK_STACK_DEPTH,
					 client, 1, client->task_stack, &client->task_buffer);
}