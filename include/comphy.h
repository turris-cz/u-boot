/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2015-2016 Marvell International Ltd.
 */

#ifndef _COMPHY_H_
#define _COMPHY_H_

#include <dt-bindings/comphy/comphy_data.h>

struct comphy_map {
	u32 type;
	u32 speed;
	u32 invert;
	bool clk_src;
	bool end_point;
};

void board_update_comphy_map(struct comphy_map *serdes_map, int count);

#endif /* _COMPHY_H_ */

