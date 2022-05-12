/*
* user_io.h
*
*/

#ifndef USER_IO_H
#define USER_IO_H

#include <inttypes.h>
#include "file_io.h"

#define UIO_STATUS      0x00
#define UIO_BUT_SW      0x01

// codes as used by minimig (amiga)
#define UIO_JOYSTICK0   0x02  // also used by 8 bit
#define UIO_JOYSTICK1   0x03  // -"-
#define UIO_MOUSE       0x04  // -"-
#define UIO_KEYBOARD    0x05  // -"-
#define UIO_KBD_OSD     0x06  // keycodes used by OSD only

// 0x08 - 0x0F - core specific

#define UIO_JOYSTICK2   0x10  // also used by minimig and 8 bit
#define UIO_JOYSTICK3   0x11  // -"-
#define UIO_JOYSTICK4   0x12  // -"-
#define UIO_JOYSTICK5   0x13  // -"-

// codes as currently used by 8bit only
#define UIO_GET_STRING  0x14
#define UIO_SET_STATUS  0x15
#define UIO_GET_SDSTAT  0x16  // read status of sd card emulation
#define UIO_SECTOR_RD   0x17  // SD card sector read
#define UIO_SECTOR_WR   0x18  // SD card sector write
#define UIO_SET_SDCONF  0x19  // send SD card configuration (CSD, CID)
#define UIO_ASTICK      0x1a
#define UIO_SIO_IN      0x1b  // serial in
#define UIO_SET_SDSTAT  0x1c  // set sd card status
#define UIO_SET_SDINFO  0x1d  // send info about mounted image
#define UIO_SET_STATUS2 0x1e  // 32bit status
#define UIO_GET_KBD_LED 0x1f  // keyboard LEDs control
#define UIO_SET_VIDEO   0x20
#define UIO_PS2_CTL     0x21  // get PS2 control from supported cores
#define UIO_RTC         0x22  // transmit RTC data to core
#define UIO_GET_VRES    0x23  // get video resolution
#define UIO_TIMESTAMP   0x24  // transmit seconds since Unix epoch
#define UIO_LEDS        0x25  // control on-board LEDs
#define UIO_AUDVOL      0x26  // Digital volume as a number of bits to shift to the right
#define UIO_SETHEIGHT   0x27  // Set max scaled vertical resolution
#define UIO_GETUARTFLG  0x28  // Get UART_FLG_*
#define UIO_GET_STATUS  0x29  // Update status from the core
#define UIO_SET_FLTCOEF 0x2A  // Set Scaler polyphase coefficients
#define UIO_SET_FLTNUM  0x2B  // Set Scaler predefined filter
#define UIO_GET_VMODE   0x2C  // Get video mode parameters
#define UIO_SET_VPOS    0x2D  // Set video positions
#define UIO_GET_OSDMASK 0x2E  // Get mask
#define UIO_SET_FBUF    0x2F  // Set frame buffer for HPS output
#define UIO_WAIT_VSYNC  0x30  // Wait for VSync
#define UIO_SET_MEMSZ   0x31  // Send memory size to the core
#define UIO_SET_GAMMA   0x32  // Enable/disable Gamma correction
#define UIO_SET_GAMCURV 0x33  // Set Gamma curve
#define UIO_CD_GET      0x34
#define UIO_CD_SET      0x35
#define UIO_INFO_GET    0x36
#define UIO_SETWIDTH    0x37  // Set max scaled horizontal resolution
#define UIO_SETSYNC     0x38
#define UIO_SET_AFILTER 0x39
#define UIO_SET_AR_CUST 0x3A
#define UIO_SET_UART    0x3B
#define UIO_CHK_UPLOAD  0x3C
#define UIO_ASTICK_2    0x3D
#define UIO_SHADOWMASK  0x3E
#define UIO_GET_RUMBLE  0x3F

// codes as used by 8bit for file loading from OSD
#define FIO_FILE_TX     0x53
#define FIO_FILE_TX_DAT 0x54
#define FIO_FILE_INDEX  0x55
#define FIO_FILE_INFO   0x56

// ao486 direct memory access
#define UIO_DMA_WRITE   0x61
#define UIO_DMA_READ    0x62
#define UIO_DMA_SDIO    0x63

// ---- Minimig v2 constants -------
#define UIO_MM2_WR      0xF0 //0x1c
#define UIO_MM2_RST     0xF1 //0x08
#define UIO_MM2_AUD     0xF2 //0x74
#define UIO_MM2_CHIP    0xF3 //0x04
#define UIO_MM2_CPU     0xF4 //0x14
#define UIO_MM2_MEM     0xF5 //0x24
#define UIO_MM2_VID     0xF6 //0x34
#define UIO_MM2_FLP     0xF7 //0x44
#define UIO_MM2_HDD     0xF8 //0x54
#define UIO_MM2_JOY     0xF9 //0x64

#define JOY_RIGHT       0x01
#define JOY_LEFT        0x02
#define JOY_DOWN        0x04
#define JOY_UP          0x08
#define JOY_BTN_SHIFT   4
#define JOY_BTN1        0x10
#define JOY_BTN2        0x20
#define JOY_BTN3        0x40
#define JOY_BTN4        0x80
#define JOY_MOVE        (JOY_RIGHT|JOY_LEFT|JOY_UP|JOY_DOWN)

// virtual gamepad buttons
#define JOY_A      JOY_BTN1
#define JOY_B      JOY_BTN2
#define JOY_SELECT JOY_BTN3
#define JOY_START  JOY_BTN4
#define JOY_X      0x100
#define JOY_Y      0x200
#define JOY_L      0x400
#define JOY_R      0x800
#define JOY_L2     0x1000
#define JOY_R2     0x2000
#define JOY_L3     0x4000
#define JOY_R3     0x8000

// keyboard LEDs control
#define KBD_LED_CAPS_CONTROL  0x01
#define KBD_LED_CAPS_STATUS   0x02
#define KBD_LED_CAPS_MASK     (KBD_LED_CAPS_CONTROL | KBD_LED_CAPS_STATUS)
#define KBD_LED_NUM_CONTROL   0x04
#define KBD_LED_NUM_STATUS    0x08
#define KBD_LED_NUM_MASK      (KBD_LED_NUM_CONTROL | KBD_LED_NUM_STATUS)
#define KBD_LED_SCRL_CONTROL  0x10
#define KBD_LED_SCRL_STATUS   0x20
#define KBD_LED_SCRL_MASK     (KBD_LED_SCRL_CONTROL | KBD_LED_SCRL_STATUS)
#define KBD_LED_FLAG_MASK     0xC0
#define KBD_LED_FLAG_STATUS   0x40

#define BUTTON1                 0b0000000000000001
#define BUTTON2                 0b0000000000000010
#define CONF_VGA_SCALER         0b0000000000000100
#define CONF_CSYNC              0b0000000000001000
#define CONF_FORCED_SCANDOUBLER 0b0000000000010000
#define CONF_YPBPR              0b0000000000100000
#define CONF_AUDIO_96K          0b0000000001000000
#define CONF_DVI                0b0000000010000000
#define CONF_HDMI_LIMITED1      0b0000000100000000
#define CONF_VGA_SOG            0b0000001000000000
#define CONF_DIRECT_VIDEO       0b0000010000000000
#define CONF_HDMI_LIMITED2      0b0000100000000000
#define CONF_VGA_FB             0b0001000000000000

// core type value should be unlikely to be returned by broken cores
#define CORE_TYPE_UNKNOWN   0x55
#define CORE_TYPE_8BIT      0xa4   // generic core
#define CORE_TYPE_SHARPMZ   0xa7   // Sharp MZ Series
#define CORE_TYPE_8BIT2     0xa8   // generic core using dual SDRAM

#define EMU_NONE  0
#define EMU_MOUSE 1
#define EMU_JOY0  2
#define EMU_JOY1  3

void user_io_init(const char *path, const char *xml);
unsigned char user_io_core_type();
void user_io_read_core_name();
void user_io_poll();
char user_io_menu_button();
char user_io_user_button();
void user_io_osd_key_enable(char);
int user_io_get_kbd_reset();
void user_io_set_kbd_reset(int reset);

int substrcpy(char *d, const char *s, char idx);

void user_io_read_confstr();
char *user_io_get_confstr(int index);
int user_io_status_bits(const char *opt, int *s, int *e, int ex = 0, int single = 0);
uint32_t user_io_status_mask(const char *opt);
uint32_t user_io_hd_mask(const char *opt);
uint32_t user_io_status_get(const char *opt, int ex = 0);
void user_io_status_set(const char *opt, uint32_t value, int ex = 0);
int user_io_status_save(const char *filename);
void user_io_status_reset();

uint32_t user_io_get_file_crc();
int  user_io_file_mount(const char *name, unsigned char index = 0, char pre = 0, int pre_size = 0);
void user_io_bufferinvalidate(unsigned char index);
char *user_io_make_filepath(const char *path, const char *filename);
char *user_io_get_core_name(int orig = 0);
char *user_io_get_core_path(const char *suffix = NULL, int recheck = 0);
void user_io_name_override(const char* name);
char has_menu();

const char *get_image_name(int i);

int user_io_get_kbdemu();
uint32_t user_io_get_uart_mode();

void user_io_mouse(unsigned char b, int16_t x, int16_t y, int16_t w);
void user_io_kbd(uint16_t key, int press);
char* user_io_create_config_name();
int user_io_get_joy_transl();
void user_io_digital_joystick(unsigned char, uint32_t, int);
void user_io_l_analog_joystick(unsigned char, char, char);
void user_io_r_analog_joystick(unsigned char, char, char);
void user_io_set_joyswap(int swap);
int user_io_get_joyswap();
char user_io_osd_is_visible();
void set_vga_fb(int enable);
int get_vga_fb();
void user_io_set_ini(int ini_num);
void user_io_send_buttons(char);
uint16_t user_io_get_sdram_cfg();

int user_io_file_tx(const char* name, unsigned char index = 0, char opensave = 0, char mute = 0, char composite = 0, uint32_t load_addr = 0);
int user_io_file_tx_a(const char* name, uint16_t index);
unsigned char user_io_ext_idx(char *, char*);
void user_io_set_index(unsigned char index);
void user_io_set_aindex(uint16_t index);
void user_io_set_download(unsigned char enable, int addr = 0);
void user_io_file_tx_data(const uint8_t *addr, uint32_t len);
void user_io_set_upload(unsigned char enable, int addr = 0);
void user_io_file_rx_data(uint8_t *addr, uint32_t len);
void user_io_file_info(const char *ext);
int user_io_get_width();

void user_io_check_reset(unsigned short modifiers, char useKeys);

void user_io_rtc_reset();

void user_io_screenshot_cmd(const char *cmd);
bool user_io_screenshot(const char *pngname, int rescale);

const char* get_rbf_dir();
const char* get_rbf_name();
const char* get_rbf_path();

uint16_t sdram_sz(int sz = -1);
int user_io_is_dualsdr();
uint16_t altcfg(int alt = -1);

int GetUARTMode();
void SetUARTMode(int mode);
int GetMidiLinkMode();
void SetMidiLinkMode(int mode);
void ResetUART();
const uint32_t* GetUARTbauds(int mode);
uint32_t GetUARTbaud(int mode);
const char* GetUARTbaud_label(int mode);
const char* GetUARTbaud_label(int mode, int idx);
int GetUARTbaud_idx(int mode);
uint32_t ValidateUARTbaud(int mode, uint32_t baud);
char * GetMidiLinkSoundfont();
void user_io_store_filename(char *filename);
int user_io_use_cheats();

int process_ss(const char *rom_name, int enable = 1);

void diskled_on();
#define DISKLED_ON  diskled_on()
#define DISKLED_OFF void()

char is_minimig();
char is_sharpmz();
char is_menu();
char is_x86();
char is_snes();
char is_neogeo();
char is_megacd();
char is_pce();
char is_archie();
char is_gba();
char is_c64();
char is_st();
char is_psx();
char is_arcade();

#define HomeDir(x) user_io_get_core_path(x)
#define CoreName user_io_get_core_name()

#endif // USER_IO_H
