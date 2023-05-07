#include "scheduler.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp_err.h>
#include <esp_timer.h>

#define SCHEDULER_TASK_STACK_SIZE 	4096
#define SCHEDULER_TASK_STACK_DEPTH	(SCHEDULER_TASK_STACK_SIZE / sizeof(StackType_t))

typedef struct scheduler {
	struct list_head tasks;
	TaskHandle_t task;
	StackType_t task_stack[SCHEDULER_TASK_STACK_DEPTH];
	StaticTask_t task_buffer;
	esp_timer_handle_t timer;
	SemaphoreHandle_t lock;
	StaticSemaphore_t lock_buffer;
	bool timer_running;
	int64_t timer_deadline_us;
} scheduler_t;

static scheduler_t scheduler_g;

void scheduler_timer_cb(void *arg) {
	scheduler_t *scheduler = arg;

	scheduler->timer_running = false;
	xTaskNotifyGive(scheduler->task);
}

static void start_timer_for_task(scheduler_task_t *task) {
	scheduler_t *scheduler = &scheduler_g;
	int64_t now = esp_timer_get_time();

	esp_timer_start_once(scheduler->timer, task->deadline_us > now ? task->deadline_us - now : 0);
}

void scheduler_run(void *arg) {
	scheduler_t *scheduler = arg;

	while (1) {
		uint64_t now;
		scheduler_task_t *cursor;
		struct list_head *next;

		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		xSemaphoreTake(scheduler->lock, portMAX_DELAY);
		now = esp_timer_get_time();
		LIST_FOR_EACH_ENTRY_SAFE(cursor, next, &scheduler->tasks, list) {
			if (now >= cursor->deadline_us) {
				LIST_DELETE(&cursor->list);
				xSemaphoreGive(scheduler->lock);
				cursor->cb(cursor->ctx);
				xSemaphoreTake(scheduler->lock, portMAX_DELAY);
			} else {
				break;
			}
		}
		if (!scheduler->timer_running && !LIST_IS_EMPTY(&scheduler->tasks)) {
			scheduler_task_t *task = LIST_GET_ENTRY(scheduler->tasks.next, scheduler_task_t, list);

			start_timer_for_task(task);
		}
		xSemaphoreGive(scheduler->lock);
	}
}

void scheduler_init() {
	scheduler_t *scheduler = &scheduler_g;
	esp_timer_create_args_t timer_args = {
		.callback = scheduler_timer_cb,
		.arg = scheduler,
		.dispatch_method = ESP_TIMER_TASK,
		.skip_unhandled_events = true
	};

	INIT_LIST_HEAD(scheduler->tasks);
	ESP_ERROR_CHECK(esp_timer_create(&timer_args, &scheduler->timer));
	scheduler->lock = xSemaphoreCreateMutexStatic(&scheduler->lock_buffer);
	scheduler->task = xTaskCreateStatic(scheduler_run, "scheduler", SCHEDULER_TASK_STACK_DEPTH,
					    scheduler, 1, scheduler->task_stack, &scheduler->task_buffer);
	scheduler->timer_running = false;
}

void scheduler_schedule_task(scheduler_task_t *task, scheduler_cb_f cb, void *ctx, int64_t deadline_us) {
	scheduler_t *scheduler = &scheduler_g;
	scheduler_task_t *cursor;
	struct list_head *prior_deadline = &scheduler->tasks;

	INIT_LIST_HEAD(task->list);
	task->deadline_us = deadline_us;
	task->cb = cb;
	task->ctx = ctx;

	xSemaphoreTake(scheduler->lock, portMAX_DELAY);
	LIST_FOR_EACH_ENTRY(cursor, &scheduler->tasks, list) {
		if (cursor->deadline_us > deadline_us) {
			break;
		}
		prior_deadline = &cursor->list;
	}
	LIST_APPEND(&task->list, prior_deadline);
	if (scheduler->timer_deadline_us > task->deadline_us && scheduler->timer_running) {
		esp_timer_stop(scheduler->timer);
		scheduler->timer_running = false;
	}
	if (!scheduler->timer_running) {
		start_timer_for_task(task);
	}
	xSemaphoreGive(scheduler->lock);
}

void scheduler_schedule_task_relative(scheduler_task_t *task, scheduler_cb_f cb, void *ctx, int64_t timeout_us) {
	int64_t now = esp_timer_get_time();

	scheduler_schedule_task(task, cb, ctx, now + timeout_us);
}

