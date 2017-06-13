/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

This code keeps status of MiST state

*/

#include <string.h>
#include "stdio.h"

#include "state.h"
#include "osd.h"
#include "hardware.h"

//#include "charrom.h"



// for I/O 
static mist_joystick_t mist_joystick_temp = {
	.vid = 0,
	.pid = 0,
	.num_buttons = 1, // DB9 has 1 button
	.state = 0,
	.state_extra = 0,
	.usb_state = 0,
	.usb_state_extra = 0,
	.turbo = 0,
	.turbo_counter = 0,
	.turbo_mask = 0x30,	 // A and B buttons		
	.turbo_state = 0xFF  // flip state (0 or 1)
};

/* latest joystick state */
static mist_joystick_t mist_joysticks[3] = { // 3rd one is dummy, used to store defaults
	{
		.vid = 0,
		.pid = 0,
	.num_buttons = 1, // DB9 has 1 button
	.state = 0,
	.state_extra = 0,
	.usb_state = 0,
	.usb_state_extra = 0,
	.turbo = 0,
	.turbo_counter = 0,
	.turbo_mask = 0x30,	 // A and B buttons		
	.turbo_state = 0xFF  // flip state (0 or 1)
	},
	{
		.vid = 0,
		.pid = 0,
	.num_buttons = 1, // DB9 has 1 button
	.state = 0,
	.state_extra = 0,
	.usb_state = 0,
	.usb_state_extra = 0,
	.turbo = 0,
	.turbo_counter = 0,
	.turbo_mask = 0x30, // A and B buttons		
	.turbo_state = 0xFF // flip state (0 or 1)
	},
	{
		.vid = 0,
		.pid = 0,
	.num_buttons = 1, // DB9 has 1 button
	.state = 0,
	.state_extra = 0,
	.usb_state = 0,
	.usb_state_extra = 0,
	.turbo = 0,
	.turbo_counter = 0,
	.turbo_mask = 0x30, // A and B buttons		
	.turbo_state = 0xFF // flip state (0 or 1)
	}
};

void joy_reset(mist_joystick_t joy) {
	joy.vid = 0;
	joy.pid = 0;
	joy.num_buttons = 1; // DB9 has 1 button
	joy.state = 0;
	joy.state_extra = 0;
	joy.usb_state = 0;
	joy.usb_state_extra = 0;
	joy.turbo = 0;
	joy.turbo_counter = 0;
	joy.turbo_mask = 0x30;	 // A and B buttons		
	joy.turbo_state = 0xFF;  // flip state (0 or 1)
}

// sets a joystick to input status
void StateJoyCopy(uint8_t num_joy, mist_joystick_t* joy) {
	mist_joystick_t mine;
	if (num_joy>1) return;
	if (!joy) return;
	mine = mist_joysticks[num_joy];
	mine.vid = joy->vid;
	mine.pid = joy->pid;
	mine.num_buttons = joy->num_buttons;
	mine.state = joy->state;
	mine.state_extra = joy->state_extra;
	mine.usb_state = joy->usb_state;
	mine.usb_state_extra = joy->usb_state_extra;
	mine.turbo = joy->turbo;
	mine.turbo_counter = joy->turbo_counter;
	mine.turbo_mask = joy->turbo_mask;
	mine.turbo_state = joy->turbo_state;
}

void StateJoyRead(uint8_t num_joy, mist_joystick_t* joy) {
	mist_joystick_t mine;
	if (num_joy>1) return;
	if (!joy) return;
	mine = mist_joysticks[num_joy];
	joy->vid = mine.vid;
	joy->pid = mine.pid;
	joy->num_buttons = mine.num_buttons;
	joy->state = mine.state;
	joy->state_extra = mine.state_extra;
	joy->usb_state = mine.usb_state;
	joy->usb_state_extra = mine.usb_state_extra;
	joy->turbo = mine.turbo;
	joy->turbo_counter = mine.turbo_counter;
	joy->turbo_mask = mine.turbo_mask;
	joy->turbo_state = mine.turbo_state;
}

// returns a copy of a status structure
mist_joystick_t StateJoyGetStructure(uint8_t num_joy) {
	StateJoyRead(num_joy, &mist_joystick_temp);
	return mist_joystick_temp;
}

// applies the turbo to a given joystick
mist_joystick_t StateJoyUpdateTurboStructure(uint8_t num_joy) {
	StateJoyRead(num_joy, &mist_joystick_temp);
	StateTurboUpdate(&mist_joystick_temp);
	//mist_joystick_t mine = mist_joystick_temp;
	StateJoyCopy(num_joy, &mist_joystick_temp);
}

uint8_t StateJoyStructureState(uint8_t num_joy) {
	mist_joystick_t mine;
	mine = StateJoyGetStructure(num_joy);
	return mine.state;
}

/* latest joystick state */
static uint8_t osd_joy;
static uint8_t osd_joy_extra;
static uint8_t osd_joy2;
static uint8_t osd_joy_extra2;
void StateJoySet(uint8_t c, uint8_t joy_num) {
	//iprintf("OSD joy: %x\n", c);
	if (joy_num == 0)
		osd_joy = c;
	else
		osd_joy2 = c;
}
void StateJoySetExtra(uint8_t c, uint8_t joy_num) {
	if (joy_num == 0)
		osd_joy_extra = c;
	else
		osd_joy_extra2 = c;
}
uint8_t StateJoyGet(uint8_t joy_num) {
	return joy_num == 0 ? osd_joy : osd_joy2;
}
uint8_t StateJoyGetExtra(uint8_t joy_num) {
	return joy_num == 0 ? osd_joy_extra : osd_joy_extra2;
}

static uint8_t raw_usb_joy;	      // four directions and 4 buttons
static uint8_t raw_usb_joy_extra; // eight extra buttons
static uint8_t raw_usb_joy_b;	      // four directions and 4 buttons
static uint8_t raw_usb_joy_extra_b; // eight extra buttons
void StateUsbJoySet(uint8_t usbjoy, uint8_t usbextra, uint8_t joy_num) {
	if (joy_num == 0) {
		raw_usb_joy = usbjoy;
		raw_usb_joy_extra = usbextra;
	}
	else {
		raw_usb_joy_b = usbjoy;
		raw_usb_joy_extra_b = usbextra;
	}
}

uint8_t StateUsbJoyGet(uint8_t joy_num) {
	return (joy_num == 0) ? raw_usb_joy : raw_usb_joy_b;
}
uint8_t StateUsbJoyGetExtra(uint8_t joy_num) {
	return (joy_num == 0) ? raw_usb_joy_extra : raw_usb_joy_extra_b;
}

static uint16_t usb_vid;
static uint16_t usb_pid;
static uint8_t num_buttons;
static uint16_t usb_vid_b;
static uint16_t usb_pid_b;
static uint8_t num_buttons_b;
void StateUsbIdSet(uint16_t vid, uint16_t pid, uint8_t num, uint8_t joy_num) {
	if (joy_num == 0) {
		usb_vid = vid;
		usb_pid = pid;
		num_buttons = num;
	}
	else {
		usb_vid_b = vid;
		usb_pid_b = pid;
		num_buttons_b = num;
	}
}
uint16_t StateUsbVidGet(uint8_t joy_num) {
	return joy_num == 0 ? usb_vid : usb_vid_b;
}
uint16_t StateUsbPidGet(uint8_t joy_num) {
	return joy_num == 0 ? usb_pid : usb_pid_b;
}
uint8_t StateUsbGetNumButtons(uint8_t joy_num) {
	return (joy_num == 0) ? num_buttons : num_buttons_b;
}

// return joystick state take into account turbo settings
void  StateJoyState(uint8_t joy_num, mist_joystick_t* joy) {
	mist_joystick_t mine;
	if (joy_num>1) return;
	if (!joy) return;
	joy->vid = StateUsbVidGet(joy_num);
	joy->pid = StateUsbPidGet(joy_num);
	//joy.num_buttons=1; // DB9 has 1 button
	joy->state = StateUsbPidGet(joy_num);
	joy->state_extra = StateJoyGetExtra(joy_num);
	joy->usb_state = StateUsbJoyGet(joy_num);
	joy->usb_state_extra = (joy_num);
	//apply turbo status
	joy->state = StateUsbPidGet(joy_num);
	if (joy->turbo > 0) {
		joy->state &= joy->turbo_state;
	}
	// chache into current static scope
	StateJoyCopy(joy_num, joy);
}

/* handle button's turbo timers */
void StateTurboUpdate(mist_joystick_t* joy) {
	if (!joy) return;
	if (joy->turbo == 0) return; // nothing to do
	joy->turbo_counter += 1;
	if (joy->turbo_counter > joy->turbo) {
		joy->turbo_counter = 0;
		joy->turbo_state ^= joy->turbo_mask;
	}
}
/* reset all turbo timers and state */
void StateTurboReset(mist_joystick_t* joy) {
	if (!joy) return;
	joy->turbo_counter = 0;
	joy->turbo_state = 0xFF;
}
/* set a specific turbo mask and timeout */
void StateTurboSet(mist_joystick_t* joy, uint16_t turbo, uint16_t mask) {
	if (!joy) return;
	StateTurboReset(joy);
	joy->turbo = turbo;
	joy->turbo_mask = mask;
}

// Keep track of connected sticks
uint8_t joysticks = 0;
uint8_t StateNumJoysticks() {
	return joysticks;
}

void StateNumJoysticksSet(uint8_t num) {
	joysticks = num;
}

/* keyboard data */
static uint8_t key_modifier = 0;
static uint8_t key_pressed[6] = { 0,0,0,0,0,0 };
static uint16_t keys_ps2[6] = { 0,0,0,0,0,0 };

void StateKeyboardPressedPS2(uint16_t *keycodes) {
	unsigned i = 0;
	for (i = 0; i<6; i++) {
		keycodes[i] = keys_ps2[i];
	}
}
void StateKeyboardSet(uint8_t modifier, uint8_t* keycodes, uint16_t* keycodes_ps2) {
	unsigned i = 0, j = 0;
	key_modifier = modifier;
	for (i = 0; i<6; i++) {
		//iprintf("Key N=%d, USB=%x, PS2=%x\n", i, keycodes[i], keycodes_ps2[i]);
		if (((keycodes[i] & 0xFF) != 0xFF) && (keycodes[i] & 0xFF)) {
			key_pressed[j] = keycodes[i];
			if ((keycodes_ps2[i] & 0xFF) != 0xFF) {
				// translate EXT into 0E
				if (0x1000 & keycodes_ps2[i]) {
					keys_ps2[j++] = (keycodes_ps2[i] & 0xFF) | 0xE000;
				}
				else {
					keys_ps2[j++] = keycodes_ps2[i] & 0xFF;
				}
			}
			else {
				keys_ps2[j++] = 0;
			}
		}
	}

	while (j<6) {
		key_pressed[j] = 0;
		keys_ps2[j++] = 0;
	}
}

uint8_t StateKeyboardModifiers() {
	return key_modifier;
}
void StateKeyboardPressed(uint8_t *keycodes) {
	uint8_t i = 0;
	for (i = 0; i<6; i++)
		keycodes[i] = key_pressed[i];
}


/* core currently loaded */
static char lastcorename[261 + 10] = "CORE";
void StateCoreNameSet(const char* str) {
	siprintf(lastcorename, "%s", str);
}
char* StateCoreName() {
	return lastcorename;
}

// clear all states
void StateReset() {
	strcpy(lastcorename, "CORE");
	//State_key = 0;
	//joysticks=0;
	key_modifier = 0;
	for (int i = 0; i<6; i++) {
		key_pressed[i] = 0;
		keys_ps2[i] = 0;
	}
	//joy_reset(mist_joy[0]);
	//joy_reset(mist_joy[1]);
	//joy_reset(mist_joy[2]);
}