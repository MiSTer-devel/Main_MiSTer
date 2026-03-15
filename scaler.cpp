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

#if defined(__ARM_NEON)
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

// this doesn't get called anywhere right now, maybe rewrite in simd later
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

// use NEON if available
#if defined(__ARM_NEON)

// simd versions of copying a bunch of pixels from the scaler's shared memory into a local buffer

// mister_scaler_read never gets called as is but was practice for
// the 32-bit (BGRA) version.
// vld3q_u8 loads 16 bytes into each of 3 registers
// vst3q_u8 stores that same data back into memory
// uint8x16x3_t is a struct that holds 3 uint8x16_t vectors
// which are themselves the data type a 128-bit register can hold
// 16 uint8_t values which is a perfect match for 24-bit RGB
int mister_scaler_read(mister_scaler *ms, unsigned char *gbuf)
{
    #ifdef PROFILING
        PROFILE_FUNCTION();
    #endif

    unsigned char *buffer = (unsigned char *)(ms->map + ms->map_off);

    for (int y = 0; y < ms->height; y++) {
        unsigned char *pixbuf = &buffer[ms->header + y * ms->line];
        unsigned char *outbuf = &gbuf[y * (ms->width * 3)];

        // we process a multiple of VEC_WIDTH to work with SIMD, then do the rest scalar
        int limit = ms->width - (ms->width % VEC_WIDTH);

        for (int x = 0; x < limit; x += VEC_WIDTH) {
            uint8x16x3_t rgb = vld3q_u8(pixbuf + x * 3);
            vst3q_u8(outbuf + x * 3, rgb);
        }

        // scalar tail
        for (int x = limit; x < ms->width; x++) {
            outbuf[x * 3 + 0] = pixbuf[x * 3 + 0];
            outbuf[x * 3 + 1] = pixbuf[x * 3 + 1];
            outbuf[x * 3 + 2] = pixbuf[x * 3 + 2];
        }
    }

    return 0;
}

// similar to above, but vdupq_n_u8 is a "splat"
// it loads a single byte into an entire register
int mister_scaler_read_32(mister_scaler *ms, unsigned char *gbuf)
{
    #ifdef PROFILING
        PROFILE_FUNCTION();
    #endif

    unsigned char *buffer = (unsigned char *)(ms->map + ms->map_off);
    const uint8x16_t alpha = vdupq_n_u8(0xFF);

    for (int y = 0; y < ms->height; y++) {
        unsigned char *pixbuf = &buffer[ms->header + y * ms->line];
        unsigned char *outbuf = &gbuf[y * (ms->width * 4)];

        // we process a multiple of VEC_WIDTH to work with SIMD, then do the rest scalar
        int limit = ms->width - (ms->width % VEC_WIDTH);
        
        // simd loop
        for (int x = 0; x < limit; x += VEC_WIDTH) {
            uint8x16x3_t rgb = vld3q_u8(pixbuf + x * 3);

            // convert from rgb to bgra
            // i hate bgra
            uint8x16x4_t bgra;
            bgra.val[0] = rgb.val[2];   
            bgra.val[1] = rgb.val[1];   
            bgra.val[2] = rgb.val[0];   
            bgra.val[3] = alpha;        

            vst4q_u8(outbuf + x * 4, bgra);
        }

        // scalar tail
        for (int x = limit; x < ms->width; x++) {
            outbuf[x * 4 + 2] = pixbuf[x * 3 + 0];
            outbuf[x * 4 + 1] = pixbuf[x * 3 + 1];
            outbuf[x * 4 + 0] = pixbuf[x * 3 + 2];
            outbuf[x * 4 + 3] = 0xFF;
        }
    }

    return 0;
}

#else

// no NEON, original scalar versions
int mister_scaler_read(mister_scaler *ms,unsigned char *gbuf)
{
    #ifdef PROFILING
        PROFILE_FUNCTION();
    #endif
    unsigned char *buffer;
    buffer = (unsigned char *)(ms->map+ms->map_off);

    // do this slow way for now..  - could use a memcpy?
    unsigned char *pixbuf;
    unsigned char *outbuf;
    for (int  y=0; y< ms->height ; y++) {
          pixbuf=&buffer[ms->header + y*ms->line];
          outbuf=&gbuf[y*(ms->width*3)];
          for (int x = 0; x < ms->width ; x++) {
            *outbuf++ = *pixbuf++;
            *outbuf++ = *pixbuf++;
            *outbuf++ = *pixbuf++;
          }
    }

    return 0;
}

int mister_scaler_read_32(mister_scaler *ms, unsigned char *gbuf) {
    #ifdef PROFILING
        PROFILE_FUNCTION();
    #endif
    unsigned char *buffer;
    buffer = (unsigned char *)(ms->map+ms->map_off);

    // do this slow way for now..  - could use a memcpy?
    unsigned char *pixbuf;
    unsigned char *outbuf;
    for (int  y=0; y< ms->height ; y++) {
          pixbuf=&buffer[ms->header + y*ms->line];
          outbuf=&gbuf[y*(ms->width*4)];
          for (int x = 0; x < ms->width ; x++) {
            outbuf[2] = *pixbuf++;
            outbuf[1] = *pixbuf++;
            outbuf[0] = *pixbuf++;
            outbuf[3] = 0xFF;
	    outbuf+=4;
          }
    }

    return 0;
}

#endif