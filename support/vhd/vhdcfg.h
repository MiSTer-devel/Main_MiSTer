#ifndef VHD_CFG_H
#define VHD_CFG_H

#include <inttypes.h>

#include "ide.h"

typedef enum 
{
	VHD_NOERROR = 0, VHD_NO_CONFIG, VHD_INVALID_CONFIG, VHD_INVALID_SIZE, VHD_MISSING_CONFIG
} vhd_error;

vhd_error parse_vhd_config(drive_t *drive);

#endif // VHD_CFG_H
