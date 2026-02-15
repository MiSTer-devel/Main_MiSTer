#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../../hardware.h"
#include "../../menu.h"
#include "../../file_io.h"
#include "../../debug.h"
#include "../../user_io.h"
#include "../../fpga_io.h"

#include "atari5200.h"
#include "atari8bit_defs.h"

extern uint8_t a8bit_buffer[];

void set_a8bit_reg(uint8_t reg, uint8_t val);
uint16_t get_a8bit_reg(uint8_t reg);
void atari8bit_dma_write(const uint8_t *buf, uint32_t addr, uint32_t len);
void atari8bit_dma_zero(uint32_t addr, uint32_t len);

static void reboot()
{
	set_a8bit_reg(REG_PAUSE, 1);

	// Initialize the first 16K of Atari RAM with a pattern
	for(int i = 0; i < BUFFER_SIZE; i += 2)
	{
		a8bit_buffer[i] = 0xFF;
		a8bit_buffer[i+1] = 0x00;
	}
	user_io_set_index(99);
	user_io_set_download(1, ATARI_BASE);
	for(int i = 0; i < 0x4000 / BUFFER_SIZE; i++) user_io_file_tx_data(a8bit_buffer, BUFFER_SIZE);
	user_io_set_upload(0);
	set_a8bit_reg(REG_RESET, 1);
	set_a8bit_reg(REG_RESET, 0);

	set_a8bit_reg(REG_PAUSE, 0);
}

static int cart_matches_total;
static uint8_t cart_matches_type[2];
static uint8_t cart_match_car;
static unsigned char cart_io_index;

int atari5200_get_match_cart_count()
{
	return cart_matches_total;
}

static const char one_chip[16] = "One chip";
static const char two_chip[16] = "Two chip";

const char *atari5200_get_cart_match_name(int match_index)
{
	return match_index == 1 ? two_chip : one_chip;
}

void atari5200_umount_cartridge()
{
	set_a8bit_reg(REG_CART1_SELECT, 0);
	reboot();
}

int atari5200_check_cartridge_file(const char* name, unsigned char index)
{
	fileTYPE f = {};

	int file_size;
	cart_match_car = 0;
	cart_matches_total = 0;
	cart_io_index = index;
	
	int ext_index = index >> 6; // CAR is index 0
	
	if (FileOpen(&f, name))
	{
		file_size = f.size;
		if (!ext_index)
		{
			cart_match_car = 1;
			FileReadAdv(&f, a8bit_buffer, 16);
		}
		FileClose(&f);

		if(cart_match_car)
		{
			// CAR file, presumably, let's check further
			if (a8bit_buffer[0] == 'C' && a8bit_buffer[1] == 'A' && a8bit_buffer[2] == 'R' && a8bit_buffer[3] == 'T')
			{
				switch(a8bit_buffer[7])
				{
					case 0x04:
					case 0x06:
					case 0x07:
					case 0x10:
					case 0x13:
					case 0x14:
					case 0x47:
					case 0x48:
					case 0x49:
					case 0x4A:
						cart_matches_type[0] = a8bit_buffer[7];
						cart_matches_total = 1;
						break;
					default:
						break;
				}
			}
		}
		else
		{
			uint8_t type = 0xFF;
			if (file_size == 0x10000) type = 0x47;
			else if (file_size == 0x20000) type = 0x48;
			else if (file_size == 0x40000) type = 0x49;
			else if (file_size == 0x80000) type = 0x4A;
			else if (file_size == 40960) type = 0x07;
			else if (file_size == 0x8000) type = 0x04;
			else if (file_size == 0x2000) type = 0x13;
			else if (file_size == 0x1000) type = 0x14;
			else if (file_size == 0x4000)
			{
				cart_matches_type[0] = 0x10;
				cart_matches_type[1] = 0x06;
				cart_matches_total = 2;
			}

			if(cart_matches_total == 0 && type != 0xFF)
			{
				cart_matches_total = 1;
				cart_matches_type[0] = type;
			}
		}
	}

	return cart_matches_total;
}

void atari5200_open_cartridge_file(const char* name, int match_index)
{
	fileTYPE f = {};
	int offset = cart_match_car ? 16 : 0;
	uint8_t cart_type = cart_matches_type[match_index];

	if (FileOpen(&f, name))
	{
		set_a8bit_reg(REG_PAUSE, 1);
		set_a8bit_reg(REG_CART1_SELECT, 0);

		ProgressMessage(0, 0, 0, 0);
		FileSeek(&f, offset, SEEK_SET);

		user_io_set_index(cart_io_index);
		user_io_set_download(1, SDRAM_BASE + 0x4000);
	
		if(cart_type == 0x06)
		{
			for(int i = 0; i < 4; i++)
			{
				ProgressMessage("Loading", f.name, i * 0x2000, 0x8000);
				if(i % 2 == 0) FileReadAdv(&f, a8bit_buffer, 0x2000);
				user_io_file_tx_data(a8bit_buffer, 0x2000);
			}
		}
		else if(cart_type == 0x10)
		{
			for(int i = 0; i < 4; i++)
			{
				ProgressMessage("Loading", f.name, i * 0x2000, 0x8000);
				if(i % 2 == 0) FileSeek(&f, offset, SEEK_SET);
				FileReadAdv(&f, a8bit_buffer, 0x2000);
				user_io_file_tx_data(a8bit_buffer, 0x2000);
			}
		}
		else if(cart_type == 0x13 || cart_type == 0x14)
		{
			int block_size = f.size - offset;
			FileReadAdv(&f, a8bit_buffer, block_size);
			for(int i = 0; i < 0x8000 / block_size; i++)
			{
				ProgressMessage("Loading", f.name, i * block_size, 0x8000);
				user_io_file_tx_data(a8bit_buffer, block_size);
			}
		}
		else if(cart_type >= 0x47 && cart_type <= 0x4A)
		{
			int block_size = f.size - offset;
			for(int i = 0; i < 0x80000 / block_size; i++)
			{
				FileSeek(&f, offset, SEEK_SET);
				for(int j = 0; j < block_size / 0x2000; j++)
				{
					ProgressMessage("Loading", f.name, i * block_size + j * 0x2000, 0x80000);
					FileReadAdv(&f, a8bit_buffer, 0x2000);
					user_io_file_tx_data(a8bit_buffer, 0x2000);
				}
			}
		}
		else if(cart_type == 0x07)
		{
			FileSeek(&f, 0, SEEK_SET);
			FileReadAdv(&f, a8bit_buffer, 512);
			uint8_t bb_type1 = (a8bit_buffer[offset] == 0x2F);
			
			FileSeek(&f, offset + (bb_type1 ? 0 : 0x2000), SEEK_SET);
			
			ProgressMessage("Loading", f.name, 0, 0xC000);
			FileReadAdv(&f, a8bit_buffer, 0x2000);
			user_io_file_tx_data(a8bit_buffer, 0x2000);

			ProgressMessage("Loading", f.name, 0x2000, 0xC000);
			FileReadAdv(&f, a8bit_buffer, 0x2000);
			user_io_file_tx_data(a8bit_buffer, 0x2000);

			FileSeek(&f, offset + (bb_type1 ? 0x8000 : 0), SEEK_SET);

			ProgressMessage("Loading", f.name, 0x4000, 0xC000);
			FileReadAdv(&f, a8bit_buffer, 0x2000);
			user_io_file_tx_data(a8bit_buffer, 0x2000);

			ProgressMessage("Loading", f.name, 0x6000, 0xC000);
			user_io_file_tx_data(a8bit_buffer, 0x2000);

			FileSeek(&f, offset + (bb_type1 ? 0x4000 : 0x6000), SEEK_SET);

			ProgressMessage("Loading", f.name, 0x8000, 0xC000);
			FileReadAdv(&f, a8bit_buffer, 0x2000);
			user_io_file_tx_data(a8bit_buffer, 0x2000);

			ProgressMessage("Loading", f.name, 0xA000, 0xC000);
			FileReadAdv(&f, a8bit_buffer, 0x2000);
			user_io_file_tx_data(a8bit_buffer, 0x2000);
		}
		else if(cart_type == 0x04)
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
		set_a8bit_reg(REG_CART1_SELECT, cart_type);
		reboot();
	}
}

void atari5200_poll()
{
	uint16_t atari_status1 = get_a8bit_reg(REG_ATARI_STATUS1);

	set_a8bit_reg(REG_PAUSE, atari_status1 & STATUS1_MASK_HALT);
	if (atari_status1 & STATUS1_MASK_COLDBOOT) reboot();	
}

void atari5200_init()
{
	set_a8bit_reg(REG_PAUSE, 1);
	cart_matches_total = 0;
	cart_match_car = 0;

	static char mainpath[512];
	const char *home = HomeDir();

	sprintf(mainpath, "%s/boot.rom", home);
	user_io_file_tx(mainpath, 0);
	atari5200_reset();
}

void atari5200_reset()
{
	set_a8bit_reg(REG_PAUSE, 1);
	set_a8bit_reg(REG_CART1_SELECT, 0);
	atari8bit_dma_zero(SDRAM_BASE + 0x4000, 0x80000);
	reboot();
}
