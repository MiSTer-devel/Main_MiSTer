/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

This is the Minimig OSD (on-screen-display) handler.

2012-02-09 - Split character rom out to separate header file, with upper 128 entries
as rotated copies of the first 128 entries.  -- AMR

29-12-2006 - created
30-12-2006 - improved and simplified
-- JB --
2008-10-04 - ARM version
2008-10-26 - added cpu and floppy configuration functions
2008-12-31 - added enable HDD command
2009-02-03 - full keyboard support
2009-06-23 - hires OSD display
2009-08-23 - adapted ConfigIDE() - support for 2 hardfiles
*/

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "osd.h"
#include "spi.h"

#include "charrom.h"
#include "logo.h"
#include "user_io.h"
#include "hardware.h"
#include "config.h"

static int osd_size = 8;

void OsdSetSize(int n)
{
	osd_size = n;
}

int OsdGetSize()
{
	return osd_size;
}

struct star
{
	int x, y;
	int dx, dy;
};

struct star stars[64];

char framebuffer[16][256];
void framebuffer_clear()
{
	memset(framebuffer, 0, sizeof(framebuffer));
}

void framebuffer_plot(int x, int y)
{
	y = (y * osd_size) / 8;
	framebuffer[y / 8][x] |= (1 << (y & 7));
}

void StarsInit()
{
	srand(time(NULL));
	for (int i = 0; i<64; ++i)
	{
		stars[i].x = (rand() % 228) << 4;	// X centre
		stars[i].y = (rand() % 56) << 4;	// Y centre
		stars[i].dx = -(rand() & 7) - 3;
		stars[i].dy = 0;
	}
}

void StarsUpdate()
{
	framebuffer_clear();
	for (int i = 0; i<64; ++i)
	{
		stars[i].x += stars[i].dx;
		stars[i].y += stars[i].dy;
		if ((stars[i].x<0) || (stars[i].x>(228 << 4)) ||
			(stars[i].y<0) || (stars[i].y>(56 << 4)))
		{
			stars[i].x = 228 << 4;
			stars[i].y = (rand() % 56) << 4;
			stars[i].dx = -(rand() & 7) - 3;
			stars[i].dy = 0;
		}
		framebuffer_plot(stars[i].x >> 4, stars[i].y >> 4);
	}
}


// time delay after which file/dir name starts to scroll
#define SCROLL_DELAY 1000
#define SCROLL_DELAY2 10
#define SCROLL_DELAY3 50

static unsigned long scroll_offset = 0; // file/dir name scrolling position
static unsigned long scroll_timer = 0;  // file/dir name scrolling timer

static int arrow;
static unsigned char titlebuffer[128];

static void rotatechar(unsigned char *in, unsigned char *out)
{
	int a;
	int b;
	int c;
	for (b = 0; b<8; ++b)
	{
		a = 0;
		for (c = 0; c<8; ++c)
		{
			a <<= 1;
			a |= (in[c] >> b) & 1;
		}
		out[b] = a;
	}
}

#define OSDHEIGHT (osd_size*8)

void OsdSetTitle(char *s, int a)
{
	// Compose the title, condensing character gaps
	arrow = a;
	int zeros = 0;
	int i = 0, j = 0;
	int outp = 0;
	while (1)
	{
		int c = s[i++];
		if (c && (outp<OSDHEIGHT))
		{
			unsigned char *p = &charfont[c][0];
			for (j = 0; j<8; ++j)
			{
				unsigned char nc = *p++;
				if (nc)
				{
					zeros = 0;
					titlebuffer[outp++] = nc;
				}
				else if (zeros == 0)
				{
					titlebuffer[outp++] = 0;
					zeros = 1;
				}
				if (outp>63)
					break;
			}
		}
		else
			break;
	}
	for (i = outp; i<OSDHEIGHT; i++)
	{
		titlebuffer[i] = 0;
	}

	// Now centre it:
	int c = (OSDHEIGHT - 1 - outp) / 2;
	memmove(titlebuffer + c, titlebuffer, outp);

	for (i = 0; i<c; ++i)
		titlebuffer[i] = 0;

	// Finally rotate it.
	for (i = 0; i<OSDHEIGHT; i += 8)
	{
		unsigned char tmp[8];
		rotatechar(&titlebuffer[i], tmp);
		for (c = 0; c<8; ++c)
		{
			titlebuffer[i + c] = tmp[c];
		}
	}
}

void OsdWrite(unsigned char n, char *s, unsigned char invert, unsigned char stipple)
{
	OsdWriteOffset(n, s, invert, stipple, 0, 0);
}

// write a null-terminated string <s> to the OSD buffer starting at line <n>
void OsdWriteOffset(unsigned char n, char *s, unsigned char invert, unsigned char stipple, char offset, char leftchar)
{
	//printf("OsdWriteOffset(%d)\n", n);
	unsigned short i;
	unsigned char b;
	const unsigned char *p;
	unsigned char stipplemask = 0xff;
	int linelimit = OSDLINELEN;
	int arrowmask = arrow;
	if (n == (osd_size-1) && (arrow & OSD_ARROW_RIGHT))
		linelimit -= 22;

	if (stipple) {
		stipplemask = 0x55;
		stipple = 0xff;
	}
	else
		stipple = 0;

	// select buffer and line to write to
	if (!is_minimig())
		spi_osd_cmd_cont(MM1_OSDCMDWRITE | n);
	else
		spi_osd_cmd32_cont(OSD_CMD_OSD_WR, n);

	if (invert)	invert = 255;

	i = 0;
	// send all characters in string to OSD
	while (1)
	{
		if (i == 0 && (n < osd_size))
		{	// Render sidestripe
			unsigned char j;
			unsigned char tmp[8];

			if (leftchar)
			{
				unsigned char tmp2[8];
				memcpy(tmp2, charfont[leftchar], 8);
				rotatechar(tmp2, tmp);
				p = tmp;
			}
			else
			{
				p = &titlebuffer[(osd_size - 1 - n) * 8];
			}

			spi16(0xffff);  // left white border

			for (j = 0; j < 8; j++)
				spi_n(255 ^ *p++, 2);

			spi16(0xffff);  // right white border
			spi16(0x0000);  // blue gap
			i += 22;
		}
		else if (n == (osd_size-1) && (arrowmask & OSD_ARROW_LEFT)) {	// Draw initial arrow
			unsigned char b;

			spi24(0x00);
			p = &charfont[0x10][0];
			for (b = 0; b<8; b++) spi8(*p++ << offset);
			p = &charfont[0x14][0];
			for (b = 0; b<8; b++) spi8(*p++ << offset);
			spi24(0x00);
			spi_n(invert, 2);
			i += 24;
			arrowmask &= ~OSD_ARROW_LEFT;
			if (*s++ == 0) break;	// Skip 3 characters, to keep alignent the same.
			if (*s++ == 0) break;
			if (*s++ == 0) break;
		}
		else {
			b = *s++;

			if (b == 0) // end of string
				break;

			else if (b == 0x0d || b == 0x0a) { // cariage return / linefeed, go to next line
											   // increment line counter
				if (++n >= linelimit)
					n = 0;

				// send new line number to OSD
				DisableOsd();

				if (!is_minimig())
					spi_osd_cmd_cont(MM1_OSDCMDWRITE | n);
				else
					spi_osd_cmd32_cont(OSD_CMD_OSD_WR, n);
			}
			else if (i<(linelimit - 8)) { // normal character
				unsigned char c;
				p = &charfont[b][0];
				for (c = 0; c<8; c++) {
					spi8(((*p++ << offset)&stipplemask) ^ invert);
					stipplemask ^= stipple;
				}
				i += 8;
			}
		}
	}

	for (; i < linelimit; i++) // clear end of line
	{
		spi8(invert);
	}

	if (n == (osd_size-1) && (arrowmask & OSD_ARROW_RIGHT))
	{	// Draw final arrow if needed
		unsigned char c;
		spi24(0x00);
		p = &charfont[0x15][0];
		for (c = 0; c<8; c++) spi8(*p++ << offset);
		p = &charfont[0x11][0];
		for (c = 0; c<8; c++) spi8(*p++ << offset);
		spi24(0x00);
		i += 22;
	}

	// deselect OSD SPI device
	DisableOsd();
}

void OsdDrawLogo(unsigned char n, char row, char superimpose)
{
	unsigned short i;
	const unsigned char *p;
	int linelimit = OSDLINELEN;

	int mag = (osd_size / 8);
	n = n * mag;

	// select buffer and line to write to
	if (!is_minimig())
	{
		spi_osd_cmd_cont(MM1_OSDCMDWRITE | n);
	}
	else
	{
		spi_osd_cmd32_cont(OSD_CMD_OSD_WR, n);
	}

	for (int k = 0; k < mag; k++)
	{
		unsigned char bt = 0;
		const unsigned char *lp = logodata[row];
		int bytes = sizeof(logodata[0]);
		if (row >= (sizeof(logodata) / sizeof(logodata[0]))) lp = 0;

		char *bg = framebuffer[n + k];

		i = 0;
		while(i < linelimit)
		{
			if (i == 0)
			{
				unsigned char j;
				p = &titlebuffer[(osd_size - 1 - n - k) * 8];
				spi16(0xffff);  // left white border
				for (j = 0; j<8; j++) spi_n(255 ^ *p++, 2);
				spi16(0xffff);  // right white border
				spi16(0x0000);  // blue gap
				i += 22;
			}

			if(lp && bytes)
			{
				bt = *lp++;
				if(mag > 1)
				{
					if (k) bt >>= 4;
					bt = (bt & 1) | ((bt & 1) << 1) | ((bt & 2) << 1) | ((bt & 2) << 2) | ((bt & 4) << 2) | ((bt & 4) << 3) | ((bt & 8) << 3) | ((bt & 8) << 4);
				}
				bytes--;
			}

			spi8(bt | *bg++);
			++i;
		}
	}

	// deselect OSD SPI device
	DisableOsd();
}

// write a null-terminated string <s> to the OSD buffer starting at line <n>
void OSD_PrintText(unsigned char line, char *text, unsigned long start, unsigned long width, unsigned long offset, unsigned char invert)
{
	// line : OSD line number (0-7)
	// text : pointer to null-terminated string
	// start : start position (in pixels)
	// width : printed text length in pixels
	// offset : scroll offset in pixels counting from the start of the string (0-7)
	// invert : invertion flag

	const unsigned char *p;
	int i, j;

	// select buffer and line to write to
	if (!is_minimig())
		spi_osd_cmd_cont(MM1_OSDCMDWRITE | line);
	else
		spi_osd_cmd32_cont(OSD_CMD_OSD_WR, line);

	if (invert)
		invert = 0xff;

	p = &titlebuffer[(osd_size - 1 - line) * 8];
	if (start>2) {
		spi16(0xffff);
		start -= 2;
	}

	i = start>16 ? 16 : start;
	for (j = 0; j<(i / 2); ++j)
		spi_n(255 ^ *p++, 2);

	if (i & 1)
		spi8(255 ^ *p);
	start -= i;

	if (start>2) {
		spi16(0xffff);
		start -= 2;
	}

	while (start--)
		spi8(0x00);

	if (offset) {
		width -= 8 - offset;
		p = &charfont[*text++][offset];
		for (; offset < 8; offset++)
			spi8(*p++^invert);
	}

	while (width > 8) {
		unsigned char b;
		p = &charfont[*text++][0];
		for (b = 0; b<8; b++) spi8(*p++^invert);
		width -= 8;
	}

	if (width) {
		p = &charfont[*text++][0];
		while (width--)
			spi8(*p++^invert);
	}

	DisableOsd();
}

// clear OSD frame buffer
void OsdClear(void)
{
	// select buffer to write to
	if (!is_minimig())
		spi_osd_cmd_cont(MM1_OSDCMDWRITE | 0x18);
	else
		spi_osd_cmd32_cont(OSD_CMD_OSD_WR, 0x18);

	// clear buffer
	spi_n(0x00, OSDLINELEN * OSDNLINE);

	// deselect OSD SPI device
	DisableOsd();
}

// enable displaying of OSD
void OsdEnable(unsigned char mode)
{
	user_io_osd_key_enable(mode & DISABLE_KEYBOARD);

	if (!is_minimig())
		spi_osd_cmd(MM1_OSDCMDENABLE | (mode & DISABLE_KEYBOARD));
	else
		spi_osd_cmd8(OSD_CMD_OSD, 0x01 | (mode & DISABLE_KEYBOARD));
}

// disable displaying of OSD
void OsdDisable(void)
{
	user_io_osd_key_enable(0);

	if (!is_minimig())
		spi_osd_cmd(MM1_OSDCMDDISABLE);
	else
		spi_osd_cmd8(OSD_CMD_OSD, 0x00);
}


void MM1_ConfigFilter(unsigned char lores, unsigned char hires) {
	spi_osd_cmd(MM1_OSDCMDCFGFLT | ((hires & 0x03) << 2) | (lores & 0x03));
}

void ConfigVideo(unsigned char hires, unsigned char lores, unsigned char scanlines)
{
	spi_osd_cmd16(OSD_CMD_VID, (((scanlines >> 6) & 0x03) << 10) | (((scanlines >> 4) & 0x03) << 8) | (((scanlines >> 2) & 0x03) << 6) | ((hires & 0x03) << 4) | ((lores & 0x03) << 2) | (scanlines & 0x03));
}

void ConfigAudio(unsigned char audio)
{
	spi_osd_cmd8(OSD_CMD_AUD, audio);
}

void ConfigMemory(unsigned char memory)
{
	spi_osd_cmd8(OSD_CMD_MEM, memory);
}

void ConfigCPU(unsigned char cpu)
{
	spi_osd_cmd8(OSD_CMD_CPU, cpu & 0x0f);
}

void ConfigChipset(unsigned char chipset)
{
	spi_osd_cmd8(OSD_CMD_CHIP, chipset & 0x1f);
}

void ConfigFloppy(unsigned char drives, unsigned char speed)
{
	spi_osd_cmd8(OSD_CMD_FLP, ((drives & 0x03) << 2) | (speed & 0x03));
}

void MM1_ConfigScanlines(unsigned char scanlines)
{
	spi_osd_cmd(MM1_OSDCMDCFGSCL | (scanlines & 0x0F));
}

void ConfigAutofire(unsigned char autofire, unsigned char mask)
{
	uint16_t param = mask;
	param = (param << 8) | autofire;
	spi_osd_cmd16(OSD_CMD_JOY, param);
}

void ScrollText(char n, const char *str, int off, int len, int max_len, unsigned char invert)
{
	// this function is called periodically when a string longer than the window is displayed.

#define BLANKSPACE 10 // number of spaces between the end and start of repeated name

	char s[40];
	long offset;
	if (!max_len) max_len = 30;

	if (str && str[0] && CheckTimer(scroll_timer)) // scroll if long name and timer delay elapsed
	{
		scroll_timer = GetTimer(SCROLL_DELAY2); // reset scroll timer to repeat delay

		scroll_offset++; // increase scroll position (1 pixel unit)
		memset(s, ' ', 32); // clear buffer

		if (!len) len = strlen(str); // get name length

		if (off+len > max_len) // scroll name if longer than display size
		{
			// reset scroll position if it exceeds predefined maximum
			if (scroll_offset >= (len + BLANKSPACE) << 3) scroll_offset = 0;

			offset = scroll_offset >> 3; // get new starting character of the name (scroll_offset is no longer in 2 pixel unit)
			len -= offset; // remaining number of characters in the name
			if (len>max_len) len = max_len;
			if (len > 0) strncpy(s, &str[offset], len); // copy name substring

			if (len < max_len - BLANKSPACE) // file name substring and blank space is shorter than display line size
			{
				strncpy(s + len + BLANKSPACE, str, max_len - len - BLANKSPACE); // repeat the name after its end and predefined number of blank space
			}

			OSD_PrintText(n, s, 22, (max_len - 1) << 3, (scroll_offset & 0x7), invert); // OSD print function with pixel precision
		}
	}
}

void ScrollReset()
{
	scroll_timer = GetTimer(SCROLL_DELAY); // set timer to start name scrolling after predefined time delay
	scroll_offset = 0; // start scrolling from the start
}

/* core currently loaded */
static char lastcorename[261 + 10] = "CORE";
void OsdCoreNameSet(const char* str)
{
	sprintf(lastcorename, "%s", str);
}

char* OsdCoreName()
{
	return lastcorename;
}
