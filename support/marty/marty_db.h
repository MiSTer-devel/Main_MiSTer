// FM Towns Marty settings database: names the title on a mounted CD or
// floppy and says what it needs of the machine.
#ifndef MARTY_DB_H
#define MARTY_DB_H

#include <stdint.h>
#include <stddef.h>
#include <string>

// Values in the OSD's own order, so each one goes straight into its bits.
struct mdb_settings
{
	uint8_t ram;       // 2 MB, 4 MB, 6 MB, 8 MB
	uint8_t speed;     // Original, Plus, Great Scott
	uint8_t pad1;      // Marty pad, 6-button, mouse, analog stick, analog pad, none, Capcom
	uint8_t pad2;
	uint8_t machine;   // FM Towns, Marty
	uint8_t fdd;       // one drive, two
	uint8_t boot;      // 1: needs a boot floppy the disc does not carry
	uint8_t save;      // 1: needs a writable user disk
	uint8_t speed_given;   // the title states its speed; the date rule leaves it
	uint16_t year;     // the title's release year, 0 when unknown
};

// Keys. A CD is keyed by its primary volume descriptor (the 2048 bytes of
// user data of sector 16), a floppy by the first bytes of its file and its
// size, and either by its file name, given without path or extension.
std::string mdb_cd_key(const uint8_t *pvd);
std::string mdb_fd_key(const uint8_t *head, size_t len, uint64_t size);
std::string mdb_exact_key(const char *name);
std::string mdb_loose_key(const char *name);
std::string mdb_japanese_key(const char *name);

// The creation year a primary volume descriptor records, 0 when it records
// none or an implausible one.
int mdb_cd_year(const uint8_t *pvd);

// Loads the shipped file and the user's (either may be missing); a later
// call reloads only when one of them has changed. Returns titles loaded.
int mdb_load(const char *path, const char *user_path);

// The title a key names, its settings resolved against the defaults.
// Returns 0 when the key names nothing.
int mdb_find(const std::string &key, mdb_settings *s, std::string *title);

void mdb_defaults(mdb_settings *s);

// The speed the file's date rule gives a disc made in the given year; 0 when
// no rule covers it.
int mdb_date_speed(int year, uint8_t *speed);

#endif
