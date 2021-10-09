// bootcore.cpp
// 2019, Aitor Gomez Garcia (spark2k06@gmail.com)
// Thanks to Sorgelig and BBond007 for their help and advice in the development of this feature.

#include "file_io.h"
#include "cfg.h"
#include "fpga_io.h"
#include "hardware.h"
#include "support/arcade/mra_loader.h"
#include "bootcore.h"
#include "user_io.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

enum bootcoreType
{
	BOOTCORE_NONE,
	BOOTCORE_LASTCORE,
	BOOTCORE_LASTCORE_EXACT,
	BOOTCORE_LASTGAME,
	BOOTCORE_LASTGAME_EXACT,
	BOOTCORE_CORENAME,
	BOOTCORE_CORENAME_EXACT
};

static bool isExact(bootcoreType t)
{
	switch( t )
	{
		case BOOTCORE_LASTCORE_EXACT:
		case BOOTCORE_LASTGAME_EXACT:
		case BOOTCORE_CORENAME_EXACT:
			return true;
		default:
			return false;
	}
}

static bool isLastCore(bootcoreType t)
{
	switch( t )
	{
		case BOOTCORE_LASTCORE_EXACT:
		case BOOTCORE_LASTGAME_EXACT:
		case BOOTCORE_LASTGAME:
		case BOOTCORE_LASTCORE:
			return true;
		default:
			return false;
	}
}

static bool isLastGame(bootcoreType t)
{
	switch( t )
	{
		case BOOTCORE_LASTGAME_EXACT:
		case BOOTCORE_LASTGAME:
			return true;
		default:
			return false;
	}
}


typedef struct
{
	uint8_t version;
	char core_name[32];
	char core_path[256];
	char game_path[256];
} lastcoreSave_t;

#define LASTCORE_VERSION 1
static lastcoreSave_t lastcore_save;

static char rbf_name[256];
static char core_path[256];
bootcoreType launch_type = BOOTCORE_NONE;
static unsigned long launch_time;
static bool launch_pending = false;

void makeRBFName(char *str)
{
	char *p = strrchr(str, '/');
	if (!p) return;

	char *spl = strrchr(p + 1, '.');
	if (spl && (!strcmp(spl, ".rbf") || !strcmp(spl, ".mra")))
	{
		*spl = 0;
	}

	memmove(str, p + 1, strlen(p + 1) + 1);
}


bool isExactcoreName(char *path)
{
	char *spl = strrchr(path, '.');
	return (spl && (!strcmp(spl, ".rbf") || !strcmp(spl, ".mra")));
}

void makeCoreName(char *path)
{
	char *orig = path;
	char *spl = strrchr(path, '.');
	if (spl && !strcmp(spl, ".rbf"))
	{
		*spl = '\0';
	}
	else
	{
		*path = '\0';
		return;
	}

	if ((spl = strrchr(path, '/')) != NULL)
	{
		path = spl + 1;
	}

	if ((spl = strrchr(path, '_')) != NULL)
	{
		*spl = 0;
	}

	if( orig != path )
	{
		memmove(orig, path, strlen(path) + 1);
	}
}

void makeExactCoreName(char *path)
{
	char *spl;
	if ((spl = strrchr(path, '/')) != NULL)
	{
		memmove(path, spl + 1, strlen(spl + 1) + 1);
	}
}


char *findCore(const char *name, char *coreName, int indent)
{
	char *spl;
	DIR *dir;
	struct dirent *entry;

	if (!(dir = opendir(name)))
	{
		return NULL;
	}

	char *indir;
	char* path = new char[256];
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_DIR) {
			if (entry->d_name[0] != '_')
				continue;
			snprintf(path, 256, "%s/%s", name, entry->d_name);
			indir = findCore(path, coreName, indent + 2);
			if (indir != NULL)
			{
				closedir(dir);
				delete[] path;
				return indir;
			}
		}
		else {
			snprintf(path, 256, "%s/%s", name, entry->d_name);
			if (strstr(path, coreName) != NULL) {
				spl = strrchr(path, '.');
				if (spl && (!strcmp(spl, ".rbf") || !strcmp(spl, ".mra")))
				{
					closedir(dir);
					return path;
				}
			}
		}
	}
	closedir(dir);
	delete[] path;
	return NULL;
}

void bootcore_init(const char *path)
{
	char bootcore[256];
	int len = FileLoadConfig("lastcore.dat", &lastcore_save, sizeof(lastcore_save));

	if( len != sizeof(lastcore_save) || lastcore_save.version != LASTCORE_VERSION )
	{
		memset( &lastcore_save, 0, sizeof( lastcore_save ) );
		lastcore_save.version = LASTCORE_VERSION;
	}

	launch_pending = false;

	// determine type
	if( !strcmp( cfg.bootcore, "lastcore" ) )
	{
		launch_type = BOOTCORE_LASTCORE;
		strcpy(bootcore, lastcore_save.core_path);
	}
	else if( !strcmp( cfg.bootcore, "lastexactcore" ) )
	{
		launch_type = BOOTCORE_LASTCORE_EXACT;
		strcpy(bootcore, lastcore_save.core_path);
	}
	else if( !strcmp( cfg.bootcore, "lastgame" ) )
	{
		launch_type = BOOTCORE_LASTGAME;
		strcpy(bootcore, lastcore_save.core_path);
	}
	else if( !strcmp( cfg.bootcore, "lastexactgame" ) )
	{
		launch_type = BOOTCORE_LASTGAME_EXACT;
		strcpy(bootcore, lastcore_save.core_path);
	}
	else if( isExactcoreName(cfg.bootcore) )
	{
		launch_type = BOOTCORE_CORENAME_EXACT;
		strcpy(bootcore, cfg.bootcore);
	}
	else
	{
		launch_type = BOOTCORE_CORENAME;
		strcpy(bootcore, cfg.bootcore);
	}

	// if we are booting a core
	if( path[0] != '\0' )
	{
		if( !is_menu() && isLastCore( launch_type ) )
		{
			if( strcmp(path, bootcore) )
			{
				strcpy(lastcore_save.core_path, path);
				
				// Clear game path if the corename differs
				if( strcmp( lastcore_save.core_name, CoreName ) )
				{
					strcpy( lastcore_save.core_name, CoreName );
					lastcore_save.game_path[0] = '\0';
				}
				FileSaveConfig("lastcore.dat", &lastcore_save, sizeof(lastcore_save));
			}
			

		}
		return;
	}

	// clean up name
	if( isExact( launch_type ) || isMraName(bootcore) )
	{
		makeExactCoreName(bootcore);
	}
	else
	{	
		makeCoreName(bootcore);
	}

	// no valid bootcore
	if( bootcore[0] == '\0' )
	{
		return;
	}

	// find the core
	char *found_path = findCore(getRootDir(), bootcore, 0);
	if (found_path == NULL)
	{
		return;
	}

	char rootDir[256];
	sprintf(rootDir, "%s/", getRootDir());
	if( strncasecmp( found_path, rootDir, strlen(rootDir) ))
	{
		strcpy(core_path, found_path);
	}
	else
	{
		strcpy(core_path, found_path + strlen(rootDir));
	}
	delete[] found_path;

	strcpy(rbf_name, core_path);
	makeRBFName(rbf_name);

	if( cfg.bootcore_timeout )
	{
		launch_time = GetTimer(cfg.bootcore_timeout * 1000UL);
		launch_pending = true;
	}
	else
	{
		launch_time = 0;
		bootcore_launch();
	}
}

void bootcore_record_file(const char *path)
{
	if( !isLastGame(launch_type) )
	{
		return;
	}

	if( strcmp( CoreName, lastcore_save.core_name ) )
	{
		return;
	}

	printf( "Bootcore: recorded %s for %s\n", path, lastcore_save.core_name );
	strncpy(lastcore_save.game_path, path, sizeof(lastcore_save.game_path));
	FileSaveConfig("lastcore.dat", &lastcore_save, sizeof(lastcore_save));
}

void bootcore_launch()
{
	launch_pending = false;
	if( isMraName(core_path) )
	{
		arcade_load(getFullPath(core_path));
	}
	else
	{
		fpga_load_rbf(core_path);
	}
}

void bootcore_load_file()
{
	if( !isLastGame( launch_type ) )
	{
		return;
	}

	if( strcmp( CoreName, lastcore_save.core_name ) )
	{
		return;
	}

	if( lastcore_save.game_path[0] )
	{
		user_io_load_or_mount( lastcore_save.game_path );
	}
}

void bootcore_cancel()
{
	launch_pending = false;
	if( isLastCore( launch_type ) )
	{
		memset( &lastcore_save, 0, sizeof(lastcore_save) );
		FileSaveConfig("lastcore.dat", &lastcore_save, sizeof(lastcore_save));
	}
}

bool bootcore_pending()
{
	return launch_pending;
}

bool bootcore_ready()
{
	return CheckTimer(launch_time);
}

unsigned int bootcore_delay()
{
	return cfg.bootcore_timeout * 1000;
}

unsigned int bootcore_remaining()
{
	unsigned long curtime = GetTimer(0);
	if( curtime >= launch_time )
	{
		return 0;
	}
	else
	{
		return ( launch_time - curtime );
	}
}

const char *bootcore_type()
{
	switch( launch_type )
	{
		case BOOTCORE_LASTCORE: return "lastcore";
		case BOOTCORE_LASTCORE_EXACT: return "lastcore (exact)";
		case BOOTCORE_CORENAME_EXACT: return "corename (exact)";
		case BOOTCORE_CORENAME: return "corename";
		case BOOTCORE_LASTGAME: return "lastgame";
		case BOOTCORE_LASTGAME_EXACT: return "lastgame (exact)";
		default: break;
	}
	return "none";
}

const char *bootcore_name()
{
	return rbf_name;
}

