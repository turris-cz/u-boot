// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Marek Behun <marek.behun@nic.cz>
 */

#include <stdarg.h>
#include <common.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <dm.h>
#include <clk.h>
#include <env.h>
#include <spi.h>
#include <mvebu/comphy.h>
#include <miiphy.h>
#include <linux/string.h>
#include <linux/libfdt.h>
#include <fdt_support.h>

#include "mox_sp.h"

#define MAX_MOX_MODULES		10

#define MOX_MODULE_SFP		0x1
#define MOX_MODULE_PCI		0x2
#define MOX_MODULE_TOPAZ	0x3
#define MOX_MODULE_PERIDOT	0x4
#define MOX_MODULE_USB3		0x5
#define MOX_MODULE_PASSPCI	0x6

#define ARMADA_37XX_NB_GPIO_SEL	0xd0013830
#define ARMADA_37XX_SPI_CTRL	0xd0010600
#define ARMADA_37XX_SPI_CFG	0xd0010604
#define ARMADA_37XX_SPI_DOUT	0xd0010608
#define ARMADA_37XX_SPI_DIN	0xd001060c

#define ETH1_PATH	"/soc/internal-regs@d0000000/ethernet@40000"
#define MDIO_PATH	"/soc/internal-regs@d0000000/mdio@32004"
#define SFP_GPIO_PATH	"/soc/internal-regs@d0000000/spi@10600/moxtet@1/gpio@0"
#define PCIE_PATH	"/soc/pcie@d0070000"
#define SFP_PATH	"/sfp"

DECLARE_GLOBAL_DATA_PTR;

static long mox_ram_size(void)
{
	switch ((readl(0xd0000200) >> 16) & 0x1f) {
	case 0xd:
		return 0x20000000;
	case 0xe:
		return 0x40000000;
	case 0xf:
	case 0x10:
		return 0x80000000;
	default:
		return 0x20000000;
	}
}

int dram_init(void)
{
	gd->ram_base = 0;
	gd->ram_size = (phys_size_t)get_ram_size(0, mox_ram_size());

	return 0;
}

int dram_init_banksize(void)
{
	gd->bd->bi_dram[0].start = (phys_addr_t)0;
	gd->bd->bi_dram[0].size = gd->ram_size;

	return 0;
}

int board_init(void)
{
	/* address of boot parameters */
	gd->bd->bi_boot_params = CONFIG_SYS_SDRAM_BASE + 0x100;

	return 0;
}
