#include <dirent.h>

static void FindLatestCore(const char* core_name, char* out_path) {
	const char* paths[] = { "/media/fat/_Console", "/media/fat/_Computer", "/media/fat", NULL };
	char latest_file[256];
	latest_file[0] = 0;
	out_path[0] = 0;

	for (int i = 0; paths[i]; i++) {
		DIR *dir = opendir(paths[i]);
		if (!dir) continue;

		struct dirent *ent;
		while ((ent = readdir(dir)) != NULL) {
			if (ent->d_type == DT_REG || ent->d_type == DT_LNK) {
				if (strncmp(ent->d_name, core_name, strlen(core_name)) == 0) {
					char *ext = strrchr(ent->d_name, '.');
					if (ext && strcasecmp(ext, ".rbf") == 0) {
						if (strcmp(ent->d_name, latest_file) > 0) {
							strcpy(latest_file, ent->d_name);
							sprintf(out_path, "%s/%s", paths[i], ent->d_name);
						}
					}
				}
			}
		}
		closedir(dir);
	}
}

void AutoLoadCore(DiscType type) {
	const char* core_name = nullptr;
	switch(type) {
		case DISC_MEGACD: core_name = "MegaCD"; break;
		case DISC_SATURN: core_name = "Saturn"; break;
		case DISC_PSX:    core_name = "PSX"; break;
		case DISC_NEOGEO: core_name = "NeoGeo"; break;
		default: return;
	}

	if (core_name) {
		char rbf_path[1024];
		FindLatestCore(core_name, rbf_path);

		if (rbf_path[0]) {
			printf("[AutoLoad] Loading core: %s\n", rbf_path);
			char msg[64];
			sprintf(msg, "Loading %s...", core_name);
			OsdWrite(15, msg, 3, 0);
			fpga_load_rbf(rbf_path);
		} else {
			printf("[AutoLoad] Core not found: %s\n", core_name);
			char msg[64];
			sprintf(msg, "%s Core Not Found!", core_name);
			OsdWrite(15, msg, 3, 0);
		}
	}
}

