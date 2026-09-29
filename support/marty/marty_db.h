#ifndef MARTY_DB_H
#define MARTY_DB_H

#include <stdint.h>
#include <stddef.h>
#include <string>

// values in OSD order
struct mdb_settings
{
	uint8_t ram;       // 2 MB, 4 MB, 6 MB, 8 MB
	uint8_t speed;     // Original, Plus, Great Scott
	uint8_t pad1;      // Marty pad, 6-button, mouse, analog stick, analog pad, none, Capcom
	uint8_t pad2;
	uint8_t machine;   // FM Towns, Marty
	uint8_t fdd;       // one drive, two
	uint8_t boot;      // needs a boot floppy
	uint8_t save;      // needs a save disk
	uint8_t speed_given;
	uint16_t year;     // 0 when unknown
};

// CD keyed by its PVD, floppy by its head and size, either by file name
std::string mdb_cd_key(const uint8_t *pvd);
std::string mdb_fd_key(const uint8_t *head, size_t len, uint64_t size);
std::string mdb_exact_key(const char *name);
std::string mdb_loose_key(const char *name);
std::string mdb_japanese_key(const char *name);
int mdb_cd_year(const uint8_t *pvd);

// reloads only when either file changed
int mdb_load(const char *path, const char *user_path);
int mdb_find(const std::string &key, mdb_settings *s, std::string *title);
void mdb_defaults(mdb_settings *s);
int mdb_date_speed(int year, uint8_t *speed);

#endif
