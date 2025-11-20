#ifndef ROMUTILS_H_
#define ROMUTILS_H_

int arcade_send_rom(const char *xml);
int xml_load(const char *xml);
void arcade_check_error();

struct dip_struct
{
	int start;
	int size;
	int num;
	int has_val;
	uint64_t mask;
	char name[32];
	char id[32][32];
	uint64_t val[32];
};

struct sw_struct
{
	char name[1024];
	int dip_num;
	uint64_t dip_def;
	uint64_t dip_cur;
	uint64_t dip_saved;
	dip_struct dip[64];
};

#define MGL_ACTION_LOAD  0
#define MGL_ACTION_RESET 1

struct mgl_item_struct
{
	char path[1024];
	int  delay;
	char type;
	union
	{
		int  index;
		int  hold;
	};
	int  valid;
	int  submenu;
	int  action;
};

struct mgl_struct
{
	int  count;
	int  current;
	mgl_item_struct item[6];
	uint32_t timer;
	int  state;
	int  done;
};

sw_struct *arcade_sw();
void arcade_sw_send();
void arcade_sw_save();
void arcade_sw_load();

// Read any mra info necessary for ini processing
void arcade_pre_parse(const char *xml);

bool arcade_is_vertical();
int arcade_get_direction();

void arcade_nvm_save();

mgl_struct* mgl_parse(const char *xml);
mgl_struct* mgl_get();

#endif
