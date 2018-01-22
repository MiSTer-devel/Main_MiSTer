// cfg.h
// 2015, rok.krajnc@gmail.com
// 2017+, Sorgelig

#ifndef __CFG_H__
#define __CFG_H__


//// includes ////
#include <inttypes.h>
#include "ini_parser.h"


//// type definitions ////
typedef struct {
	uint32_t keyrah_mode;
	uint8_t forced_scandoubler;
	uint8_t key_menu_as_rgui;
	uint8_t reset_combo;
	uint8_t ypbpr;
	uint8_t csync;
	uint8_t vga_scaler;
	uint8_t hdmi_audio_96k;
	uint8_t dvi;
	uint8_t video_mode;
	uint8_t kbd_nomouse;
	uint8_t mouse_throttle;
	char video_conf[1024];
} cfg_t;

//// functions ////
void MiSTer_ini_parse();

//// global variables ////
extern const ini_cfg_t ini_cfg;
extern cfg_t cfg;


#endif // __CFG_H__
