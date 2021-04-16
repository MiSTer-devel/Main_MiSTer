/*
Copyright 2019 alanswx
with help from the MiSTer contributors including Grabulosaure
*/

#ifndef SCALER_H
#define SCALER_H

typedef struct {
   int header;
   int width;
   int height;
   int line;

   char *map;
   int num_bytes;
   int map_off;
} mister_scaler;

#define MISTER_SCALER_BASEADDR     0x20000000
#define MISTER_SCALER_BUFFERSIZE   2048*3*1024

mister_scaler *mister_scaler_init();
int mister_scaler_read(mister_scaler *,unsigned char *buffer);
int mister_scaler_read_yuv(mister_scaler *ms,int,unsigned char *y,int, unsigned char *U,int, unsigned char *V);
void mister_scaler_free(mister_scaler *);

#endif
