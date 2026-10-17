#include <stdio.h>
#include <string.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../spi.h"
#include "../../hardware.h"
#include "../../menu.h"
#include "../arcade/mra_loader.h"
#include "pcfx.h"
#include "pcfx_cd.h"

#define BACKUP_SIZE (32 * 1024)
#define FXBMP_SIZE  (128 * 1024)

// The running game is its folder, or the disc itself when loose in games.
static char game[1024];
static char game_name[256];
static int disc_mounted;
static char notice[1100];

// Another game and card changes wait until slots 1 and 2 have been quiet for
// 100 ms with the board running, so pending saves land in the old files first.
// A save to the card runs through memory that stops while the board is held,
// so a game change must not hold the board before then.
static char          disc_next[1024];
static int           switch_pending;
static char          card_next[1024];  // "" = eject
static int           card_pending;
static unsigned long quiet_at;

static void save_path(char *out, size_t size, const char *name, const char *ext)
{
	snprintf(out, size, "%s/%s/%s%s", SAVE_DIR, CoreName2, name, ext);
}

// An empty internal backup as the BIOS formats it: boot record (jump bytes,
// "PCFXSram", the 32 KB BPB: 128-byte sectors, 64 root entries, 256 sectors,
// media F9, 3 FAT sectors) and a FAT with only its first entry used.
static void make_empty_backup(const char *path)
{
	static const uint8_t boot[] = {
		0x24, 0x8A, 0xDF, 'P', 'C', 'F', 'X', 'S', 'r', 'a', 'm',
		0x80, 0x00, 0x01, 0x01, 0x00, 0x01, 0x40, 0x00, 0x00, 0x01, 0xF9, 0x03, 0x00, 0x01, 0x00, 0x01, 0x00
	};
	static uint8_t buf[BACKUP_SIZE];
	memset(buf, 0, sizeof(buf));
	memcpy(buf, boot, sizeof(boot));
	memcpy(buf + 0x80, "\xF9\xFF\xFF", 3);
	FileSave(path, buf, sizeof(buf));
}

// Without a game: backup.sav as it is. A game's first backup starts empty.
static void mount_backup()
{
	char path[1024];
	save_path(path, sizeof(path), game_name[0] ? game_name : "backup", ".sav");
	if (game_name[0] && !FileExists(path)) make_empty_backup(path);
	user_io_set_index(PCFX_SLOT_BACKUP);
	user_io_file_mount(path, PCFX_SLOT_BACKUP, 1, BACKUP_SIZE);
	StoreIdx_S(PCFX_SLOT_BACKUP, path);
}

// The card is in exactly while a card file is mounted. The slot remembers it
// across core loads.
static void insert_card(const char *path)
{
	char cfg[64], saved[1024] = {};
	snprintf(cfg, sizeof(cfg), "%s.s%d", user_io_get_core_name(), PCFX_SLOT_FXBMP);
	snprintf(saved, sizeof(saved), "%s", path);
	FileSaveConfig(cfg, saved, sizeof(saved));
	StoreIdx_S(PCFX_SLOT_FXBMP, path);
	user_io_set_index(PCFX_SLOT_FXBMP);
	user_io_file_mount(path, PCFX_SLOT_FXBMP);
}

void pcfx_init()
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", SAVE_DIR, CoreName2);
	FileCreatePath(path);
	mount_backup();
}

void pcfx_mgl_premount()
{
	mgl_struct *mgl = mgl_get();
	int kept = 0;
	for (int i = 0; i < mgl->count; i++)
	{
		mgl_item_struct *it = &mgl->item[i];
		if (it->action == MGL_ACTION_LOAD && it->type == 'S' && it->index == PCFX_SLOT_CD)
		{
			char path[1024];
			if (it->path[0] == '/') snprintf(path, sizeof(path), "%s", it->path);
			else snprintf(path, sizeof(path), "%s/%s", HomeDir(), it->path);
			printf("CD: MGL CD mounted before reset: %s\n", path);
			pcfx_set_image(PCFX_SLOT_CD, path);
			StoreIdx_S(PCFX_SLOT_CD, path);
			continue;
		}
		if (kept != i) mgl->item[kept] = *it;
		kept++;
	}
	mgl->count = kept;
}

// Paths are made absolute first, as MGL and the OSD name the same disc differently.
// games/PCFX/Zenki/Disc 1.chd -> key /media/fat/games/PCFX/Zenki, name Zenki
// games/PCFX/Zenki.chd        -> key /media/fat/games/PCFX/Zenki, name Zenki
static void game_of(const char *path, char *key, char *name)
{
	const char *home = strrchr(HomeDir(), '/');
	home = home ? home + 1 : HomeDir();

	snprintf(key, sizeof(game), "%s", getFullPath(path));
	char *file = strrchr(key, '/');
	if (file)
	{
		*file = 0;
		const char *dir = strrchr(key, '/');
		dir = dir ? dir + 1 : key;
		if (strcasecmp(dir, home))
		{
			snprintf(name, sizeof(game_name), "%s", dir);
			return;
		}
		*file = '/';
	}

	char *ext = strrchr(key, '.');
	if (ext && (!file || ext > file)) *ext = 0;
	snprintf(name, sizeof(game_name), "%s", file ? file + 1 : key);
}

static void mount_disc(const char *filename)
{
	pcfx_cd_unload();
	int ok = filename[0] && pcfx_cd_load(filename);
	disc_mounted = ok;
	user_io_set_index(PCFX_SLOT_CD);
	user_io_file_mount(ok ? filename : "", PCFX_SLOT_CD);
	pcfx_cd_send_toc(PCFX_TOC_INDEX);
	if (ok) process_ss(filename);
}

// Reset covers the disc and backup mounts; the core holds the board until
// the backup is loaded. An explicit card pick can join the same hold.
static void switch_game(const char *filename)
{
	int held = user_io_status_get("[0]");
	if (!held) user_io_status_set("[0]", 1);
	mount_disc(filename);
	game_of(filename, game, game_name);
	mount_backup();
	if (card_pending)
	{
		insert_card(card_next);
		card_pending = 0;
	}
	if (!held) user_io_status_set("[0]", 0);
}

// The core gets the TOC after every CD mount or eject; an empty one means no disc.
// Another disc of the running game is a plain swap.
void pcfx_set_image(int index, const char *filename)
{
	int has = filename && filename[0];
	int held = user_io_status_get("[0]") && !switch_pending;
	if (index == PCFX_SLOT_FXBMP)
	{
		snprintf(card_next, sizeof(card_next), "%s", has ? filename : "");
		if (held) insert_card(card_next);
		else
		{
			card_pending = 1;
			quiet_at = GetTimer(100);
		}
		return;
	}
	if (index != PCFX_SLOT_CD)
	{
		user_io_set_index(index);
		user_io_file_mount(has ? filename : "", index);
		return;
	}

	char key[1024] = {}, name[256] = {};
	if (has) game_of(filename, key, name);
	if (!has || !strcmp(key, game)) mount_disc(has ? filename : "");
	else if (held) switch_game(filename);
	else
	{
		snprintf(disc_next, sizeof(disc_next), "%s", filename);
		switch_pending = 1;
		quiet_at = GetTimer(100);
	}
}

// OSD "Eject FX-BMP card": out once its last save has landed.
void pcfx_eject_card()
{
	pcfx_set_image(PCFX_SLOT_FXBMP, "");
}

// Blank cards are formatted by the game or BIOS.
void pcfx_new_card()
{
	const char *base = disc_mounted ? game_name : "PCFX";
	char path[1024];
	save_path(path, sizeof(path), base, ".bmp");
	for (int n = 2; FileExists(path); n++)
	{
		char name[300];
		snprintf(name, sizeof(name), "%s_%d", base, n);
		save_path(path, sizeof(path), name, ".bmp");
	}

	static uint8_t blank[FXBMP_SIZE];
	if (!FileSave(path, blank, sizeof(blank)))
	{
		snprintf(notice, sizeof(notice), "FX-BMP card not created");
		return;
	}

	pcfx_set_image(PCFX_SLOT_FXBMP, path);
	snprintf(notice, sizeof(notice), "New FX-BMP card mounted:\n%s", path);
}

// Messages wait for the OSD to close.
void pcfx_poll()
{
	if (notice[0] && !user_io_osd_is_visible())
	{
		Info(notice, 3000);
		notice[0] = 0;
	}

	if (!CheckTimer(quiet_at)) return;
	if (switch_pending && !user_io_osd_is_visible())
	{
		switch_pending = 0;
		switch_game(disc_next);
	}
	else if (card_pending && !switch_pending)
	{
		card_pending = 0;
		insert_card(card_next);
	}
}

// Only the CD slot is served here; the save slots are plain files.
int pcfx_sd_service(int disk, int op, uint32_t lba, int sz, int ack)
{
	if (!is_pcfx()) return 0;
	if (disk != PCFX_SLOT_CD) quiet_at = GetTimer(100);
	if (disk != PCFX_SLOT_CD) return 0;
	if (!(op & 1)) return -1;

	static uint8_t buf[UIO_BUFFER_SIZE];
	diskled_on();
	pcfx_cd_read(buf, lba, sz / PCFX_CD_BLOCK);

	EnableIO();
	spi_w(UIO_SECTOR_RD | ack);
	spi_block_write(buf, user_io_get_width(), sz);
	DisableIO();

	pcfx_cd_read_ahead(lba + sz / PCFX_CD_BLOCK);
	return 1;
}
