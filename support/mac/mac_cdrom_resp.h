#ifndef MAC_CDROM_RESP_H
#define MAC_CDROM_RESP_H

#include <stdint.h>

#define MAC_CD_MAX_TRACKS 99

struct mac_cd_toc
{
	int      n;
	uint8_t  ctrl[MAC_CD_MAX_TRACKS];
	uint32_t start[MAC_CD_MAX_TRACKS];
	uint32_t pregap[MAC_CD_MAX_TRACKS];
	uint32_t leadout;
	int      data_trk;
};

struct mac_cd_pos
{
	uint8_t ast;
	uint8_t ctrl;
	uint8_t trk;
	uint8_t abs_m, abs_s, abs_f;
	uint8_t rel_m, rel_s, rel_f;
};

uint8_t mac_cd_bin2bcd(uint8_t v);
uint8_t mac_cd_bcd2bin(uint8_t b);
void    mac_cd_lba2msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f);

int mac_cd_resp_inquiry(uint8_t *out);
int mac_cd_resp_mode_sense(uint8_t page, uint32_t last_lba,
                           const uint8_t ports[4], uint8_t *out);
int mac_cd_resp_toc_c1(const mac_cd_toc *t, uint8_t cdb9, uint8_t cdb5, uint8_t *out);
int mac_cd_resp_toc_43(const mac_cd_toc *t, uint8_t cdb9, uint8_t cdb6, uint8_t *out);
int mac_cd_resp_subch(const mac_cd_pos *p, uint8_t cdb3, uint8_t cdb6, uint8_t *out);
int mac_cd_resp_subq(const mac_cd_pos *p, uint8_t *out);
int mac_cd_resp_astat(const mac_cd_pos *p, uint8_t cdb3, uint8_t *out);

void mac_cd_table_c1(const mac_cd_toc *t, uint8_t *tab404);
int  mac_cd_table_43(const mac_cd_toc *t, uint8_t *tab512);
int  mac_cd_table_2(const mac_cd_toc *t, uint8_t *tab512);

int mac_cd_has_data(const mac_cd_toc *t);

void mac_cd_build_blob(const mac_cd_toc *t, uint8_t *out1024);

#endif
