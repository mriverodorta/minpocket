// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_BOOT_PROFILE_H
#define MINPOCKET_BOOT_PROFILE_H

#include <stdint.h>
#include <time.h>

static inline uint64_t mp1_boot_ns(void)
{
    struct timespec value;
    clock_gettime(CLOCK_BOOTTIME, &value);
    return (uint64_t)value.tv_sec * 1000000000 + value.tv_nsec;
}

struct mp1_boot_profile {
    uint64_t entry, devtmpfs, home_ready, drm_ready, drawn, modeset, interfaces, refresh, boost_released, second_refresh;
    uint64_t first_counter, second_counter, frequency;
    uint32_t first_sequence, second_sequence;
    unsigned drm_attempts, modeset_attempts;
};

struct mp1_display_profile {
    uint64_t open, master, discovery, allocation, framebuffer, map_ioctl, mmap;
};

void mp1_launcher_report(const struct mp1_boot_profile *, const struct mp1_display_profile *);
int mp1_sd_start(void);
int mp1_start_late_drivers(void);
void mp1_counter_sample(uint64_t *, uint64_t *);
void mp1_dev_start(const struct mp1_boot_profile *, const struct mp1_display_profile *);

#endif
