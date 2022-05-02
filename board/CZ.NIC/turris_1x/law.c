// SPDX-License-Identifier: GPL-2.0+
// (C) 2022 Pali Rohár <pali@kernel.org>

#include <common.h>
#include <asm/fsl_law.h>

struct law_entry law_table[] = {
	SET_LAW(CONFIG_SYS_FLASH_BASE_PHYS, LAW_SIZE_16M, LAW_TRGT_IF_LBC),
	SET_LAW(CONFIG_SYS_NAND_BASE_PHYS, LAW_SIZE_256K, LAW_TRGT_IF_LBC),
	SET_LAW(CONFIG_SYS_CPLD_BASE_PHYS, LAW_SIZE_128K, LAW_TRGT_IF_LBC),
};

int num_law_entries = ARRAY_SIZE(law_table);
