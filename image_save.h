#ifndef IMAGE_SAVE_H
#define IMAGE_SAVE_H

#include <stdint.h>

bool write_png_32(const char *filename, const uint8_t *rgba, int width, int height, int output_width = 0, int output_height = 0);
bool write_bmp_24(const char *filename, const uint8_t *bgr, int width, int height, int output_width = 0, int output_height = 0);
bool write_raw_rgb(const char *filename, const uint8_t *rgb, int width, int height, int output_width = 0, int output_height = 0);

#endif // IMAGE_SAVE_H