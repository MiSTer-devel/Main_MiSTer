
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../hardware.h"
#include "../../menu.h"
#include "../../cheats.h"
#include "../megacd/megacd.h"
#include "neogeocd.h"
#include "neogeo_loader.h"

static int need_reset=0;
static uint8_t has_command = 0;
static uint8_t neo_cd_en = 0;
static uint32_t poll_timer = 0;
static uint8_t cd_speed = 0;

#define CRC_START 5

#define NEOCD_GET_CMD        0
#define NEOCD_GET_SEND_DATA  1

void neocd_poll()
{
	static uint8_t last_req = 255;

	if (!poll_timer || CheckTimer(poll_timer))
	{

		set_poll_timer();

		if (has_command) {
			spi_uio_cmd_cont(UIO_CD_SET);
			uint64_t s = cdd.GetStatus(CRC_START);
			spi_w((s >> 0) & 0xFFFF);
			spi_w((s >> 16) & 0xFFFF);
			spi_w((s >> 32) & 0x00FF);
			DisableIO();

			has_command = 0;

			//printf("\x1b[32mNEOCD: Send status, status = %04X%04X%04X \n\x1b[0m", (uint16_t)((s >> 32) & 0x00FF), (uint16_t)((s >> 16) & 0xFFFF), (uint16_t)((s >> 0) & 0xFFFF));
		}

		cdd.Update();
	}


	uint8_t req = spi_uio_cmd_cont(UIO_CD_GET);

	if (req != last_req)
	{
		last_req = req;

		spi_w(NEOCD_GET_CMD);

		uint16_t data_in[4];
		data_in[0] = spi_w(0);
		data_in[1] = spi_w(0);
		data_in[2] = spi_w(0);
		DisableIO();

		if (need_reset || data_in[0] == 0xFF) {
			printf("NEOCD: request to reset\n");
			need_reset = 0;
			cdd.Reset();
		}

		cd_speed = (data_in[2] >> 8) & 3;

		uint64_t c = *((uint64_t*)(data_in));
		cdd.SetCommand(c, CRC_START);
		cdd.CommandExec();
		has_command = 1;

		//printf("\x1b[32mNEOCD: Get command, command = %04X%04X%04X, has_command = %u\n\x1b[0m", data_in[2], data_in[1], data_in[0], has_command);
	}
	else
		DisableIO();
}

void set_poll_timer()
{
	int speed = cd_speed;
	int interval = 10; // Slightly faster so the buffers stay filled when playing

	if (!cdd.isData || cdd.status != CD_STAT_PLAY || cdd.latency != 0)
	{
		speed = 0;
	}

	if (speed == 1)
	{
		interval = 5;
	}
	else if (speed == 2)
	{
		interval = 4;
	}
	else if (speed == 3)
	{
		interval = 2;
	}

	poll_timer = GetTimer(interval);
}

void neocd_set_image(char *filename)
{
	cdd.Unload();
	cdd.status = CD_STAT_OPEN;

	if (*filename)
	{
		neogeo_romset_tx(filename, 1);

		if (cdd.Load(filename) > 0)
		{
			cdd.status = cdd.loaded ? CD_STAT_STOP : CD_STAT_NO_DISC;
			cdd.latency = 10;
			cdd.SendData = neocd_send_data;
			cdd.CanSendData = neocd_can_send_data;

			// Extract NeoGeo CD game title and timestamp from ISO9660 descriptor
			char game_title[128] = {0};
			char timestamp[32] = {0};
			// Get the BIN filename from CUE or use filename directly if it's a BIN
			char bin_filename[1024] = {0};
			const char *ext = strrchr(filename, '.');
			if (ext && !strcasecmp(ext, ".cue"))
			{
				// Parse CUE to get first track BIN filename
				fileTYPE cue_file = {};
				if (FileOpen(&cue_file, filename))
				{
					char cue_content[4096];
					int bytes_read = FileReadAdv(&cue_file, cue_content, sizeof(cue_content) - 1);
					FileClose(&cue_file);

					if (bytes_read > 0)
					{
						cue_content[bytes_read] = 0;
						// Find first FILE line
						char *file_line = strstr(cue_content, "FILE");
						if (file_line)
						{
							// Extract filename between quotes
							char *quote1 = strchr(file_line, '"');
							if (quote1)
							{
								quote1++;
								char *quote2 = strchr(quote1, '"');
								if (quote2)
								{
									// Build full path
									const char *slash = strrchr(filename, '/');
									if (slash)
									{
										int path_len = slash - filename + 1;
										memcpy(bin_filename, filename, path_len);
										int name_len = quote2 - quote1;
										if ((size_t)(path_len + name_len) < sizeof(bin_filename))
										{
											memcpy(bin_filename + path_len, quote1, name_len);
											bin_filename[path_len + name_len] = 0;
										}
									}
								}
							}
						}
					}
				}
			}
			else
			{
				strncpy(bin_filename, filename, sizeof(bin_filename) - 1);
			}

			fileTYPE f = {};
			if (bin_filename[0] && FileOpen(&f, bin_filename))
			{
				uint8_t sector[2352];
				FileSeek(&f, 16 * 2352, SEEK_SET);
				if (FileReadAdv(&f, sector, 2352))
				{
					// Extract ISO9660 volume ID at offset 0x28 - read full 64 bytes
					if (memcmp(&sector[0x11], "CD001", 5) == 0)
					{
						// Extract volume ID (64 bytes) - just copy printable characters
						const char *vol_id = (const char*)&sector[0x28];
						int title_len = 0;
						for (int i = 0; i < 64; i++)
						{
							if (vol_id[i] >= 0x20 && vol_id[i] < 0x7F)
							{
								game_title[title_len++] = vol_id[i];
							}
							else
							{
								break;
							}
						}
						game_title[title_len] = 0;

						// Remove common prefixes if present
						const char *prefixes[] = {
							"INC., TYPE: 0002",
							"INC., TYPE: 0001",
							"Inc., Type:0002",
							"Inc., Type:0001"
						};
						for (int i = 0; i < 4; i++)
						{
							int prefix_len = strlen(prefixes[i]);
							if (title_len > prefix_len && strncmp(game_title, prefixes[i], prefix_len) == 0)
							{
								memmove(game_title, game_title + prefix_len, title_len - prefix_len + 1);
								break;
							}
						}

						// Extract timestamp - format as YYYYMMDD
						const char *ts = (const char*)&sector[0x33D];
						if (ts[0] >= '0' && ts[0] <= '9')
						{
							memcpy(timestamp, ts, 8);
							timestamp[8] = 0;
						}
					}
				}
				FileClose(&f);
			}

			// Build Game ID: timestamp-volume_id
			char game_id[256] = {0};
			if (timestamp[0] && game_title[0])
			{
				snprintf(game_id, sizeof(game_id), "%s-%s", timestamp, game_title);
				printf("NeoGeo CD Game ID: %s\n", game_id);
			}
			else if (game_title[0])
			{
				strncpy(game_id, game_title, sizeof(game_id) - 1);
				printf("NeoGeo CD Game ID: %s\n", game_id);
			}

			user_io_write_gamename(filename, game_id[0] ? game_id : NULL, 0);
		}
		else
		{
			cdd.status = CD_STAT_NO_DISC;
		}
	}

	neocd_reset();
}

void neocd_reset() {
	need_reset = 1;
}

int neocd_send_data(uint8_t* buf, int len, uint8_t index) {
	// set index byte
	user_io_set_index(index);

	user_io_set_download(1);
	user_io_file_tx_data(buf, len);
	user_io_set_download(0);
	return 1;
}

int neocd_is_en() {
	return (neo_cd_en == 1);
}

void neocd_set_en(int enable) {
	neo_cd_en = enable;
}

int neocd_can_send_data(uint8_t type) {
	// Ask the FPGA if it is ready to receive a sector
	spi_uio_cmd_cont(UIO_CD_GET);
	spi_w(NEOCD_GET_SEND_DATA | (type << 2));

	uint16_t data = spi_w(0);
	DisableIO();

	return (data == 1);
}
