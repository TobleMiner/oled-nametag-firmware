#include "gui.h"

#include <stddef.h>

#include <esp_log.h>

#include "util.h"

static const char *TAG = "gui";

static void gui_element_init(gui_element_t *elem) {
	INIT_LIST_HEAD(elem->list);
	elem->parent = NULL;
	elem->inverted = false;
}

static void gui_element_get_absolute_position(gui_element_t *elem, gui_point_t *pos) {
	gui_point_t position = { 0, 0 };

	while (elem) {
		position.x += elem->area.position.x;
		position.y += elem->area.position.y;
		elem = elem->parent;
	}

	*pos = position;
}

static void gui_element_check_render(gui_element_t *elem);
static void gui_element_check_render(gui_element_t *elem) {
	if (elem->dirty) {
		if (elem->ops->check_render) {
			elem->ops->check_render(elem);
		} else if (elem->parent) {
			gui_element_check_render(elem->parent);
		}
	}
}

static void gui_element_invalidate(gui_element_t *elem);
static void gui_element_invalidate_ignore_hidden_shown(gui_element_t *elem) {
	elem->dirty = true;
	if (elem->ops->invalidate) {
		elem->ops->invalidate(elem);
	} else if (elem->parent) {
		gui_element_invalidate(elem->parent);
	}
}

static void gui_element_invalidate_ignore_hidden(gui_element_t *elem) {
	if (elem->shown) {
		gui_element_invalidate_ignore_hidden_shown(elem);
	}
}

static void gui_element_invalidate(gui_element_t *elem) {
	if (!elem->hidden) {
		gui_element_invalidate_ignore_hidden(elem);
	}
}

static void gui_element_render(gui_element_t *elem, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size) {
	if (elem->shown && !elem->hidden && elem->ops->render) {
		elem->ops->render(elem, source_offset, fb, destination_size);
	}
	elem->dirty = false;
}

static void gui_element_set_shown(gui_element_t *element, bool shown) {
	element->shown = shown;
	if (element->ops->update_shown) {
		element->ops->update_shown(element);
	}
	gui_element_invalidate_ignore_hidden_shown(element);
}

static void gui_container_render(gui_element_t *element, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size) {
	gui_container_t *container = container_of(element, gui_container_t, element);
	gui_point_t pos;

	gui_element_get_absolute_position(element, &pos);
	ESP_LOGI(TAG, "Rendering container @[%u, %u]...", pos.x, pos.y);

	gui_element_t *cursor;
	LIST_FOR_EACH_ENTRY(cursor, &container->children, list) {
		// Render area realtive to container
		gui_area_t render_area = cursor->area;
		gui_fb_t local_fb = {
			.pixels = &fb->pixels[render_area.position.y * fb->stride + render_area.position.x],
			.stride = fb->stride
		};

		// Check if there is anything to render
		if (source_offset->x >= render_area.position.x + render_area.size.x ||
		    source_offset->y >= render_area.position.y + render_area.size.y) {
			// Applied offsets collapse element to zero size, no need to render it
			continue;
		}

		if (render_area.position.x >= source_offset->x + destination_size->x ||
		    render_area.position.y >= source_offset->y + destination_size->y) {
			// Applied offsets collapse element to zero size, no need to render it
			continue;
		}

		// Clip rendering area by destination area
		if (render_area.position.x + render_area.size.x - source_offset->x > destination_size->x) {
			render_area.size.x = source_offset->x + destination_size->x - render_area.position.x;
		}
		if (render_area.position.y + render_area.size.y - source_offset->y > destination_size->y) {
			render_area.size.y = source_offset->y + destination_size->y - render_area.position.y;
		}

		gui_element_render(cursor, source_offset, &local_fb, &render_area.size);
	}
}

static void gui_container_display(gui_element_t *element) {
	gui_container_t *container = container_of(element, gui_container_t, element);

	gui_element_t *cursor;
	LIST_FOR_EACH_ENTRY(cursor, &container->children, list) {
		if (cursor->ops->display) {
			cursor->ops->display(element);
		}
	}
}

static void gui_container_remove(gui_element_t *element) {
	gui_container_t *container = container_of(element, gui_container_t, element);

	gui_element_t *cursor;
	LIST_FOR_EACH_ENTRY(cursor, &container->children, list) {
		if (cursor->ops->remove) {
			cursor->ops->remove(element);
		}
	}
}

static bool gui_container_process_button_event(gui_element_t *element, const button_event_t *event, bool *stop_propagation) {
	gui_container_t *container = container_of(element, gui_container_t, element);

	gui_element_t *cursor;
	LIST_FOR_EACH_ENTRY(cursor, &container->children, list) {
		if (cursor->shown && !cursor->hidden && cursor->ops->process_button_event) {
			if (cursor->ops->process_button_event(cursor, event, stop_propagation)) {
				return true;
			}
		}
	}

	return false;
}

static void gui_container_update_shown(gui_element_t *element) {
	gui_container_t *container = container_of(element, gui_container_t, element);

	gui_element_t *cursor;
	LIST_FOR_EACH_ENTRY(cursor, &container->children, list) {
		gui_element_set_shown(cursor, container->element.shown);
	}
}

static const gui_element_ops_t gui_container_ops = {
	.render = gui_container_render,
	.display = gui_container_display,
	.remove = gui_container_remove,
	.invalidate = NULL,
	.add_child = NULL,
	.update_shown = gui_container_update_shown,
	.process_button_event = gui_container_process_button_event,
};

gui_element_t *gui_container_init(gui_container_t *container) {
	gui_element_init(&container->element);
	INIT_LIST_HEAD(container->children);
	container->element.ops = &gui_container_ops;
	return &container->element;
}

static void gui_element_set_size_(gui_element_t *elem, unsigned int width, unsigned int height) {
	elem->area.size.x = width;
	elem->area.size.y = height;
	gui_element_invalidate(elem);
}

static void gui_list_add_child(gui_element_t *elem, gui_element_t *child) {
	gui_list_t *list = container_of(elem, gui_list_t, container.element);

	if (!list->selected_entry && child->selectable) {
		list->selected_entry = child;
		gui_element_invalidate(&list->container.element);
	}
}

static void gui_fb_invert_area(const gui_fb_t *fb, const gui_area_t *area) {
	unsigned int x, y;

	for (y = 0; y < area->size.y; y++) {
		for (x = 0; x < area->size.x; x++) {
			unsigned int index = y * fb->stride + x;

			fb->pixels[index] = GUI_INVERT_PIXEL(fb->pixels[index]);
		}
	}
}

static void gui_list_render(gui_element_t *element, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size) {
	gui_list_t *list = container_of(element, gui_list_t, container.element);
	gui_point_t pos;

	gui_element_get_absolute_position(element, &pos);
	ESP_LOGI(TAG, "Rendering list @[%u, %u]...", pos.x, pos.y);

	gui_element_t *cursor;
	LIST_FOR_EACH_ENTRY(cursor, &list->container.children, list) {
		// Render area realtive to list
		gui_area_t render_area = cursor->area;
		gui_fb_t local_fb = {
			.pixels = &fb->pixels[render_area.position.y * fb->stride + render_area.position.x],
			.stride = fb->stride
		};

		// Check if there is anything to render
		if (source_offset->x >= render_area.position.x + render_area.size.x ||
		    source_offset->y >= render_area.position.y + render_area.size.y) {
			// Applied offsets collapse element to zero size, no need to render it
			continue;
		}

		if (render_area.position.x >= source_offset->x + destination_size->x ||
		    render_area.position.y >= source_offset->y + destination_size->y) {
			// Applied offsets collapse element to zero size, no need to render it
			continue;
		}

		// Clip rendering area by destination area
		if (render_area.position.x + render_area.size.x - source_offset->x > destination_size->x) {
			render_area.size.x = source_offset->x + destination_size->x - render_area.position.x;
		}
		if (render_area.position.y + render_area.size.y - source_offset->y > destination_size->y) {
			render_area.size.y = source_offset->y + destination_size->y - render_area.position.y;
		}

		gui_element_render(cursor, source_offset, &local_fb, &render_area.size);

		// Highlight selected entry by inverting its pixels
		if (cursor == list->selected_entry) {
			gui_area_t invert_area = {
				.position = {0, 0},
				.size = render_area.size
			};

			gui_fb_invert_area(&local_fb, &invert_area);
		}
	}
}

typedef enum gui_list_search_direction {
	GUI_LIST_SEARCH_DOWN,
	GUI_LIST_SEARCH_UP
} gui_list_search_direction_t;

static gui_element_t *gui_list_find_next_selectable_entry(gui_list_t *list, struct list_head *start, gui_list_search_direction_t direction) {
	struct list_head *cursor = start;

	do {
		if (direction == GUI_LIST_SEARCH_DOWN) {
			cursor = cursor->next;
		} else {
			cursor = cursor->prev;
		}

		if (cursor != &list->container.children) {
			gui_element_t *elem = container_of(cursor, gui_element_t, list);

			if (elem->shown && !elem->hidden && elem->selectable) {
				return elem;
			}
		}
	} while (cursor != start);

	return NULL;
}

static bool gui_list_scroll_up_down(gui_list_t *list, gui_list_search_direction_t direction) {
	if (list->selected_entry) {
		gui_element_t *elem = gui_list_find_next_selectable_entry(list, &list->selected_entry->list, direction);

		if (elem && elem != list->selected_entry) {
			list->selected_entry = elem;
			return true;
		}
	}

	return false;
}

static bool gui_list_process_button_event(gui_element_t *elem, const button_event_t *event, bool *stop_propagation) {
	gui_list_t *list = container_of(elem, gui_list_t, container.element);

	if (event->action == BUTTON_ACTION_RELEASE &&
	   (event->button == BUTTON_UP ||
	    event->button == BUTTON_DOWN)) {
		if (gui_list_scroll_up_down(list, event->button == BUTTON_DOWN ? GUI_LIST_SEARCH_DOWN : GUI_LIST_SEARCH_UP)) {
			gui_element_invalidate(&list->container.element);
		}
		return true;
	}

	return false;
}

static void gui_list_update_selection(gui_list_t *list) {
	bool selected_entry_in_list = false;
	bool selected_entry_valid;
	gui_element_t *cursor;

	if (list->selected_entry) {
		LIST_FOR_EACH_ENTRY(cursor, &list->container.children, list) {
			if (cursor == list->selected_entry) {
				selected_entry_in_list = true;
				break;
			}
		}
	}

	if (selected_entry_in_list) {
		selected_entry_valid =
				list->selected_entry->shown &&
				!list->selected_entry->hidden &&
				list->selected_entry->selectable;
	} else {
		selected_entry_valid = false;
	}

	if (!selected_entry_valid) {
		list->selected_entry = gui_list_find_next_selectable_entry(list, &list->container.children, GUI_LIST_SEARCH_DOWN);
	}
}

static void gui_list_update_shown(gui_element_t *elem) {
	gui_list_t *list = container_of(elem, gui_list_t, container.element);

	gui_container_update_shown(&list->container.element);
	gui_list_update_selection(list);
}

static const gui_element_ops_t gui_list_ops = {
	.render = gui_list_render,
	.display = gui_container_display,
	.remove = gui_container_remove,
	.invalidate = NULL,
	.add_child = gui_list_add_child,
	.update_shown = gui_list_update_shown,
	.process_button_event = gui_list_process_button_event,
};

gui_element_t *gui_list_init(gui_list_t *list) {
	gui_container_init(&list->container);
	list->container.element.ops = &gui_list_ops;
	list->selected_entry = NULL;
	return &list->container.element;
}

static void gui_image_render(gui_element_t *element, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size) {
	gui_image_t *image = container_of(element, gui_image_t, element);
	unsigned int copy_width = destination_size->x;
	unsigned int copy_height = destination_size->y;
	unsigned int y;
	gui_point_t pos;

	gui_element_get_absolute_position(element, &pos);
	ESP_LOGI(TAG, "Rendering image @[%u, %u]...", pos.x, pos.y);

	for (y = 0; y < copy_height; y++) {
		gui_pixel_t *dst = &fb->pixels[y * fb->stride];
		const uint8_t *src = &image->image_data_start[image->element.area.size.x * (y + source_offset->y)];

		memcpy(dst, src, copy_width * sizeof(gui_pixel_t));
	}
}

static const gui_element_ops_t gui_image_ops = {
	.render = gui_image_render,
	.display = NULL,
	.remove = NULL,
	.invalidate = NULL,
	.add_child = NULL,
};

gui_element_t *gui_image_init(gui_image_t *image, unsigned int width, unsigned int height, const uint8_t *image_data_start) {
	gui_element_init(&image->element);
	image->element.ops = &gui_image_ops;
	gui_element_set_size_(&image->element, width, height);
	image->image_data_start = image_data_start;
	return &image->element;
}

static void gui_fb_memset(const gui_fb_t *fb, gui_pixel_t color, const gui_point_t *size) {
	unsigned int x, y;

	for (y = 0; y < size->y; y++) {
		for (x = 0; x < size->x; x++) {
			fb->pixels[y * fb->stride + x] = color;
		}
	}
}

void gui_render(gui_t *gui, gui_pixel_t *fb, unsigned int stride, const gui_point_t *size) {
	const gui_point_t offset = { 0, 0 };
	gui_fb_t gui_fb = {
		.pixels = fb,
		.stride = stride
	};

	gui_fb_memset(&gui_fb, GUI_COLOR_BLACK, size);
	gui_container_render(&gui->container.element, &offset, &gui_fb, size);
}

static void gui_check_render(gui_element_t *element) {
	gui_t *gui = container_of(element, gui_t, container.element);

	if (gui->ops->request_render) {
		gui->ops->request_render(gui);
	}
}

static const gui_element_ops_t gui_ops = {
	.render = NULL,
	.display = NULL,
	.remove = NULL,
	.invalidate = NULL,
	.check_render = gui_check_render,
	.add_child = NULL
};

gui_element_t *gui_init(gui_t *gui, void *priv, const gui_ops_t *ops) {
	gui_container_init(&gui->container);
	gui->container.element.ops = &gui_ops;
	gui->container.element.hidden = false;
	gui->container.element.shown = true;
	gui->priv = priv;
	gui->ops = ops;
	gui->lock = xSemaphoreCreateMutexStatic(&gui->lock_buffer);
	return &gui->container.element;
}

void gui_lock(gui_t *gui) {
	xSemaphoreTake(gui->lock, portMAX_DELAY);
}

void gui_unlock(gui_t *gui) {
	xSemaphoreGive(gui->lock);
}

// User API functions that might require rerendering
void gui_element_set_position(gui_element_t *elem, unsigned int x, unsigned int y) {
	elem->area.position.x = x;
	elem->area.position.y = y;
	gui_element_invalidate(elem);
	gui_element_check_render(elem);
}

void gui_element_set_size(gui_element_t *elem, unsigned int width, unsigned int height) {
	gui_element_set_size_(elem, width, height);
	gui_element_check_render(elem);
}

void gui_element_set_selectable(gui_element_t *elem, bool selectable) {
	elem->selectable = selectable;
	gui_element_invalidate(elem);
	gui_element_check_render(elem);
}

void gui_element_set_hidden(gui_element_t *elem, bool hidden) {
	elem->hidden = hidden;
	gui_element_invalidate_ignore_hidden(elem);
	gui_element_check_render(elem);
}

void gui_element_show(gui_element_t *elem) {
	gui_element_set_shown(elem, true);
	gui_element_check_render(elem);
}

void gui_element_add_child(gui_element_t *parent, gui_element_t *child) {
	gui_container_t *container = container_of(parent, gui_container_t, element);

	child->parent = parent;
	LIST_APPEND_TAIL(&child->list, &container->children);
	if (parent->ops->add_child) {
		parent->ops->add_child(parent, child);
	}
	gui_element_check_render(parent);
}

void gui_element_remove_child(gui_element_t *parent, gui_element_t *child) {
	gui_element_set_shown(child, false);
	LIST_DELETE(&child->list);
	child->parent = NULL;
	gui_element_check_render(parent);
}

bool gui_process_button_event(gui_t *gui, const button_event_t *event) {
	bool stop_propagation = false;

	gui_container_process_button_event(&gui->container.element, event, &stop_propagation);
	gui_element_check_render(&gui->container.element);

	return stop_propagation;
}
