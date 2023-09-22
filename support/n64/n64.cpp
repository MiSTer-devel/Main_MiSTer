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

static bool detect_rom_settings_in_dbs(const char* lookup_hash)
{
	for (const char* db_file_name: DB_FILE_NAMES)
	{
		if (detect_rom_settings_in_db(lookup_hash, db_file_name))
			return true;
	}
	return false;
}

static bool detect_rom_settings_from_first_chunk(char* cart_id, char region_code, uint64_t crc)
{
	MemoryType save_type;
	SystemType system_type;
	CIC cic;

	const auto auto_detect = (AutoDetect)user_io_status_get("[64]");

	if (auto_detect != AutoDetect::ON)
	{
		printf("Auto-detect is off, not updating OSD settings\n");
		return true;
	}

	switch (region_code)
	{
		case 'D': //Germany
		case 'F': //France
		case 'H': //Netherlands (Dutch)
		case 'I': //Italy
		case 'L': //Gateway 64
		case 'P': //Europe
		case 'S': //Spain
		case 'U': //Australia
		case 'W': //Scandinavia
		case 'X': //Europe
		case 'Y': //Europe
			system_type = SystemType::PAL; break;
		default: 
			system_type = SystemType::NTSC; break;
	}

	// the following checks assume we're on a little-endian platform
	switch (crc)
	{
		case UINT64_C(0x000000a316adc55a): 
		case UINT64_C(0x000000039c981107): // hcs64's CIC-6102 IPL3 replacement
		case UINT64_C(0x000000a30dacd530): // Unknown. Used in SM64 hacks
		case UINT64_C(0x000000d2828281b0): // Unknown. Used in some homebrew
		case UINT64_C(0x0000009acc31e644): // Unknown. Used in some betas and homebrew. Dev boot code?
			  cic = system_type == SystemType::NTSC
			? CIC::CIC_NUS_6102 
			: CIC::CIC_NUS_7101; break;
		case UINT64_C(0x000000a405397b05): cic = CIC::CIC_NUS_7102; system_type = SystemType::PAL; break;
		case UINT64_C(0x000000a0f26f62fe): cic = CIC::CIC_NUS_6101; system_type = SystemType::NTSC; break;
		case UINT64_C(0x000000a9229d7c45): 
			  cic = system_type == SystemType::NTSC 
			? CIC::CIC_NUS_6103 
			: CIC::CIC_NUS_7103; break;
		case UINT64_C(0x000000f8b860ed00): 
			  cic = system_type == SystemType::NTSC 
			? CIC::CIC_NUS_6105 
			: CIC::CIC_NUS_7105; break;
		case UINT64_C(0x000000ba5ba4b8cd): 
			  cic = system_type == SystemType::NTSC 
			? CIC::CIC_NUS_6106 
			: CIC::CIC_NUS_7106; break;
		case UINT64_C(0x0000012daafc8aab): cic = CIC::CIC_NUS_5167; break;
		case UINT64_C(0x000000a9df4b39e1): cic = CIC::CIC_NUS_8303; break;
		case UINT64_C(0x000000aa764e39e1): cic = CIC::CIC_NUS_8401; break;
		case UINT64_C(0x000000abb0b739e1): cic = CIC::CIC_NUS_DDUS; break;
		default: return false;
	}

	switch (fnv_hash(cart_id))
	{
		case fnv_hash("4W"):
		case fnv_hash("AB"):
		case fnv_hash("AD"):
		case fnv_hash("AG"):
		case fnv_hash("B6"):
		case fnv_hash("BC"):
		case fnv_hash("BD"):
		case fnv_hash("BH"):
		case fnv_hash("BK"):
		case fnv_hash("BM"):
		case fnv_hash("BN"):
		case fnv_hash("BV"):
		case fnv_hash("CG"):
		case fnv_hash("CH"):
		case fnv_hash("CR"):
		case fnv_hash("CT"):
		case fnv_hash("CU"):
		case fnv_hash("CX"):
		case fnv_hash("D3"):
		case fnv_hash("D4"):
		case fnv_hash("DQ"):
		case fnv_hash("DR"):
		case fnv_hash("DU"):
		case fnv_hash("DY"):
		case fnv_hash("EA"):
		case fnv_hash("ER"):
		case fnv_hash("FG"):
		case fnv_hash("FH"):
		case fnv_hash("FW"):
		case fnv_hash("FX"):
		case fnv_hash("FY"):
		case fnv_hash("GC"):
		case fnv_hash("GE"):
		case fnv_hash("GU"):
		case fnv_hash("GV"):
		case fnv_hash("HA"):
		case fnv_hash("HF"):
		case fnv_hash("HP"):
		case fnv_hash("IC"):
		case fnv_hash("IJ"):
		case fnv_hash("IR"):
		case fnv_hash("JM"):
		case fnv_hash("K2"):
		case fnv_hash("KA"):
		case fnv_hash("KI"):
		case fnv_hash("KT"):
		case fnv_hash("LB"):
		case fnv_hash("LR"):
		case fnv_hash("MG"):
		case fnv_hash("MI"):
		case fnv_hash("ML"):
		case fnv_hash("MO"):
		case fnv_hash("MR"):
		case fnv_hash("MS"):
		case fnv_hash("MU"):
		case fnv_hash("MW"):
		case fnv_hash("N6"):
		case fnv_hash("NA"):
		case fnv_hash("OH"):
		case fnv_hash("PG"):
		case fnv_hash("PW"):
		case fnv_hash("PY"):
		case fnv_hash("RC"):
		case fnv_hash("RS"):
		case fnv_hash("S6"):
		case fnv_hash("SA"):
		case fnv_hash("SC"):
		case fnv_hash("SM"):
		case fnv_hash("SU"):
		case fnv_hash("SV"):
		case fnv_hash("SW"):
		case fnv_hash("T6"):
		case fnv_hash("TB"):
		case fnv_hash("TC"):
		case fnv_hash("TJ"):
		case fnv_hash("TM"):
		case fnv_hash("TN"):
		case fnv_hash("TP"):
		case fnv_hash("VL"):
		case fnv_hash("VY"):
		case fnv_hash("WC"):
		case fnv_hash("WL"):
		case fnv_hash("WR"):
		case fnv_hash("WU"):
		case fnv_hash("XO"): save_type = MemoryType::EEPROM_512; break;

		case fnv_hash("3D"):
		case fnv_hash("B7"):
		case fnv_hash("CW"):
		case fnv_hash("CZ"):
		case fnv_hash("D2"):
		case fnv_hash("D6"):
		case fnv_hash("DO"):
		case fnv_hash("DP"):
		case fnv_hash("EP"):
		case fnv_hash("EV"):
		case fnv_hash("F2"):
		case fnv_hash("FU"):
		case fnv_hash("IM"):
		case fnv_hash("M8"):
		case fnv_hash("MV"):
		case fnv_hash("MX"):
		case fnv_hash("NB"):
		case fnv_hash("NX"):
		case fnv_hash("PD"):
		case fnv_hash("RZ"):
		case fnv_hash("UB"):
		case fnv_hash("X7"):
		case fnv_hash("YS"): save_type = MemoryType::EEPROM_2k; break;

		case fnv_hash("A2"):
		case fnv_hash("AL"):
		case fnv_hash("AY"):
		case fnv_hash("DA"):
		case fnv_hash("FZ"):
		case fnv_hash("G6"):
		case fnv_hash("GP"):
		case fnv_hash("K4"):
		case fnv_hash("KG"):
		case fnv_hash("MF"):
		case fnv_hash("OB"):
		case fnv_hash("RE"):
		case fnv_hash("RI"):
		case fnv_hash("TE"):
		case fnv_hash("VB"):
		case fnv_hash("VP"):
		case fnv_hash("W2"):
		case fnv_hash("WI"):
		case fnv_hash("WX"):
		case fnv_hash("YW"):
		case fnv_hash("ZL"): save_type = MemoryType::SRAM_32k; break;

		case fnv_hash("DZ"): save_type = MemoryType::SRAM_96k; break;

		case fnv_hash("AF"):
		case fnv_hash("CC"):
		case fnv_hash("CK"):
		case fnv_hash("DL"):
		case fnv_hash("JD"):
		case fnv_hash("JF"):
		case fnv_hash("KJ"):
		case fnv_hash("M6"):
		case fnv_hash("MQ"):
		case fnv_hash("P2"):
		case fnv_hash("P3"):
		case fnv_hash("PF"):
		case fnv_hash("PH"):
		case fnv_hash("PN"):
		case fnv_hash("PO"):
		case fnv_hash("PS"):
		case fnv_hash("RH"):
		case fnv_hash("SI"):
		case fnv_hash("SQ"):
		case fnv_hash("T9"):
		case fnv_hash("W4"):
		case fnv_hash("ZS"): save_type = MemoryType::FLASH_128k; break;

		default: save_type = MemoryType::NONE; break;
	}

	printf("System: %d, Save Type: %d, CIC: %d\n", (int)system_type, (int)save_type, (int)cic);
	printf("Auto-detect is on, updating OSD settings\n");

	user_io_status_set("[80:79]", (uint32_t)system_type);
	user_io_status_set("[68:65]", (uint32_t)cic);
	user_io_status_set("[71]", (uint32_t)0); // Controller pak
	user_io_status_set("[72]", (uint32_t)0); // Rumble pak
	user_io_status_set("[73]", (uint32_t)0); // Transfer pak
	user_io_status_set("[74]", (uint32_t)0); // RTC
	user_io_status_set("[77:75]", (uint32_t)save_type);

	return true;
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
	uint64_t ipl3_crc = 0;
	char cart_id[8];
	char region_code = '\0';

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

			rom_settings_detected = detect_rom_settings_in_dbs(md5_hex);
			if (!rom_settings_detected)
			{
				printf("No ROM information found for header hash: %s\n", md5_hex);
				for (size_t i = 0x40 / sizeof(uint32_t); i < 0x1000 / sizeof(uint32_t); i++) ipl3_crc += ((uint32_t*)buf)[i];
				strncpy(cart_id, (char*)(buf + 0x3c), 2);
				cart_id[2] = '\0';
				region_code = buf[0x3e];
			}
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
		rom_settings_detected = detect_rom_settings_in_dbs(md5_hex);
		if (!rom_settings_detected) printf("No ROM information found for file hash: %s\n", md5_hex);
	}

	// Try detect (partial) ROM settings by analyzing the ROM itself. (region, cic and save type)
	// Fallback for missing db entries.
	if (!rom_settings_detected)
	{
		rom_settings_detected = detect_rom_settings_from_first_chunk(cart_id, region_code, ipl3_crc);
		if (!rom_settings_detected) printf("Unknown CIC type: %016" PRIX64 "\n", ipl3_crc);
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
