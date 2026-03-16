#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "image_save.h"
#include "menu.h"
#include "user_io.h"
#include "lib/imlib2/Imlib2.h"

#ifdef PROFILING
    #include "profiling.h"
#endif

static struct { const char *fmtstr; Imlib_Load_Error errno; } err_strings[] = {
  {"file '%s' does not exist", IMLIB_LOAD_ERROR_FILE_DOES_NOT_EXIST},
  {"file '%s' is a directory", IMLIB_LOAD_ERROR_FILE_IS_DIRECTORY},
  {"permission denied to read file '%s'", IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_READ},
  {"no loader for the file format used in file '%s'", IMLIB_LOAD_ERROR_NO_LOADER_FOR_FILE_FORMAT},
  {"path for file '%s' is too long", IMLIB_LOAD_ERROR_PATH_TOO_LONG},
  {"a component of path '%s' does not exist", IMLIB_LOAD_ERROR_PATH_COMPONENT_NON_EXISTANT},
  {"a component of path '%s' is not a directory", IMLIB_LOAD_ERROR_PATH_COMPONENT_NOT_DIRECTORY},
  {"path '%s' has too many symbolic links", IMLIB_LOAD_ERROR_TOO_MANY_SYMBOLIC_LINKS},
  {"ran out of file descriptors trying to access file '%s'", IMLIB_LOAD_ERROR_OUT_OF_FILE_DESCRIPTORS},
  {"denied write permission for file '%s'", IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_WRITE},
  {"out of disk space writing to file '%s'", IMLIB_LOAD_ERROR_OUT_OF_DISK_SPACE},
  {(const char *)NULL, (Imlib_Load_Error) 0}
};

static void print_imlib_load_error (Imlib_Load_Error err, const char *filepath) {
  int i;
  for (i = 0; err_strings[i].fmtstr != NULL; i++) {
    if (err == err_strings[i].errno) {
	printf("Screenshot Error (%d): ",err);
	printf(err_strings[i].fmtstr,filepath);
	printf("\n");
      return ;
    }
  }
  /* Unrecognised error */
    printf("Screenshot Error (%d): unrecognized error accessing file '%s'\n",err,filepath);
  return ;
}

// upscale 24bpp image using nearest neighbor.
int upscale_nearest_24(const uint8_t *src, int src_w, int src_h,int src_stride,   /* bytes per source row */
    uint8_t *dst, int dst_w, int dst_h, int dst_stride    /* bytes per destination row */)
{
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return -1;
    }

    int *xmap = (int *)malloc((size_t)dst_w * sizeof(int));
    if (!xmap) {
        return -1;
    }

    // precompute
    for (int x = 0; x < dst_w; x++) {
        xmap[x] = (x * src_w) / dst_w;
    }

    for (int y = 0; y < dst_h; y++) {
        int sy = (y * src_h) / dst_h;

        const uint8_t *src_row = src + (size_t)sy * (size_t)src_stride;
        uint8_t *dst_row = dst + (size_t)y * (size_t)dst_stride;

        for (int x = 0; x < dst_w; x++) {
            const uint8_t *sp = src_row + (size_t)xmap[x] * 3u;
            uint8_t *dp = dst_row + (size_t)x * 3u;

            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
        }
    }
    free(xmap);
    return 0;
}

// upscale 32bpp image using nearest neighbor. assumes last channel is alpha.
int upscale_nearest_32(const uint8_t *src, int src_w, int src_h, int src_stride,   /* bytes per source row */
    uint8_t *dst, int dst_w, int dst_h, int dst_stride    /* bytes per destination row */)
{
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return -1;
    }

    int *xmap = (int *)malloc((size_t)dst_w * sizeof(int));
    if (!xmap) {
        return -1;
    }

    // precompute
    for (int x = 0; x < dst_w; x++) {
        xmap[x] = (x * src_w) / dst_w;
    }

    for (int y = 0; y < dst_h; y++) {
        int sy = (y * src_h) / dst_h;

        const uint8_t *src_row = src + (size_t)sy * (size_t)src_stride;
        uint8_t *dst_row = dst + (size_t)y * (size_t)dst_stride;

        for (int x = 0; x < dst_w; x++) {
            const uint8_t *sp = src_row + (size_t)xmap[x] * 4u;
            uint8_t *dp = dst_row + (size_t)x * 4u;

            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = sp[3];
        }
    }

    free(xmap);
    return 0;
}

// use imlib2 to write a PNG file - expects BGRA
int write_png_32(const char *filename, const uint8_t *bgra, int width, int height, int output_width, int output_height)
{
    if (output_width > 0 && output_height > 0) {
        int row_bytes = (size_t)output_width * 4;
        uint8_t *scaled = (uint8_t *)malloc((size_t)output_height * (size_t)row_bytes);
        if (!scaled) {
            return -1;
        }

        upscale_nearest_32(bgra, width, height, width * 4, scaled, output_width, output_height, row_bytes);
        int result = write_png_32(filename, scaled, output_width, output_height, 0, 0);
        return result;
    }

    Imlib_Image im = imlib_create_image_using_data(width, height, (unsigned int *)bgra);
    imlib_context_set_image(im);
    
    Imlib_Load_Error error;
    imlib_save_image_with_error_return(getFullPath(filename),&error);
    if (error != IMLIB_LOAD_ERROR_NONE)
    {
        print_imlib_load_error (error, filename);
        Info("error in saving png");
        return false;
    }
    imlib_free_image_and_decache();
    return 0;
}

// write 24bpp uncompressed BMP file - expects BGR
int write_bmp_24(const char *filename, const uint8_t *bgr, int width, int height, int output_width, int output_height)
{
    if (output_width > 0 && output_height > 0) {
        int row_bytes = (size_t)output_width * 3;
        uint8_t *scaled = (uint8_t *)malloc((size_t)output_height * (size_t)row_bytes);
        if (!scaled) {
            return -1;
        }

        upscale_nearest_24(bgr, width, height, width * 3, scaled, output_width, output_height, row_bytes);
        int result = write_bmp_24(filename, scaled, output_width, output_height);
        free(scaled);
        return result;
    }

    FILE *f = fopen(filename, "wb");
    if (!f) return -1;
	
    uint32_t row_raw = width * 3;
    uint32_t row_padded = (row_raw + 3) & ~3;
    uint32_t image_size = row_padded * height;
    uint32_t file_size = 54 + image_size;

    uint8_t header[54] = {0};

    // bitmap header (file)
    header[0] = 'B';
    header[1] = 'M';

    header[2] = file_size;
    header[3] = file_size >> 8;
    header[4] = file_size >> 16;
    header[5] = file_size >> 24;

    header[10] = 54;  // pixel data offset

    // dib header
    header[14] = 40;  // header size

    header[18] = width;
    header[19] = width >> 8;
    header[20] = width >> 16;
    header[21] = width >> 24;

    header[22] = height;
    header[23] = height >> 8;
    header[24] = height >> 16;
    header[25] = height >> 24;

    header[26] = 1;      // planes
    header[28] = 24;     // bits per pixel

    header[34] = image_size;
    header[35] = image_size >> 8;
    header[36] = image_size >> 16;
    header[37] = image_size >> 24;

    fwrite(header, 1, 54, f);

    uint8_t padding[3] = {0};
    uint32_t pad = row_padded - row_raw;

	// BMP stores rows bottom-up
	for (int y = height - 1; y >= 0; y--) {
		const uint8_t *row = bgr + y * width * 3;
		fwrite(row, 1, row_raw, f);
		fwrite(padding, 1, pad, f);
	}

    fclose(f);
    return 0;
}

// write raw RGB data to file - expects RGB
int write_raw_rgb(const char *filename, const uint8_t *rgb, int width, int height, int output_width, int output_height)
{
    if (output_width > 0 && output_height > 0) { 
        int row_bytes = (size_t)output_width * 3;
        uint8_t *scaled = (uint8_t *)malloc((size_t)output_height * (size_t)row_bytes);
        if (!scaled) {
            return -1;
        }
        upscale_nearest_24(rgb, width, height, width * 3, scaled, output_width, output_height, row_bytes);
        int result = write_raw_rgb(filename, scaled, output_width, output_height, 0, 0);
        free(scaled);
        return result;
    }

    if (!filename || !rgb || width <= 0 || height <= 0) {
        return -1;
    }

    FILE *f = fopen(filename, "wb");
    if (!f) {
        return -1;
    }

    size_t bytes = (size_t)width * (size_t)height * 3;
    size_t written = fwrite(rgb, 1, bytes, f);

    fclose(f);

    return (written == bytes) ? 0 : -1;
}