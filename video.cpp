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
#include <math.h>

#include "hardware.h"
#include "user_io.h"
#include "spi.h"
#include "cfg.h"
#include "file_io.h"
#include "menu.h"
#include "video.h"
#include "input.h"
#include "shmem.h"
#include "smbus.h"
#include "str_util.h"
#include "profiling.h"
#include "offload.h"

#include "support.h"
#include "lib/imlib2/Imlib2.h"
#include "lib/md5/md5.h"

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

#define VRR_NONE     0x00
#define VRR_FREESYNC 0x01
#define VRR_VESA     0x02

static int     use_vrr = 0;
static uint8_t vrr_min_fr = 0;
static uint8_t vrr_max_fr = 0;

static volatile uint32_t *fb_base = 0;
static int fb_enabled = 0;
static int fb_width = 0;
static int fb_height = 0;
static int fb_num = 0;
static int brd_x = 0;
static int brd_y = 0;

static int menu_bg = 0;
static int menu_bgn = 0;

static VideoInfo current_video_info;

static int support_FHD = 0;

struct vrr_cap_t
{
	uint8_t active;
	uint8_t available;
	uint8_t min_fr;
	uint8_t max_fr;
	char description[128];
};

static vrr_cap_t vrr_modes[3] = {
	{0, 0, 0, 0, "None"},
	{0, 0, 0, 0, "AMD Freesync"},
	{0, 0, 0, 0, "Vesa Forum VRR"},
};

static uint8_t last_vrr_mode = 0xFF;
static float last_vrr_rate = 0.0f;
static uint32_t last_vrr_vfp = 0;
static uint8_t edid[256] = {};

struct vmode_t
{
	uint32_t vpar[8];
	double Fpix;
	uint8_t vic_mode;
	uint8_t pr;
};

vmode_t vmodes[] =
{
	{ { 1280, 110,  40, 220,  720,  5,  5, 20 },  74.25,  4, 0 }, //0  1280x720@60
	{ { 1024,  24, 136, 160,  768,  3,  6, 29 },  65,     0, 0 }, //1  1024x768@60
	{ {  720,  16,  62,  60,  480,  9,  6, 30 },  27,     3, 0 }, //2  720x480@60
	{ {  720,  12,  64,  68,  576,  5,  5, 39 },  27,    18, 0 }, //3  720x576@50
	{ { 1280,  48, 112, 248, 1024,  1,  3, 38 }, 108,     0, 0 }, //4  1280x1024@60
	{ {  800,  40, 128,  88,  600,  1,  4, 23 },  40,     0, 0 }, //5  800x600@60
	{ {  640,  16,  96,  48,  480, 10,  2, 33 },  25.175, 1, 0 }, //6  640x480@60
	{ { 1280, 440,  40, 220,  720,  5,  5, 20 },  74.25, 19, 0 }, //7  1280x720@50
	{ { 1920,  88,  44, 148, 1080,  4,  5, 36 }, 148.5,  16, 0 }, //8  1920x1080@60
	{ { 1920, 528,  44, 148, 1080,  4,  5, 36 }, 148.5,  31, 0 }, //9  1920x1080@50
	{ { 1366,  70, 143, 213,  768,  3,  3, 24 },  85.5,   0, 0 }, //10 1366x768@60
	{ { 1024,  40, 104, 144,  600,  1,  3, 18 },  48.96,  0, 0 }, //11 1024x600@60
	{ { 1920,  48,  32,  80, 1440,  2,  4, 38 }, 185.203, 0, 0 }, //12 1920x1440@60
	{ { 2048,  48,  32,  80, 1536,  2,  4, 38 }, 209.318, 0, 0 }, //13 2048x1536@60
	{ { 1280,  24,  16,  40, 1440,  3,  5, 33 }, 120.75,  0, 1 }, //14 2560x1440@60 (pr)
};
#define VMODES_NUM (sizeof(vmodes) / sizeof(vmodes[0]))

vmode_t tvmodes[] =
{
	{{ 640, 30, 60, 70, 240,  4, 4, 14 }, 12.587, 0, 0 }, //NTSC 15K
	{{ 640, 16, 96, 48, 480,  8, 4, 33 }, 25.175, 0, 0 }, //NTSC 31K
	{{ 640, 30, 60, 70, 288,  6, 4, 14 }, 12.587, 0, 0 }, //PAL 15K
	{{ 640, 16, 96, 48, 576,  2, 4, 42 }, 25.175, 0, 0 }, //PAL 31K
};

// named aliases for vmode_custom_t items
struct vmode_custom_param_t
{
	uint32_t mode;

	// [1]
	uint32_t hact;
	uint32_t hfp;
	uint32_t hs;
	uint32_t hbp;

	// [5]
	uint32_t vact;
	uint32_t vfp;
	uint32_t vs;
	uint32_t vbp;

	// [9]
	uint32_t pll[12];

	// [21]
	uint32_t hpol;
	uint32_t vpol;
	uint32_t vic;
	uint32_t rb;
	uint32_t pr;

	// [26]
	uint32_t unused[6];
};

struct vmode_custom_t
{
	union // anonymous
	{
		vmode_custom_param_t param;
		uint32_t item[32];
	};

	double Fpix;
};

static_assert(sizeof(vmode_custom_param_t) == sizeof(vmode_custom_t::item));

// Static fwd decl
static void video_fb_config();
static void video_calculate_cvt(int horiz_pixels, int vert_pixels, float refresh_rate, int reduced_blanking, vmode_custom_t *vmode);

static vmode_custom_t v_cur = {}, v_def = {}, v_pal = {}, v_ntsc = {};
static int vmode_def = 0, vmode_pal = 0, vmode_ntsc = 0;

static bool supports_pr()
{
	static uint16_t video_version = 0xffff;
	if (video_version == 0xffff) video_version = spi_uio_cmd(UIO_SET_VIDEO) & 1;
	return video_version != 0;
}

static bool supports_vrr()
{
	static uint16_t video_version = 0xffff;
	if (video_version == 0xffff) video_version = spi_uio_cmd(UIO_SET_VIDEO) & 2;
	return video_version != 0;
}

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
	PROFILE_FUNCTION();

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

struct ScalerFilter
{
	char mode;
	char filename[1023];
};

static ScalerFilter scaler_flt[3];

struct FilterPhase
{
	short t[4];
};

static constexpr int N_PHASES = 256;

struct VideoFilterDigest
{
	VideoFilterDigest() { memset(md5, 0, sizeof(md5)); }
	bool operator!=(const VideoFilterDigest& other) { return memcmp(md5, other.md5, sizeof(md5)) != 0; }
	bool operator==(const VideoFilterDigest& other) { return memcmp(md5, other.md5, sizeof(md5)) == 0; }

	unsigned char md5[16];
};

struct VideoFilter
{
	bool is_adaptive;
	FilterPhase phases[N_PHASES];
	FilterPhase adaptive_phases[N_PHASES];
	VideoFilterDigest digest;
};

static VideoFilter scaler_flt_data[3];

static bool scale_phases(FilterPhase out_phases[N_PHASES], FilterPhase *in_phases, int in_count)
{
	if (!in_count)
	{
		return false;
	}

	int dup = N_PHASES / in_count;

	if ((in_count * dup) != N_PHASES)
	{
		return false;
	}

	for (int i = 0; i < in_count; i++)
	{
		for (int j = 0; j < dup; j++)
		{
			out_phases[(i * dup) + j] = in_phases[i];
		}
	}

	return true;
}

static bool read_video_filter(int type, VideoFilter *out)
{
	PROFILE_FUNCTION();

	fileTextReader reader = {};
	FilterPhase phases[512];
	int count = 0;
	bool is_adaptive = false;
	int scale = 2;

	memset(out, 0, sizeof(VideoFilter));

	static char filename[1024];
	snprintf(filename, sizeof(filename), COEFF_DIR"/%s", scaler_flt[type].filename);

	if (FileOpenTextReader(&reader, filename))
	{
		const char *line;
		while ((line = FileReadLine(&reader)))
		{
			if (count == 0 && !strcasecmp(line, "adaptive"))
			{
				is_adaptive = true;
				continue;
			}

			if (count == 0 && !strcasecmp(line, "10bit"))
			{
				scale = 1;
				continue;
			}

			int phase[4];
			int n = sscanf(line, "%d,%d,%d,%d", &phase[0], &phase[1], &phase[2], &phase[3]);
			if (n == 4)
			{
				if (count >= (is_adaptive ? N_PHASES * 2 : N_PHASES)) return false; //too many
				phases[count].t[0] = phase[0] * scale;
				phases[count].t[1] = phase[1] * scale;
				phases[count].t[2] = phase[2] * scale;
				phases[count].t[3] = phase[3] * scale;
				count++;
			}
		}
	}

	printf( "Filter \'%s\', phases: %d adaptive: %s\n",
			scaler_flt[type].filename,
			is_adaptive ? count / 2 : count,
			is_adaptive ? "true" : "false" );

	bool valid = false;
	if (is_adaptive)
	{
		out->is_adaptive = true;
		valid = scale_phases(out->phases, phases, count / 2);
		valid = valid && scale_phases(out->adaptive_phases, phases + (count / 2), count / 2);
	}
	else if (count == 32 && !is_adaptive) // legacy
	{
		out->is_adaptive = false;
		valid = scale_phases(out->phases, phases, 16);
	}
	else if (!is_adaptive)
	{
		out->is_adaptive = false;
		valid = scale_phases(out->phases, phases, count);
	}

	if (!valid)
	{
		// Make a default NN filter in case of error
		out->is_adaptive = false;
		FilterPhase nn_phases[2] =
		{
			{ .t = { 0, 256, 0, 0 } },
			{ .t = { 0, 0, 256, 0 } }
		};
		scale_phases(out->phases, nn_phases, 2);
	}

	MD5Context ctx;
	MD5Init(&ctx);
	MD5Update(&ctx, (unsigned char *)&out->is_adaptive, sizeof(VideoFilter::is_adaptive));
	MD5Update(&ctx, (unsigned char *)out->phases, sizeof(VideoFilter::phases));
	MD5Update(&ctx, (unsigned char *)out->adaptive_phases, sizeof(VideoFilter::adaptive_phases));
	MD5Final(out->digest.md5, &ctx);

	return valid;
}

static void send_phases_legacy(int addr, const FilterPhase phases[N_PHASES])
{
	PROFILE_FUNCTION();

	for (int idx = 0; idx < N_PHASES; idx += 16)
	{
		const FilterPhase *p = &phases[idx];
		spi_w(((p->t[0] >> 1) & 0x1FF) | ((addr + 0) << 9));
		spi_w(((p->t[1] >> 1) & 0x1FF) | ((addr + 1) << 9));
		spi_w(((p->t[2] >> 1) & 0x1FF) | ((addr + 2) << 9));
		spi_w(((p->t[3] >> 1) & 0x1FF) | ((addr + 3) << 9));
		addr += 4;
	}
}

static void send_phases(int addr, const FilterPhase phases[N_PHASES], bool full_precision)
{
	PROFILE_FUNCTION();

	const int skip = full_precision ? 1 : 4;
	const int shift = full_precision ? 0 : 1;

	addr *= full_precision ? (N_PHASES * 4) : (64 * 4);

	for (int idx = 0; idx < N_PHASES; idx += skip)
	{
		const FilterPhase *p = &phases[idx];
		spi_w(addr + 0); spi_w((p->t[0] >> shift) & 0x3FF);
		spi_w(addr + 1); spi_w((p->t[1] >> shift) & 0x3FF);
		spi_w(addr + 2); spi_w((p->t[2] >> shift) & 0x3FF);
		spi_w(addr + 3); spi_w((p->t[3] >> shift) & 0x3FF);
		addr += 4;
	}
}

static VideoFilterDigest horiz_filter_digest, vert_filter_digest;

static void send_video_filters(const VideoFilter *horiz, const VideoFilter *vert, int ver)
{
	PROFILE_FUNCTION();

	spi_uio_cmd_cont(UIO_SET_FLTCOEF);

	const bool full_precision = (ver & 0x4) != 0;

	const bool send_horiz = horiz_filter_digest != horiz->digest;
	const bool send_vert = vert_filter_digest != vert->digest;

	switch( ver & 0x3 )
	{
		case 1:
			if (send_horiz) send_phases_legacy(0, horiz->phases);
			if (send_vert) send_phases_legacy(64, vert->phases);
			break;
		case 2:
			if (send_horiz) send_phases(0, horiz->phases, full_precision);
			if (send_vert) send_phases(1, vert->phases, full_precision);
			break;
		case 3:
			if (send_horiz) send_phases(0, horiz->phases, full_precision);
			if (send_vert) send_phases(1, vert->phases, full_precision);

			if (horiz->is_adaptive && send_horiz)
			{
				send_phases(2, horiz->adaptive_phases, full_precision);
			}
			else if (vert->is_adaptive && send_vert)
			{
				send_phases(3, vert->adaptive_phases, full_precision);
			}
			break;
		default:
			break;
	}

	horiz_filter_digest = horiz->digest;
	vert_filter_digest = vert->digest;

	DisableIO();
}

static void set_vfilter(int force)
{
	PROFILE_FUNCTION();

	static int last_flags = 0;

	int flt_flags = spi_uio_cmd_cont(UIO_SET_FLTNUM);
	if (!flt_flags || (!force && last_flags == flt_flags))
	{
		DisableIO();
		return;
	}

	last_flags = flt_flags;
	printf("video_set_filter: flt_flags=%d\n", flt_flags);

	spi8(scaler_flt[0].mode);
	DisableIO();

	int vert_flt;
	if (current_video_info.interlaced) vert_flt = VFILTER_HORZ;
	else if ((flt_flags & 0x30) && scaler_flt[VFILTER_SCAN].mode) vert_flt = VFILTER_SCAN;
	else if (scaler_flt[VFILTER_VERT].mode) vert_flt = VFILTER_VERT;
	else vert_flt = VFILTER_HORZ;

	send_video_filters(&scaler_flt_data[VFILTER_HORZ], &scaler_flt_data[vert_flt], flt_flags & 0xF);
}

static void setScaler()
{
	PROFILE_FUNCTION();

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
	return scaler_flt[type].mode;
}

char* video_get_scaler_coeff(int type, int only_name)
{
	char *path = scaler_flt[type].filename;
	if (only_name)
	{
		char *p = strrchr(path, '/');
		if (p) return p + 1;
	}
	return path;
}

static char scaler_cfg[128] = { 0 };

void video_set_scaler_flt(int type, int n)
{
	scaler_flt[type].mode = (char)n;
	FileSaveConfig(scaler_cfg, &scaler_flt, sizeof(scaler_flt));
	spi_uio_cmd8(UIO_SET_FLTNUM, scaler_flt[0].mode);
	set_vfilter(1);
}

void video_set_scaler_coeff(int type, const char *name)
{
	strcpy(scaler_flt[type].filename, name);
	FileSaveConfig(scaler_cfg, &scaler_flt, sizeof(scaler_flt));
	read_video_filter(type, &scaler_flt_data[type]);
	setScaler();
	user_io_send_buttons(1);
}

static void loadScalerCfg()
{
	PROFILE_FUNCTION();

	sprintf(scaler_cfg, "%s_scaler.cfg", user_io_get_core_name());
	memset(scaler_flt, 0, sizeof(scaler_cfg));
	if (!FileLoadConfig(scaler_cfg, &scaler_flt, sizeof(scaler_flt)) || scaler_flt[0].mode > 1)
	{
		memset(scaler_flt, 0, sizeof(scaler_flt));
	}

	if (!scaler_flt[VFILTER_HORZ].filename[0] && cfg.vfilter_default[0])
	{
		strcpy(scaler_flt[VFILTER_HORZ].filename, cfg.vfilter_default);
		scaler_flt[VFILTER_HORZ].mode = 1;
	}

	if (!scaler_flt[VFILTER_VERT].filename[0] && cfg.vfilter_vertical_default[0])
	{
		strcpy(scaler_flt[VFILTER_VERT].filename, cfg.vfilter_vertical_default);
		scaler_flt[VFILTER_VERT].mode = 1;
	}

	if (!scaler_flt[VFILTER_SCAN].filename[0] && cfg.vfilter_scanlines_default[0])
	{
		strcpy(scaler_flt[VFILTER_SCAN].filename, cfg.vfilter_scanlines_default);
		scaler_flt[VFILTER_SCAN].mode = 1;
	}

	if (!read_video_filter(VFILTER_HORZ, &scaler_flt_data[VFILTER_HORZ])) memset(&scaler_flt[VFILTER_HORZ], 0, sizeof(scaler_flt[VFILTER_HORZ]));
	if (!read_video_filter(VFILTER_VERT, &scaler_flt_data[VFILTER_VERT])) memset(&scaler_flt[VFILTER_VERT], 0, sizeof(scaler_flt[VFILTER_VERT]));
	if (!read_video_filter(VFILTER_SCAN, &scaler_flt_data[VFILTER_SCAN])) memset(&scaler_flt[VFILTER_SCAN], 0, sizeof(scaler_flt[VFILTER_SCAN]));
}

static char active_gamma_cfg[1024] = { 0 };
static char gamma_cfg[1024] = { 0 };
static char has_gamma = 0; // set in video_init

static void setGamma()
{
	PROFILE_FUNCTION();

	if (!memcmp(active_gamma_cfg, gamma_cfg, sizeof(gamma_cfg))) return;

	fileTextReader reader = {};
	static char filename[1024];

	if (!has_gamma) return;

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
	memcpy(active_gamma_cfg, gamma_cfg, sizeof(gamma_cfg));
}

int video_get_gamma_en()
{
	return has_gamma ? gamma_cfg[0] : -1;
}

char* video_get_gamma_curve(int only_name)
{
	char *path = gamma_cfg + 1;
	if (only_name)
	{
		char *p = strrchr(path, '/');
		if (p) return p + 1;
	}
	return path;
}

static char gamma_cfg_path[1024] = { 0 };

void video_set_gamma_en(int n)
{
	gamma_cfg[0] = (char)n;
	FileSaveConfig(gamma_cfg_path, &gamma_cfg, sizeof(gamma_cfg));
	setGamma();
}

void video_set_gamma_curve(const char *name)
{
	strcpy(gamma_cfg + 1, name);
	FileSaveConfig(gamma_cfg_path, &gamma_cfg, sizeof(gamma_cfg));
	setGamma();
	user_io_send_buttons(1);
}

static void loadGammaCfg()
{
	PROFILE_FUNCTION();
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
	PROFILE_FUNCTION();

	static char filename[1024];
	has_shadow_mask = 0;

	if (!spi_uio_cmd_cont(UIO_SHADOWMASK))
	{
		DisableIO();
		return;
	}

	has_shadow_mask = 1;
	switch (video_get_shadow_mask_mode())
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

char* video_get_shadow_mask(int only_name)
{
	char *path = shadow_mask_cfg + 1;
	if (only_name)
	{
		char *p = strrchr(path, '/');
		if (p) return p + 1;
	}
	return path;
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

void video_set_shadow_mask(const char *name)
{
	strcpy(shadow_mask_cfg + 1, name);
	FileSaveConfig(shadow_mask_cfg_path, &shadow_mask_cfg, sizeof(shadow_mask_cfg));
	setShadowMask();
	user_io_send_buttons(1);
}

static void loadShadowMaskCfg()
{
	PROFILE_FUNCTION();

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

static void hdmi_config_set_spd(bool val)
{
	int fd = i2c_open(0x39, 0);
	if (fd >= 0)
	{
		uint8_t packet_val = i2c_smbus_read_byte_data(fd, 0x40);
		if (val)
			packet_val |= 0x40;
		else
			packet_val &= ~0x40;
		int res = i2c_smbus_write_byte_data(fd, 0x40, packet_val);
		if (res < 0) printf("i2c: write error (%02X %02X): %d\n", 0x40, packet_val, res);
		i2c_close(fd);
	}
}

static void hdmi_config_set_spare(bool val)
{
	int fd = i2c_open(0x39, 0);
	if (fd >= 0)
	{
		uint8_t packet_val = i2c_smbus_read_byte_data(fd, 0x40);
		if (val)
			packet_val |= 0x01;
		else
			packet_val &= ~0x01;
		int res = i2c_smbus_write_byte_data(fd, 0x40, packet_val);
		if (res < 0) printf("i2c: write error (%02X %02X): %d\n", 0x40, packet_val, res);
		i2c_close(fd);
	}
}

static void hdmi_config_init()
{
	int ypbpr = cfg.ypbpr && cfg.direct_video;

	// address, value
	uint8_t init_data[] = {
		0x98, 03,				// ADI required Write.

		0xD6, 0b11000000,		// [7:6] HPD Control...
								// 00 = HPD is from both HPD pin or CDC HPD
								// 01 = HPD is from CDC HPD
								// 10 = HPD is from HPD pin
								// 11 = HPD is always high

		0x41, 0x10,				// Power Down control
		0x9A, 0x70,				// ADI required Write.
		0x9C, 0x30,				// ADI required Write.
		0x9D, 0b01100001,		// [7:4] must be b0110!.
								// [3:2] b00 = Input clock not divided. b01 = Clk divided by 2. b10 = Clk divided by 4. b11 = invalid!
								// [1:0] must be b01!
		0xA2, 0xA4,				// ADI required Write.
		0xA3, 0xA4,				// ADI required Write.
		0xE0, 0xD0,				// ADI required Write.


		0x35, 0x40,
		0x36, 0xD9,
		0x37, 0x0A,
		0x38, 0x00,
		0x39, 0x2D,
		0x3A, 0x00,

		0x16, 0b00111000,		// Output Format 444 [7]=0.
								// [6] must be 0!
								// Colour Depth for Input Video data [5:4] b11 = 8-bit.
								// Input Style [3:2] b10 = Style 1 (ignored when using 444 input).
								// DDR Input Edge falling [1]=0 (not using DDR atm).
								// Output Colour Space RGB [0]=0.

		0x17, 0b01100010,		// Aspect ratio 16:9 [1]=1, 4:3 [1]=0, invert sync polarity

		0x18, (uint8_t)(ypbpr ? 0x86 : (cfg.hdmi_limited & 1) ? 0x8D : (cfg.hdmi_limited & 2) ? 0x8E : 0x00),  // CSC Scaling Factors and Coefficients for RGB Full->Limited.
		0x19, (uint8_t)(ypbpr ? 0xDF : (cfg.hdmi_limited & 1) ? 0xBC : 0xFE),                       // Taken from table in ADV7513 Programming Guide.
		0x1A, (uint8_t)(ypbpr ? 0x1A : 0x00),         // CSC Channel A.
		0x1B, (uint8_t)(ypbpr ? 0x3F : 0x00),
		0x1C, (uint8_t)(ypbpr ? 0x1E : 0x00),
		0x1D, (uint8_t)(ypbpr ? 0xE2 : 0x00),
		0x1E, (uint8_t)(ypbpr ? 0x07 : 0x01),
		0x1F, (uint8_t)(ypbpr ? 0xE7 : 0x00),

		0x20, (uint8_t)(ypbpr ? 0x04 : 0x00),         // CSC Channel B.
		0x21, (uint8_t)(ypbpr ? 0x1C : 0x00),
		0x22, (uint8_t)(ypbpr ? 0x08 : (cfg.hdmi_limited & 1) ? 0x0D : 0x0E),
		0x23, (uint8_t)(ypbpr ? 0x11 : (cfg.hdmi_limited & 1) ? 0xBC : 0xFE),
		0x24, (uint8_t)(ypbpr ? 0x01 : 0x00),
		0x25, (uint8_t)(ypbpr ? 0x91 : 0x00),
		0x26, (uint8_t)(ypbpr ? 0x01 : 0x01),
		0x27, 0x00,

		0x28, (uint8_t)(ypbpr ? 0x1D : 0x00),         // CSC Channel C.
		0x29, (uint8_t)(ypbpr ? 0xAE : 0x00),
		0x2A, (uint8_t)(ypbpr ? 0x1B : 0x00),
		0x2B, (uint8_t)(ypbpr ? 0x73 : 0x00),
		0x2C, (uint8_t)(ypbpr ? 0x06 : (cfg.hdmi_limited & 1) ? 0x0D : 0x0E),
		0x2D, (uint8_t)(ypbpr ? 0xDF : (cfg.hdmi_limited & 1) ? 0xBC : 0xFE),
		0x2E, (uint8_t)(ypbpr ? 0x07 : 0x01),
		0x2F, (uint8_t)(ypbpr ? 0xE7 : 0x00),

		0x3B, 0x0,              // Automatic pixel repetition and VIC detection


		0x48, 0b00001000,       // [6]=0 Normal bus order!
								// [5] DDR Alignment.
								// [4:3] b01 Data right justified (for YCbCr 422 input modes).

		0x49, 0xA8,				// ADI required Write.
		0x4A, 0b10000000, //Auto-Calculate SPD checksum
		0x4C, 0x00,				// ADI required Write.

		0x55, (uint8_t)(cfg.hdmi_game_mode ? 0b00010010 : 0b00010000),
								// [7] must be 0!. Set RGB444 in AVinfo Frame [6:5], Set active format [4].
								// AVI InfoFrame Valid [4].
								// Bar Info [3:2] b00 Bars invalid. b01 Bars vertical. b10 Bars horizontal. b11 Bars both.
								// Scan Info [1:0] b00 (No data). b01 TV. b10 PC. b11 None.

		0x56, 0b00001000,		// [5:4] Picture Aspect Ratio
								// [3:0] Active Portion Aspect Ratio b1000 = Same as Picture Aspect Ratio

		0x57, (uint8_t)((cfg.hdmi_game_mode ? 0x80 : 0x00)		// [7] IT Content. 0 - No. 1 - Yes (type set in register 0x59).
																// [6:4] Color space (ignored for RGB)
			| ((ypbpr || cfg.hdmi_limited) ? 0b0100 : 0b1000)),	// [3:2] RGB Quantization range
																// [1:0] Non-Uniform Scaled: 00 - None. 01 - Horiz. 10 - Vert. 11 - Both.

		0x59, (uint8_t)(cfg.hdmi_game_mode ? 0x30 : 0x00),		// [7:6] [YQ1 YQ0] YCC Quantization Range: b00 = Limited Range, b01 = Full Range
																// [5:4] IT Content Type b11 = Game, b00 = Graphics/None
																// [3:0] Pixel Repetition Fields b0000 = No Repetition

		0x73, 0x01,

		0x94, 0b10000000,       // [7]=1 HPD Interrupt ENabled.

		0x99, 0x02,				// ADI required Write.
		0x9B, 0x18,				// ADI required Write.

		0x9F, 0x00,				// ADI required Write.

		0xA1, 0b00000000,	    // [6]=1 Monitor Sense Power Down DISabled.

		0xA4, 0x08,				// ADI required Write.
		0xA5, 0x04,				// ADI required Write.
		0xA6, 0x00,				// ADI required Write.
		0xA7, 0x00,				// ADI required Write.
		0xA8, 0x00,				// ADI required Write.
		0xA9, 0x00,				// ADI required Write.
		0xAA, 0x00,				// ADI required Write.
		0xAB, 0x40,				// ADI required Write.

		0xAF, (uint8_t)(0b00000100	// [7]=0 HDCP Disabled.
								// [6:5] must be b00!
								// [4]=0 Current frame is unencrypted
								// [3:2] must be b01!
			| ((cfg.dvi_mode == 1) ? 0b00 : 0b10)),	 //	[1]=1 HDMI Mode.
								// [0] must be b0!

		0xB9, 0x00,				// ADI required Write.

		0xBA, 0b01100000,		// [7:5] Input Clock delay...
								// b000 = -1.2ns.
								// b001 = -0.8ns.
								// b010 = -0.4ns.
								// b011 = No delay.
								// b100 = 0.4ns.
								// b101 = 0.8ns.
								// b110 = 1.2ns.
								// b111 = 1.6ns.

		0xBB, 0x00,				// ADI required Write.
		0xDE, 0x9C,				// ADI required Write.
		0xE4, 0x60,				// ADI required Write.
		0xFA, 0x7D,				// Nbr of times to search for good phase

		// (Audio stuff on Programming Guide, Page 66)...
		0x0A, 0b00000000,		// [6:4] Audio Select. b000 = I2S.
								// [3:2] Audio Mode. (HBR stuff, leave at 00!).

		0x0B, 0b00001110,		//

		0x0C, 0b00000100,		// [7] 0 = Use sampling rate from I2S stream.   1 = Use samp rate from I2C Register.
								// [6] 0 = Use Channel Status bits from stream. 1 = Use Channel Status bits from I2C register.
								// [2] 1 = I2S0 Enable.
								// [1:0] I2S Format: 00 = Standard. 01 = Right Justified. 10 = Left Justified. 11 = AES.

		0x0D, 0b00010000,		// [4:0] I2S Bit (Word) Width for Right-Justified.
		0x14, 0b00000010,		// [3:0] Audio Word Length. b0010 = 16 bits.
		0x15, (uint8_t)((cfg.hdmi_audio_96k ? 0x80 : 0x00) | 0b0100000),	// I2S Sampling Rate [7:4]. b0000 = (44.1KHz). b0010 = 48KHz.
								// Input ID [3:1] b000 (0) = 24-bit RGB 444 or YCrCb 444 with Separate Syncs.

		// Audio Clock Config
		0x01, 0x00,				//
		0x02, (uint8_t)(cfg.hdmi_audio_96k ? 0x30 : 0x18),	// Set N Value 12288/6144
		0x03, 0x00,				//

		0x07, 0x01,				//
		0x08, 0x22,				// Set CTS Value 74250
		0x09, 0x0A,				//
	};

	int fd = i2c_open(0x39, 0);
	if (fd >= 0)
	{
		for (uint i = 0; i < sizeof(init_data); i += 2)
		{
			int res = i2c_smbus_write_byte_data(fd, init_data[i], init_data[i + 1]);
			if (res < 0) printf("i2c: write error (%02X %02X): %d\n", init_data[i], init_data[i + 1], res);
		}

		i2c_close(fd);
	}
	else
	{
		printf("*** ADV7513 not found on i2c bus! HDMI won't be available!\n");
	}
}

static uint8_t last_sync_invert = 0xff;
static uint8_t last_pr_flags = 0xff;
static uint8_t last_vic_mode = 0xff;

static void hdmi_config_set_mode(vmode_custom_t *vm)
{
	PROFILE_FUNCTION();

	const uint8_t vic_mode = (uint8_t)vm->param.vic;
	uint8_t pr_flags;

	if (cfg.direct_video && is_menu()) pr_flags = 0; // automatic pixel repetition
	else if (vm->param.pr != 0) pr_flags = 0b01001000; // manual pixel repetition with 2x clock
	else pr_flags = 0b01000000; // manual pixel repetition

	uint8_t sync_invert = 0;
	if (vm->param.hpol == 0) sync_invert |= 1 << 5;
	if (vm->param.vpol == 0) sync_invert |= 1 << 6;

	if (last_sync_invert == sync_invert && last_pr_flags == pr_flags && last_vic_mode == vic_mode) return;

	// address, value
	uint8_t init_data[] = {
		0x17, (uint8_t)(0b00000010 | sync_invert),		// Aspect ratio 16:9 [1]=1, 4:3 [1]=0
		0x3B, pr_flags,
		0x3C, vic_mode,			// VIC
	};

	int fd = i2c_open(0x39, 0);
	if (fd >= 0)
	{
		for (uint i = 0; i < sizeof(init_data); i += 2)
		{
			int res = i2c_smbus_write_byte_data(fd, init_data[i], init_data[i + 1]);
			if (res < 0) printf("i2c: write error (%02X %02X): %d\n", init_data[i], init_data[i + 1], res);
		}

		i2c_close(fd);
	}
	else
	{
		printf("*** ADV7513 not found on i2c bus! HDMI won't be available!\n");
	}

	last_pr_flags = pr_flags;
	last_sync_invert = sync_invert;
	last_vic_mode = vic_mode;
}

static void edid_parse_cea_ext(uint8_t *cea)
{
	uint8_t *data_block_end = cea + cea[2];
	uint8_t *cur_blk_start = cea + 4;
	uint8_t *cur_blk_data = cur_blk_start;
	while (cur_blk_start != data_block_end)
	{
		cur_blk_data = cur_blk_start;
		uint8_t blk_tag = (*cur_blk_data & 0xe0) >> 5;
		uint8_t blk_size = *cur_blk_data & 0x1f;
		uint8_t blk_data_size = blk_size; //size of actual data in the block, it might be adjusted if the first byte is extended tag
		cur_blk_data++;
		//vendor specific block might be the only one?

		uint8_t is_vendor_specific = 0;
		if (blk_tag == 0x03) is_vendor_specific = 1;
		if (blk_tag == 0x07)
		{
			if (*cur_blk_data == 0x01) is_vendor_specific = 1;
			cur_blk_data++; //The extended tag uses the next byte for the type. We may not need it?
			blk_data_size--;
		}

		if (is_vendor_specific && blk_data_size >= 3)
		{
			int oui = cur_blk_data[0] | cur_blk_data[1] << 8 | cur_blk_data[2] << 16;
			cur_blk_data += 3;
			blk_data_size -= 3;
			if (oui == 0x00001a) //AMD block
			{
				uint8_t min_fr = cur_blk_data[2];

				uint8_t max_fr = cur_blk_data[3];
				if (max_fr > 62) max_fr = 62;
				if (min_fr && max_fr)
				{
					vrr_modes[VRR_FREESYNC].available = 1;
					vrr_modes[VRR_FREESYNC].min_fr = min_fr;
					vrr_modes[VRR_FREESYNC].max_fr = max_fr;
				}
			}
			else if (oui == 0xc45dd8)
			{
				if (blk_data_size > 5) //VRR lies beyond here
				{
					uint8_t min_fr = cur_blk_data[5] & 0x3f;
					uint8_t max_fr = (cur_blk_data[5] & 0xc0) << 2 | cur_blk_data[6];
					if (max_fr > 62) max_fr = 62;
					if (min_fr && max_fr)
					{
						vrr_modes[VRR_VESA].available = 1;
						vrr_modes[VRR_VESA].min_fr = min_fr;
						vrr_modes[VRR_VESA].max_fr = max_fr;
					}
				}
			}
		}
		cur_blk_start += blk_size + 1;
	}
}

static int find_edid_vrr_capability()
{
	uint8_t *cur_ext = NULL;
	uint8_t ext_cnt = edid[126];

	//Probably only one extension, but just in case...
	for (int i = 0; i < ext_cnt; i++)
	{
		cur_ext = edid + 128 + i * 128; //edid extension blocks are 128 bytes
		uint8_t ext_tag = *cur_ext;
		if (ext_tag == 0x02) //CEA EDID extension
		{
			edid_parse_cea_ext(cur_ext);
		}
	}

	for (size_t i = 1; i < sizeof(vrr_modes) / sizeof(vrr_cap_t); i++)
	{
		if (vrr_modes[i].available) printf("VRR: %s available\n", vrr_modes[i].description);
	}
	return 0;

}

static int is_edid_valid()
{
	static const uint8_t magic[] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
	if (sizeof(edid) < sizeof(magic)) return 0;
	return !memcmp(edid, magic, sizeof(magic));
}

static int get_active_edid()
{
	int fd = i2c_open(0x39, 0);
	if (fd < 0)
	{
		printf("EDID: cannot find main i2c device\n");
		return 0;
	}

	//Test if adv7513 senses hdmi clock. If not, don't bother with the edid query
	int hpd_state = i2c_smbus_read_byte_data(fd, 0x42);
	if (hpd_state < 0 || !(hpd_state & 0x20))
	{
		i2c_close(fd);
		return 0;
	}


	for (int i = 0; i < 10; i++)
	{
		i2c_smbus_write_byte_data(fd, 0xC9, 0x03);
		i2c_smbus_write_byte_data(fd, 0xC9, 0x13);
	}
	i2c_close(fd);
	fd = i2c_open(0x3f, 0);
	if (fd < 0)
	{
		printf("EDID: cannot find i2c device.\n");
		return 0;
	}

	// waiting for valid EDID
	for (int k = 0; k < 20; k++)
	{
		for (uint i = 0; i < sizeof(edid); i++) edid[i] = (uint8_t)i2c_smbus_read_byte_data(fd, i);
		if (is_edid_valid()) break;
		usleep(100000);
	}

	i2c_close(fd);
	printf("EDID:\n"); hexdump(edid, sizeof(edid), 0);

	if (!is_edid_valid())
	{
		printf("Invalid EDID: incorrect header.\n");
		bzero(edid, sizeof(edid));
		return 0;
	}
	return 1;
}

static int get_edid_vmode(vmode_custom_t *v)
{
	if (!is_edid_valid())
	{
		get_active_edid();
	}

	if (!is_edid_valid()) return 0;

	int hact, vact, pixclk_khz, hfp, hsync, hbp, vfp, vsync, vbp, hbl, vbl;
	uint8_t *x = edid + 0x36;

	pixclk_khz = (x[0] + (x[1] << 8)) * 10;
	if (pixclk_khz < 10000)
	{
		if (!pixclk_khz) printf("Invalid EDID: First two bytes are 0, invalid data.\n");
		else printf("Invalid EDID: Pixelclock < 10 MHz, assuming invalid data 0x%02x 0x%02x.\n", x[0], x[1]);
		return 0;
	}

	if (cfg.dvi_mode == 2)
	{
		cfg.dvi_mode = (edid[0x80] == 2 && edid[0x81] == 3 && (edid[0x83] & 0x40)) ? 0 : 1;
		if (cfg.dvi_mode == 1) printf("EDID: using DVI mode.\n");
	}

	unsigned char flags = x[17];
	if (flags & 0x80)
	{
		printf("EDID: preferred mode is interlaced. Fall back to default video mode.\n");
		return 0;
	}

	hact = (x[2] + ((x[4] & 0xf0) << 4));
	hbl = (x[3] + ((x[4] & 0x0f) << 8));
	hfp = (x[8] + ((x[11] & 0xc0) << 2));
	hsync = (x[9] + ((x[11] & 0x30) << 4));
	hbp = hbl - hsync - hfp;
	vact = (x[5] + ((x[7] & 0xf0) << 4));
	vbl = (x[6] + ((x[7] & 0x0f) << 8));
	vfp = ((x[10] >> 4) + ((x[11] & 0x0c) << 2));
	vsync = ((x[10] & 0x0f) + ((x[11] & 0x03) << 4));
	vbp = vbl - vsync - vfp;

	/*
	int pos_pol_hsync = 0;
	int pos_pol_vsync = 0;
	int no_pol_vsync = 0; // digital composite signals have no vsync polarity

	switch ((flags & 0x18) >> 3)
	{
	case 0x02:
		if (flags & (1 << 1)) pos_pol_hsync = 1;
		no_pol_vsync = 1;
		break;
	case 0x03:
		if (flags & (1 << 1)) pos_pol_hsync = 1;
		if (flags & (1 << 2)) pos_pol_vsync = 1;
		break;
	}
	*/

	double Fpix = pixclk_khz / 1000.f;
	double frame_rate = Fpix * 1000000.f / ((hact + hfp + hbp + hsync)*(vact + vfp + vbp + vsync));
	printf("EDID: preferred mode: %dx%d@%.1f, pixel clock: %.3fMHz\n", hact, vact, frame_rate, Fpix);

	if (hact >= 1920) support_FHD = 1;

	if (hact > 2048)
	{
		printf("EDID: Preferred resolution is too high (%dx%d).\n", hact, vact);
		printf("EDID: Falling back to default video mode.\n");
		return 0;
	}

	memset(v, 0, sizeof(vmode_custom_t));
	v->item[1] = hact;
	v->item[2] = hfp;
	v->item[3] = hsync;
	v->item[4] = hbp;
	v->item[5] = vact;
	v->item[6] = vfp;
	v->item[7] = vsync;
	v->item[8] = vbp;
	v->Fpix = Fpix;

	if (Fpix > 210.f)
	{
		printf("EDID: Preferred mode has too high pixel clock (%.3fMHz).\n", Fpix);
		if (hact == 2048 && vact == 1536)
		{
			int n = 13;
			printf("EDID: Using safe vmode %d.\n", n);
			for (int i = 0; i < 8; i++) v->item[i + 1] = vmodes[n].vpar[i];
			v->param.vic = vmodes[n].vic_mode;
			v->Fpix = vmodes[n].Fpix;
		}
		else
		{
			int fail = 1;
			if (frame_rate > 60.f)
			{
				Fpix = 60.f * (hact + hfp + hbp + hsync)*(vact + vfp + vbp + vsync) / 1000000.f;
				if (Fpix <= 210.f)
				{
					printf("EDID: Reducing frame rate to 60Hz with new pixel clock %.3fMHz.\n", Fpix);
					v->Fpix = Fpix;
					fail = 0;
				}
			}

			if (fail)
			{
				printf("EDID: Falling back to default video mode.\n");
				return 0;
			}
		}
	}

	v->param.rb = 2;
	setPLL(v->Fpix, v);
	return 1;
}

static void set_vrr_mode()
{
	PROFILE_FUNCTION();

	use_vrr = 0;
	float vrateh = 100000000;

	if (cfg.vrr_mode == 0)
	{
		if (last_vrr_mode != 0)
		{
			hdmi_config_set_spd(0);
			hdmi_config_set_spare(0);
		}
		last_vrr_mode = 0;
		return;
	}

	if (current_video_info.vtimeh) vrateh /= current_video_info.vtimeh; else vrateh = 0;
	if (cfg.vrr_vesa_framerate) vrateh = cfg.vrr_vesa_framerate;

	if ((last_vrr_mode == cfg.vrr_mode) &&
		(last_vrr_rate == vrateh) &&
		(last_vrr_vfp == v_cur.param.vfp || cfg.vrr_mode != VRR_VESA)) return;

	if (!is_edid_valid())
	{
		get_active_edid();
	}

	if (!is_edid_valid())
	{
		printf("Set VRR: No valid edid, cannot set\n");
		return;
	}

	find_edid_vrr_capability();

	if (cfg.vrr_mode == 1) //autodetect
	{
		for (uint8_t i = 1; i < sizeof(vrr_modes) / sizeof(vrr_cap_t); i++)
		{
			if (vrr_modes[i].available)
			{
				use_vrr = i;
				break;
			}
		}
	}
	else if (cfg.vrr_mode == 2)
	{ //force AMD Freesync
		use_vrr = VRR_FREESYNC;
	}
	else if (cfg.vrr_mode == 3)
	{ //force Vesa Forum VRR
		use_vrr = VRR_VESA;
	}
	else
	{
		use_vrr = 0;
	}

	vrr_min_fr = 0;
	vrr_max_fr = 0;

	if (use_vrr == VRR_VESA && !vrateh) return;
	if (use_vrr)
	{
		vrr_min_fr = cfg.vrr_min_framerate;
		vrr_max_fr = cfg.vrr_max_framerate;

		if (!vrr_min_fr) vrr_min_fr = vrr_modes[use_vrr].min_fr;
		if (!vrr_max_fr) vrr_max_fr = vrr_modes[use_vrr].max_fr;

		if (!vrr_min_fr) vrr_min_fr = 47;
		if (!vrr_max_fr) vrr_max_fr = 62;

		vrr_modes[use_vrr].active = 1;
		printf("VRR: Set %s active\n", vrr_modes[use_vrr].description);
		if (use_vrr == VRR_VESA)
		{
			printf("VESA Frame Rate %d Front Porch %d\n", (int)vrateh, v_cur.param.vfp);
		}
	}

	int16_t vrateh_i = (int16_t)vrateh;

	//These are only sent in the case that freesync or vesa vrr is enabled
	uint8_t freesync_data[] = {
		//header
		0x00, 0x83,
		0x01, 0x01,
		0x02, 0x08,
		//data
		0x04, 0x1A,
		0x05, 0x00,
		0x06, 0x00,
		//0x07
		//0x08
		0x09, 0x07,
		0x0A, vrr_min_fr,
		0x0B, vrr_max_fr,
	};

	uint8_t vesa_data[] = {
		0xC0, 0x7F,
		0xC1, 0xC0,
		0xC2, 0x00,

		0xC3, 0x40,
		0xC5, 0x01,
		0xC6, 0x00,
		0xC7, 0x01,
		0xC8, 0x00,
		0xC9, 0x04,

		0xCA, 0x01,
		0xCB, (uint8_t)v_cur.param.vfp,
		0xCC, (uint8_t)((vrateh_i >> 8) & 0x03),
		0xCD, (uint8_t)(vrateh_i & 0xFF),
	};

	int res = 0;
	int fd = i2c_open(0x38, 0);
	if (fd >= 0)
	{
		if (use_vrr == VRR_FREESYNC)
		{
			hdmi_config_set_spd(1);
			res = i2c_smbus_write_byte_data(fd, 0x1F, 0b10000000);
			if (res < 0)
			{
				printf("i2c: Vrr: Couldn't update SPD change register (0x1F, 0x80) %d\n", res);
			}
			for (uint i = 0; i < sizeof(freesync_data); i += 2)
			{
				res = i2c_smbus_write_byte_data(fd, freesync_data[i], freesync_data[i + 1]);
				if (res < 0) printf("i2c: Vrr register write error (%02X %02x): %d\n", freesync_data[i], freesync_data[i + 1], res);
			}
			res = i2c_smbus_write_byte_data(fd, 0x1F, 0x00);
			if (res < 0) printf("i2c: Vrr: Couldn't update SPD change register (0x1F, 0x00), %d\n", res);
		}
		else
		{
			hdmi_config_set_spd(0);
		}

		if (use_vrr == VRR_VESA)
		{
			hdmi_config_set_spare(1);
			res = i2c_smbus_write_byte_data(fd, 0xDF, 0b10000000);
			if (res < 0)
			{
				printf("i2c: Vrr: Couldn't update Spare Packet change register (0xDF, 0x80) %d\n", res);
			}

			for (uint i = 0; i < sizeof(vesa_data); i += 2)
			{
				res = i2c_smbus_write_byte_data(fd, vesa_data[i], vesa_data[i + 1]);
				if (res < 0) printf("i2c: Vrr register write error (%02X %02x): %d\n", vesa_data[i], vesa_data[i + 1], res);
			}
			res = i2c_smbus_write_byte_data(fd, 0xDF, 0x00);
			if (res < 0) printf("i2c: Vrr: Couldn't update Spare Packet change register (0xDF, 0x00), %d\n", res);
		}
		else
		{
			hdmi_config_set_spare(0);
		}
		i2c_close(fd);
	}
	last_vrr_mode = cfg.vrr_mode;
	last_vrr_rate = vrateh;
	last_vrr_vfp = v_cur.param.vfp;

	if (!supports_vrr() || cfg.vsync_adjust) use_vrr = 0;
}

static void video_set_mode(vmode_custom_t *v, double Fpix)
{
	PROFILE_FUNCTION();

	setGamma();
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
	else
	{
		set_vrr_mode();
	}

	if (Fpix) setPLL(Fpix, &v_cur);
	if (use_vrr)
	{
		printf("Requested variable refresh rate: min=%dHz, max=%dHz\n", vrr_min_fr, vrr_max_fr);
		int horz = v_fix.param.hact + v_fix.param.hbp + v_fix.param.hfp + v_fix.param.hs;

#if 0
		// variant 1: try to reduce vblank to reach max refresh rate but keep original pixel clock.
		// try to adjust VBlank to match max refresh
		int vbl_fmax = ((v_cur.Fpix * 1000000.f) / (vrr_max_fr * horz)) - v_fix.param.vact - v_fix.param.vs - 1;
		if (vbl_fmax < 2) vbl_fmax = 2;
		int vfp = vbl_fmax - v_fix.param.vbp;
		v_fix.param.vfp = vfp;
		if (vfp < 1)
		{
			v_fix.param.vfp = 1;
			v_fix.param.vbp = vbl_fmax - 1;
		}
		int vert = v_fix.param.vact + v_fix.param.vbp + v_fix.param.vfp + v_fix.param.vs;
#else
		// variant 2: keep original vblank and adjust pixel clock to max refresh rate
		int vert = v_fix.param.vact + v_fix.param.vbp + v_fix.param.vfp + v_fix.param.vs;
		Fpix = horz * vert * vrr_max_fr;
		Fpix /= 1000000.f;
		setPLL(Fpix, &v_cur);
#endif

		double freq_max = (v_cur.Fpix * 1000000.f) / (horz * vert);
		double freq_min = vrr_min_fr;
		int vfp_vrr = 0;
		if (freq_min && freq_min < freq_max)
		{
			vfp_vrr = ((v_cur.Fpix * 1000000.f) / (vrr_min_fr * horz)) - vert + 1;
			v_fix.param.vfp += vfp_vrr;
			if (v_fix.param.vfp > 4095) v_fix.param.vfp = 4095;
		}

		vert = v_fix.param.vact + v_fix.param.vbp + v_fix.param.vfp + v_fix.param.vs;
		freq_min = (v_cur.Fpix * 1000000.f) / (horz * vert);
		printf("Using variable refresh rate: min=%2.1fHz, max=%2.1fHz. Additional VFP lines: %d\n", freq_min, freq_max, vfp_vrr);
	}

	printf("Send HDMI parameters:\n");
	spi_uio_cmd_cont(UIO_SET_VIDEO);
	printf("video: ");
	for (int i = 1; i <= 8; i++)
	{
		if (i == 1) spi_w((v_cur.param.pr << 15) | ((use_vrr ? 1 : 0) << 14) | v_fix.item[i]);
		//hsync polarity
		else if (i == 3) spi_w((!!v_cur.param.hpol << 15) | v_fix.item[i]);
		//vsync polarity
		else if (i == 7) spi_w((!!v_cur.param.vpol << 15) | v_fix.item[i]);
		else spi_w(v_fix.item[i]);
		printf("%d(%d), ", v_cur.item[i], v_fix.item[i]);
	}

	printf("%chsync, %cvsync\n", !!v_cur.param.hpol ? '+' : '-', !!v_cur.param.vpol ? '+' : '-');

	printf("PLL: ");
	for (int i = 9; i < 21; i++)
	{
		printf("0x%X, ", v_cur.item[i]);
		if (i & 1) spi_w(v_cur.item[i] | ((i == 9 && Fpix && cfg.vsync_adjust == 2 && !is_menu()) ? 0x8000 : 0) | 0x4000);
		else
		{
			spi_w(v_cur.item[i]);
			spi_w(v_cur.item[i] >> 16);
		}
	}

	printf("Fpix=%f\n", v_cur.Fpix);
	DisableIO();

	hdmi_config_set_mode(&v_cur);

	video_fb_config();

	setShadowMask();
}

static int parse_custom_video_mode(char* vcfg, vmode_custom_t *v)
{
	char *tokens[32];
	uint32_t val[32];
	double valf = 0;

	char work[1024];
	char *next;

	if (!vcfg[0]) return -1;

	memset(v, 0, sizeof(vmode_custom_t));
	v->param.rb = 1; // default reduced blanking to true

	int token_cnt = str_tokenize(strcpyz(work, vcfg), ",", tokens, 32);

	int cnt;
	for (cnt = 0; cnt < token_cnt; cnt++)
	{
		val[cnt] = strtoul(tokens[cnt], &next, 0);
		if (*next)
		{
			break;
		}
	}

	if (cnt == 2 && token_cnt > 2)
	{
		valf = strtod(tokens[cnt], &next);
		if (!*next) cnt++;
	}

	for (int i = cnt; i < token_cnt; i++)
	{
		const char *flag = tokens[i];
		if (!strcasecmp(flag, "+vsync")) v->param.vpol = 1;
		else if (!strcasecmp(flag, "-vsync")) v->param.vpol = 0;
		else if (!strcasecmp(flag, "+hsync")) v->param.hpol = 1;
		else if (!strcasecmp(flag, "-hsync")) v->param.hpol = 0;
		else if (!strcasecmp(flag, "cvt")) v->param.rb = 0;
		else if (!strcasecmp(flag, "cvtrb")) v->param.rb = 1;
		else if (!strcasecmp(flag, "pr")) v->param.pr = 1;
		else
		{
			printf("Error parsing video_mode parameter %d \"%s\": \"%s\"\n", i, flag, vcfg);
			cfg_error("Invalid video_mode\n> %s", vcfg);
			return -1;
		}
	}

	if (cnt == 1)
	{
		v->item[0] = val[0];
		printf("Set predefined video_mode to %d\n", v->item[0]);
		return v->item[0];
	}
	else if (cnt == 3)
	{
		video_calculate_cvt(val[0], val[1], valf ? valf : val[2], v->param.rb, v);
	}
	else if (cnt >= 21)
	{
		for (int i = 0; i < cnt; i++)
			v->item[i] = val[i];
	}
	else if (cnt == 9 || cnt == 11)
	{
		v->item[0] = 1;
		for (int i = 0; i < 8; i++)
			v->item[i + 1] = val[i];

		v->Fpix = val[8] / 1000.0;

		if (cnt == 11)
		{
			v->param.hpol = val[9];
			v->param.vpol = val[10];
		}
	}
	else
	{
		printf("Error parsing video_mode parameter: ""%s""\n", vcfg);
		cfg_error("Invalid video_mode\n> %s", vcfg);
		return -1;
	}

	setPLL(v->Fpix, v);
	return -2;
}

static int store_custom_video_mode(char* vcfg, vmode_custom_t *v)
{
	int ret = parse_custom_video_mode(vcfg, v);
	if (ret == -2) return 1;

	uint mode = (ret >= 0) ? ret : (support_FHD) ? 8 : 0;
	if (mode >= VMODES_NUM) mode = 0;
	if (vmodes[mode].pr == 1 && !supports_pr()) mode = 8;
	for (int i = 0; i < 8; i++) v->item[i + 1] = vmodes[mode].vpar[i];
	v->param.vic = vmodes[mode].vic_mode;
	v->param.pr = vmodes[mode].pr;
	v->param.rb = 1;
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

static void video_mode_load()
{
	if (cfg.direct_video && cfg.vsync_adjust)
	{
		printf("Disabling vsync_adjust because of enabled direct video.\n");
		cfg.vsync_adjust = 0;
	}

	if (cfg.direct_video)
	{
		int mode = cfg.menu_pal ? 2 : 0;
		if (cfg.forced_scandoubler) mode++;

		memset(&v_def, 0, sizeof(v_def));

		v_def.item[0] = mode;
		for (int i = 0; i < 8; i++) v_def.item[i + 1] = tvmodes[mode].vpar[i];
		setPLL(tvmodes[mode].Fpix, &v_def);

		vmode_def = 1;
		vmode_pal = 0;
		vmode_ntsc = 0;
	}
	else
	{
		vmode_def = 0;
		if (!strlen(cfg.video_conf) && !strlen(cfg.video_conf_pal) && !strlen(cfg.video_conf_ntsc))
		{
			vmode_def = get_edid_vmode(&v_def);
		}

		if (!vmode_def)
		{
			vmode_def = store_custom_video_mode(cfg.video_conf, &v_def);
			vmode_pal = store_custom_video_mode(cfg.video_conf_pal, &v_pal);
			vmode_ntsc = store_custom_video_mode(cfg.video_conf_ntsc, &v_ntsc);
		}
	}
}

void video_init()
{
	fb_init();
	hdmi_config_init();
	video_mode_load();

	has_gamma = spi_uio_cmd(UIO_SET_GAMMA);

	loadGammaCfg();
	loadScalerCfg();
	loadShadowMaskCfg();

	video_set_mode(&v_def, 0);
}


static int api1_5 = 0;
int hasAPI1_5()
{
	return api1_5 || is_menu();
}

static bool get_video_info(bool force, VideoInfo *video_info)
{
	static uint16_t nres = 0;
	bool res_changed = false;
	bool fb_changed = false;

	spi_uio_cmd_cont(UIO_GET_VRES);
	uint16_t res = spi_w(0);
	if ((nres != res) || force)
	{
		res_changed = (nres != res);
		nres = res;
		video_info->width = spi_w(0) | (spi_w(0) << 16);
		video_info->height = spi_w(0) | (spi_w(0) << 16);
		video_info->htime = spi_w(0) | (spi_w(0) << 16);
		video_info->vtime = spi_w(0) | (spi_w(0) << 16);
		video_info->ptime = spi_w(0) | (spi_w(0) << 16);
		video_info->vtimeh = spi_w(0) | (spi_w(0) << 16);
		video_info->interlaced = ( res & 0x100 ) != 0;
		video_info->rotated = ( res & 0x200 ) != 0;
	}
	else
	{
		*video_info = current_video_info;
	}
	DisableIO();

	static uint8_t fb_crc = 0;
	uint8_t crc = spi_uio_cmd_cont(UIO_GET_FB_PAR);
	if (fb_crc != crc || force || res_changed)
	{
		fb_changed |= (fb_crc != crc);
		fb_crc = crc;
		video_info->arx = spi_w(0);
		video_info->arxy = !!(video_info->arx & 0x1000);
		video_info->arx &= 0xFFF;
		video_info->ary = spi_w(0) & 0xFFF;
		video_info->fb_fmt = spi_w(0);
		video_info->fb_width = spi_w(0);
		video_info->fb_height = spi_w(0);
		video_info->fb_en = !!(video_info->fb_fmt & 0x40);
	}
	DisableIO();

	return res_changed || fb_changed;
}

static void video_core_description(const VideoInfo *vi, const vmode_custom_t */*vm*/, char *str, size_t len)
{
	float vrate = 100000000;
	if (vi->vtime) vrate /= vi->vtime; else vrate = 0;
	float hrate = 100000;
	if (vi->htime) hrate /= vi->htime; else hrate = 0;

	float prate = vi->width * 100;
	prate /= vi->ptime;

	char res[16];
	snprintf(res, 16, "%dx%d%s", vi->fb_en ? vi->fb_width : vi->width, vi->fb_en ? vi->fb_height : vi->height, vi->interlaced ? "i" : "");
	snprintf(str, len, "%9s %6.2fKHz %5.1fHz", res, hrate, vrate);
}

static void video_scaler_description(const VideoInfo *vi, const vmode_custom_t *vm, char *str, size_t len)
{
	char res[16];
	float vrateh = 100000000;
	if (vi->vtimeh) vrateh /= vi->vtimeh; else vrateh = 0;
	snprintf(res, 16, "%dx%d", vm->item[1] * (vm->param.pr ? 2 : 1), vm->item[5]);
	snprintf(str, len, "%9s %6.2fMHz %5.1fHz", res, vm->Fpix, vrateh);
}

void video_core_description(char *str, size_t len)
{
	video_core_description(&current_video_info, &v_cur, str, len);
}

void video_scaler_description(char *str, size_t len)
{
	video_scaler_description(&current_video_info, &v_cur, str, len);
}

char* video_get_core_mode_name(int with_vrefresh)
{
	static char tmp[256] = {};

	if (with_vrefresh)
	{
		float vrate = 100000000;
		if (current_video_info.vtime) vrate /= current_video_info.vtime; else vrate = 0;

		snprintf(tmp, sizeof(tmp), "%dx%d@%.1f", current_video_info.width, current_video_info.height, vrate);
	}
	else
	{
		snprintf(tmp, sizeof(tmp), "%dx%d", current_video_info.width, current_video_info.height);
	}

	return tmp;
}

static void show_video_info(const VideoInfo *vi, const vmode_custom_t *vm)
{
	float vrate = 100000000;
	if (vi->vtime) vrate /= vi->vtime; else vrate = 0;
	float hrate = 100000;
	if (vi->htime) hrate /= vi->htime; else hrate = 0;

	float prate = vi->width * 100;
	prate /= vi->ptime;

	printf("\033[1;33mINFO: Video resolution: %u x %u%s, fHorz = %.1fKHz, fVert = %.1fHz, fPix = %.2fMHz\033[0m\n",
		vi->width, vi->height, vi->interlaced ? "i" : "", hrate, vrate, prate);
	printf("\033[1;33mINFO: Frame time (100MHz counter): VGA = %d, HDMI = %d\033[0m\n", vi->vtime, vi->vtimeh);
	printf("\033[1;33mINFO: AR = %d:%d, fb_en = %d, fb_width = %d, fb_height = %d\033[0m\n", vi->arx, vi->ary, vi->fb_en, vi->fb_width, vi->fb_height);
	if (vi->vtimeh) api1_5 = 1;
	if (hasAPI1_5() && cfg.video_info)
	{
		char str[128], res1[64], res2[64];
		video_core_description(vi, vm, res1, 64);
		video_scaler_description(vi, vm, res2, 64);
		snprintf(str, 128, "%s\n" \
			"\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\n" \
			"%s", res1, res2);
		Info(str, cfg.video_info * 1000);
	}
}

static void video_resolution_adjust(const VideoInfo *vi, vmode_custom_t *vm)
{
	if (cfg.vscale_mode < 4) return;

	int w = vm->param.pr ? vm->param.hact * 2 : vm->param.hact;
	int h = vm->param.vact;
	const uint32_t core_height = vi->fb_en ? vi->fb_height : vi->rotated ? vi->width : vi->height;
	const uint32_t core_width = vi->fb_en ? vi->fb_width : vi->rotated ? vi->height : vi->width;

	if (w == 0 || h == 0 || core_height == 0 || core_width == 0)
	{
		printf("video_resolution_adjust: invalid core or display sizes. Not adjusting resolution.\n");
		return;
	}

	int scale_h = h / core_height;
	if (!scale_h)
	{
		printf("video_resolution_adjust: display height less than core height. Not adjusting resolution.\n");
		return;
	}

	int ary = vi->ary;
	int arx = vi->arx;
	if (!ary || !arx)
	{
		ary = h;
		arx = w;
	}

	int scale_w = (w * ary) / (core_height * arx);
	if (!scale_w)
	{
		printf("video_resolution_adjust: display width less than core width. Not adjusting resolution.\n");
		return;
	}

	int scale = scale_h > scale_w ? scale_w : scale_h;

	int disp_h = core_height * scale;
	int core_ar_width = (disp_h * arx) / ary;
	int disp_ar_width = (disp_h * w) / h;
	int disp_w;

	if (cfg.vscale_mode == 5)
	{
		if (disp_ar_width < core_ar_width)
		{
			printf("video_resolution_adjust: ideal width %d wider than aspect restricted width %dx%d. Not adjusting resolution.\n", core_ar_width, disp_ar_width, disp_h);
			return;
		}
		disp_w = disp_ar_width;
		printf("video_resolution_adjust: using display aspect ratio - ");
	}
	else
	{
		disp_w = core_ar_width;
		printf("video_resolution_adjust: using core aspect ratio - ");
	}

	disp_w = (disp_w + 7) & ~0x7; // round up to 8

	printf("scale x%d, %dx%d.\n", scale, disp_w, disp_h);

	float refresh = 1000000.0 / ((vm->item[1] + vm->item[2] + vm->item[3] + vm->item[4])*(vm->item[5] + vm->item[6] + vm->item[7] + vm->item[8]) / vm->Fpix);
	video_calculate_cvt(disp_w, disp_h, refresh, vm->param.rb, vm);
	setPLL(vm->Fpix, vm);
}

static void video_scaling_adjust(const VideoInfo *vi, const vmode_custom_t *vm)
{
	if (cfg.vscale_mode >= 4)
	{
		spi_uio_cmd16(UIO_SETHEIGHT, 0);
		spi_uio_cmd16(UIO_SETWIDTH, 0);
		return;
	}

	const uint32_t height = vi->rotated ? vi->width : vi->height;

	uint32_t scrh = vm->item[5];
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
		else if (cfg.vscale_border)
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

	uint32_t scrw = vm->item[1];
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

	minimig_set_adjust(2);
}

bool video_mode_select(uint32_t vtime, vmode_custom_t* out_mode)
{
	vmode_custom_t *v = &v_def;
	bool adjustable = true;

	printf("\033[1;33mvideo_mode_select(%u): ", vtime);

	if (vtime == 0 || !cfg.vsync_adjust)
	{
		printf(", using default mode");
		adjustable = false;
	}
	else if (vmode_pal || vmode_ntsc)
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
				adjustable = false;
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
				adjustable = false;
			}
		}
	}
	else
	{
		printf(", using default mode");
	}
	printf(".\033[0m\n");
	memcpy(out_mode, v, sizeof(vmode_custom_t));
	return adjustable;
}

void video_mode_adjust()
{
	static bool force = false;

	VideoInfo video_info;

	const bool vid_changed = get_video_info(force, &video_info);

	if (vid_changed || force)
	{
		current_video_info = video_info;

		show_video_info(&video_info, &v_cur);
	}
	force = false;

	if (vid_changed && !is_menu())
	{
		if (cfg_has_video_sections())
		{
			cfg_parse();
			video_mode_load();
			user_io_send_buttons(1);
		}

		if ((cfg.vsync_adjust || cfg.vscale_mode >= 4))
		{
			const uint32_t vtime = video_info.vtime;

			printf("\033[1;33madjust_video_mode(%u): vsync_adjust=%d vscale_mode=%d.\033[0m\n", vtime, cfg.vsync_adjust, cfg.vscale_mode);

			vmode_custom_t new_mode;
			bool adjust = video_mode_select(vtime, &new_mode);

			video_resolution_adjust(&video_info, &new_mode);

			vmode_custom_t *v = &new_mode;
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

				float hz = 100000000.0f / vtime;
				if (cfg.refresh_min && hz < cfg.refresh_min)
				{
					printf("Estimated frame rate (%f Hz) is less than REFRESH_MIN(%f Hz). Canceling auto-adjust.\n", hz, cfg.refresh_min);
					Fpix = 0;
				}

				if (cfg.refresh_max && hz > cfg.refresh_max)
				{
					printf("Estimated frame rate (%f Hz) is more than REFRESH_MAX(%f Hz). Canceling auto-adjust.\n", hz, cfg.refresh_max);
					Fpix = 0;
				}
			}

			video_set_mode(v, Fpix);
			user_io_send_buttons(1);
			force = true;
		}
		else if (cfg_has_video_sections()) // if we have video sections but aren't updating the resolution for other reasons, then do it here
		{
			video_set_mode(&v_def, 0);
			user_io_send_buttons(1);
			force = true;
		}
		else
		{
			set_vfilter(1); // force update filters in case interlacing changed
		}

		video_scaling_adjust(&video_info, &v_cur);
	}
	else
	{
		set_vfilter(0); // update filters if flags have changed
	}
}

static void fb_write_module_params()
{
	int width = fb_width;
	int height = fb_height;
	offload_add_work([=]
	{
		FILE *fp = fopen("/sys/module/MiSTer_fb/parameters/mode", "wt");
		if (fp)
		{
			fprintf(fp, "%d %d %d %d %d\n", 8888, 1, width, height, width * 4);
			fclose(fp);
		}
	});
}

void video_fb_enable(int enable, int n)
{
	PROFILE_FUNCTION();

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
					fb_write_module_params();
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
		if (is_menu()) user_io_status_set("[8:5]", (fb_enabled && !fb_num) ? 0x160 : 0);
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


static void video_fb_config()
{
	PROFILE_FUNCTION();

	int fb_scale = cfg.fb_size;

	if (fb_scale <= 1)
	{
		if (((v_cur.item[1] * v_cur.item[5]) > FB_SIZE))
			fb_scale = 2;
		else
			fb_scale = 1;
	}
	else if (fb_scale == 3) fb_scale = 2;
	else if (fb_scale > 4) fb_scale = 4;

	const int fb_scale_x = fb_scale;
	const int fb_scale_y = v_cur.param.pr == 0 ? fb_scale : fb_scale * 2;

	fb_width = v_cur.item[1] / fb_scale_x;
	fb_height = v_cur.item[5] / fb_scale_y;

	brd_x = cfg.vscale_border / fb_scale_x;
	brd_y = cfg.vscale_border / fb_scale_y;

	if (fb_enabled) video_fb_enable(1, fb_num);

	fb_write_module_params();
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

		if (idle < 3)
		{
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

static constexpr int CELL_GRAN_RND = 4;

static int determine_vsync(int w, int h)
{
    const int arx[] =   {4, 16, 16, 5, 15};
    const int ary[] =   {3,  9, 10, 4, 9 };
	const int vsync[] = {4,  5,  6, 7, 7 };

    for (int ar = 0; ar < 5; ar++)
    {
        int w_calc = ((h * arx[ar]) / (ary[ar] * CELL_GRAN_RND)) * CELL_GRAN_RND;
        if (w_calc == w)
        {
            return vsync[ar];
        }
    }

    return 10;
}

static void video_calculate_cvt_int(int h_pixels, int v_lines, float refresh_rate, bool reduced_blanking, vmode_custom_t *vmode)
{
	// Based on xfree86 cvt.c and https://tomverbeure.github.io/video_timings_calculator

	const float CLOCK_STEP = 0.25f;
	const int MIN_V_BPORCH = 6;
	const int V_FRONT_PORCH = 3;

	const int h_pixels_rnd = (h_pixels / CELL_GRAN_RND) * CELL_GRAN_RND;
	const int v_sync = determine_vsync(h_pixels_rnd, v_lines);

	int v_back_porch;
	int h_blank, h_sync, h_back_porch, h_front_porch;
	int total_pixels;
	float pixel_freq;

	if (reduced_blanking)
	{
		const int RB_V_FPORCH = 3;
		const float RB_MIN_V_BLANK = 460.0f;

		float h_period_est = ((1000000.0f / refresh_rate) - RB_MIN_V_BLANK) / (float)v_lines;
		h_blank = 160;

		int vbi_lines = (int)(RB_MIN_V_BLANK / h_period_est) + 1;

		int rb_min_vbi = RB_V_FPORCH + v_sync + MIN_V_BPORCH;
		int act_vbi_lines = (vbi_lines < rb_min_vbi) ? rb_min_vbi : vbi_lines;

		int total_v_lines = act_vbi_lines + v_lines;

		total_pixels = h_blank + h_pixels_rnd;

		pixel_freq = CLOCK_STEP * floorf((refresh_rate * (float)(total_v_lines * total_pixels) / 1000000.0f) / CLOCK_STEP);

		v_back_porch = act_vbi_lines - V_FRONT_PORCH - v_sync;

		h_sync = 32;
		h_back_porch = 80;
		h_front_porch = h_blank - h_sync - h_back_porch;
	}
	else
	{
		const float MIN_VSYNC_BP = 550.0f;
		const float C_PRIME = 30.0f;
		const float M_PRIME = 300.0f;
		const float H_SYNC_PER = 0.08f;

		const float h_period_est = ((1.0f / refresh_rate) - MIN_VSYNC_BP / 1000000.0f) / (float)(v_lines + V_FRONT_PORCH) * 1000000.0f;

		int v_sync_bp = (int)(MIN_VSYNC_BP / h_period_est) + 1;
		if (v_sync_bp < (v_sync + MIN_V_BPORCH))
		{
			v_sync_bp = v_sync + MIN_V_BPORCH;
		}

		v_back_porch = v_sync_bp - v_sync;

		float ideal_duty_cycle = C_PRIME - (M_PRIME * h_period_est / 1000.0f);

		if (ideal_duty_cycle < 20)
		{
			h_blank = (h_pixels_rnd / 4 / (2 * CELL_GRAN_RND)) * (2 * CELL_GRAN_RND);
		}
		else
		{
			h_blank = (int)((float)h_pixels_rnd * ideal_duty_cycle / (100.0f - ideal_duty_cycle) / (2 * CELL_GRAN_RND)) * (2 * CELL_GRAN_RND);
		}

		total_pixels = h_pixels_rnd + h_blank;

		h_sync = (int)(H_SYNC_PER * (float)total_pixels / CELL_GRAN_RND) * CELL_GRAN_RND;
		h_back_porch = h_blank / 2;
		h_front_porch = h_blank - h_sync - h_back_porch;

		pixel_freq = CLOCK_STEP * floorf((float)total_pixels / h_period_est / CLOCK_STEP);
	}

	vmode->item[0] = 1;
	vmode->param.hact = h_pixels_rnd;
	vmode->param.hfp = h_front_porch;
	vmode->param.hs = h_sync;
	vmode->param.hbp = h_back_porch;
	vmode->param.vact = v_lines;
	vmode->param.vfp = V_FRONT_PORCH - 1;
	vmode->param.vs = v_sync;
	vmode->param.vbp = v_back_porch + 1;
	vmode->param.rb = reduced_blanking ? 1 : 0;
	vmode->Fpix = pixel_freq;

	if (h_pixels_rnd > 2048)
	{
		vmode->param.pr = 1;
		vmode->param.hact /= 2;
		vmode->param.hbp /= 2;
		vmode->param.hfp /= 2;
		vmode->param.hs /= 2;
		vmode->Fpix /= 2.0;
	}
	else
	{
		vmode->param.pr = 0;
	}


	printf("Calculated %dx%d@%0.1fhz %s timings: %d,%d,%d,%d,%d,%d,%d,%d,%d,%s%s\n",
		h_pixels, v_lines, refresh_rate, reduced_blanking ? "CVT-RB" : "CVT",
		vmode->item[1], vmode->item[2], vmode->item[3], vmode->item[4],
		vmode->item[5], vmode->item[6], vmode->item[7], vmode->item[8],
		(int)(pixel_freq * 1000.0f),
		reduced_blanking ? "cvtrb" : "cvt",
		vmode->param.pr ? ",pr" : "");
}

static void video_calculate_cvt(int h_pixels, int v_lines, float refresh_rate, int reduced_blanking, vmode_custom_t *vmode)
{
	// If the resolution it too wide and the core doesn't support pixel repetition then just do 1080p
	if (h_pixels > 2048 && !supports_pr())
	{
		printf("Pixel repetition not supported by core for %dx%d resolution, defaulting 1080p.\n", h_pixels, v_lines);
		video_calculate_cvt(1920, 1080, refresh_rate, reduced_blanking, vmode);
		return;
	}

	video_calculate_cvt_int(h_pixels, v_lines, refresh_rate, reduced_blanking == 1, vmode);
	if (vmode->Fpix > 210.f && reduced_blanking == 2)
	{
		printf("Calculated pixel clock is too high. Trying CVT-RB timings.\n");
		video_calculate_cvt_int(h_pixels, v_lines, refresh_rate, 1, vmode);
	}
}
