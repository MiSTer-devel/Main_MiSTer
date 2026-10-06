#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../user_io.h"
#include "../../file_io.h"
#include "sun.h"
#include "sun_idprom.h"

extern int ethernet_read_iface_mac(const char *iface, uint8_t *out);

#define IDPROM_INDEX 64

// format, machine, MAC, date, serial, checksum (bytes 0..15 XOR to 0), then 16 x 0xFF
static int make_idprom(uint8_t *p)
{
	uint8_t hw[6];
	if (!ethernet_read_iface_mac("eth0", hw) && !ethernet_read_iface_mac("wlan0", hw))
		return 0;

	memset(p, 0xFF, 32);
	p[0] = 0x01;
	p[1] = sun_idprom_machine();
	p[2] = 0x08; p[3] = 0x00; p[4] = 0x20;
	p[5] = hw[3]; p[6] = hw[4]; p[7] = hw[5];
	p[8] = 0x1A; p[9] = 0xE4; p[10] = 0x23; p[11] = 0x3B;
	p[12] = hw[3]; p[13] = hw[4]; p[14] = hw[5];
	p[15] = 0;
	for (int i = 0; i < 15; i++) p[15] ^= p[i];
	return 1;
}

void sun_idprom_send(void)
{
	uint8_t p[32];
	const char *src;

	memset(p, 0xFF, sizeof(p));
	char *path = user_io_make_filepath(HomeDir(), "boot1.rom");
	int n = FileLoad(path, 0, 0);
	if (n >= 16)
	{
		FileLoad(path, p, sizeof(p));
		src = "boot1.rom";
	}
	else if (make_idprom(p)) src = "this MiSTer's Ethernet address";
	else
	{
		printf("[sun-idprom] no boot1.rom and no host address: the core keeps its own ID PROM\n");
		return;
	}

	user_io_set_index(IDPROM_INDEX);
	user_io_set_download(1);
	user_io_file_tx_data(p, sizeof(p));
	user_io_set_download(0);
	printf("[sun-idprom] from %s: %02X:%02X:%02X:%02X:%02X:%02X, serial %u\n", src,
	       p[2], p[3], p[4], p[5], p[6], p[7], (p[12] << 16) | (p[13] << 8) | p[14]);
}
