#ifndef NEXT_CDROM_H
#define NEXT_CDROM_H

#include <stdint.h>
#include "next_cdrom_resp.h"

#define NEXT_CDROM_SLOT  3

#define NEXT_FRAME_BLK   0x7C000000u
#define NEXT_CMD_BLK     0x7D000000u
#define NEXT_RESP_BLK    0x7E000000u
#define NEXT_WIN_BASE    NEXT_FRAME_BLK
#define NEXT_DATA_LIMIT  0x40000000u
#define NEXT_CMD_CDB     496

enum
{
	NEXT_CDROM_HANDLED = 1,
	NEXT_CDROM_REJECT  = 2,
};

int      next_cdrom_mount(int index, const char *name);
void     next_cdrom_unmount(int index);
uint64_t next_cdrom_size(int index);
int      next_cdrom_active(int index);
void     next_cdrom_fill(int index, uint64_t lba, uint8_t *buf, int sz);

const next_cd_toc *next_cdrom_toc(void);
void     next_cdrom_pos(next_cd_pos *pos);
const uint8_t *next_cdrom_ports(void);
void     next_cdrom_frame(uint8_t *buf, int sz);
void     next_cdrom_command(uint32_t lba, const uint8_t *buf, int sz);

#endif
