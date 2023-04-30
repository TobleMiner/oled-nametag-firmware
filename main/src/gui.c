#include "gui.h"

#include <stddef.h>

#include <esp_log.h>

#include "util.h"

static const char *TAG = "gui";

static void gui_element_init(gui_element_t *elem) {
	INIT_LIST_HEAD(elem->list);
	INIT_LIST_HEAD(elem->event_handlers);
	elem->parent = NULL;
	elem->inverted = false;
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

static void gui_fb_invert_area(const gui_fb_t *fb, const gui_area_t *area) {
	unsigned int x, y;

	for (y = 0; y < area->size.y; y++) {
		for (x = 0; x < area->size.x; x++) {
			unsigned int index = y * fb->stride + x;

			fb->pixels[index] = GUI_INVERT_PIXEL(fb->pixels[index]);
		}
	}
}

static void gui_element_render(gui_element_t *elem, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size) {
	if (elem->shown && !elem->hidden && elem->ops->render) {
		elem->ops->render(elem, source_offset, fb, destination_size);
	}

	if (elem->inverted) {
		gui_area_t invert_area = {
			.position = { 0, 0 },
			.size = *destination_size
		};

		invert_area.size.x = MIN(invert_area.size.x, elem->area.size.x);
		invert_area.size.y = MIN(invert_area.size.y, elem->area.size.y);

		gui_fb_invert_area(fb, &invert_area);
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

static bool gui_element_dispatch_event(gui_element_t *element, gui_event_t event) {
	gui_event_handler_t *cursor;

	LIST_FOR_EACH_ENTRY(cursor, &element->event_handlers, list) {
		if (cursor->cfg.event == event) {
			if (cursor->cfg.cb(cursor, element)) {
				return true;
			}
		}
	}

	return false;
}

static void gui_container_render(gui_element_t *element, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size) {
	gui_container_t *container = container_of(element, gui_container_t, element);

	ESP_LOGI(TAG, "Rendering container from [%d, %d] to [%d, %d]...", source_offset->x, source_offset->y, destination_size->x, destination_size->y);

	gui_element_t *cursor;
	LIST_FOR_EACH_ENTRY(cursor, &container->children, list) {
		// Render area realtive to container
		gui_area_t render_area = cursor->area;
		gui_point_t local_source_offset = *source_offset;
		gui_fb_t local_fb = {
			.stride = fb->stride
		};

		// Check if there is anything to render
		if (local_source_offset.x >= render_area.position.x + render_area.size.x ||
		    local_source_offset.y >= render_area.position.y + render_area.size.y) {
			// Applied offsets collapse element to zero size, no need to render it
			ESP_LOGD(TAG, "Source offset skips over render area, nothing to do");
			continue;
		}

		if (render_area.position.x >= local_source_offset.x + destination_size->x ||
		    render_area.position.y >= local_source_offset.y + destination_size->y) {
			// Applied offsets collapse element to zero size, no need to render it
			ESP_LOGD(TAG, "Destination area ends before render area starts, nothing to do");
			continue;
		}

		// Clip render area by destination area
		if (render_area.position.x + render_area.size.x - local_source_offset.x > destination_size->x) {
			render_area.size.x = destination_size->x - render_area.position.x + local_source_offset.x;
			ESP_LOGI(TAG, "Limiting horizontal size of render area to %d", render_area.size.x);
		}
		if (render_area.position.y + render_area.size.y - local_source_offset.y > destination_size->y) {
			render_area.size.y = destination_size->y - render_area.position.y + local_source_offset.y;
			ESP_LOGI(TAG, "Limiting vertical size of render area to %d", render_area.size.y);
		}

		// Calculate effective rendering area position relative to fb
		if (render_area.position.x < local_source_offset.x) {
			render_area.size.x -= local_source_offset.x - render_area.position.x;
			local_source_offset.x -= render_area.position.x;
			render_area.position.x = 0;
		} else {
			render_area.position.x -= local_source_offset.x;
			local_source_offset.x = 0;
		}

		if (render_area.position.y < local_source_offset.y) {
			render_area.size.y -= local_source_offset.y - render_area.position.y;
			local_source_offset.y -= render_area.position.y;
			render_area.position.y = 0;
		} else {
			render_area.position.y -= local_source_offset.y;
			local_source_offset.y = 0;
		}

		local_fb.pixels = &fb->pixels[render_area.position.y * fb->stride + render_area.position.x];

		gui_element_render(cursor, &local_source_offset, &local_fb, &render_area.size);
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

static void gui_list_render(gui_element_t *element, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size) {
	gui_list_t *list = container_of(element, gui_list_t, container.element);
	gui_element_t *selected_entry = list->selected_entry;

	ESP_LOGI(TAG, "Rendering list from [%d, %d] to [%d, %d]...", source_offset->x, source_offset->y, destination_size->x, destination_size->y);

	if (selected_entry) {
		gui_area_t *area = &selected_entry->area;

		if (list->last_y_scroll_pos > area->position.y + 1) {
			// Top of entry would be clipped, scroll to show it
			list->last_y_scroll_pos = area->position.y - 1;
		} else if (area->position.y + area->size.y - list->last_y_scroll_pos + 1 >= destination_size->y) {
			// Bottom of entry would be clipped, scroll to show it
			list->last_y_scroll_pos = area->position.y + area->size.y - destination_size->y + 1;
		}
		ESP_LOGD(TAG, "Need to scroll by %d pixels to show selected entry", list->last_y_scroll_pos);
	}

	gui_element_t *cursor;
	LIST_FOR_EACH_ENTRY(cursor, &list->container.children, list) {
		// Render area relative to list
		gui_point_t scrolled_source_offset = *source_offset;
		gui_area_t render_area = cursor->area;
		gui_fb_t local_fb = {
			.stride = fb->stride
		};

		ESP_LOGD(TAG, "Entry size: %dx%d", render_area.size.x, render_area.size.y);

		scrolled_source_offset.y += list->last_y_scroll_pos;

		// Check if there is anything to render
		if (scrolled_source_offset.x >= render_area.position.x + render_area.size.x ||
		    scrolled_source_offset.y >= render_area.position.y + render_area.size.y) {
			// Element outside render area, no need to render it
			ESP_LOGD(TAG, "Source offset skips over render area, nothing to do");
			continue;
		}

		if (render_area.position.x >= scrolled_source_offset.x + destination_size->x ||
		    render_area.position.y >= scrolled_source_offset.y + destination_size->y) {
			// Element outside render area, no need to render it
			ESP_LOGD(TAG, "Destination area ends before render area starts, nothing to do");
			continue;
		}

		// Calculate effective rendering area position relative to fb
		if (render_area.position.x < scrolled_source_offset.x) {
			render_area.size.x -= scrolled_source_offset.x - render_area.position.x;
			scrolled_source_offset.x -= render_area.position.x;
			render_area.position.x = 0;
		} else {
			render_area.position.x -= scrolled_source_offset.x;
			scrolled_source_offset.x = 0;
		}

		if (render_area.position.y < scrolled_source_offset.y) {
			render_area.size.y -= scrolled_source_offset.y - render_area.position.y;
			scrolled_source_offset.y -= render_area.position.y;
			render_area.position.y = 0;
		} else {
			render_area.position.y -= scrolled_source_offset.y;
			scrolled_source_offset.y = 0;
		}

		local_fb.pixels = &fb->pixels[render_area.position.y * fb->stride + render_area.position.x];

		// Clip rendering area size by destination area
		if (render_area.position.x + render_area.size.x - scrolled_source_offset.x > destination_size->x) {
			render_area.size.x = destination_size->x - render_area.position.x + scrolled_source_offset.x;
			ESP_LOGD(TAG, "Limiting horizontal size of render area to %d", render_area.size.x);
		}
		if (render_area.position.y + render_area.size.y - scrolled_source_offset.y > destination_size->y) {
			render_area.size.y = destination_size->y - render_area.position.y + scrolled_source_offset.y;
			ESP_LOGD(TAG, "Limiting vertical size of render area to %d", render_area.size.y);
		}

		gui_element_render(cursor, &scrolled_source_offset, &local_fb, &render_area.size);

		// Highlight selected entry by inverting its pixels
		if (cursor == list->selected_entry) {
			gui_area_t invert_area = {
				.position = { 0, 0 },
				.size = render_area.size
			};

			ESP_LOGD(TAG, "Inverting %dx%d area", render_area.size.x, render_area.size.y);

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

static bool gui_list_is_selected_entry_valid(gui_list_t *list) {
	gui_element_t *cursor;

	if (list->selected_entry) {
		bool selected_entry_in_list = false;

		LIST_FOR_EACH_ENTRY(cursor, &list->container.children, list) {
			if (cursor == list->selected_entry) {
				selected_entry_in_list = true;
				break;
			}
		}

		if (selected_entry_in_list) {
			return list->selected_entry->shown &&
			       !list->selected_entry->hidden &&
			       list->selected_entry->selectable;
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

	if (gui_list_is_selected_entry_valid(list) &&
	    event->button == BUTTON_ENTER) {
		if (gui_element_dispatch_event(list->selected_entry, GUI_EVENT_CLICK)) {
			*stop_propagation = true;
			return true;
		}
	}

	return false;
}

static void gui_list_update_selection(gui_list_t *list) {
	if (!gui_list_is_selected_entry_valid(list)) {
		gui_element_t *new_selected_entry = gui_list_find_next_selectable_entry(list, &list->container.children, GUI_LIST_SEARCH_DOWN);

		if (new_selected_entry != list->selected_entry) {
			list->selected_entry = new_selected_entry;
			gui_element_invalidate(&list->container.element);
		}
	}
}

static void gui_list_invalidate(gui_element_t *elem) {
	gui_list_t *list = container_of(elem, gui_list_t, container.element);

	gui_list_update_selection(list);
	if (elem->parent) {
		gui_element_invalidate(elem->parent);
	}
}

static const gui_element_ops_t gui_list_ops = {
	.render = gui_list_render,
	.invalidate = gui_list_invalidate,
	.update_shown = gui_container_update_shown,
	.process_button_event = gui_list_process_button_event,
};

gui_element_t *gui_list_init(gui_list_t *list) {
	gui_container_init(&list->container);
	list->container.element.ops = &gui_list_ops;
	list->selected_entry = NULL;
	list->last_y_scroll_pos = 0;
	return &list->container.element;
}

static void gui_image_render(gui_element_t *element, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size) {
	gui_image_t *image = container_of(element, gui_image_t, element);
	int copy_width = MIN(element->area.size.x - source_offset->x, destination_size->x);
	int copy_height = MIN(element->area.size.y - source_offset->y, destination_size->y);
	unsigned int y;

	ESP_LOGI(TAG, "Rendering image from [%d, %d] to [%d, %d]...", source_offset->x, source_offset->y, destination_size->x, destination_size->y);

	for (y = 0; y < copy_height; y++) {
		gui_pixel_t *dst = &fb->pixels[y * fb->stride];
		const uint8_t *src = &image->image_data_start[image->element.area.size.x * (y + source_offset->y)];

		memcpy(dst, src, copy_width * sizeof(gui_pixel_t));
	}
}

static const gui_element_ops_t gui_image_ops = {
	.render = gui_image_render,
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
	gui->container.element.dirty = false;
}

static void gui_check_render(gui_element_t *element) {
	gui_t *gui = container_of(element, gui_t, container.element);

	if (gui->ops->request_render) {
		gui->ops->request_render(gui);
	}
}

static const gui_element_ops_t gui_ops = {
	.check_render = gui_check_render,
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

void gui_element_add_event_handler(gui_element_t *elem, gui_event_handler_t *handler, const gui_event_handler_cfg_t *cfg) {
	handler->cfg = *cfg;
	INIT_LIST_HEAD(handler->list);
	LIST_APPEND_TAIL(&handler->list, &elem->event_handlers);
}

void gui_element_remove_event_handler(gui_event_handler_t *handler) {
	LIST_DELETE(&handler->list);
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

void gui_element_set_inverted(gui_element_t *elem, bool inverted) {
	elem->inverted = inverted;
	gui_element_invalidate(elem);
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
