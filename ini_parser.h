// ini_parser.h
// 2015, rok.krajnc@gmail.com

#ifndef __INI_PARSER_H__
#define __INI_PARSER_H__

//// includes ////
#include <inttypes.h>


//// type definitions ////
typedef struct {
	int id;
	const char* name;
} ini_section_t;

typedef enum {
	UINT8 = 0, INT8, UINT16, INT16, UINT32, INT32, FLOAT,
	STRING, CUSTOM_HANDLER
} ini_vartypes_t;

typedef void custom_handler_t(char*);

typedef struct {
	const char* name;
	void* var;
	ini_vartypes_t type;
	int min;
	int max;
} ini_var_t;

typedef struct {
	const char* filename;
	const char* filename_alt;
	const ini_section_t* sections;
	const ini_var_t* vars;
	int nsections;
	int nvars;
} ini_cfg_t;


//// functions ////
void ini_parse(const ini_cfg_t* cfg, int alt);

#endif // __INI_PARSER_H__

