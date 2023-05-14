#pragma once

#include <stdint.h>

void oled_init(void);
void oled_write_image(const uint8_t *image, unsigned int slot);
void oled_show_image(unsigned int slot);
void oled_set_brightness(unsigned int brightness);
unsigned int oled_get_brightness(void);
