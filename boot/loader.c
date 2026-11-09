// SPDX-License-Identifier: GPL-2.0-or-later
#include <blk.h>
#include <command.h>
#include <env.h>
#include <dm/read.h>
#include <linux/libfdt.h>
#include <mmc.h>
#include <part.h>
#include <time.h>

#include "minpocket-layout.h"
#include "minpocket-startup-cache.c"

static ulong sd_ms;
static bool fit_header_read;

static int minpocket_handoff(struct cmd_tbl *cmd, int flags, int argc,
                            char *const argv[])
{
	const char *args = env_get("bootargs");
	char command_line[512];
	ulong ready_ms = get_timer(0);
	int length = snprintf(command_line, sizeof command_line,
			      "%s minpocket.ready_ms=%lu minpocket.sd_ms=%lu"
			      " minpocket.fit_reader=%u minpocket.startup_handoff=1"
			      " minpocket.startup_bytes=%lu minpocket.startup_ms=%lu",
			      args ? args : "", ready_ms, sd_ms, fit_header_read ? 1 : 0, startup_bytes, startup_ms);

	if (length < 0 || length >= (int)sizeof command_line ||
	    env_set("bootargs", command_line))
		return CMD_RET_FAILURE;
	return run_command("bootm 0x18000000#conf", 0) ?
		CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

U_BOOT_CMD(mp1handoff, 1, 0, minpocket_handoff, "start the verified MP1 FIT", "");

static int minpocket_boot(struct cmd_tbl *cmd, int flags, int argc,
                         char *const argv[])
{
	ulong start = get_timer(0);
	struct mmc *card = find_mmc_device(0);
	struct blk_desc *disk;
	fit_header_read = false;

	if (!card || mmc_init(card))
		return CMD_RET_FAILURE;

	disk = mmc_get_blk_desc(card);
	if (!disk || disk->blksz != 512 || !IS_SD(card) ||
	    dev_read_addr(card->dev) != 0xfe2b0000 || disk->lba <= PAYLOAD_LBA)
		return CMD_RET_FAILURE;

	struct disk_partition partition;
	if (part_get_info(disk, 2, &partition) || partition.start != PAYLOAD_LBA ||
	    partition.blksz != disk->blksz || !partition.size ||
	    partition.size > PAYLOAD_LIMIT_BLOCKS ||
	    partition.size > disk->lba - PAYLOAD_LBA ||
	    strcmp((const char *)partition.name, "minpocket"))
		return CMD_RET_FAILURE;

	void *fit = (void *)PAYLOAD_ADDRESS;
	lbaint_t blocks;
	u32 size;
	int images, image;

	/* Read one aligned sector first. Never derive a disk read or memory extent
	 * from the untrusted total-size field until the header and limits pass. */
	if (blk_dread(disk, PAYLOAD_LBA, 1, fit) != 1 || fdt_check_header(fit))
		return CMD_RET_FAILURE;
	size = fdt_totalsize(fit);
	if (size < sizeof(struct fdt_header) || partition.size <= MP_HOME_RESERVED_BLOCKS ||
	    size > (partition.size - MP_HOME_RESERVED_BLOCKS) * disk->blksz)
		return CMD_RET_FAILURE;
	blocks = DIV_ROUND_UP(size, disk->blksz);
	if (blocks > 1 &&
	    blk_dread(disk, PAYLOAD_LBA + 1, blocks - 1,
		      (char *)fit + disk->blksz) != blocks - 1)
		return CMD_RET_FAILURE;
	if (fdt_check_full(fit, size))
		return CMD_RET_FAILURE;

	/* MinPocket packs inline FIT data. External-data FITs need a separate size
	 * contract and must not silently boot after reading only their FDT header. */
	images = fdt_path_offset(fit, "/images");
	if (images < 0 || fdt_first_subnode(fit, images) < 0)
		return CMD_RET_FAILURE;
	fdt_for_each_subnode(image, fit, images) {
		int length;
		if (!fdt_getprop(fit, image, "data", &length) || length <= 0 ||
		    fdt_getprop(fit, image, "data-position", NULL) ||
		    fdt_getprop(fit, image, "data-offset", NULL))
			return CMD_RET_FAILURE;
	}
	if (image != -FDT_ERR_NOTFOUND)
		return CMD_RET_FAILURE;

	startup_load(disk, &partition);
	sd_ms = get_timer(start);
	fit_header_read = true;
	return minpocket_handoff(cmd, flags, argc, argv);
}

U_BOOT_CMD(minpocket, 1, 0, minpocket_boot, "boot the MP1 SD payload", "");
