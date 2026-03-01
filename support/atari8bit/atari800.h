#ifndef __ATARI800_H__
#define __ATARI800_H__

int atari800_get_match_cart_count();
const char *atari800_get_cart_match_name(int match_index);
void atari800_init();
void atari800_reset();
void atari800_poll();
void atari800_umount_cartridge(uint8_t stacked);
int atari800_check_cartridge_file(const char* name, unsigned char index);
void atari800_open_cartridge_file(const char* name, int match_index);
void atari800_open_bios_file(const char* name, unsigned char index);
void atari800_set_image(int ext_index, int file_index, const char *name);

#endif
