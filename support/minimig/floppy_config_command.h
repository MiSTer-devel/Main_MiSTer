#ifndef MINIMIG_FLOPPY_CONFIG_COMMAND_H
#define MINIMIG_FLOPPY_CONFIG_COMMAND_H

#include <stdint.h>

static inline uint8_t minimig_floppy_config_command(uint8_t drives, uint8_t speed)
{
	if (drives == 4) return 0x10 | (speed & 3);
	return ((drives & 3) << 2) | (speed & 3);
}

static inline uint8_t minimig_floppy_zero_supported(uint8_t status_hi)
{
	return !!(status_hi & 0x08);
}

static inline uint8_t minimig_floppy_zero_active(uint8_t status_hi)
{
	return minimig_floppy_zero_supported(status_hi) && !!(status_hi & 0x04);
}

static inline uint8_t minimig_floppy_requested_count(uint8_t drives)
{
	return drives == 4 ? 0 : drives + 1;
}

static inline uint8_t minimig_floppy_step_drives(uint8_t drives, int direction, uint8_t zero_supported)
{
	if (direction > 0)
	{
		if (drives == 4) return zero_supported ? 0 : 4;
		return drives < 3 ? drives + 1 : drives;
	}
	if (direction < 0)
	{
		if (!drives) return zero_supported ? 4 : 0;
		return drives <= 3 ? drives - 1 : drives;
	}
	return drives;
}

#endif
