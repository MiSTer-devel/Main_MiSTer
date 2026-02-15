#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <linux/input.h>

#include "hdmi_cec.h"
#include "cfg.h"
#include "hardware.h"
#include "input.h"
#include "smbus.h"
#include "user_io.h"

static const uint8_t ADV7513_MAIN_ADDR = 0x39;
static const uint8_t ADV7513_CEC_ADDR = 0x3C;
static const uint8_t ADV7513_EDID_ADDR = 0x3F;

static const uint8_t MAIN_REG_CEC_I2C_ADDR = 0xE1;
static const uint8_t MAIN_REG_CEC_POWER = 0xE2;
static const uint8_t MAIN_REG_CEC_CTRL = 0xE3;
static const uint8_t MAIN_REG_POWER2 = 0xD6;
static const uint8_t MAIN_REG_MONITOR_SENSE = 0xA1;
static const uint8_t MAIN_REG_HDMI_CFG = 0xAF;
static const uint8_t MAIN_REG_INT0_ENABLE = 0x94;
static const uint8_t MAIN_REG_INT1_ENABLE = 0x95;
static const uint8_t MAIN_REG_INT0_STATUS = 0x96;
static const uint8_t MAIN_REG_EDID_CTRL = 0xC9;

static const uint8_t CEC_REG_TX_FRAME_HEADER = 0x00;
static const uint8_t CEC_REG_TX_FRAME_DATA0 = 0x01;
static const uint8_t CEC_REG_TX_FRAME_LENGTH = 0x10;
static const uint8_t CEC_REG_TX_ENABLE = 0x11;
static const uint8_t CEC_REG_TX_RETRY = 0x12;
static const uint8_t CEC_REG_RX1_FRAME_HEADER = 0x15;
static const uint8_t CEC_REG_RX2_FRAME_HEADER = 0x27;
static const uint8_t CEC_REG_RX3_FRAME_HEADER = 0x38;
static const uint8_t CEC_REG_RX1_FRAME_LENGTH = 0x25;
static const uint8_t CEC_REG_RX2_FRAME_LENGTH = 0x37;
static const uint8_t CEC_REG_RX3_FRAME_LENGTH = 0x48;
static const uint8_t CEC_REG_RX_STATUS = 0x26;
static const uint8_t CEC_REG_INT_ENABLE = 0x40;
static const uint8_t CEC_REG_INT_STATUS = 0x41;
static const uint8_t CEC_REG_INT_CLEAR = 0x42;
static const uint8_t CEC_REG_RX_BUFFERS = 0x4A;
static const uint8_t CEC_REG_LOG_ADDR_MASK = 0x4B;
static const uint8_t CEC_REG_LOG_ADDR_0_1 = 0x4C;
static const uint8_t CEC_REG_LOG_ADDR_2 = 0x4D;
static const uint8_t CEC_REG_CLK_DIV = 0x4E;
static const uint8_t CEC_REG_SOFT_RESET = 0x50;

static const uint8_t CEC_INT_RX_RDY1 = 1 << 0;
static const uint8_t CEC_INT_RX_RDY2 = 1 << 1;
static const uint8_t CEC_INT_RX_RDY3 = 1 << 2;
static const uint8_t CEC_INT_TX_RETRY_TIMEOUT = 1 << 3;
static const uint8_t CEC_INT_TX_ARBITRATION = 1 << 4;
static const uint8_t CEC_INT_TX_DONE = 1 << 5;

static const uint8_t CEC_LOG_ADDR_TV = 0;
static const uint8_t CEC_LOG_ADDR_PLAYBACK1 = 4;
static const uint8_t CEC_LOG_ADDR_PLAYBACK2 = 8;
static const uint8_t CEC_LOG_ADDR_PLAYBACK3 = 11;
static const uint8_t CEC_LOG_ADDR_BROADCAST = 15;

static const uint8_t CEC_OPCODE_IMAGE_VIEW_ON = 0x04;
static const uint8_t CEC_OPCODE_TEXT_VIEW_ON = 0x0D;
static const uint8_t CEC_OPCODE_STANDBY = 0x36;
static const uint8_t CEC_OPCODE_USER_CONTROL_PRESSED = 0x44;
static const uint8_t CEC_OPCODE_USER_CONTROL_RELEASED = 0x45;
static const uint8_t CEC_OPCODE_GIVE_OSD_NAME = 0x46;
static const uint8_t CEC_OPCODE_SET_OSD_NAME = 0x47;
static const uint8_t CEC_OPCODE_ACTIVE_SOURCE = 0x82;
static const uint8_t CEC_OPCODE_GIVE_PHYSICAL_ADDRESS = 0x83;
static const uint8_t CEC_OPCODE_REPORT_PHYSICAL_ADDRESS = 0x84;
static const uint8_t CEC_OPCODE_REQUEST_ACTIVE_SOURCE = 0x85;
static const uint8_t CEC_OPCODE_SET_STREAM_PATH = 0x86;
static const uint8_t CEC_OPCODE_DEVICE_VENDOR_ID = 0x87;
static const uint8_t CEC_OPCODE_GIVE_DEVICE_VENDOR_ID = 0x8C;
static const uint8_t CEC_OPCODE_MENU_REQUEST = 0x8D;
static const uint8_t CEC_OPCODE_MENU_STATUS = 0x8E;
static const uint8_t CEC_OPCODE_GIVE_DEVICE_POWER_STATUS = 0x8F;
static const uint8_t CEC_OPCODE_REPORT_POWER_STATUS = 0x90;
static const uint8_t CEC_OPCODE_CEC_VERSION = 0x9E;
static const uint8_t CEC_OPCODE_GET_CEC_VERSION = 0x9F;

static const uint8_t CEC_USER_CONTROL_SELECT = 0x00;
static const uint8_t CEC_USER_CONTROL_UP = 0x01;
static const uint8_t CEC_USER_CONTROL_DOWN = 0x02;
static const uint8_t CEC_USER_CONTROL_LEFT = 0x03;
static const uint8_t CEC_USER_CONTROL_RIGHT = 0x04;
static const uint8_t CEC_USER_CONTROL_ROOT_MENU = 0x09;
static const uint8_t CEC_USER_CONTROL_SETUP_MENU = 0x0A;
static const uint8_t CEC_USER_CONTROL_CONTENTS_MENU = 0x0B;
static const uint8_t CEC_USER_CONTROL_FAVORITE_MENU = 0x0C;
static const uint8_t CEC_USER_CONTROL_EXIT = 0x0D;
static const uint8_t CEC_USER_CONTROL_MEDIA_TOP_MENU = 0x10;
static const uint8_t CEC_USER_CONTROL_MEDIA_CONTEXT_MENU = 0x11;
static const uint8_t CEC_USER_CONTROL_NUMBER_0 = 0x20;
static const uint8_t CEC_USER_CONTROL_NUMBER_1 = 0x21;
static const uint8_t CEC_USER_CONTROL_NUMBER_2 = 0x22;
static const uint8_t CEC_USER_CONTROL_NUMBER_3 = 0x23;
static const uint8_t CEC_USER_CONTROL_NUMBER_4 = 0x24;
static const uint8_t CEC_USER_CONTROL_NUMBER_5 = 0x25;
static const uint8_t CEC_USER_CONTROL_NUMBER_6 = 0x26;
static const uint8_t CEC_USER_CONTROL_NUMBER_7 = 0x27;
static const uint8_t CEC_USER_CONTROL_NUMBER_8 = 0x28;
static const uint8_t CEC_USER_CONTROL_NUMBER_9 = 0x29;
static const uint8_t CEC_USER_CONTROL_INPUT_SELECT = 0x34;
static const uint8_t CEC_USER_CONTROL_DISPLAY_INFO = 0x35;
static const uint8_t CEC_USER_CONTROL_HELP = 0x36;
static const uint8_t CEC_USER_CONTROL_PLAY = 0x44;
static const uint8_t CEC_USER_CONTROL_STOP = 0x45;
static const uint8_t CEC_USER_CONTROL_PAUSE = 0x46;
static const uint8_t CEC_USER_CONTROL_REWIND = 0x48;
static const uint8_t CEC_USER_CONTROL_FAST_FORWARD = 0x49;
static const uint8_t CEC_USER_CONTROL_EPG = 0x53;
static const uint8_t CEC_USER_CONTROL_INITIAL_CONFIGURATION = 0x55;
static const uint8_t CEC_USER_CONTROL_SELECT_MEDIA_FUNCTION = 0x68;
static const uint8_t CEC_USER_CONTROL_SELECT_AV_INPUT_FUNCTION = 0x69;
static const uint8_t CEC_USER_CONTROL_F1_BLUE = 0x71;
static const uint8_t CEC_USER_CONTROL_F2_RED = 0x72;
static const uint8_t CEC_USER_CONTROL_F3_GREEN = 0x73;
static const uint8_t CEC_USER_CONTROL_F4_YELLOW = 0x74;

static const uint8_t CEC_DEVICE_TYPE_PLAYBACK = 4;
static const uint8_t CEC_POWER_STATUS_ON = 0x00;
static const uint8_t CEC_VERSION_1_4 = 0x05;
static const char *CEC_DEFAULT_OSD_NAME = "MiSTer";

static const uint8_t CEC_INPUT_MODE_OFF = 0;
static const uint8_t CEC_INPUT_MODE_MENU_ONLY = 1;
static const uint8_t CEC_INPUT_MODE_ALWAYS = 2;

static const uint8_t CEC_OSD_KEY_NONE = 0;
static const uint8_t CEC_OSD_KEY_BACK = 1;
static const uint8_t CEC_OSD_KEY_RED = 2;
static const uint8_t CEC_OSD_KEY_GREEN = 3;
static const uint8_t CEC_OSD_KEY_YELLOW = 4;
static const uint8_t CEC_OSD_KEY_BLUE = 5;

static const uint16_t CEC_DEFAULT_PHYS_ADDR = 0x1000;
static const unsigned long CEC_BUTTON_TIMEOUT_MS = 500;
static const unsigned long CEC_MAIN_REFRESH_MS = 2000;
static const unsigned long CEC_TX_TIMEOUT_MS = 220;
static const unsigned long CEC_TX_TIMEOUT_RETRY_MS = 500;

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

static bool cec_enabled = false;
static int cec_main_fd = -1;
static int cec_fd = -1;
static uint8_t cec_logical_addr = CEC_LOG_ADDR_PLAYBACK1;
static uint16_t cec_physical_addr = CEC_DEFAULT_PHYS_ADDR;
static uint16_t cec_pressed_key = 0;
static unsigned long cec_press_deadline = 0;
static unsigned long cec_refresh_deadline = 0;
static bool cec_hpd_pulsed = false;
static unsigned long cec_reply_phys_deadline = 0;
static unsigned long cec_reply_name_deadline = 0;
static unsigned long cec_reply_version_deadline = 0;
static unsigned long cec_reply_power_deadline = 0;
static unsigned long cec_reply_menu_deadline = 0;
static unsigned long cec_reply_active_deadline = 0;
static unsigned long cec_forced_clear_log_deadline = 0;
static uint8_t cec_tx_fail_streak = 0;
static unsigned long cec_tx_suppress_deadline = 0;
static unsigned long cec_rx_fallback_stale_deadline = 0;
static bool cec_boot_activate_pending = false;
static unsigned long cec_boot_activate_deadline = 0;
static uint16_t cec_active_physical_addr = 0xFFFF;
static uint32_t cec_input_activity_seq = 0;
static unsigned long cec_idle_deadline = 0;
static bool cec_idle_engaged = false;

static bool cec_send_message(const cec_message_t *msg, bool with_retry = true);
static bool cec_send_image_view_on(void);
static bool cec_send_text_view_on(void);
static bool cec_send_active_source(void);
static bool cec_send_report_physical_address(void);
static bool cec_send_set_osd_name(const char *name);
static bool cec_send_cec_version(uint8_t destination);
static void cec_handle_message(const cec_message_t *msg);
static bool cec_receive_message(cec_message_t *msg);

static bool cec_debug_enabled(void)
{
	return cfg.debug != 0;
}

static uint8_t cec_get_input_mode(void)
{
	if (cfg.hdmi_cec_input_mode <= CEC_INPUT_MODE_ALWAYS) return cfg.hdmi_cec_input_mode;
	return CEC_INPUT_MODE_MENU_ONLY;
}

static uint8_t cec_get_osd_key_mode(void)
{
	const char *value = cfg.hdmi_cec_osd_key;
	if (!value || !value[0]) return CEC_OSD_KEY_BACK;

	if (!strcmp(value, "0")) return CEC_OSD_KEY_NONE;
	if (!strcmp(value, "1")) return CEC_OSD_KEY_BACK;
	if (!strcmp(value, "2")) return CEC_OSD_KEY_RED;
	if (!strcmp(value, "3")) return CEC_OSD_KEY_GREEN;
	if (!strcmp(value, "4")) return CEC_OSD_KEY_YELLOW;
	if (!strcmp(value, "5")) return CEC_OSD_KEY_BLUE;

	char keybuf[16] = {};
	size_t len = strlen(value);
	if (len >= sizeof(keybuf)) len = sizeof(keybuf) - 1;
	for (size_t i = 0; i < len; i++) keybuf[i] = (char)tolower((unsigned char)value[i]);

	if (!strcmp(keybuf, "none")) return CEC_OSD_KEY_NONE;
	if (!strcmp(keybuf, "back")) return CEC_OSD_KEY_BACK;
	if (!strcmp(keybuf, "red")) return CEC_OSD_KEY_RED;
	if (!strcmp(keybuf, "green")) return CEC_OSD_KEY_GREEN;
	if (!strcmp(keybuf, "yellow")) return CEC_OSD_KEY_YELLOW;
	if (!strcmp(keybuf, "blue")) return CEC_OSD_KEY_BLUE;

	return CEC_OSD_KEY_BACK;
}

static uint8_t cec_get_osd_button_code(void)
{
	switch (cec_get_osd_key_mode())
	{
	case CEC_OSD_KEY_NONE: return 0;
	case CEC_OSD_KEY_BACK: return CEC_USER_CONTROL_EXIT;
	case CEC_OSD_KEY_RED: return CEC_USER_CONTROL_F2_RED;
	case CEC_OSD_KEY_GREEN: return CEC_USER_CONTROL_F3_GREEN;
	case CEC_OSD_KEY_YELLOW: return CEC_USER_CONTROL_F4_YELLOW;
	case CEC_OSD_KEY_BLUE: return CEC_USER_CONTROL_F1_BLUE;
	default: return 0;
	}
}

static bool cec_is_osd_trigger_button(uint8_t button_code)
{
	uint8_t osd_button = cec_get_osd_button_code();
	return osd_button && (button_code == osd_button);
}

static bool cec_input_target_active(void)
{
	return user_io_osd_is_visible() || is_menu();
}

static bool cec_accept_button_input(uint8_t button_code)
{
	uint8_t mode = cec_get_input_mode();
	if (mode == CEC_INPUT_MODE_OFF) return false;
	if (mode == CEC_INPUT_MODE_ALWAYS) return true;
	if (mode != CEC_INPUT_MODE_MENU_ONLY) return true;

	if (cec_is_osd_trigger_button(button_code)) return true;
	return cec_input_target_active();
}

static unsigned long cec_idle_sleep_delay_ms(void)
{
	// Tie idle sleep/wake to global idle blanking ("video_off"):
	// after video_off minutes of no input activity.
	if (!cfg.video_off) return 0;

	// Match the same preset mapping as video idle blanking:
	// 1=15m, 2=30m, 3=45m, 4=60m.
	unsigned long minutes = (unsigned long)cfg.video_off;
	if (cfg.video_off <= 4) minutes = (unsigned long)cfg.video_off * 15ul;

	return minutes * 60ul * 1000ul;
}

static const char *cec_get_osd_name(void)
{
	if (cfg.hdmi_cec_name[0]) return cfg.hdmi_cec_name;
	return CEC_DEFAULT_OSD_NAME;
}

static const char *cec_opcode_name(uint8_t opcode)
{
	switch (opcode)
	{
	case CEC_OPCODE_IMAGE_VIEW_ON: return "IMAGE_VIEW_ON";
	case CEC_OPCODE_TEXT_VIEW_ON: return "TEXT_VIEW_ON";
	case CEC_OPCODE_STANDBY: return "STANDBY";
	case CEC_OPCODE_USER_CONTROL_PRESSED: return "USER_CONTROL_PRESSED";
	case CEC_OPCODE_USER_CONTROL_RELEASED: return "USER_CONTROL_RELEASED";
	case CEC_OPCODE_GIVE_OSD_NAME: return "GIVE_OSD_NAME";
	case CEC_OPCODE_SET_OSD_NAME: return "SET_OSD_NAME";
	case CEC_OPCODE_ACTIVE_SOURCE: return "ACTIVE_SOURCE";
	case CEC_OPCODE_GIVE_PHYSICAL_ADDRESS: return "GIVE_PHYSICAL_ADDRESS";
	case CEC_OPCODE_REPORT_PHYSICAL_ADDRESS: return "REPORT_PHYSICAL_ADDRESS";
	case CEC_OPCODE_REQUEST_ACTIVE_SOURCE: return "REQUEST_ACTIVE_SOURCE";
	case CEC_OPCODE_SET_STREAM_PATH: return "SET_STREAM_PATH";
	case CEC_OPCODE_DEVICE_VENDOR_ID: return "DEVICE_VENDOR_ID";
	case CEC_OPCODE_GIVE_DEVICE_VENDOR_ID: return "GIVE_DEVICE_VENDOR_ID";
	case CEC_OPCODE_MENU_REQUEST: return "MENU_REQUEST";
	case CEC_OPCODE_MENU_STATUS: return "MENU_STATUS";
	case CEC_OPCODE_GIVE_DEVICE_POWER_STATUS: return "GIVE_DEVICE_POWER_STATUS";
	case CEC_OPCODE_REPORT_POWER_STATUS: return "REPORT_POWER_STATUS";
	case CEC_OPCODE_CEC_VERSION: return "CEC_VERSION";
	case CEC_OPCODE_GET_CEC_VERSION: return "GET_CEC_VERSION";
	default: return "UNKNOWN";
	}
}

static bool cec_rate_limit(unsigned long *deadline, unsigned long interval_ms)
{
	if (!deadline) return false;
	if (!CheckTimer(*deadline)) return false;
	*deadline = GetTimer(interval_ms);
	return true;
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

	input_cec_send_key(cec_pressed_key, false);
	cec_pressed_key = 0;
	cec_press_deadline = 0;
}

static uint16_t cec_button_to_key(uint8_t button_code)
{
	if (cec_is_osd_trigger_button(button_code)) return KEY_F12;

	switch (button_code)
	{
	case CEC_USER_CONTROL_UP: return KEY_UP;
	case CEC_USER_CONTROL_DOWN: return KEY_DOWN;
	case CEC_USER_CONTROL_LEFT: return KEY_LEFT;
	case CEC_USER_CONTROL_RIGHT: return KEY_RIGHT;
	case CEC_USER_CONTROL_SELECT: return KEY_ENTER;
	case CEC_USER_CONTROL_ROOT_MENU:
	case CEC_USER_CONTROL_SETUP_MENU:
	case CEC_USER_CONTROL_CONTENTS_MENU:
	case CEC_USER_CONTROL_FAVORITE_MENU:
	case CEC_USER_CONTROL_MEDIA_TOP_MENU:
	case CEC_USER_CONTROL_MEDIA_CONTEXT_MENU:
	case CEC_USER_CONTROL_INPUT_SELECT:
	case CEC_USER_CONTROL_DISPLAY_INFO:
	case CEC_USER_CONTROL_HELP:
	case CEC_USER_CONTROL_EPG:
	case CEC_USER_CONTROL_INITIAL_CONFIGURATION:
	case CEC_USER_CONTROL_SELECT_MEDIA_FUNCTION:
	case CEC_USER_CONTROL_SELECT_AV_INPUT_FUNCTION:
		return 0;
	case CEC_USER_CONTROL_EXIT: return KEY_ESC;
	case CEC_USER_CONTROL_PLAY:
	case CEC_USER_CONTROL_PAUSE: return KEY_SPACE;
	case CEC_USER_CONTROL_STOP: return KEY_S;
	case CEC_USER_CONTROL_REWIND: return KEY_R;
	case CEC_USER_CONTROL_FAST_FORWARD: return KEY_F;
	case CEC_USER_CONTROL_NUMBER_0: return KEY_0;
	case CEC_USER_CONTROL_NUMBER_1: return KEY_1;
	case CEC_USER_CONTROL_NUMBER_2: return KEY_2;
	case CEC_USER_CONTROL_NUMBER_3: return KEY_3;
	case CEC_USER_CONTROL_NUMBER_4: return KEY_4;
	case CEC_USER_CONTROL_NUMBER_5: return KEY_5;
	case CEC_USER_CONTROL_NUMBER_6: return KEY_6;
	case CEC_USER_CONTROL_NUMBER_7: return KEY_7;
	case CEC_USER_CONTROL_NUMBER_8: return KEY_8;
	case CEC_USER_CONTROL_NUMBER_9: return KEY_9;
	default: return 0;
	}
}

static const char *cec_button_name(uint8_t button_code)
{
	switch (button_code)
	{
	case CEC_USER_CONTROL_UP: return "up";
	case CEC_USER_CONTROL_DOWN: return "down";
	case CEC_USER_CONTROL_LEFT: return "left";
	case CEC_USER_CONTROL_RIGHT: return "right";
	case CEC_USER_CONTROL_SELECT: return "ok";
	case CEC_USER_CONTROL_EXIT: return "back";
	case CEC_USER_CONTROL_F2_RED: return "red";
	case CEC_USER_CONTROL_F4_YELLOW: return "yellow";
	case CEC_USER_CONTROL_F3_GREEN: return "green";
	case CEC_USER_CONTROL_F1_BLUE: return "blue";
	default: return nullptr;
	}
}

static void cec_handle_button(uint8_t button_code, bool pressed)
{
	if (!pressed)
	{
		cec_release_key();
		return;
	}

	const uint16_t key = cec_button_to_key(button_code);
	if (cec_debug_enabled())
	{
		const char *name = cec_button_name(button_code);
		if (name && (!key || cec_pressed_key != key))
		{
			printf("CEC: button %s\n", name);
		}
	}

	if (!cec_accept_button_input(button_code)) return;
	if (!key) return;

	if (cec_pressed_key && cec_pressed_key != key)
	{
		cec_release_key();
	}

	if (!cec_pressed_key)
	{
		input_cec_send_key(key, true);
		cec_pressed_key = key;
	}

	cec_press_deadline = GetTimer(CEC_BUTTON_TIMEOUT_MS);
}

static void cec_poll_key_timeout(void)
{
	if (cec_pressed_key && CheckTimer(cec_press_deadline))
	{
		cec_release_key();
	}
}

static void cec_poll_idle_sleep_wake(void)
{
	// Global idle detector based on real input activity (not just OSD/menu navigation),
	// tied to global idle blanking ("video_off") timing.
	if (!cfg.hdmi_cec_sleep && !cfg.hdmi_cec_wake) return;

	unsigned long delay_ms = cec_idle_sleep_delay_ms();
	if (!delay_ms)
	{
		cec_idle_deadline = 0;
		cec_idle_engaged = false;
		cec_input_activity_seq = input_activity_get_seq();
		return;
	}

	if (!cec_idle_deadline)
	{
		cec_input_activity_seq = input_activity_get_seq();
		cec_idle_deadline = GetTimer(delay_ms);
	}

	uint32_t seq = input_activity_get_seq();
	if (seq != cec_input_activity_seq)
	{
		cec_input_activity_seq = seq;
		cec_idle_deadline = GetTimer(delay_ms);

		if (cec_idle_engaged)
		{
			cec_idle_engaged = false;
			if (cfg.hdmi_cec_wake) cec_send_wake();
		}

		return;
	}

	if (!cec_idle_engaged && CheckTimer(cec_idle_deadline))
	{
		cec_idle_engaged = true;
		if (cfg.hdmi_cec_sleep)
		{
			// Avoid powering off the display if MiSTer isn't the active source.
			if (cec_active_physical_addr == cec_physical_addr)
			{
				cec_send_standby();
			}
			else if (cec_debug_enabled())
			{
				printf("CEC: idle standby skipped (not active source)\n");
			}
		}
	}
}

static cec_tx_result_t cec_wait_for_tx(unsigned long timeout_ms)
{
	unsigned long timeout = GetTimer(timeout_ms);
	uint8_t low_drv_start = cec_reg_read(0x14);

	while (!CheckTimer(timeout))
	{
		uint8_t status = cec_reg_read(CEC_REG_INT_STATUS);

		if (status & (CEC_INT_TX_RETRY_TIMEOUT | CEC_INT_TX_ARBITRATION))
		{
			cec_reg_write(CEC_REG_INT_CLEAR, status & (CEC_INT_TX_RETRY_TIMEOUT | CEC_INT_TX_ARBITRATION));
			return CEC_TX_RESULT_NACK;
		}

		if (status & CEC_INT_TX_DONE)
		{
			cec_reg_write(CEC_REG_INT_CLEAR, CEC_INT_TX_DONE);
			return CEC_TX_RESULT_OK;
		}

		uint8_t tx_en = cec_reg_read(CEC_REG_TX_ENABLE);
		uint8_t low_drv_now = cec_reg_read(0x14);

		if (low_drv_now != low_drv_start && tx_en == 0)
		{
			return CEC_TX_RESULT_OK;
		}

		usleep(2000);
	}

	uint8_t low_drv_end = cec_reg_read(0x14);

	if (low_drv_end != low_drv_start)
	{
		return CEC_TX_RESULT_OK;
	}

	cec_reg_write(CEC_REG_TX_ENABLE, 0x00);
	return CEC_TX_RESULT_TIMEOUT;
}

static bool cec_send_message(const cec_message_t *msg, bool with_retry)
{
	if (!cec_enabled || !msg) return false;
	if (msg->length < 1 || msg->length > 16) return false;
	if (!CheckTimer(cec_tx_suppress_deadline)) return false;

	cec_reg_write(CEC_REG_TX_ENABLE, 0x00);
	cec_reg_write(CEC_REG_INT_CLEAR, CEC_INT_TX_RETRY_TIMEOUT | CEC_INT_TX_ARBITRATION | CEC_INT_TX_DONE);

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
	cec_reg_write(CEC_REG_TX_ENABLE, 0x01);

	cec_tx_result_t tx_res = cec_wait_for_tx(with_retry ? CEC_TX_TIMEOUT_RETRY_MS : CEC_TX_TIMEOUT_MS);

	if (tx_res == CEC_TX_RESULT_OK)
	{
		cec_tx_fail_streak = 0;
	}
	else if (tx_res == CEC_TX_RESULT_NACK)
	{
		if (cec_tx_fail_streak < 255) cec_tx_fail_streak++;
		if (cec_tx_fail_streak >= 8)
		{
			cec_tx_suppress_deadline = GetTimer(15000);
			cec_tx_fail_streak = 0;
			if (cec_debug_enabled())
			{
				printf("CEC: TX suppressed for 15000ms after repeated failures\n");
			}
		}
	}

	if (cec_debug_enabled() && msg->length > 1 && tx_res == CEC_TX_RESULT_NACK)
	{
		printf("CEC: TX NACK %X->%X op=0x%02X (%s)\n",
			(msg->header >> 4) & 0x0F,
			msg->header & 0x0F,
			msg->opcode,
			cec_opcode_name(msg->opcode));
	}

	// Treat timeout without explicit NACK/arbitration as uncertain success.
	return tx_res != CEC_TX_RESULT_NACK;
}

static uint8_t cec_pick_logical_address_from_physical(uint16_t physical_addr)
{
	uint8_t port = (physical_addr >> 12) & 0x0F;
	if (port == 2) return CEC_LOG_ADDR_PLAYBACK2;
	if (port >= 3) return CEC_LOG_ADDR_PLAYBACK3;
	return CEC_LOG_ADDR_PLAYBACK1;
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
	cec_reg_write(CEC_REG_RX_BUFFERS, 0x00);
}

static bool cec_setup_main_registers(void)
{
	if (cec_main_fd < 0) return false;

	bool ok = true;

	ok &= main_reg_write(MAIN_REG_CEC_I2C_ADDR, ADV7513_CEC_ADDR << 1);
	ok &= main_reg_write(MAIN_REG_CEC_POWER, 0x00);
	uint8_t reg_e3 = main_reg_read(MAIN_REG_CEC_CTRL);
	ok &= main_reg_write(MAIN_REG_CEC_CTRL, reg_e3 | 0x0E);

	if (!cec_hpd_pulsed)
	{
		ok &= main_reg_write(MAIN_REG_POWER2, 0x00);
		usleep(100000);
		ok &= main_reg_write(MAIN_REG_POWER2, 0xC0);
		usleep(100000);
		cec_hpd_pulsed = true;
	}
	else
	{
		ok &= main_reg_write(MAIN_REG_POWER2, 0xC0);
	}

	ok &= main_reg_write(MAIN_REG_MONITOR_SENSE, 0x40);

	uint8_t reg_af = main_reg_read(MAIN_REG_HDMI_CFG);
	uint8_t reg_af_new = (uint8_t)((reg_af & 0x9C) | 0x06);
	ok &= main_reg_write(MAIN_REG_HDMI_CFG, reg_af_new);

	uint8_t reg_94 = main_reg_read(MAIN_REG_INT0_ENABLE);
	ok &= main_reg_write(MAIN_REG_INT0_ENABLE, reg_94 | 0x80);
	uint8_t reg_95 = main_reg_read(MAIN_REG_INT1_ENABLE);
	ok &= main_reg_write(MAIN_REG_INT1_ENABLE, reg_95 | 0x20);

	if (!ok)
	{
		printf("CEC: main register setup failed\n");
		if (cec_debug_enabled())
		{
			printf("CEC: main regs E1=%02X E2=%02X E3=%02X D6=%02X AF=%02X A1=%02X 94=%02X 95=%02X 96=%02X\n",
				main_reg_read(MAIN_REG_CEC_I2C_ADDR),
				main_reg_read(MAIN_REG_CEC_POWER),
				main_reg_read(MAIN_REG_CEC_CTRL),
				main_reg_read(MAIN_REG_POWER2),
				main_reg_read(MAIN_REG_HDMI_CFG),
				main_reg_read(MAIN_REG_MONITOR_SENSE),
				main_reg_read(MAIN_REG_INT0_ENABLE),
				main_reg_read(MAIN_REG_INT1_ENABLE),
				main_reg_read(MAIN_REG_INT0_STATUS));
		}
	}

	return ok;
}

static uint16_t cec_parse_physical_address(const uint8_t *edid, size_t size)
{
	if (!edid || size < 256) return CEC_DEFAULT_PHYS_ADDR;

	if (edid[0] != 0x00 || edid[1] != 0xFF || edid[2] != 0xFF || edid[3] != 0xFF ||
		edid[4] != 0xFF || edid[5] != 0xFF || edid[6] != 0xFF || edid[7] != 0x00)
	{
		return CEC_DEFAULT_PHYS_ADDR;
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
					return (uint16_t)((blk[pos + 4] << 8) | blk[pos + 5]);
				}
			}

			pos += len + 1;
		}
	}

	return CEC_DEFAULT_PHYS_ADDR;
}

static uint16_t cec_parse_physical_address_loose(const uint8_t *edid, size_t size)
{
	if (!edid || size < 8) return CEC_DEFAULT_PHYS_ADDR;

	for (size_t i = 0; i + 4 < size; i++)
	{
		if (edid[i] == 0x03 && edid[i + 1] == 0x0C && edid[i + 2] == 0x00)
		{
			uint16_t addr = (uint16_t)((edid[i + 3] << 8) | edid[i + 4]);
			if (addr != 0x0000 && addr != 0xFFFF)
			{
				return addr;
			}
		}
	}

	return CEC_DEFAULT_PHYS_ADDR;
}

static uint16_t cec_read_physical_address(void)
{
	uint8_t edid[256] = {};

	if (cec_main_fd < 0) return CEC_DEFAULT_PHYS_ADDR;

	int edid_fd = i2c_open(ADV7513_EDID_ADDR, 0);
	if (edid_fd < 0) return CEC_DEFAULT_PHYS_ADDR;

	main_reg_write(MAIN_REG_EDID_CTRL, 0x03);
	usleep(50000);
	main_reg_write(MAIN_REG_EDID_CTRL, 0x13);
	usleep(50000);

	int read_errors = 0;
	for (uint16_t i = 0; i < sizeof(edid); i++)
	{
		int value = i2c_smbus_read_byte_data(edid_fd, (uint8_t)i);
		if (value < 0)
		{
			read_errors++;
			edid[i] = 0;
			continue;
		}

		edid[i] = (uint8_t)value;
	}

	i2c_close(edid_fd);

	uint16_t addr = cec_parse_physical_address(edid, sizeof(edid));
	if (addr == CEC_DEFAULT_PHYS_ADDR)
	{
		uint16_t loose = cec_parse_physical_address_loose(edid, sizeof(edid));
		if (loose != CEC_DEFAULT_PHYS_ADDR)
		{
			if (cec_debug_enabled()) printf("CEC: physical addr loose parse hit\n");
			addr = loose;
		}
	}

	if (cec_debug_enabled())
	{
		printf("CEC: EDID read errors=%d, physical=%X.%X.%X.%X\n",
			read_errors,
			(addr >> 12) & 0x0F,
			(addr >> 8) & 0x0F,
			(addr >> 4) & 0x0F,
			addr & 0x0F);
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
	cec_reg_write(CEC_REG_RX_BUFFERS, bit);
	usleep(200);
	cec_reg_write(CEC_REG_RX_BUFFERS, 0x00);

	return true;
}

static bool cec_receive_message(cec_message_t *msg)
{
	if (!cec_enabled || !msg) return false;

	uint8_t int_status = cec_reg_read(CEC_REG_INT_STATUS);
	uint8_t rx_bits = int_status & (CEC_INT_RX_RDY1 | CEC_INT_RX_RDY2 | CEC_INT_RX_RDY3);
	bool used_fallback = false;
	if (!rx_bits)
	{
		used_fallback = true;
		// Some ADV7513 setups miss RX ready interrupts; fall back to polling RX length registers.
		for (int i = 0; i < 3; i++)
		{
			uint8_t len = cec_reg_read(cec_rx_len_regs[i]) & 0x1F;
			if (len >= 1 && len <= 16) rx_bits |= cec_rx_int_bits[i];
		}

		(void)int_status;
	}
	if (!rx_bits) return false;

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
		if (used_fallback)
		{
			// Length-only fallback can read stale slots when RX order reports no queued frame.
			if (CheckTimer(cec_rx_fallback_stale_deadline))
			{
				cec_rx_fallback_stale_deadline = GetTimer(2000);
				cec_clear_rx_buffers();
			}
			return false;
		}

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
	cec_reg_write(CEC_REG_INT_CLEAR, cec_rx_int_bits[selected]);
	if (used_fallback)
	{
		uint8_t len_after = cec_reg_read(cec_rx_len_regs[selected]) & 0x1F;
		if (len_after >= 1 && len_after <= 16)
		{
			// Sticky fallback reads can repeatedly expose the same frame; force drain.
			cec_clear_rx_buffers();
			if (CheckTimer(cec_forced_clear_log_deadline)) cec_forced_clear_log_deadline = GetTimer(5000);
		}
	}

	bool log_rx = (msg->opcode == CEC_OPCODE_SET_STREAM_PATH);

	if (ok && cec_debug_enabled() && msg->length > 1 && log_rx)
	{
		printf("CEC: RX %X->%X op=0x%02X len=%u\n",
			(msg->header >> 4) & 0x0F,
			msg->header & 0x0F,
			msg->opcode,
			msg->length);
	}

	return ok;
}

static bool cec_send_active_source(void)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
	msg.opcode = CEC_OPCODE_ACTIVE_SOURCE;
	msg.data[0] = (uint8_t)(cec_physical_addr >> 8);
	msg.data[1] = (uint8_t)(cec_physical_addr & 0xFF);
	msg.length = 4;
	bool ok = cec_send_message(&msg);
	if (ok) cec_active_physical_addr = cec_physical_addr;
	return ok;
}

static bool cec_send_image_view_on(void)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_TV;
	msg.opcode = CEC_OPCODE_IMAGE_VIEW_ON;
	msg.length = 2;
	return cec_send_message(&msg);
}

static bool cec_send_text_view_on(void)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_TV;
	msg.opcode = CEC_OPCODE_TEXT_VIEW_ON;
	msg.length = 2;
	return cec_send_message(&msg);
}

bool cec_send_standby(void)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
	msg.opcode = CEC_OPCODE_STANDBY;
	msg.length = 2;
	return cec_send_message(&msg);
}

bool cec_send_wake(void)
{
	if (!cec_enabled) return false;

	bool wake_ok = cec_send_image_view_on(); usleep(20000);
	bool text_ok = cec_send_text_view_on(); usleep(20000);
	bool active_ok = cec_send_active_source();

	if (cec_debug_enabled()) printf("CEC: wake image=%d text=%d active=%d\n",
		wake_ok ? 1 : 0,
		text_ok ? 1 : 0,
		active_ok ? 1 : 0);

	return wake_ok && text_ok && active_ok;
}

static bool cec_send_report_physical_address(void)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
	msg.opcode = CEC_OPCODE_REPORT_PHYSICAL_ADDRESS;
	msg.data[0] = (uint8_t)(cec_physical_addr >> 8);
	msg.data[1] = (uint8_t)(cec_physical_addr & 0xFF);
	msg.data[2] = CEC_DEVICE_TYPE_PLAYBACK;
	msg.length = 5;
	return cec_send_message(&msg);
}

static bool cec_send_set_osd_name(const char *name)
{
	if (!name) return false;

	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_TV;
	msg.opcode = CEC_OPCODE_SET_OSD_NAME;

	size_t len = strlen(name);
	if (len > 14) len = 14;
	for (size_t i = 0; i < len; i++)
	{
		msg.data[i] = (uint8_t)name[i];
	}

	msg.length = (uint8_t)(2 + len);
	return cec_send_message(&msg);
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
	msg.data[0] = CEC_POWER_STATUS_ON;
	msg.length = 3;
	return cec_send_message(&msg);
}

static void cec_handle_message(const cec_message_t *msg)
{
	if (!msg || msg->length < 2) return;

	uint8_t src = (msg->header >> 4) & 0x0F;
	uint8_t dst = msg->header & 0x0F;
	if (dst != cec_logical_addr && dst != CEC_LOG_ADDR_BROADCAST) return;

	bool is_user_control = (msg->opcode == CEC_OPCODE_USER_CONTROL_PRESSED) ||
		(msg->opcode == CEC_OPCODE_USER_CONTROL_RELEASED);

	// Ignore broadcast network chatter unless it's potentially actionable.
	if (dst == CEC_LOG_ADDR_BROADCAST &&
		msg->opcode != CEC_OPCODE_ACTIVE_SOURCE &&
		msg->opcode != CEC_OPCODE_SET_STREAM_PATH &&
		msg->opcode != CEC_OPCODE_REQUEST_ACTIVE_SOURCE &&
		!(is_user_control && src == CEC_LOG_ADDR_TV))
	{
		return;
	}

	switch (msg->opcode)
	{
	case CEC_OPCODE_GIVE_PHYSICAL_ADDRESS:
		if (cec_rate_limit(&cec_reply_phys_deadline, 2000)) cec_send_report_physical_address();
		break;

	case CEC_OPCODE_GIVE_OSD_NAME:
		if (cec_rate_limit(&cec_reply_name_deadline, 2000)) cec_send_set_osd_name(cec_get_osd_name());
		break;

	case CEC_OPCODE_GET_CEC_VERSION:
		if (cec_rate_limit(&cec_reply_version_deadline, 5000)) cec_send_cec_version(src);
		break;

	case CEC_OPCODE_GIVE_DEVICE_POWER_STATUS:
		if (cec_rate_limit(&cec_reply_power_deadline, 5000)) cec_send_power_status(src);
		break;

	case CEC_OPCODE_REQUEST_ACTIVE_SOURCE:
		if (cec_rate_limit(&cec_reply_active_deadline, 2000)) cec_send_active_source();
		break;

	case CEC_OPCODE_ACTIVE_SOURCE:
		if (msg->length >= 4)
		{
			uint16_t path = (uint16_t)((msg->data[0] << 8) | msg->data[1]);
			cec_active_physical_addr = path;
		}
		break;

	case CEC_OPCODE_SET_STREAM_PATH:
		if (msg->length >= 4)
		{
			uint16_t path = (uint16_t)((msg->data[0] << 8) | msg->data[1]);
			cec_active_physical_addr = path;
			if (path == cec_physical_addr && cec_rate_limit(&cec_reply_active_deadline, 2000)) cec_send_active_source();
		}
		break;

	case CEC_OPCODE_MENU_REQUEST:
		{
			if (!cec_rate_limit(&cec_reply_menu_deadline, 1000)) break;
			cec_message_t reply = {};
			reply.header = (cec_logical_addr << 4) | src;
			reply.opcode = CEC_OPCODE_MENU_STATUS;
			reply.data[0] = 0x00;
			reply.length = 3;
			cec_send_message(&reply);
		}
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

bool cec_init(bool enable)
{
	if (!enable)
	{
		cec_deinit();
		return true;
	}

	if (cec_enabled) return true;

	cec_deinit();
	cec_hpd_pulsed = false;

	cec_main_fd = i2c_open(ADV7513_MAIN_ADDR, 0);
	if (cec_main_fd < 0)
	{
		return false;
	}

	if (!cec_setup_main_registers())
	{
		cec_deinit();
		return false;
	}

	cec_fd = i2c_open(ADV7513_CEC_ADDR, 0);
	if (cec_fd < 0)
	{
		cec_deinit();
		return false;
	}

	cec_reg_write(CEC_REG_SOFT_RESET, 0x01);
	usleep(2000);
	cec_reg_write(CEC_REG_SOFT_RESET, 0x00);

	cec_reg_write(CEC_REG_TX_ENABLE, 0x00);
	cec_reg_write(CEC_REG_CLK_DIV, 0x3D);
	cec_reg_write(CEC_REG_INT_ENABLE,
		CEC_INT_RX_RDY1 | CEC_INT_RX_RDY2 | CEC_INT_RX_RDY3 |
		CEC_INT_TX_RETRY_TIMEOUT | CEC_INT_TX_ARBITRATION | CEC_INT_TX_DONE);
	cec_reg_write(CEC_REG_INT_CLEAR, 0x3F);
	cec_clear_rx_buffers();

	cec_enabled = true;
	cec_reply_phys_deadline = 0;
	cec_reply_name_deadline = 0;
	cec_reply_version_deadline = 0;
	cec_reply_power_deadline = 0;
	cec_reply_menu_deadline = 0;
	cec_reply_active_deadline = 0;
	cec_forced_clear_log_deadline = 0;
	cec_tx_fail_streak = 0;
	cec_tx_suppress_deadline = 0;
	cec_rx_fallback_stale_deadline = 0;
	cec_boot_activate_pending = false;
	cec_boot_activate_deadline = 0;
	cec_physical_addr = cec_read_physical_address();
	cec_logical_addr = cec_pick_logical_address_from_physical(cec_physical_addr);
	cec_program_logical_address(cec_logical_addr);
	cec_refresh_deadline = GetTimer(CEC_MAIN_REFRESH_MS);

	if (cec_debug_enabled())
	{
		printf("CEC: logical=%u physical=%X.%X.%X.%X\n",
			cec_logical_addr,
			(cec_physical_addr >> 12) & 0x0F,
			(cec_physical_addr >> 8) & 0x0F,
			(cec_physical_addr >> 4) & 0x0F,
			cec_physical_addr & 0x0F);
	}

	bool pa_ok = false;
	bool wake_ok = false;
	bool text_ok = false;
	bool active_ok = false;
	const char *osd_name = cec_get_osd_name();
	pa_ok = cec_send_report_physical_address(); usleep(20000);
	(void)cec_send_set_osd_name(osd_name); usleep(20000);
	wake_ok = cec_send_image_view_on(); usleep(20000);
	text_ok = cec_send_text_view_on(); usleep(20000);
	active_ok = cec_send_active_source(); usleep(20000);

	cec_boot_activate_pending = true;
	cec_boot_activate_deadline = GetTimer(1200);

	printf("CEC: announce wake=%d text=%d phys=%d name=\"%s\" active=%d\n",
		wake_ok ? 1 : 0,
		text_ok ? 1 : 0,
		pa_ok ? 1 : 0,
		osd_name,
		active_ok ? 1 : 0);

	cec_input_activity_seq = input_activity_get_seq();
	unsigned long idle_ms = cec_idle_sleep_delay_ms();
	cec_idle_deadline = idle_ms ? GetTimer(idle_ms) : 0;
	cec_idle_engaged = false;

	return true;
}

void cec_deinit(void)
{
	cec_release_key();

	if (cec_fd >= 0)
	{
		cec_reg_write(CEC_REG_TX_ENABLE, 0x00);
		cec_reg_write(CEC_REG_INT_ENABLE, 0x00);
		cec_reg_write(CEC_REG_INT_CLEAR, 0x3F);
		cec_reg_write(CEC_REG_LOG_ADDR_MASK, 0x00);
		i2c_close(cec_fd);
	}

	if (cec_main_fd >= 0)
	{
		i2c_close(cec_main_fd);
	}

	cec_fd = -1;
	cec_main_fd = -1;
	cec_enabled = false;
	cec_logical_addr = CEC_LOG_ADDR_PLAYBACK1;
	cec_physical_addr = CEC_DEFAULT_PHYS_ADDR;
	cec_refresh_deadline = 0;
	cec_reply_phys_deadline = 0;
	cec_reply_name_deadline = 0;
	cec_reply_version_deadline = 0;
	cec_reply_power_deadline = 0;
	cec_reply_menu_deadline = 0;
	cec_reply_active_deadline = 0;
	cec_forced_clear_log_deadline = 0;
	cec_tx_fail_streak = 0;
	cec_tx_suppress_deadline = 0;
	cec_rx_fallback_stale_deadline = 0;
	cec_boot_activate_pending = false;
	cec_boot_activate_deadline = 0;
	cec_active_physical_addr = 0xFFFF;
	cec_input_activity_seq = 0;
	cec_idle_deadline = 0;
	cec_idle_engaged = false;
}

void cec_poll(void)
{
	if (!cec_enabled) return;

	if (CheckTimer(cec_refresh_deadline))
	{
		cec_setup_main_registers();
		cec_refresh_deadline = GetTimer(CEC_MAIN_REFRESH_MS);
	}

	if (cec_boot_activate_pending && CheckTimer(cec_boot_activate_deadline))
	{
		bool wake_ok = cec_send_image_view_on();
		bool text_ok = cec_send_text_view_on();
		bool active_ok = cec_send_active_source();
		if (cec_debug_enabled()) printf("CEC: boot activate retry wake=%d text=%d active=%d\n",
			wake_ok ? 1 : 0,
			text_ok ? 1 : 0,
			active_ok ? 1 : 0);
		cec_boot_activate_pending = false;
	}

	cec_message_t msg = {};
	int max_msgs = 1;
	for (int i = 0; i < max_msgs; i++)
	{
		if (!cec_receive_message(&msg)) break;
		cec_handle_message(&msg);
	}

	cec_poll_idle_sleep_wake();
	cec_poll_key_timeout();
}

bool cec_is_enabled(void)
{
	return cec_enabled;
}
