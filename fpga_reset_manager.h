/*
*  Copyright (C) 2012 Altera Corporation <www.altera.com>
*
* SPDX-License-Identifier:	GPL-2.0+
*/

#ifndef	_RESET_MANAGER_H_
#define	_RESET_MANAGER_H_

struct socfpga_reset_manager {
	uint32_t	status;
	uint32_t	ctrl;
	uint32_t	counts;
	uint32_t	padding1;
	uint32_t	mpu_mod_reset;
	uint32_t	per_mod_reset;
	uint32_t	per2_mod_reset;
	uint32_t	brg_mod_reset;
	uint32_t	misc_mod_reset;
	uint32_t	padding2[12];
	uint32_t	tstscratch;
};

#if defined(CONFIG_SOCFPGA_VIRTUAL_TARGET)
#define RSTMGR_CTRL_SWWARMRSTREQ_LSB 2
#else
#define RSTMGR_CTRL_SWWARMRSTREQ_LSB 1
#endif

/*
* Define a reset identifier, from which a permodrst bank ID
* and reset ID can be extracted using the subsequent macros
* RSTMGR_RESET() and RSTMGR_BANK().
*/
#define RSTMGR_BANK_OFFSET	8
#define RSTMGR_BANK_MASK	0x7
#define RSTMGR_RESET_OFFSET	0
#define RSTMGR_RESET_MASK	0x1f
#define RSTMGR_DEFINE(_bank, _offset)		\
	((_bank) << RSTMGR_BANK_OFFSET) | ((_offset) << RSTMGR_RESET_OFFSET)

/* Extract reset ID from the reset identifier. */
#define RSTMGR_RESET(_reset)			\
	(((_reset) >> RSTMGR_RESET_OFFSET) & RSTMGR_RESET_MASK)

/* Extract bank ID from the reset identifier. */
#define RSTMGR_BANK(_reset)			\
	(((_reset) >> RSTMGR_BANK_OFFSET) & RSTMGR_BANK_MASK)

/*
* SocFPGA Cyclone V/Arria V reset IDs, bank mapping is as follows:
* 0 ... mpumodrst
* 1 ... permodrst
* 2 ... per2modrst
* 3 ... brgmodrst
* 4 ... miscmodrst
*/
#define RSTMGR_EMAC0		RSTMGR_DEFINE(1, 0)
#define RSTMGR_EMAC1		RSTMGR_DEFINE(1, 1)
#define RSTMGR_NAND		RSTMGR_DEFINE(1, 4)
#define RSTMGR_QSPI		RSTMGR_DEFINE(1, 5)
#define RSTMGR_L4WD0		RSTMGR_DEFINE(1, 6)
#define RSTMGR_OSC1TIMER0	RSTMGR_DEFINE(1, 8)
#define RSTMGR_UART0		RSTMGR_DEFINE(1, 16)
#define RSTMGR_SPIM0		RSTMGR_DEFINE(1, 18)
#define RSTMGR_SPIM1		RSTMGR_DEFINE(1, 19)
#define RSTMGR_SDMMC		RSTMGR_DEFINE(1, 22)
#define RSTMGR_DMA		RSTMGR_DEFINE(1, 28)
#define RSTMGR_SDR		RSTMGR_DEFINE(1, 29)

/* Create a human-readable reference to SoCFPGA reset. */
#define SOCFPGA_RESET(_name)	RSTMGR_##_name

#endif /* _RESET_MANAGER_H_ */
