// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "status.h"
#include <fcntl.h>
#include <glob.h>
#include <inttypes.h>
#include <libfdisk/libfdisk.h>
#include <linux/blkpg.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define SECTOR 512
#define DATA_START 294912
#define BOOT_TYPE "A60B0000-0000-4C7E-8000-015E00004DB7"
#define LINUX_TYPE "0FC63DAF-8483-4772-8E79-3D69D8477DE4"
#define DATA_TYPE "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"

static void status(const char *value)
{
    mp1_write_status("/run/minpocket-data.status", value);
}

static int open_sd(char *disk, size_t length)
{
    const char *pattern = "/sys/bus/platform/devices/fe2b0000.mmc/mmc_host/"
                          "mmc*/mmc*:*/block/mmcblk*/mmcblk*p2";
    for (unsigned i = 0; i < 50; ++i) {
        glob_t paths = {0};
        int fd = -1;
        if (!glob(pattern, 0, NULL, &paths) && paths.gl_pathc == 1) {
            const char *name = strrchr(paths.gl_pathv[0], '/') + 1;
            int written = snprintf(disk, length, "/dev/%.*s", (int)strlen(name) - 2, name);
            if (written > 0 && (size_t)written < length)
                fd = open(disk, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
        }
        globfree(&paths);
        if (fd >= 0)
            return fd;
        usleep(100000);
    }
    return -1;
}

static int partition_matches(struct fdisk_context *context, size_t index,
                             const char *name, const char *type, uint64_t start,
                             uint64_t size)
{
    struct fdisk_partition *part = NULL;
    if (fdisk_get_partition(context, index, &part) || !part)
        return 0;
    const char *label = fdisk_partition_get_name(part);
    struct fdisk_parttype *kind = fdisk_partition_get_type(part);
    const char *guid = kind ? fdisk_parttype_get_string(kind) : NULL;
    int matches = label && !strcmp(label, name) && guid && !strcasecmp(guid, type) &&
                  fdisk_partition_get_start(part) == start &&
                  (!size || fdisk_partition_get_size(part) == size);
    fdisk_unref_partition(part);
    return matches;
}

static int known_layout(struct fdisk_context *context, int development, int *existing)
{
    size_t index = development ? 3 : 2;
    struct fdisk_table *table = NULL;
    if (fdisk_get_partitions(context, &table) || !table)
        return 0;
    size_t count = fdisk_table_get_nents(table);
    fdisk_unref_table(table);
    if (count != index && count != index + 1)
        return 0;
    *existing = count == index + 1;
    return partition_matches(context, 0, "uboot", BOOT_TYPE, 16384, 8192) &&
           partition_matches(context, 1, "minpocket", LINUX_TYPE, 32768,
                             development ? 196608 : 262144) &&
           (!development || partition_matches(context, 2, "minpocket-dev",
                                               LINUX_TYPE, 229376, 65536)) &&
           (!*existing || partition_matches(context, index, "Data", DATA_TYPE, DATA_START, 0));
}

static int add_data_entry(struct fdisk_context *context, size_t index, uint64_t sectors)
{
    struct fdisk_partition *part = fdisk_new_partition();
    if (!part)
        return -1;
    struct fdisk_parttype *type = fdisk_label_get_parttype_from_string(
        fdisk_get_label(context, "gpt"), DATA_TYPE);
    int ret = -1;
    if (type && !fdisk_partition_set_partno(part, index) &&
        !fdisk_partition_set_start(part, DATA_START) &&
        !fdisk_partition_set_size(part, sectors) &&
        !fdisk_partition_size_explicit(part, 1) &&
        !fdisk_partition_set_name(part, "Data") &&
        !fdisk_partition_set_type(part, type)) {
        size_t actual;
        if (!fdisk_add_partition(context, part, &actual) && actual == index &&
            partition_matches(context, index, "Data", DATA_TYPE, DATA_START, sectors))
            ret = 0;
    }
    fdisk_unref_partition(part);
    return ret;
}

static int format_data(int whole, const char *disk, int number, uint64_t bytes)
{
    char partition[72];
    int written = snprintf(partition, sizeof partition, "%sp%d", disk, number);
    if (written < 0 || (size_t)written >= sizeof partition)
        return -1;

    int fd = -1;
    for (unsigned i = 0; i < 50 && fd < 0; ++i) {
        fd = open(partition, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0)
            usleep(20000);
    }
    if (fd < 0)
        return -1;
    struct stat st;
    uint64_t actual, disk_seq, part_seq;
    int valid = !fstat(fd, &st) && S_ISBLK(st.st_mode) &&
                !ioctl(fd, BLKGETSIZE64, &actual) && actual == bytes &&
                !ioctl(whole, BLKGETDISKSEQ, &disk_seq) &&
                !ioctl(fd, BLKGETDISKSEQ, &part_seq) && part_seq == disk_seq;
    if (!valid) {
        close(fd);
        return -1;
    }

    pid_t child = fork();
    if (!child) {
        if (fcntl(fd, F_SETFD, 0) == -1)
            _exit(127);
        char pinned[32];
        snprintf(pinned, sizeof pinned, "/proc/self/fd/%d", fd);
        execl("/usr/sbin/mkfs.exfat", "mkfs.exfat", "-L", "Data", pinned, (char *)NULL);
        _exit(127);
    }
    close(fd);
    if (child < 0)
        return -1;
    int result;
    return waitpid(child, &result, 0) == child && WIFEXITED(result) && !WEXITSTATUS(result) ? 0 : -1;
}

static int provision(struct fdisk_context *context, int fd, const char *disk, int development)
{
    int existing;
    if (!fdisk_is_label(context, GPT) || fdisk_get_sector_size(context) != SECTOR ||
        !known_layout(context, development, &existing))
        return -1;
    if (existing) {
        status("existing");
        return 0;
    }

    uint64_t last = fdisk_get_last_lba(context);
    uint64_t capacity = fdisk_get_nsectors(context);
    if (last < DATA_START + 16383 || capacity < 34 || last != capacity - 34)
        return -1;
    uint64_t sectors = last - DATA_START + 1;
    size_t index = development ? 3 : 2;
    status("initializing");
    /* Move the original backup GPT out of the region before formatting it. */
    if (fdisk_write_disklabel(context) || fsync(fd) || add_data_entry(context, index, sectors))
        return -1;

    /* Format before publishing the GPT entry. A restart can repeat this step. */
    struct blkpg_partition part = {
        .start = (int64_t)DATA_START * SECTOR,
        .length = (int64_t)sectors * SECTOR,
        .pno = (int)index + 1,
    };
    struct blkpg_ioctl_arg command = {
        .op = BLKPG_ADD_PARTITION, .datalen = sizeof part, .data = &part,
    };
    if (ioctl(fd, BLKPG, &command))
        return -1;
    if (format_data(fd, disk, part.pno, sectors * SECTOR)) {
        command.op = BLKPG_DEL_PARTITION;
        ioctl(fd, BLKPG, &command);
        return -1;
    }
    if (fdisk_write_disklabel(context) || fsync(fd))
        return -1;
    status("initialized");
    return 0;
}

int main(void)
{
    int development = !access("/sys/firmware/devicetree/base/chosen/minipocket,development", F_OK);
    char disk[64];
    int fd = open_sd(disk, sizeof disk);
    /* Keep the lock across GPT inspection, formatting, and the final GPT write.
     * A second invocation must not overwrite the active worker's status. */
    if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB)) {
        int error = errno;
        close(fd);
        if (error == EWOULDBLOCK) {
            fprintf(stderr, "Data setup already running\n");
            return 0;
        }
        status("failed");
        return 1;
    }
    struct fdisk_context *context = fdisk_new_context();
    struct stat st;
    int ret = -1;
    if (context && fd >= 0 && !fstat(fd, &st) && S_ISBLK(st.st_mode)) {
        fdisk_disable_dialogs(context, 1);
        if (!fdisk_assign_device_by_fd(context, fd, disk, 0))
            ret = provision(context, fd, disk, development);
    }
    if (context)
        fdisk_unref_context(context);
    if (fd >= 0)
        close(fd);
    if (ret)
        status("failed");
    return ret != 0;
}
