/*
 * brightness.c
 *
 * Copyright 2016  rricharz
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

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include "brightness.h"

#define MAXCOUNT 10		// maximum number of spi transfer attemps

#define LIDBITMASK		0x04
#define	SCREENOFFMASK	0x02
#define PARITYMASK		0x80
#define BRIGHTNESSMASK  0x78
#define SHUTDOWNMASK	0X01

///////////////////////////////////////////////////

static uint32_t    spiSpeed;
static int         spiFd = -1;

static const uint8_t  spiBPW   = 8;
static const uint16_t spiDelay = 0;

static int spi_rw(unsigned char *data, int len)
{
	struct spi_ioc_transfer spi;

	memset (&spi, 0, sizeof (spi));

	spi.tx_buf        = (unsigned long)data;
	spi.rx_buf        = (unsigned long)data;
	spi.len           = len;
	spi.delay_usecs   = spiDelay;
	spi.speed_hz      = spiSpeed;
	spi.bits_per_word = spiBPW;

	return ioctl (spiFd, SPI_IOC_MESSAGE(1), &spi);
}

static int spi_open(int speed, int mode)
{
	int fd ;
	mode &= 3;	// Mode is 0, 1, 2 or 3

	if ((fd = open ("/dev/spidev1.0", O_RDWR)) < 0)
	{
		printf("Unable to open SPI device: %s\n", strerror (errno));
		return -1;
	}

	if (ioctl (fd, SPI_IOC_WR_MODE, &mode) >= 0)
	{
		if (ioctl (fd, SPI_IOC_WR_BITS_PER_WORD, &spiBPW) >= 0)
		{
			if (ioctl (fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) >= 0)
			{
				spiSpeed = speed;
				spiFd    = fd;
				return fd;
			}
			else
			{
				printf ("SPI Speed Change failure: %s\n", strerror (errno));
			}
		}
		else
		{
			printf ("SPI BPW Change failure: %s\n", strerror (errno));
		}
	}
	else
	{
		printf ("SPI Mode Change failure: %s\n", strerror (errno));
	}

	close(fd);
	fd = -1;
	return -1;
}

static void spi_close()
{
	if(spiFd >= 0) close(spiFd);
	spiFd = -1;
}

///////////////////////////////////////////////////

static int lidbit;
static int screenoffbit;
static int parity;
static int brightness = 6;
static int shutdown;

//////////////////////////////
// Calclate the parity of bits 0 - 6
static int parity7(unsigned char data)
{
	int i;
	int p = 0;
	for (i = 0; i < 7; i++) {
		if (data & 1) p = !p;
		data = data >> 1;
	}
	return p;
}

///////////////////////////////
// analyze data byte and set global variables
// return 1 of parity is ok
// Sending: bit 8 = parity of bit 1-7
static int analyze(unsigned char data)
{
	lidbit			= (data & LIDBITMASK) != 0;
	screenoffbit	= (data & SCREENOFFMASK) != 0;
	parity			= (data & PARITYMASK) != 0;
	brightness		= (data & BRIGHTNESSMASK) >> 3;
	shutdown		= (data & SHUTDOWNMASK) != 0;

	// printf("lid = %d, screen = %d, parity = %d, shutdown = %d, brightness = %d\n", lidbit, screenoffbit, parity, shutdown, brightness);

	return (parity7(data) == parity);
}

///////////////
// Calculate data byte using global variables
// Set brightness and status parity bits
// Receiving: bit 8 = brightness parity, bit 3 = status parity
static int calculate()
{
	int data = brightness << 3;
	if (parity7(brightness)) data += PARITYMASK;
	if (shutdown) data += SHUTDOWNMASK;
	if (screenoffbit) data += SCREENOFFMASK;
	if (parity7(data & 3)) data += LIDBITMASK;		// parity ofthe two state bits
	return data;
}

void setBrightness(int cmd, int val)
{
	unsigned char data, new_data;
	int count, ok;

	if (spi_open(9600, 0) < 0)
	{
		printf("Cannot initialize spi driver\n");
		return;
	}

	// send 0xFF and receive current status of pi-top-hub
	count = 0;
	data = 0xff;
	printf("Brighntess sending: 0x%X\n", data);
	do
	{
		data = 0xff;
		ok = spi_rw(&data, 1);
		if (ok) ok = analyze(data);
	}
	while ((!ok) && (count++ < MAXCOUNT));
	// printf("count = %d\n", count);

	if (ok)
	{
		printf("Brighntess receiving: 0x%X - ", data);
		printf("Current brightness = %d, ", brightness);
		//force to 0 as set to 1 if rebooted while in screenbitoff=0
		//the state is stored on pi-top-hub, but isn't reinitialised on reboot
		screenoffbit=0;
		if(cmd == BRIGHTNESS_UP)
		{
			brightness++;
		}
		else if (cmd == BRIGHTNESS_DOWN)
		{
			brightness--;
		}
		else if (cmd == BRIGHTNESS_SET)
		{
			if(!val) screenoffbit=1;
			else screenoffbit = 0;
			brightness = val;
		}

		if (brightness < 1) brightness = 1;
		if (brightness > 10) brightness = 10;

		printf("Requested brightness = %d, ", brightness);
        printf("Requested off = %d\n", screenoffbit);

		// calculate data to send
		shutdown = 0;
		new_data = calculate();

		// send new data until accepted
		count = 0;
		data = new_data;
		printf("Brighntess sending: 0x%X\n", data);
		do
		{
			data = new_data;
			ok = spi_rw(&data, 1);
			if (ok) ok = (data & BRIGHTNESSMASK) == (new_data & BRIGHTNESSMASK);
		}
		while ((!ok) && (count++ < MAXCOUNT));

		// printf("count = %d\n", count);
		if (ok)
		{
			printf("Brighntess receiving: 0x%X - ", data);
			printf("New brightness = %d\n", brightness);
		}
	}
	else printf("Reading current brightness not successful\n");

	spi_close();
}

int getBrightness()
{
	if (screenoffbit) return 0;
	return brightness;
}
