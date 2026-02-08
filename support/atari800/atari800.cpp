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

//fileTYPE hdd_image[2] = {};

typedef struct {
	uint8_t cart_type;	// type from CAR header
	char name[16];		// name of type
	uint8_t cart_mode;	// mode used in cartridge emulation
	int size;		// image size in k
} cart_def_t;

static const cart_def_t cart_def[] = 
{
	{ 1,  "Standard 8K    ", TC_MODE_8K,              8 },
	{ 2,  "Standard 16K   ", TC_MODE_16K,            16 },
	// This below is intentional, for 034M carts we fix them
	// (we also need to add 2 extra fake AND-ed banks for 
	// both 043M and 034M)
	{ 3,  "OSS 2 Chip 034M", TC_MODE_OSS_043M,       16 },
	{ 5,  "DB 32K         ", TC_MODE_DB_32,          32 },
	{ 8,  "Williams 64K   ", TC_MODE_WILLIAMS64,     64 },
	{ 9,  "Express 64K    ", TC_MODE_EXPRESS64,      64 },
	{ 10, "Diamond 64K    ", TC_MODE_DIAMOND64,      64 },
	{ 11, "SpartaDOSX 64K ", TC_MODE_SDX64,          64 },
	{ 12, "XEGS 32K       ", TC_MODE_XEGS_32,        32 },
	{ 13, "XEGS 64K (0-7) ", TC_MODE_XEGS_64,        64 },
	{ 14, "XEGS 128K      ", TC_MODE_XEGS_128,      128 },
	{ 15, "OSS 1 Chip 16K ", TC_MODE_OSS_16,         16 },
	{ 17, "Atrax DEC 128K ", TC_MODE_ATRAX128,      128 },
	{ 18, "Bounty Bob     ", TC_MODE_BOUNTY_40,      40 },
	{ 21, "Right 8K       ", TC_MODE_RIGHT_8K,        8 },
	{ 22, "Williams 32K   ", TC_MODE_WILLIAMS32,     32 },
	{ 23, "XEGS 256K      ", TC_MODE_XEGS_256,      256 },
	{ 24, "XEGS 512K      ", TC_MODE_XEGS_512,      512 },
	{ 25, "XEGS 1024K     ", TC_MODE_XEGS_1024,    1024 },
	{ 26, "MegaCart 16K   ", TC_MODE_MEGA_16,        16 },
	{ 27, "MegaCart 32K   ", TC_MODE_MEGA_32,        32 },
	{ 28, "MegaCart 64K   ", TC_MODE_MEGA_64,        64 },
	{ 29, "MegaCart 128K  ", TC_MODE_MEGA_128,      128 },
	{ 30, "MegaCart 256K  ", TC_MODE_MEGA_256,      256 },
	{ 31, "MegaCart 512K  ", TC_MODE_MEGA_512,      512 },
	{ 32, "MegaCart 1024K ", TC_MODE_MEGA_1024,    1024 },
	{ 33, "S.XEGS 32K     ", TC_MODE_SXEGS_32,       32 },
	{ 34, "S.XEGS 64K     ", TC_MODE_SXEGS_64,       64 },
	{ 35, "S.XEGS 128K    ", TC_MODE_SXEGS_128,     128 },
	{ 36, "S.XEGS 256K    ", TC_MODE_SXEGS_256,     256 },
	{ 37, "S.XEGS 512K    ", TC_MODE_SXEGS_512,     512 },
	{ 38, "S.XEGS 1024K   ", TC_MODE_SXEGS_1024,   1024 },
	{ 39, "Phoenix 8K     ", TC_MODE_PHOENIX,         8 },
	{ 40, "Blizzard 16K   ", TC_MODE_BLIZZARD,       16 },
	{ 41, "Atarimax 128K  ", TC_MODE_ATARIMAX1,     128 },
	{ 42, "Atarimax 1MB   ", TC_MODE_ATARIMAX8,    1024 },
	{ 43, "SpartaDOSX 128K", TC_MODE_SDX128,        128 },
	{ 44, "OSS 1 Chip 8K  ", TC_MODE_OSS_8,           8 },
	{ 45, "OSS 2 Chip 043M", TC_MODE_OSS_043M,       16 },
	{ 46, "Blizzard 4K    ", TC_MODE_BLIZZARD_4,      4 },
	{ 47, "AST 32K        ", TC_MODE_AST_32,         32 },
	{ 48, "Atrax SDX 64K  ", TC_MODE_ATRAX_SDX64,    64 },
	{ 49, "Atrax SDX 128K ", TC_MODE_ATRAX_SDX128,  128 },
	{ 50, "TurboSoft 64K  ", TC_MODE_TSOFT_64,       64 },
	{ 51, "TurboSoft 128K ", TC_MODE_TSOFT_128,     128 },
	{ 52, "UltraCart 32K  ", TC_MODE_ULTRA_32,       32 },
	{ 53, "Low Bank XL 8K ", TC_MODE_RIGHT_8K,        8 },
	{ 54, "SIC 128K       ", TC_MODE_SIC_128,       128 },
	{ 55, "SIC 256K       ", TC_MODE_SIC_256,       256 },
	{ 56, "SIC 512K       ", TC_MODE_SIC_512,       512 },
	{ 57, "Standard 2K    ", TC_MODE_2K,              2 },
	{ 58, "Standard 4K    ", TC_MODE_4K,              4 },
	{ 59, "Right 4K       ", TC_MODE_RIGHT_4K,        4 },
	{ 60, "Blizzard 32K   ", TC_MODE_BLIZZARD_32,    32 },
	{ 61, "MegaMax 2048K  ", TC_MODE_MEGAMAX16,    2048 },
	{ 63, "MegaCart 4096K ", TC_MODE_MEGA_4096,    4096 },
	{ 64, "MegaCart 2048K ", TC_MODE_MEGA_2048,    2048 },
	{ 67, "XEGS 64K (8-15)", TC_MODE_XEGS_64_2,      64 },
	{ 68, "Atrax ENC 128K ", TC_MODE_ATRAX_INT128,  128 },
	{ 69, "aDawliah 32K   ", TC_MODE_DAWLI_32,       32 },
	{ 70, "aDawliah 64K   ", TC_MODE_DAWLI_64,       64 },
	{ 75, "Atarimax 1MB NT", TC_MODE_ATARIMAX8_2,  1024 },
	{ 76, "Williams 16K   ", TC_MODE_WILLIAMS16,     16 },
	{ 80, "JRC 64K (LIN)  ", TC_MODE_JRC_LIN_64,     64 },
	{ 83, "SIC+ 1024K     ", TC_MODE_SIC_1024,     1024 },
	{ 86, "XE Multi 8K    ", TC_MODE_XEMULTI_8,       8 },
	{ 87, "XE Multi 16K   ", TC_MODE_XEMULTI_16,     16 },
	{ 88, "XE Multi 32K   ", TC_MODE_XEMULTI_32,     32 },
	{ 89, "XE Multi 64K   ", TC_MODE_XEMULTI_64,     64 },
	{ 90, "XE Multi 128K  ", TC_MODE_XEMULTI_128,   128 },
	{ 91, "XE Multi 256K  ", TC_MODE_XEMULTI_256,   256 },
	{ 92, "XE Multi 512K  ", TC_MODE_XEMULTI_512,   512 },
	{ 93, "XE Multi 1MB   ", TC_MODE_XEMULTI_1024, 1024 },
	{104, "J(atari) 8K    ", TC_MODE_JATARI_8,        8 },
	{105, "J(atari) 16K   ", TC_MODE_JATARI_16,      16 },
	{106, "J(atari) 32K   ", TC_MODE_JATARI_32,      32 },
	{107, "J(atari) 64K   ", TC_MODE_JATARI_64,      64 },
	{108, "J(atari) 128K  ", TC_MODE_JATARI_128,    128 },
	{109, "J(atari) 256K  ", TC_MODE_JATARI_256,    256 },
	{110, "J(atari) 512K  ", TC_MODE_JATARI_512,    512 },
	{111, "J(atari) 1MB   ", TC_MODE_JATARI_1024,  1024 },
	{112, "DCART 512K     ", TC_MODE_DCART,         512 },
	{160, "JRC 64K (INT)  ", TC_MODE_JRC_INT_64,     64 },
	{ 0, "", 0, 0 }
};

#define STATUS1_MASK_SOFTBOOT   0x0001
#define STATUS1_MASK_COLDBOOT   0x0002
#define STATUS1_MASK_HALT       0x0004
#define STATUS1_MASK_MODE800    0x0008
#define STATUS1_MASK_BOOTX      0x0010

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

void reboot(uint8_t cold, uint8_t pause)
{
	static uint8_t buf[1024];
	int i;

	set_a800_reg(REG_PAUSE, 1);
	if (cold)
	{
		set_a800_reg(REG_FREEZER, 0);
		// Initialize the first 64K of SDRAM with a pattern
		for(i = 0; i < 1024; i += 2)
		{
			buf[i] = 0xFF;
			buf[i+1] = 0x00;
		}
		user_io_set_index(99);
		// TODO use defines for memory map
		user_io_set_download(1, 0x2000000);
		for(i = 0; i < 64; i++) user_io_file_tx_data(buf, 1024);
		user_io_set_upload(0);
	}
	else
	{
		// Clean up XEX loader stuff in case of soft reset during loading
		// TODO xex_file = 0;
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

	set_a800_reg(REG_PAUSE, atari_status1 & STATUS1_MASK_HALT);
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

	static uint8_t buf[0x2000];

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
		FileReadAdv(&f, buf, to_read);
		FileClose(&f);
		
		if(cart_match_car)
		{
			// CAR file, presumably, let's check further
			if (buf[0] == 'C' && buf[1] == 'A' && buf[2] == 'R' && buf[3] == 'T')
			{
				while (cart_def[idx].cart_type)
				{
					if(cart_def[idx].cart_type == buf[7])
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
			if(to_read >= 0x2000 && buf[0] == 'S' && buf[1] == 'D' && buf[2] == 'X' && (buf[0x1FF3] == 0xE0 || buf[0x1FF3] == 0xE1))
			{
				cart_matches_idx[0] = -1;
				cart_matches_mode[0] = (buf[0x1FF3] == 0xE1) ? TC_MODE_SDX_SIDE2 : TC_MODE_SDX_U1MB;
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
	static uint8_t buf[LOAD_CHUNK_SIZE];
	static uint8_t buf2[4096];
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
				if (to_read > LOAD_CHUNK_SIZE) to_read = LOAD_CHUNK_SIZE;
				ProgressMessage("Loading", f.name, offset, f.size);
				FileReadAdv(&f, buf, to_read);
				user_io_file_tx_data(buf, to_read);
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

		// Test!		
		//user_io_set_index(99);
		//user_io_set_upload(1, 0x2800000);
		//user_io_file_rx_data(buf2, 4096);
		//user_io_set_upload(0);
		//FileSave("/tmp/atari800.tmp", buf2, 4096);

	}
}

void atari800_open_bios_file(const char* name, unsigned char index)
{
	uint8_t bios_index = (index & 0x3F);
	uint8_t mode800 = get_a800_reg(REG_ATARI_STATUS1) & STATUS1_MASK_MODE800;
	user_io_file_tx(name, index);
	if((mode800 && bios_index == 6) || (!mode800 && (bios_index == 4 || bios_index == 5))) reboot(1, 0);
}
