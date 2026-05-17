#ifndef RA_RAMREAD_H
#define RA_RAMREAD_H

#include <stdint.h>

// RetroAchievements RAM Mirror - Shared DDRAM Protocol
//
// This header defines the memory layout used by the FPGA core to expose
// emulated system RAM to the ARM/HPS side for RetroAchievements processing.
//
// The FPGA writes a snapshot of the emulated RAM to a known DDRAM address
// every VBlank. The ARM reads this snapshot to feed rcheevos.
//
// This protocol is console-agnostic. Each core writes the same header format,
// only differing in region count, sizes, and DDRAM base address.

// Magic number: "RACH" in little-endian
#define RA_MAGIC 0x52414348

// Per-core DDRAM base override (call before ra_ramread_map)
void ra_ramread_set_base(uint32_t phys_base);
uint32_t ra_ramread_get_base(void);

// DDRAM base address for RA RAM mirror (physical address accessible via /dev/mem)
// The FPGA writes to DDRAM at 0x3A00000, which maps to physical address:
//   0x30000000 (DDRAM base) + 0x3A00000 = 0x33A00000
// But from ARM side using fpga_mem(): 0x20000000 | 0x3A00000 = 0x23A00000
// Actually, the DDR3 on DE10-Nano is at physical 0x00000000-0x3FFFFFFF (1GB)
// and the FPGA DDRAM port maps to 0x30000000 + addr.
// ARM accesses DDR3 directly at the physical address.
//
// DDRAM address calculation:
// ARM physical = 0x30000000 + (FPGA_DWORD_addr * 4)
// FPGA_DWORD_addr for RA mirror = 0x3400000
// ARM physical = 0x30000000 + 0x0D000000 = 0x3D000000
// This sits below the savestate area (0x3E000000).
#define RA_DDRAM_PHYS_BASE  0x3D000000
#define RA_DDRAM_MAP_SIZE   0x00080000  // 512KB covers NES (10KB) and SNES (128KB WRAM + 256KB BSRAM)

// Header structure at offset 0x00 (written by FPGA)
typedef struct __attribute__((packed)) {
	uint32_t magic;          // 0x00: Must be RA_MAGIC (0x52414348)
	uint8_t  region_count;   // 0x04: Number of RAM regions
	uint8_t  flags;          // 0x05: Bit 0 = busy (transfer in progress)
	uint16_t core_version;   // 0x06: (major << 8) | minor — written by FPGA, 0 if unsupported
	uint32_t frame_counter;  // 0x08: Increments each VBlank
	uint32_t reserved2;      // 0x0C: SNES: bsram_size in bytes; NES: 0
} ra_header_t;

// Region descriptor at offset 0x10 + index * 8 (written by FPGA)
typedef struct __attribute__((packed)) {
	uint32_t sdram_addr;     // Source address in SDRAM (for reference)
	uint16_t size;           // Size in bytes
	uint16_t ddram_offset;   // Offset from DDRAM base where data starts
} ra_region_desc_t;

// Flag bits
#define RA_FLAG_BUSY 0x01

// ARM-written config flags at byte offset 0x40 from RA_DDRAM_PHYS_BASE.
// The ARM sets these bits; the FPGA reads them once per VBlank to gate features.
// The FPGA never writes this location, so ARM bits persist across VBlanks.
#define RA_ARM_CONFIG_OFFSET  0x40   // Byte offset from RA_DDRAM_PHYS_BASE
#define RA_ARM_CFG_RTQUERY    0x01   // Bit 0: FPGA should poll realtime query mailbox
#define RA_ARM_CFG_CLEAR_EN   0x02   // Bit 1: FPGA clears IWRAM on ROM load (set by RA)

// Maximum regions supported
#define RA_MAX_REGIONS 4

// NES-specific region indices
#define RA_NES_CPURAM_REGION  0  // $0000-$07FF (2KB)
#define RA_NES_CARTRAM_REGION 1  // $6000-$7FFF (up to 8KB+)

// Atari 2600-specific region indices
#define RA_ATARI2600_RIOT_REGION 0  // $0080-$00FF RIOT internal RAM (128 bytes)

// SNES-specific layout (fixed offsets in DDRAM mirror)
#define RA_SNES_WRAM_OFFSET   0x00100   // WRAM data starts at byte offset 0x100
#define RA_SNES_WRAM_SIZE     0x20000   // 128KB
#define RA_SNES_BSRAM_OFFSET  0x20100   // BSRAM data starts after WRAM

// Option C: Selective address reading (SNES)
#define RA_SNES_ADDRLIST_OFFSET  0x40000   // ARM → FPGA: address request table
#define RA_SNES_VALCACHE_OFFSET  0x48000   // FPGA → ARM: value response table
#define RA_SNES_MAX_ADDRS        4096      // Max tracked addresses

// ======================================================================
// Realtime Query Mailbox (Option C "on steroids")
// ARM writes query addresses, FPGA reads SDRAM, writes values back.
// Used for AddAddress pointer resolution during rc_client_do_frame().
// ======================================================================
#define RA_QUERY_CTRL_OFFSET     0x50000   // Control word (8 bytes)
#define RA_QUERY_REQ_OFFSET      0x50008   // Request slots (16 × 8 bytes)
#define RA_QUERY_RESP_OFFSET     0x50088   // Response slots (16 × 8 bytes)
#define RA_QUERY_MAX_SLOTS       16

// Control word layout (8 bytes at QUERY_CTRL_OFFSET):
//   [0] request_seq    (uint8_t)  ARM increments to signal new batch
//   [1] num_queries    (uint8_t)  Number of queries (1-16)
//   [2-3] reserved
//   [4] response_seq   (uint8_t)  FPGA writes = request_seq when done
//   [5-7] reserved
//
// Request slot layout (8 bytes each at QUERY_REQ_OFFSET + i*8):
//   [0-3] address      (uint32_t) Byte address in game RAM
//   [4]   num_bytes    (uint8_t)  Bytes to read (1-4)
//   [5-7] reserved
//
// Response slot layout (8 bytes each at QUERY_RESP_OFFSET + i*8):
//   [0-3] value        (uint32_t) Value read from game RAM (little-endian)
//   [4-7] reserved

typedef struct __attribute__((packed)) {
        uint8_t  request_seq;
        uint8_t  num_queries;
        uint16_t reserved1;
        uint8_t  response_seq;
        uint8_t  reserved2[3];
} ra_query_ctrl_t;

typedef struct __attribute__((packed)) {
        uint32_t address;
        uint8_t  num_bytes;
        uint8_t  reserved[3];
} ra_query_req_t;

typedef struct __attribute__((packed)) {
        uint32_t value;
        uint32_t reserved;
} ra_query_resp_t;

// Realtime query API — generic, works with any Option C core
void     ra_rtquery_init(void *map);
void     ra_rtquery_disable(void *map);  // clears RA_ARM_CFG_RTQUERY so FPGA stops polling
void     ra_clear_en_set(void *map);     // sets   RA_ARM_CFG_CLEAR_EN (FPGA clears IWRAM on game load)
void     ra_clear_en_clear(void *map);   // clears RA_ARM_CFG_CLEAR_EN
uint32_t ra_rtquery_read(void *map, uint32_t address, uint32_t num_bytes);

// Address request header at ADDRLIST_OFFSET (ARM writes)
typedef struct __attribute__((packed)) {
	uint32_t addr_count;    // Number of addresses
	uint32_t request_id;    // Incremented when list changes
} ra_addr_req_hdr_t;

// Value response header at VALCACHE_OFFSET (FPGA writes)
typedef struct __attribute__((packed)) {
	uint32_t response_id;   // Matches request_id when valid
	uint32_t response_frame; // Frame counter for this snapshot
} ra_val_resp_hdr_t;

// Helper: map the RA mirror DDRAM region and return pointer
// Returns NULL on failure. Caller must call ra_ramread_unmap() when done.
void *ra_ramread_map(void);
void  ra_ramread_unmap(void *map);

// Check if RA mirror is active (magic number matches)
int ra_ramread_active(const void *map);

// Read the core version written by the FPGA into the DDRAM header.
// Fills *major and *minor. Returns 0 (and sets both to 0) if map is null,
// magic doesn't match, or the FPGA didn't write a version.
int ra_ramread_get_core_version(const void *map, uint8_t *major, uint8_t *minor);

// Get current frame counter (returns 0 if not active)
uint32_t ra_ramread_frame(const void *map);

// Check if FPGA is currently writing (busy flag)
int ra_ramread_busy(const void *map);

// Get pointer to region data. Returns NULL if region index is invalid.
// The returned pointer is valid as long as the map is valid.
const uint8_t *ra_ramread_region_data(const void *map, int region_index);

// Get region size
uint16_t ra_ramread_region_size(const void *map, int region_index);

// Read a byte from the mirrored RAM using NES CPU address space mapping.
// Handles mirror resolution ($0000-$1FFF -> $0000-$07FF) and CARTRAM ($6000-$7FFF).
// Returns the byte value, or 0 if address is not in a mirrored region.
uint8_t ra_ramread_nes_byte(const void *map, uint16_t nes_addr);

// Read multiple bytes from mirrored RAM. Used as rcheevos read_memory callback.
// Returns number of bytes read.
uint32_t ra_ramread_nes_read(const void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes);

// Read SNES memory from mirrored RAM. rcheevos address map:
//   0x000000-0x01FFFF = WRAM (128KB)
//   0x020000+         = Save RAM (BSRAM)
uint32_t ra_ramread_snes_read(const void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes);

// Read Atari 2600 memory from mirrored RAM. rcheevos address map:
//   0x0080-0x00FF: RIOT internal RAM (128 bytes)
uint8_t  ra_ramread_atari2600_byte(const void *map, uint16_t addr);
uint32_t ra_ramread_atari2600_read(const void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes);

uint8_t  ra_ramread_atari7800_byte(const void *map, uint16_t addr);
uint32_t ra_ramread_atari7800_read(const void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes);

// Option C: Selective address reading (SNES)
// ARM collects needed addresses from rcheevos, writes list to DDRAM.
// FPGA reads only those addresses each VBlank. ARM reads cached values.
void     ra_snes_addrlist_init(void);
void     ra_snes_addrlist_begin_collect(void);
void     ra_snes_addrlist_add(uint32_t addr);
int      ra_snes_addrlist_end_collect(void *map);  // Returns 1 if list changed
uint8_t  ra_snes_addrlist_read_cached(const void *map, uint32_t addr);
int      ra_snes_addrlist_is_ready(const void *map);
int      ra_snes_addrlist_count(void);
const uint32_t *ra_snes_addrlist_addrs(void);  // Returns pointer to sorted address array
uint32_t ra_snes_addrlist_request_id(void);     // Current expected request ID
uint32_t ra_snes_addrlist_response_frame(const void *map);
void     ra_snes_addrlist_diag_dump(const void *map);

// Smart Cache: incremental address management (no periodic recollect)
// Used when a read_memory call finds an address not in the batch cache.
// The address is inserted into the sorted list and flushed to DDRAM
// after the frame, so the FPGA monitors it from the next VBlank onward.
int      ra_snes_addrlist_contains(uint32_t addr);       // Returns index if found, -1 on miss
// Combined lookup: returns the cached byte value, sets *hit to 1 if the address
// is in the list (0 otherwise). One binary search returns both, ~50% cheaper
// than calling contains() and read_cached() separately. *hit may be NULL.
uint8_t  ra_snes_addrlist_lookup_byte(const void *map, uint32_t addr, int *hit);
int      ra_snes_addrlist_add_dynamic(uint32_t addr);    // Insert in sorted position, returns 1 if added
int      ra_snes_addrlist_has_pending(void);              // 1 if new dynamic addresses need flush
int      ra_snes_addrlist_flush_dynamic(void *map);      // Write updated list to DDRAM, returns 1 if flushed
int      ra_snes_addrlist_dynamic_count(void);            // Number of addresses added dynamically this cycle

// Print a full diagnostic dump of the DDRAM mirror state to stdout and optional log file.
// Includes: header validation, frame counter, region descriptors, hex dump of first bytes.
void ra_ramread_debug_dump(const void *map);

// Print a one-line status summary (for periodic logging)
void ra_ramread_debug_status(const void *map);

// Realtime query: check if FPGA supports it (version >= 2)
int ra_rtquery_supported(const void *map);

#endif // RA_RAMREAD_H
