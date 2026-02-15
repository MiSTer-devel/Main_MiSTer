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
static uint8_t cart_matches_mode[2]; // It is just 1 (type 16) or 2 (type 6) chips for 16K carts
static uint8_t cart_match_car;

static unsigned char cart_io_index;

int atari5200_get_match_cart_count()
{
	return cart_matches_total;
}

static const char one_chip[16] = "One chip";
static const char two_chip[16] = "Two chip";

// 0 -> 16, 1 -> 6

const char *atari5200_get_cart_match_name(int match_index)
{
	return match_index == 1 ? two_chip : one_chip;
}

void atari5200_umount_cartridge()
{
	set_a8bit_reg(REG_CART1_SELECT, 0);
}

#if 0

int atari5200_check_cartridge_file(const char* name, unsigned char index)
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

void atari5200_open_cartridge_file(const char* name, int match_index)
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
				if (to_read > BUFFER_SIZE) to_read = BUFFER_SIZE;
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

#endif

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
