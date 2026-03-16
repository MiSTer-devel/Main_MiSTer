#ifndef BITMAP_H
#define BITMAP_H

#ifdef PROFILING
    #include "profiling.h"
#endif

#include <stdint.h>

int write_bmp_24(const char *filename, const uint8_t *bgr, int width, int height, int output_width = 0, int output_height = 0);
int write_raw_rgb(const char *filename, const uint8_t *rgb, int width, int height, int output_width = 0, int output_height = 0);

#endif // BITMAP_H