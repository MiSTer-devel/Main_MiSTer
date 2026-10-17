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

// The running game: its folder, or the disc itself when it sits loose in the
// games folder. Its backup is saves/PCFX/<game_name>.sav; its own FX-BMP card,
// if it has one, sits beside the disc as <game_name>.bmp.
static char game[1024];
static char game_name[256];
static char game_card[1024];
static char disc_title[256];   // the disc's name without extension or "(Disc N)"
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

// Copies an older save file to a new path. Old files stay.
static int seed(const char *path, const char *from, int size)
{
	if (FileExists(path) || !FileExists(from) || FileLoad(from, 0, 0) != size) return 0;

	static uint8_t buf[FXBMP_SIZE];
	return FileLoad(from, buf, size) == size && FileSave(path, buf, size);
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

// Old card names used the disc name without the extension or a "(Disc 2)" style suffix.
static void title_of(const char *path, char *out, size_t size)
{
	const char *name = strrchr(path, '/');
	snprintf(out, size, "%s", name ? name + 1 : path);
	char *cut = strcasestr(out, " (Disc");
	if (!cut) cut = strrchr(out, '.');
	if (cut) *cut = 0;
}

// Paths are made absolute first, as MGL and the OSD name the same disc differently.
// games/PCFX/Zenki/Disc 1.chd -> key /media/fat/games/PCFX/Zenki, name Zenki, card <key>/Zenki.bmp
// games/PCFX/Zenki.chd         -> key /media/fat/games/PCFX/Zenki, name Zenki, card <key>.bmp
static void game_of(const char *path, char *key, char *name, char *card)
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
			snprintf(card, sizeof(game_card), "%s/%s.bmp", key, name);
			return;
		}
		*file = '/';
	}

	char *ext = strrchr(key, '.');
	if (ext && (!file || ext > file)) *ext = 0;
	snprintf(name, sizeof(game_name), "%s", file ? file + 1 : key);
	snprintf(card, sizeof(game_card), "%s.bmp", key);
}

static void mount_disc(const char *filename)
{
	pcfx_cd_unload();
	int ok = filename[0] && pcfx_cd_load(filename);
	user_io_set_index(PCFX_SLOT_CD);
	user_io_file_mount(ok ? filename : "", PCFX_SLOT_CD);
	pcfx_cd_send_toc(PCFX_TOC_INDEX);
	if (ok) process_ss(filename);
}

// Reset is held while the disc, backup and the game's own card (if it has one)
// go in, as if they were in at power-on; the core keeps the board held until
// the backup is loaded. Without its own card the card in the slot stays.
static void switch_game(const char *filename)
{
	int held = user_io_status_get("[0]");
	if (!held) user_io_status_set("[0]", 1);
	mount_disc(filename);
	game_of(filename, game, game_name, game_card);
	title_of(filename, disc_title, sizeof(disc_title));
	mount_backup();
	if (FileExists(game_card) && strcmp(game_card, getFullPath(GetIdx_S(PCFX_SLOT_FXBMP)))) insert_card(game_card);
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

	char key[1024] = {}, name[256] = {}, card[1024] = {};
	if (has) game_of(filename, key, name, card);
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

// OSD "Create new FX-BMP card": the game's own card beside its disc, started
// from an older card of the game if there is one, else unformatted.
void pcfx_new_card()
{
	if (!game[0])
	{
		snprintf(notice, sizeof(notice), "Load a disc first:\nthe FX-BMP card goes in its folder");
		return;
	}
	if (FileExists(game_card))
	{
		snprintf(notice, sizeof(notice), "FX-BMP card already there:\n%s", game_card);
		pcfx_set_image(PCFX_SLOT_FXBMP, game_card);
		return;
	}

	char from[4][1024], old[300];
	save_path(from[0], sizeof(from[0]), game_name, ".bmp");
	snprintf(old, sizeof(old), "fxbmp_%s", game_name);
	save_path(from[1], sizeof(from[1]), old, ".bmp");
	snprintf(old, sizeof(old), "fxbmp_%s", disc_title);
	save_path(from[2], sizeof(from[2]), old, ".sav");
	save_path(from[3], sizeof(from[3]), "fxbmp", ".sav");

	int ok = 0;
	for (int i = 0; i < 4 && !ok; i++) ok = seed(game_card, from[i], FXBMP_SIZE);
	if (!ok && !FileExists(game_card))
	{
		static uint8_t blank[FXBMP_SIZE];
		ok = FileSave(game_card, blank, sizeof(blank));
	}
	if (!ok)
	{
		snprintf(notice, sizeof(notice), "FX-BMP card not created");
		return;
	}

	pcfx_set_image(PCFX_SLOT_FXBMP, game_card);
	snprintf(notice, sizeof(notice), "New FX-BMP card mounted:\n%s", game_card);
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
