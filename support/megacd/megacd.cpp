
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../hardware.h"
#include "megacd.h"


int loaded = 0, unloaded = 0;
static uint8_t has_command = 0;

void mcd_poll()
{
	static uint32_t poll_timer = 0, stat_timer = 0;
	static uint8_t last_req = 255;
	static uint8_t adj = 0;

	if (!stat_timer || CheckTimer(stat_timer))
	{
		stat_timer = GetTimer(15);

		if (has_command) {
			spi_uio_cmd_cont(UIO_CD_SET);
			uint64_t s = cdd.GetStatus();
			spi_w((s >> 0) & 0xFFFF);
			spi_w((s >> 16) & 0xFFFF);
			spi_w(((s >> 32) & 0x00FF) | (cdd.isData ? 0x01 << 8 : 0x00 << 8));
			DisableIO();

			has_command = 0;

			//printf("\x1b[32mMCD: Send status, status = %04X%04X%04X, frame = %u\n\x1b[0m", (uint16_t)((s >> 32) & 0x00FF), (uint16_t)((s >> 16) & 0xFFFF), (uint16_t)((s >> 0) & 0xFFFF), frame);
		}

	}


	uint8_t req = spi_uio_cmd_cont(UIO_CD_GET);
	if (req != last_req)
	{
		last_req = req;

		uint16_t data_in[4];
		data_in[0] = spi_w(0);
		data_in[1] = spi_w(0);
		data_in[2] = spi_w(0);
		DisableIO();

		uint64_t c = *((uint64_t*)(data_in));
		cdd.SetCommand(c);
			cdd.CommandExec();
			has_command = 1;


		//printf("\x1b[32mMCD: Get command, command = %04X%04X%04X, has_command = %u\n\x1b[0m", data_in[2], data_in[1], data_in[0], has_command);
	}
	else
		DisableIO();

	if (!poll_timer || CheckTimer(poll_timer))
	{
		poll_timer = GetTimer(13 + (!adj ? 1 : 0));
		if (++adj >= 3) adj = 0;

		cdd.Update();
	}
}


void mcd_set_image(int num, const char *filename)
{
	(void)num;

	cdd.Unload();
	unloaded = 1;
	cdd.status = CD_STAT_OPEN;

	if (*filename) {

		if (cdd.Load(filename) > 0) {
			loaded = 1;
			cdd.status = cdd.loaded ? CD_STAT_STOP : CD_STAT_NO_DISC;
			cdd.latency = 10;
		}
		else {
			cdd.status = CD_STAT_NO_DISC;
		}
	}
}

void mcd_reset() {
	cdd.Reset();
}


int cdd_t::SectorSend(uint8_t* header)
{
	uint8_t buf[2352+2352];
	int len = 2352;

	if (header) {
		memcpy(buf + 12, header, 4);
		ReadData(buf + 16);
	}
	else {
		len = ReadCDDA(buf);
	}

	// set index byte
	user_io_set_index(CD_DATA_IO_INDEX);

	// prepare transmission of new file
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0xff);
	DisableFpga();

	EnableFpga();
	spi8(UIO_FILE_TX_DAT);
	spi_write(buf, len, 1);
	DisableFpga();

	// signal end of transmission
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0x00);
	DisableFpga();

	return 1;
}


int cdd_t::SubcodeSend()
{
	uint16_t buf[98/2];

	ReadSubcode(buf);

	// set index byte
	user_io_set_index(CD_SUB_IO_INDEX);

	// prepare transmission of new file
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0xff);
	DisableFpga();

	EnableFpga();
	spi8(UIO_FILE_TX_DAT);
	spi_write((uint8_t*)buf, 98, 1);
	DisableFpga();

	// signal end of transmission
	EnableFpga();
	spi8(UIO_FILE_TX);
	spi8(0x00);
	DisableFpga();

	return 1;
}


