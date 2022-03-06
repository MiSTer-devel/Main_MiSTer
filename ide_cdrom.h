#ifndef X86_CDROM_H
#define X86_CDROM_H

int cdrom_handle_cmd(ide_config *ide);
void cdrom_handle_pkt(ide_config *ide);
void cdrom_reply(ide_config *ide, uint8_t error);
void cdrom_read(ide_config *ide);

const char* cdrom_parse(uint32_t num, const char *filename);

#endif
