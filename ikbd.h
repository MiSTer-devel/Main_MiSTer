#ifndef IKBD_H
#define IKBD_H

void ikbd_init(void);
void ikbd_poll(void);
void ikbd_reset(void);
void ikbd_joystick(unsigned char joy, unsigned char map);
void ikbd_mouse(unsigned char buttons, char x, char y);
void ikbd_keyboard(unsigned char code);

#endif // IKBD_H
