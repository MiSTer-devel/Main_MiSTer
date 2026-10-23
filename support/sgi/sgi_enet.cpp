// SGI Indy Ethernet: the machine's address (the SEEQ 80C03 and HPC3's
// Ethernet DMA channels follow; SGIIndy_MiSTer docs/design/ethernet-plan.md).

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../user_io.h"
#include "../../file_io.h"
#include "sgi.h"
#include "sgi_enet.h"

extern int ethernet_read_iface_mac(const char *iface, uint8_t *out);

#define MAC_INDEX 64

// IRIX attaches ec0 only to an address in SGI's 08:00:69 (or 00:00:1d)
static int make_mac(uint8_t *p)
{
	uint8_t hw[6];
	if (!ethernet_read_iface_mac("eth0", hw) && !ethernet_read_iface_mac("wlan0", hw))
		return 0;

	p[0] = 0x08; p[1] = 0x00; p[2] = 0x69;
	p[3] = hw[3]; p[4] = hw[4]; p[5] = hw[5];
	return 1;
}

void sgi_enet_start(void)
{
	uint8_t p[6];
	const char *src;

	char *path = user_io_make_filepath(HomeDir(), "boot1.rom");
	if (FileLoad(path, 0, 0) >= 6)
	{
		FileLoad(path, p, sizeof(p));
		src = "boot1.rom";
	}
	else if (make_mac(p)) src = "this MiSTer's Ethernet address";
	else
	{
		printf("[sgi-enet] no boot1.rom and no host address: the core keeps 08:00:69:12:34:56\n");
		return;
	}

	user_io_set_index(MAC_INDEX);
	user_io_set_download(1);
	user_io_file_tx_data(p, sizeof(p));
	user_io_set_download(0);
	printf("[sgi-enet] address from %s: %02X:%02X:%02X:%02X:%02X:%02X\n", src,
	       p[0], p[1], p[2], p[3], p[4], p[5]);
}
