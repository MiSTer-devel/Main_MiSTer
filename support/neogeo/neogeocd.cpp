
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
