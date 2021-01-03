#ifndef ROMUTILS_H_
#define ROMUTILS_H_

int arcade_send_rom(const char *xml);
int arcade_load(const char *xml);
void arcade_check_error();
int is_arcade();

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

sw_struct *arcade_sw();
void arcade_sw_send();
void arcade_sw_save();
void arcade_sw_load();
void arcade_override_name(const char *xml);

void arcade_nvm_save();

#endif
