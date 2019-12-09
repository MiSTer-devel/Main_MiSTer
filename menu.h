#ifndef MENU_H
#define MENU_H

#include <inttypes.h>

void HandleUI(void);
void menu_key_set(unsigned int c);
void PrintFileName(char *name, int row, int maxinv);
void PrintDirectory(void);
void ScrollLongName(void);

void ErrorMessage(const char *message, unsigned char code);
void InfoMessage(const char *message, int timeout = 2000, const char *title = "Message");
void Info(const char *message, int timeout = 2000, int width = 0, int height = 0, int frame = 0);
void MenuHide();

uint32_t getStatus(char *opt, uint32_t status);
void substrcpy(char *d, char *s, char idx);

void open_joystick_setup();
int menu_lightgun_cb(uint16_t type, uint16_t code, int value);

#endif
