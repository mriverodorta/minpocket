// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "status.h"
#include "boot-profile.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define RUN "/run/minpocket-dev"
#define ROOT RUN "/root"
#define DATA_ROOT ROOT "/opt/minpocket-data"

/* This child uses a separate musl root. The network runtime uses glibc. */
static void start_data_setup(void)
{
    if (fork() != 0)
        return;
    if (mount("/dev", DATA_ROOT "/dev", NULL, MS_BIND, NULL) ||
        mount("/proc", DATA_ROOT "/proc", NULL, MS_BIND, NULL) ||
        mount("/sys", DATA_ROOT "/sys", NULL, MS_BIND, NULL) ||
        mount("/run", DATA_ROOT "/run", NULL, MS_BIND, NULL) ||
        chroot(DATA_ROOT) || chdir("/")) {
        perror("mount deferred Data tools");
        _exit(1);
    }
    execl("/sbin/minpocket-data", "minpocket-data", (char *)NULL);
    perror("execute deferred Data tools");
    _exit(127);
}

static int write_setting(const char *path, const char *value)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    size_t size = strlen(value);
    ssize_t written = write(fd, value, size);
    int error = written < 0 ? errno : EIO;
    close(fd);
    if (written == (ssize_t)size)
        return 0;
    errno = error;
    return -1;
}

static int bind_platform_driver(const char *driver, const char *device)
{
    char bind[256], value[128], actual[256], expected[256];
    int count = snprintf(value, sizeof value, "%s\n", device);
    if (count < 0 || count >= (int)sizeof value) {
        errno = ENAMETOOLONG;
        return -1;
    }
    snprintf(bind, sizeof bind, "/sys/bus/platform/drivers/%s/bind", driver);
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if (!write_setting(bind, value))
            return 0;
        int error = errno;
        if (error == EBUSY) {
            struct stat bound, wanted;
            snprintf(actual, sizeof actual, "/sys/bus/platform/devices/%s/driver", device);
            snprintf(expected, sizeof expected, "/sys/bus/platform/drivers/%s", driver);
            /* bind_store holds the device lock. EBUSY can mean automatic probing
             * won the race after we enabled post-frame readiness. */
            if (!stat(actual, &bound) && !stat(expected, &wanted) &&
                bound.st_dev == wanted.st_dev && bound.st_ino == wanted.st_ino) {
                fprintf(stderr, "%s already bound to %s; continuing Wi-Fi startup\n",
                        device, driver);
                return 0;
            }
        }
        /* The driver core maps a temporary supplier deferral to EAGAIN.
         * Retry only after that response, never delay a successful fast path. */
        if (error == EAGAIN && attempt + 1 < 100) {
            usleep(50000);
            continue;
        }
        errno = error;
        return -1;
    }
    errno = EAGAIN;
    return -1;
}

static void set_status(const char *stage)
{
    mp1_write_status(RUN "/status", stage);
}

static void fail_status(const char *stage)
{
    int error = errno;
    char status[64];
    snprintf(status, sizeof status, "%s ERROR %d", stage, error);
    set_status(status);
    _exit(1);
}

void mp1_counter_sample(uint64_t *counter, uint64_t *frequency)
{
#if defined(__aarch64__)
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r" (*counter));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r" (*frequency));
#else
    /* Non-AArch64 host builds have no MP1 hardware counter. */
    *counter = 0;
    *frequency = 0;
#endif
}

int mp1_start_late_drivers(void)
{
    return write_setting("/proc/minpocket_second_refresh", "2");
}

int mp1_sd_start(void)
{
    uint64_t start = mp1_boot_ns();
    if (write_setting("/sys/module/dw_mmc_rockchip/parameters/minpocket_sd_ready", "1\n") ||
        bind_platform_driver("dwmmc_rockchip", "fe2b0000.mmc"))
        return -1;
    char report[160];
    snprintf(report, sizeof report, "Linux SD bind start us=%llu end us=%llu duration us=%llu",
             (unsigned long long)(start / 1000),
             (unsigned long long)(mp1_boot_ns() / 1000),
             (unsigned long long)((mp1_boot_ns() - start) / 1000));
    mp1_write_status("/run/minpocket-sd-profile", report);
    return 0;
}

void mp1_launcher_report(const struct mp1_boot_profile *p, const struct mp1_display_profile *d)
{
    FILE *file = fopen("/run/minpocket-launcher-profile", "w");
    if (!file)
        return;
#define PHASE(label, a, b) fprintf(file, label " us=%llu\n", \
        (unsigned long long)(((b) - (a)) / 1000))
    fprintf(file, "Launcher entry us=%llu\n", (unsigned long long)(p->entry / 1000));
    PHASE("Devtmpfs mount", p->entry, p->devtmpfs);
    PHASE("Saved Home load and decode", p->devtmpfs, p->home_ready);
    PHASE("DRM open and first buffer", p->home_ready, p->drm_ready);
    PHASE("Draw first menu", p->drm_ready, p->drawn);
    PHASE("First modeset", p->drawn, p->modeset);
    PHASE("Confirm first refresh", p->modeset, p->refresh);
    PHASE("Post-first CPU boost release", p->refresh, p->boost_released);
    PHASE("Wait second refresh", p->boost_released, p->second_refresh);
    PHASE("Post-second proc sysfs and hostname", p->second_refresh, p->interfaces);
    PHASE("Launcher total", p->entry, p->refresh);
    PHASE("Launcher to second refresh", p->entry, p->second_refresh);
#undef PHASE
    fprintf(file, "DRM attempts=%u modeset attempts=%u\n", p->drm_attempts, p->modeset_attempts);
    fprintf(file, "Refresh sequences first=%u second=%u\n", p->first_sequence, p->second_sequence);
    fprintf(file, "DRM detail us open=%llu master=%llu discovery=%llu allocation=%llu "
            "framebuffer=%llu map_ioctl=%llu mmap=%llu\n",
            (unsigned long long)(d->open / 1000), (unsigned long long)(d->master / 1000),
            (unsigned long long)(d->discovery / 1000), (unsigned long long)(d->allocation / 1000),
            (unsigned long long)(d->framebuffer / 1000), (unsigned long long)(d->map_ioctl / 1000),
            (unsigned long long)(d->mmap / 1000));
    fclose(file);
}

void mp1_dev_status(char *status, size_t size)
{
    snprintf(status, size, "WAITING FOR SECOND REFRESH");
    FILE *file = fopen(RUN "/status", "r");
    if (!file)
        return;
    if (fgets(status, size, file))
        status[strcspn(status, "\r\n")] = 0;
    fclose(file);

    if (strcmp(status, "WAITING FOR WIFI ADDRESS"))
        return;
    file = fopen("/run/launchpad/network/address", "r");
    if (!file)
        return;
    char address[INET_ADDRSTRLEN];
    struct in_addr parsed;
    if (fscanf(file, "%15s", address) == 1 && inet_pton(AF_INET, address, &parsed) == 1)
        snprintf(status, size, "SSH %s", address);
    fclose(file);
}

static int mount_runtime(void)
{
    const char *pattern = "/sys/bus/platform/devices/fe2b0000.mmc/mmc_host/"
                          "mmc*/mmc*:*/block/mmcblk*/mmcblk*p3";
    for (unsigned i = 0; i < 50; ++i) {
        glob_t parts = {0};
        if (!glob(pattern, 0, NULL, &parts) && parts.gl_pathc == 1) {
            char path[1024], device[64];
            const char *part = parts.gl_pathv[0];
            int length = snprintf(path, sizeof path, "%s/uevent", part);
            snprintf(device, sizeof device, "/dev/%s", strrchr(part, '/') + 1);
            FILE *info = length < (int)sizeof path ? fopen(path, "r") : NULL;
            int matches = 0;
            if (info) {
                while (fgets(path, sizeof path, info))
                    if (!strcmp(path, "PARTNAME=minpocket-dev\n"))
                        matches = 1;
                fclose(info);
            }
            if (matches && !mount(device, ROOT, "ext4", MS_RDONLY | MS_NOSUID | MS_NODEV, NULL)) {
                globfree(&parts);
                return 0;
            }
        }
        globfree(&parts);
        usleep(100000);
    }
    return -1;
}

static void report_refresh(const struct mp1_boot_profile *profile)
{
    FILE *report = fopen(RUN "/boot-report", "w");
    if (!report)
        return;
    uint64_t counter = profile->first_counter, frequency = profile->frequency;
    long linux_ms = profile->refresh / 1000000;
    fprintf(report, "Linux to display refresh %ld ms\n", linux_ms);
    fprintf(report, "Linux to second refresh %llu ms\n",
            (unsigned long long)(profile->second_refresh / 1000000));
    FILE *chosen = fopen("/sys/firmware/devicetree/base/chosen/minpocket,boot-counter", "rb");
    uint32_t words[6];
    if (chosen && fread(words, sizeof words[0], 6, chosen) == 6) {
        uint64_t entry = ((uint64_t)ntohl(words[0]) << 32) | ntohl(words[1]);
        uint64_t handoff = ((uint64_t)ntohl(words[2]) << 32) | ntohl(words[3]);
        uint64_t rate = ((uint64_t)ntohl(words[4]) << 32) | ntohl(words[5]);
        if (rate && rate == frequency && entry && entry <= handoff &&
            handoff <= counter && counter - entry < rate * 60) {
            fprintf(report, "U-Boot entry to handoff %llu ms\n",
                    (unsigned long long)((handoff - entry) * 1000 / rate));
            fprintf(report, "U-Boot entry to refresh %llu ms\n",
                    (unsigned long long)((counter - entry) * 1000 / rate));
            fprintf(report, "U-Boot entry to second refresh %llu ms\n",
                    (unsigned long long)((profile->second_counter - entry) * 1000 / rate));
        }
    }
    if (chosen)
        fclose(chosen);
    fclose(report);
}

void mp1_dev_start(const struct mp1_boot_profile *profile, const struct mp1_display_profile *display)
{
    pid_t child = fork();
    if (child != 0)
        return;

    umask(077);
    if (mkdir(RUN, 0700))
        _exit(1);
    report_refresh(profile);
    mp1_launcher_report(profile, display);
    int log = open(RUN "/startup.log", O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    if (log >= 0) {
        dup2(log, STDOUT_FILENO);
        dup2(log, STDERR_FILENO);
        close(log);
    }
    if (mp1_start_late_drivers())
        perror("start deferred MP1 drivers");
    set_status("STARTING SD HOST");
    if (mp1_sd_start())
        fail_status("SD HOST");
    set_status("MOUNTING SD RUNTIME");
    if (mkdir(ROOT, 0700))
        fail_status("SD RUNTIME DIRECTORY");
    if (mount_runtime()) {
        set_status("SD RUNTIME MOUNT FAILED");
        _exit(1);
    }

    start_data_setup();
    set_status("STARTING WIFI HARDWARE");
    if (write_setting("/sys/module/firmware_class/parameters/path", ROOT "/lib/firmware"))
        fail_status("WIFI FIRMWARE PATH");
    if (write_setting("/sys/module/pwrseq_simple/parameters/minpocket_wifi_ready", "1\n") ||
        bind_platform_driver("pwrseq_simple", "wifi-pwrseq"))
        fail_status("WIFI POWER SEQUENCE");
    if (write_setting("/sys/module/dw_mmc_rockchip/parameters/minpocket_wifi_ready", "1\n") ||
        bind_platform_driver("dwmmc_rockchip", "fe2c0000.mmc"))
        fail_status("WIFI SDIO HOST");

    set_status("STARTING SSH RUNTIME");
    if (mkdir("/dev/pts", 0755) ||
        mount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "mode=0620") ||
        mount("/dev", ROOT "/dev", NULL, MS_BIND | MS_REC, NULL) ||
        mount("/proc", ROOT "/proc", NULL, MS_BIND, NULL) ||
        mount("/sys", ROOT "/sys", NULL, MS_BIND, NULL) ||
        mount("/run", ROOT "/run", NULL, MS_BIND, NULL) ||
        mount("tmpfs", ROOT "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777") ||
        chroot(ROOT) || chdir("/")) {
        fail_status("SSH RUNTIME MOUNT");
    }
    execl("/bin/minpocket-dev-start", "minpocket-dev-start", (char *)NULL);
    fail_status("SSH RUNTIME EXEC");
}
