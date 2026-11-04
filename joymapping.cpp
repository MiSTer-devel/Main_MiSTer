/*
This file contains lookup information on known controllers
*/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include "joymapping.h"
#include "menu.h"
#include "input.h"
#include "user_io.h"
#include "cfg.h"
#include "hardware.h"
#include "str_util.h"
#include "file_io.h"
#include "osd.h"
#include "gamecontroller_db.h"

#define DPAD_COUNT 4
#define CORE_BUTTON_COUNT (32 - DPAD_COUNT)

/*****************************************************************************/
static void trim(char * s)
{
	char *p = s;
	int l = strlen(p);
	if (!l) return;

	while (p[l - 1] == ' ') p[--l] = 0;
	while (*p && (*p == ' ')) ++p, --l;

	memmove(s, p, l + 1);
}

static char joy_names[NUMBUTTONS][32];
static int joy_count = 0;

static char joy_nnames[NUMBUTTONS][32];
static char joy_pnames[NUMBUTTONS][32];
static int joy_nnames_given = 0, joy_pnames_given = 0;
static int defaults = 0;

static int list_entries(const char *p)
{
	int count = 1;
	while (*p) count += *p++ == ',';
	return count;
}

static void read_buttons()
{
	char *p;

	memset(joy_names, 0, sizeof(joy_names));
	memset(joy_nnames, 0, sizeof(joy_nnames));
	memset(joy_pnames, 0, sizeof(joy_pnames));
	joy_count = 0;
	joy_nnames_given = joy_pnames_given = 0;
	defaults = 0;

	user_io_read_confstr();

	// this option used as default name map (unless jn/jp is supplied)
	p = get_buttons(0);
	if (p)
	{
		for (int n = 0; n < CORE_BUTTON_COUNT; n++)
		{
			substrcpy(joy_names[n], p, n);
			if (!joy_names[n][0]) break;

			printf("joy_name[%d] = %s\n", n, joy_names[n]);

			memcpy(joy_nnames[n], joy_names[n], sizeof(joy_nnames[0]));
			char *sstr = strchr(joy_nnames[n], '(');
			if (sstr) *sstr = 0;
			trim(joy_nnames[n]);

			if (!joy_nnames[n][0]) break;
			joy_count++;
		}
		printf("\n");
	}

	// - supports empty name to skip the button from default map
	// - only base button names must be used (ABXYLR Start Select)

	// name default map
	p = get_buttons(1);
	if (p)
	{
		memset(joy_nnames, 0, sizeof(joy_nnames));
		joy_nnames_given = list_entries(p);
		for (int n = 0; n < joy_count; n++)
		{
			substrcpy(joy_nnames[n], p, n);
			trim(joy_nnames[n]);
			if (joy_nnames[n][0]) printf("joy_nname[%d] = %s\n", n, joy_nnames[n]);
		}
		printf("\n");
	}

	// positional default map
	p = get_buttons(2);
	if (p)
	{
		defaults = cfg.gamepad_defaults;
		joy_pnames_given = list_entries(p);
		for (int n = 0; n < joy_count; n++)
		{
			substrcpy(joy_pnames[n], p, n);
			trim(joy_pnames[n]);
			if (joy_pnames[n][0]) printf("joy_pname[%d] = %s\n", n, joy_pnames[n]);
		}
		printf("\n");
	}
}

static int is_fire(char* name)
{
	if (!strncasecmp(name, "fire", 4) || !strncasecmp(name, "button", 6))
	{
		if (!strcasecmp(name, "fire") || strchr(name, '1')) return 1;
		if (strchr(name, '2')) return 2;
		if (strchr(name, '3')) return 3;
		if (strchr(name, '4')) return 4;
	}

	return 0;
}

static int is_extended_button(char *name)
{
	return !strcasecmp(name, "L2")
		|| !strcasecmp(name, "R2")
		|| !strcasecmp(name, "L3")
		|| !strcasecmp(name, "R3")
		|| !strcasecmp(name, "Misc1")
		|| !strcasecmp(name, "RightX-") || !strcasecmp(name, "RightX+")
		|| !strcasecmp(name, "RightY-") || !strcasecmp(name, "RightY+");
}

static void copy_core_button_name(char *dst, size_t size, int index)
{
	snprintf(dst, size, "%.*s", (int)sizeof(joy_names[0]) - 1, joy_names[index]);
	char *p = strchr(dst, '(');
	if (p) *p = 0;
	trim(dst);
}

void map_joystick(uint32_t *map, uint32_t *mmap, const uint32_t *right_directions, uint32_t misc1)
{
	/*
	attemps to centrally defined core joy mapping to the joystick declaredy by a core config string
	we use the names declared by core with some special handling for specific edge cases

	Input button order is "virtual SNES" i.e.:
		A, B, X, Y, L, R, Select, Start
	*/
	read_buttons();

	map[SYS_BTN_RIGHT] = mmap[SYS_BTN_RIGHT] & 0xFFFF;
	map[SYS_BTN_LEFT]  = mmap[SYS_BTN_LEFT]  & 0xFFFF;
	map[SYS_BTN_DOWN]  = mmap[SYS_BTN_DOWN]  & 0xFFFF;
	map[SYS_BTN_UP]    = mmap[SYS_BTN_UP]    & 0xFFFF;

	if (mmap[SYS_AXIS_X] && !is_psx())
	{
		const uint32_t min_key = KEY_EMU + ((mmap[SYS_AXIS_X] & MAP_AXIS_MASK) << 1);
		const uint32_t max_key = min_key + 1;
		const uint32_t left_key = (mmap[SYS_AXIS_X] & MAP_FLAG_INVERT) ? max_key : min_key;
		const uint32_t right_key = (mmap[SYS_AXIS_X] & MAP_FLAG_INVERT) ? min_key : max_key;

		map[SYS_BTN_LEFT] = (left_key << 16) | map[SYS_BTN_LEFT];
		map[SYS_BTN_RIGHT] = (right_key << 16) | map[SYS_BTN_RIGHT];
	}

	if (mmap[SYS_AXIS_Y] && !is_psx())
	{
		const uint32_t min_key = KEY_EMU + ((mmap[SYS_AXIS_Y] & MAP_AXIS_MASK) << 1);
		const uint32_t max_key = min_key + 1;
		const uint32_t up_key = (mmap[SYS_AXIS_Y] & MAP_FLAG_INVERT) ? max_key : min_key;
		const uint32_t down_key = (mmap[SYS_AXIS_Y] & MAP_FLAG_INVERT) ? min_key : max_key;

		map[SYS_BTN_UP] = (up_key << 16) | map[SYS_BTN_UP];
		map[SYS_BTN_DOWN] = (down_key << 16) | map[SYS_BTN_DOWN];
	}

	// loop through core requested buttons and construct result map
	for (int i=0, n=0; i<joy_count; i++)
	{
		if (!strcmp(joy_names[i], "-")) continue;

		int idx = i+DPAD_COUNT;
		char btn_name[32];
		strcpy(btn_name, defaults ? joy_pnames[n] : joy_nnames[n]);
		if (!btn_name[0] && n >= (defaults ? joy_pnames_given : joy_nnames_given))
		{
			copy_core_button_name(btn_name, sizeof(btn_name), i);
			if (!is_extended_button(btn_name)) btn_name[0] = 0;
		}

		char *p = strchr(btn_name, '|');
		if (p) *p = 0;

		if(!strcasecmp(btn_name, "A")
		|| !strcasecmp(btn_name, "Jump")
		|| is_fire(btn_name) == 1)
		{
			map[idx] = mmap[SYS_BTN_A];
		}

		else if(!strcasecmp(btn_name, "B")
		|| is_fire(btn_name) == 2)
		{
			map[idx] = mmap[SYS_BTN_B];
		}

		else if(!strcasecmp(btn_name, "X")
		|| !strcasecmp(btn_name, "C")
		|| is_fire(btn_name) == 3)
		{
			map[idx] = mmap[SYS_BTN_X];
		}

		else if(!strcasecmp(btn_name, "Y")
		|| !strcasecmp(btn_name, "D")
		|| is_fire(btn_name) == 4)
		{
			map[idx] = mmap[SYS_BTN_Y];
		}

		// Genesis C and Z  and TG16 V and VI
		else if(!strcasecmp(btn_name, "R")
		|| !strcasecmp(btn_name, "RT")
		|| !strcasecmp(btn_name, "Coin"))
		{
			map[idx] = mmap[SYS_BTN_R];
		}

		else if(!strcasecmp(btn_name, "L")
		|| !strcasecmp(btn_name, "LT"))
		{
			map[idx] = mmap[SYS_BTN_L];
		}

		else if(!strcasecmp(btn_name, "L2"))
		{
			map[idx] = mmap[SYS_BTN_L2];
		}

		else if(!strcasecmp(btn_name, "R2"))
		{
			map[idx] = mmap[SYS_BTN_R2];
		}

		else if(!strcasecmp(btn_name, "L3"))
		{
			map[idx] = mmap[SYS_BTN_L3];
		}

		else if(!strcasecmp(btn_name, "R3"))
		{
			map[idx] = mmap[SYS_BTN_R3];
		}

		else if(!strcasecmp(btn_name, "Misc1"))
		{
			map[idx] = misc1;
		}

		else if (!strcasecmp(btn_name, "RightX-") || !strcasecmp(btn_name, "RightX+") ||
		         !strcasecmp(btn_name, "RightY-") || !strcasecmp(btn_name, "RightY+"))
		{
			map[idx] = 0;
			const int direction = (btn_name[5] == 'Y' || btn_name[5] == 'y' ? 2 : 0) + (btn_name[6] == '+');
			const int component = direction / 2;
			const uint32_t primary = mmap[SYS_AXIS_X + component];
			const uint32_t stick2 = mmap[SYS_AXIS2_X + component];
			const uint32_t axis = (primary & MAP_FLAG_ANALOG) && primary == stick2
				? mmap[SYS_AXIS1_X + component] : stick2;
			if (right_directions && right_directions[direction]) map[idx] = right_directions[direction];
			else if (axis != UINT32_MAX && (axis & MAP_FLAG_ANALOG))
				map[idx] = KEY_EMU + ((axis & MAP_AXIS_MASK) << 1) + ((direction & 1) ^ !!(axis & MAP_FLAG_INVERT));
		}

		else if(!strcasecmp(btn_name, "Select")
		|| !strcasecmp(btn_name, "Mode")
		|| !strcasecmp(btn_name, "Game Select")
		|| !strcasecmp(btn_name, "Start 2P"))
		{
			map[idx] = mmap[SYS_BTN_SELECT];
		}

		else if(!strcasecmp(btn_name, "Start")
		|| !strcasecmp(btn_name, "Run")
		|| !strcasecmp(btn_name, "Pause")
		|| !strcasecmp(btn_name, "Start 1P"))
		{
			map[idx] = mmap[SYS_BTN_START];
		}

		n++;
	}
}

int map_paddle_btn()
{
	read_buttons();
	for (int i = 0, n = 0; i < joy_count; i++)
	{
		if (!strcmp(joy_names[i], "-")) continue;
		char *p = strchr(defaults ? joy_pnames[n] : joy_nnames[n], '|');
		if (p && !strcasecmp(p, "|P")) return i + SYS_BTN_A;
		n++;
	}
	return SYS_BTN_A;
}

static const char* get_std_name(uint16_t code, uint32_t *mmap)
{
	if (code)
	{
		if (code == mmap[SYS_BTN_A]) return "[A]";
		if (code == mmap[SYS_BTN_B]) return "[B]";
		if (code == mmap[SYS_BTN_X]) return "[X]";
		if (code == mmap[SYS_BTN_Y]) return "[Y]";
		if (code == mmap[SYS_BTN_L]) return "[L]";
		if (code == mmap[SYS_BTN_R]) return "[R]";
		if (code == mmap[SYS_BTN_L2]) return "[L2]";
		if (code == mmap[SYS_BTN_R2]) return "[R2]";
		if (code == mmap[SYS_BTN_L3]) return "[L3]";
		if (code == mmap[SYS_BTN_R3]) return "[R3]";
		if (code == mmap[SYS_BTN_SELECT]) return "[\x96]";
		if (code == mmap[SYS_BTN_START]) return "[\x16]";
		return "[ ]";
	}
	return NULL;
}

void map_joystick_show(uint32_t *map, uint32_t *mmap, int num)
{
	static char mapinfo[1024];
	read_buttons();

	sprintf(mapinfo, "Map (P%d):", num);
	if (!num) sprintf(mapinfo, " Map:");
	char *list = mapinfo + strlen(mapinfo);

	// loop through core requested buttons and construct result map
	for (int i = 0; i < joy_count; i++)
	{
		if (!strcmp(joy_names[i], "-")) continue;

		const char *btn = get_std_name((uint16_t)(map[i + DPAD_COUNT]), mmap);
		if (btn)
		{
			strcat(mapinfo, "\n");
			strcat(mapinfo, btn);
			strcat(mapinfo, ": ");
			strcat(mapinfo, joy_names[i]);
		}
	}

	if(strlen(list) && cfg.controller_info) Info(mapinfo, cfg.controller_info * 1000);
}

#define SYS_MAP_GAMEPAD_COUNT   (SYS_MAP_POS_OSD + 3)
#define TRIGGER_CAPTURE_AXIS_COUNT (ABS_MAX + 1)
int menu_mouse_map = 0;

typedef struct
{
	uint8_t valid;
	int dev;
	int pos;
	int moved_score;
	uint8_t has_axis[TRIGGER_CAPTURE_AXIS_COUNT];
	input_absinfo absinfo[TRIGGER_CAPTURE_AXIS_COUNT];
} triggerCaptureState;

static void clear_mouse_map_slots(uint32_t *map)
{
	memset(&map[SYS_MS_RIGHT], 0, sizeof(map[0]) * (SYS_MS_BTN_EMU - SYS_MS_RIGHT + 1));
	memset(&map[SYS_AXIS_MX], 0, sizeof(map[0]) * ((SYS_AXIS_MY - SYS_AXIS_MX) + 1));
}

static void clear_joypad_map_slots(uint32_t *map)
{
	uint32_t mouse_buttons[SYS_MS_BTN_EMU - SYS_MS_RIGHT + 1];
	uint32_t mouse_axes[(SYS_AXIS_MY - SYS_AXIS_MX) + 1];

	memcpy(mouse_buttons, &map[SYS_MS_RIGHT], sizeof(mouse_buttons));
	memcpy(mouse_axes, &map[SYS_AXIS_MX], sizeof(mouse_axes));
	memset(map, 0, sizeof(map[0]) * NUMBUTTONS);
	memcpy(&map[SYS_MS_RIGHT], mouse_buttons, sizeof(mouse_buttons));
	memcpy(&map[SYS_AXIS_MX], mouse_axes, sizeof(mouse_axes));
}

static void clear_mapping_slot_for_pos(uint32_t *map, int pos);

static int get_system_map_button_idx(int pos)
{
	if (menu_mouse_map)
	{
		if (pos >= SYS_BTN_A && pos < (SYS_BTN_A + 8)) return SYS_MS_RIGHT + (pos - SYS_BTN_A);
		return -1;
	}

	if (pos >= SYS_BTN_RIGHT && pos <= SYS_BTN_R) return pos;
	if (pos >= SYS_MAP_BTN_L2 && pos < SYS_MAP_BTN_SELECT) return SYS_BTN_L2 + (pos - SYS_MAP_BTN_L2);
	if (pos >= SYS_MAP_BTN_SELECT && pos < SYS_MAP_POS_OSD) return SYS_BTN_SELECT + (pos - SYS_MAP_BTN_SELECT);
	return -1;
}

template<size_t N>
static void get_joymap_path(char (&path)[N], const char *name)
{
	sprintfz(path, JOYMAP_DIR "%s", name);
}

int load_map(const char *name, void *pBuffer, int size)
{
	char path[256];
	get_joymap_path(path, name);
	const int ret = FileLoadConfig(path, pBuffer, size);
	return ret ? ret : FileLoadConfig(name, pBuffer, size);
}

static void delete_map(const char *name)
{
	char path[256];
	get_joymap_path(path, name);
	FileDeleteConfig(name);
	FileDeleteConfig(path);
}

static int save_map(const char *name, void *pBuffer, int size)
{
	char path[256];
	get_joymap_path(path, name);
	FileDeleteConfig(name);
	return FileSaveConfig(path, pBuffer, size);
}

int mapping = 0;
static int mapping_paused = 0;
static int mapping_confirm = 0;
static int mapping_confirm_result = 0;
static int mapping_invalidated = 0;
int mapping_osd_owned = 0;
int mapping_node = 0;
int mapping_button;
int mapping_dev = -1;
int mapping_type;
int mapping_count;
static int mapping_clear;
static int mapping_finish;
int mapping_set;
static int mapping_menu_only;
static int mapping_existing_asked;
static uint32_t mapping_mmap[NUMBUTTONS];

enum
{
	MAP_HOLD_NONE = 0,
	MAP_HOLD_SKIP,
	MAP_HOLD_BACK,
	MAP_HOLD_CANCEL = 4,
	MAP_HOLD_FINISH = 8
};

#define MAP_HOLD_ACTION_MS 1000
#define MAP_HOLD_FEEDBACK_DELAY_MS 333
static int mapping_current_key = 0;
static int mapping_current_dev = -1;
static int mapping_hold_key = 0;
static int mapping_ignore_release_key = 0;
static int mapping_ignore_release_key2 = 0;
static uint32_t mapping_hold_timer = 0;
static uint32_t mapping_hold_feedback_timer = 0;
static int mapping_hold_action = MAP_HOLD_NONE;
static int mapping_pending_hold_action = MAP_HOLD_NONE;
static int mapping_combo_second_key = 0;
static uint8_t mapping_combo_release_mask = 0;
static uint32_t mapping_osd_hold_timer = 0;
static uint8_t mapping_osd_hold_latched = 0;
static advancedButtonMap *mapping_store = NULL;

static uint32_t tmp_axis[4];
static int tmp_axis_n = 0;

static uint8_t tmp_axis_stage_count[6] = {};
static uint8_t tmp_axis_stage_seen = 0;
static uint16_t mapping_last_axis = 0;
int mapping_key_mapped = 0;
static triggerCaptureState mapping_trigger_capture = {};

static void claim_mapping_device(int dev)
{
	if (mapping_dev >= 0) return;

	mapping_dev = dev;
	if (!is_menu())
	{
		if (!mapping_set) memset(input[dev].map, 0, sizeof(input[dev].map));
		return;
	}

	if (menu_mouse_map)
	{
		memcpy(input[dev].map, input[dev].mmap, sizeof(input[dev].map));
		clear_mouse_map_slots(input[dev].map);
	}
	else
	{
		memcpy(mapping_mmap, input[dev].mmap, sizeof(mapping_mmap));
		clear_joypad_map_slots(input[dev].map);
	}
}

static int is_menu_trigger_map_pos(int pos)
{
	return !menu_mouse_map && (pos == SYS_MAP_BTN_L2 || pos == (SYS_MAP_BTN_L2 + 1));
}

void clear_mapping_trigger_capture()
{
	memset(&mapping_trigger_capture, 0, sizeof(mapping_trigger_capture));
}

static int mapping_node_fd()
{
	return pool[(mapping_node == mapping_dev || input[mapping_node].bind == mapping_dev) ? mapping_node : mapping_dev].fd;
}

static int mapping_has_key(uint32_t code)
{
	if (code >= KEY_EMU) return 1;

	for (int i = 0; i < NUMDEV; i++)
	{
		unsigned char keybits[(KEY_MAX + 7) / 8] = {};
		if ((i == mapping_dev || input[i].bind == mapping_dev) && pool[i].fd >= 0 &&
			ioctl(pool[i].fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0 && test_bit(code, keybits)) return 1;
	}
	return 0;
}

void prepare_mapping_trigger_capture()
{
	if (!is_menu() || !mapping || !is_menu_trigger_map_pos(mapping_button) || mapping_dev < 0)
	{
		if (mapping_trigger_capture.valid) clear_mapping_trigger_capture();
		return;
	}

	if (mapping_trigger_capture.valid &&
		mapping_trigger_capture.dev == mapping_dev &&
		mapping_trigger_capture.pos == mapping_button)
		return;

	clear_mapping_trigger_capture();
	mapping_trigger_capture.dev = mapping_dev;
	mapping_trigger_capture.pos = mapping_button;

	unsigned char absbits[(ABS_MAX + 7) / 8] = {};
	const int fd = mapping_node_fd();
	if (fd < 0 || ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0)
		return;

	for (int axis = 0; axis <= ABS_MAX; axis++)
	{
		if (!test_bit(axis, absbits))
			continue;

		input_absinfo info = {};
		if (ioctl(fd, EVIOCGABS(axis), &info) < 0)
			continue;

		mapping_trigger_capture.has_axis[axis] = 1;
		mapping_trigger_capture.absinfo[axis] = info;
	}

	mapping_trigger_capture.valid = 1;
}

int map_entry_uses_axis(uint32_t map, uint16_t axis)
{
	if (map & MAP_FLAG_ANALOG)
		return map_axis_code(map) == axis;

	const uint32_t last_axis_key = KEY_EMU + (ABS_MAX << 1) + 1;
	return map >= KEY_EMU && map <= last_axis_key && ((map - KEY_EMU) >> 1) == axis;
}

static int trigger_axis_is_reserved(int dev, uint16_t axis)
{
	if (axis >= ABS_HAT0X && axis <= ABS_HAT3Y) return 1;
	if ((axis == ABS_RUDDER || axis == ABS_WHEEL) && input[dev].quirk != QUIRK_WHEEL) return 1;

	for (int i = 0; i < (int)(sizeof(tmp_axis) / sizeof(tmp_axis[0])); i++)
	{
		if (map_entry_uses_axis(tmp_axis[i], axis)) return 1;
	}

	for (int i = SYS_BTN_RIGHT; i <= SYS_BTN_UP; i++)
	{
		if (map_entry_uses_axis(input[dev].map[i], axis)) return 1;
	}

	for (int i = SYS_AXIS1_X; i <= SYS_AXIS_Y; i++)
	{
		if (map_entry_uses_axis(input[dev].map[i], axis)) return 1;
	}

	for (int i = SYS_AXIS_MX; i <= SYS_AXIS_MY; i++)
	{
		if (map_entry_uses_axis(input[dev].map[i], axis)) return 1;
	}

	return 0;
}

static int trigger_axis_has_valid_baseline(int dev, uint16_t axis)
{
	if (!mapping_trigger_capture.valid ||
		mapping_trigger_capture.dev != dev ||
		mapping_trigger_capture.pos != mapping_button ||
		axis > ABS_MAX ||
		!mapping_trigger_capture.has_axis[axis])
		return 0;

	const input_absinfo *base = &mapping_trigger_capture.absinfo[axis];
	const int64_t range = (int64_t)base->maximum - base->minimum;
	if (range <= 1) return 0;

	const int64_t threshold = (range / 8) > 4 ? (range / 8) : 4;
	const int64_t center = base->minimum + (range / 2);
	return base->value <= base->minimum + threshold ||
		base->value >= base->maximum - threshold ||
		llabs(base->value - center) <= threshold;
}

static uint32_t build_trigger_axis_map_for_axis(int dev, uint16_t axis, const input_absinfo *absinfo, int negative, int threshold_divisor)
{
	if (!absinfo || axis > ABS_MAX ||
		trigger_axis_is_reserved(dev, axis) ||
		!trigger_axis_has_valid_baseline(dev, axis))
		return 0;

	uint32_t map = axis | MAP_FLAG_ANALOG | MAP_FLAG_TRIGGER;

	if (mapping_trigger_capture.valid &&
		mapping_trigger_capture.dev == dev &&
		mapping_trigger_capture.pos == mapping_button &&
		mapping_trigger_capture.has_axis[axis])
	{
		const input_absinfo *base_info = &mapping_trigger_capture.absinfo[axis];
		const int64_t range = (int64_t)base_info->maximum - base_info->minimum;
		const int64_t delta = llabs((int64_t)absinfo->value - base_info->value);

		if (range > 1 && threshold_divisor > 0)
		{
			const int64_t threshold = (range / threshold_divisor) > 4 ? (range / threshold_divisor) : 4;
			if (delta < threshold)
				return 0;
		}

		const int64_t center = base_info->minimum + (range / 2);
		const int64_t centered_threshold = range / 4;
		if (range > 1 && llabs(base_info->value - center) <= centered_threshold)
			map |= MAP_FLAG_CENTERED;

		if (delta)
			negative = absinfo->value < base_info->value;
	}

	if (negative)
		map |= MAP_FLAG_NEGATIVE;

	return map;
}

static uint32_t build_trigger_axis_map(int dev, uint16_t keycode, const input_absinfo *absinfo)
{
	if (keycode < KEY_EMU)
		return 0;

	return build_trigger_axis_map_for_axis(dev, (keycode - KEY_EMU) >> 1, absinfo, !(keycode & 1), 5);
}

static int trigger_axis_moved_score(uint16_t axis, const input_absinfo *absinfo)
{
	if (axis > ABS_MAX || !mapping_trigger_capture.has_axis[axis])
		return 0;

	const input_absinfo *base_info = &mapping_trigger_capture.absinfo[axis];
	const int64_t range = (int64_t)base_info->maximum - base_info->minimum;
	if (range <= 1)
		return 0;

	const int64_t delta = llabs((int64_t)absinfo->value - base_info->value);
	const int64_t threshold = (range / 32) > 4 ? (range / 32) : 4;
	if (delta < threshold)
		return 0;

	return (int)((delta * 1024) / range);
}

static uint32_t further_moved_trigger_axis_map(int dev, uint16_t axis, const input_absinfo *absinfo)
{
	const int score = trigger_axis_moved_score(axis, absinfo);
	if (score <= mapping_trigger_capture.moved_score)
		return 0;

	const uint32_t map = build_trigger_axis_map_for_axis(dev, axis, absinfo, absinfo->value < mapping_trigger_capture.absinfo[axis].value, 32);
	if (map) mapping_trigger_capture.moved_score = score;
	return map;
}

static uint32_t detect_moved_trigger_axis_map(int dev)
{
	if (!mapping_trigger_capture.valid ||
		mapping_trigger_capture.dev != dev ||
		mapping_trigger_capture.pos != mapping_button ||
		mapping_node_fd() < 0)
		return 0;

	uint32_t best_map = 0;
	mapping_trigger_capture.moved_score = 0;

	for (uint16_t axis = 0; axis <= ABS_MAX; axis++)
	{
		if (!mapping_trigger_capture.has_axis[axis])
			continue;

		input_absinfo absinfo = {};
		if (ioctl(mapping_node_fd(), EVIOCGABS(axis), &absinfo) < 0)
			continue;

		const uint32_t map = further_moved_trigger_axis_map(dev, axis, &absinfo);
		if (map) best_map = map;
	}

	return best_map;
}

static uint32_t detect_standard_trigger_axis_map(int dev, uint16_t keycode)
{
	static const uint16_t left_axes[] = { ABS_Z, ABS_BRAKE };
	static const uint16_t right_axes[] = { ABS_RZ, ABS_GAS };
	const uint16_t *axes;
	if (keycode == BTN_TL2) axes = left_axes;
	else if (keycode == BTN_TR2) axes = right_axes;
	else return 0;

	if (!mapping_trigger_capture.valid ||
		mapping_trigger_capture.dev != dev ||
		mapping_trigger_capture.pos != mapping_button ||
		mapping_node_fd() < 0)
		return 0;

	for (int i = 0; i < 2; i++)
	{
		const uint16_t axis = axes[i];
		if (!mapping_trigger_capture.has_axis[axis])
			continue;

		input_absinfo absinfo = {};
		if (ioctl(mapping_node_fd(), EVIOCGABS(axis), &absinfo) < 0)
			continue;

		const input_absinfo *base_info = &mapping_trigger_capture.absinfo[axis];
		const int64_t range = (int64_t)base_info->maximum - base_info->minimum;
		if (range <= 1)
			continue;

		const int64_t edge_threshold = (range / 8) > 4 ? (range / 8) : 4;
		const int released_near_min = base_info->value <= (base_info->minimum + edge_threshold);
		const int released_near_max = base_info->value >= (base_info->maximum - edge_threshold);
		if (!released_near_min && !released_near_max)
			continue;

		const uint32_t map = build_trigger_axis_map_for_axis(dev, axis, &absinfo, released_near_max, 0);
		if (map) return map;
	}

	return 0;
}

static void apply_menu_analog_trigger_map(int dev, uint32_t trigger_map)
{
	if (!trigger_map)
		return;

	const int idx = SYS_AXIS_L2 + (mapping_button - SYS_MAP_BTN_L2);
	input[dev].map[idx] = trigger_map;
	input[dev].mmap[idx] = trigger_map;
}

static void update_menu_analog_trigger_axis(int dev, uint16_t axis, const input_absinfo *absinfo)
{
	if (!is_menu_trigger_map_pos(mapping_button) ||
		mapping_dev != dev ||
		!mapping_key_mapped ||
		mapping_key_mapped >= KEY_EMU)
		return;

	apply_menu_analog_trigger_map(dev, further_moved_trigger_axis_map(dev, axis, absinfo));
}

static void map_menu_analog_trigger(int dev, uint16_t keycode, const input_absinfo *absinfo)
{
	if (!is_menu_trigger_map_pos(mapping_button))
		return;

	uint32_t trigger_map = build_trigger_axis_map(dev, keycode, absinfo);
	if (!trigger_map && keycode < KEY_EMU)
		trigger_map = detect_moved_trigger_axis_map(dev);
	if (!trigger_map && keycode < KEY_EMU)
		trigger_map = detect_standard_trigger_axis_map(dev, keycode);

	apply_menu_analog_trigger_map(dev, trigger_map);
}

static int get_tmp_axis_stage_index(int pos)
{
	if (pos < -6 || pos > -1) return -1;
	return pos + 6;
}

static void sync_menu_tmp_axes()
{
	if (!is_menu() || menu_mouse_map || mapping_menu_only || mapping_dev < 0) return;

	const int primary = (tmp_axis_n >= 4 && (!tmp_axis[0] || tmp_axis_stage_count[get_tmp_axis_stage_index(-4)] >= 2)) ? 2 : 0;
	memcpy(&input[mapping_dev].mmap[SYS_AXIS1_X], tmp_axis, sizeof(tmp_axis));
	memcpy(&input[mapping_dev].map[SYS_AXIS1_X], tmp_axis, sizeof(tmp_axis));
	input[mapping_dev].mmap[SYS_AXIS_X] = (tmp_axis_n >= 2) ? tmp_axis[primary] : 0;
	input[mapping_dev].mmap[SYS_AXIS_Y] = (tmp_axis_n >= 2) ? tmp_axis[primary + 1] : 0;
	input[mapping_dev].map[SYS_AXIS_X] = input[mapping_dev].mmap[SYS_AXIS_X];
	input[mapping_dev].map[SYS_AXIS_Y] = input[mapping_dev].mmap[SYS_AXIS_Y];
}

void record_menu_tmp_axis_stage()
{
	const int idx = get_tmp_axis_stage_index(mapping_button);
	if (idx < 0) return;
	tmp_axis_stage_count[idx] = tmp_axis_n;
	tmp_axis_stage_seen |= 1 << idx;
}

static void restore_menu_tmp_axis_stage(int pos)
{
	const int idx = get_tmp_axis_stage_index(pos);
	if (idx < 0) return;

	tmp_axis_n = tmp_axis_stage_count[idx];
	if (tmp_axis_n > (int)(sizeof(tmp_axis) / sizeof(tmp_axis[0]))) tmp_axis_n = sizeof(tmp_axis) / sizeof(tmp_axis[0]);
	for (int i = tmp_axis_n; i < (int)(sizeof(tmp_axis) / sizeof(tmp_axis[0])); i++) tmp_axis[i] = 0;
	tmp_axis_stage_seen &= (2 << idx) - 1;

	mapping_last_axis = 0;
	sync_menu_tmp_axes();
}

static int tmp_axis_contains_axis(uint16_t axis)
{
	for (int i = 0; i < (int)(sizeof(tmp_axis) / sizeof(tmp_axis[0])); i++)
	{
		if ((tmp_axis[i] & MAP_FLAG_ANALOG) && map_axis_code(tmp_axis[i]) == axis) return 1;
	}
	return 0;
}

static int is_menu_stick2_axis(uint16_t axis)
{
	const int stage = get_tmp_axis_stage_index(-2);
	if (!(tmp_axis_stage_seen & (1 << stage))) return 0;
	for (int i = tmp_axis_stage_count[stage]; i < tmp_axis_n; i++)
		if ((tmp_axis[i] & MAP_FLAG_ANALOG) && map_axis_code(tmp_axis[i]) == axis) return 1;
	return 0;
}

static int reserve_tmp_axis_slots_for_pos(int pos)
{
	int min_count = 0;
	switch (pos)
	{
	case -3: min_count = 1; break;
	case -2: min_count = 2; break;
	case -1: min_count = 3; break;
	}

	while (tmp_axis_n < min_count && tmp_axis_n < (int)(sizeof(tmp_axis) / sizeof(tmp_axis[0])))
		tmp_axis[tmp_axis_n++] = 0;

	return tmp_axis_n < (int)(sizeof(tmp_axis) / sizeof(tmp_axis[0]));
}

static int append_tmp_axis_for_pos(int pos, uint16_t axis, int invert)
{
	if (!reserve_tmp_axis_slots_for_pos(pos)) return 0;
	tmp_axis[tmp_axis_n++] = axis | MAP_FLAG_ANALOG | (invert ? MAP_FLAG_INVERT : 0);
	return 1;
}

static uint64_t mapping_axis_centred = 0;

uint32_t osd_timer = 0;
static uint32_t map_advance_timer = 0;

static void clear_mapping_hold_state()
{
	mapping_hold_key = 0;
	mapping_hold_timer = 0;
	mapping_hold_feedback_timer = 0;
	mapping_hold_action = MAP_HOLD_NONE;
}

void clear_mapping_current_input()
{
	mapping_current_key = 0;
	mapping_current_dev = -1;
	clear_mapping_hold_state();
}

void clear_mapping_osd_hold_capture()
{
	mapping_osd_hold_timer = 0;
	mapping_osd_hold_latched = 0;
}

static void clear_mapping_combo_state()
{
	mapping_combo_second_key = 0;
	mapping_combo_release_mask = 0;
	clear_mapping_osd_hold_capture();
}

static void set_mapping_ignore_release_keys(int key1, int key2 = 0)
{
	mapping_ignore_release_key = key1;
	mapping_ignore_release_key2 = key2;
}

static void drop_mapping_menu_press()
{
	mapping_osd_owned = 0;
	osd_timer = 0;
	if (mapping_dev < 0) return;
	input[mapping_dev].osd_combo = 0;
	input[mapping_dev].osd_hold_timer = 0;
}

void invalidate_map_setting()
{
	if (mapping || mapping_paused || mapping_confirm) mapping_invalidated = 1;
	else if (!mapping_invalidated) mapping_invalidated = 2;
	mapping_osd_owned = 0;
	mapping = mapping_paused = mapping_confirm = mapping_confirm_result = 0;
	mapping_dev = -1;
	mapping_store = NULL;
	mapping_key_mapped = mapping_last_axis = 0;
	mapping_pending_hold_action = MAP_HOLD_NONE;
	osd_timer = map_advance_timer = 0;
	clear_mapping_current_input();
	clear_mapping_combo_state();
	clear_mapping_trigger_capture();
	set_mapping_ignore_release_keys(0);
}

static void do_map_clear()
{
	if (!mapping || mapping_type > 1 || mapping_dev < 0)
		return;

	clear_mapping_current_input();
	clear_mapping_combo_state();
	set_mapping_ignore_release_keys(0);
	mapping_key_mapped = 0;
	mapping_last_axis = 0;
	clear_mapping_trigger_capture();
	map_advance_timer = 0;
	osd_timer = 0;
	mapping_finish = 0;

	if (mapping_set) for (int i = 0; i < NUMBUTTONS; i++) input[mapping_dev].map[i] &= 0x0000FFFF;
	else memset(input[mapping_dev].map, 0, sizeof(input[mapping_dev].map));

	mapping_button = 0;
	mapping_clear = 1;
}

void advance_map_button()
{
	if (!(mapping_type <= 1) || mapping_button >= mapping_count)
		return;

	mapping_button += (mapping_button == -6) ? 2 : 1;

	while (mapping_type <= 1 && mapping_button < mapping_count)
	{
		int skip = 0;
		if (!is_menu() && mapping_button >= 4 && !strcmp(joy_bnames[mapping_button - 4], "-"))
			skip = 1;
		if (!skip) break;
		mapping_button++;
	}

	prepare_mapping_trigger_capture();
}

static int map_core_button_code(int dev, int code)
{
	if (!mapping || is_menu() || mapping_type > 1 || mapping_dev != dev || mapping_button < 0 || mapping_button >= mapping_count)
		return 0;

	const uint32_t mapped_code = (uint32_t)code;

	if (!mapping_button)
	{
		for (uint i = 0; i < sizeof(input[0].map) / sizeof(input[0].map[0]); i++)
		{
			input[dev].map[i] &= mapping_set ? 0x0000FFFF : 0xFFFF0000;
		}
	}

	int found = 0;
	for (int i = 0; i < mapping_button; i++)
	{
		if (mapping_set && (input[dev].map[i] >> 16) == mapped_code) found = 1;
		if (!mapping_set && (input[dev].map[i] & 0xFFFF) == mapped_code) found = 1;
	}

	if (!found)
	{
		if (mapping_set) input[dev].map[mapping_button] = (input[dev].map[mapping_button] & 0xFFFF) | (mapped_code << 16);
		else input[dev].map[mapping_button] = (input[dev].map[mapping_button] & 0xFFFF0000) | mapped_code;
		mapping_key_mapped = code;
		return 1;
	}

	return 0;
}

static int map_menu_button_code(int dev, int code, const input_absinfo *absinfo)
{
	if (mapping_dev != dev ||
		(!mapping_button && mapping_last_axis && ((code == mapping_last_axis) || (code == mapping_last_axis + 1))))
		return 0;

	input[dev].osd_combo = 0;

	int found = 0;
	if (mapping_button < SYS_MAP_POS_OSD + 1)
	{
		for (int i = (mapping_button >= BUTTON_DPAD_COUNT) ? BUTTON_DPAD_COUNT : 0; i < mapping_button; i++)
		{
			const int prev_idx = get_system_map_button_idx(i);
			if (prev_idx >= 0 && input[dev].map[prev_idx] == (uint32_t)code) found = 1;
		}
	}

	if (found && !(mapping_button == SYS_MAP_POS_OSD && mapping_type))
		return 0;

	if (mapping_button == SYS_MAP_POS_OSD + 1) input[dev].map[SYS_BTN_MENU_FUNC] = code & 0xFFFF;
	else if (mapping_button == SYS_MAP_POS_OSD + 2) input[dev].map[SYS_BTN_MENU_FUNC] = (code << 16) | input[dev].map[SYS_BTN_MENU_FUNC];
	else if (mapping_button == SYS_MAP_POS_OSD)
	{
		input[dev].map[SYS_BTN_OSD_KTGL + mapping_type] = code;
		input[dev].map[SYS_BTN_OSD_2] = input[dev].map[SYS_BTN_OSD_1];
		mapping_current_key = 0; // allow 2 buttons to be pressed
		if (mapping_type) mapping_osd_hold_timer = GetTimer(OSD_HOLD_DEFAULT_MS);
	}
	else
	{
		const int system_map_idx = get_system_map_button_idx(mapping_button);
		if (system_map_idx >= 0) input[dev].map[system_map_idx] = code;
		map_menu_analog_trigger(dev, code, absinfo);
	}

	mapping_key_mapped = code;

	const int mouse_axis_base = SYS_BTN_A + 1;
	if (menu_mouse_map && (mapping_button == mouse_axis_base || mapping_button == (mouse_axis_base + 2)))
	{
		const int cur_idx = get_system_map_button_idx(mapping_button);
		const int prev_idx = get_system_map_button_idx(mapping_button - 1);
		if (cur_idx >= 0 && prev_idx >= 0 &&
			input[dev].map[cur_idx] >= KEY_EMU &&
			input[dev].map[prev_idx] >= KEY_EMU &&
			(input[dev].map[prev_idx] - input[dev].map[cur_idx] == 1) && // same axis
			absinfo)
		{
			input[dev].map[SYS_AXIS_MX + (mapping_button - mouse_axis_base) / 2] = ((input[dev].map[cur_idx] - KEY_EMU) / 2) | MAP_FLAG_ANALOG;
		}
	}

	return 1;
}

static const char *get_map_hold_feedback_text(int action)
{
	switch (action)
	{
	case MAP_HOLD_SKIP:
		return "Skip/Unmap";
	case MAP_HOLD_BACK:
		return "Previous";
	case MAP_HOLD_CANCEL:
		return "Cancel";
	case MAP_HOLD_FINISH:
		return "Finish";
	default:
		return NULL;
	}
}

int map_hold_action_for_key(int dev, uint16_t code)
{
	const uint32_t *mmap = input[dev].mmap;
	if (mapping_type != 1) return MAP_HOLD_NONE;

	const int stick_stage = mapping_button >= -4 && mapping_button < 0;
	const int nav = (mapping_button >= 0 || stick_stage) && (!is_menu() || (mapping_type && !menu_mouse_map));
	if (nav && mmap[SYS_BTN_RIGHT] && code == mmap[SYS_BTN_RIGHT]) return MAP_HOLD_SKIP;
	if (nav && mmap[SYS_BTN_LEFT] && code == mmap[SYS_BTN_LEFT]) return MAP_HOLD_BACK;
	if (nav && mmap[SYS_BTN_SELECT] && code == mmap[SYS_BTN_SELECT]) return MAP_HOLD_CANCEL;
	if (nav && mmap[SYS_BTN_START] && code == mmap[SYS_BTN_START]) return MAP_HOLD_FINISH;
	return MAP_HOLD_NONE;
}

void set_menu_mouse_map(int enable)
{
	menu_mouse_map = enable ? 1 : 0;
}

int get_menu_mouse_map()
{
	return menu_mouse_map;
}

static void clear_mapping_slot_for_pos(uint32_t *map, int pos)
{
	if (is_menu())
	{
		if (pos < SYS_MAP_POS_OSD)
		{
			const int idx = get_system_map_button_idx(pos);
			if (idx >= 0) map[idx] = 0;
			if (is_menu_trigger_map_pos(pos)) map[SYS_AXIS_L2 + pos - SYS_MAP_BTN_L2] = 0;
			if (menu_mouse_map)
			{
				if (pos == SYS_BTN_A || pos == SYS_BTN_B) map[SYS_AXIS_MX] = 0;
				if (pos == SYS_BTN_X || pos == SYS_BTN_Y) map[SYS_AXIS_MY] = 0;
			}
		}
		else if (pos == SYS_MAP_POS_OSD)
		{
			map[SYS_BTN_OSD_1] = 0;
			map[SYS_BTN_OSD_2] = 0;
		}
		else if (pos == SYS_MAP_POS_OSD + 1)
		{
			map[SYS_BTN_MENU_FUNC] &= 0xFFFF0000;
		}
		else if (pos == SYS_MAP_POS_OSD + 2)
		{
			map[SYS_BTN_MENU_FUNC] &= 0x0000FFFF;
		}
	}
	else if (pos >= 0 && pos < mapping_count)
	{
		map[pos] &= mapping_set ? 0x0000FFFF : 0xFFFF0000;
	}
}

int step_back_map_setting()
{
	if (!mapping || mapping_type != 1 || mapping_dev < 0) return 0;

	int min_pos = is_menu() ? (menu_mouse_map ? SYS_BTN_A : mapping_menu_only ? SYS_MAP_POS_OSD : -6) : 0;
	int pos = mapping_button;

	if (pos >= mapping_count) pos = mapping_count - 1;
	else pos--;
	while (is_menu() && !menu_mouse_map && pos > -6 && pos < 0 && !(tmp_axis_stage_seen & (1 << (pos + 6)))) pos--;

	while (pos >= min_pos && !is_menu() && pos >= SYS_BTN_A && !strcmp(joy_bnames[pos - SYS_BTN_A], "-")) pos--;
	if (pos < min_pos) return 0;

	clear_mapping_slot_for_pos(input[mapping_dev].map, pos);
	mapping_button = pos;
	if (is_menu() && !menu_mouse_map && pos < 0) restore_menu_tmp_axis_stage(pos);
	clear_mapping_current_input();
	clear_mapping_osd_hold_capture();
	set_mapping_ignore_release_keys(0);
	mapping_clear = 0;
	mapping_finish = 0;
	prepare_mapping_trigger_capture();
	return 1;
}

void start_map_setting(int cnt, int set, advancedButtonMap *abm_store)
{
	mapping_invalidated = 0;
	mapping_osd_owned = 0;
	clear_mapping_current_input();
	clear_mapping_combo_state();
	set_mapping_ignore_release_keys(0);
	mapping_pending_hold_action = MAP_HOLD_NONE;

	mapping_button = 0;
	mapping = 1;
	mapping_paused = 0;
	mapping_confirm = 0;
	mapping_confirm_result = 0;
	mapping_set = set;

	if (abm_store)
	{
		mapping_type = 3;
		mapping_store = abm_store;
		mapping_dev = -1;
		mapping_set = (cnt == 3) ? 1 : set;
		if (cnt != 3)
			memset((set == 1) ? abm_store->input_codes : abm_store->output_codes, 0, sizeof(abm_store->input_codes));
		osd_timer = 0;
		map_advance_timer = 0;
	} else if (!mapping_set) {
		mapping_dev = -1;
		mapping_type = (cnt < 0) ? 3 : cnt ? 1 : 2;
	}
	mapping_count = cnt;
	mapping_clear = 0;
	mapping_finish = 0;
	mapping_menu_only = 0;
	mapping_existing_asked = 0;
	tmp_axis_n = 0;
	memset(tmp_axis_stage_count, 0, sizeof(tmp_axis_stage_count));
	tmp_axis_stage_seen = 1;
	mapping_axis_centred = 0;
	clear_mapping_trigger_capture();

	if (mapping_type <= 1 && is_menu()) mapping_button = menu_mouse_map ? SYS_BTN_A : -6;
	memset(tmp_axis, 0, sizeof(tmp_axis));
	prepare_mapping_trigger_capture();

	//un-stick the enter key
	user_io_kbd(KEY_ENTER, 0);
}

advancedButtonMap *get_map_code_store()
{
	return mapping_store;
}

int get_map_count()
{
	return mapping_count;
}

int poll_map_hold_action()
{
	if (mapping_pending_hold_action)
	{
		const int action = mapping_pending_hold_action;
		mapping_pending_hold_action = MAP_HOLD_NONE;
		return mapping ? action : 0;
	}

	if (!mapping || mapping_type > 1 || !mapping_hold_key || !mapping_hold_timer || !mapping_hold_action ||
		mapping_current_dev != mapping_dev)
		return 0;

	if (!CheckTimer(mapping_hold_timer))
		return 0;

	drop_mapping_menu_press();
	int action = mapping_hold_action;
	clear_mapping_current_input();
	return action;
}

int get_map_active()
{
	return mapping;
}

int get_map_paused()
{
	return mapping_paused;
}

int get_map_invalidated()
{
	const int result = mapping_invalidated;
	mapping_invalidated = 0;
	return result;
}

int get_map_set()
{
	return mapping_set;
}

int get_map_button()
{
	return mapping_button;
}

int get_map_type()
{
	return mapping_type;
}

int get_map_clear()
{
	return mapping_clear;
}

int get_map_finish()
{
	return mapping_finish;
}


int get_map_dev()
{
	return mapping_dev;
}


int get_map_cancel()
{
	return (mapping && !is_menu() && osd_timer && CheckTimer(osd_timer) && !mapping_hold_action);
}

int get_map_advance()
{
	return (mapping && !is_menu() && map_advance_timer && CheckTimer(map_advance_timer));
}

const char *get_map_hold_feedback()
{
	if (!mapping || mapping_type > 1 || !mapping_hold_key || !mapping_hold_timer || !mapping_hold_action)
		return NULL;

	if (CheckTimer(mapping_hold_timer))
		return NULL;

	if (!mapping_hold_feedback_timer || !CheckTimer(mapping_hold_feedback_timer))
		return NULL;

	return get_map_hold_feedback_text(mapping_hold_action);
}

const uint32_t *get_map_values()
{
	return (mapping_dev >= 0 && mapping_type == 1) ? input[mapping_dev].map : NULL;
}

int get_map_hold_progress()
{
	if (!mapping || mapping_type > 1 || !mapping_hold_key || !mapping_hold_timer || !mapping_hold_action) return -1;
	const unsigned long now = GetTimer(0);
	if (now >= mapping_hold_timer) return 100;
	return 100 - (int)((mapping_hold_timer - now) * 100 / MAP_HOLD_ACTION_MS);
}

char *get_unique_mapping(int dev, int force_unique)
{
	uint32_t vidpid = (input[dev].vid << 16) | input[dev].pid;
	static char str[128];

	for (uint i = 0; i < cfg.controller_unique_mapping[0]; i++)
	{
		if (!cfg.controller_unique_mapping[i + 1]) break;
		if (force_unique || cfg.controller_unique_mapping[i + 1] == 1 || cfg.controller_unique_mapping[i + 1] == vidpid)
		{
			sprintfz(str, "%s_%08x", input[dev].idstr, input[dev].unique_hash);
			return str;
		}
	}

	sprintfz(str, "%s", input[dev].idstr);
	return str;
}

template<size_t N>
void build_map_name(char (&name)[N], int dev, int def, int version)
{
	char *id = get_unique_mapping(dev);

	if (def || is_menu()) sprintfz(name, "input_%s%s_v%d.map", id, input[dev].mod ? "_m" : "", version);
	else sprintfz(name, "%s_input_%s%s_v%d.map", user_io_get_core_name(), id, input[dev].mod ? "_m" : "", version);
}

template void build_map_name(char (&name)[1024], int dev, int def, int version);

char *get_map_name(int dev, int def)
{
	static char name[1024];
	build_map_name(name, dev, def, 4);
	return name;
}

char *get_jkmap_name(int dev)
{
	static char name[1024];
	char *id = get_unique_mapping(dev);
	sprintfz(name, "%s_input_%s_jk.map", user_io_get_core_name(), id);
	return name;
}

char *get_kbdmap_name(int dev)
{
	static char name[128];
	char *id = get_unique_mapping(dev);

	sprintfz(name, "kbd_%s.map", id);
	return name;
}

void pause_map_setting(int pause)
{
	if (pause && mapping)
	{
		mapping = 0;
		mapping_paused = 1;
		mapping_key_mapped = 0;
		clear_mapping_current_input();
		mapping_pending_hold_action = MAP_HOLD_NONE;
		osd_timer = 0;
		map_advance_timer = 0;
	}
	else if (!pause && mapping_paused)
	{
		mapping = 1;
		mapping_paused = 0;
		user_io_kbd(KEY_ENTER, 0);
	}
}

void pause_map_for_prompt()
{
	if (mapping_dev >= 0 && is_menu() && !menu_mouse_map) memcpy(input[mapping_dev].mmap, mapping_mmap, sizeof(mapping_mmap));
	drop_mapping_menu_press();
	pause_map_setting(1);
}

void confirm_map_setting()
{
	drop_mapping_menu_press();
	pause_map_setting(1);
	mapping_confirm = 1;
	mapping_confirm_result = 0;
}

int get_map_confirm()
{
	const int result = mapping_confirm_result;
	mapping_confirm_result = 0;
	return result;
}

int pause_map_for_existing_map()
{
	if (!mapping || mapping_set || (is_menu() && menu_mouse_map) || mapping_type != 1 || mapping_dev < 0 || mapping_existing_asked)
		return 0;

	mapping_existing_asked = 1;
	const int existing = has_saved_map() ? 2 : input[mapping_dev].gcdb_mmap ? 1 : 0;
	if (!existing) return 0;

	pause_map_for_prompt();
	return existing;
}

void map_menu_buttons_only()
{
	if (!is_menu() || menu_mouse_map || mapping_dev < 0) return;

	memcpy(input[mapping_dev].mmap, mapping_mmap, sizeof(mapping_mmap));
	memcpy(input[mapping_dev].map, mapping_mmap, sizeof(mapping_mmap));
	for (int pos = SYS_MAP_POS_OSD; pos <= SYS_MAP_POS_OSD + 2; pos++) clear_mapping_slot_for_pos(input[mapping_dev].map, pos);
	mapping_menu_only = 1;
	mapping_type = 1;
	mapping_button = SYS_MAP_POS_OSD;
	mapping_key_mapped = 0;
	mapping_last_axis = 0;
	clear_mapping_current_input();
	clear_mapping_combo_state();
	set_mapping_ignore_release_keys(0);
	prepare_mapping_trigger_capture();
}

void finish_map_setting(int dismiss)
{
	mapping = 0;
	mapping_paused = 0;
	mapping_confirm = 0;
	mapping_confirm_result = 0;
	if (mapping_dev < 0 || mapping_dev >= NUMDEV) return;

	if (mapping_type == 3)
	{
		if (!dismiss) return;
		if (mapping_count == 3)
		{
			input_advanced_clear(mapping_dev);
			if (mapping_store) memset(mapping_store, 0, sizeof(advancedButtonMap));
		} else if (mapping_store) {
			memset(mapping_set == 2 ? mapping_store->output_codes : mapping_store->input_codes, 0, sizeof(mapping_store->input_codes));
		}
		return;
	}

	if (mapping_type == 2)
	{
		input[mapping_dev].has_kbdmap = 0;
		if (dismiss) FileDeleteConfig(get_kbdmap_name(mapping_dev));
		else FileSaveConfig(get_kbdmap_name(mapping_dev), &input[mapping_dev].kbdmap, sizeof(input[mapping_dev].kbdmap));
	}
	else
	{
		char legacy_name[1024];

		for (int i = 0; i < NUMDEV; i++)
		{
			input[i].has_map = 0;
			input[i].has_mmap = 0;
		}
		build_map_name(legacy_name, mapping_dev, 0, 3);

		if (!dismiss)
		{
			if (is_menu() && (menu_mouse_map || mapping_menu_only))
			{
				system_map_file saved = {};
				memcpy(saved.map, input[mapping_dev].map, sizeof(saved.map));
				memcpy(saved.directions, input[mapping_dev].right_directions, sizeof(saved.directions));
				saved.misc1 = input[mapping_dev].misc1;
				if (saved.misc1 || saved.directions[0] || saved.directions[1] || saved.directions[2] || saved.directions[3])
				{
					saved.magic = saved.misc1 ? MISC1_MAGIC : RIGHT_DIRECTIONS_MAGIC;
					const int size = saved.misc1 ? sizeof(saved) : sizeof(saved) - sizeof(saved.misc1);
					save_map(get_map_name(mapping_dev, 0), &saved, size);
				}
				else save_map(get_map_name(mapping_dev, 0), saved.map, sizeof(saved.map));
			}
			else save_map(get_map_name(mapping_dev, 0), &input[mapping_dev].map, sizeof(input[mapping_dev].map));
		}
		if (dismiss == 2)
		{
			delete_map(get_map_name(mapping_dev, 0));
			delete_map(legacy_name);
		}
	}
}

uint16_t get_map_vid()
{
	return (mapping && mapping_dev >= 0) ? input[mapping_dev].vid : 0;
}

uint16_t get_map_pid()
{
	return (mapping && mapping_dev >= 0) ? input[mapping_dev].pid : 0;
}

const char *get_map_gcdb_name()
{
	static char name[64];
	static int cached_dev = -1;
	static uint16_t cached_id[4];

	if (!mapping || mapping_dev < 0) return "";
	const uint16_t id[4] = { input[mapping_dev].bustype, input[mapping_dev].vid, input[mapping_dev].pid, input[mapping_dev].gcdb_version };
	if (cached_dev != mapping_dev || memcmp(id, cached_id, sizeof(id)))
	{
		cached_dev = mapping_dev;
		memcpy(cached_id, id, sizeof(id));
		if (!gcdb_name_for_controller(id[0], id[1], id[2], id[3], name, sizeof(name))) name[0] = 0;
	}
	return name;
}

int has_default_map()
{
	return (mapping_dev >= 0) ? (input[mapping_dev].has_mmap == 1) : 0;
}

static int map_exists(const char *name)
{
	char path[256];
	get_joymap_path(path, name);
	return FileLoadConfig(path, 0, 0) || FileLoadConfig(name, 0, 0);
}

int has_saved_map()
{
	if (mapping_dev < 0) return 0;

	char legacy_name[1024];
	build_map_name(legacy_name, mapping_dev, 0, 3);
	return map_exists(get_map_name(mapping_dev, 0)) || map_exists(legacy_name);
}

int map_osd_hold_poll()
{
	if (!mapping_osd_hold_timer || !CheckTimer(mapping_osd_hold_timer)) return 0;
	mapping_osd_hold_timer = 0;
	if (!mapping || !is_menu() || mapping_type != 1 || mapping_dev < 0 ||
		mapping_button != SYS_MAP_POS_OSD || !mapping_key_mapped || mapping_combo_second_key)
		return 0;

	input[mapping_dev].map[SYS_BTN_OSD_1] = OSD_HOLD_MARKER;
	input[mapping_dev].map[SYS_BTN_OSD_2] = mapping_key_mapped;
	mapping_osd_hold_latched = 1;
	printf("Set hold: %x\n", mapping_key_mapped);
	return 1;
}

int get_map_osd_hold()
{
	if (mapping_osd_hold_latched) return 100;
	if (!mapping_osd_hold_timer) return -1;
	const unsigned long now = GetTimer(0);
	if (now >= mapping_osd_hold_timer) return 99;
	return 100 - (int)((mapping_osd_hold_timer - now) * 100 / OSD_HOLD_DEFAULT_MS);
}

void get_map_osd_positions(int *first, int *second)
{
	*first = *second = -1;
	if (!mapping || mapping_dev < 0 || mapping_button != SYS_MAP_POS_OSD || !mapping_key_mapped) return;

	const uint32_t *map = input[mapping_dev].map;
	const uint32_t codes[2] = { osd_map_is_hold(map) ? 0 : map[SYS_BTN_OSD_1], map[SYS_BTN_OSD_2] };
	int *found[2] = { first, second };
	for (int n = 0; n < 2; n++)
	{
		for (int pos = 0; codes[n] && pos < SYS_MAP_POS_OSD; pos++)
		{
			const int idx = get_system_map_button_idx(pos);
			if (idx >= 0 && map[idx] == codes[n])
			{
				*found[n] = pos;
				break;
			}
		}
	}
}

int get_map_trigger_level()
{
	if (!mapping || !is_menu() || !is_menu_trigger_map_pos(mapping_button) || mapping_dev < 0 || !mapping_key_mapped)
		return -1;

	const uint32_t map = input[mapping_dev].map[SYS_AXIS_L2 + (mapping_button - SYS_MAP_BTN_L2)];
	if (!(map & MAP_FLAG_TRIGGER) || mapping_node_fd() < 0)
		return -1;

	input_absinfo absinfo = {};
	if (ioctl(mapping_node_fd(), EVIOCGABS(map_axis_code(map)), &absinfo) < 0)
		return -1;

	return normalize_analog_trigger(map, absinfo.value, &absinfo);
}

static bool map_uses_xy_ends(const uint32_t *map)
{
	const uint32_t first = KEY_EMU + (ABS_X << 1), last = KEY_EMU + (ABS_Y << 1) + 1;
	const uint32_t own[] = { first + 1, first, last, last - 1 };
	for (int i = SYS_BTN_RIGHT; i <= SYS_BTN_MENU_FUNC; i++)
	{
		if (i > SYS_BTN_START && i != SYS_BTN_MENU_FUNC) continue;
		const uint32_t codes[] = { map[i] & 0xFFFF, map[i] >> 16 };
		for (uint32_t code : codes)
			if (code >= first && code <= last && !(i <= SYS_BTN_UP && code == own[i])) return true;
	}
	return false;
}

void default_v3_stick(int dev, int fd, uint32_t *map)
{
	if (map[SYS_AXIS1_X] || map[SYS_AXIS1_Y] || input[dev].lightgun || input[dev].quirk == QUIRK_WHEEL ||
		input[dev].quirk == QUIRK_LIGHTGUN || input[dev].quirk == QUIRK_LIGHTGUN_CRT || input[dev].quirk == QUIRK_LIGHTGUN_MOUSE ||
		map_uses_xy_ends(map))
		return;

	for (int i = SYS_AXIS2_X; i <= SYS_AXIS2_Y; i++)
		if ((map[i] & MAP_FLAG_ANALOG) && map_axis_code(map[i]) <= ABS_Y) return;

	unsigned char absbits[(ABS_MAX + 1 + 7) / 8] = {};
	if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0 ||
		!test_bit(ABS_X, absbits) || !test_bit(ABS_Y, absbits)) return;

	for (int axis = ABS_X; axis <= ABS_Y; axis++)
	{
		input_absinfo info;
		if (ioctl(fd, EVIOCGABS(axis), &info) < 0) return;
		const int64_t range = (int64_t)info.maximum - info.minimum;
		if (range <= 2 || llabs(2 * (int64_t)info.value - info.minimum - info.maximum) > range / 2) return;
	}
	map[SYS_AXIS1_X] = ABS_X | MAP_FLAG_ANALOG;
	map[SYS_AXIS1_Y] = ABS_Y | MAP_FLAG_ANALOG;
}

int map_confirm_event(int dev, const struct input_event *ev, bool menu_event)
{
	if (!mapping_confirm || dev != mapping_dev) return 0;

	if (ev->type == EV_KEY && ev->value == 1)
	{
		const uint32_t *map = input[dev].map, *mmap = input[dev].mmap;
		const uint32_t ok = map[SYS_BTN_MENU_FUNC] & 0xFFFF;
		const uint32_t back = map[SYS_BTN_MENU_FUNC] >> 16;
		const uint32_t start = map[SYS_BTN_START];
		const uint32_t old_ok = ok ? 0 : (mmap[SYS_BTN_MENU_FUNC] & 0xFFFF) ? (mmap[SYS_BTN_MENU_FUNC] & 0xFFFF) : (mmap[SYS_BTN_A] & 0xFFFF);
		const uint32_t old_back = back ? 0 : (mmap[SYS_BTN_MENU_FUNC] >> 16) ? (mmap[SYS_BTN_MENU_FUNC] >> 16) : (mmap[SYS_BTN_B] & 0xFFFF);
		const uint32_t old_start = start ? 0 : mmap[SYS_BTN_START];
		if ((ok && ev->code == ok) || (start && (ev->code == (start & 0xFFFF) || ev->code == (start >> 16))))
			mapping_confirm_result = 1;
		else if (back && ev->code == back)
			mapping_confirm_result = -1;
		else if ((old_ok && ev->code == old_ok) || (old_start && (ev->code == (old_start & 0xFFFF) || ev->code == (old_start >> 16))))
			mapping_confirm_result = 1;
		else if (old_back && ev->code == old_back)
			mapping_confirm_result = -1;
		else if (!ok && !back && !start && !menu_event && ev->code < KEY_EMU && !mapping_has_key(old_ok) && !mapping_has_key(old_back) && !mapping_has_key(old_start & 0xFFFF))
			mapping_confirm_result = 1;
	}
	return 1;
}

void map_advanced_key(struct input_event *ev, int dev, int map_skip)
{
	bool jkm_can_intr = mapping_count == 3 && mapping_button <= 1 && mapping_set == 1;
	if (mapping_dev < 0) mapping_dev = dev;
	if (ev->value == 1 && ev->code == KEY_ENTER) map_skip = 1;
	if (map_skip && jkm_can_intr)
		mapping_finish = 1;

	if (jkm_can_intr
		  && (ev->code == (input[mapping_dev].mmap[SYS_BTN_MENU_FUNC] & 0xFFFF) || ev->code == KEY_F12))
				map_advance_timer = ev->value ? (ev->code == KEY_F12) ? 1 : GetTimer(5000) : 0;

	if (ev->value == 1)
	{
		uint16_t *code_store = mapping_set ==  1 ? mapping_store->input_codes : mapping_store->output_codes;
		code_store[mapping_button++] = ev->code;
		mapping_current_dev = mapping_dev;
		if (mapping_button == sizeof(mapping_store->input_codes)/sizeof(mapping_store->input_codes[0])) //Don't overflow, just keep replacing the last entry
			mapping_button--;
		if (mapping_button > 1)
			osd_timer = map_advance_timer = 0;
	} else if (ev->value == 0 && mapping_button > 0) {
		if (ev->code == KEY_ESC && (jkm_can_intr || (mapping_count < 3 && mapping_button <= 1)))
		{
			osd_timer = 1;
			return;
		}

		if (mapping_count == 3)
		{
			mapping_set++;
			mapping_button = 0;
			if (mapping_set > 2)
			{
				mapping_set = 1;
				input_advanced_save_entry(mapping_store, mapping_dev);
				memset(mapping_store, 0, sizeof(advancedButtonMap));
			}
		} else {
			mapping_finish = 1;
		}
		osd_timer = 0;
	}
}

void map_input_event(struct input_event *ev, struct input_absinfo *absinfo, int dev, int map_skip, int osd_event)
{
	int idx = 0;
	int released_hold_action = MAP_HOLD_NONE;
	int released_hold_feedback_expired = 0;
	int released_hold_timer_expired = 0;

	if (is_menu())
	{
		spi_uio_cmd(UIO_KEYBOARD); //ping the Menu core to wakeup
		osd_event = 0;
	}

	// paddle axis - skip from mapping
	if ((ev->type == EV_ABS || ev->type == EV_REL) && (ev->code == 7 || ev->code == 8) && input[dev].quirk != QUIRK_WHEEL) return;

	if (ev->type == EV_KEY && absinfo && ev->code >= KEY_EMU && is_menu() && mapping_type == 1 && !menu_mouse_map &&
		mapping_dev == dev && is_menu_stick2_axis((ev->code - KEY_EMU) >> 1)) return;

	if (ev->type == EV_KEY && !ev->value && mapping_dev == dev &&
		((mapping_ignore_release_key && mapping_ignore_release_key == ev->code) ||
		 (mapping_ignore_release_key2 && mapping_ignore_release_key2 == ev->code)))
	{
		if (mapping_ignore_release_key == ev->code) mapping_ignore_release_key = 0;
		if (mapping_ignore_release_key2 == ev->code) mapping_ignore_release_key2 = 0;
		return;
	}

	// protection against joysticks generating 2 codes per button
	if (ev->type == EV_KEY && !(is_menu() && mapping < 2 && mapping_button == SYS_MAP_POS_OSD) && !map_skip &&
		(mapping_dev < 0 || mapping_dev == dev))
	{
		if (!mapping_current_key)
		{
			if (ev->value == 1)
			{
				const int hold_action = (mapping_button < 0 && ev->code >= KEY_EMU && absinfo) ? MAP_HOLD_NONE : map_hold_action_for_key(dev, ev->code);
				mapping_current_key = ev->code;
				mapping_current_dev = dev;
				mapping_hold_key = hold_action ? ev->code : 0;
				mapping_hold_timer = GetTimer(MAP_HOLD_ACTION_MS);
				mapping_hold_feedback_timer = hold_action ? GetTimer(MAP_HOLD_FEEDBACK_DELAY_MS) : 0;
				mapping_hold_action = hold_action;
			}
			else return;
		}
		else
		{
			if (ev->value == 0 && mapping_current_key == ev->code && mapping_current_dev == dev)
			{
				released_hold_action = (mapping_hold_key && ev->code == mapping_hold_key) ? mapping_hold_action : MAP_HOLD_NONE;
				released_hold_feedback_expired = mapping_hold_feedback_timer && CheckTimer(mapping_hold_feedback_timer);
				released_hold_timer_expired = mapping_hold_timer && CheckTimer(mapping_hold_timer);
				clear_mapping_current_input();
			}
			else return;
		}
	}

	if (map_skip) clear_mapping_current_input();
	if (ev->type == EV_ABS && absinfo)
		update_menu_analog_trigger_axis(dev, ev->code, absinfo);

	if (ev->type == EV_KEY && mapping_button>=0 && !osd_event)
	{
		if (mapping_type == 2) // keyboard remap
		{
			if (ev->code < 256 && !input[dev].force_joy)
			{
				if (!mapping_button)
				{
					if (ev->value == 1)
					{
						if (mapping_dev < 0)
						{
							mapping_dev = dev;
							mapping_button = 0;
						}

						if (!mapping_button) mapping_button = ev->code;
						mapping_current_dev = mapping_dev;
					}
				}
				else
				{
					if (ev->value == 0 && mapping_dev >= 0 && mapping_button != ev->code)
					{
						input[mapping_dev].kbdmap[mapping_button] = ev->code;
						mapping_button = 0;
					}
				}
			}
			return;
		}
		else
		{
			int clear = (ev->code == KEY_F12 || ev->code == KEY_MENU || ev->code == KEY_HOMEPAGE) && !is_menu();
			if (ev->value == 1 && mapping_dev < 0 && !clear && !map_skip)
			{
				claim_mapping_device(dev);
				mapping_type = (ev->code >= 256 || input[dev].force_joy) ? 1 : 0;
				mapping_key_mapped = 0;
			}

			mapping_clear = 0;
			if (mapping_dev >= 0 && !map_skip && (mapping_dev == dev || clear) && mapping_button < (is_menu() ? (menu_mouse_map ? mapping_count : (mapping_type ? SYS_MAP_GAMEPAD_COUNT : SYS_MAP_POS_OSD + 1)) : mapping_count))
			{
				if (ev->value == 1 && !mapping_key_mapped)
				{
					if (clear)
					{
						do_map_clear();
					}
					else if (mapping_hold_key && ev->code == mapping_hold_key && mapping_hold_action)
					{
					}
					else if (is_menu())
					{
						map_menu_button_code(dev, ev->code, absinfo);
					}
					else
					{
						map_core_button_code(dev, ev->code);
					}
				}
				//combo for osd button
				if (ev->value == 1 && mapping_key_mapped && mapping_key_mapped != ev->code && is_menu() && mapping_button == SYS_MAP_POS_OSD && mapping_type && !mapping_osd_hold_latched && !mapping_combo_second_key)
				{
					mapping_osd_hold_timer = 0;
					if (mapping_hold_key && ev->code != mapping_hold_key)
					{
						clear_mapping_hold_state();
					}
					mapping_combo_second_key = ev->code;
					mapping_combo_release_mask = 0;
					input[dev].map[SYS_BTN_OSD_2] = ev->code;
					printf("Set combo: %x + %x\n", input[dev].map[SYS_BTN_OSD_1], input[dev].map[SYS_BTN_OSD_2]);
				}
				else if (mapping_dev == dev && ev->value == 0 && mapping_button == SYS_MAP_POS_OSD && mapping_type && mapping_key_mapped && mapping_combo_second_key)
				{
					if (ev->code == mapping_key_mapped) mapping_combo_release_mask |= 1;
					if (ev->code == mapping_combo_second_key) mapping_combo_release_mask |= 2;
					if (mapping_combo_release_mask == 3)
					{
						mapping_button++;
						mapping_key_mapped = 0;
						clear_mapping_combo_state();
					}
				}
				else if (mapping_dev == dev && ev->value == 0 &&
					released_hold_action != MAP_HOLD_NONE && !mapping_key_mapped)
				{
					clear_mapping_combo_state();
					if (released_hold_timer_expired)
					{
						mapping_pending_hold_action = released_hold_action;
					}
					else if (!released_hold_feedback_expired)
					{
						const int mapped = is_menu() ? map_menu_button_code(dev, ev->code, absinfo) : map_core_button_code(dev, ev->code);
						if (mapped)
						{
							mapping_button++;
							mapping_key_mapped = 0;
						}
					}
				}
				else if(mapping_dev == dev && ev->value == 0 && mapping_key_mapped == ev->code)
				{
					if (mapping_hold_key && ev->code == mapping_hold_key)
					{
						clear_mapping_hold_state();
					}
					mapping_button++;
					mapping_key_mapped = 0;
					clear_mapping_combo_state();
				}

				if(!ev->value && mapping_dev == dev && ((ev->code == mapping_last_axis) || (ev->code == mapping_last_axis+1)))
				{
					mapping_last_axis = 0;
				}
			}
		}
	}
	else if (is_menu())
	{
		//Define min-0-max analogs
		switch (mapping_button)
		{
			case -4: idx = SYS_AXIS1_X; break;
			case -3: idx = SYS_AXIS1_Y; break;
			case -2: idx = SYS_AXIS2_X; break;
			case -1: idx = SYS_AXIS2_Y; break;
		}

		if (mapping_dev == dev || (mapping_dev < 0 && mapping_button < 0))
		{
			int max = 0, min = 0;

			if (ev->type == EV_ABS)
			{
				const int64_t threshold = ((int64_t)absinfo->maximum - absinfo->minimum) / 5;
				const int64_t centre = ((int64_t)absinfo->minimum + absinfo->maximum) / 2;

				max = (ev->value >= (absinfo->maximum - threshold));
				if (ev->code <= ABS_MAX && llabs(ev->value - centre) <= threshold) mapping_axis_centred |= 1ULL << ev->code;

				min = absinfo->maximum > 2 && ev->code <= ABS_MAX && (mapping_axis_centred & (1ULL << ev->code)) &&
					(ev->value <= (absinfo->minimum + threshold));
			}

			//check DPAD horz
			if (mapping_button == -6)
			{
				mapping_last_axis = 0;
				if (ev->type == EV_ABS && (max || min))
				{
					claim_mapping_device(dev);
					mapping_type = 1;

					if (absinfo->maximum > 2)
					{
						tmp_axis[tmp_axis_n++] = ev->code | MAP_FLAG_ANALOG | (min ? MAP_FLAG_INVERT : 0);
						mapping_button++;
					}
					else
					{
						//Standard DPAD event
						mapping_button += 2;
					}
				}
				else if (ev->type == EV_KEY && ev->value == 1)
				{
					//DPAD uses simple button events
					if (!map_skip)
					{
						mapping_button += 2;
						claim_mapping_device(dev);
						if (ev->code < 256)
						{
							// keyboard, skip stick 1/2
							mapping_button += 4;
							mapping_type = 0;
						}
					}
				}
			}
			//check DPAD vert
			else if (mapping_button == -5)
			{
				if (ev->type == EV_ABS && (max || min) && absinfo->maximum > 1 && ev->code != (tmp_axis[0] & 0xFFFF))
				{
					tmp_axis[tmp_axis_n++] = ev->code | MAP_FLAG_ANALOG | (min ? MAP_FLAG_INVERT : 0);
					mapping_button++;
				}
			}
			//Sticks
			else if (ev->type == EV_ABS && idx)
			{
				claim_mapping_device(dev);

				if (idx && (max || min) && absinfo->maximum > 2)
				{
					if (mapping_button < 0)
					{
						if (!tmp_axis_contains_axis(ev->code) && append_tmp_axis_for_pos(mapping_button, ev->code, min))
						{
							mapping_type = 1;
							mapping_button++;
							if (tmp_axis_n >= 4) mapping_button = 0;
							mapping_last_axis = KEY_EMU + (ev->code << 1);
							if ((mapping_current_key & ~1) == mapping_last_axis) clear_mapping_current_input();
						}
					}
				}
			}
		}
	}

	while (mapping_type <= 1 && mapping_button < mapping_count)
	{
		if (map_skip)
		{
			if (map_skip == 2 || ev->value == 1)
			{
				if (mapping_dev >= 0)
				{
					if (idx) input[mapping_dev].map[idx] = 0;
					else if (mapping_button >= 0) clear_mapping_slot_for_pos(input[mapping_dev].map, mapping_button);
				}
				set_mapping_ignore_release_keys(mapping_key_mapped, mapping_combo_second_key);
				mapping_key_mapped = 0;
				clear_mapping_combo_state();
				mapping_last_axis = 0;
				advance_map_button();
				map_skip = 0;
				break;
			}
		}

		map_skip = 0;
		if (mapping_button >= 4 && !is_menu() && !strcmp(joy_bnames[mapping_button - 4], "-")) map_skip = 2;
		if (!map_skip) break;
	}

	prepare_mapping_trigger_capture();

	if (is_menu() && mapping_type <= 1 && !menu_mouse_map)
	{
		if (mapping_button < 0) record_menu_tmp_axis_stage();
		sync_menu_tmp_axes();
	}
}
