#include <stddef.h>
#include <string.h>
#include <byteswap.h>

#include "mister_vhd.h"

vhd_error validate_vhd_checksum(vhd_footer *footer)
{
    uint32_t checksum = 0;
    const uint8_t *fp = (const uint8_t *)footer;
    uint16_t checksum_offset = offsetof(vhd_footer, checksum);

    for (uint16_t i = 0; i < sizeof(vhd_footer); i++)
    {
        if (i >= checksum_offset && i < checksum_offset + sizeof(footer->checksum)) 
        {
            continue;
        }
        checksum += fp[i];
    }
    checksum = ~checksum;
    if (checksum != bswap_32(footer->checksum)) 
    {
        return VHDERR_CHECKSUM;
    }
    else
    {
        return VHDERR_NONE;
    }
}

// parse_vhd_geometry will 
vhd_error parse_vhd_geometry(fileTYPE *f, vhd_geometry *geometry) 
{
    vhd_footer footer;
    int offset = sizeof(footer);

    // verify input
    if ((f->filp == NULL && f->zip == NULL) || !f->opened() || geometry == NULL)
    {
        return VHDERR_INVALID_INPUT;
    }

    int f_offset = f->offset;


    // verify the file is large enough to contain the footer
    if (f->size < offset) 
    {
        return VHDERR_INVALID_FILESIZE;
    }

    // set the file pointer to `offset` bytes before the end and read the footer structure
    FileSeek(f, -offset, SEEK_END);
    FileReadAdv(f, &footer, offset);

    // verify the magic cookie
    if (!memcmp(footer.cookie, VHD_COOKIE, 8)==0) 
    {
        return VHDERR_INVALID_COOKIE;
    }

    // only fixed type is supported
    switch (bswap_32(footer.type))
    {
    case VHD_TYPE_FIXED:
        break;
    case VHD_TYPE_NONE:
    case VHD_TYPE_DYNAMIC:
    case VHD_TYPE_DIFFERENTIAL:
    default:
        return VHDERR_UNSUPPORTED_TYPE;
    }

    if (!validate_vhd_checksum(&footer)==VHDERR_NONE)
    {
        return VHDERR_CHECKSUM;
    }

    // set the geometry values
    geometry->size = bswap_64(footer.current_size);
    geometry->cylinders = bswap_16(footer.cylinders);
    geometry->heads = footer.heads;
    geometry->spt = footer.spt;

    // reset offset to where we found it
    FileSeek(f, f_offset, SEEK_SET);

    printf("Real VHD detected: size %lli, cyl %d, head %d, spt %d\n",geometry->size, geometry->cylinders, geometry->heads, geometry->spt);
    return VHDERR_NONE;
}
