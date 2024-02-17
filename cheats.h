#ifndef CHEATS_H
#define CHEATS_H

struct cheat_rec_t
{
	bool enabled;
	char name[256];
	int cheatSize;
	char* cheatData;

	cheat_rec_t();
	~cheat_rec_t();
};

void cheats_init(const char *rom_path, uint32_t romcrc);
int cheats_available();
void cheats_scan(int mode);
void cheats_scroll_name();
void cheats_print();
void cheats_toggle();
int cheats_loaded();

#endif
