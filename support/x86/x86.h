#ifndef X86_H
#define X86_H

void x86_init();
void x86_poll();

void x86_set_image(int num, char *filename);
const char* x86_get_image_name(int num);
const char* x86_get_image_path(int num);

void x86_config_load();
void x86_config_save();
void x86_set_fdd_boot(uint32_t boot);
void x86_set_uart_mode(int mode);

void x86_dma_set(uint32_t address, uint32_t data);
void x86_dma_sendbuf(uint32_t address, uint32_t length, uint32_t *data);
void x86_dma_recvbuf(uint32_t address, uint32_t length, uint32_t *data);

#endif
