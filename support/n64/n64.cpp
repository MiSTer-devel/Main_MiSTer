#include "n64.h"
#include "../../menu.h"
#include "../../user_io.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "lib/md5/md5.h"

// Simple hash function, see: https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function

static constexpr uint64_t FNV_PRIME = 0x100000001b3;
static constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325;

static constexpr uint64_t fnv_hash(const char *s, uint64_t h = FNV_OFFSET_BASIS)
{
	if (s) while (uint8_t a = *(s++)) h = (h ^ a) * FNV_PRIME;
	return h;
}

enum class MemoryType
{
	NONE = 0,
	EEPROM_512,
	EEPROM_2k,
	SRAM_32k,
	SRAM_96k,
	FLASH_128k
};

enum class CIC
{
	CIC_NUS_6101 = 0,
	CIC_NUS_6102,
	CIC_NUS_7101,
	CIC_NUS_7102,
	CIC_NUS_6103,
	CIC_NUS_7103,
	CIC_NUS_6105,
	CIC_NUS_7105,
	CIC_NUS_6106,
	CIC_NUS_7106,
	CIC_NUS_8303,
	CIC_NUS_8401,
	CIC_NUS_5167,
	CIC_NUS_DDUS
};

enum class SystemType
{
	NTSC = 0,
	PAL
};

enum class RomFormat
{
	UNKNOWN = 0,
	BIG_ENDIAN,
	BYTE_SWAPPED,
	LITTLE_ENDIAN,
};

enum class AutoDetect
{
	ON = 0,
	OFF = 1,
};

static RomFormat detectRomFormat(const uint8_t* data)
{
	// data should be aligned
	const uint32_t val = *(uint32_t*)data;

	// the following checks assume we're on a little-endian platform
	// for each check, the first value is for regular roms, the 2nd is for 64DD images
	if (val == 0x40123780 || val == 0x40072780) return RomFormat::BIG_ENDIAN;
	if (val == 0x12408037 || val == 0x07408027) return RomFormat::BYTE_SWAPPED;
	if (val == 0x80371240 || val == 0x80270740) return RomFormat::LITTLE_ENDIAN;

	return RomFormat::UNKNOWN;
}

static void normalizeData(uint8_t* data, size_t size, RomFormat format)
{
	switch(format)
	{
		case RomFormat::BYTE_SWAPPED:
		for (size_t i = 0; i < size; i += 2)
		{
			auto c0 = data[0];
			auto c1 = data[1];
			data[0] = c1;
			data[1] = c0;
			data += 2;
		}
		break;
		case RomFormat::LITTLE_ENDIAN:
		for (size_t i = 0; i < size; i += 4)
		{
			auto c0 = data[0];
			auto c1 = data[1];
			auto c2 = data[2];
			auto c3 = data[3];
			data[0] = c3;
			data[1] = c2;
			data[2] = c1;
			data[3] = c0;
			data += 4;
		}
		break;
		default:
		{
			// nothing to do
		}
	}
}

static void normalizeString(char* s)
{
	// change the string to lower-case
	while (*s) { *s = tolower(*s); ++s; }
}

static bool detect_rom_settings_in_db(const char* lookup_hash, const char* db_file_name)
{
	fileTextReader reader = {};

	char file_path[1024];
	sprintf(file_path, "%s/%s", HomeDir(), db_file_name);

	if (!FileOpenTextReader(&reader, file_path))
	{
		printf("Failed to open N64 data file %s\n", file_path);
		return false;
	}

	char tags[128];

	const char* line;
	while ((line = FileReadLine(&reader)))
	{
		// skip the line if it doesn't start with our hash
		if (strncmp(lookup_hash, line, 32) != 0)
			continue;

		if (sscanf(line, "%*s %s", tags) != 1)
		{
			printf("No tags found.\n");
			continue;
		}

		printf("Found ROM entry: %s\n", line);

		MemoryType save_type = MemoryType::NONE;
		SystemType system_type = SystemType::NTSC;
		CIC cic = CIC::CIC_NUS_6102;
		bool cpak = false;
		bool rpak = false;
		bool tpak = false;
		bool rtc = false;

		const char separator[] = "|";

		for (char* tag = strtok(tags, separator); tag; tag = strtok(nullptr, separator))
		{
			printf("Tag: %s\n", tag);

			normalizeString(tag);
			switch (fnv_hash(tag))
			{
				case fnv_hash("eeprom512"): save_type = MemoryType::EEPROM_512; break;
				case fnv_hash("eeprom2k"): save_type = MemoryType::EEPROM_2k; break;
				case fnv_hash("sram32k"): save_type = MemoryType::SRAM_32k; break;
				case fnv_hash("sram96k"): save_type = MemoryType::SRAM_96k; break;
				case fnv_hash("flash128k"): save_type = MemoryType::FLASH_128k; break;
				case fnv_hash("ntsc"): system_type = SystemType::NTSC; break;
				case fnv_hash("pal"): system_type = SystemType::PAL; break;
				case fnv_hash("cpak"): cpak = true; break;
				case fnv_hash("rpak"): rpak = true; break;
				case fnv_hash("tpak"): tpak = true; break;
				case fnv_hash("rtc"): rtc = true; break;
				case fnv_hash("cic6101"): cic = CIC::CIC_NUS_6101; break;
				case fnv_hash("cic6102"): cic = CIC::CIC_NUS_6102; break;
				case fnv_hash("cic6103"): cic = CIC::CIC_NUS_6103; break;
				case fnv_hash("cic6105"): cic = CIC::CIC_NUS_6105; break;
				case fnv_hash("cic6106"): cic = CIC::CIC_NUS_6106; break;
				case fnv_hash("cic7101"): cic = CIC::CIC_NUS_7101; break;
				case fnv_hash("cic7102"): cic = CIC::CIC_NUS_7102; break;
				case fnv_hash("cic7103"): cic = CIC::CIC_NUS_7103; break;
				case fnv_hash("cic7105"): cic = CIC::CIC_NUS_7105; break;
				case fnv_hash("cic7106"): cic = CIC::CIC_NUS_7106; break;
				case fnv_hash("cic8303"): cic = CIC::CIC_NUS_8303; break;
				case fnv_hash("cic8401"): cic = CIC::CIC_NUS_8401; break;
				case fnv_hash("cic5167"): cic = CIC::CIC_NUS_5167; break;
				case fnv_hash("cicDDUS"): cic = CIC::CIC_NUS_DDUS; break;
				default: printf("Unknown tag: %s\n", tag);
			}
		}
		printf("System: %d, Save Type: %d, CIC: %d, CPak: %d, RPak: %d, TPak %d, RTC: %d\n", (int)system_type, (int)save_type, (int)cic, cpak, rpak, tpak, rtc);

		const auto auto_detect = (AutoDetect)user_io_status_get("[64]");

		if (auto_detect == AutoDetect::ON)
		{
			printf("Auto-detect is on, updating OSD settings\n");

			user_io_status_set("[80:79]", (uint32_t)system_type);
			user_io_status_set("[68:65]", (uint32_t)cic);
			user_io_status_set("[71]", (uint32_t)cpak);
			user_io_status_set("[72]", (uint32_t)rpak);
			user_io_status_set("[73]", (uint32_t)tpak);
			user_io_status_set("[74]", (uint32_t)rtc);
			user_io_status_set("[77:75]", (uint32_t)save_type);
		}
		else
		{
			printf("Auto-detect is off, not updating OSD settings\n");
		}

		return true;
	}

	return false;
}

static const char* DB_FILE_NAMES[] =
{
	"N64-database.txt",
	"N64-database_user.txt",
};

static bool detect_rom_settings(const char* lookup_hash)
{
	for (const char* db_file_name: DB_FILE_NAMES)
	{
		if (detect_rom_settings_in_db(lookup_hash, db_file_name))
			return true;
	}
	return false;
}

static void md5_to_hex(uint8_t* in, char* out)
{
	char *p = out;
	for (int i = 0; i < 16; i++)
	{
		sprintf(p, "%02x", in[i]);
		p += 2;
	}
	*p = '\0';
}

int n64_rom_tx(const char* name, unsigned char index)
{
	static uint8_t buf[4096];
	fileTYPE f = {};

	if (!FileOpen(&f, name, 1)) return 0;

	unsigned long bytes2send = f.size;

	printf("N64 file %s with %lu bytes to send for index %04X\n", name, bytes2send, index);

	// set index byte
	user_io_set_index(index);

	// prepare transmission of new file
	user_io_set_download(1);

	int use_progress = 1;
	int size = bytes2send;
	if (use_progress) ProgressMessage(0, 0, 0, 0);

	// save state processing
	process_ss(name);

	bool is_first_chunk = true;
	bool rom_settings_detected = false;
	RomFormat rom_format = RomFormat::UNKNOWN;

	MD5Context ctx;
	MD5Init(&ctx);
	uint8_t md5[16];
	char md5_hex[40];

	while (bytes2send)
	{
		uint32_t chunk = (bytes2send > sizeof(buf)) ? sizeof(buf) : bytes2send;

		FileReadAdv(&f, buf, chunk);

		// perform sanity checks and detect ROM format
		if (is_first_chunk)
		{
			if (chunk < 4096)
			{
				printf("Failed to load ROM: must be at least 4096 bytes\n");
				return 0;
			}
			rom_format = detectRomFormat(buf);
		}

		// normalize data to big-endian format
		normalizeData(buf, chunk, rom_format);

		MD5Update(&ctx, buf, chunk);

		if (is_first_chunk)
		{
			// try to detect ROM settings based on header MD5 hash

			// For calculating the MD5 hash of the header, we need to make a
			// copy of the context before calling MD5Final, otherwise the file
			// hash will be incorrect lateron.
			MD5Context ctx_header;
			memcpy(&ctx_header, &ctx, sizeof(struct MD5Context));
			MD5Final(md5, &ctx_header);
			md5_to_hex(md5, md5_hex);
			printf("Header MD5: %s\n", md5_hex);

			rom_settings_detected = detect_rom_settings(md5_hex);
			if (!rom_settings_detected) printf("No ROM information found for header hash: %s\n", md5_hex);
		}

		user_io_file_tx_data(buf, chunk);

		if (use_progress) ProgressMessage("Loading", f.name, size - bytes2send, size);
		bytes2send -= chunk;
		is_first_chunk = false;
	}

	MD5Final(md5, &ctx);
	md5_to_hex(md5, md5_hex);
	printf("File MD5: %s\n", md5_hex);

	// Try to detect ROM settings from file MD5 if they are not available yet
	if (!rom_settings_detected)
	{
		rom_settings_detected = detect_rom_settings(md5_hex);
		if (!rom_settings_detected) printf("No ROM information found for file hash: %s\n", md5_hex);
	}

	printf("Done.\n");
	FileClose(&f);

	// mount save state
	char file_path[1024];
	FileGenerateSavePath(name, file_path);
	user_io_file_mount(file_path, 0, 1);

	// signal end of transmission
	user_io_set_download(0);
	ProgressMessage(0, 0, 0, 0);

	if (!rom_settings_detected) Info("auto-detect failed");

	return 1;
}
