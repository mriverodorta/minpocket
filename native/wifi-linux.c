// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "wifi.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int read_number(const char *path, const char *format, void *number)
{
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    int success = fscanf(file, format, number) == 1;
    fclose(file);
    return success ? 0 : -1;
}

static int control_open(char *local, size_t size)
{
    static const char *const paths[] = {
        "/run/minpocket/network/wpa/wlan0", "/run/launchpad/network/wpa/wlan0",
        "/run/wpa_supplicant/wlan0"
    };
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) || fcntl(fd, F_SETFL, O_NONBLOCK)) goto fail;
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(local, size, "/run/minpocket-wifi-query-%ld", (long)getpid());
    snprintf(address.sun_path, sizeof address.sun_path, "%s", local);
    unlink(local);
    if (bind(fd, (struct sockaddr *)&address, sizeof address)) goto fail;
    for (unsigned i = 0; i < sizeof paths / sizeof paths[0]; ++i) {
        snprintf(address.sun_path, sizeof address.sun_path, "%s", paths[i]);
        if (!connect(fd, (struct sockaddr *)&address, sizeof address)) return fd;
    }
fail:
    close(fd);
    unlink(local);
    return -1;
}

static int query(int fd, const char *command, char *reply, size_t size)
{
    size_t length = strlen(command);
    if (send(fd, command, length, 0) != (ssize_t)length) return -1;
    struct pollfd request = {.fd = fd, .events = POLLIN};
    if (poll(&request, 1, 200) != 1 || !(request.revents & POLLIN)) return -1;
    ssize_t count = recv(fd, reply, size - 1, 0);
    if (count <= 0 || count == (ssize_t)size - 1) return -1;
    reply[count] = 0;
    return 0;
}

struct mp_wifi_reading mp_wifi_read(void)
{
    struct mp_wifi_reading reading = {.signal_dbm = MP_WIFI_NO_SIGNAL};
    int enabled = -1;
    if (read_number("/run/minpocket/network/enabled", "%d", &enabled))
        (void)read_number("/run/launchpad/network/enabled", "%d", &enabled);
    unsigned flags;
    if (enabled == 0) { reading.state = MP_WIFI_OFFLINE; return reading; }
    if (read_number("/sys/class/net/wlan0/flags", "%x", &flags)) return reading;
    if (!(flags & 1)) {
        reading.state = enabled == 1 ? MP_WIFI_CONNECTING : MP_WIFI_OFFLINE;
        return reading;
    }
    char local[108] = "", reply[2048];
    int fd = control_open(local, sizeof local);
    if (fd >= 0) {
        if (!query(fd, "STATUS", reply, sizeof reply)) reading.state = mp_wifi_parse_status(reply);
        if (reading.state == MP_WIFI_CONNECTED && !query(fd, "SIGNAL_POLL", reply, sizeof reply))
            reading.signal_dbm = mp_wifi_parse_signal(reply);
        close(fd);
        unlink(local);
    }
    /* A missing control socket never invents a signal strength. Carrier can
     * still establish that a link exists while the service is restarting. */
    if (reading.state == MP_WIFI_UNKNOWN) {
        int carrier = 0;
        if (!read_number("/sys/class/net/wlan0/carrier", "%d", &carrier) && carrier == 1)
            reading.state = MP_WIFI_CONNECTED;
        else
            /* The radio still exists. A temporary service-query failure must
             * not remove/reinsert its slot between connecting and connected. */
            reading.state = enabled == 1 ? MP_WIFI_CONNECTING : MP_WIFI_DISCONNECTED;
    }
    return reading;
}

static void report(struct mp_wifi_reading reading)
{
    const char *temporary = "/run/.minpocket-wifi-status";
    FILE *file = fopen(temporary, "w");
    if (!file) return;
    fprintf(file, "state=%s\nsignal_dbm=%d\n",
            mp_wifi_state_name(reading.state), reading.signal_dbm);
    if (fclose(file)) { unlink(temporary); return; }
    if (rename(temporary, "/run/minpocket-wifi-status")) unlink(temporary);
}

int mp_wifi_monitor_start(int display_fd, int battery_fd)
{
    int pipes[2];
    if (pipe(pipes)) return -1;
    for (unsigned i = 0; i < 2; ++i)
        if (fcntl(pipes[i], F_SETFD, FD_CLOEXEC) || fcntl(pipes[i], F_SETFL, O_NONBLOCK)) {
            close(pipes[0]); close(pipes[1]); return -1;
        }
    pid_t child = fork();
    if (child < 0) { close(pipes[0]); close(pipes[1]); return -1; }
    if (child) { close(pipes[1]); return pipes[0]; }
    close(pipes[0]);
    close(display_fd);
    if (battery_fd >= 0) close(battery_fd);
    umask(077);
    for (;;) {
        struct mp_wifi_reading reading = mp_wifi_read();
        report(reading);
        if (write(pipes[1], &reading, sizeof reading) < 0 && errno != EINTR && errno != EAGAIN)
            _exit(0);
        struct timespec remaining = {.tv_sec = 1};
        while (nanosleep(&remaining, &remaining) && errno == EINTR) { }
    }
}
