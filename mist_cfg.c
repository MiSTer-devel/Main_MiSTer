// mist_cfg.c
// 2015, rok.krajnc@gmail.com


#include <string.h>
#include "ini_parser.h"
#include "mist_cfg.h"
#include "user_io.h"

mist_cfg_t mist_cfg;

void mist_ini_parse()
{
	memset(&mist_cfg, 0, sizeof(mist_cfg));
	ini_parse(&mist_ini_cfg);
}

// mist ini sections
const ini_section_t mist_ini_sections[] =
{
	{ 1, "MiSTer" }
};

// mist ini vars
const ini_var_t mist_ini_vars[] = {
	{ "YPBPR", (void*)(&(mist_cfg.ypbpr)), UINT8, 0, 1, 1 },
	{ "COMPOSITE_SYNC", (void*)(&(mist_cfg.csync)), UINT8, 0, 1, 1 },
	{ "FORCED_SCANDOUBLER", (void*)(&(mist_cfg.forced_scandoubler)), UINT8, 0, 1, 1 },
	{ "VGA_SCALER", (void*)(&(mist_cfg.vga_scaler)), UINT8, 0, 1, 1 },
	{ "KEYRAH_MODE", (void*)(&(mist_cfg.keyrah_mode)), UINT32, 0, 0xFFFFFFFF, 1 },
	{ "RESET_COMBO", (void*)(&(mist_cfg.reset_combo)), UINT8, 0, 3, 1 },
	{ "KEY_MENU_AS_RGUI", (void*)(&(mist_cfg.key_menu_as_rgui)), UINT8, 0, 1, 1 },
	{ "VIDEO_MODE", (void*)(mist_cfg.video_conf), STRING, 0, sizeof(mist_cfg.video_conf)-1, 1 },
	{ "HDMI_AUDIO_96K", (void*)(&(mist_cfg.hdmi_audio_96k)), UINT8, 0, 1, 1 },
	{ "DVI_MODE", (void*)(&(mist_cfg.dvi)), UINT8, 0, 1, 1 },
	{ "KBD_NOMOUSE", (void*)(&(mist_cfg.kbd_nomouse)), UINT8, 0, 1, 1 },
};

// mist ini config
const ini_cfg_t mist_ini_cfg = {
	CONFIG_DIR"/MiSTer.ini",
	mist_ini_sections,
	mist_ini_vars,
	(int)(sizeof(mist_ini_sections) / sizeof(ini_section_t)),
	(int)(sizeof(mist_ini_vars) / sizeof(ini_var_t))
};
