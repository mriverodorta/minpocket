// SPDX-License-Identifier: GPL-2.0-only
#include "wifi.h"
#include <string.h>
#include "../wifi-native30-art.h"

void mp_wifi_indicator_init(struct mp_wifi_indicator *indicator)
{
    memset(indicator, 0, sizeof *indicator);
    mp_wifi_init(&indicator->wifi);
}

void mp_wifi_indicator_update(struct mp_wifi_indicator *indicator,
                              struct mp_wifi_reading reading, uint64_t now_ms)
{
    int was_visible = indicator->wifi.reading.state != MP_WIFI_UNKNOWN;
    mp_wifi_update(&indicator->wifi, reading, now_ms);
    if (was_visible != (indicator->wifi.reading.state != MP_WIFI_UNKNOWN)) indicator->visibility_ms = now_ms;
}

static int measure(const void *context, const struct mp_theme *theme, struct mp_indicator_metrics *metrics)
{
    (void)context;
    unsigned width = theme->status.icon_height == 30 ? MP_WIFI_NATIVE_WIDTH : MP_WIFI_ICON_WIDTH;
    *metrics = (struct mp_indicator_metrics){width, theme->status.icon_height, width};
    return 0;
}

static void native_art(enum mp_wifi_state state, unsigned level, unsigned stage, uint8_t *pixels)
{
    static const uint8_t fade[] = {3, 12, 11, 10, 5};
    memcpy(pixels, wifi_native_pixels, MP_WIFI_NATIVE_PIXELS);
    for (unsigned i = 0; i < MP_WIFI_NATIVE_PIXELS; ++i) {
        if (pixels[i] < 3) continue;
        unsigned required = wifi_native_bands[i] + 1u;
        if (state != MP_WIFI_CONNECTED && state != MP_WIFI_CONNECTING) pixels[i] = 3;
        else if (required > level) pixels[i] = 3;
        else if (state == MP_WIFI_CONNECTING && required == level) pixels[i] = fade[stage];
    }
    if (state == MP_WIFI_OFFLINE) {
        for (unsigned y = 0; y < 27; ++y) {
            unsigned x = (26 - y) * 44 / 26;
            for (unsigned dx = 0; dx < 3; ++dx) pixels[y * MP_WIFI_NATIVE_WIDTH + x + dx] = 6;
        }
    }
}

static void tick(void *context, const struct mp_theme *theme, struct mp_ui_clock *clock,
                 struct mp_indicator_frame *frame)
{
    struct mp_wifi_indicator *indicator = context;
    if (theme->status.icon_height == 30 && indicator->native_revision != indicator->wifi.revision) {
        native_art(indicator->wifi.reading.state, indicator->wifi.level, 4, indicator->native_frames[0]);
        for (unsigned i = 0; i < MP_WIFI_JOIN_FRAMES; ++i)
            native_art(MP_WIFI_CONNECTING, i / 5, i % 5, indicator->native_frames[1 + i]);
        indicator->native_revision = indicator->wifi.revision;
    }
    struct mp_wifi_visual visual = mp_wifi_visual(&indicator->wifi, clock->now_ms, clock->animate);
    indicator->frame = visual.frame;
    mp_ui_clock_request(clock, mp_wifi_wait_ms(&indicator->wifi, clock->now_ms, clock->animate));
    *frame = (struct mp_indicator_frame){.visible = indicator->wifi.reading.state != MP_WIFI_UNKNOWN,
        .stamp = visual.stamp, .visibility_ms = indicator->visibility_ms};
}

static void paint(const void *context, const struct mp_theme *theme, struct mp_ui_index_surface *window)
{
    const struct mp_wifi_indicator *indicator = context;
    if (theme->status.icon_height == 30)
        mp_ui_index_sprite(window, 0, 0, indicator->native_frames[indicator->frame],
            MP_WIFI_NATIVE_WIDTH, MP_WIFI_NATIVE_HEIGHT, MP_WIFI_NATIVE_WIDTH);
    else
        mp_ui_index_sprite(window, 0, 0, indicator->wifi.frames[indicator->frame],
            MP_WIFI_ICON_WIDTH, MP_WIFI_ICON_HEIGHT, MP_WIFI_ICON_WIDTH);
}

const struct mp_status_indicator_ops mp_wifi_indicator_ops = {measure, tick, paint};
