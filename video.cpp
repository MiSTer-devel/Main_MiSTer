#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <linux/fb.h>
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
#include "video.h"
#include "input.h"
#include "shmem.h"

#include "support.h"
#include "lib/imlib2/Imlib2.h"

#define FB_SIZE  (1920*1080)
#define FB_ADDR  (0x20000000 + (32*1024*1024)) // 512mb + 32mb(Core's fb)

/*
--  [2:0] : 011=8bpp(palette) 100=16bpp 101=24bpp 110=32bpp
--  [3]   : 0=16bits 565 1=16bits 1555
--  [4]   : 0=RGB  1=BGR (for 16/24/32 modes)
--  [5]   : TBD
*/

#define FB_FMT_565  0b00100
#define FB_FMT_1555 0b01100
#define FB_FMT_888  0b00101
#define FB_FMT_8888 0b00110
#define FB_FMT_PAL8 0b00011
#define FB_FMT_RxB  0b10000
#define FB_EN       0x8000

#define FB_DV_LBRD  3
#define FB_DV_RBRD  6
#define FB_DV_UBRD  2
#define FB_DV_BBRD  2


static volatile uint32_t *fb_base = 0;
static int fb_enabled = 0;
static int fb_width = 0;
static int fb_height = 0;
static int fb_num = 0;
static int brd_x = 0;
static int brd_y = 0;

static int menu_bg = 0;
static int menu_bgn = 0;

struct vmode_t
{
	uint32_t vpar[8];
	double Fpix;
};

vmode_t vmodes[] =
{
	{ { 1280, 110,  40, 220,  720,  5,  5, 20 },  74.25  }, //0
	{ { 1024,  24, 136, 160,  768,  3,  6, 29 },  65     }, //1
	{ {  720,  16,  62,  60,  480,  9,  6, 30 },  27     }, //2
	{ {  720,  12,  64,  68,  576,  5,  5, 39 },  27     }, //3
	{ { 1280,  48, 112, 248, 1024,  1,  3, 38 }, 108     }, //4
	{ {  800,  40, 128,  88,  600,  1,  4, 23 },  40     }, //5
	{ {  640,  16,  96,  48,  480, 10,  2, 33 },  25.175 }, //6
	{ { 1280, 440,  40, 220,  720,  5,  5, 20 },  74.25  }, //7
	{ { 1920,  88,  44, 148, 1080,  4,  5, 36 }, 148.5   }, //8
	{ { 1920, 528,  44, 148, 1080,  4,  5, 36 }, 148.5   }, //9
	{ { 1366,  70, 143, 213,  768,  3,  3, 24 },  85.5   }, //10
	{ { 1024,  40, 104, 144,  600,  1,  3, 18 },  48.96  }, //11
	{ { 1920,  48,  32,  80, 1440,  2,  4, 38 }, 185.203 }, //12
	{ { 2048,  48,  32,  80, 1536,  2,  4, 38 }, 209.318 }, //13
};
#define VMODES_NUM (sizeof(vmodes) / sizeof(vmodes[0]))

vmode_t tvmodes[] =
{
	{{ 640, 30, 60, 70, 240,  4, 4, 14 }, 12.587 }, //NTSC 15K
	{{ 640, 16, 96, 48, 480,  8, 4, 33 }, 25.175 }, //NTSC 31K
	{{ 640, 30, 60, 70, 288,  6, 4, 14 }, 12.587 }, //PAL 15K
	{{ 640, 16, 96, 48, 576,  2, 4, 42 }, 25.175 }, //PAL 31K
};

struct vmode_custom_t
{
	uint32_t item[32];
	double Fpix;
};

static vmode_custom_t v_cur = {}, v_def = {}, v_pal = {}, v_ntsc = {};
static int vmode_def = 0, vmode_pal = 0, vmode_ntsc = 0;

static uint32_t getPLLdiv(uint32_t div)
{
	if (div & 1) return 0x20000 | (((div / 2) + 1) << 8) | (div / 2);
	return ((div / 2) << 8) | (div / 2);
}

static int findPLLpar(double Fout, uint32_t *pc, uint32_t *pm, double *pko)
{
	uint32_t c = 1;
	while ((Fout*c) < 400) c++;

	while (1)
	{
		double fvco = Fout*c;
		uint32_t m = (uint32_t)(fvco / 50);
		double ko = ((fvco / 50) - m);

		fvco = ko + m;
		fvco *= 50.f;

		if (ko && (ko <= 0.05f || ko >= 0.95f))
		{
			printf("Fvco=%f, C=%d, M=%d, K=%f ", fvco, c, m, ko);
			if (fvco > 1500.f)
			{
				printf("-> No exact parameters found\n");
				return 0;
			}
			printf("-> K is outside allowed range\n");
			c++;
		}
		else
		{
			*pc = c;
			*pm = m;
			*pko = ko;
			return 1;
		}
	}

	//will never reach here
	return 0;
}

static void setPLL(double Fout, vmode_custom_t *v)
{
	double Fpix;
	double fvco, ko;
	uint32_t m, c;

	printf("Calculate PLL for %.4f MHz:\n", Fout);

	if (!findPLLpar(Fout, &c, &m, &ko))
	{
		c = 1;
		while ((Fout*c) < 400) c++;

		fvco = Fout*c;
		m = (uint32_t)(fvco / 50);
		ko = ((fvco / 50) - m);

		//Make sure K is in allowed range.
		if (ko <= 0.05f)
		{
			ko = 0;
		}
		else if (ko >= 0.95f)
		{
			m++;
			ko = 0;
		}
	}

	uint32_t k = ko ? (uint32_t)(ko * 4294967296) : 1;

	fvco = ko + m;
	fvco *= 50.f;
	Fpix = fvco / c;

	printf("Fvco=%f, C=%d, M=%d, K=%f(%u) -> Fpix=%f\n", fvco, c, m, ko, k, Fpix);

	v->item[9]  = 4;
	v->item[10] = getPLLdiv(m);
	v->item[11] = 3;
	v->item[12] = 0x10000;
	v->item[13] = 5;
	v->item[14] = getPLLdiv(c);
	v->item[15] = 9;
	v->item[16] = 2;
	v->item[17] = 8;
	v->item[18] = 7;
	v->item[19] = 7;
	v->item[20] = k;

	v->Fpix = Fpix;
}

static char scaler_flt[3][1024] = { 0 };
static int phases[65][4];

static int get_phases_count(int type)
{
	fileTextReader reader = {};
	int res = 0;

	static char filename[1024];
	snprintf(filename, sizeof(filename), COEFF_DIR"/%s", scaler_flt[type] + 1);

	if (FileOpenTextReader(&reader, filename))
	{
		const char *line;
		while ((line = FileReadLine(&reader)))
		{
			int n = sscanf(line, "%d,%d,%d,%d", &phases[res][0], &phases[res][1], &phases[res][2], &phases[res][3]);
			if (n == 4) res++;
			if (res > 64) return 0; //too many
		}
	}

	if (res != 16 && res != 32 && res != 64) res = 0;
	return res;
}

static void send_phases(int type, int cnt, int ver)
{
	printf("send_phases: %d, %d, %d)\n", type, cnt, ver);
	fileTextReader reader = {};
	int phase = (type == VFILTER_VERT) ? 64 : 0;

	int v2txt = (cnt >= 64);
	int line = 0;
	do
	{
		if (ver == 2)
		{
			int rep = v2txt ? 1 : 4;
			for (int i = 0; i < rep; i++)
			{
				spi_w((phase * 4) + 0); spi_w(phases[line][0] & 0x1FF);
				spi_w((phase * 4) + 1); spi_w(phases[line][1] & 0x1FF);
				spi_w((phase * 4) + 2); spi_w(phases[line][2] & 0x1FF);
				spi_w((phase * 4) + 3); spi_w(phases[line][3] & 0x1FF);
				phase++;
			}
		}
		else
		{
			if (!(phase & 3))
			{
				spi_w((phases[line][0] & 0x1FF) | ((phase + 0) << 9));
				spi_w((phases[line][1] & 0x1FF) | ((phase + 1) << 9));
				spi_w((phases[line][2] & 0x1FF) | ((phase + 2) << 9));
				spi_w((phases[line][3] & 0x1FF) | ((phase + 3) << 9));
			}
			phase += v2txt ? 1 : 4;
		}
		line++;
	} while (phase & 63);
}

static void set_vfilter(int force)
{
	static int last_flags = 0;

	int flt_flags = spi_uio_cmd_cont(UIO_SET_FLTNUM);
	if (!flt_flags || (!force && last_flags == flt_flags))
	{
		DisableIO();
		return;
	}

	last_flags = flt_flags;
	printf("video_set_filter: flt_flags=%d\n", flt_flags);

	spi8(scaler_flt[0][0]);
	DisableIO();

	spi_uio_cmd_cont(UIO_SET_FLTCOEF);
	int phases;

	//horizontal filter
	phases = get_phases_count(VFILTER_HORZ);
	if (phases)
	{
		send_phases(VFILTER_HORZ, phases, flt_flags & 0xF);
		//vertical/scanlines filter
		int vert_flt = ((flt_flags & 0x30) && scaler_flt[VFILTER_SCAN][0]) ? VFILTER_SCAN : (scaler_flt[VFILTER_VERT][0]) ? VFILTER_VERT : VFILTER_HORZ;
		phases = get_phases_count(vert_flt);
		if (!phases) phases = get_phases_count(VFILTER_HORZ);
		send_phases(VFILTER_VERT, phases, flt_flags & 0xF);
	}

	DisableIO();
	if (!phases) spi_uio_cmd8(UIO_SET_FLTNUM, 0);
}

static void setScaler()
{
	uint32_t arc[4] = {};
	for (int i = 0; i < 2; i++)
	{
		if (cfg.custom_aspect_ratio[i][0])
		{
			if (sscanf(cfg.custom_aspect_ratio[i], "%u:%u", &arc[i * 2], &arc[(i * 2) + 1]) != 2 || arc[i * 2] < 1 || arc[i * 2] > 4095 || arc[(i * 2) + 1] < 1 || arc[(i * 2) + 1] > 4095)
			{
				arc[(i * 2) + 0] = 0;
				arc[(i * 2) + 1] = 0;
			}
		}
	}

	spi_uio_cmd_cont(UIO_SET_AR_CUST);
	for (int i = 0; i < 4; i++) spi_w(arc[i]);
	DisableIO();
	set_vfilter(1);
}

int video_get_scaler_flt(int type)
{
	return scaler_flt[type][0];
}

char* video_get_scaler_coeff(int type)
{
	return scaler_flt[type] + 1;
}

static char scaler_cfg[128] = { 0 };

void video_set_scaler_flt(int type, int n)
{
	scaler_flt[type][0] = (char)n;
	FileSaveConfig(scaler_cfg, &scaler_flt, sizeof(scaler_flt));
	spi_uio_cmd8(UIO_SET_FLTNUM, scaler_flt[0][0]);
	set_vfilter(1);
}

void video_set_scaler_coeff(int type, const char *name)
{
	strcpy(scaler_flt[type] + 1, name);
	FileSaveConfig(scaler_cfg, &scaler_flt, sizeof(scaler_flt));
	setScaler();
	user_io_send_buttons(1);
}

static void loadScalerCfg()
{
	sprintf(scaler_cfg, "%s_scaler.cfg", user_io_get_core_name());
	memset(scaler_flt, 0, sizeof(scaler_cfg));
	if (!FileLoadConfig(scaler_cfg, &scaler_flt, sizeof(scaler_flt)) || scaler_flt[0][0] > 1)
	{
		memset(scaler_flt, 0, sizeof(scaler_flt));
	}

	if (!scaler_flt[VFILTER_HORZ][1] && cfg.vfilter_default[0])
	{
		strcpy(scaler_flt[VFILTER_HORZ]+1, cfg.vfilter_default);
		scaler_flt[VFILTER_HORZ][0] = 1;
	}

	if (!scaler_flt[VFILTER_VERT][1] && cfg.vfilter_vertical_default[0])
	{
		strcpy(scaler_flt[VFILTER_VERT] + 1, cfg.vfilter_vertical_default);
		scaler_flt[VFILTER_VERT][0] = 1;
	}

	if (!scaler_flt[VFILTER_SCAN][1] && cfg.vfilter_scanlines_default[0])
	{
		strcpy(scaler_flt[VFILTER_SCAN] + 1, cfg.vfilter_scanlines_default);
		scaler_flt[VFILTER_SCAN][0] = 1;
	}

	if (!get_phases_count(VFILTER_HORZ)) memset(scaler_flt[VFILTER_HORZ], 0, sizeof(scaler_flt[VFILTER_HORZ]));
	if (!get_phases_count(VFILTER_VERT)) memset(scaler_flt[VFILTER_VERT], 0, sizeof(scaler_flt[VFILTER_VERT]));
	if (!get_phases_count(VFILTER_SCAN)) memset(scaler_flt[VFILTER_SCAN], 0, sizeof(scaler_flt[VFILTER_SCAN]));
}

static char gamma_cfg[1024] = { 0 };
static char has_gamma = 0;

static void setGamma()
{
	fileTextReader reader = {};
	static char filename[1024];

	if (!spi_uio_cmd_cont(UIO_SET_GAMMA))
	{
		DisableIO();
		return;
	}

	has_gamma = 1;
	spi8(0);
	DisableIO();
	snprintf(filename, sizeof(filename), GAMMA_DIR"/%s", gamma_cfg + 1);

	if (FileOpenTextReader(&reader, filename))
	{
		spi_uio_cmd_cont(UIO_SET_GAMCURV);

		const char *line;
		int index = 0;
		while ((line = FileReadLine(&reader)))
		{
			int c0, c1, c2;
			int n = sscanf(line, "%d,%d,%d", &c0, &c1, &c2);
			if (n == 1)
			{
				c1 = c0;
				c2 = c0;
				n = 3;
			}

			if (n == 3)
			{
				spi_w((index << 8) | (c0 & 0xFF));
				spi_w((index << 8) | (c1 & 0xFF));
				spi_w((index << 8) | (c2 & 0xFF));

				index++;
				if (index >= 256) break;
			}
		}
		DisableIO();
		spi_uio_cmd8(UIO_SET_GAMMA, gamma_cfg[0]);
	}
}

int video_get_gamma_en()
{
	return has_gamma ? gamma_cfg[0] : -1;
}

char* video_get_gamma_curve()
{
	return gamma_cfg + 1;
}

static char gamma_cfg_path[1024] = { 0 };

void video_set_gamma_en(int n)
{
	gamma_cfg[0] = (char)n;
	FileSaveConfig(gamma_cfg_path, &gamma_cfg, sizeof(gamma_cfg));
	setGamma();
}

void video_set_gamma_curve(char *name)
{
	strcpy(gamma_cfg + 1, name);
	FileSaveConfig(gamma_cfg_path, &gamma_cfg, sizeof(gamma_cfg));
	setGamma();
	user_io_send_buttons(1);
}

static void loadGammaCfg()
{
	sprintf(gamma_cfg_path, "%s_gamma.cfg", user_io_get_core_name());
	if (!FileLoadConfig(gamma_cfg_path, &gamma_cfg, sizeof(gamma_cfg) - 1) || gamma_cfg[0]>1)
	{
		memset(gamma_cfg, 0, sizeof(gamma_cfg));
	}
}

static char shadow_mask_cfg[1024] = { 0 };
static bool has_shadow_mask = false;

#define SM_FLAG_2X      ( 1 << 1 )
#define SM_FLAG_ROTATED ( 1 << 2 )
#define SM_FLAG_ENABLED ( 1 << 3 )

#define SM_FLAG(v) ( ( 0x0 << 13 ) | (v) )
#define SM_VMAX(v) ( ( 0x1 << 13 ) | (v) )
#define SM_HMAX(v) ( ( 0x2 << 13 ) | (v) )
#define SM_LUT(v)  ( ( 0x3 << 13 ) | (v) )

enum
{
	SM_MODE_NONE = 0,
	SM_MODE_1X,
	SM_MODE_2X,
	SM_MODE_1X_ROTATED,
	SM_MODE_2X_ROTATED,
	SM_MODE_COUNT
};

static void setShadowMask()
{
	static char filename[1024];
	has_shadow_mask = 0;

	if (!spi_uio_cmd_cont(UIO_SHADOWMASK))
	{
		DisableIO();
		return;
	}

	has_shadow_mask = 1;
	switch( video_get_shadow_mask_mode() )
	{
		default: spi_w(SM_FLAG(0)); break;
		case SM_MODE_1X: spi_w(SM_FLAG(SM_FLAG_ENABLED)); break;
		case SM_MODE_2X: spi_w(SM_FLAG(SM_FLAG_ENABLED | SM_FLAG_2X)); break;
		case SM_MODE_1X_ROTATED: spi_w(SM_FLAG(SM_FLAG_ENABLED | SM_FLAG_ROTATED)); break;
		case SM_MODE_2X_ROTATED: spi_w(SM_FLAG(SM_FLAG_ENABLED | SM_FLAG_ROTATED | SM_FLAG_2X)); break;
	}

	int loaded = 0;
	snprintf(filename, sizeof(filename), SMASK_DIR"/%s", shadow_mask_cfg + 1);

	fileTextReader reader;
	if (FileOpenTextReader(&reader, filename))
	{
		char *start_pos = reader.pos;
		const char *line;
		uint32_t res = 0;
		while ((line = FileReadLine(&reader)))
		{
			if (!strncasecmp(line, "resolution=", 11))
			{
				if (sscanf(line + 11, "%u", &res))
				{
					if (v_cur.item[5] >= res)
					{
						start_pos = reader.pos;
					}
				}
			}
		}

		int w = -1, h = -1;
		int y = 0;
		int v2 = 0;

		reader.pos = start_pos;
		while ((line = FileReadLine(&reader)))
		{
			if (w == -1)
			{
				if (!strcasecmp(line, "v2"))
				{
					v2 = 1;
					continue;
				}

				if (!strncasecmp(line, "resolution=", 11))
				{
					continue;
				}

				int n = sscanf(line, "%d,%d", &w, &h);
				if ((n != 2) || (w <= 0) || (h <= 0) || (w > 16) || (h > 16))
				{
					break;
				}
			}
			else
			{
				unsigned int p[16];
				int n = sscanf(line, "%X,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x", p + 0, p + 1, p + 2, p + 3, p + 4, p + 5, p + 6, p + 7, p + 8, p + 9, p + 10, p + 11, p + 12, p + 13, p + 14, p + 15);
				if (n != w)
				{
					break;
				}

				for (int x = 0; x < 16; x++) spi_w(SM_LUT(v2 ? (p[x] & 0x7FF) : (((p[x] & 7) << 8) | 0x2A)));
				y += 1;

				if (y == h)
				{
					loaded = 1;
					break;
				}
			}
		}

		if (y == h)
		{
			spi_w(SM_HMAX(w - 1));
			spi_w(SM_VMAX(h - 1));
		}
	}

	if (!loaded) spi_w(SM_FLAG(0));
	DisableIO();
}

int video_get_shadow_mask_mode()
{
	return has_shadow_mask ? shadow_mask_cfg[0] : -1;
}

char* video_get_shadow_mask()
{
	return shadow_mask_cfg + 1;
}

static char shadow_mask_cfg_path[1024] = { 0 };

void video_set_shadow_mask_mode(int n)
{
	if( n >= SM_MODE_COUNT )
	{
		n = 0;
	}
	else if (n < 0)
	{
		n = SM_MODE_COUNT - 1;
	}

	shadow_mask_cfg[0] = (char)n;
	FileSaveConfig(shadow_mask_cfg_path, &shadow_mask_cfg, sizeof(shadow_mask_cfg));
	setShadowMask();
}

void video_set_shadow_mask(char *name)
{
	strcpy(shadow_mask_cfg + 1, name);
	FileSaveConfig(shadow_mask_cfg_path, &shadow_mask_cfg, sizeof(shadow_mask_cfg));
	setShadowMask();
	user_io_send_buttons(1);
}

static void loadShadowMaskCfg()
{
	sprintf(shadow_mask_cfg_path, "%s_shmask.cfg", user_io_get_core_name());
	if (!FileLoadConfig(shadow_mask_cfg_path, &shadow_mask_cfg, sizeof(shadow_mask_cfg) - 1))
	{
		memset(shadow_mask_cfg, 0, sizeof(shadow_mask_cfg));
		if (cfg.shmask_default[0])
		{
			strcpy(shadow_mask_cfg + 1, cfg.shmask_default);
			shadow_mask_cfg[0] = cfg.shmask_mode_default;
		}
	}

	if( shadow_mask_cfg[0] >= SM_MODE_COUNT )
	{
		shadow_mask_cfg[0] = 0;
	}
}


#define IS_NEWLINE(c) (((c) == '\r') || ((c) == '\n'))
#define IS_WHITESPACE(c) (IS_NEWLINE(c) || ((c) == ' ') || ((c) == '\t'))

static char* get_preset_arg(const char *str)
{
	static char par[1024];
	snprintf(par, sizeof(par), "%s", str);
	char *pos = par;

	while (*pos && !IS_NEWLINE(*pos)) pos++;
	*pos-- = 0;

	while (pos >= par)
	{
		if (!IS_WHITESPACE(*pos)) break;
		*pos-- = 0;
	}

	return par;
}

static void load_flt_pres(const char *str, int type)
{
	char *arg = get_preset_arg(str);
	if (arg[0])
	{
		if (!strcasecmp(arg, "same") || !strcasecmp(arg, "off"))
		{
			video_set_scaler_flt(type, 0);
		}
		else
		{
			video_set_scaler_coeff(type, arg);
			video_set_scaler_flt(type, 1);
		}
	}
}

void video_loadPreset(char *name)
{
	char *arg;
	fileTextReader reader;
	if (FileOpenTextReader(&reader, name))
	{
		const char *line;
		while ((line = FileReadLine(&reader)))
		{
			if (!strncasecmp(line, "hfilter=", 8))
			{
				load_flt_pres(line + 8, VFILTER_HORZ);
			}
			else if (!strncasecmp(line, "vfilter=", 8))
			{
				load_flt_pres(line + 8, VFILTER_VERT);
			}
			else if (!strncasecmp(line, "sfilter=", 8))
			{
				load_flt_pres(line + 8, VFILTER_SCAN);
			}
			else if (!strncasecmp(line, "mask=", 5))
			{
				arg = get_preset_arg(line + 5);
				if (arg[0])
				{
					if (!strcasecmp(arg, "off") || !strcasecmp(arg, "none")) video_set_shadow_mask_mode(0);
					else video_set_shadow_mask(arg);
				}
			}
			else if (!strncasecmp(line, "maskmode=", 9))
			{
				arg = get_preset_arg(line + 9);
				if (arg[0])
				{
					if (!strcasecmp(arg, "off") || !strcasecmp(arg, "none")) video_set_shadow_mask_mode(0);
					else if (!strcasecmp(arg, "1x")) video_set_shadow_mask_mode(SM_MODE_1X);
					else if (!strcasecmp(arg, "2x")) video_set_shadow_mask_mode(SM_MODE_2X);
					else if (!strcasecmp(arg, "1x rotated")) video_set_shadow_mask_mode(SM_MODE_1X_ROTATED);
					else if (!strcasecmp(arg, "2x rotated")) video_set_shadow_mask_mode(SM_MODE_2X_ROTATED);
				}
			}
			else if (!strncasecmp(line, "gamma=", 6))
			{
				arg = get_preset_arg(line + 6);
				if (arg[0])
				{
					if (!strcasecmp(arg, "off") || !strcasecmp(arg, "none")) video_set_gamma_en(0);
					else
					{
						video_set_gamma_curve(arg);
						video_set_gamma_en(1);
					}
				}

			}
		}
	}
}

static char fb_reset_cmd[128] = {};
static void set_video(vmode_custom_t *v, double Fpix)
{
	loadGammaCfg();
	setGamma();

	loadScalerCfg();
	setScaler();

	v_cur = *v;
	vmode_custom_t v_fix = v_cur;
	if (cfg.direct_video)
	{
		v_fix.item[2] = FB_DV_RBRD;
		v_fix.item[4] = FB_DV_LBRD;
		v_fix.item[1] += v_cur.item[2] - v_fix.item[2];
		v_fix.item[1] += v_cur.item[4] - v_fix.item[4];

		v_fix.item[6] = FB_DV_BBRD;
		v_fix.item[8] = FB_DV_UBRD;;
		v_fix.item[5] += v_cur.item[6] - v_fix.item[6];
		v_fix.item[5] += v_cur.item[8] - v_fix.item[8];
	}

	printf("Send HDMI parameters:\n");
	spi_uio_cmd_cont(UIO_SET_VIDEO);
	printf("video: ");
	for (int i = 1; i <= 8; i++)
	{
		spi_w(v_fix.item[i]);
		printf("%d(%d), ", v_cur.item[i], v_fix.item[i]);
	}

	if(Fpix) setPLL(Fpix, &v_cur);

	printf("\nPLL: ");
	for (int i = 9; i < 21; i++)
	{
		printf("0x%X, ", v_cur.item[i]);
		if (i & 1) spi_w(v_cur.item[i] | ((i == 9 && Fpix && cfg.vsync_adjust == 2 && !is_menu()) ? 0x8000 : 0));
		else
		{
			spi_w(v_cur.item[i]);
			spi_w(v_cur.item[i] >> 16);
		}
	}

	printf("Fpix=%f\n", v_cur.Fpix);
	DisableIO();

	if (cfg.fb_size <= 1) cfg.fb_size = ((v_cur.item[1] * v_cur.item[5]) <= FB_SIZE) ? 1 : 2;
	else if (cfg.fb_size == 3) cfg.fb_size = 2;
	else if (cfg.fb_size > 4) cfg.fb_size = 4;

	fb_width = v_cur.item[1] / cfg.fb_size;
	fb_height = v_cur.item[5] / cfg.fb_size;

	brd_x = cfg.vscale_border / cfg.fb_size;;
	brd_y = cfg.vscale_border / cfg.fb_size;;

	if (fb_enabled) video_fb_enable(1, fb_num);

	sprintf(fb_reset_cmd, "echo %d %d %d %d %d >/sys/module/MiSTer_fb/parameters/mode", 8888, 1, fb_width, fb_height, fb_width * 4);
	system(fb_reset_cmd);

	loadShadowMaskCfg();
	setShadowMask();
}

static int parse_custom_video_mode(char* vcfg, vmode_custom_t *v)
{
	int khz = 0;
	int cnt = 0;
	char *orig = vcfg;
	while (*vcfg)
	{
		char *next;
		if (cnt == 9 && v->item[0] == 1)
		{
			double Fpix = khz ? strtoul(vcfg, &next, 0)/1000.f : strtod(vcfg, &next);
			if (vcfg == next || (Fpix < 2.f || Fpix > 300.f))
			{
				printf("Error parsing video_mode parameter: ""%s""\n", orig);
				return -1;
			}

			setPLL(Fpix, v);
			break;
		}

		uint32_t val = strtoul(vcfg, &next, 0);
		if (vcfg == next || (*next != ',' && *next))
		{
			printf("Error parsing video_mode parameter: ""%s""\n", orig);
			return -1;
		}

		if (!cnt && val >= 100)
		{
			v->item[cnt++] = 1;
			khz = 1;
		}
		if (cnt < 32) v->item[cnt] = val;
		if (*next == ',') next++;
		vcfg = next;
		cnt++;
	}

	if (cnt == 1)
	{
		printf("Set predefined video_mode to %d\n", v->item[0]);
		return v->item[0];
	}

	if ((v->item[0] == 0 && cnt < 21) || (v->item[0] == 1 && cnt < 9))
	{
		printf("Incorrect amount of items in video_mode parameter: %d\n", cnt);
		return -1;
	}

	if (v->item[0] > 1)
	{
		printf("Incorrect video_mode parameter\n");
		return -1;
	}

	return -2;
}

static int store_custom_video_mode(char* vcfg, vmode_custom_t *v)
{
	int ret = parse_custom_video_mode(vcfg, v);
	if (ret == -2) return 1;

	uint mode = (ret < 0) ? 0 : ret;
	if (mode >= VMODES_NUM) mode = 0;
	for (int i = 0; i < 8; i++) v->item[i + 1] = vmodes[mode].vpar[i];
	setPLL(vmodes[mode].Fpix, v);

	return ret >= 0;
}

static void fb_init()
{
	if (!fb_base)
	{
		fb_base = (volatile uint32_t*)shmem_map(FB_ADDR, FB_SIZE * 4 * 3);
		if (!fb_base)
		{
			printf("Unable to mmap FB!\n");
		}
	}
	spi_uio_cmd16(UIO_SET_FBUF, 0);
}

void video_mode_load()
{
	fb_init();
	if (cfg.direct_video && cfg.vsync_adjust)
	{
		printf("Disabling vsync_adjust because of enabled direct video.\n");
		cfg.vsync_adjust = 0;
	}

	if (cfg.direct_video)
	{
		int mode = cfg.menu_pal ? 2 : 0;
		if (cfg.forced_scandoubler) mode++;

		v_def.item[0] = mode;
		for (int i = 0; i < 8; i++) v_def.item[i + 1] = tvmodes[mode].vpar[i];
		setPLL(tvmodes[mode].Fpix, &v_def);

		vmode_def = 1;
		vmode_pal = 0;
		vmode_ntsc = 0;
	}
	else
	{
		vmode_def = store_custom_video_mode(cfg.video_conf, &v_def);
		vmode_pal = store_custom_video_mode(cfg.video_conf_pal, &v_pal);
		vmode_ntsc = store_custom_video_mode(cfg.video_conf_ntsc, &v_ntsc);
	}
	set_video(&v_def, 0);
}

static int api1_5 = 0;
int hasAPI1_5()
{
	return api1_5 || is_menu();
}

static uint32_t show_video_info(int force)
{
	uint32_t ret = 0;
	static uint16_t nres = 0;
	spi_uio_cmd_cont(UIO_GET_VRES);
	uint16_t res = spi_w(0);
	if ((nres != res) || force)
	{
		if (nres != res) force = 0;
		nres = res;
		uint32_t width = spi_w(0) | (spi_w(0) << 16);
		uint32_t height = spi_w(0) | (spi_w(0) << 16);
		uint32_t htime = spi_w(0) | (spi_w(0) << 16);
		uint32_t vtime = spi_w(0) | (spi_w(0) << 16);
		uint32_t ptime = spi_w(0) | (spi_w(0) << 16);
		uint32_t vtimeh = spi_w(0) | (spi_w(0) << 16);
		DisableIO();

		float vrate = 100000000;
		if (vtime) vrate /= vtime; else vrate = 0;
		float hrate = 100000;
		if (htime) hrate /= htime; else hrate = 0;

		float prate = width * 100;
		prate /= ptime;

		printf("\033[1;33mINFO: Video resolution: %u x %u%s, fHorz = %.1fKHz, fVert = %.1fHz, fPix = %.2fMHz\033[0m\n", width, height, (res & 0x100) ? "i" : "", hrate, vrate, prate);
		printf("\033[1;33mINFO: Frame time (100MHz counter): VGA = %d, HDMI = %d\033[0m\n", vtime, vtimeh);

		if (vtimeh) api1_5 = 1;
		if (hasAPI1_5() && cfg.video_info)
		{
			static char str[128], res1[16], res2[16];
			float vrateh = 100000000;
			if (vtimeh) vrateh /= vtimeh; else vrateh = 0;
			sprintf(res1, "%dx%d%s", width, height, (res & 0x100) ? "i" : "");
			sprintf(res2, "%dx%d", v_cur.item[1], v_cur.item[5]);
			sprintf(str, "%9s %6.2fKHz %4.1fHz\n" \
				         "\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\n" \
				         "%9s %6.2fMHz %4.1fHz",
				res1,  hrate, vrate, res2, v_cur.Fpix, vrateh);
			Info(str, cfg.video_info * 1000);
		}

		uint32_t scrh = v_cur.item[5];
		if (scrh)
		{
			if (cfg.vscale_mode && height)
			{
				uint32_t div = 1 << (cfg.vscale_mode - 1);
				uint32_t mag = (scrh*div) / height;
				scrh = (height * mag) / div;
				printf("Set vertical scaling to : %d\n", scrh);
				spi_uio_cmd16(UIO_SETHEIGHT, scrh);
			}
			else if(cfg.vscale_border)
			{
				uint32_t border = cfg.vscale_border * 2;
				if ((border + 100) > scrh) border = scrh - 100;
				scrh -= border;
				printf("Set max vertical resolution to : %d\n", scrh);
				spi_uio_cmd16(UIO_SETHEIGHT, scrh);
			}
			else
			{
				spi_uio_cmd16(UIO_SETHEIGHT, 0);
			}
		}

		uint32_t scrw = v_cur.item[1];
		if (scrw)
		{
			if (cfg.vscale_border && !(cfg.vscale_mode && height))
			{
				uint32_t border = cfg.vscale_border * 2;
				if ((border + 100) > scrw) border = scrw - 100;
				scrw -= border;
				printf("Set max horizontal resolution to : %d\n", scrw);
				spi_uio_cmd16(UIO_SETWIDTH, scrw);
			}
			else
			{
				spi_uio_cmd16(UIO_SETWIDTH, 0);
			}
		}

		if (vtime && vtimeh) ret = vtime;
		minimig_set_adjust(2);
	}
	else
	{
		DisableIO();
	}

	return force ? 0 : ret;
}

void video_mode_adjust()
{
	static int force = 0;

	uint32_t vtime = show_video_info(force);
	force = 0;

	if (vtime && cfg.vsync_adjust && !is_menu())
	{
		printf("\033[1;33madjust_video_mode(%u): vsync_adjust=%d", vtime, cfg.vsync_adjust);

		int adjust = 1;
		vmode_custom_t *v = &v_def;
		if (vmode_pal || vmode_ntsc)
		{
			if (vtime > 1800000)
			{
				if (vmode_pal)
				{
					printf(", using PAL mode");
					v = &v_pal;
				}
				else
				{
					printf(", PAL mode cannot be used. Using predefined NTSC mode");
					v = &v_ntsc;
					adjust = 0;
				}
			}
			else
			{
				if (vmode_ntsc)
				{
					printf(", using NTSC mode");
					v = &v_ntsc;
				}
				else
				{
					printf(", NTSC mode cannot be used. Using predefined PAL mode");
					v = &v_pal;
					adjust = 0;
				}
			}
		}

		printf(".\033[0m\n");

		double Fpix = 0;
		if (adjust)
		{
			Fpix = 100 * (v->item[1] + v->item[2] + v->item[3] + v->item[4]) * (v->item[5] + v->item[6] + v->item[7] + v->item[8]);
			Fpix /= vtime;
			if (Fpix < 2.f || Fpix > 300.f)
			{
				printf("Estimated Fpix(%.4f MHz) is outside supported range. Canceling auto-adjust.\n", Fpix);
				Fpix = 0;
			}

			uint32_t hz = 100000000 / vtime;
			if (cfg.refresh_min && hz < cfg.refresh_min)
			{
				printf("Estimated frame rate (%d Hz) is less than MONITOR_HZ_MIN(%d Hz). Canceling auto-adjust.\n", hz, cfg.refresh_min);
				Fpix = 0;
			}

			if (cfg.refresh_max && hz > cfg.refresh_max)
			{
				printf("Estimated frame rate (%d Hz) is more than MONITOR_HZ_MAX(%d Hz). Canceling auto-adjust.\n", hz, cfg.refresh_max);
				Fpix = 0;
			}
		}

		set_video(v, Fpix);
		user_io_send_buttons(1);
		force = 1;
	}
	else
	{
		set_vfilter(0);
	}
}

void video_fb_enable(int enable, int n)
{
	if (fb_base)
	{
		int res = spi_uio_cmd_cont(UIO_SET_FBUF);
		if (res)
		{
			if (is_menu() && !enable && menu_bg)
			{
				enable = 1;
				n = menu_bgn;
			}

			if (enable)
			{
				uint32_t fb_addr = FB_ADDR + (FB_SIZE * 4 * n) + (n ? 0 : 4096);
				fb_num = n;

				int xoff = 0, yoff = 0;
				if (cfg.direct_video)
				{
					xoff = v_cur.item[4] - FB_DV_LBRD;
					yoff = v_cur.item[8] - FB_DV_UBRD;
				}

				//printf("Switch to Linux frame buffer\n");
				spi_w((uint16_t)(FB_EN | FB_FMT_RxB | FB_FMT_8888)); // format, enable flag
				spi_w((uint16_t)fb_addr); // base address low word
				spi_w(fb_addr >> 16);     // base address high word
				spi_w(fb_width);          // frame width
				spi_w(fb_height);         // frame height
				spi_w(xoff);                 // scaled left
				spi_w(xoff + v_cur.item[1] - 1); // scaled right
				spi_w(yoff);                 // scaled top
				spi_w(yoff + v_cur.item[5] - 1); // scaled bottom
				spi_w(fb_width * 4);      // stride

				//printf("Linux frame buffer: %dx%d, stride = %d bytes\n", fb_width, fb_height, fb_width * 4);
				if (!fb_num)
				{
					system(fb_reset_cmd);
					input_switch(0);
				}
				else
				{
					input_switch(1);
				}
			}
			else
			{
				printf("Switch to core frame buffer\n");
				spi_w(0); // enable flag
				input_switch(1);
			}

			fb_enabled = enable;
		}
		else
		{
			printf("Core doesn't support HPS frame buffer\n");
			input_switch(1);
		}

		DisableIO();
		if (cfg.direct_video) set_vga_fb(enable);
		if (is_menu()) user_io_8bit_set_status((fb_enabled && !fb_num) ? 0x160 : 0, 0x1E0);
	}
}

int video_fb_state()
{
	if (is_menu())
	{
		return fb_enabled && !fb_num;
	}

	return fb_enabled;
}

static void draw_checkers()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);

	uint32_t col1 = 0x888888;
	uint32_t col2 = 0x666666;
	int sz = fb_width / 128;

	for (int y = brd_y; y < fb_height - brd_y; y++)
	{
		int c1 = (y / sz) & 1;
		int pos = y * fb_width;
		for (int x = brd_x; x < fb_width - brd_x; x++)
		{
			int c2 = c1 ^ ((x / sz) & 1);
			buf[pos + x] = c2 ? col2 : col1;
		}
	}
}

static void draw_hbars1()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);
	int height = fb_height - 2 * brd_y;

	int old_base = 0;
	int gray = 255;
	int sz = height / 7;
	int stp = 0;

	for (int y = brd_y; y < fb_height - brd_y; y++)
	{
		int pos = y * fb_width;
		int base_color = ((7 * (y-brd_y)) / height) + 1;
		if (old_base != base_color)
		{
			stp = sz;
			old_base = base_color;
		}

		gray = 255 * stp / sz;

		for (int x = brd_x; x < fb_width - brd_x; x++)
		{
			uint32_t color = 0;
			if (base_color & 4) color |= gray;
			if (base_color & 2) color |= gray << 8;
			if (base_color & 1) color |= gray << 16;
			buf[pos + x] = color;
		}

		stp--;
		if (stp < 0) stp = 0;
	}
}

static void draw_hbars2()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);
	int height = fb_height - 2 * brd_y;
	int width = fb_width - 2 * brd_x;

	for (int y = brd_y; y < fb_height - brd_y; y++)
	{
		int pos = y * fb_width;
		int base_color = ((14 * (y - brd_y)) / height);
		int inv = base_color & 1;
		base_color >>= 1;
		base_color = (inv ? base_color : 6 - base_color) + 1;
		for (int x = brd_x; x < fb_width - brd_x; x++)
		{
			int gray = (256 * (x - brd_x)) / width;
			if (inv) gray = 255 - gray;
			uint32_t color = 0;
			if (base_color & 4) color |= gray;
			if (base_color & 2) color |= gray << 8;
			if (base_color & 1) color |= gray << 16;
			buf[pos + x] = color;
		}
	}
}

static void draw_vbars1()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);
	int width = fb_width - 2 * brd_x;

	int sz = width / 7;
	int stp = 0;

	for (int y = brd_y; y < fb_height - brd_y; y++)
	{
		int pos = y * fb_width;
		int old_base = 0;
		int gray = 255;
		for (int x = brd_x; x < fb_width - brd_x; x++)
		{
			int base_color = ((7 * (x - brd_x)) / width) + 1;
			if (old_base != base_color)
			{
				stp = sz;
				old_base = base_color;
			}

			gray = 255 * stp / sz;

			uint32_t color = 0;
			if (base_color & 4) color |= gray;
			if (base_color & 2) color |= gray << 8;
			if (base_color & 1) color |= gray << 16;
			buf[pos + x] = color;

			stp--;
			if (stp < 0) stp = 0;
		}
	}
}

static void draw_vbars2()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);
	int height = fb_height - 2 * brd_y;
	int width = fb_width - 2 * brd_x;

	for (int y = brd_y; y < fb_height - brd_y; y++)
	{
		int pos = y * fb_width;
		for (int x = brd_x; x < fb_width - brd_x; x++)
		{
			int gray = ((256 * (y - brd_y)) / height);
			int base_color = ((14 * (x - brd_x)) / width);
			int inv = base_color & 1;
			base_color >>= 1;
			base_color = (inv ? base_color : 6 - base_color) + 1;

			if (inv) gray = 255 - gray;
			uint32_t color = 0;
			if (base_color & 4) color |= gray;
			if (base_color & 2) color |= gray << 8;
			if (base_color & 1) color |= gray << 16;
			buf[pos + x] = color;
		}
	}
}

static void draw_spectrum()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);
	int height = fb_height - 2 * brd_y;
	int width = fb_width - 2 * brd_x;

	for (int y = brd_y; y < fb_height - brd_y; y++)
	{
		int pos = y * fb_width;
		int blue = ((256 * (y - brd_y)) / height);
		for (int x = brd_x; x < fb_width - brd_x; x++)
		{
			int green = ((256 * (x - brd_x)) / width) - blue / 2;
			int red = 255 - green - blue / 2;
			if (red < 0) red = 0;
			if (green < 0) green = 0;

			buf[pos + x] = (red << 16) | (green << 8) | blue;
		}
	}
}

static void draw_black()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);

	for (int y = 0; y < fb_height; y++)
	{
		int pos = y * fb_width;
		for (int x = 0; x < fb_width; x++) buf[pos++] = 0;
	}
}

static uint64_t getus()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 10000000) + tv.tv_usec;
}

static void vs_wait()
{
	int fb = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	int zero = 0;
	uint64_t t1, t2;
	if (ioctl(fb, FBIO_WAITFORVSYNC, &zero) == -1)
	{
		printf("fb ioctl failed: %s\n", strerror(errno));
		close(fb);
		return;
	}

	t1 = getus();
	ioctl(fb, FBIO_WAITFORVSYNC, &zero);
	t2 = getus();
	close(fb);

	printf("vs_wait(us): %llu\n", t2 - t1);
}

static char *get_file_fromdir(const char* dir, int num, int *count)
{
	static char name[256+32];
	name[0] = 0;
	if(count) *count = 0;
	DIR *d = opendir(getFullPath(dir));
	if (d)
	{
		int cnt = 0;
		struct dirent *de = readdir(d);
		while (de)
		{
			int len = strlen(de->d_name);
			if (len > 4 && (!strcasecmp(de->d_name + len - 4, ".png") || !strcasecmp(de->d_name + len - 4, ".jpg")))
			{
				if (num == cnt) break;
				cnt++;
			}

			de = readdir(d);
		}

		if (de)
		{
			snprintf(name, sizeof(name), "%s/%s", dir, de->d_name);
		}
		closedir(d);
		if(count) *count = cnt;
	}

	return name;
}

static Imlib_Image load_bg()
{
	const char* fname = "menu.png";
	if (!FileExists(fname))
	{
		fname = "menu.jpg";
		if (!FileExists(fname)) fname = 0;
	}

	if (!fname)
	{
		char bgdir[32];

		int alt = altcfg();
		sprintf(bgdir, "wallpapers_alt_%d", alt);
		if (alt == 1 && !PathIsDir(bgdir)) strcpy(bgdir, "wallpapers_alt");
		if (alt <= 0 || !PathIsDir(bgdir)) strcpy(bgdir, "wallpapers");

		if (PathIsDir(bgdir))
		{
			int rndfd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
			if (rndfd >= 0)
			{
				uint32_t rnd;
				read(rndfd, &rnd, sizeof(rnd));
				close(rndfd);

				int count = 0;
				get_file_fromdir(bgdir, -1, &count);
				if (count > 0) fname = get_file_fromdir(bgdir, rnd % count, &count);
			}
		}
	}

	if (fname)
	{
		Imlib_Load_Error error = IMLIB_LOAD_ERROR_NONE;
		Imlib_Image img = imlib_load_image_with_error_return(getFullPath(fname), &error);
		if (img) return img;
		printf("Image %s loading error %d\n", fname, error);
	}

	return NULL;
}

static int bg_has_picture = 0;
extern uint8_t  _binary_logo_png_start[], _binary_logo_png_end[];
void video_menu_bg(int n, int idle)
{
	bg_has_picture = 0;
	menu_bg = n;
	if (n)
	{
		//printf("**** BG DEBUG START ****\n");
		//printf("n = %d\n", n);

		Imlib_Load_Error error;
		static Imlib_Image logo = 0;
		if (!logo)
		{
			unlink("/tmp/logo.png");
			if (FileSave("/tmp/logo.png", _binary_logo_png_start, _binary_logo_png_end - _binary_logo_png_start))
			{
				while(1)
				{
					error = IMLIB_LOAD_ERROR_NONE;
					if ((logo = imlib_load_image_with_error_return("/tmp/logo.png", &error))) break;
					else
					{
						if (error != IMLIB_LOAD_ERROR_NO_LOADER_FOR_FILE_FORMAT)
						{
							printf("logo.png error = %d\n", error);
							break;
						}
					}
					vs_wait();
				};

				if (cfg.osd_rotate)
				{
					imlib_context_set_image(logo);
					imlib_image_orientate(cfg.osd_rotate == 1 ? 3 : 1);
				}
			}
			else
			{
				printf("Fail to save to /tmp/logo.png\n");
			}
			unlink("/tmp/logo.png");
			printf("Logo = %p\n", logo);
		}

		menu_bgn = (menu_bgn == 1) ? 2 : 1;

		static Imlib_Image menubg = 0;
		static Imlib_Image bg1 = 0, bg2 = 0;
		if (!bg1) bg1 = imlib_create_image_using_data(fb_width, fb_height, (uint32_t*)(fb_base + (FB_SIZE * 1)));
		if (!bg1) printf("Warning: bg1 is 0\n");
		if (!bg2) bg2 = imlib_create_image_using_data(fb_width, fb_height, (uint32_t*)(fb_base + (FB_SIZE * 2)));
		if (!bg2) printf("Warning: bg2 is 0\n");

		Imlib_Image *bg = (menu_bgn == 1) ? &bg1 : &bg2;
		//printf("*bg = %p\n", *bg);

		static Imlib_Image curtain = 0;
		if (!curtain)
		{
			curtain = imlib_create_image(fb_width, fb_height);
			imlib_context_set_image(curtain);
			imlib_image_set_has_alpha(1);

			uint32_t *data = imlib_image_get_data();
			int sz = fb_width * fb_height;
			for (int i = 0; i < sz; i++)
			{
				*data++ = 0x9F000000;
			}
		}

		draw_black();

		switch (n)
		{
		case 1:
			if (!menubg) menubg = load_bg();
			if (menubg)
			{
				imlib_context_set_image(menubg);
				int src_w = imlib_image_get_width();
				int src_h = imlib_image_get_height();
				//printf("menubg: src_w=%d, src_h=%d\n", src_w, src_h);

				if (*bg)
				{
					imlib_context_set_image(*bg);
					imlib_blend_image_onto_image(menubg, 0,
						0, 0,                           //int source_x, int source_y,
						src_w, src_h,                   //int source_width, int source_height,
						brd_x, brd_y,                   //int destination_x, int destination_y,
						fb_width - (brd_x * 2), fb_height - (brd_y * 2) //int destination_width, int destination_height
					);
					bg_has_picture = 1;
					break;
				}
				else
				{
					printf("*bg = 0!\n");
				}
			}
			draw_checkers();
			break;
		case 2:
			draw_hbars1();
			break;
		case 3:
			draw_hbars2();
			break;
		case 4:
			draw_vbars1();
			break;
		case 5:
			draw_vbars2();
			break;
		case 6:
			draw_spectrum();
			break;
		case 7:
			draw_black();
			break;
		}

		if (cfg.logo && logo && !idle)
		{
			imlib_context_set_image(logo);

			int src_w = imlib_image_get_width();
			int src_h = imlib_image_get_height();

			printf("logo: src_w=%d, src_h=%d\n", src_w, src_h);

			int width = fb_width - (brd_x * 2);
			int height = fb_height - (brd_y * 2);

			int dst_w, dst_h;
			int dst_x, dst_y;
			if (cfg.osd_rotate)
			{
				dst_h = height / 2;
				dst_w = src_w * dst_h / src_h;
				if (cfg.osd_rotate == 1)
				{
					dst_x = brd_x;
					dst_y = height - dst_h;
				}
				else
				{
					dst_x = width - dst_w;
					dst_y = brd_y;
				}
			}
			else
			{
				dst_x = brd_x;
				dst_y = brd_y;
				dst_w = width * 2 / 7;
				dst_h = src_h * dst_w / src_w;
			}

			if (*bg)
			{
				if (cfg.direct_video && (v_cur.item[5] < 300)) dst_h /= 2;

				imlib_context_set_image(*bg);
				imlib_blend_image_onto_image(logo, 1,
					0, 0,         //int source_x, int source_y,
					src_w, src_h, //int source_width, int source_height,
					dst_x, dst_y, //int destination_x, int destination_y,
					dst_w, dst_h  //int destination_width, int destination_height
				);
			}
			else
			{
				printf("*bg = 0!\n");
			}
		}

		if (curtain)
		{
			if (idle > 1 && *bg)
			{
				imlib_context_set_image(*bg);
				imlib_blend_image_onto_image(curtain, 1,
					0, 0,                //int source_x, int source_y,
					fb_width, fb_height, //int source_width, int source_height,
					0, 0,                //int destination_x, int destination_y,
					fb_width, fb_height  //int destination_width, int destination_height
				);
			}
		}
		else
		{
			printf("curtain = 0!\n");
		}

		//test the fb driver
		//vs_wait();
		//printf("**** BG DEBUG END ****\n");
	}

	video_fb_enable(0);
}

int video_bg_has_picture()
{
	return bg_has_picture;
}

int video_chvt(int num)
{
	static int cur_vt = 0;
	if (num)
	{
		cur_vt = num;
		int fd;
		if ((fd = open("/dev/tty0", O_RDONLY | O_CLOEXEC)) >= 0)
		{
			if (ioctl(fd, VT_ACTIVATE, cur_vt)) printf("ioctl VT_ACTIVATE fails\n");
			if (ioctl(fd, VT_WAITACTIVE, cur_vt)) printf("ioctl VT_WAITACTIVE fails\n");
			close(fd);
		}
	}

	return cur_vt ? cur_vt : 1;
}

void video_cmd(char *cmd)
{
	if (video_fb_state())
	{
		int accept = 0;
		int fmt = 0, rb = 0, div = -1, width = -1, height = -1;
		uint16_t hmin, hmax, vmin, vmax;
		if (sscanf(cmd, "fb_cmd0 %d %d %d", &fmt, &rb, &div) == 3)
		{
			if (div >= 1 && div <= 4)
			{
				width = v_cur.item[1] / div;
				height = v_cur.item[5] / div;
				hmin = vmin = 0;
				hmax = v_cur.item[1] - 1;
				vmax = v_cur.item[5] - 1;
				accept = 1;
			}
		}

		if (sscanf(cmd, "fb_cmd2 %d %d %d", &fmt, &rb, &div) == 3)
		{
			if (div >= 1 && div <= 4)
			{
				width = v_cur.item[1] / div;
				height = v_cur.item[5] / div;
				hmin = vmin = 0;
				hmax = v_cur.item[1] - 1;
				vmax = v_cur.item[5] - 1;
				accept = 1;
			}
		}

		if (sscanf(cmd, "fb_cmd1 %d %d %d %d", &fmt, &rb, &width, &height) == 4)
		{
			if (width < 120 || width > (int)v_cur.item[1]) width = v_cur.item[1];
			if (height < 120 || height > (int)v_cur.item[5]) height = v_cur.item[5];

			int divx = 1;
			int divy = 1;
			if (cfg.direct_video && (v_cur.item[5] < 300))
			{
				// TV 240P/288P
				while ((width*(divx + 1)) <= (int)v_cur.item[1]) divx++;
				while ((height*(divy + 1)) <= (int)v_cur.item[5]) divy++;
			}
			else
			{
				while ((width*(divx + 1)) <= (int)v_cur.item[1] && (height*(divx + 1)) <= (int)v_cur.item[5]) divx++;
				divy = divx;
			}

			hmin = (uint16_t)((v_cur.item[1] - (width * divx)) / 2);
			vmin = (uint16_t)((v_cur.item[5] - (height * divy)) / 2);
			hmax = hmin + (width * divx) - 1;
			vmax = vmin + (height * divy) - 1;
			accept = 1;
		}

		int bpp = 0;
		int sc_fmt = 0;

		if (accept)
		{
			switch (fmt)
			{
			case 8888:
				bpp = 4;
				sc_fmt = FB_FMT_8888;
				break;

			case 1555:
				bpp = 2;
				sc_fmt = FB_FMT_1555;
				break;

			case 565:
				bpp = 2;
				sc_fmt = FB_FMT_565;
				break;

			case 8:
				bpp = 1;
				sc_fmt = FB_FMT_PAL8;
				rb = 0;
				break;

			default:
				accept = 0;
			}
		}

		if (rb)
		{
			sc_fmt |= FB_FMT_RxB;
			rb = 1;
		}

		if(accept)
		{
			int stride = ((width * bpp) + 15) & ~15;
			printf("fb_cmd: new mode: %dx%d => %dx%d color=%d stride=%d\n", width, height, hmax - hmin + 1, vmax - vmin + 1, fmt, stride);

			uint32_t addr = FB_ADDR + 4096;

			int xoff = 0, yoff = 0;
			if (cfg.direct_video)
			{
				xoff = v_cur.item[4] - FB_DV_LBRD;
				yoff = v_cur.item[8] - FB_DV_UBRD;
			}

			spi_uio_cmd_cont(UIO_SET_FBUF);
			spi_w(FB_EN | sc_fmt); // format, enable flag
			spi_w((uint16_t)addr); // base address low word
			spi_w(addr >> 16);     // base address high word
			spi_w(width);          // frame width
			spi_w(height);         // frame height
			spi_w(xoff + hmin);    // scaled left
			spi_w(xoff + hmax);    // scaled right
			spi_w(yoff + vmin);    // scaled top
			spi_w(yoff + vmax);    // scaled bottom
			spi_w(stride);         // stride
			DisableIO();

			if (cmd[6] != '2')
			{
				static char cmd[256];
				sprintf(cmd, "echo %d %d %d %d %d >/sys/module/MiSTer_fb/parameters/mode", fmt, rb, width, height, stride);
				system(cmd);
			}
		}
		else
		{
			printf("video_cmd: unknown command or format.\n");
		}
	}
}
