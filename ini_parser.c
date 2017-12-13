// ini_parser.c
// 2015, rok.krajnc@gmail.com


//// includes ////
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include "ini_parser.h"
#include "debug.h"
#include "file_io.h"
#include "user_io.h"

//// defines ////
#define INI_EOT                 4 // End-Of-Transmission

#define INI_BUF_SIZE            512
#define INI_LINE_SIZE           256

#define INI_SECTION_START       '['
#define INI_SECTION_END         ']'
#define INI_SECTION_INVALID_ID  0


//// macros ////
#define CHAR_IS_NUM(c)          (((c) >= '0') && ((c) <= '9'))
#define CHAR_IS_ALPHA_LOWER(c)  (((c) >= 'a') && ((c) <= 'z'))
#define CHAR_IS_ALPHA_UPPER(c)  (((c) >= 'A') && ((c) <= 'Z'))
#define CHAR_IS_ALPHA(c)        (CHAR_IS_ALPHA_LOWER(c) || CHAR_IS_ALPHA_UPPER(c))
#define CHAR_IS_ALPHANUM(c)     (CHAR_IS_ALPHA_LOWER(c) || CHAR_IS_ALPHA_UPPER(c) || CHAR_IS_NUM(c))
#define CHAR_IS_SPECIAL(c)      (((c) == '[') || ((c) == ']') || ((c) == '-') || ((c) == '_') || ((c) == ',') || ((c) == '='))
#define CHAR_IS_VALID(c)        (CHAR_IS_ALPHANUM(c) || CHAR_IS_SPECIAL(c))
#define CHAR_IS_WHITESPACE(c)   (((c) == ' ') || ((c) == '\t') || ((c) == '\r') || ((c) == '\n'))
#define CHAR_IS_SPACE(c)        (((c) == ' ') || ((c) == '\t'))
#define CHAR_IS_LINEEND(c)      (((c) == '\n'))
#define CHAR_IS_COMMENT(c)      (((c) == ';'))
#define CHAR_IS_QUOTE(c)        (((c) == '"'))


fileTYPE ini_file;
static uint8_t buf[512];

int ini_pt = 0;

//// ini_getch() ////
char ini_getch()
{
	if ((ini_pt & 0x3ff) == 0x200)
	{
		// reload buffer
		FileReadSec(&ini_file, buf);
	}

	if (ini_pt >= ini_file.size) return 0;
		else return buf[(ini_pt++) & 0x1ff];
}

//// ini_putch() ////
int ini_putch(char c)
{
	static int ini_pt = 0;

	buf[ini_pt++] = c;

	if ((ini_pt % 0x3ff) == 0x200)
	{
		// write buffer
		ini_pt = 0;
	}
	return ini_pt;
}


//// ini_findch() ////
char ini_findch(char c)
{
	char t;
	do {
		t = ini_getch();
	} while ((t != 0) && (t != c));
	return t;
}


//// ini_getline() ////
int ini_getline(char* line)
{
	char c;
	char ignore = 0;
	char literal = 0;
	int i = 0;

	while (i<(INI_LINE_SIZE - 1)) {
		c = ini_getch();
		if ((!c) || CHAR_IS_LINEEND(c)) break;
		else if (CHAR_IS_QUOTE(c)) literal ^= 1;
		else if (CHAR_IS_COMMENT(c) && !ignore && !literal) ignore++;
		else if (literal) line[i++] = c;
		else if (CHAR_IS_VALID(c) && !ignore) line[i++] = c;
	}
	line[i] = '\0';
	return c == 0 ? INI_EOT : literal ? 1 : 0;
}


//// ini_putline() ////
int ini_putline(char* line)
{
	int ini_pt, i = 0;

	while (i<(INI_LINE_SIZE - 1)) {
		if (!line[i]) break;
		ini_pt = ini_putch(line[i++]);
	}
	return ini_pt;
}

//// ini_get_section() ////
int ini_get_section(const ini_cfg_t* cfg, char* buf)
{
	int i = 0;

	// get section start marker
	if (buf[0] != INI_SECTION_START) {
		return INI_SECTION_INVALID_ID;
	}
	else buf++;

	// get section stop marker
	while (1) {
		if (buf[i] == INI_SECTION_END) {
			buf[i] = '\0';
			break;
		}
		i++;
		if (i >= INI_LINE_SIZE) {
			return INI_SECTION_INVALID_ID;
		}
	}

	// convert to uppercase
	for (i = 0; i<INI_LINE_SIZE; i++) {
		if (!buf[i]) break;
		else buf[i] = toupper(buf[i]);
	}

	// parse section
	for (i = 0; i<cfg->nsections; i++) {
		if (!strcasecmp(buf, cfg->sections[i].name)) {
			ini_parser_debugf("Got SECTION '%s' with ID %d", buf, cfg->sections[i].id);
			return cfg->sections[i].id;
		}
	}

	if (!strcasecmp(buf, user_io_get_core_name_ex())) return cfg->sections[0].id;

	return INI_SECTION_INVALID_ID;
}


//// ini_get_var() ////
void* ini_get_var(const ini_cfg_t* cfg, int cur_section, char* buf)
{
	int i = 0, j = 0;
	int var_id = -1;

	// find var
	while (1) {
		if (buf[i] == '=') {
			buf[i] = '\0';
			break;
		}
		else if (buf[i] == '\0') return (void*)0;
		i++;
	}

	// convert to uppercase
	for (j = 0; j <= i; j++) {
		if (!buf[j]) break;
		else buf[j] = toupper(buf[j]);
	}

	// parse var
	for (j = 0; j<cfg->nvars; j++) {
		if ((!strcasecmp(buf, cfg->vars[j].name)) && (cfg->vars[j].section_id == cur_section)) var_id = j;
	}

	// get data
	if (var_id != -1) {
		ini_parser_debugf("Got VAR '%s' with VALUE %s", buf, &(buf[i + 1]));
		i++;
		switch (cfg->vars[var_id].type) {
		case UINT8:
			*(uint8_t*)(cfg->vars[var_id].var) = strtoul(&(buf[i]), NULL, 0);
			if (*(uint8_t*)(cfg->vars[var_id].var) > cfg->vars[var_id].max) *(uint8_t*)(cfg->vars[var_id].var) = cfg->vars[var_id].max;
			if (*(uint8_t*)(cfg->vars[var_id].var) < cfg->vars[var_id].min) *(uint8_t*)(cfg->vars[var_id].var) = cfg->vars[var_id].min;
			break;
		case INT8:
			*(int8_t*)(cfg->vars[var_id].var) = strtol(&(buf[i]), NULL, 0);
			if (*(int8_t*)(cfg->vars[var_id].var) > cfg->vars[var_id].max) *(int8_t*)(cfg->vars[var_id].var) = cfg->vars[var_id].max;
			if (*(int8_t*)(cfg->vars[var_id].var) < cfg->vars[var_id].min) *(int8_t*)(cfg->vars[var_id].var) = cfg->vars[var_id].min;
			break;
		case UINT16:
			*(uint16_t*)(cfg->vars[var_id].var) = strtoul(&(buf[i]), NULL, 0);
			if (*(uint16_t*)(cfg->vars[var_id].var) > cfg->vars[var_id].max) *(uint16_t*)(cfg->vars[var_id].var) = cfg->vars[var_id].max;
			if (*(uint16_t*)(cfg->vars[var_id].var) < cfg->vars[var_id].min) *(uint16_t*)(cfg->vars[var_id].var) = cfg->vars[var_id].min;
			break;
		case INT16:
			*(int16_t*)(cfg->vars[var_id].var) = strtol(&(buf[i]), NULL, 0);
			if (*(int16_t*)(cfg->vars[var_id].var) > cfg->vars[var_id].max) *(int16_t*)(cfg->vars[var_id].var) = cfg->vars[var_id].max;
			if (*(int16_t*)(cfg->vars[var_id].var) < cfg->vars[var_id].min) *(int16_t*)(cfg->vars[var_id].var) = cfg->vars[var_id].min;
			break;
		case UINT32:
			*(uint32_t*)(cfg->vars[var_id].var) = strtoul(&(buf[i]), NULL, 0);
			if (*(uint32_t*)(cfg->vars[var_id].var) > (uint32_t)cfg->vars[var_id].max) *(uint32_t*)(cfg->vars[var_id].var) = cfg->vars[var_id].max;
			if (*(uint32_t*)(cfg->vars[var_id].var) < (uint32_t)cfg->vars[var_id].min) *(uint32_t*)(cfg->vars[var_id].var) = cfg->vars[var_id].min;
			break;
		case INT32:
			*(int32_t*)(cfg->vars[var_id].var) = strtol(&(buf[i]), NULL, 0);
			if (*(int32_t*)(cfg->vars[var_id].var) > cfg->vars[var_id].max) *(int32_t*)(cfg->vars[var_id].var) = cfg->vars[var_id].max;
			if (*(int32_t*)(cfg->vars[var_id].var) < cfg->vars[var_id].min) *(int32_t*)(cfg->vars[var_id].var) = cfg->vars[var_id].min;
			break;
		case FLOAT:
			*(float*)(cfg->vars[var_id].var) = strtof(&(buf[i]), NULL);
			if (*(float*)(cfg->vars[var_id].var) > cfg->vars[var_id].max) *(float*)(cfg->vars[var_id].var) = cfg->vars[var_id].max;
			if (*(float*)(cfg->vars[var_id].var) < cfg->vars[var_id].min) *(float*)(cfg->vars[var_id].var) = cfg->vars[var_id].min;
			break;
		case STRING:
			memset(cfg->vars[var_id].var, 0, cfg->vars[var_id].max);
			strncpy((char*)(cfg->vars[var_id].var), &(buf[i]), cfg->vars[var_id].max);
			break;
		case CUSTOM_HANDLER:
			((custom_handler_t*)(cfg->vars[var_id].var))(&(buf[i]));
			break;
		}
		return (void*)(&(cfg->vars[var_id].var));
	}

	return (void*)0;
}


//// ini_parse() ////
void ini_parse(const ini_cfg_t* cfg)
{
	char line[INI_LINE_SIZE] = { 0 };
	int section = INI_SECTION_INVALID_ID;
	int line_status;

	ini_parser_debugf("Start INI parser for core \"%s\".", user_io_get_core_name_ex());

	memset(&ini_file, 0, sizeof(ini_file));
	if (!FileOpen(&ini_file, cfg->filename))
	{
		ini_parser_debugf("Can't open file %s !", cfg->filename);
		return;
	}

	ini_parser_debugf("Opened file %s with size %d bytes.", cfg->filename, ini_file.size);

	ini_pt = 0;

	// preload buffer
	FileReadSec(&ini_file, buf);

	// parse ini
	while (1)
	{
		// get line
		line_status = ini_getline(line);
		ini_parser_debugf("line(%d): \"%s\".", line_status, line);
		// if valid line
		if (line_status != 1)
		{
			if (line[0] == INI_SECTION_START)
			{
				// if first char in line is INI_SECTION_START, get section
				section = ini_get_section(cfg, line);
			}
			else
			{
				// otherwise this is a variable, get it
				ini_get_var(cfg, section, line);
			}
		}
		// if end of file, stop
		if (line_status == INI_EOT) break;
	}

	FileClose(&ini_file);
}

//// ini_save() ////
void ini_save(const ini_cfg_t* cfg)
{
	// Not fully implemented yet.

	/*
	int section, var, ini_pt;
	char line[INI_LINE_SIZE] = { 0 };

	// open ini file
	ini_parser_debugf("Can't open file %s !", cfg->filename);
	return;

	// loop over sections
	for (section = 0; section<cfg->nsections; section++) {
		ini_parser_debugf("writing section %s ...", cfg->sections[section].name);
		siprintf(line, "[%s]\n", cfg->sections[section].name);
		ini_pt = ini_putline(line);
		// loop over vars
		for (var = 0; var<cfg->nvars; var++) {
			if (cfg->vars[var].section_id == cfg->sections[section].id) {
				ini_parser_debugf("writing var %s", cfg->vars[var].name);
				switch (cfg->vars[var].type) {
				case UINT8:
				case UINT16:
				case UINT32:
					siprintf(line, "%s=%u\n", cfg->vars[var].name, *(uint32_t*)(cfg->vars[var].var));
					break;
				case INT8:
				case INT16:
				case INT32:
					siprintf(line, "%s=%d\n", cfg->vars[var].name, *(int32_t*)(cfg->vars[var].var));
					break;
				case FLOAT:
					siprintf(line, "%s=%f\n", cfg->vars[var].name, *(float*)(cfg->vars[var].var));
					break;
				case STRING:
					siprintf(line, "%s=\"%s\"\n", cfg->vars[var].name, (char*)(cfg->vars[var].var));
					break;
				}
				ini_pt = ini_putline(line);
			}
		}
	}

	// in case the buffer is not written yet, write it now
	if (ini_pt)
	{
		//fwrite(buf, sizeof(char), ini_pt, ini_fp);
	}
	*/
}
