#ifndef __ATARI5200_H__
#define __ATARI5200_H__

int atari5200_get_match_cart_count();
const char *atari5200_get_cart_match_name(int match_index);

void atari5200_init();
void atari5200_reset();
void atari5200_poll();
void atari5200_umount_cartridge();
int atari5200_check_cartridge_file(const char* name, unsigned char index);
void atari5200_open_cartridge_file(const char* name, int match_index);

#endif
