#ifndef NEOGEOCD_H
#define NEOGEOCD_H


void neocd_poll();
void neocd_set_image(char *filename);
void neocd_reset();
int neocd_send_data(uint8_t* buf, int len, uint8_t index);
int neocd_is_en();
void neocd_set_en(int enable);
#endif
