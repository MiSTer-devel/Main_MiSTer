
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../hardware.h"
#include "pcecd.h"


static int /*loaded = 0, unloaded = 0,*/ need_reset=0;
static uint8_t has_command = 0;

void pcecd_poll()
{
	static uint32_t poll_timer = 0;
	static uint8_t last_req = 0;
	static uint8_t adj = 0;

	if (!poll_timer || CheckTimer(poll_timer))
	{
		poll_timer = GetTimer(13 + (!adj ? 1 : 0));
		if (++adj >= 3) adj = 0;

		if (pcecdd.has_status) {
			uint16_t s;
			pcecdd.GetStatus((uint8_t*)&s);

			spi_uio_cmd_cont(UIO_CD_SET);
			spi_w(s);
			spi_w(0);
			DisableIO();

			pcecdd.has_status = 0;

			printf("\x1b[32mPCECD: Send status = %02X, message = %02X\n\x1b[0m", s&0xFF, s >> 8);
		}
		else if (pcecdd.data_req) {
			spi_uio_cmd_cont(UIO_CD_SET);
			spi_w(0);
			spi_w(1);
			DisableIO();

			pcecdd.data_req = false;

			printf("\x1b[32mPCECD: Data request for MODESELECT6\n\x1b[0m");
		}

		pcecdd.Update();
	}


	uint8_t req = spi_uio_cmd_cont(UIO_CD_GET);
	if (req != last_req)
	{
		last_req = req;

		uint16_t data_in[6];
		uint16_t data_mode;
		data_in[0] = spi_w(0);
		data_in[1] = spi_w(0);
		data_in[2] = spi_w(0);
		data_in[3] = spi_w(0);
		data_in[4] = spi_w(0);
		data_in[5] = spi_w(0);
		data_mode = spi_w(0);
		DisableIO();


		switch (data_mode & 0xFF)
		{
		case 0:
			pcecdd.SetCommand((uint8_t*)data_in);
			pcecdd.CommandExec();
			has_command = 1;
			break;

		case 1:
			//TODO: process data
			pcecdd.SendStatus(0, 0);
			printf("\x1b[32mPCECD: Command MODESELECT6, received data\n\x1b[0m");
			break;

		default:
			pcecdd.can_read_next = true;
			break;
		}


		//printf("\x1b[32mMCD: Get command, command = %04X%04X%04X, has_command = %u\n\x1b[0m", data_in[2], data_in[1], data_in[0], has_command);
	}
	else
		DisableIO();

	if (need_reset) {
		need_reset = 0;
		pcecdd.Reset();
		printf("\x1b[32mPCECD: Reset\n\x1b[0m");
	}

}

void pcecd_reset() {
	need_reset = 1;
}

static void notify_mount(int load)
{
	spi_uio_cmd16(UIO_SET_SDINFO, load);
	spi_uio_cmd8(UIO_SET_SDSTAT, 1);

	if (!load)
	{
		user_io_8bit_set_status(UIO_STATUS_RESET, UIO_STATUS_RESET);
		usleep(100000);
		user_io_8bit_set_status(0, UIO_STATUS_RESET);
	}
}

int pcecd_using_cd()
{
	return pcecdd.loaded;
}

void pcecd_set_image(int num, const char *filename)
{
	(void)num;

	pcecdd.Unload();
	pcecdd.state = PCECD_STATE_NODISC;

	if (strlen(filename)) {
		static char path[1024];

		if (pcecdd.Load(filename) > 0) {
			pcecdd.state = pcecdd.loaded ? PCECD_STATE_IDLE : PCECD_STATE_NODISC;
			pcecdd.latency = 10;
			pcecdd.SendData = pcecd_send_data;

			// load CD BIOS
			sprintf(path, "%s/cd.rom", user_io_get_core_path());
			user_io_file_tx(path, 0);
			notify_mount(1);
		}
		else {
			notify_mount(0);
			pcecdd.state = PCECD_STATE_NODISC;
		}
	}
	else
	{
		pcecdd.Unload();
		notify_mount(0);
		pcecdd.state = PCECD_STATE_NODISC;
	}
}

int pcecd_send_data(uint8_t* buf, int len, uint8_t index) {
	user_io_set_index(index);
	user_io_set_download(1);
	user_io_file_tx_write(buf, len);
	user_io_set_download(0);
	return 1;
}
