// bootcore.cpp
// 2019, Aitor Gomez Garcia (spark2k06@gmail.com)
// Thanks to Sorgelig and BBond007 for their help and advice in the development of this feature.

#include "file_io.h"
#include "cfg.h"
#include "fpga_io.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>


extern int xml_load(const char *xml);
int16_t btimeout;
char bootcoretype[64];

bool isExactcoreName(char *path)
{
	char *spl = strrchr(path, '.');
	return (spl && (!strcmp(spl, ".rbf") || !strcmp(spl, ".mra")));
}

char *getcoreName(char *path)
{
	char *spl = strrchr(path, '.');
	if (spl && !strcmp(spl, ".rbf"))
	{
		*spl = '\0';
	}
	else
	{
		return NULL;
	}
	if ((spl = strrchr(path, '/')) != NULL)
	{
		path = spl + 1;
	}
	if ((spl = strrchr(path, '_')) != NULL)
	{
		*spl = 0;
	}

	return path;
}

char *getcoreExactName(char *path)
{
	char *spl;
	if ((spl = strrchr(path, '/')) != NULL)
	{
		path = spl + 1;
	}

	return path;
}

char *replaceStr(const char *str, const char *oldstr, const char *newstr)
{
	char *result;
	int i, cnt = 0;
	int newstrlen = strlen(newstr);
	int oldstrlen = strlen(oldstr);

	for (i = 0; str[i] != '\0'; i++)
	{
		if (strstr(&str[i], oldstr) == &str[i])
		{
			cnt++;
			i += oldstrlen - 1;
		}
	}

	result = new char[i + cnt * (newstrlen - oldstrlen) + 1];

	i = 0;
	while (*str)
	{
		if (strstr(str, oldstr) == str)
		{
			strcpy(&result[i], newstr);
			i += newstrlen;
			str += oldstrlen;
		}
		else
			result[i++] = *str++;
	}

	result[i] = '\0';
	return result;
}

char* loadLastcore()
{
	char full_path[2100];
	char path[256] = { CONFIG_DIR"/" };
	strcat(path, "lastcore.dat");
	sprintf(full_path, "%s/%s", getRootDir(), path);
	FILE *fd = fopen(full_path, "r");
	if (!fd)
	{
		return NULL;
	}
	fseek(fd, 0L, SEEK_END);
	long size = ftell(fd);

	fseek(fd, 0L, SEEK_SET);
	char *lastcore = new char[size + 1]();
	int ret = fread(lastcore, sizeof(char), size, fd);
	fclose(fd);
	if (ret == size)
	{
		return lastcore;
	}
	delete[] lastcore;
	return NULL;

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
	char *auxpointer;
	char auxstr[256];
	char bootcore[256];
	bool is_lastcore;
	const char *rootdir = getRootDir();
	cfg.bootcore_timeout = cfg.bootcore_timeout * 10;
	btimeout = cfg.bootcore_timeout;
	strcpy(bootcore, cfg.bootcore);

	is_lastcore = (!strcmp(cfg.bootcore, "lastcore") || !strcmp(cfg.bootcore, "lastexactcore"));

	if (is_lastcore)
	{
		strcpy(bootcoretype, cfg.bootcore);
		auxpointer = loadLastcore();
		if (auxpointer != NULL)
		{
			strcpy(bootcore, auxpointer);
			delete[] auxpointer;
		}
	}
	else
	{
		strcpy(bootcoretype, isExactcoreName(cfg.bootcore) ? "exactcorename" : "corename");
	}

	auxpointer = findCore(rootdir, bootcore, 0);
	if (auxpointer != NULL)
	{
		strcpy(bootcore, auxpointer);
		delete[] auxpointer;

		sprintf(auxstr, "%s/", rootdir);
		auxpointer = replaceStr(bootcore, auxstr, "");
		if (auxpointer != NULL)
		{
			strcpy(bootcore, auxpointer);
			delete[] auxpointer;

			if (path[0] == '\0')
			{
				if (!cfg.bootcore_timeout)
				{
					isXmlName(bootcore) ? xml_load(bootcore) : fpga_load_rbf(bootcore);
				}

				strcpy(cfg.bootcore, strcmp(bootcore, "menu.rbf") ? bootcore : "");
				return;
			}
		}
	}

	if (is_lastcore && path[0] != '\0')
	{

		strcpy(auxstr, path);
		auxpointer = (!strcmp(cfg.bootcore, "lastexactcore") || isXmlName(auxstr)) ? getcoreExactName(auxstr) : getcoreName(auxstr);

		if (auxpointer != NULL)
		{
			if (strcmp(bootcore, auxpointer))
			{
				FileSaveConfig("lastcore.dat", (char*)auxpointer, strlen(auxpointer));
			}
		}
	}
	strcpy(cfg.bootcore, "");

}

