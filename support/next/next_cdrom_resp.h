#ifndef NEXT_CDROM_RESP_H
#define NEXT_CDROM_RESP_H

#include <stdint.h>

#define NEXT_CD_MAX_TRACKS 99

struct next_cd_toc
{
	int      n;
	uint8_t  ctrl[NEXT_CD_MAX_TRACKS];
	uint32_t start[NEXT_CD_MAX_TRACKS];
	uint32_t pregap[NEXT_CD_MAX_TRACKS];
	uint32_t leadout;
	int      data_trk;
};

struct next_cd_pos
{
	uint8_t  ast;
	uint8_t  ctrl;
	uint8_t  trk;
	uint32_t abs_lba;
	uint32_t rel_lba;
};

void next_cd_lba2msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f);
void next_cd_put_addr(uint8_t *out, uint32_t lba, int msf);

int next_cd_resp_toc(const next_cd_toc *t, int msf, unsigned fmt, uint8_t track, uint8_t *out);
int next_cd_resp_subch(const next_cd_pos *p, int msf, int subq, uint8_t fmt, uint8_t *out);
int next_cd_has_data(const next_cd_toc *t);

#endif
