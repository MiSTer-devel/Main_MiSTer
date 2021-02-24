#ifndef __ST_TOS_H__
#define __ST_TOS_H__

#include "../../file_io.h"

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
#define TOS_CONTROL_VIDEO_AR1     0x00000200

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
#define TOS_CONTROL_REDIR0        0x04000000 // unused
#define TOS_CONTROL_REDIR1        0x08000000 // unused

#define TOS_CONTROL_VIKING        0x10000000   // Viking graphics card

#define TOS_CONTROL_BORDER        0x20000000
#define TOS_CONTROL_MDE60         0x40000000

#define TOS_CONTROL_VIDEO_AR2     0x80000000

extern const char* tos_mem[];
extern const char* tos_scanlines[];
extern const char* tos_stereo[];
extern const char* tos_chipset[];

const char* tos_get_cfg_string(int num);

unsigned long tos_system_ctrl();
void tos_upload(const char *);
void tos_poll();
void tos_update_sysctrl(uint32_t ctrl);
char tos_disk_is_inserted(int index);
void tos_insert_disk(int index, const char *name);
void tos_eject_all();
void tos_reset(char cold);
const char *tos_get_disk_name(int);
const char *tos_get_image_name();
const char *tos_get_cartridge_name();
char tos_cartridge_is_inserted();
void tos_load_cartridge(const char *);

void tos_config_load(int slot); // slot -1 == last config
void tos_config_save(int slot);
int tos_config_exists(int slot);

int tos_get_ar();
void tos_set_ar(int ar);

uint32_t tos_get_extctrl();
void tos_set_extctrl(uint32_t ext_ctrl);

#endif
