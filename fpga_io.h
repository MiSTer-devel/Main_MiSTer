
#include <stdint.h>

#ifndef FPGAIO_H
#define FPGAIO_H

#define BUTTON_OSD  1
#define BUTTON_USR  2

int fpga_io_init();

void fpga_spi_en(uint32_t mask, uint32_t en);
uint16_t fpga_spi(uint16_t word);
uint16_t fpga_spi_fast(uint16_t word);

void fpga_set_led(uint32_t on);
int  fpga_get_buttons();
int fpga_get_io_type();

void fpga_core_reset(int reset);
void fpga_core_write(uint32_t offset, uint32_t value);
uint32_t fpga_core_read(uint32_t offset);
int fpga_core_id();
int is_fpga_ready(int quick);

int fpga_get_fio_size();
int fpga_get_io_version();

int fpga_load_rbf(const char *name, const char *cfg = 0, const char *xml = 0);

void reboot(int cold);
void app_restart(const char *path, const char *xml = 0);
char *getappname();

#endif
