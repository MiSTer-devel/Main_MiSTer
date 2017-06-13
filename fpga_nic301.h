/*
* Copyright (C) 2014 Marek Vasut <marex@denx.de>
*
* SPDX-License-Identifier:	GPL-2.0+
*/

#ifndef	_NIC301_REGISTERS_H_
#define	_NIC301_REGISTERS_H_

struct nic301_registers {
	uint32_t	remap;				/* 0x0 */
									/* Security Register Group */
	uint32_t	_pad_0x4_0x8[1];
	uint32_t	l4main;
	uint32_t	l4sp;
	uint32_t	l4mp;				/* 0x10 */
	uint32_t	l4osc1;
	uint32_t	l4spim;
	uint32_t	stm;
	uint32_t	lwhps2fpgaregs;			/* 0x20 */
	uint32_t	_pad_0x24_0x28[1];
	uint32_t	usb1;
	uint32_t	nanddata;
	uint32_t	_pad_0x30_0x80[20];
	uint32_t	usb0;				/* 0x80 */
	uint32_t	nandregs;
	uint32_t	qspidata;
	uint32_t	fpgamgrdata;
	uint32_t	hps2fpgaregs;			/* 0x90 */
	uint32_t	acp;
	uint32_t	rom;
	uint32_t	ocram;
	uint32_t	sdrdata;			/* 0xA0 */
	uint32_t	_pad_0xa4_0x1fd0[1995];
	/* ID Register Group */
	uint32_t	periph_id_4;			/* 0x1FD0 */
	uint32_t	_pad_0x1fd4_0x1fe0[3];
	uint32_t	periph_id_0;			/* 0x1FE0 */
	uint32_t	periph_id_1;
	uint32_t	periph_id_2;
	uint32_t	periph_id_3;
	uint32_t	comp_id_0;			/* 0x1FF0 */
	uint32_t	comp_id_1;
	uint32_t	comp_id_2;
	uint32_t	comp_id_3;
	uint32_t	_pad_0x2000_0x2008[2];
	/* L4 MAIN */
	uint32_t	l4main_fn_mod_bm_iss;
	uint32_t	_pad_0x200c_0x3008[1023];
	/* L4 SP */
	uint32_t	l4sp_fn_mod_bm_iss;
	uint32_t	_pad_0x300c_0x4008[1023];
	/* L4 MP */
	uint32_t	l4mp_fn_mod_bm_iss;
	uint32_t	_pad_0x400c_0x5008[1023];
	/* L4 OSC1 */
	uint32_t	l4osc_fn_mod_bm_iss;
	uint32_t	_pad_0x500c_0x6008[1023];
	/* L4 SPIM */
	uint32_t	l4spim_fn_mod_bm_iss;
	uint32_t	_pad_0x600c_0x7008[1023];
	/* STM */
	uint32_t	stm_fn_mod_bm_iss;
	uint32_t	_pad_0x700c_0x7108[63];
	uint32_t	stm_fn_mod;
	uint32_t	_pad_0x710c_0x8008[959];
	/* LWHPS2FPGA */
	uint32_t	lwhps2fpga_fn_mod_bm_iss;
	uint32_t	_pad_0x800c_0x8108[63];
	uint32_t	lwhps2fpga_fn_mod;
	uint32_t	_pad_0x810c_0xa008[1983];
	/* USB1 */
	uint32_t	usb1_fn_mod_bm_iss;
	uint32_t	_pad_0xa00c_0xa044[14];
	uint32_t	usb1_ahb_cntl;
	uint32_t	_pad_0xa048_0xb008[1008];
	/* NANDDATA */
	uint32_t	nanddata_fn_mod_bm_iss;
	uint32_t	_pad_0xb00c_0xb108[63];
	uint32_t	nanddata_fn_mod;
	uint32_t	_pad_0xb10c_0x20008[21439];
	/* USB0 */
	uint32_t	usb0_fn_mod_bm_iss;
	uint32_t	_pad_0x2000c_0x20044[14];
	uint32_t	usb0_ahb_cntl;
	uint32_t	_pad_0x20048_0x21008[1008];
	/* NANDREGS */
	uint32_t	nandregs_fn_mod_bm_iss;
	uint32_t	_pad_0x2100c_0x21108[63];
	uint32_t	nandregs_fn_mod;
	uint32_t	_pad_0x2110c_0x22008[959];
	/* QSPIDATA */
	uint32_t	qspidata_fn_mod_bm_iss;
	uint32_t	_pad_0x2200c_0x22044[14];
	uint32_t	qspidata_ahb_cntl;
	uint32_t	_pad_0x22048_0x23008[1008];
	/* FPGAMGRDATA */
	uint32_t	fpgamgrdata_fn_mod_bm_iss;
	uint32_t	_pad_0x2300c_0x23040[13];
	uint32_t	fpgamgrdata_wr_tidemark;	/* 0x23040 */
	uint32_t	_pad_0x23044_0x23108[49];
	uint32_t	fn_mod;
	uint32_t	_pad_0x2310c_0x24008[959];
	/* HPS2FPGA */
	uint32_t	hps2fpga_fn_mod_bm_iss;
	uint32_t	_pad_0x2400c_0x24040[13];
	uint32_t	hps2fpga_wr_tidemark;		/* 0x24040 */
	uint32_t	_pad_0x24044_0x24108[49];
	uint32_t	hps2fpga_fn_mod;
	uint32_t	_pad_0x2410c_0x25008[959];
	/* ACP */
	uint32_t	acp_fn_mod_bm_iss;
	uint32_t	_pad_0x2500c_0x25108[63];
	uint32_t	acp_fn_mod;
	uint32_t	_pad_0x2510c_0x26008[959];
	/* Boot ROM */
	uint32_t	bootrom_fn_mod_bm_iss;
	uint32_t	_pad_0x2600c_0x26108[63];
	uint32_t	bootrom_fn_mod;
	uint32_t	_pad_0x2610c_0x27008[959];
	/* On-chip RAM */
	uint32_t	ocram_fn_mod_bm_iss;
	uint32_t	_pad_0x2700c_0x27040[13];
	uint32_t	ocram_wr_tidemark;		/* 0x27040 */
	uint32_t	_pad_0x27044_0x27108[49];
	uint32_t	ocram_fn_mod;
	uint32_t	_pad_0x2710c_0x42024[27590];
	/* DAP */
	uint32_t	dap_fn_mod2;
	uint32_t	dap_fn_mod_ahb;
	uint32_t	_pad_0x4202c_0x42100[53];
	uint32_t	dap_read_qos;			/* 0x42100 */
	uint32_t	dap_write_qos;
	uint32_t	dap_fn_mod;
	uint32_t	_pad_0x4210c_0x43100[1021];
	/* MPU */
	uint32_t	mpu_read_qos;			/* 0x43100 */
	uint32_t	mpu_write_qos;
	uint32_t	mpu_fn_mod;
	uint32_t	_pad_0x4310c_0x44028[967];
	/* SDMMC */
	uint32_t	sdmmc_fn_mod_ahb;
	uint32_t	_pad_0x4402c_0x44100[53];
	uint32_t	sdmmc_read_qos;			/* 0x44100 */
	uint32_t	sdmmc_write_qos;
	uint32_t	sdmmc_fn_mod;
	uint32_t	_pad_0x4410c_0x45100[1021];
	/* DMA */
	uint32_t	dma_read_qos;			/* 0x45100 */
	uint32_t	dma_write_qos;
	uint32_t	dma_fn_mod;
	uint32_t	_pad_0x4510c_0x46040[973];
	/* FPGA2HPS */
	uint32_t	fpga2hps_wr_tidemark;		/* 0x46040 */
	uint32_t	_pad_0x46044_0x46100[47];
	uint32_t	fpga2hps_read_qos;		/* 0x46100 */
	uint32_t	fpga2hps_write_qos;
	uint32_t	fpga2hps_fn_mod;
	uint32_t	_pad_0x4610c_0x47100[1021];
	/* ETR */
	uint32_t	etr_read_qos;			/* 0x47100 */
	uint32_t	etr_write_qos;
	uint32_t	etr_fn_mod;
	uint32_t	_pad_0x4710c_0x48100[1021];
	/* EMAC0 */
	uint32_t	emac0_read_qos;			/* 0x48100 */
	uint32_t	emac0_write_qos;
	uint32_t	emac0_fn_mod;
	uint32_t	_pad_0x4810c_0x49100[1021];
	/* EMAC1 */
	uint32_t	emac1_read_qos;			/* 0x49100 */
	uint32_t	emac1_write_qos;
	uint32_t	emac1_fn_mod;
	uint32_t	_pad_0x4910c_0x4a028[967];
	/* USB0 */
	uint32_t	usb0_fn_mod_ahb;
	uint32_t	_pad_0x4a02c_0x4a100[53];
	uint32_t	usb0_read_qos;			/* 0x4A100 */
	uint32_t	usb0_write_qos;
	uint32_t	usb0_fn_mod;
	uint32_t	_pad_0x4a10c_0x4b100[1021];
	/* NAND */
	uint32_t	nand_read_qos;			/* 0x4B100 */
	uint32_t	nand_write_qos;
	uint32_t	nand_fn_mod;
	uint32_t	_pad_0x4b10c_0x4c028[967];
	/* USB1 */
	uint32_t	usb1_fn_mod_ahb;
	uint32_t	_pad_0x4c02c_0x4c100[53];
	uint32_t	usb1_read_qos;			/* 0x4C100 */
	uint32_t	usb1_write_qos;
	uint32_t	usb1_fn_mod;
};

#endif	/* _NIC301_REGISTERS_H_ */
