// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/minpocket-late.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

static DEFINE_MUTEX(mp1_late_lock);
static bool mp1_charger_started;
static bool mp1_devices_queued;

static void mp1_register(const char *name, int (*register_driver)(void))
{
	ktime_t start = ktime_get();
	int ret = register_driver();

	pr_info("minpocket-late: %s us=%lld rc=%d\n", name,
		ktime_us_delta(ktime_get(), start), ret);
}

static void mp1_start_charger(void)
{
	lockdep_assert_held(&mp1_late_lock);
	if (mp1_charger_started)
		return;
	mp1_charger_started = true;
	mp1_register("charger", minpocket_register_charger);
}

static void mp1_charger_timeout(struct work_struct *work)
{
	mutex_lock(&mp1_late_lock);
	if (!mp1_charger_started)
		pr_warn("minpocket-late: charger fallback timeout\n");
	mp1_start_charger();
	mutex_unlock(&mp1_late_lock);
}
static DECLARE_DELAYED_WORK(mp1_charger_work, mp1_charger_timeout);

static void mp1_start_power(struct work_struct *work)
{
	cancel_delayed_work(&mp1_charger_work);
	mutex_lock(&mp1_late_lock);
	mp1_start_charger();
	mutex_unlock(&mp1_late_lock);
	mp1_register("rtc", minpocket_register_rtc);
}
static DECLARE_WORK(mp1_power_work, mp1_start_power);

static void mp1_start_gpu(struct work_struct *work)
{
	mp1_register("gpu", minpocket_register_gpu);
}
static DECLARE_WORK(mp1_gpu_work, mp1_start_gpu);

static void mp1_start_audio(struct work_struct *work)
{
	/* Keep the audio dependency order within its own worker. */
	mp1_register("audio-codec", minpocket_register_codec);
	mp1_register("audio-i2s", minpocket_register_i2s);
	mp1_register("audio-amplifier", minpocket_register_amplifier);
	mp1_register("audio-card", minpocket_register_audio_card);
}
static DECLARE_WORK(mp1_audio_work, mp1_start_audio);

static ssize_t mp1_second_refresh(struct file *file, const char __user *buffer,
				size_t count, loff_t *position)
{
	char signal;
	bool start;

	if (count != 1)
		return -EINVAL;
	if (get_user(signal, buffer))
		return -EFAULT;
	if (signal != '2')
		return -EINVAL;

	mutex_lock(&mp1_late_lock);
	start = !mp1_devices_queued;
	mp1_devices_queued = true;
	mutex_unlock(&mp1_late_lock);
	if (start) {
		pr_info("minpocket-late: second refresh confirmed\n");
		queue_work(system_dfl_wq, &mp1_power_work);
		queue_work(system_dfl_wq, &mp1_gpu_work);
		queue_work(system_dfl_wq, &mp1_audio_work);
	}
	return count;
}

static const struct proc_ops mp1_refresh_ops = {
	.proc_write = mp1_second_refresh,
};

static int __init mp1_late_init(void)
{
	/* Apply the same second-refresh gate on battery and USB power. The
	 * charger probe samples current plug status, including a cable present
	 * before its IRQ handlers are registered. PMIC core/regulators stay early.
	 * Keep the existing bounded fallback if userspace cannot show a frame. */
	schedule_delayed_work(&mp1_charger_work, msecs_to_jiffies(1000));
	return proc_create("minpocket_second_refresh", 0200, NULL, &mp1_refresh_ops) ? 0 : -ENOMEM;
}
late_initcall(mp1_late_init);
