// cfg.c
// 2015, rok.krajnc@gmail.com
// 2017+, Sorgelig

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include "cfg.h"
#include "debug.h"
#include "file_io.h"
#include "user_io.h"

cfg_t cfg;

typedef enum
{
	UINT8 = 0, INT8, UINT16, INT16, UINT32, INT32, FLOAT, STRING
} ini_vartypes_t;

typedef struct
{
	const char* name;
	void* var;
	ini_vartypes_t type;
	int min;
	int max;
} ini_var_t;

static const ini_var_t ini_vars[] =
{
	{ "YPBPR", (void*)(&(cfg.ypbpr)), UINT8, 0, 1 },
	{ "COMPOSITE_SYNC", (void*)(&(cfg.csync)), UINT8, 0, 1 },
	{ "FORCED_SCANDOUBLER", (void*)(&(cfg.forced_scandoubler)), UINT8, 0, 1 },
	{ "VGA_SCALER", (void*)(&(cfg.vga_scaler)), UINT8, 0, 1 },
	{ "VGA_SOG", (void*)(&(cfg.vga_sog)), UINT8, 0, 1 },
	{ "KEYRAH_MODE", (void*)(&(cfg.keyrah_mode)), UINT32, 0, (int)0xFFFFFFFF },
	{ "RESET_COMBO", (void*)(&(cfg.reset_combo)), UINT8, 0, 3 },
	{ "KEY_MENU_AS_RGUI", (void*)(&(cfg.key_menu_as_rgui)), UINT8, 0, 1 },
	{ "VIDEO_MODE", (void*)(cfg.video_conf), STRING, 0, sizeof(cfg.video_conf) - 1 },
	{ "VIDEO_MODE_PAL", (void*)(cfg.video_conf_pal), STRING, 0, sizeof(cfg.video_conf_pal) - 1 },
	{ "VIDEO_MODE_NTSC", (void*)(cfg.video_conf_ntsc), STRING, 0, sizeof(cfg.video_conf_ntsc) - 1 },
	{ "VIDEO_INFO", (void*)(&(cfg.video_info)), UINT8, 0, 10 },
	{ "VSYNC_ADJUST", (void*)(&(cfg.vsync_adjust)), UINT8, 0, 2 },
	{ "HDMI_AUDIO_96K", (void*)(&(cfg.hdmi_audio_96k)), UINT8, 0, 1 },
	{ "DVI_MODE", (void*)(&(cfg.dvi)), UINT8, 0, 1 },
	{ "HDMI_LIMITED", (void*)(&(cfg.hdmi_limited)), UINT8, 0, 2 },
	{ "KBD_NOMOUSE", (void*)(&(cfg.kbd_nomouse)), UINT8, 0, 1 },
	{ "MOUSE_THROTTLE", (void*)(&(cfg.mouse_throttle)), UINT8, 1, 100 },
	{ "BOOTSCREEN", (void*)(&(cfg.bootscreen)), UINT8, 0, 1 },
	{ "VSCALE_MODE", (void*)(&(cfg.vscale_mode)), UINT8, 0, 3 },
	{ "VSCALE_BORDER", (void*)(&(cfg.vscale_border)), UINT16, 0, 399 },
	{ "RBF_HIDE_DATECODE", (void*)(&(cfg.rbf_hide_datecode)), UINT8, 0, 1 },
	{ "MENU_PAL", (void*)(&(cfg.menu_pal)), UINT8, 0, 1 },
	{ "BOOTCORE", (void*)(&(cfg.bootcore)), STRING, 0, sizeof(cfg.bootcore) - 1 },
	{ "BOOTCORE_TIMEOUT", (void*)(&(cfg.bootcore_timeout)), INT16, 2, 30 },
	{ "FONT", (void*)(&(cfg.font)), STRING, 0, sizeof(cfg.font) - 1 },
	{ "FB_SIZE", (void*)(&(cfg.fb_size)), UINT8, 0, 4 },
	{ "FB_TERMINAL", (void*)(&(cfg.fb_terminal)), UINT8, 0, 1 },
	{ "OSD_TIMEOUT", (void*)(&(cfg.osd_timeout)), INT16, 5, 3600 },
	{ "DIRECT_VIDEO", (void*)(&(cfg.direct_video)), UINT8, 0, 1 },
	{ "OSD_ROTATE", (void*)(&(cfg.osd_rotate)), UINT8, 0, 2 },
	{ "GAMEPAD_DEFAULTS", (void*)(&(cfg.gamepad_defaults)), UINT8, 0, 1 },
	{ "RECENTS", (void*)(&(cfg.recents)), UINT8, 0, 1 },
	{ "CONTROLLER_INFO", (void*)(&(cfg.controller_info)), UINT8, 0, 10 },
	{ "REFRESH_MIN", (void*)(&(cfg.refresh_min)), UINT8, 0, 150 },
	{ "REFRESH_MAX", (void*)(&(cfg.refresh_max)), UINT8, 0, 150 },
	{ "JAMMA_VID", (void*)(&(cfg.jamma_vid)), UINT16, 0, 0xFFFF },
	{ "JAMMA_PID", (void*)(&(cfg.jamma_pid)), UINT16, 0, 0xFFFF },
	{ "SNIPER_MODE", (void*)(&(cfg.sniper_mode)), UINT8, 0, 1 },
	{ "BROWSE_EXPAND", (void*)(&(cfg.browse_expand)), UINT8, 0, 1 },
	{ "LOGO", (void*)(&(cfg.logo)), UINT8, 0, 1 },
	{ "SHARED_FOLDER", (void*)(&(cfg.shared_folder)), STRING, 0, sizeof(cfg.shared_folder) - 1 },
	{ "NO_MERGE_VID", (void*)(&(cfg.no_merge_vid)), UINT16, 0, 0xFFFF },
	{ "NO_MERGE_PID", (void*)(&(cfg.no_merge_pid)), UINT16, 0, 0xFFFF },
	{ "CUSTOM_ASPECT_RATIO_1", (void*)(&(cfg.custom_aspect_ratio[0])), STRING, 0, sizeof(cfg.custom_aspect_ratio[0]) - 1 },
	{ "CUSTOM_ASPECT_RATIO_2", (void*)(&(cfg.custom_aspect_ratio[1])), STRING, 0, sizeof(cfg.custom_aspect_ratio[1]) - 1 },
	{ "SPINNER_VID", (void*)(&(cfg.spinner_vid)), UINT16, 0, 0xFFFF },
	{ "SPINNER_PID", (void*)(&(cfg.spinner_pid)), UINT16, 0, 0xFFFF },
	{ "SPINNER_THROTTLE", (void*)(&(cfg.spinner_throttle)), INT32, -10000, 10000 },
	{ "AFILTER_DEFAULT", (void*)(&(cfg.afilter_default)), STRING, 0, sizeof(cfg.afilter_default) - 1 },
	{ "VFILTER_DEFAULT", (void*)(&(cfg.vfilter_default)), STRING, 0, sizeof(cfg.vfilter_default) - 1 },
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
                                 ((c) == ':'))

#define CHAR_IS_VALID(c)        (CHAR_IS_ALPHANUM(c) || CHAR_IS_SPECIAL(c))
#define CHAR_IS_SPACE(c)        (((c) == ' ') || ((c) == '\t'))
#define CHAR_IS_LINEEND(c)      (((c) == '\n'))
#define CHAR_IS_COMMENT(c)      (((c) == ';'))
#define CHAR_IS_QUOTE(c)        (((c) == '"'))


fileTYPE ini_file;

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

static int ini_get_section(char* buf)
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

	// get section stop marker
	while (buf[i])
	{
		if (buf[i] == INI_SECTION_END)
		{
			buf[i] = 0;
			break;
		}

		if (buf[i] == '*') wc_pos = i;

		i++;
		if (i >= INI_LINE_SIZE) return 0;
	}

	if (!strcasecmp(buf, "MiSTer") ||
		(is_arcade() && !strcasecmp(buf, "arcade")) ||
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

	return 0;
}

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

	// get data
	if (var_id != -1)
	{
		i++;
		while (buf[i] == '=' || CHAR_IS_SPACE(buf[i])) i++;
		ini_parser_debugf("Got VAR '%s' with VALUE %s", buf, buf+i);

		switch (ini_vars[var_id].type)
		{
		case UINT8:
			*(uint8_t*)(ini_vars[var_id].var) = strtoul(&(buf[i]), NULL, 0);
			if (*(uint8_t*)(ini_vars[var_id].var) > ini_vars[var_id].max) *(uint8_t*)(ini_vars[var_id].var) = ini_vars[var_id].max;
			if (*(uint8_t*)(ini_vars[var_id].var) < ini_vars[var_id].min) *(uint8_t*)(ini_vars[var_id].var) = ini_vars[var_id].min;
			break;
		case INT8:
			*(int8_t*)(ini_vars[var_id].var) = strtol(&(buf[i]), NULL, 0);
			if (*(int8_t*)(ini_vars[var_id].var) > ini_vars[var_id].max) *(int8_t*)(ini_vars[var_id].var) = ini_vars[var_id].max;
			if (*(int8_t*)(ini_vars[var_id].var) < ini_vars[var_id].min) *(int8_t*)(ini_vars[var_id].var) = ini_vars[var_id].min;
			break;
		case UINT16:
			*(uint16_t*)(ini_vars[var_id].var) = strtoul(&(buf[i]), NULL, 0);
			if (*(uint16_t*)(ini_vars[var_id].var) > ini_vars[var_id].max) *(uint16_t*)(ini_vars[var_id].var) = ini_vars[var_id].max;
			if (*(uint16_t*)(ini_vars[var_id].var) < ini_vars[var_id].min) *(uint16_t*)(ini_vars[var_id].var) = ini_vars[var_id].min;
			break;
		case INT16:
			*(int16_t*)(ini_vars[var_id].var) = strtol(&(buf[i]), NULL, 0);
			if (*(int16_t*)(ini_vars[var_id].var) > ini_vars[var_id].max) *(int16_t*)(ini_vars[var_id].var) = ini_vars[var_id].max;
			if (*(int16_t*)(ini_vars[var_id].var) < ini_vars[var_id].min) *(int16_t*)(ini_vars[var_id].var) = ini_vars[var_id].min;
			break;
		case UINT32:
			*(uint32_t*)(ini_vars[var_id].var) = strtoul(&(buf[i]), NULL, 0);
			if (*(uint32_t*)(ini_vars[var_id].var) > (uint32_t)ini_vars[var_id].max) *(uint32_t*)(ini_vars[var_id].var) = ini_vars[var_id].max;
			if (*(uint32_t*)(ini_vars[var_id].var) < (uint32_t)ini_vars[var_id].min) *(uint32_t*)(ini_vars[var_id].var) = ini_vars[var_id].min;
			break;
		case INT32:
			*(int32_t*)(ini_vars[var_id].var) = strtol(&(buf[i]), NULL, 0);
			if (*(int32_t*)(ini_vars[var_id].var) > ini_vars[var_id].max) *(int32_t*)(ini_vars[var_id].var) = ini_vars[var_id].max;
			if (*(int32_t*)(ini_vars[var_id].var) < ini_vars[var_id].min) *(int32_t*)(ini_vars[var_id].var) = ini_vars[var_id].min;
			break;
		case FLOAT:
			*(float*)(ini_vars[var_id].var) = strtof(&(buf[i]), NULL);
			if (*(float*)(ini_vars[var_id].var) > ini_vars[var_id].max) *(float*)(ini_vars[var_id].var) = ini_vars[var_id].max;
			if (*(float*)(ini_vars[var_id].var) < ini_vars[var_id].min) *(float*)(ini_vars[var_id].var) = ini_vars[var_id].min;
			break;
		case STRING:
			memset(ini_vars[var_id].var, 0, ini_vars[var_id].max);
			strncpy((char*)(ini_vars[var_id].var), &(buf[i]), ini_vars[var_id].max);
			break;
		}
	}
}

static void ini_parse(int alt)
{
	static char line[INI_LINE_SIZE];
	int section = 0;
	int eof;

	ini_parser_debugf("Start INI parser for core \"%s\"(%s).", user_io_get_core_name(0), user_io_get_core_name(1));

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
			section = ini_get_section(line);
		}
		else if (line[0] == INCL_SECTION && !section)
		{
			section = ini_get_section(line);
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

const char* cfg_get_name(uint8_t alt)
{
	static char name[64];
	strcpy(name, "MiSTer.ini");

	if (alt == 1)
	{
		strcpy(name, "MiSTer_alt_1.ini");
		if (FileExists(name)) return name;
		return "MiSTer_alt.ini";
	}

	if (alt && alt < 4) sprintf(name, "MiSTer_alt_%d.ini", alt);
	return name;
}

void cfg_parse()
{
	memset(&cfg, 0, sizeof(cfg));
	cfg.bootscreen = 1;
	cfg.fb_terminal = 1;
	cfg.controller_info = 6;
	cfg.browse_expand = 1;
	cfg.logo = 1;
	ini_parse(altcfg());
}
