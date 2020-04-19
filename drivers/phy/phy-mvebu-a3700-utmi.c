// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Marvell
 *
 * Authors:
 *   Igal Liberman <igall@marvell.com>
 *   Miquèl Raynal <miquel.raynal@bootlin.com>
 *
 * Ported from Linux to U-Boot by Marek Behun <marek.behun@nic.cz>
 *
 * Marvell A3700 UTMI PHY driver
 */

#include <common.h>
#include <asm/io.h>
#include <dm.h>
#include <generic-phy.h>
#include <linux/compat.h>
#include <linux/iopoll.h>
#include <regmap.h>
#include <syscon.h>

/* Armada 3700 UTMI PHY registers */
#define USB2_PHY_PLL_CTRL_REG0			0x0
#define   PLL_REF_DIV_OFF			0
#define   PLL_REF_DIV_MASK			GENMASK(6, 0)
#define   PLL_REF_DIV_5				5
#define   PLL_FB_DIV_OFF			16
#define   PLL_FB_DIV_MASK			GENMASK(24, 16)
#define   PLL_FB_DIV_96				96
#define   PLL_SEL_LPFR_OFF			28
#define   PLL_SEL_LPFR_MASK			GENMASK(29, 28)
#define   PLL_READY				BIT(31)
#define USB2_PHY_CAL_CTRL			0x8
#define   PHY_PLLCAL_DONE			BIT(31)
#define   PHY_IMPCAL_DONE			BIT(23)
#define USB2_RX_CHAN_CTRL1			0x18
#define   USB2PHY_SQCAL_DONE			BIT(31)
#define USB2_PHY_OTG_CTRL			0x34
#define   PHY_PU_OTG				BIT(4)
#define USB2_PHY_CHRGR_DETECT			0x38
#define   PHY_CDP_EN				BIT(2)
#define   PHY_DCP_EN				BIT(3)
#define   PHY_PD_EN				BIT(4)
#define   PHY_PU_CHRG_DTC			BIT(5)
#define   PHY_CDP_DM_AUTO			BIT(7)
#define   PHY_ENSWITCH_DP			BIT(12)
#define   PHY_ENSWITCH_DM			BIT(13)

/* Armada 3700 USB miscellaneous registers */
#define USB2_PHY_CTRL(usb32)			(usb32 ? 0x20 : 0x4)
#define   RB_USB2PHY_PU				BIT(0)
#define   USB2_DP_PULLDN_DEV_MODE		BIT(5)
#define   USB2_DM_PULLDN_DEV_MODE		BIT(6)
#define   RB_USB2PHY_SUSPM(usb32)		(usb32 ? BIT(14) : BIT(7))

#define PLL_LOCK_DELAY_US			10000
#define PLL_LOCK_TIMEOUT_US			1000000

/**
 * struct mvebu_a3700_utmi - PHY driver data
 *
 * @regs: PHY registers
 * @usb_mis: Regmap with USB miscellaneous registers including PHY ones
 * @usb32: Flag indicating which PHY is in use (impacts the register map):
 *           - The UTMI PHY wired to the USB3/USB2 controller (otg)
 *           - The UTMI PHY wired to the USB2 controller (host only)
 * @phy: PHY handle
 */
struct mvebu_a3700_utmi {
	void __iomem *regs;
	struct regmap *usb_misc;
	int usb32;
	struct phy *phy;
};

static int mvebu_a3700_utmi_phy_power_on(struct phy *phy)
{
	struct udevice *dev = phy->dev;
	struct mvebu_a3700_utmi *utmi = dev_get_priv(dev);
	int usb32 = utmi->usb32;
	int ret = 0;
	u32 reg;

	/*
	 * Setup PLL. 40MHz clock used to be the default, being 25MHz now.
	 * See "PLL Settings for Typical REFCLK" table.
	 */
	reg = readl(utmi->regs + USB2_PHY_PLL_CTRL_REG0);
	reg &= ~(PLL_REF_DIV_MASK | PLL_FB_DIV_MASK | PLL_SEL_LPFR_MASK);
	reg |= (PLL_REF_DIV_5 << PLL_REF_DIV_OFF) |
	       (PLL_FB_DIV_96 << PLL_FB_DIV_OFF);
	writel(reg, utmi->regs + USB2_PHY_PLL_CTRL_REG0);

	/* Enable PHY pull up and disable USB2 suspend */
	regmap_update_bits(utmi->usb_misc, USB2_PHY_CTRL(usb32),
			   RB_USB2PHY_SUSPM(usb32) | RB_USB2PHY_PU,
			   RB_USB2PHY_SUSPM(usb32) | RB_USB2PHY_PU);

	if (usb32) {
		/* Power up OTG module */
		reg = readl(utmi->regs + USB2_PHY_OTG_CTRL);
		reg |= PHY_PU_OTG;
		writel(reg, utmi->regs + USB2_PHY_OTG_CTRL);

		/* Disable PHY charger detection */
		reg = readl(utmi->regs + USB2_PHY_CHRGR_DETECT);
		reg &= ~(PHY_CDP_EN | PHY_DCP_EN | PHY_PD_EN | PHY_PU_CHRG_DTC |
			 PHY_CDP_DM_AUTO | PHY_ENSWITCH_DP | PHY_ENSWITCH_DM);
		writel(reg, utmi->regs + USB2_PHY_CHRGR_DETECT);

		/* Disable PHY DP/DM pull-down (used for device mode) */
		regmap_update_bits(utmi->usb_misc, USB2_PHY_CTRL(usb32),
				   USB2_DP_PULLDN_DEV_MODE |
				   USB2_DM_PULLDN_DEV_MODE, 0);
	}

	/* Wait for PLL calibration */
	ret = readl_poll_timeout(utmi->regs + USB2_PHY_CAL_CTRL, reg,
				 reg & PHY_PLLCAL_DONE,
				 /*PLL_LOCK_DELAY_US, */PLL_LOCK_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "Failed to end USB2 PLL calibration\n");
		return ret;
	}

	/* Wait for impedance calibration */
	ret = readl_poll_timeout(utmi->regs + USB2_PHY_CAL_CTRL, reg,
				 reg & PHY_IMPCAL_DONE,
				 /*PLL_LOCK_DELAY_US, */PLL_LOCK_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "Failed to end USB2 impedance calibration\n");
		return ret;
	}

	/* Wait for squelch calibration */
	ret = readl_poll_timeout(utmi->regs + USB2_RX_CHAN_CTRL1, reg,
				 reg & USB2PHY_SQCAL_DONE,
				 /*PLL_LOCK_DELAY_US, */PLL_LOCK_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "Failed to end USB2 unknown calibration\n");
		return ret;
	}

	/* Wait for PLL to be locked */
	ret = readl_poll_timeout(utmi->regs + USB2_PHY_PLL_CTRL_REG0, reg,
				 reg & PLL_READY,
				 /*PLL_LOCK_DELAY_US, */PLL_LOCK_TIMEOUT_US);
	if (ret)
		dev_err(dev, "Failed to lock USB2 PLL\n");

	return ret;
}

static int _mvebu_a3700_utmi_phy_power_off(struct mvebu_a3700_utmi *utmi)
{
	int usb32 = utmi->usb32;
	u32 reg;

	/* Disable PHY pull-up and enable USB2 suspend */
	reg = readl(utmi->regs + USB2_PHY_CTRL(usb32));
	reg &= ~(RB_USB2PHY_PU | RB_USB2PHY_SUSPM(usb32));
	writel(reg, utmi->regs + USB2_PHY_CTRL(usb32));

	/* Power down OTG module */
	if (usb32) {
		reg = readl(utmi->regs + USB2_PHY_OTG_CTRL);
		reg &= ~PHY_PU_OTG;
		writel(reg, utmi->regs + USB2_PHY_OTG_CTRL);
	}

	return 0;
}

static int mvebu_a3700_utmi_phy_power_off(struct phy *phy)
{
	struct mvebu_a3700_utmi *utmi = dev_get_priv(phy->dev);

	return _mvebu_a3700_utmi_phy_power_off(utmi);
}

static const struct phy_ops mvebu_a3700_utmi_phy_ops = {
	.power_on = mvebu_a3700_utmi_phy_power_on,
	.power_off = mvebu_a3700_utmi_phy_power_off,
};

static const struct udevice_id mvebu_a3700_utmi_of_match[] = {
	{
		.compatible = "marvell,a3700-utmi-otg-phy",
		.data = true, /* usb32 */
	},
	{
		.compatible = "marvell,a3700-utmi-host-phy",
		.data = false, /* usb32 */
	},
	{},
};

static int mvebu_a3700_utmi_phy_probe(struct udevice *dev)
{
	struct mvebu_a3700_utmi *utmi = dev_get_priv(dev);

	/* Get UTMI memory region */
	utmi->regs = dev_remap_addr_index(dev, 0);
	if (!utmi->regs) {
		dev_err(dev, "no UTMI IO address\n");
		return -ENODEV;
	}

	/* Get miscellaneous Host/PHY region */
	utmi->usb_misc = syscon_regmap_lookup_by_phandle(dev,
							 "marvell,usb-misc-reg");
	if (IS_ERR(utmi->usb_misc)) {
		dev_err(dev,
			"Missing USB misc purpose system controller\n");
		return PTR_ERR(utmi->usb_misc);
	}

	/* Retrieve usb32 parameter */
	utmi->usb32 = dev_get_driver_data(dev);

	/* Ensure the PHY is powered off */
	_mvebu_a3700_utmi_phy_power_off(utmi);

	return 0;
}

U_BOOT_DRIVER(mvebu_a3700_utmi_phy) = {
	.name = "mvebu-a3700-utmi-phy",
	.id = UCLASS_PHY,
	.of_match = mvebu_a3700_utmi_of_match,
	.ops = &mvebu_a3700_utmi_phy_ops,
	.priv_auto_alloc_size = sizeof(struct mvebu_a3700_utmi),
	.probe = mvebu_a3700_utmi_phy_probe,
};
