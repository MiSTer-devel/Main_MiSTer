//============================================================================
//
//  CEC implementation for MiSTer
//  (C) 2026 misteraddons
//  (C) 2026 Alexey Melnikov
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
//  more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
//============================================================================

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>

#include "cfg.h"
#include "hardware.h"
#include "user_io.h"
#include "input.h"
#include "smbus.h"
#include "video.h"
#include "menu.h"

#define ADV7513_MAIN_ADDR        0x39
#define ADV7513_CEC_ADDR         0x3C

#define MAIN_REG_CEC_I2C_ADDR    0xE1
#define MAIN_REG_CEC_POWER       0xE2
#define MAIN_REG_CEC_CTRL        0xE3
#define MAIN_REG_INT1_ENABLE     0x95
#define MAIN_REG_INT1_STATUS     0x97

#define CEC_REG_TX_FRAME_HEADER      0x00
#define CEC_REG_TX_FRAME_DATA0       0x01
#define CEC_REG_TX_FRAME_LENGTH      0x10
#define CEC_REG_TX_ENABLE            0x11
#define CEC_REG_TX_RETRY             0x12
#define CEC_REG_TX_COUNTER           0x14
#define CEC_REG_RX1_FRAME_HEADER     0x15
#define CEC_REG_RX2_FRAME_HEADER     0x27
#define CEC_REG_RX3_FRAME_HEADER     0x38
#define CEC_REG_RX1_FRAME_LENGTH     0x25
#define CEC_REG_RX2_FRAME_LENGTH     0x37
#define CEC_REG_RX3_FRAME_LENGTH     0x48
#define CEC_REG_RX_STATUS            0x26
#define CEC_REG_RX_READY             0x49
#define CEC_REG_RX_BUFFERS           0x4A
#define CEC_REG_LOG_ADDR_MASK        0x4B
#define CEC_REG_LOG_ADDR_0_1         0x4C
#define CEC_REG_LOG_ADDR_2           0x4D
#define CEC_REG_CLK_DIV              0x4E
#define CEC_REG_SOFT_RESET           0x50

#define CEC_INT_RX_RDY1              (1 << 0)
#define CEC_INT_RX_RDY2              (1 << 1)
#define CEC_INT_RX_RDY3              (1 << 2)
#define CEC_INT_RX_RDY_MASK          (CEC_INT_RX_RDY1 | CEC_INT_RX_RDY2 | CEC_INT_RX_RDY3)
#define CEC_INT_TX_RETRY_TIMEOUT     (1 << 3)
#define CEC_INT_TX_ARBITRATION       (1 << 4)
#define CEC_INT_TX_DONE              (1 << 5)
#define CEC_INT_TX_MASK              (CEC_INT_TX_RETRY_TIMEOUT | CEC_INT_TX_ARBITRATION | CEC_INT_TX_DONE)

#define CEC_OPCODE_IMAGE_VIEW_ON            0x04
#define CEC_OPCODE_TEXT_VIEW_ON             0x0D
#define CEC_OPCODE_STANDBY                  0x36
#define CEC_OPCODE_USER_CONTROL_PRESSED     0x44
#define CEC_OPCODE_USER_CONTROL_RELEASED    0x45
#define CEC_OPCODE_GIVE_OSD_NAME            0x46
#define CEC_OPCODE_SET_OSD_NAME             0x47
#define CEC_OPCODE_ROUTING_CHANGE           0x80
#define CEC_OPCODE_ACTIVE_SOURCE            0x82
#define CEC_OPCODE_GIVE_PHYSICAL_ADDRESS    0x83
#define CEC_OPCODE_REPORT_PHYSICAL_ADDRESS  0x84
#define CEC_OPCODE_REQUEST_ACTIVE_SOURCE    0x85
#define CEC_OPCODE_SET_STREAM_PATH          0x86
#define CEC_OPCODE_DEVICE_VENDOR_ID         0x87
#define CEC_OPCODE_GIVE_DEVICE_VENDOR_ID    0x8C
#define CEC_OPCODE_MENU_REQUEST             0x8D
#define CEC_OPCODE_MENU_STATUS              0x8E
#define CEC_OPCODE_GIVE_DEVICE_POWER_STATUS 0x8F
#define CEC_OPCODE_REPORT_POWER_STATUS      0x90
#define CEC_OPCODE_CEC_VERSION              0x9E
#define CEC_OPCODE_GET_CEC_VERSION          0x9F

#define CEC_USER_CONTROL_SELECT             0x00
#define CEC_USER_CONTROL_UP                 0x01
#define CEC_USER_CONTROL_DOWN               0x02
#define CEC_USER_CONTROL_LEFT               0x03
#define CEC_USER_CONTROL_RIGHT              0x04
#define CEC_USER_CONTROL_ROOT_MENU          0x09
#define CEC_USER_CONTROL_SETUP_MENU         0x0A
#define CEC_USER_CONTROL_CONTENTS_MENU      0x0B
#define CEC_USER_CONTROL_FAVORITE_MENU      0x0C
#define CEC_USER_CONTROL_EXIT               0x0D
#define CEC_USER_CONTROL_MEDIA_TOP_MENU     0x10
#define CEC_USER_CONTROL_MEDIA_CONTEXT_MENU 0x11
#define CEC_USER_CONTROL_NUMBER_0           0x20
#define CEC_USER_CONTROL_NUMBER_1           0x21
#define CEC_USER_CONTROL_NUMBER_2           0x22
#define CEC_USER_CONTROL_NUMBER_3           0x23
#define CEC_USER_CONTROL_NUMBER_4           0x24
#define CEC_USER_CONTROL_NUMBER_5           0x25
#define CEC_USER_CONTROL_NUMBER_6           0x26
#define CEC_USER_CONTROL_NUMBER_7           0x27
#define CEC_USER_CONTROL_NUMBER_8           0x28
#define CEC_USER_CONTROL_NUMBER_9           0x29
#define CEC_USER_CONTROL_PAGE_UP            0x30
#define CEC_USER_CONTROL_PAGE_DN            0x31
#define CEC_USER_CONTROL_INPUT_SELECT       0x34
#define CEC_USER_CONTROL_DISPLAY_INFO       0x35
#define CEC_USER_CONTROL_HELP               0x36
#define CEC_USER_CONTROL_PLAY               0x44
#define CEC_USER_CONTROL_STOP               0x45
#define CEC_USER_CONTROL_PAUSE              0x46
#define CEC_USER_CONTROL_REWIND             0x48
#define CEC_USER_CONTROL_FF                 0x49
#define CEC_USER_CONTROL_EPG                0x53
#define CEC_USER_CONTROL_F1_BLUE            0x71
#define CEC_USER_CONTROL_F2_RED             0x72
#define CEC_USER_CONTROL_F3_GREEN           0x73
#define CEC_USER_CONTROL_F4_YELLOW          0x74

#define CEC_LOG_ADDR_TV          0
#define CEC_LOG_ADDR_PLAYBACK1   4
#define CEC_LOG_ADDR_PLAYBACK2   8
#define CEC_LOG_ADDR_PLAYBACK3   11
#define CEC_LOG_ADDR_TUNER1      3
#define CEC_LOG_ADDR_TUNER2      6
#define CEC_LOG_ADDR_TUNER3      7
#define CEC_LOG_ADDR_FREEUSE     14
#define CEC_LOG_ADDR_BROADCAST   15

#define CEC_DEVICE_TYPE_PLAYBACK 4
#define CEC_DEVICE_TYPE_TUNER    3

#define CEC_VERSION_1_4          5

#define CEC_DEVICE_TYPE_MISTER   CEC_DEVICE_TYPE_PLAYBACK
#define CEC_LOG_ADDR_MISTER1     CEC_LOG_ADDR_PLAYBACK1
#define CEC_LOG_ADDR_MISTER2     CEC_LOG_ADDR_PLAYBACK2
#define CEC_LOG_ADDR_MISTER3     CEC_LOG_ADDR_PLAYBACK3
#define CEC_DEFAULT_OSD_NAME     "MiSTer"

#define CEC_INVALID_PHYS_ADDR      0xFFFF
#define CEC_BUTTON_TIMEOUT_MS      500
#define CEC_ADVERTISE_STEP_MS      120
#define CEC_POWER_ON_QUERY_WAIT_MS 700
#define CEC_POWER_ON_STEP_MS       120
#define CEC_TX_TIMEOUT_MS          220
#define CEC_TX_TIMEOUT_RETRY_MS    500

#define CEC_ADVERTISE_STARTUP_ATTEMPTS 1
#define CEC_ADVERTISE_IDENTITY_STEPS   3

typedef struct
{
	uint8_t header;
	uint8_t opcode;
	uint8_t data[14];
	uint8_t length;
} cec_message_t;

enum cec_tx_result_t
{
	CEC_TX_RESULT_OK = 0,
	CEC_TX_RESULT_NACK,
	CEC_TX_RESULT_TIMEOUT
};

enum cec_power_on_state_t
{
	CEC_POWER_ON_DONE = 0,
	CEC_POWER_ON_REQUEST_BEFORE,
	CEC_POWER_ON_WAIT_BEFORE,
	CEC_POWER_ON_IMAGE,
	CEC_POWER_ON_ACTIVE,
};

enum cec_tx_state_t {
	CEC_STATE_IDLE,
	CEC_STATE_WAITING_TX
};

static bool cec_enabled = false;
static int cec_main_fd = -1;
static int cec_fd = -1;

static uint8_t cec_logical_addr = CEC_LOG_ADDR_FREEUSE;
static uint16_t cec_physical_addr = CEC_INVALID_PHYS_ADDR;
static uint16_t cec_active_physical_addr = CEC_INVALID_PHYS_ADDR;
static uint16_t cec_pressed_key = 0;
static uint8_t cec_advertise_step = 0;
static uint8_t cec_advertise_attempts = 0;
static uint8_t cec_power_on_state = CEC_POWER_ON_DONE;
static uint32_t cec_input_activity_seq = 0;
static bool cec_idle_engaged = false;
static uint8_t cec_tx_fail_streak = 0;
static uint8_t cec_current_tx_state = CEC_STATE_IDLE;
static uint8_t cec_low_drv_start = 0;
static int edid_version = -1;
static bool cec_can_try = false;

static unsigned long cec_press_deadline = 0;
static unsigned long cec_advertise_deadline = 0;
static unsigned long cec_power_on_deadline = 0;
static unsigned long cec_idle_deadline = 0;
static unsigned long cec_tx_timeout_deadline = 0;
static unsigned long cec_retry_deadline = 0;

static unsigned long cec_idle_sleep_delay_ms(void)
{
	// Tie CEC sleep/wake to the existing hdmi_off deadline.
	if (!cfg.hdmi_off) return 0;

	unsigned long minutes = (unsigned long)cfg.hdmi_off;
	return minutes * 60ul * 1000ul;
}

static const char *cec_get_osd_name(void)
{
	return CEC_DEFAULT_OSD_NAME;
}

static uint8_t cec_reg_read(uint8_t reg)
{
	if (cec_fd < 0) return 0;

	int value = i2c_smbus_read_byte_data(cec_fd, reg);
	return (value < 0) ? 0 : (uint8_t)value;
}

static bool cec_reg_write(uint8_t reg, uint8_t value)
{
	if (cec_fd < 0) return false;
	return i2c_smbus_write_byte_data(cec_fd, reg, value) >= 0;
}

static uint8_t main_reg_read(uint8_t reg)
{
	if (cec_main_fd < 0) return 0;

	int value = i2c_smbus_read_byte_data(cec_main_fd, reg);
	return (value < 0) ? 0 : (uint8_t)value;
}

static bool main_reg_write(uint8_t reg, uint8_t value)
{
	if (cec_main_fd < 0) return false;
	return i2c_smbus_write_byte_data(cec_main_fd, reg, value) >= 0;
}

static void cec_release_key(void)
{
	if (!cec_pressed_key) return;

	user_io_kbd(cec_pressed_key, 0);
	cec_pressed_key = 0;
	cec_press_deadline = 0;
}

static void cec_handle_button(uint8_t button_code, bool pressed)
{
	if (is_menu()) printf("CEC button: 0x%02X, pressed=%d\n", button_code, pressed);
	if (!cfg.hdmi_cec_input_mode) return;

	if (!pressed)
	{
		cec_release_key();
		return;
	}

	uint16_t key;

	switch (button_code)
	{
		// main buttons
		case CEC_USER_CONTROL_UP:        key = KEY_UP; break;
		case CEC_USER_CONTROL_DOWN:      key = KEY_DOWN; break;
		case CEC_USER_CONTROL_LEFT:      key = KEY_LEFT; break;
		case CEC_USER_CONTROL_RIGHT:     key = KEY_RIGHT; break;
		case CEC_USER_CONTROL_SELECT:    key = KEY_ENTER; break;
		case CEC_USER_CONTROL_ROOT_MENU:
		case CEC_USER_CONTROL_EXIT:      key = menu_present() ? KEY_BACK : KEY_MENU; break;

		// additional buttons, may not present nor passed to CEC
		case CEC_USER_CONTROL_PLAY:
		case CEC_USER_CONTROL_PAUSE:     key = KEY_SPACE; break;
		case CEC_USER_CONTROL_STOP:      key = KEY_ESC; break;
		case CEC_USER_CONTROL_REWIND:    key = KEY_BACKSPACE; break;
		case CEC_USER_CONTROL_FF:        key = KEY_TAB; break;
		case CEC_USER_CONTROL_NUMBER_0:  key = KEY_0; break;
		case CEC_USER_CONTROL_NUMBER_1:  key = KEY_1; break;
		case CEC_USER_CONTROL_NUMBER_2:  key = KEY_2; break;
		case CEC_USER_CONTROL_NUMBER_3:  key = KEY_3; break;
		case CEC_USER_CONTROL_NUMBER_4:  key = KEY_4; break;
		case CEC_USER_CONTROL_NUMBER_5:  key = KEY_5; break;
		case CEC_USER_CONTROL_NUMBER_6:  key = KEY_6; break;
		case CEC_USER_CONTROL_NUMBER_7:  key = KEY_7; break;
		case CEC_USER_CONTROL_NUMBER_8:  key = KEY_8; break;
		case CEC_USER_CONTROL_NUMBER_9:  key = KEY_9; break;
		case CEC_USER_CONTROL_F1_BLUE:   key = menu_present() ? KEY_F11 : KEY_F1; break;
		case CEC_USER_CONTROL_F2_RED:    key = menu_present() ? KEY_MENU : KEY_F2; break;
		case CEC_USER_CONTROL_F3_GREEN:  key = menu_present() ? KEY_MINUS : KEY_F3; break;
		case CEC_USER_CONTROL_F4_YELLOW: key = menu_present() ? KEY_EQUAL : KEY_F4; break;
		case CEC_USER_CONTROL_PAGE_UP:   key = menu_present() ? KEY_EQUAL : KEY_PAGEUP; break;
		case CEC_USER_CONTROL_PAGE_DN:   key = menu_present() ? KEY_MINUS : KEY_PAGEDOWN; break;

		default: return;
	}

	if (cec_pressed_key && cec_pressed_key != key)
	{
		cec_release_key();
	}

	if (!cec_pressed_key)
	{
		user_io_kbd(key, 1);
		cec_pressed_key = key;
	}

	cec_press_deadline = GetTimer(CEC_BUTTON_TIMEOUT_MS);
}

static void handle_tx_result(cec_tx_result_t tx_res)
{
	if (tx_res == CEC_TX_RESULT_OK)
	{
		cec_tx_fail_streak = 0;
	}
	else if (tx_res == CEC_TX_RESULT_NACK)
	{
		if (cec_tx_fail_streak < 255) cec_tx_fail_streak++;
		if (cec_tx_fail_streak >= 8)
		{
			cec_tx_fail_streak = 0;
			printf("CEC: TX suppressed after repeated failures\n");
		}
	}
	cec_current_tx_state = CEC_STATE_IDLE; // Free the interface for next messages
}

void cec_poll_tx()
{
	if (cec_current_tx_state != CEC_STATE_WAITING_TX) return;

	uint8_t status = main_reg_read(MAIN_REG_INT1_STATUS);

	if (status & (CEC_INT_TX_RETRY_TIMEOUT | CEC_INT_TX_ARBITRATION))
	{
		cec_reg_write(CEC_REG_TX_ENABLE, 0x00);
		main_reg_write(MAIN_REG_INT1_STATUS, status & CEC_INT_TX_MASK);
		handle_tx_result(CEC_TX_RESULT_NACK);
		return;
	}

	if (status & CEC_INT_TX_DONE)
	{
		cec_reg_write(CEC_REG_TX_ENABLE, 0x00);
		main_reg_write(MAIN_REG_INT1_STATUS, CEC_INT_TX_DONE);
		handle_tx_result(CEC_TX_RESULT_OK);
		return;
	}

	uint8_t tx_en = cec_reg_read(CEC_REG_TX_ENABLE);
	uint8_t low_drv_now = cec_reg_read(CEC_REG_TX_COUNTER);

	if (low_drv_now != cec_low_drv_start && tx_en == 0)
	{
		handle_tx_result(CEC_TX_RESULT_OK);
		return;
	}

	if (CheckTimer(cec_tx_timeout_deadline))
	{
		uint8_t low_drv_end = cec_reg_read(CEC_REG_TX_COUNTER);
		if (low_drv_end != cec_low_drv_start)
		{
			handle_tx_result(CEC_TX_RESULT_OK);
			return;
		}

		cec_reg_write(CEC_REG_TX_ENABLE, 0x00);
		main_reg_write(MAIN_REG_INT1_STATUS, CEC_INT_TX_MASK);
		handle_tx_result(CEC_TX_RESULT_TIMEOUT);
	}
}

static bool cec_send_message(const cec_message_t *msg, bool with_retry = true)
{
	if (!cec_enabled || !msg) return false;
	if (msg->length < 1 || msg->length > 16) return false;
	if (cec_current_tx_state != CEC_STATE_IDLE) return false;

	uint8_t src = (msg->header >> 4) & 0x0F;
	uint8_t dst = msg->header & 0x0F;
	printf("CEC send: %lu message 0x%02X, src=%d, dst=%d\n", GetTimer(0), msg->opcode, src, dst);

	cec_reg_write(CEC_REG_TX_ENABLE, 0x00);
	main_reg_write(MAIN_REG_INT1_STATUS, CEC_INT_TX_MASK);

	cec_reg_write(CEC_REG_TX_FRAME_HEADER, msg->header);
	if (msg->length > 1)
	{
		cec_reg_write(CEC_REG_TX_FRAME_DATA0, msg->opcode);
		for (uint8_t i = 0; i < (msg->length - 2); i++)
		{
			cec_reg_write(CEC_REG_TX_FRAME_DATA0 + 1 + i, msg->data[i]);
		}
	}

	cec_reg_write(CEC_REG_TX_FRAME_LENGTH, msg->length);
	cec_reg_write(CEC_REG_TX_RETRY, with_retry ? 0x20 : 0x00);

	// Cache parameters for the polling worker
	cec_low_drv_start = cec_reg_read(CEC_REG_TX_COUNTER);
	cec_tx_timeout_deadline = GetTimer(with_retry ? CEC_TX_TIMEOUT_RETRY_MS : CEC_TX_TIMEOUT_MS);
	cec_current_tx_state = CEC_STATE_WAITING_TX;

	cec_reg_write(CEC_REG_TX_ENABLE, 0x01);

	return true; // Successfully accepted into the pipeline
}

static uint8_t cec_pick_logical_address_from_physical(uint16_t physical_addr)
{
	uint8_t port = (physical_addr >> 12) & 0x0F;
	if (port == 2) return CEC_LOG_ADDR_MISTER2;
	if (port >= 3) return CEC_LOG_ADDR_MISTER3;
	return CEC_LOG_ADDR_MISTER1;
}

static void cec_program_logical_address(uint8_t addr)
{
	cec_logical_addr = addr & 0x0F;
	cec_reg_write(CEC_REG_LOG_ADDR_MASK, 0x10);
	cec_reg_write(CEC_REG_LOG_ADDR_0_1, (uint8_t)((0x0F << 4) | cec_logical_addr));
	cec_reg_write(CEC_REG_LOG_ADDR_2, 0x0F);
}

static void cec_clear_rx_buffers(void)
{
	cec_reg_write(CEC_REG_RX_BUFFERS, 0x0F);
	cec_reg_write(CEC_REG_RX_BUFFERS, 0x08);
}

static bool cec_setup_main_registers()
{
	bool ok = true;

	ok &= main_reg_write(MAIN_REG_CEC_I2C_ADDR, ADV7513_CEC_ADDR << 1);
	ok &= main_reg_write(MAIN_REG_CEC_POWER, 0x00);
	ok &= main_reg_write(MAIN_REG_INT1_ENABLE, CEC_INT_RX_RDY_MASK | CEC_INT_TX_MASK);
	ok &= main_reg_write(MAIN_REG_INT1_STATUS, 0xFF);

	if (!ok)
	{
		printf("CEC: main register setup failed\n");
	}

	return ok;
}

static void cec_clock_set(int cec_clock)
{
	static struct {
		uint8_t addr;
		float value;
	} regs[] = {
		{0x51, 4500}, {0x53, 4200}, {0x55, 4800}, {0x57, 3700},
		{0x59, 3400}, {0x5B, 4000}, {0x5D, 2400}, {0x5F, 1950},
		{0x61, 2850}, {0x63,  600}, {0x65, 1500}, {0x67, 1800},
		{0x69, 1050}, {0x6B, 3600}, {0x6E, 250 }, {0x71,  300},
		{0x73,  900}, {0x75, 1200}
	};

	const int   base = (cec_clock < 45000) ? 750 : (cec_clock < 60000) ? 1000 : 2000;
	const int   clk_div = (cec_clock / base) - 1;
	const float us = 1000.f * (clk_div + 1) / (float)cec_clock;

	printf("cec_clock_set: clock=%d, base=%d, clk_div=%d, us=%.2f\n", cec_clock, base, clk_div, us);

	cec_reg_write(CEC_REG_CLK_DIV, clk_div << 2);
	for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++)
	{
		uint8_t  reg = regs[i].addr;
		uint16_t value = (uint16_t)(regs[i].value / us);

		cec_reg_write(reg, value >> 8);
		cec_reg_write(reg + 1, value);
	}
	cec_reg_write(0x4F, cec_clock / 1714);
	cec_reg_write(CEC_REG_CLK_DIV, (clk_div << 2) | 1);
}

static uint32_t cec_test()
{
	uint32_t t0, t1;
	bool finished = false;

	t0 = GetTimer(0);
	cec_reg_write(CEC_REG_TX_ENABLE, 0x01);

	while (1)
	{
		t1 = GetTimer(0) - t0;

		uint8_t status = main_reg_read(MAIN_REG_INT1_STATUS);
		if (status & CEC_INT_TX_MASK)
		{
			finished = true;
			break;
		}

		if (t1 >= 150) break;
		usleep(1000);
	}

	cec_reg_write(CEC_REG_TX_ENABLE, 0x00);
	main_reg_write(MAIN_REG_INT1_STATUS, CEC_INT_TX_MASK);

	printf("CEC: clock probe TX elapsed=%u finished=%d\n", t1, finished);
	return finished ? t1 : 0;
}

static int cec_detect_clock(int proposed_clock)
{
	static int cec_clock = -1;
	if (cec_clock >= 0) return cec_clock;

	cec_clock_set(12000);

	// set a temporary logical address so the chip can drive the bus
	cec_program_logical_address(CEC_LOG_ADDR_FREEUSE);

	main_reg_write(MAIN_REG_INT1_STATUS, CEC_INT_TX_MASK);
	cec_reg_write(CEC_REG_TX_FRAME_HEADER, (cec_logical_addr << 4) | cec_logical_addr);
	cec_reg_write(CEC_REG_TX_FRAME_LENGTH, 1);
	cec_reg_write(CEC_REG_TX_RETRY, 0x00);

	uint32_t t0 = cec_test();
	if (!t0)
	{
		printf("CEC: no clock detected. CEC will be disabled. Disable CEC in MiSTer.ini to avoid this probe and delay in startup.\n");
		cec_clock = 0;
	}
	else if (proposed_clock)
	{
		// accept proposed clock if any clock is detected
		cec_clock = proposed_clock;
	}
	else
	{
		// if t0 < 30ms then it's definitely 50MHz.
		if (t0 < 30)
		{
			printf("CEC: detected 50 MHz clock\n");
			cec_clock = 50000;
		}
		else
		{
			//If >=30ms then it can be either due to bus contention or 12MHz, so additional test is required.
			uint32_t t1 = cec_test();
			if (!t1)
			{
				// we should be here
				printf("CEC: no clock detected. CEC will be disabled. Disable CEC in MiSTer.ini to avoid this probe and delay in startup.\n");
				cec_clock = 0;
			}
			else
			{
				t0 = t0 < t1 ? t0 : t1;
				if (t0 < 30)
				{
					printf("CEC: assume 50MHz clock\n");
					cec_clock = 50000;
				}
				else
				{
					printf("CEC: assume 12MHz clock\n");
					cec_clock = 12000;
				}
			}
		}
	}

	return cec_clock;
}

static bool cec_clock()
{
	if (cfg.hdmi_cec_clock && (cfg.hdmi_cec_clock < 3 || cfg.hdmi_cec_clock > 100))
	{
		printf("CEC: clock (%.2f MHz) is outside of supported range 3-100 MHz\n", cfg.hdmi_cec_clock);
		return false;
	}

	printf("cec_clock: hdmi_cec_clock=%.2f\n", cfg.hdmi_cec_clock);

	int clock = cec_detect_clock(cfg.hdmi_cec_clock * 1000);
	if (!clock) return false;

	cec_clock_set(clock);
	return true;
}

static bool cec_parse_physical_address(const uint8_t *edid, size_t size, uint16_t *physical_addr)
{
	if (!edid || !physical_addr || size < 256) return false;

	if (edid[0] != 0x00 || edid[1] != 0xFF || edid[2] != 0xFF || edid[3] != 0xFF ||
		edid[4] != 0xFF || edid[5] != 0xFF || edid[6] != 0xFF || edid[7] != 0x00)
	{
		return false;
	}

	uint8_t ext_count = edid[126];
	for (uint8_t ext = 0; ext < ext_count; ext++)
	{
		size_t blk_off = 128 * (ext + 1);
		if (blk_off + 128 > size) break;

		const uint8_t *blk = &edid[blk_off];
		if (blk[0] != 0x02) continue;

		int dtd_offset = blk[2];
		if (dtd_offset < 4 || dtd_offset > 127) continue;

		int pos = 4;
		while (pos < dtd_offset)
		{
			uint8_t tag_len = blk[pos];
			int tag = (tag_len >> 5) & 0x07;
			int len = tag_len & 0x1F;
			if (pos + 1 + len > 127) break;

			if (tag == 0x03 && len >= 5)
			{
				if (blk[pos + 1] == 0x03 && blk[pos + 2] == 0x0C && blk[pos + 3] == 0x00)
				{
					*physical_addr = (uint16_t)((blk[pos + 4] << 8) | blk[pos + 5]);
					return true;
				}
			}

			pos += len + 1;
		}
	}

	return false;
}

static bool cec_parse_physical_address_loose(const uint8_t *edid, size_t size, uint16_t *physical_addr)
{
	if (!edid || !physical_addr || size < 8) return false;

	for (size_t i = 0; i + 4 < size; i++)
	{
		if (edid[i] == 0x03 && edid[i + 1] == 0x0C && edid[i + 2] == 0x00)
		{
			uint16_t addr = (uint16_t)((edid[i + 3] << 8) | edid[i + 4]);
			if (addr != 0x0000 && addr != 0xFFFF)
			{
				*physical_addr = addr;
				return true;
			}
		}
	}

	return false;
}

static uint16_t cec_read_physical_address()
{
	uint8_t* edid;
	int edid_size;
	uint16_t addr = CEC_INVALID_PHYS_ADDR;

	edid_version = video_get_edid(&edid, &edid_size);
	if (!cec_parse_physical_address(edid, edid_size, &addr))
	{
		cec_parse_physical_address_loose(edid, edid_size, &addr);
	}

	return addr;
}

static const uint8_t cec_rx_hdr_regs[] = { CEC_REG_RX1_FRAME_HEADER, CEC_REG_RX2_FRAME_HEADER, CEC_REG_RX3_FRAME_HEADER };
static const uint8_t cec_rx_len_regs[] = { CEC_REG_RX1_FRAME_LENGTH, CEC_REG_RX2_FRAME_LENGTH, CEC_REG_RX3_FRAME_LENGTH };
static const uint8_t cec_rx_int_bits[] = { CEC_INT_RX_RDY1, CEC_INT_RX_RDY2, CEC_INT_RX_RDY3 };

static bool cec_read_rx_buffer(int index, cec_message_t *msg)
{
	if (!msg || index < 0 || index > 2) return false;

	uint8_t len_raw = cec_reg_read(cec_rx_len_regs[index]);
	uint8_t length = len_raw & 0x1F;
	if (length < 1 || length > 16) return false;

	msg->length = length;
	msg->header = cec_reg_read(cec_rx_hdr_regs[index]);
	msg->opcode = (length > 1) ? cec_reg_read(cec_rx_hdr_regs[index] + 1) : 0;

	for (uint8_t i = 0; i < (length > 2 ? length - 2 : 0); i++)
	{
		msg->data[i] = cec_reg_read(cec_rx_hdr_regs[index] + 2 + i);
	}

	// Release the consumed RX buffer slot back to hardware.
	uint8_t bit = 1 << index;
	cec_reg_write(CEC_REG_RX_BUFFERS, 0x08 | bit);
	usleep(200);
	cec_reg_write(CEC_REG_RX_BUFFERS, 0x08);

	return true;
}

static bool cec_receive_message(cec_message_t *msg)
{
	if (!cec_enabled || !msg) return false;
	if (!fpga_get_hdmi_int()) return false;

	uint8_t rx_bits = cec_reg_read(CEC_REG_RX_READY) & CEC_INT_RX_RDY_MASK;
	if (!rx_bits)
	{
		main_reg_write(MAIN_REG_INT1_STATUS, CEC_INT_RX_RDY_MASK);
		return false;
	}

	uint8_t rx_order = cec_reg_read(CEC_REG_RX_STATUS);
	int selected = -1;
	int oldest = 4;

	for (int i = 0; i < 3; i++)
	{
		if (!(rx_bits & cec_rx_int_bits[i])) continue;

		int order = (rx_order >> (i * 2)) & 0x03;
		if (order > 0 && order < oldest)
		{
			oldest = order;
			selected = i;
		}
	}

	if (selected < 0)
	{
		for (int i = 0; i < 3; i++)
		{
			if (rx_bits & cec_rx_int_bits[i])
			{
				selected = i;
				break;
			}
		}
	}

	if (selected < 0) return false;

	bool ok = cec_read_rx_buffer(selected, msg);
	main_reg_write(MAIN_REG_INT1_STATUS, cec_rx_int_bits[selected]);

	return ok;
}

// assume_when_unknown: when no active source is known, assume it is us.
// Pass false to require a confirmed match (e.g. before skipping a TV wake).
static bool cec_is_active_source(bool assume_when_unknown = true)
{
	if (cec_active_physical_addr == CEC_INVALID_PHYS_ADDR)
	{
		printf("CEC: cec_active_physical_addr=FFFF - no active source%s.\n",
			assume_when_unknown ? ", assuming we are active" : "");
		return assume_when_unknown;
	}
	printf("CEC: cec_active_physical_addr=%04X, cec_physical_addr=%04X\n", cec_active_physical_addr, cec_physical_addr);
	return cec_active_physical_addr == cec_physical_addr;
}

static bool cec_send_active_source(bool with_retry = true)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
	msg.opcode = CEC_OPCODE_ACTIVE_SOURCE;
	msg.data[0] = (uint8_t)(cec_physical_addr >> 8);
	msg.data[1] = (uint8_t)(cec_physical_addr & 0xFF);
	msg.length = 4;
	return cec_send_message(&msg, with_retry);
}

static bool cec_send_request_active_source(bool with_retry = true)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
	msg.opcode = CEC_OPCODE_REQUEST_ACTIVE_SOURCE;
	msg.length = 2;
	return cec_send_message(&msg, with_retry);
}

static bool cec_send_image_view_on(bool with_retry = true)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_TV;
	msg.opcode = CEC_OPCODE_IMAGE_VIEW_ON;
	msg.length = 2;
	return cec_send_message(&msg, with_retry);
}

bool cec_send_standby(void)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
	msg.opcode = CEC_OPCODE_STANDBY;
	msg.length = 2;
	return cec_send_message(&msg);
}

static bool cec_send_report_physical_address(bool with_retry = true)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
	msg.opcode = CEC_OPCODE_REPORT_PHYSICAL_ADDRESS;
	msg.data[0] = (uint8_t)(cec_physical_addr >> 8);
	msg.data[1] = (uint8_t)(cec_physical_addr & 0xFF);
	msg.data[2] = CEC_DEVICE_TYPE_MISTER;
	msg.length = 5;
	return cec_send_message(&msg, with_retry);
}

static bool cec_send_device_name(uint8_t destination, bool with_retry = true)
{
	const char *name = cec_get_osd_name();
	if (!name) return false;

	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | destination;
	msg.opcode = CEC_OPCODE_SET_OSD_NAME;

	size_t len = strlen(name);
	if (len > 14) len = 14;
	for (size_t i = 0; i < len; i++)
	{
		msg.data[i] = (uint8_t)name[i];
	}

	msg.length = (uint8_t)(2 + len);
	return cec_send_message(&msg, with_retry);
}

static bool cec_send_device_vendor_id(bool with_retry = true)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
	msg.opcode = CEC_OPCODE_DEVICE_VENDOR_ID;
	msg.data[0] = 0x00;
	msg.data[1] = 0x0C;
	msg.data[2] = 0x03;
	msg.length = 5;
	return cec_send_message(&msg, with_retry);
}

static bool cec_send_cec_version(uint8_t destination)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | destination;
	msg.opcode = CEC_OPCODE_CEC_VERSION;
	msg.data[0] = CEC_VERSION_1_4;
	msg.length = 3;
	return cec_send_message(&msg);
}

static bool cec_send_power_status(uint8_t destination)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | destination;
	msg.opcode = CEC_OPCODE_REPORT_POWER_STATUS;
	msg.data[0] = 0;
	msg.length = 3;
	return cec_send_message(&msg);
}

static bool cec_send_menu_status(uint8_t destination)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | destination;
	msg.opcode = CEC_OPCODE_MENU_STATUS;
	msg.data[0] = 0x00;
	msg.length = 3;
	return cec_send_message(&msg);
}

static void cec_handle_message(const cec_message_t *msg)
{
	if (!msg || msg->length < 2) return;

	uint8_t src = (msg->header >> 4) & 0x0F;
	uint8_t dst = msg->header & 0x0F;
	if (dst != cec_logical_addr && dst != CEC_LOG_ADDR_BROADCAST) return;

	printf("CEC: %lu message 0x%02X, src=%d, dst=%d\n", GetTimer(0), msg->opcode, src, dst);

	switch (msg->opcode)
	{
	case CEC_OPCODE_GIVE_PHYSICAL_ADDRESS:
		cec_send_report_physical_address();
		break;

	case CEC_OPCODE_GIVE_OSD_NAME:
		cec_send_device_name(src);
		break;

	case CEC_OPCODE_GIVE_DEVICE_VENDOR_ID:
		cec_send_device_vendor_id();
		break;

	case CEC_OPCODE_GET_CEC_VERSION:
		cec_send_cec_version(src);
		break;

	case CEC_OPCODE_GIVE_DEVICE_POWER_STATUS:
		cec_send_power_status(src);
		break;

	case CEC_OPCODE_REQUEST_ACTIVE_SOURCE:
		if (cec_is_active_source()) cec_send_active_source();
		break;

	case CEC_OPCODE_ACTIVE_SOURCE:
		if (msg->length >= 4)
		{
			uint16_t path = (uint16_t)((msg->data[0] << 8) | msg->data[1]);
			cec_active_physical_addr = path;
			cec_is_active_source();
		}
		break;

	case CEC_OPCODE_ROUTING_CHANGE:
		if (msg->length >= 6)
		{
			uint16_t path = (uint16_t)((msg->data[2] << 8) | msg->data[3]);
			cec_active_physical_addr = path;
			if (cec_is_active_source())
			{
				cec_send_active_source();
				user_io_kbd(KEY_RESERVED, 0);
			}
		}
		break;

	case CEC_OPCODE_SET_STREAM_PATH:
		if (msg->length >= 4)
		{
			uint16_t path = (uint16_t)((msg->data[0] << 8) | msg->data[1]);
			cec_active_physical_addr = path;
			if (cec_is_active_source()) cec_send_active_source();
		}
		break;

	case CEC_OPCODE_MENU_REQUEST:
		cec_send_menu_status(src);
		break;

	case CEC_OPCODE_USER_CONTROL_PRESSED:
		if (msg->length >= 3) cec_handle_button(msg->data[0], true);
		break;

	case CEC_OPCODE_USER_CONTROL_RELEASED:
		cec_handle_button(0, false);
		break;

	default:
		break;
	}
}

void cec_deinit(void)
{
	cec_release_key();

	if (cec_fd >= 0)
	{
		cec_reg_write(CEC_REG_TX_ENABLE, 0x00);
		cec_reg_write(CEC_REG_LOG_ADDR_MASK, 0x00);
		i2c_close(cec_fd);
	}

	if (cec_main_fd >= 0)
	{
		main_reg_write(MAIN_REG_INT1_ENABLE, 0x00);
		main_reg_write(MAIN_REG_INT1_STATUS, 0xFF);
		i2c_close(cec_main_fd);
	}

	cec_fd = -1;
	cec_main_fd = -1;
	cec_enabled = false;
	cec_retry_deadline = 0;
	cec_can_try = false;
}

bool cec_init()
{
	if (cec_enabled) return true;

	cec_deinit();

	int adv_bus = -1;
	cec_main_fd = i2c_open(ADV7513_MAIN_ADDR, 0, -1, &adv_bus);
	if (cec_main_fd < 0)
	{
		return false;
	}

	if (!cec_setup_main_registers())
	{
		cec_deinit();
		return false;
	}

	// CEC is a sub-map of the same chip: pin it to the main bus rather than
	// rescanning, so a phantom on another bus can't capture the handle.
	cec_fd = i2c_open(ADV7513_CEC_ADDR, 0, adv_bus);
	if (cec_fd < 0)
	{
		cec_deinit();
		return false;
	}

	cec_reg_write(CEC_REG_SOFT_RESET, 0x01);
	usleep(2000);
	cec_reg_write(CEC_REG_SOFT_RESET, 0x00);

	if (!cec_clock())
	{
		cec_deinit();
		return false;
	}

	cec_reg_write(CEC_REG_TX_ENABLE, 0x00);

	cec_clear_rx_buffers();
	main_reg_write(MAIN_REG_INT1_STATUS, 0xFF);

	cec_tx_fail_streak = 0;
	cec_current_tx_state = CEC_STATE_IDLE;
	cec_can_try = true;

	cec_physical_addr = cec_read_physical_address();
	if (cec_physical_addr == CEC_INVALID_PHYS_ADDR)
	{
		return false;
	}

	cec_program_logical_address(cec_pick_logical_address_from_physical(cec_physical_addr));

	cec_enabled = true;

	printf("CEC: logical=%u physical=%X.%X.%X.%X\n",
		cec_logical_addr,
		(cec_physical_addr >> 12) & 0x0F,
		(cec_physical_addr >> 8) & 0x0F,
		(cec_physical_addr >> 4) & 0x0F,
		cec_physical_addr & 0x0F);

	cec_advertise_step = 0;
	cec_advertise_attempts = CEC_ADVERTISE_STARTUP_ATTEMPTS;
	cec_advertise_deadline = 0;

	cec_power_on_state = CEC_POWER_ON_DONE;
	cec_power_on_deadline = 0;
	if (cfg.hdmi_cec_power_on) cec_power_on_state = CEC_POWER_ON_REQUEST_BEFORE;

	cec_input_activity_seq = user_io_get_activity_seq();
	unsigned long idle_ms = cec_idle_sleep_delay_ms();
	cec_idle_deadline = idle_ms ? GetTimer(idle_ms) : 0;
	cec_idle_engaged = false;

	return true;
}

static void cec_poll_power_on_switch(void)
{
	if (cec_current_tx_state != CEC_STATE_IDLE) return;
	if (cec_power_on_state == CEC_POWER_ON_DONE) return;
	if (!CheckTimer(cec_power_on_deadline)) return;

	switch (cec_power_on_state)
	{
	case CEC_POWER_ON_REQUEST_BEFORE:
		cec_send_request_active_source(false);
		cec_power_on_state = CEC_POWER_ON_WAIT_BEFORE;
		cec_power_on_deadline = GetTimer(CEC_POWER_ON_QUERY_WAIT_MS);
		break;

	case CEC_POWER_ON_WAIT_BEFORE:
		// Unknown source (TV likely asleep) must still get the wake, so don't
		// assume we are active here.
		if (cec_is_active_source(false))
		{
			cec_power_on_state = CEC_POWER_ON_DONE;
			cec_power_on_deadline = 0;
		}
		else
		{
			cec_power_on_state = CEC_POWER_ON_IMAGE;
			cec_power_on_deadline = 0;
		}
		break;

	case CEC_POWER_ON_IMAGE:
		cec_send_image_view_on(false);
		cec_power_on_state = CEC_POWER_ON_ACTIVE;
		cec_power_on_deadline = GetTimer(CEC_POWER_ON_STEP_MS);
		break;

	case CEC_POWER_ON_ACTIVE:
		cec_send_active_source(false);
		cec_power_on_state = CEC_POWER_ON_DONE;
		cec_power_on_deadline = 0;
		break;

	default:
		cec_power_on_state = CEC_POWER_ON_DONE;
		cec_power_on_deadline = 0;
		break;
	}
}


static void cec_poll_idle_sleep_wake(void)
{
	if (cec_current_tx_state != CEC_STATE_IDLE) return;

	// Global idle detector based on real input activity (not just OSD/menu navigation),
	// tied to hdmi_off timing.
	if (!cfg.hdmi_cec_sleep && !cfg.hdmi_cec_wake) return;

	unsigned long delay_ms = cec_idle_sleep_delay_ms();
	if (!delay_ms)
	{
		cec_idle_deadline = 0;
		cec_idle_engaged = false;
		cec_input_activity_seq = user_io_get_activity_seq();
		return;
	}

	if (!cec_idle_deadline)
	{
		cec_input_activity_seq = user_io_get_activity_seq();
		cec_idle_deadline = GetTimer(delay_ms);
	}

	uint32_t seq = user_io_get_activity_seq();
	if (seq != cec_input_activity_seq || !input_state())
	{
		cec_input_activity_seq = seq;
		cec_idle_deadline = GetTimer(delay_ms);

		if (cec_idle_engaged)
		{
			cec_idle_engaged = false;
			if (cfg.hdmi_cec_wake && cec_is_active_source())
			{
				cec_power_on_deadline = 0;
				cec_power_on_state = CEC_POWER_ON_IMAGE;
			}
		}

		return;
	}

	if (!cec_idle_engaged && CheckTimer(cec_idle_deadline))
	{
		cec_idle_engaged = true;
		if (cfg.hdmi_cec_sleep)
		{
			// Avoid powering off the display if MiSTer is the active source.
			if (cec_is_active_source()) cec_send_standby();
		}
	}
}

static void cec_poll_advertise(void)
{
	if (cec_current_tx_state != CEC_STATE_IDLE) return;
	if (!cec_advertise_attempts) return;
	if (!CheckTimer(cec_advertise_deadline)) return;

	switch (cec_advertise_step)
	{
	case 0:
		cec_send_report_physical_address(false);
		break;

	case 1:
		cec_send_device_name(CEC_LOG_ADDR_TV, false);
		break;

	case 2:
		cec_send_request_active_source(false);
		break;
	}

	cec_advertise_step++;
	if (cec_advertise_step >= CEC_ADVERTISE_IDENTITY_STEPS)
	{
		cec_advertise_step = 0;
		cec_advertise_attempts--;
		if (cec_advertise_attempts) cec_advertise_deadline = GetTimer(CEC_ADVERTISE_STEP_MS);
	}
	else
	{
		cec_advertise_deadline = GetTimer(CEC_ADVERTISE_STEP_MS);
	}
}

static void cec_poll_key_deadline(void)
{
	if (cec_pressed_key && CheckTimer(cec_press_deadline))
	{
		cec_release_key();
	}
}

static void cec_poll_messages()
{
	if (cec_current_tx_state != CEC_STATE_IDLE) return;

	cec_message_t msg = {};
	if (cec_receive_message(&msg)) cec_handle_message(&msg);
}

void cec_poll(void)
{
	if (cfg.hdmi_cec)
	{
		if (cec_enabled)
		{
			cec_poll_tx();
			cec_poll_advertise();
			cec_poll_messages();
			cec_poll_power_on_switch();
			cec_poll_idle_sleep_wake();
			cec_poll_key_deadline();

			if (video_get_edid(0, 0) != edid_version)
			{
				cec_deinit();
			}
		}
		else if (CheckTimer(cec_retry_deadline))
		{
			if (!cec_init())
			{
				if (cec_can_try)
				{
					printf("CEC: init failed, retry in 3sec...\n");
					cec_retry_deadline = GetTimer(3000);
				}
				else
				{
					printf("CEC: init failed.\n");
					cfg.hdmi_cec = 0;
				}
			}
		}
	}
}
