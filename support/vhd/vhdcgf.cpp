// vhdcfg.cpp uses simplified code from cfg.cpp
// 2015, rok.krajnc@gmail.com
// 2017+, Sorgelig

#include <cctype>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "vhdcfg.h"
#include "file_io.h"

/* ATA Spec:
	Sectors: 8-bit
	Heads: 4-bit
	Cylinders: 16-bit
*/

typedef enum
{
	VHD_UINT8 = 0, VHD_UINT16
} vhd_cfg_type_t;

typedef struct
{
	const char* name;
	void* var;
	vhd_cfg_type_t type;
	uint16_t min;
	uint16_t max;
} vhd_var_t;

#define CFG_LINE_SIZE           64
#define CHAR_IS_LINEEND(c)      (((c) == '\n'))

fileTYPE cfg_file;

int cfg_pt = 0;
static char cfg_getch()
{
	static uint8_t buf[512];
	if (!(cfg_pt & 0x1ff)) FileReadSec(&cfg_file, buf);
	if (cfg_pt >= cfg_file.size) return 0;
	return buf[(cfg_pt++) & 0x1ff];
}

static int cfg_getline(char* line)
{
	char c, ignore = 0, skip = 1;
	int i = 0;

	while ((c = cfg_getch()))
	{
		if (!std::isspace(c)) skip = 0;
		if (i >= (CFG_LINE_SIZE - 1)) ignore = 1;

		if (CHAR_IS_LINEEND(c)) break;
		if ((std::isspace(c) || std::isalnum(c)) && !ignore && !skip) line[i++] = c;
	}
	line[i] = 0;
	while (i > 0 && std::isspace(line[i - 1])) line[--i] = 0;
	return c == 0;
}

static vhd_error ini_parse_numeric(const vhd_var_t *var, const char *text, void *out)
{
	uint32_t u32 = 0;
	char *endptr = nullptr;

	bool out_of_range = true;
	bool invalid_format = false;

	u32 = strtoul(text, &endptr, 0);
	if (u32 < var->min) u32 = var->min;
	else if (u32 > var->max) u32 = var->max;
	else out_of_range = false;

	if (*endptr) 
	{
		printf("VHD CFG ERROR: %s: \'%s\' not a number\n", var->name, text);
		return VHD_INVALID_CONFIG;
	}
	else if (out_of_range) 
	{
		printf("VHD CFG ERROR: %s: \'%s\' out of range\n", var->name, text);
		return VHD_INVALID_CONFIG;
	}
	else if (invalid_format) 
	{
		printf("VHD CFG ERROR: %s: \'%s\' invalid format\n", var->name, text);
		return VHD_INVALID_CONFIG;
	}
	
	switch (var->type)
	{
	case VHD_UINT8: *(uint8_t*)out = u32; break;
	case VHD_UINT16: *(uint16_t*)out = u32; break;
	default: break;
	}
	return VHD_NOERROR;
}

static vhd_error ini_parse_var(char* buf, vhd_var_t vars[], size_t var_count)
{
	// find var
	int i = 0;
	while (1)
	{
		if (buf[i] == '=' || std::isspace(buf[i]))
		{
			buf[i] = 0;
			break;
		}
		else if (!buf[i]) return VHD_NOERROR;
		i++;
	}

	// parse var
	int var_id = -1;
	for (size_t j = 0; j < var_count; j++)
	{
		if (!strcasecmp(buf, vars[j].name)) var_id = j;
	}

	if (var_id == -1)
	{
		printf("VHD CFG ERROR: %s: unknown option\n", buf);
		return VHD_INVALID_CONFIG;
	}
	else // get data
	{
		i++;
		while (std::isspace(buf[i])) i++;

		const vhd_var_t *var = &vars[var_id];
		return ini_parse_numeric(var, &buf[i], var->var);
	}
}

static vhd_error cfg_parse(const char *name, vhd_var_t vars[], size_t var_count) 
{
	char line[CFG_LINE_SIZE];
	int eof;
	vhd_error res = VHD_NOERROR;

	memset(line, 0, sizeof(line));
	memset(&cfg_file, 0, sizeof(cfg_file));

	if (!FileOpen(&cfg_file, name))	return VHD_NO_CONFIG;

	cfg_pt = 0;

	// parse ini
	while (1)
	{
		// get line
		eof = cfg_getline(line);
		res = ini_parse_var(line, vars, var_count);
		if (res != VHD_NOERROR)
		{
			return res;
		}

		// if end of file, stop
		if (eof) break;
	}

	FileClose(&cfg_file);

	return res;
}

// make sure the values make sense
vhd_error validate(drive_t *drive, uint8_t sectors, uint8_t heads, uint16_t cylinders)
{
	// Both sectors and heads must be defined
	if (!sectors || !heads) return VHD_MISSING_CONFIG;

	__off64_t config_size = heads * sectors * 512;
	if (cylinders == 0 && drive->f->size < config_size)
	{
		return VHD_INVALID_SIZE;
	}
	else if (cylinders == 0) return VHD_NOERROR;

	config_size = config_size * cylinders;
	if (drive->f->size < config_size) return VHD_INVALID_SIZE;

	if (drive->f->size != config_size) 
	{
		printf("config override: new size %lli\n", config_size);
		drive->total_sectors = config_size / 512;
	}
	return VHD_NOERROR;
}

// parse_vhd_config will take a filename and replace the extension with `.cfg` then attempt to parse
// CHS values into a provided `vhd_config_t`. 
vhd_error parse_vhd_config(drive_t *drive) 
{
	const char *name = drive->f->path;
	struct
	{
		uint8_t sectors;
		uint8_t heads;
		uint16_t cylinders;
	} parsed_values;

	// vhd_var_t defines the possible values that can be specified for a given image.
	// If this file exists, both `sectors` and `heads` must be defined for it to parse
	// successfully. Cylinders may also be specified if the entire file shouldn't be
	// used, i.e. -- avoid overwriting a footer.
	vhd_var_t vhd_vars[] =
	{
		{ "SECTORS", (void*)(&(parsed_values.sectors)), VHD_UINT8, 17, 256 },
		{ "HEADS", (void*)(&(parsed_values.heads)), VHD_UINT8, 2, 16 },
		{ "CYLINDERS", (void*)(&(parsed_values.cylinders)), VHD_UINT16, 0, 65535 }
	};

	char cfg_name[256];

	const char *ext = strrchr(name, '.');
	if (!ext || ext == name || ext == name + strlen(name) - 1)
	{
		return VHD_NO_CONFIG;
	}

	// find the length of the name before the extension
	size_t name_len = ext - name;
	strncpy(cfg_name, name, name_len);
	strncpy(cfg_name+name_len, ".cfg", 4);

	vhd_error result = cfg_parse(cfg_name, vhd_vars, sizeof(vhd_vars)/sizeof(vhd_var_t));
	if (result == VHD_NOERROR)
	{
		result = validate(drive, parsed_values.sectors, parsed_values.heads, parsed_values.cylinders);
		if (result == VHD_NOERROR)
		{
			printf("config override: cylinders %d heads %d sectors %d\n", parsed_values.cylinders, parsed_values.heads, parsed_values.sectors);
			drive->cylinders=parsed_values.cylinders;
			drive->heads=parsed_values.heads;
			drive->spt=parsed_values.sectors;
		}
	}
	return result;
}
