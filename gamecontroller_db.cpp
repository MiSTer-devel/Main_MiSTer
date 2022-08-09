#include "gamecontroller_db.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "input.h"
#include "file_io.h"
#include "user_io.h"
#include "profiling.h"




static const char *sdlname_to_mister_idx[] = {

	"dpright",
	"dpleft",
	"dpdown",
	"dpup",
	"a",
	"b",
	"x",
	"y",
	"leftshoulder",
	"rightshoulder",
	"back",
	"start",
	"---",
	"---",
	"---",
	"---",
	"---",
	"---",
	"---",
	"---",
	"---", //20
	"guide", //21
	"---",
	"---",
	"leftx",
	"lefty",
	"rightx",
	"righty",
};


typedef struct {

	char guid[256];
	//char name[256]; Do we really care?
	char mapping_str[1024];
	bool map_parsed;
	uint32_t map[NUMBUTTONS];
} controllerdb_entry;




static controllerdb_entry db_maps[MAX_GCDB_ENTRIES] = {};

//platform should be at the end of mapping strings. this function will null the start of platform: if found
static bool cdb_entry_matches(char *db_str)
{
	char *pl_ptr = NULL;
	
	if (!db_str || !strlen(db_str)) return false;
	pl_ptr = strcasestr(db_str, "platform:");
	if (!pl_ptr) return false;
	*pl_ptr = 0; 
	pl_ptr += strlen("platform:");
	if (!strncasecmp(pl_ptr, "Linux", 5)) return true;
	if (!strncasecmp(pl_ptr, "MiSTer", 6))
	{
		char *core_ptr = NULL;
		core_ptr = strcasestr(db_str, "mistercore:");
		if (!core_ptr) return true; //platform:MiSTer with no core designators is a generic match
		while(core_ptr)
		{
				char *nxt_c = NULL;
				char *match_c = NULL;
				core_ptr += 11;
				nxt_c = strchr(core_ptr, ',');
				if (!nxt_c) break;
				match_c = strchr(core_ptr, '*');
				if (!match_c || match_c >= nxt_c) 
				{	
					match_c = nxt_c;
				}


				if (!strncasecmp(core_ptr, user_io_get_core_name(), match_c - core_ptr)) return true;
				if (!strncasecmp(core_ptr, user_io_get_core_name(1), match_c - core_ptr)) return true;
				core_ptr = strcasestr(nxt_c, "mistercore:");
		}
	}
	return false;

}
static int find_mister_button_num(char *sdl_name, bool *idx_high)
{
	*idx_high = false;
	for(size_t i = 0; i < sizeof(sdlname_to_mister_idx)/sizeof(char *); i++)
	{
		const char *map_str = sdlname_to_mister_idx[i];
		if (!strcmp(map_str, sdl_name)) return i;
	}
	if (!strcasecmp(sdl_name, "menuok"))
	{
		*idx_high = false;
		return SYS_BTN_MENU_FUNC;
	}

	if (!strcasecmp(sdl_name, "menuesc"))
	{
		*idx_high = true;
		return SYS_BTN_MENU_FUNC;
	}
	return -1;
}


static int find_linux_code_for_button(char *btn_name)
{
	if (!btn_name || !strlen(btn_name)) return -1;

	switch(btn_name[0])
	{
			case 'b':
			{
				//Normal button
				int bidx = strtol(btn_name+1, NULL, 10);
				return BTN_GAMEPAD+bidx;
				break;
			}
			case 'a':
			{
				int aidx = strtol(btn_name+1, NULL, 10);
				return ABS_X+aidx;
				break;
			}
			case 'h':
			//Mister creates fake digital buttons for hats that depend on the code and axis direction. 
			{
				char *dot_ptr = NULL;
				int hidx = strtol(btn_name+1, NULL, 10);
				int base_hat = ABS_HAT0X + hidx*2;
				dot_ptr = strchr(btn_name, '.');
				if (dot_ptr)
				{
					int hat_dir = strtol(dot_ptr+1, NULL, 10);
					switch(hat_dir)
					{
						case 1:
							return KEY_EMU + ((base_hat+1) << 1);
							break;
						case 2:
							return KEY_EMU + (base_hat << 1) + 1;
							break;
						case 4:
							return KEY_EMU + ((base_hat+1) << 1) + 1;
						case 8:
							return KEY_EMU + (base_hat << 1); 
							break;
						default:
							return -1;
							break;
					}
				}
				break;
			}
			default:
				return -1;
	}
	return -1;
}

static int get_index_for_guid(char *guid, bool adding = true)
{

	for (int i = 0; i < MAX_GCDB_ENTRIES; i++)
	{
			if (!strlen(db_maps[i].guid))
			{
				if (adding) return i;
				return -1;
			}
			if (!strcmp(db_maps[i].guid, guid)) return i ;
	}
	//ran out???
	return -1;

}

static bool parse_mapping_for_entry(controllerdb_entry *entry)
{
	if (!entry || !entry->mapping_str[0]) return false;

	char l_btn[20] = {};
	char m_btn[20] = {};
	bool in_m_btn = true;
	int i = 0;

	char *cur_str = entry->mapping_str;

	while (cur_str && *cur_str)
	{
		if (*cur_str == ':')
		{
			i = 0;
			in_m_btn = false;
		} else if (*cur_str == ',') {
			i = 0;
			in_m_btn = true;
			if (l_btn[0] && m_btn[0])
			{
				bool m_button_high = false;
				int m_button_num = find_mister_button_num(m_btn, &m_button_high);
				int l_button_code = find_linux_code_for_button(l_btn);
				if (m_button_num != -1 && l_button_code != -1)
				{

					entry->map_parsed = true;
					entry->map[m_button_num] = m_button_high ? ((l_button_code << 16) | entry->map[m_button_num]) : ((l_button_code & 0xFFFF)  | entry->map[m_button_num]);
					if (m_button_num == SYS_BTN_OSD_KTGL+1) entry->map[m_button_num+1] = l_button_code; //guide button
				}
			}
			bzero(l_btn, sizeof(l_btn));
			bzero(m_btn, sizeof(m_btn));
		} else if (in_m_btn) {
			//Just truncate button names if they are too big
			if (i <= sizeof(m_btn))
			{
				m_btn[i] = *cur_str;
				i++;
			}
		}	 else {
			if (i <= sizeof(l_btn))
			{
				l_btn[i] = *cur_str;
				i++;
			}
		}	
		cur_str++;
	}

	if (entry->map_parsed)
	{
		if ((entry->map[SYS_BTN_MENU_FUNC] & 0xFFFF) == 0)
		{
			entry->map[SYS_BTN_MENU_FUNC] = entry->map[SYS_BTN_A] & 0xFFFF;
		}

		if ((entry->map[SYS_BTN_MENU_FUNC] & 0xFFFF0000) == 0)
		{
			entry->map[SYS_BTN_MENU_FUNC] = (entry->map[SYS_BTN_B] << 16) | entry->map[SYS_BTN_MENU_FUNC];
		}
	}
	return entry->map_parsed;
}


static bool add_controllerdb_entry(const char *db_line)
{
	char *cur_str = (char *)db_line; 
	char *tmp = NULL;
	uint32_t line_map[NUMBUTTONS] = {};
	bool is_linux, is_mister;

	bool entry_match = cdb_entry_matches(cur_str);
	//cur_str now has platform: and everything after stripped
	if (!entry_match) return false;

	char *guid_ptr = cur_str;
	tmp = strchr(cur_str, ',');
	if (!tmp) return false;
	*tmp = 0;
	cur_str = tmp + 1;

	if (!strlen(guid_ptr)) return false;
	//char *name_ptr = cur_str;
	tmp = strchr(cur_str, ',');
	if (!tmp) return false;
	*tmp = 0;
	cur_str = tmp + 1;

	int guid_idx = get_index_for_guid(guid_ptr);
	if (guid_idx > -1)
	{
		strcpy(db_maps[guid_idx].guid, guid_ptr);
		strncpy(db_maps[guid_idx].mapping_str, cur_str, sizeof(db_maps[guid_idx].mapping_str));
	}

	return true;
}


#define GCDB_DIR  "/media/fat/linux/gamecontrollerdb/"

void load_gcdb_file(char *fname) 
{
	PROFILE_FUNCTION();
	int map_cnt = 0;
	printf("controllerdb: load file %s\n", fname);
	fileTextReader reader;
	if (FileOpenTextReader(&reader, fname))
	{
		const char *line;
		while ((line = FileReadLine(&reader)))
		{
			if (add_controllerdb_entry(line)) map_cnt++;
		}
	}
	printf("controllerdb: entries %d\n", map_cnt);
}

void load_gcdb_maps()
{
	PROFILE_FUNCTION();
	char path[256] = { GCDB_DIR };
	strcat(path, "gamecontrollerdb.txt");
	load_gcdb_file(path);
	strcpy(path, GCDB_DIR);
	strcat(path, "gamecontrollerdb_user.txt");
	load_gcdb_file(path);
}

bool gcdb_map_for_controller(uint16_t bustype, uint16_t vid, uint16_t pid, uint16_t version, uint32_t *fill_map)
{
		PROFILE_FUNCTION();
		char guid_str[128] = {};
		sprintf(guid_str, "%04x0000%04x0000%04x0000%04x0000", (uint16_t)(bustype << 8 | bustype >> 8), (uint16_t)( vid << 8 |  vid >> 8), (uint16_t)(pid << 8 | pid >> 8), (uint16_t)(version << 8 | version >> 8));

		int guid_idx = get_index_for_guid(guid_str, false);
		if (guid_idx == -1) return false;
		printf("controllerdb: Found entry for controller GUID %s\n", guid_str);
		if (!db_maps[guid_idx].map_parsed)
		{
			parse_mapping_for_entry(&db_maps[guid_idx]);
		}	
		if (db_maps[guid_idx].map_parsed)
		{
			memcpy(fill_map, db_maps[guid_idx].map, sizeof(uint32_t)*NUMBUTTONS);
			return true;
		}
		return false;
}









