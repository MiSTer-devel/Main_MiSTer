#ifndef RECENT_H
#define RECENT_H

int  recent_init(int idx);
void recent_scan(int mode);
void recent_scroll_name();
void recent_print();
int  recent_select(char *dir, char *path, char *label);
void recent_update(char* dir, char* path, char* label, int idx);
void recent_clear(int idx);

#endif
