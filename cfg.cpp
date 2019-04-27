// cfg.c
// 2015, rok.krajnc@gmail.com
// 2017+, Sorgelig

#include <string.h>
#include "ini_parser.h"
#include "cfg.h"
#include "user_io.h"

cfg_t cfg;

void MiSTer_ini_parse()
{
	memset(&cfg, 0, sizeof(cfg));
	cfg.bootscreen = 1;
	ini_parse(&ini_cfg);
}

// mist ini sections
const ini_section_t ini_sections[] =
{
	{ 1, "MiSTer" }
};

// mist ini vars
const ini_var_t ini_vars[] = {
	{ "YPBPR", (void*)(&(cfg.ypbpr)), UINT8, 0, 1, 1 },
	{ "COMPOSITE_SYNC", (void*)(&(cfg.csync)), UINT8, 0, 1, 1 },
	{ "FORCED_SCANDOUBLER", (void*)(&(cfg.forced_scandoubler)), UINT8, 0, 1, 1 },
	{ "VGA_SCALER", (void*)(&(cfg.vga_scaler)), UINT8, 0, 1, 1 },
	{ "KEYRAH_MODE", (void*)(&(cfg.keyrah_mode)), UINT32, 0, (int)0xFFFFFFFF, 1 },
	{ "RESET_COMBO", (void*)(&(cfg.reset_combo)), UINT8, 0, 3, 1 },
	{ "KEY_MENU_AS_RGUI", (void*)(&(cfg.key_menu_as_rgui)), UINT8, 0, 1, 1 },
	{ "VIDEO_MODE", (void*)(cfg.video_conf), STRING, 0, sizeof(cfg.video_conf)-1, 1 },
	{ "VIDEO_MODE_PAL", (void*)(cfg.video_conf_pal), STRING, 0, sizeof(cfg.video_conf_pal) - 1, 1 },
	{ "VIDEO_MODE_NTSC", (void*)(cfg.video_conf_ntsc), STRING, 0, sizeof(cfg.video_conf_ntsc) - 1, 1 },
	{ "VIDEO_INFO", (void*)(&(cfg.video_info)), UINT8, 0, 10, 1 },
	{ "VSYNC_ADJUST", (void*)(&(cfg.vsync_adjust)), UINT8, 0, 2, 1 },
	{ "HDMI_AUDIO_96K", (void*)(&(cfg.hdmi_audio_96k)), UINT8, 0, 1, 1 },
	{ "DVI_MODE", (void*)(&(cfg.dvi)), UINT8, 0, 1, 1 },
	{ "KBD_NOMOUSE", (void*)(&(cfg.kbd_nomouse)), UINT8, 0, 1, 1 },
	{ "MOUSE_THROTTLE", (void*)(&(cfg.mouse_throttle)), UINT8, 1, 100, 1 },
	{ "BOOTSCREEN", (void*)(&(cfg.bootscreen)), UINT8, 0, 1, 1 },
	{ "VOLUMECTL", (void*)(&(cfg.volumectl)), UINT8, 0, 1, 1 },
	{ "VSCALE_MODE", (void*)(&(cfg.vscale_mode)), UINT8, 0, 3, 1 },
	{ "VSCALE_BORDER", (void*)(&(cfg.vscale_border)), UINT8, 0, 100, 1 },
	{ "RBF_HIDE_DATECODE", (void*)(&(cfg.rbf_hide_datecode)), UINT8, 0, 1, 1 },
	{ "MENU_PAL", (void*)(&(cfg.menu_pal)), UINT8, 0, 1, 1 },
	{ "BOOTCORE", (void*)(&(cfg.bootcore)), STRING, 0, sizeof(cfg.bootcore) - 1, 1 },
	{ "BOOTCORE_TIMEOUT", (void*)(&(cfg.bootcore_timeout)), INT16, 10, 30, 1 },
	{ "FONT", (void*)(&(cfg.font)), STRING, 0, sizeof(cfg.font) - 1, 1 },
};

// mist ini config
const ini_cfg_t ini_cfg = {
	"MiSTer.ini",
	CONFIG_DIR"/MiSTer.ini",
	ini_sections,
	ini_vars,
	(int)(sizeof(ini_sections) / sizeof(ini_section_t)),
	(int)(sizeof(ini_vars) / sizeof(ini_var_t))
};
