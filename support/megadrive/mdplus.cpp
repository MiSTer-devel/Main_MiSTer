// MD+ HPS-side support for MiSTer MegaDrive core
//
// Polls FPGA via EXT_BUS for MD+ CDDA commands, parses CUE sheet,
// reads WAV files, and streams 16-bit stereo PCM into a 64KB ring
// buffer in DDRAM for the FPGA to play back.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>

#include "../../user_io.h"
#include "../../fpga_io.h"
#include "../../shmem.h"
#include "../../file_io.h"
#include "mdplus.h"

// Ring buffer in DDRAM (must match mdp_audio.sv DDRAM_BASE)
// DDRAM_BASE in FPGA = 0x6000000 = 0x30000000 >> 3
#define MDP_DDRAM_PHYS    0x30000000
#define MDP_DDRAM_SIZE    0x10000     // 64KB
#define MDP_DDRAM_MASK    0xFFFF

// EXT_BUS command IDs (must match hps_ext.sv)
#define CMD_MDP_STATUS    0x60
#define CMD_MDP_ACK       0x61
#define CMD_MDP_AUDIO     0x62

// Pending command flags (bit positions in cmd_byte from hps_ext)
#define FLAG_PLAY         0x01
#define FLAG_STOP         0x02
#define FLAG_RESUME       0x04
#define FLAG_VOLUME       0x08
#define FLAG_LOOP         0x10

#define MAX_TRACKS        99
#define STREAM_CHUNK      8192   // bytes per poll iteration

static uint32_t mdp_get_ms();

struct mdp_track
{
	char wav_path[1024];
	int  loop;
	uint32_t loop_sector;  // sector offset for loop point (588 samples per sector)
};

// MD+ runtime state — zeroed on each ROM load
static struct
{
	int active;
	int playing;
	int paused;
	int draining;
	uint32_t drain_until;
	int current_track;

	mdp_track tracks[MAX_TRACKS + 1]; // 1-indexed
	int num_tracks;
	char base_dir[1024];

	FILE *wav_fp;
	uint32_t wav_data_start;
	uint32_t wav_data_size;
	uint32_t wav_position;

	volatile uint8_t *ddram;
	uint16_t wr_ptr;
} mdp;

static uint8_t staging[STREAM_CHUNK];

// Strip leading/trailing whitespace and line endings
static void trim(char *s)
{
	char *p = s;
	while (*p == ' ' || *p == '\t') p++;
	if (p != s) memmove(s, p, strlen(p) + 1);
	int len = strlen(s);
	while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' || s[len-1] == ' '))
		s[--len] = 0;
}

// CUE sheet parser
// Reads FILE, TRACK, INDEX 01, REM LOOP, and REM NOLOOP directives.
static void parse_cue(const char *cue_path)
{
	FILE *f = fopen(cue_path, "r");
	if (!f) return;

	char line[1024];
	int cur_track = 0;
	char cur_file[1024] = {};

	while (fgets(line, sizeof(line), f))
	{
		trim(line);
		if (!line[0]) continue;

		if (!strncasecmp(line, "FILE ", 5))
		{
			char *q1 = strchr(line, '"');
			if (q1)
			{
				char *q2 = strchr(q1 + 1, '"');
				if (q2)
				{
					*q2 = 0;
					snprintf(cur_file, sizeof(cur_file), "%s/%s",
					         mdp.base_dir, q1 + 1);
				}
			}
		}
		else if (!strncasecmp(line, "TRACK ", 6))
		{
			int tn = atoi(line + 6);
			if (tn >= 1 && tn <= MAX_TRACKS)
			{
				cur_track = tn;
				if (cur_track > mdp.num_tracks)
					mdp.num_tracks = cur_track;
			}
		}
		else if (!strncasecmp(line, "REM NOLOOP", 10))
		{
			// Must check NOLOOP before LOOP since LOOP is a prefix
			if (cur_track >= 1 && cur_track <= MAX_TRACKS)
			{
				mdp.tracks[cur_track].loop = 0;
				mdp.tracks[cur_track].loop_sector = 0;
			}
		}
		else if (!strncasecmp(line, "REM LOOP", 8))
		{
			if (cur_track >= 1 && cur_track <= MAX_TRACKS)
			{
				mdp.tracks[cur_track].loop = 1;
				const char *p = line + 8;
				while (*p == ' ' || *p == '\t') p++;
				if (*p >= '0' && *p <= '9')
					mdp.tracks[cur_track].loop_sector = (uint32_t)atol(p);
				else
					mdp.tracks[cur_track].loop_sector = 0;
			}
		}
		else if (!strncasecmp(line, "INDEX 01", 8) ||
		         !strncasecmp(line, "INDEX 1 ", 8))
		{
			if (cur_track >= 1 && cur_track <= MAX_TRACKS && cur_file[0])
			{
				strncpy(mdp.tracks[cur_track].wav_path, cur_file,
				        sizeof(mdp.tracks[cur_track].wav_path) - 1);
				mdp.tracks[cur_track].loop = 1; // default, REM directives override
			}
		}
	}

	fclose(f);
}

// Open a WAV and locate the "data" chunk (handles non-standard headers)
static int open_wav(int track)
{
	if (track < 1 || track > mdp.num_tracks) return 0;
	if (!mdp.tracks[track].wav_path[0]) return 0;

	if (mdp.wav_fp)
	{
		fclose(mdp.wav_fp);
		mdp.wav_fp = NULL;
	}

	mdp.wav_fp = fopen(mdp.tracks[track].wav_path, "rb");
	if (!mdp.wav_fp) return 0;

	fseek(mdp.wav_fp, 0, SEEK_END);
	uint32_t file_size = ftell(mdp.wav_fp);
	fseek(mdp.wav_fp, 12, SEEK_SET); // past RIFF header
	uint32_t pos = 12;

	while (pos < file_size - 8)
	{
		char chunk_id[4];
		uint32_t chunk_size;

		if (fread(chunk_id, 1, 4, mdp.wav_fp) != 4) break;
		if (fread(&chunk_size, 1, 4, mdp.wav_fp) != 4) break;
		pos += 8;

		if (!memcmp(chunk_id, "data", 4))
		{
			mdp.wav_data_start = pos;
			mdp.wav_data_size = chunk_size;
			mdp.wav_position = 0;
			return 1;
		}

		fseek(mdp.wav_fp, chunk_size, SEEK_CUR);
		pos += chunk_size;
	}

	fclose(mdp.wav_fp);
	mdp.wav_fp = NULL;
	return 0;
}

static void close_wav()
{
	if (mdp.wav_fp)
	{
		fclose(mdp.wav_fp);
		mdp.wav_fp = NULL;
	}
}

// Ring buffer streaming
// Reads PCM from the WAV and writes into DDRAM. Handles CUE-based
// looping and end-of-track drain (500ms to let FPGA FIFO empty).
static void stream_audio(uint16_t rd_ptr)
{
	if (!mdp.wav_fp || !mdp.playing || mdp.paused) return;

	uint16_t space = (rd_ptr - mdp.wr_ptr - 1) & MDP_DDRAM_MASK;
	if (space < 1024) return;

	uint32_t to_write = (space < STREAM_CHUNK) ? space : STREAM_CHUNK;

	uint32_t remaining = mdp.wav_data_size - mdp.wav_position;
	if (remaining == 0)
	{
		if (mdp.tracks[mdp.current_track].loop)
		{
			// Seek to loop point (sector * 2352 bytes per sector)
			uint32_t loop_byte = mdp.tracks[mdp.current_track].loop_sector * 2352;
			if (loop_byte >= mdp.wav_data_size) loop_byte = 0;
			fseek(mdp.wav_fp, mdp.wav_data_start + loop_byte, SEEK_SET);
			mdp.wav_position = loop_byte;
			remaining = mdp.wav_data_size - loop_byte;
		}
		else
		{
			// Drain mode: let FPGA FIFO empty before stopping
			mdp.draining = 1;
			mdp.drain_until = mdp_get_ms() + 500;
			return;
		}
	}

	if (to_write > remaining) to_write = remaining;

	// Align to stereo sample boundary (4 bytes)
	to_write &= ~3u;
	if (to_write == 0) return;

	size_t got = fread(staging, 1, to_write, mdp.wav_fp);
	if (got == 0) return;
	got &= ~3u;

	// Copy to ring buffer, handling wrap-around
	uint16_t first = MDP_DDRAM_SIZE - mdp.wr_ptr;
	if (first > got) first = got;

	memcpy((void *)(mdp.ddram + mdp.wr_ptr), staging, first);
	if (got > first)
		memcpy((void *)mdp.ddram, staging + first, got - first);

	mdp.wr_ptr = (mdp.wr_ptr + (uint16_t)got) & MDP_DDRAM_MASK;
	mdp.wav_position += got;
}

// EXT_BUS communication (SPI via hps_ext.sv, commands 0x60-0x62)

static void ext_read_status(uint8_t *track, uint8_t *flags,
                            uint8_t *fade, uint8_t *volume)
{
	uint16_t w0 = spi_uio_cmd_cont(CMD_MDP_STATUS);
	uint16_t w1 = spi_w(0);
	DisableIO();

	*track  = (w0 >> 8) & 0xFF;
	*flags  = w0 & 0xFF;
	*fade   = (w1 >> 8) & 0xFF;
	*volume = w1 & 0xFF;
}

static void ext_send_ack(uint8_t track, int playing)
{
	spi_uio_cmd_cont(CMD_MDP_ACK);
	spi_w(((uint16_t)track << 8) | (playing ? 1 : 0));
	DisableIO();
}

static uint16_t ext_exchange_ptrs(uint16_t wr_ptr)
{
	uint16_t rd_ptr = spi_uio_cmd_cont(CMD_MDP_AUDIO);
	spi_w(wr_ptr);
	DisableIO();
	return rd_ptr;
}

static uint32_t mdp_get_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// Looks for a .cue file matching the ROM, parses it, and maps the DDRAM
// ring buffer. Does nothing if no CUE file is found (MD+ stays inactive).
void mdplus_init(const char *rom_path)
{
	close_wav();
	memset(&mdp, 0, sizeof(mdp));

	if (strcasecmp(user_io_get_core_name(1), "MegaDrive"))
		return;

	char full_rom_path[1024];
	if (rom_path[0] == '/')
		strncpy(full_rom_path, rom_path, sizeof(full_rom_path) - 1);
	else
		snprintf(full_rom_path, sizeof(full_rom_path), "%s/%s", getRootDir(), rom_path);

	strncpy(mdp.base_dir, full_rom_path, sizeof(mdp.base_dir) - 1);
	char *slash = strrchr(mdp.base_dir, '/');
	if (slash) *slash = 0;

	char cue_path[1024];
	strncpy(cue_path, full_rom_path, sizeof(cue_path) - 1);
	char *dot = strrchr(cue_path, '.');
	if (dot) strcpy(dot, ".cue");
	else strncat(cue_path, ".cue", sizeof(cue_path) - strlen(cue_path) - 1);

	// No CUE file = no MD+ audio
	FILE *test = fopen(cue_path, "r");
	if (!test) return;
	fclose(test);

	// Shared with FPGA mdp_audio module
	mdp.ddram = (volatile uint8_t *)shmem_map(MDP_DDRAM_PHYS, MDP_DDRAM_SIZE);
	if (!mdp.ddram) return;
	memset((void *)mdp.ddram, 0, MDP_DDRAM_SIZE);

	parse_cue(cue_path);

	if (mdp.num_tracks > 0)
		mdp.active = 1;
}

// Polls FPGA for MD+ commands, handles play/stop/resume/fade,
// and streams PCM data into the DDRAM ring buffer.
void mdplus_poll()
{
	if (!mdp.active) return;

	static uint32_t last_poll = 0;
	uint32_t now = mdp_get_ms();
	if (now - last_poll < 5) return;
	last_poll = now;

	uint8_t track_num, flags, fade_sec, volume;
	ext_read_status(&track_num, &flags, &fade_sec, &volume);

	if (!flags)
	{
		if (mdp.playing && !mdp.paused)
		{
			uint16_t rd_ptr = ext_exchange_ptrs(mdp.wr_ptr);
			if (mdp.draining)
			{
				if (mdp_get_ms() >= mdp.drain_until)
				{
					mdp.playing = 0;
					mdp.draining = 0;
				}
			}
			else
			{
				stream_audio(rd_ptr);
			}
		}
		return;
	}

	// Track 0 is invalid
	if ((flags & FLAG_PLAY) && track_num == 0)
	{
		ext_send_ack(mdp.current_track, mdp.playing);
		return;
	}

	if (flags & FLAG_PLAY)
	{
		close_wav();
		mdp.paused = 0;
		mdp.draining = 0;

		if (open_wav(track_num))
		{
			mdp.current_track = track_num;
			mdp.playing = 1;
			mdp.wr_ptr = 0;
			memset((void *)mdp.ddram, 0, MDP_DDRAM_SIZE);

			// Pre-fill half the buffer before ACK to prevent underrun
			uint32_t target = MDP_DDRAM_SIZE / 2;
			if (target > mdp.wav_data_size) target = mdp.wav_data_size;
			target &= ~3u;
			uint32_t filled = 0;
			while (filled < target)
			{
				uint32_t chunk = target - filled;
				if (chunk > STREAM_CHUNK) chunk = STREAM_CHUNK;
				size_t got = fread(staging, 1, chunk, mdp.wav_fp);
				if (got == 0) break;
				got &= ~3u;
				memcpy((void *)(mdp.ddram + filled), staging, got);
				filled += got;
			}
			mdp.wr_ptr = (uint16_t)filled & MDP_DDRAM_MASK;
			mdp.wav_position = filled;
		}
		else
		{
			mdp.playing = 0;
			mdp.current_track = 0;
		}

		// Play+stop together: play wins (stop was for previous track)
		ext_send_ack(mdp.current_track, mdp.playing);
	}
	else if (flags & FLAG_STOP)
	{
		// fade_sec == 0: immediate pause; > 0: fade handled by FPGA
		if (fade_sec == 0)
			mdp.paused = 1;

		ext_send_ack(mdp.current_track, mdp.playing);
	}
	else if (flags & FLAG_RESUME)
	{
		mdp.paused = 0;
		ext_send_ack(mdp.current_track, mdp.playing);
	}
	else
	{
		ext_send_ack(mdp.current_track, mdp.playing);
	}

	if (mdp.playing && !mdp.paused)
	{
		uint16_t rd_ptr = ext_exchange_ptrs(mdp.wr_ptr);
		if (mdp.draining)
		{
			if (mdp_get_ms() >= mdp.drain_until)
			{
				mdp.playing = 0;
				mdp.draining = 0;
			}
		}
		else
		{
			stream_audio(rd_ptr);
		}
	}
}
