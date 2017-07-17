// http://wiki.amigaos.net/index.php/Keymap_Library
// http://www.win.tue.nl/~aeb/linux/kbd/scancodes-14.html

#include "osd.h"

#ifndef KEYCODES_H
#define KEYCODES_H

#define MISS  0xff
#define KEYCODE_MAX (0x6f)

// The original minimig had the keyboard connected to the FPGA. Thus all key events (even for OSD)
// came from the FPGA core. The MIST has the keyboard attached to the arm controller. To be compatible
// with the minimig core all keys (incl. OSD!) are forwarded to the FPGA and the OSD keys are returned.
// These keys are tagged with the "OSD" flag
// The atari/mist core does not forwards keys through the FPGA but queues them inside the arm controller.
// Keys flagged with "OSD_OPEN" are used to open the OSD in non-minimig. They can have a keycode which
// will be sent into the core

#define OSD               0x0100     // to be used by OSD, not the core itself
#define OSD_OPEN          0x0200     // OSD key not forwarded to core, but queued in arm controller
#define CAPS_LOCK_TOGGLE  0x0400     // caps lock toggle behaviour
#define NUM_LOCK_TOGGLE   0x0800
#define EXT               0x1000     // extended PS/2 keycode

// amiga unmapped: 
// 0x5a KP-( (mapped on Keyrah)
// 0x5b KP-) (mapped on Keyrah)
// codes >= 0x69 are for OSD only and are not sent to the amiga itself

// keycode translation table
const unsigned short usb2ami[] = {
	MISS,  // 00: NoEvent
	MISS,  // 01: Overrun Error
	MISS,  // 02: POST fail
	MISS,  // 03: ErrorUndefined
	0x20,  // 04: a
	0x35,  // 05: b
	0x33,  // 06: c
	0x22,  // 07: d
	0x12,  // 08: e
	0x23,  // 09: f
	0x24,  // 0a: g
	0x25,  // 0b: h
	0x17,  // 0c: i
	0x26,  // 0d: j
	0x27,  // 0e: k
	0x28,  // 0f: l
	0x37,  // 10: m
	0x36,  // 11: n
	0x18,  // 12: o
	0x19,  // 13: p
	0x10,  // 14: q
	0x13,  // 15: r
	0x21,  // 16: s
	0x14,  // 17: t
	0x16,  // 18: u
	0x34,  // 19: v
	0x11,  // 1a: w
	0x32,  // 1b: x
	0x15,  // 1c: y
	0x31,  // 1d: z
	0x01,  // 1e: 1
	0x02,  // 1f: 2
	0x03,  // 20: 3
	0x04,  // 21: 4
	0x05,  // 22: 5
	0x06,  // 23: 6
	0x07,  // 24: 7
	0x08,  // 25: 8
	0x09,  // 26: 9
	0x0a,  // 27: 0
	0x44,  // 28: Return
	0x45,  // 29: Escape
	0x41,  // 2a: Backspace
	0x42,  // 2b: Tab
	0x40,  // 2c: Space
	0x0b,  // 2d: -
	0x0c,  // 2e: =
	0x1a,  // 2f: [
	0x1b,  // 30: ]
	0x0d,  // 31: backslash (only on us keyboards)
	0x2b,  // 32: Europe 1 (only on international keyboards)
	0x29,  // 33: ; 
	0x2a,  // 34: '
	0x00,  // 35: `
	0x38,  // 36: ,
	0x39,  // 37: .
	0x3a,  // 38: /
	0x62 | CAPS_LOCK_TOGGLE,  // 39: Caps Lock
	0x50,  // 3a: F1
	0x51,  // 3b: F2
	0x52,  // 3c: F3
	0x53,  // 3d: F4
	0x54,  // 3e: F5
	0x55,  // 3f: F6
	0x56,  // 40: F7
	0x57,  // 41: F8
	0x58,  // 42: F9
	0x59,  // 43: F10
	0x5f,  // 44: F11
	OSD_OPEN,  // 45: F12 (OSD)
	0x6e | OSD,  // 46: Print Screen (OSD)
	NUM_LOCK_TOGGLE,  // 47: Scroll Lock (OSD)
	0x6f | OSD,  // 48: Pause
	0x0d,  // 49: backslash to avoid panic in Germany ;)
	0x6a,  // 4a: Home
	0x6c | OSD,  // 4b: Page Up (OSD)
	0x46,  // 4c: Delete
	MISS,  // 4d: End
	0x6d | OSD,  // 4e: Page Down (OSD)
	0x4e,  // 4f: Right Arrow
	0x4f,  // 50: Left Arrow
	0x4d,  // 51: Down Arrow
	0x4c,  // 52: Up Arrow
	NUM_LOCK_TOGGLE,  // 53: Num Lock
	0x5c,  // 54: KP /
	0x5d,  // 55: KP *
	0x4a,  // 56: KP -
	0x5e,  // 57: KP +
	0x43,  // 58: KP Enter
	0x1d,  // 59: KP 1
	0x1e,  // 5a: KP 2
	0x1f,  // 5b: KP 3
	0x2d,  // 5c: KP 4
	0x2e,  // 5d: KP 5
	0x2f,  // 5e: KP 6
	0x3d,  // 5f: KP 7
	0x3e,  // 60: KP 8
	0x3f,  // 61: KP 9
	0x0f,  // 62: KP 0
	0x3c,  // 63: KP .
	0x30,  // 64: Europe 2
	0x69 | OSD,  // 65: App
	MISS,  // 66: Power
	MISS,  // 67: KP =
	0x5a,  // 68: KP (
	0x5b,  // 69: KP )
	MISS,  // 6a: F15
	0x5f,  // 6b: help (for keyrah)
	NUM_LOCK_TOGGLE | 1,  // 6c: F17
	NUM_LOCK_TOGGLE | 2,  // 6d: F18
	NUM_LOCK_TOGGLE | 3,  // 6e: F19
	NUM_LOCK_TOGGLE | 4   // 6f: F20
};

// unmapped atari keys:
// 0x63   KP (
// 0x64   KP )

// keycode translation table for atari
const unsigned short usb2atari[] = {
	MISS,  // 00: NoEvent
	MISS,  // 01: Overrun Error
	MISS,  // 02: POST fail
	MISS,  // 03: ErrorUndefined
	0x1e,  // 04: a
	0x30,  // 05: b
	0x2e,  // 06: c
	0x20,  // 07: d
	0x12,  // 08: e
	0x21,  // 09: f
	0x22,  // 0a: g
	0x23,  // 0b: h
	0x17,  // 0c: i
	0x24,  // 0d: j
	0x25,  // 0e: k
	0x26,  // 0f: l
	0x32,  // 10: m
	0x31,  // 11: n
	0x18,  // 12: o
	0x19,  // 13: p
	0x10,  // 14: q
	0x13,  // 15: r
	0x1f,  // 16: s
	0x14,  // 17: t
	0x16,  // 18: u
	0x2f,  // 19: v
	0x11,  // 1a: w
	0x2d,  // 1b: x
	0x15,  // 1c: y
	0x2c,  // 1d: z
	0x02,  // 1e: 1
	0x03,  // 1f: 2
	0x04,  // 20: 3
	0x05,  // 21: 4
	0x06,  // 22: 5
	0x07,  // 23: 6
	0x08,  // 24: 7
	0x09,  // 25: 8
	0x0a,  // 26: 9
	0x0b,  // 27: 0
	0x1c,  // 28: Return
	0x01,  // 29: Escape
	0x0e,  // 2a: Backspace
	0x0f,  // 2b: Tab
	0x39,  // 2c: Space
	0x0c,  // 2d: -
	0x0d,  // 2e: =
	0x1a,  // 2f: [
	0x1b,  // 30: ]
	0x29,  // 31: backslash, only on us keyboard
	0x29,  // 32: Europe 1, only on int. keyboard
	0x27,  // 33: ; 
	0x28,  // 34: '
	0x2b,  // 35: `
	0x33,  // 36: ,
	0x34,  // 37: .
	0x35,  // 38: /
	0x3a | CAPS_LOCK_TOGGLE,  // 39: Caps Lock
	0x3b,  // 3a: F1
	0x3c,  // 3b: F2
	0x3d,  // 3c: F3
	0x3e,  // 3d: F4
	0x3f,  // 3e: F5
	0x40,  // 3f: F6
	0x41,  // 40: F7
	0x42,  // 41: F8
	0x43,  // 42: F9
	0x44,  // 43: F10
	MISS,  // 44: F11
	OSD_OPEN,  // 45: F12
	MISS,  // 46: Print Screen
	NUM_LOCK_TOGGLE,  // 47: Scroll Lock
	MISS,  // 48: Pause
	0x52,  // 49: Insert
	0x47,  // 4a: Home
	0x62,  // 4b: Page Up
	0x53,  // 4c: Delete
	MISS,  // 4d: End
	0x61,  // 4e: Page Down
	0x4d,  // 4f: Right Arrow
	0x4b,  // 50: Left Arrow
	0x50,  // 51: Down Arrow
	0x48,  // 52: Up Arrow
	NUM_LOCK_TOGGLE,  // 53: Num Lock
	0x65,  // 54: KP /
	0x66,  // 55: KP *
	0x4a,  // 56: KP -
	0x4e,  // 57: KP +
	0x72,  // 58: KP Enter
	0x6d,  // 59: KP 1
	0x6e,  // 5a: KP 2
	0x6f,  // 5b: KP 3
	0x6a,  // 5c: KP 4
	0x6b,  // 5d: KP 5
	0x6c,  // 5e: KP 6
	0x67,  // 5f: KP 7
	0x68,  // 60: KP 8
	0x69,  // 61: KP 9
	0x70,  // 62: KP 0
	0x71,  // 63: KP .
	0x60,  // 64: Europe 2
	OSD_OPEN, // 65: App
	MISS,  // 66: Power
	MISS,  // 67: KP =
	MISS,  // 68: F13
	MISS,  // 69: F14
	MISS,  // 6a: F15
	0x52,  // 6b: insert (for keyrah)
	NUM_LOCK_TOGGLE | 1,  // 6c: F17
	NUM_LOCK_TOGGLE | 2,  // 6d: F18
	NUM_LOCK_TOGGLE | 3,  // 6e: F19
	NUM_LOCK_TOGGLE | 4   // 6f: F20
};

// keycode translation table for ps2 emulation
const unsigned short usb2ps2[] = {
	MISS,  // 00: NoEvent
	MISS,  // 01: Overrun Error
	MISS,  // 02: POST fail
	MISS,  // 03: ErrorUndefined
	0x1c,  // 04: a
	0x32,  // 05: b
	0x21,  // 06: c
	0x23,  // 07: d
	0x24,  // 08: e
	0x2b,  // 09: f
	0x34,  // 0a: g
	0x33,  // 0b: h
	0x43,  // 0c: i
	0x3b,  // 0d: j
	0x42,  // 0e: k
	0x4b,  // 0f: l
	0x3a,  // 10: m
	0x31,  // 11: n
	0x44,  // 12: o
	0x4d,  // 13: p
	0x15,  // 14: q
	0x2d,  // 15: r
	0x1b,  // 16: s
	0x2c,  // 17: t
	0x3c,  // 18: u
	0x2a,  // 19: v
	0x1d,  // 1a: w
	0x22,  // 1b: x
	0x35,  // 1c: y
	0x1a,  // 1d: z
	0x16,  // 1e: 1
	0x1e,  // 1f: 2
	0x26,  // 20: 3
	0x25,  // 21: 4
	0x2e,  // 22: 5
	0x36,  // 23: 6
	0x3d,  // 24: 7
	0x3e,  // 25: 8
	0x46,  // 26: 9
	0x45,  // 27: 0
	0x5a,  // 28: Return
	0x76,  // 29: Escape
	0x66,  // 2a: Backspace
	0x0d,  // 2b: Tab
	0x29,  // 2c: Space
	0x4e,  // 2d: -
	0x55,  // 2e: =
	0x54,  // 2f: [
	0x5b,  // 30: ]
	0x5d,  // 31: backslash
	0x5d,  // 32: Europe 1
	0x4c,  // 33: ; 
	0x52,  // 34: '
	0x0e,  // 35: `
	0x41,  // 36: ,
	0x49,  // 37: .
	0x4a,  // 38: /
	0x58,  // 39: Caps Lock
	0x05,  // 3a: F1
	0x06,  // 3b: F2
	0x04,  // 3c: F3
	0x0c,  // 3d: F4
	0x03,  // 3e: F5
	0x0b,  // 3f: F6
	0x83,  // 40: F7
	0x0a,  // 41: F8
	0x01,  // 42: F9
	0x09,  // 43: F10
	0x78,  // 44: F11
	OSD_OPEN | 0x07,  // 45: F12 (OSD)
	EXT | 0x7c, // 46: Print Screen
	NUM_LOCK_TOGGLE,  // 47: Scroll Lock
	0x77,  // 48: Pause (special key handled inside user_io)
	EXT | 0x70, // 49: Insert
	EXT | 0x6c, // 4a: Home
	EXT | 0x7d, // 4b: Page Up
	EXT | 0x71, // 4c: Delete
	EXT | 0x69, // 4d: End
	EXT | 0x7a, // 4e: Page Down
	EXT | 0x74, // 4f: Right Arrow
	EXT | 0x6b, // 50: Left Arrow
	EXT | 0x72, // 51: Down Arrow
	EXT | 0x75, // 52: Up Arrow
	NUM_LOCK_TOGGLE,  // 53: Num Lock
	EXT | 0x4a, // 54: KP /
	0x7c,  // 55: KP *
	0x7b,  // 56: KP -
	0x79,  // 57: KP +
	EXT | 0x5a, // 58: KP Enter
	0x69,  // 59: KP 1
	0x72,  // 5a: KP 2
	0x7a,  // 5b: KP 3
	0x6b,  // 5c: KP 4
	0x73,  // 5d: KP 5
	0x74,  // 5e: KP 6
	0x6c,  // 5f: KP 7
	0x75,  // 60: KP 8
	0x7d,  // 61: KP 9
	0x70,  // 62: KP 0
	0x71,  // 63: KP .
	0x61,  // 64: Europe 2
	OSD_OPEN | EXT | 0x2f, // 65: App
	EXT | 0x37, // 66: Power
	0x0f,  // 67: KP =
	0x77,  // 68: Num Lock
	0x7e,  // 69: Scroll Lock
	0x18,  // 6a: F15
	EXT | 0x70,  // 6b: insert (for keyrah)
	NUM_LOCK_TOGGLE | 1,  // 6c: F17
	NUM_LOCK_TOGGLE | 2,  // 6d: F18
	NUM_LOCK_TOGGLE | 3,  // 6e: F19
	NUM_LOCK_TOGGLE | 4   // 6f: F20
};

// Archimedes unmapped keys
// Missing sterling
// Missing kp_hash
// Missing button_1
// Missing button_2
// Missing button_3
// Missing button_4
// Missing button_5

// keycode translation table
const unsigned short usb2archie[] = {
	MISS, // 00: NoEvent
	MISS, // 01: Overrun Error
	MISS, // 02: POST fail
	MISS, // 03: ErrorUndefined
	0x3c, // 04: a
	0x52, // 05: b
	0x50, // 06: c
	0x3e, // 07: d
	0x29, // 08: e
	0x3f, // 09: f
	0x40, // 0a: g
	0x41, // 0b: h
	0x2e, // 0c: i
	0x42, // 0d: j
	0x43, // 0e: k
	0x44, // 0f: l
	0x54, // 10: m
	0x53, // 11: n
	0x2f, // 12: o
	0x30, // 13: p
	0x27, // 14: q
	0x2a, // 15: r
	0x3d, // 16: s
	0x2b, // 17: t
	0x2d, // 18: u
	0x51, // 19: v
	0x28, // 1a: w
	0x4f, // 1b: x
	0x2c, // 1c: y
	0x4e, // 1d: z
	0x11, // 1e: 1
	0x12, // 1f: 2
	0x13, // 20: 3
	0x14, // 21: 4
	0x15, // 22: 5
	0x16, // 23: 6
	0x17, // 24: 7
	0x18, // 25: 8
	0x19, // 26: 9
	0x1a, // 27: 0
	0x47, // 28: Return
	0x00, // 29: Escape
	0x1e, // 2a: Backspace
	0x26, // 2b: Tab
	0x5f, // 2c: Space
	0x1b, // 2d: -
	0x1c, // 2e: =
	0x31, // 2f: [
	0x32, // 30: ]
	0x33, // 31: backslash (only on us keyboards)
	0x33, // 32: Europe 1 (only on international kbds)
	0x45, // 33: ;
	0x46, // 34: '
	0x10, // 35: `
	0x55, // 36: ,
	0x56, // 37: .
	0x57, // 38: /
	0x5d, // 39: Caps Lock
	0x01, // 3a: F1
	0x02, // 3b: F2
	0x03, // 3c: F3
	0x04, // 3d: F4
	0x05, // 3e: F5
	0x06, // 3f: F6
	0x07, // 40: F7
	0x08, // 41: F8
	0x09, // 42: F9
	0x0a, // 43: F10
	0x0b, // 44: F11
	0x0c, // 45: F12 - Used heavily by the archie... OSD moved to printscreen.
		  //  0x0d, // 46: Print Screen
	OSD_OPEN, // 46: Print Screen
	0x0e, // 47: Scroll Lock
	0x0f, // 48: Pause
	0x1f, // 49: Insert
	0x20, // 4a: Home
	0x21, // 4b: Page Up
	0x34, // 4c: Delete
	0x35, // 4d: End 
	0x36, // 4e: Page Down
	0x64, // 4f: Right Arrow
	0x62, // 50: Left Arrow
	0x63, // 51: Down Arrow
	0x59, // 52: Up Arrow
	0x22, // 53: Num Lock
	0x23, // 54: KP /
	0x24, // 55: KP *
	0x3a, // 56: KP -
	0x4b, // 57: KP +
	0x67, // 58: KP Enter
	0x5a, // 59: KP 1
	0x5b, // 5a: KP 2
	0x5c, // 5b: KP 3
	0x48, // 5c: KP 4
	0x49, // 5d: KP 5
	0x4a, // 5e: KP 6
	0x37, // 5f: KP 7
	0x38, // 60: KP 8
	0x39, // 61: KP 9
	0x65, // 62: KP 0
	0x66, // 63: KP decimal
	MISS, //  64: Europe 2
	0x72, //  65: App (maps to middle mouse button)
	MISS, //  66: Power
	MISS, //  67: KP =
	MISS, //  68: F13
	MISS, //  69: F14
	MISS, //  6a: F15
	0x1f, //  6b: insert (for keyrah)
	MISS, //  6c: F17
	MISS, //  6d: F18
	MISS, //  6e: F19
	MISS, //  6f: F20
};

#endif
