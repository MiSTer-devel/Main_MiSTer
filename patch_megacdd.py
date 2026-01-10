
import os

filepath = 'support/megacd/megacdd.cpp'

with open(filepath, 'rb') as f:
    content = f.read()

# 1. Include
target_include = b'#include "megacd.h"'
patch_include = b'#include "megacd.h"\n#include "../../cdrom_io.h"'

if target_include in content:
    content = content.replace(target_include, patch_include)
else:
    print("Error: Include target not found")

# 2. Load Logic
# Search for the lines around where we want to insert.
# Line 248: const char *ext = filename+strlen(filename)-4;
# Line 249: if (!strncasecmp(".cue", ext, 4))
# We assume the file uses TABS for indentation based on previous observations.
target_load = b'\tif (!strncasecmp(".cue", ext, 4))'
load_logic = b'''
	if ((getCDROMType(0) == DISC_MEGACD || getCDROMType(0) == DISC_UNKNOWN) && hasCDROMMedia(0) && !filename[0])
	{
		CDROM_TrackInfo tracks[100];
		int count = read_cdrom_toc(0, tracks, 99);
		if (count > 0)
		{
			this->toc.last = count;
			this->toc.end = tracks[count-1].end_lba + 1;
			for(int i=0; i<count; i++)
			{
				this->toc.tracks[i].start = tracks[i].start_lba;
				this->toc.tracks[i].end = tracks[i].end_lba;
				this->toc.tracks[i].type = tracks[i].type;
				printf("MCD: Physical Track %d: Start %d End %d Type %d\\n", i+1, tracks[i].start_lba, tracks[i].end_lba, tracks[i].type);
			}
			printf("MCD: Physical CD Mounted via TOC. Last=%d End=%d\\n", this->toc.last, this->toc.end);
			this->loaded = 1;
			return 1;
		}
	}

'''
# Note: Added extra newline at end of logic to spacing.
# And correct tab indentation (single tab).

if target_load in content:
    content = content.replace(target_load, load_logic + target_load)
else:
    print("Error: Load target not found. Trying with spaces just in case.")
    # Fallback to verify if maybe spaces used?
    # But git diff showed tabs were removed.

# 3. ReadData Logic
# Target: void cdd_t::ReadData(uint8_t *buf)\n{
target_readdata = b'void cdd_t::ReadData(uint8_t *buf)\n{'
readdata_logic = b'''
	if (this->toc.tracks[this->index].type && (this->lba >= 0))
	{
		DiscType cd_type = getCDROMType(0); // Assume drive 0 for now
		if (cd_type == DISC_MEGACD && hasCDROMMedia(0))
		{
			// Try reading from physical CD
			read_cdrom_sector(0, this->lba, buf, 2048);
			return;
		}
	}
'''
# Wait, I need to be careful.
# Original code:
# void cdd_t::ReadData(uint8_t *buf)
# {
# 	if (this->toc.tracks[this->index].type && (this->lba >= 0))
# 	{
#
# 		if (this->toc.chd_f)
# ...
#
# If I simply insert my code at the top, I duplicate the `if (this->toc.tracks...` check?
# No, I want to insert *inside* `ReadData` but *before* the existing logic?
# The user's request from earlier:
# "Updated `cdd_t::ReadData` ... Integrated physical CD detection"
# My previous implementation:
# void cdd_t::ReadData(uint8_t *buf)
# {
# 	if (this->toc.tracks[this->index].type && (this->lba >= 0))
# 	{
# 		DiscType cd_type = ...
# ...
# 		if (this->toc.chd_f) ...
#
# So I need to insert INSIDE the first IF check of `ReadData`.
# Pattern:
# 	if (this->toc.tracks[this->index].type && (this->lba >= 0))
# 	{
#
# 		if (this->toc.chd_f)
#
# I should replace `if (this->toc.tracks[this->index].type && (this->lba >= 0))\n\t{\n`
# with:
# 	if (this->toc.tracks[this->index].type && (this->lba >= 0))
# 	{
# 		DiscType cd_type = getCDROMType(0); // Assume drive 0 for now
# 		if (cd_type == DISC_MEGACD && hasCDROMMedia(0))
# 		{
# 			// Try reading from physical CD
# 			read_cdrom_sector(0, this->lba, buf, 2048);
# 			return;
# 		}
#
# 		if (this->toc.chd_f)
# ... wait, I need to match the next line to be safe.
#
# Let's try matching just the if statement line and the brace.
#
target_readdata_inner = b'if (this->toc.tracks[this->index].type && (this->lba >= 0))\n\t{'
readdata_patch = b'''if (this->toc.tracks[this->index].type && (this->lba >= 0))
	{
		DiscType cd_type = getCDROMType(0); // Assume drive 0 for now
		if (cd_type == DISC_MEGACD && hasCDROMMedia(0))
		{
			// Try reading from physical CD
			read_cdrom_sector(0, this->lba, buf, 2048);
			return;
		}
'''
if target_readdata_inner in content:
    content = content.replace(target_readdata_inner, readdata_patch)
else:
    print("Error: ReadData inner target not found")
    # try matching with spaces around brace?
    target_readdata_inner_v2 = b'if (this->toc.tracks[this->index].type && (this->lba >= 0)) {'
    if target_readdata_inner_v2 in content:
         # Ah, K&R style might be used!
         readdata_patch_v2 = b'''if (this->toc.tracks[this->index].type && (this->lba >= 0)) {
		DiscType cd_type = getCDROMType(0); // Assume drive 0 for now
		if (cd_type == DISC_MEGACD && hasCDROMMedia(0))
		{
			// Try reading from physical CD
			read_cdrom_sector(0, this->lba, buf, 2048);
			return;
		}
'''
         content = content.replace(target_readdata_inner_v2, readdata_patch_v2)
         print("Matched ReadData K&R style")
    else:
         print("Error: ReadData inner target not found (v2)")


# 4. ReadCDDA Logic
# Original:
#	if (this->isData)
#	{
#		return this->audioLength;
#	}
#
#	if (this->toc.chd_f)
#
# We want to insert between these blocks.
target_readcdda = b'if (this->toc.chd_f)'
readcdda_logic = b'''DiscType cd_type = getCDROMType(0); // Assume drive 0
	if ((cd_type == DISC_MEGACD || cd_type == DISC_UNKNOWN) && hasCDROMMedia(0))
	{
		int read_len = read_cdrom_sector(0, this->chd_audio_read_lba, buf, this->audioLength);
		if (read_len > 0)
		{
			if ((this->audioLength / 2352) > 1) this->chd_audio_read_lba++;
			return this->audioLength;
		}
		memset(buf, 0, this->audioLength);
		return this->audioLength;
	}

	'''
# Indentation: The file seems to use tabs. So indented by 1 tab.
readcdda_logic_tabs = b'''	DiscType cd_type = getCDROMType(0); // Assume drive 0
	if ((cd_type == DISC_MEGACD || cd_type == DISC_UNKNOWN) && hasCDROMMedia(0))
	{
		int read_len = read_cdrom_sector(0, this->chd_audio_read_lba, buf, this->audioLength);
		if (read_len > 0)
		{
			if ((this->audioLength / 2352) > 1) this->chd_audio_read_lba++;
			return this->audioLength;
		}
		memset(buf, 0, this->audioLength);
		return this->audioLength;
	}

'''
# We want to insert this BEFORE `if (this->toc.chd_f)` but specifically inside `ReadCDDA`.
# `ReadData` also has `if (this->toc.chd_f)`!
# unique signature for ReadCDDA context:
# 	if (this->isData)
# 	{
# 		return this->audioLength;
# 	}
#
# 	if (this->toc.chd_f)

target_readcdda_context = b'\tif (this->isData)\n\t{\n\t\treturn this->audioLength;\n\t}\n\n\tif (this->toc.chd_f)'
# Checking K&R option too
target_readcdda_context_kr = b'\tif (this->isData) {\n\t\treturn this->audioLength;\n\t}\n\n\tif (this->toc.chd_f)'

patch_readcdda_replacement = b'\tif (this->isData)\n\t{\n\t\treturn this->audioLength;\n\t}\n\n' + readcdda_logic_tabs + b'\tif (this->toc.chd_f)'
patch_readcdda_replacement_kr = b'\tif (this->isData) {\n\t\treturn this->audioLength;\n\t}\n\n' + readcdda_logic_tabs + b'\tif (this->toc.chd_f)'

if target_readcdda_context in content:
    content = content.replace(target_readcdda_context, patch_readcdda_replacement)
elif target_readcdda_context_kr in content:
    content = content.replace(target_readcdda_context_kr, patch_readcdda_replacement_kr)
    print("Matched ReadCDDA K&R style")
else:
    print("Error: ReadCDDA target not found")

with open(filepath, 'wb') as f:
    f.write(content)
