// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Marek Behun <marek.behun@nic.cz>
 */

#include <common.h>
#include <dm.h>
#include <clk.h>
#include <spi.h>
#include <linux/string.h>
#include <comphy.h>

#ifdef CONFIG_WDT_ARMADA_3720
#include <wdt.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

#ifdef CONFIG_WDT_ARMADA_3720
static struct udevice *watchdog_dev;

void watchdog_reset(void)
{
	static ulong next_reset;
	ulong now;

	if (!watchdog_dev)
		return;

	now = timer_get_us();

	/* Do not reset the watchdog too often */
	if (now > next_reset) {
		wdt_reset(watchdog_dev);
		next_reset = now + 100000;
	}
}
#endif

int board_init(void)
{
	/* address of boot parameters */
	gd->bd->bi_boot_params = CONFIG_SYS_SDRAM_BASE + 0x100;

#ifdef CONFIG_WDT_ARMADA_3720
	if (uclass_get_device(UCLASS_WDT, 0, &watchdog_dev)) {
		printf("Cannot find Armada 3720 watchdog!\n");
	} else {
		printf("Enabling Armada 3720 watchdog (3 minutes timeout).\n");
		wdt_start(watchdog_dev, 180000, 0);
	}
#endif

	return 0;
}

static int mox_read_topology(const u8 **ptopology, int *psize)
{
	static u8 topology[9];
	static int size = 0;
	struct spi_slave *slave;
	struct udevice *dev;
	u8 din[10], dout[10];
	int ret, i;

	if (size) {
		*ptopology = topology;
		*psize = size;
		return 0;
	}

	ret = spi_get_bus_and_cs(0, 1, 20000000, SPI_CPHA, "spi_generic_drv",
				 "mox-modules@1", &dev, &slave);
	if (ret)
		goto fail;

	ret = spi_claim_bus(slave);
	if (ret)
		goto fail_free;

	memset(din, 0, 10);
	memset(dout, 0, 10);

	ret = spi_xfer(slave, 80, dout, din, SPI_XFER_ONCE);
	if (ret)
		goto fail_release;

	if (din[0] != 0x00 && din[0] != 0xff) {
		ret = -ENODEV;
		goto fail_release;
	}

	for (i = 1; i < 10 && din[i] != 0xff; ++i)
		topology[i-1] = din[i] & 0xf;
	size = i-1;

	*ptopology = topology;
	*psize = size;

fail_release:
	spi_release_bus(slave);
fail_free:
	spi_free_slave(slave);
fail:
	return ret;
}

void board_update_comphy_map(struct comphy_map *serdes_map, int count)
{
	int ret, i, size, has = 0;
	const u8 *topology;

	ret = mox_read_topology(&topology, &size);
	if (ret)
		return;

	for (i = 0; i < size; ++i) {
		if (has && (topology[i] == 0x1 || topology[i] == 0x3)) {
			printf("Warning: two or more incompatible Mox modules "
			       "found, using only first!\n");
			break;
		} else if (topology[i] == 0x1) {
			printf("SFP module found, changing SERDES lane 0 speed"
			       " to 1.25 Gbps\n");
			serdes_map[0].speed = PHY_SPEED_1_25G;
			has = topology[i];
		} else if (topology[i] == 0x3) {
			printf("Topaz Switch module found, changing SERDES lane"
			       " 0 speed to 3.125 Gbps\n");
			serdes_map[0].speed = PHY_SPEED_3_125G;
			has = topology[i];
		}
	}
}

int last_stage_init(void)
{
	int ret, i, size;
	size_t len = 0;
	const u8 *topology;
	char module_topology[128];

	ret = mox_read_topology(&topology, &size);
	if (ret) {
		printf("Cannot read module topology!\n");
		return 0;
	}

	printf("Module Topology:\n");
	for (i = 0; i < size; ++i) {
		size_t mlen;
		const char *mname = "";

		switch (topology[i]) {
		case 0x1:
			mname = "sfp-";
			printf("% 4i: SFP Module\n", i+1);
			break;
		case 0x2:
			mname = "pci-";
			printf("% 4i: Mini-PCIe Module\n", i+1);
			break;
		case 0x3:
			mname = "topaz-";
			printf("% 4i: Topaz Switch Module\n", i+1);
			break;
		default:
			printf("% 4i: unknown (ID %i)\n", i+1, topology[i]);
		}

		mlen = strlen(mname);
		if (len + mlen < sizeof(module_topology)) {
			strcpy(module_topology + len, mname);
			len += mlen;
		}
	}
	printf("\n");

	module_topology[len > 0 ? len - 1 : 0] = '\0';

	env_set("module_topology", module_topology);

	return 0;
}
