
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../hardware.h"
#include "../../menu.h"
#include "pcecd.h"


static int /*loaded = 0, unloaded = 0,*/ need_reset=0;
static uint8_t has_command = 0;

void pcecd_poll()
{
	static uint32_t poll_timer = 0;
	static uint8_t last_req = 0;
	static uint8_t adj = 0;

	if (!poll_timer) poll_timer = GetTimer(13);

	if (CheckTimer(poll_timer))
	{
		if ((!pcecdd.latency) && (pcecdd.state == PCECD_STATE_READ)) {
			poll_timer += 16 + ((adj == 10) ? 1 : 0);	// 16.1ms between frames if reading data */
			if (--adj <= 0) adj = 10;
		} else {
			poll_timer += 13 + ((adj == 3) ? 1 : 0);	// 13.33ms otherwise (including latency counts) */
			if (adj > 3) adj = 3;
			if (--adj <= 0) adj = 3;
		}

		if (pcecdd.has_status && !pcecdd.latency) {
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

		uint16_t data_in[7];
		data_in[0] = spi_w(0);
		data_in[1] = spi_w(0);
		data_in[2] = spi_w(0);
		data_in[3] = spi_w(0);
		data_in[4] = spi_w(0);
		data_in[5] = spi_w(0);
		data_in[6] = spi_w(0);
		DisableIO();


		switch (data_in[6] & 0xFF)
		{
		case 0:
			pcecdd.SetCommand((uint8_t*)data_in);
			pcecdd.CommandExec();
			has_command = 1;
			break;

		case 1:
			//TODO: process data
			pcecdd.PendStatus(0, 0);
			printf("\x1b[32mPCECD: Command MODESELECT6, received data\n\x1b[0m");
			break;

		case 2:
			pcecdd.can_read_next = true;
			break;

		default:
			need_reset = 1;
			break;
		}


		//printf("\x1b[32mMCD: Get command, command = %04X%04X%04X, has_command = %u\n\x1b[0m", data_in[2], data_in[1], data_in[0], has_command);
	}
	else
		DisableIO();

	if (need_reset) {
		need_reset = 0;
		pcecdd.Reset();
		poll_timer = 0;
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

static char buf[1024];
static char us_sig[] =
	{ 0x72, 0xA2, 0xC2, 0x04, 0x12, 0xF6, 0xB6, 0xA6,
	  0x04, 0xA2, 0x36, 0xA6, 0xC6, 0x2E, 0x4E, 0xF6,
	  0x76, 0x96, 0xC6, 0xCE, 0x34, 0x32, 0x2E, 0x26,
	  0x74, 0x00 };

static int load_bios(char *biosname, const char *cuename)
{
	uint32_t size = FileLoad(biosname, 0, 0);
	if (!size) return 0;

	fileTYPE f;
	if (!FileOpen(&f, biosname)) return 0;

	int swap = 0;
	uint32_t start = size & 0x3FF;

	if (size >= 262144)
	{
		size = 262144;

		FileSeek(&f, start + size - 26, SEEK_SET);
		memset(buf, 0, sizeof(buf));
		FileReadAdv(&f, buf, 26);
		swap = !memcmp(buf, us_sig, sizeof(us_sig));
	}

	user_io_set_index(0);
	user_io_set_download(1);
	FileSeek(&f, start, SEEK_SET);

	while (size)
	{
		uint16_t chunk = (size > sizeof(buf)) ? sizeof(buf) : size;
		size -= chunk;

		FileReadAdv(&f, buf, chunk);
		if (swap)
		{
			for (uint32_t i = 0; i < chunk; i++)
			{
				unsigned char c = buf[i];
				buf[i] = ((c & 1) << 7) | ((c & 2) << 5) | ((c & 4) << 3) | ((c & 8) << 1) | ((c & 16) >> 1) | ((c & 32) >> 3) | ((c & 64) >> 5) | ((c & 128) >> 7);
			}
		}
		user_io_file_tx_write((uint8_t*)buf, chunk);
	}

	FileGenerateSavePath(cuename, buf);
	user_io_file_mount(buf, 0, 1);

	user_io_set_download(0);

	return 1;
}

void pcecd_set_image(int num, const char *filename)
{
	(void)num;

	pcecdd.Unload();
	pcecdd.state = PCECD_STATE_NODISC;

	if (strlen(filename))
	{
		printf("Load CD: %s\n", filename);

		if (pcecdd.Load(filename) > 0)
		{
			pcecdd.state = pcecdd.loaded ? PCECD_STATE_IDLE : PCECD_STATE_NODISC;
			pcecdd.latency = 10;
			pcecdd.SendData = pcecd_send_data;

			// load CD BIOS
			strcpy(buf, filename);
			char *p = strrchr(buf, '/');
			int loaded = 0;
			if (p)
			{
				p++;
				strcpy(p, "cd_bios.rom");
				loaded = load_bios(buf, filename);
			}

			if (!loaded)
			{
				sprintf(buf, "%s/cd_bios.rom", HomeDir(PCECD_DIR));
				loaded = load_bios(buf, filename);
			}

			if (!loaded) Info("CD BIOS not found!", 4000);

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
