#include "fonts.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_debug_helpers.h>

#include "embedded_files.h"
#include "util.h"

#define DPI			85
#define DOTSIZE			64
#define PIXELS_TO_DOTS(x)	((x) * DOTSIZE)
#define DOTS_TO_PIXELS(x)	DIV_ROUND((x), DOTSIZE)

static const char *TAG = "fonts";

static FT_Library ftlib = NULL;
static font_t ftface_default = NULL;

void fonts_init(void) {
	FT_Error fterr;

	fterr = FT_Init_FreeType(&ftlib);
	if (fterr) {
		ESP_LOGE(TAG, "Failed to initialize libfreetype: %s (%d)", FT_Error_String(fterr), fterr);
	} else {
		fterr = FT_New_Memory_Face(ftlib, EMBEDDED_FILE_PTRS_SIZE(droidsans_bold_ttf), 0, &ftface_default);
		if (fterr) {
			ESP_LOGE(TAG, "Failed to load font from memory: %s (%d)", FT_Error_String(fterr), fterr);
		} else {
			ESP_LOGI(TAG, "Font %s loaded", STR_NULL(FT_Get_Postscript_Name(ftface_default)));
		}
	}
}

font_t fonts_get_default_font(void) {
	return ftface_default;
}

int fonts_calculate_text_params(font_t font, unsigned int char_height_px, const char *str, font_text_params_t *params) {
	FT_Error fterr;
	FT_Vector pos = { .x = 0, .y = 0 };
	int width = 0;
	int height = 0;
	int max_top = INT_MIN;
	int min_top = INT_MAX;

	str = STR_NULL(str);
	if (!*str) {
		params->max_top = 0;
		params->effective_size.x = 0;
		params->effective_size.y = 0;
		return 0;
	}

	fterr = FT_Set_Char_Size(font, 0, PIXELS_TO_DOTS(char_height_px), DPI, DPI);
	if (fterr) {
		ESP_LOGE(TAG, "Failed to set font char size: %s (%d)", FT_Error_String(fterr), fterr);
		return fterr;
	}

	while (*str) {
		char c = *str++;
		int glyph_pos_x = DOTS_TO_PIXELS(pos.x);
		int glyph_pos_y = DOTS_TO_PIXELS(pos.y);
		FT_GlyphSlotRec *glyph;

		fterr = FT_Load_Char(font, c, FT_LOAD_RENDER);
		if (fterr) {
			ESP_LOGE(TAG, "Failed to load char '%c': %s (%d)", c, FT_Error_String(fterr), fterr);
			return fterr;
		}
		glyph = font->glyph;

		width = MAX(width, glyph_pos_x + glyph->bitmap.width + glyph->bitmap_left);
		height = MAX(height, glyph_pos_y + glyph->bitmap.rows + glyph->bitmap_top);

		min_top = MIN(min_top, glyph->bitmap_top);
		max_top = MAX(max_top, glyph->bitmap_top);

		pos.x += font->glyph->advance.x;
		pos.y += font->glyph->advance.y;
	}

	params->max_top = max_top;
	params->effective_size.x = width;
	params->effective_size.y = height - min_top;
	return 0;
}

int fonts_render_string(font_t font, const char *str, const font_text_params_t *params, const font_vec_t *source_offset, const font_fb_t *fb) {
	FT_Error fterr;
	FT_Vector pos = { .x = 0, .y = 0 };
	int max_top = params->max_top;

	str = STR_NULL(str);

	while (*str) {
		char c = *str++;
		int dst_x = DOTS_TO_PIXELS(pos.x) - source_offset->x;
		int dst_y = DOTS_TO_PIXELS(pos.y) - source_offset->y;
		unsigned int offset_x = 0;
		FT_GlyphSlotRec *glyph;

		fterr = FT_Load_Char(font, c, FT_LOAD_RENDER);
		if (fterr) {
			ESP_LOGE(TAG, "Failed to load char '%c': %s (%d)", c, FT_Error_String(fterr), fterr);
			return fterr;
		}
		glyph = font->glyph;

		dst_x += glyph->bitmap_left;
		dst_y += max_top - glyph->bitmap_top;
		if (dst_x < 0) {
			offset_x += -dst_x;
			dst_x = 0;
		}
		if (dst_x < fb->size.x && dst_y < fb->size.y && offset_x < font->glyph->bitmap.width) {
			unsigned int draw_width = MIN(font->glyph->bitmap.width, fb->size.x - dst_x);
			unsigned int draw_height = MIN(font->glyph->bitmap.rows, fb->size.y - dst_y);
			unsigned int y = 0;

			if (dst_y < 0) {
				y = -dst_y;
			}
			for (; y < draw_height; y++) {
				memcpy(&fb->pixels[(dst_y + y) * fb->stride + dst_x], &glyph->bitmap.buffer[glyph->bitmap.width * y + offset_x], draw_width - offset_x);
			}
		}

		pos.x += glyph->advance.x;
		pos.y += glyph->advance.y;
	}

	return 0;
}
