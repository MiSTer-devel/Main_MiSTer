
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

#define NUMBUTTONS         32
#define BUTTON_DPAD_COUNT  12 // dpad + 8 buttons

#define SYS_BTN_RIGHT       0
#define SYS_BTN_LEFT        1
#define SYS_BTN_DOWN        2
#define SYS_BTN_UP          3
#define SYS_BTN_A           4
#define SYS_BTN_B           5
#define SYS_BTN_X           6
#define SYS_BTN_Y           7
#define SYS_BTN_L           8
#define SYS_BTN_R           9
#define SYS_BTN_SELECT     10
#define SYS_BTN_START      11
#define SYS_MS_RIGHT       12
#define SYS_MS_LEFT        13
#define SYS_MS_DOWN        14
#define SYS_MS_UP          15
#define SYS_MS_BTN_L       16
#define SYS_MS_BTN_R       17
#define SYS_MS_BTN_M       18
#define SYS_MS_BTN_EMU     19
#define SYS_BTN_OSD_KTGL   20 // 20 for keyboard, 21+22 for gamepad
#define SYS_AXIS1_X        24
#define SYS_AXIS1_Y        25
#define SYS_AXIS2_X        26
#define SYS_AXIS2_Y        27
#define SYS_AXIS_X         28
#define SYS_AXIS_Y         29
#define SYS_AXIS_MX        30
#define SYS_AXIS_MY        31

#define KEY_EMU (KEY_MAX+1)

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
int get_map_cancel();
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

extern char joy_bnames[NUMBUTTONS][32];
extern int  joy_bcount;

#endif
