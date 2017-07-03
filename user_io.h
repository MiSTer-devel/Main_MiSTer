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

// codes as used by MiST (atari)
// directions (in/out) are from an io controller view
#define UIO_IKBD_OUT    0x02
#define UIO_IKBD_IN     0x03
#define UIO_SERIAL_OUT  0x04
#define UIO_SERIAL_IN   0x05
#define UIO_PARALLEL_IN 0x06
#define UIO_MIDI_OUT    0x07
#define UIO_MIDI_IN     0x08
#define UIO_ETH_MAC     0x09
#define UIO_ETH_STATUS  0x0a
#define UIO_ETH_FRM_IN  0x0b
#define UIO_ETH_FRM_OUT 0x0c
#define UIO_SERIAL_STAT 0x0d

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
#define UIO_SET_VIDEO   0x20  // set HDMI video mode 0: 1280x720p60(TV), 1: 1280x1024p60(PC), 2-255: reserved

// codes as used by 8bit (atari 800, zx81) via SS2
#define UIO_GET_STATUS  0x50
#define UIO_SECTOR_SND  0x51
#define UIO_SECTOR_RCV  0x52
#define UIO_FILE_TX     0x53
#define UIO_FILE_TX_DAT 0x54
#define UIO_FILE_INDEX  0x55
#define UIO_FILE_INFO   0x56

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

#define BUTTON1         0x01
#define BUTTON2         0x02

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

#define CONF_VGA_SCALER         0x04
#define CONF_CSYNC              0x08
#define CONF_FORCED_SCANDOUBLER 0x10
#define CONF_YPBPR              0x20

// core type value should be unlikely to be returned by broken cores
#define CORE_TYPE_UNKNOWN   0x55
#define CORE_TYPE_DUMB      0xa0   // core without any io controller interaction
#define CORE_TYPE_PACE      0xa2   // core from pacedev.net (joystick only)
#define CORE_TYPE_MIST      0xa3   // mist atari st core   
#define CORE_TYPE_8BIT      0xa4   // atari 800/c64 like core
#define CORE_TYPE_MINIMIG2  0xa5   // new Minimig with AGA
#define CORE_TYPE_ARCHIE    0xa6   // Acorn Archimedes

// user io status bits (currently only used by 8bit)
#define UIO_STATUS_RESET   0x01

#define UIO_STOP_BIT_1   0
#define UIO_STOP_BIT_1_5 1
#define UIO_STOP_BIT_2   2

#define UIO_PARITY_NONE  0
#define UIO_PARITY_ODD   1
#define UIO_PARITY_EVEN  2
#define UIO_PARITY_MARK  3
#define UIO_PARITY_SPACE 4

#define UIO_PRIORITY_KEYBOARD 0 
#define UIO_PRIORITY_GAMEPAD  1

typedef enum { EMU_NONE, EMU_MOUSE, EMU_JOY0, EMU_JOY1 } emu_mode_t;

// serial status data type returned from the core 
typedef struct {
	uint32_t bitrate;        // 300, 600 ... 115200
	uint8_t datasize;        // 5,6,7,8 ...
	uint8_t parity;
	uint8_t stopbits;
	uint8_t fifo_stat;       // space in cores input fifo
} __attribute__((packed)) serial_status_t;

void user_io_init();
void user_io_detect_core_type();
unsigned char user_io_core_type();
char is_minimig();
char user_io_is_8bit_with_config_string();
void user_io_poll();
char user_io_menu_button();
char user_io_button_dip_switch1();
char user_io_user_button();
void user_io_osd_key_enable(char);
void user_io_serial_tx(char *, uint16_t);
char *user_io_8bit_get_string(char);
unsigned long user_io_8bit_set_status(unsigned long, unsigned long);
int  user_io_file_tx(char *, unsigned char);
void user_io_sd_set_config(void);
char user_io_dip_switch1(void);
char user_io_serial_status(serial_status_t *, uint8_t);
int  user_io_file_mount(char *name);
char *user_io_get_core_name();
char *user_io_get_core_name_ex();
char is_menu_core();

emu_mode_t user_io_get_kbdemu();

// io controllers interface for FPGA ethernet emulation using usb ethernet
// devices attached to the io controller (ethernec emulation)
void user_io_eth_send_mac(uint8_t *);
uint32_t user_io_eth_get_status(void);
void user_io_eth_send_rx_frame(uint8_t *, uint16_t);
void user_io_eth_receive_tx_frame(uint8_t *, uint16_t);

// hooks from the usb layer
void user_io_mouse(unsigned char b, int16_t x, int16_t y);
void user_io_kbd(unsigned char m, unsigned char *k, unsigned short vid, unsigned short pid);
char* user_io_create_config_name();
void user_io_digital_joystick(unsigned char, uint16_t);
void user_io_analog_joystick(unsigned char, char, char);
char user_io_osd_is_visible();
void user_io_send_buttons(char);

void user_io_key_remap(char *);
void add_modifiers(uint8_t mod, uint16_t* keys_ps2);

void user_io_set_index(unsigned char index);
unsigned char user_io_ext_idx(char *, char*);

#endif // USER_IO_H
