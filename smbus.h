#include <stdint.h>

#ifndef SMBUS_H
#define SMBUS_H

int i2c_open(int dev_address, int is_smbus);
void i2c_close(int fd);

int i2c_smbus_write_quick(int file, uint8_t value);
int i2c_smbus_read_byte(int file);
int i2c_smbus_write_byte(int file, uint8_t value);
int i2c_smbus_read_byte_data(int file, uint8_t command);
int i2c_smbus_write_byte_data(int file, uint8_t command, uint8_t value);
int i2c_smbus_read_word_data(int file, uint8_t command);
int i2c_smbus_write_word_data(int file, uint8_t command, uint16_t value);
int i2c_smbus_process_call(int file, uint8_t command, uint16_t value);
int i2c_smbus_read_block_data(int file, uint8_t command, uint8_t *values);
int i2c_smbus_write_block_data(int file, uint8_t command, uint8_t length, const uint8_t *values);
int i2c_smbus_read_i2c_block_data(int file, uint8_t command, uint8_t length, uint8_t *values);
int i2c_smbus_write_i2c_block_data(int file, uint8_t command, uint8_t length, const uint8_t *values);
int i2c_smbus_block_process_call(int file, uint8_t command, uint8_t length, uint8_t *values);

#endif
