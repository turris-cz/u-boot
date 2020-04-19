/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * USB phy functions
 *
 * Moved from ehci-hcd.c by Marek Behun <marek.behun@nic.cz>
 *
 * Copyright (C) Marek Vasut <marex@denx.de>
 */

#ifndef __USB_HOST_PHY_H_
#define __USB_HOST_PHY_H_

#include <common.h>
#include <dm.h>

#ifdef CONFIG_PHY
int usb_phys_setup(struct udevice *dev);
int usb_phys_shutdown(struct udevice *dev);
#else
static inline int usb_phys_setup(struct udevice *dev)
{
	return 0;
}

static inline int usb_phys_shutdown(struct udevice *dev)
{
	return 0;
}
#endif

#endif /* __USB_HOST_PHY_H_ */
