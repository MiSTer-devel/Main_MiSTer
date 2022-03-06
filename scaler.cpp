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

#include "scaler.h"
#include "shmem.h"


mister_scaler * mister_scaler_init()
{
    mister_scaler *ms =(mister_scaler *) calloc(sizeof(mister_scaler),1);
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

int mister_scaler_read(mister_scaler *ms,unsigned char *gbuf)
{
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
