// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_BATTERY_H
#define MINPOCKET_BATTERY_H

#include <stdint.h>

enum {
    MP_BATTERY_WIDTH = 32, MP_BATTERY_HEIGHT = 20,
    MP_BATTERY_PIXELS = MP_BATTERY_WIDTH * MP_BATTERY_HEIGHT,
    MP_BATTERY_SMALL_WIDTH = 40, MP_BATTERY_SMALL_HEIGHT = 20,
    MP_BATTERY_SMALL_PIXELS = MP_BATTERY_SMALL_WIDTH * MP_BATTERY_SMALL_HEIGHT,
    MP_BATTERY_NATIVE_WIDTH = 60, MP_BATTERY_NATIVE_HEIGHT = 30,
    MP_BATTERY_NATIVE_PIXELS = MP_BATTERY_NATIVE_WIDTH * MP_BATTERY_NATIVE_HEIGHT,
    MP_BATTERY_PALETTE_SIZE = 13,
    MP_BATTERY_MAX_FRAMES = 33,
    MP_BATTERY_LOOP_MS = 3600,
    MP_BATTERY_REST_MS = 400,
    MP_BATTERY_SWEEP_MS = 2400
};

enum mp_battery_state {
    MP_BATTERY_UNKNOWN, MP_BATTERY_DISCHARGING, MP_BATTERY_CHARGING,
    MP_BATTERY_FULL, MP_BATTERY_NOT_CHARGING
};

struct mp_battery_reading {
    int level; /* -1 means unavailable, never an invented percentage. */
    enum mp_battery_state state;
};

/* Palette indices cache the approved artwork independently of a UI toolkit.
 * Zero is the surrounding surface, supplied by the renderer. No heap needed. */
struct mp_battery {
    struct mp_battery_reading reading;
    uint64_t epoch_ms;
    uint32_t revision;
    unsigned width, frame_count;
    uint8_t positions[MP_BATTERY_MAX_FRAMES];
    uint8_t frames[MP_BATTERY_MAX_FRAMES][MP_BATTERY_PIXELS];
};

extern const uint32_t mp_battery_palette[MP_BATTERY_PALETTE_SIZE];
void mp_battery_art(int level, int start, uint8_t pixels[MP_BATTERY_PIXELS]);
/* Cropped small variant: 18px bright body plus 2px lower shadow, with no
 * transparent top/bottom padding. Its charging shades deepen to Storm #414868.
 * The original 32x20 sprite and its palette indices stay unchanged. */
void mp_battery_small_art(const uint8_t source[MP_BATTERY_PIXELS],
                          uint8_t pixels[MP_BATTERY_SMALL_PIXELS]);
/* Native 30px version: cached casing plus direct percentage/sweep drawing.
 * start is a native pixel offset inside the fill, not a resized sprite. */
void mp_battery_native_art(int level, int start, uint8_t pixels[MP_BATTERY_NATIVE_PIXELS]);
void mp_battery_init(struct mp_battery *battery);
int mp_battery_update(struct mp_battery *battery, struct mp_battery_reading reading, uint64_t now_ms);
unsigned mp_battery_frame(const struct mp_battery *battery, uint64_t now_ms, int animate);
unsigned mp_battery_wait_ms(const struct mp_battery *battery, uint64_t now_ms, int animate);
struct mp_battery_reading mp_battery_read(const char *directory);
/* Call after the second confirmed display refresh. All sysfs reads run in
 * this separate child so a charger read cannot stall the presentation loop. */
int mp_battery_monitor_start(int display_fd);

#endif
