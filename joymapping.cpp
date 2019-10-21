/*  
This file contains lookup information on known controllers
*/

#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include "joymapping.h"
#include "menu.h"
#include "input.h"
#include "user_io.h"

#define DPAD_COUNT 4

/*****************************************************************************/

int swap_face_buttons(const char *core_name) {
	//flags cores where B A should be swapped to A B and Y X to X Y (SNES vs. Genesis/NG)
	return    (!strcmp(core_name, "Genesis") 
			|| !strcmp(core_name, "NEOGEO")
			|| !strcmp(core_name, "SMS"));
}

int map_joystick(const char *core_name, devInput (&input)[NUMDEV], int dev) {
	/*
	attemps to centrally defined core joy mapping to the joystick declaredy by a core config string
	we use the names declared by core with some special handling for specific edge cases
	
	Input button order is "virtual SNES" i.e.:
		A, B, X, Y, L, R, Select, Start
	*/
	uint32_t new_map[NUMBUTTONS];
	// first copy directions (not really needed but helps keep things consistent)
	for(int i=0; i<DPAD_COUNT; i++) {
		new_map[i] = input[dev].map[i];
	}
	// loop through core requested buttons and construct result map
	for (int i=0; i<joy_bcount; i++) {
		int new_index = i+DPAD_COUNT;
		std::string button_name = joy_bnames[i];
		if (button_name.find("(") != std::string::npos) {
			button_name = button_name.substr(0, button_name.find("("));
		}
		const char*btn_name = button_name.c_str();
		printf("  ...mapping button %s\n", btn_name);
		if(!strcmp(btn_name, "A") 
		|| !strcmp(btn_name, "Fire")
		|| !strcmp(btn_name, "Jump")
		|| !strcmp(btn_name, "Fire1")
		|| !strcmp(btn_name, "Fire 1") 
		|| !strcmp(btn_name, "Button 1") 
		|| !strcmp(btn_name, "Button I")) {
			if(swap_face_buttons(core_name)) {
				new_map[new_index] = input[dev].map[MENU_JOY_B];
			} else {
				new_map[new_index] = input[dev].map[MENU_JOY_A];
			}
			continue;
		}
		if(!strcmp(btn_name, "B") 
		|| !strcmp(btn_name, "Fire2")
		|| !strcmp(btn_name, "Fire 2") 
		|| !strcmp(btn_name, "Button 2") 
		|| !strcmp(btn_name, "Button II")) {
			if(swap_face_buttons(core_name)) {
				new_map[new_index] = input[dev].map[MENU_JOY_A];
			} else {
				new_map[new_index] = input[dev].map[MENU_JOY_B];
			}
			continue;
		}
		if(!strcmp(btn_name, "X") 
		|| !strcmp(btn_name, "Fire3")
		|| !strcmp(btn_name, "Fire 3") 
		|| !strcmp(btn_name, "Button 3") 
		|| !strcmp(btn_name, "Button III")) {
			if(swap_face_buttons(core_name)) {
				new_map[new_index] = input[dev].map[MENU_JOY_Y];
			} else {
				new_map[new_index] = input[dev].map[MENU_JOY_X];
			}
			continue;
		}
		if(!strcmp(btn_name, "Y") 
		|| !strcmp(btn_name, "Button IV")) {
			if(swap_face_buttons(core_name)) {
				new_map[new_index] = input[dev].map[MENU_JOY_X];
			} else {
				new_map[new_index] = input[dev].map[MENU_JOY_Y];
			}
			continue;
		}
		// Genesis C and Z  and TG16 V and VI
		if(!strcmp(btn_name, "C")
		|| !strcmp(btn_name, "Button V")
		|| !strcmp(btn_name, "Coin")
		) {
			new_map[new_index] = input[dev].map[MENU_JOY_R];
			continue;
		}
		if(!strcmp(btn_name, "Z")
		|| !strcmp(btn_name, "Button VI")
		|| !strcmp(btn_name, "ABC")) {
			new_map[new_index] = input[dev].map[MENU_JOY_L];
			continue;
		}
		if(!strcmp(btn_name, "Select") 
		|| !strcmp(btn_name, "Game Select")) {
			new_map[new_index] = input[dev].map[MENU_JOY_SELECT];
			continue;
		}
		if(!strcmp(btn_name, "Start") 
		|| !strcmp(btn_name, "Run")
		|| !strcmp(btn_name, "Pause")
		|| !strcmp(btn_name, "Start 1P")) {
			new_map[new_index] = input[dev].map[MENU_JOY_START];
			continue;
		}
		//nothing found so just map by position
		printf("     [no mapping found, using default map by position]\n");
		new_map[new_index] = input[dev].map[new_index];
	}
	//finally swap result map into input map
	for (int i=0; i<NUMBUTTONS; i++) {
		if (i<joy_bcount+DPAD_COUNT) 
			input[dev].map[i] = new_map[i];
		else
			input[dev].map[i] = 0;
	}
	return 1; //success
}
