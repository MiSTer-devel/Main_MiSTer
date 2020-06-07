#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <sys/sysinfo.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdarg.h>

#include "input.h"
#include "user_io.h"
#include "menu.h"
#include "hardware.h"
#include "cfg.h"
#include "fpga_io.h"
#include "osd.h"
#include "video.h"
#include "joymapping.h"
#include "support.h"

#define NUMDEV 30
#define NUMPLAYERS 6
#define UINPUT_NAME "MiSTer virtual input"

char joy_bnames[NUMBUTTONS][32] = {};
int  joy_bcount = 0;

static int ev2amiga[] =
{
	NONE, //0   KEY_RESERVED
	0x45, //1   KEY_ESC
	0x01, //2   KEY_1
	0x02, //3   KEY_2
	0x03, //4   KEY_3
	0x04, //5   KEY_4
	0x05, //6   KEY_5
	0x06, //7   KEY_6
	0x07, //8   KEY_7
	0x08, //9   KEY_8
	0x09, //10  KEY_9
	0x0a, //11  KEY_0
	0x0b, //12  KEY_MINUS
	0x0c, //13  KEY_EQUAL
	0x41, //14  KEY_BACKSPACE
	0x42, //15  KEY_TAB
	0x10, //16  KEY_Q
	0x11, //17  KEY_W
	0x12, //18  KEY_E
	0x13, //19  KEY_R
	0x14, //20  KEY_T
	0x15, //21  KEY_Y
	0x16, //22  KEY_U
	0x17, //23  KEY_I
	0x18, //24  KEY_O
	0x19, //25  KEY_P
	0x1a, //26  KEY_LEFTBRACE
	0x1b, //27  KEY_RIGHTBRACE
	0x44, //28  KEY_ENTER
	0x63, //29  KEY_LEFTCTRL
	0x20, //30  KEY_A
	0x21, //31  KEY_S
	0x22, //32  KEY_D
	0x23, //33  KEY_F
	0x24, //34  KEY_G
	0x25, //35  KEY_H
	0x26, //36  KEY_J
	0x27, //37  KEY_K
	0x28, //38  KEY_L
	0x29, //39  KEY_SEMICOLON
	0x2a, //40  KEY_APOSTROPHE
	0x00, //41  KEY_GRAVE
	0x60, //42  KEY_LEFTSHIFT
	0x0d, //43  KEY_BACKSLASH
	0x31, //44  KEY_Z
	0x32, //45  KEY_X
	0x33, //46  KEY_C
	0x34, //47  KEY_V
	0x35, //48  KEY_B
	0x36, //49  KEY_N
	0x37, //50  KEY_M
	0x38, //51  KEY_COMMA
	0x39, //52  KEY_DOT
	0x3a, //53  KEY_SLASH
	0x61, //54  KEY_RIGHTSHIFT
	0x5d, //55  KEY_KPASTERISK
	0x64, //56  KEY_LEFTALT
	0x40, //57  KEY_SPACE
	0x62 | CAPS_TOGGLE, //58  KEY_CAPSLOCK
	0x50, //59  KEY_F1
	0x51, //60  KEY_F2
	0x52, //61  KEY_F3
	0x53, //62  KEY_F4
	0x54, //63  KEY_F5
	0x55, //64  KEY_F6
	0x56, //65  KEY_F7
	0x57, //66  KEY_F8
	0x58, //67  KEY_F9
	0x59, //68  KEY_F10
	NONE, //69  KEY_NUMLOCK
	NONE, //70  KEY_SCROLLLOCK
	0x3d, //71  KEY_KP7
	0x3e, //72  KEY_KP8
	0x3f, //73  KEY_KP9
	0x4a, //74  KEY_KPMINUS
	0x2d, //75  KEY_KP4
	0x2e, //76  KEY_KP5
	0x2f, //77  KEY_KP6
	0x5e, //78  KEY_KPPLUS
	0x1d, //79  KEY_KP1
	0x1e, //80  KEY_KP2
	0x1f, //81  KEY_KP3
	0x0f, //82  KEY_KP0
	0x3c, //83  KEY_KPDOT
	NONE, //84  ???
	NONE, //85  KEY_ZENKAKU
	0x30, //86  KEY_102ND, '<' on most keyboards
	0x5f, //87  KEY_F11
	NONE, //88  KEY_F12
	NONE, //89  KEY_RO
	NONE, //90  KEY_KATAKANA
	NONE, //91  KEY_HIRAGANA
	NONE, //92  KEY_HENKAN
	NONE, //93  KEY_KATAKANA
	NONE, //94  KEY_MUHENKAN
	NONE, //95  KEY_KPJPCOMMA
	0x43, //96  KEY_KPENTER
	0x63, //97  KEY_RIGHTCTRL
	0x5c, //98  KEY_KPSLASH
	NONE, //99  KEY_SYSRQ
	0x65, //100 KEY_RIGHTALT
	NONE, //101 KEY_LINEFEED
	0x6a, //102 KEY_HOME
	0x4c, //103 KEY_UP
	0x5b, //104 KEY_PAGEUP
	0x4f, //105 KEY_LEFT
	0x4e, //106 KEY_RIGHT
	NONE, //107 KEY_END
	0x4d, //108 KEY_DOWN
	0x5a, //109 KEY_PAGEDOWN
	0x0d, //110 KEY_INSERT
	0x46, //111 KEY_DELETE
	NONE, //112 KEY_MACRO
	NONE, //113 KEY_MUTE
	NONE, //114 KEY_VOLUMEDOWN
	NONE, //115 KEY_VOLUMEUP
	NONE, //116 KEY_POWER
	NONE, //117 KEY_KPEQUAL
	NONE, //118 KEY_KPPLUSMINUS
	NONE, //119 KEY_PAUSE
	NONE, //120 KEY_SCALE
	NONE, //121 KEY_KPCOMMA
	NONE, //122 KEY_HANGEUL
	NONE, //123 KEY_HANJA
	NONE, //124 KEY_YEN
	0x66, //125 KEY_LEFTMETA
	0x67, //126 KEY_RIGHTMETA
	NONE, //127 KEY_COMPOSE
	NONE, //128 KEY_STOP
	NONE, //129 KEY_AGAIN
	NONE, //130 KEY_PROPS
	NONE, //131 KEY_UNDO
	NONE, //132 KEY_FRONT
	NONE, //133 KEY_COPY
	NONE, //134 KEY_OPEN
	NONE, //135 KEY_PASTE
	NONE, //136 KEY_FIND
	NONE, //137 KEY_CUT
	NONE, //138 KEY_HELP
	NONE, //139 KEY_MENU
	NONE, //140 KEY_CALC
	NONE, //141 KEY_SETUP
	NONE, //142 KEY_SLEEP
	NONE, //143 KEY_WAKEUP
	NONE, //144 KEY_FILE
	NONE, //145 KEY_SENDFILE
	NONE, //146 KEY_DELETEFILE
	NONE, //147 KEY_XFER
	NONE, //148 KEY_PROG1
	NONE, //149 KEY_PROG2
	NONE, //150 KEY_WWW
	NONE, //151 KEY_MSDOS
	NONE, //152 KEY_SCREENLOCK
	NONE, //153 KEY_DIRECTION
	NONE, //154 KEY_CYCLEWINDOWS
	NONE, //155 KEY_MAIL
	NONE, //156 KEY_BOOKMARKS
	NONE, //157 KEY_COMPUTER
	NONE, //158 KEY_BACK
	NONE, //159 KEY_FORWARD
	NONE, //160 KEY_CLOSECD
	NONE, //161 KEY_EJECTCD
	NONE, //162 KEY_EJECTCLOSECD
	NONE, //163 KEY_NEXTSONG
	NONE, //164 KEY_PLAYPAUSE
	NONE, //165 KEY_PREVIOUSSONG
	NONE, //166 KEY_STOPCD
	NONE, //167 KEY_RECORD
	NONE, //168 KEY_REWIND
	NONE, //169 KEY_PHONE
	NONE, //170 KEY_ISO
	NONE, //171 KEY_CONFIG
	NONE, //172 KEY_HOMEPAGE
	NONE, //173 KEY_REFRESH
	NONE, //174 KEY_EXIT
	NONE, //175 KEY_MOVE
	NONE, //176 KEY_EDIT
	NONE, //177 KEY_SCROLLUP
	NONE, //178 KEY_SCROLLDOWN
	NONE, //179 KEY_KPLEFTPAREN
	NONE, //180 KEY_KPRIGHTPAREN
	NONE, //181 KEY_NEW
	NONE, //182 KEY_REDO
	0x5a, //183 KEY_F13
	0x5b, //184 KEY_F14
	NONE, //185 KEY_F15
	0x5f, //186 KEY_F16
	NONE, //187 KEY_F17
	NONE, //188 KEY_F18
	NONE, //189 KEY_F19
	NONE, //190 KEY_F20
	NONE, //191 KEY_F21
	NONE, //192 KEY_F22
	NONE, //193 KEY_F23
	0x2b, //194 # on German keyboard, was 0x63 (CTRL on Amiga), 194 KEY_F24
	NONE, //195 ???
	NONE, //196 ???
	NONE, //197 ???
	NONE, //198 ???
	NONE, //199 ???
	NONE, //200 KEY_PLAYCD
	NONE, //201 KEY_PAUSECD
	NONE, //202 KEY_PROG3
	NONE, //203 KEY_PROG4
	NONE, //204 KEY_DASHBOARD
	NONE, //205 KEY_SUSPEND
	NONE, //206 KEY_CLOSE
	NONE, //207 KEY_PLAY
	NONE, //208 KEY_FASTFORWARD
	NONE, //209 KEY_BASSBOOST
	NONE, //210 KEY_PRINT
	NONE, //211 KEY_HP
	NONE, //212 KEY_CAMERA
	NONE, //213 KEY_SOUND
	NONE, //214 KEY_QUESTION
	NONE, //215 KEY_EMAIL
	NONE, //216 KEY_CHAT
	NONE, //217 KEY_SEARCH
	NONE, //218 KEY_CONNECT
	NONE, //219 KEY_FINANCE
	NONE, //220 KEY_SPORT
	NONE, //221 KEY_SHOP
	NONE, //222 KEY_ALTERASE
	NONE, //223 KEY_CANCEL
	NONE, //224 KEY_BRIGHT_DOWN
	NONE, //225 KEY_BRIGHT_UP
	NONE, //226 KEY_MEDIA
	NONE, //227 KEY_SWITCHVIDEO
	NONE, //228 KEY_DILLUMTOGGLE
	NONE, //229 KEY_DILLUMDOWN
	NONE, //230 KEY_DILLUMUP
	NONE, //231 KEY_SEND
	NONE, //232 KEY_REPLY
	NONE, //233 KEY_FORWARDMAIL
	NONE, //234 KEY_SAVE
	NONE, //235 KEY_DOCUMENTS
	NONE, //236 KEY_BATTERY
	NONE, //237 KEY_BLUETOOTH
	NONE, //238 KEY_WLAN
	NONE, //239 KEY_UWB
	NONE, //240 KEY_UNKNOWN
	NONE, //241 KEY_VIDEO_NEXT
	NONE, //242 KEY_VIDEO_PREV
	NONE, //243 KEY_BRIGHT_CYCLE
	NONE, //244 KEY_BRIGHT_AUTO
	NONE, //245 KEY_DISPLAY_OFF
	NONE, //246 KEY_WWAN
	NONE, //247 KEY_RFKILL
	NONE, //248 KEY_MICMUTE
	NONE, //249 ???
	NONE, //250 ???
	NONE, //251 ???
	NONE, //252 ???
	NONE, //253 ???
	NONE, //254 ???
	NONE  //255 ???
};


static const int ev2ps2[] =
{
	NONE, //0   KEY_RESERVED
	0x76, //1   KEY_ESC
	0x16, //2   KEY_1
	0x1e, //3   KEY_2
	0x26, //4   KEY_3
	0x25, //5   KEY_4
	0x2e, //6   KEY_5
	0x36, //7   KEY_6
	0x3d, //8   KEY_7
	0x3e, //9   KEY_8
	0x46, //10  KEY_9
	0x45, //11  KEY_0
	0x4e, //12  KEY_MINUS
	0x55, //13  KEY_EQUAL
	0x66, //14  KEY_BACKSPACE
	0x0d, //15  KEY_TAB
	0x15, //16  KEY_Q
	0x1d, //17  KEY_W
	0x24, //18  KEY_E
	0x2d, //19  KEY_R
	0x2c, //20  KEY_T
	0x35, //21  KEY_Y
	0x3c, //22  KEY_U
	0x43, //23  KEY_I
	0x44, //24  KEY_O
	0x4d, //25  KEY_P
	0x54, //26  KEY_LEFTBRACE
	0x5b, //27  KEY_RIGHTBRACE
	0x5a, //28  KEY_ENTER
	LCTRL | 0x14, //29  KEY_LEFTCTRL
	0x1c, //30  KEY_A
	0x1b, //31  KEY_S
	0x23, //32  KEY_D
	0x2b, //33  KEY_F
	0x34, //34  KEY_G
	0x33, //35  KEY_H
	0x3b, //36  KEY_J
	0x42, //37  KEY_K
	0x4b, //38  KEY_L
	0x4c, //39  KEY_SEMICOLON
	0x52, //40  KEY_APOSTROPHE
	0x0e, //41  KEY_GRAVE
	LSHIFT | 0x12, //42  KEY_LEFTSHIFT
	0x5d, //43  KEY_BACKSLASH
	0x1a, //44  KEY_Z
	0x22, //45  KEY_X
	0x21, //46  KEY_C
	0x2a, //47  KEY_V
	0x32, //48  KEY_B
	0x31, //49  KEY_N
	0x3a, //50  KEY_M
	0x41, //51  KEY_COMMA
	0x49, //52  KEY_DOT
	0x4a, //53  KEY_SLASH
	RSHIFT | 0x59, //54  KEY_RIGHTSHIFT
	0x7c, //55  KEY_KPASTERISK
	LALT | 0x11, //56  KEY_LEFTALT
	0x29, //57  KEY_SPACE
	0x58, //58  KEY_CAPSLOCK
	0x05, //59  KEY_F1
	0x06, //60  KEY_F2
	0x04, //61  KEY_F3
	0x0c, //62  KEY_F4
	0x03, //63  KEY_F5
	0x0b, //64  KEY_F6
	0x83, //65  KEY_F7
	0x0a, //66  KEY_F8
	0x01, //67  KEY_F9
	0x09, //68  KEY_F10
	EMU_SWITCH_2 | 0x77, //69  KEY_NUMLOCK
	EMU_SWITCH_1 | 0x7E, //70  KEY_SCROLLLOCK
	0x6c, //71  KEY_KP7
	0x75, //72  KEY_KP8
	0x7d, //73  KEY_KP9
	0x7b, //74  KEY_KPMINUS
	0x6b, //75  KEY_KP4
	0x73, //76  KEY_KP5
	0x74, //77  KEY_KP6
	0x79, //78  KEY_KPPLUS
	0x69, //79  KEY_KP1
	0x72, //80  KEY_KP2
	0x7a, //81  KEY_KP3
	0x70, //82  KEY_KP0
	0x71, //83  KEY_KPDOT
	NONE, //84  ???
	NONE, //85  KEY_ZENKAKU
	0x56, //86  KEY_102ND
	0x78, //87  KEY_F11
	0x07, //88  KEY_F12
	NONE, //89  KEY_RO
	NONE, //90  KEY_KATAKANA
	NONE, //91  KEY_HIRAGANA
	NONE, //92  KEY_HENKAN
	NONE, //93  KEY_KATAKANA
	NONE, //94  KEY_MUHENKAN
	NONE, //95  KEY_KPJPCOMMA
	EXT | 0x5a, //96  KEY_KPENTER
	RCTRL | EXT | 0x14, //97  KEY_RIGHTCTRL
	EXT | 0x4a, //98  KEY_KPSLASH
	0xE2, //99  KEY_SYSRQ
	RALT | EXT | 0x11, //100 KEY_RIGHTALT
	NONE, //101 KEY_LINEFEED
	EXT | 0x6c, //102 KEY_HOME
	EXT | 0x75, //103 KEY_UP
	EXT | 0x7d, //104 KEY_PAGEUP
	EXT | 0x6b, //105 KEY_LEFT
	EXT | 0x74, //106 KEY_RIGHT
	EXT | 0x69, //107 KEY_END
	EXT | 0x72, //108 KEY_DOWN
	EXT | 0x7a, //109 KEY_PAGEDOWN
	EXT | 0x70, //110 KEY_INSERT
	EXT | 0x71, //111 KEY_DELETE
	NONE, //112 KEY_MACRO
	NONE, //113 KEY_MUTE
	NONE, //114 KEY_VOLUMEDOWN
	NONE, //115 KEY_VOLUMEUP
	NONE, //116 KEY_POWER
	NONE, //117 KEY_KPEQUAL
	NONE, //118 KEY_KPPLUSMINUS
	0xE1, //119 KEY_PAUSE
	NONE, //120 KEY_SCALE
	NONE, //121 KEY_KPCOMMA
	NONE, //122 KEY_HANGEUL
	NONE, //123 KEY_HANJA
	NONE, //124 KEY_YEN
	LGUI | EXT | 0x1f, //125 KEY_LEFTMETA
	RGUI | EXT | 0x27, //126 KEY_RIGHTMETA
	NONE, //127 KEY_COMPOSE
	NONE, //128 KEY_STOP
	NONE, //129 KEY_AGAIN
	NONE, //130 KEY_PROPS
	NONE, //131 KEY_UNDO
	NONE, //132 KEY_FRONT
	NONE, //133 KEY_COPY
	NONE, //134 KEY_OPEN
	NONE, //135 KEY_PASTE
	NONE, //136 KEY_FIND
	NONE, //137 KEY_CUT
	NONE, //138 KEY_HELP
	NONE, //139 KEY_MENU
	NONE, //140 KEY_CALC
	NONE, //141 KEY_SETUP
	NONE, //142 KEY_SLEEP
	NONE, //143 KEY_WAKEUP
	NONE, //144 KEY_FILE
	NONE, //145 KEY_SENDFILE
	NONE, //146 KEY_DELETEFILE
	NONE, //147 KEY_XFER
	NONE, //148 KEY_PROG1
	NONE, //149 KEY_PROG2
	NONE, //150 KEY_WWW
	NONE, //151 KEY_MSDOS
	NONE, //152 KEY_SCREENLOCK
	NONE, //153 KEY_DIRECTION
	NONE, //154 KEY_CYCLEWINDOWS
	NONE, //155 KEY_MAIL
	NONE, //156 KEY_BOOKMARKS
	NONE, //157 KEY_COMPUTER
	NONE, //158 KEY_BACK
	NONE, //159 KEY_FORWARD
	NONE, //160 KEY_CLOSECD
	NONE, //161 KEY_EJECTCD
	NONE, //162 KEY_EJECTCLOSECD
	NONE, //163 KEY_NEXTSONG
	NONE, //164 KEY_PLAYPAUSE
	NONE, //165 KEY_PREVIOUSSONG
	NONE, //166 KEY_STOPCD
	NONE, //167 KEY_RECORD
	NONE, //168 KEY_REWIND
	NONE, //169 KEY_PHONE
	NONE, //170 KEY_ISO
	NONE, //171 KEY_CONFIG
	NONE, //172 KEY_HOMEPAGE
	NONE, //173 KEY_REFRESH
	NONE, //174 KEY_EXIT
	NONE, //175 KEY_MOVE
	NONE, //176 KEY_EDIT
	NONE, //177 KEY_SCROLLUP
	NONE, //178 KEY_SCROLLDOWN
	NONE, //179 KEY_KPLEFTPAREN
	NONE, //180 KEY_KPRIGHTPAREN
	NONE, //181 KEY_NEW
	NONE, //182 KEY_REDO
	NONE, //183 KEY_F13
	NONE, //184 KEY_F14
	NONE, //185 KEY_F15
	NONE, //186 KEY_F16
	EMU_SWITCH_1 | 1, //187 KEY_F17
	EMU_SWITCH_1 | 2, //188 KEY_F18
	EMU_SWITCH_1 | 3, //189 KEY_F19
	EMU_SWITCH_1 | 4, //190 KEY_F20
	NONE, //191 KEY_F21
	NONE, //192 KEY_F22
	NONE, //193 KEY_F23
	0x5D, //194 U-mlaut on DE mapped to backslash
	NONE, //195 ???
	NONE, //196 ???
	NONE, //197 ???
	NONE, //198 ???
	NONE, //199 ???
	NONE, //200 KEY_PLAYCD
	NONE, //201 KEY_PAUSECD
	NONE, //202 KEY_PROG3
	NONE, //203 KEY_PROG4
	NONE, //204 KEY_DASHBOARD
	NONE, //205 KEY_SUSPEND
	NONE, //206 KEY_CLOSE
	NONE, //207 KEY_PLAY
	NONE, //208 KEY_FASTFORWARD
	NONE, //209 KEY_BASSBOOST
	NONE, //210 KEY_PRINT
	NONE, //211 KEY_HP
	NONE, //212 KEY_CAMERA
	NONE, //213 KEY_SOUND
	NONE, //214 KEY_QUESTION
	NONE, //215 KEY_EMAIL
	NONE, //216 KEY_CHAT
	NONE, //217 KEY_SEARCH
	NONE, //218 KEY_CONNECT
	NONE, //219 KEY_FINANCE
	NONE, //220 KEY_SPORT
	NONE, //221 KEY_SHOP
	NONE, //222 KEY_ALTERASE
	NONE, //223 KEY_CANCEL
	NONE, //224 KEY_BRIGHT_DOWN
	NONE, //225 KEY_BRIGHT_UP
	NONE, //226 KEY_MEDIA
	NONE, //227 KEY_SWITCHVIDEO
	NONE, //228 KEY_DILLUMTOGGLE
	NONE, //229 KEY_DILLUMDOWN
	NONE, //230 KEY_DILLUMUP
	NONE, //231 KEY_SEND
	NONE, //232 KEY_REPLY
	NONE, //233 KEY_FORWARDMAIL
	NONE, //234 KEY_SAVE
	NONE, //235 KEY_DOCUMENTS
	NONE, //236 KEY_BATTERY
	NONE, //237 KEY_BLUETOOTH
	NONE, //238 KEY_WLAN
	NONE, //239 KEY_UWB
	NONE, //240 KEY_UNKNOWN
	NONE, //241 KEY_VIDEO_NEXT
	NONE, //242 KEY_VIDEO_PREV
	NONE, //243 KEY_BRIGHT_CYCLE
	NONE, //244 KEY_BRIGHT_AUTO
	NONE, //245 KEY_DISPLAY_OFF
	NONE, //246 KEY_WWAN
	NONE, //247 KEY_RFKILL
	NONE, //248 KEY_MICMUTE
	NONE, //249 ???
	NONE, //250 ???
	NONE, //251 ???
	NONE, //252 ???
	NONE, //253 ???
	NONE, //254 ???
	NONE  //255 ???
};

static int ev2archie[] =
{
	NONE, //0   KEY_RESERVED
	0x00, //1   KEY_ESC
	0x11, //2   KEY_1
	0x12, //3   KEY_2
	0x13, //4   KEY_3
	0x14, //5   KEY_4
	0x15, //6   KEY_5
	0x16, //7   KEY_6
	0x17, //8   KEY_7
	0x18, //9   KEY_8
	0x19, //10  KEY_9
	0x1a, //11  KEY_0
	0x1b, //12  KEY_MINUS
	0x1c, //13  KEY_EQUAL
	0x1e, //14  KEY_BACKSPACE
	0x26, //15  KEY_TAB
	0x27, //16  KEY_Q
	0x28, //17  KEY_W
	0x29, //18  KEY_E
	0x2a, //19  KEY_R
	0x2b, //20  KEY_T
	0x2c, //21  KEY_Y
	0x2d, //22  KEY_U
	0x2e, //23  KEY_I
	0x2f, //24  KEY_O
	0x30, //25  KEY_P
	0x31, //26  KEY_LEFTBRACE
	0x32, //27  KEY_RIGHTBRACE
	0x47, //28  KEY_ENTER
	0x3b, //29  KEY_LEFTCTRL
	0x3c, //30  KEY_A
	0x3d, //31  KEY_S
	0x3e, //32  KEY_D
	0x3f, //33  KEY_F
	0x40, //34  KEY_G
	0x41, //35  KEY_H
	0x42, //36  KEY_J
	0x43, //37  KEY_K
	0x44, //38  KEY_L
	0x45, //39  KEY_SEMICOLON
	0x46, //40  KEY_APOSTROPHE
	0x10, //41  KEY_GRAVE
	0x4c, //42  KEY_LEFTSHIFT
	0x33, //43  KEY_BACKSLASH
	0x4e, //44  KEY_Z
	0x4f, //45  KEY_X
	0x50, //46  KEY_C
	0x51, //47  KEY_V
	0x52, //48  KEY_B
	0x53, //49  KEY_N
	0x54, //50  KEY_M
	0x55, //51  KEY_COMMA
	0x56, //52  KEY_DOT
	0x57, //53  KEY_SLASH
	0x58, //54  KEY_RIGHTSHIFT
	0x24, //55  KEY_KPASTERISK
	0x5e, //56  KEY_LEFTALT
	0x5f, //57  KEY_SPACE
	0x5d, //58  KEY_CAPSLOCK
	0x01, //59  KEY_F1
	0x02, //60  KEY_F2
	0x03, //61  KEY_F3
	0x04, //62  KEY_F4
	0x05, //63  KEY_F5
	0x06, //64  KEY_F6
	0x07, //65  KEY_F7
	0x08, //66  KEY_F8
	0x09, //67  KEY_F9
	0x0a, //68  KEY_F10
	0x22, //69  KEY_NUMLOCK
	NONE, //70  KEY_SCROLLLOCK
	0x37, //71  KEY_KP7
	0x38, //72  KEY_KP8
	0x39, //73  KEY_KP9
	0x3a, //74  KEY_KPMINUS
	0x48, //75  KEY_KP4
	0x49, //76  KEY_KP5
	0x4a, //77  KEY_KP6
	0x4b, //78  KEY_KPPLUS
	0x5a, //79  KEY_KP1
	0x5b, //80  KEY_KP2
	0x5c, //81  KEY_KP3
	0x65, //82  KEY_KP0
	0x66, //83  KEY_KPDOT
	NONE, //84  ???
	NONE, //85  KEY_ZENKAKU
	NONE, //86  KEY_102ND
	0x0b, //87  KEY_F11
	0x0c, //88  KEY_F12
	NONE, //89  KEY_RO
	NONE, //90  KEY_KATAKANA
	NONE, //91  KEY_HIRAGANA
	NONE, //92  KEY_HENKAN
	NONE, //93  KEY_KATAKANA
	NONE, //94  KEY_MUHENKAN
	NONE, //95  KEY_KPJPCOMMA
	0x67, //96  KEY_KPENTER
	0x61, //97  KEY_RIGHTCTRL
	0x23, //98  KEY_KPSLASH
	0x0D, //99  KEY_SYSRQ
	0x60, //100 KEY_RIGHTALT
	NONE, //101 KEY_LINEFEED
	0x20, //102 KEY_HOME
	0x59, //103 KEY_UP
	0x21, //104 KEY_PAGEUP
	0x62, //105 KEY_LEFT
	0x64, //106 KEY_RIGHT
	0x35, //107 KEY_END
	0x63, //108 KEY_DOWN
	0x36, //109 KEY_PAGEDOWN
	0x1f, //110 KEY_INSERT
	0x34, //111 KEY_DELETE
	NONE, //112 KEY_MACRO
	NONE, //113 KEY_MUTE
	NONE, //114 KEY_VOLUMEDOWN
	NONE, //115 KEY_VOLUMEUP
	NONE, //116 KEY_POWER
	NONE, //117 KEY_KPEQUAL
	NONE, //118 KEY_KPPLUSMINUS
	0x0f, //119 KEY_PAUSE
	NONE, //120 KEY_SCALE
	NONE, //121 KEY_KPCOMMA
	NONE, //122 KEY_HANGEUL
	NONE, //123 KEY_HANJA
	NONE, //124 KEY_YEN
	NONE, //125 KEY_LEFTMETA
	NONE, //126 KEY_RIGHTMETA
	0x71, //127 KEY_COMPOSE
	NONE, //128 KEY_STOP
	NONE, //129 KEY_AGAIN
	NONE, //130 KEY_PROPS
	NONE, //131 KEY_UNDO
	NONE, //132 KEY_FRONT
	NONE, //133 KEY_COPY
	NONE, //134 KEY_OPEN
	NONE, //135 KEY_PASTE
	NONE, //136 KEY_FIND
	NONE, //137 KEY_CUT
	NONE, //138 KEY_HELP
	NONE, //139 KEY_MENU
	NONE, //140 KEY_CALC
	NONE, //141 KEY_SETUP
	NONE, //142 KEY_SLEEP
	NONE, //143 KEY_WAKEUP
	NONE, //144 KEY_FILE
	NONE, //145 KEY_SENDFILE
	NONE, //146 KEY_DELETEFILE
	NONE, //147 KEY_XFER
	NONE, //148 KEY_PROG1
	NONE, //149 KEY_PROG2
	NONE, //150 KEY_WWW
	NONE, //151 KEY_MSDOS
	NONE, //152 KEY_SCREENLOCK
	NONE, //153 KEY_DIRECTION
	NONE, //154 KEY_CYCLEWINDOWS
	NONE, //155 KEY_MAIL
	NONE, //156 KEY_BOOKMARKS
	NONE, //157 KEY_COMPUTER
	NONE, //158 KEY_BACK
	NONE, //159 KEY_FORWARD
	NONE, //160 KEY_CLOSECD
	NONE, //161 KEY_EJECTCD
	NONE, //162 KEY_EJECTCLOSECD
	NONE, //163 KEY_NEXTSONG
	NONE, //164 KEY_PLAYPAUSE
	NONE, //165 KEY_PREVIOUSSONG
	NONE, //166 KEY_STOPCD
	NONE, //167 KEY_RECORD
	NONE, //168 KEY_REWIND
	NONE, //169 KEY_PHONE
	NONE, //170 KEY_ISO
	NONE, //171 KEY_CONFIG
	NONE, //172 KEY_HOMEPAGE
	NONE, //173 KEY_REFRESH
	NONE, //174 KEY_EXIT
	NONE, //175 KEY_MOVE
	NONE, //176 KEY_EDIT
	NONE, //177 KEY_SCROLLUP
	NONE, //178 KEY_SCROLLDOWN
	NONE, //179 KEY_KPLEFTPAREN
	NONE, //180 KEY_KPRIGHTPAREN
	NONE, //181 KEY_NEW
	NONE, //182 KEY_REDO
	NONE, //183 KEY_F13
	NONE, //184 KEY_F14
	NONE, //185 KEY_F15
	NONE, //186 KEY_F16
	NONE, //187 KEY_F17
	NONE, //188 KEY_F18
	NONE, //189 KEY_F19
	NONE, //190 KEY_F20
	NONE, //191 KEY_F21
	NONE, //192 KEY_F22
	NONE, //193 KEY_F23
	NONE, //194 KEY_F24
	NONE, //195 ???
	NONE, //196 ???
	NONE, //197 ???
	NONE, //198 ???
	NONE, //199 ???
	NONE, //200 KEY_PLAYCD
	NONE, //201 KEY_PAUSECD
	NONE, //202 KEY_PROG3
	NONE, //203 KEY_PROG4
	NONE, //204 KEY_DASHBOARD
	NONE, //205 KEY_SUSPEND
	NONE, //206 KEY_CLOSE
	NONE, //207 KEY_PLAY
	NONE, //208 KEY_FASTFORWARD
	NONE, //209 KEY_BASSBOOST
	NONE, //210 KEY_PRINT
	NONE, //211 KEY_HP
	NONE, //212 KEY_CAMERA
	NONE, //213 KEY_SOUND
	NONE, //214 KEY_QUESTION
	NONE, //215 KEY_EMAIL
	NONE, //216 KEY_CHAT
	NONE, //217 KEY_SEARCH
	NONE, //218 KEY_CONNECT
	NONE, //219 KEY_FINANCE
	NONE, //220 KEY_SPORT
	NONE, //221 KEY_SHOP
	NONE, //222 KEY_ALTERASE
	NONE, //223 KEY_CANCEL
	NONE, //224 KEY_BRIGHT_DOWN
	NONE, //225 KEY_BRIGHT_UP
	NONE, //226 KEY_MEDIA
	NONE, //227 KEY_SWITCHVIDEO
	NONE, //228 KEY_DILLUMTOGGLE
	NONE, //229 KEY_DILLUMDOWN
	NONE, //230 KEY_DILLUMUP
	NONE, //231 KEY_SEND
	NONE, //232 KEY_REPLY
	NONE, //233 KEY_FORWARDMAIL
	NONE, //234 KEY_SAVE
	NONE, //235 KEY_DOCUMENTS
	NONE, //236 KEY_BATTERY
	NONE, //237 KEY_BLUETOOTH
	NONE, //238 KEY_WLAN
	NONE, //239 KEY_UWB
	NONE, //240 KEY_UNKNOWN
	NONE, //241 KEY_VIDEO_NEXT
	NONE, //242 KEY_VIDEO_PREV
	NONE, //243 KEY_BRIGHT_CYCLE
	NONE, //244 KEY_BRIGHT_AUTO
	NONE, //245 KEY_DISPLAY_OFF
	NONE, //246 KEY_WWAN
	NONE, //247 KEY_RFKILL
	NONE, //248 KEY_MICMUTE
	NONE, //249 ???
	NONE, //250 ???
	NONE, //251 ???
	NONE, //252 ???
	NONE, //253 ???
	NONE, //254 ???
	NONE  //255 ???
};

uint32_t get_ps2_code(uint16_t key)
{
	if (key > 255) return NONE;
	return ev2ps2[key];
}

uint32_t get_amiga_code(uint16_t key)
{
	if (key > 255) return NONE;
	return ev2amiga[key];
}

uint32_t get_archie_code(uint16_t key)
{
	if (key > 255) return NONE;
	return ev2archie[key];
}

static uint32_t modifier = 0;

uint32_t get_key_mod()
{
	return modifier & MODMASK;
}

enum QUIRK
{
	QUIRK_NONE = 0,
	QUIRK_CWIID,
	QUIRK_WIIMOTE,
	QUIRK_DS3,
	QUIRK_DS4,
	QUIRK_DS4TOUCH,
	QUIRK_MADCATZ360,
	QUIRK_PDSP,
	QUIRK_PDSP_ARCADE,
	QUIRK_JAMMASD,
	QUIRK_JPAC,
};

typedef struct
{
	uint16_t vid, pid;
	char     idstr[256];

	uint8_t  led;
	uint8_t  mouse;
	uint8_t  axis_edge[256];
	int8_t   axis_pos[256];

	uint8_t  num;
	uint8_t  has_map;
	uint32_t map[NUMBUTTONS];

	uint8_t  osd_combo;

	uint8_t  has_mmap;
	uint32_t mmap[NUMBUTTONS];
	uint16_t jkmap[1024];

	uint8_t  has_kbdmap;
	uint8_t  kbdmap[256];

	uint16_t guncal[4];

	int      accx, accy;
	int      quirk;

	int      misc_flags;
	int      mc_a;

	int      lightgun_req;
	int      lightgun;

	int      bind;
	char     devname[32];
	char     id[32];
	char     name[128];
} devInput;

static devInput input[NUMDEV] = {};

#define BTN_NUM (sizeof(devInput::map) / sizeof(devInput::map[0]))

int mfd = -1;
int mwd = -1;

static int set_watch()
{
	mwd = -1;
	mfd = inotify_init();
	if (mfd < 0)
	{
		printf("ERR: inotify_init");
		return -1;
	}

	mwd = inotify_add_watch(mfd, "/dev/input", IN_MODIFY | IN_CREATE | IN_DELETE);

	if (mwd < 0)
	{
		printf("ERR: inotify_add_watch");
		return -1;
	}

	return mfd;
}

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

static int check_devs()
{
	int result = 0;
	int length, i = 0;
	char buffer[BUF_LEN];
	length = read(mfd, buffer, BUF_LEN);

	if (length < 0)
	{
		printf("ERR: read\n");
		return 0;
	}

	while (i<length)
	{
		struct inotify_event *event = (struct inotify_event *) &buffer[i];
		if (event->len)
		{
			if (event->mask & IN_CREATE)
			{
				result = 1;
				if (event->mask & IN_ISDIR)
				{
					printf("The directory %s was created.\n", event->name);
				}
				else
				{
					printf("The file %s was created.\n", event->name);
				}
			}
			else if (event->mask & IN_DELETE)
			{
				result = 1;
				if (event->mask & IN_ISDIR)
				{
					printf("The directory %s was deleted.\n", event->name);
				}
				else
				{
					printf("The file %s was deleted.\n", event->name);
				}
			}
			/*
			else if ( event->mask & IN_MODIFY )
			{
				result = 1;
				if ( event->mask & IN_ISDIR )
				{
					printf( "The directory %s was modified.\n", event->name );
				}
				else
				{
					printf( "The file %s was modified.\n", event->name );
				}
			}
			*/
		}
		i += EVENT_SIZE + event->len;
	}

	return result;
}

static void INThandler(int code)
{
	(void)code;

	printf("\nExiting...\n");

	if (mwd >= 0) inotify_rm_watch(mfd, mwd);
	if (mfd >= 0) close(mfd);

	exit(0);
}

#define test_bit(bit, array)  (array [bit / 8] & (1 << (bit % 8)))

static char has_led(int fd)
{
	unsigned char evtype_b[(EV_MAX + 7) / 8];
	if (fd<0) return 0;

	memset(&evtype_b, 0, sizeof(evtype_b));
	if (ioctl(fd, EVIOCGBIT(0, sizeof(evtype_b)), evtype_b) < 0)
	{
		printf("ERR: evdev ioctl.\n");
		return 0;
	}

	return test_bit(EV_LED, evtype_b) ? 1 : 0;
}

static char leds_state = 0;
void set_kbdled(int mask, int state)
{
	leds_state = state ? leds_state | (mask&HID_LED_MASK) : leds_state & ~(mask&HID_LED_MASK);
}

int get_kbdled(int mask)
{
	return (leds_state & (mask&HID_LED_MASK)) ? 1 : 0;
}

int toggle_kbdled(int mask)
{
	int state = !get_kbdled(mask);
	set_kbdled(mask, state);
	return state;
}

#define JOYMAP_DIR  "inputs/"
static int load_map(const char *name, void *pBuffer, int size)
{
	char path[256] = { JOYMAP_DIR };
	strcat(path, name);
	int ret = FileLoadConfig(path, pBuffer, size);
	if (!ret) return FileLoadConfig(name, pBuffer, size);
	return ret;
}

static void delete_map(const char *name)
{
	char path[256] = { JOYMAP_DIR };
	FileCreatePath(path);
	strcat(path, name);
	FileDeleteConfig(name);
	FileDeleteConfig(path);
}

static int save_map(const char *name, void *pBuffer, int size)
{
	char path[256] = { JOYMAP_DIR };
	FileCreatePath(path);
	strcat(path, name);
	FileDeleteConfig(name);
	return FileSaveConfig(path, pBuffer, size);
}

static int mapping = 0;
static int mapping_button;
static int mapping_dev = -1;
static int mapping_type;
static int mapping_count;
static int mapping_clear;
static int mapping_set;

static uint32_t tmp_axis[4];
static int tmp_axis_n = 0;

static int grabbed = 1;

void start_map_setting(int cnt, int set)
{
	mapping_button = 0;
	mapping = 1;
	mapping_set = set;
	if (!mapping_set)
	{
		mapping_dev = -1;
		mapping_type = (cnt < 0) ? 3 : cnt ? 1 : 2;
	}
	mapping_count = cnt;
	mapping_clear = 0;
	tmp_axis_n = 0;

	if (mapping_type <= 1 && is_menu()) mapping_button = -6;
	memset(tmp_axis, 0, sizeof(tmp_axis));

	//un-stick the enter key
	user_io_kbd(KEY_ENTER, 0);
}

int get_map_button()
{
	return mapping_button;
}

int get_map_type()
{
	return mapping_type;
}

int get_map_clear()
{
	return mapping_clear;
}

static uint32_t osd_timer = 0;
int get_map_cancel()
{
	return (mapping && !is_menu() && osd_timer && CheckTimer(osd_timer));
}

static char *get_map_name(int dev, int def)
{
	static char name[128];
	if (def || is_menu()) sprintf(name, "input_%s_v3.map", input[dev].idstr);
	else sprintf(name, "%s_input_%s_v3.map", user_io_get_core_name_ex(), input[dev].idstr);
	return name;
}

static char *get_kbdmap_name(int dev)
{
	static char name[128];
	sprintf(name, "kbd_%s.map", input[dev].idstr);
	return name;
}

void finish_map_setting(int dismiss)
{
	mapping = 0;
	if (mapping_dev<0) return;

	if (mapping_type == 2)
	{
		if (dismiss) input[mapping_dev].has_kbdmap = 0;
		else if (dismiss == 2) FileDeleteConfig(get_kbdmap_name(mapping_dev));
		else FileSaveConfig(get_kbdmap_name(mapping_dev), &input[mapping_dev].kbdmap, sizeof(input[mapping_dev].kbdmap));
	}
	else if (mapping_type == 3)
	{
		if (dismiss) memset(input[mapping_dev].jkmap, 0, sizeof(input[mapping_dev].jkmap));
	}
	else
	{
		for (int i = 0; i < NUMDEV; i++) input[i].has_map = 0;

		if (!dismiss) save_map(get_map_name(mapping_dev, 0), &input[mapping_dev].map, sizeof(input[mapping_dev].map));
		if (dismiss == 2) delete_map(get_map_name(mapping_dev, 0));
		if (is_menu()) input[mapping_dev].has_mmap = 0;
	}
}

void input_lightgun_cal(uint16_t *cal)
{
	FileSaveConfig("wiimote_cal.cfg", cal, 4 * sizeof(uint16_t));
	for (int i = 0; i < NUMDEV; i++)
	{
		if (input[i].quirk == QUIRK_WIIMOTE) memcpy(input[i].guncal, cal, sizeof(input[i].guncal));
	}
}

int input_has_lightgun()
{
	for (int i = 0; i < NUMDEV; i++)
	{
		if (input[i].quirk == QUIRK_WIIMOTE) return 1;
	}
	return 0;
}

uint16_t get_map_vid()
{
	return (mapping && mapping_dev >= 0) ? input[mapping_dev].vid : 0;
}

uint16_t get_map_pid()
{
	return (mapping && mapping_dev >= 0) ? input[mapping_dev].pid : 0;
}

int has_default_map()
{
	return (mapping_dev >= 0) ? (input[mapping_dev].has_mmap == 1) : 0;
}

static char kr_fn_table[] =
{
	KEY_KPSLASH,    KEY_PAUSE,
	KEY_KPASTERISK, KEY_PRINT,
	KEY_LEFT,       KEY_HOME,
	KEY_RIGHT,      KEY_END,
	KEY_UP,         KEY_PAGEUP,
	KEY_DOWN,       KEY_PAGEDOWN,
	KEY_F1,         KEY_F11,
	KEY_F2,         KEY_F12,

	KEY_F3,         KEY_F17, // EMU_MOUSE
	KEY_F4,         KEY_F18, // EMU_JOY0
	KEY_F5,         KEY_F19, // EMU_JOY1
	KEY_F6,         KEY_F20, // EMU_NONE

    //Emulate keypad for A600
	KEY_1,          KEY_KP1,
	KEY_2,          KEY_KP2,
	KEY_3,          KEY_KP3,
	KEY_4,          KEY_KP4,
	KEY_5,          KEY_KP5,
	KEY_6,          KEY_KP6,
	KEY_7,          KEY_KP7,
	KEY_8,          KEY_KP8,
	KEY_9,          KEY_KP9,
	KEY_0,          KEY_KP0,
	KEY_MINUS,      KEY_KPMINUS,
	KEY_EQUAL,      KEY_KPPLUS,
	KEY_BACKSLASH,  KEY_KPASTERISK,
	KEY_LEFTBRACE,  KEY_F13,    //KP(
	KEY_RIGHTBRACE, KEY_F14,    //KP)
	KEY_DOT,        KEY_KPDOT,
	KEY_ENTER,      KEY_KPENTER
};

static int keyrah_trans(int key, int press)
{
	static int fn = 0;

	if (key == KEY_NUMLOCK)    return KEY_F13; // numlock -> f13
	if (key == KEY_SCROLLLOCK) return KEY_F14; // scrlock -> f14
	if (key == KEY_INSERT)     return KEY_F16; // insert -> f16. workaround!

	if (key == KEY_102ND)
	{
		if (!press && fn == 1) menu_key_set(KEY_MENU);
		fn = press ? 1 : 0;
		return 0;
	}
	else if (fn)
	{
		fn |= 2;
		for (uint32_t n = 0; n<(sizeof(kr_fn_table) / (2 * sizeof(kr_fn_table[0]))); n++)
		{
			if ((key&255) == kr_fn_table[n * 2]) return kr_fn_table[(n * 2) + 1];
		}
	}

	return key;
}

static void input_cb(struct input_event *ev, struct input_absinfo *absinfo, int dev);

static int kbd_toggle = 0;
static uint32_t joy[NUMPLAYERS] = {};
static uint32_t autofire[NUMPLAYERS] = {};
static uint32_t autofirecodes[NUMPLAYERS][BTN_NUM] = {};
static int af_delay[NUMPLAYERS] = {};

static unsigned char mouse_btn = 0;
static unsigned char mice_btn = 0;
static int mouse_emu = 0;
static int kbd_mouse_emu = 0;
static int mouse_sniper = 0;
static int mouse_emu_x = 0;
static int mouse_emu_y = 0;

static uint32_t mouse_timer = 0;

#define BTN_TGL 100
#define BTN_OSD 101

static int uinp_fd = -1;
static int input_uinp_setup()
{
	if (uinp_fd <= 0)
	{
		struct uinput_user_dev uinp;

		if (!(uinp_fd = open("/dev/uinput", O_WRONLY | O_NDELAY)))
		{
			printf("Unable to open /dev/uinput\n");
			uinp_fd = -1;
			return 0;
		}

		memset(&uinp, 0, sizeof(uinp));
		strncpy(uinp.name, UINPUT_NAME, UINPUT_MAX_NAME_SIZE);
		uinp.id.version = 4;
		uinp.id.bustype = BUS_USB;

		ioctl(uinp_fd, UI_SET_EVBIT, EV_KEY);
		for (int i = 0; i < 256; i++) ioctl(uinp_fd, UI_SET_KEYBIT, i);

		write(uinp_fd, &uinp, sizeof(uinp));
		if (ioctl(uinp_fd, UI_DEV_CREATE))
		{
			printf("Unable to create UINPUT device.");
			close(uinp_fd);
			uinp_fd = -1;
			return 0;
		}
	}
	return 1;
}

void input_uinp_destroy()
{
	if (uinp_fd > 0)
	{
		ioctl(uinp_fd, UI_DEV_DESTROY);
		close(uinp_fd);
		uinp_fd = -1;
	}
}

static unsigned long uinp_repeat = 0;
static struct input_event uinp_ev;
static void uinp_send_key(uint16_t key, int press)
{
	if (uinp_fd > 0)
	{
		if (!uinp_ev.value && press)
		{
			uinp_repeat = GetTimer(REPEATDELAY);
		}

		memset(&uinp_ev, 0, sizeof(uinp_ev));
		gettimeofday(&uinp_ev.time, NULL);
		uinp_ev.type = EV_KEY;
		uinp_ev.code = key;
		uinp_ev.value = press;
		write(uinp_fd, &uinp_ev, sizeof(uinp_ev));

		static struct input_event ev;
		ev.time = uinp_ev.time;
		ev.type = EV_SYN;
		ev.code = SYN_REPORT;
		ev.value = 0;
		write(uinp_fd, &ev, sizeof(ev));
	}
}

static void uinp_check_key()
{
	if (uinp_fd > 0)
	{
		if (!grabbed)
		{
			if (uinp_ev.value && CheckTimer(uinp_repeat))
			{
				uinp_repeat = GetTimer(REPEATRATE);
				uinp_send_key(uinp_ev.code, 2);
			}
		}
		else
		{
			if (uinp_ev.value)
			{
				uinp_send_key(uinp_ev.code, 0);
			}
		}
	}
}

static void mouse_cb(unsigned char b, int16_t x = 0, int16_t y = 0, int16_t w = 0)
{
	if (grabbed) user_io_mouse(b, x, y, w);
}

static void joy_digital(int jnum, uint32_t mask, uint32_t code, char press, int bnum, int dont_save = 0)
{
	static uint32_t osdbtn = 0;
	static char str[128];
	static uint32_t lastcode[NUMPLAYERS], lastmask[NUMPLAYERS];
	int num = jnum - 1;

	if (num < NUMPLAYERS)
	{
		if (jnum)
		{
			if (bnum != BTN_OSD && bnum != BTN_TGL)
			{
				if (!dont_save)
				{
					if (press)
					{
						lastcode[num] = code;
						lastmask[num] = mask;
					}
					else
					{
						lastcode[num] = 0;
						lastmask[num] = 0;
					}
				}
			}
			else
			{
				if (!user_io_osd_is_visible() && press)
				{
					if (lastcode[num] && lastmask[num])
					{
						int found = 0;
						int zero = -1;
						for (uint i = 0; i < BTN_NUM; i++)
						{
							if (!autofirecodes[num][i]) zero = i;
							if (autofirecodes[num][i] == lastcode[num])
							{
								found = 1;
								autofirecodes[num][i] = 0;
								break;
							}
						}

						if (!found && zero >= 0) autofirecodes[num][zero] = lastcode[num];
						autofire[num] = !found ? autofire[num] | lastmask[num] : autofire[num] & ~lastmask[num];

						if (hasAPI1_5())
						{
							if (!found) sprintf(str, "Auto fire: %d ms", af_delay[num] * 2);
							else sprintf(str, "Auto fire: OFF");
							Info(str);
						}
						else InfoMessage((!found) ? "\n\n          Auto fire\n             ON" :
							"\n\n          Auto fire\n             OFF");

						return;
					}
					else if (lastmask[num] & 0xF)
					{
						if (lastmask[num] & 9)
						{
							af_delay[num] += 25 << ((lastmask[num] & 1) ? 1 : 0);
							if (af_delay[num] > 500) af_delay[num] = 500;
						}
						else
						{
							af_delay[num] -= 25 << ((lastmask[num] & 2) ? 1 : 0);
							if (af_delay[num] < 25) af_delay[num] = 25;
						}

						static char str[256];

						if (hasAPI1_5())
						{
							sprintf(str, "Auto fire period: %d ms", af_delay[num] * 2);
							Info(str);
						}
						else
						{
							sprintf(str, "\n\n       Auto fire period\n            %dms", af_delay[num] * 2);
							InfoMessage(str);
						}

						return;
					}
				}
			}
		}

		if (bnum == BTN_TGL)
		{
			if(press) kbd_toggle = !kbd_toggle;
			return;
		}

		if (!user_io_osd_is_visible() && (bnum == BTN_OSD) && (mouse_emu & 1))
		{
			if (press)
			{
				mouse_sniper = 0;
				mouse_timer = 0;
				mouse_btn = 0;
				mouse_emu_x = 0;
				mouse_emu_y = 0;
				mouse_cb(mice_btn);

				mouse_emu ^= 2;
				if (hasAPI1_5()) Info((mouse_emu & 2) ? "Mouse mode ON" : "Mouse mode OFF");
				else InfoMessage((mouse_emu & 2) ? "\n\n       Mouse mode lock\n             ON" :
					"\n\n       Mouse mode lock\n             OFF");
			}
			return;
		}

		// clear OSD button state if not in the OSD.  this avoids problems where buttons are still held
		// on OSD exit and causes combinations to match when partial buttons are pressed.
		if (!user_io_osd_is_visible()) osdbtn = 0;

		if (user_io_osd_is_visible() || (bnum == BTN_OSD))
		{
			if (press)
			{
				osdbtn |= mask;
				if (mask & (JOY_BTN1 | JOY_BTN2)) {
					if ((osdbtn & (JOY_BTN1 | JOY_BTN2)) == (JOY_BTN1 | JOY_BTN2))
					{
						osdbtn |= JOY_BTN3;
						mask = JOY_BTN3;
					}
				}
			}
			else
			{
				int old_osdbtn = osdbtn;
				osdbtn &= ~mask;

				if (mask & (JOY_BTN1 | JOY_BTN2)) {
					if ((old_osdbtn & (JOY_BTN1 | JOY_BTN2 | JOY_BTN3)) == (JOY_BTN1 | JOY_BTN2 | JOY_BTN3))
					{
						mask = JOY_BTN3;
					}
					else if (old_osdbtn & JOY_BTN3)
					{
						if (!(osdbtn & (JOY_BTN1 | JOY_BTN2))) osdbtn &= ~JOY_BTN3;
						mask = 0;
					}
				}

				if((mask & JOY_BTN2) && !(old_osdbtn & JOY_BTN2)) mask = 0;
			}

			memset(joy, 0, sizeof(joy));
			struct input_event ev;
			ev.type = EV_KEY;
			ev.value = press;
			switch (mask)
			{
			case JOY_RIGHT:
				if (press && (osdbtn & JOY_BTN2))
				{
					user_io_set_ini(0);
					osdbtn = 0;
					return;
				}
				ev.code = KEY_RIGHT;
				break;

			case JOY_LEFT:
				if (press && (osdbtn & JOY_BTN2))
				{
					user_io_set_ini(1);
					osdbtn = 0;
					return;
				}
				ev.code = KEY_LEFT;
				break;

			case JOY_UP:
				if (press && (osdbtn & JOY_BTN2))
				{
					user_io_set_ini(2);
					osdbtn = 0;
					return;
				}
				ev.code = KEY_UP;
				break;

			case JOY_DOWN:
				if (press && (osdbtn & JOY_BTN2))
				{
					user_io_set_ini(3);
					osdbtn = 0;
					return;
				}
				ev.code = KEY_DOWN;
				break;

			case JOY_BTN1:
				ev.code = KEY_ENTER;
				break;

			case JOY_BTN2:
				ev.code = KEY_ESC;
				break;

			case JOY_BTN3:
				ev.code = KEY_BACKSPACE;
				break;

			default:
				ev.code = (bnum == BTN_OSD) ? KEY_MENU : 0;
			}

			input_cb(&ev, 0, 0);
		}
		else if (video_fb_state())
		{
			switch (mask)
			{
			case JOY_RIGHT:
				uinp_send_key(KEY_RIGHT, press);
				break;

			case JOY_LEFT:
				uinp_send_key(KEY_LEFT, press);
				break;

			case JOY_UP:
				uinp_send_key(KEY_UP, press);
				break;

			case JOY_DOWN:
				uinp_send_key(KEY_DOWN, press);
				break;

			case JOY_BTN1:
				uinp_send_key(KEY_ENTER, press);
				break;

			case JOY_BTN2:
				uinp_send_key(KEY_ESC, press);
				break;

			case JOY_BTN3:
				uinp_send_key(KEY_SPACE, press);
				break;

			case JOY_BTN4:
				uinp_send_key(KEY_TAB, press);
				break;
			}
		}
		else if(jnum)
		{
			if (press) joy[num] |= mask;
			else joy[num] &= ~mask;
			//user_io_digital_joystick(num, joy[num]);

			if (code)
			{
				int found = 0;
				for (uint i = 0; i < BTN_NUM; i++) if (autofirecodes[num][i] == code) found = 1;
				if (found) autofire[num] = press ? autofire[num] | mask : autofire[num] & ~mask;
			}
		}
	}
}

static void joy_analog(int num, int axis, int offset)
{
	static int pos[NUMPLAYERS][2] = {};

	if (grabbed && num > 0 && num < NUMPLAYERS+1)
	{
		num--;
		pos[num][axis] = offset;
		user_io_analog_joystick(num, (char)(pos[num][0]), (char)(pos[num][1]));
	}
}

static int ds_mouse_emu = 0;

static void input_cb(struct input_event *ev, struct input_absinfo *absinfo, int dev)
{
	if (ev->type != EV_KEY && ev->type != EV_ABS && ev->type != EV_REL) return;
	if (ev->type == EV_KEY && (!ev->code || ev->code == KEY_UNKNOWN)) return;

	static uint16_t last_axis = 0;

	int sub_dev = dev;

	//check if device is a part of multifunctional device
	if (input[dev].bind >= 0) dev = input[dev].bind;

	//mouse
	if (ev->type == EV_KEY && ev->code >= BTN_MOUSE && ev->code < BTN_JOYSTICK)
	{
		//skip it. we use /dev/input/mice
		return;
	}

	static int key_mapped = 0;

	if (ev->type == EV_KEY && mapping && mapping_type == 3 && ev->code == input[dev].mmap[SYS_BTN_OSD_KTGL + 1]) ev->code = KEY_ENTER;

	int map_skip = (ev->type == EV_KEY && ev->code == KEY_SPACE && ((mapping_dev >= 0 && mapping_type==1) || mapping_button<0));
	int cancel   = (ev->type == EV_KEY && ev->code == KEY_ESC);
	int enter    = (ev->type == EV_KEY && ev->code == KEY_ENTER);
	int origcode = ev->code;

	if (!input[dev].num && ((ev->type == EV_KEY && ev->code >= 256) || (input[dev].quirk == QUIRK_PDSP && ev->type == EV_REL)))
	{
		for (uint8_t num = 1; num < NUMDEV + 1; num++)
		{
			int found = 0;
			for (int i = 0; i < NUMDEV; i++)
			{
				// paddles/spinners overlay on top of other gamepad
				if (!((input[dev].quirk == QUIRK_PDSP) ^ (input[i].quirk == QUIRK_PDSP)))
				{
					found = (input[i].num == num);
					if (found) break;
				}
			}

			if (!found)
			{
				input[dev].num = num;
				break;
			}
		}
	}

	if (!input[dev].has_mmap)
	{
		if (input[dev].quirk != QUIRK_PDSP)
		{
			if (!load_map(get_map_name(dev, 1), &input[dev].mmap, sizeof(input[dev].mmap)))
			{
				memset(input[dev].mmap, 0, sizeof(input[dev].mmap));
				input[dev].has_mmap++;
			}
			if (!input[dev].mmap[SYS_BTN_OSD_KTGL + 2]) input[dev].mmap[SYS_BTN_OSD_KTGL + 2] = input[dev].mmap[SYS_BTN_OSD_KTGL + 1];
		}
		input[dev].has_mmap++;
	}

	if (!input[dev].has_map)
	{
		if (input[dev].quirk == QUIRK_PDSP)
		{
			memset(input[dev].map, 0, sizeof(input[dev].map));
			input[dev].map[map_paddle_btn()] = 0x120;
			if (cfg.controller_info)
			{
				char str[32];
				sprintf(str, "P%d paddle/spinner", input[dev].num);
				Info(str, cfg.controller_info * 1000);
			}
		}
		else if (!load_map(get_map_name(dev, 0), &input[dev].map, sizeof(input[dev].map)))
		{
			memset(input[dev].map, 0, sizeof(input[dev].map));
			if (!is_menu())
			{
				if (input[dev].has_mmap == 1)
				{
					// not defined try to guess the mapping
					map_joystick(input[dev].map, input[dev].mmap, input[dev].num);
				}
				else
				{
					input[dev].has_map++;
				}
			}
			input[dev].has_map++;
		}
		else
		{
			map_joystick_show(input[dev].map, input[dev].mmap, input[dev].num);
		}
		input[dev].has_map++;
	}

	int old_combo = input[dev].osd_combo;

	if (ev->type == EV_KEY)
	{
		if (ev->code == input[dev].mmap[SYS_BTN_OSD_KTGL + 2])
		{
			if (ev->value) input[dev].osd_combo |= 2;
			else input[dev].osd_combo &= ~2;
		}

		if (ev->code == input[dev].mmap[SYS_BTN_OSD_KTGL + 1])
		{
			if (ev->value) input[dev].osd_combo |= 1;
			else input[dev].osd_combo &= ~1;
		}
	}

	int osd_event = 0;
	if (old_combo != 3 && input[dev].osd_combo == 3)
	{
		osd_event = 1;
		if (mapping && !is_menu()) osd_timer = GetTimer(1000);
	}
	else if (old_combo == 3 && input[dev].osd_combo != 3)
	{
		osd_event = 2;
		if (mapping && !is_menu())
		{
			if (CheckTimer(osd_timer))
			{
				cancel = 1;
				ev->code = KEY_ESC;
				ev->value = 0;
			}
			else
			{
				map_skip = 1;
				ev->value = 1;
			}
		}
		osd_timer = 0;
	}

	//mapping
	if (mapping && (mapping_dev >= 0 || ev->value)
		&& !((mapping_type < 2 || !mapping_button) && (cancel || enter))
		&& input[dev].quirk != QUIRK_PDSP)
	{
		int idx = 0;

		if (is_menu())
		{
			spi_uio_cmd(UIO_KEYBOARD); //ping the Menu core to wakeup
			osd_event = 0;
		}

		// paddle axis - skip from mapping
		if ((ev->type == EV_ABS || ev->type == EV_REL) && (ev->code == 7 || ev->code == 8)) return;

		// in alternative set, the first button can be skipped, so clear the set now
		if (!mapping_button && mapping_set && ev->value == 1 && mapping_dev >= 0)
		{
			for (uint i = 0; i < sizeof(input[0].map) / sizeof(input[0].map[0]); i++)
			{
				input[mapping_dev].map[i] &= 0x0000FFFF;
			}
		}

		if (ev->type == EV_KEY && mapping_button>=0 && !osd_event)
		{
			if (mapping_type == 2)
			{
				if (ev->code < 256)
				{
					if (ev->value == 1)
					{
						if (mapping_dev < 0)
						{
							mapping_dev = dev;
							mapping_button = 0;
						}

						if (!mapping_button) mapping_button = ev->code;
					}

					if (ev->value == 0 && mapping_dev >= 0 && mapping_button && mapping_button != ev->code)
					{
						input[mapping_dev].kbdmap[mapping_button] = ev->code;
						mapping_button = 0;
					}
				}
				return;
			}
			else if (mapping_type == 3)
			{
				if (ev->value == 1 && !mapping_button)
				{
					if (mapping_dev < 0) mapping_dev = dev;
					if (mapping_dev == dev && ev->code < 1024) mapping_button = ev->code;
				}

				if (ev->value == 0 && mapping_dev >= 0 && (ev->code<256 || mapping_dev == dev) && mapping_button && mapping_button != ev->code)
				{
					// Technically it's hard to map the key to button as keyboards
					// are all the same while joysticks are personalized and numbered.
					input[mapping_dev].jkmap[mapping_button] = ev->code;
					mapping_button = 0;
				}
				return;
			}
			else
			{
				int clear = (ev->code == KEY_F12 || ev->code == KEY_MENU || ev->code == KEY_HOMEPAGE) && !is_menu();
				if (ev->value == 1 && mapping_dev < 0 && !clear)
				{
					mapping_dev = dev;
					mapping_type = (ev->code >= 256) ? 1 : 0;
					key_mapped = 0;
				}

				mapping_clear = 0;
				if (mapping_dev >= 0 && !map_skip && (mapping_dev == dev || clear) && mapping_button < (is_menu() ? (mapping_type ? SYS_BTN_CNT_ESC + 1 : SYS_BTN_OSD_KTGL + 1) : mapping_count))
				{
					if (ev->value == 1 && !key_mapped)
					{
						if (is_menu())
						{
							if (mapping_dev == dev && !(!mapping_button && last_axis && ((ev->code == last_axis) || (ev->code == last_axis + 1))))
							{
								if (!mapping_button) memset(input[dev].map, 0, sizeof(input[dev].map));
								input[dev].osd_combo = 0;

								int found = 0;
								if (mapping_button < SYS_BTN_CNT_OK)
								{
									for (int i = (mapping_button >= BUTTON_DPAD_COUNT) ? BUTTON_DPAD_COUNT : 0; i < mapping_button; i++) if (input[dev].map[i] == ev->code) found = 1;
								}

								if (!found || (mapping_button == SYS_BTN_OSD_KTGL && mapping_type))
								{
									if (mapping_button == SYS_BTN_CNT_OK) input[dev].map[SYS_BTN_MENU_FUNC] = ev->code & 0xFFFF;
									else if (mapping_button == SYS_BTN_CNT_ESC) input[dev].map[SYS_BTN_MENU_FUNC] = (ev->code << 16) | input[dev].map[SYS_BTN_MENU_FUNC];
									else if (mapping_button == SYS_BTN_OSD_KTGL)
									{
										input[dev].map[SYS_BTN_OSD_KTGL + mapping_type] = ev->code;
										input[dev].map[SYS_BTN_OSD_KTGL + 2] = input[dev].map[SYS_BTN_OSD_KTGL + 1];
									}
									else input[dev].map[mapping_button] = ev->code;

									key_mapped = ev->code;

									//check if analog stick has been used for mouse
									if (mapping_button == BUTTON_DPAD_COUNT + 1 || mapping_button == BUTTON_DPAD_COUNT + 3)
									{
										if (input[dev].map[mapping_button] >= KEY_EMU &&
											input[dev].map[mapping_button - 1] >= KEY_EMU &&
											(input[dev].map[mapping_button - 1] - input[dev].map[mapping_button] == 1) && // same axis
											absinfo)
										{
											input[dev].map[SYS_AXIS_MX + (mapping_button - (BUTTON_DPAD_COUNT + 1)) / 2] = ((input[dev].map[mapping_button] - KEY_EMU) / 2) | 0x20000;
										}
									}
								}
							}
						}
						else
						{
							if (clear)
							{
								memset(input[mapping_dev].map, 0, sizeof(input[mapping_dev].map));
								mapping_button = 0;
								mapping_clear = 1;
							}
							else
							{
								if (!mapping_button)
								{
									for (uint i = 0; i < sizeof(input[0].map) / sizeof(input[0].map[0]); i++)
									{
										input[dev].map[i] &= mapping_set ? 0x0000FFFF : 0xFFFF0000;
									}
								}

								int found = 0;
								for (int i = 0; i < mapping_button; i++)
								{
									if (mapping_set && (input[dev].map[i] >> 16) == ev->code) found = 1;
									if (!mapping_set && (input[dev].map[i] & 0xFFFF) == ev->code) found = 1;
								}

								if (!found)
								{
									if (mapping_set) input[dev].map[mapping_button] = (input[dev].map[mapping_button] & 0xFFFF) | (ev->code << 16);
									else input[dev].map[mapping_button] = (input[dev].map[mapping_button] & 0xFFFF0000) | ev->code;
									key_mapped = ev->code;
								}
							}
						}
					}
					//combo for osd button
					if (ev->value == 1 && key_mapped && key_mapped != ev->code && is_menu() && mapping_button == SYS_BTN_OSD_KTGL && mapping_type)
					{
						input[dev].map[SYS_BTN_OSD_KTGL + 2] = ev->code;
						printf("Set combo: %x + %x\n", input[dev].map[SYS_BTN_OSD_KTGL + 1], input[dev].map[SYS_BTN_OSD_KTGL + 2]);
					}
					else if(mapping_dev == dev && ev->value == 0 && key_mapped == ev->code)
					{
						mapping_button++;
						key_mapped = 0;
					}

					if(!ev->value && mapping_dev == dev && ((ev->code == last_axis) || (ev->code == last_axis+1)))
					{
						last_axis = 0;
					}
				}
			}
		}
		else if (is_menu())
		{
			//Define min-0-max analogs
			switch (mapping_button)
			{
				case 23: idx = SYS_AXIS_X;  break;
				case 24: idx = SYS_AXIS_Y;  break;
				case -4: idx = SYS_AXIS1_X; break;
				case -3: idx = SYS_AXIS1_Y; break;
				case -2: idx = SYS_AXIS2_X; break;
				case -1: idx = SYS_AXIS2_Y; break;
			}

			if (mapping_dev == dev || (mapping_dev < 0 && mapping_button < 0))
			{
				int max = 0; // , min = 0;

				if (ev->type == EV_ABS)
				{
					int threshold = (absinfo->maximum - absinfo->minimum) / 5;

					max = (ev->value >= (absinfo->maximum - threshold));
					//min = (ev->value <= (absinfo->minimum + threshold));
					//printf("threshold=%d, min=%d, max=%d\n", threshold, min, max);
				}

				//check DPAD horz
				if (mapping_button == -6)
				{
					last_axis = 0;
					if (ev->type == EV_ABS && max)
					{
						if (mapping_dev < 0) mapping_dev = dev;
						mapping_type = 1;

						if (absinfo->maximum > 2)
						{
							tmp_axis[tmp_axis_n++] = ev->code | 0x20000;
							mapping_button++;
						}
						else
						{
							//Standard DPAD event
							mapping_button += 2;
						}
					}
					else if (ev->type == EV_KEY && ev->value == 1)
					{
						//DPAD uses simple button events
						if (!map_skip)
						{
							mapping_button += 2;
							if (mapping_dev < 0) mapping_dev = dev;
							if (ev->code < 256)
							{
								// keyboard, skip stick 1/2
								mapping_button += 4;
								mapping_type = 0;
							}
						}
					}
				}
				//check DPAD vert
				else if (mapping_button == -5)
				{
					if (ev->type == EV_ABS && max && absinfo->maximum > 1 && ev->code != (tmp_axis[0] & 0xFFFF))
					{
						tmp_axis[tmp_axis_n++] = ev->code | 0x20000;
						mapping_button++;
					}
				}
				//Sticks
				else if (ev->type == EV_ABS && idx)
				{
					if (mapping_dev < 0) mapping_dev = dev;

					if (idx && max && absinfo->maximum > 2)
					{
						if (mapping_button < 0)
						{
							int found = 0;
							for (int i = 0; i < tmp_axis_n; i++) if (ev->code == (tmp_axis[i] & 0xFFFF)) found = 1;
							if (!found)
							{
								mapping_type = 1;
								tmp_axis[tmp_axis_n++] = ev->code | 0x20000;
								//if (min) tmp_axis[idx - AXIS1_X] |= 0x10000;
								mapping_button++;
								if (tmp_axis_n >= 4) mapping_button = 0;
								last_axis = KEY_EMU + (ev->code << 1);
							}
						}
						else
						{
							if (idx == SYS_AXIS_X || ev->code != (input[dev].map[idx - 1] & 0xFFFF))
							{
								input[dev].map[idx] = ev->code | 0x20000;
								//if (min) input[dev].map[idx] |= 0x10000;
								mapping_button++;
							}
						}
					}
				}
			}
		}

		while (mapping_type <= 1 && mapping_button < mapping_count)
		{
			if (map_skip)
			{
				if (map_skip == 2 || ev->value == 1)
				{
					if (mapping_dev >= 0)
					{
						if (idx) input[mapping_dev].map[idx] = 0;
						else if (mapping_button > 0)
						{
							if (!is_menu()) input[mapping_dev].map[mapping_button] &= mapping_set ? 0x0000FFFF : 0xFFFF0000;
						}
					}
					last_axis = 0;
					mapping_button++;
					if (mapping_button < 0 && (mapping_button & 1)) mapping_button++;
				}
			}

			map_skip = 0;
			if (mapping_button >= 4 && !is_menu() && !strcmp(joy_bnames[mapping_button - 4], "-")) map_skip = 2;
			if (!map_skip) break;
		}

		if (is_menu() && mapping_type <= 1 && mapping_dev >= 0)
		{
			memcpy(&input[mapping_dev].mmap[SYS_AXIS1_X], tmp_axis, sizeof(tmp_axis));
			memcpy(&input[mapping_dev].map[SYS_AXIS1_X], tmp_axis, sizeof(tmp_axis));
		}
	}
	else
	{
		key_mapped = 0;
		switch (ev->type)
		{
		case EV_KEY:
			if (ev->code < 1024 && input[dev].jkmap[ev->code] && !user_io_osd_is_visible()) ev->code = input[dev].jkmap[ev->code];

			//joystick buttons, digital directions
			if (ev->code >= 256)
			{
				if (input[dev].lightgun_req && !user_io_osd_is_visible())
				{
					if (osd_event == 1)
					{
						input[dev].lightgun = !input[dev].lightgun;
						Info(input[dev].lightgun ? "Light Gun mode is ON" : "Light Gun mode is OFF");
					}
				}
				else
				{
					if (osd_event == 1) joy_digital(input[dev].num, 0, 0, 1, BTN_OSD);
					if (osd_event == 2) joy_digital(input[dev].num, 0, 0, 0, BTN_OSD);
				}

				if (user_io_osd_is_visible() || video_fb_state())
				{
					if (ev->value <= 1)
					{
						if ((input[dev].mmap[SYS_BTN_MENU_FUNC] & 0xFFFF) ?
							(ev->code == (input[dev].mmap[SYS_BTN_MENU_FUNC] & 0xFFFF)) :
							(ev->code == input[dev].mmap[SYS_BTN_A]))
						{
							joy_digital(0, JOY_BTN1, 0, ev->value, 0);
							return;
						}

						if ((input[dev].mmap[SYS_BTN_MENU_FUNC] >> 16) ?
							(ev->code == (input[dev].mmap[SYS_BTN_MENU_FUNC] >> 16)) :
							(ev->code == input[dev].mmap[SYS_BTN_B]))
						{
							joy_digital(0, JOY_BTN2, 0, ev->value, 0);
							return;
						}

						if (ev->code == input[dev].mmap[SYS_BTN_SELECT])
						{
							struct input_event key_ev = *ev;
							key_ev.code = KEY_GRAVE;
							input_cb(&key_ev, 0, 0);
							return;
						}

						for (int i = 0; i < SYS_BTN_A; i++)
						{
							if (ev->code == input[dev].mmap[i])
							{
								joy_digital(0, 1 << i, 0, ev->value, i);
								return;
							}
						}

						for (int i = SYS_MS_RIGHT; i <= SYS_MS_UP; i++)
						{
							if (ev->code == input[dev].mmap[i])
							{
								int n = i - SYS_MS_RIGHT;
								joy_digital(0, 1 << n, 0, ev->value, n);
								return;
							}
						}

						if (input[dev].mmap[SYS_AXIS_X])
						{
							uint16_t key = KEY_EMU + ((uint16_t)input[dev].mmap[SYS_AXIS_X]*2);
							if (ev->code == (key + 1)) joy_digital(0, 1 << 0, 0, ev->value, 0);
							if (ev->code == key) joy_digital(0, 1 << 1, 0, ev->value, 1);
						}

						if (input[dev].mmap[SYS_AXIS_Y])
						{
							uint16_t key = KEY_EMU + ((uint16_t)input[dev].mmap[SYS_AXIS_Y]*2);
							if (ev->code == (key + 1)) joy_digital(0, 1 << 2, 0, ev->value, 2);
							if (ev->code == key) joy_digital(0, 1 << 3, 0, ev->value, 3);
						}
					}
				}
				else
				{
					if (mouse_emu)
					{
						int use_analog = (input[dev].mmap[SYS_AXIS_MX] || input[dev].mmap[SYS_AXIS_MY]);

						for (int i = (use_analog ? SYS_MS_BTN_L : SYS_MS_RIGHT); i <= SYS_MS_BTN_M; i++)
						{
							if (ev->code == input[dev].mmap[i])
							{
								switch (i)
								{
								case SYS_MS_RIGHT:
									mouse_emu_x = ev->value ? 10 : 0;
									break;
								case SYS_MS_LEFT:
									mouse_emu_x = ev->value ? -10 : 0;
									break;
								case SYS_MS_DOWN:
									mouse_emu_y = ev->value ? 10 : 0;
									break;
								case SYS_MS_UP:
									mouse_emu_y = ev->value ? -10 : 0;
									break;

								default:
									mouse_btn = ev->value ? mouse_btn | 1 << (i - SYS_MS_BTN_L) : mouse_btn & ~(1 << (i - SYS_MS_BTN_L));
									mouse_cb(mouse_btn | mice_btn);
									break;
								}
								return;
							}
						}
					}

					if (input[dev].has_map >= 2)
					{
						if (input[dev].has_map == 3) Info("This joystick is not defined");
						input[dev].has_map = 1;
					}

					for (uint i = 0; i < BTN_NUM; i++)
					{
						if (ev->code == (input[dev].map[i] & 0xFFFF) || ev->code == (input[dev].map[i] >> 16))
						{
							if (i <= 3 && origcode == ev->code) origcode = 0; // prevent autofire for original dpad
							if (ev->value <= 1) joy_digital(input[dev].num, 1 << i, origcode, ev->value, i, (ev->code == input[dev].mmap[SYS_BTN_OSD_KTGL + 1] || ev->code == input[dev].mmap[SYS_BTN_OSD_KTGL + 2]));

							// support 2 simultaneous functions for 1 button if defined in 2 sets. No return.
						}
					}

					if (ev->code == input[dev].mmap[SYS_MS_BTN_EMU] && (ev->value <= 1) && ((!(mouse_emu & 1)) ^ (!ev->value)))
					{
						mouse_emu = ev->value ? mouse_emu | 1 : mouse_emu & ~1;
						if (input[sub_dev].quirk == QUIRK_DS4) ds_mouse_emu = mouse_emu & 1;
						printf("mouse_emu = %d\n", mouse_emu);
						if (mouse_emu & 2)
						{
							mouse_sniper = ev->value;
						}
						else
						{
							mouse_timer = 0;
							mouse_btn = 0;
							mouse_emu_x = 0;
							mouse_emu_y = 0;
							mouse_cb(mice_btn);
						}
					}
				}
				return;
			}
			// keyboard
			else
			{
				if (!input[dev].has_kbdmap)
				{
					if (!FileLoadConfig(get_kbdmap_name(dev), &input[dev].kbdmap, sizeof(input[dev].kbdmap)))
					{
						memset(input[dev].kbdmap, 0, sizeof(input[dev].kbdmap));
					}
					input[dev].has_kbdmap = 1;
				}

				uint16_t code = ev->code;
				if (code < 256 && input[dev].kbdmap[code]) code = input[dev].kbdmap[code];

				//  replace MENU key by RGUI to allow using Right Amiga on reduced keyboards
				// (it also disables the use of Menu for OSD)
				if (cfg.key_menu_as_rgui && code == 139) code = 126;

				//Keyrah v2: USB\VID_18D8&PID_0002\A600/A1200_MULTIMEDIA_EXTENSION_VERSION
				int keyrah = (cfg.keyrah_mode && (((((uint32_t)input[dev].vid) << 16) | input[dev].pid) == cfg.keyrah_mode));
				if (keyrah) code = keyrah_trans(code, ev->value);

				uint32_t ps2code = get_ps2_code(code);
				if (ev->value) modifier |= ps2code;
				else modifier &= ~ps2code;

				uint16_t reset_m = (modifier & MODMASK) >> 8;
				if (code == 111) reset_m |= 0x100;
				user_io_check_reset(reset_m, (keyrah && !cfg.reset_combo) ? 1 : cfg.reset_combo);

				if(!user_io_osd_is_visible() && ((user_io_get_kbdemu() == EMU_JOY0) || (user_io_get_kbdemu() == EMU_JOY1)) && !video_fb_state())
				{
					if (!kbd_toggle)
					{
						for (uint i = 0; i < BTN_NUM; i++)
						{
							if (ev->code == input[dev].map[i])
							{
								if (i <= 3 && origcode == ev->code) origcode = 0; // prevent autofire for original dpad
								if (ev->value <= 1) joy_digital((user_io_get_kbdemu() == EMU_JOY0) ? 1 : 2, 1 << i, origcode, ev->value, i);
								return;
							}
						}
					}

					if (ev->code == input[dev].mmap[SYS_BTN_OSD_KTGL])
					{
						if (ev->value <= 1) joy_digital((user_io_get_kbdemu() == EMU_JOY0) ? 1 : 2, 0, 0, ev->value, BTN_TGL);
						return;
					}
				}
				else
				{
					kbd_toggle = 0;
				}

				if (!user_io_osd_is_visible() && (user_io_get_kbdemu() == EMU_MOUSE))
				{
					if (kbd_mouse_emu)
					{
						for (int i = SYS_MS_RIGHT; i <= SYS_MS_BTN_M; i++)
						{
							if (ev->code == input[dev].mmap[i])
							{
								switch (i)
								{
								case SYS_MS_RIGHT:
									mouse_emu_x = ev->value ? 10 : 0;
									break;
								case SYS_MS_LEFT:
									mouse_emu_x = ev->value ? -10 : 0;
									break;
								case SYS_MS_DOWN:
									mouse_emu_y = ev->value ? 10 : 0;
									break;
								case SYS_MS_UP:
									mouse_emu_y = ev->value ? -10 : 0;
									break;

								default:
									mouse_btn = ev->value ? mouse_btn | 1 << (i - SYS_MS_BTN_L) : mouse_btn & ~(1 << (i - SYS_MS_BTN_L));
									mouse_cb(mouse_btn | mice_btn);
									break;
								}
								return;
							}
						}

						if (ev->code == input[dev].mmap[SYS_MS_BTN_EMU])
						{
							if (ev->value <= 1) mouse_sniper = ev->value;
							return;
						}
					}

					if (ev->code == input[dev].mmap[SYS_BTN_OSD_KTGL])
					{
						if (ev->value == 1)
						{
							kbd_mouse_emu = !kbd_mouse_emu;
							printf("kbd_mouse_emu = %d\n", kbd_mouse_emu);

							mouse_timer = 0;
							mouse_btn = 0;
							mouse_emu_x = 0;
							mouse_emu_y = 0;
							mouse_cb(mice_btn);
						}
						return;
					}
				}

				if (code == KEY_HOMEPAGE) code = KEY_MENU;
				user_io_kbd(code, ev->value);
				return;
			}
			break;

		//analog joystick
		case EV_ABS:
			if (!user_io_osd_is_visible())
			{
				int value = ev->value;
				if (ev->value < absinfo->minimum) value = absinfo->minimum;
				else if (ev->value > absinfo->maximum) value = absinfo->maximum;

				if (ev->code == 8)
				{
					if (input[dev].num && input[dev].num <= NUMPLAYERS)
					{
						value -= absinfo->minimum;
						value = (value * 255) / (absinfo->maximum - absinfo->minimum);
						user_io_analog_joystick(((input[dev].num - 1) << 4) | 0xF, value, 0);
					}
					break;
				}

				int hrange = (absinfo->maximum - absinfo->minimum) / 2;
				int dead = hrange/63;

				if (input[sub_dev].quirk == QUIRK_CWIID)
				{
					if(ev->code == 3 || ev->code == 4) dead = 10;
				}

				if (input[sub_dev].quirk == QUIRK_DS3 || input[sub_dev].quirk == QUIRK_DS4)
				{
					dead = 10;
				}

				// normalize to -range/2...+range/2
				value = value - (absinfo->minimum + absinfo->maximum) / 2;

				if (ev->code > 1 || !input[dev].lightgun) //lightgun has no dead zone
				{
					// check the dead-zone and remove it from the range
					hrange -= dead;
					if (value < -dead) value += dead;
					else if (value > dead) value -= dead;
					else value = 0;
				}

				value = (value * 127) / hrange;

				//final check to eliminate additive error
				if (value < -127) value = -127;
				else if (value > 127) value = 127;

				if (input[sub_dev].axis_pos[ev->code & 0xFF] == (int8_t)value) break;
				input[sub_dev].axis_pos[ev->code & 0xFF] = (int8_t)value;

				if (ev->code == (input[dev].mmap[SYS_AXIS_MX] & 0xFFFF) && mouse_emu)
				{
					mouse_emu_x = 0;
					if (value < -1 || value>1) mouse_emu_x = value;
					mouse_emu_x /= 12;
					return;
				}
				else if (ev->code == (input[dev].mmap[SYS_AXIS_MY] & 0xFFFF) && mouse_emu)
				{
					mouse_emu_y = 0;
					if (value < -1 || value>1) mouse_emu_y = value;
					mouse_emu_y /= 12;
					return;
				}
				else if (ev->code == (input[dev].mmap[SYS_AXIS_X] & 0xFFFF) || (ev->code == 0 && input[dev].lightgun))
				{
					// skip if joystick is undefined.
					if (!input[dev].num) break;

					int offset = 0;
					if (value < -1 || value>1) offset = value;
					//printf("analog_x = %d\n", offset);
					joy_analog(input[dev].num, 0, offset);
					return;
				}
				else if (ev->code == (input[dev].mmap[SYS_AXIS_Y] & 0xFFFF) || (ev->code == 1 && input[dev].lightgun))
				{
					// skip if joystick is undefined.
					if (!input[dev].num) break;

					int offset = 0;
					if (value < -1 || value>1) offset = value;
					//printf("analog_y = %d\n", offset);
					joy_analog(input[dev].num, 1, offset);
					return;
				}
			}
			break;

		// spinner
		case EV_REL:
			if (!user_io_osd_is_visible() && ev->code == 7)
			{
				if (input[dev].num && input[dev].num <= NUMPLAYERS)
				{
					int value = ev->value;
					if (ev->value < -128) value = -128;
					else if (ev->value > 127) value = 127;

					user_io_analog_joystick(((input[dev].num - 1) << 4) | 0x8F, value, 0);
				}
			}
			break;
		}
	}
}

void send_map_cmd(int key)
{
	if (mapping && mapping_dev >= 0)
	{
		input_event ev;
		ev.type = EV_KEY;
		ev.code = key;
		ev.value = 1;
		input_cb(&ev, 0, mapping_dev);
	}
}

#define CMD_FIFO "/dev/MiSTer_cmd"
#define LED_MONITOR "/sys/class/leds/hps_led0/brightness_hw_changed"

static struct pollfd pool[NUMDEV + 3];

void mergedevs()
{
	for (int i = 0; i < NUMDEV; i++)
	{
		memset(input[i].id, 0, sizeof(input[i].id));
	}

	FILE *f = fopen("/proc/bus/input/devices", "r");
	if (!f)
	{
		printf("Failed to open /proc/bus/input/devices\n");
		return;
	}

	static char str[1024];
	char id[32] = {};
	while (fgets(str, sizeof(str), f))
	{
		int len = strlen(str);
		if (!len) id[0] = 0;
		else
		{
			if (!strncmp("S: ", str, 3))
			{
				char *p = strcasestr(str, "/input/");
				if (p)
				{
					*p = 0;
					p = strrchr(str, '/');
					if (p)
					{
						p++;
						int len = strlen(p);
						if (len > 30) p += len - 30;
						strcpy(id, p);
					}
				}
			}
			else if (!strncmp("H: ", str, 3) && id[0])
			{
				char *handlers = strchr(str, '=');
				if (handlers)
				{
					handlers++;
					for (int i = 0; i < NUMDEV; i++)
					{
						if (pool[i].fd >= 0)
						{
							char *dev = strrchr(input[i].devname, '/');
							if (dev)
							{
								char idsp[32];
								strcpy(idsp, dev+1);
								strcat(idsp, " ");
								if (strstr(handlers, idsp)) strcpy(input[i].id, id);
							}
						}
					}
				}
			}
		}
	}

	fclose(f);

	// merge multifunctional devices by id
	for (int i = 0; i < NUMDEV; i++)
	{
		//Bypass merging of specified 2 port/player controllers
		if (input[i].vid == 0x289B) // Raphnet uses buggy firmware, don't merge it.
			continue;
		else if(input[i].vid == 0x0E8F) //Vendor -Mayflash
		{
			if(input[i].pid == 0x3013)  //SNES controller 2 port adapter
				continue;
		}
		else if(input[i].vid == 0x16C0) //Vendor - XinMo
		{
			if(input[i].pid == 0x05E1) //XM-10 2 player USB Encoder
				continue;
		}

		input[i].bind = i;
		if (input[i].id[0] && !input[i].mouse)
		{
			for (int j = 0; j < i; j++)
			{
				if (!strcmp(input[i].id, input[j].id))
				{
					input[i].bind = j;
					break;
				}
			}
		}
	}

	//copy missing fields to mouseX
	for (int i = 0; i < NUMDEV; i++) if (input[i].mouse)
	{
		for (int j = 0; j < NUMDEV; j++) if (!input[j].mouse)
		{
			if (!strcmp(input[i].id, input[j].id))
			{
				input[i].bind = j;
				input[i].vid = input[j].vid;
				input[i].pid = input[j].pid;
				input[i].quirk = input[j].quirk;
				memcpy(input[i].name, input[j].name, sizeof(input[i].name));
				memcpy(input[i].idstr, input[j].idstr, sizeof(input[i].idstr));
				break;
			}
		}
	}
}

// jammasd have shifted keys: when 1P start is kept pressed, it acts as a shift key,
// outputting other key signals. Example: 1P start + 2P start = KEY_ESC
// Shifted keys are passed as normal keyboard keys.
static struct
{
	uint16_t key;
	uint16_t player;
	uint16_t btn;
} jammasd2joy[] =
{
	{KEY_5,         1, 0x120}, // 1P coin
	{KEY_1,         1, 0x121}, // 1P start (shift key)
	{KEY_UP,        1, 0x122}, // 1P up
	{KEY_DOWN,      1, 0x123}, // 1P down
	{KEY_LEFT,      1, 0x124}, // 1P left
	{KEY_RIGHT,     1, 0x125}, // 1P right
	{KEY_LEFTCTRL,  1, 0x126}, // 1P 1
	{KEY_LEFTALT,   1, 0x127}, // 1P 2
	{KEY_SPACE,     1, 0x128}, // 1P 3
	{KEY_LEFTSHIFT, 1, 0x129}, // 1P 4
	{KEY_Z,         1, 0x12A}, // 1P 5
	{KEY_X,         1, 0x12B}, // 1P 6
	{KEY_C,         1, 0x12C}, // 1P 7
	{KEY_V,         1, 0x12D}, // 1P 8
	{KEY_9,         1, 0x12E}, // Test
	{KEY_F2,        1, 0x12F}, // service

	{KEY_6,         2, 0x120}, // 2P coin
	{KEY_2,         2, 0x121}, // 2P start
	{KEY_R,         2, 0x122}, // 2P up
	{KEY_F,         2, 0x123}, // 2P down
	{KEY_D,         2, 0x124}, // 2P left
	{KEY_G,         2, 0x125}, // 2P right
	{KEY_A,         2, 0x126}, // 2P 1
	{KEY_S,         2, 0x127}, // 2P 2
	{KEY_Q,         2, 0x128}, // 2P 3
	{KEY_W,         2, 0x129}, // 2P 4
	{KEY_I,         2, 0x12A}, // 2P 5
	{KEY_K,         2, 0x12B}, // 2P 6
	{KEY_J,         2, 0x12C}, // 2P 7
	{KEY_L,         2, 0x12D}, // 2P 8
};

// J-PAC/I-PAC has shifted keys: when 1P start is kept pressed, it acts as a shift key,
// outputting other key signals. Example: 1P start + 2P start = KEY_ESC
// Shifted keys are passed as normal keyboard keys.
static struct
{
	uint16_t key;
	uint16_t player;
	uint16_t btn;
} jpac2joy[] =
{
	{KEY_5,         1, 0x120}, // 1P coin (shift + 1P 1)
	{KEY_1,         1, 0x121}, // 1P start (shift key)
	{KEY_UP,        1, 0x122}, // 1P up
	{KEY_DOWN,      1, 0x123}, // 1P down
	{KEY_LEFT,      1, 0x124}, // 1P left
	{KEY_RIGHT,     1, 0x125}, // 1P right
	{KEY_LEFTCTRL,  1, 0x126}, // 1P 1
	{KEY_LEFTALT,   1, 0x127}, // 1P 2
	{KEY_SPACE,     1, 0x128}, // 1P 3
	{KEY_LEFTSHIFT, 1, 0x129}, // 1P 4
	{KEY_Z,         1, 0x12A}, // 1P 5
	{KEY_X,         1, 0x12B}, // 1P 6
	{KEY_C,         1, 0x12C}, // 1P 7
	{KEY_V,         1, 0x12D}, // 1P 8
	{KEY_TAB,       1, 0x12E}, // Tab (shift + 1P right)
	{KEY_ENTER,     1, 0x12F}, // Enter (shift + 1P left)
	// ~ Tidle supportted?
	{KEY_P,         1, 0x131}, // P (pause) (shift + 1P down)
	{KEY_F1,        1, 0x132}, // Service
	{KEY_F2,        1, 0x133}, // Test
	{KEY_F3,        1, 0x134}, // Tilt

	{KEY_6,         2, 0x120}, // 2P coin
	{KEY_2,         2, 0x121}, // 2P start
	{KEY_R,         2, 0x122}, // 2P up
	{KEY_F,         2, 0x123}, // 2P down
	{KEY_D,         2, 0x124}, // 2P left
	{KEY_G,         2, 0x125}, // 2P right
	{KEY_A,         2, 0x126}, // 2P 1
	{KEY_S,         2, 0x127}, // 2P 2
	{KEY_Q,         2, 0x128}, // 2P 3
	{KEY_W,         2, 0x129}, // 2P 4
	{KEY_I,         2, 0x12A}, // 2P 5
	{KEY_K,         2, 0x12B}, // 2P 6
	{KEY_J,         2, 0x12C}, // 2P 7
	{KEY_L,         2, 0x12D}, // 2P 8
};

int input_test(int getchar)
{
	static char cur_leds = 0;
	static int state = 0;
	struct input_absinfo absinfo;
	struct input_event ev;

	if (state == 0)
	{
		input_uinp_setup();
		memset(pool, -1, sizeof(pool));

		signal(SIGINT, INThandler);
		pool[NUMDEV].fd = set_watch();
		pool[NUMDEV].events = POLLIN;

		unlink(CMD_FIFO);
		mkfifo(CMD_FIFO, 0666);

		pool[NUMDEV+1].fd = open(CMD_FIFO, O_RDWR | O_NONBLOCK);
		pool[NUMDEV+1].events = POLLIN;

		pool[NUMDEV + 2].fd = open(LED_MONITOR, O_RDONLY);
		pool[NUMDEV + 2].events = POLLPRI;

		state++;
	}

	if (state == 1)
	{
		printf("Open up to %d input devices.\n", NUMDEV);
		for (int i = 0; i < NUMDEV; i++)
		{
			pool[i].fd = -1;
			pool[i].events = 0;
		}

		int n = 0;
		DIR *d = opendir("/dev/input");
		if (d)
		{
			struct dirent *de;
			while ((de = readdir(d)))
			{
				if (!strncmp(de->d_name, "event", 5) || !strncmp(de->d_name, "mouse", 5))
				{
					memset(&input[n], 0, sizeof(input[n]));
					sprintf(input[n].devname, "/dev/input/%s", de->d_name);
					int fd = open(input[n].devname, O_RDWR);
					//printf("open(%s): %d\n", input[n].devname, fd);

					if (fd > 0)
					{
						pool[n].fd = fd;
						pool[n].events = POLLIN;
						input[n].mouse = !strncmp(de->d_name, "mouse", 5);

						char uniq[32] = {};
						if (!input[n].mouse)
						{
							struct input_id id;
							memset(&id, 0, sizeof(id));
							ioctl(pool[n].fd, EVIOCGID, &id);
							input[n].vid = id.vendor;
							input[n].pid = id.product;

							ioctl(pool[n].fd, EVIOCGUNIQ(sizeof(uniq)), uniq);
							ioctl(pool[n].fd, EVIOCGNAME(sizeof(input[n].name)), input[n].name);
							input[n].led = has_led(pool[n].fd);
						}

						//skip our virtual device
						if (!strcmp(input[n].name, UINPUT_NAME))
						{
							close(pool[n].fd);
							pool[n].fd = -1;
							continue;
						}

						input[n].bind = -1;

						// enable scroll wheel reading
						if (input[n].mouse)
						{
							unsigned char buffer[4];
							static const unsigned char mousedev_imps_seq[] = { 0xf3, 200, 0xf3, 100, 0xf3, 80 };
							if (write(pool[n].fd, mousedev_imps_seq, sizeof(mousedev_imps_seq)) != sizeof(mousedev_imps_seq))
							{
								printf("Cannot switch %s to ImPS/2 protocol(1)\n", input[n].devname);
							}
							else if (read(pool[n].fd, buffer, sizeof buffer) != 1 || buffer[0] != 0xFA)
							{
								printf("Failed to switch %s to ImPS/2 protocol(2)\n", input[n].devname);
							}
						}

						if (strcasestr(input[n].name, "Wiimote") && input[n].vid == 1 && input[n].pid == 1)
						{
							input[n].quirk = QUIRK_CWIID;
							input[n].lightgun = 1;
						}

						if (input[n].vid == 0x054c)
						{
							if (input[n].pid == 0x0268)  input[n].quirk = QUIRK_DS3;
							else if (input[n].pid == 0x05c4 || input[n].pid == 0x09cc || input[n].pid == 0x0ba0)
							{
								input[n].quirk = QUIRK_DS4;
								if (strcasestr(input[n].name, "Touchpad"))
								{
									input[n].quirk = QUIRK_DS4TOUCH;
								}
							}
						}

						if (input[n].vid == 0x0079 && input[n].pid == 0x1802)
						{
							input[n].lightgun = 1;
							input[n].num = 2; // force mayflash mode 1/2 as second joystick.
						}

						if (input[n].vid == 0x057e && (input[n].pid == 0x0306 || input[n].pid == 0x0330))
						{
							if (strcasestr(input[n].name, "Accelerometer"))
							{
								// don't use Accelerometer
								close(pool[n].fd);
								pool[n].fd = -1;
								continue;
							}
							else if (strcasestr(input[n].name, "Motion Plus"))
							{
								// don't use Accelerometer
								close(pool[n].fd);
								pool[n].fd = -1;
								continue;
							}
							else
							{
								input[n].quirk = QUIRK_WIIMOTE;
								input[n].guncal[0] = 0;
								input[n].guncal[1] = 767;
								input[n].guncal[2] = 1;
								input[n].guncal[3] = 1023;
								FileLoadConfig("wiimote_cal.cfg", input[n].guncal, 4 * sizeof(uint16_t));
							}
						}

						//Ultimarc lightgun
						if (input[n].vid == 0xd209 && input[n].pid == 0x1601)
						{
							input[n].lightgun = 1;
						}

						//Madcatz Arcade Stick 360
						if (input[n].vid == 0x0738 && input[n].pid == 0x4758) input[n].quirk = QUIRK_MADCATZ360;

						// mr.Spinner
						// 0x120  - Button
						// Axis 7 - EV_REL is spinner
						// Axis 8 - EV_ABS is Paddle
						// Overlays on other existing gamepads
						if (strstr(uniq, "MiSTer-S1")) input[n].quirk = QUIRK_PDSP;

						// Arcade with spinner and/or paddle:
						// Axis 7 - EV_REL is spinner
						// Axis 8 - EV_ABS is Paddle
						// Includes other buttons and axes, works as a full featured gamepad.
						if (strstr(uniq, "MiSTer-A1")) input[n].quirk = QUIRK_PDSP_ARCADE;

						//JammaSD
						if (cfg.jammasd_vid && cfg.jammasd_pid && input[n].vid == cfg.jammasd_vid && input[n].pid == cfg.jammasd_pid)
						{
							input[n].quirk = QUIRK_JAMMASD;
						}

						//J-PAC/I-PAC
						if (cfg.jpac_vid && cfg.jpac_pid && input[n].vid == cfg.jpac_vid && input[n].pid == cfg.jpac_pid)
						{
							input[n].quirk = QUIRK_JPAC;
						}

						//Arduino and Teensy devices may share the same VID:PID, so additional field UNIQ is used to differentiate them
						if ((input[n].vid == 0x2341 || (input[n].vid == 0x16C0 && (input[n].pid>>8) == 0x4)) && strlen(uniq))
						{
							snprintf(input[n].idstr, sizeof(input[n].idstr), "%04x_%04x_%s", input[n].vid, input[n].pid, uniq);
							char *p;
							while ((p = strchr(input[n].idstr, '/'))) *p = '_';
							while ((p = strchr(input[n].idstr, ' '))) *p = '_';
							while ((p = strchr(input[n].idstr, '*'))) *p = '_';
							while ((p = strchr(input[n].idstr, ':'))) *p = '_';
							strcpy(input[n].name, uniq);
						}
						else
						{
							snprintf(input[n].idstr, sizeof(input[n].idstr), "%04x_%04x", input[n].vid, input[n].pid);
						}

						ioctl(pool[n].fd, EVIOCGRAB, (grabbed | user_io_osd_is_visible()) ? 1 : 0);

						n++;
						if (n >= NUMDEV) break;
					}
				}
			}
			closedir(d);

			mergedevs();
			for (int i = 0; i < n; i++)
			{
				printf("opened %d(%2d): %s (%04x:%04x) %d \"%s\" \"%s\"\n", i, input[i].bind, input[i].devname, input[i].vid, input[i].pid, input[i].quirk, input[i].id, input[i].name);
			}
		}
		cur_leds |= 0x80;
		state++;
	}

	if (state == 2)
	{
		int timeout = 0;
		if (is_menu() && video_fb_state()) timeout = 25;

		while (1)
		{
			int return_value = poll(pool, NUMDEV + 3, timeout);
			if (!return_value) break;

			if (return_value < 0)
			{
				printf("ERR: poll\n");
				break;
			}

			if ((pool[NUMDEV].revents & POLLIN) && check_devs())
			{
				printf("Close all devices.\n");
				for (int i = 0; i < NUMDEV; i++) if (pool[i].fd >= 0)
				{
					ioctl(pool[i].fd, EVIOCGRAB, 0);
					close(pool[i].fd);
				}
				state = 1;
				return 0;
			}

			for (int i = 0; i < NUMDEV; i++)
			{
				if ((pool[i].fd >= 0) && (pool[i].revents & POLLIN))
				{
					if (!input[i].mouse)
					{
						memset(&ev, 0, sizeof(ev));
						if (read(pool[i].fd, &ev, sizeof(ev)) == sizeof(ev))
						{
							if (getchar)
							{
								if (ev.type == EV_KEY && ev.value >= 1)
								{
									return ev.code;
								}
							}
							else if (ev.type)
							{
								int dev = i;
								if (input[dev].bind >= 0) dev = input[dev].bind;

								int noabs = 0;

								if (input[i].quirk == QUIRK_DS4TOUCH && ev.type == EV_KEY)
								{
									if (ev.code == BTN_TOOL_FINGER || ev.code == BTN_TOUCH || ev.code == BTN_TOOL_DOUBLETAP) continue;
								}

								if (input[i].quirk == QUIRK_MADCATZ360 && ev.type == EV_KEY)
								{
									if (ev.code == BTN_THUMBR) input[i].misc_flags = ev.value ? (input[i].misc_flags | 1) : (input[i].misc_flags & ~1);
									else if (ev.code == BTN_MODE && !user_io_osd_is_visible())
									{
										if (input[i].misc_flags & 1)
										{
											if (ev.value)
											{
												if ((input[i].misc_flags & 0x6) == 0) input[i].misc_flags = 0x3; // X
												else if ((input[i].misc_flags & 0x6) == 2) input[i].misc_flags = 0x5; // Y
												else input[i].misc_flags = 0x1; // None

												Info(((input[i].misc_flags & 0x6) == 2) ? "Paddle mode" :
													((input[i].misc_flags & 0x6) == 4) ? "Spinner mode" :
													"Normal mode");
											}
											continue;
										}
									}
								}

								if (ev.type == EV_ABS)
								{
									if (input[i].quirk == QUIRK_WIIMOTE)
									{
										//nunchuck accel events
										if (ev.code >= 3 && ev.code <= 5) continue;
									}

									//Dualshock: drop accelerator and raw touchpad events
									if (input[i].quirk == QUIRK_DS4TOUCH && ev.code == 57)
									{
										input[dev].lightgun_req = (ev.value >= 0);
									}

									if ((input[i].quirk == QUIRK_DS4TOUCH || input[i].quirk == QUIRK_DS4 || input[i].quirk == QUIRK_DS3) && ev.code > 40)
									{
										continue;
									}

									if (ioctl(pool[i].fd, EVIOCGABS(ev.code), &absinfo) < 0) memset(&absinfo, 0, sizeof(absinfo));
									else
									{
										//DS4 specific: touchpad as lightgun
										if (input[i].quirk == QUIRK_DS4TOUCH && ev.code <= 1)
										{
											if (!input[dev].lightgun || user_io_osd_is_visible()) continue;

											if (ev.code == 1)
											{
												absinfo.minimum = 300;
												absinfo.maximum = 850;
											}
											else if (ev.code == 0)
											{
												absinfo.minimum = 200;
												absinfo.maximum = 1720;
											}
											else continue;
										}

										if (input[i].quirk == QUIRK_DS4 && ev.code <= 1)
										{
											if (input[dev].lightgun) noabs = 1;
										}

										if (input[i].quirk == QUIRK_WIIMOTE)
										{
											input[dev].lightgun = 0;
											if (absinfo.maximum == 1023 || absinfo.maximum == 767)
											{
												if (ev.code == 16)
												{
													ev.value = absinfo.maximum - ev.value;
													ev.code = 0;
													input[dev].lightgun = 1;
												}
												else if (ev.code == 17)
												{
													ev.code = 1;
													input[dev].lightgun = 1;
												}
												// other 3 IR tracking aren't used
												else continue;
											}
											else if (absinfo.maximum == 62)
											{
												//LT/RT analog
												continue;
											}
											else if (ev.code & 1)
											{
												//Y axes on wiimote and accessories are inverted
												ev.value = -ev.value;
											}
										}
									}

									if (input[i].quirk == QUIRK_MADCATZ360 && (input[i].misc_flags & 0x6) && (ev.code == 16) && !user_io_osd_is_visible())
									{
										if (ev.value)
										{
											if ((input[i].misc_flags & 0x6) == 2)
											{
												if (ev.value > 0) input[i].mc_a += 4;
												if (ev.value < 0) input[i].mc_a -= 4;

												if (input[i].mc_a > 256) input[i].mc_a = 256;
												if (input[i].mc_a < 0)   input[i].mc_a = 0;

												absinfo.maximum = 255;
												absinfo.minimum = 0;
												ev.code = 8;
												ev.value = input[i].mc_a;
											}
											else
											{
												ev.type = EV_REL;
												ev.code = 7;
											}
										}
										else continue;
									}

									if (input[i].quirk == QUIRK_CWIID)
									{
										if (ev.code == 3 || ev.code == 4)
										{
											absinfo.minimum = 30;
											absinfo.maximum = 225;
										}
									}
								}

								if (input[dev].quirk == QUIRK_JAMMASD && ev.type == EV_KEY)
								{
									input[dev].num = 0;
									for (uint32_t i = 0; i <= sizeof(jammasd2joy) / sizeof(jammasd2joy[0]); i++)
									{
										if (jammasd2joy[i].key == ev.code)
										{
											ev.code = jammasd2joy[i].btn;
											input[dev].num = jammasd2joy[i].player;
											break;
										}
									}
								}

								if (input[dev].quirk == QUIRK_JPAC && ev.type == EV_KEY)
								{
									input[dev].num = 0;
									for (uint32_t i = 0; i <= sizeof(jpac2joy) / sizeof(jpac2joy[0]); i++)
									{
										if (jpac2joy[i].key == ev.code)
										{
											ev.code = jpac2joy[i].btn;
											input[dev].num = jpac2joy[i].player;
											break;
										}
									}
								}

								//Menu combo on 8BitDo receiver in PSC mode
								if (input[dev].vid == 0x054c && input[dev].pid == 0x0cda && ev.type == EV_KEY)
								{
									//in PSC mode these keys coming from separate virtual keyboard device
									//so it's impossible to use joystick codes as keyboards aren't personalized
									if (ev.code == 164) ev.code = KEY_MENU;
									if (ev.code == 1)   ev.code = KEY_MENU;
								}

								if (ev.type == EV_KEY && ev.code == KEY_BACK && input[dev].num)
								{
									ev.code = BTN_SELECT;
								}

								//Menu button quirk of 8BitDo gamepad in X-Input mode
								if (input[dev].vid == 0x045e && input[dev].pid == 0x02e0 && ev.type == EV_KEY)
								{
									if (ev.code == KEY_MENU) ev.code = BTN_MODE;
								}

								if (is_menu() && !video_fb_state())
								{
									/*
									if (mapping && mapping_type <= 1 && !(ev.type==EV_KEY && ev.value>1))
									{
										static char str[64], str2[64];
										OsdWrite(12, "\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81\x81");
										sprintf(str, "     VID=%04X PID=%04X", input[i].vid, input[i].pid);
										OsdWrite(13, str);

										sprintf(str, "Type=%d Code=%d Value=%d", ev.type, ev.code, ev.value);
										str2[0] = 0;
										int len = (29 - (strlen(str))) / 2;
										while (len-- > 0) strcat(str2, " ");
										strcat(str2, str);
										OsdWrite(14, str2);

										str2[0] = 0;
										if (ev.type == EV_ABS)
										{
											sprintf(str, "Min=%d Max=%d", absinfo.minimum, absinfo.maximum);
											int len = (29 - (strlen(str))) / 2;
											while (len-- > 0) strcat(str2, " ");
											strcat(str2, str);
										}
										OsdWrite(15, str2);
									}
									*/

									switch (ev.type)
									{
										//keyboard, buttons
									case EV_KEY:
										printf("Input event: type=EV_KEY, code=%d(0x%x), value=%d, jnum=%d, ID:%04x:%04x:%02d\n", ev.code, ev.code, ev.value, input[dev].num, input[dev].vid, input[dev].pid, i);
										break;

									case EV_REL:
										{
											//limit the amount of EV_REL messages, so Menu core won't be laggy
											static unsigned long timeout = 0;
											if (!timeout || CheckTimer(timeout))
											{
												timeout = GetTimer(20);
												printf("Input event: type=EV_REL, Axis=%d, Offset=%d, jnum=%d, ID:%04x:%04x:%02d\n", ev.code, ev.value, input[dev].num, input[dev].vid, input[dev].pid, i);
											}
										}
										break;

									case EV_SYN:
									case EV_MSC:
										break;

										//analog joystick
									case EV_ABS:
										{
											//limit the amount of EV_ABS messages, so Menu core won't be laggy
											static unsigned long timeout = 0;
											if (!timeout || CheckTimer(timeout))
											{
												timeout = GetTimer(20);

												//reduce flood from DUALSHOCK 3/4
												if ((input[i].quirk == QUIRK_DS4 || input[i].quirk == QUIRK_DS3) && ev.code <= 5 && ev.value > 118 && ev.value < 138)
												{
													break;
												}

												//aliexpress USB encoder floods messages
												if (input[dev].vid == 0x0079 && input[dev].pid == 0x0006)
												{
													if (ev.code == 2) break;
												}

												printf("Input event: type=EV_ABS, Axis=%d, Offset=%d, jnum=%d, ID:%04x:%04x:%02d,", ev.code, ev.value, input[dev].num, input[dev].vid, input[dev].pid, i);
												printf(" abs_min = %d, abs_max = %d", absinfo.minimum, absinfo.maximum);
												if (absinfo.fuzz) printf(", fuzz = %d", absinfo.fuzz);
												if (absinfo.resolution) printf(", res = %d", absinfo.resolution);
												printf("\n");
											}
										}
										break;

									default:
										printf("Input event: type=%d, code=%d(0x%x), value=%d(0x%x), jnum=%d, ID:%04x:%04x:%02d\n", ev.type, ev.code, ev.code, ev.value, ev.value, input[dev].num, input[dev].vid, input[dev].pid, i);
									}
								}

								if (input[i].quirk == QUIRK_CWIID && ev.type == EV_ABS)
								{
									if (ev.code <= 1 && user_io_osd_is_visible())
									{
										// don't pass IR tracking to OSD
										continue;
									}
								}

								if (ev.type == EV_ABS && input[i].quirk == QUIRK_WIIMOTE && input[dev].lightgun)
								{
									menu_lightgun_cb(ev.type, ev.code, ev.value);

									// don't pass IR tracking to OSD
									if (user_io_osd_is_visible()) continue;

									if (!ev.code)
									{
										absinfo.minimum = input[i].guncal[2];
										absinfo.maximum = input[i].guncal[3];
									}
									else
									{
										absinfo.minimum = input[i].guncal[0];
										absinfo.maximum = input[i].guncal[1];
									}
								}

								if (ev.type == EV_KEY && user_io_osd_is_visible())
								{
									if (input[i].quirk == QUIRK_WIIMOTE)
									{
										if (menu_lightgun_cb(ev.type, ev.code, ev.value)) continue;
									}
								}

								if (!noabs) input_cb(&ev, &absinfo, i);

								//sumulate digital directions from analog
								if (ev.type == EV_ABS && !(mapping && mapping_type <= 1 && mapping_button < -4) && !(ev.code <= 1 && input[dev].lightgun) && input[dev].quirk != QUIRK_PDSP)
								{
									input_absinfo *pai = 0;
									uint8_t axis_edge = 0;
									if ((absinfo.maximum == 1 && absinfo.minimum == -1) || (absinfo.maximum == 2 && absinfo.minimum == 0))
									{
										if (ev.value == absinfo.minimum) axis_edge = 1;
										if (ev.value == absinfo.maximum) axis_edge = 2;
									}
									else
									{
										pai = &absinfo;
										int range = absinfo.maximum - absinfo.minimum + 1;
										int center = absinfo.minimum + (range / 2);
										int treshold = range / 4;

										int only_max = 1;
										for (int n = 0; n < 4; n++) if (input[dev].mmap[SYS_AXIS1_X + n] && ((input[dev].mmap[SYS_AXIS1_X + n] & 0xFFFF) == ev.code)) only_max = 0;

										if (ev.value < center - treshold && !only_max) axis_edge = 1;
										if (ev.value > center + treshold) axis_edge = 2;
									}

									uint8_t last_state = input[dev].axis_edge[ev.code & 255];
									input[dev].axis_edge[ev.code & 255] = axis_edge;

									//printf("last_state=%d, axis_edge=%d\n", last_state, axis_edge);
									if (last_state != axis_edge)
									{
										uint16_t ecode = KEY_EMU + (ev.code << 1) - 1;
										ev.type = EV_KEY;
										if (last_state)
										{
											ev.value = 0;
											ev.code = ecode + last_state;
											input_cb(&ev, pai, i);
										}

										if (axis_edge)
										{
											ev.value = 1;
											ev.code = ecode + axis_edge;
											input_cb(&ev, pai, i);
										}
									}

									// Menu button on 8BitDo Receiver in D-Input mode
									if (ev.code == 9 && input[dev].vid == 0x2dc8 && (input[dev].pid == 0x3100 || input[dev].pid == 0x3104))
									{
										ev.type = EV_KEY;
										ev.code = KEY_EMU + (ev.code << 1);
										input_cb(&ev, pai, i);
									}
								}
							}
						}
					}
					else
					{
						uint8_t data[4] = {};
						static int accx = 0, accy = 0;
						if (read(pool[i].fd, data, sizeof(data)))
						{
							int edev = i;
							int dev = i;
							if (input[i].bind >= 0) edev = input[i].bind; // mouse to event
							if (input[edev].bind >= 0) dev = input[edev].bind; // event to base device

							if (input[i].quirk == QUIRK_DS4TOUCH && input[dev].lightgun)
							{
								//disable DS4 mouse in lightgun mode
								continue;
							}

							int xval, yval, throttle = 1;
							xval = ((data[0] & 0x10) ? -256 : 0) | data[1];
							yval = ((data[0] & 0x20) ? -256 : 0) | data[2];

							if (is_menu() && !video_fb_state()) printf("%s: btn=0x%02X, dx=%d, dy=%d, scroll=%d\n", input[i].devname, data[0], xval, yval, (int8_t)data[3]);

							if (cfg.mouse_throttle) throttle = cfg.mouse_throttle;
							if (ds_mouse_emu) throttle *= 4;

							accx += xval;
							xval = accx / throttle;
							accx -= xval * throttle;

							accy -= yval;
							yval = accy / throttle;
							accy -= yval * throttle;

							mice_btn = data[0] & 7;
							if (ds_mouse_emu) mice_btn = (mice_btn & 4) | ((mice_btn & 1) << 1);

							mouse_cb(mouse_btn | mice_btn, xval, yval, (int8_t)data[3]);
						}
					}
				}
			}

			if ((pool[NUMDEV + 1].fd >= 0) && (pool[NUMDEV + 1].revents & POLLIN))
			{
				static char cmd[1024];
				int len = read(pool[NUMDEV + 1].fd, cmd, sizeof(cmd) - 1);
				if (len)
				{
					if (cmd[len - 1] == '\n') cmd[len - 1] = 0;
					cmd[len] = 0;
					printf("MiSTer_cmd: %s\n", cmd);
					if (!strncmp(cmd, "fb_cmd", 6)) video_cmd(cmd);
					else if (!strncmp(cmd, "load_core ", 10))
					{
						len = strlen(cmd);
						if (len > 4 && !strcasecmp(cmd + len - 4, ".mra")) arcade_load(cmd + 10);
						else fpga_load_rbf(cmd + 10);
					}
				}
			}

			if ((pool[NUMDEV + 2].fd >= 0) && (pool[NUMDEV + 2].revents & POLLPRI))
			{
				static char status[16];
				if (read(pool[NUMDEV + 2].fd, status, sizeof(status) - 1) && status[0] != '0') DISKLED_ON;
				lseek(pool[NUMDEV + 2].fd, 0, SEEK_SET);
			}
		}

		if (cur_leds != leds_state)
		{
			cur_leds = leds_state;
			for (int i = 0; i < NUMDEV; i++)
			{
				if (input[i].led)
				{
					ev.type = EV_LED;

					ev.code = LED_SCROLLL;
					ev.value = (cur_leds&HID_LED_SCROLL_LOCK) ? 1 : 0;
					write(pool[i].fd, &ev, sizeof(struct input_event));

					ev.code = LED_NUML;
					ev.value = (cur_leds&HID_LED_NUM_LOCK) ? 1 : 0;
					write(pool[i].fd, &ev, sizeof(struct input_event));

					ev.code = LED_CAPSL;
					ev.value = (cur_leds&HID_LED_CAPS_LOCK) ? 1 : 0;
					write(pool[i].fd, &ev, sizeof(struct input_event));
				}
			}
		}
	}

	return 0;
}

int input_poll(int getchar)
{
	static int af[NUMPLAYERS] = {};
	static uint32_t time[NUMPLAYERS] = {};
	static uint32_t joy_prev[NUMPLAYERS] = {};

	int ret = input_test(getchar);
	if (getchar) return ret;

	uinp_check_key();

	static int prev_dx = 0;
	static int prev_dy = 0;

	if (mouse_emu || ((user_io_get_kbdemu() == EMU_MOUSE) && kbd_mouse_emu))
	{
		if((prev_dx || mouse_emu_x || prev_dy || mouse_emu_y) && (!mouse_timer || CheckTimer(mouse_timer)))
		{
			mouse_timer = GetTimer(20);

			int dx = mouse_emu_x;
			int dy = mouse_emu_y;
			if (mouse_sniper ^ cfg.sniper_mode)
			{
				if (dx > 2) dx = 2;
				if (dx < -2) dx = -2;
				if (dy > 2) dy = 2;
				if (dy < -2) dy = -2;
			}

			mouse_cb(mouse_btn | mice_btn, dx, dy);
			prev_dx = mouse_emu_x;
			prev_dy = mouse_emu_y;
		}
	}

	if (!mouse_emu_x && !mouse_emu_y) mouse_timer = 0;

	if (grabbed)
	{
		for (int i = 0; i < NUMPLAYERS; i++)
		{
			if (!af_delay[i]) af_delay[i] = 50;

			if (!time[i]) time[i] = GetTimer(af_delay[i]);
			int send = 0;

			int newdir = ((joy[i] & 0xF) != (joy_prev[i] & 0xF));
			if (joy[i] != joy_prev[i])
			{
				if ((joy[i] ^ joy_prev[i]) & autofire[i])
				{
					time[i] = GetTimer(af_delay[i]);
					af[i] = 0;
				}

				send = 1;
				joy_prev[i] = joy[i];
			}

			if (CheckTimer(time[i]))
			{
				time[i] = GetTimer(af_delay[i]);
				af[i] = !af[i];
				if (joy[i] & autofire[i]) send = 1;
			}

			if (send)
			{
				user_io_digital_joystick(i, af[i] ? joy[i] & ~autofire[i] : joy[i], newdir);
			}
		}
	}

	if (!grabbed || user_io_osd_is_visible())
	{
		for (int i = 0; i < NUMPLAYERS; i++)
		{
			if(joy[i]) user_io_digital_joystick(i, 0, 1);

			joy[i] = 0;
			af[i] = 0;
			autofire[i] = 0;
		}
	}

	return 0;
}

int is_key_pressed(int key)
{
	unsigned char bits[(KEY_MAX + 7) / 8];
	for (int i = 0; i < NUMDEV; i++)
	{
		if (pool[i].fd > 0)
		{
			unsigned long evbit = 0;
			if (ioctl(pool[i].fd, EVIOCGBIT(0, sizeof(evbit)), &evbit) >= 0)
			{
				if (evbit & (1 << EV_KEY))
				{
					memset(bits, 0, sizeof(bits));
					if (ioctl(pool[i].fd, EVIOCGKEY(sizeof(bits)), &bits) >= 0)
					{
						if (bits[key / 8] & (1 << (key % 8)))
						{
							return 1;
						}
					}
				}
			}
		}
	}

	return 0;
}

void input_notify_mode()
{
	//reset mouse parameters on any mode switch
	kbd_mouse_emu = 1;
	mouse_sniper = 0;
	mouse_timer = 0;
	mouse_btn = 0;
	mouse_emu_x = 0;
	mouse_emu_y = 0;
	mouse_cb(mice_btn);
}

void input_switch(int grab)
{
	if (grab >= 0) grabbed = grab;
	//printf("input_switch(%d), grabbed = %d\n", grab, grabbed);

	for (int i = 0; i < NUMDEV; i++)
	{
		if (pool[i].fd >= 0) ioctl(pool[i].fd, EVIOCGRAB, (grabbed | user_io_osd_is_visible()) ? 1 : 0);
	}
}

int input_state()
{
	return grabbed;
}

static char ovr_buttons[1024] = {};
static char ovr_nmap[1024] = {};
static char ovr_pmap[1024] = {};

static char *get_btn(int type)
{
	int i = 2;
	while (1)
	{
		char *p = user_io_get_confstr(i);
		if (!p) break;

		if ((p[0] == 'J' && !type) || (p[0] == 'j' && ((p[1] == 'n' && type == 1) || (p[1] == 'p' && type == 2))))
		{
			p = strchr(p, ',');
			if (!p) break;

			p++;
			if (!strlen(p)) break;
			return p;
		}

		i++;
	}
	return NULL;
}

char *get_buttons(int type)
{
	if (type == 0 && ovr_buttons[0]) return ovr_buttons;
	if (type == 1 && ovr_nmap[0]) return ovr_nmap;
	if (type == 2 && ovr_pmap[0]) return ovr_pmap;

	return get_btn(type);
}

void set_ovr_buttons(char *s, int type)
{
	switch (type)
	{
	case 0:
		snprintf(ovr_buttons, sizeof(ovr_buttons), "%s", s);
		break;

	case 1:
		snprintf(ovr_nmap, sizeof(ovr_nmap), "%s", s);
		break;

	case 2:
		snprintf(ovr_pmap, sizeof(ovr_pmap), "%s", s);
		break;
	}
}

void parse_buttons()
{
	joy_bcount = 0;

	char *str = get_buttons();
	if (!str) return;

	for (int n = 0; n < 28; n++)
	{
		substrcpy(joy_bnames[n], str, n);
		if (!joy_bnames[n][0]) break;
		joy_bcount++;
	}
}
