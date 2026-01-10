
import os

filepath = 'support/megacd/megacd.cpp'

with open(filepath, 'rb') as f:
    content = f.read()

# 1. Include
target_include = b'#include "megacd.h"'
patch_include = b'#include "megacd.h"\n#include "../../cdrom_io.h"'

if target_include in content:
    content = content.replace(target_include, patch_include)
else:
    print("Error: megacd.cpp Include target not found")

# 2. mcd_poll Auto-Load
target_poll = b'static uint8_t adj = 0;'
patch_poll = b'''static uint8_t adj = 0;
	static bool load_attempted = false;

	if (!hasCDROMMedia(0))
	{
		load_attempted = false;
	}
	else if (!cdd.loaded && !load_attempted && (getCDROMType(0) == DISC_MEGACD || getCDROMType(0) == DISC_UNKNOWN))
	{
		load_attempted = true;
		mcd_set_image(0, "");
	}'''

if target_poll in content:
    content = content.replace(target_poll, patch_poll)
else:
    print("Error: mcd_poll target not found")

# 3. mcd_set_image BIOS Fallback
# Context:
# 		char *p = strrchr(buf, '/');
# 		if (p)
# 		{
# 			strcpy(p + 1, "cd_bios.rom");
# 			loaded = user_io_file_tx(buf);
# 		}
# We want to add else block.
# We look for the closing brace of the if(p).
# Since it is binary, I'll match the inside and the brace.
target_bios = b'strcpy(p + 1, "cd_bios.rom");\n\t\t\tloaded = user_io_file_tx(buf);\n\t\t}'
patch_bios = b'''strcpy(p + 1, "cd_bios.rom");
			loaded = user_io_file_tx(buf);
		}
		else
		{
			// Fallback for physical CD/empty path: Try known regions
			const char *bios_paths[] = {
				"/media/fat/games/MegaCD/USA/cd_bios.rom",
				"/media/fat/games/MegaCD/Europe/cd_bios.rom",
				"/media/fat/games/MegaCD/Japan/cd_bios.rom",
				"/media/fat/games/MegaCD/boot.rom"
			};
			for (int i = 0; i < 4; i++)
			{
				strcpy(buf, bios_paths[i]);
				loaded = user_io_file_tx(buf);
				if (loaded) break;
			}
		}'''

# Note: Check indent carefully. Original likely uses tabs.
# The target string above uses \t.
# The patch string I constructed uses tabs.

if target_bios in content:
    content = content.replace(target_bios, patch_bios)
else:
    print("Error: mcd_set_image BIOS target not found")
    # Debug: Print what we thought vs finding nearby string
    # Try finding just the strcpy line
    if b'strcpy(p + 1, "cd_bios.rom");' in content:
         print("Found strcpy but not full block context")

# 4. mcd_set_image Filename Check
target_check = b'if (loaded && *filename)'
patch_check = b'if (loaded && (*filename || hasCDROMMedia(0)))'

if target_check in content:
    content = content.replace(target_check, patch_check)
else:
    print("Error: mcd_set_image check target not found")

with open(filepath, 'wb') as f:
    f.write(content)
