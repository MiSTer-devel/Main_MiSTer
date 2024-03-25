/////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Name:            sharpmz.cpp
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
//                  Dec  2018      - Added additional logic for the MZ80B.
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
#include "stdio.h"
#include "string.h"
#include "malloc.h"
#include <fcntl.h>
#include <sys/stat.h>
#include "../../hardware.h"
#include "../../fpga_io.h"
#include "sharpmz.h"
#include "../../osd.h"
#include "../../menu.h"
#include "../../debug.h"
#include "../../user_io.h"

// Names of the supported machines.
//
static const char *MZMACHINES[MAX_MZMACHINES] = { "MZ80K", "MZ80C", "MZ1200", "MZ80A", "MZ700", "MZ800", "MZ80B", "MZ2000" };
//#define __SHARPMZ_DEBUG__
#if defined __SHARPMZ_DEBUG__
#define sharpmz_debugf(a, ...) printf("\033[1;31mSHARPMZ: " a "\033[0m\n", ##__VA_ARGS__)
#define sharpmz_x_debugf(a, ...) printf("\033[1;32mSHARPMZ: " a "\033[0m\n", ##__VA_ARGS__)
#else
#define sharpmz_debugf(a, ...)
#define sharpmz__x_debugf(a, ...)
#endif

static sharpmz_config_t      config;
static uint8_t               sector_buffer[1024];
static sharpmz_tape_header_t tapeHeader;
static tape_queue_t          tapeQueue;
static unsigned char         debugEnabled = 0;

static uint32_t set_status(uint32_t new_status, uint32_t mask, int ex = 0)
{
	static uint32_t status[2] = { 0, 0 };
	if (ex) ex = 1;

	// if mask is 0 just return the current status
	if (mask) {
		// keep everything not masked
		status[ex] &= ~mask;
		// updated masked bits
		status[ex] |= new_status & mask;

		spi_uio_cmd_cont(UIO_SET_STATUS2);
		spi32_w(status[0]);
		spi32_w(status[1]);
		DisableIO();
	}

	return status[ex];
}

// Method to open a file for writing.
//
int sharpmz_file_write(fileTYPE *file, const char *fileName)
{
    int           ret;
    char          directoryPath[1024];
    char          fullPath[1024];

    strcpy(directoryPath,SHARPMZ_CORE_NAME);
    findPrefixDir(directoryPath, sizeof(directoryPath));



    sprintf(fullPath, "%s/%s", directoryPath, fileName);

    const int mode = O_RDWR | O_CREAT | O_TRUNC | O_SYNC;   // No longer required as FileOpenEx has changed.  | S_IRWXU | S_IRWXG | S_IRWXO;
    ret = FileOpenEx(file, fullPath, mode);
    if (!ret)
    {
        sharpmz_debugf("sharpmz_file_write (FileOpenEx) - File:%s, error: %d.\n", fullPath, ret);
    }

    // Success.
    return(ret);
}

// Method to load a rom into the emulator.
//
void sharpmz_set_rom(romData_t &image)
{
    if(image.romEnabled)
    {
        sharpmz_debugf("Rom enabled: sharpmz_set_rom(%04x, %04x, %s)\n", image.loadAddr, image.loadSize, image.romFileName);
        sharpmz_send_file(image, 0);
    } else
    {
        sharpmz_debugf("Rom not enabled: sharpmz_set_rom(%04x, %04x, %s)\n", image.loadAddr, image.loadSize, image.romFileName);
    }
}

// Save current configuration to SD Card.
//
int sharpmz_save_config(void)
{
    FileSaveConfig(SHARPMZ_CONFIG_FILENAME, &config, sizeof(config));

    // For calls from the UI, return a state to progress on to.
    //
    return(MENU_SHARPMZ_MAIN1);
}

// Method to reset the emulator.
//
void sharpmz_reset(unsigned long preResetSleep, unsigned long postResetSleep)
{
    // Set the reset bit.
    //
    config.system_ctrl |= 1;
	set_status(config.system_ctrl, (1));

    // Sleep and hold device in reset for given period.
    //
    if(preResetSleep > 0)
        usleep(preResetSleep);

    // Remove reset.
    //
    config.system_ctrl &= ~(1);
	set_status(config.system_ctrl, (1));

    // Sleep and hold device in reset for given period.
    //
    if(postResetSleep > 0)
        usleep(postResetSleep);
}

// Reset the configuration to inbuilt defaults.
//
int sharpmz_reset_config(short setStatus)
{
    int i;
    char buf[1024];

    // Setup config defaults.
    //
    config.system_ctrl = 0;

    // Set the configuration registers to a known defualt.
    config.system_reg[REGISTER_MODEL]      = 0x03;              // MZ-80A
    config.system_reg[REGISTER_DISPLAY]    = 0x00;              // Mono 40x25
    config.system_reg[REGISTER_DISPLAY2]   = 0x78 | 0x00;       // GRAM base addr | VGA Mode. // 78 - VGA 7B - 15khz
    config.system_reg[REGISTER_DISPLAY3]   = 0x00;              // Status screen buffer.
    config.system_reg[REGISTER_CPU]        = 0x00;              // CPU speed.
    config.system_reg[REGISTER_AUDIO]      = 0x00;              // Audio - sound output.
    config.system_reg[REGISTER_CMT]        = 0x1b;              // Auto.
    config.system_reg[REGISTER_CMT2]       = 0x00;              // Scratch for APSS
    config.system_reg[REGISTER_USERROM]    = 0x00;              // User ROM disabled.
    config.system_reg[REGISTER_FDCROM]     = 0x00;              // FDC ROM disabled.
    config.system_reg[REGISTER_10]         = 0x00;              // Unused.
    config.system_reg[REGISTER_11]         = 0x00;              // Unused.
    config.system_reg[REGISTER_12]         = 0x00;              // Unused.
    config.system_reg[REGISTER_SETUP]      = 0x00;              // Setup - disable debug.
    config.system_reg[REGISTER_DEBUG]      = 0x00;              // Debug mode.
    config.system_reg[REGISTER_DEBUG2]     = 0x00;              // Debug mode.

    // Set the ROMs to known values.
    for(i=0; i < MAX_MZMACHINES; i++)
    {
        sprintf(buf, "%s_mrom.ROM",       MZMACHINES[i]);
        strcpy(config.romMonitor[i][MONITOR].romFileName, buf);
        config.romMonitor[i][MONITOR].romEnabled     = 0;
        config.romMonitor[i][MONITOR].loadAddr       = MZLOADADDR[MROM_IDX][i];
        config.romMonitor[i][MONITOR].loadSize       = MZLOADSIZE[MROM_IDX][i];
        sprintf(buf, "%s_80c_mrom.ROM",   MZMACHINES[i]);
        strcpy(config.romMonitor[i][MONITOR_80C].romFileName, buf);
        config.romMonitor[i][MONITOR_80C].romEnabled = 0;
        config.romMonitor[i][MONITOR_80C].loadAddr   = MZLOADADDR[MROM_80C_IDX][i];
        config.romMonitor[i][MONITOR_80C].loadSize   = MZLOADSIZE[MROM_80C_IDX][i];
        sprintf(buf, "%s_cgrom.ROM",      MZMACHINES[i]);
        strcpy(config.romCG[i].romFileName, buf);
        config.romCG[i].romEnabled                   = 0;
        config.romCG[i].loadAddr                     = MZLOADADDR[CGROM_IDX][i];
        config.romCG[i].loadSize                     = MZLOADSIZE[CGROM_IDX][i];
        sprintf(buf, "%s_keymap.ROM",     MZMACHINES[i]);
        strcpy(config.romKeyMap[i].romFileName, buf);
        config.romKeyMap[i].romEnabled               = 0;
        config.romKeyMap[i].loadAddr                 = MZLOADADDR[KEYMAP_IDX][i];
        config.romKeyMap[i].loadSize                 = MZLOADSIZE[KEYMAP_IDX][i];
        sprintf(buf, "%s_user.ROM",       MZMACHINES[i]);
        strcpy(config.romUser[i].romFileName, buf);
        config.romUser[i].romEnabled                 = 0;
        config.romUser[i].loadAddr                   = MZLOADADDR[USERROM_IDX][i];
        config.romUser[i].loadSize                   = MZLOADSIZE[USERROM_IDX][i];
        sprintf(buf, "%s_fdc.ROM",        MZMACHINES[i]);
        strcpy(config.romFDC[i].romFileName, buf);
        config.romFDC[i].romEnabled                  = 0;
        config.romFDC[i].loadAddr                    = MZLOADADDR[FDCROM_IDX][i];
        config.romFDC[i].loadSize                    = MZLOADSIZE[FDCROM_IDX][i];
    }

    // Set the status values.
    //
    if(setStatus)
    {
		set_status(config.system_ctrl, 0xffffffff);

        // Set the registers.
        //
        for(int i=0; i < MAX_REGISTERS; i++)
        {
            sharpmz_set_config_register(i, config.system_reg[i]);
        }
    }

    // For calls from the UI, return a state to progress on to.
    //
    return(MENU_SHARPMZ_MAIN1);
}

// Load the configuration from the HPS.
//
int sharpmz_reload_config(short setStatus)
{
    short success = 0;

    // Try to load config from the filesystem.
    //
    int size = FileLoadConfig(SHARPMZ_CONFIG_FILENAME, 0, 0);
    if (size > 0 && size == sizeof(sharpmz_config_t))
    {
        FileLoadConfig(SHARPMZ_CONFIG_FILENAME, &config, sizeof(sharpmz_config_t));
      #if defined __SHARPMZ__DEBUG
        sharpmz_debugf("Loaded config from file:%s.", SHARPMZ_CONFIG_FILENAME);
      #endif
        success = 1;
        sharpmz_debugf("Config loaded successfully.");
    }
    else
    {
        sharpmz_debugf("No %s config found, creating using defaults", SHARPMZ_CONFIG_FILENAME);
        sharpmz_reset_config(0);
        sharpmz_save_config();
    }

    // Set the status values and control registers as requested..
    //
    if(setStatus && success)
    {
		set_status(config.system_ctrl, 0xffffffff);

        // Set the registers.
        //
        for(int i=0; i < MAX_REGISTERS; i++)
        {
            sharpmz_set_config_register(i, config.system_reg[i]);
        }
    }

    // Let caller know if we succeeded to load config.
    //
    return(success);
}

// Initialisation of the machine, upload roms etc.
//
void sharpmz_init(void)
{
    int i;

    sharpmz_debugf("Sharp MZ Series Initialisation");

    // Necessary sleep to allow the reset to complete and the registers to become available.
    //
    usleep(50000);

    // Try and load the SD config.
    //
    if(sharpmz_reload_config(0) == 0)
    {
        // Set configuration to default in case we cannot load the file based configuration.
        //
        sharpmz_reset_config(0);
    }

    // Setup the status values based on the config.
    //
	set_status(config.system_ctrl, 0xffffffff);

    // Set the control registers according to config.
    //
    for(int i=0; i < MAX_REGISTERS; i++)
    {
        sharpmz_set_config_register(i, config.system_reg[i]);
    }

    // Upload defined rom files.
    //
    for(i=0; i < MAX_MZMACHINES; i++)
    {
        // Load given machine rom images at defined addresses.
        //
        sharpmz_set_rom(config.romMonitor[i][0]);
        sharpmz_set_rom(config.romMonitor[i][1]);
        sharpmz_set_rom(config.romCG[i]);
        sharpmz_set_rom(config.romKeyMap[i]);
    }

    // Read (and show) registers.
    for(i=0; i < MAX_REGISTERS; i++)
    {
        unsigned char configReg = sharpmz_read_config_register(i);
        sharpmz_debugf("Register (%02x) = %02x", i, configReg);

        // See if the debug logic has been enabled.
        //
        if(i == REGISTER_SETUP)
        {
            debugEnabled = configReg & 0x1;
        }
    }

    // Initialise tape queue.
    //
    for(int i=0; i < MAX_TAPE_QUEUE; i++)
    {
        tapeQueue.queue[i] = NULL;
    }
    tapeQueue.tapePos      = 0;
    tapeQueue.elements     = 0;
    tapeQueue.fileName[0]  = 0;

    sharpmz_debugf("Initialisation complete.");
}

// Poll handler, perform any periodic tasks via this hook.
//
void sharpmz_poll(void)
{
    // Locals.
    static unsigned long  time = GetTimer(0);
    unsigned long         timeElapsed;
    char                 *fileName;
    unsigned char         regCMT;
    unsigned char         regCMT2;

    // Get elapsed time since last poll.
    //
    timeElapsed = GetTimer(0) - time;

    // Every 2 seconds (READY signal takes 1 second to become active after previous load) to see if the tape buffer is empty.
    //
    if(timeElapsed > 1000)
    {
        // Take snapshot of registers.
        //
        regCMT  = sharpmz_read_config_register(REGISTER_CMT);
        regCMT2 = sharpmz_read_config_register(REGISTER_CMT2);

        sharpmz_debugf("CMT/CMT2 (%s%s%s%s%s%s%s:%s%s%s%s%s).",
                       regCMT & REGISTER_CMT_PLAY_READY   ? "PLAY_READY,"  : "",
                       regCMT & REGISTER_CMT_PLAYING      ? "PLAYING,"     : "",
                       regCMT & REGISTER_CMT_RECORD_READY ? "RECORD_READY,": "",
                       regCMT & REGISTER_CMT_RECORDING    ? "RECORDING,"   : "",
                       regCMT & REGISTER_CMT_ACTIVE       ? "ACTIVE,"      : "",
                       regCMT & REGISTER_CMT_SENSE        ? "SENSE,"       : "",
                       regCMT & REGISTER_CMT_WRITEBIT     ? "WRITEBIT,"    : "",
                       regCMT2 & REGISTER_CMT2_APSS       ? "APSS,"        : "",
                       regCMT2 & REGISTER_CMT2_DIRECTION  ? "DIRECTION,"   : "",
                       regCMT2 & REGISTER_CMT2_EJECT      ? "EJECT,"       : "",
                       regCMT2 & REGISTER_CMT2_PLAY       ? "PLAY,"        : "",
                       regCMT2 & REGISTER_CMT2_STOP       ? "STOP"         : "");

        // MZ80B APSS functionality.
        //
        if(sharpmz_get_machine_group() == 2)
        {
            // If Eject set, clear queue then issue CMT Register Clear.
            //
            if( regCMT2 & REGISTER_CMT2_EJECT )
            {
                sharpmz_debugf("APSS Eject Cassette (%02x:%02x).", regCMT2, REGISTER_CMT2_EJECT);
                sharpmz_clear_filelist();
            } else

            // If APSS set, rotate queue forward (DIRECTION = 1) or backward (DIRECTION = 0).
            //
            if( regCMT2 & REGISTER_CMT2_APSS )
            {
                sharpmz_debugf("APSS Search %s (%02x:%02x).", regCMT2 & REGISTER_CMT2_DIRECTION ? "Forward" : "Reverse", regCMT2, REGISTER_CMT2_APSS );
                sharpmz_apss_search(regCMT2 & REGISTER_CMT2_DIRECTION ? 1 : 0);
            }

            // If Play is active, the cache is empty and we are not recording, load into cache the next tape image.
            //
            if( (regCMT2 & REGISTER_CMT2_PLAY) && !(regCMT & REGISTER_CMT_PLAY_READY) && !(regCMT & REGISTER_CMT_RECORDING) )
            {
                // Check the tape queue, if items available, read oldest,upload and rotate.
                //
                if(tapeQueue.elements > 0)
                {
                    fileName = sharpmz_apss_search(1);

                    if(fileName != 0)
                    {
                        sharpmz_debugf("APSS Play %s, Rotate Queue Forward.", fileName);
                    }

                    if(fileName != 0)
                    {
                        sharpmz_debugf("Loading tape: %s\n", fileName);
                        sharpmz_load_tape_to_ram(fileName, 1);
                    }
                }
            }
        } else
        {
            // Check to see if the Tape READY signal is inactive, if it is and we have items in the queue, load up the next
            // tape file and send it.
            //
            if( (regCMT & REGISTER_CMT_SENSE) && !(regCMT & REGISTER_CMT_PLAY_READY) )
            {
                // Check the tape queue, if items available, pop oldest and upload.
                //
                if(tapeQueue.elements > 0)
                {
                    fileName = sharpmz_pop_filename();

                    if(fileName != 0)
                    {
                        sharpmz_debugf("Loading tape: %s\n", fileName);
                        sharpmz_load_tape_to_ram(fileName, 1);
                    }
                }
            }
        }

        // Check to see if the RECORD_READY flag is set.
        if( (regCMT & REGISTER_CMT_RECORD_READY) )
        {
            sharpmz_save_tape_from_cmt((const char *)0);
        }

        // Reset the timer.
        time = GetTimer(0);
    }
}

// Method to push a tape filename onto the queue.
//
void sharpmz_push_filename(char *fileName)
{
    // Locals.
    char *ptr = (char *)malloc(strlen(fileName)+1);

    if(tapeQueue.elements > MAX_TAPE_QUEUE)
    {
        free(ptr);
    } else
    {
        // Copy filename into queue.
        strcpy(ptr, fileName);
        tapeQueue.queue[tapeQueue.elements] = ptr;
        tapeQueue.elements++;
    }

    return;
}

// Method to read the oldest tape filename entered and return it.
//
char *sharpmz_pop_filename(void)
{
    // Pop the bottom most item and shift queue down.
    //
    tapeQueue.fileName[0] = 0;
    if(tapeQueue.elements > 0)
    {
        strcpy(tapeQueue.fileName, tapeQueue.queue[0]);
        free(tapeQueue.queue[0]);
        tapeQueue.elements--;
        for(int i= 1; i < MAX_TAPE_QUEUE; i++)
        {
            tapeQueue.queue[i-1] = tapeQueue.queue[i];
        }
        tapeQueue.queue[MAX_TAPE_QUEUE-1] = NULL;

    }

    // Return filename.
    return(tapeQueue.fileName[0] == 0 ? 0 : tapeQueue.fileName);
}


// Method to virtualise a tape and shift the position up and down the queue according to actions given.
// direction: 0 = rotate left (Rew), 1 = rotate right (Fwd)
//
char *sharpmz_apss_search(char direction)
{
    tapeQueue.fileName[0] = 0;
    if(tapeQueue.elements > 0)
    {
        if(direction == 0)
        {
            // Position is ahead of last, then shift down and return file.
            //
            if(tapeQueue.tapePos > 0)
            {
                tapeQueue.tapePos--;
                strcpy(tapeQueue.fileName, tapeQueue.queue[tapeQueue.tapePos]);
            }

        } else
        {
            // Position is below max, then return current and forward.
            //
            if(tapeQueue.tapePos < MAX_TAPE_QUEUE && tapeQueue.tapePos < tapeQueue.elements)
            {
                strcpy(tapeQueue.fileName, tapeQueue.queue[tapeQueue.tapePos]);
                tapeQueue.tapePos++;
            }
        }
    }

    // Return filename.
    return(tapeQueue.fileName[0] == 0 ? 0 : tapeQueue.fileName);
}


// Method to iterate through the list of filenames.
//
char *sharpmz_get_next_filename(char reset)
{
    static unsigned short  pos = 0;

    // Reset is active, start at beginning of list.
    //
    if(reset)
    {
        pos = 0;
    }
    tapeQueue.fileName[0] = 0;

    // If we reach the queue limit or the max elements stored, cycle the pointer
    // round to the beginning.
    //
    if(pos >= MAX_TAPE_QUEUE || pos >= tapeQueue.elements)
    {
        pos = 0;
    } else

    // Get the next element in the queue, if available.
    //
    if(tapeQueue.elements > 0)
    {
        if(pos < MAX_TAPE_QUEUE && pos < tapeQueue.elements)
        {
            strcpy(tapeQueue.fileName, tapeQueue.queue[pos++]);
        }
    }

    // Return filename if available.
    //
    return(tapeQueue.fileName[0] == 0 ? 0 : rindex(tapeQueue.fileName, '/')+1);
}

// Method to clear the queued tape list.
//
void sharpmz_clear_filelist(void)
{
    // Locals.

    if(tapeQueue.elements > 0)
    {
        for(int i=0; i < MAX_TAPE_QUEUE; i++)
        {
            if(tapeQueue.queue[i] != NULL)
            {
                free(tapeQueue.queue[i]);
            }
            tapeQueue.queue[i] = NULL;
        }
    }
    tapeQueue.elements    = 0;
    tapeQueue.tapePos     = 0;
    tapeQueue.fileName[0] = 0;

    sharpmz_debugf("Cleared Tape Queue.");
}

// Return fast tape status bits. Bit 0,1 of system_reg, 0 = Off, 1 = 2x, 2 = 4x, 3 = 16x
//
int sharpmz_get_fasttape(void)
{
    return (config.system_reg[REGISTER_CMT] & 0x00000007);
}

// Get the group to which the current machine belongs:
// 0 - MZ80K/C/A type
// 1 - MZ700 type
// 2 - MZ80B/2000 type
//
short sharpmz_get_machine_group(void)
{
    short machineModel = config.system_reg[REGISTER_MODEL] & 0x07;
    short machineGroup = 0;

    // Set value according to machine model.
    //
    switch(machineModel)
    {
        // These machines currently underdevelopment, so fall through to MZ80K
        case MZ80B_IDX:  // MZ80B
        case MZ2000_IDX: // MZ2000
            machineGroup = 2;
            break;

        case MZ80K_IDX:  // MZ80K
        case MZ80C_IDX:  // MZ80C
        case MZ1200_IDX: // MZ1200
        case MZ80A_IDX:  // MZ80A
            machineGroup = 0;
            break;

        case MZ700_IDX:  // MZ700
        case MZ800_IDX:  // MZ800
            machineGroup = 1;
            break;

        default:
            machineGroup = 0;
            break;
    }

    return(machineGroup);
}

// Return string showing Fast Tape mode.
//
const char *sharpmz_get_fasttape_string(void)
{
    short fastTape = (config.system_reg[REGISTER_CMT]) & 0x00000007;

    return(SHARPMZ_FAST_TAPE[ (sharpmz_get_machine_group() * 8)  + fastTape ]);
}

// Set fast tape status bits to given value.
//
void sharpmz_set_fasttape(short mode, short setStatus)
{
    short machineGroup = sharpmz_get_machine_group();

    // Clear out current setting.
    config.system_reg[REGISTER_CMT] &= ~0x07;

    if((machineGroup == 0 && mode > 5) || (machineGroup == 1 && mode > 5) || (machineGroup == 2 && mode > 4)) mode = 0;
    config.system_reg[REGISTER_CMT] |= (mode & 0x07);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_CMT, config.system_reg[REGISTER_CMT]);
}


// Return Ascii Mapping of header filename bits. Bit 0,1 of system_reg, 0 = Off, 1 = FROMMZ, 2 = TOMZ, 3 = BOTH
//
int sharpmz_get_cmt_ascii_mapping(void)
{
    return ((config.system_reg[REGISTER_CMT] >> 5) & 0x00000003 );
}

// Return string showing ascii mapping setting.
//
const char *sharpmz_get_cmt_ascii_mapping_string(void)
{
    short mapping = (config.system_reg[REGISTER_CMT] >> 5) & 0x00000003;

    return(SHARPMZ_ASCII_MAPPING[ mapping ]);
}

// Set ascii mapping status bits to given value.
//
void sharpmz_set_cmt_ascii_mapping(short map, short setStatus)
{
    config.system_reg[REGISTER_CMT] &= ~(3 << 5);
    config.system_reg[REGISTER_CMT] |= (map & 0x03) << 5;

    if(setStatus)
        sharpmz_set_config_register(REGISTER_CMT, config.system_reg[REGISTER_CMT]);
}

// Return tape buttons status bits. Bit 2, 3 of system_reg, 0 = Off, 1 = Play, 2 = Record, 3 = Auto.
//
int sharpmz_get_tape_buttons(void)
{
    return ((config.system_reg[REGISTER_CMT] >> 3) & 0x00000003 );
}

// Return string showing tape buttons setting.
//
const char *sharpmz_get_tape_buttons_string(void)
{
    short tapeSW = (config.system_reg[REGISTER_CMT] >> 3) & 0x00000003;

    return(SHARPMZ_TAPE_BUTTONS[ tapeSW ]);
}

// Set tape buttons status bits to given value.
//
void sharpmz_set_tape_buttons(short mode, short setStatus)
{
    config.system_reg[REGISTER_CMT] &= ~(3 << 3);
    config.system_reg[REGISTER_CMT] |= (mode & 0x03) << 3;

    if(setStatus)
        sharpmz_set_config_register(REGISTER_CMT, config.system_reg[REGISTER_CMT]);
}

// Method to return the next memory bank name string in a loop sequence.
//
short sharpmz_get_next_memory_bank(void)
{
    static short memoryBank = SHARPMZ_MEMBANK_SYSROM;

    if(memoryBank == 1) memoryBank = SHARPMZ_MEMBANK_SYSRAM;                          // SYSROM spans 2 physical banks.
    if(memoryBank >= SHARPMZ_MEMBANK_MAXBANKS) memoryBank = SHARPMZ_MEMBANK_SYSROM;   // Loop round when we get to the end.
    return (memoryBank++);
}

// Return string showing memory bank name.
//
const char *sharpmz_get_memory_bank_string(short memoryBank)
{
    return(SHARPMZ_MEMORY_BANK[ memoryBank ]);
}

// Return string showing memory bank dump file name.
//
const char *sharpmz_get_memory_bank_file(short memoryBank)
{
    return(SHARPMZ_MEMORY_BANK_FILE[ memoryBank ]);
}

// Return display type status bits. Bit 0, 1, 2 of system_reg.
//
int sharpmz_get_display_type(void)
{
    return (config.system_reg[REGISTER_DISPLAY] & 0x00000007 );
}

// Return string showing Display Type.
//
const char *sharpmz_get_display_type_string(void)
{
    short displayType = (config.system_reg[REGISTER_DISPLAY]) & 0x00000007;

    return(SHARPMZ_DISPLAY_TYPE[ displayType ]);
}

// Set display type status bits to given value.
//
void sharpmz_set_display_type(short displayType, short setStatus)
{
    // Sanity check.
    if(displayType > 3)  displayType = 0;
    //if(displayType == 1) displayType = 3; // Skip unassigned hardware.
    //if(displayType == 2) displayType = 3;

    config.system_reg[REGISTER_DISPLAY] &= ~(0x07);
    config.system_reg[REGISTER_DISPLAY] |= (displayType & 0x07);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DISPLAY, config.system_reg[REGISTER_DISPLAY]);
}

// Return aspect ratio status bits. Bit 1 of systemctrl, 0 = Off, 1 = On
//
int sharpmz_get_aspect_ratio(void)
{
    return ((config.system_ctrl & 0x00000002) != 0);
}

// Return string showing Aspect Ratio.
//
const char *sharpmz_get_aspect_ratio_string(void)
{
    short aspectRatio = (config.system_ctrl & 0x00000002);

    return(SHARPMZ_ASPECT_RATIO[ aspectRatio ]);
}

// Set aspect ratio status bit to given value.
//
void sharpmz_set_aspect_ratio(short on, short setStatus)
{
    config.system_ctrl &= ~(1 << 1);
    config.system_ctrl |= (on == 1 ? 1 << 1 : 0);

    if(setStatus)
		set_status(config.system_ctrl, (1 << 1));
}

// Return Scan doubler fx status bits. Bits 2,3,4 of systemctrl.
//
int sharpmz_get_scandoubler_fx(void)
{
    return ((config.system_ctrl >> 2) & 0x00000007 );
}

// Return string showing Scandoubler Fx mode.
//
const char *sharpmz_get_scandoubler_fx_string(void)
{
    short scandoublerFx = ((config.system_ctrl >> 2) & 0x00000007 );

    return(SHARPMZ_SCANDOUBLER_FX[ scandoublerFx ]);
}

// Set scan doubler status bits to given value.
//
void sharpmz_set_scandoubler_fx(short doubler, short setStatus)
{
    // Sanity check.
    if(doubler > 4) doubler = 0;

    config.system_ctrl &= ~(0x07 << 2);
    config.system_ctrl |= (doubler & 0x07) << 2;

    if(setStatus)
		set_status(config.system_ctrl, (0x07 << 2));
}

// Return VRAM wait state mode status bit. Bit 6 of system_reg, 0 = Off, 1 = On
//
int sharpmz_get_vram_wait_mode(void)
{
    return ((config.system_reg[REGISTER_DISPLAY] >> 6) & 0x00000001 );
}

// Return string showing VRAM wait state mode.
//
const char *sharpmz_get_vram_wait_mode_string(void)
{
    short vramWaitMode = ((config.system_reg[REGISTER_DISPLAY] >> 6) & 0x00000001 );

    return(SHARPMZ_VRAMWAIT_MODE[ vramWaitMode ]);
}

// Set VRAM wait state mode status bit to given value.
//
void sharpmz_set_vram_wait_mode(short on, short setStatus)
{
    config.system_reg[REGISTER_DISPLAY] &= ~(1 << 6);
    config.system_reg[REGISTER_DISPLAY] |= (on == 1 ? 1 << 6 : 0);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DISPLAY, config.system_reg[REGISTER_DISPLAY]);
}

// Return VRAM Output Disable bit. Bit 4 of system_reg, 0 = Output Enabled, 1 = Output Disabled
//
int sharpmz_get_vram_disable_mode(void)
{
    return ((config.system_reg[REGISTER_DISPLAY] >> 4) & 0x00000001 );
}

// Return string showing VRAM Output Disable mode.
//
const char *sharpmz_get_vram_disable_mode_string(void)
{
    short vramDisableMode = ((config.system_reg[REGISTER_DISPLAY] >> 4) & 0x00000001 );

    return(SHARPMZ_VRAMDISABLE_MODE[ vramDisableMode ]);
}

// Set VRAM Output Disable mode bit to given value.
//
void sharpmz_set_vram_disable_mode(short on, short setStatus)
{
    config.system_reg[REGISTER_DISPLAY] &= ~(1 << 4);
    config.system_reg[REGISTER_DISPLAY] |= (on == 1 ? 1 << 4 : 0);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DISPLAY, config.system_reg[REGISTER_DISPLAY]);
}

// Return GRAM IO Base Address. Bits 7 .. 3 represent the MSB of the IO address for the framebuffer control registers.
//
int sharpmz_get_gram_base_addr(void)
{
    return ((config.system_reg[REGISTER_DISPLAY2] >> 3) & 0x1f);
}

// Return string showing GRAM IO Base Address mode.
//
const char *sharpmz_get_gram_base_addr_string(void)
{
    short gramBaseAddr = ((config.system_reg[REGISTER_DISPLAY2] >> 3) & 0x1f );

    return(SHARPMZ_GRAM_BASEADDR[ gramBaseAddr ]);
}

// Set GRAM IO Base Address to given value.
//
void sharpmz_set_gram_base_addr(short addr, short setStatus)
{
    config.system_reg[REGISTER_DISPLAY2] &= ~(0x1f << 3);
    config.system_reg[REGISTER_DISPLAY2] |= addr << 3;

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DISPLAY, config.system_reg[REGISTER_DISPLAY]);
}

// Return GRAM Output Disable bit. Bit 5 of system_reg, 0 = Output Enabled, 1 = Output Disabled
//
int sharpmz_get_gram_disable_mode(void)
{
    return ((config.system_reg[REGISTER_DISPLAY] >> 5) & 0x00000001 );
}

// Return string showing GRAM Output Disable mode.
//
const char *sharpmz_get_gram_disable_mode_string(void)
{
    short gramDisableMode = ((config.system_reg[REGISTER_DISPLAY] >> 5) & 0x00000001 );

    return(SHARPMZ_GRAMDISABLE_MODE[ gramDisableMode ]);
}

// Set GRAM Output Disable mode bit to given value.
//
void sharpmz_set_gram_disable_mode(short on, short setStatus)
{
    config.system_reg[REGISTER_DISPLAY] &= ~(1 << 5);
    config.system_reg[REGISTER_DISPLAY] |= (on == 1 ? 1 << 5 : 0);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DISPLAY, config.system_reg[REGISTER_DISPLAY]);
}


// Return PCG mode status bit. Bit 7 of system_reg, 0 = Off, 1 = On
//
int sharpmz_get_pcg_mode(void)
{
    return ((config.system_reg[REGISTER_DISPLAY] >> 7) & 0x00000001 );
}

// Return string showing PCG mode.
//
const char *sharpmz_get_pcg_mode_string(void)
{
    short pcgMode = ((config.system_reg[REGISTER_DISPLAY] >> 7) & 0x00000001 );

    return(SHARPMZ_PCG_MODE[ pcgMode ]);
}

// Set PCG mode status bit to given value.
//
void sharpmz_set_pcg_mode(short on, short setStatus)
{
    config.system_reg[REGISTER_DISPLAY] &= ~(1 << 7);
    config.system_reg[REGISTER_DISPLAY] |= (on == 1 ? 1 << 7 : 0);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DISPLAY, config.system_reg[REGISTER_DISPLAY]);
}

// Return VGA mode status bit. Bit 0 of register DISPLAY2, 0 = Off, 1 = 640x480, 2 = 640x400, 3 - 640x480
//
int sharpmz_get_vga_mode(void)
{
    short vgaMode = config.system_reg[REGISTER_DISPLAY2] & 0x00000003;
    return (vgaMode);
}

// Return string showing VGA mode.
//
const char *sharpmz_get_vga_mode_string(void)
{
    short vgaMode = config.system_reg[REGISTER_DISPLAY2] & 0x00000003;

    return(SHARPMZ_VGA_MODE[ vgaMode ]);
}

// Set VGA mode status bit to given value.
//
void sharpmz_set_vga_mode(short mode, short setStatus)
{
    config.system_reg[REGISTER_DISPLAY2] &= ~(3);
    if(mode == 1 || mode == 2) mode = 3;
    config.system_reg[REGISTER_DISPLAY2] |= (mode & 0x03);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DISPLAY2, config.system_reg[REGISTER_DISPLAY2]);
}

// Return machine model status bits. Bits 0,1,2 of system_reg.
//
int sharpmz_get_machine_model(void)
{
    return (config.system_reg[REGISTER_MODEL] & 0x07);
}

// Return string showing Machine model.
//
const char *sharpmz_get_machine_model_string(short machineModel )
{
    return(SHARPMZ_MACHINE_MODEL[ machineModel & 0x07 ]);
}

// Return string showing Machine model.
//
const char *sharpmz_get_machine_model_string(void)
{
    short machineModel = (config.system_reg[REGISTER_MODEL]) & 0x07;

    return(SHARPMZ_MACHINE_MODEL[ machineModel ]);
}

// Method to return the next machine model index in a loop sequence.
//
short sharpmz_get_next_machine_model(void)
{
    static short machineModel = (config.system_reg[REGISTER_MODEL]) & 0x07;

    // Certain models not yet active.
    if(machineModel == MZ800_IDX) machineModel = MZ80B_IDX;
    return (machineModel++);
}

// Set machine model status bits to given value.
//
void sharpmz_set_machine_model(short machineModel, short setStatus)
{
    // Sanity.
    //
    machineModel &= 0x07;

    // Certain models not yet active.
    if(machineModel == MZ800_IDX) machineModel = MZ80B_IDX;

    // When setting the model, default other settings to sensible values.
    //
    switch(machineModel)
    {
        case MZ80B_IDX:  // MZ80B
        case MZ2000_IDX: // MZ2000
            sharpmz_set_display_type(0x00, 0);  // Normal display
            sharpmz_set_cpu_speed(0x00, 1);     // 2MHz MZ80C, 3.5MHz MZ700, 4MHz MZ80B
            break;

        // This machine is yet to be developed.
        case MZ800_IDX:  // MZ800
            break;

        case MZ80K_IDX:  // MZ80K
        case MZ80C_IDX:  // MZ80C
        case MZ1200_IDX: // MZ1200
        case MZ80A_IDX:  // MZ80A
            //sharpmz_set_fasttape(0x00, 1);      // No.
            sharpmz_set_display_type(0x00, 0);  // Normal display
            //sharpmz_set_pcg_mode(0x00, 1);      // ROM
            //sharpmz_set_scandoubler_fx(0x00, 1);// None.
            sharpmz_set_cpu_speed(0x00, 1);     // 2MHz MZ80C, 3.5MHz MZ700, 4MHz MZ80B
            break;

        case MZ700_IDX:  // MZ700
            //sharpmz_set_fasttape(0x00, 1);      // No.
            sharpmz_set_display_type(0x02, 0);  // Colour display
            //sharpmz_set_pcg_mode(0x00, 1);      // ROM sharpmz_set_scandoubler_fx(0x00, 0);// None.
            //sharpmz_set_scandoubler_fx(0x00, 1);// None.
            sharpmz_set_cpu_speed(0x00, 0);     // 2MHz MZ80C, 3.5MHz MZ700, 4MHz MZ80B
            break;
    }

    // Set the machine model.
    config.system_reg[REGISTER_MODEL] &= ~(0x07);
    config.system_reg[REGISTER_MODEL] |= (machineModel);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_MODEL, config.system_reg[REGISTER_MODEL]);
}

// Return string showing CPU speed for display.
//
const char *sharpmz_get_cpu_speed_string(void)
{
    short cpuSet = (config.system_reg[REGISTER_CPU]) & 0x00000007;

    return(SHARPMZ_CPU_SPEED[ (sharpmz_get_machine_group() * 8)  + cpuSet ]);
}

// Return cpu speed status bits. Bits 0, 1 of system_reg.
//
int sharpmz_get_cpu_speed(void)
{
    return ((config.system_reg[REGISTER_CPU]) & 0x00000007);
}

// Set cpu speed status bits to given value.
//
void sharpmz_set_cpu_speed(short cpuSpeed, short setStatus)
{
    short machineGroup = sharpmz_get_machine_group();

    // Clear current setting.
    config.system_reg[REGISTER_CPU] &= ~(0x07);

    if((machineGroup == 0 && cpuSpeed > 5) || (machineGroup == 1 && cpuSpeed > 4) || (machineGroup == 2 && cpuSpeed > 4)) cpuSpeed = 0;
    config.system_reg[REGISTER_CPU] |= (cpuSpeed & 0x07);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_CPU, config.system_reg[REGISTER_CPU]);
}

// Return audio source status bit. Bit 0 of system_reg, 0 = sound, 1 = tape
//
int sharpmz_get_audio_source(void)
{
    return ((config.system_reg[REGISTER_AUDIO]) & 0x00000001);
}

// Return string showing Audio source.
//
const char *sharpmz_get_audio_source_string(void)
{
    short audioSource = ((config.system_reg[REGISTER_AUDIO]) & 0x00000001);

    return(SHARPMZ_AUDIO_SOURCE[ audioSource ]);
}

// Set audio source status bit to given value.
//
void sharpmz_set_audio_source(short on, short setStatus)
{
    config.system_reg[REGISTER_AUDIO] &= ~(1);
    config.system_reg[REGISTER_AUDIO] |= (on == 1 ? 1 : 0);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_AUDIO, config.system_reg[REGISTER_AUDIO]);
}

// Set BOOT_RESET, ie. initiate an IPL boot.
//
void sharpmz_set_boot_reset(void)
{
    unsigned char configReg = sharpmz_read_config_register(REGISTER_CPU);

    sharpmz_debugf("Boot Reset(init) : %02x\n", configReg);
    configReg &= ~(1 << 7);
    configReg |= 1 << 7;
    sharpmz_debugf("Boot Reset(set) : %02x\n", configReg);
    sharpmz_set_config_register(REGISTER_CPU, configReg);
    usleep(50000);
    configReg &= ~(1 << 7);
    sharpmz_debugf("Boot Reset(reset) : %02x\n", configReg);
    sharpmz_set_config_register(REGISTER_CPU, configReg);
}

#ifdef __SHARPMZ_DEBUG__
// Return debug enable status bit. Bit 7 of system_reg, 0 = Off, 1 = On
//
int sharpmz_get_debug_enable(void)
{
    return ((config.system_reg[REGISTER_DEBUG] >> 7) & 0x00000001);
}

// Return string showing Debug Enable mode.
//
const char *sharpmz_get_debug_enable_string(void)
{
    short debugEnable = ((config.system_reg[REGISTER_DEBUG] >> 7) & 0x00000001);

    return(SHARPMZ_DEBUG_ENABLE[ debugEnable ]);
}

// Set debug enable status bit to given value.
//
void sharpmz_set_debug_enable(short on, short setStatus)
{
    config.system_reg[REGISTER_DEBUG] &= ~(1 << 7);
    config.system_reg[REGISTER_DEBUG] |= (on == 1 ? 1 << 7 : 0);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DEBUG, config.system_reg[REGISTER_DEBUG]);
}

// Return debug leds status bit. Bit 6 of system_reg, 0 = Off, 1 = On
//
int sharpmz_get_debug_leds(void)
{
    return ((config.system_reg[REGISTER_DEBUG] >> 6) & 0x00000001);
}

// Return string showing Debug Leds.
//
const char *sharpmz_get_debug_leds_string(void)
{
    short debugLeds = ((config.system_reg[REGISTER_DEBUG] >> 6) & 0x00000001);

    return(SHARPMZ_DEBUG_LEDS[ debugLeds ]);
}

// Set debug leds status bit to given value.
//
void sharpmz_set_debug_leds(short on, short setStatus)
{
    config.system_reg[REGISTER_DEBUG] &= ~(1 << 6);
    config.system_reg[REGISTER_DEBUG] |= (on == 1 ? 1 << 6 : 0);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DEBUG, config.system_reg[REGISTER_DEBUG]);
}

// Method to return the next debug bank index in a loop sequence.
//
short sharpmz_get_next_debug_leds_bank(void)
{
    static short debugLedsBank = (config.system_reg[REGISTER_DEBUG] & 0x00000007);

    // Certain models not yet active.
    if(strcmp(SHARPMZ_DEBUG_LEDS_BANK[ debugLedsBank ], "") == 0) debugLedsBank = 0;
    if(debugLedsBank >  7) debugLedsBank = 0;
    return (debugLedsBank++);
}

// Return string showing debug led bank.
//
const char *sharpmz_get_debug_leds_bank_string(void)
{
    short debugLedsBank = (config.system_reg[REGISTER_DEBUG] & 0x00000007);

    return(SHARPMZ_DEBUG_LEDS_BANK[ debugLedsBank ]);
}

// Set debug LEDS bank index.
//
void sharpmz_set_debug_leds_bank(short debugLedsBank, short setStatus)
{
    // Sanity check.
    if(debugLedsBank >  7) debugLedsBank = 0;

    config.system_reg[REGISTER_DEBUG] &= ~(0x07);
    config.system_reg[REGISTER_DEBUG] |= (debugLedsBank & 0x07);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DEBUG, config.system_reg[REGISTER_DEBUG]);
}

// Method to return the next debug sub-bank index in a loop sequence.
//
short sharpmz_get_next_debug_leds_subbank(unsigned char reset)
{
    static short debugLedsSubBank = ((config.system_reg[REGISTER_DEBUG] >> 3) & 0x00000007);
    short        debugLedsBank = (config.system_reg[REGISTER_DEBUG] & 0x00000007);

    // Limit choice according to number of subbanks and subbanks setup.
    if(reset || debugLedsSubBank > 7) debugLedsSubBank = 0;
    if(strcmp(SHARPMZ_DEBUG_LEDS_SUBBANK[ (debugLedsBank * 8) + debugLedsSubBank ], "") == 0) debugLedsSubBank = 0;
    return (debugLedsSubBank++);
}

// Return string showing debug led sub-bank.
//
const char *sharpmz_get_debug_leds_subbank_string(void)
{
    short debugLedsBank    = (config.system_reg[REGISTER_DEBUG] & 0x00000007);
    short debugLedsSubBank = ((config.system_reg[REGISTER_DEBUG] >> 3) & 0x00000007);

    return(SHARPMZ_DEBUG_LEDS_SUBBANK[ (debugLedsBank * 8) + debugLedsSubBank ]);
}

// Set debug LEDS sub-bank index.
//
void sharpmz_set_debug_leds_subbank(short debugLedsSubBank, short setStatus)
{
    // Sanity check.
    if(debugLedsSubBank > 7) debugLedsSubBank = 0;

    config.system_reg[REGISTER_DEBUG] &= ~(0x07 << 3);
    config.system_reg[REGISTER_DEBUG] |= ((debugLedsSubBank & 0x07) << 3);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DEBUG, config.system_reg[REGISTER_DEBUG]);
}

// Method to return the next debug cpu frequency index in a loop sequence.
//
short sharpmz_get_next_debug_cpufreq(void)
{
    static short debugCPUFrequency = ((config.system_reg[REGISTER_DEBUG2] >> 4) & 0x0000000f);

    // Sanity check.
    if(debugCPUFrequency > 15) debugCPUFrequency = 0;

    return (debugCPUFrequency++);
}

// Return string showing debug cpu frequency.
//
const char *sharpmz_get_debug_cpufreq_string(void)
{
    short debugCPUFrequency = ((config.system_reg[REGISTER_DEBUG2] >> 4) & 0x0000000f);

    return(SHARPMZ_DEBUG_CPUFREQ[ debugCPUFrequency ]);
}

// Set debug cpu frequency index.
//
void sharpmz_set_debug_cpufreq(short debugCPUFrequency, short setStatus)
{
    // Sanity check.
    if(debugCPUFrequency > 15) debugCPUFrequency = 0;

    config.system_reg[REGISTER_DEBUG2] &= ~(0x0f << 4);
    config.system_reg[REGISTER_DEBUG2] |= ((debugCPUFrequency & 0x0f) << 4);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DEBUG2, config.system_reg[REGISTER_DEBUG2]);
}

// Method to return the next debug sample frequency index in a loop sequence.
//
short sharpmz_get_next_debug_leds_smpfreq(void)
{
    static short debugLedsSampleFrequency = (config.system_reg[REGISTER_DEBUG2] & 0x0000000f);

    // Sanity check.
    if(debugLedsSampleFrequency > 15) debugLedsSampleFrequency = 0;

    return (debugLedsSampleFrequency++);
}

// Return string showing debug led sample frequency.
//
const char *sharpmz_get_debug_leds_smpfreq_string(void)
{
    short debugLedsSampleFrequency = (config.system_reg[REGISTER_DEBUG2] & 0x0000000f);

    return(SHARPMZ_DEBUG_LEDS_SMPFREQ[ debugLedsSampleFrequency ]);
}

// Set debug LEDS sample frequency index.
//
void sharpmz_set_debug_leds_smpfreq(short debugLedsSampleFrequency, short setStatus)
{
    // Sanity check.
    if(debugLedsSampleFrequency > 15) debugLedsSampleFrequency = 0;

    config.system_reg[REGISTER_DEBUG2] &= ~(0x0f);
    config.system_reg[REGISTER_DEBUG2] |= (debugLedsSampleFrequency & 0x0f);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_DEBUG2, config.system_reg[REGISTER_DEBUG2]);
}
#endif

// Method to return the enabled state of a user rom for given machine.
//
short sharpmz_get_user_rom_enabled(short machineModel)
{
    return ((config.system_reg[REGISTER_USERROM] >> machineModel) & 0x00000001);
}

// Return string showing user rom enabled status.
//
const char *sharpmz_get_user_rom_enabled_string(short machineModel)
{
    short userRomEnabled = ((config.system_reg[REGISTER_USERROM] >> machineModel) & 0x00000001);

    return(SHARPMZ_USERROM_ENABLED[ userRomEnabled ]);
}

// Method to set the enabled state of a user rom for a given machine.
//
void sharpmz_set_user_rom_enabled(short machineModel, short on, short setStatus)
{
    config.system_reg[REGISTER_USERROM] &= ~(1 << machineModel);
    config.system_reg[REGISTER_USERROM] |= (on == 1 ? 1 << machineModel : 0);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_USERROM, config.system_reg[REGISTER_USERROM]);
}

// Method to return the enabled state of a fdc rom for given machine.
//
short sharpmz_get_fdc_rom_enabled(short machineModel)
{
    return ((config.system_reg[REGISTER_FDCROM] >> machineModel) & 0x00000001);
}

// Return string showing fdc rom enabled status.
//
const char *sharpmz_get_fdc_rom_enabled_string(short machineModel)
{
    short fdcRomEnabled = ((config.system_reg[REGISTER_FDCROM] >> machineModel) & 0x00000001);

    return(SHARPMZ_FDCROM_ENABLED[ fdcRomEnabled ]);
}

// Method to set the enabled state of a fdc rom for a given machine.
//
void sharpmz_set_fdc_rom_enabled(short machineModel, short on, short setStatus)
{
    config.system_reg[REGISTER_FDCROM] &= ~(1 << machineModel);
    config.system_reg[REGISTER_FDCROM] |= (on == 1 ? 1 << machineModel : 0);

    if(setStatus)
        sharpmz_set_config_register(REGISTER_FDCROM, config.system_reg[REGISTER_FDCROM]);
}

// Method to return the enabled state of a rom for loading.
//
short sharpmz_get_custom_rom_enabled(short machineModel, short romType)
{
    short romEnabled = 0;

    machineModel &= 0x07;
    romType      &= 0x07;
    switch(romType)
    {
        case MROM_IDX:
            romEnabled = config.romMonitor[machineModel][MONITOR].romEnabled;
            break;

        case MROM_80C_IDX:
            romEnabled = config.romMonitor[machineModel][MONITOR_80C].romEnabled;
            break;

        case CGROM_IDX:
            romEnabled = config.romCG[machineModel].romEnabled;
            break;

        case KEYMAP_IDX:
            romEnabled = config.romKeyMap[machineModel].romEnabled;
            break;

        case USERROM_IDX:
            romEnabled = config.romUser[machineModel].romEnabled;
            break;

        case FDCROM_IDX:
            romEnabled = config.romFDC[machineModel].romEnabled;
            break;

        default:
            sharpmz_debugf("Unknown get enabled romType: %d\n", romType);
            break;
    }
    return(romEnabled);
}

// Return string showing rom enabled mode.
//
const char *sharpmz_get_custom_rom_enabled_string(short romEnabled)
{
    return(SHARPMZ_ROM_ENABLED[ romEnabled ]);
}

// Method to set the enabled state of a rom for loading.
//
void sharpmz_set_custom_rom_enabled(short machineModel, short romType, short on)
{
    machineModel &= 0x07;
    romType      &= 0x07;
    switch(romType)
    {
        case MROM_IDX:
            config.romMonitor[machineModel][MONITOR].romEnabled = (on == 1);
            break;

        case MROM_80C_IDX:
            config.romMonitor[machineModel][MONITOR_80C].romEnabled = (on == 1);
            break;

        case CGROM_IDX:
            config.romCG[machineModel].romEnabled = (on == 1);
            break;

        case KEYMAP_IDX:
            config.romKeyMap[machineModel].romEnabled = (on == 1);
            break;

        case USERROM_IDX:
            config.romUser[machineModel].romEnabled = (on == 1);
            break;

        case FDCROM_IDX:
            config.romFDC[machineModel].romEnabled = (on == 1);
            break;

        default:
            sharpmz_debugf("Unknown set enabled romType: %d\n", romType);
            break;
    }
}

// Method to return the filename of a rom.
//
char *sharpmz_get_rom_file(short machineModel, short romType)
{
    machineModel &= 0x07;
    romType      &= 0x07;
    switch(romType)
    {
        case MROM_IDX:
           return(config.romMonitor[machineModel][MONITOR].romFileName);

        case MROM_80C_IDX:
           return(config.romMonitor[machineModel][MONITOR_80C].romFileName);

        case CGROM_IDX:
            return(config.romCG[machineModel].romFileName);

        case KEYMAP_IDX:
            return(config.romKeyMap[machineModel].romFileName);

        case USERROM_IDX:
            return(config.romUser[machineModel].romFileName);

        case FDCROM_IDX:
            return(config.romFDC[machineModel].romFileName);

        default:
            sharpmz_debugf("Unknown get filename romType: %d\n", romType);
            break;
    }
    return(NULL);
}

// Method to set the filename of a rom.
//
void sharpmz_set_rom_file(short machineModel, short romType, char *filename)
{
    machineModel &= 0x07;
    romType      &= 0x03;
    switch(romType)
    {
        case MROM_IDX:
            strcpy(config.romMonitor[machineModel][MONITOR].romFileName, filename);
            sharpmz_debugf("Copy MROM:%s, %s\n", filename,config.romMonitor[machineModel][MONITOR].romFileName );
            break;

        case MROM_80C_IDX:
            strcpy(config.romMonitor[machineModel][MONITOR_80C].romFileName, filename);
            sharpmz_debugf("Copy MROM (80x25):%s, %s\n", filename,config.romMonitor[machineModel][MONITOR_80C].romFileName );
            break;

        case CGROM_IDX:
            strcpy(config.romCG[machineModel].romFileName, filename);
            sharpmz_debugf("Copy CGROM:%s, %s\n", filename,config.romCG[machineModel].romFileName );
            break;

        case KEYMAP_IDX:
            strcpy(config.romKeyMap[machineModel].romFileName, filename);
            sharpmz_debugf("Copy KEYROM:%s, %s\n", filename,config.romKeyMap[machineModel].romFileName );
            break;

        case USERROM_IDX:
            strcpy(config.romUser[machineModel].romFileName, filename);
            sharpmz_debugf("Copy USERROM:%s, %s\n", filename,config.romUser[machineModel].romFileName );
            break;

        case FDCROM_IDX:
            strcpy(config.romFDC[machineModel].romFileName, filename);
            sharpmz_debugf("Copy FDCROM:%s, %s\n", filename,config.romFDC[machineModel].romFileName );
            break;

        default:
            sharpmz_debugf("Unknown set filename romType: %d, filename:%s\n", romType, filename);
            break;
    }
}

// Return string showing last Tape type read.
//
const char *sharpmz_get_tape_type_string(void)
{
    return(SHARPMZ_TAPE_TYPE[ tapeHeader.dataType ]);
}

// Method to get the header of the last tape loaded.
//
sharpmz_tape_header_t *sharpmz_get_tape_header(void)
{
    return(&tapeHeader);
}

// Local version of File Read so that we can read exact number of bytes required
// and return parameter specifies number of bytes actually read.
//
int sharpmz_file_read(fileTYPE *file, void *pBuffer, int nSize)
{
//    if (!FileSeek(file, file->offset, SEEK_SET))
//    {
//        sharpmz_debugf("file_read error(seek).\n");
//        return 0;
//    }

    int ret = FileReadAdv(file, pBuffer, nSize);
    if (ret < 0)
    {
        sharpmz_debugf("file_read error(%d).\n", ret);
        return 0;
    }

    return ret;
}

#if defined __SHARPMZ_DEBUG__
const unsigned int status_green= 0x320000;
const unsigned int menu_green  = 0x322000;
const unsigned int vid_config  = 0x324000;

void test_sb_clear(void)
{
    EnableFpga();
    spi8(SHARPMZ_FILE_ADDR_TX);
    spi8(0x00);
    spi8(status_green >> 16);
    spi8((status_green >> 8) & 0xff);
    spi8(status_green&0xff);
    sharpmz_debugf("Load Address:%04x, %04x, %04x\n", status_green, status_green >> 8, status_green);

    for(int i=0; i< 4096; i++)
    {
        spi_w(0x0000);
    }
    DisableFpga();

    EnableFpga();
    spi8(SHARPMZ_FILE_TX);
    spi8(SHARPMZ_EOF);
    DisableFpga();

    EnableFpga();
    spi8(SHARPMZ_FILE_ADDR_TX);
    spi8(0x00);
    spi8(menu_green >> 16);
    spi8((menu_green >> 8) & 0xff);
    spi8(menu_green&0xff);
    sharpmz_debugf("Load Address:%04x, %04x, %04x\n", menu_green, menu_green >> 8, menu_green);

    for(int i=0; i< 4096; i++)
    {
        spi_w(0x0000);
    }
    DisableFpga();

    EnableFpga();
    spi8(SHARPMZ_FILE_TX);
    spi8(SHARPMZ_EOF);
    DisableFpga();
}


static unsigned char rom[2048];
void test_sb_read_cgrom(void)
{
    unsigned int mz80a_cgrom = 0x503000;

        EnableFpga();                                     // Setup the load address.
        spi8(SHARPMZ_FILE_ADDR_RX);
        spi8(0x00);
        spi8(mz80a_cgrom >> 16);                             // Memory bank to read.
        spi8(mz80a_cgrom >> 8);                              // A15-A8
        spi8(mz80a_cgrom & 0xff);                            // A7-A0

        for(int i=0; i < 2048; i++)
        {
            rom[i] = spi_b(0x00);
        }

        //spi8(0x00);                                       // Setup to read addr first byte.

        // Indicate end of upload.
        //spi8(0x00);                                        // Setup to read addr first byte.
        //spi8(SHARPMZ_EOF);
        DisableFpga();
}

void test_sb_pattern(void)
{
  for(int c=0; c < 10; c++)
  {
    test_sb_clear();
    usleep(500000);

    EnableFpga();
    spi8(SHARPMZ_FILE_TX);
    spi8(SHARPMZ_EOF);
    DisableFpga();
    usleep(500000);

    EnableFpga();
    spi8(SHARPMZ_FILE_ADDR_TX);
    spi8(0x00);
    spi8(status_green >> 16);
    spi8((status_green >> 8) & 0xff);
    spi8(status_green&0xff);
    sharpmz_debugf("Load Address:%04x, %04x, %04x\n", status_green, status_green >> 8, status_green);

    for(int i=0; i< 4096; i++)
    {
        spi8(0x81);
    }
    DisableFpga();

    EnableFpga();
    spi8(SHARPMZ_FILE_TX);
    spi8(SHARPMZ_EOF);
    DisableFpga();
    usleep(500000);
   }
}

void test_sb(void)
{
    unsigned int data;
    int z = 0;

    sharpmz_set_config_register(REGISTER_DISPLAY3, 0x3);
    test_sb_clear();
    test_sb_read_cgrom();

    EnableFpga();
    spi8(SHARPMZ_FILE_ADDR_TX);
    spi8(0x00);
    spi8(status_green >> 16);
    spi8((status_green >> 8) & 0xff);
    spi8(status_green&0xff);
    sharpmz_debugf("Load Address:%04x, %04x, %04x\n", status_green, status_green >> 8, status_green);
    for(int line=0; line < 6; line++)
    {
        for(unsigned int y=0; y < 8; y++)
        {
            for(int x = z; x < z+80; x+=1)
            {
                data = rom[ (x) << 3 | y] << 8 | rom[x << 3 | y];
                //data = rom[ 1 << 3 | y] << 8 | rom[1 << 3 | y];
                spi_w(data);
                sharpmz_debugf("Data:%02x:%02x=%04x\n", x, y, data);
            }
        }
        z = z + 80;
        if (z >= 240) { z = 0; }
    }
    DisableFpga();

    EnableFpga();
    spi8(SHARPMZ_FILE_TX);
    spi8(SHARPMZ_EOF);
    DisableFpga();

    EnableFpga();
    spi8(SHARPMZ_FILE_ADDR_TX);
    spi8(0x00);
    spi8(menu_green >> 16);
    spi8((menu_green >> 8) & 0xff);
    spi8(menu_green&0xff);
    sharpmz_debugf("Load Address:%04x, %04x, %04x\n", menu_green, menu_green >> 8, menu_green);
    z = 0;
    for(int line=0; line < 16; line++)
    {
        for(unsigned int y=0; y < 8; y++)
        {
            for(int x = z; x < z+32; x+=1)
            {
                data = rom[ (x) << 3 | y] << 8 | rom[x << 3 | y];
                spi_w(data);
                sharpmz_debugf("Data:%02x:%02x=%04x\n", x, y, data);
            }
        }
        z = z + 32;
        if (z >= 240) { z = 0; }
    }
    DisableFpga();

    EnableFpga();
    spi8(SHARPMZ_FILE_TX);
    spi8(SHARPMZ_EOF);
    DisableFpga();

}
#endif

// Method to program a config register in the emulator.
//
void sharpmz_set_config_register(short addr, unsigned char value)
{
    EnableFpga();
    spi8(SHARPMZ_CONFIG_TX);
    spi8(addr);                                   // Address of register to change.
    spi8(value);                                  // Value to set in the register.
    sharpmz_debugf("Register Set(%02x) -> %02x\n", addr, value);
    DisableFpga();

    for(int i=0; i < MAX_REGISTERS; i++)
    {
        sharpmz_debugf("%02x => %02x, ", i, sharpmz_read_config_register(i));
    }
    sharpmz_debugf("\n");
    //sharpmz_read_config_register(addr);
}

// Method to read a config register in the emulator.
//
unsigned char sharpmz_read_config_register(short addr)
{
    unsigned char value;

    EnableFpga();
    spi8(SHARPMZ_CONFIG_RX);
    spi8(addr);                                   // Address of register to change.
    spi8(0x00);
    value = spi_b(0x00);                          // Padding to give a cycle to allow data to be read.
    //sharpmz_debugf("Register Read(%02x)=%02x\n", addr, value);
    DisableFpga();

    // Return the value stored in the config register.
    return(value);
}

// Method to send a file to the emulator.
//
void sharpmz_send_file(romData_t &image, char *dirPrefix)
{
    char           sendFile[1024];
    unsigned int   actualReadSize;
    unsigned long  time = GetTimer(0);
    unsigned short i;
    fileTYPE       file = {};

    // If a prefix is given (ie. core directory), prepend it before use.
    //
    if(dirPrefix)
    {
        strcpy(sendFile, dirPrefix);
        strcat(sendFile, "/");
        strcat(sendFile, image.romFileName);
    } else
    {
        strcpy(sendFile, SHARPMZ_CORE_NAME);
        strcat(sendFile, "/");
        strcat(sendFile, image.romFileName);
    }

    sharpmz_debugf("Sending file:%s", sendFile);

    // Try and open the given file, exit on failure.
    if (!FileOpen(&file, sendFile)) return;

    // Prepare transmission of new file.
    EnableFpga();
    spi8(SHARPMZ_FILE_ADDR_TX);
    spi8(0x00);
    spi8(image.loadAddr >> 16);                   // Indicate we are uploading rom images into the correct ROM section of the memory.
    spi8((image.loadAddr >> 8) & 0xff);           // MSB of the rom image.
    spi8(image.loadAddr&0xff);                    // LSB of the rom image.
    sharpmz_debugf("Load Address:%04x, %04x, %04x\n", image.loadAddr, image.loadAddr >> 8, image.loadSize);

    // Limit size to that governed in the config. If the rom is larger than the defined size, waste additional data,
    // if less then finish early. Prevents large images from corrupting other machine roms.
    //
    for (i = 0; i < image.loadSize; i += actualReadSize)
    {
        // Work out size of data block to read.
        int readSize = (image.loadSize - i >= 512 ? 512 : image.loadSize - i);

        // Perform the read.
        DISKLED_ON;
        actualReadSize = sharpmz_file_read(&file, sector_buffer, readSize);
        DISKLED_OFF;

        // If we encountered an error, then bad file or eof, so exit.
        //
        if(actualReadSize != 0)
        {
            spi_write(sector_buffer, actualReadSize, 0);
        } else
        {
            // End of file, short file, so just move onto end.
            actualReadSize = image.loadSize -i;
        }
    }
    DisableFpga();

    // Tidy up.
    FileClose(&file);

    // For debug, display time it took to upload.
    time = GetTimer(0) - time;
    sharpmz_debugf("Uploaded in %lu ms", time >> 20);

    // signal end of transmission
    EnableFpga();
    spi8(SHARPMZ_FILE_TX);
    spi8(SHARPMZ_EOF);
    DisableFpga();
}

// Method to read the emulator memory. Either a specific bank is given or all banks via the parameter bank.
//
short sharpmz_read_ram(const char *memDumpFile, short bank)
{
    unsigned int  actualWriteSize;
    fileTYPE      file = {};

    // Open the memory image debug file for writing.
    if (!sharpmz_file_write(&file, memDumpFile))
    {
        sharpmz_debugf("Failed to open ram dump file:%s\n", memDumpFile);
        return(3);
    }

    // Depending on the bank, or all banks, loop until request is complete.
    //
    for(unsigned int mb=(bank == SHARPMZ_MEMBANK_ALL ? 0 : bank); mb <= (unsigned int)(bank == SHARPMZ_MEMBANK_ALL ? SHARPMZ_MEMBANK_MAXBANKS-1 : bank); mb++)
    {
        EnableFpga();                                     // Setup the load address.
        spi8(SHARPMZ_FILE_ADDR_RX);
        spi8(0x00);
        spi8(MZBANKADDR[mb]);                             // Memory bank to read.
        spi8(0x00);                                       // A15-A8
        spi8(0x00);                                       // A7-A0
        //spi8(0x00);                                       // Setup to read addr first byte.

        // The bank size is stored in the config.
        //
        for (unsigned long j = 0; j < MZBANKSIZE[mb] or actualWriteSize == 0; j += actualWriteSize)
        {
            spi_read(sector_buffer, 512, 0);

            DISKLED_ON;
            actualWriteSize=FileWriteAdv(&file, sector_buffer, MZBANKSIZE[mb] >= 512 ? 512 : MZBANKSIZE[mb]);
            DISKLED_OFF;
        }

        // Indicate end of upload.
        //spi8(SHARPMZ_EOF);
        DisableFpga();
    }

    // Close file to complete dump.
    FileClose(&file);

    // Success.
    //
    return(0);
}

// Method to read the header of a tape.
// Useful for decision making prior to load.
//
short sharpmz_read_tape_header(const char *tapeFile)
{
    unsigned int  actualReadSize;

    //sharpmz_debugf("Sending tape file:%s to emulator ram", tapeFile);

    // Handle for the MZF file to be processed.
    //
    fileTYPE file = {};

    // Try and open the tape file, exit if it cannot be opened.
    //
    if (!FileOpen(&file, tapeFile)) return(1);

    // Read in the tape header, this indicates crucial data such as data type, size, exec address, load address etc.
    //
    actualReadSize = sharpmz_file_read(&file, &tapeHeader, 128);
    if(actualReadSize != 128)
    {
        sharpmz_debugf("Only read:%d bytes of header, aborting.\n", actualReadSize);
        return(1);
    }

    // Some sanity checks.
    //
    if(tapeHeader.dataType == 0 || tapeHeader.dataType > 5) return(4);

    // Success.
    return(0);
}

// Method to load a tape (MZF) file directly into RAM.
// This involves reading the tape header, extracting the size and destination and loading
// the header and program into emulator ram.
//
short sharpmz_load_tape_to_ram(const char *tapeFile, unsigned char dstCMT)
{
    unsigned int  actualReadSize;
    unsigned long time = GetTimer(0);
  #if defined __SHARPMZ_DEBUG__
    char          fileName[17];
  #endif

    //sharpmz_debugf("Sending tape file:%s to emulator ram", tapeFile);

    // Handle for the MZF file to be processed.
    //
    fileTYPE file = {};

    // Try and open the tape file, exit if it cannot be opened.
    //
    if (!FileOpen(&file, tapeFile)) return(1);

    // Read in the tape header, this indicates crucial data such as data type, size, exec address, load address etc.
    //
    actualReadSize = sharpmz_file_read(&file, &tapeHeader, 128);
    if(actualReadSize != 128)
    {
        sharpmz_debugf("Only read:%d bytes of header, aborting.\n", actualReadSize);
        return(2);
    }

    // Some sanity checks.
    //
    if(tapeHeader.dataType == 0 || tapeHeader.dataType > 5) return(4);
  #if defined __SHARPMZ_DEBUG__
    for(int i=0; i < 17; i++)
    {
        fileName[i] = tapeHeader.fileName[i] == 0x0d ? 0x00 : tapeHeader.fileName[i];
    }

    // Debug output to indicate the file loaded and information about the tape image.
    //
    switch(tapeHeader.dataType)
    {
        case 0x01:
            sharpmz_debugf("Binary File(Load Addr=%04x, Size=%04x, Exec Addr=%04x, FileName=%s, StackLow=%04x, StackHigh=%04x)\n", tapeHeader.loadAddress, tapeHeader.fileSize, tapeHeader.execAddress, fileName, MZ_TAPE_HEADER_STACK_ADDR & 0xff, (MZ_TAPE_HEADER_STACK_ADDR >> 8) & 0xff);
            break;

        case 0x02:
            sharpmz_debugf("MZ-80 Basic Program(Load Addr=%04x, Size=%04x, Exec Addr=%04x, FileName=%s)\n", tapeHeader.loadAddress, tapeHeader.fileSize, tapeHeader.execAddress, fileName);
            break;

        case 0x03:
            sharpmz_debugf("MZ-80 Data File(Load Addr=%04x, Size=%04x, Exec Addr=%04x, FileName=%s)\n", tapeHeader.loadAddress, tapeHeader.fileSize, tapeHeader.execAddress, fileName);
            break;

        case 0x04:
            sharpmz_debugf("MZ-700 Data File(Load Addr=%04x, Size=%04x, Exec Addr=%04x, FileName=%s)\n", tapeHeader.loadAddress, tapeHeader.fileSize, tapeHeader.execAddress, fileName);
            break;

        case 0x05:
            sharpmz_debugf("MZ-700 Basic Program(Load Addr=%04x, Size=%04x, Exec Addr=%04x, FileName=%s)\n", tapeHeader.loadAddress, tapeHeader.fileSize, tapeHeader.execAddress, fileName);
            break;

        default:
            sharpmz_debugf("Unknown tape type(Type=%02x, Load Addr=%04x, Size=%04x, Exec Addr=%04x, FileName=%s)\n", tapeHeader.dataType, tapeHeader.loadAddress, tapeHeader.fileSize, tapeHeader.execAddress, fileName);
            break;
    }
  #endif

    // Check the data type, only load machine code directly to RAM.
    //
    if(dstCMT == 0 && tapeHeader.dataType != SHARPMZ_CMT_MC)
        return(3);

    // Reset Emulator if loading direct to RAM. This clears out memory, resets monitor and places it in a known state.
    //
    if(dstCMT == 0)
        sharpmz_reset(10, 50000);

    // Load the data from tape to RAM.
    //
    EnableFpga();                                         // Start the transmission session, clear address and set ioctl_download.
    spi8(SHARPMZ_FILE_ADDR_TX);
    spi8(0x00);
    if(dstCMT == 0)                                       // Load to emulators RAM
    {
        spi8(MZBANKADDR[SHARPMZ_MEMBANK_SYSRAM]);
        spi8((tapeHeader.loadAddress >> 8)&0xff);
        spi8(tapeHeader.loadAddress & 0xff);              // Location set inside tape header structure.
    } else
    {
        spi8(MZBANKADDR[SHARPMZ_MEMBANK_CMT_DATA]);
        spi8(0x00);
        spi8(0x00);
    }

    for (unsigned short i = 0; i < tapeHeader.fileSize && actualReadSize > 0; i += actualReadSize)
    {
        DISKLED_ON;
        actualReadSize = sharpmz_file_read(&file, sector_buffer, 512);
        DISKLED_OFF;

        sharpmz_debugf("Bytes to read, actual:%d, index:%d, sizeHeader:%d", actualReadSize, i, tapeHeader.fileSize);

        if(actualReadSize > 0)
        {
            // Write the sector (or part) to the fpga memory.
            spi_write(sector_buffer, actualReadSize, 0);
        } else
        {
            sharpmz_debugf("Bad tape or corruption, should never be 0, actual:%d, index:%d, sizeHeader:%d", actualReadSize, i, tapeHeader.fileSize);
            return(4);
        }
    }
    DisableFpga();

    // signal end of transmission
    EnableFpga();
    spi8(SHARPMZ_FILE_TX);
    spi8(SHARPMZ_EOF);
    DisableFpga();

    // Now load header - this is done last because the emulator monitor wipes the stack area on reset.
    //
    EnableFpga();                                         // Start the transmission session, clear address and set ioctl_download.
    spi8(SHARPMZ_FILE_ADDR_TX);
    spi8(0x00);                                           // Tape header position address: Bit 24
    if(dstCMT == 0)                                       // Load to emulators RAM
    {
        spi8(MZBANKADDR[SHARPMZ_MEMBANK_SYSRAM]);         // Bits 23:16
        spi8((MZ_TAPE_HEADER_STACK_ADDR >> 8) & 0xff);    // Bits 15:8
        spi8(MZ_TAPE_HEADER_STACK_ADDR & 0xff);           // Bits 7:0
    } else
    {
        spi8(MZBANKADDR[SHARPMZ_MEMBANK_CMT_HDR]);        // Bits 23:16
        spi8(0x00);                                       // Bits 15:8
        spi8(0x00);                                       // Bits 7:0
    }
    //
    spi_write((unsigned char *)&tapeHeader, MZ_TAPE_HEADER_SIZE, 0);
    //
    DisableFpga();
    EnableFpga();                                         // Finally indicate end of transmission.
    spi8(SHARPMZ_FILE_TX);
    spi8(SHARPMZ_EOF);
    DisableFpga();

    time = GetTimer(0) - time;
    sharpmz_debugf("Uploaded in %lu ms", time >> 20);

#ifdef __SHARPMZ_DEBUG__
    // Debug, show registers.
    for(unsigned int i=0; i < MAX_REGISTERS; i++)
    {
        sharpmz_debugf("Register (%02x) = %02x", i, sharpmz_read_config_register(i));
    }
#endif

    // Tidy up.
    FileClose(&file);

#ifdef __SHARPMZ_DEBUG_EXTRA__
    // Dump out the memory if needed (generally for debug purposes).
    if(dstCMT == 0)                                       // Load to emulators RAM
    {
        sharpmz_read_ram((const char *)"memory.dump", SHARPMZ_MEMBANK_SYSRAM);
    } else
    {
        sharpmz_read_ram((const char *)"cmt_header.dump", SHARPMZ_MEMBANK_CMT_HDR);
        sharpmz_read_ram((const char *)"cmt_data.dump", SHARPMZ_MEMBANK_CMT_DATA);
    }
#endif

    // Remove the LF from the header filename, not needed.
    //
    for(int i=0; i < 17; i++)
    {
        if(tapeHeader.fileName[i] == 0x0d) tapeHeader.fileName[i] = 0x00;
    }

#ifdef __SHARPMZ_DEBUG__
    // Debug, show registers.
    for(unsigned int i=0; i < MAX_REGISTERS; i++)
    {
        sharpmz_debugf("Register (%02x) = %02x", i, sharpmz_read_config_register(i));
    }
#endif

    // Success.
    //
    return(0);
}

// Method to save the contents of the CMT buffer onto a disk based MZF file.
// The method reads the header, writes it and then reads the data (upto size specified in header) and write it.
//
short sharpmz_save_tape_from_cmt(const char *tapeFile)
{
    short          dataSize = 0;
    unsigned short actualWriteSize;
    unsigned short writeSize = 0;
    char           fileName[21];

    // Handle for the MZF file to be written.
    //
    fileTYPE file = {};

    // Read the header, then data, but limit data size to the 'file size' stored in the header.
    //
    for(unsigned int mb=SHARPMZ_MEMBANK_CMT_HDR; mb <= SHARPMZ_MEMBANK_CMT_DATA; mb++)
    {
        EnableFpga();                                     // Setup the load address.
        spi8(SHARPMZ_FILE_ADDR_RX);
        spi8(0x00);
        spi8(MZBANKADDR[mb]);                                         // Memory bank to read.
        spi8(0x00);                                       // A15-A8
        spi8(0x00);                                       // A7-A0
        //spi8(0x00);                                       // Setup to read addr first byte.

        // The bank size is stored in the config.
        //
        if(mb == SHARPMZ_MEMBANK_CMT_HDR)
        {
            dataSize = MZ_TAPE_HEADER_SIZE;
        } else
        {
            dataSize = tapeHeader.fileSize;
        }
        sharpmz_debugf("mb=%d, tapesize=%04x\n", mb, tapeHeader.fileSize);
        for (; dataSize > 0; dataSize -= actualWriteSize)
        {
            sharpmz_debugf("mb=%d, dataSize=%04x, writeSize=%04x\n", mb, dataSize, writeSize);
            if(mb == SHARPMZ_MEMBANK_CMT_HDR)
            {
                writeSize = MZ_TAPE_HEADER_SIZE;
            } else
            {
                writeSize = dataSize > 512 ? 512 : dataSize;
            }
            spi_read(sector_buffer, writeSize, 0);
            if(mb == SHARPMZ_MEMBANK_CMT_HDR)
            {
                memcpy(&tapeHeader, &sector_buffer, MZ_TAPE_HEADER_SIZE);

                // Now open the file for writing. If no name provided, use the one stored in the header.
                //
                if(tapeFile == 0)
                {
                    for(int i=0; i < 17; i++)
                    {
                        fileName[i] = tapeHeader.fileName[i] == 0x0d ? 0x00 : tapeHeader.fileName[i];
                    }
                    strcat(fileName, ".mzf");
                    sharpmz_debugf("File from tape:%s (%02x,%04x,%04x,%04x)\n", fileName, tapeHeader.dataType, tapeHeader.fileSize, tapeHeader.loadAddress, tapeHeader.execAddress);
                } else
                {
                    strcpy(fileName, tapeFile);
                    sharpmz_debugf("File provided:%s\n", fileName);
                }

                // Open the memory image debug file for writing.
                if (!sharpmz_file_write(&file, fileName))
                {
                    sharpmz_debugf("Failed to open tape file:%s\n", fileName);
                    return(3);
                }
            }
            DISKLED_ON;
            actualWriteSize=FileWriteAdv(&file, sector_buffer, writeSize);
            DISKLED_OFF;
        }

        // Indicate end of upload.
        //spi8(SHARPMZ_EOF);
        DisableFpga();
    }

    // Close file to complete dump.
    FileClose(&file);

    return(0);
}

// Function from menu.cpp located here due to its static definition. Unneeded parts stripped out but otherwise it performs same
// actions.
//
void sharpmz_select_file(const char* pFileExt, unsigned char Options, char *fs_pFileExt, char chdir, char *SelectedPath)
{
    sharpmz_debugf("pFileExt = %s\n", pFileExt);
    (void)chdir;

    if (strncasecmp(SHARPMZ_CORE_NAME, SelectedPath, strlen(SHARPMZ_CORE_NAME))) strcpy(SelectedPath, SHARPMZ_CORE_NAME);

    ScanDirectory(SelectedPath, SCANF_INIT, pFileExt, Options);
    if (!flist_nDirEntries())
    {
            SelectedPath[0] = 0;
            ScanDirectory(SelectedPath, SCANF_INIT, pFileExt, Options);
    }

    AdjustDirectory(SelectedPath);
    strcpy(fs_pFileExt, pFileExt);
}

// Method to return the default UI state.
//
int sharpmz_default_ui_state(void)
{
    return(MENU_SHARPMZ_MAIN1);
}

// User interface for the SharpMZ Series emulator. This functionality has been located here to minimise changes on common
// code base and to limit common code case size.
// The same general programming structure is maintained to enable easier changes.
//
void sharpmz_ui(int      idleState,    int      idle2State,    int        systemState,    int      selectFile,
                uint32_t *parentstate, uint32_t *menustate,    uint32_t   *menusub,       uint32_t *menusub_last,
                uint64_t *menumask,    char     *selectedPath, int        *helptext_idx,  char     *helptext_custom,
                uint32_t *fs_ExtLen,   uint32_t *fs_Options,   uint32_t   *fs_MenuSelect, uint32_t *fs_MenuCancel,
                char     *fs_pFileExt,
                unsigned char menu,    unsigned char select,   unsigned char up,          unsigned char down,
                unsigned char left,    unsigned char right,    unsigned char plus,        unsigned char minus)
{
    // Locals - original variables are generally all lower case, variables specific to this method are capitalised on change of word.
    //
    static short romType;
    static short machineModel             = sharpmz_get_next_machine_model();
    static short scrollPos                = 0;
#ifdef __SHARPMZ_DEBUG__
    static short memoryBank               = sharpmz_get_next_memory_bank();
    static short fileDumped               = 0;
    static short debugCPUFrequency        = sharpmz_get_next_debug_cpufreq();
    static short debugLedsBank            = sharpmz_get_next_debug_leds_bank();
    static short debugLedsSampleFrequency = sharpmz_get_next_debug_leds_smpfreq();
#endif
    int          menuItem;
    uint32_t     subItem;
    short        romEnabled;
    short        itemCount;
    char         sBuf[40];
    char        *fileName;

    (void)plus;
    (void)minus;

    // Idle2 state (MENU_NONE2) is our main hook, when the HandleUI state machine reaches this state, if the menu key is pressed,
    // we takeover control in this method.
    //
    if(*menustate == (uint32_t)idle2State && menu)
    {
        OsdSetSize(16);
        *menusub = 0;
        OsdClear();
        OsdEnable(DISABLE_KEYBOARD);
        *menustate = MENU_SHARPMZ_MAIN1;
    }

    // The menustate originates in the HandleUI method, if a value is not recognised in the main switch statement in HandleUI then
    // it is ignored. This method utilises this fact and operates on a set of states outside those used in HandleUI.
    //
    switch(*menustate)
    {
        /******************************************************************/
        /* SharpMZ core menu                                              */
        /******************************************************************/

        case MENU_SHARPMZ_MAIN1:

            menuItem  = 0;
            subItem   = 0;
            *menumask = 0;

            OsdSetTitle(CoreName2, 0);

            OsdWrite(menuItem++, "          Main Menu", 0, 0);
            OsdWrite(menuItem++, "",                    0, 0);

            OsdWrite(menuItem++, " Tape Storage              \x16", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            // Not yet implemented.
            //OsdWrite(menuItem++, " Floppy Storage            \x16", *menusub == subItem++, 0);
            //*menumask = (*menumask << 1) | 1;

            OsdWrite(menuItem++, " Machine                   \x16", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            OsdWrite(menuItem++, " Display                   \x16", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

#ifdef __SHARPMZ_DEBUG__
            if(debugEnabled == 1)
            {
                OsdWrite(menuItem++, " Debug                     \x16", *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            } else
            {
                OsdWrite(menuItem++, " Memory Dump               \x16", *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            }
#endif

            OsdWrite(menuItem++, " System                    \x16", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            for (; menuItem < 9; menuItem++)
            {
                OsdWrite(menuItem, "", 0, 0);
            }

            if(sharpmz_get_machine_group() == 2)
            {
                OsdWrite(menuItem++, " Boot Reset",  *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            }
            OsdWrite(menuItem++, " Reset",  *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;
            OsdWrite(menuItem++, " Reload config", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;
            OsdWrite(menuItem++, " Save   config", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;
            OsdWrite(menuItem++, " Reset  config", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            for (; menuItem < 15; menuItem++)
            {
                OsdWrite(menuItem, "", 0, 0);
            }

            OsdWrite(menuItem++, "",                    0, 0);
            OsdWrite(menuItem, "            exit", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            // Set menu states, parent is the root menu, next is the processing menu 2.
            //
            *parentstate = MENU_SHARPMZ_MAIN1;
            *menustate = MENU_SHARPMZ_MAIN2;

            // set helptext with core display on top of basic info
            sprintf(helptext_custom, "                                ");
            strcat(helptext_custom, OsdCoreNameGet());
            strcat(helptext_custom, "                                ");
            strcat(helptext_custom, SHARPMZ_HELPTEXT[0]);
            *helptext_idx = 1;
            break;

        case MENU_SHARPMZ_MAIN2:
            // menu key closes menu
            if (menu)
                *menustate = idleState;

            if (select || right)
            {
#ifndef __SHARPMZ_DEBUG__
                if(*menusub > 2)
                {
                    (*menusub) += 1;
                }
#endif
                if(sharpmz_get_machine_group() != 2 && *menusub > 4) { (*menusub) += 1; }

                switch (*menusub)
                {
                case 0:  // Tape Storage
                    *menustate    = MENU_SHARPMZ_TAPE_STORAGE1;
                    *menusub_last = *menusub;
                    *menusub      = 0;
                    if(right) select = true;
                    break;

                //case 1:  // Floppy Storage
                //    *menustate    = MENU_SHARPMZ_FLOPPY_STORAGE1;
                //    *menusub_last = *menusub;
                //    *menusub      = 0;
                //    if(right) select = true;
                //    break;

                case 1:  // Machine
                    *menustate    = MENU_SHARPMZ_MACHINE1;
                    *menusub_last = *menusub;
                    *menusub      = 0;
                    if(right) select = true;
                    break;

                case 2:  // Display
                    *menustate    = MENU_SHARPMZ_DISPLAY1;
                    *menusub_last = *menusub;
                    *menusub      = 0;
                    if(right) select = true;
                    break;

#ifdef __SHARPMZ_DEBUG__
                case 3:  // Debug/Memory Dump
                    *menustate    = MENU_SHARPMZ_DEBUG1;
                    *menusub_last = *menusub;
                    *menusub      = 0;
                    if(right) select = true;
                    break;
#endif

                case 4:  // System
                    *menustate    = systemState;
#ifdef __SHARPMZ_DEBUG__
                    *menusub_last = *menusub;
#else
                    *menusub_last = *menusub - 1;
#endif
                    *menusub      = 0;
                    if(right) select = true;
                    break;

                case 5: // Boot Reset
                    if(select)
                    {
                        sharpmz_set_boot_reset();
                        *menustate = idleState;
                    }
                    break;

                case 6: // Reset Machine
                    if(select)
                    {
                        sharpmz_init();
                        *menustate = idleState;
                    }
                    break;

                case 7:  // Reload Settings
                    if(select)
                    {
                        sharpmz_reload_config(1);
                        *menustate = MENU_SHARPMZ_MAIN1;
                    }
                    break;

                case 8:  // Save Settings
                    if(select)
                    {
                        sharpmz_save_config();
                        *menustate = MENU_SHARPMZ_MAIN1;
                    }
                    break;

                case 9:  // Reset Settings
                    if(select)
                    {
                        sharpmz_reset_config(1);
                        *menustate = MENU_SHARPMZ_MAIN1;
                    }
                    break;

                case 10:  // Exit
                default:
                    *menustate = idleState;
                    if(right) select = true;
                    break;

                }
            }
            break;

        case MENU_SHARPMZ_TAPE_STORAGE1:
            menuItem  = 0;
            subItem   = 0;
            *menumask = 0;

            OsdSetTitle("Tape Storage", OSD_ARROW_LEFT);

            OsdWrite(menuItem++, "", 0, 0);

            OsdWrite(menuItem++, " Load direct to RAM:   *.MZF", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            OsdWrite(menuItem++, " Queue Tape:           *.MZF", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            OsdWrite(menuItem++, " Clear Queue                ", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            // List out the current tape queue.
            itemCount = 0;
            while((fileName = sharpmz_get_next_filename(0)) != 0)
            {
                if((sharpmz_get_machine_group() == 2 && tapeQueue.tapePos == itemCount) ||
                   (sharpmz_get_machine_group() != 2 && itemCount == 0))
                    sprintf(sBuf, "  %d>%s", itemCount++, fileName);
                else
                    sprintf(sBuf, "  %d %s", itemCount++, fileName);
                OsdWrite(menuItem++, sBuf, 0, 0);
            }

            OsdWrite(menuItem++, "", 0, 0);

            if(sharpmz_get_machine_group() != 2)
            {
                strcpy(sBuf, " Tape Buttons:         ");
                strcat(sBuf, sharpmz_get_tape_buttons_string());
                OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            }

            strcpy(sBuf, " Fast Tape Load:       ");
            strcat(sBuf, sharpmz_get_fasttape_string());
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            if(sharpmz_get_machine_group() != 2)
            {
                strcpy(sBuf, " Map Header (Ascii):   ");
                strcat(sBuf, sharpmz_get_cmt_ascii_mapping_string());
                OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            }

            for (; menuItem < 15; menuItem++)
            {
                OsdWrite(menuItem, "", 0, 0);
            }

            OsdWrite(menuItem, "            exit", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            *parentstate = *menustate;
            *menustate = MENU_SHARPMZ_TAPE_STORAGE2;
            break;


        case MENU_SHARPMZ_TAPE_STORAGE2:
            if (menu) {
                *menustate = MENU_SHARPMZ_MAIN1;
                *menusub = *menusub_last;
            }
            if (select)
            {
                *menusub_last = *menusub;
                if(sharpmz_get_machine_group() == 2 && *menusub > 2) { (*menusub) += 1; }
                if(sharpmz_get_machine_group() == 2 && *menusub > 4) { (*menusub) += 1; }
                switch(*menusub)
                {
                // Load direct to RAM? This involves parsing the MZF header, copying the header (128 bytes) to 0x10F0 in the Z80 memory range
                // then loading the remainder into the location specified (position 20) of size (position 18).
                //
                // Load from Tape? This involves sending the whole file to the emulator CMT buffer, it then plays the file into the
                // emulator as pseudo PWM tape.
                //
                case 0:
                case 1:
                    *fs_Options    = SCANO_DIR;
                    //sharpmz_select_file("MZFmzfMZTmzt", *fs_Options, fs_pFileExt, 1, selectedPath);
                    SelectFile("","MZFmzfMZTmzt", *fs_Options,  1,1 );
                    //sharpmz_select_file("MZFmzf", *fs_Options, fs_pFileExt, 1, selectedPath);
                    *fs_ExtLen     = strlen(fs_pFileExt);
                    *fs_MenuSelect = (*menusub == 0 ? MENU_SHARPMZ_TAPE_STORAGE_LOAD_TAPE_TO_RAM : MENU_SHARPMZ_TAPE_STORAGE_QUEUE_TAPE_TO_CMT);
                    *fs_MenuCancel = MENU_SHARPMZ_MAIN1;
                    *menustate     = selectFile;
                    break;

                case 2:
                    sharpmz_clear_filelist();
                    *menustate = MENU_SHARPMZ_TAPE_STORAGE1;
                    break;

                case 3:
                    sharpmz_set_tape_buttons(sharpmz_get_tape_buttons()+1, 1);
                    *menustate = MENU_SHARPMZ_TAPE_STORAGE1;
                    break;

                case 4:
                    sharpmz_set_fasttape(sharpmz_get_fasttape()+1, 1);
                    *menustate = MENU_SHARPMZ_TAPE_STORAGE1;
                    break;

                case 5:
                    sharpmz_set_cmt_ascii_mapping(sharpmz_get_cmt_ascii_mapping()+1, 1);
                    *menustate = MENU_SHARPMZ_TAPE_STORAGE1;
                    break;

                case 6:
                    *menustate = MENU_SHARPMZ_MAIN1;
                    *menusub   = 0;
                    break;
                }
                *menusub = *menusub_last;
            }
            if (left)
            {
                *menustate = MENU_SHARPMZ_MAIN1;
                *menusub   = *menusub_last;
            }
            break;

       case MENU_SHARPMZ_TAPE_STORAGE_LOAD_TAPE_TO_RAM:
            sharpmz_debugf("File selected to send to RAM: %s, for menu option:%016llx\n", selectedPath, *menumask);
            *menustate = MENU_SHARPMZ_TAPE_STORAGE1;
            if(selectedPath)
            {
                int fail=sharpmz_load_tape_to_ram(selectedPath, 0);
                sharpmz_tape_header_t *tapeHeader = sharpmz_get_tape_header();

                if(!fail)
                    OsdSetTitle("Tape Details", OSD_ARROW_LEFT);
                else
                    OsdSetTitle(" Tape Error", OSD_ARROW_LEFT);

                *menumask = 0x01;    // Exit.
                *parentstate = *menustate;
                menuItem = 0;

                if(!fail)
                    OsdWrite(menuItem++, "       Tape Details", 0, 0);
                else
                    OsdWrite(menuItem++, "        Tape Error", 0, 0);
                OsdWrite(menuItem++, "", 0, 0);
                sprintf(sBuf, " File Size: %04x", tapeHeader->fileSize);
                OsdWrite(menuItem++, sBuf,              0, 0);
                strcpy(sBuf, " File Type: ");
                strcat(sBuf, sharpmz_get_tape_type_string());
                OsdWrite(menuItem++, sBuf,              0, 0);
                strcpy(sBuf, " File Name: ");
                strcat(sBuf, tapeHeader->fileName);
                OsdWrite(menuItem++, sBuf,              0, 0);
                sprintf(sBuf, " Load Addr: %04x", tapeHeader->loadAddress);
                OsdWrite(menuItem++, sBuf,              0, 0);
                sprintf(sBuf, " Exec Addr: %04x", tapeHeader->execAddress);
                OsdWrite(menuItem++, sBuf,              0, 0);
                OsdWrite(menuItem++, "", 0, 0);

                if(!fail)
                    OsdWrite(menuItem++, "", 0, 0);
                else
                {
                    switch(fail)
                    {
                        case 1:
                            strcpy(sBuf, " Unable to open file!");
                            break;
                        case 2:
                            strcpy(sBuf, " Unable to read header!");
                            break;
                        case 3:
                            strcpy(sBuf, " Not a m/code Program!");
                            break;
                        case 4:
                            strcpy(sBuf, " Not a valid tape!");
                            break;
                    }
                    OsdWrite(menuItem++, sBuf, 0, 0);
                }
                for (; menuItem < OsdGetSize()-1; menuItem++) OsdWrite(menuItem, "", 0, 0);
                OsdWrite(menuItem, "            exit", true, 0);
                *menustate = MENU_SHARPMZ_TAPE_STORAGE_LOAD_TAPE_TO_RAM2;
            }
            break;

        case MENU_SHARPMZ_TAPE_STORAGE_LOAD_TAPE_TO_RAM2:
            if (menu || select || left || up)
            {
                *menustate = MENU_SHARPMZ_TAPE_STORAGE1;
                *menusub = *menusub_last;
            }
            break;

        case MENU_SHARPMZ_TAPE_STORAGE_QUEUE_TAPE_TO_CMT:
            sharpmz_debugf("File added to Queue: %s, for menu option:%016llx\n", selectedPath, *menumask);
            *menustate = MENU_SHARPMZ_TAPE_STORAGE1;
            if(selectedPath)
            {
                // Read in the header of the queued tape file.
                //
                int fail=sharpmz_read_tape_header(selectedPath);
                sharpmz_tape_header_t *tapeHeader = sharpmz_get_tape_header();

                *menumask = 0x01;    // Exit.
                *parentstate = *menustate;
                menuItem = 0;

                // Limit number of items in queue, makes no sense to have too many and we run out of display space.
                //
                if(tapeQueue.elements < MAX_TAPE_QUEUE && !fail)
                {
                    sharpmz_push_filename(selectedPath);
                    OsdSetTitle("Tape Queued", OSD_ARROW_LEFT);

                    OsdWrite(menuItem++, "       Tape Details", 0, 0);
                    OsdWrite(menuItem++, "", 0, 0);
                    sprintf(sBuf, " File Size: %04x", tapeHeader->fileSize);
                    OsdWrite(menuItem++, sBuf,              0, 0);
                    strcpy(sBuf, " File Type: ");
                    strcat(sBuf, sharpmz_get_tape_type_string());
                    OsdWrite(menuItem++, sBuf,              0, 0);
                    strcpy(sBuf, " File Name: ");
                    strcat(sBuf, tapeHeader->fileName);
                    OsdWrite(menuItem++, sBuf,              0, 0);
                    sprintf(sBuf, " Load Addr: %04x", tapeHeader->loadAddress);
                    OsdWrite(menuItem++, sBuf,              0, 0);
                    sprintf(sBuf, " Exec Addr: %04x", tapeHeader->execAddress);
                    OsdWrite(menuItem++, sBuf,              0, 0);
                    OsdWrite(menuItem++, "", 0, 0);
                    OsdWrite(menuItem++, "", 0, 0);
                } else
                {
                    OsdSetTitle("Queue Error", OSD_ARROW_LEFT);

                    OsdWrite(menuItem++, "        Queue Error", 0, 0);
                    OsdWrite(menuItem++, "", 0, 0);

                    if(!fail)
                        strcpy(sBuf, " Queue limit reached!");
                    else
                    {
                        switch(fail)
                        {
                            case 1:
                                strcpy(sBuf, " Unable to open file!");
                                break;
                            case 2:
                                strcpy(sBuf, " Unable to read header!");
                                break;
                            case 3:
                                strcpy(sBuf, " Not a m/code Program!");
                                break;
                            case 4:
                                strcpy(sBuf, " Not a valid tape!");
                                break;
                        }
                    }

                    OsdWrite(menuItem++, sBuf, 0, 0);
                }
                for (; menuItem < OsdGetSize()-1; menuItem++) OsdWrite(menuItem, "", 0, 0);
                OsdWrite(menuItem, "            exit", true, 0);
                *menustate = MENU_SHARPMZ_TAPE_STORAGE_QUEUE_TAPE_TO_CMT2;
            }
            break;

        case MENU_SHARPMZ_TAPE_STORAGE_QUEUE_TAPE_TO_CMT2:
            if (menu || select || left || up)
            {
                *menustate = MENU_SHARPMZ_TAPE_STORAGE1;
                *menusub = *menusub_last;
            }
            break;

//        case MENU_SHARPMZ_TAPE_STORAGE_SAVE_TAPE_FROM_CMT:
//            sharpmz_save_tape_from_cmt((const char *)0);
//            *menustate = MENU_SHARPMZ_TAPE_STORAGE1;
//            break;

        case MENU_SHARPMZ_TAPE_STORAGE_SAVE_TAPE_FROM_CMT2:
            *menustate = MENU_SHARPMZ_TAPE_STORAGE1;
            break;

        // Floppy Storage
        case MENU_SHARPMZ_FLOPPY_STORAGE1:
            menuItem  = 0;
            subItem   = 0;
            *menumask = 0;

            OsdSetTitle("Floppy Storage", OSD_ARROW_LEFT);

            OsdWrite(menuItem++, "", 0, 0);

            for (; menuItem < 15; menuItem++)
            {
                OsdWrite(menuItem, "", 0, 0);
            }

            OsdWrite(menuItem, "            exit", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            *parentstate = *menustate;
            *menustate = MENU_SHARPMZ_FLOPPY_STORAGE2;
            break;

        case MENU_SHARPMZ_FLOPPY_STORAGE2:
            if (menu) {
                *menustate = MENU_SHARPMZ_MAIN1;
                *menusub = *menusub_last;
            }
            if (select)
            {
                *menusub_last = *menusub;
                switch(*menusub)
                {
                case 0:
                    break;

                case 1:
                    *menustate = MENU_SHARPMZ_MAIN1;
                    *menusub   = *menusub_last;
                    break;
                }
            }
            if (left)
            {
                *menustate = MENU_SHARPMZ_MAIN1;
                *menusub   = *menusub_last;
            }
            break;

        // Machine configuration.
        case MENU_SHARPMZ_MACHINE1:
            menuItem  = 0;
            subItem   = 0;
            *menumask = 0;

            OsdSetTitle("Machine", OSD_ARROW_LEFT);

            OsdWrite(menuItem++, "", 0, 0);

            strcpy(sBuf, " Machine Model:      ");
            strcat(sBuf, sharpmz_get_machine_model_string());
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            strcpy(sBuf, " CPU Speed:          ");
            strcat(sBuf, sharpmz_get_cpu_speed_string());
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            OsdWrite(menuItem++, "", 0, 0);

            strcpy(sBuf, " Audio Source:       ");
            strcat(sBuf, sharpmz_get_audio_source_string());
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            OsdWrite(menuItem++, "", 0, 0);
            OsdWrite(menuItem++, " Rom Management            \x16", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            for (; menuItem < 15; menuItem++)
            {
                OsdWrite(menuItem, "", 0, 0);
            }

            OsdWrite(menuItem, "            exit", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            *parentstate = *menustate;
            *menustate = MENU_SHARPMZ_MACHINE2;
            break;


        case MENU_SHARPMZ_MACHINE2:
            if (menu) {
                *menustate = MENU_SHARPMZ_MAIN1;
                *menusub = *menusub_last;
            }
            if (select)
            {
                switch(*menusub)
                {
                case 0:  // Machine Model
                    //sharpmz_set_machine_model(sharpmz_get_machine_model() + 1, 1);
                    sharpmz_set_machine_model(sharpmz_get_machine_model() + 1, 1);
                    *menustate = MENU_SHARPMZ_MACHINE1;
                    break;

                case 1:  // CPU Speed
                    sharpmz_set_cpu_speed((sharpmz_get_cpu_speed() + 1) & 0x07, 1);
                    *menustate = MENU_SHARPMZ_MACHINE1;
                    break;

                case 2:  // Audio Source
                    sharpmz_set_audio_source(sharpmz_get_audio_source() ? 0 : 1, 1);
                    *menustate = MENU_SHARPMZ_MACHINE1;
                    break;

                case 3:  // Roms
                    *menustate    = MENU_SHARPMZ_ROMS1;
                    *menusub_last = *menusub;
                    *menusub      = 0;
                    if(right) select = true;
                    break;

                case 4:  // Exit
                    *menustate = MENU_SHARPMZ_MAIN1;
                    *menusub   = 1; //*menusub_last;
                    break;
                }
            }
            if (left)
            {
                *menustate = MENU_SHARPMZ_MAIN1;
                *menusub = 1; //*menusub_last;
            }
            break;

        case MENU_SHARPMZ_ROMS1:

            if(*menusub == 0)
            {
                scrollPos = 0;
            }
            if(down && *menusub  > 9  && scrollPos < 3) { scrollPos++; (*menusub)--; }
            if(up   && *menusub  < 10 && scrollPos > 0) { scrollPos--; (*menusub)++; }

            menuItem  = 0;
            subItem   = 0;
            *menumask = 0;

            OsdSetTitle("Rom Management", OSD_ARROW_LEFT);

            OsdWrite(menuItem++, "", 0, 0);

            if(scrollPos == 0)
            {
                strcpy(sBuf, " Machine Model:      ");
                strcat(sBuf, sharpmz_get_machine_model_string(machineModel));
                OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            }

            if(scrollPos <= 1)
            {
                strcpy(sBuf, " User ROM:           ");
                strcat(sBuf, sharpmz_get_user_rom_enabled_string(machineModel));
                OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            }

            if(scrollPos <= 2)
            {
                strcpy(sBuf, " Floppy Disk ROM:    ");
                strcat(sBuf, sharpmz_get_fdc_rom_enabled_string(machineModel));
                OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
                OsdWrite(menuItem++, "", 0, 0);
            }

            OsdWrite(menuItem++, " Enable Custom Rom:  ", 0, 0);
            romEnabled = sharpmz_get_custom_rom_enabled(machineModel, MROM_IDX);
            strcpy(sBuf, "   Monitor (40x25)   ");
            strcat(sBuf, sharpmz_get_custom_rom_enabled_string(romEnabled));
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            if(romEnabled)
            {
                strcpy(sBuf, "    \x16 ");
                strcat(sBuf, sharpmz_get_rom_file(machineModel, MROM_IDX));
                OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            }

            romEnabled = sharpmz_get_custom_rom_enabled(machineModel, MROM_80C_IDX);
            strcpy(sBuf, "   Monitor (80x25)   ");
            strcat(sBuf, sharpmz_get_custom_rom_enabled_string(romEnabled));
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            if(romEnabled)
            {
                strcpy(sBuf, "    \x16 ");
                strcat(sBuf, sharpmz_get_rom_file(machineModel, MROM_80C_IDX));
                OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            }

            romEnabled = sharpmz_get_custom_rom_enabled(machineModel, CGROM_IDX);
            strcpy(sBuf, "   Char Generator    ");
            strcat(sBuf, sharpmz_get_custom_rom_enabled_string(romEnabled));
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            if(romEnabled)
            {
                strcpy(sBuf, "    \x16 ");
                strcat(sBuf, sharpmz_get_rom_file(machineModel, CGROM_IDX));
                OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            }

            romEnabled = sharpmz_get_custom_rom_enabled(machineModel, KEYMAP_IDX);
            strcpy(sBuf, "   Key Mapping       ");
            strcat(sBuf, sharpmz_get_custom_rom_enabled_string(romEnabled));
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            if(romEnabled)
            {
                strcpy(sBuf, "    \x16 ");
                strcat(sBuf, sharpmz_get_rom_file(machineModel, KEYMAP_IDX));
                OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            }

            romEnabled = sharpmz_get_custom_rom_enabled(machineModel, USERROM_IDX);
            strcpy(sBuf, "   User ROM          ");
            strcat(sBuf, sharpmz_get_custom_rom_enabled_string(romEnabled));
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            if(romEnabled)
            {
                strcpy(sBuf, "    \x16 ");
                strcat(sBuf, sharpmz_get_rom_file(machineModel, USERROM_IDX));
                OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            }

            romEnabled = sharpmz_get_custom_rom_enabled(machineModel, FDCROM_IDX);
            strcpy(sBuf, "   Floppy Disk       ");
            strcat(sBuf, sharpmz_get_custom_rom_enabled_string(romEnabled));
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            if(romEnabled)
            {
                strcpy(sBuf, "    \x16 ");
                strcat(sBuf, sharpmz_get_rom_file(machineModel, FDCROM_IDX));
                OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;
            }

            for (; menuItem < 15; menuItem++)
            {
                OsdWrite(menuItem, "", 0, 0);
            }
            OsdWrite(menuItem, "            exit", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            *parentstate = *menustate;
            *menustate = MENU_SHARPMZ_ROMS2;
            break;


        case MENU_SHARPMZ_ROMS2:
            if (menu) {
                *menustate = MENU_SHARPMZ_MACHINE1;
                *menusub = *menusub_last;
            }
            if (select)
            {
                int selectItem = *menusub + scrollPos;

                if(selectItem > 3)  selectItem += sharpmz_get_custom_rom_enabled(machineModel, MROM_IDX)     ? 0 : 1;
                if(selectItem > 5)  selectItem += sharpmz_get_custom_rom_enabled(machineModel, MROM_80C_IDX) ? 0 : 1;
                if(selectItem > 7)  selectItem += sharpmz_get_custom_rom_enabled(machineModel, CGROM_IDX)    ? 0 : 1;
                if(selectItem > 9)  selectItem += sharpmz_get_custom_rom_enabled(machineModel, KEYMAP_IDX)   ? 0 : 1;
                if(selectItem > 11) selectItem += sharpmz_get_custom_rom_enabled(machineModel, USERROM_IDX)  ? 0 : 1;
                if(selectItem > 13) selectItem += sharpmz_get_custom_rom_enabled(machineModel, FDCROM_IDX)   ? 0 : 1;

                switch(selectItem)
                {
                    case 0:
                        machineModel = sharpmz_get_next_machine_model();
                        *menustate = MENU_SHARPMZ_ROMS1;
                        break;

                    case 1:
                        sharpmz_set_user_rom_enabled(machineModel, sharpmz_get_user_rom_enabled(machineModel) ? 0 : 1, 1);
                        *menustate = MENU_SHARPMZ_ROMS1;
                        break;

                    case 2:
                        sharpmz_set_fdc_rom_enabled(machineModel, sharpmz_get_fdc_rom_enabled(machineModel) ? 0 : 1, 1);
                        *menustate = MENU_SHARPMZ_ROMS1;
                        break;

                    case 3:
                    case 5:
                    case 7:
                    case 9:
                    case 11:
                    case 13:
                        switch(selectItem)
                        {
                            case 3:
                                romType = MROM_IDX;
                                break;

                            case 5:
                                romType = MROM_80C_IDX;
                                break;

                            case 7:
                                romType = CGROM_IDX;
                                break;

                            case 9:
                                romType = KEYMAP_IDX;
                                break;

                            case 11:
                                romType = USERROM_IDX;
                                break;

                            default:
                                romType = FDCROM_IDX;
                                break;
                        }
                        sharpmz_set_custom_rom_enabled(machineModel, romType, sharpmz_get_custom_rom_enabled(machineModel, romType) == 0);
                        *menustate = MENU_SHARPMZ_ROMS1;
                        break;

                    case 4:
                    case 6:
                    case 8:
                    case 10:
                    case 12:
                    case 14:
                        *fs_Options    = SCANO_DIR;
                        sharpmz_select_file("ROMBINrombin", *fs_Options, fs_pFileExt, 1, selectedPath);
                        *fs_ExtLen     = strlen(fs_pFileExt);
                        *fs_MenuSelect = MENU_SHARPMZ_ROM_FILE_SELECTED;
                        *fs_MenuCancel = MENU_SHARPMZ_ROMS1;
                        *menustate     = selectFile;
                        break;

                    case 15:
                        *menustate = MENU_SHARPMZ_MACHINE1;
                        *menusub   = *menusub_last;
                        break;
                }
            }
            if (left)
            {
                *menustate = MENU_SHARPMZ_MACHINE1;
                *menusub   = *menusub_last;
            }
            break;

        case MENU_SHARPMZ_ROM_FILE_SELECTED:
            if(!strncasecmp(selectedPath, SHARPMZ_CORE_NAME, 7)) strcpy(selectedPath, (char *) & selectedPath[8]);
            sharpmz_debugf("File selected: %s, model:%d, for option:%04x\n", selectedPath, machineModel, romType);
            sharpmz_set_rom_file(machineModel, romType, selectedPath);
            *menustate = MENU_SHARPMZ_ROMS1;
            break;

        // DISPLAY Menu
        case MENU_SHARPMZ_DISPLAY1:
            menuItem  = 0;
            subItem   = 0;
            *menumask = 0;

            OsdSetTitle("Display", OSD_ARROW_LEFT);

            OsdWrite(menuItem++, "", 0, 0);

            strcpy(sBuf, " Display Type:  ");
            strcat(sBuf, sharpmz_get_display_type_string());
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            strcpy(sBuf, " VGA Scaling:   ");
            strcat(sBuf, sharpmz_get_vga_mode_string());
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            strcpy(sBuf, " Video:         ");
            strcat(sBuf, sharpmz_get_vram_disable_mode_string());
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            strcpy(sBuf, " Graphics:      ");
            strcat(sBuf, sharpmz_get_gram_disable_mode_string());
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            strcpy(sBuf, " Graphics Addr: ");
            strcat(sBuf, sharpmz_get_gram_base_addr_string());
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            strcpy(sBuf, " VRAM CPU Wait: ");
            strcat(sBuf, sharpmz_get_vram_wait_mode_string());
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            strcpy(sBuf, " PCG Mode:      ");
            strcat(sBuf, sharpmz_get_pcg_mode_string());
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;
         #if defined __SHARPMZ_DEBUG__
            strcpy(sBuf, " Test SB ->     ");
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;
         #endif

          //strcpy(sBuf, " Aspect Ratio:  ");
          //strcat(sBuf, sharpmz_get_aspect_ratio_string());
          //OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
          //*menumask = (*menumask << 1) | 1;

          //strcpy(sBuf, " Scandoubler:   ");
          //strcat(sBuf, sharpmz_get_scandoubler_fx_string());
          //OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
          //*menumask = (*menumask << 1) | 1;

            for (; menuItem < 15; menuItem++)
            {
                OsdWrite(menuItem, "", 0, 0);
            }

            OsdWrite(menuItem, "            exit", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            *parentstate = *menustate;
            *menustate = MENU_SHARPMZ_DISPLAY2;
            break;

        case MENU_SHARPMZ_DISPLAY2:
            if (menu) {
                *menustate = MENU_SHARPMZ_MAIN1;
                *menusub = *menusub_last;
            }
            if (select)
            {
                switch(*menusub)
                {
                case 0:
                    sharpmz_set_display_type((sharpmz_get_display_type() + 1) & 0x07, 1);
                    *menustate = MENU_SHARPMZ_DISPLAY1;
                    break;

                case 1:
                    sharpmz_set_vga_mode(sharpmz_get_vga_mode() + 1, 1);
                    *menustate = MENU_SHARPMZ_DISPLAY1;
                    break;

                case 2:
                    sharpmz_set_vram_disable_mode(sharpmz_get_vram_disable_mode() ? 0 : 1, 1);
                    *menustate = MENU_SHARPMZ_DISPLAY1;
                    break;

                case 3:
                    sharpmz_set_gram_disable_mode(sharpmz_get_gram_disable_mode() ? 0 : 1, 1);
                    *menustate = MENU_SHARPMZ_DISPLAY1;
                    break;

                case 4:
                    sharpmz_set_gram_base_addr((sharpmz_get_gram_base_addr() + 1) & 0x1F, 1);
                    *menustate = MENU_SHARPMZ_DISPLAY1;
                    break;

                case 5:
                    sharpmz_set_vram_wait_mode(sharpmz_get_vram_wait_mode() ? 0 : 1, 1);
                    *menustate = MENU_SHARPMZ_DISPLAY1;
                    break;

                case 6:
                    sharpmz_set_pcg_mode(sharpmz_get_pcg_mode() ? 0 : 1, 1);
                    *menustate = MENU_SHARPMZ_DISPLAY1;
                    break;

             #if defined __SHARPMZ_DEBUG__
                case 7:
                    test_sb();
                    *menustate = MENU_SHARPMZ_DISPLAY1;
                    break;

                case 8:
             #else
                case 7:
             #endif
                    *menustate = MENU_SHARPMZ_MAIN1;
                    *menusub   = *menusub_last;
                    break;

              //case 7:
              //    sharpmz_set_aspect_ratio(sharpmz_get_aspect_ratio() ? 0 : 1, 1);
              //    *menustate = MENU_SHARPMZ_DISPLAY1;
              //    break;

              //case 8:
              //    sharpmz_set_scandoubler_fx((sharpmz_get_scandoubler_fx() + 1) & 0x07, 1);
              //    *menustate = MENU_SHARPMZ_DISPLAY1;
              //    break;

                }
            }
            if (left)
            {
                *menustate = MENU_SHARPMZ_MAIN1;
                *menusub = *menusub_last;
            }
            break;

#ifdef __SHARPMZ_DEBUG__
        // Debug options.
        case MENU_SHARPMZ_DEBUG1:
            menuItem  = 0;
            subItem   = 0;
            *menumask = 0;

            if(debugEnabled == 1)
            {
                OsdSetTitle("Debug", OSD_ARROW_LEFT);
            } else
            {
                OsdSetTitle("Memory Dump", OSD_ARROW_LEFT);
            }

            OsdWrite(menuItem++, "", 0, 0);

            strcpy(sBuf, " Select Memory Bank: ");
            strcat(sBuf, sharpmz_get_memory_bank_string(memoryBank));
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            if(fileDumped == 1)
            {
                strcpy(sBuf, "  File ");
                strcat(sBuf, sharpmz_get_memory_bank_file(memoryBank));
                strcat(sBuf, " written!");
                fileDumped = 0;
            } else
            {
                strcpy(sBuf, "  Dump To \x16 ");
                strcat(sBuf, sharpmz_get_memory_bank_file(memoryBank));
            }
            OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            // Only show debug options if the hardware supports it.
            //
            if(debugEnabled == 1)
            {
                OsdWrite(menuItem++, "", 0, 0);
                strcpy(sBuf, " Debug Mode:         ");
                strcat(sBuf, sharpmz_get_debug_enable_string());
                OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                *menumask = (*menumask << 1) | 1;

                // Display the debug menu only when enabled.
                //
                if(sharpmz_get_debug_enable())
                {
                    strcpy(sBuf, "   CPU Frequency:    ");
                    strcat(sBuf, sharpmz_get_debug_cpufreq_string());
                    OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                    *menumask = (*menumask << 1) | 1;

                    strcpy(sBuf, "   Debug LEDS:       ");
                    strcat(sBuf, sharpmz_get_debug_leds_string());
                    OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                    *menumask = (*menumask << 1) | 1;

                    // Display the LED menu only when enabled.
                    //
                    if(sharpmz_get_debug_leds())
                    {
                        strcpy(sBuf, "     Sample Freq:    ");
                        strcat(sBuf, sharpmz_get_debug_leds_smpfreq_string());
                        OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                        *menumask = (*menumask << 1) | 1;

                        strcpy(sBuf, "     Signal Block:   ");
                        strcat(sBuf, sharpmz_get_debug_leds_bank_string());
                        OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                        *menumask = (*menumask << 1) | 1;

                        strcpy(sBuf, "            Bank:    ");
                        strcat(sBuf, sharpmz_get_debug_leds_subbank_string());
                        OsdWrite(menuItem++, sBuf, *menusub == subItem++, 0);
                        *menumask = (*menumask << 1) | 1;
                    }
                }
            }

            for (; menuItem < 15; menuItem++)
            {
                OsdWrite(menuItem, "", 0, 0);
            }

            OsdWrite(menuItem, "            exit", *menusub == subItem++, 0);
            *menumask = (*menumask << 1) | 1;

            *parentstate = *menustate;
            *menustate = MENU_SHARPMZ_DEBUG2;
            break;

        case MENU_SHARPMZ_DEBUG2:
            if (menu) {
                *menustate = MENU_SHARPMZ_MAIN1;
                *menusub = *menusub_last;
            }
            if (select)
            {
                *menusub_last = *menusub;
                if(debugEnabled == 0) (*menusub) += 6;
                switch(*menusub)
                {
                case 0:  // Select Memory Bank to be dumped.
                    memoryBank = sharpmz_get_next_memory_bank();
                    *menustate = MENU_SHARPMZ_DEBUG1;
                    break;

                case 1:  // Dump selected Memory Bank
                    sharpmz_read_ram(sharpmz_get_memory_bank_file(memoryBank), memoryBank);
                    fileDumped = 1;
                    *menustate = MENU_SHARPMZ_DEBUG1;
                    break;

                case 2:  // Debug Enable
                    sharpmz_set_debug_enable(sharpmz_get_debug_enable() ? 0 : 1, 1);
                    *menustate = MENU_SHARPMZ_DEBUG1;
                    break;

                case 3:  // CPU Frequency
                    debugCPUFrequency = sharpmz_get_next_debug_cpufreq();
                    sharpmz_set_debug_cpufreq(debugCPUFrequency, 1);
                    *menustate = MENU_SHARPMZ_DEBUG1;
                    break;

                case 4:  // Debug LEDS
                    sharpmz_set_debug_leds(sharpmz_get_debug_leds() ? 0 : 1, 1);
                    *menustate = MENU_SHARPMZ_DEBUG1;
                    break;

                case 5:  // Sample Frequency
                    debugLedsSampleFrequency = sharpmz_get_next_debug_leds_smpfreq();
                    sharpmz_set_debug_leds_smpfreq(debugLedsSampleFrequency, 1);
                    *menustate = MENU_SHARPMZ_DEBUG1;
                    break;

                case 6:  // Signal Block
                    debugLedsBank = sharpmz_get_next_debug_leds_bank();
                    sharpmz_set_debug_leds_bank(debugLedsBank, 1);
                    sharpmz_set_debug_leds_subbank(0, 1);             // Always switch to auto if bank changed.
                    sharpmz_get_next_debug_leds_subbank(1);           // And reset the next loop.
                    *menustate = MENU_SHARPMZ_DEBUG1;
                    break;

                case 7:  // Signal Bank
                    sharpmz_set_debug_leds_subbank(sharpmz_get_next_debug_leds_subbank(0), 1);
                    *menustate = MENU_SHARPMZ_DEBUG1;
                    break;

                case 8:  // Exit
                    *menustate = MENU_SHARPMZ_MAIN1;
                    *menusub   = *menusub_last;
                    break;
                }
            }
            if (left)
            {
                *menustate = MENU_SHARPMZ_MAIN1;
                *menusub   = *menusub_last;
            }
            break;
#endif

        default:
            break;
    }
}
