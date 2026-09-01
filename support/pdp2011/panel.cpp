#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "panel.h"
#include "../../hardware.h"
#include "../../spi.h"

static void oct6(char *dst, unsigned v)
{
	for (int i = 5; i >= 0; i--) {
		dst[i] = '0' + (v & 7);
		v >>= 3;
	}
	dst[6] = 0;
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

	spi_uio_cmd_cont(UIO_PDP_PANEL);
	uint16_t r7 = spi_w(0);
	uint16_t psw = spi_w(0);
	uint16_t ir = spi_w(0);
	uint16_t flags = spi_w(0);
	uint16_t data = 0, addr = 0;
	if (flags & 8) {
		data = spi_w(0);
		addr = spi_w(0);
	}
	DisableIO();

	next = GetTimer(50);

	char line0[32], line1[32];
	if (flags & 1) {
		char pc[7], sw[7], star[7];
		oct6(pc, r7);
		oct6(sw, psw);
		oct6(star, ir);
		snprintf(line0, sizeof(line0), " PC %s  PSW %s  %s",
			pc, sw, (flags & 0x8000) ? " RUN" : "HALT");
		if (flags & 8) {
			char ma[7], d[7];
			oct6(ma, addr);
			oct6(d, data);
			if (flags & 2)
				snprintf(line1, sizeof(line1),
					(flags & 4) ? "*PC %s  MA %s  D   NXM" : "*PC %s  MA %s  D %s",
					star, ma, d);
			else
				snprintf(line1, sizeof(line1),
					(flags & 4) ? "*PC ------  MA %s  D   NXM" : "*PC ------  MA %s  D %s",
					ma, d);
		} else if (flags & 2) {
			snprintf(line1, sizeof(line1), "*PC %s", star);
		} else {
			snprintf(line1, sizeof(line1), "*PC ------");
		}
	} else {
		snprintf(line0, sizeof(line0), " PC ------  PSW ------  ----");
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
