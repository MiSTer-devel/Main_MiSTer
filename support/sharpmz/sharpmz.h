/////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Name:            sharpmz.h
// Created:         July 2018
// Author(s):       Philip Smart
// Description:     Sharp MZ series MiSTer Menu Add-On.
//                  This module is an extension to the MiSTer control program. It adds extensions to
//                  the menu system and additional I/O control specific to the Sharp MZ series
//                  emulation.
//
// Credits:
// Copyright:       (c) 2018 Philip Smart <philip.smart@net2net.org>
//
// History:         July 2018      - Initial module written.
//                  Sept 2018      - Synchronised with main MiSTer codebase.
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////
// This source file is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This source file is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
/////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef SHARPMZ_H
#define SHARPMZ_H

#include "../../file_io.h"

// Defaults.
//
#define MZ_TAPE_HEADER_STACK_ADDR         0x10f0
#define MZ_TAPE_HEADER_SIZE               128
#define MEMORY_DUMP_FILE                  "cmp/lastload"
#define MAX_FILENAME_SIZE                 1024

// HPS commands to fpga block.
//
#define SHARPMZ_EOF                       0x00
#define SHARPMZ_SOF                       0x01
#define SHARPMZ_FILE_TX                   0x53
#define SHARPMZ_FILE_TX_DAT               0x54
#define SHARPMZ_FILE_INDEX                0x55
#define SHARPMZ_FILE_INFO                 0x56
#define SHARPMZ_FILE_ADDR                 0x57
#define SHARPMZ_FILE_ADDR_TX              0x58
#define SHARPMZ_FILE_ADDR_RX              0x59
#define SHARPMZ_CONFIG_RX                 0x5A
#define SHARPMZ_CONFIG_TX                 0x5B

// Memory blocks within the Emulator.
//
#define SHARPMZ_MEMBANK_ALL               0xff
#define SHARPMZ_MEMBANK_SYSROM            0x00
#define SHARPMZ_MEMBANK_SYSRAM            0x02
#define SHARPMZ_MEMBANK_KEYMAP            0x03
#define SHARPMZ_MEMBANK_VRAM              0x04
#define SHARPMZ_MEMBANK_CMT_HDR           0x05
#define SHARPMZ_MEMBANK_CMT_DATA          0x06
#define SHARPMZ_MEMBANK_CGROM             0x07
#define SHARPMZ_MEMBANK_CGRAM             0x08
#define SHARPMZ_MEMBANK_MAXBANKS          9

// Name of the configuration file.
//
#define SHARPMZ_CONFIG_FILENAME           "SHARPMZ.CFG"

// Name of the core.
//
#define SHARPMZ_CORE_NAME                 "SharpMZ"

// Maximum number of machines currently supported by the emulation.
//
#define MAX_MZMACHINES                    8

// Maximum number of sub-roms per machine.
//
#define MAX_MROMOPTIONS                   2

// Numeric index of each machine.
//
#define MZ80K_IDX                         0    // 000
#define MZ80C_IDX                         1    // 001
#define MZ1200_IDX                        2    // 010
#define MZ80A_IDX                         3    // 011
#define MZ700_IDX                         4    // 100
#define MZ800_IDX                         5    // 101
#define MZ80B_IDX                         6    // 110
#define MZ2000_IDX                        7    // 111

// Maximum number of images which can be loaded.
//
#define MAX_IMAGE_TYPES                   6

// Numeric index of each main rom image category.
//
#define MROM_IDX                          0
#define MROM_80C_IDX                      1
#define CGROM_IDX                         2
#define KEYMAP_IDX                        3
#define USERROM_IDX                       4
#define FDCROM_IDX                        5

// Numeric index of monitor rom subtypes.
//
#define MONITOR                           0
#define MONITOR_80C                       1

// Numeric index of Option rom subtypes.
//
#define USERROM                           0
#define FDCROM                            1

// Tape(CMT) Data types.
//
#define SHARPMZ_CMT_MC                    1    // machine code program.
#define SHARPMZ_CMT_BASIC                 2    // MZ-80 Basic program.
#define SHARPMZ_CMT_DATA                  3    // MZ-80 data file.
#define SHARPMZ_CMT_700DATA               4    // MZ-700 data file.
#define SHARPMZ_CMT_700BASIC              5    // MZ700 Basic program.

// Numeric id of system registers.
//
#define REGISTER_MODEL                    0
#define REGISTER_DISPLAY                  1
#define REGISTER_CPU                      2
#define REGISTER_AUDIO                    3
#define REGISTER_CMT                      4
#define REGISTER_CMT2                     5
#define REGISTER_USERROM                  6
#define REGISTER_FDCROM                   7
#define REGISTER_8                        8
#define REGISTER_9                        9
#define REGISTER_10                      10
#define REGISTER_11                      11
#define REGISTER_12                      12
#define REGISTER_13                      13
#define REGISTER_DEBUG                   14
#define REGISTER_DEBUG2                  15
#define MAX_REGISTERS                    16

// Numeric id of bit for a given CMT register flag.
//
#define CMT_PLAY_READY                    0    // Tape play back buffer, 0 = empty, 1 = full.
#define CMT_PLAYING                       1    // Tape playback, 0 = stopped, 1 = in progress.
#define CMT_RECORD_READY                  2    // Tape record buffer full.
#define CMT_RECORDING                     3    // Tape recording, 0 = stopped, 1 = in progress.
#define CMT_ACTIVE                        4    // Tape transfer in progress, 0 = no activity, 1 = activity.
#define CMT_SENSE                         5    // Tape state Sense out.
#define CMT_WRITEBIT                      6    // Write bit to MZ.
#define CMT_READBIT                       7    // Receive bit from MZ.
#define CMT_MOTOR                         8    // Motor on/off.

// Default load addresses of roms.
//
static const unsigned int MZLOADADDR[MAX_IMAGE_TYPES][MAX_MZMACHINES]  =
                                           {
                                             { 0x00000, 0x03800, 0x07000,  0x0A800, 0x0E000, 0x11800, 0x15000, 0x17800 },    // MROM
                                             { 0x01000, 0x04800, 0x08000,  0x0B800, 0x0F000, 0x12800, 0x15800, 0x18000 },    // MROM 80C
                                             { 0x70000, 0x71000, 0x72000,  0x73000, 0x74000, 0x75000, 0x76000, 0x77000 },    // CGROM
                                             { 0x30000, 0x30100, 0x30200,  0x30300, 0x30400, 0x30500, 0x30600, 0x30700 },    // KEYMAP
                                             { 0x02000, 0x05800, 0x09000,  0x0C800, 0x10000, 0x13800, 0x16000, 0x18800 },    // USERROM
                                             { 0x02800, 0x06000, 0x09800,  0x0D000, 0x10800, 0x14000, 0x16800, 0x19000 }     // FDCROM
                                           };

// Default size of roms.
//
static const unsigned int MZLOADSIZE[MAX_IMAGE_TYPES][MAX_MZMACHINES] =
                                           {
                                             { 0x1000,  0x1000,  0x1000,   0x1000,  0x1000,  0x1000,  0x0800,  0x0800 },     // MROM
                                             { 0x1000,  0x1000,  0x1000,   0x1000,  0x1000,  0x1000,  0x0800,  0x0800 },     // MROM 80C
                                             { 0x0800,  0x0800,  0x0800,   0x0800,  0x1000,  0x0800,  0x0800,  0x0800 },     // CGROM
                                             { 0x0100,  0x0100,  0x0100,   0x0100,  0x0100,  0x0100,  0x0100,  0x0100 },     // KEYMAP
                                             { 0x0800,  0x0800,  0x0800,   0x0800,  0x0800,  0x0800,  0x0800,  0x0800 },     // USERROM
                                             { 0x1000,  0x1000,  0x1000,   0x1000,  0x1000,  0x1000,  0x1000,  0x1000 }      // FDCROM
                                           };

static const unsigned int MZBANKSIZE[SHARPMZ_MEMBANK_MAXBANKS]         =   { 0x20000, 0x0000,  0x10000, 0x00800, 0x04000, 0x00080, 0x10000, 0x08000, 0x08000 };

#if !defined(MENU_H) && !defined(USER_IO_H)
// Lookup tables for menu entries.
//
const char *SHARPMZ_FAST_TAPE[]          = { "Off", "2x", "4x", "8x", "16x", "32x", "Off", "Off",
                                             "Off", "2x", "4x", "8x", "16x", "32x", "Off", "Off",
                                             "Off", "2x", "4x", "8x", "16x", "Off", "Off", "Off"
                                           };
const char *SHARPMZ_CPU_SPEED[]          = { "2MHz",   "4MHz", "8MHz",  "16MHz", "32MHz", "64MHz",  "2MHz",   "2MHz",
                                             "3.5MHz", "7MHz", "14MHz", "28MHz", "56MHz", "112MHz", "3.5MHz", "3.5MHz",
                                             "4MHz",   "8MHz", "16MHz", "32MHz", "64MHz", "4MHz",   "4MHz",   "4MHz"
                                           };
const char *SHARPMZ_TAPE_BUTTONS[]       = { "Off", "Play", "Record", "Auto" };
const char *SHARPMZ_AUDIO_SOURCE[]       = { "Sound", "Tape" };
const char *SHARPMZ_AUDIO_VOLUME[]       = { "Max", "14", "13", "12", "11", "10", "9", "8", "7", "6", "5", "4", "3", "2", "1", "Min" };
const char *SHARPMZ_AUDIO_MUTE[]         = { "Off", "Mute" };
const char *SHARPMZ_USERROM_ENABLED[]    = { "Disabled",  "Enabled" };
const char *SHARPMZ_FDCROM_ENABLED[]     = { "Disabled",  "Enabled" };
const char *SHARPMZ_ROM_ENABLED[]        = { "Disabled",  "Enabled" };
const char *SHARPMZ_AUTO_SAVE_ENABLED[]  = { "No",  "Yes" };
const char *SHARPMZ_DISPLAY_TYPE[]       = { "Mono 40x25", "Mono 80x25 ", "Colour 40x25", "Colour 80x25", "tbd3", "tbd4", "tbd5", "tbd6" };
const char *SHARPMZ_ASPECT_RATIO[]       = { "4:3", "16:9" };
const char *SHARPMZ_SCANDOUBLER_FX[]     = { "None", "HQ2x", "CRT 25%", "CRT 50%", "CRT 75%", "tbd1", "tbd2", "tbd3" };
const char *SHARPMZ_VRAMWAIT_MODE[]      = { "Off", "On" };
const char *SHARPMZ_VRAMDISABLE_MODE[]   = { "Enabled", "Disabled" };
const char *SHARPMZ_GRAMDISABLE_MODE[]   = { "Enabled", "Disabled" };
const char *SHARPMZ_PCG_MODE[]           = { "ROM", "RAM" };
const char *SHARPMZ_MACHINE_MODEL[]      = { "MZ80K", "MZ80C", "MZ1200", "MZ80A", "MZ700", "MZ800", "MZ80B", "MZ2000" };
const char *SHARPMZ_DEBUG_ENABLE[]       = { "Off", "On" };
const char *SHARPMZ_DEBUG_LEDS[]         = { "Off", "On" };
const char *SHARPMZ_DEBUG_LEDS_BANK[]    = { "T80",    "I/O",      "IOCTL",    "Config",   "MZ80C I",  "MZ80C II", "MZ80B I", "MZ80B II" };
const char *SHARPMZ_DEBUG_LEDS_SUBBANK[] = { "Auto",   "A7-0",     "A15-8",    "DI",       "Signals",  "",         "",        "",
                                             "Auto",   "Video",    "PS2Key",   "Signals",  "",         "",         "",        "",
                                             "Auto",   "A23-16",   "A15-8",    "A7-0",     "Signals",  "",         "",        "",
                                             "Auto",   "Config 1", "Config 2", "Config 3", "Config 4", "Config 5", "",        "",
                                             "Auto",   "CS 1",     "CS 2",     "CS 3",     "INT/RE",   "Clk",      "",        "",
                                             "Auto",   "CMT 1",    "CMT 2",    "CMT 3",    "",         "",         "",        "",
                                             "",       "",         "",         "",         "",         "",         "",        "",
                                             "",       "",         "",         "",         "",         "",         "",        "",
                                           };
const char *SHARPMZ_DEBUG_CPUFREQ[]      = { "CPU/CMT", "1MHz", "100KHz", "10KHz", "5KHz", "1KHz", "500Hz", "100Hz", "50Hz", "10Hz", "5Hz", "2Hz", "1Hz", "0.5Hz", "0.2Hz", "0.1Hz" };
const char *SHARPMZ_DEBUG_LEDS_SMPFREQ[] = { "CPU/CMT", "1MHz", "100KHz", "10KHz", "5KHz", "1KHz", "500Hz", "100Hz", "50Hz", "10Hz", "5Hz", "2Hz", "1Hz", "0.5Hz", "0.2Hz", "0.1Hz" };
const char *SHARPMZ_MEMORY_BANK[]        = { "SysROM",      "SysRAM",      "KeyMap",      "VRAM",      "CMTHDR",       "CMTDATA",       "CGROM",      "CGRAM",      "All" };
const char *SHARPMZ_MEMORY_BANK_FILE[]   = { "sysrom.dump", "sysram.dump", "keymap.dump", "vram.dump", "cmt_hdr.dump", "cmt_data.dump", "cgrom.dump", "cgram.dump", "all_memory.dump" };
const char *SHARPMZ_TAPE_TYPE[]          = { "N/A", "M/code", "MZ80 Basic", "MZ80 Data", "MZ700 Data", "MZ700 Basic", "N/A" };
const char *SHARPMZ_HELPTEXT[]           = { "Welcome to the Sharp MZ Series!  Use the cursor keys to navigate the menus.  Use space bar or enter to select an item.  Press Esc or F12 to exit the menus. ",
                                          0
                                        };
#else
// External definitions.
extern const char *SHARPMZ_FAST_TAPE[];
extern const char *SHARPMZ_TAPE_BUTTONS[];
extern const char *SHARPMZ_AUDIO_SOURCE[];
extern const char *SHARPMZ_AUDIO_VOLUME[];
extern const char *SHARPMZ_AUDIO_MUTE[];
extern const char *SHARPMZ_USERROM_ENABLED[];
extern const char *SHARPMZ_FDCROM_ENABLED[];
extern const char *SHARPMZ_ROM_ENABLED[];
extern const char *SHARPMZ_AUTO_SAVE_ENABLED[];
extern const char *SHARPMZ_DISPLAY_TYPE[];
extern const char *SHARPMZ_ASPECT_RATIO[];
extern const char *SHARPMZ_SCANDOUBLER_FX[];
extern const char *SHARPMZ_VRAMWAIT_MODE[];
extern const char *SHARPMZ_VRAMDISABLE_MODE[];
extern const char *SHARPMZ_GRAMDISABLE_MODE[];
extern const char *SHARPMZ_PCG_MODE[];
extern const char *SHARPMZ_MACHINE_MODEL[];
extern const char *SHARPMZ_CPU_SPEED[];
extern const char *SHARPMZ_DEBUG_ENABLE[];
extern const char *SHARPMZ_DEBUG_LEDS[];
extern const char *SHARPMZ_DEBUG_LEDS_BANK[];
extern const char *SHARPMZ_DEBUG_CPUFREQ[];
extern const char *SHARPMZ_DEBUG_LEDS_SMPFREQ[];
extern const char *SHARPMZ_TAPE_TYPE[];
extern const char *SHARPMZ_MEMORY_BANK[];
extern const char *SHARPMZ_MEMORY_BANK_FILE[];
extern const char *SHARPMZ_HELPTEXT[];
#endif

// Structure to store the name, load address and the size of a given rom.
//
typedef struct
{
    char          romFileName[MAX_FILENAME_SIZE];
    short         romEnabled;
    unsigned int  loadAddr;
    unsigned int  loadSize;
} romData_t;

// Structure to store the configuration data of the emulator.
//
typedef struct
{
    unsigned long  system_ctrl;                        // Original MiSTer system control register.
    unsigned char  system_reg[MAX_REGISTERS];          // Emulator control register bank.
    unsigned char  autoSave;                           // Automatically save tape file if RECORD_READY becomes active. Use filename in header.
    unsigned char  volume;                             // Volume setting, using the MiSTer DAC level control. BIt 0-3 = Attenuation, bit 4 = Mute.
    romData_t      romMonitor[MAX_MZMACHINES][MAX_MROMOPTIONS]; // Details of rom monitor images to upload.
    romData_t      romCG[MAX_MZMACHINES];              // Details of rom character generator images to upload.
    romData_t      romKeyMap[MAX_MZMACHINES];          // Details of rom Key mapping images to upload.
    romData_t      romUser[MAX_MZMACHINES];            // Details of User ROM images to upload.
    romData_t      romFDC[MAX_MZMACHINES];             // Details of FDC ROM images to upload.
} sharpmz_config_t;

// Structures to store the tape file queue.
//
typedef struct tape_queue_node tape_queue_node_t;
struct tape_queue_node
{
    char                   fileName[MAX_FILENAME_SIZE];
    tape_queue_node_t      *next;
};
//
typedef struct
{
    struct tape_queue_node *top;
    struct tape_queue_node *bottom;
    unsigned short         elements;
} tape_queue_t;

// MZ Series Tape header structure - 128 bytes.
//
typedef struct
{
    unsigned char  dataType;                           // 01 = machine code program.
                                                       // 02 MZ-80 Basic program.
                                                       // 03 MZ-80 data file.
                                                       // 04 MZ-700 data file.
                                                       // 05 MZ-700 Basic program.
    char           fileName[17];                       // File name.
    unsigned short fileSize;                           // Size of data partition.
    unsigned short loadAddress;                        // Load address of the program/data.
    unsigned short execAddress;                        // Execution address of program.
    unsigned char  comment[104];                       // Free text or code area.
} sharpmz_tape_header_t;

// UI state machine states, extension set to the main menu.cpp MENU enum. Values are set above the original enum so that they
// are detected and handle within this sharpmz code base.
//
enum SHARPMZ_MENU
{
    // Sharp MZ menu entries
	MENU_SHARPMZ_MAIN1                             = 0xa0,
	MENU_SHARPMZ_MAIN2                             = 0xa1,
	MENU_SHARPMZ_TAPE_STORAGE1                     = 0xa2,
	MENU_SHARPMZ_TAPE_STORAGE2                     = 0xa3,
	MENU_SHARPMZ_TAPE_STORAGE_LOAD_TAPE_TO_RAM     = 0xa4,
	MENU_SHARPMZ_TAPE_STORAGE_LOAD_TAPE_TO_RAM2    = 0xa5,
	MENU_SHARPMZ_TAPE_STORAGE_QUEUE_TAPE_TO_CMT    = 0xa6,
	MENU_SHARPMZ_TAPE_STORAGE_QUEUE_TAPE_TO_CMT2   = 0xa7,
	MENU_SHARPMZ_TAPE_STORAGE_SAVE_TAPE_FROM_CMT   = 0xa8,
	MENU_SHARPMZ_TAPE_STORAGE_SAVE_TAPE_FROM_CMT2  = 0xa9,
	MENU_SHARPMZ_FLOPPY_STORAGE1                   = 0xaa,
	MENU_SHARPMZ_FLOPPY_STORAGE2                   = 0xab,
	MENU_SHARPMZ_DISPLAY1                          = 0xb0,
	MENU_SHARPMZ_DISPLAY2                          = 0xb1,
	MENU_SHARPMZ_MACHINE1                          = 0xb2,
	MENU_SHARPMZ_MACHINE2                          = 0xb3,
	MENU_SHARPMZ_ROMS1                             = 0xb4,
	MENU_SHARPMZ_ROMS2                             = 0xb5,
	MENU_SHARPMZ_ROM_FILE_SELECTED                 = 0xb6,
    MENU_SHARPMZ_DEBUG1                            = 0xb7,
    MENU_SHARPMZ_DEBUG2                            = 0xb8
};

// Prototypes.
//
void                   sharpmz_reset(unsigned long, unsigned long);
short                  sharpmz_read_ram(char *, short);
void                   sharpmz_init(void);
void                   sharpmz_poll(void);
short                  sharpmz_read_ram(char *, short);
const char            *sharpmz_get_rom_name(void);
void                   sharpmz_set_config_register(short addr, unsigned char value);
unsigned char          sharpmz_read_config_register(short addr);
void                   sharpmz_send_file(romData_t &, char *);
void                   sharpmz_set_rom(romData_t *);
int                    sharpmz_FileRead(fileTYPE *file, void *pBuffer, int nSize);
short                  sharpmz_get_machine_group(void);
unsigned char          sharpmz_get_auto_save_enabled(void);
int                    sharpmz_get_fasttape(void);
int                    sharpmz_get_display_type(void);
int                    sharpmz_get_aspect_ratio(void);
int                    sharpmz_get_scandoubler_fx(void);
int                    sharpmz_get_vram_wait_mode(void);
int                    sharpmz_get_vram_disable_mode(void);
int                    sharpmz_get_gram_disable_mode(void);
int                    sharpmz_get_pcg_mode(void);
int                    sharpmz_get_machine_model(void);
int                    sharpmz_get_cpu_speed(void);
int                    sharpmz_get_audio_source(void);
int                    sharpmz_get_audio_volume(void);
int                    sharpmz_get_audio_mute(void);
int                    sharpmz_get_debug_enable(void);
int                    sharpmz_get_debug_leds(void);
int                    sharpmz_get_tape_buttons(void);
short                  sharpmz_get_next_memory_bank(void);
short                  sharpmz_get_next_debug_leds_bank(void);
short                  sharpmz_get_next_debug_leds_subbank(unsigned char);
short                  sharpmz_get_next_debug_cpufreq(void);
short                  sharpmz_get_next_debug_leds_smpfreq(void);
void                   sharpmz_set_auto_save_enabled(unsigned char);
void                   sharpmz_set_fasttape(short, short);
void                   sharpmz_set_display_type(short, short);
void                   sharpmz_set_aspect_ratio(short, short);
void                   sharpmz_set_scandoubler_fx(short, short);
void                   sharpmz_set_vram_wait_mode(short, short);
void                   sharpmz_set_vram_disable_mode(short, short);
void                   sharpmz_set_gram_disable_mode(short, short);
void                   sharpmz_set_pcg_mode(short, short);
void                   sharpmz_set_machine_model(short, short);
void                   sharpmz_set_cpu_speed(short, short);
void                   sharpmz_set_audio_source(short, short);
void                   sharpmz_set_audio_volume(short, short);
void                   sharpmz_set_audio_mute(short, short);
void                   sharpmz_set_debug_enable(short, short);
void                   sharpmz_set_debug_leds(short, short);
void                   sharpmz_set_debug_leds_bank(short, short);
void                   sharpmz_set_debug_leds_subbank(short, short);
void                   sharpmz_set_debug_cpufreq(short, short);
void                   sharpmz_set_debug_leds_smpfreq(short, short);
void                   sharpmz_set_tape_buttons(short, short);
int                    sharpmz_save_config(void);
int                    sharpmz_reset_config(short);
int                    sharpmz_reload_config(short);
short                  sharpmz_get_next_machine_model(void);
short                  sharpmz_get_user_rom_enabled(short);
void                   sharpmz_set_user_rom_enabled(short, short, short);
short                  sharpmz_get_fdc_rom_enabled(short);
void                   sharpmz_set_fdc_rom_enabled(short, short, short);
short                  sharpmz_get_custom_rom_enabled(short, short);
void                   sharpmz_set_custom_rom_enabled(short, short, short);
char                  *sharpmz_get_rom_file(short, short);
void                   sharpmz_set_rom_file(short, short, char *);
short                  sharpmz_read_tape_header(const char *);
short                  sharpmz_load_tape_to_ram(const char *, unsigned char);
short                  sharpmz_save_tape_from_cmt(const char *);
sharpmz_tape_header_t *sharpmz_get_tape_header(void);
const char            *sharpmz_get_auto_save_enabled_string(void);
const char            *sharpmz_get_fasttape_string(void);
const char            *sharpmz_get_display_type_string(void);
const char            *sharpmz_get_aspect_ratio_string(void);
const char            *sharpmz_get_scandoubler_fx_string(void);
const char            *sharpmz_get_vram_wait_mode_string(void);
const char            *sharpmz_get_vram_disable_mode_string(void);
const char            *sharpmz_get_gram_disable_mode_string(void);
const char            *sharpmz_get_pcg_mode_string(void);
const char            *sharpmz_get_machine_model_string(void);
const char            *sharpmz_get_cpu_speed_string(void);
const char            *sharpmz_get_audio_source_string(void);
const char            *sharpmz_get_audio_volume_string(void);
const char            *sharpmz_get_audio_mute_string(void);
const char            *sharpmz_get_debug_enable_string(void);
const char            *sharpmz_get_debug_leds_string(void);
const char            *sharpmz_get_debug_leds_bank_string(void);
const char            *sharpmz_get_debug_leds_subbank_string(void);
const char            *sharpmz_get_debug_cpufreq_string(void);
const char            *sharpmz_get_debug_leds_smpfreq_string(void);
const char            *sharpmz_get_machine_model_string(short machineModel);
const char            *sharpmz_get_machine_model_string(void);
const char            *sharpmz_get_user_rom_enabled_string(short);
const char            *sharpmz_get_fdc_rom_enabled_string(short);
const char            *sharpmz_get_custom_rom_enabled_string(short romEnabled);
const char            *sharpmz_get_tape_type_string(void);
const char            *sharpmz_get_tape_buttons_string(void);
const char            *sharpmz_get_memory_bank_string(short);
const char            *sharpmz_get_memory_bank_file(short);
void                   sharpmz_push_filename(char *);
char                  *sharpmz_pop_filename(void);
char                  *sharpmz_get_next_filename(char);
void                   sharpmz_clear_filelist(void);
void                   sharpmz_select_file(const char*, unsigned char, char *, char, char *);
int                    sharpmz_default_ui_state(void);
void sharpmz_ui(int      idleState,    int      idle2State,    int        systemState,    int      selectFile,
                uint32_t *parentstate, uint32_t *menustate,    uint32_t   *menusub,       uint32_t *menusub_last,
				uint64_t *menumask,    char     *selectedPath, const char **helptext,     char     *helptext_custom,
				uint32_t *fs_ExtLen,   uint32_t *fs_Options,   uint32_t   *fs_MenuSelect, uint32_t *fs_MenuCancel,
                char     *fs_pFileExt,
                unsigned char menu,    unsigned char select,   unsigned char up,          unsigned char down,
                unsigned char left,    unsigned char right,    unsigned char plus,        unsigned char minus);
#endif // SHARPMZ_H
