/*
    smbus.c - SMBus level access helper functions

    Copyright (C) 1995-1997  Simon G. Vogl
    Copyright (C) 1998-1999  Frodo Looijaard <frodol@dds.nl>
    Copyright (C) 2012-2013  Jean Delvare <jdelvare@suse.de>

    This library is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.
*/

#include <errno.h>
#include <stddef.h>
#include <unistd.h>
#include <linux/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "smbus.h"

#define I2C_SLAVE                   0x0703
#define I2C_SMBUS                   0x0720	/* SMBus-level access */

#define I2C_SMBUS_READ              1
#define I2C_SMBUS_WRITE	            0

// SMBus transaction types
#define I2C_SMBUS_QUICK		        0
#define I2C_SMBUS_BYTE		        1
#define I2C_SMBUS_BYTE_DATA	        2
#define I2C_SMBUS_WORD_DATA	        3
#define I2C_SMBUS_PROC_CALL	        4
#define I2C_SMBUS_BLOCK_DATA        5
#define I2C_SMBUS_I2C_BLOCK_BROKEN  6
#define I2C_SMBUS_BLOCK_PROC_CALL   7   /* SMBus 2.0 */
#define I2C_SMBUS_I2C_BLOCK_DATA    8

// SMBus messages
#define I2C_SMBUS_BLOCK_MAX	        32  /* As specified in SMBus standard */

union i2c_smbus_data
{
	uint8_t  byte;
	uint16_t word;
	uint8_t  block[I2C_SMBUS_BLOCK_MAX + 2];	// block [0] is used for length + one more for PEC
};

struct i2c_smbus_ioctl_data
{
	uint8_t read_write;
	uint8_t command;
	uint32_t size;
	union i2c_smbus_data *data;
};

static int i2c_smbus_access(int file, char read_write, uint8_t command,
		       int size, union i2c_smbus_data *data)
{
	struct i2c_smbus_ioctl_data args;
	int err;

	args.read_write = read_write;
	args.command = command;
	args.size = size;
	args.data = data;

	err = ioctl(file, I2C_SMBUS, &args);
	if (err == -1) err = -errno;
	return err;
}

int i2c_smbus_write_quick(int file, uint8_t value)
{
	return i2c_smbus_access(file, value, 0, I2C_SMBUS_QUICK, NULL);
}

int i2c_smbus_read_byte(int file)
{
	union i2c_smbus_data data;
	int err;

	err = i2c_smbus_access(file, I2C_SMBUS_READ, 0, I2C_SMBUS_BYTE, &data);
	if (err < 0) return err;

	return 0x0FF & data.byte;
}

int i2c_smbus_write_byte(int file, uint8_t value)
{
	return i2c_smbus_access(file, I2C_SMBUS_WRITE, value, I2C_SMBUS_BYTE, NULL);
}

int i2c_smbus_read_byte_data(int file, uint8_t command)
{
	union i2c_smbus_data data;
	int err;

	err = i2c_smbus_access(file, I2C_SMBUS_READ, command, I2C_SMBUS_BYTE_DATA, &data);
	if (err < 0) return err;

	return 0x0FF & data.byte;
}

int i2c_smbus_write_byte_data(int file, uint8_t command, uint8_t value)
{
	//printf("i2c: %02X %02X\n", command, value);
	union i2c_smbus_data data;
	data.byte = value;
	return i2c_smbus_access(file, I2C_SMBUS_WRITE, command, I2C_SMBUS_BYTE_DATA, &data);
}

int i2c_smbus_read_word_data(int file, uint8_t command)
{
	union i2c_smbus_data data;
	int err;

	err = i2c_smbus_access(file, I2C_SMBUS_READ, command, I2C_SMBUS_WORD_DATA, &data);
	if (err < 0) return err;

	return 0x0FFFF & data.word;
}

int i2c_smbus_write_word_data(int file, uint8_t command, uint16_t value)
{
	union i2c_smbus_data data;
	data.word = value;
	return i2c_smbus_access(file, I2C_SMBUS_WRITE, command, I2C_SMBUS_WORD_DATA, &data);
}

int i2c_smbus_process_call(int file, uint8_t command, uint16_t value)
{
	union i2c_smbus_data data;
	data.word = value;
	if (i2c_smbus_access(file, I2C_SMBUS_WRITE, command, I2C_SMBUS_PROC_CALL, &data)) return -1;
	else return 0x0FFFF & data.word;
}

/* Returns the number of read bytes */
int i2c_smbus_read_block_data(int file, uint8_t command, uint8_t *values)
{
	union i2c_smbus_data data;
	int i, err;

	err = i2c_smbus_access(file, I2C_SMBUS_READ, command, I2C_SMBUS_BLOCK_DATA, &data);
	if (err < 0) return err;

	for (i = 1; i <= data.block[0]; i++) values[i-1] = data.block[i];
	return data.block[0];
}

int i2c_smbus_write_block_data(int file, uint8_t command, uint8_t length, const uint8_t *values)
{
	union i2c_smbus_data data;
	int i;
	if (length > I2C_SMBUS_BLOCK_MAX) length = I2C_SMBUS_BLOCK_MAX;
	for (i = 1; i <= length; i++) data.block[i] = values[i-1];
	data.block[0] = length;
	return i2c_smbus_access(file, I2C_SMBUS_WRITE, command, I2C_SMBUS_BLOCK_DATA, &data);
}

/* Returns the number of read bytes */
/* Until kernel 2.6.22, the length is hardcoded to 32 bytes. If you
   ask for less than 32 bytes, your code will only work with kernels
   2.6.23 and later. */
int i2c_smbus_read_i2c_block_data(int file, uint8_t command, uint8_t length, uint8_t *values)
{
	union i2c_smbus_data data;
	int i, err;

	if (length > I2C_SMBUS_BLOCK_MAX) length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;

	err = i2c_smbus_access(file, I2C_SMBUS_READ, command, length == 32 ? I2C_SMBUS_I2C_BLOCK_BROKEN : I2C_SMBUS_I2C_BLOCK_DATA, &data);
	if (err < 0) return err;

	for (i = 1; i <= data.block[0]; i++) values[i-1] = data.block[i];
	return data.block[0];
}

int i2c_smbus_write_i2c_block_data(int file, uint8_t command, uint8_t length, const uint8_t *values)
{
	union i2c_smbus_data data;
	int i;
	if (length > I2C_SMBUS_BLOCK_MAX) length = I2C_SMBUS_BLOCK_MAX;
	for (i = 1; i <= length; i++) data.block[i] = values[i-1];
	data.block[0] = length;
	return i2c_smbus_access(file, I2C_SMBUS_WRITE, command, I2C_SMBUS_I2C_BLOCK_BROKEN, &data);
}

/* Returns the number of read bytes */
int i2c_smbus_block_process_call(int file, uint8_t command, uint8_t length, uint8_t *values)
{
	union i2c_smbus_data data;
	int i, err;

	if (length > I2C_SMBUS_BLOCK_MAX) length = I2C_SMBUS_BLOCK_MAX;
	for (i = 1; i <= length; i++) data.block[i] = values[i-1];
	data.block[0] = length;

	err = i2c_smbus_access(file, I2C_SMBUS_WRITE, command, I2C_SMBUS_BLOCK_PROC_CALL, &data);
	if (err < 0) return err;

	for (i = 1; i <= data.block[0]; i++) values[i-1] = data.block[i];
	return data.block[0];
}

int i2c_open(int dev_address, int is_smbus)
{
	char str[16];
	for (int bus = 0; bus < 3; bus++)
	{
		int fd;
		sprintf(str, "/dev/i2c-%d", bus);

		if ((fd = open(str, O_RDWR | O_CLOEXEC)) < 0)
		{
			//printf("Unable to open I2C bus %s: %s\n", str, strerror(errno));
			continue;
		}

		if (ioctl(fd, I2C_SLAVE, dev_address) < 0)
		{
			//printf("Unable to select I2C device 0x%X on bus %s: %s\n", dev_address, str, strerror(errno));
			close(fd);
			continue;
		}

		if (is_smbus)
		{
			if (i2c_smbus_write_quick(fd, 0) < 0)
			{
				//printf("Unable to detect SMBUS device on bus %s: %s\n", str, strerror(errno));
				close(fd);
				continue;
			}
		}
		else
		{
			if (i2c_smbus_read_byte(fd) < 0)
			{
				//printf("Unable to detect I2C device on bus %s: %s\n", str, strerror(errno));
				close(fd);
				continue;
			}
		}

		printf("Opened %s for device 0x%X\n", str, dev_address);
		return fd;
	}

	return -1;
}

void i2c_close(int fd)
{
	if (fd >= 0) close(fd);
}
