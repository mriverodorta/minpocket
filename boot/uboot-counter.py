"""Carry the main U-Boot entry and Linux handoff counter samples in the DT."""
from pathlib import Path
import sys

root = Path(sys.argv[1])


def insert(path, anchor, replacement):
    file = root / path
    text = file.read_text()
    if anchor not in text:
        raise SystemExit(f"U-Boot timing anchor missing in {path}")
    file.write_text(text.replace(anchor, replacement, 1))


insert(
    "arch/arm/include/asm/global_data.h",
    "struct arch_global_data {",
    """struct arch_global_data {
	u64 minpocket_entry_ticks;
	u64 minpocket_reloc_start_ticks;
	u64 minpocket_reloc_done_ticks;
	u64 minpocket_sd_start_ticks;
	ulong minpocket_cpu_hz;""",
)
insert(
    "common/board_f.c",
    "\tgd->flags = boot_flags;",
    '\tasm volatile("isb; mrs %0, cntvct_el0" : "=r" (gd->arch.minpocket_entry_ticks));\n'
    "\tgd->flags = boot_flags;",
)
insert(
    "common/board_f.c",
    "\tmemcpy(gd->new_gd, (char *)gd, sizeof(gd_t));",
    '\tasm volatile("isb; mrs %0, cntvct_el0" : "=r" (gd->arch.minpocket_reloc_start_ticks));\n'
    "\tmemcpy(gd->new_gd, (char *)gd, sizeof(gd_t));",
)
insert(
    "common/board_r.c",
    "void board_init_r(gd_t *new_gd, ulong dest_addr)\n{",
    'void board_init_r(gd_t *new_gd, ulong dest_addr)\n{\n'
    '\tasm volatile("isb; mrs %0, cntvct_el0" : "=r" (gd->arch.minpocket_reloc_done_ticks));',
)
insert("cmd/minpocket.c", "#include <blk.h>",
       "#include <asm/global_data.h>\n#include <asm/arch-rockchip/clock.h>\n"
       "#include <clk.h>\n#include <dt-bindings/clock/rk3568-cru.h>\n"
       "#include <linux/err.h>\n#include <blk.h>")
insert(
    "cmd/minpocket.c",
    "static ulong sd_ms;",
    "DECLARE_GLOBAL_DATA_PTR;\n\nstatic ulong sd_ms;",
)
insert(
    "cmd/minpocket.c",
    '\tulong start = get_timer(0);',
    '\tasm volatile("isb; mrs %0, cntvct_el0" : "=r" (gd->arch.minpocket_sd_start_ticks));\n'
    '\tulong start = get_timer(0);',
)
insert(
    "cmd/minpocket.c",
    "\tdisk = mmc_get_blk_desc(card);",
    """	{
		struct udevice *cru;
		struct clk cpu = { .id = ARMCLK };

		gd->arch.minpocket_cpu_hz = 0;
		if (!rockchip_get_clk(&cru)) {
			ulong hz;

			cpu.dev = cru;
			hz = clk_get_rate(&cpu);
			if (!IS_ERR_VALUE(hz))
				gd->arch.minpocket_cpu_hz = hz;
		}
	}
	disk = mmc_get_blk_desc(card);""",
)
insert(
    "arch/arm/lib/bootm.c",
    '\t\tif (chosen >= 0)\n',
    """		if (chosen >= 0) {
			u64 ticks, rate;
			fdt64_t samples[6];

			asm volatile("isb; mrs %0, cntvct_el0" : "=r" (ticks));
			asm volatile("mrs %0, cntfrq_el0" : "=r" (rate));
			samples[0] = cpu_to_fdt64(gd->arch.minpocket_entry_ticks);
			samples[1] = cpu_to_fdt64(ticks);
			samples[2] = cpu_to_fdt64(rate);
			samples[3] = cpu_to_fdt64(gd->arch.minpocket_reloc_start_ticks);
			samples[4] = cpu_to_fdt64(gd->arch.minpocket_reloc_done_ticks);
			samples[5] = cpu_to_fdt64(gd->arch.minpocket_sd_start_ticks);
			fdt_setprop_inplace(images->ft_addr, chosen,
					    "minpocket,boot-counter", samples, sizeof(samples));
			fdt_setprop_inplace_u32(images->ft_addr, chosen,
					       "minpocket,cpu-clock-hz", gd->arch.minpocket_cpu_hz);
		}
		if (chosen >= 0)
""",
)
