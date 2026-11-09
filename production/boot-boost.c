// SPDX-License-Identifier: GPL-2.0-only
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/of_clk.h>
#include <linux/pm_qos.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

/* The MP1 has one shared CPU policy. A temporary minimum leaves thermal maximum
 * constraints effective. Schedutil resumes automatically when this request ends.
 * U-Boot sets the voltage before ARMCLK; cpufreq-dt owns subsequent sequencing. */
static DEFINE_MUTEX(mp1_boost_lock);
static struct freq_qos_request mp1_boot_min;
static bool released;
static unsigned long release_hz;
static int request_result;
static const char *release_reason = "pending";

static unsigned long mp1_cpu_hz(void)
{
	struct device_node *node = of_get_cpu_node(0, NULL);
	struct clk *clock;
	unsigned long hz;

	if (!node)
		return 0;
	clock = of_clk_get(node, 0);
	of_node_put(node);
	if (IS_ERR(clock))
		return 0;
	hz = clk_get_rate(clock);
	clk_put(clock);
	return hz;
}

static void mp1_release_boost(const char *reason)
{
	mutex_lock(&mp1_boost_lock);
	if (!released) {
		release_hz = mp1_cpu_hz();
		release_reason = reason;
		released = true;
		if (freq_qos_request_active(&mp1_boot_min))
			freq_qos_remove_request(&mp1_boot_min);
	}
	mutex_unlock(&mp1_boost_lock);
}

static void mp1_boost_timeout(struct work_struct *work)
{
	mp1_release_boost("timeout");
}
static DECLARE_DELAYED_WORK(mp1_boost_expiry, mp1_boost_timeout);

static int mp1_boost_policy(struct notifier_block *nb, unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;

	if (policy->cpu != 0)
		return NOTIFY_DONE;
	mutex_lock(&mp1_boost_lock);
	if (event == CPUFREQ_CREATE_POLICY && !released &&
	    !freq_qos_request_active(&mp1_boot_min))
		request_result = freq_qos_add_request(&policy->constraints, &mp1_boot_min,
					FREQ_QOS_MIN, min(policy->cpuinfo.max_freq, 1800000U));
	if (event == CPUFREQ_REMOVE_POLICY && freq_qos_request_active(&mp1_boot_min))
		freq_qos_remove_request(&mp1_boot_min);
	mutex_unlock(&mp1_boost_lock);
	return NOTIFY_OK;
}
static struct notifier_block mp1_boost_notifier = { .notifier_call = mp1_boost_policy };

static int mp1_boost_show(struct seq_file *file, void *unused)
{
	mutex_lock(&mp1_boost_lock);
	seq_printf(file, "Boot minimum kHz=1800000 active=%u result=%d\n",
		   freq_qos_request_active(&mp1_boot_min), request_result);
	seq_printf(file, "Release reason=%s CPU Hz=%lu\n", release_reason, release_hz);
	mutex_unlock(&mp1_boost_lock);
	seq_printf(file, "Current CPU Hz=%lu\n", mp1_cpu_hz());
	return 0;
}

static int mp1_boost_open(struct inode *inode, struct file *file)
{
	return single_open(file, mp1_boost_show, NULL);
}

static ssize_t mp1_boost_write(struct file *file, const char __user *buffer,
			       size_t count, loff_t *position)
{
	char value[2];

	if (count < 1 || count > sizeof(value))
		return -EINVAL;
	if (copy_from_user(value, buffer, count))
		return -EFAULT;
	if (value[0] != '0' || (count == 2 && value[1] != '\n'))
		return -EINVAL;
	mp1_release_boost("first-refresh");
	cancel_delayed_work_sync(&mp1_boost_expiry);
	return count;
}

static const struct proc_ops mp1_boost_ops = {
	.proc_open = mp1_boost_open,
	.proc_read = seq_read,
	.proc_write = mp1_boost_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init mp1_boost_init(void)
{
	int result = cpufreq_register_notifier(&mp1_boost_notifier, CPUFREQ_POLICY_NOTIFIER);

	if (result)
		return result;
	schedule_delayed_work(&mp1_boost_expiry, msecs_to_jiffies(5000));
	return 0;
}
subsys_initcall(mp1_boost_init);

static int __init mp1_boost_proc_init(void)
{
	return proc_create("minpocket_boot_boost", 0644, NULL, &mp1_boost_ops) ? 0 : -ENOMEM;
}
late_initcall(mp1_boost_proc_init);
