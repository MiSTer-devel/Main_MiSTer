#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../../hardware.h"
#include "../../menu.h"
#include "../../file_io.h"
#include "../../debug.h"
#include "../../user_io.h"
#include "../../fpga_io.h"
// #include "../../scheduler.h"

#include "atari800.h"
#include "atari8bit_defs.h"

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

// Cart modes from the original ZCPU firmware

// 8k modes (0xA000-$BFFF)
#define TC_MODE_OFF             0x00           // cart disabled
#define TC_MODE_8K              0x01           // 8k banks at $A000
#define TC_MODE_ATARIMAX1       0x02           // 8k using Atarimax 1MBit compatible banking
#define TC_MODE_ATARIMAX8       0x03           // 8k using Atarimax 8MBit compatible banking
#define TC_MODE_ATARIMAX8_2     0x10           // 8k using Atarimax 8MBit compatible banking (new type)
#define TC_MODE_DCART           0x11           // 512K DCart
#define TC_MODE_OSS_16          0x04           // 16k OSS cart, M091 banking
#define TC_MODE_OSS_8           0x05           // 8k OSS cart, M091 banking
#define TC_MODE_OSS_043M        0x06           // 16k OSS cart, 043M banking

#define TC_MODE_SDX64           0x08           // SDX 64k cart, $D5Ex banking
#define TC_MODE_SDX128          0x09           // SDX 128k cart, $D5Ex banking
#define TC_MODE_DIAMOND64       0x0A           // Diamond GOS 64k cart, $D5Dx banking
#define TC_MODE_EXPRESS64       0x0B           // Express 64k cart, $D57x banking

#define TC_MODE_ATRAX128        0x0C           // Atrax 128k cart
#define TC_MODE_WILLIAMS64      0x0D           // Williams 64k cart
#define TC_MODE_WILLIAMS32      0x0E           // Williams 32k cart
#define TC_MODE_WILLIAMS16      0x0F           // Williams 16k cart

// 16k modes (0x8000-$BFFF)
//#define TC_MODE_FLEXI           0x20           // flexi mode
#define TC_MODE_16K             0x21           // 16k banks at $8000-$BFFF
#define TC_MODE_MEGAMAX16       0x22           // MegaMax 16k mode (up to 2MB)
#define TC_MODE_BLIZZARD        0x23           // Blizzard 16k
#define TC_MODE_SIC_128         0x24           // Sic!Cart 128k
#define TC_MODE_SIC_256         0x25           // Sic!Cart 256k
#define TC_MODE_SIC_512         0x26           // Sic!Cart 512k
#define TC_MODE_SIC_1024        0x27           // Sic!Cart+ 1024k

#define TC_MODE_BLIZZARD_4      0x12           // Blizzard 4k
#define TC_MODE_BLIZZARD_32     0x13           // Blizzard 32k
#define TC_MODE_RIGHT_8K	0x14
#define TC_MODE_RIGHT_4K	0x15
#define TC_MODE_2K		0x16
#define TC_MODE_4K		0x17

// J(atari)Cart versions
#define TC_MODE_JATARI_8	0x18
#define TC_MODE_JATARI_16	0x19
#define TC_MODE_JATARI_32	0x1A
#define TC_MODE_JATARI_64	0x1B
#define TC_MODE_JATARI_128	0x1C
#define TC_MODE_JATARI_256	0x1D
#define TC_MODE_JATARI_512	0x1E
#define TC_MODE_JATARI_1024	0x1F

#define TC_MODE_MEGA_16         0x28           // switchable MegaCarts
#define TC_MODE_MEGA_32         0x29
#define TC_MODE_MEGA_64         0x2A
#define TC_MODE_MEGA_128        0x2B
#define TC_MODE_MEGA_256        0x2C
#define TC_MODE_MEGA_512        0x2D
#define TC_MODE_MEGA_1024       0x2E
#define TC_MODE_MEGA_2048       0x2F
#define TC_MODE_MEGA_4096       0x20

#define TC_MODE_XEGS_32         0x30           // non-switchable XEGS carts
#define TC_MODE_XEGS_64         0x31
#define TC_MODE_XEGS_128        0x32
#define TC_MODE_XEGS_256        0x33
#define TC_MODE_XEGS_512        0x34
#define TC_MODE_XEGS_1024       0x35
#define TC_MODE_XEGS_64_2       0x36

#define TC_MODE_SXEGS_32        0x38           // switchable XEGS carts
#define TC_MODE_SXEGS_64        0x39
#define TC_MODE_SXEGS_128       0x3A
#define TC_MODE_SXEGS_256       0x3B
#define TC_MODE_SXEGS_512       0x3C
#define TC_MODE_SXEGS_1024      0x3D

// XE Multicart versions
#define TC_MODE_XEMULTI_8	0x68
#define TC_MODE_XEMULTI_16	0x69
#define TC_MODE_XEMULTI_32	0x6A
#define TC_MODE_XEMULTI_64	0x6B
#define TC_MODE_XEMULTI_128	0x6C
#define TC_MODE_XEMULTI_256	0x6D
#define TC_MODE_XEMULTI_512	0x6E
#define TC_MODE_XEMULTI_1024	0x6F

#define TC_MODE_PHOENIX		0x40
#define TC_MODE_AST_32		0x41
#define TC_MODE_ATRAX_INT128	0x42
#define TC_MODE_ATRAX_SDX64	0x43
#define TC_MODE_ATRAX_SDX128	0x44
#define TC_MODE_TSOFT_64	0x45
#define TC_MODE_TSOFT_128	0x46
#define TC_MODE_ULTRA_32	0x47
#define TC_MODE_DAWLI_32	0x48
#define TC_MODE_DAWLI_64	0x49
#define TC_MODE_JRC_LIN_64	0x4A
#define TC_MODE_JRC_INT_64	0x4B
#define TC_MODE_SDX_SIDE2	0x4C
#define TC_MODE_SDX_U1MB	0x4D
#define TC_MODE_DB_32		0x70
#define TC_MODE_BOUNTY_40	0x73

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

uint8_t a8bit_buffer[BUFFER_SIZE];

#define XEX_LOADER_LOC          7 // XEX Loader is at $700 by default

#include "xex_loader.h"
#include "boot_xex_loader.h"

static fileTYPE xex_file = {};
static uint8_t xex_file_first_block;
static uint8_t xex_reloc;
static uint32_t xex_loader_base;

void set_a8bit_reg(uint8_t reg, uint8_t val)
{
	EnableIO();
	spi8(A800_SET_REGISTER);
	spi_w((reg << 8) | val);
	DisableIO();
}

uint16_t get_a8bit_reg(uint8_t reg)
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

void atari8bit_dma_write(const uint8_t *buf, uint32_t addr, uint32_t len)
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

void atari8bit_dma_zero(uint32_t addr, uint32_t len)
{
	memset(a8bit_buffer, 0, BUFFER_SIZE);
	uint32_t to_write = len > BUFFER_SIZE ? BUFFER_SIZE : len;
	
	user_io_set_index(99);
	user_io_set_download(1, addr);
	while(len)
	{
		user_io_file_tx_data(a8bit_buffer, to_write);
		len -= to_write;
		to_write = len > BUFFER_SIZE ? BUFFER_SIZE : len;
	}
	user_io_set_upload(0);
}

static void reboot(uint8_t cold, uint8_t pause)
{
	int i;

	set_a8bit_reg(REG_PAUSE, 1);
	if (cold)
	{
		set_a8bit_reg(REG_FREEZER, 0);
		// Initialize the first 64K of SDRAM with a pattern
		for(i = 0; i < BUFFER_SIZE; i += 2)
		{
			a8bit_buffer[i] = 0xFF;
			a8bit_buffer[i+1] = 0x00;
		}
		user_io_set_index(99);
		user_io_set_download(1, SDRAM_BASE);
		for(i = 0; i < 0x10000 / BUFFER_SIZE; i++) user_io_file_tx_data(a8bit_buffer, BUFFER_SIZE);
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

	if((get_a8bit_reg(REG_ATARI_STATUS1) & STATUS1_MASK_MODE800) && (!cold || pause))
	{
		set_a8bit_reg(REG_RESET_RNMI, 1);
		set_a8bit_reg(REG_RESET_RNMI, 0);
	}
	else
	{
		set_a8bit_reg(REG_RESET, 1);
		set_a8bit_reg(REG_RESET, 0);
	}

	if(cold)
	{
		set_a8bit_reg(REG_FREEZER, 1);
	}
	set_a8bit_reg(REG_PAUSE, pause);
}

static void uart_init(uint8_t divisor)
{
	set_a8bit_reg(REG_SIO_SETDIV, (divisor << 1) + 1);
}

static uint8_t uart_full()
{
	uint16_t r = get_a800_reg2(A800_SIO_TX_STAT);
	return r & 0x200 ? 1 : 0;
}

static void uart_send(uint8_t data)
{
	while(uart_full())
	{
//#ifdef USE_SCHEDULER // TODO ?
//		scheduler_yield();
//#endif
	}
	set_a8bit_reg(REG_SIO_TX, data);
}

static uint8_t uart_available()
{
	return !((get_a800_reg2(A800_SIO_RX_STAT) >> 8) & 0x1);
}

static uint16_t uart_receive()
{
	while(!uart_available())
	{
//#ifdef USE_SCHEDULER // TODO ?
//		scheduler_yield();
//#endif
	}
	return get_a800_reg2(A800_SIO_RX);
}

static void uart_switch()
{
	// Working with this for a while now, I still have no clue what it does... :/
	set_a8bit_reg(REG_SIO_SETDIV, (uint8_t)(get_a800_reg2(A800_SIO_GETDIV)-1));
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
	set_a8bit_reg(stacked ? REG_CART2_SELECT : REG_CART1_SELECT, 0);
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
		FileReadAdv(&f, a8bit_buffer, to_read);
		FileClose(&f);
		
		if(cart_match_car)
		{
			// CAR file, presumably, let's check further
			if (a8bit_buffer[0] == 'C' && a8bit_buffer[1] == 'A' && a8bit_buffer[2] == 'R' && a8bit_buffer[3] == 'T')
			{
				while (cart_def[idx].cart_type)
				{
					if(cart_def[idx].cart_type == a8bit_buffer[7])
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
			if(to_read >= 0x2000 && a8bit_buffer[0] == 'S' && a8bit_buffer[1] == 'D' && a8bit_buffer[2] == 'X' && (a8bit_buffer[0x1FF3] == 0xE0 || a8bit_buffer[0x1FF3] == 0xE1))
			{
				cart_matches_idx[0] = -1;
				cart_matches_mode[0] = (a8bit_buffer[0x1FF3] == 0xE1) ? TC_MODE_SDX_SIDE2 : TC_MODE_SDX_U1MB;
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
	uint8_t *buf = &a8bit_buffer[0];
	uint8_t *buf2 = &a8bit_buffer[4096];
	fileTYPE f = {};
	int offset = cart_match_car ? 16 : 0;
	uint8_t cart_type = cart_def[cart_matches_idx[match_index]].cart_type;

	if (FileOpen(&f, name))
	{
		set_a8bit_reg(REG_PAUSE, 1);
		set_a8bit_reg(REG_CART2_SELECT, 0);
		if(!stacked) set_a8bit_reg(REG_CART1_SELECT, 0);

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
				if (to_read > BUFFER_SIZE) to_read = BUFFER_SIZE;
				ProgressMessage("Loading", f.name, offset, f.size);
				FileReadAdv(&f, a8bit_buffer, to_read);
				user_io_file_tx_data(a8bit_buffer, to_read);
				offset += to_read;
			}
		}
		FileClose(&f);
		user_io_set_download(0);
		ProgressMessage(0, 0, 0, 0);
		set_a8bit_reg(stacked ? REG_CART2_SELECT : REG_CART1_SELECT, cart_matches_mode[match_index]);
		if(!stacked)
		{
			mounted_cart1_size = cart_match_car ? f.size - 16 : f.size;
		}

		if(!stacked || (get_a8bit_reg(REG_ATARI_STATUS1) & STATUS1_MASK_MODE800))
		{
			reboot(1, 0);
		}
	}
}

void atari800_open_bios_file(const char* name, unsigned char index)
{
	uint8_t bios_index = (index & 0x3F);
	uint16_t mode800 = get_a8bit_reg(REG_ATARI_STATUS1) & STATUS1_MASK_MODE800;
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

typedef struct {
	uint8_t signature[4];
	uint16_t version;
	uint16_t minVersion;
	uint16_t creator;
	uint16_t creatorVersion;
	uint32_t flags;
	uint16_t imageType;
	uint8_t density;
	uint8_t reserved0;
	uint32_t imageId;
	uint16_t imageVersion;
	uint16_t reserved1;
	uint32_t startData;
	uint32_t endData;
	uint8_t reserved2[12];
} __attribute__((packed)) atxFileHeader;

typedef struct {
	uint32_t size;
	uint16_t type;
	uint16_t reserved0;
	uint8_t trackNumber;
	uint8_t reserved1;
	uint16_t sectorCount;
	uint16_t rate;
	uint16_t reserved3;
	uint32_t flags;
	uint32_t headerSize;
	uint8_t reserved4[8];
} __attribute__((packed)) atxTrackHeader;

typedef struct {
	uint32_t next;
	uint16_t type;
	uint16_t pad0;
} __attribute__((packed)) atxSectorListHeader;

typedef struct {
	uint8_t number;
	uint8_t status;
	uint16_t timev;
	uint32_t data;
} __attribute__((packed)) atxSectorHeader;

typedef struct {
	uint32_t size;
	uint8_t type;
	uint8_t sectorIndex;
	uint16_t data;
} __attribute__((packed)) atxTrackChunk;

#define ATX_VERSION		0x01

// number of angular units in a full disk rotation
#define AU_FULL_ROTATION         26042

#define US_CS_CALC_1050 270 // According to Altirra
#define US_CS_CALC_810 5136 // According to Altirra

#define US_TRACK_STEP_810 5300 // number of microseconds drive takes to step 1 track
#define US_TRACK_STEP_1050 20120 // According to Avery / Altirra
#define US_HEAD_SETTLE_1050 20000
#define US_HEAD_SETTLE_810 10000

#define US_3FAKE_ROT_810 1566000
#define US_2FAKE_ROT_1050 942000

// mask for checking FDC status "data lost" bit
#define MASK_FDC_DLOST           0x04
// mask for checking FDC status "missing" bit
#define MASK_FDC_MISSING         0x10
// mask for checking FDC status extended data bit
#define MASK_EXTENDED_DATA       0x40

#define MASK_FDC_BUSY            0x01
#define MASK_FDC_DRQ             0x02
#define MASK_FDC_CRC             0x08
#define MASK_FDC_REC             0x20
#define MASK_FDC_WP              0x40
#define MASK_RESERVED            0x80

#define MAX_RETRIES_1050         2
#define MAX_RETRIES_810          4

#define MAX_TRACK                42

enum atx_density { atx_single, atx_medium, atx_double };

#define NUM_ATX_DRIVES 4

#define XEX_SECTOR_SIZE 128
#define ATARI_SECTOR_BUFFER_SIZE 512

static uint8_t atari_sector_buffer[ATARI_SECTOR_BUFFER_SIZE];
static uint32_t pre_ce_delay;
static uint32_t pre_an_delay;

#define DELAY_T2_MIN      100 /* BiboDos needs at least 50us delay before ACK */
#define DELAY_T5_MIN      600 /* the QMEG OS needs at least 300usec delay between ACK and complete */
#define DELAY_T3_PERIPH   150 /* QMEG OS 3 needs a delay of 150usec between complete and data */

struct {
	uint16_t bytesPerSector; // number of bytes per sector
	uint8_t sectorsPerTrack; // number of sectors in each track
	uint32_t trackOffset[MAX_TRACK]; // pre-calculated info for each track and drive
	uint8_t currentHeadTrack;
	uint8_t density;
} atx_info[NUM_ATX_DRIVES];

struct {
	uint64_t stamp;
	uint16_t angle;
} headPosition;

static uint64_t get_us(uint64_t offset)
{
	struct timespec tp;

	clock_gettime(CLOCK_BOOTTIME, &tp);

	uint64_t res;

	res = tp.tv_sec;
	res *= 1000000;
	res += (tp.tv_nsec / 1000);

	return (uint64_t)(res + offset);
}

static uint64_t check_us(uint64_t time)
{
	return (!time) || (get_us(0) >= time);
}

static void wait_us(uint64_t time)
{
	time = get_us(time);
	while (!check_us(time));
}

static void getCurrentHeadPosition()
{
	uint64_t s = get_us(0);
	headPosition.stamp = s;
	headPosition.angle = (uint16_t)((s >> 3) % AU_FULL_ROTATION);
}

static void wait_from_stamp(uint32_t us_delay)
{
	uint32_t t = get_us(0) - headPosition.stamp;
	t = us_delay - t;
	// If, for whatever reason, we are already too late, just skip
	if(t <= us_delay) wait_us(t);
}

#define ATX_FILE_ACCESS_READ    1
#define ATX_FILE_ACCESS_WRITE   2

int atx_file_access(int drv_num, int type, int offset, int len)
{
	(void)type; // ATM we only support reading, but writing is potentially possible

	FileSeek(&drive_infos[drv_num].file, offset, SEEK_SET);

	return len == FileReadAdv(&drive_infos[drv_num].file, atari_sector_buffer, len);
}

static uint8_t loadAtxFile(int drv_num)
{
	atxFileHeader *fileHeader;
	atxTrackHeader *trackHeader;
	uint8_t r = 0;

	if(!atx_file_access(drv_num, ATX_FILE_ACCESS_READ, 0, sizeof(atxFileHeader))) return r;

	// validate the ATX file header
	fileHeader = (atxFileHeader *) atari_sector_buffer;
	if (fileHeader->signature[0] != 'A' || fileHeader->signature[1] != 'T' ||
		fileHeader->signature[2] != '8' || fileHeader->signature[3] != 'X' ||
		fileHeader->version != ATX_VERSION || fileHeader->minVersion != ATX_VERSION) return r;

	r = fileHeader->density;

	// enhanced density is 26 sectors per track, single and double density are 18
	atx_info[drv_num].sectorsPerTrack = (r == atx_medium) ? 26 : 18;
	// single and enhanced density are 128 bytes per sector, double density is 256
	atx_info[drv_num].bytesPerSector = (r == atx_double) ? 256 : 128;
	atx_info[drv_num].density = r;
	atx_info[drv_num].currentHeadTrack = 0;

	// calculate track offsets
	uint32_t startOffset = fileHeader->startData;

	for(int track = 0; track < MAX_TRACK ; track++) {
		if (!atx_file_access(drv_num, ATX_FILE_ACCESS_READ, startOffset, sizeof(atxTrackHeader))) break;
		trackHeader = (atxTrackHeader *) atari_sector_buffer;
		atx_info[drv_num].trackOffset[track] = startOffset;
		startOffset += trackHeader->size;
	}

	return r;
}

// Return 0 on full success, 1 on "Atari disk problem" (may have data)
// -1 on internal storage problem (corrupt ATX) 
static int loadAtxSector(int drv_num, uint16_t num, uint8_t *status)
{

	atxTrackHeader *trackHeader;
	atxSectorListHeader *slHeader;
	atxSectorHeader *sectorHeader;
	atxTrackChunk *extSectorData;

	int r = 1;
	
	uint8_t is1050 = get_a8bit_reg(REG_ATARI_STATUS1) & STATUS1_MASK_ATX1050 ? 1 : 0;

	// calculate track and relative sector number from the absolute sector number
	uint8_t tgtTrackNumber = (num - 1) / atx_info[drv_num].sectorsPerTrack;
	uint8_t tgtSectorNumber = (num - 1) % atx_info[drv_num].sectorsPerTrack + 1;

	// set initial status (in case the target sector is not found)
	*status = MASK_FDC_MISSING;

	uint16_t atxSectorSize = atx_info[drv_num].bytesPerSector;

	// delay for track stepping if needed
	int diff = tgtTrackNumber - atx_info[drv_num].currentHeadTrack;
	if (diff)
	{
		if (diff > 0)
		{
			diff += (is1050 ? 1 : 0);
		}
		else
		{
			diff = -diff;
		}
		wait_us(is1050 ? (diff*US_TRACK_STEP_1050 + US_HEAD_SETTLE_1050) : (diff*US_TRACK_STEP_810 + US_HEAD_SETTLE_810));
	}

	getCurrentHeadPosition();

	// set new head track position
	atx_info[drv_num].currentHeadTrack = tgtTrackNumber;
	uint16_t sectorCount = 0;
	// read the track header
	uint32_t currentFileOffset = atx_info[drv_num].trackOffset[tgtTrackNumber];
	
	if (currentFileOffset)
	{
		if(atx_file_access(drv_num, ATX_FILE_ACCESS_READ, currentFileOffset, sizeof(atxTrackHeader)))
		{
			trackHeader = (atxTrackHeader *) atari_sector_buffer;
			sectorCount = trackHeader->sectorCount;
		}
		else
		{
			r = -1;
		}
	}

	if (trackHeader->trackNumber != tgtTrackNumber || atx_info[drv_num].density != ((trackHeader->flags & 0x2) ? atx_medium : atx_single))
	{
		sectorCount = 0;
	}

	uint32_t trackHeaderSize = trackHeader->headerSize;

	if (sectorCount)
	{
		currentFileOffset += trackHeaderSize;
		if(atx_file_access(drv_num, ATX_FILE_ACCESS_READ, currentFileOffset, sizeof(atxSectorListHeader)))
		{
			slHeader = (atxSectorListHeader *) atari_sector_buffer;
			// sector list header is variable length, so skip any extra header bytes that may be present
			currentFileOffset += slHeader->next - sectorCount * sizeof(atxSectorHeader);
		}
		else
		{
			sectorCount = 0;
			r = -1;
		}
	}

	uint32_t tgtSectorOffset;        // the offset of the target sector data
	int16_t weakOffset;

	uint8_t retries = is1050 ? MAX_RETRIES_1050 : MAX_RETRIES_810;

	uint32_t retryOffset = currentFileOffset;
	uint16_t extSectorSize;

	while (retries > 0)
	{
		retries--;
		currentFileOffset = retryOffset;
		int pTT;
		uint16_t tgtSectorIndex = 0;         // the index of the target sector within the sector list
		tgtSectorOffset = 0;
		weakOffset = -1;
		// iterate through all sector headers to find the target sector

		if(sectorCount)
		{
			for (int i = 0; i < sectorCount; i++)
			{
				if(!atx_file_access(drv_num, ATX_FILE_ACCESS_READ, currentFileOffset, sizeof(atxSectorHeader)))
				{
					r = -1;
					break;
				}
				sectorHeader = (atxSectorHeader *)atari_sector_buffer;

				// if the sector is not flagged as missing and its number matches the one we're looking for...
				if (sectorHeader->number == tgtSectorNumber)
				{
					if(sectorHeader->status & MASK_FDC_MISSING)
					{
						currentFileOffset += sizeof(atxSectorHeader);
						continue;
					}
					// check if it's the next sector that the head would encounter angularly...
					int tt = sectorHeader->timev - headPosition.angle;
					if (!tgtSectorOffset || (tt > 0 && pTT <= 0) || (tt > 0 && pTT > 0 && tt < pTT) || (tt <= 0 && pTT <= 0 && tt < pTT))
					{
						pTT = tt;
						*status = sectorHeader->status;
						tgtSectorIndex = i;
						tgtSectorOffset = sectorHeader->data;
					}
				}
				currentFileOffset += sizeof(atxSectorHeader);
			}
		}
	
		uint16_t actSectorSize = atxSectorSize;
		extSectorSize = 0;
		// if an extended data record exists for this track, iterate through all track chunks to search
		// for those records (note that we stop looking for chunks when we hit the 8-byte terminator; length == 0)
		if (*status & MASK_EXTENDED_DATA)
		{
			currentFileOffset = atx_info[drv_num].trackOffset[tgtTrackNumber] + trackHeaderSize;
			do {
				if(!atx_file_access(drv_num, ATX_FILE_ACCESS_READ, currentFileOffset, sizeof(atxTrackChunk)))
				{
					r = -1;
					break;
				}
				extSectorData = (atxTrackChunk *) atari_sector_buffer;
				if (extSectorData->size)
				{
					// if the target sector has a weak data flag, grab the start weak offset within the sector data
					if (extSectorData->sectorIndex == tgtSectorIndex)
					{
						if(extSectorData->type == 0x10)
						{ // weak sector
							weakOffset = extSectorData->data;
						}
						else if(extSectorData->type == 0x11)
						{ // extended sector
							extSectorSize = 128 << extSectorData->data;
							// 1050 waits for long sectors, 810 does not
							if(is1050 ? (extSectorSize > actSectorSize) : (extSectorSize < actSectorSize))
							{
								actSectorSize = extSectorSize;
							}
						}
					}
					currentFileOffset += extSectorData->size;
				}
			} while (extSectorData->size);
		}

		if (tgtSectorOffset)
		{
			if(!atx_file_access(drv_num, ATX_FILE_ACCESS_READ, atx_info[drv_num].trackOffset[tgtTrackNumber] + tgtSectorOffset, atxSectorSize))
			{
				r = -1;
				tgtSectorOffset = 0;
			}

			uint16_t au_one_sector_read = (23+actSectorSize)*(atx_info[drv_num].density == atx_single ? 8 : 4)+2;
			// We will need to circulate around the disk one more time if we are re-reading the just written sector	    
			wait_from_stamp((au_one_sector_read + pTT + (pTT > 0 ? 0 : AU_FULL_ROTATION))*8);

			if(*status)
			{		    
				// This is according to Altirra, but it breaks DjayBee's test J in 1050 mode?!
				// wait_us(is1050 ? (US_TRACK_STEP_1050+US_HEAD_SETTLE_1050) : (AU_FULL_ROTATION*8));
				// This is what seems to work:
				wait_us(AU_FULL_ROTATION*8);
			}
		}
		else
		{
			// No matching sector found at all or the track does not match the disk density
			wait_from_stamp(is1050 ? US_2FAKE_ROT_1050 : US_3FAKE_ROT_810);
			if(is1050 || retries == 2)
			{
				// Repositioning the head for the target track
				if(!is1050)
				{
					wait_us((43+tgtTrackNumber)*US_TRACK_STEP_810+US_HEAD_SETTLE_810);
				}
				else if(tgtTrackNumber)
				{
					wait_us((2*tgtTrackNumber+1)*US_TRACK_STEP_1050+US_HEAD_SETTLE_1050);
				}
			}
		}
	
		getCurrentHeadPosition();

		if(!*status || r < 0) break;
	}

	*status &= ~(MASK_RESERVED | MASK_EXTENDED_DATA);

	if (*status & MASK_FDC_DLOST)
	{
		if(is1050)
		{
			*status |= MASK_FDC_DRQ;
		}
		else
		{
			*status &= ~(MASK_FDC_DLOST | MASK_FDC_CRC);
			*status |= MASK_FDC_BUSY;
		}
	}
	if(!is1050 && (*status & MASK_FDC_REC)) *status |= MASK_FDC_WP;

	if (tgtSectorOffset && !*status && r >= 0) r = 0;

	// if a weak offset is defined, randomize the appropriate data
	if (weakOffset > -1)
	{
		for (int i = weakOffset; i < atxSectorSize; i++)
		{
			atari_sector_buffer[i] = rand();
		}
	}

	wait_from_stamp(is1050 ? US_CS_CALC_1050 : US_CS_CALC_810);
	// There is no file reading since last time stamp, so the alternative
	// below is probably equally good
	//wait_us(is1050 ? US_CS_CALC_1050 : US_CS_CALC_810);

	// the Atari expects an inverted FDC status byte
	*status = ~(*status);

	// return the number of bytes read
	return r;
}

static int speed_index = 0;
static const uint8_t speeds[] = {0x28, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00};

#define ui_speed_index ((get_a8bit_reg(REG_ATARI_STATUS1) >> 8) & 0x07)

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
	while(len-- > 0) uart_send(*buf++);
}

static void uart_send_cmpl_and_atari_sector_buffer_and_check_sum(uint8_t *buf, int len, int success)
{
	wait_us(pre_ce_delay);
	uart_send(success ? 'C' : 'E');
	wait_us(DELAY_T3_PERIPH);
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

	if(atari_sector_buffer[0] == 0x10) info |= INFO_META;
	else if(atari_sector_buffer[0]) return 0;
	
	info |= (atari_sector_buffer[4] & 0xF);
	for(int pidx = 0; pidx < 15; pidx++)
	{
		int i = (pidx + 1)*16;
		if(!atari_sector_buffer[i])
		{
			// empty slot
			if(drive_infos[pidx].info & INFO_HDD) drive_infos[pidx].info = 0;
		}
		else
		{
			drive_infos[pidx].info = (info & 0xC0) | (atari_sector_buffer[i] & 0x40 ? INFO_META : 0) |
				((atari_sector_buffer[i] & 0x30) || (atari_sector_buffer[i+12] & 0x80) ? INFO_RO : 0);
			atari_sector_buffer[i] &= 0x8F;
			if(atari_sector_buffer[i] > 3 || (atari_sector_buffer[i+1] != 0x00 && atari_sector_buffer[i+1] != 0x03) || !(atari_sector_buffer[i+12] & 0x40))
			{
				drive_infos[pidx].info = 0; // TODO ?! Is this enough to fully disable the drive?
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
			}
		}
	}
	return info;
}

static void set_drive_status(int drive_number, const char *name, uint8_t ext_index)
{
	uint8_t info = 0;
	atr_header_t atr_header;
	
	if(drive_number == 4) drive_number = MAX_DRIVES;

	if(drive_number < MAX_DRIVES && (drive_infos[drive_number].info & INFO_HDD)) return;

	if(!name[0])
	{
		FileClose(&drive_infos[drive_number].file);
		if(drive_number == MAX_DRIVES)
		{
			for(int i = 0; i < MAX_DRIVES; i++)
			{
				if(drive_infos[i].info & INFO_HDD) drive_infos[i].info = 0;
			}
		}
		return;
	}

	uint8_t read_only = (ext_index == 1) || (ext_index == 3) ||
		!FileCanWrite(name) || (get_a8bit_reg(REG_ATARI_STATUS1) & STATUS1_MASK_RDONLY);


	if(!FileOpenEx(&drive_infos[drive_number].file, name, read_only ? O_RDONLY : (O_RDWR | O_SYNC))) return;

	if(read_only) info |= INFO_RO;

	if(drive_number < MAX_DRIVES && ext_index == 0) // ATR only
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
	else if (ext_index == 3) // ATX
	{
		drive_infos[drive_number].custom_loader = 2;
		uint8_t atxType = loadAtxFile(drive_number);
		drive_infos[drive_number].sector_count = (atxType == atx_medium) ? 1040 : 720;
		drive_infos[drive_number].sector_size = (atxType == atx_double) ? 256 : 128;
	}
	else if (ext_index == 1) // XEX
	{
		drive_infos[drive_number].custom_loader = 1;
		drive_infos[drive_number].sector_count = 0x173+(drive_infos[drive_number].file.size+(XEX_SECTOR_SIZE-4))/(XEX_SECTOR_SIZE-3);
		drive_infos[drive_number].sector_size = XEX_SECTOR_SIZE;
	}
	else // ATR or IMG
	{
		if(drive_number < MAX_DRIVES)
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
		FileWriteAdv(file, action->sector_buffer, 128);
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

static void handle_device_info(sio_command_t command, int drive_number, fileTYPE *file, sio_action_t *action)
{
	(void)command;
	memset(action->sector_buffer, 0, action->bytes);
	action->sector_buffer[0] = 1;
	action->sector_buffer[2] = 1;
	action->sector_buffer[6] = drive_infos[drive_number].sector_size;
	action->sector_buffer[7] = drive_infos[drive_number].sector_size >> 8;
	action->sector_buffer[8] = drive_infos[drive_number].sector_count;
	action->sector_buffer[9] = drive_infos[drive_number].sector_count >> 8;
	action->sector_buffer[10] = drive_infos[drive_number].sector_count >> 16;
	action->sector_buffer[11] = drive_infos[drive_number].sector_count >> 24;
	if(drive_number == MAX_DRIVES)
	{
		atari800_dma_read(a8bit_buffer, ATARI_BASE + 0xDFA0, 0x60);
		memcpy(&action->sector_buffer[0x10], &a8bit_buffer[0x0F], a8bit_buffer[0x0E]);
		memcpy(&action->sector_buffer[0x38], &a8bit_buffer[0x38], a8bit_buffer[0x37]);
	}
	else
	{
		action->sector_buffer[3] = drive_infos[drive_number].partition_id;
		action->sector_buffer[4] = drive_infos[drive_number].partition_id >> 8;
		// TODO This made me realize that we are limited in the SD image size
		action->sector_buffer[12] = drive_infos[drive_number].offset >> 9;
		action->sector_buffer[13] = drive_infos[drive_number].offset >> 17;
		action->sector_buffer[14] = drive_infos[drive_number].offset >> 25;
		if(drive_infos[drive_number].info & INFO_META)
		{
			FileSeek(file, drive_infos[drive_number].meta_offset + 16, SEEK_SET);
			if(FileReadAdv(file, &action->sector_buffer[0x10], 40) != 40) action->success = 0;
		}
	}
}

#if 0
void handle_device_status(sio_command_t command, int drive_number, fileTYPE *file, sio_action_t *action)
{
	memset(action->sector_buffer, 0, action->bytes);
	action->sector_buffer[0x0C] = 0x3F;
}
#endif

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
	if(drive_number == MAX_DRIVES)
	{
		atari800_dma_read(&action->sector_buffer[3], ATARI_BASE + 0xDFAD, 1);
	}
	else
	{
		action->sector_buffer[3] = 0;
	}
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
			wait_us(850); // was DELAY_T2_MIN
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
			FileReadAdv(file, a8bit_buffer, sector_size);

			for (int i = 0; i < sector_size; i++)
			{
				if (a8bit_buffer[i] != action->sector_buffer[i]) ok = 0;
			}
		}

		if(pbi)
		{
			action->success = ok;
		}
		else
		{
			wait_us(DELAY_T5_MIN);
			uart_send(ok ? 'C' : 'E');
		}
	}
	else
	{
		uart_send('N');
	}
}

static void handle_read(sio_command_t command, int drive_number, fileTYPE *file, sio_action_t *action)
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
			{
				memset(&action->sector_buffer[5], ' ', 8);
				int si = 0;
				for(int i = 0; i < 11; i++)
				{
					char c = drive_infos[drive_number].file.name[si];
					if(c == '.')
					{
						i = 7;
					}
					else
					{
						if(c >= 'a' && c <= 'z') c -= 32;
						else if(c < 'A' || c > 'Z') c = '@';
						action->sector_buffer[5+i] = c;
					}
					si = i == 7 ? strlen(drive_infos[drive_number].file.name) - 3 : si + 1;
				}
			}
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
			sector = (read < (XEX_SECTOR_SIZE - 3)) ? 0 : sector + 1;
			action->sector_buffer[XEX_SECTOR_SIZE - 3] = (sector >> 8);
			action->sector_buffer[XEX_SECTOR_SIZE - 2] = sector;
			action->sector_buffer[XEX_SECTOR_SIZE - 1] = (uint8_t)read;
		}

		action->bytes = XEX_SECTOR_SIZE;
	}
	else if (drive_infos[drive_number].custom_loader == 2) // ATX
	{
		pre_ce_delay = 0; // Taken care of in loadAtxSector
		int res = loadAtxSector(drive_number, sector, &drive_infos[drive_number].atari_sector_status);
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

	case 0x6E: // PBI device info
		if(pbi && dstats == 0x40)
			res = &handle_device_info;
		break;
#if 0
	case 0xEC: // PBI device status
		if(pbi && dstats == 0x40)
			res = &handle_device_status;
		break;
#endif
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
		set_a8bit_reg(REG_DRIVE_LED, 1);

		pre_ce_delay = DELAY_T5_MIN;
		pre_an_delay = DELAY_T2_MIN;

		CommandHandler handle_command = get_command_handler(command, 0);

		wait_us(pre_an_delay);

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
		set_a8bit_reg(REG_DRIVE_LED, 0);
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
			set_a8bit_reg(REG_CART1_SELECT, 0);
			set_a8bit_reg(REG_CART2_SELECT, 0);
			uint16_t atari_status1 = get_a8bit_reg(REG_ATARI_STATUS1);

			atari8bit_dma_zero(SDRAM_BASE, 0x10000);
			xex_reloc = (atari_status1 & STATUS1_MASK_XEXLOC) ? 1 : XEX_LOADER_LOC;
			xex_loader_base = ATARI_BASE + xex_reloc * 0x100;
			atari8bit_dma_write(xex_loader, ATARI_BASE + XEX_LOADER_LOC * 0x100, (uint32_t)sizeof(xex_loader));
			static uint8_t write_bytes[4];
			if (xex_reloc != XEX_LOADER_LOC)
			{
				write_bytes[0] = xex_reloc;
				atari8bit_dma_write(write_bytes, ATARI_BASE + XEX_LOADER_LOC * 0x100 + XEX_STACK_FLAG, 1);			
			}

			
			write_bytes[0] = 0;
			atari8bit_dma_write(write_bytes, ATARI_COLDST, 1);			
			atari8bit_dma_write(write_bytes, ATARI_GINTLK, 1);			
			write_bytes[0] = 1;
			atari8bit_dma_write(write_bytes, ATARI_BASICF, 1);			
			write_bytes[0] = 2;
			atari8bit_dma_write(write_bytes, ATARI_BOOTFLAG, 1);
			write_bytes[0] = XEX_INIT1;
			write_bytes[1] = 0x07;
			atari8bit_dma_write(write_bytes, ATARI_CASINI, 2);
			write_bytes[0] = 0x71;
			write_bytes[1] = 0xE4;
			atari8bit_dma_write(write_bytes, ATARI_DOSVEC, 2);

			if(!(atari_status1 & STATUS1_MASK_MODE800))
			{
				write_bytes[0] = 0x5C;
				write_bytes[1] = 0x93;
				write_bytes[2] = 0x25;
				atari8bit_dma_write(write_bytes, ATARI_PUPBT, 3);						
			}
			
			set_a8bit_reg(REG_PAUSE, 0);

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
			set_a8bit_reg(REG_PAUSE, 1);
			set_a8bit_reg(REG_CART1_SELECT, 0);
			set_a8bit_reg(REG_CART2_SELECT, 0);
		}
		set_drive_status(0, name, ext_index);
		if(name[0])
		{
			reboot(1, 0);
			set_a8bit_reg(REG_OPTION_FORCE, 1);
			set_a8bit_reg(REG_OPTION_FORCE, 0);
		}
	}
	else if(file_index < 5) // 5 is the HDD and requires slightly different handling
	{
		set_drive_status(file_index, name, ext_index);
	}
}

static uint8_t process_command_pbi(const uint8_t *drives_config)
{
	// We are more or less guranteed to serve the correct device id and 
	// drive unit number by now, no need to check here
	// mark a bit (0x40) in deviceId to indicate this is PBI, this is not the same
	// as the XDCB bit (0x80)

	set_a8bit_reg(REG_DRIVE_LED, 1);

	static uint8_t iocb[16];
	atari800_dma_read(iocb, ATARI_BASE + 0x300, 16);

	sio_command_t command;

	uint8_t sd_device = (iocb[0] & 0x7F) == 0x20;
	int drive = iocb[1] - 1;
	command.deviceId = (iocb[0] + drive) | 0x40; // ddevic + dunit - 1 plus PBI marker

	/*
	  This piece of admitedely contrived logic takes care of diverting or not further processing
	  to SIO routines. The procedure is not exactly the same as on, say, Ultimate 1MB PBI BIOS, nor 
	  it is strictly according to the PBI API requirements, here we (safely?) assume there are no
	  other PBI devices (and hence ROM BIOSes), so this allows us to take some shortcuts (which also
	  speeds up things on the Atari / SDX side).
	*/

	uint8_t mode = (sd_device && !drive) ? 1 : ((!sd_device && drive < 4) ? drives_config[drive] : ((iocb[0] & 0x80) >> 7));

	if(sd_device && !drive) drive = MAX_DRIVES;

	fileTYPE *file = &drive_infos[drive < MAX_DRIVES && (drive_infos[drive].info & INFO_HDD) ? MAX_DRIVES : drive].file;
	
	if(file->opened())
	{
		if(drive_infos[drive].info & INFO_HDD) // The type should be then ATR
		{
			if(!sd_device) mode = 1;
		}
		else
		{
			if(drive_infos[drive].custom_loader == 2) mode = 0; // ATX -> Off
			if(drive_infos[drive].custom_loader == 1) mode = 1; // XEX -> PBI
		}
	}

	// HSIO does not handle 512-byte sector ATRs
	if(!mode || (file->opened() && mode == 2 && drive_infos[drive].sector_size == 512))
		return 0xFF;

	mode--;
	if (!file->opened() || mode == 1)
	{
		if(!mode) iocb[3] = 0x8A;
	}
	else
	{
		command.command = iocb[2];
		command.aux1 = iocb[0xA];
		command.aux2 = iocb[0xB];
		command.auxab = (command.deviceId & 0x80) ? iocb[0xC] | (iocb[0xD] << 8) : 0;

		CommandHandler handle_command = get_command_handler(command, iocb[3]);
		if (handle_command)
		{
			sio_action_t action;
			action.bytes = iocb[8] | (iocb[9] << 8);
			action.success = 1;
			action.respond = 1;
			// Copy over 512 bytes from Atari to the buffer
			if(action.bytes)
			{
				atari800_dma_read(atari_sector_buffer, ATARI_BASE + (iocb[4] | (iocb[5] << 8)), action.bytes);
				action.sector_buffer = atari_sector_buffer;
			}

			handle_command(command, drive, file, &action);

			if (action.respond)
			{
				iocb[8] = action.bytes & 0xFF;
				iocb[9] = (action.bytes >> 8) & 0xFF;
				atari8bit_dma_write(atari_sector_buffer, ATARI_BASE + (iocb[4] | (iocb[5] << 8)), action.bytes);
				atari8bit_dma_write(&iocb[0x08], ATARI_BASE + 0x308, 2);
			}
			iocb[3] = action.success ? 0x01 : 0x90;
		}
		else
		{
			iocb[3] = 0x8B;
		}		
	}
	atari8bit_dma_write(&iocb[0x03], ATARI_BASE + 0x303, 1);
	set_a8bit_reg(REG_DRIVE_LED, 0);
	return mode;
}

void handle_pbi()
{
	if(!(get_a8bit_reg(REG_ATARI_STATUS1) & STATUS1_MASK_MODEPBI)) return;
	
	static uint8_t pbi_ram_base[16];
	static uint8_t pbi_drives_config[4];

	atari800_dma_read(pbi_ram_base, ATARI_BASE + 0xD100, 16);

	if(pbi_ram_base[0] == 0xa5 && pbi_ram_base[1] == 0xa5)
	{
		uint16_t atari_status2 = get_a8bit_reg(REG_ATARI_STATUS2);

		if(pbi_ram_base[3] == 0x01)
		{
			pbi_drives_config[0] = (atari_status2 >> 0) & 0x3;
			pbi_drives_config[1] = (atari_status2 >> 2) & 0x3;
			pbi_drives_config[2] = (atari_status2 >> 4) & 0x3;
			pbi_drives_config[3] = (atari_status2 >> 6) & 0x3;

			memcpy(&pbi_ram_base[0x0C], pbi_drives_config, 4);
			uint8_t boot_drv = (atari_status2 >> 8) & 0x07;

			pbi_ram_base[0x0B] = boot_drv;
			pbi_ram_base[0x0A] = 0x00;
			if(boot_drv == 1 && drive_infos[MAX_DRIVES].file.opened())
			{
				// APT
				pbi_ram_base[0x0A] = drive_infos[MAX_DRIVES].info & 0xF;
			}
			else if(boot_drv)
			{
				pbi_ram_base[0x0A] = boot_drv - 1;
			}
			atari8bit_dma_write(&pbi_ram_base[0x0A], ATARI_BASE + 0xD10A, 6);
			pbi_ram_base[2] = atari_status2 & STATUS2_MASK_SPLASH ? 1 : 0;
			// Important - this has to be alone and last!
			pbi_ram_base[3] = 0;
			atari8bit_dma_write(&pbi_ram_base[0x02], ATARI_BASE + 0xD102, 2);
		}
		else if(pbi_ram_base[5] == 0x01)
		{
			pbi_ram_base[4] = process_command_pbi(pbi_drives_config);
			pbi_ram_base[5] = 0;
			// Same here with the order
			atari8bit_dma_write(&pbi_ram_base[0x04], ATARI_BASE + 0xD104, 2);
		}
	}
}

static void handle_xex()
{
	atari800_dma_read(a8bit_buffer, xex_loader_base, XEX_READ_STATUS+1);
	
	if(a8bit_buffer[0] == 0x60)
	{
		if(!a8bit_buffer[XEX_READ_STATUS])
		{
			uint8_t len_buf[2];
			int read_offset, to_read, read_len;

			len_buf[0] = 0xFF;
			len_buf[1] = 0xFF;

			// Point to rts
			a8bit_buffer[0] = 0x00;
			a8bit_buffer[1] = xex_reloc;
			atari8bit_dma_write(a8bit_buffer, ATARI_INITAD, 2);
			
			while(len_buf[0] == 0xFF && len_buf[1] == 0xFF)
			{
				if(FileReadAdv(&xex_file, len_buf, 2) != 2) goto xex_eof;
			}
			read_offset = len_buf[0] | (len_buf[1] << 8);
			if(xex_file_first_block)
			{
				xex_file_first_block = 0;					
				atari8bit_dma_write(len_buf, ATARI_RUNAD, 2);
			}
			
			if(FileReadAdv(&xex_file, len_buf, 2) != 2) goto xex_eof;
			
			read_len = (len_buf[0] | (len_buf[1] << 8)) + 1 - read_offset;
			if(read_len < 1) goto xex_eof;
			
			to_read = read_len > BUFFER_SIZE ? BUFFER_SIZE : read_len;
			
			while(read_len)
			{
				if(FileReadAdv(&xex_file, a8bit_buffer, to_read) != to_read) goto xex_eof;
				atari8bit_dma_write(a8bit_buffer, ATARI_BASE + read_offset, to_read);
				read_len -= to_read;
				read_offset += to_read;
				to_read = read_len > BUFFER_SIZE ? BUFFER_SIZE : read_len;
			}

			a8bit_buffer[0] = 0x01;
			atari8bit_dma_write(a8bit_buffer, xex_loader_base + XEX_READ_STATUS, 1);
		}
	}
	// Is loader done?
	else if(a8bit_buffer[0] == 0x5F)
xex_eof:
	{
		a8bit_buffer[0] = 0xFF;
		atari8bit_dma_write(a8bit_buffer, xex_loader_base + XEX_READ_STATUS, 1);
		FileClose(&xex_file);
	}

}

void atari800_poll()
{
	uint16_t atari_status1 = get_a8bit_reg(REG_ATARI_STATUS1);

	set_a8bit_reg(REG_PAUSE, atari_status1 & STATUS1_MASK_HALT);

	if (atari_status1 & STATUS1_MASK_SOFTBOOT)
	{
		reboot(0, 0);	
	}
	else if (atari_status1 & STATUS1_MASK_COLDBOOT)
	{
		reboot(1, 0);	
	}
	
	if(xex_file.opened()) handle_xex();
	handle_pbi();
	process_command();
}

void atari800_init()
{
	set_a8bit_reg(REG_PAUSE, 1);
	cart_matches_total = 0;
	cart_match_car = 0;

	// Try to load bootX.rom ? TODO - limit only to boot3? or require pbibios.rom name?
	// and rely on the OSD menus?
	// In any case PBI boot rom should be attempted regardless of the option setting
	// (there is no menu entry to load it)
	static char mainpath[512];
	const char *home = HomeDir();

	if(get_a8bit_reg(REG_ATARI_STATUS1) & STATUS1_MASK_BOOTX)
	{
		sprintf(mainpath, "%s/boot.rom", home);
		user_io_file_tx(mainpath, 0 << 6);
		sprintf(mainpath, "%s/boot1.rom", home);
		user_io_file_tx(mainpath, 1 << 6);
		sprintf(mainpath, "%s/boot2.rom", home);
		user_io_file_tx(mainpath, 2 << 6);
	}
	sprintf(mainpath, "%s/boot3.rom", home);
	user_io_file_tx(mainpath, 3 << 6);
	atari800_reset();
}

void atari800_reset()
{
	set_a8bit_reg(REG_PAUSE, 1);
	set_a8bit_reg(REG_CART1_SELECT, 0);
	mounted_cart1_size = 0;
	set_a8bit_reg(REG_CART2_SELECT, 0);
	set_a8bit_reg(REG_FREEZER, 0);
	for(int i=0; i <= MAX_DRIVES; i++)
	{
		FileClose(&drive_infos[i].file);
	}
	FileClose(&xex_file);
	speed_index = 0;
	uart_init(speeds[speed_index] + 6);
	reboot(1, 0);
}
