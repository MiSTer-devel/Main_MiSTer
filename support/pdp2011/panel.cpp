#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include "panel.h"
#include "../../hardware.h"
#include "../../spi.h"
#include "../../user_io.h"

#define ODT_SOCK "/tmp/pdp2011.odt"

static void oct6(char *dst, unsigned v)
{
	for (int i = 5; i >= 0; i--) {
		dst[i] = '0' + (v & 7);
		v >>= 3;
	}
	dst[6] = 0;
}

struct panel_snap {
	uint16_t r7, psw, ir, flags, data, addr;
};

static void panel_read(struct panel_snap *s)
{
	spi_uio_cmd_cont(UIO_PDP_PANEL);
	s->r7 = spi_w(0);
	s->psw = spi_w(0);
	s->ir = spi_w(0);
	s->flags = spi_w(0);
	s->data = 0;
	s->addr = 0;
	if (s->flags & 8) {
		s->data = spi_w(0);
		s->addr = spi_w(0);
	}
	DisableIO();
}

int pdp2011_panel_banner(char lines[2][32], int force)
{
	static unsigned long next;
	static char cache[2][32];
	static int cached;

	if (!force && cached && next && !CheckTimer(next)) {
		snprintf(lines[0], 32, "%s", cache[0]);
		snprintf(lines[1], 32, "%s", cache[1]);
		return 0;
	}

	struct panel_snap s;
	panel_read(&s);

	next = GetTimer(50);

	char line0[32], line1[32];
	if (s.flags & 1) {
		char pc[7], sw[7], star[7];
		oct6(pc, s.r7);
		oct6(sw, s.psw);
		oct6(star, s.ir);
		snprintf(line0, sizeof(line0), " PC %s PSW %s  %s",
			pc, sw, (s.flags & 0x8000) ? " RUN" : "HALT");
		if (s.flags & 8) {
			char ma[7], d[7];
			oct6(ma, s.addr);
			oct6(d, s.data);
			if (s.flags & 2)
				snprintf(line1, sizeof(line1),
					(s.flags & 4) ? "*PC %s MA %s D   NXM" : "*PC %s MA %s D %s",
					star, ma, d);
			else
				snprintf(line1, sizeof(line1),
					(s.flags & 4) ? "*PC ------ MA %s D   NXM" : "*PC ------ MA %s D %s",
					ma, d);
		} else if (s.flags & 2) {
			snprintf(line1, sizeof(line1), "*PC %s", star);
		} else {
			snprintf(line1, sizeof(line1), "*PC ------");
		}
	} else {
		snprintf(line0, sizeof(line0), " PC ------ PSW ------  ----");
		snprintf(line1, sizeof(line1), "*PC ------");
	}

	int changed = !cached || strcmp(cache[0], line0) || strcmp(cache[1], line1);
	snprintf(cache[0], sizeof(cache[0]), "%s", line0);
	snprintf(cache[1], sizeof(cache[1]), "%s", line1);
	cached = 1;
	snprintf(lines[0], 32, "%s", cache[0]);
	snprintf(lines[1], 32, "%s", cache[1]);
	return force || changed;
}

static void fmt_snap(char *out, size_t n, const struct panel_snap *s)
{
	char pc[7], psw[7], ir[7], ma[7], d[7];
	oct6(pc, s->r7);
	oct6(psw, s->psw);
	oct6(ir, s->ir);
	oct6(ma, s->addr);
	oct6(d, s->data);
	snprintf(out, n, "PC %s PSW %s *PC %s MA %s D %s %s%s\n",
		pc, psw, (s->flags & 2) ? ir : "------",
		ma, (s->flags & 4) ? "NXM   " : d,
		(s->flags & 0x8000) ? "RUN" : "HALT",
		(s->flags & 1) ? "" : " (no panel_dbg)");
}

static void set_sr(uint32_t v)
{
	int sr22 = (v & 017600000) ? 1 : 0;
	uint16_t lo = (uint16_t)(v & 0177777);
	user_io_status_set("[38]", sr22);
	user_io_status_set("[36]", (lo >> 15) & 1);
	user_io_status_set("[35:33]", (lo >> 12) & 7);
	user_io_status_set("[32:30]", (lo >> 9) & 7);
	user_io_status_set("[29:27]", (lo >> 6) & 7);
	user_io_status_set("[26:24]", (lo >> 3) & 7);
	user_io_status_set("[23:21]", lo & 7);
}

static void pulse(const char *opt)
{
	user_io_status_set(opt, 0);
	WaitTimer(2);
	user_io_status_set(opt, 1);
	WaitTimer(4);
	user_io_status_set(opt, 0);
	WaitTimer(4);
}

static void ensure_halt(void)
{
	user_io_status_set("[16]", 1);
	WaitTimer(4);
}

static int parse_oct(const char *s, uint32_t *out)
{
	char *end = 0;
	while (*s == ' ' || *s == '\t') s++;
	if (!*s) return 0;
	unsigned long v = strtoul(s, &end, 8);
	if (end == s) return 0;
	*out = (uint32_t)v;
	return 1;
}

static void odt_reply(int fd, const char *s)
{
	if (fd < 0 || !s) return;
	size_t n = strlen(s);
	if (n) (void)write(fd, s, n);
	(void)write(fd, ".\n", 2);
}

static void odt_cmd(int fd, char *line)
{
	char reply[256];
	struct panel_snap s;
	uint32_t a = 0, d = 0;
	char *cmd = line;
	while (*cmd == ' ' || *cmd == '\t') cmd++;
	char *arg = cmd;
	while (*arg && *arg != ' ' && *arg != '\t') arg++;
	if (*arg) {
		*arg++ = 0;
		while (*arg == ' ' || *arg == '\t') arg++;
	}

	if (!cmd[0] || !strcmp(cmd, "snap") || !strcmp(cmd, "?")) {
		panel_read(&s);
		fmt_snap(reply, sizeof(reply), &s);
		odt_reply(fd, reply);
		return;
	}
	if (!strcmp(cmd, "help")) {
		odt_reply(fd,
			"snap halt run cont start\n"
			"load <oct>  exa  dep <oct>  sr <oct>\n"
			"peek <oct>  poke <oct> <oct>\n"
			"r7 <oct>    (17600000 + 177707)\n");
		return;
	}
	if (!strcmp(cmd, "halt")) {
		user_io_status_set("[16]", 1);
		WaitTimer(4);
		panel_read(&s);
		fmt_snap(reply, sizeof(reply), &s);
		odt_reply(fd, reply);
		return;
	}
	if (!strcmp(cmd, "run") || !strcmp(cmd, "go")) {
		/* Leave state_halt with cons_ena=1 so Continue actually runs. */
		user_io_status_set("[16]", 0);
		WaitTimer(4);
		pulse("[17]");
		panel_read(&s);
		fmt_snap(reply, sizeof(reply), &s);
		odt_reply(fd, reply);
		return;
	}
	if (!strcmp(cmd, "cont") || !strcmp(cmd, "c")) {
		ensure_halt();
		pulse("[17]");
		panel_read(&s);
		fmt_snap(reply, sizeof(reply), &s);
		odt_reply(fd, reply);
		return;
	}
	if (!strcmp(cmd, "start")) {
		odt_reply(fd, "ERR start resets the CPU; use r7 + cont\n");
		return;
	}
	if (!strcmp(cmd, "sr")) {
		if (!parse_oct(arg, &a)) { odt_reply(fd, "ERR sr <oct>\n"); return; }
		set_sr(a);
		odt_reply(fd, "OK\n");
		return;
	}
	if (!strcmp(cmd, "load") || !strcmp(cmd, "l")) {
		if (!parse_oct(arg, &a)) { odt_reply(fd, "ERR load <oct>\n"); return; }
		ensure_halt();
		set_sr(a);
		pulse("[19]");
		panel_read(&s);
		fmt_snap(reply, sizeof(reply), &s);
		odt_reply(fd, reply);
		return;
	}
	if (!strcmp(cmd, "exa") || !strcmp(cmd, "e")) {
		ensure_halt();
		pulse("[20]");
		panel_read(&s);
		fmt_snap(reply, sizeof(reply), &s);
		odt_reply(fd, reply);
		return;
	}
	if (!strcmp(cmd, "dep") || !strcmp(cmd, "d")) {
		if (!parse_oct(arg, &a)) { odt_reply(fd, "ERR dep <oct>\n"); return; }
		ensure_halt();
		set_sr(a);
		pulse("[37]");
		panel_read(&s);
		fmt_snap(reply, sizeof(reply), &s);
		odt_reply(fd, reply);
		return;
	}
	if (!strcmp(cmd, "peek")) {
		if (!parse_oct(arg, &a)) { odt_reply(fd, "ERR peek <oct>\n"); return; }
		ensure_halt();
		set_sr(a);
		pulse("[19]");
		pulse("[20]");
		panel_read(&s);
		fmt_snap(reply, sizeof(reply), &s);
		odt_reply(fd, reply);
		return;
	}
	if (!strcmp(cmd, "poke")) {
		char *sp = arg;
		while (*sp && *sp != ' ' && *sp != '\t') sp++;
		if (*sp) *sp++ = 0;
		while (*sp == ' ' || *sp == '\t') sp++;
		if (!parse_oct(arg, &a) || !parse_oct(sp, &d)) {
			odt_reply(fd, "ERR poke <oct> <oct>\n");
			return;
		}
		ensure_halt();
		set_sr(a);
		pulse("[19]");
		set_sr(d);
		pulse("[37]");
		panel_read(&s);
		fmt_snap(reply, sizeof(reply), &s);
		odt_reply(fd, reply);
		return;
	}
	if (!strcmp(cmd, "r7")) {
		if (!parse_oct(arg, &a)) { odt_reply(fd, "ERR r7 <oct>\n"); return; }
		ensure_halt();
		set_sr(017600000 | 0177707);
		pulse("[19]");
		set_sr(a & 0177777);
		pulse("[37]");
		set_sr(0);
		panel_read(&s);
		fmt_snap(reply, sizeof(reply), &s);
		odt_reply(fd, reply);
		return;
	}

	snprintf(reply, sizeof(reply), "ERR unknown '%s'\n", cmd);
	odt_reply(fd, reply);
}

static int listen_fd = -1;
static int client_fd = -1;
static char inbuf[256];
static int inlen;

static void odt_close_client(void)
{
	if (client_fd >= 0) {
		close(client_fd);
		client_fd = -1;
	}
	inlen = 0;
}

static void odt_listen(void)
{
	if (listen_fd >= 0) return;
	unlink(ODT_SOCK);
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return;
	int fl = fcntl(fd, F_GETFL, 0);
	if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	struct sockaddr_un a;
	memset(&a, 0, sizeof(a));
	a.sun_family = AF_UNIX;
	strncpy(a.sun_path, ODT_SOCK, sizeof(a.sun_path) - 1);
	if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
		close(fd);
		return;
	}
	chmod(ODT_SOCK, 0666);
	if (listen(fd, 2) < 0) {
		close(fd);
		unlink(ODT_SOCK);
		return;
	}
	listen_fd = fd;
}

void pdp2011_odt_poll()
{
	if (!is_pdp2011()) {
		if (listen_fd >= 0) {
			odt_close_client();
			close(listen_fd);
			listen_fd = -1;
			unlink(ODT_SOCK);
		}
		return;
	}

	odt_listen();
	if (listen_fd < 0) return;

	if (client_fd < 0) {
		int c = accept(listen_fd, 0, 0);
		if (c < 0) return;
		int fl = fcntl(c, F_GETFL, 0);
		if (fl >= 0) fcntl(c, F_SETFL, fl | O_NONBLOCK);
		client_fd = c;
		inlen = 0;
	}

	char tmp[128];
	ssize_t n = read(client_fd, tmp, sizeof(tmp));
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) return;
		odt_close_client();
		return;
	}
	if (n == 0) {
		odt_close_client();
		return;
	}
	for (ssize_t i = 0; i < n; i++) {
		char ch = tmp[i];
		if (ch == '\r') continue;
		if (ch == '\n') {
			inbuf[inlen] = 0;
			inlen = 0;
			if (inbuf[0]) odt_cmd(client_fd, inbuf);
			else {
				struct panel_snap s;
				char reply[256];
				panel_read(&s);
				fmt_snap(reply, sizeof(reply), &s);
				odt_reply(client_fd, reply);
			}
			continue;
		}
		if (inlen < (int)sizeof(inbuf) - 1)
			inbuf[inlen++] = ch;
	}
}
