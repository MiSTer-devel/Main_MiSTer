#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "minimig.h"
#include "akiko_cd32.h"
#include "cdtv_cd.h"
#include "../../user_io.h"
#include "../../hardware.h"
#include "../../input.h"
#include "../../video.h"
#include "../../menu.h"

// 16 byte fifo for amiga key codes to limit max key rate sent into the core
#define KBD_FIFO_SIZE  16   // must be power of 2
static unsigned short kbd_fifo[KBD_FIFO_SIZE];
static unsigned char kbd_fifo_r = 0, kbd_fifo_w = 0;
static long kbd_timer = 0;

static void kbd_fifo_minimig_send(uint32_t code)
{
	spi_uio_cmd8((code & OSD) ? UIO_KBD_OSD : UIO_KEYBOARD, code & 0xff);
	kbd_timer = GetTimer(10);  // next key after 10ms earliest
}

static void kbd_fifo_enqueue(unsigned short code)
{
	// if fifo full just drop the value. This should never happen
	if (((kbd_fifo_w + 1)&(KBD_FIFO_SIZE - 1)) == kbd_fifo_r)
		return;

	// store in queue
	kbd_fifo[kbd_fifo_w] = code;
	kbd_fifo_w = (kbd_fifo_w + 1)&(KBD_FIFO_SIZE - 1);
}

// send pending bytes if timer has run up
static void kbd_fifo_poll()
{
	// timer enabled and runnig?
	if (kbd_timer && !CheckTimer(kbd_timer))
		return;

	kbd_timer = 0;  // timer == 0 means timer is not running anymore

	if (kbd_fifo_w == kbd_fifo_r)
		return;

	kbd_fifo_minimig_send(kbd_fifo[kbd_fifo_r]);
	kbd_fifo_r = (kbd_fifo_r + 1)&(KBD_FIFO_SIZE - 1);
}

static uint32_t res_timer = 0;
static char caps_lock_toggle = 0; // keep state of caps lock

void minimig_send_keycode(unsigned short key, int press)
{
	if (press > 1) return;

	uint32_t code = get_amiga_code(key);
	if (code == NONE) return;

	if (code & CAPS_TOGGLE)
	{
		if (press)
		{
			// send alternating make and break codes for caps lock
			if (caps_lock_toggle) code |= 0x80;
			caps_lock_toggle ^= HID_LED_CAPS_LOCK;
			user_io_set_kbd_led(HID_LED_CAPS_LOCK, caps_lock_toggle);
		}
		else
		{
			return;
		}
	}
	else
	{
		// amiga has "break" marker in msb
		if (!press) code |= 0x80;
	}

	code &= 0xff;
	if (minimig_get_adjust())
	{
		if (code == 0x44)
		{
			minimig_set_adjust(0);
			res_timer = 0;
			return;
		}

		if (code == 0x45)
		{
			Info("Canceled");
			res_timer = 0;
			minimig_set_adjust(2);
			return;
		}
		code |= OSD;
	}

	// send immediately if possible
	if (CheckTimer(kbd_timer) && (kbd_fifo_w == kbd_fifo_r))
	{
		kbd_fifo_minimig_send(code);
	}
	else
	{
		kbd_fifo_enqueue(code);
	}
	return;
}

static unsigned long rtc_timer = 0;
void minimig_rtc_reset()
{
	rtc_timer = 0;
}

static void minimig_poll()
{
	//HDD & FDD query
	unsigned char  c1, c2;
	EnableFpga();
	uint16_t tmp = spi_w(0);
	c1 = (uint8_t)(tmp >> 8); // cmd request and drive number
	c2 = (uint8_t)tmp;      // track number
	spi_w(0);
	spi_w(0);
	DisableFpga();
	sysled_enable(0);
	HandleFDD(c1, c2);
	sysled_enable(1);

	uint16_t sd_req = ide_check();
	ide_io(0, sd_req & 7);
	ide_io(1, (sd_req >> 3) & 7);
	if (sd_req & 0x0100) ide_cdda_send_sector();
	UpdateDriveStatus();

	if (is_minimig() == 2)
	{
		akiko_cd32_poll();
		cdtv_cd_poll();
	}

	kbd_fifo_poll();

	if (!rtc_timer || CheckTimer(rtc_timer))
	{
		// Update once per minute should be enough
		rtc_timer = GetTimer(60000);
		user_io_send_rtc(1);
	}

	minimig_share_poll();
	a2065_poll();

	if (!res_timer || CheckTimer(res_timer))
	{
		res_timer = GetTimer(500);
		if (!minimig_get_adjust())
		{
			if (is_minimig()) minimig_adjust_vsize(0);
			video_mode_adjust();
		}
	}
}

char is_minimig()
{
	static int is_minimig_type = 0;

	if (!is_minimig_type && user_io_get_core_name(1)[0])
	{
		is_minimig_type = (!strcasecmp(user_io_get_core_name(1), "minimig")) ? 1 : 2;
		if (is_minimig_type == 1)
		{
			core_poll = minimig_poll;
			uint16_t res = spi_uio_cmd(UIO_GET_VMODE);
			if (res == 1) is_minimig_type = 3;
		}
	}

	return (is_minimig_type == 1) ? 1 : (is_minimig_type == 3) ? 2 : 0;
}
