
#ifndef EVINPUT_H
#define EVINPUT_H

#include <linux/input.h>

#define HID_LED_NUM_LOCK    1
#define HID_LED_CAPS_LOCK   2
#define HID_LED_SCROLL_LOCK 4
#define HID_LED_MASK        7

#define NONE         0xFF
#define LCTRL        0x000100
#define LSHIFT       0x000200
#define LALT         0x000400
#define LGUI         0x000800
#define RCTRL        0x001000
#define RSHIFT       0x002000
#define RALT         0x004000
#define RGUI         0x008000
#define MODMASK      0x00FF00

#define OSD          0x010000  // to be used by OSD, not the core itself
#define OSD_OPEN     0x020000  // OSD key not forwarded to core, but queued in arm controller
#define CAPS_TOGGLE  0x040000  // caps lock toggle behaviour
#define EXT          0x080000
#define EMU_SWITCH_1 0x100000
#define EMU_SWITCH_2 0x200000

#define UPSTROKE     0x400000

void set_kbdled(int mask, int state);
int  get_kbdled(int mask);
int  toggle_kbdled(int mask);

void input_notify_mode();
int input_poll(int getchar);
int is_key_pressed(int key);

void start_map_setting(int cnt, int set = 0);
int get_map_button();
int get_map_type();
int get_map_clear();
void finish_map_setting(int dismiss);
uint16_t get_map_vid();
uint16_t get_map_pid();
int has_default_map();

uint32_t get_key_mod();
uint32_t get_ps2_code(uint16_t key);
uint32_t get_amiga_code(uint16_t key);
uint32_t get_atari_code(uint16_t key);
uint32_t get_archie_code(uint16_t key);

int input_has_lightgun();
void input_lightgun_cal(uint16_t *cal);

void input_switch(int grab);
int input_state();
void input_uinp_destroy();

#endif
