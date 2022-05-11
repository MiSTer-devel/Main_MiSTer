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
#include "smbus.h"
#include "battery.h"

#define MAX_COUNT       20                     // Maximum number of trials
#define SLEEP_TIME      500                    // time between two i2cget in microsec

static int i2c_handle = -1;

static int getReg(int reg, int min, int max)
{
	int count = 0;
	short value = -1;
	while ((value == -1) && (count++ < MAX_COUNT))
	{
		value = i2c_smbus_read_word_data(i2c_handle, reg);
		if (value >= 0)
		{
			if ((value > max) || (value < min)) value = -1; // out of limits
		}
		usleep(SLEEP_TIME);
	}

	return value;
}

int getBattery(int quick, struct battery_data_t *data)
{
	// don't try to check if no battery device is present
	if (i2c_handle == -2) return 0;

	i2c_handle = i2c_open(0x0B, 1);
	if (i2c_handle < 0)
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

	return 1;
}
