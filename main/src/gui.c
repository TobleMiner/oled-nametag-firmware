#include "gui.h"

#include <stddef.h>

#include "util.h"

static void gui_element_init(gui_element_t *elem) {
	INIT_LIST_HEAD(elem->list);
	elem->parent = NULL;
	elem->inverted = false;
}

static void gui_element_invalidate(gui_element_t *elem);
static void gui_element_invalidate(gui_element_t *elem) {
	if (elem->ops->invalidate) {
		elem->ops->invalidate(elem);
	} else {
		if (elem->parent) {
			gui_element_invalidate(elem->parent);
		}
	}
}

static void gui_element_render(gui_element_t *elem, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size) {
	if (elem->ops->render) {
		elem->ops->render(elem, source_offset, fb, destination_size);
	}
}

static void gui_container_render(gui_element_t *element, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size) {
	gui_container_t *container = container_of(element, gui_container_t, element);

	gui_element_t *cursor;
	LIST_FOR_EACH_ENTRY(cursor, &container->children, list) {
		// Render area realtive to container
		gui_area_t render_area = cursor->area;
		gui_fb_t local_fb = {
			.pixels = &fb->pixels[render_area.position.y * fb->stride + render_area.size.x],
			.stride = fb->stride
		};

		// Check if there is anything to render
		if (source_offset->x >= render_area.position.x + render_area.size.x ||
		    source_offset->y >= render_area.position.y + render_area.size.y) {
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

static const gui_element_ops_t gui_container_ops = {
	.render = gui_container_render,
	.display = gui_container_display,
	.remove = gui_container_remove,
	.invalidate = NULL,
	.add_child = NULL
};

gui_element_t *gui_container_init(gui_container_t *container) {
	gui_element_init(&container->element);
	INIT_LIST_HEAD(container->children);
	container->element.ops = &gui_container_ops;
	return &container->element;
}

void gui_element_add_child(gui_element_t *parent, gui_element_t *child) {
	gui_container_t *container = container_of(parent, gui_container_t, element);

	child->parent = parent;
	LIST_APPEND_TAIL(&child->list, &container->children);
	if (parent->ops->add_child) {
		parent->ops->add_child(parent, child);
	}
}

void gui_element_remove_child(gui_element_t *parent, gui_element_t *child) {
	LIST_DELETE(&child->list);
	child->parent = NULL;
}

void gui_element_set_position(gui_element_t *elem, unsigned int x, unsigned int y) {
	elem->area.position.x = x;
	elem->area.position.y = y;
}

void gui_element_set_size(gui_element_t *elem, unsigned int width, unsigned int height) {
	elem->area.size.x = width;
	elem->area.size.y = height;
}

static void gui_list_add_child(gui_element_t *elem, gui_element_t *child) {
	gui_list_t *list = container_of(elem, gui_list_t, container.element);

	if (!list->selected_entry && child->selectible) {
		list->selected_entry = child;
		gui_element_invalidate(child);
	}
}

static const gui_element_ops_t gui_list_ops = {
	.render = gui_container_render,
	.display = gui_container_display,
	.remove = gui_container_remove,
	.invalidate = NULL,
	.add_child = gui_list_add_child,
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
	gui_element_set_size(&image->element, width, height);
	image->image_data_start = image_data_start;
	return &image->element;
}

void gui_render(gui_t *gui, gui_pixel_t *fb, unsigned int stride, const gui_point_t *size) {
	const gui_point_t offset = { 0, 0 };
	gui_fb_t gui_fb = {
		.pixels = fb,
		.stride = stride
	};

	gui_container_render(&gui->container.element, &offset, &gui_fb, size);
}

static void gui_invalidate(gui_element_t *element) {
	gui_t *gui = container_of(element, gui_t, container.element);

	if (gui->ops->request_render) {
		gui->ops->request_render(gui);
	}
}

static const gui_element_ops_t gui_ops = {
	.render = NULL,
	.display = NULL,
	.remove = NULL,
	.invalidate = gui_invalidate,
	.add_child = NULL
};

gui_element_t *gui_init(gui_t *gui, void *priv, const gui_ops_t *ops) {
	gui_container_init(&gui->container);
	gui->container.element.ops = &gui_ops;
	gui->priv = priv;
	gui->ops = ops;
	return &gui->container.element;
}
