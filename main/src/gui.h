#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "buttons.h"
#include "list.h"

typedef struct gui_point {
	int x;
	int y;
} gui_point_t;

typedef struct gui_area {
	gui_point_t position;
	gui_point_t size;
} gui_area_t;

typedef uint8_t gui_pixel_t;
#define GUI_INVERT_PIXEL(px_) (((1U << (sizeof(gui_pixel_t) * 8)) - 1) - (px_))
#define GUI_COLOR_BLACK	0

typedef struct gui_fb {
	gui_pixel_t *pixels;
	unsigned int stride;
} gui_fb_t;

typedef struct gui_element gui_element_t;

typedef struct gui_element_ops {
	void (*render)(gui_element_t *element, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size);
	void (*invalidate)(gui_element_t *element);
	void (*update_shown)(gui_element_t *element);
	void (*check_render)(gui_element_t *element);
	bool (*process_button_event)(gui_element_t *element, const button_event_t *event, bool *stop_propagation);
} gui_element_ops_t;

typedef struct gui_element {
	// Managed properties
	struct list_head list;
	struct list_head event_handlers;
	gui_element_t *parent;
	bool shown;
	bool dirty;

	// User properties
	bool selectable;
	bool hidden;
	bool inverted;
	gui_area_t area;
	const gui_element_ops_t *ops;
} gui_element_t;

typedef struct gui_container {
	gui_element_t element;

	// Managed properties
	struct list_head children;
} gui_container_t;

typedef struct gui_list {
	gui_container_t container;

	// Managed properties
	gui_element_t *selected_entry;
} gui_list_t;

typedef struct gui_image {
	gui_element_t element;
	const uint8_t *image_data_start;
} gui_image_t;

typedef struct gui gui_t;

typedef struct gui_ops {
	void (*request_render)(const gui_t *gui);
} gui_ops_t;

struct gui {
	gui_container_t container;

	SemaphoreHandle_t lock;
	StaticSemaphore_t lock_buffer;
	void *priv;
	const gui_ops_t *ops;
};

typedef struct gui_event_handler gui_event_handler_t;

typedef enum gui_event {
	GUI_EVENT_CLICK
} gui_event_t;

typedef struct gui_event_handler_cfg {
	gui_event_t event;
	void *priv;
	bool (*cb)(const gui_event_handler_t *event, gui_element_t *elem);
} gui_event_handler_cfg_t;

struct gui_event_handler {
	struct list_head list;
	gui_event_handler_cfg_t cfg;
};

// Top level GUI API
gui_element_t *gui_init(gui_t *gui, void *priv, const gui_ops_t *ops);
void gui_render(gui_t *gui, gui_pixel_t *fb, unsigned int stride, const gui_point_t *size);
bool gui_process_button_event(gui_t *gui, const button_event_t *event);
void gui_lock(gui_t *gui);
void gui_unlock(gui_t *gui);

// Container level GUI API
gui_element_t *gui_container_init(gui_container_t *container);

// Element level GUI API
void gui_element_set_position(gui_element_t *elem, unsigned int x, unsigned int y);
void gui_element_set_size(gui_element_t *elem, unsigned int width, unsigned int height);
void gui_element_set_selectable(gui_element_t *elem, bool selectable);
void gui_element_set_hidden(gui_element_t *elem, bool hidden);
void gui_element_set_inverted(gui_element_t *elem, bool inverted);
void gui_element_show(gui_element_t *elem);
void gui_element_add_child(gui_element_t *parent, gui_element_t *child);
void gui_element_remove_child(gui_element_t *parent, gui_element_t *child);
void gui_element_add_event_handler(gui_element_t *elem, gui_event_handler_t *handler, const gui_event_handler_cfg_t *cfg);
void gui_element_remove_event_handler(gui_event_handler_t *handler);

// GUI image widget API
gui_element_t *gui_image_init(gui_image_t *image, unsigned int width, unsigned int height, const uint8_t *image_data_start);

// GUI list widget API
gui_element_t *gui_list_init(gui_list_t *list);
