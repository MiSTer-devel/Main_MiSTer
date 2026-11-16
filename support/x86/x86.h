#ifndef X86_H
#define X86_H

void x86_init(int core_start = 0);

void x86_ide_set();
uint16_t x86_ide_poll();

void x86_set_image(int num, char *filename);
const char* x86_get_image_name(int num);
const char* x86_get_image_path(int num);

void x86_config_load();
void x86_config_save();
void x86_set_fdd_boot(uint32_t boot);

char is_x86();
char is_pcxt();
char is_pcat286();


#endif
