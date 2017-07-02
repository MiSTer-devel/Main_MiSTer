
#ifndef EVINPUT_H
#define EVINPUT_H

#define HID_LED_NUM_LOCK    1
#define HID_LED_CAPS_LOCK   2
#define HID_LED_SCROLL_LOCK 4
#define HID_LED_MASK        7

void set_kbdled(int mask, int state);
int  get_kbdled(int mask);
int  toggle_kbdled(int mask);

int input_poll(int getchar);


void start_map_setting(int cnt);
int  get_map_button();
void finish_map_setting();
uint16_t get_map_vid();
uint16_t get_map_pid();

#endif
