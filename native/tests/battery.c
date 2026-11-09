// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "battery.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void property(const char *directory, const char *name, const char *value)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", directory, name);
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs(value, file) >= 0);
    assert(!fclose(file));
}

int main(int argc, char **argv)
{
    static const int levels[] = {-1, 0, 1, 5, 50, 70, 99, 100};
    struct mp_battery battery;
    if (argc == 2 && !strcmp(argv[1], "--palette")) {
        for (unsigned i = 0; i < 10; ++i)
            printf("#%06x\n", mp_battery_palette[i]);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--reference")) {
        for (unsigned l = 0; l < sizeof levels / sizeof levels[0]; ++l) {
            mp_battery_init(&battery);
            mp_battery_update(&battery, (struct mp_battery_reading){levels[l], MP_BATTERY_CHARGING}, 0);
            for (unsigned t = 0; t <= MP_BATTERY_LOOP_MS; t += 100)
                assert(fwrite(battery.frames[mp_battery_frame(&battery, t, 1)], 1,
                              MP_BATTERY_PIXELS, stdout) == MP_BATTERY_PIXELS);
        }
        return 0;
    }
    mp_battery_init(&battery);
    assert(battery.reading.level == -1);
    assert(mp_battery_wait_ms(&battery, 0, 1) == UINT_MAX);
    for (int level = -1; level <= 100; ++level) {
        mp_battery_init(&battery);
        assert(mp_battery_update(&battery, (struct mp_battery_reading){level, MP_BATTERY_CHARGING}, 100));
        assert(battery.frame_count <= MP_BATTERY_MAX_FRAMES);
        for (unsigned t = 0; t <= MP_BATTERY_LOOP_MS * 2; t += 13) {
            unsigned frame = mp_battery_frame(&battery, 100 + t, 1);
            assert(frame < battery.frame_count);
            const uint8_t *pixels = battery.frames[frame];
            for (unsigned y = 0; y < MP_BATTERY_HEIGHT; ++y) {
                for (unsigned x = 0; x < MP_BATTERY_WIDTH; ++x) {
                    unsigned i = y * MP_BATTERY_WIDTH + x;
                    assert(pixels[i] < 10);
                    if (x < 4 || x >= 4 + battery.width || y < 5 || y >= 14)
                        assert(pixels[i] == battery.frames[0][i]);
                    else
                        assert(pixels[i] == pixels[5 * MP_BATTERY_WIDTH + x]);
                }
            }
        }
        for (unsigned t = 0; t <= MP_BATTERY_LOOP_MS; t += 113) {
            unsigned now = 100 + t;
            unsigned current = mp_battery_frame(&battery, now, 1);
            unsigned wait = mp_battery_wait_ms(&battery, now, 1);
            if (battery.width == 0) {
                assert(wait == UINT_MAX);
                continue;
            }
            assert(wait > 0 && wait <= MP_BATTERY_LOOP_MS);
            for (unsigned ms = 1; ms < wait; ++ms)
                assert(mp_battery_frame(&battery, now + ms, 1) == current);
            assert(mp_battery_frame(&battery, now + wait, 1) != current);
        }
        assert(mp_battery_frame(&battery, 700, 0) == 0);
        assert(mp_battery_wait_ms(&battery, 700, 0) == UINT_MAX);
        assert(!mp_battery_update(&battery, (struct mp_battery_reading){level, MP_BATTERY_CHARGING}, 700));
        mp_battery_update(&battery, (struct mp_battery_reading){level, MP_BATTERY_FULL}, 800);
        assert(mp_battery_frame(&battery, 900, 1) == 0);
        assert(mp_battery_wait_ms(&battery, 900, 1) == UINT_MAX);
        mp_battery_update(&battery, (struct mp_battery_reading){level, MP_BATTERY_DISCHARGING}, 900);
        assert(mp_battery_wait_ms(&battery, 1000, 1) == UINT_MAX);
        mp_battery_update(&battery, (struct mp_battery_reading){level, MP_BATTERY_CHARGING}, 2000);
        assert(battery.epoch_ms == 2000);
    }
    mp_battery_update(&battery, (struct mp_battery_reading){101, MP_BATTERY_CHARGING}, 0);
    assert(battery.reading.level == -1 && battery.width == 0);
    mp_battery_update(&battery, (struct mp_battery_reading){75, MP_BATTERY_CHARGING}, 0);
    unsigned dark_sweep_pixels = 0, new_shades = 0;
    for (unsigned frame = 0; frame < battery.frame_count; ++frame) {
        uint8_t small[MP_BATTERY_SMALL_PIXELS + 2];
        memset(small, 0xff, sizeof small);
        mp_battery_small_art(battery.frames[frame], small + 1);
        assert(small[0] == 0xff && small[sizeof small - 1] == 0xff);
        unsigned first = MP_BATTERY_SMALL_HEIGHT, last = 0;
        unsigned bright_first = MP_BATTERY_SMALL_HEIGHT, bright_last = 0;
        for (unsigned y = 0; y < MP_BATTERY_SMALL_HEIGHT; ++y)
            for (unsigned x = 0; x < MP_BATTERY_SMALL_WIDTH; ++x) {
                uint8_t color = small[1 + y * MP_BATTERY_SMALL_WIDTH + x];
                assert(color < MP_BATTERY_PALETTE_SIZE);
                if (color) {
                    if (y < first) first = y;
                    if (y > last) last = y;
                }
                if (color >= 4) {
                    if (y < bright_first) bright_first = y;
                    if (y > bright_last) bright_last = y;
                }
                /* Animated changes must stay inside the original 75% fill. */
                unsigned sx = 2 + x * 29 / MP_BATTERY_SMALL_WIDTH;
                unsigned sy = 3 + y * 15 / MP_BATTERY_SMALL_HEIGHT;
                if (sx < 4 || sx >= 4 + battery.width || sy < 5 || sy >= 14)
                    assert(color == battery.frames[0][sy * MP_BATTERY_WIDTH + sx]);
                else {
                    if (color == 3) ++dark_sweep_pixels;
                    if (color >= 10) new_shades |= 1u << (color - 10);
                }
            }
        assert(first == 0 && last == 19);
        assert(bright_first == 0 && bright_last == 17 && bright_last - bright_first + 1 == 18);
    }
    assert(dark_sweep_pixels > 0 && new_shades == 7);
    char directory[] = "/tmp/minpocket-battery-test.XXXXXX";
    assert(mkdtemp(directory));
    property(directory, "capacity", "73\n");
    property(directory, "status", "Charging\n");
    struct mp_battery_reading reading = mp_battery_read(directory);
    assert(reading.level == 73 && reading.state == MP_BATTERY_CHARGING);
    property(directory, "status", "Full\n");
    assert(mp_battery_read(directory).state == MP_BATTERY_FULL);
    property(directory, "status", "Not charging\n");
    assert(mp_battery_read(directory).state == MP_BATTERY_NOT_CHARGING);
    property(directory, "capacity", "invalid\n");
    property(directory, "status", "Unknown\n");
    reading = mp_battery_read(directory);
    assert(reading.level == -1 && reading.state == MP_BATTERY_UNKNOWN);
    char path[256];
    snprintf(path, sizeof path, "%s/capacity", directory); assert(!unlink(path));
    snprintf(path, sizeof path, "%s/status", directory); assert(!unlink(path));
    assert(!rmdir(directory));
    assert(mp_battery_read(directory).level == -1);
    printf("Battery lifecycle, clipping, cache, deadlines, reduced motion and sysfs states passed. Cache storage: %zu bytes.\n", sizeof battery);
    return 0;
}
