#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <sys/sysinfo.h>
#include "input.h"
#include "user_io.h"
#include "menu.h"
#include "hardware.h"
#include "cfg.h"
#include "fpga_io.h"
#include "osd.h"
#include "errno.h"

#define NUMDEV 10

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
	NONE, //86  KEY_102ND		
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
	NONE, //104 KEY_PAGEUP		
	0x4f, //105 KEY_LEFT		
	0x4e, //106 KEY_RIGHT		
	NONE, //107 KEY_END			
	0x4d, //108 KEY_DOWN		
	NONE, //109 KEY_PAGEDOWN	
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
	0x63, //194 KEY_F24			
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
	NONE, //86  KEY_102ND		
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

/*

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

unsigned short modifier_keycode(unsigned char index)
{
// usb modifer bits:
//0     1     2    3    4     5     6    7
//LCTRL LSHIFT LALT LGUI RCTRL RSHIFT RALT RGUI

if (core_type == CORE_TYPE_MIST)
{
static const unsigned short atari_modifier[] = { 0x1d, 0x2a, 0x38, MISS, 0x1d, 0x36, 0x38, MISS };
return atari_modifier[index];
}
}
*/


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

uint32_t get_atari_code(uint16_t key)
{
	if (key > 255) return NONE;
	return 0; // ev2atari[key];
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

typedef struct
{
	uint16_t vid, pid;
	char     led;
	char     last_x, last_y; // last x & y axis bitmasks for each alternative directional control scheme (DPAD, Analog pad etc.)

	char     has_map;
	uint32_t map[32];

	char     has_mmap;
	uint32_t mmap[32];
	uint16_t jkmap[1024];

	char     has_kbdmap;
	uint8_t  kbdmap[256];

	int      accx, accy;
}  devInput;

static devInput input[NUMDEV] = {};
static int first_joystick = -1;

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

	mwd = inotify_add_watch(mfd, "/dev/input",
		IN_MODIFY | IN_CREATE | IN_DELETE);

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

	if (test_bit(EV_LED, evtype_b))
	{
		printf("has LEDs.\n");
		return 1;
	}

	return 0;
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

static int mapping = 0;
static int mapping_button;
static int mapping_dev;
static int mapping_type;
static int mapping_count;
static uint8_t mapping_key;

void start_map_setting(int cnt)
{
	mapping_button = 0;
	mapping = 1;
	mapping_dev = -1;
	mapping_type = (cnt<0) ? 3 : cnt ? 1 : 2;
	mapping_count = cnt;
}

int get_map_button()
{
	return (mapping_type == 2) ? mapping_key : mapping_button;
}

int get_map_type()
{
	return mapping_type;
}

static char *get_map_name(int dev, int def)
{
	static char name[128];
	if (def || is_menu_core()) sprintf(name, "input_%04x_%04x.map", input[dev].vid, input[dev].pid);
	else sprintf(name, "%s_input_%04x_%04x.map", user_io_get_core_name_ex(), input[dev].vid, input[dev].pid);
	return name;
}

static char *get_kbdmap_name(int dev)
{
	static char name[128];
	sprintf(name, "kbd_%04x_%04x.map", input[dev].vid, input[dev].pid);
	return name;
}

void finish_map_setting(int dismiss)
{
	mapping = 0;
	if (mapping_dev<0) return;

	if (mapping_type == 2)
	{
		if (dismiss) input[mapping_dev].has_kbdmap = 0;
		else FileSaveConfig(get_kbdmap_name(mapping_dev), &input[mapping_dev].kbdmap, sizeof(input[mapping_dev].kbdmap));
	}
	else if (mapping_type == 3)
	{
		if (dismiss) memset(input[mapping_dev].jkmap, 0, sizeof(input[mapping_dev].jkmap));
	}
	else
	{
		if (dismiss) input[mapping_dev].has_map = 0;
		else FileSaveConfig(get_map_name(mapping_dev, 0), &input[mapping_dev].map, sizeof(input[mapping_dev].map));
		if (is_menu_core()) input[mapping_dev].has_mmap = 0;
	}
}

uint16_t get_map_vid()
{
	return (mapping_dev >= 0) ? input[mapping_dev].vid : 0;
}

uint16_t get_map_pid()
{
	return (mapping_dev >= 0) ? input[mapping_dev].pid : 0;
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

#define KEY_EMU_LEFT  (KEY_MAX+1)
#define KEY_EMU_RIGHT (KEY_MAX+2)
#define KEY_EMU_UP    (KEY_MAX+3)
#define KEY_EMU_DOWN  (KEY_MAX+4)

#define KEY_EMU_LT    (KEY_EMU_LEFT+16)
#define KEY_EMU_RT    (KEY_EMU_LEFT+17)
#define KEY_EMU_PS    (KEY_EMU_LEFT+18)


static void input_cb(struct input_event *ev, struct input_absinfo *absinfo, int dev);

static int kbd_toggle = 0;
static uint16_t joy[2] = { 0 }, joy_prev[2] = { 0 };
static uint16_t autofire[2] = { 0 };
static int af_delay[2] = { 50, 50 };

static unsigned char mouse_btn = 0;
static int mouse_emu = 0;
static int kbd_mouse_emu = 0;
static int mouse_sniper = 0;
static int mouse_emu_x = 0;
static int mouse_emu_y = 0;

static uint32_t mouse_timer = 0;

static void joy_digital(int num, uint16_t mask, char press, int bnum)
{
	static char str[128];

	if (num < 2)
	{
		if (!user_io_osd_is_visible() && ((bnum == 17) || (bnum == 16)) && joy[num])
		{
			uint16_t amask = joy[num] & 0xFFF0;
			if(press)
			{
				if (amask)
				{
					if (autofire[num] & amask) autofire[num] &= ~amask;
					else autofire[num] |= amask;

					if (hasAPI1_5())
					{
						if(autofire[num] & amask) sprintf(str, "Auto fire: %d ms", af_delay[num] * 2);
						else sprintf(str, "Auto fire: OFF");
						Info(str);
					}
					else InfoMessage((autofire[num] & amask) ? "\n\n          Auto fire\n             ON" :
						                "\n\n          Auto fire\n             OFF");
				}
				else
				{
					if (joy[num] & 9)
					{
						 af_delay[num] += 25 << ((joy[num] & 1) ? 1 : 0);
						 if (af_delay[num] > 500) af_delay[num] = 500;
					}
					else
					{
						af_delay[num] -= 25 << ((joy[num] & 2) ? 1 : 0);
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
				}
			}
			return;
		}

		if (bnum == 16)
		{
			if(press) kbd_toggle = !kbd_toggle;
			return;
		}

		if (!user_io_osd_is_visible() && (bnum == 17) && (mouse_emu & 1))
		{
			if (press)
			{
				mouse_sniper = 0;
				mouse_timer = 0;
				mouse_btn = 0;
				mouse_emu_x = 0;
				mouse_emu_y = 0;
				user_io_mouse(0, 0, 0);

				mouse_emu ^= 2;
				if (hasAPI1_5()) Info((mouse_emu & 2) ? "Mouse mode ON" : "Mouse mode OFF");
				else InfoMessage((mouse_emu & 2) ? "\n\n       Mouse mode lock\n             ON" :
					"\n\n       Mouse mode lock\n             OFF");
			}
			return;
		}

		if (user_io_osd_is_visible() || (bnum == 17))
		{
			memset(joy, 0, sizeof(joy));
			struct input_event ev;
			ev.type = EV_KEY;
			ev.value = press;
			switch (mask)
			{
			case JOY_RIGHT:
				ev.code = KEY_RIGHT;
				break;

			case JOY_LEFT:
				ev.code = KEY_LEFT;
				break;

			case JOY_UP:
				ev.code = KEY_UP;
				break;

			case JOY_DOWN:
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
				ev.code = (bnum == 17) ? KEY_MENU : 0;
			}

			input_cb(&ev, 0, 0);
		}
		else
		{
			if (press) joy[num] |= mask;
			else joy[num] &= ~mask;
			//user_io_digital_joystick(num, joy[num]);
		}
	}
}

static void joy_analog(int num, int axis, int offset)
{
	static int pos[2][2] = { 0 };

	if (num < 2)
	{
		pos[num][axis] = offset;
		user_io_analog_joystick(num, (char)(pos[num][0]), (char)(pos[num][1]));
	}
}

static void input_cb(struct input_event *ev, struct input_absinfo *absinfo, int dev)
{
	static int key_mapped = 0;

	int map_skip = (ev->type == EV_KEY && ev->code == 57 && mapping_dev >= 0 && mapping_type==1);
	int cancel   = (ev->type == EV_KEY && ev->code == 1);
	int enter    = (ev->type == EV_KEY && ev->code == 28);

	//mouse
	switch (ev->type)
	{
	case EV_KEY:
		if (ev->code >= 272 && ev->code <= 279)
		{
			if (ev->value <= 1)
			{
				unsigned char mask = 1 << (ev->code - 272);
				mouse_btn = (ev->value) ? mouse_btn | mask : mouse_btn & ~mask;
				user_io_mouse(mouse_btn, 0, 0);
			}
			return;
		}
		break;

	case EV_REL:
		{
			int msval;
			if (!cfg.mouse_throttle) cfg.mouse_throttle = 1;

			switch (ev->code)
			{
			case 0:
				input[dev].accx += ev->value;
				msval = input[dev].accx / cfg.mouse_throttle;
				input[dev].accx -= msval * cfg.mouse_throttle;

				//printf("Mouse PosX: %d\n", msval);
				user_io_mouse(mouse_btn, msval, 0);
				return;
			case 1:
				input[dev].accy += ev->value;
				msval = input[dev].accy / cfg.mouse_throttle;
				input[dev].accy -= msval * cfg.mouse_throttle;

				//printf("Mouse PosY: %d\n", msval);
				user_io_mouse(mouse_btn, 0, msval);
				return;
			}
		}
		break;
	}

	if (!input[dev].has_map)
	{
		if (!FileLoadConfig(get_map_name(dev, 0), &input[dev].map, sizeof(input[dev].map)))
		{
			if (is_menu_core() || !FileLoadConfig(get_map_name(dev, 1), &input[dev].map, sizeof(input[dev].map)))
			{
				memset(input[dev].map, 0, sizeof(input[dev].map));
			}

			//remove alt controls from default map
			input[dev].map[8]  = 0;
			input[dev].map[9]  = 0;
			input[dev].map[10] = 0;
			input[dev].map[11] = 0;
			input[dev].map[12] = 0;
			input[dev].map[13] = 0;
			input[dev].map[14] = 0;
			input[dev].map[15] = 0;
		}
		input[dev].has_map = 1;
	}

	if (!input[dev].has_mmap)
	{
		if (!FileLoadConfig(get_map_name(dev, 1), &input[dev].mmap, sizeof(input[dev].mmap)))
		{
			memset(input[dev].mmap, 0, sizeof(input[dev].mmap));
		}
		input[dev].has_mmap = 1;
	}

	//mapping
	if (mapping && (mapping_dev >=0 || ev->value) && !cancel && !enter)
	{
		if (ev->type == EV_KEY)
		{
			if (mapping_type == 2)
			{
				if (ev->value == 1 && ev->code < 256)
				{
					if(mapping_dev < 0)
					{
						mapping_dev = dev;
						mapping_key = 0;
					}

					if (!mapping_key) mapping_key = ev->code;
					else
					{
						input[mapping_dev].kbdmap[mapping_key] = ev->code;
						mapping_key = 0;
					}
				}
				return;
			}
			else if (mapping_type == 3)
			{
				if (ev->value == 1)
				{
					if (!mapping_button)
					{
						if (mapping_dev < 0) mapping_dev = dev;
						if (mapping_dev == dev && ev->code < 1024) mapping_button = ev->code;
					}
					else if (mapping_dev >= 0 && (ev->code<256 || mapping_dev == dev))
					{
						// Technically it's hard to map the key to button as keyboards
						// are all the same while joysticks are personalized and numbered.
						input[mapping_dev].jkmap[mapping_button] = ev->code;
						mapping_button = 0;
					}
				}
			}
			else if ((mapping_dev < 0) || ((ev->code >= 256) ? mapping_type : !mapping_type))
			{
				if (mapping_dev < 0)
				{
					mapping_dev = dev;
					mapping_type = (ev->code >= 256) ? 1 : 0;
				}

				if (mapping_dev == dev && mapping_button < mapping_count)
				{
					if (ev->value == 1)
					{
						if (!mapping_button) memset(input[dev].map, 0, sizeof(input[dev].map));

						int found = 0;
						for (int i = (is_menu_core() && mapping_button >= 8) ? 8 : 0; i < mapping_button; i++) if (input[dev].map[i] == ev->code) found = 1;

						if (!found)
						{
							input[dev].map[(mapping_button == (mapping_count - 1) && is_menu_core()) ? 16 + mapping_type : mapping_button] = ev->code;
							key_mapped = 1;
						}
					}
					else if(ev->value == 0)
					{
						if (key_mapped) mapping_button++;
						key_mapped = 0;
					}
				}

				return;
			}
		}

		if (mapping_type <= 1 && map_skip && mapping_button < mapping_count)
		{
			if (ev->value == 1) mapping_button++;
			return;
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
				if (first_joystick < 0) first_joystick = dev;

				if (user_io_osd_is_visible())
				{
					if (ev->value <= 1)
					{
						for (int i = 0; i <= 11; i++)
						{
							if (ev->code == input[dev].mmap[i])
							{
								int n = i;
								if (n >= 8) n -= 8;
								joy_digital(0, 1 << n, ev->value, n);
								return;
							}
						}

						if (ev->code == input[dev].mmap[17])
						{
							joy_digital(0, 0, ev->value, 17);
							return;
						}
					}
				}
				else
				{
					if (mouse_emu)
					{
						for (int i = 8; i <= 14; i++)
						{
							if (ev->code == input[dev].mmap[i])
							{
								switch (i)
								{
								case 8:
									mouse_emu_x = ev->value ? 10 : 0;
									break;
								case 9:
									mouse_emu_x = ev->value ? -10 : 0;
									break;
								case 10:
									mouse_emu_y = ev->value ? 10 : 0;
									break;
								case 11:
									mouse_emu_y = ev->value ? -10 : 0;
									break;

								default:
									mouse_btn = ev->value ? mouse_btn | 1 << (i - 12) : mouse_btn & ~(1 << (i - 12));
									user_io_mouse(mouse_btn, 0, 0);
									break;
								}
								return;
							}
						}

						for (int i = 0; i <= 15; i++)
						{
							if (ev->code == input[dev].map[i])
							{
								if (ev->value <= 1) joy_digital((first_joystick == dev) ? 0 : 1, 1 << i, ev->value, i);
								return;
							}
						}
					}
					else
					{
						for (int i = 0; i <= 15; i++)
						{
							if (ev->code == input[dev].map[i])
							{
								if (ev->value <= 1) joy_digital((first_joystick == dev) ? 0 : 1, 1 << i, ev->value, i);
								return;
							}
						}

						for (int i = 8; i <= 11; i++)
						{
							if (ev->code == input[dev].mmap[i])
							{
								if (ev->value <= 1) joy_digital((first_joystick == dev) ? 0 : 1, 1 << (i - 8), ev->value, i - 8);
								return;
							}
						}
					}

					if (ev->code == input[dev].mmap[17])
					{
						if (ev->value <= 1) joy_digital((first_joystick == dev) ? 0 : 1, 0, ev->value, 17);
						return;
					}

					if (ev->code == input[dev].mmap[15] && (ev->value <= 1) && ((!(mouse_emu & 1)) ^ (!ev->value)))
					{
						mouse_emu = ev->value ? mouse_emu | 1 : mouse_emu & ~1;
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
							user_io_mouse(0, 0, 0);
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

				if(!user_io_osd_is_visible() && ((user_io_get_kbdemu() == EMU_JOY0) || (user_io_get_kbdemu() == EMU_JOY1)))
				{
					if (!kbd_toggle)
					{
						for (int i = 0; i <= 15; i++)
						{
							if (ev->code == input[dev].map[i])
							{
								if (ev->value <= 1) joy_digital((user_io_get_kbdemu() == EMU_JOY0) ? 0 : 1, 1 << i, ev->value, i);
								return;
							}
						}
					}

					if (ev->code == input[dev].mmap[16])
					{
						if (ev->value <= 1) joy_digital((user_io_get_kbdemu() == EMU_JOY0) ? 0 : 1, 0, ev->value, 16);
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
						for (int i = 8; i <= 14; i++)
						{
							if (ev->code == input[dev].mmap[i])
							{
								switch (i)
								{
								case 8:
									mouse_emu_x = ev->value ?  10 : 0;
									break;
								case 9:
									mouse_emu_x = ev->value ? -10 : 0;
									break;
								case 10:
									mouse_emu_y = ev->value ?  10 : 0;
									break;
								case 11:
									mouse_emu_y = ev->value ? -10 : 0;
									break;

								default:
									mouse_btn = ev->value ? mouse_btn | 1 << (i - 12) : mouse_btn & ~(1 << (i - 12));
									user_io_mouse(mouse_btn, 0, 0);
									break;
								}
								return;
							}
						}

						if (ev->code == input[dev].mmap[15])
						{
							if (ev->value <= 1) mouse_sniper = ev->value;
							return;
						}
					}

					if (ev->code == input[dev].map[16])
					{
						if (ev->value == 1)
						{
							kbd_mouse_emu = !kbd_mouse_emu;
							printf("kbd_mouse_emu = %d\n", kbd_mouse_emu);

							mouse_timer = 0;
							mouse_btn = 0;
							mouse_emu_x = 0;
							mouse_emu_y = 0;
							user_io_mouse(0, 0, 0);
						}
						return;
					}
				}

				user_io_kbd(code, ev->value);
				return;
			}
			break;

		//analog joystick
		case EV_ABS:
			if (!user_io_osd_is_visible())
			{
				if (ev->code <= 1)
				{
					//convert to 0..255 range
					int value = ((ev->value - absinfo->minimum) * 256) / (absinfo->maximum - absinfo->minimum + 1);
					//printf("ABS: axis %d = %d -> %d\n", ev->code, ev->value, value);

					if (mouse_emu)
					{
						if (ev->code == 0) // x
						{
							mouse_emu_x = 0;
							if (value < 127 || value>129) mouse_emu_x = value - 128;
							mouse_emu_x /= 12;
							return;
						}

						if (ev->code == 1) // y
						{
							mouse_emu_y = 0;
							if (value < 127 || value>129) mouse_emu_y = value - 128;
							mouse_emu_y /= 12;
							return;
						}
					}
					else
					{
						// skip if first joystick is not defined.
						if (first_joystick < 0) break;

						// TODO:
						// 1) add analog axis mapping
						// 2) enable invertion

						if (ev->code == 0) // x
						{
							int offset = 0;
							if (value < 127 || value>129) offset = value - 128;
							//printf("analog_x = %d\n", offset);
							joy_analog((first_joystick == dev) ? 0 : 1, 0, offset);
							return;
						}

						if (ev->code == 1) // y
						{
							int offset = 0;
							if (value < 127 || value>129) offset = value - 128;
							//printf("analog_y = %d\n", offset);
							joy_analog((first_joystick == dev) ? 0 : 1, 1, offset);
							return;
						}
					}
				}
			}
			break;
		}
	}
}

static uint16_t read_hex(char *filename)
{
	FILE *in;
	unsigned int value;

	in = fopen(filename, "rb");
	if (!in) return 0;

	if (fscanf(in, "%x", &value) == 1)
	{
		fclose(in);
		return (uint16_t)value;
	}
	fclose(in);
	return 0;
}

static void getVidPid(int num, uint16_t* vid, uint16_t* pid)
{
	char name[256];
	sprintf(name, "/sys/class/input/event%d/device/id/vendor", num);
	*vid = read_hex(name);
	sprintf(name, "/sys/class/input/event%d/device/id/product", num);
	*pid = read_hex(name);
}

static struct pollfd pool[NUMDEV + 1];

// Translate axis values to key events of specific codes.
// Only binary key press /release logic is supported, any value not 0 is considered
// a key press, value 0 is key release.
static void send_axis_key_event(int dev, int code1, int code2, int value1, int value2, int last1, int last2) {
	struct input_event ev;
	//note: key events do not require input_absinfo values

	ev.type = EV_KEY;
	//send key releases first
	// value1 unset but previously set
	if (!value1 && last1) {
		ev.code = code1;
		ev.value = 0;
		input_cb(&ev, 0, dev);
	}
	// value2 unset but previously set
	if (!value2 && last2) {
		ev.code = code2;
		ev.value = 0;
		input_cb(&ev, 0, dev);
	}
	//send key presses afterwards, but only if previously not set
	if (value1 && !last1)
	{
		ev.code = code1;
		ev.value = 1;
		input_cb(&ev, 0, dev);
	} else //either one or another, not both directions at the same time
	if (value2 && !last2)
	{
		ev.code = code2;
		ev.value = 1;
		input_cb(&ev, 0, dev);
	}
}

int input_test(int getchar)
{
	static char   cur_leds = 0;
	static int    state = 0;
	struct input_absinfo absinfo;

	char devname[20];
	struct input_event ev;

	if (state == 0)
	{
		signal(SIGINT, INThandler);
		pool[NUMDEV].fd = set_watch();
		pool[NUMDEV].events = POLLIN;
		state++;
	}

	if (state == 1)
	{
		printf("Open up to %d input devices.\n", NUMDEV);
		for (int i = 0; i<NUMDEV; i++)
		{
			sprintf(devname, "/dev/input/event%d", i);
			pool[i].fd = open(devname, O_RDWR);
			pool[i].events = POLLIN;
			memset(&input[i], 0, sizeof(input[i]));
			input[i].led = has_led(pool[i].fd);
			if (pool[i].fd > 0) getVidPid(i, &input[i].vid, &input[i].pid);
			if (pool[i].fd > 0) printf("opened %s (%04x:%04x)\n", devname, input[i].vid, input[i].pid);
		}

		cur_leds |= 0x80;
		state++;
	}

	if (state == 2)
	{
		int return_value = poll(pool, NUMDEV + 1, 0);
		if (return_value < 0)
		{
			printf("ERR: poll\n");
		}
		else if (return_value > 0)
		{
			if ((pool[NUMDEV].revents & POLLIN) && check_devs())
			{
				printf("Close all devices.\n");
				for (int i = 0; i<NUMDEV; i++)
				{
					if (pool[i].fd >= 0) close(pool[i].fd);
				}
				state = 1;
				return 0;
			}

			for (int i = 0; i<NUMDEV; i++)
			{
				if ((pool[i].fd >= 0) && (pool[i].revents & POLLIN))
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
						else
						{
							if (ev.type == EV_ABS)
							{
								int len = ioctl(pool[i].fd, EVIOCGABS(ev.code), &absinfo);
								if (len < 0)
								{
									memset(&absinfo, 0, sizeof(absinfo));
								}
							}

							if (is_menu_core())
							{
								switch (ev.type)
								{
									//keyboard, buttons
								case EV_KEY:
									printf("Input event: type=EV_KEY, code=%d(0x%x), value=%d\n", ev.code, ev.code, ev.value);
									break;

									//mouse
								case EV_REL:
									printf("Input event: type=EV_REL, Axis=%d, Offset:=%d\n", ev.code, ev.value);
									break;

								case EV_SYN:
								case EV_MSC:
									break;

									//analog joystick
								case EV_ABS:
									if (ev.code == 62) break;
									if (ev.code == 61) break; //ps3 accel axis
									if (ev.code == 60) break; //ps3 accel axis
									if (ev.code == 59) break; //ps3 accel axis

									//reduce flood from PS3 gamepad
									if (input[i].vid == 0x054c && input[i].pid == 0x0268)
									{ if (ev.code <= 5 && ev.value > 118 && ev.value < 138) break; }

									//aliexpress USB encoder floods messages
									if (input[i].vid == 0x0079 && input[i].pid == 0x0006)
									{ if (ev.code == 2) break; }

									printf("Input event: type=EV_ABS, Axis=%d, Offset:=%d.", ev.code, ev.value);
									printf(" ABS_INFO: min = %d max = %d", absinfo.minimum, absinfo.maximum);
									if (absinfo.fuzz) printf(" fuzz = %d", absinfo.fuzz);
									if (absinfo.resolution) printf(" res = %d", absinfo.resolution);
									printf("\n");
									break;

								default:
									printf("Input event: type=%d, code=%d(0x%x), value=%d(0x%x)\n", ev.type, ev.code, ev.code, ev.value, ev.value);
								}
							}

							input_cb(&ev, &absinfo, i);

							//sumulate digital directions from analog
							if (ev.type == EV_ABS && !(ev.code<=1 && mouse_emu && !user_io_osd_is_visible()))
							{
								// some pads use axis 16 for L/R PAD, axis 17 for U/D PAD
								// emulate PAD on axis 0/1

								// axis ranges vary per USB controller: some have 0-255, others -32768..+32767 etc.
								int range = absinfo.maximum - absinfo.minimum + 1;
								int center = absinfo.minimum + (range/2);
								int treshold = range/4;

								char l, r, u, d;
								l = r = u = d = 0;
								uint16_t offset = 0;
								char offset_mask1, offset_mask2;

								if (ev.code < 16) offset += 4;
								if (ev.code < 2)  offset += 4;
								offset_mask1 = (1 << (offset >> 1)); // 01XX
								offset_mask2 = (2 << (offset >> 1)); // 10XX

								uint16_t base_axis = !user_io_get_joy_transl() ? 0 : ~ev.code;
								uint16_t extra_axis = 2;
								if (input[i].vid == 0x0079 && input[i].pid == 0x0006) extra_axis = 0;  // AliExpress USB encoder PCB floods axis 2
								if (input[i].vid == 0x045e && input[i].pid == 0x028e) extra_axis = 3;  // 8BitDo Retro Receiver

								if(ev.code == base_axis || ev.code == extra_axis || ev.code == 16) // x
								{
									if ((ev.code < 16) ? ev.value < center - treshold : ev.value == -1) l = 1;
									if ((ev.code < 16) ? ev.value > center + treshold : ev.value ==  1) r = 1;

									// left or right pressed or now released but previously pressed (for specific controller offset)
									if (l || r || (input[i].last_x & (offset_mask1 | offset_mask2))) {
										send_axis_key_event(i, KEY_EMU_LEFT + offset, KEY_EMU_RIGHT + offset, l, r, input[i].last_x & offset_mask1, input[i].last_x & offset_mask2);
										if (l) input[i].last_x |= offset_mask1;
										else input[i].last_x &= ~offset_mask1;
										if (r) input[i].last_x |= offset_mask2;
										else input[i].last_x &= ~offset_mask2;
									}
								}

								base_axis = !user_io_get_joy_transl() ? 1 : ~ev.code;
								if (input[i].vid == 0x0079 && input[i].pid == 0x0006) base_axis = 3; // AliExpress USB encoder PCB

								extra_axis = 5;
								if (input[i].vid == 0x045e && input[i].pid == 0x028e) extra_axis = 4;  // 8BitDo Retro Receiver

								if (ev.code == base_axis || ev.code == extra_axis || ev.code == 17) // y
								{
                                    // override specific to x axis. Negative value as a center??
                                    if (input[i].vid == 0x046d && input[i].pid == 0xc21f) center = -129; // Logitech F710
                                    
									if ((ev.code < 16) ? ev.value < center - treshold : ev.value == -1) u = 1;
									if ((ev.code < 16) ? ev.value > center + treshold : ev.value ==  1) d = 1;

									// up or down pressed or now released but previously pressed (for specific controller offset)
									if (u || d || (input[i].last_y & (offset_mask1 | offset_mask2))) {
										send_axis_key_event(i, KEY_EMU_UP + offset, KEY_EMU_DOWN + offset, u, d,  input[i].last_y & offset_mask1, input[i].last_y & offset_mask2);
										if (u) input[i].last_y |= offset_mask1;
										else input[i].last_y &= ~offset_mask1;
										if (d) input[i].last_y |= offset_mask2;
										else input[i].last_y &= ~offset_mask2;
									}
								}

								if (input[i].vid == 0x045e && input[i].pid == 0x028e)  // 8BitDo Retro Receiver
								{
									ev.type = EV_KEY;
									if (ev.code == 2)
									{
										ev.code = KEY_EMU_LT;
										ev.value = (ev.value == 255) ? 1 : 0;
										input_cb(&ev, &absinfo, i);
									}

									if (ev.code == 5)
									{
										ev.code = KEY_EMU_RT;
										ev.value = (ev.value == 255) ? 1 : 0;
										input_cb(&ev, &absinfo, i);
									}
								}

								if (input[i].vid == 0x2dc8 && input[i].pid == 0x3100)  // 8BitDo Retro Receiver (Select+Left)
								{
									ev.type = EV_KEY;
									if (ev.code == 9)
									{
										ev.code = KEY_EMU_PS;
										input_cb(&ev, &absinfo, i);
									}
								}
							}
						}
					}
				}
			}
		}

		if (cur_leds != leds_state)
		{
			cur_leds = leds_state;
			for (int i = 0; i<NUMDEV; i++)
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
	static int af[2] = { 0 };
	static uint32_t time[2] = { 0 };

	int ret = input_test(getchar);
	if (getchar) return ret;

	static int prev_dx = 0;
	static int prev_dy = 0;

	if (mouse_emu || ((user_io_get_kbdemu() == EMU_MOUSE) && kbd_mouse_emu))
	{
		if((prev_dx || mouse_emu_x || prev_dy || mouse_emu_y) && (!mouse_timer || CheckTimer(mouse_timer)))
		{
			mouse_timer = GetTimer(20);

			int dx = mouse_emu_x;
			int dy = mouse_emu_y;
			if (mouse_sniper)
			{
				if (dx > 2) dx = 2;
				if (dx < -2) dx = -2;
				if (dy > 2) dy = 2;
				if (dy < -2) dy = -2;
			}

			user_io_mouse(mouse_btn, dx, dy);
			prev_dx = mouse_emu_x;
			prev_dy = mouse_emu_y;
		}
	}

	if (!mouse_emu_x && !mouse_emu_y) mouse_timer = 0;

	for (int i = 0; i < 2; i++)
	{
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
	user_io_mouse(0, 0, 0);
}
