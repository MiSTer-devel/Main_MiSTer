/*****************************************************************************/
// Handle mapping of various joystick controllers
/*****************************************************************************/

#ifndef JOYMAPPING_H
#define JOYMAPPING_H

#include <inttypes.h>
#include <sys/poll.h>
#include "input.h"

void map_joystick(uint32_t *map, uint32_t *mmap, const uint32_t *right_directions);
void map_joystick_show(uint32_t *map, uint32_t *mmap, int num);
int map_paddle_btn();

#define RIGHT_DIRECTIONS_MAGIC 0x31584452u
typedef struct
{
	uint32_t map[NUMBUTTONS];
	uint32_t magic;
	uint32_t directions[4];
} system_map_file;
static_assert(sizeof(system_map_file) == NUMBUTTONS * sizeof(uint32_t) + 20, "system map trailer layout changed");

#define NUMDEV 30
#define JOYMAP_DIR  "inputs/"
#define test_bit(bit, array)  (array [bit / 8] & (1 << (bit % 8)))

enum QUIRK
{
	QUIRK_NONE = 0,
	QUIRK_WIIMOTE,
	QUIRK_DS3,
	QUIRK_DS4,
	QUIRK_DS4TOUCH,
	QUIRK_MADCATZ360,
	QUIRK_PDSP,
	QUIRK_PDSP_ARCADE,
	QUIRK_JAMMA,
	QUIRK_JAMMA2,
	QUIRK_MSSP,
	QUIRK_TOUCHGUN,
	QUIRK_VCS,
	QUIRK_JOYCON,
	QUIRK_LIGHTGUN_CRT,
	QUIRK_LIGHTGUN,
	QUIRK_LIGHTGUN_MOUSE,
	QUIRK_WHEEL,
	QUIRK_SHIFT,
};

// core-driven force feedback effect slots
enum
{
	FFB_CONST = 0,
	FFB_SPRING,
	FFB_DAMPER,
	FFB_FRICTION,
	FFB_VIB,
	FFB_NUM
};

typedef struct
{
	uint16_t bustype, vid, pid, version, gcdb_version;
	char     idstr[256];
	char     mod;

	uint8_t  led;
	uint8_t  mouse;
	uint8_t  axis_edge[256];
	int8_t   axis_pos[256];

	uint8_t  num;
	uint8_t  has_map;
	uint32_t map[NUMBUTTONS];
	int      map_shown;

	uint8_t  osd_combo;
	uint32_t osd_hold_timer;
	uint32_t osd_hold_release_at;

	uint8_t  has_mmap;
	uint8_t  gcdb_mmap;
	uint32_t mmap[NUMBUTTONS];
	uint32_t right_directions[4];
	int      stick_l[2];
	int      stick_r[2];

	uint8_t  force_joy;
	uint8_t  has_kbdmap;
	uint8_t  kbdmap[256];

	int32_t  guncal[4];

	int      accx, accy;
	int      startx, starty;
	int      lastx, lasty;
	int      quirk;

	int      misc_flags;
	int      paddle_val;
	int      spinner_prev;
	int      spinner_acc;
	int      spinner_prediv;
	int      spinner_dir;
	int      spinner_accept;
	int      old_btn;
	int      ds_mouse_emu;

	int      lightgun_req;
	int      lightgun;

	int      has_rumble;
	int      rumble_en;
	uint16_t last_rumble;
	ff_effect rumble_effect;

	int      has_ffb;
	uint8_t  ffb_caps;
	uint8_t  ffb_loaded;
	uint8_t  ffb_playing;
	uint8_t  ffb_on;
	uint8_t  ffb_flags;
	int32_t  ffb_autoc;
	int32_t  ffb_val[FFB_NUM];
	ff_effect ffb_effect[FFB_NUM];
	int8_t   wh_centre;
	int16_t  wh_spring_id;

	int8_t   wh_steer;
	int8_t   wh_accel;
	int8_t   wh_brake;
	int8_t   wh_clutch;
	int8_t   wh_combo;
	int8_t   wh_pedal_invert;

	int      timeout;
	char     mac[64];

	int      bind;
	uint32_t unique_hash;
	char     devname[32];
	char     id[80];
	char     name[128];
	char     sysfs[512];

	int      ss_range[2];
	int 	 max_cardinal[2];
	float    max_range[2];

	uint32_t deadzone;
	advancedButtonMap advanced_map[ADVANCED_MAP_MAX];
	advancedButtonState advanced_state[ADVANCED_MAP_MAX];
	bool has_advanced_map;
	uint16_t advanced_last_pressed_keycode;
} devInput;

extern devInput input[NUMDEV];
extern struct pollfd pool[NUMDEV + 3];

uint8_t normalize_analog_trigger(uint32_t map, int value, const input_absinfo *absinfo);

extern int menu_mouse_map;
extern uint32_t osd_timer;
extern int mapping;
extern int mapping_osd_owned;
extern int mapping_node;
extern int mapping_button;
extern int mapping_dev;
extern int mapping_type;
extern int mapping_count;
extern int mapping_set;
extern int mapping_key_mapped;

static inline uint16_t map_axis_code(uint32_t map)
{
	return (uint16_t)(map & MAP_AXIS_MASK);
}

static inline int map_axis_offset(uint32_t map, int offset)
{
	if (!(map & MAP_FLAG_INVERT)) return offset;

	offset = -offset;
	return (offset > 127) ? 127 : offset;
}

static inline int menu_axis_direction(uint32_t map, uint16_t key)
{
	if (!map) return -1;
	const uint16_t key_min = KEY_EMU + (map_axis_code(map) << 1);
	if (key != key_min && key != key_min + 1) return -1;
	return (key == key_min ? 1 : 0) ^ ((map & MAP_FLAG_INVERT) ? 1 : 0);
}

int load_map(const char *name, void *pBuffer, int size);
template<size_t N> void build_map_name(char (&name)[N], int dev, int def, int version);
char *get_unique_mapping(int dev, int force_unique = 0);
char *get_map_name(int dev, int def);
char *get_jkmap_name(int dev);
char *get_kbdmap_name(int dev);
void default_v3_stick(int dev, int fd, uint32_t *map);
int map_entry_uses_axis(uint32_t map, uint16_t axis);
void clear_mapping_trigger_capture();
void prepare_mapping_trigger_capture();
void record_menu_tmp_axis_stage();
void clear_mapping_current_input();
void clear_mapping_osd_hold_capture();
void invalidate_map_setting();
void advance_map_button();
int map_hold_action_for_key(int dev, uint16_t code);
int map_osd_hold_poll();
int map_confirm_event(int dev, const struct input_event *ev, bool menu_event);
void map_advanced_key(struct input_event *ev, int dev, int map_skip);
void map_input_event(struct input_event *ev, struct input_absinfo *absinfo, int dev, int map_skip, int osd_event);

#endif // JOYMAPPING_H
