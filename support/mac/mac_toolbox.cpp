// BlueSCSI Toolbox — HPS-side handlers for the Mac SCSI family's two Toolbox
// slots: file sharing and the CD changer (see mac_toolbox.h).

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <string>
#include <vector>
#include <set>

#include "../../cfg.h"
#include "../../user_io.h"
#include "../../file_io.h"
#include "mac.h"

#define TB_SIG        0xB5
#define STATUS_GOOD   0x00
#define STATUS_CHECK  0x02
#define NAME_MAX_LEN  32     // Mac filename field in a LIST entry (bytes 2..34)
#define ENTRY_SIZE    40     // LIST entry size, bytes
#define TB_MAX_FILES  255    // file count is a single byte
#define CDC_MAX_CDS   100    // BlueSCSI MAX_FILE_LISTING_FILES cap

// ---- shared transport: reply staged by *_request, drained by *_fill ---------
struct tbx_channel
{
	uint8_t              status = STATUS_CHECK;
	std::vector<uint8_t> resp;
};
static tbx_channel tb_ch;    // file-sharing slot
static tbx_channel cdc_ch;   // CD-changer control slot

// ASCII passes through; non-ASCII -> fb (full MacRoman high table is a TODO).
static void tbx_name(const char *in, uint8_t *out, int *out_len, int cap, char fb)
{
	int n = 0;
	for (const unsigned char *p = (const unsigned char *)in; *p && n < cap; p++)
		out[n++] = (*p < 0x80) ? *p : fb;
	*out_len = n;
}

// 40-byte LIST entry: index, type, name (bytes 2..34), 40-bit BE size
// (byte 35 = bits 39..32, left zero; 36..39 = low 32).
static void tbx_list_entry(tbx_channel &ch, uint8_t index, uint8_t type,
                           const char *name, uint32_t size, char fb)
{
	uint8_t fe[ENTRY_SIZE] = {};
	fe[0] = index;
	fe[1] = type;
	int nl = 0;
	tbx_name(name, fe + 2, &nl, NAME_MAX_LEN, fb);
	fe[36] = (size >> 24) & 0xFF;
	fe[37] = (size >> 16) & 0xFF;
	fe[38] = (size >> 8)  & 0xFF;
	fe[39] =  size        & 0xFF;
	ch.resp.insert(ch.resp.end(), fe, fe + ENTRY_SIZE);
}

// Read side of the round-trip: status block (lba 0) or DataIn block (lba 1+k).
static void tbx_fill(const tbx_channel &ch, uint32_t lba, uint8_t *buf, int sz)
{
	memset(buf, 0, sz);
	if (lba == 0)
	{
		// 17-bit length: bytes 2..3 carry length[15:0], byte 4 bit 0 carries
		// length[16]. A CDB[6]=16 GET stage is exactly 65536 bytes, which a
		// BE16 field cannot express -- the old 0xFFFF clamp cost the last
		// byte of every 64 KB chunk (the client zero-fills what it never
		// receives). Byte 4 was reserved-zero. A core that predates the
		// 17-bit decode reads length 0 for a full 65536 response and serves
		// nothing -- deploy this only alongside or after a core that has it.
		size_t   full = (ch.resp.size() > 0x1FFFF) ? 0x1FFFF : ch.resp.size();
		uint16_t len  = (uint16_t)(full & 0xFFFF);
		buf[0] = ch.status;
		buf[1] = TB_SIG;
		buf[2] = (len >> 8) & 0xFF;
		buf[3] = len & 0xFF;
		buf[4] = (uint8_t)((full >> 16) & 1);
	}
	else
	{
		uint32_t off = (lba - 1) * 512;
		if (off < ch.resp.size())
		{
			size_t n = ch.resp.size() - off;
			if (n > (size_t)sz) n = (size_t)sz;
			memcpy(buf, ch.resp.data() + off, n);
		}
	}
}

static std::string dir_entry_path(const char *dir, const std::string &name)
{
	std::string p = dir;
	p += "/";
	p += name;
	return p;
}

// ============================== file sharing =================================

static FILE *tb_file = NULL;   // open file for a GET/SEND sequence
static int   tb_file_wr = 0;   // tb_file is an upload (buffered write) stream

// Close tb_file, surfacing buffered-write errors. Returns nonzero on failure.
static int tb_close(void)
{
	if (!tb_file) return 0;
	int bad = tb_file_wr && (fflush(tb_file) != 0 || ferror(tb_file));
	if (fclose(tb_file) != 0) bad = 1;
	tb_file = NULL;
	tb_file_wr = 0;
	return bad;
}

// Shared folder: `shared_folder=` in the .ini (absolute or SD-root relative)
// wins, else games/<core>/shared.
const char *toolbox_shared_dir()
{
	static char path[1024];
	if (cfg.shared_folder[0])
	{
		if (cfg.shared_folder[0] == '/') snprintf(path, sizeof(path), "%s", cfg.shared_folder);
		else                             snprintf(path, sizeof(path), "%s/%s", getRootDir(), cfg.shared_folder);
	}
	else
	{
		snprintf(path, sizeof(path), "%s/games/%s/shared", getRootDir(), user_io_get_core_name2());
	}
	return path;
}

bool toolbox_shared_ready()
{
	struct stat st;
	const char *d = toolbox_shared_dir();
	return d[0] && (stat(d, &st) == 0) && S_ISDIR(st.st_mode);
}

// stable enumeration: sorted, dotfiles filtered
static std::vector<std::string> sorted_entries()
{
	std::vector<std::string> v;
	if (!toolbox_shared_ready()) return v;
	DIR *d = opendir(toolbox_shared_dir());
	if (!d) return v;
	struct dirent *e;
	while ((e = readdir(d)) && (int)v.size() < TB_MAX_FILES)
	{
		if (e->d_name[0] == '.') continue;          // skip hidden + . / ..
		v.push_back(e->d_name);
	}
	closedir(d);
	std::sort(v.begin(), v.end());
	return v;
}

// ---- 0xD2 COUNT FILES ------------------------------------------------------
static void tb_count()
{
	if (!toolbox_shared_ready()) { tb_ch.status = STATUS_CHECK; return; }
	tb_ch.resp.push_back((uint8_t)sorted_entries().size());
	tb_ch.status = STATUS_GOOD;
}

// ---- 0xD0 LIST FILES -------------------------------------------------------
static void tb_list()
{
	if (!toolbox_shared_ready()) { tb_ch.status = STATUS_CHECK; return; }
	std::vector<std::string> ents = sorted_entries();
	for (size_t i = 0; i < ents.size(); i++)
	{
		struct stat st;
		bool is_dir = false;
		uint32_t size = 0;
		if (stat(dir_entry_path(toolbox_shared_dir(), ents[i]).c_str(), &st) == 0)
		{
			is_dir = S_ISDIR(st.st_mode);
			size = (uint32_t)st.st_size;
		}
		tbx_list_entry(tb_ch, (uint8_t)i, is_dir ? 0x00 : 0x01, ents[i].c_str(), size, '?');
	}
	tb_ch.status = STATUS_GOOD;
}

// ---- 0xD1 GET FILE (host -> Mac) -------------------------------------------
// CDB[1]=index, CDB[2..5]=offset BE (units of 4096), CDB[6]=#4K blocks (0 => 1).
#define TB_GET_MAX 65536   // one GET response (official client asks CDB[6]=16)

static void tb_get(const uint8_t *cdb)
{
	const uint32_t BLOCK = 4096;
	uint8_t  index  = cdb[1];
	uint32_t offset = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
	uint32_t blocks = cdb[6] ? cdb[6] : 1;
	uint32_t want   = blocks * BLOCK;

	if (offset == 0)
	{
		if (tb_close()) printf("Toolbox: dropped upload lost data on close\n");
		std::vector<std::string> ents = sorted_entries();
		if (index < ents.size())
			tb_file = fopen(dir_entry_path(toolbox_shared_dir(), ents[index]).c_str(), "rb");
	}

	if (!tb_file) { tb_ch.status = STATUS_CHECK; return; }

	// Stage cap only — the core streams the serve, but a bad CDB could
	// otherwise ask for 255*4096.
	if (want > TB_GET_MAX) want = TB_GET_MAX;

	if (fseeko(tb_file, (off_t)offset * BLOCK, SEEK_SET) != 0) { tb_ch.status = STATUS_CHECK; return; }

	tb_ch.resp.resize(want);
	size_t got = fread(tb_ch.resp.data(), 1, want, tb_file);
	tb_ch.resp.resize(got);                 // short read (incl. 0) signals EOF
	if (got == 0) tb_close();
	tb_ch.status = STATUS_GOOD;
}

// ---- 0xD3/D4/D5 SEND FILE (upload, Mac -> host) ----------------------------
// CDB at buf[0..9], DataOut payload at buf[16..]. Payload beyond the first 496
// bytes arrives as request blocks at LBA 1..TB_TAIL_BLKS, written BEFORE the
// CDB block; tb_tail is their flat view, so payload byte P >= 496 sits at
// tb_tail[P - 496]. A dropped tail leaves 16-byte holes every 512 bytes.
// tb_file is shared with GET, per the spec.
#define TB_PAYLOAD_OFF 16
#define TB_PAYLOAD_MAX (512 - TB_PAYLOAD_OFF)   // 496 carried under the CDB
#define TB_CHUNK_MAX   65536                    // one SEND DATA chunk (client max CDB[6]=127)
#define TB_TAIL_BLKS   ((TB_CHUNK_MAX + TB_PAYLOAD_OFF - 1) / 512)   // 128

static uint8_t tb_tail[TB_TAIL_BLKS * 512];   // LBA 1..128 request blocks, flattened

// Reject names that could escape the shared folder.
static bool tb_name_safe(const char *n)
{
	if (!n[0])           return false;
	if (strchr(n, '/'))  return false;
	if (strstr(n, "..")) return false;
	return true;
}

// 0xD3 SEND FILE PREP: payload (buf[16..]) = MacRoman filename, NUL-terminated
// within 33 bytes. Create/truncate it in the shared folder and keep it open.
static void tb_send_prep(const uint8_t *buf)
{
	if (!toolbox_shared_ready()) { tb_ch.status = STATUS_CHECK; return; }
	if (tb_close()) printf("Toolbox: dropped upload lost data on close\n");   // implicit close

	char name[33];
	int n = 0;
	for (int i = 0; i < 32; i++)
	{
		uint8_t c = buf[TB_PAYLOAD_OFF + i];
		if (c == 0) break;
		name[n++] = (c < 0x80) ? (char)c : '?';
	}
	name[n] = 0;

	if (!tb_name_safe(name)) { tb_ch.status = STATUS_CHECK; return; }
	std::string path = std::string(toolbox_shared_dir()) + "/" + name;
	tb_file = fopen(path.c_str(), "wb");
	if (tb_file)
	{
		// /media/fat is mounted sync: batch the 512-byte chunks or every one
		// costs a full synchronous card transaction (erase-cycle stalls incl.)
		static char tb_wbuf[64 * 1024];
		setvbuf(tb_file, tb_wbuf, _IOFBF, sizeof(tb_wbuf));
		tb_file_wr = 1;
	}
	tb_ch.status = tb_file ? STATUS_GOOD : STATUS_CHECK;
}

// 0xD4 SEND FILE DATA: write the payload chunk at the CDB offset (512-B units).
// CDB[6] (512-blocks) or CDB[1..2] (legacy) says how many of the 512 transferred
// bytes are VALID — the last chunk of a file is a full block with a short count.
static void tb_send_data(const uint8_t *buf)
{
	if (!tb_file) { tb_ch.status = STATUS_CHECK; return; }
	uint32_t off   = ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | buf[5];  // 512-block offset
	uint32_t bytes = buf[6] ? (uint32_t)buf[6] * 512
	                        : (((uint32_t)buf[1] << 8) | buf[2]);                  // legacy byte count
	if (bytes > TB_CHUNK_MAX) bytes = TB_CHUNK_MAX;

	static uint8_t chunk[TB_CHUNK_MAX];   // off the coroutine stack
	uint32_t head = (bytes < TB_PAYLOAD_MAX) ? bytes : TB_PAYLOAD_MAX;
	memcpy(chunk, buf + TB_PAYLOAD_OFF, head);
	if (bytes > TB_PAYLOAD_MAX) memcpy(chunk + TB_PAYLOAD_MAX, tb_tail, bytes - TB_PAYLOAD_MAX);

	// seek only when needed: ftello doesn't flush, so sequential chunks stay
	// buffered; an out-of-order offset seeks (= flushes), same as before
	off_t want = (off_t)off * 512;
	if (ftello(tb_file) != want && fseeko(tb_file, want, SEEK_SET) != 0)
	{
		tb_ch.status = STATUS_CHECK;
		return;
	}
	size_t wrote = fwrite(chunk, 1, bytes, tb_file);
	tb_ch.status = (wrote == bytes && !ferror(tb_file)) ? STATUS_GOOD : STATUS_CHECK;
}

// 0xD5 SEND FILE END: flush + close the upload (no data phase). Errors from
// buffered writes surface here — a silently short file must not report GOOD.
static void tb_send_end(void)
{
	if (!tb_file) { tb_ch.status = STATUS_CHECK; return; }
	tb_ch.status = tb_close() ? STATUS_CHECK : STATUS_GOOD;
}

void toolbox_request(uint32_t lba, const uint8_t *buf, int sz)
{
	(void)sz;
	// SEND DATA tail blocks arrive before the CDB block; flatten into tb_tail.
	if (lba >= 1 && lba <= TB_TAIL_BLKS)
	{
		memcpy(tb_tail + (lba - 1) * 512, buf, (sz < 512) ? sz : 512);
		return;
	}
	if (lba != 0) return;            // CDB + the first 496 payload bytes ride lba 0

	tb_ch.resp.clear();
	tb_ch.status = STATUS_CHECK;

	switch (buf[0])                  // CDB opcode
	{
		case 0xD2: tb_count();      break;   // COUNT FILES
		case 0xD0: tb_list();       break;   // LIST FILES
		case 0xD1: tb_get(buf);     break;   // GET FILE   (download)
		case 0xD3: tb_send_prep(buf); break; // SEND PREP  (upload: filename)
		case 0xD4: tb_send_data(buf); break; // SEND DATA  (upload: chunk)
		case 0xD5: tb_send_end();   break;   // SEND END   (upload: close)
		default:   tb_ch.status = STATUS_CHECK; break;
	}
}

void toolbox_fill(uint32_t lba, uint8_t *buf, int sz)
{
	tbx_fill(tb_ch, lba, buf, sz);
}

// ============================== CD changer ===================================

static bool cdc_pending = false;   // deferred SET NEXT CD remount
static char cdc_pending_path[1024];

// games/<core>/CD3 under the SD root (BlueSCSI "CD<id>" convention, CD id 3)
const char *cdchanger_dir()
{
	static char path[1024];
	snprintf(path, sizeof(path), "%s/games/%s/CD3", getRootDir(), user_io_get_core_name2());
	return path;
}

bool cdchanger_ready()
{
	struct stat st;
	const char *d = cdchanger_dir();
	return d[0] && (stat(d, &st) == 0) && S_ISDIR(st.st_mode);
}

static std::string lower(std::string s)
{
	for (char &c : s) c = (char)tolower((unsigned char)c);
	return s;
}

static std::string base_name(const std::string &s)
{
	size_t p = s.find_last_of("/\\");
	return (p == std::string::npos) ? s : s.substr(p + 1);
}

static bool has_ext(const std::string &n, const char *ext)
{
	size_t el = strlen(ext), nl = n.size();
	return nl > el && !strcasecmp(n.c_str() + nl - el, ext);
}

// A directly-listable CD image (self-contained mountable unit). .bin is handled
// separately (collapsed into its .cue, or listed alone as raw 2352).
static bool is_cd_direct(const std::string &n)
{
	return has_ext(n, ".iso") || has_ext(n, ".toast") || has_ext(n, ".toa") ||
	       has_ext(n, ".cue") || has_ext(n, ".chd");
}

// LIST and SET NEXT CD both call this so index N lists AND mounts the same disc.
static std::vector<std::string> sorted_cds()
{
	std::vector<std::string> all, result;
	std::set<std::string>    referenced;   // lowercased basenames of .bin owned by a .cue

	if (!cdchanger_ready()) return result;
	DIR *d = opendir(cdchanger_dir());
	if (!d) return result;
	struct dirent *e;
	while ((e = readdir(d)))
	{
		if (e->d_name[0] == '.') continue;   // hidden + . / ..
		all.push_back(e->d_name);
	}
	closedir(d);

	// Pass 1: a .bin referenced by a .cue's FILE line collapses into that .cue.
	for (const std::string &n : all)
	{
		if (!has_ext(n, ".cue")) continue;
		FILE *f = fopen(dir_entry_path(cdchanger_dir(), n).c_str(), "r");
		if (!f) continue;
		char line[600];
		while (fgets(line, sizeof(line), f))
		{
			const char *p = line;
			while (*p == ' ' || *p == '\t') p++;
			if (strncasecmp(p, "FILE", 4)) continue;
			const char *q1 = strchr(p, '"');
			if (!q1) continue;
			const char *q2 = strchr(q1 + 1, '"');
			if (!q2) continue;
			referenced.insert(lower(base_name(std::string(q1 + 1, q2))));
		}
		fclose(f);
	}

	// Pass 2: direct images + lone .bin (not owned by any cue -> raw 2352).
	for (const std::string &n : all)
	{
		if (is_cd_direct(n))                                result.push_back(n);
		else if (has_ext(n, ".bin") && !referenced.count(lower(n))) result.push_back(n);
	}

	std::sort(result.begin(), result.end());                 // stable index
	if (result.size() > CDC_MAX_CDS) result.resize(CDC_MAX_CDS);
	return result;
}

// ---- 0xDA COUNT CDS --------------------------------------------------------
static void cdc_count()
{
	cdc_ch.resp.push_back((uint8_t)sorted_cds().size());
	cdc_ch.status = STATUS_GOOD;
}

// ---- 0xD7 LIST CDS ---------------------------------------------------------
static void cdc_list()
{
	std::vector<std::string> cds = sorted_cds();
	for (size_t i = 0; i < cds.size(); i++)
	{
		struct stat st;
		uint32_t size = 0;
		if (stat(dir_entry_path(cdchanger_dir(), cds[i]).c_str(), &st) == 0) size = (uint32_t)st.st_size;
		tbx_list_entry(cdc_ch, (uint8_t)i, 0x01, cds[i].c_str(), size, '_');
	}
	cdc_ch.status = STATUS_GOOD;
}

// ---- 0xD8 SET NEXT CD ------------------------------------------------------
// CDB[1] = index from the last LIST CDS. Stage the remount for cdchanger_poll();
// status is GOOD on staging.
static void cdc_set_next(const uint8_t *cdb)
{
	std::vector<std::string> cds = sorted_cds();
	uint8_t index = cdb[1];
	if (index >= cds.size()) { cdc_ch.status = STATUS_CHECK; return; }
	snprintf(cdc_pending_path, sizeof(cdc_pending_path), "%s/%s",
	         cdchanger_dir(), cds[index].c_str());
	cdc_pending = true;
	cdc_ch.status = STATUS_GOOD;
}

void cdchanger_request(uint32_t lba, const uint8_t *buf, int sz)
{
	(void)sz;
	if (lba != 0) return;            // single request block; CDB rides lba 0

	cdc_ch.resp.clear();
	cdc_ch.status = STATUS_CHECK;

	switch (buf[0])                  // CDB opcode
	{
		case 0xDA: cdc_count();       break;   // COUNT CDS
		case 0xD7: cdc_list();        break;   // LIST CDS
		case 0xD8: cdc_set_next(buf); break;   // SET NEXT CD
		default:   cdc_ch.status = STATUS_CHECK; break;
	}
}

void cdchanger_fill(uint32_t lba, uint8_t *buf, int sz)
{
	tbx_fill(cdc_ch, lba, buf, sz);
}

// Plain remount: the client ejects via the guest driver first, so the driver's
// insertion poll auto-mounts the swapped image on its own.
void cdchanger_poll()
{
	if (!cdc_pending) return;
	cdc_pending = false;
	int slot = mac_cdrom_slot();
	if (slot < 0) return;
	printf("CD Changer: SET NEXT CD -> mount \"%s\" on slot %d\n", cdc_pending_path, slot);
	user_io_file_mount(cdc_pending_path, (unsigned char)slot, 0, 0);
}
