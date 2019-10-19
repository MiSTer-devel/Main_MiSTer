#include "spi.h"
#include "hardware.h"
#include "fpga_io.h"

#define SSPI_STROBE  (1<<17)
#define SSPI_ACK     SSPI_STROBE

#define SSPI_FPGA_EN (1<<18)
#define SSPI_OSD_EN  (1<<19)
#define SSPI_IO_EN   (1<<20)
#define SSPI_DM_EN   (1<<21)

#define SWAPW(a) ((((a)<<8)&0xff00)|(((a)>>8)&0x00ff))

static void spi_en(uint32_t mask, uint32_t en)
{
	uint32_t gpo = fpga_gpo_read() | 0x80000000;
	fpga_gpo_write(en ? gpo | mask : gpo & ~mask);
}

uint16_t spi_w(uint16_t word)
{
	uint32_t gpo = (fpga_gpo_read() & ~(0xFFFF | SSPI_STROBE)) | word;

	fpga_gpo_write(gpo);
	fpga_gpo_write(gpo | SSPI_STROBE);

	int gpi;
	do
	{
		gpi = fpga_gpi_read();
		if (gpi < 0)
		{
			printf("GPI[31]==1. FPGA is uninitialized?\n");
			return 0;
		}
	} while (!(gpi & SSPI_ACK));

	fpga_gpo_write(gpo);

	do
	{
		gpi = fpga_gpi_read();
		if (gpi < 0)
		{
			printf("GPI[31]==1. FPGA is uninitialized?\n");
			return 0;
		}
	} while (gpi & SSPI_ACK);

	return (uint16_t)gpi;
}

void spi_init(int enable)
{
	(void)enable;
	printf("Init SPI.\n");
}

uint8_t spi_b(uint8_t parm)
{
	return (uint8_t)spi_w(parm);
}

void EnableFpga()
{
	spi_en(SSPI_FPGA_EN, 1);
}

void DisableFpga()
{
	spi_en(SSPI_FPGA_EN, 0);
}

static int osd_target = OSD_ALL;

void EnableOsd_on(int target)
{
	if (!(target & OSD_ALL)) target = OSD_ALL;
	osd_target = target;
}

void EnableOsd()
{
	if (!(osd_target & OSD_ALL)) osd_target = OSD_ALL;

	uint32_t mask = SSPI_OSD_EN | SSPI_IO_EN | SSPI_FPGA_EN;
	if (osd_target & OSD_HDMI) mask &= ~SSPI_FPGA_EN;
	if (osd_target & OSD_VGA) mask &= ~SSPI_IO_EN;

	spi_en(mask, 1);
}

void DisableOsd()
{
	spi_en(SSPI_OSD_EN | SSPI_IO_EN | SSPI_FPGA_EN, 0);
}

void EnableIO()
{
	spi_en(SSPI_IO_EN, 1);
}

void DisableIO()
{
	spi_en(SSPI_IO_EN, 0);
}

void EnableDMode()
{
	spi_en(SSPI_DM_EN, 1);
}

void DisableDMode()
{
	spi_en(SSPI_DM_EN, 0);
}

uint8_t spi_in()
{
	return spi_b(0);
}

void spi8(uint8_t parm)
{
	spi_b(parm);
}

void spi16(uint16_t parm)
{
	spi8(parm >> 8);
	spi8(parm >> 0);
}

void spi24(uint32_t parm)
{
	spi8(parm >> 16);
	spi8(parm >> 8);
	spi8(parm >> 0);
}

void spi32(uint32_t parm)
{
	spi8(parm >> 24);
	spi8(parm >> 16);
	spi8(parm >> 8);
	spi8(parm >> 0);
}

uint32_t spi32w(uint32_t parm)
{
	uint32_t res;
	res = spi_w(parm);
	res |= (spi_w(parm>>16))<<16;
	return res;
}

// little endian: lsb first
void spi32le(uint32_t parm)
{
	spi8(parm >> 0);
	spi8(parm >> 8);
	spi8(parm >> 16);
	spi8(parm >> 24);
}

/* OSD related SPI functions */
void spi_osd_cmd_cont(uint8_t cmd)
{
	EnableOsd();
	spi8(cmd);
}

void spi_osd_cmd(uint8_t cmd)
{
	spi_osd_cmd_cont(cmd);
	DisableOsd();
}

void spi_osd_cmd8_cont(uint8_t cmd, uint8_t parm)
{
	EnableOsd();
	spi8(cmd);
	spi8(parm);
}

void spi_osd_cmd8(uint8_t cmd, uint8_t parm)
{
	spi_osd_cmd8_cont(cmd, parm);
	DisableOsd();
}

void spi_uio_cmd32le_cont(uint8_t cmd, uint32_t parm)
{
	EnableIO();
	spi8(cmd);
	spi32le(parm);
}

void spi_uio_cmd32le(uint8_t cmd, uint32_t parm)
{
	spi_uio_cmd32le_cont(cmd, parm);
	DisableIO();
}

/* User_io related SPI functions */
uint8_t spi_uio_cmd_cont(uint8_t cmd)
{
	EnableIO();
	return spi_b(cmd);
}

uint8_t spi_uio_cmd(uint8_t cmd)
{
	uint8_t res = spi_uio_cmd_cont(cmd);
	DisableIO();
	return res;
}

void spi_uio_cmd8_cont(uint8_t cmd, uint8_t parm)
{
	EnableIO();
	spi8(cmd);
	spi8(parm);
}

void spi_uio_cmd8(uint8_t cmd, uint8_t parm)
{
	spi_uio_cmd8_cont(cmd, parm);
	DisableIO();
}

void spi_uio_cmd16(uint8_t cmd, uint16_t parm)
{
	spi_uio_cmd_cont(cmd);
	spi_w(parm);
	DisableIO();
}

void spi_uio_cmd32(uint8_t cmd, uint32_t parm, int wide)
{
	EnableIO();
	spi8(cmd);
	if (wide)
	{
		spi_w((uint16_t)parm);
		spi_w((uint16_t)(parm >> 16));
	}
	else
	{
		spi8(parm);
		spi8(parm >> 8);
		spi8(parm >> 16);
		spi8(parm >> 24);
	}
	DisableIO();
}

void spi_n(uint8_t value, uint16_t cnt)
{
	while (cnt--) spi8(value);
}

void spi_read(uint8_t *addr, uint16_t len, int wide)
{
	if (wide)
	{
		uint16_t len16 = len >> 1;
		uint16_t *a16 = (uint16_t*)addr;
		while (len16--) *a16++ = spi_w(0);
		if (len & 1) *((uint8_t*)a16) = spi_w(0);
	}
	else
	{
		while (len--) *addr++ = spi_b(0);
	}
}

void spi_write(const uint8_t *addr, uint16_t len, int wide)
{
	if (wide)
	{
		uint16_t len16 = len >> 1;
		uint16_t *a16 = (uint16_t*)addr;
		while (len16--) spi_w(*a16++);
		if(len & 1) spi_w(*((uint8_t*)a16));
	}
	else
	{
		while (len--) spi8(*addr++);
	}
}

void spi_block_read(uint8_t *addr, int wide)
{
	spi_read(addr, 512, wide);
}

void spi_block_write(const uint8_t *addr, int wide)
{
	spi_write(addr, 512, wide);
}

void spi_block_write_16be(const uint16_t *addr)
{
	uint16_t len = 256;
	uint16_t tmp;
	while (len--)
	{
		tmp = *addr++;
		spi_w(SWAPW(tmp));
	}
}

void spi_block_read_16be(uint16_t *addr)
{
	uint16_t len = 256;
	uint16_t tmp;
	while (len--)
	{
		tmp = spi_w(0xFFFF);
		*addr++ = SWAPW(tmp);
	}
}
