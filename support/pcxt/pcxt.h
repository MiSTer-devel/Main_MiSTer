#ifndef PCXT_H
#define PCXT_H

#include "../../file_io.h"

void pcxt_init(bool rom_by_model);
void pcxt_model_rom();
void* OpenUART(void*);
void log(int level, const char* message, ...);
unsigned long GetTime(void);
unsigned long GetTime_Timeout(void);

void pcxt_unmount_images();
void pcxt_load_images();
void pcxt_set_image(int num, char* selPath);
void pcxt_config_load();
void pcxt_config_save();
const char* pcxt_get_image_name(int num);
const char* pcxt_get_image_path(int num);


#endif // PCXT_H