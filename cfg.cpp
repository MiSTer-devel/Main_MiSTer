// cfg.c
// 2015, rok.krajnc@gmail.com
// 2017+, Sorgelig

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include "cfg.h"
#include "debug.h"
#include "file_io.h"
#include "user_io.h"
#include "video.h"
#include "support/arcade/mra_loader.h"

cfg_t cfg;
static FILE *orig_stdout = NULL;
static FILE *dev_null = NULL;

typedef enum
{
	UINT8 = 0, INT8, UINT16, INT16, UINT32, INT32, HEX8, HEX16, HEX32, FLOAT, STRING, UINT32ARR, HEX32ARR, STRINGARR
} ini_vartypes_t;

typedef struct
{
	const char* name;
	void* var;
	ini_vartypes_t type;
	int64_t min;
	int64_t max;
} ini_var_t;

static const ini_var_t ini_vars[] =
{
	{ "YPBPR", (void*)(&(cfg.vga_mode_int)), UINT8, 0, 1 },
	{ "COMPOSITE_SYNC", (void*)(&(cfg.csync)), UINT8, 0, 1 },
	{ "FORCED_SCANDOUBLER", (void*)(&(cfg.forced_scandoubler)), UINT8, 0, 1 },
	{ "VGA_SCALER", (void*)(&(cfg.vga_scaler)), UINT8, 0, 1 },
	{ "VGA_SOG", (void*)(&(cfg.vga_sog)), UINT8, 0, 1 },
	{ "KEYRAH_MODE", (void*)(&(cfg.keyrah_mode)), HEX32, 0, 0xFFFFFFFF },
	{ "RESET_COMBO", (void*)(&(cfg.reset_combo)), UINT8, 0, 3 },
	{ "KEY_MENU_AS_RGUI", (void*)(&(cfg.key_menu_as_rgui)), UINT8, 0, 1 },
	{ "VIDEO_MODE", (void*)(cfg.video_conf), STRING, 0, sizeof(cfg.video_conf) - 1 },
	{ "VIDEO_MODE_PAL", (void*)(cfg.video_conf_pal), STRING, 0, sizeof(cfg.video_conf_pal) - 1 },
	{ "VIDEO_MODE_NTSC", (void*)(cfg.video_conf_ntsc), STRING, 0, sizeof(cfg.video_conf_ntsc) - 1 },
	{ "VIDEO_INFO", (void*)(&(cfg.video_info)), UINT8, 0, 10 },
	{ "VSYNC_ADJUST", (void*)(&(cfg.vsync_adjust)), UINT8, 0, 2 },
	{ "SUBCARRIER", (void*)(&(cfg.subcarrier)), UINT8, 0, 1 },
	{ "HDMI_AUDIO_96K", (void*)(&(cfg.hdmi_audio_96k)), UINT8, 0, 1 },
	{ "DVI_MODE", (void*)(&(cfg.dvi_mode)), UINT8, 0, 1 },
	{ "HDMI_LIMITED", (void*)(&(cfg.hdmi_limited)), UINT8, 0, 2 },
	{ "KBD_NOMOUSE", (void*)(&(cfg.kbd_nomouse)), UINT8, 0, 1 },
	{ "MOUSE_THROTTLE", (void*)(&(cfg.mouse_throttle)), UINT8, 1, 100 },
	{ "BOOTSCREEN", (void*)(&(cfg.bootscreen)), UINT8, 0, 1 },
	{ "VSCALE_MODE", (void*)(&(cfg.vscale_mode)), UINT8, 0, 5 },
	{ "VSCALE_BORDER", (void*)(&(cfg.vscale_border)), UINT16, 0, 399 },
	{ "RBF_HIDE_DATECODE", (void*)(&(cfg.rbf_hide_datecode)), UINT8, 0, 1 },
	{ "MENU_PAL", (void*)(&(cfg.menu_pal)), UINT8, 0, 1 },
	{ "BOOTCORE", (void*)(&(cfg.bootcore)), STRING, 0, sizeof(cfg.bootcore) - 1 },
	{ "BOOTCORE_TIMEOUT", (void*)(&(cfg.bootcore_timeout)), INT16, 2, 30 },
	{ "FONT", (void*)(&(cfg.font)), STRING, 0, sizeof(cfg.font) - 1 },
	{ "FB_SIZE", (void*)(&(cfg.fb_size)), UINT8, 0, 4 },
	{ "FB_TERMINAL", (void*)(&(cfg.fb_terminal)), UINT8, 0, 1 },
	{ "OSD_TIMEOUT", (void*)(&(cfg.osd_timeout)), INT16, 0, 3600 },
	{ "DIRECT_VIDEO", (void*)(&(cfg.direct_video)), UINT8, 0, 2 },
	{ "OSD_ROTATE", (void*)(&(cfg.osd_rotate)), UINT8, 0, 2 },
	{ "DEADZONE", (void*)(&(cfg.controller_deadzone)), STRINGARR, sizeof(cfg.controller_deadzone) / sizeof(*cfg.controller_deadzone), sizeof(*cfg.controller_deadzone) },
	{ "GAMEPAD_DEFAULTS", (void*)(&(cfg.gamepad_defaults)), UINT8, 0, 1 },
	{ "RECENTS", (void*)(&(cfg.recents)), UINT8, 0, 1 },
	{ "CONTROLLER_INFO", (void*)(&(cfg.controller_info)), UINT8, 0, 10 },
	{ "REFRESH_MIN", (void*)(&(cfg.refresh_min)), FLOAT, 0, 150 },
	{ "REFRESH_MAX", (void*)(&(cfg.refresh_max)), FLOAT, 0, 150 },
	{ "JAMMA_VID", (void*)(&(cfg.jamma_vid)), HEX16, 0, 0xFFFF },
	{ "JAMMA_PID", (void*)(&(cfg.jamma_pid)), HEX16, 0, 0xFFFF },
	{ "JAMMA2_VID", (void*)(&(cfg.jamma2_vid)), HEX16, 0, 0xFFFF },
	{ "JAMMA2_PID", (void*)(&(cfg.jamma2_pid)), HEX16, 0, 0xFFFF },
	{ "SNIPER_MODE", (void*)(&(cfg.sniper_mode)), UINT8, 0, 1 },
	{ "BROWSE_EXPAND", (void*)(&(cfg.browse_expand)), UINT8, 0, 1 },
	{ "LOGO", (void*)(&(cfg.logo)), UINT8, 0, 1 },
	{ "SHARED_FOLDER", (void*)(&(cfg.shared_folder)), STRING, 0, sizeof(cfg.shared_folder) - 1 },
	{ "NO_MERGE_VID", (void*)(&(cfg.no_merge_vid)), HEX16, 0, 0xFFFF },
	{ "NO_MERGE_PID", (void*)(&(cfg.no_merge_pid)), HEX16, 0, 0xFFFF },
	{ "NO_MERGE_VIDPID", (void*)(cfg.no_merge_vidpid), HEX32ARR, 0, 0xFFFFFFFF },
	{ "CUSTOM_ASPECT_RATIO_1", (void*)(&(cfg.custom_aspect_ratio[0])), STRING, 0, sizeof(cfg.custom_aspect_ratio[0]) - 1 },
	{ "CUSTOM_ASPECT_RATIO_2", (void*)(&(cfg.custom_aspect_ratio[1])), STRING, 0, sizeof(cfg.custom_aspect_ratio[1]) - 1 },
	{ "SPINNER_VID", (void*)(&(cfg.spinner_vid)), HEX16, 0, 0xFFFF },
	{ "SPINNER_PID", (void*)(&(cfg.spinner_pid)), HEX16, 0, 0xFFFF },
	{ "SPINNER_AXIS", (void*)(&(cfg.spinner_axis)), UINT8, 0, 2 },
	{ "SPINNER_THROTTLE", (void*)(&(cfg.spinner_throttle)), INT32, -10000, 10000 },
	{ "AFILTER_DEFAULT", (void*)(&(cfg.afilter_default)), STRING, 0, sizeof(cfg.afilter_default) - 1 },
	{ "VFILTER_DEFAULT", (void*)(&(cfg.vfilter_default)), STRING, 0, sizeof(cfg.vfilter_default) - 1 },
	{ "VFILTER_VERTICAL_DEFAULT", (void*)(&(cfg.vfilter_vertical_default)), STRING, 0, sizeof(cfg.vfilter_vertical_default) - 1 },
	{ "VFILTER_SCANLINES_DEFAULT", (void*)(&(cfg.vfilter_scanlines_default)), STRING, 0, sizeof(cfg.vfilter_scanlines_default) - 1 },
	{ "SHMASK_DEFAULT", (void*)(&(cfg.shmask_default)), STRING, 0, sizeof(cfg.shmask_default) - 1 },
	{ "SHMASK_MODE_DEFAULT", (void*)(&(cfg.shmask_mode_default)), UINT8, 0, 255 },
	{ "PRESET_DEFAULT", (void*)(&(cfg.preset_default)), STRING, 0, sizeof(cfg.preset_default) - 1 },
	{ "LOG_FILE_ENTRY", (void*)(&(cfg.log_file_entry)), UINT8, 0, 1 },
	{ "BT_AUTO_DISCONNECT", (void*)(&(cfg.bt_auto_disconnect)), UINT32, 0, 180 },
	{ "BT_RESET_BEFORE_PAIR", (void*)(&(cfg.bt_reset_before_pair)), UINT8, 0, 1 },
	{ "WAITMOUNT", (void*)(&(cfg.waitmount)), STRING, 0, sizeof(cfg.waitmount) - 1 },
	{ "RUMBLE", (void *)(&(cfg.rumble)), UINT8, 0, 1 },
	{ "WHEEL_FORCE", (void*)(&(cfg.wheel_force)), UINT8, 0, 100 },
	{ "WHEEL_RANGE", (void*)(&(cfg.wheel_range)), UINT16, 0, 1000 },
	{ "HDMI_GAME_MODE", (void *)(&(cfg.hdmi_game_mode)), UINT8, 0, 1 },
	{ "VRR_MODE", (void *)(&(cfg.vrr_mode)), UINT8, 0, 3 },
	{ "VRR_MIN_FRAMERATE", (void *)(&(cfg.vrr_min_framerate)), UINT8, 0, 255 },
	{ "VRR_MAX_FRAMERATE", (void *)(&(cfg.vrr_max_framerate)), UINT8, 0, 255 },
	{ "VRR_VESA_FRAMERATE", (void *)(&(cfg.vrr_vesa_framerate)), UINT8, 0, 255 },
	{ "VIDEO_OFF", (void*)(&(cfg.video_off)), INT16, 0, 3600 },
	{ "PLAYER_1_CONTROLLER", (void*)(&(cfg.player_controller[0])), STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]) },
	{ "PLAYER_2_CONTROLLER", (void*)(&(cfg.player_controller[1])), STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]) },
	{ "PLAYER_3_CONTROLLER", (void*)(&(cfg.player_controller[2])), STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]) },
	{ "PLAYER_4_CONTROLLER", (void*)(&(cfg.player_controller[3])), STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]) },
	{ "PLAYER_5_CONTROLLER", (void*)(&(cfg.player_controller[4])), STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]) },
	{ "PLAYER_6_CONTROLLER", (void*)(&(cfg.player_controller[5])), STRINGARR, sizeof(cfg.player_controller[0]) / sizeof(cfg.player_controller[0][0]), sizeof(cfg.player_controller[0][0]) },
	{ "DISABLE_AUTOFIRE", (void *)(&(cfg.disable_autofire)), UINT8, 0, 1 },
	{ "VIDEO_BRIGHTNESS", (void *)(&(cfg.video_brightness)), UINT8, 0, 100 },
	{ "VIDEO_CONTRAST", (void *)(&(cfg.video_contrast)), UINT8, 0, 100 },
	{ "VIDEO_SATURATION", (void *)(&(cfg.video_saturation)), UINT8, 0, 100 },
	{ "VIDEO_HUE", (void *)(&(cfg.video_hue)), UINT16, 0, 360 },
	{ "VIDEO_GAIN_OFFSET", (void *)(&(cfg.video_gain_offset)), STRING, 0, sizeof(cfg.video_gain_offset) },
	{ "HDR", (void*)(&cfg.hdr), UINT8, 0, 2 },
	{ "HDR_MAX_NITS", (void*)(&(cfg.hdr_max_nits)), UINT16, 100, 10000 },
	{ "HDR_AVG_NITS", (void*)(&(cfg.hdr_avg_nits)), UINT16, 100, 10000 },
	{ "VGA_MODE", (void*)(&(cfg.vga_mode)), STRING, 0, sizeof(cfg.vga_mode) - 1 },
	{ "NTSC_MODE", (void *)(&(cfg.ntsc_mode)), UINT8, 0, 2 },
	{ "CONTROLLER_UNIQUE_MAPPING", (void *)(cfg.controller_unique_mapping), UINT32ARR, 0, 0xFFFFFFFF },
	{ "OSD_LOCK", (void*)(&(cfg.osd_lock)), STRING, 0, sizeof(cfg.osd_lock) - 1 },
	{ "OSD_LOCK_TIME", (void*)(&(cfg.osd_lock_time)), UINT16, 0, 60 },
	{ "DEBUG", (void *)(&(cfg.debug)), UINT8, 0, 1 },
	{ "LOOKAHEAD", (void *)(&(cfg.lookahead)), UINT8, 0, 3 },
	{ "MAIN", (void*)(&(cfg.main)), STRING, 0, sizeof(cfg.main) - 1 },
	{"VFILTER_INTERLACE_DEFAULT", (void*)(&(cfg.vfilter_interlace_default)), STRING, 0, sizeof(cfg.vfilter_interlace_default) - 1 },
};

static const int nvars = (int)(sizeof(ini_vars) / sizeof(ini_var_t));

#define INI_LINE_SIZE           1024

#define INI_SECTION_START       '['
#define INI_SECTION_END         ']'
#define INCL_SECTION            '+'

#define CHAR_IS_NUM(c)          (((c) >= '0') && ((c) <= '9'))
#define CHAR_IS_ALPHA_LOWER(c)  (((c) >= 'a') && ((c) <= 'z'))
#define CHAR_IS_ALPHA_UPPER(c)  (((c) >= 'A') && ((c) <= 'Z'))
#define CHAR_IS_ALPHANUM(c)     (CHAR_IS_ALPHA_LOWER(c) || CHAR_IS_ALPHA_UPPER(c) || CHAR_IS_NUM(c))
#define CHAR_IS_SPECIAL(c)      (((c) == '[') || ((c) == ']') || ((c) == '(') || ((c) == ')') || \
                                 ((c) == '-') || ((c) == '+') || ((c) == '/') || ((c) == '=') || \
                                 ((c) == '#') || ((c) == '$') || ((c) == '@') || ((c) == '_') || \
                                 ((c) == ',') || ((c) == '.') || ((c) == '!') || ((c) == '*') || \
                                 ((c) == ':') || ((c) == '~'))

#define CHAR_IS_VALID(c)        (CHAR_IS_ALPHANUM(c) || CHAR_IS_SPECIAL(c))
#define CHAR_IS_SPACE(c)        (((c) == ' ') || ((c) == '\t'))
#define CHAR_IS_LINEEND(c)      (((c) == '\n'))
#define CHAR_IS_COMMENT(c)      (((c) == ';'))
#define CHAR_IS_QUOTE(c)        (((c) == '"'))


fileTYPE ini_file;

static bool has_video_sections = false;
static bool using_video_section = false;

int ini_pt = 0;
static char ini_getch()
{
	static uint8_t buf[512];
	if (!(ini_pt & 0x1ff)) FileReadSec(&ini_file, buf);
	if (ini_pt >= ini_file.size) return 0;
	return buf[(ini_pt++) & 0x1ff];
}

static int ini_getline(char* line)
{
	char c, ignore = 0, skip = 1;
	int i = 0;

	while ((c = ini_getch()))
	{
		if (!CHAR_IS_SPACE(c)) skip = 0;
		if (i >= (INI_LINE_SIZE - 1) || CHAR_IS_COMMENT(c)) ignore = 1;

		if (CHAR_IS_LINEEND(c)) break;
		if ((CHAR_IS_SPACE(c) || CHAR_IS_VALID(c)) && !ignore && !skip) line[i++] = c;
	}
	line[i] = 0;
	while (i > 0 && CHAR_IS_SPACE(line[i - 1])) line[--i] = 0;
	return c == 0;
}

static int ini_get_section(char* buf, const char *vmode)
{
	int i = 0;
	int incl = (buf[0] == INCL_SECTION);

	// get section start marker
	if (buf[0] != INI_SECTION_START && buf[0] != INCL_SECTION)
	{
		return 0;
	}
	else buf++;

	int wc_pos = -1;
	int eq_pos = -1;

	// get section stop marker
	while (buf[i])
	{
		if (buf[i] == INI_SECTION_END)
		{
			buf[i] = 0;
			break;
		}

		if (buf[i] == '*') wc_pos = i;
		if (buf[i] == '=') eq_pos = i;

		i++;
		if (i >= INI_LINE_SIZE) return 0;
	}

	if (!strcasecmp(buf, "MiSTer") ||
		(is_arcade() && !strcasecmp(buf, "arcade")) ||
		(arcade_is_vertical() && !strcasecmp(buf, "arcade_vertical")) ||
		((wc_pos >= 0) ? !strncasecmp(buf, user_io_get_core_name(1), wc_pos) : !strcasecmp(buf, user_io_get_core_name(1))) ||
		((wc_pos >= 0) ? !strncasecmp(buf, user_io_get_core_name(0), wc_pos) : !strcasecmp(buf, user_io_get_core_name(0))))
	{
		if (incl)
		{
			ini_parser_debugf("included '%s'", buf);
		}
		else
		{
			ini_parser_debugf("Got SECTION '%s'", buf);
		}
		return 1;
	}
	else if ((eq_pos >= 0) && !strncasecmp(buf, "video", eq_pos))
	{
		has_video_sections = true;
		if(!strcasecmp(&buf[eq_pos+1], vmode))
		{
			using_video_section = true;
			ini_parser_debugf("Got SECTION '%s'", buf);
			return 1;
		}
		else
		{
			return 0;
		}
	}

	return 0;
}

static void ini_parse_numeric(const ini_var_t *var, const char *text, void *out)
{
	uint32_t u32 = 0;
	int32_t i32 = 0;
	float f32 = 0.0f;
	char *endptr = nullptr;

	bool out_of_range = true;
	bool invalid_format = false;

	switch(var->type)
	{
	case HEX8:
	case HEX16:
	case HEX32:
	case HEX32ARR:
		invalid_format = strncasecmp(text, "0x", 2);
		// fall through

	case UINT8:
	case UINT16:
	case UINT32:
	case UINT32ARR:
		u32 = strtoul(text, &endptr, 0);
		if (u32 < var->min) u32 = var->min;
		else if (u32 > var->max) u32 = var->max;
		else out_of_range = false;
		break;

	case INT8:
	case INT16:
	case INT32:
		i32 = strtol(text, &endptr, 0);
		if (i32 < var->min) i32 = var->min;
		else if (i32 > var->max) i32 = var->max;
		else out_of_range = false;
		break;

	case FLOAT:
		f32 = strtof(text, &endptr);
		if (f32 < var->min) f32 = var->min;
		else if (f32 > var->max) f32 = var->max;
		else out_of_range = false;
		break;

	default:
		out_of_range = false;
		break;
	}

	if (*endptr) cfg_error("%s: \'%s\' not a number", var->name, text);
	else if (out_of_range) cfg_error("%s: \'%s\' out of range", var->name, text);
	else if (invalid_format) cfg_error("%s: \'%s\' invalid format", var->name, text);

	switch (var->type)
	{
	case HEX8:
	case UINT8: *(uint8_t*)out = u32; break;
	case INT8: *(int8_t*)out = i32; break;
	case HEX16:
	case UINT16: *(uint16_t*)out = u32; break;
	case HEX32ARR:
	case UINT32ARR: *(uint32_t*)out = u32; break;
	case INT16: *(int16_t*)out = i32; break;
	case HEX32:
	case UINT32: *(uint32_t*)out = u32; break;
	case INT32: *(int32_t*)out = i32; break;
	case FLOAT: *(float*)out = f32; break;
	default: break;
	}
}

// Used to determine if an array variable should be appended or restarted.
static bool var_array_append[sizeof(ini_vars) / sizeof(ini_var_t)] = {};

static void ini_parse_var(char* buf)
{
	// find var
	int i = 0;
	while (1)
	{
		if (buf[i] == '=' || CHAR_IS_SPACE(buf[i]))
		{
			buf[i] = 0;
			break;
		}
		else if (!buf[i]) return;
		i++;
	}

	// parse var
	int var_id = -1;
	for (int j = 0; j < (int)(sizeof(ini_vars) / sizeof(ini_var_t)); j++)
	{
		if (!strcasecmp(buf, ini_vars[j].name)) var_id = j;
	}

	if (var_id == -1)
	{
		cfg_error("%s: unknown option", buf);
	}
	else // get data
	{
		i++;
		while (buf[i] == '=' || CHAR_IS_SPACE(buf[i])) i++;
		ini_parser_debugf("Got VAR '%s' with VALUE %s", buf, buf+i);

		const ini_var_t *var = &ini_vars[var_id];

		switch (var->type)
		{
		case STRING:
			memset(var->var, 0, var->max);
			snprintf((char*)(var->var), var->max, "%s", buf+i);
			break;

		case STRINGARR:
			{
				int item_sz = var->max;

				if (!var_array_append[var_id])
				{
					var_array_append[var_id] = true;

					for (int n = 0; n < var->min; n++)
					{
						char *str = ((char*)var->var) + (n * item_sz);
						str[0] = 0;
					}
				}

				for (int n = 0; n < var->min; n++)
				{
					char *str = ((char*)var->var) + (n * item_sz);
					if (!strlen(str))
					{
						snprintf(str, item_sz, "%s", buf + i);
						break;
					}
				}
			}
			break;

		case HEX32ARR:
		case UINT32ARR:
			{
				if (!var_array_append[var_id])
				{
					var_array_append[var_id] = true;

					uint32_t *arr = (uint32_t*)var->var;
					arr[0] = 0;
				}

				uint32_t *arr = (uint32_t*)var->var;
				uint32_t pos = ++arr[0];
				ini_parse_numeric(var, &buf[i], &arr[pos]);
			}
			break;

		default:
			ini_parse_numeric(var, &buf[i], var->var);
			if (!strcasecmp(var->name, "DEBUG"))
			{
				stdout = cfg.debug ? orig_stdout : dev_null;
			}
			break;
		}
	}
}

static void ini_parse(int alt, const char *vmode)
{
	static char line[INI_LINE_SIZE];
	int section = 0;
	int eof;

	if (!orig_stdout) orig_stdout = stdout;
	if (!dev_null)
	{
		dev_null = fopen("/dev/null", "w");
		if (dev_null)
		{
			int null_fd = fileno(dev_null);
			if (null_fd >= 0) fcntl(null_fd, F_SETFD, FD_CLOEXEC);
			stdout = dev_null;
		}
	}

	ini_parser_debugf("Start INI parser for core \"%s\"(%s), video mode \"%s\".", user_io_get_core_name(0), user_io_get_core_name(1), vmode);

	memset(line, 0, sizeof(line));
	memset(&ini_file, 0, sizeof(ini_file));

	const char *name = cfg_get_name(alt);
	if (!FileOpen(&ini_file, name))	return;

	ini_parser_debugf("Opened file %s with size %llu bytes.", name, ini_file.size);

	ini_pt = 0;

	// parse ini
	while (1)
	{
		// get line
		eof = ini_getline(line);
		ini_parser_debugf("line(%d): \"%s\".", section, line);

		if (line[0] == INI_SECTION_START)
		{
			// if first char in line is INI_SECTION_START, get section
			section = ini_get_section(line, vmode);
			if (section)
			{
				memset(var_array_append, 0, sizeof(var_array_append));
			}
		}
		else if (line[0] == INCL_SECTION && !section)
		{
			section = ini_get_section(line, vmode);
			if (section)
			{
				memset(var_array_append, 0, sizeof(var_array_append));
			}
		}
		else if(section)
		{
			// otherwise this is a variable, get it
			ini_parse_var(line);
		}

		// if end of file, stop
		if (eof) break;
	}

	FileClose(&ini_file);
}

static constexpr int CFG_ERRORS_MAX = 4;
static constexpr int CFG_ERRORS_STRLEN = 128;
static char cfg_errors[CFG_ERRORS_MAX][CFG_ERRORS_STRLEN];
static int cfg_error_count = 0;

const char* cfg_get_name(uint8_t alt)
{
	static int done = 0;
	static char names[3][64] = {};
	static char name[64];

	if (!done)
	{
		done = 1;
		DIR *d = opendir(getRootDir());
		if (!d)
		{
			printf("Couldn't open dir: %s\n", getRootDir());
		}
		else
		{
			struct dirent *de;
			int i = 0;
			while ((de = readdir(d)) && i < 3)
			{
				int len = strlen(de->d_name);
				if (!strncasecmp(de->d_name, "MiSTer_", 7) && !strcasecmp(de->d_name + len - 4, ".ini"))
				{
					snprintf(names[i], sizeof(names[0]), "%s", de->d_name);
					i++;
				}
			}
			closedir(d);
		}

		for (int i = 1; i < 3; i++)
		{
			for (int j = 1; j < 3; j++)
			{
				if ((!names[j - 1][0] && names[j][0]) || (names[j - 1][0] && names[j][0] && strcasecmp(names[j - 1], names[j]) > 0))
				{
					strcpy(name, names[j - 1]);
					strcpy(names[j - 1], names[j]);
					strcpy(names[j], name);
				}
			}
		}
	}

	strcpy(name, "MiSTer.ini");
	if (alt && alt < 4) strcpy(name, names[alt-1]);
	return name;
}

const char* cfg_get_label(uint8_t alt)
{
	if (!alt) return "Main";

	const char *name = cfg_get_name(alt);
	if (!name[0]) return " -- ";

	static char label[6];
	snprintf(label, sizeof(label), "%s", name + 7);
	char *p = strrchr(label, '.');
	if (p) *p = 0;
	if (!strcasecmp(label, "alt"))   return "Alt1";
	if (!strcasecmp(label, "alt_1")) return "Alt1";
	if (!strcasecmp(label, "alt_2")) return "Alt2";
	if (!strcasecmp(label, "alt_3")) return "Alt3";

	for (int i = 0; i < 4; i++) if (!label[i]) label[i] = ' ';
	label[4] = 0;
	return label;
}

void cfg_parse()
{
	memset(&cfg, 0, sizeof(cfg));
	cfg.bootscreen = 1;
	cfg.fb_terminal = 1;
	cfg.controller_info = 6;
	cfg.browse_expand = 1;
	cfg.logo = 1;
	cfg.rumble = 1;
	cfg.wheel_force = 50;
	cfg.dvi_mode = 2;
	cfg.subcarrier = 0;
	cfg.lookahead = 2;
	cfg.hdr = 0;
	cfg.hdr_max_nits = 1000;
	cfg.hdr_avg_nits = 250;
	cfg.video_brightness = 50;
	cfg.video_contrast = 50;
	cfg.video_saturation = 100;
	cfg.video_hue = 0;
	strcpy(cfg.video_gain_offset, "1, 0, 1, 0, 1, 0");
	strcpy(cfg.main, "MiSTer");
	has_video_sections = false;
	using_video_section = false;
	cfg_error_count = 0;
	ini_parse(altcfg(), video_get_core_mode_name(1));
	if (has_video_sections && !using_video_section)
	{
		// second pass to look for section without vrefresh
		ini_parse(altcfg(), video_get_core_mode_name(0));
	}

	if (strlen(cfg.vga_mode))
	{
		if (!strcasecmp(cfg.vga_mode, "rgb")) cfg.vga_mode_int = 0;
		if (!strcasecmp(cfg.vga_mode, "ypbpr")) cfg.vga_mode_int = 1;
		if (!strcasecmp(cfg.vga_mode, "svideo")) cfg.vga_mode_int = 2;
		if (!strcasecmp(cfg.vga_mode, "cvbs")) cfg.vga_mode_int = 3;
	}
}

bool cfg_has_video_sections()
{
	return has_video_sections;
}


void cfg_error(const char *fmt, ...)
{
	if (cfg_error_count >= CFG_ERRORS_MAX) return;

	va_list args;
	va_start(args, fmt);
	vsnprintf(cfg_errors[cfg_error_count], CFG_ERRORS_STRLEN, fmt, args);
	va_end(args);

	printf("ERROR CFG: %s\n", cfg_errors[cfg_error_count]);

	cfg_error_count += 1;
}

bool cfg_check_errors(char *msg, size_t max_len)
{
	msg[0] = '\0';

	if (cfg_error_count == 0) return false;

	int pos = snprintf(msg, max_len, "%d INI Error%s\n---", cfg_error_count, cfg_error_count > 1 ? "s" : "");

	for (int i = 0; i < cfg_error_count; i++)
	{
		pos += snprintf(msg + pos, max_len - pos, "\n%s\n", cfg_errors[i]);
	}

	return true;
}

void cfg_print()
{
	printf("Loaded config:\n--------------\n");
	for (uint i = 0; i < (sizeof(ini_vars) / sizeof(ini_vars[0])); i++)
	{
		switch (ini_vars[i].type)
		{
		case UINT8:
			printf("  %s=%u\n", ini_vars[i].name, *(uint8_t*)ini_vars[i].var);
			break;

		case UINT16:
			printf("  %s=%u\n", ini_vars[i].name, *(uint16_t*)ini_vars[i].var);
			break;

		case UINT32:
			printf("  %s=%u\n", ini_vars[i].name, *(uint32_t*)ini_vars[i].var);
			break;

		case UINT32ARR:
			if (*(uint32_t*)ini_vars[i].var)
			{
				uint32_t* arr = (uint32_t*)ini_vars[i].var;
				for (uint32_t n = 0; n < arr[0]; n++) printf("  %s=%u\n", ini_vars[i].name, arr[n + 1]);
			}
			break;

		case HEX8:
			printf("  %s=0x%02X\n", ini_vars[i].name, *(uint8_t*)ini_vars[i].var);
			break;

		case HEX16:
			printf("  %s=0x%04X\n", ini_vars[i].name, *(uint16_t*)ini_vars[i].var);
			break;

		case HEX32:
			printf("  %s=0x%08X\n", ini_vars[i].name, *(uint32_t*)ini_vars[i].var);
			break;

		case HEX32ARR:
			if (*(uint32_t*)ini_vars[i].var)
			{
				uint32_t* arr = (uint32_t*)ini_vars[i].var;
				for (uint32_t n = 0; n < arr[0]; n++) printf("  %s=0x%08X\n", ini_vars[i].name, arr[n + 1]);
			}
			break;

		case INT8:
			printf("  %s=%d\n", ini_vars[i].name, *(int8_t*)ini_vars[i].var);
			break;

		case INT16:
			printf("  %s=%d\n", ini_vars[i].name, *(int16_t*)ini_vars[i].var);
			break;

		case INT32:
			printf("  %s=%d\n", ini_vars[i].name, *(int32_t*)ini_vars[i].var);
			break;

		case FLOAT:
			printf("  %s=%f\n", ini_vars[i].name, *(float*)ini_vars[i].var);
			break;

		case STRING:
			if (*(uint32_t*)ini_vars[i].var) printf("  %s=%s\n", ini_vars[i].name, (char*)ini_vars[i].var);
			break;

		case STRINGARR:
			for (int n = 0; n < ini_vars[i].min; n++)
			{
				char *str = ((char*)ini_vars[i].var) + (n*ini_vars[i].max);
				if (!strlen(str)) break;
				printf("  %s=%s\n", ini_vars[i].name, str);
			}
			break;
		}
	}
	printf("--------------\n");
}

static int yc_parse_mode(char* buf, yc_mode *mode)
{
	int i = 0;
	while (1)
	{
		if (buf[i] == '=' || CHAR_IS_LINEEND(buf[i]))
		{
			buf[i] = 0;
			break;
		}
		else if (!buf[i]) return 0;
		i++;
	}

	i++;
	while (buf[i] == '=' || CHAR_IS_SPACE(buf[i])) i++;
	ini_parser_debugf("Got yc_mode '%s' with VALUE %s", buf, buf + i);

	snprintf(mode->key, sizeof(mode->key), "%s", buf);
	mode->phase_inc = strtoull(buf + i, 0, 0);
	if (!mode->phase_inc)
	{
		printf("ERROR: cannot parse YC phase_inc: '%s'\n", buf + i);
		return 0;
	}

	return 1;
}

void yc_parse(yc_mode *yc_table, int max)
{
	memset(yc_table, 0, max * sizeof(yc_mode));

	static char line[INI_LINE_SIZE];
	int eof;

	memset(line, 0, sizeof(line));
	memset(&ini_file, 0, sizeof(ini_file));

	const char *corename = user_io_get_core_name(1);
	int corename_len = strlen(corename);

	const char *name = "yc.txt";
	if (!FileOpen(&ini_file, name))	return;

	ini_parser_debugf("Opened file %s with size %llu bytes.", name, ini_file.size);

	ini_pt = 0;
	int n = 0;

	while (n < max)
	{
		// get line
		eof = ini_getline(line);
		if (!strncasecmp(line, corename, corename_len))
		{
			int res = yc_parse_mode(line, &yc_table[n]);
			if (res) n++;
		}

		// if end of file, stop
		if (eof) break;
	}

	FileClose(&ini_file);
}
