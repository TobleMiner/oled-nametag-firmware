#include "menu.h"
#include "gui.h"

typedef struct gui_fft {
	gui_element_t element;

	float fft_data[256];
} gui_fft_t;


void fft_init(gui_t *gui_root);
int fft_run(menu_cb_f exit_cb, void *cb_ctx, void *priv);
