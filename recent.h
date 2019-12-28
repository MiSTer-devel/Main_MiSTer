#ifndef RECENT_H
#define RECENT_H

void recent_init();
void recent_scan(int mode);
void recent_scroll_name();
void recent_print();
int recent_available();
void recent_load();
void recent_save();
int recent_select(char* dir, char* path);
void recent_update(char* dir, char* path);
char* recent_create_config_name();
const char* recent_path(char* dir, char* path);

#endif