#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/vt.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "hardware.h"
#include "user_io.h"
#include "spi.h"
#include "cfg.h"
#include "file_io.h"
#include "menu.h"
#include "audio.h"

static uint8_t vol_att = 0;
static uint8_t corevol_att = 0;
unsigned long vol_set_timeout = 0;
static int has_filter = 0;

static char filter_cfg_path[1024] = {};
static char filter_cfg[1024] = {};

static void setFilter()
{
	fileTYPE f = {};

	has_filter = spi_uio_cmd(UIO_SET_AFILTER);
	if (!has_filter) return;

	snprintf(filter_cfg_path, sizeof(filter_cfg_path), AFILTER_DIR"/%s", filter_cfg + 1);
	if(filter_cfg[0]) printf("\nLoading audio filter: %s\n", filter_cfg_path);

	if (filter_cfg[0] && FileOpen(&f, filter_cfg_path))
	{
		char *buf = (char*)malloc(f.size + 1);
		if (buf)
		{
			memset(buf, 0, f.size + 1);
			int size;
			if ((size = FileReadAdv(&f, buf, f.size)))
			{
				int line = 0;
				spi_uio_cmd_cont(UIO_SET_AFILTER);
				spi_w((uint8_t)get_core_volume());

				char *end = buf + size;
				char *pos = buf;
				while (pos < end && line < 9)
				{
					char *st = pos;
					while ((pos < end) && *pos && (*pos != 10)) pos++;
					*pos = 0;
					while (*st == ' ' || *st == '\t' || *st == 13) st++;
					if (*st == '#' || *st == ';' || !*st) pos++;
					else
					{
						if (line == 0)
						{
							printf("version: %s\n", st);
							if (strncasecmp(st, "v1", 2)) break;
							line++;
						}
						else if (line == 1 || line == 3 || line == 4 || line == 5)
						{
							int val = 0;
							int n = sscanf(st, "%d", &val);
							printf("got %d values: %d\n", n, val);
							if (n == 1)
							{
								spi_w((uint16_t)val);
								if (line == 1) spi_w((uint16_t)(val >> 16));
								line++;
							}
						}
						else if (line == 2)
						{
							double val = 0;
							int n = sscanf(st, "%lg", &val);
							printf("got %d values: %g\n", n, val);
							if (n == 1)
							{
								int64_t coeff = 0x8000000000 * val;
								printf("  -> converted to: %lld\n", coeff);
								spi_w((uint16_t)coeff);
								spi_w((uint16_t)(coeff >> 16));
								spi_w((uint16_t)(coeff >> 32));
								line++;
							}
						}
						else
						{
							double val = 0;
							int n = sscanf(st, "%lg", &val);
							printf("got %d values: %g\n", n, val);
							if (n == 1)
							{
								int32_t coeff = 0x200000 * val;
								printf("  -> converted to: %d\n", coeff);
								spi_w((uint16_t)coeff);
								spi_w((uint16_t)(coeff >> 16));
								line++;
							}
						}
					}
				}
				DisableIO();
			}
			free(buf);
		}
	}
	else
	{
		spi_uio_cmd8(UIO_SET_AFILTER, (uint8_t)get_core_volume());
	}
}

void send_volume()
{
	int vol = get_volume();
	get_core_volume();

	if (!has_filter)
	{
		if (!(vol_att & 0x10) && vol_att + corevol_att > 7) vol_att = 7 - corevol_att;
		vol = vol_att + corevol_att;
	}
	spi_uio_cmd8(UIO_AUDVOL, vol);
}

int get_volume()
{
	return vol_att & 0x17;
}

int get_core_volume()
{
	corevol_att &= 7;
	if (corevol_att > 6) corevol_att = 6;
	return corevol_att;
}

void set_volume(int cmd)
{
	vol_set_timeout = GetTimer(1000);

	vol_att &= 0x17;
	if (!cmd) vol_att ^= 0x10;
	else if (vol_att & 0x10) vol_att &= 0xF;
	else if (cmd < 0 && vol_att < 7) vol_att += 1;
	else if (cmd > 0 && vol_att > 0) vol_att -= 1;

	send_volume();

	if (vol_att & 0x10)
	{
		Info("\x8d Mute", 1000);
	}
	else
	{
		char str[32];
		memset(str, 0, sizeof(str));

		sprintf(str, "\x8d ");
		char *bar = str + strlen(str);

		int vol = (audio_filter_en() < 0) ? get_core_volume() : 0;
		memset(bar, 0x8C, 8 - vol);
		memset(bar, 0x7f, 8 - vol - vol_att);
		Info(str, 1000);
	}
}

void set_core_volume(int cmd)
{
	vol_set_timeout = GetTimer(1000);

	corevol_att &= 7;
	if (cmd < 0 && corevol_att < 6) corevol_att += 1;
	if (cmd > 0 && corevol_att > 0) corevol_att -= 1;

	if (has_filter) setFilter();
	else send_volume();
}

void save_volume()
{
	if (vol_set_timeout && CheckTimer(vol_set_timeout))
	{
		vol_set_timeout = 0;
		FileSaveConfig("Volume.dat", &vol_att, 1);

		static char cfg_name[128];
		sprintf(cfg_name, "%s_volume.cfg", user_io_get_core_name());
		FileSaveConfig(cfg_name, &corevol_att, 1);
	}
}

void load_volume()
{
	sprintf(filter_cfg_path, "%s_afilter.cfg", user_io_get_core_name());
	if (!FileLoadConfig(filter_cfg_path, &filter_cfg, sizeof(filter_cfg) - 1) || filter_cfg[0] > 1)
	{
		memset(filter_cfg, 0, sizeof(filter_cfg));
		if (cfg.afilter_default[0])
		{
			strcpy(filter_cfg + 1, cfg.afilter_default);
			filter_cfg[0] = 1;
		}
	}

	FileLoadConfig("Volume.dat", &vol_att, 1);
	if (!is_menu())
	{
		static char cfg_name[128];
		sprintf(cfg_name, "%s_volume.cfg", user_io_get_core_name());
		FileLoadConfig(cfg_name, &corevol_att, 1);
	}

	get_volume();
	get_core_volume();

	setFilter();
	send_volume();
}

int audio_filter_en()
{
	return has_filter ? filter_cfg[0] : -1;
}

char* audio_get_filter(int only_name)
{
	char *path = filter_cfg + 1;
	if (only_name)
	{
		char *p = strrchr(path, '/');
		if (p) return p + 1;
	}
	return path;

}

void audio_set_filter(const char *name)
{
	strcpy(filter_cfg + 1, name);
	sprintf(filter_cfg_path, "%s_afilter.cfg", user_io_get_core_name());
	FileSaveConfig(filter_cfg_path, &filter_cfg, sizeof(filter_cfg));
	setFilter();
}

void audio_set_filter_en(int n)
{
	filter_cfg[0] = n ? 1 : 0;
	sprintf(filter_cfg_path, "%s_afilter.cfg", user_io_get_core_name());
	FileSaveConfig(filter_cfg_path, &filter_cfg, sizeof(filter_cfg));
	setFilter();
}
