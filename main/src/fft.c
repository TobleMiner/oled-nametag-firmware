#include "fft.h"

#include <math.h>
#include <stdint.h>

#include "gui_priv.h"
#include "microphone.h"
#include "util.h"

#define LEVEL_MIN -60
#define LEVEL_MAX  20

static int gui_fft_render(gui_element_t *element, const gui_point_t *source_offset, const gui_fb_t *fb, const gui_point_t *destination_size) {
	gui_fft_t *fft = container_of(element, gui_fft_t, element);
	int width = destination_size->x;
	int height = destination_size->y;

	microphone_get_last_fft(fft->fft_data, ARRAY_SIZE(fft->fft_data));
	for (int i = 0; i < MIN(width, ARRAY_SIZE(fft->fft_data)); i++) {
		float min_level = LEVEL_MIN;
		float max_level = LEVEL_MAX;
		float delta_level = max_level - min_level;
		float datum = fft->fft_data[i];
		datum = MIN(MAX(min_level, datum), max_level);
		float offset = datum - min_level;
		float level = offset / delta_level;
		float pos = level * (float)height;
		pos = MIN(MAX(0, pos), (float)(height - 1));
		int y = (height - 1) - (int)roundf(pos);
		fb->pixels[y * fb->stride + i] = 255;
	}

	// Update FFT view 50 times per second
	return 20;
}

static const gui_element_ops_t gui_fft_ops = {
	.render = gui_fft_render,
};

gui_element_t *gui_fft_init(gui_fft_t *fft) {
	return gui_element_init(&fft->element, &gui_fft_ops);
}



static gui_container_t app_container;

static gui_fft_t gui_fft;

static gui_t *gui;

static button_event_handler_t button_event_handler;

static menu_cb_f menu_cb = NULL;
static void *menu_cb_ctx;

static bool on_button_event(const button_event_t *event, void *priv) {
	if (event->button == BUTTON_EXIT) {
		buttons_disable_event_handler(&button_event_handler);
		gui_element_set_hidden(&app_container.element, true);
		menu_cb(menu_cb_ctx);
		return true;
	}

	return false;
}

void fft_init(gui_t *gui_root) {
	button_event_handler_multi_user_cfg_t button_event_cfg = {
		.base = {
			.cb = on_button_event
		},
		.multi = {
			.button_filter = (1 << BUTTON_EXIT),
			.action_filter = (1 << BUTTON_ACTION_RELEASE)
		}
	};

	gui = gui_root;

	gui_container_init(&app_container);
	gui_element_set_size(&app_container.element, 256, 64);
	gui_element_set_hidden(&app_container.element, true);
	gui_element_add_child(&gui->container.element, &app_container.element);

	gui_fft_init(&gui_fft);
	gui_element_set_position(&gui_fft.element, 0, 0);
	gui_element_set_size(&gui_fft.element, 256, 64);
	gui_element_add_child(&app_container.element, &gui_fft.element);

	buttons_register_multi_button_event_handler(&button_event_handler, &button_event_cfg);
}

int fft_run(menu_cb_f exit_cb, void *cb_ctx, void *priv) {
	menu_cb = exit_cb;
	menu_cb_ctx = cb_ctx;
	gui_element_set_hidden(&app_container.element, false);
	gui_element_show(&app_container.element);
	buttons_enable_event_handler(&button_event_handler);
	return 0;
}
