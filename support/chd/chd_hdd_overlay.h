#ifndef CHD_HDD_OVERLAY_H
#define CHD_HDD_OVERLAY_H

#include <stdint.h>

struct chd_hdd_overlay;
int chd_hdd_overlay_open(const char *path, const char *root, uint64_t size, const uint8_t identity[20],
	uint8_t identity_type, bool writable, chd_hdd_overlay **overlay);
void chd_hdd_overlay_close(chd_hdd_overlay *overlay);
int chd_hdd_overlay_read(chd_hdd_overlay *overlay, uint64_t sector, uint8_t data[512]);
int chd_hdd_overlay_write(chd_hdd_overlay *overlay, uint64_t offset, const void *data, uint32_t length);

#endif
