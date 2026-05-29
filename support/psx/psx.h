#ifndef PSX_H
#define PSX_H

void psx_mount_cd(int f_index, int s_index, const char *filename);
void psx_request_bios_save_mount();
bool psx_is_memcard_source_option(const char* opt);
uint32_t psx_memcard_source_normalize(const char* opt, uint32_t source);
uint32_t psx_memcard_source_next(const char* opt, uint32_t source, bool reverse);
void psx_memcard_source_changed();
void psx_fill_blanksave(uint8_t *buffer, uint32_t lba, int cnt);
void psx_read_cd(uint8_t *buffer, int lba, int cnt);
int psx_chd_hunksize();
const char* psx_get_game_id();
void psx_poll();
void psx_reset();

#endif
