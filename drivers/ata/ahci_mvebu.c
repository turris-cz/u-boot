// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Stefan Roese <sr@denx.de>
 */

#include <common.h>
#include <ahci.h>
#include <dm.h>
#include <generic-phy.h>

/*
 * Dummy implementation that can be overwritten by a board
 * specific function
 */
__weak int board_ahci_enable(void)
{
	return 0;
}

static int mvebu_ahci_bind(struct udevice *dev)
{
	struct udevice *scsi_dev;
	int ret;

	ret = ahci_bind_scsi(dev, &scsi_dev);
	if (ret) {
		debug("%s: Failed to bind (err=%d\n)", __func__, ret);
		return ret;
	}

	return 0;
}

static int mvebu_ahci_phy_power_on(struct udevice *dev)
{
	struct phy phy;
	int ret;

	ret = generic_phy_get_by_index(dev, 0, &phy);
	if (ret == -ENOENT)
		return 0;

	ret = generic_phy_init(&phy);
	if (ret)
		return ret;

	ret = generic_phy_set_mode(&phy, PHY_MODE_SATA, 0);
	if (ret)
		goto err;

	ret = generic_phy_power_on(&phy);
	if (ret)
		goto err;

	return 0;

err:
	generic_phy_exit(&phy);
	return ret;
}

static int mvebu_ahci_probe(struct udevice *dev)
{
	int ret;

	/*
	 * Board specific SATA / AHCI enable code, e.g. enable the
	 * AHCI power or deassert reset
	 */
	board_ahci_enable();

	ret = mvebu_ahci_phy_power_on(dev);
	if (ret)
		return ret;

	ahci_probe_scsi(dev, (ulong)devfdt_get_addr_ptr(dev));

	return 0;
}

static const struct udevice_id mvebu_ahci_ids[] = {
	{ .compatible = "marvell,armada-380-ahci" },
	{ .compatible = "marvell,armada-3700-ahci" },
	{ .compatible = "marvell,armada-8k-ahci" },
	{ }
};

U_BOOT_DRIVER(ahci_mvebu_drv) = {
	.name		= "ahci_mvebu",
	.id		= UCLASS_AHCI,
	.of_match	= mvebu_ahci_ids,
	.bind		= mvebu_ahci_bind,
	.probe		= mvebu_ahci_probe,
};
