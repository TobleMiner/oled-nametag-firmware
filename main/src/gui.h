#pragma once

#include <stdbool.h>
#include <stdint.h>

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

typedef struct gui_fb {
	gui_pixel_t *pixels;
	unsigned int stride;
} gui_fb_t;

typedef struct gui_element gui_element_t;

typedef struct gui_element_ops {
	void (*render)(gui_element_t *element, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size);
	void (*display)(gui_element_t *element);
	void (*remove)(gui_element_t *element);
	void (*invalidate)(gui_element_t *element);
	void (*add_child)(gui_element_t *element, gui_element_t *child);
} gui_element_ops_t;

typedef struct gui_element {
	// Managed properties
	struct list_head list;
	gui_element_t *parent;
	bool inverted;

	// User properties
	bool invertible;
	bool selectible;
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
	void (*request_render)(gui_t *gui);
} gui_ops_t;

struct gui {
	gui_container_t container;

	void *priv;
	const gui_ops_t *ops;
};

// Top level GUI API
gui_element_t *gui_init(gui_t *gui, void *priv, const gui_ops_t *ops);
void gui_render(gui_t *gui, gui_pixel_t *fb, unsigned int stride, const gui_point_t *size);

// Container level GUI API
gui_element_t *gui_container_init(gui_container_t *container);

// Element level GUI API
void gui_element_set_position(gui_element_t *elem, unsigned int x, unsigned int y);
void gui_element_set_size(gui_element_t *elem, unsigned int width, unsigned int height);
void gui_element_add_child(gui_element_t *parent, gui_element_t *child);
void gui_element_remove_child(gui_element_t *parent, gui_element_t *child);

// GUI image widget API
gui_element_t *gui_image_init(gui_image_t *image, unsigned int width, unsigned int height, const uint8_t *image_data_start);

// GUI list widget API
gui_element_t *gui_list_init(gui_list_t *list);
