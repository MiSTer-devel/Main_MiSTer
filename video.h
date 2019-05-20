#ifndef VIDEO_H
#define VIDEO_H

int   video_get_scaler_flt();
void  video_set_scaler_flt(int n);
char* video_get_scaler_coeff();
void  video_set_scaler_coeff(char *name);

void  video_mode_load();
void  video_mode_adjust();

int   hasAPI1_5();

#endif // VIDEO_H
