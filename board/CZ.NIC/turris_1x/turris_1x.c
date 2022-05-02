// SPDX-License-Identifier: GPL-2.0+
// (C) 2022 Pali Rohár <pali@kernel.org>

#include <common.h>
#include <init.h>
#include <env.h>
#include <fdt_support.h>

#include "../turris_atsha_otp.h"

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
	const char *old_model;
	const char *model;
	char serial[17];
	int len;
	int off;
	int rev;

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
	old_model = fdt_getprop(blob, 0, "model", &len);
	if (len < sizeof("Turris 1.x")-1)
		return;
	if (memcmp(old_model, "Turris 1.x", sizeof("Turris 1.x")-1) != 0)
		return;

	len = strlen(model) + 1;
	fdt_increase_size(blob, len);
	fdt_setprop(blob, 0, "model", model, len);
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

	if (reset_time > 0)
		env_set_ulong("turris_reset", reset_time);
	else
		env_set("turris_reset", NULL);

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
