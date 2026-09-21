#ifndef MAC_CDROM_PLAY_H
#define MAC_CDROM_PLAY_H

#include <stdint.h>
#include "mac_cdrom_resp.h"

enum { MAC_CD_ST_IDLE = 0, MAC_CD_ST_PLAY = 1, MAC_CD_ST_PAUSE = 2, MAC_CD_ST_END = 3 };

struct mac_cd_play
{
	const mac_cd_toc *toc;
	uint8_t  state;
	uint32_t cur;
	uint32_t stop;
	uint32_t fetch;
	uint8_t  scan;
	uint8_t  scan_dir;
	uint8_t  ports[4];
	uint32_t flush_gen;
};

void     mac_cd_play_init(mac_cd_play *p);
void     mac_cd_play_set_toc(mac_cd_play *p, const mac_cd_toc *t);
void     mac_cd_play_stop(mac_cd_play *p);
void     mac_cd_play_command(mac_cd_play *p, const uint8_t *cdb, const uint8_t *list, int list_len);
int      mac_cd_play_frame(mac_cd_play *p, uint32_t *lba);
uint8_t  mac_cd_play_ast(const mac_cd_play *p);
void     mac_cd_play_pos(const mac_cd_play *p, mac_cd_pos *out);
uint16_t mac_cd_vol_gain(uint8_t v);
void     mac_cd_play_scale(const mac_cd_play *p, int16_t *pcm, int stereo_samples);

#endif
