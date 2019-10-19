#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "fpga_io.h"
#include "file_io.h"
#include "input.h"
#include "osd.h"

#include "fpga_base_addr_ac5.h"
#include "fpga_manager.h"
#include "fpga_system_manager.h"
#include "fpga_reset_manager.h"
#include "fpga_nic301.h"

#define FPGA_REG_BASE 0xFF000000
#define FPGA_REG_SIZE 0x01000000

#define MAP_ADDR(x) (volatile uint32_t*)(&map_base[(((uint32_t)(x)) & 0xFFFFFF)>>2])
#define IS_REG(x) (((((uint32_t)(x))-1)>=(FPGA_REG_BASE - 1)) && ((((uint32_t)(x))-1)<(FPGA_REG_BASE + FPGA_REG_SIZE - 1)))

#define fatal(x) munmap((void*)map_base, FPGA_REG_SIZE); close(fd); exit(x)

static struct socfpga_reset_manager  *reset_regs   = (socfpga_reset_manager *)SOCFPGA_RSTMGR_ADDRESS;
static struct socfpga_fpga_manager   *fpgamgr_regs = (socfpga_fpga_manager *)SOCFPGA_FPGAMGRREGS_ADDRESS;
static struct socfpga_system_manager *sysmgr_regs  = (socfpga_system_manager *)SOCFPGA_SYSMGR_ADDRESS;
static struct nic301_registers       *nic301_regs  = (nic301_registers *)SOCFPGA_L3REGS_ADDRESS;

static uint32_t *map_base;
static int fd;

static __inline void writel(uint32_t val, const void* reg)
{
/*
	if(!IS_REG(reg))
	{
		printf("ERROR: Trying to write undefined address: %p\n.", reg);
		fatal(-1);
	}
*/
	*MAP_ADDR(reg) = val;
}

static __inline uint32_t readl(const void* reg)
{
/*
	if (!IS_REG(reg))
	{
		printf("ERROR: Trying to read undefined address: %p\n.", reg);
		fatal(-1);
	}
*/
	return *MAP_ADDR(reg);
}

#define clrsetbits_le32(addr, clear, set) writel((readl(addr) & ~(clear)) | (set), addr)
#define setbits_le32(addr, set)           writel( readl(addr) | (set), addr)
#define clrbits_le32(addr, clear)         writel( readl(addr) & ~(clear), addr)

/* Timeout count */
#define FPGA_TIMEOUT_CNT		0x1000000

/* Set CD ratio */
static void fpgamgr_set_cd_ratio(unsigned long ratio)
{
	clrsetbits_le32(&fpgamgr_regs->ctrl,
		0x3 << FPGAMGRREGS_CTRL_CDRATIO_LSB,
		(ratio & 0x3) << FPGAMGRREGS_CTRL_CDRATIO_LSB);
}

static int fpgamgr_dclkcnt_set(unsigned long cnt)
{
	unsigned long i;

	/* Clear any existing done status */
	if (readl(&fpgamgr_regs->dclkstat))
		writel(0x1, &fpgamgr_regs->dclkstat);

	/* Write the dclkcnt */
	writel(cnt, &fpgamgr_regs->dclkcnt);

	/* Wait till the dclkcnt done */
	for (i = 0; i < FPGA_TIMEOUT_CNT; i++) {
		if (!readl(&fpgamgr_regs->dclkstat))
			continue;

		writel(0x1, &fpgamgr_regs->dclkstat);
		return 0;
	}

	return -ETIMEDOUT;
}

/* Check whether FPGA Init_Done signal is high */
static int is_fpgamgr_initdone_high(void)
{
	unsigned long val;

	val = readl(&fpgamgr_regs->gpio_ext_porta);
	return val & FPGAMGRREGS_MON_GPIO_EXT_PORTA_ID_MASK;
}

/* Get the FPGA mode */
static int fpgamgr_get_mode(void)
{
	unsigned long val;

	val = readl(&fpgamgr_regs->stat);
	return val & FPGAMGRREGS_STAT_MODE_MASK;
}

/* Check whether FPGA is ready to be accessed */
static int fpgamgr_test_fpga_ready(void)
{
	/* Check for init done signal */
	if (!is_fpgamgr_initdone_high())
		return 0;

	/* Check again to avoid false glitches */
	if (!is_fpgamgr_initdone_high())
		return 0;

	if (fpgamgr_get_mode() != FPGAMGRREGS_MODE_USERMODE)
		return 0;

	return 1;
}

/*
// Poll until FPGA is ready to be accessed or timeout occurred
static int fpgamgr_poll_fpga_ready(void)
{
	unsigned long i;

	// If FPGA is blank, wait till WD invoke warm reset
	for (i = 0; i < FPGA_TIMEOUT_CNT; i++) {
		// check for init done signal
		if (!is_fpgamgr_initdone_high())
			continue;
		// check again to avoid false glitches
		if (!is_fpgamgr_initdone_high())
			continue;
		return 1;
	}

	return 0;
}
*/

/* Start the FPGA programming by initialize the FPGA Manager */
static int fpgamgr_program_init(void)
{
	unsigned long msel, i;

	/* Get the MSEL value */
	msel = readl(&fpgamgr_regs->stat);
	msel &= FPGAMGRREGS_STAT_MSEL_MASK;
	msel >>= FPGAMGRREGS_STAT_MSEL_LSB;

	/*
	* Set the cfg width
	* If MSEL[3] = 1, cfg width = 32 bit
	*/
	if (msel & 0x8) {
		setbits_le32(&fpgamgr_regs->ctrl,
			FPGAMGRREGS_CTRL_CFGWDTH_MASK);

		/* To determine the CD ratio */
		/* MSEL[1:0] = 0, CD Ratio = 1 */
		if ((msel & 0x3) == 0x0)
			fpgamgr_set_cd_ratio(CDRATIO_x1);
		/* MSEL[1:0] = 1, CD Ratio = 4 */
		else if ((msel & 0x3) == 0x1)
			fpgamgr_set_cd_ratio(CDRATIO_x4);
		/* MSEL[1:0] = 2, CD Ratio = 8 */
		else if ((msel & 0x3) == 0x2)
			fpgamgr_set_cd_ratio(CDRATIO_x8);

	}
	else {	/* MSEL[3] = 0 */
		clrbits_le32(&fpgamgr_regs->ctrl,
			FPGAMGRREGS_CTRL_CFGWDTH_MASK);

		/* To determine the CD ratio */
		/* MSEL[1:0] = 0, CD Ratio = 1 */
		if ((msel & 0x3) == 0x0)
			fpgamgr_set_cd_ratio(CDRATIO_x1);
		/* MSEL[1:0] = 1, CD Ratio = 2 */
		else if ((msel & 0x3) == 0x1)
			fpgamgr_set_cd_ratio(CDRATIO_x2);
		/* MSEL[1:0] = 2, CD Ratio = 4 */
		else if ((msel & 0x3) == 0x2)
			fpgamgr_set_cd_ratio(CDRATIO_x4);
	}

	/* To enable FPGA Manager configuration */
	clrbits_le32(&fpgamgr_regs->ctrl, FPGAMGRREGS_CTRL_NCE_MASK);

	/* To enable FPGA Manager drive over configuration line */
	setbits_le32(&fpgamgr_regs->ctrl, FPGAMGRREGS_CTRL_EN_MASK);

	/* Put FPGA into reset phase */
	setbits_le32(&fpgamgr_regs->ctrl, FPGAMGRREGS_CTRL_NCONFIGPULL_MASK);

	/* (1) wait until FPGA enter reset phase */
	for (i = 0; i < FPGA_TIMEOUT_CNT; i++) {
		if (fpgamgr_get_mode() == FPGAMGRREGS_MODE_RESETPHASE)
			break;
	}

	/* If not in reset state, return error */
	if (fpgamgr_get_mode() != FPGAMGRREGS_MODE_RESETPHASE) {
		puts("FPGA: Could not reset\n");
		return -1;
	}

	/* Release FPGA from reset phase */
	clrbits_le32(&fpgamgr_regs->ctrl, FPGAMGRREGS_CTRL_NCONFIGPULL_MASK);

	/* (2) wait until FPGA enter configuration phase */
	for (i = 0; i < FPGA_TIMEOUT_CNT; i++) {
		if (fpgamgr_get_mode() == FPGAMGRREGS_MODE_CFGPHASE)
			break;
	}

	/* If not in configuration state, return error */
	if (fpgamgr_get_mode() != FPGAMGRREGS_MODE_CFGPHASE) {
		puts("FPGA: Could not configure\n");
		return -2;
	}

	/* Clear all interrupts in CB Monitor */
	writel(0xFFF, &fpgamgr_regs->gpio_porta_eoi);

	/* Enable AXI configuration */
	setbits_le32(&fpgamgr_regs->ctrl, FPGAMGRREGS_CTRL_AXICFGEN_MASK);

	return 0;
}

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

/* Write the RBF data to FPGA Manager */
static void fpgamgr_program_write(const void *rbf_data, unsigned long rbf_size)
{
	uint32_t src = (uint32_t)rbf_data;
	uint32_t dst = (uint32_t)MAP_ADDR(SOCFPGA_FPGAMGRDATA_ADDRESS);

	/* Number of loops for 32-byte long copying. */
	uint32_t loops32 = rbf_size / 32;
	/* Number of loops for 4-byte long copying + trailing bytes */
	uint32_t loops4 = DIV_ROUND_UP(rbf_size % 32, 4);

	__asm volatile(
		"1:	ldmia %0!,{r0-r7}   \n"
		"	stmia %1!,{r0-r7}   \n"
		"	sub	  %1, #32       \n"
		"	subs  %2, #1        \n"
		"	bne   1b            \n"
		"	cmp   %3, #0        \n"
		"	beq   3f            \n"
		"2:	ldr	  %2, [%0], #4  \n"
		"	str   %2, [%1]      \n"
		"	subs  %3, #1        \n"
		"	bne   2b            \n"
		"3:	nop                 \n"
		: "+r"(src), "+r"(dst), "+r"(loops32), "+r"(loops4) :
		: "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "cc");
}

/* Ensure the FPGA entering config done */
static int fpgamgr_program_poll_cd(void)
{
	const uint32_t mask = FPGAMGRREGS_MON_GPIO_EXT_PORTA_NS_MASK |
		FPGAMGRREGS_MON_GPIO_EXT_PORTA_CD_MASK;
	unsigned long reg, i;

	/* (3) wait until full config done */
	for (i = 0; i < FPGA_TIMEOUT_CNT; i++) {
		reg = readl(&fpgamgr_regs->gpio_ext_porta);

		/* Config error */
		if (!(reg & mask)) {
			printf("FPGA: Configuration error.\n");
			return -3;
		}

		/* Config done without error */
		if (reg & mask)
			break;
	}

	/* Timeout happened, return error */
	if (i == FPGA_TIMEOUT_CNT) {
		printf("FPGA: Timeout waiting for program.\n");
		return -4;
	}

	/* Disable AXI configuration */
	clrbits_le32(&fpgamgr_regs->ctrl, FPGAMGRREGS_CTRL_AXICFGEN_MASK);

	return 0;
}

/* Ensure the FPGA entering init phase */
static int fpgamgr_program_poll_initphase(void)
{
	unsigned long i;

	/* Additional clocks for the CB to enter initialization phase */
	if (fpgamgr_dclkcnt_set(0x4))
		return -5;

	/* (4) wait until FPGA enter init phase or user mode */
	for (i = 0; i < FPGA_TIMEOUT_CNT; i++) {
		if (fpgamgr_get_mode() == FPGAMGRREGS_MODE_INITPHASE)
			break;
		if (fpgamgr_get_mode() == FPGAMGRREGS_MODE_USERMODE)
			break;
	}

	/* If not in configuration state, return error */
	if (i == FPGA_TIMEOUT_CNT)
		return -6;

	return 0;
}

/* Ensure the FPGA entering user mode */
static int fpgamgr_program_poll_usermode(void)
{
	unsigned long i;

	/* Additional clocks for the CB to exit initialization phase */
	if (fpgamgr_dclkcnt_set(0x5000))
		return -7;

	/* (5) wait until FPGA enter user mode */
	for (i = 0; i < FPGA_TIMEOUT_CNT; i++) {
		if (fpgamgr_get_mode() == FPGAMGRREGS_MODE_USERMODE)
			break;
	}
	/* If not in configuration state, return error */
	if (i == FPGA_TIMEOUT_CNT)
		return -8;

	/* To release FPGA Manager drive over configuration line */
	clrbits_le32(&fpgamgr_regs->ctrl, FPGAMGRREGS_CTRL_EN_MASK);

	return 0;
}

/*
* FPGA Manager to program the FPGA. This is the interface used by FPGA driver.
* Return 0 for sucess, non-zero for error.
*/
static int socfpga_load(const void *rbf_data, size_t rbf_size)
{
	unsigned long status;

	if ((uint32_t)rbf_data & 0x3) {
		printf("FPGA: Unaligned data, realign to 32bit boundary.\n");
		return -EINVAL;
	}

	/* Initialize the FPGA Manager */
	status = fpgamgr_program_init();
	if (status)
		return status;

	/* Write the RBF data to FPGA Manager */
	fpgamgr_program_write(rbf_data, rbf_size);

	/* Ensure the FPGA entering config done */
	status = fpgamgr_program_poll_cd();
	if (status)
		return status;

	/* Ensure the FPGA entering init phase */
	status = fpgamgr_program_poll_initphase();
	if (status)
		return status;

	/* Ensure the FPGA entering user mode */
	return fpgamgr_program_poll_usermode();
}

static void do_bridge(uint32_t enable)
{
	if (enable)
	{
		writel(0x00003FFF, (void*)(SOCFPGA_SDR_ADDRESS + 0x5080));
		writel(0x00000000, &reset_regs->brg_mod_reset);
		writel(0x00000019, &nic301_regs->remap);
	}
	else
	{
		writel(0, &sysmgr_regs->fpgaintfgrp_module);
		writel(0, (void*)(SOCFPGA_SDR_ADDRESS + 0x5080));
		writel(7, &reset_regs->brg_mod_reset);
		writel(1, &nic301_regs->remap);
	}
}

static int make_env(const char *name, const char *cfg)
{
	if ((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) return -1;

	void* buf = mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x1FFFF000);
	if (buf == (void *)-1)
	{
		printf("Unable to mmap(/dev/mem)\n");
		close(fd);
		return -1;
	}

	volatile char* str = (volatile char*)buf;
	memset((void*)str, 0, 0xF00);

	*str++ = 0x21;
	*str++ = 0x43;
	*str++ = 0x65;
	*str++ = 0x87;
	*str++ = 'c';
	*str++ = 'o';
	*str++ = 'r';
	*str++ = 'e';
	*str++ = '=';
	*str++ = '"';

	for (uint32_t i = 0; i < strlen(name); i++)
	{
		*str++ = name[i];
	}

	*str++ = '"';
	*str++ = '\n';
	FileLoad(cfg, (void*)str, 0);
	munmap(buf, 0x1000);
	return 0;
}

int fpga_load_rbf(const char *name, const char *cfg)
{
	OsdDisable();
	static char path[1024];
	int ret = 0;

	if(cfg)
	{
		fpga_core_reset(1);
		make_env(name, cfg);
		do_bridge(0);
		reboot(0);
	}

	printf("Loading RBF: %s\n", name);

	if(name[0] == '/') strcpy(path, name);
	else sprintf(path, "%s/%s", !strcasecmp(name, "menu.rbf") ? getStorageDir(0) : getRootDir(), name);

	int rbf = open(path, O_RDONLY);
	if (rbf < 0)
	{
		printf("Couldn't open file %s\n", path);
		return -1;
	}
	else
	{
		struct stat64 st;
		if (fstat64(rbf, &st)<0)
		{
			printf("Couldn't get info of file %s\n", path);
			ret = -1;
		}
		else
		{
			printf("Bitstream size: %lld bytes\n", st.st_size);

			void *buf = malloc(st.st_size);
			if (!buf)
			{
				printf("Couldn't allocate %llu bytes.\n", st.st_size);
				ret = -1;
			}
			else
			{
				fpga_core_reset(1);
				if (read(rbf, buf, st.st_size)<st.st_size)
				{
					printf("Couldn't read file %s\n", name);
					ret = -1;
				}
				else
				{
					void *p = buf;
					__off64_t sz = st.st_size;
					if (!memcmp(buf, "MiSTer", 6))
					{
						sz = *(uint32_t*)(((uint8_t*)buf) + 12);
						p = (void*)(((uint8_t*)buf) + 16);
					}
					do_bridge(0);
					ret = socfpga_load(p, sz);
					if (ret)
					{
						printf("Error %d while loading %s\n", ret, path);
					}
					else
					{
						do_bridge(1);
					}
				}
				free(buf);
			}
		}
	}
	close(rbf);
	app_restart(!strcasecmp(name, "menu.rbf") ? "menu.rbf" : path);

	return ret;
}

int fpga_io_init()
{
	if ((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) return -1;

	map_base = (uint32_t*)mmap(0, FPGA_REG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, FPGA_REG_BASE);
	if (map_base == (void *)-1)
	{
		printf("Unable to mmap(/dev/mem)\n");
		close(fd);
		return -1;
	}
	return 0;
}

static uint32_t gpo_copy = 0;
void fpga_gpo_write(uint32_t value)
{
	gpo_copy = value;
	writel(value, (void*)(SOCFPGA_MGR_ADDRESS + 0x10));
}

uint32_t fpga_gpo_read()
{
	return gpo_copy; //readl((void*)(SOCFPGA_MGR_ADDRESS + 0x10));
}

int fpga_gpi_read()
{
	return readl((void*)(SOCFPGA_MGR_ADDRESS + 0x14));
}

void fpga_core_write(uint32_t offset, uint32_t value)
{
	if (offset <= 0x1FFFFF) writel(value, (void*)(SOCFPGA_LWFPGASLAVES_ADDRESS + (offset & ~3)));
}

uint32_t fpga_core_read(uint32_t offset)
{
	if (offset <= 0x1FFFFF) return readl((void*)(SOCFPGA_LWFPGASLAVES_ADDRESS + (offset & ~3)));
	return 0;
}

int fpga_core_id()
{
	uint32_t gpo = (fpga_gpo_read() & 0x7FFFFFFF);
	fpga_gpo_write(gpo);
	uint32_t coretype = fpga_gpi_read();
	gpo |= 0x80000000;
	fpga_gpo_write(gpo);

	if ((coretype >> 8) != 0x5CA623) return -1;
	return coretype & 0xFF;
}

int fpga_get_fio_size()
{
	return (fpga_gpi_read()>>16) & 1;
}

int fpga_get_io_version()
{
	return (fpga_gpi_read() >> 18) & 3;
}

void fpga_set_led(uint32_t on)
{
	uint32_t gpo = fpga_gpo_read();
	fpga_gpo_write(on ? gpo | 0x20000000 : gpo & ~0x20000000);
}

int fpga_get_buttons()
{
	fpga_gpo_write(fpga_gpo_read() | 0x80000000);
	int gpi = fpga_gpi_read();
	if (gpi < 0) gpi = 0; // FPGA is not in user mode. Ignore the data;
	return (gpi >> 29) & 3;
}

int fpga_get_io_type()
{
	fpga_gpo_write(fpga_gpo_read() | 0x80000000);
	return (fpga_gpi_read() >> 28) & 1;
}

void reboot(int cold)
{
	sync();
	fpga_core_reset(1);

	usleep(500000);

	writel(cold ? 0 : 0x1, &reset_regs->tstscratch);
	writel(2, &reset_regs->ctrl);
	while (1);
}

char *getappname()
{
	static char dest[PATH_MAX];
	memset(dest, 0, sizeof(dest));

	char path[64];
	sprintf(path, "/proc/%d/exe", getpid());
	readlink(path, dest, PATH_MAX);

	return dest;
}

void app_restart(const char *path)
{
	sync();
	fpga_core_reset(1);

	input_switch(0);
	input_uinp_destroy();

	char *appname = getappname();
	printf("restarting the %s\n", appname);
	execl(appname, appname, path, NULL);

	printf("Something went wrong. Rebooting...\n");
	reboot(0);
}

void fpga_core_reset(int reset)
{
	uint32_t gpo = fpga_gpo_read() & ~0xC0000000;
	fpga_gpo_write(reset ? gpo | 0x40000000 : gpo | 0x80000000);
}

int is_fpga_ready(int quick)
{
	if (quick)
	{
		return (fpga_gpi_read() >= 0);
	}

	return fpgamgr_test_fpga_ready();
}
