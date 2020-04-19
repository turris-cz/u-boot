// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Marvell
 *
 * Authors:
 *   Evan Wang <xswang@marvell.com>
 *   Miquèl Raynal <miquel.raynal@bootlin.com>
 *
 * Structure inspired from phy-mvebu-cp110-comphy.c written by Antoine Tenart.
 * SMC call initial support done by Grzegorz Jaszczyk.
 *
 * Ported from Linux to U-Boot by Marek Behun <marek.behun@nic.cz>
 */

#include <common.h>
#include <asm/system.h>
#include <dm.h>
#include <generic-phy.h>
#include <linux/bug.h>
#include <linux/compat.h>
#include <phy_interface.h>

#define MVEBU_A3700_COMPHY_LANES		3
#define MVEBU_A3700_COMPHY_PORTS		2

/* COMPHY Fast SMC function identifiers */
#define COMPHY_SIP_POWER_ON			0x82000001
#define COMPHY_SIP_POWER_OFF			0x82000002
#define COMPHY_SIP_PLL_LOCK			0x82000003
#define COMPHY_FW_NOT_SUPPORTED			(-1)

#define COMPHY_FW_MODE_SATA			0x1
#define COMPHY_FW_MODE_SGMII			0x2
#define COMPHY_FW_MODE_HS_SGMII			0x3
#define COMPHY_FW_MODE_USB3H			0x4
#define COMPHY_FW_MODE_USB3D			0x5
#define COMPHY_FW_MODE_PCIE			0x6
#define COMPHY_FW_MODE_RXAUI			0x7
#define COMPHY_FW_MODE_XFI			0x8
#define COMPHY_FW_MODE_SFI			0x9
#define COMPHY_FW_MODE_USB3			0xa

#define COMPHY_FW_SPEED_1_25G			0 /* SGMII 1G */
#define COMPHY_FW_SPEED_2_5G			1
#define COMPHY_FW_SPEED_3_125G			2 /* SGMII 2.5G */
#define COMPHY_FW_SPEED_5G			3
#define COMPHY_FW_SPEED_5_15625G		4 /* XFI 5G */
#define COMPHY_FW_SPEED_6G			5
#define COMPHY_FW_SPEED_10_3125G		6 /* XFI 10G */
#define COMPHY_FW_SPEED_MAX			0x3F

#define COMPHY_FW_MODE(mode)			((mode) << 12)
#define COMPHY_FW_NET(mode, idx, speed)		(COMPHY_FW_MODE(mode) | \
						 ((idx) << 8) |	\
						 ((speed) << 2))
#define COMPHY_FW_PCIE(mode, idx, speed)	(COMPHY_FW_NET(mode, idx, speed))

struct mvebu_a3700_comphy_conf {
	unsigned int lane;
	enum phy_mode mode;
	int submode;
	unsigned int port;
	u32 fw_mode;
};

#define MVEBU_A3700_COMPHY_CONF(_lane, _mode, _smode, _port, _fw)	\
	{								\
		.lane = _lane,						\
		.mode = _mode,						\
		.submode = _smode,					\
		.port = _port,						\
		.fw_mode = _fw,						\
	}

#define MVEBU_A3700_COMPHY_CONF_GEN(_lane, _mode, _port, _fw) \
	MVEBU_A3700_COMPHY_CONF(_lane, _mode, PHY_INTERFACE_MODE_NONE, _port, _fw)

#define MVEBU_A3700_COMPHY_CONF_ETH(_lane, _smode, _port, _fw) \
	MVEBU_A3700_COMPHY_CONF(_lane, PHY_MODE_ETHERNET, _smode, _port, _fw)

static const struct mvebu_a3700_comphy_conf mvebu_a3700_comphy_modes[] = {
	/* lane 0 */
	MVEBU_A3700_COMPHY_CONF_GEN(0, PHY_MODE_USB_HOST_SS, 0,
				    COMPHY_FW_MODE_USB3H),
	MVEBU_A3700_COMPHY_CONF_ETH(0, PHY_INTERFACE_MODE_SGMII, 1,
				    COMPHY_FW_MODE_SGMII),
	MVEBU_A3700_COMPHY_CONF_ETH(0, PHY_INTERFACE_MODE_SGMII_2500, 1,
				    COMPHY_FW_MODE_HS_SGMII),
	/* lane 1 */
	MVEBU_A3700_COMPHY_CONF_GEN(1, PHY_MODE_PCIE, 0,
				    COMPHY_FW_MODE_PCIE),
	MVEBU_A3700_COMPHY_CONF_ETH(1, PHY_INTERFACE_MODE_SGMII, 0,
				    COMPHY_FW_MODE_SGMII),
	MVEBU_A3700_COMPHY_CONF_ETH(1, PHY_INTERFACE_MODE_SGMII_2500, 0,
				    COMPHY_FW_MODE_HS_SGMII),
	/* lane 2 */
	MVEBU_A3700_COMPHY_CONF_GEN(2, PHY_MODE_SATA, 0,
				    COMPHY_FW_MODE_SATA),
	MVEBU_A3700_COMPHY_CONF_GEN(2, PHY_MODE_USB_HOST_SS, 0,
				    COMPHY_FW_MODE_USB3H),
};

struct mvebu_a3700_comphy_lane {
	unsigned int id;
	enum phy_mode mode;
	int submode;
	int port;
};

struct mvebu_a3700_comphy {
	struct mvebu_a3700_comphy_lane lanes[MVEBU_A3700_COMPHY_LANES];
};

static int mvebu_a3700_comphy_smc(unsigned long function, unsigned long lane,
				  unsigned long mode)
{
	struct pt_regs regs;

	regs.regs[0] = function;
	regs.regs[1] = lane;
	regs.regs[2] = mode;

	smc_call(&regs);

	return regs.regs[0];
}

static int mvebu_a3700_comphy_get_fw_mode(int lane, int port,
					  enum phy_mode mode,
					  int submode)
{
	int i, n = ARRAY_SIZE(mvebu_a3700_comphy_modes);

	/* Unused PHY mux value is 0x0 */
	if (mode == PHY_MODE_INVALID)
		return -EINVAL;

	for (i = 0; i < n; i++) {
		if (mvebu_a3700_comphy_modes[i].lane == lane &&
		    mvebu_a3700_comphy_modes[i].port == port &&
		    mvebu_a3700_comphy_modes[i].mode == mode &&
		    (mode != PHY_MODE_ETHERNET ||
		     mvebu_a3700_comphy_modes[i].submode == submode))
			break;
	}

	if (i == n)
		return -EINVAL;

	return mvebu_a3700_comphy_modes[i].fw_mode;
}

static inline struct mvebu_a3700_comphy_lane *phy_to_lane(struct phy *phy)
{
	struct mvebu_a3700_comphy *priv = dev_get_priv(phy->dev);

	return &priv->lanes[phy->id];
}

static int mvebu_a3700_comphy_set_mode(struct phy *phy, enum phy_mode mode,
				       int submode)
{
	struct mvebu_a3700_comphy_lane *lane = phy_to_lane(phy);
	int fw_mode;

	fw_mode = mvebu_a3700_comphy_get_fw_mode(lane->id, lane->port, mode,
						 submode);
	if (fw_mode < 0) {
		dev_err(lane->dev, "invalid COMPHY mode\n");
		return fw_mode;
	}

	/* Just remember the mode, ->power_on() will do the real setup */
	lane->mode = mode;
	lane->submode = submode;

	return 0;
}

static int mvebu_a3700_comphy_power_on(struct phy *phy)
{
	struct mvebu_a3700_comphy_lane *lane = phy_to_lane(phy);
	u32 fw_param;
	int fw_mode;
	int ret;

	fw_mode = mvebu_a3700_comphy_get_fw_mode(lane->id, lane->port,
						 lane->mode, lane->submode);
	if (fw_mode < 0) {
		dev_err(lane->dev, "invalid COMPHY mode\n");
		return fw_mode;
	}

	switch (lane->mode) {
	case PHY_MODE_USB_HOST_SS:
		dev_dbg(lane->dev, "set lane %d to USB3 host mode\n", lane->id);
		fw_param = COMPHY_FW_MODE(fw_mode);
		break;
	case PHY_MODE_SATA:
		dev_dbg(lane->dev, "set lane %d to SATA mode\n", lane->id);
		fw_param = COMPHY_FW_MODE(fw_mode);
		break;
	case PHY_MODE_ETHERNET:
		switch (lane->submode) {
		case PHY_INTERFACE_MODE_SGMII:
			dev_dbg(lane->dev, "set lane %d to SGMII mode\n",
				lane->id);
			fw_param = COMPHY_FW_NET(fw_mode, lane->port,
						 COMPHY_FW_SPEED_1_25G);
			break;
		case PHY_INTERFACE_MODE_SGMII_2500:
			dev_dbg(lane->dev, "set lane %d to HS SGMII mode\n",
				lane->id);
			fw_param = COMPHY_FW_NET(fw_mode, lane->port,
						 COMPHY_FW_SPEED_3_125G);
			break;
		default:
			dev_err(lane->dev, "unsupported PHY submode (%d)\n",
				lane->submode);
			return -ENOTSUPP;
		}
		break;
	case PHY_MODE_PCIE:
		dev_dbg(lane->dev, "set lane %d to PCIe mode\n", lane->id);
		fw_param = COMPHY_FW_PCIE(fw_mode, lane->port,
					  COMPHY_FW_SPEED_5G);
		break;
	default:
		dev_err(lane->dev, "unsupported PHY mode (%d)\n", lane->mode);
		return -ENOTSUPP;
	}

	ret = mvebu_a3700_comphy_smc(COMPHY_SIP_POWER_ON, lane->id, fw_param);
	if (ret == COMPHY_FW_NOT_SUPPORTED)
		dev_err(lane->dev,
			"unsupported SMC call, try updating your firmware\n");

	return ret;
}

static int mvebu_a3700_comphy_power_off(struct phy *phy)
{
	struct mvebu_a3700_comphy_lane *lane = phy_to_lane(phy);

	return mvebu_a3700_comphy_smc(COMPHY_SIP_POWER_OFF, lane->id, 0);
}

static int mvebu_a3700_comphy_xlate(struct phy *phy,
				    struct ofnode_phandle_args *args)
{
	struct mvebu_a3700_comphy_lane *lane;

	if (WARN_ON(args->args_count > 2 ||
		    args->args[0] >= MVEBU_A3700_COMPHY_LANES ||
		    args->args[1] >= MVEBU_A3700_COMPHY_PORTS))
		return -EINVAL;

	phy->id = args->args[0];
	lane = phy_to_lane(phy);
	lane->port = args->args[1];

	return 0;
}

static const struct phy_ops mvebu_a3700_comphy_ops = {
	.power_on	= mvebu_a3700_comphy_power_on,
	.power_off	= mvebu_a3700_comphy_power_off,
	.set_mode	= mvebu_a3700_comphy_set_mode,
	.of_xlate	= mvebu_a3700_comphy_xlate,
};

static int mvebu_a3700_comphy_probe(struct udevice *dev)
{
	struct mvebu_a3700_comphy *priv = dev_get_priv(dev);
	ofnode child;

	dev_for_each_subnode(child, dev) {
		struct mvebu_a3700_comphy_lane *lane;
		u32 lane_id;
		int ret;

		ret = ofnode_read_u32(child, "reg", &lane_id);
		if (ret < 0) {
			dev_err(parent, "missing 'reg' property (%d)\n", ret);
			continue;
		}

		if (lane_id >= MVEBU_A3700_COMPHY_LANES) {
			dev_err(parent, "invalid 'reg' property\n");
			continue;
		}

		lane = &priv->lanes[lane_id];
		lane->mode = PHY_MODE_INVALID;
		lane->submode = PHY_INTERFACE_MODE_NONE;
		lane->id = lane_id;
		lane->port = -1;
	}

	return 0;
}

static const struct udevice_id mvebu_a3700_comphy_of_match_table[] = {
	{ .compatible = "marvell,comphy-a3700" },
	{ },
};

U_BOOT_DRIVER(mvebu_a3700_comphy) = {
	.name = "mvebu-a3700-comphy",
	.id = UCLASS_PHY,
	.of_match = mvebu_a3700_comphy_of_match_table,
	.ops = &mvebu_a3700_comphy_ops,
	.probe = mvebu_a3700_comphy_probe,
	.priv_auto_alloc_size = sizeof(struct mvebu_a3700_comphy),
};
