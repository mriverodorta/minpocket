// SPDX-License-Identifier: GPL-2.0-only
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

/* Clock queries run only when userspace reads the report after the first frame. */
static int mp1_cpu_clock_show(struct seq_file *file, void *unused)
{
	struct device_node *chosen = of_find_node_by_path("/chosen");
	struct device_node *cpu;
	struct clk *clock;
	u32 boot_hz;

	if (chosen && !of_property_read_u32(chosen, "minpocket,cpu-clock-hz", &boot_hz))
		seq_printf(file, "U-Boot ARMCLK Hz=%u\n", boot_hz);
	of_node_put(chosen);
	cpu = of_get_cpu_node(0, NULL);
	if (!cpu) {
		seq_puts(file, "Linux CPU clock unavailable: no CPU node\n");
		return 0;
	}
	clock = of_clk_get(cpu, 0);
	of_node_put(cpu);
	if (IS_ERR(clock)) {
		seq_printf(file, "Linux CPU clock unavailable: %ld\n", PTR_ERR(clock));
		return 0;
	}
	seq_printf(file, "Linux CPU clock Hz=%lu\n", clk_get_rate(clock));
	clk_put(clock);
	return 0;
}

static int __init mp1_cpu_clock_init(void)
{
	return proc_create_single("minpocket_cpu_clock", 0444, NULL, mp1_cpu_clock_show) ? 0 : -ENOMEM;
}
late_initcall(mp1_cpu_clock_init);
