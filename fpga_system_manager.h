/*
* Copyright (C) 2013 Altera Corporation <www.altera.com>
*
* SPDX-License-Identifier:	GPL-2.0+
*/

#ifndef	_SYSTEM_MANAGER_H_
#define	_SYSTEM_MANAGER_H_

struct socfpga_system_manager {
	/* System Manager Module */
	uint32_t	siliconid1;			/* 0x00 */
	uint32_t	siliconid2;
	uint32_t	_pad_0x8_0xf[2];
	uint32_t	wddbg;				/* 0x10 */
	uint32_t	bootinfo;
	uint32_t	hpsinfo;
	uint32_t	parityinj;
	/* FPGA Interface Group */
	uint32_t	fpgaintfgrp_gbl;		/* 0x20 */
	uint32_t	fpgaintfgrp_indiv;
	uint32_t	fpgaintfgrp_module;
	uint32_t	_pad_0x2c_0x2f;
	/* Scan Manager Group */
	uint32_t	scanmgrgrp_ctrl;		/* 0x30 */
	uint32_t	_pad_0x34_0x3f[3];
	/* Freeze Control Group */
	uint32_t	frzctrl_vioctrl;		/* 0x40 */
	uint32_t	_pad_0x44_0x4f[3];
	uint32_t	frzctrl_hioctrl;		/* 0x50 */
	uint32_t	frzctrl_src;
	uint32_t	frzctrl_hwctrl;
	uint32_t	_pad_0x5c_0x5f;
	/* EMAC Group */
	uint32_t	emacgrp_ctrl;			/* 0x60 */
	uint32_t	emacgrp_l3master;
	uint32_t	_pad_0x68_0x6f[2];
	/* DMA Controller Group */
	uint32_t	dmagrp_ctrl;			/* 0x70 */
	uint32_t	dmagrp_persecurity;
	uint32_t	_pad_0x78_0x7f[2];
	/* Preloader (initial software) Group */
	uint32_t	iswgrp_handoff[8];		/* 0x80 */
	uint32_t	_pad_0xa0_0xbf[8];		/* 0xa0 */
										/* Boot ROM Code Register Group */
	uint32_t	romcodegrp_ctrl;		/* 0xc0 */
	uint32_t	romcodegrp_cpu1startaddr;
	uint32_t	romcodegrp_initswstate;
	uint32_t	romcodegrp_initswlastld;
	uint32_t	romcodegrp_bootromswstate;	/* 0xd0 */
	uint32_t	__pad_0xd4_0xdf[3];
	/* Warm Boot from On-Chip RAM Group */
	uint32_t	romcodegrp_warmramgrp_enable;	/* 0xe0 */
	uint32_t	romcodegrp_warmramgrp_datastart;
	uint32_t	romcodegrp_warmramgrp_length;
	uint32_t	romcodegrp_warmramgrp_execution;
	uint32_t	romcodegrp_warmramgrp_crc;	/* 0xf0 */
	uint32_t	__pad_0xf4_0xff[3];
	/* Boot ROM Hardware Register Group */
	uint32_t	romhwgrp_ctrl;			/* 0x100 */
	uint32_t	_pad_0x104_0x107;
	/* SDMMC Controller Group */
	uint32_t	sdmmcgrp_ctrl;
	uint32_t	sdmmcgrp_l3master;
	/* NAND Flash Controller Register Group */
	uint32_t	nandgrp_bootstrap;		/* 0x110 */
	uint32_t	nandgrp_l3master;
	/* USB Controller Group */
	uint32_t	usbgrp_l3master;
	uint32_t	_pad_0x11c_0x13f[9];
	/* ECC Management Register Group */
	uint32_t	eccgrp_l2;			/* 0x140 */
	uint32_t	eccgrp_ocram;
	uint32_t	eccgrp_usb0;
	uint32_t	eccgrp_usb1;
	uint32_t	eccgrp_emac0;			/* 0x150 */
	uint32_t	eccgrp_emac1;
	uint32_t	eccgrp_dma;
	uint32_t	eccgrp_can0;
	uint32_t	eccgrp_can1;			/* 0x160 */
	uint32_t	eccgrp_nand;
	uint32_t	eccgrp_qspi;
	uint32_t	eccgrp_sdmmc;
	uint32_t	_pad_0x170_0x3ff[164];
	/* Pin Mux Control Group */
	uint32_t	emacio[20];			/* 0x400 */
	uint32_t	flashio[12];			/* 0x450 */
	uint32_t	generalio[28];			/* 0x480 */
	uint32_t	_pad_0x4f0_0x4ff[4];
	uint32_t	mixed1io[22];			/* 0x500 */
	uint32_t	mixed2io[8];			/* 0x558 */
	uint32_t	gplinmux[23];			/* 0x578 */
	uint32_t	gplmux[71];			/* 0x5d4 */
	uint32_t	nandusefpga;			/* 0x6f0 */
	uint32_t	_pad_0x6f4;
	uint32_t	rgmii1usefpga;			/* 0x6f8 */
	uint32_t	_pad_0x6fc_0x700[2];
	uint32_t	i2c0usefpga;			/* 0x704 */
	uint32_t	sdmmcusefpga;			/* 0x708 */
	uint32_t	_pad_0x70c_0x710[2];
	uint32_t	rgmii0usefpga;			/* 0x714 */
	uint32_t	_pad_0x718_0x720[3];
	uint32_t	i2c3usefpga;			/* 0x724 */
	uint32_t	i2c2usefpga;			/* 0x728 */
	uint32_t	i2c1usefpga;			/* 0x72c */
	uint32_t	spim1usefpga;			/* 0x730 */
	uint32_t	_pad_0x734;
	uint32_t	spim0usefpga;			/* 0x738 */
};

#define SYSMGR_ROMCODEGRP_CTRL_WARMRSTCFGPINMUX	(1 << 0)
#define SYSMGR_ROMCODEGRP_CTRL_WARMRSTCFGIO	(1 << 1)
#define SYSMGR_ECC_OCRAM_EN	(1 << 0)
#define SYSMGR_ECC_OCRAM_SERR	(1 << 3)
#define SYSMGR_ECC_OCRAM_DERR	(1 << 4)
#define SYSMGR_FPGAINTF_USEFPGA	0x1
#define SYSMGR_FPGAINTF_SPIM0	(1 << 0)
#define SYSMGR_FPGAINTF_SPIM1	(1 << 1)
#define SYSMGR_FPGAINTF_EMAC0	(1 << 2)
#define SYSMGR_FPGAINTF_EMAC1	(1 << 3)
#define SYSMGR_FPGAINTF_NAND	(1 << 4)
#define SYSMGR_FPGAINTF_SDMMC	(1 << 5)

#if defined(CONFIG_TARGET_SOCFPGA_GEN5)
#define SYSMGR_SDMMC_SMPLSEL_SHIFT	3
#else
#define SYSMGR_SDMMC_SMPLSEL_SHIFT	4
#endif

#define SYSMGR_SDMMC_DRVSEL_SHIFT	0

/* EMAC Group Bit definitions */
#define SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_GMII_MII	0x0
#define SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RGMII		0x1
#define SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RMII		0x2

#define SYSMGR_EMACGRP_CTRL_PHYSEL0_LSB			0
#define SYSMGR_EMACGRP_CTRL_PHYSEL1_LSB			2
#define SYSMGR_EMACGRP_CTRL_PHYSEL_MASK			0x3

#endif /* _SYSTEM_MANAGER_H_ */
