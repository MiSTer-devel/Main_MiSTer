#ifndef SPI_H
#define SPI_H

#include <inttypes.h>

#define OSD_HDMI 1
#define OSD_VGA  2
#define OSD_ALL  (OSD_VGA|OSD_HDMI)

/* main init functions */
void spi_init(int enable);

/* chip select functions */
void EnableFpga();
void DisableFpga();
void EnableOsd();
void DisableOsd();
void EnableDMode();
void DisableDMode();
void EnableIO();
void DisableIO();

// base functions
uint8_t  spi_b(uint8_t parm);
uint16_t spi_w(uint16_t word);

// input only helper
uint8_t spi_in();

void spi8(uint8_t parm);
void spi16(uint16_t parm);
void spi24(uint32_t parm);
void spi32(uint32_t parm);
void spi32le(uint32_t parm);
void spi_n(uint8_t value, uint16_t cnt);
uint32_t spi32w(uint32_t parm);

/* block transfer functions */
void spi_block_read(uint8_t *addr, int wide);
void spi_read(uint8_t *addr, uint16_t len, int wide);
void spi_block_write(const uint8_t *addr, int wide);
void spi_write(const uint8_t *addr, uint16_t len, int wide);
void spi_block_write_16be(const uint16_t *addr);
void spi_block_read_16be(uint16_t *addr);

/* OSD related SPI functions */
void EnableOsd_on(int target);
void spi_osd_cmd_cont(uint8_t cmd);
void spi_osd_cmd(uint8_t cmd);
void spi_osd_cmd8_cont(uint8_t cmd, uint8_t parm);
void spi_osd_cmd8(uint8_t cmd, uint8_t parm);

/* User_io related SPI functions */
uint8_t spi_uio_cmd_cont(uint8_t cmd);
uint8_t spi_uio_cmd(uint8_t cmd);
void spi_uio_cmd8(uint8_t cmd, uint8_t parm);
void spi_uio_cmd8_cont(uint8_t cmd, uint8_t parm);
void spi_uio_cmd16(uint8_t cmd, uint16_t parm);
void spi_uio_cmd32(uint8_t cmd, uint32_t parm, int wide);
void spi_uio_cmd32le_cont(uint8_t cmd, uint32_t parm);
void spi_uio_cmd32le(uint8_t cmd, uint32_t parm);

#endif // SPI_H
