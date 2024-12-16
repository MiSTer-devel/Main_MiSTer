#ifndef CDI_H
#define CDI_H

void cdi_mount_cd(int s_index, const char *filename);
void cdi_fill_blanksave(uint8_t *buffer, uint32_t lba, int cnt);
void cdi_read_cd(uint8_t *buffer, int lba, int cnt);
int cdi_chd_hunksize();
const char* cdi_get_game_id();
void cdi_poll();

#endif
