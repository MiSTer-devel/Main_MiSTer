#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>

#include "hdmi_cec.h"
#include "cfg.h"
#include "fpga_io.h"
#include "hardware.h"
#include "user_io.h"
#include "input.h"
#include "smbus.h"
#include "video.h"

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
static const uint8_t MAIN_REG_INT1_STATUS = 0x97;
static const uint8_t MAIN_INT_EDID_READY = 1 << 2;
static const uint8_t MAIN_REG_EDID_SEGMENT = 0xC4;
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
static const uint8_t CEC_REG_RX_READY = 0x49;
static const uint8_t CEC_REG_RX_BUFFERS = 0x4A;
static const uint8_t CEC_REG_LOG_ADDR_MASK = 0x4B;
static const uint8_t CEC_REG_LOG_ADDR_0_1 = 0x4C;
static const uint8_t CEC_REG_LOG_ADDR_2 = 0x4D;
static const uint8_t CEC_REG_CLK_DIV = 0x4E;
static const uint8_t CEC_REG_SOFT_RESET = 0x50;

static const uint8_t CEC_INT_RX_RDY1 = 1 << 0;
static const uint8_t CEC_INT_RX_RDY2 = 1 << 1;
static const uint8_t CEC_INT_RX_RDY3 = 1 << 2;
static const uint8_t CEC_INT_RX_RDY_MASK = CEC_INT_RX_RDY1 | CEC_INT_RX_RDY2 | CEC_INT_RX_RDY3;
static const uint8_t CEC_INT_TX_RETRY_TIMEOUT = 1 << 3;
static const uint8_t CEC_INT_TX_ARBITRATION = 1 << 4;
static const uint8_t CEC_INT_TX_DONE = 1 << 5;
static const uint8_t CEC_INT_TX_MASK = CEC_INT_TX_RETRY_TIMEOUT | CEC_INT_TX_ARBITRATION | CEC_INT_TX_DONE;

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
static const uint8_t CEC_INPUT_MODE_ON = 1;

static const uint16_t CEC_INVALID_PHYS_ADDR = 0xFFFF;
static const unsigned long CEC_BUTTON_TIMEOUT_MS = 500;
static const unsigned long CEC_MAIN_REFRESH_MS = 2000;
static const unsigned long CEC_POLL_INTERVAL_MS = 20;
static const unsigned long CEC_EDID_RETRY_INITIAL_MS = 15000;
static const unsigned long CEC_EDID_RETRY_MAX_MS = 300000;
static const unsigned long CEC_EDID_READY_TIMEOUT_MS = 500;
static const unsigned long CEC_ADVERTISE_STEP_MS = 120;
static const unsigned long CEC_EDID_AFTER_SWITCH_RETRY_MS = 1000;
static const unsigned long CEC_POWER_ON_QUERY_WAIT_MS = 700;
static const unsigned long CEC_POWER_ON_STEP_MS = 120;
static const unsigned long CEC_POWER_ON_VERIFY_WAIT_MS = 700;
static const unsigned long CEC_TX_TIMEOUT_MS = 220;
static const unsigned long CEC_TX_TIMEOUT_RETRY_MS = 500;
static const size_t CEC_EDID_BLOCK_SIZE = 128;
static const size_t CEC_EDID_SEGMENT_SIZE = 256;
static const size_t CEC_EDID_MAX_BLOCKS = 8;
static const uint8_t CEC_ADVERTISE_STARTUP_ATTEMPTS = 1;
static const uint8_t CEC_ADVERTISE_IDENTITY_STEPS = 2;

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
	CEC_POWER_ON_TEXT,
	CEC_POWER_ON_ACTIVE,
	CEC_POWER_ON_REQUEST_VERIFY,
	CEC_POWER_ON_WAIT_VERIFY
};

static bool cec_enabled = false;
static int cec_main_fd = -1;
static int cec_fd = -1;
static uint8_t cec_logical_addr = CEC_LOG_ADDR_PLAYBACK1;
static uint16_t cec_physical_addr = CEC_INVALID_PHYS_ADDR;
static uint16_t cec_pressed_key = 0;
static unsigned long cec_press_deadline = 0;
static unsigned long cec_refresh_deadline = 0;
static unsigned long cec_poll_deadline = 0;
static unsigned long cec_edid_retry_deadline = 0;
static unsigned long cec_edid_retry_delay_ms = 0;
static bool cec_physical_addr_from_edid = false;
static unsigned long cec_reply_phys_deadline = 0;
static unsigned long cec_reply_name_deadline = 0;
static unsigned long cec_reply_version_deadline = 0;
static unsigned long cec_reply_power_deadline = 0;
static unsigned long cec_reply_menu_deadline = 0;
static unsigned long cec_reply_active_deadline = 0;
static unsigned long cec_reply_vendor_deadline = 0;
static uint8_t cec_tx_fail_streak = 0;
static unsigned long cec_tx_suppress_deadline = 0;
static unsigned long cec_advertise_deadline = 0;
static uint8_t cec_advertise_step = 0;
static uint8_t cec_advertise_attempts = 0;
static bool cec_startup_actions_scheduled = false;
static cec_power_on_state_t cec_power_on_state = CEC_POWER_ON_DONE;
static unsigned long cec_power_on_deadline = 0;
static bool cec_reload_video_after_edid = false;
static uint16_t cec_active_physical_addr = 0xFFFF;
static uint32_t cec_input_activity_seq = 0;
static unsigned long cec_idle_deadline = 0;
static bool cec_idle_engaged = false;

static bool cec_send_message(const cec_message_t *msg, bool with_retry = true);
static bool cec_has_physical_address(void);
static bool cec_send_image_view_on(bool with_retry = true);
static bool cec_send_text_view_on(bool with_retry = true);
static bool cec_send_active_source(bool with_retry = true);
static bool cec_send_request_active_source(bool with_retry = true);
static bool cec_send_report_physical_address(bool with_retry = true);
static bool cec_send_set_osd_name(const char *name, bool with_retry = true);
static bool cec_send_cec_version(uint8_t destination);
static void cec_handle_message(const cec_message_t *msg);
static bool cec_receive_message(cec_message_t *msg);

static unsigned long cec_retry = 0;
static bool cec_init_failed_logged = false;

static uint8_t cec_get_input_mode(void)
{
	if (cfg.hdmi_cec_input_mode <= CEC_INPUT_MODE_ON) return cfg.hdmi_cec_input_mode;
	return CEC_INPUT_MODE_ON;
}

static bool cec_is_osd_trigger_button(uint8_t button_code)
{
	return button_code == CEC_USER_CONTROL_EXIT || button_code == CEC_USER_CONTROL_ROOT_MENU;
}

static bool cec_accept_button_input(uint8_t button_code)
{
	(void)button_code;
	uint8_t mode = cec_get_input_mode();
	return mode != CEC_INPUT_MODE_OFF;
}

static unsigned long cec_idle_sleep_delay_ms(void)
{
	// Tie CEC sleep/wake to the existing video_off timeout.
	if (!cfg.video_off) return 0;

	// Match the same preset mapping as video idle blanking:
	// 1=15m, 2=30m, 3=45m, 4=60m.
	unsigned long minutes = (unsigned long)cfg.video_off;
	if (cfg.video_off <= 4) minutes = (unsigned long)cfg.video_off * 15ul;

	return minutes * 60ul * 1000ul;
}

static const char *cec_get_osd_name(void)
{
	return CEC_DEFAULT_OSD_NAME;
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

	user_io_kbd(cec_pressed_key, 0);
	cec_pressed_key = 0;
	cec_press_deadline = 0;
}

static uint16_t cec_button_to_key(uint8_t button_code)
{
	if (cec_is_osd_trigger_button(button_code)) return KEY_MENU;

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

static void cec_handle_button(uint8_t button_code, bool pressed)
{
	if (is_menu()) printf("CEC button: 0x%02X, pressed=%d\n", button_code, pressed);

	if (!pressed)
	{
		cec_release_key();
		return;
	}

	const uint16_t key = cec_button_to_key(button_code);
	if (!cec_accept_button_input(button_code)) return;
	if (!key) return;

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

static cec_tx_result_t cec_wait_for_tx(unsigned long timeout_ms)
{
	unsigned long timeout = GetTimer(timeout_ms);
	uint8_t low_drv_start = cec_reg_read(0x14);

	while (!CheckTimer(timeout))
	{
		uint8_t status = main_reg_read(MAIN_REG_INT1_STATUS);

		if (status & (CEC_INT_TX_RETRY_TIMEOUT | CEC_INT_TX_ARBITRATION))
		{
			cec_reg_write(CEC_REG_TX_ENABLE, 0x00);
			main_reg_write(MAIN_REG_INT1_STATUS, status & CEC_INT_TX_MASK);
			return CEC_TX_RESULT_NACK;
		}

		if (status & CEC_INT_TX_DONE)
		{
			cec_reg_write(CEC_REG_TX_ENABLE, 0x00);
			main_reg_write(MAIN_REG_INT1_STATUS, CEC_INT_TX_DONE);
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
	main_reg_write(MAIN_REG_INT1_STATUS, CEC_INT_TX_MASK);
	return CEC_TX_RESULT_TIMEOUT;
}

static bool cec_send_message(const cec_message_t *msg, bool with_retry)
{
	if (!cec_enabled || !msg) return false;
	if (msg->length < 1 || msg->length > 16) return false;
	if (!CheckTimer(cec_tx_suppress_deadline)) return false;

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
			printf("CEC: TX suppressed for 15000ms after repeated failures\n");
		}
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
	cec_reg_write(CEC_REG_RX_BUFFERS, 0x08);
}

static bool cec_setup_main_registers(bool clear_status = false)
{
	if (cec_main_fd < 0) return false;

	bool ok = true;

	ok &= main_reg_write(MAIN_REG_CEC_I2C_ADDR, ADV7513_CEC_ADDR << 1);
	ok &= main_reg_write(MAIN_REG_CEC_POWER, 0x00);
	uint8_t reg_e3 = main_reg_read(MAIN_REG_CEC_CTRL);
	ok &= main_reg_write(MAIN_REG_CEC_CTRL, reg_e3 | 0x0E);

	uint8_t reg_94 = main_reg_read(MAIN_REG_INT0_ENABLE);
	ok &= main_reg_write(MAIN_REG_INT0_ENABLE, reg_94 & ~0x80);
	ok &= main_reg_write(MAIN_REG_INT1_ENABLE, CEC_INT_RX_RDY_MASK);
	if (clear_status)
	{
		ok &= main_reg_write(MAIN_REG_INT0_STATUS, 0xFF);
		ok &= main_reg_write(MAIN_REG_INT1_STATUS, 0xFF);
	}

	if (!ok)
	{
		printf("CEC: main register setup failed\n");
	}

	return ok;
}

static bool cec_reg_write16(uint8_t reg, uint16_t value)
{
	if (cec_fd < 0) return false;
	printf("cec_reg_write16(0x%X, %d)\n", reg, value);
	if (i2c_smbus_write_byte_data(cec_fd, reg, value >> 8) < 0) return false;
	return i2c_smbus_write_byte_data(cec_fd, reg + 1, value) >= 0;
}

static bool cec_setup_clock()
{
	if (cec_fd < 0) return false;

	if (cfg.hdmi_cec_clock && (cfg.hdmi_cec_clock < 3 || cfg.hdmi_cec_clock > 100))
	{
		printf("CEC: clock (%.2f MHz) is outside of supported range 3-100 MHz\n", cfg.hdmi_cec_clock);
		return false;
	}

	int clock = !cfg.hdmi_cec_clock ? 12000 : (cfg.hdmi_cec_clock*1000);
	int base = (cfg.hdmi_cec_clock < 45) ? 750 : (cfg.hdmi_cec_clock < 60) ? 1000 : 2000;

	int clk_div = (clock / base) - 1;
	double us = (1000.f * (clk_div + 1)) / (float)clock;

	printf("cec_setup_clock: hdmi_cec_clock=%.2f clock=%d, base=%d, clk_div=%d, us=%.2f\n", cfg.hdmi_cec_clock, clock, base, clk_div, us);

	cec_reg_write(CEC_REG_CLK_DIV, clk_div << 2);

	//st_total 4.5ms
	cec_reg_write16(0x51, 4500 / us);

	//st_total_min 4.2ms
	cec_reg_write16(0x53, 4200 / us);

	//st_total_max 4.8ms
	cec_reg_write16(0x55, 4800 / us);

	//st_low 3.7ms
	cec_reg_write16(0x57, 3700 / us);

	//st_low_min 3.4ms
	cec_reg_write16(0x59, 3400 / us);

	//st_low_max 4.0ms
	cec_reg_write16(0x5B, 4000 / us);

	//bit_total 2.4ms
	cec_reg_write16(0x5D, 2400 / us);

	//bit_total_min 1.95ms
	cec_reg_write16(0x5F, 1950 / us);

	//bit_total_max 2.85ms
	cec_reg_write16(0x61, 2850 / us);

	//bit_low_one 0.6ms
	cec_reg_write16(0x63, 600 / us);

	//bit_low_zero 1.5ms
	cec_reg_write16(0x65, 1500 / us);

	//bit_low_max 1.8ms
	cec_reg_write16(0x67, 1800 / us);

	//sample_time 1.05ms
	cec_reg_write16(0x69, 1050 / us);

	//line_error_time 3.6ms
	cec_reg_write16(0x6B, 3600 / us);

	//rise_time 250us
	cec_reg_write16(0x6E, 250 / us);

	//bit_low_one_min 0.3ms
	cec_reg_write16(0x71, 300 / us);

	//bit_low_one_max 0.9ms
	cec_reg_write16(0x73, 900 / us);

	//bit_low_zero_min 1.2ms
	cec_reg_write16(0x75, 1200 / us);

	cec_reg_write(CEC_REG_CLK_DIV, (clk_div << 2) | 1);

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

static bool cec_edid_block_checksum_ok(const uint8_t *block)
{
	uint8_t sum = 0;
	for (size_t i = 0; i < CEC_EDID_BLOCK_SIZE; i++) sum = (uint8_t)(sum + block[i]);
	return sum == 0;
}

static bool cec_wait_edid_ready(void)
{
	unsigned long timeout = GetTimer(CEC_EDID_READY_TIMEOUT_MS);
	while (!CheckTimer(timeout))
	{
		if (main_reg_read(MAIN_REG_INT0_STATUS) & MAIN_INT_EDID_READY) return true;
		usleep(10000);
	}

	return (main_reg_read(MAIN_REG_INT0_STATUS) & MAIN_INT_EDID_READY) != 0;
}

static bool cec_read_edid_segment(int edid_fd, uint8_t segment, uint8_t *edid, int *read_errors)
{
	if (edid_fd < 0 || !edid) return false;

	main_reg_write(MAIN_REG_EDID_SEGMENT, segment);
	main_reg_write(MAIN_REG_INT0_STATUS, MAIN_INT_EDID_READY);
	main_reg_write(MAIN_REG_EDID_CTRL, 0x03);
	usleep(1000);
	main_reg_write(MAIN_REG_EDID_CTRL, 0x13);

	bool ready = cec_wait_edid_ready();
	int errors = 0;

	for (uint16_t i = 0; i < CEC_EDID_SEGMENT_SIZE; i++)
	{
		int value = i2c_smbus_read_byte_data(edid_fd, (uint8_t)i);
		if (value < 0)
		{
			errors++;
			edid[i] = 0;
			continue;
		}

		edid[i] = (uint8_t)value;
	}

	if (read_errors) *read_errors += errors;
	if (!ready || errors)
	{
		printf("CEC: EDID segment %u ready=%u read_errors=%d\n", segment, ready ? 1 : 0, errors);
	}

	return ready && errors == 0;
}

static bool cec_read_physical_address(uint16_t *physical_addr)
{
	uint8_t edid[CEC_EDID_BLOCK_SIZE * CEC_EDID_MAX_BLOCKS] = {};
	uint16_t addr = CEC_INVALID_PHYS_ADDR;

	if (physical_addr) *physical_addr = CEC_INVALID_PHYS_ADDR;
	if (!physical_addr || cec_main_fd < 0) return false;

	int edid_fd = i2c_open(ADV7513_EDID_ADDR, 0);
	if (edid_fd < 0) return false;

	int read_errors = 0;
	size_t blocks_read = 0;
	if (cec_read_edid_segment(edid_fd, 0, edid, &read_errors) &&
		edid[0] == 0x00 && edid[1] == 0xFF && edid[2] == 0xFF && edid[3] == 0xFF &&
		edid[4] == 0xFF && edid[5] == 0xFF && edid[6] == 0xFF && edid[7] == 0x00 &&
		cec_edid_block_checksum_ok(edid))
	{
		size_t total_blocks = 1 + edid[126];
		if (total_blocks > CEC_EDID_MAX_BLOCKS)
		{
			printf("CEC: EDID has %u blocks, reading first %u\n", (unsigned)total_blocks, (unsigned)CEC_EDID_MAX_BLOCKS);
			total_blocks = CEC_EDID_MAX_BLOCKS;
		}

		blocks_read = total_blocks;
		for (uint8_t segment = 1; segment < (uint8_t)((total_blocks + 1) / 2); segment++)
		{
			if (!cec_read_edid_segment(edid_fd, segment, edid + (segment * CEC_EDID_SEGMENT_SIZE), &read_errors))
			{
				blocks_read = segment * 2;
				break;
			}
		}

		for (size_t block = 1; block < blocks_read; block++)
		{
			if (!cec_edid_block_checksum_ok(edid + (block * CEC_EDID_BLOCK_SIZE)))
			{
				printf("CEC: EDID block %u checksum failed\n", (unsigned)block);
				blocks_read = block;
				break;
			}
		}
	}

	i2c_close(edid_fd);

	size_t edid_size = blocks_read * CEC_EDID_BLOCK_SIZE;
	bool found = cec_parse_physical_address(edid, edid_size, &addr);
	if (!found)
	{
		found = cec_parse_physical_address_loose(edid, edid_size ? edid_size : CEC_EDID_SEGMENT_SIZE, &addr);
	}

	if (read_errors)
	{
		printf("CEC: EDID read errors=%d\n", read_errors);
	}

	if (found) *physical_addr = addr;
	return found;
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

static bool cec_hdmi_int_active(void)
{
	return fpga_get_hdmi_int() != 0;
}

static bool cec_receive_message(cec_message_t *msg)
{
	if (!cec_enabled || !msg) return false;
	if (!cec_hdmi_int_active()) return false;

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

static bool cec_send_active_source(bool with_retry)
{
	if (!cec_has_physical_address()) return false;

	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
	msg.opcode = CEC_OPCODE_ACTIVE_SOURCE;
	msg.data[0] = (uint8_t)(cec_physical_addr >> 8);
	msg.data[1] = (uint8_t)(cec_physical_addr & 0xFF);
	msg.length = 4;
	return cec_send_message(&msg, with_retry);
}

static bool cec_send_request_active_source(bool with_retry)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
	msg.opcode = CEC_OPCODE_REQUEST_ACTIVE_SOURCE;
	msg.length = 2;
	return cec_send_message(&msg, with_retry);
}

static bool cec_send_image_view_on(bool with_retry)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_TV;
	msg.opcode = CEC_OPCODE_IMAGE_VIEW_ON;
	msg.length = 2;
	return cec_send_message(&msg, with_retry);
}

static bool cec_send_text_view_on(bool with_retry)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_TV;
	msg.opcode = CEC_OPCODE_TEXT_VIEW_ON;
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

bool cec_send_wake(void)
{
	if (!cec_enabled) return false;

	bool wake_ok = cec_send_image_view_on(); usleep(20000);
	bool text_ok = cec_send_text_view_on(); usleep(20000);
	bool active_ok = cec_send_active_source();

	return wake_ok && text_ok && active_ok;
}

static bool cec_send_report_physical_address(bool with_retry)
{
	if (!cec_has_physical_address()) return false;

	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
	msg.opcode = CEC_OPCODE_REPORT_PHYSICAL_ADDRESS;
	msg.data[0] = (uint8_t)(cec_physical_addr >> 8);
	msg.data[1] = (uint8_t)(cec_physical_addr & 0xFF);
	msg.data[2] = CEC_DEVICE_TYPE_PLAYBACK;
	msg.length = 5;
	return cec_send_message(&msg, with_retry);
}

static bool cec_send_set_osd_name(const char *name, bool with_retry)
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
	return cec_send_message(&msg, with_retry);
}

static bool cec_send_device_vendor_id(bool with_retry = true)
{
	cec_message_t msg = {};
	msg.header = (cec_logical_addr << 4) | CEC_LOG_ADDR_BROADCAST;
	msg.opcode = CEC_OPCODE_DEVICE_VENDOR_ID;
	msg.data[0] = 0x00;
	msg.data[1] = 0x00;
	msg.data[2] = 0x00;
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
	msg.data[0] = CEC_POWER_STATUS_ON;
	msg.length = 3;
	return cec_send_message(&msg);
}

static void cec_set_physical_address(uint16_t physical_addr)
{
	cec_physical_addr = physical_addr;
	cec_program_logical_address(cec_pick_logical_address_from_physical(cec_physical_addr));
}

static bool cec_has_physical_address(void)
{
	return cec_physical_addr_from_edid && cec_physical_addr != CEC_INVALID_PHYS_ADDR;
}

static bool cec_is_active_source(void)
{
	return cec_has_physical_address() && cec_active_physical_addr == cec_physical_addr;
}

static void cec_schedule_power_on_switch(unsigned long delay_ms)
{
	cec_power_on_state = CEC_POWER_ON_DONE;
	cec_power_on_deadline = 0;

	if (!cfg.hdmi_cec_power_on) return;

	cec_power_on_state = CEC_POWER_ON_REQUEST_BEFORE;
	cec_power_on_deadline = delay_ms ? GetTimer(delay_ms) : 0;
}

static void cec_schedule_edid_retry(unsigned long delay_ms)
{
	cec_edid_retry_delay_ms = delay_ms;
	cec_edid_retry_deadline = delay_ms ? GetTimer(delay_ms) : 0;
}

static void cec_backoff_edid_retry(void)
{
	if (!cec_edid_retry_delay_ms) cec_edid_retry_delay_ms = CEC_EDID_RETRY_INITIAL_MS;
	else if (cec_edid_retry_delay_ms < CEC_EDID_RETRY_MAX_MS)
	{
		cec_edid_retry_delay_ms *= 2;
		if (cec_edid_retry_delay_ms > CEC_EDID_RETRY_MAX_MS) cec_edid_retry_delay_ms = CEC_EDID_RETRY_MAX_MS;
	}

	cec_edid_retry_deadline = GetTimer(cec_edid_retry_delay_ms);
}

static void cec_schedule_advertise(uint8_t attempts, unsigned long delay_ms)
{
	cec_advertise_step = 0;
	cec_advertise_attempts = attempts;
	cec_advertise_deadline = attempts ? (delay_ms ? GetTimer(delay_ms) : 0) : 0;
}

static void cec_schedule_startup_actions(void)
{
	cec_startup_actions_scheduled = true;
	cec_schedule_advertise(CEC_ADVERTISE_STARTUP_ATTEMPTS, 0);
	cec_schedule_power_on_switch(CEC_ADVERTISE_STEP_MS * CEC_ADVERTISE_IDENTITY_STEPS);
}

static void cec_schedule_post_switch_edid_retry(void)
{
	if (cec_physical_addr_from_edid) return;

	cec_reload_video_after_edid = !video_has_valid_edid();
	cec_schedule_edid_retry(CEC_EDID_AFTER_SWITCH_RETRY_MS);
}

static void cec_poll_edid_retry(void)
{
	if (cec_physical_addr_from_edid) return;
	if (!cec_edid_retry_deadline || !CheckTimer(cec_edid_retry_deadline)) return;

	uint16_t physical_addr = CEC_INVALID_PHYS_ADDR;
	if (cec_read_physical_address(&physical_addr))
	{
		cec_physical_addr_from_edid = true;
		cec_schedule_edid_retry(0);
		cec_set_physical_address(physical_addr);
		if (cec_startup_actions_scheduled) cec_schedule_advertise(CEC_ADVERTISE_STARTUP_ATTEMPTS, 0);
		else cec_schedule_startup_actions();
		if (cec_reload_video_after_edid)
		{
			cec_reload_video_after_edid = false;
			video_reload_edid_mode();
		}
		return;
	}

	cec_backoff_edid_retry();
}

static void cec_poll_advertise(void)
{
	if (!cec_advertise_attempts) return;
	if (!CheckTimer(cec_advertise_deadline)) return;

	switch (cec_advertise_step)
	{
	case 0:
		cec_send_report_physical_address(false);
		break;

	case 1:
		cec_send_set_osd_name(cec_get_osd_name(), false);
		break;
	}

	cec_advertise_step++;
	if (cec_advertise_step >= CEC_ADVERTISE_IDENTITY_STEPS)
	{
		cec_advertise_step = 0;
		cec_advertise_attempts--;
		if (cec_advertise_attempts)
		{
			cec_advertise_deadline = GetTimer(CEC_ADVERTISE_STEP_MS);
		}
		else
		{
			cec_advertise_deadline = 0;
		}
	}
	else
	{
		cec_advertise_deadline = GetTimer(CEC_ADVERTISE_STEP_MS);
	}
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
		if (cec_has_physical_address() && cec_rate_limit(&cec_reply_phys_deadline, 2000)) cec_send_report_physical_address();
		break;

	case CEC_OPCODE_GIVE_OSD_NAME:
		if (cec_rate_limit(&cec_reply_name_deadline, 2000)) cec_send_set_osd_name(cec_get_osd_name());
		break;

	case CEC_OPCODE_GIVE_DEVICE_VENDOR_ID:
		if (cec_rate_limit(&cec_reply_vendor_deadline, 5000)) cec_send_device_vendor_id();
		break;

	case CEC_OPCODE_GET_CEC_VERSION:
		if (cec_rate_limit(&cec_reply_version_deadline, 5000)) cec_send_cec_version(src);
		break;

	case CEC_OPCODE_GIVE_DEVICE_POWER_STATUS:
		if (cec_rate_limit(&cec_reply_power_deadline, 5000)) cec_send_power_status(src);
		break;

	case CEC_OPCODE_REQUEST_ACTIVE_SOURCE:
		if (cec_is_active_source() && cec_rate_limit(&cec_reply_active_deadline, 2000)) cec_send_active_source();
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
	cec_logical_addr = CEC_LOG_ADDR_PLAYBACK1;
	cec_physical_addr = CEC_INVALID_PHYS_ADDR;
	cec_refresh_deadline = 0;
	cec_poll_deadline = 0;
	cec_edid_retry_deadline = 0;
	cec_edid_retry_delay_ms = 0;
	cec_physical_addr_from_edid = false;
	cec_reply_phys_deadline = 0;
	cec_reply_name_deadline = 0;
	cec_reply_version_deadline = 0;
	cec_reply_power_deadline = 0;
	cec_reply_menu_deadline = 0;
	cec_reply_active_deadline = 0;
	cec_reply_vendor_deadline = 0;
	cec_tx_fail_streak = 0;
	cec_tx_suppress_deadline = 0;
	cec_advertise_deadline = 0;
	cec_advertise_step = 0;
	cec_advertise_attempts = 0;
	cec_startup_actions_scheduled = false;
	cec_power_on_state = CEC_POWER_ON_DONE;
	cec_power_on_deadline = 0;
	cec_reload_video_after_edid = false;
	cec_active_physical_addr = 0xFFFF;
	cec_input_activity_seq = 0;
	cec_idle_deadline = 0;
	cec_idle_engaged = false;
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

	cec_main_fd = i2c_open(ADV7513_MAIN_ADDR, 0);
	if (cec_main_fd < 0)
	{
		return false;
	}

	if (!cec_setup_main_registers(true))
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

	if (!cec_setup_clock())
	{
		cec_deinit();
		return false;
	}

	usleep(2000);
	cec_reg_write(CEC_REG_SOFT_RESET, 0x00);
	cec_reg_write(CEC_REG_TX_ENABLE, 0x00);

	cec_clear_rx_buffers();
	main_reg_write(MAIN_REG_INT1_STATUS, 0xFF);

	cec_enabled = true;
	cec_reply_phys_deadline = 0;
	cec_reply_name_deadline = 0;
	cec_reply_version_deadline = 0;
	cec_reply_power_deadline = 0;
	cec_reply_menu_deadline = 0;
	cec_reply_active_deadline = 0;
	cec_reply_vendor_deadline = 0;
	cec_tx_fail_streak = 0;
	cec_tx_suppress_deadline = 0;
	cec_advertise_deadline = 0;
	cec_advertise_step = 0;
	cec_advertise_attempts = 0;
	cec_startup_actions_scheduled = false;
	cec_power_on_state = CEC_POWER_ON_DONE;
	cec_power_on_deadline = 0;
	cec_reload_video_after_edid = false;
	cec_poll_deadline = 0;
	uint16_t physical_addr = CEC_INVALID_PHYS_ADDR;
	cec_physical_addr = CEC_INVALID_PHYS_ADDR;
	cec_program_logical_address(CEC_LOG_ADDR_PLAYBACK1);
	if (cec_read_physical_address(&physical_addr))
	{
		cec_physical_addr_from_edid = true;
		cec_set_physical_address(physical_addr);
	}
	else
	{
		cec_physical_addr_from_edid = false;
	}
	cec_schedule_edid_retry(cec_physical_addr_from_edid ? 0 : CEC_EDID_RETRY_INITIAL_MS);
	cec_refresh_deadline = GetTimer(CEC_MAIN_REFRESH_MS);

	printf("CEC: logical=%u physical=%X.%X.%X.%X\n",
		cec_logical_addr,
		(cec_physical_addr >> 12) & 0x0F,
		(cec_physical_addr >> 8) & 0x0F,
		(cec_physical_addr >> 4) & 0x0F,
		cec_physical_addr & 0x0F);

	if (cec_physical_addr_from_edid) cec_schedule_startup_actions();

	cec_input_activity_seq = user_io_get_activity_seq();
	unsigned long idle_ms = cec_idle_sleep_delay_ms();
	cec_idle_deadline = idle_ms ? GetTimer(idle_ms) : 0;
	cec_idle_engaged = false;

	return true;
}

static void cec_poll_power_on_switch(void)
{
	if (cec_power_on_state == CEC_POWER_ON_DONE) return;
	if (!CheckTimer(cec_power_on_deadline)) return;

	switch (cec_power_on_state)
	{
	case CEC_POWER_ON_REQUEST_BEFORE:
		cec_active_physical_addr = 0xFFFF;
		cec_send_request_active_source(false);
		cec_power_on_state = CEC_POWER_ON_WAIT_BEFORE;
		cec_power_on_deadline = GetTimer(CEC_POWER_ON_QUERY_WAIT_MS);
		break;

	case CEC_POWER_ON_WAIT_BEFORE:
		if (cec_is_active_source())
		{
			cec_schedule_post_switch_edid_retry();
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
		cec_power_on_state = CEC_POWER_ON_TEXT;
		cec_power_on_deadline = GetTimer(CEC_POWER_ON_STEP_MS);
		break;

	case CEC_POWER_ON_TEXT:
		cec_send_text_view_on(false);
		cec_power_on_state = CEC_POWER_ON_ACTIVE;
		cec_power_on_deadline = GetTimer(CEC_POWER_ON_STEP_MS);
		break;

	case CEC_POWER_ON_ACTIVE:
		cec_send_active_source(false);
		cec_schedule_post_switch_edid_retry();
		cec_power_on_state = CEC_POWER_ON_REQUEST_VERIFY;
		cec_power_on_deadline = GetTimer(CEC_POWER_ON_VERIFY_WAIT_MS);
		break;

	case CEC_POWER_ON_REQUEST_VERIFY:
		cec_active_physical_addr = 0xFFFF;
		cec_send_request_active_source(false);
		cec_power_on_state = CEC_POWER_ON_WAIT_VERIFY;
		cec_power_on_deadline = GetTimer(CEC_POWER_ON_QUERY_WAIT_MS);
		break;

	case CEC_POWER_ON_WAIT_VERIFY:
	default:
		cec_power_on_state = CEC_POWER_ON_DONE;
		cec_power_on_deadline = 0;
		break;
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
		}
	}
}

static void cec_poll_key_timeout(void)
{
	if (cec_pressed_key && CheckTimer(cec_press_deadline))
	{
		cec_release_key();
	}
}

void cec_poll(void)
{
	if (cfg.hdmi_cec)
	{
		if (!cec_enabled && CheckTimer(cec_retry))
		{
			if (!cec_init(true))
			{
				if (cfg.debug && !cec_init_failed_logged) printf("CEC: init failed\n");
				cec_init_failed_logged = true;
				cec_retry = GetTimer(3000);
			}
			else
			{
				cec_init_failed_logged = false;
				cec_retry = 0;
			}
		}

		if (cec_enabled)
		{
			if (!CheckTimer(cec_poll_deadline)) return;
			cec_poll_deadline = GetTimer(CEC_POLL_INTERVAL_MS);

			if (CheckTimer(cec_refresh_deadline))
			{
				cec_setup_main_registers();
				cec_refresh_deadline = GetTimer(CEC_MAIN_REFRESH_MS);
			}

			cec_poll_edid_retry();
			cec_poll_advertise();

			cec_message_t msg = {};
			int max_msgs = 1;
			for (int i = 0; i < max_msgs; i++)
			{
				if (!cec_receive_message(&msg)) break;
				cec_handle_message(&msg);
			}

			cec_poll_power_on_switch();
			cec_poll_idle_sleep_wake();
			cec_poll_key_timeout();
		}
	}
}
