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
static constexpr uint8_t CARTID_LENGTH = 6;
static constexpr auto CARTID_PREFIX = "ID:";

static constexpr uint64_t fnv_hash(const char *s, uint64_t h = FNV_OFFSET_BASIS) {
	if (s) while (uint8_t a = *(s++)) h = (h ^ a) * FNV_PRIME;
	return h;
}

enum class MemoryType {
	NONE = 0,
	EEPROM_512,
	EEPROM_2k,
	SRAM_32k,
	SRAM_96k,
	FLASH_128k
};

enum class CIC {
	UNKNOWN = -1,
	CIC_NUS_6101,
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

enum class SystemType {
	UNKNOWN = -1,
	NTSC,
	PAL
};

enum class RomFormat {
	UNKNOWN = 0,
	BIG_ENDIAN,
	BYTE_SWAPPED,
	LITTLE_ENDIAN,
};

enum class AutoDetect {
	ON = 0,
	OFF = 1,
};

static RomFormat detectRomFormat(const uint8_t* data) {
	// data should be aligned
	const uint32_t val = *(uint32_t*)data;

	// the following checks assume we're on a little-endian platform
	// for each check, the first value is for regular roms, the 2nd is for 64DD images
	// third is a malformed magic word used in homebrew (mostly pointless)
	if (val == UINT32_C(0x40123780) || val == UINT32_C(0x40072780) || val == UINT32_C(0x41123780)) return RomFormat::BIG_ENDIAN;
	if (val == UINT32_C(0x12408037) || val == UINT32_C(0x07408027) || val == UINT32_C(0x12418037)) return RomFormat::BYTE_SWAPPED;
	if (val == UINT32_C(0x80371240) || val == UINT32_C(0x80270740) || val == UINT32_C(0x80371241)) return RomFormat::LITTLE_ENDIAN;

	return RomFormat::UNKNOWN;
}

static void normalizeData(uint8_t* data, size_t size, RomFormat format) {
	switch(format) {
		case RomFormat::BYTE_SWAPPED:
			for (size_t i = 0; i < size; i += 2) {
				auto c0 = data[0];
				auto c1 = data[1];
				data[0] = c1;
				data[1] = c0;
				data += 2;
			}
			break;
		case RomFormat::LITTLE_ENDIAN:
			for (size_t i = 0; i < size; i += 4) {
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
			// nothing to do
			break;
	}
}

static void normalizeString(char* s) {
	// change the string to lower-case
	while (*s) { *s = tolower(*s); ++s; }
}

// return true if cic and system region is detected
static bool parse_and_apply_db_tags(char* tags) {
	MemoryType save_type = MemoryType::NONE;
	SystemType system_type = SystemType::UNKNOWN;
	CIC cic = CIC::UNKNOWN;
	bool cpak = false;
	bool rpak = false;
	bool tpak = false;
	bool rtc = false;

	const char separator[] = "|";

	for (char* tag = strtok(tags, separator); tag; tag = strtok(nullptr, separator)) {
		printf("Tag: %s\n", tag);

		normalizeString(tag);
		switch (fnv_hash(tag)) {
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
			default: printf("Unknown tag: %s\n", tag); break;
		}
	}
	printf("System: %d, Save Type: %d, CIC: %d, CPak: %d, RPak: %d, TPak %d, RTC: %d\n", (int)system_type, (int)save_type, (int)cic, cpak, rpak, tpak, rtc);

	const auto auto_detect = (AutoDetect)user_io_status_get("[64]");

	if (auto_detect == AutoDetect::ON) {
		printf("Auto-detect is on, updating OSD settings\n");

		if (system_type != SystemType::UNKNOWN) user_io_status_set("[80:79]", (uint32_t)system_type);
		if (cic != CIC::UNKNOWN) user_io_status_set("[68:65]", (uint32_t)cic);
		user_io_status_set("[71]", (uint32_t)cpak);
		user_io_status_set("[72]", (uint32_t)rpak);
		user_io_status_set("[73]", (uint32_t)tpak);
		user_io_status_set("[74]", (uint32_t)rtc);
		user_io_status_set("[77:75]", (uint32_t)save_type);
	}
	else {
		printf("Auto-detect is off, not updating OSD settings\n");
	}

	return (auto_detect != AutoDetect::ON) || (system_type != SystemType::UNKNOWN && cic != CIC::UNKNOWN);
}

bool md5_matches(const char* line, const char* md5) {
	for (auto i = 0; i < 32; i++) {
		if (line[i] == '\0' || md5[i] != tolower(line[i]))
			return false;
	}

	return true;
}

bool cart_id_matches(const char* line, const char* cart_id) {
	// A valid ID line should start with "ID:"
	if (strncmp(line, CARTID_PREFIX, strlen(CARTID_PREFIX)) != 0)
		return false;

	// Skip the line if it doesn't match our cart_id, '_' = don't care
	auto lp = (char*)line + strlen(CARTID_PREFIX);
	for (auto i = 0; i < CARTID_LENGTH; i++, lp++) {
		if (*lp != '_' && *lp != cart_id[i])
			return false; // character didn't match

		if (*lp == ' ') // early termination
			return true;
	}

	return true;
}

static uint8_t detect_rom_settings_in_db(const char* lookup_hash, const char* db_file_name) {
	fileTextReader reader = {};

	char file_path[1024];
	sprintf(file_path, "%s/%s", HomeDir(), db_file_name);

	if (!FileOpenTextReader(&reader, file_path)) {
		printf("Failed to open N64 data file %s\n", file_path);
		return false;
	}

	char tags[128];

	const char* line;
	while ((line = FileReadLine(&reader))) {
		// Skip the line if it doesn't start with our hash
		if (!md5_matches(line, lookup_hash))
			continue;

		printf("Found ROM entry: %s\n", line);

		if (sscanf(line, "%*s %s", tags) != 1) {
			printf("No tags found.\n");
			return 2;
		}

		// 2 = System region and/or CIC wasn't in DB, will need detection
		return parse_and_apply_db_tags(tags) ? 3 : 2;
	}

	return 0;
}

static uint8_t detect_rom_settings_in_db_with_cartid(const char* cart_id, const char* db_file_name) {
	fileTextReader reader = {};

	char file_path[1024];
	sprintf(file_path, "%s/%s", HomeDir(), db_file_name);

	if (!FileOpenTextReader(&reader, file_path)) {
		printf("Failed to open N64 data file %s\n", file_path);
		return false;
	}

	char tags[128];

	const char* line;
	while ((line = FileReadLine(&reader))) {
		// Skip the line if it doesn't start with our ID
		if (!cart_id_matches(line, cart_id))
			continue;

		printf("Found ROM entry: %s\n", line);

		if (sscanf(line, "%*s %s", tags) != 1) {
			printf("No tags found.\n");
			return 2;
		}

		// 2 = System region and/or CIC wasn't in DB, will need detection
		return parse_and_apply_db_tags(tags) ? 3 : 2;
	}

	return 0;
}

static const char* DB_FILE_NAMES[] =
{
	"N64-database.txt",
	"N64-database_user.txt",
};

static uint8_t detect_rom_settings_in_dbs_with_md5(const char* lookup) {
	for (const char* db_file_name : DB_FILE_NAMES) {
		const auto detected = detect_rom_settings_in_db(lookup, db_file_name);
		if (detected != 0) return detected;
	}

	return 0;
}

static uint8_t detect_rom_settings_in_dbs_with_cartid(const char* lookup) {
	if (strlen(lookup) < CARTID_LENGTH)
		return 0;

	for (auto i = 0; i < CARTID_LENGTH; i++) {
		if ((lookup[i] >= '0' && lookup[i] <= '9') || (lookup[i] >= 'A' && lookup[i] <= 'Z'))
			continue;

		return 0;
	}

	for (const char* db_file_name : DB_FILE_NAMES) {
		const auto detected = detect_rom_settings_in_db_with_cartid(lookup, db_file_name);
		if (detected != 0) return detected;
	}
	
	return 0;
}

static bool detect_rom_settings_from_first_chunk(char region_code, uint64_t crc) {
	SystemType system_type = SystemType::NTSC;
	CIC cic;
	bool is_known_cic = true;

	if ((AutoDetect)user_io_status_get("[64]") != AutoDetect::ON) {
		printf("Auto-detect is off, not updating OSD settings\n");
		return true;
	}

	switch (region_code) {
		case 'D': // Germany
		case 'F': // France
		case 'H': // Netherlands (Dutch)
		case 'I': // Italy
		case 'L': // Gateway 64
		case 'P': // Europe
		case 'S': // Spain
		case 'U': // Australia
		case 'W': // Scandinavia
		case 'X': // Europe
		case 'Y': // Europe
			system_type = SystemType::PAL; break;
	}

	// the following checks assume we're on a little-endian platform
	switch (crc) {
		default: 
			printf("Unknown CIC (0x%016llx), uses default\n", crc);
			is_known_cic = false;
			// fall through
		case UINT64_C(0x000000a316adc55a): 
		case UINT64_C(0x000000a30dacd530): // NOP:ed out CRC check
		case UINT64_C(0x000000039c981107): // hcs64's CIC-6102 IPL3 replacement
		case UINT64_C(0x000000d2828281b0): // Unknown. Used in some homebrew
		case UINT64_C(0x0000009acc31e644): // Unknown. Used in some betas and homebrew. Dev boot code?
			cic = system_type == SystemType::NTSC
			? CIC::CIC_NUS_6102 
			: CIC::CIC_NUS_7101; break;
		case UINT64_C(0x000000a405397b05): 
		case UINT64_C(0x000000a3fc388adb): // NOP:ed out CRC check
			system_type = SystemType::PAL; cic = CIC::CIC_NUS_7102; break;
		case UINT64_C(0x000000a0f26f62fe): 
		case UINT64_C(0x000000a0e96e72d4): // NOP:ed out CRC check
			system_type = SystemType::NTSC; cic = CIC::CIC_NUS_6101; break;
		case UINT64_C(0x000000a9229d7c45): 
		case UINT64_C(0x000000a9199c8c1b): // NOP:ed out CRC check
			cic = system_type == SystemType::NTSC
			? CIC::CIC_NUS_6103 
			: CIC::CIC_NUS_7103; break;
		case UINT64_C(0x000000f8b860ed00): 
		case UINT64_C(0x000000f8af5ffcd6): // NOP:ed out CRC check
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
	}

	printf("System: %d, CIC: %d\n", (int)system_type, (int)cic);
	printf("Auto-detect is on, updating OSD settings\n");

	user_io_status_set("[80:79]", (uint32_t)system_type);
	user_io_status_set("[68:65]", (uint32_t)cic);

	return is_known_cic;
}

static void md5_to_hex(uint8_t* in, char* out) {
	for (auto i = 0; i < 16; i++) {
		sprintf(out, "%02x", in[i]);
		out += 2;
	}
}

int n64_rom_tx(const char* name, unsigned char index) {
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

	// 0 = Nothing detected
	// 1 = System region and CIC detected
	// 2 = Found some ROM info in DB (Save type etc.), but System region and CIC has not been determined
	// 3 = Has detected everything, System type, CIC, Save type etc.
	uint8_t rom_settings_detected = 0;
	RomFormat rom_format;

	MD5Context ctx;
	MD5Init(&ctx);
	uint8_t md5[16];
	char md5_hex[40];
	uint64_t bootcode_sum = 0;
	char cart_id[8];

	while (bytes2send)
	{
		uint32_t chunk = (bytes2send > sizeof(buf)) ? sizeof(buf) : bytes2send;

		FileReadAdv(&f, buf, chunk);

		// perform sanity checks and detect ROM format
		if (is_first_chunk) {
			if (chunk < 4096) {
				printf("Failed to load ROM: must be at least 4096 bytes\n");
				return 0;
			}

			rom_format = detectRomFormat(buf);
			if (rom_format == RomFormat::UNKNOWN)
				printf("Unknown ROM format\n");
		}

		// normalize data to big-endian format
		normalizeData(buf, chunk, rom_format);

		MD5Update(&ctx, buf, chunk);

		if (is_first_chunk) {
			// Try to detect ROM settings based on header MD5 hash

			// For calculating the MD5 hash of the header, we need to make a
			// copy of the context before calling MD5Final, otherwise the file
			// hash will be incorrect lateron.
			MD5Context ctx_header;
			memcpy(&ctx_header, &ctx, sizeof(struct MD5Context));
			MD5Final(md5, &ctx_header);
			md5_to_hex(md5, md5_hex);
			printf("Header MD5: %s\n", md5_hex);

			rom_settings_detected = detect_rom_settings_in_dbs_with_md5(md5_hex);
			if (rom_settings_detected == 0)
				printf("No ROM information found for header hash: %s\n", md5_hex);

			// Calculate boot ROM checksum
			for (uint32_t i = 0x40 / sizeof(uint32_t); i < 0x1000 / sizeof(uint32_t); i++) {
				bootcode_sum += ((uint32_t*)buf)[i];
			}

			/* The first byte (starting at 0x3b) indicates the type of ROM 
			 *   'N' = cart
			 *   'D' = 64DD disk
			 *   'C' = cartridge part of expandable game
			 *   'E' = 64DD expansion for cart
			 *   'Z' = Aleck64 cart
			 * The second and third byte form a 2-letter ID for the game
			 * The fourth byte indicates the region and language for the game 
			 * The fifth byte indicates the revision of the game */

			strncpy(cart_id, (char*)(buf + 0x3b), 4);
			sprintf((char*)(cart_id + 4), "%02X", buf[0x3f]);
			printf("Cartridge ID: %s\n", cart_id);
		}

		user_io_file_tx_data(buf, chunk);

		if (use_progress) ProgressMessage("Loading", f.name, size - bytes2send, size);
		bytes2send -= chunk;
		is_first_chunk = false;
	}

	MD5Final(md5, &ctx);
	md5_to_hex(md5, md5_hex);
	printf("File MD5: %s\n", md5_hex);

	// Try to detect ROM settings from full file MD5 if they're are not detected yet
	if (rom_settings_detected == 0)
		rom_settings_detected = detect_rom_settings_in_dbs_with_md5(md5_hex);

	// Try to detect ROM settings by cart ID if they're are not detected yet
	if (rom_settings_detected == 0) {
		printf("No ROM information found for file hash: %s\n", md5_hex);
		rom_settings_detected = detect_rom_settings_in_dbs_with_cartid(cart_id);
		if (rom_settings_detected == 0)
			printf("No ROM information found for cart ID: %s\n", cart_id);
		// Try detect (partial) ROM settings by analyzing the ROM itself. (System region and CIC)
		if ((rom_settings_detected == 0 || rom_settings_detected == 2) &&
			detect_rom_settings_from_first_chunk(cart_id[3], bootcode_sum)) {
			rom_settings_detected |= 1;
		}
	}
	// Complement info found in DB with System region and CIC
	else if (rom_settings_detected == 2 && 
		detect_rom_settings_from_first_chunk(cart_id[3], bootcode_sum)) {
		rom_settings_detected = 3;
	}

	printf("Done.\n");
	FileClose(&f);

	// Mount save state
	char file_path[1024];
	FileGenerateSavePath(name, file_path);
	user_io_file_mount(file_path, 0, 1);

	// Signal end of transmission
	user_io_set_download(0);
	ProgressMessage(0, 0, 0, 0);

	if (rom_settings_detected == 0 || rom_settings_detected == 2) 
		Info("Auto-detect failed:\nUnknown CIC type.");
	else if (rom_settings_detected == 1) 
		Info("Auto-detect failed:\nROM missing from database,\nyou might not be able to save.", 5000);

	return 1;
}
