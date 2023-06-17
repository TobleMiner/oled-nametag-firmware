#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_err.h>

#define ASYNC_HTTP_TASK_STACK_SIZE 	4096
#define ASYNC_HTTP_TASK_STACK_DEPTH	(ASYNC_HTTP_TASK_STACK_SIZE / sizeof(StackType_t))

typedef struct async_http_client_ops {
	esp_err_t (*progress)(void *data, size_t len, void *ctx);
	void (*done)(void *ctx);
	void (*error)(void *ctx);
} async_http_client_ops_t;

typedef struct async_http_client {
	TaskHandle_t task;
	StaticTask_t task_buffer;
	StackType_t task_stack[ASYNC_HTTP_TASK_STACK_DEPTH];
	void *ctx;
	const async_http_client_ops_t *ops;
	const char *url;
} async_http_client_t;

void async_http_client_init(async_http_client_t *client, const async_http_client_ops_t *ops);
void async_http_client_request(async_http_client_t *client, const char *url, void *ctx);