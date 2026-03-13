// bootcore.cpp
// 2024, Aitor Gomez Garcia (info@aitorgomez.net)
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
	return (spl && (!strcmp(spl, ".rbf") || !strcmp(spl, ".mra") || !strcmp(spl, ".mgl")));
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

// Tests whether a filename is a dated release of a given core name.
//
// Parameters:
//   A - Generic core name (without date or extension), e.g. "NES"
//   B - Filename to test, e.g. "NES_20240115.rbf"
//
// Returns:
//   true if and only if B exactly matches the pattern
//     "<A>_YYYYMMDD.rbf"
//   where YYYYMMDD consists of 8 decimal digits.
//   Returns false otherwise, including on NULL inputs.
bool matchesCore_yyyyMMdd_rbf(const char *A, const char *B)
{

	static const char *ext = ".rbf";

	if (!A || !B)
		return false;

	size_t a_len = strlen(A);
	size_t b_len = strlen(B);
	size_t ext_len = strlen(ext);

	// A + '_' + 8 digits + ".rbf"
	if (b_len != a_len + 1 + 8 + ext_len)
		return false;

	// Exact A prefix
	if (strncmp(B, A, a_len) != 0)
		return false;

	// Underscore
	if (B[a_len] != '_')
		return false;

	// Extension
	if (strcmp(B + b_len - ext_len, ext) != 0)
		return false;

	// 8 digits YYYYMMDD
	const char *digits = B + a_len + 1;
	for (int i = 0; i < 8; i++)
	{
		if (digits[i] < '0' || digits[i] > '9')
			return false;
	}

	return true;
}


struct CoreMatch
{
	char *path;
	int date;
	bool exact;

	CoreMatch() : path(NULL), date(0), exact(false) {}
};

static void update_core_best(CoreMatch &best, CoreMatch &candidate)
{
	if (!candidate.path)
		return;

	if (candidate.exact)
	{
		if (best.path)
			delete[] best.path;
		best = candidate;
		return;
	}

	if (!best.exact && candidate.date > best.date)
	{
		if (best.path)
			delete[] best.path;
		best = candidate;
		return;
	}

	delete[] candidate.path;
}

static CoreMatch findCore(const char *name, const char *coreName)
{
	CoreMatch best;
	DIR *dir;
	struct dirent *entry;

	if (!(dir = opendir(name)))
		return best;

	char path[256];

	while ((entry = readdir(dir)) != NULL)
	{
		if (entry->d_type == DT_DIR)
		{
			if (entry->d_name[0] != '_')
				continue;

			snprintf(path, sizeof(path), "%s/%s", name, entry->d_name);
			CoreMatch sub = findCore(path, coreName);
			update_core_best(best, sub);
			if (best.exact)
				break;
		}
		else
		{
			// Exact filename match (any extension)
			if (!strcmp(coreName, entry->d_name))
			{
				CoreMatch exact;
				exact.exact = true;
				exact.path = new char[256];
				snprintf(exact.path, 256, "%s/%s", name, entry->d_name);
				update_core_best(best, exact);
				break;
			}

			// Dated generic match: <core>_YYYYMMDD.rbf
			if (matchesCore_yyyyMMdd_rbf(coreName, entry->d_name))
			{
				CoreMatch dated;
				dated.exact = false;
				dated.date = atoi(entry->d_name + strlen(coreName) + 1);
				dated.path = new char[256];
				snprintf(dated.path, 256, "%s/%s", name, entry->d_name);
				update_core_best(best, dated);
			}
		}
	}

	closedir(dir);
	return best;
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

	CoreMatch best_core_match = findCore(rootdir, bootcore);
	auxpointer = best_core_match.path;

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

