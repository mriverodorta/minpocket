// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_STATUS_H
#define MINPOCKET_STATUS_H

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Readers must see a complete record while the worker publishes its next stage. */
static inline int mp1_write_status(const char *path, const char *value)
{
    char temporary[512];
    int length = snprintf(temporary, sizeof temporary, "%s.XXXXXXXX", path);
    if (length < 0 || length >= (int)sizeof temporary) {
        errno = ENAMETOOLONG;
        return -1;
    }
    int fd = mkostemp(temporary, O_CLOEXEC);
    if (fd < 0)
        return -1;
    FILE *file = fdopen(fd, "w");
    int error = 0;
    if (!file) {
        error = errno;
        close(fd);
    } else {
        if (fprintf(file, "%s\n", value) < 0)
            error = errno;
        if (fclose(file) && !error)
            error = errno;
    }
    if (!error && !rename(temporary, path))
        return 0;
    if (!error)
        error = errno;
    unlink(temporary);
    errno = error;
    return -1;
}

#endif
