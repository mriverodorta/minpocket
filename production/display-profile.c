// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init.h>
#include <linux/minpocket-display.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/string.h>

static DEFINE_SPINLOCK(mp_display_lock);
static struct mp_display_sample {
	u64 start, end;
	int result;
} mp_display_samples[MP_DISPLAY_PHASES];

void minpocket_display_record(enum minpocket_display_phase phase, u64 start, int result)
{
	u64 end = ktime_get_boottime_ns();
	unsigned long flags;

	spin_lock_irqsave(&mp_display_lock, flags);
	/* Preserve the first startup measurements across later refresh-rate changes. */
	if (!mp_display_samples[phase].end)
		mp_display_samples[phase] = (struct mp_display_sample){ start, end, result };
	spin_unlock_irqrestore(&mp_display_lock, flags);
}

static int mp_display_show(struct seq_file *file, void *unused)
{
	static const char * const names[] = {
		"panel_power_reset", "panel_commands", "panel_prepare",
		"dsi_config", "dsi_phy_init", "dsi_phy_power", "dsi_lock", "dsi_stop",
		"drm_enable", "drm_planes", "drm_flip_done", "drm_cleanup",
	};
	struct mp_display_sample samples[MP_DISPLAY_PHASES];
	unsigned long flags;
	int i;

	spin_lock_irqsave(&mp_display_lock, flags);
	memcpy(samples, mp_display_samples, sizeof(samples));
	spin_unlock_irqrestore(&mp_display_lock, flags);
	for (i = 0; i < MP_DISPLAY_PHASES; i++)
		if (samples[i].end)
			seq_printf(file, "%s start_us=%llu end_us=%llu duration_us=%llu rc=%d\n",
				names[i], samples[i].start / 1000, samples[i].end / 1000,
				(samples[i].end - samples[i].start) / 1000, samples[i].result);
	return 0;
}

static int __init mp_display_init(void)
{
	return proc_create_single("minpocket_display_profile", 0444, NULL, mp_display_show) ? 0 : -ENOMEM;
}
late_initcall(mp_display_init);
