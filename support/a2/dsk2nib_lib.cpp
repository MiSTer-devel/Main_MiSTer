// On-the-fly DSK/DO/PO <-> NIB for cores that consume 6656-byte NIB tracks
// (TK2000 floppy_track.sv: 13 x 512-byte SD blocks per track).
//
// The track codec lives in a2_nib_codec.cpp, shared verbatim with the
// TK2000 Verilator sim (TK2000_MiSTer/verilator/sim/) so the sim exercises
// exactly what Main feeds the core.
//
// Fixes vs. the previous implementation:
//  * physical sectors are laid out 0..15 in order around the track, like a
//    real disk / .nib image. The old code added a second interleave on top of
//    DOS's software skew, so DOS missed almost every next sector and loads
//    were several times slower than from the equivalent .nib.
//  * .po (ProDOS order) images are supported; .do as well as .dsk.
//  * write-back accumulates the full 13-block track and only writes sectors
//    whose address and data checksums verify (the old code re-parsed partial
//    buffers, wrote sectors decoded from zero-filled data, and shared one
//    track buffer between drives).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../hardware.h"
#include "../../spi.h"

#include "dsk2nib_lib.h"
#include "a2_nib_codec.h"

#define A2_SLOTS 16

static int      g_order[A2_SLOTS];
static int      g_wb_track[A2_SLOTS] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static uint32_t g_wb_mask[A2_SLOTS];
static uint8_t  g_wb_nib[A2_SLOTS][A2_NIB_TRACK_SIZE];

// read-side cache: the core fetches a track as 13 consecutive blocks
static int      g_rd_index = -1, g_rd_track = -1;
static uint8_t  g_rd_nib[A2_NIB_TRACK_SIZE];

void a2_set_image(int index, int prodos_order)
{
	if (index < 0 || index >= A2_SLOTS) return;
	g_order[index] = prodos_order ? A2_ORDER_PRODOS : A2_ORDER_DOS;
	g_wb_track[index] = -1;
	g_wb_mask[index] = 0;
	if (g_rd_index == index) g_rd_index = g_rd_track = -1;
}

static void read_dsk_track(fileTYPE *fd, int track, uint8_t *dsk)
{
	memset(dsk, 0, A2_DSK_TRACK_SIZE);
	if (FileSeek(fd, (off_t)track * A2_DSK_TRACK_SIZE, SEEK_SET))
		FileReadAdv(fd, dsk, A2_DSK_TRACK_SIZE);
}

static void build_nib_track(fileTYPE *fd, int index, int track, uint8_t *nib)
{
	static uint8_t dsk[A2_DSK_TRACK_SIZE];
	read_dsk_track(fd, track, dsk);
	a2_nibblize_track(dsk, track, A2_DEFAULT_VOLUME, g_order[index], nib);
}

static void a2_readDsk2Nib(fileTYPE *fd, int index, uint64_t offset, uchar *byte)
{
	int track = offset / A2_NIB_TRACK_SIZE;
	int toff = offset % A2_NIB_TRACK_SIZE;
	memset(byte, 0, 512);
	if (track >= A2_TRACKS) return;

	// a pending (partially received) write-back of this track wins
	const uint8_t *src;
	if (g_wb_track[index] == track && g_wb_mask[index]) src = g_wb_nib[index];
	else {
		if (g_rd_index != index || g_rd_track != track) {
			build_nib_track(fd, index, track, g_rd_nib);
			g_rd_index = index;
			g_rd_track = track;
		}
		src = g_rd_nib;
	}
	int n = A2_NIB_TRACK_SIZE - toff;
	if (n > 512) n = 512;
	memcpy(byte, src + toff, n);
}

static void a2_writeNib2Dsk(fileTYPE *fd, int index, uint64_t offset, const uchar *byte)
{
	int track = offset / A2_NIB_TRACK_SIZE;
	int blk = (offset % A2_NIB_TRACK_SIZE) / 512;
	if (track >= A2_TRACKS) return;

	if (g_wb_track[index] != track) {
		// seed with the current contents so a partial save still decodes
		build_nib_track(fd, index, track, g_wb_nib[index]);
		g_wb_track[index] = track;
		g_wb_mask[index] = 0;
	}
	int n = A2_NIB_TRACK_SIZE - blk * 512;
	if (n > 512) n = 512;
	memcpy(g_wb_nib[index] + blk * 512, byte, n);
	g_wb_mask[index] |= 1u << blk;

	if (g_wb_mask[index] == (1u << A2_NIB_BLOCKS) - 1) {
		static uint8_t dsk[A2_DSK_TRACK_SIZE];
		read_dsk_track(fd, track, dsk);
		uint32_t got = a2_denibblize_track(g_wb_nib[index], track, g_order[index], dsk);
		if (got) {
			if (FileSeek(fd, (off_t)track * A2_DSK_TRACK_SIZE, SEEK_SET))
				FileWriteAdv(fd, dsk, A2_DSK_TRACK_SIZE);
		}
		if (got != 0xFFFF)
			printf("a2: drive %d track %d: only sectors %04X decoded, others kept\n", index, track, got);
		g_wb_mask[index] = 0;
		if (g_rd_index == index && g_rd_track == track) g_rd_index = g_rd_track = -1;
	}
}

void a2_writeDSK(fileTYPE* idx, uint64_t lba, int ack, int index)
{
	uchar chunk[512];
	EnableIO();
	spi_w(UIO_SECTOR_WR | ack);
	spi_block_read(chunk, user_io_get_width(), 512);
	DisableIO();

	a2_writeNib2Dsk(idx, index, lba * 512, chunk);
}

void a2_readDSK(fileTYPE* idx, uint64_t lba, int ack, int index)
{
	uchar chunk[512];
	a2_readDsk2Nib(idx, index, lba * 512, chunk);

	EnableIO();
	spi_w(UIO_SECTOR_RD | ack);
	spi_block_write(chunk, user_io_get_width(), 512);
	DisableIO();
}
