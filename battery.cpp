/*

 * battery.c
 * display pi-top battery status
 *
 * Copyright 2016, 2017  rricharz
 * MiSTer port. Copyright 2018 Sorgelig
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 *
 */

#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include "battery.h"

#define MAX_COUNT       20                     // Maximum number of trials
#define SLEEP_TIME      500                    // time between two i2cget in microsec

///////////////////////////////////////////////////////////////////////
// I2C definitions

#define I2C_SLAVE	0x0703
#define I2C_SMBUS	0x0720	/* SMBus-level access */

#define I2C_SMBUS_READ	1
#define I2C_SMBUS_WRITE	0

// SMBus transaction types

#define I2C_SMBUS_QUICK		    0
#define I2C_SMBUS_BYTE		    1
#define I2C_SMBUS_BYTE_DATA	    2
#define I2C_SMBUS_WORD_DATA	    3
#define I2C_SMBUS_PROC_CALL	    4
#define I2C_SMBUS_BLOCK_DATA	    5
#define I2C_SMBUS_I2C_BLOCK_BROKEN  6
#define I2C_SMBUS_BLOCK_PROC_CALL   7		/* SMBus 2.0 */
#define I2C_SMBUS_I2C_BLOCK_DATA    8

// SMBus messages

#define I2C_SMBUS_BLOCK_MAX	32	/* As specified in SMBus standard */
#define I2C_SMBUS_I2C_BLOCK_MAX	32	/* Not specified but we use same structure */

// Structures used in the ioctl() calls

union i2c_smbus_data
{
  uint8_t  byte;
  int8_t   sbyte;
  uint16_t word;
  int16_t  sword;
  uint8_t  block [I2C_SMBUS_BLOCK_MAX + 2];	// block [0] is used for length + one more for PEC
};

struct i2c_smbus_ioctl_data
{
  char read_write;
  uint8_t command;
  int size;
  union i2c_smbus_data *data;
};

static int i2c_smbus_access (int fd, char rw, uint8_t command, int size, union i2c_smbus_data *data)
{
  struct i2c_smbus_ioctl_data args;

  args.read_write = rw;
  args.command    = command;
  args.size       = size;
  args.data       = data;
  return ioctl (fd, I2C_SMBUS, &args);
}

static int i2c_smbus_write_quick(int fd, uint8_t value)
{
	return i2c_smbus_access(fd, value, 0, I2C_SMBUS_QUICK, NULL);
}

///////////////////////////////////////////////////////////////////////

static int i2c_handle = -1;

static int smbus_open(int dev_address)
{
	// re-entry compatible
	if(i2c_handle < 0)
	{
		char str[16];
		for (int bus = 2; bus >= 0; bus--)
		{
			int fd;
			sprintf(str, "/dev/i2c-%d", bus);

			if ((fd = open(str, O_RDWR | O_CLOEXEC)) < 0)
			{
				printf("Unable to open I2C bus %s: %s\n", str, strerror(errno));
				continue;
			}

			if (ioctl(fd, I2C_SLAVE, dev_address) < 0)
			{
				printf("Unable to select I2C device on bus %s: %s\n", str, strerror(errno));
				close(fd);
				continue;
			}

			if (i2c_smbus_write_quick(fd, I2C_SMBUS_WRITE) < 0)
			{
				printf("Unable to detect SMBUS device on bus %s: %s\n", str, strerror(errno));
				close(fd);
				continue;
			}

			i2c_handle = fd;
			return 1;
		}
		return 0;
	}
	return 1;
}

/*
static void smbus_close()
{
	if(i2c_handle > 0) close(i2c_handle);
	i2c_handle = -1;
}
*/

static int smbus_get(int address, short *data)
{
	union i2c_smbus_data smbus_data;

	if (i2c_smbus_access (i2c_handle, I2C_SMBUS_READ, address, I2C_SMBUS_WORD_DATA, &smbus_data)) return 0;

	*data = smbus_data.sword;
	return 1;
}

static int getReg(int reg, int min, int max)
{
	int count = 0;
	short value = -1;
	while ((value == -1) && (count++ < MAX_COUNT))
	{
		if (smbus_get(reg, &value))
		{
			if ((value > max) || (value < min)) value = -1; // out of limits
		}
		usleep(SLEEP_TIME);
	}

	return value;
}

int getBattery(int quick, struct battery_data_t *data)
{
	/*
	data->capacity = 80;
	data->load_current = -520;
	data->time = 312;
	data->current = 2510;
	data->voltage = 16200;
	data->cell[0] = 4101;
	data->cell[1] = 4102;
	data->cell[2] = 4103;
	data->cell[3] = 4104;
	return 1;
	*/
	// don't try to check if no battery device is present
	if (i2c_handle == -2) return 0;

	if (!smbus_open(0x0b))
	{
		printf("No battery found.\n");
		i2c_handle = -2;
		return 0;
	}

	data->capacity = getReg(0x0D, 0, 100);
	data->load_current = getReg(0x0A, -5000, 5000);
	if (quick) return 1;

	data->time = 0;
	if (data->load_current > 0)  data->time = getReg(0x13, 1, 999);
	if (data->load_current < -1) data->time = getReg(0x12, 1, 960);

	data->current = getReg(0x0F, 0, 5000);
	data->voltage = getReg(0x09, 5000, 20000);
	/*
	data->cell[0] = getReg(0x3F, 1000, 5000);
	data->cell[1] = getReg(0x3E, 1000, 5000);
	data->cell[2] = getReg(0x3D, 1000, 5000);
	data->cell[3] = getReg(0x3C, 1000, 5000);
	*/

	return 1;
}
