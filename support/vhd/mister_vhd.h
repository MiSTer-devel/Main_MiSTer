#ifndef MISTER_VHD_H
#define MISTER_VHD_H

#include <inttypes.h>
#include "../../file_io.h"

#define VHD_COOKIE "conectix"

struct vhd_geometry
{
	uint64_t size;
	uint16_t cylinders;
	uint8_t heads;
	uint8_t spt;
};

struct vhd_footer
{
	char cookie[8];
	uint32_t features;
	uint32_t version;
	uint64_t offset;
	uint32_t timestamp;
	char creator[4];
	uint32_t creator_version;
	uint32_t creator_host_os;
	uint64_t original_size;
	uint64_t current_size;
	uint16_t cylinders;
	uint8_t heads;
	uint8_t spt;
	uint32_t type;
	uint32_t checksum;
	char unique_id[16];
	uint8_t saved_state;
	char reserved[427];
};

enum vhd_disk_types
{
	VHD_TYPE_NONE,
	VHD_TYPE_FIXED = 2,
	VHD_TYPE_DYNAMIC,
	VHD_TYPE_DIFFERENTIAL
};

/* error types */
enum vhd_error
{
	VHDERR_NONE,
	VHDERR_INVALID_INPUT,
	VHDERR_INVALID_FILESIZE,
	VHDERR_INVALID_COOKIE,
	VHDERR_UNSUPPORTED_TYPE,
	VHDERR_CHECKSUM
};

vhd_error parse_vhd_geometry(fileTYPE *f, vhd_geometry *geometry);

#endif
