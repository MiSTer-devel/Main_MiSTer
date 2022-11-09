#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../../hardware.h"
#include "../../menu.h"
#include "../../file_io.h"
#include "../../debug.h"
#include "../../user_io.h"
#include "../../fpga_io.h"
#include "st_tos.h"

#define ST_WRITE_MEMORY 0x08
#define ST_READ_MEMORY  0x09
#define ST_ACK_DMA      0x0a
#define ST_NAK_DMA      0x0b
#define ST_GET_DMASTATE 0x0c

#define CONFIG_FILENAME  "ATARIST0.CFG"

const char* tos_mem[] = { "512kb", "1mb", "2mb", "4mb", "8mb", "14mb", "--", "--" };
const char* tos_scanlines[] = { "Off","25%","50%","75%" };
const char* tos_stereo[] = { "Mono","Stereo" };
const char* tos_chipset[] = { "ST","STE","MegaSTE","STEroids" };
const char* tos_chipset_short[] = { "ST","STe","MST","ST+" };

typedef struct {
	unsigned long system_ctrl;  // system control word
	char tos_img[1024];
	char cart_img[1024];
	char acsi_img[2][1024];
	char video_adjust[2];
	char cdc_control_redirect;
	char reserved;
	uint32_t ext_ctrl;
} tos_config_t;

static tos_config_t config;

fileTYPE hdd_image[2] = {};

static unsigned char dma_buffer[512];

static const char *acsi_cmd_name(int cmd) {
	static const char *cmdname[] = {
		"Test Drive Ready", "Restore to Zero", "Cmd $2", "Request Sense",
		"Format Drive", "Read Block limits", "Reassign Blocks", "Cmd $7",
		"Read Sector", "Cmd $9", "Write Sector", "Seek Block",
		"Cmd $C", "Cmd $D", "Cmd $E", "Cmd $F",
		"Cmd $10", "Cmd $11", "Inquiry", "Verify",
		"Cmd $14", "Mode Select", "Cmd $16", "Cmd $17",
		"Cmd $18", "Cmd $19", "Mode Sense", "Start/Stop Unit",
		"Cmd $1C", "Cmd $1D", "Cmd $1E", "Cmd $1F",
		// extended commands supported by ICD feature:
		"Cmd $20", "Cmd $21", "Cmd $22",
		"Read Format Capacities", "Cmd $24", "Read Capacity (10)",
		"Cmd $26", "Cmd $27", "Read (10)", "Read Generation",
		"Write (10)", "Seek (10)"
	};

	if (cmd > 0x2b) return NULL;

	return cmdname[cmd];
}

static void set_control(uint32_t ctrl)
{
	spi_uio_cmd_cont(UIO_SET_STATUS2);
	spi32_w(ctrl);
	spi32_w(config.ext_ctrl);
	DisableIO();
}

void tos_update_sysctrl(uint32_t ctrl)
{
	config.system_ctrl = ctrl;
	set_control(config.system_ctrl);
}

uint32_t tos_get_extctrl()
{
	return config.ext_ctrl;
}

void tos_set_extctrl(uint32_t ext_ctrl)
{
	config.ext_ctrl = ext_ctrl;
	set_control(config.system_ctrl);
}

unsigned long tos_system_ctrl()
{
	return config.system_ctrl;
}

int tos_get_ar()
{
	int ar = 0;
	if (config.system_ctrl & TOS_CONTROL_VIDEO_AR1) ar |= 1;
	if (config.system_ctrl & TOS_CONTROL_VIDEO_AR2) ar |= 2;

	return ar;
}

void tos_set_ar(int ar)
{
	if (ar & 1) config.system_ctrl |= TOS_CONTROL_VIDEO_AR1;
	else config.system_ctrl &= ~TOS_CONTROL_VIDEO_AR1;

	if (ar & 2) config.system_ctrl |= TOS_CONTROL_VIDEO_AR2;
	else config.system_ctrl &= ~TOS_CONTROL_VIDEO_AR2;
}

static void memory_read(uint8_t *data, uint32_t words)
{
	EnableIO();
	spi8(ST_READ_MEMORY);

	// transmitted bytes must be multiple of 2 (-> words)
	uint16_t *buf = (uint16_t*)data;
	while (words--) *buf++ = spi_w(0);

	DisableIO();
}

static void memory_write(uint8_t *data, uint32_t words)
{
	EnableIO();
	spi8(ST_WRITE_MEMORY);

	uint16_t *buf = (uint16_t*)data;
	while (words--) spi_w(*buf++);

	DisableIO();
}

static void dma_ack(uint8_t status)
{
	EnableIO();
	spi8(ST_ACK_DMA);
	spi8(status);
	DisableIO();
}

static void dma_nak()
{
	EnableIO();
	spi8(ST_NAK_DMA);
	DisableIO();
}

static void handle_acsi(unsigned char *buffer)
{
	static uint8_t buf[65536];

	static uint8_t asc[2] = { 0,0 };
	uint8_t target = buffer[10] >> 5;
	uint8_t device = buffer[1] >> 5;
	uint8_t cmd = buffer[0];
	uint32_t lba = 256 * 256 * (buffer[1] & 0x1f) + 256 * buffer[2] + buffer[3];
	uint32_t length = buffer[4];
	if (length == 0) length = 256;

	if (0)
	{
		tos_debugf("ACSI: target %d.%d, \"%s\" (%02x)", target, device, acsi_cmd_name(cmd), cmd);
		tos_debugf("ACSI: lba %u (%x), length %u", lba, lba, length);
	}

	// only a harddisk on ACSI 0/1 is supported
	// ACSI 0/1 is only supported if a image is loaded
	if (((target < 2) && (hdd_image[target].size != 0)))
	{
		uint32_t blocks = hdd_image[target].size / 512;

		// only lun0 is fully supported
		switch (cmd) {
		case 0x25:
			if (device == 0) {
				bzero(dma_buffer, 512);
				dma_buffer[0] = (blocks - 1) >> 24;
				dma_buffer[1] = (blocks - 1) >> 16;
				dma_buffer[2] = (blocks - 1) >> 8;
				dma_buffer[3] = (blocks - 1) >> 0;
				dma_buffer[6] = 2;  // 512 bytes per block

				memory_write(dma_buffer, 4);

				dma_ack(0x00);
				asc[target] = 0x00;
			}
			else {
				dma_ack(0x02);
				asc[target] = 0x25;
			}
			break;

		case 0x00: // test drive ready
		case 0x04: // format
			if (device == 0) {
				asc[target] = 0x00;
				dma_ack(0x00);
			}
			else {
				asc[target] = 0x25;
				dma_ack(0x02);
			}
			break;

		case 0x03: // request sense
			if (device != 0)
				asc[target] = 0x25;

			bzero(dma_buffer, 512);
			dma_buffer[7] = 0x0b;
			if (asc[target] != 0) {
				dma_buffer[2] = 0x05;
				dma_buffer[12] = asc[target];
			}
			memory_write(dma_buffer, 9); // 18 bytes
			dma_ack(0x00);
			asc[target] = 0x00;
			break;

		case 0x08: // read sector
		case 0x28: // read (10)
			if (device == 0)
			{
				if (cmd == 0x28)
				{
					lba =
						256 * 256 * 256 * buffer[2] +
						256 * 256 * buffer[3] +
						256 * buffer[4] +
						buffer[5];

					length = 256 * buffer[7] + buffer[8];
					//	  iprintf("READ(10) %d, %d\n", lba, length);
				}

				if (lba + length <= blocks)
				{
					DISKLED_ON;
					FileSeekLBA(&hdd_image[target], lba);
					while (length)
					{
						uint32_t len = length;
						if (len > 128) len = 128;
						length -= len;

						len *= 512;
						FileReadAdv(&hdd_image[target], buf, len);
						memory_write(buf, len / 2);
					}
					DISKLED_OFF;

					dma_ack(0x00);
					asc[target] = 0x00;
				}
				else
				{
					tos_debugf("ACSI: read (%u+%d) exceeds device limits (%u)", lba, length, blocks);
					dma_ack(0x02);
					asc[target] = 0x21;
				}
			}
			else
			{
				dma_ack(0x02);
				asc[target] = 0x25;
			}
			break;

		case 0x0a: // write sector
		case 0x2a: // write (10)
			if (device == 0)
			{
				if (cmd == 0x2a)
				{
					lba =
						256 * 256 * 256 * buffer[2] +
						256 * 256 * buffer[3] +
						256 * buffer[4] +
						buffer[5];

					length = 256 * buffer[7] + buffer[8];

					//	  iprintf("WRITE(10) %d, %d\n", lba, length);
				}

				if (lba + length <= blocks)
				{
					DISKLED_ON;
					FileSeekLBA(&hdd_image[target], lba);
					while (length)
					{
						uint32_t len = length;
						if (len > 128) len = 128;
						length -= len;

						len *= 512;
						memory_read(buf, len / 2);
						FileWriteAdv(&hdd_image[target], buf, len);
					}
					DISKLED_OFF;
					dma_ack(0x00);
					asc[target] = 0x00;
				}
				else {
					tos_debugf("ACSI: write (%u+%d) exceeds device limits (%u)", lba, length, blocks);
					dma_ack(0x02);
					asc[target] = 0x21;
				}
			}
			else {
				dma_ack(0x02);
				asc[target] = 0x25;
			}
			break;

		case 0x12: // inquiry
			tos_debugf("ACSI: Inquiry %.11s", hdd_image[target].name);
			bzero(dma_buffer, 512);
			dma_buffer[2] = 2;                                   // SCSI-2
			dma_buffer[4] = length - 5;                          // len
			memcpy(dma_buffer + 8, "MISTer  ", 8);               // Vendor
			memcpy(dma_buffer + 16, "                ", 16);     // Clear device entry
			memcpy(dma_buffer + 16, hdd_image[target].name, 11); // Device
			memcpy(dma_buffer + 32, "ATH ", 4);                  // Product revision
			memcpy(dma_buffer + 36, VDATE "  ", 8);              // Serial number
			if (device != 0) dma_buffer[0] = 0x7f;
			memory_write(dma_buffer, length / 2);
			dma_ack(0x00);
			asc[target] = 0x00;
			break;

		case 0x1a: // mode sense
			if (device == 0) {
				tos_debugf("ACSI: mode sense, blocks = %u", blocks);
				bzero(dma_buffer, 512);
				dma_buffer[3] = 8;            // size of extent descriptor list
				dma_buffer[5] = blocks >> 16;
				dma_buffer[6] = blocks >> 8;
				dma_buffer[7] = blocks;
				dma_buffer[10] = 2;           // byte 1 of block size in bytes (512)
				memory_write(dma_buffer, length / 2);
				dma_ack(0x00);
				asc[target] = 0x00;
			}
			else {
				asc[target] = 0x25;
				dma_ack(0x02);
			}
			break;

#if 0
		case 0x1f: // ICD command?
			tos_debugf("ACSI: ICD command %s ($%02x)",
				acsi_cmd_name(buffer[1] & 0x1f), buffer[1] & 0x1f);
			asc[target] = 0x05;
			dma_ack(0x02);
			break;
#endif

		default:
			tos_debugf("ACSI: >>>>>>>>>>>> Unsupported command <<<<<<<<<<<<<<<<");
			asc[target] = 0x20;
			dma_ack(0x02);
			break;
		}
	}
	else {
		tos_debugf("ACSI: Request for unsupported target");

		// tell acsi state machine that io controller is done
		// but don't generate a acsi irq
		dma_nak();
	}
}

static void get_dmastate()
{
	uint8_t buffer[16];

	EnableIO();
	spi8(ST_GET_DMASTATE);
	spi_read(buffer, 16, 0);
	DisableIO();

	if (buffer[10] & 0x01) handle_acsi(buffer);
}

static void fill_tx(uint16_t fill, uint32_t len, int index)
{
	user_io_set_index(index);
	user_io_set_download(1);

	len /= 2;
	EnableFpga();
	spi8(FIO_FILE_TX_DAT);
	while(len--) spi_w(fill);
	DisableFpga();

	user_io_set_download(0);
}

static char tos_cart_img[1024] = {};
void tos_load_cartridge(const char *name)
{
	if (name)
	{
		strncpy(tos_cart_img, name, 1023);
	}
	else
	{
		tos_debugf("Set cartridge: %s\n", tos_cart_img);

		const int sz = (128 * 1024) + 4;
		uint8_t *buf = new uint8_t[sz];
		if (buf)
		{
			memset(buf, -1, sz);
			if (!(config.system_ctrl & TOS_CONTROL_DONGLE)) FileLoad(tos_cart_img, buf, sz);

			user_io_set_index(2);
			user_io_set_download(1);
			user_io_file_tx_data(buf + 4, sz - 4);
			DisableFpga();

			user_io_set_download(0);
			delete buf;
		}
	}
}

char tos_cartridge_is_inserted()
{
	return tos_cart_img[0];
}

void tos_poll()
{
	static unsigned long timer = 0;

	get_dmastate();

	// check the user button
	if (!user_io_osd_is_visible() && (user_io_user_button() || user_io_get_kbd_reset()))
	{
		if (!timer) timer = GetTimer(1000);
		else if (timer != 1)
		{
			if (CheckTimer(timer))
			{
				tos_insert_disk(0, "");
				tos_insert_disk(1, "");
				tos_reset(1);
				timer = 1;
			}
		}
	}
	else
	{
		timer = 0;
	}
}

const char *tos_get_disk_name(int index)
{
	const char *name = 0;
	if(index <= 1) name = get_image_name(index);
	else
	{
		if (!hdd_image[index & 1].size) name = 0;
		else
		{
			name = strrchr(hdd_image[index & 1].name, '/');
			if (!name) name = hdd_image[index & 1].name; else name++;
		}
	}

	return name ? name : "* no disk *";
}

const char *tos_get_image_name()
{
	char *p = strrchr(config.tos_img, '/');
	return p ? p+1 : config.tos_img;
}

const char *tos_get_cartridge_name()
{
	if (!tos_cart_img[0])  return "* no cartridge *";
	char *p = strrchr(tos_cart_img, '/');
	return p ? p + 1 : tos_cart_img;
}

char tos_disk_is_inserted(int index)
{
	if (index <= 1) return (get_image_name(index) != NULL);
	return hdd_image[index & 1].size != 0;
}

static void tos_select_hdd_image(int i, const char *name)
{
	tos_debugf("Select ACSI%c image %s", '0' + i, name);

	strcpy(config.acsi_img[i], name);
	if (!strlen(name))
	{
		FileClose(&hdd_image[i]);
		hdd_image[i].size = 0;
		config.system_ctrl &= ~(TOS_ACSI0_ENABLE << i);
	}
	else
	{
		if (FileOpenEx(&hdd_image[i], name, (O_RDWR | O_SYNC)))
		{
			config.system_ctrl |= (TOS_ACSI0_ENABLE << i);
		}
	}

	// update system control
	set_control(config.system_ctrl);
}

void tos_insert_disk(int index, const char *name)
{
	static int wpins = 0;

	if (index <= 1)
	{
		user_io_file_mount(name, index);
		if (tos_disk_is_inserted(index))
		{
			if (!index) wpins &= ~TOS_CONTROL_FDC_WR_PROT_A;
			else wpins &= ~TOS_CONTROL_FDC_WR_PROT_B;
		}
		else
		{
			if (!index) wpins |= TOS_CONTROL_FDC_WR_PROT_A;
			else wpins |= TOS_CONTROL_FDC_WR_PROT_B;
		}

		set_control(config.system_ctrl ^ (wpins & (TOS_CONTROL_FDC_WR_PROT_A | TOS_CONTROL_FDC_WR_PROT_B)));
	}
	else tos_select_hdd_image(index & 1, name);
}

// force ejection of all disks (SD card has been removed)
void tos_eject_all()
{
	for (int i = 0; i < 4; i++) tos_insert_disk(i, "");
}

void tos_reset(char cold)
{
	tos_update_sysctrl(config.system_ctrl | TOS_CONTROL_CPU_RESET);  // set reset
	if (cold)
	{
		// clear first 16k
		tos_debugf("Clear first 16k");
		fill_tx(0, 16 * 1024, 3);

		// upload and verify tos image
		int len = FileLoad(config.tos_img, 0, 0);
		if (len)
		{
			tos_debugf("TOS.IMG:\n  size = %d", len);

			if (len >= 256 * 1024) user_io_file_tx(config.tos_img, 0);
			else if (len == 192 * 1024) user_io_file_tx(config.tos_img, 1);
			else tos_debugf("WARNING: Unexpected TOS size!");
		}
		else
		{
			tos_debugf("Unable to find tos.img");
			return;
		}

		for (int i = 0; i < 2; i++)
		{
			if (FileExists(config.acsi_img[i]))
			{
				tos_select_hdd_image(i, config.acsi_img[i]);
			}
		}
	}
	tos_load_cartridge(NULL);
	tos_update_sysctrl(config.system_ctrl & ~TOS_CONTROL_CPU_RESET);  // release reset
}

void tos_upload(const char *name)
{
	if(name) strcpy(config.tos_img, name);
	tos_reset(1);
}

// load/init configuration
void tos_config_load(int slot)
{
	char name[64] = { CONFIG_FILENAME };

	static char last_slot = 0;
	char new_slot;

	tos_eject_all();

	new_slot = (slot == -1) ? last_slot : slot;
	memset(&config, 0, sizeof(config));

	// set default values
	config.system_ctrl = TOS_MEMCONFIG_1M | TOS_CONTROL_VIDEO_COLOR | TOS_CONTROL_BORDER;
	strcpy(config.tos_img, HomeDir());
	strcat(config.tos_img, "/TOS.IMG");

	// try to load config
	name[7] = '0' + new_slot;
	int len = FileLoadConfig(name, 0, 0);
	tos_debugf("Configuration file size: %d (should be %d)", len, sizeof(tos_config_t));
	FileLoadConfig(name, &config, sizeof(tos_config_t));
}

// save configuration
void tos_config_save(int slot)
{
	char name[64] = { CONFIG_FILENAME };
	name[7] = '0' + slot;
	FileSaveConfig(name, &config, sizeof(config));
}

// configuration file check
int tos_config_exists(int slot)
{
	char name[64] = { CONFIG_FILENAME };
	name[7] = '0' + slot;
	return FileLoadConfig(name, 0, 0);
}

const char* tos_get_cfg_string(int num)
{
	static char str[256];

	char name[64] = { CONFIG_FILENAME };
	name[7] = '0' + num;

	static tos_config_t tmp;
	memset(&tmp, 0, sizeof(tmp));

	int len = FileLoadConfig(name, 0, 0);
	if (len)
	{
		FileLoadConfig(name, &tmp, sizeof(tos_config_t));

		memset(str, 0, sizeof(str));
		strcat(str, tos_mem[(tmp.system_ctrl >> 1) & 7]);
		strcat(str, " ");
		if (tmp.acsi_img[0][0]) strcat(str, "H0 ");
		if (tmp.acsi_img[1][0]) strcat(str, "H1 ");
		//if (tmp.cart_img[0]) strcat(str, "CR ");
		strcat(str, tos_chipset_short[(tmp.system_ctrl >> 23) & 3]);
		strcat(str, " ");
		if (!((tmp.system_ctrl >> 23) & 3) && (tmp.system_ctrl & TOS_CONTROL_BLITTER)) strcat(str, "B ");
		if (tmp.system_ctrl & TOS_CONTROL_VIKING) strcat(str, "V ");
		int ar = 0;
		if (tmp.system_ctrl & TOS_CONTROL_VIDEO_AR1) ar |= 1;
		if (tmp.system_ctrl & TOS_CONTROL_VIDEO_AR2) ar |= 2;
		sprintf(str+strlen(str), "A%d ", ar);
		strcat(str, (tmp.system_ctrl & TOS_CONTROL_VIDEO_COLOR) ? "C " : (tmp.system_ctrl & TOS_CONTROL_MDE60) ? "M6 " : "M ");
		if (!(tmp.system_ctrl & TOS_CONTROL_BORDER)) strcat(str, "F ");
		int sl = (tmp.system_ctrl >> 20) & 3;
		if (sl) sprintf(str + strlen(str), "S%d ", sl);
		strcat(str, (tmp.system_ctrl & TOS_CONTROL_STEREO) ? "AS " : "AM ");

		if (tmp.tos_img[0])
		{
			char *p = strrchr(tmp.tos_img, '/');
			if (!p) p = tmp.tos_img; else p++;
			int len = strlen(p);
			if (len >= 4) len -= 4;
			memcpy(str + strlen(str), p, len);
		}
		return str;
	}

	return "< empty slot >";
}
