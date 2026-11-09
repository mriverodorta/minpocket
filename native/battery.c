// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "battery.h"
#include "battery-native30-art.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* MinPocket Default / Cyan Edge. Opaque, discrete Storm palette steps.
 * The three intermediate colors use rounded 25/50/75% blue-to-cyan mixes. */
const uint32_t mp_battery_palette[MP_BATTERY_PALETTE_SIZE] = {
    0x24283b, 0x1b1e2e, 0x1f2335, 0x414868, 0x7aa2f7,
    0x7dcfff, 0xc0caf5, 0x7badf9, 0x7cb9fb, 0x7cc4fd,
    /* Small-icon sweep: 25/50/75% steps from cyan toward Storm #414868. */
    0x6eadd9, 0x5f8cb4, 0x506a8e
};
static const uint8_t shade[] = {5, 5, 9, 9, 8, 8, 7, 7, 4, 4};

static void rectangle(uint8_t *pixels, int x, int y, int w, int h, uint8_t color)
{
    for (int row = y; row < y + h; ++row)
        for (int column = x; column < x + w; ++column)
            if (column >= 0 && column < MP_BATTERY_WIDTH && row >= 0 && row < MP_BATTERY_HEIGHT)
                pixels[row * MP_BATTERY_WIDTH + column] = color;
}

static void body(uint8_t *pixels, int dx, int dy, uint8_t color)
{
    rectangle(pixels, 2 + dx, 4 + dy, 26, 11, color);
    rectangle(pixels, 3 + dx, 3 + dy, 24, 13, color);
    rectangle(pixels, 28 + dx, 7 + dy, 2, 5, color);
}

void mp_battery_art(int level, int start, uint8_t pixels[MP_BATTERY_PIXELS])
{
    memset(pixels, 0, MP_BATTERY_PIXELS);
    body(pixels, 1, 2, 1);
    body(pixels, 0, 0, 4);
    rectangle(pixels, 3, 4, 24, 11, 2);
    rectangle(pixels, 3, 4, 24, 1, 3);
    rectangle(pixels, 3, 5, 1, 9, 1);
    rectangle(pixels, 3, 14, 24, 1, 3);
    rectangle(pixels, 3, 3, 1, 1, 6);
    rectangle(pixels, 2, 4, 1, 1, 6);
    unsigned width = level >= 0 && level <= 100 ? (22u * (unsigned)level + 50u) / 100u : 0;
    for (unsigned x = 0; x < width; ++x) {
        int offset = (int)x - start;
        uint8_t color = offset >= 0 && offset < (int)sizeof shade ? shade[offset] : 5;
        rectangle(pixels, 4 + (int)x, 5, 1, 9, color);
    }
}

void mp_battery_small_art(const uint8_t source[MP_BATTERY_PIXELS],
                          uint8_t pixels[MP_BATTERY_SMALL_PIXELS])
{
    for (unsigned y = 0; y < MP_BATTERY_SMALL_HEIGHT; ++y) {
        for (unsigned x = 0; x < MP_BATTERY_SMALL_WIDTH; ++x) {
            unsigned sx = 2 + x * 29 / MP_BATTERY_SMALL_WIDTH;
            unsigned sy = 3 + y * 15 / MP_BATTERY_SMALL_HEIGHT;
            uint8_t color = source[sy * MP_BATTERY_WIDTH + sx];
            /* Only recolor moving fill pixels. Keep casing, shadow, static
             * cyan, empty fill and original-size artwork unchanged. */
            if (sx >= 4 && sx < 26 && sy >= 5 && sy < 14) {
                if (color == 9) color = 10;
                else if (color == 8) color = 11;
                else if (color == 7) color = 12;
                else if (color == 4) color = 3;
            }
            pixels[y * MP_BATTERY_SMALL_WIDTH + x] = color;
        }
    }
}

void mp_battery_native_art(int level, int start, uint8_t pixels[MP_BATTERY_NATIVE_PIXELS])
{
    static const uint8_t shades[] = {5, 10, 11, 12, 3};
    enum { SWEEP_WIDTH = 21 };
    memcpy(pixels, battery_native_casing, MP_BATTERY_NATIVE_PIXELS);
    unsigned width = level >= 0 && level <= 100 ? (BATTERY_NATIVE_FILL_WIDTH * (unsigned)level + 50) / 100 : 0;
    for (unsigned x = 0; x < width; ++x) {
        int offset = (int)x - start;
        uint8_t color = offset >= 0 && offset < SWEEP_WIDTH ? shades[(unsigned)offset * 5 / SWEEP_WIDTH] : 5;
        for (unsigned y = 0; y < BATTERY_NATIVE_FILL_HEIGHT; ++y)
            pixels[(BATTERY_NATIVE_FILL_Y + y) * MP_BATTERY_NATIVE_WIDTH + BATTERY_NATIVE_FILL_X + x] = color;
    }
}

static void cache_frames(struct mp_battery *battery)
{
    int level = battery->reading.level;
    battery->width = level >= 0 ? (22u * (unsigned)level + 50u) / 100u : 0;
    battery->frame_count = 1;
    mp_battery_art(level, MP_BATTERY_WIDTH, battery->frames[0]);
    battery->positions[0] = 0;
    for (unsigned position = 0; position < battery->width + sizeof shade; ++position) {
        uint8_t pixels[MP_BATTERY_PIXELS];
        mp_battery_art(level, (int)position - (int)sizeof shade, pixels);
        unsigned frame;
        for (frame = 0; frame < battery->frame_count; ++frame)
            if (!memcmp(pixels, battery->frames[frame], sizeof pixels))
                break;
        if (frame == battery->frame_count) {
            memcpy(battery->frames[frame], pixels, sizeof pixels);
            battery->frame_count++;
        }
        battery->positions[position + 1] = (uint8_t)frame;
    }
}

void mp_battery_init(struct mp_battery *battery)
{
    battery->reading = (struct mp_battery_reading){-1, MP_BATTERY_UNKNOWN};
    battery->epoch_ms = 0;
    battery->revision = 1;
    cache_frames(battery);
}

int mp_battery_update(struct mp_battery *battery, struct mp_battery_reading reading, uint64_t now_ms)
{
    if (reading.level < 0 || reading.level > 100)
        reading.level = -1;
    if (reading.state < MP_BATTERY_UNKNOWN || reading.state > MP_BATTERY_NOT_CHARGING)
        reading.state = MP_BATTERY_UNKNOWN;
    if (reading.level == battery->reading.level && reading.state == battery->reading.state)
        return 0;
    int rebuild = reading.level != battery->reading.level;
    if (reading.state == MP_BATTERY_CHARGING && battery->reading.state != MP_BATTERY_CHARGING)
        battery->epoch_ms = now_ms;
    battery->reading = reading;
    battery->revision++;
    if (rebuild)
        cache_frames(battery);
    return 1;
}

static int moving(const struct mp_battery *battery, int animate)
{
    return animate && battery->reading.state == MP_BATTERY_CHARGING && battery->width > 0;
}

unsigned mp_battery_frame(const struct mp_battery *battery, uint64_t now_ms, int animate)
{
    if (!moving(battery, animate))
        return 0;
    uint64_t elapsed = now_ms >= battery->epoch_ms ? now_ms - battery->epoch_ms : 0;
    unsigned phase = (unsigned)(elapsed % MP_BATTERY_LOOP_MS);
    if (phase < MP_BATTERY_REST_MS || phase >= MP_BATTERY_REST_MS + MP_BATTERY_SWEEP_MS)
        return 0;
    unsigned position = (phase - MP_BATTERY_REST_MS) * (battery->width + sizeof shade) / MP_BATTERY_SWEEP_MS;
    return battery->positions[position + 1];
}

unsigned mp_battery_wait_ms(const struct mp_battery *battery, uint64_t now_ms, int animate)
{
    if (!moving(battery, animate))
        return UINT_MAX;
    unsigned current = mp_battery_frame(battery, now_ms, animate);
    /* There are at most 32 pixel positions per loop. Skip duplicate cached
     * frames, including the rest period, so idle artwork causes no flips. */
    uint64_t elapsed = now_ms >= battery->epoch_ms ? now_ms - battery->epoch_ms : 0;
    uint64_t cycle = elapsed / MP_BATTERY_LOOP_MS * MP_BATTERY_LOOP_MS;
    unsigned positions = battery->width + sizeof shade;
    for (unsigned pass = 0; pass < 2; ++pass, cycle += MP_BATTERY_LOOP_MS) {
        for (unsigned i = 0; i <= positions; ++i) {
            uint64_t edge = cycle + MP_BATTERY_REST_MS +
                ((uint64_t)i * MP_BATTERY_SWEEP_MS + positions - 1) / positions;
            if (edge > elapsed && mp_battery_frame(battery, battery->epoch_ms + edge, animate) != current)
                return (unsigned)(edge - elapsed);
        }
    }
    return UINT_MAX;
}

struct mp_battery_reading mp_battery_read(const char *directory)
{
    struct mp_battery_reading reading = {-1, MP_BATTERY_UNKNOWN};
    char path[256], value[64];
    int count = snprintf(path, sizeof path, "%s/capacity", directory);
    if (count < 0 || count >= (int)sizeof path)
        return reading;
    FILE *file = fopen(path, "r");
    if (file) {
        int level;
        if (fscanf(file, "%d", &level) == 1 && level >= 0 && level <= 100)
            reading.level = level;
        fclose(file);
    }
    count = snprintf(path, sizeof path, "%s/status", directory);
    if (count < 0 || count >= (int)sizeof path)
        return reading;
    file = fopen(path, "r");
    if (file) {
        if (fgets(value, sizeof value, file)) {
            value[strcspn(value, "\r\n")] = 0;
            if (!strcmp(value, "Charging")) reading.state = MP_BATTERY_CHARGING;
            else if (!strcmp(value, "Discharging")) reading.state = MP_BATTERY_DISCHARGING;
            else if (!strcmp(value, "Full")) reading.state = MP_BATTERY_FULL;
            else if (!strcmp(value, "Not charging")) reading.state = MP_BATTERY_NOT_CHARGING;
        }
        fclose(file);
    }
    return reading;
}

int mp_battery_monitor_start(int display_fd)
{
    int pipe_fds[2];
    if (pipe(pipe_fds))
        return -1;
    for (unsigned i = 0; i < 2; ++i) {
        if (fcntl(pipe_fds[i], F_SETFD, FD_CLOEXEC) ||
            fcntl(pipe_fds[i], F_SETFL, O_NONBLOCK)) {
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            return -1;
        }
    }
    pid_t child = fork();
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return -1;
    }
    if (child) {
        close(pipe_fds[1]);
        return pipe_fds[0];
    }
    close(pipe_fds[0]);
    close(display_fd);
    for (;;) {
        struct mp_battery_reading reading = mp_battery_read("/sys/class/power_supply/rk817-battery");
        if (write(pipe_fds[1], &reading, sizeof reading) < 0 && errno != EAGAIN && errno != EINTR)
            _exit(0);
        struct timespec remaining = {.tv_sec = 2};
        while (nanosleep(&remaining, &remaining) && errno == EINTR) {
        }
    }
}
