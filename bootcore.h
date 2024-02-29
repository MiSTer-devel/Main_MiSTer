// bootcore.h
// 2024, Aitor Gomez Garcia (info@aitorgomez.net)
// Thanks to Sorgelig and BBond007 for their help and advice in the development of this feature.

#ifndef __BOOTCORE_H__
#define __BOOTCORE_H__

char *getcoreName(char *path);
char *getcoreExactName(char *path);
char *replaceStr(const char *str, const char *oldstr, const char *newstr);
char *loadLastcore();
char *findCore(const char *name, char *coreName, int indent);
void bootcore_init(const char *path);

extern char bootcoretype[64];
extern int16_t btimeout;

#endif // __BOOTCORE_H__
