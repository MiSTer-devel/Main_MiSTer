/*
Copyright 2019 alanswx
with help from the MiSTer contributors including Grabulosaure 
*/


typedef struct {
   int header;
   int width;
   int height;
   int line;

   char *map;
   int num_bytes;
   int map_off;
   int fd;
} mister_scalar;


#define MISTER_SCALAR_BASEADDR     536870912 
#define MISTER_SCALAR_BUFFERSIZE   2048*3*1024


mister_scalar *mister_scalar_init();
int mister_scalar_read(mister_scalar *,unsigned char *buffer);
int mister_scalar_read_yuv(mister_scalar *ms,int,unsigned char *y,int, unsigned char *U,int, unsigned char *V);
void mister_scalar_free(mister_scalar *);
