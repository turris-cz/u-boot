// SPDX-License-Identifier: GPL-2.0
/*
 * USB phy functions
 *
 * Based on code from ehci-hcd.c by Marek Vasut <marex@denx.de>
 *
 * Copyright (C) Marek Behun <marek.behun@nic.cz>
 */

#include <common.h>
#include <dm.h>
#include <generic-phy.h>
#include <linux/err.h>

static int usb_phy_setup(struct udevice *dev, int index)
{
	struct phy phy;
	int ret;

	ret = generic_phy_get_by_index(dev, index, &phy);
	if (ret && ret != -ENOENT) {
		dev_err(dev, "failed to get usb phy %i\n", index);
		return ret;
	}

	ret = generic_phy_init(&phy);
	if (ret) {
		dev_err(dev, "failed to init usb phy %i\n", index);
		return ret;
	}

	ret = generic_phy_set_mode(&phy, PHY_MODE_USB_HOST_SS, 0);
	if (ret) {
		ret = generic_phy_set_mode(&phy, PHY_MODE_USB_HOST, 0);
		if (ret) {
			dev_err(dev, "failed to set mode on usb phy %i\n",
				index);
			goto err;
		}
	}

	ret = generic_phy_power_on(&phy);
	if (ret) {
		dev_err(dev, "failed to power on usb phy %i\n", index);
		goto err;
	}

	return 0;

err:
	generic_phy_exit(&phy);

	return ret;
}

static int usb_phy_shutdown(struct udevice *dev, int index)
{
	struct phy phy;
	int ret;

	ret = generic_phy_get_by_index(dev, index, &phy);
	if (ret && ret != -ENOENT) {
		dev_err(dev, "failed to get usb phy %i\n", index);
		return ret;
	}

	ret = generic_phy_power_off(&phy);
	if (ret) {
		dev_err(dev, "failed to power off usb phy %i\n", index);
		return ret;
	}

	ret = generic_phy_exit(&phy);
	if (ret) {
		dev_err(dev, "failed to exit usb phy %i\n", index);
		return ret;
	}

	return 0;
}

int usb_phys_setup(struct udevice *dev)
{
	int ret, index, count;

	count = dev_count_phandle_with_args(dev, "phys", "#phy-cells");

	for (index = 0; index < count; ++index) {
		ret = usb_phy_setup(dev, index);
		if (ret)
			goto err;
	}

	return 0;
err:
	for (--index; index >= 0; --index)
		usb_phy_shutdown(dev, index);

	return ret;
}

int usb_phys_shutdown(struct udevice *dev)
{
	int ret, index, count;

	count = dev_count_phandle_with_args(dev, "phys", "#phy-cells");

	for (index = 0; index < count; ++index) {
		ret = usb_phy_shutdown(dev, index);
		if (ret)
			return ret;
	}

	return 0;
}
