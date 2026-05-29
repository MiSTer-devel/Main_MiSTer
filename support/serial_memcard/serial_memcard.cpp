#include "serial_memcard.h"

#include "../../cfg.h"
#include "../../file_io.h"
#include "../../menu.h"
#include "../../user_io.h"
#include "miniz.h"

#include <algorithm>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <string>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr size_t kWriteChunkBytes = 16;
constexpr size_t kWriteRangeChunkBytes = 48;
constexpr useconds_t kDirtyFlushDebounceUs = 750000;
constexpr useconds_t kHotplugRescanDebounceUs = 1500000;
constexpr useconds_t kGbPakScanRetryDelayUs = 250000;
constexpr uint32_t kGbPakBlockSize = 32;

struct SerialSpeed {
	speed_t speed;
	uint32_t baud;
};

const SerialSpeed kSerialSpeeds[] = {
	{ B115200, 115200 },
};

enum class CardKind : uint8_t {
	None,
	PSX,
	N64,
	GBPak,
};

struct CardSlot {
	CardKind kind = CardKind::None;
	uint8_t port = 0;
	uint8_t slot = 0;
	uint16_t blocks = 0;
	uint16_t block_size = 0;
};

struct GbPakIdentity {
	bool valid = false;
	bool cart_present = false;
	bool supported = false;
	uint8_t port = 0;
	uint8_t cgb_flag = 0;
	uint8_t header_checksum = 0;
	uint16_t global_checksum = 0;
	uint32_t rom_size = 0;
	uint32_t save_size = 0;
	std::string title;
	std::string title_key;
};

struct PendingMount {
	bool active = false;
	std::string path;
	std::string label;
	CardSlot slot;
	std::vector<uint8_t> image;
	uint64_t read_elapsed_ms = 0;
};

struct MountedCard {
	bool active = false;
	std::string path;
	CardSlot slot;
	std::vector<uint8_t> image;
	std::vector<uint8_t> dirty;
};

struct AsyncRequest {
	CardKind kind = CardKind::None;
	bool auto_select = false;
	bool by_ordinal = false;
	uint8_t port = 0;
	uint8_t slot = 0;
	uint8_t ordinal = 0;
	unsigned char index = 0;
	unsigned char index2 = 0;
	uint32_t token = 0;
};

struct AsyncResult {
	bool success = false;
	bool done = false;
	unsigned char index = 0;
	uint32_t token = 0;
	std::string path;
};

struct MountInfoMessage {
	std::string popup;
	std::string summary;
};

struct UsbId {
	uint16_t vid;
	uint16_t pid;
};

constexpr UsbId kSerialMemcardUsbIds[] = {
	{ 0x16D0, 0x1460 },
};

int serial_rx_fd = -1;
std::string serial_path;
uint32_t serial_baud = 0;
pthread_t serial_reader_thread = {};
bool serial_reader_started = false;
bool serial_reader_active = false;
pthread_mutex_t serial_command_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t serial_reader_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t serial_reader_cond = PTHREAD_COND_INITIALIZER;
std::vector<std::string> serial_reader_lines;
PendingMount pending_mounts[16];
MountedCard mounted_cards[16];
MountedCard reusable_cards[16];
pthread_mutex_t serial_mount_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t serial_flush_mutex = PTHREAD_MUTEX_INITIALIZER;
bool serial_flush_running = false;
bool serial_flush_requested = false;
pthread_mutex_t serial_rescan_mutex = PTHREAD_MUTEX_INITIALIZER;
bool serial_rescan_running = false;
uint32_t serial_rescan_generation = 0;
pthread_mutex_t serial_async_mutex = PTHREAD_MUTEX_INITIALIZER;
std::vector<AsyncRequest> serial_async_requests;
std::vector<AsyncResult> serial_async_results;
bool serial_async_running = false;
uint32_t serial_async_next_token = 1;
pthread_mutex_t serial_slot_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
std::vector<CardSlot> cached_scan_slots;
bool cached_scan_slots_valid = false;
uint8_t cached_psx_slot_mask = 0;
bool cached_psx_slots_scanned = false;
pthread_mutex_t serial_device_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
std::vector<std::string> cached_serial_candidates;
bool cached_serial_candidates_valid = false;
pthread_mutex_t serial_mount_info_mutex = PTHREAD_MUTEX_INITIALIZER;
std::vector<MountInfoMessage> serial_mount_infos;
pthread_mutex_t serial_prefetch_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t serial_prefetch_cond = PTHREAD_COND_INITIALIZER;
bool n64_prefetch_running = false;

std::vector<CardSlot> parse_scan_lines(const std::vector<std::string>& lines);
void update_slot_cache(const std::vector<CardSlot>& slots);
bool start_n64_prefetch(bool hotplug);

const char* card_kind_label(CardKind kind) {
	switch (kind) {
	case CardKind::PSX: return "PSX";
	case CardKind::N64: return "N64";
	case CardKind::GBPak: return "GBPAK";
	default: return "card";
	}
}

bool card_kind_writable(CardKind kind) {
	return kind != CardKind::None;
}

bool card_kind_uses_n64_prefetch(CardKind kind) {
	return kind == CardKind::N64 || kind == CardKind::GBPak;
}

uint64_t elapsed_ms_since(const timespec& started) {
	timespec finished = {};
	clock_gettime(CLOCK_MONOTONIC, &finished);
	const uint64_t started_ms = (uint64_t)started.tv_sec * 1000 + (uint64_t)started.tv_nsec / 1000000;
	const uint64_t finished_ms = (uint64_t)finished.tv_sec * 1000 + (uint64_t)finished.tv_nsec / 1000000;
	return finished_ms - started_ms;
}

void command_label(const char* command, char* out, size_t out_len) {
	if (!out_len) return;
	out[0] = 0;
	if (!command) return;

	size_t len = 0;
	unsigned spaces = 0;
	while (command[len] && len + 1 < out_len) {
		if (command[len] == ' ') {
			spaces++;
			if (spaces >= 2) break;
		}
		out[len] = command[len];
		len++;
	}
	out[len] = 0;
}

void log_scan_result(const std::vector<CardSlot>& slots) {
	if (slots.empty()) {
		printf("Serial memcard: scan found no cards.\n");
		fflush(stdout);
		return;
	}

	char summary[160] = {};
	size_t len = 0;
	for (const auto& slot : slots) {
		const int written = snprintf(summary + len, sizeof(summary) - len,
		                             "%s%s P%u S%u",
		                             len ? ", " : "",
		                             card_kind_label(slot.kind),
		                             slot.port + 1,
		                             slot.slot);
		if (written < 0) break;
		if ((size_t)written >= sizeof(summary) - len) {
			len = sizeof(summary) - 1;
			break;
		}
		len += (size_t)written;
	}
	printf("Serial memcard: scan found %s.\n", summary);
	fflush(stdout);
}

uint32_t image_crc32(const std::vector<uint8_t>& image) {
	return (uint32_t)crc32(0L, image.data(), (uInt)image.size());
}

void remember_serial_mount_info(const char* popup, const char* summary) {
	pthread_mutex_lock(&serial_mount_info_mutex);
	if (popup && *popup) {
		MountInfoMessage message;
		message.popup = popup;
		message.summary = (summary && *summary) ? summary : popup;
		serial_mount_infos.push_back(message);
	}
	pthread_mutex_unlock(&serial_mount_info_mutex);
}

const char* mount_info_title(CardKind kind) {
	(void)kind;
	return "Memcard loaded";
}

const char* mount_info_size_suffix(CardKind kind) {
	return kind == CardKind::GBPak ? " SRAM" : "";
}

void format_serial_mount_size_label(const PendingMount& pending, char* out, size_t out_len) {
	if (!out_len) return;
	out[0] = 0;
	const uint32_t bytes = (uint32_t)pending.image.size();
	if (bytes >= 1024 && !(bytes % 1024)) {
		snprintf(out, out_len, "%uKB%s", bytes / 1024,
		         mount_info_size_suffix(pending.slot.kind));
	} else {
		snprintf(out, out_len, "%u bytes%s", bytes,
		         mount_info_size_suffix(pending.slot.kind));
	}
}

void format_serial_mount_info(const PendingMount& pending, char* out, size_t out_len) {
	if (!out_len) return;
	out[0] = 0;
	char size_label[32] = {};
	format_serial_mount_size_label(pending, size_label, sizeof(size_label));

	snprintf(out, out_len, "%s\n%s",
	         mount_info_title(pending.slot.kind),
	         size_label);
}

void format_serial_mount_info_summary(const PendingMount& pending, char* out, size_t out_len) {
	if (!out_len) return;
	out[0] = 0;
	char size_label[32] = {};
	format_serial_mount_size_label(pending, size_label, sizeof(size_label));
	snprintf(out, out_len, "%s: %s",
	         mount_info_title(pending.slot.kind),
	         size_label);
}

void invalidate_slot_cache() {
	pthread_mutex_lock(&serial_slot_cache_mutex);
	cached_scan_slots.clear();
	cached_scan_slots_valid = false;
	cached_psx_slot_mask = 0;
	cached_psx_slots_scanned = false;
	pthread_mutex_unlock(&serial_slot_cache_mutex);
}

void invalidate_device_cache() {
	pthread_mutex_lock(&serial_device_cache_mutex);
	cached_serial_candidates.clear();
	cached_serial_candidates_valid = false;
	pthread_mutex_unlock(&serial_device_cache_mutex);
}

bool read_cached_slots(std::vector<CardSlot>& slots) {
	pthread_mutex_lock(&serial_slot_cache_mutex);
	if (!cached_scan_slots_valid) {
		pthread_mutex_unlock(&serial_slot_cache_mutex);
		return false;
	}
	slots = cached_scan_slots;
	pthread_mutex_unlock(&serial_slot_cache_mutex);
	return true;
}

bool device_path_exists(const char* path) {
	struct stat st = {};
	return stat(path, &st) == 0;
}

bool is_tty_basename(const char* name) {
	return !strncmp(name, "ttyACM", 6) || !strncmp(name, "ttyUSB", 6);
}

std::string serial_log_path(const std::string& path) {
	char resolved[PATH_MAX];
	const char* display = path.c_str();
	if (realpath(path.c_str(), resolved)) display = resolved;

	const char* base = strrchr(display, '/');
	if (base && is_tty_basename(base + 1)) {
		return std::string("/dev/") + (base + 1);
	}
	return "whitelisted serial device";
}

bool read_text_file(const char* path, char* out, size_t out_len) {
	FILE* fp = fopen(path, "r");
	if (!fp) return false;
	if (!fgets(out, (int)out_len, fp)) {
		fclose(fp);
		return false;
	}
	fclose(fp);
	for (size_t i = 0; out[i]; ++i) {
		if (out[i] == '\r' || out[i] == '\n') {
			out[i] = '\0';
			break;
		}
	}
	return true;
}

bool read_usb_id_for_tty(const char* tty_name, uint16_t* vid, uint16_t* pid) {
	char link_path[PATH_MAX];
	char resolved[PATH_MAX];
	snprintf(link_path, sizeof(link_path), "/sys/class/tty/%s/device", tty_name);
	if (!realpath(link_path, resolved)) return false;

	for (int depth = 0; depth < 8; ++depth) {
		char vid_path[PATH_MAX];
		char pid_path[PATH_MAX];
		char vid_text[16] = {};
		char pid_text[16] = {};
		snprintf(vid_path, sizeof(vid_path), "%s/idVendor", resolved);
		snprintf(pid_path, sizeof(pid_path), "%s/idProduct", resolved);
		if (read_text_file(vid_path, vid_text, sizeof(vid_text)) &&
		    read_text_file(pid_path, pid_text, sizeof(pid_text))) {
			*vid = (uint16_t)strtoul(vid_text, nullptr, 16);
			*pid = (uint16_t)strtoul(pid_text, nullptr, 16);
			return true;
		}

		char* slash = strrchr(resolved, '/');
		if (!slash || slash == resolved) break;
		*slash = '\0';
	}
	return false;
}

bool tty_name_from_path(const std::string& path, char* out, size_t out_len) {
	char resolved[PATH_MAX];
	if (!realpath(path.c_str(), resolved)) return false;
	const char* tty_name = strrchr(resolved, '/');
	tty_name = tty_name ? tty_name + 1 : resolved;
	if (strncmp(tty_name, "ttyACM", 6) && strncmp(tty_name, "ttyUSB", 6)) return false;
	snprintf(out, out_len, "%s", tty_name);
	return true;
}

bool serial_memcard_known_usb_id(const std::string& path) {
	char tty_name[64] = {};
	uint16_t vid = 0;
	uint16_t pid = 0;
	if (!tty_name_from_path(path, tty_name, sizeof(tty_name))) return false;
	if (!read_usb_id_for_tty(tty_name, &vid, &pid)) return false;
	for (const auto& id : kSerialMemcardUsbIds) {
		if (vid == id.vid && pid == id.pid) return true;
	}
	return false;
}

void add_candidate(std::vector<std::string>& candidates, const std::string& path) {
	if (path.empty() || !device_path_exists(path.c_str())) return;
	if (!serial_memcard_known_usb_id(path)) return;
	if (std::find(candidates.begin(), candidates.end(), path) != candidates.end()) return;
	candidates.push_back(path);
}

std::vector<std::string> serial_memcard_port_candidates() {
	std::vector<std::string> candidates;

	DIR* dir = opendir("/dev/serial/by-id");
	if (dir) {
		dirent* entry = nullptr;
		while ((entry = readdir(dir)) != nullptr) {
			if (entry->d_name[0] == '.') continue;
			add_candidate(candidates, std::string("/dev/serial/by-id/") + entry->d_name);
		}
		closedir(dir);
	}

	for (int i = 0; i < 16; ++i) {
		char path[64];
		snprintf(path, sizeof(path), "/dev/ttyACM%d", i);
		add_candidate(candidates, path);
	}

	for (int i = 0; i < 16; ++i) {
		char path[64];
		snprintf(path, sizeof(path), "/dev/ttyUSB%d", i);
		add_candidate(candidates, path);
	}

	return candidates;
}

std::vector<std::string> cached_serial_memcard_port_candidates() {
	pthread_mutex_lock(&serial_device_cache_mutex);
	if (cached_serial_candidates_valid) {
		auto candidates = cached_serial_candidates;
		pthread_mutex_unlock(&serial_device_cache_mutex);
		return candidates;
	}
	pthread_mutex_unlock(&serial_device_cache_mutex);

	auto candidates = serial_memcard_port_candidates();

	pthread_mutex_lock(&serial_device_cache_mutex);
	cached_serial_candidates = candidates;
	cached_serial_candidates_valid = true;
	pthread_mutex_unlock(&serial_device_cache_mutex);
	return candidates;
}

bool configure_serial(int fd, speed_t speed) {
	termios tty = {};
	if (tcgetattr(fd, &tty) != 0) return false;
	cfmakeraw(&tty);
	cfsetispeed(&tty, speed);
	cfsetospeed(&tty, speed);
	tty.c_cflag |= (CLOCAL | CREAD);
	tty.c_cflag &= ~CRTSCTS;
	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 1;
	if (tcsetattr(fd, TCSANOW, &tty) != 0) return false;

	int modem_bits = 0;
	if (ioctl(fd, TIOCMGET, &modem_bits) == 0) {
		modem_bits |= TIOCM_DTR | TIOCM_RTS;
		ioctl(fd, TIOCMSET, &modem_bits);
	}
	return true;
}

void* serial_reader_main(void*) {
	std::string line;
	while (serial_reader_active) {
		pollfd pfd = { serial_rx_fd, POLLIN, 0 };
		const int ret = poll(&pfd, 1, 100);
		if (!serial_reader_active) break;
		if (ret <= 0) continue;

		char ch = 0;
		const ssize_t got = read(serial_rx_fd, &ch, 1);
		if (got <= 0) continue;
		if (ch == '\n') {
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
				line.pop_back();
			}
			pthread_mutex_lock(&serial_reader_mutex);
			serial_reader_lines.push_back(line);
			pthread_cond_signal(&serial_reader_cond);
			pthread_mutex_unlock(&serial_reader_mutex);
			line.clear();
		} else {
			line.push_back(ch);
		}
	}
	return nullptr;
}

bool start_serial_reader() {
	pthread_mutex_lock(&serial_reader_mutex);
	serial_reader_lines.clear();
	serial_reader_active = true;
	pthread_mutex_unlock(&serial_reader_mutex);
	if (pthread_create(&serial_reader_thread, nullptr, serial_reader_main, nullptr) != 0) {
		pthread_mutex_lock(&serial_reader_mutex);
		serial_reader_active = false;
		pthread_mutex_unlock(&serial_reader_mutex);
		return false;
	}
	serial_reader_started = true;
	return true;
}

void stop_serial_reader() {
	if (!serial_reader_started) return;
	pthread_mutex_lock(&serial_reader_mutex);
	serial_reader_active = false;
	pthread_cond_broadcast(&serial_reader_cond);
	pthread_mutex_unlock(&serial_reader_mutex);
	pthread_join(serial_reader_thread, nullptr);
	serial_reader_started = false;
	pthread_mutex_lock(&serial_reader_mutex);
	serial_reader_lines.clear();
	pthread_mutex_unlock(&serial_reader_mutex);
}

void clear_serial_reader_lines() {
	pthread_mutex_lock(&serial_reader_mutex);
	serial_reader_lines.clear();
	pthread_mutex_unlock(&serial_reader_mutex);
}

void close_serial() {
	stop_serial_reader();
	if (serial_rx_fd >= 0) {
		close(serial_rx_fd);
		serial_rx_fd = -1;
	}
	serial_baud = 0;
	invalidate_slot_cache();
}

bool write_all(const char* data, size_t len) {
	if (serial_rx_fd < 0) return false;
	while (len) {
		const ssize_t written = write(serial_rx_fd, data, len);
		if (written <= 0) {
			return false;
		}
		data += written;
		len -= (size_t)written;
	}
	return true;
}

bool read_line(std::string& line, int timeout_ms) {
	line.clear();
	if (serial_rx_fd < 0 || !serial_reader_started) return false;

	timespec deadline = {};
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += timeout_ms / 1000;
	deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
	if (deadline.tv_nsec >= 1000000000) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000;
	}

	pthread_mutex_lock(&serial_reader_mutex);
	while (serial_reader_lines.empty() && serial_reader_active) {
		if (pthread_cond_timedwait(&serial_reader_cond, &serial_reader_mutex, &deadline) == ETIMEDOUT) break;
	}
	if (!serial_reader_lines.empty()) {
		line = serial_reader_lines.front();
		serial_reader_lines.erase(serial_reader_lines.begin());
		pthread_mutex_unlock(&serial_reader_mutex);
		return true;
	}
	pthread_mutex_unlock(&serial_reader_mutex);
	return false;
}

void drain_serial() {
	std::string ignored;
	for (int i = 0; i < 20; ++i) {
		if (!read_line(ignored, 25)) break;
	}
}

uint64_t monotonic_ms() {
	timespec now = {};
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

bool raw_read_byte_until(uint64_t deadline_ms, uint8_t& value) {
	while (monotonic_ms() < deadline_ms) {
		const uint64_t now = monotonic_ms();
		const int timeout_ms = deadline_ms > now ? (int)(deadline_ms - now) : 0;
		pollfd pfd = { serial_rx_fd, POLLIN, 0 };
		const int ret = poll(&pfd, 1, timeout_ms);
		if (ret < 0) {
			if (errno == EINTR) continue;
			return false;
		}
		if (ret == 0) return false;

		const ssize_t got = read(serial_rx_fd, &value, 1);
		if (got == 1) return true;
		if (got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
		if (got < 0) return false;
	}
	return false;
}

bool raw_read_line_until(std::string& line, uint64_t deadline_ms) {
	line.clear();
	uint8_t ch = 0;
	while (raw_read_byte_until(deadline_ms, ch)) {
		if (ch == '\n') {
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
				line.pop_back();
			}
			return true;
		}
		line.push_back((char)ch);
		if (line.size() > 1024) return false;
	}
	return false;
}

bool raw_read_exact_until(uint8_t* data, size_t len, uint64_t deadline_ms) {
	size_t offset = 0;
	while (offset < len && monotonic_ms() < deadline_ms) {
		pollfd pfd = { serial_rx_fd, POLLIN, 0 };
		const uint64_t now = monotonic_ms();
		const int timeout_ms = deadline_ms > now ? (int)(deadline_ms - now) : 0;
		const int ret = poll(&pfd, 1, timeout_ms);
		if (ret < 0) {
			if (errno == EINTR) continue;
			return false;
		}
		if (ret == 0) return false;

		const ssize_t got = read(serial_rx_fd, data + offset, len - offset);
		if (got > 0) {
			offset += (size_t)got;
			continue;
		}
		if (got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
		if (got < 0) return false;
	}
	return offset == len;
}

bool raw_consume_line_ending_until(uint64_t deadline_ms) {
	uint8_t ch = 0;
	if (!raw_read_byte_until(deadline_ms, ch)) return false;
	if (ch == '\n') return true;
	if (ch != '\r') return false;
	if (!raw_read_byte_until(deadline_ms, ch)) return false;
	return ch == '\n';
}

bool open_serial_memcard_port(const std::string& path, const SerialSpeed& speed) {
	serial_path = path;
	serial_rx_fd = open(path.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
	if (serial_rx_fd < 0 || !configure_serial(serial_rx_fd, speed.speed)) {
		printf("Serial memcard: failed to open %s: %s\n",
		       serial_log_path(path).c_str(), strerror(errno));
		close_serial();
		return false;
	}
	serial_baud = speed.baud;

	if (!start_serial_reader()) {
		printf("Serial memcard: failed to open %s: %s\n",
		       serial_log_path(path).c_str(), strerror(errno));
		close_serial();
		return false;
	}

	usleep(1500000);
	drain_serial();
	return true;
}

bool send_command_open(const char* command, const char* ok_token, std::vector<std::string>* out_lines,
                       int timeout_ms) {
	if (out_lines) out_lines->clear();
	clear_serial_reader_lines();

	std::string line(command);
	line += "\n";
	if (!write_all(line.c_str(), line.size())) {
		char label[64] = {};
		command_label(command, label, sizeof(label));
		printf("Serial memcard: command failed (%s): write failed.\n", label);
		fflush(stdout);
		close_serial();
		return false;
	}

	const int deadline = timeout_ms;
	int waited = 0;
	while (waited < deadline) {
		std::string received;
		const int slice = 100;
		if (!read_line(received, slice)) {
			waited += slice;
			continue;
		}
		if (out_lines) out_lines->push_back(received);
		if (received.rfind("ERR:", 0) == 0) {
			char label[64] = {};
			command_label(command, label, sizeof(label));
			printf("Serial memcard: command failed (%s): %s\n", label, received.c_str());
			return false;
		}
		if (ok_token && received == ok_token) {
			return true;
		}
		if (!ok_token && (received.rfind("OK:", 0) == 0 || received == "PONG")) {
			return true;
		}
	}

	char label[64] = {};
	command_label(command, label, sizeof(label));
	printf("Serial memcard: command timed out (%s).\n", label);
	close_serial();
	return false;
}

bool ensure_serial_open(bool log_initial_scan = true) {
	if (serial_rx_fd >= 0) return true;

	auto candidates = cached_serial_memcard_port_candidates();
	if (candidates.empty()) {
		printf("Serial memcard: no known serial memory-card bridge found.\n");
		return false;
	}

	for (const auto& candidate : candidates) {
		for (const auto& speed : kSerialSpeeds) {
			close_serial();
			if (!open_serial_memcard_port(candidate, speed)) continue;

			std::vector<std::string> lines;
			if (send_command_open("CARD SCAN", "OK:CARD_SCAN", &lines, 3000)) {
				auto slots = parse_scan_lines(lines);
				printf("Serial memcard: connected to %s at %u baud\n",
				       serial_log_path(serial_path).c_str(), serial_baud);
				fflush(stdout);
				if (log_initial_scan) log_scan_result(slots);
				update_slot_cache(slots);
				return true;
			}
		}
	}

	close_serial();
	invalidate_device_cache();
	printf("Serial memcard: no responsive serial memory-card bridge found.\n");
	return false;
}

bool send_command(const char* command, const char* ok_token, std::vector<std::string>* out_lines,
                  int timeout_ms = 8000) {
	pthread_mutex_lock(&serial_command_mutex);
	const bool result = ensure_serial_open() &&
		send_command_open(command, ok_token, out_lines, timeout_ms);
	pthread_mutex_unlock(&serial_command_mutex);
	return result;
}

bool parse_card_line(const std::string& line, CardSlot& slot) {
	int port = 0, s = 0, present = 0, blocks = 0, block_size = 0;
	char type[16] = {};
	if (sscanf(line.c_str(), "CARD P=%d S=%d TYPE=%15s PRESENT=%d BLOCKS=%d BLOCK_SIZE=%d",
	           &port, &s, type, &present, &blocks, &block_size) != 6) {
		return false;
	}
	if (!present) return false;
	if (!strcmp(type, "PSXMEM")) {
		slot.kind = CardKind::PSX;
	} else if (!strcmp(type, "N64PAK")) {
		slot.kind = CardKind::N64;
	} else if (!strcmp(type, "GBPAK")) {
		slot.kind = CardKind::GBPak;
	} else {
		return false;
	}
	if (blocks <= 0 || block_size <= 0) return false;
	slot.port = (uint8_t)port;
	slot.slot = (uint8_t)s;
	slot.blocks = (uint16_t)blocks;
	slot.block_size = (uint16_t)block_size;
	return true;
}

bool card_slot_less(const CardSlot& a, const CardSlot& b) {
	if (a.port != b.port) return a.port < b.port;
	if (a.slot != b.slot) return a.slot < b.slot;
	return (uint8_t)a.kind < (uint8_t)b.kind;
}

bool same_card_slot(const CardSlot& a, const CardSlot& b) {
	return a.kind == b.kind && a.port == b.port && a.slot == b.slot;
}

uint16_t gb_title_end_from_header(const uint8_t* header) {
	return (header[0x143] == 0x80 || header[0x143] == 0xC0) ? 0x13E : 0x143;
}

std::string gb_title_from_header(const uint8_t* header) {
	std::string title;
	const uint16_t end = gb_title_end_from_header(header);
	for (uint16_t offset = 0x134; offset <= end; ++offset) {
		const uint8_t c = header[offset];
		if (!c) break;
		title.push_back((c >= 0x20 && c <= 0x7E) ? (char)c : '_');
	}
	return title;
}

std::string gb_title_key_from_header(const uint8_t* header) {
	std::string key;
	const uint16_t end = gb_title_end_from_header(header);
	for (uint16_t offset = 0x134; offset <= end; ++offset) {
		const uint8_t c = header[offset];
		if (!c) break;
		if (isspace(c)) continue;
		if (c >= 'a' && c <= 'z') key.push_back((char)(c - 32));
		else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) key.push_back((char)c);
		else key.push_back('_');
	}
	return key;
}

bool parse_line_token_string(const std::string& line, const char* key, std::string& value) {
	const std::string needle = std::string(key) + "=";
	size_t pos = line.find(needle);
	if (pos == std::string::npos) return false;
	pos += needle.size();
	if (pos < line.size() && line[pos] == '"') {
		const size_t end = line.find('"', pos + 1);
		if (end == std::string::npos) return false;
		value = line.substr(pos + 1, end - pos - 1);
		return true;
	}
	const size_t end = line.find_first_of(" \t\r\n", pos);
	value = line.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
	return true;
}

bool parse_line_token_u32(const std::string& line, const char* key, uint32_t& value) {
	std::string text;
	if (!parse_line_token_string(line, key, text) || text.empty()) return false;
	char* end = nullptr;
	const unsigned long parsed = strtoul(text.c_str(), &end, 0);
	if (end == text.c_str()) return false;
	value = (uint32_t)parsed;
	return true;
}

bool parse_gb_info_line(const std::string& line, GbPakIdentity& identity) {
	if (line.rfind("CARD GB ", 0) != 0) return false;
	uint32_t value = 0;
	if (parse_line_token_u32(line, "P", value)) identity.port = (uint8_t)value;
	if (parse_line_token_u32(line, "CART", value)) identity.cart_present = value != 0;
	if (parse_line_token_u32(line, "SUPPORTED", value)) identity.supported = value != 0;
	if (parse_line_token_u32(line, "CGB", value)) identity.cgb_flag = (uint8_t)value;
	if (parse_line_token_u32(line, "HEADER_CSUM", value)) identity.header_checksum = (uint8_t)value;
	if (parse_line_token_u32(line, "GLOBAL_CSUM", value)) identity.global_checksum = (uint16_t)value;
	if (parse_line_token_u32(line, "ROM_SIZE", value)) identity.rom_size = value;
	if (parse_line_token_u32(line, "SAVE_SIZE", value)) identity.save_size = value;
	parse_line_token_string(line, "TITLE", identity.title);
	parse_line_token_string(line, "TITLE_KEY", identity.title_key);
	identity.valid = identity.cart_present && identity.supported &&
	                 !identity.title_key.empty() && identity.global_checksum != 0;
	return true;
}

bool read_gb_rom_identity_from_file(const char* path, GbPakIdentity& identity) {
	if (!path || !*path) return false;
	fileTYPE file = {};
	if (!FileOpen(&file, path, 1)) return false;
	uint8_t header[0x150] = {};
	const bool ok = file.size >= (int)sizeof(header) &&
	                FileReadAdv(&file, header, sizeof(header)) == (int)sizeof(header);
	FileClose(&file);
	if (!ok) return false;
	identity = GbPakIdentity{};
	identity.valid = true;
	identity.cgb_flag = header[0x143];
	identity.header_checksum = header[0x14D];
	identity.global_checksum = ((uint16_t)header[0x14E] << 8) | header[0x14F];
	identity.title = gb_title_from_header(header);
	identity.title_key = gb_title_key_from_header(header);
	return !identity.title_key.empty() && identity.global_checksum != 0;
}

bool gb_identity_matches(const GbPakIdentity& cart, const GbPakIdentity& rom) {
	return cart.valid && rom.valid &&
	       cart.global_checksum == rom.global_checksum &&
	       cart.title_key == rom.title_key;
}

std::string safe_cache_component(const std::string& text) {
	std::string out;
	for (unsigned char c : text) {
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ') {
			out.push_back((char)c);
		} else {
			out.push_back('_');
		}
	}
	while (!out.empty() && (out.back() == ' ' || out.back() == '_')) out.pop_back();
	while (!out.empty() && (out.front() == ' ' || out.front() == '_')) out.erase(out.begin());
	return out.empty() ? "cartridge" : out;
}

std::string base_name_without_extension(const char* path) {
	if (!path || !*path) return {};
	const char* base = strrchr(path, '/');
	base = base ? base + 1 : path;
	std::string name(base);
	const size_t dot = name.find_last_of('.');
	if (dot != std::string::npos) name.erase(dot);
	return name;
}

void update_slot_cache(const std::vector<CardSlot>& slots) {
	uint8_t psx_mask = 0;
	uint8_t psx_ordinal = 0;
	for (const auto& slot : slots) {
		if (slot.kind == CardKind::PSX && psx_ordinal < 8) {
			psx_mask |= 1U << psx_ordinal++;
		}
	}

	pthread_mutex_lock(&serial_slot_cache_mutex);
	cached_scan_slots = slots;
	cached_scan_slots_valid = true;
	cached_psx_slot_mask = psx_mask;
	cached_psx_slots_scanned = true;
	pthread_mutex_unlock(&serial_slot_cache_mutex);
}

std::vector<CardSlot> parse_scan_lines(const std::vector<std::string>& lines) {
	std::vector<CardSlot> slots;
	for (const auto& line : lines) {
		CardSlot slot;
		if (parse_card_line(line, slot)) slots.push_back(slot);
	}
	std::sort(slots.begin(), slots.end(), card_slot_less);
	return slots;
}

std::vector<CardSlot> scan_slots(bool log_result = true) {
	std::vector<std::string> lines;
	std::vector<CardSlot> slots;
	if (read_cached_slots(slots)) return slots;

	pthread_mutex_lock(&serial_command_mutex);
	bool ok = ensure_serial_open(log_result);
	if (ok && read_cached_slots(slots)) {
		pthread_mutex_unlock(&serial_command_mutex);
		return slots;
	}
	if (ok) ok = send_command_open("CARD SCAN", "OK:CARD_SCAN", &lines, 8000);
	if (!ok && ensure_serial_open(log_result) && read_cached_slots(slots)) {
		pthread_mutex_unlock(&serial_command_mutex);
		return slots;
	}
	pthread_mutex_unlock(&serial_command_mutex);

	if (!ok) {
		return slots;
	}

	slots = parse_scan_lines(lines);
	if (log_result) log_scan_result(slots);
	update_slot_cache(slots);
	return slots;
}

bool slots_contain_kind(const std::vector<CardSlot>& slots, CardKind kind) {
	for (const auto& slot : slots) {
		if (slot.kind == kind) return true;
	}
	return false;
}

std::vector<CardSlot> scan_slots_retry_for_kind(CardKind kind, unsigned attempts, useconds_t delay_us) {
	std::vector<CardSlot> slots;
	for (unsigned attempt = 0; attempt < attempts; ++attempt) {
		invalidate_slot_cache();
		slots = scan_slots(false);
		if (slots_contain_kind(slots, kind)) return slots;
		if (attempt + 1 < attempts) {
			usleep(delay_us);
		}
	}
	return slots;
}

bool hex_nibble(char ch, uint8_t& value) {
	if (ch >= '0' && ch <= '9') {
		value = (uint8_t)(ch - '0');
		return true;
	}
	if (ch >= 'a' && ch <= 'f') {
		value = (uint8_t)(ch - 'a' + 10);
		return true;
	}
	if (ch >= 'A' && ch <= 'F') {
		value = (uint8_t)(ch - 'A' + 10);
		return true;
	}
	return false;
}

bool parse_hex_bytes(const char* hex, std::vector<uint8_t>& out) {
	const size_t len = strlen(hex);
	if (len & 1) return false;
	out.resize(len / 2);
	for (size_t i = 0; i < out.size(); ++i) {
		uint8_t hi = 0;
		uint8_t lo = 0;
		if (!hex_nibble(hex[i * 2], hi) || !hex_nibble(hex[i * 2 + 1], lo)) {
			return false;
		}
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

bool read_block(const CardSlot& slot, uint16_t block, std::vector<uint8_t>& out) {
	char command[64];
	snprintf(command, sizeof(command), "CARD READ %u %u %u", slot.port, slot.slot, block);
	std::vector<std::string> lines;
	if (!send_command(command, "OK:CARD_READ", &lines, 8000)) return false;

	char prefix[64];
	snprintf(prefix, sizeof(prefix), "CARD DATA P=%u S=%u B=%u HEX=", slot.port, slot.slot, block);
	for (const auto& line : lines) {
		if (line.rfind(prefix, 0) != 0) continue;
		if (!parse_hex_bytes(line.c_str() + strlen(prefix), out)) return false;
		return out.size() == slot.block_size;
	}
	return false;
}

bool write_block(const CardSlot& slot, uint16_t block, const uint8_t* data) {
	char command[96];
	snprintf(command, sizeof(command), "CARD WRITEBEGIN %u %u %u", slot.port, slot.slot, block);
	if (!send_command(command, "OK:CARD_WRITEBEGIN", nullptr, 4000)) return false;

	for (uint16_t offset = 0; offset < slot.block_size; offset += kWriteChunkBytes) {
		const uint16_t remaining = slot.block_size - offset;
		const uint16_t chunk = remaining < kWriteChunkBytes
			? remaining
			: kWriteChunkBytes;
		char hex[kWriteChunkBytes * 2 + 1] = {};
		for (uint16_t i = 0; i < chunk; ++i) {
			snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02X", data[offset + i]);
		}
		snprintf(command, sizeof(command), "CARD WRITECHUNK %u %s", offset, hex);
		if (!send_command(command, "OK:CARD_WRITECHUNK", nullptr, 8000)) {
			send_command("CARD WRITEABORT", "OK:CARD_WRITEABORT", nullptr, 1000);
			return false;
		}
		usleep(2000);
	}
	return send_command("CARD WRITECOMMIT", "OK:CARD_WRITECOMMIT", nullptr, 8000);
}

bool write_blocks_range(const CardSlot& slot, uint16_t start_block, const uint8_t* data,
                        uint16_t block_count) {
	if (!block_count || !slot.block_size) return false;

	pthread_mutex_lock(&serial_command_mutex);
	bool ok = ensure_serial_open();
	char command[160];
	if (ok) {
		snprintf(command, sizeof(command), "CARD WRITEBLOCKSBEGIN %u %u %u %u",
		         slot.port, slot.slot, start_block, block_count);
		ok = send_command_open(command, "OK:CARD_WRITEBLOCKSBEGIN", nullptr, 4000);
	}

	const uint32_t total_bytes = (uint32_t)block_count * slot.block_size;
	for (uint32_t offset = 0; ok && offset < total_bytes; offset += kWriteRangeChunkBytes) {
		const uint32_t remaining = total_bytes - offset;
		const uint32_t chunk = remaining < kWriteRangeChunkBytes
			? remaining
			: kWriteRangeChunkBytes;
		char hex[kWriteRangeChunkBytes * 2 + 1] = {};
		for (uint32_t i = 0; i < chunk; ++i) {
			snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02X", data[offset + i]);
		}
		snprintf(command, sizeof(command), "CARD WRITEBLOCKSCHUNK %u %s", offset, hex);
		ok = send_command_open(command, "OK:CARD_WRITEBLOCKSCHUNK", nullptr, 12000);
		if (ok) usleep(2000);
	}

	if (ok) {
		ok = send_command_open("CARD WRITEBLOCKSCOMMIT", "OK:CARD_WRITEBLOCKSCOMMIT",
		                       nullptr, 12000);
	} else if (serial_rx_fd >= 0) {
		send_command_open("CARD WRITEBLOCKSABORT", "OK:CARD_WRITEBLOCKSABORT", nullptr, 1000);
	}

	pthread_mutex_unlock(&serial_command_mutex);
	return ok;
}

bool write_gb_save_blocks_range(const CardSlot& slot, uint16_t start_block, const uint8_t* data,
                                uint16_t block_count) {
	if (slot.kind != CardKind::GBPak || !block_count || slot.block_size != 32) return false;

	pthread_mutex_lock(&serial_command_mutex);
	bool ok = ensure_serial_open();
	char command[160];
	if (ok) {
		snprintf(command, sizeof(command), "CARD GBWRITESAVEBEGIN %u %u %u",
		         slot.port, start_block, block_count);
		ok = send_command_open(command, "OK:CARD_GBWRITESAVEBEGIN", nullptr, 4000);
	}

	const uint32_t total_bytes = (uint32_t)block_count * slot.block_size;
	for (uint32_t offset = 0; ok && offset < total_bytes; offset += kWriteRangeChunkBytes) {
		const uint32_t remaining = total_bytes - offset;
		const uint32_t chunk = remaining < kWriteRangeChunkBytes
			? remaining
			: kWriteRangeChunkBytes;
		char hex[kWriteRangeChunkBytes * 2 + 1] = {};
		for (uint32_t i = 0; i < chunk; ++i) {
			snprintf(hex + i * 2, sizeof(hex) - i * 2, "%02X", data[offset + i]);
		}
		snprintf(command, sizeof(command), "CARD GBWRITESAVECHUNK %u %s", offset, hex);
		ok = send_command_open(command, "OK:CARD_GBWRITESAVECHUNK", nullptr, 12000);
		if (ok) usleep(2000);
	}

	if (ok) {
		ok = send_command_open("CARD GBWRITESAVECOMMIT", "OK:CARD_GBWRITESAVECOMMIT",
		                       nullptr, 12000);
	} else if (serial_rx_fd >= 0) {
		send_command_open("CARD GBWRITESAVEABORT", "OK:CARD_GBWRITESAVEABORT", nullptr, 1000);
	}

	pthread_mutex_unlock(&serial_command_mutex);
	return ok;
}

bool read_image_range_open(const CardSlot& slot, std::vector<uint8_t>& image) {
	const size_t image_size = (size_t)slot.blocks * slot.block_size;
	image.assign(image_size, 0);

	char command[96];
	snprintf(command, sizeof(command), "CARD READRANGE %u %u 0 %u",
	         slot.port, slot.slot, slot.blocks);
	std::string line(command);
	line += "\n";
	if (!write_all(line.c_str(), line.size())) return false;

	const uint64_t deadline_ms = monotonic_ms() + std::max<uint64_t>(30000, slot.blocks * 120);
	if (!raw_read_line_until(line, deadline_ms)) return false;
	if (line.rfind("ERR:", 0) == 0) {
		return false;
	}

	unsigned port = 0, physical_slot = 0, start = 0, count = 0, block_size = 0, len = 0;
	if (sscanf(line.c_str(), "CARD RANGE P=%u S=%u START=%u COUNT=%u BLOCK_SIZE=%u LEN=%u",
	           &port, &physical_slot, &start, &count, &block_size, &len) != 6 ||
	    port != slot.port ||
	    physical_slot != slot.slot ||
	    start != 0 ||
	    count != slot.blocks ||
	    block_size != slot.block_size ||
	    len != image_size) {
		printf("Serial memcard: range read failed for %s P%u S%u: bad header.\n",
		       card_kind_label(slot.kind), slot.port + 1, slot.slot);
		return false;
	}

	for (uint16_t index = 0; index < slot.blocks; ++index) {
		if (!raw_read_line_until(line, deadline_ms)) return false;
		if (line.rfind("ERR:", 0) == 0) {
			printf("Serial memcard: range read failed for %s P%u S%u at block %u: %s\n",
			       card_kind_label(slot.kind), slot.port + 1, slot.slot, index, line.c_str());
			return false;
		}

		unsigned block = 0, binary_len = 0;
		if (sscanf(line.c_str(), "CARD BIN P=%u S=%u B=%u LEN=%u",
		           &port, &physical_slot, &block, &binary_len) != 4 ||
		    port != slot.port ||
		    physical_slot != slot.slot ||
		    block != index ||
		    binary_len != slot.block_size) {
			printf("Serial memcard: range read failed for %s P%u S%u at block %u: bad block header.\n",
			       card_kind_label(slot.kind), slot.port + 1, slot.slot, index);
			return false;
		}

		if (!raw_read_exact_until(image.data() + (size_t)index * slot.block_size,
		                          slot.block_size, deadline_ms) ||
		    !raw_consume_line_ending_until(deadline_ms) ||
		    !raw_read_line_until(line, deadline_ms) ||
		    line != "OK:CARD_BIN") {
			printf("Serial memcard: range read failed for %s P%u S%u at block %u.\n",
			       card_kind_label(slot.kind), slot.port + 1, slot.slot, index);
			return false;
		}
	}

	if (!raw_read_line_until(line, deadline_ms) || line != "OK:CARD_READRANGE") {
		printf("Serial memcard: range read failed for %s P%u S%u: missing completion.\n",
		       card_kind_label(slot.kind), slot.port + 1, slot.slot);
		return false;
	}
	return true;
}

bool read_image_range(const CardSlot& slot, std::vector<uint8_t>& image) {
	if (!slot.blocks || !slot.block_size) return false;

	pthread_mutex_lock(&serial_command_mutex);
	bool ok = ensure_serial_open();
	if (ok) {
		stop_serial_reader();
		ok = read_image_range_open(slot, image);
	}
	if (ok) {
		if (!start_serial_reader()) {
			close_serial();
			ok = false;
		}
	} else {
		close_serial();
	}
	pthread_mutex_unlock(&serial_command_mutex);
	return ok;
}

bool read_gb_save_range_open(const CardSlot& slot, std::vector<uint8_t>& image) {
	const size_t image_size = (size_t)slot.blocks * slot.block_size;
	image.assign(image_size, 0);

	char command[96];
	snprintf(command, sizeof(command), "CARD GBREADSAVERAW %u 0 %u", slot.port, slot.blocks);
	std::string line(command);
	line += "\n";
	if (!write_all(line.c_str(), line.size())) return false;

	const uint64_t deadline_ms = monotonic_ms() + std::max<uint64_t>(60000, slot.blocks * 150);
	if (!raw_read_line_until(line, deadline_ms)) return false;
	if (line.rfind("ERR:", 0) == 0) {
		return false;
	}

	unsigned port = 0, start = 0, count = 0, block_size = 0, len = 0;
	if (sscanf(line.c_str(), "CARD GBSAVERAW P=%u START=%u COUNT=%u BLOCK_SIZE=%u LEN=%u",
	           &port, &start, &count, &block_size, &len) != 5 ||
	    port != slot.port ||
	    start != 0 ||
	    count != slot.blocks ||
	    block_size != slot.block_size ||
	    len != image_size) {
		printf("Serial memcard: GB save read failed for P%u: bad header.\n",
		       slot.port + 1);
		return false;
	}

	if (!raw_read_exact_until(image.data(), image_size, deadline_ms) ||
	    !raw_consume_line_ending_until(deadline_ms) ||
	    !raw_read_line_until(line, deadline_ms) ||
	    line != "OK:CARD_GBREADSAVERAW") {
		printf("Serial memcard: GB save read failed for P%u: missing completion.\n",
		       slot.port + 1);
		return false;
	}
	return true;
}

bool read_gb_save_range(const CardSlot& slot, std::vector<uint8_t>& image) {
	if (!slot.blocks || !slot.block_size) return false;

	pthread_mutex_lock(&serial_command_mutex);
	bool ok = ensure_serial_open();
	if (ok) {
		stop_serial_reader();
		ok = read_gb_save_range_open(slot, image);
	}
	if (ok) {
		if (!start_serial_reader()) {
			close_serial();
			ok = false;
		}
	} else {
		close_serial();
	}
	pthread_mutex_unlock(&serial_command_mutex);
	return ok;
}

bool query_gb_pak_identity(uint8_t port, GbPakIdentity& identity) {
	char command[64];
	snprintf(command, sizeof(command), "CARD GBINFO %u", port);
	std::vector<std::string> lines;
	if (!send_command(command, "OK:CARD_GBINFO", &lines, 12000)) return false;
	for (const auto& line : lines) {
		GbPakIdentity parsed;
		if (!parse_gb_info_line(line, parsed)) continue;
		identity = parsed;
		return identity.valid;
	}
	return false;
}

bool read_image_blocks(const CardSlot& slot, std::vector<uint8_t>& image) {
	if (!slot.blocks || !slot.block_size) return false;
	image.assign((size_t)slot.blocks * slot.block_size, 0);
	std::vector<uint8_t> block_data;
	for (uint16_t block = 0; block < slot.blocks; ++block) {
		if (!read_block(slot, block, block_data)) {
			printf("Serial memcard: single-block read failed for %s P%u S%u at block %u.\n",
			       card_kind_label(slot.kind), slot.port + 1, slot.slot, block);
			return false;
		}
		memcpy(image.data() + (size_t)block * slot.block_size, block_data.data(), slot.block_size);
	}
	return true;
}

bool read_image(const CardSlot& slot, std::vector<uint8_t>& image) {
	if (slot.kind == CardKind::GBPak) {
		return read_gb_save_range(slot, image);
	}

	if (read_image_range(slot, image)) return true;
	return read_image_blocks(slot, image);
}

bool write_blocks_with_fallback(const CardSlot& slot, uint16_t block, const uint8_t* data,
                                uint16_t block_count) {
	if (slot.kind == CardKind::GBPak) {
		if (write_gb_save_blocks_range(slot, block, data, block_count)) return true;
		printf("Serial memcard: GB save range write failed for P%u (%u+%u).\n",
		       slot.port + 1, block, block_count);
		return false;
	}

	if (write_blocks_range(slot, block, data, block_count)) return true;

	for (uint16_t i = 0; i < block_count; ++i) {
		const uint16_t fallback_block = block + i;
		const uint8_t* block_data = data + (size_t)i * slot.block_size;
		if (!write_block(slot, fallback_block, block_data)) {
			printf("Serial memcard: single-block write failed for %s P%u S%u at block %u.\n",
			       card_kind_label(slot.kind), slot.port + 1, slot.slot, fallback_block);
			return false;
		}
	}
	return true;
}

bool flush_mounted_card_locked(unsigned char index) {
	if (index >= 16 || !mounted_cards[index].active) return true;

	MountedCard& mounted = mounted_cards[index];
	const CardSlot& slot = mounted.slot;
	if (!card_kind_writable(slot.kind)) return true;
	if (!slot.block_size || mounted.dirty.empty()) return true;

	timespec started = {};
	clock_gettime(CLOCK_MONOTONIC, &started);
	uint16_t dirty_blocks = 0;
	bool ok = true;
	for (uint16_t block = 0; block < mounted.dirty.size();) {
		if (!mounted.dirty[block]) {
			block++;
			continue;
		}

		const uint16_t run_start = block;
		while (block < mounted.dirty.size() && mounted.dirty[block]) block++;
		const uint16_t run_count = block - run_start;
		const uint8_t* run_data = mounted.image.data() + (size_t)run_start * slot.block_size;

		if (!write_blocks_with_fallback(slot, run_start, run_data, run_count)) {
			ok = false;
			break;
		}

		for (uint16_t i = 0; i < run_count; ++i) {
			mounted.dirty[run_start + i] = 0;
		}
		dirty_blocks += run_count;
	}

	if (dirty_blocks) {
		const uint64_t elapsed_ms = elapsed_ms_since(started);
		printf("Serial memcard: flushed %s P%u S%u (%u dirty blocks, image index %u) in %llu.%03llus.\n",
		       card_kind_label(slot.kind),
		       slot.port + 1,
		       slot.slot,
		       dirty_blocks,
		       index,
		       (unsigned long long)(elapsed_ms / 1000),
		       (unsigned long long)(elapsed_ms % 1000));
		fflush(stdout);
	}
	return ok;
}

void flush_all_mounted_cards() {
	pthread_mutex_lock(&serial_mount_mutex);
	for (unsigned char index = 0; index < 16; ++index) {
		flush_mounted_card_locked(index);
	}
	pthread_mutex_unlock(&serial_mount_mutex);
}

void* serial_flush_main(void*) {
	for (;;) {
		usleep(kDirtyFlushDebounceUs);

		pthread_mutex_lock(&serial_flush_mutex);
		serial_flush_requested = false;
		pthread_mutex_unlock(&serial_flush_mutex);

		flush_all_mounted_cards();

		pthread_mutex_lock(&serial_flush_mutex);
		if (!serial_flush_requested) {
			serial_flush_running = false;
			pthread_mutex_unlock(&serial_flush_mutex);
			return nullptr;
		}
		pthread_mutex_unlock(&serial_flush_mutex);
	}
}

void request_dirty_flush() {
	pthread_mutex_lock(&serial_flush_mutex);
	serial_flush_requested = true;
	if (!serial_flush_running) {
		pthread_t thread = {};
		serial_flush_running = true;
		if (pthread_create(&thread, nullptr, serial_flush_main, nullptr) == 0) {
			pthread_detach(thread);
		} else {
			serial_flush_running = false;
		}
	}
	pthread_mutex_unlock(&serial_flush_mutex);
}

bool save_image_file(const char* path, const std::vector<uint8_t>& image) {
	create_path(SAVE_DIR, "SerialMemcard");
	if (FileSave(path, (void*)image.data(), (int)image.size()) != (int)image.size()) {
		printf("Serial memcard: failed to write cache image %s.\n", path);
		return false;
	}
	return true;
}

void make_cache_path(const CardSlot& selected, const char* extension, const char* suffix,
                     const char* label, char* out_path, size_t out_len) {
	const char* family =
		(selected.kind == CardKind::PSX) ? "psx" :
		(selected.kind == CardKind::GBPak) ? "gb" :
		"n64";
	if (selected.kind == CardKind::GBPak && label && *label) {
		snprintf(out_path, out_len, SAVE_DIR"/SerialMemcard/serial_%s_%s_p%u_s%u%s.%s",
		         family, label, selected.port + 1, selected.slot, suffix ? suffix : "", extension);
		return;
	}
	snprintf(out_path, out_len, SAVE_DIR"/SerialMemcard/serial_%s_p%u_s%u%s.%s",
	         family, selected.port + 1, selected.slot, suffix ? suffix : "", extension);
}

void remember_pending_mount(const char* path, const CardSlot& slot, const std::vector<uint8_t>& image,
                            const char* label = nullptr, uint64_t read_elapsed_ms = 0) {
	pthread_mutex_lock(&serial_mount_mutex);
	for (auto& pending : pending_mounts) {
		if (!pending.active || !same_card_slot(pending.slot, slot)) continue;
		pending.path = path;
		pending.label = label ? label : "";
		pending.image = image;
		pending.read_elapsed_ms = read_elapsed_ms;
		pthread_mutex_unlock(&serial_mount_mutex);
		return;
	}
	for (auto& pending : pending_mounts) {
		if (!pending.active) {
			pending.active = true;
			pending.path = path;
			pending.label = label ? label : "";
			pending.slot = slot;
			pending.image = image;
			pending.read_elapsed_ms = read_elapsed_ms;
			pthread_mutex_unlock(&serial_mount_mutex);
			return;
		}
	}
	pthread_mutex_unlock(&serial_mount_mutex);
}

void clear_reusable_card_locked(const CardSlot& slot) {
	for (auto& cached : reusable_cards) {
		if (cached.active && same_card_slot(cached.slot, slot)) cached = MountedCard{};
	}
}

void remember_reusable_card_locked(const MountedCard& mounted) {
	if (!mounted.active || mounted.image.empty()) return;
	clear_reusable_card_locked(mounted.slot);
	for (auto& cached : reusable_cards) {
		if (cached.active) continue;
		cached.active = true;
		cached.path = mounted.path;
		cached.slot = mounted.slot;
		cached.image = mounted.image;
		cached.dirty.clear();
		return;
	}
}

bool find_pending_mount(const CardSlot& slot, char* out_path, size_t out_len) {
	pthread_mutex_lock(&serial_mount_mutex);
	for (const auto& pending : pending_mounts) {
		if (!pending.active || !same_card_slot(pending.slot, slot)) continue;
		if (out_path && out_len) snprintf(out_path, out_len, "%s", pending.path.c_str());
		pthread_mutex_unlock(&serial_mount_mutex);
		return true;
	}
	pthread_mutex_unlock(&serial_mount_mutex);
	return false;
}

bool remember_mounted_as_pending(const CardSlot& slot, const char* extension, char* out_path, size_t out_len) {
	std::vector<uint8_t> image;
	std::string path;

	pthread_mutex_lock(&serial_mount_mutex);
	for (const auto& mounted : mounted_cards) {
		if (!mounted.active || !same_card_slot(mounted.slot, slot)) continue;
		image = mounted.image;
		path = mounted.path;
		break;
	}
	if (image.empty()) {
		for (const auto& cached : reusable_cards) {
			if (!cached.active || !same_card_slot(cached.slot, slot)) continue;
			image = cached.image;
			path = cached.path;
			break;
		}
	}
	pthread_mutex_unlock(&serial_mount_mutex);

	if (image.empty()) return false;
	if (path.empty()) {
		char generated[1024] = {};
		make_cache_path(slot, extension, "", nullptr, generated, sizeof(generated));
		path = generated;
	}

	if (!save_image_file(path.c_str(), image)) return false;
	remember_pending_mount(path.c_str(), slot, image, nullptr, 0);
	if (out_path && out_len) snprintf(out_path, out_len, "%s", path.c_str());
	return true;
}

struct MountedCompare {
	bool active = false;
	bool dirty = false;
	bool same = false;
	unsigned char index = 0;
	uint32_t crc = 0;
};

MountedCompare compare_mounted_card(const CardSlot& slot, const std::vector<uint8_t>& image) {
	MountedCompare result;
	pthread_mutex_lock(&serial_mount_mutex);
	for (unsigned char index = 0; index < 16; ++index) {
		const MountedCard& mounted = mounted_cards[index];
		if (!mounted.active || !same_card_slot(mounted.slot, slot)) continue;
		result.active = true;
		result.index = index;
		result.dirty = std::any_of(mounted.dirty.begin(), mounted.dirty.end(),
		                           [](uint8_t dirty) { return dirty != 0; });
		result.crc = image_crc32(mounted.image);
		result.same = mounted.image.size() == image.size() &&
		              !memcmp(mounted.image.data(), image.data(), image.size());
		break;
	}
	pthread_mutex_unlock(&serial_mount_mutex);
	return result;
}

void wait_for_n64_prefetch() {
	pthread_mutex_lock(&serial_prefetch_mutex);
	while (n64_prefetch_running) {
		pthread_cond_wait(&serial_prefetch_cond, &serial_prefetch_mutex);
	}
	pthread_mutex_unlock(&serial_prefetch_mutex);
}

bool prepare_selected_slot_with_label(const CardSlot& selected, const char* extension,
                                      const char* label, char* out_path, size_t out_len) {
	std::vector<uint8_t> image;
	timespec started = {};
	clock_gettime(CLOCK_MONOTONIC, &started);
	if (!read_image(selected, image)) return false;
	const uint64_t elapsed_ms = elapsed_ms_since(started);

	make_cache_path(selected, extension, "", label, out_path, out_len);
	if (!save_image_file(out_path, image)) return false;
	remember_pending_mount(out_path, selected, image, label, elapsed_ms);
	return true;
}

bool prepare_selected_slot(const CardSlot& selected, const char* extension, char* out_path, size_t out_len) {
	return prepare_selected_slot_with_label(selected, extension, nullptr, out_path, out_len);
}

bool prepare_selected_slot_hotplug(const CardSlot& selected, const char* extension) {
	std::vector<uint8_t> image;
	timespec started = {};
	clock_gettime(CLOCK_MONOTONIC, &started);
	if (!read_image(selected, image)) return false;
	const uint64_t elapsed_ms = elapsed_ms_since(started);

	const uint32_t physical_crc = image_crc32(image);
	MountedCompare mounted = compare_mounted_card(selected, image);
	char path[1024] = {};

	if (mounted.active) {
		if (mounted.same) {
			return true;
		}

		make_cache_path(selected, extension, "_hotplug", nullptr, path, sizeof(path));
		if (!save_image_file(path, image)) return false;
		printf("Serial memcard: hotplug %s P%u S%u differs from mounted image index %u%s (card crc32 %08X, mounted crc32 %08X); cached -> %s, keeping mounted image active.\n",
		       card_kind_label(selected.kind), selected.port + 1, selected.slot,
		       mounted.index,
		       mounted.dirty ? " with dirty blocks" : "",
		       physical_crc,
		       mounted.crc,
		       path);
		fflush(stdout);
		return true;
	}

	make_cache_path(selected, extension, "", nullptr, path, sizeof(path));
	if (!save_image_file(path, image)) return false;
	remember_pending_mount(path, selected, image, nullptr, elapsed_ms);
	return true;
}

bool prepare_card_slot(CardKind kind, uint8_t port, uint8_t physical_slot, const char* extension,
                       char* out_path, size_t out_len) {
	if (!serial_memcard_enabled()) return false;
	auto slots = scan_slots(false);
	CardSlot selected;
	bool found = false;
	for (const auto& slot : slots) {
		if (slot.kind != kind) continue;
		if (slot.port == port && slot.slot == physical_slot) {
			selected = slot;
			found = true;
			break;
		}
	}
	if (!found) return false;

	if (card_kind_uses_n64_prefetch(kind)) wait_for_n64_prefetch();
	if (find_pending_mount(selected, out_path, out_len)) return true;
	if (remember_mounted_as_pending(selected, extension, out_path, out_len)) return true;
	return prepare_selected_slot(selected, extension, out_path, out_len);
}

bool prepare_card_ordinal(CardKind kind, uint8_t ordinal, const char* extension,
                          char* out_path, size_t out_len) {
	if (!serial_memcard_enabled()) return false;
	if (card_kind_uses_n64_prefetch(kind)) wait_for_n64_prefetch();

	auto slots = (kind == CardKind::N64 && ordinal == 0) ?
		scan_slots_retry_for_kind(kind, 3, kGbPakScanRetryDelayUs) :
		scan_slots(false);
	uint8_t seen = 0;
	for (const auto& slot : slots) {
		if (slot.kind != kind) continue;
		if (seen++ != ordinal) continue;
		if (find_pending_mount(slot, out_path, out_len)) return true;
		if (remember_mounted_as_pending(slot, extension, out_path, out_len)) return true;
		return prepare_selected_slot(slot, extension, out_path, out_len);
	}
	return false;
}

bool prepare_gbpak_for_rom(const char* rom_path, char* out_path, size_t out_len) {
	if (!serial_memcard_enabled()) {
		printf("Serial memcard: disabled; not attaching physical Transfer Pak save.\n");
		return false;
	}

	GbPakIdentity rom_identity;
	if (!read_gb_rom_identity_from_file(rom_path, rom_identity)) {
		printf("Serial memcard: loaded GB/GBC ROM identity unavailable; not attaching physical Transfer Pak save.\n");
		return false;
	}

	wait_for_n64_prefetch();
	auto slots = scan_slots_retry_for_kind(CardKind::GBPak, 3, kGbPakScanRetryDelayUs);
	bool found_gbpak = false;
	for (const auto& slot : slots) {
		if (slot.kind != CardKind::GBPak) continue;
		found_gbpak = true;

		GbPakIdentity cart_identity;
		if (!query_gb_pak_identity(slot.port, cart_identity)) {
			printf("Serial memcard: Transfer Pak P%u identity unavailable; not attaching physical save.\n",
			       slot.port + 1);
			continue;
		}
		if (!gb_identity_matches(cart_identity, rom_identity)) {
			printf("Serial memcard: Transfer Pak cart %s/%04X does not match loaded GB/GBC ROM %s/%04X.\n",
			       cart_identity.title_key.c_str(), cart_identity.global_checksum,
			       rom_identity.title_key.c_str(), rom_identity.global_checksum);
			continue;
		}

		std::string label = safe_cache_component(base_name_without_extension(rom_path));
		if (label.empty()) {
			label = safe_cache_component(cart_identity.title.empty() ?
			                             cart_identity.title_key : cart_identity.title);
		}
		return prepare_selected_slot_with_label(slot, "sav", label.c_str(), out_path, out_len);
	}

	if (!found_gbpak) {
		printf("Serial memcard: no Transfer Pak cartridge slot found for loaded GB/GBC ROM %s/%04X.\n",
		       rom_identity.title_key.c_str(), rom_identity.global_checksum);
	}
	else {
		printf("Serial memcard: no matching Transfer Pak cartridge found for loaded GB/GBC ROM %s/%04X.\n",
		       rom_identity.title_key.c_str(), rom_identity.global_checksum);
	}
	return false;
}

uint32_t allocate_async_token_locked() {
	if (!serial_async_next_token) serial_async_next_token = 1;
	return serial_async_next_token++;
}

void erase_async_entries_locked(unsigned char index) {
	for (auto it = serial_async_requests.begin(); it != serial_async_requests.end();) {
		if (it->index == index || (it->auto_select && it->index2 == index)) it = serial_async_requests.erase(it);
		else ++it;
	}
	for (auto it = serial_async_results.begin(); it != serial_async_results.end();) {
		if (it->index == index) it = serial_async_results.erase(it);
		else ++it;
	}
}

void push_async_result(unsigned char index, uint32_t token, bool success, const char* path) {
	AsyncResult result;
	result.success = success;
	result.done = true;
	result.index = index;
	result.token = token;
	if (path) result.path = path;
	pthread_mutex_lock(&serial_async_mutex);
	serial_async_results.push_back(result);
	pthread_mutex_unlock(&serial_async_mutex);
}

void push_async_result(const AsyncRequest& request, bool success, const char* path) {
	push_async_result(request.index, request.token, success, path);
}

void prepare_psx_auto_async_result(const AsyncRequest& request) {
	auto slots = scan_slots(false);
	std::vector<CardSlot> psx_slots;
	for (const auto& slot : slots) {
		if (slot.kind == CardKind::PSX) psx_slots.push_back(slot);
	}

	if (psx_slots.size() > 2) {
		printf("Serial memcard: %u PSX cards present; using first 2 by physical order.\n",
		       (unsigned)psx_slots.size());
	}

	const unsigned char indexes[] = { request.index, request.index2 };
	for (size_t i = 0; i < sizeof(indexes) / sizeof(indexes[0]); ++i) {
		char path[1024] = {};
		const bool success = i < psx_slots.size() &&
			prepare_selected_slot(psx_slots[i], "mcd", path, sizeof(path));
		push_async_result(indexes[i], request.token, success, success ? path : nullptr);
	}
}

void* serial_async_main(void*) {
	for (;;) {
		pthread_mutex_lock(&serial_async_mutex);
		if (serial_async_requests.empty()) {
			serial_async_running = false;
			pthread_mutex_unlock(&serial_async_mutex);
			return nullptr;
		}
		std::vector<AsyncRequest> requests = serial_async_requests;
		serial_async_requests.clear();
		pthread_mutex_unlock(&serial_async_mutex);

		for (const auto& request : requests) {
			if (request.auto_select && request.kind == CardKind::PSX) {
				prepare_psx_auto_async_result(request);
				continue;
			}

			char path[1024] = {};
			bool success = false;
			if (request.kind == CardKind::PSX) {
				if (request.by_ordinal) {
					success = prepare_card_ordinal(CardKind::PSX, request.ordinal,
					                               "mcd", path, sizeof(path));
				} else {
					success = prepare_card_slot(CardKind::PSX, request.port, request.slot,
					                            "mcd", path, sizeof(path));
				}
			}
			push_async_result(request, success, success ? path : nullptr);
		}
	}
}

void start_serial_async_worker_locked() {
	if (serial_async_running || serial_async_requests.empty()) return;

	pthread_t thread = {};
	serial_async_running = true;
	if (pthread_create(&thread, nullptr, serial_async_main, nullptr) == 0) {
		pthread_detach(thread);
		return;
	}
	serial_async_running = false;
	for (const auto& request : serial_async_requests) {
		AsyncResult result;
		result.done = true;
		result.success = false;
		result.index = request.index;
		result.token = request.token;
		serial_async_results.push_back(result);
		if (request.auto_select) {
			result.index = request.index2;
			serial_async_results.push_back(result);
		}
	}
	serial_async_requests.clear();
}

void* n64_prefetch_main(void* arg) {
	const bool hotplug = arg != nullptr;
	invalidate_slot_cache();
	auto slots = scan_slots(false);
	unsigned prepared_cpak = 0;
	unsigned prepared_gb = 0;
	for (const auto& slot : slots) {
		if (slot.kind != CardKind::N64 && slot.kind != CardKind::GBPak) continue;
		if (slot.kind == CardKind::N64 && prepared_cpak >= 4) continue;
		if (slot.kind == CardKind::GBPak && prepared_gb >= 1) continue;
		char path[1024] = {};
		const char* extension = (slot.kind == CardKind::GBPak) ? "sav" : "cpk";
		if (hotplug ? prepare_selected_slot_hotplug(slot, extension) :
		              prepare_selected_slot(slot, extension, path, sizeof(path))) {
			if (slot.kind == CardKind::GBPak) prepared_gb++;
			else prepared_cpak++;
		}
		if (prepared_cpak >= 4 && prepared_gb >= 1) break;
	}

	pthread_mutex_lock(&serial_prefetch_mutex);
	n64_prefetch_running = false;
	pthread_cond_broadcast(&serial_prefetch_cond);
	pthread_mutex_unlock(&serial_prefetch_mutex);
	return nullptr;
}

bool start_n64_prefetch(bool hotplug) {
	if (!serial_memcard_enabled()) return false;

	pthread_mutex_lock(&serial_prefetch_mutex);
	if (n64_prefetch_running) {
		pthread_mutex_unlock(&serial_prefetch_mutex);
		return true;
	}

	pthread_t thread = {};
	n64_prefetch_running = true;
	if (pthread_create(&thread, nullptr, n64_prefetch_main, hotplug ? (void*)1 : nullptr) == 0) {
		pthread_detach(thread);
		pthread_mutex_unlock(&serial_prefetch_mutex);
		return true;
	}

	n64_prefetch_running = false;
	pthread_cond_broadcast(&serial_prefetch_cond);
	pthread_mutex_unlock(&serial_prefetch_mutex);
	return false;
}

void prefetch_psx_hotplug() {
	invalidate_slot_cache();
	auto slots = scan_slots(false);
	unsigned prepared = 0;
	for (const auto& slot : slots) {
		if (slot.kind != CardKind::PSX) continue;
		if (prepare_selected_slot_hotplug(slot, "mcd")) prepared++;
		if (prepared >= 4) break;
	}
}

void* rescan_main(void*) {
	for (;;) {
		pthread_mutex_lock(&serial_rescan_mutex);
		const uint32_t generation = serial_rescan_generation;
		pthread_mutex_unlock(&serial_rescan_mutex);

		usleep(kHotplugRescanDebounceUs);

		pthread_mutex_lock(&serial_rescan_mutex);
		const bool settled = generation == serial_rescan_generation;
		pthread_mutex_unlock(&serial_rescan_mutex);
		if (!settled) continue;

		invalidate_device_cache();
		if (serial_rx_fd >= 0 || !cached_serial_memcard_port_candidates().empty()) {
			if (is_n64()) {
				start_n64_prefetch(true);
			} else if (is_psx()) {
				prefetch_psx_hotplug();
			} else {
				invalidate_slot_cache();
				scan_slots();
			}
		}

		pthread_mutex_lock(&serial_rescan_mutex);
		if (generation == serial_rescan_generation) {
			serial_rescan_running = false;
			pthread_mutex_unlock(&serial_rescan_mutex);
			return nullptr;
		}
		pthread_mutex_unlock(&serial_rescan_mutex);
	}
}

} // namespace

bool serial_memcard_enabled() {
	return serial_rx_fd >= 0 || !cached_serial_memcard_port_candidates().empty();
}

void serial_memcard_invalidate_scan_cache() {
	invalidate_slot_cache();
	invalidate_device_cache();
}

void serial_memcard_rescan_async() {
	pthread_mutex_lock(&serial_rescan_mutex);
	invalidate_device_cache();
	serial_rescan_generation++;
	if (serial_rescan_running) {
		pthread_mutex_unlock(&serial_rescan_mutex);
		return;
	}

	serial_rescan_running = true;
	pthread_t thread = {};
	if (pthread_create(&thread, nullptr, rescan_main, nullptr) == 0) {
		pthread_detach(thread);
		pthread_mutex_unlock(&serial_rescan_mutex);
		return;
	}

	serial_rescan_running = false;
	pthread_mutex_unlock(&serial_rescan_mutex);
}

bool serial_memcard_psx_slots_scanned() {
	pthread_mutex_lock(&serial_slot_cache_mutex);
	const bool scanned = cached_psx_slots_scanned;
	pthread_mutex_unlock(&serial_slot_cache_mutex);
	return scanned;
}

uint8_t serial_memcard_psx_slot_mask() {
	pthread_mutex_lock(&serial_slot_cache_mutex);
	const uint8_t mask = cached_psx_slot_mask;
	pthread_mutex_unlock(&serial_slot_cache_mutex);
	return mask;
}

bool serial_memcard_prepare_psx(uint8_t port, uint8_t slot, char* out_path, size_t out_len) {
	return prepare_card_slot(CardKind::PSX, port, slot, "mcd", out_path, out_len);
}

uint32_t serial_memcard_prepare_psx_async(uint8_t port, uint8_t slot, unsigned char index) {
	pthread_mutex_lock(&serial_async_mutex);
	erase_async_entries_locked(index);

	AsyncRequest request;
	request.kind = CardKind::PSX;
	request.port = port;
	request.slot = slot;
	request.index = index;
	request.token = allocate_async_token_locked();
	const uint32_t token = request.token;
	serial_async_requests.push_back(request);
	start_serial_async_worker_locked();
	pthread_mutex_unlock(&serial_async_mutex);
	return token;
}

uint32_t serial_memcard_prepare_psx_ordinal_async(uint8_t ordinal, unsigned char index) {
	pthread_mutex_lock(&serial_async_mutex);
	erase_async_entries_locked(index);

	AsyncRequest request;
	request.kind = CardKind::PSX;
	request.by_ordinal = true;
	request.ordinal = ordinal;
	request.index = index;
	request.token = allocate_async_token_locked();
	const uint32_t token = request.token;
	serial_async_requests.push_back(request);
	start_serial_async_worker_locked();
	pthread_mutex_unlock(&serial_async_mutex);
	return token;
}

uint32_t serial_memcard_prepare_psx_auto_async(unsigned char index1, unsigned char index2) {
	pthread_mutex_lock(&serial_async_mutex);
	erase_async_entries_locked(index1);
	erase_async_entries_locked(index2);

	AsyncRequest request;
	request.kind = CardKind::PSX;
	request.auto_select = true;
	request.index = index1;
	request.index2 = index2;
	request.token = allocate_async_token_locked();
	const uint32_t token = request.token;
	serial_async_requests.push_back(request);
	start_serial_async_worker_locked();
	pthread_mutex_unlock(&serial_async_mutex);
	return token;
}

bool serial_memcard_take_async_mount(unsigned char index, uint32_t token, char* out_path, size_t out_len, bool* done) {
	if (done) *done = false;
	pthread_mutex_lock(&serial_async_mutex);
	for (auto it = serial_async_results.begin(); it != serial_async_results.end();) {
		if (it->index != index) {
			++it;
			continue;
		}
		if (it->token != token) {
			it = serial_async_results.erase(it);
			continue;
		}
		if (done) *done = it->done;
		const bool success = it->success;
		if (success && out_path && out_len) {
			snprintf(out_path, out_len, "%s", it->path.c_str());
		}
		serial_async_results.erase(it);
		pthread_mutex_unlock(&serial_async_mutex);
		return success;
	}
	pthread_mutex_unlock(&serial_async_mutex);
	return false;
}

void serial_memcard_cancel_async_mount(unsigned char index) {
	pthread_mutex_lock(&serial_async_mutex);
	erase_async_entries_locked(index);
	pthread_mutex_unlock(&serial_async_mutex);
}

bool serial_memcard_prepare_n64_cpak(uint8_t cpak_index, char* out_path, size_t out_len) {
	return prepare_card_ordinal(CardKind::N64, cpak_index, "cpk", out_path, out_len);
}

bool serial_memcard_prepare_n64_tpak(char* out_path, size_t out_len) {
	return prepare_card_ordinal(CardKind::GBPak, 0, "sav", out_path, out_len);
}

bool serial_memcard_prepare_n64_tpak_for_rom(const char* rom_path, char* out_path, size_t out_len) {
	return prepare_gbpak_for_rom(rom_path, out_path, out_len);
}

bool serial_memcard_prepare_gb_save_for_rom(const char* rom_path, char* out_path, size_t out_len) {
	return prepare_gbpak_for_rom(rom_path, out_path, out_len);
}

bool serial_memcard_prefetch_n64_cpak() {
	return start_n64_prefetch(false);
}

bool serial_memcard_attach_mount(const char* path, unsigned char index) {
	if (!path || index >= 16) return false;
	pthread_mutex_lock(&serial_mount_mutex);
	if (mounted_cards[index].active) {
		flush_mounted_card_locked(index);
		remember_reusable_card_locked(mounted_cards[index]);
		mounted_cards[index] = MountedCard{};
	}
	for (auto& pending : pending_mounts) {
		if (!pending.active || pending.path != path) continue;
		const CardSlot slot = pending.slot;
		const size_t image_size = pending.image.size();
		const uint64_t elapsed_ms = pending.read_elapsed_ms;
		const std::string mount_path = pending.path;
		char info[256] = {};
		char summary[64] = {};
		format_serial_mount_info(pending, info, sizeof(info));
		format_serial_mount_info_summary(pending, summary, sizeof(summary));
		mounted_cards[index].active = true;
		mounted_cards[index].path = mount_path;
		mounted_cards[index].slot = slot;
		mounted_cards[index].image = pending.image;
		mounted_cards[index].dirty.assign(slot.blocks, 0);
		clear_reusable_card_locked(slot);
		pending = PendingMount{};
		remember_serial_mount_info(info, summary);
		printf("Serial memcard: mounted %s P%u S%u -> image %u (%u bytes, %llu.%03llus, %s).\n",
		       card_kind_label(slot.kind),
		       slot.port + 1,
		       slot.slot,
		       index,
		       (unsigned)image_size,
		       (unsigned long long)(elapsed_ms / 1000),
		       (unsigned long long)(elapsed_ms % 1000),
		       mount_path.c_str());
		fflush(stdout);
		pthread_mutex_unlock(&serial_mount_mutex);
		return true;
	}
	pthread_mutex_unlock(&serial_mount_mutex);
	return false;
}

bool serial_memcard_take_mount_info(char* out, size_t out_len) {
	if (!out || !out_len) return false;
	pthread_mutex_lock(&serial_mount_info_mutex);
	if (serial_mount_infos.empty()) {
		pthread_mutex_unlock(&serial_mount_info_mutex);
		out[0] = 0;
		return false;
	}
	snprintf(out, out_len, "%s", serial_mount_infos.front().popup.c_str());
	serial_mount_infos.erase(serial_mount_infos.begin());
	pthread_mutex_unlock(&serial_mount_info_mutex);
	return true;
}

bool serial_memcard_take_mount_info_summary(char* out, size_t out_len) {
	if (!out || !out_len) return false;
	pthread_mutex_lock(&serial_mount_info_mutex);
	if (serial_mount_infos.empty()) {
		pthread_mutex_unlock(&serial_mount_info_mutex);
		out[0] = 0;
		return false;
	}

	size_t len = 0;
	out[0] = 0;
	for (const auto& info : serial_mount_infos) {
		if (info.summary.empty()) continue;
		const int written = snprintf(out + len, out_len - len, "%s%s",
		                             len ? "\n" : "",
		                             info.summary.c_str());
		if (written < 0) break;
		if ((size_t)written >= out_len - len) {
			len = out_len - 1;
			break;
		}
		len += (size_t)written;
	}
	serial_mount_infos.clear();
	pthread_mutex_unlock(&serial_mount_info_mutex);
	return out[0] != 0;
}

void serial_memcard_unmount(unsigned char index) {
	if (index >= 16) return;
	pthread_mutex_lock(&serial_mount_mutex);
	flush_mounted_card_locked(index);
	remember_reusable_card_locked(mounted_cards[index]);
	mounted_cards[index] = MountedCard{};
	pthread_mutex_unlock(&serial_mount_mutex);
}

bool serial_memcard_handle_write(unsigned char index, uint64_t offset,
                                     const uint8_t* data, uint32_t len) {
	pthread_mutex_lock(&serial_mount_mutex);
	if (index >= 16 || !mounted_cards[index].active || !data || !len) {
		pthread_mutex_unlock(&serial_mount_mutex);
		return false;
	}
	MountedCard& mounted = mounted_cards[index];
	const CardSlot& slot = mounted.slot;
	if (!card_kind_writable(slot.kind)) {
		pthread_mutex_unlock(&serial_mount_mutex);
		return false;
	}
	if (!slot.block_size || offset >= mounted.image.size()) {
		pthread_mutex_unlock(&serial_mount_mutex);
		return false;
	}
	if (len > mounted.image.size() - offset) {
		len = (uint32_t)(mounted.image.size() - offset);
	}

	const uint16_t first_block = (uint16_t)(offset / slot.block_size);
	const uint16_t last_block = (uint16_t)((offset + len - 1) / slot.block_size);
	uint16_t changed_blocks = 0;
	if (mounted.dirty.size() < slot.blocks) mounted.dirty.assign(slot.blocks, 0);
	std::vector<uint8_t> block_image(slot.block_size);

	for (uint16_t block = first_block; block <= last_block; ++block) {
		const size_t block_offset = (size_t)block * slot.block_size;
		const uint64_t block_end = block_offset + slot.block_size;
		const uint64_t write_end = offset + len;
		const uint64_t copy_start = std::max<uint64_t>(offset, block_offset);
		const uint64_t copy_end = std::min<uint64_t>(write_end, block_end);

		memcpy(block_image.data(), mounted.image.data() + block_offset, slot.block_size);
		memcpy(block_image.data() + (copy_start - block_offset),
		       data + (copy_start - offset),
		       copy_end - copy_start);

		if (!memcmp(mounted.image.data() + block_offset, block_image.data(), slot.block_size)) {
			continue;
		}

		memcpy(mounted.image.data() + block_offset, block_image.data(), slot.block_size);
		mounted.dirty[block] = 1;
		changed_blocks++;
	}

	pthread_mutex_unlock(&serial_mount_mutex);
	if (changed_blocks) request_dirty_flush();
	return true;
}
