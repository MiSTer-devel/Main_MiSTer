// MiSTer integration glue for the Apple IIgs core. See iigs_disk.h.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../hardware.h"
#include "../../menu.h"

#include "iigs_fmt.h"
#include "iigs_disk.h"

// Per-slot serving state (only used for SD_TYPE_IIGS slots).
//   mode 0 = hard disk, raw blocks at hdr_off (2MG/DC42)
//   mode 1 = converted floppy, served from the in-memory woz buffer (read-only)
static int        g_mode[16]    = {};
static int64_t    g_hdr_off[16]  = {};
static uint8_t   *g_woz[16]      = {};
static size_t     g_woz_sz[16]   = {};
// Converted-floppy write-back descriptor (mode 1):
static int        g_wb_ok[16]    = {};  // write-back supported (and file writable)
static int        g_wb_kind[16]  = {};  // 1 = 3.5", 2 = 5.25"
static int64_t    g_wb_off[16]   = {};  // header offset within the source file
static int        g_wb_order[16] = {};  // 5.25 source order: 0 = DOS, 1 = ProDOS

// IIgs slot kinds: 0,1 = hard disk; 2 = 3.5"; 3 = 5.25". -1 = not an IIgs slot.
static int slot_kind(int index)
{
	if (index == 0 || index == 1) return 0;  // HDD
	if (index == 2) return 1;                 // 3.5"
	if (index == 3) return 2;                 // 5.25"
	return -1;
}

static int eqi(const char *a, const char *b) { return a && b && !strcasecmp(a, b); }

int iigs_is_core(void)
{
	const char *n = user_io_get_core_name();
	return n && !strcasecmp(n, "Apple-IIgs");
}

static void reject(const char *msg)
{
	printf("IIgs: rejecting mount: %s\n", msg);
	InfoMessage(msg, 5000, "Wrong disk for slot");
}

void iigs_unmount(int index)
{
	if (index < 0 || index >= 16) return;
	if (g_woz[index]) { free(g_woz[index]); g_woz[index] = NULL; }
	g_woz_sz[index] = 0;
	g_hdr_off[index] = 0;
	g_mode[index] = 0;
	g_wb_ok[index] = 0;
	g_wb_kind[index] = 0;
	g_wb_off[index] = 0;
	g_wb_order[index] = 0;
}

// Read the entire open image into a freshly malloc'd buffer (caller frees).
static uint8_t *read_all(fileTYPE *f, size_t *out_len)
{
	size_t n = (size_t)f->size;
	uint8_t *b = (uint8_t *)malloc(n ? n : 1);
	if (!b) return NULL;
	FileSeek(f, 0, SEEK_SET);
	size_t got = 0;
	while (got < n) {
		int r = FileReadAdv(f, b + got, n - got);
		if (r <= 0) break;
		got += (size_t)r;
	}
	FileSeek(f, 0, SEEK_SET);
	if (got != n) { free(b); return NULL; }
	*out_len = n;
	return b;
}

// Build a converted WOZ for a floppy slot. Returns malloc'd buffer + size, or
// NULL on failure. kind: 1 = 3.5", 2 = 5.25".
static uint8_t *build_woz(int kind, const char *ext, const uint8_t *raw, size_t raw_len, size_t *out_sz)
{
	// Resolve the payload (strip 2MG/DC42 header) and its order.
	const uint8_t *pay = raw;
	size_t pay_len = raw_len;
	int order_prodos = 0, order_nib = 0;

	TwoMG m;
	DC42 d;
	if (twomg_parse(raw, raw_len, &m)) {
		pay = raw + m.data_offset;
		pay_len = m.data_len;
		order_prodos = (m.format == 1);
		order_nib    = (m.format == 2);
	} else if (dc42_parse(raw, raw_len, &d)) {
		pay = raw + 84;
		pay_len = d.data_size;
		order_prodos = 1;
	} else if (raw_len == A2_NIB_IMAGE_SIZE) {
		order_nib = 1;
	} else if (eqi(ext, "po")) {
		order_prodos = 1;
	} // else .do/.dsk => DOS order

	if (kind == 1) {
		// 3.5": need an 800K ProDOS image
		if (pay_len != A2_35_IMAGE_SIZE || order_nib) return NULL;
		size_t cap = 2 * 1024 * 1024;
		uint8_t *woz = (uint8_t *)malloc(cap);
		if (!woz) return NULL;
		size_t n = a2_po_to_woz35(woz, cap, pay);
		if (!n) { free(woz); return NULL; }
		*out_sz = n;
		return woz;
	}

	// 5.25": produce a 140K DOS-order image, then encode to WOZ
	static uint8_t dsk[A2_525_IMAGE_SIZE];
	if (order_nib) {
		if (pay_len != A2_NIB_IMAGE_SIZE) return NULL;
		if (!a2_nib_to_dsk(dsk, pay)) return NULL;
	} else {
		if (pay_len != A2_525_IMAGE_SIZE) return NULL;
		if (order_prodos) a2_prodos_to_dos(dsk, pay);
		else              memcpy(dsk, pay, A2_525_IMAGE_SIZE);
	}
	size_t cap = 512 * 1024;
	uint8_t *woz = (uint8_t *)malloc(cap);
	if (!woz) return NULL;
	size_t n = a2_dsk_to_woz525(woz, cap, dsk);
	if (!n) { free(woz); return NULL; }
	*out_sz = n;
	return woz;
}

int iigs_mount(int index, const char *name, fileTYPE *f, int *out_writable)
{
	if (!iigs_is_core()) return IIGS_PASSTHRU;
	int kind = slot_kind(index);
	if (kind < 0) return IIGS_PASSTHRU;

	iigs_unmount(index);

	uint8_t head[128];
	memset(head, 0, sizeof(head));
	size_t hn = f->size < (int)sizeof(head) ? (size_t)f->size : sizeof(head);
	FileSeek(f, 0, SEEK_SET);
	FileReadAdv(f, head, hn);
	FileSeek(f, 0, SEEK_SET);

	const char *dot = strrchr(name, '.');
	const char *ext = dot ? dot + 1 : NULL;
	int wt     = woz_disk_type(head, f->size);
	int is_nib = (f->size == A2_NIB_IMAGE_SIZE);
	TwoMG m;
	int is_2mg = twomg_parse(head, f->size, &m);
	int is_dc42 = dc42_probe(head, f->size);

	if (kind == 0) {
		// ----- hard-disk slot: permissive (any ProDOS-order block image) -----
		if (wt > 0)   { reject("Hard-disk slot needs a ProDOS block image, not a WOZ floppy."); return IIGS_REJECT; }
		if (is_nib)   { reject("Hard-disk slot needs a ProDOS block image, not a .nib floppy."); return IIGS_REJECT; }
		if (is_2mg && m.format != 1) { reject("This 2MG is DOS/NIB order; not a ProDOS hard-disk image."); return IIGS_REJECT; }
		if (eqi(ext, "do")) { reject("DOS-order disk — load it in a 5.25\" drive, not a hard disk."); return IIGS_REJECT; }

		int64_t off = 0;
		if (is_2mg)       off = m.data_offset;
		else if (is_dc42) off = 84;
		if (off == 0) return IIGS_PASSTHRU;   // raw .po/.hdv: generic path serves it

		g_mode[index]    = 0;
		g_hdr_off[index] = off;
		if (f->size > off) f->size -= off;     // core sees the payload size
		*out_writable = FileCanWrite(name);
		printf("IIgs: HDD slot %d, header offset %lld, payload %lld bytes\n",
		       index, (long long)off, (long long)f->size);
		return IIGS_HANDLED;
	}

	// ----- floppy slot (kind 1 = 3.5", kind 2 = 5.25") -----
	if (wt > 0) {
		int want = (kind == 1) ? 2 : 1;
		if (wt != want) {
			reject(kind == 1 ? "That's a 5.25\" WOZ — use the 5.25\" drive."
			                 : "That's a 3.5\" WOZ — use the 3.5\" drive.");
			return IIGS_REJECT;
		}

		// A zip-backed file is a forward-only decompression stream: every
		// backward seek re-inflates from the start, which is far too slow for
		// the WOZ controller's random track access and breaks flux-timing
		// copy protection (e.g. Karateka). Load the WOZ verbatim into RAM and
		// serve from there — byte-for-byte identical, no conversion, so the
		// protection is preserved. Regular files keep fast random access via
		// the passthrough path below.
		if (f->zip) {
			size_t n = 0;
			uint8_t *buf = read_all(f, &n);
			if (!buf) { reject("Could not read the WOZ from the archive."); return IIGS_REJECT; }
			g_mode[index]   = 1;
			g_woz[index]    = buf;
			g_woz_sz[index] = n;
			g_wb_ok[index]  = 0;            // read-only: cannot write back into a zip
			f->size = (int64_t)n;
			*out_writable = 0;
			printf("IIgs: native WOZ from archive on slot %d -> RAM (%zu bytes), read-only\n", index, n);
			return IIGS_HANDLED;
		}

		return IIGS_PASSTHRU;   // native WOZ on a real file: serve directly
	}

	// Need to convert. Validate geometry up front for a clear message.
	DiskClass cls = iigs_classify(head, f->size, ext);
	int want_cls = (kind == 1) ? DC_FLOPPY_35 : DC_FLOPPY_525;
	if (cls != want_cls) {
		reject(kind == 1 ? "3.5\" drive needs an 800K disk image."
		                 : "5.25\" drive needs a 140K disk image.");
		return IIGS_REJECT;
	}

	size_t raw_len = 0;
	uint8_t *raw = read_all(f, &raw_len);
	if (!raw) { reject("Could not read the disk image."); return IIGS_REJECT; }

	// Determine the write-back descriptor before consuming `raw`.
	int wb_off = 0, wb_order = (kind == 1) ? 1 : 0, wb_ok = 0;
	TwoMG mm;
	if (twomg_parse(raw, raw_len, &mm)) {
		wb_off = (int)mm.data_offset;
		if (mm.format == 2)      wb_ok = 0;                       // NIB payload: read-only
		else { wb_ok = 1; if (kind == 2) wb_order = (mm.format == 1); }
	} else if (dc42_probe(raw, raw_len)) {
		wb_ok = 0;                                                // DC42: read-only (stale checksum)
	} else if (raw_len == A2_NIB_IMAGE_SIZE) {
		wb_ok = 0;                                                // .nib: read-only (v1)
	} else if (kind == 2 && eqi(ext, "po")) {
		wb_ok = 1; wb_order = 1;                                  // ProDOS-order 140K
	} else {
		wb_ok = 1; if (kind == 2) wb_order = 0;                   // raw .po(800K) / .do / .dsk
	}
	if (!FileCanWrite(name)) wb_ok = 0;

	size_t woz_sz = 0;
	uint8_t *woz = build_woz(kind, ext, raw, raw_len, &woz_sz);
	free(raw);
	if (!woz) { reject("Could not convert this disk to WOZ."); return IIGS_REJECT; }

	g_mode[index]    = 1;
	g_woz[index]     = woz;
	g_woz_sz[index]  = woz_sz;
	g_wb_ok[index]   = wb_ok;
	g_wb_kind[index] = kind;
	g_wb_off[index]  = wb_off;
	g_wb_order[index] = wb_order;
	f->size = (int64_t)woz_sz;            // core sees the WOZ size
	*out_writable = wb_ok;                // writable only if write-back is supported
	printf("IIgs: floppy slot %d converted to WOZ (%zu bytes), %s\n",
	       index, woz_sz, wb_ok ? "read-write (write-back)" : "read-only");
	return IIGS_HANDLED;
}

void iigs_read(int disk, fileTYPE *f, uint64_t lba, int ack)
{
	uint8_t chunk[512];
	if (g_mode[disk] == 1) {
		uint64_t off = lba * 512;
		if (g_woz[disk] && off + 512 <= g_woz_sz[disk]) memcpy(chunk, g_woz[disk] + off, 512);
		else memset(chunk, 0, 512);
	} else {
		if (!(FileSeek(f, (int64_t)(lba * 512 + g_hdr_off[disk]), SEEK_SET) &&
		      FileReadAdv(f, chunk, 512) > 0))
			memset(chunk, 0, 512);
	}
	EnableIO();
	spi_w(UIO_SECTOR_RD | ack);
	spi_block_write(chunk, user_io_get_width(), 512);
	DisableIO();
}

void iigs_write(int disk, fileTYPE *f, uint64_t lba, int ack)
{
	uint8_t chunk[512];
	EnableIO();
	spi_w(UIO_SECTOR_WR | ack);
	spi_block_read(chunk, user_io_get_width(), 512);
	DisableIO();

	if (g_mode[disk] == 0) {
		// Hard disk: write straight back to the file at the header offset.
		if (FileSeek(f, (int64_t)(lba * 512 + g_hdr_off[disk]), SEEK_SET))
			FileWriteAdv(f, chunk, 512);
		return;
	}

	// Converted floppy: update the in-memory WOZ, then (if write-back is
	// supported) decode the affected track and persist it to the source image.
	uint64_t off = lba * 512;
	if (g_woz[disk] && off + 512 <= g_woz_sz[disk]) memcpy(g_woz[disk] + off, chunk, 512);
	if (!g_wb_ok[disk]) return;

	int t = a2_woz_track_for_lba(g_woz[disk], g_woz_sz[disk], (uint32_t)lba);
	if (t < 0) return;   // header block (TMAP/TRKS dir) — nothing to persist

	if (g_wb_kind[disk] == 1) {
		// 3.5": decode the track's ProDOS blocks, write that block range back.
		static uint8_t po[A2_35_IMAGE_SIZE];
		int base = 0, cnt = 0;
		if (a2_woz35_decode_track(g_woz[disk], g_woz_sz[disk], t, po, &base, &cnt) > 0) {
			if (FileSeek(f, g_wb_off[disk] + (int64_t)base * 512, SEEK_SET))
				FileWriteAdv(f, po + (size_t)base * 512, (size_t)cnt * 512);
		}
	} else {
		// 5.25": decode the DOS-order track; re-skew to ProDOS if the source is .po.
		static uint8_t dsk[A2_525_IMAGE_SIZE];
		if (a2_woz525_decode_track(g_woz[disk], g_woz_sz[disk], t, dsk) > 0) {
			static const int D2P[16] = { 0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15 };
			uint8_t out[A2_TRACK_SIZE];
			const uint8_t *src = dsk + (size_t)t * A2_TRACK_SIZE;
			if (g_wb_order[disk] == 1)
				for (int s = 0; s < 16; s++) memcpy(out + D2P[s] * 256, src + s * 256, 256);
			else
				memcpy(out, src, A2_TRACK_SIZE);
			if (FileSeek(f, g_wb_off[disk] + (int64_t)t * A2_TRACK_SIZE, SEEK_SET))
				FileWriteAdv(f, out, A2_TRACK_SIZE);
		}
	}
}
