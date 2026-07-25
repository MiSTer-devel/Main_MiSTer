#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>
#include <vector>

#include "../../hardware.h"
#include "../../menu.h"
#include "../../osd.h"
#include "../../shmem.h"
#include "../../lib/md5/md5.h"

#include "miniz.h"
#include "n64.h"

#pragma push_macro("NONE")
#pragma push_macro("BIG_ENDIAN")
#pragma push_macro("LITTLE_ENDIAN")
#undef NONE
#undef BIG_ENDIAN
#undef LITTLE_ENDIAN

static constexpr auto RAM_SIZE = 0x800000U;
static constexpr auto CARTID_LENGTH = 6U; // Ex: NSME00
static constexpr auto MD5_LENGTH = 16U;
static constexpr auto CARTID_PREFIX = "ID:";
static constexpr auto RESET_OPT = "[0]";
static constexpr auto RELOAD_SAVE_OPT = "[40]";
static constexpr auto SAVE_BACKUP_OPT = "[41]";
static constexpr auto AUTOSAVE_OPT = "[42]";
static constexpr auto AUTODETECT_OPT = "[64]";
static constexpr auto CIC_TYPE_OPT = "[68:65]";
static constexpr auto NO_EPAK_OPT = "[70]";
static constexpr auto CPAK_OPT = "[71]";
static constexpr auto RPAK_OPT = "[72]";
static constexpr auto TPAK_OPT = "[73]";
static constexpr auto RTC_OPT = "[74]";
static constexpr auto SAVE_TYPE_OPT = "[77:75]";
static constexpr auto SYS_TYPE_OPT = "[80:79]";
static constexpr auto AUTOPAK_OPT = "[81]";
static constexpr auto PATCHES_OPT = "[90]";
static constexpr auto CHEATS_OPT = "[103]";
static constexpr auto DD_DEVELOPMENT_OPT = "[107]";
static constexpr const char* const CONTROLLER_OPTS[] = { "[51:49]", "[54:52]", "[57:55]", "[60:58]" };

static constexpr size_t CPAK_PAGE_SIZE = 256; // N64 controller pak page size
static constexpr size_t CPAK_ID_SECTOR_PAGE = 0;
static constexpr size_t CPAK_FAT_PAGE = 1;
static constexpr size_t CPAK_FAT_BACKUP_PAGE = 2;

// Page 0 is divided into 8 slots of 32 bytes each.
// The N64 hardware requires the ID info to be repeated in specific slots.
static constexpr size_t CPAK_ID_ENTRY_SIZE = 32;
static constexpr size_t CPAK_ID_ENTRY_1_OFFSET = CPAK_ID_ENTRY_SIZE * 1;
static constexpr size_t CPAK_ID_ENTRY_2_OFFSET = CPAK_ID_ENTRY_SIZE * 3;
static constexpr size_t CPAK_ID_ENTRY_3_OFFSET = CPAK_ID_ENTRY_SIZE * 4;
static constexpr size_t CPAK_ID_ENTRY_4_OFFSET = CPAK_ID_ENTRY_SIZE * 6;

// Simple hash function, see: https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
// (Modified to make it case-insensitive)
static constexpr uint64_t FNV_PRIME = 0x100000001b3;
static constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325;
static constexpr uint64_t fnv_hash(const char* s, uint64_t h = FNV_OFFSET_BASIS) {
	if (s) while (uint8_t a = *(s++)) h = (h ^ ((a >= 'A') && (a <= 'Z') ? a + ('a' - 'A') : a)) * FNV_PRIME;
	return h;
}

enum class MemoryType : uint32_t {
	NONE = 0,
	EEPROM_512,
	EEPROM_2k,
	SRAM_32k,
	SRAM_96k,
	FLASH_128k,
	CPAK = ~2U,
	TPAK = ~1U,
	UNKNOWN = ~0U
};

enum class CIC : uint32_t {
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
	CIC_NUS_DDUS,
	CIC_NUS_5101,
	UNKNOWN = ~0U
};

enum class SystemType : uint32_t {
	NTSC = 0,
	PAL,
	UNKNOWN = ~0U
};

enum class ByteOrder : uint32_t {
	BIG_ENDIAN = 0,
	BYTE_SWAPPED,
	LITTLE_ENDIAN,
	UNKNOWN = ~0U
};

enum class PadType : uint32_t {
	N64_PAD = 0,
	UNPLUGGED,
	N64_PAD_WITH_CPAK,
	N64_PAD_WITH_RPAK,
	SNAC,
	N64_PAD_WITH_TPAK,
	KEYBOARD,
	MOUSE,
	UNKNOWN = ~0U
};

enum class AutoDetect : uint32_t {
	ON = 0,
	OFF
};

enum class ComparisionType : uint8_t {
	OPTYPE_ALWAYS = 0,
	OPTYPE_EQUALS = 0x1,
	OPTYPE_GREATER = 0x2,
	OPTYPE_LESS = 0x3,
	OPTYPE_GREATER_EQ = 0x4,
	OPTYPE_LESS_EQ = 0x5,
	OPTYPE_NOT_EQ = 0x6,
	OPTYPE_EMPTY = 0xf
};

struct cheat_code_flags {
	enum ComparisionType cmp_type : 4;
	uint8_t cmp_mask : 4;
	bool is_boot_code : 1;
	bool is_boot_code_executed : 1;
	bool is_gs_button_code : 1;
	bool is_gs_button_pressed : 1;
	uint32_t _unused : 20;
} __attribute__((packed));

struct cheat_code {
	struct cheat_code_flags flags;
	uint32_t address;
	uint32_t compare;
	uint32_t replace;
};

struct patch_data {
	uint32_t address;
	uint32_t diff;
	bool turbo_core_only;
};

static uint8_t loaded = 0;
static char current_rom_path[1024] = { '\0' };
static char current_rom_path_gb[1024] = { '\0' };
static char current_dd_disk_path[1024] = { '\0' };
static char current_dd_ram_save_path[1024] = { '\0' };
static uint32_t current_dd_load_addr = 0;
static uint8_t current_dd_disk_loaded = 0;
static uint8_t current_dd_skip_initial_osd_save = 0;
static uint8_t current_dd_cart_expansion = 0;
static uint8_t current_dd_disk_type = 0;
static char current_dd_disk_id[5] = {};
static char old_save_path[1024];
static void* rdram_ptr = nullptr;
static cheat_code* cheat_codes = nullptr;
static uint32_t cheat_codes_count = 0;
static patch_data patches[256];
static int is_turbo_core_type = 0;

static const char* stringify(MemoryType v) {
	switch (v) {
	case MemoryType::EEPROM_512: return "4K EEPROM";
	case MemoryType::EEPROM_2k: return "16K EEPROM";
	case MemoryType::SRAM_32k: return "256K SRAM";
	case MemoryType::SRAM_96k: return "768K SRAM";
	case MemoryType::FLASH_128k: return "Flash RAM";
	case MemoryType::CPAK: return "CPAK DATA";
	case MemoryType::TPAK: return "TPAK DATA";
	default: return "(none)";
	}
}

static size_t get_save_size(MemoryType v) {
	switch (v) {
	case MemoryType::EEPROM_512: return 0x200;
	case MemoryType::EEPROM_2k: return 0x800;
	case MemoryType::SRAM_32k: return 0x8000;
	case MemoryType::SRAM_96k: return 0x18000;
	case MemoryType::FLASH_128k: return 0x20000;
	case MemoryType::CPAK:
	case MemoryType::TPAK: return 0x8000; // 32 KiByte
	default: return 0;
	}
}

static const char* stringify(CIC v) {
	switch (v) {
	case CIC::CIC_NUS_6101: return "6101";
	case CIC::CIC_NUS_6102: return "6102";
	case CIC::CIC_NUS_7101: return "7101";
	case CIC::CIC_NUS_7102: return "7102";
	case CIC::CIC_NUS_6103: return "6103";
	case CIC::CIC_NUS_7103: return "7103";
	case CIC::CIC_NUS_6105: return "6105";
	case CIC::CIC_NUS_7105: return "7105";
	case CIC::CIC_NUS_6106: return "6106";
	case CIC::CIC_NUS_7106: return "7106";
	case CIC::CIC_NUS_8303: return "8303";
	case CIC::CIC_NUS_8401: return "8401";
	case CIC::CIC_NUS_5167: return "5167";
	case CIC::CIC_NUS_DDUS: return "DDUS";
	case CIC::CIC_NUS_5101: return "5101";
	default: return "Unknown";
	}
}

static const char* stringify(SystemType v) {
	switch (v) {
	case SystemType::NTSC: return "NTSC";
	case SystemType::PAL: return "PAL";
	default: return "Unknown";
	}
}

static const char* stringify(bool v) {
	return v
		? "Yes"
		: "No";
}

static ByteOrder detect_rom_endianness(const uint8_t* data) {
	// Data should be aligned
	const uint32_t val = *(uint32_t*)data;

	/* The following checks assume we're on a little-endian platform.
	   For each check, the first value is for regular ROMs, the 2nd is for 64DD images and
	   the third is a malformed "word" used in some homebrew(?) */
	switch (val) {
	case UINT32_C(0x40123780):
	case UINT32_C(0x40072780):
	case UINT32_C(0x41123780):
		return ByteOrder::BIG_ENDIAN;
	case UINT32_C(0x12408037):
	case UINT32_C(0x07408027):
	case UINT32_C(0x12418037):
		return ByteOrder::BYTE_SWAPPED;
	case UINT32_C(0x80371240):
	case UINT32_C(0x80270740):
	case UINT32_C(0x80371241):
		return ByteOrder::LITTLE_ENDIAN;
	default:
		break;
	}

	// Endianness could not be determined, use just first byte and hope for the best.
	switch (val & 0xff) {
	case 0x80:
		return ByteOrder::BIG_ENDIAN;
	case 0x37:
	case 0x27:
		return ByteOrder::BYTE_SWAPPED;
	case 0x40:
	case 0x41:
		return ByteOrder::LITTLE_ENDIAN;
	default:
		return ByteOrder::UNKNOWN;
	}
}

static void normalize_data(uint8_t* data, size_t size, ByteOrder endianness) {
	static uint8_t temp0, temp1, temp2;
	switch (endianness) {
	case ByteOrder::BYTE_SWAPPED:
		for (size_t i = 0; i < (size & ~1U); i += 2) {
			temp0 = data[0];
			data[0] = data[1];
			data[1] = temp0;
			data += 2;
		}
		break;
	case ByteOrder::LITTLE_ENDIAN:
		for (size_t i = 0; i < (size & ~3U); i += 4) {
			temp0 = data[0];
			temp1 = data[1];
			temp2 = data[2];
			data[0] = data[3];
			data[1] = temp2;
			data[2] = temp1;
			data[3] = temp0;
			data += 4;
		}
		break;
	default:
		// Do nothing
		break;
	}
}

static MemoryType get_cart_save_type() {
	auto v = (MemoryType)user_io_status_get(SAVE_TYPE_OPT);
	return (get_save_size(v) ? v : MemoryType::NONE);
}

static void set_cart_save_type(MemoryType v) {
	user_io_status_set(SAVE_TYPE_OPT,
		(uint32_t)(get_save_size(v) ? v : MemoryType::NONE));
}

static uint32_t get_save_offset(unsigned char idx) {
	uint32_t offset = 0;
	MemoryType save_type = get_cart_save_type();

	if (idx && (save_type != MemoryType::NONE)) {
		offset += get_save_size(save_type);
		idx--;
	}

	if (idx && (bool)user_io_status_get(TPAK_OPT)) {
		offset += get_save_size(MemoryType::TPAK);
		idx--;
	}

	offset += get_save_size(MemoryType::CPAK) * idx;

	return offset;
}

// *** Controller PAK stuff, heavily influenced by Libdragon's "cpaktool" ***

// Minimalistic CPAK formatter/initializer.
static void cpak_calc_id_crc(uint8_t* id_block) {
	uint16_t sum = 0;
	// Sum first 14 half-words.
	// The overflowing is done on purpose.
	for (size_t i = 0; i < 14; i++) {
		sum += (id_block[i << 1] << 8) | id_block[(i << 1) | 1];
	}

	id_block[0x1c] = sum >> 8;
	id_block[0x1d] = sum & 0xff;

	// Inverted checksum.
	// The N64 expects (0xfff2 - sum), NOT (~sum). Weird.
	const uint16_t inv_sum = 0xfff2 - sum;
	id_block[0x1e] = inv_sum >> 8;
	id_block[0x1f] = inv_sum & 0xff;
}

static uint8_t cpak_calc_fat_crc(const uint8_t* page_data) {
	uint8_t sum = 0;
	// The overflowing is done on purpose.
	for (size_t i = 2; i < CPAK_PAGE_SIZE; i++) {
		sum += page_data[i];
	}
	return sum;
}

static void cpak_format(uint8_t* data) {
	// Initialize the seed() function.
	uint32_t seed;
	int32_t rndfd = open("/dev/urandom", O_RDONLY);
	read(rndfd, &seed, sizeof(seed));
	close(rndfd);
	srand(seed);
	
	memset(data, 0, get_save_size(MemoryType::CPAK));
	uint8_t id_template[CPAK_ID_ENTRY_SIZE];
	memset(id_template, 0, CPAK_ID_ENTRY_SIZE);

	// (0x00-0x17) contains a serial number,
	// unique for that controller pak.
	// We fill (0x00-0x10) with random data.
	for (size_t i = 0; i < 0x10; i++) {
		id_template[i] = (uint8_t)(rand() & 0xff);
	}

	// The rest of the serial is some deterministic branding. :)
	memcpy(&id_template[0x10], "MiSTer", 6);

	id_template[0x19] = 0x81; // Device ID. (controller pak)
	id_template[0x1a] = 0x01; // 1 bank

	cpak_calc_id_crc(id_template);

	// Write ID block to the 4 specified locations in page 0.
	const size_t p0_base = CPAK_ID_SECTOR_PAGE * CPAK_PAGE_SIZE;
	memcpy(data + p0_base + CPAK_ID_ENTRY_1_OFFSET, id_template, CPAK_ID_ENTRY_SIZE);
	memcpy(data + p0_base + CPAK_ID_ENTRY_2_OFFSET, id_template, CPAK_ID_ENTRY_SIZE);
	memcpy(data + p0_base + CPAK_ID_ENTRY_3_OFFSET, id_template, CPAK_ID_ENTRY_SIZE);
	memcpy(data + p0_base + CPAK_ID_ENTRY_4_OFFSET, id_template, CPAK_ID_ENTRY_SIZE);

	uint8_t fat_page[CPAK_PAGE_SIZE];
	memset(fat_page, 0, CPAK_PAGE_SIZE);

	// Init the user entries (index 5 to 127) to (0x00, 0x03), repeating.
	for (size_t i = 5; i < (CPAK_PAGE_SIZE >> 1); i++) {
		fat_page[(i << 1) | 1] = 0x03;
	}

	fat_page[1] = cpak_calc_fat_crc(fat_page);

	memcpy(data + (CPAK_FAT_PAGE * CPAK_PAGE_SIZE), fat_page, CPAK_PAGE_SIZE);
	memcpy(data + (CPAK_FAT_BACKUP_PAGE * CPAK_PAGE_SIZE), fat_page, CPAK_PAGE_SIZE);
}

static char full_path[1024];
static uint8_t save_file_buf[0x20000]; // Largest save size
static uint8_t mounted_save_files = 0;

static size_t create_file(const char* filename, const uint8_t* data, size_t sz) {
	snprintf(full_path, sizeof(full_path), "%s/%s", getRootDir(), filename);
	FILE* fp = fopen(full_path, "w");
	if (!fp) return 0;
	sz = fwrite(data, 1, sz, fp);
	fclose(fp);
	return sz;
}

static size_t read_file(const char* filename, uint8_t* data, uint32_t offset, size_t sz) {
	snprintf(full_path, sizeof(full_path), "%s/%s", getRootDir(), filename);
	FILE* fp = fopen(full_path, "r");
	if (!fp) return 0;
	fseek(fp, 0L, SEEK_END);
	size_t fs = ftell(fp);
	if (fs > sizeof(save_file_buf)) {
		fs = sizeof(save_file_buf);
	}
	if (offset >= fs) {
		fclose(fp);
		return 0;
	}
	if (offset + sz > fs) {
		sz = fs - offset;
	}
	fseek(fp, offset, SEEK_SET);
	sz = fread(data, 1, sz, fp);
	fclose(fp);

	return sz;
}

// Returns true if all elements in the given array is 0x00 or 0xff.
static bool is_empty(const uint8_t* arr, size_t sz) {
	if (!sz) {
		return true;
	}
	const uint8_t head = *(arr++);
	if ((head != 0x00) && (head != 0xff)) {
		return false;
	}
	for (; --sz > 0; arr++) {
		if (head != *arr) {
			return false;
		}
	}
	return true;
}

// Shouldn't be possible to allocate more than 5 save files right now...
static N64SaveFile* save_files[8] = { nullptr };

struct N64SaveFile {
	int idx;
	MemoryType type;

	fileTYPE* get_image() const {
		return ((this->idx >= 0) && (this->idx < (int)(sizeof(save_files) / sizeof(*save_files)))) ? (fileTYPE*)::get_image(this->idx) : nullptr;
	}

	bool is_mounted() const {
		fileTYPE* image;
		return (image = this->get_image()) && image->filp;
	}

	bool needs_byteswap() const {
		return (this->type == MemoryType::CPAK || this->type == MemoryType::TPAK);
	}

	size_t get_size() const {
		return is_mounted() ? (size_t)this->get_image()->size : get_save_size(this->type);
	}

	// Return "true" if target is fine
	bool copy_file(const char* path, const char* to_path, bool overwrite, bool resize = false) const {
		if (!FileExists(path, 0)) {
			return false;
		}

		size_t expected_sz = get_save_size(this->type);
		size_t sz = read_file(path, save_file_buf, 0, resize ? expected_sz : sizeof(save_file_buf));

		if (FileExists(to_path, 0) && (!overwrite || is_empty(save_file_buf, sz))) {
			return true;
		}

		if (resize && (sz < expected_sz)) {
			// Pad file
			memset(save_file_buf + sz, 0, expected_sz - sz);
			sz = expected_sz;
		}

		if (!create_file(to_path, save_file_buf, sz)) {
			return false;
		}

		printf("Copied save data \"%s\" to \"%s\". (%u bytes)\n", path, to_path, sz);
		return true;
	}

	// Return "true" if created
	bool create_if_missing(const char* path, const char* old_path) {
		if (FileExists(path, 0)) {
			return false;
		}

		size_t sz = this->get_size();
		bool found_old_data = false;

		if (sz && FileExists(old_path, 0)) {
			uint32_t off = get_save_offset((this->idx < 0) ? mounted_save_files : this->idx);
			size_t read_bytes;
			if ((read_bytes = read_file(old_path, save_file_buf, off, sz)) && !is_empty(save_file_buf, sz)) {
				printf("Found old save data \"%s\", converting to %s.\n", old_path, stringify(type));
				found_old_data = true;
				if (this->needs_byteswap()) {
					normalize_data(save_file_buf, read_bytes, ByteOrder::LITTLE_ENDIAN);
				}
				if (read_bytes < sz) {
					memset(save_file_buf + read_bytes, 0xff, (sz - read_bytes));
				}
			}
		}

		if (!found_old_data) {
			if (this->type == MemoryType::CPAK) {
				cpak_format(save_file_buf);
			}
			else {
				memset(save_file_buf, 0xff, sz);
			}
		}

		return create_file(path, save_file_buf, sz);
	}

	void mount(const char* path) {
		int pre_size = get_save_size(this->type);
		if (!pre_size) return;
		uint8_t idx = mounted_save_files++;
		if (idx >= (sizeof(save_files) / sizeof(*save_files))) return;
		if (save_files[idx] && save_files[idx]->is_mounted()) {
			save_files[idx]->unmount();
			delete save_files[idx];
			save_files[idx] = nullptr;
		}
		user_io_file_mount(path, idx, 1, pre_size);
		this->idx = idx;
		save_files[idx] = this;
	}

	void mount(const char* path, const char* old_path) {
		this->create_if_missing(path, old_path);
		this->mount(path);
	}

	void unmount() {
		if (!this->is_mounted()) return;
		printf("Unmounting %s save file at %d slot.\n", stringify(this->type), this->idx);
		user_io_file_mount("", this->idx);
	}

	N64SaveFile(MemoryType type) {
		this->idx = -1; // Unmounted
		this->type = type;
	}
};

static bool is_auto() {
	return (AutoDetect)user_io_status_get(AUTODETECT_OPT) == AutoDetect::ON;
}

static bool is_autopak() {
	return !user_io_status_get(AUTOPAK_OPT);
}

static bool is_turbo_core() {
	if (!is_turbo_core_type) {
		is_turbo_core_type = 2;

		user_io_read_confstr();
		for (size_t i = 2; ; i++) {
			char *p = user_io_get_confstr(i);
			if (!p) break;
			if (strncasecmp(p, "TURBO", 5) == 0) {
				is_turbo_core_type = 1;
				break;
			}
		}
	}

	return is_turbo_core_type == 1;
}

static bool cheats_enabled() {
	return !user_io_status_get(CHEATS_OPT);
}

static void cheats_disable() {
	user_io_status_set(CHEATS_OPT, 1);
}

static bool patches_enabled() {
	return !user_io_status_get(PATCHES_OPT);
}

static uint8_t hex_to_dec(const char x) {
	if (x >= '0' && x <= '9') return (x - '0');
	if (x >= 'A' && x <= 'F') return (x - 'A') + 10;
	if (x >= 'a' && x <= 'f') return (x - 'a') + 10;
	return 0;
}

static void trim(char* out, size_t max_len, const char* str)
{
	if (!*str || !max_len) {
		*out = '\0';
		return;
	}

	const char* end;
	size_t out_size = max_len;

	// Trim leading space
	while (isspace(*str)) {
		if (!(--out_size)) {
			*out = '\0';
			return;
		}
		str++;
	}

	// All spaces?
	if (!*str) {
		*out = '\0';
		return;
	}

	// Trim trailing space
	end = str + strnlen(str, out_size) - 1;
	while (end > str && isspace(*end)) {
		end--;
	}
	end++;

	// Set output size to minimum of trimmed string length and buffer size minus 1
	out_size = (size_t)(end - str) < (max_len - 1) ? (end - str) : max_len - 1;

	// Copy trimmed string and add null terminator
	memcpy(out, str, out_size);
	out[out_size] = '\0';

	// Obfuscate illegal characters
	for (size_t i = 0; (i < out_size) && out[i]; i++) {
		if ((out[i] >= 0x20) && (out[i] < 0xa0)) {
			continue;
		}
		out[i] = '?';
	}
}

// Returns true if CIC and System Region is detected, or if auto-detection is turned off
static bool parse_and_apply_db_tags(char* tags) {
	if (!tags) return false;

	const char* separator = "| ";
	MemoryType save_type = MemoryType::NONE;
	SystemType system_type = SystemType::UNKNOWN;
	CIC cic_type = CIC::UNKNOWN;
	bool no_epak = false;
	bool cpak = false;
	bool rpak = false;
	bool tpak = false;
	bool rtc = false;
	int p_match;
	uint32_t p_addr, p_diff;
	char p_tag;

	PadType prefered_pad = PadType::N64_PAD;

	patch_data* patch = patches;

	for (char* tag = strtok(tags, separator); tag; tag = strtok(nullptr, separator)) {
		// Patch tag
		if ((p_match = sscanf(tag, "%*[Pp]%8x:%8x%*[:]%c", &p_addr, &p_diff, &p_tag)) >= 2) {
			printf("Patch: 0x%08x:0x%08x", p_addr, p_diff);
			if (p_match >= 3) printf(":%c", p_tag);
			printf("\n");
			patch->address = p_addr;
			patch->diff = p_diff;
			patch->turbo_core_only = p_match >= 3 && (p_tag == 'T' || p_tag == 't');
			patch++;
			continue;
		}

		switch (fnv_hash(tag)) {
		case fnv_hash("eeprom512"): save_type = MemoryType::EEPROM_512; break;
		case fnv_hash("eeprom2k"): save_type = MemoryType::EEPROM_2k; break;
		case fnv_hash("sram32k"): save_type = MemoryType::SRAM_32k; break;
		case fnv_hash("sram96k"): save_type = MemoryType::SRAM_96k; break;
		case fnv_hash("flash128k"): save_type = MemoryType::FLASH_128k; break;
		case fnv_hash("noepak"): no_epak = true; break;
		case fnv_hash("cpak"): cpak = true; if (prefered_pad == PadType::N64_PAD) prefered_pad = PadType::N64_PAD_WITH_CPAK; break;
		case fnv_hash("rpak"): rpak = true; if (prefered_pad == PadType::N64_PAD) prefered_pad = PadType::N64_PAD_WITH_RPAK; break;
		case fnv_hash("tpak"): tpak = true; if (prefered_pad == PadType::N64_PAD) prefered_pad = PadType::N64_PAD_WITH_TPAK; break;
		case fnv_hash("rtc"): rtc = true; break;
		case fnv_hash("ntsc"): ntsc: system_type = SystemType::NTSC; break;
		case fnv_hash("pal"): pal: system_type = SystemType::PAL; break;
		case fnv_hash("cic6101"): cic_type = CIC::CIC_NUS_6101; goto ntsc;
		case fnv_hash("cic6102"): cic_type = CIC::CIC_NUS_6102; goto ntsc;
		case fnv_hash("cic6103"): cic_type = CIC::CIC_NUS_6103; goto ntsc;
		case fnv_hash("cic6105"): cic_type = CIC::CIC_NUS_6105; goto ntsc;
		case fnv_hash("cic6106"): cic_type = CIC::CIC_NUS_6106; goto ntsc;
		case fnv_hash("cic7101"): cic_type = CIC::CIC_NUS_7101; goto pal;
		case fnv_hash("cic7102"): cic_type = CIC::CIC_NUS_7102; goto pal;
		case fnv_hash("cic7103"): cic_type = CIC::CIC_NUS_7103; goto pal;
		case fnv_hash("cic7105"): cic_type = CIC::CIC_NUS_7105; goto pal;
		case fnv_hash("cic7106"): cic_type = CIC::CIC_NUS_7106; goto pal;
		case fnv_hash("cic8303"): cic_type = CIC::CIC_NUS_8303; break;
		case fnv_hash("cic8401"): cic_type = CIC::CIC_NUS_8401; break;
		case fnv_hash("cic5167"): cic_type = CIC::CIC_NUS_5167; break;
		case fnv_hash("cicddus"): cic_type = CIC::CIC_NUS_DDUS; break;
		case fnv_hash("cic5101"): cic_type = CIC::CIC_NUS_5101; break;
		default: printf("Unknown tag: [%s] (skipping)\n", tag); break;
		}
	}

	if (system_type == SystemType::UNKNOWN && cic_type != CIC::UNKNOWN) {
		system_type = SystemType::NTSC;
	}

	printf("Region: %s, Save Type: %s, CIC: %s, CPak: %s, RPak: %s, TPak %s, RTC: %s, Mem: %uMiB\n",
		stringify(system_type),
		stringify(save_type),
		stringify(cic_type),
		stringify(cpak),
		stringify(rpak),
		stringify(tpak),
		stringify(rtc),
		(no_epak ? 4 : 8));

	if (!is_auto()) {
		printf("Auto-detect is OFF, not updating OSD settings.\n");
		return true;
	}

	printf("Auto-detect is ON, updating OSD settings.\n");

	if (system_type != SystemType::UNKNOWN) user_io_status_set(SYS_TYPE_OPT, (uint32_t)system_type);
	if (cic_type != CIC::UNKNOWN) user_io_status_set(CIC_TYPE_OPT, (uint32_t)cic_type);

	user_io_status_set(NO_EPAK_OPT, (uint32_t)no_epak);
	user_io_status_set(CPAK_OPT, (uint32_t)cpak);
	user_io_status_set(RPAK_OPT, (uint32_t)rpak);
	user_io_status_set(TPAK_OPT, (uint32_t)tpak);
	user_io_status_set(RTC_OPT, (uint32_t)rtc);
	set_cart_save_type(save_type);

	auto pad0 = (PadType)user_io_status_get(CONTROLLER_OPTS[0]);
	if (is_autopak() && pad0 != PadType::SNAC && pad0 != PadType::KEYBOARD && pad0 != PadType::MOUSE) {
		user_io_status_set(CONTROLLER_OPTS[0], (uint32_t)prefered_pad);
	}

	return (system_type != SystemType::UNKNOWN && cic_type != CIC::UNKNOWN);
}

static bool md5_matches(const char* line, const char* md5) {
	for (size_t i = MD5_LENGTH * 2; i > 0; --i, line++, md5++) {
		int c = *line;
		if (!c || *md5 != tolower(c)) {
			return false;
		}
	}

	return true;
}

// Returns numbers of matching characters if match, otherwize 0
static size_t cart_id_is_match(const char* line, const char* cart_id) {
	const auto prefix_len = strlen(CARTID_PREFIX);

	// A valid ID line should start with "ID:"
	if (strncmp(line, CARTID_PREFIX, prefix_len)) {
		return 0;
	}

	// Skip the line if it doesn't match our cart_id, '_' = don't care
	const char* lp = line + prefix_len;
	for (size_t i = 0; i < CARTID_LENGTH && *lp; i++, lp++) {
		if (i && isspace(*lp)) {
			return i; // Early termination
		}

		if (*lp != '_' && *lp != cart_id[i]) {
			return 0; // Character didn't match pattern
		}
	}

	return CARTID_LENGTH;
}

static uint8_t detect_rom_settings_in_db(const char* lookup_hash, const char* db_file_name) {
	fileTextReader reader = {};

	snprintf(full_path, sizeof(full_path), "%s/%s", HomeDir(), db_file_name);

	if (!FileOpenTextReader(&reader, full_path)) {
		printf("Failed to open N64 data file \"%s\".\n", db_file_name);
		return 0;
	}

	while (const char* line = FileReadLine(&reader)) {
		// Skip the line if it doesn't start with our hash
		if (!md5_matches(line, lookup_hash)) continue;

		const char* s = line + (MD5_LENGTH * 2);
		char* tags = new char[strlen(s) + 1];
		if (sscanf(s, "%*[ \t]%[^#;]", tags) <= 0) {
			printf("Found ROM entry for MD5 %s, but the tag was malformed! (%s)\n", lookup_hash, s);
			return 2;
		}

		printf("Found ROM entry for MD5 %s: [%s]\n", lookup_hash, tags);

		// 2 = System region and/or CIC wasn't in DB, will need further detection
		return parse_and_apply_db_tags(tags) ? 3 : 2;
	}

	return 0;
}

static uint8_t detect_rom_settings_in_db_with_cartid(const char* cart_id, const char* db_file_name) {
	fileTextReader reader = {};

	snprintf(full_path, sizeof(full_path), "%s/%s", HomeDir(), db_file_name);

	if (!FileOpenTextReader(&reader, full_path)) {
		printf("Failed to open N64 data file \"%s\".\n", db_file_name);
		return 0;
	}

	while (const char* line = FileReadLine(&reader)) {
		// Skip lines that doesn't start with our ID
		size_t i;
		if (!(i = cart_id_is_match(line, cart_id))) continue;

		auto s = line + strlen(CARTID_PREFIX) + i;
		auto tags = new char[strlen(s) + 1];
		if (sscanf(s, "%*[ \t]%[^#;]", tags) <= 0) {
			printf("Found ROM entry for ID [%s], but the tag was malformed! \"%s\".\n", cart_id, s);
			return 2;
		}

		printf("Found ROM entry for ID [%s]: \"%s\".\n", cart_id, tags);

		// 2 = System region and/or CIC wasn't in DB, will need further detection
		return parse_and_apply_db_tags(tags) ? 3 : 2;
	}

	return 0;
}

static const char* DB_FILE_NAMES[] = {
	"N64-database_user.txt",
	"N64-database.txt"
};

static uint8_t detect_rom_settings_in_dbs_with_md5(const char* lookup_hash) {
	uint8_t detected = 0;
	for (auto i = 0U; i < (sizeof(DB_FILE_NAMES) / sizeof(*DB_FILE_NAMES)); i++) {
		if ((detected = detect_rom_settings_in_db(lookup_hash, DB_FILE_NAMES[i]))) {
			break;
		}
	}

	return detected;
}

static uint8_t detect_rom_settings_in_dbs_with_cartid(const char* lookup_id) {
	// Check if all characters in the lookup are valid
	for (size_t i = 0; i < CARTID_LENGTH; i++) {
		auto c = lookup_id[i];
		if (isalnum(c)) {
			continue;
		}

		printf("Not a valid Cart ID: [%s]!\n", lookup_id);
		return 0;
	}

	uint8_t detected = 0;
	for (auto i = 0U; i < (sizeof(DB_FILE_NAMES) / sizeof(*DB_FILE_NAMES)); i++) {
		if ((detected = detect_rom_settings_in_db_with_cartid(lookup_id, DB_FILE_NAMES[i]))) {
			break;
		}
	}

	return detected;
}

// "Advanced" Homebrew ROM Header https://n64brew.dev/wiki/ROM_Header
static bool detect_homebrew_header(const uint8_t* controller_settings, const char* cart_id) {
	if ((cart_id[1] != 'E') || (cart_id[2] != 'D')) return false;

	printf("Detected Advanced Homebrew ROM Header, how fancy!\n");

	if (!is_auto()) {
		printf("Auto-detect is OFF, not updating OSD settings.\n");
		return false;
	}

	switch (hex_to_dec(cart_id[4])) {
	case 1:
		set_cart_save_type(MemoryType::EEPROM_512);
		break;
	case 2:
		set_cart_save_type(MemoryType::EEPROM_2k);
		break;
	case 3:
		set_cart_save_type(MemoryType::SRAM_32k);
		break;
	case 4:
		set_cart_save_type(MemoryType::SRAM_96k);
		break;
	case 5:
		set_cart_save_type(MemoryType::FLASH_128k);
		break;
		//case 6:
		//	set_cart_save_type(MemoryType::SRAM_128k);
		//	break;
	default:
		set_cart_save_type(MemoryType::NONE);
		break;
	}

	printf("Auto-detect is ON, updating OSD settings.\n");

	user_io_status_set(RTC_OPT, (uint32_t)(hex_to_dec(cart_id[5]) & 1)); // RTC

	user_io_status_set(RPAK_OPT, (uint32_t)(
		(controller_settings[0] == 0x01) ||
		(controller_settings[1] == 0x01) ||
		(controller_settings[2] == 0x01) ||
		(controller_settings[3] == 0x01) ? 1 : 0)); // Rumble Pak

	user_io_status_set(CPAK_OPT, (uint32_t)(
		(controller_settings[0] == 0x02) ||
		(controller_settings[1] == 0x02) ||
		(controller_settings[2] == 0x02) ||
		(controller_settings[3] == 0x02) ? 1 : 0)); // Controller Pak

	user_io_status_set(TPAK_OPT, (uint32_t)(controller_settings[0] == 0x03 ? 1 : 0)); // Transfer Pak is P1 only

	if (!is_autopak()) return true;

	size_t c_idx = 0;
	for (auto c_opt : CONTROLLER_OPTS) {
		auto pad_type = (PadType)user_io_status_get(c_opt);
		if (controller_settings[c_idx] && pad_type != PadType::SNAC && pad_type != PadType::KEYBOARD && pad_type != PadType::MOUSE) {
			if (controller_settings[c_idx] < 0x80) {
				user_io_status_set(c_opt, (uint32_t)(
					(controller_settings[c_idx] == 0x01) ? PadType::N64_PAD_WITH_RPAK :
					(controller_settings[c_idx] == 0x02) ? PadType::N64_PAD_WITH_CPAK :
					(controller_settings[c_idx] == 0x03) && (c_idx == 0) ? PadType::N64_PAD_WITH_TPAK :
					PadType::N64_PAD));
			}
			else if (controller_settings[c_idx] == 0xff) {
				user_io_status_set(c_opt, (uint32_t)PadType::UNPLUGGED);
			}
		}
		c_idx++;
	}

	return true;
}

static bool detect_rom_settings_from_first_chunk(const char region_code, const uint64_t* signatures, size_t sig_len) {
	SystemType system_type;
	CIC cic = CIC::UNKNOWN;
	bool is_known_signature = true;

	switch (region_code) {
	case 'D': // Germany
	case 'F': // France
	case 'H': // Netherlands
	case 'I': // Italy
	case 'L': // Gateway 64 (PAL)
	case 'P': // Europe
	case 'S': // Spain
	case 'U': // Australia
	case 'W': // Scandinavia
	case 'X': // Europe
	case 'Y': // Europe
	case 'Z': // Europe
		system_type = SystemType::PAL; break;
	case 'A': // Asia (NTSC, used for 1080 US/JP)
	case 'B': // Brazil
	case 'C': // China
	case 'E': // North America
	case 'G': // Gateway 64 (NTSC)
	case 'J': // Japan
	case 'K': // Korea
	case 'N': // Canada
	default:
		system_type = SystemType::NTSC; break;
	}

	if (sig_len) do {
		switch (*signatures) {
		default:
			if (--sig_len) {
				signatures++;
				break;
			}
			printf("Unknown CIC signature: 0x%016llx, uses default.\n", *signatures);
			is_known_signature = false;
			// Fall through
		case UINT64_C(0x000000a316adc55a): // CIC-6102/7101 IPL3
		case UINT64_C(0x000000a30dacd530): // NOP:ed out CRC check
		case UINT64_C(0x000000039c981107): // hcs64's CIC-6102 IPL3 replacement
		case UINT64_C(0x000000d2828281b0): // Unknown. Used in some homebrew
		case UINT64_C(0x000000d2531456f6): // Unknown. Used in some homebrew
		case UINT64_C(0x000000d28bc5f6ae): // Unknown. Used in some homebrew
		case UINT64_C(0x000000d2be3c4486): // Xeno Crisis custom IPL3
		case UINT64_C(0x0000009acc31e644): // HW1 IPL3 (Turok E3 prototype)
		case UINT64_C(0x0000009474732e6b): // IPL3 re-assembled with the GNU assembler (iQue)
			cic = (system_type != SystemType::PAL) ? CIC::CIC_NUS_6102 : CIC::CIC_NUS_7101; break;
		case UINT64_C(0x000000a405397b05): // CIC-7102 IPL3
		case UINT64_C(0x000000a3fc388adb): // NOP:ed out CRC check
			system_type = SystemType::PAL; cic = CIC::CIC_NUS_7102; break;
		case UINT64_C(0x000000a0f26f62fe): // CIC-6101 IPL3
		case UINT64_C(0x000000a0e96e72d4): // NOP:ed out CRC check
			system_type = SystemType::NTSC; cic = CIC::CIC_NUS_6101; break;
		case UINT64_C(0x000000a9229d7c45): // CIC-x103 IPL3
		case UINT64_C(0x000000a9199c8c1b): // NOP:ed out CRC check
		case UINT64_C(0x000000271316d406): // All zeros bar font (iQue Paper Mario)
			cic = (system_type != SystemType::PAL) ? CIC::CIC_NUS_6103 : CIC::CIC_NUS_7103; break;
		case UINT64_C(0x000000f8b860ed00): // CIC-x105 IPL3
		case UINT64_C(0x000000f8af5ffcd6): // NOP:ed out CRC check
			cic = (system_type != SystemType::PAL) ? CIC::CIC_NUS_6105 : CIC::CIC_NUS_7105; break;
		case UINT64_C(0x000000ba5ba4b8cd): // CIC-x106 IPL3
			cic = (system_type != SystemType::PAL) ? CIC::CIC_NUS_6106 : CIC::CIC_NUS_7106; break;
		case UINT64_C(0x0000012daafc8aab): cic = CIC::CIC_NUS_5167; break;
		case UINT64_C(0x000000a9df4b39e1): cic = CIC::CIC_NUS_8303; break;
		case UINT64_C(0x000000aa764e39e1): cic = CIC::CIC_NUS_8401; break;
		case UINT64_C(0x000000abb0b739e1): cic = CIC::CIC_NUS_DDUS; break;
		case UINT64_C(0x00000081ce470326): // CIC-5101 IPL3
		case UINT64_C(0x000000827a47195a): // Kuru Kuru Fever
		case UINT64_C(0x00000082551e4848): // Tower & Shaft
			cic = CIC::CIC_NUS_5101; break;
		}
	} while (cic == CIC::UNKNOWN);

	printf("Region: %s, CIC: CIC-NUS-%s\n", stringify(system_type), stringify(cic));

	if (!is_auto()) {
		printf("Auto-detect is OFF, not updating OSD settings.\n");
		return true;
	}

	printf("Auto-detect is ON, updating OSD settings.\n");

	user_io_status_set(SYS_TYPE_OPT, (uint32_t)system_type);
	user_io_status_set(CIC_TYPE_OPT, (uint32_t)cic);

	return is_known_signature;
}

// Creates a lower-case hex string out of the MD5 buffer
static void md5_to_hex(uint8_t* md5, char* out) {
	for (size_t i = 0; i < MD5_LENGTH; i++, md5++, out += 2)
		sprintf(out, "%02x", *md5);
}

static void calc_bootcode_checksums(uint64_t bootcode_sums[2], const uint8_t* buf) {
	size_t i;
	uint64_t sum = 0;

	// Calculate boot code checksum for bytes 0x40 - 0xc00 (Aleck64)
	for (i = 0x40 / sizeof(uint32_t); i < 0xc00 / sizeof(uint32_t); i++) {
		sum += ((uint32_t*)buf)[i];
	}
	bootcode_sums[1] = sum;

	// Calculate boot code checksum for bytes 0x40 - 0x1000
	for (; i < 0x1000 / sizeof(uint32_t); i++) {
		sum += ((uint32_t*)buf)[i];
	}
	bootcode_sums[0] = sum;
}

static void create_save_path(char* save_path, const MemoryType type) {
	create_path(SAVE_DIR, CoreName2);
	sprintf(save_path, SAVE_DIR"/%s/", CoreName2);
	char* fname = save_path + strlen(save_path);

	char* p = strrchr(current_rom_path, '/');
	if (p) {
		strcat(fname, p + 1);
	}
	else {
		strcat(fname, current_rom_path);
	}

	char ext[16];

	switch (type) {
	case MemoryType::EEPROM_512:
	case MemoryType::EEPROM_2k:
		strcpy(ext, ".eep");
		break;
	case MemoryType::SRAM_32k:
	case MemoryType::SRAM_96k:
		strcpy(ext, ".sra");
		break;
	case MemoryType::FLASH_128k:
		strcpy(ext, ".fla");
		break;
	case MemoryType::CPAK:
	case MemoryType::TPAK:
		sprintf(ext, "_%u%s",
			(mounted_save_files + ((get_cart_save_type() == MemoryType::NONE) ? 1 : 0)),
			((type == MemoryType::CPAK) ? ".cpk" : ".tpk"));
		break;
	default:
		strcpy(ext, ".sav");
		break;
	}

	p = strrchr(fname, '.');
	if (p) {
		strcpy(p, ext);
	}
	else {
		strcat(fname, ext);
	}
}

static void mount_save_core(const char* save_path, const MemoryType type, const char* old_path) {
	auto save_file = new N64SaveFile(type);
	save_file->mount(save_path, old_path);
}

static void mount_save(const MemoryType type, const char* old_path) {
	static char save_path[1024];
	create_save_path(save_path, type);
	mount_save_core(save_path, type, old_path);
}

static void unmount_all_saves() {
	for (size_t i = 0; i < (sizeof(save_files) / sizeof(*save_files)); i++) {
		if (save_files[i]) {
			save_files[i]->unmount();
			delete save_files[i];
			save_files[i] = nullptr;
		}
	}
	mounted_save_files = 0;
}

static void mount_save_gb(const char* old_path) {
	static const size_t games_path_len = strlen(GAMES_DIR"/");
	static const char* ext_gb_save = ".sav";
	static const char* backup_suffix = "_bak";
	static char save_path[1024];
	static char tpak_path[1024];
	static char backup_path[1024];
	static char core_name[32] = "GAMEBOY";

	create_save_path(tpak_path, MemoryType::TPAK);

	char* p;

	if (!*current_rom_path_gb) {
		printf("No Game Boy game mounted. Will use regular .tpk save file.\n");
		mount_save_core(tpak_path, MemoryType::TPAK, old_path);
		return;
	}

	if (strncmp(current_rom_path_gb, GAMES_DIR"/", games_path_len) ||
		!(p = strchr(current_rom_path_gb + games_path_len, '/'))) {
		printf("Unrecognized Game Boy game save path. Will use \"/" SAVE_DIR"/%s/\".\n", core_name);
	}
	else {
		const uint32_t core_name_len = p - (current_rom_path_gb + games_path_len);
		if (!strncmp(current_rom_path_gb + games_path_len, CoreName2, core_name_len)) {
			printf("Game Boy game loaded from \"/" GAMES_DIR"/%s/\". Will use \"/" SAVE_DIR"/%s/\".\n", CoreName2, core_name);
		}
		else {
			memcpy(core_name, current_rom_path_gb + games_path_len, core_name_len);
			core_name[core_name_len] = '\0';
			printf("Game Boy core name is: %s\n", core_name);
		}
	}

	sprintf(save_path, SAVE_DIR"/%s/", core_name);
	char* fname = save_path + strlen(save_path);

	p = strrchr(current_rom_path_gb, '/');
	if (p) {
		strcat(fname, p + 1);
	}
	else {
		strcat(fname, current_rom_path_gb);
	}

	p = strrchr(fname, '.');
	if (p) {
		strcpy(p, ext_gb_save);
	}
	else {
		strcat(fname, ext_gb_save);
	}

	auto save_file = new N64SaveFile(MemoryType::TPAK);
	if (!FileExists(save_path)) {
		create_path(SAVE_DIR, core_name);
		if (!save_file->copy_file(tpak_path, save_path, true, true)) {
			save_file->create_if_missing(save_path, old_path);
			printf("Created new Game Boy save file \"/%s/\".\n", save_path);
		}
		else {
			printf("Copied old .tpk save file to \"/%s/\".\n", save_path);
		}
	}
	else {
		save_file->copy_file(save_path, tpak_path, false, true);
		strcpy(backup_path, save_path);
		p = backup_path + strlen(save_path) - strlen(ext_gb_save);
		strcpy(p, backup_suffix);
		strcpy(p + strlen(backup_suffix), ext_gb_save);
		if (!save_file->copy_file(save_path, backup_path, false)) {
			printf("Failed to create a backup of \"/%s\"! Will use regular .tpk save file.\n", save_path);
			save_file->mount(tpak_path, old_path);
			return;
		}
		else {
			printf("Created a backup of \"/%s\" at \"/%s\".\n", save_path, backup_path);
		}
	}

	save_file->mount(save_path);
}

static void get_old_save_path(char* save_path) {
	static const char* ext = ".sav";

	create_path(SAVE_DIR, CoreName2);
	sprintf(save_path, SAVE_DIR"/%s/", CoreName2);
	char* fname = save_path + strlen(save_path);

	char* p = strrchr(current_rom_path, '/');
	if (p) {
		strcat(fname, p + 1);
	}
	else {
		strcat(fname, current_rom_path);
	}

	p = strrchr(fname, '.');
	if (p) {
		strcpy(p, ext);
	}
	else {
		strcat(fname, ext);
	}
}

// Find the correct save file and offset for a given LBA
static N64SaveFile* resolve_save_file(const uint64_t sector_index, const uint32_t sector_size, const uint32_t chunk_size,
                                      uint8_t& out_file_index, uint64_t& out_file_offset) {
	out_file_index = 0;
	out_file_offset = 0;

	if (!chunk_size) return nullptr;

	int64_t absolute_pos = sector_index * sector_size;

	for (; out_file_index < mounted_save_files; ++out_file_index) {
		// This file ends where the next one begins.
		uint32_t file_end = get_save_offset(out_file_index + 1);

		// If our position is before this boundary, it belongs to this file slot.
		if (absolute_pos < file_end) {
			N64SaveFile* file = save_files[out_file_index];
			if (!file || !file->is_mounted()) return nullptr;

			// Calculate offset relative to the start of this specific file.
			out_file_offset = absolute_pos - get_save_offset(out_file_index);
			return file;
		}
	}

	return nullptr;
}

static void write_save_file(const uint64_t sector_index, const uint32_t sector_size, const int ack_flags,
                            uint8_t* buffer, uint32_t chunk_size) {
	uint64_t file_offset;
	uint8_t file_index;

	// Fetch sector data from FPGA.
	EnableIO();
	spi_w(UIO_SECTOR_WR | ack_flags);
	spi_block_read(buffer, user_io_get_width(), chunk_size);
	DisableIO();

	N64SaveFile* file = resolve_save_file(sector_index, sector_size, chunk_size, file_index, file_offset);
	if (!file) return;

	fileTYPE* img = file->get_image();
	if (!img || ((uint64_t)img->size < file_offset)) return;

	// Clamp write size to file bounds.
	if ((file_offset + chunk_size) > (uint64_t)img->size) {
		chunk_size = img->size - file_offset;
	}

	if (FileSeek(img, file_offset, SEEK_SET)) {
		diskled_on();

		if (file->needs_byteswap()) {
			normalize_data(buffer, chunk_size, ByteOrder::LITTLE_ENDIAN);
		}

		// Only write save data if it's different from the old one.
		uint8_t existing_data[chunk_size];
		if ((chunk_size != (uint32_t)FileReadAdv(img, existing_data, chunk_size)) || memcmp(buffer, existing_data, chunk_size)) {
			menu_process_save();
			FileSeek(img, file_offset, SEEK_SET);
			FileWriteAdv(img, buffer, chunk_size);
		}

		// Log success if we hit the end of the file.
		if ((file_offset + sector_size) >= (uint64_t)img->size) {
			printf("Wrote N64 save data to \"%s\". (%lld bytes)\n", get_image_name(file_index), img->size);
		}
	}
}

static void read_save_file(const uint64_t sector_index, const uint32_t sector_size, const int ack_flags,
                           uint64_t& confirmed_sector, uint8_t* buffer, const uint32_t buffer_capacity, uint32_t chunk_size) {
	uint8_t file_index;
	uint64_t file_offset;

	N64SaveFile* file = resolve_save_file(sector_index, sector_size, chunk_size, file_index, file_offset);

	if (file) {
		fileTYPE* img = file->get_image();
		if (img && img->size) {
			diskled_on();

			if (FileSeek(img, file_offset, SEEK_SET)) {
				const uint32_t bytes_read = (uint32_t)FileReadAdv(img, buffer, chunk_size);

				if (file->needs_byteswap()) {
					normalize_data(buffer, bytes_read, ByteOrder::LITTLE_ENDIAN);
				}

				// Pad incomplete chunks with zeros.
				if (bytes_read < chunk_size) {
					memset(buffer + bytes_read, 0, chunk_size - bytes_read);
				}

				// Log success if we hit the end of the file.
				if ((file_offset + sector_size) >= (uint64_t)img->size) {
					printf("Read N64 save data from \"%s\". (%lld bytes)\n", get_image_name(file_index), img->size);
				}

				confirmed_sector = sector_index;
			}
		}
	}

	// Handle failure case (file not found, unmounted, or read error).
	if (confirmed_sector == -1LLU) {
		printf("Couldn't Read N64 save data from \"%s\"!!\n", get_image_name(file_index));
		memset(buffer, 0, buffer_capacity);
	}

	// Send sector data to FPGA.
	EnableIO();
	spi_w(UIO_SECTOR_RD | ack_flags);
	spi_block_write(buffer, user_io_get_width(), chunk_size);
	DisableIO();
}

/* The N64 core handles all its save types by sending all of them concatenated together to/from the FPGA.
   e.g. (EEPROM/SRAM/FLASH) (+ TPAK) (+ CPAK 1) (+ CPAK 2) (+ CPAK 3) (+ CPAK 4)
   We need to split up the continuous data stream into files if we want to save.
   We need to merge the files into a single continuous data stream if we want to load. */
bool n64_process_save(const bool use_save, const int op, const uint64_t sector_index, const uint32_t sector_size, const int ack_flags,
					  uint64_t& confirmed_sector, uint8_t* buffer, const uint32_t buffer_capacity, uint32_t chunk_size) {
	if (!use_save || !(op & 3)) return false;

	confirmed_sector = -1LLU; // Default to error state.

	if (op == 2) {
		write_save_file(sector_index, sector_size, ack_flags, buffer, chunk_size);
	}
	else {
		read_save_file(sector_index, sector_size, ack_flags, confirmed_sector, buffer, buffer_capacity, chunk_size);
	}

	return true;
}

static void mount_all_saves() {
	get_old_save_path(old_save_path);
	auto cart_save_type = get_cart_save_type();

	if (cart_save_type != MemoryType::NONE) {
		mount_save(cart_save_type, old_save_path);
	}

	auto use_cpak = (bool)user_io_status_get(CPAK_OPT);

	// First controller can be either tpak or cpak. Tpak is prioritized.
	if ((bool)user_io_status_get(TPAK_OPT)) {
		mount_save_gb(old_save_path);
	}
	else if (use_cpak) {
		mount_save(MemoryType::CPAK, old_save_path);
	}

	if (use_cpak) {
		mount_save(MemoryType::CPAK, old_save_path);
		mount_save(MemoryType::CPAK, old_save_path);
		mount_save(MemoryType::CPAK, old_save_path);
	}
}

static int cheat_compare(const volatile uint8_t* mem, const int32_t new_val, const cheat_code_flags flags) {
	if (flags.cmp_type == ComparisionType::OPTYPE_ALWAYS)
		return 1;

	uint32_t old_val_masked = 0, new_val_masked = 0;

	// Mask and reverse
	for (size_t i = 0; flags.cmp_mask >> i; i++) {
		old_val_masked <<= 8;
		new_val_masked <<= 8;
		if (flags.cmp_mask & (0x1 << i)) {
			old_val_masked |= mem[i];
			new_val_masked |= (new_val >> (i * 8)) & 0xff;
		}
	}

	// Return "0" if not equal according to comparision type, so next code will get skipped
	switch (flags.cmp_type) {
	case ComparisionType::OPTYPE_EQUALS:
		return new_val_masked == old_val_masked;
	case ComparisionType::OPTYPE_GREATER:
		return new_val_masked > old_val_masked;
	case ComparisionType::OPTYPE_LESS:
		return new_val_masked < old_val_masked;
	case ComparisionType::OPTYPE_GREATER_EQ:
		return new_val_masked >= old_val_masked;
	case ComparisionType::OPTYPE_LESS_EQ:
		return new_val_masked <= old_val_masked;
	case ComparisionType::OPTYPE_NOT_EQ:
		return new_val_masked != old_val_masked;
	default:
		// Unknown comparision type
		return -1;
	}
}

static int cheat_execute(cheat_code* code) {
	if ((code->address >= RAM_SIZE) || !code->flags.cmp_mask)
		return -1;

	// Boot code, run only once. Skip if already executed.
	if (code->flags.is_boot_code) {
		if (code->flags.is_boot_code_executed)
			return 1;

		code->flags.is_boot_code_executed = true;
	}

	// Game Shark button code. Only run if certain button is pressed. Hard-coded to F5 right now.
	if (code->flags.is_gs_button_code && !is_key_pressed(63))
		return 1;

	volatile uint8_t* mem = ((volatile uint8_t*)rdram_ptr) + code->address;

	int result = cheat_compare(mem, code->replace, code->flags);
	if (result != 1) return result;

	if ((code->flags.cmp_mask == 0xf) && !(code->address & 0x3)) {
		// 32-bit aligned write
		*(uint32_t*)mem = code->replace;
	}
	else if ((code->flags.cmp_mask == 0x3) && !(code->address & 0x1)) {
		// 16-bit aligned write
		*(uint16_t*)mem = code->replace & 0xffff;
	}
	else {
		// Write byte-by-byte to memory in big-endian order
		if (code->flags.cmp_mask & (0x1 << 0)) mem[0] = (code->replace >> (8 * 0)) & 0xff;
		if (code->flags.cmp_mask & (0x1 << 1)) mem[1] = (code->replace >> (8 * 1)) & 0xff;
		if (code->flags.cmp_mask & (0x1 << 2)) mem[2] = (code->replace >> (8 * 2)) & 0xff;
		if (code->flags.cmp_mask & (0x1 << 3)) mem[3] = (code->replace >> (8 * 3)) & 0xff;
	}

	return 1;
}

void n64_cheats_send(const void* buf_ptr, const uint32_t size) {
	cheat_codes = (cheat_code*)buf_ptr;
	cheat_codes_count = size;
}

static void n64dd_poll_save();

static unsigned long poll_timer = 0;

void n64_reset() {
	printf("Resetting N64...\n");
	if (cheat_codes && cheats_loaded() && cheats_enabled()) {
		for (uint32_t i = 0; i < cheat_codes_count; i++) {
			cheat_codes[i].flags.is_boot_code_executed = false;
		}

		poll_timer = GetTimer(2500);
	}
}

void n64_poll() {
	static uint8_t adj = 0;

	if (!poll_timer || CheckTimer(poll_timer)) {
		if (!(loaded && is_fpga_ready(0))) {
			poll_timer = GetTimer(1000);
			printf("Waiting for N64 game to be loaded...\n");
			return;
		}

		auto system_type = (SystemType)user_io_status_get(SYS_TYPE_OPT);

		// 17, 17, 16, 17, 17, 16... Repeated, to achieve (1 / .01667 ms) = 60 Hz.
		poll_timer = GetTimer((system_type == SystemType::PAL) ? 20 : ((adj > 1) ? 17 : 16));
		if (adj > 3 || --adj == 0) adj = 3;

		if (cheats_loaded()) {
			if (rdram_ptr == (void*)-1) return;

			if (!rdram_ptr) {
				if (!(rdram_ptr = shmem_map(0x30000000, RAM_SIZE))) {
					rdram_ptr = (void*)-1;
					printf("Failed to map RDRAM!\n");
					Info("Failed to initialize cheat engine!", 2000);
				}
				else {
					printf("Mapped RDRAM at 0x%" PRIXPTR ".\n", (uintptr_t)rdram_ptr);
				}
			}
			else if (cheat_codes && cheats_enabled()) {
				for (uint32_t i = 0; i < cheat_codes_count; i++) {
					switch (cheat_execute(&cheat_codes[i])) {
					case 0:
						// Unfullfilled conditional code (DXXXXXXX), skip the next flagged code(s)
						while ((i < (cheat_codes_count - 1)) && (cheat_codes[i + 1].compare & 0x1)) {
							i++;
						}
						// Fall through
					case 1:
						// Normal code (8XXXXXXX)
						continue;
					}

					// Invalid or unhandled code.
					printf("Invalid cheat code: %08x\t%08x\t%08x\t%08x !\n",
						cheat_codes[i].address,
						cheat_codes[i].compare,
						cheat_codes[i].replace,
						*(uint32_t*)&cheat_codes[i].flags);
					Info("Invalid cheat code! Disabling cheats.", 1500);
					cheats_disable();
					break;
				}
			}
		}

		n64dd_poll_save();
	}
}

static constexpr uint32_t N64_ROM_FASTLOAD_ADDR = 0x32000000;
static constexpr uint32_t N64DD_IPL_LOAD_ADDR = 0x30C00000;
static constexpr const char* N64DD_IPL_BOOT_FILE = "boot3.rom";
static constexpr const char* N64DD_IPL_DEV_BOOT_FILE = "boot4.rom";
static constexpr const char* N64DD_IPL_US_BOOT_FILE = "boot5.rom";
static constexpr const char* N64DD_IPL_DISK_FILE = "dd_bios.rom";
static constexpr uint32_t N64DD_IPL_SIZE = 4 * 1024 * 1024;
static constexpr uint32_t N64DD_NDD_SIZE = 0x03DEC800;
static constexpr uint32_t N64DD_EXPANDED_SIZE = 0x06E28000;
static constexpr uint32_t N64DD_SYSTEM_SECTOR_LENGTH = 232;
static constexpr uint32_t N64DD_DEV_SYSTEM_SECTOR_LENGTH = 192;
static constexpr uint32_t N64DD_SECTORS_PER_BLOCK = 85;
static constexpr uint32_t N64DD_BAD_TRACKS_PER_ZONE = 12;
static constexpr uint32_t N64DD_FLAT_HEAD_STRIDE = 0x03714000;
static constexpr uint32_t N64DD_FLAT_TRACK_STRIDE = 0x0000C000;
static constexpr uint32_t N64DD_FLAT_BLOCK_STRIDE = 0x00006000;
static constexpr uint32_t N64DD_FLAT_SECTOR_STRIDE = 0x00000100;
static constexpr uint32_t N64DD_FLAT_TRACKS_PER_HEAD = 1175;
static constexpr uint32_t N64DD_DIRTY_FLAG_OFFSET = 0x00005F00;
static constexpr uint32_t N64DD_DIRTY_MARKER_SIZE = 8;
static constexpr uint8_t N64DD_BAD_BLOCK_FILL = 0xDD;
static constexpr uint32_t N64DD_SAVE_CHUNK_SIZE = 1024 * 1024;
static constexpr uint8_t N64DD_DIRTY_MAGIC_BYTE = 0xD1;
static constexpr uint16_t N64DD_RAM_START_LBA[7] = {0x05A2, 0x07C6, 0x09EA, 0x0C0E, 0x0E32, 0x1010, 0x10DC};
static constexpr uint32_t N64DD_RAM_SIZE[7] = {
	0x024A9DC0, 0x01C226C0, 0x01450F00, 0x00D35680, 0x006CFD40, 0x001DA240, 0
};

enum class N64DDDiskRegion : uint8_t {
	UNKNOWN,
	JAPAN,
	USA,
	DEVELOPMENT
};

static bool n64dd_load_ram_save_to_mem(const char* save_path, const char* disk_path, uint8_t* mem, uint8_t disk_type);
static bool n64dd_save_ram_image(const char* save_path, const char* disk_path, uint8_t* mem, uint8_t disk_type, bool force);

static void n64dd_clear_disk_save_state() {
	current_dd_disk_path[0] = '\0';
	current_dd_ram_save_path[0] = '\0';
	current_dd_load_addr = 0;
	current_dd_disk_loaded = 0;
	current_dd_skip_initial_osd_save = 0;
	current_dd_cart_expansion = 0;
	current_dd_disk_type = 0;
	current_dd_disk_id[0] = '\0';
}

static void n64dd_create_ram_save_path(char* save_path, const char* disk_path) {
	create_path(SAVE_DIR, CoreName2);

	const char* fname = strrchr(disk_path, '/');
	const char* fname_backslash = strrchr(disk_path, '\\');
	if (fname_backslash && (!fname || fname_backslash > fname)) fname = fname_backslash;
	fname = fname ? fname + 1 : disk_path;

	char base_name[1024];
	strncpy(base_name, fname, sizeof(base_name) - 1);
	base_name[sizeof(base_name) - 1] = '\0';

	char* ext = strrchr(base_name, '.');
	if (ext) *ext = '\0';

	snprintf(save_path, 1024, SAVE_DIR"/%s/%s.ram", CoreName2, base_name);
}

static const char* n64_leaf_name(const char* path) {
	if (!path) return nullptr;
	const char* slash = strrchr(path, '/');
	const char* backslash = strrchr(path, '\\');
	if (backslash && (!slash || backslash > slash)) slash = backslash;
	return slash ? slash + 1 : path;
}

static void n64dd_progress(const char* title, const char* name, uint64_t current, uint64_t max) {
	if (!title || !name || !max) return;
	if (current > max) current = max;
	ProgressMessage(title, n64_leaf_name(name), (int)current, (int)max);
}

static bool n64dd_save_ram_to_file(bool force) {
	if (!current_dd_disk_loaded || !current_dd_load_addr ||
		!current_dd_ram_save_path[0] || !current_dd_disk_path[0]) return false;

	const uint8_t preserve_osd = user_io_osd_is_visible() ? 1 : 0;
	uint8_t* mem = (uint8_t*)shmem_map(fpga_mem(current_dd_load_addr), N64DD_EXPANDED_SIZE);
	if (!mem || mem == (uint8_t*)-1) {
		printf("64DD save: failed to map DDRAM disk image.\n");
		return false;
	}

	const bool ram_ok = n64dd_save_ram_image(current_dd_ram_save_path, current_dd_disk_path,
		mem, current_dd_disk_type, force);

	shmem_unmap(mem, N64DD_EXPANDED_SIZE);
	if (!preserve_osd) ProgressMessage();
	return ram_ok;
}

static bool n64dd_save_now(bool force, const char* reason) {
	if (!current_dd_disk_loaded) return false;
	const char* save_reason = reason ? reason : "forced";
	const bool ok = n64dd_save_ram_to_file(force);
	if (!ok) printf("64DD save: %s save failed.\n", save_reason);
	return ok;
}

static bool n64dd_flush_pending_save_before_disk_change() {
	return n64dd_save_now(true, "disk-change");
}

void n64_save_dd_disk() {
	n64dd_save_now(true, "manual");
	current_dd_skip_initial_osd_save = 0;
}

static void n64dd_poll_save() {
	static uint8_t previous_manual_save = 0;
	static uint8_t previous_osd_visible = 0;
	const uint8_t manual_save = user_io_status_get(SAVE_BACKUP_OPT) ? 1 : 0;
	const uint8_t osd_visible = user_io_osd_is_visible() ? 1 : 0;
	const bool autosave_enabled = !user_io_status_get(AUTOSAVE_OPT);
	bool saved_this_poll = false;

	if (!current_dd_disk_loaded) {
		previous_manual_save = manual_save;
		previous_osd_visible = osd_visible;
		current_dd_skip_initial_osd_save = 0;
		return;
	}

	if (manual_save && !previous_manual_save) {
		n64dd_save_now(true, "manual");
		saved_this_poll = true;
		current_dd_skip_initial_osd_save = 0;
	}
	previous_manual_save = manual_save;

	if (autosave_enabled && !previous_osd_visible && osd_visible) {
		if (current_dd_skip_initial_osd_save) {
			current_dd_skip_initial_osd_save = 0;
		} else if (!saved_this_poll) {
			n64dd_save_now(true, "OSD-open");
		}
	}
	if (!osd_visible && current_dd_skip_initial_osd_save) {
		current_dd_skip_initial_osd_save = 0;
	}
	previous_osd_visible = osd_visible;
}

struct n64dd_zone {
	uint8_t head;
	uint16_t sector_length;
	uint16_t tracks;
	uint16_t track_offset;
};

struct n64dd_sector_map_entry {
	uint32_t flat_offset;
	uint16_t sector_length;
};

static constexpr n64dd_zone N64DD_ZONES[16] = {
	{0, 232, 158, 0},    {0, 216, 158, 158},  {0, 208, 149, 316},  {0, 192, 149, 465},
	{0, 176, 149, 614},  {0, 160, 149, 763},  {0, 144, 149, 912},  {0, 128, 114, 1061},
	{1, 216, 158, 0},    {1, 208, 158, 158},  {1, 192, 149, 316},  {1, 176, 149, 465},
	{1, 160, 149, 614},  {1, 144, 149, 763},  {1, 128, 149, 912},  {1, 112, 114, 1061},
};

static constexpr uint8_t N64DD_VZONE_TO_PZONE[7][16] = {
	{0, 1, 2, 9, 8, 3, 4, 5, 6, 7, 15, 14, 13, 12, 11, 10},
	{0, 1, 2, 3, 10, 9, 8, 4, 5, 6, 7, 15, 14, 13, 12, 11},
	{0, 1, 2, 3, 4, 11, 10, 9, 8, 5, 6, 7, 15, 14, 13, 12},
	{0, 1, 2, 3, 4, 5, 12, 11, 10, 9, 8, 6, 7, 15, 14, 13},
	{0, 1, 2, 3, 4, 5, 6, 13, 12, 11, 10, 9, 8, 7, 15, 14},
	{0, 1, 2, 3, 4, 5, 6, 7, 14, 13, 12, 11, 10, 9, 8, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 15, 14, 13, 12, 11, 10, 9, 8},
};
static constexpr uint16_t N64DD_BLOCK_SIZE[16] = {
	0x4D08, 0x47B8, 0x4510, 0x3FC0, 0x3A70, 0x3520, 0x2FD0, 0x2A80,
	0x47B8, 0x4510, 0x3FC0, 0x3A70, 0x3520, 0x2FD0, 0x2A80, 0x2530,
};

static bool n64dd_read_exact(fileTYPE* file, uint64_t offset, uint8_t* data, uint32_t size) {
	if (!FileSeek(file, offset, SEEK_SET)) return false;
	const int read = FileReadAdv(file, data, size);
	return read >= 0 && static_cast<uint32_t>(read) == size;
}

static N64DDDiskRegion n64dd_detect_retail_region(fileTYPE* file) {
	static constexpr uint8_t JAPAN_SIGNATURE[4] = {0xE8, 0x48, 0xD3, 0x16};
	static constexpr uint8_t USA_SIGNATURE[4] = {0x22, 0x63, 0xEE, 0x56};
	uint8_t signature[4];

	if (!n64dd_read_exact(file, 0, signature, sizeof(signature))) return N64DDDiskRegion::UNKNOWN;
	if (!memcmp(signature, JAPAN_SIGNATURE, sizeof(signature))) return N64DDDiskRegion::JAPAN;
	if (!memcmp(signature, USA_SIGNATURE, sizeof(signature))) return N64DDDiskRegion::USA;
	return N64DDDiskRegion::UNKNOWN;
}

static bool n64dd_copy_ipl_to_mem(fileTYPE* file, const char* name, uint8_t* ddipl_dest, uint8_t* boot_dest, uint32_t size) {
	static uint8_t buf[4096];
	ByteOrder rom_endianness = ByteOrder::BIG_ENDIAN;
	bool is_first_chunk = true;
	uint32_t copied = 0;

	if (!FileSeek(file, 0, SEEK_SET)) return false;

	while (copied < size) {
		uint32_t chunk = std::min<uint32_t>(sizeof(buf), size - copied);
		const int read = FileReadAdv(file, buf, chunk);
		if (read < 0 || static_cast<uint32_t>(read) != chunk) return false;

		if (is_first_chunk) {
			if (chunk < 4096) {
				printf("Failed to load 64DD IPL: must be at least 4096 bytes.\n");
				return false;
			}

			rom_endianness = detect_rom_endianness(buf);
		}

		normalize_data(buf, chunk, rom_endianness);

		if (is_first_chunk) {
			const bool development_ipl = !memcmp(buf + 0x3B, "NDXJ", 4);
			user_io_status_set(DD_DEVELOPMENT_OPT, development_ipl ? 1 : 0);
			printf("64DD IPL: %s drive ID selected.\n", development_ipl ? "development" : "retail");
		}

		if (is_first_chunk && boot_dest) {
			uint64_t bootcode_sums[2] = { };
			calc_bootcode_checksums(bootcode_sums, buf);
			detect_rom_settings_from_first_chunk((char)buf[0x3e], bootcode_sums, sizeof(bootcode_sums) / sizeof(*bootcode_sums));
			if (is_auto()) set_cart_save_type(MemoryType::NONE);
		}

		memcpy(ddipl_dest + copied, buf, chunk);
		if (boot_dest) memcpy(boot_dest + copied, buf, chunk);

		copied += chunk;
		is_first_chunk = false;
		n64dd_progress("Loading", name, copied, size);
	}

	return true;
}

static uint32_t n64dd_flat_offset(uint32_t track, uint32_t head, uint32_t block, uint32_t sector) {
	if (sector >= 90) sector -= 90;
	if (sector > 84) sector = 84;

	return (head ? N64DD_FLAT_HEAD_STRIDE : 0) +
		(track * N64DD_FLAT_TRACK_STRIDE) +
		(block * N64DD_FLAT_BLOCK_STRIDE) +
		(sector * N64DD_FLAT_SECTOR_STRIDE);
}

static bool n64dd_mark_bad_flat_block(uint8_t* dest, uint32_t track, uint32_t head, uint32_t block) {
	const uint32_t dest_offset = n64dd_flat_offset(track, head, block, 0);
	if ((dest_offset + N64DD_FLAT_BLOCK_STRIDE) > N64DD_EXPANDED_SIZE) return false;
	for (uint32_t sector = 0; sector < N64DD_SECTORS_PER_BLOCK; sector++) {
		memset(dest + dest_offset + (sector * N64DD_FLAT_SECTOR_STRIDE) + 0xF8, N64DD_BAD_BLOCK_FILL, 8);
	}
	return true;
}

static bool n64dd_copy_block_to_flat(fileTYPE* file, uint64_t source_offset, uint8_t* dest,
	uint32_t track, uint32_t head, uint32_t block, uint32_t sector_length, std::vector<uint8_t>& block_data) {
	const uint32_t block_length = sector_length * N64DD_SECTORS_PER_BLOCK;
	block_data.resize(block_length);

	if (!n64dd_read_exact(file, source_offset, block_data.data(), block_length)) return false;

	for (uint32_t sector = 0; sector < N64DD_SECTORS_PER_BLOCK; sector++) {
		const uint32_t dest_offset = n64dd_flat_offset(track, head, block, sector);
		if ((dest_offset + sector_length) > N64DD_EXPANDED_SIZE) return false;
		memcpy(dest + dest_offset, block_data.data() + (sector * sector_length), sector_length);
	}

	return true;
}

static bool n64dd_copy_file_to_vector(fileTYPE* file, std::vector<uint8_t>& data, uint32_t expected_size,
	const char* progress_title, const char* progress_name) {
	data.resize(expected_size);
	if (!FileSeek(file, 0, SEEK_SET)) return false;

	uint32_t copied = 0;
	while (copied < expected_size) {
		const uint32_t chunk = std::min<uint32_t>(N64DD_SAVE_CHUNK_SIZE, expected_size - copied);
		const int read = FileReadAdv(file, data.data() + copied, (int)chunk);
		if (read < 0 || static_cast<uint32_t>(read) != chunk) return false;

		copied += chunk;
		if (progress_title) n64dd_progress(progress_title, progress_name, copied, expected_size);
	}

	return true;
}

static bool n64dd_write_vector_to_file(const char* save_path, const std::vector<uint8_t>& data) {
	const uint32_t total_size = (uint32_t)data.size();
	fileTYPE save_file = {};
	if (!FileOpenEx(&save_file, save_path, O_CREAT | O_TRUNC | O_RDWR | O_SYNC, 0, 0)) {
		printf("64DD save: failed to create \"%s\".\n", save_path);
		return false;
	}

	uint32_t written = 0;
	while (written < total_size) {
		const uint32_t chunk = std::min<uint32_t>(N64DD_SAVE_CHUNK_SIZE, total_size - written);
		const int ret = FileWriteAdv(&save_file, const_cast<uint8_t*>(data.data() + written), (int)chunk);
		if (ret < 0 || static_cast<uint32_t>(ret) != chunk) {
			printf("64DD save: failed writing \"%s\" at 0x%08x.\n", save_path, written);
			FileClose(&save_file);
			return false;
		}
		written += chunk;
		n64dd_progress("Saving", save_path, written, total_size);
	}

	FileClose(&save_file);
	return true;
}

static bool n64dd_flat_block_is_dirty(const uint8_t* flat, uint32_t block_offset) {
	const uint32_t marker_offset = block_offset + N64DD_DIRTY_FLAG_OFFSET;
	if ((marker_offset + N64DD_DIRTY_MARKER_SIZE) > N64DD_EXPANDED_SIZE) return false;

	for (uint32_t i = 0; i < N64DD_DIRTY_MARKER_SIZE; i++) {
		if (flat[marker_offset + i] != N64DD_DIRTY_MAGIC_BYTE) return false;
	}
	return true;
}

static bool n64dd_find_dirty_flat_blocks(const uint8_t* flat, std::vector<uint32_t>& dirty_blocks) {
	dirty_blocks.clear();
	for (uint32_t head = 0; head < 2; head++) {
		for (uint32_t track = 0; track < N64DD_FLAT_TRACKS_PER_HEAD; track++) {
			for (uint32_t block = 0; block < 2; block++) {
				const uint32_t block_offset = n64dd_flat_offset(track, head, block, 0);
				if ((block_offset + N64DD_DIRTY_FLAG_OFFSET + N64DD_DIRTY_MARKER_SIZE) > N64DD_EXPANDED_SIZE) {
					return false;
				}
				if (n64dd_flat_block_is_dirty(flat, block_offset)) dirty_blocks.push_back(block_offset);
			}
		}
	}
	return true;
}

static void n64dd_clear_dirty_flat_blocks(uint8_t* flat, const std::vector<uint32_t>& dirty_blocks) {
	for (uint32_t block_offset : dirty_blocks) {
		const uint32_t marker_offset = block_offset + N64DD_DIRTY_FLAG_OFFSET;
		if ((marker_offset + N64DD_DIRTY_MARKER_SIZE) <= N64DD_EXPANDED_SIZE) {
			memset(flat + marker_offset, 0, N64DD_DIRTY_MARKER_SIZE);
		}
	}
}

static bool n64dd_file_matches_vector(const char* path, const std::vector<uint8_t>& data) {
	if (!FileExists(path, 0)) return false;

	fileTYPE file = {};
	if (!FileOpenEx(&file, path, O_RDONLY, 0, 0)) return false;
	if ((uint64_t)file.size != data.size()) {
		FileClose(&file);
		return false;
	}

	static std::vector<uint8_t> existing;
	existing.resize(N64DD_SAVE_CHUNK_SIZE);
	uint32_t checked = 0;
	while (checked < data.size()) {
		const uint32_t chunk = std::min<uint32_t>(N64DD_SAVE_CHUNK_SIZE, (uint32_t)data.size() - checked);
		const int read = FileReadAdv(&file, existing.data(), (int)chunk);
		if (read < 0 || static_cast<uint32_t>(read) != chunk || memcmp(existing.data(), data.data() + checked, chunk)) {
			FileClose(&file);
			return false;
		}
		checked += chunk;
	}

	FileClose(&file);
	return true;
}

static bool n64dd_repeat_check(const std::vector<uint8_t>& data, uint32_t repeat, uint32_t size) {
	if (data.size() < repeat * size) return false;
	for (uint32_t i = 0; i < size; i++) {
		for (uint32_t j = 1; j < repeat; j++) {
			if (data[i] != data[(j * size) + i]) return false;
		}
	}
	return true;
}

static bool n64dd_valid_system_block(const std::vector<uint8_t>& block, uint32_t sector_length, bool dev) {
	if (block.size() < N64DD_BLOCK_SIZE[0]) return false;

	const bool retail_magic =
		(block[0] == 0xE8 && block[1] == 0x48 && block[2] == 0xD3 && block[3] == 0x16) ||
		(block[0] == 0x22 && block[1] == 0x63 && block[2] == 0xEE && block[3] == 0x56);
	const bool dev_magic = block[0] == 0x00 && block[1] == 0x00 && block[2] == 0x00 && block[3] == 0x00;

	if (dev ? !dev_magic : !retail_magic) return false;
	if (block[4] != 0x10) return false;
	if (block[5] < 0x10 || block[5] >= 0x17) return false;
	if (block[0x18] != 0xFF || block[0x19] != 0xFF || block[0x1A] != 0xFF || block[0x1B] != 0xFF) return false;
	if (block[0x1C] != 0x80) return false;

	return n64dd_repeat_check(block, N64DD_SECTORS_PER_BLOCK, sector_length);
}

static bool n64dd_valid_id_block(fileTYPE* file, uint32_t lba, char* disk_id) {
	std::vector<uint8_t> block(N64DD_BLOCK_SIZE[0]);
	if (!n64dd_read_exact(file, (uint64_t)lba * N64DD_BLOCK_SIZE[0], block.data(), (uint32_t)block.size())) return false;
	if (!n64dd_repeat_check(block, N64DD_SECTORS_PER_BLOCK, N64DD_SYSTEM_SECTOR_LENGTH)) return false;
	if (disk_id && !disk_id[0]) {
		memcpy(disk_id, block.data(), 4);
		disk_id[4] = '\0';
	}
	return true;
}

static bool n64dd_read_disk_id(fileTYPE* file, char* disk_id) {
	static constexpr uint8_t id_blocks[2] = {15, 14};
	if (disk_id) disk_id[0] = '\0';

	bool valid_id = false;
	for (uint8_t id_block : id_blocks) {
		if (n64dd_valid_id_block(file, id_block, disk_id)) valid_id = true;
	}
	return valid_id;
}

static bool n64dd_find_ndd_system_block(fileTYPE* file, std::vector<uint8_t>& sys_data, uint8_t& disk_type,
	std::vector<uint8_t>& bad_lbas, char* disk_id, bool verbose = true, bool* development = nullptr) {
	static constexpr uint8_t retail_system_blocks[4] = {9, 8, 1, 0};
	static constexpr uint8_t dev_system_blocks[4] = {11, 10, 3, 2};
	static constexpr uint8_t retail_bad_blocks[13] = {2, 3, 10, 11, 12, 16, 17, 18, 19, 20, 21, 22, 23};
	static constexpr uint8_t dev_bad_blocks[12] = {0, 1, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23};
	std::vector<uint8_t> block(N64DD_BLOCK_SIZE[0]);
	std::vector<uint8_t> best_sys_data;
	uint8_t best_disk_type = 0;
	uint8_t best_system_block = 0;
	bool found = false;
	if (disk_id) disk_id[0] = '\0';
	if (development) *development = false;

	bad_lbas.assign(0x10DC, 0);
	for (uint8_t system_block : retail_system_blocks) {
		if (!n64dd_read_exact(file, (uint64_t)system_block * N64DD_BLOCK_SIZE[0], block.data(), (uint32_t)block.size())) {
			return false;
		}
		if (n64dd_valid_system_block(block, N64DD_SYSTEM_SECTOR_LENGTH, false)) {
			best_sys_data.assign(block.begin(), block.begin() + N64DD_SYSTEM_SECTOR_LENGTH);
			best_disk_type = block[5] & 0x0F;
			best_system_block = system_block;
			found = true;
		} else {
			bad_lbas[system_block] = 1;
		}
	}
	if (found) {
		for (uint8_t lba : retail_bad_blocks) bad_lbas[lba] = 1;
		if (!n64dd_read_disk_id(file, disk_id)) return false;

		sys_data.swap(best_sys_data);
		disk_type = best_disk_type;
		if (verbose) {
			printf("64DD NDD: retail system block %u, disk type %u.\n", (unsigned)best_system_block, (unsigned)disk_type);
			if (disk_id && disk_id[0]) printf("64DD NDD: disk ID %.4s.\n", disk_id);
			printf("64DD NDD: IPL load LBA blocks %u, load address 0x%08x.\n",
				(unsigned)((sys_data[0x06] << 8) | sys_data[0x07]),
				(unsigned)((sys_data[0x1C] << 24) | (sys_data[0x1D] << 16) | (sys_data[0x1E] << 8) | sys_data[0x1F]));
		}
		return disk_type < 7;
	}

	bad_lbas.assign(0x10DC, 0);
	for (uint8_t system_block : dev_system_blocks) {
		if (!n64dd_read_exact(file, (uint64_t)system_block * N64DD_BLOCK_SIZE[0], block.data(), (uint32_t)block.size())) {
			return false;
		}
		if (n64dd_valid_system_block(block, 192, true)) {
			best_sys_data.assign(block.begin(), block.begin() + N64DD_SYSTEM_SECTOR_LENGTH);
			best_disk_type = block[5] & 0x0F;
			best_system_block = system_block;
			found = true;
		} else {
			bad_lbas[system_block] = 1;
		}
	}
	if (found) {
		for (uint8_t lba : dev_bad_blocks) bad_lbas[lba] = 1;
		if (!n64dd_read_disk_id(file, disk_id)) return false;

		sys_data.swap(best_sys_data);
		disk_type = best_disk_type;
		if (development) *development = true;
		if (verbose) {
			printf("64DD NDD: development system block %u, disk type %u.\n", (unsigned)best_system_block, (unsigned)disk_type);
			if (disk_id && disk_id[0]) printf("64DD NDD: disk ID %.4s.\n", disk_id);
			printf("64DD NDD: IPL load LBA blocks %u, load address 0x%08x.\n",
				(unsigned)((sys_data[0x06] << 8) | sys_data[0x07]),
				(unsigned)((sys_data[0x1C] << 24) | (sys_data[0x1D] << 16) | (sys_data[0x1E] << 8) | sys_data[0x1F]));
		}
		return disk_type < 7;
	}

	return false;
}

static bool n64dd_expand_ndd_to_flat(fileTYPE* file, uint8_t* dest, bool& cart_expansion_disk, char* disk_id,
	uint8_t& disk_type, bool* development_out = nullptr, const char* progress_name = nullptr) {
	if (file->size != N64DD_NDD_SIZE) return false;

	std::vector<uint8_t> sys_data;
	std::vector<uint8_t> bad_lbas;
	bool development = false;
	if (!n64dd_find_ndd_system_block(file, sys_data, disk_type, bad_lbas, disk_id, true, &development)) return false;
	if (development_out) *development_out = development;
	cart_expansion_disk = disk_id && disk_id[0] == 'E';
	if (cart_expansion_disk) {
		printf("64DD NDD: cart expansion disk detected.\n");
	}

	uint64_t source_offset = 0;
	uint32_t lba = 0;
	uint32_t starting_block = 0;
	std::vector<uint8_t> block_data;

	for (uint32_t vzone = 0; vzone < 16; vzone++) {
		const uint32_t pzone = N64DD_VZONE_TO_PZONE[disk_type][vzone];
		const auto& zone = N64DD_ZONES[pzone];
		const uint32_t block_size = zone.sector_length * N64DD_SECTORS_PER_BLOCK;
		const uint32_t defect_start = pzone ? sys_data[0x07 + pzone] : 0;
		const uint32_t defect_stop = sys_data[0x07 + pzone + 1];
		uint32_t listed_bad_tracks = 0;
		std::vector<uint8_t> bad_tracks(zone.tracks, 0);

		for (uint32_t defect_offset = defect_start; defect_offset < defect_stop; defect_offset++) {
			const uint32_t bad_track = sys_data[0x20 + defect_offset];
			if (bad_track < zone.tracks) bad_tracks[bad_track] = 1;
			listed_bad_tracks++;
		}

		for (uint32_t filler_track = 0; listed_bad_tracks < N64DD_BAD_TRACKS_PER_ZONE && filler_track < zone.tracks; filler_track++) {
			bad_tracks[zone.tracks - 1 - filler_track] = 1;
			listed_bad_tracks++;
		}

		for (uint32_t zone_track_index = 0; zone_track_index < zone.tracks; zone_track_index++) {
			const uint32_t zone_track = zone.head ? (zone.tracks - 1 - zone_track_index) : zone_track_index;
			const uint32_t track = zone.track_offset + zone_track;
			if (bad_tracks[zone_track]) {
				for (uint32_t block = 0; block < 2; block++) {
					if (!n64dd_mark_bad_flat_block(dest, track, zone.head, block)) return false;
				}
				continue;
			}

			for (uint32_t block = 0; block < 2; block++) {
				const uint32_t flat_block = starting_block ^ block;
				if (lba >= bad_lbas.size()) return false;
				const bool development_system_lba = development && (lba == 2 || lba == 3 || lba == 10 || lba == 11);
				const uint32_t sector_length = development_system_lba ? N64DD_DEV_SYSTEM_SECTOR_LENGTH : zone.sector_length;
				if (!n64dd_copy_block_to_flat(file, source_offset, dest, track, zone.head, flat_block, sector_length, block_data)) {
					return false;
				}
				if (bad_lbas[lba]) {
					if (!n64dd_mark_bad_flat_block(dest, track, zone.head, flat_block)) return false;
				}

				source_offset += block_size;
				lba++;
				n64dd_progress("Loading", progress_name, source_offset, file->size);
			}
			starting_block ^= 1;
		}
	}

	return lba == 0x10DC && source_offset == (uint64_t)file->size;
}

static bool n64dd_build_compact_ndd_sector_map(fileTYPE* file, uint8_t& disk_type, std::vector<n64dd_sector_map_entry>& map) {
	if (file->size != N64DD_NDD_SIZE) return false;

	std::vector<uint8_t> sys_data;
	std::vector<uint8_t> bad_lbas;
	if (!n64dd_find_ndd_system_block(file, sys_data, disk_type, bad_lbas, nullptr, false)) return false;

	map.clear();
	map.reserve(0x10DC * N64DD_SECTORS_PER_BLOCK);

	uint32_t lba = 0;
	uint32_t starting_block = 0;
	uint32_t source_offset = 0;
	for (uint32_t vzone = 0; vzone < 16; vzone++) {
		const uint32_t pzone = N64DD_VZONE_TO_PZONE[disk_type][vzone];
		const auto& zone = N64DD_ZONES[pzone];
		const uint32_t source_block_size = zone.sector_length * N64DD_SECTORS_PER_BLOCK;
		const uint32_t defect_start = pzone ? sys_data[0x07 + pzone] : 0;
		const uint32_t defect_stop = sys_data[0x07 + pzone + 1];
		uint32_t listed_bad_tracks = 0;
		std::vector<uint8_t> bad_tracks(zone.tracks, 0);

		for (uint32_t defect_offset = defect_start; defect_offset < defect_stop; defect_offset++) {
			if ((0x20 + defect_offset) >= sys_data.size()) return false;
			const uint32_t bad_track = sys_data[0x20 + defect_offset];
			if (bad_track < zone.tracks) bad_tracks[bad_track] = 1;
			listed_bad_tracks++;
		}

		for (uint32_t filler_track = 0; listed_bad_tracks < N64DD_BAD_TRACKS_PER_ZONE && filler_track < zone.tracks; filler_track++) {
			bad_tracks[zone.tracks - 1 - filler_track] = 1;
			listed_bad_tracks++;
		}

		for (uint32_t zone_track_index = 0; zone_track_index < zone.tracks; zone_track_index++) {
			const uint32_t zone_track = zone.head ? (zone.tracks - 1 - zone_track_index) : zone_track_index;
			const uint32_t track = zone.track_offset + zone_track;
			if (bad_tracks[zone_track]) continue;

			for (uint32_t block = 0; block < 2; block++) {
				if (lba >= bad_lbas.size()) return false;
				const uint32_t flat_block = starting_block ^ block;
				if ((source_offset + source_block_size) > N64DD_NDD_SIZE) return false;

				for (uint32_t sector = 0; sector < N64DD_SECTORS_PER_BLOCK; sector++) {
					n64dd_sector_map_entry entry = {};
					entry.flat_offset = n64dd_flat_offset(track, zone.head, flat_block, sector);
					entry.sector_length = zone.sector_length;
					if ((entry.flat_offset + entry.sector_length) > N64DD_EXPANDED_SIZE) return false;
					map.push_back(entry);
				}

				source_offset += source_block_size;
				lba++;
			}
			starting_block ^= 1;
		}
	}

	return lba == 0x10DC && source_offset == N64DD_NDD_SIZE;
}

static bool n64dd_build_ram_sector_map(const char* disk_path, uint8_t disk_type,
	std::vector<n64dd_sector_map_entry>& map, size_t& first_sector) {
	map.clear();
	first_sector = 0;
	if (disk_type >= 7) return false;
	if (!N64DD_RAM_SIZE[disk_type]) return true;

	fileTYPE file = {};
	if (!FileOpen(&file, disk_path, 1)) {
		printf("64DD RAM save: failed to reopen source disk \"%s\".\n", disk_path);
		return false;
	}

	if (file.size != N64DD_NDD_SIZE) {
		FileClose(&file);
		printf("64DD RAM save: source disk is not a compact NDD image.\n");
		return false;
	}

	uint8_t detected_disk_type = disk_type;
	const bool ok = n64dd_build_compact_ndd_sector_map(&file, detected_disk_type, map);
	FileClose(&file);
	if (!ok) return false;
	if (detected_disk_type != disk_type) {
		printf("64DD RAM save: source disk type changed from %u to %u.\n",
			(unsigned)disk_type,
			(unsigned)detected_disk_type);
		return false;
	}

	first_sector = (size_t)N64DD_RAM_START_LBA[disk_type] * N64DD_SECTORS_PER_BLOCK;
	if (first_sector > map.size()) return false;

	uint32_t ram_size = 0;
	for (size_t i = first_sector; i < map.size(); i++) ram_size += map[i].sector_length;
	if (ram_size != N64DD_RAM_SIZE[disk_type]) {
		printf("64DD RAM save: mapped size %u does not match disk type %u size %u.\n",
			(unsigned)ram_size,
			(unsigned)disk_type,
			(unsigned)N64DD_RAM_SIZE[disk_type]);
		return false;
	}

	return true;
}

static void n64dd_begin_save_ui() {
	menu_process_save();
	if (user_io_osd_is_visible()) {
		HandleUI();
		OsdUpdate();
	}
}

static bool n64dd_build_ram_image(const char* disk_path, const uint8_t* flat, uint8_t disk_type,
	std::vector<uint8_t>& ram) {
	ram.clear();
	if (disk_type >= 7) return false;
	if (!N64DD_RAM_SIZE[disk_type]) return true;

	std::vector<n64dd_sector_map_entry> map;
	size_t first_sector = 0;
	if (!n64dd_build_ram_sector_map(disk_path, disk_type, map, first_sector)) return false;

	ram.reserve(N64DD_RAM_SIZE[disk_type]);
	for (size_t i = first_sector; i < map.size(); i++) {
		const auto& entry = map[i];
		if ((entry.flat_offset + entry.sector_length) > N64DD_EXPANDED_SIZE) return false;
		ram.insert(ram.end(), flat + entry.flat_offset, flat + entry.flat_offset + entry.sector_length);
	}

	return ram.size() == N64DD_RAM_SIZE[disk_type];
}

static bool n64dd_save_ram_image(const char* save_path, const char* disk_path, uint8_t* mem,
	uint8_t disk_type, bool force) {
	if (disk_type >= 7) return false;
	if (!N64DD_RAM_SIZE[disk_type]) return true;

	std::vector<uint8_t> ram;
	if (!n64dd_build_ram_image(disk_path, mem, disk_type, ram)) return false;

	std::vector<uint32_t> dirty_blocks;
	if (!n64dd_find_dirty_flat_blocks(mem, dirty_blocks)) return false;
	if (n64dd_file_matches_vector(save_path, ram)) {
		n64dd_clear_dirty_flat_blocks(mem, dirty_blocks);
		if (force) printf("64DD RAM save \"%s\" is already up to date.\n", save_path);
		return true;
	}

	diskled_on();
	n64dd_begin_save_ui();
	if (!n64dd_write_vector_to_file(save_path, ram)) return false;

	n64dd_clear_dirty_flat_blocks(mem, dirty_blocks);
	printf("Saved 64DD MFS RAM area to \"%s\" (%u bytes).\n",
		save_path,
		(unsigned)ram.size());
	return true;
}

static bool n64dd_load_ram_save_to_mem(const char* save_path, const char* disk_path, uint8_t* mem,
	uint8_t disk_type) {
	if (!FileExists(save_path, 0)) return false;
	if (disk_type >= 7 || !N64DD_RAM_SIZE[disk_type]) return false;

	fileTYPE save_file = {};
	if (!FileOpenEx(&save_file, save_path, O_RDONLY, 0, 0)) {
		printf("64DD RAM save: failed to open \"%s\".\n", save_path);
		return false;
	}
	if ((uint64_t)save_file.size != N64DD_RAM_SIZE[disk_type]) {
		printf("64DD RAM save: ignoring \"%s\" with size %llu; disk type %u requires %u bytes.\n",
			save_path,
			(unsigned long long)save_file.size,
			(unsigned)disk_type,
			(unsigned)N64DD_RAM_SIZE[disk_type]);
		FileClose(&save_file);
		return false;
	}

	std::vector<n64dd_sector_map_entry> map;
	size_t first_sector = 0;
	if (!n64dd_build_ram_sector_map(disk_path, disk_type, map, first_sector)) {
		FileClose(&save_file);
		return false;
	}

	std::vector<uint8_t> ram;
	const bool read_ok = n64dd_copy_file_to_vector(&save_file, ram, N64DD_RAM_SIZE[disk_type],
		"Loading", save_path);
	FileClose(&save_file);
	if (!read_ok) return false;

	size_t ram_offset = 0;
	for (size_t i = first_sector; i < map.size(); i++) {
		const auto& entry = map[i];
		if ((entry.flat_offset + entry.sector_length) > N64DD_EXPANDED_SIZE ||
			(ram_offset + entry.sector_length) > ram.size()) return false;
		memcpy(mem + entry.flat_offset, ram.data() + ram_offset, entry.sector_length);
		ram_offset += entry.sector_length;
	}
	if (ram_offset != ram.size()) return false;

	printf("Loaded 64DD MFS RAM area \"%s\" (%u bytes).\n",
		save_path,
		(unsigned)ram.size());
	return true;
}

static int n64_dd_ipl_tx(fileTYPE* file, const char* name, const unsigned char idx, const uint32_t load_addr) {
	uint32_t data_size = file->size;
	const bool boot_ipl = ((idx & 0x3f) == 4);

	if (!load_addr) {
		printf("64DD IPL loading requires a DDRAM load address.\n");
		FileClose(file);
		return 0;
	}

	if (data_size > N64DD_IPL_SIZE) {
		printf("64DD IPL file is too large: %u bytes, max %u bytes.\n", data_size, N64DD_IPL_SIZE);
		FileClose(file);
		return 0;
	}
	if (data_size < 4096) {
		printf("64DD IPL file is too small: %u bytes.\n", data_size);
		FileClose(file);
		return 0;
	}

	printf("Loading 64DD IPL \"%s\" to DDRAM at 0x%08x%s.\n",
		name,
		load_addr,
		boot_ipl ? " and staging it for boot" : "");
	user_io_set_index(idx);
	user_io_set_download(1, N64DD_IPL_SIZE);
	ProgressMessage();

	uint8_t* ddipl_mem = (uint8_t*)shmem_map(fpga_mem(load_addr), N64DD_IPL_SIZE);
	uint8_t* boot_mem = boot_ipl ? (uint8_t*)shmem_map(fpga_mem(N64_ROM_FASTLOAD_ADDR), N64DD_IPL_SIZE) : nullptr;

	if (!ddipl_mem || ddipl_mem == (uint8_t*)-1 || (boot_ipl && (!boot_mem || boot_mem == (uint8_t*)-1))) {
		if (ddipl_mem && ddipl_mem != (uint8_t*)-1) shmem_unmap(ddipl_mem, N64DD_IPL_SIZE);
		if (boot_mem && boot_mem != (uint8_t*)-1) shmem_unmap(boot_mem, N64DD_IPL_SIZE);
		user_io_set_download(0);
		FileClose(file);
		printf("Failed to map 64DD IPL DDRAM buffers.\n");
		return 0;
	}

	memset(ddipl_mem, 0, N64DD_IPL_SIZE);
	if (boot_mem) memset(boot_mem, 0, N64DD_IPL_SIZE);
	bool ok = n64dd_copy_ipl_to_mem(file, name, ddipl_mem, boot_mem, data_size);

	shmem_unmap(ddipl_mem, N64DD_IPL_SIZE);
	if (boot_mem) shmem_unmap(boot_mem, N64DD_IPL_SIZE);
	FileClose(file);

	user_io_set_download(0);
	ProgressMessage();

	if (!ok) {
		printf("Failed to load 64DD IPL \"%s\".\n", name);
		return 0;
	}

	if (boot_ipl) {
		strcpy(current_rom_path, name);
		loaded = 1;
	}

	printf("Done loading 64DD IPL.\n");
	return 1;
}

static bool n64dd_make_companion_path(const char* disk_path, const char* companion_name,
	char* companion_path, size_t companion_path_size) {
	const char* slash = strrchr(disk_path, '/');
	const char* backslash = strrchr(disk_path, '\\');
	if (backslash && (!slash || backslash > slash)) slash = backslash;

	if (slash) {
		const size_t dir_len = slash - disk_path;
		if (dir_len + 1 + strlen(companion_name) >= companion_path_size) return false;
		snprintf(companion_path, companion_path_size, "%.*s/%s", (int)dir_len, disk_path, companion_name);
	} else {
		if (strlen(companion_name) >= companion_path_size) return false;
		snprintf(companion_path, companion_path_size, "%s", companion_name);
	}

	return true;
}

static bool n64dd_open_disk_ipl(fileTYPE* file, const char* disk_path, char* boot_path, size_t boot_path_size) {
	if (!n64dd_make_companion_path(disk_path, N64DD_IPL_DISK_FILE, boot_path, boot_path_size)) return false;
	return FileOpen(file, boot_path, 1);
}

static bool n64dd_make_cart_path(const char* disk_path, char* cart_path, size_t cart_path_size) {
	const size_t disk_path_len = strlen(disk_path);
	if (disk_path_len + 1 > cart_path_size) return false;

	strcpy(cart_path, disk_path);
	char* slash = strrchr(cart_path, '/');
	char* backslash = strrchr(cart_path, '\\');
	if (backslash && (!slash || backslash > slash)) slash = backslash;
	char* extension = strrchr(cart_path, '.');
	if (extension && (!slash || extension > slash)) {
		if ((size_t)(extension - cart_path) + strlen(".rom") + 1 > cart_path_size) return false;
		strcpy(extension, ".rom");
	} else {
		if (disk_path_len + strlen(".rom") + 1 > cart_path_size) return false;
		strcat(cart_path, ".rom");
	}

	return true;
}

static int n64dd_autoload_ipl(const char* disk_path, bool boot_ipl, N64DDDiskRegion disk_region) {
	fileTYPE ipl_file = {};
	char boot_path[1024];

	if (!n64dd_open_disk_ipl(&ipl_file, disk_path, boot_path, sizeof(boot_path))) {
		const char* root_ipl = N64DD_IPL_BOOT_FILE;
		if (disk_region == N64DDDiskRegion::DEVELOPMENT) root_ipl = N64DD_IPL_DEV_BOOT_FILE;
		else if (disk_region == N64DDDiskRegion::USA) root_ipl = N64DD_IPL_US_BOOT_FILE;
		snprintf(boot_path, sizeof(boot_path), "%s/%s", HomeDir(), root_ipl);
		if (!strcmp(current_rom_path, boot_path)) {
			printf("64DD IPL autoload: root %s is already loaded; keeping current IPL.\n", root_ipl);
			return 1;
		}
		if (!FileOpen(&ipl_file, boot_path, 1)) {
			printf("64DD IPL autoload: root %s not found; keeping current IPL.\n", root_ipl);
			return 1;
		}
	}

	printf("64DD IPL autoload: %s \"%s\".\n",
		boot_ipl ? "booting" : "preloading before companion cart",
		boot_path);
	if (boot_ipl) unmount_all_saves();
	return n64_dd_ipl_tx(&ipl_file, boot_path, boot_ipl ? 4 : 5, N64DD_IPL_LOAD_ADDR);
}

static int n64_dd_disk_tx(fileTYPE* file, const char* name, const unsigned char idx, const uint32_t load_addr) {
	if (!load_addr) {
		printf("64DD disk loading requires a DDRAM load address.\n");
		FileClose(file);
		return 0;
	}

	n64dd_flush_pending_save_before_disk_change();
	n64dd_clear_disk_save_state();

	char ram_save_path[1024] = {};
	n64dd_create_ram_save_path(ram_save_path, name);

	printf("Loading 64DD disk \"%s\" to DDRAM at 0x%08x.\n", name, load_addr);
	user_io_set_index(idx);
	user_io_set_download(1, N64DD_EXPANDED_SIZE);
	ProgressMessage();

	uint8_t* mem = (uint8_t*)shmem_map(fpga_mem(load_addr), N64DD_EXPANDED_SIZE);
	if (!mem || mem == (uint8_t*)-1) {
		user_io_set_download(0);
		FileClose(file);
		printf("Failed to map DDRAM for 64DD disk load.\n");
		return 0;
	}

	memset(mem, 0, N64DD_EXPANDED_SIZE);
	bool ok = false;
	bool cart_expansion_disk = false;
	bool development_disk = false;
	N64DDDiskRegion disk_region = N64DDDiskRegion::UNKNOWN;
	uint8_t disk_type = 0;
	char disk_id[5] = {};
	if (file->size == N64DD_NDD_SIZE) {
		printf("64DD disk loader: compact NDD image, expanding to flat DDR layout (%u bytes).\n", N64DD_EXPANDED_SIZE);
		ok = n64dd_expand_ndd_to_flat(file, mem, cart_expansion_disk, disk_id, disk_type, &development_disk, name);
	} else {
		printf("Unsupported 64DD disk image size: %llu bytes; expected compact NDD size %u.\n",
			(unsigned long long)file->size,
			(unsigned)N64DD_NDD_SIZE);
	}

	if (ok) {
		disk_region = development_disk ? N64DDDiskRegion::DEVELOPMENT : n64dd_detect_retail_region(file);
		if (disk_region == N64DDDiskRegion::JAPAN) {
			printf("64DD disk: Japanese retail region detected.\n");
		} else if (disk_region == N64DDDiskRegion::USA) {
			printf("64DD disk: USA retail region detected.\n");
		} else if (disk_region == N64DDDiskRegion::DEVELOPMENT) {
			printf("64DD disk: development media detected.\n");
		} else {
			printf("64DD disk: unknown retail region signature; defaulting to %s.\n", N64DD_IPL_BOOT_FILE);
		}
		if (cart_expansion_disk) {
			printf("64DD disk: cart expansion disk detected.\n");
		}
		strncpy(current_dd_ram_save_path, ram_save_path, sizeof(current_dd_ram_save_path) - 1);
		current_dd_ram_save_path[sizeof(current_dd_ram_save_path) - 1] = '\0';
		n64dd_load_ram_save_to_mem(current_dd_ram_save_path, name, mem, disk_type);
	}

	shmem_unmap(mem, N64DD_EXPANDED_SIZE);
	FileClose(file);

	user_io_set_download(0);
	ProgressMessage();

	if (!ok) {
		printf("Failed to load 64DD disk image \"%s\".\n", name);
		return 0;
	}

	strncpy(current_dd_disk_path, name, sizeof(current_dd_disk_path) - 1);
	current_dd_disk_path[sizeof(current_dd_disk_path) - 1] = '\0';
	current_dd_load_addr = load_addr;
	current_dd_disk_loaded = 1;
	current_dd_skip_initial_osd_save = user_io_osd_is_visible() ? 1 : 0;
	current_dd_cart_expansion = cart_expansion_disk ? 1 : 0;
	current_dd_disk_type = disk_type;
	strncpy(current_dd_disk_id, disk_id, sizeof(current_dd_disk_id) - 1);
	current_dd_disk_id[sizeof(current_dd_disk_id) - 1] = '\0';
	printf("Done loading 64DD disk image.\n");

	char cart_path[1024] = {};
	const bool autoload_cart =
		n64dd_make_cart_path(name, cart_path, sizeof(cart_path)) &&
		FileExists(cart_path, 0);
	if (!n64dd_autoload_ipl(name, !autoload_cart, disk_region)) return 0;
	if (!autoload_cart) return 1;

	printf("64DD cart autoload: loading \"%s\" after disk and IPL.\n", cart_path);
	uint32_t cart_crc = 0;
	return n64_rom_tx(cart_path, 1, N64_ROM_FASTLOAD_ADDR, cart_crc);
}

int n64_rom_tx(const char* name, const unsigned char idx, const uint32_t load_addr, uint32_t& file_crc) {
	static uint8_t buf[4096];
	fileTYPE f;

	if (!FileOpen(&f, name, 1)) {
		return 0;
	}

	uint32_t data_size = f.size;
	uint32_t data_left = data_size;

	printf("N64 file \"%s\" with %u bytes to send for index %02x.\n", name, data_size, idx);

	if (((idx & 0x3f) == 3) && data_size) {
		file_crc = 0;
		return n64_dd_disk_tx(&f, name, idx, load_addr);
	}
	if (((idx & 0x3f) == 4) && data_size) {
		file_crc = 0;
		unmount_all_saves();
		return n64_dd_ipl_tx(&f, name, idx, load_addr);
	}

	if (((idx & 0x3f) == 2) && data_size) {
		// Handle non-N64 files (Game Boy) for Transfer Pak.
		const bool gb_changed = !(*current_rom_path_gb) || strcmp(current_rom_path_gb, name);
		bool should_reset = false;
		file_crc = 0;

		if (gb_changed) strcpy(current_rom_path_gb, name);

		unmount_all_saves();
		user_io_set_index(idx);

		if (*current_rom_path && user_io_status_get(TPAK_OPT)) {
			mount_all_saves();

			if (gb_changed) {
				// Force reload of backup RAM after switching the mounted Game Boy save.
				user_io_status_set(RELOAD_SAVE_OPT, 1);
				usleep(100000);
				user_io_status_set(RELOAD_SAVE_OPT, 0);

				// Hold reset while the new Game Boy ROM is streamed.
				user_io_status_set(RESET_OPT, 1);
				should_reset = true;
			}
		}

		user_io_set_download(1);
		ProgressMessage();

		while (data_left) {
			uint32_t chunk = (data_left > sizeof(buf)) ? sizeof(buf) : data_left;
			FileReadAdv(&f, buf, chunk);

			user_io_file_tx_data(buf, chunk);

			ProgressMessage("Loading", n64_leaf_name(f.name), data_size - data_left, data_size);
			data_left -= chunk;
		}

		printf("Done loading Game Boy ROM.\n");
		FileClose(&f);

		user_io_set_download(0);
		ProgressMessage();

		if (should_reset) {
			user_io_status_set(RESET_OPT, 0);
		}

		return 1;
	}

	const uint8_t dd_disk_was_loaded = current_dd_disk_loaded;
	const uint8_t dd_disk_was_cart_expansion = current_dd_cart_expansion;
	const char dd_disk_was_id[5] = {
		current_dd_disk_id[0],
		current_dd_disk_id[1],
		current_dd_disk_id[2],
		current_dd_disk_id[3],
		'\0'
	};
	n64dd_flush_pending_save_before_disk_change();
	unmount_all_saves();

	// Set index byte
	user_io_set_index(idx);

	loaded = 0;

	if (rdram_ptr && rdram_ptr != (void*)-1) {
		shmem_unmap(rdram_ptr, RAM_SIZE);
		rdram_ptr = nullptr;
	}

	// save state processing
	process_ss(name);

	/* 0 = Nothing detected
	   1 = System region and CIC detected
	   2 = Found some ROM info in DB (Save type etc.), but System region and/or CIC has not been determined
	   3 = Has detected everything, System type, CIC, Save type etc. */
	uint8_t rom_settings_detected = 0;
	ByteOrder rom_endianness;
	uint8_t md5[MD5_LENGTH];
	char md5_hex[MD5_LENGTH * 2 + 1];
	uint64_t bootcode_sums[2] = { };
	uint8_t controller_settings[4] = { };
	bool is_first_chunk = true;
	char cart_id[CARTID_LENGTH + 1] = { };
	char internal_name[20 + 1];

	memset(patches, 0, sizeof(patches));

	// CRC32 is used for cheat look-up
	file_crc = 0;

	void* mem = load_addr ? (uint8_t*)shmem_map(fpga_mem(load_addr), data_size) : nullptr;
	uint8_t* write_ptr = (uint8_t*)mem;

	MD5Context ctx;
	MD5Init(&ctx);

	// prepare transmission of new file
	user_io_set_download(1, load_addr ? data_size : 0);
	ProgressMessage();

	while (data_left) {
		size_t chunk = (data_left > sizeof(buf)) ? sizeof(buf) : data_left;

		FileReadAdv(&f, buf, chunk);

		// Perform sanity checks and detect ROM endianness
		if (is_first_chunk) {
			if (chunk < 4096) {
				// Signal end of transmission
				user_io_set_download(0);
				*current_rom_path = '\0';
				printf("Failed to load ROM: must be at least 4096 bytes.\n");

				return 0;
			}

			rom_endianness = detect_rom_endianness(buf);
		}

		// Normalize data to big-endian format, if needed
		normalize_data(buf, chunk, rom_endianness);
		MD5Update(&ctx, buf, chunk);

		if (is_first_chunk) {
			/* Try to detect ROM settings based on header MD5 hash.
			   For calculating the MD5 hash of the header, we need to make a
			   copy of the context before calling MD5Final, otherwise the file
			   hash will be incorrect later on. */

			MD5Context ctx_header;
			memcpy(&ctx_header, &ctx, sizeof(struct MD5Context));
			MD5Final(md5, &ctx_header);
			md5_to_hex(md5, md5_hex);
			printf("Header MD5 hash: %s\n", md5_hex);

			trim(internal_name, sizeof(internal_name), (char*)&buf[0x20]);
			rom_settings_detected = detect_rom_settings_in_dbs_with_md5(md5_hex);
			memcpy(controller_settings, &buf[0x34], sizeof(controller_settings));
			calc_bootcode_checksums(bootcode_sums, buf);

			/* The first byte (starting at 0x3b) indicates the type of ROM
				 'N' = Cartridge
				 'D' = 64DD disk
				 'C' = Cartridge part of expandable game
				 'E' = 64DD expansion for cart
				 'Z' = Aleck64 cart
			   The 2nd and 3rd byte form a 2-letter ID for the game
			   The 4th byte indicates the region and language for the game
			   The 5th byte indicates the revision of the game */

			auto p_cid = (char*)&buf[0x3b];
			for (auto i = 0; i < 4; i++, p_cid++) {
				if (isalnum(*p_cid)) {
					cart_id[i] = *p_cid;
				}
				else {
					cart_id[i] = '?';
				}
			}

			if (strncmp(cart_id, "????", 4)) {
				sprintf(cart_id + 4, "%02X", buf[0x3f]);
				printf("Cartridge ID: %s\n", cart_id);
			}
			else {
				memset(cart_id, '\0', CARTID_LENGTH);
			}
		}

		// Copy to DDR memory for fast ROM loading
		if (mem) {
			memcpy(write_ptr, buf, chunk);
			write_ptr += chunk;
		}
		else {
			// Fallback to normal (slow) loading
			user_io_file_tx_data(buf, chunk);
		}

		ProgressMessage("Loading", n64_leaf_name(f.name), data_size - data_left, data_size);
		data_left -= chunk;
		is_first_chunk = false;

		// CRC32 is used for cheat look-up. Cheat files from gamehacking.org use byte swapped CRC32 for some reason...
		normalize_data(buf, chunk, ByteOrder::BYTE_SWAPPED);
		file_crc = crc32(file_crc, buf, chunk);
	}

	MD5Final(md5, &ctx);
	md5_to_hex(md5, md5_hex);
	printf("File MD5: %s\n", md5_hex);

	// Try to detect ROM settings from full file MD5 if they're are not detected yet
	if (!rom_settings_detected) {
		printf("No ROM information found for header hash.\n");
		rom_settings_detected = detect_rom_settings_in_dbs_with_md5(md5_hex);
		// Try to detect ROM settings by cart ID if they're are not detected yet
		if (!rom_settings_detected) {
			printf("No ROM information found for file hash.\n");
			rom_settings_detected = detect_rom_settings_in_dbs_with_cartid(cart_id);
			if (!rom_settings_detected) {
				if (detect_homebrew_header(controller_settings, cart_id)) {
					rom_settings_detected = 2;
				}
				else {
					printf("No ROM information found for Cart ID.\n");
					if (is_auto()) {
						// Defaulting misc. System Settings, everything OFF
						user_io_status_set(NO_EPAK_OPT, 0); // Enable Expansion Pak
						user_io_status_set(CPAK_OPT, 0); // Disable Controller Pak
						user_io_status_set(RPAK_OPT, 0); // Disable Rumble Pak
						user_io_status_set(TPAK_OPT, 0); // Disable Transfer Pak
						user_io_status_set(RTC_OPT, 0); // Disable RTC
						set_cart_save_type(MemoryType::NONE); // Disable Save
					}
				}
			}
		}
	}

	if (!(rom_settings_detected & 1) &&
		detect_rom_settings_from_first_chunk(cart_id[3], bootcode_sums, sizeof(bootcode_sums) / sizeof(*bootcode_sums))) {
		// Try detect (partial) ROM settings by analyzing the ROM itself. (System region and CIC)
		rom_settings_detected |= 1;
	}

	printf("Done loading N64 ROM.\n");
	printf("CRC32: %08X\n", file_crc);
	FileClose(&f);

	// Write game ID if cart_id exists (BIOS check is handled in user_io_write_gameid)
	if (cart_id[0])
	{
		user_io_write_gameid(name, file_crc, cart_id);
	}

	bool is_patched = false;

	if (mem) {
		if (patches_enabled()) {
			for (auto patch = patches; patch->address * 4 < data_size && patch->diff; patch++) {
				if (patch->turbo_core_only && !is_turbo_core()) continue;
				((uint32_t*)mem)[patch->address] ^= patch->diff;
				is_patched = true;
			}
		}

		shmem_unmap(mem, data_size);
	}

	strcpy(current_rom_path, name);

	const bool dd_disk_matches_cart =
		dd_disk_was_id[0] &&
		cart_id[0] &&
		!memcmp(dd_disk_was_id + 1, cart_id + 1, 3);
	const bool preserve_dd_disk =
		dd_disk_was_loaded &&
		(cart_id[0] == 'C' || (dd_disk_was_cart_expansion && dd_disk_matches_cart));
	if (preserve_dd_disk) {
		printf("64DD cart expansion: preserving loaded disk %.4s for cart ID %.4s.\n", dd_disk_was_id, cart_id);
	} else {
		if (dd_disk_was_loaded) {
			printf("64DD cart expansion: clearing loaded disk %.4s for cart ID %.4s.\n", dd_disk_was_id, cart_id);
		}
		n64dd_clear_disk_save_state();
	}

	mount_all_saves();

	// Signal end of transmission
	user_io_set_download(0);
	ProgressMessage();

	loaded = 1;

	if (!cfg.controller_info || !is_auto()) {
		return 1;
	}

	auto system_type = SystemType::UNKNOWN;
	auto cic = CIC::UNKNOWN;

	char info[256];
	size_t len = sprintf(info, "Auto-detect:");
	if (*cart_id && ((cart_id[1] != 'E') || (cart_id[2] != 'D'))) len += sprintf(info + len, "\n[%.4s] v.%u.%u", cart_id, hex_to_dec(cart_id[4]) + 1, hex_to_dec(cart_id[5]));
	if (*internal_name) len += sprintf(info + len, "\n\"%s\"", internal_name);
	if (!(rom_settings_detected & 1)) {
		len += sprintf(info + len, "\nUnknown Region/CIC");
	}
	else {
		system_type = (SystemType)user_io_status_get(SYS_TYPE_OPT);
		cic = (CIC)user_io_status_get(CIC_TYPE_OPT);
		len += sprintf(info + len, "\nRegion: %s (%s)", stringify(system_type), stringify(cic));
	}

	if (!(rom_settings_detected & 2)) {
		sprintf(info + len, "\nROM missing from database.\nYou might not be able to save.");

		Info(info, cfg.controller_info * 1000);
	}
	else {
		auto save_type = get_cart_save_type();
		auto no_epak = (bool)user_io_status_get(NO_EPAK_OPT);
		auto tpak = (bool)user_io_status_get(TPAK_OPT);
		auto cpak = (bool)user_io_status_get(CPAK_OPT);
		auto rpak = (bool)user_io_status_get(RPAK_OPT);
		auto rtc = (bool)user_io_status_get(RTC_OPT);

		if (save_type != MemoryType::NONE) len += sprintf(info + len, "\nSave Type: %s", stringify(save_type));
		if (tpak) len += sprintf(info + len, "\nTransfer Pak \x96");
		if (cpak) len += sprintf(info + len, "\nController Pak \x96");
		if (rpak) len += sprintf(info + len, "\nRumble Pak \x96");
		if (rtc) len += sprintf(info + len, "\nRTC \x96");
		if (no_epak) len += sprintf(info + len, "\nDisable Exp. Pak \x96");
		if (is_patched) sprintf(info + len, "\nPatched \x96");

		Info(info, cfg.controller_info * 1000);
	}

	return 1;
}

#pragma pop_macro("NONE")
#pragma pop_macro("BIG_ENDIAN")
#pragma pop_macro("LITTLE_ENDIAN")
