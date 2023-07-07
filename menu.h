#ifndef MENU_H
#define MENU_H

#include <inttypes.h>

void SelectFile(const char* path, const char* pFileExt, int Options, unsigned char MenuSelect, unsigned char MenuCancel);

void HandleUI(void);
void menu_key_set(unsigned int c);
void menu_process_save();
void PrintDirectory(int expand = 0);
void ScrollLongName(void);

void ProgressMessage(const char* title = 0, const char* text = 0, int current = 0, int max = 0);
void InfoMessage(const char *message, int timeout = 2000, const char *title = "Message");
void Info(const char *message, int timeout = 2000, int width = 0, int height = 0, int frame = 0);
void MenuHide();
void SelectINI();

void open_joystick_setup();
int menu_lightgun_cb(int idx, uint16_t type, uint16_t code, int value);

int menu_allow_cfg_switch();
void StoreIdx_F(int idx, const char *path);
void StoreIdx_S(int idx, const char *path);

#endif
