#ifndef VIDEO_H
#define VIDEO_H

#define VFILTER_HORZ 0
#define VFILTER_VERT 1
#define VFILTER_SCAN 2

struct VideoInfo
{
    uint32_t width;
	uint32_t height;
	uint32_t htime;
	uint32_t vtime;
	uint32_t ptime;
	uint32_t vtimeh;

    bool interlaced;
    bool rotated;
};

int   video_get_scaler_flt(int type);
void  video_set_scaler_flt(int type, int n);
char* video_get_scaler_coeff(int type, int only_name = 1);
void  video_set_scaler_coeff(int type, const char *name);

int   video_get_gamma_en();
void  video_set_gamma_en(int n);
char* video_get_gamma_curve(int only_name = 1);
void  video_set_gamma_curve(const char *name);

int   video_get_shadow_mask_mode();
void  video_set_shadow_mask_mode(int n);
char* video_get_shadow_mask(int only_name = 1);
void  video_set_shadow_mask(const char *name);
void  video_loadPreset(char *name);

void  video_mode_load();
void  video_mode_adjust();

int   hasAPI1_5();

void video_fb_enable(int enable, int n = 0);
int video_fb_state();
void video_menu_bg(int n, int idle = 0);
int video_bg_has_picture();
int video_chvt(int num);
void video_cmd(char *cmd);

bool video_is_rotated();
void video_core_description(char *str, size_t len);
void video_scaler_description(char *str, size_t len);
char* video_get_core_mode_name(int with_vrefresh = 1);


#endif // VIDEO_H
