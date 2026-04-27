#include <stdio.h>
#include <string.h>

#include "game_docs.h"
#include "file_io.h"
#include "user_io.h"
#include "support.h"

static char manual_path[1024] = {};

void game_docs_init(const char *rom_path, uint32_t romcrc)
{
	manual_path[0] = 0;

	char manuals_dir[1024];
	snprintf(manuals_dir, sizeof(manuals_dir), "%s", user_io_get_core_name2());
	findDocsDir(manuals_dir, sizeof(manuals_dir));
	strcat(manuals_dir, "/Manuals");

	const char *pcecd_dir = NULL;
	char pcecd_manuals_dir[1024];
	if (pcecd_using_cd())
	{
		snprintf(pcecd_manuals_dir, sizeof(pcecd_manuals_dir), "%sCD", user_io_get_core_name2());
		findDocsDir(pcecd_manuals_dir, sizeof(pcecd_manuals_dir));
		strcat(pcecd_manuals_dir, "/Manuals");
		pcecd_dir = pcecd_manuals_dir;
	}

	if (findGameAsset(manual_path, sizeof(manual_path), rom_path, romcrc, ".pdf", manuals_dir, pcecd_dir, NULL))
	{
		printf("Using manual file: %s\n", manual_path);
	} else {
		manual_path[0] = 0;
		printf("No manual file found.\n");
	}
}

int game_docs_manual_available()
{
	return manual_path[0] != 0;
}

const char *game_docs_get_manual()
{
	return manual_path;
}
