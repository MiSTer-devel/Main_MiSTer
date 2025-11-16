
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

#define NUMPLAYERS          6

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
#define SYS_BTN_MENU_FUNC  23
#define SYS_AXIS1_X        24
#define SYS_AXIS1_Y        25
#define SYS_AXIS2_X        26
#define SYS_AXIS2_Y        27
#define SYS_AXIS_X         28
#define SYS_AXIS_Y         29
#define SYS_AXIS_MX        30
#define SYS_AXIS_MY        31

#define SYS_BTN_CNT_OK     21
#define SYS_BTN_CNT_ESC    22

#define SPIN_LEFT          30
#define SPIN_RIGHT         31

#define KEY_EMU (KEY_MAX+1)


#define ADVANCED_MAP_MAX 32 


typedef struct {
        uint32_t button_mask;
				uint16_t output_codes[4];
        uint16_t input_codes[4];
} advancedButtonMap;


typedef struct {

	uint8_t input_state;
	uint32_t current_mask;
	uint32_t input_btn_mask;
	uint8_t pressed : 1;
	uint8_t last_pressed : 1;
	uint8_t autofire : 1;
} advancedButtonState;


void set_kbdled(int mask, int state);
int  get_kbdled(int mask);
int  toggle_kbdled(int mask);
void sysled_enable(int en);

void input_notify_mode();
int input_poll(int getchar);
int is_key_pressed(int key);

void start_map_setting(int cnt, int set = 0, advancedButtonMap *code_store = NULL);
int get_map_set();
int get_map_button();
int get_map_type();
int get_map_clear();
int get_map_cancel();
int get_map_finish();
void finish_map_setting(int dismiss);
uint16_t get_map_vid();
uint16_t get_map_pid();
int get_map_dev();
advancedButtonMap *get_map_code_store();
int get_map_advance();
int get_map_count();
int has_default_map();
void send_map_cmd(int key);
void reset_players();

uint32_t get_key_mod();
uint32_t get_ps2_code(uint16_t key);
uint32_t get_amiga_code(uint16_t key);
uint32_t get_archie_code(uint16_t key);

int input_has_lightgun();
void input_lightgun_save(int idx, int32_t *cal);

void input_switch(int grab);
int input_state();
void input_uinp_destroy();

extern char joy_bnames[NUMBUTTONS][32];
extern int  joy_bcount;
extern uint8_t ps2_kbd_scan_set;

void parse_buttons();
char *get_buttons(int type = 0);
void set_ovr_buttons(char *s, int type);

#define FOR_EACH_SET_BIT(mask, bit)                     \
    for (uint32_t _m = (mask); _m; _m &= (_m - 1))      \
        for (int bit = __builtin_ctz(_m), _once = 1; _once; _once = 0)
void start_code_capture(int dnum);
void end_code_capture();
uint16_t get_captured_code();
int code_capture_osd_count();
int get_last_input_dev();
int get_dev_num(int dev);
advancedButtonMap *get_advanced_map_defs(int devnum);
void get_button_name_for_code(uint16_t btn_code, int devnum, char *bname, size_t bname_sz);
void input_advanced_save(int dev_num, bool do_delete=false);
void input_advanced_load(int dev_num);
void input_advanced_save_entry(advancedButtonMap *abm_entry, int devnum);
void input_advanced_clear(int devnum);
void input_advanced_delete(advancedButtonMap *todel, int devnum);

#endif
