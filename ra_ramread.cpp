#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "shmem.h"
#include "ra_ramread.h"

#define RA_DBG(fmt, ...) printf("\033[1;35mRA_MEM: " fmt "\033[0m\n", ##__VA_ARGS__)

static uint32_t g_ra_ddram_base = RA_DDRAM_PHYS_BASE;

void ra_ramread_set_base(uint32_t phys_base)
{
	g_ra_ddram_base = phys_base;
}

uint32_t ra_ramread_get_base(void)
{
	return g_ra_ddram_base;
}

void *ra_ramread_map(void)
{
	return shmem_map(g_ra_ddram_base, RA_DDRAM_MAP_SIZE);
}

void ra_ramread_unmap(void *map)
{
	if (map) shmem_unmap(map, RA_DDRAM_MAP_SIZE);
}

int ra_ramread_active(const void *map)
{
	if (!map) return 0;
	const ra_header_t *hdr = (const ra_header_t *)map;
	return hdr->magic == RA_MAGIC;
}

uint32_t ra_ramread_frame(const void *map)
{
	if (!map) return 0;
	const ra_header_t *hdr = (const ra_header_t *)map;
	if (hdr->magic != RA_MAGIC) return 0;
	return hdr->frame_counter;
}

int ra_ramread_busy(const void *map)
{
	if (!map) return 0;
	const ra_header_t *hdr = (const ra_header_t *)map;
	return (hdr->flags & RA_FLAG_BUSY) ? 1 : 0;
}

int ra_ramread_get_core_version(const void *map, uint8_t *major, uint8_t *minor)
{
	if (major) *major = 0;
	if (minor) *minor = 0;
	if (!map) return 0;
	const ra_header_t *hdr = (const ra_header_t *)map;
	if (hdr->magic != RA_MAGIC || hdr->core_version == 0) return 0;
	if (major) *major = (hdr->core_version >> 8) & 0xFF;
	if (minor) *minor = hdr->core_version & 0xFF;
	return 1;
}

static const ra_region_desc_t *get_region_desc(const void *map, int region_index)
{
	const ra_header_t *hdr = (const ra_header_t *)map;
	if (!map || hdr->magic != RA_MAGIC) return NULL;
	if (region_index < 0 || region_index >= hdr->region_count) return NULL;
	if (region_index >= RA_MAX_REGIONS) return NULL;

	// Descriptors start at offset 0x10, each is 8 bytes
	const uint8_t *base = (const uint8_t *)map;
	return (const ra_region_desc_t *)(base + 0x10 + region_index * 8);
}

const uint8_t *ra_ramread_region_data(const void *map, int region_index)
{
	const ra_region_desc_t *desc = get_region_desc(map, region_index);
	if (!desc || desc->size == 0) return NULL;

	const uint8_t *base = (const uint8_t *)map;
	return base + desc->ddram_offset;
}

uint16_t ra_ramread_region_size(const void *map, int region_index)
{
	const ra_region_desc_t *desc = get_region_desc(map, region_index);
	if (!desc) return 0;
	return desc->size;
}

uint8_t ra_ramread_nes_byte(const void *map, uint16_t nes_addr)
{
	// NES CPU address space:
	// $0000-$1FFF: Internal RAM (2KB, mirrored 4x) -> Region 0
	// $6000-$7FFF: Cart SRAM/WRAM -> Region 1
	if (nes_addr < 0x2000) {
		uint16_t offset = nes_addr & 0x07FF; // Resolve mirrors
		const uint8_t *data = ra_ramread_region_data(map, RA_NES_CPURAM_REGION);
		uint16_t size = ra_ramread_region_size(map, RA_NES_CPURAM_REGION);
		if (data && offset < size) return data[offset];
		return 0;
	}
	else if (nes_addr >= 0x6000 && nes_addr <= 0x7FFF) {
		uint16_t offset = nes_addr - 0x6000;
		const uint8_t *data = ra_ramread_region_data(map, RA_NES_CARTRAM_REGION);
		uint16_t size = ra_ramread_region_size(map, RA_NES_CARTRAM_REGION);
		if (data && offset < size) return data[offset];
		return 0;
	}

	return 0;
}

uint32_t ra_ramread_nes_read(const void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes)
{
	for (uint32_t i = 0; i < num_bytes; i++) {
		buffer[i] = ra_ramread_nes_byte(map, (uint16_t)(address + i));
	}
	return num_bytes;
}

uint32_t ra_ramread_snes_read(const void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes)
{
	if (!map) { memset(buffer, 0, num_bytes); return num_bytes; }
	const uint8_t *base = (const uint8_t *)map;
	const ra_header_t *hdr = (const ra_header_t *)base;
	if (hdr->magic != RA_MAGIC) { memset(buffer, 0, num_bytes); return num_bytes; }

	// BSRAM size stored at offset 0x0C (reserved2 field)
	uint32_t bsram_sz = hdr->reserved2;

	for (uint32_t i = 0; i < num_bytes; i++) {
		uint32_t addr = address + i;
		if (addr < RA_SNES_WRAM_SIZE) {
			// WRAM: 128KB at mirror offset 0x100
			uint32_t off = RA_SNES_WRAM_OFFSET + addr;
			if (off < RA_DDRAM_MAP_SIZE)
				buffer[i] = base[off];
			else
				buffer[i] = 0;
		} else {
			// BSRAM: at mirror offset 0x20100
			uint32_t sram_off = addr - RA_SNES_WRAM_SIZE;
			if (sram_off < bsram_sz) {
				uint32_t off = RA_SNES_BSRAM_OFFSET + sram_off;
				if (off < RA_DDRAM_MAP_SIZE)
					buffer[i] = base[off];
				else
					buffer[i] = 0;
			} else {
				buffer[i] = 0;
			}
		}
	}
	return num_bytes;
}

uint8_t ra_ramread_atari2600_byte(const void *map, uint16_t addr)
{
	// rcheevos maps Atari 2600 RIOT RAM as a 128-byte block starting at address 0.
	// This matches Stella2014's retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM) which
	// returns a direct pointer to the 128-byte RIOT RAM — so rcheevos address 0x00
	// = RIOT byte 0 = CPU $0080, and rcheevos address 0x4E = RIOT byte 0x4E = CPU $00CE.
	// The old check (addr & 0x0080) was wrong: it rejected all addresses below 0x80.
	if (addr < 128) {
		if (!map) return 0;
		const ra_header_t *hdr = (const ra_header_t *)map;
		if (hdr->magic != RA_MAGIC) return 0;

		// Bypass the region descriptor: ra_riot_mirror always writes RIOT data
		// at a fixed DDRAM offset of 0x100 with size 128.
		const uint8_t *data = (const uint8_t *)map + 0x100;
		return data[addr];
	}
	return 0;
}

uint32_t ra_ramread_atari2600_read(const void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes)
{
	for (uint32_t i = 0; i < num_bytes; i++) {
		buffer[i] = ra_ramread_atari2600_byte(map, (uint16_t)(address + i));
	}
	return num_bytes;
}

// ---------------------------------------------------------------------------
// Atari 7800 -- 4 KB internal RAM (ram0 + ram1)
//
// Atari 7800 hardware memory map:
//   ram1 (2KB): physical 0x1800-0x1FFF -> BRAM index = addr & 0x7FF -> DDRAM+0x900
//   ram0 (2KB): physical 0x2000-0x27FF -> BRAM index = addr & 0x7FF -> DDRAM+0x100
//               mirrors   0x0040-0x00FF -> BRAM index = addr & 0x7FF -> DDRAM+0x100
//               mirrors   0x0140-0x01FF -> BRAM index = addr & 0x7FF -> DDRAM+0x100
//
// BRAM is indexed by AB[10:0] which equals addr & 0x7FF for all valid ranges.
// rcheevos achievement conditions use the actual hardware addresses.
// ---------------------------------------------------------------------------
uint8_t ra_ramread_atari7800_byte(const void *map, uint16_t addr)
{
	if (!map) return 0;
	const ra_header_t *hdr = (const ra_header_t *)map;
	if (hdr->magic != RA_MAGIC) return 0;
	// ram1: physical 0x1800-0x1FFF
	if (addr >= 0x1800 && addr <= 0x1FFF)
		return ((const uint8_t *)map + 0x900)[addr & 0x7FF];
	// ram0: physical 0x2000-0x27FF (main) and zero-page/stack mirrors
	// ProSystem (reference emulator for Atari 7800 achievements) stores game
	// variables 8 bytes higher in BRAM than our FPGA. The -8 offset compensates.
	// However, system/BIOS variables in BRAM[0x00-0x7F] map directly (no offset).
	// Game variables in BRAM[0x80+] need the -8 shift.
	// Example: RA addr 0x20C4 (bram=0xC4 >=0x80) -> BRAM[0xBC] = hammer timer.
	//          RA addr 0x205B (bram=0x5B < 0x80) -> BRAM[0x5B] = 0 (no reset).
	if ((addr >= 0x2000 && addr <= 0x27FF) ||
	    (addr >= 0x0040 && addr <= 0x00FF) ||
	    (addr >= 0x0140 && addr <= 0x01FF)) {
		uint16_t bram_idx = addr & 0x7FF;
		return ((const uint8_t *)map + 0x100)[bram_idx];
	}
	return 0;
}

uint32_t ra_ramread_atari7800_read(const void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes)
{
	for (uint32_t i = 0; i < num_bytes; i++) {
		buffer[i] = ra_ramread_atari7800_byte(map, (uint16_t)(address + i));
	}
	return num_bytes;
}

void ra_ramread_debug_dump(const void *map)
{
	RA_DBG("=== DDRAM Mirror Diagnostic Dump ===");
	RA_DBG("Base address: 0x%08X (size: 0x%X)", g_ra_ddram_base, RA_DDRAM_MAP_SIZE);

	if (!map) {
		RA_DBG("ERROR: map pointer is NULL");
		return;
	}

	const ra_header_t *hdr = (const ra_header_t *)map;
	RA_DBG("Header raw bytes: %02X %02X %02X %02X %02X %02X %02X %02X",
		((const uint8_t *)map)[0], ((const uint8_t *)map)[1],
		((const uint8_t *)map)[2], ((const uint8_t *)map)[3],
		((const uint8_t *)map)[4], ((const uint8_t *)map)[5],
		((const uint8_t *)map)[6], ((const uint8_t *)map)[7]);

	if (hdr->magic != RA_MAGIC) {
		RA_DBG("Magic: 0x%08X (INVALID - expected 0x%08X 'RACH')", hdr->magic, RA_MAGIC);
		RA_DBG("Mirror not active. FPGA core may not support RA or not started yet.");
		return;
	}

	RA_DBG("Magic: 0x%08X (OK - 'RACH')", hdr->magic);
	RA_DBG("Region count: %d", hdr->region_count);
	RA_DBG("Flags: 0x%02X (busy=%d)", hdr->flags, (hdr->flags & RA_FLAG_BUSY) ? 1 : 0);
	RA_DBG("Frame counter: %u", hdr->frame_counter);

	for (int i = 0; i < hdr->region_count && i < RA_MAX_REGIONS; i++) {
		const ra_region_desc_t *desc = (const ra_region_desc_t *)((const uint8_t *)map + 0x10 + i * 8);
		RA_DBG("Region %d: sdram_addr=0x%06X size=%u ddram_offset=0x%04X",
			i, desc->sdram_addr, desc->size, desc->ddram_offset);

		const uint8_t *data = (const uint8_t *)map + desc->ddram_offset;
		if (desc->size > 0 && (uint32_t)desc->ddram_offset < RA_DDRAM_MAP_SIZE) {
			int dump_len = desc->size < 64 ? desc->size : 64;
			printf("\033[1;35mRA_MEM:   First %d bytes: ", dump_len);
			for (int j = 0; j < dump_len; j++) {
				printf("%02X ", data[j]);
				if ((j & 0xF) == 0xF && j + 1 < dump_len) printf("\n                         ");
			}
			printf("\033[0m\n");

			// Check if all zeros (common if mirror not writing yet)
			int all_zero = 1;
			for (int j = 0; j < dump_len; j++) {
				if (data[j] != 0) { all_zero = 0; break; }
			}
			if (all_zero) {
				RA_DBG("  WARNING: Region data is all zeros");
			}
		}
	}

	RA_DBG("=== End Diagnostic Dump ===");
}

void ra_ramread_debug_status(const void *map)
{
	if (!map) {
		RA_DBG("STATUS: not mapped");
		return;
	}
	const ra_header_t *hdr = (const ra_header_t *)map;
	if (hdr->magic != RA_MAGIC) {
		RA_DBG("STATUS: inactive (bad magic 0x%08X)", hdr->magic);
		return;
	}
	RA_DBG("STATUS: frame=%u regions=%d busy=%d",
		hdr->frame_counter, hdr->region_count, (hdr->flags & RA_FLAG_BUSY) ? 1 : 0);
}

// ======================================================================
// Option C: Selective Address Reading (SNES)
// ======================================================================

static int s_addr_cmp(const void *a, const void *b)
{
	uint32_t va = *(const uint32_t *)a;
	uint32_t vb = *(const uint32_t *)b;
	return (va > vb) - (va < vb);
}

static uint32_t s_snes_addrs[RA_SNES_MAX_ADDRS];
static int      s_snes_addr_count = 0;
static uint32_t s_snes_request_id = 0;
static int      s_snes_collecting = 0;

#define COLLECT_BUF_MAX (RA_SNES_MAX_ADDRS * 4)
static uint32_t s_collect_buf[COLLECT_BUF_MAX];
static int      s_collect_count = 0;

void ra_snes_addrlist_init(void)
{
	s_snes_addr_count = 0;
	s_snes_request_id = 0;
	s_snes_collecting = 0;
	s_collect_count = 0;
}

void ra_snes_addrlist_begin_collect(void)
{
	s_snes_collecting = 1;
	s_collect_count = 0;
}

void ra_snes_addrlist_add(uint32_t addr)
{
	if (!s_snes_collecting) return;
	if (s_collect_count < COLLECT_BUF_MAX)
		s_collect_buf[s_collect_count++] = addr;
}

int ra_snes_addrlist_end_collect(void *map)
{
	s_snes_collecting = 0;
	if (s_collect_count == 0) return 0;

	// Sort
	qsort(s_collect_buf, s_collect_count, sizeof(uint32_t), s_addr_cmp);

	// Deduplicate
	int new_count = 0;
	for (int i = 0; i < s_collect_count; i++) {
		if (new_count == 0 || s_collect_buf[i] != s_collect_buf[new_count - 1])
			s_collect_buf[new_count++] = s_collect_buf[i];
	}
	if (new_count > RA_SNES_MAX_ADDRS)
		new_count = RA_SNES_MAX_ADDRS;

	// Compare with current list
	int changed = (new_count != s_snes_addr_count);
	if (!changed) {
		for (int i = 0; i < new_count; i++) {
			if (s_collect_buf[i] != s_snes_addrs[i]) { changed = 1; break; }
		}
	}
	if (!changed) return 0;

	// Update local list
	memcpy(s_snes_addrs, s_collect_buf, new_count * sizeof(uint32_t));
	s_snes_addr_count = new_count;
	s_snes_request_id++;

	// Write to DDRAM
	if (!map) return 1;
	uint8_t *base = (uint8_t *)map;

	// Write addresses first (before header, so FPGA sees consistent data)
	uint32_t *addrs = (uint32_t *)(base + RA_SNES_ADDRLIST_OFFSET + 8);
	memcpy(addrs, s_snes_addrs, new_count * sizeof(uint32_t));
	__sync_synchronize();

	// Write header: addr_count first, then request_id as the "commit" signal.
	// FPGA reads both atomically as a 64-bit word. If it catches an in-between
	// state, seeing old request_id with new addr_count is safe (it will process
	// addresses but ARM won't see the response as ready until request_id matches).
	ra_addr_req_hdr_t *hdr = (ra_addr_req_hdr_t *)(base + RA_SNES_ADDRLIST_OFFSET);
	hdr->addr_count = new_count;
	__sync_synchronize();
	hdr->request_id = s_snes_request_id;
	__sync_synchronize();

	RA_DBG("AddrList: %d addrs, request_id=%u, first_addr=0x%05X",
		new_count, s_snes_request_id,
		new_count > 0 ? s_snes_addrs[0] : 0);
	return 1;
}

uint8_t ra_snes_addrlist_read_cached(const void *map, uint32_t addr)
{
	if (!map || s_snes_addr_count == 0) return 0;

	// Binary search
	int lo = 0, hi = s_snes_addr_count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (s_snes_addrs[mid] == addr) {
			const uint8_t *vals = (const uint8_t *)map + RA_SNES_VALCACHE_OFFSET + 8;
			return vals[mid];
		}
		if (s_snes_addrs[mid] < addr) lo = mid + 1;
		else hi = mid - 1;
	}
	return 0;
}

int ra_snes_addrlist_is_ready(const void *map)
{
	if (!map || s_snes_addr_count == 0 || s_snes_request_id == 0) return 0;
	const uint8_t *base = (const uint8_t *)map;
	const ra_val_resp_hdr_t *resp = (const ra_val_resp_hdr_t *)(base + RA_SNES_VALCACHE_OFFSET);
	return resp->response_id == s_snes_request_id;
}

int ra_snes_addrlist_count(void)
{
	return s_snes_addr_count;
}

const uint32_t *ra_snes_addrlist_addrs(void)
{
	return s_snes_addrs;
}

uint32_t ra_snes_addrlist_request_id(void)
{
	return s_snes_request_id;
}

uint32_t ra_snes_addrlist_response_frame(const void *map)
{
	if (!map) return 0;
	const uint8_t *base = (const uint8_t *)map;
	const ra_val_resp_hdr_t *resp = (const ra_val_resp_hdr_t *)(base + RA_SNES_VALCACHE_OFFSET);
	return resp->response_frame;
}

void ra_snes_addrlist_diag_dump(const void *map)
{
	if (!map || s_snes_addr_count == 0) return;
	const uint8_t *base = (const uint8_t *)map;

	// Dump VALCACHE response header
	const ra_val_resp_hdr_t *resp = (const ra_val_resp_hdr_t *)(base + RA_SNES_VALCACHE_OFFSET);
	RA_DBG("VALCACHE hdr: resp_id=%u resp_frame=%u (expect req_id=%u)",
		resp->response_id, resp->response_frame, s_snes_request_id);

	// Dump first 32 raw bytes from VALCACHE+8 (value area)
	const uint8_t *vals = base + RA_SNES_VALCACHE_OFFSET + 8;
	int dump_len = s_snes_addr_count < 32 ? s_snes_addr_count : 32;
	printf("\033[1;35mRA_MEM: VALCACHE raw[0..%d]: ", dump_len - 1);
	int non_zero = 0;
	for (int i = 0; i < dump_len; i++) {
		printf("%02X ", vals[i]);
		if (vals[i]) non_zero++;
	}
	printf("\033[0m\n");
	RA_DBG("VALCACHE: %d/%d non-zero in first %d bytes", non_zero, dump_len, dump_len);

	// Dump first 5 addresses for reference
	printf("\033[1;35mRA_MEM: Addrs[0..4]: ");
	for (int i = 0; i < 5 && i < s_snes_addr_count; i++) {
		printf("0x%05X ", s_snes_addrs[i]);
	}
	printf("\033[0m\n");

	// Dump ADDRLIST header as seen from DDRAM
	const ra_addr_req_hdr_t *ahdr = (const ra_addr_req_hdr_t *)(base + RA_SNES_ADDRLIST_OFFSET);
	RA_DBG("ADDRLIST hdr in DDRAM: count=%u req_id=%u", ahdr->addr_count, ahdr->request_id);
}

// ======================================================================
// Smart Cache: incremental address management
// ======================================================================

static int s_dynamic_pending = 0;   // 1 if new addresses added since last flush
static int s_dynamic_added   = 0;   // count of addresses added this cycle

int ra_snes_addrlist_contains(uint32_t addr)
{
	if (s_snes_addr_count == 0) return -1;
	int lo = 0, hi = s_snes_addr_count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (s_snes_addrs[mid] == addr) return mid;
		if (s_snes_addrs[mid] < addr) lo = mid + 1;
		else hi = mid - 1;
	}
	return -1;
}

int ra_snes_addrlist_add_dynamic(uint32_t addr)
{
	// Check if already in the list
	if (s_snes_addr_count > 0) {
		int lo = 0, hi = s_snes_addr_count - 1;
		int insert_pos = s_snes_addr_count; // default: append at end
		while (lo <= hi) {
			int mid = (lo + hi) / 2;
			if (s_snes_addrs[mid] == addr) return 0; // already exists
			if (s_snes_addrs[mid] < addr) lo = mid + 1;
			else { insert_pos = mid; hi = mid - 1; }
		}
		if (lo < s_snes_addr_count && s_snes_addrs[lo] > addr)
			insert_pos = lo;
		if (s_snes_addr_count >= RA_SNES_MAX_ADDRS) return 0; // list full

		// Insert at sorted position
		if (insert_pos < s_snes_addr_count) {
			memmove(&s_snes_addrs[insert_pos + 1],
				&s_snes_addrs[insert_pos],
				(s_snes_addr_count - insert_pos) * sizeof(uint32_t));
		}
		s_snes_addrs[insert_pos] = addr;
	} else {
		if (s_snes_addr_count >= RA_SNES_MAX_ADDRS) return 0;
		s_snes_addrs[0] = addr;
	}
	s_snes_addr_count++;
	s_dynamic_pending = 1;
	s_dynamic_added++;
	return 1;
}

int ra_snes_addrlist_has_pending(void)
{
	return s_dynamic_pending;
}

int ra_snes_addrlist_flush_dynamic(void *map)
{
	if (!s_dynamic_pending || !map) return 0;
	s_dynamic_pending = 0;
	int flushed = s_dynamic_added;
	s_dynamic_added = 0;

	// Bump request ID and write entire list to DDRAM
	s_snes_request_id++;
	uint8_t *base = (uint8_t *)map;

	// Write addresses first
	uint32_t *addrs = (uint32_t *)(base + RA_SNES_ADDRLIST_OFFSET + 8);
	memcpy(addrs, s_snes_addrs, s_snes_addr_count * sizeof(uint32_t));
	__sync_synchronize();

	// Write header
	ra_addr_req_hdr_t *hdr = (ra_addr_req_hdr_t *)(base + RA_SNES_ADDRLIST_OFFSET);
	hdr->addr_count = s_snes_addr_count;
	__sync_synchronize();
	hdr->request_id = s_snes_request_id;
	__sync_synchronize();

	RA_DBG("SmartCache flush: %d new addrs, total=%d, request_id=%u",
		flushed, s_snes_addr_count, s_snes_request_id);
	return 1;
}

int ra_snes_addrlist_dynamic_count(void)
{
	return s_dynamic_added;
}

// ======================================================================
// Realtime Query Mailbox (Option C "on steroids")
// ======================================================================

static uint8_t s_rtquery_seq = 0;

void ra_rtquery_init(void *map)
{
        s_rtquery_seq = 0;
        if (!map) return;

        // Clear the control word so FPGA sees no pending request
        uint8_t *base = (uint8_t *)map;
        ra_query_ctrl_t *ctrl = (ra_query_ctrl_t *)(base + RA_QUERY_CTRL_OFFSET);
        memset(ctrl, 0, sizeof(*ctrl));
        __sync_synchronize();

        // Signal FPGA that rtquery polling is now needed.
        // The FPGA reads RA_ARM_CONFIG_OFFSET once per VBlank and starts polling
        // the query mailbox only when this bit is set. This prevents ~107k unnecessary
        // DDRAM reads per second on cores where rtquery is enabled but not actively used.
        base[RA_ARM_CONFIG_OFFSET] |= RA_ARM_CFG_RTQUERY;
        __sync_synchronize();

        RA_DBG("RTQuery: initialized (mailbox at offset 0x%X)", RA_QUERY_CTRL_OFFSET);
}

void ra_rtquery_disable(void *map)
{
        if (!map) return;
        uint8_t *base = (uint8_t *)map;
        base[RA_ARM_CONFIG_OFFSET] &= (uint8_t)(~RA_ARM_CFG_RTQUERY);
        __sync_synchronize();
        RA_DBG("RTQuery: disabled (FPGA will stop polling query mailbox after next VBlank)");
}

uint32_t ra_rtquery_read(void *map, uint32_t address, uint32_t num_bytes)
{
        if (!map || num_bytes == 0 || num_bytes > 4) return 0;

        uint8_t *base = (uint8_t *)map;
        ra_query_ctrl_t *ctrl = (ra_query_ctrl_t *)(base + RA_QUERY_CTRL_OFFSET);
        ra_query_req_t  *req  = (ra_query_req_t *)(base + RA_QUERY_REQ_OFFSET);
        ra_query_resp_t *resp = (ra_query_resp_t *)(base + RA_QUERY_RESP_OFFSET);

        // Fill single query slot
        req[0].address   = address;
        req[0].num_bytes = (uint8_t)num_bytes;
        __sync_synchronize();

        // Increment sequence and write control
        s_rtquery_seq++;
        if (s_rtquery_seq == 0) s_rtquery_seq = 1;  // Never use 0

        ctrl->num_queries = 1;
        ctrl->request_seq = s_rtquery_seq;
        __sync_synchronize();

        // Busy-wait for FPGA response (typically 5-50µs)
        volatile ra_query_ctrl_t *vctrl = (volatile ra_query_ctrl_t *)ctrl;
        int timeout = 100000;  // ~100ms safety limit at ~1µs per iteration
        while (vctrl->response_seq != s_rtquery_seq && --timeout > 0) {
                // Spin
        }

        if (timeout <= 0) {
                // Timeout — FPGA didn't respond
                return 0;
        }

        // Read result
        volatile ra_query_resp_t *vresp = (volatile ra_query_resp_t *)resp;
        uint32_t value = vresp[0].value;

        // Mask to requested byte count
        if (num_bytes < 4) {
                value &= (1u << (num_bytes * 8)) - 1;
        }

        return value;
}

int ra_rtquery_supported(const void *map)
{
        // Check FPGA version in debug word at DDRAM_BASE + 0x10
        // Debug word 1: {ver(8), dispatch_cnt(8), first_dout(16), timeout_cnt(16), ok_cnt(16)}
        // ver is at byte offset 0x17 (top byte of the 64-bit word at offset 0x10)
        if (!map) return 0;
        const uint8_t *base = (const uint8_t *)map;
        // The debug word is at DDRAM offset 0x10 (word index 2)
        // In the 64-bit word: FPGA_VERSION is in bits [63:56] = byte[7] of the word
        // At byte offset 0x10 + 7 = 0x17
        uint8_t ver = base[0x17];
        return ver >= 0x02;
}

