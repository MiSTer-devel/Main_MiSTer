#ifndef ROMUTILS_H_
#define ROMUTILS_H_

int arcade_send_rom(const char *xml);
int arcade_load(const char *xml);
int arcade_check_error(void);

#endif
