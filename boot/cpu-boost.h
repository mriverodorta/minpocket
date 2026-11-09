/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/delay.h>
#include <power/regulator.h>

/* Run after relocation and device binding, before SD initialization/read. The
 * ROM, DDR loader and trusted firmware are unchanged. Supply errors skip the
 * boost. Leave a raised supply in place on clock errors rather than undervolting
 * an uncertain clock state. Linux's OPP regulator driver later owns the rail. */
static void mp1_cpu_boost(void)
{
	struct udevice *supply, *cru;
	struct clk cpu = { .id = ARMCLK };
	int uv;
	ulong hz;

	if (rockchip_get_clk(&cru))
		return;
	cpu.dev = cru;
	hz = clk_get_rate(&cpu);
	if (IS_ERR_VALUE(hz) || hz >= 1800000000)
		return;
	if (regulator_get_by_platname("vdd_cpu", &supply))
		return;
	uv = regulator_get_value(supply);
	if (uv < 712500 || uv > 1390000)
		return;
	if (uv < 1150000 && regulator_set_value(supply, 1150000))
		return;
	/* 125 mV rise at the board's 2300 uV/us ramp needs about 55 us. */
	udelay(100);
	uv = regulator_get_value(supply);
	if (uv < 1150000 || uv > 1390000)
		return;
	hz = clk_set_rate(&cpu, 1800000000);
	if (IS_ERR_VALUE(hz))
		return;
	/* The existing counter patch records the actual ARMCLK after mmc_init. */
}
