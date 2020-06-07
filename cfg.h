// cfg.h
// 2015, rok.krajnc@gmail.com
// 2017+, Sorgelig

#ifndef __CFG_H__
#define __CFG_H__

#include <inttypes.h>

//// type definitions ////
typedef struct {
	uint32_t keyrah_mode;
	uint8_t forced_scandoubler;
	uint8_t key_menu_as_rgui;
	uint8_t reset_combo;
	uint8_t ypbpr;
	uint8_t csync;
	uint8_t vga_scaler;
	uint8_t vga_sog;
	uint8_t hdmi_audio_96k;
	uint8_t dvi;
	uint8_t hdmi_limited;
	uint8_t direct_video;
	uint8_t video_info;
	uint8_t refresh_min;
	uint8_t refresh_max;
	uint8_t controller_info;
	uint8_t vsync_adjust;
	uint8_t kbd_nomouse;
	uint8_t mouse_throttle;
	uint8_t bootscreen;
	uint8_t vscale_mode;
	uint16_t vscale_border;
	uint8_t rbf_hide_datecode;
	uint8_t menu_pal;
	int16_t bootcore_timeout;
	uint8_t fb_size;
	uint8_t fb_terminal;
	uint8_t osd_rotate;
	uint16_t osd_timeout;
	uint8_t gamepad_defaults;
	uint8_t recents;
	uint16_t jammasd_vid;
	uint16_t jammasd_pid;
	uint16_t jpac_vid;
	uint16_t jpac_pid;
	uint8_t sniper_mode;
	uint8_t browse_expand;
	char bootcore[256];
	char video_conf[1024];
	char video_conf_pal[1024];
	char video_conf_ntsc[1024];
	char font[1024];
} cfg_t;

extern cfg_t cfg;

//// functions ////
void cfg_parse();
const char* cfg_get_name(uint8_t alt);

#endif // __CFG_H__
