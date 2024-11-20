#ifndef X86_CDROM_H
#define X86_CDROM_H

int cdrom_handle_cmd(ide_config *ide);
void cdrom_handle_pkt(ide_config *ide);
void cdrom_reply(ide_config *ide, uint8_t error, uint8_t asc_code = 0, uint8_t ascq_code = 0, bool unit_attention = true);
void cdrom_read(ide_config *ide);
void cdrom_mode_select(ide_config *ide);
void ide_cdda_send_sector();

const char* cdrom_parse(uint32_t num, const char *filename);

#endif
