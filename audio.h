#ifndef AUDIO_H
#define AUDIO_H

void set_volume(int cmd);
int  get_volume();
int  get_core_volume();
void set_core_volume(int cmd);
void send_volume();
void save_volume();
void load_volume();

int audio_filter_en();
char* audio_get_filter(int only_name);
void audio_set_filter(const char *name);
void audio_set_filter_en(int n);

#endif
