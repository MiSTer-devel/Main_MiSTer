/*
* Copyright (C) 2012 Altera Corporation <www.altera.com>
* All rights reserved.
*
* SPDX-License-Identifier:    BSD-3-Clause
*/

#ifndef	_FPGA_MANAGER_H_
#define	_FPGA_MANAGER_H_

struct socfpga_fpga_manager {
	/* FPGA Manager Module */
	uint32_t	stat;			/* 0x00 */
	uint32_t	ctrl;
	uint32_t	dclkcnt;
	uint32_t	dclkstat;
	uint32_t	gpo;			/* 0x10 */
	uint32_t	gpi;
	uint32_t	misci;			/* 0x18 */
	uint32_t	_pad_0x1c_0x82c[517];

	/* Configuration Monitor (MON) Registers */
	uint32_t	gpio_inten;		/* 0x830 */
	uint32_t	gpio_intmask;
	uint32_t	gpio_inttype_level;
	uint32_t	gpio_int_polarity;
	uint32_t	gpio_intstatus;		/* 0x840 */
	uint32_t	gpio_raw_intstatus;
	uint32_t	_pad_0x848;
	uint32_t	gpio_porta_eoi;
	uint32_t	gpio_ext_porta;		/* 0x850 */
	uint32_t	_pad_0x854_0x85c[3];
	uint32_t	gpio_1s_sync;		/* 0x860 */
	uint32_t	_pad_0x864_0x868[2];
	uint32_t	gpio_ver_id_code;
	uint32_t	gpio_config_reg2;	/* 0x870 */
	uint32_t	gpio_config_reg1;
};

#define FPGAMGRREGS_STAT_MODE_MASK		0x7
#define FPGAMGRREGS_STAT_MSEL_MASK		0xf8
#define FPGAMGRREGS_STAT_MSEL_LSB		3

#define FPGAMGRREGS_CTRL_CFGWDTH_MASK		0x200
#define FPGAMGRREGS_CTRL_AXICFGEN_MASK		0x100
#define FPGAMGRREGS_CTRL_NCONFIGPULL_MASK	0x4
#define FPGAMGRREGS_CTRL_NCE_MASK		0x2
#define FPGAMGRREGS_CTRL_EN_MASK		0x1
#define FPGAMGRREGS_CTRL_CDRATIO_LSB		6

#define FPGAMGRREGS_MON_GPIO_EXT_PORTA_CRC_MASK	0x8
#define FPGAMGRREGS_MON_GPIO_EXT_PORTA_ID_MASK	0x4
#define FPGAMGRREGS_MON_GPIO_EXT_PORTA_CD_MASK	0x2
#define FPGAMGRREGS_MON_GPIO_EXT_PORTA_NS_MASK	0x1

/* FPGA Mode */
#define FPGAMGRREGS_MODE_FPGAOFF		0x0
#define FPGAMGRREGS_MODE_RESETPHASE		0x1
#define FPGAMGRREGS_MODE_CFGPHASE		0x2
#define FPGAMGRREGS_MODE_INITPHASE		0x3
#define FPGAMGRREGS_MODE_USERMODE		0x4
#define FPGAMGRREGS_MODE_UNKNOWN		0x5

/* FPGA CD Ratio Value */
#define CDRATIO_x1				0x0
#define CDRATIO_x2				0x1
#define CDRATIO_x4				0x2
#define CDRATIO_x8				0x3

#endif /* _FPGA_MANAGER_H_ */
