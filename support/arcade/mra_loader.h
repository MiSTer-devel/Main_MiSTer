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

struct mgl_struct
{
	char path[1024];
	int  delay;
	char type;
	int  index;
	int  valid;
	int  parsed;
	uint32_t timer;
};

sw_struct *arcade_sw(int n);
void arcade_sw_send(int n);
void arcade_sw_save(int n);
void arcade_sw_load(int n);
void arcade_override_name(const char *xml);

void arcade_nvm_save();

mgl_struct* mgl_parse(const char *xml);
mgl_struct* mgl_get();

#endif
