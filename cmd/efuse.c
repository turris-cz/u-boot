// SPDX-License-Identifier: GPL-2.0+
/*
 */

#include <common.h>
#include <command.h>
#include <dm.h>
#include <errno.h>

#include <asm/io.h>
#include <asm/arch/cpu.h>
#include <asm/arch/soc.h>

/*-----------------------------------------------------------------------
 * Definitions
 */

#define RWTM_CMD_PARAM0 0xD00B0000
#define RWTM_CMD_PARAM1 0xD00B0004
#define RWTM_CMD_PARAM2 0xD00B0008
#define RWTM_CMD 0xD00B0040
#define RWTM_RET_STATUS 0xD00B0080
#define RWTM_CMD_STATUS0 0xD00B0084
#define RWTM_CMD_STATUS1 0xD00B0088
#define RWTM_CMD_STATUS2 0xD00B008C
#define EFUSE_MAX_ROW 43

enum mbox_opsize {
        MB_OPSZ_BIT     = 1,    /* single bit */
        MB_OPSZ_BYTE    = 2,    /* single byte */
        MB_OPSZ_WORD    = 3,    /* 4 bytes - half row */
        MB_OPSZ_DWORD   = 4,    /* 8 bytes - one row */
        MB_OPSZ_256B    = 5,    /* 32 bytes - 4 rows */
        MB_OPSZ_MAX
};

enum mbox_op {
        MB_OP_READ      = 1,
        MB_OP_WRITE     = 2,
        MB_OP_LOCK     = 3,
        MB_OP_MAX
};

enum mbox_status {
        MB_STAT_SUCCESS                 = 0,
        MB_STAT_HW_ERROR                = 1,
        MB_STAT_TIMEOUT                 = 2,
        MB_STAT_BAD_ARGUMENT            = 3,
        MB_STAT_BAD_COMMAND             = 4,

        MB_STAT_MAX
};

static int otp_read(void) {
	printf("DEBUG %s\n", __func__);
	for (u32 row=0; row<=EFUSE_MAX_ROW; row++) {
		writel(row, RWTM_CMD_PARAM0);
		u32 cmd = ((MB_OP_READ << 8) | MB_OPSZ_DWORD);
		writel(cmd, RWTM_CMD);
		udelay(100000);
		u32 stat = readl(RWTM_RET_STATUS);
		u32 l = readl(RWTM_CMD_STATUS0);
		u32 h = readl(RWTM_CMD_STATUS1);
		u32 sfb = readl(RWTM_CMD_STATUS2);
		printf("row %d status %d val 0x%08x%08x sfb %d\n", row, stat, l, h, sfb);
		udelay(100000);
	}
}

static int otp_write_row(u32 row, u32 l, u32 h) {
	printf("DEBUG %s\n", __func__);
	printf("attempting write row %d l 0x%08x h 0x%08x\n", row, l, h);
	writel(row, RWTM_CMD_PARAM0);
	writel(l, RWTM_CMD_PARAM1);
	writel(h, RWTM_CMD_PARAM2);
	u32 cmd = ((MB_OP_WRITE << 8) | MB_OPSZ_DWORD);
	writel(cmd, RWTM_CMD);
	printf("write finish\n");
	udelay(100000);
	u32 stat = readl(RWTM_RET_STATUS);
	printf("row write %d status %d\n", row, stat);
	udelay(100000);
}

static int otp_lock(u32 row) {
	printf("DEBUG %s\n", __func__);
	printf("attempting lock row %d\n", row);
	writel(row, RWTM_CMD_PARAM0);
	u32 cmd = ((MB_OP_LOCK << 8) | MB_OPSZ_DWORD);
	writel(cmd, RWTM_CMD);
	printf("lock finish\n");
	udelay(100000);
	u32 stat = readl(RWTM_RET_STATUS);
	printf("lock row %d status %d\n", row, stat);
	udelay(1000000);
}

int do_efuse (cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	u32 row, l, h;

	printf("DEBUG %s\n", __func__);

	if ((flag & CMD_FLAG_REPEAT) == 0) {
		if (argc == 2) {
			if (argv[1] && (strcmp(argv[1], "r")==0)) {
				otp_read();
				return 0;
			}
		} else if (argc == 3) {
			if (argv[1] && (strcmp(argv[1], "l")==0)) {
				row = simple_strtoul(argv[2], NULL, 10);
				otp_lock(row);
				return 0;
			}
		} else if (argc == 5) {
			if (argv[1] && (strcmp(argv[1], "w")==0)) {
				row = simple_strtoul(argv[2], NULL, 10);
				l = simple_strtoul(argv[3], NULL, 16);
				h = simple_strtoul(argv[4], NULL, 16);
				otp_write_row(row, l, h);
				return 0;
			}
		}
		return 1;
	}
	return 0;
}

/***************************************************/

U_BOOT_CMD(
	efuse,	5,	1,	do_efuse,
	"Marvel Armada 37xx EFUSE utility command",
	"<cmd> [<row> <ldata> <hdata>] - send command with data\n"
	"<cmd>          - r,w,l (read, write, lock)\n"
);

