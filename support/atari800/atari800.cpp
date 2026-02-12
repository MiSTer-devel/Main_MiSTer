#include <stdlib.h>
#include <stdio.h>
#include <string.h>
// #include <errno.h>

#include "../../hardware.h"
#include "../../menu.h"
#include "../../file_io.h"
#include "../../debug.h"
#include "../../user_io.h"
#include "../../fpga_io.h"
// #include "../../scheduler.h"

#include "atari800.h"

#define A800_SIO_TX_STAT  0x03
#define A800_SIO_RX       0x04
#define A800_SIO_RX_STAT  0x05
#define A800_SIO_GETDIV   0x06
#define A800_SIO_ERROR    0x07

#define A800_GET_REGISTER 0x08
#define A800_SET_REGISTER 0x09

#define REG_CART1_SELECT  0x01
#define REG_CART2_SELECT  0x02
#define REG_RESET         0x03
#define REG_PAUSE         0x04
#define REG_FREEZER       0x05
#define REG_RESET_RNMI    0x06
#define REG_OPTION_FORCE  0x07
#define REG_DRIVE_LED     0x08
#define REG_SIO_TX        0x09
#define REG_SIO_SETDIV    0x0A

#define REG_ATARI_STATUS1 0x01
#define REG_ATARI_STATUS2 0x02

#define SDRAM_BASE        0x2000000
#define ATARI_BASE        0x0010000

#define ATARI_COLDST      (ATARI_BASE + 0x244)
#define ATARI_BASICF      (ATARI_BASE + 0x3F8)
#define ATARI_GINTLK      (ATARI_BASE + 0x3FA)
#define ATARI_PUPBT       (ATARI_BASE + 0x33D)
#define ATARI_BOOTFLAG    (ATARI_BASE + 0x09)
#define ATARI_CASINI      (ATARI_BASE + 0x02)
#define ATARI_DOSVEC      (ATARI_BASE + 0x0A)
#define ATARI_RUNAD       (ATARI_BASE + 0x2E0)
#define ATARI_INITAD      (ATARI_BASE + 0x2E2)

typedef struct {
	uint8_t cart_type;	// type from CAR header
	char name[16];		// name of type
	uint8_t cart_mode;	// mode used in cartridge emulation
	int size;		// image size in k
} cart_def_t;

static const cart_def_t cart_def[] = 
{
	{ 1,  "Standard       ", TC_MODE_8K,              8 },
	{ 2,  "Standard       ", TC_MODE_16K,            16 },
	// This below is intentional, for 034M carts we fix them
	// (we also need to add 2 extra fake AND-ed banks for 
	// both 043M and 034M)
	{ 3,  "OSS 2 Chip 034M", TC_MODE_OSS_043M,       16 },
	{ 5,  "DB             ", TC_MODE_DB_32,          32 },
	{ 8,  "Williams       ", TC_MODE_WILLIAMS64,     64 },
	{ 9,  "Express        ", TC_MODE_EXPRESS64,      64 },
	{ 10, "Diamond        ", TC_MODE_DIAMOND64,      64 },
	{ 11, "Sparta DOS X   ", TC_MODE_SDX64,          64 },
	{ 12, "XEGS           ", TC_MODE_XEGS_32,        32 },
	{ 13, "XEGS Bank 0-7  ", TC_MODE_XEGS_64,        64 },
	{ 14, "XEGS           ", TC_MODE_XEGS_128,      128 },
	{ 15, "OSS 1 Chip     ", TC_MODE_OSS_16,         16 },
	{ 17, "Atrax Decoded  ", TC_MODE_ATRAX128,      128 },
	{ 18, "Bounty Bob     ", TC_MODE_BOUNTY_40,      40 },
	{ 21, "Right          ", TC_MODE_RIGHT_8K,        8 },
	{ 22, "Williams       ", TC_MODE_WILLIAMS32,     32 },
	{ 23, "XEGS           ", TC_MODE_XEGS_256,      256 },
	{ 24, "XEGS           ", TC_MODE_XEGS_512,      512 },
	{ 25, "XEGS           ", TC_MODE_XEGS_1024,    1024 },
	{ 26, "MegaCart       ", TC_MODE_MEGA_16,        16 },
	{ 27, "MegaCart       ", TC_MODE_MEGA_32,        32 },
	{ 28, "MegaCart       ", TC_MODE_MEGA_64,        64 },
	{ 29, "MegaCart       ", TC_MODE_MEGA_128,      128 },
	{ 30, "MegaCart       ", TC_MODE_MEGA_256,      256 },
	{ 31, "MegaCart       ", TC_MODE_MEGA_512,      512 },
	{ 32, "MegaCart       ", TC_MODE_MEGA_1024,    1024 },
	{ 33, "Super XEGS     ", TC_MODE_SXEGS_32,       32 },
	{ 34, "Super XEGS     ", TC_MODE_SXEGS_64,       64 },
	{ 35, "Super XEGS     ", TC_MODE_SXEGS_128,     128 },
	{ 36, "Super XEGS     ", TC_MODE_SXEGS_256,     256 },
	{ 37, "Super XEGS     ", TC_MODE_SXEGS_512,     512 },
	{ 38, "Super XEGS     ", TC_MODE_SXEGS_1024,   1024 },
	{ 39, "Phoenix        ", TC_MODE_PHOENIX,         8 },
	{ 40, "Blizzard       ", TC_MODE_BLIZZARD,       16 },
	{ 41, "Atarimax       ", TC_MODE_ATARIMAX1,     128 },
	{ 42, "Atarimax Old   ", TC_MODE_ATARIMAX8,    1024 },
	{ 43, "Sparta DOS X   ", TC_MODE_SDX128,        128 },
	{ 44, "OSS 1 Chip     ", TC_MODE_OSS_8,           8 },
	{ 45, "OSS 2 Chip 043M", TC_MODE_OSS_043M,       16 },
	{ 46, "Blizzard       ", TC_MODE_BLIZZARD_4,      4 },
	{ 47, "AST            ", TC_MODE_AST_32,         32 },
	{ 48, "Atrax SDX      ", TC_MODE_ATRAX_SDX64,    64 },
	{ 49, "Atrax SDX      ", TC_MODE_ATRAX_SDX128,  128 },
	{ 50, "TurboSoft      ", TC_MODE_TSOFT_64,       64 },
	{ 51, "TurboSoft      ", TC_MODE_TSOFT_128,     128 },
	{ 52, "UltraCart      ", TC_MODE_ULTRA_32,       32 },
	{ 53, "Low Bank XL    ", TC_MODE_RIGHT_8K,        8 },
	{ 54, "S.I.C.         ", TC_MODE_SIC_128,       128 },
	{ 55, "S.I.C.         ", TC_MODE_SIC_256,       256 },
	{ 56, "S.I.C.         ", TC_MODE_SIC_512,       512 },
	{ 57, "Standard       ", TC_MODE_2K,              2 },
	{ 58, "Standard       ", TC_MODE_4K,              4 },
	{ 59, "Right          ", TC_MODE_RIGHT_4K,        4 },
	{ 60, "Blizzard       ", TC_MODE_BLIZZARD_32,    32 },
	{ 61, "MegaMax        ", TC_MODE_MEGAMAX16,    2048 },
	{ 63, "MegaCart       ", TC_MODE_MEGA_4096,    4096 },
	{ 64, "MegaCart       ", TC_MODE_MEGA_2048,    2048 },
	{ 67, "XEGS Bank 8-15 ", TC_MODE_XEGS_64_2,      64 },
	{ 68, "Atrax Encoded  ", TC_MODE_ATRAX_INT128,  128 },
	{ 69, "aDawliah       ", TC_MODE_DAWLI_32,       32 },
	{ 70, "aDawliah       ", TC_MODE_DAWLI_64,       64 },
	{ 75, "Atarimax New   ", TC_MODE_ATARIMAX8_2,  1024 },
	{ 76, "Williams       ", TC_MODE_WILLIAMS16,     16 },
	{ 80, "JRC-Linear     ", TC_MODE_JRC_LIN_64,     64 },
	{ 83, "S.I.C.+        ", TC_MODE_SIC_1024,     1024 },
	{ 86, "XE Multicart   ", TC_MODE_XEMULTI_8,       8 },
	{ 87, "XE Multicart   ", TC_MODE_XEMULTI_16,     16 },
	{ 88, "XE Multicart   ", TC_MODE_XEMULTI_32,     32 },
	{ 89, "XE Multicart   ", TC_MODE_XEMULTI_64,     64 },
	{ 90, "XE Multicart   ", TC_MODE_XEMULTI_128,   128 },
	{ 91, "XE Multicart   ", TC_MODE_XEMULTI_256,   256 },
	{ 92, "XE Multicart   ", TC_MODE_XEMULTI_512,   512 },
	{ 93, "XE Multicart   ", TC_MODE_XEMULTI_1024, 1024 },
	{104, "J(atari)cart   ", TC_MODE_JATARI_8,        8 },
	{105, "J(atari)cart   ", TC_MODE_JATARI_16,      16 },
	{106, "J(atari)cart   ", TC_MODE_JATARI_32,      32 },
	{107, "J(atari)cart   ", TC_MODE_JATARI_64,      64 },
	{108, "J(atari)cart   ", TC_MODE_JATARI_128,    128 },
	{109, "J(atari)cart   ", TC_MODE_JATARI_256,    256 },
	{110, "J(atari)cart   ", TC_MODE_JATARI_512,    512 },
	{111, "J(atari)cart   ", TC_MODE_JATARI_1024,  1024 },
	{112, "DCART          ", TC_MODE_DCART,         512 },
	{160, "JRC-Interleaved", TC_MODE_JRC_INT_64,     64 },
	{ 0, "", 0, 0 }
};

#define STATUS1_MASK_SOFTBOOT   0x0001
#define STATUS1_MASK_COLDBOOT   0x0002
#define STATUS1_MASK_HALT       0x0004
#define STATUS1_MASK_MODE800    0x0008
#define STATUS1_MASK_BOOTX      0x0010
#define STATUS1_MASK_XEXLOC     0x0020
#define STATUS1_MASK_RDONLY     0x0040
#define STATUS1_MASK_MODEPBI    0x0080
#define STATUS1_MASK_ATX1050    0x8000

#define STATUS2_MASK_DRVCFG     0x00FF
#define STATUS2_MASK_BOOTDRV    0x0700
#define STATUS2_MASK_SPLASH     0x0800

static uint8_t buffer[A800_BUFFER_SIZE];

#define XEX_LOADER_LOC          7 // XEX Loader is at $700 by default

#include "xex_loader.h"
#include "boot_xex_loader.h"

static fileTYPE xex_file = {};
static uint8_t xex_file_first_block;
static uint8_t xex_reloc;
static uint32_t xex_loader_base;

static void set_a800_reg(uint8_t reg, uint8_t val)
{
	EnableIO();
	spi8(A800_SET_REGISTER);
	spi_w((reg << 8) | val);
	DisableIO();
}

static uint16_t get_a800_reg(uint8_t reg)
{
	uint16_t r;
	EnableIO();
	spi8(A800_GET_REGISTER);
	r = spi_w(reg << 8);
	DisableIO();
	return r;
}

static uint16_t get_a800_reg2(uint8_t reg)
{
	uint16_t r;
	EnableIO();
	spi8(reg);
	r = spi_w(0);
	DisableIO();
	return r;
}

static void atari800_dma_write(const uint8_t *buf, uint32_t addr, uint32_t len)
{
	user_io_set_index(99);
	user_io_set_download(1, addr);
	user_io_file_tx_data(buf, len);
	user_io_set_download(0);
}

static void atari800_dma_read(uint8_t *buf, uint32_t addr, uint32_t len)
{
	user_io_set_index(99);
	user_io_set_upload(1, addr);
	user_io_file_rx_data(buf, len);		
	user_io_set_upload(0);
}

static void atari800_dma_zero(uint32_t addr, uint32_t len)
{
	memset(buffer, 0, A800_BUFFER_SIZE);
	uint32_t to_write = len > A800_BUFFER_SIZE ? A800_BUFFER_SIZE : len;
	
	user_io_set_index(99);
	user_io_set_download(1, addr);
	while(len)
	{
		user_io_file_tx_data(buffer, to_write);
		len -= to_write;
		to_write = len > A800_BUFFER_SIZE ? A800_BUFFER_SIZE : len;
	}
	user_io_set_upload(0);
}

static void reboot(uint8_t cold, uint8_t pause)
{
	int i;

	set_a800_reg(REG_PAUSE, 1);
	if (cold)
	{
		set_a800_reg(REG_FREEZER, 0);
		// Initialize the first 64K of SDRAM with a pattern
		for(i = 0; i < A800_BUFFER_SIZE; i += 2)
		{
			buffer[i] = 0xFF;
			buffer[i+1] = 0x00;
		}
		user_io_set_index(99);
		user_io_set_download(1, SDRAM_BASE);
		for(i = 0; i < 0x10000 / A800_BUFFER_SIZE; i++) user_io_file_tx_data(buffer, A800_BUFFER_SIZE);
		user_io_set_upload(0);
	}
	else
	{
		FileClose(&xex_file);
	}
	
	// Both cold==1 and pause==1 is a special case when 
	// the XEX loader performs a cold/warm boot to push 
	// in the loader, in this case on the 800 we just want
	// the same effect as pressing the RESET (so soft)
	// while we actually mean a power cycle with forced
	// OS initialization. (In other words, on 800 a power
	// cycle does not allow to pre-init the OS to do a warm
	// start, it will always be cold).

	if((get_a800_reg(REG_ATARI_STATUS1) & STATUS1_MASK_MODE800) && (!cold || pause))
	{
		set_a800_reg(REG_RESET_RNMI, 1);
		set_a800_reg(REG_RESET_RNMI, 0);
	}
	else
	{
		set_a800_reg(REG_RESET, 1);
		set_a800_reg(REG_RESET, 0);
	}

	if(cold)
	{
		set_a800_reg(REG_FREEZER, 1);
	}
	set_a800_reg(REG_PAUSE, pause);
}

static void uart_init(uint8_t divisor)
{
	set_a800_reg(REG_SIO_SETDIV, (divisor << 1) + 1);
}

// TODO timeouts for the two?

static uint8_t uart_full()
{
	uint16_t r = get_a800_reg2(A800_SIO_TX_STAT);
	return r & 0x200 ? 1 : 0;
}

static void uart_send(uint8_t data)
{
	while(uart_full())
	{
		// TODO 
		//scheduler_yield();
	}
	set_a800_reg(REG_SIO_TX, data);
}

static uint8_t uart_available()
{
	return !((get_a800_reg2(A800_SIO_RX_STAT) >> 8) & 0x1);
}

static uint16_t uart_receive()
{
	while(!uart_available())
	{
		// TODO
		//scheduler_yield();
	}
	return get_a800_reg2(A800_SIO_RX);
}

static void uart_switch()
{
	// Working with this for a while now, I still have no clue what it does... :/
	set_a800_reg(REG_SIO_SETDIV, (uint8_t)(get_a800_reg2(A800_SIO_GETDIV)-1));
}

static uint16_t uart_error()
{
	return get_a800_reg2(A800_SIO_ERROR);
}

static int cart_matches_total;
static uint8_t cart_matches_mode[16]; // The are 15 supported 64K carts, this is the max ATM
static int cart_matches_idx[16];
static uint8_t cart_match_car;
static int mounted_cart1_size; 
static unsigned char cart_io_index;

int atari800_get_match_cart_count()
{
	return cart_matches_total;
}

const char *atari800_get_cart_match_name(int match_index)
{
	return cart_def[cart_matches_idx[match_index]].name;
}

void atari800_umount_cartridge(uint8_t stacked)
{
	// TODO Clever cart deselect 1 & 2 and reboot?
	set_a800_reg(stacked ? REG_CART2_SELECT : REG_CART1_SELECT, 0);
	if(!stacked)
	{
		mounted_cart1_size = 0;
	}		
}

int atari800_check_cartridge_file(const char* name, unsigned char index)
{
	fileTYPE f = {};

	int to_read = 0x2000;
	int idx = 0;
	int file_size;
	
	cart_match_car = 0;
	cart_matches_total = 0;
	cart_io_index = index;
	
	int ext_index = index >> 6;
	uint8_t stacked = (index & 0x3F) == 9;
	
	if (!(stacked && mounted_cart1_size > 0x100000) && FileOpen(&f, name))
	{
		file_size = f.size;
		if (!ext_index)
		{
			to_read = 16;
			cart_match_car = 1;
		}

		if(to_read > f.size)
		{
			to_read = f.size;
		}
		FileReadAdv(&f, buffer, to_read);
		FileClose(&f);
		
		if(cart_match_car)
		{
			// CAR file, presumably, let's check further
			if (buffer[0] == 'C' && buffer[1] == 'A' && buffer[2] == 'R' && buffer[3] == 'T')
			{
				while (cart_def[idx].cart_type)
				{
					if(cart_def[idx].cart_type == buffer[7])
					{
						cart_matches_idx[0] = idx;
						cart_matches_mode[0] = cart_def[idx].cart_mode;
						cart_matches_total = 1;
						break;
					}
					idx++;
				}
			}
		}
		else
		{
			// First check for Ultimate & SIDE2 SDX cartridges
			if(to_read >= 0x2000 && buffer[0] == 'S' && buffer[1] == 'D' && buffer[2] == 'X' && (buffer[0x1FF3] == 0xE0 || buffer[0x1FF3] == 0xE1))
			{
				cart_matches_idx[0] = -1;
				cart_matches_mode[0] = (buffer[0x1FF3] == 0xE1) ? TC_MODE_SDX_SIDE2 : TC_MODE_SDX_U1MB;
				cart_matches_total = 1;
			}
			else
			{
				while (cart_def[idx].cart_type)
				{
					if(cart_def[idx].size == (file_size >> 10))
					{
						cart_matches_idx[cart_matches_total] = idx;
						cart_matches_mode[cart_matches_total] = cart_def[idx].cart_mode;
						cart_matches_total++;
					}
					idx++;
				}
			}
		}
	}

	return cart_matches_total;
}

void atari800_open_cartridge_file(const char* name, int match_index)
{
	uint8_t stacked = (cart_io_index & 0x3F) == 9;
	uint8_t *buf = &buffer[0];
	uint8_t *buf2 = &buffer[4096];
	fileTYPE f = {};
	int offset = cart_match_car ? 16 : 0;
	uint8_t cart_type = cart_def[cart_matches_idx[match_index]].cart_type;

	if (FileOpen(&f, name))
	{
		set_a800_reg(REG_PAUSE, 1);
		set_a800_reg(REG_CART2_SELECT, 0);
		if(!stacked) set_a800_reg(REG_CART1_SELECT, 0);

		ProgressMessage(0, 0, 0, 0);
		FileSeek(&f, offset, SEEK_SET);

		user_io_set_index(cart_io_index);
		user_io_set_download(1);
	
		if(cart_type == 3 || cart_type == 45)
		{
			ProgressMessage("Loading", f.name, 0, 6);
			FileReadAdv(&f, buf, 4096);
			user_io_file_tx_data(buf, 4096);

			if (cart_type == 3) FileSeek(&f, offset + 8192, SEEK_SET);
			ProgressMessage("Loading", f.name, 1, 6);
			FileReadAdv(&f, buf, 4096);
			user_io_file_tx_data(buf, 4096);

			if (cart_type == 3) FileSeek(&f, offset + 4096, SEEK_SET);
			ProgressMessage("Loading", f.name, 2, 6);
			FileReadAdv(&f, buf2, 4096); // NOTE different buffer!
			user_io_file_tx_data(buf2, 4096);
			
			if (cart_type == 3) FileSeek(&f, offset + 12288, SEEK_SET);
			ProgressMessage("Loading", f.name, 3, 6);
			FileReadAdv(&f, buf, 4096);
			user_io_file_tx_data(buf, 4096);
			
			FileSeek(&f, offset, SEEK_SET);
			ProgressMessage("Loading", f.name, 4, 6);
			FileReadAdv(&f, buf, 4096);
			for(int i = 0; i < 4096; i++) buf[i] &= buf2[i];
			user_io_file_tx_data(buf, 4096);

			if (cart_type == 3) FileSeek(&f, offset + 8192, SEEK_SET);
			ProgressMessage("Loading", f.name, 5, 6);
			FileReadAdv(&f, buf, 4096);
			for(int i = 0; i < 4096; i++) buf[i] &= buf2[i];
			user_io_file_tx_data(buf, 4096);
		}
		else
		{
			while (offset < f.size)
			{
				int to_read = f.size - offset;
				if (to_read > A800_BUFFER_SIZE) to_read = A800_BUFFER_SIZE;
				ProgressMessage("Loading", f.name, offset, f.size);
				FileReadAdv(&f, buffer, to_read);
				user_io_file_tx_data(buffer, to_read);
				offset += to_read;
			}
		}
		FileClose(&f);
		user_io_set_download(0);
		ProgressMessage(0, 0, 0, 0);
		set_a800_reg(stacked ? REG_CART2_SELECT : REG_CART1_SELECT, cart_matches_mode[match_index]);
		if(!stacked)
		{
			mounted_cart1_size = cart_match_car ? f.size - 16 : f.size;
		}

		if(!stacked || (get_a800_reg(REG_ATARI_STATUS1) & STATUS1_MASK_MODE800))
		{
			reboot(1, 0);
		}
	}
}

void atari800_open_bios_file(const char* name, unsigned char index)
{
	uint8_t bios_index = (index & 0x3F);
	uint16_t mode800 = get_a800_reg(REG_ATARI_STATUS1) & STATUS1_MASK_MODE800;
	user_io_file_tx(name, index);
	if((mode800 && bios_index == 6) || (!mode800 && (bios_index == 4 || bios_index == 5))) reboot(1, 0);
}

#define MAX_DRIVES 15

typedef struct {
	fileTYPE file;
	uint8_t info;
	uint8_t custom_loader;
	uint32_t offset;
	uint32_t meta_offset; // HDD only
	uint16_t partition_id; // HDD only
	uint32_t sector_count;
	uint16_t sector_size;
	uint8_t atari_sector_status;
} drive_info_t;

static drive_info_t drive_infos[MAX_DRIVES + 1] = {};

#define INFO_RO 0x40
#define INFO_HDD 0x80
#define INFO_META 0x20 // if the HDD uses the meta information sectors
#define INFO_SS 0x10 // mark that the sector is smaller than the SD card image sector

typedef struct {
	uint16_t wMagic;
	uint16_t wPars;
	uint16_t wSecSize;
	uint8_t btParsHigh;
	uint32_t dwCRC;
	uint32_t dwUNUSED;
	uint8_t btFlags;
} __attribute__((packed)) atr_header_t;

typedef struct {
	uint8_t deviceId;
	uint8_t command;
	uint8_t aux1;
	uint8_t aux2;
	uint8_t chksum;
	uint16_t auxab;
} __attribute__((packed)) sio_command_t;

typedef struct {
	int bytes;
	int success;
	int speed;
	int respond;
	uint8_t *sector_buffer;
} sio_action_t;

#define XEX_SECTOR_SIZE 128
#define ATARI_SECTOR_BUFFER_SIZE 512

static uint8_t atari_sector_buffer[ATARI_SECTOR_BUFFER_SIZE];
static uint32_t pre_ce_delay;
static uint32_t pre_an_delay;

#define DELAY_T2_MIN      100 /* 100 BiboDos needs at least 50us delay before ACK */
#define DELAY_T5_MIN      600 /* 300 the QMEG OS needs at least 300usec delay between ACK and complete */
#define DELAY_T3_PERIPH   150 /* 150 QMEG OS 3 needs a delay of 150usec between complete and data */

static int speed_index = 0;
static const uint8_t speeds[] = {0x28, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00};

#define ui_speed_index ((get_a800_reg(REG_ATARI_STATUS1) >> 8) & 0x07)

static uint8_t get_checksum(uint8_t* buf, int len)
{
	uint8_t sumo = 0;
	uint8_t sum = 0;
	for(int i = 0; i < len; i++)
	{
		sum += buf[i];
		if(sum < sumo) sum++;
		sumo = sum;
	}
	return sum;
}

static void uart_send_buffer(uint8_t *buf, int len)
{
	while(len > 0) { uart_send(*buf++); len--; }
}

static void uart_send_cmpl_and_atari_sector_buffer_and_check_sum(uint8_t *buf, int len, int success)
{
	usleep(pre_ce_delay);
	uart_send(success ? 'C' : 'E');
	usleep(DELAY_T3_PERIPH);
	uart_send_buffer(buf, len);
	uart_send(get_checksum(buf, len));
}

static uint8_t hdd_partition_scan(fileTYPE *file, uint8_t info)
{
	int offset = 0;
	FileSeek(file, offset, SEEK_SET);
	if(FileReadAdv(file, atari_sector_buffer, 256) != 256) return 0;

	if(atari_sector_buffer[1] != 'A' || atari_sector_buffer[2] != 'P' || atari_sector_buffer[3] != 'T')
	{
		if(FileReadAdv(file, atari_sector_buffer, 256) != 256) return 0;
		offset = 0xC2;
		while(offset < 0x100)
		{
			if(atari_sector_buffer[offset] == 0x7F)
			{
				offset = ((atari_sector_buffer[offset+4]) |
					((atari_sector_buffer[offset+5] << 8)) |
					((atari_sector_buffer[offset+6] << 16)) |
					((atari_sector_buffer[offset+7] << 24))) << 9;
				FileSeek(file, offset, SEEK_SET);
				if(FileReadAdv(file, atari_sector_buffer, 256) != 256 || atari_sector_buffer[1] != 'A' || atari_sector_buffer[2] != 'P' || atari_sector_buffer[3] != 'T') return 0;
				break;
			}
			offset += 16;
		}
	}
	if(atari_sector_buffer[0] == 0x10)
	{
		info |= INFO_META;
	}
	else if(atari_sector_buffer[0]) return 0;
	info |= (atari_sector_buffer[4] & 0xF);
	for(int pidx = 0; pidx < 15; pidx++)
	{
		int i = (pidx+1)*16;
		if(!atari_sector_buffer[i])
		{
			// empty slot
			if(drive_infos[pidx].info & INFO_HDD)
			{
				drive_infos[pidx].info = 0;
			}
		}
		else
		{
			drive_infos[pidx].info = (info & 0xC0) | (atari_sector_buffer[i] & 0x40 ? INFO_META : 0) |
				((atari_sector_buffer[i] & 0x30) || (atari_sector_buffer[i+12] & 0x80) ? INFO_RO : 0);
			atari_sector_buffer[i] &= 0x8F;
			if(atari_sector_buffer[i] > 3 || (atari_sector_buffer[i+1] != 0x00 && atari_sector_buffer[i+1] != 0x03) || !(atari_sector_buffer[i+12] & 0x40))
			{
				drive_infos[pidx].info = 0; // TODO
			}
			else
			{
				drive_infos[pidx].sector_size = 128 << (atari_sector_buffer[i]-1);
				if(drive_infos[pidx].sector_size != 512)
				{
					drive_infos[pidx].info |= INFO_SS;
				}
				drive_infos[pidx].offset =
					( atari_sector_buffer[i+2] |
					(atari_sector_buffer[i+3] << 8) |
					(atari_sector_buffer[i+4] << 16) |
					(atari_sector_buffer[i+5] << 24) ) << 9;
				drive_infos[pidx].sector_count =
					atari_sector_buffer[i+6] |
					(atari_sector_buffer[i+7] << 8) |
					(atari_sector_buffer[i+8] << 16) |
					(atari_sector_buffer[i+9] << 24);
				drive_infos[pidx].partition_id = atari_sector_buffer[i+10] | (atari_sector_buffer[i+11] << 8);
				if(atari_sector_buffer[i+1] == 0x00) // DOS partition
				{
					uint16_t p_offset =
						(atari_sector_buffer[i+14] | (atari_sector_buffer[i+15] << 8)) << 9;
					drive_infos[pidx].meta_offset = drive_infos[pidx].offset - 512;
					drive_infos[pidx].offset += p_offset;
				}
				else // External partition
				{
					drive_infos[pidx].meta_offset =
						(atari_sector_buffer[i+13] |
						(atari_sector_buffer[i+14] << 8) |
						(atari_sector_buffer[i+15] << 16)) << 9;
				}
				drive_infos[pidx].custom_loader = 0;
				drive_infos[pidx].atari_sector_status = 0xFF;
				//drive_infos[pidx].file = file;
			}
		}
	}
	return info;
}

static void set_drive_status(int drive_number, const char *name, uint8_t ext_index)
{
	uint8_t info = 0;
	atr_header_t atr_header;

	if(!name[0])
	{
		// TODO we should remember where the HDD was mounted and check drive_number for that!!!
		// TODO Or not! If we mount HDD from a separate slot!
		// TODO What to do when trying to mount over an HDD partition??? (or the other way round)
		// TODO Should HDD simply have priority
		// TODO should HDD loading be limited by PBI mode ?
		if(drive_number > 1 && drive_infos[MAX_DRIVES].file.opened())
		{
			FileClose(&drive_infos[MAX_DRIVES].file);
			for(int i = 0; i <= MAX_DRIVES; i++)
			{
				if(drive_infos[i].info & INFO_HDD)
				{
					drive_infos[i].info = 0;
				}
			}
		}
		else
		{
			FileClose(&drive_infos[drive_number].file);
		}
		return;
	}

	uint8_t read_only = (ext_index == 1) || (ext_index == 3 && drive_number < 2) ||
		!FileCanWrite(name) || (get_a800_reg(REG_ATARI_STATUS1) & STATUS1_MASK_RDONLY);


	// Slots 3 & 4 double as HDD image -> redirect to the last slot in the table
	// remember where the drive was mounted?
	if(drive_number > 1 && ext_index == 3) drive_number = MAX_DRIVES;

	if(!FileOpenEx(&drive_infos[drive_number].file, name, read_only ? O_RDONLY : (O_RDWR | O_SYNC))) return;

	if(read_only) info |= INFO_RO;

	if(ext_index == 0) // ATR only
	{
		if (FileReadAdv(&drive_infos[drive_number].file, (uint8_t *)&atr_header, 16) != 16)
		{
			FileClose(&drive_infos[drive_number].file);
			return;
		}
	}

	drive_infos[drive_number].custom_loader = 0;
	drive_infos[drive_number].atari_sector_status = 0xff;

	if (ext_index == 2) // XDF
	{
		drive_infos[drive_number].offset = 0;
		drive_infos[drive_number].sector_count = drive_infos[drive_number].file.size / 0x80;
		drive_infos[drive_number].sector_size = 0x80;
	}
	else if (ext_index == 3) // ATX or HDD image
	{
		if(drive_number < MAX_DRIVES)
		{
			drive_infos[drive_number].custom_loader = 2;
			// TODO
			//gAtxFile = &file;
			//uint8_t atxType = loadAtxFile(drive_number);
			//drive_infos[drive_number].sector_count = (atxType == 1) ? 1040 : 720;
			//drive_infos[drive_number].sector_size = (atxType == 2) ? 256 : 128;
		}
		else
		{
			info |= INFO_HDD;
			drive_infos[drive_number].sector_size = 512;
			drive_infos[drive_number].sector_count = drive_infos[drive_number].file.size / 0x200;
			drive_infos[drive_number].atari_sector_status = 0;
			drive_infos[drive_number].offset = 0;
			info = hdd_partition_scan(&drive_infos[drive_number].file, info);
			if(!info)
			{
				FileClose(&drive_infos[drive_number].file);
				return;
			}
		}
	}
	else if (ext_index == 1) // XEX
	{
		drive_infos[drive_number].custom_loader = 1;
		drive_infos[drive_number].sector_count = 0x173+(drive_infos[drive_number].file.size+(XEX_SECTOR_SIZE-4))/(XEX_SECTOR_SIZE-3);
		drive_infos[drive_number].sector_size = XEX_SECTOR_SIZE;
	}
	else // ATR
	{
		drive_infos[drive_number].offset = 16;
		if(atr_header.wSecSize == 512)
		{
			drive_infos[drive_number].sector_count = (atr_header.wPars | (atr_header.btParsHigh << 16)) / 32;
		}
		else
		{
			drive_infos[drive_number].sector_count = 3 + ((atr_header.wPars | (atr_header.btParsHigh << 16))*16 - 128*3) / atr_header.wSecSize;
		}
		drive_infos[drive_number].sector_size = atr_header.wSecSize;
	}
	//drive_infos[drive_number].file = file;
	drive_infos[drive_number].info = info;
}

typedef void (*CommandHandler)(sio_command_t, int, fileTYPE *, sio_action_t *);

static void handle_speed(sio_command_t command, int drive_number, fileTYPE *file, sio_action_t *action)
{
	(void)file;
	// We should be guaranteed that this is not called in PBI mode,
	// so no need to check the PBI bit
	action->bytes = 1;
	if(drive_infos[drive_number].custom_loader == 2)
	{
		speed_index = 0;
		action->sector_buffer[0] = speeds[speed_index];
	}
	else
	{
		speed_index = command.aux2 ? 0 : ui_speed_index;
		action->sector_buffer[0] = speeds[ui_speed_index];
	}
	action->speed = speeds[speed_index] + 6;
}

static void handle_format(sio_command_t command, int drive_number, fileTYPE *file, sio_action_t *action)
{
	(void)command;
	memset(action->sector_buffer, 0, drive_infos[drive_number].sector_size);
	int i = drive_infos[drive_number].offset;
	FileSeek(file, i, SEEK_SET);
	for (; i != file->size; i += 128)
	{
		FileWriteAdv(file, action->sector_buffer, 128); // TODO check if 128 was written??
	}
	action->sector_buffer[0] = 0xff;
	action->sector_buffer[1] = 0xff;
	action->bytes = drive_infos[drive_number].sector_size;
}

static void handle_read_percom(sio_command_t command, int drive_number, fileTYPE *file, sio_action_t *action)
{
	(void)command;
	(void)file;
	uint16_t total_sectors = drive_infos[drive_number].sector_count;
	memset(action->sector_buffer, 0, 12);
	action->sector_buffer[1] = 0x03;
	action->sector_buffer[6] = drive_infos[drive_number].sector_size >> 8;
	action->sector_buffer[7] = drive_infos[drive_number].sector_size & 0xff;
	action->sector_buffer[8] = 0xff;

	if(!(drive_infos[drive_number].info & INFO_HDD) && (total_sectors == 720 || total_sectors == 1040 || total_sectors == 1440))
	{
		total_sectors = total_sectors / 40;
		if(total_sectors == 36)
		{
			total_sectors = total_sectors / 2;
			action->sector_buffer[4] = 1;
		}
		action->sector_buffer[0] = 40;
		action->sector_buffer[5] = (drive_infos[drive_number].sector_size == 256 || total_sectors == 26) ? 4 : 0;
	}
	else
	{
		action->sector_buffer[0] = 1;
		action->sector_buffer[5] = (drive_infos[drive_number].sector_size == 128) ? 0 : 4;
	}
	action->sector_buffer[2] = total_sectors >> 8;
	action->sector_buffer[3] = total_sectors & 0xff;
	action->bytes = 12;
}

static void handle_force_media_change(sio_command_t command, int drive_number, fileTYPE *file, sio_action_t *action)
{
	(void)command;
	action->respond = 0;
	uint8_t info = hdd_partition_scan(file, INFO_HDD | ((file->mode & O_RDONLY) ? INFO_RO : 0));
	if(info)
	{
		drive_infos[drive_number].info =  info;
	}
	else
	{
		action->success = 0;
	}
}

// TODO device info

/*
void handle_device_status(sio_command_t command, int drive_number, fileTYPE *file, sio_action_t *action)
{
	memset(action->sector_buffer, 0, action->bytes);
	action->sector_buffer[0x0C] = 0x3F;
}
*/

static void handle_get_status(sio_command_t command, int drive_number, fileTYPE *file, sio_action_t *action)
{
	(void)command;
	(void)file;
	uint8_t status = 0x40;

	if(drive_number != MAX_DRIVES)
	{

		status = 0x10; // Motor on;

		if (drive_infos[drive_number].info & INFO_RO)
		{
			status |= 0x08; // write protected; // no write support yet...
		}
		if(drive_infos[drive_number].sector_count != 720)
		{
			status |= 0x80; // medium density - or a strange one...
		}
		if(drive_infos[drive_number].sector_size != 128)
		{
			status |= 0x20; // 256 byte sectors
		}
	}
	action->sector_buffer[0] = status;
	action->sector_buffer[1] = drive_infos[drive_number].atari_sector_status;
	action->sector_buffer[2] = drive_number == MAX_DRIVES ? 0x10 : 0xe0; // What should be our ID?
	// TODO this is PBI stuff
	//action->sector_buffer[3] = drive_number == MAX_DRIVES ? ((unsigned volatile char *)(atari_regbase + 0xDFAD))[0] : 0x00; // version
	action->sector_buffer[3] = 0;
	action->bytes = 4;
}

static int set_location_offset(int drive_number, uint32_t sector, uint32_t *location)
{
	*location = drive_infos[drive_number].offset;
	int sector_size = drive_infos[drive_number].info & (INFO_HDD | INFO_SS) ? 512 : drive_infos[drive_number].sector_size;
	if(drive_infos[drive_number].sector_size == 512 || (drive_infos[drive_number].info & INFO_HDD))
	{
		if(drive_number != MAX_DRIVES)
		{
			sector--;
		}
		*location += sector * sector_size;
	}
	else
	{
		if(sector > 3)
		{
			*location += 128*3 + (sector-4) * sector_size;
		}
		else
		{
			*location = *location + 128 * (sector - 1);
			sector_size = 128;
		}
	}
	return sector_size;
}

static void handle_write(sio_command_t command, int drive_number, fileTYPE *file, sio_action_t *action)
{
	uint8_t pbi = command.deviceId & 0x40;
	uint32_t sector = (command.auxab << 16) | command.aux1 | (command.aux2 << 8);
	int sector_size = 0;
	uint32_t location = 0;

	action->respond = 0;

	sector_size = set_location_offset(drive_number, sector, &location);

	uint8_t checksum = 0;
	uint8_t expchk = 0;

	if(!pbi)
	{
		for (int i = 0; i < sector_size; i++)
		{
			action->sector_buffer[i] = uart_receive();
		}
		checksum = uart_receive();
		expchk = get_checksum(action->sector_buffer, sector_size);
	}
	if (checksum == expchk)
	{
		if(!pbi)
		{
			uart_send('A');
			usleep(850); // TODO DELAY_T2_MIN 850
		}

		FileSeek(file, location, SEEK_SET);
		if(drive_infos[drive_number].info & INFO_SS)
		{
			int step = 512 / drive_infos[drive_number].sector_size;
			sector_size = ATARI_SECTOR_BUFFER_SIZE;
			memset(atari_sector_buffer, 0, sector_size);
			int i = 0;
			int written = 0;
			while(written < sector_size)
			{
				atari_sector_buffer[written] = action->sector_buffer[i++];
				written += step;
			}
			FileWriteAdv(file, atari_sector_buffer, sector_size);
		}
		else
		{
			FileWriteAdv(file, atari_sector_buffer, sector_size);
		}

		int ok = 1;

		if (command.command == 0x57)
		{
			FileSeek(file, location, SEEK_SET);
			FileReadAdv(file, buffer, sector_size);

			for (int i = 0; i < sector_size; i++)
			{
				if (buffer[i] != action->sector_buffer[i]) ok = 0;
			}
		}

		if(pbi)
		{
			action->success = ok;
		}
		else
		{
			usleep(DELAY_T5_MIN);
			uart_send(ok ? 'C' : 'E');
		}
	}
	else
	{
		uart_send('N');
	}
}

const uint8_t cfile_name[] = {'F','I','L','E','N','A','M','E','X','E','X'};

void handle_read(sio_command_t command, int drive_number, fileTYPE *file, sio_action_t *action)
{
	uint32_t sector = (command.auxab << 16) | command.aux1 | (command.aux2<<8);

	uint32_t location = 0;

	if(drive_infos[drive_number].custom_loader == 1) // XEX file
	{
		int file_sectors;

		if (sector <= 2)
		{
			memcpy(action->sector_buffer, &boot_xex_loader[(sector - 1) * XEX_SECTOR_SIZE], XEX_SECTOR_SIZE);
		}
		else if(sector == 0x168)
		{
			file_sectors = drive_infos[drive_number].sector_count;
			int vtoc_sectors = file_sectors / 1024;
			int rem = file_sectors - (vtoc_sectors * 1024);
			if(rem > 943) {
				vtoc_sectors += 2;
			}
			else if(rem)
			{
				vtoc_sectors++;
			}
			if(!(vtoc_sectors % 2))
			{
				vtoc_sectors++;
			}

			file_sectors -= (vtoc_sectors + 12);
			action->sector_buffer[0] = (uint8_t)((vtoc_sectors + 3)/2);
			goto set_number_of_sectors_to_buffer_1_2;
		}
		else if(sector == 0x169)
		{
			file_sectors = drive_infos[drive_number].sector_count - 0x173;

			// TODO proper file name!!!
			memcpy(&action->sector_buffer[5], cfile_name, 11);
			memset(&action->sector_buffer[16], 0, XEX_SECTOR_SIZE-16);

			action->sector_buffer[0]=(file_sectors > 0x28F) ? 0x46 : 0x42;

			action->sector_buffer[3] = 0x71;
			action->sector_buffer[4] = 0x01;
set_number_of_sectors_to_buffer_1_2:
			action->sector_buffer[1] = file_sectors;
			action->sector_buffer[2] = (file_sectors >> 8);
		}
		else if(sector >= 0x171)
		{
			FileSeek(file, (sector - 0x171) * (XEX_SECTOR_SIZE - 3), SEEK_SET);
			int read = FileReadAdv(file, action->sector_buffer, XEX_SECTOR_SIZE - 3);
			if(read < (XEX_SECTOR_SIZE-3))
				sector = 0;
			else
				sector++;

			action->sector_buffer[XEX_SECTOR_SIZE - 3] = (sector >> 8);
			action->sector_buffer[XEX_SECTOR_SIZE - 2] = sector;
			action->sector_buffer[XEX_SECTOR_SIZE - 1] = (uint8_t)read;
		}

		action->bytes = XEX_SECTOR_SIZE;
	}
	else if (drive_infos[drive_number].custom_loader == 2) // ATX
	{
		pre_ce_delay = 0; // Taken care of in loadAtxSector
		// TODO !!!
		// gAtxFile = file;
		// int res = loadAtxSector(drive_number, sector, &drive_infos[drive_number].atari_sector_status);
		int res = 0;
		action->bytes = drive_infos[drive_number].sector_size;
		action->success = (res == 0);
	}
	else
	{
		action->bytes = set_location_offset(drive_number, sector, &location);
		FileSeek(file, location, SEEK_SET);
		if(drive_infos[drive_number].info & INFO_SS)
		{
			uint8_t step = 512 / drive_infos[drive_number].sector_size;
			FileReadAdv(file, atari_sector_buffer, ATARI_SECTOR_BUFFER_SIZE, -1);
			int read = 0;
			int n = 0;
			while(read < ATARI_SECTOR_BUFFER_SIZE)
			{
				action->sector_buffer[n++] = atari_sector_buffer[read];
				read += step;
			}
		}
		else
		{
			FileReadAdv(file, action->sector_buffer, action->bytes, -1);
		}
	}
}

static CommandHandler get_command_handler(sio_command_t command, uint8_t dstats)
{
	CommandHandler res = NULL;
	uint32_t sector = (command.auxab << 16) | command.aux1 | (command.aux2 << 8);
	// The HDD SD card counts sectors from 0
	uint8_t min_sector = (command.deviceId & 0x3F) == 0x20 ? 0 : 1;
	int drive_number = min_sector ? (command.deviceId & 0xf) - 1 : MAX_DRIVES;
	uint8_t pbi = command.deviceId & 0x40;
	uint8_t writable = !(drive_infos[drive_number].info & INFO_RO);

	switch (command.command)
	{
	case 0x3f:
		if(!pbi) res = &handle_speed;
		break;
	case 0x21: // format single
	case 0x22: // format enhanced
		if(writable && !pbi) res = &handle_format;
		break;
	case 0x46:
		if(pbi) res = &handle_force_media_change;
		break;
	case 0x4e: // read percom block
		if(min_sector) res = &handle_read_percom;
		break;
	case 0x53: // get status
		if(!pbi || dstats == 0x40) res = &handle_get_status;
		break;
	case 0x50: // write
	case 0x57: // write with verify
		if (writable && (!pbi || dstats == 0x80) && sector >= min_sector && sector - min_sector < drive_infos[drive_number].sector_count)
			res = &handle_write;
		break;
	case 0x52: // read
		if ((!pbi || dstats == 0x40) && sector >= min_sector && sector - min_sector < drive_infos[drive_number].sector_count)
		{
			if(drive_infos[drive_number].custom_loader == 2) // ATX!
			{
				pre_an_delay = 3220;
			}
			res = &handle_read;
		}
		break;
/* TODO!!!
	case 0x6E: // PBI device info
		if(pbi && dstats == 0x40)
			res = &handle_device_info;
		break;
*/
/*
	case 0xEC: // PBI device status
		if(pbi && dstats == 0x40)
			res = &handleDeviceStatus;
		break;
*/
	}
	return res;
}

int get_command(sio_command_t *cmd)
{
	if(!uart_available()) return 0;

	int success = 1;
	for (int i = 0; i < 5; i++)
	{
		uint16_t data = uart_receive(); // Timeout?
		if (uart_error() || ((data >> 8) != (i+1)))
		{
			success = 0;
			break;
		}
		((uint8_t *)cmd)[i] = (uint8_t)(data & 0xff);
	}

	if(!success) return success;

	uart_receive();

	if (get_checksum((uint8_t *)cmd, 4) == cmd->chksum)
	{
		uart_switch();
	}
	else
	{
		success = 0;
	}

	return success;
}

static void process_command()
{
	sio_command_t command;

	if(!get_command(&command)) { return; }
	command.auxab = 0;

	int drive = (command.deviceId & 0xf) -1;
	if (command.deviceId >= 0x31 && command.deviceId <= 0x34 && drive_infos[drive].sector_size != 512)
	{
		fileTYPE *file = &drive_infos[drive].file;
		if (!file->opened()) return;
		set_a800_reg(REG_DRIVE_LED, 1);

		pre_ce_delay = DELAY_T5_MIN;
		pre_an_delay = DELAY_T2_MIN;

		CommandHandler handle_command = get_command_handler(command, 0);

		usleep(pre_an_delay);

		if (handle_command)
		{
			sio_action_t action;
			action.bytes = 0;
			action.success = 1;
			action.speed = -1;
			action.respond = 1;
			action.sector_buffer = atari_sector_buffer;

			uart_send('A');
			memset(atari_sector_buffer, 0, ATARI_SECTOR_BUFFER_SIZE);

			handle_command(command, drive, file, &action);

			if (action.respond) uart_send_cmpl_and_atari_sector_buffer_and_check_sum(action.sector_buffer, action.bytes, action.success);
			if (action.speed >= 0)
				uart_init(action.speed);
		}
		else
		{
			uart_send('N');
		}
		set_a800_reg(REG_DRIVE_LED, 0);
	}
}

void atari800_set_image(int ext_index, int file_index, const char *name)
{
	if(file_index == 5) // XEX file
	{
		if(name[0] && FileOpen(&xex_file, name))
		{
			xex_file_first_block = 1;
			reboot(1, 1);
			set_a800_reg(REG_CART1_SELECT, 0);
			set_a800_reg(REG_CART2_SELECT, 0);
			uint16_t atari_status1 = get_a800_reg(REG_ATARI_STATUS1);

			atari800_dma_zero(SDRAM_BASE, 0x10000);
			xex_reloc = (atari_status1 & STATUS1_MASK_XEXLOC) ? 1 : XEX_LOADER_LOC;
			xex_loader_base = ATARI_BASE + xex_reloc * 0x100;
			atari800_dma_write(xex_loader, ATARI_BASE + XEX_LOADER_LOC * 0x100, XEX_LOADER_SIZE);
			static uint8_t write_bytes[4];
			if (xex_reloc != XEX_LOADER_LOC)
			{
				write_bytes[0] = xex_reloc;
				atari800_dma_write(write_bytes, ATARI_BASE + XEX_LOADER_LOC * 0x100 + XEX_STACK_FLAG, 1);			
			}

			
			write_bytes[0] = 0;
			atari800_dma_write(write_bytes, ATARI_COLDST, 1);			
			atari800_dma_write(write_bytes, ATARI_GINTLK, 1);			
			write_bytes[0] = 1;
			atari800_dma_write(write_bytes, ATARI_BASICF, 1);			
			write_bytes[0] = 2;
			atari800_dma_write(write_bytes, ATARI_BOOTFLAG, 1);
			write_bytes[0] = XEX_INIT1;
			write_bytes[1] = 0x07;
			atari800_dma_write(write_bytes, ATARI_CASINI, 2);
			write_bytes[0] = 0x71;
			write_bytes[1] = 0xE4;
			atari800_dma_write(write_bytes, ATARI_DOSVEC, 2);

			if(!(atari_status1 & STATUS1_MASK_MODE800))
			{
				write_bytes[0] = 0x5C;
				write_bytes[1] = 0x93;
				write_bytes[2] = 0x25;
				atari800_dma_write(write_bytes, ATARI_PUPBT, 3);						
			}
			
			set_a800_reg(REG_PAUSE, 0);

		}
		else
		{
			FileClose(&xex_file);
		}		
	}
	else if(file_index == 6) // D1: disk image with automatic boot
	{
		if(name[0])
		{
			set_a800_reg(REG_PAUSE, 1);
			set_a800_reg(REG_CART1_SELECT, 0);
			set_a800_reg(REG_CART2_SELECT, 0);
		}
		set_drive_status(0, name, ext_index);
		if(name[0])
		{
			reboot(1, 0);
			set_a800_reg(REG_OPTION_FORCE, 1);
			set_a800_reg(REG_OPTION_FORCE, 0);
		}
	}
	else if(file_index < 4)
	{
		set_drive_status(file_index, name, ext_index);
	}
	// TODO separate entry for HDD mount
}

static void handle_xex()
{
	atari800_dma_read(buffer, xex_loader_base, XEX_READ_STATUS+1);
	
	if(buffer[0] == 0x60)
	{
		if(!buffer[XEX_READ_STATUS])
		{
			uint8_t len_buf[2];
			int read_offset, to_read, read_len;

			len_buf[0] = 0xFF;
			len_buf[1] = 0xFF;

			// Point to rts
			buffer[0] = 0x00;
			buffer[1] = xex_reloc;
			atari800_dma_write(buffer, ATARI_INITAD, 2);
			
			// NOTE! purposely reusing the "mounted" variable
			while(len_buf[0] == 0xFF && len_buf[1] == 0xFF)
			{
				if(FileReadAdv(&xex_file, len_buf, 2) != 2) goto xex_eof;
			}
			read_offset = len_buf[0] | (len_buf[1] << 8);
			if(xex_file_first_block)
			{
				xex_file_first_block = 0;					
				atari800_dma_write(len_buf, ATARI_RUNAD, 2);
			}
			
			if(FileReadAdv(&xex_file, len_buf, 2) != 2) goto xex_eof;
			
			read_len = (len_buf[0] | (len_buf[1] << 8)) + 1 - read_offset;
			if(read_len < 1) goto xex_eof;
			
			to_read = read_len > A800_BUFFER_SIZE ? A800_BUFFER_SIZE : read_len;
			
			while(read_len)
			{
				if(FileReadAdv(&xex_file, buffer, to_read) != to_read) goto xex_eof;
				atari800_dma_write(buffer, ATARI_BASE + read_offset, to_read);
				read_len -= to_read;
				read_offset += to_read;
				to_read = read_len > A800_BUFFER_SIZE ? A800_BUFFER_SIZE : read_len;
			}

			buffer[0] = 0x01;
			atari800_dma_write(buffer, xex_loader_base + XEX_READ_STATUS, 1);
		}
	}
	// Is loader done?
	else if(buffer[0] == 0x5F)
xex_eof:
	{
		buffer[0] = 0xFF;
		atari800_dma_write(buffer, xex_loader_base + XEX_READ_STATUS, 1);
		FileClose(&xex_file);
	}

}

void atari800_poll()
{
	uint16_t atari_status1 = get_a800_reg(REG_ATARI_STATUS1);

	set_a800_reg(REG_PAUSE, atari_status1 & STATUS1_MASK_HALT);

	if (atari_status1 & STATUS1_MASK_SOFTBOOT)
	{
		reboot(0, 0);	
	}
	else if (atari_status1 & STATUS1_MASK_COLDBOOT)
	{
		reboot(1, 0);	
	}
	
	if(xex_file.opened()) handle_xex();

	process_command();
}

void atari800_init()
{
	set_a800_reg(REG_PAUSE, 1);
	cart_matches_total = 0;
	cart_match_car = 0;
	// Try to load bootX.rom ? TODO - limit only to boot3? or require pbibios.rom name?
	if(get_a800_reg(REG_ATARI_STATUS1) & STATUS1_MASK_BOOTX)
	{
		static char mainpath[512];
		const char *home = HomeDir();
		sprintf(mainpath, "%s/boot.rom", home);
		user_io_file_tx(mainpath, 0 << 6);
		sprintf(mainpath, "%s/boot1.rom", home);
		user_io_file_tx(mainpath, 1 << 6);
		sprintf(mainpath, "%s/boot2.rom", home);
		user_io_file_tx(mainpath, 2 << 6);
// At the current state of development PBI rom hangs the Atari
#if 0
		sprintf(mainpath, "%s/boot3.rom", home);
		user_io_file_tx(mainpath, 3 << 6);
#endif
	}
	atari800_reset();
}

void atari800_reset()
{
	set_a800_reg(REG_PAUSE, 1);
	set_a800_reg(REG_CART1_SELECT, 0);
	mounted_cart1_size = 0;
	set_a800_reg(REG_CART2_SELECT, 0);
	set_a800_reg(REG_FREEZER, 0);
	for(int i=0; i <= MAX_DRIVES; i++)
	{
		FileClose(&drive_infos[i].file);
	}
	FileClose(&xex_file);
	speed_index = 0;
	uart_init(speeds[speed_index] + 6);
	reboot(1, 0);
}
