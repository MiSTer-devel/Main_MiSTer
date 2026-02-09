#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../../hardware.h"
#include "../../menu.h"
#include "../../file_io.h"
#include "../../debug.h"
#include "../../user_io.h"
#include "../../fpga_io.h"
#include "atari800.h"

#define A800_GET_REGISTER 0x08
#define A800_SET_REGISTER 0x09

#define REG_CART1_SELECT  0x01
#define REG_CART2_SELECT  0x02
#define REG_RESET         0x03
#define REG_PAUSE         0x04
#define REG_FREEZER       0x05
#define REG_RESET_RNMI    0x06

#define REG_ATARI_STATUS1 0x01

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

static uint8_t buffer[A800_BUFFER_SIZE];

#define XEX_LOADER_LOC          7 // XEX Loader is at $700 by default

#include "xex_loader.h"

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
	reboot(1, 0);
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
	
		//user_io_set_index(99);
		//user_io_set_download(1, stacked ? 0x2900000 : 0x2800000);
		
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

void atari800_set_image(int ext_index, int file_index, const char *name)
{
	(void)ext_index;
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

	if (atari_status1 & STATUS1_MASK_SOFTBOOT)
	{
		reboot(0, 0);	
	}
	else if (atari_status1 & STATUS1_MASK_COLDBOOT)
	{
		reboot(1, 0);	
	}
	
	if(xex_file.opened()) handle_xex();

	set_a800_reg(REG_PAUSE, atari_status1 & STATUS1_MASK_HALT);
}
