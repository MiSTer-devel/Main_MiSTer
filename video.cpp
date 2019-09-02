#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/mman.h>
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

#include "support.h"
#include "lib/imlib2/Imlib2.h"

#define FB_SIZE  (1024*1024*8/4)               // 8MB
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


static volatile uint32_t *fb_base = 0;
static int fb_enabled = 0;
static int fb_width = 0;
static int fb_width_full = 0;
static int fb_height = 0;
static int fb_stride = 0;
static int fb_num = 0;

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
};
#define VMODES_NUM (sizeof(vmodes) / sizeof(vmodes[0]))

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

static char scaler_flt_cfg[1024] = { 0 };
static char new_scaler = 0;

static void setScaler()
{
	fileTYPE f = {};
	static char filename[1024];

	if (!spi_uio_cmd_cont(UIO_SET_FLTNUM))
	{
		DisableIO();
		return;
	}

	new_scaler = 1;
	spi8(scaler_flt_cfg[0]);
	DisableIO();
	sprintf(filename, COEFF_DIR"/%s", scaler_flt_cfg + 1);

	if (FileOpen(&f, filename))
	{
		//printf("Read scaler coefficients\n");
		char *buf = (char*)malloc(f.size+1);
		if (buf)
		{
			memset(buf, 0, f.size + 1);
			int size;
			if ((size = FileReadAdv(&f, buf, f.size)))
			{
				spi_uio_cmd_cont(UIO_SET_FLTCOEF);

				char *end = buf + size;
				char *pos = buf;
				int phase = 0;
				while (pos < end)
				{
					char *st = pos;
					while ((pos < end) && *pos && (*pos != 10)) pos++;
					*pos = 0;
					while (*st == ' ' || *st == '\t' || *st == 13) st++;
					if (*st == '#' || *st == ';' || !*st) pos++;
					else
					{
						int c0, c1, c2, c3;
						int n = sscanf(st, "%d,%d,%d,%d", &c0, &c1, &c2, &c3);
						if (n == 4)
						{
							//printf("   phase %c-%02d: %4d,%4d,%4d,%4d\n", (phase >= 16) ? 'V' : 'H', phase % 16, c0, c1, c2, c3);
							//printf("%03X: %03X %03X %03X %03X;\n",phase*4, c0 & 0x1FF, c1 & 0x1FF, c2 & 0x1FF, c3 & 0x1FF);

							spi_w((c0 & 0x1FF) | (((phase * 4) + 0) << 9));
							spi_w((c1 & 0x1FF) | (((phase * 4) + 1) << 9));
							spi_w((c2 & 0x1FF) | (((phase * 4) + 2) << 9));
							spi_w((c3 & 0x1FF) | (((phase * 4) + 3) << 9));

							phase++;
							if (phase >= 32) break;
						}
					}
				}
				DisableIO();
			}

			free(buf);
		}
	}
}

int video_get_scaler_flt()
{
	return new_scaler ? scaler_flt_cfg[0] : -1;
}

char* video_get_scaler_coeff()
{
	return scaler_flt_cfg + 1;
}

static char scaler_cfg[128] = { 0 };

void video_set_scaler_flt(int n)
{
	scaler_flt_cfg[0] = (char)n;
	FileSaveConfig(scaler_cfg, &scaler_flt_cfg, sizeof(scaler_flt_cfg));
	spi_uio_cmd8(UIO_SET_FLTNUM, scaler_flt_cfg[0]);
	spi_uio_cmd(UIO_SET_FLTCOEF);
}

void video_set_scaler_coeff(char *name)
{
	strcpy(scaler_flt_cfg + 1, name);
	FileSaveConfig(scaler_cfg, &scaler_flt_cfg, sizeof(scaler_flt_cfg));
	setScaler();
	user_io_send_buttons(1);
}

static void loadScalerCfg()
{
	sprintf(scaler_cfg, "%s_scaler.cfg", user_io_get_core_name_ex());
	if (!FileLoadConfig(scaler_cfg, &scaler_flt_cfg, sizeof(scaler_flt_cfg) - 1) || scaler_flt_cfg[0]>4)
	{
		memset(scaler_flt_cfg, 0, sizeof(scaler_flt_cfg));
	}
}

static char fb_reset_cmd[128] = {};
static void set_video(vmode_custom_t *v, double Fpix)
{
	loadScalerCfg();
	setScaler();

	printf("Send HDMI parameters:\n");
	spi_uio_cmd_cont(UIO_SET_VIDEO);
	printf("video: ");
	for (int i = 1; i <= 8; i++)
	{
		v_cur.item[i] = v->item[i];
		spi_w(v_cur.item[i]);
		printf("%d, ", v_cur.item[i]);
	}

	for (int i = 9; i < 21; i++) v_cur.item[i] = v->item[i];
	v_cur.Fpix = v->Fpix;

	if(Fpix) setPLL(Fpix, &v_cur);

	printf("\nPLL: ");
	for (int i = 9; i < 21; i++)
	{
		printf("0x%X, ", v_cur.item[i]);
		if (i & 1) spi_w(v_cur.item[i] | ((i == 9 && Fpix && cfg.vsync_adjust == 2 && !is_menu_core()) ? 0x8000 : 0));
		else
		{
			spi_w(v_cur.item[i]);
			spi_w(v_cur.item[i] >> 16);
		}
	}

	printf("Fpix=%f\n", v_cur.Fpix);
	DisableIO();

	if (cfg.fb_size < 1) cfg.fb_size = (v_cur.item[1] < 1000) ? 1 : 2;
	else if (cfg.fb_size == 3) cfg.fb_size = 2;
	else if (cfg.fb_size > 4) cfg.fb_size = 4;

	fb_width = v_cur.item[1] / cfg.fb_size;
	fb_height = v_cur.item[5] / cfg.fb_size;
	fb_stride = ((fb_width * 4) + 255) & ~255;
	fb_width_full = fb_stride / 4;

	if (fb_enabled) video_fb_enable(1, fb_num);

	sprintf(fb_reset_cmd, "echo %d %d %d %d %d >/sys/module/MiSTer_fb/parameters/mode", 8888, 1, fb_width, fb_height, fb_stride);
	system(fb_reset_cmd);
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
		int fd = open("/dev/mem", O_RDWR | O_SYNC);
		if (fd == -1) return;

		fb_base = (volatile uint32_t*)mmap(0, FB_SIZE * 4 * 3, PROT_READ | PROT_WRITE, MAP_SHARED, fd, FB_ADDR);
		if (fb_base == (void *)-1)
		{
			printf("Unable to mmap FB!\n");
			fb_base = 0;
		}
		close(fd);
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
	vmode_def  = store_custom_video_mode(cfg.video_conf, &v_def);
	vmode_pal  = store_custom_video_mode(cfg.video_conf_pal, &v_pal);
	vmode_ntsc = store_custom_video_mode(cfg.video_conf_ntsc, &v_ntsc);
	set_video(&v_def, 0);
}

static int api1_5 = 0;
int hasAPI1_5()
{
	return api1_5 || is_menu_core();
}

static uint32_t show_video_info(int force)
{
	uint32_t ret = 0;
	static uint16_t nres = 0;
	spi_uio_cmd_cont(UIO_GET_VRES);
	uint16_t res = spi_w(0);
	if ((nres != res) || force)
	{
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
		if (height && scrh)
		{
			if (cfg.vscale_border)
			{
				uint32_t border = cfg.vscale_border * 2;
				if ((border + 100) > scrh) border = scrh - 100;
				scrh -= border;
			}

			if (cfg.vscale_mode)
			{
				uint32_t div = 1 << (cfg.vscale_mode - 1);
				uint32_t mag = (scrh*div) / height;
				scrh = (height * mag) / div;
			}

			if(cfg.vscale_border || cfg.vscale_mode)
			{
				printf("Set vertical scaling to : %d\n", scrh);
				spi_uio_cmd16(UIO_SETHEIGHT, scrh);
			}
			else
			{
				spi_uio_cmd16(UIO_SETHEIGHT, 0);
			}
		}

		if (vtime && vtimeh) ret = vtime;
		minimig_set_adjust(2);
	}
	else
	{
		DisableIO();
	}

	return ret;
}

void video_mode_adjust()
{
	uint32_t vtime = show_video_info(0);
	if (vtime && cfg.vsync_adjust && !is_menu_core())
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
		}

		set_video(v, Fpix);
		user_io_send_buttons(1);
		usleep(100000);
		show_video_info(1);
	}
}

void video_fb_enable(int enable, int n)
{
	if (fb_base)
	{
		int res = spi_uio_cmd_cont(UIO_SET_FBUF);
		if (res)
		{
			if (is_menu_core() && !enable && menu_bg)
			{
				enable = 1;
				n = menu_bgn;
			}

			if (enable)
			{
				uint32_t fb_addr = FB_ADDR + (FB_SIZE * 4 * n) + (n ? 0 : 4096);
				fb_num = n;

				printf("Switch to HPS frame buffer\n");
				spi_w((uint16_t)(FB_EN | FB_FMT_RxB | FB_FMT_8888)); // format, enable flag
				spi_w((uint16_t)fb_addr); // base address low word
				spi_w(fb_addr >> 16);     // base address high word
				spi_w(fb_width);          // frame width
				spi_w(fb_height);         // frame height
				spi_w(0);                 // scaled left
				spi_w(v_cur.item[1] - 1); // scaled right
				spi_w(0);                 // scaled top
				spi_w(v_cur.item[5] - 1); // scaled bottom

				printf("HPS frame buffer: %dx%d, stride = %d bytes\n", fb_width, fb_height, fb_stride);
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
		if (is_menu_core()) user_io_8bit_set_status((fb_enabled && !fb_num) ? 0x160 : 0, 0x1E0);
	}
}

int video_fb_state()
{
	if (is_menu_core())
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
	int sz = fb_width/128;

	int pos = 0;
	for (int y = 0; y < fb_height; y++)
	{
		int c1 = (y / sz) & 1;
		pos = y * fb_width_full;
		for (int x = 0; x < fb_width; x++)
		{
			int c2 = c1 ^ ((x / sz) & 1);
			buf[pos++] = c2 ? col2 : col1;
		}
	}
}

static void draw_hbars1()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);
	int old_base = 0;
	int gray = 255;
	int sz = fb_height/7;
	int stp = 0;

	for (int y = 0; y < fb_height; y++)
	{
		int pos = y * fb_width_full;
		int base_color = ((7 * y) / fb_height)+1;
		if (old_base != base_color)
		{
			stp = sz;
			old_base = base_color;
		}

		gray = 255 * stp / sz;

		for (int x = 0; x < fb_width; x++)
		{
			uint32_t color = 0;
			if (base_color & 4) color |= gray;
			if (base_color & 2) color |= gray << 8;
			if (base_color & 1) color |= gray << 16;
			buf[pos++] = color;
		}

		stp--;
		if (stp < 0) stp = 0;
	}
}

static void draw_hbars2()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);

	for (int y = 0; y < fb_height; y++)
	{
		int pos = y * fb_width_full;
		int base_color = ((14 * y) / fb_height);
		int inv = base_color & 1;
		base_color >>= 1;
		base_color = (inv ? base_color : 6 - base_color) + 1;
		for (int x = 0; x < fb_width; x++)
		{
			int gray = (256 * x) / fb_width;
			if (inv) gray = 255 - gray;
			uint32_t color = 0;
			if (base_color & 4) color |= gray;
			if (base_color & 2) color |= gray << 8;
			if (base_color & 1) color |= gray << 16;
			buf[pos++] = color;
		}
	}
}

static void draw_vbars1()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);
	int sz = fb_width / 7;
	int stp = 0;

	for (int y = 0; y < fb_height; y++)
	{
		int pos = y * fb_width_full;
		int old_base = 0;
		int gray = 255;
		for (int x = 0; x < fb_width; x++)
		{
			int base_color = ((7 * x) / fb_width)+1;
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
			buf[pos++] = color;

			stp--;
			if (stp < 0) stp = 0;
		}
	}
}

static void draw_vbars2()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);

	for (int y = 0; y < fb_height; y++)
	{
		int pos = y * fb_width_full;
		for (int x = 0; x < fb_width; x++)
		{
			int gray = ((256 * y) / fb_height);
			int base_color = ((14 * x) / fb_width);
			int inv = base_color & 1;
			base_color >>= 1;
			base_color = (inv ? base_color : 6 - base_color) + 1;

			if (inv) gray = 255 - gray;
			uint32_t color = 0;
			if (base_color & 4) color |= gray;
			if (base_color & 2) color |= gray << 8;
			if (base_color & 1) color |= gray << 16;
			buf[pos++] = color;
		}
	}
}

static void draw_spectrum()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);

	for (int y = 0; y < fb_height; y++)
	{
		int pos = y * fb_width_full;
		int blue = ((256 * y) / fb_height);
		for (int x = 0; x < fb_width; x++)
		{
			int green = ((256 * x) / fb_width) - blue / 2;
			int red = 255 - green - blue / 2;
			if (red < 0) red = 0;
			if (green < 0) green = 0;

			buf[pos++] = (red<<16) | (green<<8) | blue;
		}
	}
}

static void draw_black()
{
	volatile uint32_t* buf = fb_base + (FB_SIZE*menu_bgn);

	for (int y = 0; y < fb_height; y++)
	{
		int pos = y * fb_width_full;
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
	int fb = open("/dev/fb0", O_RDWR);
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

static int bg_has_picture = 0;
extern uint8_t  _binary_logo_png_start[], _binary_logo_png_end[];
void video_menu_bg(int n, int idle)
{
	bg_has_picture = 0;
	menu_bg = n;
	if (n)
	{
		printf("**** BG DEBUG START ****\n");
		printf("n = %d\n", n);

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
		if (!bg1) bg1 = imlib_create_image_using_data(fb_width_full, fb_height, (uint32_t*)(fb_base + (FB_SIZE * 1)));
		if (!bg1) printf("Warning: bg1 is 0\n");
		if (!bg2) bg2 = imlib_create_image_using_data(fb_width_full, fb_height, (uint32_t*)(fb_base + (FB_SIZE * 2)));
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

		switch (n)
		{
		case 1:
			if (menubg || FileExists("menu.png") || FileExists("menu.jpg"))
			{
				if (!menubg)
				{
					if (!menubg && FileExists("menu.png"))
					{
						error = IMLIB_LOAD_ERROR_NONE;
						menubg = imlib_load_image_with_error_return(getFullPath("menu.png"), &error);
						if (!menubg)
						{
							printf("menu.png error = %d\n", error);
						}
					}

					if (!menubg && FileExists("menu.jpg"))
					{
						error = IMLIB_LOAD_ERROR_NONE;
						menubg = imlib_load_image_with_error_return(getFullPath("menu.jpg"), &error);
						if (!menubg)
						{
							printf("menu.jpg error = %d\n", error);
						}
					}

					printf("menubg = %p\n", menubg);
				}

				if (menubg)
				{
					imlib_context_set_image(menubg);
					int src_w = imlib_image_get_width();
					int src_h = imlib_image_get_height();
					printf("menubg: src_w=%d, src_h=%d\n", src_w, src_h);

					if (*bg)
					{
						imlib_context_set_image(*bg);
						imlib_blend_image_onto_image(menubg, 0,
							0, 0,               //int source_x, int source_y,
							src_w, src_h,       //int source_width, int source_height,
							0, 0,               //int destination_x, int destination_y,
							fb_width, fb_height //int destination_width, int destination_height
						);
						bg_has_picture = 1;
						break;
					}
					else
					{
						printf("*bg = 0!\n");
					}
				}
			}
			else
			{
				printf("Neither menu.png nor menu.jpg found!\n");
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

		if (logo && !idle)
		{
			imlib_context_set_image(logo);

			int src_w = imlib_image_get_width();
			int src_h = imlib_image_get_height();
			printf("logo: src_w=%d, src_h=%d\n", src_w, src_h);

			if (*bg)
			{
				imlib_context_set_image(*bg);
				imlib_blend_image_onto_image(logo, 1,
					0, 0,         //int source_x, int source_y,
					src_w, src_h, //int source_width, int source_height,
					0, 0,         //int destination_x, int destination_y,
					fb_width*2 / 7, src_h*fb_width*2 / (src_w * 7) //int destination_width, int destination_height
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
		vs_wait();
		printf("**** BG DEBUG END ****\n");
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
		if ((fd = open("/dev/tty0", O_RDONLY)) >= 0)
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

			div = 1;
			while ((width*(div + 1)) <= (int)v_cur.item[1] && (height*(div + 1)) <= (int)v_cur.item[5]) div++;

			hmin = (uint16_t)((v_cur.item[1] - (width * div)) / 2);
			vmin = (uint16_t)((v_cur.item[5] - (height * div)) / 2);
			hmax = hmin + (width * div) - 1;
			vmax = vmin + (height * div) - 1;
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
			int stride = ((width * bpp) + 255) & ~255;
			printf("fb_cmd: new mode: %dx%d => %dx%d color=%d stride=%d\n", width, height, hmax - hmin + 1, vmax - vmin + 1, fmt, stride);

			uint32_t addr = FB_ADDR + 4096;

			spi_uio_cmd_cont(UIO_SET_FBUF);
			spi_w(FB_EN | sc_fmt); // format, enable flag
			spi_w((uint16_t)addr); // base address low word
			spi_w(addr >> 16);     // base address high word
			spi_w(width);          // frame width
			spi_w(height);         // frame height
			spi_w(hmin);           // scaled left
			spi_w(hmax);           // scaled right
			spi_w(vmin);           // scaled top
			spi_w(vmax);           // scaled bottom
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
