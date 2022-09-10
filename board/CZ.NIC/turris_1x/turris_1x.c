// SPDX-License-Identifier: GPL-2.0+
// (C) 2022 Pali Rohár <pali@kernel.org>

#include <common.h>
#include <init.h>
#include <env.h>
#include <fdt_support.h>
#include <image.h>
#include <asm/fsl_law.h>
#include <asm/global_data.h>
#include <asm/mmu.h>

#include "../turris_atsha_otp.h"

DECLARE_GLOBAL_DATA_PTR;

/*
 * Reset time cycle register provided by Turris CPLD firmware.
 * Turris CPLD firmware is open source and available at:
 * https://gitlab.nic.cz/turris/hw/turris_cpld/-/blob/master/CZ_NIC_Router_CPLD.v
 */
#define TURRIS_CPLD_RESET_TIME_CYCLE_REG	((void *)CONFIG_SYS_CPLD_BASE + 0x1f)
#define  TURRIS_CPLD_RESET_TIME_CYCLE_300MS	BIT(0)
#define  TURRIS_CPLD_RESET_TIME_CYCLE_1S	BIT(1)
#define  TURRIS_CPLD_RESET_TIME_CYCLE_2S	BIT(2)
#define  TURRIS_CPLD_RESET_TIME_CYCLE_3S	BIT(3)
#define  TURRIS_CPLD_RESET_TIME_CYCLE_4S	BIT(4)
#define  TURRIS_CPLD_RESET_TIME_CYCLE_5S	BIT(5)
#define  TURRIS_CPLD_RESET_TIME_CYCLE_6S	BIT(6)

#define TURRIS_CPLD_LED_BRIGHTNESS_REG_FIRST	((void *)CONFIG_SYS_CPLD_BASE + 0x13)
#define TURRIS_CPLD_LED_BRIGHTNESS_REG_LAST	((void *)CONFIG_SYS_CPLD_BASE + 0x1e)
#define TURRIS_CPLD_LED_SW_OVERRIDE_REG		((void *)CONFIG_SYS_CPLD_BASE + 0x22)

int dram_init_banksize(void)
{
	phys_size_t size = gd->ram_size;

	static_assert(CONFIG_NR_DRAM_BANKS >= 3);

	gd->bd->bi_dram[0].start = gd->ram_base;
	gd->bd->bi_dram[0].size = get_effective_memsize();
	size -= gd->bd->bi_dram[0].size;

	/* Note: This address space is not mapped via TLB entries in U-Boot */

	if (size > 0) {
		/*
		 * Setup additional overlapping 1 GB DDR LAW at the end of
		 * 32-bit physical address space. It overlaps with all other
		 * peripherals on P2020 mapped to physical address space.
		 * But this is not issue because documentation says:
		 * P2020 QorIQ Integrated Processor Reference Manual,
		 * section 2.3.1 Precedence of local access windows:
		 * If two local access windows overlap, the lower
		 * numbered window takes precedence.
		 */
		if (set_ddr_laws(0xc0000000, SZ_1G, LAW_TRGT_IF_DDR_1) < 0) {
			printf("Error: Cannot setup DDR LAW for more than 2 GB\n");
			return 0;
		}
	}

	if (size > 0) {
		/* Free space between PCIe bus 3 MEM and NOR */
		gd->bd->bi_dram[1].start = 0xc0200000;
		gd->bd->bi_dram[1].size = min(size, 0xef000000 - gd->bd->bi_dram[1].start);
		size -= gd->bd->bi_dram[1].size;
	}

	if (size > 0) {
		/* Free space between NOR and NAND */
		gd->bd->bi_dram[2].start = 0xf0000000;
		gd->bd->bi_dram[2].size = min(size, 0xff800000 - gd->bd->bi_dram[2].start);
		size -= gd->bd->bi_dram[2].size;
	}

	return 0;
}

static inline int fdt_setprop_inplace_u32_partial(void *blob, int node,
						  const char *name,
						  u32 idx, u32 val)
{
	val = cpu_to_fdt32(val);

	return fdt_setprop_inplace_namelen_partial(blob, node, name,
						   strlen(name),
						   idx * sizeof(u32),
						   &val, sizeof(u32));
}

/* Decrease size of 3rd PCIe controller MEM in "ranges" DT to 2MB recursively */
static void fdt_fixup_pcie3_mem_size(void *blob, int node)
{
	int pci_cells, cpu_cells, size_cells;
	const u32 *ranges;
	int pnode;
	int i, len;
	u32 pci_flags;
	u64 cpu_addr;
	u64 size;
	int idx;
	int subnode;
	int ret;

	if (!fdtdec_get_is_enabled(blob, node))
		return;

	ranges = fdt_getprop(blob, node, "ranges", &len);
	if (!ranges || !len || len % sizeof(u32))
		return;

	/*
	 * The "ranges" property is an array of
	 *   { <PCI address> <CPU address> <size in PCI address space> }
	 * where number of PCI address cells and size cells is stored in the
	 * "#address-cells" and "#size-cells" properties of the same node
	 * containing the "ranges" property and number of CPU address cells
	 * is stored in the parent's "#address-cells" property.
	 *
	 * All 3 elements can span a different number of cells. Fetch them.
	 */
	pnode = fdt_parent_offset(blob, node);
	pci_cells = fdt_address_cells(blob, node);
	cpu_cells = fdt_address_cells(blob, pnode);
	size_cells = fdt_size_cells(blob, node);

	/* PCI addresses always use 3 cells */
	if (pci_cells != 3)
		return;

	/* CPU addresses and sizes on P2020 may be 32-bit (1 cell) or 64-bit (2 cells) */
	if (cpu_cells != 1 && cpu_cells != 2)
		return;
	if (size_cells != 1 && size_cells != 2)
		return;

	for (i = 0; i < len / sizeof(u32); i += pci_cells + cpu_cells + size_cells) {
		/* PCI address consists of 3 cells: flags, addr.hi, addr.lo */
		pci_flags = fdt32_to_cpu(ranges[i]);

		cpu_addr = fdt32_to_cpu(ranges[i + pci_cells]);
		if (cpu_cells == 2) {
			cpu_addr <<= 32;
			cpu_addr |= fdt32_to_cpu(ranges[i + pci_cells + 1]);
		}

		size = fdt32_to_cpu(ranges[i + pci_cells + cpu_cells]);
		if (size_cells == 2) {
			size <<= 32;
			size |= fdt32_to_cpu(ranges[i + pci_cells + cpu_cells + 1]);
		}

		/*
		 * Bits [25:24] of PCI flags defines space code
		 * 0b10 is 32-bit MEM and 0b11 is 64-bit MEM.
		 * Check for any type of PCIe MEM mapping.
		 */
		if (!((pci_flags & 0x02000000) &&
		      cpu_addr == CONFIG_SYS_PCIE3_MEM_PHYS &&
		      size > SZ_2M))
			continue;

		printf("Decreasing PCIe MEM size for 3rd PCIe controller to 2 MB\n");
		idx = i + pci_cells + cpu_cells;
		if (size_cells == 2) {
			ret = fdt_setprop_inplace_u32_partial(blob, node,
							      "ranges", idx, 0);
			if (ret)
				goto err;
			idx++;
		}
		ret = fdt_setprop_inplace_u32_partial(blob, node,
						      "ranges", idx, SZ_2M);
		if (ret)
			goto err;
	}

	/* Recursively fix also all subnodes */
	fdt_for_each_subnode(subnode, blob, node)
		fdt_fixup_pcie3_mem_size(blob, subnode);

	return;

err:
	printf("Error: Cannot update \"ranges\" property\n");
}

void ft_memory_setup(void *blob, struct bd_info *bd)
{
	u64 start[CONFIG_NR_DRAM_BANKS];
	u64 size[CONFIG_NR_DRAM_BANKS];
	int count;
	int node;

	if (!env_get("bootm_low") && !env_get("bootm_size")) {
		for (count = 0; count < CONFIG_NR_DRAM_BANKS; count++) {
			start[count] = gd->bd->bi_dram[count].start;
			size[count] = gd->bd->bi_dram[count].size;
			if (!size[count])
				break;
		}
		fdt_fixup_memory_banks(blob, start, size, count);
	} else {
		fdt_fixup_memory(blob, env_get_bootm_low(), env_get_bootm_size());
	}

	fdt_for_each_node_by_compatible(node, blob, -1, "fsl,mpc8548-pcie")
		fdt_fixup_pcie3_mem_size(blob, node);
}

static int detect_model_serial(const char **model, char serial[17])
{
	u32 version_num;
	int err;

	err = turris_atsha_otp_get_serial_number(serial);
	if (err) {
		*model = "Turris 1.x";
		strcpy(serial, "unknown");
		return -1;
	}

	version_num = simple_strtoull(serial, NULL, 16) >> 32;

	/*
	 * Turris 1.0 boards (RTRS01) have version_num 0x5.
	 * Turris 1.1 boards (RTRS02) have version_num 0x6, 0x7, 0x8 and 0x9.
	 */
	if (be32_to_cpu(version_num) >= 0x6) {
		*model = "Turris 1.1 (RTRS02)";
		return 1;
	} else {
		*model = "Turris 1.0 (RTRS01)";
		return 0;
	}
}

void fix_fdt_model(void *blob)
{
	const char *model;
	char serial[17];
	int len;
	int off;
	int rev;
	char c;

	rev = detect_model_serial(&model, serial);
	if (rev < 0)
		return;

	/* Turris 1.0 boards (RTRS01) do not have third PCIe controller */
	if (rev == 0) {
		off = fdt_path_offset(blob, "pci2");
		if (off >= 0)
			fdt_del_node(blob, off);
	}

	/* Fix model string only in case it is generic "Turris 1.x" */
	model = fdt_getprop(blob, 0, "model", &len);
	if (len < sizeof("Turris 1.x")-1)
		return;
	if (memcmp(model, "Turris 1.x", sizeof("Turris 1.x")-1) != 0)
		return;

	c = '0' + rev;
	fdt_setprop_inplace_namelen_partial(blob, 0, "model", sizeof("model")-1,
					    sizeof("Turris 1.")-1, &c, 1);
}

int misc_init_r(void)
{
	turris_atsha_otp_init_mac_addresses(0);
	turris_atsha_otp_init_serial_number();
	return 0;
}

int show_board_info(void)
{
	const char *model;
	char serial[17];
	void *reg;
	int err;

	/* Disable software control of all Turris LEDs */
	out_8(TURRIS_CPLD_LED_SW_OVERRIDE_REG, 0x00);

	/* Reset colors of all Turris LEDs to their default values */
	for (reg = TURRIS_CPLD_LED_BRIGHTNESS_REG_FIRST;
	     reg <= TURRIS_CPLD_LED_BRIGHTNESS_REG_LAST;
	     reg++)
		out_8(reg, 0xff);

	detect_model_serial(&model, serial);
	printf("Model: %s\n", model);
	printf("Serial Number: %s\n", serial);

	err = checkboard();
	if (err)
		return err;

	return 0;
}

static void handle_reset_button(void)
{
	const char * const vars[1] = { "bootcmd_rescue", };
	u8 reset_time_raw, reset_time;

	/*
	 * Ensure that bootcmd_rescue has always stock value, so that running
	 *   run bootcmd_rescue
	 * always works correctly.
	 */
	env_set_default_vars(1, (char * const *)vars, 0);

	reset_time_raw = in_8(TURRIS_CPLD_RESET_TIME_CYCLE_REG);
	if (reset_time_raw & TURRIS_CPLD_RESET_TIME_CYCLE_6S)
		reset_time = 6;
	else if (reset_time_raw & TURRIS_CPLD_RESET_TIME_CYCLE_5S)
		reset_time = 5;
	else if (reset_time_raw & TURRIS_CPLD_RESET_TIME_CYCLE_4S)
		reset_time = 4;
	else if (reset_time_raw & TURRIS_CPLD_RESET_TIME_CYCLE_3S)
		reset_time = 3;
	else if (reset_time_raw & TURRIS_CPLD_RESET_TIME_CYCLE_2S)
		reset_time = 2;
	else if (reset_time_raw & TURRIS_CPLD_RESET_TIME_CYCLE_1S)
		reset_time = 1;
	else
		reset_time = 0;

	env_set_ulong("turris_reset", reset_time);

	/* Check if red reset button was hold for at least six seconds. */
	if (reset_time >= 6) {
		const char * const vars[3] = {
			"bootcmd",
			"bootdelay",
			"distro_bootcmd",
		};

		/*
		 * Set the above envs to their default values, in case the user
		 * managed to break them.
		 */
		env_set_default_vars(3, (char * const *)vars, 0);

		/* Ensure bootcmd_rescue is used by distroboot */
		env_set("boot_targets", "rescue");

		printf("RESET button was hold for >= 6s, overwriting boot_targets for system rescue!\n");
	} else {
		/*
		 * In case the user somehow managed to save environment with
		 * boot_targets=rescue, reset boot_targets to default value.
		 * This could happen in subsequent commands if bootcmd_rescue
		 * failed.
		 */
		if (!strcmp(env_get("boot_targets"), "rescue")) {
			const char * const vars[1] = {
				"boot_targets",
			};

			env_set_default_vars(1, (char * const *)vars, 0);
		}

		if (reset_time > 0)
			printf("RESET button was hold for %us.\n", reset_time);
	}
}

int last_stage_init(void)
{
	handle_reset_button();
	return 0;
}
