/*

http://removers.free.fr/wikipendium/wakka.php?wiki=IntelligentKeyboardBible
https://www.kernel.org/doc/Documentation/input/atarikbd.txt

ikbd ToDo:

Feature                      Example using/needing it    impl. tested
---------------------------------------------------------------------
mouse y at bottom            Bolo                         X     X
mouse button key events      Goldrunner/A_008             X     X
joystick interrogation mode  Xevious/A_004                X     X
Absolute mouse mode          Addicataball/A_050           X     X
disable mouse                ?                            X
disable joystick             ?                            X
Joysticks also generate      Goldrunner                   X    -X
mouse button events!
Pause/Resume                 PACMANIA_STE/Gembench        X
mouse keycode mode           Goldrunner                   X     X

Games that have ikbd problems:
PowerMonger/PP_106           fixed
Stardust                     fixed
M1 tank platoon/A_385        fixed
*/

#include <stdio.h>
#include <string.h>

#include "../../user_io.h"
#include "../../spi.h"
#include "st_ikbd.h"
#include "../../debug.h"

#define IKBD_AUTO_MS   20

// atari ikbd stuff
#define IKBD_STATE_JOYSTICK_EVENT_REPORTING    0x01
#define IKBD_STATE_MOUSE_Y_BOTTOM              0x02
#define IKBD_STATE_MOUSE_BUTTON_AS_KEY         0x04   // mouse buttons act like keys
#define IKBD_STATE_MOUSE_DISABLED              0x08
#define IKBD_STATE_MOUSE_ABSOLUTE              0x10
#define IKBD_STATE_MOUSE_ABSOLUTE_IN_PROGRESS  0x20
#define IKBD_STATE_WAIT4RESET                  0x40
#define IKBD_STATE_PAUSED                      0x80

#define IKBD_DEFAULT IKBD_STATE_JOYSTICK_EVENT_REPORTING

/* ------------------- transmit queue ------------------- */
#define QUEUE_LEN 16    // power of 2!
static unsigned short tx_queue[QUEUE_LEN];
static unsigned char wptr = 0, rptr = 0;
static unsigned long ikbd_timer = 0;

/* -------- main structure to keep track of ikbd state -------- */
static struct
{
	unsigned char state;
	unsigned long auto_timer;  // auto report timer (50hz/20ms)
	unsigned long rtc_timer;
	// ----- joystick state -------
	struct
	{
		unsigned char state;    // current state
		unsigned char prev;     // last reported state
	} joy[2];

	// ----- mouse state -------
	struct
	{
		// current state
		unsigned char but, but_prev;
		short x, y;

		struct
		{
			// absolute mouse state
			unsigned char buttons;
			struct
			{
				unsigned short x, y;
			} max;
			struct
			{
				unsigned char x, y;
			} scale;
			struct
			{
				unsigned short x, y;
			} pos;
		} abs;
	} mouse;

	// ----- clock state ------
	unsigned char date[6];

	unsigned int tx_cnt;   // tx byte counter for debugging

	// ----- buffer tp hold incoming commands ------
	struct
	{
		char size;

		union
		{
			struct
			{
				unsigned char code;    // cmd code

				// command specific structures
				union
				{
					unsigned char mouse_button_action;
					unsigned char reset;
					struct
					{
						unsigned short max_x, max_y;
					} __attribute__((packed)) abs_mouse_pos;
					struct
					{
						unsigned char dist_x, dist_y;
					} __attribute__((packed)) mouse_keycode;
					struct
					{
						unsigned char x, y;
					} __attribute__((packed)) mouse_threshold;
					struct
					{
						unsigned char x, y;
					} __attribute__((packed)) mouse_scale;
					struct
					{
						unsigned char f;
						unsigned short x, y;
					} __attribute__((packed)) load_mouse_pos;
					unsigned char date[6];
				};
			} __attribute__((packed)) command;

			unsigned char byte[0];
		};
	} buffer;

} ikbd;

// read a 16 bit word in big endian
unsigned short be16(unsigned short in)
{
	return ((in & 0xff) << 8) + ((in & 0xff00) >> 8);
}

static void enqueue(unsigned short b)
{
	if (((wptr + 1) & (QUEUE_LEN - 1)) == rptr) {
		return;
	}

	tx_queue[wptr] = b;
	wptr = (wptr + 1) & (QUEUE_LEN - 1);
}

unsigned char bcd2bin(unsigned char in)
{
	return 10 * (in >> 4) + (in & 0x0f);
}

unsigned char bin2bcd(unsigned char in)
{
	return 16 * (in / 10) + (in % 10);
}

// convert internal joystick format into atari ikbd format
static unsigned char joystick_map2ikbd(unsigned char in)
{
	unsigned char out = 0;

	if (in & JOY_UP) { out |= 0x01; }
	if (in & JOY_DOWN) { out |= 0x02; }
	if (in & JOY_LEFT) { out |= 0x04; }
	if (in & JOY_RIGHT) { out |= 0x08; }
	if (in & JOY_BTN1) { out |= 0x80; }

	return out;
}

void ikbd_handler_mouse_button_action(void)
{
	unsigned char action = ikbd.buffer.command.mouse_button_action;
	ikbd_debugf("mouse button action = %d", action);

	// bit 2: Mouse buttons act like keys (LEFT=0x74 & RIGHT=0x75)
	if (action & 0x04) { ikbd.state |= IKBD_STATE_MOUSE_BUTTON_AS_KEY; }
	else { ikbd.state &= ~IKBD_STATE_MOUSE_BUTTON_AS_KEY; }
}

void ikbd_handler_set_relative_mouse_pos(void)
{
	ikbd_debugf("Set relative mouse positioning");
	ikbd.state &= ~IKBD_STATE_MOUSE_DISABLED;
	ikbd.state &= ~IKBD_STATE_MOUSE_ABSOLUTE;
}

void ikbd_handler_set_abs_mouse_pos(void)
{
	ikbd.mouse.abs.max.x = be16(ikbd.buffer.command.abs_mouse_pos.max_x);
	ikbd.mouse.abs.max.y = be16(ikbd.buffer.command.abs_mouse_pos.max_y);

	ikbd_debugf("Set absolute mouse positioning, max = %u/%u", ikbd.mouse.abs.max.x, ikbd.mouse.abs.max.y);

	ikbd.state &= ~IKBD_STATE_MOUSE_DISABLED;
	ikbd.state |= IKBD_STATE_MOUSE_ABSOLUTE;
	ikbd.mouse.abs.buttons = 2 | 8;
}

void ikbd_handler_set_mouse_keycode_mode(void)
{
	ikbd_debugf("Set mouse keycode mode dist %u/%u", ikbd.buffer.command.mouse_keycode.dist_x, ikbd.buffer.command.mouse_keycode.dist_y);
}

void ikbd_handler_set_mouse_threshold(void)
{
	ikbd_debugf("Set mouse threshold %u/%u", ikbd.buffer.command.mouse_threshold.x, ikbd.buffer.command.mouse_threshold.y);
}

void ikbd_handler_set_mouse_scale(void)
{
	ikbd_debugf("Set mouse scale %u/%u", ikbd.buffer.command.mouse_scale.x, ikbd.buffer.command.mouse_scale.y);

	ikbd.mouse.abs.scale.x = ikbd.buffer.command.mouse_scale.x;
	ikbd.mouse.abs.scale.y = ikbd.buffer.command.mouse_scale.y;
}

void ikbd_handler_interrogate_mouse_pos(void)
{
	//    ikbd_debugf("Interrogate Mouse Position");
	if (ikbd.state & IKBD_STATE_MOUSE_ABSOLUTE) {

		enqueue(0x8000 + 3);   // 3ms delay, hatari uses 18000 cycles (~2.25ms)
		enqueue(0xf7);
		enqueue(ikbd.mouse.abs.buttons);
		enqueue(ikbd.mouse.abs.pos.x >> 8);
		enqueue(ikbd.mouse.abs.pos.x & 0xff);
		enqueue(ikbd.mouse.abs.pos.y >> 8);
		enqueue(ikbd.mouse.abs.pos.y & 0xff);

		ikbd.mouse.abs.buttons = 0;
	}
}

void ikbd_handler_load_mouse_pos(void)
{
	ikbd.mouse.abs.pos.x = be16(ikbd.buffer.command.load_mouse_pos.x);
	ikbd.mouse.abs.pos.y = be16(ikbd.buffer.command.load_mouse_pos.y);

	ikbd_debugf("Load mouse position %u/%u", ikbd.mouse.abs.pos.x, ikbd.mouse.abs.pos.y);
}

void ikbd_handler_set_y_bottom(void)
{
	ikbd_debugf("Set Y at bottom");
	ikbd.state |= IKBD_STATE_MOUSE_Y_BOTTOM;
}

void ikbd_handler_set_y_top(void)
{
	ikbd_debugf("Set Y at top");
	ikbd.state &= ~IKBD_STATE_MOUSE_Y_BOTTOM;
}

void ikbd_handler_resume(void)
{
	ikbd.state &= ~IKBD_STATE_PAUSED;
}

void ikbd_handler_disable_mouse(void)
{
	ikbd_debugf("Disable mouse");
	ikbd.state |= IKBD_STATE_MOUSE_DISABLED;
}

void ikbd_handler_pause(void)
{
	ikbd.state |= IKBD_STATE_PAUSED;
}

void ikbd_handler_set_joystick_event_reporting(void)
{
	ikbd_debugf("Set Joystick event reporting");
	ikbd.state |= IKBD_STATE_JOYSTICK_EVENT_REPORTING;
	ikbd.state &= ~IKBD_STATE_PAUSED;
}

void ikbd_handler_set_joystick_interrogation_mode(void)
{
	ikbd_debugf("Set Joystick interrogation mode");
	ikbd.state &= ~IKBD_STATE_JOYSTICK_EVENT_REPORTING;
	ikbd.state &= ~IKBD_STATE_PAUSED;
}

void ikbd_handler_interrogate_joystick(void)
{
	// send reply
	enqueue(0xfd);
	enqueue(ikbd.joy[0].state | ((ikbd.mouse.but & (1 << 0)) ? 0x80 : 0x00));
	enqueue(ikbd.joy[1].state | ((ikbd.mouse.but & (1 << 1)) ? 0x80 : 0x00));
}

void ikbd_handler_disable_joysticks(void)
{
	ikbd_debugf("Disable joysticks");
	ikbd.state &= ~IKBD_STATE_JOYSTICK_EVENT_REPORTING;
}

void ikbd_handler_time_set(void)
{
	unsigned char c;
	for (c = 0;
		 c < 6;
		 c++)
		ikbd.date[c] = bcd2bin(ikbd.buffer.command.date[c]);

	// release SPI since it will be used by usb when
	// reading the time from the rtc
	DisableIO();

	// try to set time on rtc if present
	//usb_rtc_set_time(ikbd.date);

	spi_uio_cmd_cont(UIO_IKBD_IN);

	ikbd_debugf("Time of day clock set: %u:%02u:%02u %u.%u.%u", ikbd.date[3], ikbd.date[4], ikbd.date[5], ikbd.date[2], ikbd.date[1], 1900 + ikbd.date[0]);
}

void ikbd_handler_interrogate_time(void)
{
	unsigned char i;

	// release SPI since it will be used by usb when
	// reading the time from the rtc
	DisableIO();

	// try to fetch time from rtc if present
	//usb_rtc_get_time(ikbd.date);

	spi_uio_cmd_cont(UIO_IKBD_IN);

	ikbd_debugf("Interrogate time of day %u:%02u:%02u %u.%u.%u", ikbd.date[3], ikbd.date[4], ikbd.date[5], ikbd.date[2], ikbd.date[1], 1900 + ikbd.date[0]);

	enqueue(0x8000 + 64);   // wait 64ms
	enqueue(0xfc);
	for (i = 0;
		 i < 6;
		 i++)
		enqueue(bin2bcd(ikbd.date[i]));
}

void ikbd_handler_reset(void)
{
	ikbd_debugf("Reset %x", ikbd.buffer.command.reset);

	if (ikbd.buffer.command.reset == 1) {
		ikbd.state = IKBD_DEFAULT;

		enqueue(0x8000 + 300);   // wait 300ms
		enqueue(0xf0);
	}
}

// ---- list of supported ikbd commands ----
struct ikbd_command_handler_t
{
	unsigned char code;
	unsigned char length;

	void (*handler)(void);
};

ikbd_command_handler_t ikbd_command_handler[] = {{0x07, 2, ikbd_handler_mouse_button_action},
												 {0x08, 1, ikbd_handler_set_relative_mouse_pos},
												 {0x09, 5, ikbd_handler_set_abs_mouse_pos},
												 {0x0a, 3, ikbd_handler_set_mouse_keycode_mode},
												 {0x0b, 3, ikbd_handler_set_mouse_threshold},
												 {0x0c, 3, ikbd_handler_set_mouse_scale},
												 {0x0d, 1, ikbd_handler_interrogate_mouse_pos},
												 {0x0e, 6, ikbd_handler_load_mouse_pos},
												 {0x0f, 1, ikbd_handler_set_y_bottom},
												 {0x10, 1, ikbd_handler_set_y_top},
												 {0x11, 1, ikbd_handler_resume},
												 {0x12, 1, ikbd_handler_disable_mouse},
												 {0x13, 1, ikbd_handler_pause},
												 {0x14, 1, ikbd_handler_set_joystick_event_reporting},
												 {0x15, 1, ikbd_handler_set_joystick_interrogation_mode},
												 {0x16, 1, ikbd_handler_interrogate_joystick},
												 {0x1a, 1, ikbd_handler_disable_joysticks},
												 {0x1c, 1, ikbd_handler_interrogate_time},
												 {0x1b, 7, ikbd_handler_time_set},
												 {0x80, 2, ikbd_handler_reset},
												 {0,    0, NULL}    // end of list
};

void ikbd_init()
{
	// reset ikbd state
	memset(&ikbd, 0, sizeof(ikbd));
	ikbd.state = IKBD_DEFAULT | IKBD_STATE_WAIT4RESET;

	ikbd.mouse.abs.max.x = ikbd.mouse.abs.max.y = 65535;
	ikbd.mouse.abs.scale.x = ikbd.mouse.abs.scale.y = 1;

	ikbd_debugf("Init");

	// init ikbd date to some default
	ikbd.date[0] = 113;
	ikbd.date[1] = 7;
	ikbd.date[2] = 20;
	ikbd.date[3] = 20;
	ikbd.date[4] = 58;

	// handle auto events
	ikbd.auto_timer = GetTimer(0);
	ikbd.rtc_timer = GetTimer(1000);
}

void ikbd_reset(void)
{
	ikbd.tx_cnt = 0;
	ikbd.state |= IKBD_STATE_WAIT4RESET;
}

// process inout from atari core into ikbd
void ikbd_handle_input(unsigned char cmd)
{
	// store byte in buffer
	unsigned char *byte = ikbd.buffer.byte;
	byte[(int) (ikbd.buffer.size++)] = cmd;

	// check if there's a known command in the buffer
	int c;
	for (c = 0;
		 ikbd_command_handler[c].length && (ikbd_command_handler[c].code != ikbd.buffer.command.code);
		 c++);

	// not a valid command? -> flush buffer
	if (!ikbd_command_handler[c].length) {
		ikbd.buffer.size = 0;
	} else {
		// valid command and enough bytes?
		if (ikbd_command_handler[c].length == ikbd.buffer.size) {
			ikbd_command_handler[c].handler();
			ikbd.buffer.size = 0;
		}
	}
}

// advance the ikbd time by one second
static void ikbd_update_time()
{
	static const char mdays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	short year = 1900 + ikbd.date[0];
	char is_leap = (!(year % 4) && (year % 100)) || !(year % 400);

	// advance seconds
	ikbd.date[5]++;
	if (ikbd.date[5] == 60) {
		ikbd.date[5] = 0;

		// advance minutes
		ikbd.date[4]++;
		if (ikbd.date[4] == 60) {
			ikbd.date[4] = 0;

			// advance hours
			ikbd.date[3]++;
			if (ikbd.date[3] == 24) {
				ikbd.date[3] = 0;

				// advance days
				ikbd.date[2]++;
				if ((ikbd.date[2] == mdays[ikbd.date[1] - 1] + 1) || (is_leap && (ikbd.date[1] == 2) && (ikbd.date[2] == 29))) {
					ikbd.date[2] = 1;

					// advance month
					ikbd.date[1]++;
					if (ikbd.date[1] == 13) {
						ikbd.date[1] = 0;

						// advance year
						ikbd.date[0]++;
					}
				}
			}
		}
	}
}

void ikbd_poll(void)
{
#ifdef IKBD_DEBUG
	static unsigned long xtimer = 0;
	static unsigned int last_cnt = 0;
	if (CheckTimer(xtimer)) {
		xtimer = GetTimer(2000);
		if (ikbd.tx_cnt != last_cnt) {
			ikbd_debugf("sent bytes: %d", ikbd.tx_cnt);
			last_cnt = ikbd.tx_cnt;
		}
	}
#endif
	if (CheckTimer(ikbd.rtc_timer)) {
		ikbd.rtc_timer = GetTimer(1000);
		ikbd_update_time();
	}

	// do auto events every 20ms
	if (CheckTimer(ikbd.auto_timer)) {
		ikbd.auto_timer = GetTimer(IKBD_AUTO_MS);

		if (!(ikbd.state & IKBD_STATE_WAIT4RESET) && !(ikbd.state & IKBD_STATE_PAUSED)) {

			/* --------- joystick ---------- */
			if (ikbd.state & IKBD_STATE_JOYSTICK_EVENT_REPORTING) {
				int i;
				for (i = 0;
					 i < 2;
					 i++) {
					unsigned char state = ikbd.joy[i].state;

					// left mouse button 1 is also joystick 0 fire button
					// right mouse button 0 is also joystick 1 fire button
					if (ikbd.mouse.but & (2 >> i)) { state |= 0x80; }

					if (state != ikbd.joy[i].prev) {
						// printf("JOY%d: %x\n", i, state);
						enqueue(0xfe + i);
						enqueue(state);
						ikbd.joy[i].prev = state;
					}
				}
			}

			/* ----------- relative mouse ---------- */
			if (!(ikbd.state & IKBD_STATE_MOUSE_DISABLED) && !(ikbd.state & IKBD_STATE_MOUSE_ABSOLUTE)) {
				unsigned char b = ikbd.mouse.but;

				// include joystick buttons into mouse state
				if (ikbd.joy[0].state & 0x80) { b |= 2; }
				if (ikbd.joy[1].state & 0x80) { b |= 1; }

				if (ikbd.mouse.x || ikbd.mouse.y || (b != ikbd.mouse.but_prev)) {
					do {
						char x, y;
						if (ikbd.mouse.x < -128) { x = -128; }
						else if (ikbd.mouse.x > 127) { x = 127; }
						else { x = ikbd.mouse.x; }

						if (ikbd.mouse.y < -128) { y = -128; }
						else if (ikbd.mouse.y > 127) { y = 127; }
						else { y = ikbd.mouse.y; }

						// printf("RMOUSE: %x %x %x\n", b, x&0xff, y&0xff);
						enqueue(0xf8 | b);
						enqueue(x & 0xff);
						enqueue(y & 0xff);

						ikbd.mouse.x -= x;
						ikbd.mouse.y -= y;

					} while (ikbd.mouse.x || ikbd.mouse.y);

					// check if mouse buttons are supposed to be treated like keys
					if (ikbd.state & IKBD_STATE_MOUSE_BUTTON_AS_KEY) {

						// check if mouse button state has changed
						if (b != ikbd.mouse.but_prev) {
							// Mouse buttons act like keys (LEFT=0x74 & RIGHT=0x75)

							// handle left mouse button
							if ((b ^ ikbd.mouse.but_prev) & 2) { ikbd_keyboard(0x74 | ((b & 2) ? 0x00 : 0x80)); }
							// handle right mouse button
							if ((b ^ ikbd.mouse.but_prev) & 1) { ikbd_keyboard(0x75 | ((b & 1) ? 0x00 : 0x80)); }
						}
					}

					ikbd.mouse.but_prev = b;
				}
			}
		}
	}

	static unsigned long mtimer = 0;
	if (CheckTimer(mtimer)) {
		mtimer = GetTimer(10);

		// check for incoming ikbd data
		spi_uio_cmd_cont(UIO_IKBD_IN);

		while (spi_in())
			ikbd_handle_input(spi_in());

		DisableIO();
	}

	// everything below must not happen faster than 1khz
	static unsigned long rtimer = 0;
	if (!CheckTimer(rtimer)) {
		return;
	}

	// next event 1 ms later
	rtimer = GetTimer(1);

	// timer active?
	if (ikbd_timer) {
		if (!CheckTimer(ikbd_timer)) {
			return;
		}

		ikbd_timer = 0;
	}

	if (rptr == wptr) { return; }

	if (tx_queue[rptr] & 0x8000) {

		// request to start timer?
		if (tx_queue[rptr] & 0x8000) {
			ikbd_timer = GetTimer(tx_queue[rptr] & 0x3fff);
		}

		rptr = (rptr + 1) & (QUEUE_LEN - 1);
		return;
	}

	// transmit data from queue
	spi_uio_cmd_cont(UIO_IKBD_OUT);
	spi8(tx_queue[rptr]);
	DisableIO();

	ikbd.tx_cnt++;

	rptr = (rptr + 1) & (QUEUE_LEN - 1);
}

// called from external parts to report joystick states
void ikbd_joystick(unsigned char joystick, unsigned char map)
{
	ikbd.joy[joystick].state = joystick_map2ikbd(map);
}

void ikbd_keyboard(unsigned char code)
{
#ifdef IKBD_DEBUG
	ikbd_debugf("send keycode %x%s", code & 0x7f, (code & 0x80) ? " BREAK" : "");
#endif
	enqueue(code);
}

void ikbd_mouse(unsigned char b, signed char x, signed char y)
{

	// honour reversal of y axis
	if (ikbd.state & IKBD_STATE_MOUSE_Y_BOTTOM) {
		y = -y;
	}

	// update relative mouse state
	ikbd.mouse.but = ((b & 1) ? 2 : 0) | ((b & 2) ? 1 : 0);
	ikbd.mouse.x += x;
	ikbd.mouse.y += y;

	// save button state for absolute mouse reports

	if (ikbd.state & IKBD_STATE_MOUSE_ABSOLUTE) {
		// include joystick buttons into mouse state
		if (ikbd.joy[0].state & 0x80) { b |= 2; }
		if (ikbd.joy[1].state & 0x80) { b |= 1; }

		if (b & 2) { ikbd.mouse.abs.buttons |= 1; }
		else { ikbd.mouse.abs.buttons |= 2; }
		if (b & 1) { ikbd.mouse.abs.buttons |= 4; }
		else { ikbd.mouse.abs.buttons |= 8; }

		if (ikbd.mouse.abs.scale.x > 1) { x *= ikbd.mouse.abs.scale.x; }
		if (ikbd.mouse.abs.scale.y > 1) { y *= ikbd.mouse.abs.scale.y; }

		//    ikbd_debugf("abs inc %d %d -> ", x, y);

		if (x < 0) {
			x = -x;

			if (ikbd.mouse.abs.pos.x > x) { ikbd.mouse.abs.pos.x -= x; }
			else { ikbd.mouse.abs.pos.x = 0; }
		} else if (x > 0) {
			if (ikbd.mouse.abs.pos.x < ikbd.mouse.abs.max.x - x) {
				ikbd.mouse.abs.pos.x += x;
			} else {
				ikbd.mouse.abs.pos.x = ikbd.mouse.abs.max.x;
			}
		}

		if (y < 0) {
			y = -y;
			if (ikbd.mouse.abs.pos.y > y) { ikbd.mouse.abs.pos.y -= y; }
			else { ikbd.mouse.abs.pos.y = 0; }
		} else if (y > 0) {
			if (ikbd.mouse.abs.pos.y < ikbd.mouse.abs.max.y - y) {
				ikbd.mouse.abs.pos.y += y;
			} else {
				ikbd.mouse.abs.pos.y = ikbd.mouse.abs.max.y;
			}
		}
	}
}
