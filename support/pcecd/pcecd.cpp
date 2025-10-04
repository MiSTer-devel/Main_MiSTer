
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../hardware.h"
#include "../../menu.h"
#include "../../lib/md5/md5.h"
#include "pcecd.h"


static int need_reset=0;
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
			poll_timer += 16;				// 16.0ms between frames if reading data */
		} else {
			poll_timer += 13 + ((adj == 3) ? 1 : 0);	// 13.33ms otherwise (including latency counts) */
			if (adj > 3) adj = 3;
			if (--adj <= 0) adj = 3;
		}

		if (pcecdd.has_status && !pcecdd.latency) {

			pcecdd.SendStatus(pcecdd.GetStatus());
			pcecdd.has_status = 0;
		}
		else if (pcecdd.data_req && !pcecdd.latency) {

			pcecdd.SendDataRequest();
			pcecdd.data_req = false;
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
			pcecdd.SendStatus(0);
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
		user_io_status_set("[0]", 1);
		usleep(100000);
		user_io_status_set("[0]", 0);
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

static const char* identify_game_by_hash(const char *filename, char *out_hash)
{
	out_hash[0] = 0;

	// Parse CUE file to find data track
	fileTYPE cue_file = {};
	if (!FileOpen(&cue_file, filename)) return NULL;

	char cue_content[4096];
	int bytes_read = FileReadAdv(&cue_file, cue_content, sizeof(cue_content) - 1);
	FileClose(&cue_file);

	if (bytes_read <= 0) return NULL;
	cue_content[bytes_read] = 0;

	// Find BIN filename
	char *file_line = strstr(cue_content, "FILE");
	if (!file_line) return NULL;

	char *quote1 = strchr(file_line, '"');
	if (!quote1) return NULL;
	quote1++;

	char *quote2 = strchr(quote1, '"');
	if (!quote2) return NULL;

	char bin_filename[1024];
	int name_len = quote2 - quote1;
	if ((size_t)name_len >= sizeof(bin_filename)) return NULL;

	// Build full path to BIN
	const char *slash = strrchr(filename, '/');
	if (!slash) return NULL;

	int path_len = slash - filename + 1;
	memcpy(bin_filename, filename, path_len);
	memcpy(bin_filename + path_len, quote1, name_len);
	bin_filename[path_len + name_len] = 0;

	// Find data track sector
	char *mode_line = strstr(cue_content, "MODE");
	if (!mode_line) return NULL;

	// Find INDEX 01 line after MODE
	char *index_line = strstr(mode_line, "INDEX 01");
	if (!index_line) return NULL;

	int minutes, seconds, frames;
	if (sscanf(index_line, "INDEX 01 %d:%d:%d", &minutes, &seconds, &frames) != 3)
		return NULL;

	int data_sector = (minutes * 60 + seconds) * 75 + frames;

	// Open BIN and read first 100 sectors
	fileTYPE bin_file = {};
	if (!FileOpen(&bin_file, bin_filename)) return NULL;

	uint32_t offset = data_sector * 2352;
	FileSeek(&bin_file, offset, SEEK_SET);

	// Read 100 sectors
	uint8_t *hash_data = (uint8_t*)malloc(100 * 2352);
	if (!hash_data)
	{
		FileClose(&bin_file);
		return NULL;
	}

	int read_size = FileReadAdv(&bin_file, hash_data, 100 * 2352);
	FileClose(&bin_file);

	if (read_size < 100 * 2352)
	{
		free(hash_data);
		return NULL;
	}

	// Compute MD5
	uint8_t hash[16];
	MD5_CTX ctx;
	MD5Init(&ctx);
	MD5Update(&ctx, hash_data, 100 * 2352);
	MD5Final(hash, &ctx);
	free(hash_data);

	// Convert to hex string
	for (int i = 0; i < 16; i++)
		sprintf(out_hash + i * 2, "%02x", hash[i]);
	out_hash[32] = 0;

	return out_hash;
}

static int load_bios(char *biosname, const char *cuename, int sgx)
{
	fileTYPE f;
	if (!FileOpen(&f, biosname)) return 0;

	uint8_t us_cart = 0, swap = 0;

	uint32_t start = f.size & 0x3FF;
	uint32_t size = f.size;

	if (size >= 262144)
	{
		size = 262144;

		FileSeek(&f, start + size - 26, SEEK_SET);
		memset(buf, 0, sizeof(buf));
		FileReadAdv(&f, buf, 26);
		swap = !memcmp(buf, us_sig, sizeof(us_sig)) ? 1 : 0;
	}

	printf("CD SGX mode = %d\n", sgx);

	user_io_set_index(sgx ? 1 : 0);
	user_io_set_download(1);
	FileSeek(&f, start, SEEK_SET);

	while (size)
	{
		int chunk = (size > sizeof(buf)) ? sizeof(buf) : size;
		size -= chunk;

		FileReadAdv(&f, buf, chunk);
		if (swap)
		{
			for (int i = 0; i < chunk; i++)
			{
				unsigned char c = buf[i];
				buf[i] = ((c & 1) << 7) | ((c & 2) << 5) | ((c & 4) << 3) | ((c & 8) << 1) | ((c & 16) >> 1) | ((c & 32) >> 3) | ((c & 64) >> 5) | ((c & 128) >> 7);
			}
		}
		else if (f.size >= 262144)
		{
			for (int i = 0; i < chunk - 8; i++)
			{
				if (!memcmp(buf + i, "ALL DATA", 8)) us_cart = 1;
			}
		}
		user_io_file_tx_data((uint8_t*)buf, chunk);
	}

	FileGenerateSavePath(cuename, buf);
	user_io_file_mount(buf, 0, 1);

	user_io_set_download(0);
	pcecdd.SetRegion(swap | us_cart);
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

			// Identify game by hash
			char hash[33];
			identify_game_by_hash(filename, hash);

			user_io_write_gamename(filename, hash, 0);

			int sgx = 0;

			// load CD BIOS
			strcpy(buf, filename);
			char *p = strrchr(buf, '/');
			int loaded = 0;
			if (p)
			{
				p++;

				strcpy(p, "sgx");
				if (FileExists(buf)) sgx = 1;

				strcpy(p, "cd_bios.rom");
				loaded = load_bios(buf, filename, sgx);
			}

			if (!loaded)
			{
				sprintf(buf, "%s/cd_bios.rom", HomeDir(PCECD_DIR));
				loaded = load_bios(buf, filename, sgx);
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
	user_io_file_tx_data(buf, len);
	user_io_set_download(0);
	return 1;
}
