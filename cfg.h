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
	uint8_t dvi_mode;
	uint8_t hdmi_limited;
	uint8_t direct_video;
	uint8_t video_info;
	float refresh_min;
	float refresh_max;
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
	uint16_t jamma_vid;
	uint16_t jamma_pid;
	uint16_t no_merge_vid;
	uint16_t no_merge_pid;
	uint32_t no_merge_vidpid[256];
	uint16_t spinner_vid;
	uint16_t spinner_pid;
	int spinner_throttle;
	uint8_t spinner_axis;
	uint8_t sniper_mode;
	uint8_t browse_expand;
	uint8_t logo;
	uint8_t log_file_entry;
	uint8_t shmask_mode_default;
	int bt_auto_disconnect;
	int bt_reset_before_pair;
	char bootcore[256];
	char video_conf[1024];
	char video_conf_pal[1024];
	char video_conf_ntsc[1024];
	char font[1024];
	char shared_folder[1024];
	char waitmount[1024];
	char custom_aspect_ratio[2][16];
	char afilter_default[1023];
	char vfilter_default[1023];
	char vfilter_vertical_default[1023];
	char vfilter_scanlines_default[1023];
	char shmask_default[1023];
	uint8_t rumble;
	uint8_t wheel_force;
	uint16_t wheel_range;
	uint8_t hdmi_game_mode;
	uint8_t vrr_mode;
	uint8_t vrr_min_framerate;
	uint8_t vrr_max_framerate;
	uint8_t vrr_vesa_framerate;
	uint16_t video_off;
} cfg_t;

extern cfg_t cfg;

//// functions ////
void cfg_parse();
const char* cfg_get_name(uint8_t alt);
bool cfg_has_video_sections();

void cfg_error(const char *fmt, ...);
bool cfg_check_errors(char *msg, size_t max_len);

#endif // __CFG_H__
