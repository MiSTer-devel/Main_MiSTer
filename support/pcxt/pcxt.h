#ifndef PCXT_H
#define PCXT_H

#include "../../file_io.h"

void pcxt_init(void);
void* OpenUART(void*);
void log(int level, const char* message, ...);
unsigned long GetTime(void);
unsigned long GetTime_Timeout(void);

void pcxt_poll();
void pcxt_unmount_images();
void pcxt_load_images();
void pcxt_set_image(int num, char* selPath);
void hdd_set(int num, char* selPath);
void pcxt_config_load();
void pcxt_config_save();
const char* pcxt_get_image_name(int num);
const char* pcxt_get_image_path(int num);


#endif // PCXT_H