#pragma once

#include <stddef.h>
#include <stdint.h>

#include <ft2build.h>
#include FT_FREETYPE_H

typedef FT_Face font_t;

typedef struct font_vec {
	int x;
	int y;
} font_vec_t;

typedef struct font_fb {
	uint8_t *pixels;
	unsigned int stride;
	font_vec_t size;
} font_fb_t;

typedef struct font_text_params {
	int max_top;
	font_vec_t effective_size;
} font_text_params_t;

void fonts_init(void);
font_t fonts_get_default_font(void);

int fonts_calculate_text_params(font_t font, unsigned int char_height_px, const char *str, font_text_params_t *params);

int fonts_render_string(font_t font, const char *str, const font_text_params_t *params, const font_vec_t *source_offset, const font_fb_t *fb);
