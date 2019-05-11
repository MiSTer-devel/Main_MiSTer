#ifndef CHEATS_H
#define CHEATS_H

void cheats_init(char *rom_path, uint32_t romcrc);
int cheats_available();
void cheats_scan(int mode);
void cheats_scroll_name();
void cheats_print();
void cheats_toggle();
int cheats_loaded();

#endif
