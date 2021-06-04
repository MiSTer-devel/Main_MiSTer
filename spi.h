#ifndef SPI_H
#define SPI_H

#include <inttypes.h>
#include "fpga_io.h"

#define OSD_HDMI 1
#define OSD_VGA  2
#define OSD_ALL  (OSD_VGA|OSD_HDMI)

/* chip select functions */
void EnableFpga();
void DisableFpga();
void EnableOsd();
void DisableOsd();
void EnableIO();
void DisableIO();

// base functions
uint8_t  inline spi_b(uint8_t parm)
{
	return (uint8_t)fpga_spi(parm);
}

uint16_t inline spi_w(uint16_t word)
{
	return fpga_spi(word);
}

// input only helper
uint8_t inline spi_in()
{
	return (uint8_t)fpga_spi(0);
}

#define spi8(x) spi_b(x)

void spi32_b(uint32_t parm);
uint32_t spi32_w(uint32_t parm);

/* block transfer functions */
void spi_read(uint8_t *addr, uint16_t len, int wide);
void spi_write(const uint8_t *addr, uint16_t len, int wide);
void spi_block_read(uint8_t *addr, int wide, int sz = 512);
void spi_block_write(const uint8_t *addr, int wide, int sz = 512);

/* OSD related SPI functions */
void EnableOsd_on(int target);
void spi_osd_cmd_cont(uint8_t cmd);
void spi_osd_cmd(uint8_t cmd);
void spi_osd_cmd8_cont(uint8_t cmd, uint8_t parm);
void spi_osd_cmd8(uint8_t cmd, uint8_t parm);

/* User_io related SPI functions */
uint16_t spi_uio_cmd_cont(uint8_t cmd);
uint16_t spi_uio_cmd(uint8_t cmd);
uint8_t spi_uio_cmd8_cont(uint8_t cmd, uint8_t parm);
uint8_t spi_uio_cmd8(uint8_t cmd, uint8_t parm);
uint16_t spi_uio_cmd16(uint8_t cmd, uint16_t parm);
void spi_uio_cmd32(uint8_t cmd, uint32_t parm, int wide);
void spi_uio_cmd32_cont(uint8_t cmd, uint32_t parm);

#endif // SPI_H
