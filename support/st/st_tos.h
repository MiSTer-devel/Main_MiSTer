#ifndef __ST_TOS_H__
#define __ST_TOS_H__

#include "../../file_io.h"

// FPGA spi cmommands
#define MIST_INVALID      0x00

// memory interface
#define MIST_SET_ADDRESS  0x01
#define MIST_WRITE_MEMORY 0x02
#define MIST_READ_MEMORY  0x03
#define MIST_SET_CONTROL  0x04
#define MIST_GET_DMASTATE 0x05   // reads state of dma and floppy controller
#define MIST_ACK_DMA      0x06   // acknowledge a dma command
#define MIST_BUS_REQ      0x07   // request bus
#define MIST_BUS_REL      0x08   // release bus
#define MIST_SET_VADJ     0x09
#define MIST_NAK_DMA      0x0a   // reject a dma command

// tos sysconfig bits:
// 0     - RESET
// 1-3   - Memory configuration
// 4-5   - CPU configuration
// 6-7   - Floppy A+B write protection
// 8     - Color/Monochrome mode
// 9     - PAL mode in 56 or 50 Hz
// 10-17 - ACSI device enable

// memory configurations (0x02/0x04/0x08)
// (currently 4MB are fixed and cannot be changed)
#define TOS_MEMCONFIG_512K       (0<<1)   // not yet supported
#define TOS_MEMCONFIG_1M         (1<<1)   // not yet supported
#define TOS_MEMCONFIG_2M         (2<<1)   // not yet supported
#define TOS_MEMCONFIG_4M         (3<<1)   // not yet supported
#define TOS_MEMCONFIG_8M         (4<<1)
#define TOS_MEMCONFIG_14M        (5<<1)
#define TOS_MEMCONFIG_RES0       (6<<1)   // reserved
#define TOS_MEMCONFIG_RES1       (7<<1)   // reserved

// cpu configurations (0x10/0x20)
#define TOS_CPUCONFIG_68000      (0<<4)
#define TOS_CPUCONFIG_68010      (1<<4)
#define TOS_CPUCONFIG_RESERVED   (2<<4)
#define TOS_CPUCONFIG_68020      (3<<4)

// control bits (all control bits have unknown state after core startup)
#define TOS_CONTROL_CPU_RESET     0x00000001
#define TOS_CONTROL_FDC_WR_PROT_A 0x00000040
#define TOS_CONTROL_FDC_WR_PROT_B 0x00000080
#define TOS_CONTROL_VIDEO_COLOR   0x00000100   // input to mfp
#define TOS_CONTROL_PAL50HZ       0x00000200   // display pal at 50hz (56 hz otherwise)

// up to eight acsi devices can be enabled
#define TOS_ACSI0_ENABLE          0x00000400
#define TOS_ACSI1_ENABLE          0x00000800
#define TOS_ACSI2_ENABLE          0x00001000
#define TOS_ACSI3_ENABLE          0x00002000
#define TOS_ACSI4_ENABLE          0x00004000
#define TOS_ACSI5_ENABLE          0x00008000
#define TOS_ACSI6_ENABLE          0x00010000
#define TOS_ACSI7_ENABLE          0x00020000

#define TOS_CONTROL_TURBO         0x00040000
#define TOS_CONTROL_BLITTER       0x00080000

#define TOS_CONTROL_SCANLINES0    0x00100000   // 0 = off, 1 = 25%, 2 = 50%, 3 = 75%
#define TOS_CONTROL_SCANLINES1    0x00200000
#define TOS_CONTROL_SCANLINES     (TOS_CONTROL_SCANLINES0|TOS_CONTROL_SCANLINES1)

#define TOS_CONTROL_STEREO        0x00400000
#define TOS_CONTROL_STE           0x00800000
#define TOS_CONTROL_MSTE          0x01000000
#define TOS_CONTROL_ETHERNET      0x02000000

// USB redirection modes
// (NONE=0, RS232=1, PARALLEL=2, MIDI=3)
#define TOS_CONTROL_REDIR0        0x04000000
#define TOS_CONTROL_REDIR1        0x08000000

#define TOS_CONTROL_VIKING        0x10000000   // Viking graphics card

unsigned long tos_system_ctrl(void);

void tos_upload(const char *);
void tos_poll();
void tos_update_sysctrl(unsigned long);
char *tos_get_disk_name(std::size_t);
char tos_disk_is_inserted(std::size_t index);
void tos_insert_disk(std::size_t i, const char *name);
void tos_eject_all();
void tos_select_hdd_image(std::size_t i, const char *name);
void tos_set_direct_hdd(char on);
char tos_get_direct_hdd();
void tos_reset(char cold);
char *tos_get_image_name();
char *tos_get_cartridge_name();
char tos_cartridge_is_inserted();
void tos_load_cartridge(const char *);

void tos_set_video_adjust(char axis, char value);
char tos_get_video_adjust(char axis);

void tos_config_init(void);
void tos_config_save(void);

#endif
