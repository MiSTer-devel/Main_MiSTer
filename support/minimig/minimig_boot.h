// boot.h
// bootscreen functions
// 2014, rok.krajnc@gmail.com


#ifndef __MINIMIG_BOOT_H__
#define __MINIMIG_BOOT_H__


//// defines ////
#define SCREEN_WIDTH    640
#define SCREEN_HEIGHT   256
#define SCREEN_SIZE     SCREEN_WIDTH * SCREEN_HEIGHT
#define SCREEN_MEM_SIZE 2*SCREEN_SIZE/8
#define SCREEN_ADDRESS  0x80000
#define SCREEN_BPL1     0x80000
#define SCREEN_BPL2     0x85000


#define LOGO_WIDTH      208
#define LOGO_HEIGHT     32
#define LOGO_OFFSET     (64*SCREEN_WIDTH/8+24)
#define LOGO_LSKIP      (SCREEN_WIDTH-LOGO_WIDTH)/8
#define LOGO_SIZE       0x680
#define LOGO_FILE       "MINIMIG.ART"

#define BALL_SIZE       0x4000
#define BALL_ADDRESS    0x8a000
#define BALL_FILE       "MINIMIG.BAL"

#define COPPER_SIZE     0x35c
#define COPPER_ADDRESS  0x8e680
#define COPPER_FILE     "MINIMIG.COP"

#define BLITS           64

//// functions ////
void BootInit();
void BootPrintEx(const char * str);
void BootHome();

#define BootPrint(text) printf("%s\n", text)

#endif // __BOOT_H__

