#ifndef NEXT_CDROM_PLAY_H
#define NEXT_CDROM_PLAY_H

#include <stdint.h>
#include "next_cdrom_resp.h"

enum { NEXT_CD_ST_IDLE = 0, NEXT_CD_ST_PLAY = 1, NEXT_CD_ST_PAUSE = 2, NEXT_CD_ST_END = 3 };

struct next_cd_play
{
	const next_cd_toc *toc;
	uint8_t  state;
	uint32_t cur;
	uint32_t stop;
	uint32_t fetch;
	uint8_t  ports[4];
	uint32_t flush_gen;
};

void     next_cd_play_init(next_cd_play *p);
void     next_cd_play_set_toc(next_cd_play *p, const next_cd_toc *t);
void     next_cd_play_stop(next_cd_play *p);
void     next_cd_play_command(next_cd_play *p, const uint8_t *cdb, const uint8_t *list, int list_len);
int      next_cd_play_frame(next_cd_play *p, uint32_t *lba);
uint8_t  next_cd_play_ast(const next_cd_play *p);
void     next_cd_play_pos(const next_cd_play *p, next_cd_pos *out);
uint16_t next_cd_vol_gain(uint8_t v);
void     next_cd_play_scale(const next_cd_play *p, int16_t *pcm, int stereo_samples);

#endif
