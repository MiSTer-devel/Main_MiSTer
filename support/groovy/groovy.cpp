
/* UDP server */
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <netinet/ether.h>
#include <linux/if_packet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/net_tstamp.h>
#include <stdio.h>
#include <memory.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <signal.h>

#include <sys/time.h> 
#include <sys/stat.h>
#include <time.h>

#include <unistd.h>
#include <sched.h>
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#include "../../hardware.h"
#include "../../shmem.h"
#include "../../fpga_io.h"
#include "../../spi.h"

#include "logo.h"
#include "pll.h"

// FPGA SPI commands 
#define UIO_GET_GROOVY_STATUS     0xf0 
#define UIO_GET_GROOVY_HPS        0xf1 
#define UIO_SET_GROOVY_INIT       0xf2
#define UIO_SET_GROOVY_SWITCHRES  0xf3
#define UIO_SET_GROOVY_BLIT       0xf4
#define UIO_SET_GROOVY_LOGO       0xf5
#define UIO_SET_GROOVY_AUDIO      0xf6
#define UIO_SET_GROOVY_BLIT_LZ4   0xf7


// FPGA DDR shared
#define BASEADDR 0x1C000000
#define HEADER_LEN 0xff       
#define CHUNK 7 
#define HEADER_OFFSET HEADER_LEN - CHUNK                        
#define FRAMEBUFFER_SIZE  (720 * 576 * 4 * 2) // RGBA 720x576 with 2 fields
#define AUDIO_SIZE (8192 * 2 * 2)             // 8192 samples with 2 16bit-channels
#define LZ4_SIZE (720 * 576 * 4)              // Estimated LZ4 MAX   
#define FIELD_OFFSET 0x195000                 // 0x12fcff position for fpga (after first field)    
#define AUDIO_OFFSET 0x32a000                 // 0x25f8ff position for fpga (after framebuffer)    
#define LZ4_OFFSET_A 0x332000                 // 0x2678ff position for fpga (after audio)          
#define LZ4_OFFSET_B 0x4c7000                 // 0x3974ff position for fpga (after lz4_offset_A)   
#define BUFFERSIZE FRAMEBUFFER_SIZE + AUDIO_SIZE + LZ4_SIZE + LZ4_SIZE + HEADER_LEN


// UDP server 
#define UDP_PORT 32100
#define CMD_CLOSE 1
#define CMD_INIT 2
#define CMD_SWITCHRES 3
#define CMD_AUDIO 4
#define CMD_GET_STATUS 5
#define CMD_BLIT_VSYNC 6

#define LOGO_TIMER 16

//https://stackoverflow.com/questions/64318331/how-to-print-logs-on-both-console-and-file-in-c-language
#define LOG_TIMER 25
static struct timeval logTS, logTS_ant, blitStart, blitStop; 
static int doVerbose = 0; 
static int difUs = 0;
static unsigned long logTime = 0;
static FILE * fp = NULL;

#define LOG(sev,fmt, ...) do {	\
			        if (sev == 0) printf(fmt, __VA_ARGS__);	\
			        if (sev <= doVerbose) { \
			        	gettimeofday(&logTS, NULL); 	\
			        	difUs = (difUs != 0) ? ((logTS.tv_sec - logTS_ant.tv_sec) * 1000000 + logTS.tv_usec - logTS_ant.tv_usec) : -1;	\
					fprintf(fp, "[%06.3f]",(double) difUs/1000); \
					fprintf(fp, fmt, __VA_ARGS__);	\
                                	gettimeofday(&logTS_ant, NULL); \
                                } \
                           } while (0)


typedef union
{
  struct
  {
    unsigned char bit0 : 1;
    unsigned char bit1 : 1;
    unsigned char bit2 : 1;
    unsigned char bit3 : 1;
    unsigned char bit4 : 1;
    unsigned char bit5 : 1;
    unsigned char bit6 : 1;
    unsigned char bit7 : 1;   
  }u;
   uint8_t byte;
} bitByte;


typedef struct {   		
   //frame sync           
   uint32_t PoC_frame_recv; 	     
   uint32_t PoC_frame_ddr; 	         
      
   //modeline + pll -> burst 3
   uint16_t PoC_H; 	// 08
   uint8_t  PoC_HFP; 	// 10
   uint8_t  PoC_HS; 	// 11
   uint8_t  PoC_HBP; 	// 12
   uint16_t PoC_V; 	// 13
   uint8_t  PoC_VFP; 	// 15   
   uint8_t  PoC_VS;     // 16
   uint8_t  PoC_VBP;    // 17
   
   //pll   
   uint8_t  PoC_pll_M0;  // 18 High
   uint8_t  PoC_pll_M1;  // 19 Low
   uint8_t  PoC_pll_C0;  // 20 High
   uint8_t  PoC_pll_C1;  // 21 Low
   uint32_t PoC_pll_K;   // 22 
   uint8_t  PoC_ce_pix;  // 26
   
   uint8_t  PoC_interlaced;  
   uint8_t  PoC_FB_progressive;  
   
   double   PoC_pclock;   
   
   uint32_t PoC_buffer_offset; // FIELD/AUDIO/LZ4 position on DDR
        
   //framebuffer          
   uint32_t PoC_bytes_len;               
   uint32_t PoC_pixels_len;               
   uint32_t PoC_bytes_recv;                             
   uint32_t PoC_pixels_ddr;   
   uint32_t PoC_field_frame;  
   uint8_t  PoC_field;  
   
   double PoC_width_time; 
   uint16_t PoC_V_Total;        
   
   //audio   
   uint32_t PoC_bytes_audio_len;
   
   //lz4
   uint32_t PoC_bytes_lz4_len;
   uint32_t PoC_bytes_lz4_ddr;     
   uint8_t  PoC_field_lz4;  
               
} PoC_type;

union {
    double d;
    uint64_t i;
} u;

/* General Server variables */
static int groovyServer = 0;
static int sockfd;
static struct sockaddr_in servaddr;	
static struct sockaddr_in clientaddr;
static socklen_t clilen = sizeof(struct sockaddr); 
static char recvbuf[65536] = { 0 };

/* Logo */
static int groovyLogo = 0;
static int logoX = 0;
static int logoY = 0;
static int logoSignX = 0;
static int logoSignY = 0;
static unsigned long logoTime = 0;

static PoC_type *poc;
static uint8_t *map = 0;    
static uint8_t* buffer;

static int blitCompression = 0;
static uint8_t audioRate = 0;
static uint8_t audioChannels = 0;
static uint8_t rgbMode = 0;

static int isBlitting = 0;
static int isCorePriority = 0;

static uint8_t hpsBlit = 0; 
static uint16_t numBlit = 0;
static uint8_t doScreensaver = 0;
static uint8_t isConnected = 0; 

/* FPGA HPS EXT STATUS */
static uint16_t fpga_vga_vcount = 0;
static uint32_t fpga_vga_frame = 0;
static uint32_t fpga_vram_pixels = 0;
static uint32_t fpga_vram_queue = 0;
static uint8_t  fpga_vram_end_frame = 0;
static uint8_t  fpga_vram_ready = 0;
static uint8_t  fpga_vram_synced = 0;
static uint8_t  fpga_vga_frameskip = 0;
static uint8_t  fpga_vga_vblank = 0;
static uint8_t  fpga_vga_f1 = 0;
static uint8_t  fpga_audio = 0;
static uint8_t  fpga_init = 0;
static uint32_t fpga_lz4_uncompressed = 0;

/* DEBUG */
/*
static uint32_t fpga_lz4_writed = 0;
static uint8_t  fpga_lz4_state = 0;
static uint8_t  fpga_lz4_run = 0;
static uint8_t  fpga_lz4_resume = 0;
static uint8_t  fpga_lz4_test1 = 0;
static uint8_t  fpga_lz4_test2 = 0;
static uint8_t  fpga_lz4_stop= 0;
static uint8_t  fpga_lz4_AB = 0;
static uint8_t  fpga_lz4_cmd_fskip = 0;
static uint32_t fpga_lz4_compressed = 0;
static uint32_t fpga_lz4_gravats = 0;
static uint32_t fpga_lz4_llegits = 0;
static uint32_t fpga_lz4_subframe_bytes = 0;
static uint16_t fpga_lz4_subframe_blit = 0;
*/

static void initDDR()
{
	memset(&buffer[0],0x00,0xff);	  
}

static void initVerboseFile()
{
	fp = fopen ("/tmp/groovy.log", "wt"); 
	if (fp == NULL)
	{
		printf("error groovy.log\n");       			
	}  	
	struct stat stats;
    	if (fstat(fileno(fp), &stats) == -1) 
    	{
        	printf("error groovy.log stats\n");        		
    	}		   		    		
    	if (setvbuf(fp, NULL, _IOFBF, stats.st_blksize) != 0)    		    	
    	{
        	printf("setvbuf failed \n");         		
    	}    		
    	logTime = GetTimer(1000);						
	
}

static void groovy_FPGA_hps()
{
    uint16_t req = 0;
    EnableIO();	
    do
    {
    	req = fpga_spi_fast(UIO_GET_GROOVY_HPS);
    } while (req == 0);           
    uint16_t hps = spi_w(0); 						    
    DisableIO(); 	
    bitByte bits;  
    bits.byte = (uint8_t) hps;
  	    		    			
    if (bits.u.bit0 == 1 && bits.u.bit1 == 0) doVerbose = 1;
    else if (bits.u.bit0 == 0 && bits.u.bit1 == 1) doVerbose = 2;
    else if (bits.u.bit0 == 1 && bits.u.bit1 == 1) doVerbose = 3;
    else doVerbose = 0;
	
    initVerboseFile();				 	
    hpsBlit = bits.u.bit2;			
    doScreensaver = !bits.u.bit3;		
    printf("doVerbose=%d hpsBlit=%d doScreenSaver=%d\n", doVerbose, hpsBlit, doScreensaver);	
}

static void groovy_FPGA_status(uint8_t isACK)
{
    uint16_t req = 0;
    EnableIO();	
    do
    {
    	req = fpga_spi_fast(UIO_GET_GROOVY_STATUS);
    } while (req == 0);
    
    fpga_vga_frame   = spi_w(0) | spi_w(0) << 16;  	  			
    fpga_vga_vcount  = spi_w(0);  						  			
    uint16_t word16  = spi_w(0);  			 
    uint8_t word8_l  = (uint8_t)(word16 & 0x00FF);  
  			
    bitByte bits;  
    bits.byte = word8_l;
    fpga_vram_ready     = bits.u.bit0;
    fpga_vram_end_frame = bits.u.bit1;
    fpga_vram_synced    = bits.u.bit2;  
    fpga_vga_frameskip  = bits.u.bit3;   
    fpga_vga_vblank     = bits.u.bit4;  
    fpga_vga_f1         = bits.u.bit5; 
    fpga_audio          = bits.u.bit6;  
    fpga_init           = bits.u.bit7; 
			
    uint8_t word8_h = (uint8_t)((word16 & 0xFF00) >> 8);			  			    
    fpga_vram_queue = word8_h; // 8b
  			 										
    if (fpga_vga_vcount <= poc->PoC_interlaced) //end line
    {
	if (poc->PoC_interlaced)
	{
		if (!fpga_vga_vcount) //based on field
		{
			fpga_vga_vcount = poc->PoC_V_Total;
		}
		else
		{
			fpga_vga_vcount = poc->PoC_V_Total - 1;
		}
	}
	else
	{
		fpga_vga_vcount = poc->PoC_V_Total;
	}			
    }	
        
    if (!isACK)
    {
    	fpga_vram_queue |= spi_w(0) << 8; //24b
    	fpga_vram_pixels = spi_w(0) | spi_w(0) << 16;	
		
	if (blitCompression)
	{	
		fpga_lz4_uncompressed  = spi_w(0) | spi_w(0) << 16; 
	}	 
	
		/* DEBUG	
			fpga_lz4_state = spi_w(0);
			fpga_lz4_writed = spi_w(0) | spi_w(0) << 16;
			
			uint16_t wordlz4 = spi_w(0);
			
			bits.byte = (uint8_t) wordlz4;
			fpga_lz4_cmd_fskip = bits.u.bit6;
			fpga_lz4_AB = bits.u.bit5;
			fpga_lz4_stop = bits.u.bit4;
			fpga_lz4_test2 = bits.u.bit3;
			fpga_lz4_test1 = bits.u.bit2;
			fpga_lz4_resume = bits.u.bit1;
                	fpga_lz4_run = bits.u.bit0;
		
			fpga_lz4_compressed =  spi_w(0) | spi_w(0) << 16;
			fpga_lz4_gravats = spi_w(0) | spi_w(0) << 16;
			fpga_lz4_llegits = spi_w(0) | spi_w(0) << 16;
			fpga_lz4_subframe_bytes = spi_w(0) | spi_w(0) << 16;
			fpga_lz4_subframe_blit = spi_w(0);
	        */		   	
    }									    	
    DisableIO(); 	
}

static void groovy_FPGA_switchres()
{
    uint16_t req = 0;
    EnableIO();	
    do
    {
    	req = fpga_spi_fast(UIO_SET_GROOVY_SWITCHRES);
    } while (req == 0);
    spi_w(1); 	
    DisableIO();        
}

static void groovy_FPGA_blit()
{	
    uint16_t req = 0;
    EnableIO();	
    do
    {
    	req = fpga_spi_fast(UIO_SET_GROOVY_BLIT);
    } while (req == 0);    				
    spi_w(1);    	
    DisableIO();        
}

static void groovy_FPGA_blit_lz4(uint32_t compressed_bytes)
{	
    uint16_t req = 0;   
    EnableIO();	
    do
    {		
 	req = fpga_spi_fast(UIO_SET_GROOVY_BLIT_LZ4);							
    } while (req == 0);	
    uint16_t lz4_zone = (poc->PoC_field_lz4) ? 0 : 1;
    spi_w(lz4_zone);
    spi_w((uint16_t) compressed_bytes);
    spi_w((uint16_t) (compressed_bytes >> 16));
    DisableIO();        
}

static void groovy_FPGA_init(uint8_t cmd, uint8_t audio_rate, uint8_t audio_chan, uint8_t rgb_mode)
{
    uint16_t req = 0;
    EnableIO();	
    do
    {
    	req = fpga_spi_fast(UIO_SET_GROOVY_INIT);
    } while (req == 0);
    spi_w(cmd);	
    bitByte bits; 
    bits.byte = audio_rate;
    bits.u.bit2 = (audio_chan == 1) ? 1 : 0;
    bits.u.bit3 = (audio_chan == 2) ? 1 : 0;
    bits.u.bit4 = (rgb_mode == 1) ? 1 : 0;			
    spi_w((uint16_t) bits.byte);			
    DisableIO();
}

static void groovy_FPGA_logo(uint8_t cmd)
{
    uint16_t req = 0;
    EnableIO();	
    do
    {
    	req = fpga_spi_fast(UIO_SET_GROOVY_LOGO);
    } while (req == 0);
    spi_w(cmd);		
    DisableIO();        
}

static void groovy_FPGA_audio(uint16_t samples)
{	
    uint16_t req = 0;
    EnableIO();	
    do
    {
    	req = fpga_spi_fast(UIO_SET_GROOVY_AUDIO);
    } while (req == 0);
    spi_w(samples);		
    DisableIO();   
}

static void loadLogo(int logoStart)
{	
	if (!doScreensaver)
	{
		return;
	}
	
	if (logoStart)
	{
		do
		{
			groovy_FPGA_status(0);			
		}  while (fpga_init != 0);	
		
		buffer[0] = (1) & 0xff;  
	 	buffer[1] = (1 >> 8) & 0xff;	       	  
	     	buffer[2] = (1 >> 16) & 0xff;    
	  	buffer[3] = (61440) & 0xff;  
	   	buffer[4] = (61440 >> 8) & 0xff;	       	  
	 	buffer[5] = (61440 >> 16) & 0xff;      	 			         	      	                     
		buffer[6] = (1) & 0xff; 	
		buffer[7] = (1 >> 8) & 0xff;
		
		logoTime = GetTimer(LOGO_TIMER); 					
	}

	if (CheckTimer(logoTime))    	
	{		
		groovy_FPGA_status(0);
		if (fpga_vga_vcount == 241)
		{								 		
			memset(&buffer[HEADER_OFFSET], 0x00, 184320);   	         		   	         				
		       	int z=0;		       	
		       	int offset = (256 * logoY * 3) + (logoX * 3); 
		       	for (int i=0; i<64; i++)
		       	{       				       		
		       		memcpy(&buffer[HEADER_OFFSET+offset], (char*)&logoImage[z], 192);
		       		offset += 256 * 3;		       		
		       		z += 64 * 3;
		       	}              		       			       		                          	                       	 			         	      	                                         		       		       
		       	logoTime = GetTimer(LOGO_TIMER);   
		       	
		       	logoX = (logoSignX) ? logoX - 1 : logoX + 1;
		       	logoY = (logoSignY) ? logoY - 2 : logoY + 2;	       		       	
		       	
		       	if (logoX >= 192 && !logoSignX)
		       	{
		       		logoSignX = !logoSignX;		       		       			       		
		       	}    	                   	        
		       	
		       	if (logoY >= 176 && !logoSignY)
		       	{	       		
		       		logoSignY = !logoSignY;
		       	}    
		       	
		       	if (logoX <= 0 && logoSignX)
		       	{
		       		logoSignX = !logoSignX;		       		       			       		
		       	}    	                   	        
		       	
		       	if (logoY <= 0 && logoSignY)
		       	{	       		
		       		logoSignY = !logoSignY;
		       	}    		       			       			     
		}                   	        	       	
	}
}

static void groovy_FPGA_blit(uint32_t bytes, uint16_t numBlit)
{            		
    poc->PoC_pixels_ddr = (rgbMode) ? bytes >> 2 : bytes / 3;     
    	                       	 			         	      	                                         
    buffer[3] = (poc->PoC_pixels_ddr) & 0xff;  
    buffer[4] = (poc->PoC_pixels_ddr >> 8) & 0xff;	       	  
    buffer[5] = (poc->PoC_pixels_ddr >> 16) & 0xff;      	 			         	      	                 
    
    buffer[6] = (numBlit) & 0xff; 	
    buffer[7] = (numBlit >> 8) & 0xff;	       	  
    
    if (poc->PoC_frame_ddr != poc->PoC_frame_recv)
    {
    	poc->PoC_frame_ddr  = poc->PoC_frame_recv;
    	
    	buffer[0] = (poc->PoC_frame_ddr) & 0xff;  
    	buffer[1] = (poc->PoC_frame_ddr >> 8) & 0xff;	       	  
    	buffer[2] = (poc->PoC_frame_ddr >> 16) & 0xff;        	  
    	
    	groovy_FPGA_blit();         	    	
    }	                     		         	      	             
       
}

static void groovy_FPGA_blit_lz4(uint32_t bytes, uint16_t numBlit)
{          	    
    poc->PoC_bytes_lz4_ddr = bytes;	                       	 			         	      	                                         
    buffer[35] = (poc->PoC_bytes_lz4_ddr) & 0xff;  
    buffer[36] = (poc->PoC_bytes_lz4_ddr >> 8) & 0xff;	       	  
    buffer[37] = (poc->PoC_bytes_lz4_ddr >> 16) & 0xff;      	 			         	      	                 
    
    buffer[38] = (numBlit) & 0xff; 	
    buffer[39] = (numBlit >> 8) & 0xff;	       	  
    
    if (poc->PoC_frame_ddr != poc->PoC_frame_recv)
    {
    	poc->PoC_frame_ddr  = poc->PoC_frame_recv;
    	
    	buffer[32] = (poc->PoC_frame_ddr) & 0xff;  
    	buffer[33] = (poc->PoC_frame_ddr >> 8) & 0xff;	       	  
    	buffer[34] = (poc->PoC_frame_ddr >> 16) & 0xff;        	  
    	    	
    	groovy_FPGA_blit_lz4(poc->PoC_bytes_lz4_len);    	
    }	                     		         	      	             
       
}

static void setSwitchres()
{               	   	 	    
    //modeline			       
    uint64_t udp_pclock_bits; 
    uint16_t udp_hactive;
    uint16_t udp_hbegin;
    uint16_t udp_hend;
    uint16_t udp_htotal;
    uint16_t udp_vactive;
    uint16_t udp_vbegin;
    uint16_t udp_vend;
    uint16_t udp_vtotal;
    uint8_t  udp_interlace;
     
    memcpy(&udp_pclock_bits,&recvbuf[1],8);			     
    memcpy(&udp_hactive,&recvbuf[9],2);			     
    memcpy(&udp_hbegin,&recvbuf[11],2);			     
    memcpy(&udp_hend,&recvbuf[13],2);			     
    memcpy(&udp_htotal,&recvbuf[15],2);			     
    memcpy(&udp_vactive,&recvbuf[17],2);			     
    memcpy(&udp_vbegin,&recvbuf[19],2);			     
    memcpy(&udp_vend,&recvbuf[21],2);			     
    memcpy(&udp_vtotal,&recvbuf[23],2);			     
    memcpy(&udp_interlace,&recvbuf[25],1);			     
           
    u.i = udp_pclock_bits;
    double udp_pclock = u.d;     
                                          
    poc->PoC_width_time = (double) udp_htotal * (1 / (udp_pclock * 1000)); //in ms, time to raster 1 line
    poc->PoC_V_Total = udp_vtotal;
                                                                                            			      			      			       
    LOG(1,"[Modeline] %f %d %d %d %d %d %d %d %d %s(%d)\n",udp_pclock,udp_hactive,udp_hbegin,udp_hend,udp_htotal,udp_vactive,udp_vbegin,udp_vend,udp_vtotal,udp_interlace?"interlace":"progressive",udp_interlace);
       	                     	                       	                                    	                      			       
    poc->PoC_pixels_ddr = 0;
    poc->PoC_H = udp_hactive;
    poc->PoC_HFP = udp_hbegin - udp_hactive;
    poc->PoC_HS = udp_hend - udp_hbegin;
    poc->PoC_HBP = udp_htotal - udp_hend;
    poc->PoC_V = udp_vactive;
    poc->PoC_VFP = udp_vbegin - udp_vactive;
    poc->PoC_VS = udp_vend - udp_vbegin;
    poc->PoC_VBP = udp_vtotal - udp_vend;         
           
    poc->PoC_ce_pix = (udp_pclock * 16 < 90) ? 16 : (udp_pclock * 12 < 90) ? 12 : (udp_pclock * 8 < 90) ? 8 : (udp_pclock * 6 < 90) ? 6 : 4;	// we want at least 40Mhz clksys for vga scaler	       
    
    poc->PoC_interlaced = (udp_interlace >= 1) ? 1 : 0; 
    poc->PoC_FB_progressive = (udp_interlace == 0 || udp_interlace == 2) ? 1 : 0;  
    
    poc->PoC_field_frame = poc->PoC_frame_ddr + 1;
    poc->PoC_field = 0;    
           
    int M=0;    
    int C=0;
    int K=0;	
    
    getMCK_PLL_Fractional(udp_pclock*poc->PoC_ce_pix,M,C,K);
    poc->PoC_pll_M0 = (M % 2 == 0) ? M >> 1 : (M >> 1) + 1;
    poc->PoC_pll_M1 = M >> 1;    
    poc->PoC_pll_C0 = (C % 2 == 0) ? C >> 1 : (C >> 1) + 1;
    poc->PoC_pll_C1 = C >> 1;	        
    poc->PoC_pll_K = K;	        
    
    poc->PoC_pixels_len = poc->PoC_H * poc->PoC_V;    
    
    if (poc->PoC_interlaced && !poc->PoC_FB_progressive)
    {
    	poc->PoC_pixels_len = poc->PoC_pixels_len >> 1;
    } 
     
    poc->PoC_bytes_len = (rgbMode) ? poc->PoC_pixels_len << 2 : poc->PoC_pixels_len * 3;            
    poc->PoC_bytes_recv = 0;          
    poc->PoC_buffer_offset = 0;
    
    LOG(1,"[FPGA header] %d %d %d %d %d %d %d %d ce_pix=%d PLL(M0=%d,M1=%d,C0=%d,C1=%d,K=%d) \n",poc->PoC_H,poc->PoC_HFP, poc->PoC_HS,poc->PoC_HBP,poc->PoC_V,poc->PoC_VFP, poc->PoC_VS,poc->PoC_VBP,poc->PoC_ce_pix,poc->PoC_pll_M0,poc->PoC_pll_M1,poc->PoC_pll_C0,poc->PoC_pll_C1,poc->PoC_pll_K);				       			       	
    
    //clean pixels on ddr (auto_blit)
    buffer[4] = 0x00;  
    buffer[5] = 0x00;	       	  
    buffer[6] = 0x00;  
    buffer[7] = 0x00; 	           		         
     
    //modeline + pll -> burst 3
    buffer[8]  =  poc->PoC_H & 0xff;        
    buffer[9]  = (poc->PoC_H >> 8);
    buffer[10] =  poc->PoC_HFP;
    buffer[11] =  poc->PoC_HS;
    buffer[12] =  poc->PoC_HBP;
    buffer[13] =  poc->PoC_V & 0xff;        
    buffer[14] = (poc->PoC_V >> 8);			
    buffer[15] =  poc->PoC_VFP;
    buffer[16] =  poc->PoC_VS;
    buffer[17] =  poc->PoC_VBP;
    
    //pll   
    buffer[18] =  poc->PoC_pll_M0;  
    buffer[19] =  poc->PoC_pll_M1;      
    buffer[20] =  poc->PoC_pll_C0;  
    buffer[21] =  poc->PoC_pll_C1; 
    buffer[22] = (poc->PoC_pll_K) & 0xff;  
    buffer[23] = (poc->PoC_pll_K >> 8) & 0xff;	       	  
    buffer[24] = (poc->PoC_pll_K >> 16) & 0xff;    
    buffer[25] = (poc->PoC_pll_K >> 24) & 0xff;   
    buffer[26] =  poc->PoC_ce_pix;  
    buffer[27] =  udp_interlace;     
    
    groovy_FPGA_switchres();                 
}


static void setClose()
{          				
	groovy_FPGA_init(0, 0, 0, 0);	
	isBlitting = 0;		
	numBlit = 0;
	blitCompression = 0;		
	free(poc);	
	initDDR();	
	isConnected = 0;		
	
	// load LOGO
	if (doScreensaver)
	{			
		loadLogo(1);		
		groovy_FPGA_init(1, 0, 0, 0);    	
		groovy_FPGA_blit(); 
		groovy_FPGA_logo(1);
		groovyLogo = 1;	
	}
	
}

static void sendACK(uint32_t udp_frame, uint16_t udp_vsync)
{          	
	LOG(2, "[ACK_%s]\n", "STATUS");	
	
	int flags = 0;
	flags |= MSG_CONFIRM;	
	char sendbuf[13];
	//echo
	sendbuf[0] = udp_frame & 0xff;
	sendbuf[1] = udp_frame >> 8;	
	sendbuf[2] = udp_frame >> 16;	
	sendbuf[3] = udp_frame >> 24;	
	sendbuf[4] = udp_vsync & 0xff;
	sendbuf[5] = udp_vsync >> 8;
	//gpu
	sendbuf[6] = fpga_vga_frame  & 0xff;
	sendbuf[7] = fpga_vga_frame  >> 8;	
	sendbuf[8] = fpga_vga_frame  >> 16;	
	sendbuf[9] = fpga_vga_frame  >> 24;	
	sendbuf[10] = fpga_vga_vcount & 0xff;
	sendbuf[11] = fpga_vga_vcount >> 8;
	//debug bits
	bitByte bits;
	bits.byte = 0;
	bits.u.bit0 = fpga_vram_ready;
	bits.u.bit1 = fpga_vram_end_frame;
	bits.u.bit2 = fpga_vram_synced;
	bits.u.bit3 = fpga_vga_frameskip;
	bits.u.bit4 = fpga_vga_vblank;
	bits.u.bit5 = fpga_vga_f1;
	bits.u.bit6 = fpga_audio;
	bits.u.bit7 = (fpga_vram_queue > 0) ? 1 : 0;
	sendbuf[12] = bits.byte;
	
	sendto(sockfd, sendbuf, 13, flags, (struct sockaddr *)&clientaddr, clilen);																
}

static void setInit(uint8_t compression, uint8_t audio_rate, uint8_t audio_chan, uint8_t rgb_mode)
{          				
	blitCompression = (compression <= 1) ? compression : 0;
	audioRate = (audio_rate <= 3) ? audio_rate : 0;
	audioChannels = (audio_chan <= 2) ? audio_chan : 0;	
	rgbMode = (rgb_mode <= 1) ? rgb_mode : 0;	
	poc = (PoC_type *) calloc(1, sizeof(PoC_type));
	initDDR();
	isBlitting = 0;	
	numBlit = 0;	
	
	// load LOGO
	if (doScreensaver)
	{
		groovy_FPGA_init(0, 0, 0, 0);
		groovy_FPGA_logo(0);																				
		groovyLogo = 0;					
	}
	
	if (!isConnected)
	{				
		char hoststr[NI_MAXHOST];
		char portstr[NI_MAXSERV];
		getnameinfo((struct sockaddr *)&clientaddr, clilen, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
		LOG(1,"[Connected %s:%s]\n", hoststr, portstr);		
		isConnected = 1;
	}
	
	do
	{
		groovy_FPGA_status(0);
	} while (fpga_init != 0);	
	
	groovy_FPGA_init(1, audioRate, audioChannels, rgbMode);		
}

static void setBlit(uint32_t udp_frame, uint32_t udp_lz4_size)
{          			
	poc->PoC_frame_recv = udp_frame;		
	poc->PoC_bytes_recv = 0;			
	poc->PoC_bytes_lz4_ddr = 0;		
	poc->PoC_bytes_lz4_len = (blitCompression) ? udp_lz4_size : 0;	
	poc->PoC_field = (!poc->PoC_FB_progressive) ? (poc->PoC_frame_recv + poc->PoC_field_frame) % 2 : 0; 
	poc->PoC_buffer_offset = (blitCompression) ? (poc->PoC_field_lz4) ? LZ4_OFFSET_B : LZ4_OFFSET_A : (!poc->PoC_FB_progressive && poc->PoC_field) ? FIELD_OFFSET : 0;					
	poc->PoC_field_lz4 = (blitCompression) ? !poc->PoC_field_lz4 : 0;	
	
	isBlitting = 1;	
	isCorePriority = 1;
	numBlit = 0;	
	
	if (blitCompression)
	{
		groovy_FPGA_blit_lz4(0, 0); 	
	}	
	else
	{
		groovy_FPGA_blit(0, 0);
	}
					
	if (doVerbose > 0 && doVerbose < 3)
	{
		groovy_FPGA_status(0);
		LOG(1, "[GET_STATUS][DDR fr=%d bl=%d][GPU fr=%d vc=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][AUDIO=%d][LZ4 inf=%d]\n", poc->PoC_frame_ddr, numBlit, fpga_vga_frame, fpga_vga_vcount, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_audio, fpga_lz4_uncompressed);								
	}	
	
	if (!doVerbose && !fpga_vram_synced)
 	{
 		groovy_FPGA_status(0);
 		LOG(0, "[GET_STATUS][DDR fr=%d bl=%d][GPU fr=%d vc=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][AUDIO=%d][LZ4 inf=%d]\n", poc->PoC_frame_ddr, numBlit, fpga_vga_frame, fpga_vga_vcount, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_audio, fpga_lz4_uncompressed);		
 	}		 		
	gettimeofday(&blitStart, NULL); 	
}

static void setBlitAudio(uint16_t udp_bytes_samples)
{	
	poc->PoC_bytes_audio_len = udp_bytes_samples;	
	poc->PoC_buffer_offset = AUDIO_OFFSET;	
	poc->PoC_bytes_recv = 0;	
		
	isBlitting = 2;																	
	isCorePriority = 1;
}

static void setBlitRawAudio(uint16_t len)
{
	poc->PoC_bytes_recv += len;	
	isBlitting = (poc->PoC_bytes_recv >= poc->PoC_bytes_audio_len) ? 0 : 2;	
	
	LOG(2, "[DDR_AUDIO][%d/%d]\n", poc->PoC_bytes_recv, poc->PoC_bytes_audio_len);
			
	if (isBlitting == 0)
	{
		uint16_t sound_samples = (audioChannels == 0) ? 0 : (audioChannels == 1) ? poc->PoC_bytes_audio_len >> 1 : poc->PoC_bytes_audio_len >> 2;		
		groovy_FPGA_audio(sound_samples);				
		poc->PoC_buffer_offset = 0;
		isCorePriority = 0;		
	}	
}
												
static void setBlitRaw(uint16_t len)
{       	
	poc->PoC_bytes_recv += len;									
	isBlitting = (poc->PoC_bytes_recv >= poc->PoC_bytes_len) ? 0 : 1;
	       	
       	if (!hpsBlit) //ASAP
       	{
       		numBlit++;     		
		groovy_FPGA_blit(poc->PoC_bytes_recv, numBlit);		
		LOG(2, "[ACK_BLIT][(%d) px=%d/%d %d/%d]\n", numBlit, poc->PoC_pixels_ddr, poc->PoC_pixels_len, poc->PoC_bytes_recv, poc->PoC_bytes_len);      
       	}
       	else
       	{
       		LOG(2, "[DDR_BLIT][%d/%d]\n", poc->PoC_bytes_recv, poc->PoC_bytes_len);	
       	}
		
        if (isBlitting == 0)
        {	
        	isCorePriority = 0;	        	
        	if (poc->PoC_pixels_ddr < poc->PoC_pixels_len)
        	{        		
        		numBlit++;        		
			groovy_FPGA_blit(poc->PoC_bytes_recv, numBlit);				
			LOG(2, "[ACK_BLIT][(%d) px=%d/%d %d/%d]\n", numBlit, poc->PoC_pixels_ddr, poc->PoC_pixels_len, poc->PoC_bytes_recv, poc->PoC_bytes_len);      
        	}
        	poc->PoC_buffer_offset = 0;        	
		gettimeofday(&blitStop, NULL); 
        	int difBlit = ((blitStop.tv_sec - blitStart.tv_sec) * 1000000 + blitStop.tv_usec - blitStart.tv_usec);
		LOG(1, "[DDR_BLIT][TOTAL %06.3f][(%d) Bytes=%d]\n",(double) difBlit/1000, numBlit, poc->PoC_bytes_len); 			                	 
        }			     
}

static void setBlitLZ4(uint16_t len)
{
	poc->PoC_bytes_recv += len;
	isBlitting = (poc->PoC_bytes_recv >= poc->PoC_bytes_lz4_len) ? 0 : 1;
		
	if (!hpsBlit) //ASAP
       	{
       		numBlit++;     		
		groovy_FPGA_blit_lz4(poc->PoC_bytes_recv, numBlit); 		
		LOG(2, "[ACK_BLIT][(%d) %d/%d]\n", numBlit, poc->PoC_bytes_recv, poc->PoC_bytes_lz4_len);
       	} 
       	else
       	{
       		LOG(2, "[LZ4_BLIT][%d/%d]\n", poc->PoC_bytes_recv, poc->PoC_bytes_lz4_len);
       	}			
			
	if (isBlitting == 0)
        {	
        	isCorePriority = 0;	
        	if (poc->PoC_bytes_lz4_ddr < poc->PoC_bytes_lz4_len)
        	{        		
        		numBlit++;     		
			groovy_FPGA_blit_lz4(poc->PoC_bytes_recv, numBlit); 		
			LOG(2, "[ACK_BLIT][(%d) %d/%d]\n", numBlit, poc->PoC_bytes_recv, poc->PoC_bytes_lz4_len);   
        	}        	        	
        	poc->PoC_buffer_offset = 0;            		    	
		gettimeofday(&blitStop, NULL); 
        	int difBlit = ((blitStop.tv_sec - blitStart.tv_sec) * 1000000 + blitStop.tv_usec - blitStart.tv_usec);
		LOG(1, "[LZ4_BLIT][TOTAL %06.3f][(%d) Bytes=%d]\n",(double) difBlit/1000, numBlit, poc->PoC_bytes_lz4_len); 			                	 
        }	
}

static void groovy_map_ddr()
{	
    	int pagesize = sysconf(_SC_PAGE_SIZE);
    	if (pagesize==0) pagesize=4096;       	
    	int offset = BASEADDR;
    	int map_start = offset & ~(pagesize - 1);
    	int map_off = offset - map_start;
    	int num_bytes=BUFFERSIZE;
    	
    	map = (uint8_t*)shmem_map(map_start, num_bytes+map_off);      	    	
    	buffer = map + map_off;         	
    	    
    	initDDR();
    	poc = (PoC_type *) calloc(1, sizeof(PoC_type));	
    	
    	isCorePriority = 0;	    	
    	isBlitting = 0;	    	
}

static void groovy_udp_server_init()
{
	LOG(1, "[UDP][SERVER][%s]\n", "START");
	
	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);				
    	if (sockfd < 0)
    	{
    		printf("socket error\n");       		
    	}    	    				    	        	
	        		    
    	memset(&servaddr, 0, sizeof(servaddr));
    	servaddr.sin_family = AF_INET;
    	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    	servaddr.sin_port = htons(UDP_PORT);
    	  	
        // Non blocking socket                                                                                                     
    	int flags;
    	flags = fcntl(sockfd, F_GETFD, 0);    	
    	if (flags < 0) 
    	{
      		printf("get falg error\n");       		
    	}
    	flags |= O_NONBLOCK;
    	if (fcntl(sockfd, F_SETFL, flags) < 0) 
    	{
       		printf("set nonblock fail\n");       		
    	}   	
    	    		     	    	        	 	    		    	    		
	// Settings	
	int size = 2 * 1024 * 1024;	
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUFFORCE, (void*)&size, sizeof(size)) < 0)     
        {
        	printf("Error so_rcvbufforce\n");        	
        }  
                                    	
	int beTrueAddr = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void*)&beTrueAddr,sizeof(beTrueAddr)) < 0)
	{
        	printf("Error so_reuseaddr\n");        	
        } 
                            	                         	         
    	if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    	{
    		printf("bind error\n");        	
    	}         	    	    	 	    	    	    	    	
    	
    	groovyServer = 2;   	    	    	 			
}

static void groovy_start()
{			
	printf("Groovy-Server 0.3 starting\n");
	
	// get HPS Server Settings
	groovy_FPGA_hps();   
	
	// reset fpga    
    	groovy_FPGA_init(0, 0, 0, 0);    	    	
	
	// map DDR 		
	groovy_map_ddr();
	
	// load LOGO	
	if (doScreensaver)
	{
		loadLogo(1);
		groovy_FPGA_init(1, 0, 0, 0);    		
		groovy_FPGA_blit(); 
		groovy_FPGA_logo(1); 			
		groovyLogo = 1;			
	}	
	
    	// UDP Server     	
	groovy_udp_server_init();   	  	  		    	    	    	           	    	    	    	    	    	        	    	
	    	
    	printf("Groovy-Server 0.3 started\n");    			    	                          		
}

void groovy_poll()
{	
	if (!groovyServer)
	{
		groovy_start();
	}	    	                                                                          	                                               					   											
  				
	int len = 0; 					
	char* recvbufPtr;
	
	do
	{												
		if (doVerbose == 3 && isConnected)		
		{		
			groovy_FPGA_status(0);																		 
			//LOG(3, "[GET_STATUS][DDR fr=%d bl=%d][GPU vc=%d fr=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][LZ4 state_1=%d inf=%d wr=%d, run=%d resume=%d t1=%d t2=%d cmd_fskip=%d stop=%d AB=%d com=%d grav=%d lleg=%d, sub=%d blit=%d]\n", poc->PoC_frame_ddr, numBlit, fpga_vga_vcount, fpga_vga_frame, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_lz4_state, fpga_lz4_uncompressed, fpga_lz4_writed, fpga_lz4_run, fpga_lz4_resume, fpga_lz4_test1, fpga_lz4_test2, fpga_lz4_cmd_fskip, fpga_lz4_stop, fpga_lz4_AB, fpga_lz4_compressed, fpga_lz4_gravats, fpga_lz4_llegits, fpga_lz4_subframe_bytes, fpga_lz4_subframe_blit);     			     						
			LOG(3, "[GET_STATUS][DDR fr=%d bl=%d][GPU fr=%d vc=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][LZ4 un=%d]\n", poc->PoC_frame_ddr, numBlit, fpga_vga_frame, fpga_vga_vcount, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_lz4_uncompressed);     			     									
		}					     		    
						
		recvbufPtr = (isBlitting) ? (char *) (buffer + HEADER_OFFSET + poc->PoC_buffer_offset + poc->PoC_bytes_recv) : (char *) &recvbuf[0];											
		len = recvfrom(sockfd, recvbufPtr, 65536, 0, (struct sockaddr *)&clientaddr, &clilen);			
									
		if (len > 0) 
		{    				
			if (isBlitting)
			{								
				//udp error lost detection 				
				if (len > 0 && len < 1472)
				{	
					int prev_len = len;
					int tota_len = 0;							
					if (isBlitting == 1 && !blitCompression && poc->PoC_bytes_recv + len != poc->PoC_bytes_len) // raw rgb
					{
						groovy_FPGA_blit(poc->PoC_bytes_len, 65535);
						isBlitting = 0;
						prev_len = poc->PoC_bytes_len % 1472;
						tota_len = poc->PoC_bytes_len;
					}					
					if (isBlitting == 1 && blitCompression && poc->PoC_bytes_recv + len != poc->PoC_bytes_lz4_len) // lz4 rgb
					{
						groovy_FPGA_blit_lz4(poc->PoC_bytes_lz4_len, 65535);
						isBlitting = 0;						
						prev_len = poc->PoC_bytes_lz4_len % 1472;
						tota_len = poc->PoC_bytes_lz4_len;
					}																										
					if (isBlitting == 2 && poc->PoC_bytes_recv + len != poc->PoC_bytes_audio_len) // audio
					{
						isBlitting = 0;
						prev_len = poc->PoC_bytes_audio_len % 1472;
						tota_len = poc->PoC_bytes_audio_len;
					}					
					if (!isBlitting)
					{
						isCorePriority = 0;							
						if (len != prev_len && len <= 26)
						{							 
							memcpy((char *) &recvbuf[0], recvbufPtr, len);
							recvbufPtr = (char *) &recvbuf[0]; 
							LOG(0,"[UDP_ERROR][RECONFIG fr=%d recv=%d/%d prev_len=%d len=%d]\n", poc->PoC_frame_ddr, poc->PoC_bytes_recv, tota_len, prev_len, len);								
						} 
						else
						{
							LOG(0,"[UDP_ERROR][fr=%d recv=%d/%d len=%d]\n", poc->PoC_frame_ddr, poc->PoC_bytes_recv, tota_len, len);	
							len = -1;
						}
					}															
				}
			}	
											
			if (!isBlitting)
			{				
	    			switch (recvbufPtr[0]) 
	    			{		
		    			case CMD_CLOSE:
					{	
						if (len == 1)
						{	
							LOG(1, "[CMD_CLOSE][%d]\n", recvbufPtr[0]);				   											        							
							setClose();																					
						}	
					}; break;
					
					case CMD_INIT:
					{	
						if (len == 4 || len == 5)
						{
							if (doVerbose)
							{
								initVerboseFile();
							}
							uint8_t compression = recvbufPtr[1];														
							uint8_t audio_rate = recvbufPtr[2];
							uint8_t audio_channels = recvbufPtr[3];	
							uint8_t rgb_mode = (len == 5) ? recvbufPtr[4] : 0;														
							setInit(compression, audio_rate, audio_channels, rgb_mode);							
							sendACK(0, 0);				
							LOG(1, "[CMD_INIT][%d][LZ4=%d][Audio rate=%d chan=%d][%s]\n", recvbufPtr[0], compression, audio_rate, audio_channels, (rgb_mode) ? "RGBA888" : "RGB888");											       										
						}	
					}; break;
					
					case CMD_SWITCHRES:
					{	
						if (len == 26)
						{			       	       				       			
				       			setSwitchres();			
				       			LOG(1, "[CMD_SWITCHRES][%d]\n", recvbufPtr[0]);		       						       				       						       						       		
				       		}	
					}; break;
					
					case CMD_AUDIO:
					{	
						if (len == 3)
						{							
							uint16_t udp_bytes_samples = ((uint16_t) recvbufPtr[2]  << 8) | recvbufPtr[1];							
							setBlitAudio(udp_bytes_samples);					
							LOG(1, "[CMD_AUDIO][%d][Bytes=%d]\n", recvbufPtr[0], udp_bytes_samples);	
						}
					}; break;											
					
					case CMD_GET_STATUS:
					{	
						if (len == 1)
						{
							groovy_FPGA_status(1);					
							sendACK(0, 0);							        											
				       			LOG(1, "[CMD_GET_STATUS][%d][GPU fr=%d vc=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][AUDIO=%d][LZ4 inf=%d]\n", recvbufPtr[0], fpga_vga_frame, fpga_vga_vcount, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_audio, fpga_lz4_uncompressed);				       		
							
						}	
					}; break;
					
					case CMD_BLIT_VSYNC:
					{	
						if (len == 7 || len == 11 || len == 9)
						{	
							uint32_t udp_lz4_size = 0;				
							uint32_t udp_frame = ((uint32_t) recvbufPtr[4]  << 24) | ((uint32_t)recvbufPtr[3]  << 16) | ((uint32_t)recvbufPtr[2]  << 8) | recvbufPtr[1];
							uint16_t udp_vsync = ((uint16_t) recvbufPtr[6]  << 8) | recvbufPtr[5];
							if (!blitCompression)
							{
								LOG(1, "[CMD_BLIT][%d][Frame=%d][Vsync=%d]\n", recvbufPtr[0], udp_frame, udp_vsync);
							}							
					       		else if (len == 11 && blitCompression)
							{
								udp_lz4_size = ((uint32_t) recvbufPtr[10]  << 24) | ((uint32_t)recvbufPtr[9]  << 16) | ((uint32_t)recvbufPtr[8]  << 8) | recvbufPtr[7];
								LOG(1, "[CMD_BLIT][%d][Frame=%d][Vsync=%d][CSize=%d]\n", recvbufPtr[0], udp_frame, udp_vsync, udp_lz4_size);							
							}																													       		
					       		setBlit(udp_frame, udp_lz4_size);					       			
					       		groovy_FPGA_status(1);
					       		sendACK(udp_frame, udp_vsync);	
					       	}					       					       				       					       		
					}; break;										
					
					default: 
					{
						LOG(1,"command: %i (len=%d)\n", recvbufPtr[0], len);						
					}										
				}				
			}			
			else
			{						        																																		
				if (poc->PoC_bytes_len > 0) // modeline?
				{       
					if (isBlitting == 1) 
					{
						if (blitCompression)
						{
							setBlitLZ4(len);
						}
						else
						{
							setBlitRaw(len); 
						}
					}  										
					else 
					{
						setBlitRawAudio(len);
					} 					
				}
				else
				{					
					LOG(1, "[UDP_BLIT][%d bytes][Skipped no modeline]\n", len);
					isBlitting = 0;
					isCorePriority = 0;					
				}																				
			}							
		} 							       						
								
	} while (isCorePriority);
	
	if (doScreensaver && groovyLogo)
	{
		loadLogo(0);
	}		
				        
	if (doVerbose > 0 && CheckTimer(logTime))				
   	{   	      		
		fflush(fp);	
		logTime = GetTimer(LOG_TIMER);		
   	} 
   	   	    	
   	              
} 








     