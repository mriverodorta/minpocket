"""MP1 CPU boot boost using the board's TCS4525 supply and RK3566 OPP."""
from pathlib import Path
import sys

root = Path(sys.argv[1])


def patch(name, old, new):
    path = root / name
    source = path.read_text()
    if source.count(old) != 1:
        raise SystemExit(f"CPU boost anchor mismatch in {name}: {old}")
    path.write_text(source.replace(old, new, 1))


# TCS4525 register addresses, 7-bit selector and 6.25 mV steps match Linux's
# drivers/regulator/fan53555.c. Never use the FAN53555 register map for this IC.
patch("include/power/fan53555.h", "\tFAN53555_VENDOR_SILERGY,",
      "\tFAN53555_VENDOR_SILERGY,\n\tFAN53555_VENDOR_TCS,")
patch("drivers/power/pmic/fan53555.c", "\t{ },\n};",
      '\t{ .compatible = "tcs,tcs4525", .data = FAN53555_VENDOR_TCS, },\n\t{ },\n};')
regulator = "drivers/power/regulator/fan53555.c"
patch(regulator, "\t/* Option 00 */",
      "\t{ FAN53555_VENDOR_TCS, 0xc, 0x0, false, 600000, 6250 },\n\t/* Option 00 */")
patch(regulator, "\treturn 0;\n}\n\nstatic int fan53555_regulator_get_value",
      """	if (dev->driver_data == FAN53555_VENDOR_TCS) {
		/* Logical VSEL0/1 map to TCS4525 registers 0x11/0x10. */
		plat->vol_reg = plat->vol_reg ? 0x10 : 0x11;
		plat->sleep_reg = plat->sleep_reg ? 0x10 : 0x11;
	}
	return 0;
}

static int fan53555_regulator_get_value""")
patch(regulator, "(reg & 0x3f)",
      "(reg & (priv->vendor == FAN53555_VENDOR_TCS ? 0x7f : 0x3f))")
patch(regulator, "\tu8 vol;\n\n\tvol = (uV - priv->vsel_min) / priv->vsel_step;",
      """	u8 vol;
	u8 mask = priv->vendor == FAN53555_VENDOR_TCS ? 0x7f : 0x3f;

	if (uV < priv->vsel_min || (uV - priv->vsel_min) % priv->vsel_step ||
	    (uV - priv->vsel_min) / priv->vsel_step > mask)
		return -EINVAL;
	vol = (uV - priv->vsel_min) / priv->vsel_step;""")
patch(regulator, "pdata->vol_reg, GENMASK(6, 0), vol", "pdata->vol_reg, mask, vol")

clock = "drivers/clk/rockchip/clk_rk3568.c"
# Match Linux clk-rk3568.c: ACLK divisor 1, peripheral/debug divisors 7.
patch(clock, "\tRK3568_CPUCLK_RATE(1416000000, 1, 5),",
      "\tRK3568_CPUCLK_RATE(1800000000, 1, 7),\n\tRK3568_CPUCLK_RATE(1416000000, 1, 5),")
patch(clock, "\tRK3036_PLL_RATE(1608000000, 1, 67, 1, 1, 1, 0),",
      "\tRK3036_PLL_RATE(1800000000, 1, 75, 1, 1, 1, 0),\n"
      "\tRK3036_PLL_RATE(1608000000, 1, 67, 1, 1, 1, 0),")
# The generic driver silently caches the first request. This board requires an
# actual transition, checked errors, and a subsequent hardware rate readback.
patch(clock, "\t\tif (priv->armclk_hz)\n\t\t\trk3568_armclk_set_clk(priv, rate);\n\t\tpriv->armclk_hz = rate;",
      "\t\tret = rk3568_armclk_set_clk(priv, rate);\n\t\tif (ret)\n\t\t\treturn ret;\n\t\tpriv->armclk_hz = rate;")

loader = "cmd/minpocket.c"
patch(loader, '#include "minpocket-layout.h"',
      '#include "minpocket-layout.h"\n#include "minpocket-cpu-boost.h"')
patch(loader, "\tulong start = get_timer(0);",
      "\tmp1_cpu_boost();\n\tulong start = get_timer(0);")
(root / "cmd/minpocket-cpu-boost.h").write_text(
    (Path(__file__).parent / "cpu-boost.h").read_text())
