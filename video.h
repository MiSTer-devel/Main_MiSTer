#ifndef VIDEO_H
#define VIDEO_H

int   video_get_scaler_flt();
void  video_set_scaler_flt(int n);
char* video_get_scaler_coeff();
void  video_set_scaler_coeff(char *name);

int   video_get_gamma_en();
void  video_set_gamma_en(int n);
char* video_get_gamma_curve();
void  video_set_gamma_curve(char *name);

void  video_mode_load();
void  video_mode_adjust();

int   hasAPI1_5();

void video_fb_enable(int enable, int n = 0);
int video_fb_state();
void video_menu_bg(int n, int idle = 0);
int video_bg_has_picture();
int video_chvt(int num);
void video_cmd(char *cmd);

#endif // VIDEO_H
