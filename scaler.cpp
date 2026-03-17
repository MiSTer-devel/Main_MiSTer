/*
Copyright 2019 alanswx
with help from the MiSTer contributors including Grabulosaure
*/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sched.h>
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <err.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
const int VEC_WIDTH = 16;
#endif

#include "scaler.h"
#include "shmem.h"

#ifdef PROFILING
#include "profiling.h"
#endif

mister_scaler * mister_scaler_init()
{
    mister_scaler *ms = (mister_scaler *)calloc(1, sizeof(mister_scaler));
    int	 pagesize = sysconf(_SC_PAGE_SIZE);
    if (pagesize==0) pagesize=4096;
    int offset = MISTER_SCALER_BASEADDR;
    int	map_start = offset & ~(pagesize - 1);
    ms->map_off = offset - map_start;
    ms->num_bytes=MISTER_SCALER_BUFFERSIZE;
    //printf("map_start = %d map_off=%d offset=%d\n",map_start,ms->map_off,offset);

    unsigned char *buffer;
    ms->map=(char *)shmem_map(map_start, ms->num_bytes+ms->map_off);
    if (!ms->map)
    {
        mister_scaler_free(ms);
        return NULL;
    }
    buffer = (unsigned char *)(ms->map+ms->map_off);
    printf (" 1: %02X %02X %02X %02X   %02X %02X %02X %02X   %02X %02X %02X %02X   %02X %02X %02X %02X\n",
            buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],
            buffer[8],buffer[9],buffer[10],buffer[11],buffer[12],buffer[13],buffer[14],buffer[15]);
    if (buffer[0]!=1 || buffer[1]!=1) {
        printf("problem\n");
        mister_scaler_free(ms);
        return NULL;
    }

    ms->header=buffer[2]<<8 | buffer[3];
    ms->width =buffer[6]<<8 | buffer[7];
    ms->height=buffer[8]<<8 | buffer[9];
    ms->line  =buffer[10]<<8 | buffer[11];
    ms->output_width =buffer[12]<<8 | buffer[13];
    ms->output_height=buffer[14]<<8 | buffer[15];

    printf ("Image: Width=%i Height=%i  Line=%i  Header=%i output_width=%i output_height=%i \n",ms->width,ms->height,ms->line,ms->header,ms->output_width,ms->output_height);
   /*
    printf (" 1: %02X %02X %02X %02X   %02X %02X %02X %02X   %02X %02X %02X %02X   %02X %02X %02X %02X\n",
            buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],
            buffer[8],buffer[9],buffer[10],buffer[11],buffer[12],buffer[13],buffer[14],buffer[15]);
    */

   return ms;

}

void mister_scaler_free(mister_scaler *ms)
{
   shmem_unmap(ms->map,ms->num_bytes+ms->map_off);
   free(ms);
}

// not currently used - leaving in for reference in case we want to add YUV later
/*
int mister_scaler_read_yuv(mister_scaler *ms,int lineY,unsigned char *bufY, int lineU, unsigned char *bufU, int lineV, unsigned char *bufV)
{
    unsigned char *buffer;
    buffer = (unsigned char *)(ms->map+ms->map_off);

    // do this slow way for now..
    unsigned char *pixbuf;
    unsigned char *outbufy;
    unsigned char *outbufU;
    unsigned char *outbufV;
    for (int  y=0; y< ms->height ; y++)
	{
        pixbuf=&buffer[ms->header + y*ms->line];
        outbufy=&bufY[y*(lineY)];
        outbufU=&bufU[y*(lineU)];
        outbufV=&bufV[y*(lineV)];
        for (int x = 0; x < ms->width ; x++)
		{
			int R,G,B;
			R = *pixbuf++;
			G = *pixbuf++;
			B = *pixbuf++;
			int Y =  (0.257 * R) + (0.504 * G) + (0.098 * B) + 16;
			int U = -(0.148 * R) - (0.291 * G) + (0.439 * B) + 128;
			int V =  (0.439 * R) - (0.368 * G) - (0.071 * B) + 128;

			*outbufy++ = Y;
			*outbufU++ = U;
			*outbufV++ = V;
        }
    }

    return 0;
}
*/

// use NEON if available
#if defined(__ARM_NEON)

// simd optimized copy from scaler to buffer.
// simd seems like overkill but it reduced the time it took to copy data from the scaler into
// a buffer by an order of magnitude (~50-80ms down to ~3-5ms) which is huge when we perform
// the copy on the main thread.

// if you've not done simd stuff before this is a very straightforward use of it
// so this might be a nice example.

int mister_scaler_read(mister_scaler *ms, unsigned char *gbuf, mister_scaler_format_t format = RGB)
{
    #ifdef PROFILING
        PROFILE_FUNCTION();
    #endif

    unsigned char *buffer = (unsigned char *)(ms->map + ms->map_off);
    
    // this is a "splat" - prefilling a vector register with a single value
    const uint8x16_t alpha = vdupq_n_u8(0xFF);

    for (int y = 0; y < ms->height; y++) {
        unsigned char *pixbuf = &buffer[ms->header + y * ms->line];
        unsigned char *outbuf;

        if (format == RGBA || format == BGRA)
            outbuf = &gbuf[y * (ms->width * 4)];
        else
            outbuf = &gbuf[y * (ms->width * 3)];
        
        // VEC_WIDTH is the number of elements a vector register can hold.
        // 24/32-bit image data is stored as pixels of 3 or 4 bytes (8 bits).
        // our ARMv7 NEON registers are 128 bits / 8 bits = 16 bytes per register.
        // any data left after doing 16-byte chunks falls back to our scalar code to be completed.
        int limit = ms->width - (ms->width % VEC_WIDTH);
        for (int x = 0; x < limit; x += VEC_WIDTH) {
            
            // load 16 pixels (48 bytes) from the scaler buffer into our vector registers.
            // uint8x16x3_t is a struct of 3 uint8x16_t vectors, so it can hold 16 pixels of RGB data.
            // behind the scenes we'll have three vector registers, each containing 16 bytes
            // representing red, green, or blue values for that pixel.
            uint8x16x3_t rgb = vld3q_u8(pixbuf + x * 3);

            switch (format)
            {
                case RGB:
                    vst3q_u8(outbuf + x * 3, rgb);
                    break;
                // some image formats don't use RGB ordering, so we need to shuffle our data
                // around. this is easy since uint8x16x3_t is a struct
                case BGR:
                    uint8x16x3_t bgr;
                    bgr.val[0] = rgb.val[2];   
                    bgr.val[1] = rgb.val[1];   
                    bgr.val[2] = rgb.val[0];   
                    vst3q_u8(outbuf + x * 3, bgr);
                    break;
                case RGBA:
                    uint8x16x4_t rgba;
                    rgba.val[0] = rgb.val[0];   
                    rgba.val[1] = rgb.val[1];   
                    rgba.val[2] = rgb.val[2];   
                    rgba.val[3] = alpha;        
                    vst4q_u8(outbuf + x * 4, rgba);
                    break;
                case BGRA:
                    uint8x16x4_t bgra;
                    bgra.val[0] = rgb.val[2];   
                    bgra.val[1] = rgb.val[1];   
                    bgra.val[2] = rgb.val[0];   
                    bgra.val[3] = alpha;        
                    vst4q_u8(outbuf + x * 4, bgra);
                    break;
                case YUV:
                    // not supported yet
                default:
                    break;
            }
        }

        // scalar tail; this processes any remaining data that didn't fit into our vector
        // registers. this is (basically) the same code we'd use if we weren't using simd at all.
        switch (format)
        {
            case RGB:
                for (int x = limit; x < ms->width; x++) {
                    outbuf[x * 3 + 0] = pixbuf[x * 3 + 0];
                    outbuf[x * 3 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 3 + 2] = pixbuf[x * 3 + 2];
                }
                break;
            case BGR:
                for (int x = limit; x < ms->width; x++) {
                    outbuf[x * 3 + 2] = pixbuf[x * 3 + 0];
                    outbuf[x * 3 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 3 + 0] = pixbuf[x * 3 + 2];
                }
                break;
            case RGBA:
                for (int x = limit; x < ms->width; x++) {
                    outbuf[x * 4 + 0] = pixbuf[x * 3 + 0];
                    outbuf[x * 4 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 4 + 2] = pixbuf[x * 3 + 2];
                    outbuf[x * 4 + 3] = 0xFF;
                }
                break;
            case BGRA:
                for (int x = limit; x < ms->width; x++) {
                    outbuf[x * 4 + 2] = pixbuf[x * 3 + 0];
                    outbuf[x * 4 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 4 + 0] = pixbuf[x * 3 + 2];
                    outbuf[x * 4 + 3] = 0xFF;
                }
                break;
            case YUV:
                // not supported yet
            default:
                break;
        }
    }
    return 0;
}

#else

// no NEON available, do all scalar
int mister_scaler_read(mister_scaler *ms, unsigned char *gbuf, mister_scaler_format_t format = RGB)
{
    #ifdef PROFILING
        PROFILE_FUNCTION();
    #endif

    unsigned char *buffer = (unsigned char *)(ms->map + ms->map_off);

    for (int y = 0; y < ms->height; y++) {
        unsigned char *pixbuf = &buffer[ms->header + y * ms->line];
        unsigned char *outbuf;

        if (format == RGBA || format == BGRA)
            outbuf = &gbuf[y * (ms->width * 4)];
        else
            outbuf = &gbuf[y * (ms->width * 3)];

        // scalar version
        switch (format)
        {
            case RGB:
                for (int x = 0; x < ms->width; x++) {
                    outbuf[x * 3 + 0] = pixbuf[x * 3 + 0];
                    outbuf[x * 3 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 3 + 2] = pixbuf[x * 3 + 2];
                }
                break;
            case BGR:
                for (int x = 0; x < ms->width; x++) {
                    outbuf[x * 3 + 2] = pixbuf[x * 3 + 0];
                    outbuf[x * 3 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 3 + 0] = pixbuf[x * 3 + 2];
                }
                break;
            case RGBA:
                for (int x = 0; x < ms->width; x++) {
                    outbuf[x * 4 + 0] = pixbuf[x * 3 + 0];
                    outbuf[x * 4 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 4 + 2] = pixbuf[x * 3 + 2];
                    outbuf[x * 4 + 3] = 0xFF;
                }
                break;
            case BGRA:
                for (int x = 0; x < ms->width; x++) {
                    outbuf[x * 4 + 2] = pixbuf[x * 3 + 0];
                    outbuf[x * 4 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 4 + 0] = pixbuf[x * 3 + 2];
                    outbuf[x * 4 + 3] = 0xFF;
                }
                break;
            case YUV:
                // not supported yet
            default:
                break;
        }
    }
    return 0;
}

#endif