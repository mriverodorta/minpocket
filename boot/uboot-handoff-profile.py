"""Measure the SD read and the FIT boot path with the ARM counter."""

import os
from pathlib import Path
import sys

root = Path(sys.argv[1])


def patch(path, edits):
    file = root / path
    source = file.read_text()
    for old, new in edits:
        if source.count(old) != 1:
            raise SystemExit(f"Expected one timing anchor in {path}: {old}")
        source = source.replace(old, new, 1)
    file.write_text(source)


patch("arch/arm/include/asm/global_data.h", [
    ("struct arch_global_data {", """enum minpocket_handoff_step {
	MINPOCKET_SD_INIT,
	MINPOCKET_SD_READ,
	MINPOCKET_READY_TO_HANDOFF,
	MINPOCKET_FIT_FIND,
	MINPOCKET_FIT_VERIFY,
	MINPOCKET_KERNEL_DECOMP,
	MINPOCKET_LINUX_PREP,
	MINPOCKET_HANDOFF_STEPS,
};

struct arch_global_data {
	u64 minpocket_ready_ticks;
	u64 minpocket_handoff_ticks[MINPOCKET_HANDOFF_STEPS];
	u32 minpocket_sd_clock;
	u32 minpocket_sd_width;
	u32 minpocket_sd_bytes;
	const char *minpocket_sd_mode;"""),
])

patch("cmd/minpocket.c", [
    ("#include <mmc.h>", "#include <mmc.h>\n#include <minpocket-profile.h>"),
    ("	if (blk_dread", """	memset(gd->arch.minpocket_handoff_ticks, 0,
	       sizeof(gd->arch.minpocket_handoff_ticks));
	u64 read_start = minpocket_ticks();

	gd->arch.minpocket_handoff_ticks[MINPOCKET_SD_INIT] =
		read_start - gd->arch.minpocket_sd_start_ticks;
	if (blk_dread"""),
    ("	sd_ms = get_timer(start);", """	gd->arch.minpocket_ready_ticks = minpocket_ticks();
	gd->arch.minpocket_handoff_ticks[MINPOCKET_SD_READ] =
		gd->arch.minpocket_ready_ticks - read_start;
	gd->arch.minpocket_sd_clock = card->clock;
	gd->arch.minpocket_sd_width = card->bus_width;
	gd->arch.minpocket_sd_bytes = blocks * disk->blksz;
	gd->arch.minpocket_sd_mode = IS_SD(card) && card->selected_mode == MMC_LEGACY ?
		"SD default speed" : mmc_mode_name(card->selected_mode);
	sd_ms = get_timer(start);"""),
])

if os.environ.get("MP1_PROFILE_LINUX") == "1":
    patch("include/mmc.h", [
        ("const char *mmc_mode_name(enum bus_mode mode);",
         "const char *mmc_mode_name(enum bus_mode mode);\nu32 minpocket_sd_clock_hz(struct mmc *mmc);"),
    ])
    patch("drivers/mmc/rockchip_dw_mmc.c", [
        ("U_BOOT_DRIVER(rockchip_rk3288_dw_mshc) = {", """u32 minpocket_sd_clock_hz(struct mmc *mmc)
{
	struct rockchip_dwmmc_priv *priv;
	ulong source_hz;
	u32 divisor;

	if (mmc->dev->driver != DM_DRIVER_GET(rockchip_rk3288_dw_mshc))
		return 0;
	priv = dev_get_priv(mmc->dev);
	source_hz = clk_get_rate(&priv->clk);
	if (IS_ERR_VALUE(source_hz))
		return 0;

	/* CLKSRC selects divider 0. A zero divisor bypasses the divider. */
	divisor = dwmci_readl(&priv->host, DWMCI_CLKDIV) & 0xff;
	return divisor ? source_hz / (2 * divisor) : source_hz;
}

U_BOOT_DRIVER(rockchip_rk3288_dw_mshc) = {"""),
    ])
    if os.environ.get("MP1_TEST_UBOOT_SD_50") == "1":
        patch("drivers/mmc/rockchip_dw_mmc.c", [
            ("	ret = clk_set_rate(&priv->clk, freq);", """	/*
	 * The RK3568 SD clock generator divides the source by two
	 * before the DesignWare divider. Try 50 MHz at the card
	 * only once SD High Speed has been selected on the MP1 slot.
	 */
	if (dev_read_addr(dev) == 0xfe2b0000 &&
	    host->mmc->selected_mode == SD_HS && freq == 50000000) {
		ret = clk_set_rate(&priv->clk, 100000000);
		if (ret >= 0)
			return freq;
	}

	ret = clk_set_rate(&priv->clk, freq);"""),
            ("	/* CLKSRC selects divider 0.", """	/* Account for the fixed divider on the MP1 SD controller. */
	if (dev_read_addr(mmc->dev) == 0xfe2b0000)
		source_hz /= 2;

	/* CLKSRC selects divider 0."""),
        ])
        print("MinPocket U-Boot SD experiment: request 100 MHz source in SD High Speed")
    patch("cmd/minpocket.c", [
        ("gd->arch.minpocket_sd_clock = card->clock;",
         "gd->arch.minpocket_sd_clock = minpocket_sd_clock_hz(card);"),
    ])

if os.environ.get("MP1_TEST_SHA256_CE") == "1":
    software = """void minpocket_sha256_software(sha256_context *ctx, const unsigned char *data,
                               unsigned int blocks)"""
    patch("lib/sha256.c", [
        ("__weak void sha256_process(sha256_context *ctx, const unsigned char *data,",
         software + ";\n\n" + software + "\n{\n"
         "\twhile (blocks--) {\n\t\tsha256_process_one(ctx, data);\n\t\tdata += 64;\n\t}\n}\n\n"
         "__weak void sha256_process(sha256_context *ctx, const unsigned char *data,"),
        ("""\tif (!blocks)
\t\treturn;

\twhile (blocks--) {
\t\tsha256_process_one(ctx, data);
\t\tdata += 64;
\t}
}""", "\tminpocket_sha256_software(ctx, data, blocks);\n}"),
    ])
    patch("arch/arm/cpu/armv8/sha256_ce_glue.c", [
        ("void sha256_process(", software + ";\n\nvoid sha256_process("),
        ("\tsha256_armv8_ce_process(ctx->state, data, blocks);", """\tunsigned long features;

\tasm volatile("mrs %0, id_aa64isar0_el1" : "=r"(features));
\t/* ID_AA64ISAR0_EL1.SHA2 is nonzero when SHA-256 is available. */
\tif ((features >> 12) & 0xf)
\t\tsha256_armv8_ce_process(ctx->state, data, blocks);
\telse
\t\tminpocket_sha256_software(ctx, data, blocks);"""),
    ])
    print("MinPocket handoff experiment: ARM SHA-256 with software fallback")

patch("boot/bootm.c", [
    ("#include <asm/global_data.h>", "#include <asm/global_data.h>\n#include <minpocket-profile.h>"),
    ("	if (!ret && (states & BOOTM_STATE_FINDOS))", """	u64 find_start = minpocket_ticks();

	if (!ret && (states & BOOTM_STATE_FINDOS))"""),
    ("	if (IS_ENABLED(CONFIG_MEASURED_BOOT) && !ret &&", """	gd->arch.minpocket_handoff_ticks[MINPOCKET_FIT_FIND] = minpocket_ticks() - find_start;

	if (IS_ENABLED(CONFIG_MEASURED_BOOT) && !ret &&"""),
    ("	err = image_decomp(os.comp", """	u64 decomp_start = minpocket_ticks();

	err = image_decomp(os.comp"""),
    ("			   decomp_len, &load_end);", """			   decomp_len, &load_end);
	gd->arch.minpocket_handoff_ticks[MINPOCKET_KERNEL_DECOMP] =
		minpocket_ticks() - decomp_start;"""),
    ("	if (!ret && (states & BOOTM_STATE_OS_PREP)) {\n		int flags = 0;", """	if (!ret && (states & BOOTM_STATE_OS_PREP)) {
		u64 prep_start = minpocket_ticks();
		int flags = 0;"""),
    ("		ret = boot_fn(BOOTM_STATE_OS_PREP, bmi);", """		ret = boot_fn(BOOTM_STATE_OS_PREP, bmi);
		gd->arch.minpocket_handoff_ticks[MINPOCKET_LINUX_PREP] =
			minpocket_ticks() - prep_start;"""),
])

patch("boot/image-fit.c", [
    ("#include <asm/global_data.h>", "#include <asm/global_data.h>\n#include <minpocket-profile.h>"),
    ("""	return fit_image_verify_with_data(fit, image_noffset, gd_fdt_blob(),
					  data, size);""", """#ifndef USE_HOSTCC
	u64 verify_start = minpocket_ticks();
#endif
	int verified = fit_image_verify_with_data(fit, image_noffset, gd_fdt_blob(),
						  data, size);
#ifndef USE_HOSTCC
	gd->arch.minpocket_handoff_ticks[MINPOCKET_FIT_VERIFY] += minpocket_ticks() - verify_start;
#endif
	return verified;"""),
])

patch("arch/arm/lib/bootm.c", [
    ('"minpocket,boot-counter", samples, sizeof(samples));', '''"minpocket,boot-counter", samples, sizeof(samples));
			if (rate && gd->arch.minpocket_ready_ticks) {
				fdt32_t profile[MINPOCKET_HANDOFF_STEPS];
				struct {
					fdt32_t clock_hz;
					fdt32_t width;
					fdt32_t bytes;
					char mode[32];
				} link = {
					.clock_hz = cpu_to_fdt32(gd->arch.minpocket_sd_clock),
					.width = cpu_to_fdt32(gd->arch.minpocket_sd_width),
					.bytes = cpu_to_fdt32(gd->arch.minpocket_sd_bytes),
				};

				gd->arch.minpocket_handoff_ticks[MINPOCKET_READY_TO_HANDOFF] =
					ticks - gd->arch.minpocket_ready_ticks;
				for (int i = 0; i < ARRAY_SIZE(profile); i++)
					profile[i] = cpu_to_fdt32(gd->arch.minpocket_handoff_ticks[i] *
								  1000000 / rate);
				fdt_setprop_inplace(images->ft_addr, chosen,
						    "minpocket,handoff-profile", profile, sizeof(profile));
				if (gd->arch.minpocket_sd_mode)
					strlcpy(link.mode, gd->arch.minpocket_sd_mode, sizeof(link.mode));
				fdt_setprop_inplace(images->ft_addr, chosen,
						    "minpocket,sd-link", &link, sizeof(link));
			}'''),
])
