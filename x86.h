#ifndef X86_H
#define X86_H

void x86_init();
void x86_poll();
int  x86_send(fileTYPE *f, uint8_t index);

void x86_set_image(int num, char *filename);

void x86_config_load();
void x86_config_save();
void x86_set_fdd_boot(uint32_t boot);

#endif
